// Widescreen for what the game decides and draws for a 4:3 screen, which RT64's Aspect Ratio Expand (or Manual) widens
// the picture past: its culling (us.toml's hook in func_8009989C), its full-screen 2D rectangles (the hook in
// func_8009784C), a menu's backdrop (the hooks in func_80098D58), its sprites' clip to the screen (the hook in
// func_800976E0), its scene transition, drawn from the CPU (the hook in func_8008EF78), and its HUD, placed at a 4:3
// screen's corners (the hooks in func_800BC53C and the sprites' and text's drawers), each left alone at Original.
// The 2D rectangles' hook also joins a big image's tiles, which show their seams upscaled at any aspect.

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
// the one the texture wraps around to, and upscaled a big image drawn as tiles that do that showed a grid of thin dark
// lines (the title logo's tiles are joined below instead). An end within a texel of such an edge is pulled in to the
// last texel's centre.
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

// The title logo is a big image drawn as 32x32 tiles, each two textures: its colour (tile 0, RGBA16) and its mask (tile
// 1, I4, the alpha). A tile holds 30x30 texels of the image inside a ring of clear ones, and the game draws the tiles
// overlapping by a couple of pixels so that one tile's edge fades out as the next one's fades in. At a console's few
// pixels that hides the joins, but upscaled the two fading edges, drawn over each other, cover less than a solid texel
// (a thin dark line), and as the logo fades they add up (a lighter one). The image runs on from one tile's last texel
// inside the ring to the next one's first, so where two tiles meet each one's ring there is given the other's texels
// beside it, and RT64's filtering reads across the join as it does inside the image. Both keep the game's own
// coordinates and steps, and they meet at a single whole pixel, so nothing is drawn twice, where each reads only its
// own texture: RT64 puts a texel's centre on its whole coordinate less the tile's upper left (0.5 here), so a tile reads
// coordinates 0.5 to 31.5 without wrapping around to its far side. A side that meets no tile is cut back to there too,
// since past it the filtering reads the ring on the far side, which may hold a neighbour's texels. The one before is
// already in the frame's list (the list is sent once it is done), so its command is changed there: the first word
// holds its right and bottom edges. The rectangles drawn this frame are kept, the frame told by the count
// func_8008E944 keeps at 0x800C8C40.

static float rect_coord(uint8_t* rdram, gpr rect, int offset) {
    return (int16_t)MEM_HU(offset, rect) / 32.0f;
}

static float rect_step(uint8_t* rdram, gpr rect, int offset) {
    return (int16_t)MEM_HU(offset, rect) / 1024.0f;
}

// A logo tile's two textures in RDRAM: its colour (32x32 RGBA16) and its mask (32x32 I4).
struct TileTextures {
    gpr colour, mask;
};

// The textures of the logo tile about to be written at command, or zeros where the rectangle isn't one: the latest
// G_SETTIMG of each before it, back to the rectangle before, in the list or in a texture's load block that the list
// calls (G_DL, which F3DEX numbers 0x06, into a block that ends at G_ENDDL), and the G_SETTILE and G_SETTILESIZE that
// make tiles 0 and 1 the logo's (wrapped at 32 texels, from 0.5 to 32).
static TileTextures find_tile_textures(uint8_t* rdram, gpr command) {
    gpr colour = 0, mask = 0;
    bool tiles[4] = {};
    auto note = [&](gpr at) {
        const uint32_t w0 = MEM_W(0, at), w1 = MEM_W(4, at);
        if (w0 == 0xFD100000u && colour == 0 && (w1 >> 24) == 0x80) {
            colour = (gpr)(int32_t)w1;
        }
        else if (w0 == 0xFD900000u && mask == 0 && (w1 >> 24) == 0x80) {
            mask = (gpr)(int32_t)w1;
        }
        tiles[0] |= w0 == 0xF5101000u && w1 == 0x00014050u;
        tiles[1] |= w0 == 0xF5800500u && w1 == 0x01014050u;
        tiles[2] |= w0 == 0xF2002002u && w1 == 0x00080080u;
        tiles[3] |= w0 == 0xF2002002u && w1 == 0x01080080u;
    };
    for (int k = 1; k <= 48; k++) {
        const gpr at = command - 8 * k;
        const uint32_t w0 = MEM_W(0, at), w1 = MEM_W(4, at);
        if ((w0 >> 24) == 0xE4) {
            break;
        }
        if (w0 == 0x06000000u && (w1 >> 24) == 0x80) {
            const gpr block = (gpr)(int32_t)w1;
            int length = 0;
            while (length < 32 && ((uint32_t)MEM_W(8 * length, block) >> 24) != 0xB8) {
                length++;
            }
            for (int j = length - 1; j >= 0; j--) {
                note(block + 8 * j);
            }
        }
        else {
            note(at);
        }
    }
    if (colour == 0 || mask == 0 || !tiles[0] || !tiles[1] || !tiles[2] || !tiles[3]) {
        return {};
    }
    return { colour, mask };
}

// Copies a column (axis 0) or row (axis 1) of one tile's texels, colour and mask, over another's.
static void copy_texels(uint8_t* rdram, const TileTextures& from, int from_line, const TileTextures& to, int to_line,
    int axis) {
    for (int i = 0; i < 32; i++) {
        const int from_x = axis == 0 ? from_line : i, from_y = axis == 0 ? i : from_line;
        const int to_x = axis == 0 ? to_line : i, to_y = axis == 0 ? i : to_line;
        MEM_H(2 * (to_y * 32 + to_x), to.colour) = MEM_H(2 * (from_y * 32 + from_x), from.colour);
        const uint8_t texel = (MEM_BU(from_y * 16 + from_x / 2, from.mask) >> ((from_x & 1) ? 0 : 4)) & 0xF;
        const uint8_t pair = MEM_BU(to_y * 16 + to_x / 2, to.mask);
        MEM_B(to_y * 16 + to_x / 2, to.mask) = (int8_t)((to_x & 1) ? (pair & 0xF0) | texel : (pair & 0x0F) | (texel << 4));
    }
}

// Where (quarter pixels) a tile drawn from p, its coordinate c there and its step per pixel step, comes inside 0.5-31.5
// and where it leaves it.
static float tile_enters(float p, float c, float step) {
    return p + ((step > 0.0f ? 0.5f : 31.5f) - c) / step * 4.0f;
}

static float tile_leaves(float p, float c, float step) {
    return p + ((step > 0.0f ? 31.5f : 0.5f) - c) / step * 4.0f;
}

struct DrawnRect {
    gpr command;
    int32_t x0, y0, x1, y1; // as the game gave them, to tell its rows and columns
    int32_t start[2], end[2]; // as drawn, across and down
    float coord[2], step[2];
    TileTextures textures;
};
static DrawnRect drawn_rects[64];
static int drawn_rect_count = 0;
static int32_t drawn_rects_frame = -1;

static uint32_t rect_edges_word(const DrawnRect& rect) {
    return 0xE4000000u | ((uint32_t)(rect.end[0] & 0xFFF) << 12) | (uint32_t)(rect.end[1] & 0xFFF);
}

// Joins a logo tile (its textures) that starts at start (quarter pixels) on axis, its texture coordinate there coord
// and its step per pixel step, to before, which it overlaps there: returns where it starts now, and sets coord to its
// coordinate there.
static int32_t join_tiles(uint8_t* rdram, DrawnRect& before, const TileTextures& textures, int axis, int32_t start,
    int32_t end, float& coord, float step) {
    if ((step > 0.0f) != (before.step[axis] > 0.0f) || (uint32_t)MEM_W(0x0, before.command) != rect_edges_word(before)) {
        return start;
    }
    // The whole pixel nearest halfway between where this one comes inside its texture and where the one before leaves.
    const float enters = tile_enters(start, coord, step);
    const float leaves = std::min<float>(tile_leaves(before.start[axis], before.coord[axis], before.step[axis]),
        before.end[axis]);
    const int32_t first = std::max<int32_t>((int32_t)std::ceil(enters / 4.0f - 0.01f) * 4, before.start[axis] + 4);
    const int32_t last = std::min<int32_t>((int32_t)std::floor(leaves / 4.0f + 0.01f) * 4, end - 4);
    if (first > last) {
        return start;
    }
    const int32_t join = std::clamp<int32_t>(std::lround((enters + leaves) / 8.0f) * 4, first, last);
    // The one before ends at its texture's high edge where its coordinate grows, so its ring there is its last line (31)
    // and the texels beside it the line before (30); this one starts at the other edge.
    const int before_ring = step > 0.0f ? 31 : 0, ring = 31 - before_ring;
    const int before_inner = step > 0.0f ? 30 : 1, inner = 31 - before_inner;
    copy_texels(rdram, textures, inner, before.textures, before_ring, axis);
    copy_texels(rdram, before.textures, before_inner, textures, ring, axis);
    before.end[axis] = join;
    MEM_W(0x0, before.command) = (int32_t)rect_edges_word(before);
    coord += (join - start) / 4.0f * step;
    return join;
}

// The 2D drawer's scaled texture rectangles (us.toml's hook in func_8009784C calls this before one is written, to
// command). A rectangle over the whole screen (the fades) is drawn from 0 to a pixel short of its right and bottom
// edges, and RT64 stretches a rectangle across a wide picture only when it spans the screen, so a fade covered only the
// 4:3 middle: it is made to span it. The logo's tiles are joined (above), and other rectangles kept inside their
// textures. The rectangle is +0x0 x, +0x4 y, +0x8 right, +0xC bottom in quarter pixels, its texture coordinates at
// +0x16 and +0x1A (S10.5) and their steps per pixel at +0x1E and +0x22 (S5.10).
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

    const TileTextures textures = find_tile_textures(rdram, command);
    const float ds = rect_step(rdram, rect, 0x1E), dt = rect_step(rdram, rect, 0x22);
    if (textures.colour == 0 || ds == 0.0f || dt == 0.0f) {
        keep_inside_texture(rdram, rect, x1 - x0, 0x16, 0x1E);
        keep_inside_texture(rdram, rect, y1 - y0, 0x1A, 0x22);
        return;
    }

    const int32_t frame = MEM_W(0, (gpr)(int32_t)0x800C8C40);
    if (frame != drawn_rects_frame || (drawn_rect_count > 0 && command <= drawn_rects[drawn_rect_count - 1].command)) {
        drawn_rects_frame = frame;
        drawn_rect_count = 0;
    }
    DrawnRect drawn = { command, x0, y0, x1, y1, { x0, y0 }, { x1, y1 },
        { rect_coord(rdram, rect, 0x16), rect_coord(rdram, rect, 0x1A) }, { ds, dt }, textures };
    for (int i = 0; i < drawn_rect_count; i++) {
        DrawnRect& before = drawn_rects[i];
        if (before.y0 == y0 && before.y1 == y1 && before.x0 < x0 && x0 < before.x1 && before.x1 < x1) {
            drawn.start[0] = join_tiles(rdram, before, textures, 0, drawn.start[0], x1, drawn.coord[0], ds);
        }
        else if (before.x0 == x0 && before.x1 == x1 && before.y0 < y0 && y0 < before.y1 && before.y1 < y1) {
            drawn.start[1] = join_tiles(rdram, before, textures, 1, drawn.start[1], y1, drawn.coord[1], dt);
        }
    }
    // Its sides cut back to where it reads only its own texels, on whole pixels.
    for (int axis = 0; axis < 2; axis++) {
        const float enters = tile_enters(drawn.start[axis], drawn.coord[axis], drawn.step[axis]);
        if (enters > drawn.start[axis] + 0.01f) {
            const int32_t start = (int32_t)std::ceil(enters / 4.0f - 0.01f) * 4;
            drawn.coord[axis] += (start - drawn.start[axis]) / 4.0f * drawn.step[axis];
            drawn.start[axis] = start;
        }
        const float leaves = tile_leaves(drawn.start[axis], drawn.coord[axis], drawn.step[axis]);
        if (leaves < drawn.end[axis] - 0.01f) {
            drawn.end[axis] = (int32_t)std::floor(leaves / 4.0f + 0.01f) * 4;
        }
    }
    MEM_W(0x0, rect) = drawn.start[0];
    MEM_W(0x4, rect) = drawn.start[1];
    MEM_W(0x8, rect) = drawn.end[0];
    MEM_W(0xC, rect) = drawn.end[1];
    MEM_H(0x16, rect) = (int16_t)std::lround(drawn.coord[0] * 32.0f);
    MEM_H(0x1A, rect) = (int16_t)std::lround(drawn.coord[1] * 32.0f);
    if (drawn_rect_count < 64) {
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

// The sprites and particles (a lum's glow, the sparkles as Rayman lands): func_80092768 projects one on the CPU to a
// texture rectangle on the screen, and func_800976E0 (us.toml's hook at its start calls this) clips it to the 4:3
// screen before the 2D drawer, func_8009784C, writes it. RT64 draws such a rectangle where the 3D around it is, at its
// place in the wide picture's 4:3 middle, so a sprite was cut on a straight line at the 4:3 edges and one wholly past
// them was never drawn. A rectangle that reaches past them is clipped here to the wide picture's edges instead, and
// drawn with RT64's extended GBI: its texture rectangle as an extended one, whose left can be below 0 (G_TEXRECT's
// can't), under a scissor that reaches the window's edges, pushed before it and popped after, so nothing else draws
// under it. Returns whether this did (v0 then the list's end); otherwise the game's own clip runs, at Original and for
// a rectangle inside the screen. The rectangle is +0x0 x, +0x4 y, +0x8 right, +0xC bottom in quarter pixels, its
// texture coordinates at +0x14 and +0x18 and their steps per pixel at +0x1C and +0x20 (words); the screen's height is
// at +0x14 and its width at +0x1C (pixels). The extended commands are rt64_extended_gbi.h's, for its opcode 0x64
// (gEXEnable registers it).
extern "C" void func_8009784C(uint8_t* rdram, recomp_context* ctx);

static constexpr uint32_t ex_command(uint32_t id) {
    return (0x64u << 24) | id;
}
static constexpr uint32_t ex_origin_none = 0x800;

extern "C" int r2_wide_sprite(uint8_t* rdram, recomp_context* ctx) {
    const gpr list = ctx->r4, rect = ctx->r5, screen = ctx->r7;
    const int32_t width = MEM_W(0x1C, screen) * 4, height = MEM_W(0x14, screen) * 4;
    int32_t x0 = MEM_W(0x0, rect), y0 = MEM_W(0x4, rect), x1 = MEM_W(0x8, rect), y1 = MEM_W(0xC, rect);
    const float k = widescreen_scale();
    if (k <= 1.0f || (x0 >= 0 && x1 <= width)) {
        return 0;
    }
    // Rounded in, so the scissor never reaches past the picture.
    const int32_t left = -(int32_t)std::floor((k - 1.0f) * width * 0.5f), right = width - left;
    if (x1 <= left || y1 <= 0 || x0 > right || y0 > height) {
        ctx->r2 = list;
        return 1;
    }
    if (x0 < left) {
        MEM_W(0x14, rect) = MEM_W(0x14, rect) + (((left - x0) * MEM_W(0x1C, rect)) >> 7);
        MEM_W(0x0, rect) = x0 = left;
    }
    if (y0 < 0) {
        MEM_W(0x18, rect) = MEM_W(0x18, rect) + ((-y0 * MEM_W(0x20, rect)) >> 7);
        MEM_W(0x4, rect) = y0 = 0;
    }
    if (x1 > right) {
        MEM_W(0x8, rect) = x1 = right;
    }
    if (y1 > height) {
        MEM_W(0xC, rect) = y1 = height;
    }

    gpr at = list;
    auto write = [&](uint32_t w0, uint32_t w1) {
        MEM_W(0, at) = (int32_t)w0;
        MEM_W(4, at) = (int32_t)w1;
        at += 8;
    };
    write(0x00525464u, (1u << 28) | 0x64u); // gEXEnable
    write(ex_command(0x17), 0); // gEXPushScissor
    write(ex_command(0x05), (ex_origin_none << 2) | (ex_origin_none << 14)); // gEXSetScissor, G_SC_NON_INTERLACE
    write(((uint32_t)left & 0xFFFF) << 16, (((uint32_t)right & 0xFFFF) << 16) | ((uint32_t)height & 0xFFFF));

    ctx->r4 = at;
    func_8009784C(rdram, ctx);
    const gpr end = ctx->r2;

    // The texture rectangle it wrote (G_TEXRECT, then G_RDPHALF_1 with its coordinates and G_RDPHALF_2 with their
    // steps) as gEXTextureRectangle, at the rectangle's place as the drawer left it.
    for (gpr command = at; command + 24 <= end; command += 8) {
        if (((uint32_t)MEM_W(0, command) >> 24) != 0xE4 || ((uint32_t)MEM_W(8, command) >> 24) != 0xB4 ||
            ((uint32_t)MEM_W(16, command) >> 24) != 0xB3) {
            continue;
        }
        const uint32_t tile = ((uint32_t)MEM_W(4, command) >> 24) & 7;
        const int32_t coords = MEM_W(12, command), steps = MEM_W(20, command);
        x0 = MEM_W(0x0, rect), y0 = MEM_W(0x4, rect), x1 = MEM_W(0x8, rect), y1 = MEM_W(0xC, rect);
        MEM_W(0, command) = (int32_t)ex_command(0x02);
        MEM_W(4, command) = (int32_t)(tile | (ex_origin_none << 3) | (ex_origin_none << 15));
        MEM_W(8, command) = (int32_t)((((uint32_t)x0 & 0xFFFF) << 16) | ((uint32_t)y0 & 0xFFFF));
        MEM_W(12, command) = (int32_t)((((uint32_t)x1 & 0xFFFF) << 16) | ((uint32_t)y1 & 0xFFFF));
        MEM_W(16, command) = coords;
        MEM_W(20, command) = steps;
        break;
    }
    at = end;
    write(ex_command(0x18), 0); // gEXPopScissor
    ctx->r2 = at;
    return 1;
}

// The HUD: func_800BC53C draws it each frame, its bars (health, air) and then its icons (Rayman's face, the lums' and
// cages' icons, the column of hints at the right, a race's timer) through func_800BC460, and then the text on screen
// through func_800C2844 (the counters beside those icons, a dialogue, the intro's story). Each bar and icon is a sprite
// that func_80096EAC places in percent of a 4:3 screen and draws with the 2D drawer, func_8009784C, so in a wide
// picture the HUD sat at the 4:3 corners. With RT64's extended GBI each one is drawn from the wide picture's edge
// instead, as far from it as it was from the 4:3 screen's: one in the top half that starts in the screen's left third
// and ends left of its right third from the left edge, one that ends in the right third and starts right of the left
// third from the right edge (offset back by the screen's width), under a scissor that reaches the picture's edges, and
// the alignment and scissor put back after it. A dialogue's speaker (Murfy's face) is an icon too, in the bottom left
// beside the dialogue's box, which stays in the middle, and so does it. Text moves only with an icon it sits beside and on
// its side of the screen, so a counter goes with its icon and everything else (a dialogue, the story, a countdown)
// stays in the middle: a text entry (func_800C2F48 draws one, a sprite a letter) is given room for those commands
// before it, filled in once its letters say where it is. us.toml's hooks call these, none at Original.
struct HudRect {
    float x0, y0, x1, y1; // percent of the screen
    uint32_t origin;
};
static constexpr uint32_t ex_origin_left = 0x0, ex_origin_right = 0x400;
static constexpr gpr display_list_end = (gpr)(int32_t)0x800C8C2C;
static constexpr int hud_align_commands = 6;
static bool hud_pass = false, hud_icons = false, hud_sprite_aligned = false;
static HudRect hud_anchored[32];
static int hud_anchored_count = 0;
static gpr hud_text_room = 0;
static HudRect hud_text;

// Writes, from at, the commands that draw what follows from origin under a scissor out to the picture's edges; returns
// where they end.
static gpr write_hud_align(uint8_t* rdram, gpr at, uint32_t origin) {
    const int32_t width = MEM_W(0, (gpr)(int32_t)0x8001A110) * 4, height = MEM_W(0, (gpr)(int32_t)0x8001A114) * 4;
    // Rounded in, so the scissor never reaches past the picture.
    const int32_t left = -(int32_t)std::floor((widescreen_scale() - 1.0f) * width * 0.5f), right = width - left;
    const int32_t offset = origin == ex_origin_right ? -width : 0;
    auto write = [&](uint32_t w0, uint32_t w1) {
        MEM_W(0, at) = (int32_t)w0;
        MEM_W(4, at) = (int32_t)w1;
        at += 8;
    };
    write(0x00525464u, (1u << 28) | 0x64u); // gEXEnable
    write(ex_command(0x17), 0); // gEXPushScissor
    write(ex_command(0x05), (ex_origin_none << 2) | (ex_origin_none << 14)); // gEXSetScissor, G_SC_NON_INTERLACE
    write(((uint32_t)left & 0xFFFF) << 16, (((uint32_t)right & 0xFFFF) << 16) | ((uint32_t)height & 0xFFFF));
    write(ex_command(0x06), origin | (origin << 12)); // gEXSetRectAlign
    write(((uint32_t)offset & 0xFFFF) << 16, ((uint32_t)offset & 0xFFFF) << 16);
    return at;
}

// Writes, from at, the commands that put the alignment and the scissor back; returns where they end.
static gpr write_hud_unalign(uint8_t* rdram, gpr at) {
    MEM_W(0, at) = (int32_t)ex_command(0x06); // gEXSetRectAlign
    MEM_W(4, at) = (int32_t)(ex_origin_none | (ex_origin_none << 12));
    MEM_W(8, at) = 0;
    MEM_W(12, at) = 0;
    MEM_W(16, at) = (int32_t)ex_command(0x18); // gEXPopScissor
    MEM_W(20, at) = 0;
    return at + 24;
}

extern "C" void r2_hud_start() {
    hud_pass = hud_icons = widescreen_scale() > 1.0f;
    hud_anchored_count = 0;
}

extern "C" void r2_hud_icons_end() {
    hud_icons = false;
}

extern "C" void r2_hud_end() {
    hud_pass = hud_icons = false;
    hud_anchored_count = 0;
}

// Before func_80096EAC has the 2D drawer write a sprite at list (its rectangle in quarter pixels as r2_fix_rect's, the
// screen's width at +0x1C and its height at +0x14 in pixels): returns where the drawer writes it.
extern "C" gpr r2_hud_sprite(uint8_t* rdram, gpr list, gpr rect, gpr screen) {
    if (!hud_pass) {
        return list;
    }
    const float width = MEM_W(0x1C, screen) * 4 / 100.0f, height = MEM_W(0x14, screen) * 4 / 100.0f;
    if (width <= 0.0f || height <= 0.0f) {
        return list;
    }
    HudRect sprite = { MEM_W(0x0, rect) / width, MEM_W(0x4, rect) / height, MEM_W(0x8, rect) / width,
        MEM_W(0xC, rect) / height, ex_origin_none };
    if (hud_text_room != 0) {
        hud_text.x0 = std::min(hud_text.x0, sprite.x0);
        hud_text.y0 = std::min(hud_text.y0, sprite.y0);
        hud_text.x1 = std::max(hud_text.x1, sprite.x1);
        hud_text.y1 = std::max(hud_text.y1, sprite.y1);
        return list;
    }
    if (!hud_icons) {
        return list;
    }
    if (sprite.x0 < 100.0f / 3.0f && sprite.x1 < 200.0f / 3.0f && sprite.y1 <= 50.0f) {
        sprite.origin = ex_origin_left;
    }
    else if (sprite.x1 > 200.0f / 3.0f && sprite.x0 > 100.0f / 3.0f) {
        sprite.origin = ex_origin_right;
    }
    else {
        return list;
    }
    if (hud_anchored_count < 32) {
        hud_anchored[hud_anchored_count++] = sprite;
    }
    hud_sprite_aligned = true;
    return write_hud_align(rdram, list, sprite.origin);
}

// After it, the list's end in end: returns it past the commands that put the alignment back.
extern "C" gpr r2_hud_sprite_end(uint8_t* rdram, gpr end) {
    if (!hud_sprite_aligned) {
        return end;
    }
    hud_sprite_aligned = false;
    return write_hud_unalign(rdram, end);
}

// Before func_800C2844 has func_800C2F48 draw a text entry, once some icon has moved: leaves room in the list (the
// list's end is at 0x800C8C2C) for the alignment's commands, G_SPNOOPs until the entry's letters are drawn.
extern "C" void r2_hud_text_start(uint8_t* rdram) {
    hud_text_room = 0;
    if (!hud_pass || hud_anchored_count == 0) {
        return;
    }
    hud_text_room = (gpr)MEM_W(0, display_list_end);
    for (int i = 0; i < hud_align_commands * 2; i++) {
        MEM_W(i * 4, hud_text_room) = 0;
    }
    MEM_W(0, display_list_end) = (int32_t)(hud_text_room + hud_align_commands * 8);
    hud_text = { 1e9f, 1e9f, -1e9f, -1e9f, ex_origin_none };
}

// After it: an entry beside a moved icon (within 6% of the screen's width across and 3% of its height down) and wholly
// on its half of the screen moves with it.
extern "C" void r2_hud_text_end(uint8_t* rdram) {
    const gpr room = hud_text_room;
    hud_text_room = 0;
    if (room == 0 || hud_text.x0 > hud_text.x1) {
        return;
    }
    for (int i = 0; i < hud_anchored_count; i++) {
        const HudRect& icon = hud_anchored[i];
        const bool side = icon.origin == ex_origin_left ? hud_text.x1 < 50.0f : hud_text.x0 > 50.0f;
        if (side && hud_text.x0 - 6.0f < icon.x1 && icon.x0 < hud_text.x1 + 6.0f && hud_text.y0 - 3.0f < icon.y1 &&
            icon.y0 < hud_text.y1 + 3.0f) {
            write_hud_align(rdram, room, icon.origin);
            const gpr end = (gpr)MEM_W(0, display_list_end);
            MEM_W(0, display_list_end) = (int32_t)write_hud_unalign(rdram, end);
            return;
        }
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
