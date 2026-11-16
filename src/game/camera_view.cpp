// The camera's view, handed to RT64 so that the frames it draws between the game's own know where the camera is: a
// camera that cut to another shot then pairs nothing across the cut (RT64_SKIP_CAMERA_CUTS), where before the frames
// between drew the world halfway between the two shots.
//
// The game puts its camera into every model's matrix: each modelview it loads is the model's place times the view, and
// its projection is a bare perspective, so RT64 found an identity view in every frame and its cut test saw a camera
// that never moved. RT64's extended view matrix (gEXSetViewMatrixFloat) is for such a game: RT64 takes each world
// matrix as the modelview times that matrix and the view as its inverse, which draws the same picture, so given the
// inverse of the game's view, the world stands still where it stands still and the camera is the view.
//
// func_8009989C sets a camera up for a frame (us.toml's hooks): the camera object it is given, or the one before where
// it is given none, which it keeps at 0x800CF2E0 either way, holds its view at +0xC, the translation at +0x10 and the rotation's rows at +0x1C, in world units, and the viewport's own display
// list at +0x8, which the frame's list calls before what the camera draws (its viewport, perspective normal and
// projection, then G_ENDDL), leaves room for three commands before the projection's Mtx at +0x48. The display list's
// units are 32 to a world unit, so its view is that one with the translation times 32. Where the list's end is written,
// this puts gEXEnable and gEXSetViewMatrixFloat in its place, the end after them, and the matrix in a slot of
// view_slots_start that is the viewport's for good: RT64 reads it as it reads the list, and the game writes the
// viewport's Mtx beside it no sooner either.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "recomp.h"

// A slot of 0x40 bytes a viewport, at the top of the extra RAM the patches are linked into (patches/patches.ld ends
// them below it), under 16 MB, where RT64 reads without its extended RDRAM.
static constexpr uint32_t view_slots_start = 0x80FFF000;
static constexpr uint32_t view_slots_end = 0x81000000;
static constexpr uint32_t view_slot_size = 0x40;
// The display list's units to a world unit.
static constexpr float list_units_per_world_unit = 32.0f;
// rt64_extended_gbi.h's gEXEnable for the extended opcode 0x64, and gEXSetViewMatrixFloat.
static constexpr uint32_t ex_enable_w0 = 0x00525464;
static constexpr uint32_t ex_enable_w1 = (1u << 28) | 0x64u;
static constexpr uint32_t ex_set_view_matrix_float_w0 = (0x64u << 24) | 0x2Eu;
static constexpr uint32_t g_enddl = 0xB8000000;

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

// At func_8009989C's write of the viewport's G_ENDDL (end, its second word not yet written): returns where the end goes
// now, the list left as it was where there is no room or no slot.
extern "C" gpr r2_camera_view(uint8_t* rdram, gpr viewport, gpr end) {
    if (end + 0x18 > viewport + 0x48) {
        return end;
    }

    static std::unordered_map<uint32_t, uint32_t> slots;
    auto slot = slots.find((uint32_t)viewport);
    if (slot == slots.end()) {
        const uint32_t next = view_slots_start + (uint32_t)slots.size() * view_slot_size;
        if (next + view_slot_size > view_slots_end) {
            return end;
        }
        slot = slots.emplace((uint32_t)viewport, next).first;
    }

    // The view, rows r and translation t (row vectors, as RT64's matrices: a point p goes to p * r + t), in the list's
    // units; its inverse, the rows transposed (r's inverse, for a view that scales too) and -t times that.
    const gpr camera = MEM_W(0, (gpr)(int32_t)0x800CF2E0);
    if ((uint32_t)camera < 0x80000000u || (uint32_t)camera >= 0x80800000u) {
        return end;
    }
    float r[3][3], t[3];
    for (int i = 0; i < 3; i++) {
        t[i] = load_f32(rdram, camera + 0x10 + i * 4) * list_units_per_world_unit;
        for (int j = 0; j < 3; j++) {
            r[i][j] = load_f32(rdram, camera + 0x1C + i * 0xC + j * 4);
        }
    }
    const float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
        r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (!std::isfinite(det) || std::fabs(det) < 1e-6f) {
        return end;
    }
    float inv[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            const int i1 = (i + 1) % 3, i2 = (i + 2) % 3, j1 = (j + 1) % 3, j2 = (j + 2) % 3;
            inv[j][i] = (r[i1][j1] * r[i2][j2] - r[i1][j2] * r[i2][j1]) / det;
        }
    }

    const gpr matrix = (gpr)(int32_t)slot->second;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            store_f32(rdram, matrix + (i * 4 + j) * 4, inv[i][j]);
        }
        store_f32(rdram, matrix + (i * 4 + 3) * 4, 0.0f);
    }
    for (int j = 0; j < 3; j++) {
        store_f32(rdram, matrix + (12 + j) * 4, -(t[0] * inv[0][j] + t[1] * inv[1][j] + t[2] * inv[2][j]));
    }
    store_f32(rdram, matrix + 15 * 4, 1.0f);

    MEM_W(0x0, end) = (int32_t)ex_enable_w0;
    MEM_W(0x4, end) = (int32_t)ex_enable_w1;
    MEM_W(0x8, end) = (int32_t)ex_set_view_matrix_float_w0;
    MEM_W(0xC, end) = (int32_t)slot->second;
    MEM_W(0x10, end) = (int32_t)g_enddl;
    return end + 0x10;
}
