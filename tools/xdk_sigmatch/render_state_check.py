"""Prove the device's render-state register shadows against the GPU, and take the census.

Usage: python tools\\xdk_sigmatch\\render_state_check.py <native_discovery_*.jsonl> --gpu <gpu_state.log[.gz]>
           [--top N]

The capture's "draw" and "tess" rows carry "rs" (the render-state registers read from the
device's register shadows after the draw builder returned, xdk_layout.h DeviceRegShadowOffset)
and "cmd" (the device's command write pointer at that moment). The GPU log comes from the
temporary SDK diagnostic of frame-map section 13 (one line per DRAW_INDX packet the command
processor hands to the backend, with the same registers from the GPU register file):

  D f=<gpu frame> d=<draw ordinal in the frame> a=<physical address just past the packet>
    n=<index count> bs=<bin select> bm=<bin mask> vs=<hash> ps=<hash> pitch=<surface pitch>
    mode=<RB_MODECONTROL & 7> c0=<vertex constant 0, four dwords> r2104=.. r210E=.. r2200=..
    r2201=.. r2202=.. r2205=.. r2105=.. r2106=.. r2107=.. r2108=..   (hex without 0x)
  S f=<gpu frame> a=<address>     a draw packet skipped by the bin predicate

Pairing. A capture row and a GPU line are the same draw when all of these hold:
  - the GPU frame is the capture frame plus one offset for the whole capture (the offset with
    the most confirmed pairs; the runner-up is printed);
  - the packet ends at the row's write pointer + 4 (the draw packet is the last thing the
    builder wrote) or 24 bytes before it (per-draw bin masks: seven more dwords follow);
  - the packet's index count is the one the builder's arguments give;
  - the GPU ran the row's vertex shader, or its vertex constant c0 has the row's bits (a depth
    pre-pass runs the same packet with other shaders).
A tiled draw executes more than once; every confirmed execution is compared. A packet at the
address that fails the last two tests is counted as not confirmed and never compared.

Per register: sampled rows (the row has the register), paired, equal (every execution holds
the device's value), unequal, and the same per execution; the first five mismatches with the
capture frame, the GPU frame and the GPU draw ordinal. Then the executions by GPU pass, the
GPU's draws in the tiled passes, and the census of the capture's decoded states with counts by
vertex and pixel shader (bit layouts: SDK registers.h; main-scene rows and the other rows
apart).

Exit code 1 if any register is unequal in any execution or no draw was paired, else 0.
"""
import argparse
import gzip
import json
import struct
import sys
from collections import Counter, defaultdict

# The registers of "rs", in the order the capture writes them. The first six are the state
# registers; 2105..2108 are the blend constant.
REGISTERS = ("2104", "210E", "2200", "2201", "2202", "2205", "2105", "2106", "2107", "2108")
REGISTER_NAMES = {
    "2104": "RB_COLOR_MASK", "210E": "RB_ALPHA_REF", "2200": "RB_DEPTHCONTROL",
    "2201": "RB_BLENDCONTROL0", "2202": "RB_COLORCONTROL", "2205": "PA_SU_SC_MODE_CNTL",
    "2105": "RB_BLEND_RED", "2106": "RB_BLEND_GREEN", "2107": "RB_BLEND_BLUE", "2108": "RB_BLEND_ALPHA",
}
# Packet end minus write pointer (see the docstring).
PACKET_END_DELTAS = (4, -24)
MAX_MISMATCHES = 5
OFFSET_SCAN_ROWS = 6000

COMPARE = ("never", "less", "equal", "lequal", "greater", "notequal", "gequal", "always")
FACTOR = {0: "zero", 1: "one", 4: "src_colour", 5: "inv_src_colour", 6: "src_alpha", 7: "inv_src_alpha",
          8: "dst_colour", 9: "inv_dst_colour", 10: "dst_alpha", 11: "inv_dst_alpha", 12: "const_colour",
          13: "inv_const_colour", 14: "const_alpha", 15: "inv_const_alpha", 16: "src_alpha_sat"}
BLEND_OP = {0: "add", 1: "subtract", 2: "min", 3: "max", 4: "rev_subtract"}
CONSTANT_FACTORS = (12, 13, 14, 15)


def gpu_address(cpu_virtual):
    """xdk_layout.h GpuAddress: the 0xE0000000 view is offset by 0x1000."""
    return (cpu_virtual & 0x1FFFFFFF) + (((cpu_virtual >> 20) + 0x200) & 0x1000)


def _f32_bits(x):
    try:
        return struct.unpack("<I", struct.pack("<f", x))[0]
    except (OverflowError, struct.error, TypeError):
        return None


def _hex(text):
    try:
        return int(text, 16)
    except (TypeError, ValueError):
        return None


def expected_count(func, args):
    """The index count of the DRAW_INDX packet the builder writes for these arguments (r4..r7)."""
    if len(args) < 4:
        return None
    if "DrawIndexedVertices" in func:
        return args[3]          # r7 index count
    if "DrawVertices" in func:
        return args[2]          # r6 vertex count
    if "8221C9C8" in func:
        return args[2]          # r6 patches
    if "82207C30" in func:
        return 4 * args[3]      # r7 patches, four tessellation factors each
    return None


def load_capture(path):
    """The capture's draw and tess rows, reduced to what the proof and the census read."""
    rows = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if not (line.startswith('{"kind":"draw"') or line.startswith('{"kind": "draw"')
                    or line.startswith('{"kind":"tess"') or line.startswith('{"kind": "tess"')):
                continue
            try:
                r = json.loads(line)
            except ValueError:
                continue  # a row cut off when the game was closed
            rs = r.get("rs")
            vs = r.get("vs_hash")
            if vs is None and isinstance(r.get("vs"), dict):
                vs = r["vs"].get("hash")
            consts = r.get("bank") or r.get("vconst")
            if not consts and isinstance(r.get("consts"), dict):
                consts = r["consts"].get("c0")
            c0 = tuple(_f32_bits(x) for x in consts[:4]) if consts and len(consts) >= 4 else None
            args = [_hex(a) for a in r.get("args", [])]
            rows.append({
                "kind": r["kind"], "frame": r.get("frame", 0), "func": r.get("func", ""),
                "count": None if None in args else expected_count(r.get("func", ""), args),
                "in_scene": bool(r.get("in_scene")),
                "rs": {k: _hex(v) for k, v in rs.items() if _hex(v) is not None} if isinstance(rs, dict) else None,
                "cmd": _hex(r.get("cmd")),
                "vs": _hex(vs) or None,  # hash 0: the shader was not readable
                "ps": _hex(r.get("ps_hash")) or None,
                "c0": None if c0 is None or None in c0 else c0,
            })
    return rows


def packet_ends(row):
    """The physical addresses a draw packet of this row can end at."""
    if row["cmd"] is None:
        return ()
    base = gpu_address(row["cmd"])
    return tuple(base + d for d in PACKET_END_DELTAS)


def load_gpu(path, wanted):
    """(draws, skipped, passes, frames): draws[frame][address] = executions (only addresses in
    `wanted` are kept), skipped[frame] = addresses, passes = every logged draw by
    (pitch, bin select, mode, colour mask), frames = the GPU frames logged."""
    draws = defaultdict(lambda: defaultdict(list))
    skipped = defaultdict(set)
    passes = Counter()
    frames = set()
    opener = gzip.open if str(path).endswith(".gz") else open
    with opener(path, "rt", encoding="ascii", errors="replace") as f:
        for line in f:
            if line.startswith("D "):
                try:
                    d = dict(kv.split("=", 1) for kv in line.split()[1:])
                    frame, addr = int(d["f"]), int(d["a"], 16)
                    bs = int(d.get("bs", "0"), 16) & 0xFFFFFFFF
                    regs = {k: int(d["r" + k], 16) for k in REGISTERS if "r" + k in d}
                    ex = {"d": int(d["d"]), "n": int(d["n"]), "bs": bs, "vs": int(d["vs"], 16),
                          "ps": int(d["ps"], 16), "pitch": int(d["pitch"]), "mode": int(d["mode"]),
                          "c0": tuple(int(x, 16) for x in d["c0"].split(",")), "regs": regs}
                except (KeyError, ValueError):
                    continue  # a line cut off when the game was closed
                frames.add(frame)
                passes[(ex["pitch"], bs, ex["mode"], regs.get("2104"))] += 1
                if addr in wanted:
                    draws[frame][addr].append(ex)
            elif line.startswith("S "):
                try:
                    d = dict(kv.split("=", 1) for kv in line.split()[1:])
                    frame, addr = int(d["f"]), int(d["a"], 16)
                except (KeyError, ValueError):
                    continue
                frames.add(frame)
                if addr in wanted:
                    skipped[frame].add(addr)
    return draws, skipped, passes, frames


def confirmed(row, ex):
    """The GPU line is this row's draw: its index count, and its shader or its c0."""
    if row["count"] is None or ex["n"] != row["count"]:
        return False
    return (row["vs"] is not None and ex["vs"] == row["vs"]) or (row["c0"] is not None and ex["c0"] == row["c0"])


def executions_at(row, frame_draws):
    """The GPU lines at the row's packet address in one GPU frame ([] if none)."""
    for addr in packet_ends(row):
        found = frame_draws.get(addr)
        if found:
            return found
    return []


def find_frame_offset(rows, draws, frames):
    """(offset, confirmed rows at it, runner-up offset, confirmed rows at the runner-up, rows
    probed); offset None when no offset confirms a row."""
    probe = [r for r in rows if r["rs"] is not None and r["cmd"] is not None]
    step = max(1, len(probe) // OFFSET_SCAN_ROWS)
    probe = probe[::step]
    if not probe or not frames:
        return None, 0, None, 0, len(probe)
    row_frames = [r["frame"] for r in probe]
    scores = []
    for offset in range(min(frames) - max(row_frames), max(frames) - min(row_frames) + 1):
        hits = 0
        for r in probe:
            frame_draws = draws.get(r["frame"] + offset)
            if frame_draws and any(confirmed(r, ex) for ex in executions_at(r, frame_draws)):
                hits += 1
        scores.append((hits, -offset))
    scores.sort(reverse=True)
    best = scores[0]
    second = scores[1] if len(scores) > 1 else (0, 0)
    if best[0] == 0:
        return None, 0, None, 0, len(probe)
    return -best[1], best[0], -second[1], second[0], len(probe)


def decode_alpha(word):
    if word & 0x8:
        text = f"alpha test on, function {word & 7} ({COMPARE[word & 7]})"
    else:
        text = "alpha test off"
    return text + (", alpha to mask" if word & 0x10 else "")


def _factor(v):
    return FACTOR.get(v, f"factor{v}")


def _op(v):
    return BLEND_OP.get(v, f"op{v}")


def blend_fields(word):
    """(colour source, colour operation, colour destination, alpha source, operation, destination)."""
    return (word & 0x1F, (word >> 5) & 7, (word >> 8) & 0x1F,
            (word >> 16) & 0x1F, (word >> 21) & 7, (word >> 24) & 0x1F)


def blend_is_off(word):
    return blend_fields(word) == (1, 0, 0, 1, 0, 0)


def decode_blend(word):
    cs, co, cd, as_, ao, ad = blend_fields(word)
    if blend_is_off(word):
        return "blend off (one add zero)"
    return (f"blend colour {_factor(cs)} {_op(co)} {_factor(cd)}, "
            f"alpha {_factor(as_)} {_op(ao)} {_factor(ad)}")


def uses_blend_constant(word):
    cs, _, cd, as_, _, ad = blend_fields(word)
    return any(f in CONSTANT_FACTORS for f in (cs, cd, as_, ad))


def decode_depth(word):
    func = (word >> 4) & 7
    return (f"depth test {'on' if word & 2 else 'off'}, write {'on' if word & 4 else 'off'}, "
            f"function {func} ({COMPARE[func]})")


def decode_cull(word):
    mode = ("none", "front", "back", "front and back")[word & 3]
    return f"cull {mode}, front face {'cw' if word & 4 else 'ccw'}"


def decode_mask(word):
    return "colour mask " + "".join(c if word & (1 << i) else "-" for i, c in enumerate("RGBA"))


def decode_alpha_ref(word):
    return f"alpha reference {struct.unpack('<f', struct.pack('<I', word))[0]:.6g}"


def _shader(row):
    vs = f"0x{row['vs']:016X}" if row["vs"] is not None else "?"
    ps = f"0x{row['ps']:016X}" if row["ps"] is not None else "?"
    return f"vs {vs} ps {ps}"


def _print_table(title, counts, by_shader, top):
    """One census table: each value with its row count and its largest shader pairs."""
    print(f"  {title}")
    width = max((len(k) for k in counts), default=0) + 2
    for value, n in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"    {value:<{width}}{n}")
        shaders = sorted(by_shader[value].items(), key=lambda kv: (-kv[1], kv[0]))
        for shader, m in shaders[:top]:
            print(f"        {shader}  {m}")
        if len(shaders) > top:
            print(f"        (+{len(shaders) - top} more shader pairs, {sum(m for _, m in shaders[top:])} rows)")


def print_census(title, rows, top, combinations):
    print(f"{title} ({len(rows)} rows)")
    if not rows:
        return
    tables = (
        ("alpha test (RB_COLORCONTROL 0x2202)", "2202", decode_alpha),
        ("blend (RB_BLENDCONTROL0 0x2201)", "2201", decode_blend),
        ("depth (RB_DEPTHCONTROL 0x2200)", "2200", decode_depth),
        ("cull (PA_SU_SC_MODE_CNTL 0x2205)", "2205", decode_cull),
        ("colour mask (RB_COLOR_MASK 0x2104, target 0)", "2104", lambda w: decode_mask(w & 0xF)),
    )
    for name, reg, decode in tables:
        counts, by_shader = Counter(), defaultdict(Counter)
        for r in rows:
            if reg in r["rs"]:
                value = decode(r["rs"][reg])
                counts[value] += 1
                by_shader[value][_shader(r)] += 1
        _print_table(name, counts, by_shader, top)
    counts, by_shader = Counter(), defaultdict(Counter)
    for r in rows:
        if r["rs"].get("2202", 0) & 0x8 and "210E" in r["rs"]:
            value = decode_alpha_ref(r["rs"]["210E"])
            counts[value] += 1
            by_shader[value][_shader(r)] += 1
    _print_table("alpha reference of the rows with alpha test on (RB_ALPHA_REF 0x210E)", counts, by_shader, top)
    with_blend = [r for r in rows if "2201" in r["rs"]]
    constant = [r for r in with_blend if uses_blend_constant(r["rs"]["2201"])]
    print(f"  blend constant used by {len(constant)} of {len(with_blend)} rows "
          "(a constant-colour or constant-alpha factor)")
    values = Counter(tuple(r["rs"].get(k) for k in ("2105", "2106", "2107", "2108")) for r in rows)
    for value, n in values.most_common(4):
        print("    blend constant registers " +
              " ".join("?" if v is None else f"0x{v:08X}" for v in value) + f"  {n}")
    opaque = [r for r in rows if "2201" in r["rs"] and blend_is_off(r["rs"]["2201"])]
    print(f"  rows with blend off: {len(opaque)}")
    for reg, decode in (("2200", decode_depth), ("2205", decode_cull)):
        for value, n in Counter(decode(r["rs"][reg]) for r in opaque if reg in r["rs"]).most_common():
            print(f"    {value}  {n}")
    if not combinations:
        return
    keys = ("2200", "2201", "2202", "2205", "2104")
    combos = defaultdict(list)
    for r in rows:
        if all(k in r["rs"] for k in keys):
            combos[tuple(r["rs"][k] for k in keys)].append(r)
    print(f"  state combinations ({len(combos)} distinct)")
    for words, group in sorted(combos.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        print("    " + " ".join(f"{k}=0x{w:08X}" for k, w in zip(keys, words)) + f"  {len(group)}")
        kinds = Counter(r["kind"] for r in group)
        shaders = Counter(_shader(r) for r in group)
        print(f"        {decode_depth(words[0])}; {decode_blend(words[1])}; {decode_alpha(words[2])}; "
              f"{decode_cull(words[3])}; {decode_mask(words[4] & 0xF)}")
        print(f"        draw rows {kinds.get('draw', 0)}, tess rows {kinds.get('tess', 0)}; "
              f"{len(shaders)} shader pairs, most rows: {shaders.most_common(1)[0][0]}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", help="native_discovery_*.jsonl with \"rs\" and \"cmd\" in its draw and tess rows")
    ap.add_argument("--gpu", required=True, help="the GPU state log (plain or .gz)")
    ap.add_argument("--top", type=int, default=4, help="shader pairs listed per census value (default 4)")
    args = ap.parse_args(argv)

    rows = load_capture(args.capture)
    sampled = [r for r in rows if r["rs"] is not None]
    print(f"capture: rows {len(rows)}, sampled {len(sampled)}, not sampled {len(rows) - len(sampled)} "
          "(draw and tess rows; not sampled = no \"rs\")")
    wanted = {a for r in sampled for a in packet_ends(r)}
    draws, skipped, passes, frames = load_gpu(args.gpu, wanted)
    print(f"GPU log: {sum(passes.values())} draws in {len(frames)} frames"
          + (f" ({min(frames)}..{max(frames)})" if frames else ""))

    offset, hits, second, second_hits, probed = find_frame_offset(sampled, draws, frames)
    if offset is None:
        print(f"frame offset: none confirms a row ({probed} rows probed)")
    else:
        print(f"frame offset: GPU frame = capture frame + {offset} ({hits} of {probed} probed rows confirmed; "
              f"runner-up + {second}: {second_hits})")

    # Pair every sampled row and compare every confirmed execution.
    status = Counter()
    stats = {reg: Counter() for reg in REGISTERS}
    mismatches = {reg: [] for reg in REGISTERS}
    by_pass = defaultdict(lambda: {"executions": 0, "unequal": Counter()})
    executions = unconfirmed = 0
    per_row = Counter()
    confirmed_by = Counter()
    for r in sampled:
        for reg in REGISTERS:
            if reg in r["rs"]:
                stats[reg]["sampled"] += 1
        if r["cmd"] is None:
            status["no command pointer"] += 1
            continue
        gpu_frame = None if offset is None else r["frame"] + offset
        found = executions_at(r, draws.get(gpu_frame, {})) if gpu_frame is not None else []
        if not found:
            if gpu_frame is not None and gpu_frame not in frames:
                status["frame outside the GPU log"] += 1
            elif gpu_frame is not None and any(a in skipped.get(gpu_frame, ()) for a in packet_ends(r)):
                status["skipped by the bin predicate"] += 1
            else:
                status["no packet at the address"] += 1
            continue
        mine = [ex for ex in found if confirmed(r, ex)]
        unconfirmed += len(found) - len(mine)
        if not mine:
            status["packet at the address not confirmed"] += 1
            continue
        status["paired"] += 1
        executions += len(mine)
        per_row[len(mine)] += 1
        row_unequal = set()
        for ex in mine:
            own = r["vs"] is not None and ex["vs"] == r["vs"]
            same_c0 = r["c0"] is not None and ex["c0"] == r["c0"]
            confirmed_by[" and ".join(n for n, ok in (("vertex shader", own), ("c0", same_c0)) if ok)] += 1
            key = (ex["pitch"], ex["bs"], ex["mode"], "own" if own else "other")
            by_pass[key]["executions"] += 1
            for reg in REGISTERS:
                if reg not in r["rs"] or reg not in ex["regs"]:
                    continue
                if r["rs"][reg] == ex["regs"][reg]:
                    stats[reg]["executions equal"] += 1
                    continue
                stats[reg]["executions unequal"] += 1
                by_pass[key]["unequal"][reg] += 1
                row_unequal.add(reg)
                if len(mismatches[reg]) < MAX_MISMATCHES:
                    mismatches[reg].append(
                        f"mismatch {reg}: frame {r['frame']} (GPU frame {gpu_frame}, draw {ex['d']}) "
                        f"{r['func']} {_shader(r)}: device 0x{r['rs'][reg]:08X}, GPU 0x{ex['regs'][reg]:08X} "
                        f"(pitch {ex['pitch']}, bin select {ex['bs']:08X}, mode {ex['mode']}, "
                        f"GPU vs 0x{ex['vs']:016X} ps 0x{ex['ps']:016X})")
        for reg in REGISTERS:
            if reg in r["rs"]:
                stats[reg]["paired"] += 1
                stats[reg]["unequal" if reg in row_unequal else "equal"] += 1

    paired = status["paired"]
    share = 100.0 * paired / len(sampled) if sampled else 0.0
    print(f"pairing: paired {paired} of {len(sampled)} sampled rows ({share:.2f}%)")
    for name in ("no command pointer", "frame outside the GPU log", "skipped by the bin predicate",
                 "no packet at the address", "packet at the address not confirmed"):
        print(f"  {name} {status[name]}")
    print(f"executions {executions} (compared), by executions per row: "
          + (", ".join(f"{k}: {v} rows" for k, v in sorted(per_row.items())) or "-")
          + f"; lines at a paired address not confirmed and not compared: {unconfirmed}")
    print("  confirmed by index count and: "
          + (", ".join(f"{k} {v}" for k, v in sorted(confirmed_by.items())) or "-"))

    print("registers (rows; a row is equal when every execution holds the device's value):")
    unequal = []
    for reg in REGISTERS:
        s = stats[reg]
        print(f"register {reg} {REGISTER_NAMES[reg]}: sampled {s['sampled']}, paired {s['paired']}, "
              f"equal {s['equal']}, unequal {s['unequal']}; "
              f"executions {s['executions equal'] + s['executions unequal']}, equal {s['executions equal']}, "
              f"unequal {s['executions unequal']}")
        if s["executions unequal"]:
            unequal.append(reg)
    for reg in REGISTERS:
        for line in mismatches[reg]:
            print(line)

    print("executions by GPU pass (shader own = the GPU ran the row's vertex shader):")
    for key in sorted(by_pass):
        pitch, bs, mode, shader = key
        bad = by_pass[key]["unequal"]
        text = ", ".join(f"{reg}: {bad[reg]}" for reg in REGISTERS if bad[reg]) or "-"
        print(f"  pitch {pitch} bin select {bs:08X} mode {mode} shader {shader}  "
              f"executions {by_pass[key]['executions']}  unequal {text}")
    print("GPU draws in tiled passes (every logged draw with a bin select other than FFFFFFFF):")
    totals = defaultdict(Counter)
    for (pitch, bs, mode, mask), n in passes.items():
        if bs != 0xFFFFFFFF:
            totals[(pitch, bs, mode)]["?" if mask is None else f"0x{mask:08X}"] += n
    for key in sorted(totals):
        masks = ", ".join(f"{m}: {n}" for m, n in sorted(totals[key].items()))
        print(f"  pitch {key[0]} bin select {key[1]:08X} mode {key[2]}  draws {sum(totals[key].values())}  "
              f"colour mask {masks}")

    print_census("census, main scene", [r for r in sampled if r["in_scene"]], args.top, True)
    print_census("census, other passes", [r for r in sampled if not r["in_scene"]], args.top, False)

    if not paired:
        print("RESULT: no draw was paired")
        return 1
    if unequal:
        print("RESULT: UNEQUAL registers: " + ", ".join(unequal))
        return 1
    print("RESULT: every register equal in every paired draw")
    return 0


if __name__ == "__main__":
    sys.exit(main())
