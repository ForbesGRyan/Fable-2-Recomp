#!/usr/bin/env python3
"""The lake's textures (frame-map section 20, "The water's textures"): the Python twins of render::DecodeK8Level,
render::DecodeDxnLevelRg8 and render::WaterTextureFetch (src/native/render/texture_decode.h) and readers of a
discovery capture's dumps.

A water draw (vertex shader BEAD84BD72072E0E) samples a shore mask at slot 1 (and its vertex shader the same
texture at slot 16): Xenos format 2, k_8, one byte a texel; a normal map at slot 2: format 49, k_DXN, 4 x 4
blocks of 16 bytes, the BC4 block of x and then that of y after the fetch's endian swap, here as two bytes a
texel, each palette entry rounded to nearest (spec ruling 20); and a reflection picture at slot 13 (k_8_8_8_8,
256 x 256; slot 14 the same shape, all zero in the captures so far). The channels here are the TEXTURE's: the
fetch constant's swizzle says which of them the shader's components are.

  water_textures.py fetches <capture.jsonl>
      the fetch constants of slots 1, 2, 13, 14 (and the vertex shader's 16) of every water row, decoded: size,
      pitch, tiling, endian, swizzle, clamp, filters, levels, signs; one line a distinct constant with its rows
  water_textures.py sheet <capture.jsonl> [--out PNG]
      the masks the water rows name and the normal map (x, y and the two together) as one PNG
  water_textures.py census <capture.jsonl>
      each mask's values and the alpha the pixel shader makes of them with the rows' own c141:
      alpha = 1 - saturate(c141.x - (1 - m) * c141.y) (the research's b_masks.py, line for line)
  water_textures.py levels <capture.jsonl>
      the normal map's stored levels (a capture whose texture row names them) against the 2 x 2 mean pyramid of
      its decoded base: the largest and the mean difference a level and channel
  water_textures.py compare <bin> <fc>
      a dumped mask or normal map through the decoder here against the C++ test's <bin>.cpp.r8 or
      <bin>.cpp.rg8 (FABLE2_K8_BIN / FABLE2_K8_FC, FABLE2_WDXN_BIN / FABLE2_WDXN_FC of
      tests/native/test_texture_decode.cpp); <fc>: the six dwords as hex, comma-separated
"""
import argparse
import json
import struct
import sys
from pathlib import Path

import numpy as np

import impostor_pictures as ip   # tiled_offsets (held to the scalar port by its own test and by this file's), write_png

WATER_VS = "BEAD84BD72072E0E"                 # texture_decode.h, kWaterVsHash
XENOS_K8, XENOS_DXN = 2, 49                   # texture_decode.h, kXenosK8, kXenosDxn
FETCH_LIMIT = {"k8": 256, "dxn": 1024}        # WaterTextureFetch's kSize rule
MAX_EDGE = 8192                               # what a fetch constant can name
FILTERS = ("point", "linear", "basemap", "fetch-const")


def decode_k8(data, width, height, pitch, tiled):
    """render::DecodeK8Level: k_8 -> uint8 [y, x]. `pitch` in texels; a texel is one byte at the tiled offset, or
    linear at y * ((pitch + 31) & ~31) + x. ValueError where the C++ returns false: an empty image, a side above
    8,192, a pitch below the width, a texel outside `data`."""
    width, height, pitch = int(width), int(height), int(pitch)
    if width < 1 or height < 1 or width > MAX_EDGE or height > MAX_EDGE:
        raise ValueError("no image of %d x %d" % (width, height))
    if pitch < width:
        raise ValueError("a pitch of %d texels below the width %d" % (pitch, width))
    if tiled:
        off = ip.tiled_offsets(width, height, pitch, 0)
    else:
        y, x = np.meshgrid(np.arange(height, dtype=np.int64), np.arange(width, dtype=np.int64), indexing="ij")
        off = y * ((pitch + 31) & ~31) + x
    if int(off.max()) + 1 > len(data):
        raise ValueError("a texel lies outside the %d bytes given" % len(data))
    return np.frombuffer(bytes(data), dtype=np.uint8)[off].copy()


def _swap(blocks, endian):
    """render::CopySwapped over rows of 16 bytes: the result is what the GPU reads."""
    if endian == 1:
        return blocks.reshape(-1, 8, 2)[:, :, ::-1].reshape(-1, 16)
    if endian == 2:
        return blocks.reshape(-1, 4, 4)[:, :, ::-1].reshape(-1, 16)
    if endian == 3:
        return blocks.reshape(-1, 4, 2, 2)[:, :, ::-1, :].reshape(-1, 16)
    return blocks


def _bc4_bytes(blk, rounded=True):
    """The sixteen values of each 8-byte BC4 block of `blk` [n, 8] as bytes [n, 16], rounded to nearest the way
    the C++ does it: render::Bc4Value's single-precision value, / 255, * 255 + 0.5, truncated. `rounded=False`: the
    palette entries themselves, 0 to 255 and not whole (float32 [n, 16]): what a host BC5 fetch reads."""
    a0, a1 = blk[:, 0].astype(np.int64), blk[:, 1].astype(np.int64)
    bits = np.zeros(len(blk), dtype=np.int64)
    for b in range(6):
        bits |= blk[:, 2 + b].astype(np.int64) << (8 * b)
    out = np.zeros((len(blk), 16), dtype=np.uint8 if rounded else np.float32)
    seven, five, full, half = np.float32(7), np.float32(5), np.float32(255), np.float32(0.5)
    many = a0 > a1
    for i in range(16):
        k = (bits >> (3 * i)) & 7
        v7 = ((8 - k) * a0 + (k - 1) * a1).astype(np.float32) / seven
        v5 = ((6 - k) * a0 + (k - 1) * a1).astype(np.float32) / five
        tail = np.where(k == 6, np.float32(0), full)
        v = np.where(k == 0, a0.astype(np.float32), np.where(k == 1, a1.astype(np.float32),
                     np.where(many, v7, np.where(k < 6, v5, tail)))).astype(np.float32)
        out[:, i] = ((v / full) * full + half).astype(np.float32).astype(np.uint8) if rounded else v
    return out


def decode_dxn_rg8(data, width, height, pitch_blocks, tiled, endian, origin=(0, 0), rounded=True):
    """render::DecodeDxnLevelRg8: a k_DXN level -> uint8 [y, x, 2] (x, y), each palette entry rounded to nearest.
    `rounded=False`: float32 [y, x, 2] of the palette entries as they are (0 to 255, not whole): ledger ruling 32, the
    normal map is sampled by the GPU as BC5, and the 8-bit decode is the reference of tests and replays only.
    `pitch_blocks`: the level's pitch in 4 x 4 blocks (at least its width in blocks); blocks tiled at
    TiledOffset2D(bx, by, pitch, 4) or linear in rows of the pitch aligned to 32 blocks, 16 bytes each.
    `origin`: the level's block offset inside a packed tail (the C++ caller lifts such a level out with
    UntileLevel first). ValueError where the C++ returns false: an empty image, a side above 8,192, a block
    outside `data`."""
    width, height = int(width), int(height)
    if width < 1 or height < 1 or width > MAX_EDGE or height > MAX_EDGE:
        raise ValueError("no image of %d x %d" % (width, height))
    wb, hb = (width + 3) // 4, (height + 3) // 4
    ox, oy = int(origin[0]), int(origin[1])
    pitch = max(int(pitch_blocks), wb)
    if tiled:
        off = ip.tiled_offsets(wb + ox, hb + oy, pitch, 4)[oy:, ox:]
    else:
        by, bx = np.meshgrid(np.arange(hb, dtype=np.int64) + oy, np.arange(wb, dtype=np.int64) + ox, indexing="ij")
        off = by * (((pitch + 31) & ~31) * 16) + bx * 16
    if int(off.max()) + 16 > len(data):
        raise ValueError("a block lies outside the %d bytes given" % len(data))
    raw = np.frombuffer(bytes(data), dtype=np.uint8)
    blocks = _swap(raw[(off.reshape(-1, 1) + np.arange(16, dtype=np.int64)).reshape(-1)].reshape(-1, 16), int(endian))
    xs = _bc4_bytes(blocks[:, 0:8], rounded).reshape(hb, wb, 4, 4)
    ys = _bc4_bytes(blocks[:, 8:16], rounded).reshape(hb, wb, 4, 4)
    both = np.stack([xs, ys], axis=-1)                          # [by, bx, row, column, 2]
    return np.ascontiguousarray(both.transpose(0, 2, 1, 3, 4).reshape(hb * 4, wb * 4, 2)[:height, :width])


def _words(fc):
    words = [int(v, 16) if isinstance(v, str) else int(v) for v in fc]
    if len(words) != 6:
        raise ValueError("a fetch constant is six dwords")
    return words


def describe(fc):
    """Every field of a texture fetch constant a census prints (the SDK's xe_gpu_texture_fetch_t, host-order
    dwords), whatever its format. ValueError for a constant that is no texture's."""
    fc = _words(fc)
    if (fc[0] & 3) != 2:
        raise ValueError("not a texture fetch constant")
    width, height = (fc[2] & 0x1FFF) + 1, ((fc[2] >> 13) & 0x1FFF) + 1
    mip = fc[5] & 0xFFFFF000
    mip_max = (fc[4] >> 6) & 0xF
    log2 = max(width, height).bit_length() - 1
    lod_bias = (fc[4] >> 12) & 0x3FF
    return {"format": fc[1] & 0x3F, "dimension": (fc[5] >> 9) & 3, "width": width, "height": height,
            "pitch": ((fc[0] >> 22) & 0x1FF) << 5, "tiled": (fc[0] >> 31) != 0, "endian": (fc[1] >> 6) & 3,
            "base": fc[1] & 0xFFFFF000, "mip": mip, "mip_min": (fc[4] >> 2) & 0xF, "mip_max": mip_max,
            "packed": ((fc[5] >> 11) & 1) != 0, "levels": (min(mip_max, log2) + 1) if mip else 1,   # render::LevelCount
            "swizzle": ip.swizzle(fc), "clamp": ((fc[0] >> 10) & 7, (fc[0] >> 13) & 7),
            "sign": tuple((fc[0] >> (2 + 2 * i)) & 3 for i in range(4)),
            "filter": (FILTERS[(fc[3] >> 19) & 3], FILTERS[(fc[3] >> 21) & 3], FILTERS[(fc[3] >> 23) & 3]),
            "aniso": (fc[3] >> 25) & 7, "lod_bias": (lod_bias - 1024 if lod_bias & 0x200 else lod_bias) / 32.0,
            "exp_adjust": (fc[3] >> 13) & 0x3F}


def fetch(fc, kind):
    """render::WaterTextureFetch: the fields of a 2D fetch constant of the kind's format ("k8": the shore mask,
    "dxn": the normal map). ValueError where the C++ answers anything but kNone: no texture, not 2D, another
    format, no base, a pitch below the width, a mask above 256 x 256 or a normal map above 1,024 x 1,024; and
    (ledger ruling 33, after the review of Tasks 1 to 5, I1: the C++ side is the next builder's) a constant that
    does not deliver its components as unsigned fractions: a sign field other than 0 (1 signed, 2 unsigned biased,
    3 gamma) on a component the shader is given (x for the mask, x and y for the normal map), the integer number
    format (dword 3 bit 0) or an exponent adjust other than 0. That is what makes a mask's sample a number in
    [0, 1], which the dead draw's proof needs (water_check.py `dead`)."""
    if kind not in FETCH_LIMIT:
        raise ValueError("the kind is k8 or dxn, not %r" % (kind,))
    f = describe(fc)
    if f["dimension"] != 1:
        raise ValueError("not a 2D texture")
    if f["format"] != (XENOS_K8 if kind == "k8" else XENOS_DXN):
        raise ValueError("format %d is not the %s's" % (f["format"], "mask" if kind == "k8" else "normal map"))
    if f["base"] == 0 or f["pitch"] < f["width"]:
        raise ValueError("no base, or a pitch below the width")
    if f["width"] > FETCH_LIMIT[kind] or f["height"] > FETCH_LIMIT[kind]:
        raise ValueError("%d x %d is above the %s's limit of %d" % (f["width"], f["height"], kind, FETCH_LIMIT[kind]))
    delivered = 1 if kind == "k8" else 2
    if any(f["sign"][:delivered]):
        raise ValueError("the sign fields %s do not deliver an unsigned %s" % (f["sign"], "mask" if kind == "k8" else "normal map"))
    if _words(fc)[3] & 1:
        raise ValueError("the number format is integer, not fraction")
    if f["exp_adjust"]:
        raise ValueError("an exponent adjust of %d" % (f["exp_adjust"] - 64 if f["exp_adjust"] & 0x20 else f["exp_adjust"]))
    return f


def _file(capture_dir, name):
    return Path(capture_dir, *str(name).split("/")).read_bytes()


def mask(capture_dir, texture_row):
    """A dumped shore mask (a "texture" row of format 2) as uint8 [y, x]. ValueError for another texture or a dump
    that does not hold the level."""
    f = fetch(texture_row["fc"], "k8")
    return decode_k8(_file(capture_dir, texture_row["file"]), f["width"], f["height"], f["pitch"], f["tiled"])


def normal(capture_dir, texture_row, level, rounded=True):
    """Level `level` of the dumped normal map (a "texture" row of format 49) as uint8 [y, x, 2] (float32, the
    palette entries unrounded, with rounded=False): 0 from the base
    level's file, a higher one from the row's "mips" (a capture made since water discovery: frame-map section 20).
    ValueError for another texture, a level the row does not name (mean_levels stands in for the stored levels
    of a capture without them) or a dump that does not hold it."""
    f = fetch(texture_row["fc"], "dxn")
    if level == 0:
        return decode_dxn_rg8(_file(capture_dir, texture_row["file"]), f["width"], f["height"], f["pitch"] // 4, f["tiled"], f["endian"],
                              rounded=rounded)
    mips = texture_row.get("mips") or {}
    row = next((lv for lv in mips.get("levels") or [] if lv.get("level") == level), None)
    if row is None:
        raise ValueError("the capture holds no stored level %d of the normal map" % level)
    data = _file(capture_dir, mips["file"])[int(row["offset"]):]
    return decode_dxn_rg8(data, row["w"], row["h"], int(row["pitch"]) // 16, bool(mips.get("tiled")), f["endian"], origin=(row["x"], row["y"]),
                          rounded=rounded)


def mean_levels(base):
    """The 2 x 2 mean pyramid of a decoded base, uint8 [y, x] or [y, x, c]: each level's texel the mean of the
    four texels above it (of two where a side is already 1), down to 1 x 1. The sums are carried whole and each
    level is rounded once, to nearest with a half up, so that eight levels do not round eight times (rounding
    every level by itself lifts the 1 x 1 level of the lake's normal map from 128 to 129). It stands in for the
    stored levels of a capture that has none (plan Task 5; `levels` compares the stored ones with it)."""
    out = []
    total = np.asarray(base).astype(np.int64)
    count = np.ones(total.shape[:2], dtype=np.int64)
    while total.shape[0] > 1 or total.shape[1] > 1:
        h, w = total.shape[0], total.shape[1]
        ys = (np.arange(max(1, h // 2)) * 2, np.minimum(np.arange(max(1, h // 2)) * 2 + 1, h - 1))
        xs = (np.arange(max(1, w // 2)) * 2, np.minimum(np.arange(max(1, w // 2)) * 2 + 1, w - 1))
        total = sum(total[y][:, x] for y in ys for x in xs)
        count = sum(count[y][:, x] for y in ys for x in xs)
        c = count if total.ndim == 2 else count[..., None]
        out.append(((2 * total + c) // (2 * c)).astype(np.uint8))
    return out


def read_rows(path):
    rows = []
    with open(path, encoding="utf-8") as fh:
        lines = [l for l in fh if l.strip()]
    for i, line in enumerate(lines):
        try:
            rows.append(json.loads(line))
        except ValueError:
            if i != len(lines) - 1:
                raise
    return rows


def water_rows(rows):
    def shader(r):
        return str(r.get("vs_hash") or (r.get("vs") or {}).get("hash") or "").upper()
    return [r for r in rows if r.get("kind") == "draw" and shader(r).endswith(WATER_VS)]


def _texture_rows(rows, fmt):
    """{(base, the six dwords): the first "texture" row of that constant} for 2D textures of format `fmt`."""
    out = {}
    for r in rows:
        if r.get("kind") != "texture" or len(r.get("fc") or []) != 6:
            continue
        fc = _words(r["fc"])
        if (fc[0] & 3) == 2 and (fc[1] & 0x3F) == fmt:
            out.setdefault(tuple(fc), r)
    return out


def water_textures(rows):
    """(mask texture rows in the capture's order, the normal map's texture row or None): the textures the water
    rows' slots 1 and 2 name."""
    water = water_rows(rows)
    want1 = {tuple(_words(r["tf"]["1"])) for r in water if (r.get("tf") or {}).get("1")}
    want2 = {tuple(_words(r["tf"]["2"])) for r in water if (r.get("tf") or {}).get("2")}
    masks = [row for fc, row in _texture_rows(rows, XENOS_K8).items() if fc in want1]
    normals = [row for fc, row in _texture_rows(rows, XENOS_DXN).items() if fc in want2]
    return masks, (normals[0] if normals else None)


def fetches_cli(capture, out=None):
    out = out or sys.stdout
    rows = read_rows(capture)
    water = water_rows(rows)
    print("%s: %d water rows" % (Path(capture).name, len(water)), file=out)
    seen = {}
    for r in water:
        for slot in ("1", "2", "13", "14", "16"):
            fc = (r.get("tf") or {}).get(slot)
            if fc:
                seen[(int(slot), tuple(_words(fc)))] = seen.get((int(slot), tuple(_words(fc))), 0) + 1
    for (slot, fc), count in sorted(seen.items()):
        try:
            f = describe(fc)
        except ValueError as e:
            print("slot %2d  %3d rows  %s  %s" % (slot, count, " ".join("%08X" % v for v in fc), e), file=out)
            continue
        print("slot %2d  %3d rows  %08X  format %2d  dim %d  %d x %d  pitch %d  %s  endian %d  swizzle %s  clamp %s  "
              "filter mag %s min %s mip %s  aniso %d  levels %d (mip %d..%d at %08X%s)  sign %s  lod_bias %g  exp %d  | %s" % (
                  slot, count, f["base"], f["format"], f["dimension"], f["width"], f["height"], f["pitch"],
                  "tiled" if f["tiled"] else "linear", f["endian"], f["swizzle"], f["clamp"], f["filter"][0], f["filter"][1],
                  f["filter"][2], f["aniso"], f["levels"], f["mip_min"], f["mip_max"], f["mip"], ", packed" if f["packed"] else "",
                  f["sign"], f["lod_bias"], f["exp_adjust"], " ".join("%08X" % v for v in fc)), file=out)
    return 0 if water else 1


def _c141(rows):
    """c141.xy of the water rows ("pfull": 32 hex digits a register), or None when no row has it; ValueError when
    the rows disagree."""
    found = set()
    for r in water_rows(rows):
        text = (r.get("pfull") or "")[141 * 32:141 * 32 + 16]
        if len(text) == 16 and "-" not in text:
            found.add(struct.unpack(">ff", bytes.fromhex(text)))
    if len(found) > 1:
        raise ValueError("the water rows hold %d different c141.xy" % len(found))
    return found.pop() if found else None


def census_cli(capture, out=None):
    out = out or sys.stdout
    rows = read_rows(capture)
    masks, _ = water_textures(rows)
    c141 = _c141(rows)
    if c141 is None:
        print("no water row carries c141: nothing to make the alpha with", file=out)
        return 1
    print("c141 = (%g, %g)" % c141, file=out)
    base = Path(capture).parent
    for r in masks:
        f = fetch(r["fc"], "k8")
        a = mask(base, r).astype(np.float64) / 255.0
        # in double precision from the row's single-precision c141, as the research's script took its literals
        alpha = 1 - np.clip(float("%g" % c141[0]) - (1 - a) * float("%g" % c141[1]), 0, 1)
        print("%s %2dx%-2d pitch %3d tiled %d | m mean %.3f min %.3f max %.3f, share m==0 %.2f, m==1 %.2f | alpha mean %.3f max %.4f, "
              "share alpha<=0.0588 %.2f, share alpha>=0.99 %.2f, alpha==1 %.2f" % (
                  r["file"].split("/")[-1][:8], f["width"], f["height"], f["pitch"], f["tiled"], a.mean(), a.min(), a.max(),
                  (a == 0).mean(), (a == 1).mean(), alpha.mean(), alpha.max(), (alpha <= 0.0588).mean(), (alpha >= 0.99).mean(),
                  (alpha >= 1).mean()), file=out)
    return 0 if masks else 1


def sheet_cli(capture, out_path=None):
    rows = read_rows(capture)
    masks, normal_row = water_textures(rows)
    base = Path(capture).parent
    tiles = []
    for r in masks:
        m = mask(base, r)
        scale = max(1, 64 // max(m.shape))
        tiles.append(np.repeat(np.repeat(np.stack([m, m, m], axis=-1), scale, axis=0), scale, axis=1))
    cell, gap = 64, 4
    columns = max(1, len(tiles))
    width = max(columns * (cell + gap) + gap, 3 * (256 + gap) + gap)
    height = gap + cell + gap + (256 + gap if normal_row is not None else 0)
    sheet = np.full((height, width, 3), 48, dtype=np.uint8)
    for i, t in enumerate(tiles):
        x = gap + i * (cell + gap)
        sheet[gap:gap + t.shape[0], x:x + t.shape[1]] = t
    if normal_row is not None:
        n = normal(base, normal_row, 0)
        y = gap + cell + gap
        zero = np.zeros(n.shape[:2], dtype=np.uint8)
        for i, picture in enumerate((np.stack([n[..., 0]] * 3, axis=-1), np.stack([n[..., 1]] * 3, axis=-1),
                                     np.stack([n[..., 0], n[..., 1], zero + 255], axis=-1))):
            h, w = min(256, picture.shape[0]), min(256, picture.shape[1])
            x = gap + i * (256 + gap)
            sheet[y:y + h, x:x + w] = picture[:h, :w]
    out_path = Path(out_path) if out_path else Path(capture).with_suffix(".water_textures.png")
    ip.write_png(out_path, sheet)
    print("%s: %d masks%s -> %s" % (Path(capture).name, len(tiles), "" if normal_row is None else ", the normal map (x, y, both)", out_path))
    return 0 if tiles or normal_row is not None else 1


def levels_cli(capture, out=None):
    out = out or sys.stdout
    rows = read_rows(capture)
    _, normal_row = water_textures(rows)
    if normal_row is None:
        print("no normal map among the water rows' textures", file=out)
        return 1
    base = Path(capture).parent
    stored = (normal_row.get("mips") or {}).get("levels") or []
    if not stored:
        print("the capture holds no stored levels of the normal map (made before water discovery): the 2 x 2 mean stands in, unchecked", file=out)
        return 1
    mean = mean_levels(normal(base, normal_row, 0))
    for lv in stored:
        n = int(lv["level"])
        got = normal(base, normal_row, n).astype(np.int32)
        want = mean[n - 1].astype(np.int32)
        d = np.abs(got - want)
        print("level %d (%d x %d): against the 2 x 2 mean, largest difference x %d y %d, mean x %.3f y %.3f" % (
            n, got.shape[1], got.shape[0], d[..., 0].max(), d[..., 1].max(), d[..., 0].mean(), d[..., 1].mean()), file=out)
    return 0


def compare(bin_path, fcs):
    fc = [int(v, 16) for v in str(fcs).split(",")]
    data = Path(bin_path).read_bytes()
    if (fc[1] & 0x3F) == XENOS_K8:
        f = fetch(fc, "k8")
        cpp, mine = Path(str(bin_path) + ".cpp.r8"), decode_k8(data, f["width"], f["height"], f["pitch"], f["tiled"]).tobytes()
        names = ("FABLE2_K8_BIN", "FABLE2_K8_FC")
    else:
        f = fetch(fc, "dxn")
        cpp = Path(str(bin_path) + ".cpp.rg8")
        mine = decode_dxn_rg8(data, f["width"], f["height"], f["pitch"] // 4, f["tiled"], f["endian"]).tobytes()
        names = ("FABLE2_WDXN_BIN", "FABLE2_WDXN_FC")
    if not cpp.exists():
        print("no %s: run tests/native/test_texture_decode.cpp's executable with %s and %s set" % ((cpp,) + names))
        return 2
    theirs = cpp.read_bytes()
    if mine == theirs:
        print("%s: %d x %d, equal byte for byte (%d bytes)" % (Path(bin_path).name, f["width"], f["height"], len(mine)))
        return 0
    different = sum(1 for a, b in zip(mine, theirs) if a != b) + abs(len(mine) - len(theirs))
    print("%s: %d bytes differ (%d here, %d from the C++ decoder)" % (Path(bin_path).name, different, len(mine), len(theirs)))
    return 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("fetches", "census", "levels"):
        sub.add_parser(name).add_argument("capture")
    s = sub.add_parser("sheet")
    s.add_argument("capture")
    s.add_argument("--out")
    c = sub.add_parser("compare")
    c.add_argument("bin")
    c.add_argument("fc")
    a = ap.parse_args(argv)
    if a.cmd == "fetches":
        return fetches_cli(a.capture)
    if a.cmd == "census":
        return census_cli(a.capture)
    if a.cmd == "levels":
        return levels_cli(a.capture)
    if a.cmd == "sheet":
        return sheet_cli(a.capture, a.out)
    return compare(a.bin, a.fc)


if __name__ == "__main__":
    raise SystemExit(main())
