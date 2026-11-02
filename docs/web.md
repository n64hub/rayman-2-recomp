# The web page

The game plays in a browser: built with Emscripten on N64Recomp-browser's `frontend/` into `Rayman2Recompiled.html`
(the launcher, the recomp's menus, SDL2's audio and keyboard, the ROM from Load ROM, and the saves kept in IndexedDB;
`lib/N64Recomp-browser/frontend/CMakeLists.txt`'s header says what each part does), drawn through WebGPU. The page is
built from your ROM like the desktop program, so it is never built in CI and never published with the game in it.

What is the game's own stays here: the `EMSCRIPTEN` branches of `CMakeLists.txt` that make the game a page,
`tools/build_web.sh`, the prebuilt shader list (`src/rt64_shaders.txt`, recorded by `tools/shader_plan.json`; the
list's header says how to record it again), the play plans (`tools/play_plan.json`, `tools/play_plan_extended.json`)
and the survey they share (`tools/survey.js`). The rest is N64Recomp-browser's: `docs/porting.md` (WebGPU and the
shaders, prebuilt shader lists, Emscripten), `docs/playing.md` (playing and measuring a page), `docs/forks.md` and
`docs/findings.md`. Every play of this game's page is a line in N64Recomp-browser's `plays.jsonl` (game `r2`), never a
record here. The page is not published yet: the game is held unreleased until it plays further.

## Build it

On Linux with Docker, with the ROM at `roms/rayman2.us.z64`:

```bash
tools/build_web.sh                  # [cpus] [cmake arguments]
```

It builds `build/web/Rayman2Recompiled.html` with its `.js`, `.wasm` (about 11 MB) and `.data` (about 9 MB: the
recomp's fonts and menus, nothing of the game) in `tools/docker/Dockerfile.web`, the image every N64Recomp-browser
page is built in, with ccache in `build/ccache-web`, so a rebuild after a change takes a minute or two. The script's
header says each step. Two things differ from the desktop build:

- The patches library (`patches/`, built for MIPS) is recompiled by the native N64Recomp that `tools/recompile.sh`
  builds, not the runtime's, which Emscripten would build for the browser.
- RT64 builds its pipelines from `src/rt64_shaders.txt` (`RT64_PREBUILT_SHADER_LIST`), in sections: the boot's before
  the game starts, and a level's the first time it draws.

Serve `build/web` over HTTP from localhost (any static server; the page brings its own `coi_serviceworker.js` for the
headers threads need) and open `Rayman2Recompiled.html` in Chrome or Firefox with WebGPU, then Load ROM.

## The plans

A plan reaches the intro story the way a player does, by the keyboard's defaults (Enter is Start, Space A, WASD the
stick): Start Game, the logos and the attract demo, Start to the main menu, and A through New Game, a slot and a name.
The intro story can't be skipped and runs about 100 s, so from there `tools/survey.js` changes level as the game's own
exits do: it writes the next level's name where the engine reads it and asks for a change of level, and the game loads
that level. So a plan reaches any level with a new game, without a save. The survey's header has the addresses, found
by reading RDRAM as the game played (there is no decomp); the waits in a level are on the game's own clock, 60 a
second of play.

`tools/play_plan.json` (about 2.5 minutes) boots, shoots the menu, watches 25 s of the intro story, then plays the
prison ship (jail_20) and walks about the Woods of Light (learn_10). `tools/play_plan_extended.json` (about 5
minutes) goes on to five more levels. `tools/play_plan_levels.json` (about 10 minutes, a release's check) visits
every level of the game's table in turn, with a shot of each, and goes on past a level that doesn't play, so one play
shows every level that fails. Play them with N64Recomp-browser's `tools/play_page.mjs` (its `docs/playing.md`).

## The shader list

`src/rt64_shaders.txt` holds the boot's shaders and a section a level of the game's table of levels, each built the
first time the level draws. It is recorded by code with `tools/shader_plan.json` (the list's header says what each
section plays and how to record it again). A play that misses a shader prints it (`play_page.mjs` counts the misses);
add it to the section of the level it was missed in, or, for a screen any level shows (the pause menu), to the plan's
last section, pause, which goes into the boot's.

## How it plays

By the regular plan on a desktop GPU, uncapped at 3840x2000 on an idle host, Chrome presents 354 a second (the
display's 360 holds it, not the page), its longest frame gap 23 ms, with no shader waits, no misses and no WebGPU
errors; Firefox builds each level's section with the game waiting (half a second or so) and plays with no misses or
errors, its longest gaps (0.3-0.7 s) as a level changes. Its runs are in N64Recomp-browser's `plays.jsonl` (`r2`).
