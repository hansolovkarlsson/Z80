// abc806/emu/src/rtc.c - E050-16 real-time clock.
//
// Reimplemented from MAME's e0516_device (src/devices/machine/e0516.cpp,
// BSD-3-Clause, Curt Coder) and from the ABC806's own wiring of it.
//
// ## The protocol
//
// Every transaction begins by taking CS low, which resets the shift state
// and arms it for a four-bit command. Bits are then clocked in on CLK's
// rising edge, most significant first:
//
//   bits 3:1   register address 0-7
//   bit 0      1 = read, 0 = write
//
// Address 7 is not a register but a mode: continuous read-out or
// continuous write-in of all seven registers at once, 56 bits. Any other
// address transfers a single 8-bit BCD register.
//
// Data then moves on the *falling* edge for reads and the rising edge for
// writes - the asymmetry matters, and getting it wrong yields a value that
// is plausibly shaped and wrong by one bit position.
//
// ## What the ABC806 simplifies
//
// The real chip has an OUTSEL pin that can put the data line into high
// impedance for part of a read. This machine ties it high
// (`m_rtc->outsel_rd_cb().set_constant(1)` in MAME's own config), which
// removes the hi-Z state and its special case entirely. That is worth
// knowing before porting this to another ABC800-family machine: the
// simplification is the board's, not the chip's.
//
// ## Registers
//
// Seven, each a BCD byte: second, minute, hour, day, month, year,
// day-of-week. In continuous read-out the latch is assembled with second
// at the top and hour at the bottom, and shifts out LSB first - so the
// first byte the host sees is the hour and the last is the second.

#include <time.h>

#include "rtc.h"

enum { RTC_SECOND, RTC_MINUTE, RTC_HOUR, RTC_DAY, RTC_MONTH, RTC_YEAR,
       RTC_DAY_OF_WEEK, RTC_REGS };

enum { STATE_ADDRESS, STATE_DATA_READ, STATE_DATA_WRITE };

static uint8_t reg[RTC_REGS];      // BCD, as the chip stores them

static bool cs = true;             // active low; idle high
static bool clk = false;
static bool dio = false;

static int state = STATE_ADDRESS;
static int bits_left = 4;
static uint8_t reg_latch = 0;      // the 4-bit command being shifted in
static uint64_t data_latch = 0;

static uint8_t to_bcd(int v) {
    return (uint8_t)(((v / 10) % 10) << 4 | (v % 10));
}

static int from_bcd(uint8_t v) {
    return ((v >> 4) & 0x0F) * 10 + (v & 0x0F);
}

void abc806_rtc_init(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t) {
        reg[RTC_SECOND]      = to_bcd(t->tm_sec);
        reg[RTC_MINUTE]      = to_bcd(t->tm_min);
        reg[RTC_HOUR]        = to_bcd(t->tm_hour);
        reg[RTC_DAY]         = to_bcd(t->tm_mday);
        reg[RTC_MONTH]       = to_bcd(t->tm_mon + 1);
        reg[RTC_YEAR]        = to_bcd(t->tm_year % 100);
        reg[RTC_DAY_OF_WEEK] = to_bcd(t->tm_wday);
    }
    cs = true;
    clk = false;
    dio = false;
    state = STATE_ADDRESS;
    bits_left = 4;
    reg_latch = 0;
    data_latch = 0;
}

static int address(void) { return (reg_latch >> 1) & 0x07; }

void abc806_rtc_cs(bool state_in) {
    if (!cs && state_in) {
        // Rising edge, i.e. the transaction ending. A single-register
        // write commits here rather than on the last clock, which is the
        // chip's own behaviour: the host can abandon a write by never
        // releasing CS.
        if (state == STATE_DATA_WRITE && bits_left == 0 && address() != 7)
            reg[address()] = (uint8_t)(data_latch & 0xFF);
    }
    if (cs != state_in && !state_in) {
        // Falling edge: arm for a fresh command.
        data_latch = 0;
        reg_latch = 0;
        bits_left = 4;
        state = STATE_ADDRESS;
    }
    cs = state_in;
}

void abc806_rtc_dio_w(bool state_in) {
    // The host only drives the line when it is not the chip's turn to.
    if (state != STATE_DATA_READ) dio = state_in;
}

bool abc806_rtc_dio_r(void) {
    if (cs) return false;   // deselected: the line floats, read as 0
    return dio;
}

void abc806_rtc_clk(bool state_in) {
    if (cs) return;
    if (clk == state_in) return;
    clk = state_in;

    if (!bits_left) return;

    // Reads move on the falling edge, writes and the command on the
    // rising one.
    if (!clk && state != STATE_DATA_READ) return;
    if (clk && state == STATE_DATA_READ) return;

    bits_left--;

    if (state == STATE_ADDRESS) {
        reg_latch = (uint8_t)((reg_latch << 1) | (dio ? 1 : 0));
        reg_latch &= 0x0F;
        if (bits_left) return;

        if (reg_latch & 0x01) {
            state = STATE_DATA_READ;
            if (address() == 7) {
                // Continuous read-out: all seven registers, second at the
                // top of the latch and hour at the bottom, shifted out
                // LSB first.
                bits_left = 56;
                data_latch = reg[RTC_SECOND];
                data_latch = (data_latch << 8) | reg[RTC_DAY_OF_WEEK];
                data_latch = (data_latch << 8) | reg[RTC_YEAR];
                data_latch = (data_latch << 8) | reg[RTC_MONTH];
                data_latch = (data_latch << 8) | reg[RTC_DAY];
                data_latch = (data_latch << 8) | reg[RTC_MINUTE];
                data_latch = (data_latch << 8) | reg[RTC_HOUR];
            } else {
                bits_left = 8;
                data_latch = reg[address()];
            }
        } else {
            state = STATE_DATA_WRITE;
            bits_left = (address() == 7) ? 56 : 8;
        }
        return;
    }

    if (state == STATE_DATA_READ) {
        dio = (data_latch & 1) != 0;
        data_latch >>= 1;
        return;
    }

    // Write. Continuous mode commits each register as its last bit
    // arrives; a single-register write commits when CS is released.
    data_latch = (data_latch << 1) | (dio ? 1 : 0);
    if (address() == 7) {
        switch (bits_left) {
            case 48: reg[RTC_HOUR]        = data_latch & 0xFF; break;
            case 40: reg[RTC_MINUTE]      = data_latch & 0xFF; break;
            case 32: reg[RTC_DAY]         = data_latch & 0xFF; break;
            case 24: reg[RTC_MONTH]       = data_latch & 0xFF; break;
            case 16: reg[RTC_YEAR]        = data_latch & 0xFF; break;
            case 8:  reg[RTC_DAY_OF_WEEK] = data_latch & 0xFF; break;
            case 0:  reg[RTC_SECOND]      = data_latch & 0xFF; break;
            default: break;
        }
    }
    (void)from_bcd;
}
