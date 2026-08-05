#ifndef _CPM_H
#define _CPM_H

#include "z80.h"

void cpm_console_init(void);
void cpm_fileio_init(void);
void cpm_bios_init(uint8_t *ram);
void check_cpm_bdos(Z80 *cpu, uint8_t *ram);
void check_cpm_bios(Z80 *cpu, uint8_t *ram);
// Address a CCP is loaded at when booting one (see cpm_set_ccp_mode) -
// main.c needs this to know where to load the CCP image and set the
// initial PC.
#define CCP_BASE 0xE400
// Nominal BDOS entry address written into the standard "JP <bdos>" at
// 0x0005-0x0007 (see cpm/docs/CPM_REFERENCE.md's zero-page map) - no real
// resident BDOS code lives here (BDOS calls are intercepted at 0x0005
// directly, before fetch), but the address itself matters: real CP/M
// software commonly reads it back (LHLD 6/LD HL,(6)) as a proxy for "top
// of available TPA" to gauge free memory, e.g. Turbo Pascal's TINST.COM
// terminal-setup utility refuses to run at all if this looks too low.
// Comfortably between CCP_BASE and BIOS_BASE, giving a plausible ~61KB
// of apparent free memory.
#define BDOS_ENTRY 0xF200
// Enables/disables CCP-boot mode: with it on, a warm boot (BIOS WBOOT,
// including BDOS function 0/P_TERMCPM) re-enters the CCP at CCP_BASE
// instead of halting the emulator - see check_cpm_bios()'s WBOOT case.
void cpm_set_ccp_mode(int enabled);
int cpm_is_ccp_mode(void);

#endif
