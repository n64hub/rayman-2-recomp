// The scene transition's start (func_800FADC8; us.toml's hook calls this as it loads the transition's map). The map is
// a file as big as half a framebuffer, loaded into the framebuffer the game drew the frame before last into, which a
// console no longer shows by then: the last frame's task has ended and the VI has swapped to its framebuffer. The
// recomp's CPU gets there in the same retrace, while the map's framebuffer is still the one shown, and RT64 shows what
// the CPU writes into a shown framebuffer (RT64_STRETCH_CPU_CHANGES, for the transition's own passes), so for a frame
// the screen showed the map's bytes as pixels. So the load waits for the retrace that swaps that framebuffer out, three
// at most (the scheduler thread counts them in 0x800C8C3C).
#include <cstdint>

#include "recomp.h"

extern "C" int32_t osViGetCurrentFramebuffer();
extern "C" void yield_self(uint8_t* rdram);

extern "C" void r2_wait_framebuffer_hidden(uint8_t* rdram, gpr framebuffer) {
    const int32_t start = MEM_W(0, (gpr)(int32_t)0x800C8C3C);
    while (((uint32_t)osViGetCurrentFramebuffer() & 0x1FFFFFFF) == ((uint32_t)framebuffer & 0x1FFFFFFF) &&
           MEM_W(0, (gpr)(int32_t)0x800C8C3C) - start < 3) {
        yield_self(rdram);
    }
}
