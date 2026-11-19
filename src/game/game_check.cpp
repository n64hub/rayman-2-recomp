// The game's own checks (func_8008F86C; us.toml's hook calls this as it starts). The game stops on a failed check with
// a `break` (255) in that function, which some 22 places call, and the runtime turns the `break` into the program
// closing, with what it prints lost: the release is a windowed program with no console. So before the `break`, this
// says which check failed in a message box (which ultramodern also prints to stderr): the game function that called it
// and the level the game was in (its name at 0x800E5D44), for a player's report to name. The recompiled calls don't set
// ra, so the caller comes from the hook's host return address, which r2::game_function_at maps to the game function
// whose recompiled code holds it (null on the web, where it is left out). The program still closes after it, as it did:
// a check that failed leaves the game in a state its code doesn't expect to go on from.
#include <cstdio>

#include "recomp.h"
#include "ultramodern/ultramodern.hpp"
#include "rayman2.h"

extern "C" void r2_game_check(uint8_t* rdram, recomp_context* ctx, void* host_return) {
    char level[17]{};
    for (int i = 0; i < 16; i++) {
        uint8_t c = MEM_BU(i, (gpr)(int32_t)0x800E5D44);
        if (c < 0x20 || c > 0x7E) break;
        level[i] = (char)c;
    }
    uint32_t caller = host_return != nullptr ? r2::game_function_at(host_return) : 0;
    char check[24] = "(unknown)";
    if (caller != 0) {
        snprintf(check, sizeof(check), "func_%08X", caller);
    }
    char message[320];
    snprintf(message, sizeof(message),
        "The game stopped at one of its own checks, so it has to close.\n\n"
        "Check: %s\nLevel: %s\n\n"
        "Please report it with these two lines and what you were doing.",
        check, level[0] ? level : "(none)");
    ultramodern::error_handling::message_box(message);
}
