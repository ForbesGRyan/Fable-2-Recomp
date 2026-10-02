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
#include "guest_read.h"
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
std::atomic<bool> g_armed{false};      // discovery rows are being written
std::atomic<bool> g_failed{false};
std::atomic<uint32_t> g_swaps{0};      // guest frames since start
std::atomic<int> g_frames_written{0};  // discovery frames completed
std::atomic<uint64_t> g_draw_count{0};

std::mutex g_mutex;  // DrawState + discovery file
DrawState g_state;
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
      if (ctx.r5.u32 == 0x4000) g_state.vs_bank_ptr = ctx.r6.u32;
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

// Caller holds g_mutex. One decoded "draw" row (frame-map section 8) for
// DrawVertices / DrawIndexedVertices; other draw hooks have no VB objects.
void WriteDrawRow(uint32_t id, const LastArgs& args, uint32_t device, const DeviceSnapshot& dev) {
  char buf[512];
  std::string row;
  std::snprintf(buf, sizeof(buf),
                "{\"kind\":\"draw\",\"frame\":%d,\"func\":\"%s\",\"args\":[\"0x%08X\",\"0x%08X\","
                "\"0x%08X\",\"0x%08X\"],\"device\":\"0x%08X\",\"viewport\":[1120,720]",
                g_frames_written.load(std::memory_order_relaxed) + 1, HookName(id), args.r[1],
                args.r[2], args.r[3], args.r[4], device);
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
  if (id == kHook_D3DDevice_DrawIndexedVertices) {
    const uint32_t ib = dev.ib_obj;
    const uint8_t* ibo = ib ? ReadVirtual(ib, 4 * (xdk::kIbSizeDword + 1)) : nullptr;
    if (ibo) {
      const uint32_t common = LoadBe32(ibo + 4 * xdk::kIbFormatDword);
      const uint32_t addr = xdk::GpuAddress(LoadBe32(ibo + 4 * xdk::kIbAddressDword));
      const uint32_t ib_size = LoadBe32(ibo + 4 * xdk::kIbSizeDword);
      const bool index32 = (common & xdk::kIbFormatMask) != 0;
      const uint32_t isize = index32 ? 4 : 2;
      const uint64_t end = (uint64_t(args.r[3]) + args.r[4]) * isize;
      // Largest index the draw reads, evidence for the address and format
      // fields (checked offline against the vb row's vertex count). Indices are
      // little-endian words with the GPU endian swap applied on fetch.
      const uint32_t endian = (common >> xdk::kIbEndianShift) & 3;
      int64_t max_index = -1;
      uint32_t restarts = 0;
      if (end <= ib_size && args.r[4] && args.r[4] <= 0x100000u) {
        if (const uint8_t* idx = ReadPhysical(addr + args.r[3] * isize, args.r[4] * isize)) {
          for (uint32_t i = 0; i < args.r[4]; ++i) {
            const uint8_t* e = idx + isize * i;
            uint32_t v = index32 ? uint32_t(e[0]) | uint32_t(e[1]) << 8 | uint32_t(e[2]) << 16 |
                                       uint32_t(e[3]) << 24
                                 : uint32_t(e[0]) | uint32_t(e[1]) << 8;
            if (endian == 1) {  // 8in16
              v = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
            } else if (endian == 2) {  // 8in32
              v = (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
            } else if (endian == 3) {  // 16in32
              v = (v >> 16) | (v << 16);
            }
            // Strips are cut with the all-ones reset index; it is not a vertex.
            if (v == (index32 ? 0xFFFFFFFFu : 0xFFFFu)) {
              ++restarts;
              continue;
            }
            max_index = std::max<int64_t>(max_index, v);
          }
        }
      }
      std::snprintf(buf, sizeof(buf),
                    ",\"ib\":{\"obj\":\"0x%08X\",\"common\":\"0x%08X\",\"phys_addr\":%u,\"size\":%u,"
                    "\"index32\":%s,\"endian\":%u,\"base_vertex\":%u,\"start\":%u,\"count\":%u,"
                    "\"max_index\":%lld,\"restarts\":%u,\"fits\":%s,\"aligned\":%s}",
                    ib, common, addr, ib_size, index32 ? "true" : "false",
                    endian, args.r[2], args.r[3], args.r[4],
                    static_cast<long long>(max_index), restarts,
                    end <= ib_size ? "true" : "false",
                    (addr % 4 == 0 && addr < 0x20000000u) ? "true" : "false");
      row += buf;
    } else {
      row += ",\"ib\":null";
    }
  }

  // Vertex shader and its microcode.
  // Device field first; with it null the GPU-owned GpuLoadShaders load.
  const bool gpu_owned = dev.vs_obj == 0 && t_gpu_vs.obj != 0;
  const uint32_t vs = gpu_owned ? t_gpu_vs.obj : dev.vs_obj;
  uint32_t flags = 0;
  if (!vs || !ReadVirtualBe32(vs + xdk::kVsHeaderOffset, &flags)) return finish("no_vs");
  const uint32_t variant =
      gpu_owned ? t_gpu_vs.variant : ((flags & xdk::kVsVariantFlagMask) ? 1 : 0);
  const UcodeRef cand[2] = {ReadVsUcode(vs, 0), ReadVsUcode(vs, 1)};
  std::snprintf(buf, sizeof(buf), ",\"vs\":{\"obj\":\"0x%08X\",\"flags\":\"0x%08X\",\"variant\":%u,\"candidates\":[",
                vs, flags, variant);
  row += buf;
  for (int v = 0; v < 2; ++v) {
    std::snprintf(buf, sizeof(buf),
                  "%s{\"record\":\"0x%08X\",\"phys\":\"0x%08X\",\"bytes\":%u,\"hash\":\"0x%016llX\"}",
                  v ? "," : "", cand[v].record, cand[v].phys, cand[v].bytes,
                  static_cast<unsigned long long>(cand[v].hash));
    row += buf;
  }
  // The GPU runs the patched immediate copy when the last load on this thread
  // was one for this object; otherwise the object's own microcode.
  const bool immediate = !gpu_owned && t_vs_load.obj == vs;
  std::snprintf(buf, sizeof(buf), "],\"source\":\"%s\",\"copy\":\"0x%08X\"",
                immediate ? "immediate" : (gpu_owned ? "gpu_load" : "object"),
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
  std::snprintf(buf, sizeof(buf),
                ",\"pos\":{\"fetch_slot\":%u,\"offset_bytes\":%u,\"stride_bytes\":%u,\"format\":%u,"
                "\"signed\":%d,\"normalized\":%d,\"exp_adjust\":%d}",
                pos.fetch_slot, pos.offset_bytes, pos.stride_bytes, pf->format, pos.is_signed ? 1 : 0,
                pos.normalized ? 1 : 0, pos.exp_adjust);
  row += buf;

  // Vertex buffer: stream i feeds fetch slot 95 - i; its object's fetch
  // constant (base) plus the stream offset must equal the device shadow.
  if (pos.fetch_slot > xdk::kStreamFetchSlotBase ||
      xdk::kStreamFetchSlotBase - pos.fetch_slot >= xdk::kMaxStreams) {
    return finish("slot_not_a_stream");
  }
  const uint32_t stream = xdk::kStreamFetchSlotBase - pos.fetch_slot;
  const uint32_t vb = dev.stream_obj[stream];
  const uint8_t* vbo = vb ? ReadVirtual(vb, 4 * (xdk::kVbFetchDword + 2)) : nullptr;
  if (!vbo) return finish("no_vb");
  const uint32_t d0 = LoadBe32(vbo + 4 * xdk::kVbFetchDword);
  const uint32_t d1 = LoadBe32(vbo + 4 * xdk::kVbFetchDword + 4);
  const uint32_t base = xdk::GpuAddress(d0 & ~3u);
  const uint32_t size = ((d1 >> 2) & 0xFFFFFF) * 4;
  const uint32_t fc0 = dev.stream_fc[stream][0], fc1 = dev.stream_fc[stream][1];
  const uint32_t offset = (fc0 & ~3u) - base;
  const bool fc_match = (fc0 & 3) == 3 && offset < size && fc1 == d1 - offset;
  std::snprintf(buf, sizeof(buf),
                ",\"vb\":{\"stream\":%u,\"obj\":\"0x%08X\",\"phys_addr\":%u,\"size\":%u,\"offset\":%u,"
                "\"fc\":[\"0x%08X\",\"0x%08X\"],\"fc_match\":%s,\"stride_dw\":%u,\"vertices\":%u}",
                stream, vb, base, size, offset, fc0, fc1, fc_match ? "true" : "false",
                dev.stream_stride_dw[stream],
                offset < size ? (size - offset) / pos.stride_bytes : 0);
  row += buf;

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

  // First vertices of the stream (base + offset).
  const uint32_t avail = size - std::min(offset, size);
  const uint32_t elem = PositionBytes(pos.format);
  uint32_t count = 0;
  if (pos.stride_bytes && avail >= pos.offset_bytes + elem) {
    count = std::min<uint32_t>(64, (avail - pos.offset_bytes - elem) / pos.stride_bytes + 1);
  }
  const uint8_t* src = count ? ReadPhysical(base + offset, avail) : nullptr;
  Float4 verts[64];
  if (src && DecodePositions(src, avail, pos, 0, count, verts)) {
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

}  // namespace

void SetMemory(rex::memory::Memory* memory) {
  GuestMemory() = memory;
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
  if (!g_armed.load(std::memory_order_relaxed) || !IsDrawId(id)) return;
  const uint64_t n = g_draw_count.fetch_add(1, std::memory_order_relaxed);
  if (n % Every() != 0) return;
  const LastArgs& args = t_last_args[id];
  const uint32_t device = args.r[0];  // r3 of every draw hook is the device
  DeviceSnapshot dev;
  const bool have_dev = ReadDevice(device, &dev);
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_file) return;
  WriteRawRow(id, args, device, have_dev ? &dev : nullptr);
  if (have_dev &&
      (id == kHook_D3DDevice_DrawVertices || id == kHook_D3DDevice_DrawIndexedVertices)) {
    WriteDrawRow(id, args, device, dev);
  }
}

void OnSwap() {
  g_swaps.fetch_add(1, std::memory_order_relaxed);
  detail::CacheGeneration().fetch_add(1, std::memory_order_relaxed);
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
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_file) std::fflush(g_file);
  if (frame >= FramesRequested()) {
    g_armed.store(false);
    if (g_file) {
      std::fclose(g_file);
      g_file = nullptr;
    }
    REXSYS_INFO("[native-discovery] complete: {} frames written to {}", frame, g_path.string());
  }
}

}  // namespace fable2::native::capture
