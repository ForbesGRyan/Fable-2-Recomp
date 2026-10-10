// Synthetic standalone test; no game or GPU.
#include "../../src/native/render/frame_scene.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
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
// Allocations of this size or more, counted: in the creeping-frame case below they are the
// buffers of a frame's records and light inputs (nothing else there is that large).
static constexpr size_t kBufferBytes = 32 * 1024;
static std::atomic<long long> g_buffers{0};
void* operator new(std::size_t n) {
  const size_t lo = g_fail_lo.load();
  if (lo != 0 && n >= lo && n < g_fail_hi.load()) {
    g_fail_lo = 0;
    throw std::bad_alloc();
  }
  void* p = std::malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  ++g_live;
  if (n >= kBufferBytes) ++g_buffers;
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

// The light side array and the frame's light block (FrameScene::lights, ::light), the reuse of
// the three buffers, and the pool under threads. `r`: a drawable record. Returns 0 or the number
// of the case that failed.
static int LightAndPoolTests(const capture::DrawRecord& r) {
  using capture::kNoSideIndex;
  using capture::LightFamily;
  using capture::LightInputs;
  using capture::LightReason;
  using capture::TerrainPatch;
  using render::LightOf;
  using render::TerrainOf;
  auto inputs = [](LightFamily family, uint32_t mark, bool multi = false) {
    LightInputs l;
    l.family = family;
    l.multi = multi;
    l.colour_first = mark;
    return l;
  };
  auto patch = [](float first) {
    TerrainPatch p;
    p.active = true;
    p.patch = first;
    return p;
  };
  capture::DrawRecord plain = r;
  plain.material.ps_hash = 0xAAAA;
  capture::DrawRecord ground = r;
  ground.prim = capture::kPrimQuadPatch;
  ground.material.status = capture::MaterialStatus::kTerrain;
  ground.material.ps_hash = 0x9C21;

  // --- the side array: each lit record's inputs at its light_index (LightOf) ---
  {
    const LightInputs obj = inputs(LightFamily::kObject, 1), plant = inputs(LightFamily::kPlant, 2, true),
                      layer = inputs(LightFamily::kTerrainLayer, 3);
    const TerrainPatch p0 = patch(10);
    render::FrameBuilder lb;
    lb.Add(plain, nullptr, &obj);                 // outside the bracket: ignored, no entry
    lb.Open();
    lb.Add(plain, nullptr, &obj);                 // 0: light 0
    lb.Add(plain);                                // 1: no inputs (light-unknown unless the caller says why)
    lb.Add(ground, &p0, &layer);                  // 2: terrain 0 and light 1
    capture::DrawRecord np = plain;
    np.material.ps_hash = 0xBEEF;
    lb.Add(np, nullptr, nullptr, LightReason::kNormalUnsupported);  // 3
    capture::DrawRecord gone = plain;
    gone.skip = SkipReason::kBadIndex;
    lb.Add(gone, nullptr, &plant);                // a skipped draw takes no entry and is in no light tally
    capture::DrawRecord stale = plain;
    stale.light_index = 0;
    stale.material.ps_hash = 0xF00D;
    lb.Add(stale);                                // 4: an index the frame did not give is not kept
    lb.Add(plain, nullptr, &plant);               // 5: light 2, a "-multi" form
    const LightInputs none{};
    lb.Add(plain, nullptr, &none);                // 6: inputs without a family are no inputs
    auto f1 = lb.Finish(1);
    if (f1->captured != 8 || f1->draws.size() != 7 || f1->lights.size() != 3 || f1->terrain.size() != 1) return 130;
    const uint16_t want[7] = {0, kNoSideIndex, 1, kNoSideIndex, kNoSideIndex, 2, kNoSideIndex};
    for (int i = 0; i < 7; ++i) {
      if (f1->draws[i].light_index != want[i]) return 131;
    }
    if (f1->draws[2].terrain_index != 0 || TerrainOf(*f1, f1->draws[2])->patch != 10.0f) return 132;
    const LightInputs* l0 = LightOf(*f1, f1->draws[0]);
    const LightInputs* l1 = LightOf(*f1, f1->draws[2]);
    const LightInputs* l2 = LightOf(*f1, f1->draws[5]);
    if (l0 != &f1->lights[0] || l1 != &f1->lights[1] || l2 != &f1->lights[2]) return 133;
    if (l0->family != LightFamily::kObject || l0->colour_first != 1 || l1->family != LightFamily::kTerrainLayer ||
        l1->colour_first != 3 || l2->family != LightFamily::kPlant || l2->colour_first != 2 || !l2->multi) {
      return 134;
    }
    // A record without an index: null. An index past the end (a record of another scene, a
    // damaged one): null, nothing is read.
    if (LightOf(*f1, f1->draws[1]) || LightOf(*f1, f1->draws[3]) || LightOf(*f1, f1->draws[4]) ||
        LightOf(*f1, f1->draws[6]) || LightOf(*f1, capture::DrawRecord{})) {
      return 135;
    }
    capture::DrawRecord past = f1->draws[0];
    past.light_index = 3;
    if (LightOf(*f1, past)) return 136;
    past.light_index = 0xFFFE;
    if (LightOf(*f1, past)) return 137;
    if (LightOf(render::FrameScene{}, f1->draws[0])) return 138;
    {
      render::FrameScene big;
      big.lights.resize(size_t(kNoSideIndex) + 2);
      past.light_index = kNoSideIndex;
      if (LightOf(big, past)) return 139;
      past.light_index = kNoSideIndex - 1;
      if (LightOf(big, past) != &big.lights[kNoSideIndex - 1]) return 140;
    }
    // The frame's tallies: lit records by family and how many of them are "-multi" forms; the
    // others by reason, and their pixel shaders by count.
    using F = LightFamily;
    const uint32_t families[size_t(F::kCount)] = {0, 1, 0, 1, 0, 1, 0};
    for (size_t i = 0; i < size_t(F::kCount); ++i) {
      if (f1->light_family[i] != families[i]) return 141;
    }
    if (f1->light_multi != 1) return 142;
    if (f1->light_reason[size_t(LightReason::kLightUnknown)] != 3 ||
        f1->light_reason[size_t(LightReason::kNormalUnsupported)] != 1 || f1->light_reason[0] != 0) {
      return 143;
    }
    const std::vector<std::pair<uint64_t, uint32_t>> unknown = {{0xAAAA, 2}, {0xBEEF, 1}, {0xF00D, 1}};
    if (f1->light_unknown_ps != unknown) return 144;
    if (f1->skipped[size_t(SkipReason::kBadIndex)] != 1) return 145;

    // The next frame starts with an empty array and clean tallies; the first frame's scene
    // keeps its own entries.
    lb.Open();
    lb.Add(plain);
    lb.Add(plain, nullptr, &plant);
    // A reason that is none, or no reason at all, for a record without inputs: light-unknown.
    lb.Add(plain, nullptr, nullptr, LightReason::kNone);
    lb.Add(plain, nullptr, nullptr, LightReason::kCount);
    auto f2 = lb.Finish(2);
    if (f2->draws.size() != 4 || f2->lights.size() != 1 || f2->draws[0].light_index != kNoSideIndex ||
        f2->draws[1].light_index != 0 || LightOf(*f2, f2->draws[1])->colour_first != 2) {
      return 146;
    }
    if (f2->light_family[size_t(F::kPlant)] != 1 || f2->light_family[size_t(F::kObject)] != 0 || f2->light_multi != 1 ||
        f2->light_reason[size_t(LightReason::kLightUnknown)] != 3 ||
        f2->light_reason[size_t(LightReason::kNormalUnsupported)] != 0 ||
        f2->light_unknown_ps != std::vector<std::pair<uint64_t, uint32_t>>{{0xAAAA, 3}}) {
      return 147;
    }
    if (f1->lights.size() != 3 || LightOf(*f1, f1->draws[5])->colour_first != 2) return 148;
    // A frame without lit records, and one without records, hold none.
    lb.Open();
    lb.Add(plain);
    auto f3 = lb.Finish(3);
    if (!f3->lights.empty() || f3->light_reason[size_t(LightReason::kLightUnknown)] != 1) return 149;
    lb.Open();
    lb.CountUnrecorded();
    auto f4 = lb.Finish(4);
    if (!f4->lights.empty() || f4->light_reason[size_t(LightReason::kLightUnknown)] != 0 || !f4->light_unknown_ps.empty()) return 150;

    // The index is 16 bits: a frame holds 65,535 light entries. A lit record past that is kept,
    // with its material, without inputs, and counted light-unknown: the light never removes a
    // draw (a terrain record past the terrain array's cap is skipped: it cannot be built).
    lb.Open();
    for (uint32_t i = 0; i < kNoSideIndex; ++i) {
      const LightInputs l = inputs(LightFamily::kObject, i);
      lb.Add(plain, nullptr, &l);
    }
    lb.Add(plain, nullptr, &obj);
    // (whatever reason comes with inputs that were fine: the cap is why it has none)
    lb.Add(plain, nullptr, &plant, LightReason::kNormalUnsupported);
    lb.Add(plain);
    auto f5 = lb.Finish(5);
    if (f5->captured != 65538 || f5->draws.size() != 65538 || f5->lights.size() != 65535) return 151;
    for (uint32_t n : f5->skipped) {
      if (n) return 152;
    }
    for (uint32_t i = 0; i < kNoSideIndex; ++i) {
      const LightInputs* l = LightOf(*f5, f5->draws[i]);
      if (f5->draws[i].light_index != i || !l || l->colour_first != i) return 153;
    }
    for (uint32_t i = 65535; i < 65538; ++i) {
      if (f5->draws[i].light_index != kNoSideIndex || LightOf(*f5, f5->draws[i])) return 154;
    }
    if (f5->light_family[size_t(F::kObject)] != 65535 || f5->light_family[size_t(F::kPlant)] != 0 ||
        f5->light_multi != 0 || f5->light_reason[size_t(LightReason::kLightUnknown)] != 3 ||
        f5->light_reason[size_t(LightReason::kNormalUnsupported)] != 0) {
      return 155;
    }
    lb.Open();
    lb.Add(plain, nullptr, &plant);
    auto f6 = lb.Finish(6);
    if (f6->lights.size() != 1 || f6->draws[0].light_index != 0) return 156;
  }

  // --- the frame's light block: the first valid one offered inside the scene ---
  {
    capture::LightBlock day{};
    day.valid = true;
    day.sun_dir[0] = 0.5f;
    day.sun_col[2] = 0.25f;
    capture::LightBlock later = day;
    later.sun_dir[0] = -0.75f;
    const capture::LightBlock zero{};
    if (render::FrameScene{}.light.valid || render::FrameScene{}.light_tries != 0) return 160;
    render::FrameBuilder bb;
    bb.SetLight(day);       // outside the bracket: not the scene's
    bb.CountLightTry();
    if (bb.HasLight()) return 161;
    bb.Open();
    if (bb.HasLight()) return 162;
    bb.CountLightTry();
    bb.SetLight(zero);      // a read that gave no block
    if (bb.HasLight()) return 163;
    bb.CountLightTry();
    bb.SetLight(day);
    if (!bb.HasLight()) return 164;
    bb.SetLight(later);     // the frame keeps its first
    bb.SetLight(zero);
    auto f = bb.Finish(1);
    if (!f->light.valid || f->light.sun_dir[0] != 0.5f || f->light.sun_col[2] != 0.25f || f->light_tries != 2) return 165;
    // The next frame has none until one is offered: never the frame before's.
    if (bb.HasLight()) return 166;
    bb.Open();
    bb.Add(plain);
    capture::LightBlock junk = day;  // not valid, whatever else it holds: nothing of it is kept
    junk.valid = false;
    bb.SetLight(junk);
    auto g = bb.Finish(2);
    if (g->light.valid || std::memcmp(&g->light, &zero, sizeof(zero)) != 0 || g->light_tries != 0) return 167;
    if (!f->light.valid) return 168;
    bb.Open();
    bb.SetLight(later);
    auto h = bb.Finish(3);
    if (!h->light.valid || h->light.sun_dir[0] != -0.75f || h->light_tries != 0) return 169;
  }

  // --- reuse: the light array circulates with the records and the terrain array ---
  // Frame `id`: `terrain` terrain records (patch id * 1000 + i), then `lit` plain records with
  // inputs (mark id * 1000 + i), then `dark` plain ones; each record's count is the frame's id.
  auto build = [&](render::FrameBuilder& b, uint32_t id, uint32_t terrain, uint32_t lit, uint32_t dark) {
    capture::DrawRecord g = ground, o = plain;
    g.count = o.count = id;
    b.Open();
    for (uint32_t i = 0; i < terrain; ++i) {
      const TerrainPatch p = patch(float(id * 1000 + i));
      b.Add(g, &p);
    }
    for (uint32_t i = 0; i < lit; ++i) {
      const LightInputs l = inputs(LightFamily::kObject, id * 1000 + i);
      b.Add(o, nullptr, &l);
    }
    for (uint32_t i = 0; i < dark; ++i) b.Add(o);
    return b.Finish(id);
  };
  auto whole = [&](const render::FrameScene& sc, uint32_t id, uint32_t terrain, uint32_t lit, uint32_t dark) {
    if (sc.draws.size() != terrain + lit + dark || sc.terrain.size() != terrain || sc.lights.size() != lit) return false;
    for (uint32_t i = 0; i < terrain + lit + dark; ++i) {
      const TerrainPatch* t = TerrainOf(sc, sc.draws[i]);
      const LightInputs* l = LightOf(sc, sc.draws[i]);
      if (sc.draws[i].count != id) return false;
      if (i < terrain ? (!t || t->patch != float(id * 1000 + i)) : t != nullptr) return false;
      const bool is_lit = i >= terrain && i < terrain + lit;
      if (is_lit ? (!l || sc.draws[i].light_index != i - terrain || l->colour_first != id * 1000 + (i - terrain)) : l != nullptr) {
        return false;
      }
    }
    return true;
  };
  const long long live_before = g_live.load();
  {
    render::FrameBuilder rb;
    auto a = build(rb, 1, 3, 5, 2);
    const LightInputs* a_lights = a->lights.data();
    const capture::DrawRecord* a_draws = a->draws.data();
    const size_t a_lcap = a->lights.capacity();
    auto b = build(rb, 2, 3, 5, 2);
    if (b->lights.data() == a_lights || rb.spare_count() != 0) return 170;
    a.reset();
    if (rb.spare_count() != 1) return 171;
    auto c = build(rb, 3, 2, 2, 1);
    if (c->lights.data() == a_lights || c->lights.data() == b->lights.data()) return 172;
    auto d = build(rb, 4, 1, 2, 0);
    // Frame 4 is in the three buffers of frame 1, at their capacities, and holds only itself.
    if (d->lights.data() != a_lights || d->draws.data() != a_draws || d->lights.capacity() != a_lcap) return 173;
    if (!whole(*d, 4, 1, 2, 0) || !whole(*b, 2, 3, 5, 2) || !whole(*c, 3, 2, 2, 1)) return 174;
    b.reset();
    c.reset();
    d.reset();
    if (rb.spare_count() != 3) return 175;
    const long long live = g_live.load();
    rb.ReleaseSpares();  // three buffers a pair now
    if (rb.spare_count() != 0 || g_live.load() != live - 9) return 176;
    // Buffers that hold only light inputs (no record survived) are kept like any other.
    render::SceneBufferPool pool;
    render::SceneBufferPool::Buffers only;
    only.lights.resize(3);
    pool.Return(std::move(only));
    if (pool.size() != 1 || pool.Take().lights.size() != 3 || pool.size() != 0) return 177;
    pool.Return(render::SceneBufferPool::Buffers{});  // nothing to keep
    if (pool.size() != 0) return 178;
  }
  if (g_live.load() != live_before) return 179;

  // --- a frame that grows a little at a time does not allocate each time ---
  // The next frame's buffers are sized by the frame before with half as much again, so a frame
  // creeping up by one record (as the game's does: a few draws more or fewer every frame) finds
  // room. Three scenes alive, as in the game; the frame has 30 terrain records and 200 lit
  // records to begin with and one more lit record every 5 frames, up to 290. After the warm-up
  // each buffer in circulation was allocated with room for 300, except the first frame's own
  // (grown by appending): at most that one is replaced, once for the records and once for the
  // light inputs. (Sized by the frame before exactly, every step of the creep allocates.)
  {
    render::FrameBuilder cb;
    std::shared_ptr<render::FrameScene> ring[3];
    long long after_warm_up = 0;
    size_t largest = 0;
    for (uint32_t f = 0; f < 480; ++f) {
      if (f == 30) after_warm_up = g_buffers.load();
      const uint32_t lit = f < 30 ? 200 : 200 + (f - 30) / 5;
      ring[f % 3] = build(cb, f + 1, 30, lit, 0);
      if (!whole(*ring[f % 3], f + 1, 30, lit, 0)) return 180;
      largest = std::max(largest, ring[f % 3]->draws.capacity());
    }
    const long long allocations = g_buffers.load() - after_warm_up;
    if (allocations > 2) {
      std::cout << "creeping frame: " << allocations << " buffer allocations after the warm-up\n";
      return 181;
    }
    // The capacity stays within the largest frame and its half (the vector's own growth is 1.5 too).
    if (largest > (30 + 290) * 3 / 2 + 1) return 182;
  }
  if (g_live.load() != live_before) return 183;

  // --- the pool under threads ---
  // A: the pool alone. Three threads take a set of buffers, check that every entry carries the
  // set's one marker (before and after a pause: the set is theirs alone), and give it back; one
  // of them frees the spares now and then. Without the pool's lock two threads get the same
  // buffers, or the list's count runs past its slots.
  {
    render::SceneBufferPool pool;
    std::atomic<uint32_t> next_id{1}, bad{0};
    auto worker = [&](int who) {
      for (int i = 0; i < 40000; ++i) {
        render::SceneBufferPool::Buffers b = pool.Take();
        if (b.draws.capacity() == 0) {
          const uint32_t id = next_id.fetch_add(1);
          b.draws.resize(4);
          b.terrain.resize(2);
          b.lights.resize(3);
          for (auto& d : b.draws) d.count = id;
          for (auto& t : b.terrain) t.patches = id;
          for (auto& l : b.lights) l.colour_first = id;
        }
        const uint32_t id = b.draws.empty() ? 0 : b.draws[0].count;
        for (int pass = 0; pass < 2; ++pass) {
          if (b.draws.size() != 4 || b.terrain.size() != 2 || b.lights.size() != 3) {
            ++bad;
            break;
          }
          for (auto& d : b.draws) bad += d.count != id;
          for (auto& t : b.terrain) bad += t.patches != id;
          for (auto& l : b.lights) bad += l.colour_first != id;
          if (who == 0 && (i & 63) == 0) std::this_thread::yield();
        }
        if (who == 2 && (i % 1000) == 0) pool.Release();
        pool.Return(std::move(b));
        if (pool.size() > render::SceneBufferPool::kMaxSpares) ++bad;
      }
    };
    std::thread t0(worker, 0), t1(worker, 1), t2(worker, 2);
    t0.join();
    t1.join();
    t2.join();
    if (bad.load() != 0) return 190;
  }
  if (g_live.load() != live_before) return 191;
  // B: builder and publisher with two readers that drop each scene at once (often its last
  // reference), while the capture side frees the spares now and then (the view going off and on).
  {
    render::FrameBuilder rb;
    render::ScenePublisher shared;
    std::atomic<bool> stop{false};
    std::atomic<uint32_t> bad{0}, seen{0};
    auto sizes = [](uint32_t id, uint32_t* terrain, uint32_t* lit) {
      *terrain = 20 + id % 7;
      *lit = 5 + id % 3;
    };
    auto reader = [&] {
      while (!stop.load()) {
        std::shared_ptr<const render::FrameScene> s = shared.Latest();
        if (!s) continue;
        uint32_t terrain, lit;
        sizes(uint32_t(s->frame), &terrain, &lit);
        if (!whole(*s, uint32_t(s->frame), terrain, lit, 3)) ++bad;
        ++seen;
        s.reset();
      }
    };
    std::thread t1(reader), t2(reader);
    for (uint32_t id = 1; id <= 6000; ++id) {
      uint32_t terrain, lit;
      sizes(id, &terrain, &lit);
      shared.Publish(build(rb, id, terrain, lit, 3));
      if (id % 97 == 0) rb.ReleaseSpares();
      if (rb.spare_count() > render::SceneBufferPool::kMaxSpares) ++bad;
    }
    while (seen.load() == 0) std::this_thread::yield();
    stop = true;
    t1.join();
    t2.join();
    shared.Publish(nullptr);
    if (bad.load() != 0) return 192;
  }
  if (g_live.load() != live_before) return 193;
  // C: the builder dies on one thread while another drops its scenes. Each scene's buffers go to
  // the pool while it lives and are freed by the scene itself afterwards; nothing is lost or
  // freed twice.
  {
    for (int round = 0; round < 400; ++round) {
      auto rb = std::make_unique<render::FrameBuilder>();
      std::shared_ptr<render::FrameScene> held[6];
      for (uint32_t i = 0; i < 6; ++i) held[i] = build(*rb, 60 + i, 2, 4, 2);
      std::atomic<bool> go{false};
      std::thread t([&] {
        while (!go.load()) {
        }
        for (auto& h : held) h.reset();
      });
      go = true;
      rb.reset();
      t.join();
    }
  }
  if (g_live.load() != live_before) return 194;
  return 0;
}

// The two shadow passes' record lists, their casters, the frame's shadow block and its tallies
// (FrameScene::shadow_draws, ::shadow_casters, ::shadow, ...). `r`: a drawable record. Returns 0
// or the number of the case that failed.
static int ShadowTests(const capture::DrawRecord& r) {
  using capture::CasterKind;
  using capture::ShadowBlock;
  using capture::ShadowBlockReason;
  using capture::ShadowCaster;
  using capture::ShadowSkip;
  using render::kMaxShadowRecords;
  // An orthographic light and the registers that hold it (tests/test_shadow_matrices.py's numbers).
  auto light = [](float m[4][4], float tx, float ty, float scale) {
    const float v[4][4] = {{0.052114f * scale, 0.0f, 0.041577f * scale, tx},
                           {0.000898f * scale, 0.066651f * scale, -0.001126f * scale, ty},
                           {0.005422f, -0.000188f, -0.006796f, 0.737847f},
                           {0.0f, 0.0f, 0.0f, 1.0f}};
    std::memcpy(m, v, sizeof(v));
  };
  float near[4][4], far[4][4];
  light(near, -6.732423f, -10.841799f, 1.0f);
  light(far, -3.123047f, -4.865234f, 30.0f / 70.0f);
  auto registers = [&] {
    float reg[256][4] = {};
    bool have[256] = {};
    const float c12[4] = {20.0f, 35.0f, 2.0f, 0.486f};
    std::memcpy(reg[12], c12, sizeof(c12));
    reg[13][0] = reg[14][0] = 1.0f / 1024.0f;
    const float half = 0.5f / 1024.0f;
    for (int c = 0; c < 2; ++c) {
      const float (*l)[4] = c ? far : near;
      for (int j = 0; j < 4; ++j) {
        reg[36 + 4 * c][j] = 0.5f * l[0][j] + (0.5f + half) * l[3][j];
        reg[37 + 4 * c][j] = -0.5f * l[1][j] + (0.5f + half) * l[3][j];
        reg[38 + 4 * c][j] = l[2][j];
        reg[39 + 4 * c][j] = l[3][j];
      }
    }
    for (uint32_t i : capture::kShadowBlockRegisters) have[i] = true;
    return capture::MakeShadowBlock(reg, have);
  };
  const ShadowBlock reg = registers();
  if (!reg.valid) return 1;
  auto caster = [](CasterKind kind, bool biased, float scale, float offset) {
    ShadowCaster c;
    c.kind = kind;
    c.biased = biased;
    c.bias_scale = scale;
    c.bias_offset = offset;
    return c;
  };
  const ShadowCaster near_caster = caster(CasterKind::kOpaque, true, -32.0f, -0.001f);
  const ShadowCaster far_caster = caster(CasterKind::kAlphaTest, true, -80.0f, -0.005f);
  // (the record comes with side indices and a seq of its own: AddShadow keeps none of them)
  auto record = [&](uint32_t mark, uint64_t vs = 0xF160) {
    capture::DrawRecord d = r;
    d.count = mark;
    d.vs_hash = vs;
    d.terrain_index = 3;
    d.light_index = 5;
    d.seq = 777;
    return d;
  };
  // A frame as the capture makes it: `n` near and `m` far records, the first of each with its
  // pass's matrix, and the registers at the first scene draw.
  auto frame = [&](render::FrameBuilder& b, uint32_t id, uint32_t n, uint32_t m) {
    for (uint32_t i = 0; i < n; ++i) b.AddShadow(0, record(1000 * id + i), near_caster, 0, i == 0 ? near : nullptr);
    for (uint32_t i = 0; i < m; ++i) b.AddShadow(1, record(2000 * id + i), far_caster, 0, i == 0 ? far : nullptr);
    b.CountShadowPass();
    b.CountShadowPass();
    b.SetShadowRegisters(reg);
    return b.Finish(id);
  };
  const long long live_before = g_live.load();
  {
    // Shadows off (the builder's state at first): nothing is taken, the block says "off".
    render::FrameBuilder b;
    if (b.AddShadow(0, record(1), near_caster, 0, near)) return 2;
    b.CountShadowExtra();
    b.CountShadowPass();
    b.SetShadowRegisters(reg);
    auto s = b.Finish(1);
    if (s->shadow.valid || s->shadow_reason != ShadowBlockReason::kOff || s->shadow_records) return 3;
    if (!s->shadow_draws[0].empty() || !s->shadow_draws[1].empty() || s->shadow_extra != 0 || s->shadow_passes != 0) return 4;
    if (render::ShadowLine(*s) != "off") return 5;

    // On: two passes, each list with its casters, record i with caster i; a valid block.
    b.ShadowsOn(true);
    s = frame(b, 2, 3, 4);
    if (!s->shadow_records || s->shadow_draws[0].size() != 3 || s->shadow_draws[1].size() != 4) return 6;
    if (s->shadow_casters[0].size() != 3 || s->shadow_casters[1].size() != 4) return 7;
    for (uint32_t i = 0; i < 3; ++i) {
      const capture::DrawRecord& d = s->shadow_draws[0][i];
      if (d.count != 2000 + i || d.seq != i || d.terrain_index != capture::kNoSideIndex || d.light_index != capture::kNoSideIndex) return 8;
      const ShadowCaster& c = s->shadow_casters[0][i];
      if (c.kind != CasterKind::kOpaque || !c.biased || c.bias_scale != -32.0f || c.bias_offset != -0.001f) return 9;
    }
    for (uint32_t i = 0; i < 4; ++i) {
      if (s->shadow_draws[1][i].count != 4000 + i || s->shadow_draws[1][i].seq != i) return 10;
      if (s->shadow_draws[1][i].terrain_index != capture::kNoSideIndex || s->shadow_draws[1][i].light_index != capture::kNoSideIndex) return 19;
      if (s->shadow_casters[1][i].kind != CasterKind::kAlphaTest || s->shadow_casters[1][i].bias_scale != -80.0f) return 11;
    }
    if (!s->shadow.valid || s->shadow_reason != ShadowBlockReason::kNone) return 12;
    if (std::memcmp(s->shadow.world_to_clip[0], near, 64) != 0 || std::memcmp(s->shadow.world_to_clip[1], far, 64) != 0) return 13;
    if (std::memcmp(s->shadow.world_to_map, reg.world_to_map, sizeof(reg.world_to_map)) != 0) return 14;
    if (s->shadow.bias[0][0] != -32.0f || s->shadow.bias[0][1] != -0.001f || s->shadow.bias[1][0] != -80.0f ||
        s->shadow.bias[1][1] != -0.005f || s->shadow.split[0] != 20.0f || s->shadow.texel[0] != 1.0f / 1024.0f) {
      return 15;
    }
    if (s->shadow_passes != 2 || s->shadow_extra != 0) return 16;
    // The shadow records are no scene records: the scene's own counts do not move.
    if (s->captured != 0 || !s->draws.empty() || s->lights.size() != 0) return 17;
    if (render::ShadowLine(*s) != "block valid, near 3 (skipped {}), far 4 (skipped {}), extra 0") {
      std::cout << "shadow line: " << render::ShadowLine(*s) << "\n";
      return 18;
    }

    // The next frame holds nothing of this one: no record, no caster, no block, no count.
    auto empty = b.Finish(3);
    if (!empty->shadow_records || !empty->shadow_draws[0].empty() || !empty->shadow_draws[1].empty() ||
        !empty->shadow_casters[0].empty() || !empty->shadow_casters[1].empty()) {
      return 20;
    }
    static const unsigned char zero[offsetof(ShadowBlock, valid)] = {};
    if (empty->shadow.valid || std::memcmp(&empty->shadow, zero, sizeof(zero)) != 0 ||
        empty->shadow_reason != ShadowBlockReason::kRegisters) {
      return 21;
    }
    if (empty->shadow_passes != 0 || empty->shadow_extra != 0 || empty->shadow_skipped_by_vs.size() != 0) return 22;
    for (int c = 0; c < 2; ++c) {
      for (uint32_t n : empty->shadow_skipped[c]) {
        if (n != 0) return 23;
      }
    }
    if (render::ShadowLine(*empty) != "block invalid(registers), near 0 (skipped {}), far 0 (skipped {}), extra 0") return 24;
    // ... and frame 2 is still whole while frame 4 is built in other buffers.
    auto next = frame(b, 4, 2, 1);
    if (s->shadow_draws[0].size() != 3 || s->shadow_draws[0][2].count != 2002 || s->shadow_casters[1][3].bias_scale != -80.0f) return 25;
    if (next->shadow_draws[0].data() == s->shadow_draws[0].data() || next->shadow_draws[0][1].count != 4001) return 26;
    // Reuse: when frame 2 is dropped its four shadow buffers come back for a later frame.
    const capture::DrawRecord* near_buffer = s->shadow_draws[0].data();
    const ShadowCaster* far_casters = s->shadow_casters[1].data();
    s.reset();
    if (b.spare_count() != 1) return 27;
    auto again = frame(b, 5, 2, 2);  // (at this frame's end the builder takes the spare set for the next one)
    auto later = frame(b, 6, 3, 4);
    if (again->shadow_draws[0].data() != near_buffer && later->shadow_draws[0].data() != near_buffer) return 28;
    if (again->shadow_casters[1].data() != far_casters && later->shadow_casters[1].data() != far_casters) return 29;
    if (later->shadow_draws[0].size() != 3 || later->shadow_draws[0][0].count != 6000 || later->shadow_draws[1][3].count != 12003) return 30;

    // The same by capacity, which no fresh allocation can imitate: a frame of 40 and 40 records is
    // dropped, and the frame after the next, with one record a pass, sits in its four buffers.
    {
      auto big = frame(b, 20, 40, 40);
      const size_t cap = big->shadow_draws[0].capacity(), caster_cap = big->shadow_casters[1].capacity();
      if (cap < 40 || caster_cap < 40) return 90;
      big.reset();
      auto one = frame(b, 21, 1, 1);
      auto two = frame(b, 22, 1, 1);
      if (two->shadow_draws[0].capacity() != cap || two->shadow_draws[1].capacity() < 40 ||
          two->shadow_casters[0].capacity() < 40 || two->shadow_casters[1].capacity() != caster_cap) {
        return 91;
      }
      if (two->shadow_draws[0].size() != 1 || two->shadow_draws[0][0].count != 22000 || !two->shadow.valid) return 92;
    }

    // The proof and the tallies.
    // The registers without the passes: no block. One pass only: none either.
    b.SetShadowRegisters(reg);
    auto f = b.Finish(7);
    if (f->shadow.valid || f->shadow_reason != ShadowBlockReason::kNoPass) return 31;
    b.AddShadow(0, record(1), near_caster, 0, near);
    b.SetShadowRegisters(reg);
    f = b.Finish(8);
    if (f->shadow.valid || f->shadow_reason != ShadowBlockReason::kNoPass || f->shadow_draws[0].size() != 1) return 32;
    // The passes without the registers.
    b.AddShadow(0, record(1), near_caster, 0, near);
    b.AddShadow(1, record(2), far_caster, 0, far);
    f = b.Finish(9);
    if (f->shadow.valid || f->shadow_reason != ShadowBlockReason::kRegisters) return 33;
    // Registers that are not valid are not kept, and do not replace valid ones; the first valid ones stay.
    b.AddShadow(0, record(1), near_caster, 0, near);
    b.AddShadow(1, record(2), far_caster, 0, far);
    b.SetShadowRegisters(ShadowBlock{});
    if (b.HasShadowRegisters()) return 34;
    b.SetShadowRegisters(reg);
    ShadowBlock other = reg;
    other.split[3] = 0.9f;
    b.SetShadowRegisters(other);
    b.SetShadowRegisters(ShadowBlock{});
    if (!b.HasShadowRegisters()) return 35;
    f = b.Finish(10);
    if (!f->shadow.valid || f->shadow.split[3] != 0.486f) return 36;
    if (b.HasShadowRegisters()) return 37;
    // A pass whose first matrix is another light's: the frame has no block ("proof"). The matrix
    // kept is the first one given, whatever comes later.
    b.AddShadow(0, record(1), near_caster, 0, far);
    b.AddShadow(0, record(2), near_caster, 0, near);
    b.AddShadow(1, record(3), far_caster, 0, far);
    b.SetShadowRegisters(reg);
    f = b.Finish(11);
    if (f->shadow.valid || f->shadow_reason != ShadowBlockReason::kProof || f->shadow_draws[0].size() != 2) return 38;
    if (render::ShadowLine(*f) != "block invalid(proof), near 2 (skipped {}), far 1 (skipped {}), extra 0") return 39;
    // A record given without a matrix first: the next one's is the pass's.
    b.AddShadow(0, record(1), near_caster, 0, nullptr);
    b.AddShadow(0, record(2), near_caster, 0, near);
    b.AddShadow(1, record(3), far_caster, 0, far);
    b.SetShadowRegisters(reg);
    if (!b.Finish(12)->shadow.valid) return 40;
    // Records of a pass with two biases: no block ("bias"). A record without the offset is no disagreement.
    b.AddShadow(0, record(1), near_caster, 0, near);
    b.AddShadow(0, record(2), far_caster, 0, nullptr);
    b.AddShadow(1, record(3), far_caster, 0, far);
    b.SetShadowRegisters(reg);
    f = b.Finish(13);
    if (f->shadow.valid || f->shadow_reason != ShadowBlockReason::kBias) return 41;
    b.AddShadow(0, record(1), near_caster, 0, near);
    b.AddShadow(0, record(2), caster(CasterKind::kAlphaTest, false, 0.0f, 0.0f), 0, nullptr);
    b.AddShadow(1, record(3), caster(CasterKind::kOpaque, false, 0.0f, 0.0f), 0, far);
    b.SetShadowRegisters(reg);
    f = b.Finish(14);
    if (!f->shadow.valid || f->shadow.bias[0][0] != -32.0f || f->shadow.bias[1][0] != 0.0f || f->shadow.bias[1][1] != 0.0f) return 42;
    if (f->shadow_casters[0][1].biased || f->shadow_casters[0][1].kind != CasterKind::kAlphaTest) return 43;

    // Skipped draws: by reason per pass and by vertex shader; a skipped draw leaves no record, no
    // caster, no matrix and no say in the bias. The reason given with the draw comes before the
    // record's own (the capture gives one only where it knows better: no caster entry).
    capture::DrawRecord bad = record(1, 0xBAD1);
    bad.skip = SkipReason::kBadIndex;
    if (b.AddShadow(0, bad, far_caster, 0, far)) return 44;
    if (b.AddShadow(0, bad, far_caster, uint8_t(ShadowSkip::kNoCaster), far)) return 45;
    if (b.AddShadow(1, record(2, 0xBAD2), near_caster, uint8_t(ShadowSkip::kNoCaster), near)) return 46;
    if (b.AddShadow(1, record(3, 0xBAD2), near_caster, uint8_t(ShadowSkip::kNoCaster), near)) return 47;
    if (b.AddShadow(1, record(4, 0xBAD3), near_caster, uint8_t(ShadowSkip::kAlphaInputs), near)) return 48;
    if (b.AddShadow(1, record(5, 0xBAD3), near_caster, uint8_t(ShadowSkip::kStateUnread), near)) return 49;
    if (b.AddShadow(1, record(6), near_caster, 200, near)) return 50;   // no reason of that number: skipped as bad-memory
    if (b.AddShadow(2, record(7), near_caster, 0, near)) return 51;     // no third cascade
    if (!b.AddShadow(0, record(8), near_caster, 0, near) || !b.AddShadow(1, record(9), far_caster, 0, far)) return 52;
    b.CountShadowExtra();
    b.CountShadowExtra();
    b.CountShadowPass();
    b.CountShadowPass();
    b.CountShadowPass();
    b.SetShadowRegisters(reg);
    f = b.Finish(15);
    if (!f->shadow.valid || f->shadow_draws[0].size() != 1 || f->shadow_draws[1].size() != 1) return 53;
    if (f->shadow_draws[0][0].count != 8 || f->shadow_draws[0][0].seq != 0 || f->shadow_casters[0].size() != 1) return 54;
    if (f->shadow_skipped[0][size_t(SkipReason::kBadIndex)] != 1 || f->shadow_skipped[0][size_t(ShadowSkip::kNoCaster)] != 1 ||
        f->shadow_skipped[1][size_t(ShadowSkip::kNoCaster)] != 2 || f->shadow_skipped[1][size_t(ShadowSkip::kAlphaInputs)] != 1 ||
        f->shadow_skipped[1][size_t(ShadowSkip::kStateUnread)] != 1 || f->shadow_skipped[1][size_t(SkipReason::kBadMemory)] != 1) {
      return 55;
    }
    if (f->shadow_skipped_by_vs.Total(uint8_t(SkipReason::kBadIndex)) != 1 ||
        f->shadow_skipped_by_vs.Total(uint8_t(ShadowSkip::kNoCaster)) != 3 || f->shadow_skipped_by_vs.size() != 6) {
      return 56;
    }
    if (f->shadow_extra != 2 || f->shadow_passes != 3) return 57;
    if (render::ShadowLine(*f) !=
        "block valid, near 1 (skipped {bad-index: 1, no-caster: 1}), far 1 (skipped {bad-memory: 1, no-caster: 2, "
        "alpha-inputs: 1, state-unread: 1}), extra 2 (shadow-pass-extra)") {
      std::cout << "shadow line: " << render::ShadowLine(*f) << "\n";
      return 58;
    }
    if (render::ShadowSkippedByVs(*f) != "0x000000000000F160(bad-memory): 1, 0x000000000000BAD1(bad-index): 1, "
                                          "0x000000000000BAD2(no-caster): 2, 0x000000000000BAD1(no-caster): 1, "
                                          "0x000000000000BAD3(alpha-inputs): 1, 0x000000000000BAD3(state-unread): 1") {
      std::cout << "skipped by vs: " << render::ShadowSkippedByVs(*f) << "\n";
      return 59;
    }

    // The limit: a pass with more records than a list holds. The rest is counted (over-limit),
    // the list does not grow past the limit, and the block is still the frame's.
    for (uint32_t i = 0; i < kMaxShadowRecords + 7; ++i) {
      const bool added = b.AddShadow(0, record(i), near_caster, 0, i == 0 ? near : nullptr);
      if (added != (i < kMaxShadowRecords)) return 60;
    }
    b.AddShadow(1, record(1), far_caster, 0, far);
    b.SetShadowRegisters(reg);
    f = b.Finish(16);
    if (f->shadow_draws[0].size() != kMaxShadowRecords || f->shadow_casters[0].size() != kMaxShadowRecords) return 61;
    if (f->shadow_draws[0].capacity() > kMaxShadowRecords || f->shadow_casters[0].capacity() > kMaxShadowRecords) {
      std::cout << "shadow list capacity " << f->shadow_draws[0].capacity() << "\n";
      return 62;
    }
    if (f->shadow_skipped[0][size_t(ShadowSkip::kOverLimit)] != 7 || f->shadow_draws[0].back().count != kMaxShadowRecords - 1) return 63;
    if (!f->shadow.valid || f->shadow_draws[1].size() != 1) return 64;
    // (and nothing of frame 15's tallies is in it)
    if (f->shadow_skipped[1][size_t(ShadowSkip::kNoCaster)] != 0 || f->shadow_skipped[0][size_t(SkipReason::kBadIndex)] != 0 ||
        f->shadow_skipped_by_vs.size() != 1 || f->shadow_extra != 0 || f->shadow_passes != 0) {
      return 69;
    }
    // The frame after it starts with room for the limit and no more.
    for (uint32_t i = 0; i < kMaxShadowRecords + 1; ++i) b.AddShadow(0, record(i), near_caster, 0, nullptr);
    auto g = b.Finish(17);
    if (g->shadow_draws[0].size() != kMaxShadowRecords || g->shadow_draws[0].capacity() > kMaxShadowRecords) return 65;

    // Shadows switched off again: nothing is taken from the next frame on.
    b.ShadowsOn(false);
    if (b.AddShadow(0, record(1), near_caster, 0, near)) return 66;
    f = b.Finish(18);
    if (f->shadow_records || f->shadow_reason != ShadowBlockReason::kOff || !f->shadow_draws[0].empty()) return 67;
  }
  if (g_live.load() != live_before) return 68;
  return 0;
}

// The frame's fog block, its source and the sky block (FrameScene::fog, ::fog_source, ::sky) with
// their reasons and the log's line. Returns 0 or the number of the case that failed.
static int FogTests(const capture::DrawRecord& r) {
  using capture::FogBlock;
  using capture::FogBlockReason;
  using capture::FogSource;
  using capture::SkyBlock;
  using capture::SkyBlockReason;
  // A block as MakeFogBlock gives it: registers of the bridge's shape, flat tables (the builder
  // does not judge a block, it carries it).
  auto fog = [](float start) {
    const float regs[6][4] = {{1.0f, 1.0f, 0.0f, 0.0f}, {0.0003f, 0.0004f, 0.001f, 0.0f}, {0.005f, 0.006f, 0.01f, 0.0f},
                              {0.0f, 0.6f, 0.8f, 0.0f}, {start, 1900.0f, 1.0f, 0.0005f}, {0.25f, 0.5f, 0.75f, 0.0f}};
    const uint32_t fc4[6] = {0x00804956, 0x1FAB505D, 0x0000003F, 0x00280D11, 0x00000003, 0x00000018};
    const uint32_t fc5[6] = {0x02004802, 0x1FAB4086, 0x000000FF, 0x00280C14, 0x00000003, 0x00000018};
    static uint8_t tf4[512], tf5[1024];
    for (int i = 0; i < 256; ++i) {
      tf4[2 * i] = 0x38;  // every half float 0.5
      tf4[2 * i + 1] = 0x00;
    }
    std::memset(tf5, 0xFF, sizeof(tf5));
    return capture::MakeFogBlock(regs, fc4, fc5, tf4, sizeof(tf4), tf5, sizeof(tf5));
  };
  auto sky = [](float c31x) {
    const float c31[4] = {c31x, 0, 0, 0}, c46[4] = {100.0f, 0, 0, 0}, c47[4] = {0.25f, 0, 0, 0};
    const uint32_t fc[6] = {0x00800002, 0x1A544086, 0x0001E00F, 0x00A80C14, 0x00000003, 0x00000218};
    return capture::MakeSkyBlock(c31, c46, c47, fc, fc);
  };
  const FogBlock first = fog(20.0f), second = fog(30.0f);
  const SkyBlock sky1 = sky(150.0f), sky2 = sky(160.0f);
  if (!first.valid || !second.valid || !sky1.valid || !sky2.valid) return 1;
  FogSource from1, from2;
  from1.base[0] = 0x1FAB5000;
  from1.base[1] = 0x1FAB4000;
  from1.content[0] = 0xE1CDB1ADBFB1321Cull;
  from1.content[1] = 0x591ADB7C80BECA41ull;
  from2.base[0] = 0x1FAB1000;
  from2.base[1] = 0x1FAB0000;
  from2.content[0] = 2;
  from2.content[1] = 3;
  // Nothing of a fog or sky block in a scene: every float zero, no source.
  auto none = [](const render::FrameScene& s) {
    static const unsigned char zero[sizeof(FogBlock)] = {};
    return !s.fog.valid && std::memcmp(&s.fog, zero, offsetof(FogBlock, valid)) == 0 && !s.sky.valid &&
           std::memcmp(&s.sky, zero, offsetof(SkyBlock, valid)) == 0 && s.fog_source.base[0] == 0 && s.fog_source.base[1] == 0 &&
           s.fog_source.content[0] == 0 && s.fog_source.content[1] == 0;
  };
  render::FrameBuilder b;
  {
    // Fog off (the builder's state at first): nothing is taken, the frame says "off".
    b.Open();
    b.Add(r);
    b.SetFog(first, from1);
    b.CountFogTry(FogBlockReason::kAxis);
    b.SetSky(sky1);
    b.CountSkyTry(SkyBlockReason::kRange);
    if (b.HasFog() || b.HasSky()) return 2;
    auto s = b.Finish(1);
    if (s->fog_records || !none(*s) || s->fog_reason != FogBlockReason::kOff || s->sky_reason != SkyBlockReason::kOff) return 2;
    if (s->fog_tries != 0 || s->sky_tries != 0 || render::FogLine(*s) != "off") return 3;
  }
  b.FogOn(true);
  {
    // On, a frame without a fogged draw and without a sky draw (a menu): no blocks, "no-draw".
    b.Open();
    b.Add(r);
    auto s = b.Finish(2);
    if (!s->fog_records || !none(*s) || s->fog_reason != FogBlockReason::kNoDraw || s->sky_reason != SkyBlockReason::kNoDraw) return 4;
    if (render::FogLine(*s) != "block invalid(no-draw), tries 0 | sky invalid(no-draw), tries 0") return 5;
  }
  {
    // The first valid block of a frame is the frame's, with its source; a later one is not taken,
    // and neither is anything offered outside the scene.
    b.SetFog(second, from2);  // before the scene opens
    b.SetSky(sky2);
    b.CountFogTry(FogBlockReason::kUnread);
    b.CountSkyTry(SkyBlockReason::kUnread);
    b.Open();
    if (b.HasFog() || b.HasSky()) return 6;
    b.Add(r);
    b.SetFog(FogBlock{}, from2);  // an invalid block is no block
    b.SetSky(SkyBlock{});
    if (b.HasFog() || b.HasSky()) return 7;
    b.SetFog(first, from1);
    b.SetSky(sky1);
    if (!b.HasFog() || !b.HasSky()) return 8;
    b.SetFog(second, from2);
    b.SetSky(sky2);
    b.CountFogTry(FogBlockReason::kAxis);  // (a read after the block: not counted, the frame has its block)
    b.CountSkyTry(SkyBlockReason::kRange);
    auto s = b.Finish(3);
    if (!s->fog.valid || s->fog_reason != FogBlockReason::kNone || std::memcmp(&s->fog, &first, offsetof(FogBlock, valid)) != 0) return 9;
    if (s->fog.range[0] != 20.0f || s->fog_tries != 0) return 10;
    if (s->fog_source.base[0] != 0x1FAB5000 || s->fog_source.base[1] != 0x1FAB4000 ||
        s->fog_source.content[0] != 0xE1CDB1ADBFB1321Cull || s->fog_source.content[1] != 0x591ADB7C80BECA41ull) {
      return 11;
    }
    if (!s->sky.valid || s->sky_reason != SkyBlockReason::kNone || s->sky.c31x != 150.0f || s->sky_tries != 0) return 12;
    const std::string line = render::FogLine(*s);
    if (line != "block valid, tries 0, c28 (1, 1) c66 (0, 0.6, 0.8) c67 (20, 1900, 1, 0.0005) c68 (0.25, 0.5, 0.75) "
                "tf4 0x1FAB5000 E1CDB1ADBFB1321C tf5 0x1FAB4000 591ADB7C80BECA41 | sky valid, tries 0, c31.x 150 c46.x 100 "
                "c47.x 0.25 tf13 0x1A544086 tf14 0x1A544086") {
      std::cout << line << "\n";
      return 13;
    }
  }
  {
    // Review Focus 1: the next frame starts with nothing of the last one. Reads that gave no block
    // are counted and the last one's reason is the frame's.
    b.Open();
    b.Add(r);
    b.CountFogTry(FogBlockReason::kUnread);
    b.CountFogTry(FogBlockReason::kScatterTexel);
    b.CountSkyTry(SkyBlockReason::kRegister);
    auto s = b.Finish(4);
    if (!s->fog_records || !none(*s)) return 14;
    if (s->fog_reason != FogBlockReason::kScatterTexel || s->fog_tries != 2) return 15;
    if (s->sky_reason != SkyBlockReason::kRegister || s->sky_tries != 1) return 16;
    if (render::FogLine(*s) != "block invalid(tf4-texel), tries 2 | sky invalid(register), tries 1") return 17;
    // A read that fails and one that then gives the block: the frame has the block and the count.
    b.Open();
    b.Add(r);
    b.CountFogTry(FogBlockReason::kUnread);
    b.SetFog(second, from2);
    auto t = b.Finish(5);
    if (!t->fog.valid || t->fog_reason != FogBlockReason::kNone || t->fog_tries != 1 || t->fog.range[0] != 30.0f) return 18;
    if (t->fog_source.content[0] != 2 || t->sky.valid || t->sky_reason != SkyBlockReason::kNoDraw) return 19;
    // And the frame after it has none of it.
    b.Open();
    auto u = b.Finish(6);
    if (!none(*u) || u->fog_reason != FogBlockReason::kNoDraw || u->fog_tries != 0) return 20;
  }
  {
    // Switched off at a frame boundary with a block held: that frame has none, and neither has
    // the next one with the switch on again.
    b.Open();
    b.SetFog(first, from1);
    b.SetSky(sky1);
    b.FogOn(false);
    auto s = b.Finish(7);
    if (s->fog_records || !none(*s) || s->fog_reason != FogBlockReason::kOff || s->sky_reason != SkyBlockReason::kOff) return 21;
    b.FogOn(true);
    b.Open();
    auto t = b.Finish(8);
    if (!t->fog_records || !none(*t) || t->fog_reason != FogBlockReason::kNoDraw) return 22;
  }
  return 0;
}

// The frame's post block (FrameScene::post, ::post_age, ::post_source) with its reasons, the block
// kept across frames and the log's line. Returns 0 or the number of the case that failed.
static int PostTests(const capture::DrawRecord& r) {
  using capture::PostBlock;
  using capture::PostBlockReason;
  using capture::PostSource;
  // A block as MakePostBlock gives it, of a scene whose output scale is `scale` (the bright pass's
  // c77.y is its reciprocal), with a flat table of `texel` (the builder does not judge a block's
  // values; it pairs it with the frame's light block and carries it).
  auto post = [](float scale, float texel) {
    capture::PostRegisters regs{};
    const float composite[5][4] = {{3.0f}, {0.25f}, {50.0f}, {-0.005f, 1.1f}, {0.4545f}};
    const float bright[4][4] = {{1.5f}, {0.2f}, {2.0f}, {0.087f, 1.0f / scale}};
    std::memcpy(regs.composite, composite, sizeof(composite));
    std::memcpy(regs.bright, bright, sizeof(bright));
    regs.have_composite = regs.have_bright = true;
    const uint32_t fc[6] = {0x04000956, 0x1FAA00A4, 0x000001FF, 0x010016D1, 0x00000000, 0x00000018};
    static uint8_t table[2048];
    uint32_t bits;
    std::memcpy(&bits, &texel, sizeof(bits));
    for (int i = 0; i < 512; ++i) {
      for (int k = 0; k < 4; ++k) table[4 * i + k] = uint8_t(bits >> (24 - 8 * k));
    }
    return capture::MakePostBlock(regs, fc, table, sizeof(table));
  };
  auto light = [](float scale) {
    capture::LightBlock l{};
    l.out_scale = scale;
    l.valid = true;
    return l;
  };
  const PostBlock eight = post(8.0f, 0.25f), ten = post(10.0f, 0.5f);
  if (!eight.valid || !ten.valid || eight.parts != 0) return 1;
  const PostSource from8{0x1FAA0000, 0x9075910DFD72DC3Dull}, from10{0x1FAA0000, 7};
  // Nothing of a post block in a scene: every float zero, no part, no source, age 0.
  auto none = [](const render::FrameScene& s) {
    static const unsigned char zero[sizeof(PostBlock)] = {};
    return !s.post.valid && s.post.parts == 0 && std::memcmp(&s.post, zero, offsetof(PostBlock, parts)) == 0 &&
           s.post_source.base == 0 && s.post_source.content == 0 && s.post_age == 0;
  };
  auto has = [](const render::FrameScene& s, const PostBlock& b, const PostSource& from, uint32_t age) {
    return s.post.valid && s.post_reason == PostBlockReason::kNone && s.post_age == age &&
           std::memcmp(&s.post, &b, offsetof(PostBlock, valid)) == 0 && s.post_source.base == from.base &&
           s.post_source.content == from.content;
  };
  // One frame: a main scene with a record and, when `scale` is not 0, a light block of that scale;
  // then, outside the scene, where the game issues its post rectangles, what `after` does.
  uint64_t number = 100;
  auto frame = [&](render::FrameBuilder& b, float scale, auto after) {
    b.Open();
    b.Add(r);
    if (scale != 0.0f) b.SetLight(light(scale));
    b.Close();
    after(b);
    return b.Finish(++number);
  };
  auto nothing = [](render::FrameBuilder&) {};
  render::FrameBuilder b;
  {
    // Post off (the builder's state at first): nothing is taken, the frame says "off".
    auto s = frame(b, 8.0f, [&](render::FrameBuilder& fb) {
      fb.SetPost(eight, from8);
      fb.CountPostTry(PostBlockReason::kRange);
      if (fb.HasPost()) std::abort();
    });
    if (s->post_records || !none(*s) || s->post_reason != PostBlockReason::kOff || s->post_tries != 0) return 2;
    if (render::PostLine(*s) != "off") return 3;
    // ... and nothing of it was kept for the frames after the switch.
    b.PostOn(true);
    auto t = frame(b, 8.0f, nothing);
    if (!t->post_records || !none(*t) || t->post_reason != PostBlockReason::kNoDraw || t->post_tries != 0) return 4;
    if (render::PostLine(*t) != "block invalid(no-draw), tries 0") return 5;
  }
  {
    // The frame's own block, read outside the main scene, pairing with the frame's light block: the
    // frame's, at age 0, with its source. The first valid one is kept; an invalid one is none; a
    // read after the block is not counted.
    auto s = frame(b, 8.0f, [&](render::FrameBuilder& fb) {
      fb.SetPost(PostBlock{}, from10);
      if (fb.HasPost()) std::abort();
      fb.CountPostTry(PostBlockReason::kUnread);
      fb.SetPost(eight, from8);
      if (!fb.HasPost()) std::abort();
      fb.SetPost(ten, from10);
      fb.CountPostTry(PostBlockReason::kRange);
    });
    if (!s->post_records || !has(*s, eight, from8, 0) || s->post_tries != 1) return 6;
    const std::string line = render::PostLine(*s);
    if (line != "block valid, age 0, tries 1, c20.w 8, 37B1 (3, 0.25, 50, -0.005, 1.1, 0.4545), EE92 (1.5, 0.2, 2, 0.087, 0.125), "
                "table 0x1FAA0000 9075910DFD72DC3D, parts 0") {
      std::cout << line << "\n";
      return 7;
    }
    // Kept: the frames after it that read none take it at ages 1 to 4 (kPostBlockMaxAge) while it
    // pairs with their own light block; the fifth has none ("stale"), and after that nothing is
    // kept any more ("no-draw").
    for (uint32_t age = 1; age <= 6; ++age) {
      auto t = frame(b, 8.0f, nothing);
      if (!t->post_records || t->post_tries != 0) return 8;
      if (age <= capture::kPostBlockMaxAge) {
        if (!has(*t, eight, from8, age)) return 9;
        if (age == 2 && render::PostLine(*t).rfind("block valid, age 2, tries 0, c20.w 8, ", 0) != 0) return 10;
      } else if (!none(*t) || t->post_reason != (age == 5 ? PostBlockReason::kStale : PostBlockReason::kNoDraw)) {
        return 11;
      }
    }
    if (render::PostLine(*frame(b, 8.0f, nothing)) != "block invalid(no-draw), tries 0") return 12;
  }
  {
    // Kept and not pairing: the scene's scale moved away from the kept block's. None, "pairing";
    // the block is still kept, and serves a later frame it pairs with, at its age.
    (void)frame(b, 8.0f, [&](render::FrameBuilder& fb) { fb.SetPost(eight, from8); });
    auto s = frame(b, 10.0f, nothing);
    if (!none(*s) || s->post_reason != PostBlockReason::kPairing) return 13;
    if (render::PostLine(*s) != "block invalid(pairing), tries 0") return 14;
    auto t = frame(b, 8.004f, nothing);  // within 1e-3
    if (!has(*t, eight, from8, 2)) return 15;
    auto u = frame(b, 8.01f, nothing);  // outside it
    if (!none(*u) || u->post_reason != PostBlockReason::kPairing) return 16;
  }
  {
    // The frame's own block does not pair with the frame's scene: the kept one if that does (never
    // the mixed pair); and the block that was read is the one kept from then on.
    (void)frame(b, 8.0f, [&](render::FrameBuilder& fb) { fb.SetPost(eight, from8); });
    auto s = frame(b, 8.0f, [&](render::FrameBuilder& fb) { fb.SetPost(ten, from10); });
    if (!has(*s, eight, from8, 1)) return 17;
    auto t = frame(b, 8.0f, nothing);
    if (!none(*t) || t->post_reason != PostBlockReason::kPairing) return 18;  // (kept: ten, age 1)
    auto u = frame(b, 10.0f, nothing);
    if (!has(*u, ten, from10, 2)) return 19;
    // Its own does not pair and nothing kept serves: "pairing".
    render::FrameBuilder fresh;
    fresh.PostOn(true);
    auto v = frame(fresh, 8.0f, [&](render::FrameBuilder& fb) { fb.SetPost(ten, from10); });
    if (!none(*v) || v->post_reason != PostBlockReason::kPairing || !v->post_records) return 20;
  }
  {
    // A frame without a valid light block has no post block, its own or a kept one.
    render::FrameBuilder fresh;
    fresh.PostOn(true);
    auto s = frame(fresh, 0.0f, [&](render::FrameBuilder& fb) { fb.SetPost(eight, from8); });
    if (s->light.valid || !none(*s) || s->post_reason != PostBlockReason::kPairing) return 21;
    auto t = frame(fresh, 0.0f, nothing);
    if (!none(*t) || t->post_reason != PostBlockReason::kPairing) return 22;
    auto u = frame(fresh, 8.0f, nothing);  // the block read two frames ago serves the next lit frame
    if (!has(*u, eight, from8, 2)) return 23;
    // A light block that is not valid gives no scale, whatever its field says.
    render::FrameBuilder other;
    other.PostOn(true);
    other.Open();
    capture::LightBlock invalid{};
    invalid.out_scale = 8.0f;
    other.SetLight(invalid);
    other.Close();
    other.SetPost(eight, from8);
    auto v = other.Finish(1);
    if (!none(*v) || v->post_reason != PostBlockReason::kPairing) return 24;
  }
  {
    // Reads that gave no block are counted and the last one's reason is the frame's; with a kept
    // block that serves, the frame has that block and the count.
    render::FrameBuilder fresh;
    fresh.PostOn(true);
    auto s = frame(fresh, 8.0f, [&](render::FrameBuilder& fb) {
      fb.CountPostTry(PostBlockReason::kUnread);
      fb.CountPostTry(PostBlockReason::kTableTexel);
    });
    if (!none(*s) || s->post_reason != PostBlockReason::kTableTexel || s->post_tries != 2) return 25;
    if (render::PostLine(*s) != "block invalid(table-texel), tries 2") return 26;
    (void)frame(fresh, 8.0f, [&](render::FrameBuilder& fb) { fb.SetPost(eight, from8); });
    auto t = frame(fresh, 8.0f, [&](render::FrameBuilder& fb) { fb.CountPostTry(PostBlockReason::kRegister); });
    if (!has(*t, eight, from8, 1) || t->post_tries != 1) return 27;
    // The next frame starts with no try of the last one.
    auto u = frame(fresh, 8.0f, nothing);
    if (!has(*u, eight, from8, 2) || u->post_tries != 0) return 28;
    // Failed reads and a kept block that is too old: the read's reason, not "stale".
    (void)frame(fresh, 8.0f, nothing);
    (void)frame(fresh, 8.0f, nothing);
    auto v = frame(fresh, 8.0f, [&](render::FrameBuilder& fb) { fb.CountPostTry(PostBlockReason::kRange); });
    if (!none(*v) || v->post_reason != PostBlockReason::kRange || v->post_tries != 1) return 29;
  }
  {
    // Switched off at a frame boundary with a block read and one kept: that frame has none
    // ("off"), and neither has the next one with the switch on again (nothing is kept across a
    // stretch in which nothing was read).
    render::FrameBuilder fresh;
    fresh.PostOn(true);
    (void)frame(fresh, 8.0f, [&](render::FrameBuilder& fb) { fb.SetPost(eight, from8); });
    auto s = frame(fresh, 8.0f, [&](render::FrameBuilder& fb) {
      fb.SetPost(eight, from8);
      fb.PostOn(false);
    });
    if (s->post_records || !none(*s) || s->post_reason != PostBlockReason::kOff) return 30;
    fresh.PostOn(true);
    auto t = frame(fresh, 8.0f, nothing);
    if (!t->post_records || !none(*t) || t->post_reason != PostBlockReason::kNoDraw) return 31;
  }
  {
    // The blur part travels with the block.
    capture::PostRegisters regs{};
    const float composite[5][4] = {{3.0f}, {0.25f}, {50.0f}, {-0.005f, 1.1f}, {0.4545f}};
    const float bright[4][4] = {{1.5f}, {0.2f}, {2.0f}, {0.087f, 0.125f}};
    std::memcpy(regs.composite, composite, sizeof(composite));
    std::memcpy(regs.bright, bright, sizeof(bright));
    for (int k = 0; k < 2; ++k) {
      regs.blur[k][0][0] = 0.5f;
      regs.blur[k][0][1] = 0.25f;
      regs.have_blur[k] = true;
    }
    regs.have_composite = regs.have_bright = true;
    const uint32_t fc[6] = {0x04000956, 0x1FAA00A4, 0x000001FF, 0x010016D1, 0x00000000, 0x00000018};
    static const uint8_t table[2048] = {};
    const PostBlock blurred = capture::MakePostBlock(regs, fc, table, sizeof(table));
    render::FrameBuilder fresh;
    fresh.PostOn(true);
    auto s = frame(fresh, 8.0f, [&](render::FrameBuilder& fb) { fb.SetPost(blurred, from8); });
    if (!s->post.valid || s->post.parts != capture::kPostPartBlur || s->post.blur[1][1] != 0.25f) return 32;
    if (render::PostLine(*s).find(", parts 1") == std::string::npos) return 33;
  }
  return 0;
}

// The frame's quad records (FrameScene::quads, ::quad_count, ::quad_reason): a whole set or none of
// it, the bound, the refusals, a record that never arrived, the log's line. Returns 0 or the number
// of the case that failed.
static int QuadTests(const capture::DrawRecord& r) {
  using capture::PostQuadKind;
  using capture::QuadRecord;
  using capture::QuadRecordReason;
  using capture::QuadSetReason;
  static_assert(capture::kMaxQuadRecords == 24);
  // A record as MakeQuadRecord gives it, of `kind`, told apart by `mark` (the builder does not
  // judge a record's values; it carries it and gives it its place).
  auto quad = [](PostQuadKind kind, float mark) {
    QuadRecord q{};
    q.ps_hash = capture::kPostQuadTable[size_t(kind)].ps;
    q.vs_hash = capture::kPostQuadTable[size_t(kind)].vs;
    q.kind = kind;
    q.layer = capture::kPostQuadTable[size_t(kind)].layer;
    q.sequence = 999;  // (the builder's to set)
    q.corners[0][0] = mark;
    return q;
  };
  // Nothing of a quad in a scene.
  auto none = [](const render::FrameScene& s) {
    return s.quad_count == 0 && s.quads.empty();
  };
  uint64_t number = 300;
  // One frame: a main scene with a record and what `in_scene` does in it; then, outside the scene,
  // where the game draws its sun layer, what `after` does.
  auto frame = [&](render::FrameBuilder& b, auto in_scene, auto after) {
    b.Open();
    b.Add(r);
    in_scene(b);
    b.Close();
    after(b);
    return b.Finish(++number);
  };
  auto nothing = [](render::FrameBuilder&) {};
  // A furniture call and, as the capture does it one hooked draw later, its record.
  auto whole = [&](render::FrameBuilder& fb, PostQuadKind kind, float mark) {
    fb.QuadSeen(kind);
    fb.AddQuad(quad(kind, mark), QuadRecordReason::kNone);
  };
  render::FrameBuilder b;
  {
    // Quads off (the builder's state at first): nothing is taken, the frame says "off".
    auto s = frame(b, [&](render::FrameBuilder& fb) { whole(fb, PostQuadKind::kMoon, 1.0f); }, nothing);
    if (s->quad_records || !none(*s) || s->quad_reason != QuadSetReason::kOff || s->quad_seen != 0) return 1;
    if (render::QuadLine(*s) != "off") return 2;
    b.QuadsOn(true);
    // ... and nothing of it was kept for the frame after the switch: a frame without a furniture call.
    auto t = frame(b, nothing, nothing);
    if (!t->quad_records || !none(*t) || t->quad_reason != QuadSetReason::kNoDraw || t->quad_seen != 0) return 3;
    if (render::QuadLine(*t) != "set none(no-draw), seen 0") return 4;
  }
  {
    // A whole set: the scene's kinds inside the scene, the layer's after it (the builder asks for no
    // open scene), each record at its place among the frame's, in the order of arrival.
    auto s = frame(
        b,
        [&](render::FrameBuilder& fb) {
          whole(fb, PostQuadKind::kMoon, 1.0f);
          whole(fb, PostQuadKind::kCloud, 2.0f);
          whole(fb, PostQuadKind::kCloud, 3.0f);
          whole(fb, PostQuadKind::kGlare, 4.0f);
        },
        [&](render::FrameBuilder& fb) {
          whole(fb, PostQuadKind::kBeamSource, 5.0f);
          whole(fb, PostQuadKind::kFlare, 6.0f);
          whole(fb, PostQuadKind::kSunDisc, 7.0f);
        });
    if (s->quad_reason != QuadSetReason::kNone || s->quad_count != 7 || s->quad_seen != 7 || s->quad_over != 0) return 5;
    const PostQuadKind kinds[7] = {PostQuadKind::kMoon,       PostQuadKind::kCloud, PostQuadKind::kCloud,  PostQuadKind::kGlare,
                                   PostQuadKind::kBeamSource, PostQuadKind::kFlare, PostQuadKind::kSunDisc};
    for (uint32_t i = 0; i < 7; ++i) {
      const QuadRecord& q = s->quads[i];
      if (q.kind != kinds[i] || q.sequence != i || q.corners[0][0] != float(i + 1) ||
          q.ps_hash != capture::kPostQuadTable[size_t(kinds[i])].ps) {
        return 6;
      }
    }
    if (s->quads.size() != 7) return 7;
    const uint32_t by_kind[8] = {1, 2, 1, 1, 1, 0, 1, 0};
    if (std::memcmp(s->quad_kinds, by_kind, sizeof(by_kind)) != 0) return 8;
    if (render::QuadLine(*s) != "set whole, 7 records {moon: 1, cloud: 2, glare: 1, beam-source: 1, flare: 1, sun-disc: 1}") return 9;
    // The next frame starts with none of it (the flares come and go between frames).
    auto t = frame(b, [&](render::FrameBuilder& fb) { whole(fb, PostQuadKind::kGlare, 9.0f); }, nothing);
    if (t->quad_reason != QuadSetReason::kNone || t->quad_count != 1 || t->quads[0].corners[0][0] != 9.0f ||
        t->quads[0].sequence != 0 || t->quad_kinds[size_t(PostQuadKind::kMoon)] != 0 || t->quads.size() != 1) {
      return 10;
    }
    // (the first scene is not written again by the builder)
    if (s->quad_count != 7 || s->quads[3].corners[0][0] != 4.0f) return 11;
  }
  {
    // The bound: 24 records are a set; one call more and the frame has none of them.
    auto full = frame(b, nothing, [&](render::FrameBuilder& fb) {
      for (int i = 0; i < 24; ++i) whole(fb, PostQuadKind::kFlare, float(i));
    });
    if (full->quad_reason != QuadSetReason::kNone || full->quad_count != 24 || full->quads.size() != 24 || full->quads[23].sequence != 23 ||
        full->quads[23].corners[0][0] != 23.0f) {
      return 12;
    }
    auto over = frame(b, nothing, [&](render::FrameBuilder& fb) {
      for (int i = 0; i < 27; ++i) whole(fb, PostQuadKind::kFlare, float(i));
    });
    if (over->quad_reason != QuadSetReason::kOverLimit || !none(*over) || over->quad_seen != 27 || over->quad_over != 3) return 13;
    if (render::QuadLine(*over) != "set none(over-limit), seen 27, taken 24, over 3 {flare: 27}") return 14;
    // ... and the frame after it is whole again.
    auto next = frame(b, nothing, [&](render::FrameBuilder& fb) { whole(fb, PostQuadKind::kFlare, 1.0f); });
    if (next->quad_reason != QuadSetReason::kNone || next->quad_count != 1 || next->quad_over != 0) return 15;
  }
  {
    // A refused record (MakeQuadRecord's reason, or the call's own: the shader, the storage): none
    // of the frame's records is given, also not those taken before it or after it.
    auto s = frame(
        b,
        [&](render::FrameBuilder& fb) {
          whole(fb, PostQuadKind::kMoon, 1.0f);
          fb.QuadSeen(PostQuadKind::kGlare);
          fb.AddQuad(QuadRecord{}, QuadRecordReason::kCorner);
          whole(fb, PostQuadKind::kGlare, 3.0f);
        },
        [&](render::FrameBuilder& fb) {
          fb.QuadSeen(PostQuadKind::kSunDisc);
          fb.AddQuad(QuadRecord{}, QuadRecordReason::kStorage);
        });
    if (s->quad_reason != QuadSetReason::kRefused || !none(*s) || s->quad_seen != 4) return 16;
    if (s->quad_refused[size_t(QuadRecordReason::kCorner)] != 1 || s->quad_refused[size_t(QuadRecordReason::kStorage)] != 1) return 17;
    if (render::QuadLine(*s) != "set none(refused), seen 4, taken 2, refused {storage: 1, corner: 1} {moon: 1, glare: 2, sun-disc: 1}") {
      return 18;
    }
    // A valid record handed over with a reason is a refusal; a record without a shader handed over
    // without one is "unread" (nothing invalid is ever stored).
    auto t = frame(b, nothing, [&](render::FrameBuilder& fb) {
      fb.QuadSeen(PostQuadKind::kFlare);
      fb.AddQuad(quad(PostQuadKind::kFlare, 1.0f), QuadRecordReason::kIndices);
      fb.QuadSeen(PostQuadKind::kFlare);
      fb.AddQuad(QuadRecord{}, QuadRecordReason::kNone);
    });
    if (t->quad_reason != QuadSetReason::kRefused || !none(*t) || t->quad_refused[size_t(QuadRecordReason::kIndices)] != 1 ||
        t->quad_refused[size_t(QuadRecordReason::kUnread)] != 1) {
      return 19;
    }
    // A reason that is no reason counts as unread.
    auto u = frame(b, nothing, [&](render::FrameBuilder& fb) {
      fb.QuadSeen(PostQuadKind::kFlare);
      fb.AddQuad(QuadRecord{}, QuadRecordReason(200));
    });
    if (u->quad_reason != QuadSetReason::kRefused || u->quad_refused[size_t(QuadRecordReason::kUnread)] != 1) return 20;
    // The frame after refusals is whole again.
    auto v = frame(b, nothing, [&](render::FrameBuilder& fb) { whole(fb, PostQuadKind::kFlare, 1.0f); });
    if (v->quad_reason != QuadSetReason::kNone || v->quad_count != 1 || v->quad_refused[size_t(QuadRecordReason::kCorner)] != 0) return 21;
  }
  {
    // The dropped pending record: a call whose storage was not read before the frame ended (the
    // frame's last quad without a later hooked draw). The frame has no furniture, and the record,
    // arriving in the next frame, is not that frame's: the capture drops it there (its frame
    // check); were it handed over all the same, the next frame would hold a record more than it saw
    // and be no set either.
    auto s = frame(b, [&](render::FrameBuilder& fb) { whole(fb, PostQuadKind::kMoon, 1.0f); },
                   [&](render::FrameBuilder& fb) { fb.QuadSeen(PostQuadKind::kSunDisc); });
    if (s->quad_reason != QuadSetReason::kPending || !none(*s) || s->quad_seen != 2) return 22;
    if (render::QuadLine(*s) != "set none(pending), seen 2, taken 1 {moon: 1, sun-disc: 1}") return 23;
    auto t = frame(b, nothing, [&](render::FrameBuilder& fb) { whole(fb, PostQuadKind::kFlare, 2.0f); });
    if (t->quad_reason != QuadSetReason::kNone || t->quad_count != 1 || t->quads[0].corners[0][0] != 2.0f) return 24;
    auto u = frame(b, nothing, [&](render::FrameBuilder& fb) {
      fb.AddQuad(quad(PostQuadKind::kSunDisc, 5.0f), QuadRecordReason::kNone);  // (a stray record)
      whole(fb, PostQuadKind::kFlare, 2.0f);
    });
    if (u->quad_reason != QuadSetReason::kPending || !none(*u)) return 25;
  }
  {
    // A kind that is none counts as a call and refuses the set.
    auto s = frame(b, nothing, [&](render::FrameBuilder& fb) {
      fb.QuadSeen(PostQuadKind::kCount);
      fb.AddQuad(quad(PostQuadKind::kFlare, 1.0f), QuadRecordReason::kNone);
    });
    if (s->quad_seen != 1 || s->quad_count != 1 || s->quad_reason != QuadSetReason::kNone) return 26;
    QuadRecord bad = quad(PostQuadKind::kFlare, 1.0f);
    bad.kind = PostQuadKind::kCount;
    auto t = frame(b, nothing, [&](render::FrameBuilder& fb) {
      fb.QuadSeen(PostQuadKind::kFlare);
      fb.AddQuad(bad, QuadRecordReason::kNone);
    });
    if (t->quad_reason != QuadSetReason::kRefused || !none(*t) || t->quad_refused[size_t(QuadRecordReason::kUnread)] != 1) return 27;
  }
  {
    // The switch off at a boundary: what was seen since is not taken, and nothing is carried over.
    render::FrameBuilder fresh;
    fresh.QuadsOn(true);
    fresh.QuadSeen(PostQuadKind::kMoon);
    fresh.QuadsOn(false);
    auto s = frame(fresh, [&](render::FrameBuilder& fb) { whole(fb, PostQuadKind::kMoon, 1.0f); }, nothing);
    if (s->quad_records || s->quad_reason != QuadSetReason::kOff || !none(*s) || s->quad_seen != 0) return 28;
    fresh.QuadsOn(true);
    auto t = frame(fresh, [&](render::FrameBuilder& fb) { whole(fb, PostQuadKind::kMoon, 1.0f); }, nothing);
    if (t->quad_reason != QuadSetReason::kNone || t->quad_count != 1 || t->quad_seen != 1) return 29;
    // Calls made while the quads are off do nothing, also when the switch comes on before the frame ends.
    render::FrameBuilder late;
    late.Open();
    late.Add(r);
    whole(late, PostQuadKind::kMoon, 1.0f);
    late.QuadsOn(true);
    late.Close();
    auto u = late.Finish(++number);
    if (u->quad_reason != QuadSetReason::kNoDraw || u->quad_seen != 0 || u->quad_taken != 0 || !none(*u)) return 30;
  }
  {
    // The sun layer's depth place: the frame's tree occluders, each matched to the record it draws again.
    // Records: three of the scene (the frame helper adds `r` first), the second of them the late leaf draw.
    capture::DrawRecord leaf = r, other = r, occluder = r;
    leaf.indexed = other.indexed = occluder.indexed = true;
    leaf.skip = other.skip = occluder.skip = capture::SkipReason::kNone;
    leaf.instances.active = other.instances.active = occluder.instances.active = false;
    leaf.skin.active = other.skin.active = occluder.skin.active = false;
    leaf.terrain_index = other.terrain_index = occluder.terrain_index = capture::kNoSideIndex;
    leaf.ib = occluder.ib = {0x1C2E9380, 37444};
    leaf.count = occluder.count = 18722;
    other.ib = {0x1C000000, 600};
    other.count = 300;
    occluder.vs_hash = 0x432563420047C96Cull;
    auto scene_of = [&](render::FrameBuilder& fb) {
      fb.Add(other);
      fb.Add(leaf);
      fb.Add(other);
    };
    render::FrameBuilder lb;
    // Quads off: nothing is counted and the frame has no place.
    auto off = frame(lb, scene_of, [&](render::FrameBuilder& fb) { fb.LayerOccluderSeen(&occluder); });
    if (off->layer_occluders != 0 || off->layer_occluders_failed != 0 || off->layer_depth_place != capture::kNoDepthPlace) return 31;
    lb.QuadsOn(true);
    // No occluder call: no place, nothing failed.
    auto plain = frame(lb, scene_of, nothing);
    if (plain->layer_occluders != 0 || plain->layer_occluders_failed != 0 || plain->layer_depth_place != capture::kNoDepthPlace) return 32;
    // One occluder: the leaf's record. It is the scene's third record if `r` is drawable, else its second.
    auto one = frame(lb, scene_of, [&](render::FrameBuilder& fb) { fb.LayerOccluderSeen(&occluder); });
    const uint32_t leaf_at = uint32_t(one->draws.size()) - 2;
    if (one->draws.size() < 3 || one->layer_occluders != 1 || one->layer_occluders_failed != 0 || one->layer_depth_place != leaf_at) return 33;
    if (render::QuadLine(*one).find(", tree occluders 1 (0 failed), depth before record ") == std::string::npos) return 34;
    // ... and nothing of it is kept for the next frame.
    auto after_one = frame(lb, scene_of, nothing);
    if (after_one->layer_occluders != 0 || after_one->layer_depth_place != capture::kNoDepthPlace) return 35;
    if (render::QuadLine(*after_one).find("tree occluders") != std::string::npos) return 35;
    // A call that could not be read, and one without a twin: counted failed; the place of the good one stays.
    capture::DrawRecord stranger = occluder;
    stranger.count = 17;
    auto mixed = frame(lb, scene_of, [&](render::FrameBuilder& fb) {
      fb.LayerOccluderSeen(nullptr);
      fb.LayerOccluderSeen(&occluder);
      fb.LayerOccluderSeen(&stranger);
    });
    if (mixed->layer_occluders != 3 || mixed->layer_occluders_failed != 2 || mixed->layer_depth_place != leaf_at) return 36;
    // Failures alone: no place.
    auto failed = frame(lb, scene_of, [&](render::FrameBuilder& fb) { fb.LayerOccluderSeen(&stranger); });
    if (failed->layer_occluders != 1 || failed->layer_occluders_failed != 1 || failed->layer_depth_place != capture::kNoDepthPlace) return 37;
    if (render::QuadLine(*failed).find(", tree occluders 1 (1 failed), no depth place") == std::string::npos) return 37;
    // Two late draws: the place is the earlier record's, whatever the order of the occluders.
    capture::DrawRecord leaf2 = leaf, occluder2 = occluder;
    leaf2.ib = occluder2.ib = {0x1C300000, 2000};
    leaf2.count = occluder2.count = 1000;
    auto two = frame(lb, [&](render::FrameBuilder& fb) {
      fb.Add(other);
      fb.Add(leaf2);
      fb.Add(leaf);
    }, [&](render::FrameBuilder& fb) {
      fb.LayerOccluderSeen(&occluder);
      fb.LayerOccluderSeen(&occluder2);
    });
    if (two->layer_occluders != 2 || two->layer_occluders_failed != 0 || two->layer_depth_place != uint32_t(two->draws.size()) - 2) return 38;
    // The same draw twice in the scene: ambiguous, failed.
    auto twice = frame(lb, [&](render::FrameBuilder& fb) {
      fb.Add(leaf);
      fb.Add(leaf);
    }, [&](render::FrameBuilder& fb) { fb.LayerOccluderSeen(&occluder); });
    if (twice->layer_occluders_failed != 1 || twice->layer_depth_place != capture::kNoDepthPlace) return 39;
    // More calls than the bound: those past it fail, also when they have a twin.
    auto many = frame(lb, scene_of, [&](render::FrameBuilder& fb) {
      for (uint32_t i = 0; i < capture::kMaxLayerOccluders + 2; ++i) fb.LayerOccluderSeen(&occluder);
    });
    if (many->layer_occluders != capture::kMaxLayerOccluders + 2 || many->layer_occluders_failed != 2 || many->layer_depth_place != leaf_at) return 40;
  }
  // The draw record's size is what it was: the quads are a block of their own.
  static_assert(sizeof(capture::DrawRecord) == 616);
  return 0;
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

  if (const int code = LightAndPoolTests(r)) {
    std::cout << "FAIL: light side array / pool case " << code << "\n";  // (an exit code would wrap at 256)
    return 129;
  }
  if (const int code = ShadowTests(r)) {
    std::cout << "FAIL: shadow lists / block case " << code << "\n";
    return 130;
  }
  if (const int code = FogTests(r)) {
    std::cout << "FAIL: fog and sky block case " << code << "\n";
    return 131;
  }
  if (const int code = PostTests(r)) {
    std::cout << "FAIL: post block case " << code << "\n";
    return 132;
  }
  if (const int code = QuadTests(r)) {
    std::cout << "FAIL: quad records case " << code << "\n";
    return 133;
  }

  render::ScenePublisher pub;
  if (pub.Latest()) return 18;
  pub.Publish(s);
  pub.Publish(s2);
  if (pub.Latest()->frame != 43) return 19;
  std::cout << "PASS: draw record, frame builder, terrain side array, light side array and block, buffer reuse, "
               "pool under threads, shadow lists and block, fog and sky blocks, post block, quad records, publisher\n";
  return 0;
}
