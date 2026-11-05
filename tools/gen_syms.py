"""Writes syms/rayman2.us.syms.toml, N64Recomp's symbols for Rayman 2: The Great Escape (USA), from the ROM alone.

There is no decompilation of this game and no symbol map, so every function is found in the ROM's code:

- The code is three sections stored raw, each with its data after its text (SECTIONS): the boot segment, which the
  IPL3 copies to 0x80000400 with the first MB of ROM, and the main and support segments, which the game thread
  (0x80000854) reads with PI DMA once and keeps. Nothing else is code: no overlay, no compressed code.
- spimdisasm finds the functions in each text (jal targets, the code after a function's last jr ra, hi/lo pairs that
  make a text address), and a function it splits at a label inside one, which only that function's own branches reach,
  is joined again (merge_split_functions).
- A function reached only through a pointer is one N64Recomp must have as a function too, or the runtime can't find
  it when the game calls the pointer. Code makes some of those pointers (a lui then an addiu to a text address: thread
  entries, callbacks), and the rest are words in the sections' data (the script engine's tables of handlers, object
  methods). A run of data words that all point inside one function is that function's jump table, which N64Recomp
  reads itself, so only words that point at a function's start, or past the end of what the function before reaches,
  are taken (data_pointers).
- libultra and libc in the boot segment are named by n64sym's signatures (ref/n64sym, MIT: git clone
  https://github.com/shygoo/n64sym ref/n64sym): a function matches when the CRC32 of its first 8 bytes and of its
  whole body agree once relocated fields are masked out. N64Recomp has the runtime replace libultra functions by name,
  so those it reimplements must carry their real names; MANUAL_NAMES has the ones the signatures can't tell.

Everything else is func_<address>. Names worked out since go into syms/names.toml ([[func]] section, vram, name),
applied last, and keep their names for good (mods and hooks use them).

Usage: py -3 -I tools/gen_syms.py [--report]   (needs roms/rayman2.us.z64, ref/n64sym and spimdisasm)
"""
import csv
import struct
import subprocess
import sys
import tempfile
import tomllib
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ROM_PATH = ROOT / "roms" / "rayman2.us.z64"
SIG_PATH = ROOT / "ref" / "n64sym" / "src" / "builtin_signatures.sig"
NAMES_PATH = ROOT / "syms" / "names.toml"
OUT_PATH = ROOT / "syms" / "rayman2.us.syms.toml"
ROM_SHA1 = "50558356b059ad3fbaf5fe95380512b9dceaaf52"

# name, ROM offset, VRAM, size of the text, size of the whole segment (text and data) in ROM.
SECTIONS = [
    # The text ends where rspboot starts (0x80017D90); the microcodes and the data follow, then bss at 0x8001D0C0.
    ("boot", 0x00001000, 0x80000400, 0x80017D90 - 0x80000400, 0x1CCC0),
    # The last function ends at 0x800C6D98; the data starts at 0x800C6DB0.
    ("main", 0x0001DCC0, 0x80025C50, 0x800C6DA0 - 0x80025C50, 0xA7F30),
    ("support", 0x000C5BF0, 0x800F64A0, 0x801009B0 - 0x800F64A0, 0xAE30),
]

# libultra functions the signatures can't name, by hand from what they do. N64Recomp drops or reimplements libultra
# functions by name.
MANUAL_NAMES = {
    0x8000C3D0: "__osExceptionPreamble",   # lui/addiu/jr $k0 into __osException
    0x8000C3E0: "__osException",
    0x8000C944: "send_mesg",               # the handler's: __osPopThread, __osEnqueueThread
    0x8000CD10: "__osCleanupThread",       # the return address osCreateThread gives a thread
    0x80007980: "osAiSetNextBuffer",       # AI_DRAM_ADDR/AI_LEN after __osAiDeviceBusy, through osVirtualToPhysical
    0x80008B10: "osContInit",              # osGetTime, a timer, __osPackRequestData, __osContGetInitData
    0x80008914: "__osPackReadData",        # follows osContGetReadData
    0x8000AA60: "osMotorStart",            # osMotorStop's twin, after it
    0x80012470: "__osGetId",               # __osPfsSelectBank, __osContRamRead, __osCheckPackId, __osRepairPackId
    # Two stores of a0 and a1 to the time osGetTime reads (0x80021E40). Unnamed, the game's resets of the time went to
    # that word and not to the runtime's osGetTime, which then counted from the program's start, the launcher's time
    # included, where the game counts from its boot.
    0x80004280: "osSetTime",
    # The PI and SI access queues have the same code but for the globals they touch, so the signatures can't tell
    # them apart: the PI's lie with osPiStartDma, the SI's with __osSiRawStartDma.
    0x8000BA90: "__osPiCreateAccessQueue",
    0x8000BAE4: "__osPiGetAccess",
    0x8000BB50: "__osPiRelAccess",
    0x80011680: "__osSiCreateAccessQueue",
    0x800116D4: "__osSiGetAccess",
    0x80011740: "__osSiRelAccess",
}

# Function starts the analysis misses: where the entry point jumps once bss is clear (jr $t2), and the two the
# exception handler's code runs into.
EXTRA_FUNCTION_STARTS = {0x80000450, 0x8000C3E0, 0x8000CD10}

# Signature names n64sym gives unrelated tiny functions.
UNTRUSTED_NAMES = {"n_alSynRemovePlayer", "osProfileFlush", "n_alSeqpDelete", "alSynFreeFX", "ptstart", "__dummy",
                   "osSyncPrintf", "__osGetId", "osScGetCmdQ"}


def word(buf, off):
    return struct.unpack_from(">I", buf, off)[0]


def load_sigs():
    sigs, cur = [], None
    for line in SIG_PATH.read_text().splitlines():
        p = line.split()
        if not p or p[0].startswith("#"):
            continue
        if p[0].startswith("."):
            cur["relocs"] += [(int(o, 16), p[0], p[1]) for o in p[2:]]
        else:
            cur = {"name": p[0], "size": int(p[1], 16), "crc8": int(p[2], 16), "crc": int(p[3], 16), "relocs": []}
            sigs.append(cur)
    return [s for s in sigs if s["size"] >= 12]


def masked(buf, off, sig):
    data = bytearray(buf[off:off + sig["size"]])
    if len(data) < sig["size"]:
        return None
    for o, kind, _ in sig["relocs"]:
        w = struct.unpack_from(">I", data, o)[0]
        w &= 0xFFFF0000 if kind in (".hi16", ".lo16") else 0xFC000000
        struct.pack_into(">I", data, o, w)
    return bytes(data)


def match_library(buf, vram, func_addrs, sigs):
    """{vram: name} for the functions whose code matches a signature (the largest match wins)."""
    by_crc8 = {}
    for s in sigs:
        by_crc8.setdefault(s["crc8"], []).append(s)
    names = {}
    for addr in func_addrs:
        off = addr - vram
        hits = []
        for s in by_crc8.get(zlib.crc32(masked(buf, off, {"size": 8, "relocs": []}) or b""), []):
            d = masked(buf, off, s)
            if d is not None and zlib.crc32(d[:8]) == s["crc8"] and zlib.crc32(d) == s["crc"]:
                hits.append(s)
        if not hits:
            continue
        size = max(s["size"] for s in hits)
        best = [s for s in hits if s["size"] == size]
        if len({s["name"] for s in best}) > 1:
            print(f"  ambiguous at {addr:08X}: {sorted({s['name'] for s in best})}")
            continue
        names[addr] = best[0]["name"]
        # The jal targets in a matched function are named by its relocations too.
        for o, kind, target in best[0]["relocs"]:
            if kind == ".targ26" and "_text_" not in target:
                names.setdefault(((word(buf, off + o) & 0x3FFFFFF) << 2) | 0x80000000, target)
    return names


def spimdisasm_functions(buf, vram, tmp):
    binpath = Path(tmp) / f"{vram:08X}.bin"
    binpath.write_bytes(buf)
    info = Path(tmp) / f"{vram:08X}.csv"
    subprocess.run([sys.executable, "-m", "spimdisasm", "singleFileDisasm", str(binpath), str(Path(tmp) / "asm"),
                    "--start", "0", "--end", hex(len(buf)), "--vram", hex(vram), "--function-info", str(info)],
                   check=True, capture_output=True)
    with info.open() as f:
        return sorted(int(r["address"], 16) for r in csv.DictReader(f))


def branch_targets(buf, vram, start, end):
    """The targets of the PC-relative branches and j instructions in [start, end)."""
    out = set()
    for a in range(start, end, 4):
        w = word(buf, a - vram)
        op = w >> 26
        if op in (4, 5, 6, 7, 20, 21, 22, 23) or (op == 1 and ((w >> 16) & 0x1F) in (0, 1, 2, 3)) or \
                (op == 0x11 and ((w >> 21) & 0x1F) == 8):  # beq/bne/blez/bgtz (and likely), regimm, bc1x
            out.add(a + 4 + (((w & 0xFFFF) ^ 0x8000) - 0x8000) * 4)
        elif op == 2:  # j
            out.add(((a + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
    return out


def in_text(addr):
    return any(v <= addr < v + t for _, _, v, t, _ in SECTIONS)


def code_pointers(rom):
    """{target: [where]} for each jal target and each text address code makes with a lui and an addiu."""
    out = {}
    for _, r, v, t, _ in SECTIONS:
        his = {}
        for o in range(0, t, 4):
            w = word(rom, r + o)
            op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
            if op == 3:
                out.setdefault(((w & 0x3FFFFFF) << 2) | 0x80000000, []).append(v + o)
            elif op == 15:
                his[rt] = imm << 16
            elif op == 9 and rs in his:
                target = (his[rs] + imm - (imm & 0x8000) * 2) & 0xFFFFFFFF
                if in_text(target) and target % 4 == 0:
                    out.setdefault(target, []).append(v + o)
            if op == 2 or (op == 0 and (w & 0x3F) == 8):
                his.clear()  # a jump ends what a lui set up, as a function's end does
            elif op != 15:
                his.pop((w >> 11) & 31 if op == 0 else rt, None)  # anything else written to the register
    return out


def jump_table_words(rom, starts):
    """The addresses of the words in every jump table: where a jr through a register other than $ra jumps to a word
    loaded from a lui's %hi and the lw's %lo, the table runs on while its words point inside the jr's function."""
    starts = sorted(starts)
    out = set()
    for _, r, v, t, size in SECTIONS:
        for o in range(0, t, 4):
            w = word(rom, r + o)
            if w >> 26 != 0 or w & 0x3F != 8 or (w >> 21) & 31 == 31:
                continue
            reg = (w >> 21) & 31
            lo = base = None
            for back in range(1, 12):
                x = word(rom, r + o - back * 4)
                if lo is None and x >> 26 == 35 and (x >> 16) & 31 == reg:  # lw reg, %lo(table)(base)
                    lo, base = (x & 0xFFFF) - (x & 0x8000) * 2, (x >> 21) & 31
                elif lo is not None and x >> 26 == 15:  # the lui of %hi(table), into base or a register added to it
                    table = ((x & 0xFFFF) << 16) + lo & 0xFFFFFFFF
                    break
            else:
                continue
            i = __import__("bisect").bisect_right(starts, v + o) - 1
            first, end = starts[i], starts[i + 1] if i + 1 < len(starts) else v + t
            for sr, sv, ssize in ((sr, sv, ss) for _, sr, sv, _, ss in SECTIONS if sv <= table < sv + ss):
                a = table
                while a < sv + ssize and first <= word(rom, sr + a - sv) < end:
                    out.add(a)
                    a += 4
    return out


def data_pointers(rom, starts, jump_tables):
    """{target: [where]} for the words in the sections' data that point into a text, but those in jump tables. GCC
    often keeps a table's address in a saved register from the function's start, out of jump_table_words' sight, so a
    run of two or more words that point inside one function, not all at its start, is its jump table too: a table of
    function pointers points at functions' starts."""
    starts = sorted(starts)
    owner = lambda a: starts[max(0, __import__("bisect").bisect_right(starts, a) - 1)]
    out = {}
    for _, r, v, t, size in SECTIONS:
        run = []
        for o in list(range(t, size, 4)) + [None]:
            w = word(rom, r + o) if o is not None else 0
            pointer = o is not None and in_text(w) and w % 4 == 0 and v + o not in jump_tables
            if pointer and run and owner(w) == owner(run[-1][1]):
                run.append((v + o, w))
                continue
            if not (len(run) >= 2 and any(p != owner(p) for _, p in run)):
                for at, p in run:
                    out.setdefault(p, []).append(at)
            run = [(v + o, w)] if pointer else []
    return out


def looks_like_start(rom, addr):
    """The code before addr ends a function: a jr $ra or a j and its delay slot, and nothing but nops after them."""
    for _, r, v, t, _ in SECTIONS:
        if v <= addr < v + t:
            a = addr - 4
            while a > v and word(rom, r + a - v) == 0 and addr - a < 32:
                a -= 4
            for b in (a, a - 4):
                w = word(rom, r + b - v) if b >= v else 0
                if w == 0x03E00008 or w >> 26 == 2:
                    return True
            return addr == v
    return False


def merge_split_functions(buf, vram, size, starts, called):
    """Joins a function spimdisasm split at a label inside it: when a function branches into the next and nothing calls
    or points at that one, it is the same function."""
    merged = True
    while merged:
        merged = False
        for i in range(len(starts) - 1):
            end = starts[i + 1]
            beyond = [t for t in branch_targets(buf, vram, starts[i], end) if end <= t < vram + size]
            if not beyond:
                continue
            j = i + 1
            while j < len(starts) and starts[j] <= max(beyond):
                j += 1
            if all(s not in called for s in starts[i + 1:j]):
                starts = starts[:i + 1] + starts[j:]
                merged = True
                break
    return starts


def load_registry():
    if not NAMES_PATH.exists():
        return {}
    return {(f["section"], f["vram"]): f["name"] for f in tomllib.loads(NAMES_PATH.read_text()).get("func", [])}


def main():
    import hashlib
    rom = ROM_PATH.read_bytes()
    if hashlib.sha1(rom).hexdigest() != ROM_SHA1:
        sys.exit(f"{ROM_PATH}: not Rayman 2 (USA), SHA-1 {ROM_SHA1}")
    report = "--report" in sys.argv
    sigs = load_sigs()
    registry = load_registry()
    found = {}
    with tempfile.TemporaryDirectory() as tmp:
        for sec, r, v, t, _ in SECTIONS:
            found[sec] = spimdisasm_functions(rom[r:r + t], v, tmp)
    known = {a for s in found.values() for a in s}
    by_code = code_pointers(rom)
    by_data = data_pointers(rom, known, jump_table_words(rom, known))
    pointed = {a for a in set(by_code) | set(by_data) if a in known or looks_like_start(rom, a)}
    rejected = sorted(a for a in set(by_code) | set(by_data) if a not in pointed and in_text(a))
    pointed |= EXTRA_FUNCTION_STARTS
    out = ["# Written by tools/gen_syms.py from the ROM; not edited by hand.", ""]
    used = set()
    for sec, r, v, t, _ in SECTIONS:
        buf = rom[r:r + t]
        starts = sorted(set(found[sec]) | {a for a in pointed if v <= a < v + t})
        new = len(starts) - len(found[sec])
        starts = merge_split_functions(buf, v, t, starts, pointed)
        names = {}
        if sec == "boot":
            names = {a: n for a, n in match_library(buf, v, starts, sigs).items() if n not in UNTRUSTED_NAMES}
            manual = set(MANUAL_NAMES.values())
            names = {a: n for a, n in names.items() if n not in manual}
            names.update(MANUAL_NAMES)
        out += ["[[section]]", f'name = "{sec}"', f"rom = 0x{r:08X}", f"vram = 0x{v:08X}", f"size = 0x{t:X}", "",
                "functions = ["]
        named = 0
        for i, addr in enumerate(starts):
            end = starts[i + 1] if i + 1 < len(starts) else v + t
            name = registry.get((sec, addr)) or names.get(addr)
            if name is None or name in used:
                name = f"func_{addr:08X}"
            else:
                named += 1
            used.add(name)
            out.append(f'    {{ name = "{name}", vram = 0x{addr:08X}, size = 0x{end - addr:X} }},')
        out += ["]", ""]
        print(f"{sec}: {len(starts)} functions ({len(found[sec])} from spimdisasm, {new} more from pointers), "
              f"{named} named")
        if report:
            for a in sorted(set(starts) - set(found[sec])):
                print(f"  {a:08X} from {'code' if a in by_code else 'data'} at "
                      + ", ".join(f"{x:08X}" for x in (by_code.get(a) or by_data.get(a) or [])[:4]))
    print(f"{len(rejected)} pointers into the middle of a function left alone")
    if report:
        for a in rejected:
            print(f"  {a:08X} from {'code' if a in by_code else 'data'} at "
                  + ", ".join(f"{x:08X}" for x in (by_code.get(a) or by_data.get(a) or [])[:4]))
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text("\n".join(out), newline="\n")
    print(f"wrote {OUT_PATH.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
