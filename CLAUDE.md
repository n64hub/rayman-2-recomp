# CLAUDE.md

A static recompilation of the N64 *Rayman 2: The Great Escape* (USA) with N64Recomp, N64ModernRuntime, RecompFrontend
and RT64. There is no decompilation of this game: its functions are found in the ROM's code. Almost all of it is
written by AI agents, so this file is what the next one needs: the rules, where things are, and the facts about the
game the code can't say on its own.

## What lives where

This repo is the game: its recompilation, the hooks and patches over it, the desktop build and its web page.

- `syms/`: the symbols N64Recomp reads, written from the ROM by `tools/gen_syms.py` (its header says how), never
  edited by hand: the game's three code sections with their ROM offsets and VRAM, libultra's functions named by
  n64sym's signatures and the rest `func_<address>`. A name worked out since goes into `syms/names.toml`, and keeps it.
- `us.toml`: N64Recomp's configuration, with the game's hooks; `tools/recompile.sh` writes `RecompiledFuncs/` with it,
  and the audio microcode with `aspMain.us.toml`.
- `patches/` and `patches.toml`: MIPS C recompiled into PatchesLib, where a patch to the game's code goes
  (`patches/Makefile`'s header says how they build). With no decomp there are no headers of the game's: a patch
  declares what it touches by address. `ui_funcs.h`, `patch_helpers.h` and `recompui_event_structs.h` are
  RecompFrontend's.
- `src/`: RecompFrontend's main (`src/main/main.cpp`, with the GameEntry), the code sections it gives the runtime
  (`src/main/register_overlays.cpp`), the natives the patches call (`src/game/recomp_api.cpp`), the Controller Pak
  (`src/game/controller_pak.cpp`), the libultra functions the runtime lacks (`src/game/libultra.cpp`), the scene
  transition's wait (`src/game/transition.cpp`), mouse look (`src/game/mouse_look.cpp`), the settings, and on Windows a stack trace on a crash (`src/main/crash_trace.cpp`).
- `lib/`: upstream `rt64`, `N64ModernRuntime` and `RecompFrontend` at the commits N64Recomp-browser's web forks are
  built on, and `N64Recomp-browser` at a release tag. The web support every recomp's page shares is developed and
  documented there; `tools/build_n64recomp.sh` and `CMakeLists.txt` apply its `patches/` to the other three, and
  `CMakeLists.txt` then this game's own changes to them (`runtime-patches/`, its README lists them).
- `docs/web.md`: the web page: `tools/build_web.sh`, its prebuilt shader list (`src/rt64_shaders.txt`,
  `tools/shader_plan.json`) and its play plans (`tools/play_plan*.json`).
- `docs/architecture.md`: how the game and its recompilation fit together, the facts the code can't say on its
  own (How it fits together, below).
- Comments beside the code, for how and why: each file's header. Read the header of the file you change and update
  it in the same commit.

## Rules

- No ROMs, ROM-derived assets or extracted game data in git, CI or PRs (`tools/ci/check_repo.py` checks). Nothing made
  from the ROM is committed but `syms/`'s names, addresses and sizes. The icon (`icons/icon.svg`, the launcher's
  thumbnail and the program's icon by `tools/icon.py`) is drawn for the project from plain shapes: no logo, character or
  picture from the game.
- The desktop releases are built where the ROM is, by `tools/release.sh` (its header says how), and published to
  GitHub Releases with their notes in `releases/`.
- The scope is the game as it plays, plus what a web page needs (RT64 interpolation to the display's rate, resolution
  Auto), plus modern enhancements: each an option with an Original value that plays as the console does, none
  changing how the game plays. USA only.
- Write for a stranger, in the present tense: nothing about a contributor's machine, servers, paths or accounts, and
  nothing about where anyone keeps ROMs.
- GPL-3.0, as the recompilations it follows.

## If the change reaches another repo, YOU make it

This repo's work is the project's two Ubi Soft recompilations, this one and `donald-duck-goin-quackers-recomp`, which
are checked and kept the same way. So a change the work needs in Goin' Quackers' repo is made in the same
work, not handed on as work of its own: a PR there, from a checkout of its `main`, landed by whoever does the work like
one here.

## How it fits together

`docs/architecture.md` has the facts about the game the code can't say on its own: the code, spins, the idle watchdog,
the RSP's done message, one frame a VI, the sound queue, an animation's last frame, microcodes, saves, the Expansion
Pak, input, the camera, widescreen, interpolation, the intro's white fade and testing. Read its part before changing the
recompilation, a patch or a hook, and add a new finding there, not here.
