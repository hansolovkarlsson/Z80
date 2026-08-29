// abcbus/disk.c - the synthetic ABC-bus disk controller.
// See disk.h for why this models the protocol rather than the card, and
// why it is shared rather than owned by a machine.
//
// The protocol below is reimplemented from sasq64/abc80sim's own synthetic
// controller (src/disk.c, src/abcio.c) - the same source ABC80's Milestone
// 6 already leaned on for its sector interleave. It is not copied, and
// every claim it makes was cross-checked against real DOS ROMs,
// disassembled with this project's bin/z80dasm. Three details match
// exactly rather than approximately in the ABC802's ROM:
//
//   1. The command byte is a bitmask, and the ROM's only two command
//      constants decode cleanly under it. L6080 issues 0x03 and L608D
//      issues 0x0C; with 0x01=read, 0x02=to-host, 0x04=from-host,
//      0x08=write, those are exactly "read then send" and "receive then
//      write". Read and write composed from primitives, not two arbitrary
//      opcodes that happen to fit.
//   2. The device-select mask matches: abc80sim does `sel = value & 0x3f`,
//      the ROM does `AND 3Fh` at 0x6172 immediately before `OUT (01h),A`.
//   3. The device-select values match: 0x2C/0x2D/0x2E appear in the ROM's
//      own select table at 0x61DA-0x61FB.
//
// The ABC80's ABC-DOS ROM independently confirms all three, five years
// earlier in hardware terms and in a completely separate code base: the
// same 0x03 and 0x0C command constants (0x6071 and 0x60AA), the same
// four-byte B/C/D/E header (0x6136-0x6142), and the same 0x2D select code
// - hardcoded there, since that ROM only ever talks to an ABC830. Two
// unrelated ROMs agreeing on a bitmask is what makes it a bitmask rather
// than a coincidence.
//
// The status byte is likewise pinned by what the ROM actually tests:
//
//   - ABC802 `L6196` polls with `IN A,(01h)` then `INC A / JR Z` and
//     `DEC A / JR Z`: a status of 0xFF *or* 0x00 is taken as "no device"
//     and the poll bails immediately. So a present controller must never
//     report either. This is precisely why the pre-existing "every
//     ABC-bus read returns 0xFF" behavior reads as no card fitted -
//     correctly.
//   - ABC802 `L616F` waits for `(STAT & 0x80) == 0x80` before issuing a
//     command, so bit 7 means "idle, ready for a command header".
//   - The transfer loops - ABC802 `L612D`/`L6140`, ABC80 `0x6144`/`0x614D`
//     - all do `IN A,(01h)` then rotate bit 0 into carry, so bit 0 means
//     "ready to move a byte".
//   - After the header, ABC802 `L6107` waits for either bit 7 or
//     `(STAT & 0x05) == 0x01`.
//
// Bit 3 needed the ABC80's ROM to pin down, and it is the one bit an
// ABC802-only reading gets backwards. Three tests fix it:
//
//   - ABC80 `0x6118`, after a command header: `BIT 3,A / JR Z` treats bit 3
//     *clear* as "this command produced nothing", and jumps away to read
//     the result. So the bit must be SET while a transfer is pending.
//   - ABC80 `0x60E9`, at the end of every command: `IN A,(01h) / CPL /
//     AND 08h`, whose Z flag the write path at `0x60C1` returns on as
//     success. So the bit must STILL be set once the command has finished
//     and the controller is back at idle.
//   - The error paths (ABC80 `0x608F`, `0x60C9`) are reached exactly when
//     it is clear.
//
// So bit 3 is not an error flag but its complement: "this command has not
// failed", set from the moment a header is accepted until something goes
// wrong. Modeling it as an error flag - which is what this file did while
// only the ABC802 exercised it, whose ROM never reads the bit at all -
// inverts every one of those three tests. Failures are reported through
// the auxiliary status byte below instead.
//
// An idle, healthy controller is therefore 0x89, and one that has just
// failed a command is 0x81.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"

#define SECTOR_SIZE 256
#define NUM_BUFFERS 4
#define NUM_UNITS   8

// Where the host is in the four-byte command header, or in a data
// transfer. The header is written a byte at a time to the OUT port.
typedef enum {
    ST_K0 = 0, ST_K1, ST_K2, ST_K3,  // collecting the command header
    ST_UPLOAD,                       // host -> controller buffer
    ST_DOWNLOAD                      // controller buffer -> host
} BusState;

// Command bits in k[0]. The controller performs whichever are set, in
// this order, clearing each as it goes - so one header can say "read the
// sector, then hand it to the host".
#define CMD_READ_SECTOR   0x01  // media -> buffer
#define CMD_SECTOR_TO_HOST 0x02 // buffer -> host
#define CMD_SECTOR_FROM_HOST 0x04 // host -> buffer
#define CMD_WRITE_SECTOR  0x08  // buffer -> media

// Status bits, as derived from the ROMs' own polling above.
#define STAT_READY 0x01  // a byte can move now
#define STAT_OK    0x08  // the current command has not failed
#define STAT_IDLE  0x80  // command completed, ready for the next header

// Auxiliary status, read back from the INP port once a command finishes.
// The ROM checks it with `OR A / JR Z` at 0x6158: zero means success.
#define AUX_SEEK_ERROR   0x10
#define AUX_WRITE_PROT   0x40
#define AUX_NOT_READY    0x80

// Drive geometry. Two of the four device-select codes are wired up - the
// ABC830 ("mo") and the ABC832/834 ("mf") - because those are the classes
// of media this project has verified ground truth for. The 8-inch SF and
// the hard disk are named in disk.h but not modeled: their geometry, and
// in particular their interleave, cannot be inferred from these two, which
// need opposite settings (see DRIVE_MF below).
typedef struct {
    uint8_t select;
    unsigned sectors_per_cluster;
    unsigned sectors;       // total, for range checking
    unsigned interleave_factor;
    unsigned interleave_mask;
    const char *name;
} DriveType;

// ABC830 ("mo"): 40 tracks x 1 side x 16 sectors x 256 bytes = 160KB,
// exactly the size of the real dumped images in the abc80.net archive.
//
// Its interleave is this project's own empirically verified finding from
// ABC80's Milestone 6 (factor 7, mask 15), *not* abc80sim's - that
// implementation ships with interleave compiled out. Both cannot be right
// for the same media, and this one is now confirmed twice: on ABC80,
// where every directory entry resolved to a consistent file header with
// it and to garbage without, and again here, where disabling it stops
// real media booting at all.
static const DriveType DRIVE_MO = {ABCBUS_SEL_MO, 1, 40 * 1 * 16, 7, 15, "mo"};

// ABC832/834 ("mf"): 80 tracks x 2 sides x 16 sectors = 640KB, and the
// ABC802's own native drive. Four sectors per cluster rather than one,
// which changes how a command header's sector address is decoded.
//
// Interleave: none. That is not an assumption carried over from
// abc80sim - it was tested the same way the ABC830's was, by booting real
// 640KB media both ways. Identity works and factor 7 does not, which is
// the opposite result to the ABC830 and precisely why neither drive's
// value could be inferred from the other. A mask of 0 makes the mapping
// in file_offset() an identity, so no special case is needed.
static const DriveType DRIVE_MF = {ABCBUS_SEL_MF, 4, 80 * 2 * 16, 0, 0, "mf"};

// Which controller is fitted. Chosen from the attached image's size
// rather than a flag: the two formats differ by a factor of four, a real
// dump is always exactly one of those sizes, and asking the user to
// restate something the file already says is a good way to collect bug
// reports about the wrong geometry.
static const DriveType *drive_type = &DRIVE_MO;

// A --interleave override, or -1 for "use whatever the fitted drive says".
// See abcbus_disk_set_interleave() in disk.h for why an override exists at
// all: two dump conventions for the same media, and nothing in a disk
// image says which one it is.
static long interleave_override = -1;

static FILE *units[NUM_UNITS];
static bool card_present = false;
static bool selected = false;

static BusState state = ST_K0;
static uint8_t k[4];
static uint8_t aux_status;
static int in_ptr = -1;   // -1 = not sending; otherwise index into buffer
static int out_ptr;
static uint8_t buffers[NUM_BUFFERS][SECTOR_SIZE];

bool abcbus_disk_present(void) { return card_present; }

void abcbus_disk_reset(void) {
    state = ST_K0;
    aux_status = 0;
    in_ptr = -1;
    out_ptr = 0;
    memset(k, 0, sizeof(k));
}

// Pick the controller type from the image size. Returns NULL for a size
// that matches no known drive, which is nearly always a truncated
// download or the wrong file entirely - worth refusing loudly rather than
// serving garbage sectors from.
static const DriveType *drive_type_for_size(long size) {
    if (size == (long)DRIVE_MO.sectors * SECTOR_SIZE) return &DRIVE_MO;
    if (size == (long)DRIVE_MF.sectors * SECTOR_SIZE) return &DRIVE_MF;
    return NULL;
}

bool abcbus_disk_attach(int unit, const char *path) {
    if (unit < 0 || unit >= NUM_UNITS) return false;
    // r+b: the controller must be able to write back. A read-only image
    // is reported to the ROM as write-protected rather than refused, the
    // same distinction real media makes.
    FILE *f = fopen(path, "r+b");
    if (!f) {
        f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "Failed to open disk image '%s'\n", path);
            return false;
        }
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        fprintf(stderr, "Cannot determine size of disk image '%s'\n", path);
        return false;
    }
    long size = ftell(f);
    const DriveType *type = drive_type_for_size(size);
    if (!type) {
        fclose(f);
        fprintf(stderr,
                "Disk image '%s' is %ld bytes, which is neither an ABC830 "
                "(%ld) nor an ABC832/834 (%ld) image\n",
                path, size, (long)DRIVE_MO.sectors * SECTOR_SIZE,
                (long)DRIVE_MF.sectors * SECTOR_SIZE);
        return false;
    }
    // A second image must match the first: one controller is fitted, and
    // its drives are all of its own type.
    if (card_present && type != drive_type) {
        fclose(f);
        fprintf(stderr, "Disk image '%s' is a %s image, but a %s controller "
                        "is already fitted\n", path, type->name, drive_type->name);
        return false;
    }
    drive_type = type;

    if (units[unit]) fclose(units[unit]);
    units[unit] = f;
    card_present = true;
    abcbus_disk_reset();
    return true;
}

const char *abcbus_disk_type_name(void) { return drive_type->name; }

void abcbus_disk_set_interleave(unsigned factor) {
    interleave_override = (long)factor;
}

unsigned abcbus_disk_interleave(void) {
    return interleave_override >= 0 ? (unsigned)interleave_override
                                    : drive_type->interleave_factor;
}

int abcbus_disk_attached_count(void) {
    int n = 0;
    for (int i = 0; i < NUM_UNITS; i++) {
        if (units[i]) n++;
    }
    return n;
}

bool abcbus_disk_attach_arg(const char *arg) {
    // "N:path" pins the drive; a bare path takes the next free one, so
    // two plain --disk arguments land on drives 0 and 1 - which is what
    // two-drive software (and the ROM's own MO1:/MF1: device names)
    // expects, without inventing a second flag for it.
    if (arg[0] >= '0' && arg[0] <= '7' && arg[1] == ':' && arg[2] != '\0') {
        return abcbus_disk_attach(arg[0] - '0', arg + 2);
    }
    int unit = abcbus_disk_attached_count();
    if (unit >= NUM_UNITS) {
        fprintf(stderr, "Too many disk images: the controller has %d drives\n", NUM_UNITS);
        return false;
    }
    return abcbus_disk_attach(unit, arg);
}

void abcbus_disk_close(void) {
    for (int i = 0; i < NUM_UNITS; i++) {
        if (units[i]) fclose(units[i]);
        units[i] = NULL;
    }
    card_present = false;
}

// ABCBUS_TRACE=1 logs every command header the card completes, to stderr.
// The protocol is a state machine driven entirely by the ROM, so when
// something is wrong the useful question is almost always "which sector
// did it actually ask for" - which no amount of reading the ROM answers
// as directly as watching it run.
static int trace_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *v = getenv("ABCBUS_TRACE");
        cached = (v && *v && *v != '0') ? 1 : 0;
    }
    return cached;
}

void abcbus_disk_select(uint8_t select) {
    // A real card compares CS against its own DIP-set address and simply
    // stops responding when it does not match - which is what makes the
    // ROM's scan across 0x24/0x2C/0x2D/0x2E find exactly the cards fitted.
    selected = card_present && (select == drive_type->select);
}

// The logical sector the current command header addresses. The ABC830
// uses the older, "clustered" addressing rather than a flat 16-bit
// sector number: k[2] and the top three bits of k[3] form a cluster
// index, the low five bits of k[3] the sector within it.
static unsigned current_sector(void) {
    unsigned cluster = ((unsigned)k[2] << 3) + (k[3] >> 5);
    return cluster * drive_type->sectors_per_cluster + (k[3] & 31);
}

// Where that sector physically lives in the image file. Real ABC830 media
// is sector-interleaved within each 16-sector track, and the archived
// dumps store sectors in physical order - so a logical sector must be
// mapped before indexing the file. Track-boundary sectors map to
// themselves, which is why an unmapped implementation appears to work
// right up until it reads anything that is not at a multiple of 16.
static long file_offset(void) {
    unsigned s = current_sector();
    unsigned factor = drive_type->interleave_factor;
    unsigned mask = drive_type->interleave_mask;
    if (interleave_override >= 0) {
        factor = (unsigned)interleave_override;
        // Both modeled drives put 16 sectors on a track, so the mask that
        // confines the permutation to one track is the same for either;
        // a factor of 0 makes the mapping an identity regardless.
        mask = factor ? 15 : 0;
    }
    unsigned mapped = (s & ~mask) | ((s * factor) & mask);
    return (long)mapped * SECTOR_SIZE;
}

static FILE *current_unit(void) {
    return units[k[1] & 0x07];
}

static uint8_t *current_buffer(void) {
    return buffers[(k[1] >> 6) & 0x03];
}

// Perform whichever command bits remain set, clearing each as it is done.
// The two transfer bits hand control back to the host mid-command: the
// state machine parks in UPLOAD/DOWNLOAD until 256 bytes have moved, then
// this is re-entered to finish whatever is left (typically the write).
static void run_command(void) {
    FILE *f = current_unit();
    uint8_t *buf = current_buffer();

    if (k[0] & CMD_READ_SECTOR) {
        k[0] &= (uint8_t)~CMD_READ_SECTOR;
        memset(buf, 0, SECTOR_SIZE);
        if (fseek(f, file_offset(), SEEK_SET) != 0 ||
            fread(buf, 1, SECTOR_SIZE, f) != SECTOR_SIZE) {
            aux_status = AUX_SEEK_ERROR;
            state = ST_K0;
            return;
        }
    }

    if (k[0] & CMD_SECTOR_TO_HOST) {
        k[0] &= (uint8_t)~CMD_SECTOR_TO_HOST;
        in_ptr = 0;
        state = ST_DOWNLOAD;
        return;
    }

    if (k[0] & CMD_SECTOR_FROM_HOST) {
        k[0] &= (uint8_t)~CMD_SECTOR_FROM_HOST;
        out_ptr = 0;
        state = ST_UPLOAD;
        return;
    }

    if (k[0] & CMD_WRITE_SECTOR) {
        k[0] &= (uint8_t)~CMD_WRITE_SECTOR;
        if (fseek(f, file_offset(), SEEK_SET) != 0 ||
            fwrite(buf, 1, SECTOR_SIZE, f) != SECTOR_SIZE) {
            aux_status = AUX_WRITE_PROT;
            state = ST_K0;
            return;
        }
        fflush(f);
    }

    state = ST_K0;
}

// The header is complete: validate the drive and sector before acting, so
// a bad request reports an error rather than seeking off the end of the
// image.
static void command_header_complete(void) {
    if (trace_enabled()) {
        fprintf(stderr, "[abcbus] cmd %02X %02X %02X %02X -> unit %d buf %d "
                        "sector %u offset %ld\n",
                k[0], k[1], k[2], k[3], k[1] & 0x07, (k[1] >> 6) & 0x03,
                current_sector(), file_offset());
    }
    if (!current_unit()) {
        aux_status = AUX_NOT_READY;
        state = ST_K0;
        return;
    }
    if (current_sector() >= drive_type->sectors) {
        aux_status = AUX_SEEK_ERROR;
        state = ST_K0;
        return;
    }
    run_command();
}

void abcbus_disk_out(int port, uint8_t value) {
    if (!selected) return;

    switch (port) {
        case 0:
            switch (state) {
                case ST_K0:
                case ST_K1:
                case ST_K2:
                    // A new header clears the previous command's result,
                    // so a caller reading status mid-header sees this
                    // command's state rather than the last one's.
                    aux_status = 0;
                    k[state - ST_K0] = value;
                    state = (BusState)(state + 1);
                    break;
                case ST_K3:
                    aux_status = 0;
                    k[3] = value;
                    state = ST_K0;
                    command_header_complete();
                    break;
                case ST_UPLOAD:
                    current_buffer()[out_ptr++] = value;
                    if (out_ptr >= SECTOR_SIZE) run_command();
                    break;
                case ST_DOWNLOAD:
                    break; // host writing while the card is sending: ignored
            }
            break;

        case 2:  // C1
        case 4:  // C3
            // The ROM pulses C1 before each command (0x60AB) and C3 after
            // a failed selection (0x609E). On the real card these are an
            // NMI to the controller's own CPU and a device reset; for a
            // synthetic controller both amount to "abandon whatever
            // transfer was in progress and be ready for a header".
            abcbus_disk_reset();
            break;

        default:
            break;
    }
}

uint8_t abcbus_disk_in(int port) {
    // Not selected, or no card: the bus floats high. The ROM reads 0xFF as
    // "nothing there" and moves on, which is exactly the behavior this
    // emulator had before the card existed at all.
    if (!selected) return 0xFF;

    switch (port) {
        case 0:
            if (in_ptr >= 0) {
                uint8_t v = current_buffer()[in_ptr++];
                if (in_ptr >= SECTOR_SIZE) {
                    in_ptr = -1;
                    run_command();
                }
                return v;
            }
            // Outside a transfer, the INP port reads back the result of
            // the last command - what the ROM tests with `OR A` at 0x6158.
            return aux_status;

        case 1: {
            // Never 0x00 and never 0xFF: both mean "no device" to the
            // ABC802's poll at 0x6196, and STAT_READY alone guarantees the
            // first while nothing here sets enough bits for the second.
            //
            // Bit 2 is deliberately never set. The ABC80's ROM loads it
            // straight into the low byte of the transfer address at 0x6120
            // (`AND 04h` ... `LD L,A`), which is only correct because the
            // bit is zero and the buffer is 256-byte aligned.
            uint8_t stat = STAT_READY;
            if (aux_status == 0) stat |= STAT_OK;
            if (state == ST_K0) stat |= STAT_IDLE;
            return stat;
        }

        default:
            return 0xFF;
    }
}
