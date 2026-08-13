#ifndef ABC80_CASSETTE_H
#define ABC80_CASSETTE_H

#include <stdint.h>

// BASIC's own program-storage pointers, fixed RAM addresses grounded
// against MAME's abc80.h (`BOFA = 0xfe1c, EOFA = 0xfe1e, HEAD = 0xfe20`):
// "Beginning Of File Area" / "End Of File Area" - the [BOFA, EOFA) byte
// range in RAM holds the current program's own tokenized representation,
// exactly as verified empirically against this project's own real ROM run
// (see abc80/docs/ABC80_ROADMAP.md's Milestone 4 section): typing a
// numbered line moves EOFA forward by the stored line's length, and the
// stored bytes decode as genuine BASIC tokens (a length byte matching the
// line's own size, a little-endian line number, keyword/operator tokens,
// and a repeated numeric-literal encoding for each numeric constant).
#define ABC80_BOFA_ADDR 0xFE1C
#define ABC80_EOFA_ADDR 0xFE1E
#define ABC80_HEAD_ADDR 0xFE20

// Writes ram[BOFA, EOFA) to `path`, prefixed with one reserved header
// byte (see cassette.c's own top comment for why that byte's real-
// hardware meaning isn't confirmed here). Returns 1 on success, 0 on any
// error (reported to stderr).
int abc80_cassette_quicksave(const uint8_t *ram, const char *path);

// Reads `path` (as written by abc80_cassette_quicksave(), or any real
// ABC80 quickload-compatible file - see cassette.c) and injects its
// bytes (skipping the one header byte) into `ram` starting at the
// *current* BOFA value, then updates EOFA/HEAD to reflect the newly
// loaded program - the exact mechanism MAME's own quickload_cb uses.
// Returns 1 on success, 0 on any error (reported to stderr).
int abc80_cassette_quickload(uint8_t *ram, const char *path);

#endif
