#pragma once

#include <cstdint>

enum class CodePatternOperation : std::uint8_t {
    None,
    Abs4,
    Abs5,
    Read,
    // Reads a single byte at the offset and sign-extends it to 32 bits - for sites that
    // recompiled from disp32 to disp8 (e.g. lea reg,[reg+0x68]); .read() there would grab
    // the next instruction's bytes as a "displacement" (the 2026-09-26 PanelStyleOffset
    // class of breakage: byte-match green, resolved value garbage, wild style writes).
    Read8
};
