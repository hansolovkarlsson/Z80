// abc802/emu/src/cassette.c - see cassette.h for what this models and why
// it stops at the SIO's byte boundary.

#include <stdio.h>
#include <stdlib.h>

#include "cassette.h"

static FILE *tape;
static long written;
static long read_bytes;

bool abc802_cassette_attach(const char *path) {
    // "r+b" then "w+b": open an existing tape without truncating it, and
    // create one only when there is nothing there. Truncating on attach
    // would make a LOAD of a tape saved by an earlier run impossible,
    // which is the one thing this feature exists to allow.
    tape = fopen(path, "r+b");
    if (!tape) tape = fopen(path, "w+b");
    if (!tape) {
        fprintf(stderr, "Failed to open cassette image '%s': ", path);
        perror(NULL);
        return false;
    }
    written = 0;
    read_bytes = 0;
    return true;
}

bool abc802_cassette_present(void) { return tape != NULL; }

void abc802_cassette_write(uint8_t value) {
    if (!tape) return;
    if (fputc(value, tape) != EOF) written++;
}

int abc802_cassette_read(void) {
    if (!tape) return -1;
    int c = fgetc(tape);
    if (c == EOF) return -1;
    read_bytes++;
    return c;
}

void abc802_cassette_close(void) {
    if (!tape) return;
    fclose(tape);
    tape = NULL;
}

long abc802_cassette_bytes_written(void) { return written; }
long abc802_cassette_bytes_read(void) { return read_bytes; }
