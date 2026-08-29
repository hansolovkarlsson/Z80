// abcbus/disk.h - a synthetic ABC-bus floppy controller, shared by every
// machine target in this repo that carries the bus.
//
// It lives at the repo root, beside z80core/, for the same reason that one
// does: the ABC bus is a bus, not a machine. The ABC80 (1978) and the
// ABC800 family (1981-) both expose it, both drive it with the same
// four-byte command header and the same status bits, and both boot real
// media through this one implementation. Putting it under either target's
// directory would make that target a de facto shared library for the other
// - the exact arrangement moving z80core/ out of cpm/ was meant to end.
//
// This is a *device model*, not a PC-address trap. The ABC80 target used
// to intercept two addresses inside its own DOS ROM (0x6068/0x60A1) and
// service whole sector transfers in C, which worked because that ROM
// happens to offer a single sector-level routine to stand in front of. The
// ABC802's DOS ROM does not - its bus driver is generic, and callers issue
// *sequences* of bus commands per logical operation, so there is no
// equivalent place to cut. That is what forced a real protocol
// implementation here (see abc802/docs/ABC802_FLOPPY_SCOPING.md for the
// full comparison), and once it existed the ABC80's trap had nothing left
// to justify it.
//
// So this implements the protocol instead: the six ABC-bus ports and the
// command state machine behind them, serving 256-byte sectors out of a
// disk image file. Anything that talks to the bus correctly works,
// including code paths nobody thought to trap. Three ROMs now do, none of
// which this file was written against one-by-one: the ABC802's DOS, the
// ABC80's ABC-DOS, and the ABC80's UFD-DOS - that last one a different DOS
// entirely, with its own bus driver and its own device-select scheme, which
// was never trapped, never analysed routine-by-routine, and works anyway.
//
// What it deliberately does *not* do is emulate the real controller card,
// which is a complete second computer - its own Z80 at 4 MHz, a Z80 DMA
// controller, an FD1793 FDC, and firmware in five ROM variants. MAME
// models that faithfully and consequently contains no protocol logic at
// all: its card-side handlers are a byte latch, a busy flag and an NMI
// pulse, with every byte of meaning living in firmware it executes. A
// synthetic controller sits behind the same bus interface a real one
// would, so this does not foreclose that option later.
//
// Each machine keeps its own port decode (the ABC80 masks port numbers
// with 0x17, the ABC802 uses its own range decode) and its own DOS ROM
// loading; only the card is here.

#ifndef ABCBUS_DISK_H
#define ABCBUS_DISK_H

#include <stdbool.h>
#include <stdint.h>

// ABC-bus device-select codes, as the ROM's own select table at
// 0x61DA-0x61FB carries them and as abc80sim independently names them.
#define ABCBUS_SEL_HD 0x24  // hard disk
#define ABCBUS_SEL_MF 0x2C  // ABC832/834 - 640K floppy
#define ABCBUS_SEL_MO 0x2D  // ABC830 - 160K floppy
#define ABCBUS_SEL_SF 0x2E  // 8-inch floppy

// Attach `path` as the image for drive `unit` (0-7), enabling the card.
// Which controller is fitted - ABC830 (160KB) or ABC832/834 (640KB) - is
// decided by the image's size, so a second image must be of the same
// type. Returns false if the file cannot be opened or is not a
// recognized size. With no image attached the card is absent entirely and every
// bus read floats high, which is the ROM's own "no card fitted" signal.
bool abcbus_disk_attach(int unit, const char *path);

// Attach one --disk argument. An explicit unit may be given as a "N:"
// prefix ("1:games.img"); otherwise images are assigned to drives in the
// order they appear on the command line, so two plain --disk arguments
// become drives 0 and 1. Returns false on any failure, having already
// reported why.
bool abcbus_disk_attach_arg(const char *arg);

// How many drives currently have an image, i.e. the next unit a bare
// --disk argument would take.
int abcbus_disk_attached_count(void);

// True once at least one image is attached, i.e. the card is present.
bool abcbus_disk_present(void);

// Short name of the controller the attached image selected ("mo" for a
// 160KB ABC830, "mf" for a 640KB ABC832/834), for startup reporting.
const char *abcbus_disk_type_name(void);

// Override the sector interleave the fitted drive would otherwise use.
// `factor` is the multiplier applied within each 16-sector track; 0
// disables interleaving entirely, i.e. the image is in plain logical
// sector order.
//
// This exists because two dump conventions are genuinely in circulation
// for the same media. The abc80.net `.img` archive stores ABC830 sectors
// in *physical* order, which is why DRIVE_MO's factor of 7 was needed to
// boot it at all (ABC80 Milestone 6, re-confirmed on the ABC802). Other
// archives ship `.dsk` files in logical order, and those read as Error 37
// - the file is found and its data is garbage - until the factor is
// turned off. Neither dump is wrong; they simply record different things,
// and nothing inside a disk says which. Note that a directory hex dump
// cannot tell them apart: track-boundary sectors map to themselves under
// any factor, so only reading a real file settles it.
//
// May be called before or after attaching; it takes effect per transfer.
void abcbus_disk_set_interleave(unsigned factor);

// The interleave factor actually in force - the override if one was set,
// otherwise the fitted drive type's own. For startup reporting.
unsigned abcbus_disk_interleave(void);

// Release any attached images.
void abcbus_disk_close(void);

// --- The bus interface, called from ports.c -------------------------
//
// `select` is the value written to CS, already masked to 6 bits by the
// caller the way the ROM itself masks it (`AND 3Fh` at 0x6172).
void abcbus_disk_select(uint8_t select);

// Port offsets within the ABC-bus range: 0 = INP/OUT, 1 = STAT/CS,
// 2..5 = C1..C4, 7 = RST.
void abcbus_disk_out(int port, uint8_t value);
uint8_t abcbus_disk_in(int port);

// Reset the command state machine. Called on C1/C3 and at machine reset.
void abcbus_disk_reset(void);

#endif // ABCBUS_DISK_H
