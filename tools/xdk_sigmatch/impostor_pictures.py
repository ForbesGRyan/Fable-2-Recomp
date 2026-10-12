#!/usr/bin/env python3
"""The pictures of the far woods' impostors (frame-map section 19, "The pictures' format"): the Python twin of
render::Decode1555Level (src/native/render/texture_decode.h) and readers of a discovery capture's dumps.

Each impostor draw (vertex shader 129F7F2793DA9084, a BeginVertices quad list inside the main scene) samples a
256 x 256 texture of Xenos format 3 (k_1_5_5_5) at tf0: 16 bits a texel, tiled, the two bytes exchanged by the
fetch's endian 1; after that swap alpha is bit 15 and three 5-bit fields lie below it. Bits 0 to 4 are the
texture's red by the SDK's own reading (its load shader and its resolve: the frame map has the lines), so
LOW_FIELD_CHANNEL is 0, as k1555LowFieldChannel. The channels here are the TEXTURE's: the fetch constant's
swizzle (dword 3, bits 1 to 12) says which of them the shader's red, green and blue are, and `swizzle` /
`swizzled` apply it. In the captures so far the impostors' fetch names (2, 1, 0, 3): the shader's red is
channel 2.

  impostor_pictures.py sheet <capture.jsonl> [--frame F] [--out PNG]
      every format-3 picture of the capture (with --frame: the pictures of that frame's impostor draws) and its
      stored levels as one PNG, as the fetch gives the colours; a texel without alpha magenta, a texel the dump
      does not reach grey
  impostor_pictures.py mean <capture.jsonl> <frame>
      the mean of the texels with the alpha bit set over the pictures of the frame's impostor draws, in stored
      units 0 to 1: the texture's red, green, blue, and the same as the fetch's swizzle gives them
  impostor_pictures.py levels <capture.jsonl>...
      the fit of the native levels' rule (make_levels) to the levels the game stores: every rule of the family
      against every picture of the captures' impostor draws (captures made under --readback_resolve=full)
  impostor_pictures.py content <capture.jsonl>...
      the content rule's two counts (content_stats) for every whole format-3 base of the captures, and its verdict
  impostor_pictures.py used <native_scene_<stamp>.f2np> <capture.jsonl>
      the pictures the native pass used in a dumped frame against the capture's dumps of the same bases: the share
      of equal texels against each frame of the capture, and which frame each picture is (spec criterion 6)
  impostor_pictures.py compare <bin> <fc>
      a dumped base level through decode_1555 against the C++ test's <bin>.cpp.rgba (FABLE2_1555_BIN /
      FABLE2_1555_FC of tests/native/test_texture_decode.cpp); <fc>: the six dwords as hex, comma-separated
"""
import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

import numpy as np

IMPOSTOR_VS = "129F7F2793DA9084"
LOW_FIELD_CHANNEL = 0   # texture_decode.h, k1555LowFieldChannel (tests/test_impostor_pictures.py holds them equal)
MAX_EDGE = 8192         # what a fetch constant can name


def tiled_offsets(width, height, pitch, bpb_log2):
    """capture::TiledOffset2D (src/native/capture/xenos_tiling.h) for every texel of a width x height level:
    an int64 array [y, x] of byte offsets; pitch in blocks."""
    y, x = np.meshgrid(np.arange(height, dtype=np.int64), np.arange(width, dtype=np.int64), indexing="ij")
    pitch = (int(pitch) + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (bpb_log2 + 7)
    micro = ((x & 7) + ((y & 0xE) << 2)) << bpb_log2
    offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4)
    return (((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2)
            + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F))


def _words(data, width, height, pitch, tiled, endian, origin=(0, 0), partial=False):
    """(words [y, x] uint16 after the endian swap, have [y, x] bool). A texel outside `data` raises ValueError,
    or with `partial` reads 0 and is marked in `have`. `origin`: the level's block offset inside a packed tail."""
    width, height, pitch = int(width), int(height), int(pitch)
    if width < 1 or height < 1 or width > MAX_EDGE or height > MAX_EDGE:
        raise ValueError("no image of %d x %d" % (width, height))
    if pitch < width:
        raise ValueError("a pitch of %d texels below the width %d" % (pitch, width))
    ox, oy = int(origin[0]), int(origin[1])
    if tiled:
        off = tiled_offsets(width + ox, height + oy, pitch, 1)[oy:, ox:]
    else:
        y, x = np.meshgrid(np.arange(height, dtype=np.int64) + oy, np.arange(width, dtype=np.int64) + ox, indexing="ij")
        off = y * (((pitch + 31) & ~31) * 2) + x * 2
    have = off + 2 <= len(data)
    if not partial and not bool(have.all()):
        raise ValueError("a texel lies outside the %d bytes given" % len(data))
    raw = np.frombuffer(bytes(data) + b"\0\0", dtype=np.uint8)
    at = np.where(have, off, len(data))
    first, second = raw[at].astype(np.uint16), raw[at + 1].astype(np.uint16)
    # endian 1 (8-in-16) exchanges a texel's two bytes; every other value reads them as stored (the SDK's load shader)
    words = (first << 8) | second if endian == 1 else first | (second << 8)
    return np.where(have, words, 0).astype(np.uint16), have


def fields_1555(data, width, height, pitch, tiled, endian, origin=(0, 0)):
    """The stored fields of a level, uint8 [y, x, 4]: bits 0 to 4, bits 5 to 9, bits 10 to 14 (each 0 to 31) and
    the alpha bit (0 or 1). ValueError as decode_1555."""
    w, _ = _words(data, width, height, pitch, tiled, endian, origin)
    return np.stack([w & 31, (w >> 5) & 31, (w >> 10) & 31, w >> 15], axis=-1).astype(np.uint8)


def _rgba(fields, low_channel):
    if low_channel not in (0, 2):
        raise ValueError("the low field is channel 0 (red) or 2 (blue), not %r" % (low_channel,))
    f = fields.astype(np.uint32)
    out = np.zeros(fields.shape, dtype=np.uint8)
    out[..., low_channel] = (f[..., 0] * 255 + 15) // 31
    out[..., 1] = (f[..., 1] * 255 + 15) // 31
    out[..., 2 - low_channel] = (f[..., 2] * 255 + 15) // 31
    out[..., 3] = f[..., 3] * 255
    return out


def decode_1555(data, width, height, pitch, tiled, endian, low_channel=LOW_FIELD_CHANNEL):
    """render::Decode1555Level: k_1_5_5_5 -> RGBA8, uint8 [y, x, 4]. `pitch` in texels; a texel is 16 bits at the
    tiled offset, or linear at y * ((pitch + 31) & ~31) * 2 + x * 2; each field v -> (v * 255 + 15) // 31, alpha 0
    or 255; `low_channel`: the channel of bits 0 to 4 (0 red, 2 blue). ValueError where the C++ returns false: an
    empty image, one above 8,192 texels, a pitch below the width, a texel outside `data`."""
    if low_channel not in (0, 2):
        raise ValueError("the low field is channel 0 (red) or 2 (blue), not %r" % (low_channel,))
    return _rgba(fields_1555(data, width, height, pitch, tiled, endian), low_channel)


def fetch(fc):
    """The fields of a 2D fetch constant of format 3 that the decoder needs (render::DumpFetch's bits)."""
    fc = [int(v, 16) if isinstance(v, str) else int(v) for v in fc]
    if len(fc) != 6 or (fc[0] & 3) != 2 or ((fc[5] >> 9) & 3) != 1 or (fc[1] & 0x3F) != 3:
        raise ValueError("not a 2D fetch constant of format 3")
    return {"width": (fc[2] & 0x1FFF) + 1, "height": ((fc[2] >> 13) & 0x1FFF) + 1, "pitch": ((fc[0] >> 22) & 0x1FF) << 5,
            "tiled": (fc[0] >> 31) != 0, "endian": (fc[1] >> 6) & 3, "base": fc[1] & 0xFFFFF000, "mip": fc[5] & 0xFFFFF000,
            "mip_max": (fc[4] >> 6) & 0xF, "swizzle": swizzle(fc)}


def swizzle(fc):
    """The fetch constant's swizzle (dword 3, bits 1 to 12): for the shader's x, y, z, w the texture channel 0 to 3,
    4 for zero, 5 for one (render::TextureFetch::swizzle)."""
    word = int(fc[3], 16) if isinstance(fc[3], str) else int(fc[3])
    return tuple(s if s <= 5 else 4 for s in (((word >> (1 + 3 * i)) & 7) for i in range(4)))


def swizzled(rgb, swz):
    """Three texture channels as the fetch gives them to the shader's red, green and blue."""
    pick = lambda s: float(rgb[s]) if s < 3 else (1.0 if s == 5 else 0.0)   # noqa: E731 (3, alpha, is not a colour here: zero)
    return tuple(pick(s) if s != 3 else 0.0 for s in swz[:3])


def _level_fields(capture_dir, texture_row, level, partial=False):
    f = fetch(texture_row.get("fc") or [])
    capture_dir = Path(capture_dir)
    if level == 0:
        data = (capture_dir / texture_row["file"]).read_bytes()
        w, have = _words(data, f["width"], f["height"], f["pitch"], f["tiled"], f["endian"], partial=partial)
    else:
        mips = texture_row.get("mips") or {}
        lv = next((l for l in mips.get("levels") or [] if int(l.get("level", 0)) == level), None)
        if lv is None:
            raise ValueError("%s has no stored level %d" % (texture_row.get("file"), level))
        data = (capture_dir / mips["file"]).read_bytes()
        offset = int(lv["offset"])
        if offset >= len(data):
            raise ValueError("level %d lies outside its file" % level)
        w, have = _words(data[offset:], int(lv["w"]), int(lv["h"]), int(lv["pitch"]) // 2, bool(mips.get("tiled")), f["endian"],
                         (int(lv.get("x", 0)), int(lv.get("y", 0))), partial=partial)
    return np.stack([w & 31, (w >> 5) & 31, (w >> 10) & 31, w >> 15], axis=-1).astype(np.uint8), have


def picture(capture_dir, texture_row, level, partial=False, low_channel=LOW_FIELD_CHANNEL):
    """A dumped base (level 0) or stored level of a capture's format-3 texture row as RGBA8 [y, x, 4].
    `capture_dir`: the folder of the capture's .jsonl (the row's files are named from there). A dump that does not
    hold every texel raises ValueError; with `partial` the result is (rgba, have): what is there, and where."""
    fields, have = _level_fields(capture_dir, texture_row, level, partial)
    rgba = _rgba(fields, low_channel)
    return (rgba, have) if partial else rgba


def mean(field_pictures, low_channel=LOW_FIELD_CHANNEL):
    """((red, green, blue), count): the mean of the texels with the alpha bit set over `field_pictures` (arrays of
    fields_1555), in stored units 0 to 1 (a field / 31), the texture's channels; (None, 0) when no texel is set."""
    total, count = np.zeros(3, dtype=np.int64), 0
    for f in field_pictures:
        on = f[..., 3] == 1
        total += f[on][:, :3].astype(np.int64).sum(axis=0)
        count += int(on.sum())
    if count == 0:
        return None, 0
    m = [float(total[i]) / (31.0 * count) for i in range(3)]
    return ((m[0], m[1], m[2]) if low_channel == 0 else (m[2], m[1], m[0])), count


def read_rows(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except ValueError:
                pass   # a partial last line (the capture was cut off)
    return rows


def _is_format3(row):
    try:
        fetch(row.get("fc") or [])
        return True
    except ValueError:
        return False


def format3_rows(rows):
    """The capture's texture rows of 2D format-3 textures, in the capture's order."""
    return [r for r in rows if r.get("kind") == "texture" and _is_format3(r)]


def frame_pictures(rows, frame):
    """[(n, texture row | None)] for the frame's impostor draws (in-scene "verts" rows of IMPOSTOR_VS), in order:
    the texture row of the draw's tf0 by base address and, where the draw names one ("tfc"), by content; a capture
    from before the format was kept by content has one row an address. None: no row holds what the draw sampled."""
    textures = format3_rows(rows)
    out = []
    for r in rows:
        if r.get("kind") != "verts" or not r.get("in_scene") or r.get("frame") != frame:
            continue
        if IMPOSTOR_VS not in str((r.get("vs") or {}).get("hash", "")).upper():
            continue
        fc = (r.get("tf") or {}).get("0")
        found = None
        if fc and _is_format3({"fc": fc}):
            base = fetch(fc)["base"]
            content = (r.get("tfc") or {}).get("0")
            for t in textures:
                if fetch(t["fc"])["base"] != base:
                    continue
                if content is None or str(t.get("content", "")).upper() == str(content).upper():
                    found = t
                    break
        out.append((r.get("n"), found))
    return out


def write_png(path, rgb):
    """An RGB uint8 [y, x, 3] array as a PNG (no dependency beyond zlib)."""
    h, w = rgb.shape[:2]
    raw = b"".join(b"\0" + rgb[y].tobytes() for y in range(h))

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


CLEAR, MISSING = (255, 0, 255), (40, 40, 40)
TILE_W, TILE_H = 256 + 128, 256


def _shown(fields, have, swz):
    """A level as the fetch gives its colours: RGB uint8, CLEAR where the alpha bit is off, MISSING where no byte is."""
    rgba = _rgba(fields, LOW_FIELD_CHANNEL)
    out = np.zeros(fields.shape[:2] + (3,), dtype=np.uint8)
    for i, s in enumerate(swz[:3]):
        out[..., i] = rgba[..., s] if s < 3 else (255 if s == 5 else 0)
    out[fields[..., 3] == 0] = CLEAR
    out[~have] = MISSING
    return out


def sheet(capture, frame=None, out=None, columns=7):
    capture = Path(capture)
    rows = read_rows(capture)
    if frame is None:
        chosen = format3_rows(rows)
    else:
        chosen = [t for _, t in frame_pictures(rows, frame) if t is not None]
    print("%d format-3 pictures%s" % (len(chosen), "" if frame is None else " of frame %d's impostor draws" % frame))
    if not chosen:
        return 1
    tiles = []
    for t in chosen:
        f = fetch(t["fc"])
        tile = np.full((TILE_H, TILE_W, 3), 0, dtype=np.uint8)
        fields, have = _level_fields(capture.parent, t, 0, partial=True)
        base = _shown(fields, have, f["swizzle"])
        tile[:min(TILE_H, base.shape[0]), :min(256, base.shape[1])] = base[:TILE_H, :256]
        on = (fields[..., 3] == 1) & have
        m, count = mean([np.where(have[..., None], fields, 0)])
        line = "%s  %dx%d tiled %d endian %d pitch %d swizzle %s  texels there %.1f%%  alpha on %.3f  mean (texture r,g,b) %s" % (
            t["file"].split("/")[-1], f["width"], f["height"], f["tiled"], f["endian"], f["pitch"], "".join(str(s) for s in f["swizzle"]),
            100.0 * float(have.mean()), float(on.sum()) / max(1, int(have.sum())), None if m is None else tuple(round(v, 3) for v in m))
        y = 0
        levels = []
        for lv in (t.get("mips") or {}).get("levels") or []:
            n = int(lv["level"])
            lf, lh = _level_fields(capture.parent, t, n, partial=True)
            shown = _shown(lf, lh, f["swizzle"])
            if y + shown.shape[0] <= TILE_H and shown.shape[1] <= 128:
                tile[y:y + shown.shape[0], 256:256 + shown.shape[1]] = shown
                y += shown.shape[0]
            levels.append("%d: %dx%d alpha on %.3f" % (n, shown.shape[1], shown.shape[0],
                                                       float(((lf[..., 3] == 1) & lh).sum()) / max(1, int(lh.sum()))))
        print(line + ("  levels " + ", ".join(levels) if levels else "  no stored levels"))
        tiles.append(tile)
    grid_rows = (len(tiles) + columns - 1) // columns
    img = np.zeros((grid_rows * TILE_H, columns * TILE_W, 3), dtype=np.uint8)
    for i, tile in enumerate(tiles):
        img[(i // columns) * TILE_H:(i // columns + 1) * TILE_H, (i % columns) * TILE_W:(i % columns + 1) * TILE_W] = tile
    stamp = capture.stem.replace("native_discovery_", "")
    out = Path(out) if out else Path("out/native-evidence/distant-trees") / ("t7_sheet_%s%s.png" % (stamp, "" if frame is None else "_f%d" % frame))
    out.parent.mkdir(parents=True, exist_ok=True)
    write_png(out, img)
    print("wrote", out.as_posix())
    return 0


def mean_cli(capture, frame):
    capture = Path(capture)
    rows = read_rows(capture)
    draws = frame_pictures(rows, frame)
    if not draws:
        print("frame %d has no impostor draw" % frame)
        return 1
    missing = [n for n, t in draws if t is None]
    if missing:
        print("frame %d: %d of %d impostor draws have no picture in the capture (verts rows %s)" % (frame, len(missing), len(draws), missing))
        return 1
    pictures, swizzles = [], set()
    for n, t in draws:
        try:
            fields, _ = _level_fields(capture.parent, t, 0)
        except ValueError as e:
            print("frame %d verts %s: %s: %s" % (frame, n, t["file"], e))
            return 1
        pictures.append(fields)
        swizzles.add(fetch(t["fc"])["swizzle"])
    m, count = mean(pictures)
    if m is None:
        print("frame %d: no texel of %d pictures has the alpha bit set" % (frame, len(pictures)))
        return 1
    total = sum(int(p.shape[0]) * int(p.shape[1]) for p in pictures)
    print("capture %s frame %d: %d pictures, %d of %d texels with the alpha bit set (%.4f)" % (
        capture.stem, frame, len(pictures), count, total, count / total))
    print("mean, the texture's channels (red = bits 0 to 4), stored units 0 to 1: %.6f %.6f %.6f" % m)
    for swz in sorted(swizzles):
        print("mean as the fetch gives it to the shader (swizzle %s): %.6f %.6f %.6f" % (("".join(str(s) for s in swz),) + swizzled(m, swz)))
    if len(swizzles) != 1:
        print("the pictures' fetch constants name %d different swizzles" % len(swizzles))
        return 1
    return 0


def compare(bin_path, fcs):
    fc = [int(v, 16) for v in fcs.split(",")]
    f = fetch(fc)
    cpp = Path(str(bin_path) + ".cpp.rgba")
    if not cpp.exists():
        print("no %s: run tests/native/test_texture_decode.cpp's executable with FABLE2_1555_BIN and FABLE2_1555_FC set" % cpp)
        return 2
    mine = decode_1555(Path(bin_path).read_bytes(), f["width"], f["height"], f["pitch"], f["tiled"], f["endian"]).tobytes()
    theirs = cpp.read_bytes()
    if mine == theirs:
        print("%s: %d x %d, equal texel for texel (%d bytes)" % (Path(bin_path).name, f["width"], f["height"], len(mine)))
        return 0
    different = sum(1 for a, b in zip(mine, theirs) if a != b) + abs(len(mine) - len(theirs))
    print("%s: %d bytes differ (%d here, %d from the C++ decoder)" % (Path(bin_path).name, different, len(mine), len(theirs)))
    return 1


# --- The native levels and the content rule (plan Task 13; frame-map section 19, "The pictures: levels and content")
#
# The game's read-back list brings a picture's base level to guest memory and not its three stored levels, so the
# native view makes levels 1 to 3 itself (render::MakeImpostorLevels, src/native/render/impostor_picture.h). The
# rule is one of a family, fitted to the levels the game stores (captures made under --readback_resolve=full):
#   source  "above": a level from the level above it, as this rule made it; "base": from the base's 2^k x 2^k block
#   colour  "all": each five-bit field the mean of all parents', a half rounded up; "covered": of the covered
#           parents' alone (zero without one)
#   count   a texel is covered when at least count of 4 parents are (from the base: that share of its block)
LEVEL_SOURCES, LEVEL_COLOURS = ("above", "base"), ("all", "covered")
LEVEL_RULE = ("above", "all", 3)      # as fitted to D1 and D2 (`levels`); kImpostorLevelAlphaCount is its count
PICTURE_SIZE = 256
RING_TEXELS = 4 * (PICTURE_SIZE - 1)
# The content rule (`content`): of the base's outermost ring at least RING_CLEAR_MIN texels are clear, and the
# covered texels lie in [COVER_MIN, COVER_MAX]. The native kImpostorRingClearMin, kImpostorCoverMin, kImpostorCoverMax.
RING_CLEAR_MIN, COVER_MIN, COVER_MAX = 944, 1024, 25800


def _reduce(fields, factor, colour, count):
    """One level from `fields` by blocks of factor x factor parents."""
    h, w = fields.shape[0] // factor, fields.shape[1] // factor
    blocks = fields.reshape(h, factor, w, factor, 4).astype(np.int64)
    parents = factor * factor
    covered = blocks[..., 3].sum(axis=(1, 3))
    out = np.zeros((h, w, 4), dtype=np.uint8)
    if colour == "all":
        total = blocks[..., :3].sum(axis=(1, 3))
        out[..., :3] = (2 * total + parents) // (2 * parents)
    else:
        total = (blocks[..., :3] * blocks[..., 3:4]).sum(axis=(1, 3))
        n = np.maximum(covered, 1)[..., None]
        out[..., :3] = np.where(covered[..., None] > 0, (2 * total + n) // (2 * n), 0)
    out[..., 3] = covered * 4 >= count * parents
    return out


def make_levels(base_fields, source=LEVEL_RULE[0], colour=LEVEL_RULE[1], count=LEVEL_RULE[2], levels=3):
    """The lower levels of a picture from its base, each uint8 [y, x, 4] of fields as fields_1555 gives them
    (three five-bit fields and the alpha bit): `levels` arrays of half, a quarter, ... of the base's edge.
    ValueError for a rule outside the family or a base that is not a square of a power of two with that many
    halvings."""
    if source not in LEVEL_SOURCES or colour not in LEVEL_COLOURS or count not in (1, 2, 3, 4):
        raise ValueError("no such rule: %r, %r, %r" % (source, colour, count))
    edge = base_fields.shape[0]
    if base_fields.ndim != 3 or base_fields.shape[1] != edge or base_fields.shape[2] != 4 or edge & (edge - 1) or edge >> levels < 1:
        raise ValueError("no base for %d levels: shape %r" % (levels, base_fields.shape))
    out, above = [], base_fields
    for k in range(1, levels + 1):
        level = _reduce(above, 2, colour, count) if source == "above" else _reduce(base_fields, 1 << k, colour, count)
        out.append(level)
        above = level
    return out


def content_stats(base_fields):
    """(ring_clear, covered) of a 256 x 256 base: how many of the 1,020 texels of its outermost ring have the alpha
    bit clear, and how many texels of the level have it set (render::MeasureImpostorPicture)."""
    if base_fields.shape != (PICTURE_SIZE, PICTURE_SIZE, 4):
        raise ValueError("not a base level: shape %r" % (base_fields.shape,))
    a = base_fields[..., 3] != 0
    ring = np.zeros(a.shape, dtype=bool)
    ring[0, :] = ring[-1, :] = ring[:, 0] = ring[:, -1] = True
    return int((ring & ~a).sum()), int(a.sum())


def picture_usable(ring_clear, covered):
    """render::ImpostorPictureUsable: a clear ring and a covered middle of a tree's size."""
    return ring_clear >= RING_CLEAR_MIN and COVER_MIN <= covered <= COVER_MAX


def _frame_bases(capture, frames=None):
    """[(frame, texture row)] of the pictures of a capture's impostor draws, for every frame row (or `frames`)."""
    capture = Path(capture)
    rows = read_rows(capture)
    numbers = frames or sorted({r.get("frame") for r in rows if r.get("kind") == "verts" and r.get("in_scene")
                                and IMPOSTOR_VS in str((r.get("vs") or {}).get("hash", "")).upper()})
    return capture, rows, [(n, t) for n in numbers for _, t in frame_pictures(rows, n) if t is not None]


def levels_cli(captures, out=sys.stdout):
    """The fit: every rule of the family against the stored levels of the captures' pictures. Per rule and level
    the share of texels whose alpha bit is the rule's, and of the texels the game's level covers the share whose
    three fields are within 2 (of 31) of the rule's; the worst picture of each. Criterion 5: 97% and 95%."""
    pictures = []
    for capture in captures:
        capture, _, bases = _frame_bases(capture)
        for frame, t in bases:
            try:
                base = _level_fields(capture.parent, t, 0)[0]
                stored = [_level_fields(capture.parent, t, k)[0] for k in (1, 2, 3)]
            except ValueError as e:
                print("%s frame %s %s: left out: %s" % (capture.stem, frame, t.get("file"), e), file=out)
                continue
            pictures.append((capture.stem[-6:], frame, t["file"].split("/")[-1][:8], base, stored))
    print("%d pictures with a base and three stored levels, of %s" % (len(pictures), ", ".join(Path(c).stem[-6:] for c in captures)), file=out)
    if not pictures:
        return 1
    results = []
    for source in LEVEL_SOURCES:
        for colour in LEVEL_COLOURS:
            for count in (1, 2, 3, 4):
                alpha_ok, alpha_n, col_ok, col_n = np.zeros(3), np.zeros(3), np.zeros(3), np.zeros(3)
                worst_a, worst_c = [1.0] * 3, [1.0] * 3
                for _, _, _, base, stored in pictures:
                    made = make_levels(base, source, colour, count)
                    for k in range(3):
                        same = made[k][..., 3] == stored[k][..., 3]
                        on = stored[k][..., 3] == 1
                        near = (np.abs(made[k][..., :3].astype(np.int16) - stored[k][..., :3].astype(np.int16)) <= 2).all(axis=-1)
                        alpha_ok[k] += same.sum(); alpha_n[k] += same.size
                        col_ok[k] += (near & on).sum(); col_n[k] += on.sum()
                        worst_a[k] = min(worst_a[k], float(same.mean()))
                        if on.any():
                            worst_c[k] = min(worst_c[k], float((near & on).sum()) / float(on.sum()))
                results.append((source, colour, count, alpha_ok / alpha_n, col_ok / np.maximum(col_n, 1), worst_a, worst_c))
    print("rule (source, colour, count): alpha equal at levels 1, 2, 3 (worst picture) | colour within 2 on the game's covered texels (worst picture)", file=out)
    for source, colour, count, a, c, wa, wc in results:
        mark = "  <- LEVEL_RULE" if (source, colour, count) == LEVEL_RULE else ""
        print("  %-5s %-7s %d: alpha %s (%s) | colour %s (%s)%s" % (
            source, colour, count, " ".join("%.4f" % v for v in a), " ".join("%.4f" % v for v in wa),
            " ".join("%.4f" % v for v in c), " ".join("%.4f" % v for v in wc), mark), file=out)
    best = max(results, key=lambda r: (float(min(r[3])) + float(min(r[4])), float(r[3].sum() + r[4].sum())))
    print("best by the lower of its figures: %s %s %d" % best[:3], file=out)
    fitted = next(r for r in results if r[:3] == LEVEL_RULE)
    met = bool((fitted[3] >= 0.97).all() and (fitted[4] >= 0.95).all())
    print("criterion 5 for LEVEL_RULE %r (alpha 97%% of the texels of each level, colour 95%% of the covered ones): %s" % (
        LEVEL_RULE, "met" if met else "not met as written"), file=out)
    return 0 if best[:3] == LEVEL_RULE else 1


def content_cli(captures, out=sys.stdout):
    """The content rule's two counts for every format-3 base of the captures (whole dumps only), and what the rule
    says of each."""
    used = refused = 0
    for capture in captures:
        capture = Path(capture)
        rows = read_rows(capture)
        for t in format3_rows(rows):
            try:
                base = _level_fields(capture.parent, t, 0)[0]
            except ValueError as e:
                print("%s %s: not whole: %s" % (capture.stem[-6:], t["file"].split("/")[-1], e), file=out)
                continue
            ring, covered = content_stats(base)
            ok = picture_usable(ring, covered)
            used += ok
            refused += not ok
            print("%s %-48s ring clear %4d of %d, covered %5d (%.3f): %s" % (
                capture.stem[-6:], t["file"].split("/")[-1], ring, RING_TEXELS, covered, covered / 65536.0, "usable" if ok else "REFUSED"), file=out)
    print("usable %d, refused %d; the rule: ring clear >= %d and %d <= covered <= %d" % (used, refused, RING_CLEAR_MIN, COVER_MIN, COVER_MAX), file=out)
    return 0


# --- The pictures the pass used (plan Task 14; spec criterion 6) ---
USED_MAGIC, USED_MAX, USED_BOUND = 0x504E3246, 32, 0.98      # "F2NP"; render::kImpostorPicturesMax; criterion 6's share


def read_used(path):
    """(head, pictures) of a native_scene_<stamp>.f2np (render::PackImpostorPictures): head {"scene_frame", "count"};
    pictures [{"base", "place", "hash", "rgba": uint8 [256, 256, 4], the texture's channels as uploaded}].
    ValueError for anything render::ParseImpostorPictures refuses."""
    data = Path(path).read_bytes()
    if len(data) < 24:
        raise ValueError("%s: shorter than a head" % path)
    magic, version, scene_frame, count, reserved = struct.unpack_from("<IIQII", data, 0)
    one = 16 + PICTURE_SIZE * PICTURE_SIZE * 4
    if magic != USED_MAGIC or version != 1 or reserved != 0 or count > USED_MAX or len(data) != 24 + count * one:
        raise ValueError("%s: not a pictures file (magic 0x%08X, version %d, count %d, %d bytes)" % (path, magic, version, count, len(data)))
    pictures = []
    for i in range(count):
        at = 24 + i * one
        base, place, content = struct.unpack_from("<IIQ", data, at)
        rgba = np.frombuffer(data, dtype=np.uint8, count=one - 16, offset=at + 16).reshape(PICTURE_SIZE, PICTURE_SIZE, 4)
        pictures.append({"base": base, "place": place, "hash": content, "rgba": rgba})
    return {"scene_frame": scene_frame, "count": count}, pictures


def used(f2np, capture):
    """Each picture of the file against the capture's dump of the same base in every frame of the capture: the
    share of texels equal in all four channels. {"scene_frame", "paired_frame" (the capture frame whose row names
    the file's scene frame, or None), "pictures": [{"base", "place", "hash", "equal": {frame: share}, "best_frame"}]}."""
    head, pictures = read_used(f2np)
    capture = Path(capture)
    rows = read_rows(capture)
    frames = sorted({r["frame"] for r in rows if r.get("kind") == "frame" and "frame" in r})
    paired = next((r["frame"] for r in rows if r.get("kind") == "frame" and r.get("scene_frame") == head["scene_frame"]), None)
    dumps = {}
    for n in frames:
        for _, tex in frame_pictures(rows, n):
            if tex is None:
                continue
            try:
                dumps.setdefault(fetch(tex["fc"])["base"], {})[n] = picture(capture.parent, tex, 0)
            except ValueError:
                pass
    out = []
    for pic in pictures:
        equal = {n: float((rgba == pic["rgba"]).all(axis=-1).mean()) for n, rgba in sorted(dumps.get(pic["base"], {}).items())}
        best = max(equal, key=lambda n: (equal[n], -abs(n - (paired or n)))) if equal else None
        out.append({"base": pic["base"], "place": pic["place"], "hash": pic["hash"], "equal": equal, "best_frame": best})
    return {"scene_frame": head["scene_frame"], "paired_frame": paired, "pictures": out}


def used_cli(f2np, capture, out=sys.stdout):
    result = used(f2np, capture)
    print("%s: scene frame %d, %d pictures; the capture's frame of that scene: %s" % (
        Path(f2np).name, result["scene_frame"], len(result["pictures"]), result["paired_frame"]), file=out)
    held = 0
    for pic in result["pictures"]:
        shares = ", ".join("frame %d %.4f" % (n, s) for n, s in pic["equal"].items()) or "no dump of this base in the capture"
        ok = bool(pic["equal"]) and max(pic["equal"].values()) >= USED_BOUND
        held += ok
        age = "" if pic["best_frame"] is None or result["paired_frame"] is None else " (the paired frame%+d)" % (pic["best_frame"] - result["paired_frame"])
        print("  base 0x%08X place %d hash 0x%016X: texels equal to the capture's dump: %s; nearest frame %s%s: %s" % (
            pic["base"], pic["place"], pic["hash"], shares, pic["best_frame"], age, "held" if ok else "NOT HELD"), file=out)
    print("criterion 6 (each picture equal on at least %.0f%% of its texels to a dump of the capture): %d of %d held" % (
        100 * USED_BOUND, held, len(result["pictures"])), file=out)
    return 0 if result["pictures"] and held == len(result["pictures"]) else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("sheet")
    s.add_argument("capture")
    s.add_argument("--frame", type=int)
    s.add_argument("--out")
    m = sub.add_parser("mean")
    m.add_argument("capture")
    m.add_argument("frame", type=int)
    c = sub.add_parser("compare")
    c.add_argument("bin")
    c.add_argument("fc")
    lv = sub.add_parser("levels")
    lv.add_argument("captures", nargs="+")
    ct = sub.add_parser("content")
    ct.add_argument("captures", nargs="+")
    u = sub.add_parser("used")
    u.add_argument("f2np")
    u.add_argument("capture")
    a = ap.parse_args(argv)
    if a.cmd == "used":
        return used_cli(a.f2np, a.capture)
    if a.cmd == "levels":
        return levels_cli(a.captures)
    if a.cmd == "content":
        return content_cli(a.captures)
    if a.cmd == "sheet":
        return sheet(a.capture, a.frame, a.out)
    if a.cmd == "mean":
        return mean_cli(a.capture, a.frame)
    return compare(a.bin, a.fc)


if __name__ == "__main__":
    raise SystemExit(main())
