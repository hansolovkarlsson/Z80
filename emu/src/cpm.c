#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include "z80.h"

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
    raw.c_cc[VMIN] = 1;              // block for at least 1 byte
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// Non-blocking: is a byte waiting on stdin right now?
static int console_char_ready(void) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    struct timeval timeout = {0, 0};
    return select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout) > 0;
}

// Blocking single-byte read. EOF (e.g. piped/redirected stdin exhausted)
// maps to ^Z (26), the traditional CP/M "no more input" sentinel, so a
// program driven from a non-interactive stdin doesn't spin forever.
static int console_read_char(void) {
    uint8_t c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) return 26;
    return c;
}

void check_cpm_bdos(Z80 *cpu, uint8_t *ram) {
    if (cpu->pc == 0x0005) { // Intercept call to BDOS entry
        if (cpu->c == 1) {
            // Function 1: Console Input (wait for a char, echo it, return in A/L)
            int c = console_read_char();
            putchar(c);
            fflush(stdout);
            cpu->a = (uint8_t)c;
            cpu->l = (uint8_t)c;
        }
        else if (cpu->c == 2) {
            // Function 2: Console Output (Char in E)
            putchar(cpu->e);
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
                putchar(cpu->e);
                fflush(stdout);
            }
        }
        else if (cpu->c == 9) {
            // Function 9: Print String (Address in DE, terminated by '$')
            uint16_t addr = cpu->de;
            while (ram[addr] != '$') {
                putchar(ram[addr++]);
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

        // Simulate RET: Pop return address off the stack into PC
        uint8_t low = ram[cpu->sp++];
        uint8_t high = ram[cpu->sp++];
        cpu->pc = (high << 8) | low;
    }
}
