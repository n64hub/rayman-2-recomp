// Patches to the game's code go in this folder, a file each; the fixes the recompiled code needs to run are hooks in
// us.toml. This one export lets a mod ask for this recompilation's patches (RECOMP_IMPORT) to require them.

#include "patches.h"

RECOMP_EXPORT u32 r2_patches_version(void) {
    return 1;
}
