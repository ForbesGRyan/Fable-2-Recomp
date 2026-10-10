#pragma once

// The frame's quad records (pure): the game's sky furniture and the quads of its half-size sun
// layer (frame-map section 17, "Quad records"; docs/native-renderer/lighting-formulas.md, "The sun
// layer and the sky furniture"). The game draws each of them through the indexed immediate builder
// 0x82217EE8 (xdk_layout.h) as four vertices of 20 bytes (position x y z, coordinate u v) and the
// six indices 0 1 2 1 3 2; which of them a call is, is told by the pixel shader it runs, from a
// generated table of the shaders that have a machine check (post_quad_table.inc, spec ruling 18).
// A record holds what the native pass needs to draw the quad again: the corners as the game
// filled the storage, the vertex registers c0..c3, the pixel registers its shader reads, its
// textures' fetch constants and the draw's state words, the alpha test's among them.
// They are a bounded block of their own beside the frame's draw records (FrameScene::quads): no
// DrawRecord grows for them.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace fable2::native::capture {

// The most quad records a frame holds. The captures have 6 in the main scene (moon, three cloud
// layers, the sun's and the moon's glare) and up to 11 at the sun layer (three cloud occluders, the
// beam source, up to five flares, moon glow, sun disc): 17. A frame with more has none (QuadSet).
inline constexpr uint32_t kMaxQuadRecords = 24;

// Reason string (the plan's): a frame with the quads wanted whose set is not whole, and a call
// whose record was refused.
inline constexpr const char* kQuadRecordReason = "quad-record";

// The kinds, in the order of the generated table (light_formulas.POST_QUAD_SHADERS).
enum class PostQuadKind : uint8_t { kMoon, kCloud, kGlare, kBeamSource, kFlare, kMoonGlow, kSunDisc, kCloudOccluder, kCount };
// Where a kind is drawn: the main scene's target, or one of the sun layer's (its depth with colour
// mask 0, the beam source's picture, the picture of flares, moon glow and sun disc).
enum class PostQuadTarget : uint8_t { kScene, kLayerDepth, kLayer1, kLayer2 };
// How it is drawn: source alpha / inverse source alpha (blend word 0x07060706), one / one
// (0x01010101), no blend (0x00010001), or nothing but depth.
enum class PostQuadBlend : uint8_t { kAlpha, kAdd, kReplace, kDepth };

inline const char* PostQuadKindName(PostQuadKind k) {
  static constexpr const char* kNames[] = {"moon", "cloud", "glare", "beam-source", "flare", "moon-glow", "sun-disc",
                                           "cloud-occluder"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == size_t(PostQuadKind::kCount));
  return k < PostQuadKind::kCount ? kNames[size_t(k)] : "?";
}

// One line of post_quad_table.inc (tools/xdk_sigmatch/gen_post_quad_table.py has the columns).
// alpha_test: what every captured row of the shader had; a record carries the draw's own words
// (ruling 23) and nothing is decided by this flag.
struct PostQuadEntry {
  uint64_t ps, vs;
  PostQuadKind kind;
  uint8_t layer;  // 0 the main scene, 1 the sun layer
  PostQuadTarget target;
  PostQuadBlend blend;
  bool alpha_test;
  uint8_t slot_count;
  uint8_t slots[3];
  uint8_t register_count;
  uint8_t registers[12];
};

inline constexpr PostQuadEntry kPostQuadTable[] = {
#define FABLE2_POST_QUAD(ps, vs, kind, layer, target, blend, test, slot_count, s0, s1, s2, register_count, r0, r1, r2, r3, r4, r5, \
                         r6, r7, r8, r9, r10, r11)                                                                              \
  {ps, vs, PostQuadKind::k##kind, layer, PostQuadTarget::k##target, PostQuadBlend::k##blend, (test) != 0, slot_count,           \
   {s0, s1, s2}, register_count, {r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11}},
#include "post_quad_table.inc"
#undef FABLE2_POST_QUAD
};

// What a reader relies on: the table's line i is kind i (so a kind is a table index), no hash is
// zero or listed twice, the counts fit the record, the registers ascend below the literals (c252
// on), the slots are fetch slots, a scene kind is at the scene's target and a layer kind at one of
// the layer's, and "depth" is the blend of the layer's depth and of nothing else.
constexpr bool PostQuadTableIsWellFormed() {
  if (std::size(kPostQuadTable) != size_t(PostQuadKind::kCount)) return false;
  for (size_t i = 0; i < std::size(kPostQuadTable); ++i) {
    const PostQuadEntry& e = kPostQuadTable[i];
    if (size_t(e.kind) != i || e.ps == 0 || e.vs == 0 || e.ps == e.vs) return false;
    for (size_t k = 0; k < i; ++k) {
      if (kPostQuadTable[k].ps == e.ps) return false;
    }
    if (e.layer > 1 || (e.layer == 0) != (e.target == PostQuadTarget::kScene)) return false;
    if ((e.blend == PostQuadBlend::kDepth) != (e.target == PostQuadTarget::kLayerDepth)) return false;
    if (e.slot_count < 1 || e.slot_count > 3 || e.register_count > 12) return false;
    for (size_t k = 0; k < e.slot_count; ++k) {
      if (e.slots[k] > 31) return false;
    }
    for (size_t k = 0; k < e.register_count; ++k) {
      if (e.registers[k] > 251 || (k > 0 && e.registers[k] <= e.registers[k - 1])) return false;
    }
  }
  return true;
}
static_assert(PostQuadTableIsWellFormed(), "post_quad_table.inc holds an entry the capture cannot read");

// The table's entry of a pixel shader, null for any other shader (a reader takes "not in the
// table" as "not read").
inline constexpr const PostQuadEntry* FindPostQuad(uint64_t ps_hash) {
  for (const PostQuadEntry& e : kPostQuadTable) {
    if (e.ps == ps_hash) return &e;
  }
  return nullptr;
}

// Whether a call that runs `entry`'s shader is one of its kind's quads by where it is drawn
// (controller ruling 24, kind by kind): a scene kind inside the main scene; a layer kind outside it
// at a target of half the scene's pitch (`pitch`: the call's RB_SURFACE_INFO pitch; `scene_pitch`:
// the main scene's of this frame, 0 while none was seen). The flare's shader is also drawn into
// the water's reflection views, a quarter of the scene's pitch, before the main scene: those are
// not the layer's flares and are not read (the capture does not look at them at all: before the
// main scene's first call of the builder it looks at no call outside the scene, so they are not
// counted "at another target" either; this rule would refuse them if it were asked). No pitch is
// carried as a number.
inline constexpr bool QuadTargetMatches(const PostQuadEntry& entry, bool in_scene, uint32_t pitch, uint32_t scene_pitch) {
  if (entry.layer == 0) return in_scene;
  return !in_scene && scene_pitch != 0 && pitch != 0 && pitch * 2 == scene_pitch;
}

// What the capture does with one outermost call of the indexed builder in a frame with the quads
// wanted (capture.cpp, ObserveQuadDraw), before the call's shader is looked at. `scene_pitch`: the
// main scene's pitch of this frame, after an in-scene call's own try at reading it (0: not known);
// `post_done`: the frame's composite was seen; `outside_looks`: calls outside the scene looked at
// so far this frame.
//   kLook:   the call is looked at (an outside one is counted in `outside_looks` by the caller);
//   kSkip:   a call outside the scene before the main scene's first call of the builder (the
//            water's reflection views) or after the frame's composite (the HUD): not looked at;
//   kRefuse: the frame's set cannot be whole, and the caller counts one refused call ("unread"),
//            once a frame. Inside the scene: the scene's pitch could not be read, so no call
//            outside it can be told to be the layer's (QuadTargetMatches) and none would be looked
//            at: without this the set would read "whole" with the scene's quads alone. Outside:
//            kMaxQuadLooks calls were looked at and the composite has not come: a layer quad may
//            lie beyond the bound. (The captures have 11 to 16 such calls a frame: the "indx" rows
//            outside the scene between its first inside row and the composite, in the 44 frames
//            of the six captures named at kMaxPostLooks, post_block.h.)
inline constexpr uint32_t kMaxQuadLooks = 256;
enum class QuadLook : uint8_t { kLook, kSkip, kRefuse };
inline constexpr QuadLook ChooseQuadLook(bool in_scene, uint32_t scene_pitch, bool post_done, uint32_t outside_looks) {
  if (in_scene) return scene_pitch != 0 ? QuadLook::kLook : QuadLook::kRefuse;
  if (scene_pitch == 0 || post_done) return QuadLook::kSkip;
  return outside_looks >= kMaxQuadLooks ? QuadLook::kRefuse : QuadLook::kLook;
}

// One quad of the frame. An invalid record is all zero (ps_hash 0: QuadRecordValid).
struct QuadRecord {
  uint64_t ps_hash, vs_hash;
  PostQuadKind kind;
  uint8_t layer;            // 0 the main scene, 1 the sun layer
  uint16_t sequence;        // its place among the frame's quad records
  float corners[4][5];      // position x y z and u v, as the game filled the storage
  float view_proj[4][4];    // vertex c0..c3 at the call
  float ps[12][4];          // the pixel registers POST_QUAD_SHADERS names for the kind, in its order
  uint32_t fetch[3][6];     // its texture slots' fetch constants, in the kind's order
  uint32_t blend, depth, colour_mask;  // RB_BLENDCONTROL0, RB_DEPTHCONTROL, RB_COLOR_MASK's four channels
  // The draw's own alpha test (controller ruling 23). alpha_control: RB_COLORCONTROL bits 0-3, the
  // function (bits 0-2) and the enable bit (bit 3). alpha_ref: RB_ALPHA_REF with the test on, in
  // [0, 1]; 0 with it off, where the register is stale (it then holds the sun disc's 0.502).
  uint32_t alpha_control;
  float alpha_ref;
};
// 16 + 4 + 80 + 64 + 192 + 72 + 12 + 8.
static_assert(sizeof(QuadRecord) == 448, "QuadRecord grew or shrank: the documents state its size");

inline bool QuadRecordValid(const QuadRecord& r) { return r.ps_hash != 0; }
inline bool QuadAlphaTest(const QuadRecord& r) { return (r.alpha_control & 8) != 0; }

// What was read for one call, by the capture.
struct QuadInputs {
  const uint8_t* vertices; size_t vertex_bytes; uint32_t stride;   // the storage as it was read (guest order)
  const uint16_t* indices; size_t index_count;                     // host order
  const float (*view_proj)[4];                 // vertex c0..c3
  const float (*ps)[4]; size_t ps_count;       // the kind's pixel registers, in its order
  const uint32_t (*fetch)[6]; size_t fetch_count;
  uint32_t blend, depth, colour_mask;
  uint32_t colour_control, alpha_ref;          // RB_COLORCONTROL and RB_ALPHA_REF (a float's bits), as the device holds them
  uint32_t vport_z_scale, vte_cntl;            // PA_CL_VPORT_ZSCALE (a float's bits) and PA_CL_VTE_CNTL: the depth range
};

// Why a call of a table shader at its kind's target gave no record; kNone: it gave one.
enum class QuadRecordReason : uint8_t {
  kNone,
  kUnread,    // the device, a shader, a register, a fetch constant, a state word or the storage could not be read
  kShader,    // the vertex shader is not the one the kind's pixel shader is checked with
  kStorage,   // the builder's arguments or the device's storage fields are not a quad's (4 vertices of 20 bytes, 6
              // 16-bit indices from vertex 0, a triangle list), or the fields are another draw's
  kShape,     // not 80 bytes of vertices at a stride of 20 with six indices
  kIndices,   // the six indices are not 0 1 2 1 3 2
  kCorner,    // a corner's position or coordinate is not finite or beyond 1e7
  kMatrix,    // the same for a component of c0..c3
  kRegister,  // the same for a pixel register the kind reads
  kState,     // the blend word or the colour mask is not the kind's, or the depth range is not the reversed one
  kAlpha,     // alpha-to-mask, or the alpha test on with a reference that is no number or outside [0, 1]
  kCount
};

inline const char* QuadRecordReasonName(QuadRecordReason r) {
  static constexpr const char* kNames[] = {"none", "unread", "shader", "storage", "shape", "indices",
                                           "corner", "matrix", "register", "state", "alpha"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == size_t(QuadRecordReason::kCount));
  return r < QuadRecordReason::kCount ? kNames[size_t(r)] : "?";
}

// The six indices of every captured furniture quad (plan Task 10): corners 0 1 / 2 3.
inline constexpr uint16_t kQuadIndices[6] = {0, 1, 2, 1, 3, 2};
inline constexpr uint32_t kQuadVertices = 4, kQuadStride = 20;
// No float of a record is beyond this (corners 3,400 and matrix terms 180 at most in the captures).
inline constexpr float kQuadValueBound = 1e7f;

namespace detail {
inline bool QuadValueFits(float v) { return std::fabs(v) <= kQuadValueBound; }  // (false for a NaN and an infinity)
inline constexpr uint32_t QuadBlendWord(PostQuadBlend b) {
  return b == PostQuadBlend::kAlpha ? 0x07060706u : b == PostQuadBlend::kAdd ? 0x01010101u : 0x00010001u;
}
}  // namespace detail

// The record of one call of `entry`'s shader, or no record (all zero) with the first rule that
// fails in `why` (optional), tried in this order:
//   - everything was read: the storage, the indices, the matrix, as many registers and fetch
//     constants as the kind names (kUnread);
//   - 80 bytes of vertices at a stride of 20 and six indices (kShape); the indices are 0 1 2 1 3 2
//     (kIndices);
//   - every corner value (kCorner), matrix component (kMatrix) and component of the kind's
//     registers (kRegister) is finite and within 1e7;
//   - the blend word is the kind's and the colour mask has all four channels; a depth kind: the
//     mask has none, whatever the blend word; and the depth range is the game's reversed one,
//     window depth 1 - z / w (PA_CL_VTE_CNTL bit 4 with a negative finite Z scale, as every
//     captured row has it), under which a consumer reads the depth word's function (kState);
//   - no alpha-to-mask; with the alpha test on, its reference is a number in [0, 1] (kAlpha).
// The depth word is carried as it is, to be read under the reversed range: which of its states
// can be drawn is the pass's to say.
// Registers and slots beyond the kind's counts are zero in the record. `sequence` is 0: the frame
// gives a record its place (FrameBuilder::AddQuad).
inline QuadRecord MakeQuadRecord(const PostQuadEntry& entry, const QuadInputs& in, QuadRecordReason* why) {
  auto fail = [&](QuadRecordReason r) {
    if (why) *why = r;
    return QuadRecord{};
  };
  if (!in.vertices || !in.indices || !in.view_proj || !in.ps || !in.fetch || in.ps_count != entry.register_count ||
      in.fetch_count != entry.slot_count || entry.register_count > 12 || entry.slot_count > 3) {
    return fail(QuadRecordReason::kUnread);
  }
  if (in.stride != kQuadStride || in.vertex_bytes != size_t(kQuadVertices) * kQuadStride || in.index_count != std::size(kQuadIndices)) {
    return fail(QuadRecordReason::kShape);
  }
  for (size_t i = 0; i < std::size(kQuadIndices); ++i) {
    if (in.indices[i] != kQuadIndices[i]) return fail(QuadRecordReason::kIndices);
  }
  QuadRecord r{};
  for (uint32_t c = 0; c < kQuadVertices; ++c) {
    for (uint32_t k = 0; k < 5; ++k) {
      const uint8_t* p = in.vertices + kQuadStride * c + 4 * k;
      const uint32_t bits = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
      float v;
      std::memcpy(&v, &bits, sizeof(v));
      if (!detail::QuadValueFits(v)) return fail(QuadRecordReason::kCorner);
      r.corners[c][k] = v;
    }
  }
  for (int i = 0; i < 4; ++i) {
    for (int k = 0; k < 4; ++k) {
      if (!detail::QuadValueFits(in.view_proj[i][k])) return fail(QuadRecordReason::kMatrix);
      r.view_proj[i][k] = in.view_proj[i][k];
    }
  }
  for (size_t i = 0; i < entry.register_count; ++i) {
    for (int k = 0; k < 4; ++k) {
      if (!detail::QuadValueFits(in.ps[i][k])) return fail(QuadRecordReason::kRegister);
      r.ps[i][k] = in.ps[i][k];
    }
  }
  const uint32_t mask = in.colour_mask & 0xF;
  if (entry.blend == PostQuadBlend::kDepth ? mask != 0 : (mask != 0xF || in.blend != detail::QuadBlendWord(entry.blend))) {
    return fail(QuadRecordReason::kState);
  }
  float z_scale = 0.0f;
  std::memcpy(&z_scale, &in.vport_z_scale, sizeof(z_scale));
  if (!(in.vte_cntl & 0x10) || !std::isfinite(z_scale) || !(z_scale < 0.0f)) return fail(QuadRecordReason::kState);
  if (in.colour_control & 0x10) return fail(QuadRecordReason::kAlpha);
  float ref = 0.0f;
  if (in.colour_control & 8) {
    std::memcpy(&ref, &in.alpha_ref, sizeof(ref));
    if (!(ref >= 0.0f && ref <= 1.0f)) return fail(QuadRecordReason::kAlpha);  // (written so that a NaN fails it)
  }
  for (size_t i = 0; i < entry.slot_count; ++i) std::memcpy(r.fetch[i], in.fetch[i], sizeof(r.fetch[i]));
  r.ps_hash = entry.ps;
  r.vs_hash = entry.vs;
  r.kind = entry.kind;
  r.layer = entry.layer;
  r.blend = in.blend;
  r.depth = in.depth;
  r.colour_mask = mask;
  r.alpha_control = in.colour_control & 0xF;
  r.alpha_ref = ref;
  if (why) *why = QuadRecordReason::kNone;
  return r;
}

// Why a frame has no quad records to draw with (FrameScene::quad_reason); kNone: its set is whole.
enum class QuadSetReason : uint8_t {
  kNone,
  kOff,        // the quads were not wanted this frame (the view, the light, the post or the sun switch off): nothing was read
  kNoDraw,     // no call of the frame ran a table shader at its kind's target (a menu, a loading screen)
  kRefused,    // a call gave no record (FrameScene::quad_refused has the reasons)
  kOverLimit,  // more calls than kMaxQuadRecords
  kPending,    // a call's storage was not read before the frame ended, or its record was dropped for a frame that ended
  kCount
};

inline const char* QuadSetReasonName(QuadSetReason r) {
  static constexpr const char* kNames[] = {"none", "off", "no-draw", "refused", "over-limit", "pending"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == size_t(QuadSetReason::kCount));
  return r < QuadSetReason::kCount ? kNames[size_t(r)] : "?";
}

// The frame's choice (FrameBuilder::Finish; pure): its records are a set only when every furniture
// call of the frame gave one and all of them fit. Half a set is never given: a frame that lacks one
// quad would be drawn with the others' light and without that one's, and nothing would say so.
// on: the quads were wanted. seen: the frame's furniture calls. taken: the records held. refused:
// the calls that gave none, by reason. over: valid records beyond the bound.
inline QuadSetReason ChooseQuadSet(bool on, uint32_t seen, uint32_t taken, const uint32_t refused[size_t(QuadRecordReason::kCount)],
                                   uint32_t over) {
  if (!on) return QuadSetReason::kOff;
  uint32_t bad = 0;
  for (size_t i = 1; i < size_t(QuadRecordReason::kCount); ++i) bad += refused[i];
  if (bad) return QuadSetReason::kRefused;
  if (over || taken > kMaxQuadRecords) return QuadSetReason::kOverLimit;
  if (seen != taken) return QuadSetReason::kPending;
  return seen ? QuadSetReason::kNone : QuadSetReason::kNoDraw;
}

}  // namespace fable2::native::capture
