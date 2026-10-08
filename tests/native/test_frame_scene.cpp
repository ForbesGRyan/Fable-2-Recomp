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
    // The record does not hold the patch: the frame keeps it in its side array (below).
    if (rt.skip != SkipReason::kNone || capture::TerrainInputsOf(t) != &t.terrain) return 50;
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
    if (capture::AssembleRecord(o, 0).skip != SkipReason::kNone || capture::TerrainInputsOf(o)) return 55;
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
  if (!s->records || s->capture_ms != 0.0) return 20;  // defaults: records built, untimed

  // Records off (no consumer): draws are counted inside the bracket only, no
  // records are kept, and the next Finish starts clean.
  b.CountUnrecorded();              // outside bracket: ignored
  b.Open();
  b.CountUnrecorded();
  b.CountUnrecorded();
  auto c = b.Finish(44);
  if (c->captured != 2 || !c->draws.empty()) return 21;
  for (uint32_t n : c->skipped) {
    if (n) return 22;
  }
  if (b.Finish(45)->captured != 0) return 23;

  // Material tallies: capture-side statuses of drawable records only, and the
  // untextured PS hashes (ps-unknown / no-albedo / uv-unsupported) by count.
  {
    using capture::MaterialStatus;
    auto tex = r; tex.material.status = MaterialStatus::kTextured; tex.material.ps_hash = 0x7;
    auto unk = r; unk.material.status = MaterialStatus::kPsUnknown; unk.material.ps_hash = 0xA;
    auto sk = r; sk.skip = SkipReason::kNoTransform;
    sk.material.status = MaterialStatus::kPsUnknown; sk.material.ps_hash = 0xB;
    b.Open();
    b.Add(tex); b.Add(unk); b.Add(unk); b.Add(sk);
    auto m = b.Finish(46);
    if (m->material[size_t(MaterialStatus::kTextured)] != 1) return 60;
    if (m->material[size_t(MaterialStatus::kPsUnknown)] != 2) return 61;
    uint32_t total = 0;
    for (uint32_t n : m->material) total += n;
    if (total != 3) return 62;
    const std::vector<std::pair<uint64_t, uint32_t>> want = {{0xA, 2}};
    if (m->untextured_ps != want) return 63;
    // The next frame starts clean.
    auto m2 = b.Finish(47);
    for (uint32_t n : m2->material) {
      if (n) return 64;
    }
    if (!m2->untextured_ps.empty()) return 65;
  }

  // The terrain inputs of a frame's terrain records are in the scene's side
  // array, each at its record's terrain_index (TerrainOf); the records do not
  // hold them.
  {
    using capture::kNoSideIndex;
    using capture::TerrainPatch;
    using render::TerrainOf;
    auto patch = [](float first) {
      TerrainPatch p;
      p.active = true;
      p.patch = first;
      return p;
    };
    const capture::DrawRecord plain = r;
    capture::DrawRecord ground = r;
    ground.prim = capture::kPrimQuadPatch;
    ground.vs_hash = 0xC30A;
    ground.material.status = capture::MaterialStatus::kTerrain;
    const TerrainPatch p0 = patch(10), p1 = patch(11), p2 = patch(12);
    render::FrameBuilder tb;
    tb.Add(ground, &p0);                 // outside the bracket: ignored, no entry
    tb.Open();
    tb.Add(ground, &p0);
    tb.Add(plain);
    tb.Add(ground, &p1);
    auto gone = ground; gone.skip = SkipReason::kBadIndex;
    tb.Add(gone, &p2);                   // a skipped terrain draw takes no entry
    auto stale = plain; stale.terrain_index = 0;
    tb.Add(stale);                       // an index the frame did not give is not kept
    auto f1 = tb.Finish(1);
    if (f1->captured != 5 || f1->draws.size() != 4 || f1->terrain.size() != 2) return 70;
    if (f1->skipped[size_t(SkipReason::kBadIndex)] != 1) return 71;
    if (f1->draws[0].terrain_index != 0 || f1->draws[1].terrain_index != kNoSideIndex ||
        f1->draws[2].terrain_index != 1 || f1->draws[3].terrain_index != kNoSideIndex) {
      return 72;
    }
    const TerrainPatch* t0 = TerrainOf(*f1, f1->draws[0]);
    const TerrainPatch* t1 = TerrainOf(*f1, f1->draws[2]);
    if (t0 != &f1->terrain[0] || t1 != &f1->terrain[1] || !t0->active || t0->patch != 10.0f || t1->patch != 11.0f) {
      return 73;
    }
    // A record without an index: null.
    if (TerrainOf(*f1, f1->draws[1]) || TerrainOf(*f1, f1->draws[3]) || TerrainOf(*f1, capture::DrawRecord{})) return 74;
    // An index past the end of the scene's array (a record of another scene,
    // a damaged one): null, nothing is read.
    capture::DrawRecord past = f1->draws[0];
    past.terrain_index = 2;
    if (TerrainOf(*f1, past)) return 75;
    past.terrain_index = 0xFFFE;
    if (TerrainOf(*f1, past)) return 76;
    if (TerrainOf(render::FrameScene{}, f1->draws[0])) return 77;
    // kNoSideIndex is "none" also in a scene with that many entries and more.
    {
      render::FrameScene big;
      big.terrain.resize(size_t(kNoSideIndex) + 2);
      past.terrain_index = kNoSideIndex;
      if (TerrainOf(big, past)) return 78;
      past.terrain_index = kNoSideIndex - 1;
      if (TerrainOf(big, past) != &big.terrain[kNoSideIndex - 1]) return 79;
    }

    // The next frame starts with an empty array (its first terrain record is
    // index 0 again) and the first frame's scene keeps its own entries.
    tb.Open();
    tb.Add(plain);
    tb.Add(ground, &p2);
    auto f2 = tb.Finish(2);
    if (f2->draws.size() != 2 || f2->terrain.size() != 1 || f2->draws[0].terrain_index != kNoSideIndex ||
        f2->draws[1].terrain_index != 0) {
      return 80;
    }
    if (TerrainOf(*f2, f2->draws[1]) != &f2->terrain[0] || f2->terrain[0].patch != 12.0f) return 81;
    if (f1->terrain.size() != 2 || TerrainOf(*f1, f1->draws[0])->patch != 10.0f ||
        TerrainOf(*f1, f1->draws[2])->patch != 11.0f) {
      return 82;
    }
    // A frame without terrain, and one without records, hold none.
    tb.Open();
    tb.Add(plain);
    if (!tb.Finish(3)->terrain.empty()) return 83;
    tb.Open();
    tb.CountUnrecorded();
    auto f4 = tb.Finish(4);
    if (f4->captured != 1 || !f4->draws.empty() || !f4->terrain.empty()) return 84;

    // The index is 16 bits and kNoSideIndex means none: a frame holds 65,535
    // terrain records, indices 0 to 65,534. One more is skipped as bad-memory
    // (the capture's reason for a count past a cap), counted by reason and by
    // shader like any skipped draw and in no material tally; its index never
    // wraps onto another record's inputs. Other draws are still recorded.
    tb.Open();
    for (uint32_t i = 0; i < kNoSideIndex; ++i) {
      const TerrainPatch p = patch(float(i));
      tb.Add(ground, &p);
    }
    tb.Add(ground, &p0);
    tb.Add(ground, &p1);
    tb.Add(plain);
    auto f5 = tb.Finish(5);
    if (f5->captured != 65538 || f5->draws.size() != 65536 || f5->terrain.size() != 65535) return 85;
    if (f5->skipped[size_t(SkipReason::kBadMemory)] != 2) return 86;
    for (uint32_t i = 0; i < kNoSideIndex; ++i) {
      const TerrainPatch* t = TerrainOf(*f5, f5->draws[i]);
      if (f5->draws[i].terrain_index != i || !t || t->patch != float(i)) return 87;
    }
    if (f5->draws.back().terrain_index != kNoSideIndex || TerrainOf(*f5, f5->draws.back())) return 88;
    capture::ShaderTally<render::kSkippedTallySize>::Entry top[2];
    if (f5->skipped_by_vs.Top(uint8_t(SkipReason::kBadMemory), top, 2) != 1 || top[0].hash != 0xC30A ||
        top[0].count != 2) {
      return 89;
    }
    if (f5->material[size_t(capture::MaterialStatus::kTerrain)] != 65535) return 90;
    // The frame after it is not affected.
    tb.Open();
    tb.Add(ground, &p2);
    auto f6 = tb.Finish(6);
    if (f6->draws.size() != 1 || f6->terrain.size() != 1 || f6->draws[0].terrain_index != 0) return 91;
    for (uint32_t n : f6->skipped) {
      if (n) return 92;
    }
  }

  render::ScenePublisher pub;
  if (pub.Latest()) return 18;
  pub.Publish(s);
  pub.Publish(s2);
  if (pub.Latest()->frame != 43) return 19;
  std::cout << "PASS: draw record, frame builder, terrain side array, publisher\n";
  return 0;
}
