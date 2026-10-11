"""The three terrain vertex shaders run through ucode_eval.py on random patch points, against the
closed formulas of the reading (frame-map section 13, "Terrain"). Evidence of the materials
sub-project's Task 9, tracked by the lighting sub-project with the dump folder's path and the trial
count as the only changes; `python vs_check.py` prints what `vs_check.txt` of Task 9 holds.

`python vs_check.py --casters` (the shadows sub-project): the vertex shaders of the two shadow passes
against their entries of vs-transforms.json, below ("the shadow casters' vertex shaders")."""
import json
import math
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ucode_eval as ue

DUMPS = Path(__file__).resolve().parents[2] / "out" / "shader_dump"
NAN = float("nan")
LIT_LAYER = {253: [0.5, 1 / 15, -1.0, 15 / 14], 254: [0, 1, 3, 2], 255: [NAN] * 4}   # [vtess] log, GPU register file
LIT_FAR = {254: [0.5, -1.0, 0, 0], 255: [0, 1, 3, 2]}


def close(a, b, tol=1e-4):
    return all((x != x and y != y) or abs(x - y) <= tol * max(1.0, abs(x), abs(y)) for x, y in zip(a, b))


def layer(hash_, rng):
    c = {i: [rng.uniform(-1, 1) for _ in range(4)] for i in (0, 1, 2, 3, 4, 5, 6, 19)}
    c[8] = [rng.choice([0.125, 0.0625]), rng.choice([0.125, 0.25]), 1, 0.125]
    c[11] = [2, 0.5, 0.25, 0]
    c[14] = [1 / 384, 1 / 300, 1, 1]
    c[46] = [8, 8, 89.739, 1]
    c[47] = [1 / 384, 1 / 384, rng.choice([0, 3.0]), rng.choice([0, -5.0])]
    c[113] = [rng.choice([80, 96, 112]), rng.choice([128, 144]), 0, 0]
    c[114] = [rng.choice([1 / 255, -1.0]), 0, 0, 0]
    c[115] = [16 / 1650, 16 / 1815, (33 * rng.randrange(50) + 0.5) / 1650, (33 * rng.randrange(55) + 0.5) / 1815]
    c.update(LIT_LAYER)
    patch = rng.randrange(4)
    u, v = rng.random(), rng.random()
    n = rng.randrange(16)
    hval = rng.random()
    nx, ny = rng.uniform(0.3, 0.7), rng.uniform(0.3, 0.7)
    seen = {}

    def sampler(slot, coords, attrs):
        seen[slot] = coords
        if slot == 16:
            return [hval, 0, 0, 0]
        if slot == 18:
            return [0.11, 0.22, 0.33, n / 15]   # only .w is the DXT3A value in this test
        if slot == 17:
            return [nx, ny, 0, 0]
        raise KeyError(slot)

    m = ue.Machine(ue.load(DUMPS, hash_, "vert"), c, sampler, regs={0: [patch, u, v, 0], 1: [0, 0, 0, 0]})
    out = m.run()
    row = math.floor(patch * c[11][1])
    col = patch - row * c[11][0]
    wx = (col + u) * c[46][0] + c[113][0]
    wy = (row + v) * c[46][1] + c[113][1]
    h = hval * c[46][2]
    exp_h_uv = ((wx - c[47][2]) * c[47][0], (wy - c[47][3]) * c[47][1])
    exp_w_uv = ((col + u) * c[115][0] + c[115][2], (row + v) * c[115][1] + c[115][3])
    ok = close(seen[16], exp_h_uv) and close(seen[18], exp_w_uv) and close(seen[17], exp_h_uv)
    hole = c[114][0] > n / 15
    pos = [sum(c[r][k] * p for k, p in zip(range(4), (wx, wy, h, 1))) for r in range(4)]
    weight = c[253][3] * (n / 15 - c[253][1])
    detail = (c[8][1] * wx, -c[8][1] * wy)
    base = (c[14][0] * (wx - c[47][2]), c[14][1] * (wy - c[47][3]))
    if hole:
        ok = ok and all(x != x for x in out["oPos"])
    else:
        ok = ok and close(out["oPos"], pos, 1e-3)
        if hash_.startswith("C30A"):
            ok = ok and close(out["o2"][:2], detail) and close(out["o5"][:2], base) and close([out["o9"][3]], [weight])
            ok = ok and close(out["o0"][:2], (c[8][0] * wy, -c[8][0] * h)) and close(out["o1"][:2], (c[8][0] * wx, -c[8][0] * h))
            wpos = [sum(c[r][k] * p for k, p in zip(range(4), (wx, wy, h, 1))) for r in (4, 5, 6)]
            ok = ok and close(out["o4"][:3], wpos, 1e-3)
            N = (2 * nx - 1, 2 * ny - 1)
            nz = math.sqrt(abs(1 - N[0] ** 2 - N[1] ** 2))
            ok = ok and close(out["o3"][:3], (N[0], N[1], nz))
        else:
            ok = ok and close(out["o0"][:2], detail) and close(out["o3"][:2], base) and close([out["o7"][3]], [weight])
            wpos = [sum(c[r][k] * p for k, p in zip(range(4), (wx, wy, h, 1))) for r in (4, 5, 6)]
            ok = ok and close(out["o2"][:3], wpos, 1e-3)
            N = (2 * nx - 1, 2 * ny - 1)
            nz = math.sqrt(abs(1 - N[0] ** 2 - N[1] ** 2))
            ok = ok and close(out["o1"][:3], (N[0], N[1], nz))
    return ok, hole, sorted(m.used_consts)


def far(rng):
    h_ = "5003700B7C9B1C16"
    c = {i: [rng.uniform(-1, 1) for _ in range(4)] for i in (0, 1, 2, 3)}
    cols = rng.choice([1, 2, 3, 6])
    c[8] = [rng.choice([8, 32]), rng.choice([8, 32]), 89.739, 1]
    c[11] = [cols, 1 / cols, 0.5 / cols, 0]
    c[14] = [1 / 384, 1 / 300, 1, 1]
    c[46] = [1 / 384, 1 / 128, rng.choice([0, 32.0]), rng.choice([0, 448.0])]
    c[47] = [rng.choice([96, 144]), rng.choice([96, 192]), 0, 0]
    c[72] = [16 / 17, 0.5 / 17, 0, 0]
    c[113] = [rng.choice([0, 3, 19]), 0, 0, 0]
    c[114] = [1 / 48, 1 / 48, 0, 0]
    c[115] = [rng.randrange(40), rng.randrange(40), 0, 0]
    c.update(LIT_FAR)
    k = rng.randrange(6)
    u, v = rng.random(), rng.random()
    hval, ta, tb = rng.random(), rng.random(), rng.random()
    nx, ny = rng.uniform(0.3, 0.7), rng.uniform(0.3, 0.7)
    seen = {}

    def sampler(slot, coords, attrs):
        seen[slot] = coords
        return {16: [hval, 0, 0, 0], 18: [ta, 0.9, 0.8, 0.7], 19: [tb, 0.6, 0.5, 0.4], 17: [nx, ny, 0, 0]}[slot]

    m = ue.Machine(ue.load(DUMPS, h_, "vert"), c, sampler, regs={0: [k, u, v, 0], 1: [0, 0, 0, 0]})
    out = m.run()
    p = k + c[113][0]
    row = math.floor(p * c[11][1] + 1e-9)
    col = p - row * c[11][0]
    wx = (col + u) * c[8][0] + c[47][0]
    wy = (row + v) * c[8][1] + c[47][1]
    h = hval * c[8][2]
    huv = ((wx - c[46][2]) * c[46][0], (wy - c[46][3]) * c[46][1])
    tu = ((c[115][0] + col + u * c[72][0] + c[72][1]) * c[114][0], (c[115][1] + row + c[254][0]) * c[114][1])
    tv = ((c[115][0] + col + v * c[72][0] + c[72][1]) * c[114][0], (c[115][1] + row + c[254][0]) * c[114][1])
    pos = [sum(c[r][j] * q for j, q in zip(range(4), (wx, wy, h, 1))) for r in range(4)]
    ok = close(seen[16], huv) and close(seen[17], huv) and close(seen[18], tu) and close(seen[19], tv)
    ok = ok and close(out["oPos"], pos, 1e-3)
    ok = ok and close(out["o0"], (ta, tb, (wx - c[46][2]) * c[14][0], (wy - c[46][3]) * c[14][1]))
    ok = ok and close(out["o1"][:3], (wx, wy, h))
    N = (2 * nx - 1, 2 * ny - 1)
    ok = ok and close(out["o2"][:3], (N[0], N[1], math.sqrt(abs(1 - N[0] ** 2 - N[1] ** 2))))
    return ok, sorted(m.used_consts)


def run(trials=400):
    """Runs the three shaders on `trials` random points each; returns the report lines."""
    lines = []
    rng = random.Random(9)
    for h_ in ("C30A97D946FA2BE4", "FB68A7F2301210E1"):
        n_ok = n_hole = 0
        used = None
        for _ in range(trials):
            ok, hole, used = layer(h_, rng)
            n_ok += ok
            n_hole += hole
        lines.append(f"{h_}: {n_ok} of {trials} random points agree with the reading ({n_hole} culled by the layer test); constants read {used}")
    n_ok = 0
    for _ in range(trials):
        ok, used = far(rng)
        n_ok += ok
    lines.append(f"5003700B7C9B1C16: {n_ok} of {trials} random points agree with the reading; constants read {used}")
    return lines


# --- the shadow casters' vertex shaders (shadows sub-project, plan Task 2) ------------------------
#
# The vertex shaders of the two shadow passes are drawn nowhere else, and a capture's rows outside the
# main scene carry no stream dumps, so position_check.py cannot replay them. This check stands in its
# place: a table entry of vs-transforms.json is replayed by position_check's own functions (the ones
# that mirror the native renderer: the position fetch and its swizzle, instance_expand.h, bone_skin.h)
# on random streams and constants laid out as the shader's dump fetches them, and the result is held
# against the shader's microcode run on the same bytes: the clip position, the vertices an instance
# entry's distance cut drops (the microcode gives them a NaN position), and the entry's "uv". A shader
# marked "deformed" in the table moves its vertices in the wind; the check then runs with the sway's
# amplitude constants zero (CASTER_SWAY_OFF), or with the entry's "wind" member (the closed formula of
# light_formulas.py through position_check.wind_positions), or, with neither, compares the uv alone.

TABLE = Path(__file__).resolve().parents[2] / "docs" / "native-renderer" / "vs-transforms.json"
_PI = math.pi
# Literal tables: "vs_lit" of every shadow-pass row of the shader in the three captures of 2026-10-08
# (out/native-evidence/shadows/task2_caster_rows.txt; one table per shader in every row).
CASTER_LITERALS = {
    "F160B4DA459A6D40": {},
    "57818A7FD1C4F026": {},
    "5F4416192E87005F": {255: [0.0, 1.0, 0.0, 0.0]},
    "82F6433A69263C75": {255: [0.0, 1.0, 0.0, 0.0]},
    "3A0F9098B839DDBC": {255: [0.0, 1.0, 0.0, 0.0]},
    "87E0DB8B7C32F6A5": {255: [0.0, 1.0, 0.0, 0.0]},
    "A8A5B2A89F70336A": {252: [0.0] * 4, 253: [2 * _PI, -_PI, 1 / (2 * _PI), 0.0], 254: [0.0, 1.0, 0.5, 0.1],
                         255: [NAN, NAN, NAN, 5.0]},
    "432563420047C96C": dict(zip(range(248, 256), (
        [0.0] * 4, [-_PI, 0.5, 0.0, 0.0], [0.25, 1.0, 0.1, 0.7], [0.11, 0.23, -0.3, 0.05], [0.15, 100.0, 2 * _PI, 0.25],
        [0.035, 0.472, 0.55, 0.9], [0.04, 0.025, 0.0, 1.0], [0.3849, 0.3, 0.019245, 0.28867501]))),
}
# Deformed shaders whose sway has an amplitude in constants: the registers set to zero for the position
# check (A8A5...: instr 42, 43 multiply the displacement by c26.y and c26.x).
CASTER_SWAY_OFF = {"A8A5B2A89F70336A": (26,)}
# Deformed shaders without such a switch and without a "wind" member in their entry: the uv alone is compared.
# None since the foliage sub-project (plan Task 7): the leaves' caster 4325 has the member, and its position
# is compared with the wind on (position_check.wind_positions).
CASTER_UV_ONLY = ()
_VERTEX_FORMATS = {"FMT_16_16_16_16_FLOAT": 32, "FMT_16_16_FLOAT": 31, "FMT_8_8_8_8": 6}


def dump_fetches(text):
    """The vertex fetches of a dump in order, in the form a capture row lists them ("fetches": the
    stream vfN is fetch slot 95 - N; offsets and strides in dwords; the destination swizzle as bits), each
    with the dump's own attributes under "attrs"."""
    import gen_transform_table as gt
    out, stream, stride = [], None, None
    for ins in ue.st.parse(text):
        if not ins.op.startswith("vfetch"):
            continue
        if ins.op == "vfetch_full":
            stream, stride = ins.fetch_slot, int(ins.attrs["Stride"])
        out.append({"i": len(out), "instr": ins.index, "slot": 95 - stream, "reg": ins.dest,
                    "fmt": _VERTEX_FORMATS[ins.attrs["DataFormat"]], "off": int(ins.attrs.get("Offset", 0)),
                    "stride": stride, "mini": ins.op == "vfetch_mini", "signed": ins.attrs.get("Signed") == "true",
                    "norm": ins.attrs.get("NumFormat") != "integer", "exp": 0, "swz": gt.swizzle_bits(ins.dest_swz),
                    "attrs": dict(ins.attrs)})
    return out


def element(data, at, attrs):
    """A vertex element's source components (x, y, z, w) as the GPU delivers them from a stream whose
    dwords are stored byte-reversed (fetch-constant endian 2, as every stream of the captures): a dword's
    low half or byte is the first component."""
    fmt = attrs["DataFormat"]
    if fmt == "FMT_16_16_16_16_FLOAT":
        m = struct.unpack_from(">4e", data, at)
        return [m[1], m[0], m[3], m[2]]
    if fmt == "FMT_16_16_FLOAT":
        m = struct.unpack_from(">2e", data, at)
        return [m[1], m[0], 0.0, 1.0]
    if fmt == "FMT_8_8_8_8":
        word = struct.unpack_from(">I", data, at)[0]
        parts = [(word >> (8 * k)) & 0xFF for k in range(4)]
        return [float(p) for p in parts] if attrs.get("NumFormat") == "integer" else [p / 255.0 for p in parts]
    raise NotImplementedError(f"vertex format {fmt}")


def _random_stream(rng, fetches, slot, count, bones=0):
    """`count` vertices of the stream on `slot`: random halves in every dword, a random bone index below
    `bones` in each byte of an integer 8_8_8_8 element, random bytes (the first never zero) in a
    normalised one."""
    stride = next(f["stride"] for f in fetches if f["slot"] == slot)
    words = {f["off"]: f for f in fetches if f["slot"] == slot and f["fmt"] == 6}
    out = bytearray()
    for _ in range(count):
        for dword in range(stride):
            if dword in words and not words[dword]["norm"]:
                out += bytes(rng.randrange(bones) for _ in range(4))
            elif dword in words:
                out += bytes([rng.randrange(1, 256)] + [rng.randrange(256) for _ in range(3)])
            else:
                out += struct.pack(">2e", rng.uniform(-2, 2), rng.uniform(-2, 2))
    return bytes(out)


def caster_trial(hash_, entry, text, rng, sway=False):
    """One random draw of a caster vertex shader, a dozen vertices: (vertices that agree, vertices,
    vertices the entry's cut drops). `sway`: leave the sway's amplitude constants random (the microcode
    then moves the vertices and a "deformed" entry is expected to disagree)."""
    import position_check as pc
    spec = pc.parse_entry(entry)
    fetches = dump_fetches(text)
    bank = [rng.uniform(-1, 1) for _ in range(64 * 4)]
    bank[15] = rng.uniform(2.0, 4.0)                                # c3.w: a w that is not near zero
    streams, vertices, bones, copies = {}, 12, 6, 5
    if spec["kind"] == "instance":
        per_copy, first = rng.randrange(4, 9), rng.randrange(3)
        bank[48:52] = [1.0 / per_copy, float(per_copy), float(first), rng.random()]        # c12
        bank[28:31] = [rng.uniform(-50, 50) for _ in range(3)]                             # c7.xyz: the offset
        bank[36:39] = [bank[28 + k] + rng.uniform(-1, 1) for k in range(3)]                # c9.xyz: the eye
        bank[54] = rng.uniform(4.0, 30.0)                                                  # c13.z: the cut
        rows_slot = fetches[spec["rows"][0]]["slot"]
        streams[rows_slot] = _random_stream(rng, fetches, rows_slot, first + copies)
        mesh_slot = fetches[spec["pos_fetch"]]["slot"]
        streams[mesh_slot] = _random_stream(rng, fetches, mesh_slot, per_copy)
        idx = sorted(rng.sample(range(per_copy * copies), vertices))
    else:
        for slot in sorted({f["slot"] for f in fetches}):
            streams[slot] = _random_stream(rng, fetches, slot, bones if slot != 95 else vertices, bones)
        idx = list(range(vertices))
    if not sway:
        for reg in CASTER_SWAY_OFF.get(hash_, ()):
            bank[4 * reg:4 * reg + 4] = [0.0] * 4
    bank = [pc.f32(x) for x in bank]                                # single precision, as the game's constants
    row = {"fetches": fetches, "idx": idx, "base_vertex": 0, "bank": bank, "ib": {"max_index": max(idx)}}

    def get(slot):
        if slot not in streams:
            raise pc.Unreadable(f"no stream on slot {slot}")
        return pc.Stream(streams[slot], len(streams[slot]), 2)

    positions, info = pc.replay(row, spec, get)
    if "wind" in entry and hash_ not in CASTER_SWAY_OFF:
        positions = pc.wind_positions(row, entry, get, positions, info)
    dropped = info.get("cut") or [False] * len(positions)
    consts = {i: bank[4 * i:4 * i + 4] for i in range(64)}
    consts.update(CASTER_LITERALS[hash_])
    stride = {}

    def vfetch(stream, offset, attrs, index):
        if "Stride" in attrs:
            stride[stream] = int(attrs["Stride"])
        data = streams[95 - stream]
        at = (int(round(index)) * stride[stream] + offset) * 4
        if index != round(index) or at < 0 or at + 4 > len(data):
            raise IndexError(f"vf{stream} index {index}")
        return element(data, at, attrs)

    def no_texture(slot, coords, attrs):
        raise KeyError(f"vertex texture tf{slot}")

    agree = 0
    for k, index in enumerate(idx):
        m = ue.Machine(text, consts, no_texture, regs={0: [float(index), 0.0, 0.0, 0.0]}, vfetch=vfetch)
        out = m.run()
        ok = True
        if hash_ not in CASTER_UV_ONLY:
            if dropped[k]:
                ok = any(x != x for x in out["oPos"])
            else:
                ok = close(out["oPos"], pc.project(row, spec, positions[k]), 1e-3)
        vertex = info["pairs"][k][1] if spec["kind"] == "instance" else index
        for name, uv in (entry.get("uv") or {}).items():
            f = fetches[uv["fetch"]]
            if f["fmt"] != uv["format"] or f["off"] != uv["offset"] or uv.get("stages"):
                ok = False
                continue
            source = element(streams[f["slot"]], (vertex * f["stride"] + f["off"]) * 4, f["attrs"])
            ok = ok and close([out[name[:2]]["xyzw".index(name[3])]], [source["xyzw".index(uv["src"])]], 1e-6)
        agree += ok
    return agree, len(idx), sum(dropped)


def caster_check(hash_, entry=None, trials=100, seed=9, dumps=DUMPS, sway=False):
    """`trials` random draws of one caster vertex shader against its table entry (or `entry`): (vertices
    that agree, vertices, vertices cut)."""
    if entry is None:
        entry = json.loads(TABLE.read_text())["0x" + hash_]
    text = ue.load(dumps, hash_, "vert")
    rng = random.Random(seed)
    total = [0, 0, 0]
    for _ in range(trials):
        for i, n in enumerate(caster_trial(hash_, entry, text, rng, sway)):
            total[i] += n
    return tuple(total)


def run_casters(trials=100, dumps=DUMPS):
    """Every shader of CASTER_LITERALS against the committed table: (report lines, shaders that do not
    agree in every vertex)."""
    table = json.loads(TABLE.read_text())
    lines, bad = [], 0
    for hash_ in CASTER_LITERALS:
        entry = table["0x" + hash_]
        form = "instance" if "instance" in entry else "skin" if "skin" in entry else "plain"
        agree, n, cut = caster_check(hash_, entry, trials, dumps=dumps)
        what = "uv only (deformed, no switch for the sway)" if hash_ in CASTER_UV_ONLY else \
            "position" + (" with the sway's amplitude zero" if hash_ in CASTER_SWAY_OFF else "") + \
            (" and uv" if entry.get("uv") else ", no uv in the entry") + (f", {cut} vertices cut" if form == "instance" else "")
        lines.append(f"{hash_} ({form}): {agree} of {n} random vertices agree with the table entry; compared: {what}")
        bad += agree != n or n == 0
    return lines, bad


def main(argv=None):
    """Without arguments: the three terrain shaders (as Task 9 printed them). --casters: the shadow
    casters' vertex shaders against their table entries; exit code 1 if one disagrees."""
    argv = sys.argv[1:] if argv is None else argv
    if "--casters" in argv:
        lines, bad = run_casters()
        print("\n".join(lines))
        return 1 if bad else 0
    print("\n".join(run()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
