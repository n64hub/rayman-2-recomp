#include "rayman2.h"
#include "../../RecompiledFuncs/recomp_overlays.inl"

#include "librecomp/overlays.hpp"

// The game's code sections, from N64Recomp's section table (syms/, tools/gen_syms.py): the boot segment, and the main
// and support segments the game thread reads out of ROM once at boot and keeps.
void r2::register_overlays() {
    recomp::overlays::overlay_section_table_data_t sections {
        .code_sections = section_table,
        .num_code_sections = ARRLEN(section_table),
        .total_num_sections = num_sections,
    };

    recomp::overlays::overlays_by_index_t overlays {
        .table = overlay_sections_by_index,
        .len = ARRLEN(overlay_sections_by_index),
    };

    recomp::overlays::register_overlays(sections, overlays);
}

// The runtime maps every section in the 1 MB after ROM 0x1000 at start, as if it ran where the IPL3 copies it, past
// 0x80000400. Only the boot segment does: main and support lie in that MB too, but the game reads them to 0x80025C50
// and 0x800F64A0, so they are taken back out and mapped there instead (the game's own copies come before any call).
void r2::map_code_sections() {
    constexpr uint32_t boot_rom = 0x1000, boot_ram = 0x80000400;
    constexpr uint32_t main_rom = 0x1DCC0, main_ram = 0x80025C50, main_size = 0xA7F30;
    constexpr uint32_t support_rom = 0xC5BF0, support_ram = 0x800F64A0, support_size = 0xAE30;
    unload_overlays((int32_t)(boot_ram + main_rom - boot_rom), support_rom + support_size - main_rom);
    load_overlays(main_rom, (int32_t)main_ram, main_size);
    load_overlays(support_rom, (int32_t)support_ram, support_size);
}
