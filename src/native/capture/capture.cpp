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
#include <tuple>
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
#include "fog_block.h"
#include "guest_read.h"
#include "light_block.h"
#include "main_scene.h"
#include "material.h"
#include "position_decode.h"
#include "post_block.h"
#include "quad_record.h"
#include "render_state.h"
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
// The consumer also wants the two shadow passes (SetShadowsWanted: the native
// view, the light and the shadows on), and the value latched for the current
// guest frame with g_records_frame: set only in a frame with records. With it
// clear nothing of the shadows is read, observed or recorded.
std::atomic<bool> g_shadows_wanted{false};
std::atomic<bool> g_shadows_frame{false};
// The consumer also wants the frame's fog and sky blocks (SetFogWanted: the
// native view, the light and the fog on), latched like the shadows' and set
// only in a frame with records. With it clear nothing of the fog or the sky is
// read: no register, no fetch constant, no table.
std::atomic<bool> g_fog_wanted{false};
std::atomic<bool> g_fog_frame{false};
// The consumer also wants the frame's post block (SetPostWanted: the native
// view, the light and the post chain on), latched like the shadows' and set
// only in a frame with records. With it clear no hooked draw outside the main
// scene is looked at for the post chain and nothing of it is read.
std::atomic<bool> g_post_wanted{false};
std::atomic<bool> g_post_frame{false};
// The consumer also wants the frame's quad records and the radial blur's registers (SetSunWanted:
// the native view, the light, the post chain and the sun's switch on: fable2_native_sun), and the
// frame being built is read for them. Latched like the post block's word, and only with it.
std::atomic<bool> g_sun_wanted{false};
std::atomic<bool> g_quads_frame{false};
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
// Frames with records since the last periodic log line, and how many of them have no light
// block ("light-block"). OnSwap thread only.
uint32_t g_light_frames = 0;
uint32_t g_light_block_missing = 0;
// The same for the shadows: frames with shadow records since the last line, and how many of
// them have no shadow block ("shadow-block"). OnSwap thread only.
uint32_t g_shadow_frames = 0;
uint32_t g_shadow_block_missing = 0;
// The same for the fog and the sky: frames with the fog wanted since the last line, and those
// of them without a fog block ("fog-block") and without a sky block ("sky-block"), by reason.
// OnSwap thread only.
uint32_t g_fog_frames = 0;
uint32_t g_fog_block_missing[size_t(FogBlockReason::kCount)] = {};
uint32_t g_sky_block_missing[size_t(SkyBlockReason::kCount)] = {};
// Frames since the last line whose fog tables, read again when the frame was over, were not the
// bytes the frame's block was made of (PublishScene), and the frames in which that was asked.
uint32_t g_fog_tables_checked = 0;
uint32_t g_fog_tables_changed = 0;
// Lines written about a frame whose fog block was read and refused (at most kFogRefusedLines).
uint32_t g_fog_refused_lines = 0;
constexpr uint32_t kFogRefusedLines = 8;
// The same for the post block: frames with it wanted since the last line, those of them without
// a block ("post-block") by reason, and those whose block is a kept one of an earlier frame
// (FrameScene::post_age above 0). OnSwap thread only.
uint32_t g_post_frames = 0;
uint32_t g_post_block_missing[size_t(PostBlockReason::kCount)] = {};
uint32_t g_post_kept_frames = 0;
// The same for the quad records: frames with them wanted since the last line, those of them
// without a set to draw ("quad-record") by reason, and those whose post block has the radial
// blur's registers. The last frame's reason and the lines written about a change of it (at most
// kQuadChangeLines). OnSwap thread only.
uint32_t g_quad_frames = 0;
uint32_t g_quad_set_missing[size_t(QuadSetReason::kCount)] = {};
uint32_t g_quad_radial_frames = 0;
QuadSetReason g_quad_last_reason = QuadSetReason::kOff;
uint32_t g_quad_change_lines = 0;
constexpr uint32_t kQuadChangeLines = 16;

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
// The same for the textures kept once per content (render::DumpKeyedByContent: 1D tables and the
// game's own targets): the (base address, identity, content hash) triples already dumped, the
// files and the bytes written.
std::set<std::tuple<uint32_t, uint64_t, uint64_t>> g_tex_content_seen;
uint32_t g_tex_content_count = 0;
uint64_t g_tex_content_bytes = 0;
// Discovery stream dumps (caller holds g_mutex): the capture's dump folder, the
// content hashes already dumped per (address, size), files and bytes written.
std::filesystem::path g_geo_dir;
std::map<std::pair<uint32_t, uint32_t>, std::vector<uint64_t>> g_geo_seen;
uint32_t g_geo_count = 0;
uint64_t g_geo_bytes = 0;

// Frames to capture and the draw sampling: seeded from the environment, replaced by
// ArmDiscoveryNow. Written only by OnSwap (guest thread), always before g_armed is set.
std::atomic<int>& FramesTarget() {
  static std::atomic<int> frames = [] {
    const char* v = std::getenv("FABLE2_NATIVE_DISCOVERY");
    return v ? std::max(0, std::atoi(v)) : 0;
  }();
  return frames;
}

int FramesRequested() { return FramesTarget().load(std::memory_order_relaxed); }

std::atomic<uint64_t>& EverySetting() {
  static std::atomic<uint64_t> n = [] {
    const char* v = std::getenv("FABLE2_NATIVE_DISCOVERY_EVERY");
    const long long x = v ? std::atoll(v) : 64;
    return uint64_t(x > 0 ? x : 64);
  }();
  return n;
}

// A run-time capture request (ArmDiscoveryNow): (frames << 16) | every, 0 = none. Taken
// by OnSwap, which opens the file and arms; it ignores the environment delay.
std::atomic<uint64_t> g_runtime_request{0};
std::atomic<bool> g_runtime_capture{false};  // current capture came from a request

double DelaySeconds() {
  static const double d = [] {
    const char* v = std::getenv("FABLE2_NATIVE_DISCOVERY_DELAY");
    return v ? std::max(0.0, std::atof(v)) : 0.0;
  }();
  return d;
}

uint64_t Every() { return EverySetting().load(std::memory_order_relaxed); }

thread_local std::array<LastArgs, kMaxHookIds> t_last_args;  // indexed by hook id
thread_local DrawNesting t_nesting;  // only the outermost draw of a call chain is recorded

// World-view-projection constants per vertex shader hash, found offline by
// tools/xdk_sigmatch/matrix_finder.py or confirmed by hand from the shader's
// disassembly (docs/native-renderer/vs-transforms.json, frame-map section 9).
// The generated table also holds per-shader position swizzles, skin layouts
// (one bone or weighted), instance layouts and terrain parameters.
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
#define FABLE2_VS_LIGHT(H, K, NF, NM, NO, NS, SF, SM, SO, SS, A0, SC, PW)
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_INSTANCE
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
#undef FABLE2_VS_LIGHT
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
#define FABLE2_VS_LIGHT(H, K, NF, NM, NO, NS, SF, SM, SO, SS, A0, SC, PW)
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_INSTANCE
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
#undef FABLE2_VS_LIGHT
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
#define FABLE2_VS_LIGHT(H, K, NF, NM, NO, NS, SF, SM, SO, SS, A0, SC, PW)
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_INSTANCE
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
#undef FABLE2_VS_LIGHT
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
#define FABLE2_VS_LIGHT(H, K, NF, NM, NO, NS, SF, SM, SO, SS, A0, SC, PW)
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_INSTANCE
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
#undef FABLE2_VS_LIGHT
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
#define FABLE2_VS_LIGHT(H, K, NF, NM, NO, NS, SF, SM, SO, SS, A0, SC, PW)
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_INSTANCE
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
#undef FABLE2_VS_LIGHT
    {0, {0, 0, 0, 0, 0, 0, -1, 0}}};

// Albedo texture slot and UV interpolators per pixel shader hash
// (docs/native-renderer/ps-albedo.json, tools/xdk_sigmatch/gen_albedo_table.py)
// and the vertex fetch behind each UV interpolator component per vertex
// shader hash (vs-transforms.json "uv"). Either may be empty.
const std::vector<AlbedoSpec> kAlbedoTable = {
#define FABLE2_PS_ALBEDO(H, SL, UI, UC, US0, UO0, US1, UO1, VI, VC, VS0, VO0, VS1, VO1) \
  {H, int8_t(SL), UI, UC, VI, VC, {{US0, UO0}, {US1, UO1}}, {{VS0, VO0}, {VS1, VO1}}},
#define FABLE2_PS_NO_ALBEDO(H) {H, int8_t(-1), 0, 0, 0, 0, {}, {}},
#define FABLE2_PS_ALPHA(H, S, SL, C, SR0, SR1, DC)
#define FABLE2_PS_TERRAIN(H, K, B, D, SS, ST, SE)
#define FABLE2_PS_LIGHT(H, F, M, FS)
#define FABLE2_PS_LIGHT_VS(H, VS)
#include "ps_albedo_table.inc"
#undef FABLE2_PS_ALBEDO
#undef FABLE2_PS_NO_ALBEDO
#undef FABLE2_PS_ALPHA
#undef FABLE2_PS_TERRAIN
#undef FABLE2_PS_LIGHT
#undef FABLE2_PS_LIGHT_VS
};

// Alpha recipe per pixel shader hash (ps-albedo.json "alpha"); the source is the generated
// 0 one, 1 albedo, 2 fetch; the two scale refs are pixel constant register * 4 + component or -1.
struct PsAlphaEntry {
  uint64_t ps_hash;
  AlphaSpec spec;
};
const std::vector<PsAlphaEntry> kAlphaTable = {
#define FABLE2_PS_ALBEDO(H, SL, UI, UC, US0, UO0, US1, UO1, VI, VC, VS0, VO0, VS1, VO1)
#define FABLE2_PS_NO_ALBEDO(H)
#define FABLE2_PS_ALPHA(H, S, SL, C, SR0, SR1, DC) \
  {H, {AlphaSource((S) + 1), uint8_t(SL), uint8_t(C), {int32_t(SR0), int32_t(SR1)}, (DC) != 0}},
#define FABLE2_PS_TERRAIN(H, K, B, D, SS, ST, SE)
#define FABLE2_PS_LIGHT(H, F, M, FS)
#define FABLE2_PS_LIGHT_VS(H, VS)
#include "ps_albedo_table.inc"
#undef FABLE2_PS_ALBEDO
#undef FABLE2_PS_NO_ALBEDO
#undef FABLE2_PS_ALPHA
#undef FABLE2_PS_TERRAIN
#undef FABLE2_PS_LIGHT
#undef FABLE2_PS_LIGHT_VS
};

// Terrain pixel shaders (ps-albedo.json entries whose "kind" is a terrain kind): the kind and the
// texture fetch slots of the base map and of the detail texture (far kinds: the far colour), and
// for a layer shader with the steep-ground block ("steep") the side texture's slot with the
// block's two literals (has_steep; terrain_patch.h, TerrainSteepSpec).
// These shaders have no albedo entry, so FindAlbedo does not know them.
struct PsTerrainEntry {
  uint64_t ps_hash;
  MaterialKind kind;
  uint8_t base_slot, detail_slot;
  bool has_steep;
  TerrainSteepSpec steep;
};
const std::vector<PsTerrainEntry> kPsTerrainTable = {
#define FABLE2_PS_ALBEDO(H, SL, UI, UC, US0, UO0, US1, UO1, VI, VC, VS0, VO0, VS1, VO1)
#define FABLE2_PS_NO_ALBEDO(H)
#define FABLE2_PS_ALPHA(H, S, SL, C, SR0, SR1, DC)
#define FABLE2_PS_TERRAIN(H, K, B, D, SS, ST, SE) \
  {H, MaterialKind(K), uint8_t(B), uint8_t(D), (SS) >= 0, {uint8_t((SS) >= 0 ? (SS) : 0), ST, SE}},
#define FABLE2_PS_LIGHT(H, F, M, FS)
#define FABLE2_PS_LIGHT_VS(H, VS)
#include "ps_albedo_table.inc"
#undef FABLE2_PS_ALBEDO
#undef FABLE2_PS_NO_ALBEDO
#undef FABLE2_PS_ALPHA
#undef FABLE2_PS_TERRAIN
#undef FABLE2_PS_LIGHT
#undef FABLE2_PS_LIGHT_VS
};
const std::vector<VsUvSpec> kVsUvTable = {
#define FABLE2_VS_TRANSFORM(H, B, L, P, D)
#define FABLE2_VS_POS_SWIZZLE(H, S)
#define FABLE2_VS_SKIN(H, I, W, R0, R1, R2, S0, S1, S2, N, IC0, WC0, IC1, WC1, IC2, WC2, IC3, WC3)
#define FABLE2_VS_INSTANCE(H, R0, R1, R2, S0, S1, S2, INV, CNT, FIRST, BIAS, OFF, CE, CD)
#define FABLE2_VS_TERRAIN(H, G, CE, HS, O, TO, TS, PO, F)
#define FABLE2_VS_UV(H, I, C, F, S, FM, O, S0, O0, S1, O1) \
  {H, I, C, int8_t(F), S, FM, O, {{S0, O0}, {S1, O1}}},
#define FABLE2_VS_LIGHT(H, K, NF, NM, NO, NS, SF, SM, SO, SS, A0, SC, PW)
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_INSTANCE
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
#undef FABLE2_VS_LIGHT
};


// Light families (ps-albedo.json "light", vs-transforms.json "light"; material.h): per pixel shader
// its family, the "-multi" flag and the facing literal; the (pixel, vertex) pairs the family was
// checked with (tools/xdk_sigmatch/light_check.py); per vertex shader what the light reads on the
// vertex side. A draw has a family only when all three name it (MakeLightInputs).
const std::vector<PsLightSpec> kPsLightTable = {
#define FABLE2_PS_ALBEDO(H, SL, UI, UC, US0, UO0, US1, UO1, VI, VC, VS0, VO0, VS1, VO1)
#define FABLE2_PS_NO_ALBEDO(H)
#define FABLE2_PS_ALPHA(H, S, SL, C, SR0, SR1, DC)
#define FABLE2_PS_TERRAIN(H, K, B, D, SS, ST, SE)
#define FABLE2_PS_LIGHT(H, F, M, FS) {H, LightFamily(F), (M) != 0, FS},
#define FABLE2_PS_LIGHT_VS(H, VS)
#include "ps_albedo_table.inc"
#undef FABLE2_PS_ALBEDO
#undef FABLE2_PS_NO_ALBEDO
#undef FABLE2_PS_ALPHA
#undef FABLE2_PS_TERRAIN
#undef FABLE2_PS_LIGHT
#undef FABLE2_PS_LIGHT_VS
};
const std::vector<PsLightPair> kPsLightPairs = {
#define FABLE2_PS_ALBEDO(H, SL, UI, UC, US0, UO0, US1, UO1, VI, VC, VS0, VO0, VS1, VO1)
#define FABLE2_PS_NO_ALBEDO(H)
#define FABLE2_PS_ALPHA(H, S, SL, C, SR0, SR1, DC)
#define FABLE2_PS_TERRAIN(H, K, B, D, SS, ST, SE)
#define FABLE2_PS_LIGHT(H, F, M, FS)
#define FABLE2_PS_LIGHT_VS(H, VS) {H, VS},
#include "ps_albedo_table.inc"
#undef FABLE2_PS_ALBEDO
#undef FABLE2_PS_NO_ALBEDO
#undef FABLE2_PS_ALPHA
#undef FABLE2_PS_TERRAIN
#undef FABLE2_PS_LIGHT
#undef FABLE2_PS_LIGHT_VS
};
const std::vector<VsLightSpec> kVsLightTable = {
#define FABLE2_VS_TRANSFORM(H, B, L, P, D)
#define FABLE2_VS_POS_SWIZZLE(H, S)
#define FABLE2_VS_SKIN(H, I, W, R0, R1, R2, S0, S1, S2, N, IC0, WC0, IC1, WC1, IC2, WC2, IC3, WC3)
#define FABLE2_VS_INSTANCE(H, R0, R1, R2, S0, S1, S2, INV, CNT, FIRST, BIAS, OFF, CE, CD)
#define FABLE2_VS_TERRAIN(H, G, CE, HS, O, TO, TS, PO, F)
#define FABLE2_VS_UV(H, I, C, F, S, FM, O, S0, O0, S1, O1)
#define FABLE2_VS_LIGHT(H, K, NF, NM, NO, NS, SF, SM, SO, SS, A0, SC, PW)                                    \
  {H, LightVertexKind(K), {int8_t(NF), uint8_t(NM), uint8_t(NO), uint16_t(NS)},                                \
   {int8_t(SF), uint8_t(SM), uint8_t(SO), uint16_t(SS)}, int8_t(A0), SC, PW},
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_INSTANCE
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
#undef FABLE2_VS_LIGHT
};

// The light's per-draw registers are read from the device's banks as they stand: none of them is
// a register a buffer load can name (light_block.h, kBufferLoadRanges), for which the bank would
// hold another value than the GPU.
constexpr bool LightRegistersAreBankOnly() {
  for (uint32_t r = kLightVsRotationReg; r < kLightVsRotationReg + 4; ++r) {  // c4..c6 and c7 (the scale)
    if (IsBufferLoadRegister(kVertexBank, r)) return false;
  }
  for (uint32_t r = kLightVsProbeReg; r < kLightVsProbeReg + kLightProbeRegisters; ++r) {
    if (IsBufferLoadRegister(kVertexBank, r)) return false;
  }
  return !IsBufferLoadRegister(kVertexBank, kLightVsIndexOffsetReg) && !IsBufferLoadRegister(kVertexBank, kLightVsBendReg) &&
         !IsBufferLoadRegister(kPixelBank, kLightPsSunScaleReg) && !IsBufferLoadRegister(kPixelBank, kLightPsFadeReg) &&
         !IsBufferLoadRegister(kPixelBank, kLightPsAmbientReg) && !IsBufferLoadRegister(kPixelBank, kLightPsTintReg);
}
static_assert(LightRegistersAreBankOnly(), "a light register is loaded from a constant buffer: the bank does not hold it");

// Linear scans; callers cache the result per shader (PsInfo, VsInfo).
const AlbedoSpec* FindAlbedo(uint64_t ps_hash) {
  for (const AlbedoSpec& e : kAlbedoTable) {
    if (e.ps_hash == ps_hash) return &e;
  }
  return nullptr;
}

AlphaSpec FindAlpha(uint64_t ps_hash) {
  for (const PsAlphaEntry& e : kAlphaTable) {
    if (e.ps_hash == ps_hash) return e.spec;
  }
  return {};
}

const PsTerrainEntry* FindPsTerrain(uint64_t ps_hash) {
  for (const PsTerrainEntry& e : kPsTerrainTable) {
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

const PsLightSpec* FindPsLight(uint64_t ps_hash) {
  for (const PsLightSpec& e : kPsLightTable) {
    if (e.ps_hash == ps_hash) return &e;
  }
  return nullptr;
}

const VsLightSpec* FindVsLight(uint64_t vs_hash) {
  for (const VsLightSpec& e : kVsLightTable) {
    if (e.vs_hash == vs_hash) return &e;
  }
  return nullptr;
}

// Whether the family of pixel shader `ps_hash` was checked with vertex shader `vs_hash`.
bool LightPairChecked(uint64_t ps_hash, uint64_t vs_hash) {
  for (const PsLightPair& p : kPsLightPairs) {
    if (p.ps_hash == ps_hash && p.vs_hash == vs_hash) return true;
  }
  return false;
}

// The shader pairs the game draws into its two sun shadow maps
// (docs/native-renderer/shadow-casters.json; lighting-formulas.md, "Casters"):
// `ps_hash` 0 for a draw without a pixel shader. An alpha-test caster's alpha is
// the w of the texture in fetch slot `slot`, read at interpolator 0 (xy) times
// pixel c8.xy plus c8.zw, times the pixel constant `scale_ref` (register * 4 +
// component; -1: none).
struct CasterEntry {
  uint64_t vs_hash;
  uint64_t ps_hash;
  CasterKind kind;
  int8_t slot;
  int16_t scale_ref;
};
constexpr CasterEntry kCasterTable[] = {
#define FABLE2_SHADOW_CASTER(VS, PS, K, SL, SR) {VS, PS, CasterKind(K), int8_t(SL), int16_t(SR)},
#include "shadow_caster_table.inc"
#undef FABLE2_SHADOW_CASTER
};
// What the table may hold: the two kinds; an opaque caster without a texture,
// an alpha-test caster with a pixel shader, a texture slot and a scale that is
// a pixel constant component or none.
constexpr bool CasterTableIsWellFormed() {
  for (const CasterEntry& e : kCasterTable) {
    if (e.vs_hash == 0) return false;
    if (e.kind == CasterKind::kOpaque) {
      if (e.slot != -1 || e.scale_ref != -1) return false;
    } else if (e.kind == CasterKind::kAlphaTest) {
      if (e.ps_hash == 0 || e.slot < 0 || e.slot >= 32 || e.scale_ref < -1 || e.scale_ref >= 1024) return false;
    } else {
      return false;
    }
  }
  return true;
}
static_assert(CasterTableIsWellFormed(), "shadow_caster_table.inc holds an entry the capture cannot read");
// The pixel registers a caster's material reads (c8: the UV scale and offset; the scale of the
// table) are the bank's at any draw: no buffer load names them.
constexpr bool CasterRegistersAreBankOnly() {
  if (IsBufferLoadRegister(kPixelBank, 8)) return false;
  for (const CasterEntry& e : kCasterTable) {
    if (e.scale_ref >= 0 && IsBufferLoadRegister(kPixelBank, uint32_t(e.scale_ref) / 4)) return false;
  }
  return true;
}
static_assert(CasterRegistersAreBankOnly(), "a caster's pixel register is loaded from a constant buffer");

// The caster entry of a shadow-pass draw's shaders (`ps_hash` 0: no pixel shader), or null.
const CasterEntry* FindCaster(uint64_t vs_hash, uint64_t ps_hash) {
  for (const CasterEntry& e : kCasterTable) {
    if (e.vs_hash == vs_hash && e.ps_hash == ps_hash) return &e;
  }
  return nullptr;
}

// The vertex shader of the shadow passes that builds the world position itself and multiplies
// it by c0..c3, so that c0..c3 is the light matrix and c4..c6 are not world rows: the instanced
// bushes (frame-map section 15, "Passes"; tools/xdk_sigmatch/shadow_matrices.py, DIRECT_VS).
// Every other caster shader has oPos = c0..c3 . p with the world rows in c4..c6.
constexpr uint64_t kShadowDirectMatrixVs = 0xA8A5B2A89F70336Aull;

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

// --- Pixel constants (frame-map section 14, "Pixel constants") ----------------
// Where the GPU's pixel constants come from on this thread: the device's bank,
// or the constant buffers LoadAluConstants named (light_block.h, ConstantLoads;
// xdk_layout.h, kAluLoad*). The two upload hooks hand it every upload only
// while somebody reads the result (FollowPixelLoads: a frame with records, or
// discovery); otherwise they only mark that an upload was skipped
// (ConstantLoads::Skip), and no pixel constant that a load can name (c0..c7,
// c80..c115: light_block.h, kBufferLoadRanges) is known until the first main
// scene whose uploads are all followed from its begin (ConstantLoads::Observe).
// Every other pixel constant is the bank's whatever was followed.
// constinit (the constructor is constexpr): the model is initialised with the
// thread, so no access, the skipped path included, tests an initialisation guard.
constinit thread_local ConstantLoads t_ps_loads{kPixelBank};
// The same for the vertex bank (vertex c40..c43 and c76..c107 are loaded from
// buffers too). Discovery evidence only ("vfull"): no record reads through
// it, so it is followed only while a discovery capture is requested or
// running. Once an upload was skipped (ConstantLoads::ever_skipped) rows carry
// no vertex registers (a capture armed by the environment follows from the
// first upload; one armed at run time does not).
constinit thread_local ConstantLoads t_vs_loads{kVertexBank};
// A followed LoadAluConstants call named a register that no call site of the
// table names (xdk_layout.h, kAluLoadSites): logged once per process.
std::atomic<bool> g_untabled_load_logged{false};
// The device of the draw this thread is capturing (the r3 of its draw hook).
thread_local uint32_t t_draw_device = 0;

// Discovery evidence: the pixel bool word (b128..b159) as its last writer on
// this thread put it on the GPU (light_block.h, BoolWordWriters). Rows carry
// the word as "pbool_load" beside the device's shadow "pbool".
thread_local BoolWordWriters t_gpu_pbool;
// The bool shadow was pending when this thread's current draw hook was entered
// (discovery only): the builder then flushes it.
thread_local bool t_bool_flush_pending = false;

// Whether this thread's pixel constant uploads are handed to t_ps_loads now.
bool FollowPixelLoads() {
  return g_discovery_active.load(std::memory_order_relaxed) ||
         (g_records_frame.load(std::memory_order_relaxed) && g_render.load(std::memory_order_relaxed));
}

// Tells t_ps_loads whether the main scene's bracket is open as an upload hook
// that is being followed finds it: the device's tiling flag, the byte the
// draws read (main_scene.h). Called before the hook's own upload is handed
// over, so an upload at a scene's begin belongs to the scene. A flag that
// cannot be read is taken as no scene.
void ObservePixelScene(uint32_t device) {
  const uint8_t* flag = device ? ReadVirtual(device + xdk::kDeviceTilingFlagOffset, 1) : nullptr;
  t_ps_loads.Observe(flag && TilingActive(*flag));
}

// The four dwords of constant `reg` from `from`: the 256-register bank at
// guest address `bank` (0: none), or register `from.index` of a constant
// buffer. False when the source is unknown or its memory cannot be read.
bool ReadConstantBits(uint32_t bank, const ConstantSource& from, uint32_t reg, uint32_t bits[4]) {
  const uint8_t* p = nullptr;
  if (from.kind == ConstantSource::Kind::kBank) {
    p = bank && reg < ConstantLoads::kRegisters ? ReadVirtual(bank + 16 * reg, 16) : nullptr;
  } else if (from.kind == ConstantSource::Kind::kMemory) {
    uint32_t data = 0, address = 0;
    if (from.object && ReadVirtualBe32(from.object + 4 * xdk::kConstantBufferDataDword, &data) &&
        BufferRegisterAddress(data, from.index, &address)) {
      p = ReadVirtual(address, 16);
    }
  }
  if (!p) return false;
  for (int i = 0; i < 4; ++i) bits[i] = LoadBe32(p + 4 * i);
  return true;
}

// The device's pixel and vertex constant banks (0 without a device).
uint32_t PixelBank(uint32_t device) {
  const uint32_t bank_ptr = g_ps_bank_ptr.load(std::memory_order_relaxed);
  return bank_ptr ? bank_ptr : (device ? device + xdk::kDevicePsConstantsOffset : 0);
}
uint32_t VertexBank(uint32_t device) {
  const uint32_t bank_ptr = g_vs_bank_ptr.load(std::memory_order_relaxed);
  return bank_ptr ? bank_ptr : (device ? device + xdk::kDeviceVsConstantsOffset : 0);
}

void UpdateState(uint32_t id, const PPCContext& ctx) {
  // Records read only the constant bank pointers, lock-free (r5 0x4000 vertex,
  // 0x4400 pixel; xdk_layout.h kDeviceVsConstantsOffset / kDevicePsConstantsOffset).
  if (id == kHook_D3DDevice_SetPending_AluConstants) {
    if (ctx.r5.u32 == 0x4000) {
      g_vs_bank_ptr.store(ctx.r6.u32, std::memory_order_relaxed);
      if (g_discovery_active.load(std::memory_order_relaxed)) {
        t_vs_loads.BankUpload(ctx.r4.u64);
      } else {
        t_vs_loads.Skip();
      }
    } else if (ctx.r5.u32 == 0x4400) {
      g_ps_bank_ptr.store(ctx.r6.u32, std::memory_order_relaxed);
      // r4: the pending mask, one bit per group of four registers. The bank's
      // values replace what a load put in those registers. Followed only while
      // somebody reads the result; otherwise the upload is marked as skipped
      // (three byte stores, nothing of the guest is read, and no timer). The
      // followed path is capture work of the frame: timed here, since the
      // timer below starts only for discovery.
      if (FollowPixelLoads()) {
        CaptureTimer timer;
        ObservePixelScene(ctx.r3.u32);
        t_ps_loads.BankUpload(ctx.r4.u64);
      } else {
        t_ps_loads.Skip();
      }
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

// Discovery evidence: the literal constants a shader object uploads itself
// (xdk_layout.h, kShaderLiteralTableField; zero in the device banks), as
// [{"reg":first register,"v":[floats]},...]: [] for a shader without a table,
// null when the table or a run cannot be read. Bounded: the first
// kMaxLiteralRuns runs, kMaxLiteralDwords dwords of each.
constexpr uint32_t kMaxLiteralRuns = 4;
constexpr uint32_t kMaxLiteralDwords = 64;

std::string LiteralsJson(const UcodeLayout& l, uint32_t obj, uint32_t variant) {
  uint32_t table_off = 0, base = 0, size = 0;
  const uint32_t header = obj + l.header_offset;
  if (!obj || !ReadVirtualBe32(header + xdk::kShaderLiteralTableField + 8 * variant, &table_off) ||
      !ReadVirtualBe32(obj + 4 * l.base_dword, &base)) {
    return "null";
  }
  if (table_off == 0) return "[]";
  const uint32_t table = header + table_off;
  if (!ReadVirtualBe32(table + xdk::kLiteralTableSizeOffset, &size)) return "null";
  size = std::min(size, 8 * kMaxLiteralRuns);
  const uint8_t* entries = size ? ReadVirtual(table + xdk::kLiteralTableEntriesOffset, size) : nullptr;
  if (size && !entries) return "null";
  xdk::LiteralRun runs[kMaxLiteralRuns];
  const uint32_t n = xdk::ParseLiteralRuns(entries, size, runs, kMaxLiteralRuns);
  std::string s = "[";
  char buf[32];
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t dwords = std::min<uint32_t>(runs[i].dwords, kMaxLiteralDwords);
    const uint8_t* v = ReadPhysical(xdk::GpuAddress(base + runs[i].offset), 4 * dwords);
    if (!v) return "null";
    std::snprintf(buf, sizeof(buf), "%s{\"reg\":%u,\"v\":", i ? "," : "", runs[i].first_register);
    s += buf;
    s += BeFloats(v, dwords);
    s += "}";
  }
  return s + "]";
}

// The registers a shader object loads itself (its literal table, xdk_layout.h
// kShaderLiteral*): bit c of `regs` for register `bank_first` + c of the table
// (a pixel shader's table numbers pixel register c as 256 + c, a vertex
// shader's vertex register c as c). False when the table cannot be read or is
// larger than kMaxLiteralGuardRuns runs: no register can then be told from a
// literal.
constexpr uint32_t kMaxLiteralGuardRuns = 32;

bool ReadLiteralRegisters(const UcodeLayout& l, uint32_t obj, uint32_t variant, uint32_t bank_first,
                          uint64_t regs[4]) {
  regs[0] = regs[1] = regs[2] = regs[3] = 0;
  uint32_t table_off = 0, size = 0;
  const uint32_t header = obj + l.header_offset;
  if (!obj || !ReadVirtualBe32(header + xdk::kShaderLiteralTableField + 8 * variant, &table_off)) return false;
  if (table_off == 0) return true;  // no table: no literal
  const uint32_t table = header + table_off;
  if (!ReadVirtualBe32(table + xdk::kLiteralTableSizeOffset, &size)) return false;
  if (size == 0) return true;
  if (size > 8 * kMaxLiteralGuardRuns) return false;
  const uint8_t* entries = ReadVirtual(table + xdk::kLiteralTableEntriesOffset, size);
  if (!entries) return false;
  xdk::LiteralRun runs[kMaxLiteralGuardRuns];
  const uint32_t n = xdk::ParseLiteralRuns(entries, size, runs, kMaxLiteralGuardRuns);
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t first = runs[i].first_register, count = (uint32_t(runs[i].dwords) + 3) / 4;
    for (uint32_t r = first; r < first + count; ++r) {
      if (r >= bank_first && r - bank_first < 256) regs[(r - bank_first) / 64] |= 1ull << ((r - bank_first) % 64);
    }
  }
  return true;
}

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

// Whether a shader's microcode record says it uses flow constants (bool and
// loop constants; xdk_layout.h, kUcodeRecordFlowDword). `ok` false when the
// record cannot be read.
bool UsesFlowConstants(const UcodeLayout& l, uint32_t obj, uint32_t variant, bool* ok) {
  uint32_t rec_off = 0, flow = 0;
  const uint32_t header = obj + l.header_offset;
  *ok = ReadVirtualBe32(header + l.record_field + 8 * variant, &rec_off) &&
        ReadVirtualBe32(header + rec_off + 4 * xdk::kUcodeRecordFlowDword, &flow);
  return *ok && flow != 0;
}

void OnGpuLoadShaders(const LastArgs& a) {
  const uint32_t vs = a.r[1], ps = a.r[2];
  uint32_t flags = 0;
  t_gpu_ps = ps;
  t_gpu_vs = {};
  const bool discovery = g_discovery_active.load(std::memory_order_relaxed);
  if (!discovery) t_gpu_pbool.Forget();
  if (!vs || !ReadVirtualBe32(vs + xdk::kVsHeaderOffset, &flags)) {
    t_gpu_pbool.Forget();
    return;
  }
  t_gpu_vs.obj = vs;
  // 0x822219BC..0x822219C8: the variant flag applies only without a pixel
  // shader; 0x82221A10 forces variant 0 otherwise.
  t_gpu_vs.variant = (!ps && (flags & xdk::kVsVariantFlagMask)) ? 1 : 0;
  if (!discovery) return;
  // The flow constants are uploaded only when one of the two shaders uses them
  // (0x82221ADC `or. r26,r20,r4`); otherwise the GPU keeps its bool words.
  bool vs_ok = false, ps_ok = true;
  const bool uploads = UsesFlowConstants(kVsLayout, vs, t_gpu_vs.variant, &vs_ok) |
                       (ps != 0 && UsesFlowConstants(kPsLayout, ps, 0, &ps_ok));
  uint32_t word = 0;
  const bool word_read =
      uploads && ReadVirtualBe32(xdk::kEngineFlowConstantsAddress + xdk::kEnginePsBoolWordOffset, &word);
  t_gpu_pbool.OnShaderLoad(vs_ok && ps_ok, uploads, word_read, word);
}

// Whether a LoadAluConstants call of bank `bank` (r4) is handed to that bank's
// model: the vertex bank's while discovery is requested or running, the pixel
// bank's also in a frame with records. If not, the models the call may have
// written are told that an upload was skipped (byte stores; nothing of the
// guest is read and the hook is not timed) and false is returned.
bool FollowAluLoad(uint32_t bank) {
  if (g_discovery_active.load(std::memory_order_relaxed)) return true;
  if (bank != 1) t_vs_loads.Skip();
  if (bank == 0) return false;
  if (g_records_frame.load(std::memory_order_relaxed) && g_render.load(std::memory_order_relaxed)) return true;
  t_ps_loads.Skip();
  return false;
}

// LoadAluConstants (0x8227CFC8: r3 device, r4 bank, r5 first register, r6
// count, r7 constant buffer, r8 the buffer's first register; xdk_layout.h) has
// written its LOAD_ALU_CONSTANT packet, and the call is followed
// (FollowAluLoad): it is handed to its bank's model (t_ps_loads, t_vs_loads;
// light_block.h, MapAluLoad). A call that cannot be followed (the predicate
// word or the bin mask unread, arguments outside the bank, bins in the mask's
// high word) is a skipped upload for the models it may have written. While
// discovery is armed every call writes an "aluload" row with the registers as
// the buffer holds them and the device's command write pointer (the packet's
// last dword), for tools/xdk_sigmatch/pconst_check.py; "tiling" is whether
// the device's tiling flag was set at the call (0 also when it was unread),
// "bin_mask_hi" the bin mask's high word, "untabled" how many calls so far
// named a register outside the call sites' ranges for the first time (0 unless
// the static list of call sites is incomplete).
void OnAluLoad(const LastArgs& a) {
  const uint32_t device = a.r[0];
  AluLoadCall call;
  call.bank = a.r[1], call.start = a.r[2], call.count = a.r[3], call.object = a.r[4], call.first = a.r[5];
  call.state_read = device && ReadVirtualBe32(device + xdk::kDevicePredicateOffset, &call.predicate) &&
                    ReadVirtualBe32(device + xdk::kDeviceBinMaskLoOffset, &call.mask_lo) &&
                    ReadVirtualBe32(device + xdk::kDeviceBinMaskHiOffset, &call.mask_hi);
  const uint32_t bank = call.bank, start = call.start, count = call.count, object = call.object,
                 first = call.first;
  // Here the pixel model is being followed whatever the bank (FollowAluLoad):
  // the scene first, so a load at a scene's begin belongs to it.
  ObservePixelScene(device);
  const uint32_t untabled = t_ps_loads.untabled_loads() + t_vs_loads.untabled_loads();
  AluLoad load;
  if (!MapAluLoad(call, &load)) {
    if (bank != 0) t_ps_loads.Unfollowable(start, count);
    if (bank != 1) t_vs_loads.Unfollowable(start, count);
  } else if (load.pixel) {
    t_ps_loads.Load(load.start, load.count, load.object, load.first, load.predicated, load.mask);
  } else if (g_discovery_active.load(std::memory_order_relaxed)) {
    t_vs_loads.Load(load.start, load.count, load.object, load.first, load.predicated, load.mask);
  } else {
    t_vs_loads.Skip();
  }
  // A register outside the call sites' ranges: it is followed like a load
  // register from now on (the model added it); say so once.
  if (t_ps_loads.untabled_loads() + t_vs_loads.untabled_loads() != untabled &&
      !g_untabled_load_logged.exchange(true)) {
    REXSYS_WARN("[native] LoadAluConstants named registers outside its call-site table: bank {} c{} count {} "
                "(followed from now on; xdk_layout.h kAluLoadSites is incomplete)",
                bank, start, count);
  }
  if (!g_armed.load(std::memory_order_relaxed)) return;
  uint32_t data = 0, write = 0;
  const bool have_data = object && ReadVirtualBe32(object + 4 * xdk::kConstantBufferDataDword, &data);
  const bool have_write = device && ReadVirtualBe32(device + xdk::kDeviceCommandWriteOffset, &write);
  char buf[320];
  std::snprintf(buf, sizeof(buf),
                "{\"kind\":\"aluload\",\"frame\":%d,\"bank\":%u,\"start\":%u,\"count\":%u,\"obj\":\"0x%08X\","
                "\"first\":%u,\"data\":\"0x%08X\",\"pred\":%d,\"bin_mask\":\"0x%08X\",\"bin_mask_hi\":\"0x%08X\","
                "\"tiling\":%d,\"untabled\":%u",
                g_frames_written.load(std::memory_order_relaxed) + 1, bank, start, count, object, first,
                have_data ? data : 0, call.state_read ? int(call.predicate != 0) : -1, call.mask_lo, call.mask_hi,
                t_ps_loads.in_scene() ? 1 : 0, t_ps_loads.untabled_loads() + t_vs_loads.untabled_loads());
  std::string row = buf;
  if (have_write) {
    std::snprintf(buf, sizeof(buf), ",\"cmd\":\"0x%08X\"", write);
    row += buf;
  }
  // The registers as the buffer holds them now (bounded: a bank has 256).
  const uint32_t n = count <= ConstantLoads::kRegisters ? count : 0;
  if (have_data && n && first < ConstantLoads::kMaxBufferRegisters) {
    uint32_t bits[4 * ConstantLoads::kRegisters];
    bool have[ConstantLoads::kRegisters];
    for (uint32_t i = 0; i < n; ++i) {
      have[i] = ReadConstantBits(0, ConstantSource{ConstantSource::Kind::kMemory, object, first + i}, 0,
                                 &bits[4 * i]);
    }
    row += ",\"v\":\"";
    row += RegistersHex(bits, have, n);
    row += "\"";
  }
  row += "}\n";
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_file) std::fputs(row.c_str(), g_file);
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
  AlphaSpec alpha;                     // FindAlpha; kNone = no recipe
  const PsTerrainEntry* terrain = nullptr;  // FindPsTerrain: a terrain pixel shader (the entry is stable)
  const PsLightSpec* light = nullptr;       // FindPsLight: the shader's light family, null without one
  const FogShaderEntry* fog = nullptr;      // FindFogShader: the shader has the game's fog block, null without it
  uint32_t phys = 0;
  uint32_t bytes = 0;
  // ReadLiteralRegisters: the pixel registers the shader loads itself;
  // `literals_known` false when its table could not be read.
  uint64_t literal_regs[4] = {};
  bool literals_known = false;
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
  e.alpha = FindAlpha(u.hash);
  e.terrain = FindPsTerrain(u.hash);
  e.light = FindPsLight(u.hash);
  e.fog = FindFogShader(u.hash);
  e.phys = u.phys;
  e.bytes = u.bytes;
  e.literals_known = ReadLiteralRegisters(kPsLayout, obj, 0, 256, e.literal_regs);
  return &e;
}

// The GPU's pixel constant `reg` (0..255) at the draw this thread is capturing,
// as host floats (their bits are the guest's): the device's bank, or the
// constant buffer the last LoadAluConstants of the register named; a register
// loaded per tile is the first colour tile's. Proven against the GPU's register
// file (frame-map section 14). False, with `out` untouched, when the register
// cannot be read, and for a register the draw's pixel shader loads itself (a
// literal of its table, which no bank or buffer holds; also when the shader or
// its table cannot be read): nothing may then be drawn from it.
// Which source is decided by the table of the registers a load can name first
// (light_block.h, kBufferLoadRanges: pixel c0..c7 and c80..c115). Any other
// register is read from the bank, whatever uploads were followed: no buffer
// is ever its source. A load register is false while the uploads are not
// known: after a time in which they were not followed (no records and no
// discovery) until the first main scene followed from its begin, and then
// until an upload of it was seen (ConstantLoads). Not covered: a register that
// only another shader's literals wrote, which no shader of this draw reads;
// the bank's value is returned for it.
bool ReadPixelConstant(uint32_t reg, float out[4]) {
  if (reg >= ConstantLoads::kRegisters || !t_draw_device) return false;
  uint32_t vs_field = 0, ps_field = 0;
  if (!ReadVirtualBe32(t_draw_device + xdk::kVsDeviceFieldOffset, &vs_field) ||
      !ReadVirtualBe32(t_draw_device + xdk::kPsDeviceFieldOffset, &ps_field)) {
    return false;
  }
  // The pixel shader as ChoosePs picks it; none: a depth-only draw, no literal.
  if (const uint32_t ps_obj = vs_field ? ps_field : t_gpu_ps) {
    const PsInfo* ps = LookupPs(ps_obj);
    if (!ps || !ps->literals_known || ((ps->literal_regs[reg / 64] >> (reg % 64)) & 1)) return false;
  }
  uint32_t bits[4];
  if (!ReadConstantBits(PixelBank(t_draw_device), t_ps_loads.Find(reg), reg, bits)) return false;
  std::memcpy(out, bits, sizeof(bits));
  return true;
}

// Whether the light set's registers, as the draw this thread is capturing sees them, come from a
// constant buffer loaded since the draw's main scene began (five lookups in the load model, no
// guest read): the part of ReadLightBlock that costs nothing.
bool LightSetThisScene() {
  bool this_scene = true;
  for (uint32_t r : kLightSetRegisters) this_scene = this_scene && t_ps_loads.LoadedThisScene(r);
  return this_scene;
}

// The light set in force at the draw this thread is capturing, as the upload that decides its
// first register (c80: the sun's slot, which MakeLightBlock judges the set by): that upload's
// order number in this thread's load model, another for every load of the set. 0 unless the
// whole set comes from a buffer loaded in this scene (LightSetThisScene), where no block is read.
uint64_t LightSetUpload() {
  if (!LightSetThisScene()) return 0;
  uint64_t upload = 0;
  t_ps_loads.Find(kLightSetRegisters[0], &upload);
  return upload;
}

// The light block at the draw this thread is capturing (light_block.h), its
// registers read through ReadPixelConstant. Invalid (all zero) unless the
// light set's registers come from a constant buffer loaded since the main
// scene of this draw began: the bank's values there, and a buffer loaded in an
// earlier scene, are not this frame's light.
// Costly (eleven ReadPixelConstant calls): the capture makes it once per frame for the frame's
// block (RecordDraw), never per draw; discovery rows make it per row ("light").
LightBlock ReadLightBlock() {
  const bool this_scene = LightSetThisScene();
  if (!this_scene) return LightBlock{};
  float reg[ConstantLoads::kRegisters][4] = {};
  bool have[ConstantLoads::kRegisters] = {};
  for (uint32_t r : kLightBlockRegisters) have[r] = ReadPixelConstant(r, reg[r]);
  return MakeLightBlock(reg, have, this_scene);
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
// row. Bounded: kMaxTextures per capture, kMaxTextureBytes each. The extent is
// the tiled address upper bound / linear rows of the base level (row pitch by
// the SDK's base-level rule, render::BaseLevelLinear), also for k_16 and k_DXN,
// which the renderer does not decode but the terrain replay reads (heightmap,
// far colour tables, normal map: render::BaseLevelDumpBytes). Any other format
// the renderer does not decode gets kMaxUnsupportedBytes (the census needs to
// see it; such a dump is not the whole level).
// Fog discovery (frame-map section 16): a 1D texture is dumped too, its texels in one row
// (render::Fetch1DDumpBytes; a format without a texel size gets kMaxUnsupported1DTexelBytes a
// texel, at most kMaxUnsupportedBytes), and the textures the game rewrites in place
// (render::DumpKeyedByContent: every 1D texture, 2D targets of the colour and depth formats, and since
// post discovery, frame-map section 17, any 2D texture at a post buffer's address)
// are kept once per content and not once per address: the first content seen at (base, identity)
// under the usual name, every other one as <BASE>_<IDENTITY16>_<XXH3>.bin, each with a "texture"
// row that carries the frame and the content hash ("frame", "content"; "dim":1 for a 1D
// texture), written before the row of the draw that sampled it. Such a texture is read and hashed
// at every row that names it. Bounded apart from the others, so that a long capture's tables
// cannot use up the ordinary textures' files: kMaxContentTextures files, and kMaxContentTextureBytes
// for the 2D targets (a 1D table does not count against the bytes). Returns the content hash of such a texture as it is at this moment (also when that
// content was dumped before or its file could not be written), for the row's "tfc"; 0 for any
// other texture and when the bytes were not read.
constexpr uint32_t kMaxTextures = 768;
constexpr uint32_t kMaxTextureBytes = 8u << 20;
constexpr uint32_t kMaxUnsupportedBytes = 64u << 10;
constexpr uint32_t kMaxUnsupported1DTexelBytes = 16;
constexpr uint32_t kMaxContentTextures = 1024;
constexpr uint64_t kMaxContentTextureBytes = 512ull << 20;

uint64_t DumpTexture(const uint32_t fc[6]) {
  const bool by_content = render::DumpKeyedByContent(fc);
  if (g_tex_dir.empty() || (by_content ? g_tex_content_count >= kMaxContentTextures : g_tex_count >= kMaxTextures)) {
    return 0;
  }
  render::TextureFetch t;
  const bool is_1d = render::Is1DFetch(fc);
  const render::FetchError err = is_1d ? render::FetchError::kNot2D : render::DecodeTextureFetch(fc, &t);
  uint64_t bytes = 0;
  uint32_t base = fc[1] & 0xFFFFF000u;
  if (is_1d) {
    if (!base) return 0;
    bytes = render::Fetch1DDumpBytes(fc);
    if (!bytes) {
      bytes = std::min<uint64_t>(uint64_t(render::Fetch1DWidth(fc)) * kMaxUnsupported1DTexelBytes, kMaxUnsupportedBytes);
    }
    bytes = std::min<uint64_t>(bytes, kMaxTextureBytes);
  } else if (err == render::FetchError::kNone) {
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
    bytes = render::BaseLevelDumpBytes(fc);
    bytes = bytes ? std::min<uint64_t>(bytes, kMaxTextureBytes) : kMaxUnsupportedBytes;
  } else {
    return 0;
  }
  const uint64_t identity = render::TextureIdentity(fc);
  const uint8_t* src = nullptr;
  uint64_t content = 0;
  char name[64];
  if (by_content) {
    // The byte budget is the targets': a 1D table (at most kMaxTextureBytes, in practice 1.5 KB) is
    // bounded by the file count alone, so a long capture's targets cannot starve the tables.
    if (bytes == 0 || (!is_1d && g_tex_content_bytes + bytes > kMaxContentTextureBytes)) return 0;
    src = ReadPhysical(base, uint32_t(bytes));
    if (!src) return 0;
    content = XXH3_64bits(src, size_t(bytes));
    // Marked as dumped only once its file is written (below): a failed write is tried again at
    // the next row that names the content.
    if (g_tex_content_seen.count({base, identity, content})) return content;
    if (!g_tex_seen.count({base, identity})) {
      std::snprintf(name, sizeof(name), "%08X_%016llX.bin", base, static_cast<unsigned long long>(identity));
    } else {
      std::snprintf(name, sizeof(name), "%08X_%016llX_%016llX.bin", base, static_cast<unsigned long long>(identity),
                    static_cast<unsigned long long>(content));
    }
  } else {
    if (!g_tex_seen.insert({base, identity}).second || bytes == 0) return 0;
    src = ReadPhysical(base, uint32_t(bytes));
    if (!src) return 0;
    std::snprintf(name, sizeof(name), "%08X_%016llX.bin", base, static_cast<unsigned long long>(identity));
  }
  std::error_code ec;
  std::filesystem::create_directories(g_tex_dir, ec);
  std::FILE* f = std::fopen((g_tex_dir / name).string().c_str(), "wb");
  if (!f) return content;
  const bool ok = std::fwrite(src, 1, size_t(bytes), f) == size_t(bytes);
  std::fclose(f);
  if (!ok) return content;
  char buf[224];
  std::snprintf(buf, sizeof(buf), "{\"kind\":\"texture\",\"file\":\"%s/%s\",\"bytes\":%llu",
                g_tex_dir.filename().string().c_str(), name, static_cast<unsigned long long>(bytes));
  std::string row = buf;
  if (by_content) {
    g_tex_content_seen.insert({base, identity, content});
    g_tex_seen.insert({base, identity});
    ++g_tex_content_count;
    if (!is_1d) g_tex_content_bytes += bytes;
    std::snprintf(buf, sizeof(buf), ",\"frame\":%d,\"content\":\"0x%016llX\"%s",
                  g_frames_written.load(std::memory_order_relaxed) + 1, static_cast<unsigned long long>(content),
                  is_1d ? ",\"dim\":1" : "");
    row += buf;
  } else {
    ++g_tex_count;
  }
  row += ",\"fc\":";
  row += HexDwords(fc, 6);
  row += "}\n";
  std::fputs(row.c_str(), g_file);
  return content;
}

// A member of a row's "tfc" (the content hash of a slot's texture where DumpTexture gives one:
// which dump of a texture kept by content the draw sampled): "<slot>":"0x<hash>".
void AppendTextureContent(std::string& contents, uint32_t slot, uint64_t content) {
  if (!content) return;
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%s\"%u\":\"0x%016llX\"", contents.empty() ? "" : ",", slot,
                static_cast<unsigned long long>(content));
  contents += buf;
}

// Discovery evidence: the texture fetch constants (host-order dwords, read like
// the terrain heightmap fetch) of every slot the pixel shader samples, and a
// dump of each distinct texture. Writes "tf":{"<slot>":[6 dwords] | null}.
// Fog discovery (frame-map section 16): the slots the vertex shader samples are
// in "tf" as well, after the pixel shader's (the trees' and billboards' fog
// tables tf16 / tf17), and listed by themselves in "vs_tex_slots" (left out
// when the vertex shader's microcode cannot be read). "tfc" names, for a slot
// whose texture is kept by content, the content the draw sampled (DumpTexture).
void AppendTextureFetches(std::string& row, uint32_t device, const DeviceSnapshot& dev) {
  uint32_t obj = 0;
  std::vector<uint32_t> slots;
  if (ChoosePs(dev, &obj)) {
    if (const PsInfo* ps = LookupPs(obj)) slots = ps->tex_slots;  // copy: not held across calls
  }
  char buf[32];
  VsChoice vc;
  if (ChooseVs(dev, &vc)) {
    // The object's own microcode, also on the immediate path: the patched copy the GPU runs then
    // differs from it only in vertex fetch dwords (WriteDrawRow, "template_diff"), so both name
    // the same texture slots, and the immediate load's state is not touched from here.
    const UcodeRef u = ReadVsUcode(vc.obj, vc.variant);
    if (u.ok) {
      const std::vector<uint32_t> vs_slots = TextureFetchSlots(u.dwords.data(), u.dwords.size());
      row += ",\"vs_tex_slots\":[";
      for (size_t i = 0; i < vs_slots.size(); ++i) {
        std::snprintf(buf, sizeof(buf), "%s%u", i ? "," : "", vs_slots[i]);
        row += buf;
        if (std::find(slots.begin(), slots.end(), vs_slots[i]) == slots.end()) slots.push_back(vs_slots[i]);
      }
      row += "]";
    }
  }
  row += ",\"tf\":{";
  bool first = true;
  std::string contents;
  for (uint32_t slot : slots) {
    if (slot >= 32) continue;
    const uint32_t at =
        device + xdk::kDeviceVertexFetchOffset + xdk::kDeviceTextureFetchStride * slot;
    const uint8_t* p = device ? ReadVirtual(at, 24) : nullptr;
    uint32_t v[6] = {};
    if (p) {
      for (int i = 0; i < 6; ++i) v[i] = LoadBe32(p + 4 * i);
      AppendTextureContent(contents, slot, DumpTexture(v));
    }
    std::snprintf(buf, sizeof(buf), "%s\"%u\":", first ? "" : ",", slot);
    row += buf;
    row += p ? HexDwords(v, 6) : "null";
    first = false;
  }
  row += "},\"tfc\":{";
  row += contents;
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
// The "fetches" member of a decoded row: the shader's vertex fetches in DecodeVertexFetches order.
void AppendFetches(std::string& row, const std::vector<VertexFetch>& fetches) {
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
  row += "]";
}

void AppendGeometry(std::string& row, bool indexed, const LastArgs& args, uint32_t device,
                    const DeviceSnapshot& dev, const std::vector<VertexFetch>& fetches) {
  char buf[256];
  AppendFetches(row, fetches);
  row += ",\"streams\":[";
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

// Discovery evidence (frame-map section 13, "Render state"): the render-state
// registers from the device's register shadows as the draw builder left them
// ("rs": register -> dword for the registers of kRowStateRegisters, a float
// register as its bits; a register whose shadow cannot be read is left out;
// "bin_mask_lo" / "bin_mask_hi" are the device's explicit bin mask), and the
// device's command write pointer after the draw ("cmd": the last dword the
// builder wrote, read at `cmd_offset`: BeginVertices keeps its own), which
// tools/xdk_sigmatch/render_state_check.py pairs with the address of the
// DRAW_INDX packet the GPU executes.
void AppendRenderState(std::string& row, uint32_t device,
                       uint32_t cmd_offset = xdk::kDeviceCommandWriteOffset) {
  char buf[40];
  row += ",\"rs\":{";
  const std::string words =
      RegisterWords(kRowStateRegisters, std::size(kRowStateRegisters), [device](uint32_t reg, uint32_t* word) {
        const uint32_t at = xdk::DeviceRegShadowOffset(reg);
        return device && at && ReadVirtualBe32(device + at, word);
      });
  row += words;
  const bool first = words.empty();
  // The explicit bin mask (SetPredication): which passes the draw executes in.
  uint32_t lo = 0, hi = 0;
  if (device && ReadVirtualBe32(device + xdk::kDeviceBinMaskLoOffset, &lo) &&
      ReadVirtualBe32(device + xdk::kDeviceBinMaskHiOffset, &hi)) {
    std::snprintf(buf, sizeof(buf), "%s\"bin_mask_lo\":\"0x%08X\"", first ? "" : ",", lo);
    row += buf;
    std::snprintf(buf, sizeof(buf), ",\"bin_mask_hi\":\"0x%08X\"", hi);
    row += buf;
  }
  row += "}";
  uint32_t write = 0;
  if (device && ReadVirtualBe32(device + cmd_offset, &write)) {
    std::snprintf(buf, sizeof(buf), ",\"cmd\":\"0x%08X\"", write);
    row += buf;
  }
  // RB_SURFACE_INFO as the device holds it: pitch in bits 0-13, the surface's
  // multisampling in bits 16-17 (0 none, 1 two samples, 2 four).
  uint32_t surface = 0;
  if (device && ReadVirtualBe32(device + xdk::kDeviceSurfaceInfoOffset, &surface)) {
    std::snprintf(buf, sizeof(buf), ",\"surface_info\":\"0x%08X\"", surface);
    row += buf;
  }
}

// Discovery evidence (frame-map section 13, "Alpha and missing shaders"): pixel
// constants c0..c31 as host floats ("pconst", 128 values; left out when the
// bank cannot be read), from the bank ReadPsBankRegisters reads for the draw's
// material. They hold the constants the alpha recipes of ps-albedo.json
// multiply by (c15.w, c26.w) and the UV scale (c8).
void AppendPixelConstants(std::string& row, uint32_t device) {
  const uint32_t bank_ptr = g_ps_bank_ptr.load(std::memory_order_relaxed);
  const uint8_t* p =
      device ? ReadVirtual(bank_ptr ? bank_ptr : device + xdk::kDevicePsConstantsOffset, 32 * 16) : nullptr;
  if (!p) return;
  row += ",\"pconst\":";
  row += BeFloats(p, 128);
}

// Discovery evidence (frame-map section 14, "Pixel constants"), for
// tools/xdk_sigmatch/pconst_check.py, which compares it with the GPU's register
// file. For one constant bank, under keys that begin with `prefix` ("p" pixel,
// "v" vertex); register values are their bits in hex (RegistersHex):
//   "<p>full"       all 256 registers as `read` gives them (the reader);
//   "<p>tiles"      the loads issued under a bin mask that leaves bins out,
//                   oldest first, each as the runs of registers it still
//                   decides: {"mask", "start", "v"}; none while the model is
//                   not whole (ConstantLoads::binned_count), where "<p>full"
//                   has dashes for every register a load can name;
//   "<p>bank_diff"  the registers whose value in the device's bank at `bank`
//                   is another one (or that the reader did not read), with the
//                   bank's value; left out when the bank cannot be read.
// The caller is the thread that issued the draw (the loads are per thread).
template <typename Read>
void AppendBankRegisters(std::string& row, const char* prefix, const ConstantLoads& loads, uint32_t bank,
                         Read read) {
  constexpr uint32_t kRegs = ConstantLoads::kRegisters;
  uint32_t bits[4 * kRegs] = {};
  bool have[kRegs];
  for (uint32_t r = 0; r < kRegs; ++r) have[r] = read(r, &bits[4 * r]);
  char buf[64];
  std::snprintf(buf, sizeof(buf), ",\"%sfull\":\"", prefix);
  row += buf;
  row += RegistersHex(bits, have, kRegs);
  std::snprintf(buf, sizeof(buf), "\",\"%stiles\":[", prefix);
  row += buf;
  bool first = true;
  for (uint32_t i = 0; i < loads.binned_count(); ++i) {
    const ConstantLoads::Binned& b = loads.binned(i);
    for (uint32_t r = b.start; r < b.start + b.count;) {
      if (!loads.InForce(b, r)) {
        ++r;
        continue;
      }
      uint32_t run[4 * kRegs];
      bool run_have[kRegs];
      uint32_t n = 0;
      for (; r < b.start + b.count && loads.InForce(b, r); ++r, ++n) {
        run_have[n] = ReadConstantBits(
            bank, ConstantSource{ConstantSource::Kind::kMemory, b.object, b.first + (r - b.start)}, r,
            &run[4 * n]);
      }
      std::snprintf(buf, sizeof(buf), "%s{\"mask\":\"0x%08X\",\"start\":%u,\"v\":\"", first ? "" : ",", b.mask,
                    r - n);
      row += buf;
      row += RegistersHex(run, run_have, n);
      row += "\"}";
      first = false;
    }
  }
  row += "]";
  const uint8_t* shadow = bank ? ReadVirtual(bank, 16 * kRegs) : nullptr;
  if (shadow) {
    std::snprintf(buf, sizeof(buf), ",\"%sbank_diff\":{", prefix);
    row += buf;
    first = true;
    for (uint32_t r = 0; r < kRegs; ++r) {
      uint32_t v[4];
      for (int i = 0; i < 4; ++i) v[i] = LoadBe32(shadow + 16 * r + 4 * i);
      if (have[r] && std::memcmp(v, &bits[4 * r], sizeof(v)) == 0) continue;
      std::snprintf(buf, sizeof(buf), "%s\"%u\":\"", first ? "" : ",", r);
      row += buf;
      row += RegistersHex(v, nullptr, 1);
      row += "\"";
      first = false;
    }
    row += "}";
  }
}

// The pixel bank's rows ("pfull" is what ReadPixelConstant gives); "light":
// whether ReadLightBlock gives a valid block at this draw; and "pbool_load":
// the pixel bool word as its last writer on this thread left it (t_gpu_pbool),
// when that is known.
void AppendPixelRegisters(std::string& row, uint32_t device) {
  AppendBankRegisters(row, "p", t_ps_loads, PixelBank(device), [](uint32_t r, uint32_t* bits) {
    float f[4];
    if (!ReadPixelConstant(r, f)) return false;
    std::memcpy(bits, f, sizeof(f));
    return true;
  });
  row += ReadLightBlock().valid ? ",\"light\":1" : ",\"light\":0";
  if (t_gpu_pbool.known) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), ",\"pbool_load\":\"0x%08X\"", t_gpu_pbool.word);
    row += buf;
  }
}

// The vertex bank's rows ("vfull", "vtiles", "vbank_diff"): the registers as
// the same model gives them for the vertex bank, a register the vertex shader
// `vs` (variant `variant`) loads itself left out, as for the pixel bank; no
// register is read when the shader's literal table cannot be. Nothing is
// written when the vertex bank's uploads were not followed from the start
// (ConstantLoads::ever_skipped). Discovery evidence only: no record reads
// vertex constants through it.
void AppendVertexRegisters(std::string& row, uint32_t device, uint32_t vs, uint32_t variant) {
  if (t_vs_loads.ever_skipped()) return;
  uint64_t literal[4] = {};
  const bool literals_known = vs && ReadLiteralRegisters(kVsLayout, vs, variant, 0, literal);
  const uint32_t bank = VertexBank(device);
  AppendBankRegisters(row, "v", t_vs_loads, bank, [&](uint32_t r, uint32_t* bits) {
    if (!literals_known || ((literal[r / 64] >> (r % 64)) & 1)) return false;
    return ReadConstantBits(bank, t_vs_loads.Find(r), r, bits);
  });
}

// Discovery evidence for the terrain shaders (frame-map section 13, "Terrain"):
// the pixel constants above c31 that they read, from the same bank, as
// "pc":{"cN":[4 floats]} (a register that cannot be read is left out). c45 is
// the slope blend of the layer shaders paired with C30A; the others are
// lighting, sun and fog terms the unlit rule drops, recorded so that what they
// hold is known.
void AppendTerrainPixelConstants(std::string& row, uint32_t device) {
  const uint32_t bank_ptr = g_ps_bank_ptr.load(std::memory_order_relaxed);
  const uint32_t bank = bank_ptr ? bank_ptr : device + xdk::kDevicePsConstantsOffset;
  row += ",\"pc\":{";
  bool first = true;
  char buf[24];
  for (uint32_t c : {45u, 66u, 67u, 68u, 80u, 81u, 87u, 88u, 109u, 112u}) {
    const uint8_t* p = device ? ReadVirtual(bank + 16 * c, 16) : nullptr;
    if (!p) continue;
    std::snprintf(buf, sizeof(buf), "%s\"c%u\":", first ? "" : ",", c);
    row += buf;
    row += BeFloats(p, 4);
    first = false;
  }
  row += "}";
}

// The nine words of a draw's render state (render_state.h) from the device's
// register shadows, as AppendRenderState reads them, and the low word of its bin
// mask. `read` stays false when any of them cannot be read. RB_SURFACE_INFO
// (the sample count alpha-to-mask works under) is read apart from them and only
// for a draw with alpha-to-mask on (NeedsSurfaceInfo): a word that was not or
// cannot be read stays 0, no multisampling, and makes nothing invalid.
void ReadStateWords(uint32_t device, StateWords* w) {
  *w = StateWords{};
  if (!device) return;
  auto reg = [&](uint32_t r, uint32_t* out) {
    return ReadVirtualBe32(device + xdk::DeviceRegShadowOffset(r), out);
  };
  w->read = reg(xdk::kRegColorControl, &w->color_control) && reg(xdk::kRegAlphaRef, &w->alpha_ref) &&
            reg(xdk::kRegBlendControl0, &w->blend_control) && reg(xdk::kRegColorMask, &w->color_mask) &&
            reg(xdk::kRegDepthControl, &w->depth_control) && reg(xdk::kRegSuScModeCntl, &w->su_sc_mode) &&
            reg(xdk::kRegVportZScale, &w->vport_z_scale) && reg(xdk::kRegVteCntl, &w->vte_cntl) &&
            ReadVirtualBe32(device + xdk::kDeviceBinMaskLoOffset, &w->bin_mask_lo);
  if (!NeedsSurfaceInfo(*w) || !reg(xdk::kRegSurfaceInfo, &w->surface_info)) w->surface_info = 0;
}

// Discovery evidence, "lin": the draw's light inputs as the records get them (FillLightInputs).
// Defined with the records' light inputs, below.
void AppendLightInputs(std::string& row, uint32_t device, const DeviceSnapshot& dev, const VsChoice* vc, bool indexed,
                       int64_t max_vertex);

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
  // Vertex bool constants b0..b31 from the device shadow (frame-map section
  // 12, "The cexec b0 block"): bit 0 of "vbool" is b0.
  if (const uint8_t* bools = ReadVirtual(device + xdk::kDeviceBoolConstantsOffset, 4)) {
    std::snprintf(buf, sizeof(buf), ",\"vbool\":\"0x%08X\"", LoadBe32(bools));
    row += buf;
  }
  // Pixel bool constants b128..b159 from the same shadow (bit 0 of "pbool" is
  // b128, which the prop shaders' colour division is under; frame-map section
  // 13, "Alpha and missing shaders").
  if (const uint8_t* bools = ReadVirtual(device + xdk::kDevicePsBoolConstantsOffset, 4)) {
    std::snprintf(buf, sizeof(buf), ",\"pbool\":\"0x%08X\"", LoadBe32(bools));
    row += buf;
  }
  AppendRenderState(row, device);
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
  AppendPixelConstants(row, device);
  AppendPixelRegisters(row, device);
  // The pixel shader's literal constants, as tess rows carry them: registers
  // the shader loads itself, which no constant reader covers (frame-map
  // section 14, "Pixel constants").
  {
    uint32_t ps_obj = 0;
    row += ",\"ps_lit\":";
    row += ChoosePs(dev, &ps_obj) ? LiteralsJson(kPsLayout, ps_obj, 0) : "null";
  }

  // Vertex shader and its microcode.
  VsChoice vc;
  const bool have_vs = ChooseVs(dev, &vc);
  // The light inputs (also without a vertex shader: the row then says why there are none). The
  // largest vertex the draw reads: its largest index plus the base vertex, or its last vertex.
  AppendLightInputs(row, device, dev, have_vs ? &vc : nullptr, indexed,
                    indexed ? (scanned && scan.max_index >= 0 ? scan.max_index + int64_t(int32_t(args.r[2])) : -1)
                            : (args.r[3] ? int64_t(args.r[2]) + args.r[3] - 1 : -1));
  if (!have_vs) return finish("no_vs");
  AppendVertexRegisters(row, device, vc.obj, vc.variant);
  row += ",\"vs_lit\":";
  row += LiteralsJson(kVsLayout, vc.obj, vc.variant);
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

// Caller holds g_mutex. One decoded "verts" row (frame-map section 15, "Mask
// draws") for a BeginVertices draw: what the two shadow-mask rectangles and the
// other full-screen passes are drawn with. Discovery only; nothing is recorded
// from these draws. It carries the render state ("rs", with "cmd" from the
// builder's own write pointer and "surface_info"), the two bool words, the pixel
// shader with its texture fetch constants ("tf", each 2D texture dumped), the
// pixel constants ("pconst", "pfull" and the rows around it), the pixel shader's
// literals ("ps_lit"), the vertex shader ("vs": object, source, hash) with c0 of
// the vertex bank ("consts", which render_state_check.py confirms a pair by) and
// where the builder put the vertices ("storage", "storage_dwords": the caller
// fills them after the builder returns, so their values are not in the row).
void WriteVertsRow(uint32_t id, const LastArgs& args, uint32_t device, const DeviceSnapshot& dev,
                   bool in_scene) {
  char buf[256];
  std::string row = VertsRowHead(g_frames_written.load(std::memory_order_relaxed) + 1, HookName(id), &args.r[1],
                                 device, in_scene);
  if (const uint8_t* bools = ReadVirtual(device + xdk::kDeviceBoolConstantsOffset, 4)) {
    std::snprintf(buf, sizeof(buf), ",\"vbool\":\"0x%08X\"", LoadBe32(bools));
    row += buf;
  }
  if (const uint8_t* bools = ReadVirtual(device + xdk::kDevicePsBoolConstantsOffset, 4)) {
    std::snprintf(buf, sizeof(buf), ",\"pbool\":\"0x%08X\"", LoadBe32(bools));
    row += buf;
  }
  AppendRenderState(row, device, xdk::kDeviceBeginVerticesWriteOffset);
  uint32_t storage = 0, dwords = 0;
  if (ReadVirtualBe32(device + xdk::kDeviceBeginVerticesStorageOffset, &storage) &&
      ReadVirtualBe32(device + xdk::kDeviceBeginVerticesDwordsOffset, &dwords)) {
    std::snprintf(buf, sizeof(buf), ",\"storage\":\"0x%08X\",\"storage_dwords\":%u", storage, dwords);
    row += buf;
  }
  AppendPs(row, dev);
  AppendTextureFetches(row, device, dev);
  AppendPixelConstants(row, device);
  AppendPixelRegisters(row, device);
  {
    uint32_t ps_obj = 0;
    row += ",\"ps_lit\":";
    row += ChoosePs(dev, &ps_obj) ? LiteralsJson(kPsLayout, ps_obj, 0) : "null";
  }
  VsChoice vc;
  if (ChooseVs(dev, &vc)) {
    const UcodeRef u = vc.immediate ? UcodeRef{} : ReadVsUcode(vc.obj, vc.variant);
    const uint64_t hash = vc.immediate ? (t_vs_load.usable ? t_vs_load.hash : 0) : u.hash;
    std::snprintf(buf, sizeof(buf),
                  ",\"vs\":{\"obj\":\"0x%08X\",\"source\":\"%s\",\"variant\":%u,\"hash\":\"0x%016llX\"}", vc.obj,
                  vc.immediate ? "immediate" : (vc.gpu_owned ? "gpu_load" : "object"), vc.variant,
                  static_cast<unsigned long long>(hash));
    row += buf;
  }
  const uint32_t bank_ptr =
      g_state.vs_bank_ptr ? g_state.vs_bank_ptr : device + xdk::kDeviceVsConstantsOffset;
  if (const uint8_t* p = ReadVirtual(bank_ptr, 16)) {
    row += ",\"consts\":{\"c0\":";
    row += BeFloats(p, 4);
    row += "}";
  }
  row += "}\n";
  std::fputs(row.c_str(), g_file);
}

// Discovery only: the storage of the last "indx" row this thread wrote, to be read once the game
// has filled it (it does that after the builder returns: xdk_layout.h). Written out as an
// "indxdata" row when this thread enters its next hooked draw.
struct PendingIndexed {
  bool valid = false;
  uint32_t capture = 0;  // g_capture_serial of the capture the row is in
  int frame = 0;
  uint32_t row = 0;  // the "indx" row's number ("n")
  uint32_t vertex_storage = 0, vertex_bytes = 0, stride = 0;
  uint32_t index_storage = 0, indices = 0;
  bool index32 = false;
};
thread_local PendingIndexed t_pending_indexed;
uint32_t g_indx_rows = 0;  // "indx" rows of this process, under g_mutex: joins a row and its data
uint32_t g_capture_serial = 0;  // captures opened by this process (OpenLog), under g_mutex
constexpr uint32_t kMaxIndxVertexBytes = 4096;

// Caller holds g_mutex. One decoded "indx" row (frame-map section 16, "Discovery") for a draw of
// the indexed immediate builder 0x82217EE8 (xdk_layout.h): the sky's cube and the lake among
// them. Discovery only; nothing is recorded from these draws. It carries what a "verts" row
// carries (render state with "cmd" from the builder's own write pointer, the bool words, the
// pixel shader with "tf" of both shaders' slots, "pconst", "pfull", "ps_lit", the vertex shader),
// and besides: the vertex registers ("vfull", when the vertex uploads were followed), the
// vertex shader's literals ("vs_lit") and vertex fetches ("fetches": the layout of the storage),
// the whole vertex bank as host floats ("bank", as draw rows have it) and the storage the
// builder reserved ("vstorage", "vdwords", "istorage", "idwords" from the device, "n" the row's
// number). The storage is empty at this moment; "storage_ok" says whether the device's index
// storage is the pointer the builder returned (false: the reservation failed or the fields are
// another draw's, and no data row follows).
void WriteIndxRow(uint32_t id, const LastArgs& args, uint32_t device, const DeviceSnapshot& dev,
                  bool in_scene) {
  char buf[256];
  const int frame = g_frames_written.load(std::memory_order_relaxed) + 1;
  std::string row = IndxRowHead(frame, HookName(id), &args.r[1], device, in_scene);
  if (const uint8_t* bools = ReadVirtual(device + xdk::kDeviceBoolConstantsOffset, 4)) {
    std::snprintf(buf, sizeof(buf), ",\"vbool\":\"0x%08X\"", LoadBe32(bools));
    row += buf;
  }
  if (const uint8_t* bools = ReadVirtual(device + xdk::kDevicePsBoolConstantsOffset, 4)) {
    std::snprintf(buf, sizeof(buf), ",\"pbool\":\"0x%08X\"", LoadBe32(bools));
    row += buf;
  }
  AppendRenderState(row, device, xdk::kDeviceBeginVerticesWriteOffset);
  const uint32_t n = ++g_indx_rows;
  uint32_t vstorage = 0, vdwords = 0, istorage = 0, idwords = 0, returned = 0;
  const bool fields = ReadVirtualBe32(device + xdk::kDeviceBeginVerticesStorageOffset, &vstorage) &&
                      ReadVirtualBe32(device + xdk::kDeviceBeginVerticesDwordsOffset, &vdwords) &&
                      ReadVirtualBe32(device + xdk::kDeviceIndexedIndexStorageOffset, &istorage) &&
                      ReadVirtualBe32(device + xdk::kDeviceIndexedIndexDwordsOffset, &idwords);
  // r10 is where the builder returned the index storage: the device's fields are this draw's
  // only when they hold that pointer and the sizes the arguments give.
  const bool index32 = (args.r[5] & 4) != 0;
  const bool storage_ok = fields && vstorage != 0 && istorage != 0 && ReadVirtualBe32(args.r[7], &returned) &&
                          returned == istorage && vdwords == IndxVertexDwords(args.r[3], args.r[6]) &&
                          idwords == IndxIndexDwords(args.r[4], index32);
  if (fields) {
    std::snprintf(buf, sizeof(buf),
                  ",\"n\":%u,\"vstorage\":\"0x%08X\",\"vdwords\":%u,\"istorage\":\"0x%08X\",\"idwords\":%u,"
                  "\"storage_ok\":%s",
                  n, vstorage, vdwords, istorage, idwords, storage_ok ? "true" : "false");
  } else {
    std::snprintf(buf, sizeof(buf), ",\"n\":%u,\"storage_ok\":false", n);
  }
  row += buf;
  PendingIndexed& pending = t_pending_indexed;
  pending = PendingIndexed{};
  if (storage_ok) {
    pending.valid = true;
    pending.capture = g_capture_serial;
    pending.frame = frame;
    pending.row = n;
    pending.vertex_storage = vstorage;
    pending.vertex_bytes = uint32_t(std::min<uint64_t>(uint64_t(vdwords) * 4, kMaxIndxVertexBytes));
    pending.stride = args.r[6];
    pending.index_storage = istorage;
    pending.indices = std::min(args.r[4], kMaxGeoIndices);
    pending.index32 = index32;
  }
  AppendPs(row, dev);
  AppendTextureFetches(row, device, dev);
  AppendPixelConstants(row, device);
  AppendPixelRegisters(row, device);
  {
    uint32_t ps_obj = 0;
    row += ",\"ps_lit\":";
    row += ChoosePs(dev, &ps_obj) ? LiteralsJson(kPsLayout, ps_obj, 0) : "null";
  }
  VsChoice vc;
  if (ChooseVs(dev, &vc)) {
    // The object's microcode, also for an immediate load (AppendTextureFetches): its vertex
    // fetches name the layout; the patched copy's hash is the immediate load's.
    const UcodeRef u = ReadVsUcode(vc.obj, vc.variant);
    const uint64_t hash = vc.immediate ? (t_vs_load.usable ? t_vs_load.hash : 0) : u.hash;
    std::snprintf(buf, sizeof(buf),
                  ",\"vs\":{\"obj\":\"0x%08X\",\"source\":\"%s\",\"variant\":%u,\"hash\":\"0x%016llX\","
                  "\"object_hash\":\"0x%016llX\"}",
                  vc.obj, vc.immediate ? "immediate" : (vc.gpu_owned ? "gpu_load" : "object"), vc.variant,
                  static_cast<unsigned long long>(hash), static_cast<unsigned long long>(u.ok ? u.hash : 0));
    row += buf;
    AppendVertexRegisters(row, device, vc.obj, vc.variant);
    row += ",\"vs_lit\":";
    row += LiteralsJson(kVsLayout, vc.obj, vc.variant);
    if (u.ok) AppendFetches(row, DecodeVertexFetches(u.dwords.data(), u.dwords.size()));
  }
  const uint32_t bank_ptr =
      g_state.vs_bank_ptr ? g_state.vs_bank_ptr : device + xdk::kDeviceVsConstantsOffset;
  if (const uint8_t* bank = ReadVirtual(bank_ptr, 256 * 16)) {
    row += ",\"bank\":";
    row += BeFloats(bank, 1024);
  }
  row += "}\n";
  std::fputs(row.c_str(), g_file);
}

// Discovery only. The "indxdata" row of this thread's last "indx" row, if one is pending: the
// storage as the game filled it after the builder returned. "n" and "frame" are the "indx"
// row's; "verts" the first bytes of the vertex storage as hex in guest order (at most
// kMaxIndxVertexBytes; "vbytes" how many, "stride" the vertex size), "idx" the first indices (at
// most kMaxGeoIndices) as the guest's big-endian words, base vertex not added. A storage that
// cannot be read leaves its member out. The bytes are read when this thread enters its next
// hooked draw: that the game has filled them by then, and that nothing has reused the storage, is
// what the reader checks on the values (a cube's corners, indices below the vertex count).
void FlushPendingIndexed() {
  PendingIndexed pending = t_pending_indexed;
  t_pending_indexed = PendingIndexed{};
  if (!pending.valid) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_file || pending.capture != g_capture_serial) return;  // the row's capture is over
  char buf[160];
  std::snprintf(buf, sizeof(buf), "{\"kind\":\"indxdata\",\"frame\":%d,\"n\":%u,\"stride\":%u", pending.frame,
                pending.row, pending.stride);
  std::string row = buf;
  if (const uint8_t* v = pending.vertex_bytes ? ReadVirtual(pending.vertex_storage, pending.vertex_bytes) : nullptr) {
    std::snprintf(buf, sizeof(buf), ",\"vbytes\":%u,\"verts\":\"", pending.vertex_bytes);
    row += buf;
    row += HexBytes(v, pending.vertex_bytes);
    row += "\"";
  }
  const uint32_t isize = pending.index32 ? 4 : 2;
  if (const uint8_t* p = pending.indices ? ReadVirtual(pending.index_storage, pending.indices * isize) : nullptr) {
    row += ",\"idx\":[";
    for (uint32_t i = 0; i < pending.indices; ++i) {
      const uint32_t index = pending.index32 ? LoadBe32(p + 4 * i) : (uint32_t(p[2 * i]) << 8 | p[2 * i + 1]);
      std::snprintf(buf, sizeof(buf), "%s%u", i ? "," : "", index);
      row += buf;
    }
    row += "]";
  }
  row += "}\n";
  std::fputs(row.c_str(), g_file);
}

// Caller holds g_mutex. One "tess" row (frame-map section 9) for the
// tessellated-patch builders DrawIndx:8221C9C8 (r5 first patch, r6 patch
// count, auto-indexed quad patches) and DrawIndx:82207C30 (adaptive: r7
// patches, r7 * 4 tessellation factors from the bound index buffer at r6): the
// vertex shader, the constants the terrain shaders read and texture fetch
// constants 16-19 and every slot the vertex and pixel shaders sample from the device shadow (each 2D
// texture also dumped, discovery only), then what a terrain material needs besides (frame-map
// section 13, "Terrain"): the pixel constants ("pconst" c0..c31 as draw rows have them, "pc" the
// higher registers the terrain shaders read), the pixel bool word ("pbool") and the literal
// constants of the two shaders ("vs_lit", "ps_lit").
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
  AppendRenderState(row, device);
  VsChoice vc;
  const bool vc_ok = ChooseVs(dev, &vc);
  if (vc_ok) {
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
  // c4..c6 (the world matrix of the layers), c14 (the base map scale) and c19 came with the terrain
  // material: rows written before it have none of them.
  for (uint32_t c : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 8u, 11u, 14u, 19u, 46u, 47u, 72u, 113u, 114u, 115u, 252u,
                     253u, 254u, 255u}) {
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
  // Texture fetch constants: 16-19 always (the terrain vertex shaders' heightmap and weight map; tf18 is the
  // k_DXT3A layer-weight and hole map), plus every slot the chosen vertex shader's microcode and the pixel
  // shader sample (the terrain pixel shaders' colour textures). Each 2D texture is dumped once through
  // DumpTexture (discovery only: it does nothing without the texture folder; bounded by kMaxTextures).
  std::vector<uint32_t> slots = {16, 17, 18, 19};
  if (vc_ok && !vc.immediate) {
    const UcodeRef u = ReadVsUcode(vc.obj, vc.variant);
    for (uint32_t slot : TextureFetchSlots(u.dwords.data(), u.dwords.size())) slots.push_back(slot);
  }
  uint32_t ps_obj = 0;
  if (ChoosePs(dev, &ps_obj)) {
    if (const PsInfo* ps = LookupPs(ps_obj)) {
      for (uint32_t slot : ps->tex_slots) slots.push_back(slot);
    }
  }
  std::sort(slots.begin(), slots.end());
  slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
  row += "},\"tf\":{";
  first = true;
  std::string contents;  // "tfc", as AppendTextureFetches writes it
  for (uint32_t t : slots) {
    if (t >= 32) continue;
    const uint32_t at =
        device + xdk::kDeviceVertexFetchOffset + xdk::kDeviceTextureFetchStride * t;
    const uint8_t* p = device ? ReadVirtual(at, 24) : nullptr;
    uint32_t v[6] = {};
    if (p) {
      for (int i = 0; i < 6; ++i) v[i] = LoadBe32(p + 4 * i);
      AppendTextureContent(contents, t, DumpTexture(v));
    }
    std::snprintf(buf, sizeof(buf), "%s\"%u\":", first ? "" : ",", t);
    first = false;
    row += buf;
    row += p ? HexDwords(v, 6) : "null";
  }
  row += "},\"tfc\":{";
  row += contents;
  row += "}";
  AppendPixelConstants(row, device);
  AppendPixelRegisters(row, device);
  AppendLightInputs(row, device, dev, vc_ok ? &vc : nullptr, false, -1);
  if (vc_ok) AppendVertexRegisters(row, device, vc.obj, vc.variant);
  AppendTerrainPixelConstants(row, device);
  if (const uint8_t* bools = ReadVirtual(device + xdk::kDevicePsBoolConstantsOffset, 4)) {
    std::snprintf(buf, sizeof(buf), ",\"pbool\":\"0x%08X\"", LoadBe32(bools));
    row += buf;
  }
  // The immediate path runs a patched copy of the vertex microcode, but the table is the object's.
  row += ",\"vs_lit\":";
  row += vc_ok ? LiteralsJson(kVsLayout, vc.obj, vc.variant) : "null";
  row += ",\"ps_lit\":";
  row += ps_obj ? LiteralsJson(kPsLayout, ps_obj, 0) : "null";
  row += "}\n";
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
  // vs-transforms.json "light" (FindVsLight): what the light reads on the vertex side.
  const VsLightSpec* light = nullptr;
  // ReadLiteralRegisters: the vertex registers the shader loads itself (the device's bank does not
  // hold them); `literals_known` false when its table could not be read. Read only for a shader
  // with a light entry: nothing else reads vertex constants that could be literals.
  uint64_t literal_regs[4] = {};
  bool literals_known = false;
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
  e.light = FindVsLight(hash);
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
    // (the immediate path runs a patched copy of the microcode, but the literal table is the object's)
    if (e.light) e.literals_known = ReadLiteralRegisters(kVsLayout, c.obj, c.variant, 0, e.literal_regs);
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
  if (e.light) e.literals_known = ReadLiteralRegisters(kVsLayout, c.obj, c.variant, 0, e.literal_regs);
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

// The device snapshot and vertex shader FillDrawInputs (FillTerrainInputs) used,
// for FillMaterial and FillLightInputs (the device is read once per draw). `vs`
// points into t_vs_cache: valid until the next LookupVs. `ps`: the pixel shader
// the material was read with, into t_ps_cache: valid until the next LookupPs.
// max_vertex: the largest vertex index the draw reads (its largest index plus
// the base vertex; DrawVertices: its last vertex), -1 when that is not known.
struct DrawShaders {
  bool have_dev = false;
  DeviceSnapshot dev;
  const VsInfo* vs = nullptr;
  const PsInfo* ps = nullptr;
  int64_t max_vertex = -1;
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
    const bool scanned = ScanIndices(ib, in.start, in.count, &scan);
    if (scanned && scan.max_index >= 0) shaders->max_vertex = scan.max_index + int64_t(in.base_vertex);
    if (!scanned) {
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
  } else if (in.count != 0) {
    shaders->max_vertex = int64_t(in.start) + in.count - 1;
  }
  // Skin (vs-transforms.json "skin", one bone or weighted): the bone palette
  // is the stream feeding the rows' fetch slot; endians come from the two
  // fetch constants.
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
      // A palette without one whole bone cannot skin anything (SkinPositions
      // would fail in the renderer, counted render-other).
      if (ok && PaletteBones(s, s.palette_size) > 0) {
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

// The material inputs of a terrain draw (terrain_patch.h; frame-map section 13,
// "Terrain"), for a patch that was built. The draw's pixel shader must have a
// terrain entry (ps-albedo.json) of the builder's kind: a layer shader on the
// layer builder, a far shader on the adaptive one. The registers
// MakeTerrainMaterialInputs names are read into t_bank and t_ps_bank (the pixel
// bank as FillMaterial reads an alpha recipe's scale) and at most six fetch
// constants from the device shadow: bounded reads, and anything the material
// reads that is unreadable leaves in.terrain.material invalid. The base map's
// fetch constant and its two registers (vs c14, ps c21) are light, not material:
// when one cannot be read only base_valid stays false; so is the vertex
// shader's normal map (tf17), read for every terrain draw: without it the draw
// has no normals and keeps its material. A layer shader with the
// steep-ground block (the entry's "steep") adds one pixel register (c45) and
// the side texture's fetch constant, and needs the normal map; the
// block's side scale is in the detail scale's register. A record whose shader
// has the block but whose inputs for it cannot be read is invalid like any
// other, so that its square falls back as a whole. Also records the pixel shader and its
// kind in the material; the clay pass draws the record with them (clay_logic.h,
// "Terrain material") and as flat clay when they are not valid.
void FillTerrainMaterial(uint32_t device, const DeviceSnapshot& dev, bool adaptive, DrawInputs& in,
                         const PsInfo** ps_out) {
  in.terrain.material = TerrainMaterialInputs{};
  if (!in.terrain.active) return;
  uint32_t ps_obj = 0;
  const PsInfo* ps = ChoosePs(dev, &ps_obj) ? LookupPs(ps_obj) : nullptr;
  *ps_out = ps;  // for the light inputs (FillLightInputs)
  if (!ps) return;
  in.material.ps_hash = ps->hash;
  const PsTerrainEntry* entry = ps->terrain;  // points into the table: stable
  if (!entry) return;
  in.material.kind = entry->kind;
  if ((entry->kind != MaterialKind::kTerrainLayer) != adaptive) return;
  bool ok = ReadBankRegisters(device, kTerrainVsCullReg, 2);
  ok = ok && (adaptive ? ReadBankRegisters(device, kTerrainVsFarKnotReg, 1)
                       : ReadBankRegisters(device, kTerrainVsDetailReg, 1) && ReadBankRegisters(device, kTerrainVsWorldReg, 3));
  for (uint32_t reg : {kTerrainPsEyeReg, kTerrainPsFadeReg}) {
    ok = ok && ReadPsBankRegisters(device, reg, 1);
  }
  const TerrainSteepSpec* steep = entry->has_steep ? &entry->steep : nullptr;
  if (steep) ok = ok && ReadPsBankRegisters(device, kTerrainPsSlopeReg, 1);
  if (!ok) return;
  // An unread base register is handed on as not finite (never as what the bank held before).
  const float unread = std::numeric_limits<float>::quiet_NaN();
  if (!ReadBankRegisters(device, kTerrainVsBaseScaleReg, 1)) {
    std::fill_n(t_bank + 4 * kTerrainVsBaseScaleReg, 4, unread);
  }
  if (!ReadPsBankRegisters(device, kTerrainPsBaseReg, 1)) {
    std::fill_n(t_ps_bank + 4 * kTerrainPsBaseReg, 4, unread);
  }
  uint32_t fc[6][6];  // 2 and 3: the far tables or (2) the weights; 4: the steep block's side texture; 5: the normal map
  auto fetch = [&](uint32_t slot, uint32_t* out) -> const uint32_t* {
    const uint8_t* p =
        slot < 32 ? ReadVirtual(device + xdk::kDeviceVertexFetchOffset + xdk::kDeviceTextureFetchStride * slot, 24)
                  : nullptr;
    if (!p) return nullptr;
    for (int i = 0; i < 6; ++i) out[i] = LoadBe32(p + 4 * i);
    return out;
  };
  TerrainFetches f;
  f.detail = fetch(entry->detail_slot, fc[0]);
  f.base = fetch(entry->base_slot, fc[1]);
  if (adaptive) {
    f.far_u = fetch(kTerrainFarUFetch, fc[2]);
    f.far_v = fetch(kTerrainFarVFetch, fc[3]);
  } else {
    f.weight = fetch(kTerrainWeightFetch, fc[2]);
  }
  if (steep) f.side = fetch(steep->side_slot, fc[4]);
  // The vertex shader's normal map: the light of every terrain draw reads it (and the block).
  f.normal = fetch(kTerrainNormalFetch, fc[5]);
  MakeTerrainMaterialInputs(in.terrain, adaptive, t_bank, t_ps_bank, f, &in.terrain.material, steep);
}

// Fills `in` for a tessellated terrain draw (DrawIndx:8221C9C8, the adaptive
// DrawIndx:82207C30): patches [first, first + patches) of the bound vertex
// shader's terrain grid (terrain_patch.h). A shader without a terrain entry
// leaves the draw unsupported; an unhandled heightmap leaves `terrain`
// inactive (kUnknownPosFormat). `adaptive`: the draw is the adaptive builder's.
// `shaders` receives the device snapshot and the two shaders, as FillDrawInputs
// and FillMaterial leave them for an ordinary draw.
SkipReason FillTerrainInputs(uint32_t device, uint32_t first, uint32_t patches, bool adaptive, DrawInputs& in,
                             DrawShaders* shaders) {
  if (const SkipReason c = CountSkip(patches); c != SkipReason::kNone) return c;
  DeviceSnapshot& dev = shaders->dev;
  shaders->have_dev = ReadDevice(device, &dev);
  if (!shaders->have_dev) return SkipReason::kNone;
  VsChoice vc;
  const VsInfo* vs = ChooseVs(dev, &vc) ? LookupVs(vc) : nullptr;
  shaders->vs = vs;
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
  FillTerrainMaterial(device, dev, adaptive, in, &shaders->ps);
  return SkipReason::kNone;
}

// Albedo texture and UVs for a recorded draw (material.h). Never changes the
// draw's skip reason. Terrain draws need no lookups here (FillTerrainMaterial
// has recorded their pixel shader, kind and inputs; the status stays terrain,
// and the renderer decides from the inputs whether the record gets the terrain
// material or flat clay); a draw whose device was not read
// stays ps-unknown.
void ResolveAlbedo(uint32_t device, const DeviceSnapshot& dev, const VsInfo* vs, const AlbedoSpec& a, DrawInputs& in);
void FillMaterial(uint32_t device, DrawShaders& shaders, DrawInputs& in) {
  Material& m = in.material;
  if (in.terrain_shader) {
    m.status = MaterialStatus::kTerrain;
    return;
  }
  m.status = MaterialStatus::kPsUnknown;
  m.alpha = {};
  m.alpha_scale = 1.0f;
  m.kind = MaterialKind::kPlain;
  // (A patch draw whose vertex shader has no terrain entry is never drawn: no lookups for it,
  // though its device snapshot is here now, read for the terrain draws' light inputs.)
  if (!shaders.have_dev || in.prim == kPrimQuadPatch) return;
  const DeviceSnapshot& dev = shaders.dev;
  uint32_t ps_obj = 0;
  const PsInfo* ps = ChoosePs(dev, &ps_obj) ? LookupPs(ps_obj) : nullptr;
  shaders.ps = ps;  // for the light inputs (FillLightInputs)
  if (!ps) return;
  // Copied at once: ps points into t_ps_cache (the table entry is stable).
  m.ps_hash = ps->hash;
  m.kind = ps->terrain ? ps->terrain->kind : MaterialKind::kPlain;
  if (ps->alpha.source != AlphaSource::kNone) {
    // The recipe's scale constants (register * 4 + component) at this draw, multiplied; a
    // constant that cannot be read or is not finite leaves the draw without a recipe.
    float vals[2];
    int n = 0;
    bool ok = true;
    for (const int32_t ref : ps->alpha.scale_ref) {
      if (ref < 0) continue;
      ok = ok && ref < 1024 && ReadPsBankRegisters(device, uint32_t(ref) / 4, 1);
      if (ok) vals[n++] = t_ps_bank[ref];
    }
    float scale = 1.0f;
    if (ok && ComposeAlphaScale(vals, n, &scale)) {
      m.alpha = ps->alpha;
      m.alpha_scale = scale;
    }
  }
  const AlbedoSpec* albedo = ps->albedo;
  if (!albedo) return;
  if (albedo->slot < 0) {
    m.status = MaterialStatus::kNoAlbedo;
    return;
  }
  ResolveAlbedo(device, shaders.dev, shaders.vs, *albedo, in);
}

// The texture and its coordinates for in.material from a pixel shader's albedo entry `a` (slot
// 0 to 31) and the vertex shader's "uv" entries: the fetch constant, the UV element and its
// stream, and the scale and offset folded from the constants the two entries name. The status
// becomes textured, or uv-unsupported when any of it does not resolve or cannot be read. Used
// by FillMaterial for a scene draw, and by FillCasterMaterial for an alpha-tested shadow caster
// (whose "albedo" is the texture its alpha comes from).
void ResolveAlbedo(uint32_t device, const DeviceSnapshot& dev, const VsInfo* vs, const AlbedoSpec& a, DrawInputs& in) {
  Material& m = in.material;
  m.status = MaterialStatus::kUvUnsupported;
  if (!vs || a.slot < 0 || a.slot >= 32 || a.u_interp >= 16 || a.v_interp >= 16 || a.u_comp >= 4 || a.v_comp >= 4) return;
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

// The stream behind a light fetch's slot (material.h, LightStream): resolved only when the stream
// is bound as its vertex buffer object says (fc_match), from its bound offset on.
LightStream ResolveLightStream(const DeviceSnapshot& dev, const VertexFetch* f) {
  LightStream out;
  StreamView sv;
  if (!f || ResolveStream(dev, f->fetch_slot, &sv) || !sv.fc_match) return out;
  out.resolved = true;
  out.vb = {sv.base + sv.offset, sv.size - sv.offset};
  out.endian = uint8_t(sv.fc1 & 3);
  return out;
}

// The light inputs of a draw (material.h, MakeLightInputs; frame-map section 14, "Light inputs"):
// its family from the two shaders' table entries, then what the family reads per draw. Guest reads,
// all bounded: the registers LightRegisterRuns names, from the device's two banks into t_bank and
// t_ps_bank (at most 18 vertex and 3 pixel registers; a register the shader loads itself, or whose
// literal table could not be read, is left unread), and the vertex buffer objects of the two
// streams the entry's fetches name. A draw without a family costs the table lookups its shaders'
// cache entries already hold and reads nothing. Anything unread or not usable gives a reason and
// default inputs: the draw then keeps its material and is drawn without light.
// `instanced`: the draw has its instance set; `max_vertex`: DrawShaders::max_vertex.
LightReason FillLightInputs(uint32_t device, const DeviceSnapshot& dev, const VsInfo* vs, const PsInfo* ps,
                            bool instanced, int64_t max_vertex, LightInputs* out, const char** why) {
  LightDraw d;
  d.ps = ps ? ps->light : nullptr;
  d.vs = vs ? vs->light : nullptr;
  d.pair = d.ps && d.vs && LightPairChecked(ps->hash, vs->hash);
  if (!d.pair) return MakeLightInputs(d, out, why);  // no family: nothing is read
  d.fetches = &vs->fetches;
  d.instanced = instanced;
  d.max_vertex = max_vertex;
  d.vs_regs.bank = t_bank;
  d.ps_regs.bank = t_ps_bank;
  LightRegisterRun runs[kMaxLightRegisterRuns];
  const size_t n = LightRegisterRuns(*d.ps, *d.vs, runs);
  for (size_t i = 0; i < n; ++i) {
    const LightRegisterRun& run = runs[i];
    const bool pixel = run.bank != 0;
    const uint64_t* literal = pixel ? ps->literal_regs : vs->literal_regs;
    bool ok = pixel ? ps->literals_known : vs->literals_known;
    for (uint32_t r = run.reg; ok && r < uint32_t(run.reg) + run.count; ++r) ok = !((literal[r / 64] >> (r % 64)) & 1);
    if (!ok) continue;
    if (pixel) {
      // Through the load model like every pixel constant (the bank for these registers, unless
      // a call the static list does not know named one: then what that load decides).
      for (uint32_t r = run.reg; ok && r < uint32_t(run.reg) + run.count; ++r) {
        uint32_t bits[4];
        ok = ReadConstantBits(PixelBank(device), t_ps_loads.Find(r), r, bits);
        if (ok) std::memcpy(&t_ps_bank[4 * r], bits, sizeof(bits));
      }
    } else {
      ok = ReadBankRegisters(device, run.reg, run.count);
    }
    if (ok) (pixel ? d.ps_regs : d.vs_regs).Mark(run.reg, run.count);
  }
  d.normal_stream = ResolveLightStream(dev, LightFetch(vs->fetches, d.vs->normal));
  d.second_stream = ResolveLightStream(dev, LightFetch(vs->fetches, d.vs->second));
  return MakeLightInputs(d, out, why);
}

// Discovery evidence (frame-map section 14, "Light inputs"), in draw and tess rows: the draw's
// light inputs as FillLightInputs gives them at the row's draw, read again here (the row is
// written whether or not the draw is recorded), under "lin":
//   {"reason":"none","family","multi","kind","a0","normal":{element},"second":{element},"first",
//    "power","facing","rot":[9],"scale","c11x","c27w","c49":[3],"c26":[3],"pos":[3],"wrap","c8x","vc":[[4] x n]}
// with an element as {"phys","size","off","stride","fmt","src","flags"} (left out for a terrain
// draw; "src" the element's source components in the reader's order, as the table names them:
// "xyz", "yxw", "zyxw"; the fetch's destination swizzle is the row's "fetches"[i]."swz"), "pos" and
// "wrap" a leaf's world translation (vertex c4..c6 .w) and vertex c50.w (zero for any other family),
// "c8x" a trunk's or a leaf's vertex c8.x, the scale of the trees' in-scatter (-1: none usable, and
// for any other family),
// and "vc" the probe sets (twelve registers) or the plants' c45 (one); or, without inputs,
//   {"reason":"light-unknown"|"normal-unsupported","why": MakeLightInputs' word[,"family","multi"][,"kind"]}
// where family and kind are what the two shaders' entries say, when they have one.
// One difference from the records: `instanced` here is that the vertex shader's instance rows
// match its fetches (the record also needs the set built from the instance stream); a row can
// therefore show inputs for an instanced draw whose record has none.
void AppendLightInputs(std::string& row, uint32_t device, const DeviceSnapshot& dev, const VsChoice* vc, bool indexed,
                       int64_t max_vertex) {
  const VsInfo* vs = vc ? LookupVs(*vc) : nullptr;
  uint32_t ps_obj = 0;
  const PsInfo* ps = ChoosePs(dev, &ps_obj) ? LookupPs(ps_obj) : nullptr;
  LightInputs l;
  const char* why = "";
  const LightReason reason = FillLightInputs(device, dev, vs, ps, indexed && vs && vs->have_instance, max_vertex, &l, &why);
  char buf[192];
  std::snprintf(buf, sizeof(buf), ",\"lin\":{\"reason\":\"%s\"", LightReasonName(reason));
  row += buf;
  if (reason != LightReason::kNone) {
    std::snprintf(buf, sizeof(buf), ",\"why\":\"%s\"", why);
    row += buf;
    if (ps && ps->light) {
      std::snprintf(buf, sizeof(buf), ",\"family\":\"%s\",\"multi\":%d", LightFamilyName(ps->light->family),
                    ps->light->multi ? 1 : 0);
      row += buf;
    }
    if (vs && vs->light) {
      std::snprintf(buf, sizeof(buf), ",\"kind\":\"%s\"", LightVertexKindName(vs->light->kind));
      row += buf;
    }
    row += "}";
    return;
  }
  std::snprintf(buf, sizeof(buf), ",\"family\":\"%s\",\"multi\":%d,\"kind\":\"%s\",\"a0\":%d", LightFamilyName(l.family),
                l.multi ? 1 : 0, LightVertexKindName(l.kind), int(l.a0_comp));
  row += buf;
  auto element = [&](const char* key, const LightElement& e) {
    char src[5] = {};
    for (uint32_t i = 0; i < e.source_count && i < 4; ++i) src[i] = "xyzw"[LightSourceComponent(e, i)];
    std::snprintf(buf, sizeof(buf),
                  ",\"%s\":{\"phys\":%u,\"size\":%u,\"off\":%u,\"stride\":%u,\"fmt\":%u,\"src\":\"%s\",\"flags\":%u}", key,
                  e.vb.phys_addr, e.vb.size, e.offset_bytes, e.stride_bytes, e.format, src, e.flags);
    row += buf;
  };
  auto floats = [&](const char* key, const float* v, int n) {
    row += ",\"";
    row += key;
    row += n > 1 ? "\":[" : "\":";
    for (int i = 0; i < n; ++i) {
      if (i) row += ",";
      AppendFloat(row, v[i]);
    }
    if (n > 1) row += "]";
  };
  if (l.kind != LightVertexKind::kTerrain) {
    element("normal", l.normal);
    element("second", l.second);
  }
  std::snprintf(buf, sizeof(buf), ",\"first\":%u", l.colour_first);
  row += buf;
  floats("power", &l.colour_power, 1);
  floats("facing", &l.facing_scale, 1);
  floats("rot", &l.world_rot[0][0], 9);
  floats("scale", &l.world_scale, 1);
  floats("c11x", &l.c11x, 1);
  floats("c27w", &l.c27w, 1);
  floats("c49", l.c49, 3);
  floats("c26", l.c26, 3);
  floats("pos", l.world_pos, 3);
  floats("wrap", &l.wrap, 1);
  floats("c8x", &l.tree_fog, 1);
  const uint32_t sets = l.kind == LightVertexKind::kProbe ? kLightProbeRegisters : l.kind == LightVertexKind::kInstance ? 1 : 0;
  if (sets) {
    row += ",\"vc\":[";
    for (uint32_t r = 0; r < sets; ++r) {
      row += r ? ",[" : "[";
      for (int c = 0; c < 4; ++c) {
        if (c) row += ",";
        AppendFloat(row, l.vs_const[r][c]);
      }
      row += "]";
    }
    row += "]";
  }
  row += "}";
}

// The frame's light block (FrameScene::light) is read once per light set until one read gives
// it, and at most LightBlockTries::kMaxSets (8) times a frame without result (light_block.h,
// LightBlockTries): a read is eleven ReadPixelConstant calls, far too many to make at every draw
// of a frame whose light set is not a sun's (a night, an interior: no block, "light-block"),
// and a set without a sun gives no block at the next draw under it either.
// g_light_tries_frame: frames ended (EndMainSceneFrame), which is what tells the tries of one frame
// from the next's (not g_light_frames, the frames counted for the log's light-block line). The
// tries are the drawing thread's own, as the load model is whose upload numbers they hold.
std::atomic<uint32_t> g_light_tries_frame{0};
constinit thread_local LightBlockTries t_light_tries;

// --- The two sun shadow passes (frame-map section 15; shadow_block.h, main_scene.h) ---------
// Taken only in a frame with records and with the shadows wanted (g_shadows_frame): the draws
// of the two passes as record lists of their own (FrameScene::shadow_draws), and the frame's
// shadow block, proven against the passes when the frame ends (FrameBuilder::Finish).

// This thread's shadow state of the current frame (the drawing thread's own, like the light's
// tries; g_light_tries_frame tells one frame from the next): the pass tracker, which pass has
// given its light matrix, and the reads of the block's registers.
struct ShadowFrame {
  ShadowPassTracker tracker;
  bool have_matrix[2] = {};
  bool registers_done = false;
  uint32_t register_tries = 0;
  uint32_t frame = 0;
};
constinit thread_local ShadowFrame t_shadow;
// At most this many reads of the block's registers (eleven ReadPixelConstant calls each) in a
// frame that gives none: what a frame without valid registers costs, as for the light.
constexpr uint32_t kMaxShadowRegisterTries = 8;

// Shadow draws, passes, extra draws and register reads that were read in one frame and found, under the lock, to
// belong to a frame that is over (the frame's recheck below): dropped, and counted here for the
// shadow: log line since its last print (atomic: the line is written at the frame end).
std::atomic<uint32_t> g_shadow_dropped{0};

// Whether `f` is still the frame being built. Call with g_scene_mutex held: the frame's number
// moves under that mutex (EndMainSceneFrame), together with the builder's Finish. A caller that
// gets false drops what it holds; the drop is counted (g_shadow_dropped).
bool ShadowFrameCurrent(const ShadowFrame& f) {
  const bool current = f.frame == g_light_tries_frame.load(std::memory_order_relaxed);
  if (!current) g_shadow_dropped.fetch_add(1, std::memory_order_relaxed);
  return current;
}

ShadowFrame& CurrentShadowFrame() {
  const uint32_t frame = g_light_tries_frame.load(std::memory_order_relaxed);
  if (t_shadow.frame != frame) {
    t_shadow = ShadowFrame{};
    t_shadow.frame = frame;
  }
  return t_shadow;
}

// The registers' part of the frame's shadow block at the main-scene draw this thread is
// capturing (shadow_block.h, MakeShadowBlock): pixel c12, c13, c14 and c36..c43 through
// ReadPixelConstant, the reader the captures of frame-map section 15 were checked with ("pfull":
// one value in every decoded scene row of a frame). Read once a frame, at the frame's first
// scene draw of a kind those rows are (DrawIndexedVertices, DrawVertices, the two terrain
// builders; never at a BeginVertices draw: the two mask rectangles are drawn with other values
// in c36..c43), and again at the next such draw while a read gives no valid registers (one of
// them a literal of that draw's pixel shader, or not read), up to kMaxShadowRegisterTries.
void ReadShadowRegisters(uint32_t id, ShadowFrame& f) {
  if (f.registers_done || f.register_tries >= kMaxShadowRegisterTries) return;
  if (id != kHook_D3DDevice_DrawIndexedVertices && id != kHook_D3DDevice_DrawVertices && id != kHook_DrawIndx_8221C9C8 &&
      id != kHook_DrawIndx_82207C30) {
    return;
  }
  float reg[ConstantLoads::kRegisters][4] = {};
  bool have[ConstantLoads::kRegisters] = {};
  for (uint32_t r : kShadowBlockRegisters) have[r] = ReadPixelConstant(r, reg[r]);
  const ShadowBlock registers = MakeShadowBlock(reg, have);
  if (!registers.valid) {
    ++f.register_tries;
    return;
  }
  std::lock_guard<std::mutex> lock(g_scene_mutex);
  if (!g_builder.InMainScene()) return;  // (as RecordDraw: the bracket is checked again under the lock)
  // The frame is checked again under the lock, as for the shadow draws: registers read in one frame
  // must not become the next frame's block.
  if (!ShadowFrameCurrent(f)) return;
  g_builder.SetShadowRegisters(registers);
  f.registers_done = true;
}

// The material of an alpha-tested shadow caster (draw_record.h, "Shadow-pass records"): the
// texture its alpha comes from as the record's albedo (the fetch constant of the entry's slot;
// the coordinates of interpolator 0, x and y, times pixel c8.xy plus c8.zw, through the vertex
// shader's "uv" entries: ResolveAlbedo), and the alpha recipe (the texture's w times the entry's
// scale constant at this draw). False when any of it cannot be read or is not finite: the draw
// is then skipped (alpha-inputs), never drawn as an opaque caster.
bool FillCasterMaterial(uint32_t device, const DrawShaders& shaders, const CasterEntry& entry, DrawInputs& in) {
  Material& m = in.material;
  m = Material{};
  m.ps_hash = entry.ps_hash;
  constexpr int32_t kPixelC8 = (1 << 10) | (8 * 4);  // a UV stage's constant reference: bank 1, register 8
  AlbedoSpec a{};
  a.ps_hash = entry.ps_hash;
  a.slot = entry.slot;
  a.u_interp = 0;
  a.u_comp = 0;
  a.v_interp = 0;
  a.v_comp = 1;
  a.u_stages[0] = UvStage{kPixelC8 + 0, kPixelC8 + 2};
  a.v_stages[0] = UvStage{kPixelC8 + 1, kPixelC8 + 3};
  ResolveAlbedo(device, shaders.dev, shaders.vs, a, in);
  if (m.status != MaterialStatus::kTextured) return false;
  for (float v : m.uv_xform) {
    if (!std::isfinite(v)) return false;
  }
  float scale = 1.0f;
  if (entry.scale_ref >= 0) {
    if (!ReadPsBankRegisters(device, uint32_t(entry.scale_ref) / 4, 1)) return false;
    if (!ComposeAlphaScale(&t_ps_bank[entry.scale_ref], 1, &scale)) return false;
  }
  m.alpha.source = AlphaSource::kAlbedo;
  m.alpha.slot = uint8_t(entry.slot);
  m.alpha.component = 3;
  m.alpha.scale_ref[0] = entry.scale_ref;
  m.alpha.scale_ref[1] = -1;
  m.alpha_scale = scale;
  return true;
}

// One draw of a shadow pass (`pass`: kNear or kFar), as a DrawRecord in that pass's list. The
// geometry is read as a scene draw's (FillDrawInputs: streams, indices, the bone palette, the
// instance set; only DrawIndexedVertices and DrawVertices, any other hook is an unsupported
// primitive). Then, fail closed (a draw that cannot be read is skipped and counted, never drawn
// from guessed data):
//  - the draw's shader pair must be in the caster table (no-caster, counted by vertex shader);
//  - the state words must be read, and with the polygon offset on (PA_SU_SC_MODE_CNTL bits 11,
//    12) its four registers (state-unread);
//  - an alpha-test caster needs its texture, coordinates and scale (alpha-inputs);
//  - the transform rows must be finite (no-transform).
// The first recorded draw of a pass also gives the pass's light matrix for the frame's proof:
// c0..c3 times the inverse of the world rows c4..c6 (PassMatrix), or c0..c3 itself for the
// instanced bushes' shader (kShadowDirectMatrixVs).
void RecordShadowDraw(uint32_t id, const LastArgs& a, uint32_t device, ShadowPass pass, ShadowFrame& f) {
  const size_t cascade = pass == ShadowPass::kNear ? 0 : 1;
  DrawInputs in;
  in.func_id = id;
  SkipReason extra = SkipReason::kNone;
  DrawShaders shaders;
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
  }
  if (IsSupportedPrim(in.prim)) extra = FillDrawInputs(device, in, &shaders);
  ReadStateWords(device, &in.state_words);
  uint8_t skip = 0;
  ShadowCaster caster;
  const CasterEntry* entry = nullptr;
  if (shaders.vs) {
    // The pixel shader as the draw runs it: none (a depth-only draw), or one whose hash is known.
    uint32_t ps_obj = 0;
    uint64_t ps_hash = 0;
    bool ps_known = true;
    if (ChoosePs(shaders.dev, &ps_obj)) {
      shaders.ps = LookupPs(ps_obj);
      ps_known = shaders.ps != nullptr;
      if (ps_known) ps_hash = shaders.ps->hash;
    }
    entry = ps_known ? FindCaster(shaders.vs->hash, ps_hash) : nullptr;
    if (!entry) {
      skip = uint8_t(ShadowSkip::kNoCaster);
    } else if (!in.state_words.read) {
      skip = uint8_t(ShadowSkip::kStateUnread);
    } else {
      caster.kind = entry->kind;
      uint32_t words[4];
      const uint8_t* p = ReadVirtual(device + xdk::DeviceRegShadowOffset(xdk::kRegPolyOffsetFrontScale), sizeof(words));
      for (int i = 0; p && i < 4; ++i) words[i] = LoadBe32(p + 4 * i);
      const DrawBias bias = ShadowDrawBias(in.state_words.su_sc_mode, p ? words : nullptr);
      caster.biased = bias.biased;
      caster.bias_scale = bias.scale;
      caster.bias_offset = bias.offset;
      if (bias.biased && !p) {
        skip = uint8_t(ShadowSkip::kStateUnread);
      } else if (entry->kind == CasterKind::kAlphaTest && !FillCasterMaterial(device, shaders, *entry, in)) {
        skip = uint8_t(ShadowSkip::kAlphaInputs);
      }
    }
  }
  DrawRecord r = AssembleRecord(in, 0);
  r.skip = ResolveSkip(r.skip, extra);
  if (skip == 0 && r.skip == SkipReason::kNone) {
    for (float v : r.rows) {
      if (!std::isfinite(v)) r.skip = SkipReason::kNoTransform;
    }
  }
  // The pass's light matrix, from the first draw of the pass that is recorded and gives one.
  float matrix[4][4];
  bool have_matrix = false;
  if (skip == 0 && r.skip == SkipReason::kNone && !f.have_matrix[cascade] && ReadBankRegisters(device, 0, 7)) {
    float c[7][4];
    std::memcpy(c, t_bank, sizeof(c));
    if (r.vs_hash == kShadowDirectMatrixVs) {
      have_matrix = true;
      for (int i = 0; i < 16; ++i) have_matrix = have_matrix && std::isfinite(c[i / 4][i % 4]);
      if (have_matrix) std::memcpy(matrix, c, sizeof(matrix));
    } else {
      have_matrix = PassMatrix(c, c + 4, matrix);
    }
  }
  std::lock_guard<std::mutex> lock(g_scene_mutex);
  // The frame is checked again under the lock, as RecordDraw checks its bracket: the swap may
  // run on another thread than the draws (nothing here shows that it does not), and a draw read
  // in one frame must not land in the next frame's pass, whose cascade it was not classified in.
  if (!ShadowFrameCurrent(f)) return;
  if (g_builder.AddShadow(cascade, r, caster, skip, have_matrix ? matrix : nullptr) && have_matrix) {
    f.have_matrix[cascade] = true;
  }
}

// One outermost guest draw call outside the main scene, in a frame with shadow records: tells
// the pass tracker and records the draw if it is one of a shadow pass.
// The game's clear (DrawIndx2:821EF988) opens a pass when it clears the shadow map: r8 the
// map's surface word, r7 its height (frame-map section 15: r7 = 0x400, r8 = 0x10000410). The
// routine is entered a second time for each clear, with a packet header in r7 and no surface
// word; that call is no draw and tells the tracker nothing. A clear of another surface (a
// first entry: r7 below 0x10000) opens nothing and ends the run of an open pass.
// Any other hook is a draw: on the map (the device's RB_SURFACE_INFO shadow) with the colour
// mask 0 it is a shadow draw; a word that cannot be read is not the map, and an unread mask is
// a mask.
void ObserveShadowDraw(uint32_t id, const LastArgs& a, uint32_t device) {
  ShadowFrame& f = CurrentShadowFrame();
  if (id == kHook_DrawIndx2_821EF988) {
    if (a.r[4] >= 0x10000) return;
    f.tracker.Observe(a.r[5], 0, true, false);
    if (a.r[5] != kShadowMapSurfaceWord) return;
    std::lock_guard<std::mutex> lock(g_scene_mutex);
    if (ShadowFrameCurrent(f)) g_builder.CountShadowPass();
    return;
  }
  uint32_t surface = 0, mask = 0xF;
  if (!device || !ReadVirtualBe32(device + xdk::kDeviceSurfaceInfoOffset, &surface)) surface = 0;
  if (surface == kShadowMapSurfaceWord &&
      !ReadVirtualBe32(device + xdk::DeviceRegShadowOffset(xdk::kRegColorMask), &mask)) {
    mask = 0xF;
  }
  const ShadowPass pass = f.tracker.Observe(surface, mask, false, false);
  if (pass == ShadowPass::kNone) return;
  if (pass == ShadowPass::kExtra) {
    std::lock_guard<std::mutex> lock(g_scene_mutex);
    if (ShadowFrameCurrent(f)) g_builder.CountShadowExtra();
    return;
  }
  RecordShadowDraw(id, a, device, pass, f);
}

// --- The fog and sky blocks (frame-map section 16; fog_block.h) ------------------------------
// Read only in a frame with records and with the fog wanted (g_fog_frame), once a frame each,
// at a main-scene draw this thread is capturing.

// This thread's fog state of the current frame (the drawing thread's own, like the shadows';
// g_light_tries_frame tells one frame from the next): whether each block was taken, and the
// reads that gave none.
struct FogFrame {
  bool fog_done = false;
  uint32_t fog_tries = 0;
  bool sky_done = false;
  uint32_t sky_tries = 0;
  uint32_t sky_untold = 0;  // builder calls whose pixel shader could not be told (ReadSkyBlock)
  uint32_t frame = 0;
};
constinit thread_local FogFrame t_fog;
// At most this many reads of each block in a frame that gives none (a fog read is six
// ReadPixelConstant calls, two fetch constants and 1.5 KB of tables; a sky read three and two):
// what a frame whose tables cannot be read costs.
constexpr uint32_t kMaxFogTries = 8;
static_assert(kFogScatterBytes + kFogTransmitBytes == 1536);

// Fog and sky reads made in one frame and found, under the lock, to belong to a frame that is
// over (the frame's recheck below): dropped, and counted here for the fog: log line since its
// last print.
std::atomic<uint32_t> g_fog_dropped{0};

// The registers of the two blocks are the bank's at any scene draw: no buffer load names them
// (fog_block.h, kFogBlockRegisters; ReadPixelConstant would otherwise need the load model's answer).
constexpr bool FogRegistersAreBankOnly() {
  for (uint32_t r : kFogBlockRegisters) {
    if (IsBufferLoadRegister(kPixelBank, r)) return false;
  }
  for (uint32_t r : kSkyBlockRegisters) {
    if (IsBufferLoadRegister(kPixelBank, r)) return false;
  }
  return true;
}
static_assert(FogRegistersAreBankOnly(), "a fog or sky register is loaded from a constant buffer");

FogFrame& CurrentFogFrame() {
  const uint32_t frame = g_light_tries_frame.load(std::memory_order_relaxed);
  if (t_fog.frame != frame) {
    t_fog = FogFrame{};
    t_fog.frame = frame;
  }
  return t_fog;
}

// Whether `f` is still the frame being built (as ShadowFrameCurrent: call with g_scene_mutex
// held; a caller that gets false drops what it holds, and the drop is counted).
bool FogFrameCurrent(const FogFrame& f) {
  const bool current = f.frame == g_light_tries_frame.load(std::memory_order_relaxed);
  if (!current) g_fog_dropped.fetch_add(1, std::memory_order_relaxed);
  return current;
}

// The fetch constant of texture slot `slot` as the device holds it (host-order dwords).
bool ReadTextureFetch(uint32_t device, uint32_t slot, uint32_t fc[6]) {
  const uint8_t* p = device && slot < 32
                         ? ReadVirtual(device + xdk::kDeviceVertexFetchOffset + xdk::kDeviceTextureFetchStride * slot, 24)
                         : nullptr;
  if (!p) return false;
  for (int i = 0; i < 6; ++i) fc[i] = LoadBe32(p + 4 * i);
  return true;
}

// The frame's fog block at the main-scene draw this thread is capturing, which runs a pixel
// shader with the game's fog block (PsInfo::fog): pixel c28 and c64..c68 through
// ReadPixelConstant, the fetch constants of slots 4 and 5 from the device, and the two tables'
// bytes from guest memory at the constants' base addresses (512 and 1,024 bytes, each read whole
// or not at all: ReadPhysical). The registers and the constants are judged before any table
// byte is read, by MakeFogBlock itself without bytes: its rules come in one order (registers,
// then tf4's constant, then tf5's, then the bytes), so a first call that fails for tf4's bytes
// has passed every rule before them, and any other reason is the frame's, in that same order
// (review of plan Task 3, M6). An address is followed only for a constant that is the table's pin.
// The bytes are copied first; the copy is what is hashed (FogSource: the content a discovery
// capture names the table by) and decoded (MakeFogBlock), so the two cannot differ.
// The tables are whole at this draw: in the captures of frame-map section 16 every main-scene
// row of a frame that samples a table names one (address, content) pair, from the frame's first
// fogged draw to its last (56 of 56 table-frames). Read once a frame, at the frame's first such
// draw, and again at the next while a read gives no block, up to kMaxFogTries; each read that
// gives none is counted in the frame with its reason.
void ReadFogBlock(uint32_t device, FogFrame& f) {
  if (f.fog_done || f.fog_tries >= kMaxFogTries) return;
  FogBlockReason why = FogBlockReason::kUnread;
  FogBlock block{};
  FogSource source;
  float regs[6][4] = {};
  uint32_t fc[2][6] = {};
  bool read = true;
  for (int i = 0; read && i < 6; ++i) read = ReadPixelConstant(kFogBlockRegisters[i], regs[i]);
  for (int t = 0; read && t < 2; ++t) read = ReadTextureFetch(device, kFogTableSlots[t], fc[t]);
  if (read) MakeFogBlock(regs, fc[0], fc[1], nullptr, 0, nullptr, 0, &why);
  if (read && why == FogBlockReason::kScatterBytes) {
    uint8_t tf4[kFogScatterBytes], tf5[kFogTransmitBytes];
    const uint8_t* p4 = ReadPhysical(fc[0][1] & 0xFFFFF000u, uint32_t(kFogScatterBytes));
    const uint8_t* p5 = ReadPhysical(fc[1][1] & 0xFFFFF000u, uint32_t(kFogTransmitBytes));
    if (p4 && p5) {
      std::memcpy(tf4, p4, sizeof(tf4));
      std::memcpy(tf5, p5, sizeof(tf5));
      block = MakeFogBlock(regs, fc[0], fc[1], tf4, sizeof(tf4), tf5, sizeof(tf5), &why);
      if (block.valid) {
        for (int t = 0; t < 2; ++t) source.base[t] = fc[t][1] & 0xFFFFF000u;
        source.content[0] = XXH3_64bits(tf4, sizeof(tf4));
        source.content[1] = XXH3_64bits(tf5, sizeof(tf5));
      }
    } else {
      why = FogBlockReason::kUnread;
    }
  }
  std::lock_guard<std::mutex> lock(g_scene_mutex);
  if (!g_builder.InMainScene()) return;  // (as RecordDraw: the bracket is checked again under the lock)
  // The frame is checked again under the lock, as for the shadow registers: a block read in one
  // frame must not become the next frame's.
  if (!FogFrameCurrent(f)) return;
  if (block.valid) {
    g_builder.SetFog(block, source);
    f.fog_done = true;
  } else {
    g_builder.CountFogTry(why);
    ++f.fog_tries;
  }
}

// The frame's sky block at a main-scene call of the indexed builder 0x82217EE8 (sparkles,
// particles and other quads come through it too: 93 calls in a frame at the bridge, in the scene
// and outside it; capture native_discovery_20261009_065904, frame 2). The sky draw
// is told from the others without decoding anything of its geometry: by the pixel shader the
// draw runs, whose object the device holds at any hooked call (ChoosePs; its microcode hash
// through LookupPs, cached per object) and which is the sky's (kSkyPixelShader) for exactly one
// in-scene call a frame in the captures (28 of 28 frames; the reflection passes' four sky draws
// are outside the main scene and never get here). At that call: pixel c31, c46, c47 through
// ReadPixelConstant and the fetch constants of slots 13 and 14. Once a frame; a read that gives
// no block is counted in the frame with its reason (at most kMaxFogTries).
// A call whose shader cannot be told (the device or the shader's record unreadable) may have
// been the sky's: it is counted in the frame as a try with "unread", so that a frame which then
// has no sky block does not read "no-draw" (review of plan Task 3, M3). Those counts have a
// bound of their own (kMaxFogTries a frame) and do not use up the reads above: the builder's
// other calls come before the sky's, and a few untold ones must not stop the search for it.
void ReadSkyBlock(uint32_t device, FogFrame& f) {
  if (f.sky_done || f.sky_tries >= kMaxFogTries) return;
  DeviceSnapshot dev;
  uint32_t ps_obj = 0;
  const PsInfo* ps = ReadDevice(device, &dev) && ChoosePs(dev, &ps_obj) ? LookupPs(ps_obj) : nullptr;
  if (!ps) {
    if (f.sky_untold >= kMaxFogTries) return;
    std::lock_guard<std::mutex> lock(g_scene_mutex);
    if (!g_builder.InMainScene() || !FogFrameCurrent(f)) return;
    g_builder.CountSkyTry(SkyBlockReason::kUnread);
    ++f.sky_untold;
    return;
  }
  if (ps->hash != kSkyPixelShader) return;
  float regs[3][4] = {};
  uint32_t fc[2][6] = {};
  bool read = true;
  for (int i = 0; read && i < 3; ++i) read = ReadPixelConstant(kSkyBlockRegisters[i], regs[i]);
  for (int t = 0; read && t < 2; ++t) read = ReadTextureFetch(device, kSkyTextureSlots[t], fc[t]);
  SkyBlockReason why = SkyBlockReason::kUnread;
  const SkyBlock block = read ? MakeSkyBlock(regs[0], regs[1], regs[2], fc[0], fc[1], &why) : SkyBlock{};
  std::lock_guard<std::mutex> lock(g_scene_mutex);
  if (!g_builder.InMainScene()) return;
  if (!FogFrameCurrent(f)) return;
  if (block.valid) {
    g_builder.SetSky(block);
    f.sky_done = true;
  } else {
    g_builder.CountSkyTry(why);
    ++f.sky_tries;
  }
}

// --- The post block (frame-map section 17, "Post block"; post_block.h) -----------------------
// Read only in a frame with records and with the post block wanted (g_post_frame), at the
// game's own post rectangles: outermost calls of D3DDevice_BeginVertices outside the main scene.

// This thread's post state of the current frame (the drawing thread's own, like the fog's;
// g_light_tries_frame tells one frame from the next): the registers read so far at the bright
// pass and the blurs, whether the block was taken at the composite, and the reads that gave none.
struct PostFrame {
  PostRegisters regs{};
  bool done = false;
  uint32_t tries = 0;
  uint32_t untold = 0;  // rectangles whose pixel shader could not be told (ObservePostDraw)
  uint32_t frame = 0;
  // The radial blur's c31, c46, c47 (read only with the quads wanted): the game draws two such
  // rectangles a frame with one set of registers (16 of 16 in capture native_discovery_20261010_052158).
  // radial_bad: a read failed, or the second rectangle's registers were not the first's.
  float radial[3][4] = {};
  bool have_radial = false;
  bool radial_bad = false;
};
constinit thread_local PostFrame t_post;
// At most this many reads at a composite in a frame that gives no block (a read is five
// ReadPixelConstant calls, one fetch constant and 2 KB of table): what a frame whose table
// cannot be read costs. The game issues one composite a frame (72 of 72 captured frames).
constexpr uint32_t kMaxPostTries = 8;

// Post reads made in one frame and found, under the lock, to belong to a frame that is over (the
// frame's recheck below): dropped, and counted here for the post: log line since its last print.
std::atomic<uint32_t> g_post_dropped{0};

// The registers of the block are the bank's at any hooked draw: no buffer load names them
// (post_block.h; ReadPixelConstant would otherwise need the load model's answer, which is kept
// for the main scene's draws).
constexpr bool PostRegistersAreBankOnly() {
  for (uint32_t r : kPostCompositeRegisters) {
    if (IsBufferLoadRegister(kPixelBank, r)) return false;
  }
  for (uint32_t r : kPostBrightRegisters) {
    if (IsBufferLoadRegister(kPixelBank, r)) return false;
  }
  for (uint32_t r : kPostBlurRegisters) {
    if (IsBufferLoadRegister(kPixelBank, r)) return false;
  }
  for (uint32_t r : kPostRadialRegisters) {
    if (IsBufferLoadRegister(kPixelBank, r)) return false;
  }
  return true;
}
static_assert(PostRegistersAreBankOnly(), "a post register is loaded from a constant buffer");

PostFrame& CurrentPostFrame() {
  const uint32_t frame = g_light_tries_frame.load(std::memory_order_relaxed);
  if (t_post.frame != frame) {
    t_post = PostFrame{};
    t_post.frame = frame;
  }
  return t_post;
}

// Whether `f` is still the frame being built (as ShadowFrameCurrent: call with g_scene_mutex
// held; a caller that gets false drops what it holds, and the drop is counted).
bool PostFrameCurrent(const PostFrame& f) {
  const bool current = f.frame == g_light_tries_frame.load(std::memory_order_relaxed);
  if (!current) g_post_dropped.fetch_add(1, std::memory_order_relaxed);
  return current;
}

// `n` whole pixel registers through ReadPixelConstant; false when one cannot be read.
bool ReadPostRegisters(const uint32_t* regs, int n, float out[][4]) {
  bool read = true;
  for (int i = 0; read && i < n; ++i) read = ReadPixelConstant(regs[i], out[i]);
  return read;
}

// One outermost hooked draw outside the main scene, in a frame with the post block wanted. Only
// D3DDevice_BeginVertices is looked at: every post rectangle comes through it (frame-map section
// 17: the "verts" rows of the post shaders). Which step a rectangle is, is told by the pixel
// shader the draw runs (ChoosePs; its microcode hash through LookupPs, cached per object), as the
// sky draw is told:
//   - the bright pass (kPostBrightPs): its c31, c46, c47, c77 into this thread's PostFrame;
//   - the blurs (kPostBlurXPs, kPostBlurYPs): their c46, c47;
//   - the radial blur (kPostRadialPs), only with the quads wanted (g_quads_frame): its c31, c46,
//     c47, which the block takes at the composite (SetPostRadial) when both of its rectangles had
//     the same registers;
//   - the composite (kPostCompositePs): its c31, c46, c47, c77, c78, the fetch constant of slot 1,
//     and the table's 2,048 bytes from guest memory at the constant's base address, read whole or
//     not at all (ReadPhysical). The registers and the constant are judged before any table byte
//     is read, by MakePostBlock itself without bytes: its rules come in one order, so a first call
//     that fails for the table's bytes has passed every rule before them. An address is followed
//     only for a constant that is the table's pin. The bytes are copied first; the copy is what is
//     hashed (PostSource: the content a discovery capture names the table by) and decoded, so the
//     two cannot differ. Then the block is handed to the builder under the scene's mutex, after
//     the frame's recheck: a block read in one frame must not become the next frame's.
// The registers are read at the hook's return, where the discovery rows' "pfull" is read too.
// After the frame's block was taken, and after kMaxPostTries reads that gave none, nothing more
// is looked at this frame: what remains per call is this thread's frame check. Before that, a
// call costs the device's snapshot and the shader's lookup (the hash is cached by object).
// A rectangle whose shader cannot be told (the device or the shader's record unreadable) may have
// been the composite: it is counted in the frame as a try with "unread", so that a frame which
// then has no block does not read "no-draw"; those counts have a bound of their own and do not
// use up the reads above. A draw without a pixel shader (depth only) is no post rectangle.
void ObservePostDraw(uint32_t id, uint32_t device) {
  if (id != kHook_D3DDevice_BeginVertices) return;
  PostFrame& f = CurrentPostFrame();
  if (f.done || f.tries >= kMaxPostTries) return;
  DeviceSnapshot dev;
  uint32_t ps_obj = 0;
  const bool have_dev = ReadDevice(device, &dev);
  if (have_dev && !ChoosePs(dev, &ps_obj)) return;
  const PsInfo* ps = have_dev ? LookupPs(ps_obj) : nullptr;
  if (!ps) {
    if (f.untold >= kMaxPostTries) return;
    std::lock_guard<std::mutex> lock(g_scene_mutex);
    if (!PostFrameCurrent(f)) return;
    g_builder.CountPostTry(PostBlockReason::kUnread);
    ++f.untold;
    return;
  }
  const uint64_t hash = ps->hash;  // (`ps` is the cache's entry: not held across the reads below)
  if (hash == kPostBrightPs) {
    f.regs.have_bright = ReadPostRegisters(kPostBrightRegisters, 4, f.regs.bright);
    return;
  }
  if (hash == kPostBlurXPs || hash == kPostBlurYPs) {
    const int s = hash == kPostBlurYPs ? 1 : 0;
    f.regs.have_blur[s] = ReadPostRegisters(kPostBlurRegisters, 2, f.regs.blur[s]);
    return;
  }
  if (hash == kPostRadialPs) {
    if (!g_quads_frame.load(std::memory_order_relaxed)) return;
    float radial[3][4] = {};
    if (!ReadPostRegisters(kPostRadialRegisters, 3, radial)) {
      f.radial_bad = true;
      return;
    }
    if (f.have_radial && !PostRadialSame(f.radial, radial)) f.radial_bad = true;
    std::memcpy(f.radial, radial, sizeof(radial));
    f.have_radial = true;
    return;
  }
  if (hash != kPostCompositePs) return;
  PostRegisters regs = f.regs;
  regs.have_composite = ReadPostRegisters(kPostCompositeRegisters, 5, regs.composite);
  uint32_t fc[6] = {};
  const bool read = regs.have_composite && ReadTextureFetch(device, kPostTableSlot, fc);
  PostBlockReason why = PostBlockReason::kUnread;
  PostBlock block{};
  PostSource source;
  if (read) MakePostBlock(regs, fc, nullptr, 0, &why);
  if (read && why == PostBlockReason::kTableBytes) {
    uint8_t table[kPostTableBytes];
    const uint32_t base = fc[1] & 0xFFFFF000u;
    if (const uint8_t* p = ReadPhysical(base, uint32_t(kPostTableBytes))) {
      std::memcpy(table, p, sizeof(table));
      block = MakePostBlock(regs, fc, table, sizeof(table), &why);
      if (block.valid) {
        source.base = base;
        source.content = XXH3_64bits(table, sizeof(table));
        // The radial blur's registers, judged by SetPostRadial; a block without them is still valid.
        if (f.have_radial && !f.radial_bad) SetPostRadial(&block, f.radial[0], f.radial[1], f.radial[2]);
      }
    } else {
      why = PostBlockReason::kUnread;
    }
  }
  std::lock_guard<std::mutex> lock(g_scene_mutex);
  if (!PostFrameCurrent(f)) return;
  if (block.valid) {
    g_builder.SetPost(block, source);
    f.done = true;
  } else {
    g_builder.CountPostTry(why);
    ++f.tries;
  }
}

// --- The quad records (frame-map section 17, "Quad records"; quad_record.h) -------------------
// Read only in a frame with records and with the quads wanted (g_quads_frame), at outermost calls
// of the indexed immediate builder 0x82217EE8: the scene's furniture inside the main scene, the
// sun layer's quads between the scene and the composite.

// This thread's quad state of the current frame (the drawing thread's own, like the post's;
// g_light_tries_frame tells one frame from the next).
struct QuadFrame {
  // RB_SURFACE_INFO's pitch at the frame's first main-scene call of the builder; 0: no such call
  // yet. The layer's target is told by it (QuadTargetMatches), and before it no call outside the
  // scene is looked at (the water's reflection views come before the main scene).
  uint32_t scene_pitch = 0;
  uint32_t untold = 0;  // calls whose pixel shader could not be told
  uint32_t frame = 0;
};
constinit thread_local QuadFrame t_quad;
// At most this many calls a frame are counted as refused for a pixel shader that could not be told.
constexpr uint32_t kMaxQuadUntold = 8;

// A furniture call whose storage this thread has yet to read: the game fills the vertices and the
// indices after the builder returns (xdk_layout.h), so they are read when this thread enters its
// next hooked draw (FlushPendingQuad), as discovery's "indxdata" rows are. Everything else of the
// record was read at the call's return, where the discovery rows read it too.
struct PendingQuad {
  bool valid = false;
  uint32_t frame = 0;  // QuadFrame::frame of the call
  const PostQuadEntry* entry = nullptr;
  uint32_t vertex_storage = 0, index_storage = 0;
  float view_proj[4][4] = {};
  float ps[12][4] = {};
  uint32_t fetch[3][6] = {};
  uint32_t blend = 0, depth = 0, colour_mask = 0, colour_control = 0, alpha_ref = 0, vport_z_scale = 0, vte_cntl = 0;
};
constinit thread_local PendingQuad t_pending_quad;

// Quad records that were not handed to their frame: the frame was over when the storage was read
// (the frame's recheck under the lock), or another furniture call came before the read. Counted
// here for the quads: log line since its last print; the frame they belonged to has no set
// ("pending").
std::atomic<uint32_t> g_quad_dropped{0};
// Calls that ran a table shader somewhere else than at its kind's target (ruling 24), between the
// main scene's first call of the builder and the frame's composite: a layer kind's shader at a target
// that is not half the scene's pitch, or inside the scene. Not read, counted here for the same line.
// (The flares drawn into the water's reflection views are NOT among them: those calls come before the
// main scene, where no call outside the scene is looked at; review of plan Tasks 12 and 13, Minor 2.)
std::atomic<uint32_t> g_quad_elsewhere{0};

// The registers of a record are the bank's at any hooked draw: no buffer load names a pixel
// register of the table or vertex c0..c3 (light_block.h, kBufferLoadRanges).
constexpr bool QuadRegistersAreBankOnly() {
  for (const PostQuadEntry& e : kPostQuadTable) {
    for (size_t i = 0; i < e.register_count; ++i) {
      if (IsBufferLoadRegister(kPixelBank, e.registers[i])) return false;
    }
  }
  for (uint32_t r = 0; r < 4; ++r) {
    if (IsBufferLoadRegister(kVertexBank, r)) return false;
  }
  return true;
}
static_assert(QuadRegistersAreBankOnly(), "a quad's register is loaded from a constant buffer");
// The builder's primitive type of a quad: a triangle list (every captured furniture row).
constexpr uint32_t kQuadPrimitive = 4;

QuadFrame& CurrentQuadFrame() {
  const uint32_t frame = g_light_tries_frame.load(std::memory_order_relaxed);
  if (t_quad.frame != frame) {
    t_quad = QuadFrame{};
    t_quad.frame = frame;
  }
  return t_quad;
}

// One outermost call of the indexed builder in a frame with the quads wanted, at the hook's
// return. Which quad it is, is told by the pixel shader the draw runs (the device's two shader
// fields as ChoosePs reads them; the microcode hash through LookupPs, cached per object) and by
// where it is drawn (QuadTargetMatches, ruling 24): any other call costs those two reads and the
// lookup and nothing else, a call outside the scene before the frame's main scene or after its
// composite only this thread's frame checks.
// For a furniture call, at this moment: the vertex shader (it must be the one the pixel shader is
// checked with: spec ruling 18), the builder's arguments and the device's storage fields (a quad
// of four vertices of 20 bytes and six 16-bit indices from vertex 0, the fields this draw's:
// the pointer the builder returned at r10 is the device's, as WriteIndxRow asks), vertex c0..c3
// from the bank (not literals of the vertex shader), the kind's pixel registers through
// ReadPixelConstant (which refuses a literal), its slots' fetch constants and the seven state
// words from the device's register shadows. Every count and size comes from the arguments and is
// held to the quad's before anything is read by it; the storage's two addresses are kept and
// 80 and 12 bytes are read from them later, whole or not at all.
// The call is counted in the frame under the scene's mutex after the frame's recheck
// (FrameBuilder::QuadSeen), refused there at once when something above failed, and otherwise left
// pending for this thread's next hooked draw.
// A call whose pixel shader cannot be told may have been a quad: it is counted as a refused one
// ("unread"), at most kMaxQuadUntold a frame, so that such a frame draws no furniture rather than
// some of it. A draw without a pixel shader is no furniture quad.
void ObserveQuadDraw(const LastArgs& a, uint32_t device, bool in_scene) {
  QuadFrame& f = CurrentQuadFrame();
  uint32_t surface = 0;
  bool have_surface = false;
  if (in_scene) {
    if (!f.scene_pitch) {
      have_surface = device && ReadVirtualBe32(device + xdk::kDeviceSurfaceInfoOffset, &surface);
      if (have_surface) f.scene_pitch = surface & xdk::kSurfacePitchMask;
    }
  } else if (!f.scene_pitch || CurrentPostFrame().done) {
    return;
  }
  uint32_t vs_field = 0, ps_field = 0;
  const bool fields = device && ReadVirtualBe32(device + xdk::kVsDeviceFieldOffset, &vs_field) &&
                      ReadVirtualBe32(device + xdk::kPsDeviceFieldOffset, &ps_field);
  const uint32_t ps_obj = vs_field ? ps_field : t_gpu_ps;  // (as ChoosePs)
  if (fields && !ps_obj) return;
  const PsInfo* ps = fields ? LookupPs(ps_obj) : nullptr;
  if (!ps) {
    if (f.untold >= kMaxQuadUntold) return;
    std::lock_guard<std::mutex> lock(g_scene_mutex);
    if (f.frame != g_light_tries_frame.load(std::memory_order_relaxed)) return;
    g_builder.QuadSeen(PostQuadKind::kCount);
    g_builder.AddQuad(QuadRecord{}, QuadRecordReason::kUnread);
    ++f.untold;
    return;
  }
  const PostQuadEntry* entry = FindPostQuad(ps->hash);  // (`ps` is the cache's entry: not held across the reads below)
  if (!entry) return;
  if (!have_surface) have_surface = ReadVirtualBe32(device + xdk::kDeviceSurfaceInfoOffset, &surface);
  if (!QuadTargetMatches(*entry, in_scene, have_surface ? surface & xdk::kSurfacePitchMask : 0, f.scene_pitch)) {
    g_quad_elsewhere.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  PendingQuad q;
  q.frame = f.frame;
  q.entry = entry;
  QuadRecordReason why = QuadRecordReason::kNone;
  // The vertex shader, and that c0..c3 are the bank's for it.
  DeviceSnapshot dev;
  VsChoice vc;
  const VsInfo* vs = ReadDevice(device, &dev) && ChooseVs(dev, &vc) ? LookupVs(vc) : nullptr;
  uint64_t vs_literals[4] = {};
  if (!vs) {
    why = QuadRecordReason::kUnread;
  } else if (vs->hash != entry->vs) {
    why = QuadRecordReason::kShader;
  } else if (!ReadLiteralRegisters(kVsLayout, vc.obj, vc.variant, 0, vs_literals) || (vs_literals[0] & 0xF) != 0) {
    why = QuadRecordReason::kUnread;
  }
  // The builder's arguments (r4 the primitive type, r5 the base vertex, r6 the vertex count, r7
  // the index count, r8 the index format, r9 the vertex size, r10 where the index storage is
  // returned), then the device's storage fields.
  if (why == QuadRecordReason::kNone) {
    const bool index32 = (a.r[5] & 4) != 0;
    const bool quad = (a.r[1] & 0x3F) == kQuadPrimitive && a.r[2] == 0 && a.r[3] == kQuadVertices &&
                      a.r[4] == std::size(kQuadIndices) && !index32 && a.r[6] == kQuadStride;
    uint32_t vdwords = 0, idwords = 0, returned = 0;
    if (!quad) {
      why = QuadRecordReason::kStorage;
    } else if (!ReadVirtualBe32(device + xdk::kDeviceBeginVerticesStorageOffset, &q.vertex_storage) ||
               !ReadVirtualBe32(device + xdk::kDeviceBeginVerticesDwordsOffset, &vdwords) ||
               !ReadVirtualBe32(device + xdk::kDeviceIndexedIndexStorageOffset, &q.index_storage) ||
               !ReadVirtualBe32(device + xdk::kDeviceIndexedIndexDwordsOffset, &idwords) || !ReadVirtualBe32(a.r[7], &returned)) {
      why = QuadRecordReason::kUnread;
    } else if (q.vertex_storage == 0 || q.index_storage == 0 || returned != q.index_storage ||
               vdwords != IndxVertexDwords(kQuadVertices, kQuadStride) ||
               idwords != IndxIndexDwords(uint32_t(std::size(kQuadIndices)), false)) {
      why = QuadRecordReason::kStorage;
    }
  }
  if (why == QuadRecordReason::kNone) {
    float bank[16] = {};
    bool read = ReadConstantRegisters(VertexBank(device), 0, 4, bank);
    if (read) std::memcpy(q.view_proj, bank, sizeof(q.view_proj));
    for (size_t i = 0; read && i < entry->register_count; ++i) read = ReadPixelConstant(entry->registers[i], q.ps[i]);
    for (size_t i = 0; read && i < entry->slot_count; ++i) read = ReadTextureFetch(device, entry->slots[i], q.fetch[i]);
    auto reg = [&](uint32_t r, uint32_t* out) { return ReadVirtualBe32(device + xdk::DeviceRegShadowOffset(r), out); };
    read = read && reg(xdk::kRegBlendControl0, &q.blend) && reg(xdk::kRegDepthControl, &q.depth) &&
           reg(xdk::kRegColorMask, &q.colour_mask) && reg(xdk::kRegColorControl, &q.colour_control) &&
           reg(xdk::kRegAlphaRef, &q.alpha_ref) && reg(xdk::kRegVportZScale, &q.vport_z_scale) && reg(xdk::kRegVteCntl, &q.vte_cntl);
    if (!read) why = QuadRecordReason::kUnread;
  }
  std::lock_guard<std::mutex> lock(g_scene_mutex);
  // The frame is checked again under the lock: a call of one frame must not count in the next.
  if (f.frame != g_light_tries_frame.load(std::memory_order_relaxed)) {
    g_quad_dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  g_builder.QuadSeen(entry->kind);
  if (why != QuadRecordReason::kNone) {
    g_builder.AddQuad(QuadRecord{}, why);
    return;
  }
  // (a record still pending here was never read: its frame, this one or an earlier, lacks it)
  if (t_pending_quad.valid) g_quad_dropped.fetch_add(1, std::memory_order_relaxed);
  t_pending_quad = q;
  t_pending_quad.valid = true;
}

// The record of this thread's pending furniture call, now that the game has filled its storage:
// 80 bytes of vertices and 12 of indices from the two addresses kept at the call, each read whole
// or not at all and copied before it is judged, so that what MakeQuadRecord accepts is what the
// record holds. Handed to the builder under the scene's mutex after the frame's recheck; a record
// of a frame that ended is dropped and counted, and that frame, which was told of the call, has
// no set (FrameBuilder::Finish, "pending").
void FlushPendingQuad() {
  const PendingQuad q = t_pending_quad;
  t_pending_quad.valid = false;
  if (!q.valid || !q.entry) return;
  constexpr uint32_t kVertexBytes = kQuadVertices * kQuadStride, kIndexBytes = 2 * uint32_t(std::size(kQuadIndices));
  QuadRecordReason why = QuadRecordReason::kUnread;
  QuadRecord record{};
  const uint8_t* v = ReadVirtual(q.vertex_storage, kVertexBytes);
  const uint8_t* x = ReadVirtual(q.index_storage, kIndexBytes);
  if (v && x) {
    uint8_t vertices[kVertexBytes];
    uint16_t indices[std::size(kQuadIndices)];
    std::memcpy(vertices, v, sizeof(vertices));
    for (size_t i = 0; i < std::size(indices); ++i) indices[i] = uint16_t(uint32_t(x[2 * i]) << 8 | x[2 * i + 1]);
    QuadInputs in{};
    in.vertices = vertices;
    in.vertex_bytes = sizeof(vertices);
    in.stride = kQuadStride;
    in.indices = indices;
    in.index_count = std::size(indices);
    in.view_proj = q.view_proj;
    in.ps = q.ps;
    in.ps_count = q.entry->register_count;
    in.fetch = q.fetch;
    in.fetch_count = q.entry->slot_count;
    in.blend = q.blend;
    in.depth = q.depth;
    in.colour_mask = q.colour_mask;
    in.colour_control = q.colour_control;
    in.alpha_ref = q.alpha_ref;
    in.vport_z_scale = q.vport_z_scale;
    in.vte_cntl = q.vte_cntl;
    record = MakeQuadRecord(*q.entry, in, &why);
  }
  std::lock_guard<std::mutex> lock(g_scene_mutex);
  if (q.frame != g_light_tries_frame.load(std::memory_order_relaxed)) {
    g_quad_dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  g_builder.AddQuad(record, why);
}

// One outermost DrawIndexedVertices call outside the main scene in a frame with the quads wanted: the sun
// layer's tree occluder, if it is one (draw_record.h, "The sun layer's depth place"). Looked at only between
// the main scene's first call of the indexed builder (the scene's pitch is known from it: QuadFrame) and
// the frame's composite, as the layer's quads are: the shadow passes' draws come before, the HUD's after,
// and cost this thread's frame checks alone. Told by the pixel shader (F1EB2497108F159D) and the target (half the
// scene's pitch, as the layer's quads: ruling 24). Its geometry is read as a scene draw's (FillDrawInputs)
// and made a record (AssembleRecord), which the frame's builder matches to the scene record it draws again,
// under the scene's mutex after the frame's recheck. A call whose pixel shader cannot be told, or whose
// record cannot be made, is handed over as a failed one: the frame then has no place it can trust, and a
// consumer draws no sun layer for it (at most kMaxLayerOccluders calls a frame are counted).
void ObserveLayerOccluder(const LastArgs& a, uint32_t device) {
  QuadFrame& f = CurrentQuadFrame();
  if (!f.scene_pitch || CurrentPostFrame().done) return;
  uint32_t vs_field = 0, ps_field = 0;
  const bool fields = device && ReadVirtualBe32(device + xdk::kVsDeviceFieldOffset, &vs_field) &&
                      ReadVirtualBe32(device + xdk::kPsDeviceFieldOffset, &ps_field);
  const uint32_t ps_obj = vs_field ? ps_field : t_gpu_ps;  // (as ChoosePs)
  if (fields && !ps_obj) return;  // (a draw without a pixel shader is no tree occluder)
  const PsInfo* ps = fields ? LookupPs(ps_obj) : nullptr;
  bool read = false;
  DrawRecord record;
  if (ps) {
    if (ps->hash != kTreeOccluderPs) return;
    uint32_t surface = 0;
    if (!ReadVirtualBe32(device + xdk::kDeviceSurfaceInfoOffset, &surface)) {
      surface = 0;
    } else if ((surface & xdk::kSurfacePitchMask) * 2 != f.scene_pitch) {
      return;  // (the shader at another target: not the layer's)
    }
    if (surface) {
      DrawInputs in;
      in.func_id = kHook_D3DDevice_DrawIndexedVertices;
      in.indexed = true;
      in.prim = a.r[1];
      in.base_vertex = int32_t(a.r[2]);
      in.start = a.r[3];
      in.count = a.r[4];
      DrawShaders shaders;
      const SkipReason extra = IsSupportedPrim(in.prim) ? FillDrawInputs(device, in, &shaders) : SkipReason::kNone;
      record = AssembleRecord(in, 0);
      record.skip = ResolveSkip(record.skip, extra);
      read = record.skip == SkipReason::kNone;
    }
  }
  std::lock_guard<std::mutex> lock(g_scene_mutex);
  // The frame is checked again under the lock: a call of one frame must not count in the next.
  if (f.frame != g_light_tries_frame.load(std::memory_order_relaxed)) return;
  g_builder.LayerOccluderSeen(read ? &record : nullptr);
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
  DrawShaders shaders;
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
    extra = FillTerrainInputs(device, in.start, in.count, false, in, &shaders);
  } else if (id == kHook_DrawIndx_82207C30) {
    const uint8_t* hos = device ? ReadVirtual(device + xdk::kDeviceHosCntlOffset, 4) : nullptr;
    if (hos && (LoadBe32(hos) & 3) == xdk::kHosCntlAdaptive) {
      in.prim = kPrimQuadPatch;
      in.start = 0;
      in.count = a.r[4];
      extra = FillTerrainInputs(device, 0, in.count, true, in, &shaders);
    }
  }
  if (IsSupportedPrim(in.prim)) extra = FillDrawInputs(device, in, &shaders);
  FillMaterial(device, shaders, in);
  ReadStateWords(device, &in.state_words);
  // HasColourPass is proved only for draws of the tiled main scene (xdk_layout.h);
  // RecordDraw is reached only for those (OnDraw: in_scene) and the bracket is
  // checked again below. Unreadable words leave the draw a colour draw.
  in.depth_only = in.state_words.read && !xdk::HasColourPass(in.state_words.bin_mask_lo);
  // The registers of the frame's shadow block, once a frame (ReadShadowRegisters), and the end
  // of a shadow pass's run for the tracker: only in a frame with shadow records.
  if (g_shadows_frame.load(std::memory_order_relaxed)) {
    ShadowFrame& shadow = CurrentShadowFrame();
    shadow.tracker.Observe(0, 0xF, false, true);
    ReadShadowRegisters(id, shadow);
  }
  // The frame's fog block, at the frame's first colour draw whose pixel shader has the game's
  // fog block (shaders.ps: set for a draw whose shaders were read, never for the indexed
  // builder's), and its sky block, at the indexed builder's call that draws the sky: only in a
  // frame with the fog wanted.
  if (g_fog_frame.load(std::memory_order_relaxed)) {
    FogFrame& fog = CurrentFogFrame();
    if (id == kHook_DrawIndx_82217EE8) {
      ReadSkyBlock(device, fog);
    } else if (!in.depth_only && shaders.ps && shaders.ps->fog) {
      ReadFogBlock(device, fog);
    }
  }
  // The scene's furniture quads (moon, clouds, glare), at the indexed builder's calls: only in a
  // frame with the quads wanted.
  if (id == kHook_DrawIndx_82217EE8 && g_quads_frame.load(std::memory_order_relaxed)) ObserveQuadDraw(a, device, true);
  // Light (frame-map section 14, "Light inputs"). Only for a colour draw that got this far: a
  // draw that is skipped anyway has no use for its inputs.
  LightInputs light;
  LightReason light_reason = LightReason::kLightUnknown;
  LightBlock block{};
  bool block_tried = false;
  uint64_t block_set = 0;
  if (extra == SkipReason::kNone && in.have_shader && !in.depth_only && shaders.have_dev) {
    light_reason = FillLightInputs(device, shaders.dev, shaders.vs, shaders.ps, in.instances.active,
                                   shaders.max_vertex, &light, nullptr);
    // The frame's light block: once per frame, at the first such draw whose pixel shader has a
    // one-light family that reads the set's first light (its first light is the sun; a two- or
    // four-light shader has a lantern there and its block is invalid by design; a leaf's pixel
    // shader reads no light register: LightFamilyReadsSun) and at which the light set was loaded in this
    // scene (ReadLightBlock; never the scene's first draw whatever it is: frame-map section 14,
    // "Replays"). A read that gives no block is counted, and the next read is made at the first
    // such draw under another load of the light set, not at the next draw (a set without a sun
    // gives the same answer at every draw under it): up to LightBlockTries::kMaxSets sets a frame.
    const PsLightSpec* family = shaders.ps ? shaders.ps->light : nullptr;
    if (family && !family->multi && LightFamilyReadsSun(family->family) &&
        t_light_tries.Open(g_light_tries_frame.load(std::memory_order_relaxed))) {
      const uint64_t set = LightSetUpload();
      if (t_light_tries.ShouldTry(set)) {
        block = ReadLightBlock();
        block_tried = true;
        block_set = set;
        // A read without a block is counted here, whatever becomes of the draw below: it is never
        // made twice for a set. A read that gave the block is marked done only when the frame has
        // taken it (below, under the lock): were this draw dropped there, the frame would have no
        // block and no further read.
        if (!block.valid) t_light_tries.Tried(set, false);
      }
    }
  }
  std::lock_guard<std::mutex> lock(g_scene_mutex);
  if (!g_builder.InMainScene()) return;
  if (block_tried) {
    if (block.valid) {
      g_builder.SetLight(block);
      t_light_tries.Tried(block_set, true);  // (the frame has its block: nothing more is read for it)
    } else {
      g_builder.CountLightTry();
    }
  }
  DrawRecord r = AssembleRecord(in, g_builder.NextSeq());
  // A capture-side reason (garbage count, bad index memory) replaces
  // AssembleRecord's, which only sees the inputs filled before it.
  r.skip = ResolveSkip(r.skip, extra);
  if (r.skip == SkipReason::kUnsupportedPrim && id < kMaxHookIds) ++g_unsupported_by_hook[id];
  // A terrain draw's inputs and a lit draw's light inputs go to the frame's side arrays, not
  // into the record.
  g_builder.Add(r, TerrainInputsOf(in), light_reason == LightReason::kNone ? &light : nullptr, light_reason);
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

// "name: n" pairs of a scene's light tallies: the lit records by family, the others by reason.
std::string LightFamilies(const render::FrameScene& sc, const char* quote) {
  std::string out;
  char buf[64];
  for (size_t i = 1; i < size_t(LightFamily::kCount); ++i) {
    if (!sc.light_family[i]) continue;
    std::snprintf(buf, sizeof(buf), "%s%s%s%s: %u", out.empty() ? "" : ", ", quote, LightFamilyName(LightFamily(i)), quote,
                  sc.light_family[i]);
    out += buf;
  }
  return out;
}
std::string LightReasons(const render::FrameScene& sc, const char* quote) {
  std::string out;
  char buf[64];
  for (size_t i = 1; i < size_t(LightReason::kCount); ++i) {
    if (!sc.light_reason[i]) continue;
    std::snprintf(buf, sizeof(buf), "%s%s%s%s: %u", out.empty() ? "" : ", ", quote, LightReasonName(LightReason(i)), quote,
                  sc.light_reason[i]);
    out += buf;
  }
  return out;
}

// Discovery evidence, in the frame row of a frame with records: the frame's light block as the
// scene carries it ("valid" 0: none was read; "tries": reads that gave none) and the frame's light
// tallies: {"light":{"valid","tries","sun_dir","sun_col","eye","tint","tint_pow","add","out_scale",
// "floor","axis","mask_xform","lit","multi","families":{name: n},"reasons":{name: n}}}.
void AppendFrameLight(std::string& row, const render::FrameScene& sc) {
  const LightBlock& l = sc.light;
  char buf[96];
  auto floats = [&](const char* key, const float* v, int n) {
    row += ",\"";
    row += key;
    row += n > 1 ? "\":[" : "\":";
    for (int i = 0; i < n; ++i) {
      if (i) row += ",";
      AppendFloat(row, v[i]);
    }
    if (n > 1) row += "]";
  };
  std::snprintf(buf, sizeof(buf), ",\"light\":{\"valid\":%d,\"tries\":%u", l.valid ? 1 : 0, sc.light_tries);
  row += buf;
  if (l.valid) {
    floats("sun_dir", l.sun_dir, 3);
    floats("sun_col", l.sun_col, 3);
    floats("eye", l.eye, 3);
    floats("tint", l.tint, 3);
    floats("tint_pow", &l.tint_pow, 1);
    floats("add", l.add, 3);
    floats("out_scale", &l.out_scale, 1);
    floats("floor", l.floor, 2);
    floats("axis", l.axis, 3);
    floats("mask_xform", l.mask_xform, 4);
  }
  uint32_t lit = 0;
  for (uint32_t n : sc.light_family) lit += n;
  std::snprintf(buf, sizeof(buf), ",\"lit\":%u,\"multi\":%u,\"families\":{", lit, sc.light_multi);
  row += buf;
  row += LightFamilies(sc, "\"");
  row += "},\"reasons\":{";
  row += LightReasons(sc, "\"");
  row += "}}";
}

// Discovery evidence, in the frame row of a frame with records: the frame's shadow capture as the
// scene carries it, {"shadow":{"on","valid","reason","passes","near","far","extra",
// "skipped":[{name: n} near, {name: n} far],"by_vs":"0xHASH(reason): n, ...","kinds":[[opaque,
// alpha-test] near, [..] far],"unbiased":[near, far],"split","texel","bias":[scale, offset near,
// scale, offset far],"map":[32: the registers' rows],"clip":[32: the passes' matrices]}}; the
// numbers after "extra" only for a valid block. "kinds": the records by caster kind; "unbiased":
// the records without the polygon offset. tools/xdk_sigmatch/shadow_matrices.py reads the same
// capture's shadow rows, which is what the counts and the matrices are held against.
void AppendFrameShadow(std::string& row, const render::FrameScene& sc) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), ",\"shadow\":{\"on\":%d,\"valid\":%d,\"reason\":\"%s\",\"passes\":%u,\"near\":%zu,\"far\":%zu,"
                                  "\"extra\":%u,\"skipped\":[",
                sc.shadow_records ? 1 : 0, sc.shadow.valid ? 1 : 0, ShadowBlockReasonName(sc.shadow_reason), sc.shadow_passes,
                sc.shadow_draws[0].size(), sc.shadow_draws[1].size(), sc.shadow_extra);
  row += buf;
  for (size_t c = 0; c < 2; ++c) {
    row += c ? ",{" : "{";
    bool first = true;
    for (size_t i = 1; i < kShadowSkipCount; ++i) {
      if (!sc.shadow_skipped[c][i]) continue;
      std::snprintf(buf, sizeof(buf), "%s\"%s\":%u", first ? "" : ",", ShadowSkipName(uint8_t(i)), sc.shadow_skipped[c][i]);
      row += buf;
      first = false;
    }
    row += "}";
  }
  row += "],\"by_vs\":\"";
  row += render::ShadowSkippedByVs(sc);
  row += "\",\"kinds\":[";
  uint32_t unbiased[2] = {};
  for (size_t c = 0; c < 2; ++c) {
    uint32_t kinds[2] = {};
    for (const ShadowCaster& caster : sc.shadow_casters[c]) {
      ++kinds[caster.kind == CasterKind::kAlphaTest ? 1 : 0];
      unbiased[c] += caster.biased ? 0 : 1;
    }
    std::snprintf(buf, sizeof(buf), "%s[%u,%u]", c ? "," : "", kinds[0], kinds[1]);
    row += buf;
  }
  std::snprintf(buf, sizeof(buf), "],\"unbiased\":[%u,%u]", unbiased[0], unbiased[1]);
  row += buf;
  if (sc.shadow.valid) {
    auto floats = [&](const char* key, const float* v, int n) {
      row += ",\"";
      row += key;
      row += "\":[";
      for (int i = 0; i < n; ++i) {
        if (i) row += ",";
        AppendFloat(row, v[i]);
      }
      row += "]";
    };
    floats("split", sc.shadow.split, 4);
    floats("texel", sc.shadow.texel, 2);
    floats("bias", &sc.shadow.bias[0][0], 4);
    floats("map", &sc.shadow.world_to_map[0][0][0], 32);
    floats("clip", &sc.shadow.world_to_clip[0][0][0], 32);
  }
  row += "}";
}

// Discovery evidence, in the frame row of a frame with records: the frame's fog and sky blocks
// as the scene carries them, {"fog":{"on","valid","reason","tries","c28":[2],"c64":[3],"c65":[3],
// "c66":[3],"c67":[4],"c68":[3],"tf4":{"base","content"},"tf5":{"base","content"},"scatter":[192],
// "transmit":[768],"sky":{"valid","reason","tries","c31x","c46x","c47x","tf13":[6],"tf14":[6]}}};
// the registers, the tables' sources and the decoded tables (texel by texel, r g b) only for a
// valid fog block, the sky's numbers only for a valid sky block. "base", "content": where each
// table was read and the hash of the bytes that were decoded (FogSource), the same hash the
// capture's rows name the table by in "tfc". tools/xdk_sigmatch/fog_tables.py (block) holds all
// of it against the same frame's rows and dumps (spec criterion 2).
void AppendFrameFog(std::string& row, const render::FrameScene& sc) {
  char buf[192];
  auto floats = [&](const char* key, const float* v, int n) {
    row += ",\"";
    row += key;
    row += n > 1 ? "\":[" : "\":";
    for (int i = 0; i < n; ++i) {
      if (i) row += ",";
      AppendFloat(row, v[i]);
    }
    if (n > 1) row += "]";
  };
  std::snprintf(buf, sizeof(buf), ",\"fog\":{\"on\":%d,\"valid\":%d,\"reason\":\"%s\",\"tries\":%u", sc.fog_records ? 1 : 0,
                sc.fog.valid ? 1 : 0, FogBlockReasonName(sc.fog_reason), sc.fog_tries);
  row += buf;
  if (sc.fog.valid) {
    floats("c28", sc.fog.amount, 2);
    floats("c64", sc.fog.ext[0], 3);
    floats("c65", sc.fog.ext[1], 3);
    floats("c66", sc.fog.axis, 3);
    floats("c67", sc.fog.range, 4);
    floats("c68", sc.fog.near_colour, 3);
    for (int t = 0; t < 2; ++t) {
      std::snprintf(buf, sizeof(buf), ",\"tf%d\":{\"base\":\"0x%08X\",\"content\":\"0x%016llX\"}", t == 0 ? 4 : 5,
                    sc.fog_source.base[t], static_cast<unsigned long long>(sc.fog_source.content[t]));
      row += buf;
    }
    floats("scatter", &sc.fog.scatter[0][0], 64 * 3);
    floats("transmit", &sc.fog.transmit[0][0], 256 * 3);
  }
  std::snprintf(buf, sizeof(buf), ",\"sky\":{\"valid\":%d,\"reason\":\"%s\",\"tries\":%u", sc.sky.valid ? 1 : 0,
                SkyBlockReasonName(sc.sky_reason), sc.sky_tries);
  row += buf;
  if (sc.sky.valid) {
    floats("c31x", &sc.sky.c31x, 1);
    floats("c46x", &sc.sky.c46x, 1);
    floats("c47x", &sc.sky.c47x, 1);
    row += ",\"tf13\":";
    row += HexDwords(sc.sky.fetch[0], 6);
    row += ",\"tf14\":";
    row += HexDwords(sc.sky.fetch[1], 6);
  }
  row += "}}";
}

// Discovery evidence, in the frame row of a frame with records: the frame's post block as the
// scene carries it, {"post":{"on","valid","reason","age","tries","parts","composite":[6],
// "bright":[5],"blur":[14],"radial":[4],"table":{"base","content"},"texels":[512]}}; the registers, the
// table's source and the decoded texels only for a valid block. "composite", "bright", "blur" in
// PostBlock's order; "base", "content": where the table was read and the hash of the bytes that
// were decoded (PostSource), the same hash the composite's row names the table by in "tfc".
// tools/xdk_sigmatch/post_tables.py (block) holds all of it against the same frame's rows and
// dump (spec criterion 3).
void AppendFramePost(std::string& row, const render::FrameScene& sc) {
  char buf[192];
  auto floats = [&](const char* key, const float* v, int n) {
    row += ",\"";
    row += key;
    row += "\":[";
    for (int i = 0; i < n; ++i) {
      if (i) row += ",";
      AppendFloat(row, v[i]);
    }
    row += "]";
  };
  std::snprintf(buf, sizeof(buf), ",\"post\":{\"on\":%d,\"valid\":%d,\"reason\":\"%s\",\"age\":%u,\"tries\":%u,\"parts\":%u",
                sc.post_records ? 1 : 0, sc.post.valid ? 1 : 0, PostBlockReasonName(sc.post_reason), sc.post_age, sc.post_tries,
                unsigned(sc.post.parts));
  row += buf;
  if (sc.post.valid) {
    floats("composite", sc.post.composite, 6);
    floats("bright", sc.post.bright, 5);
    floats("blur", &sc.post.blur[0][0], 14);
    floats("radial", sc.post.radial, 4);
    std::snprintf(buf, sizeof(buf), ",\"table\":{\"base\":\"0x%08X\",\"content\":\"0x%016llX\"}", sc.post_source.base,
                  static_cast<unsigned long long>(sc.post_source.content));
    row += buf;
    floats("texels", sc.post.table, int(kPostTableTexels));
  }
  row += "}";
}

// Discovery evidence, in the frame row of a frame with records: the frame's quad records as the
// scene carries them, {"quads":{"on","reason","count","seen","taken","over","records":[{"kind",
// "layer","seq","ps","vs","corners","view_proj","regs","fetch","blend","depth","mask",
// "alpha_control","alpha_ref"}]}}. "corners" (20 floats: x y z u v of each corner), "view_proj"
// (16) and "regs" (48: the kind's registers in the table's order, zeros after them) are the
// floats' bits, eight hex digits each; "fetch" the three slots' six dwords; "alpha_ref" the
// reference's bits. "records" only for a whole set. tools/xdk_sigmatch/post_tables.py (quads
// --check) holds each record against the same frame's "indx" and "indxdata" rows (spec criterion 17).
void AppendFrameQuads(std::string& row, const render::FrameScene& sc) {
  char buf[192];
  auto bits = [&](const char* key, const float* v, int n) {
    row += ",\"";
    row += key;
    row += "\":\"";
    for (int i = 0; i < n; ++i) {
      uint32_t word;
      std::memcpy(&word, &v[i], sizeof(word));
      std::snprintf(buf, sizeof(buf), "%08X", word);
      row += buf;
    }
    row += "\"";
  };
  std::snprintf(buf, sizeof(buf), ",\"quads\":{\"on\":%d,\"reason\":\"%s\",\"count\":%u,\"seen\":%u,\"taken\":%u,\"over\":%u",
                sc.quad_records ? 1 : 0, QuadSetReasonName(sc.quad_reason), sc.quad_count, sc.quad_seen, sc.quad_taken,
                sc.quad_over);
  row += buf;
  if (sc.quad_reason == QuadSetReason::kNone) {
    row += ",\"records\":[";
    for (uint32_t i = 0; i < sc.quads.size() && i < kMaxQuadRecords; ++i) {
      const QuadRecord& q = sc.quads[i];
      std::snprintf(buf, sizeof(buf), "%s{\"kind\":\"%s\",\"layer\":%u,\"seq\":%u,\"ps\":\"0x%016llX\",\"vs\":\"0x%016llX\"",
                    i ? "," : "", PostQuadKindName(q.kind), unsigned(q.layer), unsigned(q.sequence),
                    static_cast<unsigned long long>(q.ps_hash), static_cast<unsigned long long>(q.vs_hash));
      row += buf;
      bits("corners", &q.corners[0][0], 20);
      bits("view_proj", &q.view_proj[0][0], 16);
      bits("regs", &q.ps[0][0], 48);
      row += ",\"fetch\":[";
      for (int t = 0; t < 3; ++t) {
        row += t ? "," : "";
        row += HexDwords(q.fetch[t], 6);
      }
      uint32_t ref;
      std::memcpy(&ref, &q.alpha_ref, sizeof(ref));
      std::snprintf(buf, sizeof(buf),
                    "],\"blend\":\"0x%08X\",\"depth\":\"0x%08X\",\"mask\":%u,\"alpha_control\":%u,\"alpha_ref\":\"0x%08X\"}",
                    q.blend, q.depth, q.colour_mask, q.alpha_control, ref);
      row += buf;
    }
    row += "]";
  }
  row += "}";
  // (the layer's depth place: the tree occluders seen, those that named no record, the record's index or -1)
  std::snprintf(buf, sizeof(buf), ",\"layer_depth\":{\"occluders\":%u,\"failed\":%u,\"place\":%lld}", sc.layer_occluders,
                sc.layer_occluders_failed, sc.layer_depth_place == kNoDepthPlace ? -1ll : static_cast<long long>(sc.layer_depth_place));
  row += buf;
}

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
      // Records start or stop at a frame boundary, never mid-frame. (Read with acquire, before the
      // shadows' and the fog's words: SetRecordsWanted.)
      const bool records = g_records_wanted.load(std::memory_order_acquire);
      g_records_frame.store(records, std::memory_order_relaxed);
      // The shadow passes with them, and only in a frame with records.
      const bool shadows = records && g_shadows_wanted.load(std::memory_order_relaxed);
      g_shadows_frame.store(shadows, std::memory_order_relaxed);
      g_builder.ShadowsOn(shadows);
      // The fog and sky blocks likewise.
      const bool fog = records && g_fog_wanted.load(std::memory_order_relaxed);
      g_fog_frame.store(fog, std::memory_order_relaxed);
      g_builder.FogOn(fog);
      // The post block likewise.
      const bool post = records && g_post_wanted.load(std::memory_order_relaxed);
      g_post_frame.store(post, std::memory_order_relaxed);
      g_builder.PostOn(post);
      // The quad records and the radial blur's registers, only with the post block.
      const bool quads = post && g_sun_wanted.load(std::memory_order_relaxed);
      g_quads_frame.store(quads, std::memory_order_relaxed);
      g_builder.QuadsOn(quads);
      // The next frame reads its own light block (and its own shadow state: CurrentShadowFrame).
      g_light_tries_frame.fetch_add(1, std::memory_order_relaxed);
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
  row += "}";
  // The frame's light as the records have it (a frame with records only).
  if (done->scene && done->scene->records) {
    AppendFrameLight(row, *done->scene);
    AppendFrameShadow(row, *done->scene);
    AppendFrameFog(row, *done->scene);
    AppendFramePost(row, *done->scene);
    AppendFrameQuads(row, *done->scene);
  }
  row += "}\n";
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
  ++g_capture_serial;
  {
    std::string stamp = g_path.stem().string();  // native_discovery_<date>_<time>
    stamp.erase(0, std::string("native_discovery_").size());
    g_tex_dir = dir / ("native_tex_" + stamp);
    g_tex_seen.clear();
    g_tex_count = 0;
    g_tex_content_seen.clear();
    g_tex_content_count = 0;
    g_tex_content_bytes = 0;
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
//
// Ownership of the records and their side arrays (FrameScene::terrain,
// FrameScene::lights) and of the frame's light block: while a
// frame is captured they belong to g_builder and are written only under
// g_scene_mutex. FrameBuilder::Finish (EndMainSceneFrame, under that mutex)
// moves them into a new FrameScene and gives the builder other buffers (below);
// EndMainSceneFrame fills the scene's two remaining fields and keeps only a
// pointer to const, so from then on nothing writes them while a reference to
// the scene exists. The scene is published here as a
// shared_ptr<const FrameScene>: the render thread takes its own reference
// (ScenePublisher::Latest, under the publisher's mutex) and only reads, for as
// long as it keeps that reference; pointers into the scene (TerrainOf, LightOf)
// are valid for that long and no longer. A side array is therefore never read
// while it is written: it is reachable from the render thread only between its
// last write and the drop of the scene's last reference.
// Reuse: when that last reference drops (on the render thread, or here when a
// scene is replaced before it was drawn) the scene's deleter hands the three
// buffers to the builder's spare list (render::SceneBufferPool: its own lock,
// at most four sets, the rest freed), and a later Finish takes a set,
// cleared, for the next frame. A spare is reachable from no scene. The spares
// are freed when a frame has no records (the view went off, where the scene is
// released too): the builder then keeps its one set, and nothing else.
void PublishScene(const FinishedScene& done) {
  if (!done.scene) return;
  const render::FrameScene& sc = *done.scene;
  Publisher().Publish(sc.records ? done.scene : nullptr);
  if (!sc.records) g_builder.ReleaseSpares();  // (locks the spare list only: no scene mutex needed)
  g_capture_window.Add(sc.capture_ms);
  if (sc.records) {
    ++g_light_frames;
    g_light_block_missing += sc.light.valid ? 0 : 1;
    if (sc.shadow_records) {
      ++g_shadow_frames;
      g_shadow_block_missing += sc.shadow.valid ? 0 : 1;
    }
    if (sc.fog_records) {
      ++g_fog_frames;
      if (!sc.fog.valid && sc.fog_reason < FogBlockReason::kCount) ++g_fog_block_missing[size_t(sc.fog_reason)];
      if (!sc.sky.valid && sc.sky_reason < SkyBlockReason::kCount) ++g_sky_block_missing[size_t(sc.sky_reason)];
      // A frame whose fog block was READ and refused (every read of up to kMaxFogTries, at the frame's
      // successive fogged draws, gave none; the reason is the last read's): its own line, the first
      // few times, because the 300-frame line names only its own frame (final review: run 416's one
      // "tf4-texel" frame could not be placed). Only with the fog wanted.
      if (!sc.fog.valid && sc.fog_tries && g_fog_refused_lines < kFogRefusedLines) {
        ++g_fog_refused_lines;
        REXSYS_INFO("[native] capture: frame {} fog block refused: {} reads at the frame's fogged draws gave none, the last: {} "
                    "({} drawable records; sky {}){}",
                    sc.frame, sc.fog_tries, FogBlockReasonName(sc.fog_reason), sc.draws.size(),
                    sc.sky.valid ? "valid" : SkyBlockReasonName(sc.sky_reason),
                    g_fog_refused_lines == kFogRefusedLines ? " (last such line)" : "");
      }
      // A frame WITH a block: are its two tables, now that the frame is over, still the bytes the
      // block was made of at the frame's first fogged draw? A table the game writes while its frame is
      // drawn (the design's risk 2: the moment of the read within the ring of three) would show here.
      // Two hashes of 512 and 1,024 bytes a frame, on this thread, only with the fog wanted; nothing
      // is done about a difference but to count it.
      if (sc.fog.valid) {
        ++g_fog_tables_checked;
        const uint8_t* p4 = ReadPhysical(sc.fog_source.base[0], uint32_t(kFogScatterBytes));
        const uint8_t* p5 = ReadPhysical(sc.fog_source.base[1], uint32_t(kFogTransmitBytes));
        if (!p4 || !p5 || XXH3_64bits(p4, kFogScatterBytes) != sc.fog_source.content[0] ||
            XXH3_64bits(p5, kFogTransmitBytes) != sc.fog_source.content[1]) {
          ++g_fog_tables_changed;
        }
      }
    }
  }
  if (sc.records && sc.post_records) {
    ++g_post_frames;
    if (!sc.post.valid && sc.post_reason < PostBlockReason::kCount) ++g_post_block_missing[size_t(sc.post_reason)];
    if (sc.post.valid && sc.post_age != 0) ++g_post_kept_frames;
  }
  if (sc.records && sc.quad_records) {
    ++g_quad_frames;
    if (sc.quad_reason < QuadSetReason::kCount) ++g_quad_set_missing[size_t(sc.quad_reason)];
    if (sc.post.valid && (sc.post.parts & kPostPartRadial)) ++g_quad_radial_frames;
  }
  // One line when the frame's quad set changes its state (whole, none and why), the first few
  // times: the 300-frame line names only its own frame.
  if (sc.records) {
    const QuadSetReason reason = sc.quad_records ? sc.quad_reason : QuadSetReason::kOff;
    if (reason != g_quad_last_reason && g_quad_change_lines < kQuadChangeLines) {
      ++g_quad_change_lines;
      REXSYS_INFO("[native] capture: frame {} quads changed: {}{}", sc.frame, render::QuadLine(sc),
                  g_quad_change_lines == kQuadChangeLines ? " (last such line)" : "");
    }
    g_quad_last_reason = reason;
  }
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
  // This is what the capture knows, before the renderer: every terrain record
  // is listed as "terrain" here, under "untextured by reason", also when the
  // clay pass then draws it with its terrain material (the clay log's Terrain
  // line and "textured N of M drawn" say how many it did). The log text is kept
  // as it is: the frame map quotes these lines.
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
  if (sc.records) {
    // light: the drawable records with light inputs ("lit": the capture has what their family
    // reads; nothing is drawn with it yet) by family, those without by reason and by pixel shader,
    // this frame's light block, and in how many of the frames with records since the last line no
    // block was read (light-block).
    uint32_t lit = 0;
    for (uint32_t n : sc.light_family) lit += n;
    std::string by_ps;
    for (const auto& [hash, n] : sc.light_unknown_ps) {
      std::snprintf(buf, sizeof(buf), "%s0x%016llX: %u", by_ps.empty() ? "" : ", ", static_cast<unsigned long long>(hash), n);
      by_ps += buf;
    }
    char block[160] = "missing";
    if (sc.light.valid) {
      std::snprintf(block, sizeof(block), "valid, sun (%.4f, %.4f, %.4f) colour (%.4f, %.4f, %.4f)", sc.light.sun_dir[0],
                    sc.light.sun_dir[1], sc.light.sun_dir[2], sc.light.sun_col[0], sc.light.sun_col[1], sc.light.sun_col[2]);
    }
    REXSYS_INFO("[native] capture: frame {} light: lit {} by family {{{}}} (multi {}), unknown {{{}}} by ps {{{}}} "
                "| block {} (tries {}) | {} in {} of {} frames",
                sc.frame, lit, LightFamilies(sc, ""), sc.light_multi, LightReasons(sc, ""), by_ps, block, sc.light_tries,
                kLightBlockReason, g_light_block_missing, g_light_frames);
    g_light_block_missing = 0;
    g_light_frames = 0;
    // shadow: this frame's shadow capture (render::ShadowLine: the block, the two passes' records
    // and their skipped draws by reason, the draws on the map outside the passes), the skipped
    // draws by vertex shader, the block's split and bias, and in how many of the frames with
    // shadow records since the last line there was no block (shadow-block). "off" without them.
    if (sc.shadow_records) {
      char numbers[192] = "";
      if (sc.shadow.valid) {
        std::snprintf(numbers, sizeof(numbers), " | split (%g, %g, %g, %.4f) bias near (%g, %g) far (%g, %g)", sc.shadow.split[0],
                      sc.shadow.split[1], sc.shadow.split[2], sc.shadow.split[3], sc.shadow.bias[0][0], sc.shadow.bias[0][1],
                      sc.shadow.bias[1][0], sc.shadow.bias[1][1]);
      }
      // (dropped: shadow draws, passes and extra draws of a frame that was over when they were to be
      // added, since the last line; they are in no pass and in no count of ShadowLine)
      REXSYS_INFO("[native] capture: frame {} shadow: {} | passes {} | skipped by vs {{{}}}{} | {} in {} of {} frames | "
                  "dropped at a frame's end {}", sc.frame,
                  render::ShadowLine(sc), sc.shadow_passes, render::ShadowSkippedByVs(sc), numbers, kShadowBlockReason,
                  g_shadow_block_missing, g_shadow_frames, g_shadow_dropped.exchange(0, std::memory_order_relaxed));
    } else {
      REXSYS_INFO("[native] capture: frame {} shadow: {}", sc.frame, render::ShadowLine(sc));
    }
    g_shadow_block_missing = 0;
    g_shadow_frames = 0;
    // fog: this frame's fog and sky blocks (render::FogLine: the registers, where each table was
    // read and its content hash, the sky's registers and textures), and in how many of the frames
    // with the fog wanted since the last line there was no fog block (fog-block) and no sky block
    // (sky-block), by reason. "off" without the fog.
    if (sc.fog_records || g_fog_frames) {
      std::string fog_missing, sky_missing;
      uint32_t fog_total = 0, sky_total = 0;
      for (size_t i = 0; i < size_t(FogBlockReason::kCount); ++i) {
        if (!g_fog_block_missing[i]) continue;
        fog_total += g_fog_block_missing[i];
        std::snprintf(buf, sizeof(buf), "%s%s: %u", fog_missing.empty() ? "" : ", ", FogBlockReasonName(FogBlockReason(i)),
                      g_fog_block_missing[i]);
        fog_missing += buf;
      }
      for (size_t i = 0; i < size_t(SkyBlockReason::kCount); ++i) {
        if (!g_sky_block_missing[i]) continue;
        sky_total += g_sky_block_missing[i];
        std::snprintf(buf, sizeof(buf), "%s%s: %u", sky_missing.empty() ? "" : ", ", SkyBlockReasonName(SkyBlockReason(i)),
                      g_sky_block_missing[i]);
        sky_missing += buf;
      }
      // (dropped: fog and sky reads of a frame that was over when they were to be taken, since the last line;
      // tables changed: frames with a block whose tables were other bytes when the frame was over)
      REXSYS_INFO("[native] capture: frame {} fog: {} | {} in {} of {} frames {{{}}} | {} in {} of {} frames {{{}}} | "
                  "dropped at a frame's end {} | tables changed by the frame's end in {} of {} frames",
                  sc.frame, render::FogLine(sc), kFogBlockReason, fog_total, g_fog_frames, fog_missing, kSkyBlockReason,
                  sky_total, g_fog_frames, sky_missing, g_fog_dropped.exchange(0, std::memory_order_relaxed),
                  g_fog_tables_changed, g_fog_tables_checked);
    } else {
      REXSYS_INFO("[native] capture: frame {} fog: {}", sc.frame, render::FogLine(sc));
    }
    g_fog_frames = 0;
    g_fog_tables_checked = g_fog_tables_changed = 0;
    std::fill(std::begin(g_fog_block_missing), std::end(g_fog_block_missing), 0u);
    std::fill(std::begin(g_sky_block_missing), std::end(g_sky_block_missing), 0u);
    // post: this frame's post block (render::PostLine: its age, the frame's c20.w, the composite's
    // and the bright pass's registers, where the table was read and its content hash), in how many
    // of the frames with the post block wanted since the last line there was none (post-block), by
    // reason, and in how many the block was a kept one of an earlier frame. "off" without it.
    if (sc.post_records || g_post_frames) {
      std::string post_missing;
      uint32_t post_total = 0;
      for (size_t i = 0; i < size_t(PostBlockReason::kCount); ++i) {
        if (!g_post_block_missing[i]) continue;
        post_total += g_post_block_missing[i];
        std::snprintf(buf, sizeof(buf), "%s%s: %u", post_missing.empty() ? "" : ", ", PostBlockReasonName(PostBlockReason(i)),
                      g_post_block_missing[i]);
        post_missing += buf;
      }
      // (dropped: post reads of a frame that was over when they were to be taken, since the last line)
      REXSYS_INFO("[native] capture: frame {} post: {} | {} in {} of {} frames {{{}}} | kept from an earlier frame in {} | "
                  "dropped at a frame's end {}",
                  sc.frame, render::PostLine(sc), kPostBlockReason, post_total, g_post_frames, post_missing, g_post_kept_frames,
                  g_post_dropped.exchange(0, std::memory_order_relaxed));
    } else {
      REXSYS_INFO("[native] capture: frame {} post: {}", sc.frame, render::PostLine(sc));
    }
    g_post_frames = g_post_kept_frames = 0;
    std::fill(std::begin(g_post_block_missing), std::end(g_post_block_missing), 0u);
    // quads: this frame's quad records (render::QuadLine: the set and its kinds, or why there is
    // none), in how many of the frames with the quads wanted since the last line there was no set
    // to draw (quad-record) by reason (a frame without a furniture call, "no-draw", is none and is
    // listed), in how many the post block had the radial blur's registers, the calls of a table
    // shader at another target than its kind's, and the records dropped. "off" without them.
    if (sc.quad_records || g_quad_frames) {
      std::string quad_missing;
      uint32_t quad_total = 0;
      for (size_t i = 1; i < size_t(QuadSetReason::kCount); ++i) {
        if (!g_quad_set_missing[i]) continue;
        quad_total += g_quad_set_missing[i];
        std::snprintf(buf, sizeof(buf), "%s%s: %u", quad_missing.empty() ? "" : ", ", QuadSetReasonName(QuadSetReason(i)),
                      g_quad_set_missing[i]);
        quad_missing += buf;
      }
      REXSYS_INFO("[native] capture: frame {} quads: {} | {} in {} of {} frames {{{}}} | radial registers in {} | "
                  "at another target {} | dropped {}",
                  sc.frame, render::QuadLine(sc), kQuadRecordReason, quad_total, g_quad_frames, quad_missing, g_quad_radial_frames,
                  g_quad_elsewhere.exchange(0, std::memory_order_relaxed), g_quad_dropped.exchange(0, std::memory_order_relaxed));
    } else {
      REXSYS_INFO("[native] capture: frame {} quads: {}", sc.frame, render::QuadLine(sc));
    }
    g_quad_frames = g_quad_radial_frames = 0;
    std::fill(std::begin(g_quad_set_missing), std::end(g_quad_set_missing), 0u);
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

bool ArmDiscoveryNow(uint32_t frames, uint32_t every) {
  if (frames < 1 || frames > 100000 || every < 1 || every > 64) {
    REXSYS_WARN("[native-discovery] refused: frames {} every {} out of range", frames, every);
    return false;
  }
  if (!g_enabled.load(std::memory_order_acquire)) {
    REXSYS_WARN("[native-discovery] refused: capture layer off (needs fable2_native_render=true)");
    return false;
  }
  // Claims the discovery state; set while a capture is armed, open, or waiting for its
  // environment delay, so any of those refuses the request (no queue).
  if (g_discovery_active.exchange(true)) {
    REXSYS_WARN("[native-discovery] refused: a capture is still running or pending");
    return false;
  }
  g_runtime_request.store((uint64_t(frames) << 16) | every);
  REXSYS_INFO("[native-discovery] capture requested: {} frames, every {}", frames, every);
  return true;
}

// Stored with release and read with acquire at the frame's boundary (FinishFrame's latch), where
// the shadows', the fog's and the post block's words are read after it: a caller that switches the
// records ON stores those words first (PollFrame), so a boundary that sees the records wanted also sees them,
// and no frame is recorded without its fog block for the order of three stores (final review, M6).
void SetRecordsWanted(bool wanted) {
  g_records_wanted.store(wanted, std::memory_order_release);
}

void SetShadowsWanted(bool wanted) {
  g_shadows_wanted.store(wanted, std::memory_order_relaxed);
}

void SetFogWanted(bool wanted) {
  g_fog_wanted.store(wanted, std::memory_order_relaxed);
}

void SetPostWanted(bool wanted) {
  g_post_wanted.store(wanted, std::memory_order_relaxed);
}

void SetSunWanted(bool wanted) {
  g_sun_wanted.store(wanted, std::memory_order_relaxed);
}

// --- Post discovery (frame-map section 17, "Discovery"): resolves and the post buffers' memory ---
// Rows only; nothing here is read or written unless a capture is armed, and no record is touched.
//
// A "resolve" row for every outermost call of DrawIndx2:821EF988 (the game's clear routine, which
// is entered twice a clear: frame-map section 15): its arguments r4..r10, the device's
// RB_SURFACE_INFO and the dwords the call wrote to the command buffer, taken between the device's
// command write pointer (+kDeviceCommandWriteOffset: the address of the last dword written) at
// the hook's entry and at its return, as hex in guest order ("cmd", at most kMaxResolveDwords;
// "cmd_ok" false, and no "cmd", when the pointer did not advance by 1 to that many dwords or the
// dwords cannot be read; "from" is the first dword's address).
//
// The post chain's own resolves are not issued through that routine: in the captures of
// 2026-10-09 no hooked call lies between two consecutive post rectangles (for one, between the
// "verts" row of the luminance pass 5821366EE8FDE893 and the bright pass's), although the GPU
// executes a resolve there. So the command stream between two consecutive outermost hooked draws
// of a thread is looked at too: from the end of the earlier draw's packets (the builder's own
// pointer, +kDeviceBeginVerticesWriteOffset, for BeginVertices and the indexed immediate builder,
// which do not store the device's; the device's for every other hook) to the end of the later
// one's, or to the later one's entry when it is the clear routine (whose own dwords are in its
// "resolve" row). When those dwords name a copy register (NamesCopyRegisters) they are written as
// a "cmdgap" row before the later draw's own rows: "func" the later draw's hook, "from" the first
// dword's address, "dwords" their number, "cmd" as above (at most kMaxGapDwords). A stretch that is
// not looked at is written as a "cmdgap" row without "cmd" and with "skipped": "long" (over the
// cap), "back" (the pointer went back: another segment of the command buffer, or its wrap),
// "unaligned" or "unreadable" (SkippedGapRow); a resolve issued inside one is in no row, so the
// reader counts them. "n" numbers the "resolve" and "cmdgap" rows of a frame together, from 1.
// tools/xdk_sigmatch/post_tables.py (resolves) walks the packets.
constexpr uint32_t kMaxResolveDwords = 512;
constexpr uint32_t kMaxGapDwords = 2048;
struct StreamMark {
  bool entry_valid = false;  // the device's write pointer at this thread's last outermost entry of the clear routine
  uint32_t entry = 0;
  bool end_valid = false;    // the end of this thread's last outermost hooked draw's packets
  uint32_t end = 0;
  uint32_t capture = 0;      // g_capture_serial of the capture `end` was read in
};
thread_local StreamMark t_stream;
uint32_t g_resolve_rows = 0;  // "resolve" and "cmdgap" rows of the frame being written, under g_mutex

// The dwords in (after, last] of the guest's command buffer (both the address of a dword), or
// nullptr when that is not 1 to `max_dwords` readable dwords (ClassifyStream has the rule).
// `*count` their number, or the distance of a stretch over the cap. `*skipped`, when given: why a
// stretch that holds dwords was not taken (the "skipped" word), nullptr otherwise.
const uint8_t* StreamDwords(uint32_t after, uint32_t last, uint32_t max_dwords, uint32_t* count,
                            const char** skipped = nullptr) {
  uint32_t first = 0;
  const StreamSpan span = ClassifyStream(after, last, max_dwords, &first, count);
  if (skipped) *skipped = StreamSkipName(span);
  if (span != StreamSpan::kDwords) return nullptr;
  const uint8_t* p = ReadVirtual(first, *count * 4);
  if (!p && skipped) *skipped = "unreadable";
  return p;
}

// The outermost hooked draw `id` of this thread has returned while a capture is armed.
void WriteStreamRows(uint32_t id, const LastArgs& args, uint32_t device) {
  StreamMark& m = t_stream;
  const bool clear = id == kHook_DrawIndx2_821EF988;
  const bool own_pointer = id == kHook_D3DDevice_BeginVertices || id == kHook_DrawIndx_82217EE8;
  uint32_t end = 0;
  const bool have_end =
      device && ReadVirtualBe32(device + (own_pointer ? xdk::kDeviceBeginVerticesWriteOffset
                                                      : xdk::kDeviceCommandWriteOffset), &end);
  const bool have_entry = clear && m.entry_valid;
  m.entry_valid = false;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_file) return;
  char buf[256];
  const int frame = g_frames_written.load(std::memory_order_relaxed) + 1;
  // The stream since the last hooked draw of this thread (of this capture).
  const uint32_t gap_last = clear ? m.entry : end;
  if (m.end_valid && m.capture == g_capture_serial && (clear ? have_entry : have_end)) {
    uint32_t count = 0;
    const char* skipped = nullptr;
    const uint8_t* p = StreamDwords(m.end, gap_last, kMaxGapDwords, &count, &skipped);
    if (skipped) {
      std::fputs(SkippedGapRow(frame, ++g_resolve_rows, HookName(id), skipped, m.end, gap_last, count).c_str(), g_file);
    } else if (p && NamesCopyRegisters(p, count)) {
      std::snprintf(buf, sizeof(buf), "{\"kind\":\"cmdgap\",\"frame\":%d,\"n\":%u,\"func\":\"%s\",\"from\":\"0x%08X\","
                                      "\"dwords\":%u,\"cmd\":\"",
                    frame, ++g_resolve_rows, HookName(id), m.end + 4, count);
      std::string row = buf;
      row += HexBytes(p, size_t(count) * 4);
      row += "\"}\n";
      std::fputs(row.c_str(), g_file);
    }
  }
  if (clear) {
    uint32_t surface = 0, count = 0;
    const bool have_surface = device && ReadVirtualBe32(device + xdk::kDeviceSurfaceInfoOffset, &surface);
    const uint8_t* p = have_entry && have_end ? StreamDwords(m.entry, end, kMaxResolveDwords, &count) : nullptr;
    std::snprintf(buf, sizeof(buf), "{\"kind\":\"resolve\",\"frame\":%d,\"n\":%u,\"args\":[", frame, ++g_resolve_rows);
    std::string row = buf;
    for (int i = 1; i < 8; ++i) {
      std::snprintf(buf, sizeof(buf), "%s\"0x%08X\"", i > 1 ? "," : "", args.r[i]);
      row += buf;
    }
    row += "]";
    if (have_surface) {
      std::snprintf(buf, sizeof(buf), ",\"surface\":\"0x%08X\"", surface);
      row += buf;
    }
    row += p ? ",\"cmd_ok\":true" : ",\"cmd_ok\":false";
    if (p) {
      std::snprintf(buf, sizeof(buf), ",\"from\":\"0x%08X\",\"cmd\":\"", m.entry + 4);
      row += buf;
      row += HexBytes(p, size_t(count) * 4);
      row += "\"";
    }
    row += "}\n";
    std::fputs(row.c_str(), g_file);
  }
  m.end_valid = have_end;
  m.end = end;
  m.capture = g_capture_serial;
}

// The block the post chain's small buffers live in (render::PostBufferBase), whole, as it is at the
// end of capture frame `frame`: native_tex_<stamp>/mem_12A00000_f<N>.bin, a page that cannot be read
// as zeros, with a "memdump" row ("unreadable": such pages). For finding the luminance resolve by
// content when no row names its address (post_tables.py find-luminance). Only when the emulator
// copies resolves to guest memory (--readback_resolve is fast, some or full): otherwise nothing of
// a resolve is there. Caller holds g_mutex with g_file open.
constexpr uint32_t kPostMemoryBase = 0x12A00000u, kPostMemoryEnd = 0x12E00000u;
static_assert(render::PostBufferBase(kPostMemoryBase) && render::PostBufferBase(kPostMemoryEnd - 0x1000) &&
              !render::PostBufferBase(kPostMemoryEnd));
void DumpPostMemory(int frame) {
  if (g_tex_dir.empty()) return;
  const std::string mode = rex::cvar::GetFlagByName("readback_resolve");
  if (mode != "fast" && mode != "some" && mode != "full") return;
  char name[48];
  std::snprintf(name, sizeof(name), "mem_%08X_f%d.bin", kPostMemoryBase, frame);
  std::error_code ec;
  std::filesystem::create_directories(g_tex_dir, ec);
  std::FILE* f = std::fopen((g_tex_dir / name).string().c_str(), "wb");
  if (!f) return;
  static const uint8_t kZeros[0x1000] = {};
  uint32_t unreadable = 0;
  bool ok = true;
  for (uint32_t page = kPostMemoryBase; ok && page < kPostMemoryEnd; page += 0x1000) {
    const uint8_t* p = ReadPhysical(page, 0x1000);
    if (!p) ++unreadable;
    ok = std::fwrite(p ? p : kZeros, 1, 0x1000, f) == 0x1000;
  }
  std::fclose(f);
  if (!ok) return;
  char buf[224];
  std::snprintf(buf, sizeof(buf),
                "{\"kind\":\"memdump\",\"frame\":%d,\"file\":\"%s/%s\",\"base\":\"0x%08X\",\"bytes\":%u,"
                "\"unreadable\":%u,\"readback_resolve\":\"%s\"}\n",
                frame, g_tex_dir.filename().string().c_str(), name, kPostMemoryBase, kPostMemoryEnd - kPostMemoryBase,
                unreadable, mode.c_str());
  std::fputs(buf, g_file);
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
  if (IsDrawId(id)) {
    const bool nested = t_nesting.Enter();
    if (nested) g_nested_draws.fetch_add(1, std::memory_order_relaxed);
    // The storage of this thread's last furniture quad, filled by the game since (timed: it reads
    // and takes the scene's mutex; without a pending quad this is one thread-local test).
    if (t_pending_quad.valid) {
      CaptureTimer timer;
      FlushPendingQuad();
    }
    // Discovery evidence (t_gpu_pbool): whether this builder will flush the bool shadow.
    // A pointer of an earlier entry must not outlive it (a capture that ended between the clear
    // routine's entry and its return, and a later one armed inside a later call).
    if (id == kHook_DrawIndx2_821EF988 && !nested) t_stream.entry_valid = false;
    if (g_discovery_active.load(std::memory_order_relaxed)) {
      uint32_t pending = 0;
      t_bool_flush_pending = ReadVirtualBe32(ctx.r3.u32 + xdk::kDeviceBoolPendingOffset, &pending) &&
                             (pending & xdk::kDeviceBoolPendingMaskHi) != 0;
      // The storage of this thread's last "indx" row, filled by the game since (discovery only).
      if (t_pending_indexed.valid) FlushPendingIndexed();
      // Post discovery: where the command buffer stood when the clear routine was entered (WriteStreamRows).
      if (id == kHook_DrawIndx2_821EF988 && !nested) {
        t_stream.entry_valid = g_armed.load(std::memory_order_relaxed) &&
                               ReadVirtualBe32(ctx.r3.u32 + xdk::kDeviceCommandWriteOffset, &t_stream.entry);
      }
    }
  }
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
  if (id == kHook_LoadAluConstants_8227CFC8) {
    if (!FollowAluLoad(t_last_args[id].r[1])) return;  // skipped: not timed
    CaptureTimer timer;
    OnAluLoad(t_last_args[id]);
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
  t_draw_device = device;             // for ReadPixelConstant
  if (t_bool_flush_pending) {
    // The builder wrote the device's bool shadow to the GPU (discovery evidence).
    t_bool_flush_pending = false;
    uint32_t shadow = 0;
    const bool shadow_read = ReadVirtualBe32(device + xdk::kDevicePsBoolConstantsOffset, &shadow);
    t_gpu_pbool.OnBuilderFlush(shadow_read, shadow);
  }
  // Records: only the outermost draw of a nested call chain, so geometry is
  // never captured twice, and only for a frame a consumer wants records for;
  // otherwise the draw is only counted.
  const bool render = outermost && g_render.load(std::memory_order_relaxed);
  const bool records = render && g_records_frame.load(std::memory_order_relaxed);
  const bool in_scene = ObserveMainScene(device, render && !records);
  // The pixel constants' model takes the bracket from the draws too, where
  // its uploads are followed: with records here, with discovery below (no
  // instruction is added to a draw that neither reads).
  if (records) t_ps_loads.Observe(in_scene);
  if (in_scene && records) {
    RecordDraw(id, args, device);
  } else if (records) {
    // Outside the scene, in a frame with shadow records: the two shadow passes.
    if (g_shadows_frame.load(std::memory_order_relaxed)) ObserveShadowDraw(id, args, device);
    // And in a frame with the post block wanted: the game's post rectangles (both, when both are wanted).
    if (g_post_frame.load(std::memory_order_relaxed)) ObservePostDraw(id, device);
    // And with the quads wanted: the sun layer's quads, at the indexed builder's calls.
    if (id == kHook_DrawIndx_82217EE8 && g_quads_frame.load(std::memory_order_relaxed)) ObserveQuadDraw(args, device, false);
    // And the layer's tree occluders, at DrawIndexedVertices: they name the records issued after the game's depth copy.
    if (id == kHook_D3DDevice_DrawIndexedVertices && g_quads_frame.load(std::memory_order_relaxed)) ObserveLayerOccluder(args, device);
  }
  if (!g_armed.load(std::memory_order_relaxed)) return;
  if (!records) t_ps_loads.Observe(in_scene);
  // Post discovery: every outermost draw, whatever the sampling (a resolve between two draws is one row).
  if (outermost) WriteStreamRows(id, args, device);
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
  } else if (have_dev && id == kHook_D3DDevice_BeginVertices) {
    WriteVertsRow(id, args, device, dev, in_scene);
  } else if (have_dev && id == kHook_DrawIndx_82217EE8) {
    WriteIndxRow(id, args, device, dev, in_scene);
  }
}

void OnSwap() {
  if (!g_enabled.load(std::memory_order_acquire)) return;
  SwapTimer timer;
  const uint64_t swap = g_swaps.fetch_add(1, std::memory_order_relaxed) + 1;
  // A run-time request: g_discovery_active was claimed by ArmDiscoveryNow, so no capture
  // is running or waiting for its delay; set the parameters up and let the code below
  // open the file and arm exactly as the environment path does.
  if (const uint64_t req = g_runtime_request.exchange(0); req != 0 &&
                                                          !g_armed.load(std::memory_order_relaxed)) {
    FramesTarget().store(int(req >> 16), std::memory_order_relaxed);
    EverySetting().store(req & 0xFFFF, std::memory_order_relaxed);
    g_frames_written.store(0, std::memory_order_relaxed);
    g_draw_count.store(0, std::memory_order_relaxed);
    g_failed.store(false);
    g_runtime_capture.store(true, std::memory_order_relaxed);
  }
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
    if (!g_runtime_capture.load(std::memory_order_relaxed) && elapsed < DelaySeconds()) return;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_file && !OpenLog()) {
        g_runtime_capture.store(false, std::memory_order_relaxed);
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
  g_resolve_rows = 0;  // (the "resolve" and "cmdgap" rows are numbered within a frame)
  // Post discovery: the post buffers' memory as the capture's first two frames left it.
  if (g_file && frame <= 2) DumpPostMemory(frame);
  if (g_file) std::fflush(g_file);
  if (frame >= FramesRequested()) {
    g_armed.store(false);
    g_runtime_capture.store(false, std::memory_order_relaxed);
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
