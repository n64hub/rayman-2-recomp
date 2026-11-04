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
  (`src/game/controller_pak.cpp`), the libultra functions the runtime lacks (`src/game/libultra.cpp`), the settings,
  and on Windows a stack trace on a crash (`src/main/crash_trace.cpp`).
- `lib/`: upstream `rt64`, `N64ModernRuntime` and `RecompFrontend` at the commits N64Recomp-browser's web forks are
  built on, and `N64Recomp-browser` at a release tag. The web support every recomp's page shares is developed and
  documented there; `tools/build_n64recomp.sh` and `CMakeLists.txt` apply its `patches/` to the other three, and
  `CMakeLists.txt` then this game's own changes to them (`runtime-patches/`, its README lists them).
- `docs/web.md`: the web page: `tools/build_web.sh`, its prebuilt shader list (`src/rt64_shaders.txt`,
  `tools/shader_plan.json`) and its play plans (`tools/play_plan*.json`).
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

## How it fits together

- **The code.** Three sections, stored raw: the boot segment (ROM 0x1000, VRAM 0x80000400), which the IPL3 copies with
  the first MB of ROM, and the main (0x80025C50) and support (0x800F64A0) segments, which the game thread reads out of
  ROM once at boot and keeps. Nothing else is code: no overlay, nothing compressed. The runtime maps the MB after ROM
  0x1000 at 0x80000400 at start, so `src/main/register_overlays.cpp` maps main and support where the game reads them.
- **Spins.** The runtime runs one game thread at a time and switches only when the running one calls into it, where a
  console's interrupts preempt any thread. The game spins in three places that wait on its controller thread: for a
  Controller Pak request to end, and each frame for the controllers' read. Each pass yields (`us.toml`). Without them
  the game freezes after "Expansion Pak detected!" at boot, or on the language menu with no input taken. A new hang
  with one thread busy and every other in `osRecvMesg` is another such loop: `--stacks` finds it.
- **The idle watchdog.** The idle thread waits for the main thread's OSThread state to read libultra's
  OS_STATE_STOPPED (1) and then breaks into the game's crash handler. ultramodern keeps its own states in that field,
  where 1 is QUEUED, so the hook in `us.toml` parks the idle thread for good instead. Without it the program ends
  with 0xC0000409 a few seconds in.
- **The RSP's done message.** ultramodern tells a game a graphics task's RSP work is done as RT64 starts it, which
  holds for games that keep a list's inputs until the RDP is done too. This one changes them once the RSP is done, so
  `runtime-patches/` sends the message once RT64 has read the list. Without it a slow renderer (lavapipe, a page on a
  busy host) read lists being rewritten: a crash in `RT64::Interpreter::processDisplayLists` or "Unable to find a
  matching GBI".
- **One frame a VI.** The game keys its cache of texture loads by the retrace count a frame starts in, and has one
  display list buffer, so it takes for granted that no two frames start in one VI: a console's CPU never builds them
  that fast. The recomp's does, and such a frame calls the load blocks the frame before wrote, where its own commands
  now go: textures from the wrong place, or a call to itself that RT64 follows until the page ends with Aborted(OOM).
  A hook in `us.toml` holds a frame that would start in the same count until the next retrace.
- **The sound queue.** The game's sound engine queues calls in an array of 250 with no check of its size, emptied
  as each audio frame is made and filled each retrace with the stops of finished voices. A graphics list RT64 takes
  long to read (lavapipe building a pipeline) holds back the audio task, and after about 84 retraces the queue
  overwrites the voice table after it: "Failed to find function at 0x8019B..." at the next retrace. A hook in
  `us.toml` drops what would go past its end.
- **Microcodes.** Graphics: F3DEX.NoN 1.23, which RT64 knows. Audio: `aspMain` (libultra's ABI 1), recompiled by
  RSPRecomp from `aspMain.us.toml`. librecomp's vector unit uses SSE4.1, so `RecompiledFuncs` builds with
  `-march=nehalem` on x86-64.
- **Saves.** The game saves only to a Controller Pak (libultra's `osPfs*`), so `main.cpp` declares no cartridge save
  and `src/game/controller_pak.cpp` keeps a pak of 123 pages in `saves/controllerpak_1.mpk`, the format emulators use.
  The game reads the controllers' status for a pak first, so port 1 reports one (`main.cpp`,
  `get_connected_device_info`); with none it never leaves the save slots' screen.
- **The Expansion Pak.** The game finds the runtime's 8 MB and says so at boot ("Expansion Pak detected!").
- **Input.** RecompFrontend's default bindings. Keyboard: WASD the stick, Space A, Left Shift B, Q Z, R R, E L, Enter
  Start, the arrows the C buttons. The game reads port 1 only.
- **Widescreen.** RecompFrontend's Graphics tab defaults to Aspect Ratio Expand, Resolution Auto and Framerate Display;
  RT64 widens the 3D. Original (Aspect Ratio Original) is the console's 4:3. A menu's backdrop (the pause menu's swirl,
  while 0x800C87D0 is set) is an opaque gradient at 4:3 over the frame's clear, a fill of the level's fog colour that
  RT64 widens to the window's edges, so a hook in `us.toml` clears black under it: without it the sides show the level's
  colour. `src/game/widescreen.cpp` widens what the game decides for a 4:3 screen: its culling planes, its sprites and
  particles (projected on the CPU to texture rectangles and clipped to the screen, drawn past it with RT64's extended
  GBI under a scissor `RT64_SCISSOR_INSIDE_FRAMEBUFFER` counts only to the frame's edges), its full-screen fades, that
  backdrop's gradient and its HUD, whose bars, icons and the counters beside them it draws from the wide picture's edges
  with RT64's rectangle alignment (a dialogue, its speaker's face and other text stay in the middle), none of them at
  Original. The scene transition, which the game blacks out from the CPU in blotches, is stretched across the picture by
  `RT64_STRETCH_CPU_CHANGES`, the blotches a third wider at 16:9; `CMakeLists.txt` sets both options. It also joins the
  title logo's overlapping tiles at every aspect, each tile's clear edge given its neighbour's texels, which upscaled
  showed their seams.
- **Interpolation.** The game moves some things only on its own frames, which RT64 moves on every frame drawn: its
  texture scrolls (the intro's sky, the language menu's water), masked tiles stepped a texel on some frames and not
  others (rt64-web 08427aa), and its menu cursor, a butterfly whose vertices the CPU places anew each frame with no
  matrix ID (rt64-web a03baa2). The butterfly's light flickering between two levels is the game's own: vertex
  colours it writes, no RSP light, the same at Original.
- **The intro's white fade.** As "Ubi Soft presents" fades out, the intro story cuts to white and its next shot of the
  sea comes up through a white fog in about 0.6 s, "PRESENTS" still fading over it. That is the game's own: a PAL
  console's capture shows it, and so does the desktop build at Framerate Original and Aspect Ratio Original.
- **Testing.** `--boot` starts the game from the launcher's first frame once the ROM is stored, `--window <w>x<h>`
  sizes the window, `--input <file>` plays port 1 from a script of timed button states (`main.cpp` says the format),
  and on Windows `--stacks <seconds>` prints every thread's stack that long after start, for unattended runs and
  hangs.
