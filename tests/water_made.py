"""A made discovery capture of the lake's surface for the tests of water_check.py (`rows`, `dead`, `views`) and of
water_replay.py (the water sub-project, plan Tasks 4 and 6): one flat quad of two triangles drawn by vertex shader
BEAD84BD72072E0E with pixel shader A17D8AEC3A817D45 (and, if asked, by the opaque twin F6D98C7B4D98438B before it),
its vertex and index dumps, a shore mask, a normal map of one block, a reflection picture, an empty slot 14, the
frame's two fog tables, and, if asked, the rows of one reflection view. Every row has the members the readers take
from a real capture's rows and no other. Not a test file: tests/test_water_formulas.py and tests/test_water_replay.py
import it. records_capture (plan Task 9) is the same capture as a build with the water's switch on writes it:
"seq" on the water rows, the fog's tables at slots 4 and 5, and the frame row's "water"."""
import json
import struct
from pathlib import Path

VS, PS, PS_OPAQUE = "BEAD84BD72072E0E", "A17D8AEC3A817D45", "F6D98C7B4D98438B"
SIZE = (1120, 720)
GARBAGE = 1.5e22                                # what a component no formula reads holds
# The view-projection of the made scene: the picture's x is the world's x / 100, its y the world's y / 100, the
# depth 0.5 everywhere, w = 1. The quad lies level at HEIGHT with the eye EYE straight above it.
M = ([0.01, 0.0, 0.0, 0.0], [0.0, 0.01, 0.0, 0.0], [0.0, 0.0, 0.0, 0.5], [0.0, 0.0, 0.0, 1.0])
HEIGHT = 20.0
EYE = (0.0, 0.0, 200.0)
STATE = {"surface": {"2200": "0x00708746", "2201": "0x07060706"}, "opaque": {"2200": "0x00708766", "2201": "0x00010001"}}
PIXEL = {0: (0.0, 0.0, 1.0 / 1120.0, 1.0 / 720.0), 19: EYE + (GARBAGE,), 20: (GARBAGE, GARBAGE, GARBAGE, 9.15686417),
         28: (1.0, 1.0, GARBAGE, GARBAGE), 31: (2.0 / 3.0, 2.0 / 3.0, GARBAGE, GARBAGE),
         66: (-0.83130461, 0.0904366747, 0.548410416, GARBAGE), 67: (19.0682278, GARBAGE, 1.0, 0.000500000024),
         68: (0.266010404, 0.398030013, 0.5, GARBAGE), 116: (0.104081, GARBAGE, GARBAGE, GARBAGE),
         117: (-0.420816, GARBAGE, GARBAGE, GARBAGE), 126: (0.027451, 0.0745098, 0.0431373, GARBAGE),
         127: (0.0588235, 0.129412, 0.121569, GARBAGE), 128: (0.3, GARBAGE, GARBAGE, GARBAGE),
         129: (0.420816, 0.420816, GARBAGE, GARBAGE), 130: (0.420816, 0.420816, GARBAGE, GARBAGE),
         131: (0.5, GARBAGE, GARBAGE, GARBAGE), 136: (0.279184, GARBAGE, GARBAGE, GARBAGE),
         137: (0.5, GARBAGE, GARBAGE, GARBAGE), 138: (128.0, GARBAGE, GARBAGE, GARBAGE),
         139: (0.8, 0.2, GARBAGE, GARBAGE), 140: (0.05, GARBAGE, GARBAGE, GARBAGE),
         141: (2.003, 2.0, GARBAGE, 0.0588235), 142: (-0.1, 0.05, -0.99, GARBAGE), 143: (1.0, 0.84, 0.41, GARBAGE)}
VERTEX = {115: (1.0365, 1.1775, GARBAGE, GARBAGE), 116: (-2.3504, 1.0095, GARBAGE, GARBAGE),
          117: (0.08, 0.08, GARBAGE, GARBAGE), 118: (0.06, 0.06, GARBAGE, GARBAGE)}
LITERALS = [0.0] * 8 + [0.0, 2.0, 0.0, 0.0, 0.5, -1.0, 1.0, -2.0]      # c252 to c255 of both pixel shaders


def fc(fmt, width, height, base, tiled=False, endian=0, swizzle=(0, 1, 2, 3), clamp=(0, 0), pitch=32, levels=False):
    """A texture fetch constant (six dwords as the rows print them): a 2D texture of Xenos format `fmt` with
    linear filters."""
    d0 = 2 | (clamp[0] << 10) | (clamp[1] << 13) | ((pitch >> 5) << 22) | (int(bool(tiled)) << 31)
    d1 = fmt | (endian << 6) | (base & 0xFFFFF000)
    d2 = (width - 1) | ((height - 1) << 13)
    d3 = sum(s << (1 + 3 * i) for i, s in enumerate(swizzle)) | (1 << 19) | (1 << 21) | (1 << 23)
    d4 = (8 << 6) if levels else 0
    d5 = (1 << 9) | ((base + 0x10000) & 0xFFFFF000 if levels else 0)
    return ["0x%08X" % (v & 0xFFFFFFFF) for v in (d0, d1, d2, d3, d4, d5)]


def pfull(registers):
    """256 registers as a row's "pfull": the bits of each float, 32 hex digits a register; a register that is not
    named holds zeros."""
    out = []
    for reg in range(256):
        out.append(struct.pack(">4f", *registers.get(reg, (0.0, 0.0, 0.0, 0.0))).hex().upper())
    return "".join(out)


def bank(view_projection, registers):
    values = [0.0] * 1024
    for i, row in enumerate(view_projection):
        values[4 * i:4 * i + 4] = row
    for reg, v in registers.items():
        values[4 * reg:4 * reg + 4] = v
    return [struct.unpack("<f", struct.pack("<f", x))[0] for x in values]


def _json(row):
    def clean(x):
        if isinstance(x, float) and x != x:
            return None
        if isinstance(x, list):
            return [clean(v) for v in x]
        if isinstance(x, dict):
            return {k: clean(v) for k, v in x.items()}
        return x
    return json.dumps(clean(row), separators=(",", ":"))


def water_row(kind, frame=1, pixel=None, vertex=None, matrix=M, tf=None, pbool="0x00000100", pbool_load="0x00000110",
              stream="geo/quad.bin", index="geo/quad_index.bin", count=6):
    """A "draw" row of the made quad by A17D (kind "surface") or F6D9 ("opaque")."""
    registers = dict(PIXEL)
    registers.update(pixel or {})
    vregisters = dict(VERTEX)
    vregisters.update(vertex or {})
    rs = {"2104": "0x0000000F", "210E": "0x3D70F0F2", "2202": "0x0000000C", "2205": "0x00018006", "2113": "0xBF800000", "2114": "0x3F800000"}
    rs.update(STATE[kind])
    return {"kind": "draw", "frame": frame, "args": ["0x00000004", "0x00000000", "0x00000000", "0x%08X" % count],
            "viewport": list(SIZE), "in_scene": True, "vbool": "0x00000000", "pbool": pbool, "rs": rs,
            "surface_info": "0x11810460",
            "ib": {"index32": False, "endian": 1, "start": 0, "count": count, "file": index, "file_offset": 0, "file_bytes": 2 * count},
            "ps_hash": "0x" + (PS if kind == "surface" else PS_OPAQUE), "tf": tf, "pfull": pfull(registers),
            "pbool_load": pbool_load, "ps_lit": [{"reg": 508, "v": LITERALS}],
            "vs_lit": [{"reg": 252, "v": [0.0] * 12 + [1.0, 0.0, 0.0, 0.0]}], "vs_hash": "0x" + VS,
            "fetches": [{"i": 0, "slot": 95, "off": 0, "stride": 5}, {"i": 1, "slot": 95, "off": 3, "stride": 5}],
            "streams": [{"slot": 95, "endian": 2, "file": stream}], "bank": bank(matrix, vregisters)}


def view_rows(matrix, frame=1, rows=3):
    """The rows of one reflection view (surface pitch 280), each with the view's c0..c3, and the resolve after it."""
    out = [{"kind": "draw", "frame": frame, "in_scene": False, "surface_info": "0x04020118",
            "bank": bank(matrix, {})[:64]} for _ in range(rows)]
    return out + [{"kind": "resolve", "frame": frame, "n": 1}]


def capture(folder, mask=(0, 255), pixel=None, vertex=None, opaque=False, views=(), picture=None, picture_size=(4, 4),
            quad=((-50.0, -30.0), (50.0, 30.0)), frame_fog=True, extra=(), mask_size=None, matrix=M):
    """Writes a made capture into `folder` and returns the path of its .jsonl. `mask`: the shore mask's bytes, a row
    of texels (`mask_size` (width, height) for more than one row); `pixel` / `vertex`: registers that replace the
    made ones; `opaque`: the F6D9 row before the A17D row; `views`: matrices, one reflection view each, before the
    scene; `picture`: slot 13's texels as bytes (r, g, b, a a texel; default a ramp); `quad`: the mesh's two
    corners; `extra`: rows written after the water's."""
    folder = Path(folder)
    (folder / "geo").mkdir(parents=True, exist_ok=True)
    (folder / "tex").mkdir(parents=True, exist_ok=True)
    (x0, y0), (x1, y1) = quad
    vertices = [(x0, y0, HEIGHT, 0.0, 0.0), (x1, y0, HEIGHT, 1.0, 0.0), (x1, y1, HEIGHT, 1.0, 1.0), (x0, y1, HEIGHT, 0.0, 1.0)]
    (folder / "geo" / "quad.bin").write_bytes(b"".join(struct.pack(">5f", *v) for v in vertices))
    (folder / "geo" / "quad_index.bin").write_bytes(struct.pack(">6H", 0, 1, 2, 0, 2, 3))
    mask = bytes(mask)
    mw, mh = mask_size or (len(mask), 1)
    stored = bytearray(32 * mh)                                 # linear rows of the pitch (32 texels)
    for y in range(mh):
        stored[32 * y:32 * y + mw] = mask[mw * y:mw * (y + 1)]
    (folder / "tex" / "mask.bin").write_bytes(bytes(stored))
    # one block of the normal map: x from 0x90 down to 0x70, y from 0x88 down to 0x78, each texel its own index
    (folder / "tex" / "normal.bin").write_bytes(bytes([0x90, 0x70, 0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA,
                                                        0x88, 0x78, 0x53, 0x97, 0x2E, 0x53, 0x97, 0x2E]))
    pw, ph = picture_size
    if picture is None:
        picture = bytes((37 * i + 11 * k) % 200 + 20 for i in range(pw * ph) for k in range(4))
    rows = bytearray(32 * 4 * ph)
    for y in range(ph):
        rows[128 * y:128 * y + 4 * pw] = picture[4 * pw * y:4 * pw * (y + 1)]
    (folder / "tex" / "picture.bin").write_bytes(bytes(rows))
    (folder / "tex" / "near.bin").write_bytes(bytes(len(rows)))
    tf = {"1": fc(2, mw, mh, 0x1BD03000, clamp=(2, 2), swizzle=(0, 0, 0, 5)), "2": fc(49, 4, 4, 0x14D40000),
          "13": fc(6, pw, ph, 0x13401000), "14": fc(6, pw, ph, 0x13441000), "16": fc(2, mw, mh, 0x1BD03000, clamp=(2, 2), swizzle=(0, 0, 0, 5))}
    out = [{"kind": "texture", "file": "tex/mask.bin", "bytes": len(stored), "fc": tf["1"]},
           {"kind": "texture", "file": "tex/normal.bin", "bytes": 16, "fc": tf["2"]},
           {"kind": "texture", "file": "tex/picture.bin", "bytes": len(rows), "fc": tf["13"]},
           {"kind": "texture", "file": "tex/near.bin", "bytes": len(rows), "fc": tf["14"]}]
    for matrix_ in views:
        out += view_rows(matrix_)
    if opaque:
        out.append(water_row("opaque", pixel=pixel, vertex=vertex, tf=tf, matrix=matrix))
    out.append(water_row("surface", pixel=pixel, vertex=vertex, tf=tf, matrix=matrix))
    out += list(extra)
    frame = {"kind": "frame", "frame": 1, "scene_frame": 7, "light": {"eye": list(EYE)}}
    if frame_fog:
        frame["fog"] = {"transmit": [0.9 - 0.002 * (i // 3) for i in range(3 * 256)],
                        "scatter": [0.3 + 0.004 * (i // 3) + 0.1 * (i % 3) for i in range(3 * 64)]}
    out.append(frame)
    path = folder / "native_discovery_made.jsonl"
    path.write_text("".join(_json(r) + "\n" for r in out), encoding="utf-8", newline="\n")
    return path


# --- a capture made with the water's switch on (plan Task 9: water_check.py records) --------------------------------

RECORD_PIXEL_REGISTERS = (0, 19, 20, 28, 31, 66, 67, 68, 116, 117, 126, 127, 128, 129, 130, 131, 136, 137, 138, 139, 140, 141, 142, 143)
FOG_TABLES = {"4": ["0x00804956", "0x1FAB105D", "0x0000003F", "0x00280D11", "0x00000003", "0x00000018"],
              "5": ["0x02004802", "0x1FAB0086", "0x000000FF", "0x00280C14", "0x00000003", "0x00000018"]}


def _bits(values):
    return ["0x%08X" % struct.unpack(">I", struct.pack(">f", v))[0] for v in values]


def record(kind, place, tf, pixel=None, vertex=None, pbool="0x00000110"):
    """One record of a frame row's "water" as the capture writes it, made from this file's registers and not from a
    row: what the capture read at the draw whose sequence number is `place`."""
    registers = dict(PIXEL)
    registers.update(pixel or {})
    vregisters = dict(VERTEX)
    vregisters.update(vertex or {})
    return {"place": place, "kind": kind, "ps_hash": "0x" + (PS if kind == "surface" else PS_OPAQUE), "vs_hash": "0x" + VS,
            "pbool": pbool, "stride": 5,
            "vs": sum((_bits(vregisters[reg]) for reg in (115, 116, 117, 118)), []),
            "ps": sum((_bits(registers.get(reg, (0.0, 0.0, 0.0, 0.0))) for reg in RECORD_PIXEL_REGISTERS), []),
            "fetch": [list(tf[slot]) for slot in ("1", "2", "13", "14")],
            "fog_base": ["0x%08X" % (int(FOG_TABLES[slot][1], 16) & 0xFFFFF000) for slot in ("4", "5")]}


def records_capture(folder, places=(5, 900), change=None, **kw):
    """capture(opaque=True) as a build with the water's switch on writes it: the opaque twin's row with "seq"
    places[0] and the surface's with places[1], the fog's two tables at slots 4 and 5 of both, and the frame row's
    "water": on, whole, seen one of each kind, taken 2, no reason, two records. `change(rows, water)`: called with
    the capture's rows (dicts, in file order) and the frame row's "water" before the file is written, to make one
    thing wrong."""
    path = capture(folder, opaque=True, **kw)
    rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    water = [r for r in rows if r.get("kind") == "draw" and r.get("in_scene") and (r.get("vs_hash") or "").upper().endswith(VS)]
    assert len(water) == 2 and water[0]["ps_hash"].endswith(PS_OPAQUE) and water[1]["ps_hash"].endswith(PS)
    listed = []
    for r, place, kind in zip(water, places, ("opaque", "surface")):
        r["seq"] = place
        r["tf"].update(FOG_TABLES)
        listed.append(record(kind, place, r["tf"], pixel=kw.get("pixel"), vertex=kw.get("vertex")))
    frame = [r for r in rows if r.get("kind") == "frame"][0]
    frame["water"] = {"on": True, "whole": True, "seen": {"surface": 1, "opaque": 1}, "taken": 2, "refused": {}, "records": listed}
    if change:
        change(rows, frame["water"])
    path.write_text("".join(_json(r) + "\n" for r in rows), encoding="utf-8", newline="\n")
    return path


# --- a made stored native scene (src/native/render/scene_dump.h; ledger ruling 46: water_replay.py main on made dumps) -

DUMP_POST, DUMP_LAYER, DUMP_PREPASS, DUMP_PRE_WATER, DUMP_TARGETS, DUMP_WATER = 1, 2, 4, 8, 16, 32
DUMP_TARGET_SIZE = 256


def scene_dump(width, height, flags, scene, depth, exposure=1.0, scene_frame=7, asked_frame=7, layers=(), pre_water=None, water=None,
               targets=(), bad_values=0):
    """The bytes of a stored native scene as render::PackSceneDump writes it (version 2 iff a flag above 4): the head,
    the linear target [h, w, 3] as floats, the depth [h, w] as levels, with DUMP_LAYER the two layer planes
    [h / 2, w / 2], with DUMP_PRE_WATER the depth before the water step, with DUMP_WATER the linear target after it,
    with DUMP_TARGETS the count and each target's (base, height, rgba [256, 256, 4] bytes). A plane whose flag is not
    set must not be given, and the other way round: this writer makes whole files only. HELD TO THE PASS'S WRITER by
    DUMP_LAYOUT_PIN (below): tests/native/test_scene_dump.cpp packs the same made planes with PackSceneDump and holds
    the same number."""
    import numpy as np
    if bool(flags & DUMP_LAYER) != (len(layers) == 2) or bool(flags & DUMP_PRE_WATER) != (pre_water is not None) or \
            bool(flags & DUMP_WATER) != (water is not None) or bool(flags & DUMP_TARGETS) != bool(targets) or len(targets) > 4:
        raise ValueError("the planes given are not the flags'")
    version = 2 if flags & (DUMP_PRE_WATER | DUMP_TARGETS | DUMP_WATER) else 1
    out = [struct.pack("<4sIIIQQIfII", b"F2NS", version, width, height, scene_frame, asked_frame, flags, exposure, bad_values, 0)]

    def floats(plane):
        a = np.asarray(plane, dtype="<f4")
        if a.shape != (height, width, 3):
            raise ValueError("a colour plane of %s" % (a.shape,))
        return a.tobytes()

    def levels(plane, w, h):
        a = np.asarray(plane, dtype="<u4")
        if a.shape != (h, w):
            raise ValueError("a depth plane of %s" % (a.shape,))
        return a.tobytes()

    out += [floats(scene), levels(depth, width, height)]
    out += [levels(p, width // 2, height // 2) for p in layers]
    if pre_water is not None:
        out.append(levels(pre_water, width, height))
    if water is not None:
        out.append(floats(water))
    if targets:
        out.append(struct.pack("<I", len(targets)))
        for base, level, rgba in targets:
            a = np.asarray(rgba, dtype=np.uint8)
            if a.shape != (DUMP_TARGET_SIZE, DUMP_TARGET_SIZE, 4):
                raise ValueError("a target of %s" % (a.shape,))
            out.append(struct.pack("<If", base, level) + a.tobytes())
    return b"".join(out)


# The layout's pin: FNV-1a (64 bits) of the file of pinned_dump(), a 4 x 2 scene with every flag and two targets, made
# here by scene_dump and in tests/native/test_scene_dump.cpp (case 114) by render::PackSceneDump from the same made
# planes. Each side holds this number, and tests/test_water_replay.py holds that the C++ test's text has it.
DUMP_LAYOUT_PIN = 0xBFBEAEE52E6818BF


def pinned_words(first):
    """The eight R11G11B10 words of a made 4 x 2 colour plane (no exponent of 31), texel i = first + its index."""
    return [((i * 37 + 5) % 0x7C0) | (((i * 101 + 64) % 0x7C0) << 11) | (((i * 53 + 33) % 0x3E0) << 22) for i in range(first, first + 8)]


def fnv1a64(data):
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def pinned_dump(decode):
    """The pinned file. `decode(word)`: the three floats of an R11G11B10 word (water_replay.r11g11b10)."""
    import numpy as np
    colour = lambda first: np.array([decode(w) for w in pinned_words(first)], dtype=np.float32).reshape(2, 4, 3)
    depth = np.array([(i * 1234567 + 89) & 0xFFFFFF for i in range(8)], dtype=np.uint32).reshape(2, 4)
    pre = np.array([(i * 7654321 + 3) & 0xFFFFFF for i in range(8)], dtype=np.uint32).reshape(2, 4)
    layers = [np.array([(j * 4099 + k * 17 + 1) & 0xFFFFFF for j in range(2)], dtype=np.uint32).reshape(1, 2) for k in range(2)]
    j = np.arange(DUMP_TARGET_SIZE * DUMP_TARGET_SIZE * 4, dtype=np.int64)
    targets = [(base, level, ((j * 31 + k * 7 + (j >> 8)) & 0xFF).astype(np.uint8).reshape(DUMP_TARGET_SIZE, DUMP_TARGET_SIZE, 4))
               for k, (base, level) in enumerate(((0x13401000, 20.1024), (0x13501000, 40.0)))]
    return scene_dump(4, 2, 63, colour(0), depth, exposure=7.5, scene_frame=77, asked_frame=76, layers=layers, pre_water=pre,
                      water=colour(8), targets=targets)
