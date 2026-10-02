// Synthetic standalone test; no game or GPU.
#include "../../src/native/render/geometry_cache_index.h"
#include "../../src/native/render/texture_residency.h"
#include <vector>

using namespace fable2::native::render;

int main() {
  // --- sample hash: sensitive inside the sampled ranges, cheap on big textures ---
  std::vector<uint8_t> tex(1 << 20, 0x11);
  const uint64_t h0 = SampleHash(tex.data(), uint32_t(tex.size()));
  tex[0] ^= 1;  // first byte is always sampled
  if (SampleHash(tex.data(), uint32_t(tex.size())) == h0) return 1;
  tex[0] ^= 1;
  tex[tex.size() - 1] ^= 1;  // last byte is always sampled
  if (SampleHash(tex.data(), uint32_t(tex.size())) == h0) return 2;
  tex[tex.size() - 1] ^= 1;
  if (SampleHash(tex.data(), uint32_t(tex.size())) != h0) return 3;
  // Small textures are hashed whole.
  std::vector<uint8_t> small(1000, 3);
  const uint64_t s0 = SampleHash(small.data(), 1000);
  small[500] = 4;
  if (SampleHash(small.data(), 1000) == s0) return 4;
  if (SampleHash(nullptr, 0) != SampleHash(nullptr, 0)) return 5;

  // --- change streaks ---
  TextureState st;
  if (!NoteSample(st, 1, 1)) return 6;          // first observation counts as a change (upload)
  if (NoteSample(st, 1, 2) || st.dynamic) return 7;
  for (uint64_t f = 3; f < 3 + kDynamicStreak; ++f) NoteSample(st, 100 + f, f);  // changes every frame
  if (st.dynamic) return 8;                     // exactly kDynamicStreak changed frames: not yet
  NoteSample(st, 999, 3 + kDynamicStreak);
  if (!st.dynamic) return 9;                    // one more: dynamic
  // A gap in observations (texture not drawn) resets the streak, but dynamic
  // clears only after kDynamicClearFrames frames without a change.
  uint64_t f = 3 + kDynamicStreak + 1;
  for (uint32_t i = 0; i < kDynamicClearFrames - 1; ++i, ++f) NoteSample(st, 999, f);
  if (!st.dynamic) return 10;
  NoteSample(st, 999, f);
  if (st.dynamic) return 11;
  TextureState gap;
  NoteSample(gap, 1, 1);
  NoteSample(gap, 2, 2);
  NoteSample(gap, 3, 10);                       // not observed in frames 3-9
  if (gap.streak != 1) return 12;

  // --- upload budget ---
  UploadBudget b(100);
  b.BeginFrame(100);
  if (!b.TryTake(60) || b.used() != 60) return 13;
  if (b.TryTake(50)) return 14;                 // would exceed
  if (!b.TryTake(40) || b.used() != 100) return 15;
  b.BeginFrame(100);
  if (!b.TryTake(500)) return 16;               // one oversize upload per frame is allowed
  if (b.TryTake(1)) return 17;
  b.BeginFrame(100);
  if (!b.TryTake(10) || b.TryTake(500)) return 18;  // oversize only as the first upload

  // --- LRU via GeometryCacheIndex (kind 2): nothing used this frame is evicted ---
  GeometryCacheIndex idx(100);
  idx.BeginFrame(1);
  std::vector<uint32_t> ev;
  const GeoKey k1{0x1000, 64, 0, 0, 2}, k2{0x2000, 64, 0, 0, 2};
  idx.Insert(k1, 1, 80, &ev);
  idx.Insert(k2, 2, 80, &ev);
  if (!ev.empty() || idx.resident_bytes() != 160) return 19;  // over budget, both in use
  idx.BeginFrame(2);
  idx.Lookup(k2, 2);
  idx.Insert(GeoKey{0x3000, 64, 0, 0, 2}, 3, 10, &ev);
  if (ev.size() != 1 || idx.Lookup(k1, 1).hit) return 20;     // k1 (unused this frame) evicted
  return 0;
}
