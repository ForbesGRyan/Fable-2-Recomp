#pragma once

// Texture cache policy (pure: no SDK/GPU deps): a sparse content hash checked
// every frame, change streaks that mark render-target-like textures dynamic,
// and the per-frame upload budget.

#include <algorithm>
#include <cstdint>

namespace fable2::native::render {

inline constexpr uint32_t kSampleCount = 16;
inline constexpr uint32_t kSampleBytes = 256;
// More than this many consecutive observed frames with a change: dynamic.
inline constexpr uint32_t kDynamicStreak = 8;
// A dynamic texture becomes static again after this many observed frames
// without a change.
inline constexpr uint32_t kDynamicClearFrames = 60;

// FNV-1a over kSampleCount ranges of kSampleBytes spread evenly over
// [0, size), always including the first and last bytes; textures of at most
// kSampleCount * kSampleBytes bytes are hashed whole.
inline uint64_t SampleHash(const uint8_t* data, uint32_t size) {
  uint64_t h = 1469598103934665603ull ^ size;
  auto mix = [&](uint32_t from, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) h = (h ^ data[from + i]) * 1099511628211ull;
  };
  if (!data || size == 0) return h;
  if (size <= kSampleCount * kSampleBytes) {
    mix(0, size);
    return h;
  }
  const uint64_t span = size - kSampleBytes;
  for (uint32_t i = 0; i < kSampleCount; ++i) {
    mix(uint32_t(span * i / (kSampleCount - 1)), kSampleBytes);
  }
  return h;
}

struct TextureState {
  uint64_t sample_hash = 0;
  uint64_t last_frame = 0;   // last observation (0 = never)
  uint64_t last_change = 0;
  uint32_t streak = 0;       // consecutive observed frames with a change
  bool dynamic = false;
};

// Records this frame's sample hash. Returns true if the contents changed
// (always on the first observation).
inline bool NoteSample(TextureState& s, uint64_t sample_hash, uint64_t frame) {
  const bool first = s.last_frame == 0;
  const bool changed = first || sample_hash != s.sample_hash;
  const bool consecutive = !first && frame == s.last_frame + 1;
  if (changed && !first) {
    s.streak = consecutive ? s.streak + 1 : 1;
    s.last_change = frame;
    if (s.streak > kDynamicStreak) s.dynamic = true;
  } else if (!changed) {
    s.streak = 0;
    if (s.dynamic && frame - s.last_change >= kDynamicClearFrames) s.dynamic = false;
  }
  if (first) s.last_change = frame;
  s.sample_hash = sample_hash;
  s.last_frame = frame;
  return changed;
}

// Guest bytes decoded per frame. The first upload of a frame may exceed the
// budget (a texture larger than the budget would otherwise never load).
class UploadBudget {
 public:
  explicit UploadBudget(uint64_t bytes) : budget_(bytes) {}
  void BeginFrame(uint64_t bytes) {
    budget_ = bytes;
    used_ = 0;
  }
  bool TryTake(uint64_t bytes) {
    if (used_ == 0 || used_ + bytes <= budget_) {
      used_ += bytes;
      return true;
    }
    return false;
  }
  uint64_t used() const { return used_; }

 private:
  uint64_t budget_;
  uint64_t used_ = 0;
};

}  // namespace fable2::native::render
