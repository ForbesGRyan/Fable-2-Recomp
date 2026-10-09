#pragma once

// Pure helpers for the clay pass and its geometry cache (no SDK/GPU deps):
// cache keys, vertex counts, index range checks, draw colors, the root
// constant block, pipeline keys and the alpha rule of a draw's render state,
// the terrain material (its constants, the fallback of a ground square),
// texture counters and the F3 status text.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../capture/draw_record.h"
#include "../capture/render_state.h"
#include "../capture/shader_tally.h"
#include "frame_scene.h"
#include "geometry_cache_index.h"
#include "normal_decode.h"

namespace fable2::native::render {

// Why the pass did not draw a shadow record into its map ("Native shadow maps"
// below; ClayStats::shadow_skipped). kState: its depth state is not the shadow
// passes' (reversed range, greater-equal, written). kMatrix: a row of its transform is not finite.
// kBias: its polygon offset is not a pair of numbers of usable size. kAlpha: an
// alpha-tested caster whose material has no alpha the shader can form. kGeometry:
// its positions or indices could not be built (a bad index among them).
// kTexture: an alpha-tested caster whose coordinates or texture are not there
// this frame (never drawn as a solid caster). kPipeline: no pipeline of its cull.
// kConstants: the frame's constant buffer could not be made. kState is the
// record's depth state alone since the review of plan Task 4: kTwin, the
// record has no caster entry beside it or one of no known kind; kUnread, its
// state words were not read.
enum class ShadowDrawSkip : uint8_t {
  kNone, kState, kMatrix, kBias, kAlpha, kGeometry, kTexture, kPipeline, kConstants, kTwin, kUnread, kCount
};

inline const char* ShadowDrawSkipName(ShadowDrawSkip s) {
  switch (s) {
    case ShadowDrawSkip::kNone: return "none";
    case ShadowDrawSkip::kState: return "state";
    case ShadowDrawSkip::kMatrix: return "matrix";
    case ShadowDrawSkip::kBias: return "bias";
    case ShadowDrawSkip::kAlpha: return "alpha";
    case ShadowDrawSkip::kGeometry: return "geometry";
    case ShadowDrawSkip::kTexture: return "texture";
    case ShadowDrawSkip::kPipeline: return "pipeline";
    case ShadowDrawSkip::kConstants: return "constants";
    case ShadowDrawSkip::kTwin: return "no-caster-entry";
    case ShadowDrawSkip::kUnread: return "state-unread";
    default: return "?";
  }
}

// The frame's mask ("Native shadows" below): why a frame has no single inverse view-projection, and
// what the pass did about the mask.
inline constexpr const char* kShadowViewReason = "shadow-view";
inline constexpr float kFrameViewTolerance = 1e-4f;
enum class FrameViewReason : uint8_t { kNone, kNoRecord, kDisagree, kSingular, kEye, kCount };
inline const char* FrameViewReasonName(FrameViewReason r) {
  switch (r) {
    case FrameViewReason::kNone: return "none";
    case FrameViewReason::kNoRecord: return "no-record";
    case FrameViewReason::kDisagree: return "disagree";
    case FrameViewReason::kSingular: return "singular";
    case FrameViewReason::kEye: return "eye";
    default: return "?";
  }
}

// What the pass did about the frame's mask (ClayStats::mask; the Shadow line). kOff: the shadows'
// switch is off. kNoLight: the frame is drawn without light (the switch, the textures, or no light
// block). kBlock: the frame has no valid shadow block, whatever the reason, a frame without shadow
// records included ("shadow-block"). kMaps: the block is valid and the frame has no constant buffer
// because it has nothing to draw (a buffer that could not be made zeroes the frame's light: kNoLight). kView: no single inverse view-projection, or
// no usable eye ("shadow-view"; ClayStats::mask_view says which). kObjects: the shadows' GPU objects
// could not be made (the shadows are off until the pass is rebuilt or the game restarted:
// ClayPass::shadows_usable). kDrawn: the mask was written and is bound.
enum class ClayMask : uint8_t { kOff, kNoLight, kBlock, kMaps, kView, kObjects, kDrawn, kCount };

// The pass's decision about the frame's mask (ClayPass::Render), one chain in which the first thing
// missing decides. `shadows`: fable2_native_shadows. `light`: the frame is drawn with the light's
// switch on (and the textures, without which there is no light). `objects`: the shadows' GPU objects
// exist or may still be made; false once making them has failed. `block`: FrameScene::shadow is
// valid. `light_block`: the frame has a light block the pass can use. `constants`: the frame's
// constant buffer was made. `eye`: the frame's eye is usable. `view`: why the frame has no single
// inverse view-projection (FrameInverseViewProj), kNone when it has one. `finite`: the mask's
// constants could be made of it (MakeClayMaskConstants).
// The failed objects come before the block: once they have failed the capture records no shadow
// passes (native_render_state.h, ShadowRecordsWanted), so every later frame has no block, and it is
// the objects that the line must name.
struct ClayMaskInputs {
  bool shadows = false, light = false, objects = false, block = false, light_block = false, constants = false, eye = false;
  FrameViewReason view = FrameViewReason::kNone;
  bool finite = false;
};
struct ClayMaskDecision {
  ClayMask mask = ClayMask::kOff;
  FrameViewReason view = FrameViewReason::kNone;  // with kView: why
};
constexpr ClayMaskDecision DecideClayMask(const ClayMaskInputs& in) {
  if (!in.shadows) return {ClayMask::kOff, FrameViewReason::kNone};
  if (!in.light) return {ClayMask::kNoLight, FrameViewReason::kNone};
  if (!in.objects) return {ClayMask::kObjects, FrameViewReason::kNone};
  if (!in.block) return {ClayMask::kBlock, FrameViewReason::kNone};
  if (!in.light_block) return {ClayMask::kNoLight, FrameViewReason::kNone};
  if (!in.constants) return {ClayMask::kMaps, FrameViewReason::kNone};
  if (!in.eye) return {ClayMask::kView, FrameViewReason::kEye};
  if (in.view != FrameViewReason::kNone) return {ClayMask::kView, in.view};
  if (!in.finite) return {ClayMask::kView, FrameViewReason::kSingular};
  return {ClayMask::kDrawn, FrameViewReason::kNone};
}
// The two shadow maps are prepared and drawn only for a frame whose mask is drawn: nothing else reads
// them (the shadow view shows a flat colour for a frame without them).
constexpr bool ClayMapsDrawn(ClayMask mask) { return mask == ClayMask::kDrawn; }

// What the pass did about the frame's fog ("Native fog" below; ClayStats::fog; the Fog line). kOff:
// the fog's switch is off. kNoLight: the frame is drawn without light (the light's switch, the
// textures, no light block, no constant buffer): the fog is of the lit colour. kBlock: the frame has
// no valid fog block, whatever the capture's reason. kValues: the block is marked valid and holds a
// value or a texel the pass will not hand a shader (MakeClayFogConstants, SetClayFrameFog). kView:
// the frame has no usable eye or no single inverse view-projection, from which a pixel's world
// position is rebuilt (`view` says which, as for the mask). kDrawn: the lit pixels are fogged. The
// three in the middle are the frames counted "fog-block".
enum class ClayFog : uint8_t { kOff, kNoLight, kBlock, kValues, kView, kDrawn, kCount };

// The pass's decision about the frame's fog (ClayPass::Render), one chain in which the first thing
// missing decides. `fog`: fable2_native_fog as the pass reads it NOW (a scene captured with the
// switch on can arrive after it went off: it is then drawn without fog). `light`: the light's
// switch (and the textures). `light_block`: the frame's constants are lit. `block`:
// FrameScene::fog is valid. `values`: MakeClayFogConstants gives constants that are on. `eye`: the
// frame's eye is usable. `view`: why the frame has no single inverse view-projection
// (FrameInverseViewProj), kNone when it has one. `filled`: SetClayFrameFog wrote the frame's tail
// (false: a texel or an entry of the matrix that is no number).
struct ClayFogInputs {
  bool fog = false, light = false, light_block = false, block = false, values = false, eye = false;
  FrameViewReason view = FrameViewReason::kNone;
  bool filled = false;
};
struct ClayFogDecision {
  ClayFog fog = ClayFog::kOff;
  FrameViewReason view = FrameViewReason::kNone;  // with kView: why
};
constexpr ClayFogDecision DecideClayFog(const ClayFogInputs& in) {
  if (!in.fog) return {ClayFog::kOff, FrameViewReason::kNone};
  if (!in.light || !in.light_block) return {ClayFog::kNoLight, FrameViewReason::kNone};
  if (!in.block) return {ClayFog::kBlock, FrameViewReason::kNone};
  if (!in.values) return {ClayFog::kValues, FrameViewReason::kNone};
  if (!in.eye) return {ClayFog::kView, FrameViewReason::kEye};
  if (in.view != FrameViewReason::kNone) return {ClayFog::kView, in.view};
  if (!in.filled) return {ClayFog::kValues, FrameViewReason::kNone};
  return {ClayFog::kDrawn, FrameViewReason::kNone};
}

struct ClayStats {
  uint32_t drawn = 0, skipped_bad_index = 0, uploads = 0, hits = 0;
  uint64_t resident_bytes = 0;
  double hash_ms = 0, decode_ms = 0, record_ms = 0;
  // Drawn records whose shader deforms the position (drawn undeformed).
  uint32_t deformed = 0;
  // Renderer-side skips other than bad indices: unreadable guest memory, a
  // failed decode or upload, or an empty triangle list.
  uint32_t skipped_other = 0;
  // (Among them: the frame's drawable records when its constant buffer could
  // not be made, ClayRecordsWithoutConstants; then nothing of the frame is drawn.)
  // Drawn records whose positions came from the instanced (flat stream) and
  // the bone-skinned builders, cache hits included. Counted with `drawn`: a
  // record whose index list fails afterwards is in neither.
  uint32_t instanced = 0, skinned = 0;
  // Render state (DecideDrawState). Of the drawn records: those drawn with the
  // alpha test (or alpha-to-mask) applied, with a blend, with a test or blend
  // the material has no alpha for (drawn without the test, with alpha 1), and
  // with a state that was not read (drawn with the default state) or a depth
  // function the RHI cannot express (the default depth state only; blend,
  // colour mask and cull stay). blend_unknown: records left out because their
  // state blends and their colour is not known, or the RHI cannot express the
  // blend.
  uint32_t alpha_tested = 0, blended = 0, alpha_unknown = 0, blend_unknown = 0, state_invalid = 0;
  // Pipelines created in the pass's cache (a slot whose creation failed is not
  // counted), and records that found no pipeline for their state because the
  // cache was full or the pipeline could not be created: drawn records that
  // used the default pipeline, and terrain records that lost their material
  // for it (terrain-inputs; counted once, also when their flat draw finds none).
  uint32_t pipelines = 0, pipeline_overflow = 0;
  // alpha_unknown and blend_unknown by pixel shader (kTallyAlphaUnknown, kTallyBlendUnknown).
  capture::ShaderTally<32> unknown_by_ps;
  // Terrain layer records left out because their ground square is drawn as flat
  // clay by another record (PlanTerrain), and the distinct (colour, weights,
  // side) texture sets bound for the terrain records drawn with their material.
  uint32_t terrain_skipped = 0, terrain_tuples = 0;
  // Terrain records that could not have their material this frame, by their own
  // reason (TerrainMaterialStatus), whatever the plan then did with them: drawn
  // flat under that reason, drawn flat or left out under terrain-square.
  uint32_t terrain_unusable[size_t(capture::MaterialStatus::kCount)] = {};
  // The same for terrain records whose geometry could not be built (their squares
  // fall back too), by what was missing: the positions (the heightmap could not
  // be read, the upload failed) or, with positions, the indices. Counted while the
  // texture path is on, like terrain_unusable.
  uint32_t terrain_no_positions = 0, terrain_no_indices = 0;
  // The light inputs of the prepared records (frame_scene.h, LightOf) as the
  // geometry cache built them (a terrain record is counted when it is drawn
  // with its material: after the plan, FinishTerrain). lit: prepared
  // records with light inputs. light_ready: those whose normals and the
  // second buffer of their vertex kind were built (a colour stream: its
  // colours; probe: the weights and set indices; instance: the copies'
  // colours; terrain has none). light_missing: the
  // others by reason (index: capture::LightReason; normal-unsupported: a normal
  // element no reader decodes; light-unknown: a stream that could not be read
  // or holds too few elements, a second buffer that was refused, a failed
  // upload), and by vertex shader
  // (light_missing_by_vs; the kind is the reason). light_second: of the
  // light-unknown ones, the records whose probe or instance colour buffer was
  // refused, by cause (index: LightSecond, normal_decode.h). light_rotation: the world
  // rotation of the object and trunk records among the ready ones, by
  // RotationKind (normal_decode.h), and those that are not a rotation by
  // vertex shader (light_rotation_by_vs; the kind is the RotationKind).
  uint32_t lit = 0, light_ready = 0;
  uint32_t light_missing[size_t(capture::LightReason::kCount)] = {};
  capture::ShaderTally<32> light_missing_by_vs;
  uint32_t light_second[size_t(LightSecond::kCount)] = {};
  // The bend inputs of the frame's instanced plant records (GeometryCache::
  // InstanceBend): the records that have their buffer and the bytes of those
  // buffers (16 a flat position; a buffer two records share is counted twice).
  uint32_t light_bend = 0;
  uint64_t light_bend_bytes = 0;
  uint32_t light_rotation[size_t(RotationKind::kCount)] = {};
  capture::ShaderTally<32> light_rotation_by_vs;
  // The light as drawn ("Native light" below; the Light line). light_switch:
  // the frame was drawn with fable2_native_light on (the switch on and the
  // texture path on: the light is of the material). light_textures_off: the
  // switch was on and the texture path off. light_block: the frame had a
  // light block the pass could use (MakeClayFrameConstants). light_drawn: the
  // drawn records lit by their family's function, by family. light_unlit: the
  // drawn records drawn without light (the unlit linear colour of the
  // materials picture), by reason (ClayUnlit), and light_unlit_by_ps the pixel
  // shaders of those that are unlit for a reason of their own (light-unknown,
  // normal-unsupported; the kind is the ClayUnlit value).
  bool light_switch = false, light_block = false, light_textures_off = false;
  uint32_t light_drawn[size_t(capture::LightFamily::kCount)] = {};
  uint32_t light_unlit[4] = {};
  capture::ShaderTally<32> light_unlit_by_ps;
  float light_sun_dir[3] = {}, light_sun_col[3] = {};
  // The shadow maps ("Native shadow maps" below; the Shadow line). shadow_switch:
  // fable2_native_shadows is on, or the scene holds shadow records all the same
  // (taken before the switch went off): what "Shadow: on" means, set once a
  // frame (ClayPass::Render). shadow_block: the scene has a valid shadow block
  // (shadow_reason: why not). shadow_maps: the two maps were cleared and drawn
  // this frame, which is only in a frame whose mask is drawn (ClayMapsDrawn);
  // shadow_light_off: they were not although the block is valid, because the
  // frame is drawn without light (the switch, the textures, or no light block).
  // Per map (0 near, 1 far):
  // shadow_records, the scene's records; shadow_drawn, those drawn into the map;
  // shadow_skipped, the others by the pass's reason; shadow_capture_skipped, the
  // game's shadow draws the capture did not record, by its reason (a
  // SkipReason's or a ShadowSkip's number). shadow_skipped_by_vs: the pass's
  // skips by vertex shader (the kind is the ShadowDrawSkip). shadow_extra: draws
  // on the game's map that are in neither pass (shadow-pass-extra), and
  // shadow_passes the passes the game opened: a frame with a valid block may
  // still hold a partial pass, and these say so.
  bool shadow_switch = false, shadow_block = false, shadow_maps = false, shadow_light_off = false;
  capture::ShadowBlockReason shadow_reason = capture::ShadowBlockReason::kOff;
  uint32_t shadow_records[2] = {}, shadow_drawn[2] = {};
  uint32_t shadow_skipped[2][size_t(ShadowDrawSkip::kCount)] = {};
  uint32_t shadow_capture_skipped[2][capture::kShadowSkipCount] = {};
  capture::ShaderTally<32> shadow_skipped_by_vs;
  uint32_t shadow_extra = 0, shadow_passes = 0;
  float shadow_split[4] = {};
  // The pre-pass and the mask ("Native shadows" below; the Shadow line's second half).
  // prepass_drawn: scene records drawn depth-only before the mask; prepass_skipped: depth-writing
  // records left out of it for want of a pipeline. mask: what the pass did about the frame's mask;
  // mask_view: with kView, why (FrameViewReason); mask_rotated: the mask was written with the
  // game's rotation tile (false with a drawn mask: unrotated taps, the tile could not be read or
  // failed its check). mask_view_outliers: records whose matrix was not the frame's view-projection
  // (FrameView::outliers) in a frame that has one. The frame counts are the pass's totals since it
  // was made, this frame included: frames drawn with light and the shadows' switch on, those of
  // them without a valid shadow block (shadow-block), without a view (shadow-view), with unrotated
  // taps, without the frame's light block (no-light: the white mask) and without the shadows'
  // objects (no-objects: their creation failed; the white mask).
  // prepass_culled: depth-writing records left out of the pre-pass because they lie wholly beyond
  // the mask's reach (PrepassBeyondReach): the mask is the same without them.
  uint32_t prepass_drawn = 0, prepass_skipped = 0, prepass_culled = 0;
  ClayMask mask = ClayMask::kOff;
  FrameViewReason mask_view = FrameViewReason::kNone;
  uint32_t mask_view_outliers = 0;
  bool mask_rotated = false;
  uint64_t shadow_frames = 0, shadow_block_frames = 0, shadow_view_frames = 0, shadow_unrotated_frames = 0;
  uint64_t shadow_nolight_frames = 0, shadow_objects_frames = 0;
  // The fog ("Native fog" below; the Fog line). fog_switch: fable2_native_fog. fog_block,
  // fog_reason: the scene's fog block and, without one, the capture's reason. fog, fog_view: what
  // the pass did about it (ClayFog), and with kView why. fog_trees: lit trunk and leaf draws with a
  // usable in-scatter scale (the trees' form in a fogged frame); fog_trees_none: those without one,
  // drawn without fog in any frame.
  // The frame counts are the pass's totals since it was made, this frame included: frames drawn
  // with the fog's and the light's switch on, and those of them not fogged for want of a block,
  // for a value, for a view ("fog-block" is the three together) or for want of light.
  bool fog_switch = false, fog_block = false;
  capture::FogBlockReason fog_reason = capture::FogBlockReason::kOff;
  ClayFog fog = ClayFog::kOff;
  FrameViewReason fog_view = FrameViewReason::kNone;
  uint32_t fog_trees = 0, fog_trees_none = 0;
  uint64_t fog_frames = 0, fog_block_frames = 0, fog_values_frames = 0, fog_view_frames = 0, fog_nolight_frames = 0;
};
// The pass's frame counts of the fog, which follow from the frame's verdict: one frame added.
// `on`: the fog's switch and the light's switch together (the frames that are counted).
struct ClayFogFrames {
  uint64_t frames = 0, block = 0, values = 0, view = 0, nolight = 0;
};
inline void CountClayFogFrame(bool on, ClayFog fog, ClayFogFrames& n) {
  if (!on) return;
  ++n.frames;
  if (fog == ClayFog::kBlock) ++n.block;
  if (fog == ClayFog::kValues) ++n.values;
  if (fog == ClayFog::kView) ++n.view;
  if (fog == ClayFog::kNoLight) ++n.nolight;
}
// The frames counted "fog-block": lit frames with the switch on that were drawn without fog.
inline uint64_t ClayFogBlockFrames(const ClayFogFrames& n) { return n.block + n.values + n.view; }
// Which frames the last two counts take: `shadows_on` is the switch and the light's switch
// together (the frames shadow_frames counts).
inline bool CountsNoLightFrame(bool shadows_on, ClayMask mask) { return shadows_on && mask == ClayMask::kNoLight; }
inline bool CountsNoObjectsFrame(bool shadows_on, ClayMask mask) { return shadows_on && mask == ClayMask::kObjects; }
// The pass's frame counts (ClayStats::shadow_frames and the five beside it), which follow from the
// frame's verdict: one frame added. A valid block whose frame has no constant buffer (kMaps) is counted
// with the frames without a block: the frame has no shadows of its block either way. `rotated`: a
// drawn mask was written with the game's rotation tile.
struct ClayMaskFrames {
  uint64_t frames = 0, block = 0, view = 0, unrotated = 0, nolight = 0, objects = 0;
};
inline void CountClayMaskFrame(bool shadows_on, ClayMask mask, bool rotated, ClayMaskFrames& n) {
  if (!shadows_on) return;
  ++n.frames;
  if (mask == ClayMask::kBlock || mask == ClayMask::kMaps) ++n.block;
  if (mask == ClayMask::kView) ++n.view;
  if (mask == ClayMask::kDrawn && !rotated) ++n.unrotated;
  if (CountsNoLightFrame(shadows_on, mask)) ++n.nolight;
  if (CountsNoObjectsFrame(shadows_on, mask)) ++n.objects;
}

inline constexpr uint8_t kTallyAlphaUnknown = 0, kTallyBlendUnknown = 1;

// Why a drawn record has no light (ClayStats::light_unlit). kOff: the switch is
// off. kBlock: the frame has no light block ("light-block": every draw of the
// frame). kUnknown ("light-unknown"): the record has no light inputs, its
// family is not one the pass lights, an input or buffer is missing or not
// usable, its world rotation has no direction, or its ground square is unlit
// as a whole. kNormal ("normal-unsupported"): its normal element is one no
// reader decodes.
enum class ClayUnlit : uint8_t { kOff, kBlock, kUnknown, kNormal, kCount };
static_assert(size_t(ClayUnlit::kCount) == 4);

inline const char* ClayUnlitName(ClayUnlit u) {
  switch (u) {
    case ClayUnlit::kOff: return "light-off";
    case ClayUnlit::kBlock: return capture::kLightBlockReason;
    case ClayUnlit::kUnknown: return capture::LightReasonName(capture::LightReason::kLightUnknown);
    case ClayUnlit::kNormal: return capture::LightReasonName(capture::LightReason::kNormalUnsupported);
    default: return "?";
  }
}

// Albedo texture counters of one clay frame (TextureCache and the clay pass).
struct TextureStats {
  uint32_t textured = 0, resident = 0, uploads = 0, mirror = 0, base_only = 0;
  uint64_t resident_bytes = 0, upload_bytes = 0;
  double decode_ms = 0;
  uint32_t status[size_t(capture::MaterialStatus::kCount)] = {};  // final statuses of drawn records
  // Drawn terrain records, and those of them drawn with their material (counted in `textured` too).
  uint32_t terrain = 0, terrain_textured = 0;
  bool latched = false;  // an RHI failure turned the texture path off
};

enum class ClayColor : uint8_t { kClay, kDraw, kShader };

inline ClayColor ParseClayColor(std::string_view text, bool* recognized) {
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
  while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
  std::string lower(text);
  for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
  bool ok = true;
  ClayColor c = ClayColor::kClay;
  if (lower == "draw") c = ClayColor::kDraw;
  else if (lower == "shader") c = ClayColor::kShader;
  else if (lower != "clay") ok = false;
  if (recognized) *recognized = ok;
  return c;
}

// 64 -> 32 bit mix (murmur3 fmix64, folded).
inline uint32_t Hash32(uint64_t v) {
  v ^= v >> 33;
  v *= 0xFF51AFD7ED558CCDull;
  v ^= v >> 33;
  v *= 0xC4CEB9FE1A85EC53ull;
  v ^= v >> 33;
  return uint32_t(v ^ (v >> 32));
}

inline uint32_t HashCombine32(std::initializer_list<uint32_t> values) {
  uint64_t h = 0x9E3779B97F4A7C15ull;
  for (uint32_t v : values) h = (h ^ v) * 0x100000001B3ull + 0x632BE59BD9B4E019ull;
  return Hash32(h);
}

inline constexpr uint32_t kClayBaseColor = 0xB8B0A0u;

inline uint32_t ClayColorValue(ClayColor color, const capture::DrawRecord& r) {
  switch (color) {
    case ClayColor::kDraw: return 0x404040u | (Hash32(uint64_t(r.seq)) & 0xBFBFBFu);
    case ClayColor::kShader: return 0x404040u | (Hash32(r.vs_hash) & 0xBFBFBFu);
    default: return kClayBaseColor;
  }
}

// Positions in a stream of `vb_size` bytes: every vertex whose position lies
// fully inside the stream.
inline uint32_t PositionCount(uint32_t vb_size, const capture::PosLayout& l) {
  const uint32_t bytes = capture::PositionBytes(l.format);
  if (bytes == 0 || l.stride_bytes == 0) return 0;
  const uint64_t need = uint64_t(l.offset_bytes) + bytes;
  if (need > vb_size) return 0;
  return uint32_t((vb_size - need) / l.stride_bytes + 1);
}

// Vertices the record's position buffer holds: the terrain grid points, an
// instanced draw's flat stream, or every position in the stream. `terrain`,
// here and in the keys below: the record's terrain inputs from its scene's
// side array (TerrainOf), null for a record that is not a terrain record.
inline uint32_t RecordVertexCount(const capture::DrawRecord& r, const capture::TerrainPatch* terrain) {
  if (terrain) {
    constexpr uint32_t kPoints = (capture::kTerrainGrid + 1) * (capture::kTerrainGrid + 1);
    return terrain->patches * kPoints;
  }
  if (r.instances.active) return r.instances.flat_count;
  return PositionCount(r.vb.size, r.pos);
}

inline uint32_t FloatBits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}

inline uint32_t LayoutHash(const capture::PosLayout& l) {
  return HashCombine32({l.offset_bytes, uint32_t(l.format), uint32_t(l.exp_adjust), l.swizzle,
                        uint32_t(l.swap16), uint32_t(l.is_signed), uint32_t(l.normalized),
                        l.stride_bytes});
}

// Everything of an instance set that changes the flat stream built from a
// mesh: the instance stream's range and row layouts, the four index constants,
// the offset and the flat count (the content hash covers both streams' bytes).
inline uint32_t InstanceHash(const capture::InstanceSet& s) {
  return HashCombine32({s.rows_addr, s.rows_size, LayoutHash(s.rows[0]), LayoutHash(s.rows[1]),
                        LayoutHash(s.rows[2]), FloatBits(s.inv_count), FloatBits(s.count),
                        FloatBits(s.first), FloatBits(s.bias), FloatBits(s.offset[0]),
                        FloatBits(s.offset[1]), FloatBits(s.offset[2]), s.flat_count});
}

// Everything of a skin that changes what is built from a stream through its
// bones: the bone words' layout, the palette's range and its rows' layouts (the
// content hash covers the palette's bytes).
inline uint32_t SkinHash(const capture::BoneSkin& s) {
  return HashCombine32({s.bones, s.index_offset_bytes, s.index_shift[0], s.index_shift[1], s.index_shift[2],
                        s.index_shift[3], s.index_endian, uint32_t(s.weighted), s.weight_offset_bytes,
                        s.weight_shift[0], s.weight_shift[1], s.weight_shift[2], s.weight_shift[3],
                        s.palette_addr, s.palette_size, s.bone_stride, LayoutHash(s.rows[0]),
                        LayoutHash(s.rows[1]), LayoutHash(s.rows[2])});
}

// Key of the record's vertex stream decoded on its own (kind 0): the stream
// plus every layout field that changes the decode, whatever is built from it
// afterwards. An instanced draw's mesh is decoded under this key once per
// frame for all the draws that expand it (FrameMemo).
inline GeoKey MeshPositionKey(const capture::DrawRecord& r) {
  GeoKey k;
  k.addr = r.vb.phys_addr;
  k.size = r.vb.size;
  k.stride = r.pos.stride_bytes;
  k.extra = LayoutHash(r.pos);
  k.kind = 0;
  return k;
}

// Cache key of a decoded position stream: the stream plus every layout field
// that changes the decode (the content hash covers the raw bytes only). A
// skinned stream adds its bone layout and palette range (kind 0); a terrain
// patch run is keyed by its heightmap and every patch parameter (kind 2); an
// instanced draw's flat stream is keyed by its mesh stream and its instance
// set (kind 5).
inline GeoKey PositionKey(const capture::DrawRecord& r, const capture::TerrainPatch* terrain) {
  GeoKey k;
  if (terrain) {
    const capture::TerrainPatch& t = *terrain;
    const capture::HeightMap& m = t.map;
    k.addr = m.phys_addr;
    k.size = m.size;
    k.stride = capture::kTerrainGrid;
    k.extra = HashCombine32(
        {FloatBits(t.patch), t.patches, FloatBits(t.cols), FloatBits(t.inv_cols),
         FloatBits(t.cell[0]), FloatBits(t.cell[1]), FloatBits(t.height_scale),
         FloatBits(t.origin[0]), FloatBits(t.origin[1]), FloatBits(t.tex_offset[0]),
         FloatBits(t.tex_offset[1]), FloatBits(t.tex_scale[0]), FloatBits(t.tex_scale[1]), m.width,
         m.height, m.pitch, m.endian, uint32_t(m.tiled), uint32_t(m.clamp_x), uint32_t(m.clamp_y)});
    k.kind = 2;
    return k;
  }
  k = MeshPositionKey(r);
  if (r.instances.active) {
    k.extra = HashCombine32({k.extra, InstanceHash(r.instances)});
    k.kind = 5;
    return k;
  }
  if (r.skin.active) k.extra = HashCombine32({k.extra, SkinHash(r.skin)});
  k.kind = 0;
  return k;
}

// --- Normals and colours (normal_decode.h; frame-map section 14, "Normals") ---

// Key of a normal stream decoded on its own (kind 9): the stream holding the
// element and what changes its decode (where the element lies, its format and
// the source components the reader takes, LightElement::source). Nothing of
// the draw: a plain mesh's normals are in the stream's own space, one buffer
// however many draws place and turn the mesh (the world rotation is the
// draw's, LightInputs::world_rot). The sign and scale flags and the endian
// decide only whether the element is read at all (NormalElementSupported),
// and where a shader's fetch puts the components in its register is not in
// the element, so two shaders that fetch one stream's normal differently
// share its buffer. The content hash is the stream's.
inline GeoKey MeshNormalKey(const capture::LightInputs& l) {
  const capture::LightElement& e = l.normal;
  GeoKey k;
  k.addr = e.vb.phys_addr;
  k.size = e.vb.size;
  k.stride = e.stride_bytes;
  k.extra = HashCombine32({e.offset_bytes, e.format, e.source, e.source_count});
  k.kind = 9;
  return k;
}

// Everything of an instance set that changes the flat normals built from a
// mesh's normals: InstanceHash without the offset, which moves positions only
// (ExpandInstanceNormals reads the rows' 3 x 3; the content hash covers the
// mesh's and the instance stream's bytes).
inline uint32_t InstanceNormalHash(const capture::InstanceSet& s) {
  return HashCombine32({s.rows_addr, s.rows_size, LayoutHash(s.rows[0]), LayoutHash(s.rows[1]),
                        LayoutHash(s.rows[2]), FloatBits(s.inv_count), FloatBits(s.count),
                        FloatBits(s.first), FloatBits(s.bias), s.flat_count});
}

// Cache key of a record's normal buffer, as PositionKey is of its positions:
// MeshNormalKey; a skinned stream adds its bone layout and palette range and
// the position stream its bone words are read from (kind 9; the content hash
// adds that stream's and the palette's bytes); an instanced draw's flat
// normals add InstanceNormalHash (kind 10; the content hash adds the instance
// stream's bytes).
inline GeoKey NormalKey(const capture::DrawRecord& r, const capture::LightInputs& l) {
  GeoKey k = MeshNormalKey(l);
  if (r.instances.active) {
    k.extra = HashCombine32({k.extra, InstanceNormalHash(r.instances)});
    k.kind = 10;
    return k;
  }
  if (r.skin.active) {
    k.extra = HashCombine32({k.extra, SkinHash(r.skin), r.vb.phys_addr, r.vb.size, r.pos.stride_bytes});
  }
  return k;
}

// Cache key of a record's colour buffer (kind 11), which holds `count` colours
// of the stream from element LightInputs::colour_first on (DecodeColours): the
// stream, where the element lies, the source components the reader takes, the
// draw's offset into the stream and the
// count. Meshes that share one colour stream at different offsets are different
// entries; draws of one mesh share theirs. The content hash is the stream's.
inline GeoKey ColourKey(const capture::LightInputs& l, uint32_t count) {
  const capture::LightElement& e = l.second;
  GeoKey k;
  k.addr = e.vb.phys_addr;
  k.size = e.vb.size;
  k.stride = e.stride_bytes;
  k.extra = HashCombine32({e.offset_bytes, e.format, e.source, e.source_count, l.colour_first, count});
  k.kind = 11;
  return k;
}

// Cache key of a mesh's probe buffer (kind 12: the four weights and the set
// index of every vertex, BuildProbe): the weights' stream with where the
// element lies and the source components the reader takes, and the position
// stream with what places the set index in a vertex (the element's offset,
// stride and format, the pair swap, the component: DecodeProbeSets reads
// nothing else of the layout, not the fetch's destination swizzle). Nothing of
// the draw: the probe constants c28..c39 are the draw's and in no buffer, and
// the weights are the bind mesh's, so a skinned mesh has one entry whatever
// its palette holds. The content hash is the two streams'.
inline GeoKey ProbeKey(const capture::DrawRecord& r, const capture::LightInputs& l) {
  const capture::LightElement& e = l.second;
  GeoKey k;
  k.addr = e.vb.phys_addr;
  k.size = e.vb.size;
  k.stride = e.stride_bytes;
  k.extra = HashCombine32({e.offset_bytes, e.format, e.source, e.source_count, r.vb.phys_addr, r.vb.size,
                           r.pos.offset_bytes, r.pos.stride_bytes, uint32_t(r.pos.format), uint32_t(r.pos.swap16),
                           uint32_t(int32_t(l.a0_comp))});
  k.kind = 12;
  return k;
}

// Cache key of an instanced draw's flat colours (kind 13: the colour of each
// flat position's copy, ExpandInstanceColours): the instance stream's colour
// element and the source components the reader takes, and the instance set as
// the flat normals have it (InstanceNormalHash: an entry of each per instance
// set; the offset moves positions only). Nothing of the mesh. The content hash
// is the element's stream's.
inline GeoKey InstanceColourKey(const capture::DrawRecord& r, const capture::LightInputs& l) {
  const capture::LightElement& e = l.second;
  GeoKey k;
  k.addr = e.vb.phys_addr;
  k.size = e.vb.size;
  k.stride = e.stride_bytes;
  k.extra = HashCombine32({e.offset_bytes, e.format, e.source, e.source_count, InstanceNormalHash(r.instances)});
  k.kind = 13;
  return k;
}

// Cache key of an instanced draw's bend inputs (kind 14: the up axis of each
// flat position's copy and the height of its mesh vertex, ExpandInstanceBend):
// the mesh's position stream with every layout field that changes its decode
// (MeshPositionKey: the height is the decoded position's third component), and
// the instance set as the flat normals have it (InstanceNormalHash: the up
// axis is of the rows' 3 x 3; the offset moves positions only). Nothing of the
// light's inputs: the bend constants c45 are the draw's and in no buffer. The
// content hash is the two streams'.
inline GeoKey InstanceBendKey(const capture::DrawRecord& r) {
  GeoKey k = MeshPositionKey(r);
  k.extra = HashCombine32({k.extra, InstanceNormalHash(r.instances)});
  k.kind = 14;
  return k;
}

inline GeoKey IndexKey(const capture::DrawRecord& r, const capture::TerrainPatch* terrain) {
  GeoKey k;
  if (terrain) {
    // The grid's triangle list depends only on its shape.
    k.stride = capture::kTerrainGrid;
    k.extra = terrain->patches;
    k.kind = 3;
    return k;
  }
  k.addr = r.indexed ? r.ib.phys_addr : 0;
  k.size = r.indexed ? r.ib.size : 0;
  k.stride = r.index32 ? 1 : 0;
  k.extra = HashCombine32({r.prim, r.start, r.count, uint32_t(r.indexed)});
  k.kind = 1;
  return k;
}

// Key of the record's UV stream decoded on its own (kind 4; kind 3 is the
// terrain grid's index list): the stream holding the UV element plus every
// layout field that changes the decode. Like MeshPositionKey, also the
// per-frame key of an instanced draw's mesh UVs.
inline GeoKey MeshUvKey(const capture::DrawRecord& r) {
  const capture::Material& m = r.material;
  const capture::UvLayout& l = m.uv;
  GeoKey k;
  k.addr = m.uv_vb.phys_addr;
  k.size = m.uv_vb.size;
  k.stride = l.stride_bytes;
  k.extra = HashCombine32({l.offset_bytes, uint32_t(l.format), uint32_t(l.comp_u),
                           uint32_t(l.comp_v), uint32_t(l.swap16), uint32_t(l.normalized),
                           uint32_t(l.is_signed), uint32_t(l.exp_adjust),
                           uint32_t(l.v_element_delta)});
  k.kind = 4;
  return k;
}

// Everything of an instance set that changes the flat UVs built from a mesh's
// UVs: uvs[i] = mesh_uv[vertex(i)] (ExpandInstanceUvs), and vertex(i) takes
// the index, the bias, inv_count and count. The first copy, the instance
// stream, its row layouts and the offset only place the copies; they stay out,
// so a draw whose instance batch changed keeps its UV buffer.
// ExpandInstanceUvs never reads the instance stream. It writes a zero UV in
// two cases only: InstanceIndex fails (the copy number, which is where the
// first copy enters, is negative, not finite or at least 65,536), or the
// vertex is past the mesh UVs decoded. InstanceRangeSkip records no draw with
// the first case: every index below flat_count maps to a copy below 65,536
// and to a vertex below count. The second depends on count and on the mesh UV
// stream, which are in the key.
inline uint32_t InstanceUvHash(const capture::InstanceSet& s) {
  return HashCombine32({FloatBits(s.inv_count), FloatBits(s.count), FloatBits(s.bias), s.flat_count});
}

// Cache key of a decoded UV stream: MeshUvKey; an instanced draw's flat UVs
// (kind 6) add InstanceUvHash. Their content hash is the mesh UV stream's.
inline GeoKey UvKey(const capture::DrawRecord& r) {
  GeoKey k = MeshUvKey(r);
  if (r.instances.active) {
    k.extra = HashCombine32({k.extra, InstanceUvHash(r.instances)});
    k.kind = 6;
  }
  return k;
}

// Cache key of a far terrain draw's colour coordinates (kind 7), which
// BuildTerrainFarUvs reads from the adaptive vertex shader's two tables: the
// tables (fetch dwords 0 to 2 of each: pitch and tiling, address, format and
// endian, size), the draw's patches and the constants that address the tables.
// The content hash covers both tables' bytes. The heightmap, the height scale
// and what changes with the eye (fade, eye) do not enter.
inline GeoKey TerrainFarUvKey(const capture::TerrainPatch& t) {
  const capture::TerrainMaterialInputs& m = t.material;
  GeoKey k;
  k.addr = m.far_u_fetch[1] & 0xFFFFF000u;
  k.size = m.far_v_fetch[1] & 0xFFFFF000u;  // (the second table's address)
  k.stride = capture::kTerrainGrid;
  k.extra = HashCombine32({m.far_u_fetch[0], m.far_u_fetch[1], m.far_u_fetch[2], m.far_v_fetch[0], m.far_v_fetch[1],
                           m.far_v_fetch[2], FloatBits(t.patch), t.patches, FloatBits(t.cols), FloatBits(t.inv_cols),
                           FloatBits(m.far_table[0]), FloatBits(m.far_table[1]), FloatBits(m.far_table[2]),
                           FloatBits(m.far_table[3]), FloatBits(m.far_patch[0]), FloatBits(m.far_patch[1])});
  k.kind = 7;
  return k;
}

// Cache key of a terrain record's vertex normals (kind 8), which
// BuildTerrainNormals reads from the terrain vertex shader's normal map at the
// grid points, for the light and for the steep-ground block: the map (fetch
// dwords 0 to 2: pitch, tiling and clamp modes, address, format and endian,
// size), the draw's patches and what places a grid point on the map (cell,
// origin, the height coordinate's offset and scale). The content hash covers
// the map's bytes. The heightmap, the height scale and everything of the draw
// itself (its textures and constants) do not enter: the layers of a square
// share one buffer.
inline GeoKey TerrainNormalKey(const capture::TerrainPatch& t) {
  const capture::TerrainMaterialInputs& m = t.material;
  GeoKey k;
  k.addr = m.normal_fetch[1] & 0xFFFFF000u;
  k.size = m.normal_fetch[2];
  k.stride = capture::kTerrainGrid;
  k.extra = HashCombine32({m.normal_fetch[0], m.normal_fetch[1], m.normal_fetch[2], FloatBits(t.patch), t.patches,
                           FloatBits(t.cols), FloatBits(t.inv_cols), FloatBits(t.cell[0]), FloatBits(t.cell[1]),
                           FloatBits(t.origin[0]), FloatBits(t.origin[1]), FloatBits(t.tex_offset[0]),
                           FloatBits(t.tex_offset[1]), FloatBits(t.tex_scale[0]), FloatBits(t.tex_scale[1])});
  k.kind = 8;
  return k;
}

// Mesh vertices (positions or UVs) to decode for an instanced draw, of the
// `stream_count` its stream holds: every index maps to a vertex below
// s.count (InstanceIndex, checked at capture by InstanceBoundsOk), so one
// copy's vertices are enough when the stream holds more. 0 for a count that
// is not at least 1 (NaN included), no bound from a count past 2^32: the
// capture records neither, and neither reaches the cast.
inline uint32_t InstanceMeshCount(uint32_t stream_count, const capture::InstanceSet& s) {
  if (!(s.count >= 1.0f)) return 0;
  if (s.count >= 4294967296.0f) return stream_count;
  return std::min(stream_count, uint32_t(s.count));
}

// UVs in a stream of `vb_size` bytes: every vertex whose UV element (both
// elements when u and v sit apart) lies fully inside the stream.
inline uint32_t UvCount(uint32_t vb_size, const capture::UvLayout& l) {
  const uint32_t need = capture::UvElementEnd(l);
  if (need == 0 || l.stride_bytes == 0 || need > vb_size) return 0;
  return uint32_t((vb_size - need) / l.stride_bytes + 1);
}

struct IndexBytes {
  uint32_t offset = 0, size = 0;
};

// The index bytes an indexed draw references, within its index buffer.
inline bool ReferencedIndexBytes(const capture::DrawRecord& r, IndexBytes* out) {
  const uint64_t width = r.index32 ? 4 : 2;
  const uint64_t begin = uint64_t(r.start) * width;
  const uint64_t end = (uint64_t(r.start) + r.count) * width;
  if (end > r.ib.size) return false;
  out->offset = uint32_t(begin);
  out->size = uint32_t(end - begin);
  return true;
}

// Vertex count the shader and the kBadIndex check may use: never more than
// the uploaded buffer holds (root SRVs have no hardware bounds check, and a
// key collision could pair a record with a smaller decoded stream).
inline uint32_t DrawVertexCount(uint32_t buffer_count, uint32_t record_count) {
  return std::min(buffer_count, record_count);
}

// The renderer's kBadIndex rule: every vertex the record's triangles name,
// from the lowest (min_index + base_vertex) to the highest
// (max_index + base_vertex), must lie inside the position stream. Both ends
// are checked here, on the CPU: the vertex shader's own bounds check is a
// select AFTER the load (the compiled shader loads positions[v] and uvs[v]
// for every vertex), and a buffer bound by address has no bounds check, so a
// vertex below 0 would be loaded from before the start of the buffer.
inline bool IndexRangeValid(uint32_t min_index, uint32_t max_index, int32_t base_vertex, uint32_t vertex_count) {
  const int64_t lo = int64_t(min_index) + base_vertex;
  const int64_t hi = int64_t(max_index) + base_vertex;
  return min_index <= max_index && lo >= 0 && hi < int64_t(vertex_count);
}

// Whether the frame's records are drawn at all: the number of them left out
// (0: the frame is drawn). Every clay shader's layout declares the constant
// buffers b1 and b2, and both are views of the frame's constant buffer; when
// that buffer could not be made nothing may be drawn with them unset, so the
// whole frame's records are left out (counted as render-other, logged once).
// The targets are still cleared and mapped: the native view shows the clear
// colour for that frame, never a stale target.
inline uint32_t ClayRecordsWithoutConstants(uint32_t drawable_records, bool constants) {
  return constants ? 0u : drawable_records;
}

// A draw's render state as a pipeline of the clay pass: which pixel shader, and
// blend, colour mask, depth and cull in the values the RHI can express. Factors
// and operations are capture::Factor / BlendOp values, depth_func a
// capture::Compare on the pass's own depth (not reversed), cull a capture::Cull
// in the pass's window space, where clockwise faces are front (ClayCull). The
// alpha test is not pipeline state: it is a discard driven by ClayAlpha.
inline constexpr uint8_t kTexturedTerrainLayer = 2, kTexturedTerrainFar = 3;
inline constexpr uint8_t kClayPixelShaders = 4;  // the values of PipelineKey::textured

struct PipelineKey {
  uint8_t textured = 0;      // 0 flat, 1 textured, 2 terrain layer, 3 terrain far (the pass's pixel shaders)
  uint8_t blend = 0, src = 1, dst = 0, op = 0, src_alpha = 1, dst_alpha = 0, op_alpha = 0;
  uint8_t color_mask = 0xF;
  uint8_t depth_test = 1, depth_write = 1, depth_func = 1;   // Compare values
  uint8_t cull = 0;          // Cull values
  bool operator==(const PipelineKey&) const = default;
};

// The game's depth function on a depth buffer of the other direction.
inline capture::Compare MirrorCompare(capture::Compare f) {
  switch (f) {
    case capture::Compare::kLess: return capture::Compare::kGreater;
    case capture::Compare::kLessEqual: return capture::Compare::kGreaterEqual;
    case capture::Compare::kGreater: return capture::Compare::kLess;
    case capture::Compare::kGreaterEqual: return capture::Compare::kLessEqual;
    default: return f;
  }
}

// The depth function the pass draws with: its depth is not reversed.
inline capture::Compare ClayDepthFunc(const capture::RenderState& s) {
  return s.depth_reversed ? MirrorCompare(s.depth_func) : s.depth_func;
}

// What the RHI's enums hold (native_rhi.h: BlendFactor, BlendOp, CompareFunc).
inline bool RhiFactor(capture::Factor f) {
  return f == capture::Factor::kZero || f == capture::Factor::kOne || f == capture::Factor::kSrcAlpha ||
         f == capture::Factor::kInvSrcAlpha;
}
inline bool RhiBlendOp(capture::BlendOp op) {
  return op == capture::BlendOp::kAdd || op == capture::BlendOp::kMin;
}
inline bool BlendSupported(const capture::RenderState& s) {
  return RhiFactor(s.src) && RhiFactor(s.dst) && RhiFactor(s.src_alpha) && RhiFactor(s.dst_alpha) &&
         RhiBlendOp(s.op) && RhiBlendOp(s.op_alpha);
}
// False: the depth test is on with a function the RHI cannot express on the
// pass's depth; the draw then uses the default depth state.
inline bool DepthSupported(const capture::RenderState& s) {
  if (!s.depth_test) return true;
  const capture::Compare f = ClayDepthFunc(s);
  return f == capture::Compare::kAlways || f == capture::Compare::kLess || f == capture::Compare::kLessEqual;
}

// The captured cull mode in the pass's window space, where clockwise faces are
// front (the RHI has no front-face control; D3D12's default). The pass draws the
// game's own clip positions into a viewport of the same orientation, so a
// triangle keeps the winding it has in the game: with clockwise front faces
// (PA_SU_SC_MODE_CNTL bit 2) the mode is the captured one, with
// counter-clockwise ones front and back trade places. Checked against the
// emulated frame for meshes and terrain: frame-map section 13, "Render state".
inline capture::Cull ClayCull(capture::Cull cull, bool front_cw) {
  if (cull == capture::Cull::kNone || front_cw) return cull;
  return cull == capture::Cull::kFront ? capture::Cull::kBack : capture::Cull::kFront;
}

// An invalid state gives the default key of `textured`. A blend the RHI cannot
// express gives no blend (DecideDrawState leaves such a draw out), a depth
// function it cannot express the default depth state. Without the depth test
// nothing is written to depth, as on the console.
inline PipelineKey MakePipelineKey(const capture::RenderState& s, uint8_t textured) {
  PipelineKey k;
  k.textured = textured;
  if (!s.valid) return k;
  if (s.blend && BlendSupported(s)) {
    k.blend = 1;
    k.src = uint8_t(s.src); k.dst = uint8_t(s.dst); k.op = uint8_t(s.op);
    k.src_alpha = uint8_t(s.src_alpha); k.dst_alpha = uint8_t(s.dst_alpha); k.op_alpha = uint8_t(s.op_alpha);
  }
  k.color_mask = s.color_mask;
  if (!s.depth_test) {
    k.depth_test = 0;
    k.depth_write = 0;
  } else if (DepthSupported(s)) {
    k.depth_write = s.depth_write ? 1 : 0;
    k.depth_func = uint8_t(ClayDepthFunc(s));
  }
  k.cull = uint8_t(ClayCull(s.cull, s.front_cw));
  return k;
}

inline constexpr size_t kMaxClayPipelines = 64;
inline constexpr size_t kNoPipelineSlot = ~size_t(0);

// Index of `k` in `keys`, appended if absent and there is room; kNoPipelineSlot when the
// cache is full (the caller then draws with the default pipeline and counts the overflow).
inline size_t FindOrAddPipelineSlot(std::vector<PipelineKey>& keys, const PipelineKey& k,
                                    size_t max = kMaxClayPipelines) {
  for (size_t i = 0; i < keys.size(); ++i) {
    if (keys[i] == k) return i;
  }
  if (keys.size() >= max) return kNoPipelineSlot;
  keys.push_back(k);
  return keys.size() - 1;
}

// ClayConstants::alpha_mode: bits 0-2 the alpha test's compare function
// (capture::Compare), then the flags below; bits 5-6 are the albedo component
// (0-3 = x-w) that is the alpha; bits 16-23 the alpha-to-mask dither offsets
// (RenderState::alpha_to_mask_offsets), bits 24-25 the sample count they are
// applied under (RenderState::alpha_to_mask_samples). None of this is pipeline
// state: the pixel shader computes the thresholds.
inline constexpr uint32_t kAlphaTest = 1u << 3;        // discard unless alpha <func> alpha_ref
inline constexpr uint32_t kAlphaFromAlbedo = 1u << 4;  // alpha is an albedo component (else the literal 1)
inline constexpr uint32_t kAlphaBlend = 1u << 7;       // write the alpha (else 1)
inline constexpr uint32_t kAlphaToMask = 1u << 8;      // discard unless AlphaToMaskCovers
inline constexpr uint32_t kAlphaDivide = 1u << 9;      // the colour is the albedo's divided by its alpha
inline constexpr uint32_t kAlphaToMaskShift = 16;      // the four 2-bit offsets
inline constexpr uint32_t kAlphaToMaskSamplesShift = 24;  // the surface's sample count, 2 bits

// Alpha-to-mask, which the textured pixel shader does after the alpha test.
// The GPU covers a sample iff alpha >= base - offset * scale (the emulator's
// rule, SDK dxbc_translator_om.cpp CompletePixelShader_AlphaToMask), with the
// offset of the pixel's parity, index (x & 1) * 2 + (y & 1), two bits each from
// bit 0 of `offsets`, and per sample count of the surface (`samples`,
// RB_SURFACE_INFO bits 16-17):
//   0, no multisampling: one sample, base 1, scale 1/4;
//   1, two samples: bases 0.5 and 1, scale 1/8;
//   2, four samples: bases 0.75, 0.25, 0.5 and 1, scale 1/16.
// The clay target has one sample per pixel: the pixel (x, y) is kept iff at
// least half of the game's samples would be covered (ruling, 2026-10-07):
//   0: alpha >= 1 - offset / 4;
//   1: alpha >= 0.5 - offset / 8;
//   2: alpha >= 0.5 - offset / 16.
// Any other count is taken as 0. The alpha is not clamped and a NaN covers
// nothing. Under two samples, the main scene's count, 0xAA cuts at 0.25 at
// every pixel and the dither pattern 0x87 at 0.125, 0.375, 0.5, 0.25 for
// indices 0 to 3: a texel of alpha 0.5 or more is kept at every pixel, and a
// fading draw keeps none, one, two, three or all of each 2x2 block below that.
// A pixel of which the game covers one sample of two is drawn whole here: more
// solid than the game, never a hole the game does not have.
inline bool AlphaToMaskCovers(uint8_t offsets, uint32_t x, uint32_t y, float alpha, uint32_t samples = 0) {
  const uint32_t index = ((x & 1u) << 1) | (y & 1u);
  const uint32_t offset = (uint32_t(offsets) >> (2 * index)) & 3u;
  const float mask_base = (samples == 1u || samples == 2u) ? 0.5f : 1.0f;
  const float per_offset = samples == 1u ? 0.125f : samples == 2u ? 0.0625f : 0.25f;
  return alpha >= mask_base - float(offset) * per_offset;
}

// The alpha fields of the root constants; the default is an opaque draw.
struct ClayAlpha {
  uint32_t mode = 0;
  float ref = 0.0f;
  float scale = 1.0f;  // Material::alpha_scale
};

// What the pass does with one record.
struct DrawState {
  bool skip = false;  // blend-unknown: the record is not drawn
  PipelineKey key;
  ClayAlpha alpha;
  // The ClayStats counters the drawn record adds to.
  bool alpha_tested = false, blended = false, alpha_unknown = false, state_invalid = false;
};

// `textured`: the PipelineKey value the record is drawn with this frame (0: flat
// clay, its colour is not known). `flat_terrain`: a terrain patch drawn as flat
// clay; its blend is the layer's own, which flat clay does not have, so it is
// drawn opaque and not left out. A terrain record drawn with its material
// (`textured` 2 or 3) needs no recipe: the terrain pixel shaders form their own
// alpha (weight x fade, or 1), so its test and blend always apply, and the
// material's recipe, scale and colour division are not used. Rules
// (DecideAlpha): a blended draw whose colour is unknown, or whose blend the RHI
// cannot express, is left out; a test
// or blend without an alpha the pixel shader can form (no recipe, a flat draw,
// a recipe that needs another fetch) is drawn without the test and with alpha
// 1; an invalid state or an inexpressible depth function draws with the default
// state. A colour mask of 0 writes no colour, so its blend does not matter.
// A flat terrain draw whose depth state cannot be used tests less-equal with
// depth write instead of the default less (TerrainFlatDefaultKey): it has to
// cover the underlay of its square, which lies at the same depth.
inline DrawState DecideDrawState(const capture::RenderState& rs, const capture::Material& m,
                                 uint8_t textured, bool flat_terrain) {
  DrawState d;
  d.key = MakePipelineKey(rs, textured);
  d.state_invalid = !rs.valid || !DepthSupported(rs);
  if (flat_terrain && d.state_invalid) {
    d.key.depth_test = 1;
    d.key.depth_write = 1;
    d.key.depth_func = uint8_t(capture::Compare::kLessEqual);
  }
  if (!rs.valid) return d;
  const bool blend = rs.blend && rs.color_mask != 0 && !flat_terrain;
  if (!blend) {
    const PipelineKey opaque;
    d.key.blend = 0;
    d.key.src = opaque.src; d.key.dst = opaque.dst; d.key.op = opaque.op;
    d.key.src_alpha = opaque.src_alpha; d.key.dst_alpha = opaque.dst_alpha; d.key.op_alpha = opaque.op_alpha;
  } else if (!BlendSupported(rs)) {
    d.skip = true;
    return d;
  }
  const bool colour_known = textured != 0;
  const bool terrain = textured == kTexturedTerrainLayer || textured == kTexturedTerrainFar;
  const capture::AlphaSpec& a = m.alpha;
  const bool has_recipe = terrain || (colour_known && (a.source == capture::AlphaSource::kAlbedo ||
                                                       a.source == capture::AlphaSource::kOne));
  // The shader divides its colour by the alpha whatever the state is.
  if (has_recipe && !terrain && a.divide_colour) d.alpha.mode |= kAlphaDivide;
  const bool test = rs.alpha_test || rs.alpha_to_mask;
  switch (capture::DecideAlpha(test, blend, colour_known, has_recipe)) {
    case capture::AlphaRule::kOpaque:
      break;
    case capture::AlphaRule::kSkipBlendUnknown:
      d.skip = true;
      break;
    case capture::AlphaRule::kDrawAlphaUnknown:
      d.alpha_unknown = true;
      d.blended = blend;
      break;
    case capture::AlphaRule::kApply:
      d.alpha_tested = test;
      d.blended = blend;
      if (rs.alpha_test) d.alpha.mode |= (uint32_t(rs.alpha_func) & 7u) | kAlphaTest;
      if (rs.alpha_to_mask) {
        d.alpha.mode |= kAlphaToMask | (uint32_t(rs.alpha_to_mask_offsets) << kAlphaToMaskShift) |
                        (uint32_t(rs.alpha_to_mask_samples & 3) << kAlphaToMaskSamplesShift);
      }
      if (!terrain && a.source == capture::AlphaSource::kAlbedo) {
        d.alpha.mode |= kAlphaFromAlbedo | (uint32_t(a.component & 3) << 5);
      }
      if (blend) d.alpha.mode |= kAlphaBlend;
      d.alpha.ref = rs.alpha_ref;
      if (!terrain) d.alpha.scale = m.alpha_scale;
      break;
  }
  return d;
}

// Root constants, laid out as the clay HLSL cbuffer (36 dwords).
struct ClayConstants {
  float rows[16];
  uint32_t layout;
  int32_t base_vertex;
  uint32_t vertex_count;
  uint32_t color;
  float uv[4];          // u * uv[0] + uv[2], v * uv[1] + uv[3]
  uint32_t textured;    // 1: sample the albedo at t3
  uint32_t sampler;     // static sampler s0-s3 (SamplerIndex)
  uint32_t alpha_mode;  // ClayAlpha::mode
  float alpha_ref;
  float cut[4];         // eye xyz, squared distance (DrawRecord::cut; +inf: no cut)
  float alpha_scale;
  uint32_t light_mode;   // kLightMode* ("Native light" below); 0: the draw has no light and no vertex normal
  uint32_t light_count;  // elements the draw's normal (and colour) buffer holds: read for a vertex below it
  uint32_t pad2;
};
static_assert(sizeof(ClayConstants) == 144);

// The pass's binding layout: what each binding parameter of the clay shaders
// is (clay_pass.cpp creates the layout from this table, and binds by the
// parameter's index in it; test_native_shaders.cpp holds every resource each
// compiled shader declares against it, so that no shader can declare a
// register the layout does not bind for its stage). kBuffer: a structured
// buffer bound by address, which has NO bounds check on the GPU: whatever is
// bound there must hold every element the shader can load for the draw.
enum class ClayBindingKind : uint8_t { kConstants, kConstantBuffer, kBuffer, kTextures };
enum class ClayBindingStage : uint8_t { kAll, kVertex, kPixel };
struct ClayBinding {
  ClayBindingKind kind;
  uint8_t reg;    // b register (constants, constant buffer) or t register (buffer, first texture)
  uint8_t count;  // kConstants: 32-bit words; kTextures: textures at consecutive registers; else 1
  ClayBindingStage stage;
};
inline constexpr ClayBinding kClayBindings[] = {
    {ClayBindingKind::kConstants, 0, 36, ClayBindingStage::kAll},        // 0: b0 Draw (ClayConstants)
    {ClayBindingKind::kBuffer, 0, 1, ClayBindingStage::kAll},            // 1: t0 positions (float4)
    {ClayBindingKind::kBuffer, 1, 1, ClayBindingStage::kAll},            // 2: t1 indices (uint)
    {ClayBindingKind::kBuffer, 2, 1, ClayBindingStage::kAll},            // 3: t2 UVs (float2)
    {ClayBindingKind::kTextures, 3, 2, ClayBindingStage::kPixel},        // 4: t3 albedo, t4 the screen mask of the sun's
                                                                         //     shadow ("Native shadows" below; 1 x 1 white
                                                                         //     for a frame without one)
    {ClayBindingKind::kTextures, 5, 4, ClayBindingStage::kPixel},        // 5: t5..t8 terrain colour, weights, side, base map
    {ClayBindingKind::kConstantBuffer, 1, 1, ClayBindingStage::kAll},    // 6: b1 the draw's slot (Terrain or Object)
    {ClayBindingKind::kConstantBuffer, 2, 1, ClayBindingStage::kAll},    // 7: b2 Frame
    {ClayBindingKind::kBuffer, 9, 1, ClayBindingStage::kVertex},         // 8: t9 terrain normals (float2)
    {ClayBindingKind::kBuffer, 10, 1, ClayBindingStage::kVertex},        // 9: t10 object normals (float3)
    {ClayBindingKind::kBuffer, 11, 1, ClayBindingStage::kVertex},        // 10: t11 an object's word per vertex (uint): the
                                                                         //     stream colour, or the probe set's index
    {ClayBindingKind::kBuffer, 12, 1, ClayBindingStage::kVertex},        // 11: t12 probe weights (float4)
};
// (plan Task 5 of the shadows: the albedo's parameter became the pair and every register behind it
// moved up by one; the parameters' indices and the root words did not change)
inline constexpr uint32_t kClayAlbedoParam = 4, kClayMaskRegister = 4;
static_assert(kClayBindings[kClayAlbedoParam].kind == ClayBindingKind::kTextures && kClayBindings[kClayAlbedoParam].reg == 3 &&
              kClayBindings[kClayAlbedoParam].count == 2 && kClayBindings[kClayAlbedoParam].reg + 1 == kClayMaskRegister);
// (twelve parameters: the backend's limit, nrhi::kMaxBindingParams; clay_pass.cpp asserts it)
inline constexpr uint32_t kClayStaticSamplers = 4;  // s0..s3, pixel stage (SamplerIndex)
static_assert(kClayBindings[0].count * 4 == sizeof(ClayConstants));

// Words of the root signature the layout takes (64 at most): a constant block
// its words, a buffer or constant buffer address 2, a texture table 1.
constexpr uint32_t ClayRootWords() {
  uint32_t words = 0;
  for (const ClayBinding& b : kClayBindings) {
    words += b.kind == ClayBindingKind::kConstants ? b.count : b.kind == ClayBindingKind::kTextures ? 1u : 2u;
  }
  return words;
}
static_assert(ClayRootWords() == 56 && ClayRootWords() <= 64);

// The largest element (bytes) the vertex shader loads per vertex from the
// light's four buffers (t9 float2, t10 float3, t11 uint, t12 float4), and the
// element of the position buffer (float4). A draw that does not use one of
// the four has its own position buffer bound there: it holds an element of 16
// bytes for every vertex the draw can name, so any load the shader could make
// for the draw lies inside it.
inline constexpr uint32_t kClayLightElementBytesMax = 16, kClayPositionElementBytes = 16;
static_assert(kClayLightElementBytesMax <= kClayPositionElementBytes);
// The second buffers of the two vertex kinds without a colour stream keep to
// that: the probe buffer is two regions, float4 weights (read at t12) and one
// uint set index a vertex (read at t11 from the region's offset:
// normal_decode.h, BuildProbe); an instanced plant's colours are one uint a
// flat position (read at t11 like the stream colours) and its bend inputs one
// float4 a flat position (read at t12 like the weights: ExpandInstanceBend).
inline constexpr uint32_t kProbeWeightElementBytes = 16, kProbeSetElementBytes = 4, kInstanceColourElementBytes = 4,
                          kBendElementBytes = 16;
static_assert(kProbeWeightElementBytes <= kClayLightElementBytesMax && kProbeSetElementBytes <= kClayLightElementBytesMax &&
              kInstanceColourElementBytes <= kClayLightElementBytesMax && kBendElementBytes <= kClayLightElementBytesMax);

// ClayConstants::light_mode. kLightModeTerrainNormals: the vertex shader forms
// the game's terrain normal from the draw's buffer at t9 (float2 per grid
// point): for the steep-ground block and for the light. kLightModeObject: the
// vertex shader reads the stream normal at t10 and the draw's word at t11 and
// puts the normal through the draw's rows of cbuffer Object; the word is the
// stream colour, unless kLightModeProbe (with the object bit only): then it is
// the vertex's probe set, the vertex shader also reads the four weights at
// t12, and the colour is the probe colour made of them and the draw's twelve
// probe registers. kLightModeInstance (with the object bit only; plan Task
// 8b): the draw is an instanced plant: the normal at t10 is in the world
// already and goes through no rows, the word at t11 is the colour of the
// vertex's copy, t12 holds the bend inputs (the copy's up axis and the
// vertex's height), and the vertex shader makes of them the game's vertex-side
// colour and the bend of the normal; with kLightModePlantLit the plant's
// family is plant-lit (its pixel shader has the ambient factor: the vertex
// side exports the colour without the added light and the vector of the
// factor), without it plant (the vertex side's colour is the whole ambient).
// kLightModeLeaf (with the object and probe bits, without the instance bit;
// plan Task 8c): the draw is a tree leaf: the buffers are a probe record's
// (normals at t10, the probe set at t11, the weights at t12), and the vertex
// shader makes of them, the draw's rows and the frame's eye and sun what the
// game's leaf vertex shaders make: the ambient of the probe colour and the two
// scalars of the sun's light on the side of the card that faces the viewer.
// kLightModeLit: the pixel shader lights the draw with its family's function
// (the frame's block in cbuffer Frame, the draw's values in its slot at b1);
// without it the draw is the unlit linear colour of the materials picture.
inline constexpr uint32_t kLightModeTerrainNormals = 1u, kLightModeObject = 2u, kLightModeLit = 4u, kLightModeProbe = 8u,
                          kLightModeInstance = 16u, kLightModePlantLit = 32u, kLightModeLeaf = 64u;
// Every value the bits can make (the shader tests compile each shader once per value).
inline constexpr uint32_t kLightModeCount = 128u;

// The light_mode of a lit mesh record by its vertex kind and family: a colour
// stream's object (6), an object or trunk with probe colours (14), an
// instanced plant of family plant (22) or plant-lit (54), a tree leaf (78: a
// probe record with the leaf bit); 0 for a pair the pass does not light.
constexpr uint32_t LitObjectMode(capture::LightVertexKind kind, capture::LightFamily family) {
  const bool object = family == capture::LightFamily::kObject, trunk = family == capture::LightFamily::kTreeTrunk;
  switch (kind) {
    case capture::LightVertexKind::kStream: return object ? kLightModeObject | kLightModeLit : 0;
    case capture::LightVertexKind::kProbe:
      if (family == capture::LightFamily::kTreeLeaf) return kLightModeObject | kLightModeLit | kLightModeProbe | kLightModeLeaf;
      return object || trunk ? kLightModeObject | kLightModeLit | kLightModeProbe : 0;
    case capture::LightVertexKind::kInstance:
      if (family == capture::LightFamily::kPlant) return kLightModeObject | kLightModeLit | kLightModeInstance;
      if (family == capture::LightFamily::kPlantLit) {
        return kLightModeObject | kLightModeLit | kLightModeInstance | kLightModePlantLit;
      }
      return 0;
    default: return 0;
  }
}

// What the pass binds at the light's four buffer registers (t9 to t12:
// kClayBindings[kClayLightBufferParam0 + i]) for a draw with `light_mode`: the
// draw's own buffer of that register where the mode names it, else the draw's
// positions. EVERY draw gets all four: nothing may stay bound from another
// draw, whose (smaller) buffer this draw's vertex indices would address.
// kProbeSets and kProbeWeights are the two regions of the draw's probe buffer
// (LightBufferOffset); kColours is a colour stream's buffer and never an
// instance set's; kInstanceColours and kBend are an instanced plant's two
// buffers (its copies' colours; the bend inputs). The instance bit decides
// before the probe bit, as in the vertex shader (the pass makes no mode with
// both). A leaf is a probe record with the leaf bit: the probe buffer's two
// regions. The three mesh blocks of the vertex shader exclude each other by
// the mode (LightModeBlock: its own tests), and a mode none of them takes,
// which the pass never makes, loads from none of t9 to t12: the positions.
// The terrain normals are named only where no mesh block runs: a mode with
// the terrain bit and a mesh block (never made either) has its normal from
// the block, its compiled program loads nothing from t9, and a mesh record
// has no buffer of two floats a vertex to bind there.
enum class ClayLightBuffer : uint8_t {
  kPositions, kTerrainNormals, kNormals, kColours, kProbeSets, kProbeWeights, kInstanceColours, kBend
};
inline constexpr uint32_t kClayLightBufferParam0 = 8, kClayLightBufferCount = 4;
struct ClayLightBuffers {
  ClayLightBuffer source[kClayLightBufferCount];  // t9, t10, t11, t12
};
// Which of the vertex shader's three mesh blocks a mode takes, by the shader's own tests (kClayVs):
// an object's or trunk's with the object bit and neither the instance nor the leaf bit; an instanced
// plant's with the object and instance bits and no leaf bit; a leaf's with the object, probe and
// leaf bits and no instance bit; none otherwise.
enum class LightModeBlock : uint8_t { kNone, kObject, kPlant, kLeaf };
constexpr LightModeBlock LightModeBlockOf(uint32_t light_mode) {
  constexpr uint32_t kinds = kLightModeObject | kLightModeInstance | kLightModeLeaf;
  if ((light_mode & kinds) == kLightModeObject) return LightModeBlock::kObject;
  if ((light_mode & kinds) == (kLightModeObject | kLightModeInstance)) return LightModeBlock::kPlant;
  if ((light_mode & (kinds | kLightModeProbe)) == (kLightModeObject | kLightModeLeaf | kLightModeProbe)) return LightModeBlock::kLeaf;
  return LightModeBlock::kNone;
}
constexpr ClayLightBuffers LightBufferSources(uint32_t light_mode) {
  const bool terrain = (light_mode & kLightModeTerrainNormals) != 0;
  const LightModeBlock block = LightModeBlockOf(light_mode);
  const bool object = block == LightModeBlock::kObject, instance = block == LightModeBlock::kPlant;
  const bool probe = block == LightModeBlock::kLeaf || (object && (light_mode & kLightModeProbe) != 0);
  return {{terrain && block == LightModeBlock::kNone ? ClayLightBuffer::kTerrainNormals : ClayLightBuffer::kPositions,
           block != LightModeBlock::kNone ? ClayLightBuffer::kNormals : ClayLightBuffer::kPositions,
           instance ? ClayLightBuffer::kInstanceColours
           : probe  ? ClayLightBuffer::kProbeSets
           : object ? ClayLightBuffer::kColours
                    : ClayLightBuffer::kPositions,
           instance ? ClayLightBuffer::kBend : probe ? ClayLightBuffer::kProbeWeights : ClayLightBuffer::kPositions}};
}
static_assert(kClayBindings[kClayLightBufferParam0].reg == 9 && kClayBindings[kClayLightBufferParam0 + 1].reg == 10 &&
              kClayBindings[kClayLightBufferParam0 + 2].reg == 11 && kClayBindings[kClayLightBufferParam0 + 3].reg == 12 &&
              std::size(kClayBindings) == kClayLightBufferParam0 + kClayLightBufferCount);
// The byte offset a source is bound from in its buffer: the set indices are
// the probe buffer's second region, behind the weights of its `probe_count`
// vertices (ProbeSetOffset: a multiple of 256 bytes, which the RHI's bind
// offsets must be); everything else is bound from the buffer's start.
// `probe_count` is the count the probe buffer was built with, never the
// draw's light_count (the smaller of that and the normals' count).
constexpr uint64_t LightBufferOffset(ClayLightBuffer source, uint32_t probe_count) {
  return source == ClayLightBuffer::kProbeSets ? ProbeSetOffset(probe_count) : 0;
}

inline ClayConstants MakeClayConstants(const capture::DrawRecord& r, uint32_t vertex_count,
                                       uint32_t color, const float uv[4], bool textured,
                                       uint32_t sampler, const ClayAlpha& alpha = {}, uint32_t light_mode = 0,
                                       uint32_t light_count = 0) {
  ClayConstants c{};
  std::copy(std::begin(r.rows), std::end(r.rows), std::begin(c.rows));
  c.layout = r.layout == capture::TransformLayout::kCombine ? 1u : 0u;
  c.base_vertex = r.base_vertex;
  c.vertex_count = vertex_count;
  c.color = color;
  std::copy(uv, uv + 4, std::begin(c.uv));
  c.textured = textured ? 1u : 0u;
  c.sampler = sampler;
  c.alpha_mode = alpha.mode;
  c.alpha_ref = alpha.ref;
  std::copy(std::begin(r.cut), std::end(r.cut), std::begin(c.cut));
  c.alpha_scale = alpha.scale;
  c.light_mode = light_mode;
  // Never more than the positions: the shader reads a light buffer only for a vertex it draws.
  c.light_count = light_mode ? std::min(light_count, vertex_count) : 0u;
  return c;
}

// --- Linear target and output mapping (frame-map section 14, "Linear target") ---
// The pass draws linear colour into a float target, as the game does, and one
// full-screen step (shaders::kOutputMapPs) makes the display values of the 8-bit
// target the composite views sample: display = sqrt(saturate(linear * word)),
// the word 1 in every frame (kClayMapExposure). The exposure E of a lit frame is
// not the mapping's: the lit draws write their value times E ("Native light"
// below), so the unlit draws and the clear of a lit frame are shown as in a
// frame without light. The functions below are the HLSL's rules;
// test_native_shaders.cpp pins the statements they mirror.

// The output mapping for one channel. Only a value in [0, 1] comes out: a
// negative value, a NaN (no comparison holds for it) and 0 * infinity give 0,
// +infinity gives 1.
inline float OutputMap(float linear, float exposure) {
  const float v = linear * exposure;
  if (!(v > 0.0f)) return 0.0f;
  return std::sqrt(v < 1.0f ? v : 1.0f);
}

// What a clay pixel shader writes for the display value `display` (albedo or
// clay colour times the facet shade: the value the 8-bit target held, which
// clamped it to [0, 1]): its square, so that the mapping with E = 1 gives the
// value back. The shade therefore multiplies before the square. A NaN gives 0.
inline float ClayLinear(float display) {
  if (!(display > 0.0f)) return 0.0f;
  const float d = display < 1.0f ? display : 1.0f;
  return d * d;
}

// The terrain shaders have the linear value itself (the game's A = tf0^2,
// F = tf13^2, or the steep-ground mix) and the shade of the display value:
// (sqrt(v) * shade)^2 = v * shade^2, clamped as ClayLinear clamps.
inline float ClayLinearShaded(float linear, float shade) {
  const float v = linear * shade * shade;
  if (!(v > 0.0f)) return 0.0f;
  return v < 1.0f ? v : 1.0f;
}

// The pass's clear colour: the 8-bit levels the old target showed where nothing
// is drawn (it was cleared to 0.08, 0.08, 0.1; 20, 20, 25 in the shots), and
// for the linear target the values nearest those levels' squares that its
// channels hold exactly (101 steps of 2^-14 in the 11-bit floats, 39 of 2^-12
// in the 10-bit one), so that the clear does not depend on a GPU's rounding;
// the mapping returns the three levels. The fourth is the alpha of the clear
// call; the target has none.
// The same levels in a lit frame and in one without light (test case 418): the
// exposure is in the lit draws' values, not in the mapping (the final fix wave;
// before it the mapping multiplied every pixel by E and a lit frame showed the
// clear at 30, 30, 37).
inline constexpr int kClayClearLevels[3] = {20, 20, 25};
inline constexpr float kClayClearLinear[4] = {101.0f / 16384.0f, 101.0f / 16384.0f, 39.0f / 4096.0f, 1.0f};

// fable2_native_light. Off: no draw is lit, which is the materials picture. On: the draws of the families the pass lights are lit in
// a frame that has a light block ("Native light" below); every other draw,
// and every draw of a frame without a block, is drawn as with the switch off.
enum class ClayLight : uint8_t { kOff, kOn };

// The words of an on / off switch (fable2_native_light, and fable2_native_shadows, which is read with
// this parser): on, true, 1 and off, false, 0, in any case, blanks around them ignored. Anything else
// is off and not recognised (the caller warns once, with kClaySwitchWords).
inline constexpr const char* kClaySwitchWords = "on, true, 1, off, false, 0";
inline ClayLight ParseClayLight(std::string_view text, bool* recognized) {
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
  while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
  std::string lower(text);
  for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
  const bool on = lower == "on" || lower == "true" || lower == "1";
  if (recognized) *recognized = on || lower == "off" || lower == "false" || lower == "0";
  return on ? ClayLight::kOn : ClayLight::kOff;
}

// Whether the pass asks the geometry cache for the light's vertex inputs of a frame (the normals of
// the mesh records, their colours, probe buffers and bend inputs, the terrain normals of ground
// without the steep block): only with the light on. With it off nothing reads them (20 MB at the
// bridge, built for nothing until the final fix wave); a run that turns the light on builds them in
// the first frame drawn with it, before that frame's draws, as a run started with the light on
// does in its first frame. A frame with the switch on and no light block builds them all the same:
// the next frame has the block.
inline bool ClayLightInputsBuilt(ClayLight light) { return light == ClayLight::kOn; }

// The exposure of a lit frame, calibrated once (spec, design 4; plan Task 9) on
// the dirt patch at the bridge ("dirt patch" of docs/native-renderer/
// lighting-regions.json) and fixed here. It stands in for the game's own
// exposure, which belongs to its post chain and is not drawn.
// Evidence: run 341 (binary of commit c4d9e57, exposure 1, a quiet machine, RUN
// UNCHECKED), log out/native-evidence/lighting/task8cq_bridge_fable_2_341.log,
// shots task8cq_bridge_shot_36s.png (emulated), _40s.png (native, lit),
// _44s.png (emulated). tools/xdk_sigmatch/region_compare.py --calibrate
// "dirt patch": mean linear luminance 0.32914 and 0.33498 emulated, 0.14926
// native, ratio 0.4495, E = 2.2248 (red 2.30, green 2.20, blue 1.97 taken
// alone); the two emulated shots differ by 1.8%, so two decimals are kept.
// Output in out/native-evidence/lighting/task9/e1_regions_bridge_341.txt.
// Frame-map section 14, "Validation", has what this one number does and does
// not bring into line.
inline constexpr float kLitExposure = 2.22f;

// The first word of the output mapping's constants (cbuffer Map's `exposure`): 1 in every frame,
// lit or not. The mapping takes the target's values as they are.
inline constexpr float kClayMapExposure = 1.0f;

// The exposure E of the lit output: one number for the frame, no draw's own, and the lit draws'
// alone. kLitExposure with light on: MakeClayFrameConstants puts it into a lit frame's block
// (ClayFrameConstants::exposure, the one place a shader gets it from), and each lit pixel path
// writes ClayLit(value * E). 1 with light off, where nothing is lit and nothing reads it. The lit
// linear values are the game's before its output scale c20.w (16), which its own later passes
// take back. Until the final fix wave E was the mapping's word, which made every unlit draw and
// the clear of a lit frame sqrt(E) = 1.49 times brighter on the display.
inline float ClayExposure(ClayLight light) {
  switch (light) {
    case ClayLight::kOff: return 1.0f;
    case ClayLight::kOn: return kLitExposure;
  }
  return 1.0f;
}

// Bytes per texel of the pass's three targets: the linear colour
// (R11G11B10_FLOAT: 11-bit floats for red and green, a 10-bit float for blue,
// no sign and no alpha), the mapped colour (R8G8B8A8_UNORM) and the depth
// (D32_FLOAT). The 32-bit float format was taken for the frame rate (ruling,
// 2026-10-08): R16G16B16A16_FLOAT, 8 bytes, returns every 8-bit level exactly
// but cost 2 to 3 ms a frame more in one set of game runs (later runs did not
// separate the two formats); this one is up to two levels off
// in red and green and up to three in blue, always darker where the GPU rounds
// toward zero as this one does (one level off at most for a value that is an
// 8-bit level's square; test_clay_logic.cpp, cases 397 to
// 400 and 416; frame-map section 14, "Linear target"). The linear format is one
// for every pipeline of the pass, so PipelineKey has no field for it.
inline constexpr uint32_t kClayLinearTexelBytes = 4, kClayMappedTexelBytes = 4, kClayDepthTexelBytes = 4;

inline uint64_t ClayTargetBytes(uint32_t width, uint32_t height, uint32_t texel_bytes) {
  return uint64_t(width) * height * texel_bytes;
}

// The log line of the pass's targets, written when they are created.
inline std::string FormatTargetText(uint32_t width, uint32_t height) {
  const double mb = 1024.0 * 1024.0;
  char buf[192];
  std::snprintf(buf, sizeof(buf),
                "targets %u x %u: linear R11G11B10_FLOAT %.1f MB, mapped R8G8B8A8_UNORM %.1f MB, "
                "depth D32_FLOAT %.1f MB",
                width, height, double(ClayTargetBytes(width, height, kClayLinearTexelBytes)) / mb,
                double(ClayTargetBytes(width, height, kClayMappedTexelBytes)) / mb,
                double(ClayTargetBytes(width, height, kClayDepthTexelBytes)) / mb);
  return buf;
}

// --- Native light (frame-map section 14, "Native light"; docs/native-renderer/lighting-formulas.md) ---
// With fable2_native_light on, in a frame that has a light block, the draws of
// seven families are lit by the game's own ambient, baked and sun light, in
// linear space, their sun shadowed by the screen mask ("Native shadows" below; the mask is 1 in a
// frame without one, which is the picture without shadows): the terrain layers,
// the far terrain, the objects (their baked term from the colour stream of
// vertex shader 0xECD6..., or the probe colour of 0x79EA..., 0xD4D5...,
// 0xA1F7...), the tree trunks (probe colours, 0x475E...) and the instanced
// plants without and with the ambient factor (the colour of the vertex's copy
// and the normal bent towards the copy's up axis: the seven plant vertex
// shaders), each with its two- and four-light forms, and the tree leaves (the
// three leaf vertex shaders: plan Task 8c). Every other draw (shaders without a
// family), and every draw of a frame without a block, is
// the unlit colour of the materials picture, counted by reason (ClayUnlit).
//
// The functions below are the pure mirrors of the HLSL light functions
// (shaders::kClayLightHlsl, same names and argument lists), which are the
// functions of tools/xdk_sigmatch/light_formulas.py term for term, with its
// guards: a vector without a direction or with a component that is not a
// number normalises to zero, the power of a zero base is 0, a scalar that is
// not a number counts as 0, and every result is a number. The tests pin both
// against the same literals (tests/test_light_check.py, PinnedForTheRenderer;
// tests/native/test_clay_logic.cpp).
//
// The value is the formula's WITHOUT the game's output scale c20.w (16): the
// game multiplies by it at the end of every scene shader and its later passes
// take it back; here the exposure of the lit output stands in its place
// (ClayExposure: each lit pixel path multiplies its value by it). An
// object's c49.xyz is inside its function (LightObject); its c26.xyz, which
// the game applies together with c20.w after the fog, multiplies the function's
// result (ClayObjectConstants::c26; (1, 1, 1) in every row captured).

inline float LightFinite(float x) { return std::isfinite(x) ? x : 0.0f; }

// Clamp to [0, 1]; a NaN gives 0.
inline float LightSat(float x) {
  if (x != x) return 0.0f;
  return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

inline float LightDot(const Float3& a, const Float3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// v / |v|; (0, 0, 0) for a vector without a length or with a component that is not a number.
// The zero normal is never made a direction (ruling: N = 0 gives no sun and t = 0, the ambient at
// its floor, which is what the game's saturating dot products give).
inline Float3 LightNormalize(const Float3& v) {
  // (a component that is not a number makes the squared length one too: NaN or infinity)
  const float n2 = LightDot(v, v);
  if (!(n2 > 0.0f) || !std::isfinite(n2)) return {0.0f, 0.0f, 0.0f};
  const float k = 1.0f / std::sqrt(n2);
  return {v.x * k, v.y * k, v.z * k};
}

// |base|^exponent per channel, as the game computes it (exp2(exponent * log2(|base|))); 0 for a
// zero base, a base or an exponent that is not a number, and a result that is none.
inline Float3 LightPow(const Float3& base, float exponent) {
  if (!std::isfinite(exponent)) return {0.0f, 0.0f, 0.0f};
  auto one = [&](float b) {
    b = std::fabs(b);
    return b > 0.0f && std::isfinite(b) ? LightFinite(std::pow(b, exponent)) : 0.0f;
  };
  return {one(base.x), one(base.y), one(base.z)};
}

// The ambient factor L = floor + (1 - floor) * t^2 per channel, t clamped to [0, 1].
inline Float3 AmbientFactor(const Float3& floor, float t) {
  t = LightSat(t);
  auto one = [&](float f) {
    f = LightFinite(f);
    return f + (1.0f - f) * t * t;
  };
  return {one(floor.x), one(floor.y), one(floor.z)};
}

// The vertex shaders' vector for t: lerp(normal, axis, (1 + normal . axis) / 2), not normalised.
// normal: the vertex normal as the vertex shader has it (terrain: the decoded map normal; objects:
// M n); axis: vertex c19.xyz (= pixel c23.xyz, the block's axis).
inline Float3 AmbientAxis(const Float3& normal, const Float3& axis) {
  const float k = 0.5f + 0.5f * LightDot(normal, axis);
  return {LightFinite(normal.x * (1.0f - k) + axis.x * k), LightFinite(normal.y * (1.0f - k) + axis.y * k),
          LightFinite(normal.z * (1.0f - k) + axis.z * k)};
}

// t = sat(normalize(axis_vector) . normalize(normal)); 0 when either has no direction.
inline float AmbientT(const Float3& axis_vector, const Float3& normal) {
  return LightSat(LightDot(LightNormalize(axis_vector), LightNormalize(normal)));
}

// The colour-stream vertex shader's o6: pow(|colour|, power) per channel (power: its literal 2.2).
inline Float3 VertexColour(const Float3& stream_colour, float power) { return LightPow(stream_colour, power); }

// The probe vertex shaders' o6 (lighting-formulas.md, "Objects", probe_colour): of the draw's twelve
// probe registers c28..c39 (`probe`), the set at `set` (the vertex's a0: 0, 4 or 8), the base
// c[28 + a0].xyz plus the four-component dot products of the vertex's weights with c[29 + a0],
// c[30 + a0], c[31 + a0] for red, green and blue. No power here: the pixel shader's pow(|x|, c21.w) is
// the only one the colour takes, and its absolute value is what the game computes for a colour that is
// zero or negative (the weights are signed; LightPow). The set is clamped to the last one, as the clay
// vertex shader clamps it: the buffer holds 0, 4 or 8 only (normal_decode.h, DecodeProbeSets), and the
// clamp keeps any other word inside the twelve registers.
inline constexpr uint32_t kProbeSetMax = 8;
static_assert(kProbeSetMax + 4 == capture::kLightProbeRegisters);
constexpr uint32_t ProbeSetClamp(uint32_t set) { return set < kProbeSetMax ? set : kProbeSetMax; }
inline Float3 ProbeColour(const float probe[capture::kLightProbeRegisters][4], uint32_t set, const float weights[4]) {
  const uint32_t a0 = ProbeSetClamp(set);
  auto one = [&](int channel) {
    const float* row = probe[a0 + 1 + channel];
    const float sum = row[0] * weights[0] + row[1] * weights[1] + row[2] * weights[2] + row[3] * weights[3];
    return LightFinite(LightFinite(probe[a0][channel]) + sum);
  };
  return {one(0), one(1), one(2)};
}

// Terrain layers only: sat(k * (N . c109.xyz)), k the pixel shader's literal 8.
inline float FacingGate(float n_dot_c109, float k) { return LightSat(LightFinite(k) * LightFinite(n_dot_c109)); }

// The one-light gate on the sun: facing * mask (the screen mask at the pixel; 1 in a frame without one).
inline float SunGateSingle(float mask, float facing) { return LightSat(facing) * LightSat(mask); }

namespace light_detail {
// pow(base, tint_pow) * tint * L + add, per channel.
inline Float3 Ambient(const Float3& base, const Float3& tint, float tint_pow, const Float3& add, const Float3& floor,
                      float t) {
  const Float3 lit = AmbientFactor(floor, t);
  const Float3 p = LightPow(base, tint_pow);
  return {p.x * LightFinite(tint.x) * lit.x + LightFinite(add.x), p.y * LightFinite(tint.y) * lit.y + LightFinite(add.y),
          p.z * LightFinite(tint.z) * lit.z + LightFinite(add.z)};
}
// sun_col * sat(n_dot_sun) * sun_gate.
inline Float3 Sun(const Float3& sun_col, float n_dot_sun, float sun_gate) {
  const float k = LightSat(n_dot_sun) * LightFinite(sun_gate);
  return {LightFinite(sun_col.x) * k, LightFinite(sun_col.y) * k, LightFinite(sun_col.z) * k};
}
}  // namespace light_detail

// Terrain layers: A * c11x * (pow(baked, tint_pow) * tint * L + add) + A * sun_col * n_dot_sun * sun_gate.
inline Float3 LightTerrainLayer(const Float3& A, const Float3& baked, const Float3& tint, float tint_pow,
                                const Float3& add, const Float3& floor, float t, float n_dot_sun,
                                const Float3& sun_col, float c11x, float sun_gate) {
  const Float3 amb = light_detail::Ambient(baked, tint, tint_pow, add, floor, t);
  const Float3 sun = light_detail::Sun(sun_col, n_dot_sun, sun_gate);
  const float k = LightFinite(c11x);
  return {LightFinite(LightFinite(A.x) * (k * amb.x + sun.x)), LightFinite(LightFinite(A.y) * (k * amb.y + sun.y)),
          LightFinite(LightFinite(A.z) * (k * amb.z + sun.z))};
}

// Far terrain: F * (pow(baked, tint_pow) * tint * L + add) + F * c11x * sun_col * n_dot_sun.
inline Float3 LightTerrainFar(const Float3& F, const Float3& baked, const Float3& tint, float tint_pow,
                              const Float3& add, const Float3& floor, float t, float n_dot_sun, const Float3& sun_col,
                              float c11x) {
  const Float3 amb = light_detail::Ambient(baked, tint, tint_pow, add, floor, t);
  const Float3 sun = light_detail::Sun(sun_col, n_dot_sun, 1.0f);
  const float k = LightFinite(c11x);
  return {LightFinite(LightFinite(F.x) * (amb.x + k * sun.x)), LightFinite(LightFinite(F.y) * (amb.y + k * sun.y)),
          LightFinite(LightFinite(F.z) * (amb.z + k * sun.z))};
}

// Lit objects: (A * c27w * (pow(vcolour, tint_pow) * tint * L + add) + A * sun_col * n_dot_sun * sun_gate) * c49.
inline Float3 LightObject(const Float3& A, const Float3& vcolour, const Float3& tint, float tint_pow,
                          const Float3& add, const Float3& floor, float t, float n_dot_sun, const Float3& sun_col,
                          float c27w, float sun_gate, const Float3& c49) {
  const Float3 amb = light_detail::Ambient(vcolour, tint, tint_pow, add, floor, t);
  const Float3 sun = light_detail::Sun(sun_col, n_dot_sun, sun_gate);
  const float k = LightFinite(c27w);
  return {LightFinite(LightFinite(A.x) * (k * amb.x + sun.x) * LightFinite(c49.x)),
          LightFinite(LightFinite(A.y) * (k * amb.y + sun.y) * LightFinite(c49.y)),
          LightFinite(LightFinite(A.z) * (k * amb.z + sun.z) * LightFinite(c49.z))};
}

// --- The instanced plants (plan Task 8b; lighting-formulas.md, "Instanced plants") ---
// The game's plant vertex shaders compute, per vertex: the colour of the vertex's copy raised to
// vertex c18.w and times c18.xyz, plus c54.xyz in the family whose pixel shader has no ambient factor
// (0x8123... instr 17, 26 to 33: logarithms of the absolute value, the exponent, the powers, `mad r7,
// r7, c18, c54`; export o4), and the bend of the normal towards the copy's up axis (instr 38 to 61;
// export o3). The pixel shaders blend the interpolated normal towards the bend and call the family's
// function. The mirrors below are split the same way.

// What the instance colour's exponent is, over the frame's c21.w: vertex c18 is (pixel c21.xyz,
// 2.2 * pixel c21.w) and vertex c54 is pixel c20 at every draw compared (bit for bit, the product as
// a float: 1,688 main-scene plant rows of two captures, out/native-evidence/lighting/task8b/
// vertex_copies_8b.txt; 32,880 draws of the GPU's logs, frame-map section 14, "The vertex side"), so
// the frame's block gives the vertex side's constants: an observation of two scenes, not a rule
// read from the game. It is the objects' power of the stream colour and the ambient exponent in one.
inline constexpr float kInstanceColourPower = 2.2f;

// The plant vertex shaders' colour (light_formulas.instance_ambient): pow(|instance_colour|,
// tint_pow) * tint + add. Family plant: add = c54.xyz (the whole ambient, interpolator o4 of
// 0x8123...); family plant-lit: add = 0 (the pixel shader applies the ambient factor and adds c20).
inline Float3 InstanceAmbient(const Float3& instance_colour, const Float3& tint, float tint_pow, const Float3& add) {
  const Float3 p = LightPow(instance_colour, tint_pow);
  return {LightFinite(p.x * LightFinite(tint.x) + LightFinite(add.x)), LightFinite(p.y * LightFinite(tint.y) + LightFinite(add.y)),
          LightFinite(p.z * LightFinite(tint.z) + LightFinite(add.z))};
}

// The height blend of a plant vertex: sat(z * c45.y + c45.z), z the vertex's third position
// component in its copy's space (0x8123... instr 38, `mad_sat`). A value that is not a number gives 0.
inline float PlantHeight(float z, float scale, float offset) { return LightSat(z * scale + offset); }

// The vertex side of the bent normal (light_formulas.plant_normal, its first line and what the
// interpolator carries): (strength * bent, strength * h) with bent = normalize(lerp(normal, up, h)),
// h = sat(height). normal: normalize(M n), the instanced normal; up: the copy's up axis (unit, or zero
// for a copy that has none); strength: vertex c45.x. A strength of 0 gives (0, 0, 0, 0): no bend.
inline capture::Float4 PlantBend(const Float3& normal, const Float3& up, float height, float strength) {
  const float h = LightSat(height);
  const Float3 bent = LightNormalize(
      {normal.x + (up.x - normal.x) * h, normal.y + (up.y - normal.y) * h, normal.z + (up.z - normal.z) * h});
  const float s = LightFinite(strength);
  return {s * bent.x, s * bent.y, s * bent.z, s * h};
}

// The pixel side: normalize(lerp(normalize(normal), bend.xyz, sat(bend.w))) (0xA7E4... instr 11 to
// 13, 15 to 20). Zero when the result has no direction or a component that is not a number.
inline Float3 PlantNormal(const Float3& normal, const capture::Float4& bend) {
  const Float3 n = LightNormalize(normal);
  const float k = LightSat(bend.w);
  return LightNormalize({n.x + (bend.x - n.x) * k, n.y + (bend.y - n.y) * k, n.z + (bend.z - n.z) * k});
}

// Plants without an ambient factor (light_formulas.plant): A * vs_ambient + A * sun_col * n_dot_sun * sun_gate.
inline Float3 LightPlant(const Float3& A, const Float3& vs_ambient, float n_dot_sun, const Float3& sun_col, float sun_gate) {
  const Float3 sun = light_detail::Sun(sun_col, n_dot_sun, sun_gate);
  return {LightFinite(LightFinite(A.x) * (LightFinite(vs_ambient.x) + sun.x)), LightFinite(LightFinite(A.y) * (LightFinite(vs_ambient.y) + sun.y)),
          LightFinite(LightFinite(A.z) * (LightFinite(vs_ambient.z) + sun.z))};
}

// Plants with the ambient factor: the object form with the instance colour, c27.w = 1 and c49 = 1
// (light_formulas.lit_object with those; these shaders read neither), the power and the tint applied
// by the vertex shader: A * (vs_base * L + add) + A * sun_col * n_dot_sun * sun_gate, vs_base =
// InstanceAmbient(colour, tint, tint_pow, 0), L from t.
inline Float3 LightPlantLit(const Float3& A, const Float3& vs_base, const Float3& add, const Float3& floor, float t, float n_dot_sun,
                            const Float3& sun_col, float sun_gate) {
  const Float3 lit = AmbientFactor(floor, t);
  const Float3 sun = light_detail::Sun(sun_col, n_dot_sun, sun_gate);
  auto one = [](float a, float base, float l, float plus, float s) {
    return LightFinite(LightFinite(a) * (LightFinite(base) * l + LightFinite(plus) + s));
  };
  return {one(A.x, vs_base.x, lit.x, add.x, sun.x), one(A.y, vs_base.y, lit.y, add.y, sun.y), one(A.z, vs_base.z, lit.z, add.z, sun.z)};
}

// --- The tree leaves (plan Task 8c; lighting-formulas.md, "Tree leaves") ---
// The game's leaf vertex shaders (0xA584..., and 0xD7DD... / 0x2D40... with two and four lights)
// compute everything but the texture and the screen mask per vertex: the ambient
// pow(|probe colour|, c18.w * literal) * c18.xyz + c54.xyz (export o1), and the light's two scalars
// on the side of the card that faces the viewer, times the light's colour (o2 front, o3 back). The
// cards are drawn without culling (register 0x2205 = 0x00218000 in every leaf row captured), and
// the shader turns the normal to the viewer instead (0xA584... instr 108 to 110). The pixel shader
// 0x7CD5... adds them up on the squared albedo. The mirrors below are split the same way, but for
// one thing: the two scalars are interpolated WITHOUT the sun's colour, which the pixel side
// multiplies in (LightLeaf): the colour is one per frame, so the product commutes with the
// interpolation.
//
// The sun. The light registers are the vertex shader's own (c76.. positions or directions, c83..
// colours, c90 the blend of cone and range). At every leaf draw compared (176 + 328 main-scene rows
// of two captures whose rows carry the followed vertex registers:
// out/native-evidence/lighting/task8c/leaf_rows_check.txt) the frame's sun stands there bit for bit,
// in slot 0 of the one-light shader, slot 1 of the two-light and slot 2 of the four-light one, with
// w = 0 (a direction) and c90 zero at its slot (no cone, no range: spot = 1); the slots before it
// are lanterns, which the pass does not draw (ruling 5; light_check.py --multi-sun: with the
// lanterns dark the two- and four-light shaders are the one-light function of that sun). Vertex c9
// (the eye) is pixel c19, c18 is (pixel c21.xyz, 2.2 * c21.w) and c54 is pixel c20 in those rows,
// bit for bit: the frame's block gives them. Observations of two daylight scenes, not rules read
// from the game.

// The literal 1 / 2.2 on the ambient exponent, as the three vertex shaders' literal tables hold it
// (c249.w of 0xA584..., c248.w of 0xD7DD..., c255.x of 0x2D40...: "vs_lit" of every row of theirs in
// the captures, task8c/leaf_literals.txt), and the exponent's factor over the frame's c21.w:
// c18.w * literal = (2.2 * c21.w) * 0.4545.
inline constexpr float kLeafGamma = 0.45454543828964233f;
inline constexpr float kLeafColourPower = kInstanceColourPower * kLeafGamma;
// The pixel shader's two literals on the screen mask, c255.w and c255.z (0.6 and 0.4 in every
// "ps_lit" of 0x7CD5... in the captures and at every execution in the GPU's logs: frame-map
// section 14, "The shaders' literals").
inline constexpr float kLeafMaskGain = 0.6f, kLeafMaskBias = 0.4f;

// The leaf vertex shaders' two scalars of one light (light_formulas.leaf_light):
//   Nf    = the normal, negated when it faces away from the viewer (view . normal < 0)
//   front = sat((light_dir . Nf + wrap) / (1 + wrap)) * spot
//   back  = sat(-(light_dir . Nf)) * (1 - (1 - sat(view . Nf))^2) * spot
// normal: M n as it is, NOT normalised (0xA584... instr 83, 87, 95, 96: the rows, times c7.w, and
// nothing more); view: the unit vector from the vertex to the eye; light_dir: the unit vector to
// the light; wrap: vertex c50.w; spot: 1 for the sun. A wrap of -1 gives no front light.
struct LeafSides {
  float front, back;
};
inline LeafSides LeafLight(const Float3& normal, const Float3& view, const Float3& light_dir, float wrap, float spot) {
  Float3 n{LightFinite(normal.x), LightFinite(normal.y), LightFinite(normal.z)};
  if (LightDot(view, n) < 0.0f) n = {-n.x, -n.y, -n.z};
  const float nl = LightDot(light_dir, n);
  const float w = LightFinite(wrap);
  const float front = (1.0f + w) != 0.0f ? LightSat((nl + w) / (1.0f + w)) : 0.0f;
  const float turn = 1.0f - LightSat(LightDot(view, n));
  const float back = LightSat(-nl) * (1.0f - turn * turn);
  const float s = LightFinite(spot);
  return {LightFinite(front * s), LightFinite(back * s)};
}

// The world position of a leaf vertex from the draw's block (below): (M p) / c7.w + the translation,
// which is the game's c4..c6 . (p, 1) (the block's rows hold M = R * c7.w and the translation in
// their fourth components; leaf[1] is 1 / c7.w). The game's own position has the wind on it, which
// the pass does not draw (frame-map section 12).
struct ClayObjectConstants;
inline Float3 LeafWorldPosition(const ClayObjectConstants& c, const Float3& p);

// The unit vector from a vertex to the eye; zero in the eye itself.
inline Float3 LeafView(const Float3& eye, const Float3& world) {
  return LightNormalize({LightFinite(eye.x - world.x), LightFinite(eye.y - world.y), LightFinite(eye.z - world.z)});
}

// The leaf pixel shader's two factors of the screen mask (light_formulas.leaf_mask): m1 = mask *
// gain + bias on the front light, m2 = m1 * gain + bias on the back light (0x7CD5... instr 15, 17).
// With the mask at 1 (a frame without a mask) and the shader's literals both are 1.
struct LeafMasks {
  float m1, m2;
};
inline LeafMasks LeafMask(float mask, float gain, float bias) {
  const float m1 = LightFinite(LightSat(mask) * LightFinite(gain) + LightFinite(bias));
  return {m1, LightFinite(m1 * LightFinite(gain) + LightFinite(bias))};
}

// Tree leaves (light_formulas.tree_leaf): (A * (vs_ambient * c27w + m1 * sun_col * front) + green *
// A * m2 * sun_col * back) * c49, green = 1 where A.g >= A.r (a leaf texel; the back light does not
// pass through a twig's). vs_ambient, front and back are the vertex side's, interpolated.
inline Float3 LightLeaf(const Float3& A, const Float3& vs_ambient, float front, float back, const Float3& sun_col, float c27w,
                        float m1, float m2, const Float3& c49) {
  const float green = LightFinite(A.y) >= LightFinite(A.x) ? 1.0f : 0.0f;
  auto one = [&](float a, float amb, float sun, float tint) {
    a = LightFinite(a);
    sun = LightFinite(sun);
    const float lit = a * (LightFinite(amb) * LightFinite(c27w) + LightFinite(m1) * sun * LightFinite(front));
    return LightFinite((lit + green * a * LightFinite(m2) * sun * LightFinite(back)) * LightFinite(tint));
  };
  return {one(A.x, vs_ambient.x, sun_col.x, c49.x), one(A.y, vs_ambient.y, sun_col.y, c49.y), one(A.z, vs_ambient.z, sun_col.z, c49.z)};
}

// What a lit draw writes into the float target for the value `v` (the light function's value times
// the frame's exposure): v itself, not clamped at 1 (the output mapping clamps), but nothing that is not
// a number, nothing negative and nothing above kClayLitMax: a NaN (no comparison holds for it) and
// a negative value give 0, +infinity gives the bound. The bound keeps a sum of every blended draw
// of a frame over one pixel far below the target's largest value (64,512 in its 10-bit channel:
// 1,008 draws of 64), so no infinity arises in the target, from which a blend factor of 0 would
// make a NaN. A lit value is at most a few times its albedo (the captured block: tint up to 1.9,
// sun colour up to 1.0).
inline constexpr float kClayLitMax = 64.0f;
inline float ClayLit(float v) {
  if (!(v > 0.0f)) return 0.0f;
  return v < kClayLitMax ? v : kClayLitMax;
}

// The largest magnitude a light constant may have to reach a shader: the captured values are
// below 2, and a product of five such bounds is still a number in single precision.
inline constexpr float kLightValueMax = 1024.0f;
inline bool LightValueUsable(float v) { return std::fabs(v) <= kLightValueMax; }  // (false for a NaN and for infinity)
// A world position (the frame's eye, a leaf draw's translation) is no light value: the levels are
// larger than 1024 units. What reaches a shader of it is a number of at most this magnitude; the
// shader forms a difference of two of them and normalises it.
inline constexpr float kLightPositionMax = 1048576.0f;
inline bool LightPositionUsable(float v) { return std::fabs(v) <= kLightPositionMax; }
// A probe buffer's weights are bounded alike where they are decoded (normal_decode.h, DecodeWeights).
static_assert(kProbeWeightMax == kLightValueMax);

// The tables' sizes (capture::FogBlock: tf4 and tf5 decoded).
inline constexpr uint32_t kFogScatterTexels = 64, kFogTransmitTexels = 256;
static_assert(sizeof(capture::FogBlock::scatter) == kFogScatterTexels * 3 * sizeof(float) &&
              sizeof(capture::FogBlock::transmit) == kFogTransmitTexels * 3 * sizeof(float));

// The frame's fog for the shaders ("Native fog" below): what the two forms read of the frame's block besides the tables
// and the eye (which is the frame's light constants': ClayFrameConstants::eye). 48 bytes, laid out
// as three rows of a constant buffer.
struct ClayFogConstants {
  float axis[3];         // c66.xyz
  uint32_t on;           // 1: the frame is fogged; 0: no pixel of it is (the other fields are zero)
  float near_colour[3];  // c68.rgb
  float start;           // c67.x
  float slope;           // c67.z: the blend from near_colour to the in-scatter table over distance
  float scale;           // c67.w: the transmittance table's distance scale
  float amount;          // c28.x: how much of the fog is applied (the ground form)
  float scatter;         // c28.y: the in-scatter's scale (the ground form; a tree's is its vertex c8.x)
};
static_assert(sizeof(ClayFogConstants) == 48);

// The frame's light and fog for the shaders (cbuffer Frame, register b2). One per Render, at the head
// of the frame's constant buffer (kClayFrameSlots small slots). Two parts. The light's
// (kClayFrameLightBytes; shaders::kClayFrameFields is its layout) is what every clay shader declares.
// The fog's tail (shaders::kClayFrameFogFields) is declared by the three lit pixel shaders alone
// (CLAY_FOG): the fog's constants, the frame's inverse view-projection, from which a lit pixel
// rebuilds its world position, and the two tables as arrays, which the shader reads as FogTableRead
// does (the controller's ruling of 2026-10-09: the layout has no binding parameter left for two
// textures; arrays need no texture, no upload and no neutral object, and the read is table_1d
// exactly). The sky step (plan Task 5) reads the in-scatter table from the same place.
struct ClayFrameConstants {
  float sun_dir[3];     // c80.xyz of a one-light draw (bit-equal to c109.xyz: MakeLightBlock), as it is
  uint32_t light_on;    // 1: the frame is lit; 0: no draw of it is (the other fields are zero)
  float sun_col[3];     // c87.xyz
  float tint_pow;       // c21.w
  float tint[3];        // c21.xyz
  float exposure;       // E of the lit output (ClayExposure of "on"): the lit pixel paths multiply by it
  float add[3];         // c20.xyz (c20.w, the output scale, is not applied)
  float pad0;
  float floor[3];       // c22.xyz; the block carries x and y, z is 0 (0 in every row captured)
  float mask_du;        // 1 / the clay target's width: a lit pixel reads the screen mask at (pos.xy - 0.5) * (mask_du, mask_dv)
  float axis[3];        // c23.xyz (= vertex c19.xyz)
  float mask_dv;        // 1 / the clay target's height (SetClayFrameMask; both 0 in a frame without light)
  float eye[3];         // c19.xyz, the eye (= vertex c9.xyz of the leaf shaders); zero when not usable
  uint32_t eye_on;      // 1: `eye` is the frame's eye; 0: the frame's leaves are not lit (LightFrameLights)
  // The fog's tail: all zero unless SetClayFrameFog filled it for this frame.
  ClayFogConstants fog;
  float inv_view_proj[4][4];                // rows: world (x, y, z, w) = rows . (X, Y, depth, 1), as the mask step's
  float scatter[kFogScatterTexels][4];      // tf4 decoded, rgb (the fourth float 0)
  float transmit[kFogTransmitTexels][4];    // tf5 decoded, rgb
};
inline constexpr uint32_t kClayFrameLightBytes = offsetof(ClayFrameConstants, fog);
static_assert(kClayFrameLightBytes == 112 && sizeof(ClayFrameConstants) == 5344 && offsetof(ClayFrameConstants, inv_view_proj) == 160 &&
              offsetof(ClayFrameConstants, scatter) == 224 && offsetof(ClayFrameConstants, transmit) == 1248);

// The frame's constants from its light block. Lit only with the switch on and a valid block whose
// every value is a number of usable size (the block's own rules: light_block.h, MakeLightBlock; a
// frame without one is "light-block"); else everything is zero. The eye is not among those values:
// a frame whose eye is not usable is lit all the same, without it (eye_on 0: LightFrameLights).
inline ClayFrameConstants MakeClayFrameConstants(const capture::LightBlock& b, ClayLight light) {
  ClayFrameConstants c{};
  if (light != ClayLight::kOn || !b.valid) return c;
  bool ok = b.tint_pow > 0.0f && b.tint_pow <= 8.0f && LightValueUsable(b.floor[0]) && LightValueUsable(b.floor[1]);
  for (int i = 0; i < 3; ++i) {
    ok = ok && LightValueUsable(b.sun_dir[i]) && LightValueUsable(b.sun_col[i]) && LightValueUsable(b.tint[i]) &&
         LightValueUsable(b.add[i]) && LightValueUsable(b.axis[i]);
  }
  if (!ok) return c;
  for (int i = 0; i < 3; ++i) {
    c.sun_dir[i] = b.sun_dir[i];
    c.sun_col[i] = b.sun_col[i];
    c.tint[i] = b.tint[i];
    c.add[i] = b.add[i];
    c.axis[i] = b.axis[i];
  }
  c.floor[0] = b.floor[0];
  c.floor[1] = b.floor[1];
  c.tint_pow = b.tint_pow;
  c.light_on = 1;
  c.exposure = ClayExposure(ClayLight::kOn);  // (only here: a frame without light has none, and no draw reads it)
  // The eye: all three components or none (a position: LightPositionUsable).
  if (LightPositionUsable(b.eye[0]) && LightPositionUsable(b.eye[1]) && LightPositionUsable(b.eye[2])) {
    for (int i = 0; i < 3; ++i) c.eye[i] = b.eye[i];
    c.eye_on = 1;
  }
  return c;
}

// Where a lit pixel reads the screen mask (cbuffer Frame's f_mask_du, f_mask_dv): the factors of
// its coordinate, 1 / the clay target's size, normalised coordinates whatever the target's size is
// and whatever texture is bound as the mask (the mask target of that size, or the 1 x 1 white one).
// Only in a lit frame: a frame without light keeps its constants all zero (no draw reads them).
inline void SetClayFrameMask(ClayFrameConstants& c, uint32_t width, uint32_t height) {
  if (!c.light_on || width == 0 || height == 0) return;
  c.mask_du = 1.0f / float(width);
  c.mask_dv = 1.0f / float(height);
}

// The read itself, as the game's lit pixel shaders fetch the mask (tf8: 1120 x 720, k_8, filter
// linear for magnification and minification, clamped; e.g. 0x26052DEFBA688AF2 instr 4 and 5:
// r0.zw = |r4.xy| * c0.zw + c0.xy, tfetch2D tf8, with r4 the pixel position register, which holds
// the pixel's whole coordinates, and c0 = (0, 0, 1 / 1120, 1 / 720)): the coordinate is the pixel's
// CORNER over the target's size, so the linear filter returns the mean of the four texels around
// that corner, (px - 1, px) x (py - 1, py), clamped at the target's edge. The native lit shaders
// read the same: SampleLevel with the linear clamped sampler at (pos.xy - 0.5) * (mask_du, mask_dv),
// pos the pixel's centre. A shadow edge one texel wide in the mask is two pixels wide in the light.
// MaskReadCoordinate: one axis of that coordinate from the pixel centre's position.
inline float MaskReadCoordinate(float centre, float inv_size) { return (centre - 0.5f) * inv_size; }
// The texels of pixel (px, py) on a mask of `width` x `height` (columns x0, x1 and rows y0, y1,
// each with weight 1/2; equal at an edge and on the 1 x 1 white texture).
struct MaskReadTexels {
  uint32_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
};
inline MaskReadTexels MaskRead(uint32_t px, uint32_t py, uint32_t width, uint32_t height) {
  MaskReadTexels t;
  if (width == 0 || height == 0) return t;
  t.x1 = px < width ? px : width - 1;
  t.y1 = py < height ? py : height - 1;
  t.x0 = px == 0 ? 0 : (px - 1 < width ? px - 1 : width - 1);
  t.y0 = py == 0 ? 0 : (py - 1 < height ? py - 1 : height - 1);
  return t;
}
// The value a lit pixel reads from an 8-bit mask (rows of `width` bytes): the mean of its four
// texels over 255. Without a mask: 1 (lit).
inline float MaskReadValue(const uint8_t* mask, uint32_t width, uint32_t height, uint32_t px, uint32_t py) {
  if (!mask || width == 0 || height == 0) return 1.0f;
  const MaskReadTexels t = MaskRead(px, py, width, height);
  const uint32_t sum = uint32_t(mask[size_t(t.y0) * width + t.x0]) + mask[size_t(t.y0) * width + t.x1] +
                       mask[size_t(t.y1) * width + t.x0] + mask[size_t(t.y1) * width + t.x1];
  return float(sum) / (4.0f * 255.0f);
}

// Whether a frame's constants light a draw of `family`: the frame is lit, and for a leaf, whose
// vertex-side light takes the vector to the eye, the frame's eye is usable.
inline bool LightFrameLights(const ClayFrameConstants& frame, capture::LightFamily family) {
  return frame.light_on != 0 && (family != capture::LightFamily::kTreeLeaf || frame.eye_on != 0);
}

// Whether the pass lights a draw with these inputs (plan Tasks 7 and 8a): the terrain layers and
// the far terrain; the objects whose vertex shader has the colour stream (LightVertexKind::kStream)
// or probe colours (kProbe: the plain and the bone-skinned ones); the tree trunks (probe colours).
// Their two- and four-light forms (LightInputs::multi) are drawn with the frame's sun through the
// family's one-light function and without their lanterns: for every such pair of the table the
// microcode, with the sun in the slot the game puts it in behind dark lanterns, is that function
// (light_check.py --multi-sun; tests/test_light_check.py, MultiSun). Since plan Task 8b also the
// instanced plants (kInstance), with and without the ambient factor, and their two-light forms (the
// sun in slot 1 at every such draw captured). Since plan Task 8c the tree leaves (probe colours;
// their one family covers the one-, two- and four-light vertex shaders: "The tree leaves" above).
inline bool LightFamilyDrawn(const capture::LightInputs& l) {
  switch (l.family) {
    case capture::LightFamily::kTerrainLayer:
    case capture::LightFamily::kTerrainFar: return l.kind == capture::LightVertexKind::kTerrain;
    case capture::LightFamily::kObject:
      return l.kind == capture::LightVertexKind::kStream || l.kind == capture::LightVertexKind::kProbe;
    case capture::LightFamily::kTreeTrunk:
    case capture::LightFamily::kTreeLeaf: return l.kind == capture::LightVertexKind::kProbe;
    case capture::LightFamily::kPlant:
    case capture::LightFamily::kPlantLit: return l.kind == capture::LightVertexKind::kInstance;
    default: return false;
  }
}

// Whether a mesh record of this vertex kind has the buffers its light reads: the normals and the
// kind's OWN other buffers (a colour stream's colours; the probe weights and sets; an instanced
// plant's copies' colours AND its bend inputs). A probe record has no colour buffer, and the colours
// an instanced record has are its copies', no stream colours: no kind is lit with another's.
constexpr bool LightObjectReady(capture::LightVertexKind kind, bool normals, bool colours, bool probe, bool bend) {
  switch (kind) {
    case capture::LightVertexKind::kStream: return normals && colours;
    case capture::LightVertexKind::kProbe: return normals && probe;
    case capture::LightVertexKind::kInstance: return normals && colours && bend;
    default: return false;
  }
}

// A lit object's, trunk's or plant's values for the shaders (cbuffer Object, register b1, in the
// draw's slot of the frame's constant buffer; shaders::kClayObjectFields is its layout). 288 bytes
// (272 before the leaves' register, plan Task 8c):
// more than a 256-byte slot, so an object's, trunk's or leaf's slot is kClayObjectSlotBytes (below). An
// instanced plant's values all lie in the first kClayPlantConstantBytes: it has a small slot.
struct ClayObjectConstants {
  // Rows of M = world_rot * world_scale in xyz: the game's own (R n) * c7.w. The fourth components
  // are 0 for an object or trunk, and for a leaf the world position's translation (vertex c4..c6 .w). An instanced plant has no rows (its normals are in the world
  // already; xyz 0, not read) and carries its bend constants there: vertex c45.x, c45.y, c45.z (the
  // strength, and the scale and offset of the height blend) in rot[0][3], rot[1][3], rot[2][3].
  float rot[3][4];
  float c49[3];        // pixel c49.xyz: on the whole colour (kInstance: 0, not read)
  float c27w;          // pixel c27.w: on the ambient part (kInstance: 0, not read)
  float c26[3];        // pixel c26.xyz: with the output scale, after the function (kInstance: 0, not read)
  // kStream: the vertex shader's literal on the stream colour (2.2); kProbe: 0, not read (a leaf:
  // kLeafColourPower, the factor of the frame's exponent in its ambient's); kInstance:
  // kInstanceColourPower, the factor of the frame's exponent in the instance colour's.
  float colour_power;
  float probe[capture::kLightProbeRegisters][4];  // kProbe: vertex c28..c39 (three sets of four); else 0, not read
  // [0], [1]: a leaf only (zero otherwise, not read): vertex c50.w, the wrap of the front light; the
  // inverse of c7.w, which takes the scale out of M p for the world position.
  // [2], [3]: the draw's fog ("Native fog" below). [3] the form (kTreeFogForm*): the ground form for
  // an object; for a trunk or a leaf the trees' form with the scale [2] (vertex c8.x), or no fog
  // when the draw has no usable scale ([2] is then 0).
  float leaf[4];
};
inline constexpr float kTreeFogFormGround = 0.0f, kTreeFogFormTree = 1.0f, kTreeFogFormNone = 2.0f;
static_assert(sizeof(ClayObjectConstants) == 288 && offsetof(ClayObjectConstants, probe) == 80 &&
              offsetof(ClayObjectConstants, leaf) == 272);
inline Float3 LeafWorldPosition(const ClayObjectConstants& c, const Float3& p) {
  auto row = [&](int r) {
    return LightFinite(LightFinite(c.rot[r][0] * p.x + c.rot[r][1] * p.y + c.rot[r][2] * p.z) * LightFinite(c.leaf[1]) +
                       LightFinite(c.rot[r][3]));
  };
  return {row(0), row(1), row(2)};
}
// The bytes of the block an instanced plant's shader path reads: the rows' fourth components and
// colour_power, all before the probe registers.
inline constexpr uint32_t kClayPlantConstantBytes = offsetof(ClayObjectConstants, probe);
static_assert(offsetof(ClayObjectConstants, colour_power) + sizeof(float) == kClayPlantConstantBytes);

// False (the draw is then not lit: light-unknown), with `out` all zero, unless the inputs are an
// object's with a colour stream, an object's, trunk's or leaf's with probe colours or an instanced plant's
// and EVERY value the kind's shader path reads is a number of usable size. An object or trunk: the
// world rotation gives a direction (every RotationKind but kDegenerate: the normal is the game's
// normalize(M n), ruling of 2026-10-08), the rows, c27.w, c49, c26, and the stream colour's power in
// (0, 8] or each of the forty-eight components of the probe registers. An instanced plant (family
// plant or plant-lit on the instance kind): the three bend constants c45.xyz; nothing else of the
// inputs is read or carried (it has no world rotation, its shaders read no c27, c49 or c26, and
// c45.w is no bend constant). A leaf: a trunk's values, and its world translation (a position), its
// wrap (0 to kLightValueMax) and a scale whose inverse is usable. All or nothing: no block is made
// with one value left out.
inline bool MakeClayObjectConstants(const capture::LightInputs& l, ClayObjectConstants* out) {
  *out = ClayObjectConstants{};
  if (l.kind == capture::LightVertexKind::kInstance) {
    if (l.family != capture::LightFamily::kPlant && l.family != capture::LightFamily::kPlantLit) return false;
    ClayObjectConstants c{};
    for (int k = 0; k < 3; ++k) {
      if (!LightValueUsable(l.vs_const[0][k])) return false;
      c.rot[k][3] = l.vs_const[0][k];
    }
    c.colour_power = kInstanceColourPower;
    *out = c;
    return true;
  }
  const bool stream = l.kind == capture::LightVertexKind::kStream, probe = l.kind == capture::LightVertexKind::kProbe;
  const bool leaf = l.family == capture::LightFamily::kTreeLeaf;
  if (!(l.family == capture::LightFamily::kObject && (stream || probe)) &&
      !((l.family == capture::LightFamily::kTreeTrunk || leaf) && probe)) {
    return false;
  }
  if (ClassifyRotation(l.world_rot, l.world_scale) == RotationKind::kDegenerate) return false;
  if (stream && !(l.colour_power > 0.0f && l.colour_power <= 8.0f)) return false;
  if (!LightValueUsable(l.c27w)) return false;
  ClayObjectConstants c{};
  if (probe) {
    for (uint32_t r = 0; r < capture::kLightProbeRegisters; ++r) {
      for (int k = 0; k < 4; ++k) {
        if (!LightValueUsable(l.vs_const[r][k])) return false;
        c.probe[r][k] = l.vs_const[r][k];
      }
    }
  }
  for (int r = 0; r < 3; ++r) {
    for (int k = 0; k < 3; ++k) {
      c.rot[r][k] = l.world_rot[r][k] * l.world_scale;
      if (!LightValueUsable(c.rot[r][k])) return false;
    }
    if (!LightValueUsable(l.c49[r]) || !LightValueUsable(l.c26[r])) return false;
    c.c49[r] = l.c49[r];
    c.c26[r] = l.c26[r];
  }
  c.c27w = l.c27w;
  c.colour_power = stream ? l.colour_power : 0.0f;
  // The form of the draw's fog: a tree's own (ruling B) with its scale, or none without a usable one.
  if (l.family != capture::LightFamily::kObject) {
    const bool usable = l.tree_fog >= 0.0f && l.tree_fog <= capture::kTreeFogScaleMax;  // (TreeFogScaleUsable, below)
    c.leaf[2] = usable ? l.tree_fog : 0.0f;
    c.leaf[3] = usable ? kTreeFogFormTree : kTreeFogFormNone;
  }
  if (leaf) {
    // The translation, the wrap (the front light divides by 1 + wrap: not negative) and the
    // inverse of the scale (ClassifyRotation above refused a scale of zero).
    const float inv_scale = 1.0f / l.world_scale;
    if (!(l.wrap >= 0.0f) || !LightValueUsable(l.wrap) || !LightValueUsable(inv_scale)) return false;
    for (int r = 0; r < 3; ++r) {
      if (!LightPositionUsable(l.world_pos[r])) return false;
      c.rot[r][3] = l.world_pos[r];
    }
    c.leaf[0] = l.wrap;
    c.leaf[1] = inv_scale;
    c.colour_power = kLeafColourPower;
  }
  *out = c;
  return true;
}

// Elements a record's normal and colour buffers must hold: one for every vertex its indices name,
// the draw's largest vertex (max_index + base_vertex) plus one. That is the capture's rule for the
// streams (material.h, MakeLightInputs: the normal stream, the colour stream from its offset and the
// probe weights hold the draw's largest vertex), not the whole position stream: meshes share
// streams, and a colour stream ends where its last mesh's vertices end. 0 when the indices name no
// vertex inside the positions (the high end of IndexRangeValid; such a record is not drawn).
inline uint32_t LightVertexNeed(uint32_t max_index, int32_t base_vertex, uint32_t vertex_count) {
  const int64_t hi = int64_t(max_index) + base_vertex;
  if (hi < 0 || hi >= int64_t(vertex_count)) return 0;
  return uint32_t(hi) + 1u;
}

// --- Native fog (frame-map section 16, "Native fog"; docs/native-renderer/lighting-formulas.md, "Fog and sky") ---
//
// The game fogs in its scene pixel shaders, after the lit colour and before the output scale: one
// block of arithmetic on six registers that hold one value a frame and two 1D tables it rewrites
// every frame (capture/fog_block.h has the frame's block). The functions here are the CPU mirrors
// of what the native lit shaders do with it, pinned to the reference
// (tools/xdk_sigmatch/light_formulas.py: fog, vertex_fog, fog_apply, fog_coords, table_1d) by the
// numbers of tools/xdk_sigmatch/fog_pins.py; they compute in double on the single-precision values
// a shader gets.
//
// Two forms. The ground form (FogMirror: terrain layers, far terrain, objects, plants; light_formulas.fog):
//   V = eye - P;  d = |V|;  dD = d - start;  unfogged unless dD > 0
//   a = sat((-V / d) . axis);  S = scatter(a)          the in-scatter colour by the angle to the axis
//   s = scale * dD;            T = transmit(s)         the transmittance by distance, per channel
//   t = sat(slope * dD);       F = near + t * (S - near)
//   G = C * T + (1 - T) * k * F;  C' = C + amount * (G - C)
// The trees' form (TreeFogMirror: trunks and leaves; the controller's ruling B): the game computes
// the trees' fog in their vertex shaders from the same two tables and the same axis, start and scale
// (frame-map section 16, "The trees' tables": 28 of 28 frames), with no blend from the near colour
// and no amount, and its pixel shaders apply it to the whole colour. Here it is evaluated per pixel:
//   C' = C * T + (1 - T) * S * c8x                     c8x: the draw's vertex c8.x (LightInputs::tree_fog)
// Both come after the family's lit colour (an object's or a tree's c49 included) and before its
// c26 and the frame's exposure, which stands in for the game's output scale c20.w.
//
// Guards (plan, Review Focus 2; review of plan Task 3, M2). A pixel is left UNFOGGED, its colour as
// it went in, when the fog is off for the frame, when a value the form reads is not a number, when
// the pixel is at the eye (d = 0: no direction; it is inside any start), when its squared distance
// is not below kFogDistance2Max (a position that is no number among them: no comparison holds for a
// NaN), or when the result is not a number. A colour channel that is no number counts as 0, as in
// the light functions. A zero axis reads the in-scatter table at 0; a distance coordinate beyond
// the table's end reads its last texel. What the product hands a shader is narrower still: the
// constants come from MakeClayFogConstants (every value a number inside the block's bounds) and the
// tables from a valid block (finite texels), so that with a finite colour no product can overflow
// (fog_block.h, kFogRegisterLimit).

// The largest squared distance from the eye a pixel is fogged at: beyond it (10^15 units; the
// levels are a few thousand across) the pixel keeps its colour. Far below the largest single, so
// that the shader's comparison against it also refuses a squared distance that overflowed and one
// that is no number, without a test for either that a compiler could remove.
inline constexpr float kFogDistance2Max = 1e30f;

// A 1D table of `n` texels as the game's fetch reads it with its linear filter and clamped
// addressing (light_formulas.table_1d): texel i has its centre at u = (i + 0.5) / n; between two
// centres the result is their linear blend; below the first centre and above the last, and outside
// 0..1, the edge texel alone. A coordinate that is no number reads texel 0. No table: zeros.
// The native tables are textures of the same sizes read with a linear, clamped sampler at
// (u, 0.5): the same centres (the host's are at (i + 0.5) / n); the GPU's filter weight has 8
// fractional bits, so its blend can differ from this one by 1 / 512 of the difference of two
// neighbouring texels.
// (K floats a texel, the first three read: the block's tables have three, the frame's arrays four)
template <size_t K>
inline void FogTableRead(const float (*table)[K], uint32_t n, double u, double out[3]) {
  static_assert(K >= 3);
  out[0] = out[1] = out[2] = 0.0;
  if (!table || n == 0) return;
  double x = 0.0;
  if (u == u) {
    const double c = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
    x = c * double(n) - 0.5;
    if (x < 0.0) x = 0.0;
    if (x > double(n) - 1.0) x = double(n) - 1.0;
  }
  uint32_t i = uint32_t(x);  // (0 <= x <= n - 1: a number an integer holds)
  if (i > n - 1) i = n - 1;
  const uint32_t j = i + 1 < n ? i + 1 : n - 1;
  const double f = x - double(i);
  for (int k = 0; k < 3; ++k) {
    const double p = double(table[i][k]), q = double(table[j][k]);
    out[k] = f == 0.0 ? p : p + (q - p) * f;
  }
}

// The frame's fog constants from its block. On only when the fog is `wanted` (the fog's switch as
// the pass reads it now, in a frame that is lit) and the block is valid and every value the forms
// read is a number inside the block's own bounds (fog_block.h, MakeFogBlock: checked again here, so
// that no value reaches a shader on the strength of a flag alone); else all zero: the frame is
// drawn without fog. The sky's numbers in the block (c67.y, c64, c65) are not read.
inline ClayFogConstants MakeClayFogConstants(const capture::FogBlock& b, bool wanted) {
  ClayFogConstants c{};
  if (!wanted || !b.valid) return c;
  auto bounded = [](float v) { return std::fabs(v) <= capture::kFogRegisterLimit; };  // (false for a NaN and for infinity)
  bool ok = b.range[0] >= 0.0f && bounded(b.range[0]) && b.range[2] >= 0.0f && bounded(b.range[2]) && b.range[3] > 0.0f &&
            bounded(b.range[3]) && b.amount[0] >= 0.0f && b.amount[0] <= 4.0f && b.amount[1] >= 0.0f && b.amount[1] <= 4.0f;
  for (int i = 0; i < 3; ++i) ok = ok && bounded(b.axis[i]) && bounded(b.near_colour[i]);
  if (!ok) return c;
  for (int i = 0; i < 3; ++i) {
    c.axis[i] = b.axis[i];
    c.near_colour[i] = b.near_colour[i];
  }
  c.on = 1;
  c.start = b.range[0];
  c.slope = b.range[2];
  c.scale = b.range[3];
  c.amount = b.amount[0];
  c.scatter = b.amount[1];
  return c;
}

// Whether a tree draw's in-scatter scale may reach its shader: a number in [0, kTreeFogScaleMax]
// (capture::kTreeFogNone, the marker of a draw without one, is not). A trunk or a leaf without a
// usable scale is drawn without fog and counted.
inline bool TreeFogScaleUsable(float c8x) { return c8x >= 0.0f && c8x <= capture::kTreeFogScaleMax; }

// The fog's coordinates of a pixel (light_formulas.fog_coords): the in-scatter table's (a), the
// transmittance table's (s, not clamped: the table's read clamps it) and the near blend (t).
// `fogged` false: the pixel is left as it is (the guards above), and the coordinates are zero.
struct FogPoint {
  bool fogged = false;
  double a = 0.0, s = 0.0, t = 0.0;
};
inline FogPoint FogCoords(const float P[3], const float eye[3], const ClayFogConstants& f) {
  FogPoint out;
  if (!f.on) return out;
  if (!std::isfinite(f.axis[0]) || !std::isfinite(f.axis[1]) || !std::isfinite(f.axis[2]) || !std::isfinite(f.start) ||
      !std::isfinite(f.slope) || !std::isfinite(f.scale)) {
    return out;
  }
  double V[3], d2 = 0.0;
  for (int k = 0; k < 3; ++k) {
    V[k] = double(eye[k]) - double(P[k]);
    d2 += V[k] * V[k];
  }
  if (!(d2 > 0.0 && d2 < double(kFogDistance2Max))) return out;  // (false for a NaN: a position that is no number)
  const double d = std::sqrt(d2), dD = d - double(f.start);
  if (!(dD > 0.0)) return out;
  const double inv = 1.0 / d;
  double a = 0.0;
  for (int k = 0; k < 3; ++k) a += (-V[k] * inv) * double(f.axis[k]);
  a = a != a ? 0.0 : (a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a));
  const double s = double(f.scale) * dD;
  double t = double(f.slope) * dD;
  t = t != t ? 0.0 : (t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t));
  if (!std::isfinite(a) || !std::isfinite(s) || !std::isfinite(t)) return out;
  out.fogged = true;
  out.a = a;
  out.s = s;
  out.t = t;
  return out;
}

// The ground form: the colour `C` (linear, the family's lit colour before c26 and the exposure) of
// a pixel at world position `P`, fogged. `scatter`, `transmit`: the frame's two tables
// (capture::FogBlock::scatter, ::transmit).
template <size_t K>
inline void FogMirror(const float C[3], const float P[3], const float eye[3], const ClayFogConstants& f,
                      const float (*scatter)[K], const float (*transmit)[K], double out[3]) {
  double base[3];
  for (int k = 0; k < 3; ++k) out[k] = base[k] = std::isfinite(C[k]) ? double(C[k]) : 0.0;
  if (!scatter || !transmit) return;
  const FogPoint at = FogCoords(P, eye, f);
  if (!at.fogged) return;
  if (!std::isfinite(f.amount) || !std::isfinite(f.scatter) || !std::isfinite(f.near_colour[0]) ||
      !std::isfinite(f.near_colour[1]) || !std::isfinite(f.near_colour[2])) {
    return;
  }
  double S[3], T[3], fogged[3];
  FogTableRead(scatter, kFogScatterTexels, at.a, S);
  FogTableRead(transmit, kFogTransmitTexels, at.s, T);
  const double scale = double(f.scatter);
  for (int k = 0; k < 3; ++k) {
    if (!std::isfinite(S[k]) || !std::isfinite(T[k])) return;
    const double near_colour = double(f.near_colour[k]);
    const double F = near_colour + at.t * (S[k] - near_colour);
    const double G = base[k] * T[k] + (1.0 - T[k]) * scale * F;
    fogged[k] = base[k] + double(f.amount) * (G - base[k]);
    if (!std::isfinite(fogged[k])) return;
  }
  for (int k = 0; k < 3; ++k) out[k] = fogged[k];
}

// The trees' form (ruling B): `C` is the trunk's or the leaf's lit colour with its c49, `c8x` the
// draw's in-scatter scale. Reads neither the near colour, the slope nor the two amounts.
template <size_t K>
inline void TreeFogMirror(const float C[3], const float P[3], const float eye[3], const ClayFogConstants& f, float c8x,
                          const float (*scatter)[K], const float (*transmit)[K], double out[3]) {
  double base[3];
  for (int k = 0; k < 3; ++k) out[k] = base[k] = std::isfinite(C[k]) ? double(C[k]) : 0.0;
  if (!scatter || !transmit || !std::isfinite(c8x)) return;
  const FogPoint at = FogCoords(P, eye, f);
  if (!at.fogged) return;
  double S[3], T[3], fogged[3];
  FogTableRead(scatter, kFogScatterTexels, at.a, S);
  FogTableRead(transmit, kFogTransmitTexels, at.s, T);
  for (int k = 0; k < 3; ++k) {
    if (!std::isfinite(S[k]) || !std::isfinite(T[k])) return;
    const double add = (1.0 - T[k]) * S[k] * double(c8x);
    fogged[k] = base[k] * T[k] + add;
    if (!std::isfinite(add) || !std::isfinite(fogged[k])) return;
  }
  for (int k = 0; k < 3; ++k) out[k] = fogged[k];
}

// The fog's tail of the frame's constants (ClayFrameConstants::fog and what follows) from the frame's
// block and its inverse view-projection (FrameInverseViewProj; null: the frame has none). First
// every byte of the tail is made zero; it is then written only when the frame is lit, has its eye
// and its target's size (SetClayFrameMask), the fog is wanted, the block gives constants that are on
// (MakeClayFogConstants), and every entry of the matrix and every texel of both tables is a number.
// So a frame without fog holds nothing of an earlier frame's tables or registers, whatever the
// constants held before the call (plan, Review Focus 1), and the neutral state is one flag
// (fog.on = 0: no shader reads the rest). True when the tail was written.
inline bool SetClayFrameFog(ClayFrameConstants& c, const capture::FogBlock& b, bool wanted, const float inverse[4][4]) {
  c.fog = ClayFogConstants{};
  std::memset(c.inv_view_proj, 0, sizeof(c.inv_view_proj));
  std::memset(c.scatter, 0, sizeof(c.scatter));
  std::memset(c.transmit, 0, sizeof(c.transmit));
  if (!c.light_on || !c.eye_on || !inverse) return false;
  if (!(c.mask_du > 0.0f && c.mask_dv > 0.0f) || !std::isfinite(c.mask_du) || !std::isfinite(c.mask_dv)) return false;
  const ClayFogConstants fog = MakeClayFogConstants(b, wanted);
  if (!fog.on) return false;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      if (!std::isfinite(inverse[i][j])) return false;
    }
  }
  for (const auto& texel : b.scatter) {
    if (!std::isfinite(texel[0]) || !std::isfinite(texel[1]) || !std::isfinite(texel[2])) return false;
  }
  for (const auto& texel : b.transmit) {
    if (!std::isfinite(texel[0]) || !std::isfinite(texel[1]) || !std::isfinite(texel[2])) return false;
  }
  c.fog = fog;
  std::memcpy(c.inv_view_proj, inverse, sizeof(c.inv_view_proj));
  for (uint32_t i = 0; i < kFogScatterTexels; ++i) std::memcpy(c.scatter[i], b.scatter[i], sizeof(b.scatter[i]));
  for (uint32_t i = 0; i < kFogTransmitTexels; ++i) std::memcpy(c.transmit[i], b.transmit[i], sizeof(b.transmit[i]));
  return true;
}

// A lit pixel's world position (the controller's ruling of 2026-10-09: one source for every
// family, because the vertex shader hands on a mesh's position as stored, in model space): the
// pixel's own SV_Position through the frame's inverse view-projection, the mask step's arithmetic.
//   S = (2 x / W - 1, 1 - 2 y / H, z, 1);  P = (rows . S).xyz / (rows . S).w
// (x, y): SV_Position.xy, the pixel's centre; z: SV_Position.z, which in the clay pass is the clip
// z / w of the draw's own vertex shader for every family (one vertex shader, `o.pos = c`; viewport
// depth 0..1; no depth bias; the same value the pre-pass writes and the mask reads), blended draws
// included: each pixel uses its own depth. In single precision, as the shader computes it. False
// (the pixel is left unfogged) when w is zero or no number.
// The error of the rebuilt position is the depth's: test case 587 measures it on a captured frame's
// matrices over the distance the tables span.
inline bool FogPixelWorld(const ClayFrameConstants& c, float pos_x, float pos_y, float depth, float P[3]) {
  const float S[4] = {pos_x * (2.0f * c.mask_du) - 1.0f, 1.0f - pos_y * (2.0f * c.mask_dv), depth, 1.0f};
  auto row = [&](int r) {
    return c.inv_view_proj[r][0] * S[0] + c.inv_view_proj[r][1] * S[1] + c.inv_view_proj[r][2] * S[2] + c.inv_view_proj[r][3] * S[3];
  };
  const float w = row(3);
  if (!(std::fabs(w) > 0.0f && std::fabs(w) < 3.0e38f)) return false;
  for (int k = 0; k < 3; ++k) P[k] = row(k) / w;
  return true;
}

// The fog of a lit pixel as the lit pixel shaders compute it: the position from the pixel's own
// depth, then the form the draw's object block names (`form`, `scale`: ClayObjectConstants::leaf[3]
// and [2]; a terrain draw and an instanced plant have the ground form): the trees' form with the
// scale for kTreeFogFormTree, no fog for kTreeFogFormNone, the ground form for anything else.
inline void FogPixelMirror(const float C[3], float pos_x, float pos_y, float depth, const ClayFrameConstants& c, float form,
                           float scale, double out[3]) {
  for (int k = 0; k < 3; ++k) out[k] = std::isfinite(C[k]) ? double(C[k]) : 0.0;
  float P[3];
  if (form == kTreeFogFormNone || !FogPixelWorld(c, pos_x, pos_y, depth, P)) return;  // (a frame without fog: the forms' own test)
  if (form == kTreeFogFormTree) {
    TreeFogMirror(C, P, c.eye, c.fog, scale, c.scatter, c.transmit, out);
  } else {
    FogMirror(C, P, c.eye, c.fog, c.scatter, c.transmit, out);
  }
}

// The Fog line of the status text and the log: the switch, the frame's block, what the pass did,
// the frame's tree draws, and the pass's totals since it was made.
inline std::string FormatFogText(const ClayStats& st) {
  if (!st.fog_switch) return "Fog: off";
  std::string text = "Fog: on, block ";
  if (st.fog_block) {
    text += "valid";
  } else {
    text += std::string("invalid (") + capture::kFogBlockReason + ": " + capture::FogBlockReasonName(st.fog_reason) + ")";
  }
  switch (st.fog) {
    case ClayFog::kDrawn: text += ", drawn"; break;
    case ClayFog::kNoLight: text += ", not drawn (no light)"; break;
    case ClayFog::kBlock: text += std::string(", not drawn (") + capture::kFogBlockReason + ": block)"; break;
    case ClayFog::kValues: text += std::string(", not drawn (") + capture::kFogBlockReason + ": values)"; break;
    case ClayFog::kView:
      text += std::string(", not drawn (") + capture::kFogBlockReason + ": view: " + FrameViewReasonName(st.fog_view) + ")";
      break;
    default: text += ", not drawn (off)"; break;
  }
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                " | trees %u with a scale, %u without | frames since start %llu: %s %llu {block: %llu, values: %llu, view: %llu}, "
                "no-light %llu",
                st.fog_trees, st.fog_trees_none, static_cast<unsigned long long>(st.fog_frames), capture::kFogBlockReason,
                static_cast<unsigned long long>(st.fog_block_frames + st.fog_values_frames + st.fog_view_frames),
                static_cast<unsigned long long>(st.fog_block_frames), static_cast<unsigned long long>(st.fog_values_frames),
                static_cast<unsigned long long>(st.fog_view_frames), static_cast<unsigned long long>(st.fog_nolight_frames));
  return text + buf;
}

// --- Terrain material (frame-map section 13, "Terrain", "Native terrain material") ---
// A terrain record whose inputs and textures are usable is drawn by one of the
// two terrain pixel shaders (fable2_native_shaders.h) with its captured render
// state: a layer of the layer builder (8221C9C8) with kClayTerrainLayerPs, a
// draw of the adaptive builder (82207C30) with kClayTerrainFarPs.

// The terrain shaders' constants (cbuffer Terrain, register b1), laid out as
// shaders::kClayTerrainFields says. They do not fit the root signature (64
// words: 36 are the draw's constants, 6 the three buffers, 2 the two texture
// tables, and the RHI has one constant block per layout), so every terrain
// draw of a frame has a slot of kClayTerrainSlotBytes in one constant buffer.
struct ClayTerrainConstants {
  float detail_uv[4];   // layer: tf0 coordinate of the world position, scaled into the host texture
  float weight_uv[4];   // layer: tf18 coordinate of the world position
  float world[12];      // TerrainMaterialInputs::world
  float eye[3];
  float hole_ref;
  float fade[2];
  float weight_scale, weight_bias;
  uint32_t colour_sampler;  // static sampler of the colour texture (SamplerIndex)
  uint32_t weight_sampler;  // of the weights; the shader reads its clamp bit and filters linearly
  uint32_t flags;           // kTerrainFadeIn, kTerrainSteep
  // The steep-ground block (kTerrainSteep; zero without it).
  uint32_t side_sampler;    // static sampler of the side texture
  float side_uv[2];         // TerrainMaterialInputs::side_uv, scaled into the host texture
  float slope[2];           // TerrainMaterialInputs::slope
  float steep_threshold, steep_eps;
  uint32_t pad[2];
  // The light (SetClayTerrainLight; zero for a record drawn without it).
  float base_uv[4];         // tf3 coordinate of the world position, scaled into the host texture
  uint32_t base_sampler;    // static sampler of the base map
  float c11x;               // pixel c11.x: layers on the ambient part, far terrain on the sun
  float facing_scale;       // layers: the literal of sat(scale * (N . c109)); far terrain: 0
  uint32_t light_pad;
};
static_assert(sizeof(ClayTerrainConstants) == 192);
inline constexpr uint32_t kClayTerrainSlotBytes = 256;  // the RHI's constant buffer offset alignment

// The frame's constant buffer (clay_pass.cpp, FinishTerrain): `small_slots` slots of
// kClayTerrainSlotBytes (the first kClayFrameSlots the frame's block, cbuffer Frame, bound at b2 at
// offset 0: its light's part fits the first slot, its fog's tail takes the rest; then one per terrain record drawn
// with its material, cbuffer Terrain; then one per lit instanced plant, the first
// kClayPlantConstantBytes of cbuffer Object), then one slot of kClayObjectSlotBytes per lit object
// or trunk (cbuffer Object: 288 bytes with the probe registers and the leaf's, more than a small slot). Everything is bound by
// address and offset: no view has a size, so the buffer itself must hold, from the start of EVERY
// slot the pass binds at b1, the largest block a shader declares there. That is cbuffer Object, which
// the vertex shader declares for every draw, a terrain draw's and an unlit draw's included (they do
// not read it; a validation layer holds the declaration against the buffer all the same).
inline constexpr uint32_t kClayObjectSlotBytes = 512;
// The small slots the frame's block takes at the buffer's head (21: 5,344 bytes). Slot 0 is also what
// b1 is bound to before a draw binds its own: the buffer holds kClayObjectSlotBytes from there.
inline constexpr uint32_t kClayFrameSlots = (sizeof(ClayFrameConstants) + kClayTerrainSlotBytes - 1) / kClayTerrainSlotBytes;
static_assert(sizeof(ClayObjectConstants) <= kClayObjectSlotBytes && sizeof(ClayTerrainConstants) <= kClayTerrainSlotBytes &&
              kClayFrameLightBytes <= kClayTerrainSlotBytes && sizeof(ClayFrameConstants) <= kClayFrameSlots * kClayTerrainSlotBytes &&
              kClayFrameSlots == 21 && kClayObjectSlotBytes % kClayTerrainSlotBytes == 0);
// The buffer's size is a multiple of this (256 small slots), so that the pool's same-size reuse
// holds while the number of terrain draws and lit objects moves.
inline constexpr uint64_t kClayConstantBufferStep = 256ull * kClayTerrainSlotBytes;
// Byte offset of small slot `slot` (the frame's block starts at 0; a terrain record's; a lit plant's).
constexpr uint64_t ClaySmallSlotOffset(uint64_t slot) { return slot * kClayTerrainSlotBytes; }
static_assert(kClayPlantConstantBytes <= kClayTerrainSlotBytes);
// Byte offset of lit object `object`'s slot behind `small_slots` small ones.
constexpr uint64_t ClayObjectSlotOffset(uint64_t small_slots, uint64_t object) {
  return small_slots * kClayTerrainSlotBytes + object * kClayObjectSlotBytes;
}
// Bytes of the frame's buffer: every slot, in whole steps. Without objects the last small slot is
// the buffer's last, and it is padded so that a view of kClayObjectSlotBytes from its start lies
// inside the buffer (with objects, their slots lie behind it and the last of them is that large).
constexpr uint64_t ClayConstantBufferBytes(uint64_t small_slots, uint64_t objects) {
  const uint64_t pad = objects == 0 ? kClayObjectSlotBytes - kClayTerrainSlotBytes : 0;
  const uint64_t bytes = ClayObjectSlotOffset(small_slots, objects) + pad;
  return (bytes + kClayConstantBufferStep - 1) / kClayConstantBufferStep * kClayConstantBufferStep;
}
// ClayTerrainConstants::flags: the far colour is multiplied by sat(d2 * fade[0] + fade[1]) (the
// kind terrain-far-fade, shader 2F8A: the underlay the layers add to).
inline constexpr uint32_t kTerrainFadeIn = 1u;
// The layer's pixel shader has the steep-ground block and the draw has what it reads: the side
// texture at t6 and the vertex normals in the draw's buffer at t2 (TerrainSteepStatus).
inline constexpr uint32_t kTerrainSteep = 2u;

// The pipeline of a flat terrain draw whose state could not be read: the default
// state with depth less-equal. The pass keeps it as a default pipeline, so that
// a flat terrain draw that finds no pipeline of its own in a full cache still
// passes the depth test over its underlay.
inline PipelineKey TerrainFlatDefaultKey() {
  PipelineKey k;
  k.depth_func = uint8_t(capture::Compare::kLessEqual);
  return k;
}

// Whether a terrain record's captured state can be drawn as captured: it was
// read, its depth function and its blend are ones the RHI has. If not, the
// record is not drawn with its material (reason terrain-inputs): the pass's
// default state is opaque with depth less, under which a layer over its
// underlay fails the depth test at equal depth and a first layer writes its
// colour unweighted, and a layer left out of its square's sum darkens it. The
// square then falls back as a whole (PlanTerrain).
inline bool TerrainStateUsable(const capture::RenderState& rs) {
  return rs.valid && DepthSupported(rs) && !(rs.blend && rs.color_mask != 0 && !BlendSupported(rs));
}

// PipelineKey::textured of a terrain kind.
inline uint8_t TerrainTextured(capture::MaterialKind kind) {
  return kind == capture::MaterialKind::kTerrainLayer ? kTexturedTerrainLayer : kTexturedTerrainFar;
}

// `colour_fix`: the colour texture's (width / host width, height / host height). A far draw's
// colour coordinate is per vertex (TerrainFarUvKey) and is scaled by the draw's uv transform.
// `side_fix` and `side_sampler`: the same of the side texture of a layer with the steep-ground
// block, when the pass has it (nullptr: the block stays off).
inline ClayTerrainConstants MakeClayTerrainConstants(const capture::TerrainMaterialInputs& m,
                                                     capture::MaterialKind kind, const float colour_fix[2],
                                                     uint32_t colour_sampler, uint32_t weight_sampler,
                                                     const float* side_fix = nullptr, uint32_t side_sampler = 0) {
  ClayTerrainConstants c{};
  for (int i = 0; i < 4; ++i) {
    c.detail_uv[i] = m.detail_uv[i] * colour_fix[i & 1];
    c.weight_uv[i] = m.weight_uv[i];
  }
  std::copy(std::begin(m.world), std::end(m.world), std::begin(c.world));
  std::copy(std::begin(m.eye), std::end(m.eye), std::begin(c.eye));
  c.hole_ref = m.hole_ref;
  c.fade[0] = m.fade[0];
  c.fade[1] = m.fade[1];
  c.weight_scale = m.weight_scale;
  c.weight_bias = m.weight_bias;
  c.colour_sampler = colour_sampler;
  c.weight_sampler = weight_sampler;
  c.flags = kind == capture::MaterialKind::kTerrainFarFade ? kTerrainFadeIn : 0u;
  if (m.steep && side_fix && kind == capture::MaterialKind::kTerrainLayer) {
    c.flags |= kTerrainSteep;
    c.side_sampler = side_sampler;
    c.side_uv[0] = m.side_uv[0] * side_fix[0];
    c.side_uv[1] = m.side_uv[1] * side_fix[1];
    c.slope[0] = m.slope[0];
    c.slope[1] = m.slope[1];
    c.steep_threshold = m.steep_threshold;
    c.steep_eps = m.steep_eps;
  }
  return c;
}

// The light's part of a terrain record's constants. `base_fix`, `base_sampler`: the base map's
// (width / host width, height / host height) and static sampler, as the texture cache resolved
// it. False, with `c` untouched (the record is then not lit, and with it its ground square:
// PlanTerrain), unless the inputs are a terrain family's, the record has its base map inputs
// (TerrainMaterialInputs::base_valid), every value is a number of usable size, and a layer has its
// facing literal (above 0).
inline bool SetClayTerrainLight(ClayTerrainConstants& c, const capture::TerrainMaterialInputs& m,
                                const capture::LightInputs& l, const float base_fix[2], uint32_t base_sampler) {
  const bool layer = l.family == capture::LightFamily::kTerrainLayer;
  if ((!layer && l.family != capture::LightFamily::kTerrainFar) || l.kind != capture::LightVertexKind::kTerrain) {
    return false;
  }
  if (!m.valid || !m.base_valid || !LightValueUsable(l.c11x)) return false;
  if (layer && !(l.facing_scale > 0.0f && LightValueUsable(l.facing_scale))) return false;
  float uv[4];
  for (int i = 0; i < 4; ++i) {
    uv[i] = m.base_uv[i] * base_fix[i & 1];
    if (!std::isfinite(uv[i])) return false;
  }
  std::copy(std::begin(uv), std::end(uv), std::begin(c.base_uv));
  c.base_sampler = base_sampler;
  c.c11x = l.c11x;
  c.facing_scale = layer ? l.facing_scale : 0.0f;
  return true;
}

// kTextured when a terrain record can be drawn with its material this frame,
// else the reason it is flat clay. `kind` is the record's pixel shader's
// (Material::kind), `adaptive_builder` whether the adaptive builder drew it;
// `colour` and `weight` are the texture cache's statuses of the colour texture
// and (layers) the weight cells, `far_uvs` whether the far colour coordinates
// were built (far draws). Inputs that are not valid, or a pixel shader that is
// not a terrain shader of the record's builder: terrain-inputs, and no texture
// was asked for. A texture that is not usable: the cache's reason, the colour
// texture's first. Far coordinates that could not be built (a table unreadable
// or of a layout that is not handled): uv-unsupported.
inline capture::MaterialStatus TerrainMaterialStatus(const capture::TerrainMaterialInputs& m,
                                                     capture::MaterialKind kind, bool adaptive_builder,
                                                     capture::MaterialStatus colour, capture::MaterialStatus weight,
                                                     bool far_uvs) {
  using capture::MaterialKind;
  using capture::MaterialStatus;
  const bool far_kind = kind == MaterialKind::kTerrainFar || kind == MaterialKind::kTerrainFarFade;
  const bool layer_kind = kind == MaterialKind::kTerrainLayer;
  if (!m.valid || m.adaptive != adaptive_builder || (adaptive_builder ? !far_kind : !layer_kind)) {
    return MaterialStatus::kTerrainInputs;
  }
  if (colour != MaterialStatus::kTextured) return colour;
  if (adaptive_builder) return far_uvs ? MaterialStatus::kTextured : MaterialStatus::kUvUnsupported;
  return weight;
}

// The steep-ground block's part of a layer record's status, asked when
// TerrainMaterialStatus is kTextured: kTextured for a record without the block
// (`side` and `normals` are not looked at); for one with it, the texture
// cache's reason of the side texture when that is not usable, uv-unsupported
// when the vertex normals could not be built (the normal map unreadable or of a
// layout that is not handled), else kTextured. A record that is not kTextured
// here cannot have its material, so its square falls back as a whole
// (PlanTerrain): the block is not left out of a layer that has it.
inline capture::MaterialStatus TerrainSteepStatus(const capture::TerrainMaterialInputs& m,
                                                  capture::MaterialStatus side, bool normals) {
  using capture::MaterialStatus;
  if (!m.steep) return MaterialStatus::kTextured;
  if (side != MaterialStatus::kTextured) return side;
  return normals ? MaterialStatus::kTextured : MaterialStatus::kUvUnsupported;
}

// A patch's rectangle of ground: corner and size in world units.
struct TerrainRect {
  float x0 = 0, y0 = 0, w = 0, h = 0;
  bool operator==(const TerrainRect&) const = default;
};

// Patch `k` of a terrain record, as the vertex shaders place it: row = floor(p / cols),
// col = p - row * cols, corner = origin + (col, row) * cell (the first grid point of
// BuildTerrainPositions, computed the same way, so equal patches give equal rectangles).
inline TerrainRect TerrainPatchRect(const capture::TerrainPatch& t, uint32_t k) {
  const float p = t.patch + float(k);
  const float row = std::floor(p * t.inv_cols);
  const float col = p - row * t.cols;
  return {col * t.cell[0] + t.origin[0], row * t.cell[1] + t.origin[1], t.cell[0], t.cell[1]};
}

enum class TerrainAction : uint8_t {
  kMaterial,  // drawn by its terrain pixel shader with its captured state
  kFlat,      // drawn as flat clay, opaque
  kSkip       // not drawn
};

// One terrain record of a frame for PlanTerrain, in draw order.
struct TerrainPlanItem {
  const capture::TerrainPatch* patch = nullptr;  // the record's terrain inputs (TerrainOf)
  bool layer = false;     // a draw of the layer builder; else of the adaptive builder
  bool usable = false;    // its material can be drawn this frame (TerrainMaterialStatus is kTextured)
  bool drawable = true;   // its positions and indices were built
  TerrainAction action = TerrainAction::kFlat;  // out
  // In: the record's light can be drawn (its inputs, normals and base map are there, the frame is
  // lit). Out: the record is drawn lit: it is drawn with its material and, for a layer, every layer
  // drawn with its material in each of its squares can be lit too (below).
  bool light = false;
};

// What the pass does with each terrain record of a frame. The ground near the
// eye is the sum of the layer draws of a square (one patch rectangle of the
// layer builder: a tile's origin, its sub-patch and the cell size, compared as
// TerrainPatchRect) over an underlay (a draw of the adaptive builder that
// covers the square; found by the square's centre), each layer blended by its
// captured state. A sum with a part missing or replaced by flat clay is wrong
// (too dark, or additive layers over clay), so a square is all or nothing:
//  - every draw usable and drawable: each with its material;
//  - a layer of a square not usable (or without geometry): the square is "bad".
//    Its first drawable layer that is not yet under a flat draw is drawn as flat
//    clay (opaque: it covers the underlay), every other layer of the square is
//    left out;
//  - an underlay that is not usable is itself drawn flat (opaque, all its
//    patches), and every layer of the squares it covers is left out; one without
//    geometry draws nothing and its squares are bad as above;
//  - a layer draw of several patches ties its squares together: when one is bad
//    all are (a square can then lie under two opaque flat draws, never under a
//    material layer over flat clay).
// The light follows the same rule one step on (TerrainPlanItem::light): the
// lit layers of a square add up like its unlit ones, and a sum of lit and unlit
// layers is neither, so a square is lit as a whole or not at all. A layer drawn
// with its material that cannot be lit makes its squares unlit, a layer draw of
// several patches ties its squares together, and a layer in an unlit square is
// drawn without light. A record that is not drawn with its material has no
// light; a draw of the adaptive builder is lit or not by itself (it covers
// many squares, and where it fades into unlit layers the two differ in the
// ring of the cross-fade: counted, not hidden).
// A draw of the adaptive builder never depends on another draw. A record with
// more than capture::kMaxTerrainPatches patches has no grid (no positions are
// built for it) and is not walked: as a layer it has no squares; as an underlay
// it cannot say which squares it lies under and draws nothing, so every square
// of the frame is bad (the treatment of an underlay without geometry).
//
// The work is bounded (records come from guest memory: 16 layer records of 4096
// patches over an underlay without material took 218 ms). Linking one patch of
// a layer record to its square counts kTerrainPlanLinkCost steps (a table
// lookup), testing one square against one patch of an underlay without its
// material one step. A frame that would take more than kTerrainPlanBudget steps
// is not planned by squares: every square is bad without the walk, which here
// means every layer record that has geometry is flat clay (several opaque flat
// draws of one square show the same clay) and every far draw is what it is by
// itself. A real frame is far below the budget: the bridge (session-1 capture)
// has 492 layer records of one patch each (31,488 steps) and 75 far records of
// 1,606 patches under about 100 squares (about 160,000 steps more if none of
// them had its material).
// `scratch`: storage a caller keeps between frames (nullptr: local).
inline constexpr uint64_t kTerrainPlanBudget = 1u << 20;
inline constexpr uint64_t kTerrainPlanLinkCost = 64;

struct TerrainPlanScratch {
  struct Square {
    TerrainRect rect;
    bool bad = false, covered = false;
    bool dark = false;  // a layer drawn with its material in it cannot be lit
  };
  struct RectHash {
    size_t operator()(const TerrainRect& r) const {
      return HashCombine32({FloatBits(r.x0), FloatBits(r.y0), FloatBits(r.w), FloatBits(r.h)});
    }
  };
  std::vector<Square> squares;
  std::unordered_map<TerrainRect, uint32_t, RectHash> index;
  // link[first[i] .. first[i + 1]): the squares of layer item i, one per patch.
  std::vector<uint32_t> link, first;
};

inline void PlanTerrain(std::vector<TerrainPlanItem>& items, TerrainPlanScratch* scratch = nullptr) {
  using Square = TerrainPlanScratch::Square;
  TerrainPlanScratch local;
  TerrainPlanScratch& w = scratch ? *scratch : local;
  std::vector<Square>& squares = w.squares;
  auto& index = w.index;
  std::vector<uint32_t>& link = w.link;
  std::vector<uint32_t>& first = w.first;
  squares.clear();
  index.clear();
  link.clear();
  first.assign(items.size() + 1, 0);
  auto has_grid = [](const TerrainPlanItem& it) {
    return it.patch && it.patch->patches <= capture::kMaxTerrainPatches;
  };
  auto own_action = [](const TerrainPlanItem& it) {
    return !it.drawable ? TerrainAction::kSkip : it.usable ? TerrainAction::kMaterial : TerrainAction::kFlat;
  };
  // Over the budget: every square is bad without the walk.
  auto unplanned = [&] {
    for (TerrainPlanItem& it : items) {
      it.action = !it.layer ? own_action(it) : it.drawable ? TerrainAction::kFlat : TerrainAction::kSkip;
      it.light = it.light && it.action == TerrainAction::kMaterial;
    }
  };
  uint64_t steps = 0;
  for (const TerrainPlanItem& it : items) {
    if (it.layer && has_grid(it)) steps += kTerrainPlanLinkCost * it.patch->patches;
  }
  if (steps > kTerrainPlanBudget) return unplanned();
  bool every_bad = false;
  for (size_t i = 0; i < items.size(); ++i) {
    first[i] = uint32_t(link.size());
    const TerrainPlanItem& it = items[i];
    if (!it.layer || !has_grid(it)) continue;
    for (uint32_t k = 0; k < it.patch->patches; ++k) {
      const TerrainRect rect = TerrainPatchRect(*it.patch, k);
      const auto found = index.try_emplace(rect, uint32_t(squares.size()));
      if (found.second) squares.push_back({rect});
      link.push_back(found.first->second);
    }
  }
  first[items.size()] = uint32_t(link.size());

  for (size_t i = 0; i < items.size(); ++i) {
    TerrainPlanItem& it = items[i];
    const bool whole = it.usable && it.drawable;
    if (it.layer) {
      if (!whole) {
        for (uint32_t l = first[i]; l < first[i + 1]; ++l) squares[link[l]].bad = true;
      }
      continue;
    }
    it.action = own_action(it);
    if (it.patch && !has_grid(it)) {
      every_bad = true;  // over the patch bound: no grid to find its squares by
      continue;
    }
    if (whole || !it.patch) continue;
    // The squares over this draw have no underlay to add to: bad, and under its flat draw if it has one.
    steps += uint64_t(it.patch->patches) * squares.size();
    if (steps > kTerrainPlanBudget) return unplanned();
    for (uint32_t k = 0; k < it.patch->patches; ++k) {
      const TerrainRect f = TerrainPatchRect(*it.patch, k);
      for (Square& s : squares) {
        const float cx = s.rect.x0 + 0.5f * s.rect.w, cy = s.rect.y0 + 0.5f * s.rect.h;
        if (cx >= f.x0 && cx < f.x0 + f.w && cy >= f.y0 && cy < f.y0 + f.h) {
          s.bad = true;
          s.covered = s.covered || it.drawable;
        }
      }
    }
  }
  if (every_bad) {
    for (Square& s : squares) s.bad = true;
  }
  // A layer draw with a bad square makes all its squares bad.
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t i = 0; i < items.size(); ++i) {
      bool bad = false;
      for (uint32_t l = first[i]; l < first[i + 1]; ++l) bad = bad || squares[link[l]].bad;
      if (!bad) continue;
      for (uint32_t l = first[i]; l < first[i + 1]; ++l) {
        changed = changed || !squares[link[l]].bad;
        squares[link[l]].bad = true;
      }
    }
  }
  for (size_t i = 0; i < items.size(); ++i) {
    TerrainPlanItem& it = items[i];
    if (!it.layer) continue;
    bool bad = false, open = false;
    for (uint32_t l = first[i]; l < first[i + 1]; ++l) {
      bad = bad || squares[link[l]].bad;
      open = open || !squares[link[l]].covered;
    }
    if (!bad) {
      // (a layer without a patch has no square: it is drawn on its own)
      it.action = own_action(it);
    } else if (it.drawable && open) {
      it.action = TerrainAction::kFlat;
      for (uint32_t l = first[i]; l < first[i + 1]; ++l) squares[link[l]].covered = true;
    } else {
      it.action = TerrainAction::kSkip;
    }
  }
  // The light: only of a record drawn with its material, and a square as a whole.
  for (size_t i = 0; i < items.size(); ++i) {
    TerrainPlanItem& it = items[i];
    const bool material = it.action == TerrainAction::kMaterial;
    it.light = it.light && material;
    if (material && !it.light) {  // (only a layer has squares linked)
      for (uint32_t l = first[i]; l < first[i + 1]; ++l) squares[link[l]].dark = true;
    }

  }
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t i = 0; i < items.size(); ++i) {
      if (!items[i].layer || items[i].action != TerrainAction::kMaterial) continue;
      bool dark = false;
      for (uint32_t l = first[i]; l < first[i + 1]; ++l) dark = dark || squares[link[l]].dark;
      if (!dark) continue;
      for (uint32_t l = first[i]; l < first[i + 1]; ++l) {
        changed = changed || !squares[link[l]].dark;
        squares[link[l]].dark = true;
      }
    }
  }
  for (size_t i = 0; i < items.size(); ++i) {
    if (!items[i].layer) continue;
    for (uint32_t l = first[i]; l < first[i + 1]; ++l) items[i].light = items[i].light && !squares[link[l]].dark;
  }
}

// --- Native shadow maps (frame-map section 15; spec 2026-10-08, Design 4) ---
// The game's two sun shadow maps, drawn by the pass from the frame's shadow
// records (FrameScene::shadow_draws) into two depth targets of its own, each
// record with its own rows (the game's matrix of that draw: object to the
// light's clip space) through the clay vertex shader, depth-only, with the
// pixel shader kClayShadowPs.
//
// Size: kShadowMapSize, the game's 1024, a constant of the pass: nothing of it
// follows the window or the clay target.
//
// Depth convention. The game draws its maps under the reversed range: window
// depth = 1 - z of the clip position (viewport Z scale -1, offset 1), cleared
// to 0, tested greater-equal: the largest window depth, the caster nearest the
// light, stays. The RHI has less and less-equal only, so the native map holds
// the pass's usual direction: depth = z (ShadowNativeDepth of the game's window
// depth, 1 - window), cleared to kShadowMapClear = 1, tested less-equal
// (MirrorCompare of the game's function): the smallest z stays, which is the
// same caster. A native texel is therefore the depth row of the light matrix
// (ShadowBlock::world_to_map row 2, world_to_clip row 2) at the nearest caster,
// plus the bias; 1 where no caster is.
//
// Bias. The game's polygon offset (ShadowCaster: scale in the register's
// units, sixteenths of a pixel of slope; offset in window-depth units) moves
// the game's window depth: ShadowBias(depth, slope, scale, offset) =
// depth + (scale / 16) * slope + offset, slope the larger of the depth's two
// screen derivatives (the D3D rule). The game's pairs are negative (near -32,
// -0.001; far -80, -0.005): the window depth falls, the caster moves away from
// the light. In the native direction the same step is added:
// ShadowMapDepth = z - (scale / 16) * slope - offset (ShadowBiasTerms holds the
// two factors with their signs turned), saturated; the RHI's pipeline has no
// bias, so kClayShadowPs writes it as the pixel's depth. The offset is exact
// here. The emulated frame's is not: the SDK makes an integer bias of it that a
// float depth target scales with the exponent of the primitive's depth, right
// for window depths in [0.5, 1) and half or less below 0.5 (frame-map section
// 15, "Bias"), so casters whose game window depth is under 0.5 (native depth
// over 0.5: the far half of the map's range) are pushed back further here than
// in the emulated picture.
//
// Texels. The pass draws with a viewport whose origin is (0.5, 0.5)
// (kShadowViewportOrigin): a clip position (x, y, w) lands at window
// ((x / w / 2 + 1 / 2) * 1024 + 0.5, (-y / w / 2 + 1 / 2) * 1024 + 0.5), and the
// centre of native texel (i, j) is window (i + 0.5, j + 0.5), texture coordinate
// ((i + 0.5) / 1024, (j + 0.5) / 1024). That is where the scene's c36..c43
// (ShadowBlock::world_to_map: u = x / 2 + (1 / 2 + h) w, v = -y / 2 + (1 / 2 + h) w,
// h half a texel) put the same point: (u, v) * 1024 is the window position
// above. So world_to_map addresses the native maps as it stands, with its half
// texel and nothing added or taken away (the mask's microcode adds none), and a
// lookup must use world_to_map, never world_to_clip; drawing uses the records'
// rows (world_to_clip times the draw's own world matrix). The game snaps the
// light's origin to whole texels of clip space, so the origin lies on a texel
// centre in both. (Inference: the half texel in the registers is the console's
// pixel centre at whole window coordinates; the viewport origin stands in for
// it. Without it a native texel would be the game's shifted by half a texel.)
inline constexpr uint32_t kShadowMapSize = 1024;
inline constexpr uint32_t kShadowMapTexelBytes = 4;  // D32_FLOAT
inline constexpr float kShadowMapClear = 1.0f;
inline constexpr float kShadowViewportOrigin = 0.5f;
// The maps' depth function, a capture::Compare on the native map.
inline constexpr capture::Compare kShadowMapCompare = capture::Compare::kLessEqual;

// The game's window depth of a clip-space z (z / w; the light is orthographic),
// and the native map's depth of a game window depth.
inline float ShadowGameDepth(float z) { return 1.0f - z; }
inline float ShadowNativeDepth(float game_depth) { return 1.0f - game_depth; }

// The game's polygon offset on a game window depth: `slope` the larger absolute
// screen derivative of the depth, `scale` and `offset` the registers' values
// (ShadowCaster::bias_scale, ::bias_offset).
inline float ShadowBias(float depth, float slope, float scale, float offset) {
  return depth + (scale / 16.0f) * slope + offset;
}

// Whether a shadow record's depth state is the one the maps are drawn with: the
// test on, depth written, the reversed range, greater-equal, which on the
// native map is kShadowMapCompare. Any other state is not drawn (kState): the
// convention above holds for this one alone.
inline bool ShadowDepthStateUsable(const capture::RenderState& rs) {
  return rs.valid && rs.depth_test && rs.depth_write && rs.depth_reversed &&
         rs.depth_func == capture::Compare::kGreaterEqual && ClayDepthFunc(rs) == kShadowMapCompare;
}

// The bias of one draw as kClayShadowPs adds it to the native depth:
// depth + slope * (the depth's larger derivative) + constant. Both zero for a
// draw without the offset.
struct ShadowBiasTerms {
  float slope = 0.0f;
  float constant = 0.0f;
};
// The largest scale and offset a draw's bias may hold (the game's: 80 and
// 0.005). A pair past them, or one that is not a number, is not a bias.
inline constexpr float kShadowBiasScaleMax = 4096.0f, kShadowBiasOffsetMax = 1.0f;

// False, with `out` untouched, for a biased caster whose pair is not usable.
inline bool MakeShadowBiasTerms(const capture::ShadowCaster& c, ShadowBiasTerms* out) {
  ShadowBiasTerms t;
  if (c.biased) {
    if (!(std::fabs(c.bias_scale) <= kShadowBiasScaleMax) || !(std::fabs(c.bias_offset) <= kShadowBiasOffsetMax)) return false;
    t.slope = -(c.bias_scale / 16.0f);
    t.constant = -c.bias_offset;
  }
  *out = t;
  return true;
}

// What kClayShadowPs writes for a pixel whose rasterised depth is `z` (the clip
// z: the native direction) and whose depth changes by `slope` a pixel at most:
// saturate(z + b.slope * slope + b.constant). A NaN gives 0, as the HLSL's
// saturate does (none arises: z is the rasteriser's and the terms are checked).
inline float ShadowMapDepth(float z, float slope, const ShadowBiasTerms& b) {
  const float v = z + b.slope * slope + b.constant;
  if (!(v > 0.0f)) return 0.0f;
  return v < 1.0f ? v : 1.0f;
}

// Alpha-to-mask on the maps (ruling after the review of Task 2). A map has one
// sample a pixel, as the game's (surface word 0x10000410: no multisampling),
// and there the GPU covers the pixel iff alpha >= 1 - offset / 4 (the emulator's
// rule: SDK src/graphics/pipeline/shader/dxbc_translator_om.cpp, lines 1681 to
// 1685, threshold = offset * -scale + base and alpha >= threshold; line 1790,
// the form without multisampling, base 1 and scale 1/4; lines 1740 to 1746, the
// offset of the pixel's parity, x the high bit). The offsets are the top byte
// of RB_COLORCONTROL (register 0x2202), two bits a parity from bit 24, and bit
// 4 turns it on. The leaf cards' word 0xAA00001C: every offset 2, alpha >= 0.5
// at every pixel, after the alpha test (greater than the reference 0).
inline bool ShadowAlphaToMaskOn(uint32_t color_control) { return (color_control & 0x10u) != 0; }
inline float ShadowAlphaToMaskThreshold(uint32_t color_control, uint32_t x, uint32_t y) {
  const uint32_t index = ((x & 1u) << 1) | (y & 1u);
  const uint32_t offset = (color_control >> (24 + 2 * index)) & 3u;
  return 1.0f - 0.25f * float(offset);
}

// The three depth-only pipelines of the maps, one a cull mode (capture::Cull in
// the pass's window space, ClayCull): none (the leaf cards), front, back. No
// other state differs between shadow draws: no colour target, the depth test
// kShadowMapCompare with write; the alpha test and the bias are the shader's.
inline constexpr size_t kShadowPipelines = 3;
constexpr size_t ShadowPipelineSlot(capture::Cull cull) {
  return cull == capture::Cull::kFront ? 1 : cull == capture::Cull::kBack ? 2 : 0;
}

// What the pass does with one shadow record.
struct ShadowDrawState {
  ShadowDrawSkip skip = ShadowDrawSkip::kNone;
  capture::Cull cull = capture::Cull::kNone;  // in the pass's window space
  // The draw's alpha decides: it needs its coordinates and its texture, and is
  // not drawn without them. `alpha`: the test's fields of the root constants
  // (kAlphaTest with the function, kAlphaToMask with the offsets, always as on
  // one sample; kAlphaFromAlbedo with the component; the reference, the scale).
  bool textured = false;
  ClayAlpha alpha;
  ShadowBiasTerms bias;
};

// Rules (fail closed: nothing is drawn from a value that was not read or is
// not a number). The state words must have been read (kUnread) and the depth
// state be the shadow passes' (kState); the caster entry of a known kind (kTwin); the rows
// finite (kMatrix); the bias usable (kBias). An opaque caster (no pixel shader
// in the game) is drawn solid. An alpha-test caster whose state has the alpha
// test or alpha-to-mask on is drawn with them from its texture's alpha times
// its scale, and needs a textured material with that recipe, a finite scale of
// usable size and a finite coordinate transform (kAlpha); with neither on it is
// solid, as in the game. The bushes' distance dissolve is not drawn (ruling):
// their caster is the texture alpha test alone.
inline ShadowDrawState DecideShadowDraw(const capture::DrawRecord& r, const capture::ShadowCaster& caster) {
  ShadowDrawState d;
  auto skip = [&](ShadowDrawSkip why) {
    d = ShadowDrawState{};
    d.skip = why;
    return d;
  };
  const capture::RenderState rs = capture::DecodeRenderState(r.state_words);
  if (!rs.valid) return skip(ShadowDrawSkip::kUnread);
  if (!ShadowDepthStateUsable(rs)) return skip(ShadowDrawSkip::kState);
  if (caster.kind != capture::CasterKind::kOpaque && caster.kind != capture::CasterKind::kAlphaTest) {
    return skip(ShadowDrawSkip::kTwin);
  }
  for (float v : r.rows) {
    if (!std::isfinite(v)) return skip(ShadowDrawSkip::kMatrix);
  }
  if (!MakeShadowBiasTerms(caster, &d.bias)) return skip(ShadowDrawSkip::kBias);
  d.cull = ClayCull(rs.cull, rs.front_cw);
  if (caster.kind == capture::CasterKind::kAlphaTest && (rs.alpha_test || rs.alpha_to_mask)) {
    const capture::Material& m = r.material;
    bool usable = m.status == capture::MaterialStatus::kTextured && m.alpha.source == capture::AlphaSource::kAlbedo &&
                  std::fabs(m.alpha_scale) <= 1024.0f;  // (false for a NaN)
    for (float v : m.uv_xform) usable = usable && std::isfinite(v);
    if (!usable) return skip(ShadowDrawSkip::kAlpha);
    d.textured = true;
    d.alpha.mode = kAlphaFromAlbedo | (uint32_t(m.alpha.component & 3) << 5);
    if (rs.alpha_test) d.alpha.mode |= (uint32_t(rs.alpha_func) & 7u) | kAlphaTest;
    // (the sample count's two bits stay 0: the map has one sample whatever the record's surface word says)
    if (rs.alpha_to_mask) d.alpha.mode |= kAlphaToMask | (uint32_t(rs.alpha_to_mask_offsets) << kAlphaToMaskShift);
    d.alpha.ref = rs.alpha_ref;
    d.alpha.scale = m.alpha_scale;
  }
  return d;
}

// The root constants of a shadow draw: a clay draw's (the record's rows, layout,
// base vertex and cut; the coordinates' transform, the sampler and the alpha
// fields of a draw whose alpha decides) without light, and with the bias in the
// two words a depth-only draw has no other use for: `color` holds the bits of
// the constant term and `pad2` those of the slope term (kClayShadowPs reads
// them with asfloat; the vertex shader reads neither).
inline ClayConstants MakeShadowConstants(const capture::DrawRecord& r, uint32_t vertex_count, const float uv[4],
                                         bool textured, uint32_t sampler, const ClayAlpha& alpha,
                                         const ShadowBiasTerms& bias) {
  ClayConstants c = MakeClayConstants(r, vertex_count, 0, uv, textured, sampler, alpha, 0, 0);
  c.color = FloatBits(bias.constant);
  c.pad2 = FloatBits(bias.slope);
  return c;
}

// Where the pass's viewport puts a clip position in a map, in texels from the
// map's corner (the window position: "Texels" above), and where a map's
// register rows (ShadowBlock::world_to_map[c]) put a world position, in the
// same units ((u, v) / w * kShadowMapSize). False for a w of zero or a value
// that is not finite. The texel is the floor of each.
inline bool ShadowMapWindow(const float clip[4], double out[2]) {
  const double w = clip[3];
  if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) || !std::isfinite(w) || w == 0.0) return false;
  const double size = double(kShadowMapSize);
  out[0] = (0.5 * (double(clip[0]) / w) + 0.5) * size + double(kShadowViewportOrigin);
  out[1] = (-0.5 * (double(clip[1]) / w) + 0.5) * size + double(kShadowViewportOrigin);
  return true;
}
inline bool ShadowMapLookup(const float map_rows[4][4], const float world[3], double out[2]) {
  double v[4];
  for (int i = 0; i < 4; ++i) {
    v[i] = double(map_rows[i][0]) * world[0] + double(map_rows[i][1]) * world[1] + double(map_rows[i][2]) * world[2] +
           double(map_rows[i][3]);
    if (!std::isfinite(v[i])) return false;
  }
  if (v[3] == 0.0) return false;
  out[0] = v[0] / v[3] * double(kShadowMapSize);
  out[1] = v[1] / v[3] * double(kShadowMapSize);
  return true;
}

// --- Native shadows: the depth pre-pass and the screen mask (frame-map section 15, "Native shadows"; spec 2026-10-08, Design 5) ---
// The game's order: depth pre-pass, mask, colour. With the shadows on, in a lit
// frame whose shadow block is valid and whose two maps were drawn, the pass
//   1. draws every scene record that writes depth once more before the colour
//      draws, depth only (PrepassKey), with the record's own vertex path and
//      the discards of its colour form (the alpha test, alpha-to-mask, a
//      terrain layer's hole and weight), into the clay depth target;
//   2. writes the screen mask (1 = lit by the sun) in one full-screen step
//      from that depth and the two maps: the arithmetic of the game's mask
//      shader 0x0F99810F072BF0FC (light_formulas.py: mask_0f99 = mask_near,
//      mask_far, mask_combine; lighting-formulas.md, "Shadows"), evaluated for
//      every pixel, a pixel at the clear depth included (it is a far pixel:
//      its mask is split.w, as the game's is). The game's second mask shader
//      (0x1B4B...) is not ported: in the emulated frame 0F99 writes every pixel;
//   3. clears the depth again and draws the colour pass as it always did, each
//      lit pixel reading the mask at its place (kClayBindings: t4).
// The depth of step 1 is the mask's alone: the colour pass does not test
// against it (it starts from a cleared depth, with each record's captured
// function), so no record can lose a pixel to its own pre-pass depth.
// In every other frame nothing of this is drawn and the 1 x 1 white texture
// is bound as the mask: the lighting picture.
//
// Compare directions. The game's maps hold g = 1 - z of the nearest caster and
// its shader tests 1 - z > g (near map, strictly) and 1 - z >= g (far map). The
// native maps hold n = z of the nearest caster, 1 where none is ("Native shadow
// maps"), so with n = 1 - g:
//   near: lit iff z < n      (MaskNearLit: the receiver is nearer the light than the stored caster)
//   far:  lit iff z <= n     (MaskFarLit: not farther)
// z the receiver's depth row of ShadowBlock::world_to_map. A receiver that IS
// the stored caster is lit through the caster's bias (n = z + bias); without a
// bias the near map shadows it and the far map does not, as in the game.
//
// The mirrors below are in double (the inputs are the floats the shader gets),
// so that they can be held to the reference's numbers to many places; the HLSL
// (shaders::kClayMaskPs) is the same arithmetic in single precision.

// A scene record is in the pre-pass when its colour pipeline tests and writes
// depth. Its pre-pass pipeline: the same pixel shader kind, depth function and
// cull, no colour written, no blend. At most 4 kinds x 3 functions (always,
// less, less-equal) x 3 cull modes = 36 keys, the size of the pre-pass's own
// pipeline cache: it cannot overflow, and the State line's count is the colour
// pass's alone.
inline constexpr size_t kMaxPrepassPipelines = 36;
inline bool InPrepass(const PipelineKey& colour) { return colour.depth_test != 0 && colour.depth_write != 0; }
inline PipelineKey PrepassKey(const PipelineKey& colour) {
  PipelineKey k;
  k.textured = colour.textured;
  k.color_mask = 0;
  k.depth_test = 1;
  k.depth_write = 1;
  k.depth_func = colour.depth_func;
  k.cull = colour.cull;
  return k;
}
// Whether a textured record's alpha can discard a pixel (the alpha test, alpha-to-mask). One whose
// alpha cannot has the depth of flat clay: its pre-pass draw is the flat kind's (PrepassKey with the
// alpha): no texture is read and no coordinates are bound (the review of plan Task 5). A terrain
// record keeps its kind: a layer discards by its hole and weight whatever its alpha says.
inline bool PrepassDiscards(const ClayAlpha& alpha) { return (alpha.mode & (kAlphaTest | kAlphaToMask)) != 0; }
inline PipelineKey PrepassKey(const PipelineKey& colour, const ClayAlpha& alpha) {
  PipelineKey k = PrepassKey(colour);
  if (k.textured == 1 && !PrepassDiscards(alpha)) k.textured = 0;
  return k;
}

// The literals of 0x0F99... (frame-map section 15, "Literals": the GPU's register file at every
// execution in 212 logged frames, and the shader object's table): the twelve tap offsets c250..c255
// paired as the microcode pairs them (light_formulas.MASK_NEAR_TAPS), the weight of a tap c248.y, how
// far beyond split.y the far map is still read c248.x, the slope of the fade c249.w; and the kernel
// radius, the shader's c77.x = 2.5 / 1024 in all of them: 2.5 texels of the near map.
inline constexpr float kMaskNearTaps[12][2] = {
    {-0.20334500074386597f, 0.6207159757614136f},  {0.18546099960803986f, -0.893123984336853f},
    {0.9623399972915649f, -0.19498300552368164f},  {0.5194560289382935f, 0.7670220136642456f},
    {-0.6959139704704285f, 0.45713698863983154f},  {0.5074309706687927f, 0.06442499905824661f},
    {-0.8401439785957336f, -0.07357999682426453f}, {0.8964200019836426f, 0.41245800256729126f},
    {0.4734340012073517f, -0.4800260066986084f},   {-0.7915589809417725f, -0.5977100133895874f},
    {-0.32621198892593384f, -0.40580999851226807f}, {-0.32194000482559204f, -0.9326149821281433f},
};
inline constexpr uint32_t kMaskNearTapCount = 12;
inline constexpr float kMaskTapWeight = 0.0833333358168602f;
inline constexpr float kMaskFarReach = 10.0f;
inline constexpr float kMaskFadeSlope = 0.1f;
inline constexpr float kMaskNearRadiusTexels = 2.5f;
inline constexpr float kMaskNearRadius = kMaskNearRadiusTexels / float(kShadowMapSize);
// The rotation tile repeats every 32 pixels of the GAME's target, 1120 x 720 (its c47 = (0.5, 0.5,
// 1/32, 1/32): the texel of pixel p is p mod 32). On a native target of another size the pixel's
// centre is scaled into those units first, so the pattern keeps the game's scale on the screen.
inline constexpr uint32_t kMaskTileSize = 32;
inline constexpr float kMaskGameWidth = 1120.0f, kMaskGameHeight = 720.0f;

inline bool MaskNearLit(double z, double stored) { return z < stored; }
inline bool MaskFarLit(double z, double stored) { return z <= stored; }

// (u, v, depth) of the homogeneous point P under four rows: rows[0..2] . P / rows[3] . P. False when w
// is zero or a result is not finite (light_formulas._mask_project).
inline bool MaskProject(const float rows[4][4], const double P[4], double out[3]) {
  double v[4];
  for (int i = 0; i < 4; ++i) {
    v[i] = double(rows[i][0]) * P[0] + double(rows[i][1]) * P[1] + double(rows[i][2]) * P[2] + double(rows[i][3]) * P[3];
  }
  if (!std::isfinite(v[3]) || v[3] == 0.0) return false;
  double r[3];
  for (int i = 0; i < 3; ++i) {
    r[i] = v[i] / v[3];
    if (!std::isfinite(r[i])) return false;
  }
  out[0] = r[0];
  out[1] = r[1];
  out[2] = r[2];
  return true;
}

// A tap offset turned by the pixel's rotation terms (a, b): (a x + b y, a y - b x) (mask_rotate).
inline void MaskRotate(const float offset[2], const double rot[2], double out[2]) {
  out[0] = rot[0] * double(offset[0]) + rot[1] * double(offset[1]);
  out[1] = rot[0] * double(offset[1]) - rot[1] * double(offset[0]);
}

// The texel a point fetch at `coordinate` (in texels, finite) reads under clamp addressing.
inline int MaskTexelIndex(double coordinate, uint32_t size) {
  const double t = std::floor(coordinate);
  if (!(t > 0.0)) return 0;
  if (t > double(size - 1)) return int(size - 1);
  return int(t);
}

// The near map's term (mask_near): the share of the twelve taps that are lit. `texel(ix, iy)`: the
// native near map's value at a texel inside the map; `rot`: the rotation terms (a, b); `radius`: in
// units of the map (kMaskNearRadius); `size`: the map's size in texels. A point the rows cannot
// project gives 1 without a fetch; a tap whose coordinate is not finite counts as lit.
template <class Texel>
double MaskNear(const double P[4], const float rows[4][4], Texel&& texel, const double rot[2], double radius, uint32_t size) {
  double p[3];
  if (!MaskProject(rows, P, p) || size == 0) return 1.0;
  int lit = 0;
  for (uint32_t i = 0; i < kMaskNearTapCount; ++i) {
    double o[2];
    MaskRotate(kMaskNearTaps[i], rot, o);
    const double tu = p[0] + radius * o[0], tv = p[1] + radius * o[1];
    if (!std::isfinite(tu) || !std::isfinite(tv) ||
        MaskNearLit(p[2], double(texel(MaskTexelIndex(tu * double(size), size), MaskTexelIndex(tv * double(size), size))))) {
      ++lit;
    }
  }
  return double(lit) * double(kMaskTapWeight);
}

// The far map's term (mask_far): the linear filter of four lit / shadowed texels, floor(x) - 1 and
// floor(x) by floor(y) - 1 and floor(y), weights frac(x) and frac(y) towards the higher texel, with
// (x, y) = (u, v) * size. `texel(ix, iy)`: the native far map's value at a texel inside the map (the
// indices are clamped here). A point the rows cannot project, or one whose texel is no number an
// integer holds, gives 1 without a fetch.
template <class Texel>
double MaskFar(const double P[4], const float rows[4][4], Texel&& texel, uint32_t size) {
  double p[3];
  if (!MaskProject(rows, P, p) || size == 0) return 1.0;
  const double x = p[0] * double(size), y = p[1] * double(size);
  if (!(std::fabs(x) < 2147483648.0 && std::fabs(y) < 2147483648.0)) return 1.0;
  const double ix = std::floor(x), iy = std::floor(y);
  const double fx = x - ix, fy = y - iy;
  auto lit = [&](double dx, double dy) {
    return MaskFarLit(p[2], double(texel(MaskTexelIndex(ix + dx, size), MaskTexelIndex(iy + dy, size)))) ? 1.0 : 0.0;
  };
  const double v = fx * fy * lit(0, 0) + (1.0 - fx) * fy * lit(-1, 0) + (1.0 - fx) * (1.0 - fy) * lit(-1, -1) + fx * (1.0 - fy) * lit(0, -1);
  return std::isfinite(v) ? v : 0.0;
}

// The two maps' terms and the fade (mask_combine):
//   m = lerp(near, far, sat((d - split[0]) / split[2]));  result = lerp(m, split[3], sat(slope * (d - split[1])))
// The value beyond the shadow distance is split[3] (c12.w, 0.46 to 0.53 in the captures), NOT 1. A
// blend width that is not positive is a step at split[0]; a value that is not finite counts as 0.
inline double MaskCombine(double d, double near_term, double far_term, const float split[4], float slope) {
  auto finite = [](double x) { return std::isfinite(x) ? x : 0.0; };
  auto sat = [](double x) { return x != x ? 0.0 : x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : x; };
  const double s[4] = {finite(split[0]), finite(split[1]), finite(split[2]), finite(split[3])};
  const double k = finite(slope);
  near_term = finite(near_term);
  far_term = finite(far_term);
  if (d != d) d = 0.0;
  const double w = s[2] > 0.0 ? sat((d - s[0]) / s[2]) : (d > s[0] ? 1.0 : 0.0);
  const double m = near_term + w * (far_term - near_term);
  const double fade = sat(k * (d - s[1]));
  return finite(m + fade * (s[3] - m));
}

// The mask step's constants (cbuffer Mask of shaders::kClayMaskPs, register b0 of the mask's own
// layout; shaders::kClayMaskFields is its layout): one small slot of the frame's constant buffer.
struct ClayMaskConstants {
  float inv_view_proj[4][4];  // rows: world (x, y, z, w) = rows . (X, Y, depth, 1), (X, Y) the pixel's centre in -1..1, Y up
  float near_rows[4][4];      // ShadowBlock::world_to_map[0] (pixel c36..c39 at a scene draw)
  float far_rows[4][4];       // ShadowBlock::world_to_map[1] (c40..c43)
  float eye[3];               // the frame's eye (LightBlock::eye, pixel c19 = the mask shader's c31)
  float radius;               // kMaskNearRadius
  float split[4];             // ShadowBlock::split (c12)
  float inv_target[2];        // 1 / the mask target's width and height
  float tile_scale[2];        // game pixels a native pixel: kMaskGameWidth / width, kMaskGameHeight / height
  uint32_t rotate;            // 1: the rotation tile is bound at t3; 0: unrotated taps, (a, b) = (1, 0)
  float map_size;             // kShadowMapSize
  float pad[2];
};
static_assert(sizeof(ClayMaskConstants) == 256 && sizeof(ClayMaskConstants) <= kClayTerrainSlotBytes);

// The frame's mask constants. False, with `out` untouched, when the block is not valid, the target
// has no size, or a value is not a number of usable size (a position's bound for the eye and the
// matrices' entries: no value read from the guest reaches the shader unchecked).
inline bool MakeClayMaskConstants(const capture::ShadowBlock& block, const float inv_view_proj[4][4], const float eye[3],
                                  uint32_t width, uint32_t height, bool rotate, ClayMaskConstants* out) {
  if (!block.valid || !inv_view_proj || !eye || !out || width == 0 || height == 0) return false;
  ClayMaskConstants c{};
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      if (!std::isfinite(inv_view_proj[i][j]) || !std::isfinite(block.world_to_map[0][i][j]) ||
          !std::isfinite(block.world_to_map[1][i][j])) {
        return false;
      }
      c.inv_view_proj[i][j] = inv_view_proj[i][j];
      c.near_rows[i][j] = block.world_to_map[0][i][j];
      c.far_rows[i][j] = block.world_to_map[1][i][j];
    }
    if (!std::isfinite(block.split[i])) return false;
    c.split[i] = block.split[i];
  }
  for (int i = 0; i < 3; ++i) {
    if (!LightPositionUsable(eye[i])) return false;
    c.eye[i] = eye[i];
  }
  c.radius = kMaskNearRadius;
  c.inv_target[0] = 1.0f / float(width);
  c.inv_target[1] = 1.0f / float(height);
  c.tile_scale[0] = kMaskGameWidth / float(width);
  c.tile_scale[1] = kMaskGameHeight / float(height);
  c.rotate = rotate ? 1u : 0u;
  c.map_size = float(kShadowMapSize);
  *out = c;
  return true;
}

// The texel of the rotation tile a native pixel reads: the pixel's centre in the game's pixel units,
// floored, modulo the tile (point fetch, repeat).
inline uint32_t MaskTileTexel(uint32_t pixel, float tile_scale) {
  const double g = std::floor((double(pixel) + 0.5) * double(tile_scale));
  if (!(g >= 0.0) || !(g < 4294967296.0)) return 0;
  return uint32_t(g) % kMaskTileSize;
}

// The mask of native pixel (px, py) whose pre-pass depth is `depth` (0 at the near plane, 1 the clear
// value): mask_0f99 in the native form, as shaders::kClayMaskPs computes it.
//   S = (X, Y, depth, 1), X = (2 px + 1) / width - 1, Y = 1 - (2 py + 1) / height
//   P = inv_view_proj . S / w;  d = |P - eye|
//   near = MaskNear(P) where d < split[0] + split[2], else 0
//   far  = MaskFar(P)  where split[0] < d < split[1] + kMaskFarReach, else 0
//   MaskCombine(d, near, far, split, kMaskFadeSlope)
// `near_texel(ix, iy)`, `far_texel(ix, iy)`: the native maps; `rot_texel(tx, ty, out[2])`: the
// rotation tile's texel as the shader's (.x, .y) in 0..1, read only when the constants say it is
// bound. A pixel without a finite world position gives split[3], as one beyond the shadow distance.
template <class Near, class Far, class Rot>
double MaskPixel(uint32_t px, uint32_t py, float depth, const ClayMaskConstants& c, Near&& near_texel, Far&& far_texel, Rot&& rot_texel) {
  const double S[4] = {(2.0 * double(px) + 1.0) * double(c.inv_target[0]) - 1.0, 1.0 - (2.0 * double(py) + 1.0) * double(c.inv_target[1]),
                       double(depth), 1.0};
  const double beyond = std::isfinite(c.split[3]) ? double(c.split[3]) : 0.0;
  double world[3];
  if (!MaskProject(c.inv_view_proj, S, world)) return beyond;
  double d2 = 0.0;
  for (int i = 0; i < 3; ++i) d2 += (world[i] - double(c.eye[i])) * (world[i] - double(c.eye[i]));
  const double d = std::sqrt(d2);
  if (!std::isfinite(d)) return beyond;
  const double P[4] = {world[0], world[1], world[2], 1.0};
  auto finite = [](float x) { return std::isfinite(x) ? double(x) : 0.0; };
  const double s0 = finite(c.split[0]), s1 = finite(c.split[1]), s2 = finite(c.split[2]);
  const uint32_t size = c.map_size >= 1.0f && c.map_size <= 16384.0f ? uint32_t(c.map_size) : 0u;
  double near_term = 0.0, far_term = 0.0;
  if (s0 + s2 > d) {
    double rot[2] = {1.0, 0.0};
    if (c.rotate) {
      float t[2] = {1.0f, 0.5f};
      rot_texel(MaskTileTexel(px, c.tile_scale[0]), MaskTileTexel(py, c.tile_scale[1]), t);
      rot[0] = 2.0 * double(t[0]) - 1.0;
      rot[1] = 2.0 * double(t[1]) - 1.0;
    }
    near_term = MaskNear(P, c.near_rows, near_texel, rot, double(c.radius), size);
  }
  if (d > s0 && s1 + double(kMaskFarReach) > d) far_term = MaskFar(P, c.far_rows, far_texel, size);
  return MaskCombine(d, near_term, far_term, c.split, kMaskFadeSlope);
}

// The frame's view-projection. The clay vertex shader makes a clip position of each record's own
// rows (the game's c0..c3 of that draw) and the positions the pass builds for it. For two kinds of
// record those positions are world positions, so their rows ARE the frame's world-to-clip matrix: a
// terrain record (x, y, height from the heightmap) and an instanced record (the copies laid out in the
// world). A tree leaf's record has its whole world matrix beside it (LightInputs::world_rot and
// world_pos: vertex c4..c6), so it gives rows * inverse(world rows), as PassMatrix gives a shadow
// pass's. No other record carries its world translation. In the captures every one of these
// agrees: the terrain's and the instanced records' rows are bit-equal within a frame (567 and 157 rows
// a frame at the bridge), the leaves' and the other meshes' products agree with them to 2.5e-7 of the
// largest entry, and the inverse is the mask shader's own c116..c119 to 4e-8 (frame-map section 15,
// "Native shadows").
// FrameInverseViewProj: the inverse of that one matrix: the matrix of the MAJORITY of the records
// that give one (more than half of them agree with it within kFrameViewTolerance, of the row's
// largest entry of its kind: the three direction entries, or the translation). The others, a matrix
// that is not finite among them, are outliers: counted (FrameView::outliers, on the Shadow line) and
// without a say, so that one odd record does not take the frame's shadows away. False (the frame
// has no mask: "shadow-view") when no record gives a matrix, when no matrix has a majority (fail
// closed: one against one, two against two, all different), or when the matrix has no finite inverse.
// A record's rows as the matrix M of clip = M . p (rows x, y, z, w), by its layout.
inline void RecordMatrix(const capture::DrawRecord& r, float m[4][4]) {
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) m[i][j] = r.layout == capture::TransformLayout::kCombine ? r.rows[j * 4 + i] : r.rows[i * 4 + j];
  }
}

// Whether `m` is `ref` within `tol`: each entry within tol times the larger of its own magnitude in
// `ref` and the largest direction entry of its row. Nothing that is not finite agrees.
inline bool ViewMatricesAgree(const float ref[4][4], const float m[4][4], float tol) {
  for (int i = 0; i < 4; ++i) {
    const double row = std::fmax(std::fabs(double(ref[i][0])), std::fmax(std::fabs(double(ref[i][1])), std::fabs(double(ref[i][2]))));
    for (int j = 0; j < 4; ++j) {
      if (!std::isfinite(ref[i][j]) || !std::isfinite(m[i][j])) return false;
      const double bound = double(tol) * std::fmax(row, std::fabs(double(ref[i][j])));
      if (!(std::fabs(double(m[i][j]) - double(ref[i][j])) <= bound)) return false;
    }
  }
  return true;
}

// The inverse of a 4 x 4 matrix (Gauss-Jordan with row pivots, in double). False, with `out`
// untouched, when an entry is not finite, a pivot is no larger than 1e-12 of the largest entry, or
// the result does not fit a float.
inline bool InvertMatrix4(const float m[4][4], float out[4][4]) {
  double a[4][8];
  double scale = 0.0;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      if (!std::isfinite(m[i][j])) return false;
      a[i][j] = double(m[i][j]);
      a[i][4 + j] = i == j ? 1.0 : 0.0;
      scale = std::fmax(scale, std::fabs(a[i][j]));
    }
  }
  if (scale == 0.0) return false;
  for (int c = 0; c < 4; ++c) {
    int pivot = c;
    for (int r = c + 1; r < 4; ++r) {
      if (std::fabs(a[r][c]) > std::fabs(a[pivot][c])) pivot = r;
    }
    if (!(std::fabs(a[pivot][c]) > 1e-12 * scale)) return false;
    if (pivot != c) {
      for (int j = 0; j < 8; ++j) std::swap(a[c][j], a[pivot][j]);
    }
    const double d = a[c][c];
    for (int j = 0; j < 8; ++j) a[c][j] /= d;
    for (int r = 0; r < 4; ++r) {
      if (r == c) continue;
      const double f = a[r][c];
      if (f == 0.0) continue;
      for (int j = 0; j < 8; ++j) a[r][j] -= f * a[c][j];
    }
  }
  float inv[4][4];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      inv[i][j] = float(a[i][4 + j]);
      if (!std::isfinite(inv[i][j])) return false;
    }
  }
  std::memcpy(out, inv, sizeof(inv));
  return true;
}

// What FrameInverseViewProj found: the records that gave a matrix, those of them that do not agree
// with the matrix it chose (or tried), and why there is no inverse.
struct FrameView {
  uint32_t candidates = 0;
  uint32_t outliers = 0;
  FrameViewReason why = FrameViewReason::kNoRecord;
};

inline bool FrameInverseViewProj(const FrameScene& scene, float inverse[4][4], FrameView* info = nullptr,
                                 float view_proj[4][4] = nullptr) {
  FrameView v;
  auto done = [&](FrameViewReason why) {
    v.why = why;
    if (info) *info = v;
    return why == FrameViewReason::kNone;
  };
  // The matrix of one record, when it gives one.
  auto matrix_of = [&](const capture::DrawRecord& r, float m[4][4]) {
    if (r.skip != capture::SkipReason::kNone) return false;
    if (TerrainOf(scene, r) || r.instances.active) {
      RecordMatrix(r, m);
      return true;
    }
    if (const capture::LightInputs* l = LightOf(scene, r);
        l && l->family == capture::LightFamily::kTreeLeaf && l->kind == capture::LightVertexKind::kProbe) {
      float rows[4][4], world[3][4];
      RecordMatrix(r, rows);
      for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) world[i][j] = l->world_rot[i][j];
        world[i][3] = l->world_pos[i];
      }
      // (a leaf whose world rows cannot be inverted gives no matrix; it is no outlier)
      return capture::PassMatrix(rows, world, m);
    }
    return false;
  };
  // First pass: the majority's candidate (a vote: a matrix that more than half of the records agree
  // with survives it). Second pass: how many agree with it. (ViewMatricesAgree is false for anything
  // that is not finite, so a matrix that is not finite agrees with nothing, itself included.)
  // (When no record disagreed in the first pass, which is every frame of the captures, the candidate
  // never changed and every record was held against it: the second pass is not needed.)
  float ref[4][4] = {};
  uint32_t votes = 0;
  bool disagreed = false;
  for (const capture::DrawRecord& r : scene.draws) {
    float m[4][4];
    if (!matrix_of(r, m)) continue;
    ++v.candidates;
    if (votes == 0) {
      disagreed = disagreed || v.candidates > 1;
      std::memcpy(ref, m, sizeof(ref));
      votes = 1;
    } else if (ViewMatricesAgree(ref, m, kFrameViewTolerance)) {
      ++votes;
    } else {
      --votes;
      disagreed = true;
    }
  }
  if (v.candidates == 0) return done(FrameViewReason::kNoRecord);
  uint32_t agree = 0;
  if (!disagreed) {
    agree = ViewMatricesAgree(ref, ref, kFrameViewTolerance) ? v.candidates : 0;  // (a lone matrix that is not finite agrees with nothing)
  } else {
    for (const capture::DrawRecord& r : scene.draws) {
      float m[4][4];
      if (matrix_of(r, m) && ViewMatricesAgree(ref, m, kFrameViewTolerance)) ++agree;
    }
  }
  v.outliers = v.candidates - agree;
  if (!(uint64_t(agree) * 2 > v.candidates)) return done(FrameViewReason::kDisagree);
  float inv[4][4];
  if (!InvertMatrix4(ref, inv)) return done(FrameViewReason::kSingular);
  std::memcpy(inverse, inv, sizeof(inv));
  if (view_proj) std::memcpy(view_proj, ref, sizeof(ref));
  return done(FrameViewReason::kNone);
}

// The pre-pass's reach (plan Task 5b: the third spot's frame rate). Beyond split.y + kMaskFarReach
// units from the eye the mask is split.w whatever the depth: MaskCombine's fade is saturated there
// (0.1 * (d - split.y) >= 1) and neither map is read (the far term stops at the same distance, the
// near one at split.x + split.z), and a pixel at the clear depth is such a pixel too. So a
// depth-writing record that lies WHOLLY beyond that distance changes no pixel of the mask and is
// left out of the pre-pass: where it was the nearest surface the depth is now the clear value or
// that of a surface still farther away, and the mask is split.w either way; a nearer surface is
// in the pre-pass as before. This needs the record's depth function to keep the nearer surface
// (less, less-equal): a far record drawn with another function could overwrite a near one's depth,
// and is kept.
// The bound is the distance along the view axis, which is never more than the distance from the
// eye: with the frame's view-projection V (world to clip) and w(P) = V[3] . (P, 1), w(P) - w(eye) =
// V[3].xyz . (P - eye) <= |V[3].xyz| * |P - eye|. A record's own matrix M (RecordMatrix: V times its
// world matrix) gives the same w from the positions in its buffer, and w is linear, so its smallest
// value over the buffer is not below its smallest value over the positions' box (each of the four
// terms at its smaller end). Beyond: (w_min - w(eye)) / |V[3].xyz| >= reach, the reach with a
// margin for the mask's own rounding of the distance (it rebuilds the position from the depth).
// The box is of the buffer the pass draws (after the bones, after the copies were laid out; the
// rest pose of a deformed mesh, as drawn), computed when the buffer is built: sound for skinned and
// instanced records as well. Anything not known or not finite: kept.
inline constexpr float kPrepassReachMargin = 0.05f;
inline float PrepassReach(const float split[4]) { return split[1] + kMaskFarReach + kPrepassReachMargin; }

// The box of a position buffer's vertices, all four components. Not valid: no vertex, or a value
// that is not finite.
struct PositionBounds {
  float lo[4] = {}, hi[4] = {};
  bool valid = false;
};
inline PositionBounds MakePositionBounds(const capture::Float4* p, size_t count) {
  PositionBounds b;
  if (!p || count == 0) return b;
  float lo[4] = {p[0].x, p[0].y, p[0].z, p[0].w}, hi[4] = {p[0].x, p[0].y, p[0].z, p[0].w};
  bool finite = true;  // (every value is looked at: min and max would pass over a NaN)
  for (size_t i = 0; i < count; ++i) {
    const float v[4] = {p[i].x, p[i].y, p[i].z, p[i].w};
    for (int k = 0; k < 4; ++k) {
      finite = finite && std::isfinite(v[k]);
      lo[k] = v[k] < lo[k] ? v[k] : lo[k];
      hi[k] = v[k] > hi[k] ? v[k] : hi[k];
    }
  }
  if (!finite) return b;
  std::memcpy(b.lo, lo, sizeof(lo));
  std::memcpy(b.hi, hi, sizeof(hi));
  b.valid = true;
  return b;
}

// Whether a record with matrix `m` (RecordMatrix) and position box `box`, whose pre-pass depth
// function is `depth_func` (a capture::Compare on the pass's depth), lies wholly beyond the mask's
// reach in the frame of view-projection `view_proj`, eye and split (above).
inline bool PrepassBeyondReach(const float m[4][4], const PositionBounds& box, const float view_proj[4][4], const float eye[3],
                               const float split[4], uint8_t depth_func) {
  if (!box.valid || !m || !view_proj || !eye || !split) return false;
  if (depth_func != uint8_t(capture::Compare::kLess) && depth_func != uint8_t(capture::Compare::kLessEqual)) return false;
  double w_min = 0.0;
  for (int k = 0; k < 4; ++k) {
    if (!std::isfinite(m[3][k]) || !std::isfinite(box.lo[k]) || !std::isfinite(box.hi[k]) || !(box.lo[k] <= box.hi[k])) return false;
    const double a = double(m[3][k]) * double(box.lo[k]), b = double(m[3][k]) * double(box.hi[k]);
    w_min += a < b ? a : b;
  }
  double length2 = 0.0, w_eye = double(view_proj[3][3]);
  for (int k = 0; k < 3; ++k) {
    if (!std::isfinite(view_proj[3][k]) || !std::isfinite(eye[k])) return false;
    length2 += double(view_proj[3][k]) * double(view_proj[3][k]);
    w_eye += double(view_proj[3][k]) * double(eye[k]);
  }
  const double reach = double(PrepassReach(split));
  if (!std::isfinite(view_proj[3][3]) || !std::isfinite(w_min) || !std::isfinite(w_eye) || !std::isfinite(reach) || !(length2 > 0.0)) return false;
  return (w_min - w_eye) / std::sqrt(length2) >= reach;
}

// The log line of the maps, written when the pass's targets are created.
inline std::string FormatShadowTargetText() {
  char buf[128];
  std::snprintf(buf, sizeof(buf), "shadow maps 2 x %u x %u: depth D32_FLOAT %.1f MB together", kShadowMapSize, kShadowMapSize,
                double(2 * ClayTargetBytes(kShadowMapSize, kShadowMapSize, kShadowMapTexelBytes)) / (1024.0 * 1024.0));
  return buf;
}
// ... and of the mask target, one byte a pixel of the clay target's size.
inline constexpr uint32_t kClayMaskTexelBytes = 1;  // R8_UNORM
inline std::string FormatMaskTargetText(uint32_t width, uint32_t height) {
  char buf[128];
  std::snprintf(buf, sizeof(buf), "shadow mask %u x %u: R8_UNORM %.1f MB", width, height,
                double(ClayTargetBytes(width, height, kClayMaskTexelBytes)) / (1024.0 * 1024.0));
  return buf;
}

// The log's and F3's Shadow line (spec Design 5). "Shadow: off" with the
// shadows' switch off and no shadow records in the scene (ClayStats::shadow_switch).
// Else the block (valid, or invalid with the capture's
// reason: the frame is then drawn without shadows, "shadow-block"), and per
// map the records drawn into it of the scene's records, with what was not
// drawn by reason: the pass's own reasons (ShadowDrawSkipName) first, then,
// marked "capture", the game's shadow draws the capture did not record
// (ShadowSkipName). Then the draws on the game's map in neither pass
// (shadow-pass-extra) and the passes the game opened when they are not two: a
// frame with a valid block may hold a partial pass, and this is where it
// shows. Then the split distances of a valid block, and that the maps were not
// drawn when the block is valid and they were not (the mask's part says why).
// The frame counts at the end are the pass's totals since it was made ("since
// start"), over the frames drawn with the shadows' and the light's switches on;
// the capture's own line counts "shadow-block in N of M frames" over one log
// window of the frames with shadow records, which is another count.
inline std::string FormatShadowText(const ClayStats& st) {
  if (!st.shadow_switch) return "Shadow: off";
  std::string text = "Shadow: on, block ";
  if (st.shadow_block) {
    text += "valid";
  } else {
    text += std::string("invalid (") + capture::kShadowBlockReason + ": " + capture::ShadowBlockReasonName(st.shadow_reason) + ")";
  }
  const char* names[2] = {"near", "far"};
  char buf[160];
  for (size_t c = 0; c < 2; ++c) {
    std::snprintf(buf, sizeof(buf), ", %s %u of %u draws (skipped {", names[c], st.shadow_drawn[c], st.shadow_records[c]);
    text += buf;
    bool first = true;
    for (size_t i = 1; i < size_t(ShadowDrawSkip::kCount); ++i) {
      if (!st.shadow_skipped[c][i]) continue;
      std::snprintf(buf, sizeof(buf), "%s%s: %u", first ? "" : ", ", ShadowDrawSkipName(ShadowDrawSkip(i)), st.shadow_skipped[c][i]);
      text += buf;
      first = false;
    }
    for (size_t i = 1; i < capture::kShadowSkipCount; ++i) {
      if (!st.shadow_capture_skipped[c][i]) continue;
      std::snprintf(buf, sizeof(buf), "%scapture %s: %u", first ? "" : ", ", capture::ShadowSkipName(uint8_t(i)),
                    st.shadow_capture_skipped[c][i]);
      text += buf;
      first = false;
    }
    text += "})";
  }
  std::snprintf(buf, sizeof(buf), ", extra %u", st.shadow_extra);
  text += buf;
  if (st.shadow_extra) text += std::string(" (") + capture::kShadowPassExtraReason + ")";
  if (st.shadow_passes != 2) {
    std::snprintf(buf, sizeof(buf), ", passes %u", st.shadow_passes);
    text += buf;
  }
  if (st.shadow_block) {
    std::snprintf(buf, sizeof(buf), ", split (%g, %g, %g, %.4f)", st.shadow_split[0], st.shadow_split[1], st.shadow_split[2],
                  st.shadow_split[3]);
    text += buf;
    if (!st.shadow_maps) text += st.shadow_light_off ? " | maps not drawn: the frame has no light" : " | maps not drawn";
  }
  // The pre-pass and the mask: what was drawn before the mask, what is bound as the mask and, when
  // it is the white texture, why; then the pass's frame counts by reason.
  std::snprintf(buf, sizeof(buf), " | pre-pass %u draws, %u beyond reach", st.prepass_drawn, st.prepass_culled);
  text += buf;
  if (st.prepass_skipped) {
    std::snprintf(buf, sizeof(buf), " (%u without a pipeline)", st.prepass_skipped);
    text += buf;
  }
  switch (st.mask) {
    case ClayMask::kDrawn:
      text += st.mask_rotated ? " | mask drawn" : " | mask drawn, taps unrotated";
      if (st.mask_view_outliers) {
        std::snprintf(buf, sizeof(buf), " (%u records with another view)", st.mask_view_outliers);
        text += buf;
      }
      break;
    case ClayMask::kNoLight: text += " | mask white (no light)"; break;
    case ClayMask::kBlock: text += std::string(" | mask white (") + capture::kShadowBlockReason + ")"; break;
    case ClayMask::kMaps: text += " | mask white (maps not drawn)"; break;
    case ClayMask::kView:
      text += std::string(" | mask white (") + kShadowViewReason + ": " + FrameViewReasonName(st.mask_view) + ")";
      break;
    case ClayMask::kObjects:
      text += " | mask white (the shadows' objects could not be made: off until the pass is rebuilt or the game restarted)";
      break;
    default: text += " | mask white (off)"; break;
  }
  std::snprintf(buf, sizeof(buf), " | frames since start %llu: %s %llu, %s %llu, unrotated %llu",
                static_cast<unsigned long long>(st.shadow_frames),
                capture::kShadowBlockReason, static_cast<unsigned long long>(st.shadow_block_frames), kShadowViewReason,
                static_cast<unsigned long long>(st.shadow_view_frames), static_cast<unsigned long long>(st.shadow_unrotated_frames));
  text += buf;
  std::snprintf(buf, sizeof(buf), ", no-light %llu, no-objects %llu", static_cast<unsigned long long>(st.shadow_nolight_frames),
                static_cast<unsigned long long>(st.shadow_objects_frames));
  text += buf;
  return text;
}

// The pass's shadow skips by vertex shader, "0xHASH(reason): n" by reason and
// within one the most frequent shader first; empty when there is none.
inline std::string FormatShadowSkippedByVs(const ClayStats& st) {
  constexpr size_t kTop = 8;
  std::string text;
  char buf[96];
  for (size_t kind = 1; kind < size_t(ShadowDrawSkip::kCount); ++kind) {
    capture::ShaderTally<32>::Entry top[kTop];
    const size_t n = st.shadow_skipped_by_vs.Top(uint8_t(kind), top, kTop);
    for (size_t i = 0; i < n; ++i) {
      std::snprintf(buf, sizeof(buf), "%s0x%016llX(%s): %u", text.empty() ? "" : ", ",
                    static_cast<unsigned long long>(top[i].hash), ShadowDrawSkipName(ShadowDrawSkip(kind)), top[i].count);
      text += buf;
    }
  }
  return text;
}

// The F3 texture line: textured share of the drawn records (terrain records
// drawn with their material among them) and of the drawn non-terrain records
// (spec success criterion 2), cache counters and the top
// three final statuses other than textured, among which the state reasons rank:
// `alpha-unknown` (drawn records whose alpha test or blend had no alpha to
// apply) and `blend-unknown` (records left out, ClayStats). "Textures: off
// (latched)" once an RHI failure turned the texture path off.
inline std::string FormatTextureText(const TextureStats& ts, uint32_t drawn, uint32_t alpha_unknown = 0,
                                     uint32_t blend_unknown = 0) {
  if (ts.latched) return "Textures: off (latched)";
  std::vector<std::pair<const char*, uint32_t>> reasons;
  for (size_t i = 0; i < size_t(capture::MaterialStatus::kCount); ++i) {
    if (i == size_t(capture::MaterialStatus::kTextured) || ts.status[i] == 0) continue;
    reasons.emplace_back(capture::MaterialStatusName(capture::MaterialStatus(i)), ts.status[i]);
  }
  if (alpha_unknown) reasons.emplace_back("alpha-unknown", alpha_unknown);
  if (blend_unknown) reasons.emplace_back("blend-unknown", blend_unknown);
  std::stable_sort(reasons.begin(), reasons.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  const uint32_t share = drawn ? uint32_t(uint64_t(ts.textured) * 100 / drawn) : 0;
  const uint32_t non_terrain = drawn > ts.terrain ? drawn - ts.terrain : 0;
  const uint32_t textured_nt = ts.textured > ts.terrain_textured ? ts.textured - ts.terrain_textured : 0;
  const uint32_t share_nt = non_terrain ? uint32_t(uint64_t(textured_nt) * 100 / non_terrain) : 0;
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "Textures: textured %u of %u drawn (%u%%, %u%% non-terrain), resident %u (%.1f MB), "
                "uploads %u (%.1f MB), decode %.2f ms",
                ts.textured, drawn, share, share_nt, ts.resident,
                double(ts.resident_bytes) / (1024.0 * 1024.0), ts.uploads,
                double(ts.upload_bytes) / (1024.0 * 1024.0), ts.decode_ms);
  std::string text = buf;
  if (!reasons.empty()) {
    text += " | top untextured:";
    for (size_t i = 0; i < reasons.size() && i < 3; ++i) {
      text += std::string(i ? ", " : " ") + reasons[i].first + " " + std::to_string(reasons[i].second);
    }
  }
  return text;
}

// The F3 state line: pipelines in the pass's cache (with the draws that found
// no pipeline there, when there are any) and the drawn records with the alpha
// test applied, with a blend, and with an invalid state.
inline std::string FormatStateText(const ClayStats& st) {
  std::string text = "State: " + std::to_string(st.pipelines) + " pipelines";
  if (st.pipeline_overflow) text += " (overflow " + std::to_string(st.pipeline_overflow) + ")";
  return text + ", alpha test " + std::to_string(st.alpha_tested) + ", blended " +
         std::to_string(st.blended) + ", invalid " + std::to_string(st.state_invalid);
}

// The log's terrain line: the drawn terrain records, those drawn with their
// material and as flat clay, the layer records left out with their square, and
// the distinct texture sets bound (colour, weights, side texture: each takes
// three slots of the RHI's shader-visible view heap until one of its views is
// destroyed).
// Then, when there are any, the records that could not have their material by
// their own reason, most first (ClayStats::terrain_unusable, and no-positions
// and no-indices for records whose geometry could not be built): the cause of a
// square's fallback, which the other lines show only as terrain-square when the
// record is not the square's flat draw.
inline std::string FormatTerrainText(const ClayStats& st, const TextureStats& ts) {
  const uint32_t flat = ts.terrain > ts.terrain_textured ? ts.terrain - ts.terrain_textured : 0;
  std::string text = "Terrain: material " + std::to_string(ts.terrain_textured) + " of " +
                     std::to_string(ts.terrain) + " drawn, flat " + std::to_string(flat) + ", left out " +
                     std::to_string(st.terrain_skipped) + ", texture sets " + std::to_string(st.terrain_tuples);
  std::vector<std::pair<const char*, uint32_t>> reasons;
  for (size_t i = 0; i < size_t(capture::MaterialStatus::kCount); ++i) {
    if (st.terrain_unusable[i]) {
      reasons.emplace_back(capture::MaterialStatusName(capture::MaterialStatus(i)), st.terrain_unusable[i]);
    }
  }
  if (st.terrain_no_positions) reasons.emplace_back("no-positions", st.terrain_no_positions);
  if (st.terrain_no_indices) reasons.emplace_back("no-indices", st.terrain_no_indices);
  std::stable_sort(reasons.begin(), reasons.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  for (size_t i = 0; i < reasons.size(); ++i) {
    text += std::string(i ? ", " : " | without material: ") + reasons[i].first + " " + std::to_string(reasons[i].second);
  }
  return text;
}

// The log's light line. With the light off: that no light input was built
// (ClayLightInputsBuilt). Else: the prepared records with light inputs and those whose
// normals and second buffer (colours, probe weights and sets, instance
// colours) the geometry cache built, the world rotations of the
// objects and trunks among them by kind, and, when there are any, the rotations
// that are not one by vertex shader, the records without their buffers by
// reason and vertex shader (top eight each), the causes for which a probe or
// instance buffer (the copies' colours, the bend inputs) was refused, and the
// plant records that have their bend inputs with those buffers' bytes.
inline std::string FormatLightText(const ClayStats& st) {
  constexpr size_t kTop = 8;
  if (!st.light_switch) return "Light inputs: not built (light off)";
  std::string text = "Light inputs: ready " + std::to_string(st.light_ready) + " of " + std::to_string(st.lit) +
                     " lit records | world rotation:";
  for (size_t i = 0; i < size_t(RotationKind::kCount); ++i) {
    text += std::string(i ? ", " : " ") + RotationKindName(RotationKind(i)) + " " + std::to_string(st.light_rotation[i]);
  }
  auto by_vs = [&](const capture::ShaderTally<32>& tally, uint8_t kind) {
    capture::ShaderTally<32>::Entry top[kTop];
    const size_t n = tally.Top(kind, top, kTop);
    std::string list;
    char buf[48];
    for (size_t i = 0; i < n; ++i) {
      std::snprintf(buf, sizeof(buf), "%s0x%016llX: %u", i ? ", " : "", static_cast<unsigned long long>(top[i].hash),
                    top[i].count);
      list += buf;
    }
    return list;
  };
  for (size_t i = 1; i < size_t(RotationKind::kCount); ++i) {
    if (!st.light_rotation[i]) continue;
    text += std::string(" | ") + RotationKindName(RotationKind(i)) + " by vs {" + by_vs(st.light_rotation_by_vs, uint8_t(i)) + "}";
  }
  for (size_t i = 1; i < size_t(capture::LightReason::kCount); ++i) {
    if (!st.light_missing[i]) continue;
    text += std::string(" | ") + capture::LightReasonName(capture::LightReason(i)) + " " +
            std::to_string(st.light_missing[i]) + " by vs {" + by_vs(st.light_missing_by_vs, uint8_t(i)) + "}";
  }
  bool refused = false;
  for (size_t i = 1; i < size_t(LightSecond::kCount); ++i) {
    if (!st.light_second[i]) continue;
    text += std::string(refused ? ", " : " | probe or instance buffer refused: ") + LightSecondName(LightSecond(i)) + " " +
            std::to_string(st.light_second[i]);
    refused = true;
  }
  if (st.light_bend) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), " | bend inputs: %u records, %.1f MB", st.light_bend,
                  double(st.light_bend_bytes) / (1024.0 * 1024.0));
    text += buf;
  }
  return text;
}

// The log's and F3's Light line: the switch (and, where the frame was drawn
// without light although the switch is on, why: the texture path is off),
// whether the frame had a light
// block the pass could use, the drawn records lit by their family's function
// (by family), the drawn records without light by reason, the pixel shaders of
// those unlit for a reason of their own (light-unknown and normal-unsupported
// together, top eight), and the frame's sun.
inline std::string FormatLightDrawnText(const ClayStats& st) {
  constexpr size_t kTop = 8;
  std::string text = std::string("Light: ") + (st.light_switch ? "on" : "off");
  if (!st.light_switch && st.light_textures_off) text += " (the switch is on; textures are off)";
  if (st.light_switch) text += std::string(", block ") + (st.light_block ? "valid" : "missing");
  uint32_t lit = 0;
  for (uint32_t n : st.light_drawn) lit += n;
  text += ", lit " + std::to_string(lit) + " (";
  for (size_t i = 1; i < size_t(capture::LightFamily::kCount); ++i) {
    text += std::string(i > 1 ? ", " : "") + capture::LightFamilyName(capture::LightFamily(i)) + " " +
            std::to_string(st.light_drawn[i]);
  }
  text += ")";
  uint32_t unlit = 0;
  for (uint32_t n : st.light_unlit) unlit += n;
  text += ", unlit " + std::to_string(unlit);
  if (unlit) {
    text += " {";
    bool first = true;
    for (size_t i = 0; i < size_t(ClayUnlit::kCount); ++i) {
      if (!st.light_unlit[i]) continue;
      text += std::string(first ? "" : ", ") + ClayUnlitName(ClayUnlit(i)) + ": " + std::to_string(st.light_unlit[i]);
      first = false;
    }
    text += "}";
  }
  // Both reasons of a record's own in one list, most first.
  capture::ShaderTally<32>::Entry top[2 * kTop];
  size_t n = st.light_unlit_by_ps.Top(uint8_t(ClayUnlit::kUnknown), top, kTop);
  n += st.light_unlit_by_ps.Top(uint8_t(ClayUnlit::kNormal), top + n, kTop);
  std::stable_sort(top, top + n, [](const auto& a, const auto& b) { return a.count > b.count; });
  if (n) {
    text += ", unknown {";
    char buf[48];
    for (size_t i = 0; i < n && i < kTop; ++i) {
      std::snprintf(buf, sizeof(buf), "%s0x%016llX: %u", i ? ", " : "", static_cast<unsigned long long>(top[i].hash),
                    top[i].count);
      text += buf;
    }
    text += "}";
  }
  if (st.light_block) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), ", sun (%.3f, %.3f, %.3f) colour (%.3f, %.3f, %.3f)", st.light_sun_dir[0],
                  st.light_sun_dir[1], st.light_sun_dir[2], st.light_sun_col[0], st.light_sun_col[1], st.light_sun_col[2]);
    text += buf;
  }
  return text;
}

// The log's breakdown of alpha-unknown and blend-unknown by pixel shader (top
// eight each); empty when there is neither.
inline std::string FormatUnknownByPs(const ClayStats& st) {
  constexpr size_t kTop = 8;
  std::string text;
  const std::pair<uint8_t, const char*> kinds[] = {{kTallyAlphaUnknown, "alpha-unknown"},
                                                   {kTallyBlendUnknown, "blend-unknown"}};
  for (const auto& [kind, name] : kinds) {
    capture::ShaderTally<32>::Entry top[kTop];
    const size_t n = st.unknown_by_ps.Top(kind, top, kTop);
    if (!n) continue;
    text += std::string(text.empty() ? "" : " ") + name + " by ps {";
    char buf[48];
    for (size_t i = 0; i < n; ++i) {
      std::snprintf(buf, sizeof(buf), "%s0x%016llX: %u", i ? ", " : "",
                    static_cast<unsigned long long>(top[i].hash), top[i].count);
      text += buf;
    }
    text += "}";
  }
  return text;
}

// Five F3 lines: capture/draw counts with the top three skip reasons (records
// the pass left out as blend-unknown, and terrain layers left out with their
// square as terrain-square, among them), the geometry cache counters
// and GPU-thread CPU timings, the state line, the guest-thread capture time of
// the scene's frame, then the texture line (tex nullptr: the texture path is
// off).
inline std::string FormatStatusText(const FrameScene& scene, const ClayStats& st,
                                    const TextureStats* tex) {
  std::vector<std::pair<std::string, uint64_t>> reasons;
  for (size_t i = 0; i < size_t(capture::SkipReason::kCount); ++i) {
    uint64_t n = scene.skipped[i];
    if (i == size_t(capture::SkipReason::kBadIndex)) n += st.skipped_bad_index;
    if (n) reasons.emplace_back(capture::SkipReasonName(capture::SkipReason(i)), n);
  }
  if (st.skipped_other) reasons.emplace_back("render-other", st.skipped_other);
  if (st.blend_unknown) reasons.emplace_back("blend-unknown", st.blend_unknown);
  if (st.terrain_skipped) reasons.emplace_back("terrain-square", st.terrain_skipped);
  uint64_t skipped = 0;
  for (const auto& r : reasons) skipped += r.second;
  std::stable_sort(reasons.begin(), reasons.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  std::string text = "Native: captured " + std::to_string(scene.captured) + ", drawn " +
                     std::to_string(st.drawn) + " (deformed " + std::to_string(st.deformed) +
                     "), skipped " + std::to_string(skipped);
  if (!reasons.empty()) {
    text += " (top:";
    for (size_t i = 0; i < reasons.size() && i < 3; ++i) {
      text += (i ? ", " : " ") + reasons[i].first + " " + std::to_string(reasons[i].second);
    }
    text += ")";
  }
  char geo[256];
  std::snprintf(geo, sizeof(geo),
                "\nGeometry: %u uploads, %u hits, %.1f MB resident, instanced %u, skinned %u | "
                "hash %.2f ms, decode %.2f ms, record %.2f ms",
                st.uploads, st.hits, double(st.resident_bytes) / (1024.0 * 1024.0), st.instanced,
                st.skinned, st.hash_ms, st.decode_ms, st.record_ms);
  char cap[64];
  std::snprintf(cap, sizeof(cap), "\nCapture: %.2f ms guest time per frame\n", scene.capture_ms);
  return text + geo + "\n" + FormatStateText(st) + cap +
         (tex ? FormatTextureText(*tex, st.drawn, st.alpha_unknown, st.blend_unknown) : std::string("Textures: off"));
}

}  // namespace fable2::native::render
