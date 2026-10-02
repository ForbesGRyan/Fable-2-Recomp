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
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
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
#include "position_decode.h"
#include "vfetch_decode.h"
#include "xdk_hook_ids.h"
#include "xdk_layout.h"

REXCVAR_DECLARE(bool, fable2_native_render);

namespace fable2::native::capture {
namespace {

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
constexpr uint32_t kDevicePsFieldOffset = 0x3194;  // SetPixelShader 0x82208D6C
constexpr uint32_t kDeviceSnapshotBytes = xdk::kVsDeviceFieldOffset + 4;  // highest field read
static_assert(kDevicePsFieldOffset < xdk::kVsDeviceFieldOffset &&
              xdk::kDeviceStreamStrideOffset + xdk::kMaxStreams <= kDeviceSnapshotBytes);

std::atomic<bool> g_enabled{false};    // native renderer on, or discovery requested
std::atomic<bool> g_render{false};     // native renderer on: build DrawRecords
std::atomic<bool> g_armed{false};      // discovery rows are being written
std::atomic<bool> g_failed{false};
std::atomic<uint64_t> g_swaps{0};      // guest frames since start (while enabled)
std::atomic<int> g_frames_written{0};  // discovery frames completed
std::atomic<uint64_t> g_draw_count{0};
std::atomic<uint64_t> g_nested_draws{0};  // draw hooks entered inside another draw hook
std::atomic<uint32_t> g_vs_bank_ptr{0};   // mirror of DrawState::vs_bank_ptr for records

std::mutex g_mutex;  // DrawState + discovery file
DrawState g_state;

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
uint32_t g_unsupported_by_hook[128] = {};  // kUnsupportedPrim records per hook id this frame
std::filesystem::path g_path;
std::FILE* g_file = nullptr;

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

thread_local std::array<LastArgs, 128> t_last_args;  // indexed by hook id (< 128)
thread_local DrawNesting t_nesting;  // only the outermost draw of a call chain is recorded

// World-view-projection constants per vertex shader hash, found offline by
// tools/xdk_sigmatch/matrix_finder.py (frame-map section 9).
struct TableEntry {
  uint64_t hash;
  uint32_t base;
  uint8_t layout;
  int pos_fetch;
  uint8_t deformed;  // the shader moves the position first (vs-transforms.json "deformed")
};
static constexpr TableEntry kTransformTable[] = {
#define FABLE2_VS_TRANSFORM(H, B, L, P, D) {H, B, L, P, D},
#include "vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
};

const TransformInfo* FindTransform(uint64_t hash) {
  static const std::unordered_map<uint64_t, TransformInfo> table = [] {
    std::unordered_map<uint64_t, TransformInfo> m;
    for (const TableEntry& e : kTransformTable) {
      m[e.hash] = TransformInfo{e.base, TransformLayout(e.layout), e.pos_fetch, e.deformed != 0};
    }
    return m;
  }();
  const auto it = table.find(hash);
  return it == table.end() ? nullptr : &it->second;
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
  s->ps_obj = LoadBe32(d + kDevicePsFieldOffset);
  return true;
}

void UpdateState(uint32_t id, const PPCContext& ctx) {
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
      if (ctx.r5.u32 == 0x4000) {
        g_state.vs_bank_ptr = ctx.r6.u32;
        g_vs_bank_ptr.store(ctx.r6.u32, std::memory_order_relaxed);
      }
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
  std::snprintf(buf, sizeof(buf), ",\"vs_bank_ptr\":\"0x%08X\",\"device_dwords\":{\"0x%X\":",
                s.vs_bank_ptr, kDeviceDumpOffset);
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

// Variant `v` of a vertex shader object's microcode (xdk_layout.h).
UcodeRef ReadVsUcode(uint32_t obj, uint32_t v) {
  UcodeRef u;
  uint32_t base = 0, rec_off = 0, offset = 0, size = 0;
  const uint32_t header = obj + xdk::kVsHeaderOffset;
  if (!ReadVirtualBe32(obj + 4 * xdk::kVsUcodeBaseDword, &base) ||
      !ReadVirtualBe32(header + xdk::kVsRecordOffsetField + 8 * v, &rec_off)) {
    return u;
  }
  u.record = header + rec_off;
  if (!ReadVirtualBe32(u.record + 4 * xdk::kVsUcodeAddressDword, &offset) ||
      !ReadVirtualBe32(u.record + 4 * xdk::kVsUcodeSizeDword, &size)) {
    return u;
  }
  u.bytes = size << xdk::kVsUcodeSizeShift;
  u.phys = xdk::GpuAddress(base + offset);
  if (u.bytes == 0 || u.bytes % 4 || u.bytes > 0x40000) return u;
  const uint8_t* p = ReadPhysical(u.phys, u.bytes);
  if (!p) return u;
  u.hash = XXH3_64bits(p, u.bytes);
  u.dwords.resize(u.bytes / 4);
  for (size_t i = 0; i < u.dwords.size(); ++i) u.dwords[i] = LoadBe32(p + 4 * i);
  u.ok = true;
  return u;
}

// The last vertex shader the XDK loaded with IM_LOAD_IMMEDIATE on this thread
// (patched copy in the command buffer, xdk_layout.h). Draws that do not reload
// the shader keep using it on the GPU.
struct VsLoad {
  uint32_t obj = 0;
  uint32_t copy = 0;  // guest virtual address of the copy
  uint64_t hash = 0;
  std::vector<uint32_t> dwords;  // host order
};
thread_local VsLoad t_vs_load;

void OnVsLoadImmediate(const LastArgs& a) {
  const uint32_t device = a.r[0], obj = a.r[2], variant = a.r[7];
  t_vs_load.obj = 0;
  const UcodeRef tmpl = obj && variant < 2 ? ReadVsUcode(obj, variant) : UcodeRef{};
  uint32_t write = 0;
  if (!tmpl.ok || !ReadVirtualBe32(device + xdk::kDeviceCommandWriteOffset, &write)) return;
  const uint32_t n = tmpl.bytes / 4;
  const uint32_t copy = write + 4 - tmpl.bytes;
  const uint8_t* p = ReadVirtual(copy - 12, tmpl.bytes + 12);
  if (!p) return;
  const uint32_t header = xdk::kImLoadImmediateHeader | ((n + 1) << 16);
  if ((LoadBe32(p) & ~1u) != header || LoadBe32(p + 4) != 0 || LoadBe32(p + 8) != n) return;
  p += 12;
  t_vs_load.obj = obj;
  t_vs_load.copy = copy;
  t_vs_load.hash = XXH3_64bits(p, tmpl.bytes);
  t_vs_load.dwords.resize(n);
  for (uint32_t i = 0; i < n; ++i) t_vs_load.dwords[i] = LoadBe32(p + 4 * i);
}

// The last vertex shader GpuLoadShaders (0x82221978: r3 device, r4 vertex
// shader, r5 pixel shader) loaded on this thread. Gameplay indexed draws use
// this GPU-owned path with device+0x3198 null (xdk_layout.h, kVsFallback*).
struct GpuVsLoad {
  uint32_t obj = 0;
  uint32_t variant = 0;
};
thread_local GpuVsLoad t_gpu_vs;

void OnGpuLoadShaders(const LastArgs& a) {
  const uint32_t vs = a.r[1], ps = a.r[2];
  uint32_t flags = 0;
  t_gpu_vs = {};
  if (!vs || !ReadVirtualBe32(vs + xdk::kVsHeaderOffset, &flags)) return;
  t_gpu_vs.obj = vs;
  // 0x822219BC..0x822219C8: the variant flag applies only without a pixel
  // shader; 0x82221A10 forces variant 0 otherwise.
  t_gpu_vs.variant = (!ps && (flags & xdk::kVsVariantFlagMask)) ? 1 : 0;
}

// Index buffer object fields (xdk_layout.h).
struct IbView {
  uint32_t common = 0;
  uint32_t addr = 0;  // GPU physical
  uint32_t size = 0;  // bytes
  uint32_t endian = 0;
  bool index32 = false;
};

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

struct IndexScan {
  int64_t max_index = -1;  // largest non-reset index, -1 if none
  uint32_t restarts = 0;
  uint32_t n_first = 0;  // first non-reset indices, for discovery positions
  uint32_t first[64] = {};
};

// Reads indices [start, start + count) as the GPU fetches them: little-endian
// words with the buffer's endian swap applied. False if the range is empty,
// too large, outside the buffer or unreadable.
bool ScanIndices(const IbView& ib, uint32_t start, uint32_t count, IndexScan* out) {
  const uint32_t isize = ib.index32 ? 4 : 2;
  if (!count || count > kMaxDrawCount || (uint64_t(start) + count) * isize > ib.size) return false;
  const uint8_t* idx = ReadPhysical(ib.addr + start * isize, count * isize);
  if (!idx) return false;
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* e = idx + isize * i;
    uint32_t v = ib.index32 ? uint32_t(e[0]) | uint32_t(e[1]) << 8 | uint32_t(e[2]) << 16 |
                                  uint32_t(e[3]) << 24
                            : uint32_t(e[0]) | uint32_t(e[1]) << 8;
    if (ib.endian == 1) {  // 8in16
      v = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
    } else if (ib.endian == 2) {  // 8in32
      v = (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
    } else if (ib.endian == 3) {  // 16in32
      v = (v >> 16) | (v << 16);
    }
    // Strips are cut with the all-ones reset index; it is not a vertex.
    if (v == (ib.index32 ? 0xFFFFFFFFu : 0xFFFFu)) {
      ++out->restarts;
      continue;
    }
    out->max_index = std::max<int64_t>(out->max_index, v);
    if (out->n_first < 64) out->first[out->n_first++] = v;
  }
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

// The vertex buffer feeding a fetch slot: stream i feeds slot 95 - i; its
// object's fetch constant (base) plus the stream offset must equal the device
// shadow (fc_match).
struct StreamView {
  uint32_t stream = 0;
  uint32_t obj = 0;
  uint32_t base = 0;  // GPU physical, without the stream offset
  uint32_t size = 0;  // bytes from base
  uint32_t offset = 0;
  uint32_t fc0 = 0, fc1 = 0;
  bool fc_match = false;
};

// nullptr, or the reason the stream cannot be resolved.
const char* ResolveStream(const DeviceSnapshot& dev, uint32_t fetch_slot, StreamView* v) {
  if (fetch_slot > xdk::kStreamFetchSlotBase ||
      xdk::kStreamFetchSlotBase - fetch_slot >= xdk::kMaxStreams) {
    return "slot_not_a_stream";
  }
  v->stream = xdk::kStreamFetchSlotBase - fetch_slot;
  v->obj = dev.stream_obj[v->stream];
  const uint8_t* vbo = v->obj ? ReadVirtual(v->obj, 4 * (xdk::kVbFetchDword + 2)) : nullptr;
  if (!vbo) return "no_vb";
  const uint32_t d0 = LoadBe32(vbo + 4 * xdk::kVbFetchDword);
  const uint32_t d1 = LoadBe32(vbo + 4 * xdk::kVbFetchDword + 4);
  v->base = xdk::GpuAddress(d0 & ~3u);
  v->size = ((d1 >> 2) & 0xFFFFFF) * 4;
  v->fc0 = dev.stream_fc[v->stream][0];
  v->fc1 = dev.stream_fc[v->stream][1];
  v->offset = (v->fc0 & ~3u) - v->base;
  v->fc_match = (v->fc0 & 3) == 3 && v->offset < v->size && v->fc1 == d1 - v->offset;
  return nullptr;
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
  std::snprintf(buf, sizeof(buf), "],\"source\":\"%s\",\"copy\":\"0x%08X\"",
                immediate ? "immediate" : (vc.gpu_owned ? "gpu_load" : "object"),
                immediate ? t_vs_load.copy : 0);
  row += buf;
  // Evidence for the template location: the copy differs from the object's
  // microcode only in the patched vertex fetch dwords.
  if (immediate && cand[variant].ok && cand[variant].dwords.size() == t_vs_load.dwords.size()) {
    row += ",\"template_diff\":[";
    int shown = 0;
    for (size_t i = 0; i < t_vs_load.dwords.size(); ++i) {
      if (t_vs_load.dwords[i] == cand[variant].dwords[i]) continue;
      std::snprintf(buf, sizeof(buf), "%s%zu", shown ? "," : "", i);
      row += buf;
      if (++shown == 32) break;
    }
    row += "]";
  }
  row += "}";
  const uint64_t hash = immediate ? t_vs_load.hash : cand[variant].hash;
  const std::vector<uint32_t>& ucode = immediate ? t_vs_load.dwords : cand[variant].dwords;
  if (!immediate && !cand[variant].ok) return finish("no_ucode");
  std::snprintf(buf, sizeof(buf), ",\"vs_hash\":\"0x%016llX\",\"vs_dwords\":%zu",
                static_cast<unsigned long long>(hash), ucode.size());
  row += buf;

  // Position element.
  PosLayout pos;
  std::vector<VertexFetch> fetches = DecodeVertexFetches(ucode.data(), ucode.size());
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
};
thread_local std::unordered_map<uint32_t, VsInfo> t_vs_cache;

void FillShader(VsInfo& e, const uint32_t* ucode, size_t dwords, uint64_t hash) {
  e.hash = hash;
  e.transform = FindTransform(hash);
  const std::vector<VertexFetch> fetches = DecodeVertexFetches(ucode, dwords);
  e.have_pos = SelectPosition(fetches, e.transform ? e.transform->pos_fetch : -1, &e.pos);
}

const VsInfo* LookupVs(const VsChoice& c) {
  if (t_vs_cache.size() > 4096) t_vs_cache.clear();
  VsInfo& e = t_vs_cache[c.obj];
  if (c.immediate) {
    if (e.immediate && e.hash && e.hash == t_vs_load.hash) return &e;
    e = VsInfo{};
    e.immediate = true;
    FillShader(e, t_vs_load.dwords.data(), t_vs_load.dwords.size(), t_vs_load.hash);
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

thread_local float t_bank[1024];  // vertex constants, host floats (only the transform window is filled)

// Fills `in` for DrawIndexedVertices / DrawVertices (prim and counts already
// set). Returns a skip reason AssembleRecord does not check (garbage counts,
// indices outside the position stream), or kNone.
SkipReason FillDrawInputs(uint32_t device, DrawInputs& in) {
  if (const SkipReason c = CountSkip(in.count); c != SkipReason::kNone) return c;
  DeviceSnapshot dev;
  if (!ReadDevice(device, &dev)) return SkipReason::kNone;  // have_shader stays false
  VsChoice vc;
  const VsInfo* vs = ChooseVs(dev, &vc) ? LookupVs(vc) : nullptr;
  if (!vs) return SkipReason::kNone;
  in.have_shader = true;
  in.vs_hash = vs->hash;
  in.transform = vs->transform;
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
    // draws whose first fetch is per-instance data) or are unreadable.
    IndexScan scan;
    if (!ScanIndices(ib, in.start, in.count, &scan)) {
      extra = SkipReason::kBadMemory;
    } else if (scan.max_index >= 0 &&
               (scan.max_index + in.base_vertex >= int64_t(vertices) || in.base_vertex < 0)) {
      extra = SkipReason::kBadIndex;
    }
  } else if (uint64_t(in.start) + in.count > vertices) {
    extra = SkipReason::kBadIndex;
  }
  // Bank: only the transform's four registers are converted.
  if (in.transform && in.transform->base_reg <= 252) {
    const uint32_t bank_ptr = g_vs_bank_ptr.load(std::memory_order_relaxed);
    const uint32_t at = (bank_ptr ? bank_ptr : device + xdk::kDeviceVsConstantsOffset) +
                        16 * in.transform->base_reg;
    if (const uint8_t* p = ReadVirtual(at, 64)) {
      float* rows = t_bank + 4 * in.transform->base_reg;
      for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t bits = LoadBe32(p + 4 * i);
        std::memcpy(&rows[i], &bits, 4);
      }
      in.bank = t_bank;
    } else {
      extra = SkipReason::kBadMemory;
    }
  }
  return extra;
}

// One outermost guest draw inside the main scene, as a DrawRecord in the frame.
// Draw-argument mapping (frame-map section 8): DrawIndexedVertices r4 prim, r5
// base vertex, r6 start index, r7 index count; DrawVertices r4 prim, r5 start
// vertex, r6 vertex count. Other draw hooks: argument mapping unconfirmed, so
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
  }
  if (IsSupportedPrim(in.prim)) extra = FillDrawInputs(device, in);
  std::lock_guard<std::mutex> lock(g_scene_mutex);
  if (!g_builder.InMainScene()) return;
  DrawRecord r = AssembleRecord(in, g_builder.NextSeq());
  // A capture-side reason (garbage count, bad index memory) replaces
  // AssembleRecord's, which only sees the inputs filled before it.
  r.skip = ResolveSkip(r.skip, extra);
  if (r.skip == SkipReason::kUnsupportedPrim && id < 128) ++g_unsupported_by_hook[id];
  g_builder.Add(r);
}

// Device tiling flag byte, or nullptr if unreadable.
const uint8_t* ReadTilingFlag(uint32_t device) {
  return device ? ReadVirtual(device + xdk::kDeviceTilingFlagOffset, 1) : nullptr;
}

// Reads the device tiling flag at a guest draw (the draw functions only read
// it), moves the main-scene bracket and returns whether the draw is in it.
// Evidence is collected while discovery is armed.
bool ObserveMainScene(uint32_t device) {
  const uint8_t* flag = ReadTilingFlag(device);
  const bool tiling = flag && TilingActive(*flag);
  if (!g_armed.load(std::memory_order_relaxed)) {
    std::lock_guard<std::mutex> lock(g_scene_mutex);
    return ObserveDraw(g_builder, tiling, g_bracket);
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
  uint32_t unsupported_by_hook[128] = {};
};

// Ends the frame's bracket and, with the renderer on, finishes the frame's
// scene (`swap` = guest frame number). With `write` (discovery armed, caller
// holds g_mutex with g_file open) writes the frame row (frame-map section 8):
// captured = hooked guest draw calls, in_bracket = those in the main scene.
void EndMainSceneFrame(bool write, int frame, uint64_t swap, FinishedScene* done) {
  BracketStats s;
  SceneEvidence e;
  {
    std::lock_guard<std::mutex> lock(g_scene_mutex);
    s = EndFrame(g_builder, g_bracket);
    e = g_evidence;
    g_evidence = {};
    if (g_render.load(std::memory_order_relaxed)) {
      done->scene = g_builder.Finish(swap);
      std::copy(std::begin(g_unsupported_by_hook), std::end(g_unsupported_by_hook),
                std::begin(done->unsupported_by_hook));
      std::fill(std::begin(g_unsupported_by_hook), std::end(g_unsupported_by_hook), 0u);
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
  REXSYS_INFO("[native-discovery] writing {} frames to {}", FramesRequested(), g_path.string());
  return true;
}

// Publishes the frame's scene; every 300 guest frames logs its coverage
// (frame-map section 9).
void PublishScene(const FinishedScene& done) {
  if (!done.scene) return;
  Publisher().Publish(done.scene);
  const render::FrameScene& sc = *done.scene;
  if (sc.frame % 300 != 0) return;
  std::string skipped, unsupported;
  char buf[96];
  for (size_t i = 1; i < size_t(SkipReason::kCount); ++i) {
    if (!sc.skipped[i]) continue;
    std::snprintf(buf, sizeof(buf), "%s%s: %u", skipped.empty() ? "" : ", ",
                  SkipReasonName(SkipReason(i)), sc.skipped[i]);
    skipped += buf;
  }
  for (uint32_t id = 0; id < 128; ++id) {
    if (!done.unsupported_by_hook[id]) continue;
    std::snprintf(buf, sizeof(buf), "%s%s: %u", unsupported.empty() ? "" : ", ", HookName(id),
                  done.unsupported_by_hook[id]);
    unsupported += buf;
  }
  size_t deformed = 0;
  for (const DrawRecord& r : sc.draws) deformed += r.deformed ? 1 : 0;
  REXSYS_INFO(
      "[native] capture: frame {} captured {} drawable {} (deformed {}) skipped {{{}}} nested_total {}",
      sc.frame, sc.captured, sc.draws.size(), deformed, skipped,
      g_nested_draws.load(std::memory_order_relaxed));
  if (!unsupported.empty()) {
    REXSYS_INFO("[native] capture: frame {} unsupported by hook {{{}}}", sc.frame, unsupported);
  }
}

}  // namespace

render::ScenePublisher& Publisher() {
  static render::ScenePublisher publisher;
  return publisher;
}

void SetMemory(rex::memory::Memory* memory) {
  GuestMemory() = memory;
  g_render.store(REXCVAR_GET(fable2_native_render), std::memory_order_relaxed);
  g_enabled.store(REXCVAR_GET(fable2_native_render) || FramesRequested() > 0,
                  std::memory_order_relaxed);
}

void OnXdkCall(uint32_t id, PPCContext& ctx, uint8_t*) {
  if (!g_enabled.load(std::memory_order_relaxed) || id >= t_last_args.size()) return;
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
      UpdateState(id, ctx);
      break;
    default:
      break;
  }
}

void OnXdkReturn(uint32_t id, PPCContext&, uint8_t*) {
  if (!g_enabled.load(std::memory_order_relaxed) || id >= t_last_args.size()) return;
  if (id == kHook_VsLoadImmediate_821DFDE0) {
    OnVsLoadImmediate(t_last_args[id]);
    return;
  }
  if (id == kHook_GpuLoadShaders_82221978) {
    OnGpuLoadShaders(t_last_args[id]);
    return;
  }
  if (id == kHook_IndirectBuffer_82286248) {
    ObserveIndirectBuffer(t_last_args[id].r[0]);
    return;
  }
  if (!IsDrawId(id)) return;
  const bool outermost = t_nesting.Leave();
  const LastArgs& args = t_last_args[id];
  const uint32_t device = args.r[0];  // r3 of every draw hook is the device
  const bool in_scene = ObserveMainScene(device);
  // Records: only the outermost draw of a nested call chain, so geometry is
  // never captured twice.
  if (in_scene && outermost && g_render.load(std::memory_order_relaxed)) {
    RecordDraw(id, args, device);
  }
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
  }
}

void OnSwap() {
  if (!g_enabled.load(std::memory_order_relaxed)) return;
  const uint64_t swap = g_swaps.fetch_add(1, std::memory_order_relaxed) + 1;
  detail::CacheGeneration().fetch_add(1, std::memory_order_relaxed);
  if (!g_armed.load(std::memory_order_relaxed)) {
    FinishedScene done;
    EndMainSceneFrame(false, 0, swap, &done);
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
      if (!g_file && !OpenLog()) return;
    }
    g_armed.store(true);
    REXSYS_INFO("[native-discovery] armed after {:.1f} s", elapsed);
    return;
  }
  // One guest frame of rows has completed.
  const int frame = g_frames_written.fetch_add(1) + 1;
  FinishedScene done;
  std::unique_lock<std::mutex> lock(g_mutex);
  EndMainSceneFrame(g_file != nullptr, frame, swap, &done);
  if (g_file) std::fflush(g_file);
  if (frame >= FramesRequested()) {
    g_armed.store(false);
    if (g_file) {
      std::fclose(g_file);
      g_file = nullptr;
    }
    REXSYS_INFO("[native-discovery] complete: {} frames written to {}", frame, g_path.string());
  }
  lock.unlock();
  PublishScene(done);
}

}  // namespace fable2::native::capture
