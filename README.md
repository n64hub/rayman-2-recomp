# Rayman 2: Recompiled

> **Made with AI.** Almost all of the code, documentation and tests in this repo were written by AI agents
> (Claude), directed and play-tested by a person. None of it is offered to the upstream projects it builds on, which
> don't take AI-written code.

A native PC port of the N64 *Rayman 2: The Great Escape* (USA), made by static recompilation
([N64Recomp](https://github.com/N64Recomp/N64Recomp) + [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) +
[RT64](https://github.com/rt64/rt64), with [RecompFrontend](https://github.com/N64Recomp/RecompFrontend)'s launcher
and menus): the game's own MIPS code translated to C and run natively, with RT64 drawing it. There is no
decompilation of this game: its functions are found in the ROM's code by `tools/gen_syms.py`. It is not an emulator.

The repo holds no ROM, no asset from the game and no data extracted from it: you supply your own ROM, and nothing in
git or CI is made from one except the function names, addresses and sizes in `syms/`.

**Status:** released for Windows and Linux (`releases/`). The desktop build boots through the logos and the intro
story, plays a new game into the Woods of Light, and loads and draws every level of the game's table, the pause menu
and the world map's save prompt with them, with sound, Controller Pak saves, and keyboard and gamepad input. The game
hasn't been played from start to end yet. The same game plays in a browser through WebGPU, at
[n64hub.com/rayman-2](https://n64hub.com/rayman-2/) (`docs/web.md`).

## The ROM

*Rayman 2: The Great Escape* (USA), as a big-endian `.z64` of 32 MiB with SHA-1
`50558356b059ad3fbaf5fe95380512b9dceaaf52`. Other regions and revisions are not supported.

## Building

You need that ROM at `roms/rayman2.us.z64`. On Windows: LLVM's clang-cl, CMake 3.31 and Ninja, and a clang
with a MIPS backend plus ld.lld for `patches/`. On Linux: `clang lld libsdl2-dev libgtk-3-dev libfreetype-dev`, CMake
3.31 and Ninja.

From the repo's root in a bash shell:

```sh
git -c core.longpaths=true submodule update --init --recursive   # paths past Windows' 260 characters
bash tools/recompile.sh -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl   # the game's code as C
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl \
      -DPATCHES_C_COMPILER=<MIPS clang> -DPATCHES_LD=<ld.lld>
ninja -C build Rayman2Recompiled
```

With [ccache](https://ccache.dev) on `PATH` every compile goes through it, so a new build dir or another checkout
of the same code compiles only what changed since the last build on that computer (CMakeLists.txt says how).

On Linux the same lines take `clang` and `clang++` in place of `clang-cl`, and the system's `clang` and `ld.lld` for
`patches/`. `tools/recompile.sh` builds N64Recomp from the submodule the first time, then translates the game. Run
`build/Rayman2Recompiled` (`.exe` on Windows) from the repo's root, pick the ROM with Load ROM, then Start Game.
`tools/release.sh`'s header says how a release is built.

`syms/` is written from the ROM by `tools/gen_syms.py` (its header says how; it needs spimdisasm and n64sym's
signatures). Only a change to how it finds functions needs it again.

## License

GPL-3.0 (`LICENSE`), as the recompilations it follows.
