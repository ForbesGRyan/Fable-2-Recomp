#pragma once

// One captured guest draw call (pure: no SDK/GPU deps).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "index_convert.h"
#include "instance_expand.h"
#include "material.h"
#include "position_decode.h"
#include "render_state.h"
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
  kDepthOnly,
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
    case SkipReason::kDepthOnly: return "depth-only";
    default: return "?";
  }
}

// DrawRecord::cut[3] of a draw without a distance cut: no squared distance is above it.
inline constexpr float kNoCut = std::numeric_limits<float>::infinity();

// The cut constants as read from the vertex constants -> the record's cut.
// Anything that is not a finite eye and a finite squared distance of at least
// zero means no cut: garbage must never remove geometry.
inline void SanitizeCut(const float in[4], float out[4]) {
  const bool ok = std::isfinite(in[0]) && std::isfinite(in[1]) && std::isfinite(in[2]) &&
                  std::isfinite(in[3]) && in[3] >= 0.0f;
  for (int k = 0; k < 3; ++k) out[k] = ok ? in[k] : 0.0f;
  out[3] = ok ? in[3] : kNoCut;
}

// kDot: clip[i] = dot(row[i], p).  kCombine: clip = p.x*row0 + p.y*row1 + p.z*row2 + p.w*row3.
enum class TransformLayout : uint8_t { kDot = 0, kCombine = 1 };

// A record's index into one of its frame's side arrays (frame_scene.h) when the
// frame holds nothing there for it. Inputs only some draws have are kept in a
// side array of the frame and indexed from the record, so that the other
// records do not carry them.
inline constexpr uint16_t kNoSideIndex = 0xFFFF;

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
  // The instancing shaders' distance cut (frame-map section 12): eye x, y, z
  // and a squared distance. The game's shader writes a NaN position for a
  // vertex whose world position, the flat position the renderer builds, is
  // farther than that from the eye; the clay vertex shader does the same.
  // cut[3] = +inf: no cut (every other draw, an entry without "cut", garbage
  // constants). Not part of any geometry cache key: the eye moves every frame.
  float cut[4] = {0.0f, 0.0f, 0.0f, kNoCut};
  // A heightmap terrain patch run (terrain_patch.h): no vertex or index buffer;
  // the renderer builds the grid from the heightmap. The patch and its material
  // inputs are entry `terrain_index` of the frame's FrameScene::terrain, read
  // through TerrainOf (frame_scene.h); kNoSideIndex: not a terrain record. Set
  // by FrameBuilder::Add, which owns the array, never by AssembleRecord.
  uint16_t terrain_index = kNoSideIndex;
  // The draw's light inputs (material.h, LightInputs) are entry `light_index` of
  // the frame's FrameScene::lights, read through LightOf (frame_scene.h);
  // kNoSideIndex: the draw has none (no light family, or an input that could
  // not be read) and is drawn without light. Set by FrameBuilder::Add, like
  // terrain_index. The two indices lie in what was padding.
  uint16_t light_index = kNoSideIndex;
  // Albedo texture and UVs (material.h); status says why a draw stays clay.
  Material material;
  // The draw's GPU render-state words as the device held them (render_state.h);
  // DecodeRenderState turns them into the state the native pass draws with.
  StateWords state_words;
  SkipReason skip = SkipReason::kNone;
  // The draw's wind inputs (wind_inputs.h, WindInputs) are entry `wind_index` of
  // the frame's FrameScene::wind (a scene record) or FrameScene::shadow_wind of
  // its cascade (a shadow record), read through WindOf / ShadowWindOf
  // (frame_scene.h); kNoSideIndex: the draw has none (the wind off, a shader
  // that does not sway, an input that could not be read) and is drawn at rest.
  // Set by FrameBuilder::Add and ::AddShadow. It lies in what was the record's
  // tail padding (foliage spec ruling 7).
  uint16_t wind_index = kNoSideIndex;
};

// 1,064 bytes with the terrain inputs (452) in every record; a frame has about
// 950 records at the bridge and each is copied into the frame on the guest
// thread. Measured, so that a field added here is noticed: inputs that only
// some draws have belong in a side array (the terrain inputs, the light
// inputs, the wind inputs: an index of 16 bits each).
static_assert(sizeof(DrawRecord) == 616, "DrawRecord grew or shrank: see the comment above");
static_assert(offsetof(DrawRecord, wind_index) == 614, "DrawRecord::wind_index is not in the tail padding: see the comment above");

// --- The sun layer's depth place (frame-map section 17, "The layer's depth place"; controller ruling 39) ---
// The game copies its scene's depth in the middle of the scene and starts its sun layer's depth from that
// copy. What it draws after the copy is not in it. Leaf draws issued after the copy (a tree the game fades
// to half between the eye and the hero: pixel c15.w 0.5) are put into the layer's depth by a draw of their
// own, the "tree occluder" (pixel shader F1EB2497108F159D at the layer's target): the same index buffer,
// range, vertex buffer and vertex c0..c3 as the scene's draw. So a tree occluder names a record of the scene
// that the game issued after its depth copy, and the first such record is where the native pass takes the
// layer's depth (nothing recorded says where the copy itself lies: it passes no hooked call).
// FrameScene::layer_depth_place holds that record's index; kNoDepthPlace: the frame has none.
inline constexpr uint32_t kNoDepthPlace = 0xFFFFFFFFu;
// The tree occluder's pixel shader (tools/xdk_sigmatch/light_formulas.py, POST_FURNITURE_SHADERS).
inline constexpr uint64_t kTreeOccluderPs = 0xF1EB2497108F159Dull;
// At most this many tree occluders a frame are looked at (three in the frames captured); more: the frame's
// place is not known.
inline constexpr uint32_t kMaxLayerOccluders = 16;
// Whether the drawing thread looks at one more call that may be a tree occluder, having handed `handed`
// of this frame to the frame's builder: one past the bound is still handed (the builder counts it failed,
// which is what tells the frame that its place is not known), none after it (each costs a record's
// assembly and the scene's mutex).
inline constexpr bool LayerOccluderLooked(uint32_t handed) { return handed <= kMaxLayerOccluders; }

enum class OccluderTwin : uint8_t { kFound, kNone, kAmbiguous };
// The record of `draws` that `occluder` (the record AssembleRecord makes of the occluder's draw) draws
// again: indexed, with the same primitive type, index format, index buffer, range, base vertex, vertex
// buffer, transform layout and rows, bit for bit; both drawable (skip none) and neither instanced nor
// skinned (their positions are built from more than these). Exactly one such record: kFound and its index
// in *index. None: kNone. More than one: kAmbiguous (which of them the game drew late cannot be told).
// *index is written only with kFound.
inline OccluderTwin FindOccluderTwin(const DrawRecord* draws, size_t count, const DrawRecord& occluder, uint32_t* index) {
  if (!draws || !index || occluder.skip != SkipReason::kNone || !occluder.indexed || occluder.instances.active ||
      occluder.skin.active) {
    return OccluderTwin::kNone;
  }
  size_t found = 0, at = 0;
  for (size_t i = 0; i < count; ++i) {
    const DrawRecord& r = draws[i];
    if (r.skip != SkipReason::kNone || !r.indexed || r.instances.active || r.skin.active) continue;
    if (r.prim != occluder.prim || r.index32 != occluder.index32 || r.base_vertex != occluder.base_vertex ||
        r.start != occluder.start || r.count != occluder.count || r.ib.phys_addr != occluder.ib.phys_addr ||
        r.ib.size != occluder.ib.size || r.vb.phys_addr != occluder.vb.phys_addr || r.vb.size != occluder.vb.size ||
        r.layout != occluder.layout || std::memcmp(r.rows, occluder.rows, sizeof(r.rows)) != 0) {
      continue;
    }
    if (found++ == 0) at = i;
  }
  if (found == 0 || at >= kNoDepthPlace) return OccluderTwin::kNone;
  if (found > 1) return OccluderTwin::kAmbiguous;
  *index = uint32_t(at);
  return OccluderTwin::kFound;
}

// --- Shadow-pass records (frame-map section 15; frame_scene.h, FrameScene::shadow_draws) ---
// A draw of one of the game's two sun shadow passes is a DrawRecord like a
// scene draw's: geometry (streams, indices, bone palette, instance set), the
// transform rows (vertex c0..c3 at the shadow draw: object, or for the
// instanced bushes world, to the light's clip space) and the state words. Of
// those the native shadow pass reads: color_control (register 0x2202: the
// alpha test, its function, alpha-to-mask and its offsets), alpha_ref, and
// su_sc_mode (the cull; bits 11 and 12, the polygon offset's enables), through
// DecodeRenderState. An alpha-tested caster's texture is the record's material:
// status textured, `fetch` the texture's fetch constant, `uv` / `uv_vb` /
// `uv_xform` its coordinates, `alpha` the texture's w with `alpha_scale`.
// terrain_index and light_index are not used (kNoSideIndex).
// What the record has no field for is in its twin entry of
// FrameScene::shadow_casters, the same index as the record.

// docs/native-renderer/shadow-casters.json "kind", as shadow_caster_table.inc
// writes it (tools/xdk_sigmatch/gen_caster_table.py, KINDS): 0 opaque (depth
// only), 1 alpha-test (the draw's alpha test on the texture's alpha decides).
enum class CasterKind : uint8_t { kOpaque = 0, kAlphaTest = 1 };

struct ShadowCaster {
  CasterKind kind = CasterKind::kOpaque;
  // The polygon offset applies to this draw (PA_SU_SC_MODE_CNTL bits 11, 12);
  // false: drawn without bias. Its pair as the device held it at the draw
  // (shadow_block.h, ShadowDrawBias): in a frame with a valid shadow block the
  // pass's pair (ShadowBlock::bias), zero when not biased.
  bool biased = false;
  float bias_scale = 0.0f;
  float bias_offset = 0.0f;
};

// Why a shadow-pass draw was not recorded: a record's reason (SkipReason, by
// its number) or one of these, numbered after them so that one tally and one
// counter array hold both.
enum class ShadowSkip : uint8_t {
  kNoCaster = uint8_t(SkipReason::kCount),  // the draw's shader pair has no entry in the caster table
  kAlphaInputs,   // an alpha-test caster whose texture, coordinates or alpha scale could not be read
  kStateUnread,   // the draw's state words or polygon offset registers could not be read
  kOverLimit,     // the pass has more records than a list holds (frame_scene.h, kMaxShadowRecords)
  kCount
};
inline constexpr size_t kShadowSkipCount = size_t(ShadowSkip::kCount);

// The name of a shadow skip code (a SkipReason's or a ShadowSkip's number).
inline const char* ShadowSkipName(uint8_t code) {
  if (code < uint8_t(SkipReason::kCount)) return SkipReasonName(SkipReason(code));
  switch (ShadowSkip(code)) {
    case ShadowSkip::kNoCaster: return "no-caster";
    case ShadowSkip::kAlphaInputs: return "alpha-inputs";
    case ShadowSkip::kStateUnread: return "state-unread";
    case ShadowSkip::kOverLimit: return "over-limit";
    default: return "?";
  }
}

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
  // The shader has a skin entry (vs-transforms.json "skin"); `skin.active` is
  // set when its layout matched the decoded fetches and the palette stream
  // resolved. Without it the draw is skin-unsupported, never a bind-pose draw.
  bool skin_shader = false;
  // The entry's "cut" constants as read (eye x, y, z, squared distance); taken
  // into the record, sanitized, only with an active instance set.
  float cut[4] = {0.0f, 0.0f, 0.0f, kNoCut};
  Material material;
  StateWords state_words;
  // A main-scene draw whose device bin mask has no colour-pass bit (HasColourPass
  // in xdk_layout.h): the game executes it only in its depth pre-pass, so the
  // native pass, which draws each record once as colour, skips it.
  bool depth_only = false;
};

// Terrain: a tessellated patch draw whose shader has a terrain entry.
inline bool IsTerrainDraw(const DrawInputs& in) { return in.prim == kPrimQuadPatch && in.terrain_shader; }

// The terrain inputs the frame keeps for the record AssembleRecord makes of
// `in` (FrameBuilder::Add): the patch of a terrain draw that was built. Null
// for every other draw: terrain fields on an ordinary draw are ignored, and a
// terrain draw without its patch is skipped (kUnknownPosFormat).
inline const TerrainPatch* TerrainInputsOf(const DrawInputs& in) {
  return IsTerrainDraw(in) && in.terrain.active ? &in.terrain : nullptr;
}

// The record of a draw. A terrain draw's patch is not in it (TerrainInputsOf).
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
  r.state_words = in.state_words;
  auto skip = [&](SkipReason why) { r.skip = why; return r; };
  const bool terrain = IsTerrainDraw(in);
  if (!terrain && !IsSupportedPrim(in.prim)) return skip(SkipReason::kUnsupportedPrim);
  if (!in.have_shader) return skip(SkipReason::kUnknownShader);
  if (in.depth_only) return skip(SkipReason::kDepthOnly);
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
    // Same for a skin shader whose skin was not established (checked after the
    // instance entry; no shader has both). The capture's own bad-index and
    // bad-memory reasons still replace it (ResolveSkip).
    if (in.skin_shader && !in.skin.active) return skip(SkipReason::kSkinUnsupported);
    if (!in.have_pos) return skip(SkipReason::kUnknownPosFormat);
    if (!in.have_vb || (in.indexed && !in.have_ib)) return skip(SkipReason::kNoStream);
    if (in.vb.size == 0) return skip(SkipReason::kBadMemory);
    if (in.indexed && (uint64_t(in.start) + in.count) * (in.index32 ? 4 : 2) > in.ib.size)
      return skip(SkipReason::kBadMemory);
  }
  if (!in.transform || !in.bank || in.transform->base_reg > 252) return skip(SkipReason::kNoTransform);
  r.skin = in.skin;
  if (!terrain) r.instances = in.instances;
  if (r.instances.active) SanitizeCut(in.cut, r.cut);
  std::memcpy(r.rows, in.bank + size_t(in.transform->base_reg) * 4, sizeof(r.rows));
  r.layout = in.transform->layout;
  // An active skin replaces the table's bind-pose approximation.
  r.deformed = in.transform->deformed && !in.skin.active;
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
