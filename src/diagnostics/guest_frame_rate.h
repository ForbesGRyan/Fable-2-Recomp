// guest_frame_rate.h - always-on guest frame-time tracker for the F3 overlay.
//
// The SDK debug overlay (F3) only prints "Guest: NN FPS" when the app's
// frame-stats provider returns data. The provider in fable_2_app.h reads the
// SDK perf counter kFrameTimeUs, but the vsync-present-gate SDK fork never
// sets it (and compiles perf counters out of Release), so the overlay was
// empty. This header keeps our own smoothed frame time instead:
// MainRenderLoop_82B9CD68 (one call per guest frame, see fps_meter.h) calls
// record_frame(), and the provider falls back to frame_time_us().
//
// Cost: one steady_clock read (already taken by the override) plus a few
// arithmetic ops per guest frame. Single writer (the guest render thread),
// any reader (the UI thread); a relaxed atomic is enough for a display value.

#pragma once

#include <atomic>
#include <cstdint>

namespace fable2::guest_frame_rate {

// Smoothed frame time in microseconds (0 = no data yet).
inline std::atomic<int64_t>& smoothed_us() {
  static std::atomic<int64_t> v{0};
  return v;
}

// Called once per guest frame from the render thread with a monotonic
// timestamp in microseconds.
inline void record_frame(int64_t now_us) {
  static int64_t last_us = 0;
  static double ema_us = 0.0;
  if (last_us != 0) {
    const int64_t dt = now_us - last_us;
    // Ignore long gaps (loading screens, debugger breaks) so one hitch
    // doesn't hold the readout down for seconds.
    if (dt > 0 && dt < 1'000'000) {
      ema_us = ema_us == 0.0 ? double(dt) : ema_us + (double(dt) - ema_us) * 0.1;
      smoothed_us().store(int64_t(ema_us), std::memory_order_relaxed);
    }
  }
  last_us = now_us;
}

inline int64_t frame_time_us() {
  return smoothed_us().load(std::memory_order_relaxed);
}

}  // namespace fable2::guest_frame_rate
