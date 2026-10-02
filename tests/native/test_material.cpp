// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/draw_record.h"
#include <cmath>
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
  if (ResolveUvFetch(f, u, other, &l)) return 11;           // u and v from different fetches
  VsUvSpec past = u; past.fetch_index = 5;
  if (ResolveUvFetch(f, past, v, &l)) return 12;

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
  // Default material status is ps-unknown.
  if (Material{}.status != MaterialStatus::kPsUnknown) return 15;
  return 0;
}
