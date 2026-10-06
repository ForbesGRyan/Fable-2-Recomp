// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/draw_record.h"
#include <cmath>
#include <limits>
#include <cstring>
#include <vector>

using namespace fable2::native::capture;

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }
static int32_t Ref(int bank, int reg, int comp, bool neg = false) {
  return (int32_t(neg) << 11) | (bank << 10) | (reg * 4 + comp);
}

int main() {
  if (std::strcmp(MaterialStatusName(MaterialStatus::kTextured), "textured") != 0) return 1;
  if (std::strcmp(MaterialStatusName(MaterialStatus::kTextureDynamic), "texture-dynamic") != 0) return 2;
  if (std::strcmp(MaterialStatusName(MaterialStatus::kUvUnsupported), "uv-unsupported") != 0) return 3;

  float vs[1024] = {}, ps[1024] = {};
  vs[10 * 4 + 1] = 2.0f;   // c10.y (vertex)
  vs[10 * 4 + 3] = 0.5f;   // c10.w
  ps[8 * 4 + 1] = 3.0f;    // c8.y (pixel)
  ps[8 * 4 + 3] = -1.0f;   // c8.w
  if (!Near(RefValue(kNoRef, 1.0f, vs, ps), 1.0f) || !Near(RefValue(Ref(1, 8, 1), 0, vs, ps), 3.0f)) return 4;
  if (!Near(RefValue(Ref(0, 10, 3, true), 0, vs, ps), -0.5f)) return 5;
  // VS: x*c10.y + c10.w, then PS: x*c8.y + c8.w  ->  x*6 + (0.5*3 - 1) = x*6 + 0.5
  UvStage vst[2] = {{Ref(0, 10, 1), Ref(0, 10, 3)}, {}};
  UvStage pst[2] = {{Ref(1, 8, 1), Ref(1, 8, 3)}, {}};
  float a = 0, b = 0;
  ComposeAxis(vst, pst, vs, ps, &a, &b);
  if (!Near(a, 6.0f) || !Near(b, 0.5f)) return 6;
  UvStage none[2] = {};
  ComposeAxis(none, none, vs, ps, &a, &b);
  if (!Near(a, 1.0f) || !Near(b, 0.0f)) return 7;

  // ResolveUvFetch: both axes from fetch 1 (half2 at offset 3), sources y then x.
  std::vector<VertexFetch> f(2);
  f[0].format = 32; f[0].stride_dwords = 5; f[0].offset_dwords = 0;
  f[1].format = 31; f[1].stride_dwords = 5; f[1].offset_dwords = 3; f[1].mini = true;
  VsUvSpec u{0x1, 0, 0, 1, 1, 31, 3, {}};
  VsUvSpec v{0x1, 0, 1, 1, 0, 31, 3, {}};
  UvLayout l;
  if (!ResolveUvFetch(f, u, v, &l) || l.comp_u != 1 || l.comp_v != 0 || l.offset_bytes != 12) return 8;
  VsUvSpec wrong_fmt = u; wrong_fmt.xenos_format = 37;      // table disagrees with the decoded fetch
  if (ResolveUvFetch(f, wrong_fmt, v, &l)) return 9;
  VsUvSpec wrong_off = v; wrong_off.offset_dwords = 0;
  if (ResolveUvFetch(f, u, wrong_off, &l)) return 10;
  VsUvSpec other = v; other.fetch_index = 0; other.xenos_format = 32; other.offset_dwords = 0;
  if (ResolveUvFetch(f, u, other, &l)) return 11;           // u and v from fetches of different formats
  VsUvSpec past = u; past.fetch_index = 5;
  if (ResolveUvFetch(f, past, v, &l)) return 12;
  if (ResolveUvFetch(f, u, past, &l)) return 30;
  // u and v from two fetches of one stream with the same element format (the
  // instancing shaders: source z of the position element and of the normal element).
  std::vector<VertexFetch> g(3);
  g[0].format = 32; g[0].fetch_slot = 95; g[0].stride_dwords = 6; g[0].offset_dwords = 0;
  g[1] = g[0]; g[1].offset_dwords = 2; g[1].mini = true;
  g[2] = g[1]; g[2].offset_dwords = 4;
  VsUvSpec gu{0x2, 0, 0, 0, 2, 32, 0, {}};
  VsUvSpec gv{0x2, 0, 1, 1, 2, 32, 2, {}};
  if (!ResolveUvFetch(g, gu, gv, &l) || l.comp_u != 2 || l.comp_v != 2 || l.offset_bytes != 0 ||
      l.v_element_delta != 8 || l.stride_bytes != 24 || l.fetch_slot != 95) {
    return 31;
  }
  VsUvSpec gv_wrong = gv; gv_wrong.offset_dwords = 4;       // the table names another element
  if (ResolveUvFetch(g, gu, gv_wrong, &l)) return 32;
  g[1].fetch_slot = 94;                                     // v in another stream
  if (ResolveUvFetch(g, gu, gv, &l)) return 33;

  // Refs visited by ForEachRef (the capture reads only these registers).
  std::vector<int32_t> seen;
  ForEachRef(vst, 2, [&](int32_t r) { seen.push_back(r); });
  if (seen.size() != 2 || seen[0] != Ref(0, 10, 1) || seen[1] != Ref(0, 10, 3)) return 13;

  // Records carry the material through AssembleRecord.
  DrawInputs in;
  in.prim = 4; in.have_shader = true; in.have_pos = true; in.have_vb = true; in.vb = {0x1000, 64};
  in.pos.format = PosFormat::kFloat3; in.pos.stride_bytes = 12;
  TransformInfo ti{0, TransformLayout::kDot, -1};
  float bank[1024] = {};
  in.transform = &ti; in.bank = bank;
  in.material.status = MaterialStatus::kTextured;
  in.material.uv_xform[0] = 4.0f;
  DrawRecord r = AssembleRecord(in, 7);
  if (r.skip != SkipReason::kNone || r.material.status != MaterialStatus::kTextured || r.material.uv_xform[0] != 4.0f) return 14;
  // The alpha recipe, its scale and the kind ride along too.
  in.material.alpha = {AlphaSource::kFetch, 8, 1, {26 * 4 + 3, 15 * 4 + 3}, true};
  in.material.alpha_scale = 0.25f;
  in.material.kind = MaterialKind::kTerrainFar;
  r = AssembleRecord(in, 7);
  if (r.material.alpha.source != AlphaSource::kFetch || r.material.alpha.slot != 8 ||
      r.material.alpha.component != 1 || r.material.alpha.scale_ref[0] != 26 * 4 + 3 ||
      r.material.alpha.scale_ref[1] != 15 * 4 + 3 || !r.material.alpha.divide_colour ||
      r.material.alpha_scale != 0.25f || r.material.kind != MaterialKind::kTerrainFar) {
    return 16;
  }
  // Default material status is ps-unknown.
  if (Material{}.status != MaterialStatus::kPsUnknown) return 15;
  {
    // Defaults: no alpha recipe (scale refs -1, component w, colour not divided), scale 1, plain.
    const Material d;
    if (d.alpha.source != AlphaSource::kNone || d.alpha.slot != 0 || d.alpha.component != 3 ||
        d.alpha.scale_ref[0] != -1 || d.alpha.scale_ref[1] != -1 || d.alpha.divide_colour ||
        d.alpha_scale != 1.0f || d.kind != MaterialKind::kPlain) {
      return 17;
    }
  }
  // ComposeAlphaScale: the product of the constants read; nothing finite-unsafe is accepted.
  {
    float out = 7.0f;
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float two[2] = {0.5f, 4.0f};
    if (!ComposeAlphaScale(two, 0, &out) || out != 1.0f) return 50;
    if (!ComposeAlphaScale(two, 1, &out) || out != 0.5f) return 51;
    if (!ComposeAlphaScale(two, 2, &out) || out != 2.0f) return 52;
    out = 7.0f;
    const float bad_nan[2] = {0.5f, nan}, bad_inf[2] = {inf, 2.0f}, bad_ninf[2] = {-inf, 2.0f};
    if (ComposeAlphaScale(bad_nan, 2, &out) || ComposeAlphaScale(bad_inf, 2, &out) ||
        ComposeAlphaScale(bad_ninf, 2, &out)) {
      return 53;
    }
    const float big[2] = {1e30f, 1e30f};  // finite factors whose product overflows
    if (ComposeAlphaScale(big, 2, &out)) return 54;
    if (out != 7.0f) return 55;           // out untouched on failure
  }
  // DecideAlpha, every input: (test or mask, blend, colour known, has recipe).
  for (int bits = 0; bits < 16; ++bits) {
    const bool test = bits & 1, blend = bits & 2, colour = bits & 4, recipe = bits & 8;
    AlphaRule want;
    if (!test && !blend) want = AlphaRule::kOpaque;
    else if (blend && !colour) want = AlphaRule::kSkipBlendUnknown;
    else if (recipe) want = AlphaRule::kApply;
    else want = AlphaRule::kDrawAlphaUnknown;
    if (DecideAlpha(test, blend, colour, recipe) != want) return 100 + bits;
  }
  // Spot checks of the named cases, so the loop above cannot mirror a wrong rule.
  if (DecideAlpha(false, false, false, false) != AlphaRule::kOpaque) return 40;
  if (DecideAlpha(true, false, false, false) != AlphaRule::kDrawAlphaUnknown) return 41;   // test, no recipe
  if (DecideAlpha(true, false, false, true) != AlphaRule::kApply) return 42;               // colour unknown only matters for blend
  if (DecideAlpha(false, true, false, true) != AlphaRule::kSkipBlendUnknown) return 43;
  if (DecideAlpha(false, true, true, false) != AlphaRule::kDrawAlphaUnknown) return 44;    // blend, colour, no recipe
  if (DecideAlpha(false, true, true, true) != AlphaRule::kApply) return 45;
  return 0;
}
