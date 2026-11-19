// The game's own checks (func_8008F86C; us.toml's hook calls this as it starts). The game stops on a failed check with
// a `break` (255) in that function, which some 22 places call, and the runtime turns the `break` into the program
// closing, with what it prints lost: the release is a windowed program with no console. So before the `break`, this
// says which check failed in a message box and on stderr: the address it was called from (the return address in ra)
// and the level the game was in (its name at 0x800E5D44), for a player's report to name. The program still closes
// after it, as it did: a check that failed leaves the game in a state its code doesn't expect to go on from.
#include <cstdio>
#include <string>

#include "recomp.h"
#include "ultramodern/ultramodern.hpp"

extern "C" void r2_game_check(uint8_t* rdram, recomp_context* ctx) {
    char level[17]{};
    for (int i = 0; i < 16; i++) {
        uint8_t c = MEM_BU(i, (gpr)(int32_t)0x800E5D44);
        if (c < 0x20 || c > 0x7E) break;
        level[i] = (char)c;
    }
    char message[320];
    snprintf(message, sizeof(message),
        "The game stopped at one of its own checks, so it has to close.\n\n"
        "Check: 0x%08X\nLevel: %s\n\n"
        "Please report it with these two lines and what you were doing.",
        (uint32_t)ctx->r31 - 8, level[0] ? level : "(none)");
    fprintf(stderr, "%s\n", message);
    fflush(stderr);
    ultramodern::error_handling::message_box(message);
}
