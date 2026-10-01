#pragma once

// D3D census (docs/superpowers/plans/2026-10-01-native-renderer-discovery.md):
// strong overrides on the XDK D3D functions mapped by tools/xdk_sigmatch,
// logging per-frame call counts, callers (LR) and r3-r8 samples, plus the
// SDK's emulated-frame stats. Inert unless FABLE2_D3D_CENSUS=<frames>.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging/macros.h>
#include <rex/ppc/func.h>
#include <rex/graphics/native_guest_renderer.h>

namespace fable2::d3dcensus {

struct FuncFrame {
  const char* name = nullptr;
  uint64_t calls = 0;
  std::map<uint32_t, uint64_t> callers;
  std::vector<std::array<uint32_t, 6>> args;
};

inline int FramesRequested() {
  static const int frames = [] {
    const char* v = std::getenv("FABLE2_D3D_CENSUS");
    return v ? std::max(0, std::atoi(v)) : 0;
  }();
  return frames;
}

inline std::mutex& Mutex() { static std::mutex m; return m; }
inline std::map<uint32_t, FuncFrame>& Current() { static std::map<uint32_t, FuncFrame> m; return m; }
inline std::atomic<int>& FramesWritten() { static std::atomic<int> n{0}; return n; }

inline std::atomic<bool>& Armed() { static std::atomic<bool> a{false}; return a; }
inline std::atomic<bool>& Failed() { static std::atomic<bool> a{false}; return a; }

inline double DelaySeconds() {
  static const double d = [] {
    const char* v = std::getenv("FABLE2_D3D_CENSUS_DELAY");
    return v ? std::max(0.0, std::atof(v)) : 0.0;
  }();
  return d;
}

inline bool Active() {
  return FramesRequested() > 0 && !Failed().load(std::memory_order_relaxed) &&
         FramesWritten().load(std::memory_order_relaxed) < FramesRequested();
}

inline std::filesystem::path& LogPath() { static std::filesystem::path p; return p; }

// <exe folder>/logs/d3d_census_<timestamp>.jsonl, independent of the CWD.
inline std::FILE* Log() {
  static std::FILE* f = [] {
    char name[64];
    const std::time_t t = std::time(nullptr);
    std::strftime(name, sizeof(name), "d3d_census_%Y%m%d_%H%M%S.jsonl", std::localtime(&t));
    const std::filesystem::path dir = rex::filesystem::GetExecutableFolder() / "logs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    LogPath() = dir / name;
    std::FILE* fp = std::fopen(LogPath().string().c_str(), "w");
    if (!fp) {
      REXSYS_WARN("[census] cannot open {}; census disabled", LogPath().string());
      Failed().store(true);
    } else {
      REXSYS_INFO("[census] writing {} frames to {}", FramesRequested(), LogPath().string());
    }
    return fp;
  }();
  return f;
}

inline void OnCall(uint32_t id, const char* name, const PPCContext& ctx) {
  if (!Armed().load(std::memory_order_relaxed) || !Active()) return;
  std::lock_guard<std::mutex> lock(Mutex());
  FuncFrame& f = Current()[id];
  f.name = name;
  ++f.calls;
  ++f.callers[uint32_t(ctx.lr)];
  if (f.args.size() < 4) {
    f.args.push_back({ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32});
  }
}

// Per guest frame (MainRenderLoop override). Writes one JSON line and flushes.
// Frame pairing: `funcs` = hooked calls since the previous OnFrame (i.e. the
// previous guest iteration); `gpu` = the last swap the GPU thread closed, which
// may repeat or skip relative to guest iterations (use gpu.gpu_frame to detect).
inline void OnFrame() {
  if (!Active()) return;
  static const auto start = std::chrono::steady_clock::now();
  static auto last = start;
  const auto now = std::chrono::steady_clock::now();
  if (!Armed().load(std::memory_order_relaxed)) {
    const double elapsed = std::chrono::duration<double>(now - start).count();
    if (elapsed < DelaySeconds()) return;
    rex::cvar::SetFlagByName("native_render_collect_stats", "true");
    {
      std::lock_guard<std::mutex> lock(Mutex());
      Current().clear();
    }
    last = now;
    Armed().store(true);
    REXSYS_INFO("[census] armed after {:.1f} s", elapsed);
  }
  const double guest_ms = std::chrono::duration<double, std::milli>(now - last).count();
  last = now;

  std::string line;
  char buf[384];
  const int frame = FramesWritten().fetch_add(1) + 1;
  std::snprintf(buf, sizeof(buf), "{\"frame\":%d,\"guest_ms\":%.3f", frame, guest_ms);
  line += buf;

  rex::graphics::EmulatedFrameStats gpu;
  if (rex::graphics::GetLastEmulatedFrameStats(&gpu)) {
    std::snprintf(buf, sizeof(buf),
                  ",\"gpu\":{\"gpu_frame\":%llu,\"draws\":%u,\"draw_cpu_ms\":%.3f,\"copies\":%u,\"copy_cpu_ms\":%.3f,\"swap_interval_ms\":%.3f,\"pitches\":{",
                  (unsigned long long)gpu.frame, gpu.draws, gpu.draw_cpu_ns / 1e6, gpu.copies, gpu.copy_cpu_ns / 1e6, gpu.swap_interval_ns / 1e6);
    line += buf;
    for (uint32_t i = 0; i < gpu.pitch_count; ++i) {
      std::snprintf(buf, sizeof(buf), "%s\"%u\":%u", i ? "," : "", gpu.pitches[i], gpu.pitch_draws[i]);
      line += buf;
    }
    std::snprintf(buf, sizeof(buf),
                  "},\"other\":%u,\"bin_selects\":%u,\"tiles\":%u,\"pred_draws\":%u,\"pred_skips\":%u,\"tile_selects\":[",
                  gpu.other_pitch_draws, gpu.bin_select_writes, gpu.tiles, gpu.predicated_draws,
                  gpu.predicated_skips);
    line += buf;
    for (uint32_t i = 0; i < gpu.tiles; ++i) {
      std::snprintf(buf, sizeof(buf), "%s\"0x%llX\"", i ? "," : "",
                    (unsigned long long)gpu.tile_selects[i]);
      line += buf;
    }
    line += "],\"extents\":{";
    // Per pitch: [window scissor BR x, y, viewport w, h] (max over the frame).
    bool first_extent = true;
    for (uint32_t i = 0; i < gpu.pitch_count; ++i) {
      const auto& w = gpu.pitch_window_br[i];
      const auto& sc = gpu.pitch_viewport[i];
      if (!(w[0] | w[1] | sc[0] | sc[1])) continue;
      std::snprintf(buf, sizeof(buf), "%s\"%u\":[%u,%u,%u,%u]", first_extent ? "" : ",",
                    gpu.pitches[i], w[0], w[1], sc[0], sc[1]);
      line += buf;
      first_extent = false;
    }
    line += "}}";
  }

  line += ",\"funcs\":{";
  {
    std::lock_guard<std::mutex> lock(Mutex());
    bool first_fn = true;
    for (auto& [id, f] : Current()) {
      std::snprintf(buf, sizeof(buf), "%s\"%s\":{\"calls\":%llu,\"callers\":{", first_fn ? "" : ",",
                    f.name, (unsigned long long)f.calls);
      line += buf;
      first_fn = false;
      std::vector<std::pair<uint64_t, uint32_t>> top;
      for (auto& [lr, n] : f.callers) top.push_back({n, lr});
      std::sort(top.rbegin(), top.rend());
      for (size_t i = 0; i < top.size() && i < 8; ++i) {
        std::snprintf(buf, sizeof(buf), "%s\"0x%08X\":%llu", i ? "," : "", top[i].second,
                      (unsigned long long)top[i].first);
        line += buf;
      }
      line += "},\"args\":[";
      for (size_t i = 0; i < f.args.size(); ++i) {
        const auto& a = f.args[i];
        std::snprintf(buf, sizeof(buf),
                      "%s[\"0x%08X\",\"0x%08X\",\"0x%08X\",\"0x%08X\",\"0x%08X\",\"0x%08X\"]",
                      i ? "," : "", a[0], a[1], a[2], a[3], a[4], a[5]);
        line += buf;
      }
      line += "]}";
    }
    Current().clear();
  }
  line += "}}\n";
  if (std::FILE* f = Log()) {
    std::fputs(line.c_str(), f);
    std::fflush(f);
    if (frame >= FramesRequested()) {
      rex::cvar::SetFlagByName("native_render_collect_stats", "false");
      REXSYS_INFO("[census] complete: {} frames written to {}", frame, LogPath().string());
    }
  }
}

}  // namespace fable2::d3dcensus
