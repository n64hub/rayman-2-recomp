#ifndef __RAYMAN2_H__
#define __RAYMAN2_H__

#include <cstdint>
#include <string_view>

namespace r2 {
    inline constexpr std::u8string_view program_id = u8"Rayman2Recompiled";
    inline constexpr std::string_view program_name = "Rayman 2: Recompiled";

    // The game's code sections (register_overlays.cpp) and the patches over it (register_patches.cpp).
    void register_overlays();
    void register_patches();

    // The settings (config.cpp): RecompFrontend's tabs.
    void init_config();

    // Maps the main and support segments at the addresses the game reads them to (register_overlays.cpp), before the
    // game runs.
    void map_code_sections();
}

// Mouse look (mouse_look.cpp): the mouse's motion since the last input poll, read at each one, or --input's.
extern "C" void r2_poll_mouse();
extern "C" void r2_add_mouse_motion(float x, float y);

#endif
