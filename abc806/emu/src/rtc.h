// abc806/emu/src/rtc.h - the Microelectronic-Marin E050-16 real-time
// clock, as the ABC806 wires it.
//
// The machine points at this itself: booted from a real UFD-DOS system
// disk, the DOS prints `Datum och tid: 19é5-é5-é5 é5.é5.é5` - a date line
// made of whatever the unmodelled clock happened to return. It is the one
// device the ROM visibly asks for and does not get.
//
// The chip has no address or data bus. It hangs off three bits of the
// 74ALS259 addressable latch at port 0x36 - chip select, clock, and a
// bidirectional data line - and its output is read back as bit 7 of port
// 0x37, sharing that port with the HRU II palette PROM's low nibble. So a
// single register read is thirty-odd `OUT`s and `IN`s, and the whole
// protocol is a shift register.

#ifndef ABC806_RTC_H
#define ABC806_RTC_H

#include <stdbool.h>
#include <stdint.h>

// The three lines, driven from the 74ALS259. CS is active low: the latch
// bit is inverted on the way to the chip, which the caller has already
// done by the time it gets here.
void abc806_rtc_cs(bool state);
void abc806_rtc_clk(bool state);
void abc806_rtc_dio_w(bool state);

// The data line read back, which becomes bit 7 of port 0x37.
bool abc806_rtc_dio_r(void);

// Seed the clock from the host's own time, so a booted machine shows a
// plausible date rather than midnight on the first of January.
void abc806_rtc_init(void);

#endif // ABC806_RTC_H
