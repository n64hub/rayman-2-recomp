// Rayman 2's survey for the page's plans (N64Recomp-browser's tools/play_page.mjs and record_shaders.mjs load it with an
// evalFile step): which level the game is in, and a change of level asked for as the game's own exits ask for one, so
// a plan reaches a level without playing the intro story or the levels before it. There is no decomp: the addresses
// were found by reading RDRAM as the game played.
//
// The engine keeps its state at 0x800E5D40: a mode byte at 0x800E5D43 (3 at boot, 9 while a menu or a level plays, 5
// while one loads, 6 asks for a change of level), then the level's name at 0x800E5D44, the next level's at 0x800E5D62
// and the first level's ("menu") at 0x800E5D80, 30 bytes each. go(name) writes the next level's name and mode 6, and
// the game loads that level as it does at a level's end. The game's own clock is the word at 0x800C8D84, 60 a second
// of play, stopped while a level loads and 0 until the menu first draws: the plans' clock steps read it.
(() => {
  const byte = (a) => (n64r(a & ~3) >>> (24 - 8 * (a & 3))) & 255;
  const setByte = (a, v) => {
    const sh = 24 - 8 * (a & 3);
    n64w(a & ~3, (n64r(a & ~3) & ~(255 << sh)) | ((v & 255) << sh));
  };
  const name = (a) => {
    let s = '';
    for (let c = byte(a); c >= 32 && c < 127 && s.length < 29; c = byte(a + s.length)) s += String.fromCharCode(c);
    return s;
  };
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const r2 = window.r2 = {
    mode: () => byte(0x800E5D43),
    level: () => name(0x800E5D44).toLowerCase(),
    clock: () => n64r(0x800C8D84),
    // The level's name and the mode, for a plan's log.
    where: () => `level ${r2.level() || '-'} mode ${r2.mode()} clock ${r2.clock()}`,
    // Waits up to ms for the game to play the level (mode 9), true once it does.
    playing: async (level, ms = 30000) => {
      for (const t0 = Date.now(); Date.now() - t0 < ms; await sleep(100)) {
        if (r2.level() === level && r2.mode() === 9) return true;
      }
      console.log(`[survey] not playing ${level} after ${ms} ms: ${r2.where()}`);
      return false;
    },
    // Changes to the level (its name as the game's table has it, lower case: learn_10), and waits for it to play.
    go: async (level, ms = 30000) => {
      for (let i = 0; i < 30; i++) setByte(0x800E5D62 + i, i < level.length ? level.charCodeAt(i) : 0);
      setByte(0x800E5D43, 6);
      const ok = await r2.playing(level, ms);
      console.log(`[survey] go ${level}: ${r2.where()}`);
      return ok;
    },
  };
  return true;
})();
