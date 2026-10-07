"""Prove the capture's shader constants against the GPU's register file.

Usage: python tools\\xdk_sigmatch\\pconst_check.py <native_discovery_*.jsonl> <gpu_pconst.log[.gz]>
           [--frame N] [--key pfull|pbank|vfull|vbank] [--dumps <shader dump folder>] [--values LIST]

The capture's "draw" and "tess" rows carry (frame-map section 14, "Pixel constants"):
  "pfull"       the 256 pixel registers as the capture's reader gives them at the draw: 8192 hex
                digits, register r at [32 r, 32 r + 32) as the bits of x, y, z, w; a register that
                was not read is 32 dashes (one the draw's pixel shader loads itself, or one whose
                memory could not be read)
  "ptiles"      the loads issued under a bin mask that does not cover every bin, in the order they
                were issued: [{"mask": "0x..", "start": first register, "v": hex of the registers}]
  "pbank_diff"  {register: hex} for the registers whose value in the device's constant bank
                (device + 0x1780) is another one than "pfull" has; absent when the bank was not read
  "vfull", "vtiles", "vbank_diff"   the same three for the vertex bank (device + 0x780)
  "pbool"       the device's pixel bool word (b128..b159)
  "pbool_load"  that word as its last writer on the draw's thread put it on the GPU
and "rs", "cmd", the shader hashes and the vertex constant c0 that render_state_check.py pairs by.
Its "aluload" rows are the calls of LoadAluConstants (bank, first register, count, the buffer's
data address and first register, the predicate and bin mask in force, the command write pointer).

The GPU log comes from the temporary SDK diagnostic of that section: one line per DRAW_INDX
packet the command processor hands to the backend, and one per LOAD_ALU_CONSTANT packet:

  D f=<gpu frame> d=<draw ordinal> a=<physical address just past the packet> pt=<primitive>
    n=<index count> bs=<bin select> bm=<bin mask> vs=<hash> ps=<hash> pitch=<surface pitch>
    mode=<RB_MODECONTROL & 7> c0=<vertex constant 0> b=<the eight bool words, 0x4900..0x4907>
    [full=1] P<r>=<32 hex digits> ...
  S f=<gpu frame> a=<address>      a draw packet skipped by the bin predicate
  L f=<gpu frame> a=<address just past the packet> x=<1 executed, 0 skipped by the predicate>
    bs=<bin select> bm=<bin mask> [src=<physical address> idx=<dword index> t=<type> n=<dwords>]

P<r> is pixel register r (register file 0x4400 + 4 r, four dwords). A line with full=1 carries
all 256; any other line only the registers that changed since the frame's previous D line. The
first D line of a frame is a full one; lines before a frame's first full line cannot be
reconstructed and are not used. V<r> is vertex register r (0x4000 + 4 r) in the same way, in
logs made with FABLE2_GPU_PCONST_VERTEX=1; the vertex keys read those.

Pairing is render_state_check.py's: the packet's address within the frame, the GPU frame being
the capture frame plus one offset for the whole capture, confirmed by the index count and the
vertex shader or c0. Every confirmed execution of a row is compared, register by register and
bit by bit.

What a register is compared with. With --key pfull (the default): the row's "pfull", except
that a load of "ptiles" whose mask intersects the execution's bin select replaces it (the last
such load in the list; the GPU executes a predicated load only in those passes). With
--key pbank: the device bank ("pfull" with "pbank_diff" applied), without the tile loads.
--key vfull and vbank do the same with the vertex bank's rows and the log's V tokens.

Passes. An execution whose bin select has no colour bin is a depth pre-pass; it is compared and
reported apart and never decides. Rows outside the main scene are reported apart as well.

Registers the shader does not read. The GPU replays a tile's commands, so a register keeps, for
the first draws of a replay, the value the replay before it ended with unless the game loads it
again; the game loads a light set only for a draw whose shader uses one. With --dumps (a
--dump_shaders folder), a difference in a register that the pixel shader the GPU ran does not
name in its microcode (shader_<HASH>.ucode.frag) is counted apart ("different where the pixel
shader does not read it") and does not decide; for the vertex keys the vertex shader and its
.ucode.vert, where c[N+a0] (a palette) counts as every register from N on. Without the option,
or without that shader's dump, every difference counts.

Output: pairing, one line per register (main-scene colour executions compared / equal /
different / not compared, then the pre-pass and the other passes), up to three mismatches per
register with the values, the registers that differ, the pixel bool word against the GPU's
(register 0x4904) for both bool keys with the bits that differ, the load rows against the GPU's
LOAD_ALU_CONSTANT packets (a row's packet ends at its command write pointer + 4 and must name
the row's buffer, register and size; the executed packets no row stands for are listed by
register range: the shaders' literals), and with --values the GPU's values of the listed
registers.

Exit code 0, with "RESULT: OK", only when every main-scene row in a frame of the GPU log is
paired, at least one main-scene colour execution was compared, and every register of the light
block (light_block.h: c0, c19, c20, c21, c22, c23, c80, c87, c94, c109, c112) was read in every
main-scene row and is equal in every main-scene colour execution (with --dumps: in every one
whose pixel shader reads it). With a vertex key there is no light block: every one of the 256
registers must be equal there. Otherwise 1, with "RESULT: FAIL".
"""
import argparse
import gzip
import json
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import render_state_check as rsc  # noqa: E402

REGISTERS = 256
REG_DIGITS = 32
LIGHT_BLOCK = (0, 19, 20, 21, 22, 23, 80, 87, 94, 109, 112)
# The pixel bools the scene shaders test: b128 (colour division of the prop shaders), b129, b132,
# b133 / b134 (emissive and cube blocks), b135 (the per-light shadow map), b136 (water).
BOOL_BITS = (128, 129, 132, 133, 134, 135, 136)
MAX_MISMATCHES = 3
_HEX = set("0123456789ABCDEFabcdef")


def parse_registers(text):
    """The 256 registers of a "pfull" string as 32-digit hex strings (None: not read); None for
    a string of another length or with other characters."""
    if not isinstance(text, str) or len(text) != REGISTERS * REG_DIGITS:
        return None
    out = []
    for r in range(REGISTERS):
        v = text[r * REG_DIGITS:(r + 1) * REG_DIGITS]
        if v == "-" * REG_DIGITS:
            out.append(None)
        elif set(v) <= _HEX:
            out.append(v.upper())
        else:
            return None
    return out


def floats(reg_hex):
    """The four floats of a register's 32 hex digits."""
    return struct.unpack(">4f", bytes.fromhex(reg_hex))


def _show(reg_hex, digits=9):
    if reg_hex is None:
        return "(not read)"
    return "(" + ", ".join(f"{x:.{digits}g}" for x in floats(reg_hex)) + ")"


def parse_register_list(text):
    """"0,19-21,87" -> [0, 19, 20, 21, 87]; ValueError outside 0..255."""
    out = []
    for part in text.split(","):
        lo, _, hi = part.strip().partition("-")
        lo, hi = int(lo), int(hi) if hi else int(lo)
        if not 0 <= lo <= hi < REGISTERS:
            raise ValueError(f"register list: {part!r} is outside 0..{REGISTERS - 1}")
        out.extend(range(lo, hi + 1))
    return out


def _split_registers(text):
    """A hex string of whole registers as a list; None if it is not one."""
    if not isinstance(text, str) or not text or len(text) % REG_DIGITS or not set(text) <= _HEX:
        return None
    return [text[i:i + REG_DIGITS].upper() for i in range(0, len(text), REG_DIGITS)]


class ShaderReads:
    """Which constant registers and bool constants a shader's microcode names, from its dump
    shader_<HASH>.ucode.frag (pixel) or .ucode.vert (vertex) in a --dump_shaders folder.
    reads(hash) is (registers, bools), or None when that cannot be told: no folder, no dump, or
    an indexed constant in a form other than c[N+a0]. c[N+a0] (a palette) is taken to read every
    register from N on. A draw without the shader (hash 0) reads nothing."""

    def __init__(self, folder, vertex=False):
        self.folder = Path(folder) if folder else None
        self.suffix = ".ucode.vert" if vertex else ".ucode.frag"
        self.cache = {}
        self.unknown = set()

    def reads(self, ps_hash):
        if self.folder is None:
            return None
        if ps_hash == 0:
            return frozenset(), frozenset()
        if ps_hash not in self.cache:
            found = None
            try:
                text = (self.folder / f"shader_{ps_hash:016X}{self.suffix}").read_text(errors="replace")
            except OSError:
                text = None
            if text is not None and text.count("c[") == len(re.findall(r"\bc\[\d+\+a0\]", text)):
                named = {int(m) for m in re.findall(r"\bc(\d+)\b", text)}
                for base in re.findall(r"\bc\[(\d+)\+a0\]", text):
                    named.update(range(int(base), REGISTERS))
                found = (frozenset(named), frozenset(int(m) for m in re.findall(r"\bb(\d+)\b", text)))
            if found is None:
                self.unknown.add(ps_hash)
            self.cache[ps_hash] = found
        return self.cache[ps_hash]


def _load_row(r):
    """An "aluload" row reduced to what the packet comparison reads; None if it lacks a field."""
    try:
        out = {"frame": int(r["frame"]), "bank": int(r["bank"]), "start": int(r["start"]),
               "count": int(r["count"]), "first": int(r["first"]), "pred": int(r.get("pred", -1)),
               "data": rsc._hex(r.get("data")), "mask": rsc._hex(r.get("bin_mask")), "cmd": rsc._hex(r.get("cmd"))}
    except (KeyError, TypeError, ValueError):
        return None
    return None if out["data"] is None or out["cmd"] is None else out


def load_capture(path, key, frame=None):
    """(rows, loads). rows: the draw and tess rows, reduced as render_state_check.load_capture
    reduces them (so that its pairing applies), with the pixel registers of `key` under "P"
    (None: not sampled). loads: the "aluload" rows."""
    rows, loads = [], []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith('{"kind":"aluload"') or line.startswith('{"kind": "aluload"'):
                try:
                    load = _load_row(json.loads(line))
                except ValueError:
                    continue
                if load is not None and (frame is None or load["frame"] == frame):
                    loads.append(load)
                continue
            if not (line.startswith('{"kind":"draw"') or line.startswith('{"kind": "draw"')
                    or line.startswith('{"kind":"tess"') or line.startswith('{"kind": "tess"')):
                continue
            try:
                r = json.loads(line)
            except ValueError:
                continue  # a row cut off when the game was closed
            if frame is not None and r.get("frame") != frame:
                continue
            rs = r.get("rs")
            vs = r.get("vs_hash")
            if vs is None and isinstance(r.get("vs"), dict):
                vs = r["vs"].get("hash")
            consts = r.get("bank") or r.get("vconst")
            if not consts and isinstance(r.get("consts"), dict):
                consts = r["consts"].get("c0")
            c0 = tuple(rsc._f32_bits(x) for x in consts[:4]) if consts and len(consts) >= 4 else None
            args = [rsc._hex(a) for a in r.get("args", [])]
            bank = key[0]  # "p" pixel, "v" vertex
            regs = parse_registers(r.get(bank + "full"))
            tiles = []
            if regs is not None and key.endswith("bank"):
                diff = r.get(bank + "bank_diff")
                if not isinstance(diff, dict):
                    regs = None  # the bank was not read: nothing to compare
                for name, value in (diff.items() if regs is not None else ()):
                    one = _split_registers(value)
                    if name.isdigit() and int(name) < REGISTERS and one and len(one) == 1:
                        regs[int(name)] = one[0]
            elif regs is not None:
                for t in r.get(bank + "tiles") or ():
                    mask, values = rsc._hex(t.get("mask")), _split_registers(t.get("v"))
                    start = t.get("start")
                    if mask is None or values is None or not isinstance(start, int):
                        continue
                    if 0 <= start and start + len(values) <= REGISTERS:
                        tiles.append((mask, start, values))
            rows.append({
                "kind": r["kind"], "frame": r.get("frame", 0), "func": r.get("func", ""),
                "count": None if None in args else rsc.expected_count(r.get("func", ""), args),
                "in_scene": bool(r.get("in_scene")),
                "rs": {k: rsc._hex(v) for k, v in rs.items() if rsc._hex(v) is not None}
                if isinstance(rs, dict) else None,
                "cmd": rsc._hex(r.get("cmd")),
                "vs": rsc._hex(vs) or None, "ps": rsc._hex(r.get("ps_hash")) or None,
                "c0": None if c0 is None or None in c0 else c0,
                "P": regs, "tiles": tiles, "pbool": rsc._hex(r.get("pbool")),
                "pbool_load": rsc._hex(r.get("pbool_load")),
            })
    return rows, loads


def load_gpu(path, wanted, letter="P"):
    """(draws, skipped, frames, per_frame, unusable, loads): draws[frame][address] = executions
    (only the addresses in `wanted` are kept), each with the 256 registers of one bank under "P"
    (`letter`: the tokens read, "P" pixel or "V" vertex);
    per_frame[frame] = [D lines, D lines at a wanted address]; unusable = D lines before their
    frame's first full line; loads[frame][address] = the L lines (LOAD_ALU_CONSTANT packets of
    type 0) as (executed, bin select, source address, dword index, dwords)."""
    draws = defaultdict(lambda: defaultdict(list))
    skipped = defaultdict(set)
    per_frame = defaultdict(lambda: [0, 0])
    loads = defaultdict(lambda: defaultdict(list))
    frames = set()
    unusable = 0
    state, state_frame = None, None
    opener = gzip.open if str(path).endswith(".gz") else open
    with opener(path, "rt", encoding="ascii", errors="replace") as f:
        for line in f:
            if line.startswith("D "):
                try:
                    d, changes = {}, []
                    for token in line.split()[1:]:
                        k, v = token.split("=", 1)
                        if k[0] in "PV" and k[1:].isdigit():
                            if k[0] != letter:
                                continue
                            if len(v) != REG_DIGITS or not set(v) <= _HEX:
                                raise ValueError(token)
                            changes.append((int(k[1:]), v.upper()))
                        else:
                            d[k] = v
                    frame, addr = int(d["f"]), int(d["a"], 16)
                    bools = [int(x, 16) for x in d["b"].split(",")]
                    ex = {"d": int(d["d"]), "n": int(d["n"]), "bs": int(d["bs"], 16) & 0xFFFFFFFF,
                          "bm": int(d.get("bm", "0"), 16) & 0xFFFFFFFF, "vs": int(d["vs"], 16),
                          "ps": int(d["ps"], 16), "pitch": int(d["pitch"]), "mode": int(d["mode"]),
                          "c0": tuple(int(x, 16) for x in d["c0"].split(",")), "pb": bools[4]}
                    if any(not 0 <= r < REGISTERS for r, _ in changes):
                        raise ValueError(line)
                except (KeyError, ValueError, IndexError):
                    continue  # a line cut off when the game was closed
                frames.add(frame)
                if frame != state_frame:
                    state, state_frame = None, frame
                if d.get("full") == "1" and len({r for r, _ in changes}) == REGISTERS:
                    state = [None] * REGISTERS
                if state is None:
                    unusable += 1
                    continue
                for r, v in changes:
                    state[r] = v
                per_frame[frame][0] += 1
                if addr in wanted:
                    per_frame[frame][1] += 1
                    ex["P"] = tuple(state)
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
            elif line.startswith("L "):
                try:
                    d = dict(kv.split("=", 1) for kv in line.split()[1:])
                    frame, addr, executed = int(d["f"]), int(d["a"], 16), d["x"] == "1"
                    bs = int(d["bs"], 16) & 0xFFFFFFFF
                    if executed and int(d["t"]) != 0:
                        continue  # fetch, bool or loop constants: not ALU registers
                    fields = (int(d["src"], 16), int(d["idx"], 16), int(d["n"])) if executed else (None, None, None)
                except (KeyError, ValueError):
                    continue
                frames.add(frame)
                loads[frame][addr].append((executed, bs) + fields)
    return draws, skipped, frames, per_frame, unusable, loads


def _load_name(bank, start, count):
    return f"{'pixel' if bank else 'vertex'} c{start}..c{start + count - 1}"


def print_loads(capture_loads, gpu_loads, offset, frames):
    """The "aluload" rows against the GPU's LOAD_ALU_CONSTANT packets: a row's packet ends at its
    command write pointer + 4; an executed packet must name the row's buffer (GpuAddress of its
    data + 16 * first register), its register (dword index ((bank << 8) + start) * 4) and its
    size (4 * count dwords)."""
    status = Counter()
    kinds = defaultdict(lambda: {"rows": 0, "executed": Counter(), "skipped": Counter()})
    differs = []
    used = defaultdict(set)
    for row in capture_loads:
        gpu_frame = None if offset is None else row["frame"] + offset
        addr = rsc.gpu_address(row["cmd"]) + 4
        found = gpu_loads.get(gpu_frame, {}).get(addr) if gpu_frame is not None else None
        if not found:
            status["outside the GPU log" if gpu_frame is not None and gpu_frame not in frames
                   else "without a packet"] += 1
            continue
        status["paired"] += 1
        used[gpu_frame].add(addr)
        want = (rsc.gpu_address(row["data"] + 16 * row["first"]) & 0x3FFFFFFF,
                ((row["bank"] << 8) + row["start"]) * 4, 4 * row["count"])
        executed = [ex for ex in found if ex[0]]
        same = all(ex[2:] == want for ex in executed)
        status["never executed" if not executed else "packet as the row says" if same else "another packet"] += 1
        if not same and len(differs) < MAX_MISMATCHES:
            bad = next(ex for ex in executed if ex[2:] != want)
            differs.append(f"load differs: frame {row['frame']} (GPU frame {gpu_frame}) "
                           f"{_load_name(row['bank'], row['start'], row['count'])}: the row says source "
                           f"{want[0]:08X} index {want[1]:04X} dwords {want[2]}, the packet {bad[2]:08X} "
                           f"{bad[3]:04X} {bad[4]}")
        mask = None if row["pred"] != 1 else row["mask"]
        kind = kinds[(row["bank"], row["start"], row["count"], mask)]
        kind["rows"] += 1
        for ex in found:
            kind["executed" if ex[0] else "skipped"][ex[1]] += 1
    print(f"constant loads: rows {len(capture_loads)}, paired {status['paired']}, packet as the row says "
          f"{status['packet as the row says']}, another packet {status['another packet']}, "
          f"never executed {status['never executed']}, without a packet {status['without a packet']}, "
          f"outside the GPU log {status['outside the GPU log']}")
    for line in differs:
        print(line)
    for (bank, start, count, mask), kind in sorted(kinds.items(), key=lambda kv: (kv[0][0], kv[0][1], kv[0][3] or 0)):
        def selects(counter):
            return f"{sum(counter.values())}" + (
                " (bin selects " + " ".join(f"{b:08X}" for b in sorted(counter)) + ")" if counter else "")
        print(f"  load {_load_name(bank, start, count)} ({count}), "
              + ("every bin" if mask is None else f"predicated under {mask:08X}")
              + f": rows {kind['rows']}, executed {selects(kind['executed'])}, skipped {selects(kind['skipped'])}")
    # Executed packets no row stands for: uploads of another path (a shader's literals).
    others = Counter()
    row_frames = {row["frame"] + offset for row in capture_loads} if offset is not None else set()
    for frame in row_frames:
        for addr, found in gpu_loads.get(frame, {}).items():
            if addr in used[frame]:
                continue
            for ex in found:
                if ex[0]:
                    others[(ex[3] >= 0x400, (ex[3] % 0x400) // 4, ex[4] // 4)] += 1
    print(f"GPU loads without a row: {sum(others.values())} (executed LOAD_ALU_CONSTANT packets in the frames "
          "of the load rows)")
    for (bank, start, count), n in sorted(others.items()):
        print(f"  {_load_name(bank, start, count)} ({count}): {n}")


def expected(row, reg, bin_select):
    """The row's value of `reg` as an execution under `bin_select` should see it."""
    value = row["P"][reg]
    for mask, start, values in row["tiles"]:
        if mask & bin_select and start <= reg < start + len(values):
            value = values[reg - start]
    return value


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", help="native_discovery_*.jsonl with \"pfull\" in its draw and tess rows")
    ap.add_argument("gpu", help="the GPU pixel-constant log (plain or .gz)")
    ap.add_argument("--frame", type=int, help="only this capture frame")
    ap.add_argument("--key", choices=("pfull", "pbank", "vfull", "vbank"), default="pfull",
                    help="pfull: the capture's reader of the pixel bank (default); pbank: the device's pixel "
                    "bank; vfull, vbank: the same for the vertex bank")
    ap.add_argument("--values", type=parse_register_list, default=[],
                    help="registers whose GPU values are listed, e.g. 0,19-23,80,87")
    ap.add_argument("--dumps", help="a --dump_shaders folder: a difference in a register the pixel shader "
                    "the GPU ran does not read is counted apart and does not decide")
    args = ap.parse_args(argv)
    vertex = args.key[0] == "v"
    shader_reads = ShaderReads(args.dumps, vertex)
    shader_key, shader_name = ("vs", "vertex") if vertex else ("ps", "pixel")
    decisive = tuple(range(REGISTERS)) if vertex else LIGHT_BLOCK

    rows, capture_loads = load_capture(args.capture, args.key, args.frame)
    sampled = [r for r in rows if r["P"] is not None and r["rs"] is not None]
    print(f"capture: rows {len(rows)}, sampled {len(sampled)}, not sampled {len(rows) - len(sampled)} "
          f"(draw and tess rows; not sampled = no \"{args.key[0]}full\""
          + (f" or no \"{args.key}_diff\"" if args.key.endswith("bank") else "") + f"); compared: {args.key}"
          + (f"; capture frame {args.frame} only" if args.frame is not None else ""))
    wanted = {a for r in sampled for a in rsc.packet_ends(r)}
    draws, skipped, frames, per_frame, unusable, gpu_loads = load_gpu(args.gpu, wanted, "V" if vertex else "P")
    print(f"GPU log: {sum(n for n, _ in per_frame.values())} draws in {len(frames)} frames"
          + (f" ({min(frames)}..{max(frames)})" if frames else ""))
    if unusable:
        print(f"GPU log: {unusable} lines before their frame's first full line were not used")

    offset, hits, second, second_hits, probed = rsc.find_frame_offset(sampled, draws, frames)
    if offset is None:
        print(f"frame offset: none confirms a row ({probed} rows probed)")
    else:
        print(f"frame offset: GPU frame = capture frame + {offset} ({hits} of {probed} probed rows confirmed; "
              f"runner-up + {second}: {second_hits})")

    status = Counter()
    # Per register: [main-scene colour, pre-pass, other passes] x Counter(equal / different / not compared).
    stats = [[Counter(), Counter(), Counter()] for _ in range(REGISTERS)]
    mismatches = defaultdict(list)
    values = {reg: Counter() for reg in args.values}
    not_read = set()
    n_exec = Counter()
    per_row = Counter()
    bools = Counter()
    bool_bits = {b: Counter() for b in BOOL_BITS}
    differing_bits = {"device": Counter(), "load": Counter()}
    tested_bits = {"device": Counter(), "load": Counter()}  # of those, where the pixel shader tests the bit
    paired_frames = set()
    for r in sampled:
        if r["cmd"] is None:
            status["no command pointer"] += 1
            continue
        gpu_frame = None if offset is None else r["frame"] + offset
        found = rsc.executions_at(r, draws.get(gpu_frame, {})) if gpu_frame is not None else []
        if not found:
            if gpu_frame is not None and gpu_frame not in frames:
                status["frame outside the GPU log"] += 1
            elif gpu_frame is not None and any(a in skipped.get(gpu_frame, ()) for a in rsc.packet_ends(r)):
                status["skipped by the bin predicate"] += 1
            else:
                status["no packet at the address"] += 1
                status["unpaired in scene" if r["in_scene"] else "unpaired elsewhere"] += 1
            continue
        mine = [ex for ex in found if rsc.confirmed(r, ex)]
        if not mine:
            status["packet at the address not confirmed"] += 1
            status["unpaired in scene" if r["in_scene"] else "unpaired elsewhere"] += 1
            continue
        status["paired"] += 1
        paired_frames.add(gpu_frame)
        per_row[len(mine)] += 1
        if r["in_scene"] and not vertex:
            not_read.update(reg for reg in LIGHT_BLOCK if r["P"][reg] is None)
        for ex in mine:
            pre_pass = rsc.is_pre_pass(ex["bs"])
            kind = 1 if pre_pass else (0 if r["in_scene"] else 2)
            n_exec[kind] += 1
            gpu = ex["P"]
            reads = shader_reads.reads(ex[shader_key])
            bool_reads = reads if not vertex else None
            for reg in range(REGISTERS):
                device = expected(r, reg, ex["bs"])
                if device is None or gpu[reg] is None:
                    stats[reg][kind]["not compared"] += 1
                    continue
                if device == gpu[reg]:
                    stats[reg][kind]["equal"] += 1
                    continue
                if not pre_pass and reads is not None and reg not in reads[0]:
                    stats[reg][kind]["unread"] += 1
                    continue
                stats[reg][kind]["different"] += 1
                if kind == 0 and len(mismatches[reg]) < MAX_MISMATCHES:
                    mismatches[reg].append(
                        f"mismatch c{reg}: frame {r['frame']} (GPU frame {gpu_frame}, draw {ex['d']}) "
                        f"{r['func']} {rsc._shader(r)}: capture {_show(device)}, GPU {_show(gpu[reg])} "
                        f"(pitch {ex['pitch']}, bin select {ex['bs']:08X}, mode {ex['mode']})")
            if kind == 0:
                for reg in args.values:
                    values[reg][gpu[reg]] += 1
            # The bool word: the device's shadow ("pbool") and the word its last writer put on
            # the GPU ("pbool_load"), each against the GPU's register 0x4904.
            for key, name in (("pbool", "device"), ("pbool_load", "load")):
                if r[key] is None:
                    continue
                bools[name + " all"] += 1
                bools[name + " all different"] += r[key] != ex["pb"]
                if kind != 0:
                    continue
                bools[name + " compared"] += 1
                bools[name + " different"] += r[key] != ex["pb"]
                for bit in range(32):
                    if (r[key] ^ ex["pb"]) >> bit & 1:
                        differing_bits[name][128 + bit] += 1
                        if bool_reads is None or 128 + bit in bool_reads[1]:
                            tested_bits[name][128 + bit] += 1
                for b in BOOL_BITS:
                    bit, gpu_bit = (r[key] >> (b - 128)) & 1, (ex["pb"] >> (b - 128)) & 1
                    bool_bits[b][name] += bit
                    bool_bits[b][name + " differs"] += bit != gpu_bit
            if kind == 0:
                for b in BOOL_BITS:
                    bool_bits[b]["gpu"] += (ex["pb"] >> (b - 128)) & 1

    paired = status["paired"]
    share = 100.0 * paired / len(sampled) if sampled else 0.0
    print(f"pairing: paired {paired} of {len(sampled)} sampled rows ({share:.2f}%)")
    for name in ("no command pointer", "frame outside the GPU log", "skipped by the bin predicate",
                 "no packet at the address", "packet at the address not confirmed"):
        print(f"  {name} {status[name]}")
    print(f"executions compared: main-scene colour {n_exec[0]}, pre-pass {n_exec[1]}, other passes {n_exec[2]}; "
          "by executions per row: " + (", ".join(f"{k}: {v} rows" for k, v in sorted(per_row.items())) or "-"))
    total = sum(per_frame[f][0] for f in paired_frames)
    at_row = sum(per_frame[f][1] for f in paired_frames)
    print(f"GPU draws in the paired frames: {total}; at a sampled row's address {at_row}; "
          f"without a row {total - at_row}")

    print("registers (main-scene colour executions; then the depth pre-pass and the rows outside the main scene):")
    different, unread_only = [], []
    apart = f"different where the {shader_name} shader does not read it"
    for reg in range(REGISTERS):
        scene, pre, other = stats[reg]
        line = (f"register c{reg} {'[light block] ' if reg in LIGHT_BLOCK and not vertex else ''}"
                "main-scene colour executions "
                f"compared {scene['equal'] + scene['different'] + scene['unread']}, equal {scene['equal']}, "
                f"different {scene['different']}"
                + (f", {apart} {scene['unread']}" if scene["unread"] else "")
                + (f", not compared {scene['not compared']}" if scene["not compared"] else "")
                + f"; pre-pass compared {pre['equal'] + pre['different']}, different {pre['different']}"
                + f"; other passes compared {other['equal'] + other['different'] + other['unread']}, "
                f"different {other['different']}"
                + (f", {apart} {other['unread']}" if other["unread"] else ""))
        print(line)
        if scene["different"]:
            different.append(reg)
        elif scene["unread"]:
            unread_only.append(reg)
    for reg in range(REGISTERS):
        for line in mismatches[reg]:
            print(line)
    print("registers different in a main-scene colour execution: "
          + (" ".join(f"c{reg}" for reg in different) or "none"))
    print("registers different in an execution outside the main scene: "
          + (" ".join(f"c{reg}" for reg in range(REGISTERS) if stats[reg][2]["different"]) or "none"))
    if args.dumps:
        print(f"registers different only where the {shader_name} shader does not read them: "
              + (" ".join(f"c{reg}" for reg in unread_only) or "none"))
        print(f"{shader_name} shaders without a readable dump: {len(shader_reads.unknown)}"
              + ("".join(f" 0x{h:016X}" for h in sorted(shader_reads.unknown)[:8])))

    for name, title in (() if vertex else
                        (("device", "pixel bool word (register 0x4904, b128..b159), the device's shadow \"pbool\""),
                         ("load", "pixel bool word as its last writer left it, \"pbool_load\""))):
        compared, differs = bools[name + " compared"], bools[name + " different"]
        print(f"{title}: main-scene colour executions compared {compared}, equal {compared - differs}, "
              f"different {differs}; all executions compared {bools[name + ' all']}, "
              f"different {bools[name + ' all different']}")
        print(f"  bits of \"{'pbool' if name == 'device' else 'pbool_load'}\" that differ from the GPU's there: "
              + (", ".join(f"b{b}: {n}" + (f" (tested by the pixel shader in {tested_bits[name][b]})"
                                           if args.dumps else "")
                           for b, n in sorted(differing_bits[name].items())) or "none"))
    for b in (() if vertex else BOOL_BITS):
        c = bool_bits[b]
        print(f"  b{b}: GPU set {c['gpu']}, device set {c['device']}, device differs {c['device differs']}, "
              f"load set {c['load']} of {bools['load compared']}, load differs {c['load differs']}")

    print_loads(capture_loads, gpu_loads, offset, frames)

    for reg in args.values:
        top = values[reg].most_common(4)
        print(f"value c{reg}: {len(values[reg])} distinct in {sum(values[reg].values())} main-scene colour "
              "executions: " + ", ".join(f"{_show(v, 6)} x{n}" for v, n in top)
              + (" ..." if len(values[reg]) > len(top) else ""))

    paired_text = f"paired {paired} of {len(sampled)} sampled rows"
    block_different = [reg for reg in decisive if stats[reg][0]["different"]]
    unpaired = status["unpaired in scene"] + status["unpaired elsewhere"] + status["no command pointer"] \
        + status["skipped by the bin predicate"]
    reasons = []
    if unpaired:
        reasons.append(f"{unpaired} rows not paired")
    if not n_exec[0]:
        reasons.append("no main-scene colour execution compared")
    if block_different:
        reasons.append(("vertex registers" if vertex else "light block registers")
                       + " different in a main-scene colour execution: "
                       + " ".join(f"c{reg}" for reg in block_different))
    if not_read:
        reasons.append("light block registers not read in a main-scene row: "
                       + " ".join(f"c{reg}" for reg in sorted(not_read)))
    outside = status["frame outside the GPU log"]
    tail = f"; {outside} rows in frames outside the GPU log were not compared" if outside else ""
    if reasons:
        print(f"RESULT: FAIL ({args.key}): {paired_text}; " + "; ".join(reasons) + tail)
        return 1
    print(f"RESULT: OK ({args.key}): {paired_text}; "
          + ("every vertex register is" if vertex else
             "the light block's registers (" + " ".join(f"c{reg}" for reg in LIGHT_BLOCK) + ") are")
          + f" equal in all {n_exec[0]} main-scene colour executions"
          + (f" whose {shader_name} shader reads them" if args.dumps else "")
          + (f"; other registers differ there: {len(different)}" if different and not vertex else "") + tail)
    return 0


if __name__ == "__main__":
    sys.exit(main())
