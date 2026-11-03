// Widescreen for what the game decides and draws for a 4:3 screen, which RT64's Aspect Ratio Expand (or Manual) widens
// the picture past: its culling (us.toml's hook in func_8009989C), its full-screen 2D rectangles (the hook in
// func_8009784C), a menu's backdrop (the hooks in func_80098D58) and its scene transition, drawn from the CPU (the hook
// in func_8008EF78), each left alone at Original. The 2D rectangles' hook also joins a big image's tiles, which show
// their seams upscaled at any aspect.

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

// The game decides what to draw by its camera's planes, which it builds for 4:3: an object or a piece of the level past
// them was never drawn, so at the wide picture's sides things popped in and out. This widens the camera's left and
// right planes to the window's aspect ratio as RT64 widens the projection.
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

// Keeps a scaled rectangle's texture coordinates inside its texture. RT64 puts a texel's centre on its whole coordinate,
// so coordinates that run out to a power-of-two edge (32.0 for a 32-texel tile) blend the last texel half and half with
// the one the texture wraps around to, and upscaled a big image drawn as tiles that do that (the title logo) showed a
// grid of thin dark lines. An end within a texel of such an edge is pulled in to the last texel's centre.
static void keep_inside_texture(uint8_t* rdram, gpr rect, int length, int coord_offset, int step_offset) {
    int16_t coord = (int16_t)MEM_HU(coord_offset, rect), step = (int16_t)MEM_HU(step_offset, rect);
    if (length <= 0 || step == 0 || step == 1024 || step == -1024) {
        return;
    }
    float start = coord / 32.0f, end = start + (length / 4.0f) * (step / 1024.0f);
    float lo = std::min(start, end), hi = std::max(start, end);
    float edge = 8.0f;
    while (edge < hi - 0.001f) {
        edge *= 2.0f;
    }
    if (lo < 0.0f || hi <= edge - 1.0f || hi > edge + 0.01f || lo >= edge - 1.0f) {
        return;
    }
    if (start > end) {
        start = edge - 1.0f;
        MEM_H(coord_offset, rect) = (int16_t)std::lround(start * 32.0f);
    }
    else {
        end = edge - 1.0f;
    }
    MEM_H(step_offset, rect) = (int16_t)std::lround((end - start) / (length / 4.0f) * 1024.0f);
}

// The title logo's tiles are 32x32 textures whose outermost ring of texels is clear (its mask, the alpha, is 0 there),
// drawn overlapping by a couple of pixels so that one tile's edge fades out as the next one's fades in. At a console's
// few pixels that hides the joins, but upscaled the two fading edges, drawn over each other, cover less than one solid
// texel (a thin dark line), and as the logo fades they add up (a lighter one). So each tile in a row or column of them
// meets the one before at a single place, both of them solid there: the one before ends there at its last solid texel
// (its step stretched by a percent or two) and this one starts there at its first. The one before is already in the
// frame's list (the list is sent once it is done), so its command is changed there: the first word holds its right
// and bottom edges and the sixth its steps. The rectangles drawn this frame are kept, the frame told by the count
// func_8008E944 keeps at 0x800C8C40.

// A tile's last solid texel at a side whose coordinate is within a texel of its start (0, so 1) or of a power-of-two
// edge (so the edge less 2), or NAN where it is no tile's side.
static float solid_texel(float coord) {
    if (coord >= -0.01f && coord <= 1.0f) {
        return 1.5f;
    }
    for (float edge = 8.0f; edge <= 1024.0f; edge *= 2.0f) {
        if (coord >= edge - 1.0f && coord <= edge + 0.01f) {
            return edge - 1.5f;
        }
    }
    return NAN;
}

static float rect_coord(uint8_t* rdram, gpr rect, int offset) {
    return (int16_t)MEM_HU(offset, rect) / 32.0f;
}

static float rect_step(uint8_t* rdram, gpr rect, int offset) {
    return (int16_t)MEM_HU(offset, rect) / 1024.0f;
}

struct DrawnRect {
    gpr command;
    int32_t x0, y0, x1, y1; // as the game gave them, to tell its rows and columns
    int32_t start[2], end[2]; // as drawn, across and down
    float coord[2], step[2];
    float end_solid[2]; // solid_texel() at its right and bottom
};
static DrawnRect drawn_rects[64];
static int drawn_rect_count = 0;
static int32_t drawn_rects_frame = -1;

static uint32_t rect_edges_word(const DrawnRect& rect) {
    return 0xE4000000u | ((uint32_t)(rect.end[0] & 0xFFF) << 12) | (uint32_t)(rect.end[1] & 0xFFF);
}

static uint32_t rect_steps_word(const DrawnRect& rect) {
    return ((uint32_t)(uint16_t)(int16_t)std::lround(rect.step[0] * 1024.0f) << 16) |
        (uint32_t)(uint16_t)(int16_t)std::lround(rect.step[1] * 1024.0f);
}

// Joins a rectangle that starts at start (quarter pixels) on axis, its texture coordinate there coord and its step per
// pixel step, to before, which it overlaps there: returns where it starts now, and sets coord to its coordinate there.
static int32_t join_tiles(uint8_t* rdram, DrawnRect& before, int axis, int32_t start, int32_t end, float& coord,
    float step) {
    const float solid = solid_texel(coord), before_solid = before.end_solid[axis];
    if (std::isnan(solid) || std::isnan(before_solid) || step == 0.0f || before.step[axis] == 0.0f) {
        return start;
    }
    if ((uint32_t)MEM_W(0x0, before.command) != rect_edges_word(before) ||
        (uint32_t)MEM_W(0x14, before.command) != rect_steps_word(before)) {
        return start;
    }
    // Where each reaches its solid texel as drawn, and the join on the whole pixel halfway between.
    const float before_at = before.start[axis] + (before_solid - before.coord[axis]) / before.step[axis] * 4.0f;
    const float at = start + (solid - coord) / step * 4.0f;
    const int32_t join = std::clamp<int32_t>(std::lround((before_at + at) * 0.125f) * 4,
        std::max(start, before.start[axis] + 4), std::min(before.end[axis], end - 4));
    const float before_step = (before_solid - before.coord[axis]) / ((join - before.start[axis]) / 4.0f);
    if (before_step / before.step[axis] < 0.95f || before_step / before.step[axis] > 1.05f ||
        std::abs(solid - (coord + (join - start) / 4.0f * step)) > 1.5f) {
        return start;
    }
    before.end[axis] = join;
    before.step[axis] = before_step;
    MEM_W(0x0, before.command) = (int32_t)rect_edges_word(before);
    MEM_W(0x14, before.command) = (int32_t)rect_steps_word(before);
    coord = solid;
    return join;
}

// The 2D drawer's scaled texture rectangles (us.toml's hook in func_8009784C calls this before one is written, to
// command). A rectangle over the whole screen (the fades) is drawn from 0 to a pixel short of its right and bottom
// edges, and RT64 stretches a rectangle across a wide picture only when it spans the screen, so a fade covered only the
// 4:3 middle: it is made to span it. A big image's tiles are joined (above) and kept inside their textures. The
// rectangle is +0x0 x, +0x4 y, +0x8 right, +0xC bottom in quarter pixels, its texture coordinates at +0x16 and +0x1A
// (S10.5) and their steps per pixel at +0x1E and +0x22 (S5.10).
extern "C" void r2_fix_rect(uint8_t* rdram, gpr rect, gpr command) {
    const int32_t screen_width = MEM_W(0, (gpr)(int32_t)0x8001A110) * 4;
    const int32_t screen_height = MEM_W(0, (gpr)(int32_t)0x8001A114) * 4;
    int32_t x0 = MEM_W(0x0, rect), y0 = MEM_W(0x4, rect), x1 = MEM_W(0x8, rect), y1 = MEM_W(0xC, rect);
    if (x0 <= 0 && x1 >= screen_width - 4 && x1 < screen_width && y0 <= 0 && y1 >= screen_height - 4) {
        MEM_W(0x8, rect) = x1 = screen_width;
        if (y1 < screen_height) {
            MEM_W(0xC, rect) = y1 = screen_height;
        }
    }

    const int32_t frame = MEM_W(0, (gpr)(int32_t)0x800C8C40);
    if (frame != drawn_rects_frame || (drawn_rect_count > 0 && command <= drawn_rects[drawn_rect_count - 1].command)) {
        drawn_rects_frame = frame;
        drawn_rect_count = 0;
    }
    float s = rect_coord(rdram, rect, 0x16), t = rect_coord(rdram, rect, 0x1A);
    const float ds = rect_step(rdram, rect, 0x1E), dt = rect_step(rdram, rect, 0x22);
    DrawnRect drawn = { command, x0, y0, x1, y1, { x0, y0 }, { x1, y1 }, {}, {},
        { solid_texel(s + (x1 - x0) / 4.0f * ds), solid_texel(t + (y1 - y0) / 4.0f * dt) } };
    int32_t start_x = x0, start_y = y0;
    for (int i = 0; i < drawn_rect_count; i++) {
        DrawnRect& before = drawn_rects[i];
        if (before.y0 == y0 && before.y1 == y1 && before.x0 < x0 && x0 < before.x1 && before.x1 < x1) {
            start_x = join_tiles(rdram, before, 0, start_x, x1, s, ds);
        }
        else if (before.x0 == x0 && before.x1 == x1 && before.y0 < y0 && y0 < before.y1 && before.y1 < y1) {
            start_y = join_tiles(rdram, before, 1, start_y, y1, t, dt);
        }
    }
    if (start_x != x0) {
        MEM_W(0x0, rect) = start_x;
        MEM_H(0x16, rect) = (int16_t)std::lround(s * 32.0f);
    }
    if (start_y != y0) {
        MEM_W(0x4, rect) = start_y;
        MEM_H(0x1A, rect) = (int16_t)std::lround(t * 32.0f);
    }

    keep_inside_texture(rdram, rect, x1 - start_x, 0x16, 0x1E);
    keep_inside_texture(rdram, rect, y1 - start_y, 0x1A, 0x22);
    if (drawn_rect_count < 64) {
        drawn.start[0] = start_x;
        drawn.start[1] = start_y;
        drawn.coord[0] = rect_coord(rdram, rect, 0x16);
        drawn.coord[1] = rect_coord(rdram, rect, 0x1A);
        drawn.step[0] = rect_step(rdram, rect, 0x1E);
        drawn.step[1] = rect_step(rdram, rect, 0x22);
        drawn_rects[drawn_rect_count++] = drawn;
    }
}

// A menu's backdrop (the pause menu's): func_80098D58 animates the colours of a gradient, a mesh of 5 by 6 vertices at
// 0x800E53B8 drawn under an orthographic projection a little past a 4:3 screen's edges. RT64 keeps an orthographic
// projection at 4:3, so in a wide picture the sides showed the swirl over black, darker than the middle. Once the
// colours are made, r2_widen_backdrop spreads the vertices' x about the mesh's middle by k, and the next frame's colours
// are made from the x they had (r2_backdrop_x), so they stay a console's.
static constexpr gpr backdrop_vertices = (gpr)(int32_t)0x800E53B8;
static constexpr int backdrop_vertex_count = 30;
static int16_t backdrop_x[backdrop_vertex_count], backdrop_written_x[backdrop_vertex_count];
static bool backdrop_widened[backdrop_vertex_count];

// Whether vertex i holds the x this wrote, not one the game has written since.
static bool backdrop_vertex_widened(uint8_t* rdram, int i) {
    return backdrop_widened[i] && MEM_H(i * 0x10, backdrop_vertices) == backdrop_written_x[i];
}

extern "C" gpr r2_backdrop_x(uint8_t* rdram, gpr vertex, gpr x) {
    const int64_t i = ((int64_t)vertex - (int64_t)backdrop_vertices) / 0x10;
    if (i < 0 || i >= backdrop_vertex_count || !backdrop_vertex_widened(rdram, (int)i)) {
        return x;
    }
    return (gpr)(int64_t)backdrop_x[i];
}

extern "C" void r2_widen_backdrop(uint8_t* rdram) {
    const float k = widescreen_scale();
    int lo = INT16_MAX, hi = INT16_MIN;
    for (int i = 0; i < backdrop_vertex_count; i++) {
        if (!backdrop_vertex_widened(rdram, i)) {
            backdrop_x[i] = MEM_H(i * 0x10, backdrop_vertices);
        }
        lo = std::min<int>(lo, backdrop_x[i]);
        hi = std::max<int>(hi, backdrop_x[i]);
    }
    const float middle = (lo + hi) * 0.5f;
    for (int i = 0; i < backdrop_vertex_count; i++) {
        const long x = k <= 1.0f ? backdrop_x[i] : std::lround(middle + (backdrop_x[i] - middle) * k);
        backdrop_written_x[i] = (int16_t)std::clamp<long>(x, INT16_MIN, INT16_MAX);
        MEM_H(i * 0x10, backdrop_vertices) = backdrop_written_x[i];
        backdrop_widened[i] = true;
    }
}

// The 4:3 column that a picture's column x of width shows once RT64 stretches it k times across the wide picture:
// the middle shows the 4:3 picture, and the sides mirror it at its edges.
static int stretched_column(int x, int width, float k) {
    float v = (x + 0.5f) * k - width * (k - 1.0f) * 0.5f;
    if (v < 0.0f) {
        v = -v;
    }
    else if (v >= width) {
        v = 2.0f * width - v;
    }
    return std::clamp(int(v), 0, width - 1);
}

// The transition between scenes (func_8008EF78's thread, us.toml's hook calls this): while the game draws nothing, the
// thread blacks out the shown framebuffer from the CPU, a pass each retrace, each pass the pixel pairs whose byte in a
// map (the other framebuffer, a byte a pair) equals that pass's level, so the black spreads in blotches. RT64 shows
// what the CPU writes in the 4:3 middle of a wide picture, so the sides stayed as they were, cut off on a straight
// line. RT64 stretches it across the picture for this game (main.cpp), and this pass writes the blotches squeezed
// in by as much, so they spread from the map, mirrored at its edges, over the whole picture. Returns the word count
// left for the game's own pass: none once this has done it.
extern "C" gpr r2_wipe_pass(uint8_t* rdram, gpr map, gpr fb, gpr level, gpr words) {
    const float k = widescreen_scale();
    if (k <= 1.0f) {
        return words;
    }
    const int width = MEM_W(0, (gpr)(int32_t)0x8001A110), height = MEM_W(0, (gpr)(int32_t)0x8001A114);
    if (width <= 0 || height <= 0 || (int64_t)width * height / 8 != (int64_t)words) {
        return words;
    }
    static int columns[1024];
    if (width > 1024) {
        return words;
    }
    for (int x = 0; x < width; x++) {
        columns[x] = stretched_column(x, width, k);
    }
    const int8_t match = (int8_t)level;
    for (int y = 0; y < height; y++) {
        const int32_t row = y * width;
        for (int x = 0; x < width; x++) {
            if (MEM_B((row + columns[x]) / 2, map) == match) {
                MEM_H((row + x) * 2, fb) = 0;
            }
        }
    }
    return 0;
}
