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
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <rex/cvar.h>
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

inline bool Active() {
  return FramesRequested() > 0 && FramesWritten().load(std::memory_order_relaxed) < FramesRequested();
}

inline std::FILE* Log() {
  static std::FILE* f = [] {
    char name[96];
    const std::time_t t = std::time(nullptr);
    std::strftime(name, sizeof(name), "logs/d3d_census_%Y%m%d_%H%M%S.jsonl", std::localtime(&t));
    return std::fopen(name, "w");
  }();
  return f;
}

inline void OnCall(uint32_t id, const char* name, const PPCContext& ctx) {
  if (!Active()) return;
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
inline void OnFrame() {
  if (!Active()) return;
  static bool stats_enabled = [] {
    rex::cvar::SetFlagByName("native_render_collect_stats", "true");
    return true;
  }();
  (void)stats_enabled;
  static auto last = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  const double guest_ms = std::chrono::duration<double, std::milli>(now - last).count();
  last = now;

  std::string line;
  char buf[256];
  const int frame = FramesWritten().fetch_add(1) + 1;
  std::snprintf(buf, sizeof(buf), "{\"frame\":%d,\"guest_ms\":%.3f", frame, guest_ms);
  line += buf;

  rex::graphics::EmulatedFrameStats gpu;
  if (rex::graphics::GetLastEmulatedFrameStats(&gpu)) {
    std::snprintf(buf, sizeof(buf),
                  ",\"gpu\":{\"draws\":%u,\"draw_cpu_ms\":%.3f,\"copies\":%u,\"copy_cpu_ms\":%.3f,\"swap_interval_ms\":%.3f,\"pitches\":{",
                  gpu.draws, gpu.draw_cpu_ns / 1e6, gpu.copies, gpu.copy_cpu_ns / 1e6, gpu.swap_interval_ns / 1e6);
    line += buf;
    for (uint32_t i = 0; i < gpu.pitch_count; ++i) {
      std::snprintf(buf, sizeof(buf), "%s\"%u\":%u", i ? "," : "", gpu.pitches[i], gpu.pitch_draws[i]);
      line += buf;
    }
    std::snprintf(buf, sizeof(buf), "},\"other\":%u}", gpu.other_pitch_draws);
    line += buf;
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
  }
}

}  // namespace fable2::d3dcensus

#define FABLE2_D3D_CENSUS_HOOK(ID, NAME, SYM)                                  \
  extern "C" void __imp__##SYM(PPCContext& ctx, uint8_t* base);               \
  extern "C" void SYM(PPCContext& __restrict ctx, uint8_t* base) {            \
    fable2::d3dcensus::OnCall(ID, NAME, ctx);                                 \
    __imp__##SYM(ctx, base);                                                  \
  }
#include "fable2_d3d_census_hooks.inc"
#undef FABLE2_D3D_CENSUS_HOOK