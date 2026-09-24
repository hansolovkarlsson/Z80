export BASEDIR=`pwd`
# First on PATH, not last: other packages ship tools with the same names
# (Homebrew has its own z80asm), and appending let those win silently.
export PATH="$BASEDIR/bin:$PATH"
