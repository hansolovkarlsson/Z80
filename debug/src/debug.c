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
// RAM, the ABC806's high-resolution plane).

#include "debug.h"

#include <ctype.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "../../disasm/src/decode.h"

#define MAX_WATCHES 16

typedef struct {
    uint16_t addr;
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

bool z80dbg_parse_option(Z80Debugger **dbg, int argc, char **argv, int *i) {
    const char *arg = argv[*i];
    if (strcmp(arg, "--debug") == 0) {
        if (!*dbg) *dbg = debugger_new();
        (*dbg)->stop_next = true;
        return true;
    }
    if (strcmp(arg, "--break") == 0 || strcmp(arg, "--debug-script") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "%s needs a value\n", arg);
            exit(EXIT_FAILURE);
        }
        const char *value = argv[++*i];
        if (!*dbg) *dbg = debugger_new();
        if (arg[2] == 'b') {
            unsigned long addr;
            if (!parse_hex(value, 0xFFFF, &addr)) {
                fprintf(stderr, "--break: '%s' is not an address (hex, 0000-FFFF)\n", value);
                exit(EXIT_FAILURE);
            }
            (*dbg)->breakpoints[addr >> 3] |= (uint8_t)(1u << (addr & 7));
        } else {
            (*dbg)->script_path = value;
        }
        return true;
    }
    return false;
}

void z80dbg_print_usage(FILE *out) {
    fprintf(out, "  --debug          stop in the debugger before the first instruction\n");
    fprintf(out, "  --break ADDR     stop in the debugger when PC reaches ADDR (hex);\n");
    fprintf(out, "                   repeatable\n");
    fprintf(out, "  --debug-script F read debugger commands from F instead of the\n");
    fprintf(out, "                   terminal; at its end the run continues undisturbed.\n");
    fprintf(out, "                   See docs/DEBUGGER.md for the commands\n");
}

bool z80dbg_start(Z80Debugger *dbg) {
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

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    return true;
}

// ------------------------------------------------------------------ output

static void print_insn(FILE *out, const Z80 *cpu, uint16_t pc) {
    DecodedInsn d = decode_instruction(cpu->memory, pc);
    char bytes[16] = "";
    for (int k = 0; k < d.length && k < 4; k++) {
        char b[4];
        snprintf(b, sizeof b, k ? " %02X" : "%02X", cpu->memory[(uint16_t)(pc + k)]);
        strcat(bytes, b);
    }
    fprintf(out, "%04X  %-12s %s\n", pc, bytes, d.text);
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

static void dump_memory(FILE *out, const Z80 *cpu, uint16_t addr, long len) {
    for (long off = 0; off < len; off += 16) {
        uint16_t line = (uint16_t)(addr + off);
        fprintf(out, "%04X ", line);
        char ascii[17];
        int n = (int)((len - off) < 16 ? (len - off) : 16);
        for (int k = 0; k < 16; k++) {
            if (k < n) {
                uint8_t b = cpu->memory[(uint16_t)(line + k)];
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
        "  u [addr] [n]     disassemble n instructions (default: PC, 8)\n"
        "  q                end the run\n"
        "Addresses and values are hex; counts are decimal. An empty line\n"
        "repeats s or n.\n");
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

static void add_watch(Z80Debugger *dbg, const Z80 *cpu, uint16_t addr, long len, FILE *out) {
    if (dbg->watch_count == MAX_WATCHES) {
        fprintf(out, "at most %d watches\n", MAX_WATCHES);
        return;
    }
    if (len > 0xFFFF) len = 0xFFFF;
    Watch *w = &dbg->watches[dbg->watch_count];
    w->snapshot = malloc((size_t)len);
    if (!w->snapshot) return;
    w->addr = addr;
    w->len = (uint16_t)len;
    for (long k = 0; k < len; k++) w->snapshot[k] = cpu->memory[(uint16_t)(addr + k)];
    dbg->watch_count++;
    fprintf(out, "watching %04X, %ld byte%s\n", addr, len, len == 1 ? "" : "s");
}

// Reads and runs commands until one resumes execution. Returns
// Z80DBG_QUIT if the user ended the run.
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

        line[strcspn(line, "\r\n")] = '\0';
        char *start = line;
        while (isspace((unsigned char)*start)) start++;
        if (*start == '\0') {
            if (dbg->last_command[0] == '\0') continue;
            strcpy(line, dbg->last_command);
            start = line;
        }

        char *argv[8];
        int argc = 0;
        char copy[256];
        strcpy(copy, start);
        for (char *tok = strtok(copy, " \t"); tok && argc < 8; tok = strtok(NULL, " \t"))
            argv[argc++] = tok;
        const char *cmd = argv[0];
        dbg->last_command[0] = '\0';

        if (!strcmp(cmd, "s") || !strcmp(cmd, "step")) {
            long n = 1;
            if (argc > 1 && !parse_count(argv[1], &n)) {
                fprintf(out, "s: '%s' is not a count\n", argv[1]);
                continue;
            }
            dbg->step_left = n;
            strcpy(dbg->last_command, "s");
            break;
        } else if (!strcmp(cmd, "n") || !strcmp(cmd, "next")) {
            strcpy(dbg->last_command, "n");
            if (is_call_or_rst(cpu)) {
                dbg->next_sp = cpu->sp;
            } else {
                dbg->step_left = 1;
            }
            break;
        } else if (!strcmp(cmd, "c") || !strcmp(cmd, "cont")) {
            break;
        } else if (!strcmp(cmd, "b") || !strcmp(cmd, "break")) {
            if (argc == 1) {
                int listed = 0;
                for (unsigned a = 0; a < 0x10000; a++) {
                    if (dbg->breakpoints[a >> 3] & (1u << (a & 7))) {
                        fprintf(out, "breakpoint %04X\n", a);
                        listed++;
                    }
                }
                if (!listed) fprintf(out, "no breakpoints\n");
            }
            for (int k = 1; k < argc; k++) {
                unsigned long a;
                if (!parse_hex(argv[k], 0xFFFF, &a)) {
                    fprintf(out, "b: '%s' is not an address\n", argv[k]);
                    continue;
                }
                dbg->breakpoints[a >> 3] |= (uint8_t)(1u << (a & 7));
                fprintf(out, "breakpoint %04lX\n", a);
            }
        } else if (!strcmp(cmd, "d") || !strcmp(cmd, "delete")) {
            if (argc > 1 && !strcmp(argv[1], "all")) {
                memset(dbg->breakpoints, 0, sizeof dbg->breakpoints);
                fprintf(out, "all breakpoints deleted\n");
            } else {
                unsigned long a;
                if (argc < 2 || !parse_hex(argv[1], 0xFFFF, &a)) {
                    fprintf(out, "d: give an address, or 'all'\n");
                    continue;
                }
                dbg->breakpoints[a >> 3] &= (uint8_t)~(1u << (a & 7));
                fprintf(out, "breakpoint %04lX deleted\n", a);
            }
        } else if (!strcmp(cmd, "w") || !strcmp(cmd, "watch")) {
            if (argc == 1) {
                if (!dbg->watch_count) fprintf(out, "no watches\n");
                for (int k = 0; k < dbg->watch_count; k++)
                    fprintf(out, "watching %04X, %u byte%s\n", dbg->watches[k].addr,
                            dbg->watches[k].len, dbg->watches[k].len == 1 ? "" : "s");
                continue;
            }
            unsigned long a;
            long len = 1;
            if (!parse_hex(argv[1], 0xFFFF, &a) || (argc > 2 && !parse_count(argv[2], &len))) {
                fprintf(out, "w: usage is w addr [len]\n");
                continue;
            }
            add_watch(dbg, cpu, (uint16_t)a, len, out);
        } else if (!strcmp(cmd, "r") || !strcmp(cmd, "regs")) {
            if (argc == 1) {
                print_regs(out, cpu, true);
                continue;
            }
            char *eq = strchr(argv[1], '=');
            unsigned long v;
            if (!eq || (*eq = '\0', !parse_hex(eq + 1, 0xFFFF, &v)) || !set_register(cpu, argv[1], v)) {
                fprintf(out, "r: usage is r reg=value, e.g. r hl=1234 or r a=0f\n");
                continue;
            }
            print_regs(out, cpu, false);
        } else if (!strcmp(cmd, "m") || !strcmp(cmd, "mem")) {
            unsigned long a;
            long len = 64;
            if (argc < 2 || !parse_hex(argv[1], 0xFFFF, &a) || (argc > 2 && !parse_count(argv[2], &len))) {
                fprintf(out, "m: usage is m addr [len]\n");
                continue;
            }
            dump_memory(out, cpu, (uint16_t)a, len);
        } else if (!strcmp(cmd, "u") || !strcmp(cmd, "dis")) {
            unsigned long a = cpu->pc;
            long n = 8;
            if ((argc > 1 && !parse_hex(argv[1], 0xFFFF, &a)) || (argc > 2 && !parse_count(argv[2], &n))) {
                fprintf(out, "u: usage is u [addr] [n]\n");
                continue;
            }
            uint16_t pc = (uint16_t)a;
            for (long k = 0; k < n; k++) {
                print_insn(out, cpu, pc);
                pc = (uint16_t)(pc + decode_instruction(cpu->memory, pc).length);
            }
        } else if (!strcmp(cmd, "q") || !strcmp(cmd, "quit")) {
            result = Z80DBG_QUIT;
            break;
        } else if (!strcmp(cmd, "h") || !strcmp(cmd, "help") || !strcmp(cmd, "?")) {
            print_help(out);
        } else {
            fprintf(out, "unknown command '%s'; h for help\n", cmd);
        }
    }

    leave_prompt_terminal(dbg);
    interrupted = 0;   // a Ctrl-C typed at the prompt is not a request to stop again
    return result;
}

// ------------------------------------------------------------------- hooks

int z80dbg_before_step(Z80Debugger *dbg, Z80 *cpu) {
    if (dbg->detached) return Z80DBG_RUN;
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
        print_insn(stderr, cpu, pc);
        if (prompt(dbg, cpu) == Z80DBG_QUIT) return Z80DBG_QUIT;
        if (dbg->detached) return Z80DBG_RUN;
    }

    dbg->last_pc = cpu->pc;
    if (dbg->step_left > 0) print_insn(stderr, cpu, cpu->pc);
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
            uint16_t a = (uint16_t)(w->addr + off);
            uint8_t now = cpu->memory[a];
            if (now == w->snapshot[off]) continue;
            fprintf(stderr, "[watch] %04X: %02X -> %02X, written by the instruction at %04X\n",
                    a, w->snapshot[off], now, dbg->last_pc);
            w->snapshot[off] = now;
            dbg->stop_next = true;
            dbg->step_left = 0;
        }
    }
}
