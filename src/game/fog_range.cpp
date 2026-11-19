// DIAGNOSTIC (not for merge): logs a fog light whose ranges func_8009138C asserts on, with the frames before it.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "recomp.h"

namespace {
struct Seen { int32_t vi; uint32_t light; float a20, a24, a28, a6c, f20, f22, f24, f26; };
Seen ring[64];
int at = 0;
}

extern "C" void r2_fog_range(uint8_t* rdram, recomp_context* ctx) {
    uint32_t light = (uint32_t)ctx->r17;
    auto f = [&](uint32_t off) { uint32_t w = MEM_W(off, (gpr)(int32_t)light); float v; memcpy(&v, &w, 4); return v; };
    Seen s{ MEM_W(0, (gpr)(int32_t)0x800C8C3C), light, f(0x20), f(0x24), f(0x28), f(0x6C),
            ctx->f20.fl, ctx->f22.fl, ctx->f24.fl, ctx->f26.fl };
    ring[at++ % 64] = s;
    if (ctx->f20.fl < ctx->f22.fl && ctx->f24.fl < ctx->f26.fl) return;
    fprintf(stderr, "fog range out of order (vi light +20 +24 +28 +6C | f20 f22 f24 f26), oldest first:\n");
    for (int i = 0; i < 64; i++) {
        const Seen& e = ring[(at + i) % 64];
        if (!e.light) continue;
        fprintf(stderr, "  %d %08X %g %g %g %g | %g %g %g %g\n", e.vi, e.light, e.a20, e.a24, e.a28, e.a6c, e.f20, e.f22, e.f24, e.f26);
    }
    fflush(stderr);
    if (!(ctx->f20.fl < ctx->f22.fl)) ctx->f22.fl = ctx->f20.fl + 1.0f;
    if (!(ctx->f24.fl < ctx->f26.fl)) ctx->f26.fl = ctx->f24.fl + 1.0f;
}
