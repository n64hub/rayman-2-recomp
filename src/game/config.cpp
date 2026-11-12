// The settings: RecompFrontend's General, Controls, Graphics, Sound and Mods tabs, with its default bindings. Its own
// Graphics options carry what a recomp needs: Resolution and MS Anti-Aliasing (high resolution), Framerate
// (interpolation to the display's rate) and Aspect Ratio and HUD Placement (widescreen). The General tab leaves out
// Rumble Strength, Gyro Sensitivity and Mouse Sensitivity: the game has no Rumble Pak support (no osMotor in its code,
// no "Rumble Pak" in its ROM) and nothing in it reads the gyro or the mouse, so each would change nothing, and Mouse
// Sensitivity above 0 (a Steam Deck's default) would only hold the cursor in the window.

#include <filesystem>

#include "recompui/config.h"
#include "util/file.h"
#include "rayman2.h"

void r2::init_config() {
    std::filesystem::path app_dir = recompui::file::get_app_folder_path();
    if (!app_dir.empty()) {
        std::filesystem::create_directories(app_dir);
    }

    recompui::config::create_general_tab(recompui::config::GeneralTabOptions{});
    recompui::config::create_controls_tab();
    recompui::config::create_graphics_tab();
    recompui::config::create_sound_tab();
    recompui::config::create_mods_tab();
    recompui::config::finalize();
}
