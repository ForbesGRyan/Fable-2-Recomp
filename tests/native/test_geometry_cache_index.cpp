// Synthetic standalone test; no game or GPU.
#include "../../src/native/render/geometry_cache_index.h"
#include <algorithm>
#include <iostream>

using namespace fable2::native::render;

int main() {
  GeometryCacheIndex c(100);
  std::vector<uint32_t> ev;
  c.BeginFrame(1);
  GeoKey a{0x1000, 40, 12, 0, 0}, b{0x2000, 40, 12, 0, 0}, d{0x3000, 40, 12, 0, 0};
  if (c.Lookup(a, 7).hit) return 1;
  const uint32_t ida = c.Insert(a, 7, 40, &ev);
  if (!ev.empty() || !c.Lookup(a, 7).hit || c.Lookup(a, 7).id != ida) return 2;
  if (c.Lookup(a, 8).hit) return 3;                    // content changed -> miss
  const uint32_t ida2 = c.Insert(a, 8, 40, &ev);       // replace: old id evicted
  if (ev.size() != 1 || ev[0] != ida || ida2 == ida || c.resident_bytes() != 40) return 4;
  ev.clear();
  c.BeginFrame(2);
  const uint32_t idb = c.Insert(b, 1, 40, &ev);
  c.BeginFrame(3);
  c.Lookup(b, 1);                                      // b used in frame 3, a last used frame 1
  const uint32_t idd = c.Insert(d, 1, 40, &ev);        // 120 > 100: evict LRU (a)
  if (ev.size() != 1 || ev[0] != ida2 || c.resident_bytes() != 80) return 5;
  ev.clear();
  // Everything resident is used this frame: over budget is allowed, nothing evicted.
  GeoKey e{0x4000, 40, 12, 0, 0};
  c.Lookup(d, 1);
  c.Insert(e, 1, 40, &ev);
  if (!ev.empty() || c.resident_bytes() != 120 || c.size() != 3) return 6;
  // Lower budget: next insert in a new frame evicts down to budget.
  c.set_budget_bytes(50);
  c.BeginFrame(4);
  GeoKey f{0x5000, 10, 12, 0, 0};
  c.Insert(f, 1, 10, &ev);
  if (c.resident_bytes() > 50 || std::find(ev.begin(), ev.end(), idb) == ev.end()) return 7;
  (void)idd;
  // Different kind with same address is a different key.
  GeoKey fi = f; fi.kind = 1;
  if (c.Lookup(fi, 1).hit) return 8;
  // At the budget one pass evicts entries that neither this frame nor the
  // previous one used down to the low-water mark (15/16 of the budget), least
  // recently used first, so the inserts that follow do not scan the index
  // again. 4,000 entries of 16 bytes fill the budget exactly, 100 per frame;
  // ids ascend with the entry number.
  {
    constexpr uint32_t kEntries = 4000, kBytes = 16;
    constexpr uint64_t kBudget = uint64_t(kEntries) * kBytes;   // 64,000; low water 60,000
    GeometryCacheIndex big(kBudget);
    if (big.low_water_bytes() != 60000) return 9;
    auto key = [](uint32_t i) { return GeoKey{0x10000 + i * 16, 16, 12, 0, 5}; };
    std::vector<uint32_t> ids, out;
    for (uint32_t i = 0; i < kEntries; ++i) {
      big.BeginFrame(1 + i / 100);
      ids.push_back(big.Insert(key(i), i, kBytes, &out));
    }
    if (!out.empty() || big.resident_bytes() != kBudget || big.size() != kEntries) return 10;
    // Frame 50 uses the 100 oldest entries again, then inserts one more.
    big.BeginFrame(50);
    for (uint32_t i = 0; i < 100; ++i) {
      if (!big.Lookup(key(i), i).hit) return 11;
    }
    uint32_t next = kEntries;
    big.Insert(key(next), next, kBytes, &out);
    ++next;
    // 251 entries leave: the total with the new entry is the low-water mark.
    if (out.size() != 251 || big.resident_bytes() != big.low_water_bytes()) return 12;
    if (big.size() != kEntries + 1 - 251) return 13;
    // They are the least recently used that were not used in this frame, in
    // that order: entries 100..350 (frames 2 and 3 and the first 51 of frame
    // 4's hundred; within a frame the lowest id first).
    for (uint32_t k = 0; k < 251; ++k) {
      if (out[k] != ids[100 + k]) return 14;
    }
    // Entries used in this frame survive, and so does the next one in line.
    for (uint32_t i = 0; i < 100; ++i) {
      if (!big.Lookup(key(i), i).hit) return 15;
    }
    big.BeginFrame(51);
    if (big.Lookup(key(350), 350).hit || !big.Lookup(key(351), 351).hit) return 16;
    // The inserts right after evict nothing: 250 of them fit below the budget.
    out.clear();
    for (uint32_t k = 0; k < 250; ++k, ++next) {
      big.Insert(key(next), next, kBytes, &out);
      if (!out.empty()) return 17;
    }
    if (big.resident_bytes() != kBudget) return 18;
    // The next one crosses the budget again: one more pass, down to the mark
    // (entry 351 was used in this frame and stays).
    big.Insert(key(next), next, kBytes, &out);
    if (out.size() != 251 || big.resident_bytes() != big.low_water_bytes() || out[0] != ids[352]) return 19;
    if (!big.Lookup(key(351), 351).hit) return 27;
    // Everything used in the current frame: nothing can leave, the index goes over budget.
    GeometryCacheIndex busy(100);
    busy.BeginFrame(1);
    out.clear();
    for (uint32_t i = 0; i < 10; ++i) busy.Insert(key(i), i, 16, &out);
    if (!out.empty() || busy.resident_bytes() != 160 || busy.size() != 10) return 28;
    // In the next frame they are the previous frame's entries: a later draw
    // of this frame may still look them up, so an insert takes only what it
    // needs to fit the budget (5 of them: 5 * 16 + 16 = 96), not what the
    // low-water mark (94) would ask for (6).
    busy.BeginFrame(2);
    busy.Insert(key(10), 10, 16, &out);
    if (out.size() != 5 || busy.resident_bytes() != 96) return 29;
    busy.Insert(key(11), 11, 16, &out);
    if (out.size() != 6 || busy.resident_bytes() != 96) return 41;
  }
  // A live set near the budget is not evicted mid-frame. 980 live entries
  // (0.98 of the budget) are looked up in every frame, after the frame's 10
  // new keys went in: at the inserts they still carry the previous frame and
  // look idle. Only the keys of two frames ago leave, in one pass.
  {
    constexpr uint64_t kBudget = 100000;  // low water 93,750
    constexpr uint32_t kLive = 980, kBytes = 100;
    GeometryCacheIndex live(kBudget);
    auto key = [](uint32_t i) { return GeoKey{0x40000 + i * 16, 16, 12, 0, 5}; };
    std::vector<uint32_t> ids, out;
    live.BeginFrame(1);
    for (uint32_t i = 0; i < kLive; ++i) ids.push_back(live.Insert(key(i), i, kBytes, &out));
    uint32_t next = kLive;
    for (uint64_t frame = 2; frame <= 200; ++frame) {
      live.BeginFrame(frame);
      out.clear();
      size_t after_first = 0;
      for (uint32_t k = 0; k < 10; ++k, ++next) {
        live.Insert(key(next), next, kBytes, &out);
        if (k == 0) after_first = out.size();
      }
      for (uint32_t i = 0; i < kLive; ++i) {
        const LookupResult found = live.Lookup(key(i), i);
        if (!found.hit || found.id != ids[i]) return 43;  // a live entry was evicted and would be rebuilt
      }
      // (the pass runs at the frame's first insert; the nine after it fit)
      const size_t gone = frame >= 4 ? 10 : 0;
      if (after_first != gone || out.size() != gone) return 44;
      if (live.resident_bytes() != (frame == 2 ? 99000u : 100000u)) return 45;
    }
    // A frame that needs more room than the old entries give takes
    // previous-frame entries one insert at a time, never down to the mark: 30
    // new keys evict the 10 of two frames ago and then 20 more entries.
    live.BeginFrame(201);
    out.clear();
    for (uint32_t k = 0; k < 30; ++k, ++next) live.Insert(key(next), next, kBytes, &out);
    if (out.size() != 30 || live.resident_bytes() != kBudget) return 46;
  }
  // Nothing idle is left in a frame that needs more than the budget: the
  // index goes over it. By reading the header, the insert that finds nothing
  // to evict is the last one of the frame to scan the index; what a test can
  // see is that nothing leaves, and that the next frame evicts again.
  {
    GeometryCacheIndex full(100);
    const GeoKey a{0x1000, 40, 12, 0, 5}, b{0x2000, 40, 12, 0, 5}, c2{0x3000, 40, 12, 0, 5},
        d2{0x4000, 40, 12, 0, 5}, e2{0x5000, 40, 12, 0, 5}, f2{0x6000, 40, 12, 0, 5}, g2{0x7000, 40, 12, 0, 5};
    std::vector<uint32_t> out;
    full.BeginFrame(1);
    const uint32_t id_a = full.Insert(a, 1, 40, &out);
    const uint32_t id_b = full.Insert(b, 1, 40, &out);
    full.BeginFrame(2);
    const uint32_t id_c = full.Insert(c2, 1, 40, &out);  // 120 > 100: a leaves
    const uint32_t id_d = full.Insert(d2, 1, 40, &out);  // b leaves: nothing idle is left
    if (out.size() != 2 || out[0] != id_a || out[1] != id_b || full.resident_bytes() != 80) return 47;
    const uint32_t id_e = full.Insert(e2, 1, 40, &out);
    full.Insert(f2, 1, 40, &out);
    if (out.size() != 2 || full.resident_bytes() != 160 || full.size() != 4) return 48;
    if (!full.Lookup(c2, 1).hit || !full.Lookup(d2, 1).hit || !full.Lookup(e2, 1).hit || !full.Lookup(f2, 1).hit) {
      return 49;
    }
    // The next frame can evict again: three of the four leave for one insert (40 + 40 <= 100).
    full.BeginFrame(3);
    out.clear();
    full.Insert(g2, 1, 40, &out);
    if (out.size() != 3 || out[0] != id_c || out[1] != id_d || out[2] != id_e || full.resident_bytes() != 80) {
      return 50;
    }
  }
  // RetirePool: replaced buffers are reused once their submission completed,
  // only for the same size; the byte budget pushes out the oldest.
  {
    RetirePool<int> pool(100);
    std::vector<int> destroy;
    pool.Retire(1, 40, 5, &destroy);
    pool.Retire(2, 40, 6, &destroy);
    if (!destroy.empty() || pool.bytes() != 80) return 20;
    if (pool.Take(40, 4).has_value()) return 21;  // submission 5 not complete
    auto t = pool.Take(40, 5);
    if (!t || *t != 1 || pool.bytes() != 40) return 22;
    if (pool.Take(48, 10).has_value()) return 23;  // no buffer of that size
    pool.Retire(3, 40, 7, &destroy);
    pool.Retire(4, 40, 8, &destroy);  // 120 > 100: oldest (2) destroyed
    if (destroy.size() != 1 || destroy[0] != 2 || pool.bytes() != 80) return 24;
    pool.Retire(5, 200, 9, &destroy);  // larger than the budget: destroyed at once
    if (destroy.size() != 2 || destroy[1] != 5) return 25;
    std::vector<int> all;
    pool.Drain(&all);
    if (all.size() != 2 || pool.bytes() != 0) return 26;
  }
  // FrameMemo: a decoded stream is kept for the frame under (key, content
  // hash), so many draws of one mesh decode it once; over the byte budget
  // nothing is stored and the caller keeps its own buffer.
  {
    FrameMemo<uint32_t> memo(40);  // 10 elements
    const GeoKey m1{0x1000, 64, 16, 7, 0}, m2{0x2000, 64, 16, 7, 0};
    if (memo.Find(m1, 5) || memo.bytes() != 0 || !memo.Fits(10) || memo.Fits(11)) return 30;
    const std::vector<uint32_t>* v = memo.Store(m1, 5, std::vector<uint32_t>{1, 2, 3, 4});
    if (!v || v->size() != 4 || (*v)[3] != 4 || memo.bytes() != 16) return 31;
    if (memo.Find(m1, 5) != v) return 32;                        // the same vector for every later draw
    if (memo.Find(m1, 6) || memo.Find(m2, 5)) return 33;         // other content, other stream
    GeoKey layout = m1;
    layout.extra = 8;                                            // same range, other layout
    if (memo.Find(layout, 5)) return 34;
    // A second stream; the first one's vector stays where it was.
    const std::vector<uint32_t>* v2 = memo.Store(m2, 9, std::vector<uint32_t>(6, 11));
    if (!v2 || memo.bytes() != 40 || memo.Find(m1, 5) != v || (*v)[0] != 1) return 35;
    // Full: nothing more is stored, what is there stays, the caller's vector is untouched.
    std::vector<uint32_t> big(3, 2);
    if (memo.Fits(1) || memo.Store(layout, 5, std::move(big)) || big.size() != 3) return 36;
    if (memo.bytes() != 40 || memo.Find(layout, 5) || memo.Find(m2, 9) != v2) return 37;
    // New content under a stored key replaces it (and frees its bytes first).
    const std::vector<uint32_t>* v3 = memo.Store(m2, 10, std::vector<uint32_t>(2, 3));
    if (!v3 || memo.bytes() != 24 || memo.Find(m2, 9) || memo.Find(m2, 10) != v3) return 38;
    // A vector larger than the whole budget is never stored.
    if (memo.Store(layout, 1, std::vector<uint32_t>(11, 0)) || memo.bytes() != 24) return 39;
    // The next frame starts empty.
    memo.Clear();
    if (memo.bytes() != 0 || memo.Find(m1, 5) || memo.Find(m2, 10) || !memo.Fits(10)) return 40;
  }
  std::cout << "PASS: geometry cache index\n";
  return 0;
}
