// abc802/emu/src/cassette.h - a cassette recorder on SIO channel B.
//
// Modeled at the SIO's byte boundary, not as an audio signal, and that is
// a deliberate scoping decision rather than a shortcut. Tracing the ROM
// through a real `SAVE "CAS:T"` shows it hands the SIO whole *bytes*: it
// programs channel B for synchronous operation (WR6/WR7 sync characters),
// then writes 590 bytes to the channel's data port - 32 zeros of leader,
// a 0x16 SYN, and then the framed record carrying the filename and the
// program. The FSK modulation everybody means by "cassette interface"
// happens in hardware *after* the SIO, between it and the tape head.
//
// So the SIO's data port is the real protocol boundary, exactly as the
// four-byte command header is for abcbus/disk.c: this file stores the
// byte stream the ROM produces and replays it, and the ROM's own framing,
// checksums and file format go through unmodified. What it deliberately
// does not model is the analogue layer - tape speed, dropouts, or a real
// .wav - and no signal is modulated. Loading a recording made by real
// hardware would need that; round-tripping this machine's own saves does
// not.
//
// One file is both sides. A SAVE appends the bytes the ROM transmits; a
// LOAD reads them back in order. That is what makes a two-process
// SAVE-then-LOAD round trip a genuine test rather than a memory echo.

#ifndef ABC802_CASSETTE_H
#define ABC802_CASSETTE_H

#include <stdbool.h>
#include <stdint.h>

// Attach `path` as the tape. Created if absent, so a SAVE can record onto
// a fresh one; opened for both directions so one file serves SAVE and
// LOAD. Returns false, having reported why, if it cannot be opened.
bool abc802_cassette_attach(const char *path);

// True once a tape is attached. With none, channel B behaves exactly as
// it did before this existed - transmitted bytes are discarded and
// nothing is ever received.
bool abc802_cassette_present(void);

// One byte the ROM transmitted on channel B.
void abc802_cassette_write(uint8_t value);

// The next byte to receive, or -1 when the tape has run out - which is
// what a real recorder at the end of its tape gives the SIO: silence, and
// so no character.
int abc802_cassette_read(void);

// Flush and close. Called at the end of a run so a SAVE is on disk even
// when the emulator is stopped by its T-state cap mid-program.
void abc802_cassette_close(void);

// How many bytes have been written and read this run, for the end-of-run
// summary - the only way to see that a cassette operation happened at
// all, since the ROM reports success the same way either way.
long abc802_cassette_bytes_written(void);
long abc802_cassette_bytes_read(void);

#endif // ABC802_CASSETTE_H
