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
  std::cout << "PASS: geometry cache index\n";
  return 0;
}
