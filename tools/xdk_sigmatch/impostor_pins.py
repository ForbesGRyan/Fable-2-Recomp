"""The numbers the C++ mirrors of the far woods' impostors are pinned to (distant trees, plan Task 6; the mirrors
are plan Task 9's: ImpostorPositionMirror, ImpostorPixelMirror, ImpostorFogMirror), made by light_formulas.py:
impostor_position, impostor_distance, impostor_pixel, impostor_out, fog_coords and impostor_fog.

Inputs, all in this file (nothing is read from a capture when the lines are made). CAPTURED: ten quads of the
in-scene 129F rows of four discovery captures of 2026-10-11 (155547 the bridge, 155731 the third spot, 160251 the
held sunward view, 155929 by the trees), each with its row's fourteen vertex registers and one corner; each tuple
names its capture, frame, line and quad, and tests/test_impostor_formulas.py reads them again from the capture
when it is there. MADE: quads the captured ones do not reach (a lying axis with a third component, both powers
1, a quad level with the eye, a steep view, a picture coordinate that is turned). Every number is a
single-precision bit pattern, as the renderer holds it.

What a position line pins (ledger ruling 26): oPos, o0 (the raised place) and o1 of
impostor_position(corner, word, registers, single=True): the registers, the word and EVERY operation's result
rounded to single precision in the reference's order, which is what the console computes and what a
single-precision mirror computes. The double reference lies up to 1.7e-5 of max(1, |oPos|) from it on the
captured registers (impostor_check.py rows) and up to 2.5e-4 on made ones with large rows; criterion 1's bound
of 1e-4 for a mirror is taken against THESE numbers.

A pixel line: the picture's texel (four values), the raised place, the eye (pixel c19.xyz), c12, c29, c31.x, the
fog pair o2 and o3, c20.w; then the distance, oC0.rgb = impostor_out(impostor_pixel(...)) and the alpha, in
double precision rounded to single. The places are laid out along one ray from the eye so that the distances
cross the dissolve (48 to 56 units) and the light's fall (35 to 45).

A fog line: the raised place, the eye (vertex c47.xyz), c110, c111; then whether the block runs, the three
coordinates (a, s, t) of fog_coords (0 where it does not run), and, for the two texels the line gives (the
transmittance at s and the in-scatter at a, as the mirror's own table read would return them), o2 and o3 of
impostor_fog. Double precision rounded to single.

The literals are not in the lines: they are the table's (src/native/capture/impostor_table.inc).

Usage: python tools/xdk_sigmatch/impostor_pins.py          the lines
       python tools/xdk_sigmatch/impostor_pins.py --inc    tests/native/impostor_pins.inc (plan Task 9 commits it): the
                                                           same lines, each section between #ifdef
                                                           FABLE2_IMPOSTOR_PINS_<SECTION> and #endif
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import light_formulas as lf  # noqa: E402

REG = lf.IMPOSTOR_SHADER["reg"]


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def from_bits(b):
    return struct.unpack('<f', struct.pack('<I', b))[0]


def bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


# (corner, the word (x, y, z, s), the fourteen registers in light_formulas.IMPOSTOR_SHADER["reg"]'s order)
CAPTURED = (
    # capture 155547 frame 1 line 3561 quad 0, 59.5 units from the eye
    (0, (0x4300E000, 0x42F34000, 0x41F8C000, 0x416FA000),
     ((0xBD1F4751, 0x3D993502, 0x3F314983, 0x00000000), (0x3F5D4D2D, 0x3F00B131, 0x00000000, 0x00000000), (0x42A424EA, 0x431D56DA, 0x4248AECD, 0x00000000),
      (0x3F7A3822, 0x00000000, 0x00000000, 0x00000000), (0x00000000, 0x3FDE6ACB, 0x00000000, 0x00000000), (0x00000000, 0x00000000, 0x3F8000A8, 0xBDCCCDDA),
      (0x00000000, 0x00000000, 0x3F800000, 0x00000000), (0xBF54D061, 0x3DB936DD, 0x3F0C64A0, 0x00000000), (0x41988BBB, 0x44ED8000, 0x3F800000, 0x3A03126F),
      (0x3F800000, 0x3C9F4740, 0x3C6E2280, 0x3F7BF370), (0x3FC00000, 0x40000000, 0x00000000, 0x00000000), (0x3F00B12F, 0xBF5D4D2B, 0x32BFFFFE, 0x42BD82C0),
      (0x3E1CD12C, 0x3DB662C8, 0x3F7BF370, 0xC297EA61), (0x3F59CD1B, 0x3EFD5035, 0xBE3567A8, 0xC30AC829))),
    # capture 155547 frame 1 line 3571 quad 0, 49.7 units from the eye
    (1, (0x42F78000, 0x43370000, 0x42160000, 0x4087E000),
     ((0x3D8BF73A, 0x3DA5EF5E, 0x3F6BB89B, 0x00000000), (0x3F5D4D2D, 0x3F00B131, 0x00000000, 0x00000000), (0x42A424EA, 0x431D56DA, 0x4248AECD, 0x00000000),
      (0x3F7A3822, 0x00000000, 0x00000000, 0x00000000), (0x00000000, 0x3FDE6ACB, 0x00000000, 0x00000000), (0x00000000, 0x00000000, 0x3F8000A8, 0xBDCCCDDA),
      (0x00000000, 0x00000000, 0x3F800000, 0x00000000), (0xBF54D061, 0x3DB936DD, 0x3F0C64A0, 0x00000000), (0x41988BBB, 0x44ED8000, 0x3F800000, 0x3A03126F),
      (0x3F800000, 0xBD0BF740, 0x3C772740, 0x3F7BF371), (0x3FC00000, 0x40000000, 0x00000000, 0x00000000), (0x3F00B12F, 0xBF5D4D2B, 0x32BFFFFE, 0x42BD82C0),
      (0x3E1CD12C, 0x3DB662C8, 0x3F7BF370, 0xC297EA61), (0x3F59CD1B, 0x3EFD5035, 0xBE3567A8, 0xC30AC829))),
    # capture 155547 frame 1 line 3581 quad 100, 369.0 units from the eye
    (2, (0x43DEE000, 0x42C2A000, 0x42844000, 0x3FDF6000),
     ((0xBB6B59DB, 0x3C6E185A, 0x3F6C5060, 0x00000000), (0x3F5D4D2D, 0x3F00B131, 0x00000000, 0x00000000), (0x42A424EA, 0x431D56DA, 0x4248AECD, 0x00000000),
      (0x3F7A3822, 0x00000000, 0x00000000, 0x00000000), (0x00000000, 0x3FDE6ACB, 0x00000000, 0x00000000), (0x00000000, 0x00000000, 0x3F8000A8, 0xBDCCCDDA),
      (0x00000000, 0x00000000, 0x3F800000, 0x00000000), (0xBF54D061, 0x3DB936DD, 0x3F0C64A0, 0x00000000), (0x41988BBB, 0x44ED8000, 0x3F800000, 0x3A03126F),
      (0x3F7FFFFF, 0x3AEB5A00, 0x3C16A900, 0x3F7BF370), (0x3FC00000, 0x40000000, 0x00000000, 0x00000000), (0x3F00B12F, 0xBF5D4D2B, 0x32BFFFFE, 0x42BD82C0),
      (0x3E1CD12C, 0x3DB662C8, 0x3F7BF370, 0xC297EA61), (0x3F59CD1B, 0x3EFD5035, 0xBE3567A8, 0xC30AC829))),
    # capture 155547 frame 1 line 3606 quad 5, 228.7 units from the eye
    (3, (0x4389E000, 0x438B8000, 0x4214A000, 0x416F0000),
     ((0x3D06A180, 0xBD5A038D, 0x3F6E9A0E, 0x00000000), (0x3F5D4D2D, 0x3F00B131, 0x00000000, 0x00000000), (0x42A424EA, 0x431D56DA, 0x4248AECD, 0x00000000),
      (0x3F7A3822, 0x00000000, 0x00000000, 0x00000000), (0x00000000, 0x3FDE6ACB, 0x00000000, 0x00000000), (0x00000000, 0x00000000, 0x3F8000A8, 0xBDCCCDDA),
      (0x00000000, 0x00000000, 0x3F800000, 0x00000000), (0xBF54D061, 0x3DB936DD, 0x3F0C64A0, 0x00000000), (0x41988BBB, 0x44ED8000, 0x3F800000, 0x3A03126F),
      (0x3F800000, 0xBC86A180, 0x3B515000, 0x3F7BF36E), (0x3FC00000, 0x40000000, 0x00000000, 0x00000000), (0x3F00B12F, 0xBF5D4D2B, 0x32BFFFFE, 0x42BD82C0),
      (0x3E1CD12C, 0x3DB662C8, 0x3F7BF370, 0xC297EA61), (0x3F59CD1B, 0x3EFD5035, 0xBE3567A8, 0xC30AC829))),
    # capture 155547 frame 1 line 3626 quad 2, 281.1 units from the eye
    (0, (0x43280000, 0x43D3C000, 0x428CA000, 0x41040000),
     ((0xBD07405E, 0x3BF4DCB3, 0x3F725742, 0x00000000), (0x3F5D4D2D, 0x3F00B131, 0x00000000, 0x00000000), (0x42A424EA, 0x431D56DA, 0x4248AECD, 0x00000000),
      (0x3F7A3822, 0x00000000, 0x00000000, 0x00000000), (0x00000000, 0x3FDE6ACB, 0x00000000, 0x00000000), (0x00000000, 0x00000000, 0x3F8000A8, 0xBDCCCDDA),
      (0x00000000, 0x00000000, 0x3F800000, 0x00000000), (0xBF54D061, 0x3DB936DD, 0x3F0C64A0, 0x00000000), (0x41988BBB, 0x44ED8000, 0x3F800000, 0x3A03126F),
      (0x3F800000, 0x3C874060, 0x3C0C6A00, 0x3F7BF372), (0x3FC00000, 0x40000000, 0x00000000, 0x00000000), (0x3F00B12F, 0xBF5D4D2B, 0x32BFFFFE, 0x42BD82C0),
      (0x3E1CD12C, 0x3DB662C8, 0x3F7BF370, 0xC297EA61), (0x3F59CD1B, 0x3EFD5035, 0xBE3567A8, 0xC30AC829))),
    # capture 155731 frame 1 line 4149 quad 0, 50.5 units from the eye
    (2, (0x42F78000, 0x43370000, 0x42160000, 0x4087E000),
     ((0x3D8BF73A, 0x3DA5EF5E, 0x3F6BB89B, 0x00000000), (0x3F7FD8EB, 0xBD0D7097, 0x00000000, 0x00000000), (0x42A2CDB6, 0x431D0158, 0x4248BAD0, 0x00000000),
      (0x3F7A3822, 0x00000000, 0x00000000, 0x00000000), (0x00000000, 0x3FDE6ACB, 0x00000000, 0x00000000), (0x00000000, 0x00000000, 0x3F8000A8, 0xBDCCCDDA),
      (0x00000000, 0x00000000, 0x3F800000, 0x00000000), (0xBF54AEB5, 0x3D8E5ABF, 0x3F0D5EC8, 0x00000000), (0x419B8388, 0x44ED8000, 0x3F800000, 0x3A03126F),
      (0x3F800000, 0xBD0BF740, 0x3CD9E000, 0x3F77A3E2), (0x3FC00000, 0x40000000, 0x00000000, 0x00000000), (0xBD0D7091, 0xBF7FD8E9, 0x339FFFFF, 0x431FB8F6),
      (0x3E81B26B, 0xBC0F6637, 0x3F77A3E1, 0xC287945C), (0x3F777E11, 0xBD08D22C, 0xBE81C639, 0xC272EE1E))),
    # capture 155731 frame 1 line 4174 quad 50, 362.2 units from the eye
    (1, (0x43D7C000, 0x4286E000, 0x4292A000, 0x3F3A2000),
     ((0xBD1D2D4C, 0x3CFA5539, 0x3F6BADB2, 0x00000000), (0x3F7FD8EB, 0xBD0D7097, 0x00000000, 0x00000000), (0x42A2CDB6, 0x431D0158, 0x4248BAD0, 0x00000000),
      (0x3F7A3822, 0x00000000, 0x00000000, 0x00000000), (0x00000000, 0x3FDE6ACB, 0x00000000, 0x00000000), (0x00000000, 0x00000000, 0x3F8000A8, 0xBDCCCDDA),
      (0x00000000, 0x00000000, 0x3F800000, 0x00000000), (0xBF54AEB5, 0x3D8E5ABF, 0x3F0D5EC8, 0x00000000), (0x419B8388, 0x44ED8000, 0x3F800000, 0x3A03126F),
      (0x3F800000, 0x3C9D2D40, 0x3CA57B80, 0x3F77A3E2), (0x3FC00000, 0x40000000, 0x00000000, 0x00000000), (0xBD0D7091, 0xBF7FD8E9, 0x339FFFFF, 0x431FB8F6),
      (0x3E81B26B, 0xBC0F6637, 0x3F77A3E1, 0xC287945C), (0x3F777E11, 0xBD08D22C, 0xBE81C639, 0xC272EE1E))),
    # capture 160251 frame 1 line 4701 quad 3, 57.4 units from the eye
    (3, (0x42038000, 0x4319A000, 0x42176000, 0x4165E000),
     ((0xBD1F4751, 0x3D993502, 0x3F314983, 0x00000000), (0xBF6DA844, 0x3EBE50D4, 0x00000000, 0x00000000), (0x42B431AC, 0x431E5EC9, 0x423CD3F5, 0x00000000),
      (0x3F7A3822, 0x00000000, 0x00000000, 0x00000000), (0x00000000, 0x3FDE6ACB, 0x00000000, 0x00000000), (0x00000000, 0x00000000, 0x3F8000A8, 0xBDCCCDDA),
      (0x00000000, 0x00000000, 0x3F800000, 0x00000000), (0xBF548C02, 0x3D6918A7, 0x3F0DF12E, 0x00000000), (0x419D4D3E, 0x44ED8000, 0x3F800000, 0x3A03126F),
      (0x3F800000, 0x3C9F4740, 0x3D204BE0, 0x3F6320B1), (0x3FC00000, 0x40000000, 0x00000000, 0x00000000), (0x3EBE50D4, 0x3F6DA842, 0x33BFFFFE, 0xC3348348),
      (0x3EDB496B, 0xBE2F9AD3, 0x3F6320B2, 0xC2553FA9), (0xBF52DA8E, 0x3EA8D9FA, 0x3EEC3630, 0x3E4D6100))),
    # capture 160251 frame 1 line 4711 quad 0, 50.8 units from the eye
    (0, (0x421F4000, 0x43240000, 0x42184000, 0x40A12000),
     ((0x3D8BF73A, 0x3DA5EF5E, 0x3F6BB89B, 0x00000000), (0xBF6DA844, 0x3EBE50D4, 0x00000000, 0x00000000), (0x42B431AC, 0x431E5EC9, 0x423CD3F5, 0x00000000),
      (0x3F7A3822, 0x00000000, 0x00000000, 0x00000000), (0x00000000, 0x3FDE6ACB, 0x00000000, 0x00000000), (0x00000000, 0x00000000, 0x3F8000A8, 0xBDCCCDDA),
      (0x00000000, 0x00000000, 0x3F800000, 0x00000000), (0xBF548C02, 0x3D6918A7, 0x3F0DF12E, 0x00000000), (0x419D4D3E, 0x44ED8000, 0x3F800000, 0x3A03126F),
      (0x3F800000, 0xBD0BF748, 0x3D1A6C80, 0x3F6320B2), (0x3FC00000, 0x40000000, 0x00000000, 0x00000000), (0x3EBE50D4, 0x3F6DA842, 0x33BFFFFE, 0xC3348348),
      (0x3EDB496B, 0xBE2F9AD3, 0x3F6320B2, 0xC2553FA9), (0xBF52DA8E, 0x3EA8D9FA, 0x3EEC3630, 0x3E4D6100))),
    # capture 155929 frame 1 line 3001 quad 7, 190.3 units from the eye
    (1, (0xC13F2000, 0x43A58000, 0x42AAA000, 0x409C6000),
     ((0xBB6B59DB, 0x3C6E185A, 0x3F6C5060, 0x00000000), (0xBF598B2F, 0x3F06F27A, 0x00000000, 0x00000000), (0x42A8EC92, 0x432BED8B, 0x42470523, 0x00000000),
      (0x3F7A3822, 0x00000000, 0x00000000, 0x00000000), (0x00000000, 0x3FDE6ACB, 0x00000000, 0x00000000), (0x00000000, 0x00000000, 0x3F8000A8, 0xBDCCCDDA),
      (0x00000000, 0x00000000, 0x3F800000, 0x00000000), (0xBF53D49E, 0x3C885A2B, 0x3F0FAF9D, 0x00000000), (0x41A305EA, 0x44ED8000, 0x3F800000, 0x3A03126F),
      (0x3F800000, 0x3AEB5800, 0x3C5C9DC0, 0x3F79E987), (0x3FC00000, 0x40000000, 0x00000000, 0x00000000), (0x3F06F279, 0x3F598B30, 0x33600000, 0xC33E9FBF),
      (0xBE3CA3CC, 0x3DEA08EA, 0x3F79E988, 0xC252A333), (0xBF545ED6, 0x3F03BCF4, 0xBE5DFC95, 0xC0F3DF28))),
)
# (capture, frame, the row's place among the frame's 129F rows, the quad) of each CAPTURED tuple, in order.
CAPTURED_ROWS = (("155547", 1, 0, 0), ("155547", 1, 2, 0), ("155547", 1, 4, 100), ("155547", 1, 9, 5), ("155547", 1, 13, 2),
                 ("155731", 1, 2, 0), ("155731", 1, 7, 50), ("160251", 1, 0, 3), ("160251", 1, 2, 0), ("155929", 1, 4, 7))


def _made(corner, word, over):
    """A made quad on the first captured quad's registers with some replaced."""
    regs = [list(r) for r in CAPTURED[0][2]]
    for reg, values in over.items():
        regs[REG.index(reg)] = [bits(v) for v in values]
    return (corner, tuple(bits(v) for v in word), tuple(tuple(r) for r in regs))


MADE = (
    # the lying axis with a third component (no captured c46 has one)
    _made(0, (120.0, 140.0, 30.0, 12.5), {46: (0.8, 0.5, 0.33, 0.0)}),
    _made(3, (120.0, 140.0, 30.0, 12.5), {46: (0.8, 0.5, 0.33, 0.0)}),
    # c114.y = 0: both powers 1
    _made(1, (250.0, 90.0, 41.5, 7.25), {114: (1.5, 0.0, 0.0, 0.0)}),
    # a tree whose raised place lies just below and just above the eye's height (50.1707): the height comparison's two sides
    _made(2, (100.0, 200.0, 50.0, 0.25), {8: (0.0, 0.0, 0.5, 0.0)}),
    _made(2, (100.0, 200.0, 50.25, 0.25), {8: (0.0, 0.0, 0.5, 0.0)}),
    # a view looking steeply down, and c113 that turns the picture's coordinate
    _made(0, (90.0, 160.0, 20.0, 9.0), {115: (0.6, -0.8, 0.0, 78.0), 116: (0.48, 0.36, 0.8, -137.0), 117: (-0.64, -0.48, 0.6, 99.0),
                                        113: (0.5, -0.25, 0.125, 0.75)}),
)

# The pixel lines: (texel, the distance wanted, o2, o3). The registers are capture 155547's frame 1 (below).
PIXEL_C12 = (0x41A00000, 0x420C0000, 0x40000000, 0x3EF0D3F5)
PIXEL_C19 = (0x42A424EA, 0x431D56DA, 0x4248AECD, 0x3F800000)
PIXEL_C20W = 0x41128284
PIXEL_C29 = (0x42400000, 0x3E000000, 0x4479999A, 0x41CCCCC0)
PIXEL_C31X = 0x3EBB0564
PIXEL_RAY = (0.36, -0.48, 0.8)             # a unit direction: the place is the eye plus the distance along it
PIXEL = (
    ((0.1290, 0.2581, 0.0323, 1.0), 30.0, (1.0, 1.0, 1.0), (0.0, 0.0, 0.0)),            # absent: nearer than 48
    ((0.1290, 0.2581, 0.0323, 1.0), 38.5, (0.9, 0.8, 0.7), (0.01, 0.02, 0.03)),         # the light falling, absent
    ((0.1290, 0.2581, 0.0323, 1.0), 49.0, (0.9, 0.8, 0.7), (0.01, 0.02, 0.03)),         # an eighth: below the test
    ((0.1290, 0.2581, 0.0323, 1.0), 52.0, (0.9, 0.8, 0.7), (0.01, 0.02, 0.03)),         # a half: not above 128 / 255
    ((0.1290, 0.2581, 0.0323, 1.0), 53.0, (0.9, 0.8, 0.7), (0.01, 0.02, 0.03)),         # one sample of two
    ((0.1290, 0.2581, 0.0323, 1.0), 54.5, (0.9, 0.8, 0.7), (0.01, 0.02, 0.03)),         # above three quarters: both samples
    ((0.4839, 0.5161, 0.2903, 0.75), 60.0, (0.9, 0.8, 0.7), (0.01, 0.02, 0.03)),        # whole, a filtered edge texel
    ((0.1290, 0.2581, 0.0323, 1.0), 300.0, (0.55, 0.45, 0.35), (0.2, 0.3, 0.4)),        # far, fogged
    ((1.0, 1.0, 1.0, 0.0), 300.0, (0.55, 0.45, 0.35), (0.2, 0.3, 0.4)),                 # the clear ground
)

# The fog lines: (the distance from the eye along FOG_RAY, the transmittance texel at s, the in-scatter texel at a).
FOG_EYE = (0x42A424EA, 0x431D56DA, 0x4248AECD)      # vertex c47.xyz of capture 155547
FOG_C110 = (0xBF54D061, 0x3DB936DD, 0x3F0C64A0, 0x00000000)
FOG_C111 = (0x41988BBB, 0x44ED8000, 0x3F800000, 0x3A03126F)
FOG_RAY = (-0.6, 0.64, 0.48)
FOG = (
    (10.0, (0.9, 0.8, 0.7), (0.3, 0.4, 0.5)),       # before the start (19.07 units): no table read
    (19.0, (0.9, 0.8, 0.7), (0.3, 0.4, 0.5)),       # just before it
    (19.5, (0.99, 0.98, 0.97), (0.3, 0.4, 0.5)),    # just begun
    (60.0, (0.9, 0.8, 0.7), (0.3, 0.4, 0.5)),
    (350.0, (0.6, 0.5, 0.4), (0.31, 0.38, 0.45)),
    (596.0, (0.45, 0.35, 0.25), (0.33, 0.39, 0.45)),
)


def _floats(t):
    return tuple(from_bits(b) for b in t)


def position_pin(row):
    """((corner, word, registers as floats), (oPos, o0, o1)) of a CAPTURED or MADE tuple: the single-precision
    reference."""
    corner, word, regs = row
    c = {reg: list(_floats(regs[i])) for i, reg in enumerate(REG)}
    out = lf.impostor_position(corner, _floats(word), c, single=True)
    return (corner, _floats(word), tuple(_floats(r) for r in regs)), (tuple(out["pos"]), tuple(out["place"]), tuple(out["uv"]))


def pixel_pin(row):
    """((texel, place, eye, c12, c29, c31.x, o2, o3, c20.w), (the distance, oC0.rgb, the alpha))."""
    texel, distance, o2, o3 = row
    eye = _floats(PIXEL_C19)[:3]
    place = tuple(f32(eye[i] + distance * PIXEL_RAY[i]) for i in range(3))
    texel, o2, o3 = tuple(f32(v) for v in texel), tuple(f32(v) for v in o2), tuple(f32(v) for v in o3)
    c12, c29, c31x, c20w = _floats(PIXEL_C12), _floats(PIXEL_C29), from_bits(PIXEL_C31X), from_bits(PIXEL_C20W)
    d = lf.impostor_distance(place, eye)
    colour, alpha = lf.impostor_pixel(texel, d, c12, c29, c31x)
    return (texel, place, eye, c12, c29, c31x, o2, o3, c20w), (d, tuple(lf.impostor_out(colour, o2, o3, c20w)), alpha)


def fog_pin(row):
    """((place, eye, c110, c111, the transmittance texel, the in-scatter texel), (runs, (a, s, t), o2, o3))."""
    distance, transmit, scatter = row
    eye, c110, c111 = _floats(FOG_EYE), _floats(FOG_C110), _floats(FOG_C111)
    place = tuple(f32(eye[i] + distance * FOG_RAY[i]) for i in range(3))
    transmit, scatter = tuple(f32(v) for v in transmit), tuple(f32(v) for v in scatter)
    coords = lf.fog_coords(place, eye, c110, c111)
    o2, o3 = lf.impostor_fog(place, eye, c110, c111, lambda s: list(transmit), lambda a, t: list(scatter))
    return (place, eye, c110, c111, transmit, scatter), (coords is not None, coords or (0.0, 0.0, 0.0), tuple(o2), tuple(o3))


def _b(v):
    return '0x%08Xu' % bits(v)


def _v(values):
    return '{' + ', '.join(_b(v) for v in values) + '}'


def _regs(regs):
    return '{' + ', '.join(_v(r) for r in regs) + '}'


def lines():
    """The pins as lines of a C++ table (every number a float's bits)."""
    out = ['// impostor position pins: the corner, the word (x, y, z, s), the fourteen vertex registers (c8, c46, c47, c72 to '
           'c75, c110, c111, c113 to c117), then oPos, o0 (the raised place) and o1 (light_formulas.impostor_position, '
           'single=True: the single-precision reference of ledger ruling 26)']
    for k, row in enumerate(CAPTURED + MADE):
        (corner, word, regs), (pos, place, uv) = position_pin(row)
        if not all(math.isfinite(v) for v in pos + place + uv):
            raise ValueError('impostor_pins: position line %d is no number' % k)
        out.append('    {%d, %s, %s, %s, %s, %s},  // %s' % (corner, _v(word), _regs(regs), _v(pos), _v(place), _v(uv),
                                                              'captured' if k < len(CAPTURED) else 'made'))
    out.append('// impostor pixel pins: the texel (r, g, b, alpha as the shader receives it), the raised place, the eye (pixel '
               'c19.xyz), c12, c29, c31.x, o2, o3, c20.w, then the distance, oC0.rgb and the alpha (light_formulas.'
               'impostor_distance, impostor_pixel, impostor_out; double rounded to single)')
    for row in PIXEL:
        (texel, place, eye, c12, c29, c31x, o2, o3, c20w), (d, rgb, alpha) = pixel_pin(row)
        out.append('    {%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s},' % (
            _v(texel), _v(place), _v(eye), _v(c12), _v(c29), _b(c31x), _v(o2), _v(o3), _b(c20w), _b(d), _v(rgb), _b(alpha)))
    out.append('// impostor fog pins: the raised place, the eye (vertex c47.xyz), c110, c111, the transmittance texel read at s, '
               'the in-scatter texel read at a, then whether the block runs (1 or 0), the coordinates (a, s, t) (light_formulas.'
               'fog_coords; 0 where it does not run), o2 and o3 (light_formulas.impostor_fog; double rounded to single)')
    for row in FOG:
        (place, eye, c110, c111, transmit, scatter), (runs, coords, o2, o3) = fog_pin(row)
        out.append('    {%s, %s, %s, %s, %s, %s, %d, %s, %s, %s},' % (
            _v(place), _v(eye), _v(c110), _v(c111), _v(transmit), _v(scatter), 1 if runs else 0, _v(coords), _v(o2), _v(o3)))
    return out


INC_SECTIONS = {"impostor position pins": "POSITION", "impostor pixel pins": "PIXEL", "impostor fog pins": "FOG"}


def inc_lines():
    """tests/native/impostor_pins.inc: lines() with each section under a macro of its own."""
    out = ['// Generated by tools/xdk_sigmatch/impostor_pins.py --inc (distant trees, plan Task 6). Do not edit: regenerate it.',
           '// The pins of the impostor mirrors: every number is the bits of a float. A test defines one of',
           '// FABLE2_IMPOSTOR_PINS_POSITION, _PIXEL, _FOG and includes the file inside the braces of a table of that section.']
    open_ = False
    for line in lines():
        if line.startswith('// '):
            if open_:
                out.append('#endif')
            out.append('#ifdef FABLE2_IMPOSTOR_PINS_' + INC_SECTIONS[line[3:line.index(':')]])
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
