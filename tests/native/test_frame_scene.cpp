// Synthetic standalone test; no game or GPU.
#include "../../src/native/render/frame_scene.h"
#include <iostream>
#include <string>

using namespace fable2::native;
using capture::SkipReason;

static capture::DrawInputs Good() {
  static float bank[256 * 4] = {};
  static const capture::TransformInfo t{4, capture::TransformLayout::kCombine, -1};
  for (int i = 0; i < 16; ++i) bank[4 * 4 + i] = float(i + 1);
  capture::DrawInputs in{};
  in.func_id = 2; in.prim = 6; in.start = 0; in.count = 30; in.indexed = true;
  in.have_shader = true; in.vs_hash = 0xABCD; in.have_pos = true;
  in.pos.format = capture::PosFormat::kFloat3; in.pos.stride_bytes = 12;
  in.have_vb = true; in.vb = {0x1000, 1200}; in.have_ib = true; in.ib = {0x2000, 60};
  in.transform = &t; in.bank = bank;
  return in;
}

int main() {
  auto r = capture::AssembleRecord(Good(), 7);
  if (r.skip != SkipReason::kNone || r.seq != 7 || r.rows[0] != 1.0f || r.rows[15] != 16.0f) return 1;
  if (r.layout != capture::TransformLayout::kCombine || r.vb.size != 1200) return 2;
  auto in = Good(); in.have_shader = false;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kUnknownShader) return 3;
  in = Good(); in.have_pos = false;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kUnknownPosFormat) return 4;
  in = Good(); in.transform = nullptr;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kNoTransform) return 5;
  in = Good(); in.bank = nullptr;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kNoTransform) return 6;
  in = Good(); in.prim = 8;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kUnsupportedPrim) return 7;
  in = Good(); in.have_vb = false;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kNoStream) return 8;
  in = Good(); in.have_ib = false;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kNoStream) return 9;
  in = Good(); in.vb.size = 0;
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kBadMemory) return 10;
  in = Good(); in.ib.size = 30;  // 30 indices * 2 bytes = 60 > 30
  if (capture::AssembleRecord(in, 0).skip != SkipReason::kBadMemory) return 11;
  if (std::string(capture::SkipReasonName(SkipReason::kBadIndex)) != "bad-index") return 12;

  // A deformed transform (the shader moves the fetched position: skinning,
  // displacement) marks the drawable record; plain transforms do not.
  static const capture::TransformInfo td{0, capture::TransformLayout::kDot, -1, true};
  in = Good(); in.transform = &td;
  const auto rd = capture::AssembleRecord(in, 0);
  if (rd.skip != SkipReason::kNone || !rd.deformed) return 40;
  if (r.deformed) return 41;

  // Skip-reason precedence: a capture-side reason (garbage count, bad index
  // memory) replaces AssembleRecord's, except for unsupported primitives.
  if (capture::CountSkip(capture::kMaxDrawCount) != SkipReason::kNone) return 42;
  if (capture::CountSkip(capture::kMaxDrawCount + 1) != SkipReason::kBadMemory) return 43;
  if (capture::ResolveSkip(SkipReason::kNoTransform, SkipReason::kBadIndex) != SkipReason::kBadIndex) return 44;
  if (capture::ResolveSkip(SkipReason::kNone, SkipReason::kBadMemory) != SkipReason::kBadMemory) return 45;
  if (capture::ResolveSkip(SkipReason::kUnsupportedPrim, SkipReason::kBadMemory) != SkipReason::kUnsupportedPrim) return 46;
  if (capture::ResolveSkip(SkipReason::kNoStream, SkipReason::kNone) != SkipReason::kNoStream) return 47;
  // An oversized draw is rejected before the shader is read, so AssembleRecord
  // alone would call it kUnknownShader; the final reason is kBadMemory.
  in = Good(); in.count = capture::kMaxDrawCount + 1; in.have_shader = false;
  if (capture::ResolveSkip(capture::AssembleRecord(in, 0).skip, capture::CountSkip(in.count)) !=
      SkipReason::kBadMemory) return 48;
  // ... but an unsupported primitive with a garbage count stays unsupported.
  in.prim = 8;
  if (capture::ResolveSkip(capture::AssembleRecord(in, 0).skip, capture::CountSkip(in.count)) !=
      SkipReason::kUnsupportedPrim) return 49;

  // Terrain patches (tessellated quad patches, no vertex buffer): drawable
  // when the shader has a terrain entry and the patch was built.
  {
    capture::DrawInputs t{};
    t.func_id = 3; t.prim = capture::kPrimQuadPatch; t.start = 1; t.count = 1;
    t.have_shader = true; t.vs_hash = 0xC30A;
    t.transform = Good().transform; t.bank = Good().bank;
    t.terrain_shader = true;
    t.terrain.active = true; t.terrain.patch = 1; t.terrain.patches = 1; t.terrain.map.size = 4096;
    const auto rt = capture::AssembleRecord(t, 5);
    if (rt.skip != SkipReason::kNone || !rt.terrain.active || rt.terrain.patch != 1.0f) return 50;
    if (rt.rows[0] != 1.0f) return 51;
    // The shader is a terrain shader but the patch could not be built
    // (unsupported heightmap): unknown position format.
    auto t2 = t; t2.terrain.active = false;
    if (capture::AssembleRecord(t2, 0).skip != SkipReason::kUnknownPosFormat) return 52;
    // A patch draw whose shader has no terrain entry stays unsupported.
    t2 = t; t2.terrain_shader = false; t2.terrain.active = false;
    if (capture::AssembleRecord(t2, 0).skip != SkipReason::kUnsupportedPrim) return 53;
    // Terrain still needs the transform.
    t2 = t; t2.transform = nullptr;
    if (capture::AssembleRecord(t2, 0).skip != SkipReason::kNoTransform) return 54;
    // Terrain fields on an ordinary draw are ignored.
    auto o = Good(); o.terrain.active = true;
    if (capture::AssembleRecord(o, 0).terrain.active) return 55;
  }
  // A rigid skin layout travels with the record.
  {
    auto s = Good();
    s.skin.active = true; s.skin.palette_addr = 0x3000; s.skin.palette_size = 168;
    const auto rs = capture::AssembleRecord(s, 0);
    if (rs.skip != SkipReason::kNone || !rs.skin.active || rs.skin.palette_size != 168) return 56;
  }

  render::FrameBuilder b;
  b.Add(r);                         // outside bracket: ignored
  b.Close();                        // close while closed: ignored
  b.Open(); b.Open();               // nested open: ignored
  if (!b.InMainScene()) return 13;
  b.Add(r);
  auto skipped = r; skipped.skip = SkipReason::kNoTransform;
  b.Add(skipped);
  b.Close();
  b.Add(r);                         // after close: ignored
  b.Open();                         // left open at Finish
  b.Add(r);
  auto s = b.Finish(42);
  if (s->frame != 42 || s->captured != 3 || s->draws.size() != 2) return 14;
  if (s->skipped[size_t(SkipReason::kNoTransform)] != 1) return 15;
  if (b.InMainScene()) return 16;   // Finish closes
  auto s2 = b.Finish(43);
  if (s2->captured != 0 || !s2->draws.empty()) return 17;

  render::ScenePublisher pub;
  if (pub.Latest()) return 18;
  pub.Publish(s);
  pub.Publish(s2);
  if (pub.Latest()->frame != 43) return 19;
  std::cout << "PASS: draw record, frame builder, publisher\n";
  return 0;
}
