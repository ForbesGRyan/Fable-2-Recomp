#pragma once

// The frame's shadow block (pure): what the native pass needs of the game's two
// sun shadow maps (frame-map section 15, "Shadows"), and the proof that the
// two sources of the light matrices agree. The game draws a near and a far
// map (1024 x 1024 depth, orthographic) before the main scene; the scene's
// pixel registers c36..c39 and c40..c43 hold world to map texture space for
// them, c12 the split distances, c13.x and c14.x the maps' texel sizes.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace fable2::native::capture {

// world_to_map: per map (0 near, 1 far) the rows u, v, depth, w of pixel
//   c36..c39 / c40..c43, as the game's mask shader reads them: texture space
//   with the half texel in (u = 0.5 x + (0.5 + h) w, v = -0.5 y + (0.5 + h) w,
//   depth = z; h = half a texel, plus in every capture).
// world_to_clip: per map the light matrix of the pass's own draws (rows x, y,
//   z, w; PassMatrix of the pass's first record that gives one), which the
//   block's proof held against world_to_map (PassAgrees). What the casters were
//   drawn with: clip x, y in [-1, 1], depth = z under the reversed range.
// split: c12 = (begin of the blend to the far map, begin of the fade, width of
//   the blend, the value faded to); (20, 35, 2, w) in every capture, w moving
//   with the time of day (seen from 0.46 to 0.53).
// texel: (c13.x, c14.x), the near and the far map's texel size.
// bias: per map the polygon offset of the pass's draws as the device held it
//   (registers 0x2380 / 0x2381): scale (the slope scale times 16), offset (in
//   depth units); (0, 0) for a pass none of whose records has the offset on.
//   A record without the enable bits is drawn without bias whatever this says
//   (ShadowCaster::biased in draw_record.h).
// An invalid block is all zero: nothing of it may be used.
struct ShadowBlock {
  float world_to_map[2][4][4];
  float world_to_clip[2][4][4];
  float split[4];
  float texel[2];
  float bias[2][2];
  bool valid;
};
// 75 floats and the flag: one is copied into each frame's scene (FrameScene::shadow).
static_assert(sizeof(ShadowBlock) == 300, "ShadowBlock grew or shrank: the documents state its size");

// The pixel registers MakeShadowBlock reads. None of them is a register a
// LoadAluConstants call can name (light_block.h, kBufferLoadRanges): the
// device's bank holds their value at any scene draw.
inline constexpr uint32_t kShadowBlockRegisters[] = {12, 13, 14, 36, 37, 38, 39, 40, 41, 42, 43};

// Reason strings of the shadows (the plan's): a frame whose shadow block was
// not read or failed its proof; a third or later shadow pass in a frame.
inline constexpr const char* kShadowBlockReason = "shadow-block";
inline constexpr const char* kShadowPassExtraReason = "shadow-pass-extra";

// The registers' part of the block from the pixel registers by number: `reg[r]`
// is register r, `have[r]` whether it was read. Valid only when every register
// of kShadowBlockRegisters was read and is finite in every component,
// split[0] > 0, split[1] > split[0], split[2] > 0, split[3] in [0, 1] and both
// texels in (0, 1]. world_to_clip and bias stay zero: they come from the
// passes (FinishShadowBlock). An invalid block is all zero.
inline ShadowBlock MakeShadowBlock(const float (*reg)[4], const bool* have) {
  if (!reg || !have) return ShadowBlock{};
  for (uint32_t r : kShadowBlockRegisters) {
    if (!have[r]) return ShadowBlock{};
    for (int c = 0; c < 4; ++c) {
      if (!std::isfinite(reg[r][c])) return ShadowBlock{};
    }
  }
  ShadowBlock b{};
  std::memcpy(b.world_to_map[0], reg[36], sizeof(b.world_to_map[0]));
  std::memcpy(b.world_to_map[1], reg[40], sizeof(b.world_to_map[1]));
  std::memcpy(b.split, reg[12], sizeof(b.split));
  b.texel[0] = reg[13][0];
  b.texel[1] = reg[14][0];
  if (!(b.split[0] > 0.0f && b.split[1] > b.split[0] && b.split[2] > 0.0f)) return ShadowBlock{};
  if (!(b.split[3] >= 0.0f && b.split[3] <= 1.0f)) return ShadowBlock{};
  for (float t : b.texel) {
    if (!(t > 0.0f && t <= 1.0f)) return ShadowBlock{};
  }
  b.valid = true;
  return b;
}

// The light matrix a shadow draw implies: M = [c0..c3] * inverse([world; 0 0 0 1])
// for the vertex registers c0..c3 (`c0_3`, rows) and the world rows c4..c6
// (`world`), as tools/xdk_sigmatch/shadow_matrices.py computes it (row_matrix,
// inverse_affine: the same adjugate, the same test for a singular block,
// |det| <= 1e-12 * (largest entry)^3). In double, so that the product of floats
// loses nothing. False, with `out` untouched, when an input is not finite, the
// world block is singular, or the result does not fit a float.
// (A shader that builds the world position itself holds the matrix in c0..c3:
// the caller takes those as they are and does not call this.)
inline bool PassMatrix(const float c0_3[4][4], const float world[3][4], float out[4][4]) {
  if (!c0_3 || !world || !out) return false;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      if (!std::isfinite(c0_3[i][j]) || (i < 3 && !std::isfinite(world[i][j]))) return false;
    }
  }
  const double a[3] = {world[0][0], world[0][1], world[0][2]};
  const double b[3] = {world[1][0], world[1][1], world[1][2]};
  const double c[3] = {world[2][0], world[2][1], world[2][2]};
  const double cof[3][3] = {
      {b[1] * c[2] - b[2] * c[1], a[2] * c[1] - a[1] * c[2], a[1] * b[2] - a[2] * b[1]},
      {b[2] * c[0] - b[0] * c[2], a[0] * c[2] - a[2] * c[0], a[2] * b[0] - a[0] * b[2]},
      {b[0] * c[1] - b[1] * c[0], a[1] * c[0] - a[0] * c[1], a[0] * b[1] - a[1] * b[0]}};
  const double det = a[0] * cof[0][0] + a[1] * cof[1][0] + a[2] * cof[2][0];
  double scale = 0.0;
  for (int k = 0; k < 3; ++k) scale = std::fmax(scale, std::fmax(std::fabs(a[k]), std::fmax(std::fabs(b[k]), std::fabs(c[k]))));
  if (!std::isfinite(det) || scale == 0.0 || std::fabs(det) <= 1e-12 * scale * scale * scale) return false;
  double inv[4][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 1}};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) inv[i][j] = cof[i][j] / det;
    inv[i][3] = -(inv[i][0] * world[0][3] + inv[i][1] * world[1][3] + inv[i][2] * world[2][3]);
  }
  float m[4][4];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      double sum = 0.0;
      for (int k = 0; k < 4; ++k) sum += double(c0_3[i][k]) * inv[k][j];
      m[i][j] = float(sum);
      if (!std::isfinite(m[i][j])) return false;
    }
  }
  std::memcpy(out, m, sizeof(m));
  return true;
}

// How a pass's light matrix fits a map's four register rows (shadow_matrices.py,
// map_residual): the largest difference of the texture-space form of the matrix
// from the rows, per row (u, v, depth, w) and overall, with the half texel that
// fits best per axis: +1 plus, -1 minus, 0 none (tried in that order; the first
// of equals is kept, as there). plus_residual: the same largest difference with
// the half texel plus on both axes, which is what every captured pass holds
// (6,612 shadow rows, residual at most 9.95e-7) and what the frame's proof
// compares (PassAgrees): the best fit alone would take a map half a texel or
// one texel on for the registers' by choosing another sign.
struct MapFit {
  double residual = 0.0;
  double part[4] = {};
  int sign_u = 0;
  int sign_v = 0;
  double plus_residual = 0.0;
};

// The fit of `pass` (rows x, y, z, w) to `map_rows` (rows u, v, depth, w) for a
// map whose texel is `texel`: u = 0.5 x + (0.5 + h) w, v = -0.5 y + (0.5 + h) w,
// depth = z, w = w, h = +/- texel / 2 or 0. False, with `fit` untouched, when a
// value is not finite or the texel is not in (0, 1].
inline bool MapResidual(const float pass[4][4], const float map_rows[4][4], float texel, MapFit* fit) {
  if (!pass || !map_rows || !fit || !(texel > 0.0f && texel <= 1.0f)) return false;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      if (!std::isfinite(pass[i][j]) || !std::isfinite(map_rows[i][j])) return false;
    }
  }
  const double half = 0.5 * double(texel);
  MapFit f;
  const double k[2] = {0.5, -0.5};
  double plus = 0.0;
  for (int axis = 0; axis < 2; ++axis) {
    bool have = false;
    double best = 0.0;
    int best_sign = 0;
    for (int sign : {1, -1, 0}) {
      double r = 0.0;
      for (int j = 0; j < 4; ++j) {
        r = std::fmax(r, std::fabs(k[axis] * double(pass[axis][j]) + (0.5 + sign * half) * double(pass[3][j]) -
                                   double(map_rows[axis][j])));
      }
      if (sign == 1) plus = std::fmax(plus, r);
      if (!have || r < best) {
        have = true;
        best = r;
        best_sign = sign;
      }
    }
    f.part[axis] = best;
    (axis == 0 ? f.sign_u : f.sign_v) = best_sign;
  }
  for (int row = 2; row < 4; ++row) {
    for (int j = 0; j < 4; ++j) {
      f.part[row] = std::fmax(f.part[row], std::fabs(double(pass[row][j]) - double(map_rows[row][j])));
    }
  }
  f.residual = std::fmax(std::fmax(f.part[0], f.part[1]), std::fmax(f.part[2], f.part[3]));
  f.plus_residual = std::fmax(plus, std::fmax(f.part[2], f.part[3]));
  if (!std::isfinite(f.residual) || !std::isfinite(f.plus_residual)) return false;
  *fit = f;
  return true;
}

// Whether a pass's light matrix is the map's register rows within `tol`, with
// the half texel plus on both axes (MapFit::plus_residual; ruling after the
// review of Task 3: the minus sign and no half texel do not agree any more,
// whatever MapResidual's best fit says). Nothing that is not finite agrees
// with anything. The frame's proof uses kShadowProofTolerance (1e-4): a texel
// of a 1024-map is 0.00098 in texture space, so a map a tenth of a texel off,
// a depth row 5e-4 off or another frame's matrix is told.
inline bool PassAgrees(const float pass[4][4], const float map_rows[4][4], float texel, float tol) {
  MapFit fit;
  if (!(tol >= 0.0f) || !std::isfinite(tol) || !MapResidual(pass, map_rows, texel, &fit)) return false;
  return fit.plus_residual <= double(tol);
}

// PA_SU_SC_MODE_CNTL bits 11 and 12: the polygon offset applies to front and
// to back faces (xdk_layout.h, kSuScPolyOffsetFrontEnable / BackEnable).
inline constexpr uint32_t kShadowBiasFrontEnable = 1u << 11;
inline constexpr uint32_t kShadowBiasBackEnable = 1u << 12;

// The polygon offset of one draw: whether it has one, and its pair.
struct DrawBias {
  bool biased = false;
  float scale = 0.0f;   // PA_SU_POLY_OFFSET_*_SCALE as the device holds it (the slope scale times 16)
  float offset = 0.0f;  // PA_SU_POLY_OFFSET_*_OFFSET
};

// From a draw's PA_SU_SC_MODE_CNTL word and the bits of registers 0x2380..0x2383
// (front scale, front offset, back scale, back offset; null: not read). Neither
// enable bit: no bias, whatever the registers hold. Otherwise the pair of the
// enabled face; with both faces enabled the two pairs must be equal. Words that
// were not read, or two pairs for two faces, give a pair that is not finite, so
// that the pass's bias (PassBias) fails on it instead of taking a guess.
inline DrawBias ShadowDrawBias(uint32_t su_sc_mode, const uint32_t words[4]) {
  const bool front = (su_sc_mode & kShadowBiasFrontEnable) != 0, back = (su_sc_mode & kShadowBiasBackEnable) != 0;
  DrawBias d;
  if (!front && !back) return d;
  d.biased = true;
  const float nan = std::nanf("");
  d.scale = d.offset = nan;
  if (!words) return d;
  float f[4];
  std::memcpy(f, words, sizeof(f));
  if (front && back && !(f[0] == f[2] && f[1] == f[3])) return d;
  d.scale = front ? f[0] : f[2];
  d.offset = front ? f[1] : f[3];
  return d;
}

// The bias of one pass, from its recorded draws: every one that has the offset
// on must hold the same finite pair (frame-map section 15, "Bias": the bias
// belongs to the pass; near scale -32, offset -0.001; far -80, -0.005).
struct PassBias {
  uint32_t biased = 0;  // records with the offset on
  bool bad = false;     // two pairs, or one that is not finite
  float scale = 0.0f;
  float offset = 0.0f;

  void Add(bool draw_biased, float draw_scale, float draw_offset) {
    if (!draw_biased) return;
    if (!std::isfinite(draw_scale) || !std::isfinite(draw_offset)) {
      bad = true;
    } else if (!have_) {
      have_ = true;
      scale = draw_scale;
      offset = draw_offset;
    } else if (draw_scale != scale || draw_offset != offset) {
      bad = true;
    }
    ++biased;
  }
  bool ok() const { return !bad; }

 private:
  bool have_ = false;
};

// Why a frame has no shadow block (FrameScene::shadow_reason); kNone: it has one.
enum class ShadowBlockReason : uint8_t {
  kNone,
  kOff,        // no shadow records were taken this frame (the view, the light or the shadows off)
  kRegisters,  // the scene's registers were not read, or failed MakeShadowBlock's rules
  kNoPass,     // a map without a recorded draw that gives its light matrix
  kProof,      // a pass's own light matrix is not the registers' (PassAgrees)
  kBias,       // the records of a pass disagree on the polygon offset, or it is not finite
  kCount
};

inline const char* ShadowBlockReasonName(ShadowBlockReason r) {
  switch (r) {
    case ShadowBlockReason::kNone: return "none";
    case ShadowBlockReason::kOff: return "off";
    case ShadowBlockReason::kRegisters: return "registers";
    case ShadowBlockReason::kNoPass: return "no-pass";
    case ShadowBlockReason::kProof: return "proof";
    case ShadowBlockReason::kBias: return "bias";
    default: return "?";
  }
}

// What one pass gives the proof: the light matrix of its first record that
// gives one, and its records' bias.
struct ShadowPassProof {
  bool have_matrix = false;
  float matrix[4][4] = {};
  PassBias bias;
};

// The tolerance of the frame's proof: 1e-4 of texture space, a tenth of a texel
// of a 1024-map (ruling after Task 3; the spec said 1e-3, which is more than a
// texel, 0.00098). Two figures of the captures, both far below it: 9.95e-7 is
// the largest residual of a single shadow row's own matrix, over the 6,612
// shadow rows of plan Task 3's captures (what this proof compares: one
// record's matrix); 2.8e-7 is the largest residual of a pass's median matrix,
// over the 128 passes of plan Task 1's captures (shadow_matrices.py; frame-map
// section 15, "Passes"). All with the half texel plus on both axes, which is
// the only fit that agrees (PassAgrees).
inline constexpr float kShadowProofTolerance = 1e-4f;

// The frame's block: `registers` (MakeShadowBlock) with the two passes' proof.
// Valid only when the registers are, both passes gave a matrix, each agrees
// with its map's rows under the map's own texel (texel[0] the near map's,
// texel[1] the far map's; PassAgrees, `tol`), and each
// pass's bias is one finite pair or none. Otherwise all zero, and `why`
// (optional) says which of these failed first.
inline ShadowBlock FinishShadowBlock(const ShadowBlock& registers, const ShadowPassProof pass[2], float tol,
                                     ShadowBlockReason* why) {
  auto fail = [&](ShadowBlockReason r) {
    if (why) *why = r;
    return ShadowBlock{};
  };
  if (!pass || !registers.valid) return fail(ShadowBlockReason::kRegisters);
  if (!pass[0].have_matrix || !pass[1].have_matrix) return fail(ShadowBlockReason::kNoPass);
  for (int c = 0; c < 2; ++c) {
    if (!PassAgrees(pass[c].matrix, registers.world_to_map[c], registers.texel[c], tol)) return fail(ShadowBlockReason::kProof);
  }
  if (!pass[0].bias.ok() || !pass[1].bias.ok()) return fail(ShadowBlockReason::kBias);
  ShadowBlock b = registers;
  for (int c = 0; c < 2; ++c) {
    std::memcpy(b.world_to_clip[c], pass[c].matrix, sizeof(b.world_to_clip[c]));
    b.bias[c][0] = pass[c].bias.scale;
    b.bias[c][1] = pass[c].bias.offset;
  }
  b.valid = true;
  if (why) *why = ShadowBlockReason::kNone;
  return b;
}

}  // namespace fable2::native::capture
