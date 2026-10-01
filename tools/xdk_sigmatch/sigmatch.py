"""Match statically linked XDK functions between two Xbox 360 images.

Inputs are dumps from fable_2_xex_image_dump (<prefix>.img + <prefix>.json).
Function boundaries come from .pdata. Build-variant instruction fields are
masked: branch displacements always; at the "loose" level also the 16-bit
immediates of D-form instructions whose base register is not r1 (absolute
addresses, globals, struct offsets that moved between XDK builds).
"""
import difflib
import json
import struct
from pathlib import Path

# D-form opcodes with a 16-bit immediate/displacement: addi, addis, ori,
# oris, lwz..stfdu (32-55), ld/std family (58, 62).
D_FORM_IMM = {14, 15, 24, 25, *range(32, 56), 58, 62}


class Image:
    def __init__(self, base, data, sections):
        self.base = base
        self.data = data
        self.sections = sections

    def word(self, addr):
        off = addr - self.base
        return struct.unpack_from(">I", self.data, off)[0]

    def contains(self, addr):
        return 0 <= addr - self.base <= len(self.data) - 4

    def is_executable(self, addr):
        return any(s["executable"] and s["address"] <= addr < s["address"] + s["size"]
                   for s in self.sections)


def load_image(prefix):
    meta = json.loads(Path(prefix + ".json").read_text())
    data = Path(prefix + ".img").read_bytes()
    img = Image(meta["base"], data, meta["sections"])
    img.meta = meta
    return img


def parse_pdata(image, address, size):
    """Function start -> length in bytes. Skips zero-length and non-code entries."""
    funcs = {}
    for off in range(0, size - size % 8, 8):
        start, packed = struct.unpack_from(">II", image.data, address - image.base + off)
        length = ((packed >> 8) & 0x3FFFFF) * 4
        if length == 0 or not image.is_executable(start) or not image.is_executable(start + length - 4):
            continue
        funcs[start] = length
    return funcs


def mask_for(word, level):
    op = word >> 26
    if op == 18:            # b / bl: LI displacement
        return 0xFC000003
    if op == 16:            # bc: BD displacement
        return 0xFFFF0003
    if level == "strict":
        return 0xFFFFFFFF
    ra = (word >> 16) & 0x1F
    if op in D_FORM_IMM and ra != 1:
        return 0xFFFF0000
    return 0xFFFFFFFF


def function_words(image, start, length):
    return [image.word(start + i) for i in range(0, length, 4)]


def matches(ref_words, cand_words, level):
    if len(ref_words) != len(cand_words):
        return False
    for r, c in zip(ref_words, cand_words):
        m = mask_for(r, level)
        if (r & m) != (c & m):
            return False
    return True


PREFIX_WORDS = 16


def _candidates(ref_words, tgt_img, tgt_funcs, level, prefix=False):
    out = []
    for start, length in sorted(tgt_funcs.items()):
        if prefix:
            if length < 4 * len(ref_words):
                continue
            cand = function_words(tgt_img, start, 4 * len(ref_words))
        else:
            if length != 4 * len(ref_words):
                continue
            cand = function_words(tgt_img, start, length)
        if matches(ref_words, cand, level):
            out.append(start)
    return out


FUZZY_MIN_SCORE = 0.85
FUZZY_MIN_MARGIN = 0.30


def opcode_seq(words):
    return [w >> 26 for w in words]


def fuzzy_score(ref_words, cand_words):
    return difflib.SequenceMatcher(None, opcode_seq(ref_words), opcode_seq(cand_words),
                                   autojunk=False).ratio()


def _fuzzy(result, words, length, tgt_img, tgt_funcs):
    tol = max(16, length // 10)
    scored = sorted(((fuzzy_score(words, function_words(tgt_img, s, l)), s)
                     for s, l in tgt_funcs.items() if abs(l - length) <= tol), reverse=True)
    if not scored:
        return
    best, best_addr = scored[0]
    runner = scored[1][0] if len(scored) > 1 else 0.0
    result.update(score=round(best, 3), runner_up=round(runner, 3))
    if best >= FUZZY_MIN_SCORE and best - runner >= FUZZY_MIN_MARGIN:
        result.update(target=best_addr, confidence="fuzzy")
    else:
        result["candidates"] = [best_addr]


def _match_one(name, ref_addr, ref_img, ref_funcs, tgt_img, tgt_funcs):
    result = {"name": name, "ref": ref_addr, "target": None, "confidence": "none", "candidates": []}
    length = ref_funcs.get(ref_addr)
    if length is None:
        result["confidence"] = "no-ref-function"
        return result
    words = function_words(ref_img, ref_addr, length)
    for level, prefix in (("strict", False), ("loose", False), ("loose", True)):
        ws = words[:PREFIX_WORDS] if prefix else words
        if prefix and len(words) <= PREFIX_WORDS:
            continue
        cands = _candidates(ws, tgt_img, tgt_funcs, level, prefix)
        if len(cands) == 1:
            result.update(target=cands[0], confidence="prefix" if prefix else level)
            return result
        if len(cands) > 1:
            result.update(confidence="ambiguous", candidates=cands)
            return result
    _fuzzy(result, words, length, tgt_img, tgt_funcs)
    return result


def match_references(ref_img, ref_funcs, tgt_img, tgt_funcs, references):
    return [_match_one(name, addr, ref_img, ref_funcs, tgt_img, tgt_funcs)
            for name, addr in references.items()]


def call_targets(image, start, length):
    targets = []
    for addr in range(start, start + length, 4):
        w = image.word(addr)
        if (w >> 26) == 18 and (w & 3) == 1:  # bl (LK=1, AA=0)
            disp = w & 0x03FFFFFC
            if disp & 0x02000000:
                disp -= 0x04000000
            targets.append(addr + disp)
    return targets


def propagate(ref_img, ref_funcs, tgt_img, tgt_funcs, results):
    results = list(results)
    by_ref = {r["ref"]: r for r in results if r["target"] is not None}
    implied = {}  # ref callee -> set of target callees
    names = {}
    changed = True
    while changed:
        changed = False
        for r in list(by_ref.values()):
            if r["confidence"] not in ("strict", "loose", "fuzzy", "callgraph"):
                continue
            rc = call_targets(ref_img, r["ref"], ref_funcs[r["ref"]])
            tc = call_targets(tgt_img, r["target"], tgt_funcs[r["target"]])
            if len(rc) != len(tc):
                continue
            for i, (a, b) in enumerate(zip(rc, tc)):
                if a not in ref_funcs or b not in tgt_funcs:
                    continue
                implied.setdefault(a, set()).add(b)
                names.setdefault(a, f'{r["name"]}.callee{i}')
                if a not in by_ref:
                    entry = {"name": names[a], "ref": a, "target": b, "confidence": "callgraph",
                             "candidates": []}
                    by_ref[a] = entry
                    results.append(entry)
                    changed = True
    for a, targets in implied.items():
        if len(targets) > 1 and a in by_ref and by_ref[a]["confidence"] == "callgraph":
            by_ref[a].update(target=None, confidence="conflict", candidates=sorted(targets))
    return results


def render_markdown(results):
    lines = ["| Name | Skate 3 TU3 | Fable 2 | Confidence | Score | Notes |",
             "|---|---|---|---|---|---|"]
    for r in results:
        tgt = f'0x{r["target"]:08X}' if r["target"] is not None else "-"
        notes = ", ".join(f"0x{c:08X}" for c in r["candidates"])
        score = f'{r["score"]:.3f}' if "score" in r else ""
        lines.append(f'| {r["name"]} | 0x{r["ref"]:08X} | {tgt} | {r["confidence"]} | {score} | {notes} |')
    return "\n".join(lines) + "\n"


def disasm(prefix, addr, count):
    """Print `count` instructions at `addr` (requires: pip install capstone)."""
    import capstone
    img = load_image(prefix)
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    code = img.data[addr - img.base: addr - img.base + 4 * count]
    for ins in md.disasm(code, addr):
        print(f"0x{ins.address:08X}: {ins.mnemonic:8} {ins.op_str}")


def main():
    import sys
    if len(sys.argv) >= 2 and sys.argv[1] == "disasm":
        disasm(sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4]) if len(sys.argv) > 4 else 24)
        return
    import argparse
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--ref", required=True)
    p.add_argument("--target", required=True)
    p.add_argument("--refs", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--markdown", required=True)
    a = p.parse_args()
    ref_img, tgt_img = load_image(a.ref), load_image(a.target)
    rp, tp = ref_img.meta["pdata"], tgt_img.meta["pdata"]
    ref_funcs = parse_pdata(ref_img, rp["address"], rp["size"])
    tgt_funcs = parse_pdata(tgt_img, tp["address"], tp["size"])
    refs = {k: int(v, 16) for k, v in json.loads(Path(a.refs).read_text()).items()}
    results = propagate(ref_img, ref_funcs, tgt_img, tgt_funcs,
                        match_references(ref_img, ref_funcs, tgt_img, tgt_funcs, refs))
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text(json.dumps(results, indent=2))
    Path(a.markdown).write_text(render_markdown(results))
    found = sum(1 for r in results if r["target"] is not None)
    print(f"{found}/{len(results)} functions mapped "
          f"({len(ref_funcs)} ref / {len(tgt_funcs)} target functions)")


if __name__ == "__main__":
    main()
