echo "+----------------------------+"
echo "| COMPILING                  |"
echo "+----------------------------+"
gcc -Wall -Wextra -O2 main.c z80.c cpm.c alu.c -o z80_emulator
