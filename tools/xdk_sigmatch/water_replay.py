"""The offline replays of the lake's surface (the water sub-project, plan Task 6; spec Design 3, criteria 4 and 5): the
game's own formula (light_formulas.water_vertex, water_pixel; held to the microcode by water_check.py) run over a
capture's A17D rows with the capture's own dumps, against the game's own pictures. Unlike water_check.py `rows`
this is NOT circular: the other side is the game's scene colour (0x199E0000), the game's depth copy and the
game's reflection pictures, which no formula of ours made. It is what holds the things `rows` cannot: the byte
order of the textures, the picture's orientation and channel order, the moment the dumps were taken.

  scene <capture> <frame> [--flip] [--swap-rb] [--no-glint] [--levels mean|stored] [--normal exact|rounded] [--place X Y]
        criterion 4. The frame's A17D rows through water_vertex, rasterised (foliage_replay.fragments) at the
        sampling place of the game's depth copy (fitted on the capture's own unmoving draws by foliage_replay, or
        given), depth-tested against that copy with the rows' own depth function (greater, written), the alpha
        test of the rows' state, shaded by water_pixel with the row's registers, literals and bools and with: the
        mask (bilinear, clamped); the normal map at the level the fetch takes (`level`), linear between two levels;
        the picture of the row's own slot 13 through its fetch constant's swizzle and addressing; slot 14's dump;
        the frame's fog tables. Printed for the pixels with alpha of 0.99 or more ("judged"; the bed under the
        water is in no capture and is at most 1% there), whole and by slot-13 base: replay over the game's scene,
        mean red, green and blue; the correlation of the two luminances; the verdict against 0.93 to 1.07 a
        channel, a correlation of at least 0.7 and at least 5,000 judged pixels. --flip reads the picture upside
        down (v for 1 - v), --swap-rb exchanges its red and blue: the two controls, each of which must miss a
        bound, or the measure does not discriminate and nothing is built on it (`decide`).
  variants <capture>... [--frame 1]   the four variants of `scene` on each capture and the verdict's rule (the plan's,
        stated before the numbers): the orientation and channel order are those of the ONE variant that meets
        both bounds on the pixels of the deciding base (0x13501000) in every capture while each other misses one.
        Beside it, reported and not part of the rule: the same with the normal map decoded to 8 bits (ledger
        ruling 32: how much the rounding moves the lake's own pixels) and without the glint.
  sky <capture> [--frame 1]           criterion 5: light_formulas.sky along the rays of diag(c31.x, c31.y, 1, 1) * M *
        Mirror(h) for each texel centre of a 256 x 256 picture (rows 0, 1 and 3, as SkyRayBasis forms them), at
        exposure 1, saturated to 8 bits, zero where the ray points below the level; against each dumped slot-13
        picture on the game's sky texels (below the horizon row, not darker than 0.8 of the picture's sky, at
        least three texels from any darker texel): the mean ratio a channel (0.95 to 1.05), with red and blue as
        the fetch constant reads them and exchanged, and the first row with content against the derived horizon
        (within one row) for the picture as stored and upside down.
  gap <capture> <scene.f2ns> [--frame 1]   the gap before (the research's b_gap.py): native over game on the
        placeholder's footprint (the native depth at a mesh's height) and on the replay's kept pixels
  lookup <capture> [--frame 1]        where each kept pixel's slot-13 lookup lands, and the share whose texel and
        every texel within 8 of it is sky in the game's picture, by base (criterion 8's set)
  glint <capture> [--frame 1]         the kept pixels whose sun term is above 5% of the pixel (criterion 12, W4)
  edge <capture> [--frame 1]          the kept pixels within 20 units of the eye, those with an alpha below 0.99, the
        nearest distance (criterion 11's sets, W3)
  view <capture>... [--frame 1] [--view K]... [--png file] [--water]   plan Task 15: the reflection views. For each
        view of the frame (a run of rows at surface pitch 280 outside the scene): a census of its rows by shader
        pair (count, what is drawn, state words, texture slots), then the view replayed into a 256 x 256 picture as
        the native pass would draw it: the mirrored sky (`sky`), and over it every patch of the view's far-terrain
        rows (7DDA) by the native grid (terrain_replay: 17 x 17 points a patch at the heightmap's height, under the
        row's own c0..c3), only the triangles whose upper side faces the view's eye (the rows' cull word), cut at
        the water's plane, 2 x 2 sampling places a texel, shaded by terrain_replay.lit_far_colour with the frame's
        light block and light_formulas.fog with the frame's fog block from the view's eye, output scale 1; then, for
        a view the game blurs, light_formulas.water_blur7 with the blur row's own offsets (`view_blur`). ASSUMED,
        because no capture holds a view's viewport, scissor or clip registers: the viewport is the whole 256 x 256,
        a texel's square is centred on its integral window position, the clip is the plane z = h. The picture checks
        each: the silhouette's agreement is printed with the square moved by -1, -0.5, 0 and +0.5 texels, and a
        replay without the cull or the cut is `replay_view(cull=False)` / `plane=None`. Against the game's picture
        of the view's base (the k-th smallest slot-13 base): the silhouette texels (a channel 8 / 255 or more from
        the sky's, 3 texels inside the drawn part) of each within one texel of the other's (criterion 10's measure),
        the share on the same side within 8 texels of the game's shore line, the colour on the texels that are
        silhouette in both, and what the replay has on top at the game's silhouette texels. The object rows (ECD6 /
        8D90) are rasterised for the cover alone where the capture holds their streams (it holds none for a view's
        rows: they are then among "nothing"); the export rows (6954 / A4A9) write the position (0, 0, 0, 0) and
        draw no pixel; the flares are not replayed. Where the scratch base 0x13601000 holds the view's sharp picture
        (the frame's last blurred view), the sharp replay against it and the blur on the game's own pair. --water:
        the replayed pictures through `scene` in the game's pictures' place: the game's own, the mirrored sky alone,
        sky and far terrain sharp, sky and far terrain blurred as the game blurs.
  targets <capture> <scene.f2ns> [--frame 1]   criterion 10's part of phase 13b, on the stored scene's reflection
        targets (flag 16; plan Task 11): each target against the capture's picture of the same base: criterion 5's two
        measures (the colour on the game's sky texels within 0.95 to 1.05 a channel; the first row with content
        within one of the derived horizon's); and that the targets of one frame are equal texel for texel (in this
        phase each holds the mirrored sky alone, which does not depend on the water's level). A REAL-GPU CHECK: the
        colour is held to the game's own pictures, which the software adapter's read-back leaves empty. Where a
        picture of the game is empty or not read the command says so, compares what needs none (the height, the
        first row against the derived horizon, the targets' equality) and ends "NOT DECIDED on this pair" with exit
        1; it does not read "not met" for that.
  cover <capture> <scene.f2ns> [--frame 1]     criterion 7: the native water pixels (those whose last depth is nearer
        than the stored plane of the depth before the water step, flag 8) against the replay's kept pixels on the
        game's depth copy: each side's share farther than one pixel from the other (bound 2% each); and the bed: on
        the native water pixels, the depth before the water step within 1% of the game's depth copy, as distances
        from the eye (bound 80%).
  colour <capture> <scene.f2ns> [<off capture> <off scene.f2ns>] [--frame 1]   criterion 8: on the replay's judged
        pixels whose lookup samples the sky (`lookup`'s set), the stored native scene over the game's scene a
        channel (0.92 to 1.08) and the hue's difference (6 degrees), with at least 5,000 such pixels or "not
        decidable"; the same of the off-run's pair on the same pixels (the distance before); and, reported and not
        bound, the far-shore pixels (the judged pixels that are not sky-sampling), the whole footprint and its
        luminance percentiles.
  main <capture> <scene.f2ns> [--frame 1] [--levels mean|stored] [--place X Y]   THE SURFACE PROGRAM'S MAIN BY VALUE
        (ledger ruling 44, after the review of Tasks 4 to 15, I2: nothing else holds the main's fetches, lookup, fog
        and scale; as amended by ruling 46, after the review of Tasks 11 and 12, I2 and I3). The replay fed THE PASS'S
        OWN INPUTS against the stored scene's water plane (flag 32: the linear target right after the water step):
        the stored scene's reflection targets in the game's pictures' place (`replay(pictures=)`), zero in slot 14's,
        the stored exposure as the output scale, and THE DEPTH TEST AND THE JUDGED SET FROM THE STORED SCENE'S OWN
        DEPTH BEFORE THE WATER STEP (flag 8), sampled at the native pixel centre. NOTHING OF THE GAME'S READ-BACK IS
        READ (no depth copy, no scene, no picture of the game): the command runs on the software adapter's pair as
        on a real GPU's. Per pixel, on the judged pixels (kept, alpha 0.99 or more) MORE THAN 3 PIXELS FROM THE JUDGED
        SET'S EDGE. The bound, stated before any run: the ratio of the means, native over replay, within 0.99 to 1.01
        a channel; the median of the per-pixel luminance ratio within 0.99 to 1.01; and its 5th to 95th percentile
        span no wider than the replay-against-game's own, 0.95 to 1.08. THE NUMBER FORMAT: the plane is R11G11B10
        float (6 bits of mantissa in red and green: steps of 1.6%; 5 in blue: 3.1%), and an exact replay against a
        target that truncates reads about 0.985 in blue with a right program. So the replay, held to the program's
        own clamp (31.875), is brought to the format with BOTH roundings (to nearest, toward zero); the three bounds
        are printed for each and for the replay as it is, AND THE BOUND IS APPLIED TO THE ROUNDING THAT LIES NEARER
        THE STORED PLANE (the smaller mean absolute difference on the judged interior), which the verdict line
        names. Which rounding either adapter applies is not established: the line is how it is learnt. In phase 13b
        the targets hold the mirrored sky alone; in phase 13c sky, terrain and blur: the command is the same. What it
        cannot see: the bed under the surface (at most 1% of a judged pixel, and 0.3% in open water: the replay
        blends over black), the blend's own second rounding (the pass blends over a bed already in the format), and
        the normal map's stored levels where the capture has none (`--levels mean` stands in for the GPU's own filter
        over the stored ones; a capture of this binary has them: `--levels stored`).
Each prints its inputs' names and exits 1 when a bound it states is missed or its inputs are not there.

The level a fetch takes (`level`): the SDK's translator (thirdparty/rexglue-sdk/src/graphics/pipeline/shader/
dxbc_translator_fetch.cpp, lines 1401 to 1528, and the samples at 1836 and 1866) gives a tfetch2D with computed
LOD to the host as sample_d with the coordinate's two screen derivatives (deriv_rtx_coarse, deriv_rty_coarse), each
multiplied by 2 ^ bias, where the bias is the fetch constant's LOD bias (dword 4 bits 12 to 21, signed, / 32) plus
the instruction's; the gradient exponent adjust is not applied (its FIXME). The host then takes, with anisotropy
off, log2 of the longer of the two derivatives in texels, held to the sampler's mip range (the constant's mip_min
and mip_max, and the levels the texture has), linear between the two levels about it when the mip filter is
linear. So: level = clamp(log2(max(|d(uv)/dx| in texels, |d(uv)/dy| in texels)) + bias). The derivative here is
the forward difference of the coordinate on the water's plane a pixel to the right and a pixel down (the host's is
one difference a 2 x 2 quad: the same number on a plane up to the perspective's curvature).

The normal map's levels: until a capture holds the stored ones (W1), the 2 x 2 mean of the base (`--levels mean`).
Its values: the BC5 palette entries as they are (`--normal exact`, the default: the emulator samples host BC5,
ledger ruling 32) or rounded to 8 bits (`--normal rounded`: render::DecodeDxnLevelRg8, the tests' reference).
"""
import argparse
import math
import struct
import sys
from collections import namedtuple
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capture_replay as cr  # noqa: E402
import impostor_check as ic  # noqa: E402
import light_formulas as lf  # noqa: E402
import water_check as wc  # noqa: E402
import water_textures as wt  # noqa: E402

SIZE = (1120, 720)
JUDGED_ALPHA = 0.99             # the bed is at most 1% of such a pixel
JUDGED_MIN = 5000               # criterion 4: fewer judged pixels read "not decidable"
BAND = (0.93, 1.07)             # criterion 4: replay over game, the mean of each channel
CORRELATION_MIN = 0.7           # criterion 4: of the two luminances over the judged pixels
SKY_BAND = (0.95, 1.05)         # criterion 5
SKY_DARK = 0.8                  # a sky texel is not darker than this of the picture's sky
SKY_MARGIN = 3                  # ... and this many texels from any darker texel
LOOKUP_REACH = 8                # criterion 8: every texel within this of the lookup is sky
MAIN_BAND = (0.99, 1.01)        # `main` (ledger ruling 44): native over replay, the ratio of the means a channel and the median
MAIN_SPAN = (0.95, 1.08)        # ... and the 5th to 95th percentile of the per-pixel luminance ratio (the replay-against-game's own)
MAIN_EDGE = 3                   # ... on judged pixels more than this many pixels from the judged set's edge
COVER_FAR = 0.02                # criterion 7: each side's share farther than one pixel from the other
BED_SHARE = 0.80                # criterion 7: the bed within 1% of the game's depth copy on this share of the water pixels
COLOUR_BAND = (0.92, 1.08)      # criterion 8: native over game a channel, on the sky-sampling pixels
HUE_BOUND = 6.0                 # criterion 8: degrees
GLINT_SHARE = 0.05              # criterion 12
GLINT_MIN = 500                 # a view counts with this many
NEAR_UNITS = 20.0               # criterion 11
DECIDING_BASE = 0x13501000      # one resolve a frame (spec criterion 4); 0x13401000 is open question 4's
LUMA = (0.2125, 0.7154, 0.0721)
VARIANTS = (("as the constant reads it", False, False), ("flip", True, False), ("swap-rb", False, True), ("flip and swap-rb", True, True))
# `main` (ledger ruling 46): the pass's own inputs and the stored plane's number format.
TARGET_MANTISSA = (6, 6, 5)     # the linear target is R11G11B10 float (clay_pass.cpp kLinearFormat; scene_dump.h DecodeR11G11B10)
ROUNDINGS = ("nearest", "toward zero")      # the two ways an adapter may bring a float to that format; which one is not established
PASS_MAX = 31.875               # the surface program's own clamp of what it writes (fable2_native_shaders.h, kClayWaterPs)
PASS_PLACE = (0.5, 0.5)         # where the native rasteriser samples a pixel: D3D's pixel centre
DEPTH_LEVELS = 16777215.0       # scene_dump.h: a stored depth is one of 16777216 levels, 0 the near plane


def unsigned_float(bits, mantissa_bits):
    """render::DecodeUnsignedFloat: the value of an unsigned float of a 5-bit exponent over `mantissa_bits` of mantissa
    (exponent 0 a denormal, 2^-14 * m / 2^bits; 31 an infinity or no number; else 2^(e - 15) * (1 + m / 2^bits))."""
    mantissa, exponent = bits & ((1 << mantissa_bits) - 1), (bits >> mantissa_bits) & 31
    fraction = mantissa / float(1 << mantissa_bits)
    if exponent == 0:
        return math.ldexp(fraction, -14)
    if exponent == 31:
        return math.nan if mantissa else math.inf
    return math.ldexp(1.0 + fraction, exponent - 15)


def r11g11b10(word):
    """render::DecodeR11G11B10: (red, green, blue) of a texel's word of the linear target."""
    return (unsigned_float(word & 0x7FF, 6), unsigned_float((word >> 11) & 0x7FF, 6), unsigned_float(word >> 22, 5))


def quantise_channel(values, mantissa_bits, rounding):
    """`values` (an array) as an unsigned float of `mantissa_bits` holds them: each brought to a value the format has,
    the nearest (a half to the even mantissa) or the next toward zero. Between 2^e and 2^(e + 1) the format's step is
    2^(e - mantissa_bits), and below 2^-14 it is the denormals' 2^(-14 - mantissa_bits). A value that is not above 0
    or is no number gives 0 (the format has no sign); one above the largest finite value gives that value (the
    surface's program writes nothing above PASS_MAX, so the format's infinity is never asked for)."""
    if rounding not in ROUNDINGS:
        raise ValueError("the rounding is one of %s, not %r" % (ROUNDINGS, rounding))
    v = np.asarray(values, dtype=np.float64)
    top = 2.0 ** 15 * (2.0 - 2.0 ** -mantissa_bits)
    out = np.zeros(v.shape)
    pos = v > 0.0                                                # (false for a value that is no number)
    x = np.minimum(v[pos], top)
    exponent = np.maximum(np.frexp(x)[1] - 1, -14)               # x = f * 2^E with f in [0.5, 1): the binade is E - 1
    step = np.ldexp(1.0, exponent - mantissa_bits)
    steps = x / step                                             # exact: a division by a power of two
    out[pos] = np.minimum((np.rint(steps) if rounding == "nearest" else np.floor(steps)) * step, top)
    return out


def quantise(rgb, rounding):
    """A colour array [..., 3] as the linear target holds it (TARGET_MANTISSA a channel): quantise_channel of each."""
    a = np.asarray(rgb, dtype=np.float64)
    return np.stack([quantise_channel(a[..., k], TARGET_MANTISSA[k], rounding) for k in range(3)], axis=-1)


def no_near(u, v):
    """Slot 14 as the native surface has it: zero (spec ruling 21; kClayWaterPs gives WaterMix float3(0, 0, 0))."""
    return (0.0, 0.0, 0.0)


def pass_depth(levels):
    """A stored native depth plane (levels: 0 the near plane, 16777215 the far plane and the cleared depth, z / w
    under the frame's view-projection) as the depth `replay` tests against: the game's reversed form, 1 - z / w, so
    that the rows' own "greater" is the pass's "less" on the plane as stored."""
    return 1.0 - np.asarray(levels, dtype=np.float64) / DEPTH_LEVELS


def reference_of(word):
    """The alpha reference of a state word 0x210E."""
    return struct.unpack(">f", struct.pack(">I", int(word)))[0]


def level(du_dx, dv_dx, du_dy, dv_dy, fetch):
    """The level a tfetch2D with computed LOD takes (module text): log2 of the longer of the coordinate's two screen
    derivatives in texels, plus the fetch constant's LOD bias, held to the constant's mip range and to the levels
    there are. `fetch`: the constant's fields (water_textures.describe). A derivative that is no number, or a
    texture of one level, gives the lowest level of the range."""
    low = float(fetch.get("mip_min", 0))
    high = float(min(fetch.get("mip_max", 0), fetch.get("levels", 1) - 1))
    if high <= low:
        return low
    w, h = fetch["width"], fetch["height"]
    step = max(math.hypot(du_dx * w, dv_dx * h), math.hypot(du_dy * w, dv_dy * h))
    if not math.isfinite(step) or step <= 0.0:
        return low
    return min(max(math.log2(step) + float(fetch.get("lod_bias", 0.0)), low), high)


def sample_levels(levels, u, v, at, clamp=(0, 0)):
    """A texture's levels (each [h, w, c] floats, the base first) at (u, v) and a level `at`: bilinear in the two
    levels about it and linear between them; `at` is held to the levels there are."""
    at = min(max(float(at), 0.0), float(len(levels) - 1))
    low = int(math.floor(at))
    part = at - low
    out = wc.bilinear(levels[low], u, v, clamp)
    if part > 0.0 and low + 1 < len(levels):
        other = wc.bilinear(levels[low + 1], u, v, clamp)
        out = [a * (1.0 - part) + b * part for a, b in zip(out, other)]
    return out


def mean_levels(base):
    """The 2 x 2 mean pyramid of a float image [h, w, c], the base first, down to 1 x 1 (nothing rounded)."""
    out = [np.asarray(base, dtype=np.float64)]
    while out[-1].shape[0] > 1 or out[-1].shape[1] > 1:
        a = out[-1]
        h, w = a.shape[0], a.shape[1]
        ys = (np.arange(max(1, h // 2)) * 2, np.minimum(np.arange(max(1, h // 2)) * 2 + 1, h - 1))
        xs = (np.arange(max(1, w // 2)) * 2, np.minimum(np.arange(max(1, w // 2)) * 2 + 1, w - 1))
        out.append(sum(a[y][:, x] for y in ys for x in xs) / 4.0)
    return out


def normal_levels(textures, fc, source="mean", decode="exact"):
    """(the normal map's levels as floats in 0..1 [h, w, 2], the base first; the constant's fields; a note).
    `decode`: "exact" the BC5 palette entries as they are, "rounded" to 8 bits. `source`: "mean" the 2 x 2 mean of
    the base, "stored" the capture's own levels (cr.Unreadable when the capture has none)."""
    row = textures.rows.get(fc)
    if row is None:
        raise cr.Unreadable("no texture row of the normal map's fetch constant")
    f = textures.fields(fc)
    if f["format"] != wt.XENOS_DXN:
        raise cr.Unreadable("slot 2 is format %d, not k_DXN" % f["format"])
    rounded = decode == "rounded"
    try:
        base = wt.normal(textures.dir, row, 0, rounded=rounded).astype(np.float64) / 255.0
        if source == "stored":
            stored = (row.get("mips") or {}).get("levels") or []
            if not stored:
                raise cr.Unreadable("the capture holds no stored levels of the normal map (made before water discovery)")
            levels = [base] + [wt.normal(textures.dir, row, int(lv["level"]), rounded=rounded).astype(np.float64) / 255.0
                               for lv in sorted(stored, key=lambda lv: lv["level"])]
            return levels, f, "the capture's stored levels (%d)" % len(levels)
    except ValueError as e:
        raise cr.Unreadable(str(e)) from None
    levels = mean_levels(base)
    return levels, f, "levels made by a 2 x 2 mean of the base (%d)" % len(levels)


# colour [h, w, 3]: the replay's scene colour, the water over a bed of 0 at its alpha (0 where nothing is kept);
# alpha, kept (drawn by the state's alpha test and the depth test), base (the slot-13 base a kept pixel samples),
# lookup [h, w, 2] (slot 13's coordinate), glint (the sun term's share of the pixel before fog), distance (from the
# eye), fate counts {"killed", "skipped", "shaded"}; rows replayed; fragments the depth copy hid; the mean level of
# the two normal fetches; problems (texts); notes (texts).
Replay = namedtuple("Replay", "colour alpha kept base lookup glint distance fates rows hidden levels problems notes")


def _plane_xy(M, h, px, py, size):
    """The world (x, y) of the points of the plane z = h that the window positions (px, py) show (arrays)."""
    nx = px / size[0] * 2.0 - 1.0
    ny = 1.0 - py / size[1] * 2.0
    a = M[0][None, :] - nx[:, None] * M[3][None, :]
    b = M[1][None, :] - ny[:, None] * M[3][None, :]
    # a . (x, y, h, 1) = 0 and b . (x, y, h, 1) = 0
    ra = -(a[:, 2] * h + a[:, 3])
    rb = -(b[:, 2] * h + b[:, 3])
    det = a[:, 0] * b[:, 1] - a[:, 1] * b[:, 0]
    with np.errstate(divide="ignore", invalid="ignore"):
        x = (ra * b[:, 1] - a[:, 1] * rb) / det
        y = (a[:, 0] * rb - ra * b[:, 0]) / det
    return x, y


def replay(path, frame, flip=False, swap_rb=False, glint=True, levels="mean", decode="exact", place=(0.0, 0.0), depth=None,
           zscale=-1.0, zoffset=1.0, dumps=wc.DUMPS, pictures=None, near=None, out_scale=None):
    """The frame's A17D rows as the game's formula draws them (module text, `scene`): Replay. `depth`: the game's
    depth copy [h, w] as stored (None: no depth test); `place`: the sampling place, offsets from the integral
    position. `pictures`: {slot-13 base: a picture [h, w, 3] of floats in the shader's red, green, blue} that takes
    the dump's place for the rows of that base, under the same addressing (plan Task 15: the replayed views).
    THE PASS'S OWN INPUTS (`main`, ledger ruling 46): `near`: f(u, v) -> rgb in slot 14's place (no_near: the native
    surface's zero); with it and `pictures` the capture's own dumps of slots 13 and 14 are NOT READ (the software
    adapter's read-back leaves them empty, and a capture may hold none), and a row whose base has no picture is
    not replayed, said. `out_scale`: the frame's output scale in the place of each row's c20.w (the pass writes
    its own: the game's in a post frame, the fixed exposure otherwise), so the colour is in the stored plane's unit."""
    import foliage_replay as fp
    capture = wc.read_capture(path, frame)
    textures = wc.RowTextures(capture)
    text = wc._text(dumps, wc.PS)
    rows = [r for r in capture["water"] if r.get("frame") == frame and wc.row_kind(r) == "surface"]
    height, width = SIZE[1], SIZE[0]
    colour = np.zeros((height, width, 3))
    alpha = np.zeros((height, width))
    kept = np.zeros((height, width), dtype=bool)
    base = np.zeros((height, width), dtype=np.int64)
    lookup = np.zeros((height, width, 2))
    shine = np.zeros((height, width))
    distance = np.full((height, width), np.inf)
    zbuf = None if depth is None else np.array(depth, dtype=np.float64)
    fates = {"killed": 0, "skipped": 0, "shaded": 0}
    problems, notes, hidden, done = [], [], 0, 0
    level_sum = level_count = 0.0
    own = pictures is not None and near is not None             # the pass's own pictures: the capture's are not read
    for row in rows:
        where = "line %s" % row.get("line")
        try:
            setup = wc.RowSetup(capture, textures, row, text, pictures=not own)
            maps, nf, note = normal_levels(textures, wc._fc(row, 2), levels, decode)
            plane = wc.row_height(textures.dir, row)
        except (cr.Unreadable, ValueError) as e:
            problems.append("%s: NOT REPLAYED: %s" % (where, e))
            continue
        if note not in notes:
            notes.append(note)
        if tuple(row.get("viewport") or SIZE) != SIZE:
            problems.append("%s: NOT REPLAYED: a viewport of %s" % (where, row.get("viewport")))
            continue
        rs = row.get("rs") or {}
        word = int(str(rs.get("2200", "0x0")), 16)
        if (word >> 4) & 7 != 4 or not word & 2:
            problems.append("%s: NOT REPLAYED: depth word 0x%08X is not tested with function 4 (greater)" % (where, word))
            continue
        control = int(str(rs.get("2202", "0x0")), 16)
        reference = reference_of(int(str(rs.get("210E", "0x0")), 16)) if control & 8 else -math.inf
        if control & 8 and control & 7 != 4:
            problems.append("%s: NOT REPLAYED: alpha function %d, not 4 (greater)" % (where, control & 7))
            continue
        slot13 = wc._fc(row, 13)[1] & 0xFFFFF000
        if own and slot13 not in pictures:
            problems.append("%s: NOT REPLAYED: no target of the row's slot-13 base %08X among the pictures given" % (where, slot13))
            continue
        done += 1
        consts = setup.consts
        if out_scale is not None:
            consts = dict(consts)
            consts[20] = list(consts[20][:3]) + [float(out_scale)]
        bools = set(setup.bools) if glint else set(setup.bools) - {136}
        M = np.array([setup.vbank[reg] for reg in (0, 1, 2, 3)], dtype=np.float64)
        clip = []
        for p, pair in zip(setup.positions, setup.pairs):
            v = lf.water_vertex(p, pair, setup.vbank)
            clip.append(list(v["pos"]) + list(v["place"]) + list(v["coord"]) + list(v["mask"]))
        picture = setup.mirror
        if pictures and slot13 in pictures:
            def picture(u, v, image=np.asarray(pictures[slot13], dtype=np.float64), clamp=textures.fields(wc._fc(row, 13))["clamp"]):
                return wc.bilinear(image, u, v, clamp)
        if flip or swap_rb:
            def picture(u, v, read=setup.mirror):
                t = read(u, 1.0 - v if flip else v)
                return [t[2], t[1], t[0]] if swap_rb else t
        c117, c118 = setup.vbank[117], setup.vbank[118]
        for _, ys, xs, zw, attrs in fp.fragments(np.array(clip, dtype=np.float64), np.array(setup.triangles, dtype=np.int64), SIZE, place):
            stored = fp.stored_depth(zw, zscale, zoffset)
            if zbuf is not None:
                front = stored > zbuf[ys, xs]
                hidden += int((~front).sum())
                if not front.any():
                    continue
                ys, xs, stored, attrs = ys[front], xs[front], stored[front], attrs[front]
            # the coordinates' screen derivatives on the water's plane (module text)
            if plane is not None:
                px, py = xs + place[0], ys + place[1]
                x0, y0 = _plane_xy(M, plane, px, py, SIZE)
                x1, y1 = _plane_xy(M, plane, px + 1.0, py, SIZE)
                x2, y2 = _plane_xy(M, plane, px, py + 1.0, SIZE)
                grads = np.stack([x1 - x0, y1 - y0, x2 - x0, y2 - y0], axis=1)
            else:
                grads = np.zeros((len(ys), 4))
            for k in range(len(ys)):
                y, x = int(ys[k]), int(xs[k])
                a = attrs[k]
                P, coord, pair = [wc.f32(v) for v in a[0:3]], [wc.f32(v) for v in a[3:7]], [wc.f32(v) for v in a[7:9]]
                m = setup.mask(pair[0], pair[1])[0]
                gx, gy, hx, hy = grads[k]
                la = level(gx * c117[0], gy * c117[1], hx * c117[0], hy * c117[1], nf)
                lb = level(gx * c118[0], gy * c118[1], hx * c118[0], hy * c118[1], nf)
                at = iter((la, lb))
                tex = {"normal": lambda u, v: sample_levels(maps, u, v, next(at), nf["clamp"]), 13: picture, 14: near or setup.near,
                       8: wc._no_shadow, "tf4": setup.tf4, "tf5": setup.tf5}
                try:
                    out = lf.water_pixel((x, y), P, coord[:2], coord[2:], m, consts, tex, bools=bools, lit=setup.lit)
                except cr.Unreadable as e:
                    problems.append("%s: NOT REPLAYED: %s" % (where, e))
                    break
                fates[out["fate"]] += 1
                if out["fate"] == "killed" or not out["alpha"] > reference:
                    continue
                al = out["alpha"]
                colour[y, x] = [c * al + old * (1.0 - al) for c, old in zip(out["colour"], colour[y, x])]
                alpha[y, x], kept[y, x], base[y, x] = al, True, slot13
                if zbuf is not None and word & 4:
                    zbuf[y, x] = stored[k]
                if out["fate"] == "shaded":
                    lookup[y, x] = out["lookup"]
                    shine[y, x] = out["glint"]
                    distance[y, x] = out["distance"]
                    level_sum += la + lb
                    level_count += 2
    return Replay(colour, alpha, kept, base, lookup, shine, distance, fates, done, hidden,
                  level_sum / level_count if level_count else 0.0, problems, notes)


def luminance(picture):
    return (np.asarray(picture, dtype=np.float64) * LUMA).sum(-1)


# judged pixels; replay over game, the mean of red, green and blue; the correlation of the two luminances; the
# verdict ("met", "not met", "not decidable"); the bounds missed (names); the game's and the replay's mean colour.
Measure = namedtuple("Measure", "judged ratio correlation verdict missed game replay")


def _measure(sel, replayed, game, judged_min):
    n = int(sel.sum())
    if n == 0:
        return Measure(0, (math.nan,) * 3, math.nan, "not decidable", ("no judged pixel",), (math.nan,) * 3, (math.nan,) * 3)
    r, g = replayed[sel], np.asarray(game, dtype=np.float64)[sel]
    rm, gm = r.mean(0), g.mean(0)
    with np.errstate(divide="ignore", invalid="ignore"):
        ratio = tuple(float(x) for x in rm / gm)
    lr, lg = luminance(r), luminance(g)
    correlation = float(np.corrcoef(lr, lg)[0, 1]) if n > 1 and lr.std() > 0.0 and lg.std() > 0.0 else math.nan
    missed = [name for name, x in zip(("red", "green", "blue"), ratio) if not BAND[0] <= x <= BAND[1]]
    if not correlation >= CORRELATION_MIN:
        missed.append("correlation")
    if n < judged_min:
        verdict = "not decidable"
    else:
        verdict = "not met" if missed else "met"
    return Measure(n, ratio, correlation, verdict, tuple(missed), tuple(float(x) for x in gm), tuple(float(x) for x in rm))


def measure(r, game, judged_min=JUDGED_MIN):
    """{None: the Measure over all judged pixels, base: the Measure of those that sample that slot-13 base}:
    criterion 4's measure of a Replay against the game's scene [h, w, 3]. Judged: kept with alpha of JUDGED_ALPHA or
    more."""
    judged = r.kept & (r.alpha >= JUDGED_ALPHA)
    out = {None: _measure(judged, r.colour, game, judged_min)}
    for b in sorted(int(x) for x in np.unique(r.base[r.kept])):
        out[b] = _measure(judged & (r.base == b), r.colour, game, judged_min)
    return out


# judged pixels at which two variants differ by more than PREFER_STEP of the game's luminance; those at which the
# game is nearer the first, and the second; the median of the variants' difference there, of the game's luminance.
Prefer = namedtuple("Prefer", "differ first second gap")
PREFER_STEP = 0.03              # two variants differ at a pixel when their luminances lie this far apart, of the game's
PREFER_MIN = 500                # fewer differing pixels cannot tell
PREFER_SHARE = 0.75             # the game must be nearer one variant at this share of them


def prefer(a, b, game, base=None):
    """The paired measure of two Replays of one frame against the game's scene: only the judged pixels (of slot-13
    base `base`, if given) at which the two differ are asked, and at each which of the two the game's luminance is
    nearer to. It cannot be carried by anything the two variants share (the glint, the fog, the base colour), which
    the bounds of criterion 4 can be."""
    sel = a.kept & b.kept & (a.alpha >= JUDGED_ALPHA) & (b.alpha >= JUDGED_ALPHA)
    if base is not None:
        sel &= a.base == base
    la, lb, lg = luminance(a.colour)[sel], luminance(b.colour)[sel], luminance(np.asarray(game, dtype=np.float64))[sel]
    with np.errstate(divide="ignore", invalid="ignore"):
        apart = np.abs(la - lb) / np.maximum(lg, 1e-9)
    differ = apart > PREFER_STEP
    da, db = np.abs(la - lg)[differ], np.abs(lb - lg)[differ]
    return Prefer(int(differ.sum()), int((da < db).sum()), int((db < da).sum()), float(np.median(apart[differ])) if differ.any() else 0.0)


def prefer_verdict(p, least=PREFER_MIN):
    """"the first", "the second" or "cannot tell" (fewer than `least` differing pixels, or the game nearer
    neither at PREFER_SHARE of them)."""
    if p.differ < least:
        return "cannot tell"
    if p.first >= PREFER_SHARE * p.differ:
        return "the first"
    if p.second >= PREFER_SHARE * p.differ:
        return "the second"
    return "cannot tell"


def decide(verdicts):
    """The verdict's rule over the four variants (module text): `verdicts` is {variant: [its verdict on the deciding
    base in each capture]}. Returns (the variant, "decided") or (None, why not)."""
    if any(v == "not decidable" for vs in verdicts.values() for v in vs):
        return None, "not decidable: a capture has too few judged pixels of the deciding base"
    whole = [name for name, vs in verdicts.items() if vs and all(v == "met" for v in vs)]
    if len(whole) == 1:
        return whole[0], "decided"
    if not whole:
        return None, "the formula does not close: no variant meets both bounds in every capture"
    return None, "the measure does not discriminate: %s each meet both bounds in every capture" % " and ".join(whole)


def edge_sets(r):
    """{"kept", "near" (kept within NEAR_UNITS of the eye), "fading" (kept with an alpha below JUDGED_ALPHA),
    "nearest" (the smallest distance of a kept pixel; infinity without one)}."""
    d = r.distance[r.kept]
    return {"kept": int(r.kept.sum()), "near": int((d < NEAR_UNITS).sum()), "fading": int((r.kept & (r.alpha < JUDGED_ALPHA)).sum()),
            "nearest": float(d.min()) if len(d) else math.inf}


def glint_set(r):
    """{"kept", "above" (kept pixels whose sun term is above GLINT_SHARE of the pixel), "largest"}."""
    g = r.glint[r.kept]
    return {"kept": int(r.kept.sum()), "above": int((g > GLINT_SHARE).sum()), "largest": float(g.max()) if len(g) else 0.0}


# --- the mirrored sky (criterion 5) --------------------------------------------------------------------------------

def view_rows(M, c31xy, h):
    return wc.water_view_rows(M, c31xy, h)


def horizon_row(view, size=256):
    """The picture row (a number; texel j spans j to j + 1) at which the ray of the picture's middle column is
    level: above it (smaller rows) the mirrored view shows what lies below the water plane, and nothing is drawn."""
    T = np.asarray(view, dtype=np.float64)
    a, b0, b1 = T[0, :3], T[1, :3], T[3, :3]
    # the ray at (0, Y) is a x (b0 - Y b1); its third component is linear in Y
    z0, z1 = np.cross(a, b0)[2], np.cross(a, b1)[2]
    Y = z0 / z1
    return (1.0 - Y) / 2.0 * size


def view_rays(view, size=256):
    """The view rays [size, size, 3] of the texel centres of a size x size picture under a view-projection: from
    rows 0, 1 and 3 (SkyRayBasis), pointing into the view."""
    T = np.asarray(view, dtype=np.float64)
    c = (np.arange(size) + 0.5) / size * 2.0 - 1.0
    X, Y = np.meshgrid(c, -c)
    a = T[0, :3][None, None, :] - X[..., None] * T[3, :3][None, None, :]
    b = T[1, :3][None, None, :] - Y[..., None] * T[3, :3][None, None, :]
    d = np.cross(a, b)
    sign = np.sign((d * T[3, :3]).sum(-1))
    return d * sign[..., None]


def sky_picture(view, block, tf4, tf13=None, tf14=None, size=256):
    """The mirrored sky as the native target holds it (spec Design 7): light_formulas.sky along the view's rays at
    exposure 1, saturated and rounded to 8 bits, zero where the ray's third component is below 0. uint8
    [size, size, 3]."""
    rays = view_rays(view, size)
    out = np.zeros((size, size, 3), dtype=np.uint8)
    for j in range(size):
        for i in range(size):
            n = rays[j, i]
            if not n[2] >= 0.0:
                continue
            rgb = lf.sky(n, block, tf4, tf13, tf14)
            out[j, i] = [int(min(max(v, 0.0), 1.0) * 255.0 + 0.5) for v in rgb]
    return out


def _grow(mask, steps):
    out = mask.copy()
    for _ in range(steps):
        g = out.copy()
        g[1:] |= out[:-1]
        g[:-1] |= out[1:]
        g[:, 1:] |= out[:, :-1]
        g[:, :-1] |= out[:, 1:]
        g[1:, 1:] |= out[:-1, :-1]
        g[:-1, :-1] |= out[1:, 1:]
        g[1:, :-1] |= out[:-1, 1:]
        g[:-1, 1:] |= out[1:, :-1]
        out = g
    return out


def picture_sky(picture):
    """The colour of a reflection picture's sky: the median of its lowest eighth of rows (floats, [h, w, 3])."""
    h = picture.shape[0]
    return np.median(np.asarray(picture, dtype=np.float64)[-max(1, h // 8):].reshape(-1, 3), axis=0)


def sky_texels(picture, horizon, margin=SKY_MARGIN, dark=SKY_DARK):
    """The game's sky texels of a reflection picture [h, w, 3] (floats): below the horizon row, not darker than `dark`
    of the picture's sky, and at least `margin` texels from any darker texel (the rows above the horizon are darker:
    nothing is drawn there)."""
    lum = luminance(picture)
    low = lum < dark * float(luminance(picture_sky(picture)))
    rows = np.arange(picture.shape[0])[:, None] + 0.5
    low |= np.broadcast_to(rows < horizon, low.shape)
    return ~_grow(low, margin)


def first_content_row(picture, floor=0.01):
    """The median over the columns of the first row at which a column has content (a channel sum above `floor`)."""
    has = np.asarray(picture, dtype=np.float64).sum(-1) > floor
    first = np.where(has.any(axis=0), has.argmax(axis=0), picture.shape[0])
    return float(np.median(first))


# --- the reflection views (plan Task 15) ---------------------------------------------------------------------------
# What a view's rows are, by pixel shader: the far terrain; the tessellation factors' memory export (695413A9831D88DA
# writes the clip position (0, 0, 0, 0) for every vertex: no pixel); the objects; the sky's cube; the flares.
VIEW_FAMILIES = {"7DDAD5C29B3F56EE": "terrain", "A4A965C189287B99": "export", "8D900846800943C8": "object",
                 "F7601FBE485936FD": "sky", "D72FD0903CCE7DD6": "flare"}
VIEW_KINDS = ("nothing", "terrain", "export", "object", "sky", "flare", "other")
VIEW_SIZE = 256
VIEW_SAMPLES = 2                # sampling places a texel edge (the game's views have four samples a texel)
VIEW_SHIFT = -0.5               # a texel's square is centred on the integral window position (`view` prints the others)
SILHOUETTE = 8.0 / 255.0        # a texel is silhouette when a channel lies this far from the sky's there
EDGE_BAND = 8                   # texels about the game's shore line on which the side is judged
BLUR_REACH = 3                  # texels a blurred view's draw reaches beyond its own

# kind [h, w] (index into VIEW_KINDS; 0 where nothing is drawn), ident [h, w] (the draw's place in the list + 1),
# depth [h, w] (z / w; infinity where nothing), world [h, w, 3] (the place's interpolated attributes; NaN without).
Top = namedtuple("Top", "kind ident depth world")


def view_top(draws, size, plane=None, origin=0.0):
    """What a view has on top at every sampling place: `draws` is [(clip [n, 4] or [n, 7] with the world place of
    each vertex, tris [m, 3], kind)] in any order; the nearest z / w wins (the views' depth function), a later draw
    at an equal depth. `plane`: fragments whose world height is below it are not drawn (the views' clip at the water;
    draws without a world place are not cut)."""
    import foliage_replay as fp
    width, height = size
    depth = np.full((height, width), np.inf)
    kind = np.zeros((height, width), dtype=np.int8)
    ident = np.zeros((height, width), dtype=np.int32)
    world = np.full((height, width, 3), np.nan)
    for n, (clip, tris, name) in enumerate(draws):
        if name not in VIEW_KINDS[1:]:
            raise ValueError("not a view's kind: %r" % (name,))
        clip = np.asarray(clip, dtype=np.float64)
        for _, ys, xs, d, attrs in fp.fragments(clip, tris, size, origin):
            keep = d <= depth[ys, xs]
            if plane is not None and attrs.shape[1] >= 3:
                keep &= attrs[:, 2] >= plane
            if not keep.any():
                continue
            ys, xs = ys[keep], xs[keep]
            depth[ys, xs] = d[keep]
            kind[ys, xs] = VIEW_KINDS.index(name)
            ident[ys, xs] = n + 1
            world[ys, xs] = attrs[keep][:, :3] if attrs.shape[1] >= 3 else np.nan
    return Top(kind, ident, depth, world)


def view_shares(selected, top):
    """{kind: share} of the selected sampling places by what `top` has there ("nothing" where no draw); {} when none
    is selected."""
    n = int(selected.sum())
    if n == 0:
        return {}
    return {name: float((top.kind[selected] == k).sum()) / n for k, name in enumerate(VIEW_KINDS)}


def _taps(picture, u, v):
    """A picture [h, w, c] at normalised (u, v) arrays: linear between texel centres, clamped to the edge."""
    h, w = picture.shape[:2]
    x, y = u * w - 0.5, v * h - 0.5
    x0, y0 = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
    fx, fy = (x - x0)[..., None], (y - y0)[..., None]
    xa, xb, ya, yb = np.clip(x0, 0, w - 1), np.clip(x0 + 1, 0, w - 1), np.clip(y0, 0, h - 1), np.clip(y0 + 1, 0, h - 1)
    return (picture[ya, xa] * (1 - fx) + picture[ya, xb] * fx) * (1 - fy) + (picture[yb, xa] * (1 - fx) + picture[yb, xb] * fx) * fy


def view_blur(picture, offsets, weight, shift=0.0):
    """light_formulas.water_blur7 over a whole picture [h, w, 3]: pixel (i, j) reads at ((i + shift) / w, (j + shift)
    / h) plus each offset. shift 0: the pixel's integral position, which is what the game's blur has (the captured
    sharp and blurred pictures: frame map section 20); 0.5 would be the texel's centre."""
    picture = np.asarray(picture, dtype=np.float64)
    if len(offsets) != 7:
        raise ValueError("the blur has 7 taps, got %d" % len(offsets))
    h, w = picture.shape[:2]
    u, v = np.meshgrid((np.arange(w) + shift) / w, (np.arange(h) + shift) / h)
    out = np.zeros_like(picture)
    for k in (1, 0, 2, 3, 4, 5, 6):
        out += _taps(picture, u + offsets[k][0], v + offsets[k][1])
    return out * weight


def eight_bits(picture):
    return np.rint(np.clip(np.asarray(picture, dtype=np.float64), 0.0, 1.0) * 255.0) / 255.0


def near_share(a, b, reach=1):
    """The share of a's texels that lie within `reach` texels of a texel of b (criterion 10's measure); NaN when a
    has none."""
    n = int(a.sum())
    if n == 0:
        return math.nan
    return float((a & _grow(b, reach)).sum()) / n


def _shrink(mask, steps):
    return ~_grow(~mask, steps)
# A view of a frame: its place in the frame (1 for the first), its rows, its c0..c3, the eye its rows' c19 holds (the
# main eye mirrored), the F186 row that follows it (None: the game does not blur this view).
View = namedtuple("View", "index rows matrix eye blur")


def _hash(row, stage="ps"):
    text = cr.ps_hash(row) if stage == "ps" else cr.vs_hash(row)
    text = text or ""
    return text[2:].upper() if text.lower().startswith("0x") else text.upper()


def family_of(row):
    return VIEW_FAMILIES.get(_hash(row), "other")


def read_views(path, frame):
    """The reflection views of a frame, in their order: a view is a run of draw, indx, tess and verts rows at surface
    pitch 280 outside the scene (water_check.capture_views); a verts row of the blur's pixel shader right after the
    run is the view's blur."""
    found, current = [], None
    for r in cr.rows(path, kinds=("draw", "tess", "indx", "verts", "resolve"), frame=frame):
        if r["kind"] == "resolve":
            current = None
            continue
        pitch = int(str(r.get("surface_info", "0x0")), 16) & 0x3FFF
        if pitch == wc.VIEW_PITCH and not r.get("in_scene"):
            if current is None:
                current = {"rows": [], "blur": None}
                found.append(current)
            current["rows"].append(r)
            continue
        if current is not None and r["kind"] == "verts" and _hash(r) == lf.WATER_BLUR_SHADER["ps"]:
            current["blur"] = r
        current = None
    out = []
    for k, v in enumerate(found):
        matrix = eye = None
        for r in v["rows"]:
            c = r.get("consts") or {}
            if family_of(r) == "terrain" and all("c%d" % i in c for i in range(4)):
                matrix = np.array([c["c%d" % i] for i in range(4)], dtype=np.float64)
            elif family_of(r) == "sky" and r.get("bank") and matrix is None:
                matrix = np.array(r["bank"][0:16], dtype=np.float64).reshape(4, 4)
            else:
                continue
            reg = ic.pixel_register(r, 19)
            eye = [float(x) for x in reg[:3]] if reg is not None else eye
        out.append(View(k + 1, v["rows"], matrix, eye, v["blur"]))
    return out


def blur_taps(row):
    """(the seven offsets, the weight) of a blur row: the xy of its c116 to c122 and its literal c255.x."""
    offsets = [tuple(ic.pixel_register(row, reg)[:2]) for reg in lf.WATER_BLUR_SHADER["offsets"]]
    literals = ic.row_literals(row, "ps")
    reg, comp = lf.WATER_BLUR_SHADER["weight"]
    if reg not in literals:
        raise cr.Unreadable("the blur row has no literal register c%d" % reg)
    return offsets, float(literals[reg][comp])


def census(view):
    """[(kind of row, vertex shader, pixel shader, family, rows, what is drawn, the state words, texture slots)] of a
    view's rows, in first-appearance order."""
    groups = {}
    for r in view.rows:
        key = (r["kind"], _hash(r, "vs")[:4], _hash(r)[:4], family_of(r))
        g = groups.setdefault(key, {"rows": 0, "prim": set(), "count": 0, "state": set(), "slots": set(), "textures": set()})
        g["rows"] += 1
        args = [int(str(a), 16) for a in r.get("args") or []]
        if r["kind"] == "tess":
            g["prim"].add("patches")
            g["count"] += args[3] if len(args) > 3 else 0
        elif r["kind"] == "indx":
            g["prim"].add("prim %s" % r.get("prim"))
            g["count"] += int(r.get("indices") or 0)
        else:
            g["prim"].add("prim %d" % args[0] if args else "?")
            g["count"] += int((r.get("ib") or {}).get("count") or (args[2] if len(args) > 2 else 0))
        rs = r.get("rs") or {}
        g["state"].add(tuple(rs.get(k) for k in ("2200", "2201", "2202", "2205", "2104")))
        for slot, fc in (r.get("tf") or {}).items():
            g["slots"].add(int(slot))
            g["textures"].add(fc[1])
    return [key + (g["rows"], "%d %s" % (g["count"], "/".join(sorted(g["prim"]))), sorted(g["state"]), sorted(g["slots"]), len(g["textures"]))
            for key, g in groups.items()]


_GRID_TRIS = {}


def _grid_tris(n):
    if n not in _GRID_TRIS:
        tris = []
        for j in range(n):
            for i in range(n):
                a = j * (n + 1) + i
                tris += [(a, a + 1, a + n + 2), (a, a + n + 2, a + n + 1)]      # BuildGridIndices: cut from a to c
        _GRID_TRIS[n] = np.array(tris, dtype=np.int64)
    return _GRID_TRIS[n]


def facing(points, tris, eye):
    """Which triangles [m, 3] over world points [n, 3] show the eye their upper side: (b - a) x (c - a) . (eye - a)
    > 0 for a triangle that is counter-clockwise seen from above. A view's rows cull the back with the winding
    turned (0x2205 is 0x00218006 where the main scene's rows have 0x00218002: the mirror turns the handedness), so
    what a view draws of the ground is its upper side as the mirrored eye sees it, and ground seen from below is
    not drawn."""
    p = np.asarray(points, dtype=np.float64)[:, :3]
    a, b, c = p[tris[:, 0]], p[tris[:, 1]], p[tris[:, 2]]
    return (np.cross(b - a, c - a) * (np.asarray(eye, dtype=np.float64)[None, :] - a)).sum(1) > 0.0


def terrain_draws(view, textures, segments=16, cull=True):
    """([(clip with the world place [289, 7], tris, "terrain")], [(row, Far, SquareGrid)] in step, problems): every
    patch of every far-terrain row of a view as the native pass draws a far patch (terrain_replay: the square from
    the row's constants, 17 x 17 grid points at the heightmap's point-sampled height, two triangles a grid quad),
    under the row's own c0..c3. `segments`: 16 is the native pass's grid; 8, 4, 2 or 1 takes every second, fourth,
    eighth or only the corner points of it (what a patch the game tessellates less finely would show); a dict
    {cell size: segments} gives each patch size its own. `cull`: only the triangles `facing` the view's eye."""
    import terrain_replay as tr
    draws, shade, problems = [], [], []
    for row in view.rows:
        if family_of(row) != "terrain":
            continue
        c = row.get("consts") or {}
        try:
            T = np.array([c["c%d" % i] for i in range(4)], dtype=np.float64)
            height, normals = textures.get(row, 16, "heightmap"), textures.get(row, 17, "normal map")
            far = tr.Far(tr.far_inputs(c, row.get("pconst")), textures.get(row, 13, "far colour"), textures.get(row, 3, "base map"),
                         textures.get(row, 18, "far u table"), textures.get(row, 19, "far v table"), _hash(row) == tr.FADING_FAR[2:])
            far.lin = tr.light_inputs_of(row, "terrain-far")
            patches = int(str(row["args"][3]), 16)
            for p in range(patches):
                sq = tr.patch_square(c, p, far=True)
                grid = tr.SquareGrid(sq, row, height, normals, far=True)
                n = segments.get(round(sq[2]), 16) if isinstance(segments, dict) else segments
                step = grid.N // n
                if step * n != grid.N:
                    raise ValueError("%d segments do not divide the grid's %d" % (n, grid.N))
                pts = np.array([[sq[0] + sq[2] * i / n, sq[1] + sq[3] * j / n, grid.points[j * step * (grid.N + 1) + i * step][3], 1.0]
                                for j in range(n + 1) for i in range(n + 1)])
                tris = _grid_tris(n)
                if cull:
                    tris = tris[facing(pts, tris, view.eye)]
                draws.append((np.concatenate([pts @ T.T, pts[:, :3]], axis=1), tris, "terrain"))
                shade.append((row, far, grid))
        except (tr.MissingInput, KeyError, IndexError, ValueError) as e:
            problems.append("line %s: a far-terrain row NOT REPLAYED: %s" % (row.get("line"), e))
    return draws, shade, problems


def object_draws(view, capture_dir):
    """([(clip with the world place, tris, "object")], problems): the view's object rows by their table entry's
    position (the microcode's where the table has none); the world place is the view's c0..c3 solved for the clip
    position."""
    import foliage_replay as fp
    table = fp.load_table()
    draws, problems = [], []
    inverse = np.linalg.inv(view.matrix)
    for row in view.rows:
        if family_of(row) != "object":
            continue
        g = fp.row_geometry(row, capture_dir, wind=False, table=table)
        if g.reason:
            g = fp.row_geometry(row, capture_dir, wind=True)
        if g.reason or g.state != "whole":
            problems.append("line %s: an object row NOT REPLAYED: %s" % (row.get("line"), g.reason or g.note or g.state))
            continue
        world = g.clip @ inverse.T
        with np.errstate(divide="ignore", invalid="ignore"):
            world = world[:, :3] / world[:, 3:4]
        draws.append((np.concatenate([g.clip, world], axis=1), g.tris, "object"))
    return draws, problems


# colour [256 s, 256 s, 3] and cover [256 s, 256 s] of the far terrain at the sampling places (shift 0: a texel's square
# from its integral position to the next), `top`: view_top of terrain and objects together, the counts and problems.
ViewReplay = namedtuple("ViewReplay", "colour cover top patches objects shaded problems")


def replay_view(view, capture_dir, textures, block, fog, tables, plane, samples=VIEW_SAMPLES, objects=True, segments=16, shade=True,
                cull=True):
    """A view's far terrain as the native pass would draw it into a 256 x 256 target (module text, `view`)."""
    import terrain_replay as tr
    size = (VIEW_SIZE * samples, VIEW_SIZE * samples)
    terrain, shading, problems = terrain_draws(view, textures, segments, cull)
    top = view_top(terrain, size, plane, 0.5)
    colour = np.zeros((size[1], size[0], 3))
    tf4, tf5 = tables
    ys, xs = np.nonzero(top.kind == VIEW_KINDS.index("terrain"))
    for y, x in zip(ys.tolist(), xs.tolist()) if shade else ():
        row, far, grid = shading[int(top.ident[y, x]) - 1]
        wx, wy, wz = (float(v) for v in top.world[y, x])
        c = far.colour.sample(*tr.far_uv(far, wx, wy))
        normal, _ = grid.at(wx, wy)
        lit = tr.lit_far_colour(block, far.lin, [c[i] * c[i] for i in range(3)], far.base.sample(*tr.apply_uv(far.inp["base_uv"], wx, wy)), normal)
        fogged = lf.fog(lit, (wx, wy, wz), view.eye, fog["c28"], fog["c66"], fog["c67"], fog["c68"], tf4, tf5)
        scale = float(ic.pixel_register(row, 20)[3])                    # 1 in a view
        colour[y, x] = [v * scale for v in fogged]
    cover = top.kind == VIEW_KINDS.index("terrain")
    count = 0
    if objects:
        more, more_problems = object_draws(view, capture_dir)
        problems += more_problems
        count = len(more)
        if more:
            over = view_top(more, size, plane, 0.5)
            nearer = over.depth < top.depth
            top = Top(np.where(nearer, over.kind, top.kind), np.where(nearer, over.ident + len(terrain), top.ident),
                      np.minimum(over.depth, top.depth), np.where(nearer[..., None], over.world, top.world))
    return ViewReplay(colour, cover, top, len(terrain), count, len(ys), problems)


def texels(values, samples=VIEW_SAMPLES, shift=VIEW_SHIFT):
    """A sampling-place array [h s, w s(, c)] as texels: the mean of each texel's s x s places, the texel's square
    moved by `shift` texels along both axes (a multiple of 1 / s)."""
    step = int(round(shift * samples))
    if abs(step - shift * samples) > 1e-9:
        raise ValueError("the shift is not a multiple of a sampling place")
    v = np.roll(np.asarray(values, dtype=np.float64), (-step, -step), axis=(0, 1))
    h, w = v.shape[0] // samples, v.shape[1] // samples
    return v.reshape((h, samples, w, samples) + v.shape[2:]).mean(axis=(1, 3))


def view_picture(r, sky, shift=VIEW_SHIFT, samples=VIEW_SAMPLES):
    """The 256 x 256 picture of a ViewReplay over a sky picture [256, 256, 3] (floats): each texel the mean of its
    sampling places, a place the terrain's colour where the terrain covers it and the sky's texel elsewhere; 8 bits."""
    cover = texels(r.cover, samples, shift)
    colour = texels(r.colour * r.cover[..., None], samples, shift)
    return eight_bits(colour + np.asarray(sky, dtype=np.float64) * (1.0 - cover[..., None]))


# The measures of a replayed picture against the game's (frame map section 20, "The reflection views").
ViewMeasure = namedtuple("ViewMeasure", "game replay near_game near_replay side band_side band both ratio median")


def compare_view(game, picture, sky, sharp_sky):
    """`game`, `picture`: [256, 256, 3]; `sky`: the sky as the picture holds it (blurred where the picture is);
    `sharp_sky`: the sky before any blur (it says which texels lie below the water plane's line). A texel is
    silhouette when it lies at least 3 texels inside the drawn part and one of its channels is SILHOUETTE or more
    from the sky's there (the far shore stands in haze: a rule on the luminance alone loses it)."""
    drawn = _shrink(luminance(sharp_sky) > 0.0, 3)
    sil_g = drawn & (np.abs(game - sky).max(-1) >= SILHOUETTE)
    sil_r = drawn & (np.abs(picture - sky).max(-1) >= SILHOUETTE)
    band = drawn & _grow(sil_g, EDGE_BAND) & ~_shrink(sil_g | ~drawn, EDGE_BAND)
    both = _shrink(sil_g & sil_r, 1)
    if both.any():
        ratio = tuple(float(x) for x in picture[both].mean(0) / game[both].mean(0))
        with np.errstate(divide="ignore", invalid="ignore"):
            median = float(np.median(luminance(picture[both]) / luminance(game[both])))
    else:
        ratio, median = (math.nan,) * 3, math.nan
    return ViewMeasure(int(sil_g.sum()), int(sil_r.sum()), near_share(sil_g, sil_r), near_share(sil_r, sil_g),
                       float((sil_g == sil_r)[drawn].mean()) if drawn.any() else math.nan,
                       float((sil_g == sil_r)[band].mean()) if band.any() else math.nan, int(band.sum()), int(both.sum()), ratio, median), sil_g


def _say_view(say, name, m):
    say("    %-34s silhouette texels game %5d replay %5d; within one texel: game's of the replay's %.1f%%, the replay's of the "
        "game's %.1f%%; same side: all drawn texels %.2f%%, the %d within %d of the game's shore line %.1f%%" % (
            name, m.game, m.replay, 100 * m.near_game, 100 * m.near_replay, 100 * m.side, m.band, EDGE_BAND, 100 * m.band_side))
    say("    %-34s silhouette in both, one texel inside: %d texels; replay / game red %.3f green %.3f blue %.3f; the median "
        "luminance ratio a texel %.3f" % ("", m.both, m.ratio[0], m.ratio[1], m.ratio[2], m.median))


def _view_setup(path, frame):
    """What every view of a frame shares: the capture, the views, the water rows' bases with their heights, the
    pictures by base, the light block, the fog block, the tables, the sky's inputs."""
    import terrain_replay as tr
    cap = wc.read_capture(path, frame)
    frame_row = cap["frames"].get(frame) or {}
    block = tr.light_block_of(frame_row)
    fog = frame_row.get("fog") or {}
    if not fog.get("valid"):
        raise cr.Unreadable("the frame row has no valid fog block")
    tables = wc.row_tables(cap, frame)
    textures = wc.RowTextures(cap)
    bases = {}
    for r in cap["water"]:
        if r.get("frame") == frame and wc.row_kind(r) == "surface":
            fc = wc._fc(r, 13)
            bases.setdefault(fc[1] & 0xFFFFF000, (fc, wc.row_height(textures.dir, r)))
    return {"cap": cap, "views": read_views(path, frame), "bases": bases, "textures": textures, "block": block, "fog": fog,
            "tables": tables, "eye": frame_row["light"]["eye"], "terrain": tr._Textures(tr.read_capture(path, frame)[1]),
            "sky": _sky_inputs(path, frame)}


def _picture_of(textures, fc):
    image, f = textures.image(fc)
    return image[..., list(f["swizzle"][:3])]


def view_pictures(path, frame, say=print, png=None, only=None):
    """Replays every view of a frame and compares each with the game's picture of its base. Returns {base: {"sky",
    "sharp", "blurred" (None where the game does not blur the view), "game"}} for `water`."""
    s = _view_setup(path, frame)
    views, bases = s["views"], sorted(s["bases"])
    say("%s frame %d: %d views at pitch %d; the water rows' slot-13 bases: %s" % (
        Path(path).name, frame, len(views), wc.VIEW_PITCH, ", ".join("%08X (h %.6g)" % (b, s["bases"][b][1]) for b in bases)))
    block_sky, tf4, tf13, tf14, note = s["sky"]
    sky = None
    out, sheets = {}, []
    scratch = [k for k in s["textures"].rows if (k[1] & 0xFFFFF000) == SCRATCH_BASE]
    for view in views:
        say("  view %d: %d rows; the eye in its rows (%.4f, %.4f, %.4f); %s" % (
            view.index, len(view.rows), view.eye[0], view.eye[1], view.eye[2],
            "followed by the blur (line %s)" % view.blur.get("line") if view.blur else "no blur row follows"))
        for kind, vs, ps, family, rows, drawn, state, slots, n_tex in census(view):
            say("    %-5s %s / %s %-8s rows %3d: %s; depth, blend, alpha, cull, colour mask %s; slots %s (%d textures)" % (
                kind, vs, ps, family, rows, drawn, "; ".join(" ".join(str(w) for w in st) for st in state), slots, n_tex))
        if only is not None and view.index not in only:
            continue
        if view.index > len(bases):
            say("    NOT COMPARED: no slot-13 base for this view")
            continue
        base = bases[view.index - 1]
        fc, height = s["bases"][base]
        plane = (float(s["eye"][2]) + view.eye[2]) / 2.0
        say("    its picture by the rule: %08X; the plane from the two eyes %.4f, the meshes' height %.6g" % (base, plane, height))
        if sky is None:
            sky = sky_picture(view.matrix, block_sky, tf4, tf13, tf14, VIEW_SIZE).astype(np.float64) / 255.0
        r = replay_view(view, s["textures"].dir, s["terrain"], s["block"], s["fog"], s["tables"], plane)
        for text in r.problems:
            say("    " + text)
        say("    replayed: %d patches of the far terrain, %d sampling places shaded (%d x %d a texel), %d object rows rasterised for "
            "the cover alone" % (r.patches, r.shaded, VIEW_SAMPLES, VIEW_SAMPLES, r.objects))
        game = _picture_of(s["textures"], fc)
        sharp = view_picture(r, sky)
        entry = {"sky": sky, "sharp": sharp, "blurred": None, "game": game}
        if view.blur is not None:
            offsets, weight = blur_taps(view.blur)
            entry["blurred"] = eight_bits(view_blur(sharp, offsets, weight))
            blurred_sky = view_blur(sky, offsets, weight)
            m, sil = compare_view(game, entry["blurred"], blurred_sky, sky)
            _say_view(say, "blurred, against %08X:" % base, m)
            _say_view(say, "control: NOT blurred against it:", compare_view(game, sharp, blurred_sky, sky)[0])
            _say_view(say, "control: the sky alone, blurred:", compare_view(game, eight_bits(blurred_sky), blurred_sky, sky)[0])
        else:
            m, sil = compare_view(game, sharp, sky, sky)
            _say_view(say, "sharp, against %08X:" % base, m)
            _say_view(say, "control: the sky alone:", compare_view(game, sky, sky, sky)[0])
        for shift in (-1.0, -0.5, 0.0, 0.5):
            other = view_picture(r, sky, shift)
            if view.blur is not None:
                other = eight_bits(view_blur(other, offsets, weight))
            mm = compare_view(game, other, blurred_sky if view.blur is not None else sky, sky)[0]
            say("      a texel's square moved by %+.1f: same side on the band %.1f%%, within one texel %.1f%% and %.1f%%%s" % (
                shift, 100 * mm.band_side, 100 * mm.near_game, 100 * mm.near_replay, "  (the one used)" if shift == VIEW_SHIFT else ""))
        # what the replay has on top at the game's silhouette texels, by sampling place; under a blur a place counts
        # for a draw that lies within the blur's reach of it (two texels and the taps' own half texel)
        places = np.roll(np.repeat(np.repeat(sil, VIEW_SAMPLES, 0), VIEW_SAMPLES, 1), int(round(VIEW_SHIFT * VIEW_SAMPLES)), axis=(0, 1))
        top = r.top
        if view.blur is not None:
            spread = np.zeros_like(top.kind)
            for k in range(len(VIEW_KINDS) - 1, 0, -1):
                spread[_grow(top.kind == k, BLUR_REACH * VIEW_SAMPLES)] = k
            top = Top(spread, top.ident, top.depth, top.world)
        shares = view_shares(places, top)
        say("    the game's %d silhouette texels by what the replay has on top%s (by sampling place): %s; the export rows (A4A9) "
            "write the position (0, 0, 0, 0): none; the %d object rows have no vertex streams in the capture and are NOT "
            "rasterised: their texels are among \"nothing\"" % (
                int(sil.sum()), " or within %d texels (the blur's reach)" % BLUR_REACH if view.blur is not None else "",
                ", ".join("%s %.1f%%" % (k, 100 * v) for k, v in shares.items() if v > 0.0) or "-", sum(1 for x in view.rows if family_of(x) == "object") - r.objects))
        if scratch and view.blur is not None:
            # the scratch base holds the sharp picture of the frame's LAST blurred view (as the previous frame left it)
            last = [v for v in views if v.blur is not None][-1]
            if last.index == view.index:
                held = _picture_of(s["textures"], scratch[0])
                _say_view(say, "sharp, against the scratch %08X:" % SCRATCH_BASE, compare_view(held, sharp, sky, sky)[0])
                d = np.abs(eight_bits(view_blur(held, offsets, weight)) - game)[8:-8, 8:-8] * 255.0
                say("    the blur on the game's own pictures: blur7(%08X) against %08X: mean |difference| %.3f of 255, within 1 of 255 "
                    "at %.2f%% of the texels (taps at the texel's centre instead: %.3f)" % (
                        SCRATCH_BASE, base, float(d.mean()), 100 * float((d.max(-1) <= 1.0 + 1e-9).mean()),
                        float((np.abs(eight_bits(view_blur(held, offsets, weight, 0.5)) - game)[8:-8, 8:-8] * 255.0).mean())))
        out[base] = entry
        shown = entry["blurred"] if entry["blurred"] is not None else sharp
        sheets.append(np.concatenate([game, shown, np.abs(game - shown) * 4.0], axis=1))
    if png and sheets:
        import impostor_pictures as ip
        sheet = np.concatenate(sheets, axis=0)
        ip.write_png(Path(png), (np.sqrt(np.clip(sheet * 3.0, 0.0, 1.0)) * 255.0 + 0.5).astype(np.uint8))
        say("  sheet %s: a row a view: the game's picture, the replay, four times their difference (shown at 3 times, square root)" % png)
    return out


SCRATCH_BASE = 0x13601000       # where a view that is to be blurred is resolved first


def _percentiles(values):
    return [float(np.percentile(values, q)) for q in (5, 50, 95)]


def cmd_view(args, say=print):
    bad = 0
    for capture in args.capture:
        path = cr.find_capture(capture)
        pictures = view_pictures(path, args.frame, say, args.png, set(args.view) if args.view else None)
        if not pictures:
            bad += 1
            continue
        if not args.water:
            continue
        path, fr, game, place = _inputs(capture, args.frame, args.place if args.place else (-0.25, -0.25), say)
        if fr is None or game is None or fr.depth is None:
            say("  the water: NOT COMPARED (no scene or no depth copy)")
            bad += 1
            continue
        variants = (("the game's own pictures", None),
                    ("the mirrored sky alone", {b: p["sky"] for b, p in pictures.items()}),
                    ("sky and far terrain, sharp", {b: p["sharp"] for b, p in pictures.items()}),
                    ("sky and far terrain, blurred as the game", {b: p["blurred"] if p["blurred"] is not None else p["sharp"] for b, p in pictures.items()}))
        lum_game = luminance(game)
        for name, given in variants:
            r = replay(path, args.frame, place=place, depth=fr.depth, zscale=fr.zscale, zoffset=fr.zoffset, pictures=given)
            m = measure(r, game)
            _say_measure(say, name, r, m)
            judged = r.kept & (r.alpha >= JUDGED_ALPHA)
            if judged.any():
                a, b = _percentiles(luminance(r.colour)[judged]), _percentiles(lum_game[judged])
                err = np.abs(luminance(r.colour)[judged] - lum_game[judged]) / lum_game[judged]
                say("      luminance at the 5th, 50th, 95th percentile: replay %s, game %s, ratios %s; a pixel's |replay - game| / "
                    "game: mean %.3f, 90th percentile %.3f" % (" ".join("%.3f" % v for v in a), " ".join("%.3f" % v for v in b),
                                                               " ".join("%.3f" % (x / y) for x, y in zip(a, b)), float(err.mean()), float(np.percentile(err, 90))))
    return 1 if bad else 0
# --- the commands -------------------------------------------------------------------------------------------------

def _inputs(path, frame, place, say):
    """(capture path, the frame with the game's depth copy, the game's scene or None, the sampling place)."""
    import foliage_replay as fp
    import foliage_trace as ft
    path = cr.find_capture(str(path))
    try:
        fr = fp.load_frame(path, frame)
    except cr.Unreadable as e:
        say("%s frame %d: %s" % (path.name, frame, e))
        return path, None, None, None
    say("%s frame %d: the game's depth copy: %s; the copy stands at line %s; the frame's A17D rows at lines %s" % (
        path.name, frame, fr.depth_note, fr.copy_line,
        ", ".join(str(r["line"]) for r in wc.water_rows(path) if r.get("frame") == frame and wc.row_kind(r) == "surface") or "none"))
    try:
        game, c20w = ft.game_scene(path, frame)
        say("  the game's scene 0x199E0000 of the frame: read (c20.w %.9g)" % c20w)
    except cr.Unreadable as e:
        game = None
        say("  the game's scene: NOT READ: %s" % e)
    if place is not None:
        say("  the sampling place of the depth copy: GIVEN (%g, %g)" % tuple(place))
    elif fr.depth is None:
        place = (fp.SAMPLE_ORIGIN, fp.SAMPLE_ORIGIN)
        say("  the sampling place: (%g, %g), not fitted (no depth copy)" % place)
    else:
        items = fp.replay_rows(fr, fr.rows_before_copy(), fp.load_table(), cache=Path(cr.REPO) / "out" / "tests" / "water-replay-cache")
        place = fp.sample_place(fr, items, say)
    return path, fr, game, tuple(place)


def _base_name(base):
    return "all" if base is None else "0x%08X" % base


def _say_measure(say, name, r, m):
    say("  %-26s kept %6d (killed %d, skipped %d, hidden by the depth copy %d); the mean level of the normal fetches %.2f" % (
        name, int(r.kept.sum()), r.fates["killed"], r.fates["skipped"], r.hidden, r.levels))
    for base, x in m.items():
        say("    %-10s judged %6d: replay / game red %.3f green %.3f blue %.3f; correlation %.3f; game mean %s, replay mean %s: %s%s" % (
            _base_name(base), x.judged, x.ratio[0], x.ratio[1], x.ratio[2], x.correlation,
            " ".join("%.4f" % v for v in x.game), " ".join("%.4f" % v for v in x.replay), x.verdict,
            " (misses %s)" % ", ".join(x.missed) if x.missed and x.verdict != "met" else ""))


def cmd_scene(args, say=print):
    path, fr, game, place = _inputs(args.capture, args.frame, args.place, say)
    if fr is None:
        return 1
    r = replay(path, args.frame, args.flip, args.swap_rb, not args.no_glint, args.levels, args.normal, place, fr.depth, fr.zscale, fr.zoffset)
    for text in r.problems + r.notes:
        say("  " + text)
    say("  the normal map sampled %s" % ("as BC5's own palette entries" if args.normal == "exact" else "rounded to 8 bits"))
    if game is None or fr.depth is None:
        say("criterion 4: not decidable (the capture holds no %s)" % ("scene" if game is None else "depth copy"))
        return 1
    m = measure(r, game)
    name = ", ".join(n for n, on in (("flip", args.flip), ("swap-rb", args.swap_rb), ("no glint", args.no_glint)) if on) or "as the constant reads it"
    _say_measure(say, name, r, m)
    deciding = m.get(DECIDING_BASE)
    say("criterion 4 on the pixels of 0x%08X (bounds %.2f to %.2f a channel, correlation at least %.1f, at least %d judged): %s" % (
        DECIDING_BASE, BAND[0], BAND[1], CORRELATION_MIN, JUDGED_MIN, deciding.verdict if deciding else "no such pixel"))
    return 0 if deciding and deciding.verdict == "met" and not r.problems else 1


def cmd_variants(args, say=print):
    verdicts = {name: [] for name, _, _ in VARIANTS}
    whole = {name: [] for name, _, _ in VARIANTS}
    pairs = {name: [] for name, _, _ in VARIANTS[1:]}
    bad = False
    for capture in args.capture:
        path, fr, game, place = _inputs(capture, args.frame, args.place, say)
        if fr is None or game is None or fr.depth is None:
            say("  NOT COMPARED")
            bad = True
            continue
        own = None
        for name, flip, swap in VARIANTS:
            r = replay(path, args.frame, flip, swap, True, args.levels, "exact", place, fr.depth, fr.zscale, fr.zoffset)
            if own is None:
                own = r
                for text in r.problems + r.notes:
                    say("  " + text)
                bad = bad or bool(r.problems)
            m = measure(r, game)
            _say_measure(say, name, r, m)
            verdicts[name].append(m[DECIDING_BASE].verdict if DECIDING_BASE in m else "not decidable")
            whole[name].append(m[None].verdict)
            if r is not own:
                p = prefer(own, r, game)
                pairs[name].append(prefer_verdict(p))
                say("    the paired measure against the first variant: %d judged pixels at which the two differ by more than %.0f%% of "
                    "the game's luminance (median %.1f%%); the game is nearer the first at %d, nearer this one at %d: %s" % (
                        p.differ, 100 * PREFER_STEP, 100 * p.gap, p.first, p.second, prefer_verdict(p)))
        # reported, not part of the rule
        judged = own.kept & (own.alpha >= JUDGED_ALPHA)
        for name, kw in (("normal map rounded to 8 bits", {"decode": "rounded"}), ("without the glint", {"glint": False})):
            r = replay(path, args.frame, False, False, kw.get("glint", True), args.levels, kw.get("decode", "exact"), place, fr.depth, fr.zscale, fr.zoffset)
            _say_measure(say, name, r, measure(r, game))
            both = judged & r.kept
            if both.any():
                d = np.abs(r.colour[both] - own.colour[both]) / np.maximum(own.colour[both], 1e-6)
                say("    against the first variant on %d judged pixels: relative difference a pixel, the largest channel: median %.2e, "
                    "95th percentile %.2e, largest %.2e; pixels that differ by more than 1%%: %d" % (
                        int(both.sum()), float(np.median(d.max(1))), float(np.percentile(d.max(1), 95)), float(d.max()), int((d.max(1) > 0.01).sum())))
    choice, why = decide(verdicts)
    say("the rule: the orientation and channel order are those of the one variant that meets both bounds on the pixels of 0x%08X in "
        "every capture while each other misses one in at least one" % DECIDING_BASE)
    for name, vs in verdicts.items():
        say("  %-26s %s" % (name, ", ".join(vs) or "-"))
    say("verdict as the criterion is written: %s" % ("%s: %s" % (choice, why) if choice else why))
    choice2, why2 = decide(whole)
    say("the same rule on ALL judged pixels (a substituted measure: most of the lake samples 0x13401000, not the deciding base):")
    for name, vs in whole.items():
        say("  %-26s %s" % (name, ", ".join(vs) or "-"))
    say("verdict by the substituted measure: %s" % ("%s: %s" % (choice2, why2) if choice2 else why2))
    say("the paired measure (only the pixels at which a variant differs from the first; needs %d of them and the game nearer one at "
        "%.0f%%):" % (PREFER_MIN, 100 * PREFER_SHARE))
    for name, vs in pairs.items():
        say("  the first against %-18s %s" % (name + ":", ", ".join(vs) or "-"))
    return 0 if choice and not bad else 1


def _sky_inputs(path, frame):
    """(the frame's sky block for light_formulas.sky, tf4, tf13 or None, tf14 or None, a note)."""
    capture = wc.read_capture(path, frame)
    fog = (capture["frames"].get(frame) or {}).get("fog") or {}
    sky = fog.get("sky") or {}
    tables = wc.row_tables(capture, frame)
    if tables is None or not sky.get("valid"):
        raise cr.Unreadable("the frame row has no valid fog and sky block")
    block = {"c31x": sky["c31x"], "c46x": sky["c46x"], "c47x": sky["c47x"], "c64": fog["c64"], "c65": fog["c65"], "c66": fog["c66"],
             "c67y": fog["c67"][1]}
    textures = wc.RowTextures(capture)
    out, notes = [], []
    for slot in ("tf13", "tf14"):
        fc = tuple(int(str(v), 16) for v in sky.get(slot) or ())
        try:
            if fc not in textures.rows:         # the texture row of the same texture under another sampler word (dword 0)
                same = [k for k in textures.rows if k[1:] == fc[1:] and (k[0] >> 31) == (fc[0] >> 31) and ((k[0] >> 22) & 0x1FF) == ((fc[0] >> 22) & 0x1FF)]
                if same:
                    textures.rows[fc] = textures.rows[same[0]]
            image, f = textures.image(fc)
            if image.shape[2] != 4:
                raise cr.Unreadable("not four channels")
            swz = f["swizzle"]
            out.append(lambda u, v, image=image, f=f, swz=swz: wc._shader(wc.bilinear(image, u, v, f["clamp"]), swz, 4))
            notes.append("%s %d x %d, swizzle %s, clamp %s" % (slot, f["width"], f["height"], swz, f["clamp"]))
        except (cr.Unreadable, ValueError) as e:
            out.append(None)
            notes.append("%s NOT READ (%s): the clear-sky term stands in" % (slot, e))
    if out[0] is None:
        out[1] = None
    return block, tables[0], out[0], out[1], "; ".join(notes)


def cmd_sky(args, say=print):
    bad = compared = 0
    for capture in args.capture:
        path = cr.find_capture(capture)
        cap = wc.read_capture(path, args.frame)
        rows = [r for r in cap["water"] if r.get("frame") == args.frame and wc.row_kind(r) == "surface"]
        if not rows:
            say("%s frame %d: no water row" % (path.name, args.frame))
            continue
        try:
            block, tf4, tf13, tf14, note = _sky_inputs(path, args.frame)
        except cr.Unreadable as e:
            say("%s frame %d: NOT COMPARED: %s" % (path.name, args.frame, e))
            bad += 1
            continue
        say("%s frame %d: the sky's block of the frame row (c31.x %g, c46.x %g, c47.x %g); %s" % (
            path.name, args.frame, block["c31x"], block["c46x"], block["c47x"], note))
        textures = wc.RowTextures(cap)
        M = np.array(rows[0]["bank"][0:16], dtype=np.float64).reshape(4, 4)
        c31 = ic.pixel_register(rows[0], 31)
        seen, made = set(), {}
        for row in rows:
            fc = wc._fc(row, 13)
            if fc in seen:
                continue
            seen.add(fc)
            h = wc.row_height(textures.dir, row)
            try:
                image, f = textures.image(fc)
            except cr.Unreadable as e:
                say("  picture %08X: NOT READ: %s" % (fc[1] & 0xFFFFF000, e))
                bad += 1
                continue
            view = view_rows(M, c31[:2], h)
            horizon = horizon_row(view, f["height"])
            if "sky" not in made:               # the rays need rows 0, 1 and 3: one sky for every plane
                made["sky"] = sky_picture(view, block, tf4, tf13, tf14, f["height"]).astype(np.float64) / 255.0
            native = made["sky"]
            swz = f["swizzle"]
            game = image[..., list(swz[:3])]
            compared += 1
            name = str(textures.rows[fc]["file"]).split("/")[-1]
            first = first_content_row(game)
            flipped = first_content_row(game[::-1])
            say("  picture %08X (%s, h %.6g): the derived horizon at row %.2f; the first row with content: %.1f as stored, %.1f upside "
                "down; the replay's own %.1f" % (fc[1] & 0xFFFFF000, name, h, horizon, first, flipped, first_content_row(native)))
            sel = sky_texels(game, horizon)
            n = int(sel.sum())
            if n == 0:
                say("    no sky texel: NOT COMPARED")
                bad += 1
                continue
            ratios = {}
            for label, order in (("as the constant reads it", (0, 1, 2)), ("red and blue exchanged", (2, 1, 0))):
                g = game[..., list(order)][sel].mean(0)
                ratios[label] = native[sel].mean(0) / g
                say("    %d sky texels, the picture's channels %s: game mean %s, mirrored sky mean %s, sky / game %s" % (
                    n, label, " ".join("%.4f" % v for v in g), " ".join("%.4f" % v for v in native[sel].mean(0)),
                    " ".join("%.3f" % v for v in ratios[label])))
            ok_colour = all(SKY_BAND[0] <= v <= SKY_BAND[1] for v in ratios["as the constant reads it"])
            other = all(SKY_BAND[0] <= v <= SKY_BAND[1] for v in ratios["red and blue exchanged"])
            ok_row = abs(first - math.ceil(horizon - 0.5)) <= 1.0
            ok_flip = abs(flipped - math.ceil(horizon - 0.5)) <= 1.0
            say("    criterion 5: the channels within %.2f to %.2f: %s (exchanged: %s); the first row within one of the horizon's: %s "
                "(upside down: %s)" % (SKY_BAND[0], SKY_BAND[1], "met" if ok_colour else "NOT MET", "met" if other else "not met",
                                       "met" if ok_row else "NOT MET", "met" if ok_flip else "not met"))
            bad += not (ok_colour and ok_row)
        if args.png and "sky" in made:
            import impostor_pictures as ip
            fc = wc._fc(rows[0], 13)
            image, f = textures.image(fc)
            game = (image[..., list(f["swizzle"][:3])] * 255.0 + 0.5).astype(np.uint8)
            ip.write_png(Path(args.png), np.concatenate([game, (made["sky"] * 255.0 + 0.5).astype(np.uint8)], axis=1))
            say("  sheet %s: the game's picture %08X (left) beside the mirrored sky (right), as stored" % (args.png, fc[1] & 0xFFFFF000))
    if not compared:
        say("no picture was compared")
        return 1
    say("criterion 5: %s" % ("met on every picture" if not bad else "%d pictures do not meet it or were not compared" % bad))
    return 1 if bad else 0


def _replayed(args, say):
    path, fr, game, place = _inputs(args.capture, args.frame, args.place, say)
    if fr is None:
        return None
    r = replay(path, args.frame, place=place, depth=fr.depth, zscale=fr.zscale, zoffset=fr.zoffset)
    for text in r.problems + r.notes:
        say("  " + text)
    return path, fr, game, r


def cmd_glint(args, say=print):
    got = _replayed(args, say)
    if got is None:
        return 1
    path, fr, game, r = got
    if not r.rows:
        say("%s: no A17D row in the frame: the lake is not in this view" % path.name)
        return 1
    g = glint_set(r)
    say("%s frame %d: %d kept pixels; the sun term above %.0f%% of the pixel in %d (the largest share %.3f); a view counts with %d: %s" % (
        path.name, args.frame, g["kept"], 100 * GLINT_SHARE, g["above"], g["largest"], GLINT_MIN, "counts" if g["above"] >= GLINT_MIN else "does not count"))
    return 0 if g["above"] >= GLINT_MIN else 1


def cmd_edge(args, say=print):
    got = _replayed(args, say)
    if got is None:
        return 1
    path, fr, game, r = got
    if not r.rows:
        say("%s: no A17D row in the frame: the lake is not in this view" % path.name)
        return 1
    e = edge_sets(r)
    say("%s frame %d: %d kept pixels; within %g units of the eye: %d (the nearest %.1f units); with an alpha between the reference "
        "and %.2f: %d" % (path.name, args.frame, e["kept"], NEAR_UNITS, e["near"], e["nearest"], JUDGED_ALPHA, e["fading"]))
    return 0


def cmd_lookup(args, say=print):
    got = _replayed(args, say)
    if got is None:
        return 1
    path, fr, game, r = got
    cap = wc.read_capture(path, args.frame)
    textures = wc.RowTextures(cap)
    sampling = np.zeros(r.kept.shape, dtype=bool)
    done = set()
    for row in [x for x in cap["water"] if x.get("frame") == args.frame and wc.row_kind(x) == "surface"]:
        fc = wc._fc(row, 13)
        b = fc[1] & 0xFFFFF000
        sel = r.kept & (r.base == b)
        if not sel.any() or b in done:
            continue
        done.add(b)
        image, f = textures.image(fc)
        picture = image[..., list(f["swizzle"][:3])]
        M = np.array(row["bank"][0:16], dtype=np.float64).reshape(4, 4)
        horizon = horizon_row(view_rows(M, ic.pixel_register(row, 31)[:2], wc.row_height(textures.dir, row)), f["height"])
        lum = luminance(picture)
        low = lum < SKY_DARK * float(luminance(picture_sky(picture)))
        clear = ~_grow(low, LOOKUP_REACH)
        at = r.lookup[sel]
        px = np.floor(at[:, 0] * f["width"]).astype(np.int64) % f["width"]
        py = np.floor(at[:, 1] * f["height"]).astype(np.int64) % f["height"]
        ok = clear[py, px]
        sampling[sel] = ok
        say("  picture %08X (horizon row %.1f): %d kept pixels, lookups at rows %d to %d, columns %d to %d; sky-sampling (the texel and "
            "every texel within %d of it not darker than %.1f of the picture's sky): %d (%.1f%%)" % (
                b, horizon, int(sel.sum()), int(py.min()), int(py.max()), int(px.min()), int(px.max()), LOOKUP_REACH, SKY_DARK,
                int(ok.sum()), 100.0 * ok.mean()))
    total = int(r.kept.sum())
    say("%s frame %d: %d kept pixels, %d sky-sampling (%.1f%%)" % (path.name, args.frame, total, int(sampling.sum()),
                                                                     100.0 * sampling.sum() / total if total else math.nan))
    if args.out:
        np.save(args.out, np.stack([r.kept, sampling]))
        say("  written %s (two planes: kept, sky-sampling)" % args.out)
    return 0 if total else 1


def _hue(rgb):
    import colorsys
    return colorsys.rgb_to_hsv(*[float(max(v, 0.0)) for v in rgb])[0] * 360.0


def cmd_gap(args, say=print):
    import foliage_trace as ft
    got = _replayed(args, say)
    if got is None:
        return 1
    path, fr, game, r = got
    if game is None or fr.depth is None:
        say("the gap: not decidable (no scene or no depth copy)")
        return 1
    head, nscene, nlev, _ = ft.read_dump(args.dump)
    scale = (ft.game_scene(path, args.frame)[1] / head["exposure"]) if head["exposure"] else 1.0
    say("the stored native scene %s: frame %s (the capture's frame row says %s), %d x %d, exposure %g: the native colour times %.6g" % (
        Path(args.dump).name, head["scene_frame"], ft.scene_frame_of(path, args.frame), head["width"], head["height"], head["exposure"], scale))
    H, W = fr.depth.shape
    if nlev.shape != (H, W):
        yi, xi = np.arange(H) * nlev.shape[0] // H, np.arange(W) * nlev.shape[1] // W
        nlev, nscene = nlev[yi][:, xi], nscene[yi][:, xi]
    rows = [x for x in wc.water_rows(path) if x.get("frame") == args.frame and wc.row_kind(x) == "surface"]
    M = np.array(rows[0]["bank"][0:16], dtype=np.float64).reshape(4, 4)
    Mi = np.linalg.inv(M)
    ys, xs = np.mgrid[0:H, 0:W]

    def world(zw):
        c = np.stack([(xs + 0.5) / W * 2 - 1, 1 - (ys + 0.5) / H * 2, zw, np.ones((H, W))], -1) @ Mi.T
        return c[..., :3] / c[..., 3:4]

    nnone = nlev >= 16777215
    nw = world(nlev.astype(np.float64) / 16777215.0)
    gw = world((fr.depth - fr.zoffset) / fr.zscale)
    eye = np.array(wc.read_capture(path)["frames"][args.frame]["light"]["eye"])
    native = nscene[..., :3].astype(np.float64) * scale
    heights = sorted({round(wc.row_height(path.parent, x), 4) for x in rows})

    def line(name, sel):
        n = int(sel.sum())
        if not n:
            say("  %-44s 0 pixels" % name)
            return
        g, a = game[sel].mean(0), native[sel].mean(0)
        dh = (_hue(a) - _hue(g) + 180.0) % 360.0 - 180.0
        nd, gd = np.linalg.norm(nw[sel] - eye, axis=-1), np.linalg.norm(gw[sel] - eye, axis=-1)
        gl, nl = luminance(game[sel]), luminance(native[sel])
        say("  %-44s %6d px %5.2f%% | native / game red %.3f green %.3f blue %.3f luminance %.3f | hue %+.1f degrees | luminance 5th, "
            "50th, 95th: game %.3f %.3f %.3f native %.3f %.3f %.3f | native depth within 1%% of the game's copy %.1f%%" % (
                name, n, 100.0 * n / sel.size, a[0] / g[0], a[1] / g[1], a[2] / g[2], nl.mean() / gl.mean(), dh,
                *np.percentile(gl, [5, 50, 95]), *np.percentile(nl, [5, 50, 95]), 100.0 * (np.abs(nd / gd - 1.0) < 0.01).mean()))

    for h in heights:
        line("the placeholder's footprint, native depth at %.4f" % h, ~nnone & (np.abs(nw[..., 2] - h) < 0.05))
    line("the replay's kept pixels", r.kept)
    line("the replay's judged pixels (alpha 0.99 or more)", r.kept & (r.alpha >= JUDGED_ALPHA))
    for b in sorted(int(x) for x in np.unique(r.base[r.kept])):
        line("  kept, sampling 0x%08X" % b, r.kept & (r.base == b))
    line("the whole picture", np.ones((H, W), dtype=bool))
    return 0


# --- the stored native scene's water (plan Tasks 11 and 12; ledger ruling 44) ---------------------------------------

def far_share(a, b, reach=1):
    """The share of `a`'s pixels farther than `reach` pixels from every pixel of `b` (two masks); NaN for an empty a."""
    n = int(a.sum())
    return float((a & ~_grow(b, reach)).sum()) / n if n else math.nan


def cover_measure(native, kept, reach=1):
    """Criterion 7's first part: {"native": the native water pixels' share farther than `reach` from the replay's,
    "replay": the replay's kept pixels' share farther than `reach` from the native ones, "verdict"}."""
    out = {"native_pixels": int(native.sum()), "replay_pixels": int(kept.sum()),
           "native": far_share(native, kept, reach), "replay": far_share(kept, native, reach)}
    ok = all(v == v and v <= COVER_FAR for v in (out["native"], out["replay"]))
    out["verdict"] = "met" if ok else "not met as written" if out["native_pixels"] and out["replay_pixels"] else "not decidable"
    return out


def main_measure(native, replayed, judged, edge=MAIN_EDGE):
    """`main`'s measure: native and replayed [h, w, 3] in one unit, judged [h, w]. On the judged pixels more than
    `edge` pixels from the judged set's edge: {"pixels", "ratio" (native over replay, the means' a channel), "median",
    "p5", "p95" (of the per-pixel luminance ratio), "missed" (the bounds missed, by name), "verdict", "distance" (the
    mean over those pixels and the three channels of |native - replayed|: how near the two lie, for main_rounded)}.
    Pixels whose replayed luminance is not above 0 are left out of the percentiles and counted ("dark")."""
    inner = _shrink(np.asarray(judged, dtype=bool), edge)
    n = int(inner.sum())
    out = {"pixels": n, "ratio": (math.nan,) * 3, "median": math.nan, "p5": math.nan, "p95": math.nan, "dark": 0, "missed": [], "verdict": "not decidable",
           "distance": math.nan}
    if not n:
        return out
    a, b = np.asarray(native, dtype=np.float64)[inner], np.asarray(replayed, dtype=np.float64)[inner]
    out["distance"] = float(np.abs(a - b).mean())
    means = b.mean(0)
    out["ratio"] = tuple(float(x / y) if y > 0 else math.nan for x, y in zip(a.mean(0), means))
    la, lb = luminance(a), luminance(b)
    lit = lb > 0
    out["dark"] = int((~lit).sum())
    if lit.any():
        ratio = la[lit] / lb[lit]
        out["p5"], out["median"], out["p95"] = (float(v) for v in np.percentile(ratio, [5, 50, 95]))
    if not all(v == v and MAIN_BAND[0] <= v <= MAIN_BAND[1] for v in out["ratio"]):
        out["missed"].append("the ratio of the means")
    if not (out["median"] == out["median"] and MAIN_BAND[0] <= out["median"] <= MAIN_BAND[1]):
        out["missed"].append("the median")
    if not (out["p5"] == out["p5"] and out["p5"] >= MAIN_SPAN[0] and out["p95"] <= MAIN_SPAN[1]):
        out["missed"].append("the 5th to 95th percentile span")
    out["verdict"] = "met" if not out["missed"] else "not met as written"
    return out


def main_rounded(native, replayed, judged, edge=MAIN_EDGE):
    """`main`'s measure against a plane of the linear target's number format (ledger ruling 46; the review of Tasks 11
    and 12, I3). The stored plane holds 6 bits of mantissa in red and green and 5 in blue; an exact replay against a
    target that truncates reads about 0.992 in red and green and 0.985 in blue, outside ruling 44's 0.99 to 1.01 with
    a right program. Which rounding an adapter applies when it writes the format is not established, so BOTH are
    computed: {"nearest": main_measure against quantise(replayed, "nearest"), "toward zero": likewise, "exact": against
    the replay as it is (reported, not bound), "nearer": the rounding whose replay lies nearer the plane (the smaller
    "distance"; "nearest" on a tie; None without a judged interior), "tie": the two distances are equal, "verdict":
    the nearer rounding's (ruling 44's bound is applied to that one and to no other)}. `native` and `replayed` in the
    plane's own unit (replay(out_scale=)), `replayed` already held to the program's clamp."""
    out = {name: main_measure(native, quantise(replayed, name), judged, edge) for name in ROUNDINGS}
    out["exact"] = main_measure(native, replayed, judged, edge)
    a, b = (out[name]["distance"] for name in ROUNDINGS)
    if a != a or b != b:
        out.update(nearer=None, tie=False, verdict="not decidable")
        return out
    out["nearer"] = ROUNDINGS[0] if a <= b else ROUNDINGS[1]
    out["tie"] = a == b
    out["verdict"] = out[out["nearer"]]["verdict"]
    return out


def colour_measure(native, game, sel):
    """Criterion 8's figures on a set: {"pixels", "ratio" (native over game, the means' a channel), "hue" (degrees,
    native minus game), "p" (the 5th, 50th and 95th percentile of the two luminances: game, native)}."""
    n = int(np.asarray(sel).sum())
    if not n:
        return {"pixels": 0, "ratio": (math.nan,) * 3, "hue": math.nan, "p": ((math.nan,) * 3, (math.nan,) * 3)}
    g, a = np.asarray(game, dtype=np.float64)[sel], np.asarray(native, dtype=np.float64)[sel]
    gm, am = g.mean(0), a.mean(0)
    hue = (_hue(am) - _hue(gm) + 180.0) % 360.0 - 180.0
    return {"pixels": n, "ratio": tuple(float(x / y) if y > 0 else math.nan for x, y in zip(am, gm)), "hue": float(hue),
            "p": (tuple(float(v) for v in np.percentile(luminance(g), [5, 50, 95])), tuple(float(v) for v in np.percentile(luminance(a), [5, 50, 95])))}


def colour_verdict(m, least=JUDGED_MIN):
    """Criterion 8 on the sky-sampling set's figures: "not decidable" below `least` pixels; else "met" when red, green
    and blue are within COLOUR_BAND and the hue within HUE_BOUND degrees."""
    if m["pixels"] < least:
        return "not decidable"
    ok = all(v == v and COLOUR_BAND[0] <= v <= COLOUR_BAND[1] for v in m["ratio"]) and abs(m["hue"]) <= HUE_BOUND
    return "met" if ok else "not met as written"


def target_pictures(water):
    """{base: the target's red, green and blue as floats in 0..1 [256, 256, 3]} of a stored scene's water part
    (foliage_trace.read_dump_water): what the pass's surface draws sampled, in the shader's own channel order."""
    return {int(base): np.asarray(rgba, dtype=np.float64)[..., :3] / 255.0 for base, _, rgba in water["targets"]}


def targets_equal(water):
    """Whether the stored targets of one frame are equal texel for texel (phase 13b: each is the mirrored sky alone)."""
    pictures = [np.asarray(rgba)[..., :3] for _, _, rgba in water["targets"]]
    return bool(pictures) and all(np.array_equal(pictures[0], p) for p in pictures[1:])


def _stored(args, say, need=(), game=True):
    """(head, native scene, native levels, the water's part, the factor that brings the native colour to the game's
    scale) of the stored scene, or None with the reason said. `need`: the water's planes the command cannot do without.
    `game=False`: the factor is None and nothing of the game's read-back is asked for (`main`, `targets`)."""
    import foliage_trace as ft
    path = cr.find_capture(str(args.capture))
    head, nscene, nlev, _ = ft.read_dump(args.dump)
    water = ft.read_dump_water(args.dump)
    want = ft.scene_frame_of(path, args.frame)
    say("the stored native scene %s: version %d, flags %d, scene frame %s (the capture's frame row %d says %s), exposure %g, %d reflection targets, "
        "the depth before the water %s, the scene after the water step %s" % (
            Path(args.dump).name, head["version"], head["flags"], head["scene_frame"], args.frame, want, head["exposure"], len(water["targets"]),
            "stored" if water["pre_water"] is not None else "not stored", "stored" if water["water"] is not None else "not stored"))
    if want is not None and int(head["scene_frame"]) != int(want):
        say("NOT COMPARED: the stored scene is not the capture's frame %d" % args.frame)
        return None
    for name in need:
        if (not water["targets"]) if name == "targets" else water[name] is None:
            say("NOT COMPARED: the stored scene has no %s (its frame's water was not drawn, or the binary is of before the water)" % name)
            return None
    if not game:
        return head, nscene, nlev, water, None
    scale = (ft.game_scene(path, args.frame)[1] / head["exposure"]) if head["exposure"] else 1.0
    return head, nscene, nlev, water, scale


def cmd_targets(args, say=print):
    got = _stored(args, say, need=("targets",), game=False)
    if got is None:
        return 1
    head, _, _, water, _ = got
    path = cr.find_capture(str(args.capture))
    cap = wc.read_capture(path, args.frame)
    textures = wc.RowTextures(cap)
    rows = [r for r in cap["water"] if r.get("frame") == args.frame and wc.row_kind(r) == "surface"]
    by_base = {}
    for row in rows:
        by_base.setdefault(wc._fc(row, 13)[1] & 0xFFFFF000, row)
    bad = empty = 0
    for base, height, rgba in water["targets"]:
        native = np.asarray(rgba, dtype=np.float64)[..., :3] / 255.0
        row = by_base.get(int(base))
        if row is None:
            say("  target %08X (height %.6g): no water row of the frame samples that base: NOT COMPARED" % (base, height))
            bad += 1
            continue
        fc = wc._fc(row, 13)
        f = textures.fields(fc)
        # The game's own picture of the base: read back by the emulator. THE SOFTWARE ADAPTER'S READ-BACK LEAVES IT
        # EMPTY (sub-project 12's ledger: every base 131,072 zero bytes), and a capture may hold no dump of it: the
        # colour is then not compared and the pair decides nothing about it (ledger ruling 46).
        try:
            image, _ = textures.image(fc)
            game = image[..., list(f["swizzle"][:3])]
            why = None if float(game.max()) > 0.0 else "is EMPTY"
        except (cr.Unreadable, OSError) as e:
            game, why = None, "is NOT READ, taken as EMPTY (%s)" % e
        M = np.array(row["bank"][0:16], dtype=np.float64).reshape(4, 4)
        mesh = wc.row_height(textures.dir, row)
        view = view_rows(M, ic.pixel_register(row, 31)[:2], mesh)
        horizon = horizon_row(view, native.shape[0])           # in the target's own rows (the game's picture has as many: 256)
        first = first_content_row(native)
        ok_row = abs(first - math.ceil(horizon - 0.5)) <= 1.0
        ok_height = mesh is not None and abs(float(height) - float(mesh)) <= 1e-3
        head_text = "  target %08X: height %.6g (the mesh's %s): %s; the first row with content %.1f against the derived horizon %.2f: %s; " % (
            base, height, "%.6g" % mesh if mesh is not None else "not flat", "equal" if ok_height else "NOT EQUAL", first, horizon,
            "in place" if ok_row else "OUT OF PLACE")
        tail_text = "; alpha set on %.1f%% of its texels" % (100.0 * float((np.asarray(rgba)[..., 3] > 0).mean()))
        if why is not None:
            empty += 1
            say(head_text + "the capture's picture of this base %s: the colour is LEFT UNCOMPARED (the software adapter's read-back leaves "
                "the game's pictures empty; the colour against the game's picture is a real-GPU check)" % why + tail_text)
            bad += not (ok_row and ok_height)
            continue
        if game.shape[:2] != native.shape[:2]:
            say(head_text + "the capture's picture of this base is %d x %d, the target %d x %d: NOT COMPARED" % (
                game.shape[1], game.shape[0], native.shape[1], native.shape[0]) + tail_text)
            bad += 1
            continue
        sel = sky_texels(game, horizon)
        n = int(sel.sum())
        ratio = native[sel].mean(0) / game[sel].mean(0) if n else np.full(3, np.nan)
        ok_colour = n > 0 and all(SKY_BAND[0] <= v <= SKY_BAND[1] for v in ratio)
        say(head_text + "%d of the game's sky texels, native / game %s: %s" % (n, " ".join("%.3f" % v for v in ratio), "met" if ok_colour else "NOT MET") +
            tail_text)
        bad += not (ok_colour and ok_row and ok_height)
    equal = targets_equal(water)
    say("  the %d targets are %s texel for texel" % (len(water["targets"]), "equal" if equal else "NOT EQUAL"))
    missing = sorted(set(by_base) - {int(b) for b, _, _ in water["targets"]})
    if missing:
        say("  bases the frame's rows sample and the stored scene has no target of: %s" % " ".join("%08X" % b for b in missing))
    ok = not bad and equal and not missing
    if empty:
        # (no verdict on the criterion: its colour part was not compared. Exit 1: an input is not there.)
        say("criterion 10 (13b): NOT DECIDED on this pair: %d of %d of the game's pictures are empty or not read, as on the software adapter; "
            "the targets' colour against the game's pictures is a real-GPU check (run this command on a real-GPU pair); what needs no picture "
            "of the game: %s" % (empty, len(water["targets"]), "met" if ok else "FAULT (a line above says which)"))
        return 1
    say("criterion 10 (13b): %s" % ("met" if ok else "not met as written"))
    return 0 if ok else 1


def _native_planes(args, say, need):
    """The replay of the capture's frame and the stored scene brought to the game's picture: (path, fr, game, r, head,
    native colour at the game's scale [H, W, 3], native levels [H, W], the water's part, the scale), or None."""
    got = _replayed(args, say)
    if got is None:
        return None
    path, fr, game, r = got
    stored = _stored(args, say, need)
    if stored is None:
        return None
    head, nscene, nlev, water, scale = stored
    H, W = r.kept.shape
    if nlev.shape != (H, W):
        say("NOT COMPARED: the stored scene is %d x %d, the replay %d x %d" % (nlev.shape[1], nlev.shape[0], W, H))
        return None
    return path, fr, game, r, head, nscene[..., :3].astype(np.float64) * scale, nlev, water, scale


def cmd_cover(args, say=print):
    got = _native_planes(args, say, ("pre_water",))
    if got is None:
        return 1
    path, fr, game, r, head, native, nlev, water, scale = got
    if fr.depth is None:
        say("criterion 7: not decidable (no depth copy)")
        return 1
    pre = water["pre_water"]
    native_water = nlev < pre                       # the last depth nearer than the depth before the water step
    m = cover_measure(native_water, r.kept)
    say("criterion 7 (a): native water pixels %d, the replay's kept pixels %d; farther than one pixel from the other: native %.2f%%, "
        "replay %.2f%% (bound %.0f%% each): %s" % (m["native_pixels"], m["replay_pixels"], 100.0 * m["native"], 100.0 * m["replay"],
                                                     100.0 * COVER_FAR, m["verdict"]))
    rows = [x for x in wc.water_rows(path) if x.get("frame") == args.frame and wc.row_kind(x) == "surface"]
    M = np.array(rows[0]["bank"][0:16], dtype=np.float64).reshape(4, 4)
    Mi = np.linalg.inv(M)
    H, W = nlev.shape
    ys, xs = np.mgrid[0:H, 0:W]

    def distance(zw):
        c = np.stack([(xs + 0.5) / W * 2 - 1, 1 - (ys + 0.5) / H * 2, zw, np.ones((H, W))], -1) @ Mi.T
        return np.linalg.norm(c[..., :3] / c[..., 3:4] - eye, axis=-1)

    eye = np.array(wc.read_capture(path)["frames"][args.frame]["light"]["eye"])
    bed = distance(pre.astype(np.float64) / 16777215.0)
    copy = distance((fr.depth - fr.zoffset) / fr.zscale)
    n = int(native_water.sum())
    share = float((np.abs(bed[native_water] / copy[native_water] - 1.0) < 0.01).mean()) if n else math.nan
    ok_bed = share == share and share >= BED_SHARE
    say("criterion 7 (b): the bed: on the %d native water pixels the depth before the water step lies within 1%% of the game's depth copy at "
        "%.1f%% (bound %.0f%%): %s" % (n, 100.0 * share, 100.0 * BED_SHARE, "met" if ok_bed else "not met as written" if n else "not decidable"))
    return 0 if m["verdict"] == "met" and ok_bed else 1


def _sky_sampling(path, frame, r, say):
    """`lookup`'s set: the replay's kept pixels whose lookup's texel and every texel within LOOKUP_REACH of it is sky in
    the game's own picture of the row's base."""
    cap = wc.read_capture(path, frame)
    textures = wc.RowTextures(cap)
    sampling = np.zeros(r.kept.shape, dtype=bool)
    done = set()
    for row in [x for x in cap["water"] if x.get("frame") == frame and wc.row_kind(x) == "surface"]:
        fc = wc._fc(row, 13)
        b = fc[1] & 0xFFFFF000
        sel = r.kept & (r.base == b)
        if not sel.any() or b in done:
            continue
        done.add(b)
        image, f = textures.image(fc)
        picture = image[..., list(f["swizzle"][:3])]
        low = luminance(picture) < SKY_DARK * float(luminance(picture_sky(picture)))
        clear = ~_grow(low, LOOKUP_REACH)
        at = r.lookup[sel]
        px = np.floor(at[:, 0] * f["width"]).astype(np.int64) % f["width"]
        py = np.floor(at[:, 1] * f["height"]).astype(np.int64) % f["height"]
        sampling[sel] = clear[py, px]
    return sampling


def cmd_colour(args, say=print):
    got = _native_planes(args, say, ())
    if got is None:
        return 1
    path, fr, game, r, head, native, nlev, water, scale = got
    if game is None:
        say("criterion 8: not decidable (the capture holds no scene)")
        return 1
    judged = r.kept & (r.alpha >= JUDGED_ALPHA)
    sky = judged & _sky_sampling(path, args.frame, r, say)
    shore = judged & ~sky
    sets = (("the sky-sampling judged pixels", sky), ("the far-shore judged pixels (not bound)", shore), ("the whole footprint (not bound)", r.kept))
    figures = {}
    for name, sel in sets:
        m = figures[name] = colour_measure(native, game, sel)
        say("  %-42s %6d px: native / game red %.3f green %.3f blue %.3f, hue %+.1f degrees; luminance 5th, 50th, 95th: game %s native %s" % (
            name, m["pixels"], *m["ratio"], m["hue"], " ".join("%.3f" % v for v in m["p"][0]), " ".join("%.3f" % v for v in m["p"][1])))
    on = figures[sets[0][0]]
    verdict = colour_verdict(on)
    if args.off_capture and args.off_dump:
        import foliage_trace as ft
        off_path = cr.find_capture(str(args.off_capture))
        off_head, off_scene, _, _ = ft.read_dump(args.off_dump)
        off_game, off_c20w = ft.game_scene(off_path, args.frame)
        off_native = off_scene[..., :3].astype(np.float64) * ((off_c20w / off_head["exposure"]) if off_head["exposure"] else 1.0)
        if off_native.shape[:2] == sky.shape:
            off = colour_measure(off_native, off_game, sky)
            say("  the off-run's pair %s / %s on the same pixels: native / game red %.3f green %.3f blue %.3f, hue %+.1f degrees" % (
                off_path.name, Path(args.off_dump).name, *off["ratio"], off["hue"]))
            nearer = all(abs(on["ratio"][k] - 1.0) <= 0.5 * abs(off["ratio"][k] - 1.0) for k in (0, 2))
            say("  red and blue nearer to 1 than half the off-run's distance: %s" % ("yes" if nearer else "NO"))
            if verdict == "met" and not nearer:
                verdict = "not met as written"
        else:
            say("  the off-run's stored scene has another size: NOT COMPARED")
    say("criterion 8 (bounds %.2f to %.2f a channel, the hue within %.0f degrees, at least %d sky-sampling judged pixels): %s" % (
        COLOUR_BAND[0], COLOUR_BAND[1], HUE_BOUND, JUDGED_MIN, verdict))
    return 0 if verdict == "met" else 1


def cmd_main(args, say=print):
    # Everything here is the stored scene's and the capture's rows': NOTHING OF THE GAME'S READ-BACK (no depth copy, no
    # scene 0x199E0000, no picture of the game), so the software adapter's pair carries the check (ledger ruling 46).
    path = cr.find_capture(str(args.capture))
    stored = _stored(args, say, need=("targets", "water", "pre_water"), game=False)
    if stored is None:
        return 1
    head, nscene, nlev, water, _ = stored
    exposure = float(head["exposure"])
    if not exposure > 0.0:
        say("NOT COMPARED: the stored scene's exposure is %g" % exposure)
        return 1
    rows = [x for x in wc.water_rows(path) if x.get("frame") == args.frame and wc.row_kind(x) == "surface"]
    own = sorted({float(ic.pixel_register(x, 20)[3]) for x in rows if ic.pixel_register(x, 20) is not None})
    post = bool(int(head["flags"]) & 1)
    say("  the output scale is the stored scene's exposure %.9g (%s); the frame's %d surface rows have c20.w %s%s" % (
        exposure, "a post frame: the game's scale" if post else "no post: the pass's fixed exposure", len(rows),
        " ".join("%.9g" % v for v in own) or "(none read)",
        "; A ROW'S c20.w IS NOT THE EXPOSURE WITHIN A THOUSANDTH (the pass's scale rule would have refused the frame)"
        if post and any(abs(v - exposure) > 1e-3 * max(exposure, 1.0) for v in own) else ""))
    place = tuple(args.place) if args.place is not None else PASS_PLACE
    say("  the depth test and the judged set: the stored scene's own depth before the water step (the rows' own function on it), sampled at "
        "(%g, %g)%s; slot 14 is zero, as the native surface has it" % (place[0], place[1], "" if args.place is not None else ", the native pixel centre"))
    pictures = target_pictures(water)
    r = replay(path, args.frame, levels=args.levels, place=place, depth=pass_depth(water["pre_water"]), pictures=pictures, near=no_near,
               out_scale=exposure)
    for text in r.problems + r.notes:
        say("  " + text)
    say("  the replay fed the stored scene's own targets in the game's pictures' place: %s; rows replayed %d of %d; kept %d (killed %d, "
        "skipped %d, hidden by the stored depth %d)" % (" ".join("%08X" % b for b in sorted(pictures)), r.rows, len(rows), int(r.kept.sum()),
                                                         r.fates["killed"], r.fates["skipped"], r.hidden))
    native = np.asarray(water["water"], dtype=np.float64)[..., :3]
    if native.shape[:2] != r.kept.shape:
        say("NOT COMPARED: the stored plane is %d x %d" % (native.shape[1], native.shape[0]))
        return 1
    judged = r.kept & (r.alpha >= JUDGED_ALPHA)
    replayed = np.clip(r.colour, 0.0, PASS_MAX)               # the program's own clamp of what it writes
    m = main_rounded(native, replayed, judged)
    say("  judged pixels %d, of which %d lie more than %d pixels from the judged set's edge (%d of them with no replayed light)" % (
        int(judged.sum()), m["exact"]["pixels"], MAIN_EDGE, m["exact"]["dark"]))
    say("  the stored scene after the water step over the replay, the replay brought to the plane's number format (R11G11B10 float: %d bits "
        "of mantissa in red and green, %d in blue) with each rounding:" % (TARGET_MANTISSA[0], TARGET_MANTISSA[2]))
    for name, label in ((ROUNDINGS[0], "rounded %s:" % ROUNDINGS[0]), (ROUNDINGS[1], "rounded %s:" % ROUNDINGS[1]),
                        ("exact", "not rounded (reported, not bound):")):
        x = m[name]
        say("    %-34s red %.4f green %.4f blue %.4f; per-pixel luminance ratio: 5th %.4f, median %.4f, 95th %.4f; mean distance %.3g: %s%s" % (
            label, *x["ratio"], x["p5"], x["median"], x["p95"], x["distance"], x["verdict"],
            " (misses %s)" % ", ".join(x["missed"]) if x["missed"] else ""))
    nearer = m["nearer"]
    missed = m[nearer]["missed"] if nearer else []
    say("the surface program's main by value (the ratio of the means and the median within %.2f to %.2f, the 5th to 95th percentile inside "
        "%.2f to %.2f), applied to the rounding that lies nearer the stored plane (%s%s): %s%s" % (
            MAIN_BAND[0], MAIN_BAND[1], MAIN_SPAN[0], MAIN_SPAN[1], nearer or "none: no judged interior",
            "; the two lie equally near" if m["tie"] else "", m["verdict"], " (misses %s)" % ", ".join(missed) if missed else ""))
    return 0 if m["verdict"] == "met" and not r.problems else 1


def main(argv=None, say=print):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)

    def common(p, many=False):
        p.add_argument("capture", nargs="+" if many else None)
        p.add_argument("--place", type=float, nargs=2, default=None)
        return p

    p = common(sub.add_parser("scene"))
    p.add_argument("frame", type=int)
    p.add_argument("--flip", action="store_true")
    p.add_argument("--swap-rb", action="store_true")
    p.add_argument("--no-glint", action="store_true")
    p.add_argument("--levels", choices=("mean", "stored"), default="mean")
    p.add_argument("--normal", choices=("exact", "rounded"), default="exact")
    p.set_defaults(run=cmd_scene)
    p = common(sub.add_parser("variants"), many=True)
    p.add_argument("--frame", type=int, default=1)
    p.add_argument("--levels", choices=("mean", "stored"), default="mean")
    p.set_defaults(run=cmd_variants)
    p = sub.add_parser("sky")
    p.add_argument("capture", nargs="+")
    p.add_argument("--frame", type=int, default=1)
    p.add_argument("--png")
    p.set_defaults(run=cmd_sky)
    p = common(sub.add_parser("gap"))
    p.add_argument("dump")
    p.add_argument("--frame", type=int, default=1)
    p.set_defaults(run=cmd_gap)
    for name, run in (("lookup", cmd_lookup), ("glint", cmd_glint), ("edge", cmd_edge)):
        p = common(sub.add_parser(name))
        p.add_argument("--frame", type=int, default=1)
        if name == "lookup":
            p.add_argument("--out")
        p.set_defaults(run=run)
    for name, run in (("targets", cmd_targets), ("cover", cmd_cover), ("colour", cmd_colour), ("main", cmd_main)):
        p = common(sub.add_parser(name))
        p.add_argument("dump")
        if name == "colour":
            p.add_argument("off_capture", nargs="?")
            p.add_argument("off_dump", nargs="?")
        p.add_argument("--frame", type=int, default=1)
        if name == "main":
            p.add_argument("--levels", choices=("mean", "stored"), default="mean")
        p.set_defaults(run=run)
    p = common(sub.add_parser("view"), many=True)
    p.add_argument("--frame", type=int, default=1)
    p.add_argument("--view", type=int, action="append", help="only this view (1 for the frame's first); may be given again")
    p.add_argument("--png")
    p.add_argument("--water", action="store_true", help="the replayed pictures through `scene` in the game's pictures' place")
    p.set_defaults(run=cmd_view)
    args = ap.parse_args(argv)
    try:
        return args.run(args, say)
    except cr.Unreadable as e:
        say("NOT READ: %s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
