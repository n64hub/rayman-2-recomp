// A scene's fog (func_8009138C; us.toml's hook calls this before its checks). The function turns a fog light's near
// and far distances (f20, f22) and its fog's start and end (f24, f26) into the RSP's fog multiplier and offset, and
// stops the game with its assertion (func_8008F86C, a `break`) when far isn't past near or end isn't past start. A
// scene's script can set all four for the whole scene (opcodes 0x19 and 0x1A), and some pull the end in towards zero
// by steps, so that the scene fogs over: one step too far, in a slow frame, put it past the start (end 6, then -156
// with start -108), and the runtime turns the `break` into the program closing. Here, out of order, far and end are
// put just past near and start, which draws the scene fogged over, as the script was taking it.
#include "recomp.h"

extern "C" void r2_fog_range(uint8_t* rdram, recomp_context* ctx) {
    if (!(ctx->f20.fl < ctx->f22.fl)) ctx->f22.fl = ctx->f20.fl + 1.0f;
    if (!(ctx->f24.fl < ctx->f26.fl)) ctx->f26.fl = ctx->f24.fl + 1.0f;
}
