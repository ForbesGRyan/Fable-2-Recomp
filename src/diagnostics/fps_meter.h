// fps_meter.h - lightweight per-frame rate meter for verifying FPS changes.
//
// Strong override of the recompiled main-loop function MainRenderLoop_82B9CD68
// (0x82B9CD68; renamed from sub_82B9CD68 in fable_2_manifest.toml). One
// call per frame; ~33 ms period at the 30 fps cap, see
// docs/FPS_CAP_INVESTIGATION.md). Counts invocations over 5 s windows and
// appends `mainloop rate=NN.N/s` lines to fps_meter.log (CWD = exe dir).
// Forward-only: the override just counts, then calls the original
// __imp__ entry point, so it is safe to leave compiled in.
//
// Enabled with the FABLE2_FPS_METER=1 environment variable. Off by default.
// Include this header in the app's main TU (main.cpp) - the strong override
// below then binds in place of the weak recompiled symbol.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include <rex/logging/macros.h>
#include <rex/ppc/func.h>
#include <rex/graphics/native_guest_renderer.h>

#include "fable2_func_trace.h"
#include "capture.h"
#include "fable2_native_render.h"
#include "fable2_f5_lua.h"  // F5 external-Lua run (polled per frame)
#include "guest_frame_rate.h"
#include "thread_placement.h"  // FABLE2_THREAD_PLACE experiment (inert without it)

// Defined in fable2_d3d_census.h (included by main.cpp after this header).
namespace fable2::d3dcensus { inline void OnFrame(); }

namespace fable2::functrace_window {

// Traces a bounded time window of the (unfiltered) guest call stream, armed
// from the main loop so it lines up with a known on-screen moment (e.g. the
// main menu) without running a multi-GB unfiltered trace for the whole boot.
//
//   FABLE2_TRACE_WINDOW=1         arm the window
//   FABLE2_TRACE_DELAY=<sec>      start this many seconds after the first
//                                 main-loop call (default 0)
//   FABLE2_TRACE_DUR=<sec>        trace duration in seconds (default 5)
//   FABLE2_TRACE_FILTER=<substr>  optional name filter while the window is
//                                 open (default: everything)
//
// Writes the usual fable2_func_trace.log (append) + summary files.
inline bool enabled() {
#ifdef _WIN32
  char v[8] = {};
  size_t n = 0;
  return ::getenv_s(&n, v, sizeof(v), "FABLE2_TRACE_WINDOW") == 0 && v[0] == '1';
#else
  const char* v = std::getenv("FABLE2_TRACE_WINDOW");
  return v != nullptr && v[0] == '1';
#endif
}

inline double delay_seconds() {
  const char* v = std::getenv("FABLE2_TRACE_DELAY");
  return v ? std::atof(v) : 0.0;
}

inline double duration_seconds() {
  const char* v = std::getenv("FABLE2_TRACE_DUR");
  return v ? std::atof(v) : 5.0;
}

inline void run_window(int64_t now_us) {
  if (!enabled()) return;
  static const int64_t t0 = now_us;  // first main-loop call
  const double delay_s = delay_seconds();
  const double dur_s = duration_seconds();
  const int64_t start_us = t0 + static_cast<int64_t>(delay_s * 1e6);
  const int64_t end_us = start_us + static_cast<int64_t>(dur_s * 1e6);
  if (now_us < start_us) return;
  if (now_us < end_us) {
    if (!fable2::functrace::enabled().load()) {
      const char* f = std::getenv("FABLE2_TRACE_FILTER");
      if (f) Fable2FuncTraceSetFilter(f);
      fable2::functrace::set_enabled(true);
      std::fprintf(stderr,
                   "[functrace-window] tracing %.1fs starting now\n", dur_s);
    }
  } else {
    if (fable2::functrace::enabled().load()) {
      fable2::functrace::set_enabled(false);
      fable2::functrace::flush();
      std::fprintf(stderr, "[functrace-window] window done, log flushed\n");
    }
  }
}

}  // namespace fable2::functrace_window

namespace fable2::fpsmeter {

inline bool enabled() {
  static const bool e = [] {
    const char* v = std::getenv("FABLE2_FPS_METER");
    return v && v[0] == '1';
  }();
  return e;
}

}  // namespace fable2::fpsmeter

namespace fable2::guest_work_log {

// "[frame] guest" log line every 300 guest frames (raw per-frame medians from
// guest_frame_rate.h), for A/B runs read from the log: on with the native
// renderer or discovery (capture layer enabled), or FABLE2_GUEST_WORK_LOG=1.
inline bool enabled() {
  static const bool env = [] {
    const char* v = std::getenv("FABLE2_GUEST_WORK_LOG");
    return v && v[0] == '1';
  }();
  return env || fable2::native::capture::Enabled();
}

// Command-processor (GPU emulation thread) time per closed GPU frame, sampled
// once per guest frame from the SDK's emulated-frame stats (FABLE2_GUEST_WORK_LOG=1
// also turns the collection on). A GPU frame that repeats across guest frames
// is counted once; one skipped is missed. Render thread only.
struct CpWindows {
  diagnostics::WindowStats busy_ms{300}, wait_reg_ms{300}, idle_ms{300}, present_ms{300},
      draws{300}, draw_cpu_ms{300}, copies{300}, copy_cpu_ms{300}, interval_ms{300},
      submissions{300}, submit_ms{300}, fence_waits{300}, fence_ms{300}, tile_mappings{300},
      kicks{300}, tiles{300}, pred_draws{300}, pred_skips{300}, bin_selects{300},
      gpu_busy_ms{300}, gpu_timed{300};
  uint64_t last_gpu_frame = 0;
  bool acquired = false;
};
inline CpWindows& cp_windows() {
  static CpWindows w;
  return w;
}

inline void sample_cp() {
  if (!enabled()) return;
  CpWindows& c = cp_windows();
  if (!c.acquired) {
    rex::graphics::AcquireEmulatedFrameStats();
    c.acquired = true;
  }
  rex::graphics::EmulatedFrameStats gpu;
  if (!rex::graphics::GetLastEmulatedFrameStats(&gpu) || gpu.frame == c.last_gpu_frame) return;
  c.last_gpu_frame = gpu.frame;
  c.busy_ms.Add(double(gpu.cp_busy_ns) / 1e6);
  c.wait_reg_ms.Add(double(gpu.cp_wait_reg_ns) / 1e6);
  c.idle_ms.Add(double(gpu.cp_idle_ns) / 1e6);
  c.present_ms.Add(double(gpu.cp_present_ns) / 1e6);
  c.draws.Add(double(gpu.draws));
  c.draw_cpu_ms.Add(double(gpu.draw_cpu_ns) / 1e6);
  c.copies.Add(double(gpu.copies));
  c.copy_cpu_ms.Add(double(gpu.copy_cpu_ns) / 1e6);
  c.interval_ms.Add(double(gpu.swap_interval_ns) / 1e6);
  c.submissions.Add(double(gpu.submissions));
  c.submit_ms.Add(double(gpu.submit_ns) / 1e6);
  c.fence_waits.Add(double(gpu.fence_waits));
  c.fence_ms.Add(double(gpu.fence_wait_ns) / 1e6);
  c.tile_mappings.Add(double(gpu.tile_mappings));
  c.kicks.Add(double(gpu.primary_buffer_ends));
  c.tiles.Add(double(gpu.tiles));
  c.pred_draws.Add(double(gpu.predicated_draws));
  c.pred_skips.Add(double(gpu.predicated_skips));
  c.bin_selects.Add(double(gpu.bin_select_writes));
  c.gpu_busy_ms.Add(double(gpu.gpu_busy_ns) / 1e6);
  c.gpu_timed.Add(double(gpu.gpu_timed_submissions));
}

inline void poll() {
  auto& w = fable2::guest_frame_rate::windows();
  if (!w.work_ms.full()) return;
  if (enabled()) {
    const double mean_frame = w.frame_ms.Mean();
    REXSYS_INFO(
        "[frame] guest {} frames: {:.1f} fps, frame median {:.2f} ms, work median {:.2f} ms "
        "(p90 {:.2f}), swap median {:.2f} ms, wait median {:.2f} ms",
        w.work_ms.size(), mean_frame > 0 ? 1000.0 / mean_frame : 0.0, w.frame_ms.Median(),
        w.work_ms.Median(), w.work_ms.Percentile(0.9), w.swap_ms.Median(), w.wait_ms.Median());
    CpWindows& c = cp_windows();
    if (c.busy_ms.size() > 0) {
      REXSYS_INFO(
          "[frame] cp {} gpu frames: interval median {:.2f} ms, busy median {:.2f} ms (p90 {:.2f}, "
          "max {:.2f}), wait_reg median {:.2f} ms, idle median {:.2f} ms, present median {:.2f} ms; "
          "draws median {:.0f} (draw cpu {:.2f} ms), resolves median {:.0f} ({:.2f} ms)",
          c.busy_ms.size(), c.interval_ms.Median(), c.busy_ms.Median(), c.busy_ms.Percentile(0.9),
          c.busy_ms.Max(), c.wait_reg_ms.Median(), c.idle_ms.Median(), c.present_ms.Median(),
          c.draws.Median(), c.draw_cpu_ms.Median(), c.copies.Median(), c.copy_cpu_ms.Median());
      REXSYS_INFO(
          "[frame] cp queue: submissions median {:.0f} (max {:.0f}) in {:.2f} ms (p90 {:.2f}), "
          "fence waits median {:.0f} (max {:.0f}) blocked {:.2f} ms (p90 {:.2f}, max {:.2f}), "
          "tile mappings median {:.0f} (max {:.0f}), ring kicks median {:.0f}",
          c.submissions.Median(), c.submissions.Max(), c.submit_ms.Median(),
          c.submit_ms.Percentile(0.9), c.fence_waits.Median(), c.fence_waits.Max(),
          c.fence_ms.Median(), c.fence_ms.Percentile(0.9), c.fence_ms.Max(),
          c.tile_mappings.Median(), c.tile_mappings.Max(), c.kicks.Median());
      REXSYS_INFO(
          "[frame] cp tiling: tiles median {:.0f}, bin selects median {:.0f}, predicated draws "
          "median {:.0f}, predicated packets skipped median {:.0f}",
          c.tiles.Median(), c.bin_selects.Median(), c.pred_draws.Median(), c.pred_skips.Median());
      if (c.gpu_timed.Max() > 0) {
        REXSYS_INFO(
            "[frame] gpu: busy median {:.2f} ms (p90 {:.2f}, max {:.2f}) over {:.0f} timed "
            "submissions a frame (worker timestamps; the result lands a frame or two late)",
            c.gpu_busy_ms.Median(), c.gpu_busy_ms.Percentile(0.9), c.gpu_busy_ms.Max(),
            c.gpu_timed.Median());
      }
    }
    c.busy_ms.Reset();
    c.wait_reg_ms.Reset();
    c.idle_ms.Reset();
    c.present_ms.Reset();
    c.draws.Reset();
    c.draw_cpu_ms.Reset();
    c.copies.Reset();
    c.copy_cpu_ms.Reset();
    c.interval_ms.Reset();
    c.submissions.Reset();
    c.submit_ms.Reset();
    c.fence_waits.Reset();
    c.fence_ms.Reset();
    c.tile_mappings.Reset();
    c.kicks.Reset();
    c.tiles.Reset();
    c.pred_draws.Reset();
    c.pred_skips.Reset();
    c.bin_selects.Reset();
    c.gpu_busy_ms.Reset();
    c.gpu_timed.Reset();
  }
  w.frame_ms.Reset();
  w.work_ms.Reset();
  w.swap_ms.Reset();
  w.wait_ms.Reset();
}

}  // namespace fable2::guest_work_log

// Defined in src/core/fable2_boot_save.h, which main.cpp includes after this header.
namespace fable2::bootsave {
inline void OnFrame(PPCContext& ctx, uint8_t* base);
}

extern "C" void MainRenderLoop_82B9CD68(PPCContext& ctx, uint8_t* base) {
  const int64_t frame_us =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();
  // Guest frame time for the F3 overlay (see guest_frame_rate.h).
  fable2::guest_frame_rate::record_frame(frame_us);
  // Periodic guest work log line (A/B measurements, see guest_work_log).
  fable2::guest_work_log::sample_cp();
  fable2::guest_work_log::poll();
  // Bounded unfiltered trace window (FABLE2_TRACE_WINDOW=1); see above.
  fable2::functrace_window::run_window(frame_us);
  if (fable2::fpsmeter::enabled()) {
    static std::atomic<uint64_t> calls{0};
    static std::atomic<int64_t> window_start_us{0};
    static std::atomic<uint64_t> window_calls{0};
    static std::ofstream& logf = [] -> std::ofstream& {
      std::remove("fps_meter.log");  // fresh log per run
      static std::ofstream f{"fps_meter.log", std::ios::app};
      return f;
    }();
    const int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
    calls.fetch_add(1, std::memory_order_relaxed);
    int64_t start = window_start_us.load(std::memory_order_relaxed);
    if (start == 0)
      window_start_us.compare_exchange_strong(start, now_us);
    const uint64_t n = window_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == 1 || (now_us - start) >= 5'000'000) {
      window_calls.store(0, std::memory_order_relaxed);
      window_start_us.store(now_us, std::memory_order_relaxed);
      const double secs = (now_us - start) / 1'000'000.0;
      if (logf) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "mainloop rate=%.1f/s (total %llu)\n",
                      n / secs,
                      (unsigned long long)calls.load(std::memory_order_relaxed));
        logf << buf;
        logf.flush();
      }
    }
  }
  // Thread placement experiment (FABLE2_THREAD_PLACE; applied once, inert otherwise).
  fable2::threadplace::PollFrame();
  // F6 native-renderer toggle + overlay request (per frame).
  fable2::native::PollFrame();
  // Native capture frame end: finishes and publishes the frame's scene and
  // advances discovery (inert unless the native renderer or discovery is on).
  fable2::native::capture::OnSwap();
  // D3D census (FABLE2_D3D_CENSUS=<frames>); inert otherwise.
  fable2::d3dcensus::OnFrame();
  // F5 (host) -> run the external Lua file (per-frame, responsive).
  fable2::f5lua::poll_mainloop(ctx, base);
  // FABLE2_BOOT_SAVE: load a named save from the front end (inert when unset).
  fable2::bootsave::OnFrame(ctx, base);
  const auto swap_t0 = std::chrono::steady_clock::now();
  fable2::guest_frame_rate::in_swap() = true;
  __imp__MainRenderLoop_82B9CD68(ctx, base);
  fable2::guest_frame_rate::in_swap() = false;
  fable2::guest_frame_rate::record_swap(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                            swap_t0)
          .count());
}
