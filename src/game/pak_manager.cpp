// The Controller Pak manager's font. The pak itself is the runtime's (librecomp/src/pak.cpp, on in port 1 by the
// GameEntry's controller_pak_ports in src/main/main.cpp); this is the one part of the game's pak screens it can't run.
//
// At boot the game opens its Controller Pak manager when Start is held on controller 1 with a pak in it (a player
// pressing Start through the logos holds it then), and func_800F7960 decodes its font: 13-bit values packed in the ROM
// from 0xB70 on, read with the CPU straight from the cartridge's address, 0xB0000B70, as a console can. The runtime
// maps no cartridge there, so that load read a wild host address and the program crashed a few seconds in. A hook in
// us.toml runs this in its place, the same decode over the ROM the runtime holds: a0 is the font's buffer of 0x340
// halfwords, kept in 0x801013B0, cleared and filled from its +4 in rows of 14 values at the row each of the three pairs
// at 0x801009E0 (a row index, then a count of rows) names, 2 bits skipped after each row.

#include <cstdint>
#include <span>

#include "recomp.h"
#include "librecomp/game.hpp"

extern "C" void r2_pak_manager_font(uint8_t* rdram, recomp_context* ctx) {
    std::span<const uint8_t> rom = recomp::get_rom();
    uint32_t rom_offset = 0xB70;
    auto next_word = [&]() {
        uint32_t word = ((uint32_t)rom[rom_offset] << 24) | (rom[rom_offset + 1] << 16) | (rom[rom_offset + 2] << 8) |
            rom[rom_offset + 3];
        rom_offset += 4;
        return word;
    };
    gpr buffer = ctx->r4;
    MEM_W(0, (gpr)(int32_t)0x801013B0) = (int32_t)buffer;
    for (int i = 0; i < 0x340; i++) {
        MEM_H(2 * i, buffer) = 0;
    }
    uint32_t word = next_word();
    uint32_t bit = 0;
    for (int pair = 0; pair < 3; pair++) {
        uint32_t row_index = MEM_BU(2 * pair, (gpr)(int32_t)0x801009E0);
        uint32_t rows = MEM_BU(2 * pair + 1, (gpr)(int32_t)0x801009E0);
        gpr out = buffer + row_index * 32 + 4;
        for (uint32_t row = 0; row < rows; row++) {
            for (int i = 0; i < 14; i++, out += 2) {
                bit += 13;
                if (bit >= 32) {
                    bit &= 31;
                    MEM_H(0, out) = (word << bit) & 0x1FFF;
                    word = next_word();
                    if (bit == 0) {
                        continue;
                    }
                }
                MEM_H(0, out) = MEM_HU(0, out) | ((word >> (32 - bit)) & 0x1FFF);
            }
            out += 4;
            bit += 2;
        }
    }
}
