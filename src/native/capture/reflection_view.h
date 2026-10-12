#pragma once

// The off-scene lake views, pure; only a whole pitch-280 run ending in its
// indexed sky is a view. An incomplete run refuses the frame's entire set.
#include <cstdint>

namespace fable2::native::capture {
inline constexpr uint32_t kMaxReflectionDraws = 256, kMaxReflectionViews = 8;
inline constexpr uint64_t kReflectionTerrainPs = 0x7DDAD5C29B3F56EEull;
inline constexpr uint64_t kReflectionSkyPs = 0xF7601FBE485936FDull;
inline constexpr uint64_t kReflectionBlurPs = 0xF186D27249B462F8ull;
inline constexpr uint64_t kReflectionFlarePs = 0xD72FD0903CCE7DD6ull;
struct ReflectionView {
  uint32_t first = 0, count = 0, blurred = 0;
  // The blur's read components only: captured at its BeginVertices call.
  float offsets[7][2] = {};
  float weight = 0;
};
enum class ReflectionDraw : uint8_t { kNone, kTerrain, kOther, kSky, kBlur };
class ReflectionViewTracker {
 public:
  void Reset() { *this = ReflectionViewTracker{}; }
  ReflectionDraw Observe(uint32_t surface, uint32_t scene_surface, bool shadow, uint64_t ps,
                         bool indexed, bool begin, bool in_scene) {
    if (dropped_) return ReflectionDraw::kNone;
    if (begin && ps == kReflectionBlurPs && closed_ && !in_scene) {
      closed_ = false;
      return ReflectionDraw::kBlur;
    }
    const bool view = !in_scene && !shadow && surface != scene_surface && (surface & 0x3FFFu) == 280;
    if (!view) {
      if (open_) dropped_ = true;
      open_ = closed_ = false;
      return ReflectionDraw::kNone;
    }
    // Only the known indexed flare belongs to the preceding sky. Export,
    // sky-only and unknown runs must still open/count a new whole view.
    if (closed_ && indexed && ps == kReflectionFlarePs) return ReflectionDraw::kOther;
    if (closed_) closed_ = false;
    if (!open_) {
      if (views_ == kMaxReflectionViews) { dropped_ = true; return ReflectionDraw::kNone; }
      open_ = true;
    }
    if (indexed && ps == kReflectionSkyPs) {
      open_ = false;
      closed_ = true;
      ++views_;
      return ReflectionDraw::kSky;
    }
    return ps == kReflectionTerrainPs ? ReflectionDraw::kTerrain : ReflectionDraw::kOther;
  }
  uint32_t views() const { return views_; }
  bool dropped() const { return dropped_; }
  bool open() const { return open_; }
 private:
  uint32_t views_ = 0;
  bool open_ = false, closed_ = false, dropped_ = false;
};
}  // namespace fable2::native::capture
