#pragma once

// One captured guest draw call (pure: no SDK/GPU deps).

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "index_convert.h"
#include "instance_expand.h"
#include "material.h"
#include "position_decode.h"
#include "bone_skin.h"
#include "terrain_patch.h"

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
  kInstanceUnsupported,
  kSkinUnsupported,
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
    case SkipReason::kInstanceUnsupported: return "instance-unsupported";
    case SkipReason::kSkinUnsupported: return "skin-unsupported";
    default: return "?";
  }
}

// kDot: clip[i] = dot(row[i], p).  kCombine: clip = p.x*row0 + p.y*row1 + p.z*row2 + p.w*row3.
enum class TransformLayout : uint8_t { kDot = 0, kCombine = 1 };

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
  bool deformed = false;  // the shader moves the position first; drawn undeformed
  // Positions are bone-transformed per vertex (bone_skin.h) before rows.
  BoneSkin skin;
  // An instanced draw (instance_expand.h): vb/pos are the mesh stream; the
  // renderer builds `instances.flat_count` positions indexed by the guest
  // index (plus base vertex), so the index path is unchanged.
  InstanceSet instances;
  // A heightmap terrain patch run (terrain_patch.h): no vertex or index buffer;
  // the renderer builds the grid from the heightmap.
  TerrainPatch terrain;
  // Albedo texture and UVs (material.h); status says why a draw stays clay.
  Material material;
  SkipReason skip = SkipReason::kNone;
};

struct TransformInfo {
  uint32_t base_reg;
  TransformLayout layout;
  int pos_fetch;  // -1 = first full vertex fetch
  // The shader moves the fetched position (skinning, displacement) before
  // applying this transform: records draw it undeformed (bind pose, no wind).
  bool deformed = false;
  // Nonzero: replaces the position fetch's destination swizzle
  // (vs-transforms.json "pos_swizzle", SelectPosition).
  uint32_t pos_swizzle = 0;
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
  BoneSkin skin;               // active: per-vertex bone transform
  // The shader is a terrain shader (vs-transforms.json "terrain"); `terrain`
  // is active when its patch was built (heightmap handled).
  bool terrain_shader = false;
  TerrainPatch terrain;
  // The shader is an instancing shader (vs-transforms.json "instance");
  // `instances` is active when its set was built (mesh position and rows
  // selected, both streams and the constants read, range checked). vb/pos are
  // then the mesh stream. Without the set the draw is instance-unsupported.
  bool instance_shader = false;
  InstanceSet instances;
  Material material;
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
  r.material = in.material;
  auto skip = [&](SkipReason why) { r.skip = why; return r; };
  // Terrain: a tessellated patch draw whose shader has a terrain entry.
  const bool terrain = in.prim == kPrimQuadPatch && in.terrain_shader;
  if (!terrain && !IsSupportedPrim(in.prim)) return skip(SkipReason::kUnsupportedPrim);
  if (!in.have_shader) return skip(SkipReason::kUnknownShader);
  if (terrain) {
    if (!in.terrain.active) return skip(SkipReason::kUnknownPosFormat);
  } else {
    // An instancing shader whose set was not built: whatever the capture
    // stopped at (the entry's mesh fetch is not a readable position, the
    // mesh or the instance stream does not resolve, the rows do not match the
    // decoded fetches, the draw has no indices), the instanced draw is not
    // established and nothing is drawn from the parts that were found. The
    // capture's own reasons (bad-index, bad-memory) replace this one
    // (ResolveSkip).
    if (in.instance_shader && !in.instances.active) return skip(SkipReason::kInstanceUnsupported);
    if (!in.have_pos) return skip(SkipReason::kUnknownPosFormat);
    if (!in.have_vb || (in.indexed && !in.have_ib)) return skip(SkipReason::kNoStream);
    if (in.vb.size == 0) return skip(SkipReason::kBadMemory);
    if (in.indexed && (uint64_t(in.start) + in.count) * (in.index32 ? 4 : 2) > in.ib.size)
      return skip(SkipReason::kBadMemory);
  }
  if (!in.transform || !in.bank || in.transform->base_reg > 252) return skip(SkipReason::kNoTransform);
  if (terrain) r.terrain = in.terrain;
  r.skin = in.skin;
  if (!terrain) r.instances = in.instances;
  std::memcpy(r.rows, in.bank + size_t(in.transform->base_reg) * 4, sizeof(r.rows));
  r.layout = in.transform->layout;
  r.deformed = in.transform->deformed;
  return r;
}

// Index/vertex counts above this are garbage arguments, not draws.
inline constexpr uint32_t kMaxDrawCount = 4194304;

// Capture-side rejection of a draw's count, checked before any index read.
inline SkipReason CountSkip(uint32_t count) {
  return count > kMaxDrawCount ? SkipReason::kBadMemory : SkipReason::kNone;
}

// Capture-side range check of an instanced indexed draw, before it is
// recorded (instance_expand.h). `s` has its rows, stream and constants;
// `max_index` is the draw's largest non-reset index (-1: none) and `vertices`
// what the mesh stream holds. The largest vertex index the shader maps
// (max_index + base_vertex) must land inside the copies in the instance
// stream and the mesh; garbage constants or indices are kBadIndex. On success
// s->flat_count is the flat stream's length; one past the draw-count cap is
// kBadMemory, so nothing larger than kMaxDrawCount positions is ever built.
inline SkipReason InstanceRangeSkip(InstanceSet* s, int64_t max_index, int32_t base_vertex,
                                    uint32_t vertices) {
  s->flat_count = 0;
  if (max_index < 0 || base_vertex < 0) return SkipReason::kBadIndex;
  const uint64_t max = uint64_t(max_index) + uint32_t(base_vertex);
  if (max >= 0xFFFFFFFFull || !InstanceBoundsOk(*s, uint32_t(max), InstanceCopies(*s), vertices)) {
    return SkipReason::kBadIndex;
  }
  if (max >= kMaxDrawCount) return SkipReason::kBadMemory;
  s->flat_count = uint32_t(max) + 1;
  return SkipReason::kNone;
}

// Final skip reason: a capture-side reason (garbage count, unreadable or
// out-of-range indices) replaces AssembleRecord's, which may only reflect the
// inputs the capture stopped filling; an unsupported primitive always wins.
inline SkipReason ResolveSkip(SkipReason assembled, SkipReason capture) {
  if (capture == SkipReason::kNone || assembled == SkipReason::kUnsupportedPrim) return assembled;
  return capture;
}

}  // namespace fable2::native::capture
