// debug/src/debug.c - the Z80 debugger shared by every machine target. See
// debug.h for how a CLI hooks it in, and docs/DEBUGGER.md for the commands.
//
// Memory is always read from the flat 64K array (cpu->memory), never
// through z80_read_byte(). That is not a shortcut: the ABC806's read hook
// latches an attribute byte whenever character RAM is read, so a debugger
// going through the hook would change the machine it is inspecting. The
// flat array is also exactly what instruction fetch sees - the core's
// fetch_byte() bypasses the hook too - so disassembly is faithful. What it
// cannot show is data the hooks divert elsewhere (the ABC802/806 character
// RAM, the ABC806's high-resolution plane). Those a machine registers as
// named spaces (z80dbg_add_space()), read and written through its own
// peek and poke, which reach the storage without going through the hook
// either.

#include "debug.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "../../disasm/src/decode.h"

#define MAX_WATCHES 16
#define MAX_SYMBOL_FILES 8
#define MAX_BREAK_NAMES 32
#define MAX_SPACES 8

// One line of a symbol file (z80asm -s). Only labels name addresses in the
// output: an EQU may be a plain number that happens to equal an address.
// Both kinds are accepted wherever an address is typed.
typedef struct {
    char name[64];
    uint16_t value;
    bool is_label;
} DbgSymbol;

// A place `m`, `e` and `w` can name: the CPU's 64K (space -1), or an
// offset into one of the machine's registered spaces.
typedef struct {
    int space;
    uint32_t addr;
} Location;

typedef struct {
    Location at;
    uint16_t len;
    uint8_t *snapshot;
} Watch;

struct Z80Debugger {
    uint8_t breakpoints[65536 / 8];
    bool stop_next;          // prompt before the next instruction
    long step_left;          // instructions still to run under `s`, reported
    long next_sp;            // `n`: stop when SP is back up to this, or -1
    bool detached;           // commands ran out: never stop again
    uint16_t last_pc;        // the instruction after_step() is reporting on
    Watch watches[MAX_WATCHES];
    int watch_count;

    const char *script_path;
    FILE *in;
    bool in_is_tty;
    struct termios saved_termios;
    bool termios_saved;
    char last_command[256];

    DbgSymbol *symbols;
    int symbol_count;
    int *label_at;           // 64K: the label naming each address, or -1
    const char *symbol_paths[MAX_SYMBOL_FILES];
    int symbol_path_count;
    const char *break_names[MAX_BREAK_NAMES];   // --break, resolved at start
    int break_name_count;
    Z80DbgSpace spaces[MAX_SPACES];
    int space_count;
    double seconds_stopped;  // at the prompt, for z80dbg_seconds_stopped()

    // Async mode (z80dbg_set_async()): input is read by the caller's main
    // loop into `pending`, and a stop leaves `waiting` set until a command
    // resumes. Lines left over after a resume wait for the next stop, so a
    // script behaves as it does in the blocking prompt.
    bool async;
    bool waiting;
    struct timespec stopped_at;
    char pending[4096];
    size_t pending_len;
    bool input_ended;
    // Set when z80dbg_poll_input() resumed: the next before_step() is for
    // the instruction the prompt stopped on, and must run it rather than
    // stop on the same breakpoint again, as the blocking prompt's caller
    // does by simply carrying on.
    bool resumed;
};

static volatile sig_atomic_t interrupted;

static void on_sigint(int sig) {
    (void)sig;
    interrupted = 1;
}

static Z80Debugger *debugger_new(void) {
    Z80Debugger *dbg = calloc(1, sizeof *dbg);
    if (!dbg) {
        perror("z80dbg");
        exit(EXIT_FAILURE);
    }
    dbg->next_sp = -1;
    return dbg;
}

// Addresses and values are hex, as they are everywhere else in Z80 work:
// "1234", "0x1234", "$1234" and "1234h" all mean 0x1234.
static bool parse_hex(const char *text, unsigned long max, unsigned long *out) {
    char buf[32];
    size_t n = strlen(text);
    if (n == 0 || n >= sizeof buf) return false;
    memcpy(buf, text, n + 1);
    char *p = buf;
    if (*p == '$') p++;
    else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    size_t len = strlen(p);
    if (len > 1 && (p[len - 1] == 'h' || p[len - 1] == 'H')) p[--len] = '\0';
    if (len == 0) return false;
    char *end;
    unsigned long v = strtoul(p, &end, 16);
    if (*end != '\0' || v > max) return false;
    *out = v;
    return true;
}

// Counts (steps, lines, bytes) are decimal.
static bool parse_count(const char *text, long *out) {
    char *end;
    long v = strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' || v <= 0) return false;
    *out = v;
    return true;
}

static const DbgSymbol *find_symbol(const Z80Debugger *dbg, const char *name) {
    for (int k = 0; k < dbg->symbol_count; k++)
        if (strcmp(dbg->symbols[k].name, name) == 0) return &dbg->symbols[k];
    return NULL;
}

// An address as typed: a symbol, a symbol plus or minus a hex offset, or a
// hex number. A symbol wins over a number spelled the same way ("add",
// "beef"); $BEEF, 0xBEEF and 0BEEFh can only be numbers, since no name
// starts with $ or a digit.
static bool resolve_addr(const Z80Debugger *dbg, const char *text, unsigned long max, unsigned long *out) {
    if (!*text) return false;
    const char *op = strpbrk(text + 1, "+-");
    size_t n = op ? (size_t)(op - text) : strlen(text);
    char base[64];
    if (n >= sizeof base) return false;
    memcpy(base, text, n);
    base[n] = '\0';

    unsigned long v;
    const DbgSymbol *sym = find_symbol(dbg, base);
    if (sym) v = sym->value;
    else if (!parse_hex(base, 0xFFFF, &v)) return false;
    if (op) {
        unsigned long off;
        if (!parse_hex(op + 1, 0xFFFF, &off)) return false;
        v = ((*op == '+') ? v + off : v - off) & 0xFFFF;   // wraps as the Z80 does
    }
    if (v > max) return false;
    *out = v;
    return true;
}

// Reads z80asm -s output: `name equ value ; label|equ`. A line without the
// comment counts as a label, so a hand-written file works too. Values that
// are not 16-bit addresses (negative, too large) are skipped.
static bool load_symbols(Z80Debugger *dbg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "z80dbg: cannot open symbols '%s'\n", path);
        return false;
    }
    char line[256];
    int loaded = 0;
    while (fgets(line, sizeof line, f)) {
        bool is_label = true;
        char *semi = strchr(line, ';');
        if (semi) {
            // Only the comment's first word decides, so a hand-written
            // comment that merely contains "equ" ("frequency") stays a label.
            char kind[8] = "";
            sscanf(semi + 1, "%7s", kind);
            is_label = strcasecmp(kind, "equ") != 0;
            *semi = '\0';
        }
        char name[64], equ[8], value[32];
        unsigned long v;
        if (sscanf(line, "%63s %7s %31s", name, equ, value) != 3) continue;
        if (strcasecmp(equ, "equ") != 0 || !parse_hex(value, 0xFFFF, &v)) continue;

        DbgSymbol *grown = realloc(dbg->symbols, (size_t)(dbg->symbol_count + 1) * sizeof *grown);
        if (!grown) break;
        dbg->symbols = grown;
        DbgSymbol *sym = &dbg->symbols[dbg->symbol_count++];
        snprintf(sym->name, sizeof sym->name, "%s", name);
        sym->value = (uint16_t)v;
        sym->is_label = is_label;
        loaded++;
    }
    fclose(f);
    fprintf(stderr, "[z80dbg: %d symbols from %s]\n", loaded, path);
    return true;
}

static const char *label_for(const Z80Debugger *dbg, uint16_t addr) {
    if (!dbg->label_at || dbg->label_at[addr] < 0) return NULL;
    return dbg->symbols[dbg->label_at[addr]].name;
}

// Includes a stop still in progress, which only async mode can be asked
// about: a window's timer keeps ticking at the prompt, and a total that
// left the current stop out would jump back by its whole length on
// resuming, so anything scheduled against it (the next redraw) would wait
// that long again.
double z80dbg_seconds_stopped(const Z80Debugger *dbg) {
    if (!dbg) return 0.0;
    double total = dbg->seconds_stopped;
    if (dbg->async && dbg->waiting) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        total += (double)(now.tv_sec - dbg->stopped_at.tv_sec) +
                 (double)(now.tv_nsec - dbg->stopped_at.tv_nsec) / 1e9;
    }
    return total;
}

void z80dbg_add_space(Z80Debugger *dbg, const Z80DbgSpace *space) {
    if (!dbg || dbg->space_count == MAX_SPACES) return;
    dbg->spaces[dbg->space_count++] = *space;
}

// NAME:OFFSET names a registered space, with the offset in hex; anything
// else is a CPU address as resolve_addr() reads it. Only the CPU's
// addresses take symbols, since a symbol file describes that 64K.
static bool resolve_location(const Z80Debugger *dbg, const char *text, Location *out) {
    const char *colon = strchr(text, ':');
    if (!colon) {
        unsigned long a;
        if (!resolve_addr(dbg, text, 0xFFFF, &a)) return false;
        out->space = -1;
        out->addr = (uint32_t)a;
        return true;
    }
    for (int k = 0; k < dbg->space_count; k++) {
        const Z80DbgSpace *sp = &dbg->spaces[k];
        if (strlen(sp->name) != (size_t)(colon - text) || strncasecmp(sp->name, text, (size_t)(colon - text)))
            continue;
        unsigned long off;
        if (!parse_hex(colon + 1, sp->size - 1, &off)) return false;
        out->space = k;
        out->addr = (uint32_t)off;
        return true;
    }
    return false;
}

// Bytes from here to the end of the location's memory: a dump or watch in a
// space stops at its end rather than wrapping, since offset 0 does not
// follow the last byte the way 0000 follows FFFF.
static long location_room(const Z80Debugger *dbg, Location at) {
    return at.space < 0 ? 0x10000 : (long)(dbg->spaces[at.space].size - at.addr);
}

static uint8_t location_peek(const Z80Debugger *dbg, const Z80 *cpu, Location at, long off) {
    if (at.space < 0) return cpu->memory[(uint16_t)(at.addr + (unsigned long)off)];
    return dbg->spaces[at.space].peek(at.addr + (uint32_t)off);
}

static void location_poke(const Z80Debugger *dbg, Z80 *cpu, Location at, long off, uint8_t v) {
    if (at.space < 0) cpu->memory[(uint16_t)(at.addr + (unsigned long)off)] = v;
    else dbg->spaces[at.space].poke(at.addr + (uint32_t)off, v);
}

// "7800" for the CPU, "chr:0000" for a space, wide enough for its size.
static void location_text(const Z80Debugger *dbg, Location at, long off, char *buf, size_t n) {
    if (at.space < 0) {
        snprintf(buf, n, "%04X", (uint16_t)(at.addr + (unsigned long)off));
        return;
    }
    const Z80DbgSpace *sp = &dbg->spaces[at.space];
    snprintf(buf, n, "%s:%0*X", sp->name, sp->size > 0x10000 ? 5 : 4, at.addr + (uint32_t)off);
}

bool z80dbg_parse_option(Z80Debugger **dbg, int argc, char **argv, int *i) {
    const char *arg = argv[*i];
    if (strcmp(arg, "--debug") == 0) {
        if (!*dbg) *dbg = debugger_new();
        (*dbg)->stop_next = true;
        return true;
    }
    if (strcmp(arg, "--break") == 0 || strcmp(arg, "--debug-script") == 0 ||
        strcmp(arg, "--symbols") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "%s needs a value\n", arg);
            exit(EXIT_FAILURE);
        }
        const char *value = argv[++*i];
        if (!*dbg) *dbg = debugger_new();
        // --break may name a symbol, and the symbol files are not read
        // until z80dbg_start(), so both are kept and dealt with there.
        if (arg[2] == 'b') {
            if ((*dbg)->break_name_count == MAX_BREAK_NAMES) {
                fprintf(stderr, "--break: at most %d\n", MAX_BREAK_NAMES);
                exit(EXIT_FAILURE);
            }
            (*dbg)->break_names[(*dbg)->break_name_count++] = value;
        } else if (arg[2] == 's') {
            if ((*dbg)->symbol_path_count == MAX_SYMBOL_FILES) {
                fprintf(stderr, "--symbols: at most %d files\n", MAX_SYMBOL_FILES);
                exit(EXIT_FAILURE);
            }
            (*dbg)->symbol_paths[(*dbg)->symbol_path_count++] = value;
        } else {
            (*dbg)->script_path = value;
        }
        return true;
    }
    return false;
}

void z80dbg_print_usage(FILE *out) {
    fprintf(out, "  --debug          stop in the debugger before the first instruction\n");
    fprintf(out, "  --break ADDR     stop in the debugger when PC reaches ADDR (hex, or a\n");
    fprintf(out, "                   symbol); repeatable\n");
    fprintf(out, "  --symbols F      name addresses from F, as written by z80asm -s;\n");
    fprintf(out, "                   repeatable\n");
    fprintf(out, "  --debug-script F read debugger commands from F instead of the\n");
    fprintf(out, "                   terminal; at its end the run continues undisturbed.\n");
    fprintf(out, "                   See docs/DEBUGGER.md for the commands\n");
    fprintf(out, "  With --interactive, where Ctrl-C belongs to the machine, Ctrl-] or\n");
    fprintf(out, "  F12 stops in the debugger instead.\n");
}

bool z80dbg_start(Z80Debugger *dbg) {
    for (int k = 0; k < dbg->symbol_path_count; k++)
        if (!load_symbols(dbg, dbg->symbol_paths[k])) return false;
    if (dbg->symbol_count) {
        dbg->label_at = malloc(65536 * sizeof *dbg->label_at);
        if (!dbg->label_at) return false;
        for (int a = 0; a < 65536; a++) dbg->label_at[a] = -1;
        for (int k = 0; k < dbg->symbol_count; k++)   // the first label wins
            if (dbg->symbols[k].is_label && dbg->label_at[dbg->symbols[k].value] < 0)
                dbg->label_at[dbg->symbols[k].value] = k;
    }
    for (int k = 0; k < dbg->break_name_count; k++) {
        unsigned long a;
        if (!resolve_addr(dbg, dbg->break_names[k], 0xFFFF, &a)) {
            fprintf(stderr, "--break: '%s' is neither a symbol nor an address\n", dbg->break_names[k]);
            return false;
        }
        dbg->breakpoints[a >> 3] |= (uint8_t)(1u << (a & 7));
    }

    if (dbg->script_path) {
        dbg->in = fopen(dbg->script_path, "r");
        if (!dbg->in) {
            fprintf(stderr, "z80dbg: cannot open '%s'\n", dbg->script_path);
            return false;
        }
    } else {
        // Not stdin: a CP/M program is reading that as its console.
        dbg->in = fopen("/dev/tty", "r");
        if (!dbg->in) {
            fprintf(stderr, "z80dbg: no terminal to read commands from; use --debug-script\n");
            return false;
        }
    }
    dbg->in_is_tty = isatty(fileno(dbg->in));
    if (dbg->async) {
        // The caller's main loop reads this fd; a read must never block it.
        int fl = fcntl(fileno(dbg->in), F_GETFL);
        if (fl >= 0) fcntl(fileno(dbg->in), F_SETFL, fl | O_NONBLOCK);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    return true;
}

// ------------------------------------------------------------------ output

// With symbols loaded, a labelled address gets a `name:` line above it and
// a jump, call or (nn) target is named in the text, the way z80dasm names
// its own labels. Immediate values stay numbers, as they do there: nothing
// in `LD DE,0134h` says 0134h is an address.
static void print_insn(const Z80Debugger *dbg, FILE *out, const Z80 *cpu, uint16_t pc) {
    DecodedInsn d = decode_instruction(cpu->memory, pc);
    char bytes[16] = "";
    for (int k = 0; k < d.length && k < 4; k++) {
        char b[4];
        snprintf(b, sizeof b, k ? " %02X" : "%02X", cpu->memory[(uint16_t)(pc + k)]);
        strcat(bytes, b);
    }

    const char *here = label_for(dbg, pc);
    if (here) fprintf(out, "%s:\n", here);

    char text[128];
    snprintf(text, sizeof text, "%s", d.text);
    const char *target = d.has_ref ? label_for(dbg, d.ref_addr) : NULL;
    if (target) {
        char hex[8];   // as decode.c writes it: a leading 0 before A-F
        snprintf(hex, sizeof hex, (d.ref_addr >> 12) >= 0xA ? "0%04Xh" : "%04Xh", d.ref_addr);
        char *pos = strstr(text, hex);
        if (pos) {
            char rest[128];
            snprintf(rest, sizeof rest, "%s", pos + strlen(hex));
            snprintf(pos, sizeof text - (size_t)(pos - text), "%s%s", target, rest);
        }
    }
    fprintf(out, "%04X  %-12s %s\n", pc, bytes, text);
}

static void flags_text(uint8_t f, char out[9]) {
    static const char names[] = "SZYHXPNC";
    for (int k = 0; k < 8; k++) out[k] = (f & (0x80 >> k)) ? names[k] : '.';
    out[8] = '\0';
}

static void print_regs(FILE *out, const Z80 *cpu, bool with_alternates) {
    char flags[9];
    flags_text(cpu->f, flags);
    fprintf(out, "AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X PC=%04X  %s  IM%u %s\n",
            cpu->af, cpu->bc, cpu->de, cpu->hl, cpu->ix, cpu->iy, cpu->sp, cpu->pc,
            flags, cpu->im, cpu->iff1 ? "EI" : "DI");
    if (with_alternates) {
        fprintf(out, "AF'=%04X BC'=%04X DE'=%04X HL'=%04X I=%02X R=%02X\n",
                cpu->af_alt, cpu->bc_alt, cpu->de_alt, cpu->hl_alt, cpu->i, cpu->r);
    }
}

static void dump_memory(FILE *out, const Z80Debugger *dbg, const Z80 *cpu, Location at, long len) {
    if (len > location_room(dbg, at)) len = location_room(dbg, at);
    for (long off = 0; off < len; off += 16) {
        char where[32];
        location_text(dbg, at, off, where, sizeof where);
        fprintf(out, "%s ", where);
        char ascii[17];
        int n = (int)((len - off) < 16 ? (len - off) : 16);
        for (int k = 0; k < 16; k++) {
            if (k < n) {
                uint8_t b = location_peek(dbg, cpu, at, off + k);
                fprintf(out, " %02X", b);
                ascii[k] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
            } else {
                fprintf(out, "   ");
                ascii[k] = ' ';
            }
        }
        ascii[16] = '\0';
        fprintf(out, "  %s\n", ascii);
    }
}

static void print_help(FILE *out) {
    fprintf(out,
        "  s [n]            step n instructions (default 1), showing each\n"
        "  n                step, running a CALL or RST through to its return\n"
        "  c                continue to a breakpoint, watchpoint or the end\n"
        "  b [addr...]      set breakpoints, or list them\n"
        "  d addr|all       delete a breakpoint, or all of them\n"
        "  w [addr [len]]   stop when those bytes change, or list watches\n"
        "  r [reg=val]      show all registers, or set one (a, bc, ix, af', pc...)\n"
        "  m addr [len]     dump memory (default 64 bytes)\n"
        "  e addr byte...   write bytes to memory, then show them\n"
        "  u [addr] [n]     disassemble n instructions (default: PC, 8)\n"
        "  q                end the run\n"
        "Addresses and values are hex; counts are decimal. An empty line\n"
        "repeats s or n.\n");
}

static void print_spaces(FILE *out, const Z80Debugger *dbg) {
    if (!dbg->space_count) return;
    fprintf(out, "m, e and w also take space:offset, reading this machine's memory\n"
                 "where its bus diverts it, without the side effects of a CPU access:\n");
    for (int k = 0; k < dbg->space_count; k++)
        fprintf(out, "  %-8s %s, offsets 0-%X\n", dbg->spaces[k].name, dbg->spaces[k].what,
                dbg->spaces[k].size - 1);
}

// ---------------------------------------------------------------- terminal

// The CP/M target puts the terminal in raw mode for its console, so the
// prompt restores line editing and echo while it reads, and puts back
// whatever the machine had on the way out.
static void enter_prompt_terminal(Z80Debugger *dbg) {
    if (!dbg->in_is_tty) return;
    int fd = fileno(dbg->in);
    if (tcgetattr(fd, &dbg->saved_termios) != 0) return;
    dbg->termios_saved = true;
    struct termios cooked = dbg->saved_termios;
    cooked.c_lflag |= ICANON | ECHO | ISIG;
    cooked.c_iflag |= ICRNL;
    tcsetattr(fd, TCSANOW, &cooked);
}

static void leave_prompt_terminal(Z80Debugger *dbg) {
    if (!dbg->termios_saved) return;
    tcsetattr(fileno(dbg->in), TCSANOW, &dbg->saved_termios);
    dbg->termios_saved = false;
}

// ---------------------------------------------------------------- commands

static bool is_call_or_rst(const Z80 *cpu) {
    uint8_t op = cpu->memory[cpu->pc];
    if (op == 0xCD) return true;               // CALL nn
    if ((op & 0xC7) == 0xC4) return true;      // CALL cc,nn
    if ((op & 0xC7) == 0xC7) return true;      // RST p
    return false;
}

static bool set_register(Z80 *cpu, const char *name, unsigned long v) {
    struct { const char *name; void *field; bool wide; } regs[] = {
        {"a", &cpu->a, false}, {"f", &cpu->f, false}, {"b", &cpu->b, false},
        {"c", &cpu->c, false}, {"d", &cpu->d, false}, {"e", &cpu->e, false},
        {"h", &cpu->h, false}, {"l", &cpu->l, false}, {"i", &cpu->i, false},
        {"r", &cpu->r, false},
        {"af", &cpu->af, true}, {"bc", &cpu->bc, true}, {"de", &cpu->de, true},
        {"hl", &cpu->hl, true}, {"ix", &cpu->ix, true}, {"iy", &cpu->iy, true},
        {"sp", &cpu->sp, true}, {"pc", &cpu->pc, true},
        {"af'", &cpu->af_alt, true}, {"bc'", &cpu->bc_alt, true},
        {"de'", &cpu->de_alt, true}, {"hl'", &cpu->hl_alt, true},
    };
    for (size_t k = 0; k < sizeof regs / sizeof regs[0]; k++) {
        if (strcasecmp(name, regs[k].name) != 0) continue;
        if (regs[k].wide) {
            if (v > 0xFFFF) return false;
            *(uint16_t *)regs[k].field = (uint16_t)v;
        } else {
            if (v > 0xFF) return false;
            *(uint8_t *)regs[k].field = (uint8_t)v;
        }
        return true;
    }
    return false;
}

static void print_watch(FILE *out, const Z80Debugger *dbg, const Watch *w) {
    char where[32];
    location_text(dbg, w->at, 0, where, sizeof where);
    fprintf(out, "watching %s, %u byte%s\n", where, w->len, w->len == 1 ? "" : "s");
}

static void add_watch(Z80Debugger *dbg, const Z80 *cpu, Location at, long len, FILE *out) {
    if (dbg->watch_count == MAX_WATCHES) {
        fprintf(out, "at most %d watches\n", MAX_WATCHES);
        return;
    }
    if (len > 0xFFFF) len = 0xFFFF;
    if (len > location_room(dbg, at)) len = location_room(dbg, at);
    Watch *w = &dbg->watches[dbg->watch_count];
    w->snapshot = malloc((size_t)len);
    if (!w->snapshot) return;
    w->at = at;
    w->len = (uint16_t)len;
    for (long k = 0; k < len; k++) w->snapshot[k] = location_peek(dbg, cpu, at, k);
    dbg->watch_count++;
    print_watch(out, dbg, w);
}

// Reads and runs commands until one resumes execution. Returns
// Z80DBG_QUIT if the user ended the run.
// Runs one command line. The line may be edited in place (an empty one
// becomes the last s or n). Shared by the blocking prompt and by
// z80dbg_poll_input(), so the two cannot come to disagree.
enum { CMD_STAY, CMD_RESUME, CMD_QUIT };

static int run_command(Z80Debugger *dbg, Z80 *cpu, char *line) {
    FILE *out = stderr;
    line[strcspn(line, "\r\n")] = '\0';
    char *start = line;
    while (isspace((unsigned char)*start)) start++;
    if (*start == '\0') {
        if (dbg->last_command[0] == '\0') return CMD_STAY;
        strcpy(line, dbg->last_command);
        start = line;
    }

    char *argv[64];   // enough for a long `e`
    int argc = 0;
    char copy[256];
    strcpy(copy, start);
    for (char *tok = strtok(copy, " \t"); tok && argc < 64; tok = strtok(NULL, " \t"))
        argv[argc++] = tok;
    const char *cmd = argv[0];
    dbg->last_command[0] = '\0';

    if (!strcmp(cmd, "s") || !strcmp(cmd, "step")) {
        long n = 1;
        if (argc > 1 && !parse_count(argv[1], &n)) {
            fprintf(out, "s: '%s' is not a count\n", argv[1]);
            return CMD_STAY;
        }
        dbg->step_left = n;
        strcpy(dbg->last_command, start);   // an empty line repeats `s 2` as `s 2`
        return CMD_RESUME;
    } else if (!strcmp(cmd, "n") || !strcmp(cmd, "next")) {
        strcpy(dbg->last_command, "n");
        if (is_call_or_rst(cpu)) {
            dbg->next_sp = cpu->sp;
        } else {
            dbg->step_left = 1;
        }
        return CMD_RESUME;
    } else if (!strcmp(cmd, "c") || !strcmp(cmd, "cont")) {
        return CMD_RESUME;
    } else if (!strcmp(cmd, "b") || !strcmp(cmd, "break")) {
        if (argc == 1) {
            int listed = 0;
            for (unsigned a = 0; a < 0x10000; a++) {
                if (dbg->breakpoints[a >> 3] & (1u << (a & 7))) {
                    const char *name = label_for(dbg, (uint16_t)a);
                    fprintf(out, "breakpoint %04X%s%s\n", a, name ? " " : "", name ? name : "");
                    listed++;
                }
            }
            if (!listed) fprintf(out, "no breakpoints\n");
        }
        for (int k = 1; k < argc; k++) {
            unsigned long a;
            if (!resolve_addr(dbg, argv[k], 0xFFFF, &a)) {
                fprintf(out, "b: '%s' is neither a symbol nor an address\n", argv[k]);
                continue;
            }
            dbg->breakpoints[a >> 3] |= (uint8_t)(1u << (a & 7));
            const char *name = label_for(dbg, (uint16_t)a);
            fprintf(out, "breakpoint %04lX%s%s\n", a, name ? " " : "", name ? name : "");
        }
    } else if (!strcmp(cmd, "d") || !strcmp(cmd, "delete")) {
        if (argc > 1 && !strcmp(argv[1], "all")) {
            memset(dbg->breakpoints, 0, sizeof dbg->breakpoints);
            fprintf(out, "all breakpoints deleted\n");
        } else {
            unsigned long a;
            if (argc < 2 || !resolve_addr(dbg, argv[1], 0xFFFF, &a)) {
                fprintf(out, "d: give an address, or 'all'\n");
                return CMD_STAY;
            }
            dbg->breakpoints[a >> 3] &= (uint8_t)~(1u << (a & 7));
            fprintf(out, "breakpoint %04lX deleted\n", a);
        }
    } else if (!strcmp(cmd, "w") || !strcmp(cmd, "watch")) {
        if (argc == 1) {
            if (!dbg->watch_count) fprintf(out, "no watches\n");
            for (int k = 0; k < dbg->watch_count; k++) print_watch(out, dbg, &dbg->watches[k]);
            return CMD_STAY;
        }
        Location at;
        long len = 1;
        if (!resolve_location(dbg, argv[1], &at) || (argc > 2 && !parse_count(argv[2], &len))) {
            fprintf(out, "w: usage is w addr [len]\n");
            return CMD_STAY;
        }
        add_watch(dbg, cpu, at, len, out);
    } else if (!strcmp(cmd, "r") || !strcmp(cmd, "regs")) {
        if (argc == 1) {
            print_regs(out, cpu, true);
            return CMD_STAY;
        }
        char *eq = strchr(argv[1], '=');
        unsigned long v;
        if (!eq || (*eq = '\0', !resolve_addr(dbg, eq + 1, 0xFFFF, &v)) || !set_register(cpu, argv[1], v)) {
            fprintf(out, "r: usage is r reg=value, e.g. r hl=1234 or r a=0f\n");
            return CMD_STAY;
        }
        print_regs(out, cpu, false);
    } else if (!strcmp(cmd, "m") || !strcmp(cmd, "mem")) {
        Location at;
        long len = 64;
        if (argc < 2 || !resolve_location(dbg, argv[1], &at) || (argc > 2 && !parse_count(argv[2], &len))) {
            fprintf(out, "m: usage is m addr [len]\n");
            return CMD_STAY;
        }
        dump_memory(out, dbg, cpu, at, len);
    } else if (!strcmp(cmd, "e") || !strcmp(cmd, "enter")) {
        // A CPU address writes the flat array, like every other access
        // here: it can patch code in a ROM image. A space:offset writes
        // through the machine's poke instead. Every byte is checked
        // before any is written, so a typo writes nothing, and a write
        // that would run off the end of a space is refused whole.
        Location at;
        unsigned long v;
        uint8_t bytes[64];
        int n = 0;
        bool ok = argc >= 3 && resolve_location(dbg, argv[1], &at);
        for (int k = 2; ok && k < argc; k++) {
            ok = parse_hex(argv[k], 0xFF, &v);
            bytes[n++] = (uint8_t)v;
        }
        if (!ok || n > location_room(dbg, at)) {
            fprintf(out, "e: usage is e addr byte..., bytes in hex (00-FF)\n");
            return CMD_STAY;
        }
        for (int k = 0; k < n; k++) {
            location_poke(dbg, cpu, at, k, bytes[k]);
            // A watch must not report the debugger's own write as a
            // change the program made.
            for (int w = 0; w < dbg->watch_count; w++) {
                Watch *wt = &dbg->watches[w];
                if (wt->at.space != at.space) continue;
                uint32_t off = at.space < 0 ? (uint16_t)(at.addr + (uint32_t)k - wt->at.addr)
                                            : at.addr + (uint32_t)k - wt->at.addr;
                if (off < wt->len) wt->snapshot[off] = bytes[k];
            }
        }
        dump_memory(out, dbg, cpu, at, n);
    } else if (!strcmp(cmd, "u") || !strcmp(cmd, "dis")) {
        unsigned long a = cpu->pc;
        long n = 8;
        if ((argc > 1 && !resolve_addr(dbg, argv[1], 0xFFFF, &a)) || (argc > 2 && !parse_count(argv[2], &n))) {
            fprintf(out, "u: usage is u [addr] [n]\n");
            return CMD_STAY;
        }
        uint16_t pc = (uint16_t)a;
        for (long k = 0; k < n; k++) {
            print_insn(dbg, out, cpu, pc);
            pc = (uint16_t)(pc + decode_instruction(cpu->memory, pc).length);
        }
    } else if (!strcmp(cmd, "q") || !strcmp(cmd, "quit")) {
        return CMD_QUIT;
        return CMD_RESUME;
    } else if (!strcmp(cmd, "h") || !strcmp(cmd, "help") || !strcmp(cmd, "?")) {
        print_help(out);
        print_spaces(out, dbg);
    } else {
        fprintf(out, "unknown command '%s'; h for help\n", cmd);
    }
    return CMD_STAY;
}

static int prompt(Z80Debugger *dbg, Z80 *cpu) {
    FILE *out = stderr;
    enter_prompt_terminal(dbg);

    int result = Z80DBG_RUN;
    for (;;) {
        fprintf(out, "(z80dbg) ");
        fflush(out);
        char line[256];
        if (!fgets(line, sizeof line, dbg->in)) {
            fprintf(out, "\n[z80dbg: end of commands, continuing without the debugger]\n");
            dbg->detached = true;
            break;
        }
        if (!dbg->in_is_tty) fputs(line, out);   // a script's log reads like a session

        int r = run_command(dbg, cpu, line);
        if (r == CMD_STAY) continue;
        if (r == CMD_QUIT) result = Z80DBG_QUIT;
        break;
    }

    leave_prompt_terminal(dbg);
    interrupted = 0;   // a Ctrl-C typed at the prompt is not a request to stop again
    return result;
}

// ------------------------------------------------------------------- async

void z80dbg_set_async(Z80Debugger *dbg, bool async) { dbg->async = async; }
// Only a terminal needs watching. A script is read by the debugger itself
// whenever it wants the next line, since a file is always readable and a
// watch on it would spin the caller's main loop.
int z80dbg_input_fd(const Z80Debugger *dbg) {
    return dbg->in && dbg->in_is_tty && !dbg->input_ended ? fileno(dbg->in) : -1;
}
bool z80dbg_is_stopped(const Z80Debugger *dbg) { return dbg && dbg->waiting; }

void z80dbg_request_stop(Z80Debugger *dbg) {
    if (dbg && !dbg->detached) dbg->stop_next = true;
}

static void prompt_text(void) {
    fprintf(stderr, "(z80dbg) ");
    fflush(stderr);
}

static void end_async_stop(Z80Debugger *dbg) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    dbg->seconds_stopped += (double)(now.tv_sec - dbg->stopped_at.tv_sec) +
                            (double)(now.tv_nsec - dbg->stopped_at.tv_nsec) / 1e9;
    dbg->waiting = false;
    dbg->resumed = true;
    leave_prompt_terminal(dbg);
    interrupted = 0;   // as in prompt(): a Ctrl-C at the prompt is not a new stop
}

// One read of whatever input is available, into `pending`.
static void fill_pending(Z80Debugger *dbg) {
    if (!dbg->in || dbg->input_ended || dbg->pending_len == sizeof dbg->pending) return;
    ssize_t got = read(fileno(dbg->in), dbg->pending + dbg->pending_len,
                       sizeof dbg->pending - dbg->pending_len);
    if (got > 0) dbg->pending_len += (size_t)got;
    else if (got == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
        dbg->input_ended = true;
}

// Runs the complete lines already read, while the prompt is open. Stops at
// a line that resumes, leaving any after it for the next stop.
static int drain_pending(Z80Debugger *dbg, Z80 *cpu) {
    while (dbg->waiting) {
        char *nl = memchr(dbg->pending, '\n', dbg->pending_len);
        if (!nl && !dbg->in_is_tty && !dbg->input_ended) {
            fill_pending(dbg);   // a script: the next line is always there to read
            continue;
        }
        if (!nl && !(dbg->input_ended && dbg->pending_len)) {
            if (!dbg->input_ended) return Z80DBG_STOPPED;
            fprintf(stderr, "\n[z80dbg: end of commands, continuing without the debugger]\n");
            dbg->detached = true;
            end_async_stop(dbg);
            return Z80DBG_RUN;
        }
        size_t n = nl ? (size_t)(nl - dbg->pending) + 1 : dbg->pending_len;
        char line[256];
        size_t keep = n < sizeof line - 1 ? n : sizeof line - 1;
        memcpy(line, dbg->pending, keep);
        line[keep] = '\0';
        memmove(dbg->pending, dbg->pending + n, dbg->pending_len - n);
        dbg->pending_len -= n;
        if (!dbg->in_is_tty) {   // a script's log reads like a session
            fputs(line, stderr);
            if (!nl) fputc('\n', stderr);
        }

        int r = run_command(dbg, cpu, line);
        if (r == CMD_STAY) {
            prompt_text();
            continue;
        }
        end_async_stop(dbg);
        return r == CMD_QUIT ? Z80DBG_QUIT : Z80DBG_RUN;
    }
    return Z80DBG_RUN;
}

int z80dbg_poll_input(Z80Debugger *dbg, Z80 *cpu) {
    fill_pending(dbg);
    return dbg->waiting ? drain_pending(dbg, cpu) : Z80DBG_RUN;
}

// ------------------------------------------------------------------- hooks

int z80dbg_before_step(Z80Debugger *dbg, Z80 *cpu) {
    if (dbg->detached) return Z80DBG_RUN;
    if (dbg->waiting) return Z80DBG_STOPPED;
    if (dbg->resumed) {
        dbg->resumed = false;
        dbg->last_pc = cpu->pc;
        if (dbg->step_left > 0) print_insn(dbg, stderr, cpu, cpu->pc);
        return Z80DBG_RUN;
    }
    uint16_t pc = cpu->pc;

    // `n` waits for the stack to come back to its depth before the call,
    // not for PC to equal the return address. The two agree on a plain
    // machine, but CP/M's z80_step() runs an intercepted BDOS call *and*
    // the instruction it returns to in one step, so PC is never seen at
    // the return address. Signed, so a stack that wraps past 0000 still
    // counts as deeper.
    bool returned = dbg->next_sp >= 0 && (int16_t)(cpu->sp - (uint16_t)dbg->next_sp) >= 0;

    const char *why = NULL;
    if (interrupted) why = "interrupted";
    else if (dbg->breakpoints[pc >> 3] & (1u << (pc & 7))) why = "breakpoint";
    else if (returned) why = "returned";
    else if (dbg->stop_next) why = "stopped";

    if (why) {
        dbg->stop_next = false;
        dbg->step_left = 0;
        dbg->next_sp = -1;
        fflush(stdout);   // the program's output so far lands before ours
        fprintf(stderr, "[%s] ", why);
        print_insn(dbg, stderr, cpu, pc);
        if (dbg->async) {
            // Commands already read (a script's next lines, or typed ahead)
            // run now; otherwise the caller's main loop brings them.
            enter_prompt_terminal(dbg);
            clock_gettime(CLOCK_MONOTONIC, &dbg->stopped_at);
            dbg->waiting = true;
            prompt_text();
            int result = drain_pending(dbg, cpu);
            dbg->resumed = false;   // resumed here, so this call runs it
            if (result != Z80DBG_RUN) return result;
            if (dbg->detached) return Z80DBG_RUN;
            dbg->last_pc = cpu->pc;
            if (dbg->step_left > 0) print_insn(dbg, stderr, cpu, cpu->pc);
            return Z80DBG_RUN;
        }
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int result = prompt(dbg, cpu);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        dbg->seconds_stopped += (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
        if (result == Z80DBG_QUIT) return Z80DBG_QUIT;
        if (dbg->detached) return Z80DBG_RUN;
    }

    dbg->last_pc = cpu->pc;
    if (dbg->step_left > 0) print_insn(dbg, stderr, cpu, cpu->pc);
    return Z80DBG_RUN;
}

void z80dbg_after_step(Z80Debugger *dbg, Z80 *cpu) {
    if (dbg->detached) return;

    if (dbg->step_left > 0) {
        print_regs(stderr, cpu, false);
        if (--dbg->step_left == 0) dbg->stop_next = true;
    }

    for (int k = 0; k < dbg->watch_count; k++) {
        Watch *w = &dbg->watches[k];
        for (unsigned off = 0; off < w->len; off++) {
            uint8_t now = location_peek(dbg, cpu, w->at, off);
            if (now == w->snapshot[off]) continue;
            char where[32];
            location_text(dbg, w->at, off, where, sizeof where);
            fprintf(stderr, "[watch] %s: %02X -> %02X, written by the instruction at %04X\n",
                    where, w->snapshot[off], now, dbg->last_pc);
            w->snapshot[off] = now;
            dbg->stop_next = true;
            dbg->step_left = 0;
        }
    }
}
