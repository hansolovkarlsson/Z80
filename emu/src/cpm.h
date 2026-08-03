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
// Enables/disables CCP-boot mode: with it on, a warm boot (BIOS WBOOT,
// including BDOS function 0/P_TERMCPM) re-enters the CCP at CCP_BASE
// instead of halting the emulator - see check_cpm_bios()'s WBOOT case.
void cpm_set_ccp_mode(int enabled);
int cpm_is_ccp_mode(void);

#endif
