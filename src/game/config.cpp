// The settings: RecompFrontend's General, Controls, Graphics, Sound and Mods tabs, with its default bindings. Its own
// Graphics options carry what a recomp needs: Resolution and MS Anti-Aliasing (high resolution), Framerate
// (interpolation to the display's rate) and Aspect Ratio and HUD Placement (widescreen). The General tab adds Mouse
// Sensitivity, mouse look (src/game/mouse_look.cpp): 50% on a new profile, as N64Recomp-browser's checklist has a new
// profile's mouse look on, where RecompFrontend's own option starts at 0 and speaks of Zelda's items, so the tab
// leaves its own out and adds this one. At 0 the camera is the console's and the cursor isn't held in the window. It
// leaves out Rumble Strength and Gyro Sensitivity: the game has no Rumble Pak support (no osMotor in its code, no
// "Rumble Pak" in its ROM) and nothing in it reads the gyro, so each would change nothing.

#include <filesystem>

#include "recompui/config.h"
#include "util/file.h"
#include "rayman2.h"

void r2::init_config() {
    std::filesystem::path app_dir = recompui::file::get_app_folder_path();
    if (!app_dir.empty()) {
        std::filesystem::create_directories(app_dir);
    }

    recompui::config::GeneralTabOptions general_options{};
    general_options.has_rumble_strength = false;
    recomp::config::Config& general = recompui::config::create_general_tab(general_options);
    general.add_percent_number_option(
        recompui::config::general::options::mouse_sensitivity,
        "Mouse Sensitivity",
        "Turns the camera with the mouse while a level plays: left and right swing it around Rayman, as C-Left and "
        "C-Right do, and in first person (C-Up) the mouse looks around, up and down the way the stick does."
        "<br />"
        "<br />"
        "<b>Setting this to zero turns mouse look off, as the game plays on a console.</b>",
        50.0
    );
    recompui::config::create_controls_tab();
    recompui::config::create_graphics_tab();
    recompui::config::create_sound_tab();
    recompui::config::create_mods_tab();
    recompui::config::finalize();
}
