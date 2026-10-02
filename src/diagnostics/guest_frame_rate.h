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

#include "window_stats.h"

namespace fable2::guest_frame_rate {

// Raw per-frame times (ms) over a 300-frame window for the periodic
// "[frame] guest" log line (fps_meter.h). Render thread only. Work is the
// frame interval minus the swap and the limiter/fence waits, as in F3.
struct Windows {
  diagnostics::WindowStats frame_ms{300}, work_ms{300}, swap_ms{300}, wait_ms{300};
};
inline Windows& windows() {
  static Windows w;
  return w;
}

// Smoothed frame time in microseconds (0 = no data yet).
inline std::atomic<int64_t>& smoothed_us() {
  static std::atomic<int64_t> v{0};
  return v;
}

// Called once per guest frame from the render thread with a monotonic
// timestamp in microseconds.
// Raw (unsmoothed) per-frame record of the last completed guest frame, for
// the overlay's history/percentiles. frame_count is published last (release).
struct RawFrame {
  std::atomic<int64_t> frame_us{0}, swap_us{0}, wait_us{0};
  std::atomic<uint64_t> count{0};
};
inline RawFrame& raw() {
  static RawFrame r;
  return r;
}

// Limiter/fence wait (sub_82242628) accumulated since the last frame boundary,
// and the swap duration of the previous frame. Render thread only; waits made
// from inside the swap call are excluded (the swap bucket already has them).
inline int64_t& pending_wait_us() {
  static int64_t v = 0;
  return v;
}
inline int64_t& pending_swap_us() {
  static int64_t v = 0;
  return v;
}
inline bool& in_swap() {
  static thread_local bool v = false;
  return v;
}
inline bool& on_render_thread() {
  static thread_local bool v = false;
  return v;
}
inline void add_wait(int64_t dt_us) {
  if (on_render_thread() && !in_swap() && dt_us > 0) pending_wait_us() += dt_us;
}

inline std::atomic<int64_t>& smoothed_wait_us() {
  static std::atomic<int64_t> v{0};
  return v;
}
inline int64_t wait_us() {
  return smoothed_wait_us().load(std::memory_order_relaxed);
}

inline void record_frame(int64_t now_us) {
  static int64_t last_us = 0;
  static double ema_us = 0.0;
  static double wait_ema_us = 0.0;
  on_render_thread() = true;
  if (last_us != 0) {
    const int64_t dt = now_us - last_us;
    if (dt > 0) {
      // Frame boundary: publish the finished frame's raw numbers.
      RawFrame& r = raw();
      r.frame_us.store(dt, std::memory_order_relaxed);
      r.swap_us.store(pending_swap_us(), std::memory_order_relaxed);
      r.wait_us.store(pending_wait_us(), std::memory_order_relaxed);
      r.count.fetch_add(1, std::memory_order_release);
      wait_ema_us = wait_ema_us == 0.0
                        ? double(pending_wait_us())
                        : wait_ema_us + (double(pending_wait_us()) - wait_ema_us) * 0.1;
      smoothed_wait_us().store(int64_t(wait_ema_us), std::memory_order_relaxed);
      if (dt < 1'000'000) {  // loading hitches excluded, as below
        const int64_t work = dt - pending_swap_us() - pending_wait_us();
        Windows& w = windows();
        w.frame_ms.Add(double(dt) / 1000.0);
        w.work_ms.Add(double(work > 0 ? work : 0) / 1000.0);
        w.swap_ms.Add(double(pending_swap_us()) / 1000.0);
        w.wait_ms.Add(double(pending_wait_us()) / 1000.0);
      }
    }
    pending_wait_us() = 0;
    pending_swap_us() = 0;
    // Ignore long gaps (loading screens, debugger breaks) so one hitch
    // doesn't hold the readout down for seconds.
    if (dt > 0 && dt < 1'000'000) {
      ema_us = ema_us == 0.0 ? double(dt) : ema_us + (double(dt) - ema_us) * 0.1;
      smoothed_us().store(int64_t(ema_us), std::memory_order_relaxed);
    }
  }
  last_us = now_us;
}

// Smoothed time spent inside the guest's swap/present call per frame (us).
inline std::atomic<int64_t>& smoothed_swap_us() {
  static std::atomic<int64_t> v{0};
  return v;
}

// Called once per guest frame with the duration of the original
// MainRenderLoop call (render thread only).
inline void record_swap(int64_t dt_us) {
  static double ema_us = 0.0;
  pending_swap_us() = dt_us > 0 ? dt_us : 0;
  if (dt_us < 0 || dt_us >= 1'000'000) return;
  ema_us = ema_us == 0.0 ? double(dt_us) : ema_us + (double(dt_us) - ema_us) * 0.1;
  smoothed_swap_us().store(int64_t(ema_us), std::memory_order_relaxed);
}

inline int64_t swap_us() {
  return smoothed_swap_us().load(std::memory_order_relaxed);
}

inline int64_t frame_time_us() {
  return smoothed_us().load(std::memory_order_relaxed);
}

}  // namespace fable2::guest_frame_rate
