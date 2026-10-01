#pragma once

// One captured guest draw call (pure: no SDK/GPU deps).

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "index_convert.h"
#include "position_decode.h"

namespace fable2::native::capture {

enum class SkipReason : uint8_t {
  kNone,
  kNoTransform,
  kUnknownShader,
  kUnknownPosFormat,
  kUnsupportedPrim,
  kBadMemory,
  kBadIndex,
  kNoStream,
  kCount
};

inline const char* SkipReasonName(SkipReason r) {
  switch (r) {
    case SkipReason::kNone: return "none";
    case SkipReason::kNoTransform: return "no-transform";
    case SkipReason::kUnknownShader: return "unknown-shader";
    case SkipReason::kUnknownPosFormat: return "unknown-pos-format";
    case SkipReason::kUnsupportedPrim: return "unsupported-prim";
    case SkipReason::kBadMemory: return "bad-memory";
    case SkipReason::kBadIndex: return "bad-index";
    case SkipReason::kNoStream: return "no-stream";
    default: return "?";
  }
}

// kDot: clip[i] = dot(row[i], p).  kCombine: clip = p.x*row0 + p.y*row1 + p.z*row2 + p.w*row3.
enum class TransformLayout : uint8_t { kDot = 0, kCombine = 1 };

struct BufferRef {
  uint32_t phys_addr = 0;
  uint32_t size = 0;
};

struct DrawRecord {
  uint32_t seq = 0;
  uint32_t func_id = 0;
  uint32_t prim = 0;
  int32_t base_vertex = 0;
  uint32_t start = 0;
  uint32_t count = 0;
  bool indexed = false;
  bool index32 = false;
  BufferRef ib;
  BufferRef vb;
  PosLayout pos;
  uint64_t vs_hash = 0;
  float rows[16] = {};
  TransformLayout layout = TransformLayout::kDot;
  SkipReason skip = SkipReason::kNone;
};

struct TransformInfo {
  uint32_t base_reg;
  TransformLayout layout;
  int pos_fetch;  // -1 = first full vertex fetch
};

struct DrawInputs {
  uint32_t func_id = 0, prim = 0, start = 0, count = 0;
  int32_t base_vertex = 0;
  bool indexed = false;
  bool have_shader = false;
  uint64_t vs_hash = 0;
  bool have_pos = false;
  PosLayout pos;
  bool have_vb = false;
  BufferRef vb;
  bool have_ib = false;
  BufferRef ib;
  bool index32 = false;
  const TransformInfo* transform = nullptr;
  const float* bank = nullptr;  // 256 registers * 4 floats, host order
};

inline DrawRecord AssembleRecord(const DrawInputs& in, uint32_t seq) {
  DrawRecord r;
  r.seq = seq;
  r.func_id = in.func_id;
  r.prim = in.prim;
  r.base_vertex = in.base_vertex;
  r.start = in.start;
  r.count = in.count;
  r.indexed = in.indexed;
  r.index32 = in.index32;
  r.ib = in.ib;
  r.vb = in.vb;
  r.pos = in.pos;
  r.vs_hash = in.vs_hash;
  auto skip = [&](SkipReason why) { r.skip = why; return r; };
  if (!IsSupportedPrim(in.prim)) return skip(SkipReason::kUnsupportedPrim);
  if (!in.have_shader) return skip(SkipReason::kUnknownShader);
  if (!in.have_pos) return skip(SkipReason::kUnknownPosFormat);
  if (!in.have_vb || (in.indexed && !in.have_ib)) return skip(SkipReason::kNoStream);
  if (in.vb.size == 0) return skip(SkipReason::kBadMemory);
  if (in.indexed && (uint64_t(in.start) + in.count) * (in.index32 ? 4 : 2) > in.ib.size)
    return skip(SkipReason::kBadMemory);
  if (!in.transform || !in.bank || in.transform->base_reg > 252) return skip(SkipReason::kNoTransform);
  std::memcpy(r.rows, in.bank + size_t(in.transform->base_reg) * 4, sizeof(r.rows));
  r.layout = in.transform->layout;
  return r;
}

}  // namespace fable2::native::capture
