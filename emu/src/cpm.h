#ifndef _CPM_H
#define _CPM_H

#include "z80.h"

void cpm_console_init(void);
void cpm_fileio_init(void);
void check_cpm_bdos(Z80 *cpu, uint8_t *ram);

#endif
