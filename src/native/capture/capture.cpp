#include "capture.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_M_X64) || defined(__x86_64__)
#include <intrin.h>
#endif
#include <thread>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/logging/macros.h>
#include <rex/ppc/context.h>

#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif
#include <xxhash.h>

#include "discovery_format.h"
#include "draw_nesting.h"
#include "draw_record.h"
#include "guest_read.h"
#include "main_scene.h"
#include "material.h"
#include "position_decode.h"
#include "bone_skin.h"
#include "instance_expand.h"
#include "shader_tally.h"
#include "stream_resolve.h"
#include "terrain_patch.h"
#include "vfetch_decode.h"
#include "window_stats.h"
#include "xdk_hook_ids.h"
#include "xdk_layout.h"
#include "../render/texture_decode.h"

REXCVAR_DECLARE(bool, fable2_native_render);

namespace fable2::native::capture {
namespace {

// Per-hook arrays are indexed by hook id; every generated id must fit.
constexpr uint32_t kMaxHookIds = 128;
#define FABLE2_XDK_HOOK(ID, NAME, SYM) \
  static_assert((ID) < kMaxHookIds, "hook id " #ID " does not fit the per-hook arrays");
#include "xdk_hooks.inc"
#undef FABLE2_XDK_HOOK

struct LastArgs {
  uint32_t r[8] = {};  // r3..r10
};

// Binding state seen through the hooks. The device fields (DeviceSnapshot) are
// authoritative at draw time; these stay as evidence in the raw rows.
struct DrawState {
  uint32_t device = 0;
  uint32_t stream_obj[4] = {};
  uint32_t stream_offset[4] = {};
  uint32_t stream_args[4][2] = {};  // r7 stride bytes, r8 dirty mask
  uint32_t ib_obj = 0;
  uint32_t vs_obj = 0;  // "SetPixelShader?" r4: the real SetVertexShader (xdk_layout.h)
  uint32_t ps_obj = 0;  // "SetVertexShader?" r4: the real SetPixelShader
  uint32_t vs_bank_ptr = 0;
  uint32_t ps_bank_ptr = 0;
};

// Binding state read from the guest device at draw time (xdk_layout.h).
struct DeviceSnapshot {
  uint32_t stream_obj[xdk::kMaxStreams] = {};
  uint32_t stream_stride_dw[xdk::kMaxStreams] = {};
  uint32_t stream_fc[xdk::kMaxStreams][2] = {};  // fetch slot 95 - i
  uint32_t ib_obj = 0;
  uint32_t vs_obj = 0;
  uint32_t ps_obj = 0;
};

constexpr uint32_t kDeviceDumpOffset = 0x480;
constexpr uint32_t kDeviceDumpDwords = 24;
constexpr uint32_t kDeviceSnapshotBytes = xdk::kVsDeviceFieldOffset + 4;  // highest field read
static_assert(xdk::kPsDeviceFieldOffset < xdk::kVsDeviceFieldOffset &&
              xdk::kDeviceStreamStrideOffset + xdk::kMaxStreams <= kDeviceSnapshotBytes);

std::atomic<bool> g_enabled{false};    // native renderer on, or discovery requested
std::atomic<bool> g_render{false};     // native renderer on: build DrawRecords
std::atomic<bool> g_armed{false};      // discovery rows are being written
// Discovery requested and not finished: the hook-side DrawState evidence the
// rows carry is maintained (under g_mutex) only while this is set.
std::atomic<bool> g_discovery_active{false};
// A consumer wants draw records (SetRecordsWanted, from PollFrame), and the
// value latched for the current guest frame at the previous swap. With it
// clear, main-scene draws are only counted.
std::atomic<bool> g_records_wanted{false};
std::atomic<bool> g_records_frame{false};
std::atomic<bool> g_failed{false};
std::atomic<uint64_t> g_swaps{0};      // guest frames since start (while enabled)
std::atomic<int> g_frames_written{0};  // discovery frames completed
std::atomic<uint64_t> g_draw_count{0};
std::atomic<uint64_t> g_nested_draws{0};  // draw hooks entered inside another draw hook
std::atomic<uint32_t> g_vs_bank_ptr{0};   // vertex constant bank pointer (lock-free, records)
std::atomic<uint32_t> g_ps_bank_ptr{0};   // pixel constant bank pointer (lock-free, records)

std::mutex g_mutex;  // DrawState + discovery file
DrawState g_state;

// --- Guest-thread capture timer ---------------------------------------------
// The capture work done on guest threads (hook returns, discovery state
// updates, the swap) is timed with the TSC, which is cheap enough to read
// around every draw; g_ticks_per_ms is calibrated against steady_clock in
// SetMemory. Ticks accumulate per guest frame in g_capture_ticks.
std::atomic<uint64_t> g_capture_ticks{0};
double g_ticks_per_ms = 1e6;  // written once in SetMemory, before any hook runs
fable2::diagnostics::WindowStats g_capture_window(300);  // OnSwap thread only

inline uint64_t Ticks() {
#if defined(_M_X64) || defined(__x86_64__)
  return __rdtsc();
#else
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
#endif
}

void CalibrateTicks() {
#if defined(_M_X64) || defined(__x86_64__)
  const auto t0 = std::chrono::steady_clock::now();
  const uint64_t c0 = Ticks();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const uint64_t c1 = Ticks();
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (ms > 0 && c1 > c0) g_ticks_per_ms = double(c1 - c0) / ms;
#endif
}

// Adds the scope's duration to the frame's capture time.
struct CaptureTimer {
  uint64_t t0 = Ticks();
  ~CaptureTimer() { g_capture_ticks.fetch_add(Ticks() - t0, std::memory_order_relaxed); }
  CaptureTimer() = default;
  CaptureTimer(const CaptureTimer&) = delete;
  CaptureTimer& operator=(const CaptureTimer&) = delete;
};

// The swap's own capture work: everything up to the frame's end counts for
// that frame (TakeFrame), the rest (publishing, logging) for the next one.
struct SwapTimer {
  uint64_t t0 = Ticks();
  ~SwapTimer() { g_capture_ticks.fetch_add(Ticks() - t0, std::memory_order_relaxed); }
  // The finished frame's capture time in ms; restarts the scope's clock.
  double TakeFrameMs() {
    const uint64_t now = Ticks();
    const uint64_t total = g_capture_ticks.exchange(0, std::memory_order_relaxed) + (now - t0);
    t0 = now;
    return double(total) / g_ticks_per_ms;
  }
  SwapTimer() = default;
  SwapTimer(const SwapTimer&) = delete;
  SwapTimer& operator=(const SwapTimer&) = delete;
};

// Draws per color-surface pitch (first 16 distinct pitches, the rest in `other`).
struct PitchHistogram {
  uint32_t pitch[16] = {};
  uint32_t count[16] = {};
  uint32_t n = 0;
  uint32_t other = 0;
  void Add(uint32_t p) {
    for (uint32_t i = 0; i < n; ++i) {
      if (pitch[i] == p) {
        ++count[i];
        return;
      }
    }
    if (n == 16) {
      ++other;
      return;
    }
    pitch[n] = p;
    count[n++] = 1;
  }
};

// Per-frame evidence for the bracket, written in the frame row.
struct SceneEvidence {
  uint32_t flag_bytes[256] = {};  // draws per device tiling flag byte value
  uint32_t flag_unread = 0;       // draws whose flag byte could not be read
  PitchHistogram pitch_in;        // RB_SURFACE_INFO pitch of draws in the bracket
  PitchHistogram pitch_out;       // ... and outside it
  uint32_t ib_in = 0;             // IndirectBuffer:82286248 inserts in the bracket
  uint32_t ib_out = 0;
};

// Main-scene bracket (main_scene.h), driven by every hooked guest draw.
std::mutex g_scene_mutex;  // builder + per-frame bracket counts and evidence
render::FrameBuilder g_builder;
BracketStats g_bracket;
SceneEvidence g_evidence;
uint32_t g_unsupported_by_hook[kMaxHookIds] = {};  // kUnsupportedPrim records per hook id this frame
std::filesystem::path g_path;
std::FILE* g_file = nullptr;
// Discovery texture dumps (caller holds g_mutex): the capture's dump folder,
// the (base address, identity) pairs already dumped and how many were written.
std::filesystem::path g_tex_dir;
std::set<std::pair<uint32_t, uint64_t>> g_tex_seen;
uint32_t g_tex_count = 0;
// Discovery stream dumps (caller holds g_mutex): the capture's dump folder, the
// content hashes already dumped per (address, size), files and bytes written.
std::filesystem::path g_geo_dir;
std::map<std::pair<uint32_t, uint32_t>, std::vector<uint64_t>> g_geo_seen;
uint32_t g_geo_count = 0;
uint64_t g_geo_bytes = 0;

int FramesRequested() {
  static const int frames = [] {
    const char* v = std::getenv("FABLE2_NATIVE_DISCOVERY");
    return v ? std::max(0, std::atoi(v)) : 0;
  }();
  return frames;
}

double DelaySeconds() {
  static const double d = [] {
    const char* v = std::getenv("FABLE2_NATIVE_DISCOVERY_DELAY");
    return v ? std::max(0.0, std::atof(v)) : 0.0;
  }();
  return d;
}

uint64_t Every() {
  static const uint64_t n = [] {
    const char* v = std::getenv("FABLE2_NATIVE_DISCOVERY_EVERY");
    const long long x = v ? std::atoll(v) : 64;
    return uint64_t(x > 0 ? x : 64);
  }();
  return n;
}

thread_local std::array<LastArgs, kMaxHookIds> t_last_args;  // indexed by hook id
thread_local DrawNesting t_nesting;  // only the outermost draw of a call chain is recorded

// World-view-projection constants per vertex shader hash, found offline by
// tools/xdk_sigmatch/matrix_finder.py or confirmed by hand from the shader's
// disassembly (docs/native-renderer/vs-transforms.json, frame-map section 9).
// The generated table also holds per-shader position swizzles, rigid skin
// layouts and terrain parameters.
struct TableEntry {
  uint64_t hash;
  uint32_t base;
  uint8_t layout;
  int pos_fetch;
  uint8_t deformed;  // the shader moves the position first (vs-transforms.json "deformed")
};
static constexpr TableEntry kTransformTable[] = {
#define FABLE2_VS_TRANSFORM(H, B, L, P, D) {H, B, L, P, D},
#define FABLE2_VS_POS_SWIZZLE(H, S)
#define FABLE2_VS_SKIN(H, I, W, R0, R1, R2, S0, S1, S2, N, IC0, WC0, IC1, WC1, IC2, WC2, IC3, WC3)
#define FABLE2_VS_INSTANCE(H, R0, R1, R2, S0, S1, S2, INV, CNT, FIRST, BIAS, OFF, CE, CD)
#define FABLE2_VS_TERRAIN(H, G, CE, HS, O, TO, TS, PO, F)
#define FABLE2_VS_UV(H, I, C, F, S, FM, O, S0, O0, S1, O1)
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_INSTANCE
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
};

struct PosSwizzleEntry {
  uint64_t hash;
  uint32_t swizzle;
};
static constexpr PosSwizzleEntry kPosSwizzleTable[] = {
#define FABLE2_VS_TRANSFORM(H, B, L, P, D)
#define FABLE2_VS_POS_SWIZZLE(H, S) {H, S},
#define FABLE2_VS_SKIN(H, I, W, R0, R1, R2, S0, S1, S2, N, IC0, WC0, IC1, WC1, IC2, WC2, IC3, WC3)
#define FABLE2_VS_INSTANCE(H, R0, R1, R2, S0, S1, S2, INV, CNT, FIRST, BIAS, OFF, CE, CD)
#define FABLE2_VS_TERRAIN(H, G, CE, HS, O, TO, TS, PO, F)
#define FABLE2_VS_UV(H, I, C, F, S, FM, O, S0, O0, S1, O1)
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_INSTANCE
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
    {0, 0}};

struct SkinEntry {
  uint64_t hash;
  SkinSpec spec;
};
static constexpr SkinEntry kSkinTable[] = {
#define FABLE2_VS_TRANSFORM(H, B, L, P, D)
#define FABLE2_VS_POS_SWIZZLE(H, S)
#define FABLE2_VS_SKIN(H, I, W, R0, R1, R2, S0, S1, S2, N, IC0, WC0, IC1, WC1, IC2, WC2, IC3, WC3) \
  {H, {I, W, {R0, R1, R2}, {S0, S1, S2}, N, {IC0, IC1, IC2, IC3}, {WC0, WC1, WC2, WC3}}},
#define FABLE2_VS_INSTANCE(H, R0, R1, R2, S0, S1, S2, INV, CNT, FIRST, BIAS, OFF, CE, CD)
#define FABLE2_VS_TERRAIN(H, G, CE, HS, O, TO, TS, PO, F)
#define FABLE2_VS_UV(H, I, C, F, S, FM, O, S0, O0, S1, O1)
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_INSTANCE
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
    {0, {-1, -1, {-1, -1, -1}, {0, 0, 0}, 1, {0, 0, 0, 0}, {0, 0, 0, 0}}}};

struct InstanceEntry {
  uint64_t hash;
  InstanceSpec spec;
};
static constexpr InstanceEntry kInstanceTable[] = {
#define FABLE2_VS_TRANSFORM(H, B, L, P, D)
#define FABLE2_VS_POS_SWIZZLE(H, S)
#define FABLE2_VS_SKIN(H, I, W, R0, R1, R2, S0, S1, S2, N, IC0, WC0, IC1, WC1, IC2, WC2, IC3, WC3)
#define FABLE2_VS_INSTANCE(H, R0, R1, R2, S0, S1, S2, INV, CNT, FIRST, BIAS, OFF, CE, CD) \
  {H, {{R0, R1, R2}, {S0, S1, S2}, INV, CNT, FIRST, BIAS, OFF, CE, CD}},
#define FABLE2_VS_TERRAIN(H, G, CE, HS, O, TO, TS, PO, F)
#define FABLE2_VS_UV(H, I, C, F, S, FM, O, S0, O0, S1, O1)
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_INSTANCE
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
    {0, {{-1, -1, -1}, {0, 0, 0}, 0, 0, 0, 0.0f, 0, -1, -1}}};

struct TerrainEntry {
  uint64_t hash;
  TerrainSpec spec;
};
static constexpr TerrainEntry kTerrainTable[] = {
#define FABLE2_VS_TRANSFORM(H, B, L, P, D)
#define FABLE2_VS_POS_SWIZZLE(H, S)
#define FABLE2_VS_SKIN(H, I, W, R0, R1, R2, S0, S1, S2, N, IC0, WC0, IC1, WC1, IC2, WC2, IC3, WC3)
#define FABLE2_VS_INSTANCE(H, R0, R1, R2, S0, S1, S2, INV, CNT, FIRST, BIAS, OFF, CE, CD)
#define FABLE2_VS_TERRAIN(H, G, CE, HS, O, TO, TS, PO, F) {H, {G, CE, HS, O, TO, TS, PO, F}},
#define FABLE2_VS_UV(H, I, C, F, S, FM, O, S0, O0, S1, O1)
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_INSTANCE
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
    {0, {0, 0, 0, 0, 0, 0, -1, 0}}};

// Albedo texture slot and UV interpolators per pixel shader hash
// (docs/native-renderer/ps-albedo.json, tools/xdk_sigmatch/gen_albedo_table.py)
// and the vertex fetch behind each UV interpolator component per vertex
// shader hash (vs-transforms.json "uv"). Either may be empty.
const std::vector<AlbedoSpec> kAlbedoTable = {
#define FABLE2_PS_ALBEDO(H, SL, UI, UC, US0, UO0, US1, UO1, VI, VC, VS0, VO0, VS1, VO1) \
  {H, int8_t(SL), UI, UC, VI, VC, {{US0, UO0}, {US1, UO1}}, {{VS0, VO0}, {VS1, VO1}}},
#define FABLE2_PS_NO_ALBEDO(H) {H, int8_t(-1), 0, 0, 0, 0, {}, {}},
#include "ps_albedo_table.inc"
#undef FABLE2_PS_ALBEDO
#undef FABLE2_PS_NO_ALBEDO
};
const std::vector<VsUvSpec> kVsUvTable = {
#define FABLE2_VS_TRANSFORM(H, B, L, P, D)
#define FABLE2_VS_POS_SWIZZLE(H, S)
#define FABLE2_VS_SKIN(H, I, W, R0, R1, R2, S0, S1, S2, N, IC0, WC0, IC1, WC1, IC2, WC2, IC3, WC3)
#define FABLE2_VS_INSTANCE(H, R0, R1, R2, S0, S1, S2, INV, CNT, FIRST, BIAS, OFF, CE, CD)
#define FABLE2_VS_TERRAIN(H, G, CE, HS, O, TO, TS, PO, F)
#define FABLE2_VS_UV(H, I, C, F, S, FM, O, S0, O0, S1, O1) \
  {H, I, C, int8_t(F), S, FM, O, {{S0, O0}, {S1, O1}}},
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_INSTANCE
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
};

// Linear scans; callers cache the result per shader (PsInfo, VsInfo).
const AlbedoSpec* FindAlbedo(uint64_t ps_hash) {
  for (const AlbedoSpec& e : kAlbedoTable) {
    if (e.ps_hash == ps_hash) return &e;
  }
  return nullptr;
}

const VsUvSpec* FindVsUv(uint64_t vs_hash, uint8_t interp, uint8_t comp) {
  for (const VsUvSpec& e : kVsUvTable) {
    if (e.vs_hash == vs_hash && e.interp == interp && e.comp == comp) return &e;
  }
  return nullptr;
}

const TransformInfo* FindTransform(uint64_t hash) {
  static const std::unordered_map<uint64_t, TransformInfo> table = [] {
    std::unordered_map<uint64_t, TransformInfo> m;
    for (const TableEntry& e : kTransformTable) {
      m[e.hash] = TransformInfo{e.base, TransformLayout(e.layout), e.pos_fetch, e.deformed != 0};
    }
    for (const PosSwizzleEntry& e : kPosSwizzleTable) {
      if (auto it = m.find(e.hash); e.hash && it != m.end()) it->second.pos_swizzle = e.swizzle;
    }
    return m;
  }();
  const auto it = table.find(hash);
  return it == table.end() ? nullptr : &it->second;
}

const SkinSpec* FindSkin(uint64_t hash) {
  for (const SkinEntry& e : kSkinTable) {
    if (e.hash && e.hash == hash) return &e.spec;
  }
  return nullptr;
}

const InstanceSpec* FindInstance(uint64_t hash) {
  for (const InstanceEntry& e : kInstanceTable) {
    if (e.hash && e.hash == hash) return &e.spec;
  }
  return nullptr;
}

const TerrainSpec* FindTerrain(uint64_t hash) {
  for (const TerrainEntry& e : kTerrainTable) {
    if (e.hash && e.hash == hash) return &e.spec;
  }
  return nullptr;
}

bool IsDrawId(uint32_t id) {
  switch (id) {
    case kHook_D3DDevice_DrawIndexedVertices:
    case kHook_D3DDevice_DrawVertices:
    case kHook_D3DDevice_BeginVertices:
    case kHook_DrawIndx_8221C9C8:
    case kHook_DrawIndx_82217EE8:
    case kHook_DrawIndx_82207C30:
    case kHook_DrawIndx_82B9C068:
    case kHook_DrawIndx2_821EF988:
    case kHook_DrawIndx2_82B928A8:
    case kHook_DrawIndx2_82B98770:
    case kHook_DrawIndx2_82B988C8:
    case kHook_DrawIndx2_82BA29B8:
    case kHook_DrawIndx2_82BA5448:
    case kHook_DrawIndx2_82BA5CE8:
      return true;
    default:
      return false;
  }
}

const char* HookName(uint32_t id) {
  static const std::unordered_map<uint32_t, const char*> names = {
#define FABLE2_XDK_HOOK(ID, NAME, SYM) {ID, NAME},
#include "xdk_hooks.inc"
#undef FABLE2_XDK_HOOK
  };
  const auto it = names.find(id);
  return it == names.end() ? "?" : it->second;
}

// Object dwords as a JSON array (big-endian guest words), or "null".
std::string ObjectDwords(uint32_t guest_address, uint32_t count) {
  count = std::min<uint32_t>(count, 64);
  const uint8_t* p = ReadVirtual(guest_address, count * 4);
  if (!p) return "null";
  uint32_t v[64];
  for (uint32_t i = 0; i < count; ++i) v[i] = LoadBe32(p + i * 4);
  return HexDwords(v, count);
}

bool ReadVirtualBe32(uint32_t guest_address, uint32_t* out) {
  const uint8_t* p = ReadVirtual(guest_address, 4);
  if (!p) return false;
  *out = LoadBe32(p);
  return true;
}

bool ReadDevice(uint32_t device, DeviceSnapshot* s) {
  const uint8_t* d = device ? ReadVirtual(device, kDeviceSnapshotBytes) : nullptr;
  if (!d) return false;
  for (uint32_t i = 0; i < xdk::kMaxStreams; ++i) {
    s->stream_obj[i] = LoadBe32(d + xdk::kDeviceStreamObjectOffset + 4 * i);
    s->stream_stride_dw[i] = d[xdk::kDeviceStreamStrideOffset + i];
    const uint32_t slot = xdk::kStreamFetchSlotBase - i;
    s->stream_fc[i][0] = LoadBe32(d + xdk::kDeviceVertexFetchOffset + 8 * slot);
    s->stream_fc[i][1] = LoadBe32(d + xdk::kDeviceVertexFetchOffset + 8 * slot + 4);
  }
  s->ib_obj = LoadBe32(d + xdk::kDeviceIndexBufferOffset);
  s->vs_obj = LoadBe32(d + xdk::kVsDeviceFieldOffset);
  s->ps_obj = LoadBe32(d + xdk::kPsDeviceFieldOffset);
  return true;
}

void UpdateState(uint32_t id, const PPCContext& ctx) {
  // Records read only the constant bank pointers, lock-free (r5 0x4000 vertex,
  // 0x4400 pixel; xdk_layout.h kDeviceVsConstantsOffset / kDevicePsConstantsOffset).
  if (id == kHook_D3DDevice_SetPending_AluConstants) {
    if (ctx.r5.u32 == 0x4000) {
      g_vs_bank_ptr.store(ctx.r6.u32, std::memory_order_relaxed);
    } else if (ctx.r5.u32 == 0x4400) {
      g_ps_bank_ptr.store(ctx.r6.u32, std::memory_order_relaxed);
    }
  }
  // The rest is evidence for discovery rows only.
  if (!g_discovery_active.load(std::memory_order_relaxed)) return;
  CaptureTimer timer;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_state.device = ctx.r3.u32;
  switch (id) {
    case kHook_D3DDevice_SetStreamSource: {
      // 0x821B6DA0: r4 index, r5 object, r6 offset (bytes), r7 stride (bytes),
      // r8 dirty mask OR'ed into device+0x18 (frame-map section 8).
      const uint32_t index = ctx.r4.u32;
      if (index >= 4) break;
      g_state.stream_obj[index] = ctx.r5.u32;
      g_state.stream_offset[index] = ctx.r6.u32;
      g_state.stream_args[index][0] = ctx.r7.u32;
      g_state.stream_args[index][1] = ctx.r8.u32;
      break;
    }
    case kHook_D3DDevice_SetIndices:
      g_state.ib_obj = ctx.r4.u32;
      break;
    case kHook_D3DDevice_SetPixelShader:  // misnamed: stores device+0x3198 (vertex)
      g_state.vs_obj = ctx.r4.u32;
      break;
    case kHook_D3DDevice_SetVertexShader:  // misnamed: stores device+0x3194 (pixel)
      g_state.ps_obj = ctx.r4.u32;
      break;
    case kHook_D3DDevice_SetPending_AluConstants:
      if (ctx.r5.u32 == 0x4000) g_state.vs_bank_ptr = ctx.r6.u32;
      if (ctx.r5.u32 == 0x4400) g_state.ps_bank_ptr = ctx.r6.u32;
      break;
    default:
      break;
  }
}

void AppendFloat(std::string& s, float f) {
  char buf[32];
  if (std::isnan(f)) {
    s += "NaN";
  } else if (std::isinf(f)) {
    s += f > 0 ? "Infinity" : "-Infinity";
  } else {
    std::snprintf(buf, sizeof(buf), "%.9g", double(f));
    s += buf;
  }
}

// Caller holds g_mutex.
void WriteRawRow(uint32_t id, const LastArgs& args, uint32_t device, const DeviceSnapshot* dev) {
  const DrawState& s = g_state;
  char buf[256];
  std::string row;
  std::snprintf(buf, sizeof(buf), "{\"kind\":\"raw\",\"frame\":%d,\"func\":\"%s\",\"args\":[",
                g_frames_written.load(std::memory_order_relaxed) + 1, HookName(id));
  row += buf;
  for (int i = 0; i < 8; ++i) {
    std::snprintf(buf, sizeof(buf), "%s\"0x%08X\"", i ? "," : "", args.r[i]);
    row += buf;
  }
  // Streams, index buffer and shaders come from the device fields; the hook
  // view is kept under "hook" for comparison.
  std::snprintf(buf, sizeof(buf), "],\"device\":\"0x%08X\",\"streams\":[", device);
  row += buf;
  for (int i = 0; i < 4; ++i) {
    const uint32_t obj = dev ? dev->stream_obj[i] : 0;
    std::snprintf(buf, sizeof(buf),
                  "%s{\"index\":%d,\"obj\":\"0x%08X\",\"stride_dw\":%u,\"fc\":[\"0x%08X\",\"0x%08X\"],"
                  "\"obj_dwords\":",
                  i ? "," : "", i, obj, dev ? dev->stream_stride_dw[i] : 0,
                  dev ? dev->stream_fc[i][0] : 0, dev ? dev->stream_fc[i][1] : 0);
    row += buf;
    row += obj ? ObjectDwords(obj, 16) : "null";
    row += "}";
  }
  const uint32_t ib = dev ? dev->ib_obj : 0;
  std::snprintf(buf, sizeof(buf), "],\"ib\":{\"obj\":\"0x%08X\",\"obj_dwords\":", ib);
  row += buf;
  row += ib ? ObjectDwords(ib, 16) : "null";
  const uint32_t vs = dev ? dev->vs_obj : 0;
  std::snprintf(buf, sizeof(buf), "},\"vs\":{\"obj\":\"0x%08X\",\"obj_dwords\":", vs);
  row += buf;
  row += vs ? ObjectDwords(vs, 16) : "null";
  row += ",\"header_dwords\":";
  row += vs ? ObjectDwords(vs + xdk::kVsHeaderOffset, 12) : "null";
  std::snprintf(buf, sizeof(buf), "},\"ps_obj\":\"0x%08X\"", dev ? dev->ps_obj : 0);
  row += buf;
  std::snprintf(buf, sizeof(buf),
                ",\"hook\":{\"stream0_obj\":\"0x%08X\",\"stream0_offset\":%u,\"stream0_args\":[%u,%u],"
                "\"ib_obj\":\"0x%08X\",\"vs_obj\":\"0x%08X\",\"ps_obj\":\"0x%08X\"}",
                s.stream_obj[0], s.stream_offset[0], s.stream_args[0][0], s.stream_args[0][1],
                s.ib_obj, s.vs_obj, s.ps_obj);
  row += buf;
  std::snprintf(buf, sizeof(buf),
                ",\"vs_bank_ptr\":\"0x%08X\",\"ps_bank_ptr\":\"0x%08X\",\"device_dwords\":{\"0x%X\":",
                s.vs_bank_ptr, s.ps_bank_ptr, kDeviceDumpOffset);
  row += buf;
  row += device ? ObjectDwords(device + kDeviceDumpOffset, kDeviceDumpDwords) : "null";
  row += "}}\n";
  std::fputs(row.c_str(), g_file);
}

struct UcodeRef {
  bool ok = false;
  uint32_t record = 0;  // guest virtual address of the variant record
  uint32_t phys = 0;
  uint32_t bytes = 0;
  uint64_t hash = 0;
  std::vector<uint32_t> dwords;  // host order
};

// Microcode location of a shader object (xdk_layout.h): the record sits at
// header + *(header + record_field); microcode = GpuAddress(base + record
// address dword), size from the record's size dword.
struct UcodeLayout {
  uint32_t base_dword, header_offset, record_field, address_dword, size_dword, size_shift;
};
constexpr UcodeLayout kVsLayout = {xdk::kVsUcodeBaseDword,    xdk::kVsHeaderOffset,
                                   xdk::kVsRecordOffsetField, xdk::kVsUcodeAddressDword,
                                   xdk::kVsUcodeSizeDword,    xdk::kVsUcodeSizeShift};
constexpr UcodeLayout kPsLayout = {xdk::kPsUcodeBaseDword,    xdk::kPsHeaderOffset,
                                   xdk::kPsRecordOffsetField, xdk::kPsUcodeAddressDword,
                                   xdk::kPsUcodeSizeDword,    xdk::kPsUcodeSizeShift};

// The record's microcode address and size, without reading the microcode.
// `record_index` selects the vertex shader variant (8 bytes per entry).
bool ReadUcodeRecord(const UcodeLayout& l, uint32_t obj, uint32_t record_index, UcodeRef* u) {
  uint32_t base = 0, rec_off = 0, offset = 0, size = 0;
  const uint32_t header = obj + l.header_offset;
  if (!ReadVirtualBe32(obj + 4 * l.base_dword, &base) ||
      !ReadVirtualBe32(header + l.record_field + 8 * record_index, &rec_off)) {
    return false;
  }
  u->record = header + rec_off;
  if (!ReadVirtualBe32(u->record + 4 * l.address_dword, &offset) ||
      !ReadVirtualBe32(u->record + 4 * l.size_dword, &size)) {
    return false;
  }
  u->bytes = size << l.size_shift;
  u->phys = xdk::GpuAddress(base + offset);
  return u->bytes != 0 && u->bytes % 4 == 0 && u->bytes <= 0x40000;
}

UcodeRef ReadUcode(const UcodeLayout& l, uint32_t obj, uint32_t record_index) {
  UcodeRef u;
  if (!ReadUcodeRecord(l, obj, record_index, &u)) return u;
  const uint8_t* p = ReadPhysical(u.phys, u.bytes);
  if (!p) return u;
  u.hash = XXH3_64bits(p, u.bytes);  // the emulator's ucode_data_hash (raw guest bytes)
  u.dwords.resize(u.bytes / 4);
  for (size_t i = 0; i < u.dwords.size(); ++i) u.dwords[i] = LoadBe32(p + 4 * i);
  u.ok = true;
  return u;
}

// Variant `v` of a vertex shader object's microcode (xdk_layout.h).
UcodeRef ReadVsUcode(uint32_t obj, uint32_t v) { return ReadUcode(kVsLayout, obj, v); }

// A pixel shader object's microcode (xdk_layout.h, kPs*; one record, loaded
// unpatched by both the shader flush and GpuLoadShaders).
UcodeRef ReadPsUcode(uint32_t obj) { return ReadUcode(kPsLayout, obj, 0); }

// Microcode size in bytes of variant `v` of a vertex shader object, from its
// variant record (xdk_layout.h) without reading the microcode.
bool ReadVsUcodeBytes(uint32_t obj, uint32_t v, uint32_t* bytes) {
  uint32_t rec_off = 0, size = 0;
  const uint32_t header = obj + xdk::kVsHeaderOffset;
  if (!ReadVirtualBe32(header + xdk::kVsRecordOffsetField + 8 * v, &rec_off) ||
      !ReadVirtualBe32(header + rec_off + 4 * xdk::kVsUcodeSizeDword, &size)) {
    return false;
  }
  *bytes = size << xdk::kVsUcodeSizeShift;
  return *bytes != 0 && *bytes % 4 == 0 && *bytes <= 0x40000;
}

// The last vertex shader the XDK loaded with IM_LOAD_IMMEDIATE on this thread
// (patched copy in the command buffer, xdk_layout.h). Draws that do not reload
// the shader keep using it on the GPU.
struct VsLoad {
  uint32_t obj = 0;   // the GPU runs this object's immediate copy (0: none)
  uint32_t copy = 0;  // guest virtual address of the copy
  uint32_t bytes = 0;
  bool usable = false;  // copy validated and hashed at load time
  uint64_t hash = 0;
  bool have_dwords = false;      // `dwords` converted for this load
  std::vector<uint32_t> dwords;  // host order, reused across loads
};
thread_local VsLoad t_vs_load;

void OnVsLoadImmediate(const LastArgs& a) {
  const uint32_t device = a.r[0], obj = a.r[2], variant = a.r[7];
  t_vs_load.obj = 0;
  t_vs_load.usable = false;
  t_vs_load.have_dwords = false;
  if (!obj || variant >= 2) return;
  if (!g_records_frame.load(std::memory_order_relaxed) &&
      !g_discovery_active.load(std::memory_order_relaxed)) {
    // Nobody reads the copy this frame: only remember that the GPU runs this
    // object's immediate copy, so its draws are never decoded from the
    // object's own microcode. They get no shader until it is reloaded.
    t_vs_load.obj = obj;
    return;
  }
  uint32_t bytes = 0, write = 0;
  if (!ReadVsUcodeBytes(obj, variant, &bytes) ||
      !ReadVirtualBe32(device + xdk::kDeviceCommandWriteOffset, &write)) {
    return;
  }
  const uint32_t n = bytes / 4;
  const uint32_t copy = write + 4 - bytes;
  const uint8_t* p = ReadVirtual(copy - 12, bytes + 12);
  if (!p) return;
  const uint32_t header = xdk::kImLoadImmediateHeader | ((n + 1) << 16);
  if ((LoadBe32(p) & ~1u) != header || LoadBe32(p + 4) != 0 || LoadBe32(p + 8) != n) return;
  t_vs_load.obj = obj;
  t_vs_load.copy = copy;
  t_vs_load.bytes = bytes;
  t_vs_load.hash = XXH3_64bits(p + 12, bytes);
  t_vs_load.usable = true;
}

// The current immediate copy's microcode (host order), converted on first use
// (a shader-cache miss or a discovery row); nullptr if the load is not usable
// or the copy no longer hashes to what was loaded.
const std::vector<uint32_t>* VsLoadDwords() {
  VsLoad& l = t_vs_load;
  if (!l.usable) return nullptr;
  if (l.have_dwords) return &l.dwords;
  const uint8_t* p = ReadVirtual(l.copy, l.bytes);
  if (!p || XXH3_64bits(p, l.bytes) != l.hash) {
    l.usable = false;
    return nullptr;
  }
  l.dwords.resize(l.bytes / 4);
  for (size_t i = 0; i < l.dwords.size(); ++i) l.dwords[i] = LoadBe32(p + 4 * i);
  l.have_dwords = true;
  return &l.dwords;
}

// The last vertex shader GpuLoadShaders (0x82221978: r3 device, r4 vertex
// shader, r5 pixel shader) loaded on this thread. Gameplay indexed draws use
// this GPU-owned path with device+0x3198 null (xdk_layout.h, kVsFallback*).
struct GpuVsLoad {
  uint32_t obj = 0;
  uint32_t variant = 0;
};
thread_local GpuVsLoad t_gpu_vs;
// The pixel shader of that GpuLoadShaders call (r5; 0 = none loaded).
thread_local uint32_t t_gpu_ps = 0;

void OnGpuLoadShaders(const LastArgs& a) {
  const uint32_t vs = a.r[1], ps = a.r[2];
  uint32_t flags = 0;
  t_gpu_ps = ps;
  t_gpu_vs = {};
  if (!vs || !ReadVirtualBe32(vs + xdk::kVsHeaderOffset, &flags)) return;
  t_gpu_vs.obj = vs;
  // 0x822219BC..0x822219C8: the variant flag applies only without a pixel
  // shader; 0x82221A10 forces variant 0 otherwise.
  t_gpu_vs.variant = (!ps && (flags & xdk::kVsVariantFlagMask)) ? 1 : 0;
}

bool ReadIb(uint32_t ib, IbView* v) {
  const uint8_t* o = ib ? ReadVirtual(ib, 4 * (xdk::kIbSizeDword + 1)) : nullptr;
  if (!o) return false;
  v->common = LoadBe32(o + 4 * xdk::kIbFormatDword);
  v->addr = xdk::GpuAddress(LoadBe32(o + 4 * xdk::kIbAddressDword));
  v->size = LoadBe32(o + 4 * xdk::kIbSizeDword);
  v->index32 = (v->common & xdk::kIbFormatMask) != 0;
  v->endian = (v->common >> xdk::kIbEndianShift) & 3;
  return true;
}

// Reads indices [start, start + count) as the GPU fetches them
// (stream_resolve.h). False if the range is empty, too large, outside the
// buffer or unreadable.
bool ScanIndices(const IbView& ib, uint32_t start, uint32_t count, IndexScan* out) {
  if (!IndexRangeFits(ib, start, count)) return false;
  const uint32_t isize = ib.index32 ? 4 : 2;
  const uint8_t* idx = ReadPhysical(ib.addr + start * isize, count * isize);
  if (!idx) return false;
  ScanIndexWords(idx, count, ib.index32, ib.endian, out);
  return true;
}

// The vertex shader a draw runs (xdk_layout.h, kVsSource / kVsFallbackSource):
// the device field first; with it null the GPU-owned GpuLoadShaders load. The
// GPU runs the patched immediate copy when the last load on this thread was
// one for this object; otherwise the object's own microcode.
struct VsChoice {
  uint32_t obj = 0;
  uint32_t flags = 0;
  uint32_t variant = 0;
  bool gpu_owned = false;
  bool immediate = false;
};

bool ChooseVs(const DeviceSnapshot& dev, VsChoice* c) {
  static_assert(xdk::kVsSource == xdk::VsSource::kDeviceField &&
                xdk::kVsFallbackSource == xdk::VsSource::kHookR4);
  c->gpu_owned = dev.vs_obj == 0 && t_gpu_vs.obj != 0;
  c->obj = c->gpu_owned ? t_gpu_vs.obj : dev.vs_obj;
  if (!c->obj || !ReadVirtualBe32(c->obj + xdk::kVsHeaderOffset, &c->flags)) return false;
  c->variant = c->gpu_owned ? t_gpu_vs.variant : ((c->flags & xdk::kVsVariantFlagMask) ? 1 : 0);
  c->immediate = !c->gpu_owned && t_vs_load.obj == c->obj;
  return true;
}

// The pixel shader a draw runs (xdk_layout.h, kPsDeviceFieldOffset). With the
// device's vertex shader field null the shader flush is skipped, so the GPU
// runs the last GpuLoadShaders r5 on this thread; otherwise the device field
// (a stale device field does not apply then: frame-map section 8). A null
// pixel shader means depth-only (RB_MODECONTROL 5): false, no shader.
bool ChoosePs(const DeviceSnapshot& dev, uint32_t* obj) {
  *obj = dev.vs_obj ? dev.ps_obj : t_gpu_ps;
  return *obj != 0;
}

// Per pixel-shader object: its microcode hash and the texture fetch slots it
// samples, kept until the record's microcode address or size changes.
struct PsInfo {
  uint64_t hash = 0;
  std::vector<uint32_t> tex_slots;     // TextureFetchSlots (vfetch_decode.h)
  const AlbedoSpec* albedo = nullptr;  // FindAlbedo; slot -1 = samples no albedo
  uint32_t phys = 0;
  uint32_t bytes = 0;
};
thread_local std::unordered_map<uint32_t, PsInfo> t_ps_cache;

const PsInfo* LookupPs(uint32_t obj) {
  if (!obj) return nullptr;
  if (t_ps_cache.size() > 4096) t_ps_cache.clear();
  UcodeRef rec;
  if (!ReadUcodeRecord(kPsLayout, obj, 0, &rec)) {
    t_ps_cache.erase(obj);
    return nullptr;
  }
  PsInfo& e = t_ps_cache[obj];
  if (e.hash && e.phys == rec.phys && e.bytes == rec.bytes) return &e;
  const UcodeRef u = ReadPsUcode(obj);
  if (!u.ok) {
    t_ps_cache.erase(obj);
    return nullptr;
  }
  e = PsInfo{};
  e.hash = u.hash;
  e.tex_slots = TextureFetchSlots(u.dwords.data(), u.dwords.size());
  e.albedo = FindAlbedo(u.hash);
  e.phys = u.phys;
  e.bytes = u.bytes;
  return &e;
}

// The vertex buffer feeding a fetch slot (stream_resolve.h): nullptr, or the
// reason the stream cannot be resolved.
const char* ResolveStream(const DeviceSnapshot& dev, uint32_t fetch_slot, StreamView* v) {
  if (!StreamForFetchSlot(fetch_slot, &v->stream)) return "slot_not_a_stream";
  v->obj = dev.stream_obj[v->stream];
  const uint8_t* vbo = v->obj ? ReadVirtual(v->obj, 4 * (xdk::kVbFetchDword + 2)) : nullptr;
  if (!vbo) return "no_vb";
  StreamFromFetch(LoadBe32(vbo + 4 * xdk::kVbFetchDword), LoadBe32(vbo + 4 * xdk::kVbFetchDword + 4),
                  dev.stream_fc[v->stream][0], dev.stream_fc[v->stream][1], v);
  return nullptr;
}

// Discovery evidence for the pixel shader (frame-map section 8, "Pixel shader
// microcode"): the chosen object and both candidates (device field, last
// GpuLoadShaders r5), the record, the texture fetch slots and "ps_hash"
// (0 when unknown or depth-only), which must name a shader_<HASH>.ucode.frag
// dump.
void AppendPs(std::string& row, const DeviceSnapshot& dev) {
  char buf[256];
  uint32_t obj = 0;
  const PsInfo* ps = ChoosePs(dev, &obj) ? LookupPs(obj) : nullptr;
  std::snprintf(buf, sizeof(buf),
                ",\"ps\":{\"obj\":\"0x%08X\",\"source\":\"%s\",\"dev_obj\":\"0x%08X\","
                "\"gpu_obj\":\"0x%08X\",\"phys\":\"0x%08X\",\"bytes\":%u,\"tex_slots\":[",
                obj, dev.vs_obj ? "device" : "gpu_load", dev.ps_obj, t_gpu_ps, ps ? ps->phys : 0,
                ps ? ps->bytes : 0);
  row += buf;
  if (ps) {
    for (size_t i = 0; i < ps->tex_slots.size(); ++i) {
      std::snprintf(buf, sizeof(buf), "%s%u", i ? "," : "", ps->tex_slots[i]);
      row += buf;
    }
  }
  row += "]";
  // The candidate not chosen, when set and different: evidence for the choice
  // (checked offline against the emulator's (vertex, pixel) pipeline pairs).
  const uint32_t other = dev.vs_obj ? t_gpu_ps : dev.ps_obj;
  if (other && other != obj) {
    const UcodeRef alt = ReadPsUcode(other);
    std::snprintf(buf, sizeof(buf), ",\"alt_hash\":\"0x%016llX\"",
                  static_cast<unsigned long long>(alt.ok ? alt.hash : 0));
    row += buf;
  }
  std::snprintf(buf, sizeof(buf), "},\"ps_hash\":\"0x%016llX\"",
                static_cast<unsigned long long>(ps ? ps->hash : 0));
  row += buf;
}

// Discovery only (caller holds g_mutex with g_file open). Dumps the raw guest
// bytes of a 2D fetch constant's base level once per (base address, identity)
// to logs/native_tex_<stamp>/<BASE>_<IDENTITY16>.bin and writes a "texture"
// row. Bounded: kMaxTextures per capture, kMaxTextureBytes each, kMaxUnsupportedBytes
// for formats the renderer does not decode (the census needs them). The extent
// is the tiled address upper bound / linear rows of the base level (row pitch
// by the SDK's base-level rule, render::BaseLevelLinear).
constexpr uint32_t kMaxTextures = 768;
constexpr uint32_t kMaxTextureBytes = 8u << 20;
constexpr uint32_t kMaxUnsupportedBytes = 64u << 10;

void DumpTexture(const uint32_t fc[6]) {
  if (g_tex_dir.empty() || g_tex_count >= kMaxTextures) return;
  render::TextureFetch t;
  const render::FetchError err = render::DecodeTextureFetch(fc, &t);
  uint64_t bytes = 0;
  uint32_t base = fc[1] & 0xFFFFF000u;
  if (err == render::FetchError::kNone) {
    const render::TexFormatInfo fi = render::FormatInfo(t.format);
    const uint32_t wb = (t.width + fi.block - 1) / fi.block;
    const uint32_t hb = (t.height + fi.block - 1) / fi.block;
    const uint32_t pitch_blocks = t.pitch_texels / fi.block;
    base = t.base_phys;
    bytes = t.tiled ? rex::graphics::texture_util::GetTiledAddressUpperBound2D(wb, hb, pitch_blocks,
                                                                               fi.bpb_log2)
                    : uint64_t(render::BaseLevelLinear(t).row_pitch_bytes) * (hb - 1) +
                          uint64_t(wb) * fi.bytes_per_block;
    bytes = std::min<uint64_t>(bytes, kMaxTextureBytes);
  } else if (err == render::FetchError::kFormat && base) {
    bytes = kMaxUnsupportedBytes;
  } else {
    return;
  }
  const uint64_t identity = render::TextureIdentity(fc);
  if (!g_tex_seen.insert({base, identity}).second || bytes == 0) return;
  const uint8_t* src = ReadPhysical(base, uint32_t(bytes));
  if (!src) return;
  char name[48];
  std::snprintf(name, sizeof(name), "%08X_%016llX.bin", base, static_cast<unsigned long long>(identity));
  std::error_code ec;
  std::filesystem::create_directories(g_tex_dir, ec);
  std::FILE* f = std::fopen((g_tex_dir / name).string().c_str(), "wb");
  if (!f) return;
  const bool ok = std::fwrite(src, 1, size_t(bytes), f) == size_t(bytes);
  std::fclose(f);
  if (!ok) return;
  ++g_tex_count;
  char buf[160];
  std::snprintf(buf, sizeof(buf), "{\"kind\":\"texture\",\"file\":\"%s/%s\",\"bytes\":%llu,\"fc\":",
                g_tex_dir.filename().string().c_str(), name, static_cast<unsigned long long>(bytes));
  std::string row = buf;
  row += HexDwords(fc, 6);
  row += "}\n";
  std::fputs(row.c_str(), g_file);
}

// Discovery evidence: the texture fetch constants (host-order dwords, read like
// the terrain heightmap fetch) of every slot the pixel shader samples, and a
// dump of each distinct 2D texture. Writes "tf":{"<slot>":[6 dwords] | null}.
void AppendTextureFetches(std::string& row, uint32_t device, const DeviceSnapshot& dev) {
  uint32_t obj = 0;
  std::vector<uint32_t> slots;
  if (ChoosePs(dev, &obj)) {
    if (const PsInfo* ps = LookupPs(obj)) slots = ps->tex_slots;  // copy: not held across calls
  }
  row += ",\"tf\":{";
  char buf[32];
  bool first = true;
  for (uint32_t slot : slots) {
    if (slot >= 32) continue;
    const uint32_t at =
        device + xdk::kDeviceVertexFetchOffset + xdk::kDeviceTextureFetchStride * slot;
    const uint8_t* p = device ? ReadVirtual(at, 24) : nullptr;
    uint32_t v[6] = {};
    if (p) {
      for (int i = 0; i < 6; ++i) v[i] = LoadBe32(p + 4 * i);
      DumpTexture(v);
    }
    std::snprintf(buf, sizeof(buf), "%s\"%u\":", first ? "" : ",", slot);
    row += buf;
    row += p ? HexDwords(v, 6) : "null";
    first = false;
  }
  row += "}";
}

// Discovery only (caller holds g_mutex with g_file open). Dumps `bytes` of a
// vertex stream at GPU physical `phys` to logs/native_geo_<stamp>/ and returns
// the file name, or "" if the bytes are unreadable or a cap is hit. A stream is
// written once per content: <PHYS8>_<SIZE8>.bin holds the first bytes seen at
// (phys, bytes); other contents of the same range (per-frame bone palettes,
// reused dynamic buffers) get <PHYS8>_<SIZE8>_<XXH3>.bin, so a row never names
// bytes other than the ones its draw read.
constexpr uint32_t kMaxGeoFiles = 4096;
constexpr uint32_t kMaxGeoStreamBytes = 1u << 20;
constexpr uint64_t kMaxGeoTotalBytes = 512ull << 20;
constexpr uint32_t kMaxGeoIndices = 512;

std::string DumpStream(uint32_t phys, uint32_t bytes) {
  if (g_geo_dir.empty() || bytes == 0) return {};
  const uint8_t* src = ReadPhysical(phys, bytes);
  if (!src) return {};
  const uint64_t hash = XXH3_64bits(src, bytes);
  std::vector<uint64_t>& seen = g_geo_seen[{phys, bytes}];
  const size_t at = size_t(std::find(seen.begin(), seen.end(), hash) - seen.begin());
  char name[64];
  if (at == 0) {
    std::snprintf(name, sizeof(name), "%08X_%08X.bin", phys, bytes);
  } else {
    std::snprintf(name, sizeof(name), "%08X_%08X_%016llX.bin", phys, bytes,
                  static_cast<unsigned long long>(hash));
  }
  if (at < seen.size()) return name;
  if (g_geo_count >= kMaxGeoFiles || g_geo_bytes + bytes > kMaxGeoTotalBytes) return {};
  std::error_code ec;
  std::filesystem::create_directories(g_geo_dir, ec);
  std::FILE* f = std::fopen((g_geo_dir / name).string().c_str(), "wb");
  if (!f) return {};
  const bool ok = std::fwrite(src, 1, bytes, f) == bytes;
  std::fclose(f);
  if (!ok) return {};
  seen.push_back(hash);
  ++g_geo_count;
  g_geo_bytes += bytes;
  return name;
}

// Discovery evidence for tools/xdk_sigmatch/position_check.py (in-scene draw
// rows; caller holds g_mutex): the shader's vertex fetches in
// DecodeVertexFetches order ("fetches"), a dump of every stream they read
// ("streams": bytes from the stream's offset, at most kMaxGeoStreamBytes;
// "total" is the stream's full extent when the dump is shorter; "file" is
// left out when a cap is hit), the draw's first indices after the index
// endian swap, base vertex not added ("idx", "base_vertex"; DrawVertices lists
// its own vertices), and vertex constants c0..c15 as host floats ("vconst").
// A reset index is written as -1 and counted in "idx_resets": it is not a
// vertex, but it cuts the strip or fan, which the checker's edge metric needs.
void AppendGeometry(std::string& row, bool indexed, const LastArgs& args, uint32_t device,
                    const DeviceSnapshot& dev, const std::vector<VertexFetch>& fetches) {
  char buf[256];
  row += ",\"fetches\":[";
  for (size_t i = 0; i < fetches.size(); ++i) {
    const VertexFetch& f = fetches[i];
    std::snprintf(buf, sizeof(buf),
                  "%s{\"i\":%zu,\"instr\":%u,\"slot\":%u,\"reg\":%u,\"fmt\":%u,\"off\":%d,\"stride\":%u,"
                  "\"mini\":%s,\"signed\":%s,\"norm\":%s,\"exp\":%d,\"swz\":%u}",
                  i ? "," : "", i, f.instr_index, f.fetch_slot, f.dst_reg, f.format, f.offset_dwords,
                  f.stride_dwords, f.mini ? "true" : "false", f.is_signed ? "true" : "false",
                  f.normalized ? "true" : "false", f.exp_adjust, f.dst_swizzle);
    row += buf;
  }
  row += "],\"streams\":[";
  uint32_t slots[16];  // distinct fetch slots, in fetch order
  uint32_t n_slots = 0;
  for (const VertexFetch& f : fetches) {
    if (n_slots == 16 || std::find(slots, slots + n_slots, f.fetch_slot) != slots + n_slots) continue;
    slots[n_slots++] = f.fetch_slot;
    StreamView sv;
    if (const char* error = ResolveStream(dev, f.fetch_slot, &sv)) {
      std::snprintf(buf, sizeof(buf), "%s{\"slot\":%u,\"error\":\"%s\"}", n_slots > 1 ? "," : "",
                    f.fetch_slot, error);
      row += buf;
      continue;
    }
    const uint32_t total = sv.size - std::min(sv.offset, sv.size);
    const uint32_t bytes = std::min(total, kMaxGeoStreamBytes);
    const uint32_t phys = sv.base + sv.offset;
    std::snprintf(buf, sizeof(buf),
                  "%s{\"slot\":%u,\"phys\":%u,\"size\":%u,\"stride\":%u,\"endian\":%u",
                  n_slots > 1 ? "," : "", f.fetch_slot, phys, bytes,
                  dev.stream_stride_dw[sv.stream] * 4, sv.fc1 & 3);
    row += buf;
    if (bytes < total) {
      std::snprintf(buf, sizeof(buf), ",\"total\":%u", total);
      row += buf;
    }
    const std::string file = DumpStream(phys, bytes);
    if (!file.empty()) {
      row += ",\"file\":\"";
      row += g_geo_dir.filename().string();
      row += "/";
      row += file;
      row += "\"";
    }
    row += "}";
  }
  // DrawIndexedVertices: r5 base vertex, r6 start index, r7 index count.
  // DrawVertices: r5 start vertex, r6 vertex count.
  row += "],\"idx\":[";
  bool first = true;
  uint32_t resets = 0;
  auto add_index = [&](int64_t v) {
    std::snprintf(buf, sizeof(buf), "%s%lld", first ? "" : ",", static_cast<long long>(v));
    row += buf;
    first = false;
  };
  if (indexed) {
    IbView ib;
    const bool have_ib = ReadIb(dev.ib_obj, &ib);
    const uint32_t n = std::min(args.r[4], kMaxGeoIndices);
    const uint32_t isize = ib.index32 ? 4 : 2;
    const uint8_t* idx = have_ib && IndexRangeFits(ib, args.r[3], n)
                             ? ReadPhysical(ib.addr + args.r[3] * isize, n * isize)
                             : nullptr;
    for (uint32_t i = 0; idx && i < n; ++i) {
      IndexScan one;  // one word through the scan the records use
      ScanIndexWords(idx + isize * i, 1, ib.index32, ib.endian, &one);
      add_index(one.n_first ? int64_t(one.first[0]) : -1);
      resets += one.restarts;
    }
  } else {
    const uint32_t n = std::min(args.r[3], kMaxGeoIndices);
    for (uint32_t i = 0; i < n; ++i) add_index(args.r[2] + i);
  }
  std::snprintf(buf, sizeof(buf), "],\"idx_resets\":%u,\"base_vertex\":%d", resets,
                indexed ? int32_t(args.r[2]) : 0);
  row += buf;
  const uint32_t bank_ptr =
      g_state.vs_bank_ptr ? g_state.vs_bank_ptr : device + xdk::kDeviceVsConstantsOffset;
  if (const uint8_t* bank = ReadVirtual(bank_ptr, 16 * 16)) {
    row += ",\"vconst\":[";
    for (uint32_t i = 0; i < 64; ++i) {
      const uint32_t bits = LoadBe32(bank + 4 * i);
      float f;
      std::memcpy(&f, &bits, 4);
      if (i) row += ",";
      AppendFloat(row, f);
    }
    row += "]";
  }
}

// Caller holds g_mutex. One decoded "draw" row (frame-map section 8) for
// DrawVertices / DrawIndexedVertices; other draw hooks have no VB objects.
// `in_scene`: the draw is inside the main-scene bracket.
void WriteDrawRow(uint32_t id, const LastArgs& args, uint32_t device, const DeviceSnapshot& dev,
                  bool in_scene) {
  char buf[512];
  std::string row;
  std::snprintf(buf, sizeof(buf),
                "{\"kind\":\"draw\",\"frame\":%d,\"func\":\"%s\",\"args\":[\"0x%08X\",\"0x%08X\","
                "\"0x%08X\",\"0x%08X\"],\"device\":\"0x%08X\",\"viewport\":[1120,720],"
                "\"in_scene\":%s",
                g_frames_written.load(std::memory_order_relaxed) + 1, HookName(id), args.r[1],
                args.r[2], args.r[3], args.r[4], device, in_scene ? "true" : "false");
  row += buf;
  auto finish = [&](const char* error) {
    if (error) {
      row += ",\"error\":\"";
      row += error;
      row += "\"";
    }
    row += "}\n";
    std::fputs(row.c_str(), g_file);
  };

  // Index buffer (DrawIndexedVertices: r4 prim, r5 base vertex, r6 start, r7 count).
  // Written first: it does not depend on the vertex shader being readable.
  const bool indexed = id == kHook_D3DDevice_DrawIndexedVertices;
  IndexScan scan;  // max_index -1 if unknown
  bool scanned = false;
  if (indexed) {
    IbView ib;
    if (ReadIb(dev.ib_obj, &ib)) {
      const uint32_t isize = ib.index32 ? 4 : 2;
      const uint64_t end = (uint64_t(args.r[3]) + args.r[4]) * isize;
      // Largest index the draw reads, evidence for the address and format
      // fields (checked offline against the vb row's vertex count).
      scanned = ScanIndices(ib, args.r[3], args.r[4], &scan);
      std::snprintf(buf, sizeof(buf),
                    ",\"ib\":{\"obj\":\"0x%08X\",\"common\":\"0x%08X\",\"phys_addr\":%u,\"size\":%u,"
                    "\"index32\":%s,\"endian\":%u,\"base_vertex\":%u,\"start\":%u,\"count\":%u,"
                    "\"max_index\":%lld,\"restarts\":%u,\"fits\":%s,\"aligned\":%s}",
                    dev.ib_obj, ib.common, ib.addr, ib.size, ib.index32 ? "true" : "false",
                    ib.endian, args.r[2], args.r[3], args.r[4],
                    static_cast<long long>(scan.max_index), scan.restarts,
                    end <= ib.size ? "true" : "false",
                    (ib.addr % 4 == 0 && ib.addr < 0x20000000u) ? "true" : "false");
      row += buf;
    } else {
      row += ",\"ib\":null";
    }
  }

  // Pixel shader: also independent of the vertex shader.
  AppendPs(row, dev);
  AppendTextureFetches(row, device, dev);

  // Vertex shader and its microcode.
  VsChoice vc;
  if (!ChooseVs(dev, &vc)) return finish("no_vs");
  const uint32_t vs = vc.obj, variant = vc.variant;
  const UcodeRef cand[2] = {ReadVsUcode(vs, 0), ReadVsUcode(vs, 1)};
  std::snprintf(buf, sizeof(buf), ",\"vs\":{\"obj\":\"0x%08X\",\"flags\":\"0x%08X\",\"variant\":%u,\"candidates\":[",
                vs, vc.flags, variant);
  row += buf;
  for (int v = 0; v < 2; ++v) {
    std::snprintf(buf, sizeof(buf),
                  "%s{\"record\":\"0x%08X\",\"phys\":\"0x%08X\",\"bytes\":%u,\"hash\":\"0x%016llX\"}",
                  v ? "," : "", cand[v].record, cand[v].phys, cand[v].bytes,
                  static_cast<unsigned long long>(cand[v].hash));
    row += buf;
  }
  const bool immediate = vc.immediate;
  const std::vector<uint32_t>* copy = immediate ? VsLoadDwords() : nullptr;
  std::snprintf(buf, sizeof(buf), "],\"source\":\"%s\",\"copy\":\"0x%08X\"",
                immediate ? "immediate" : (vc.gpu_owned ? "gpu_load" : "object"),
                copy ? t_vs_load.copy : 0);
  row += buf;
  // Evidence for the template location: the copy differs from the object's
  // microcode only in the patched vertex fetch dwords.
  if (copy && cand[variant].ok && cand[variant].dwords.size() == copy->size()) {
    row += ",\"template_diff\":[";
    int shown = 0;
    for (size_t i = 0; i < copy->size(); ++i) {
      if ((*copy)[i] == cand[variant].dwords[i]) continue;
      std::snprintf(buf, sizeof(buf), "%s%zu", shown ? "," : "", i);
      row += buf;
      if (++shown == 32) break;
    }
    row += "]";
  }
  row += "}";
  if (immediate ? !copy : !cand[variant].ok) return finish("no_ucode");
  const uint64_t hash = immediate ? t_vs_load.hash : cand[variant].hash;
  const std::vector<uint32_t>& ucode = immediate ? *copy : cand[variant].dwords;
  std::snprintf(buf, sizeof(buf), ",\"vs_hash\":\"0x%016llX\",\"vs_dwords\":%zu",
                static_cast<unsigned long long>(hash), ucode.size());
  row += buf;

  // Position element.
  PosLayout pos;
  std::vector<VertexFetch> fetches = DecodeVertexFetches(ucode.data(), ucode.size());
  if (in_scene) AppendGeometry(row, indexed, args, device, dev, fetches);
  const VertexFetch* pf = nullptr;
  for (const auto& f : fetches) {
    if (!f.mini) { pf = &f; break; }
  }
  if (!SelectPosition(fetches, -1, &pos) || !pf) return finish("no_position");

  // Vertex buffer.
  StreamView sv;
  if (const char* error = ResolveStream(dev, pos.fetch_slot, &sv)) return finish(error);
  const bool endian_ok = ApplyFetchEndian(&pos, sv.fc1 & 3);
  std::snprintf(buf, sizeof(buf),
                ",\"pos\":{\"fetch_slot\":%u,\"offset_bytes\":%u,\"stride_bytes\":%u,\"format\":%u,"
                "\"signed\":%d,\"normalized\":%d,\"exp_adjust\":%d,\"swizzle\":\"0x%03X\","
                "\"swap16\":%s,\"endian_ok\":%s}",
                pos.fetch_slot, pos.offset_bytes, pos.stride_bytes, pf->format, pos.is_signed ? 1 : 0,
                pos.normalized ? 1 : 0, pos.exp_adjust, pos.swizzle, pos.swap16 ? "true" : "false",
                endian_ok ? "true" : "false");
  row += buf;
  const uint32_t vertices = sv.offset < sv.size ? (sv.size - sv.offset) / pos.stride_bytes : 0;
  std::snprintf(buf, sizeof(buf),
                ",\"vb\":{\"stream\":%u,\"obj\":\"0x%08X\",\"phys_addr\":%u,\"size\":%u,\"offset\":%u,"
                "\"fc\":[\"0x%08X\",\"0x%08X\"],\"fc_match\":%s,\"stride_dw\":%u,\"vertices\":%u}",
                sv.stream, sv.obj, sv.base, sv.size, sv.offset, sv.fc0, sv.fc1,
                sv.fc_match ? "true" : "false", dev.stream_stride_dw[sv.stream], vertices);
  row += buf;
  // The draw reads past the stream the position element was taken from, so
  // that stream is not the per-vertex one (e.g. per-instance data fetched
  // first): flag the row and leave its positions out. DrawVertices
  // (0x8221C518): r4 prim, r5 start vertex, r6 count.
  const bool pos_suspect =
      indexed ? (scan.max_index >= 0 && scan.max_index + int64_t(args.r[2]) >= int64_t(vertices))
              : uint64_t(args.r[2]) + args.r[3] > vertices;
  if (pos_suspect) row += ",\"pos_suspect\":true";

  // Vertex constant bank (host floats).
  const uint32_t bank_ptr = g_state.vs_bank_ptr ? g_state.vs_bank_ptr
                                                : device + xdk::kDeviceVsConstantsOffset;
  if (const uint8_t* bank = ReadVirtual(bank_ptr, 256 * 16)) {
    row += ",\"bank\":[";
    for (uint32_t i = 0; i < 1024; ++i) {
      const uint32_t bits = LoadBe32(bank + 4 * i);
      float f;
      std::memcpy(&f, &bits, 4);
      if (i) row += ",";
      AppendFloat(row, f);
    }
    row += "]";
  }

  // The draw's own first vertices (up to 64): indexed draws reach them through
  // their first indices plus the base vertex, DrawVertices from its start
  // vertex. Positions are what the fetch hands the shader (swizzle and endian
  // applied), so a matrix found from them applies to the record's layout.
  const uint32_t avail = sv.size - std::min(sv.offset, sv.size);
  const uint8_t* src =
      endian_ok && vertices && !pos_suspect ? ReadPhysical(sv.base + sv.offset, avail) : nullptr;
  Float4 verts[64];
  uint32_t count = 0;
  if (src && indexed && scanned) {
    for (uint32_t i = 0; i < scan.n_first; ++i) {
      const int64_t v = int64_t(scan.first[i]) + int32_t(args.r[2]);
      if (v < 0 || v >= int64_t(vertices) ||
          !DecodePositions(src, avail, pos, uint32_t(v), 1, &verts[count])) {
        continue;
      }
      ++count;
    }
  } else if (src && !indexed) {
    const uint32_t n = std::min<uint32_t>(64, args.r[3]);
    if (DecodePositions(src, avail, pos, args.r[2], n, verts)) count = n;
  }
  if (count) {
    row += ",\"positions\":[";
    for (uint32_t i = 0; i < count; ++i) {
      row += i ? ",[" : "[";
      AppendFloat(row, verts[i].x);
      row += ",";
      AppendFloat(row, verts[i].y);
      row += ",";
      AppendFloat(row, verts[i].z);
      row += ",";
      AppendFloat(row, verts[i].w);
      row += "]";
    }
    row += "]";
  }
  finish(nullptr);
}

// Caller holds g_mutex. One "tess" row (frame-map section 9) for the
// tessellated-patch builders DrawIndx:8221C9C8 (r5 first patch, r6 patch
// count, auto-indexed quad patches) and DrawIndx:82207C30 (adaptive: r7
// patches, r7 * 4 tessellation factors from the bound index buffer at r6): the
// vertex shader, the constants the terrain shaders read and texture fetch
// constants 16-19 from the device shadow.
void WriteTessRow(uint32_t id, const LastArgs& args, uint32_t device, const DeviceSnapshot& dev,
                  bool in_scene) {
  char buf[512];
  std::string row;
  std::snprintf(buf, sizeof(buf),
                "{\"kind\":\"tess\",\"frame\":%d,\"func\":\"%s\",\"args\":[\"0x%08X\",\"0x%08X\","
                "\"0x%08X\",\"0x%08X\"],\"device\":\"0x%08X\",\"in_scene\":%s",
                g_frames_written.load(std::memory_order_relaxed) + 1, HookName(id), args.r[1],
                args.r[2], args.r[3], args.r[4], device, in_scene ? "true" : "false");
  row += buf;
  VsChoice vc;
  if (ChooseVs(dev, &vc)) {
    const UcodeRef u = vc.immediate ? UcodeRef{} : ReadVsUcode(vc.obj, vc.variant);
    const uint64_t hash = vc.immediate ? (t_vs_load.usable ? t_vs_load.hash : 0) : u.hash;
    std::snprintf(buf, sizeof(buf),
                  ",\"vs\":{\"obj\":\"0x%08X\",\"source\":\"%s\",\"hash\":\"0x%016llX\"}", vc.obj,
                  vc.immediate ? "immediate" : (vc.gpu_owned ? "gpu_load" : "object"),
                  static_cast<unsigned long long>(hash));
    row += buf;
  }
  AppendPs(row, dev);
  const uint32_t bank_ptr =
      g_state.vs_bank_ptr ? g_state.vs_bank_ptr : device + xdk::kDeviceVsConstantsOffset;
  row += ",\"consts\":{";
  bool first = true;
  for (uint32_t c : {0u, 1u, 2u, 3u, 8u, 11u, 46u, 47u, 72u, 113u, 114u, 115u, 252u, 253u, 254u,
                     255u}) {
    const uint8_t* p = ReadVirtual(bank_ptr + 16 * c, 16);
    if (!p) continue;
    std::snprintf(buf, sizeof(buf), "%s\"c%u\":[", first ? "" : ",", c);
    row += buf;
    for (int i = 0; i < 4; ++i) {
      const uint32_t bits = LoadBe32(p + 4 * i);
      float f;
      std::memcpy(&f, &bits, 4);
      if (i) row += ",";
      AppendFloat(row, f);
    }
    row += "]";
    first = false;
  }
  row += "},\"tf\":{";
  for (uint32_t t = 16; t < 20; ++t) {
    const uint32_t at =
        device + xdk::kDeviceVertexFetchOffset + xdk::kDeviceTextureFetchStride * t;
    const uint8_t* p = device ? ReadVirtual(at, 24) : nullptr;
    uint32_t v[6] = {};
    if (p) {
      for (int i = 0; i < 6; ++i) v[i] = LoadBe32(p + 4 * i);
    }
    std::snprintf(buf, sizeof(buf), "%s\"%u\":", t == 16 ? "" : ",", t);
    row += buf;
    row += p ? HexDwords(v, 6) : "null";
  }
  row += "}}\n";
  std::fputs(row.c_str(), g_file);
}

// --- Draw records (native renderer) ------------------------------------------

// Per vertex-shader object: what the record needs from its microcode, kept
// until the microcode the GPU runs changes (object path: microcode address,
// size or patched-declaration id; immediate path: the copy's hash), so
// steady-state draws do not re-hash microcode.
struct VsInfo {
  bool immediate = false;
  uint32_t variant = 0;
  uint32_t phys = 0;
  uint32_t bytes = 0;
  uint32_t patch_id = 0;  // object dword 10 (xdk_layout.h: patched-declaration id)
  uint64_t hash = 0;
  const TransformInfo* transform = nullptr;
  bool have_pos = false;
  PosLayout pos;
  // vs-transforms.json "skin": the layout (endians unset) and the bone
  // stream's fetch slot; skin_required with !have_skin = layout not handled.
  bool skin_required = false;
  bool have_skin = false;
  BoneSkin skin;
  uint32_t skin_slot = 0;
  // vs-transforms.json "instance" (the table entry is stable): the row
  // layouts and bias (endians, stream and constants unset) and the instance
  // stream's fetch slot; `pos` is then the mesh position (the entry's
  // mesh_fetch is the transform's pos_fetch). instance with !have_instance =
  // the entry does not match the decoded fetches.
  const InstanceSpec* instance = nullptr;
  bool have_instance = false;
  InstanceSet instance_rows;
  uint32_t instance_slot = 0;
  const TerrainSpec* terrain = nullptr;  // vs-transforms.json "terrain"
  // vs-transforms.json "uv" per interpolator component (FindVsUv), and the
  // decoded vertex fetches its fetch_index refers to (ResolveUvFetch).
  const VsUvSpec* uv[16][4] = {};
  std::vector<VertexFetch> fetches;
};
thread_local std::unordered_map<uint32_t, VsInfo> t_vs_cache;

void FillShader(VsInfo& e, const uint32_t* ucode, size_t dwords, uint64_t hash) {
  e.hash = hash;
  e.transform = FindTransform(hash);
  e.fetches = DecodeVertexFetches(ucode, dwords);
  const std::vector<VertexFetch>& fetches = e.fetches;
  e.have_pos = SelectPosition(fetches, e.transform ? e.transform->pos_fetch : -1, &e.pos,
                              e.transform ? e.transform->pos_swizzle : 0);
  if (const SkinSpec* spec = FindSkin(hash)) {
    e.skin_required = true;
    e.have_skin = e.have_pos && SelectSkin(fetches, *spec, e.pos, &e.skin, &e.skin_slot);
  }
  e.instance = FindInstance(hash);
  if (e.instance) {
    e.have_instance = e.have_pos && SelectInstanceRows(fetches, *e.instance, e.pos.fetch_slot,
                                                       &e.instance_rows, &e.instance_slot);
  }
  e.terrain = FindTerrain(hash);
  for (uint8_t i = 0; i < 16; ++i) {
    for (uint8_t c = 0; c < 4; ++c) e.uv[i][c] = FindVsUv(hash, i, c);
  }
}

const VsInfo* LookupVs(const VsChoice& c) {
  if (t_vs_cache.size() > 4096) t_vs_cache.clear();
  VsInfo& e = t_vs_cache[c.obj];
  if (c.immediate) {
    if (!t_vs_load.usable) return nullptr;  // loaded while no consumer: unknown
    if (e.immediate && e.hash && e.hash == t_vs_load.hash) return &e;
    const std::vector<uint32_t>* ucode = VsLoadDwords();
    if (!ucode) {
      t_vs_cache.erase(c.obj);
      return nullptr;
    }
    e = VsInfo{};
    e.immediate = true;
    FillShader(e, ucode->data(), ucode->size(), t_vs_load.hash);
    return &e;
  }
  // The variant's record (xdk_layout.h), read without touching the microcode.
  uint32_t base = 0, rec_off = 0, offset = 0, size = 0, patch_id = 0;
  const uint32_t header = c.obj + xdk::kVsHeaderOffset;
  if (!ReadVirtualBe32(c.obj + 4 * xdk::kVsUcodeBaseDword, &base) ||
      !ReadVirtualBe32(c.obj + 4 * 10, &patch_id) ||
      !ReadVirtualBe32(header + xdk::kVsRecordOffsetField + 8 * c.variant, &rec_off) ||
      !ReadVirtualBe32(header + rec_off + 4 * xdk::kVsUcodeAddressDword, &offset) ||
      !ReadVirtualBe32(header + rec_off + 4 * xdk::kVsUcodeSizeDword, &size)) {
    t_vs_cache.erase(c.obj);
    return nullptr;
  }
  const uint32_t phys = xdk::GpuAddress(base + offset);
  const uint32_t bytes = size << xdk::kVsUcodeSizeShift;
  if (!e.immediate && e.hash && e.variant == c.variant && e.phys == phys && e.bytes == bytes &&
      e.patch_id == patch_id) {
    return &e;
  }
  const UcodeRef u = ReadVsUcode(c.obj, c.variant);
  if (!u.ok) {
    t_vs_cache.erase(c.obj);
    return nullptr;
  }
  e = VsInfo{};
  e.variant = c.variant;
  e.phys = phys;
  e.bytes = bytes;
  e.patch_id = patch_id;
  FillShader(e, u.dwords.data(), u.dwords.size(), u.hash);
  return &e;
}

// Vertex / pixel constants, host floats; only the registers a draw needs are filled.
thread_local float t_bank[1024];
thread_local float t_ps_bank[1024];

// Converts constants [reg, reg + count) of a 256 x float4 bank at `bank` into `out`.
bool ReadConstantRegisters(uint32_t bank, uint32_t reg, uint32_t count, float* out) {
  if (reg + count > 256) return false;
  const uint8_t* p = ReadVirtual(bank + 16 * reg, 16 * count);
  if (!p) return false;
  for (uint32_t i = 0; i < 4 * count; ++i) {
    const uint32_t bits = LoadBe32(p + 4 * i);
    std::memcpy(&out[4 * reg + i], &bits, 4);
  }
  return true;
}

// Converts vertex constants [reg, reg + count) of the device's bank into t_bank.
bool ReadBankRegisters(uint32_t device, uint32_t reg, uint32_t count) {
  const uint32_t bank_ptr = g_vs_bank_ptr.load(std::memory_order_relaxed);
  return ReadConstantRegisters(bank_ptr ? bank_ptr : device + xdk::kDeviceVsConstantsOffset, reg,
                               count, t_bank);
}

// Converts pixel constants [reg, reg + count) of the device's bank into t_ps_bank.
bool ReadPsBankRegisters(uint32_t device, uint32_t reg, uint32_t count) {
  const uint32_t bank_ptr = g_ps_bank_ptr.load(std::memory_order_relaxed);
  return ReadConstantRegisters(bank_ptr ? bank_ptr : device + xdk::kDevicePsConstantsOffset, reg,
                               count, t_ps_bank);
}

// The device snapshot and vertex shader FillDrawInputs used, for FillMaterial
// (the device is read once per draw). `vs` points into t_vs_cache: valid
// until the next LookupVs.
struct DrawShaders {
  bool have_dev = false;
  DeviceSnapshot dev;
  const VsInfo* vs = nullptr;
};

// Builds the instance set of an indexed draw of an instancing shader
// (vs-transforms.json "instance", instance_expand.h): the instance stream
// behind the rows' fetch slot, the constants the entry names (through t_bank)
// and the range check on the draw's largest index (`scan`); `vertices` is
// what the mesh stream holds. in.instances becomes active only when all of it
// holds; the entry's distance cut constants (eye, squared distance) go to
// in.cut as read (AssembleRecord sanitizes them). Returns why not:
// instance-unsupported when the instance stream does
// not resolve or a constant reference is not a vertex constant (kNone when
// the rows do not match the shader's fetches: AssembleRecord gives the same
// reason for any unbuilt set), bad-index for garbage constants or indices,
// bad-memory for unreadable constants or a flat stream over the draw-count
// cap.
SkipReason FillInstanceSet(uint32_t device, const DeviceSnapshot& dev, const VsInfo& vs,
                           const IndexScan& scan, uint32_t vertices, DrawInputs& in) {
  if (!vs.have_instance) return SkipReason::kNone;
  const InstanceSpec& spec = *vs.instance;
  InstanceSet set = vs.instance_rows;
  StreamView rows;
  if (ResolveStream(dev, vs.instance_slot, &rows) || !rows.fc_match) {
    return SkipReason::kInstanceUnsupported;
  }
  for (PosLayout& row : set.rows) {
    if (!ApplyFetchEndian(&row, rows.fc1 & 3)) return SkipReason::kInstanceUnsupported;
  }
  set.rows_addr = rows.base + rows.offset;
  set.rows_size = rows.size - rows.offset;
  // Constant references are register * 4 + component; the offset's y and z
  // follow its x. Only the registers named are converted.
  if (spec.offset_ref < 0 || spec.offset_ref > 1021) return SkipReason::kInstanceUnsupported;
  const struct {
    int32_t ref;
    float* out;
  } consts[] = {{spec.inv_count_ref, &set.inv_count}, {spec.count_ref, &set.count},
                {spec.first_ref, &set.first},         {spec.offset_ref, &set.offset[0]},
                {spec.offset_ref + 1, &set.offset[1]}, {spec.offset_ref + 2, &set.offset[2]}};
  uint32_t have_reg = 256;  // the register t_bank was last filled for
  for (const auto& c : consts) {
    if (c.ref < 0 || c.ref >= 1024) return SkipReason::kInstanceUnsupported;
    const uint32_t reg = uint32_t(c.ref) / 4;
    if (reg != have_reg && !ReadBankRegisters(device, reg, 1)) return SkipReason::kBadMemory;
    have_reg = reg;
    *c.out = t_bank[c.ref];
  }
  // Sets flat_count; the draw-count cap on the flat stream is enforced here.
  if (const SkipReason why = InstanceRangeSkip(&set, scan.max_index, in.base_vertex, vertices);
      why != SkipReason::kNone) {
    return why;
  }
  // The distance cut ("cut": both references or neither): the eye's x
  // reference (y and z follow) and the squared distance.
  if (spec.cut_eye_ref >= 0 || spec.cut_dist2_ref >= 0) {
    if (spec.cut_eye_ref < 0 || spec.cut_eye_ref > 1021 || spec.cut_dist2_ref < 0 ||
        spec.cut_dist2_ref >= 1024) {
      return SkipReason::kInstanceUnsupported;
    }
    const uint32_t eye_reg = uint32_t(spec.cut_eye_ref) / 4;
    const uint32_t last_reg = uint32_t(spec.cut_eye_ref + 2) / 4;
    if (!ReadBankRegisters(device, eye_reg, last_reg - eye_reg + 1)) return SkipReason::kBadMemory;
    for (int k = 0; k < 3; ++k) in.cut[k] = t_bank[spec.cut_eye_ref + k];
    if (!ReadBankRegisters(device, uint32_t(spec.cut_dist2_ref) / 4, 1)) return SkipReason::kBadMemory;
    in.cut[3] = t_bank[spec.cut_dist2_ref];
  }
  in.instances = set;
  return SkipReason::kNone;
}

// Fills `in` for DrawIndexedVertices / DrawVertices (prim and counts already
// set). Returns a skip reason AssembleRecord does not check (garbage counts,
// indices outside the position stream), or kNone.
SkipReason FillDrawInputs(uint32_t device, DrawInputs& in, DrawShaders* shaders) {
  if (const SkipReason c = CountSkip(in.count); c != SkipReason::kNone) return c;
  DeviceSnapshot& dev = shaders->dev;
  shaders->have_dev = ReadDevice(device, &dev);
  if (!shaders->have_dev) return SkipReason::kNone;  // have_shader stays false
  VsChoice vc;
  const VsInfo* vs = ChooseVs(dev, &vc) ? LookupVs(vc) : nullptr;
  shaders->vs = vs;
  if (!vs) return SkipReason::kNone;
  in.have_shader = true;
  in.vs_hash = vs->hash;
  in.transform = vs->transform;
  // An instancing shader: `pos` is the mesh position; the draw is recorded
  // only with its instance set (FillInstanceSet). Every return below that
  // leaves the set unbuilt (mesh position not selectable, mesh stream or
  // index buffer not resolved, an endian the decoder cannot read) is
  // instance-unsupported in AssembleRecord, not the plain draw's reason.
  in.instance_shader = vs->instance != nullptr;
  in.skin_shader = vs->skin_required;
  if (!vs->have_pos) return SkipReason::kNone;
  in.pos = vs->pos;
  in.have_pos = true;
  StreamView sv;  // no stream: kNoStream
  if (ResolveStream(dev, in.pos.fetch_slot, &sv) || !sv.fc_match) return SkipReason::kNone;
  // An endian the decoder cannot read: kUnknownPosFormat.
  in.have_pos = ApplyFetchEndian(&in.pos, sv.fc1 & 3);
  if (!in.have_pos) return SkipReason::kNone;
  in.have_vb = true;
  in.vb = {sv.base + sv.offset, sv.size - sv.offset};
  const uint32_t vertices = in.vb.size / in.pos.stride_bytes;
  SkipReason extra = SkipReason::kNone;
  if (in.indexed) {
    IbView ib;
    if (!ReadIb(dev.ib_obj, &ib)) return SkipReason::kNone;
    in.have_ib = true;
    in.ib = {ib.addr, ib.size};
    in.index32 = ib.index32;
    // Records carry big-endian indices (index_convert.h): 16-bit words with
    // 8in16 or 32-bit words with 8in32 (every sampled buffer is 8in16).
    if (ib.endian != (ib.index32 ? 2u : 1u)) return SkipReason::kBadIndex;
    // Indices that reach past the position stream (pos_suspect: instanced
    // draws whose first fetch is per-instance data) or are unreadable. An
    // instancing shader's index names a copy and a mesh vertex instead: its
    // range check is the instance set's.
    IndexScan scan;
    if (!ScanIndices(ib, in.start, in.count, &scan)) {
      extra = SkipReason::kBadMemory;
    } else if (in.instance_shader) {
      extra = FillInstanceSet(device, dev, *vs, scan, vertices, in);
    } else if (scan.max_index >= 0 &&
               (scan.max_index + in.base_vertex >= int64_t(vertices) || in.base_vertex < 0)) {
      extra = SkipReason::kBadIndex;
    }
  } else if (in.instance_shader) {
    extra = SkipReason::kInstanceUnsupported;  // the index encodes the copy: no index, no instancing
  } else if (uint64_t(in.start) + in.count > vertices) {
    extra = SkipReason::kBadIndex;
  }
  // Rigid skin (vs-transforms.json "skin"): the bone palette is the stream
  // feeding the rows' fetch slot; endians come from the two fetch constants.
  if (vs->skin_required) {
    SkipReason why = SkipReason::kNone;
    StreamView bones;
    BoneSkin s = vs->skin;
    if (!vs->have_skin) {
      why = SkipReason::kSkinUnsupported;
    } else if (ResolveStream(dev, vs->skin_slot, &bones) || !bones.fc_match) {
      why = SkipReason::kSkinUnsupported;
    } else {
      s.index_endian = sv.fc1 & 3;
      bool ok = s.index_endian == 0 || s.index_endian == 2;
      for (PosLayout& row : s.rows) ok = ok && ApplyFetchEndian(&row, bones.fc1 & 3);
      s.palette_addr = bones.base + bones.offset;
      s.palette_size = bones.size - bones.offset;
      if (ok) {
        in.skin = s;
      } else {
        why = SkipReason::kSkinUnsupported;
      }
    }
    if (extra == SkipReason::kNone) extra = why;
  }
  // Bank: only the transform's four registers are converted.
  if (in.transform && in.transform->base_reg <= 252 &&
      !ReadBankRegisters(device, in.transform->base_reg, 4)) {
    extra = SkipReason::kBadMemory;
  } else if (in.transform && in.transform->base_reg <= 252) {
    in.bank = t_bank;
  }
  return extra;
}

// Fills `in` for a tessellated terrain draw (DrawIndx:8221C9C8, the adaptive
// DrawIndx:82207C30): patches [first, first + patches) of the bound vertex
// shader's terrain grid (terrain_patch.h). A shader without a terrain entry
// leaves the draw unsupported; an unhandled heightmap leaves `terrain`
// inactive (kUnknownPosFormat).
SkipReason FillTerrainInputs(uint32_t device, uint32_t first, uint32_t patches, DrawInputs& in) {
  if (const SkipReason c = CountSkip(patches); c != SkipReason::kNone) return c;
  DeviceSnapshot dev;
  if (!ReadDevice(device, &dev)) return SkipReason::kNone;
  VsChoice vc;
  const VsInfo* vs = ChooseVs(dev, &vc) ? LookupVs(vc) : nullptr;
  if (!vs) return SkipReason::kNone;
  in.have_shader = true;
  in.vs_hash = vs->hash;
  in.transform = vs->transform;
  if (!vs->terrain) return SkipReason::kNone;
  in.terrain_shader = true;
  const TerrainSpec& s = *vs->terrain;
  // The spec's registers (each a component index reg * 4 + c) and the
  // transform rows, into t_bank.
  for (uint32_t c : {uint32_t(s.grid), uint32_t(s.cell), uint32_t(s.height_scale),
                     uint32_t(s.origin), uint32_t(s.tex_offset), uint32_t(s.tex_scale)}) {
    if (c >= 1024 || !ReadBankRegisters(device, c / 4, 1)) return SkipReason::kBadMemory;
  }
  if (s.patch_offset >= 0 &&
      (s.patch_offset >= 1024 || !ReadBankRegisters(device, uint32_t(s.patch_offset) / 4, 1))) {
    return SkipReason::kBadMemory;
  }
  if (in.transform && in.transform->base_reg <= 252) {
    if (!ReadBankRegisters(device, in.transform->base_reg, 4)) return SkipReason::kBadMemory;
    in.bank = t_bank;
  }
  // The heightmap's texture fetch constant (device shadow, 6 dwords per
  // texture constant; xdk_layout.h kDeviceTextureFetchStride).
  const uint8_t* p =
      s.height_fetch < 32 ? ReadVirtual(device + xdk::kDeviceVertexFetchOffset +
                                            xdk::kDeviceTextureFetchStride * s.height_fetch,
                                        24)
                          : nullptr;
  if (!p) return SkipReason::kBadMemory;
  uint32_t fc[6];
  for (int i = 0; i < 6; ++i) fc[i] = LoadBe32(p + 4 * i);
  MakeTerrainPatch(s, t_bank, fc, first, patches, &in.terrain);
  return SkipReason::kNone;
}

// Albedo texture and UVs for a recorded draw (material.h). Never changes the
// draw's skip reason. Terrain draws need no lookups; a draw whose device was
// not read stays ps-unknown.
void FillMaterial(uint32_t device, const DrawShaders& shaders, DrawInputs& in) {
  Material& m = in.material;
  if (in.terrain_shader) {
    m.status = MaterialStatus::kTerrain;
    return;
  }
  m.status = MaterialStatus::kPsUnknown;
  if (!shaders.have_dev) return;
  const DeviceSnapshot& dev = shaders.dev;
  uint32_t ps_obj = 0;
  const PsInfo* ps = ChoosePs(dev, &ps_obj) ? LookupPs(ps_obj) : nullptr;
  if (!ps) return;
  // Copied at once: ps points into t_ps_cache (the table entry is stable).
  m.ps_hash = ps->hash;
  const AlbedoSpec* albedo = ps->albedo;
  if (!albedo) return;
  const AlbedoSpec& a = *albedo;
  if (a.slot < 0) {
    m.status = MaterialStatus::kNoAlbedo;
    return;
  }
  m.status = MaterialStatus::kUvUnsupported;
  const VsInfo* vs = shaders.vs;
  if (!vs || a.slot >= 32 || a.u_interp >= 16 || a.v_interp >= 16 || a.u_comp >= 4 || a.v_comp >= 4) return;
  const VsUvSpec* u = vs->uv[a.u_interp][a.u_comp];
  const VsUvSpec* v = vs->uv[a.v_interp][a.v_comp];
  UvLayout uv;
  StreamView sv;
  if (!u || !v || !ResolveUvFetch(vs->fetches, *u, *v, &uv) || ResolveStream(dev, uv.fetch_slot, &sv) ||
      !sv.fc_match || !ApplyUvEndian(&uv, sv.fc1 & 3)) {
    return;
  }
  // An instanced draw's flat UVs are the mesh vertex's (ExpandInstanceUvs):
  // the UV element must sit in the mesh stream, not the per-instance one.
  if (in.instance_shader && uv.fetch_slot != in.pos.fetch_slot) return;
  // Constants named by the four stage lists, then the albedo fetch constant.
  // t_bank: only the named registers are written, the transform rows stay.
  bool ok = true;
  auto need = [&](int32_t ref) {
    const uint32_t reg = uint32_t(ref & 0x3FF) / 4;
    ok = ok && (((ref >> 10) & 1) ? ReadPsBankRegisters(device, reg, 1) : ReadBankRegisters(device, reg, 1));
  };
  ForEachRef(u->stages, 2, need);
  ForEachRef(v->stages, 2, need);
  ForEachRef(a.u_stages, 2, need);
  ForEachRef(a.v_stages, 2, need);
  const uint8_t* fc =
      ReadVirtual(device + xdk::kDeviceVertexFetchOffset + xdk::kDeviceTextureFetchStride * uint32_t(a.slot), 24);
  if (!ok || !fc) return;
  for (int i = 0; i < 6; ++i) m.fetch[i] = LoadBe32(fc + 4 * i);
  ComposeAxis(u->stages, a.u_stages, t_bank, t_ps_bank, &m.uv_xform[0], &m.uv_xform[2]);
  ComposeAxis(v->stages, a.v_stages, t_bank, t_ps_bank, &m.uv_xform[1], &m.uv_xform[3]);
  m.uv = uv;
  m.uv_vb = {sv.base + sv.offset, sv.size - sv.offset};
  m.status = MaterialStatus::kTextured;
}

// One outermost guest draw inside the main scene, as a DrawRecord in the frame.
// Draw-argument mapping (frame-map section 8): DrawIndexedVertices r4 prim, r5
// base vertex, r6 start index, r7 index count; DrawVertices r4 prim, r5 start
// vertex, r6 vertex count; DrawIndx:8221C9C8 (auto-indexed quad patches) r5
// first patch, r6 patch count; DrawIndx:82207C30 in adaptive tessellation
// (device VGT_HOS_CNTL shadow = 2) r7 patch count from patch 0. Other draw
// hooks (and 82207C30 in other modes): argument mapping unconfirmed, so
// recorded as unsupported (prim 0).
void RecordDraw(uint32_t id, const LastArgs& a, uint32_t device) {
  DrawInputs in;
  in.func_id = id;
  SkipReason extra = SkipReason::kNone;
  if (id == kHook_D3DDevice_DrawIndexedVertices) {
    in.indexed = true;
    in.prim = a.r[1];
    in.base_vertex = int32_t(a.r[2]);
    in.start = a.r[3];
    in.count = a.r[4];
  } else if (id == kHook_D3DDevice_DrawVertices) {
    in.prim = a.r[1];
    in.start = a.r[2];
    in.count = a.r[3];
  } else if (id == kHook_DrawIndx_8221C9C8) {
    in.prim = kPrimQuadPatch;
    in.start = a.r[2];
    in.count = a.r[3];
    extra = FillTerrainInputs(device, in.start, in.count, in);
  } else if (id == kHook_DrawIndx_82207C30) {
    const uint8_t* hos = device ? ReadVirtual(device + xdk::kDeviceHosCntlOffset, 4) : nullptr;
    if (hos && (LoadBe32(hos) & 3) == xdk::kHosCntlAdaptive) {
      in.prim = kPrimQuadPatch;
      in.start = 0;
      in.count = a.r[4];
      extra = FillTerrainInputs(device, 0, in.count, in);
    }
  }
  DrawShaders shaders;
  if (IsSupportedPrim(in.prim)) extra = FillDrawInputs(device, in, &shaders);
  FillMaterial(device, shaders, in);
  std::lock_guard<std::mutex> lock(g_scene_mutex);
  if (!g_builder.InMainScene()) return;
  DrawRecord r = AssembleRecord(in, g_builder.NextSeq());
  // A capture-side reason (garbage count, bad index memory) replaces
  // AssembleRecord's, which only sees the inputs filled before it.
  r.skip = ResolveSkip(r.skip, extra);
  if (r.skip == SkipReason::kUnsupportedPrim && id < kMaxHookIds) ++g_unsupported_by_hook[id];
  g_builder.Add(r);
}

// Device tiling flag byte, or nullptr if unreadable.
const uint8_t* ReadTilingFlag(uint32_t device) {
  return device ? ReadVirtual(device + xdk::kDeviceTilingFlagOffset, 1) : nullptr;
}

// Reads the device tiling flag at a guest draw (the draw functions only read
// it), moves the main-scene bracket and returns whether the draw is in it.
// `count_unrecorded`: a main-scene draw is counted in the frame's scene
// without a record (no consumer this frame). Evidence is collected while
// discovery is armed.
bool ObserveMainScene(uint32_t device, bool count_unrecorded) {
  const uint8_t* flag = ReadTilingFlag(device);
  const bool tiling = flag && TilingActive(*flag);
  if (!g_armed.load(std::memory_order_relaxed)) {
    std::lock_guard<std::mutex> lock(g_scene_mutex);
    const bool in = ObserveDraw(g_builder, tiling, g_bracket);
    if (in && count_unrecorded) g_builder.CountUnrecorded();
    return in;
  }
  const uint8_t* surface = device ? ReadVirtual(device + xdk::kDeviceSurfaceInfoOffset, 4) : nullptr;
  const uint32_t pitch = surface ? LoadBe32(surface) & xdk::kSurfacePitchMask : 0;
  std::lock_guard<std::mutex> lock(g_scene_mutex);
  if (flag) {
    ++g_evidence.flag_bytes[*flag];
  } else {
    ++g_evidence.flag_unread;
  }
  const bool in = ObserveDraw(g_builder, tiling, g_bracket);
  if (in && count_unrecorded) g_builder.CountUnrecorded();
  (in ? g_evidence.pitch_in : g_evidence.pitch_out).Add(pitch);
  return in;
}

// IndirectBuffer:82286248 (r3 device): evidence only.
void ObserveIndirectBuffer(uint32_t device) {
  if (!g_armed.load(std::memory_order_relaxed)) return;
  const uint8_t* flag = ReadTilingFlag(device);
  std::lock_guard<std::mutex> lock(g_scene_mutex);
  ++(flag && TilingActive(*flag) ? g_evidence.ib_in : g_evidence.ib_out);
}

void AppendPitches(std::string& row, const char* key, const PitchHistogram& h) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), ",\"%s\":{", key);
  row += buf;
  for (uint32_t i = 0; i < h.n; ++i) {
    std::snprintf(buf, sizeof(buf), "%s\"%u\":%u", i ? "," : "", h.pitch[i], h.count[i]);
    row += buf;
  }
  if (h.other) {
    std::snprintf(buf, sizeof(buf), "%s\"other\":%u", h.n ? "," : "", h.other);
    row += buf;
  }
  row += "}";
}

struct FinishedScene {
  std::shared_ptr<const render::FrameScene> scene;  // null with the renderer off
  uint32_t unsupported_by_hook[kMaxHookIds] = {};
};

// Ends the frame's bracket and, with the renderer on, finishes the frame's
// scene (`swap` = guest frame number) with its capture time from `timer`,
// then latches whether the next frame builds records. With `write`
// (discovery armed, caller holds g_mutex with g_file open) writes the frame
// row (frame-map section 8): captured = hooked guest draw calls, in_bracket =
// those in the main scene.
void EndMainSceneFrame(bool write, int frame, uint64_t swap, SwapTimer& timer,
                       FinishedScene* done) {
  BracketStats s;
  SceneEvidence e;
  {
    std::lock_guard<std::mutex> lock(g_scene_mutex);
    s = EndFrame(g_builder, g_bracket);
    e = g_evidence;
    g_evidence = {};
    const double capture_ms = timer.TakeFrameMs();  // also with discovery only
    if (g_render.load(std::memory_order_relaxed)) {
      std::shared_ptr<render::FrameScene> scene = g_builder.Finish(swap);
      scene->records = g_records_frame.load(std::memory_order_relaxed);
      scene->capture_ms = capture_ms;
      done->scene = std::move(scene);
      std::copy(std::begin(g_unsupported_by_hook), std::end(g_unsupported_by_hook),
                std::begin(done->unsupported_by_hook));
      std::fill(std::begin(g_unsupported_by_hook), std::end(g_unsupported_by_hook), 0u);
      // Records start or stop at a frame boundary, never mid-frame.
      g_records_frame.store(g_records_wanted.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
    }
  }
  if (!write) return;
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "{\"kind\":\"frame\",\"frame\":%d,\"captured\":%u,\"in_bracket\":%u,"
                "\"main_scene\":{\"opens\":%u,\"closes\":%u,\"ib_in\":%u,\"ib_out\":%u,"
                "\"flag_unread\":%u,\"flag_bytes\":{",
                frame, s.draws, s.in_bracket, s.opens, s.closes, e.ib_in, e.ib_out, e.flag_unread);
  std::string row = buf;
  bool first = true;
  for (int v = 0; v < 256; ++v) {
    if (!e.flag_bytes[v]) continue;
    std::snprintf(buf, sizeof(buf), "%s\"0x%02X\":%u", first ? "" : ",", v, e.flag_bytes[v]);
    row += buf;
    first = false;
  }
  row += "}";
  AppendPitches(row, "pitch_in", e.pitch_in);
  AppendPitches(row, "pitch_out", e.pitch_out);
  row += "}}\n";
  std::fputs(row.c_str(), g_file);
}

// Caller holds g_mutex.
bool OpenLog() {
  char name[64];
  const std::time_t t = std::time(nullptr);
  std::strftime(name, sizeof(name), "native_discovery_%Y%m%d_%H%M%S.jsonl", std::localtime(&t));
  const std::filesystem::path dir = rex::filesystem::GetExecutableFolder() / "logs";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  g_path = dir / name;
  std::FILE* fp = std::fopen(g_path.string().c_str(), "w");
  if (!fp) {
    REXSYS_WARN("[native-discovery] cannot open {}; discovery disabled", g_path.string());
    g_failed.store(true);
    return false;
  }
  g_file = fp;
  {
    std::string stamp = g_path.stem().string();  // native_discovery_<date>_<time>
    stamp.erase(0, std::string("native_discovery_").size());
    g_tex_dir = dir / ("native_tex_" + stamp);
    g_tex_seen.clear();
    g_tex_count = 0;
    g_geo_dir = dir / ("native_geo_" + stamp);
    g_geo_seen.clear();
    g_geo_count = 0;
    g_geo_bytes = 0;
  }
  REXSYS_INFO("[native-discovery] writing {} frames to {}", FramesRequested(), g_path.string());
  return true;
}

// "0xHASH: n" pairs of a tally's top entries of one kind.
template <size_t N>
std::string FormatTop(const ShaderTally<N>& t, uint8_t kind, size_t k,
                      const char* (*tag)(uint64_t hash) = nullptr) {
  typename ShaderTally<N>::Entry top[N];
  const size_t m = t.Top(kind, top, std::min(k, N));
  std::string out;
  char buf[96];
  for (size_t i = 0; i < m; ++i) {
    const char* suffix = tag ? tag(top[i].hash) : "";
    std::snprintf(buf, sizeof(buf), "%s0x%016llX%s: %u", i ? ", " : "",
                  static_cast<unsigned long long>(top[i].hash), suffix, top[i].count);
    out += buf;
  }
  return out;
}

// Per-shader coverage (the periodic log): the vertex shaders behind each skip
// reason (unsupported-prim is broken down by hook) and the most frequent
// drawable shaders, tagged by how their positions are formed.
void LogShaderBreakdown(const render::FrameScene& sc) {
  constexpr size_t kTopSkipped = 5;
  constexpr size_t kTopDrawable = 10;
  for (size_t i = 1; i < size_t(SkipReason::kCount); ++i) {
    const SkipReason why = SkipReason(i);
    if (!sc.skipped[i] || why == SkipReason::kUnsupportedPrim) continue;
    const std::string top = FormatTop(sc.skipped_by_vs, uint8_t(i), kTopSkipped);
    if (top.empty()) continue;
    REXSYS_INFO("[native] capture: frame {} {} by vs {{{}}}", sc.frame, SkipReasonName(why), top);
  }
  if (sc.skipped_by_vs.Other()) {
    REXSYS_INFO("[native] capture: frame {} skipped by vs: {} draws past the {}-shader tally",
                sc.frame, sc.skipped_by_vs.Other(), render::kSkippedTallySize);
  }
  if (sc.draws.empty()) return;
  ShaderTally<64> drawable;
  for (const DrawRecord& r : sc.draws) drawable.Add(r.vs_hash, 0);
  const std::string top = FormatTop(drawable, 0, kTopDrawable, [](uint64_t hash) {
    if (FindTerrain(hash)) return "(terrain)";
    if (FindInstance(hash)) return "(instanced)";
    if (FindSkin(hash)) return "(skin)";
    const TransformInfo* t = FindTransform(hash);
    return t && t->deformed ? "(deformed)" : "";
  });
  REXSYS_INFO("[native] capture: frame {} drawable by vs {{{}}}{}", sc.frame, top,
              drawable.Other() ? " (+ more shaders)" : "");
}

// Publishes the frame's scene (none for a frame without records, so a
// consumer never draws a stale one); every 300 guest frames logs its coverage
// (frame-map section 9) and the guest-thread capture time, with any view.
void PublishScene(const FinishedScene& done) {
  if (!done.scene) return;
  const render::FrameScene& sc = *done.scene;
  Publisher().Publish(sc.records ? done.scene : nullptr);
  g_capture_window.Add(sc.capture_ms);
  if (sc.frame % 300 != 0) return;
  std::string skipped, unsupported;
  char buf[96];
  for (size_t i = 1; i < size_t(SkipReason::kCount); ++i) {
    if (!sc.skipped[i]) continue;
    std::snprintf(buf, sizeof(buf), "%s%s: %u", skipped.empty() ? "" : ", ",
                  SkipReasonName(SkipReason(i)), sc.skipped[i]);
    skipped += buf;
  }
  for (uint32_t id = 0; id < kMaxHookIds; ++id) {
    if (!done.unsupported_by_hook[id]) continue;
    std::snprintf(buf, sizeof(buf), "%s%s: %u", unsupported.empty() ? "" : ", ", HookName(id),
                  done.unsupported_by_hook[id]);
    unsupported += buf;
  }
  size_t deformed = 0;
  for (const DrawRecord& r : sc.draws) deformed += r.deformed ? 1 : 0;
  // Capture-side material statuses of the drawable records (records on only).
  std::string untextured;
  for (size_t i = 0; i < size_t(MaterialStatus::kCount); ++i) {
    if (!sc.material[i] || MaterialStatus(i) == MaterialStatus::kTextured) continue;
    std::snprintf(buf, sizeof(buf), "%s%s: %u", untextured.empty() ? "" : ", ",
                  MaterialStatusName(MaterialStatus(i)), sc.material[i]);
    untextured += buf;
  }
  // capture: guest-thread capture time of this frame, then the median, p90
  // and max over the frames since the last line. With records off (no
  // composite view) only `captured` is counted.
  REXSYS_INFO(
      "[native] capture: frame {} captured {} drawable {} (deformed {}) skipped {{{}}} nested_total {} "
      "| records {} | capture {:.3f} ms (median {:.3f}, p90 {:.3f}, max {:.3f} over {} frames) "
      "| textured {} untextured by reason {{{}}}",
      sc.frame, sc.captured, sc.draws.size(), deformed, skipped,
      g_nested_draws.load(std::memory_order_relaxed), sc.records ? "on" : "off", sc.capture_ms,
      g_capture_window.Median(), g_capture_window.Percentile(0.9), g_capture_window.Max(),
      g_capture_window.size(), sc.material[size_t(MaterialStatus::kTextured)], untextured);
  g_capture_window.Reset();
  if (!sc.untextured_ps.empty()) {
    // Tagged by what the albedo table knows of the shader: absent (ps-unknown),
    // "(no-albedo)", or "(uv)" (has an albedo; the UVs did not resolve).
    std::string top;
    for (const auto& [hash, n] : sc.untextured_ps) {
      const AlbedoSpec* a = FindAlbedo(hash);
      std::snprintf(buf, sizeof(buf), "%s0x%016llX%s: %u", top.empty() ? "" : ", ",
                    static_cast<unsigned long long>(hash), !a ? "" : a->slot < 0 ? "(no-albedo)" : "(uv)", n);
      top += buf;
    }
    REXSYS_INFO("[native] capture: frame {} untextured by ps {{{}}}", sc.frame, top);
  }
  if (!unsupported.empty()) {
    REXSYS_INFO("[native] capture: frame {} unsupported by hook {{{}}}", sc.frame, unsupported);
  }
  LogShaderBreakdown(sc);
}

}  // namespace

render::ScenePublisher& Publisher() {
  static render::ScenePublisher publisher;
  return publisher;
}

void SetMemory(rex::memory::Memory* memory) {
  GuestMemory() = memory;
  g_render.store(REXCVAR_GET(fable2_native_render), std::memory_order_relaxed);
  g_discovery_active.store(FramesRequested() > 0, std::memory_order_relaxed);
  const bool enabled = REXCVAR_GET(fable2_native_render) || FramesRequested() > 0;
  if (enabled) CalibrateTicks();  // before the hooks see g_enabled
  g_enabled.store(enabled, std::memory_order_release);
}

bool Enabled() { return g_enabled.load(std::memory_order_relaxed); }

void SetRecordsWanted(bool wanted) {
  g_records_wanted.store(wanted, std::memory_order_relaxed);
}

void OnXdkCall(uint32_t id, PPCContext& ctx, uint8_t*) {
  if (!g_enabled.load(std::memory_order_relaxed) || id >= t_last_args.size()) return;
  // Not timed: eight stores, cheaper than the timer itself.
  LastArgs& a = t_last_args[id];
  a.r[0] = ctx.r3.u32;
  a.r[1] = ctx.r4.u32;
  a.r[2] = ctx.r5.u32;
  a.r[3] = ctx.r6.u32;
  a.r[4] = ctx.r7.u32;
  a.r[5] = ctx.r8.u32;
  a.r[6] = ctx.r9.u32;
  a.r[7] = ctx.r10.u32;
  if (IsDrawId(id) && t_nesting.Enter()) g_nested_draws.fetch_add(1, std::memory_order_relaxed);
  switch (id) {
    case kHook_D3DDevice_SetStreamSource:
    case kHook_D3DDevice_SetIndices:
    case kHook_D3DDevice_SetVertexShader:
    case kHook_D3DDevice_SetPixelShader:
    case kHook_D3DDevice_SetPending_AluConstants:
      UpdateState(id, ctx);  // timed inside while discovery needs the state
      break;
    default:
      break;
  }
}

void OnXdkReturn(uint32_t id, PPCContext&, uint8_t*) {
  if (!g_enabled.load(std::memory_order_relaxed) || id >= t_last_args.size()) return;
  if (id == kHook_VsLoadImmediate_821DFDE0) {
    CaptureTimer timer;
    OnVsLoadImmediate(t_last_args[id]);
    return;
  }
  if (id == kHook_GpuLoadShaders_82221978) {
    CaptureTimer timer;
    OnGpuLoadShaders(t_last_args[id]);
    return;
  }
  if (id == kHook_IndirectBuffer_82286248) {
    if (!g_armed.load(std::memory_order_relaxed)) return;
    CaptureTimer timer;
    ObserveIndirectBuffer(t_last_args[id].r[0]);
    return;
  }
  if (!IsDrawId(id)) return;
  CaptureTimer timer;
  const bool outermost = t_nesting.Leave();
  const LastArgs& args = t_last_args[id];
  const uint32_t device = args.r[0];  // r3 of every draw hook is the device
  // Records: only the outermost draw of a nested call chain, so geometry is
  // never captured twice, and only for a frame a consumer wants records for;
  // otherwise the draw is only counted.
  const bool render = outermost && g_render.load(std::memory_order_relaxed);
  const bool records = render && g_records_frame.load(std::memory_order_relaxed);
  const bool in_scene = ObserveMainScene(device, render && !records);
  if (in_scene && records) RecordDraw(id, args, device);
  if (!g_armed.load(std::memory_order_relaxed)) return;
  const uint64_t n = g_draw_count.fetch_add(1, std::memory_order_relaxed);
  if (n % Every() != 0) return;
  DeviceSnapshot dev;
  const bool have_dev = ReadDevice(device, &dev);
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_file) return;
  WriteRawRow(id, args, device, have_dev ? &dev : nullptr);
  if (have_dev &&
      (id == kHook_D3DDevice_DrawVertices || id == kHook_D3DDevice_DrawIndexedVertices)) {
    WriteDrawRow(id, args, device, dev, in_scene);
  } else if (have_dev && (id == kHook_DrawIndx_8221C9C8 || id == kHook_DrawIndx_82207C30)) {
    WriteTessRow(id, args, device, dev, in_scene);
  }
}

void OnSwap() {
  if (!g_enabled.load(std::memory_order_acquire)) return;
  SwapTimer timer;
  const uint64_t swap = g_swaps.fetch_add(1, std::memory_order_relaxed) + 1;
  detail::CacheGeneration().fetch_add(1, std::memory_order_relaxed);
  if (!g_armed.load(std::memory_order_relaxed)) {
    FinishedScene done;
    EndMainSceneFrame(false, 0, swap, timer, &done);
    PublishScene(done);
  }
  if (FramesRequested() <= 0 || g_failed.load(std::memory_order_relaxed)) return;
  static const auto start = std::chrono::steady_clock::now();
  if (!g_armed.load(std::memory_order_relaxed)) {
    if (g_frames_written.load(std::memory_order_relaxed) >= FramesRequested()) return;
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (elapsed < DelaySeconds()) return;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_file && !OpenLog()) {
        g_discovery_active.store(false, std::memory_order_relaxed);
        return;
      }
    }
    g_armed.store(true);
    REXSYS_INFO("[native-discovery] armed after {:.1f} s", elapsed);
    return;
  }
  // One guest frame of rows has completed.
  const int frame = g_frames_written.fetch_add(1) + 1;
  FinishedScene done;
  std::unique_lock<std::mutex> lock(g_mutex);
  EndMainSceneFrame(g_file != nullptr, frame, swap, timer, &done);
  if (g_file) std::fflush(g_file);
  if (frame >= FramesRequested()) {
    g_armed.store(false);
    g_discovery_active.store(false, std::memory_order_relaxed);
    if (g_file) {
      std::fclose(g_file);
      g_file = nullptr;
    }
    REXSYS_INFO("[native-discovery] complete: {} frames written to {}; {} stream dumps, {} bytes",
                frame, g_path.string(), g_geo_count, g_geo_bytes);
  }
  lock.unlock();
  PublishScene(done);
}

}  // namespace fable2::native::capture
