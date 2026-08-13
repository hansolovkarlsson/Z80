// abc80/emu/src/charset.c - ABC80's real character set in TEXT mode: the
// Swedish/Finnish national variant of ISO 646 (SEN 850200 Annex B), not
// plain ASCII. Not assumed from the standard alone - chargen.c's own
// comment already found character 0x5B decoding to a clear "A with
// umlaut dots" via bin/abc80-chargen-dump; building this file went on to
// decode all eight other candidate positions (0x40, 0x5C, 0x5E, 0x60,
// 0x7B, 0x7C, 0x7D, 0x7E) the same way and confirmed every one matches
// the standard Swedish mapping exactly: É Ä Ö Å Ü é ä ö å ü in place of
// plain ASCII's @ [ \ ] ^ ` { | } ~.

#include "charset.h"

uint32_t abc80_charset_codepoint(uint8_t character) {
    character &= 0x7F;
    switch (character) {
        case 0x40: return 0xC9; // É
        case 0x5B: return 0xC4; // Ä
        case 0x5C: return 0xD6; // Ö
        case 0x5D: return 0xC5; // Å
        case 0x5E: return 0xDC; // Ü
        case 0x60: return 0xE9; // é
        case 0x7B: return 0xE4; // ä
        case 0x7C: return 0xF6; // ö
        case 0x7D: return 0xE5; // å
        case 0x7E: return 0xFC; // ü
        default:
            // Control codes (0x00-0x1F, 0x7F) render as blank in this
            // whole-cell text renderer - real chargen output for these is
            // empty anyway (see chargen_dump's own output for 0x00-0x1F).
            if (character < 0x20 || character == 0x7F) return 0x20;
            return character; // plain ASCII range, unchanged
    }
}
