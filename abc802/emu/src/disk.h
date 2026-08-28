// abc802/emu/src/disk.h - a synthetic ABC-bus disk controller.
//
// This is a *device model*, not a PC-address trap. The ABC80 target's
// floppy support (abc80/emu/src/disk.c) intercepts two addresses inside
// that machine's DOS ROM and services whole block operations in C; that
// works there because the ROM offers a single sector-level routine to
// stand in front of. The ABC802's DOS ROM does not - its bus driver at
// 0x608F-0x616E is generic, and callers issue *sequences* of bus commands
// per logical operation, so there is no equivalent place to cut. See
// abc802/docs/ABC802_FLOPPY_SCOPING.md for the full comparison.
//
// So this implements the protocol instead: the six ABC-bus ports and the
// command state machine behind them, serving 256-byte sectors out of a
// disk image file. Anything that talks to the bus correctly works,
// including code paths nobody thought to trap.
//
// What it deliberately does *not* do is emulate the real controller card,
// which is a complete second computer - its own Z80 at 4 MHz, a Z80 DMA
// controller, an FD1793 FDC, and firmware in five ROM variants. MAME
// models that faithfully and consequently contains no protocol logic at
// all: its card-side handlers are a byte latch, a busy flag and an NMI
// pulse, with every byte of meaning living in firmware it executes. A
// synthetic controller sits behind the same bus interface a real one
// would, so this does not foreclose that option later.

#ifndef ABC802_DISK_H
#define ABC802_DISK_H

#include <stdbool.h>
#include <stdint.h>

// ABC-bus device-select codes, as the ROM's own select table at
// 0x61DA-0x61FB carries them and as abc80sim independently names them.
#define ABC802_SEL_HD 0x24  // hard disk
#define ABC802_SEL_MF 0x2C  // ABC832/834 - 640K floppy
#define ABC802_SEL_MO 0x2D  // ABC830 - 160K floppy
#define ABC802_SEL_SF 0x2E  // 8-inch floppy

// Attach `path` as the image for drive `unit` (0-7) of the ABC830-class
// floppy controller, enabling the card. Returns false if the file cannot
// be opened. With no image attached the card is absent entirely and every
// bus read floats high, which is the ROM's own "no card fitted" signal.
bool abc802_disk_attach(int unit, const char *path);

// True once at least one image is attached, i.e. the card is present.
bool abc802_disk_present(void);

// Release any attached images.
void abc802_disk_close(void);

// --- The bus interface, called from ports.c -------------------------
//
// `select` is the value written to CS, already masked to 6 bits by the
// caller the way the ROM itself masks it (`AND 3Fh` at 0x6172).
void abc802_disk_select(uint8_t select);

// Port offsets within the ABC-bus range: 0 = INP/OUT, 1 = STAT/CS,
// 2..5 = C1..C4, 7 = RST.
void abc802_disk_out(int port, uint8_t value);
uint8_t abc802_disk_in(int port);

// Reset the command state machine. Called on C1/C3 and at machine reset.
void abc802_disk_reset(void);

#endif // ABC802_DISK_H
