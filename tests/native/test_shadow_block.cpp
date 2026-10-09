// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/shadow_block.h"
#include "../../src/native/capture/xdk_layout.h"
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

using namespace fable2::native::capture;

static const float kNaN = std::numeric_limits<float>::quiet_NaN();
static const float kInf = std::numeric_limits<float>::infinity();
static const float kHalf = 0.5f / 1024.0f;

// The numbers of tests/test_shadow_matrices.py (light(), world(), map_rows()): an orthographic
// light matrix of the shape the bridge capture shows, whose origin lies on a whole texel.
static void Light(float m[4][4], float tx = -6.732423f, float ty = -10.841799f, float scale = 1.0f) {
  const float v[4][4] = {{0.052114f * scale, 0.0f, 0.041577f * scale, tx},
                         {0.000898f * scale, 0.066651f * scale, -0.001126f * scale, ty},
                         {0.005422f, -0.000188f, -0.006796f, 0.737847f},
                         {0.0f, 0.0f, 0.0f, 1.0f}};
  std::memcpy(m, v, sizeof(v));
}
static void World(float w[3][4], float angle, float tx, float ty, float tz, float s = 1.0f) {
  const float c = std::cos(angle) * s, n = std::sin(angle) * s;
  const float v[3][4] = {{c, 0.0f, n, tx}, {0.0f, s, 0.0f, ty}, {-n, 0.0f, c, tz}};
  std::memcpy(w, v, sizeof(v));
}
// a (4 x 4) times the affine matrix whose first three rows are w.
static void Mul(const float a[4][4], const float w[3][4], float out[4][4]) {
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      double sum = j == 3 ? a[i][3] : 0.0;
      for (int k = 0; k < 3; ++k) sum += double(a[i][k]) * w[k][j];
      out[i][j] = float(sum);
    }
  }
}
// c36..c39 (or c40..c43) of a light matrix: texture space with the half texel of sign su, sv.
static void MapRows(const float l[4][4], int su, int sv, float out[4][4]) {
  for (int j = 0; j < 4; ++j) {
    out[0][j] = 0.5f * l[0][j] + (0.5f + su * kHalf) * l[3][j];
    out[1][j] = -0.5f * l[1][j] + (0.5f + sv * kHalf) * l[3][j];
    out[2][j] = l[2][j];
    out[3][j] = l[3][j];
  }
}
static double MaxDiff(const float a[4][4], const float b[4][4]) {
  double d = 0;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) d = std::fmax(d, std::fabs(double(a[i][j]) - b[i][j]));
  }
  return d;
}
// Every float of the block zero and the flag clear (the padding after the flag is not compared).
static bool AllZero(const ShadowBlock& b) {
  static const unsigned char zero[offsetof(ShadowBlock, valid)] = {};
  return !b.valid && std::memcmp(&b, zero, sizeof(zero)) == 0;
}

struct Registers {
  float reg[256][4] = {};
  bool have[256] = {};
};
// The pixel registers of a frame at the bridge: c12 = (20, 35, 2, 0.486), c13 = c14 = 1 / 1024,
// c36..c39 and c40..c43 the two maps' rows.
static Registers Good() {
  Registers r;
  float near[4][4], far[4][4], rows[4][4];
  Light(near);
  Light(far, -3.123047f, -4.865234f, 30.0f / 70.0f);
  const float c12[4] = {20.0f, 35.0f, 2.0f, 0.486f};
  std::memcpy(r.reg[12], c12, sizeof(c12));
  r.reg[13][0] = r.reg[13][1] = r.reg[14][0] = r.reg[14][1] = 1.0f / 1024.0f;
  MapRows(near, 1, 1, rows);
  std::memcpy(r.reg[36], rows, sizeof(rows));
  MapRows(far, 1, 1, rows);
  std::memcpy(r.reg[40], rows, sizeof(rows));
  for (uint32_t i : kShadowBlockRegisters) r.have[i] = true;
  return r;
}

static int BlockTests() {
  if (sizeof(ShadowBlock) != 300) return 1;
  // The registers the block is made of: c12, c13, c14 and the eight rows.
  {
    const uint32_t expect[] = {12, 13, 14, 36, 37, 38, 39, 40, 41, 42, 43};
    if (sizeof(kShadowBlockRegisters) != sizeof(expect) || std::memcmp(kShadowBlockRegisters, expect, sizeof(expect)) != 0) return 2;
  }
  Registers g = Good();
  ShadowBlock b = MakeShadowBlock(g.reg, g.have);
  if (!b.valid) return 3;
  if (std::memcmp(b.world_to_map[0], g.reg[36], 64) != 0 || std::memcmp(b.world_to_map[1], g.reg[40], 64) != 0) return 4;
  if (b.split[0] != 20.0f || b.split[1] != 35.0f || b.split[2] != 2.0f || b.split[3] != 0.486f) return 5;
  if (b.texel[0] != 1.0f / 1024.0f || b.texel[1] != 1.0f / 1024.0f) return 6;
  // The registers alone prove nothing of the passes: no clip matrix and no bias yet.
  for (int c = 0; c < 2; ++c) {
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        if (b.world_to_clip[c][i][j] != 0.0f) return 7;
      }
    }
    if (b.bias[c][0] != 0.0f || b.bias[c][1] != 0.0f) return 8;
  }
  // The near texel is c13.x and the far one c14.x, each its own.
  g.reg[14][0] = 1.0f / 2048.0f;
  b = MakeShadowBlock(g.reg, g.have);
  if (!b.valid || b.texel[0] != 1.0f / 1024.0f || b.texel[1] != 1.0f / 2048.0f) return 9;

  // No registers at all.
  if (!AllZero(MakeShadowBlock(nullptr, g.have)) || !AllZero(MakeShadowBlock(g.reg, nullptr))) return 10;
  // Every register must have been read, and an invalid block is all zero.
  for (uint32_t r : kShadowBlockRegisters) {
    g = Good();
    g.have[r] = false;
    if (!AllZero(MakeShadowBlock(g.reg, g.have))) return 11;
    // ... and be finite in every component.
    for (int c = 0; c < 4; ++c) {
      for (float bad : {kNaN, kInf, -kInf}) {
        g = Good();
        g.reg[r][c] = bad;
        if (!AllZero(MakeShadowBlock(g.reg, g.have))) return 12;
      }
    }
  }
  // A register the block does not read may be anything.
  g = Good();
  g.have[0] = false;
  g.reg[44][0] = kNaN;
  if (!MakeShadowBlock(g.reg, g.have).valid) return 13;

  auto with_split = [](int c, float v) {
    Registers r = Good();
    r.reg[12][c] = v;
    return MakeShadowBlock(r.reg, r.have);
  };
  // split[0] > 0.
  if (!AllZero(with_split(0, 0.0f)) || !AllZero(with_split(0, -20.0f))) return 20;
  if (!with_split(0, 0.001f).valid) return 21;
  // split[1] > split[0].
  if (!AllZero(with_split(1, 20.0f)) || !AllZero(with_split(1, 19.0f))) return 22;
  if (!with_split(1, 20.5f).valid) return 23;
  // split[2] > 0: the blend divides by it.
  if (!AllZero(with_split(2, 0.0f)) || !AllZero(with_split(2, -2.0f))) return 24;
  // split[3] in [0, 1], nothing tighter: it moves with the time of day (0.46 to 0.51 in the captures).
  if (!AllZero(with_split(3, -0.001f)) || !AllZero(with_split(3, 1.001f))) return 25;
  for (float w : {0.0f, 0.46f, 0.512f, 1.0f}) {
    if (!with_split(3, w).valid) return 26;
  }
  // texel in (0, 1], each of the two.
  for (uint32_t r : {13u, 14u}) {
    for (float bad : {0.0f, -1.0f / 1024.0f, 1.5f}) {
      g = Good();
      g.reg[r][0] = bad;
      if (!AllZero(MakeShadowBlock(g.reg, g.have))) return 27;
    }
    g = Good();
    g.reg[r][0] = 1.0f;
    if (!MakeShadowBlock(g.reg, g.have).valid) return 28;
  }
  return 0;
}

static int MatrixTests() {
  float near[4][4], w[3][4], c[4][4], m[4][4];
  Light(near);
  // The pair of the Python test: c0..c3 = light * world; the pass's matrix is the light's.
  for (int i = 0; i < 4; ++i) {
    World(w, 0.3f * i, 80.0f + i, 150.0f, 50.0f - i, 1.0f + 0.1f * i);
    Mul(near, w, c);
    if (!PassMatrix(c, w, m)) return 40;
    if (MaxDiff(m, near) > 1e-5) {
      std::printf("FAIL: PassMatrix off by %g\n", MaxDiff(m, near));
      return 41;
    }
  }
  // Not a multiple or a transpose of it: the translation column is where it belongs.
  if (std::fabs(m[0][3] - -6.732423f) > 1e-5 || std::fabs(m[3][3] - 1.0f) > 1e-6 || m[3][0] != 0.0f) return 42;
  // A singular world block: all zero, a zero row, two equal rows.
  float keep[4][4];
  std::memcpy(keep, m, sizeof(m));
  float dead[3][4] = {};
  if (PassMatrix(c, dead, m)) return 43;
  World(w, 0.2f, 1.0f, 2.0f, 3.0f);
  std::memset(w[1], 0, sizeof(w[1]));
  if (PassMatrix(c, w, m)) return 44;
  World(w, 0.2f, 1.0f, 2.0f, 3.0f);
  std::memcpy(w[2], w[0], sizeof(w[0]));
  if (PassMatrix(c, w, m)) return 45;
  // Nearly singular counts as singular (the determinant against the block's size cubed, 1e-12):
  // the inverse would be finite, and of no use.
  {
    const float thin[3][4] = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1e-13f, 0.0f}};
    if (PassMatrix(c, thin, m)) return 54;
    // ... but not a block that is small as a whole.
    const float small[3][4] = {{1e-10f, 0.0f, 0.0f, 0.0f}, {0.0f, 1e-10f, 0.0f, 0.0f}, {0.0f, 0.0f, 1e-10f, 0.0f}};
    float out[4][4];
    if (!PassMatrix(c, small, out)) return 55;
  }
  // A failure writes nothing.
  if (std::memcmp(keep, m, sizeof(m)) != 0) return 46;
  // Non-finite inputs, in either operand and in the translation.
  for (float bad : {kNaN, kInf, -kInf}) {
    World(w, 0.2f, 1.0f, 2.0f, 3.0f);
    w[1][1] = bad;
    if (PassMatrix(c, w, m)) return 47;
    World(w, 0.2f, 1.0f, 2.0f, 3.0f);
    w[2][3] = bad;
    if (PassMatrix(c, w, m)) return 48;
    World(w, 0.2f, 1.0f, 2.0f, 3.0f);
    float cb[4][4];
    std::memcpy(cb, c, sizeof(c));
    cb[3][2] = bad;
    if (PassMatrix(cb, w, m)) return 49;
  }
  if (std::memcmp(keep, m, sizeof(m)) != 0) return 50;
  // A product that does not fit a float is no matrix either.
  World(w, 0.0f, 0.0f, 0.0f, 0.0f, 1e-12f);
  float big[4][4];
  Light(big);
  big[0][0] = 3e38f;
  if (PassMatrix(big, w, m)) return 51;
  // No pointers.
  World(w, 0.2f, 1.0f, 2.0f, 3.0f);
  if (PassMatrix(nullptr, w, m) || PassMatrix(c, nullptr, m) || PassMatrix(c, w, nullptr)) return 52;
  // A small, well-formed world block (a scale of 0.01) is not singular.
  World(w, 0.7f, 5.0f, 6.0f, 7.0f, 0.01f);
  Mul(near, w, c);
  if (!PassMatrix(c, w, m) || MaxDiff(m, near) > 1e-3) return 53;
  return 0;
}

static int AgreeTests() {
  const float texel = 1.0f / 1024.0f;
  float near[4][4], far[4][4], rows[4][4];
  Light(near);
  Light(far, -3.123047f, -4.865234f, 30.0f / 70.0f);
  // The half texel in either sign, per axis, and none: all accepted, and the fit says which.
  const int signs[] = {1, -1, 0};
  for (int su : signs) {
    for (int sv : signs) {
      MapRows(near, su, sv, rows);
      MapFit fit;
      if (!MapResidual(near, rows, texel, &fit)) return 60;
      if (fit.sign_u != su || fit.sign_v != sv) {
        std::printf("FAIL: half texel (%d, %d) read as (%d, %d)\n", su, sv, fit.sign_u, fit.sign_v);
        return 61;
      }
      if (fit.residual > 1e-6) return 62;
      if (!PassAgrees(near, rows, texel, 1e-3f)) return 63;
    }
  }
  // What the captures hold: plus in u and v (frame-map section 15), for both maps.
  MapRows(far, 1, 1, rows);
  MapFit fit;
  if (!MapResidual(far, rows, texel, &fit) || fit.sign_u != 1 || fit.sign_v != 1 || !PassAgrees(far, rows, texel, 1e-3f)) return 65;
  // The snap and the half texel on the same numbers: the light's origin is a whole texel
  // (clip * 512), so the registers put it on a texel centre (u * 1024 = whole + 0.5).
  const float (*lights[2])[4] = {near, far};
  for (const float (*l)[4] : lights) {
    MapRows(l, 1, 1, rows);
    for (int axis = 0; axis < 2; ++axis) {
      const double origin = double(l[axis][3]) * 512.0;
      if (std::fabs(origin - std::round(origin)) > 0.002) return 66;
      const double centre = double(rows[axis][3]) * 1024.0 - 0.5;
      if (std::fabs(centre - std::round(centre)) > 0.004) {
        std::printf("FAIL: axis %d origin at %.6f texels in the registers\n", axis, double(rows[axis][3]) * 1024.0);
        return 67;
      }
    }
  }
  // A wrong matrix: the Python test's (tx -6.5 for -6.732423, a deviation of 0.23), the far one
  // for the near one, a wrong depth row, a wrong w row.
  float wrong[4][4];
  MapRows(near, 1, 1, rows);
  Light(wrong, -6.5f);
  if (PassAgrees(wrong, rows, texel, 1e-3f)) return 70;
  // (0.5 * 0.232423 in u, less the texel the best sign takes off it)
  if (!MapResidual(wrong, rows, texel, &fit) || std::fabs(fit.residual - (0.5 * 0.232423 - 1.0 / 1024.0)) > 1e-5 ||
      fit.part[0] != fit.residual) {
    return 71;
  }
  if (PassAgrees(far, rows, texel, 1e-3f)) return 72;
  Light(wrong);
  wrong[2][3] += 0.01f;
  if (PassAgrees(wrong, rows, texel, 1e-3f)) return 73;
  if (!MapResidual(wrong, rows, texel, &fit) || std::fabs(fit.part[2] - 0.01) > 1e-5 || fit.part[0] > 1e-6) return 74;
  Light(wrong);
  wrong[3][3] = 1.01f;
  if (PassAgrees(wrong, rows, texel, 1e-3f)) return 75;
  // The w row is compared for itself: a pass whose w row is another one, with x and y made to
  // give the same u and v all the same.
  Light(wrong);
  wrong[3][0] = 0.01f;
  wrong[0][0] -= 2.0f * (0.5f + kHalf) * 0.01f;
  wrong[1][0] += 2.0f * (0.5f + kHalf) * 0.01f;
  if (!MapResidual(wrong, rows, texel, &fit) || fit.part[0] > 1e-6 || fit.part[1] > 1e-6 || std::fabs(fit.part[3] - 0.01) > 1e-6) return 86;
  if (PassAgrees(wrong, rows, texel, 1e-3f)) return 87;
  // The tolerance is the limit: 0.002 off passes at 1e-2 and fails at 1e-3.
  Light(wrong, -6.732423f + 0.004f);
  if (!PassAgrees(wrong, rows, texel, 1e-2f) || PassAgrees(wrong, rows, texel, 1e-3f)) return 76;
  // Non-finite anything, a texel outside (0, 1], a tolerance that is none: no agreement.
  for (float bad : {kNaN, kInf, -kInf}) {
    Light(wrong);
    wrong[1][2] = bad;
    if (PassAgrees(wrong, rows, texel, 1e-3f) || MapResidual(wrong, rows, texel, &fit)) return 77;
    float bad_rows[4][4];
    std::memcpy(bad_rows, rows, sizeof(rows));
    bad_rows[3][0] = bad;
    if (PassAgrees(near, bad_rows, texel, 1e-3f)) return 78;
    if (PassAgrees(near, rows, bad, 1e-3f) || PassAgrees(near, rows, texel, bad)) return 79;
  }
  if (PassAgrees(near, rows, 0.0f, 1e-3f) || PassAgrees(near, rows, -texel, 1e-3f) || PassAgrees(near, rows, 1.5f, 1e-3f)) return 80;
  if (PassAgrees(near, rows, texel, -1.0f)) return 81;
  if (PassAgrees(nullptr, rows, texel, 1e-3f) || PassAgrees(near, nullptr, texel, 1e-3f)) return 82;
  // A pass matrix of zeros is not the registers'.
  float zero[4][4] = {};
  if (PassAgrees(zero, rows, texel, 1e-3f)) return 83;
  // What the tolerance of 1e-3 cannot tell: a map moved by one texel (1 / 1024 = 0.00098 in
  // texture space), since the half texel is fitted in either sign. Three texels are told.
  float moved[4][4];
  Light(moved, -6.732423f + 2.0f / 1024.0f);
  if (!PassAgrees(moved, rows, texel, 1e-3f)) return 84;
  Light(moved, -6.732423f + 6.0f / 1024.0f);
  if (PassAgrees(moved, rows, texel, 1e-3f)) return 85;
  return 0;
}

static int BiasTests() {
  // The enables are PA_SU_SC_MODE_CNTL bits 11 and 12.
  if (kShadowBiasFrontEnable != xdk::kSuScPolyOffsetFrontEnable || kShadowBiasBackEnable != xdk::kSuScPolyOffsetBackEnable) return 100;
  auto bits = [](float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
  };
  // The near pass's words (frame-map section 15, "Bias"): scale -32, offset -0.001, both faces.
  const uint32_t near_words[4] = {0xC2000000u, 0xBA83126Fu, 0xC2000000u, 0xBA83126Fu};
  DrawBias d = ShadowDrawBias(0x00219806u, near_words);
  if (!d.biased || d.scale != -32.0f || std::fabs(d.offset - -0.001f) > 1e-9f) return 101;
  // Without the enable bits a draw has no bias, whatever the registers hold.
  d = ShadowDrawBias(0x00218000u & ~0x1800u, near_words);
  if (d.biased || d.scale != 0.0f || d.offset != 0.0f) return 102;
  d = ShadowDrawBias(0x00000006u, nullptr);
  if (d.biased) return 103;
  // One face: its pair.
  const uint32_t two[4] = {bits(-32.0f), bits(-0.001f), bits(-80.0f), bits(-0.005f)};
  d = ShadowDrawBias(kShadowBiasFrontEnable, two);
  if (!d.biased || d.scale != -32.0f || d.offset != -0.001f) return 104;
  d = ShadowDrawBias(kShadowBiasBackEnable, two);
  if (!d.biased || d.scale != -80.0f || d.offset != -0.005f) return 105;
  // Both faces with two pairs: no single bias; words that were not read: none either.
  d = ShadowDrawBias(kShadowBiasFrontEnable | kShadowBiasBackEnable, two);
  if (!d.biased || !std::isnan(d.scale) || !std::isnan(d.offset)) return 106;
  d = ShadowDrawBias(kShadowBiasFrontEnable, nullptr);
  if (!d.biased || !std::isnan(d.scale)) return 107;

  // A pass: every biased record must hold the same finite pair.
  PassBias p;
  if (!p.ok() || p.biased != 0 || p.scale != 0.0f || p.offset != 0.0f) return 110;
  p.Add(false, 123.0f, 4.0f);  // a draw without the enable bits says nothing
  if (!p.ok() || p.biased != 0 || p.scale != 0.0f) return 111;
  p.Add(true, -32.0f, -0.001f);
  p.Add(true, -32.0f, -0.001f);
  p.Add(false, kNaN, kNaN);
  if (!p.ok() || p.biased != 2 || p.scale != -32.0f || p.offset != -0.001f) return 112;
  PassBias q = p;
  q.Add(true, -80.0f, -0.001f);
  if (q.ok()) return 113;
  q = p;
  q.Add(true, -32.0f, -0.005f);
  if (q.ok()) return 114;
  for (float bad : {kNaN, kInf, -kInf}) {
    q = p;
    q.Add(true, bad, -0.001f);
    if (q.ok()) return 115;
    q = PassBias{};
    q.Add(true, -32.0f, bad);  // the first biased record too
    if (q.ok()) return 116;
    q.Add(true, -32.0f, -0.001f);  // and nothing mends it
    if (q.ok()) return 117;
  }
  return 0;
}

static int FinishTests() {
  const Registers g = Good();
  const ShadowBlock reg = MakeShadowBlock(g.reg, g.have);
  ShadowPassProof pass[2];
  Light(pass[0].matrix);
  Light(pass[1].matrix, -3.123047f, -4.865234f, 30.0f / 70.0f);
  pass[0].have_matrix = pass[1].have_matrix = true;
  pass[0].bias.Add(true, -32.0f, -0.001f);
  pass[1].bias.Add(true, -80.0f, -0.005f);
  ShadowBlockReason why = ShadowBlockReason::kOff;
  ShadowBlock b = FinishShadowBlock(reg, pass, 1e-3f, &why);
  if (!b.valid || why != ShadowBlockReason::kNone) return 130;
  if (std::memcmp(b.world_to_map, reg.world_to_map, sizeof(reg.world_to_map)) != 0) return 131;
  if (std::memcmp(b.world_to_clip[0], pass[0].matrix, 64) != 0 || std::memcmp(b.world_to_clip[1], pass[1].matrix, 64) != 0) return 132;
  if (b.bias[0][0] != -32.0f || b.bias[0][1] != -0.001f || b.bias[1][0] != -80.0f || b.bias[1][1] != -0.005f) return 133;
  if (b.split[3] != 0.486f || b.texel[1] != 1.0f / 1024.0f) return 134;
  // A pass none of whose records has the offset on: drawn without bias, and valid.
  ShadowPassProof plain[2] = {pass[0], pass[1]};
  plain[1].bias = PassBias{};
  b = FinishShadowBlock(reg, plain, 1e-3f, &why);
  if (!b.valid || b.bias[1][0] != 0.0f || b.bias[1][1] != 0.0f || b.bias[0][0] != -32.0f) return 135;
  if (!FinishShadowBlock(reg, pass, 1e-3f, nullptr).valid) return 136;  // (the reason is optional)

  // Registers that were not read or failed their rules.
  b = FinishShadowBlock(ShadowBlock{}, pass, 1e-3f, &why);
  if (!AllZero(b) || why != ShadowBlockReason::kRegisters) return 140;
  // A cascade without a record that gave its matrix (a pass with no readable draw, one pass only).
  for (int c = 0; c < 2; ++c) {
    ShadowPassProof p[2] = {pass[0], pass[1]};
    p[c].have_matrix = false;
    b = FinishShadowBlock(reg, p, 1e-3f, &why);
    if (!AllZero(b) || why != ShadowBlockReason::kNoPass) return 141;
  }
  // The proof: a pass whose own matrix is another one (the two exchanged, one moved by four
  // texels, one all zero, one not finite).
  {
    ShadowPassProof p[2] = {pass[1], pass[0]};
    b = FinishShadowBlock(reg, p, 1e-3f, &why);
    if (!AllZero(b) || why != ShadowBlockReason::kProof) return 142;
    for (int c = 0; c < 2; ++c) {
      ShadowPassProof m[2] = {pass[0], pass[1]};
      m[c].matrix[0][3] += 8.0f / 1024.0f;
      b = FinishShadowBlock(reg, m, 1e-3f, &why);
      if (!AllZero(b) || why != ShadowBlockReason::kProof) return 143;
      m[c] = pass[c];
      std::memset(m[c].matrix, 0, sizeof(m[c].matrix));
      if (FinishShadowBlock(reg, m, 1e-3f, &why).valid || why != ShadowBlockReason::kProof) return 144;
      m[c] = pass[c];
      m[c].matrix[2][1] = kNaN;
      if (FinishShadowBlock(reg, m, 1e-3f, &why).valid || why != ShadowBlockReason::kProof) return 145;
    }
  }
  // The bias: records of a pass that disagree, a pair that is not finite.
  for (int c = 0; c < 2; ++c) {
    ShadowPassProof p[2] = {pass[0], pass[1]};
    p[c].bias.Add(true, -1.0f, -0.001f);
    b = FinishShadowBlock(reg, p, 1e-3f, &why);
    if (!AllZero(b) || why != ShadowBlockReason::kBias) return 146;
    p[c] = pass[c];
    p[c].bias.Add(true, kNaN, -0.001f);
    if (FinishShadowBlock(reg, p, 1e-3f, &why).valid || why != ShadowBlockReason::kBias) return 147;
  }
  // The reasons' names, as the log writes them.
  if (std::strcmp(ShadowBlockReasonName(ShadowBlockReason::kNone), "none") != 0 ||
      std::strcmp(ShadowBlockReasonName(ShadowBlockReason::kOff), "off") != 0 ||
      std::strcmp(ShadowBlockReasonName(ShadowBlockReason::kRegisters), "registers") != 0 ||
      std::strcmp(ShadowBlockReasonName(ShadowBlockReason::kNoPass), "no-pass") != 0 ||
      std::strcmp(ShadowBlockReasonName(ShadowBlockReason::kProof), "proof") != 0 ||
      std::strcmp(ShadowBlockReasonName(ShadowBlockReason::kBias), "bias") != 0) {
    return 148;
  }
  if (std::strcmp(kShadowBlockReason, "shadow-block") != 0 || std::strcmp(kShadowPassExtraReason, "shadow-pass-extra") != 0) return 149;
  return 0;
}

int main() {
  for (int (*test)() : {BlockTests, MatrixTests, AgreeTests, BiasTests, FinishTests}) {
    if (const int code = test()) {
      std::printf("FAIL: shadow block case %d\n", code);
      return 1;
    }
  }
  std::puts("PASS: shadow block rules, pass matrix, texture-space agreement with the half texel and the snap, bias, the frame's proof");
  return 0;
}
