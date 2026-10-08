"""Replay of captured probe draws: the game's vertex microcode against the native renderer's decode.

Usage: python tools/xdk_sigmatch/replay_check.py <native_discovery_*.jsonl> [--frame N] [--tolerance T]
                                                 [--dumps DIR] [--every] [--out-of-scene]

A draw whose light inputs have vertex kind "probe" (vertex shaders 79EAC49585797037, D4D558DA6A82BDC8,
A1F7E9885EC466DF, 475EC9F795E5EDBB: hero, dog, villagers, props, trunks) takes its vertex colour from
twelve vertex constants and, per vertex, four weights and a set index. For every such draw row of the
capture and every vertex its two streams hold, two values are computed from the SAME captured bytes:

(a) THE GAME'S: the shader's own microcode (ucode_eval.py on out/shader_dump/shader_<HASH>.ucode.vert)
    with the row's 256 vertex registers ("bank"), the shader's literals ("vs_lit"), the bool word and
    the row's dumped streams, each fetch delivering the element as the GPU's fetch does (fetch_source:
    the unpacking of the SDK's translator, graphics/pipeline/shader/dxbc_translator_fetch.cpp, for a
    stream of endian 8in32). The probe colour is the interpolator the shader writes it to (PROBE_OUTPUT).

(b) THE NATIVE RENDERER'S, mirrored from the C++ and the HLSL:
    weights    DecodeWeights (src/native/render/normal_decode.h): the element at vertex * stride + off of
               the weights stream ("lin"."second"), w[k] = the big-endian float at 4 * comp[k], comp the
               element's selector ("src": "xyzw" = 0, 1, 2, 3); refused unless format 38, endian 8in32,
               four distinct components, |w| <= 1024 (WeightsElementSupported, kProbeWeightMax);
    set index  DecodeProbeSets (same file) through DecodePositions (src/native/capture/position_decode.h)
               with the identity swizzle: four big-endian halves m[0..3] of the position element, source
               component c = m[c ^ 1] (the pair swap of 8in32), the set = source component "lin"."a0"
               (2: ProbeSetSourceComponent), which must be exactly 0, 4 or 8;
    count      GeometryCache::Probe (src/native/render/geometry_cache.cpp): every vertex both streams hold;
    colour     kClayVs (src/native/fable2_native_shaders.h, the probe block): a0 = min(set, 8),
               o_probe[a0].xyz + (dot(o_probe[a0 + 1], w), dot(o_probe[a0 + 2], w), dot(o_probe[a0 + 3], w)),
               o_probe the row's "lin"."vc" (vertex c28..c39 as the record holds them).

(c) The two are compared per vertex and channel. Both sides are evaluated in double precision from the
    same single-precision inputs, so they agree to rounding when the decode is the game's: the default
    tolerance is 1e-6 absolute (single precision resolves about 1e-7 at these magnitudes). Also compared:
    the set index against the microcode's address register, and "lin"."vc" against bank c28..c39.

The control (the check must be able to fail): the native side again with the four weights in each of the
23 other orders. Per shader the table prints the SMALLEST of the 23 largest differences: a wrong order of
the weights differs from the game by at least that much somewhere. A shader whose control does not exceed
the tolerance has streams that cannot tell the orders apart: the check is blind there (exit code 3).

Exit code: 0 every vertex of every draw within the tolerance and every control above it; 1 a mismatch
(the first example is printed: bytes, both results); 2 no probe draw could be checked, or the microcode
could not be run at some vertex (a vertex not evaluated is not a match); 3 a blind control. A draw the
native reader refuses (its reasons are counted) has no probe buffer and no light in the renderer: it is
not compared and is not a mismatch.

What is delivered as zero and counted, because the capture does not hold it and the probe colour does not
read it (if it did, the comparison would show it): a fetch from a third stream (the bone palette) past
its dumped bytes, and the trunks' vertex textures tf16 / tf17 (their vertex fog).
"""
import argparse
import itertools
import json
import math
import struct
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ucode_eval as ue  # noqa: E402

DUMPS = Path(__file__).resolve().parents[2] / "out" / "shader_dump"
TOLERANCE = 1e-6

# Where each probe vertex shader writes the probe colour (the `add` of c[28 + a0].xyz and the three
# dot products, and the export): 79EA... instr 39, D4D5... 87, A1F7... 63 write o6; 475E... adds into r8
# at instr 77 and exports o8 at 99.
PROBE_OUTPUT = {"79EAC49585797037": 6, "D4D558DA6A82BDC8": 6, "A1F7E9885EC466DF": 6, "475EC9F795E5EDBB": 8}

FORMAT_NAMES = {6: "FMT_8_8_8_8", 7: "FMT_2_10_10_10", 16: "FMT_10_11_11", 31: "FMT_16_16_FLOAT",
                32: "FMT_16_16_16_16_FLOAT", 38: "FMT_32_32_32_32_FLOAT"}
_PACKED = {6: ((8, 0), (8, 8), (8, 16), (8, 24)), 7: ((10, 0), (10, 10), (10, 20), (2, 30)),
           16: ((11, 0), (11, 11), (10, 22))}
_DWORDS = {6: 1, 7: 1, 16: 1, 31: 1, 32: 2, 38: 4}
PROBE_REG = 28          # vertex c28..c39: three sets of four (material.h, kLightVsProbeReg)
PROBE_REGISTERS = 12
WEIGHT_MAX = 1024.0     # normal_decode.h, kProbeWeightMax
SOURCE_LETTERS = "xyzw"


class Refused(Exception):
    """The native reader gives this draw no probe buffer (the reason is the message)."""


def f32(x):
    """The single-precision value nearest x, as a Python float (the rows print nine digits)."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def _half(h):
    return struct.unpack("<e", struct.pack("<H", h))[0]


def fetch_source(fmt, data, at, signed=True, normalized=False, exp=0):
    """The four source components (x, y, z, w before the destination swizzle) of the element of Xenos
    vertex format `fmt` at byte `at` of a stream with endian 8in32, as the GPU's fetch delivers them
    (the SDK's dxbc_translator_fetch.cpp: the dwords are byte-swapped, i.e. read big-endian; packed
    formats take their fields from bit 0 up; 16-bit pairs are (low half, high half) of each dword).
    A component the format does not have is 0. IndexError when the element is not inside `data`."""
    n = _DWORDS.get(fmt)
    if n is None:
        raise NotImplementedError(f"vertex format {fmt}")
    if at < 0 or at + 4 * n > len(data):
        raise IndexError(f"element at byte {at} ({4 * n} bytes) outside the stream ({len(data)} bytes)")
    d = struct.unpack_from(f">{n}I", data, at)
    if fmt == 38:
        v = list(struct.unpack_from(">4f", data, at))
    elif fmt in (31, 32):
        v = []
        for w in d:
            v += [_half(w & 0xFFFF), _half(w >> 16)]
    else:
        v = []
        for width, shift in _PACKED[fmt]:
            raw = (d[0] >> shift) & ((1 << width) - 1)
            if signed:
                if raw >> (width - 1):
                    raw -= 1 << width
                x = float(raw)
                if normalized:
                    if width > 2:
                        x /= float((1 << (width - 1)) - 1)
                    x = max(x, -1.0)
            else:
                x = float(raw)
                if normalized and width > 1:
                    x /= float((1 << width) - 1)
            v.append(x)
    v += [0.0] * (4 - len(v))
    if exp:
        v = [math.ldexp(x, exp) for x in v]
    return v


class Streams:
    """ucode_eval's fetch callback over one draw row: `data` maps a fetch slot (95 - stream) to the
    stream's bytes; the row's "fetches" give each element's format, sign and stride."""

    def __init__(self, row, data, lenient=()):
        self.data = data
        self.fetches = {(f["slot"], f["off"]): f for f in row["fetches"]}
        self.stride = {f["slot"]: f["stride"] for f in row["fetches"]}
        self.lenient = set(lenient)   # slots whose fetches past the bytes deliver zeros (counted)
        self.outside = 0

    def __call__(self, stream, offset, attrs, index):
        slot = 95 - stream
        f = self.fetches.get((slot, offset))
        if f is None:
            raise KeyError(f"the row has no fetch of slot {slot} at offset {offset}")
        name = attrs.get("DataFormat")
        if name is not None and FORMAT_NAMES.get(f["fmt"]) != name:
            raise ValueError(f"slot {slot} offset {offset}: the row says format {f['fmt']}, the microcode {name}")
        if slot not in self.data:
            raise IndexError(f"slot {slot}: the stream was not dumped")
        at = (int(math.floor(index + 0.5)) * self.stride[slot] + offset) * 4
        try:
            return fetch_source(f["fmt"], self.data[slot], at, f.get("signed", True), f.get("norm", False), f.get("exp", 0))
        except IndexError:
            if slot in self.lenient:
                self.outside += 1
                return [0.0, 0.0, 0.0, 0.0]
            raise


def _no_texture(slot, coords, attrs):
    return [0.0, 0.0, 0.0, 0.0]


def game_constants(row):
    c = {k: [f32(x) for x in row["bank"][4 * k:4 * k + 4]] for k in range(len(row["bank"]) // 4)}
    for lit in row.get("vs_lit", ()):
        for j in range(len(lit["v"]) // 4):
            c[lit["reg"] + j] = [f32(x) for x in lit["v"][4 * j:4 * j + 4]]
    return c


def game_probe_colour(text, consts, streams, index, out_reg, bools=()):
    """(the probe colour the microcode exports for vertex `index`, the address register it ended with)."""
    m = ue.Machine(text, consts, _no_texture, regs={0: [float(index), 0.0, 0.0, 0.0]}, bools=bools, vfetch=streams)
    out = m.run()
    return out[f"o{out_reg}"][:3], m.a0


# --- the native renderer's side ---------------------------------------------------------------------

def native_element(lin):
    """The weights element as WeightsElementSupported accepts it: (offset, stride, components)."""
    e = lin["second"]
    endian = (e["flags"] >> 2) & 3
    comp = [SOURCE_LETTERS.index(ch) for ch in e["src"]]
    if e["fmt"] != 38 or endian != 2 or e["stride"] == 0 or e["off"] + 16 > e["stride"]:
        raise Refused("element")
    if comp != [0, 1, 2, 3]:              # WeightsSourceComponents: any other selector gives no weights
        raise Refused("element")
    return e["off"], e["stride"], comp


def native_weights(data, lin, vertex):
    """DecodeWeights for one vertex."""
    off, stride, comp = native_element(lin)
    at = vertex * stride + off
    if at + 16 > len(data):
        raise Refused("stream")
    w = [struct.unpack_from(">f", data, at + 4 * k)[0] for k in comp]
    if not all(abs(x) <= WEIGHT_MAX for x in w):      # (a NaN too)
        raise Refused("value")
    return w


def native_probe_set(data, pos, a0_comp, vertex):
    """DecodeProbeSets for one vertex."""
    if pos["format"] != 32 or not pos["swap16"] or pos["stride_bytes"] == 0 or a0_comp != 2:
        raise Refused("element")          # ProbeSetSupported
    at = vertex * pos["stride_bytes"] + pos["offset_bytes"]
    if at + 8 > len(data):
        raise Refused("stream")
    m = [_half(h) for h in struct.unpack_from(">4H", data, at)]
    source = [m[c ^ 1] for c in range(4)]
    s = source[a0_comp]
    if s not in (0.0, 4.0, 8.0):
        raise Refused("probe-set")
    return int(s)


def native_count(lin, pos, weights_size, vb_size):
    """GeometryCache::Probe: min(WeightsCount, PositionCount)."""
    off, stride, _ = native_element(lin)
    wc = 0 if off + 16 > weights_size else (weights_size - (off + 16)) // stride + 1
    need = pos["offset_bytes"] + 8
    pc = 0 if pos["stride_bytes"] == 0 or need > vb_size else (vb_size - need) // pos["stride_bytes"] + 1
    return min(wc, pc)


def native_probe_colour(vc, set_index, w):
    """kClayVs, the probe block."""
    a0 = min(set_index, 8)
    return [vc[a0][i] + sum(x * y for x, y in zip(vc[a0 + 1 + i], w)) for i in range(3)]


PERMUTATIONS = [p for p in itertools.permutations(range(4)) if p != (0, 1, 2, 3)]


def _letters(p):
    return "".join(SOURCE_LETTERS[i] for i in p)


class ShaderResult:
    def __init__(self):
        self.draws = self.vertices = self.failed = self.set_mismatch = self.const_mismatch = 0
        self.refused = defaultdict(int)
        self.outside = 0
        self.max_abs = 0.0
        self.max_rel = 0.0
        self.worst = None                 # the vertex with the largest difference
        self.first_bad = None             # the first vertex above the tolerance
        self.control = {p: 0.0 for p in PERMUTATIONS}
        self.sets = defaultdict(int)

    def control_min(self):
        p = min(self.control, key=lambda q: self.control[q])
        return p, self.control[p]


def check_row(row, text, data, result, tolerance=TOLERANCE, out_reg=None):
    """One draw row: every vertex both streams hold, the game's colour against the native one.
    data: {fetch slot: bytes}. Adds to `result` (a ShaderResult). Returns the vertices compared."""
    lin, pos = row["lin"], row["pos"]
    vs = row["vs_hash"][2:].upper()
    out_reg = PROBE_OUTPUT[vs] if out_reg is None else out_reg
    pos_slot = pos["fetch_slot"]
    second_slot = next((s["slot"] for s in row["streams"] if s["phys"] == lin["second"]["phys"] and s["slot"] != pos_slot), None)
    try:
        if second_slot is None or second_slot not in data or pos_slot not in data:
            raise Refused("stream")
        count = native_count(lin, pos, len(data[second_slot]), len(data[pos_slot]))
        if count == 0:
            raise Refused("stream")
        # All or nothing in the renderer (BuildProbe): one vertex refused and the mesh has no buffer.
        decoded = [(native_weights(data[second_slot], lin, v), native_probe_set(data[pos_slot], pos, lin["a0"], v))
                   for v in range(count)]
    except Refused as why:
        result.refused[str(why)] += 1
        return 0
    result.draws += 1
    consts = game_constants(row)
    vc = [[f32(x) for x in r4] for r4 in lin["vc"]]
    if any(vc[k] != consts[PROBE_REG + k] for k in range(PROBE_REGISTERS)):
        result.const_mismatch += 1
    vbool = int(row.get("vbool", "0x0"), 16)
    bools = [n for n in range(32) if (vbool >> n) & 1]
    streams = Streams(row, data, lenient=[s for s in data if s not in (pos_slot, second_slot)])
    done = 0
    for v, (w, s) in enumerate(decoded):
        try:
            game, a0 = game_probe_colour(text, consts, streams, v, out_reg, bools)
        except (IndexError, KeyError, ValueError, NotImplementedError, ZeroDivisionError, OverflowError):
            result.failed += 1
            continue
        done += 1
        result.vertices += 1
        result.sets[s] += 1
        if a0 != s:
            result.set_mismatch += 1
        native = native_probe_colour(vc, s, w)
        diff = max((abs(g - n) if g == g else math.inf) for g, n in zip(game, native))
        rel = diff / max(max(abs(g) for g in game if g == g) if any(g == g for g in game) else 0.0, 1e-30)
        example = dict(cmd=row.get("cmd"), frame=row.get("frame"), vertex=v, game=list(game), native=native, game_a0=a0, set=s,
                       weights=w, diff=diff,
                       weight_bytes=bytes(data[second_slot][v * lin["second"]["stride"] + lin["second"]["off"]:][:16]).hex(),
                       position_bytes=bytes(data[pos_slot][v * pos["stride_bytes"] + pos["offset_bytes"]:][:8]).hex(),
                       vc=vc[min(s, 8):min(s, 8) + 4])
        if diff > result.max_abs or result.worst is None:
            result.max_abs, result.worst = max(diff, result.max_abs), example
        result.max_rel = max(result.max_rel, rel)
        if (diff > tolerance or a0 != s) and result.first_bad is None:
            result.first_bad = example
        for p in PERMUTATIONS:
            other = native_probe_colour(vc, s, [w[i] for i in p])
            d = max((abs(g - n) if g == g else math.inf) for g, n in zip(game, other))
            if d > result.control[p]:
                result.control[p] = d
    result.outside += streams.outside
    return done


class _ParseCache:
    """ucode_eval parses the shader's text on every run; within a check the parses are remembered."""

    def __enter__(self):
        self.real = ue.st.parse
        memo = {}

        def parse(line):
            if line not in memo:
                memo[line] = self.real(line)
            return list(memo[line])
        ue.st.parse = parse
        return self

    def __exit__(self, *exc):
        ue.st.parse = self.real


def probe_rows(path, frame=None, in_scene_only=True):
    """The capture's draw rows with probe light inputs (reason none)."""
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if '"kind":"draw"' not in line or '"kind":"probe"' not in line:
                continue
            if in_scene_only and '"in_scene":true' not in line:
                continue
            row = json.loads(line)
            lin = row.get("lin") or {}
            if lin.get("kind") != "probe" or lin.get("reason") != "none":
                continue
            if frame is not None and row.get("frame") != frame:
                continue
            yield row


def check_capture(path, frame=None, tolerance=TOLERANCE, dumps=DUMPS, every=False, in_scene_only=True):
    """{vertex shader: ShaderResult} over the capture's probe rows. Without `every`, a draw whose
    shader, streams (by content) and registers equal an earlier draw's is counted once (the frames of
    one capture repeat most draws bit for bit)."""
    path = Path(path)
    results, texts, files, seen = {}, {}, {}, set()
    skipped = 0
    with _ParseCache():
        for row in probe_rows(path, frame, in_scene_only):
            vs = row["vs_hash"][2:].upper()
            res = results.setdefault(vs, ShaderResult())
            if vs not in PROBE_OUTPUT:
                res.refused["no probe output known for this shader"] += 1
                continue
            data = {}
            for s in row["streams"]:
                if s.get("file"):
                    if s["file"] not in files:
                        f = path.parent / s["file"]
                        files[s["file"]] = f.read_bytes() if f.is_file() else None
                    if files[s["file"]] is not None:
                        data[s["slot"]] = files[s["file"]]
            if not every:
                key = (vs, tuple(sorted((slot, hash(b)) for slot, b in data.items())), tuple(row["bank"]),
                       json.dumps(row["lin"], sort_keys=True), row.get("vbool"))
                if key in seen:
                    skipped += 1
                    continue
                seen.add(key)
            if vs not in texts:
                texts[vs] = ue.load(dumps, vs, "vert")
            check_row(row, texts[vs], data, res, tolerance)
    return results, skipped


def verdict(results, tolerance=TOLERANCE):
    """The exit code for a set of results."""
    checked = [r for r in results.values() if r.vertices]
    if not checked or any(r.failed for r in results.values()):
        return 2
    if any(r.max_abs > tolerance or r.set_mismatch or r.const_mismatch for r in checked):
        return 1
    if any(r.control_min()[1] <= tolerance for r in checked):
        return 3
    return 0


def print_results(results, skipped, tolerance=TOLERANCE, out=None):
    out = out or sys.stdout
    w = out.write
    w(f"tolerance {tolerance:g} (absolute, per channel)\n")
    w("vertex shader     draws  vertices  largest |game - native|  largest relative  set differs  constants differ  "
      "control: smallest of the 23 permuted largest differences (order)\n")
    for vs in sorted(results):
        r = results[vs]
        p, c = r.control_min()
        w(f"{vs}  {r.draws:5d}  {r.vertices:8d}  {r.max_abs:22.3e}  {r.max_rel:16.3e}  {r.set_mismatch:11d}  {r.const_mismatch:16d}  "
          f"{c:.6f} ({_letters(p)})\n")
    for vs in sorted(results):
        r = results[vs]
        w(f"{vs}: set indices over the vertices {dict(sorted(r.sets.items()))}; microcode runs that failed {r.failed}; "
          f"draws the native reader refuses {dict(r.refused)}; fetches of a third stream past its bytes, delivered as zero: {r.outside}\n")
        w("    control per order (largest |game - native with the weights in that order|): " +
          ", ".join(f"{_letters(p)} {r.control[p]:.4f}" for p in PERMUTATIONS) + "\n")
        for label, e in (("largest difference", r.worst), ("FIRST MISMATCH", r.first_bad)):
            if e is None:
                continue
            w(f"    {label}: draw cmd {e['cmd']} frame {e['frame']} vertex {e['vertex']}: weight bytes {e['weight_bytes']}, "
              f"position bytes {e['position_bytes']}\n")
            w(f"        weights {e['weights']}, set {e['set']} (microcode a0 {e['game_a0']})\n")
            w(f"        constants of the set: {e['vc']}\n")
            w(f"        game   {e['game']}\n        native {e['native']}\n        difference {e['diff']:.3e}\n")
    code = verdict(results, tolerance)
    w({0: "MATCH: the native decode gives the game's probe colour in every vertex, and every other order of the weights does not\n",
       1: "MISMATCH: the native decode differs from the game's arithmetic (see FIRST MISMATCH)\n",
       2: "INCOMPLETE: no probe draw of the capture could be evaluated, or the microcode failed at some vertex (see the counts)\n",
       3: "BLIND: a shader's streams do not tell the weights' orders apart; no verdict for it\n"}[code])
    if skipped:
        w(f"({skipped} draw rows equal to an earlier one in shader, streams and registers were not repeated; --every repeats them)\n")
    return code


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("capture")
    ap.add_argument("--frame", type=int, default=None, help="only this frame of the capture (default: all)")
    ap.add_argument("--tolerance", type=float, default=TOLERANCE)
    ap.add_argument("--dumps", default=str(DUMPS), help="folder of shader_<HASH>.ucode.vert")
    ap.add_argument("--every", action="store_true", help="also repeat draws equal to an earlier one")
    ap.add_argument("--out-of-scene", action="store_true", help="also draws outside the main scene")
    a = ap.parse_args(argv)
    results, skipped = check_capture(a.capture, a.frame, a.tolerance, Path(a.dumps), a.every, not a.out_of_scene)
    print(f"{Path(a.capture).name}" + (f" frame {a.frame}" if a.frame is not None else " (all frames)"))
    return print_results(results, skipped, a.tolerance)


if __name__ == "__main__":
    sys.exit(main())
