// abc802/emu/src/ports.c - I/O port decoding plus the Z80 CTC, Z80 DART and
// MC6845 CRTC behind it.
//
// Port map (from MAME's src/mame/luxor/abc80x.cpp abc800_io/abc806_io,
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
// (ABC806_TRACE_IO=1), not by reading the mirror masks and reasoning ahead
// - which is exactly why the masks are honored here rather than assumed
// unused. Only the high byte of the 16-bit port address is ignored, since
// z80core's z80_io_in/out pass a single 8-bit port number.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../abcbus/disk.h"
#include "ports.h"
#include "memory.h"
#include "rtc.h"

// --- The frame clock on DART channel B's RI input ---------------------
//
// The option PROM's routine at 0x7617 is a delay: it samples RR0 on
// channel B, then spins until *bit 4 changes state*
// (`IN A,(C) / XOR B / AND 10h / JR Z`). Bit 4 of a Z80 DART's RR0 is RI,
// and with nothing driving it the machine waits there forever - which is
// exactly where this target sat for two milestones, having booted
// perfectly and drawn nothing.
//
// What makes a status bit flip on its own is a periodic input, and the
// only periodic signal a 1983 machine has to hand at this point in its
// boot is the video frame. Driving RI from a 50 Hz square wave unblocks
// the routine and the ROM immediately writes its sign-on to the screen.
//
// Stated as inference, because it is: MAME drives channel A's RI and
// channel B's CTS and leaves channel B's RI alone, so this is not read out
// of another implementation. What is *observed* is that the loop waits on
// a change to this bit and that a periodic source satisfies it. If the
// real machine sources it from something else at the same rate - a
// hardware timer, a mains-derived tick - the behaviour here is
// indistinguishable, and the frequency is the part that matters.
#define ABC806_CPU_HZ    3000000
#define ABC806_FRAME_HZ  50
static long long frame_cycles = 0;

static bool frame_phase(void) {
    long long half = ABC806_CPU_HZ / (ABC806_FRAME_HZ * 2);
    return ((frame_cycles / half) & 1) != 0;
}
#include "memory.h"

static int trace_io = 0;   // ABC806_TRACE_IO=1

// ---------------------------------------------------------------- MC6845

// 18 registers, selected by the address latch written to port 0x38 and then
// read/written through 0x31/0x39. Only the register file is modeled: this
// emulator renders straight from character RAM on demand rather than
// reproducing the CRTC's real scanline-by-scanline fetch, so the registers
// matter for *what* to draw (start address, cursor, character counts), not
// for pixel timing.
static uint8_t crtc_regs[18];
static uint8_t crtc_addr;

uint8_t abc806_crtc_reg(int index) {
    if (index < 0 || index >= 18) return 0;
    return crtc_regs[index];
}

bool abc806_crtc_programmed(void) {
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

bool abc806_keyboard_ready(void) {
    // WR1 bits 3-4 select the receive-interrupt mode; any nonzero value
    // means "interrupt on received characters" is enabled.
    return (dart[1].wr[1] & 0x18) != 0;
}

void abc806_set_config(bool eighty_columns, bool fifty_hz) {
    config_80_columns = eighty_columns;
    config_50hz = fifty_hz;
}

// True while the DART still holds a received byte the ROM has not read.
bool abc806_keyboard_busy(void) {
    return dart[1].rx_ready;
}

void abc806_keyboard_send(uint8_t code) {
    dart[1].rx_data = code;
    dart[1].rx_ready = true;
    dart[1].rx_int_pending = true;
}

// DART channel B's DTR output is not a serial signal on this machine: it
// drives LRS, the low-32K ROM/RAM select. RTS-B likewise drives the
// 80/40-column video mux. Both are ABC802-specific rewirings documented in
// MAME's abc802 machine_config.
static bool col80 = true;

bool abc806_80_column(void) { return col80; }

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
        // DTR-B means something different here than on the ABC802. There
        // it is LRS, selecting ROM or RAM in the low 32K; on the ABC806 it
        // is KEYDTR, which with EME off swaps the low 32K between ROM and
        // the high-resolution video plane. Same pin, same chip, different
        // wiring - and the sort of thing that would silently half-work if
        // carried across unexamined. From MAME's own abc806 machine
        // config: out_dtrb_callback -> keydtr_w.
        //
        // Both bits invert on the way out, these being active-low pins.
        bool dtr_bit = (value & 0x80) != 0;
        bool rts_bit = (value & 0x02) != 0;
        abc806_set_keydtr(!dtr_bit);
        col80 = !rts_bit;
        if (trace_io) {
            fprintf(stderr, "[dart] WR5 B=%02X -> KEYDTR=%d col80=%d\n",
                    value, (int)!dtr_bit, (int)col80);
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
        // Channel B's RI is the frame clock - see the note at the top.
        if (channel == 1 && frame_phase()) status |= 0x10;
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
// ---------------------------------------------------------------------
// Z80 SIO/2
// ---------------------------------------------------------------------
//
// Channel A is the machine's second RS-232 port; channel B is the
// **cassette** interface (MAME wires the cassette's own input to the SIO's
// rxb and drives its output from txdb/rtsb, with the motor on dtrb).
// Neither has a device attached here, so nothing is ever received and
// transmitted bytes go nowhere - but the registers themselves are now
// real, where previously every read returned one of two constants
// regardless of what the ROM had programmed.
//
// Two of the machine's configuration DIP switches arrive as channel B
// modem-status inputs rather than as anything memory-mapped, which is the
// reason this needs to be more than a stub at all:
//
//   S1 "Clear Screen Time Out"  ->  channel B DCD  (RR0 bit 3)
//   S2 (undocumented in MAME too) -> channel B CTS (RR0 bit 5)
//
// Defaults follow MAME's own: S1 off, S2 on.
typedef struct {
    uint8_t wr[8];
    uint8_t wr_pointer;
    uint8_t rx_data;
    bool rx_ready;
    bool dcd;   // modem-status input, wired per channel above
    bool cts;
} SioChannel;

static SioChannel sio[2];
static uint8_t sio_vector;

#define SIO_RR0_RX_AVAILABLE 0x01
#define SIO_RR0_TX_EMPTY     0x04
#define SIO_RR0_DCD          0x08
#define SIO_RR0_CTS          0x20
#define SIO_RR1_ALL_SENT     0x01

static void sio_channel_reset(int channel) {
    SioChannel *ch = &sio[channel];
    ch->wr_pointer = 0;
    ch->rx_ready = false;
    ch->rx_data = 0;
    memset(ch->wr, 0, sizeof(ch->wr));
}

static uint8_t sio_read_control(int channel) {
    SioChannel *ch = &sio[channel];
    uint8_t reg = ch->wr_pointer;
    // Reading a status register clears the pointer back to RR0, exactly as
    // writing a data register clears it back to WR0.
    ch->wr_pointer = 0;

    switch (reg) {
        case 0:
            // Transmit is always reported empty: with nothing attached, a
            // byte written to the data port has by definition already
            // gone as far as it is ever going to. A ROM polling loop that
            // waits for this bit must see it or it never exits.
            return (uint8_t)((ch->rx_ready ? SIO_RR0_RX_AVAILABLE : 0) |
                             SIO_RR0_TX_EMPTY |
                             (ch->dcd ? SIO_RR0_DCD : 0) |
                             (ch->cts ? SIO_RR0_CTS : 0));
        case 1:
            // No parity, overrun or framing errors can occur with no
            // receiver attached; "all sent" is the only true bit.
            return SIO_RR1_ALL_SENT;
        case 2:
            // The datasheet makes RR2 valid on channel B only. Channel A
            // returns 0 rather than a plausible-looking vector, so a
            // caller reading the wrong channel gets an obviously wrong
            // answer instead of a subtly right-looking one.
            return channel == 1 ? sio_vector : 0x00;
        default:
            return 0x00;
    }
}

static void sio_write_control(int channel, uint8_t value) {
    SioChannel *ch = &sio[channel];
    uint8_t reg = ch->wr_pointer;

    if (reg == 0) {
        // WR0 carries the next register pointer in bits 0-2 and a command
        // in bits 3-5. Only "channel reset" changes anything observable
        // here; the interrupt-related commands have nothing to act on,
        // since no SIO source can raise one with no device attached.
        ch->wr_pointer = value & 0x07;
        uint8_t command = (uint8_t)((value >> 3) & 0x07);
        if (command == 3) {  // channel reset
            sio_channel_reset(channel);
        }
        return;
    }

    ch->wr[reg] = value;
    ch->wr_pointer = 0;

    // WR2 holds the interrupt vector, and only channel B's copy is the
    // one the chip actually presents - the same arrangement the DART uses.
    if (reg == 2 && channel == 1) {
        sio_vector = value;
    }
}

// The 74ALS259 addressable latch at 13G. A write names one of eight bits
// in its low three bits and supplies the value in bit 7 - so a single OUT
// sets exactly one control line. Two of those lines steer the memory map,
// which is why this lives beside the port decode rather than in memory.c.
//
//   0 EME      extended memory enable - turns the page map on
//   1 40       40-column mode
//   2 HRU2 A8  palette PROM high address
//   3 PROT INI protection device, not modeled
//   4 TXOFF    blank the text layer
//   5 RTC CS   (inverted)
//   6 RTC CLK
//   7 RTC DIO / PROT DIN
static uint8_t sto_latch = 0;
static bool hru2_a8 = false;

// The 16-entry high-resolution colour lookup, written through port 0x07.
static uint8_t hrc[16];

static void sto_write(uint8_t value) {
    int bit = value & 0x07;
    bool state = (value & 0x80) != 0;
    if (state) sto_latch |= (uint8_t)(1u << bit);
    else       sto_latch &= (uint8_t)~(1u << bit);

    switch (bit) {
        case 0: abc806_set_eme(state); break;
        case 1: col80 = !state; break;   // 40-column line
        case 2: hru2_a8 = state; break;  // palette PROM high address
        // The clock's three lines. CS is inverted between the latch and
        // the chip, which is why a set latch bit *deselects* it.
        case 5: abc806_rtc_cs(!state); break;
        case 6: abc806_rtc_clk(state); break;
        case 7: abc806_rtc_dio_w(state); break;
        default: break;                  // 3 = PROT INI, 4 = TXOFF
    }
}

// the mirror arithmetic in exactly one place.
typedef enum {
    DEV_NONE, DEV_ABCBUS, DEV_DART, DEV_CRTC, DEV_SIO, DEV_CTC,
    // ABC806 additions. Everything above this line is shared with the
    // ABC802, whose ports.c this file was seeded from - see the note at
    // the top about why that duplication is deliberate for now.
    // 0x06 (HRS) and 0x07 (HRC) are handled inside DEV_ABCBUS, since they
    // overlap that range and only differ on write.
    DEV_MAP,    // 0x34 rw page map, entry in A8-A15
    DEV_ATTR,   // 0x35 rw attribute latch
    DEV_STO,    // 0x36 rw status in / 74LS259 addressable latch
    DEV_SSO     // 0x37 rw palette PROM in / sync select out
} Device;

static Device decode_port(uint8_t port, int *index) {
    // The ABC806's own ports are decoded before the ABC-bus range, because
    // 0x06 and 0x07 overlap it: MAME builds this map by installing the
    // ABC800M ports first and letting these overwrite them, and writes to
    // 0x06/0x07 are the video registers rather than ABC-bus C4/RST.
    // Reads at those addresses stay with the bus.
    // Exact low-byte matches, not a masked range. MAME gives 0x34-0x36
    // `select(0xff00)`/`mirror(0xff00)` - the *high* byte varies, carrying
    // the register index, and the low byte does not mirror at all. Only
    // 0x37 mirrors, with 0x18, so it also answers at 0x3F.
    //
    // Getting this wrong is not subtle in its effect but is very subtle to
    // spot: a `port & 0x3F` match here also claims 0x74, 0xB4 and 0xF4,
    // which are CTC mirrors, and the machine stops booting several
    // thousand instructions later with an illegal opcode.
    *index = 0;
    if (port == 0x34) return DEV_MAP;
    if (port == 0x35) return DEV_ATTR;
    if (port == 0x36) return DEV_STO;
    // 0x37 with MAME's mirror(0x18) means bits 3 and 4 are don't-care,
    // but the base already has bit 4 set - so the mirrored set is
    // {0x27, 0x2F, 0x37, 0x3F}, and 0x27/0x2F collide with DART mirrors.
    // Nothing reads this port yet (DEV_SSO is a stub), so the narrow,
    // certainly-correct pair is taken and the collision left documented
    // rather than resolved by guessing which device wins.
    if (port == 0x37 || port == 0x3F) return DEV_SSO;
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
    (void)stored;
    uint8_t value = 0xFF;
    int index = 0;

    switch (decode_port(port, &index)) {
        case DEV_MAP:
            // The page map reads back the entry the high address byte
            // selects. On OUT/IN (C) the Z80 puts B on A8-A15, so B is
            // the index - the same trick HRC uses.
            value = abc806_get_map(cpu->b & 0x0F);
            break;
        case DEV_ATTR:
            // Whatever the last character-RAM read latched.
            value = abc806_get_attr_latch();
            break;
        case DEV_STO:
            // Only bit 7 is defined (PROT DOUT) and MAME returns it low,
            // i.e. 0x7F. Reproduced rather than returning 0xFF, because a
            // protection line that reads high is a claim about hardware
            // this emulator has not modeled.
            value = 0x7F;
            break;
        case DEV_SSO:
            // One port, two devices: the HRU II palette PROM answers in
            // the low nibble and the real-time clock's data line in bit 7.
            // The PROM's address comes from the *high* address byte, which
            // for IN A,(C) is register B, plus the A8 line off the
            // 74ALS259.
            value = (uint8_t)(abc806_hru2_prom()[((hru2_a8 ? 1 : 0) << 8) |
                                                 cpu->b] & 0x0F);
            if (abc806_rtc_dio_r()) value |= 0x80;
            break;
        case DEV_ABCBUS:
            // The synthetic controller answers only when it is the
            // selected device; otherwise the bus floats high, which is
            // the ROM's own "no card fitted" signal (see disk.c). With no
            // image attached nothing is selected ever, so this is exactly
            // the pre-existing behavior.
            value = abcbus_disk_in(index);
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
        case DEV_SIO: {
            // Port layout matches the DART's: bit 1 selects the channel,
            // bit 0 selects control vs data.
            int channel = (index & 0x02) ? 1 : 0;
            if (index & 0x01) {
                value = sio_read_control(channel);
            } else {
                value = sio[channel].rx_data;
                sio[channel].rx_ready = false;
            }
            break;
        }
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
    if (trace_io) fprintf(stderr, "[out] %02X <- %02X\n", port, value);

    int index = 0;
    switch (decode_port(port, &index)) {
        case DEV_MAP:
            // OUT (C),r puts B on A8-A15, and the page map takes its entry
            // number from there rather than from the port number.
            // The index is the *high address byte*, so a trace has to show B.
            if (getenv("ABC806_TRACE_MAP")) fprintf(stderr, "[map] b=%02X <- %02X\n", cpu->b, value);
            abc806_set_map(cpu->b & 0x0F, value);
            break;
        case DEV_ATTR:
            // Loads the latch that a subsequent character-RAM write
            // carries into the parallel attribute plane.
            abc806_set_attr_latch(value);
            break;
        case DEV_STO:
            sto_write(value);
            break;
        case DEV_SSO:
            break;   // sync select; nothing depends on it yet
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
        case DEV_SIO: {
            int channel = (index & 0x02) ? 1 : 0;
            if (index & 0x01) {
                sio_write_control(channel, value);
            }
            // A data write is a transmitted byte. Channel A's would go to
            // the RS-232 port and channel B's to the cassette; neither
            // exists, so it is discarded - but the transmit-empty status
            // above still reports it as sent, which is what keeps the
            // ROM's own polling loops from hanging.
            break;
        }
        case DEV_CTC:
            ctc_write(index, value);
            break;
        case DEV_ABCBUS:
            // On this machine 0x06 and 0x07 are video registers on write,
            // overlapping the ABC-bus range; reads there stay with the bus.
            if (index == 6) { abc806_set_hrs(value); break; }
            if (index == 7) { hrc[cpu->b & 0x0F] = value; break; }
            // Port 1 is CS: it selects which expansion card listens. The
            // ROM masks the select to 6 bits itself (AND 3Fh at 0x6172);
            // masking again here keeps the card honest if some other
            // caller does not.
            if (index == 1) abcbus_disk_select(value & 0x3F);
            else abcbus_disk_out(index, value);
            break;
        default:
            break;
    }

    return 1;  // fully decoded here; never fall through to the io_ports[] store
}

// --------------------------------------------------------------- timing

void abc806_ports_tick(Z80 *cpu, int cycles) {
    frame_cycles += cycles;

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

int abc806_cursor_address(void) {
    // R10 bits 6:5 = 01 is "cursor not displayed"; every other value shows
    // it, blinking or not.
    if ((crtc_regs[10] & 0x60) == 0x20) return -1;
    return ((crtc_regs[14] << 8) | crtc_regs[15]) & 0x7FF;
}

void abc806_ports_attach(Z80 *cpu) {
    trace_io = getenv("ABC806_TRACE_IO") != NULL;

    memset(ctc, 0, sizeof(ctc));
    memset(dart, 0, sizeof(dart));
    memset(sio, 0, sizeof(sio));
    memset(crtc_regs, 0, sizeof(crtc_regs));
    crtc_addr = 0;
    ctc_vector = 0;
    dart_vector = 0;
    sio_vector = 0;
    col80 = true;

    // Two configuration DIP switches reach the ROM as SIO channel B
    // modem-status inputs rather than through anything memory-mapped:
    // S1 ("Clear Screen Time Out") on DCD and S2 (undocumented, in MAME
    // too) on CTS. Defaults follow MAME's own - S1 off, S2 on. They are
    // wired here rather than left at zero because a status bit the
    // hardware genuinely asserts is not the emulator's to withhold.
    sio[1].dcd = false;  // S1 off
    sio[1].cts = true;   // S2 on

    cpu->io_in_hook = io_in;
    cpu->io_out_hook = io_out;
}
