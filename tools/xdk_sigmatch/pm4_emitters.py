"""List guest functions that build PM4 type-3 packet headers for draws and tiling.

Scans every .pdata function of an image dump (fable_2_xex_image_dump output)
for a `lis rX,0xC0xx` followed within 16 instructions by `ori`/`addi` whose
immediate is a type-3 header low half (opcode in bits 8-14, predicate bit
allowed). That is how the XDK builds draw and bin-mask headers inline, so the
result is the complete set of guest functions that emit those packets
directly (precompiled command buffers in data sections are not covered).

Usage: python tools\\xdk_sigmatch\\pm4_emitters.py out\\xdk\\fable2
"""
import bisect
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sigmatch as sm  # noqa: E402

OPCODES = {
    0x22: "DRAW_INDX", 0x36: "DRAW_INDX_2", 0x34: "DRAW_INDX_BIN", 0x35: "DRAW_INDX_2_BIN",
    0x3F: "INDIRECT_BUFFER", 0x37: "INDIRECT_BUFFER_PFD",
    0x50: "SET_BIN_MASK", 0x51: "SET_BIN_SELECT", 0x4B: "SET_BIN_BASE_OFFSET",
    0x60: "SET_BIN_MASK_LO", 0x61: "SET_BIN_MASK_HI",
    0x62: "SET_BIN_SELECT_LO", 0x63: "SET_BIN_SELECT_HI",
}
LOOKBACK = 16


def header_sites(words, start):
    """[(address, opcode_name, header)] for type-3 headers built in `words`."""
    sites = []
    for i, w in enumerate(words):
        if (w >> 26) not in (24, 14):          # ori, addi
            continue
        imm = w & 0xFFFF
        op = (imm >> 8) & 0x7F
        if op not in OPCODES or imm & 0xFE:   # low byte: only the predicate bit
            continue
        for j in range(max(0, i - LOOKBACK), i + 1):
            v = words[j]
            if v >> 26 == 15 and (v >> 16) & 31 == 0 and (v & 0xFFFF) >> 8 == 0xC0:  # lis rX,0xC0xx
                sites.append((start + 4 * i, OPCODES[op], ((v & 0xFFFF) << 16) | imm))
                break
    return sites


def static_callers(image, funcs, targets):
    """{target: [(call address, containing function)]} for direct `bl` calls."""
    out = {t: [] for t in targets}
    for s, length in funcs.items():
        for a in range(s, s + length, 4):
            w = image.word(a)
            if w >> 26 != 18 or not w & 1:
                continue
            li = w & 0x03FFFFFC
            if li & 0x02000000:
                li -= 0x04000000
            t = li if w & 2 else a + li
            if t in out:
                out[t].append((a, s))
    return out


def scan(image, funcs):
    """{opcode_name: {function_start: [sites]}}."""
    by_op = {}
    for s in sorted(funcs):
        words = [image.word(s + i) for i in range(0, funcs[s], 4)]
        for site in header_sites(words, s):
            by_op.setdefault(site[1], {}).setdefault(s, []).append(site)
    return by_op


def main():
    prefix = sys.argv[1] if len(sys.argv) > 1 else "out/xdk/fable2"
    img = sm.load_image(prefix)
    pd = img.meta["pdata"]
    funcs = sm.parse_pdata(img, pd["address"], pd["size"])
    by_op = scan(img, funcs)
    emitters = sorted({f for name in ("DRAW_INDX", "DRAW_INDX_2", "INDIRECT_BUFFER", "SET_BIN_SELECT_LO")
                       for f in by_op.get(name, {})})
    callers = static_callers(img, funcs, emitters)
    for name in sorted(by_op):
        fs = by_op[name]
        print(f"{name}: {sum(len(v) for v in fs.values())} sites in {len(fs)} functions")
        for f in sorted(fs):
            calls = callers.get(f)
            tail = "" if calls is None else "  callers: " + ", ".join(
                f"0x{a:08X} in 0x{c:08X}" for a, c in calls[:8])
            print(f"  0x{f:08X} ({funcs[f] // 4} instrs){tail}")


if __name__ == "__main__":
    main()
