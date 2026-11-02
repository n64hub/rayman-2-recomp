// libultra functions the game links that N64Recomp leaves out (its ignored_funcs) and the runtime doesn't implement,
// so each call to one in RecompiledFuncs/ comes here.
#include <cstdint>

#include "recomp.h"

#include <cstdio>

// The thread queue, called by the interrupt-side message send of the development board's GIO and debugger code
// (0x80013800-0x80013E90): a cartridge has no such board, so the calls never come. Each says so once if one does.
static void unexpected(const char* name) {
    static bool said = false;
    if (!said) {
        said = true;
        fprintf(stderr, "Rayman 2: unexpected call to %s\n", name);
    }
}

extern "C" void __osPopThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    unexpected("__osPopThread");
    ctx->r2 = 0;
}

extern "C" void __osEnqueueThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    unexpected("__osEnqueueThread");
}

// The Controller Pak's bank and RAM, for the game's own copies of libultra's page functions (__osPfsDeclearPage,
// __osPfsReleasePages). Only its osPfsAllocateFile and osPfsDeleteFile call those, and src/game/controller_pak.cpp
// replaces both, so the calls never come; were one to, it would find no Pak (PFS_ERR_NOPACK).
constexpr int32_t PFS_ERR_NOPACK = 1;

extern "C" void __osPfsSelectBank_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}
