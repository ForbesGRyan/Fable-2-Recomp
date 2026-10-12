"""The numbers the C++ mirrors of the lake's surface and its HLSL are pinned to (the water sub-project, plan Task 4; the
mirrors are plan Task 10's: WaterMirror, WaterOpaqueDead, WaterViewRows), made by light_formulas.py: water_vertex,
water_alpha, water_kept, water_opaque_dead, water_pixel, and by water_check.water_view_rows.

Inputs, all in this file (nothing is read from a capture when the lines are made). CAPTURED_ROWS: four A17D rows of
frame 1 of four discovery captures of 2026-10-11 (155547 the bridge, 155731 the third spot, 155929 by the trees,
160104 on the sun side), each with its eight vertex registers (c0 to c3, c115 to c118), its 24 pixel registers in
light_formulas.WATER_SHADER["ps_reg"]'s order and its bool word ("pbool_load"), AS CAPTURED: the components no
formula reads hold the capture's garbage (c129.z is no number), which a mirror must not touch.
tests/test_water_formulas.py reads them again from the capture when it is there. CAPTURED_VERTICES and
CAPTURED_PIXELS: vertices of those rows' meshes and pixels inside them (one of the row's largest glint, one of its
middle) with the texels the capture's dumps give at the pixel's fetches. MADE_*: what the captured rows do not
reach (the near branch with a picture at slot 14, the shadow factor under b137, a sun mirrored into the eye, a
pixel before the fog's start, land, and the four points at which the console's zero product decides the pixel:
ledger ruling 34). Every number is a single-precision bit pattern, as the renderer holds it.

What a line pins (sub-project 12's ledger ruling 26): the SINGLE-PRECISION reference, `single=True`: the inputs read
and every operation's result rounded to single precision in the reference's order, a multiply-add as a rounded
product and a rounded sum, a product with a zero factor +0 (the console's rule). The double reference lies up to
2.1e-5 of max(1, |colour|) from it on the captured rows (water_check.py rows) and up to 2.5e-4 on made pixels: a
mirror is held to THESE numbers, to a few units in the last place, never to the double reference.

  // water vertex pins    the position (3), the pair (2), c0 to c3 and c115 to c118 (4 each), then oPos (4), o0.xyz
                          (3), o1 (4), o3.xy (2)
  // water alpha pins     the mask's sample, c141 (4), the state's alpha reference, the kind (0 the surface A17D, 1 the
                          opaque twin F6D9), then the alpha, the fate (0 killed, 1 skipped: colour 0 with its alpha,
                          2 shaded) and whether the state's test draws the pixel (1: not killed and alpha > reference).
                          TWO thresholds: c141.w decides shaded against skipped inside the shader, the reference
                          decides drawn against not; they differ by one unit in the last place in every captured row
                          (c141.w 0x3D70F0F1, register 0x210E 0x3D70F0F2) and are two things.
  // water pixel pins     the position register's first two components, o0.xyz, o1 (4), the mask's sample, the 24
                          pixel registers (4 each), the bool word, the two normal samples (2 each), slot 13's and
                          slot 14's texel (3 each), slot 8's, the in-scatter texel at a and the transmittance texel
                          at s (3 each), then the fate, the alpha, oC0.rgb, the lookup of slot 13 and of slot 14 (2
                          each), the normal (3), the fresnel factor, the distance, the sun term (3) and the fog's two
                          coordinates (0, 0 before the start)
  // water view pins      M (c0 to c3 of the main scene, 16), c31.xy, the plane's height, then the sixteen floats of
                          diag(c31.x, c31.y, 1, 1) * M * Mirror(h), made in double from those singles and rounded once
                          (WaterViewRows computes in double: spec Design 7)
  // water dead pins      c141 (4), then light_formulas.water_opaque_dead (1 or 0): min(c141.x, c141.x - c141.y) >=
                          1e-6 with magnitudes at most 4, the difference and the comparison in DOUBLE on the single
                          inputs (the difference of two singles is exact in double); the lines stand at the margin
                          itself, one unit below it, at the captured value, beyond 4 and at no number

The literals are not in the lines: they are the table's (src/native/capture/water_table.inc).

Usage: python tools/xdk_sigmatch/water_pins.py          the lines
       python tools/xdk_sigmatch/water_pins.py --inc    tests/native/water_pins.inc (plan Task 10 commits it): the same
                                                        lines, each section between #ifdef FABLE2_WATER_PINS_<SECTION>
                                                        and #endif
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import light_formulas as lf  # noqa: E402

VS_REG = (0, 1, 2, 3) + tuple(lf.WATER_SHADER["reg"])
PS_REG = tuple(lf.WATER_SHADER["ps_reg"])


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def from_bits(b):
    return struct.unpack('<f', struct.pack('<I', b))[0]


def bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


# (capture, frame, the row's place among the frame's A17D rows, the bool word, the eight vertex registers, the 24
# pixel registers), every register as four float bit patterns.
CAPTURED_ROWS = (
    # 0: capture 155547 frame 1, A17D row 0 (line 4906)
    ("155547", 1, 0, 0x00000110,
     ((0x3EFB927F, 0xBF584DE2, 0x32BBAA18, 0x42B93B3D),
      (0x3E883ECF, 0x3E1E75BF, 0x3FDAE636, 0xC303FCA0),
      (0x3F59CE39, 0x3EFD5181, 0xBE356896, 0xC30AE279),
      (0x3F59CD1B, 0x3EFD5035, 0xBE3567A8, 0xC30AC829),
      (0x3F84AC10, 0x3F96B91C, 0x00000000, 0x00000000),
      (0xC0166CE6, 0x3F81378B, 0x00000000, 0x000017AE),
      (0x3DA3D70A, 0x3DA3D70A, 0x000064A0, 0x10000000),
      (0x3D75C28F, 0x3D75C28F, 0x00000004, 0x00000001)),
     ((0x00000000, 0x00000000, 0x3A6A0EA1, 0x3AB60B61), (0x42A424EA, 0x431D56DA, 0x4248AECD, 0x00000000), (0x3BBB3EB6, 0x3D8F6272, 0x3DBA5340, 0x41128284),
      (0x3F800000, 0x3F800000, 0x00000000, 0x00000000), (0x3F2AAAAB, 0x3F2AAAAB, 0x00000000, 0x3F800000), (0xBF54D061, 0x3DB936DD, 0x3F0C64A0, 0x00000000),
      (0x41988BBB, 0x44ED8000, 0x3F800000, 0x3A03126F), (0x3E883284, 0x3ECBCABF, 0x3EEEEEF0, 0x00000000), (0x3DD52887, 0x00000000, 0x00000000, 0x00000000),
      (0xBED7753A, 0x00000000, 0x00000000, 0x00000000), (0x3CE0E0E2, 0x3D989899, 0x3D30B0B1, 0x00000000), (0x3D70F0F2, 0x3E048485, 0x3DF8F8FA, 0x00000000),
      (0x3E99999A, 0x3C23D70A, 0x00000000, 0x41500000), (0x3ED7753A, 0x3ED7753A, 0xFFFFFFFF, 0x65735C43), (0x3ED7753A, 0x3ED7753A, 0x00000000, 0x3F800000),
      (0x3F000000, 0x00000000, 0x00000000, 0x3F800000), (0x3E8EF12D, 0x00000000, 0x00000004, 0x00000001), (0x3F000000, 0x000027AF, 0x00003F4E, 0x00001928),
      (0x43000000, 0x42000000, 0x3F800000, 0x3F800000), (0x3F4CCCCD, 0x3E4CCCCC, 0xBF1F7DB0, 0x701BF030), (0x3D4CCCCD, 0x40EA79BD, 0x41AA10BB, 0x701BF030),
      (0x40003127, 0x40000000, 0x3AC49BA6, 0x3D70F0F1), (0x3F28878C, 0xBD92AC4B, 0xBF3FD3C3, 0x80000000), (0x3F7FE51A, 0x3F572106, 0x3ECF94EB, 0x3F800000))),
    # 1: capture 155731 frame 1, A17D row 6 (line 5536)
    ("155731", 1, 6, 0x00000110,
     ((0xBD0A3EF1, 0xBF7A11ED, 0x339C6314, 0x431C1DA6),
      (0x3EE15DA7, 0xBC792CEB, 0x3FD7276B, 0xC2EB966D),
      (0x3F777F56, 0xBD08D2E0, 0xBE81C6E3, 0xC27355C4),
      (0x3F777E11, 0xBD08D22C, 0xBE81C639, 0xC272EE1E),
      (0x3F93BEA7, 0x3FA7FB6E, 0x00000000, 0x00000000),
      (0xC028A507, 0x3F915D9A, 0x00000000, 0x000016C2),
      (0x3DA3D70A, 0x3DA3D70A, 0x000063F0, 0x10000000),
      (0x3D75C290, 0x3D75C290, 0x00000004, 0x00000001)),
     ((0x00000000, 0x00000000, 0x3A6A0EA1, 0x3AB60B61), (0x42A2CDB6, 0x431D0158, 0x4248BAD0, 0x00000000), (0x3BCBD4CB, 0x3D9058DF, 0x3DBB55CE, 0x410C80F9),
      (0x3F800000, 0x3F800000, 0x00000000, 0x00000000), (0x3F2AAAAB, 0x3F2AAAAB, 0x00000000, 0x3F800000), (0xBF54AEB5, 0x3D8E5ABF, 0x3F0D5EC8, 0x00000000),
      (0x419B8388, 0x44ED8000, 0x3F800000, 0x3A03126F), (0x3E88586D, 0x3ECC3C7B, 0x3EEEEEF0, 0x00000000), (0x3DE80A3D, 0x00000000, 0x00000000, 0x00000000),
      (0xBED866EA, 0x00000000, 0x00000000, 0x00000000), (0x3CE0E0E2, 0x3D989899, 0x3D30B0B1, 0x00000000), (0x3D70F0F2, 0x3E048485, 0x3DF8F8FA, 0x00000000),
      (0x3E99999A, 0x3C23D70A, 0x00000000, 0x41500000), (0x3ED866EA, 0x3ED866EA, 0xFFFFFFFF, 0x61796D61), (0x3ED866EA, 0x3ED866EA, 0x00000000, 0x3F800000),
      (0x3F000000, 0x00000000, 0x00000000, 0x3F800000), (0x3E8DFF7D, 0x00000000, 0x00000004, 0x00000001), (0x3F000000, 0x000026FB, 0x00003E31, 0x000018FC),
      (0x43000000, 0x42000000, 0x3F800000, 0x3F800000), (0x3F4CCCCE, 0x3E4CCCC8, 0xBEDC452B, 0x701BF030), (0x3D4CCCCD, 0x40F0C205, 0x41AE34A7, 0x701BF030),
      (0x40003127, 0x40000000, 0x3AC49BA6, 0x3D70F0F1), (0x3F25A7E8, 0xBD5DC1A8, 0xBF42AF39, 0x80000000), (0x3F7EBB73, 0x3F55F821, 0x3ECE5170, 0x3F800000))),
    # 2: capture 155929 frame 1, A17D row 0 (line 4159)
    ("155929", 1, 0, 0x00000110,
     ((0x3F03E661, 0x3F54A1A0, 0x335AF11E, 0xC33A51CD),
      (0xBEA3E4BC, 0x3E4B555C, 0x3FD920C5, 0xC2B70165),
      (0xBF545FED, 0x3F03BDA1, 0xBE5DFDB8, 0xC0F7139F),
      (0xBF545ED6, 0x3F03BCF4, 0xBE5DFC95, 0xC0F3DF28),
      (0x3FB7734C, 0x3FD10CCF, 0x00000000, 0x00000000),
      (0xC055548A, 0x3FB99AA9, 0x00000000, 0x000017AE),
      (0x3DA3D70A, 0x3DA3D70A, 0x000064A0, 0x10000000),
      (0x3D75C28E, 0x3D75C28E, 0x00000004, 0x00000001)),
     ((0x00000000, 0x00000000, 0x3A6A0EA1, 0x3AB60B61), (0x42A8EC92, 0x432BED8B, 0x42470523, 0x00000000), (0x3BF5CBE6, 0x3D92C85A, 0x3DBDE3FC, 0x4108AE1E),
      (0x3F800000, 0x3F800000, 0x00000000, 0x00000000), (0x3F2AAAAB, 0x3F2AAAAB, 0x00000000, 0x3F800000), (0xBF53D49E, 0x3C885A2B, 0x3F0FAF9D, 0x00000000),
      (0x41A305EA, 0x44ED8000, 0x3F800000, 0x3A03126F), (0x3E88B859, 0x3ECD5C3E, 0x3EEEEEF0, 0x00000000), (0x3E0BE80E, 0x00000000, 0x00000000, 0x00000000),
      (0xBEDACA68, 0x00000000, 0x00000000, 0x00000000), (0x3CE0E0E2, 0x3D989899, 0x3D30B0B1, 0x00000000), (0x3D70F0F2, 0x3E048485, 0x3DF8F8FA, 0x00000000),
      (0x3E99999A, 0x3C23D70A, 0x00000000, 0x41500000), (0x3EDACA68, 0x3EDACA68, 0xFFFFFFFF, 0x5F4D4E46), (0x3EDACA68, 0x3EDACA68, 0x00000000, 0x3F800000),
      (0x3F000000, 0x00000000, 0x00000000, 0x3F800000), (0x3E8B9BFF, 0x00000000, 0x00000004, 0x00000001), (0x3F000000, 0x000027AF, 0x00003F4E, 0x00001928),
      (0x43000000, 0x42000000, 0x3F800000, 0x3F800000), (0x3F4CCCCD, 0x3E4CCCCC, 0xBF1F7DB0, 0x701BF030), (0x3D4CCCCD, 0x40EA79BD, 0x41AA10BB, 0x701BF030),
      (0x40003127, 0x40000000, 0x3AC49BA6, 0x3D70F0F1), (0x3F1DBAF9, 0xBC4B0EB7, 0xBF499C78, 0x80000000), (0x3F7BCA5A, 0x3F5308F3, 0x3ECB1EFF, 0x3F800000))),
    # 3: capture 160104 frame 1, A17D row 1 (line 4838)
    ("160104", 1, 1, 0x00000110,
     ((0x3EBA0488, 0x3F684A75, 0xB2BBAA1A, 0xC3306FC8),
      (0xBE92512B, 0x3DEA572A, 0x3FDAE62F, 0xC29BB947),
      (0xBF69E72B, 0x3EBB4F07, 0xBE356942, 0x4208AA2A),
      (0xBF69E5F8, 0x3EBB4E11, 0xBE356854, 0x42090FDD),
      (0x3F946149, 0x3FA8B53E, 0x00000000, 0x00000000),
      (0xC0296646, 0x3F920785, 0x00000000, 0x000017AE),
      (0x3DA3D70A, 0x3DA3D70A, 0x000064A0, 0x10000000),
      (0x3D75C28F, 0x3D75C28F, 0x00000004, 0x00000001)),
     ((0x00000000, 0x00000000, 0x3A6A0EA1, 0x3AB60B61), (0x42B61278, 0x431DFE88, 0x4248AECD, 0x00000000), (0x3BCC6E44, 0x3D9061C6, 0x3DBB5F26, 0x40FB4978),
      (0x3F800000, 0x3F800000, 0x00000000, 0x00000000), (0x3F2AAAAB, 0x3F2AAAAB, 0x00000000, 0x3F800000), (0xBF54ACEE, 0x3D8CCE47, 0x3F0D67A7, 0x00000000),
      (0x419B9EFE, 0x44ED8000, 0x3F800000, 0x3A03126F), (0x3E8859CC, 0x3ECC4098, 0x3EEEEEF0, 0x00000000), (0x3DE8B8F3, 0x00000000, 0x00000000, 0x00000000),
      (0xBED86FA6, 0x00000000, 0x00000000, 0x00000000), (0x3CE0E0E2, 0x3D989899, 0x3D30B0B1, 0x00000000), (0x3D70F0F1, 0x3E048485, 0x3DF8F8FA, 0x00000000),
      (0x3E99999A, 0x3C23D70A, 0x00000000, 0x41500000), (0x3ED86FA6, 0x3ED86FA6, 0xFFFFFFFF, 0x5F4D4E46), (0x3ED86FA6, 0x3ED86FA6, 0x00000000, 0x3F800000),
      (0x3F000000, 0x00000000, 0x00000000, 0x3F800000), (0x3E8DF6C1, 0x00000000, 0x00000004, 0x00000001), (0x3F000000, 0x000027AF, 0x00003F4E, 0x00001928),
      (0x43000000, 0x42000000, 0x3F800000, 0x3F800000), (0x3F4CCCCD, 0x3E4CCCCC, 0xBF1F7DB0, 0x701BF030), (0x3D4CCCCD, 0x40EA79BD, 0x41AA10BB, 0x701BF030),
      (0x40003127, 0x40000000, 0x3AC49BA6, 0x3D70F0F1), (0x3F258C8F, 0xBD5B35AC, 0xBF42C95C, 0x80000000), (0x3F7EB0B0, 0x3F55ED65, 0x3ECE45BF, 0x3F800000))),
)
# Vertices of those rows' meshes: (the row, the position, the pair).
CAPTURED_VERTICES = (
    (0, (0x42740000, 0x434C0001, 0x41A0D1BC), (0x00000000, 0x00000000)),   # vertex 0
    (0, (0x42740000, 0x435C0001, 0x41A0D1BC), (0x00000000, 0x3F2AAAAB)),   # vertex 14
    (1, (0x43F00001, 0x435E0009, 0x41A0D1BC), (0x00000000, 0x00000000)),   # vertex 0
    (1, (0x43F75556, 0x43710009, 0x41A0D1BC), (0x3F2AAAAB, 0x3F000000)),   # vertex 14
    (2, (0x427E0000, 0x434B0001, 0x41A0D1BC), (0x00000000, 0x00000000)),   # vertex 0
    (2, (0x427E0000, 0x434C0001, 0x41A0D1BC), (0x00000000, 0x3F800000)),   # vertex 2
    (3, (0x42740000, 0x434C0001, 0x41A0D1BC), (0x00000000, 0x00000000)),   # vertex 0
    (3, (0x42740000, 0x435C0001, 0x41A0D1BC), (0x00000000, 0x3F2AAAAB)),   # vertex 14
)
# Pixels inside those rows' meshes: (the row, the position register's xy, o0.xyz, o1, the mask's sample, the two normal
# samples, slot 13's texel, slot 14's, slot 8's, the in-scatter texel, the transmittance texel).
CAPTURED_PIXELS = (
    (0, (0x40A00000, 0x441D8000), (0x42BF9F21, 0x435FDC7D, 0x41A0D1BC), (0x410B38CA, 0x4198B100, 0x40597CF2, 0x41670F17), 0x3D0DD489, (0x3EFEFEFF, 0x3EF6B6B7), (0x3EFEFEFF, 0x3EFAFAFB), (0x3D0C84A6, 0x3D40C0C1, 0x3D9C87E9), (0x00000000, 0x00000000, 0x00000000), 0x3F800000, (0x3E9B8000, 0x3EC12000, 0x3EE9A000), (0x3F6E3A59, 0x3F6C3857, 0x3F5E996E)),   # glint 9.92e-28 of the colour, 74 units
    (0, (0x40C00000, 0x441EC000), (0x42BF3A65, 0x435EBE16, 0x41A0D1BC), (0x410AF852, 0x4197F9B4, 0x4058BB89, 0x4165FC25), 0x3EC72233, (0x3EF993E3, 0x3EFF0361), (0x3EFCFCFD, 0x3EFFFADB), (0x3D384B0F, 0x3D687B40, 0x3DA90607), (0x00000000, 0x00000000, 0x00000000), 0x3F800000, (0x3E9B8000, 0x3EC12000, 0x3EE9A000), (0x3F6EA0AC, 0x3F6C9EAA, 0x3F5F21DB)),   # glint 7.75e-46 of the colour, 73 units
    (1, (0x43CA0000, 0x43740000), (0x43F6452A, 0x4380AD29, 0x41A0D1BC), (0x42223ADD, 0x41AF3466, 0x41D756BB, 0x41849D5D), 0x3B5401ED, (0x3EF50002, 0x3EF47779), (0x3EF6513B, 0x3F052028), (0x3E8F17EC, 0x3EB74014, 0x3EE97246), (0x00000000, 0x00000000, 0x00000000), 0x3F800000, (0x3E9C8000, 0x3EC2C000, 0x3EEBC000), (0x3F2351A8, 0x3F19479E, 0x3ED430DE)),   # glint 2.94e-70 of the colour, 424 units
    (1, (0x43CF8000, 0x43740000), (0x43F6036D, 0x43793E18, 0x41A0D1BC), (0x422210CA, 0x41AA039D, 0x41D7179F, 0x4180B8C7), 0x00000000, (0x3F063C7C, 0x3EF8CECD), (0x3EFBED49, 0x3F04B676), (0x3E71C1B9, 0x3E9AFDAB, 0x3ECE245D), (0x00000000, 0x00000000, 0x00000000), 0x3F800000, (0x3E9C8000, 0x3EC2C000, 0x3EEBC000), (0x3F239EC9, 0x3F1994BF, 0x3ED4CB1F)),   # glint 0 of the colour, 422 units
    (2, (0x44434000, 0x44300000), (0x42807C06, 0x434B3B02, 0x41A0D1BC), (0x40D2529D, 0x418F2216, 0x3F057090, 0x415A4D41), 0x00000000, (0x3EF77EF0, 0x3EFA69EA), (0x3F016CB0, 0x3EF0C59F), (0x3E98C629, 0x3E673F3A, 0x3E87C42E), (0x00000000, 0x00000000, 0x00000000), 0x3F800000, (0x3E9FEA50, 0x3EC6FBB0, 0x3EF0EBA0), (0x3F7700AE, 0x3F75FFAD, 0x3F6EFF1F)),   # glint 0.636 of the colour, 48 units
    (2, (0x44414000, 0x442E4000), (0x427E2364, 0x434B39FB, 0x41A0D1BC), (0x40D082D5, 0x418F216D, 0x3EF523C4, 0x415A4C45), 0x00000000, (0x3EFC954C, 0x3F02B86C), (0x3F052044, 0x3EF6BEA0), (0x3E428336, 0x3E180499, 0x3E33C692), (0x00000000, 0x00000000, 0x00000000), 0x3F800000, (0x3EA222F1, 0x3EC7DE6E, 0x3EF0B9EA), (0x3F76E2DC, 0x3F75E1DC, 0x3F6ECD6E)),   # glint 7.71e-14 of the colour, 48 units
    (3, (0x44898000, 0x4430C000), (0x42904E06, 0x4351BA42, 0x41A0D1BC), (0x40DDCE1C, 0x4190C518, 0x3FD754D2, 0x415B9796), 0x3C703581, (0x3EFD91EA, 0x3EEBEDEE), (0x3EFAFAFB, 0x3EFF0A22), (0x3D30B0B1, 0x3D330AA4, 0x3D808081), (0x00000000, 0x00000000, 0x00000000), 0x3F800000, (0x3EA2AAF0, 0x3EC5565E, 0x3EEB33EB), (0x3F71DC9A, 0x3F6FCFED, 0x3F65B08B)),   # glint 0.409 of the colour, 63 units
    (3, (0x44898000, 0x44268000), (0x428B960D, 0x435704B7, 0x41A0D1BC), (0x40D7C3E8, 0x419427F2, 0x3FC53634, 0x4160ABDD), 0x3CB45E33, (0x3EFF9CAF, 0x3F00BF3F), (0x3F010565, 0x3EFF178B), (0x3D350C43, 0x3D42ED59, 0x3D754C83), (0x00000000, 0x00000000, 0x00000000), 0x3F800000, (0x3EA867AE, 0x3EC7B888, 0x3EEAB4B2), (0x3F708B15, 0x3F6DD5A5, 0x3F6264BF)),   # glint 6.72e-32 of the colour, 68 units
)
# Three captured views' inputs: (M, the main scene's c0 to c3; c31.xy; the height of the meshes that sample the view's
# picture). The game's own view rows are held to the derived ones by water_check.py views.
CAPTURED_VIEWS = (
    ((0x3EFB927F, 0xBF584DE2, 0x32BBAA18, 0x42B93B3D, 0x3E883ECF, 0x3E1E75BF, 0x3FDAE636, 0xC303FCA0, 0x3F59CE39, 0x3EFD5181, 0xBE356896, 0xC30AE279, 0x3F59CD1B, 0x3EFD5035, 0xBE3567A8, 0xC30AC829),
     (0x3F2AAAAB, 0x3F2AAAAB), 0x4221E568),   # capture 155547 frame 1 view 2, picture 13481000
    ((0xBD0A3EF1, 0xBF7A11ED, 0x339C6314, 0x431C1DA6, 0x3EE15DA7, 0xBC792CEB, 0x3FD7276B, 0xC2EB966D, 0x3F777F56, 0xBD08D2E0, 0xBE81C6E3, 0xC27355C4, 0x3F777E11, 0xBD08D22C, 0xBE81C639, 0xC272EE1E),
     (0x3F2AAAAB, 0x3F2AAAAB), 0x41A0D1BC),   # capture 155731 frame 1 view 1, picture 13401000
    ((0x3F03E661, 0x3F54A1A0, 0x335AF11E, 0xC33A51CD, 0xBEA3E4BC, 0x3E4B555C, 0x3FD920C5, 0xC2B70165, 0xBF545FED, 0x3F03BDA1, 0xBE5DFDB8, 0xC0F7139F, 0xBF545ED6, 0x3F03BCF4, 0xBE5DFC95, 0xC0F3DF28),
     (0x3F2AAAAB, 0x3F2AAAAB), 0x41A0D1BC),   # capture 155929 frame 1 view 1, picture 13401000
)


def _floats(t):
    return tuple(from_bits(b) for b in t)


def _b(values):
    return tuple(bits(float(v)) for v in values)


def row_registers(index):
    """(vertex {register: [4]}, pixel {register: [4]}, the bool word) of CAPTURED_ROWS[index], as floats."""
    _, _, _, word, vertex, pixel = CAPTURED_ROWS[index]
    return ({reg: list(_floats(vertex[i])) for i, reg in enumerate(VS_REG)},
            {reg: list(_floats(pixel[i])) for i, reg in enumerate(PS_REG)}, word)


# Made vertices: (the row, the position, the pair).
MADE_VERTICES = (
    (0, _b((-311.5, 902.25, 20.10241)), _b((0.125, 0.875))),
    (1, _b((1500.0, -64.0, 40.0)), _b((1.0, 0.0))),
)

# The alpha lines: (the mask's sample, c141, the state's alpha reference, the kind). CAPTURED_C141 and REFERENCE are
# every captured row's (water_check.py bools / states: 164 rows).
CAPTURED_C141 = (0x40003127, 0x40000000, 0x3AC49BA6, 0x3D70F0F1)
REFERENCE = 0x3D70F0F2
_C = CAPTURED_C141
ALPHA = (
    (bits(0.0), _C, REFERENCE, 0),                     # open water: 0.997, shaded and drawn
    (bits(0.0), _C, REFERENCE, 1),                     # the opaque twin kills it
    (bits(100.0 / 255.0), _C, REFERENCE, 0),           # the fade
    (bits(120.0 / 255.0), _C, REFERENCE, 0),           # just above the threshold
    (bits(121.0 / 255.0), _C, REFERENCE, 0),           # at the shore: below both thresholds, colour 0, not drawn
    (bits(1.0), _C, REFERENCE, 0),                     # land: alpha 0
    (bits(1.0), _C, REFERENCE, 1),
    (bits(0.5), (bits(0.5), bits(1.0), 0, 0x3D70F0F1), REFERENCE, 0),      # the ramp at 0: alpha 1, the surface kills
    (bits(0.5), (bits(0.5), bits(1.0), 0, 0x3D70F0F1), REFERENCE, 1),      # and the opaque twin shades
    (bits(0.25), (bits(1.0), bits(1.0), 0, bits(0.5)), bits(0.125), 0),    # alpha 0.75 above both
    (bits(0.75), (bits(1.0), bits(1.0), 0, bits(0.5)), bits(0.125), 0),    # alpha 0.25: skipped by c141.w (colour 0) and DRAWN by the state's test
    (bits(0.75), (bits(1.0), bits(1.0), 0, bits(0.125)), bits(0.5), 0),    # alpha 0.25: shaded by c141.w and NOT drawn
)


def _pixel(row, q, P, coord, m, a, b, r13, r14, shadow, scatter, transmit, over=None, word=None):
    """A made pixel on a captured row's registers with some replaced: the tuple of CAPTURED_PIXELS' shape plus the
    replaced registers and the bool word."""
    return (row, _b(q), _b(P), _b(coord), bits(m), _b(a), _b(b), _b(r13), _b(r14), bits(shadow), _b(scatter), _b(transmit),
            {reg: _b(v) for reg, v in (over or {}).items()}, word)


_EYE = _floats(CAPTURED_ROWS[0][5][PS_REG.index(19)])[:3]
_SKY = (0.17, 0.21, 0.33)
_S, _T = (0.31, 0.38, 0.45), (0.62, 0.55, 0.47)
MADE_PIXELS = (
    # the near branch: 8 units from the eye, a picture at slot 14
    _pixel(0, (300.0, 500.0), (_EYE[0] + 4.8, _EYE[1] + 6.4, _EYE[2]), (7.25, 13.5, 2.75, 10.0), 0.0, (0.52, 0.47), (0.49, 0.55),
           _SKY, (0.4, 0.3, 0.2), 1.0, _S, _T),
    # the shadow factor under b137 (bit 9), half in shadow
    _pixel(0, (640.0, 360.0), (_EYE[0] + 90.0, _EYE[1] + 120.0, 20.10241), (15.4, 23.3, 5.2, 17.6), 0.0, (0.5, 0.5), (0.45, 0.56),
           _SKY, (0.0, 0.0, 0.0), 0.5, _S, _T, word=0x00000310),
    # the sun mirrored into the eye over a level surface: the glint whole (c142 points from the sun; not a unit vector)
    _pixel(0, (560.0, 400.0), (_EYE[0] + 60.0, _EYE[1], 20.10241), (12.0, 13.7, 2.5, 10.4), 0.0, (0.5, 0.5), (0.5, 0.5),
           _SKY, (0.0, 0.0, 0.0), 1.0, _S, _T, over={142: (-0.894427, 0.0, -0.447214, 0.0), 19: (_EYE[0], _EYE[1], 50.10241, 1.0)}),
    # before the fog's start: no table is read
    _pixel(0, (100.0, 700.0), (_EYE[0] + 3.0, _EYE[1] + 4.0, _EYE[2] - 12.0), (7.0, 13.0, 2.0, 10.0), 0.0, (0.51, 0.5), (0.5, 0.49),
           _SKY, (0.1, 0.1, 0.1), 1.0, _S, _T),
    # land: skipped, colour 0 with its alpha; and the surface's kill
    _pixel(0, (640.0, 360.0), (_EYE[0] + 90.0, _EYE[1] + 120.0, 20.10241), (15.4, 23.3, 5.2, 17.6), 1.0, (0.5, 0.5), (0.5, 0.5),
           _SKY, (0.0, 0.0, 0.0), 1.0, _S, _T),
    _pixel(0, (640.0, 360.0), (_EYE[0] + 90.0, _EYE[1] + 120.0, 20.10241), (15.4, 23.3, 5.2, 17.6), 0.5, (0.5, 0.5), (0.5, 0.5),
           _SKY, (0.0, 0.0, 0.0), 1.0, _S, _T, over={141: (0.5, 1.0, 0.0, 0.0588235)}),
    # the four points of the console's zero product (ledger ruling 34): a record's rule keeps c128.x, c136.x and c138.x
    # above 0, so a mirror never meets three of these in a drawn frame; they pin what the reference IS there
    _pixel(0, (640.0, 360.0), (_EYE[0] + 90.0, _EYE[1] + 120.0, 20.10241), (15.4, 23.3, 5.2, 17.6), 0.0, (0.5, 0.5), (0.5, 0.5),
           _SKY, (0.0, 0.0, 0.0), 1.0, _S, _T, over={128: (0.0, 0.0, 0.0, 0.0)}),                      # flat_zero: N = 0
    _pixel(0, (640.0, 360.0), _EYE, (15.4, 23.3, 5.2, 17.6), 0.0, (0.52, 0.47), (0.49, 0.55),
           _SKY, (0.4, 0.3, 0.2), 1.0, _S, _T),                                                         # at_eye: V = 0
    _pixel(0, (640.0, 360.0), (_EYE[0] + 90.0, _EYE[1] + 120.0, 20.10241), (15.4, 23.3, 5.2, 17.6), 0.0, (0.5, 0.5), (0.5, 0.5),
           _SKY, (0.0, 0.0, 0.0), 1.0, _S, _T, over={138: (0.0, 0.0, 0.0, 0.0), 142: (0.0, 0.0, 1.0, 0.0)}),   # power_zero: the sun's whole colour
    _pixel(0, (640.0, 360.0), (_EYE[0] + 90.0, _EYE[1] + 120.0, 20.10241), (15.4, 23.3, 5.2, 17.6), 0.0, (0.5, 0.5), (0.5, 0.5),
           _SKY, (0.0, 0.0, 0.0), 1.0, _S, _T, over={136: (0.0, 0.0, 0.0, 0.0)}),                      # second_flat: N2 = 0
)
MADE_KINDS = ("near", "shadow", "glint", "before the fog", "skipped", "killed", "flat_zero", "at_eye", "power_zero", "second_flat")

# The dead lines: c141 as bit patterns (module text).
DEAD = (
    CAPTURED_C141,
    (0x358637BE, 0x00000000, 0, 0),         # y = 0: the smallest x for which the condition holds (1.00000011e-06)
    (0x358637BD, 0x00000000, 0, 0),         # one unit in the last place below it
    (0x40000005, 0x40000000, 0, 0),         # y = 2, the captured: the smallest x for which the condition holds (2.00000119)
    (0x40000004, 0x40000000, 0, 0),         # one unit in the last place below it
    (0x358637BE, 0xBFC00000, 0, 0),         # y below 0: the nearer end is x itself: the smallest x for which the condition holds (1.00000011e-06)
    (0x358637BD, 0xBFC00000, 0, 0),         # one unit in the last place below it
    (0x40800000, 0x3F800000, 0, 0),         # x = 4, the largest magnitude
    (0x40800001, 0x3F800000, 0, 0),         # beyond 4
    (0x40000000, 0xC0800001, 0, 0),         # y beyond -4
    (0x7FC00000, 0x40000000, 0, 0),         # no number
    (0x40003127, 0x7F800000, 0, 0),         # infinite
    (0x40000000, 0x40000000, 0, 0),         # x = y: alpha 1 at a mask of 0, alive
    (0x3F800000, 0x40000000, 0, 0),         # x below y
)


def vertex_pin(item):
    """((position, pair, the eight registers), (oPos, o0.xyz, o1, o3.xy)) of a (row, position, pair) tuple."""
    row, position, pair = item
    c, _, _ = row_registers(row)
    out = lf.water_vertex(_floats(position), _floats(pair), c, single=True)
    return (_floats(position), _floats(pair), tuple(tuple(c[reg]) for reg in VS_REG)), (tuple(out["pos"]), tuple(out["place"]), tuple(out["coord"]), tuple(out["mask"]))


FATES = {"killed": 0, "skipped": 1, "shaded": 2}
KINDS = ("surface", "opaque")


def alpha_pin(item):
    """((m, c141, reference, kind), (alpha, fate, drawn))."""
    m, c141, reference, kind = item
    m, c141, reference = from_bits(m), _floats(c141), from_bits(reference)
    alpha = lf.water_alpha(m, c141, single=True)
    fate = lf.water_kept(alpha, c141, KINDS[kind])
    return (m, c141, reference, kind), (alpha, FATES[fate], int(fate != "killed" and alpha > reference))


def pixel_inputs(item):
    """(q, P, coord, m, registers {reg: [4]}, the bool word, the callbacks' texels: a, b, r13, r14, shadow, scatter,
    transmit) of a CAPTURED_PIXELS or MADE_PIXELS tuple, as floats."""
    row, q, P, coord, m, a, b, r13, r14, shadow, scatter, transmit = item[:12]
    over, word = (item[12], item[13]) if len(item) > 12 else ({}, None)
    _, c, row_word = row_registers(row)
    for reg, values in over.items():
        c[reg] = list(_floats(values))
    return (_floats(q), _floats(P), _floats(coord), from_bits(m), c, row_word if word is None else word,
            _floats(a), _floats(b), _floats(r13), _floats(r14), from_bits(shadow), _floats(scatter), _floats(transmit))


def pixel_pin(item):
    """(pixel_inputs(item), the results: fate, alpha, colour, lookup, lookup14, normal, fresnel, distance, sun, fog_at)
    by water_pixel(..., single=True) with callbacks that return the item's texels."""
    q, P, coord, m, c, word, a, b, r13, r14, shadow, scatter, transmit = inputs = pixel_inputs(item)
    samples = iter((a, b))
    tex = {"normal": lambda u, v: next(samples), 13: lambda u, v: r13, 14: lambda u, v: r14, 8: lambda u, v: shadow,
           "tf4": lambda x: scatter, "tf5": lambda x: transmit}
    out = lf.water_pixel(q, P, coord[:2], coord[2:], m, c, tex, bools=lf.water_bools(word), single=True)
    zero2, zero3 = (0.0, 0.0), (0.0, 0.0, 0.0)
    if out["fate"] != "shaded":
        return inputs, (FATES[out["fate"]], out["alpha"], tuple(out["colour"]), zero2, zero2, zero3, 0.0, 0.0, zero3, zero2)
    return inputs, (FATES[out["fate"]], out["alpha"], tuple(out["colour"]), tuple(out["lookup"]), tuple(out["lookup14"]),
                    tuple(out["normal"]), out["fresnel"], out["distance"], tuple(out["sun"]), tuple(out["fog_at"] or zero2))


def view_pin(item):
    """((M sixteen, c31.xy, h), the sixteen floats of diag(c31.x, c31.y, 1, 1) * M * Mirror(h)): double arithmetic on
    the single inputs, each entry rounded to single once. Mirror(h) negates the third column and adds 2 h times it
    to the fourth."""
    M, c31, h = _floats(item[0]), _floats(item[1]), from_bits(item[2])
    out = []
    for row in range(4):
        r = M[4 * row:4 * row + 4]
        scale = c31[row] if row < 2 else 1.0
        out += [f32(scale * r[0]), f32(scale * r[1]), f32(scale * -r[2]), f32(scale * (r[3] + 2.0 * h * r[2]))]
    return (M, c31, h), tuple(out)


def dead_pin(item):
    c141 = _floats(item)
    return c141, int(lf.water_opaque_dead(c141))


def _x(v):
    return '0x%08Xu' % bits(v)


def _v(values):
    return '{' + ', '.join(_x(v) for v in values) + '}'


def _regs(regs):
    return '{' + ', '.join(_v(r) for r in regs) + '}'


def lines():
    """The pins as lines of a C++ table (every number a float's bits; counts and flags plain)."""
    out = ['// water vertex pins: the position, the pair, c0 to c3 and c115 to c118, then oPos, o0.xyz, o1 and o3.xy '
           '(light_formulas.water_vertex, single=True: the single-precision reference of sub-project 12\'s ledger ruling 26)']
    for k, item in enumerate(CAPTURED_VERTICES + MADE_VERTICES):
        (position, pair, regs), (pos, place, coord, mask) = vertex_pin(item)
        if not all(math.isfinite(v) for v in pos + place + coord + mask):
            raise ValueError('water_pins: vertex line %d is no number' % k)
        out.append('    {%s, %s, %s, %s, %s, %s, %s},  // %s' % (_v(position), _v(pair), _regs(regs), _v(pos), _v(place), _v(coord), _v(mask),
                                                                   'captured' if k < len(CAPTURED_VERTICES) else 'made'))
    out.append('// water alpha pins: the mask\'s sample, c141, the state\'s alpha reference, the kind (0 A17D, 1 F6D9), then the alpha, the '
               'fate (0 killed, 1 skipped, 2 shaded: light_formulas.water_alpha, water_kept, single=True) and drawn (1: not killed and '
               'alpha > reference). c141.w and the reference are two thresholds')
    for item in ALPHA:
        (m, c141, reference, kind), (alpha, fate, drawn) = alpha_pin(item)
        out.append('    {%s, %s, %s, %d, %s, %d, %d},' % (_x(m), _v(c141), _x(reference), kind, _x(alpha), fate, drawn))
    out.append('// water pixel pins: the position register\'s xy, o0.xyz, o1, the mask\'s sample, the 24 pixel registers (c0, c19, c20, c28, '
               'c31, c66 to c68, c116, c117, c126 to c131, c136 to c143), the bool word, the two normal samples, slot 13\'s and slot '
               '14\'s texel, slot 8\'s, the in-scatter and the transmittance texel, then the fate, the alpha, oC0.rgb, the lookups of slots '
               '13 and 14, the normal, the fresnel factor, the distance, the sun term and the fog\'s coordinates '
               '(light_formulas.water_pixel, single=True)')
    for k, item in enumerate(CAPTURED_PIXELS + MADE_PIXELS):
        (q, P, coord, m, c, word, a, b, r13, r14, shadow, scatter, transmit), result = pixel_pin(item)
        fate, alpha, colour, at13, at14, normal, fresnel, distance, sun, fog_at = result
        if not all(math.isfinite(v) for v in (alpha, fresnel, distance) + colour + at13 + at14 + normal + sun + fog_at):
            raise ValueError('water_pins: pixel line %d is no number' % k)
        name = 'captured' if k < len(CAPTURED_PIXELS) else 'made: ' + MADE_KINDS[k - len(CAPTURED_PIXELS)]
        out.append('    {%s, %s, %s, %s, %s, 0x%08Xu, %s, %s, %s, %s, %s, %s, %s, %d, %s, %s, %s, %s, %s, %s, %s, %s, %s},  // %s' % (
            _v(q), _v(P), _v(coord), _x(m), _regs([c[reg] for reg in PS_REG]), word, _v(a), _v(b), _v(r13), _v(r14), _x(shadow), _v(scatter),
            _v(transmit), fate, _x(alpha), _v(colour), _v(at13), _v(at14), _v(normal), _x(fresnel), _x(distance), _v(sun), _v(fog_at), name))
    out.append('// water view pins: M (the main scene\'s c0 to c3), c31.xy, the plane\'s height, then the sixteen floats of '
               'diag(c31.x, c31.y, 1, 1) * M * Mirror(h) (double arithmetic on these singles, rounded once)')
    for item in CAPTURED_VIEWS:
        (M, c31, h), rows = view_pin(item)
        out.append('    {%s, %s, %s, %s},' % (_v(M), _v(c31), _x(h), _v(rows)))
    out.append('// water dead pins: c141, then light_formulas.water_opaque_dead (the difference and the comparison in double on the '
               'single inputs)')
    for item in DEAD:
        c141, dead = dead_pin(item)
        out.append('    {{%s}, %d},' % (', '.join('0x%08Xu' % b for b in item), dead))
    return out


INC_SECTIONS = {"water vertex pins": "VERTEX", "water alpha pins": "ALPHA", "water pixel pins": "PIXEL", "water view pins": "VIEW",
                "water dead pins": "DEAD"}


def inc_lines():
    """tests/native/water_pins.inc: lines() with each section under a macro of its own."""
    out = ['// Generated by tools/xdk_sigmatch/water_pins.py --inc (the water sub-project, plan Task 4). Do not edit: regenerate it.',
           '// The pins of the water mirrors: every number is the bits of a float. A test defines one of',
           '// FABLE2_WATER_PINS_VERTEX, _ALPHA, _PIXEL, _VIEW, _DEAD and includes the file inside the braces of a table of that section.']
    open_ = False
    for line in lines():
        if line.startswith('// '):
            if open_:
                out.append('#endif')
            out.append('#ifdef FABLE2_WATER_PINS_' + INC_SECTIONS[line[3:line.index(':')]])
            open_ = True
        out.append(line)
    out.append('#endif')
    return out


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv not in ([], ['--inc']):
        print(__doc__)
        return 2
    print('\n'.join(inc_lines() if argv else lines()))
    return 0


if __name__ == '__main__':
    sys.exit(main())
