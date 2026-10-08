// Synthetic standalone test; no game or GPU.
#include "../../src/native/render/frame_scene.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <thread>

using namespace fable2::native;
using capture::SkipReason;

// Every allocation of this program, counted (the buffer reuse cases below read
// which buffers are kept and which are freed), and one made to fail on request
// (the next allocation of g_fail_lo <= bytes < g_fail_hi throws once).
static std::atomic<long long> g_live{0};
static std::atomic<size_t> g_fail_lo{0}, g_fail_hi{0};
void* operator new(std::size_t n) {
  const size_t lo = g_fail_lo.load();
  if (lo != 0 && n >= lo && n < g_fail_hi.load()) {
    g_fail_lo = 0;
    throw std::bad_alloc();
  }
  void* p = std::malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  ++g_live;
  return p;
}
void operator delete(void* p) noexcept {
  if (!p) return;
  --g_live;
  std::free(p);
}
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }

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

  // The buffers of a finished scene (its records and its side array) go back
  // to the builder when the scene's last reference drops, and a later Finish
  // takes them, cleared, for the frame after it: a frame in the world is built
  // in memory that has been touched before, not in a new allocation.
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
    capture::DrawRecord ground = r;
    ground.prim = capture::kPrimQuadPatch;
    ground.material.status = capture::MaterialStatus::kTerrain;
    // Frame `id`: `terrain` terrain records (patch id * 1000 + i), then `plain` others; each
    // record's count is the frame's id.
    auto build = [&](render::FrameBuilder& b, uint32_t id, uint32_t terrain, uint32_t plain) {
      capture::DrawRecord g = ground, o = r;
      g.count = o.count = id;
      b.Open();
      for (uint32_t i = 0; i < terrain; ++i) {
        const TerrainPatch p = patch(float(id * 1000 + i));
        b.Add(g, &p);
      }
      for (uint32_t i = 0; i < plain; ++i) b.Add(o);
      return b.Finish(id);
    };
    // The scene holds exactly its frame: counts, each record's frame and each terrain record's own patch.
    auto whole = [&](const render::FrameScene& sc, uint32_t id, uint32_t terrain, uint32_t plain) {
      if (sc.draws.size() != terrain + plain || sc.terrain.size() != terrain) return false;
      for (uint32_t i = 0; i < terrain + plain; ++i) {
        const TerrainPatch* t = TerrainOf(sc, sc.draws[i]);
        if (sc.draws[i].count != id) return false;
        if (i < terrain ? (!t || sc.draws[i].terrain_index != i || t->patch != float(id * 1000 + i)) : t != nullptr) {
          return false;
        }
      }
      return true;
    };
    const long long live_before = g_live.load();
    {
      render::FrameBuilder rb;
      if (render::SceneBufferPool::kMaxSpares != 4 || rb.spare_count() != 0) return 93;
      auto a = build(rb, 1, 3, 5);
      const capture::DrawRecord* a_draws = a->draws.data();
      const TerrainPatch* a_terrain = a->terrain.data();
      const size_t a_dcap = a->draws.capacity(), a_tcap = a->terrain.capacity();
      auto b = build(rb, 2, 3, 5);
      // Two scenes alive at once never share a buffer, and nothing of a living scene is spare.
      if (b->draws.data() == a_draws || b->terrain.data() == a_terrain || rb.spare_count() != 0) return 94;
      {
        // (the published pointer is to const: the buffers still come back when it is the last)
        std::shared_ptr<const render::FrameScene> published = a;
        a.reset();
        if (rb.spare_count() != 0 || !whole(*published, 1, 3, 5)) return 95;
      }
      if (rb.spare_count() != 1) return 96;
      // Frame 3 is built in the buffers the builder took at Finish(2), when `a` was alive: not a's.
      // Finish(3) takes a's pair for frame 4.
      auto c = build(rb, 3, 2, 2);
      if (rb.spare_count() != 0 || c->draws.data() == a_draws || c->draws.data() == b->draws.data() ||
          c->terrain.data() == a_terrain || c->terrain.data() == b->terrain.data()) {
        return 97;
      }
      auto d = build(rb, 4, 1, 1);
      // The same buffers, at their capacities ...
      if (d->draws.data() != a_draws || d->terrain.data() != a_terrain || d->draws.capacity() != a_dcap ||
          d->terrain.capacity() != a_tcap) {
        return 98;
      }
      // ... and cleared: frame 4 alone is in them, its indices from 0 (frame 1 had 8 records and 3 patches).
      if (!whole(*d, 4, 1, 1)) return 99;
      // The scenes alive all along kept their own.
      if (!whole(*b, 2, 3, 5) || !whole(*c, 3, 2, 2)) return 100;
      // A frame without a record takes no buffer and gives none back: nothing circulates while
      // records are off.
      b.reset();
      c.reset();
      d.reset();
      if (rb.spare_count() != 3) return 101;
      rb.Open();
      rb.CountUnrecorded();
      auto e = rb.Finish(5);
      if (e->captured != 1 || e->draws.capacity() != 0 || e->terrain.capacity() != 0 || rb.spare_count() != 3) return 102;
      e.reset();
      if (rb.spare_count() != 3) return 103;
      // ReleaseSpares frees them: two allocations each.
      const long long live = g_live.load();
      rb.ReleaseSpares();
      if (rb.spare_count() != 0 || g_live.load() != live - 6) return 104;
      rb.ReleaseSpares();
      if (rb.spare_count() != 0) return 105;
    }
    if (g_live.load() != live_before) return 106;  // the builder frees what it holds

    // At most kMaxSpares pairs are kept. A fifth is freed at once (two allocations more than a
    // kept one gives back), and so is a sixth.
    {
      render::FrameBuilder rb;
      std::shared_ptr<render::FrameScene> held[6];
      for (uint32_t i = 0; i < 6; ++i) held[i] = build(rb, 10 + i, 4, 4);
      for (uint32_t i = 0; i < 6; ++i) {
        for (uint32_t k = i + 1; k < 6; ++k) {
          if (held[i]->draws.data() == held[k]->draws.data() || held[i]->terrain.data() == held[k]->terrain.data()) return 107;
        }
      }
      if (rb.spare_count() != 0) return 108;
      long long live = g_live.load();
      held[0].reset();
      const long long kept = live - g_live.load();  // the scene's own allocations; its buffers stay
      if (rb.spare_count() != 1 || kept < 1) return 109;
      for (uint32_t i = 1; i < 4; ++i) {
        live = g_live.load();
        held[i].reset();
        if (rb.spare_count() != i + 1 || live - g_live.load() != kept) return 110;
      }
      for (uint32_t i = 4; i < 6; ++i) {
        live = g_live.load();
        held[i].reset();
        if (rb.spare_count() != 4 || live - g_live.load() != kept + 2) return 111;
      }
      // The four that were kept are used again, one per frame, before anything is allocated.
      const long long live_full = g_live.load();
      for (uint32_t i = 0; i < 4; ++i) {
        held[i] = build(rb, 20 + i, 4, 4);
        if (rb.spare_count() != 3 - i || !whole(*held[i], 20 + i, 4, 4)) return 112;
      }
      if (g_live.load() != live_full + 4 * kept) return 113;  // four scenes, no buffer
    }
    if (g_live.load() != live_before) return 114;

    // A scene that outlives its builder frees its buffers itself.
    {
      std::shared_ptr<render::FrameScene> late;
      {
        render::FrameBuilder rb;
        late = build(rb, 30, 2, 2);
      }
      if (!whole(*late, 30, 2, 2)) return 115;
    }
    if (g_live.load() != live_before) return 116;

    // The last reference usually drops on the render thread. A second thread takes every
    // published scene, checks that it holds its own frame while the builder fills other
    // buffers (twice, with the builder running in between), and drops the one before.
    {
      render::FrameBuilder rb;
      render::ScenePublisher shared;
      std::atomic<bool> stop{false};
      std::atomic<uint32_t> bad{0}, seen{0};
      auto size_of = [](uint32_t id) { return 20 + id % 7; };
      std::thread reader([&] {
        std::shared_ptr<const render::FrameScene> current;
        while (!stop.load()) {
          std::shared_ptr<const render::FrameScene> next = shared.Latest();
          if (!next || next == current) {
            std::this_thread::yield();
            continue;
          }
          current = std::move(next);  // the scene before it: usually its last reference
          const uint32_t id = uint32_t(current->frame);
          for (int pass = 0; pass < 2; ++pass) {
            if (!whole(*current, id, size_of(id), 3)) ++bad;
            std::this_thread::yield();
          }
          ++seen;
        }
      });
      for (uint32_t id = 1; id <= 4000; ++id) {
        shared.Publish(build(rb, id, size_of(id), 3));
        if (id % 8 == 0) std::this_thread::yield();
      }
      // (a machine that never ran the reader during the loop still gives it the last scene)
      while (seen.load() == 0) std::this_thread::yield();
      stop = true;
      reader.join();
      if (bad.load() != 0) return 117;
      if (rb.spare_count() > render::SceneBufferPool::kMaxSpares) return 118;
      shared.Publish(nullptr);
    }
    if (g_live.load() != live_before) return 119;

    // Add appends the patch before the record. If the patch cannot be appended (its array has
    // to grow and the allocation fails) the frame holds no record of the draw; the other order
    // would leave a record whose index the next terrain record's patch then fills.
    {
      render::FrameBuilder rb;
      auto first = build(rb, 40, 3, 200);  // held: the next frame's arrays are new, sized by this frame
      capture::DrawRecord g = ground;
      rb.Open();
      uint32_t added = 0;
      bool threw = false;
      while (!threw && added < 64) {
        const TerrainPatch p = patch(float(100 + added));
        g_fail_lo = sizeof(TerrainPatch);  // a patch array of up to 64 entries: never the records' array
        g_fail_hi = 65 * sizeof(TerrainPatch);
        try {
          rb.Add(g, &p);
          ++added;
        } catch (const std::bad_alloc&) {
          threw = true;
        }
        g_fail_lo = 0;
      }
      if (!threw || added == 0) return 120;
      const TerrainPatch after = patch(999.0f);
      rb.Add(g, &after);
      auto sc = rb.Finish(41);
      if (sc->draws.size() != added + 1 || sc->terrain.size() != added + 1) return 121;
      for (uint32_t i = 0; i <= added; ++i) {
        const TerrainPatch* t = TerrainOf(*sc, sc->draws[i]);
        if (!t || sc->draws[i].terrain_index != i || t->patch != (i < added ? float(100 + i) : 999.0f)) return 122;
      }
      if (!whole(*first, 40, 3, 200)) return 123;
    }
    // The other failure: the patch is in, the record cannot be appended. The patch stays without
    // a record, and the next terrain record still gets its own entry.
    {
      render::FrameBuilder rb;
      auto first = build(rb, 50, 4, 0);  // held: four records, four patches
      capture::DrawRecord g = ground;
      rb.Open();
      uint32_t added = 0;
      bool threw = false;
      while (!threw && added < 64) {
        g_fail_lo = 1;  // the records' array is the only one that grows here (no patch is added)
        g_fail_hi = ~size_t(0);
        try {
          rb.Add(r);
          ++added;
        } catch (const std::bad_alloc&) {
          threw = true;
        }
        g_fail_lo = 0;
      }
      if (!threw || added == 0) return 124;
      // The records' array is full, the patch array is empty: this Add appends its patch and
      // fails on the record.
      const TerrainPatch orphan = patch(7.0f), own = patch(8.0f);
      g_fail_lo = 1;
      g_fail_hi = ~size_t(0);
      threw = false;
      try {
        rb.Add(g, &orphan);
      } catch (const std::bad_alloc&) {
        threw = true;
      }
      g_fail_lo = 0;
      if (!threw) return 125;
      rb.Add(g, &own);
      auto sc = rb.Finish(51);
      if (sc->draws.size() != added + 1 || sc->terrain.size() != 2) return 126;
      const TerrainPatch* t = TerrainOf(*sc, sc->draws.back());
      if (!t || sc->draws.back().terrain_index != 1 || t->patch != 8.0f) return 127;
    }
    if (g_live.load() != live_before) return 128;
  }

  render::ScenePublisher pub;
  if (pub.Latest()) return 18;
  pub.Publish(s);
  pub.Publish(s2);
  if (pub.Latest()->frame != 43) return 19;
  std::cout << "PASS: draw record, frame builder, terrain side array, buffer reuse, publisher\n";
  return 0;
}
