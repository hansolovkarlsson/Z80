// abc802/emu/src/ports.c - I/O port decoding plus the Z80 CTC, Z80 DART and
// MC6845 CRTC behind it.
//
// Port map (from MAME's src/mame/luxor/abc80x.cpp abc800_io/abc802_io,
// BSD-3-Clause, Curt Coder - reimplemented, not copied):
//
//   0x00  ABC-bus INP (r) / OUT (w)      mirror 0xff18
//   0x01  ABC-bus STAT (r) / CS (w)      mirror 0xff18
//   0x02..0x05  ABC-bus C1..C4 (w)       mirror 0xff18
//   0x05  "pling" speaker strobe (r)     mirror 0xff18
//   0x07  ABC-bus RST (r)                mirror 0xff18
//   0x20..0x23  Z80 DART                 mirror 0xff0c
//   0x31  MC6845 register read           mirror 0xff06
//   0x38  MC6845 address write           mirror 0xff06
//   0x39  MC6845 register write          mirror 0xff06
//   0x40..0x43  Z80 SIO/2                mirror 0xff1c
//   0x60..0x63  Z80 CTC                  mirror 0xff1c
//
// The low don't-care bits in those mirrors are NOT a detail that can be
// skipped: the real boot ROM writes the CTC's interrupt vector to port
// 0x64, not 0x60, and programs channel 3 at 0x63. Decoding only the
// literal 0x60-0x63 range silently dropped the vector write and left every
// CTC interrupt vectoring through 0x00. Found by tracing real ROM I/O
// (ABC802_TRACE_IO=1), not by reading the mirror masks and reasoning ahead
// - which is exactly why the masks are honored here rather than assumed
// unused. Only the high byte of the 16-bit port address is ignored, since
// z80core's z80_io_in/out pass a single 8-bit port number.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "ports.h"
#include "memory.h"

static int trace_io = 0;   // ABC802_TRACE_IO=1

// ---------------------------------------------------------------- MC6845

// 18 registers, selected by the address latch written to port 0x38 and then
// read/written through 0x31/0x39. Only the register file is modeled: this
// emulator renders straight from character RAM on demand rather than
// reproducing the CRTC's real scanline-by-scanline fetch, so the registers
// matter for *what* to draw (start address, cursor, character counts), not
// for pixel timing.
static uint8_t crtc_regs[18];
static uint8_t crtc_addr;

uint8_t abc802_crtc_reg(int index) {
    if (index < 0 || index >= 18) return 0;
    return crtc_regs[index];
}

bool abc802_crtc_programmed(void) {
    // R1 = horizontal displayed, R6 = vertical displayed. Both nonzero
    // means the ROM has set up a real display rather than left the chip
    // in its power-on state.
    return crtc_regs[1] != 0 && crtc_regs[6] != 0;
}

// ------------------------------------------------------------- Z80 CTC

// Four counter/timer channels. Each has a control byte, a time constant,
// and a live down-counter driven by the CPU clock through the channel's
// own prescaler. Channel 0's interrupt is what the ABC800-series ROMs use
// as their periodic system tick.
typedef struct {
    uint8_t control;
    uint8_t time_constant;
    int     counter;        // remaining prescaled ticks
    bool    expects_tc;     // next byte written to this channel is a time constant
    bool    int_pending;
} CtcChannel;

static CtcChannel ctc[4];
static uint8_t ctc_vector;   // written to channel 0, low 3 bits supplied by hardware

#define CTC_INT_ENABLE 0x80
#define CTC_MODE_COUNTER 0x40
#define CTC_PRESCALER_256 0x20
#define CTC_TC_FOLLOWS 0x04
#define CTC_RESET 0x02
#define CTC_CONTROL 0x01

static void ctc_reload(CtcChannel *ch) {
    int tc = ch->time_constant ? ch->time_constant : 256;
    int prescale = (ch->control & CTC_PRESCALER_256) ? 256 : 16;
    ch->counter = tc * prescale;
}

static void ctc_write(int channel, uint8_t value) {
    CtcChannel *ch = &ctc[channel];

    if (ch->expects_tc) {
        ch->time_constant = value;
        ch->expects_tc = false;
        ctc_reload(ch);
        return;
    }

    if (!(value & CTC_CONTROL)) {
        // Bit 0 clear marks an interrupt vector, and only channel 0
        // carries it: the CTC supplies bits 1-2 itself to identify which
        // channel is interrupting.
        if (channel == 0) ctc_vector = value & 0xF8;
        return;
    }

    ch->control = value;
    if (value & CTC_TC_FOLLOWS) ch->expects_tc = true;
    if (value & CTC_RESET) {
        ch->counter = 0;
        ch->int_pending = false;
    }
}

static uint8_t ctc_read(int channel) {
    // Reading a channel returns its live down-counter value, scaled back
    // out of the prescaler.
    CtcChannel *ch = &ctc[channel];
    int prescale = (ch->control & CTC_PRESCALER_256) ? 256 : 16;
    int remaining = ch->counter / prescale;
    return (uint8_t)(remaining & 0xFF);
}

// ------------------------------------------------------------- Z80 DART

// Two channels. A is the RS-232 printer port; B is the keyboard, which on
// a real ABC802 is a *serial* device (an ABC55/ABC77 keyboard with its own
// microcontroller) rather than a scanned matrix. Only what the boot path
// needs is modeled: the write-register file, the read-register status
// bytes, and the two control outputs the ABC802 repurposes as machine
// control lines.
typedef struct {
    uint8_t wr[8];
    uint8_t wr_pointer;
    uint8_t rx_data;
    bool    rx_ready;
    bool    rx_int_pending;
} DartChannel;

static DartChannel dart[2];
static uint8_t dart_vector;

// Configuration DIP switches, read by the ROM through DART modem-status
// inputs (see dart_read_control). Defaults match MAME's own ABC802
// defaults: 40 characters per line (S3 off) and 50 Hz.
static bool config_80_columns = false;
static bool config_50hz = true;

// Z80 SIO/DART "status affects vector" encoding: the base vector in WR2 is
// modified with a 3-bit condition code in bits 1-3 identifying which of the
// eight interrupt sources fired. Channel B receive-character-available is
// code 2, so the delivered vector is base | (2 << 1). The ABC802's boot ROM
// copies ten bytes to 0xFFB0 - five word-sized vector-table entries
// covering exactly this range - which is what confirms the encoding here
// rather than leaving it as datasheet inference.
#define DART_VEC_CHB_RX_AVAIL 0x04

bool abc802_keyboard_ready(void) {
    // WR1 bits 3-4 select the receive-interrupt mode; any nonzero value
    // means "interrupt on received characters" is enabled.
    return (dart[1].wr[1] & 0x18) != 0;
}

void abc802_set_config(bool eighty_columns, bool fifty_hz) {
    config_80_columns = eighty_columns;
    config_50hz = fifty_hz;
}

// True while the DART still holds a received byte the ROM has not read.
bool abc802_keyboard_busy(void) {
    return dart[1].rx_ready;
}

void abc802_keyboard_send(uint8_t code) {
    dart[1].rx_data = code;
    dart[1].rx_ready = true;
    dart[1].rx_int_pending = true;
}

// DART channel B's DTR output is not a serial signal on this machine: it
// drives LRS, the low-32K ROM/RAM select. RTS-B likewise drives the
// 80/40-column video mux. Both are ABC802-specific rewirings documented in
// MAME's abc802 machine_config.
static bool col80 = true;

bool abc802_80_column(void) { return col80; }

static void dart_write_control(int channel, uint8_t value) {
    DartChannel *ch = &dart[channel];
    uint8_t reg = ch->wr_pointer;
    ch->wr_pointer = 0;

    if (reg == 0) {
        ch->wr_pointer = value & 0x07;
        return;
    }

    ch->wr[reg] = value;

    if (reg == 2 && channel == 1) {
        dart_vector = value;
    }
    if (reg == 5 && channel == 1) {
        // WR5 bit 7 = DTR, bit 1 = RTS. Neither is a serial signal on this
        // machine: DTR-B drives LRS (the low-32K ROM/RAM select) and RTS-B
        // drives the 80/40-column video mux.
        //
        // Both invert once on the way out, because these are active-low
        // pins: setting the WR5 bit drives the pin *low*, and a low pin
        // means "RAM" and "40 columns" respectively. So a set bit selects
        // RAM, and a set bit selects 40 columns. Confirmed against the
        // real ROM rather than derived from the datasheet alone: it writes
        // WR5=0x68 (both bits clear) during boot and then programs the
        // CRTC for 80 columns, which only agrees with the polarity if a
        // clear RTS bit means 80.
        bool dtr_bit = (value & 0x80) != 0;
        bool rts_bit = (value & 0x02) != 0;
        abc802_set_lrs(dtr_bit);
        col80 = !rts_bit;
        if (trace_io) {
            fprintf(stderr, "[dart] WR5 B=%02X -> LRS(ram)=%d col80=%d\n",
                    value, (int)dtr_bit, (int)col80);
        }
    }
}

static uint8_t dart_read_control(int channel) {
    DartChannel *ch = &dart[channel];
    uint8_t reg = ch->wr_pointer;
    ch->wr_pointer = 0;

    if (reg == 0) {
        // RR0: bit 0 = Rx character available, bit 2 = Tx buffer empty,
        // bit 3 = DCD, bit 4 = RI, bit 5 = CTS. Tx is always reported
        // empty (this emulator never holds a transmit byte), which keeps
        // ROM output loops moving.
        //
        // Two of these are not serial handshake lines at all on this
        // machine: the ABC802's configuration DIP switches are wired to
        // DART modem-status inputs, so the boot ROM reads its own setup
        // through them. Channel A's RI carries S3 (40 vs 80 characters per
        // line) and channel B's CTS carries the frame frequency. Confirmed
        // empirically: with RI clear the ROM lays its text out in every
        // other character cell, which is exactly the 40-column format.
        uint8_t status = 0x04;
        if (ch->rx_ready) status |= 0x01;
        if (channel == 0 && config_80_columns) status |= 0x10;
        if (channel == 1 && config_50hz)       status |= 0x20;
        return status;
    }
    if (reg == 1) {
        // RR1: bit 0 = All Sent. Same reasoning as Tx buffer empty above.
        return 0x01;
    }
    return 0;
}

// ---------------------------------------------------------------- ports

// Which chip a port number selects, after applying the don't-care bits
// documented above. Returning the device plus its own register index keeps
// the mirror arithmetic in exactly one place.
typedef enum { DEV_NONE, DEV_ABCBUS, DEV_DART, DEV_CRTC, DEV_SIO, DEV_CTC } Device;

static Device decode_port(uint8_t port, int *index) {
    if ((port & 0xE0) == 0x00) { *index = port & 0x07; return DEV_ABCBUS; }
    if ((port & 0xF0) == 0x20) { *index = port & 0x03; return DEV_DART; }
    if ((port & 0xF9) == 0x31 || (port & 0xF9) == 0x38 || (port & 0xF9) == 0x39) {
        *index = port & 0xF9;
        return DEV_CRTC;
    }
    if ((port & 0xE0) == 0x40) { *index = port & 0x03; return DEV_SIO; }
    if ((port & 0xE0) == 0x60) { *index = port & 0x03; return DEV_CTC; }
    *index = 0;
    return DEV_NONE;
}

static uint8_t io_in(Z80 *cpu, uint8_t port, uint8_t stored) {
    (void)cpu; (void)stored;
    uint8_t value = 0xFF;
    int index = 0;

    switch (decode_port(port, &index)) {
        case DEV_ABCBUS:
            // The synthetic controller answers only when it is the
            // selected device; otherwise the bus floats high, which is
            // the ROM's own "no card fitted" signal (see disk.c). With no
            // image attached nothing is selected ever, so this is exactly
            // the pre-existing behavior.
            value = abc802_disk_in(index);
            break;
        case DEV_DART: {
            int channel = (index & 0x02) ? 1 : 0;
            if (index & 0x01) {
                value = dart_read_control(channel);
            } else {
                value = dart[channel].rx_data;
                dart[channel].rx_ready = false;
            }
            break;
        }
        case DEV_CRTC:
            value = (index == 0x31 && crtc_addr < 18) ? crtc_regs[crtc_addr] : 0;
            break;
        case DEV_SIO:
            // Nothing attached to either channel. Report "transmit buffer
            // empty, no receive data" so any ROM polling loop exits.
            value = (index & 0x01) ? 0x04 : 0x00;
            break;
        case DEV_CTC:
            value = ctc_read(index);
            break;
        case DEV_NONE:
            break;
    }

    if (trace_io) fprintf(stderr, "[in ] %02X -> %02X\n", port, value);
    return value;
}

static int io_out(Z80 *cpu, uint8_t port, uint8_t value) {
    (void)cpu;
    if (trace_io) fprintf(stderr, "[out] %02X <- %02X\n", port, value);

    int index = 0;
    switch (decode_port(port, &index)) {
        case DEV_DART: {
            int channel = (index & 0x02) ? 1 : 0;
            if (index & 0x01) dart_write_control(channel, value);
            // Data writes are transmitted bytes; nothing is attached to
            // either channel's transmit side yet.
            break;
        }
        case DEV_CRTC:
            if (index == 0x38) crtc_addr = value & 0x1F;
            else if (index == 0x39 && crtc_addr < 18) crtc_regs[crtc_addr] = value;
            break;
        case DEV_CTC:
            ctc_write(index, value);
            break;
        case DEV_ABCBUS:
            // Port 1 is CS: it selects which expansion card listens. The
            // ROM masks the select to 6 bits itself (AND 3Fh at 0x6172);
            // masking again here keeps the card honest if some other
            // caller does not.
            if (index == 1) abc802_disk_select(value & 0x3F);
            else abc802_disk_out(index, value);
            break;
        default:
            break;
    }

    return 1;  // fully decoded here; never fall through to the io_ports[] store
}

// --------------------------------------------------------------- timing

void abc802_ports_tick(Z80 *cpu, int cycles) {
    for (int i = 0; i < 4; i++) {
        CtcChannel *ch = &ctc[i];
        // Only timer mode is driven by the CPU clock; counter mode waits
        // on an external trigger this emulator doesn't model.
        if (!(ch->control & CTC_CONTROL)) continue;
        if (ch->control & CTC_MODE_COUNTER) continue;
        if (ch->counter <= 0) continue;

        ch->counter -= cycles;
        if (ch->counter <= 0) {
            ctc_reload(ch);
            if (ch->control & CTC_INT_ENABLE) ch->int_pending = true;
        }
    }

    // Deliver the highest-priority pending interrupt. The daisy chain runs
    // CTC -> SIO -> DART, so a CTC channel always wins, and within the CTC
    // the lowest channel number does.
    if (!cpu->int_pending && cpu->iff1) {
        for (int i = 0; i < 4; i++) {
            if (ctc[i].int_pending) {
                ctc[i].int_pending = false;
                uint8_t vec = (uint8_t)(ctc_vector | (i << 1));
                if (trace_io) {
                    uint16_t tbl = (uint16_t)((cpu->i << 8) | vec);
                    fprintf(stderr, "[int] CTC ch%d vector %02X -> table %04X -> handler %04X\n",
                            i, vec, tbl,
                            (uint16_t)(cpu->memory[tbl] | (cpu->memory[(uint16_t)(tbl+1)] << 8)));
                }
                z80_request_int(cpu, vec);
                return;
            }
        }

        // Then the DART, last in the daisy chain. Channel B is the
        // keyboard.
        if (dart[1].rx_int_pending) {
            dart[1].rx_int_pending = false;
            uint8_t vec = (uint8_t)(dart_vector | DART_VEC_CHB_RX_AVAIL);
            if (trace_io) {
                uint16_t tbl = (uint16_t)((cpu->i << 8) | vec);
                fprintf(stderr, "[int] DART chB rx vector %02X -> table %04X -> handler %04X\n",
                        vec, tbl,
                        (uint16_t)(cpu->memory[tbl] | (cpu->memory[(uint16_t)(tbl + 1)] << 8)));
            }
            z80_request_int(cpu, vec);
            return;
        }
    }
}

void abc802_ports_attach(Z80 *cpu) {
    trace_io = getenv("ABC802_TRACE_IO") != NULL;

    memset(ctc, 0, sizeof(ctc));
    memset(dart, 0, sizeof(dart));
    memset(crtc_regs, 0, sizeof(crtc_regs));
    crtc_addr = 0;
    ctc_vector = 0;
    dart_vector = 0;
    col80 = true;

    cpu->io_in_hook = io_in;
    cpu->io_out_hook = io_out;
}
