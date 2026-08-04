#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <dirent.h>
#include "z80.h"
#include "cpm.h"

// Console input needs the host terminal in raw mode (no line buffering, no
// local echo) so CP/M's own character-at-a-time BDOS calls (functions 1,
// 6, 10, 11 - see docs/CPM_REFERENCE.md) see input the same way real CP/M
// hardware would, rather than waiting for a host Enter keypress on every
// call. Only touched when stdin is actually a terminal; a piped/redirected
// stdin is left alone (there's no terminal mode to change, and raw mode
// wouldn't affect a pipe's behavior anyway).
static struct termios orig_termios;
static int termios_saved = 0;

static void cpm_console_shutdown(void) {
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    }
}

void cpm_console_init(void) {
    if (!isatty(STDIN_FILENO)) return;

    if (tcgetattr(STDIN_FILENO, &orig_termios) != 0) return;
    termios_saved = 1;
    atexit(cpm_console_shutdown);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO); // no line buffering, no local echo
    // Without this, ICRNL (on by default) silently rewrites the real CR
    // (0x0D) the Enter key sends into LF (0x0A) before read() ever sees
    // it. CP/M software (e.g. Tasty Basic's own line-input routine, which
    // explicitly discards LF as noise while waiting for a *real* CR) is
    // written against a genuine raw serial line where no such host-side
    // translation happens - so leaving ICRNL on makes Enter look dead.
    raw.c_iflag &= ~(ICRNL | INLCR | IGNCR);
    // IXON (on by default) is classic Unix software flow control: the
    // kernel tty driver intercepts Ctrl-S/Ctrl-Q as XOFF/XON (pause/
    // resume output) and never delivers the byte to read() at all. Real
    // CP/M-era software written against a genuine raw serial line
    // expects Ctrl-S to arrive as a normal byte - Turbo Pascal's editor
    // uses it for "character left", which is exactly what surfaced this
    // (Ctrl-S appeared to do nothing, since the byte never got here).
    raw.c_iflag &= ~IXON;
    raw.c_cc[VMIN] = 1;              // block for at least 1 byte
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// select() alone can't tell "a real byte is waiting" apart from "stdin
// is at EOF" for a pipe/redirected file - both make it report readable,
// since a read() genuinely wouldn't block either way. A real terminal's
// console status never has this ambiguity (idle means "no key pressed
// yet", never "spontaneously readable"), so software that polls status
// before reading - e.g. BDS C's own console-output routine, which checks
// for a Ctrl-C abort after every character it prints - saw "ready"
// forever once a piped/redirected stdin ran dry, called what it thought
// was a real read, and got EOF's ^Z (26) sentinel echoed into the
// output stream after every single character. One real byte of
// look-ahead (`pending_char`) plus a sticky `seen_eof` flag (once a pipe
// hits EOF it never has more data) lets `console_char_ready()` actually
// disambiguate by attempting the read itself, buffering a genuine byte
// for the next `console_read_char()` call rather than losing it.
static int pending_char = -1;
static int seen_eof = 0;

static int console_char_ready(void) {
    if (seen_eof) return 0;
    if (pending_char != -1) return 1;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    struct timeval timeout = {0, 0};
    if (select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout) <= 0) return 0;
    uint8_t c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) {
        seen_eof = 1;
        return 0;
    }
    pending_char = c;
    return 1;
}

// Blocking single-byte read. EOF (e.g. piped/redirected stdin exhausted)
// maps to ^Z (26), the traditional CP/M "no more input" sentinel, so a
// program driven from a non-interactive stdin doesn't spin forever.
static int console_read_char(void) {
    int c;
    if (pending_char != -1) {
        c = pending_char;
        pending_char = -1;
    } else if (seen_eof) {
        c = -1; // no real read attempted; already known to be EOF
    } else {
        uint8_t byte;
        ssize_t n = read(STDIN_FILENO, &byte, 1);
        if (n <= 0) {
            seen_eof = 1;
            c = -1;
        } else {
            c = byte;
        }
    }
    if (c < 0) return 26;
    // Modern keyboards' Backspace/Delete key sends DEL (0x7F) in raw
    // terminal mode, but CP/M-era software (e.g. Tasty Basic's own
    // line-input routine) was written against real serial terminals that
    // used the classic BS (0x08) convention and only recognizes that
    // byte as "erase last character" - translate so backspace works
    // without needing to reconfigure the host terminal's erase key.
    if (c == 0x7F) c = 0x08;
    return c;
}

// Real CP/M-era software targeting a graphical/color terminal (e.g. the
// ANSI-enhanced SARGON port in resources/sargon/sargon78.com) commonly
// emits high-bit bytes (0x80-0xFF) meaning IBM PC/DOS "code page 437" -
// box-drawing, block-shading, and a handful of accented/Greek/math
// glyphs, not raw byte values a modern UTF-8 terminal understands (its
// own README even tells PuTTY users to explicitly set "Code Page 437").
// VT100/ANSI cursor-positioning and color (SGR) escape codes already
// pass through untouched and render correctly in any modern terminal -
// this table is the one remaining piece: translate CP437's upper half to
// the equivalent Unicode codepoint, indexed by byte-0x80. Values below
// 0x80 (plain ASCII plus C0 control codes like CR/LF/ESC) are identical
// in both encodings and need no translation.
static const uint16_t cp437_high[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, // 80-87
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5, // 88-8F
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, // 90-97
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, // 98-9F
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, // A0-A7
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, // A8-AF
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, // B0-B7
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, // B8-BF
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, // C0-C7
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, // C8-CF
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, // D0-D7
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, // D8-DF
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, // E0-E7
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229, // E8-EF
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, // F0-F7
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0, // F8-FF
};

static void putchar_utf8(uint16_t cp) {
    if (cp < 0x80) {
        putchar((int)cp);
    } else if (cp < 0x800) {
        putchar(0xC0 | (cp >> 6));
        putchar(0x80 | (cp & 0x3F));
    } else {
        putchar(0xE0 | (cp >> 12));
        putchar(0x80 | ((cp >> 6) & 0x3F));
        putchar(0x80 | (cp & 0x3F));
    }
}

// Every console-output BDOS/BIOS function should emit a program-supplied
// byte through this instead of calling putchar() directly - console
// *input* echo (typed keystrokes) skips it, since that's always plain
// ASCII from the keyboard.
static void console_emit(uint8_t c) {
    if (c < 0x80) {
        putchar(c);
    } else {
        putchar_utf8(cp437_high[c - 0x80]);
    }
}

/*
 * File I/O
 *
 * Real CP/M maps FCB-addressed files onto physical disk geometry via a
 * Disk Parameter Block per drive (see docs/CPM_REFERENCE.md) - modeling
 * that means emulating an actual disk image. Instead, every drive/user
 * number is collapsed onto a single host directory (CPM_DISK_DIR, created
 * relative to the current working directory if it doesn't exist): an FCB
 * naming "FOO.TXT" maps straight to "cpm_disk/FOO.TXT" on the host. This
 * can't express CP/M's drive-switching or per-user file areas, but covers
 * what the vast majority of CP/M-80 transient programs actually need from
 * BDOS file calls, without an on-disk format to get bit-exact.
 */

#define CPM_DISK_DIR "cpm_disk"
#define CPM_RECORD_SIZE 128
#define CPM_RECORDS_PER_EXTENT 128 // 128 * 128 bytes = 16KB, one FCB extent

void cpm_fileio_init(void) {
    mkdir(CPM_DISK_DIR, 0755); // ignore EEXIST; any other failure surfaces
                                // later as F_OPEN/F_MAKE failing to open
}

// Builds a host path from the 8.3 name/type fields starting at `f1_addr`
// (the address of the FCB's F1 byte - fcb_addr+1 for a file's own name,
// fcb_addr+17 for F_RENAME's "new name" fields, which reuse FCB+16 as a
// second F1..T3 block). Masks off the high attribute bit each byte can
// carry and trims trailing spaces.
static void build_host_path(Z80 *cpu, uint16_t f1_addr, char *out, size_t outsz) {
    char name[9], type[4];
    int n = 0, t = 0;

    for (int i = 0; i < 8; i++) {
        uint8_t ch = z80_read_byte(cpu, f1_addr + i) & 0x7F;
        if (ch != ' ') name[n++] = (char)toupper(ch);
    }
    name[n] = '\0';

    for (int i = 0; i < 3; i++) {
        uint8_t ch = z80_read_byte(cpu, f1_addr + 8 + i) & 0x7F;
        if (ch != ' ') type[t++] = (char)toupper(ch);
    }
    type[t] = '\0';

    if (t > 0) {
        snprintf(out, outsz, "%s/%s.%s", CPM_DISK_DIR, name, type);
    } else {
        snprintf(out, outsz, "%s/%s", CPM_DISK_DIR, name);
    }
}

// An 11-char (8 name + 3 type, no dot) uppercase representation used for
// wildcard matching: '?' in a search FCB matches any character at that
// position (the CCP/caller already expands '*' into a run of '?'s before
// BDOS ever sees it), anything else must match exactly, including spaces
// for a name/type shorter than the field width.
static void fcb_pattern(Z80 *cpu, uint16_t fcb_addr, char pat[11]) {
    for (int i = 0; i < 11; i++) {
        uint8_t ch = z80_read_byte(cpu, fcb_addr + 1 + i) & 0x7F;
        pat[i] = (char)toupper(ch);
    }
}

// Same 11-char shape, built from a host filename instead of an FCB, for
// comparing against fcb_pattern()'s output and for filling in a matched
// directory-entry image. Returns 0 if `host_name` doesn't fit an 8.3 shape
// (no dot, name >8 chars, type >3 chars, extra dots) - such host files are
// invisible to F_SFIRST/F_SNEXT.
static int host_name_to_fcb_form(const char *host_name, char out[11]) {
    const char *dot = strchr(host_name, '.');
    size_t namelen = dot ? (size_t)(dot - host_name) : strlen(host_name);
    size_t typelen = dot ? strlen(dot + 1) : 0;
    if (namelen == 0 || namelen > 8 || typelen > 3) return 0;
    if (dot && strchr(dot + 1, '.')) return 0; // more than one dot

    memset(out, ' ', 11);
    for (size_t i = 0; i < namelen; i++) out[i] = (char)toupper((unsigned char)host_name[i]);
    for (size_t i = 0; i < typelen; i++) out[8 + i] = (char)toupper((unsigned char)dot[1 + i]);
    return 1;
}

static int fcb_pattern_match(const char pat[11], const char cand[11]) {
    for (int i = 0; i < 11; i++) {
        if (pat[i] != '?' && pat[i] != cand[i]) return 0;
    }
    return 1;
}

// Tracks an open file per in-use FCB. Real CP/M has no separate "file
// handle" - the FCB's own memory (and its address, as far as this
// emulator's concerned) IS the handle - so this table is keyed by the FCB
// address a program passed to F_OPEN/F_MAKE, exactly as it'll pass that
// same address back to F_READ/F_WRITE/F_CLOSE.
#define MAX_OPEN_FILES 16
typedef struct {
    int in_use;
    uint16_t fcb_addr;
    FILE *fp;
} OpenFile;
static OpenFile open_files[MAX_OPEN_FILES];

static OpenFile *find_open_file(uint16_t fcb_addr) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_files[i].in_use && open_files[i].fcb_addr == fcb_addr) return &open_files[i];
    }
    return NULL;
}

static OpenFile *alloc_open_file(uint16_t fcb_addr) {
    // Real CP/M has no file-handle concept distinct from the FCB itself -
    // opening an FCB address that's already open (a program reusing one
    // FCB buffer for a new file without an intervening F_CLOSE, which is
    // completely normal: Turbo Pascal does exactly this loading TURBO.MSG
    // and then a work/main file through the same FCB) just overwrites that
    // FCB's fields on real hardware. Mirror that here: reuse the existing
    // slot (closing its stale handle first) instead of leaving it in place
    // and allocating a second entry for the same address - find_open_file()
    // would then keep matching the *old* entry first, silently reading the
    // previous file's content forever.
    OpenFile *existing = find_open_file(fcb_addr);
    if (existing) {
        fclose(existing->fp);
        return existing;
    }
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].in_use) {
            open_files[i].in_use = 1;
            open_files[i].fcb_addr = fcb_addr;
            return &open_files[i];
        }
    }
    return NULL;
}

static uint16_t dma_addr = 0x0080; // set via F_DMAOFF (26); CP/M default

// Reads the FCB's EX/CR sequential-position fields as one linear record
// number (EX counts whole 16KB extents, CR the record within one).
static long fcb_sequential_record(Z80 *cpu, uint16_t fcb_addr) {
    uint8_t ex = z80_read_byte(cpu, fcb_addr + 0x0C);
    uint8_t cr = z80_read_byte(cpu, fcb_addr + 0x20);
    return (long)ex * CPM_RECORDS_PER_EXTENT + cr;
}

static void fcb_set_sequential_record(Z80 *cpu, uint16_t fcb_addr, long rec) {
    uint8_t ex = (uint8_t)((rec / CPM_RECORDS_PER_EXTENT) & 0x1F);
    uint8_t cr = (uint8_t)(rec % CPM_RECORDS_PER_EXTENT);
    z80_write_byte(cpu, fcb_addr + 0x0C, ex);
    z80_write_byte(cpu, fcb_addr + 0x20, cr);
}

// R0-R2 (24 bits total, though CP/M 2.2 files rarely need more than R0-R1)
// as one linear record number, for the random-access functions.
static long fcb_random_record(Z80 *cpu, uint16_t fcb_addr) {
    uint8_t r0 = z80_read_byte(cpu, fcb_addr + 0x21);
    uint8_t r1 = z80_read_byte(cpu, fcb_addr + 0x22);
    uint8_t r2 = z80_read_byte(cpu, fcb_addr + 0x23);
    return (long)r0 | ((long)r1 << 8) | ((long)r2 << 16);
}

// Directory search (F_SFIRST/F_SNEXT) state: which pattern we're matching
// and where we left off, so F_SNEXT can resume a search F_SFIRST started.
static DIR *search_dir = NULL;
static char search_pattern[11];

// Advances `search_dir`, looking for the next host entry matching
// `search_pattern`. On a match, fills a 32-byte CP/M directory-entry image
// at the current DMA address (always slot 0 of the notional 4-per-record
// packing real disk directories use - which slot doesn't matter to a
// caller, only that BDOS filled in *some* entry and told us where) and
// returns 1. Returns 0 (closing `search_dir`) once entries are exhausted.
static int search_advance(Z80 *cpu) {
    if (!search_dir) return 0;

    struct dirent *entry;
    while ((entry = readdir(search_dir)) != NULL) {
        char form[11];
        if (!host_name_to_fcb_form(entry->d_name, form)) continue;
        if (!fcb_pattern_match(search_pattern, form)) continue;

        char path[300];
        snprintf(path, sizeof(path), "%s/%s", CPM_DISK_DIR, entry->d_name);
        struct stat st;
        long records = 0;
        if (stat(path, &st) == 0) {
            records = (st.st_size + CPM_RECORD_SIZE - 1) / CPM_RECORD_SIZE;
        }
        if (records > CPM_RECORDS_PER_EXTENT) records = CPM_RECORDS_PER_EXTENT;

        z80_write_byte(cpu, dma_addr + 0, 0); // DR
        for (int i = 0; i < 11; i++) {
            z80_write_byte(cpu, dma_addr + 1 + i, (uint8_t)form[i]);
        }
        z80_write_byte(cpu, dma_addr + 12, 0);              // EX
        z80_write_byte(cpu, dma_addr + 13, 0);               // S1
        z80_write_byte(cpu, dma_addr + 14, 0);               // S2
        z80_write_byte(cpu, dma_addr + 15, (uint8_t)records); // RC
        for (int i = 16; i < 32; i++) z80_write_byte(cpu, dma_addr + i, 0); // AL
        return 1;
    }

    closedir(search_dir);
    search_dir = NULL;
    return 0;
}

static uint8_t current_drive = 0;
static uint8_t current_user = 0;
static int ccp_mode = 0;

void cpm_set_ccp_mode(int enabled) {
    ccp_mode = enabled;
}

int cpm_is_ccp_mode(void) {
    return ccp_mode;
}

/*
 * BIOS
 *
 * Real CP/M programs are supposed to go through BDOS (CALL 0005h) for
 * everything, but it's a well-established, portable technique for
 * performance-sensitive code to call directly into the BIOS instead,
 * bypassing BDOS's function-number dispatch overhead. Locating the BIOS
 * is itself a standard trick: address 0x0000 always holds a 3-byte
 * `JP <wboot>` instruction (see docs/CPM_REFERENCE.md's zero-page map),
 * so a program reads that jump's target to find WBOOT, then reaches any
 * other vector via its known fixed offset from BIOS_BASE (WBOOT itself is
 * always at BIOS_BASE+3 - see the 17-vector table also in
 * docs/CPM_REFERENCE.md).
 *
 * MBASIC's own low-level character-output routine goes one step further,
 * and it's *also* a well-known, standard CP/M technique: rather than
 * calling through the CONOUT vector every time (paying for its `JP`
 * indirection on every character), it reads CONOUT's *own jump target*
 * once at startup - the 2 bytes right after that vector's `JP` opcode -
 * and self-patches that address directly into its own code, bypassing
 * the jump table entirely afterward. This means every vector needs to be
 * a genuine 3-byte `JP <target>`, not just a bare RET: something reading
 * "the real CONOUT address" needs a real address to find there. Each
 * vector's own target is simply itself - since check_cpm_bios() below
 * intercepts a matching PC *before* any fetch/execute happens, it doesn't
 * matter whether a caller reaches a given address by calling the vector
 * directly or by reading-then-calling its self-referencing target; both
 * land on the exact same PC value and get the exact same handling.
 *
 * Before this existed, main.c only wrote a single RET byte at 0x0000: no
 * real jump, so a program reading "the BIOS address" out of it got zero,
 * and any vector computed relative to that zero was zero too - which is
 * exactly why MBASIC's first attempted character output silently ended
 * the program (calling address 0, which main.c's run loop already treats
 * as "terminated", before ever printing its banner).
 */
#define BIOS_BASE 0xFC00
#define BIOS_V_BOOT    0x00
#define BIOS_V_WBOOT   0x03
#define BIOS_V_CONST   0x06
#define BIOS_V_CONIN   0x09
#define BIOS_V_CONOUT  0x0C
#define BIOS_V_LIST    0x0F
#define BIOS_V_PUNCH   0x12
#define BIOS_V_READER  0x15
#define BIOS_V_HOME    0x18
#define BIOS_V_SELDSK  0x1B
#define BIOS_V_SETTRK  0x1E
#define BIOS_V_SETSEC  0x21
#define BIOS_V_SETDMA  0x24
#define BIOS_V_READ    0x27
#define BIOS_V_WRITE   0x2A
#define BIOS_V_LISTST  0x2D
#define BIOS_V_SECTRAN 0x30

// A fake but internally-consistent Disk Parameter Header/Block, giving
// real software a plausible non-garbage answer when it asks "how much
// disk space is free" - BDOS functions DRV_DPB (31) and DRV_ALLOCVEC (27)
// and BIOS SELDSK all point here. Before this existed, none of the three
// were handled at all, so a caller got back whatever HL already
// contained (leftover from its own prior code, not a real DPB address) -
// found via two independent real programs that both read it as "the disk
// is full": Turbo Pascal's D)ir command showing "Bytes Remaining On A:
// 0k" despite writes succeeding, and dBASE II's QUIT sequence reporting
// "Disk is full" for a database that had just been written successfully.
// Values describe an ~8MB fixed (non-removable) disk - BSH/BLM/EXM/DSM/
// DRM/AL0/AL1/CKS chosen per the real formulas in the CP/M 2.2 Alteration
// Guide (ch. 6): BLS=4096-byte blocks (BSH=5, BLM=31), DSM=2039 (2040
// blocks * 4096 = 8,355,840 bytes), DRM=1023 (1024 directory entries, 128
// per 4096-byte block, so the first 8 blocks - all of AL0 - are reserved
// for the directory), CKS=0 and OFF=0 since there's no real removable-
// media or reserved-track concept here. Not modeling any specific real
// drive, the same spirit as BDOS_ENTRY's "plausible ~61KB of free
// memory" - this project doesn't emulate real disk geometry (see
// docs/CPM_REFERENCE.md's DPB section), so the actual number just needs
// to read as sane and report plenty of free space, which an all-zero
// allocation vector (nothing marked "in use") does directly.
#define DPH_BASE    0xF300 // 16 bytes: XLT, 3 scratch words, DIRBUF, DPB, CSV, ALV
#define DPB_BASE    0xF310 // 15 bytes: SPT BSH BLM EXM DSM DRM AL0 AL1 CKS OFF
#define DIRBUF_BASE 0xF320 // 128-byte BDOS directory scratch buffer (also reused as CSV, since CKS=0 means it's never actually touched)
#define ALV_BASE    0xF3A0 // 255 bytes = ceil((DSM+1)/8); all zero = nothing allocated

void cpm_bios_init(uint8_t *ram) {
    // JP to WBOOT at address 0 - this is what a program actually reads to
    // locate the BIOS (see the comment above).
    ram[0x0000] = 0xC3; // JP
    ram[0x0001] = (uint8_t)((BIOS_BASE + BIOS_V_WBOOT) & 0xFF);
    ram[0x0002] = (uint8_t)((BIOS_BASE + BIOS_V_WBOOT) >> 8);

    // Every vector is a real "JP <self>" - see the comment above for why
    // a bare RET isn't enough. check_cpm_bios() intercepts all 17
    // addresses before the CPU ever fetches/executes this JP.
    static const int vectors[] = {
        BIOS_V_BOOT, BIOS_V_WBOOT, BIOS_V_CONST, BIOS_V_CONIN, BIOS_V_CONOUT,
        BIOS_V_LIST, BIOS_V_PUNCH, BIOS_V_READER, BIOS_V_HOME, BIOS_V_SELDSK,
        BIOS_V_SETTRK, BIOS_V_SETSEC, BIOS_V_SETDMA, BIOS_V_READ,
        BIOS_V_WRITE, BIOS_V_LISTST, BIOS_V_SECTRAN,
    };
    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        uint16_t addr = BIOS_BASE + vectors[i];
        ram[addr] = 0xC3; // JP
        ram[addr + 1] = (uint8_t)(addr & 0xFF);
        ram[addr + 2] = (uint8_t)(addr >> 8);
    }

    // The fake DPB (see its own comment above) - written once, here,
    // rather than computed per-call, since none of it ever changes.
    static const uint8_t dpb[15] = {
        0x80, 0x00, // SPT = 128
        0x05,       // BSH
        0x1F,       // BLM
        0x01,       // EXM
        0xF7, 0x07, // DSM = 2039
        0xFF, 0x03, // DRM = 1023
        0xFF, 0x00, // AL0, AL1
        0x00, 0x00, // CKS = 0 (fixed disk)
        0x00, 0x00, // OFF = 0
    };
    memcpy(ram + DPB_BASE, dpb, sizeof(dpb));
    memset(ram + DIRBUF_BASE, 0, 128);
    memset(ram + ALV_BASE, 0, 255); // nothing allocated = all free

    // DPH: XLT=0000 (no sector translation), 3 scratch words=0000,
    // DIRBUF, DPB, CSV (=DIRBUF_BASE, unused since CKS=0), ALV.
    uint16_t dph[8] = {0, 0, 0, 0, DIRBUF_BASE, DPB_BASE, DIRBUF_BASE, ALV_BASE};
    for (int i = 0; i < 8; i++) {
        ram[DPH_BASE + i * 2] = (uint8_t)(dph[i] & 0xFF);
        ram[DPH_BASE + i * 2 + 1] = (uint8_t)(dph[i] >> 8);
    }
}

void check_cpm_bios(Z80 *cpu, uint8_t *ram) {
    if (cpu->pc == BIOS_BASE + BIOS_V_WBOOT) {
        // With a CCP loaded (see cpm_set_ccp_mode), a warm boot re-enters
        // it at CCP_BASE instead of halting - real CP/M's WBOOT reloads
        // CCP+BDOS off disk and jumps back into the CCP too, so this is
        // the direct analog for a design with no real disk image. This
        // is also how BDOS function 0 (P_TERMCPM) gets back to the CCP
        // prompt after a program quits: it sets PC to 0, which fetches
        // the JP-to-WBOOT installed at address 0, landing right here.
        //
        // Without a CCP loaded, warm boot never returns to its caller -
        // terminate directly, same reasoning as P_TERMCPM below. Must be
        // caught here (not left to execute the JP-to-self at this
        // address) since z80_step() checks for PC==0 right after this
        // call and would otherwise fetch/execute the JP-to-WBOOT at
        // address 0 forever.
        if (ccp_mode) {
            // The CCP's own cold-boot entry point (ccploc, i.e.
            // CCP_BASE) expects the current disk/user byte packed into C
            // - on real hardware, BIOS's WBOOT loads that from the
            // persisted low-memory byte at 0x0004 before jumping there,
            // since a real warm boot reloads the CCP fresh off disk on
            // every entry, cold or warm. This design keeps the CCP
            // resident across warm boots instead (just re-entering the
            // same in-RAM copy) rather than reloading it - but ccploc's
            // own logic doesn't know that, so it still needs C seeded
            // the same way, or a stale value already sitting in C from
            // whatever the just-exited program was doing gets
            // misread as the disk/user byte, corrupting the CCP's own
            // notion of the current disk (the CCP itself keeps 0x0004
            // up to date via setdiska before ever running a program -
            // see ccp_cpm.asm - so this is always a real, current value,
            // not a guess).
            cpu->c = ram[0x0004];
            cpu->pc = CCP_BASE;
        } else {
            cpu->pc = 0x0000;
        }
        return;
    }

    if (cpu->pc == BIOS_BASE + BIOS_V_CONST) {
        cpu->a = console_char_ready() ? 0xFF : 0x00;
    } else if (cpu->pc == BIOS_BASE + BIOS_V_CONIN) {
        cpu->a = console_read_char(); // raw BIOS input - no echo
    } else if (cpu->pc == BIOS_BASE + BIOS_V_CONOUT) {
        console_emit(cpu->c); // BIOS CONOUT takes the character in C, not E
        fflush(stdout);
    } else if (cpu->pc == BIOS_BASE + BIOS_V_READER) {
        cpu->a = 26; // ^Z: no reader device attached
    } else if (cpu->pc == BIOS_BASE + BIOS_V_SELDSK) {
        // Every drive number is "valid" here (see the File I/O comment -
        // every drive/user collapses onto one host directory), so this
        // always returns the one fake DPH rather than 0 (which real CP/M
        // reserves for "no such drive").
        cpu->hl = DPH_BASE;
    } else if (cpu->pc == BIOS_BASE + BIOS_V_READ || cpu->pc == BIOS_BASE + BIOS_V_WRITE) {
        cpu->a = 1; // no disk I/O at the BIOS level here - see the File I/O comment
    } else if (cpu->pc == BIOS_BASE + BIOS_V_LISTST) {
        cpu->a = 0; // printer never ready - no printer device
    } else if (cpu->pc == BIOS_BASE + BIOS_V_SECTRAN) {
        cpu->hl = cpu->bc; // identity translation - no sector skewing
    } else if (cpu->pc >= BIOS_BASE && cpu->pc < BIOS_BASE + 0x33) {
        // BOOT, LIST, PUNCH, HOME, SETTRK, SETSEC, SETDMA: harmless no-ops
        // (no printer/punch/disk-geometry device backs any of these here).
    } else {
        return; // not a BIOS vector at all
    }

    // Simulate RET: Pop return address off the stack into PC
    uint8_t low = ram[cpu->sp++];
    uint8_t high = ram[cpu->sp++];
    cpu->pc = (high << 8) | low;
}

void check_cpm_bdos(Z80 *cpu, uint8_t *ram) {
    // Almost all real software calls the BDOS via "CALL 0005h", but the
    // standard convention (see BDOS_ENTRY's own comment in cpm.h) is that
    // 0x0005 itself just holds "JP BDOS_ENTRY" - some software calls that
    // address directly instead, having read it out of 0x0006-0x0007, so
    // both need to be intercepted identically (same reasoning as the
    // self-referencing BIOS vectors in check_cpm_bios()).
    if (cpu->pc == 0x0005 || cpu->pc == BDOS_ENTRY) { // Intercept call to BDOS entry
        if (cpu->c == 0) {
            // Function 0: System Reset (P_TERMCPM) - warm boot, i.e. quit
            // back to the OS. Real CP/M restarts the CCP; there's no CCP
            // here, so just jump straight to 0x0000 - main.c's run loop
            // already treats PC==0 as normal program termination. Unlike
            // every other function, this does NOT fall through to the
            // RET simulation below: a real warm boot never returns to the
            // caller, so popping a return address here would be wrong.
            cpu->pc = 0x0000;
            return;
        }
        else if (cpu->c == 1) {
            // Function 1: Console Input (wait for a char, echo it, return in A/L)
            int c = console_read_char();
            putchar(c);
            fflush(stdout);
            cpu->a = (uint8_t)c;
            cpu->l = (uint8_t)c;
        }
        else if (cpu->c == 2) {
            // Function 2: Console Output (Char in E)
            console_emit(cpu->e);
            fflush(stdout); // Flush buffer immediately so test prints show instantly
        }
        else if (cpu->c == 6) {
            // Function 6: Direct Console I/O. E=0FFh polls/reads (no echo,
            // 0 if nothing waiting); any other E is a character to output.
            if (cpu->e == 0xFF) {
                if (console_char_ready()) {
                    int c = console_read_char();
                    cpu->a = (uint8_t)c;
                    cpu->l = (uint8_t)c;
                } else {
                    cpu->a = 0;
                    cpu->l = 0;
                }
            } else {
                console_emit(cpu->e);
                fflush(stdout);
            }
        }
        else if (cpu->c == 9) {
            // Function 9: Print String (Address in DE, terminated by '$')
            uint16_t addr = cpu->de;
            while (ram[addr] != '$') {
                console_emit(ram[addr++]);
            }
            fflush(stdout);
        }
        else if (cpu->c == 10) {
            // Function 10: Buffered console line input. DE -> buffer where
            // byte 0 = max chars, byte 1 = actual count (written by us),
            // bytes 2.. = the characters. Basic backspace/delete editing;
            // stops at CR or when the buffer fills.
            uint16_t addr = cpu->de;
            uint8_t max_len = ram[addr];
            uint8_t count = 0;
            for (;;) {
                int c = console_read_char();
                if (c == '\r' || c == '\n') {
                    putchar('\r');
                    putchar('\n');
                    break;
                }
                if ((c == 0x08 || c == 0x7F) && count > 0) { // backspace/DEL
                    count--;
                    printf("\b \b");
                } else if (c >= 0x20 && c < 0x7F && count < max_len) {
                    ram[addr + 2 + count] = (uint8_t)c;
                    count++;
                    putchar(c);
                }
                fflush(stdout);
            }
            ram[addr + 1] = count;
        }
        else if (cpu->c == 11) {
            // Function 11: Console Status (0 = no input waiting, 0FFh = ready)
            cpu->a = console_char_ready() ? 0xFF : 0x00;
            cpu->l = cpu->a;
        }
        else if (cpu->c == 12) {
            // Function 12: Return CP/M version. B/H = system type (0 =
            // 8-bit CP/M), A/L = version in BCD-ish form (0x22 = 2.2).
            // Real software (e.g. MBASIC) checks this and refuses to run
            // if it reads back 0 - leaving this unimplemented isn't a
            // silent no-op like most unhandled functions, it looks like
            // "version 0.0" and the program just quits immediately.
            cpu->b = 0;
            cpu->h = 0;
            cpu->a = 0x22;
            cpu->l = 0x22;
        }
        else if (cpu->c == 13) {
            // Function 13: Reset disk system - log out all drives, close
            // any files a program forgot to close, select drive A.
            for (int i = 0; i < MAX_OPEN_FILES; i++) {
                if (open_files[i].in_use) fclose(open_files[i].fp);
                open_files[i].in_use = 0;
            }
            if (search_dir) { closedir(search_dir); search_dir = NULL; }
            current_drive = 0;
            dma_addr = 0x0080;
            cpu->a = 0;
        }
        else if (cpu->c == 14) {
            // Function 14: Select disk drive. Every drive maps onto the
            // same host directory (see the File I/O comment above), so
            // this just records which one is "current" for DRV_GET.
            current_drive = cpu->e;
            cpu->a = 0;
        }
        else if (cpu->c == 15) {
            // Function 15: Open file.
            uint16_t fcb_addr = cpu->de;
            char path[300];
            build_host_path(cpu, fcb_addr + 1, path, sizeof(path));
            FILE *fp = fopen(path, "rb+");
            OpenFile *of = fp ? alloc_open_file(fcb_addr) : NULL;
            if (fp && of) {
                of->fp = fp;
                struct stat st;
                long total_records = 0;
                if (stat(path, &st) == 0) total_records = (st.st_size + CPM_RECORD_SIZE - 1) / CPM_RECORD_SIZE;
                // Real CP/M's F_OPEN searches the directory for the
                // extent matching the FCB's own EX/S1/S2, letting a
                // caller reposition mid-file by setting EX (and CR)
                // before calling Open rather than always restarting from
                // the beginning - some real programs rely on exactly
                // that (a CP/M Colossal Cave Adventure port's own
                // data-file paging is what surfaced this). Since this
                // design maps a whole CP/M file onto one flat host file
                // rather than real per-extent directory entries, honor a
                // caller-supplied nonzero EX instead of always resetting
                // to 0, computing RC relative to that extent's base
                // record. EX==0 (the overwhelmingly common "just open
                // it" case) keeps the previous always-reset-CR behavior
                // unchanged, since plenty of real programs assume Open
                // zeroes CR for them in that case.
                uint8_t ex = z80_read_byte(cpu, fcb_addr + 0x0C);
                long base_record = (long)ex * CPM_RECORDS_PER_EXTENT;
                long remaining = total_records - base_record;
                if (remaining < 0) remaining = 0;
                if (remaining > CPM_RECORDS_PER_EXTENT) remaining = CPM_RECORDS_PER_EXTENT;
                z80_write_byte(cpu, fcb_addr + 0x0D, 0);              // S1
                z80_write_byte(cpu, fcb_addr + 0x0E, 0);              // S2
                z80_write_byte(cpu, fcb_addr + 0x0F, (uint8_t)remaining); // RC
                if (ex == 0) z80_write_byte(cpu, fcb_addr + 0x20, 0); // CR
                cpu->a = 0;
            } else {
                if (fp) fclose(fp);
                cpu->a = 0xFF;
            }
        }
        else if (cpu->c == 16) {
            // Function 16: Close file.
            OpenFile *of = find_open_file(cpu->de);
            if (of) {
                fclose(of->fp);
                of->in_use = 0;
                cpu->a = 0;
            } else {
                cpu->a = 0xFF;
            }
        }
        else if (cpu->c == 17 || cpu->c == 18) {
            // Function 17/18: Find first/next directory match ('?'
            // wildcards in the FCB - see fcb_pattern()).
            if (cpu->c == 17) {
                if (search_dir) closedir(search_dir);
                fcb_pattern(cpu, cpu->de, search_pattern);
                search_dir = opendir(CPM_DISK_DIR);
            }
            cpu->a = search_advance(cpu) ? 0 : 0xFF;
        }
        else if (cpu->c == 19) {
            // Function 19: Delete file(s) (wildcards allowed).
            char pat[11];
            fcb_pattern(cpu, cpu->de, pat);
            DIR *d = opendir(CPM_DISK_DIR);
            int deleted = 0;
            struct dirent *entry;
            while (d && (entry = readdir(d)) != NULL) {
                char form[11];
                if (!host_name_to_fcb_form(entry->d_name, form)) continue;
                if (!fcb_pattern_match(pat, form)) continue;
                char path[300];
                snprintf(path, sizeof(path), "%s/%s", CPM_DISK_DIR, entry->d_name);
                if (remove(path) == 0) deleted++;
            }
            if (d) closedir(d);
            cpu->a = deleted > 0 ? 0 : 0xFF;
        }
        else if (cpu->c == 20) {
            // Function 20: Sequential read, one 128-byte record at a time.
            OpenFile *of = find_open_file(cpu->de);
            if (!of) {
                cpu->a = 9; // unopened FCB
            } else {
                long rec = fcb_sequential_record(cpu, cpu->de);
                fseek(of->fp, rec * CPM_RECORD_SIZE, SEEK_SET);
                uint8_t buf[CPM_RECORD_SIZE] = {0};
                size_t n = fread(buf, 1, CPM_RECORD_SIZE, of->fp);
                if (n == 0) {
                    cpu->a = 1; // EOF
                } else {
                    for (int i = 0; i < CPM_RECORD_SIZE; i++) z80_write_byte(cpu, dma_addr + i, buf[i]);
                    fcb_set_sequential_record(cpu, cpu->de, rec + 1);
                    cpu->a = 0;
                }
            }
        }
        else if (cpu->c == 21) {
            // Function 21: Sequential write, one 128-byte record at a time.
            OpenFile *of = find_open_file(cpu->de);
            if (!of) {
                cpu->a = 9; // unopened FCB
            } else {
                long rec = fcb_sequential_record(cpu, cpu->de);
                fseek(of->fp, rec * CPM_RECORD_SIZE, SEEK_SET);
                uint8_t buf[CPM_RECORD_SIZE];
                for (int i = 0; i < CPM_RECORD_SIZE; i++) buf[i] = z80_read_byte(cpu, dma_addr + i);
                size_t n = fwrite(buf, 1, CPM_RECORD_SIZE, of->fp);
                fflush(of->fp);
                if (n != CPM_RECORD_SIZE) {
                    cpu->a = 1; // write failed (treated as "disk full")
                } else {
                    fcb_set_sequential_record(cpu, cpu->de, rec + 1);
                    cpu->a = 0;
                }
            }
        }
        else if (cpu->c == 22) {
            // Function 22: Create (and open) a new file.
            uint16_t fcb_addr = cpu->de;
            char path[300];
            build_host_path(cpu, fcb_addr + 1, path, sizeof(path));
            FILE *fp = fopen(path, "wb+");
            OpenFile *of = fp ? alloc_open_file(fcb_addr) : NULL;
            if (fp && of) {
                of->fp = fp;
                z80_write_byte(cpu, fcb_addr + 0x0C, 0); // EX
                z80_write_byte(cpu, fcb_addr + 0x0D, 0); // S1
                z80_write_byte(cpu, fcb_addr + 0x0E, 0); // S2
                z80_write_byte(cpu, fcb_addr + 0x0F, 0); // RC
                z80_write_byte(cpu, fcb_addr + 0x20, 0); // CR
                cpu->a = 0;
            } else {
                if (fp) fclose(fp);
                cpu->a = 0xFF;
            }
        }
        else if (cpu->c == 23) {
            // Function 23: Rename. New name's F1-T3 fields live at FCB+17
            // (the "FCB+16" convention counts from a DR-equivalent byte
            // at +16; the name itself starts one byte later).
            char old_path[300], new_path[300];
            build_host_path(cpu, cpu->de + 1, old_path, sizeof(old_path));
            build_host_path(cpu, cpu->de + 17, new_path, sizeof(new_path));
            cpu->a = (rename(old_path, new_path) == 0) ? 0 : 0xFF;
        }
        else if (cpu->c == 25) {
            // Function 25: Return current drive.
            cpu->a = current_drive;
        }
        else if (cpu->c == 26) {
            // Function 26: Set DMA address for subsequent read/write calls.
            dma_addr = cpu->de;
        }
        else if (cpu->c == 27) {
            // Function 27: Address of the current drive's allocation
            // bitmap - see the fake-DPB comment above BIOS_V_SELDSK's
            // definition.
            cpu->hl = ALV_BASE;
        }
        else if (cpu->c == 31) {
            // Function 31: Address of the current drive's Disk Parameter
            // Block - see the fake-DPB comment above BIOS_V_SELDSK's
            // definition.
            cpu->hl = DPB_BASE;
        }
        else if (cpu->c == 32) {
            // Function 32: Set/get current user number (0FFh in E = query).
            if (cpu->e == 0xFF) {
                cpu->a = current_user;
            } else {
                current_user = cpu->e & 0x0F;
                cpu->a = current_user;
            }
        }
        else if (cpu->c == 33 || cpu->c == 34 || cpu->c == 40) {
            // Function 33/34/40: Random-access read/write (40 = write with
            // zero-fill, which a host filesystem gives us for free when
            // writing past EOF via fseek, so it's handled identically to
            // plain random write here).
            OpenFile *of = find_open_file(cpu->de);
            if (!of) {
                cpu->a = 9; // unopened FCB
            } else {
                long rec = fcb_random_record(cpu, cpu->de);
                fseek(of->fp, rec * CPM_RECORD_SIZE, SEEK_SET);
                if (cpu->c == 33) {
                    uint8_t buf[CPM_RECORD_SIZE] = {0};
                    size_t n = fread(buf, 1, CPM_RECORD_SIZE, of->fp);
                    if (n == 0) {
                        cpu->a = 1; // reading past end of file
                    } else {
                        for (int i = 0; i < CPM_RECORD_SIZE; i++) z80_write_byte(cpu, dma_addr + i, buf[i]);
                        cpu->a = 0;
                    }
                } else {
                    uint8_t buf[CPM_RECORD_SIZE];
                    for (int i = 0; i < CPM_RECORD_SIZE; i++) buf[i] = z80_read_byte(cpu, dma_addr + i);
                    size_t n = fwrite(buf, 1, CPM_RECORD_SIZE, of->fp);
                    fflush(of->fp);
                    cpu->a = (n == CPM_RECORD_SIZE) ? 0 : 1;
                }
                fcb_set_sequential_record(cpu, cpu->de, rec); // keep CR/EX in step
            }
        }
        else if (cpu->c == 35) {
            // Function 35: Set R0-R2 to the file's size in records.
            char path[300];
            build_host_path(cpu, cpu->de + 1, path, sizeof(path));
            struct stat st;
            long records = (stat(path, &st) == 0) ? (st.st_size + CPM_RECORD_SIZE - 1) / CPM_RECORD_SIZE : 0;
            z80_write_byte(cpu, cpu->de + 0x21, (uint8_t)(records & 0xFF));
            z80_write_byte(cpu, cpu->de + 0x22, (uint8_t)((records >> 8) & 0xFF));
            z80_write_byte(cpu, cpu->de + 0x23, (uint8_t)((records >> 16) & 0xFF));
            cpu->a = 0;
        }

        // Simulate RET: Pop return address off the stack into PC
        uint8_t low = ram[cpu->sp++];
        uint8_t high = ram[cpu->sp++];
        cpu->pc = (high << 8) | low;
    }
}
