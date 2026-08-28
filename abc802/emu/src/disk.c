// abc802/emu/src/disk.c - the synthetic ABC-bus disk controller.
// See disk.h for why this models the protocol rather than the card.
//
// The protocol below is reimplemented from sasq64/abc80sim's own synthetic
// controller (src/disk.c, src/abcio.c) - the same source ABC80's Milestone
// 6 already leaned on for its sector interleave. It is not copied, and
// every claim it makes was cross-checked against this machine's *own* DOS
// ROM, disassembled with this project's bin/z80dasm. Three details match
// exactly rather than approximately, which is what makes it safe to treat
// an ABC80-derived description as authoritative for the ABC802:
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
// The status byte is likewise pinned by what the ROM actually tests:
//
//   - `L6196` polls with `IN A,(01h)` then `INC A / JR Z` and `DEC A /
//     JR Z`: a status of 0xFF *or* 0x00 is taken as "no device" and the
//     poll bails immediately. So a present controller must never report
//     either. This is precisely why the pre-existing "every ABC-bus read
//     returns 0xFF" behavior reads as no card fitted - correctly.
//   - `L616F` waits for `(STAT & 0x80) == 0x80` before issuing a command,
//     so bit 7 means "idle, ready for a command header".
//   - The transfer loops at `L612D`/`L6140` do `IN A,(01h) / RRCA / JP NC`,
//     so bit 0 means "ready to move a byte".
//   - After the header, `L6107` waits for either bit 7 or
//     `(STAT & 0x05) == 0x01`.
//
// An idle, ready controller is therefore 0x81, which satisfies all four.

#include <stdio.h>
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

// Status bits, as derived from the ROM's own polling above.
#define STAT_READY 0x01  // a byte can move now
#define STAT_IDLE  0x80  // command completed, ready for the next header
#define STAT_ERROR 0x08

// Auxiliary status, read back from the INP port once a command finishes.
// The ROM checks it with `OR A / JR Z` at 0x6158: zero means success.
#define AUX_SEEK_ERROR   0x10
#define AUX_WRITE_PROT   0x40
#define AUX_NOT_READY    0x80

// Drive geometry. Only the ABC830 ("mo") is wired up for now, because
// that is the class of media this project already has verified ground
// truth for; the others are here because the ROM scans for them and
// naming them documents what a future step would fill in.
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
static const DriveType DRIVE_MO = {ABC802_SEL_MO, 1, 40 * 1 * 16, 7, 15, "mo"};

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
static const DriveType DRIVE_MF = {ABC802_SEL_MF, 4, 80 * 2 * 16, 0, 0, "mf"};

// Which controller is fitted. Chosen from the attached image's size
// rather than a flag: the two formats differ by a factor of four, a real
// dump is always exactly one of those sizes, and asking the user to
// restate something the file already says is a good way to collect bug
// reports about the wrong geometry.
static const DriveType *drive_type = &DRIVE_MO;

static FILE *units[NUM_UNITS];
static bool card_present = false;
static bool selected = false;

static BusState state = ST_K0;
static uint8_t k[4];
static uint8_t status;
static uint8_t aux_status;
static int in_ptr = -1;   // -1 = not sending; otherwise index into buffer
static int out_ptr;
static uint8_t buffers[NUM_BUFFERS][SECTOR_SIZE];

bool abc802_disk_present(void) { return card_present; }

void abc802_disk_reset(void) {
    state = ST_K0;
    status = 0;
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

bool abc802_disk_attach(int unit, const char *path) {
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
    abc802_disk_reset();
    return true;
}

const char *abc802_disk_type_name(void) { return drive_type->name; }

void abc802_disk_close(void) {
    for (int i = 0; i < NUM_UNITS; i++) {
        if (units[i]) fclose(units[i]);
        units[i] = NULL;
    }
    card_present = false;
}

void abc802_disk_select(uint8_t select) {
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
    unsigned mask = drive_type->interleave_mask;
    unsigned mapped = (s & ~mask) | ((s * drive_type->interleave_factor) & mask);
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
            status = STAT_ERROR;
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
            status = STAT_ERROR;
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
    if (!current_unit()) {
        status = STAT_ERROR;
        aux_status = AUX_NOT_READY;
        state = ST_K0;
        return;
    }
    if (current_sector() >= drive_type->sectors) {
        status = STAT_ERROR;
        aux_status = AUX_SEEK_ERROR;
        state = ST_K0;
        return;
    }
    run_command();
}

void abc802_disk_out(int port, uint8_t value) {
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
                    status = 0;
                    aux_status = 0;
                    k[state - ST_K0] = value;
                    state = (BusState)(state + 1);
                    break;
                case ST_K3:
                    status = 0;
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
            abc802_disk_reset();
            break;

        default:
            break;
    }
}

uint8_t abc802_disk_in(int port) {
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

        case 1:
            return (uint8_t)(STAT_READY | status | (state == ST_K0 ? STAT_IDLE : 0));

        default:
            return 0xFF;
    }
}
