#include <stdio.h>
#include "z80.h"


void check_cpm_bdos(Z80 *cpu, uint8_t *ram) {
    if (cpu->pc == 0x0005) { // Intercept call to BDOS entry
        if (cpu->c == 2) {
            // Function 2: Console Output (Char in E)
            putchar(cpu->e);
            fflush(stdout); // Flush buffer immediately so test prints show instantly
        } 
        else if (cpu->c == 9) {
            // Function 9: Print String (Address in DE, terminated by '$')
            uint16_t addr = cpu->de;
            while (ram[addr] != '$') {
                putchar(ram[addr++]);
            }
            fflush(stdout);
        }

        // Simulate RET: Pop return address off the stack into PC
        uint8_t low = ram[cpu->sp++];
        uint8_t high = ram[cpu->sp++];
        cpu->pc = (high << 8) | low;
    }
}

