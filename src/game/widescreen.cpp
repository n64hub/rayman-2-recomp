// Widescreen for the game's own culling (us.toml's hook in func_8009989C calls it). RT64's Aspect Ratio Expand widens
// the picture's 3D, but the game decides what to draw by its camera's planes, which it builds for 4:3: an object or a
// piece of the level past them was never drawn, so at the wide picture's sides things popped in and out. This widens
// the camera's left and right planes to the window's aspect ratio as RT64 widens the projection.

#include <algorithm>
#include <cmath>
#include <cstring>

#include "recomp.h"
#include "ultramodern/config.hpp"
#include "recompui/recompui.h"

// How many times wider than 4:3 RT64 draws the 3D: the window's aspect ratio with Expand, 16:9 with Manual (RT64's
// aspectTarget, which RecompFrontend leaves at its default), 1 with Original. RT64 never draws narrower than 4:3.
static float widescreen_scale() {
    float target = 4.0f / 3.0f;
    switch (ultramodern::renderer::get_graphics_config().ar_option) {
        case ultramodern::renderer::AspectRatio::Expand: {
            int width = 0, height = 0;
            recompui::get_window_size(width, height);
            if (width > 0 && height > 0) {
                target = float(width) / float(height);
            }
            break;
        }
        case ultramodern::renderer::AspectRatio::Manual:
            target = 16.0f / 9.0f;
            break;
        default:
            break;
    }
    return std::max(1.0f, target / (4.0f / 3.0f));
}

static float load_f32(uint8_t* rdram, gpr addr) {
    int32_t bits = MEM_W(0, addr);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

static void store_f32(uint8_t* rdram, gpr addr, float value) {
    int32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    MEM_W(0, addr) = bits;
}

// The camera keeps its clipping planes at +0x98, four floats each (x, y, z, d, in the camera's space): left, right,
// top, bottom. The left and right have no y, and their edge at a depth z is the x where x*a + z*c + d = 0, so x is
// widened by k with (a, 0, k*c, k*d), normalized again for the game's sphere tests: a perspective camera's planes
// (d = 0) take the wider field of view, an orthographic one's (c = 0) the wider half-width.
extern "C" void r2_widen_cull_planes(uint8_t* rdram, gpr camera) {
    float k = widescreen_scale();
    if (k <= 1.0f) {
        return;
    }
    for (gpr plane = camera + 0x98; plane < camera + 0xB8; plane += 0x10) {
        float a = load_f32(rdram, plane + 0x0);
        float c = load_f32(rdram, plane + 0x8) * k;
        float d = load_f32(rdram, plane + 0xC) * k;
        float length = std::sqrt(a * a + c * c);
        if (length <= 0.0f) {
            continue;
        }
        store_f32(rdram, plane + 0x0, a / length);
        store_f32(rdram, plane + 0x8, c / length);
        store_f32(rdram, plane + 0xC, d / length);
    }
}
