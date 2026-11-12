// Mouse look: the General tab's Mouse Sensitivity (config.cpp) turns the game's camera by the mouse, 0 leaving it to
// the stick and the C buttons as the console does. The game reads no mouse, so the camera is turned where it is kept:
// the camera's superobject (its pointer at 0x800E5FF8) and its matrix, which the game reads back each frame
// (docs/architecture.md, The camera). main.cpp's poll_input adds each poll's motion (RecompFrontend's deltas, scaled by
// the option) to what this holds, and us.toml's hook at the end of func_8008E944, as each frame starts, turns the
// camera by it on the game's own thread.
//
// Mouse x turns the view: behind Rayman the camera swings around him as a C-Left or C-Right press does, about the point
// it looks at, and the game swings it back behind him as it does after one; in first person (C-Up) it turns in place.
// Mouse y looks up and down in first person only, the way the game's stick does there: pulled back (the mouse moved
// toward the player) looks up. Only while a level plays (mode 9 at 0x800E5D43) with no pause menu (0x800C87D0): motion
// meanwhile is dropped.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

#include "recomp.h"
#include "recompinput/recompinput.h"
#include "rayman2.h"

// Radians the view turns for a pixel of motion at 100%.
static constexpr float radians_per_pixel = 0.004f;
// The camera's distance from the point it looks at, under which it is first person's (about 1.1; behind Rayman about
// 8.4).
static constexpr float first_person_distance = 3.0f;
// How far first person looks up or down, about as far as the stick reaches (column 2's z about 0.95).
static constexpr float pitch_limit = 1.25f;

static std::mutex motion_mutex;
static float motion_x = 0.0f;
static float motion_y = 0.0f;

extern "C" void r2_add_mouse_motion(float x, float y) {
    std::lock_guard lock{ motion_mutex };
    motion_x += x;
    motion_y += y;
}

extern "C" void r2_poll_mouse() {
    float x, y;
    recompinput::get_mouse_deltas(&x, &y);
    if (x != 0.0f || y != 0.0f) {
        r2_add_mouse_motion(x, y);
    }
}

static bool in_rdram(uint32_t addr) {
    return addr >= 0x80000000u && addr < 0x80800000u && (addr & 3) == 0;
}

static float load_f32(uint8_t* rdram, uint32_t addr) {
    int32_t bits = MEM_W(0, (gpr)(int32_t)addr);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

static void store_f32(uint8_t* rdram, uint32_t addr, float value) {
    int32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    MEM_W(0, (gpr)(int32_t)addr) = bits;
}

// Rotates the x and y of the three floats at addr about the vertical (z), by c and s.
static void turn_xy(uint8_t* rdram, uint32_t addr, float c, float s) {
    float x = load_f32(rdram, addr), y = load_f32(rdram, addr + 4);
    store_f32(rdram, addr, c * x - s * y);
    store_f32(rdram, addr + 4, s * x + c * y);
}

extern "C" void r2_mouse_look(uint8_t* rdram) {
    float x, y;
    {
        std::lock_guard lock{ motion_mutex };
        x = motion_x;
        y = motion_y;
        motion_x = motion_y = 0.0f;
    }
    if ((x == 0.0f && y == 0.0f) || MEM_BU(0, (gpr)(int32_t)0x800E5D43) != 9 || MEM_BU(0, (gpr)(int32_t)0x800C87D0) != 0) {
        return;
    }

    // The superobject's engine object (+0x4) holds its cineinfo (+0x10), whose +0x28 is the point the camera looks at;
    // its matrix (+0x20) the camera's position (+0x4) and three columns (+0x10, +0x1C, +0x28): the first is level, the
    // side, and the other two turn about it as the view looks up and down.
    uint32_t spo = MEM_W(0, (gpr)(int32_t)0x800E5FF8);
    if (!in_rdram(spo)) return;
    uint32_t engine = MEM_W(0x4, (gpr)(int32_t)spo);
    uint32_t matrix = MEM_W(0x20, (gpr)(int32_t)spo);
    if (!in_rdram(engine) || !in_rdram(matrix)) return;
    uint32_t cine = MEM_W(0x10, (gpr)(int32_t)engine);
    if (!in_rdram(cine)) return;
    if (std::fabs(load_f32(rdram, matrix + 0x18)) > 0.2f) return;

    float pivot_x = load_f32(rdram, cine + 0x28), pivot_y = load_f32(rdram, cine + 0x2C), pivot_z = load_f32(rdram, cine + 0x30);
    float dx = load_f32(rdram, matrix + 0x4) - pivot_x, dy = load_f32(rdram, matrix + 0x8) - pivot_y,
        dz = load_f32(rdram, matrix + 0xC) - pivot_z;
    if (!std::isfinite(dx + dy + dz)) return;
    bool first_person = dx * dx + dy * dy + dz * dz < first_person_distance * first_person_distance;

    // A positive angle turns counterclockwise seen from above, the way C-Right swings the camera, so the view left.
    float yaw = -x * radians_per_pixel;
    if (yaw != 0.0f) {
        float c = std::cos(yaw), s = std::sin(yaw);
        if (!first_person) {
            store_f32(rdram, matrix + 0x4, pivot_x + c * dx - s * dy);
            store_f32(rdram, matrix + 0x8, pivot_y + s * dx + c * dy);
        }
        turn_xy(rdram, matrix + 0x10, c, s);
        turn_xy(rdram, matrix + 0x1C, c, s);
        turn_xy(rdram, matrix + 0x28, c, s);
    }

    // Column 2 is the view's back, column 3 its up: a positive angle tips the back up, so the view looks down. The game
    // doesn't hold the view to its own limits once it is turned, so the mouse stops at pitch_limit (or where the stick
    // left it, past that).
    if (first_person && y != 0.0f) {
        float b[3], u[3];
        for (int i = 0; i < 3; i++) {
            b[i] = load_f32(rdram, matrix + 0x1C + i * 4);
            u[i] = load_f32(rdram, matrix + 0x28 + i * 4);
        }
        float pitch = std::atan2(b[2], u[2]);
        float limit = std::max(pitch_limit, std::fabs(pitch));
        float angle = std::clamp(pitch - y * radians_per_pixel, -limit, limit) - pitch;
        float c = std::cos(angle), s = std::sin(angle);
        for (int i = 0; i < 3; i++) {
            store_f32(rdram, matrix + 0x1C + i * 4, c * b[i] + s * u[i]);
            store_f32(rdram, matrix + 0x28 + i * 4, -s * b[i] + c * u[i]);
        }
    }
}
