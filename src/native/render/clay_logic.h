#pragma once

// Pure helpers for the clay pass and its geometry cache (no SDK/GPU deps):
// cache keys, vertex counts, index range checks, draw colors, the root
// constant block, pipeline keys and the alpha rule of a draw's render state,
// the terrain material (its constants, the fallback of a ground square),
// texture counters and the F3 status text.

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
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
#include "wind_decode.h"

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
// Why a tree occluder's twin is not drawn into the sun layer's depth ("The tree occluders' redraw", below;
// counted on the Sun line). kState: the twin's state words were not read, it has no alpha-to-mask (the
// occluder's kept set is not known then) or its alpha test's reference is no number. kAlpha: its material
// has not the albedo's alpha, or a scale or coordinate transform that is no number. kTexture: its
// coordinates or texture are not there this frame (never drawn solid). kRecord: the twin is not among the
// frame's drawn records. kPipeline: no pipeline of its vertex program (a twin drawn moved in the scene is
// never drawn at rest in the layer).
enum class SunTreeSkip : uint8_t { kNone, kState, kAlpha, kTexture, kRecord, kPipeline, kCount };
inline const char* SunTreeSkipName(SunTreeSkip s) {
  switch (s) {
    case SunTreeSkip::kState: return "state";
    case SunTreeSkip::kAlpha: return "alpha";
    case SunTreeSkip::kTexture: return "texture";
    case SunTreeSkip::kRecord: return "record";
    case SunTreeSkip::kPipeline: return "pipeline";
    default: return "none";
  }
}

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
// position is rebuilt (`view` says which, as for the mask). kDrawn: the frame is fogged (its lit
// draws by their family's form, its unlit draws by DrawFogOf; the sky goes with it). The three in
// the middle are the frames counted "fog-block".
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

// The rules of the sky that are not the fog's ("Native sky" below; review of plan Task 3, I1: they
// were taken out of the fog block so that a night or an interior keeps its fog, and are judged
// where the sky is decided). kHeight: c67.y, the sky's path height, is below 0. kExtinction: a
// component of c64 or c65 is below 0 (the sky's exp(-(l1 * c64 + l2 * c65)) would grow with the
// path). kPath: a zenith path c31.x or c46.x below 0 or beyond capture::kFogRegisterLimit (the sky
// block bounds neither from above, and a product downstream could overflow). kBlend: c47.x outside
// 0..1. kValue: a value that is no number, or beyond the limit. A frame one of them fails in has no
// sky and is counted "sky-block" with the rule.
enum class SkyRule : uint8_t { kNone, kValue, kHeight, kExtinction, kPath, kBlend, kCount };
// The plan's reason string of a sky drawn without its texture term.
inline constexpr const char* kSkyTextureReason = "sky-texture";
inline const char* SkyRuleName(SkyRule r) {
  switch (r) {
    case SkyRule::kNone: return "none";
    case SkyRule::kValue: return "value";
    case SkyRule::kHeight: return "height";
    case SkyRule::kExtinction: return "extinction";
    case SkyRule::kPath: return "path";
    case SkyRule::kBlend: return "blend";
    default: return "?";
  }
}

// What the pass did about the frame's sky ("Native sky" below; ClayStats::sky; the Sky line). The
// sky goes with the fog (one switch, fable2_native_fog) and reads the fog's tail of the frame's
// constants (the in-scatter table, the axis) and the lit draws' exposure, so a frame without fog
// has no sky. kOff: the switch is off. kNoLight: the frame is drawn without light. kNoFog: the
// frame is not fogged for want of a fog block or of usable values ("fog-block"). kView: the frame
// has no single view, or its view-projection gives no ray (`view` says which). kBlock: the frame
// has no valid sky block, whatever the capture's reason. kRules: the blocks hold a value the sky's
// own rules refuse (SkyRule). kObjects: the sky's GPU objects could not be made (off until the
// pass is rebuilt), or the frame has no constant buffer for the step's constants. kDrawn: the sky step is drawn. In every case but the last the pixels nothing
// is drawn at keep the clear colour, as before the sky.
enum class ClaySky : uint8_t { kOff, kNoLight, kNoFog, kView, kBlock, kRules, kObjects, kDrawn, kCount };

// The pass's decision about the frame's sky (ClayPass::Render), one chain in which the first thing
// missing decides. `fog`, `fog_view`: the frame's verdict about its fog (DecideClayFog: the switch
// as the pass reads it now, the light, the fog block, its values, the eye, the one view, the
// filled tail). `block`: FrameScene::sky is valid. `rule`: SkyRules of the two blocks. `ray`: the
// frame's view-projection gives the view rays (SkyRayBasis). `constants`: the frame's constant
// buffer was made and the sky's constants could be made of the blocks (MakeClaySkyConstants).
// `objects`: the sky's GPU objects exist or may still be made; false once making them has failed.
struct ClaySkyInputs {
  ClayFog fog = ClayFog::kOff;
  FrameViewReason fog_view = FrameViewReason::kNone;
  bool block = false;
  SkyRule rule = SkyRule::kNone;
  bool ray = false, constants = false, objects = false;
};
struct ClaySkyDecision {
  ClaySky sky = ClaySky::kOff;
  FrameViewReason view = FrameViewReason::kNone;  // with kView: why
  SkyRule rule = SkyRule::kNone;                  // with kRules: which
};
constexpr ClaySkyDecision DecideClaySky(const ClaySkyInputs& in) {
  switch (in.fog) {
    case ClayFog::kDrawn: break;
    case ClayFog::kNoLight: return {ClaySky::kNoLight, FrameViewReason::kNone, SkyRule::kNone};
    case ClayFog::kBlock:
    case ClayFog::kValues: return {ClaySky::kNoFog, FrameViewReason::kNone, SkyRule::kNone};
    case ClayFog::kView: return {ClaySky::kView, in.fog_view, SkyRule::kNone};
    default: return {ClaySky::kOff, FrameViewReason::kNone, SkyRule::kNone};  // (kOff, and anything that is no verdict)
  }
  if (!in.block) return {ClaySky::kBlock, FrameViewReason::kNone, SkyRule::kNone};
  if (in.rule != SkyRule::kNone) return {ClaySky::kRules, FrameViewReason::kNone, in.rule};
  if (!in.ray) return {ClaySky::kView, FrameViewReason::kSingular, SkyRule::kNone};
  // (the rules passed and the ray was found, so what is missing is the frame's constant buffer:
  // "no objects", not a refused value; review of plan Task 5, M3)
  if (!in.constants) return {ClaySky::kObjects, FrameViewReason::kNone, SkyRule::kNone};
  if (!in.objects) return {ClaySky::kObjects, FrameViewReason::kNone, SkyRule::kNone};
  return {ClaySky::kDrawn, FrameViewReason::kNone, SkyRule::kNone};
}

// Where the sky step goes among the colour pass's records ("Native sky" below). The pass draws its
// records in the scene's order, which is the game's: the opaque group, the terrain layers (blended
// onto their own ground, with depth write), then the game's sky draw, then the blended group. The
// sky is drawn BEFORE the first drawn record that blends and is no terrain record, or after the
// last record when there is none. It is depth-tested against the clear value without depth write,
// so by its place alone:
//   - no record at or after its place can be covered: those are drawn later, over it;
//   - every blended record that is no terrain record is at or after its place;
//   - a record before its place is covered only at pixels where the depth is still the clear value
//     when the sky is drawn, which a record that writes depth never leaves behind (a fragment drawn
//     exactly at the far plane apart: it is the clear value). `before_no_depth` counts the drawn
//     records before the place that write no depth: where nothing else wrote depth under them the
//     sky is drawn over them, as the game's sky is over the same draws.
// `drawn`: the record is drawn this frame (it has a pipeline). `terrain`: a terrain record.
// `blend`, `depth_write`: of its pipeline's key (depth_write: depth test and write, InPrepass).
struct SkyOrderItem {
  bool drawn = false, terrain = false, blend = false, depth_write = false;
};
struct SkyOrder {
  size_t place = 0;              // the sky is drawn before the record of this index; the count: after all
  uint32_t before = 0;           // drawn records before the place
  uint32_t before_no_depth = 0;  // those of them that write no depth
  // A drawn terrain record lies at or after the place: the sky is drawn earlier than the game
  // draws its own (a blended record that is no terrain record came before ground). Harmless by the
  // first point above, and counted, with the pixel shader of the record at the place, so that the
  // cause can be read (review of plan Task 5, M1).
  bool early = false;
};
constexpr SkyOrder SkyPlace(const SkyOrderItem* items, size_t count) {
  SkyOrder o;
  o.place = count;
  for (size_t i = 0; i < count; ++i) {
    if (!items[i].drawn) continue;
    if (items[i].blend && !items[i].terrain) {
      o.place = i;
      break;
    }
    ++o.before;
    if (!items[i].depth_write) ++o.before_no_depth;
  }
  for (size_t i = o.place; i < count; ++i) {
    if (items[i].drawn && items[i].terrain) o.early = true;
  }
  return o;
}

// What the pass did about the frame's post chain ("Native post" below; ClayStats::post; the Post line).
// A frame is either a POST FRAME (kDrawn: its lit draws write the game's own scale c20.w, its unlit
// draws the frame's unlit scale, and the game's composite and the scan-out ramp are drawn in place of
// the output mapping) or TODAY'S FRAME (everything else: the fixed exposure, the unlit word 1, the
// output mapping as it was), never a mixture. kOff: fable2_native_post is off. kNoLight: the frame is
// drawn without light (the light's switch, the textures, no light block). kBlock: the frame has no
// post block the pass can use (none in the scene, whatever the capture's reason, one older than
// capture::kPostBlockMaxAge frames, one that does not pair with the frame's c20.w, or one whose values
// the pass will not hand a shader): "post-block". kRamp: no scan-out ramp, or one that fails its rule:
// "post-ramp". kObjects: the post steps' GPU objects could not be made (off until the pass is rebuilt):
// "post-objects". kBuffer: the frame has no constant buffer (nothing of it is drawn, or the buffer could
// not be made: then no record is drawn at all).
inline constexpr const char* kPostRampReason = "post-ramp";
inline constexpr const char* kPostObjectsReason = "post-objects";
enum class ClayPost : uint8_t { kOff, kNoLight, kBlock, kRamp, kObjects, kBuffer, kDrawn, kCount };

// The pass's decision about the frame's post (ClayPass::Render), one chain in which the first thing
// missing decides; made before the frame's exposure and unlit word are written, and once more when the
// frame's constant buffer is known. `post`: fable2_native_post as the pass reads it now. `light`: the
// light's switch (and the textures). `lit`: the frame's constants are lit (MakeClayFrameConstants).
// `block`: the scene's post block is valid, within the age bound, pairs with the frame's c20.w, and the
// step's constants and the frame's two words could be made of it (MakeClayPostConstants,
// SetClayFramePost). `ramp`: a scan-out ramp was handed over and passes its rule. `objects`: the post
// steps' objects exist or may still be made; false once making them has failed. `buffer`: the frame's
// constant buffer was made.
struct ClayPostInputs {
  bool post = false, light = false, lit = false, block = false, ramp = false, objects = false, buffer = false;
};
struct ClayPostDecision {
  ClayPost post = ClayPost::kOff;
};
constexpr ClayPostDecision DecideClayPost(const ClayPostInputs& in) {
  if (!in.post) return {ClayPost::kOff};
  if (!in.light || !in.lit) return {ClayPost::kNoLight};
  if (!in.block) return {ClayPost::kBlock};
  if (!in.ramp) return {ClayPost::kRamp};
  if (!in.objects) return {ClayPost::kObjects};
  if (!in.buffer) return {ClayPost::kBuffer};
  return {ClayPost::kDrawn};
}
// What ClayPass::Render is asked about the post chain: the three switches as they are read now (sun:
// plan Task 14; nothing reads it before) and the scan-out ramp of this frame, 256
// entries of red, green, blue in 0..1 (native_render_state.h, PostRamp::rgb), null when there is none
// or it failed its rule.
struct ClayPostRequest {
  bool post = false, bloom = false, sun = false;
  const float (*ramp)[3] = nullptr;
  // The pass was not rendered for a while before this Render (the native view was off, or there was no
  // scene): what its front targets hold is not the previous frame's picture (ClayFrontAfterGap).
  bool gap = false;
};
// Why the pass cannot use the scene's post block (ClayStats::post_fault; the Post line's word behind
// "post-block" when the scene has a valid block: review of plan Task 5, Minor 11). kMissing: the scene
// has none (the capture's reason says why). kAge: it was read more than capture::kPostBlockMaxAge
// frames ago. kPairing: the frame has no light block, or the block's bright pass does not pair with
// the frame's c20.w (capture::PostBlockPairs). kValues: MakeClayPostConstants refused a register or a
// texel. kWords: SetClayFramePost refused the frame's two words (the scale or the unlit scale).
enum class ClayPostFault : uint8_t { kNone, kMissing, kAge, kPairing, kValues, kWords, kCount };
constexpr const char* ClayPostFaultName(ClayPostFault f) {
  switch (f) {
    case ClayPostFault::kAge: return "age";
    case ClayPostFault::kPairing: return "pairing";
    case ClayPostFault::kValues: return "values";
    case ClayPostFault::kWords: return "words";
    default: return "none";
  }
}
// The first three rules, in the order the pass asks them (the block; its age; the pairing).
inline ClayPostFault PostBlockFault(const capture::PostBlock& b, uint32_t age, bool light_valid, float out_scale) {
  if (!b.valid) return ClayPostFault::kMissing;
  if (age > capture::kPostBlockMaxAge) return ClayPostFault::kAge;
  if (!light_valid || !capture::PostBlockPairs(b, out_scale)) return ClayPostFault::kPairing;
  return ClayPostFault::kNone;
}
// The bloom of a frame ("Native bloom" below; ClayStats::bloom; counted on the Post line). kOn: a post
// frame with fable2_native_bloom on whose steps' constants carry the blurs' weights
// (ClayPostConstants::bloom_on): the four bloom steps are drawn and the composite reads their result.
// kNoWeights: a post frame with the switch on whose block has no blur part (capture::kPostPartBlur):
// drawn without bloom, the composite alone, and counted. kOff: everything else (the switch off, or no
// post frame): the picture of plan Task 5. kNoObjects: the verdict would be kOn but the bloom's objects do
// not exist (ClayBloomWithObjects: not reachable while a frame whose bloom objects failed is no post
// frame at all, "post-objects"; a word of its own so that such a frame, should the rule change, is not
// counted "no-weights"): drawn without bloom.
enum class ClayBloom : uint8_t { kOff, kNoWeights, kOn, kNoObjects, kCount };
constexpr ClayBloom DecideClayBloom(bool post_frame, bool wanted, bool weights) {
  if (!post_frame || !wanted) return ClayBloom::kOff;
  return weights ? ClayBloom::kOn : ClayBloom::kNoWeights;
}
constexpr ClayBloom ClayBloomWithObjects(ClayBloom b, bool objects) {
  return b == ClayBloom::kOn && !objects ? ClayBloom::kNoObjects : b;
}
constexpr const char* ClayBloomName(ClayBloom b) {
  return b == ClayBloom::kOn ? "on" : b == ClayBloom::kNoWeights ? "no-weights" : b == ClayBloom::kNoObjects ? "no-objects" : "off";
}
// The scene's furniture of a frame ("Native furniture" below; ClayStats::furniture; the Furniture line).
// kDrawn: the frame's quads of the kinds this build draws are drawn. Every other verdict: none of
// them is. kOff: fable2_native_sun is off. kNoPost: the frame is no post frame. kNoSky: it has no
// sky. kNoQuads: its scene has no furniture call. kRecords: its scene's quad set is not whole
// ("quad-record": a call refused, more than the bound, a record that did not arrive, or a scene
// captured without the quads). kState: a record of a kind this build draws cannot be drawn as it was
// captured (its state, a value, another frame's scale). kObjects: the quad step's objects could not
// be made ("post-objects").
enum class ClayFurniture : uint8_t { kOff, kNoPost, kNoSky, kNoQuads, kRecords, kState, kObjects, kDrawn, kCount };

// What the pass did about the frame's sun layer (ClayStats::sun; the Sun line). kDrawn: the layer's
// depth, its two pictures, the reductions and the radial blur were drawn and the bright pass read them.
// Every other verdict: nothing of the layer is drawn and the bright pass's sun flag is 0, the picture of
// the bloom alone. kOff: fable2_native_sun is off. kNoFurniture: the frame's furniture is not drawn (no
// post frame, no sky, no whole set of quad records, a scene record that cannot be drawn, the quad
// step's objects: the Furniture line says which). kNoBloom: the frame's bloom is not on (the bright
// pass, which the layer feeds, is not drawn). kNoRadial: the post block has no radial blur's registers,
// or they fail their bounds. kNoQuads: the set has no record of the layer. kState: a layer record
// cannot be drawn as it was captured. kObjects: the layer's objects could not be made ("post-objects").
// kTexture: a layer quad's texture could not be had this frame (the first frames after a view switch:
// the upload budget): the layer is all of its quads or none, since one without an occluder is worse
// than none. kDepthPlace: where the layer's depth is to be taken is not known for this frame (a tree
// occluder of the game's layer named no single record of the scene: PlanSunDepthPlace); a layer whose depth
// held what the game's does not would lose its beams, so none is drawn.
enum class ClaySun : uint8_t { kOff, kNoFurniture, kNoBloom, kNoRadial, kNoQuads, kState, kObjects, kTexture, kDepthPlace, kDrawn, kCount };
// What the pass did about the scene's clouds of a frame (ClayStats::clouds; "Native clouds" below; the
// Clouds line). kDrawn: every cloud record of the frame's set was drawn. Every other verdict: no cloud is
// drawn (the clouds are all of the set's or none; the moon and the glare keep their own verdict). kOff: the
// frame's furniture is not drawn (the switch off, no post frame, no sky, no whole set: the Furniture line
// says which). kNone: the set has no cloud record. kState: a cloud record cannot be drawn as it was
// captured (its state or a value). kObjects: the clouds' objects could not be made ("post-objects").
// kTexture: a cloud's texture could not be had this frame ("quad-texture"). kFeed: the oracle feed's sun
// part is drawn this frame, and its quads hold the slots the scene's quads were given: no cloud is drawn
// (review of plan Tasks 16 and 17, Minor 8: the line read "drawn 3" in such frames).
enum class ClayClouds : uint8_t { kOff, kNone, kState, kObjects, kTexture, kDrawn, kFeed, kCount };
struct ClayCloudFrames {
  uint64_t frames = 0, by[size_t(ClayClouds::kCount)] = {};
};
inline void CountClayCloudFrame(ClayClouds c, ClayCloudFrames& n) {
  if (c == ClayClouds::kOff || size_t(c) >= size_t(ClayClouds::kCount)) return;
  ++n.frames;
  ++n.by[size_t(c)];
}
// What the pass does about one record's wind ("Native wind" below; DecideClayWind; the Wind line).
// kDrawn: the record is moved, in every pass that draws it, by the wind vertex program. Every other
// verdict: it is today's record at rest, by today's program, in every pass. kOff: the frame has no
// wind (the switch off, or a scene captured without it). kNone: the record has no wind inputs (its
// shader does not sway, or the capture refused it and counted why: capture::WindReason); a terrain
// record never has. kBuffer: its wind words could not be had for every vertex it draws. kPipeline:
// the wind program or a pipeline of it for the record's state could not be made (the colour pass's,
// or the depth-only one of the pre-pass or of a shadow map). kSlot: no slot of the frame's
// constant buffer was left for its wind constants.
enum class ClayWind : uint8_t { kOff, kNone, kBuffer, kPipeline, kSlot, kDrawn, kCount };
constexpr const char* ClayWindName(ClayWind w) {
  switch (w) {
    case ClayWind::kOff: return "off";
    case ClayWind::kNone: return "none";
    case ClayWind::kBuffer: return "buffer";
    case ClayWind::kPipeline: return "pipeline";
    case ClayWind::kSlot: return "slot";
    case ClayWind::kDrawn: return "drawn";
    default: return "?";
  }
}
// What the pass does about one record's coverage ("Coverage" below; DecideClayCoverage; the Coverage line).
// kOff: fable2_native_coverage is off: nothing of the coverage is made and every record is today's. kNone:
// no coverage record that is drawn with its texture (CoverageOf, CoverageDrawable): today's record, and no
// reason. kFull: a LATE record whose scale lets a pixel have both samples covered: today's record (none is
// captured: every late record is at 0.5; whether such a record belongs in the pre-pass is not settled).
// kPipeline: a pipeline of its state could not be made (the half one; an early record's full one): today's
// record, in both passes. kDrawn: a late record's half-covered pixels at one half over what the target
// holds, at its own place, and nothing of it in the pre-pass; an early record's fully covered pixels in
// place, its half-covered ones at one half in the tail (or in place: CoverageHalfInTail), and the pre-pass
// as it is today.
inline constexpr const char* kCoverageReason = "coverage";
enum class ClayCoverage : uint8_t { kOff, kNone, kFull, kPipeline, kDrawn, kCount };
constexpr const char* ClayCoverageName(ClayCoverage c) {
  switch (c) {
    case ClayCoverage::kOff: return "off";
    case ClayCoverage::kNone: return "none";
    case ClayCoverage::kFull: return "full";
    case ClayCoverage::kPipeline: return "pipeline";
    case ClayCoverage::kDrawn: return "drawn";
    default: return "?";
  }
}
// The pass's frame counts of the sun layer, by verdict: one frame added. Counted: every frame with the
// sun's switch on, which is every verdict but kOff.
struct ClaySunFrames {
  uint64_t frames = 0, by[size_t(ClaySun::kCount)] = {};
};
inline void CountClaySunFrame(ClaySun s, ClaySunFrames& n) {
  if (s == ClaySun::kOff || size_t(s) >= size_t(ClaySun::kCount)) return;
  ++n.frames;
  ++n.by[size_t(s)];
}

// The water's verdict for a frame ("Native water" below; frame-map section 20; spec Design 5): kDrawn, or the first
// reason DecideClayWater finds, in kWaterDecisionOrder (this order, but `view` before `plane`). Declared here
// because ClayStats carries it.
enum class ClayWater : uint8_t { kDrawn, kOff, kNoRecords, kLight, kOpaqueLive, kPair, kPlane, kView, kReflection, kTexture,
                                 kOrder, kPipeline, kSlot, kScale, kCount };
inline const char* ClayWaterName(ClayWater w) {
  static constexpr const char* kNames[] = {"drawn", "off", "no-records", "light", "opaque-live", "pair", "plane", "view", "reflection",
                                           "texture", "order", "pipeline", "slot", "scale"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == size_t(ClayWater::kCount));
  return w < ClayWater::kCount ? kNames[size_t(w)] : "?";
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
  // pipeline_variants: of `pipelines`, those of the wind's and the coverage's own cache (VariantKey);
  // pipeline_refused: the keys that cache refused this frame because it was full (the record is then
  // drawn at rest or whole, by its ordinary pipeline, and counted on the Wind or the Coverage line too).
  uint32_t pipelines = 0, pipeline_overflow = 0;
  uint32_t pipeline_variants = 0, pipeline_refused = 0;
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
  // The wind words of the frame's wind records (GeometryCache::WindWords counts them itself): the
  // calls that were given a buffer and the bytes of those buffers (a buffer two records share is
  // counted twice), and the calls that were given none, by reason (index: WindBuild).
  uint32_t wind_words = 0;
  uint64_t wind_words_bytes = 0;
  uint32_t wind_build[size_t(WindBuild::kCount)] = {};
  // The wind as drawn ("Native wind" below; the Wind line). wind_switch: fable2_native_wind as the
  // Render read it; wind_scene: the scene was captured with the wind (FrameScene::wind_records): only
  // with both is anything moved. wind_moved: the scene's records drawn moved, by family (index:
  // capture::WindFamily; counted at the colour draw). wind_casters: the shadow records drawn moved
  // (both maps). wind_rest: records and casters that have wind inputs and are drawn at rest, by the
  // pass's reason (index: ClayWind; kBuffer, kPipeline, kSlot), counted when they are prepared.
  // wind_capture_rest: the frame's draws the capture gave no inputs for a fault of their own, scene
  // and shadow passes together (index: capture::WindReason, the reasons past kNoEntry).
  bool wind_switch = false, wind_scene = false;
  uint32_t wind_moved[size_t(capture::WindFamily::kCount)] = {};
  uint32_t wind_casters = 0;
  uint32_t wind_rest[size_t(ClayWind::kCount)] = {};
  uint32_t wind_capture_rest[size_t(capture::WindReason::kCount)] = {};
  // The coverage as drawn ("Coverage" below; the Coverage line). coverage_switch: fable2_native_coverage as
  // the Render read it. coverage_late: the late records drawn at half (counted at the colour draw);
  // coverage_early: the early records drawn by their two parts; coverage_tail_draws: those of them whose
  // half-covered pixels were drawn in the tail (the others' in place). coverage_rest: coverage records drawn
  // as today's, by the pass's reason (index: ClayCoverage; kFull, kPipeline), counted when they are prepared.
  // coverage_sky_moved: the sky step was drawn before the frame's first blend of a coverage record, earlier
  // than its own place (SkyPlace); coverage_sky_over: the drawn records between the two places that are no
  // coverage records, which are now drawn after the sky step, the moon and the clouds;
  // coverage_sky_over_no_depth: those of them that write no depth (today the sky covers them where nothing
  // else wrote depth; moved, they are drawn over it).
  bool coverage_switch = false, coverage_sky_moved = false;
  uint32_t coverage_late = 0, coverage_early = 0, coverage_tail_draws = 0;
  // The half parts' depth steps drawn this frame (ruling 60: one before each half part's colour step).
  uint32_t coverage_depth_steps = 0;
  uint32_t coverage_rest[size_t(ClayCoverage::kCount)] = {};
  uint32_t coverage_sky_over = 0, coverage_sky_over_no_depth = 0;
  // The impostors as drawn ("Native impostors" below; the Impostors line). impostor_mode: fable2_native_impostors
  // as the Render read it, in a scene captured with it (ClayImpostorMode); kOff: every count is 0. impostors_seen:
  // the game's calls of the pair in the frame (FrameScene::impostor_seen); impostors_drawn, impostor_quads: the
  // records the step drew and their quads; impostor_skip: the frame's records it did not draw, by the pass's
  // reason (index: ClayImpostorSkip as a number); impostor_capture: the calls that gave no record, by the
  // capture's reason (index: capture::ImpostorReason; FrameScene::impostor_refused).
  capture::ImpostorMode impostor_mode = capture::ImpostorMode::kOff;
  uint32_t impostors_seen = 0, impostors_drawn = 0, impostor_quads = 0;
  uint32_t impostor_skip[16] = {};
  // (`on` alone) impostor_pictures: the game's pictures the frame's records could use (read whole, a tree's by
  // the content rule, in a texture); impostor_refreshed: those of them decoded and uploaded this frame (the
  // others' sampled bytes were the ones their texture was made from).
  uint32_t impostor_pictures = 0, impostor_refreshed = 0;
  // (`on` alone) the frame's pictures were taken from the diagnostic feed's file (fable2_native_impostor_feed),
  // not from the running game's guest memory: the Impostors line says "on (feed)" (ledger ruling 47).
  bool impostor_feed = false;
  // (the CPU time of reading, hashing, decoding and staging them this frame, for the log)
  double impostor_picture_ms = 0.0;
  uint32_t impostor_capture[size_t(capture::ImpostorReason::kCount)] = {};
  // The water as drawn ("Native water" below; the Water line). water: the frame's verdict (kOff: the switch off,
  // or a scene captured without it; every count is then 0). water_seen: the game's surface draws in the frame
  // (FrameScene::water_seen[0]); water_drawn: the surface records the step drew; water_targets: the reflection
  // targets it drew them with; water_left_out: the opaque twins left out of the pre-pass and the colour pass
  // (only in a drawn frame); water_frames: the frames since the start by verdict; water_capture: the draws that
  // gave no record, by the capture's reason (index: capture::WaterReason; FrameScene::water_refused); water_ms:
  // the step's own CPU time this frame, for the log.
  ClayWater water = ClayWater::kOff;
  uint32_t water_seen = 0, water_drawn = 0, water_targets = 0, water_left_out = 0;
  uint32_t water_frames[size_t(ClayWater::kCount)] = {};
  uint32_t water_capture[size_t(capture::WaterReason::kCount)] = {};
  double water_ms = 0.0;
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
  // drawn without fog in any frame. The drawn records of a fogged frame by their fog (DrawFogOf,
  // CountDrawFog): fog_unlit: records without light that were fogged ("Unlit draws" below),
  // fog_unlit_alpha: those of them with the blended form's alpha scale, fog_unlit_tree: those of
  // them that are trunks or leaves, in the trees' form with their own scale; fog_unlit_tree_none:
  // unlit trunks and leaves left unfogged for want of a usable scale; fog_unlit_none: other records
  // without light left unfogged, their pixel shader not being in the fog table; fog_lit_none: LIT
  // records left unfogged because their own pixel shader in the game has no fog (final review, I1).
  // fog_view_outliers: records whose matrix was not the frame's
  // view-projection (FrameView::outliers) in a fogged frame: their pixels are fogged with the
  // majority's inverse, a wrong position (review of plan Task 4, M3).
  // The frame counts are the pass's totals since it was made, this frame included: frames drawn
  // with the fog's switch on, and those of them not fogged for want of a block, for a value, for a
  // view ("fog-block" is the three together) or for want of light ("no-light": the light's switch
  // or the texture path off, no light block, no constant buffer; final review, M5).
  bool fog_switch = false, fog_block = false;
  capture::FogBlockReason fog_reason = capture::FogBlockReason::kOff;
  ClayFog fog = ClayFog::kOff;
  FrameViewReason fog_view = FrameViewReason::kNone;
  uint32_t fog_trees = 0, fog_trees_none = 0;
  uint32_t fog_unlit = 0, fog_unlit_alpha = 0, fog_unlit_tree = 0, fog_unlit_tree_none = 0, fog_unlit_none = 0;
  uint32_t fog_lit_none = 0, fog_view_outliers = 0;
  uint64_t fog_frames = 0, fog_block_frames = 0, fog_values_frames = 0, fog_view_frames = 0, fog_nolight_frames = 0;
  // The sky ("Native sky" below; the Sky line). sky_block, sky_reason: the scene's sky block and,
  // without one, the capture's reason. sky, sky_view, sky_rule: what the pass did about it
  // (ClaySky), and with kView or kRules why. sky_textured: the drawn sky has the texture term (the
  // game's two sky textures, resolved by the texture cache); sky_texture_missing: it is the
  // clear-sky term alone because a texture the shader would read could not be had ("sky-texture").
  // sky_before, sky_before_no_depth: the drawn records before the sky's place and those of them
  // that write no depth (SkyPlace). sky_place_record, sky_place_ps: the place is a record (not the
  // end of the list), and that record's pixel shader. sky_early_frames: frames whose sky was drawn
  // before a drawn terrain record (SkyOrder::early). sky_ray, sky_axis_cos: of a drawn sky, the view ray of the
  // picture's centre and its cosine to the fog's axis c66 (1: the view looks along the axis, where
  // the in-scatter table is read at its far end). The frame counts are the pass's totals since it was made, this
  // frame included: frames with the fog's switch on, and those of them without a
  // sky by reason: no fog ("fog-block" frames and frames without light), no view, no valid sky
  // block or a refused value ("sky-block" is the two together), no objects; and the frames whose
  // sky was drawn without its textures ("sky-texture").
  bool sky_block = false, sky_textured = false, sky_texture_missing = false;
  capture::SkyBlockReason sky_reason = capture::SkyBlockReason::kOff;
  ClaySky sky = ClaySky::kOff;
  FrameViewReason sky_view = FrameViewReason::kNone;
  SkyRule sky_rule = SkyRule::kNone;
  uint32_t sky_before = 0, sky_before_no_depth = 0;
  bool sky_place_record = false;
  uint64_t sky_place_ps = 0;
  uint64_t sky_early_frames = 0;
  float sky_ray[3] = {}, sky_axis_cos = 0.0f;
  uint64_t sky_frames = 0, sky_nofog_frames = 0, sky_view_frames = 0, sky_block_frames = 0, sky_rules_frames = 0;
  uint64_t sky_objects_frames = 0, sky_texture_frames = 0;
  // The post chain ("Native post" below; the Post line). post_switch: fable2_native_post. post_block,
  // post_reason, post_age: the scene's post block, without one the capture's reason, and with one how
  // many frames ago it was read (0: the frame's own). post: what the pass did (ClayPost). post_scale,
  // post_unlit: of a post frame, the scale its lit draws wrote (the game's c20.w) and its unlit scale.
  // The frame counts are the pass's totals since it was made, this frame included: frames with the
  // switch on, those drawn as post frames, and those drawn as today's by reason.
  bool post_switch = false, post_block = false;
  capture::PostBlockReason post_reason = capture::PostBlockReason::kOff;
  ClayPost post = ClayPost::kOff;
  uint32_t post_age = 0;
  float post_scale = 0.0f, post_unlit = 1.0f;
  // (the share of the previous frame's picture in this frame's front target: kPostPreviousWeight or 0;
  // plan Task 15; on the Sun line)
  float post_previous = 0.0f;
  uint64_t post_frames = 0, post_drawn_frames = 0, post_nolight_frames = 0, post_block_frames = 0, post_ramp_frames = 0;
  uint64_t post_objects_frames = 0, post_buffer_frames = 0;
  // post_fault: with a valid block the pass could not use (post == kBlock), which of the pass's rules
  // refused it. post_coord: the composite's c46.x of the frame's block (kPostUnlitCoord, 0.25, in every
  // capture; the unlit scale is the gain at scene luminance 1 only with that value). bloom: the bloom of
  // this frame (ClayBloom). bloom_frames, bloom_noweights_frames: post frames since the pass was made
  // that were drawn with the bloom, and with the switch on but without the blurs' weights.
  ClayPostFault post_fault = ClayPostFault::kNone;
  float post_coord = 0.25f;
  ClayBloom bloom = ClayBloom::kOff;
  uint64_t bloom_frames = 0, bloom_noweights_frames = 0;
  // The scene's furniture ("Native furniture" below; the Furniture line). sun_switch:
  // fable2_native_sun. quad_set, quad_records: the scene's quad set and, when it is whole, its
  // records. furniture: what the pass did (ClayFurniture). Of a drawn frame: the quads drawn by kind,
  // those left out because their texture could not be had ("quad-texture"), and the set's records of
  // kinds this build does not draw. The frame counts are the pass's totals since it was made, this
  // frame included: frames with the switch on, those drawn (and of them those with a quad left out
  // for its texture), and those not drawn by reason.
  bool sun_switch = false;
  capture::QuadSetReason quad_set = capture::QuadSetReason::kOff;
  uint32_t quad_records = 0;
  ClayFurniture furniture = ClayFurniture::kOff;
  uint32_t quad_drawn_moon = 0, quad_drawn_glare = 0, quad_texture = 0, quad_later = 0;
  uint64_t furniture_frames = 0, furniture_drawn_frames = 0, furniture_texture_frames = 0, furniture_nopost_frames = 0;
  uint64_t furniture_nosky_frames = 0, furniture_noquads_frames = 0, furniture_records_frames = 0, furniture_state_frames = 0;
  uint64_t furniture_objects_frames = 0;
  // The sun layer ("Native sun layer" below; the Sun line). sun: what the pass did (ClaySun). Of a drawn
  // layer: its quads by target (0 the occluders into its depth, 1 layer 1, 2 layer 2) and the radial blur's
  // centre. sun_frames: the pass's totals since it was made, this frame included, by verdict.
  ClaySun sun = ClaySun::kOff;
  // The scene's clouds ("Native clouds" below; the Clouds line): what the pass did, the cloud records of
  // the frame's set, and the pass's frame counts by verdict.
  ClayClouds clouds = ClayClouds::kOff;
  uint32_t cloud_records = 0;
  ClayCloudFrames cloud_frames;
  uint32_t sun_drawn[3] = {};
  // The record of FrameScene::draws before which a drawn layer's depth was taken (PlanSunDepthPlace);
  // capture::kNoDepthPlace: after the scene's last record.
  uint32_t sun_depth_record = capture::kNoDepthPlace;
  // The tree occluders' redraw of a drawn layer ("The tree occluders' redraw"): the frame's twins
  // (FrameScene::layer_twin_count; 0 on the oracle feed, whose depth is the file's), those drawn into the
  // layer's depth, and the others by reason (index: SunTreeSkip).
  uint32_t sun_trees = 0, sun_trees_drawn = 0;
  uint32_t sun_tree_skipped[size_t(SunTreeSkip::kCount)] = {};
  float sun_centre[2] = {};
  ClaySunFrames sun_frames;
};
// The pass's frame counts of the post chain, which follow from the frame's verdict alone: one frame
// added. Counted: every frame with the post switch on, which is every verdict but kOff.
struct ClayPostFrames {
  uint64_t frames = 0, drawn = 0, nolight = 0, block = 0, ramp = 0, objects = 0, buffer = 0;
  uint64_t bloom = 0, noweights = 0;  // of the drawn ones: with the bloom; wanted, without the blurs' weights
};
inline void CountClayPostFrame(ClayPost post, ClayPostFrames& n) {
  if (post == ClayPost::kOff || size_t(post) >= size_t(ClayPost::kCount)) return;
  ++n.frames;
  if (post == ClayPost::kDrawn) ++n.drawn;
  if (post == ClayPost::kNoLight) ++n.nolight;
  if (post == ClayPost::kBlock) ++n.block;
  if (post == ClayPost::kRamp) ++n.ramp;
  if (post == ClayPost::kObjects) ++n.objects;
  if (post == ClayPost::kBuffer) ++n.buffer;
}
// The frame's bloom added to the counts (a frame's verdict is kOn or kNoWeights only in a post frame).
inline void CountClayBloomFrame(ClayBloom bloom, ClayPostFrames& n) {
  if (bloom == ClayBloom::kOn) ++n.bloom;
  if (bloom == ClayBloom::kNoWeights) ++n.noweights;
}
// The pass's frame counts of the sky, which follow from the frame's verdict alone: one frame added.
// Counted: every frame with the fog's switch on (the sky has no switch of its own), which is every
// verdict but kOff; a frame drawn without light is among them, under "no-fog" (final review, M5).
// `texture_missing`: a drawn sky without its texture term.
// `early`: a drawn sky whose place lies before a drawn terrain record (SkyOrder::early).
struct ClaySkyFrames {
  uint64_t frames = 0, nofog = 0, view = 0, block = 0, rules = 0, objects = 0, texture = 0, early = 0;
};
inline void CountClaySkyFrame(ClaySky sky, bool texture_missing, ClaySkyFrames& n, bool early = false) {
  if (sky == ClaySky::kOff || size_t(sky) >= size_t(ClaySky::kCount)) return;
  ++n.frames;
  if (sky == ClaySky::kNoFog || sky == ClaySky::kNoLight) ++n.nofog;
  if (sky == ClaySky::kView) ++n.view;
  if (sky == ClaySky::kBlock) ++n.block;
  if (sky == ClaySky::kRules) ++n.rules;
  if (sky == ClaySky::kObjects) ++n.objects;
  if (sky == ClaySky::kDrawn && texture_missing) ++n.texture;
  if (sky == ClaySky::kDrawn && early) ++n.early;
}
// The frames counted "sky-block": frames that could have had a sky and had none for their sky block.
inline uint64_t ClaySkyBlockFrames(const ClaySkyFrames& n) { return n.block + n.rules; }
// The pass's frame counts of the fog, which follow from the frame's verdict alone: one frame added.
// Counted: every frame with the fog's switch on, which is every verdict but kOff (DecideClayFog
// gives kOff for exactly the frames with the switch off). A frame drawn without light, the light's
// switch off among the reasons, is counted "no-light" (final review, M5: before it, such a frame
// with the light's switch off was not counted at all, and the line read "no-light 0" beside
// "not drawn (no light)": run 421).
struct ClayFogFrames {
  uint64_t frames = 0, block = 0, values = 0, view = 0, nolight = 0;
};
inline void CountClayFogFrame(ClayFog fog, ClayFogFrames& n) {
  if (fog == ClayFog::kOff || size_t(fog) >= size_t(ClayFog::kCount)) return;
  ++n.frames;
  if (fog == ClayFog::kBlock) ++n.block;
  if (fog == ClayFog::kValues) ++n.values;
  if (fog == ClayFog::kView) ++n.view;
  if (fog == ClayFog::kNoLight) ++n.nolight;
}
// The frames counted "fog-block": frames with the switch on and with light that were drawn without fog.
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

// Cache key of a record's wind words (wind_decode.h; frame-map section 18, "Wind words").
// A tree's (kind 15: BuildTreeWindWords): the weights' stream, the family, and of each element the
// family reads its stream, where it lies and the source components the reader takes; a leaf's adds
// the position stream and every layout field that changes its decode, because the entry also holds
// the mesh's extent (the largest distance of a vertex from its pivot). A plant's (kind 16:
// ExpandPlantWindWords): the mesh's position stream with its layout (MeshPositionKey: z is the
// decoded position's third component) and the instance set as the flat normals have it
// (InstanceNormalHash: the seed is of the rows; the offset moves positions only). Nothing of the
// draw's registers, of its table row or of its transform: the words are the mesh's, and the wind
// rebuilds nothing. The content hash is the streams'.
inline GeoKey WindKey(const capture::DrawRecord& r, const capture::WindInputs& w) {
  if (w.family == capture::WindFamily::kPlant) {
    GeoKey k = MeshPositionKey(r);
    k.extra = HashCombine32({k.extra, InstanceNormalHash(r.instances)});
    k.kind = 16;
    return k;
  }
  const capture::LightElement& weights = w.element[capture::kWindWeights];
  GeoKey k;
  k.addr = weights.vb.phys_addr;
  k.size = weights.vb.size;
  k.stride = weights.stride_bytes;
  uint32_t h = uint32_t(w.family);
  for (uint32_t e = 0; e < capture::kWindElements; ++e) {
    if (!wind_detail::Reads(w.family, e)) continue;
    const capture::LightElement& el = w.element[e];
    h = HashCombine32({h, e, el.vb.phys_addr, el.vb.size, el.stride_bytes, el.offset_bytes, el.format, el.source, el.source_count});
  }
  if (w.family == capture::WindFamily::kLeaf) h = HashCombine32({h, r.vb.phys_addr, r.vb.size, r.pos.stride_bytes, LayoutHash(r.pos)});
  k.extra = h;
  k.kind = 15;
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

// What GeometryCache::WindWords does before anything is read from the guest or uploaded, as pure
// functions (review of plan Tasks 8 and 9, Important 2: the order of the ranges against the
// elements, a leaf's count, the `need` rule and the record's kind are held by tests here; the
// cache keeps the reads, the lookup, the upload and the insert).
//
// PlanWindWords: the guest ranges a record's wind words are built from, in the order the cache
// reads and hashes them, which of them is which, and the count. A tree: one range an element the
// family reads, in the elements' order (a leaf: pivot, weights, flutter; a trunk: weights), then a
// leaf's position stream (the record's own: its extent is measured against it). A plant: the
// mesh's position stream, then the instance rows. count: the vertices the buffer holds (a tree:
// what every element's stream and, for a leaf, the positions hold whole; a plant: its flat
// positions); mesh_count: a plant's mesh vertices; words: count times the family's words.
// why, with no range, count or words: kElement for a family that is none, a record that is not
// the family's kind (a plant without its instance set, a tree with one, a skinned record) and an
// element no reader decodes as the family's; kStream for a plant without a mesh vertex or rows or
// with more flat positions than the capture's cap, for `need` 0, for fewer vertices than `need`
// and for more words than 32 bits count.
struct WindRange {
  uint32_t addr = 0, size = 0;
};
inline constexpr uint32_t kWindRanges = capture::kWindElements + 1;
struct WindWordsPlan {
  WindBuild why = WindBuild::kElement;
  bool plant = false, leaf = false;
  WindRange range[kWindRanges];
  uint32_t range_count = 0;
  int8_t element_range[capture::kWindElements] = {-1, -1, -1};  // element e's stream among `range`; -1: not read
  int8_t position_range = -1;  // a leaf's positions, a plant's mesh; -1: a trunk
  int8_t rows_range = -1;      // a plant's instance rows
  uint32_t count = 0, mesh_count = 0, words = 0;
};
inline WindWordsPlan PlanWindWords(const capture::DrawRecord& r, const capture::WindInputs& wind, uint32_t need) {
  auto refused = [](WindBuild why) {
    WindWordsPlan none;
    none.why = why;
    return none;
  };
  WindWordsPlan plan;
  const capture::WindFamily family = wind.family;
  plan.plant = family == capture::WindFamily::kPlant;
  plan.leaf = family == capture::WindFamily::kLeaf;
  if (!plan.plant && !plan.leaf && family != capture::WindFamily::kTrunk) return refused(WindBuild::kElement);
  // A plant is an instanced record and a tree is not; no wind mesh is skinned.
  const capture::InstanceSet& s = r.instances;
  if (plan.plant != s.active || r.skin.active) return refused(WindBuild::kElement);
  if (plan.plant) {
    plan.mesh_count = InstanceMeshCount(PositionCount(r.vb.size, r.pos), s);
    // The capture caps flat_count (InstanceRangeSkip); nothing larger is built.
    if (plan.mesh_count == 0 || s.rows_size == 0 || s.flat_count > capture::kMaxDrawCount) return refused(WindBuild::kStream);
    plan.count = s.flat_count;
    plan.position_range = int8_t(plan.range_count);
    plan.range[plan.range_count++] = {r.vb.phys_addr, r.vb.size};
    plan.rows_range = int8_t(plan.range_count);
    plan.range[plan.range_count++] = {s.rows_addr, s.rows_size};
  } else {
    for (uint32_t e = 0; e < capture::kWindElements; ++e) {
      if (!wind_detail::Reads(family, e)) continue;
      if (!wind_detail::Supported(wind.element[e], e)) return refused(WindBuild::kElement);
      plan.element_range[e] = int8_t(plan.range_count);
      plan.range[plan.range_count++] = {wind.element[e].vb.phys_addr, wind.element[e].vb.size};
    }
    plan.count = TreeWindVertices(wind);
    if (plan.leaf) {
      plan.count = std::min(plan.count, PositionCount(r.vb.size, r.pos));
      plan.position_range = int8_t(plan.range_count);
      plan.range[plan.range_count++] = {r.vb.phys_addr, r.vb.size};
    }
  }
  plan.words = WindWordsNeeded(family, plan.count);
  if (need == 0 || plan.count < need || plan.words == 0) return refused(WindBuild::kStream);
  plan.why = WindBuild::kNone;
  return plan;
}

// The words of a plan (plan.words floats into `out`) from the bytes of its ranges: data[i] the
// bytes of plan.range[i], range[i].size of them. A tree: each element from its own range
// (BuildTreeWindWords), a leaf's positions decoded from the position range into `positions` (a
// scratch vector) for the extent; `extent` receives the mesh's extent. A plant: `plant_mesh`, the
// mesh's decoded positions (plan.mesh_count of them, decoded from the position range by the
// caller, which has them memoised), and the rows' range (ExpandPlantWindWords); its extent is
// zero. kNone, or with nothing usable in `out`: the plan's own reason; kStream without a range's
// bytes, `out`, a plant's mesh or a leaf's scratch vector, and for positions that do not decode;
// and what the two builders give.
inline WindBuild BuildWindWords(const WindWordsPlan& plan, const capture::DrawRecord& r, const capture::WindInputs& wind,
                                const uint8_t* const data[kWindRanges], const capture::Float4* plant_mesh,
                                std::vector<capture::Float4>* positions, float* out, WindExtent* extent) {
  if (extent) *extent = WindExtent{};
  if (plan.why != WindBuild::kNone) return plan.why;
  if (!data || !out || plan.range_count > kWindRanges) return WindBuild::kStream;
  for (uint32_t i = 0; i < plan.range_count; ++i) {
    if (!data[i]) return WindBuild::kStream;
  }
  if (plan.plant) {
    if (!plant_mesh || plan.rows_range < 0) return WindBuild::kStream;
    return ExpandPlantWindWords(plant_mesh, plan.mesh_count, data[plan.rows_range], plan.range[plan.rows_range].size, r.instances,
                                plan.count, out);
  }
  const uint8_t* streams[capture::kWindElements] = {};
  size_t sizes[capture::kWindElements] = {};
  for (uint32_t e = 0; e < capture::kWindElements; ++e) {
    const int8_t at = plan.element_range[e];
    if (at < 0) continue;
    streams[e] = data[at];
    sizes[e] = plan.range[at].size;
  }
  const capture::Float4* decoded = nullptr;
  if (plan.leaf) {
    if (!positions || plan.position_range < 0) return WindBuild::kStream;
    positions->resize(plan.count);
    if (!capture::DecodePositions(data[plan.position_range], plan.range[plan.position_range].size, r.pos, 0, plan.count, positions->data())) {
      return WindBuild::kStream;
    }
    decoded = positions->data();
  }
  return BuildTreeWindWords(wind, streams, sizes, decoded, plan.count, out, extent);
}

// Whether a cached buffer of `entry_count` vertices serves a draw that names `need` (the cache
// then hands out the entry's own count and its stored extent, never the asker's).
constexpr bool WindWordsServe(uint32_t entry_count, uint32_t need) { return need != 0 && entry_count >= need; }

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
  uint8_t wind = 0;          // 1: the wind vertex program ("Native wind" below); 0: the clay vertex shader, as always
  // 1: the half program of a coverage record ("Coverage" below: the pixels with one sample of two, blended at one
  // half); 2: the full program (the pixels with both samples, as the record's own state draws them); 0: the kind's
  // own pixel shader, as always. Only CoverageHalfKey, CoverageHalfDepthKey and CoverageFullKey set it; no
  // pre-pass key has it.
  uint8_t coverage = 0;
  bool operator==(const PipelineKey&) const = default;
};
static_assert(sizeof(PipelineKey) == 15);

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

// The colour pass has TWO pipeline caches, neither evicted within a run (final review of sub-project 11, I1):
//   the ordinary one, kMaxClayPipelines keys: every key without `wind` and without `coverage`, the cache of
//   before the two switches, which holds exactly what it held then (no variant ever takes a slot of it);
//   the variants' one, kMaxClayVariantPipelines keys: every key with `wind` or `coverage` (VariantKey).
// A render state has at most seven variants (its own moved; its half part, that part's depth step and its
// full part; those three moved: RecordPipelineKeys), so the variants' cache holds every variant of a full
// ordinary cache: 7 x 64 = 448. Host memory at the bound: 448 keys of 15 bytes and 448 pointers, about 10 KB,
// and one pipeline object of the driver's for each key made (not measured; the programs are shared). A lookup
// is a linear search over the keys (at most 6.7 KB), a few a record.
// Beyond a bound nothing is replaced and the key is refused, every frame anew, counted and logged once a cache
// (PipelineRefusalText):
//   a variant: the record is drawn as before the switches (a wind key: at rest; a coverage key: whole), by its
//   ordinary pipeline: TakeRecordPipelines;
//   an ordinary key: the record is drawn with the default pipeline of its kind, blended or not, exactly as
//   before the two switches (ledger ruling 68: with every switch off nothing changes, past the bound too).
inline constexpr size_t kClayVariantsOfAState = 7;
inline constexpr size_t kMaxClayVariantPipelines = kClayVariantsOfAState * kMaxClayPipelines;
constexpr bool VariantKey(const PipelineKey& k) { return k.wind != 0 || k.coverage != 0; }
// What a refused key was, for the log: a key of the wind program (a moved coverage part too), a coverage
// part at rest, or an ordinary key.
enum class PipelineKeyKind : uint8_t { kOrdinary, kWind, kCoverage };
constexpr PipelineKeyKind PipelineKeyKindOf(const PipelineKey& k) {
  return k.wind ? PipelineKeyKind::kWind : k.coverage ? PipelineKeyKind::kCoverage : PipelineKeyKind::kOrdinary;
}
constexpr const char* PipelineRefusalText(PipelineKeyKind kind) {
  switch (kind) {
    case PipelineKeyKind::kWind: return "a wind key: its records are drawn at rest";
    case PipelineKeyKind::kCoverage: return "a coverage key: its records are drawn whole";
    default: return "an ordinary key: its records are drawn with the default pipeline";
  }
}

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

// --- Coverage (sub-project 11, phase 11b; frame-map section 18, "Coverage: the rule and the order") ----------
// The main scene has two samples a pixel, and under alpha-to-mask the game covers each by its own
// threshold (AlphaToMaskCovers, above: the first at 0.5 - offset / 8, the second at 1 - offset / 8). The
// clay target has one sample: today a pixel is drawn whole where the FIRST sample is covered. A draw
// that fades (its alpha scale below 1) has pixels with the first sample alone, which the game's
// resolve shows at one half over what lies behind; drawn whole they are a solid tree where the game has
// a see-through one. Under --fable2_native_coverage such a record's pixels are parted by their covered
// samples: both, drawn as today; one, blended at one half over what the target holds (ruling 14 of the
// design; the pass: "Native coverage" below).

// How many of the scene's two samples the game covers at pixel (x, y): 0, 1 or 2 (two samples alone).
// The emulator's rule (SDK dxbc_translator_om.cpp, CompletePixelShader_AlphaToMask, the 2x branch: the
// threshold is base - offset / 8 for the bases 0.5 and 1, and a sample is covered iff alpha >= it; no
// number covers nothing). 1 or 2 exactly where AlphaToMaskCovers(offsets, x, y, alpha, 1) keeps the pixel.
constexpr uint32_t CoveredSamples(uint8_t offsets, uint32_t x, uint32_t y, float alpha) {
  const uint32_t index = ((x & 1u) << 1) | (y & 1u);
  const float offset = float((uint32_t(offsets) >> (2 * index)) & 3u);
  return (alpha >= 0.5f - 0.125f * offset ? 1u : 0u) + (alpha >= 1.0f - 0.125f * offset ? 1u : 0u);
}

// Family A: the pixel shaders of the tree leaf and of the trunk and branch, whose alpha is the albedo's
// times pixel c15.w (the fade word) times c26.w (ps_albedo_table.inc: scale refs 107 and 63). The plants'
// pixel shaders (family B) are under alpha-to-mask too and never fade: no coverage record.
inline constexpr uint64_t kCoveragePs[] = {0x7CD57B81550F19E3ull, 0x00E09D1BC5295D52ull, 0x58D3FAAAA84A48AAull,
                                           0x2FBE9B4EE822609Aull};   // family A: leaf, trunk and branch
enum class CoverageKind : uint8_t { kNone, kEarly, kLate };
// A coverage record: the switch on, a family A pixel shader, a textured material whose alpha is the albedo's, the
// state valid with alpha-to-mask on two samples, no blend, a depth write, and an alpha scale that is a number in
// [0, 1). Late for offsets 0xAA (the tree between the eye and the hero, at fade 0.5, which the game issues after
// its depth copy), early for any other (0x87: a tree dissolving by distance). A draw at scale 1 keeps today's
// rule (ruling 13): its half-covered pixels are edge texels alone.
inline CoverageKind CoverageOf(bool on, const capture::DrawRecord& r, const capture::RenderState& rs) {
  if (!on || r.terrain_index != capture::kNoSideIndex) return CoverageKind::kNone;
  const capture::Material& m = r.material;
  bool family = false;
  for (const uint64_t ps : kCoveragePs) family = family || ps == m.ps_hash;
  if (!family || m.status != capture::MaterialStatus::kTextured || m.alpha.source != capture::AlphaSource::kAlbedo) {
    return CoverageKind::kNone;
  }
  if (!rs.valid || !rs.alpha_to_mask || rs.alpha_to_mask_samples != 1 || rs.blend || !rs.depth_test || !rs.depth_write) {
    return CoverageKind::kNone;
  }
  if (!(m.alpha_scale >= 0.0f && m.alpha_scale < 1.0f)) return CoverageKind::kNone;  // (no number: none)
  return rs.alpha_to_mask_offsets == 0xAA ? CoverageKind::kLate : CoverageKind::kEarly;
}
// Whether any pixel of a record of this scale can have both samples covered (its alpha is at most `scale`):
// the scale reaches the second sample's lowest threshold of the four parities. False for a late record at
// 0.5 (0xAA: 0.75 everywhere), which therefore has no fully covered pixel and no place in the pre-pass.
constexpr bool CoverageHasFull(uint8_t offsets, float scale) {
  uint32_t most = 0;
  for (uint32_t index = 0; index < 4; ++index) {
    const uint32_t offset = (uint32_t(offsets) >> (2 * index)) & 3u;
    most = offset > most ? offset : most;
  }
  return scale >= 1.0f - 0.125f * float(most);
}
// The colour pass's order under coverage (PlanCoverageOrder). One item a drawn record, in the frame's order:
// its coverage kind, and whether it is a blended record that is no terrain record (what SkyPlace looks for).
struct CoverageItem { CoverageKind kind; bool blends; };          // one drawn record, in the frame's order
inline constexpr size_t kNoCoverageTail = ~size_t(0);
struct CoverageOrder { size_t sky; size_t tail; };                // drawn before the record at this index (count: after the last)
// `sky_place`: SkyPlace's index. `depth_place`: the index of the first drawn record at or beyond the frame's
// depth place, `count` when the layer's depth is taken after the last record or not at all.
// A half-covered pixel is blended over what the target holds, so everything the game shows behind it must
// be there first: the sky step is drawn before the first such blend (it writes no depth, so by SkyPlace's
// own argument drawing it earlier changes no pixel of a record that writes depth), and the early records'
// half-covered pixels are drawn together in a TAIL, after the opaque records before it, which lies before
// the sun layer's depth is taken (the game's depth copy holds every pixel whose first sample an early
// record covers), before the first late record and before the first blended record.
//   no early and no late item: {sky_place, kNoCoverageTail}: today's order, whatever the other inputs.
//   first_late = the first late item's index (count if none);
//   tail = min(sky_place, depth_place, first_late) when an early item lies before that index, else none;
//   sky  = min(sky_place, tail, first_late).
// An early item at or beyond `tail` (or with no tail) is drawn as a late one: blended in place.
// At index i the pass does: the sky step if sky == i, then the early tail if tail == i, then the layer's
// depth step if it is due, then record i.
constexpr CoverageOrder PlanCoverageOrder(const CoverageItem* items, size_t count, size_t sky_place, size_t depth_place) {
  size_t first_late = count, first_early = count;
  for (size_t i = count; i-- > 0;) {
    if (items[i].kind == CoverageKind::kLate) first_late = i;
    if (items[i].kind == CoverageKind::kEarly) first_early = i;
  }
  if (first_late == count && first_early == count) return {sky_place, kNoCoverageTail};
  // (the first blended record bounds the sky's place whatever the caller names: no blend of a frame with coverage
  // records lies before the sky step)
  for (size_t i = 0; i < count && i < sky_place; ++i) {
    if (items[i].blends) {
      sky_place = i;
      break;
    }
  }
  size_t bound = sky_place < depth_place ? sky_place : depth_place;
  bound = first_late < bound ? first_late : bound;
  const size_t tail = first_early < bound ? bound : kNoCoverageTail;
  size_t sky = sky_place < first_late ? sky_place : first_late;
  sky = tail < sky ? tail : sky;
  return {sky, tail};
}

// Native coverage, the late records (plan Task 13). A late record that the frame draws with its texture is
// drawn in the colour pass alone, at its own place, by the half program (shaders::kClayTexturedHalfPs: it keeps
// the pixels with exactly one covered sample and writes the constant alpha one half), in two steps since ledger
// ruling 60 ("The half part is drawn in TWO STEPS", below): its depth with the record's own depth test and depth
// write, then its colour blended source alpha over inverse source alpha without a depth write; and it is in no
// depth pre-pass (CoverageInPrepass: the game's depth copy has nothing of it; an early record keeps today's
// pre-pass draw). So its end depth is today's (every pixel its first sample covers), the layer's depth and
// the depth place are untouched, and what lies behind it shows through at one half: the game's resolve of two
// samples for everything drawn before it. The shadow mask is not rebuilt (ruling 15): a half-covered pixel's
// sun term takes the mask of what lies behind it. All or nothing a record: one that cannot have its half
// pipeline is today's record in both passes.
// Whether a record's draw state is one the half program can stand in for: drawn with its texture (not flat
// clay, not terrain), its alpha an albedo component, alpha-to-mask under two samples, no blend, depth test and
// depth write. (CoverageOf asks the same of the captured record; this asks it of what the frame draws.)
inline bool CoverageDrawable(const DrawState& d) {
  const uint32_t need = kAlphaToMask | kAlphaFromAlbedo;
  return !d.skip && d.key.textured == 1 && d.key.blend == 0 && d.key.coverage == 0 && d.key.depth_test != 0 && d.key.depth_write != 0 &&
         (d.alpha.mode & need) == need && ((d.alpha.mode >> kAlphaToMaskSamplesShift) & 3u) == 1u;
}
// The half part is drawn in TWO STEPS (ledger ruling 60), in the place the one draw had. A record's leaf cards lie
// behind one another, and one draw that tests, writes and blends at once blends every card that is nearer than
// all before it: a pixel under three such cards kept an eighth of what lay behind where the game keeps one half
// (one sample of two, whatever the cards; measured: frame-map section 18, "The half blend, once"). So:
//   1. its depth (CoverageHalfDepthKey): the half program, no colour, no blend, the record's depth test and write.
//      After it the depth holds the nearest half fragment at every pixel one passed: exactly what the one draw left;
//   2. its colour (CoverageHalfKey): the half program, the half blend, the depth test less-or-equal, NO depth
//      write: against the depth step 1 left no fragment of the record is nearer, so the test passes exactly the
//      fragments AT that depth (the RHI has no "equal"; here less-or-equal is it): the nearest card, once.
// Both steps run the same vertex program on the same constants and buffers (with the wind both are the wind
// program's or the record rests), so step 2's depth is step 1's bit for bit.
// The key of the half part's depth step: the record's key with the half program and no colour written.
constexpr PipelineKey CoverageHalfDepthKey(PipelineKey k) {
  k.coverage = 1;
  k.blend = 0;
  k.color_mask = 0;
  return k;
}
// The key of the half part's colour step: the record's key with the half program, the half blend, the depth test
// less-or-equal (whatever the record's own: a "less" would pass nothing against the record's own depth) and no
// depth write. The target has no alpha channel; the alpha's factors keep what is there.
constexpr PipelineKey CoverageHalfKey(PipelineKey k) {
  k.coverage = 1;
  k.depth_test = 1;
  k.depth_write = 0;
  k.depth_func = uint8_t(capture::Compare::kLessEqual);
  k.blend = 1;
  k.src = uint8_t(capture::Factor::kSrcAlpha);
  k.dst = uint8_t(capture::Factor::kInvSrcAlpha);
  k.op = uint8_t(capture::BlendOp::kAdd);
  k.src_alpha = uint8_t(capture::Factor::kZero);
  k.dst_alpha = uint8_t(capture::Factor::kOne);
  k.op_alpha = uint8_t(capture::BlendOp::kAdd);
  return k;
}
// The colour-pass key of a coverage record's fully covered pixels (plan Task 14): the record's own key with
// the full program, which keeps the pixels with both samples covered and is today's program otherwise:
// opaque, in place, with the record's depth test and write.
constexpr PipelineKey CoverageFullKey(PipelineKey k) {
  k.coverage = 2;
  return k;
}
// The pipelines one record is drawn by, and the order they are asked in (final review of sub-project 11, I1).
// CoveragePlan: the parts a coverage record is drawn by. kLate: its half part in place (and that part's depth
// step). kEarlyFull: its full part in place, its half part in the tail or in place. kEarlyHalf: an early record
// whose scale admits no fully covered pixel: its half part alone; its `place` pipeline is its own, which only
// names its state (the pre-pass's key, the sky's order) and is not drawn in the colour pass. kNone: today's record.
enum class CoveragePlan : uint8_t { kNone, kLate, kEarlyFull, kEarlyHalf };
constexpr CoveragePlan CoveragePlanOf(CoverageKind kind, bool has_full) {
  return kind == CoverageKind::kLate    ? CoveragePlan::kLate
         : kind == CoverageKind::kEarly ? (has_full ? CoveragePlan::kEarlyFull : CoveragePlan::kEarlyHalf)
                                        : CoveragePlan::kNone;
}
// The keys of a record's parts under a plan, at rest or moved (`wind`: every one is the wind program's; `own`:
// DecideDrawState's key, which has neither member).
struct RecordKeys {
  PipelineKey place, tail, depth;
  bool has_tail = false, has_depth = false;
};
constexpr RecordKeys RecordPipelineKeys(PipelineKey own, CoveragePlan plan, bool wind) {
  own.wind = wind ? 1 : 0;
  RecordKeys keys;
  keys.place = plan == CoveragePlan::kLate ? CoverageHalfKey(own) : plan == CoveragePlan::kEarlyFull ? CoverageFullKey(own) : own;
  if (plan != CoveragePlan::kNone) {
    keys.depth = CoverageHalfDepthKey(own);
    keys.has_depth = true;
  }
  if (plan == CoveragePlan::kEarlyFull || plan == CoveragePlan::kEarlyHalf) {
    keys.tail = CoverageHalfKey(own);
    keys.has_tail = true;
  }
  return keys;
}
// T: a pipeline handle that is false when there is none (the pass: nrhi::Pipeline*). `moved`: every part is the
// wind program's. `plan`: the plan the parts are of (kNone: `place` alone, the record's own state).
template <class T>
struct RecordPipelines {
  T place{}, tail{}, depth{};
  bool moved = false;
  CoveragePlan plan = CoveragePlan::kNone;
};
// `ask(key)`: the cached pipeline of the key, made on first use; false when it is refused. `sway`: the record
// may be drawn moved (it has wind words and the wind program exists). A set is all or nothing, its half part's
// colour asked first, the part in its place last; the first complete set of this order is the record's:
//   1. moved, with its coverage parts;   2. at rest, with its coverage parts (the wind's verdict: pipeline);
//   3. moved, whole (the coverage's verdict: pipeline);   4. at rest, whole: `place` alone, which may be none
//   (the caller then draws with the default pipeline of the record's kind, as always).
// So a record that is drawn moved has asked for no rest key: no rest pipeline is made for it, and none of its
// rest keys takes a slot. A record that cannot sway asks for no wind key, and without a plan for its own key
// alone: the pass of before the switches.
template <class T, class Ask>
RecordPipelines<T> TakeRecordPipelines(const PipelineKey& own, CoveragePlan plan, bool sway, Ask&& ask) {
  auto take = [&](CoveragePlan of, bool wind, RecordPipelines<T>* out) {
    const RecordKeys keys = RecordPipelineKeys(own, of, wind);
    RecordPipelines<T> got;
    got.moved = wind;
    got.plan = of;
    if (of == CoveragePlan::kLate) {
      got.place = ask(keys.place);
      if (!got.place) return false;
    } else if (keys.has_tail) {
      got.tail = ask(keys.tail);
      if (!got.tail) return false;
    }
    if (keys.has_depth) {
      got.depth = ask(keys.depth);
      if (!got.depth) return false;
    }
    if (of != CoveragePlan::kLate) {
      got.place = ask(keys.place);
      if (!got.place) return false;
    }
    *out = got;
    return true;
  };
  RecordPipelines<T> got;
  if (sway && plan != CoveragePlan::kNone && take(plan, true, &got)) return got;
  if (plan != CoveragePlan::kNone && take(plan, false, &got)) return got;
  if (sway && take(CoveragePlan::kNone, true, &got)) return got;
  take(CoveragePlan::kNone, false, &got);
  return got;
}
// Native coverage, the early records (plan Task 14; the design check is in the task's report). An early record
// (a tree dissolving by distance: offsets 0x87, a scale below 1) is drawn by its two parts: the fully covered
// pixels in place by the full program (left out when its scale admits none: !CoverageHasFull), the half-covered
// pixels by the half program at one half, TOGETHER with the other early records' in a tail
// (PlanCoverageOrder::tail: after the opaque records before it; before the sun layer's depth is taken, before
// the first late record and before the first blended record), each in the half part's two steps (its depth with
// the record's depth test and write, then its colour without a depth write: CoverageHalfDepthKey). An
// early record at or beyond the tail has its half-covered pixels drawn in its own place, as a late record's.
// After the tail the depth holds every pixel an early record's first sample covers: the game's depth copy, and
// today's depth.
// The depth pre-pass: an early record is in it AS IT IS TODAY (the first sample's pixels, its own depth-only
// program), not by its fully covered pixels alone as the design first had it. The pre-pass feeds the shadow
// mask alone, and the game's mask reads its depth copy, which holds the first sample of every row issued
// before it: so the game gives a half-covered pixel of a fading tree the shadow at the tree's own depth, for
// both samples, and so does this. The mask is therefore exactly the one of before the coverage for every
// early record. A late record is in no pre-pass (the game's copy has nothing of it).
constexpr bool CoverageInPrepass(CoverageKind kind) { return kind != CoverageKind::kLate; }
// Whether a drawn coverage record's half-covered pixels are drawn in the tail (else in its own place).
constexpr bool CoverageHalfInTail(CoverageKind kind, size_t index, size_t tail) {
  return kind == CoverageKind::kEarly && tail != kNoCoverageTail && index < tail;
}
// The pass's decision about one record, one chain. `kind`: CoverageOf. `drawable`: CoverageDrawable of the
// frame's draw state. `has_full`: CoverageHasFull of its offsets and scale. `has_pipeline`: the coverage
// programs and every pipeline the record's parts need exist (the half one; an early record's full one when it
// has fully covered pixels; asked last, only for a record that would be drawn with them).
struct ClayCoverageInputs {
  bool on = false;
  CoverageKind kind = CoverageKind::kNone;
  bool drawable = false, has_full = false, has_pipeline = false;
};
constexpr ClayCoverage DecideClayCoverage(const ClayCoverageInputs& in) {
  if (!in.on) return ClayCoverage::kOff;
  if (in.kind == CoverageKind::kNone || !in.drawable) return ClayCoverage::kNone;
  if (in.kind == CoverageKind::kLate && in.has_full) return ClayCoverage::kFull;
  if (!in.has_pipeline) return ClayCoverage::kPipeline;
  return ClayCoverage::kDrawn;
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
  // The bits of a float: a shadow draw's slope bias (MakeShadowConstants); a colour draw's
  // in-scatter scale under kLightModeUnlitFogTree (DrawFogScale); 0 otherwise, and read by no other draw.
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
// Two more bits of the same word, above the light's seven, which only the pixel stage reads and
// only for a draw without kLightModeLit ("Native fog" below, "Unlit draws"; plan Task 6 of the fog
// sub-project). kLightModeUnlitFog: the draw's unlit colour is fogged with the ground form, its
// in-scatter under the lit draws' exposure. kLightModeUnlitFogAlpha (with the first only): the
// in-scatter is also multiplied by the alpha the draw writes (the game's blended form). With both
// clear every program is the one the light's bits name.
inline constexpr uint32_t kLightModeUnlitFog = 128u, kLightModeUnlitFogAlpha = 256u;
// Two more, of the final review (DrawFogOf below). kLightModeLitNoFog (with kLightModeLit only; read
// by the lit path of the textured pixel shader): the draw is lit and its own pixel shader in the
// game has no fog: its pixels are left unfogged. kLightModeUnlitFogTree (without kLightModeLit and
// without the two unlit bits above): an unlit trunk or leaf: its unlit colour is fogged with the
// trees' form, the in-scatter's scale (the draw's vertex c8.x times the frame's exposure) being the
// float bits of ClayConstants::pad2. No vertex stage reads any of the four.
inline constexpr uint32_t kLightModeLitNoFog = 512u, kLightModeUnlitFogTree = 1024u;
// Two bits above those, which only the WIND vertex program reads ("Native wind" below): the wind
// family of a record that is moved (a capture::WindFamily: 1 a trunk, 2 a leaf, 3 an instanced
// plant), 0 for every draw at rest. The pass sets them only together with the wind program's
// pipeline, the record's wind words at t9 and its wind constants in the slot at b1; no other
// program reads them (the pixel shaders test single bits of the word, none of them these).
inline constexpr uint32_t kLightModeWindShift = 12, kLightModeWindMask = 3u << kLightModeWindShift;
static_assert(uint32_t(capture::WindFamily::kCount) - 1 <= (kLightModeWindMask >> kLightModeWindShift) &&
              (kLightModeWindMask & (kLightModeCount - 1 + kLightModeUnlitFog + kLightModeUnlitFogAlpha + kLightModeLitNoFog +
                                     kLightModeUnlitFogTree)) == 0);
constexpr uint32_t LightModeWind(capture::WindFamily f) {
  return f == capture::WindFamily::kNone || f >= capture::WindFamily::kCount ? 0u : uint32_t(f) << kLightModeWindShift;
}

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
// The fit was made with the terrain's base map read as stored, before commit
// 5d0f974 gave the base map its fetch constant's gamma sign. It is not fitted
// again (controller ruling 30: the old mapping is the fallback of a frame
// without a post block, and the post chain replaces the constant): sunlit
// ground under the old mapping is about 17% under that fit now.
inline constexpr float kLitExposure = 2.22f;

// The first word of the output mapping's constants (cbuffer Map's `exposure`): 1 in every frame,
// lit or not. The mapping takes the target's values as they are.
inline constexpr float kClayMapExposure = 1.0f;

// The exposure E of the lit output: one number for the frame, no draw's own, and the lit draws'
// alone. kLitExposure with light on (in a post frame the game's own scale c20.w takes its place:
// SetClayFramePost, "Native post"): MakeClayFrameConstants puts it into a lit frame's block
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
// The fog's tail (shaders::kClayFrameFogFields) is declared by the colour programs of the four pixel
// shaders that draw the scene (CLAY_FOG: the three that light and, since the unlit draws' fog, the flat
// shader) and by the sky step's shader: the fog's constants, the frame's inverse view-projection, from which a pixel
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
  float exposure;       // E of the lit output: the lit pixel paths multiply by it. ClayExposure of "on"; in a post
                        // frame the game's output scale c20.w (SetClayFramePost)
  float add[3];         // c20.xyz
  float post = 1.0f;    // what every unlit colour is multiplied by: the unlit scale of a post frame, 1 in every
                        // other frame, one without light included (never 0: SetClayFramePost, "Native post")
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
// frame without one is "light-block"); else everything is zero but the unlit word, which is 1 (a frame
// without post: "Native post" below). The eye is not among those values:
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

// Whether a tree draw's in-scatter scale may reach its shader ("Native fog" below): a number in
// [0, kTreeFogScaleMax] (capture::kTreeFogNone, the marker of a draw without one, is not; false for a
// NaN). A trunk or a leaf without a usable scale is drawn without fog and counted.
constexpr bool TreeFogScaleUsable(float c8x) { return c8x >= 0.0f && c8x <= capture::kTreeFogScaleMax; }

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
    const bool usable = TreeFogScaleUsable(l.tree_fog);
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
// The native tables are arrays in the frame's constant block (the controller's ruling of plan Task
// 4: no binding parameter was left for textures), which the shaders read by hand with this
// arithmetic in single precision (fog_scatter, fog_transmit): the same centres, the same clamps,
// no sampler and no filter of the GPU's.
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
// (capture::FogBlock::scatter, ::transmit). `gain`: a factor on the in-scatter term beside c28.y
// (light_formulas.fog's scatter_scale): 1 for every lit draw; an unlit draw's is UnlitFogMirror's.
template <size_t K>
inline void FogMirror(const float C[3], const float P[3], const float eye[3], const ClayFogConstants& f,
                      const float (*scatter)[K], const float (*transmit)[K], double out[3], double gain = 1.0) {
  double base[3];
  for (int k = 0; k < 3; ++k) out[k] = base[k] = std::isfinite(C[k]) ? double(C[k]) : 0.0;
  if (!scatter || !transmit || !std::isfinite(gain)) return;
  const FogPoint at = FogCoords(P, eye, f);
  if (!at.fogged) return;
  if (!std::isfinite(f.amount) || !std::isfinite(f.scatter) || !std::isfinite(f.near_colour[0]) ||
      !std::isfinite(f.near_colour[1]) || !std::isfinite(f.near_colour[2])) {
    return;
  }
  double S[3], T[3], fogged[3];
  FogTableRead(scatter, kFogScatterTexels, at.a, S);
  FogTableRead(transmit, kFogTransmitTexels, at.s, T);
  const double scale = double(f.scatter) * gain;
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

// --- Unlit draws (plan Task 6; the controller's ruling after the review of plan Task 4) ---
// The game fogs every draw whose pixel shader has the fog block, lit by the native pass or not. A
// draw the pass does not light (a family it has no function for: the water, the fur, the blended
// characters; or a draw whose light inputs could not be read) is drawn as flat clay or with its
// texture alone, and in a fogged frame it is fogged all the same, so that it does not stand
// unhazed against hazed ground:
//   C' = fog(U, ..., scatter_scale = exposure * alpha)        light_formulas.fog, the ground form
// U: the draw's unlit colour as the pass writes it (a display-linear value that carries no
// exposure: the colour under the haze stays the placeholder it was). exposure: the frame's, which
// every lit value is multiplied by after its fog, so that the in-scatter an unlit pixel gains is
// the in-scatter a lit pixel at the same place gains. alpha: 1, or for a shader of the game's
// blended form (FogShaderEntry::alpha: its in-scatter is multiplied by the pixel's alpha) the
// textured draw's own alpha, saturated (a flat draw has none: 1); the game's is its output alpha
// before c26.w, the native one is the material's (texture alpha times the draw's scale): an
// approximation.
// The shader bounds the fogged value like a lit one (clay_lit: a number, not negative, at most 64;
// final review, M2: with c28.x above 1, which the block allows up to 4, the blend could go below 0
// and a blended draw would darken what is under it). The mirrors below do not: with the captured
// c28 = (1, 1) and a colour in 0..1 the value is inside the bound.
//
// Which draws, and in which form: DrawFogOf, one rule for every drawn record (final review, I1, M1,
// M3). The game's own pixel shader decides:
//   - a shader of the fog table (capture::FindFogShader: the 40 of the game's fog block): fogged.
//     Lit: by its family's lit path (the ground form; an object's block names it). Unlit: the form
//     above, with the alpha factor only where the draw's program writes an alpha of its own (a
//     textured mesh draw; a flat draw and a terrain draw have none: scale 1).
//   - a trunk's or a leaf's shader (not in the table: a tree's fog is its VERTEX shader's): the
//     trees' form with the draw's scale c8.x. Lit: through its object block, or none without a
//     usable scale (MakeClayObjectConstants; counted fog_trees / fog_trees_none). Unlit (its light
//     could not be made): the same form on the unlit colour, the in-scatter times the frame's
//     exposure, when the record's light inputs carry a usable scale; unfogged and counted
//     "trees without a scale" otherwise (no inputs at all among the reasons).
//   - any other shader: NOT fogged, lit or not. Lit, that is the one plant form without a fog
//     block, 26052DEFBA688AF2 (22 instructions, no fog register; the game draws more plant
//     batches with it as the fog's start moves out: 3 to 14 a frame at the bridge), and whatever
//     a later table adds (test case 597 holds the two tables against each other);
//     the lit path is told by a bit. Unlit: counted "not in the fog table" (the lake's surface
//     2B09..., which is not of the form, is drawn through the indexed builder and is no record of
//     the pass at all).
// A lit terrain record is always fogged: every terrain shader of the light table is in the fog
// table (case 597), and the terrain programs have no path without fog.
enum class DrawFog : uint8_t {
  kNone,              // the frame is not fogged
  kLit,               // lit, fogged by its family's lit path
  kLitNoFog,          // lit, not fogged: its pixel shader has no fog in the game
  kUnlitPlain,        // unlit, the unlit form
  kUnlitAlpha,        // unlit, the unlit form with the in-scatter times the alpha the draw writes
  kUnlitTree,         // an unlit trunk or leaf: the trees' form with its scale
  kUnlitTreeNoScale,  // an unlit trunk or leaf without a usable scale: not fogged
  kUnlitNoFog,        // unlit, not fogged: its pixel shader is neither in the fog table nor a tree's
  kCount
};
// What the rule reads. `frame_fogged`: the frame's flag (ClayFogConstants::on). `lit`: the pass
// lights the draw. `terrain`: a terrain record. `family`: the light family of the draw's pixel
// shader (capture::PsLightFamily; kNone without one). `in_table`, `alpha`: the shader's entry of
// the fog table, and its blended form. `own_alpha`: the draw's program writes an alpha of its own
// (the textured shader with an albedo). `tree_scale`: the record's LightInputs::tree_fog
// (capture::kTreeFogNone without light inputs).
struct DrawFogInputs {
  bool frame_fogged = false, lit = false, terrain = false;
  capture::LightFamily family = capture::LightFamily::kNone;
  bool in_table = false, alpha = false, own_alpha = false;
  float tree_scale = capture::kTreeFogNone;
};
constexpr DrawFog DrawFogOf(const DrawFogInputs& in) {
  if (!in.frame_fogged) return DrawFog::kNone;
  const bool tree = !in.terrain && (in.family == capture::LightFamily::kTreeTrunk || in.family == capture::LightFamily::kTreeLeaf);
  if (in.lit) return in.terrain || tree || in.in_table ? DrawFog::kLit : DrawFog::kLitNoFog;
  if (tree) return TreeFogScaleUsable(in.tree_scale) ? DrawFog::kUnlitTree : DrawFog::kUnlitTreeNoScale;
  if (!in.in_table) return DrawFog::kUnlitNoFog;
  return in.alpha && in.own_alpha ? DrawFog::kUnlitAlpha : DrawFog::kUnlitPlain;
}
// A record's: the family and the entry by its pixel shader, the scale from its light inputs (null:
// it has none).
inline DrawFog RecordDrawFog(bool frame_fogged, bool lit, bool terrain, bool own_alpha, uint64_t ps_hash,
                             const capture::LightInputs* light) {
  const capture::FogShaderEntry* entry = capture::FindFogShader(ps_hash);
  DrawFogInputs in;
  in.frame_fogged = frame_fogged;
  in.lit = lit;
  in.terrain = terrain;
  in.family = capture::PsLightFamily(ps_hash);
  in.in_table = entry != nullptr;
  in.alpha = entry && entry->alpha;
  in.own_alpha = own_alpha;
  in.tree_scale = light ? light->tree_fog : capture::kTreeFogNone;
  return DrawFogOf(in);
}
// Its bits of ClayConstants::light_mode (none for a draw fogged by its lit path or not at all).
constexpr uint32_t DrawFogMode(DrawFog fog) {
  switch (fog) {
    case DrawFog::kLitNoFog: return kLightModeLitNoFog;
    case DrawFog::kUnlitPlain: return kLightModeUnlitFog;
    case DrawFog::kUnlitAlpha: return kLightModeUnlitFog | kLightModeUnlitFogAlpha;
    case DrawFog::kUnlitTree: return kLightModeUnlitFogTree;
    default: return 0u;
  }
}
// The scale the draw's constants carry (the float of ClayConstants::pad2): an unlit tree's
// in-scatter scale times the frame's exposure, which every lit value is multiplied by after its
// fog and an unlit value is not; 0 for every other draw, and for a product that is no number.
inline float DrawFogScale(DrawFog fog, float tree_scale, float exposure) {
  if (fog != DrawFog::kUnlitTree) return 0.0f;
  const float scale = tree_scale * exposure;
  return std::isfinite(scale) ? scale : 0.0f;
}
// The mirror: FogMirror with the in-scatter times `exposure` and `alpha`. Either not a number: the
// pixel is left as it is.
template <size_t K>
inline void UnlitFogMirror(const float C[3], const float P[3], const float eye[3], const ClayFogConstants& f, float exposure,
                           float alpha, const float (*scatter)[K], const float (*transmit)[K], double out[3]) {
  FogMirror(C, P, eye, f, scatter, transmit, out, double(exposure) * double(alpha));
}
// An unlit trunk's or leaf's: the trees' form with the scale times the exposure (the single the
// pass hands the shader). A scale or a product that is no number: the pixel is left as it is.
template <size_t K>
inline void UnlitTreeFogMirror(const float C[3], const float P[3], const float eye[3], const ClayFogConstants& f, float c8x,
                               float exposure, const float (*scatter)[K], const float (*transmit)[K], double out[3]) {
  TreeFogMirror(C, P, eye, f, c8x * exposure, scatter, transmit, out);
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

// The counts of the Fog line that follow from a drawn record's fog (the lit trees' two counts are
// PrepareLight's: fog_trees, fog_trees_none).
inline void CountDrawFog(DrawFog fog, ClayStats& st) {
  switch (fog) {
    case DrawFog::kLitNoFog: ++st.fog_lit_none; break;
    case DrawFog::kUnlitPlain: ++st.fog_unlit; break;
    case DrawFog::kUnlitAlpha:
      ++st.fog_unlit;
      ++st.fog_unlit_alpha;
      break;
    case DrawFog::kUnlitTree:
      ++st.fog_unlit;
      ++st.fog_unlit_tree;
      break;
    case DrawFog::kUnlitTreeNoScale: ++st.fog_unlit_tree_none; break;
    case DrawFog::kUnlitNoFog: ++st.fog_unlit_none; break;
    default: break;
  }
}

// The Fog line of the status text and the log: the switch, the frame's block, what the pass did,
// the frame's tree draws, its unlit draws and its lit draws without fog, and the pass's totals
// since it was made.
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
  char buf[640];  // (the line with every count at its largest is whole: test case 585)
  std::snprintf(buf, sizeof(buf),
                " | trees %u with a scale, %u without | unlit %u fogged (%u by their alpha, %u trees by their scale), "
                "%u trees without a scale, %u not in the fog table | lit %u not fogged (no fog in their pixel shader) | "
                "%u records with another view | frames since start %llu: %s %llu {block: %llu, values: %llu, view: %llu}, "
                "no-light %llu",
                st.fog_trees, st.fog_trees_none, st.fog_unlit, st.fog_unlit_alpha, st.fog_unlit_tree, st.fog_unlit_tree_none,
                st.fog_unlit_none, st.fog_lit_none, st.fog_view_outliers,
                static_cast<unsigned long long>(st.fog_frames), capture::kFogBlockReason,
                static_cast<unsigned long long>(st.fog_block_frames + st.fog_values_frames + st.fog_view_frames),
                static_cast<unsigned long long>(st.fog_block_frames), static_cast<unsigned long long>(st.fog_values_frames),
                static_cast<unsigned long long>(st.fog_view_frames), static_cast<unsigned long long>(st.fog_nolight_frames));
  return text + buf;
}

// --- Native sky (frame-map section 16, "Native sky"; docs/native-renderer/lighting-formulas.md, "The sky") ---
//
// The game draws its sky as one cube around the eye with pixel shader 0xF7601FBE485936FD, after the
// terrain layers and before its blended draws, depth-tested (greater-or-equal on its reversed
// depth) without depth write and without blend. The native pass draws one full-screen triangle in
// that place (SkyPlace) instead of the cube, which only supplies a direction: at the far depth of
// the pass's own convention (clip z / w = 1, which is also the value the depth is cleared to; the
// pass keeps the game's clip z and mirrors its compare), tested less-or-equal, so that it lands
// exactly where the depth is still the clear value; no depth write, no blend.
//
// The arithmetic is the game's shader (light_formulas.sky, machine-checked against the microcode),
// with its own polynomials for the lat-long coordinate and its literals as the exact singles
// (light_formulas.SKY_LITERALS):
//   n      = the view ray of the pixel, normalised
//   a      = sat(n . c66);  S = scatter(a)                    the fog's in-scatter table
//   k      = |n.z|^0.2;  h = c67.y * (1.05 - k)
//   l1     = h + (c31.x - h) * k;  l2 = h + (c46.x - h) * k   the two path lengths
//   E      = exp2(-1.4426950 * (l1 * c64 + l2 * c65))
//   base   = (1 - E) * S                                      the clear-sky term
//   (u, v) = SkyLatLong(n);  s13 = tf13(u, v);  s14 = tf14(u, v)
//   col    = s13.rgb^2 + c47.x * (s14.rgb^2 - s13.rgb^2);  alpha = s13.a + c47.x * (s14.a - s13.a)
//   sky    = base + alpha * (col - base)
// and the result goes through the lit draws' exposure, which stands in for the game's c20.w. In
// every captured frame the two textures are one 16 x 16 texture of zeros and c47.x is 0: the sky is
// the clear-sky term alone there, and the texture term has never been seen with content.
//
// The mirrors below compute in double on the single-precision values a shader gets and are pinned to
// the reference by the numbers of tools/xdk_sigmatch/sky_pins.py. Guards: a ray without a length or
// that is no number counts as (0, 0, 0) (the table's first texel, the horizon's paths); a value or
// a texel that is no number counts as 0; the result is a number for any input (there is no other
// colour to fall back to: the pass does not draw a sky whose constants it will not hand a shader).

// The shader's literals c250..c255 (light_formulas.SKY_LITERALS), each the exact single.
inline constexpr double kSkyAtan[5] = {0.9998660087585449, -0.3302994966506958, 0.18014100193977356, -0.08513300120830536,
                                       0.02083509974181652};  // q, q^3, q^5, q^7, q^9
inline constexpr double kSkyAcos[4] = {1.5707287788391113, -0.2121143937110901, 0.07426100224256516, -0.018729299306869507};
inline constexpr double kSkyPi = 3.1415927410125732, kSkyHalfPi = 1.5707963705062866;
inline constexpr double kSkyInvTwoPi = 0.15915963053703308, kSkyTwoOverPi = 0.6366385221481323;
inline constexpr double kSkyPathPower = 0.20000000298023224, kSkyPathHorizon = 1.0499999523162842;
inline constexpr double kSkyInvLn2 = 1.4426950216293335;

// The sky textures' coordinate of a unit direction (or of (0, 0, 0)): light_formulas.sky_latlong,
// the shader's arithmetic and not atan2 / asin (which differ from it by 1.6e-5 and 4.3e-5).
//   u = 0.5 + atan2(y, x) / (2 pi) by its polynomial in min / max of |x|, |y|; on the half plane
//   y = 0, x < 0 and just north of it u is 1.0000147, just south -0.0000147 (the seam; y = 0 counts
//   as positive); at the poles (x = y = 0) the ratio is 0 by the console's product rule: u = 0.5.
//   v = asin(z) * 2 / pi by its polynomial: 0 at the horizon, 1.00003 at the zenith, negative below.
inline void SkyLatLong(const double n[3], double uv[2]) {
  const double x = n[0], y = n[1], z = n[2];
  const double ax = std::fabs(x), ay = std::fabs(y), az = std::fabs(z);
  const double hi = ax > ay ? ax : ay, lo = ax > ay ? ay : ax;
  const double q = hi > 0.0 ? lo * (1.0 / hi) : 0.0;
  const double q2 = q * q;
  double at = q * (q2 * (q2 * (q2 * (q2 * kSkyAtan[4] + kSkyAtan[3]) + kSkyAtan[2]) + kSkyAtan[1]) + kSkyAtan[0]);
  if (ay > ax) at = at + (-2.0 * at + kSkyHalfPi);
  if (x < 0.0) at = at + -kSkyPi;
  if ((x < y ? x : y) < 0.0 && (x < y ? y : x) >= 0.0) at = at - (at + at);
  const double root = std::sqrt(std::fabs(1.0 - az));
  const double poly = az * (az * (az * kSkyAcos[3] + kSkyAcos[2]) + kSkyAcos[1]) + kSkyAcos[0];
  double ac = poly * root;
  if (z < 0.0) ac = ac + (-2.0 * root * poly + kSkyPi);
  uv[0] = 0.5 + at * kSkyInvTwoPi;
  uv[1] = (kSkyHalfPi - ac) * kSkyTwoOverPi;
}

// The sky step's constants (cbuffer Sky of shaders::kClaySkyPs, register b0 of the sky's own layout;
// shaders::kClaySkyFields is its layout): one small slot of the frame's constant buffer. The rest of
// what the shader reads is the frame's block at b2: the in-scatter table, the axis c66, the
// exposure, and 1 / the target's size.
struct ClaySkyConstants {
  float ray0[3];        // the view ray of a pixel: ray0 + X * rayx + Y * rayy, (X, Y) its centre in -1..1, Y up (SkyRayBasis)
  float c31x;           // pixel c31.x at the sky draw: the first path's length at the zenith
  float rayx[3];
  float c46x;           // c46.x: the second path's
  float rayy[3];
  float c47x;           // c47.x: the blend from the first sky texture to the second
  float ext1[3];        // c64.rgb
  float c67y;           // c67.y: the paths' height
  float ext2[3];        // c65.rgb
  uint32_t textures;    // 1: the two sky textures are bound at t0, t1 and read; 0: the clear-sky term alone
  float fix13[2];       // host texture scale of tf13 (TextureCache::Resolve's uv_fix)
  float fix14[2];       // ... of tf14
  uint32_t sampler13;   // static sampler of tf13 (SamplerIndex)
  uint32_t sampler14;   // ... of tf14
  uint32_t pad[2];
};
static_assert(sizeof(ClaySkyConstants) == 112);

// The sky's own rules on the two blocks (SkyRule): judged here and not in the fog block, so that a
// frame whose sky numbers are odd keeps its fog. Both blocks must be valid for a sky; this judges
// only the values (the first rule that fails): every value the sky reads a number within
// capture::kFogRegisterLimit; c67.y >= 0; no component of c64, c65 below 0; c31.x, c46.x >= 0;
// c47.x in 0..1. With them the two paths are not negative and E lies in 0..1, so the clear-sky term
// is between 0 and the table's texel and no product can overflow.
inline SkyRule SkyRules(const capture::FogBlock& fog, const capture::SkyBlock& sky) {
  auto bounded = [](float v) { return std::fabs(v) <= capture::kFogRegisterLimit; };  // (false for a NaN and for infinity)
  bool ok = bounded(fog.range[1]) && bounded(sky.c31x) && bounded(sky.c46x) && bounded(sky.c47x);
  for (int i = 0; i < 3; ++i) ok = ok && bounded(fog.ext[0][i]) && bounded(fog.ext[1][i]) && bounded(fog.axis[i]);
  if (!ok) return SkyRule::kValue;
  if (!(fog.range[1] >= 0.0f)) return SkyRule::kHeight;
  for (int i = 0; i < 3; ++i) {
    if (!(fog.ext[0][i] >= 0.0f && fog.ext[1][i] >= 0.0f)) return SkyRule::kExtinction;
  }
  if (!(sky.c31x >= 0.0f && sky.c46x >= 0.0f)) return SkyRule::kPath;
  if (!(sky.c47x >= 0.0f && sky.c47x <= 1.0f)) return SkyRule::kBlend;
  return SkyRule::kNone;
}

// The view rays of the frame from its view-projection V (clip = V . (p, 1); FrameInverseViewProj's
// `view_proj`, the matrix whose inverse the mask and the fog use). A direction d is seen at the
// screen point (X, Y) (-1..1, Y up) when r0 . d = X * (r3 . d) and r1 . d = Y * (r3 . d), with
// r0, r1, r3 the first three entries of V's x, y and w rows; so
//   d(X, Y) = r0 x r1 + X * (r1 x r3) + Y * (r3 x r0),   towards the scene when r3 . (r0 x r1) > 0
// (negated otherwise). It is the direction from the eye through the pixel, the same line the
// inverse view-projection gives for the pixel at any two depths, and it reads neither the depth row
// nor the translation column: it does not depend on a depth value, on the depth convention or on
// the eye. Formed from V itself and not from its inverse because the inverse's rows cancel (its
// entries are of the size of the eye's coordinates over the near plane): the same ray through the
// single-precision inverse is off by about 1e-4 radian, this one by the rounding of V's entries
// (test case 593 measures both at the picture's edges and corners).
// `basis`: ray0, rayx, rayy, scaled so that ray0 has length 1 (computed in double, stored as
// singles). False, with `basis` untouched, when an entry is no number, the three rows are not
// independent (no forward direction) or a result does not fit.
inline bool SkyRayBasis(const float view_proj[4][4], float basis[3][3]) {
  if (!view_proj || !basis) return false;
  double r[3][3];
  static constexpr int kRows[3] = {0, 1, 3};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      if (!std::isfinite(view_proj[kRows[i]][j])) return false;
      r[i][j] = double(view_proj[kRows[i]][j]);
    }
  }
  auto cross = [](const double a[3], const double b[3], double out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
  };
  double d[3][3];
  cross(r[0], r[1], d[0]);
  cross(r[1], r[2], d[1]);
  cross(r[2], r[0], d[2]);
  const double det = r[2][0] * d[0][0] + r[2][1] * d[0][1] + r[2][2] * d[0][2];
  const double len = std::sqrt(d[0][0] * d[0][0] + d[0][1] * d[0][1] + d[0][2] * d[0][2]);
  if (!std::isfinite(det) || !std::isfinite(len) || !(len > 0.0)) return false;
  // (independent rows: the forward ray is not at a right angle to the view axis within rounding)
  const double w_len = std::sqrt(r[2][0] * r[2][0] + r[2][1] * r[2][1] + r[2][2] * r[2][2]);
  if (!(std::fabs(det) > 1e-9 * len * w_len)) return false;
  const double scale = (det > 0.0 ? 1.0 : -1.0) / len;
  float out[3][3];
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      out[i][j] = float(d[i][j] * scale);
      if (!std::isfinite(out[i][j])) return false;
    }
  }
  std::memcpy(basis, out, sizeof(out));
  return true;
}

// The frame's sky constants. False, with `out` untouched, unless both blocks are valid, the sky's
// rules hold (SkyRules: checked again here, so that no value reaches a shader on the strength of a
// flag alone) and the basis is of numbers. Without textures; SetClaySkyTextures turns them on.
inline bool MakeClaySkyConstants(const capture::FogBlock& fog, const capture::SkyBlock& sky, const float basis[3][3],
                                 ClaySkyConstants* out) {
  if (!fog.valid || !sky.valid || !basis || !out || SkyRules(fog, sky) != SkyRule::kNone) return false;
  ClaySkyConstants c{};
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(basis[0][i]) || !std::isfinite(basis[1][i]) || !std::isfinite(basis[2][i])) return false;
    c.ray0[i] = basis[0][i];
    c.rayx[i] = basis[1][i];
    c.rayy[i] = basis[2][i];
    c.ext1[i] = fog.ext[0][i];
    c.ext2[i] = fog.ext[1][i];
  }
  c.c31x = sky.c31x;
  c.c46x = sky.c46x;
  c.c47x = sky.c47x;
  c.c67y = fog.range[1];
  c.fix13[0] = c.fix13[1] = c.fix14[0] = c.fix14[1] = 1.0f;
  *out = c;
  return true;
}

// The sky's two textures. The second is read only where the blend is above 0 (with c47.x = 0 the
// shader's blend returns the first, whatever the second holds): the pass then binds the first in
// its place. The texture term is drawn only when every texture that is read could be had; else the
// sky is the clear-sky term alone and the frame is counted "sky-texture".
constexpr bool SkySecondTextureRead(float c47x) { return c47x > 0.0f; }
constexpr bool SkyTexturesUsable(bool first, bool second, float c47x) { return first && (second || !SkySecondTextureRead(c47x)); }
inline void SetClaySkyTextures(ClaySkyConstants& c, uint32_t sampler13, const float fix13[2], uint32_t sampler14, const float fix14[2]) {
  const bool ok = fix13 && fix14 && std::isfinite(fix13[0]) && std::isfinite(fix13[1]) && std::isfinite(fix14[0]) && std::isfinite(fix14[1]);
  c.textures = ok ? 1u : 0u;
  c.sampler13 = ok ? (sampler13 & 3u) : 0u;
  c.sampler14 = ok ? (sampler14 & 3u) : 0u;
  for (int i = 0; i < 2; ++i) {
    c.fix13[i] = ok ? fix13[i] : 1.0f;
    c.fix14[i] = ok ? fix14[i] : 1.0f;
  }
}

// The sky's two path lengths from |n.z| of the unit ray, and E (light_formulas.sky_paths,
// sky_extinction). At the zenith the paths are c31.x and c46.x, at the horizon both 1.05 * c67.y.
inline void SkyPaths(double az, const ClaySkyConstants& c, double l[2]) {
  auto finite = [](float v) { return std::isfinite(v) ? double(v) : 0.0; };
  const double k = az > 0.0 ? std::pow(az, kSkyPathPower) : 0.0;
  const double h = finite(c.c67y) * (kSkyPathHorizon - k);
  l[0] = h + (finite(c.c31x) - h) * k;
  l[1] = h + (finite(c.c46x) - h) * k;
  for (int i = 0; i < 2; ++i) {
    if (!std::isfinite(l[i])) l[i] = 0.0;
  }
}
inline void SkyExtinction(const double l[2], const ClaySkyConstants& c, double E[3]) {
  auto finite = [](float v) { return std::isfinite(v) ? double(v) : 0.0; };
  for (int i = 0; i < 3; ++i) {
    const double x = -(l[0] * finite(c.ext1[i]) + l[1] * finite(c.ext2[i])) * kSkyInvLn2;
    const double e = std::exp2(x);
    E[i] = std::isfinite(e) ? e : 0.0;  // (the reference: an overflow counts as 0; unreachable inside the rules)
  }
}

// The sky of a view ray `n` (any length), linear, before the exposure: light_formulas.sky.
// `axis`: c66.xyz (the frame's fog constants'); `scatter`: the frame's in-scatter table. `s13`,
// `s14`: what the two textures return at SkyLatLong(n), four floats each; s13 null: the clear-sky
// term alone; s14 null: the first texture alone.
template <size_t K>
inline void SkyMirror(const float n[3], const ClaySkyConstants& c, const float axis[3], const float (*scatter)[K], const float* s13,
                      const float* s14, double out[3]) {
  auto finite = [](float v) { return std::isfinite(v) ? double(v) : 0.0; };
  double u[3] = {0.0, 0.0, 0.0};
  if (std::isfinite(n[0]) && std::isfinite(n[1]) && std::isfinite(n[2])) {
    const double n2 = double(n[0]) * n[0] + double(n[1]) * n[1] + double(n[2]) * n[2];
    if (n2 > 0.0 && std::isfinite(n2)) {
      const double inv = 1.0 / std::sqrt(n2);
      for (int i = 0; i < 3; ++i) u[i] = double(n[i]) * inv;
    }
  }
  double a = u[0] * finite(axis[0]) + u[1] * finite(axis[1]) + u[2] * finite(axis[2]);
  a = a != a ? 0.0 : (a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a));
  double S[3], l[2], E[3], base[3];
  FogTableRead(scatter, kFogScatterTexels, a, S);
  SkyPaths(std::fabs(u[2]), c, l);
  SkyExtinction(l, c, E);
  for (int i = 0; i < 3; ++i) {
    base[i] = (1.0 - E[i]) * (std::isfinite(S[i]) ? S[i] : 0.0);
    out[i] = std::isfinite(base[i]) ? base[i] : 0.0;
  }
  if (!s13) return;
  double col[3], alpha = finite(s13[3]);
  for (int i = 0; i < 3; ++i) col[i] = finite(s13[i]) * finite(s13[i]);
  if (s14) {
    const double k = finite(c.c47x);
    for (int i = 0; i < 3; ++i) col[i] = col[i] + k * (finite(s14[i]) * finite(s14[i]) - col[i]);
    alpha = alpha + k * (finite(s14[3]) - alpha);
  }
  for (int i = 0; i < 3; ++i) {
    const double v = base[i] + alpha * (col[i] - base[i]);
    out[i] = std::isfinite(v) ? v : 0.0;
  }
}

// The view ray of the pixel whose centre is (pos_x, pos_y), in single precision as the shader forms
// it: X = pos_x * (2 / width) - 1, Y = 1 - pos_y * (2 / height), n = ray0 + X * rayx + Y * rayy.
inline void SkyPixelRay(const ClaySkyConstants& c, float pos_x, float pos_y, float inv_width, float inv_height, float n[3]) {
  const float X = pos_x * (2.0f * inv_width) - 1.0f, Y = 1.0f - pos_y * (2.0f * inv_height);
  for (int i = 0; i < 3; ++i) n[i] = c.ray0[i] + X * c.rayx[i] + Y * c.rayy[i];
}

// The sky of a pixel as the sky step's shader computes it, with the frame's block (its fog's axis,
// its in-scatter table, 1 / the target's size), before the exposure.
inline void SkyPixelMirror(float pos_x, float pos_y, const ClayFrameConstants& frame, const ClaySkyConstants& c, const float* s13,
                           const float* s14, double out[3]) {
  float n[3];
  SkyPixelRay(c, pos_x, pos_y, frame.mask_du, frame.mask_dv, n);
  SkyMirror(n, c, frame.fog.axis, frame.scatter, s13, s14, out);
}

// The Sky line of the status text and the log: the frame's sky block, what the pass did, the
// sky's place among the records, and the pass's totals since it was made.
inline std::string FormatSkyText(const ClayStats& st) {
  if (!st.fog_switch) return "Sky: off";
  std::string text = "Sky: on, block ";
  if (st.sky_block) {
    text += "valid";
  } else {
    text += std::string("invalid (") + capture::kSkyBlockReason + ": " + capture::SkyBlockReasonName(st.sky_reason) + ")";
  }
  switch (st.sky) {
    case ClaySky::kDrawn:
      text += st.sky_textured ? ", drawn with its textures"
                              : st.sky_texture_missing ? std::string(", drawn without its textures (") + kSkyTextureReason + ")"
                                                       : std::string(", drawn");
      break;
    case ClaySky::kNoLight: text += ", not drawn (no light)"; break;
    case ClaySky::kNoFog: text += std::string(", not drawn (no fog: ") + capture::kFogBlockReason + ")"; break;
    case ClaySky::kView: text += std::string(", not drawn (view: ") + FrameViewReasonName(st.sky_view) + ")"; break;
    case ClaySky::kBlock: text += std::string(", not drawn (") + capture::kSkyBlockReason + ": block)"; break;
    case ClaySky::kRules:
      text += std::string(", not drawn (") + capture::kSkyBlockReason + ": " + SkyRuleName(st.sky_rule) + ")";
      break;
    case ClaySky::kObjects: text += ", not drawn (no objects)"; break;
    default: text += ", not drawn (off)"; break;
  }
  char place[48];
  if (st.sky_place_record) {
    std::snprintf(place, sizeof(place), "then pixel shader 0x%016llX", static_cast<unsigned long long>(st.sky_place_ps));
  } else {
    std::snprintf(place, sizeof(place), "then the last record");
  }
  char buf[768];  // (four floats of up to 44 characters each and nine counts at their largest fit)
  std::snprintf(buf, sizeof(buf),
                " | view (%.3f, %.3f, %.3f), to the fog's axis %.3f | before it %u records, %u without depth write, %s | "
                "frames since start %llu: %s %llu {block: %llu, rules: %llu}, %s %llu, no-fog %llu, view %llu, no-objects %llu, "
                "early %llu",
                double(st.sky_ray[0]), double(st.sky_ray[1]), double(st.sky_ray[2]), double(st.sky_axis_cos), st.sky_before,
                st.sky_before_no_depth, place, static_cast<unsigned long long>(st.sky_frames), capture::kSkyBlockReason,
                static_cast<unsigned long long>(st.sky_block_frames + st.sky_rules_frames),
                static_cast<unsigned long long>(st.sky_block_frames), static_cast<unsigned long long>(st.sky_rules_frames),
                kSkyTextureReason, static_cast<unsigned long long>(st.sky_texture_frames),
                static_cast<unsigned long long>(st.sky_nofog_frames), static_cast<unsigned long long>(st.sky_view_frames),
                static_cast<unsigned long long>(st.sky_objects_frames), static_cast<unsigned long long>(st.sky_early_frames));
  return text + buf;
}

// --- Native post (frame-map section 17, "Native composite"; docs/native-renderer/lighting-formulas.md, "The post chain") ---
//
// The game ends a frame with a composite (pixel shader 0x37B186F4BD3288F7) that maps its scene target
// to the 8-bit front buffer through a table of 512 floats the CPU rewrites every frame (the exposure:
// a gain by scene luminance), and scan-out then puts the front buffer through the guest's 256-entry
// gamma ramp. A POST FRAME of the native pass (DecideClayPost) does the same in two full-screen steps
// in place of the output mapping, with the frame's own post block (capture::PostBlock) and ramp:
//   L   = lum(scene) = 0.2125 r + 0.7154 g + 0.0721 b        scene: the linear target, read as PostScene
//   e   = c77.y * T[texel(c46.x * L)]                         the table at the NEAREST texel (PostTableTexel)
//   c   = sat(e * (L + sat(c47.x * L) * (scene - L)))
//   b   = sat(bloom * c31.x);  s = c + b * (1 - c)            bloom: the bloom's last target; 0 with bloom_on 0
//   t   = s + c77.x * (s - lum(s));  front = |t| ^ c78.x      into the front target (8 bits)
//   out = ramp[floor(front * 255 + 0.5)] per channel          into the mapped target
// and its lit draws write the game's own scale: the frame's exposure word is the light block's c20.w
// instead of kLitExposure, and its unlit draws write their colour times the frame's unlit scale
// (PostUnlitScale: spec ruling 9), so that they pass the composite as a lit pixel of that value would.
// Every other frame is today's: the exposure kLitExposure, the unlit word 1, shaders::kOutputMapPs.
//
// The table and the ramp travel as arrays in the steps' constants (ClayPostConstants: spec ruling 12),
// read by index: no texture, no filter, the game's point fetch exactly.
//
// Values that are no numbers (plan, review focus 2). The rule of every reader of the linear target:
// a channel that is not above 0 is 0 (a NaN, for which no comparison holds, and a negative value among
// them) and one above 31.875, the largest value of the game's scene target, is 31.875 (an infinity
// among them). With that, and with registers and texels inside the block's bounds, every later term is
// a number: nothing that is no number leaves the composite, and no index leaves the table or the ramp.
// The mirrors below compute in double on the single-precision values a shader gets and are pinned to
// the reference by the numbers of tools/xdk_sigmatch/post_pins.py; like the reference they give black
// for a register or texel that is no number, which MakeClayPostConstants never hands on.

inline constexpr double kPostLum[3] = {0.2125, 0.7154, 0.0721};  // the composite's literal c255 (light_formulas.POST_LUM)
inline constexpr float kPostSceneMax = 31.875f;                  // light_formulas.POST_SCENE_MAX
// The table's texel of scene luminance 1 with the captured c46.x = 0.25 (the unlit scale's), and the
// largest unlit scale: an unlit value is at most 1, so what an unlit draw writes stays within what a lit
// draw may write (kClayLitMax).
inline constexpr uint32_t kPostUnlitTexel = 128;
inline constexpr float kPostUnlitMax = 64.0f;
// The c46.x with which texel 128 is scene luminance 1. A block's rule allows (0, 64]: with another
// value the unlit scale is the gain at scene luminance kPostUnlitCoord / c46.x, and the Post line says
// so (ClayStats::post_coord; review of plan Task 5, Minor 8).
inline constexpr float kPostUnlitCoord = 0.25f;
static_assert(kPostUnlitMax <= kClayLitMax && kPostUnlitTexel < capture::kPostTableTexels &&
              float(kPostUnlitTexel) == kPostUnlitCoord * float(capture::kPostTableTexels));

// A channel of the linear target as the post steps read it (light_formulas.post_scene).
inline double PostScene(float v, float scene_max = kPostSceneMax) {
  if (!(v > 0.0f)) return 0.0;
  return double(v < scene_max ? v : scene_max);
}

// The texel of the 512-texel table a coordinate reads (light_formulas.post_table_texel; controller
// ruling 19): the NEAREST, floor(u * 512 + 0.5) clamped to 511, which is the composite's point fetch
// with its half-texel offset; not floor(u * 512). Texel k serves u in [(k - 0.5) / 512, (k + 0.5) / 512).
// In single precision, as the shader computes it (min(511, uint(512 * saturate(u) + 0.5))); the
// reference computes in double, and the two agree at every pin: u * 512 is exact in a single, and the
// pins lie 5e-4 of a texel from a boundary, eight times a single's step there. A coordinate that is no
// number or not above 0 reads texel 0; one at or beyond the end the last texel.
inline uint32_t PostTableTexel(float u) {
  if (!(u > 0.0f)) return 0;
  const float x = 512.0f * (u < 1.0f ? u : 1.0f) + 0.5f;
  const uint32_t i = uint32_t(x);  // (x is in 0.5 .. 512.5)
  return i < capture::kPostTableTexels - 1 ? i : capture::kPostTableTexels - 1;
}

// The frame's unlit scale (spec ruling 9; light_formulas.post_unlit_scale): 1 / (c77.y * T[128]), the
// reciprocal of the composite's gain at scene luminance 1, at most 64; 64 for a gain that is no number
// or not above 1 / 64. An invalid block has none: 1, the word of a frame without post.
inline float PostUnlitScale(const capture::PostBlock& b) {
  if (!b.valid) return 1.0f;
  const double gain = double(b.composite[4]) * double(b.table[kPostUnlitTexel]);
  return (gain > 1.0 / 64.0 && std::isfinite(gain)) ? float(1.0 / gain) : kPostUnlitMax;
}

// The frame's two words of a post frame (cbuffer Frame's f_exposure and f_post), written over those
// MakeClayFrameConstants gave: the exposure becomes the game's output scale c20.w (`out_scale`, the
// frame's light block's) and the unlit word the block's unlit scale. Only for a lit frame and a valid
// block that pairs with that scale (capture::PostBlockPairs, checked again here), a scale that is a
// usable light value above 0 and an unlit scale in (0, 64]; else false and nothing is touched: the
// frame keeps today's words. So the game's scale never stands without its block.
inline bool SetClayFramePost(ClayFrameConstants& c, const capture::PostBlock& b, float out_scale) {
  if (!c.light_on || !b.valid || !capture::PostBlockPairs(b, out_scale)) return false;
  if (!(out_scale > 0.0f) || !LightValueUsable(out_scale)) return false;
  const float unlit = PostUnlitScale(b);
  if (!(unlit > 0.0f && unlit <= kPostUnlitMax)) return false;
  c.exposure = out_scale;
  c.post = unlit;
  return true;
}

// The post steps' constants (cbuffer Post of shaders::kClayPostCompositePs and kClayPostRampPs,
// register b0 of the post layout; shaders::kClayPostFields is its layout): kClayPostSlots small slots
// of the frame's constant buffer. The bright pass's registers and the blurs' weights are read by the
// bloom's steps (shaders::kClayPostBrightPs, kClayPostBlurPs: plan Task 8), the radial blur's registers
// by the sun layer's (shaders::kClaySunRadialPs: plan Task 14).
struct ClayPostConstants {
  float composite[6];   // 37B1: c31.x, c46.x, c47.x, c77.x, c77.y, c78.x
  float scene_max;      // 31.875: the clamp of every read of the linear target (spec ruling 15)
  float previous;       // the previous frame's share of the front target (plan Task 15; StepClayFront: 0 without one)
  float bright[5];      // EE92: c31.x, c46.x, c47.x, c77.x, c77.y
  uint32_t bloom_on;    // 1: the bloom's steps are drawn and the composite reads their result (plan Task 8)
  uint32_t sun_on;      // 1: the sun layer feeds the bright pass (plan Task 14)
  uint32_t pad;
  float blur[4][4];     // EA83's seven weights in rows 0 and 1 (the centre first), 28DB's in rows 2 and 3
  float radial[4];      // 4BE4: c31.x, c31.y, c46.x, c47.x
  float table[capture::kPostTableTexels / 4][4];  // texel i at [i >> 2][i & 3]
  float ramp[256][4];   // entry i: red, green, blue in 0..1 (the fourth float 0)
};
static_assert(sizeof(ClayPostConstants) == 6288 && offsetof(ClayPostConstants, table) == 144 &&
              offsetof(ClayPostConstants, ramp) == 2192 && sizeof(ClayPostConstants::table) == sizeof(capture::PostBlock::table));

// The scan-out ramp as the pass takes it: 256 entries of red, green, blue, every one a number in [0, 1]
// and no channel decreasing from one entry to the next (native_render_state.h, PostRamp's rule, judged
// again on the floats: a frame whose ramp fails it is drawn without post, "post-ramp").
inline bool PostRampUsable(const float (*ramp)[3]) {
  if (!ramp) return false;
  for (int i = 0; i < 256; ++i) {
    for (int ch = 0; ch < 3; ++ch) {
      if (!(ramp[i][ch] >= 0.0f && ramp[i][ch] <= 1.0f) || (i > 0 && !(ramp[i][ch] >= ramp[i - 1][ch]))) return false;
    }
  }
  return true;
}

// The steps' constants of a frame. False, with `out` all zero, unless: the block is valid; each of its
// composite's and bright pass's registers is inside the bound the block's own rule gives it
// (post_block.h, MakePostBlock: judged again here, so that no value reaches a shader on the strength of
// a flag alone; a value that is no number fails its bound); every texel is in [0, 4); and `ramp` is
// usable (PostRampUsable). `bloom`, `sun`: the switches; the flags are set only with the block's part they need
// (the blurs' weights; the radial blur's registers), whose values are copied when the block has them.
inline bool MakeClayPostConstants(const capture::PostBlock& b, const float (*ramp)[3], bool bloom, bool sun, ClayPostConstants* out) {
  if (!out) return false;
  *out = ClayPostConstants{};
  if (!b.valid || !PostRampUsable(ramp)) return false;
  const float* c = b.composite;
  const float* h = b.bright;
  if (!(c[0] >= 0.0f && c[0] <= 64.0f) || !(c[1] > 0.0f && c[1] <= 64.0f) || !(c[2] >= 0.0f && c[2] <= 1e6f) ||
      !(std::fabs(c[3]) <= 4.0f) || !(c[4] > 0.0f && c[4] <= 64.0f) || !(c[5] > 0.0f && c[5] <= 1.0f)) {
    return false;
  }
  if (!(h[0] >= 0.0f && h[0] <= 64.0f) || !(h[1] >= 0.0f && h[1] <= 64.0f) || !(h[2] >= 1e-3f && h[2] <= 64.0f) ||
      !(std::fabs(h[3]) <= 4.0f) || !(h[4] > 0.0f && h[4] <= 64.0f)) {
    return false;
  }
  for (float texel : b.table) {
    if (!(texel >= 0.0f && texel < 4.0f)) return false;
  }
  ClayPostConstants k{};
  std::memcpy(k.composite, b.composite, sizeof(k.composite));
  std::memcpy(k.bright, b.bright, sizeof(k.bright));
  k.scene_max = kPostSceneMax;
  if (b.parts & capture::kPostPartBlur) {
    // A weight outside 0..1 (or no number) is 0 in the constants, and the bloom is then off for the
    // frame: a kernel short of a tap is not drawn on the strength of the flag.
    bool weights = true;
    for (int s = 0; s < 2; ++s) {
      for (int i = 0; i < 7; ++i) {
        const float w = b.blur[s][i];
        weights = weights && w >= 0.0f && w <= 1.0f;
        k.blur[2 * s + (i >> 2)][i & 3] = (w >= 0.0f && w <= 1.0f) ? w : 0.0f;
      }
    }
    k.bloom_on = (bloom && weights) ? 1u : 0u;
  }
  if (b.parts & capture::kPostPartRadial) {
    bool ok = true;
    for (float v : b.radial) ok = ok && std::isfinite(v);
    if (ok) {
      std::memcpy(k.radial, b.radial, sizeof(k.radial));
      k.sun_on = (sun && k.bloom_on) ? 1u : 0u;
    }
  }
  static_assert(sizeof(k.table) == sizeof(b.table));
  std::memcpy(k.table, b.table, sizeof(k.table));
  for (int i = 0; i < 256; ++i) {
    for (int ch = 0; ch < 3; ++ch) k.ramp[i][ch] = ramp[i][ch];
  }
  *out = k;
  return true;
}
// The small slots the constants take in the frame's constant buffer.
inline constexpr uint32_t kClayPostSlots = (sizeof(ClayPostConstants) + 255) / 256;
static_assert(kClayPostSlots == 25);

// The table coordinate of a scene texel, c46.x * lum(scene as PostScene reads it); 0 with a register
// that is no number.
inline double PostCompositeCoordinate(const float scene[3], const ClayPostConstants& k) {
  if (!std::isfinite(k.composite[1]) || !std::isfinite(k.scene_max)) return 0.0;
  double L = 0.0;
  for (int i = 0; i < 3; ++i) L += PostScene(scene[i], k.scene_max) * kPostLum[i];
  return double(k.composite[1]) * L;
}

// The composite of one pixel (light_formulas.post_composite, with the target's clamp): the front
// target's value before the ramp, three numbers in [0, 1] for any input. `scene`: the linear target's
// texel; `bloom`: the blurred bright pass at the pixel, (0, 0, 0) for none.
inline void PostCompositeMirror(const float scene[3], const float bloom[3], const ClayPostConstants& k, double out[3]) {
  auto sat = [](double x) { return x != x ? 0.0 : (x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x)); };
  out[0] = out[1] = out[2] = 0.0;
  for (float r : k.composite) {
    if (!std::isfinite(r)) return;
  }
  if (!std::isfinite(k.scene_max)) return;
  const double c31x = k.composite[0], c47x = k.composite[2], c77x = k.composite[3], c77y = k.composite[4], c78x = k.composite[5];
  double s[3], L = 0.0;
  for (int i = 0; i < 3; ++i) {
    s[i] = PostScene(scene[i], k.scene_max);
    L += s[i] * kPostLum[i];
  }
  const uint32_t texel = PostTableTexel(float(PostCompositeCoordinate(scene, k)));
  const float stored = k.table[texel >> 2][texel & 3];
  const double T = std::isfinite(stored) ? double(stored) : 0.0;
  const double e = c77y * T;
  const double fade = sat(c47x * L);
  double scr[3], L2 = 0.0;
  for (int i = 0; i < 3; ++i) {
    const double c = sat(e * (L + fade * (s[i] - L)));
    const double b = sat(double(bloom[i]) * c31x);
    scr[i] = c + b * (1.0 - c);
    L2 += scr[i] * kPostLum[i];
  }
  for (int i = 0; i < 3; ++i) {
    const double base = std::fabs(scr[i] + c77x * (scr[i] - L2));
    const double p = (base > 0.0 && std::isfinite(base)) ? std::pow(base, c78x) : 0.0;
    out[i] = sat(std::isfinite(p) ? p : 0.0);
  }
}

// The ramp's entry of a front-target value (light_formulas.post_ramp_index): min(255, floor(v * 255 +
// 0.5)); 0 for a value that is no number or not above 0. In single precision, as the shader computes
// it; an 8-bit level n over 255 reads entry n.
inline uint32_t PostRampIndex(float v) {
  if (!(v > 0.0f)) return 0;
  const float x = (v < 1.0f ? v : 1.0f) * 255.0f + 0.5f;
  const uint32_t i = uint32_t(x);  // (x is in 0.5 .. 255.5)
  return i < 255u ? i : 255u;
}
// One channel through the ramp (light_formulas.post_ramp): `ramp` is that channel's 256 entries.
inline float PostRampMirror(float front, const float ramp[256]) { return ramp[PostRampIndex(front)]; }

// --- Native bloom (frame-map section 17, "Native bloom"; docs/native-renderer/lighting-formulas.md, "The post chain") ---
//
// With fable2_native_bloom on, a post frame whose block carries the blurs' weights (ClayBloom::kOn)
// draws the game's bloom between its scene and its composite, in targets a quarter of the pass's
// LOGICAL size (ledger ruling 6; kPostBloomDivisor):
//   D  = downsample   0.25 * the four scene texels (0, 0), (2, 0), (0, 2), (2, 2) of the pixel's 4 x 4
//                     block, each read as PostScene                           (game: 0x87E85199E1E9B260)
//   P  = bright pass  L = lum(D);  sat(D * sat(L * c77.y - c77.x) / L * c31.x / c47.x); 0 for L = 0
//                     (the sun's two layers, tf1 and tf2, are behind the sun's flag: plan Task 14)  (EE92)
//   Q  = blur along x of P, P = blur along y of Q: 13 point taps at -6..+6 texels, clamped to the edge,
//                     with the frame's weights                                 (EA83, 28DB)
// and the composite's b = sat(bloom * c31.x) reads P linearly at bloom texel coordinate x / 4 + 0.5
// (kPostBloomFetch). D is a float target in the scene's range; P and Q are 10-bit (the game's
// k_2_10_10_10: R10G10B10A2_UNORM, spec ruling 14).
//
// The level a UNORM target stores is written by the step itself (controller ruling 25): the last
// statement of every step into an 8-bit or a 10-bit target is floor(sat(v) * levels + 0.5) / levels
// (PostLevel), with the level count of the target's format in the step's words. Reason: Direct3D lets a
// card convert a float to UNORM within 0.6 of a level; the real card (NVIDIA RTX 5080; review of plan
// Task 6) cuts the float to 12 fractional bits before it rounds, so 0.5 to 0.7% of the pixels of the
// 8-bit writes came out one level low against the reference, which rounds to the nearest. A value that
// is already a level survives that conversion (test case 610). The 10-bit writes are rounded the same
// way for the same reason: the reference rounds each of them to the nearest of 1,023 (post_replay.q10),
// and the game's own targets hold only levels; what the card does at 10 bits was not measured.
//
// kPostDownsampleBias: the downsample's pixel position is the pixel's INTEGRAL position, 0 (not its
// centre, 0.5). Evidence: (1) the emulated picture is the SDK's translation of the game's shader,
// which hands the shader floor(SV_Position.xy) as param_gen (thirdparty/rexglue-sdk/src/graphics/
// pipeline/shader/dxbc_translator.cpp, StartPixelShader: OpRoundNI); (2) offline against the game's
// front buffer of four read-back captures (out/native-evidence/post/t8_bloom_fetch.txt), with the
// composite's fetch as kPostBloomFetch has it: bias 0 fits at a median of 0.565 to 0.578 levels on the
// three bridge captures, bias 0.5 at 0.753 to 0.765, a box of the sixteen texels at 0.608 to 0.620. That the console's
// rasteriser hands the same position is inference. Discovery question 6 (the game's own downsample
// buffer) stays unsettled: that buffer is in no capture.
//
// kPostBloomFetch: the composite samples the bloom at the scene's own interpolated coordinate with
// the fetch's half texel (37B1, instruction 6: tfetch2D tf2, OffsetX = OffsetY = 0.5), which at pixel
// x is bloom texel coordinate x / 4 + 0.5: the scene's fetch (instruction 3) and the bloom's take the
// same interpolated coordinate and the same offset of half a texel of their own texture, so where the
// scene's fetch is the texel centre x + 0.5 of 1120, the bloom's is x / 4 + 0.5 of 280 (what is left as
// inference: that the emulator's fetch offsets are the console's). Not "texel centres aligned", (x + 0.5) / 4, which
// the research's replay and the plan's Task 8 line assumed: against the front buffer the fetch's place
// fits at a median of 0.565 to 0.578 levels where the aligned centres give 0.716 to 0.732 (same file).
inline constexpr uint32_t kPostFrontLevels = 255, kPostBloomLevels = 1023;  // R8G8B8A8_UNORM; R10G10B10A2_UNORM
inline constexpr uint32_t kPostBloomDivisor = 4;
inline constexpr float kPostDownsampleBias = 0.0f;
inline constexpr float kPostBloomFetch = 0.5f;  // in bloom texels
static_assert(kPostDownsampleBias == 0.0f || kPostDownsampleBias == 0.5f);

// The level of a UNORM target with `levels` steps (255, 1023) a value is stored as, written by the
// step itself: min(levels, floor(sat(v) * levels + 0.5)); 0 for a value that is no number or not above
// 0. In single precision, as the shader computes it. With 255 it is PostRampIndex.
inline uint32_t PostLevelIndex(float v, uint32_t levels) {
  if (!(v > 0.0f) || levels == 0) return 0;
  const float x = (v < 1.0f ? v : 1.0f) * float(levels) + 0.5f;
  const uint32_t i = uint32_t(x);  // (x is in 0.5 .. levels + 0.5)
  return i < levels ? i : levels;
}
// The value a step writes for it: that level over the level count (0 without levels: a float target,
// which no step rounds).
inline float PostLevel(float v, uint32_t levels) { return levels ? float(PostLevelIndex(v, levels)) / float(levels) : 0.0f; }

// A picture for the mirrors: w x h texels of three floats (red first), clamped to the edge.
struct PostPicture {
  const float* rgb = nullptr;
  uint32_t w = 0, h = 0;
};
// The texel at (x, y), the coordinates clamped into the picture; (0, 0, 0) of a picture without texels.
inline void PostPictureTexel(const PostPicture& p, int64_t x, int64_t y, float out[3]) {
  out[0] = out[1] = out[2] = 0.0f;
  if (!p.rgb || p.w == 0 || p.h == 0) return;
  const size_t cx = size_t(x < 0 ? 0 : (x >= int64_t(p.w) ? int64_t(p.w) - 1 : x));
  const size_t cy = size_t(y < 0 ? 0 : (y >= int64_t(p.h) ? int64_t(p.h) - 1 : y));
  std::memcpy(out, p.rgb + (cy * p.w + cx) * 3, 3 * sizeof(float));
}
// The picture read linearly at the unnormalised coordinate (px, py), texel i's centre at i + 0.5,
// clamped to the edge (light_formulas.post_texture with linear=True, term for term: a texel whose
// weight is 0 is not read, so a neighbour that is no number does not reach a sample at a centre). A
// coordinate that is no number reads texel 0 of its axis.
inline void PostPictureLinear(const PostPicture& p, double px, double py, double out[3]) {
  out[0] = out[1] = out[2] = 0.0;
  if (!p.rgb || p.w == 0 || p.h == 0) return;
  auto axis = [](double c, uint32_t n, int64_t* i0, int64_t* i1, double* f) {
    if (c != c) {
      *i0 = *i1 = 0;
      *f = 0.0;
      return;
    }
    const double last = double(n) - 1.0;
    double x = c - 0.5;
    x = x < 0.0 ? 0.0 : (x > last ? last : x);
    *i0 = int64_t(x);
    *i1 = *i0 + 1 < int64_t(n) ? *i0 + 1 : int64_t(n) - 1;
    *f = x - double(*i0);
  };
  int64_t x0, x1, y0, y1;
  double fx, fy;
  axis(px, p.w, &x0, &x1, &fx);
  axis(py, p.h, &y0, &y1, &fy);
  float a[3], b[3], c[3], d[3];
  PostPictureTexel(p, x0, y0, a);
  if (fx == 0.0 && fy == 0.0) {
    for (int i = 0; i < 3; ++i) out[i] = double(a[i]);
    return;
  }
  PostPictureTexel(p, x1, y0, b);
  PostPictureTexel(p, x0, y1, c);
  PostPictureTexel(p, x1, y1, d);
  for (int i = 0; i < 3; ++i) {
    const double top = fx != 0.0 ? double(a[i]) + (double(b[i]) - double(a[i])) * fx : double(a[i]);
    const double bottom = fx != 0.0 ? double(c[i]) + (double(d[i]) - double(c[i])) * fx : double(c[i]);
    out[i] = fy != 0.0 ? top + (bottom - top) * fy : top;
  }
}

// The downsample of pixel (x, y) of the quarter-size target (light_formulas.post_downsample): `scene`
// read linearly at P + (2.5, 2.5), (2.5, 0.5), (0.5, 0.5), (0.5, 2.5) with P = 4 * (pixel + bias), each
// sample's channel as PostScene, a quarter of their sum: three numbers in [0, scene_max] for any input.
// `bias`: kPostDownsampleBias in the pass (0: the fetches are four texel centres); a bias that is no
// number counts as 0.
inline void PostDownsampleMirror(const PostPicture& scene, uint32_t x, uint32_t y, float bias, float scene_max, double out[3]) {
  out[0] = out[1] = out[2] = 0.0;
  if (!std::isfinite(scene_max)) return;
  const double b = std::isfinite(bias) ? double(bias) : 0.0;
  const double px = 4.0 * (double(x) + b), py = 4.0 * (double(y) + b);
  static constexpr double kTaps[4][2] = {{2.5, 2.5}, {2.5, 0.5}, {0.5, 0.5}, {0.5, 2.5}};
  double sum[3] = {0.0, 0.0, 0.0};
  for (const auto& tap : kTaps) {
    double t[3];
    PostPictureLinear(scene, px + tap[0], py + tap[1], t);
    // (PostScene on the sample: a sample that is no number is 0; float(t) is exact at a texel centre)
    for (int i = 0; i < 3; ++i) sum[i] += t[i] > 0.0 ? (t[i] < double(scene_max) ? t[i] : double(scene_max)) : 0.0;
  }
  for (int i = 0; i < 3; ++i) out[i] = 0.25 * sum[i];
}

// The bright pass of one texel (light_formulas.post_bright, with the target's clamp): three numbers in
// [0, 1] for any input. `d`: the downsample's texel (read as PostScene); `tf1`, `tf2`: the sun's two
// layers at the texel, which count only with the constants' sun flag (plan Task 14; without it they are
// not read). L = 0 has no scene term (the game's shader multiplies 0 by 1 / 0 there). A register that
// is no number, or a divisor c47.x not above 0, gives black (MakeClayPostConstants hands on neither).
inline void PostBrightMirror(const float d[3], const float tf1[3], const float tf2[3], const ClayPostConstants& k, double out[3]) {
  auto sat = [](double x) { return x != x ? 0.0 : (x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x)); };
  auto fin = [](float x) { return std::isfinite(x) ? double(x) : 0.0; };
  out[0] = out[1] = out[2] = 0.0;
  for (float r : k.bright) {
    if (!std::isfinite(r)) return;
  }
  if (!std::isfinite(k.scene_max)) return;
  const double c31x = k.bright[0], c46x = k.bright[1], c47x = k.bright[2], c77x = k.bright[3], c77y = k.bright[4];
  if (!(c47x > 0.0)) return;
  double s[3], L = 0.0;
  for (int i = 0; i < 3; ++i) {
    s[i] = PostScene(d[i], k.scene_max);
    L += s[i] * kPostLum[i];
  }
  const double gain = L > 0.0 ? sat(L * c77y - c77x) / L * c31x : 0.0;
  for (int i = 0; i < 3; ++i) {
    const double v = k.sun_on ? (s[i] * gain + fin(tf1[i]) * 6.0 * c46x + fin(tf2[i])) / c47x : (s[i] * gain) / c47x;
    out[i] = sat(v);
  }
}

// One blur of pixel (x, y) (light_formulas.post_blur13, with the target's clamp): 13 point taps of
// `src` at -6..+6 texels along x, or along y with `vertical`, clamped to the edge, with the weights of
// the constants' rows (EA83's in rows 0 and 1, 28DB's in rows 2 and 3; the centre first). A tap or a
// weight that is no number counts as 0: three numbers in [0, 1] for any input.
inline void PostBlurMirror(const PostPicture& src, uint32_t x, uint32_t y, const ClayPostConstants& k, bool vertical, double out[3]) {
  auto fin = [](float v) { return std::isfinite(v) ? double(v) : 0.0; };
  const int row = vertical ? 2 : 0;
  double sum[3] = {0.0, 0.0, 0.0};
  for (int tap = -6; tap <= 6; ++tap) {
    const int a = tap < 0 ? -tap : tap;
    const double w = fin(k.blur[row + (a >> 2)][a & 3]);
    float t[3];
    PostPictureTexel(src, int64_t(x) + (vertical ? 0 : tap), int64_t(y) + (vertical ? tap : 0), t);
    for (int i = 0; i < 3; ++i) sum[i] += w * fin(t[i]);
  }
  for (int i = 0; i < 3; ++i) out[i] = sum[i] != sum[i] ? 0.0 : (sum[i] < 0.0 ? 0.0 : (sum[i] > 1.0 ? 1.0 : sum[i]));
}

// The bloom texel coordinate the composite samples at pixel `x` of the scene (either axis): the
// scene's interpolated coordinate x / width, in bloom texels x / kPostBloomDivisor, plus the fetch's
// half texel. And the sample: `bloom` (the blurred bright pass, a quarter of the scene's size) read
// linearly there, clamped to the edge.
constexpr double PostBloomCoordinate(uint32_t x) { return double(x) / double(kPostBloomDivisor) + double(kPostBloomFetch); }
inline void PostBloomFetchMirror(const PostPicture& bloom, uint32_t x, uint32_t y, double out[3]) {
  PostPictureLinear(bloom, PostBloomCoordinate(x), PostBloomCoordinate(y), out);
}

// A post step's own words (cbuffer Step of the post shaders, register b1 of the post layout;
// shaders::kClayPostStepFields is its layout). texel: the reciprocal of the width and height of the
// texture the step addresses by coordinate (the downsample's and the composite's scene), so that no
// shader holds a size (plan, review focus 6). levels: the level count of the step's target, from its
// format (255, 1023), for the write's own rounding (PostLevel); 0 for the downsample's float target.
// offset: the downsample's pixel position (kPostDownsampleBias); the composite's bloom fetch in pixels
// of the scene (kPostBloomFetch bloom texels: 2); 0 in the other steps. dir: a blur's direction in
// texels, (1, 0) along x, (0, 1) along y; (0, 0) in the other steps. last: the source's last texel,
// which a blur's taps are clamped to.
enum class ClayPostStep : uint8_t { kDownsample, kBright, kBlurX, kBlurY, kComposite, kRamp };
struct ClayPostStepConstants {
  float texel[2];
  float levels;
  float offset;
  int32_t dir[2];
  int32_t last[2];
};
static_assert(sizeof(ClayPostStepConstants) == 32);
// `width`, `height`: the size of the step's source (a size of 0 counts as 1). `levels`: of the step's
// target's format.
constexpr ClayPostStepConstants MakeClayPostStep(ClayPostStep step, uint32_t width, uint32_t height, uint32_t levels) {
  const uint32_t w = width ? width : 1, h = height ? height : 1;
  ClayPostStepConstants s{};
  s.texel[0] = 1.0f / float(w);
  s.texel[1] = 1.0f / float(h);
  s.levels = float(levels);
  s.offset = step == ClayPostStep::kDownsample ? kPostDownsampleBias
             : step == ClayPostStep::kComposite ? kPostBloomFetch * float(kPostBloomDivisor)
                                                : 0.0f;
  s.dir[0] = step == ClayPostStep::kBlurX ? 1 : 0;
  s.dir[1] = step == ClayPostStep::kBlurY ? 1 : 0;
  s.last[0] = int32_t(w - 1);
  s.last[1] = int32_t(h - 1);
  return s;
}

// --- The previous-frame blend (sub-project 10, plan Task 15; ledger ruling 7) ---
// The game's last draw of a frame lays the previous frame's final picture over the new one with a
// constant weight of 26 / 255 (pass F, 686D: blend 0x0F0E0F0E with the blend constant 0x3DD0D0D2; frame
// map, "Sun layer discovery"): front = new * (1 - w) + previous * w, in the 8-bit front buffer, and that
// result is the next frame's "previous". The native pass does it in the composite (the RHI has no
// constant blend factor): two front targets, exchanged each post frame; the composite writes its own
// level, mixes it with the other target's texel by the share and writes the level of that
// (PostPreviousMirror). The cost is what ruling 7 allows: one more kept target, one mix, one more
// texture in the composite's table.
// The share (StepClayFront) is the game's weight only with fable2_native_sun on (the switch of phase
// 10c), in a post frame whose predecessor was a post frame; 0 in the first frame after the targets were
// made, after a frame without post, with the switch off, and on the oracle feed (whose picture is held
// against a reference without the blend). With the share 0 the composite writes what it wrote before,
// bit for bit, and with the switch off the second target is never made.
inline constexpr float kPostPreviousWeight = 0.10196079313755035f;  // light_formulas.POST_PREVIOUS_WEIGHT: 26 / 255

// One channel of the front target with the blend (light_formulas.post_previous_blend). composite: the
// level the composite wrote; previous: the other front target's texel; share: the constants' `previous`.
// A share that is not in (0, 1] (0, or no number) gives the composite's value as it is. Otherwise the
// level of `levels` nearest composite + (previous - composite) * share, both values taken into [0, 1]
// first (one that is no number as 0): a number in [0, 1] for any input.
inline double PostPreviousMirror(float composite, float previous, float share, uint32_t levels = kPostFrontLevels) {
  auto unit = [](float v) { return std::isfinite(v) ? (v > 0.0f ? (v < 1.0f ? double(v) : 1.0) : 0.0) : 0.0; };
  const double n = unit(composite);
  if (!(share > 0.0f && share <= 1.0f) || levels == 0) return n;
  const double v = n + (unit(previous) - n) * double(share);
  const double level = std::floor((v > 0.0 ? (v < 1.0 ? v : 1.0) : 0.0) * double(levels) + 0.5);
  return level / double(levels);
}

// The pass's two front targets from one Render to the next. current: the target the last post frame
// wrote; valid: the last Render was a post frame of the scene's own picture, so that target holds the
// previous frame's final picture.
struct ClayFrontState {
  uint32_t current = 0;
  bool valid = false;
};
// The state a Render starts from when the pass was not rendered for the frames before it (`gap`: the
// native view was off, or no scene was there to draw): the targets keep their order, and what they hold is
// seconds old, no previous frame's picture. Without this the first native frame after the view comes back
// mixed a tenth of the picture from before the switch (review of plan Tasks 14 and 15, Minor 1).
constexpr ClayFrontState ClayFrontAfterGap(ClayFrontState s, bool gap) {
  if (gap) s.valid = false;
  return s;
}
// Whether the scene of this Render (its guest frame number, `frame`) comes after a stretch of scenes that
// were never drawn, by the last rendered scene's number (review of plan Tasks 16 and 17, Minor 5: the view's
// own gap flag covers the view off and "no scene"; it does not see published scenes the pass never drew).
// A gap: no scene rendered before; a number that went back; more than kClaySceneGapFrames on (the bound of
// the post block's age, ledger ruling 5: a picture up to four frames old may still be mixed at a tenth, an
// older one is no previous frame's). NOT covered, by intent: a stretch in which the game itself presents
// no frame (the numbers then follow one another, and the game's own blend takes the same old picture).
inline constexpr uint64_t kClaySceneGapFrames = 4;
constexpr bool ClaySceneGap(bool rendered_before, uint64_t last_frame, uint64_t frame) {
  if (!rendered_before || frame < last_frame) return true;
  return frame - last_frame > kClaySceneGapFrames;
}
// What a Render does with them. write: the target the composite writes and the ramp reads. read: the
// other one, read by the composite only with a share above 0. share: the constants' `previous`. next:
// the state after this Render.
struct ClayFrontStep {
  uint32_t write = 0, read = 0;
  float share = 0.0f;
  ClayFrontState next;
};
// post_frame: this Render is a post frame. wanted: fable2_native_sun as it is read now. second: the
// second front target exists. fed: the oracle feed's scene is drawn. Without post nothing is written
// and the next frame has no previous picture. With the switch off, or without the second target, the
// target of before is written again and nothing is read. Otherwise the targets are exchanged, and the
// share is the game's weight when the last Render left a valid picture and this one is not fed.
constexpr ClayFrontStep StepClayFront(ClayFrontState s, bool post_frame, bool wanted, bool second, bool fed) {
  const uint32_t current = s.current & 1u;
  ClayFrontStep step;
  step.write = step.read = current;
  step.next.current = current;
  if (!post_frame) return step;
  if (!wanted || !second) {
    step.next.valid = !fed;
    return step;
  }
  step.write = current ^ 1u;
  step.read = current;
  step.share = (s.valid && !fed) ? kPostPreviousWeight : 0.0f;
  step.next.current = step.write;
  step.next.valid = !fed;
  return step;
}

// The Post line of the status text and the log: the switch, the scene's block, what the pass did (a
// post frame's bloom with it; behind "post-block" of a valid block the pass's own rule that refused it),
// a post frame's two scales (and its c46.x when that is not the 0.25 the unlit scale's texel stands
// for), and the pass's totals since it was made.
inline std::string FormatPostText(const ClayStats& st) {
  if (!st.post_switch) return "Post: off";
  std::string text = "Post: on, block ";
  char buf[512];  // (the line with every count and both floats at their largest is whole: test case 609)
  if (st.post_block) {
    std::snprintf(buf, sizeof(buf), "valid, age %u", st.post_age);
    text += buf;
  } else {
    text += std::string("invalid (") + capture::kPostBlockReason + ": " + capture::PostBlockReasonName(st.post_reason) + ")";
  }
  bool exposure = true;
  switch (st.post) {
    case ClayPost::kDrawn: text += std::string(", drawn, bloom ") + ClayBloomName(st.bloom); break;
    case ClayPost::kNoLight:
      text += ", not drawn (no light)";
      exposure = false;
      break;
    case ClayPost::kBlock:
      text += std::string(", not drawn (") + capture::kPostBlockReason;
      if (st.post_block) text += std::string(": ") + ClayPostFaultName(st.post_fault);
      text += ")";
      break;
    case ClayPost::kRamp: text += std::string(", not drawn (") + kPostRampReason + ")"; break;
    case ClayPost::kObjects: text += std::string(", not drawn (") + kPostObjectsReason + ")"; break;
    case ClayPost::kBuffer:
      text += ", not drawn (no constant buffer)";
      exposure = false;
      break;
    default: text += ", not drawn (off)"; break;
  }
  if (st.post == ClayPost::kDrawn) {
    std::snprintf(buf, sizeof(buf), " | c20.w %.4f, unlit scale %.4f", double(st.post_scale), double(st.post_unlit));
    if (st.post_coord != kPostUnlitCoord) {
      text += buf;
      // (bounded for any float: the line with the largest values is whole, test case 609)
      const double coord = st.post_coord > 0.0f && st.post_coord <= 64.0f ? double(st.post_coord) : 0.0;
      std::snprintf(buf, sizeof(buf), " (c46.x %.4f: the gain at scene luminance %.3f, not 1)", coord,
                    coord > 0.0 ? double(kPostUnlitCoord) / coord : 0.0);
    }
  } else if (exposure) {
    std::snprintf(buf, sizeof(buf), " | today's mapping, exposure %.2f", double(kLitExposure));
  } else {
    std::snprintf(buf, sizeof(buf), " | today's mapping");
  }
  text += buf;
  std::snprintf(buf, sizeof(buf),
                " | frames since start %llu: drawn %llu (bloom %llu, no-weights %llu), %s %llu, %s %llu, %s %llu, no-light %llu, no-buffer %llu",
                static_cast<unsigned long long>(st.post_frames), static_cast<unsigned long long>(st.post_drawn_frames),
                static_cast<unsigned long long>(st.bloom_frames), static_cast<unsigned long long>(st.bloom_noweights_frames),
                capture::kPostBlockReason, static_cast<unsigned long long>(st.post_block_frames), kPostRampReason,
                static_cast<unsigned long long>(st.post_ramp_frames), kPostObjectsReason,
                static_cast<unsigned long long>(st.post_objects_frames), static_cast<unsigned long long>(st.post_nolight_frames),
                static_cast<unsigned long long>(st.post_buffer_frames));
  return text + buf;
}

// --- Native furniture (frame-map section 17, "Scene furniture"; sub-project 10, plan Task 13) ---
// The game's sky furniture in the main scene, drawn again from the frame's quad records
// (capture/quad_record.h): the moon right after the sky step, the glare (the sun's and the moon's)
// after the last record. Each quad is the game's own four corners through the game's own matrix
// (shaders::kClayQuadVs: six vertices by SV_VertexID, the order 0 1 2 1 3 2) and its pixel shader's
// arithmetic (shaders::kClayQuadPs; the mirrors below are pinned to light_formulas.post_moon,
// post_glare and post_quad_position, each machine-checked against the microcode):
//   moon (18D3):   rgb = tex.rgb * c31.rgb * tex.a * c20.w,  alpha = c31.w * tex.a
//   glare (0498):  rgb = tex.rgb * c31.rgb,                  alpha = 1
// with the record's own state: the moon blended by its alpha, alpha-tested (greater than the draw's
// reference) and depth-tested without write; the glare added, without a depth test. What is written
// is a number of the scene's range: (v > 0) ? min(v, 31.875) : 0, the alpha saturated.
// Only in a post frame with a sky, with fable2_native_sun on, and only from a WHOLE set of records:
// a frame whose set is not whole, or in which a record of a kind this build draws cannot be drawn
// as it was captured, draws no furniture at all (ClayFurniture). A quad whose texture cannot be had
// this frame is left out alone and counted ("quad-texture").
// The other kinds of a set (clouds, the sun layer's quads) are later tasks': counted, not drawn.
inline constexpr const char* kQuadTextureReason = "quad-texture";

// Where a kind is drawn by this build: kAfterSky (the moon), kAfterRecords (the glare), kNotDrawn.
enum class ClayQuadPlace : uint8_t { kNotDrawn, kAfterSky, kAfterRecords };
constexpr ClayQuadPlace ClayQuadPlaceOf(capture::PostQuadKind kind) {
  return kind == capture::PostQuadKind::kMoon    ? ClayQuadPlace::kAfterSky
         : kind == capture::PostQuadKind::kGlare ? ClayQuadPlace::kAfterRecords
                                                 : ClayQuadPlace::kNotDrawn;
}

// The state the pass draws a record with (a pipeline of the quad step each), from the record's own
// words; kNone: the pass has no pipeline for them, and the record cannot be drawn.
//   kAlphaDepth: source alpha / inverse source alpha (0x07060706), depth-tested without write. The
//     game's function must be greater-or-equal, which under its reversed depth range (the record
//     was made only under it: capture::MakeQuadRecord) is less-or-equal on the pass's depth.
//   kAdd: one / one (0x01010101), no depth test.
// An alpha test that is on must be "greater" (every captured row); all four colour channels.
enum class ClayQuadState : uint8_t { kNone, kAlphaDepth, kAdd, kCount };
inline constexpr uint32_t kQuadBlendAlpha = 0x07060706u, kQuadBlendAdd = 0x01010101u;
constexpr ClayQuadState QuadDrawState(const capture::QuadRecord& q) {
  const bool test = (q.depth & 2) != 0, write = (q.depth & 4) != 0;
  const uint32_t func = (q.depth >> 4) & 7;
  if ((q.alpha_control & 8) && (q.alpha_control & 7) != uint32_t(capture::Compare::kGreater)) return ClayQuadState::kNone;
  if (q.colour_mask != 0xF) return ClayQuadState::kNone;
  if (q.blend == kQuadBlendAlpha && test && !write && func == uint32_t(capture::Compare::kGreaterEqual)) return ClayQuadState::kAlphaDepth;
  if (q.blend == kQuadBlendAdd && !test) return ClayQuadState::kAdd;
  return ClayQuadState::kNone;
}

// Whether a record's texture is fetched as stored: the four sign fields of its fetch constant (dword 0,
// bits 2 to 9) are 0, as in every captured quad (0x82000002, 0x84000002, 0x88000002). A quad whose texture
// is fetched signed or through the gamma curve (the terrain's base map was one: "The base map's gamma
// sign") is not drawn: the quad step samples a texture as the cache decoded it and applies no such field
// (review of plan Tasks 12 and 13, Minor 9).
inline constexpr uint32_t kFetchSignMask = 0x000003FCu;
constexpr bool QuadTextureUnsigned(const capture::QuadRecord& q) { return (q.fetch[0][0] & kFetchSignMask) == 0; }

// A quad's constants (cbuffer Quad of shaders::kClayQuadVs / kClayQuadPs, register b0 of the quad
// step's layout; shaders::kClayQuadFields is its layout): kClayQuadSlots small slots of the frame's
// constant buffer a quad, kClayQuadFrameSlots a frame (every record of a full set).
struct ClayQuadConstants {
  float corner[4][4];     // the corners' positions: x y z, 1
  float coord[4][4];      // their coordinates: u v, 0, 0
  float view_proj[4][4];  // vertex c0..c3 at the game's call
  float reg[12][4];       // the kind's pixel registers, in the record's order (the generated table's)
  uint32_t kind;          // the formula: capture::PostQuadKind + 1 (1 the moon, 3 the glare)
  uint32_t sampler;       // static sampler of the texture (SamplerIndex)
  uint32_t alpha_test;    // 1: the pixel is dropped unless its alpha is above alpha_ref
  float alpha_ref;
  float fix[2];           // host texture scale (TextureCache::Resolve's uv_fix)
  float scene_max;        // the clamp of what is written (kPostSceneMax)
  uint32_t pad;
};
static_assert(sizeof(ClayQuadConstants) == 416);
inline constexpr uint32_t kClayQuadSlots = (sizeof(ClayQuadConstants) + 255) / 256;
inline constexpr uint32_t kClayQuadFrameSlots = kClayQuadSlots * capture::kMaxQuadRecords;
static_assert(kClayQuadSlots == 2 && kClayQuadFrameSlots == 48);
// The bounds of what a quad's shader is handed (each written so that a value that is no number
// fails it): a colour register's component in [0, 64], the scale in (0, 64].
inline constexpr float kQuadRegisterMax = 64.0f;

// A record's constants. False, with `out` untouched, unless (checked again here, so that no value
// reaches a shader on the strength of the capture's flag alone):
//   - the record is valid, of a kind this build draws, with a state the pass can draw (QuadDrawState);
//   - every corner value and matrix component is finite and within capture::kQuadValueBound;
//   - the moon: c31's four components in [0, 64]; c20.w in (0, 64] and equal to the frame's output
//     scale within 1e-3 of it (`out_scale`: the frame's light block's c20.w, which the lit draws of a
//     post frame write with; a moon written with another scale would be another frame's);
//     the glare: c31.rgb in [0, 64];
//   - an alpha test in use has its reference in [0, 1];
//   - the texture's scale is finite and in (0, 64] in both axes;
//   - the texture is fetched as stored (QuadTextureUnsigned).
inline bool MakeClayQuadConstants(const capture::QuadRecord& q, float out_scale, uint32_t sampler, const float fix[2],
                                  ClayQuadConstants* out) {
  if (!out || !fix || !capture::QuadRecordValid(q) || ClayQuadPlaceOf(q.kind) == ClayQuadPlace::kNotDrawn) return false;
  if (QuadDrawState(q) == ClayQuadState::kNone) return false;
  if (!QuadTextureUnsigned(q)) return false;
  auto fits = [](float v) { return std::fabs(v) <= capture::kQuadValueBound; };
  auto colour = [](float v) { return v >= 0.0f && v <= kQuadRegisterMax; };
  ClayQuadConstants c{};
  for (int i = 0; i < 4; ++i) {
    for (int k = 0; k < 5; ++k) {
      if (!fits(q.corners[i][k])) return false;
    }
    for (int k = 0; k < 4; ++k) {
      if (!fits(q.view_proj[i][k])) return false;
      c.view_proj[i][k] = q.view_proj[i][k];
    }
    c.corner[i][0] = q.corners[i][0];
    c.corner[i][1] = q.corners[i][1];
    c.corner[i][2] = q.corners[i][2];
    c.corner[i][3] = 1.0f;
    c.coord[i][0] = q.corners[i][3];
    c.coord[i][1] = q.corners[i][4];
  }
  if (q.kind == capture::PostQuadKind::kMoon) {
    // (the record's registers: c20, c31)
    const float scale = q.ps[0][3];
    if (!(scale > 0.0f && scale <= kQuadRegisterMax) || !(out_scale > 0.0f && out_scale <= kQuadRegisterMax)) return false;
    if (!(std::fabs(double(scale) - double(out_scale)) <= 1e-3 * double(out_scale))) return false;
    for (int k = 0; k < 4; ++k) {
      if (!colour(q.ps[1][k])) return false;
    }
    std::memcpy(c.reg[0], q.ps[0], sizeof(c.reg[0]));
    std::memcpy(c.reg[1], q.ps[1], sizeof(c.reg[1]));
    for (int k = 0; k < 3; ++k) c.reg[0][k] = 0.0f;  // (c20.xyz: not read by the moon's shader)
  } else if (q.kind == capture::PostQuadKind::kGlare) {
    // (the glare's register: c31; its fourth component is not read)
    for (int k = 0; k < 3; ++k) {
      if (!colour(q.ps[0][k])) return false;
      c.reg[0][k] = q.ps[0][k];
    }
  } else {
    // (no other kind's registers are known here: a kind that is given a place without its own branch is
    // refused, not drawn with the glare's reading of its registers; review of plan Tasks 12 and 13, Minor 3)
    return false;
  }
  const bool test = capture::QuadAlphaTest(q);
  if (test && !(q.alpha_ref >= 0.0f && q.alpha_ref <= 1.0f)) return false;
  if (!(fix[0] > 0.0f && fix[0] <= 64.0f) || !(fix[1] > 0.0f && fix[1] <= 64.0f)) return false;
  c.kind = uint32_t(q.kind) + 1;
  c.sampler = sampler & 3u;
  c.alpha_test = test ? 1u : 0u;
  c.alpha_ref = test ? q.alpha_ref : 0.0f;
  c.fix[0] = fix[0];
  c.fix[1] = fix[1];
  c.scene_max = kPostSceneMax;
  *out = c;
  return true;
}

// The frame's furniture: which records are drawn, each with its state, in the records' order; how
// many are other kinds' (`later`); and whether every record of a kind this build draws can be
// drawn (`drawable`: MakeClayQuadConstants with a unit texture scale). A set with one that cannot is
// not drawn at all. count above capture::kMaxQuadRecords, or a null array with a count: nothing, not
// drawable.
struct ClayQuadItem {
  uint8_t record = 0;  // its index in the scene's quads
  ClayQuadState state = ClayQuadState::kNone;
  ClayQuadPlace place = ClayQuadPlace::kNotDrawn;
};
struct ClayQuadPlan {
  uint32_t count = 0;
  ClayQuadItem items[capture::kMaxQuadRecords];
  uint32_t later = 0;
  bool drawable = true;
};
inline ClayQuadPlan PlanClayQuads(const capture::QuadRecord* quads, size_t count, float out_scale) {
  ClayQuadPlan plan;
  if (count > capture::kMaxQuadRecords || (count && !quads)) {
    plan.drawable = false;
    return plan;
  }
  static constexpr float kUnit[2] = {1.0f, 1.0f};
  for (size_t i = 0; i < count; ++i) {
    const capture::QuadRecord& q = quads[i];
    const ClayQuadPlace place = capture::QuadRecordValid(q) ? ClayQuadPlaceOf(q.kind) : ClayQuadPlace::kNotDrawn;
    if (capture::QuadRecordValid(q) && place == ClayQuadPlace::kNotDrawn) {
      ++plan.later;
      continue;
    }
    ClayQuadConstants unused;
    if (!MakeClayQuadConstants(q, out_scale, 0, kUnit, &unused)) {
      plan = ClayQuadPlan{};
      plan.drawable = false;
      return plan;
    }
    plan.items[plan.count].record = uint8_t(i);
    plan.items[plan.count].state = QuadDrawState(q);
    plan.items[plan.count].place = place;
    ++plan.count;
  }
  return plan;
}

// The mirrors of the quad step's two shaders (pinned to the reference's numbers: test cases 630 on).
// A corner's clip position: (c0 . p1, c1 . p1, c2 . p1, c3 . p1), p1 = (x, y, z, 1) (post_quad_position).
inline void QuadPositionMirror(const float corner[3], const float view_proj[4][4], double out[4]) {
  for (int i = 0; i < 4; ++i) {
    out[i] = double(view_proj[i][0]) * double(corner[0]) + double(view_proj[i][1]) * double(corner[1]) +
             double(view_proj[i][2]) * double(corner[2]) + double(view_proj[i][3]);
  }
}
namespace detail {
inline double QuadFinite(float v) { return std::isfinite(v) ? double(v) : 0.0; }
// What the step writes: a colour channel (v > 0) ? min(v, top) : 0, the alpha saturated; a value
// that is no number is 0.
inline void QuadOut(const double rgb[3], double alpha, double top, double out[4]) {
  for (int i = 0; i < 3; ++i) out[i] = rgb[i] > 0.0 ? std::min(rgb[i], top) : 0.0;
  out[3] = alpha > 0.0 ? std::min(alpha, 1.0) : 0.0;
}
}  // namespace detail
// The moon (post_moon). t: the texel (a channel that is no number reads 0); c31; c20.w. A register
// that is no number gives zeros.
inline void QuadMoonMirror(const float t[4], const float c31[4], float c20w, double out[4]) {
  out[0] = out[1] = out[2] = out[3] = 0.0;
  if (!std::isfinite(c20w) || !std::isfinite(c31[0]) || !std::isfinite(c31[1]) || !std::isfinite(c31[2]) || !std::isfinite(c31[3])) return;
  const double a = detail::QuadFinite(t[3]);
  double rgb[3];
  for (int i = 0; i < 3; ++i) rgb[i] = detail::QuadFinite(t[i]) * double(c31[i]) * a * double(c20w);
  detail::QuadOut(rgb, double(c31[3]) * a, double(kPostSceneMax), out);
}
// The glare (post_glare): tex.rgb * c31.rgb, alpha 1.
inline void QuadGlareMirror(const float t[4], const float c31[4], double out[4]) {
  out[0] = out[1] = out[2] = out[3] = 0.0;
  if (!std::isfinite(c31[0]) || !std::isfinite(c31[1]) || !std::isfinite(c31[2])) return;
  double rgb[3];
  for (int i = 0; i < 3; ++i) rgb[i] = detail::QuadFinite(t[i]) * double(c31[i]);
  detail::QuadOut(rgb, 1.0, double(kPostSceneMax), out);
}
// The alpha test of a record as the step applies it (the shader's discard): kept unless the test is
// on and the alpha is not above the reference (a NaN alpha is not kept).
constexpr bool QuadAlphaKept(bool test, float alpha, float ref) { return !test || alpha > ref; }

// The pass's decision about the frame's furniture (ClayPass::Render), one chain in which the first
// thing missing decides. `sun`: fable2_native_sun as the pass reads it now. `post_frame`: the frame
// is a post frame (ClayPost::kDrawn). `sky`: its sky is drawn (the moon's place is behind it).
// `set`: the scene's quad set (FrameScene::quad_reason). `drawable`: every record of a kind this
// build draws can be drawn (ClayQuadPlan::drawable). `objects`: the quad step's objects exist or
// may still be made; false once making them has failed.
struct ClayFurnitureInputs {
  bool sun = false, post_frame = false, sky = false;
  capture::QuadSetReason set = capture::QuadSetReason::kOff;
  bool drawable = false, objects = false;
};
constexpr ClayFurniture DecideClayFurniture(const ClayFurnitureInputs& in) {
  if (!in.sun) return ClayFurniture::kOff;
  if (!in.post_frame) return ClayFurniture::kNoPost;
  if (!in.sky) return ClayFurniture::kNoSky;
  if (in.set == capture::QuadSetReason::kNoDraw) return ClayFurniture::kNoQuads;
  if (in.set != capture::QuadSetReason::kNone) return ClayFurniture::kRecords;
  if (!in.drawable) return ClayFurniture::kState;
  if (!in.objects) return ClayFurniture::kObjects;
  return ClayFurniture::kDrawn;
}
// The pass's frame counts of the furniture, which follow from the frame's verdict alone: one frame
// added. Counted: every frame with the sun's switch on, which is every verdict but kOff. `texture`:
// a drawn frame in which a quad was left out for its texture.
struct ClayFurnitureFrames {
  uint64_t frames = 0, drawn = 0, nopost = 0, nosky = 0, noquads = 0, records = 0, state = 0, objects = 0, texture = 0;
};
inline void CountClayFurnitureFrame(ClayFurniture f, bool texture_missing, ClayFurnitureFrames& n) {
  if (f == ClayFurniture::kOff || size_t(f) >= size_t(ClayFurniture::kCount)) return;
  ++n.frames;
  if (f == ClayFurniture::kDrawn) {
    ++n.drawn;
    if (texture_missing) ++n.texture;
  }
  if (f == ClayFurniture::kNoPost) ++n.nopost;
  if (f == ClayFurniture::kNoSky) ++n.nosky;
  if (f == ClayFurniture::kNoQuads) ++n.noquads;
  if (f == ClayFurniture::kRecords) ++n.records;
  if (f == ClayFurniture::kState) ++n.state;
  if (f == ClayFurniture::kObjects) ++n.objects;
}

// The Furniture line of the status text and the log: the switch, the scene's quad set, what the pass
// did (the quads drawn by kind, those left out for their texture, the records of kinds this build
// does not draw), and the pass's totals since it was made.
inline std::string FormatFurnitureText(const ClayStats& st) {
  if (!st.sun_switch) return "Furniture: off";
  char buf[320];
  std::string text = "Furniture: on, quads ";
  if (st.quad_set == capture::QuadSetReason::kNone) {
    std::snprintf(buf, sizeof(buf), "whole (%u)", st.quad_records);
    text += buf;
  } else {
    text += std::string("none (") + capture::kQuadRecordReason + ": " + capture::QuadSetReasonName(st.quad_set) + ")";
  }
  switch (st.furniture) {
    case ClayFurniture::kDrawn:
      std::snprintf(buf, sizeof(buf), ", drawn: moon %u, glare %u, %s %u, other kinds %u", st.quad_drawn_moon, st.quad_drawn_glare,
                    kQuadTextureReason, st.quad_texture, st.quad_later);
      text += buf;
      break;
    case ClayFurniture::kNoPost: text += ", not drawn (no post frame)"; break;
    case ClayFurniture::kNoSky: text += ", not drawn (no sky)"; break;
    case ClayFurniture::kNoQuads: text += ", not drawn (no quad)"; break;
    case ClayFurniture::kRecords: text += std::string(", not drawn (") + capture::kQuadRecordReason + ")"; break;
    case ClayFurniture::kState: text += std::string(", not drawn (") + capture::kQuadRecordReason + ": a record's state or values)"; break;
    case ClayFurniture::kObjects: text += std::string(", not drawn (") + kPostObjectsReason + ")"; break;
    default: text += ", not drawn (off)"; break;
  }
  std::snprintf(buf, sizeof(buf),
                " | frames since start %llu: drawn %llu (%s %llu), no-post %llu, no-sky %llu, no-quad %llu, %s %llu, state %llu, %s %llu",
                static_cast<unsigned long long>(st.furniture_frames), static_cast<unsigned long long>(st.furniture_drawn_frames),
                kQuadTextureReason, static_cast<unsigned long long>(st.furniture_texture_frames),
                static_cast<unsigned long long>(st.furniture_nopost_frames), static_cast<unsigned long long>(st.furniture_nosky_frames),
                static_cast<unsigned long long>(st.furniture_noquads_frames), capture::kQuadRecordReason,
                static_cast<unsigned long long>(st.furniture_records_frames), static_cast<unsigned long long>(st.furniture_state_frames),
                kPostObjectsReason, static_cast<unsigned long long>(st.furniture_objects_frames));
  return text + buf;
}

// --- Native clouds (frame-map section 17, "Clouds"; sub-project 10, plan Task 16) ---
// The scene's cloud layers (three quads high over the world, pixel shader 61976295517D7015 with vertex shader
// C304C6A1AF18B46F), drawn again from the frame's quad records after the moon, with fable2_native_sun on, in a
// frame whose furniture is drawn. A quad is the game's four corners through the game's matrix, the corner's
// own position passed on as the pixel's world position, and the shader's arithmetic (shaders::kClayCloudPs;
// CloudMirror is pinned to light_formulas.post_cloud, machine-checked against the microcode):
//   t      = tex(uv * c31.xy + c46.xy)                    the layer's texture: colour and density
//   alpha  = t.a * c69.x * (1 - sat(0.001 * (|P.xy - c79.xy| - 1000)))
//   alpha <= 0.001:  rgb = 0
//   else   g  = (t.a one texel on in x less one texel back, the same in y)
//          N  = (g.x, g.y, c70.x) normalised;  Dn = (P - c77.xyz) normalised
//          a1 = sat(-N . c78.xyz);  b1 = sat(-Dn . N);  b2 = sat(Dn . N)
//          l  = b1^2 (1 - t.a) + b2 (1 + (1 - a1)^32)
//          rgb = (l c47.rgb + c71.x) c51.x t.rgb c20.w
// "One texel" is a texel of the texture's BASE level (CloudTexelStep), whatever level the sample itself is
// taken from: that is what the emulator's fetch offset does, and the native picture is measured against
// emulated ones (plan Task 16 as amended; frame map, "Sun layer discovery", "The clouds").
// State, from the record: blended by its alpha (0x07060706), alpha-tested ("greater" than the draw's
// reference, 0.0196 captured), depth-tested (the game's greater-or-equal, less-or-equal on the pass's depth).
// THE NATIVE CLOUDS WRITE NO DEPTH, although the game's do (0x00708766): the sun layer's depth must not hold
// them (the game takes its layer's depth copy before them; with the scene clouds' threshold in the layer's
// depth the beam source keeps 11 pixels of 12,310), and after them the pass draws nothing that is tested
// against their depth but the records the game issued later, which the game's clouds hide only where their
// alpha is above 0.0196 at one sample. So the layer's depth is the same whether it is taken before a late
// record or after the last one (PlanSunDepthPlace), and the clouds need no place of their own in it.
// What is written is a number of the scene's range: (v > 0) ? min(v, 31.875) : 0, the alpha saturated.
inline constexpr float kCloudThin = 0.0010000000474974513f;       // 6197 c254.y: the alpha at or below which the colour is 0
inline constexpr float kCloudFadeRate = 0.0010000000474974513f;   // c254.y again: the distance fade's rate
inline constexpr float kCloudFadeStart = 1000.0f;                 // c254.z
inline constexpr float kCloudPower = 32.0f;                       // c255.x
// The bounds of a cloud record's values (each written so that a value that is no number fails it): the
// tiling and the scroll within 4,096 (as a cloud occluder's), the scales in [0, 64], the normal's third
// component within 4,096, a direction's component within 64, positions within the capture's bound.
inline constexpr float kCloudTilingMax = 4096.0f;
// The record's registers, in the generated table's order (c20, c31, c46, c47, c51, c69, c70, c71, c77, c78, c79).
enum : uint32_t { kCloudC20 = 0, kCloudC31, kCloudC46, kCloudC47, kCloudC51, kCloudC69, kCloudC70, kCloudC71, kCloudC77, kCloudC78, kCloudC79, kCloudRegisters };
// (the twelfth register of the constants holds the base level's texel step: CloudTexelStep)
inline constexpr uint32_t kCloudStepRegister = 11;
static_assert(kCloudRegisters == 11);

// Whether the pass can draw a cloud record as it was captured: blended by its alpha, all four colour
// channels, depth-tested greater-or-equal (with or without the depth write, which the pass does not make),
// the alpha test on with "greater".
constexpr bool CloudDrawState(const capture::QuadRecord& q) {
  const bool test = (q.depth & 2) != 0;
  const uint32_t func = (q.depth >> 4) & 7;
  return q.blend == kQuadBlendAlpha && q.colour_mask == 0xF && test && func == uint32_t(capture::Compare::kGreaterEqual) &&
         (q.alpha_control & 8) != 0 && (q.alpha_control & 7) == uint32_t(capture::Compare::kGreater);
}
// One texel of the base level of a cloud's texture, in the texture's own coordinate: (1 / width, 1 / height)
// by its fetch constant's size field (dword 2: the width less 1 in bits 0 to 12, the height less 1 in bits
// 13 to 25).
inline void CloudTexelStep(const uint32_t fetch[6], float step[2]) {
  step[0] = 1.0f / float((fetch[2] & 0x1FFFu) + 1u);
  step[1] = 1.0f / float(((fetch[2] >> 13) & 0x1FFFu) + 1u);
}
// A cloud record's constants (the quad step's block: shaders::kClayCloudFields is its layout, the scene
// quads' with the texel step in the twelfth register). False, with `out` untouched, unless: the record is
// valid, a cloud, drawable (CloudDrawState), its texture fetched as stored; every corner value and matrix
// component within capture::kQuadValueBound; c20.w in (0, 64] and the frame's output scale within a
// thousandth of it (as the moon's: a cloud written with another scale would be another frame's); the tiling
// and the scroll within 4,096; c47.rgb, c51.x, c69.x and c71.x in [0, 64]; c70.x within 4,096; c77.xyz and
// c79.xy within the capture's bound; c78.xyz within 64; the alpha reference in [0, 1]; the texture's scale
// finite and in (0, 64].
inline bool MakeClayCloudConstants(const capture::QuadRecord& q, float out_scale, uint32_t sampler, const float fix[2],
                                   ClayQuadConstants* out) {
  if (!out || !fix || !capture::QuadRecordValid(q) || q.kind != capture::PostQuadKind::kCloud || q.layer != 0) return false;
  if (!CloudDrawState(q) || !QuadTextureUnsigned(q)) return false;
  auto fits = [](float v) { return std::fabs(v) <= capture::kQuadValueBound; };
  auto scale = [](float v) { return v >= 0.0f && v <= kQuadRegisterMax; };
  auto within = [](float v, float bound) { return std::fabs(v) <= bound; };
  ClayQuadConstants c{};
  for (int i = 0; i < 4; ++i) {
    for (int k = 0; k < 5; ++k) {
      if (!fits(q.corners[i][k])) return false;
    }
    for (int k = 0; k < 4; ++k) {
      if (!fits(q.view_proj[i][k])) return false;
      c.view_proj[i][k] = q.view_proj[i][k];
    }
    c.corner[i][0] = q.corners[i][0];
    c.corner[i][1] = q.corners[i][1];
    c.corner[i][2] = q.corners[i][2];
    c.corner[i][3] = 1.0f;
    c.coord[i][0] = q.corners[i][3];
    c.coord[i][1] = q.corners[i][4];
  }
  const float c20w = q.ps[kCloudC20][3];
  if (!(c20w > 0.0f && c20w <= kQuadRegisterMax) || !(out_scale > 0.0f && out_scale <= kQuadRegisterMax)) return false;
  if (!(std::fabs(double(c20w) - double(out_scale)) <= 1e-3 * double(out_scale))) return false;
  for (int k = 0; k < 2; ++k) {
    if (!within(q.ps[kCloudC31][k], kCloudTilingMax) || !within(q.ps[kCloudC46][k], kCloudTilingMax) || !fits(q.ps[kCloudC79][k])) return false;
  }
  for (int k = 0; k < 3; ++k) {
    if (!scale(q.ps[kCloudC47][k]) || !fits(q.ps[kCloudC77][k]) || !within(q.ps[kCloudC78][k], kQuadRegisterMax)) return false;
  }
  if (!scale(q.ps[kCloudC51][0]) || !scale(q.ps[kCloudC69][0]) || !scale(q.ps[kCloudC71][0]) || !within(q.ps[kCloudC70][0], kCloudTilingMax)) return false;
  if (!(q.alpha_ref >= 0.0f && q.alpha_ref <= 1.0f)) return false;
  if (!(fix[0] > 0.0f && fix[0] <= 64.0f) || !(fix[1] > 0.0f && fix[1] <= 64.0f)) return false;
  // (only the components the shader reads are handed on; the others are 0)
  c.reg[kCloudC20][3] = c20w;
  for (int k = 0; k < 2; ++k) {
    c.reg[kCloudC31][k] = q.ps[kCloudC31][k];
    c.reg[kCloudC46][k] = q.ps[kCloudC46][k];
    c.reg[kCloudC79][k] = q.ps[kCloudC79][k];
  }
  for (int k = 0; k < 3; ++k) {
    c.reg[kCloudC47][k] = q.ps[kCloudC47][k];
    c.reg[kCloudC77][k] = q.ps[kCloudC77][k];
    c.reg[kCloudC78][k] = q.ps[kCloudC78][k];
  }
  c.reg[kCloudC51][0] = q.ps[kCloudC51][0];
  c.reg[kCloudC69][0] = q.ps[kCloudC69][0];
  c.reg[kCloudC70][0] = q.ps[kCloudC70][0];
  c.reg[kCloudC71][0] = q.ps[kCloudC71][0];
  CloudTexelStep(q.fetch[0], c.reg[kCloudStepRegister]);
  c.kind = uint32_t(q.kind) + 1;
  c.sampler = sampler & 3u;
  c.alpha_test = 1u;
  c.alpha_ref = q.alpha_ref;
  c.scene_max = kPostSceneMax;  // (the scene's clamp, as the scene quads')
  c.fix[0] = fix[0];
  c.fix[1] = fix[1];
  *out = c;
  return true;
}

// The frame's clouds: the cloud records of the set, in the records' order, and whether every one of them
// can be drawn (MakeClayCloudConstants with a unit texture scale). One that cannot: none is drawn. count
// above capture::kMaxQuadRecords, or a null array with a count: nothing, not drawable.
struct ClayCloudPlan {
  uint32_t count = 0;
  uint8_t records[capture::kMaxQuadRecords] = {};
  bool drawable = true;
};
inline ClayCloudPlan PlanClayClouds(const capture::QuadRecord* quads, size_t count, float out_scale) {
  ClayCloudPlan plan;
  if (count > capture::kMaxQuadRecords || (count && !quads)) {
    plan.drawable = false;
    return plan;
  }
  static constexpr float kUnit[2] = {1.0f, 1.0f};
  for (size_t i = 0; i < count; ++i) {
    const capture::QuadRecord& q = quads[i];
    if (!capture::QuadRecordValid(q) || q.kind != capture::PostQuadKind::kCloud) continue;
    ClayQuadConstants unused;
    if (!MakeClayCloudConstants(q, out_scale, 0, kUnit, &unused)) {
      plan = ClayCloudPlan{};
      plan.drawable = false;
      return plan;
    }
    plan.records[plan.count++] = uint8_t(i);
  }
  return plan;
}

// The verdict (ClayPass::Render), the first thing missing deciding: the frame's furniture drawn; a cloud
// record in the set; every one drawable; the clouds' objects, asked last; every cloud's texture; and, of a
// frame that would draw them, whether the oracle feed's sun part took their slots (`feed`).
struct ClayCloudInputs {
  bool furniture = false, any = false, drawable = false, objects = false, textures = false, feed = false;
};
constexpr ClayClouds DecideClayClouds(const ClayCloudInputs& in) {
  if (!in.furniture) return ClayClouds::kOff;
  if (!in.drawable) return ClayClouds::kState;
  if (!in.any) return ClayClouds::kNone;
  if (!in.objects) return ClayClouds::kObjects;
  if (!in.textures) return ClayClouds::kTexture;
  if (in.feed) return ClayClouds::kFeed;
  return ClayClouds::kDrawn;
}
static_assert(DecideClayClouds(ClayCloudInputs{}) == ClayClouds::kOff);
constexpr const char* ClayCloudsName(ClayClouds c) {
  switch (c) {
    case ClayClouds::kNone: return "no cloud record";
    case ClayClouds::kState: return "a record's state or values";
    case ClayClouds::kObjects: return kPostObjectsReason;
    case ClayClouds::kTexture: return kQuadTextureReason;
    case ClayClouds::kDrawn: return "drawn";
    case ClayClouds::kFeed: return "the oracle feed's sun part";
    default: return "no furniture";
  }
}
// The Clouds line of the status text and the log: what the pass did about the frame's clouds, and the
// pass's totals since it was made by verdict. "Clouds: off" with the sun's switch off.
inline std::string FormatCloudText(const ClayStats& st) {
  if (!st.sun_switch) return "Clouds: off";
  char buf[256];
  std::string text = "Clouds: ";
  if (st.clouds == ClayClouds::kDrawn) {
    std::snprintf(buf, sizeof(buf), "drawn %u", st.cloud_records);
    text += buf;
  } else {
    text += std::string("not drawn (") + ClayCloudsName(st.clouds) + ")";
  }
  std::snprintf(buf, sizeof(buf), " | frames since start %llu: drawn %llu", static_cast<unsigned long long>(st.cloud_frames.frames),
                static_cast<unsigned long long>(st.cloud_frames.by[size_t(ClayClouds::kDrawn)]));
  text += buf;
  for (ClayClouds c : {ClayClouds::kNone, ClayClouds::kState, ClayClouds::kObjects, ClayClouds::kTexture}) {
    std::snprintf(buf, sizeof(buf), ", %s %llu", ClayCloudsName(c), static_cast<unsigned long long>(st.cloud_frames.by[size_t(c)]));
    text += buf;
  }
  // (a run without the oracle feed keeps the line it had)
  if (const uint64_t fed = st.cloud_frames.by[size_t(ClayClouds::kFeed)]) {
    std::snprintf(buf, sizeof(buf), ", %s %llu", ClayCloudsName(ClayClouds::kFeed), static_cast<unsigned long long>(fed));
    text += buf;
  }
  return text;
}

// The cloud's mirror (post_cloud). t: the texel; n: the alpha of its four neighbours, a texel of the base
// level on in x, back in x, on in y, back in y; P: the pixel's world position; the registers as the record
// names them. out: rgb and alpha as the step writes them. A register or position that is no number gives
// zeros; a texel's channel that is no number reads 0.
inline void CloudMirror(const float t[4], const float n[4], const float P[3], float c20w, const float c47[3], float c51x, float c69x,
                        float c70x, float c71x, const float c77[3], const float c78[3], const float c79[2], double out[4]) {
  out[0] = out[1] = out[2] = out[3] = 0.0;
  const float all[] = {P[0], P[1], P[2], c20w, c47[0], c47[1], c47[2], c51x, c69x, c70x, c71x, c77[0], c77[1], c77[2],
                       c78[0], c78[1], c78[2], c79[0], c79[1]};
  for (float v : all) {
    if (!std::isfinite(v)) return;
  }
  const double a = detail::QuadFinite(t[3]);
  const double ex = double(P[0]) - double(c79[0]), ey = double(P[1]) - double(c79[1]);
  const double reach = double(kCloudFadeRate) * (std::sqrt(std::fabs(ex * ex + ey * ey)) - double(kCloudFadeStart));
  const double fade = 1.0 - (reach > 0.0 ? (reach < 1.0 ? reach : 1.0) : 0.0);
  const double alpha = a * double(c69x) * fade;
  double rgb[3] = {0.0, 0.0, 0.0};
  if (alpha > double(kCloudThin)) {
    const double gx = detail::QuadFinite(n[0]) - detail::QuadFinite(n[1]);
    const double gy = detail::QuadFinite(n[2]) - detail::QuadFinite(n[3]);
    const double n2 = std::fabs(double(c70x) * double(c70x) + (gx * gx + gy * gy));
    const double inv = n2 > 0.0 ? 1.0 / std::sqrt(n2) : 0.0;
    const double N[3] = {gx * inv, gy * inv, double(c70x) * inv};
    const double d[3] = {double(P[0]) - double(c77[0]), double(P[1]) - double(c77[1]), double(P[2]) - double(c77[2])};
    const double d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    const double dk = (d2 > 0.0 && std::isfinite(d2)) ? 1.0 / std::sqrt(d2) : 0.0;
    const double Dn[3] = {d[0] * dk, d[1] * dk, d[2] * dk};
    auto sat = [](double v) { return v > 0.0 ? (v < 1.0 ? v : 1.0) : 0.0; };
    const double a1 = sat(-(N[0] * double(c78[0]) + N[1] * double(c78[1]) + N[2] * double(c78[2])));
    const double towards = Dn[0] * N[0] + Dn[1] * N[1] + Dn[2] * N[2];
    const double b1 = sat(-towards), b2 = sat(towards);
    const double base = std::fabs(1.0 - a1);
    const double lobe = base > 0.0 ? std::pow(base, double(kCloudPower)) : 0.0;
    const double l = b1 * b1 * (1.0 - a) + (b2 + b2 * lobe);
    for (int i = 0; i < 3; ++i) {
      const double v = (l * double(c47[i]) + double(c71x)) * double(c51x) * detail::QuadFinite(t[i]) * double(c20w);
      rgb[i] = std::isfinite(v) ? v : 0.0;
    }
  }
  detail::QuadOut(rgb, alpha, double(kPostSceneMax), out);
}

// --- Native sun layer (frame-map section 17, "Sun layer"; sub-project 10, plan Task 14) ---
//
// After its main scene the game draws a "sun layer" at half the scene's size and feeds it to the bloom's
// bright pass (frame map, "Sun layer discovery": the graph by the resolves' addresses):
//   depth    the scene's mid-frame depth copy at every second texel (5D67), then into it, colour mask 0, the
//            three cloud quads where their alpha is above the draw's reference (3FF7; 0.149 captured), the
//            far water's cube and the scene's late leaf draws;
//   layer 1  cleared; the beam source (8E4D), depth-tested: tex.rgb * (1 / 6) * c31.rgb;
//   layer 2  cleared; flares (D72F, added: tex.a * c31.rgb), the moon glow (4B58, blended by its alpha,
//            alpha-tested: tex.rgb * 0.2 * tex.a) and the sun disc (3E77, added, alpha-tested: tex.rgb *
//            c31.rgb * tex.a * c20.w), each depth-tested against the same depth;
//   tf1      layer 1 reduced to a quarter of the scene's size (0A96: the mean of four) and blurred radially
//            twice about the sun's point (4BE4: 14 point taps, p_k = c + (uv - c) * s^k, w_k = g^k / 2,
//            s = 1 + (4 / 7) (c47.x - 1), g = 1 + (4 / 7) (c46.x - 1), the sum times 4 / 7);
//   tf2      layer 2 reduced;
// and the bright pass adds tf1 * 6 * c46.x + tf2 (PostBrightMirror, with the sun's flag). The 8-bit
// reduction of a cleared picture the game also makes (C1) has no reader and is not built (ruling 33).
//
// The native layer (ClaySun::kDrawn), with fable2_native_sun on, in a frame whose furniture is drawn (a
// post frame with a sky and a whole set of quad records), whose bloom is on and whose post block has the
// radial blur's registers: the same steps from the frame's own quad records, in targets of the pass's
// LOGICAL size divided by 2 and by 4 (kSunLayerDivisor, kSunSmallDivisor), whatever the scene target's
// sample count or scale becomes.
//
// THE LAYER'S DEPTH IS NOT THE SCENE'S END DEPTH IN THE GAME, and what the native one is must be said
// exactly (review of plan Task 10, Important 1 to 3). The game's: the depth copy taken before the far
// water, one late leaf draw, the dog's fur and the scene's clouds, brought up by the occluders. The
// scene's own clouds write depth with their alpha test at 0.0196; the layer's cloud occluders use the
// same alpha at 0.149, so the layer's depth holds far less cloud than the scene's (with 0.0196 the beam
// source keeps 11 pixels of 12,310). The native one: the pass's depth as it is BEFORE the first record the
// game issued after its depth copy (PlanSunDepthPlace: the frame's tree occluders name those records; the
// colour pass is interrupted there for the layer's depth step), or, in a frame without such a record, after
// the scene's last record. The first writing of this comment took the depth after the last record in every
// frame, "the late leaves among them": that put a tree the game shows at half, and keeps out of its copy,
// whole into the layer's depth (review of plan Tasks 14 and 15, Important 2). No step of the pass writes
// depth after the records: the sky step, the moon, the glare and the scene's clouds test without writing.
// The depth is read at the texel
// (ratio * x, ratio * y) by point (SunDepthTexel: the game's every second texel of its 1120 x 720 copy,
// the top left texel of the block at another scale), then the frame's cloud-occluder records drawn into
// it by 3FF7's rule with the record's own reference. What the native depth lacks against the game's:
// the far lake (2B09 is not drawn natively, so the layer sees the lake's bed, not its surface) and
// whatever the pass does not draw (the distant trees: beams pass where the game's far woods stand).
// Direction: the game tests greater-or-equal on its reversed window depth 1 - z / w; the pass's depth
// holds z / w (0 near, 1 far), on which the same test is less-or-equal (SunDepthPasses).
//
// Every write into a 10-bit target is a level of 1,023, rounded by the step itself (controller ruling
// 25; PostLevel). The one exception is the moon glow's blend (source alpha / inverse source alpha into
// layer 2): its result is the card's to round, the RHI having no way to round after a blend.
inline constexpr const char* kSunLayerReason = "sun-layer";
inline constexpr uint32_t kSunLayerDivisor = 2, kSunSmallDivisor = 4;
inline constexpr uint32_t kSunLevels = 1023;  // R10G10B10A2_UNORM, the game's k_2_10_10_10
// The shaders' own literals, single-precision as the microcode holds them (captured "ps_lit" rows;
// tools/xdk_sigmatch/post_pins.py makes the pins with the same floats).
inline constexpr float kSunBeamScale = 0.1666666716337204f;  // 8E4D c255.x: 1 / 6
inline constexpr float kSunGlowScale = 0.20000000298023224f;  // 4B58 c255.x
inline constexpr float kSunFadeStart = 1000.0f, kSunFadeRate = 0.0010000000474974513f;  // 3FF7 c255.x, c255.z
inline constexpr uint32_t kSunRadialTaps = 14;
inline constexpr double kSunRadialNorm = 4.0 / 7.0;  // 4BE4's literal: of the two factors and of the sum

// Where a layer quad's pixels are sampled (controller ruling 38; review of plan Tasks 14 and 15, Important 1).
// The game draws the layer with PA_SU_VTX_CNTL = 4 (register 0x2302, bit 0 clear: a pixel is sampled at its
// integral window position; read from the command words of captures 052158 and 093600 at the cloud
// occluders, the beam source, the moon glow and the sun disc). Direct3D samples a pixel at its centre. So the
// layer's quads are drawn with a viewport whose origin is (0.5, 0.5), as the shadow maps are
// (kShadowViewportOrigin): a clip position lands half a pixel further right and down, and the centre of
// native pixel i is the game's window position i (SunQuadGameSample). The reference's rasteriser samples at
// PIXEL_CENTRE = 0 (tools/xdk_sigmatch/post_sun_layer.py); tests/test_post_literals.py holds the two together.
// The full-screen steps (depth, reduction, radial blur) address their sources by floor(SV_Position) and are
// drawn with the origin at 0: they were the reference's already.
inline constexpr float kSunQuadViewportOrigin = 0.5f;
// The window coordinate of a normalised device coordinate on a layer target of `size` pixels along that axis
// under the quads' viewport (`down`: the y axis, which runs down the window).
inline double SunQuadWindow(double ndc, uint32_t size, bool down) {
  return ((down ? -ndc : ndc) * 0.5 + 0.5) * double(size) + double(kSunQuadViewportOrigin);
}
// The game's window position that native pixel `pixel`'s sample point (its centre) stands for.
inline double SunQuadGameSample(uint32_t pixel) { return double(pixel) + 0.5 - double(kSunQuadViewportOrigin); }

// The depth test of a layer quad as the pass makes it: the quad's z / w against the layer's depth,
// less-or-equal (the game's greater-or-equal under its reversed range). A NaN on either side fails.
constexpr bool SunDepthPasses(float quad, float stored) { return quad <= stored; }

// The scene depth's texel a texel of the layer's depth takes (either axis): `ratio` times its position,
// by point, never beyond `last`. ratio: the scene depth's size over the layer's (2 today: the game's
// every second texel); 0 counts as 1.
constexpr uint32_t SunDepthTexel(uint32_t x, uint32_t ratio, uint32_t last) {
  const uint64_t at = uint64_t(x) * (ratio ? ratio : 1);
  return at < last ? uint32_t(at) : last;
}
// The layer's depth of a scene depth value: clamped to [0, 1]; a value that is no number is 0, the
// nearest depth, behind which nothing of the layer is drawn (fail closed: no light where the depth is
// not known).
inline float SunDepthValue(float d) { return d > 0.0f ? (d < 1.0f ? d : 1.0f) : 0.0f; }

// The radial blur's registers as the pass hands them to its shader (ClayPostConstants::radial: c31.x,
// c31.y, c46.x, c47.x): the capture's rule judged again (post_block.h, SetPostRadial): the centre's two
// coordinates in [-4, 4], the two factors in (0, 1]. Written so that a value that is no number fails.
inline bool SunRadialUsable(const float radial[4]) {
  return radial && std::fabs(radial[0]) <= 4.0f && std::fabs(radial[1]) <= 4.0f && radial[2] > 0.0f && radial[2] <= 1.0f &&
         radial[3] > 0.0f && radial[3] <= 1.0f;
}

// The 14 taps of one radial pass at the normalised coordinate (u, v) (light_formulas.post_radial_taps):
// tap k at c + (uv - c) * s^k with the weight g^k / 2. With registers that are not usable: no tap
// (every weight 0, every place the origin).
struct SunRadialTap {
  double u = 0.0, v = 0.0, w = 0.0;
};
inline void SunRadialTaps(double u, double v, const float radial[4], SunRadialTap taps[kSunRadialTaps]) {
  for (uint32_t k = 0; k < kSunRadialTaps; ++k) taps[k] = SunRadialTap{};
  if (!SunRadialUsable(radial) || !std::isfinite(u) || !std::isfinite(v)) return;
  const double cx = radial[0], cy = radial[1];
  const double s = (double(radial[3]) - 1.0) * kSunRadialNorm + 1.0, g = (double(radial[2]) - 1.0) * kSunRadialNorm + 1.0;
  for (uint32_t k = 0; k < kSunRadialTaps; ++k) {
    const double sk = std::pow(s, double(k));
    taps[k].u = cx + (u - cx) * sk;
    taps[k].v = cy + (v - cy) * sk;
    taps[k].w = 0.5 * std::pow(g, double(k));
  }
}
// The texel of an `n`-texel axis a tap's coordinate reads: the point fetch with its half texel,
// floor(min(max(c * n + 0.5, 0), n - 1)); texel 0 for a coordinate that is no number.
inline uint32_t SunRadialTexel(double c, uint32_t n) {
  if (n == 0) return 0;
  const double p = c * double(n) + 0.5;
  if (!(p > 0.0)) return 0;
  const double last = double(n) - 1.0;
  return uint32_t(p < last ? p : last);
}
// One radial pass of pixel (x, y) of a target the source's size (light_formulas.post_radial, with the
// target's clamp): the pixel's coordinate is (x / w, y / h), the sum of the 14 taps times 4 / 7. A tap's
// channel that is no number counts as 0: three numbers in [0, 1] for any input.
inline void SunRadialMirror(const PostPicture& src, uint32_t x, uint32_t y, const float radial[4], double out[3]) {
  out[0] = out[1] = out[2] = 0.0;
  if (!src.rgb || src.w == 0 || src.h == 0) return;
  SunRadialTap taps[kSunRadialTaps];
  SunRadialTaps(double(x) / double(src.w), double(y) / double(src.h), radial, taps);
  double sum[3] = {0.0, 0.0, 0.0};
  for (const SunRadialTap& tap : taps) {
    float t[3];
    PostPictureTexel(src, int64_t(SunRadialTexel(tap.u, src.w)), int64_t(SunRadialTexel(tap.v, src.h)), t);
    for (int i = 0; i < 3; ++i) sum[i] += tap.w * (std::isfinite(t[i]) ? double(t[i]) : 0.0);
  }
  for (int i = 0; i < 3; ++i) {
    const double v = sum[i] * kSunRadialNorm;
    out[i] = v != v ? 0.0 : (v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v));
  }
}

// The reduction of pixel (x, y) of a target 1 / ratio of the source's size (light_formulas.post_reduce:
// one linear fetch at the corner the block's middle four texels share, ratio * pixel + ratio / 2; with
// the game's ratio 2 the mean of the pixel's four texels), clamped to [0, 1]; a sample that is no
// number is 0. ratio 0 counts as 1.
inline void SunReduceMirror(const PostPicture& src, uint32_t x, uint32_t y, uint32_t ratio, double out[3]) {
  const double r = double(ratio ? ratio : 1);
  PostPictureLinear(src, r * double(x) + 0.5 * r, r * double(y) + 0.5 * r, out);
  for (int i = 0; i < 3; ++i) out[i] = out[i] > 0.0 ? (out[i] < 1.0 ? out[i] : 1.0) : 0.0;
}

// A step of the layer that is drawn as a post step (the post layout, cbuffer Step's eight words):
//   kDepth   the layer's depth from the scene's: `dir` the size ratio, `last` the scene depth's last texel;
//   kReduce  a picture at 1 / ratio of its size: `texel` the source's reciprocal size, `dir` the ratio,
//            `levels` 1,023;
//   kRadial  one radial pass: `texel` the reciprocal size and `last` the last texel of the source (which
//            is the target's size), `levels` 1,023.
// Sizes that do not divide (or a zero) give a ratio of 1.
enum class ClaySunStep : uint8_t { kDepth, kReduce, kRadial };
constexpr ClayPostStepConstants MakeClaySunStep(ClaySunStep step, uint32_t source_w, uint32_t source_h, uint32_t target_w,
                                                uint32_t target_h) {
  const uint32_t w = source_w ? source_w : 1, h = source_h ? source_h : 1;
  const uint32_t rx = target_w && w % target_w == 0 ? w / target_w : 1, ry = target_h && h % target_h == 0 ? h / target_h : 1;
  ClayPostStepConstants s{};
  s.texel[0] = 1.0f / float(w);
  s.texel[1] = 1.0f / float(h);
  s.levels = step == ClaySunStep::kDepth ? 0.0f : float(kSunLevels);
  s.offset = 0.0f;
  s.dir[0] = step == ClaySunStep::kRadial ? 0 : int32_t(rx);
  s.dir[1] = step == ClaySunStep::kRadial ? 0 : int32_t(ry);
  s.last[0] = int32_t(w - 1);
  s.last[1] = int32_t(h - 1);
  return s;
}

// The state the pass draws a layer record with (a pipeline of the layer each), from the record's own
// words; kNone: the pass has no pipeline for them, and the record cannot be drawn. Every state tests the
// depth with the game's greater-or-equal (SunDepthPasses) and a colour state writes none:
//   kReplace  no blend (0x00010001), all four channels: the beam source;
//   kAdd      one / one (0x01010101): the flares, the sun disc;
//   kAlpha    source alpha / inverse source alpha (0x07060706): the moon glow;
//   kDepth    colour mask 0, depth test AND write: the cloud occluders.
// An alpha test that is on must be "greater". The state must be the kind's (a beam source with the
// flares' blend is not drawn).
enum class ClaySunState : uint8_t { kNone, kReplace, kAdd, kAlpha, kDepth, kCount };
inline constexpr uint32_t kQuadBlendReplace = 0x00010001u;
constexpr ClaySunState SunQuadDrawState(const capture::QuadRecord& q) {
  const bool test = (q.depth & 2) != 0, write = (q.depth & 4) != 0;
  const uint32_t func = (q.depth >> 4) & 7;
  if (q.layer != 1 || !test || func != uint32_t(capture::Compare::kGreaterEqual)) return ClaySunState::kNone;
  if ((q.alpha_control & 8) && (q.alpha_control & 7) != uint32_t(capture::Compare::kGreater)) return ClaySunState::kNone;
  switch (q.kind) {
    case capture::PostQuadKind::kCloudOccluder:
      return q.colour_mask == 0 && write ? ClaySunState::kDepth : ClaySunState::kNone;
    case capture::PostQuadKind::kBeamSource:
      return q.colour_mask == 0xF && !write && q.blend == kQuadBlendReplace ? ClaySunState::kReplace : ClaySunState::kNone;
    case capture::PostQuadKind::kFlare:
    case capture::PostQuadKind::kSunDisc:
      return q.colour_mask == 0xF && !write && q.blend == kQuadBlendAdd ? ClaySunState::kAdd : ClaySunState::kNone;
    case capture::PostQuadKind::kMoonGlow:
      return q.colour_mask == 0xF && !write && q.blend == kQuadBlendAlpha ? ClaySunState::kAlpha : ClaySunState::kNone;
    default: return ClaySunState::kNone;
  }
}
// Which of the layer's three targets a state is drawn into: 0 its depth, 1 layer 1, 2 layer 2.
constexpr uint32_t SunQuadTarget(capture::PostQuadKind kind) {
  return kind == capture::PostQuadKind::kCloudOccluder ? 0u : kind == capture::PostQuadKind::kBeamSource ? 1u : 2u;
}

// A layer quad's constants (cbuffer SunQuad of shaders::kClaySunQuadVs / kClaySunQuadPs, register b0 of
// the quad step's layout; shaders::kClaySunQuadFields is its layout): the scene quads' block
// (ClayQuadConstants) with the target's level count where that has the scene's clamp, in the same two
// small slots of the record.
struct ClaySunQuadConstants {
  float corner[4][4];     // the corners' positions: x y z, 1
  float coord[4][4];      // their coordinates: u v, 0, 0
  float view_proj[4][4];  // vertex c0..c3 at the game's call
  float reg[12][4];       // the kind's pixel registers, in the record's order (the generated table's)
  uint32_t kind;          // the formula: capture::PostQuadKind + 1 (4 beam source, 5 flare, 6 moon glow, 7 sun disc, 8 cloud occluder)
  uint32_t sampler;       // static sampler of the texture (SamplerIndex)
  uint32_t alpha_test;    // 1: the pixel is dropped unless its alpha is above alpha_ref
  float alpha_ref;
  float fix[2];           // host texture scale (TextureCache::Resolve's uv_fix)
  float levels;           // the level count of the layer's targets (kSunLevels), for the write's own rounding
  uint32_t pad;
};
static_assert(sizeof(ClaySunQuadConstants) == sizeof(ClayQuadConstants) && offsetof(ClaySunQuadConstants, levels) == 408);
// The bound of a cloud occluder's tiling and scroll (c31.xy, c46.xy; 5 to 7 and below 1 captured): the
// coordinate they make of a corner's (0 to 1) stays far inside what a sampler takes.
inline constexpr float kSunTilingMax = 4096.0f;

// A layer record's constants. False, with `out` untouched, unless (checked again here, as the scene
// quads' are):
//   - the record is valid, of the layer, with a state the pass can draw (SunQuadDrawState);
//   - every corner value and matrix component is finite and within capture::kQuadValueBound;
//   - the beam source and the flares: c31.rgb in [0, 64]; the sun disc: c31.rgb in [0, 64] and c20.w in
//     (0, 64] (1 in every captured row: the register at the draw's call, not the scene's scale); the
//     moon glow reads no register; a cloud occluder: c31.xy and c46.xy within kSunTilingMax, the eye
//     c47.xy within capture::kQuadValueBound, c77.x in [0, 64];
//   - an alpha test in use has its reference in [0, 1]; a cloud occluder has the test on (without it
//     the quad would write its depth everywhere: nothing of the kind was captured);
//   - the texture's scale is finite and in (0, 64] in both axes, and it is fetched as stored
//     (QuadTextureUnsigned).
inline bool MakeClaySunQuadConstants(const capture::QuadRecord& q, uint32_t sampler, const float fix[2], ClaySunQuadConstants* out) {
  if (!out || !fix || !capture::QuadRecordValid(q) || SunQuadDrawState(q) == ClaySunState::kNone) return false;
  if (!QuadTextureUnsigned(q)) return false;
  auto fits = [](float v) { return std::fabs(v) <= capture::kQuadValueBound; };
  auto colour = [](float v) { return v >= 0.0f && v <= kQuadRegisterMax; };
  auto tiling = [](float v) { return std::fabs(v) <= kSunTilingMax; };
  ClaySunQuadConstants c{};
  for (int i = 0; i < 4; ++i) {
    for (int k = 0; k < 5; ++k) {
      if (!fits(q.corners[i][k])) return false;
    }
    for (int k = 0; k < 4; ++k) {
      if (!fits(q.view_proj[i][k])) return false;
      c.view_proj[i][k] = q.view_proj[i][k];
    }
    c.corner[i][0] = q.corners[i][0];
    c.corner[i][1] = q.corners[i][1];
    c.corner[i][2] = q.corners[i][2];
    c.corner[i][3] = 1.0f;
    c.coord[i][0] = q.corners[i][3];
    c.coord[i][1] = q.corners[i][4];
  }
  const bool test = capture::QuadAlphaTest(q);
  switch (q.kind) {
    case capture::PostQuadKind::kBeamSource:
    case capture::PostQuadKind::kFlare:
      // (the record's register: c31; its fourth component is not read)
      for (int k = 0; k < 3; ++k) {
        if (!colour(q.ps[0][k])) return false;
        c.reg[0][k] = q.ps[0][k];
      }
      break;
    case capture::PostQuadKind::kMoonGlow: break;
    case capture::PostQuadKind::kSunDisc:
      // (the record's registers: c20, c31; c20.xyz and c31.w are not read)
      if (!(q.ps[0][3] > 0.0f && q.ps[0][3] <= kQuadRegisterMax)) return false;
      c.reg[0][3] = q.ps[0][3];
      for (int k = 0; k < 3; ++k) {
        if (!colour(q.ps[1][k])) return false;
        c.reg[1][k] = q.ps[1][k];
      }
      break;
    case capture::PostQuadKind::kCloudOccluder:
      // (the record's registers: c31 the tiling, c46 the scroll, c47 the eye, c77; two components of each
      // of the first three and one of the last are read)
      if (!test) return false;
      for (int k = 0; k < 2; ++k) {
        if (!tiling(q.ps[0][k]) || !tiling(q.ps[1][k]) || !fits(q.ps[2][k])) return false;
        c.reg[0][k] = q.ps[0][k];
        c.reg[1][k] = q.ps[1][k];
        c.reg[2][k] = q.ps[2][k];
      }
      if (!colour(q.ps[3][0])) return false;
      c.reg[3][0] = q.ps[3][0];
      break;
    default: return false;
  }
  if (test && !(q.alpha_ref >= 0.0f && q.alpha_ref <= 1.0f)) return false;
  if (!(fix[0] > 0.0f && fix[0] <= 64.0f) || !(fix[1] > 0.0f && fix[1] <= 64.0f)) return false;
  c.kind = uint32_t(q.kind) + 1;
  c.sampler = sampler & 3u;
  c.alpha_test = test ? 1u : 0u;
  c.alpha_ref = test ? q.alpha_ref : 0.0f;
  c.fix[0] = fix[0];
  c.fix[1] = fix[1];
  c.levels = float(kSunLevels);
  *out = c;
  return true;
}

// The layer's records of a frame's set: each with its state and its target (SunQuadTarget), in the
// records' order, which is the game's order of drawing; the count by target; and whether every layer
// record can be drawn (`drawable`: MakeClaySunQuadConstants with a unit texture scale). A set with one
// that cannot is not drawn at all (a layer without one of its occluders is worse than no layer). Records
// of the scene's kinds are not the layer's and are skipped. count above capture::kMaxQuadRecords, or a
// null array with a count: nothing, not drawable.
struct ClaySunItem {
  uint8_t record = 0;  // its index in the set
  ClaySunState state = ClaySunState::kNone;
  uint8_t target = 0;  // 0 the layer's depth, 1 layer 1, 2 layer 2
  capture::PostQuadKind kind = capture::PostQuadKind::kCount;
};
struct ClaySunPlan {
  uint32_t count = 0;
  ClaySunItem items[capture::kMaxQuadRecords];
  uint32_t by_target[3] = {};
  bool drawable = true;
};
inline ClaySunPlan PlanClaySunQuads(const capture::QuadRecord* quads, size_t count) {
  ClaySunPlan plan;
  if (count > capture::kMaxQuadRecords || (count && !quads)) {
    plan.drawable = false;
    return plan;
  }
  static constexpr float kUnit[2] = {1.0f, 1.0f};
  for (size_t i = 0; i < count; ++i) {
    const capture::QuadRecord& q = quads[i];
    if (capture::QuadRecordValid(q) && q.layer == 0) continue;
    ClaySunQuadConstants unused;
    if (!MakeClaySunQuadConstants(q, 0, kUnit, &unused)) {
      plan = ClaySunPlan{};
      plan.drawable = false;
      return plan;
    }
    ClaySunItem& item = plan.items[plan.count++];
    item.record = uint8_t(i);
    item.state = SunQuadDrawState(q);
    item.target = uint8_t(SunQuadTarget(q.kind));
    item.kind = q.kind;
    ++plan.by_target[item.target];
  }
  return plan;
}

// The mirrors of the layer quads' formulas (pinned to the reference's numbers: test cases 640 on). What
// a colour kind writes: each channel (v > 0) ? min(v, 1) : 0, the alpha saturated; a texel's channel
// that is no number reads 0; a register that is no number gives zeros.
// The beam source (post_beam_source): tex.rgb * (1 / 6) * c31.rgb, alpha 1.
inline void SunBeamMirror(const float t[4], const float c31[4], double out[4]) {
  out[0] = out[1] = out[2] = out[3] = 0.0;
  if (!std::isfinite(c31[0]) || !std::isfinite(c31[1]) || !std::isfinite(c31[2])) return;
  double rgb[3];
  for (int i = 0; i < 3; ++i) rgb[i] = detail::QuadFinite(t[i]) * double(kSunBeamScale) * double(c31[i]);
  detail::QuadOut(rgb, 1.0, 1.0, out);
}
// A flare (post_flare): tex.a * c31.rgb, alpha 1.
inline void SunFlareMirror(const float t[4], const float c31[4], double out[4]) {
  out[0] = out[1] = out[2] = out[3] = 0.0;
  if (!std::isfinite(c31[0]) || !std::isfinite(c31[1]) || !std::isfinite(c31[2])) return;
  double rgb[3];
  for (int i = 0; i < 3; ++i) rgb[i] = detail::QuadFinite(t[3]) * double(c31[i]);
  detail::QuadOut(rgb, 1.0, 1.0, out);
}
// The moon glow (post_moon_glow): tex.rgb * 0.2 * tex.a, alpha tex.a.
inline void SunGlowMirror(const float t[4], double out[4]) {
  const double a = detail::QuadFinite(t[3]);
  double rgb[3];
  for (int i = 0; i < 3; ++i) rgb[i] = detail::QuadFinite(t[i]) * double(kSunGlowScale) * a;
  detail::QuadOut(rgb, a, 1.0, out);
}
// The sun disc (post_sun_disc): tex.rgb * c31.rgb * tex.a * c20.w, alpha tex.a.
inline void SunDiscMirror(const float t[4], const float c31[4], float c20w, double out[4]) {
  out[0] = out[1] = out[2] = out[3] = 0.0;
  if (!std::isfinite(c20w) || !std::isfinite(c31[0]) || !std::isfinite(c31[1]) || !std::isfinite(c31[2])) return;
  const double alpha = detail::QuadFinite(t[3]);
  double rgb[3];
  for (int i = 0; i < 3; ++i) rgb[i] = detail::QuadFinite(t[i]) * double(c31[i]) * alpha * double(c20w);
  detail::QuadOut(rgb, alpha, 1.0, out);
}
// The alpha a cloud occluder's test sees (post_cloud_occluder): the texel's alpha times c77.x times the
// clouds' distance fade, 1 - sat(0.001 * (|P.xy - eye.xy| - 1000)), P the pixel's world position (the
// corners' positions, interpolated) and the eye the record's c47.xy; in [0, 1]. Anything that is no
// number gives 0: the pixel is not kept, and the layer's depth stays the scene's there.
inline double SunOccluderAlphaMirror(float texel_alpha, const float world[2], const float eye[2], float c77x) {
  if (!std::isfinite(world[0]) || !std::isfinite(world[1]) || !std::isfinite(eye[0]) || !std::isfinite(eye[1]) || !std::isfinite(c77x)) {
    return 0.0;
  }
  const double dx = double(world[0]) - double(eye[0]), dy = double(world[1]) - double(eye[1]);
  const double reach = double(kSunFadeRate) * (std::sqrt(dx * dx + dy * dy) - double(kSunFadeStart));
  const double fade = 1.0 - (reach > 0.0 ? (reach < 1.0 ? reach : 1.0) : 0.0);
  const double a = detail::QuadFinite(texel_alpha) * double(c77x) * fade;
  return a > 0.0 ? (a < 1.0 ? a : 1.0) : 0.0;
}

// Where the layer's depth is taken among the scene's records (draw_record.h, "The sun layer's depth place";
// controller ruling 39). The game's layer starts from a depth copy made in the middle of the scene; a record
// the game issued after that copy must not be in the depth the native layer copies (the one captured: a tree
// faded to half, drawn late, which the native pass draws whole: with it in the depth the beam source loses
// 2,230 of its 7,567 lit texels on run 3's frame and the beams' root a third of its light). The frame's tree
// occluders name those records (FrameScene::layer_depth_place). The plan: usable, and before which record of
// FrameScene::draws the depth is taken (`record`; capture::kNoDepthPlace: after the last one, the frame had
// no occluder). Not usable, and then no layer is drawn (ClaySun::kDepthPlace): an occluder call that named
// no single record, a place without a call, a place beyond the records.
// The game's own tree occluder (F1EB: the late draw into the layer's depth, where its alpha, texture alpha
// times 0.5, reaches 0.5 on a target without multisampling) is drawn again after the cloud occluders
// ("The tree occluders' redraw", below; sub-project 11, phase 11c): the layer's depth as it is taken has
// nothing of the late draw, and the redraw puts into it the part the game's has.
struct SunDepthPlace {
  bool usable = false;
  uint32_t record = capture::kNoDepthPlace;
};
constexpr SunDepthPlace PlanSunDepthPlace(uint32_t occluders, uint32_t failed, uint32_t place, size_t draws) {
  SunDepthPlace plan;
  if (failed != 0) return plan;
  if (occluders == 0) {
    plan.usable = place == capture::kNoDepthPlace;
    return plan;
  }
  if (place == capture::kNoDepthPlace || place >= draws) return plan;
  plan.usable = true;
  plan.record = place;
  return plan;
}
// Whether the layer's depth is taken before the record at `index` of FrameScene::draws is drawn: at the
// first record at or beyond the plan's (the plan's own may not be drawable this frame).
constexpr bool SunDepthBefore(const SunDepthPlace& plan, size_t index) {
  return plan.usable && plan.record != capture::kNoDepthPlace && index >= plan.record;
}
// PlanCoverageOrder's `depth_place`: the index, among the frame's prepared records, of the first one before
// which the layer's depth is taken (SunDepthBefore of its index in FrameScene::draws; `records`: those
// indices, in the frame's order); `count` when the depth is not taken among the records (`early` false: no
// drawn layer, the feed, or a frame without a place) or no prepared record lies at or beyond the place.
constexpr size_t CoverageDepthPlace(const SunDepthPlace& plan, bool early, const uint32_t* records, size_t count) {
  if (!early) return count;
  for (size_t i = 0; i < count; ++i) {
    if (SunDepthBefore(plan, records[i])) return i;
  }
  return count;
}
// The colour loop's steps that are no record's own: the sky step, the early records' tail and the layer's depth.
// Two index spaces meet here (final review of sub-project 11, section 7): `sky_at` and `tail_at` are places among
// the frame's PREPARED records (SkyPlace, PlanCoverageOrder; a place at or beyond their count: after the last),
// while the layer's depth is taken before the first prepared record whose place in FrameScene::draws is at or
// beyond the plan's (`depth`, SunDepthBefore). `sky_drawn`: the frame has a sky step. `depth_early`: the layer's
// depth is taken among the records (a drawn layer of the frame's own scene with a place).
struct ColourOrder {
  bool sky_drawn = false;
  size_t sky_at = 0;
  size_t tail_at = kNoCoverageTail;
  bool depth_early = false;
  SunDepthPlace depth;
  // Where the impostor step is drawn: before the prepared record at this index (PlanImpostorPlace; at or beyond
  // the count: after the last); ~size_t(0): the frame has no impostor step.
  size_t impostors_at = ~size_t(0);
  // Where the water step is drawn: before the prepared record at this index (PlanWaterPlace; at or beyond the
  // count: after the last); ~size_t(0): the frame's water is not drawn.
  size_t water_at = ~size_t(0);
};
// What is due right before the prepared record at `index`, whose place in FrameScene::draws is `record`, in this
// order: the tail (with the sky step first when it is due at the same record: `sky_first`), the layer's depth,
// the sky step. `sky_done`: the sky step was drawn with a tail before (the loop sets it on `sky_first`).
// `depth_taken`: the layer's depth was taken at an earlier record.
// `impostors`: the impostor step, due first of an index's steps (it is opaque and writes depth: PlanImpostorPlace
// puts it at or before the sky step, the tail and the layer's depth).
// `water`: the water step, due LAST of an index's steps: after the tail and after the sky step (the game draws the
// sky, the moon and the clouds before the water; the step blends and writes depth). `depth_after_water`: the
// layer's depth and the water are due at the same index; the loop then takes the depth AFTER the water (and so
// after the sky step, which writes no depth), not before the sky as in every other frame: the game's layer sees
// the lake's level, and with the dead draw left out the surface is what holds it in the native depth.
struct ColourDue {
  bool sky_first = false, tail = false, layer_depth = false, sky = false;
  bool impostors = false;
  bool water = false, depth_after_water = false;
};
constexpr ColourDue ColourStepsDue(const ColourOrder& order, size_t index, size_t record, bool sky_done, bool depth_taken) {
  ColourDue due;
  const bool sky_here = order.sky_drawn && index == order.sky_at && !sky_done;
  due.tail = order.tail_at == index;
  due.sky_first = due.tail && sky_here;
  due.layer_depth = order.depth_early && !depth_taken && SunDepthBefore(order.depth, record);
  due.sky = sky_here && !due.sky_first;
  due.impostors = order.impostors_at == index;
  due.water = order.water_at == index;
  due.depth_after_water = due.water && due.layer_depth;
  return due;
}
// After the last of `count` prepared records: the sky step of a frame without a blended record, then a tail
// whose place is after the last record.
constexpr ColourDue ColourStepsAfter(const ColourOrder& order, size_t count) {
  ColourDue due;
  due.sky = order.sky_drawn && order.sky_at >= count;
  due.tail = order.tail_at != kNoCoverageTail && order.tail_at >= count;
  due.impostors = order.impostors_at != ~size_t(0) && order.impostors_at >= count;
  // (after the sky and the tail)
  due.water = order.water_at != ~size_t(0) && order.water_at >= count;
  return due;
}

// --- The tree occluders' redraw (sub-project 11, phase 11c; frame-map section 18, "The tree occluder") ---
// The game draws each late leaf draw once more into its sun layer's depth (pixel shader F1EB at the layer's
// target, colour mask 0, depth test greater-or-equal with write, no cull): the mesh, the transform and the
// wind of the scene's draw (its twin: capture::FindOccluderTwin; FrameScene::layer_twins), with the leaf
// caster's alpha, the texture's times pixel c15.w, under the record's alpha test and alpha-to-mask ON ONE
// SAMPLE (the layer's target has no multisampling: alpha >= 1 - offset / 4, ShadowAlphaToMaskThreshold; the
// captured word 0xAA00001C gives 0.5 at every pixel, and at the fade 0.5 a texel is kept only where the
// filtered texture alpha is exactly 1). The pass does the same with the twin's own record, after the cloud
// occluders and before layer 1: the textured depth-only program (the pre-pass's text; the wind program when
// the twin is drawn moved in the scene, with the same wind words and slot), the layer's viewport with the
// quads' half-pixel origin, tested less-or-equal against the layer's depth (SunDepthPasses) and written.
// Under fable2_native_sun, no switch of its own (ruling 16); a twin that cannot be drawn is left out and
// counted, and the layer is then today's there (fail closed towards the layer of before).
// The twin's alpha scale is the scene shader's (c15.w times c26.w; F1EB has c15.w alone): equal while c26.w
// is 1, as it is in every family A row captured (plan Task 12).
struct SunTreeDraw {
  SunTreeSkip skip = SunTreeSkip::kNone;
  ClayAlpha alpha;  // the root constants' alpha fields of the redraw; the default when skipped
};
// `twin`: the scene's record; `rs`: its decoded state; `textured`: its coordinates and albedo are there this
// frame. The alpha fields: from the albedo's component; the test with the record's function and reference;
// alpha-to-mask with the record's offsets and the sample count's bits 0 (one sample); the record's scale.
inline SunTreeDraw DecideSunTree(const capture::DrawRecord& twin, const capture::RenderState& rs, bool textured) {
  SunTreeDraw d;
  auto skip = [&](SunTreeSkip why) {
    d = SunTreeDraw{};
    d.skip = why;
    return d;
  };
  if (!rs.valid || !rs.alpha_to_mask || (rs.alpha_test && !std::isfinite(rs.alpha_ref))) return skip(SunTreeSkip::kState);
  const capture::Material& m = twin.material;
  bool usable = m.status == capture::MaterialStatus::kTextured && m.alpha.source == capture::AlphaSource::kAlbedo &&
                std::fabs(m.alpha_scale) <= 1024.0f;  // (false for a NaN)
  for (float v : m.uv_xform) usable = usable && std::isfinite(v);
  if (!usable) return skip(SunTreeSkip::kAlpha);
  if (!textured) return skip(SunTreeSkip::kTexture);
  d.alpha.mode = kAlphaFromAlbedo | (uint32_t(m.alpha.component & 3) << 5) | kAlphaToMask |
                 (uint32_t(rs.alpha_to_mask_offsets) << kAlphaToMaskShift);
  if (rs.alpha_test) d.alpha.mode |= (uint32_t(rs.alpha_func) & 7u) | kAlphaTest;
  d.alpha.ref = rs.alpha_test ? rs.alpha_ref : 0.0f;
  d.alpha.scale = m.alpha_scale;
  return d;
}
// The redraw's two pipelines: the clay vertex shader (0) and the wind program (1).
inline constexpr size_t kSunTreePipelines = 2;
constexpr size_t SunTreePipelineSlot(bool wind) { return wind ? 1 : 0; }
// Which of the frame's prepared records a twin's index names: the place in `records` (each prepared
// record's index in FrameScene::draws, in the frame's order) of `twin`; kNoSunTreeRecord when none is it.
inline constexpr size_t kNoSunTreeRecord = ~size_t(0);
constexpr size_t SunTreeRecord(const uint32_t* records, size_t count, uint32_t twin) {
  if (!records) return kNoSunTreeRecord;
  for (size_t i = 0; i < count; ++i) {
    if (records[i] == twin) return i;
  }
  return kNoSunTreeRecord;
}

struct ClaySunInputs {
  bool sun = false, furniture = false, bloom = false, radial = false, quads = false, drawable = false, objects = false, textures = false;
  // (true unless the frame's depth place is not usable: the one input that is there by default, so that a
  // frame without tree occluders, the common one, is judged as before)
  bool depth_place = true;
};
constexpr ClaySun DecideClaySun(const ClaySunInputs& in) {
  if (!in.sun) return ClaySun::kOff;
  if (!in.furniture) return ClaySun::kNoFurniture;
  if (!in.bloom) return ClaySun::kNoBloom;
  if (!in.radial) return ClaySun::kNoRadial;
  if (!in.drawable) return ClaySun::kState;
  if (!in.quads) return ClaySun::kNoQuads;
  if (!in.depth_place) return ClaySun::kDepthPlace;
  if (!in.objects) return ClaySun::kObjects;
  if (!in.textures) return ClaySun::kTexture;
  return ClaySun::kDrawn;
}
constexpr const char* ClaySunName(ClaySun s) {
  switch (s) {
    case ClaySun::kNoFurniture: return "no furniture";
    case ClaySun::kNoBloom: return "no bloom";
    case ClaySun::kNoRadial: return "no radial registers";
    case ClaySun::kNoQuads: return "no layer quad";
    case ClaySun::kState: return "a record's state or values";
    case ClaySun::kObjects: return kPostObjectsReason;
    case ClaySun::kTexture: return kQuadTextureReason;
    case ClaySun::kDepthPlace: return "depth place";
    case ClaySun::kDrawn: return "drawn";
    default: return "off";
  }
}
// The Sun line of the status text and the log: the switch, what the pass did (a drawn layer's quads by
// target and the blur's centre), the share of the previous frame's picture in the front target (plan
// Task 15), and the pass's totals since it was made, every frame that was not drawn under "sun-layer"
// by reason.
inline std::string FormatSunText(const ClayStats& st) {
  if (!st.sun_switch) return "Sun: off";
  char buf[512];
  std::string text = "Sun: on, ";
  if (st.sun == ClaySun::kDrawn) {
    std::snprintf(buf, sizeof(buf), "drawn: occluders %u, layer 1 %u, layer 2 %u, centre (%.4f, %.4f)", st.sun_drawn[0], st.sun_drawn[1],
                  st.sun_drawn[2], std::fabs(st.sun_centre[0]) <= 4.0f ? double(st.sun_centre[0]) : 0.0,
                  std::fabs(st.sun_centre[1]) <= 4.0f ? double(st.sun_centre[1]) : 0.0);
    text += buf;
    // (where the layer's depth was taken, only when that was among the records: a late draw of the game's)
    if (st.sun_depth_record != capture::kNoDepthPlace) {
      std::snprintf(buf, sizeof(buf), ", depth before record %u", st.sun_depth_record);
      text += buf;
    }
    // (the tree occluders' redraw, only in a frame that had a twin: those drawn, and the others by reason)
    if (st.sun_trees) {
      std::snprintf(buf, sizeof(buf), ", tree occluders drawn %u of %u", st.sun_trees_drawn, st.sun_trees);
      text += buf;
      std::string why;
      for (SunTreeSkip s : {SunTreeSkip::kState, SunTreeSkip::kAlpha, SunTreeSkip::kTexture, SunTreeSkip::kRecord, SunTreeSkip::kPipeline}) {
        if (!st.sun_tree_skipped[size_t(s)]) continue;
        std::snprintf(buf, sizeof(buf), "%s%s: %u", why.empty() ? "" : ", ", SunTreeSkipName(s), st.sun_tree_skipped[size_t(s)]);
        why += buf;
      }
      if (!why.empty()) text += " (not drawn {" + why + "})";
    }
  } else {
    text += std::string("not drawn (") + kSunLayerReason + ": " + ClaySunName(st.sun) + ")";
  }
  std::snprintf(buf, sizeof(buf), ", previous frame's share %.4f", st.post_previous > 0.0f && st.post_previous <= 1.0f ? double(st.post_previous) : 0.0);
  text += buf;
  std::snprintf(buf, sizeof(buf), " | frames since start %llu: drawn %llu, %s {", static_cast<unsigned long long>(st.sun_frames.frames),
                static_cast<unsigned long long>(st.sun_frames.by[size_t(ClaySun::kDrawn)]), kSunLayerReason);
  text += buf;
  bool first = true;
  for (ClaySun s : {ClaySun::kNoFurniture, ClaySun::kNoBloom, ClaySun::kNoRadial, ClaySun::kNoQuads, ClaySun::kState, ClaySun::kObjects,
                    ClaySun::kTexture, ClaySun::kDepthPlace}) {
    std::snprintf(buf, sizeof(buf), "%s%s: %llu", first ? "" : ", ", ClaySunName(s), static_cast<unsigned long long>(st.sun_frames.by[size_t(s)]));
    text += buf;
    first = false;
  }
  return text + "}";
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
  uint32_t base_gamma;      // bits 0 to 2: the base map's r, g, b are fetched with the gamma sign (TerrainBaseGamma)
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
// The slot plan of the frame's buffer (clay_pass.cpp, FinishTerrain; final review, test gap 1: the
// one arithmetic that corrupts memory when wrong, so it is here and under test case 598). Small
// slots in this order: the frame's block (kClayFrameSlots), one per terrain record DRAWN with its
// material (from kClayFrameTerrainSlot0, given out in the records' order), one per lit instanced
// plant (from plant0), the mask step's constants, the sky step's, the post steps' (kClayPostSlots
// slots: ClayPostConstants), the quads' (kClayQuadFrameSlots: kClayQuadSlots for each record of a
// full set, ClayQuadConstants; quad i's from quad0 + kClayQuadSlots * i); then the large slots of the
// lit objects, trunks and leaves (ClayObjectSlotOffset(object0, i)).
// `impostor_slots`: the small slots of the impostor step's constants, after the quads' (kClayImpostorFrameSlots in a
// frame with the impostors' switch not off: record i's at impostor0 + kClayImpostorSlots * i; 0, the default, in
// every other frame, whose plan is then the one of before the impostors, slot for slot).
// `water_slots`: the small slots of the water step, after the impostors' (kClayWaterStepSlots in a frame whose
// water's scene verdict is drawn: "Native water" below; 0, the default, in every other frame, whose plan is then
// the one of before the water, slot for slot).
inline constexpr uint32_t kClayFrameTerrainSlot0 = kClayFrameSlots;
struct ClayFrameSlotPlan {
  uint32_t plant0 = 0, mask = 0, sky = 0, post = 0, quad0 = 0, object0 = 0;
  uint32_t impostor0 = 0;
  uint32_t water0 = 0;
};
constexpr ClayFrameSlotPlan PlanClayFrameSlots(uint32_t terrain_drawn, uint32_t plants, uint32_t impostor_slots = 0, uint32_t water_slots = 0) {
  ClayFrameSlotPlan p;
  p.plant0 = kClayFrameTerrainSlot0 + terrain_drawn;
  p.mask = p.plant0 + plants;
  p.sky = p.mask + 1;
  p.post = p.sky + 1;
  p.quad0 = p.post + kClayPostSlots;
  p.impostor0 = p.quad0 + kClayQuadFrameSlots;
  p.water0 = p.impostor0 + impostor_slots;
  p.object0 = p.water0 + water_slots;
  return p;
}
// The small slots the buffer is sized with, before it is known which terrain records are drawn with
// their material: every terrain record counts, so the plan of any number of them drawn fits
// (PlanClayFrameSlots(drawn, plants).object0 <= this for drawn <= terrain_records).
constexpr uint64_t ClayFrameSmallSlots(uint64_t terrain_records, uint64_t plants, uint64_t impostor_slots = 0, uint64_t water_slots = 0) {
  return uint64_t(kClayFrameTerrainSlot0) + terrain_records + plants + 2 + kClayPostSlots + kClayQuadFrameSlots + impostor_slots + water_slots;
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

// The gamma sign of the base map tf3 (post sub-project, Task 8b; frame-map section 17, "The base map's
// gamma sign"). A fetch constant has a sign field per stored component (dword 0, bits 2 to 9, two bits
// each: 0 unsigned, 1 signed, 2 unsigned biased, 3 gamma), and the fetch's result component i takes the
// field of the stored component its swizzle names (dword 3, three bits a component from bit 1; 4 and 5
// are the constants 0 and 1, which have no sign): the SDK's texture_util::SwizzleSigns. With the field 3
// the console gives the filtered texel through its piecewise linear curve (PwlGammaToLinear), and every
// terrain row of every capture fetches tf3 with 3, 3, 3, 0. TerrainBaseGamma: the result's r, g, b that
// have the gamma sign, as bits 0 to 2 (the terrain shaders' t_base_gamma); kTerrainBaseGammaRefused when
// one of the three is signed or biased, which no captured row has and the pass does not read: such a
// record is not lit (SetClayTerrainLight). Alpha's field is not looked at: the light reads no alpha.
inline constexpr uint32_t kTerrainBaseGammaRefused = 0xFFFFFFFFu;
inline uint32_t TerrainBaseGamma(const uint32_t fetch[6]) {
  uint32_t mask = 0;
  for (uint32_t i = 0; i < 3; ++i) {
    const uint32_t from = (fetch[3] >> (1 + 3 * i)) & 7u;
    if (from & 4u) continue;
    const uint32_t sign = (fetch[0] >> (2 + 2 * from)) & 3u;
    if (sign == 1u || sign == 2u) return kTerrainBaseGammaRefused;
    if (sign == 3u) mask |= 1u << i;
  }
  return mask;
}

// The console's piecewise linear curve from a stored (gamma) value in 0..1 to the linear one, as the
// SDK applies it to a fetch whose sign field is 3 (thirdparty/rexglue-sdk/src/graphics/xenos.cpp,
// PWLGammaToLinear; dxbc_translator_fetch.cpp, TextureSign::kGamma): the mirror of the terrain shaders'
// base_linear and of light_formulas.pwl_gamma_to_linear. With v = 255 g: 1023 * linear is v below 64,
// 2 v - 64 below 96, 4 v - 256 below 192 and 8 v - 1024 from there, each plus the whole part of itself
// times its slope / 1024. A value that is no number gives 0 (every comparison is false for it).
inline float PwlGammaToLinear(float g) {
  g = (g > 0.0f) ? ((g < 1.0f) ? g : 1.0f) : 0.0f;
  float scale, offset;
  if (g >= 96.0f / 255.0f) {
    scale = (g >= 192.0f / 255.0f) ? 8.0f / 1024.0f : 4.0f / 1024.0f;
    offset = (g >= 192.0f / 255.0f) ? -1024.0f : -256.0f;
  } else {
    scale = (g >= 64.0f / 255.0f) ? 2.0f / 1024.0f : 1.0f / 1024.0f;
    offset = (g >= 64.0f / 255.0f) ? -64.0f : 0.0f;
  }
  const float linear = g * (261120.0f * scale) + offset;
  return (linear + std::trunc(linear * scale)) / 1023.0f;
}

// The light's part of a terrain record's constants. `base_fix`, `base_sampler`: the base map's
// (width / host width, height / host height) and static sampler, as the texture cache resolved
// it. False, with `c` untouched (the record is then not lit, and with it its ground square:
// PlanTerrain), unless the inputs are a terrain family's, the record has its base map inputs
// (TerrainMaterialInputs::base_valid), every value is a number of usable size, a layer has its
// facing literal (above 0), and the base map's colour components are unsigned or gamma
// (TerrainBaseGamma, whose mask goes into the block).
inline bool SetClayTerrainLight(ClayTerrainConstants& c, const capture::TerrainMaterialInputs& m,
                                const capture::LightInputs& l, const float base_fix[2], uint32_t base_sampler) {
  const bool layer = l.family == capture::LightFamily::kTerrainLayer;
  if ((!layer && l.family != capture::LightFamily::kTerrainFar) || l.kind != capture::LightVertexKind::kTerrain) {
    return false;
  }
  if (!m.valid || !m.base_valid || !LightValueUsable(l.c11x)) return false;
  if (layer && !(l.facing_scale > 0.0f && LightValueUsable(l.facing_scale))) return false;
  const uint32_t base_gamma = TerrainBaseGamma(m.base_fetch);
  if (base_gamma == kTerrainBaseGammaRefused) return false;
  float uv[4];
  for (int i = 0; i < 4; ++i) {
    uv[i] = m.base_uv[i] * base_fix[i & 1];
    if (!std::isfinite(uv[i])) return false;
  }
  std::copy(std::begin(uv), std::end(uv), std::begin(c.base_uv));
  c.base_sampler = base_sampler;
  c.base_gamma = base_gamma;
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
// less, less-equal) x 3 cull modes = 36 keys, and, with the wind ("Native wind"), the same
// for the wind program's two kinds (flat and textured: a wind record is never a terrain
// record), 18 more: 54, the size of the pre-pass's own
// pipeline cache: it cannot overflow, and the State line's count is the colour
// pass's alone.
inline constexpr size_t kMaxPrepassRestPipelines = 36, kMaxPrepassWindPipelines = 18;
inline constexpr size_t kMaxPrepassPipelines = kMaxPrepassRestPipelines + kMaxPrepassWindPipelines;
inline bool InPrepass(const PipelineKey& colour) { return colour.depth_test != 0 && colour.depth_write != 0; }
inline PipelineKey PrepassKey(const PipelineKey& colour) {
  PipelineKey k;
  k.textured = colour.textured;
  k.color_mask = 0;
  k.depth_test = 1;
  k.depth_write = 1;
  k.depth_func = colour.depth_func;
  k.cull = colour.cull;
  k.wind = colour.wind;  // (the pre-pass draws a wind record with the wind program too: one program for every pass)
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

// A box grown by `reach` in x, y and z (w as it was): the box of a wind record's positions wherever
// a box decides something, the reach an upper bound of how far the wind moves a vertex (foliage spec
// Design 8). An invalid box stays invalid, and a reach that is no number, not finite or below zero
// gives an invalid one: whoever asks then decides as for a record without a box (it is kept).
inline PositionBounds GrowBounds(const PositionBounds& box, float reach) {
  PositionBounds out;
  if (!box.valid || !std::isfinite(reach) || reach < 0.0f) return out;
  out = box;
  for (int k = 0; k < 3; ++k) {
    out.lo[k] = box.lo[k] - reach;
    out.hi[k] = box.hi[k] + reach;
  }
  return out;
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

// --- Native impostors (frame-map section 19; sub-project 12; plan Task 9 fills this section) ---
//
// The flat colour of the far woods' quads while the switch is at `flat` (spec ruling 6): the mean of the texels
// with the alpha bit set over the fourteen pictures of capture native_discovery_20261011_155547 (D1, the bridge),
// frame 1, in the pictures' stored units 0 to 1:
//   python tools/xdk_sigmatch/impostor_pictures.py mean native_discovery_20261011_155547.jsonl 1
//   -> 14 pictures, 172,125 of 917,504 texels covered; the texture's channels (red = bits 0 to 4 of the swapped
//      word) 0.050493 0.250169 0.145376; as the fetch gives it to the shader 0.145376 0.250169 0.050493
//   (out/native-evidence/distant-trees/t7_mean_d1.txt; frame 2: 0.145336 0.250308 0.050491 of 172,022 texels).
// The literal is the SECOND triple: red, green, blue as pixel shader 4B1D receives them, after the fetch
// constant's swizzle (2, 1, 0, 3) (tf0's dword 3 = 0x00A80C14, bits 1 to 12 = kImpostorFlatSwizzle, in every 129F
// row of the five discovery captures). A 1 x 1 picture made of it is sampled WITHOUT a swizzle; a picture decoded
// by Decode1555Level holds the texture's channels and needs the swizzle applied (its red and blue exchanged).
inline constexpr float kImpostorFlatColour[3] = {0.145376f, 0.250169f, 0.050493f};
inline constexpr uint32_t kImpostorFlatSwizzle = 0x60Au;   // x <- 2, y <- 1, z <- 0, w <- 3 (three bits a component)
static_assert(kImpostorFlatSwizzle == capture::kImpostorSwizzle, "the flat colour was taken through another swizzle than the record's fetch has");

// The draw's rules and mirrors (plan Task 9; pure: no binding, target or shader text is in this section).
// The game's two formulas are light_formulas.impostor_position (vertex shader 129F), impostor_pixel /
// impostor_out and impostor_fog (docs/native-renderer/lighting-formulas.md, "The impostors"); the three mirrors
// below are pinned to them by tests/native/impostor_pins.inc (tools/xdk_sigmatch/impostor_pins.py).

// cbuffer Impostor: register b0 of the impostor step's own layout. One a drawn record.
// 224 + 80 + 16 + 16 bytes (recomputed by plan Task 9 from what is carried: the plan's 336 stands).
struct ClayImpostorConstants {
  float vs[capture::kImpostorVsRegisters][4];   // the record's vertex registers, in the table's order
  float ps[capture::kImpostorPsRegisters][4];   // c12, c19, c20, c29, c31
  uint32_t first_quad, quads;                   // the record's words in the frame's buffer; first_quad + quads <= kMaxImpostorQuads
  uint32_t kept;                                // kImpostorKeepFirst, kImpostorKeepFull or kImpostorKeepHalf
  uint32_t sampler;                             // the static sampler of the picture's fetch (0 in flat)
  float alpha_ref, out_scale, fog_on, pad;      // the alpha test's reference; what the colour is multiplied by; 1: the frame's fog is on
};
static_assert(sizeof(ClayImpostorConstants) == 336 && offsetof(ClayImpostorConstants, ps) == 224 &&
              offsetof(ClayImpostorConstants, first_quad) == 304 && offsetof(ClayImpostorConstants, alpha_ref) == 320);
inline constexpr uint32_t kClayImpostorSlots = (sizeof(ClayImpostorConstants) + 255) / 256;
inline constexpr uint32_t kClayImpostorFrameSlots = kClayImpostorSlots * capture::kMaxImpostorRecords;   // only with the switch not off
static_assert(kClayImpostorSlots == 2 && kClayImpostorFrameSlots == 64);

// Which pixels a draw shades (spec ruling 13; `covered`: CoveredSamples under the draw's offsets 0xAA, the game's
// two samples; `alpha_test`: alpha > the draw's reference).
//   kImpostorKeepFirst: the alpha test passed and the first sample is covered. What the step draws, in `flat`
//                       and in `on`, under either value of --fable2_native_coverage: the pixel is drawn whole,
//                       with depth, and the native depth is the game's first sample's, which is what its depth
//                       copy holds.
//   kImpostorKeepFull:  both samples (the part drawn in place under the coverage's switch).
//   kImpostorKeepHalf:  exactly one (the part the coverage's tail would draw at one half).
// Full and half part the first set. No step that draws the half part exists in this sub-project (ledger ruling
// 48: with the game's pictures drawn, every tree-pixel and far-region figure is inside its band without it; what
// that costs is in frame-map section 19, "The draw's rules" and "Limits (12)").
enum : uint32_t { kImpostorKeepFirst = 1, kImpostorKeepFull = 2, kImpostorKeepHalf = 3 };
constexpr bool ImpostorKept(uint32_t kept, bool alpha_test, uint32_t covered) {
  if (!alpha_test) return false;
  return kept == kImpostorKeepFirst ? covered >= 1 : kept == kImpostorKeepFull ? covered == 2 : kept == kImpostorKeepHalf ? covered == 1 : false;
}
inline constexpr uint8_t kImpostorMaskOffsets = 0xAA;   // RB_COLORCONTROL bits 24 to 31 of every captured draw (the record's `state` rule)

// Why a record of the frame is not drawn by the step (the Impostors line and F3: "impostors not drawn {...}").
// kPlace (ledger ruling 38's companion, review of Tasks 8 and 9, Minor 4; not among the spec's strings): the frame's
// impostor records do not lie in one run among the drawn records (ImpostorPlacesOneRun), so no one place is the
// game's for all of them: none is drawn.
// kPictureMode (ledger ruling 54): the run's read-back mode brings pictures that are a frame old or frozen
// (fable2::ImpostorReadback, readback_force_list.h): none is drawn, each record counted by this name.
enum class ClayImpostorSkip : uint8_t { kNone, kPipeline, kSlot, kBuffer, kScale, kPictureBase, kPictureUnread,
                                        kPictureContent, kPictureUpload, kPictureMode, kPlace, kCount };
constexpr const char* ClayImpostorSkipName(ClayImpostorSkip s) {
  constexpr const char* kNames[] = {"none", "pipeline", "slot", "buffer", "scale", "picture: base",
                                    "picture: unread", "picture: content", "picture: upload", "picture: mode", "place"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == size_t(ClayImpostorSkip::kCount));
  return s < ClayImpostorSkip::kCount ? kNames[size_t(s)] : "?";
}
// What is known of a record's picture. kNone: no picture at all (nothing was read for it).
enum class ImpostorPicture : uint8_t { kNone, kUsable, kBase, kUnread, kContent, kUpload, kMode };
// flat: drawn with the flat texture whatever the picture's state. on: drawn only with a usable picture; anything
// else is its reason, and no picture at all is "picture: unread": never a flat quad in a picture's place, which
// would put depth and colour where the game has neither. off: never asked; "pipeline".
// Which of the read-back list's pictures an address is: n where base == first + n * step and n < count, else -1
// (the pass gives fable2::kImpostorPictureBase, kImpostorPictureStep, kImpostorPictures: a record whose tf0 base
// is none of the seeded fourteen has no picture in guest memory, "picture: base").
constexpr int ImpostorPictureSlot(uint32_t base, uint32_t first, uint32_t step, uint32_t count) {
  if (step == 0 || base < first) return -1;
  const uint32_t d = base - first;
  return d % step == 0 && d / step < count ? int(d / step) : -1;
}
constexpr ClayImpostorSkip DecideImpostorPicture(capture::ImpostorMode mode, ImpostorPicture picture) {
  if (mode == capture::ImpostorMode::kFlat) return ClayImpostorSkip::kNone;
  if (mode != capture::ImpostorMode::kOn) return ClayImpostorSkip::kPipeline;
  switch (picture) {
    case ImpostorPicture::kUsable: return ClayImpostorSkip::kNone;
    case ImpostorPicture::kBase: return ClayImpostorSkip::kPictureBase;
    case ImpostorPicture::kContent: return ClayImpostorSkip::kPictureContent;
    case ImpostorPicture::kUpload: return ClayImpostorSkip::kPictureUpload;
    case ImpostorPicture::kMode: return ClayImpostorSkip::kPictureMode;
    default: return ClayImpostorSkip::kPictureUnread;
  }
}

// The constants of one record's draw. False, with `out` untouched, unless: the record is valid; quads > 0 and
// first_quad + quads <= kMaxImpostorQuads (no 32-bit wrap: the words are read from a buffer bound by address,
// which has no bounds check); every component of every register that a formula reads is a number inside the
// table's bound (checked again here, as the capture checked it; a component no formula reads is carried as it is:
// ledger ruling 34); c20.w in (0, 64]; `out_scale` in (0, 64]; in a post frame (`post_frame`) c20.w equal to
// `out_scale` within a thousandth of it (the record is the frame's own: as the moon's quad); alpha_ref in [0, 1];
// `kept` one of the three sets.
inline bool MakeClayImpostorConstants(const capture::ImpostorRecord& r, float out_scale, bool post_frame, bool fog_on, uint32_t kept,
                                      uint32_t sampler, ClayImpostorConstants* out) {
  if (!out || !capture::ImpostorRecordValid(r)) return false;
  if (r.quads == 0 || r.quads > capture::kMaxImpostorQuads || r.first_quad > capture::kMaxImpostorQuads - r.quads) return false;
  for (uint32_t i = 0; i < capture::kImpostorVsRegisters; ++i) {
    for (uint32_t k = 0; k < 4; ++k) {
      if (!capture::detail::ImpostorBounded(0, i, k, r.vs[i][k])) return false;
    }
  }
  for (uint32_t i = 0; i < capture::kImpostorPsRegisters; ++i) {
    for (uint32_t k = 0; k < 4; ++k) {
      if (!capture::detail::ImpostorBounded(1, i, k, r.ps[i][k])) return false;
    }
  }
  const float scale = r.ps[capture::kImpP20][3];
  if (!(scale > 0.0f && scale <= capture::kImpostorScaleMax) || !(out_scale > 0.0f && out_scale <= capture::kImpostorScaleMax)) return false;
  if (post_frame && !(std::fabs(scale - out_scale) <= 1e-3f * out_scale)) return false;
  if (!(r.alpha_ref >= 0.0f && r.alpha_ref <= 1.0f)) return false;
  if (kept != kImpostorKeepFirst && kept != kImpostorKeepFull && kept != kImpostorKeepHalf) return false;
  ClayImpostorConstants c{};
  std::memcpy(c.vs, r.vs, sizeof(c.vs));
  std::memcpy(c.ps, r.ps, sizeof(c.ps));
  c.first_quad = r.first_quad;
  c.quads = r.quads;
  c.kept = kept;
  c.sampler = sampler;
  c.alpha_ref = r.alpha_ref;
  c.out_scale = out_scale;
  c.fog_on = fog_on ? 1.0f : 0.0f;
  *out = c;
  return true;
}

// The shaders' own literals the mirrors use, by name, held to the table's (impostor_table.inc: the vertex shader's
// c254 = (0.5, 0.25, 2, 0) and c255 = (0, 1, -1, 4); the pixel shader's c255 = (0, 1, 0.1, 0.5)). Of the vertex
// shader's, 0.5, 0.25 and 4 only make the corner's number from the vertex index (light_formulas.impostor_corner),
// which a mirror is given; of the pixel shader's, 0.5 is read only under bool b129.
namespace impostor_detail {
constexpr uint32_t LiteralBits(uint32_t kind, uint32_t reg, uint32_t comp) {
  for (const capture::ImpostorTable::Literal& l : capture::kImpostorTable.literal) {
    if (l.kind == kind && l.reg == reg) return l.bits[comp];
  }
  return 0xFFFFFFFFu;
}
}  // namespace impostor_detail
inline constexpr float kImpostorTwo = 2.0f, kImpostorMinusOne = -1.0f, kImpostorOne = 1.0f;
inline constexpr float kImpostorTenth = std::bit_cast<float>(impostor_detail::LiteralBits(1, 255, 2));
static_assert(impostor_detail::LiteralBits(0, 254, 2) == 0x40000000u && impostor_detail::LiteralBits(0, 255, 2) == 0xBF800000u &&
              impostor_detail::LiteralBits(0, 255, 1) == 0x3F800000u && impostor_detail::LiteralBits(0, 255, 0) == 0u &&
              impostor_detail::LiteralBits(1, 255, 1) == 0x3F800000u && impostor_detail::LiteralBits(1, 255, 2) == 0x3DCCCCCDu,
              "the mirrors' literals are not the table's");

// IMPOSTOR MIRRORS BEGIN (tests/test_impostor_literals.py reads the text between the two markers)
// The position of corner `k` (0 to 3) of the quad whose word is `word` (x, y, z, s): oPos, o0 (the raised place,
// one value a quad) and o1, in single precision and in the reference's order of operations
// (light_formulas.impostor_position, single=True, term for term; the instruction numbers are there).
// The console's product rule (a zero factor gives +0 whatever the other is) is NOT imitated (ledger ruling 27):
// where it matters the reference lands a quad at the view's origin, and here the position is no number: a quad of
// no size (refused before: the record's `quad` rule; guarded again here) and a view whose second row has no part
// of either axis. A corner that is no number in the reference is no number here.
// THAT "NO NUMBER" IS A SIGNAL TO THE CALLER, NEVER A POSITION TO HAND ON (ledger ruling 37, which stands over
// this contract's earlier wording, "which a rasteriser drops"): the vertex program (shaders::kClayImpostorVs)
// tests the quad's size, the two weights' sum and the four components and emits one fixed clipped position
// instead, so nothing that is no number reaches the rasteriser (tests/test_impostor_hlsl.py holds it).
inline void ImpostorPositionMirror(uint32_t k, const float word[4], const ClayImpostorConstants& c, float pos[4], float place[3], float uv[2]) {
#pragma clang fp contract(off)
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float X = word[0], Y = word[1], Z = word[2], S = word[3];
  if (k > 3 || !(S > 0.0f)) {   // (also a size that is no number)
    for (int i = 0; i < 4; ++i) pos[i] = nan;
    for (int i = 0; i < 3; ++i) place[i] = nan;
    uv[0] = uv[1] = nan;
    return;
  }
  const float* a = c.vs[capture::kImpC46];
  const float* r115 = c.vs[capture::kImpC115];
  const float* r116 = c.vs[capture::kImpC116];
  const float* r117 = c.vs[capture::kImpC117];
  const float* c114 = c.vs[capture::kImpC114];
  const float lift = S * c.vs[capture::kImpC8][2];
  // The four points: the lying axis's two ends, the upright axis's two heights. The raise is added before the place's height.
  const float fore[3] = {S * a[0] + X, S * a[1] + Y, (S * a[2] + lift) + Z};
  const float back[3] = {(-S) * a[0] + X, a[1] * (-S) + Y, (a[2] * (-S) + lift) + Z};
  const float top_z = (lift + S) + Z;
  const float bottom_z = (lift + (-S)) + Z;
  place[0] = X;
  place[1] = Y;
  place[2] = Z + lift;
  // The corner: u, "the picture's top edge", v.
  const float u = (k == 2 || k == 3) ? kImpostorOne : 0.0f;
  const bool upper = k == 1 || k == 2;
  const float v = -(upper ? kImpostorOne : 0.0f) + kImpostorOne;
  // The view's rows on the points, sum by sum.
  const float xy115 = (X * r115[0] + Y * r115[1]) + 0.0f;
  const float xy116 = (X * r116[0] + Y * r116[1]) + 0.0f;
  const float xy117 = X * r117[0] + Y * r117[1];
  const float top[3] = {(xy115 + top_z * r115[2]) + r115[3], r116[3] + (top_z * r116[2] + xy116), (top_z * r117[2] + xy117) + r117[3]};
  const float bottom[3] = {(xy115 + bottom_z * r115[2]) + r115[3], r116[3] + (xy116 + bottom_z * r116[2]),
                           r117[3] + (r117[2] * bottom_z + xy117)};
  const float fore_v[3] = {((fore[0] * r115[0] + fore[1] * r115[1]) + fore[2] * r115[2]) + r115[3],
                           (fore[2] * r116[2] + (fore[0] * r116[0] + fore[1] * r116[1])) + r116[3],
                           (fore[2] * r117[2] + (r117[0] * fore[0] + r117[1] * fore[1])) + r117[3]};
  auto dp3 = [&](const float* row) { return ((0.0f + back[2] * row[2]) + back[1] * row[1]) + back[0] * row[0]; };
  const float back_v[3] = {dp3(r115) + r115[3], dp3(r116) + r116[3], dp3(r117) + r117[3]};
  // The two weights: each axis's extent in the view's second row, to the power c114.y (the upright one scaled by c114.x).
  const float upright = std::fabs(top[1] + (-bottom[1]));
  const float lying = std::fabs(fore_v[1] + (-back_v[1]));
  auto power = [&](float extent) {   // 2 ^ (c114.y * log2 extent); an extent of 0 has no weight
    const float log = float(std::log2(double(extent)));
    return float(std::exp2(double(log * c114[1])));
  };
  const float ev = power(std::fabs(c114[0] * upright));
  const float eh = power(lying);
  const float total = ev + eh;
  const float inverse = 1.0f / total;   // (infinite for no extent at all: the weights are then no numbers)
  const float wv = ev * inverse, wh = eh * inverse;
  const bool above = place[2] > c.vs[capture::kImpC47][2];
  const float* end = upper ? top : bottom;
  const float* side = upper ? (above ? back_v : fore_v) : (above ? fore_v : back_v);
  float q[3] = {wv * end[0] + wh * side[0], wh * side[1] + wv * end[1], wh * side[2] + wv * end[2]};
  q[0] = ((u * kImpostorTwo + kImpostorMinusOne) * S) + q[0];
  const float w = std::fabs(q[0]) >= 0.0f ? kImpostorOne : 0.0f;   // 0 where q[0] is no number
  const uint32_t rows[4] = {capture::kImpC72, capture::kImpC73, capture::kImpC74, capture::kImpC75};
  for (int i = 0; i < 4; ++i) {
    const float* row = c.vs[rows[i]];
    pos[i] = (((0.0f + row[2] * q[2]) + row[1] * q[1]) + row[0] * q[0]) + row[3] * w;
  }
  const float* c113 = c.vs[capture::kImpC113];
  uv[0] = u * c113[0] + c113[1];
  uv[1] = v * c113[3] + c113[2];
}

// The fog's three coordinates at a raised place, from the record's own registers: the eye c47.xyz, the axis
// c110.xyz, the range c111 (.x the start, .z the near blend's slope, .w the transmittance table's scale; .y is
// the sky's and is not read). `fogged` false before the start, for a value that is no number, and with the
// frame's fog off in the constants (light_formulas.fog_coords, through FogCoords).
inline FogPoint ImpostorFogCoords(const float place[3], const ClayImpostorConstants& c) {
  if (!(c.fog_on != 0.0f)) return FogPoint{};
  ClayFogConstants f{};
  for (int i = 0; i < 3; ++i) f.axis[i] = c.vs[capture::kImpC110][i];
  f.on = 1;
  f.start = c.vs[capture::kImpC111][0];
  f.slope = c.vs[capture::kImpC111][2];
  f.scale = c.vs[capture::kImpC111][3];
  return FogCoords(place, c.vs[capture::kImpC47], f);
}
// (o2, o3) at the raised place (light_formulas.impostor_fog: the trees' vertex form without a c8.x factor, once a
// tree): o2 the transmittance table at s, o3 the in-scatter table at the angle times (1 - o2), from the frame's two
// tables (which the record's slots 16 and 17 are: the capture's `fog-table` rule). (1, 0) with the frame's fog off
// (in the record's constants or in the frame's), before the start, and for any value that is no number.
inline void ImpostorFogMirror(const float place[3], const ClayImpostorConstants& c, const ClayFrameConstants& frame, float o2[3], float o3[3]) {
  for (int i = 0; i < 3; ++i) {
    o2[i] = 1.0f;
    o3[i] = 0.0f;
  }
  if (!frame.fog.on) return;
  const FogPoint at = ImpostorFogCoords(place, c);
  if (!at.fogged) return;
  double T[3], S2[3], add[3];
  FogTableRead(frame.transmit, kFogTransmitTexels, at.s, T);
  FogTableRead(frame.scatter, kFogScatterTexels, at.a, S2);
  for (int i = 0; i < 3; ++i) {
    add[i] = (1.0 - T[i]) * S2[i];
    if (!std::isfinite(T[i]) || !std::isfinite(S2[i]) || !std::isfinite(add[i])) return;
  }
  for (int i = 0; i < 3; ++i) {
    o2[i] = float(T[i]);
    o3[i] = float(add[i]);
  }
}

// The pixel shader's values before the fog pair (light_formulas.impostor_distance, impostor_pixel): the distance
// d = |c19 - o0|, the colour texel.rgb * c31.x * (1 + sat(0.1 * (d - c12.y)) * (c12.w - 1)) and the alpha
// texel.a * sat((d - c29.x) * c29.y). `texel`: the picture's four values as the fetch gives them to the shader.
struct ImpostorPixelValues {
  float distance = 0.0f, alpha = 0.0f;
  float colour[3] = {};
};
inline ImpostorPixelValues ImpostorPixelCore(const float texel[4], const float place[3], const ClayImpostorConstants& c) {
#pragma clang fp contract(off)
  auto sat = [](float x) { return x != x ? 0.0f : (x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x)); };   // (no number gives 0)
  const float* c12 = c.ps[capture::kImpP12];
  const float* c19 = c.ps[capture::kImpP19];
  const float* c29 = c.ps[capture::kImpP29];
  const float vx = c19[0] - place[0], vy = c19[1] - place[1], vz = c19[2] - place[2];
  ImpostorPixelValues out;
  out.distance = std::sqrt(std::fabs(((0.0f + vz * vz) + vx * vx) + vy * vy));
  const float span = c12[3] + (-kImpostorOne);
  const float fall = sat(kImpostorTenth * (out.distance + (-c12[1])));
  const float dissolve = sat((out.distance - c29[0]) * c29[1]);
  const float light = fall * span + kImpostorOne;
  for (int i = 0; i < 3; ++i) out.colour[i] = (texel[i] * c.ps[capture::kImpP31][0]) * light;
  out.alpha = texel[3] * dissolve;
  return out;
}
// The written colour, ((colour * o2 + o3) * the output scale) as the lit draws write the linear target
// ((v > 0) ? min(v, kPostSceneMax) : 0 a channel; a texel that is no number reads 0), and whether the pixel is kept
// by `c.kept`: the alpha test (alpha > the draw's reference) and the covered samples under the offsets 0xAA
// (ImpostorKept). The scale is the constants' out_scale: the game's c20.w in a post frame (MakeClayImpostorConstants).
inline void ImpostorPixelMirror(const float texel[4], const float place[3], const float o2[3], const float o3[3],
                                const ClayImpostorConstants& c, float out[3], bool* kept) {
#pragma clang fp contract(off)
  const ImpostorPixelValues p = ImpostorPixelCore(texel, place, c);
  for (int i = 0; i < 3; ++i) {
    const float v = (p.colour[i] * o2[i] + o3[i]) * c.out_scale;
    out[i] = v > 0.0f ? (v < kPostSceneMax ? v : kPostSceneMax) : 0.0f;   // (no number: 0)
  }
  if (kept) *kept = ImpostorKept(c.kept, p.alpha > c.alpha_ref, CoveredSamples(kImpostorMaskOffsets, 0, 0, p.alpha));
}
// IMPOSTOR MIRRORS END

// Where the impostor step is drawn in the colour loop: before the prepared record at the returned index (`count`:
// after the last). `seqs`: the prepared records' DrawRecord::seq in the frame's order; `place`: the smallest place
// of the frame's impostor records (the seq their calls were counted under: where the game issues them, before the
// terrain layers). The first record at or beyond the place, moved forward so that the step lies at or before the
// sky step (`sky_at`), the early records' tail (`tail_at`; kNoCoverageTail: none) and the layer's depth
// (`depth_at`): it is opaque and writes depth, so drawn earlier it changes no pixel, and drawn after one of the
// three it would miss the sky's test, a blend or the sun layer's depth.
constexpr size_t PlanImpostorPlace(const uint32_t* seqs, size_t count, uint32_t place, size_t sky_at, size_t tail_at, size_t depth_at) {
  size_t at = count;
  for (size_t i = 0; seqs && i < count; ++i) {
    if (seqs[i] >= place) {
      at = i;
      break;
    }
  }
  if (sky_at < at) at = sky_at;
  if (tail_at < at) at = tail_at;   // (kNoCoverageTail is the largest index: no bound)
  if (depth_at < at) at = depth_at;
  return at;
}

// Whether the frame's impostor records lie in one run among the records the pass draws: no drawn record's seq
// (`seqs`, the prepared records' DrawRecord::seq in the frame's order) lies strictly between the smallest and the
// largest place of the impostor records (`first`, `last`). PlanImpostorPlace gives ONE place, the first record's;
// with a drawn record between two runs the second run would be drawn before a record the game draws it after
// (it matters where that record blends). Such a frame's impostors are not drawn at all ("place"). Every captured
// frame has one run (places 426 to 439 and 498 to 511).
constexpr bool ImpostorPlacesOneRun(const uint32_t* seqs, size_t count, uint32_t first, uint32_t last) {
  if (last < first) return false;
  for (size_t i = 0; seqs && i < count; ++i) {
    if (seqs[i] > first && seqs[i] < last) return false;
  }
  return true;
}

// The impostor step (plan Task 10; clay_pass.cpp, EnsureImpostorObjects and Render; shaders::kClayImpostorVs and
// kClayImpostorPs; frame-map section 19, "The impostor step").
static_assert(size_t(ClayImpostorSkip::kCount) <= std::size(ClayStats{}.impostor_skip));
// The step's small slots in the frame's constant buffer, taken only in a frame that draws the step
// (PlanClayFrameSlots' and ClayFrameSmallSlots' `impostor_slots`): the records' constants (kClayImpostorFrameSlots:
// record i's at impostor0 + kClayImpostorSlots * i), then the frame's quad words, four floats a quad, for the
// capture's bound of quads (ClayImpostorWordsSlot). The words are bound to the vertex program from there as a
// buffer by address: they live and die with the frame's buffer, so no frame in flight reads another frame's.
inline constexpr uint32_t kClayImpostorWordBytes = 16;   // StructuredBuffer<float4>: x, y, z, s
inline constexpr uint32_t kClayImpostorWordSlots =
    (capture::kMaxImpostorQuads * kClayImpostorWordBytes + kClayTerrainSlotBytes - 1) / kClayTerrainSlotBytes;
inline constexpr uint32_t kClayImpostorStepSlots = kClayImpostorFrameSlots + kClayImpostorWordSlots;
static_assert(kClayImpostorWordSlots == 256 && kClayImpostorStepSlots == 320);
constexpr uint32_t ClayImpostorWordsSlot(const ClayFrameSlotPlan& plan) { return plan.impostor0 + kClayImpostorFrameSlots; }
// Whether a record's words lie inside the scene's array of `floats` floats (FrameScene::impostor_words: four a
// quad, the record's from 4 * first_quad) and inside the capture's bound; else the record is not drawn ("buffer").
// Written so that nothing wraps.
constexpr bool ImpostorWordsHeld(const capture::ImpostorRecord& r, size_t floats) {
  if (r.quads == 0 || r.quads > capture::kMaxImpostorQuads || r.first_quad > capture::kMaxImpostorQuads - r.quads) return false;
  return floats / 4 >= size_t(r.first_quad) + size_t(r.quads);
}
// The step's mode in a frame: the switch as the Render reads it now, in a scene that was captured with the
// impostors wanted (FrameScene::impostor_mode; a scene of before the switch came on has no records). Off when
// either is off.
constexpr capture::ImpostorMode ClayImpostorMode(capture::ImpostorMode now, capture::ImpostorMode scene) {
  return now == capture::ImpostorMode::kOff || scene == capture::ImpostorMode::kOff ? capture::ImpostorMode::kOff : now;
}
// The flat picture's one texel (R8G8B8A8_UNORM): kImpostorFlatColour rounded to bytes, alpha 255. The picture
// has no shape in `flat`: its alpha is 1 at every place of the quad (spec Design 11).
constexpr void ImpostorFlatTexel(uint8_t texel[4]) {
  for (int i = 0; i < 3; ++i) texel[i] = uint8_t(kImpostorFlatColour[i] * 255.0f + 0.5f);
  texel[3] = 255;
}
// The log's and F3's Impostors line. "Impostors: off" with the mode off. Else the mode, the records drawn of the
// game's calls seen, their quads, the frame's records the step did not draw by the pass's reason, and the calls
// that gave no record by the capture's.
inline std::string FormatImpostorText(const ClayStats& st) {
  if (st.impostor_mode == capture::ImpostorMode::kOff) return "Impostors: off";
  char buf[160];
  // (a feed run is marked: its pictures are a file's, not the running game's; `on` alone can have a feed)
  const bool feed = st.impostor_mode == capture::ImpostorMode::kOn && st.impostor_feed;
  std::snprintf(buf, sizeof(buf), "Impostors: %s%s, drawn %u of %u (%u quads)", capture::ImpostorModeName(st.impostor_mode), feed ? " (feed)" : "",
                st.impostors_drawn, st.impostors_seen, st.impostor_quads);
  std::string text = buf;
  // (the pictures in `on` alone: `flat` has the line of before)
  if (st.impostor_mode == capture::ImpostorMode::kOn) {
    std::snprintf(buf, sizeof(buf), ", pictures %u (refreshed %u)", st.impostor_pictures, st.impostor_refreshed);
    text += buf;
  }
  text += " | not drawn {";
  bool first = true;
  auto add = [&](const char* name, uint32_t n) {
    if (!n) return;
    std::snprintf(buf, sizeof(buf), "%s%s: %u", first ? "" : ", ", name, n);
    text += buf;
    first = false;
  };
  for (size_t i = size_t(ClayImpostorSkip::kNone) + 1; i < size_t(ClayImpostorSkip::kCount); ++i) {
    add(ClayImpostorSkipName(ClayImpostorSkip(i)), st.impostor_skip[i]);
  }
  text += "} | ";
  text += capture::kImpostorRecordReason;
  text += " {";
  first = true;
  for (size_t i = size_t(capture::ImpostorReason::kNone) + 1; i < size_t(capture::ImpostorReason::kCount); ++i) {
    add(capture::ImpostorReasonName(capture::ImpostorReason(i)), st.impostor_capture[i]);
  }
  return text + "}";
}
// What the pass logs a change of (at most 16 lines a run): the mode, the drawn count, the quads and every
// reason's count, as one number (FNV-1a over the words).
constexpr uint64_t ImpostorStepForm(const ClayStats& st) {
  uint64_t h = 0xCBF29CE484222325ull;
  auto mix = [&](uint32_t v) {
    for (int i = 0; i < 4; ++i) h = (h ^ ((v >> (8 * i)) & 0xFFu)) * 0x100000001B3ull;
  };
  mix(uint32_t(st.impostor_mode));
  mix(st.impostor_mode == capture::ImpostorMode::kOn && st.impostor_feed ? 1u : 0u);
  mix(st.impostors_seen);
  mix(st.impostors_drawn);
  mix(st.impostor_quads);
  mix(st.impostor_pictures);   // (not impostor_refreshed: it may change from frame to frame with nothing else changing)
  for (uint32_t n : st.impostor_skip) mix(n);
  for (uint32_t n : st.impostor_capture) mix(n);
  return h;
}

// --- Native water (frame-map section 20; sub-project 13; plan Task 10 fills this section) ---
// The lake's surface: the game's blended draw BEAD / A17D by its own formulas, late in the frame over the real
// lake bed, with the reflection taken from native 256 x 256 targets; the opaque twin F6D9, which draws nothing in
// the game, left out in a frame whose water is drawn and only there. This section is the draw's rules and mirrors
// (pure: no binding, target or shader text is in it). The formulas are light_formulas.water_vertex, water_alpha,
// water_kept, water_pixel and its parts (docs/native-renderer/lighting-formulas.md, "Water"); the mirrors below
// are pinned to their single-precision form by tests/native/water_pins.inc (tools/xdk_sigmatch/water_pins.py).
// ClayWater, the frame's verdict, is declared before ClayStats (which carries it).

// The dead draw (spec ruling 12): F6D9 kills every pixel, whatever the mask holds, when the saturate's argument is
// positive at both ends of the mask: min(c141.x, c141.x - c141.y) >= 1e-6 with |c141.x|, |c141.y| <= 4
// (light_formulas.water_opaque_dead). The difference and the comparisons are made in DOUBLE on the single inputs
// (the difference of two singles of that size is exact in double), against the double 1e-6: the single nearest
// 1e-6 is BELOW it (0x358637BD), so a single-precision margin would pass a c141 the reference refuses. False for a
// value that is no number or infinite. c141.z and c141.w are not part of it. It holds for every mask in [0, 1],
// which the record's fetch rule guarantees (render::WaterTextureFetch, ledger ruling 33).
inline constexpr double kWaterDeadMargin = 1e-6;
inline constexpr double kWaterDeadReach = 4.0;
constexpr bool WaterOpaqueDead(const float c141[4]) {
  if (!c141) return false;
  const double x = double(c141[0]), y = double(c141[1]);
  if (!(x >= -kWaterDeadReach && x <= kWaterDeadReach && y >= -kWaterDeadReach && y <= kWaterDeadReach)) return false;   // (false for no number)
  const double end = x - y;
  return (x < end ? x : end) >= kWaterDeadMargin;
}

// The mirrored camera (spec ruling 15): diag(c31x, c31y, 1, 1) * M * Mirror(h), with M the frame's view-projection
// (rows . (p, 1)) and Mirror(h) the reflection in the plane z = h: a row (a, b, c, d) becomes
// f * (a, b, -c, d + 2 h c), f the row's factor (c31x, c31y, 1, 1). Computed in double from the singles, each entry
// rounded once (water_check.water_view_rows; the game's own view rows equal it within 1.4e-6 in 32 of 32 views).
// False, `out` untouched, for a value that is no number, a factor outside (0, 4], a height beyond 1e6 or a result
// that does not fit.
inline bool WaterViewRows(const float view_proj[4][4], float c31x, float c31y, float h, float out[4][4]) {
#pragma clang fp contract(off)
  if (!view_proj || !out) return false;
  if (!(c31x > 0.0f && c31x <= capture::kWaterViewFactorMax && c31y > 0.0f && c31y <= capture::kWaterViewFactorMax)) return false;
  if (!(std::fabs(h) <= capture::kWaterPlaceMax)) return false;
  float rows[4][4];
  for (int i = 0; i < 4; ++i) {
    const double f = i == 0 ? double(c31x) : i == 1 ? double(c31y) : 1.0;
    const double a = double(view_proj[i][0]), b = double(view_proj[i][1]), c = double(view_proj[i][2]), d = double(view_proj[i][3]);
    const double twice = 2.0 * double(h);
    const double lifted = twice * c;
    const double moved = d + lifted;
    rows[i][0] = float(f * a);
    rows[i][1] = float(f * b);
    rows[i][2] = float(f * -c);
    rows[i][3] = float(f * moved);
    for (int j = 0; j < 4; ++j) {
      if (!std::isfinite(rows[i][j])) return false;   // (also an entry that is no number)
    }
  }
  std::memcpy(out, rows, sizeof(rows));
  return true;
}

// A mesh's height from the box of its positions (GeometryCache: computed once a buffer): valid iff the box is
// valid and hi[2] - lo[2] <= 1e-3 (which implies the spec's "every vertex within 1e-3 of the first"); h is lo[2].
inline constexpr float kWaterFlatness = 1e-3f;
struct WaterHeight {
  bool valid = false;
  float h = 0.0f;
};
constexpr WaterHeight WaterMeshHeight(const PositionBounds& b) {
  if (!b.valid || !(b.hi[2] - b.lo[2] <= kWaterFlatness) || !(b.hi[2] >= b.lo[2])) return WaterHeight{};
  return WaterHeight{true, b.lo[2]};
}

// The targets (spec ruling 16): one for each distinct slot-13 base of the frame's surface records, ascending, at
// most kMaxWaterTargets. target_of[i]: record i's target. ok false with `why`: kPlane (a mesh that is not flat, or
// two heights on one base more than kWaterFlatness apart), else kReflection (a fifth base; no records' array).
// THE ORDER IS THE RULE OF PHASE 13C TOO: the game renders its k-th reflection view of a frame into the picture at
// the k-th smallest slot-13 base (10 of 10 frames by the resolves' destinations, 32 of 32 views by the heights:
// plan Task 15), so target k is the k-th view's (PlanWaterViews).
inline constexpr uint32_t kMaxWaterTargets = 4, kWaterTargetSize = 256;
struct WaterTargetPlan {
  bool ok = false;
  ClayWater why = ClayWater::kDrawn;
  uint32_t count = 0, base[kMaxWaterTargets] = {};
  float height[kMaxWaterTargets] = {};
};
constexpr uint32_t WaterPictureBase(const capture::WaterRecord& r) { return r.fetch[capture::kWatPicture][1] & 0xFFFFF000u; }
inline WaterTargetPlan PlanWaterTargets(const capture::WaterRecord* const* surface, const WaterHeight* heights, size_t count, uint8_t* target_of) {
  WaterTargetPlan plan;
  plan.why = ClayWater::kReflection;
  if (count == 0) {
    plan.ok = true;
    plan.why = ClayWater::kDrawn;
    return plan;
  }
  if (!surface || !heights || !target_of || count > capture::kMaxWaterRecords) return plan;
  bool flat = true, fits = true;
  uint32_t n = 0, base[kMaxWaterTargets] = {};
  float height[kMaxWaterTargets] = {};
  for (size_t i = 0; i < count; ++i) {
    if (!surface[i]) return plan;
    if (!heights[i].valid) {
      flat = false;
      continue;
    }
    const uint32_t b = WaterPictureBase(*surface[i]);
    uint32_t k = 0;
    while (k < n && base[k] != b) ++k;
    if (k == n) {
      if (n == kMaxWaterTargets) {
        fits = false;
        continue;
      }
      base[n] = b;
      height[n] = heights[i].h;
      ++n;
    } else if (!(std::fabs(heights[i].h - height[k]) <= kWaterFlatness)) {
      flat = false;
    }
  }
  if (!flat) {
    plan.why = ClayWater::kPlane;
    return plan;
  }
  if (!fits) return plan;
  // ascending by base (at most four: an insertion sort)
  for (uint32_t i = 1; i < n; ++i) {
    for (uint32_t j = i; j > 0 && base[j] < base[j - 1]; --j) {
      std::swap(base[j], base[j - 1]);
      std::swap(height[j], height[j - 1]);
    }
  }
  for (size_t i = 0; i < count; ++i) {
    const uint32_t b = WaterPictureBase(*surface[i]);
    uint32_t k = 0;
    while (k < n && base[k] != b) ++k;
    target_of[i] = uint8_t(k);
  }
  plan.ok = true;
  plan.why = ClayWater::kDrawn;
  plan.count = n;
  for (uint32_t k = 0; k < n; ++k) {
    plan.base[k] = base[k];
    plan.height[k] = height[k];
  }
  return plan;
}

// The frame's verdict (spec Design 5): kDrawn, or the first reason in the decision's order (kWaterDecisionOrder,
// below: the enum's, but `view` before `plane`).
struct ClayWaterInputs {
  bool on = false;                     // the switch now, in a scene captured with it
  uint32_t surface = 0, opaque = 0;    // the scene's records by kind (0 and 0: the scene's block is empty)
  bool light = false;                  // the frame is lit: the linear target's meaning
  bool opaque_dead = true;             // WaterOpaqueDead of every opaque record's c141
  bool planes = true;                  // PlanWaterTargets did not say kPlane (judged after `view`: a record that fails that has no height)
  bool view = true;                    // every record's draw has the frame's one view-projection, row layout kDot
  bool targets = true;                 // PlanWaterTargets did not say kReflection, and the frame's sky is drawn with its constants
  bool objects_targets = true;         // the targets and the mirrored sky's objects exist
  bool textures = true;                // every mask and the normal map resolved, every pair buffer made
  bool order = true;                   // PlanWaterPlace: usable, and the surface records' places one run (WaterPlacesOneRun)
  bool objects = true;                 // the surface step's layout, programs and pipelines exist
  bool slots = true;                   // the frame's buffer exists and holds the step's slots
  bool fits = true;                    // WaterRecordsFit(surface): the step holds every surface record of the scene
  bool scale = true;                   // every record's c20.w is the frame's output scale within a thousandth
};
// Whether the step holds a scene's surface records (one prepared surface, two slots, a record): at most
// capture::kMaxWaterRecords. The capture takes no more (its frame is whole or empty), so a scene beyond it is not
// reachable; the verdict of one is `slot` all the same, never `drawn` with nothing prepared (the review of Tasks 11
// and 12, Minor 2).
constexpr bool WaterRecordsFit(uint32_t surface) { return surface <= capture::kMaxWaterRecords; }
// THE DECISION'S ORDER (DecideClayWater: the first of these whose input is missing is the frame's reason). The
// enum's order but for one pair: `view` is judged BEFORE `plane` (ledger ruling 46; the review of Tasks 11 and 12,
// I1). A surface record whose draw record is not found, whose layout is not kDot or whose rows are not the frame's
// view-projection is never asked for its positions, so it has no height and PlanWaterTargets answers kPlane for
// it: with `plane` first the log named the mesh's flatness where the cause was the record lookup or the matrix.
// A frame reads `plane` only when every record passed the view's three checks.
inline constexpr ClayWater kWaterDecisionOrder[] = {ClayWater::kOff,        ClayWater::kNoRecords, ClayWater::kLight,   ClayWater::kOpaqueLive,
                                                    ClayWater::kPair,       ClayWater::kView,      ClayWater::kPlane,   ClayWater::kReflection,
                                                    ClayWater::kTexture,    ClayWater::kOrder,     ClayWater::kPipeline, ClayWater::kSlot,
                                                    ClayWater::kScale};
static_assert(sizeof(kWaterDecisionOrder) / sizeof(kWaterDecisionOrder[0]) == size_t(ClayWater::kCount) - 1);
constexpr ClayWater DecideClayWater(const ClayWaterInputs& in) {
  if (!in.on) return ClayWater::kOff;
  if (in.surface == 0) return ClayWater::kNoRecords;
  if (!in.light) return ClayWater::kLight;
  if (!in.opaque_dead) return ClayWater::kOpaqueLive;
  if (in.surface != in.opaque) return ClayWater::kPair;
  if (!in.view) return ClayWater::kView;      // (before `plane`: kWaterDecisionOrder)
  if (!in.planes) return ClayWater::kPlane;
  if (!in.targets || !in.objects_targets) return ClayWater::kReflection;
  if (!in.textures) return ClayWater::kTexture;
  if (!in.order) return ClayWater::kOrder;
  if (!in.objects) return ClayWater::kPipeline;
  if (!in.slots || !in.fits) return ClayWater::kSlot;
  if (!in.scale) return ClayWater::kScale;
  return ClayWater::kDrawn;
}
// Whether a record of `kind` is left out of the pre-pass and the colour pass (drawn by the water step, or not at
// all): an opaque record ONLY with the verdict kDrawn (the dead draw is removed only together with the surface
// that replaces it: spec ruling 11); a surface record always (today it is blend-unknown and not drawn).
constexpr bool WaterLeavesOut(ClayWater verdict, capture::WaterKind kind) {
  if (kind == capture::WaterKind::kSurface) return true;
  return kind == capture::WaterKind::kOpaque && verdict == ClayWater::kDrawn;
}
// What the head of Render can say before the frame's buffer is sized (the scene's own rules: the switch, the
// records, the light, the dead draw, the pairs, their number, the scale): the verdict with every later input true. The step's
// slots are reserved iff this is kDrawn; every other frame's slot plan is the one of before, slot for slot.
constexpr ClayWater WaterSceneVerdict(const ClayWaterInputs& in) {
  ClayWaterInputs scene;
  scene.on = in.on;
  scene.surface = in.surface;
  scene.opaque = in.opaque;
  scene.light = in.light;
  scene.opaque_dead = in.opaque_dead;
  scene.fits = in.fits;
  scene.scale = in.scale;
  return DecideClayWater(scene);
}
// The scale rule's tolerance (as the impostors' has it): a record's c20.w against the frame's output scale.
inline constexpr float kWaterScaleTolerance = 1e-3f;
constexpr bool WaterScaleMatches(float c20w, float out_scale) {
  const float d = c20w - out_scale;
  return capture::WaterScaleBounded(c20w) && capture::WaterScaleBounded(out_scale) &&
         (d < 0.0f ? -d : d) <= kWaterScaleTolerance * (out_scale > 1.0f ? out_scale : 1.0f);
}

// The draw record a water record names: the index in `draws` (FrameScene::draws, which holds the frame's DRAWABLE
// records in ascending DrawRecord::seq: a skipped draw has a number and no record) of the record whose seq is
// `place`; ~size_t(0) when none has it. A binary search.
constexpr size_t WaterDrawIndex(const capture::DrawRecord* draws, size_t count, uint32_t place) {
  size_t lo = 0, hi = draws ? count : 0;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (draws[mid].seq < place) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return draws && lo < count && draws[lo].seq == place ? lo : ~size_t(0);
}

// Where the step is drawn: before the prepared record at `at` (count: after the last), after the steps already due
// at that index. `seqs`: the prepared records' DrawRecord::seq in the frame's order, the dead draws left out;
// `place`: the smallest place of the frame's surface records. usable iff
//   - the sky step (`sky_at`, with `sky_drawn`) lies at or before `at`: the game draws the sky, the moon and the
//     clouds before the water, and a sky drawn after it would pass its depth test over the surface;
//   - the early records' tail (`tail_at`; kNoCoverageTail: none) lies at or before `at`;
//   - the sun layer's depth (`depth_at`: the index among the prepared records before which it is taken,
//     CoverageDepthPlace; at or beyond `count`, or the default: not among them, or after the last) is NOT taken
//     before `at`. With the dead draw left out, a layer's depth taken before the water step holds the lake's bed
//     where the game's holds the surface's cube, and the beams would pass through the lake; in the captures the
//     place stands after the water's rows (spec Design 8). At the same index the loop draws the water first
//     (ColourDue::depth_after_water).
// A frame in which any of the three fails is `order`, and today's.
struct WaterPlace {
  bool usable = false;
  size_t at = 0;
};
constexpr WaterPlace PlanWaterPlace(const uint32_t* seqs, size_t count, uint32_t place, bool sky_drawn, size_t sky_at, size_t tail_at,
                                    size_t depth_at = ~size_t(0)) {
  WaterPlace w;
  w.at = count;
  for (size_t i = 0; seqs && i < count; ++i) {
    if (seqs[i] >= place) {
      w.at = i;
      break;
    }
  }
  w.usable = (!sky_drawn || sky_at <= w.at) && (tail_at == kNoCoverageTail || tail_at <= w.at) && depth_at >= w.at;
  return w;
}
// Whether the frame's surface records lie in one run among the records the pass draws: no drawn record's seq lies
// strictly between the smallest and the largest place (`first`, `last`). PlanWaterPlace gives ONE place; with a
// drawn record between two runs the second run would be drawn before a record the game draws it after, and the
// water blends and writes depth. Such a frame is `order`. (Twelve consecutive places in every captured frame.)
constexpr bool WaterPlacesOneRun(const uint32_t* seqs, size_t count, uint32_t first, uint32_t last) {
  return ImpostorPlacesOneRun(seqs, count, first, last);
}

// The record's constants (cbuffer Water of shaders::kClayWaterVs and kClayWaterPs, register b0 of the step's
// layout; shaders::kClayWaterFields is its layout: plan Task 12). What the pixel program reads beyond them is the
// frame's block at b2: the fog's tables and registers (c28, c66, c67, c68, held to the record's by the capture's
// `fog` rule) and nothing else of the record's 24 registers: c20.w is `out_scale`, and c139 is read by no record.
// Eighteen of the record's registers (the plan counted seventeen; its own list names these eighteen).
inline constexpr uint32_t kClayWaterPsRegisters = 18;
enum : uint32_t { kCwP0, kCwP19, kCwP31, kCwP116, kCwP117, kCwP126, kCwP127, kCwP128, kCwP129, kCwP130, kCwP131, kCwP136, kCwP137,
                  kCwP138, kCwP140, kCwP141, kCwP142, kCwP143 };
// Which of the record's registers each is (capture::kWatP...).
inline constexpr uint8_t kClayWaterPsFrom[kClayWaterPsRegisters] = {
    capture::kWatP0,   capture::kWatP19,  capture::kWatP31,  capture::kWatP116, capture::kWatP117, capture::kWatP126,
    capture::kWatP127, capture::kWatP128, capture::kWatP129, capture::kWatP130, capture::kWatP131, capture::kWatP136,
    capture::kWatP137, capture::kWatP138, capture::kWatP140, capture::kWatP141, capture::kWatP142, capture::kWatP143};
struct ClayWaterConstants {
  float rows[4][4];                     // the draw record's rows (layout kDot)
  float vs[4][4];                       // c115..c118
  float ps[kClayWaterPsRegisters][4];   // unread components as the game left them: the program names every register by component
  float alpha_ref, out_scale;           // the draw's alpha reference (the state's, not c141.w); the frame's output scale
  uint32_t sun_on, fog_on;              // bool b136; the frame's fog
  uint32_t sampler_mask, sampler_normal, sampler_picture;   // static samplers (SamplerIndex of each fetch)
  uint32_t swizzle;                     // the mask's channel in bits 0 to 2, the normal's x in 3 to 5, its y in 6 to 8 (0 to 3 a channel, 4: 0, 5: 1)
  // The draw's own base vertex and the vertices its position buffer holds (plan Task 12: the vertex program pulls
  // a vertex by SV_VertexID through the draw's index buffer, as the clay vertex program does, and needs both;
  // plan Task 10's block had left them out). Its index range was held to them on the CPU (GeometryCache::Indices).
  int32_t base_vertex;
  uint32_t vertex_count;
  uint32_t pad[2];
};
// 64 + 64 + 288 + 16 + 16 (recomputed by plan Task 10 from what is carried: the plan's 432 had 17 registers), and
// 16 for the draw's base vertex and vertex count (plan Task 12): 464 bytes, two small slots as before.
static_assert(sizeof(ClayWaterConstants) == 464 && offsetof(ClayWaterConstants, vs) == 64 && offsetof(ClayWaterConstants, ps) == 128 &&
              offsetof(ClayWaterConstants, alpha_ref) == 416 && offsetof(ClayWaterConstants, sun_on) == 424 &&
              offsetof(ClayWaterConstants, sampler_mask) == 432 && offsetof(ClayWaterConstants, swizzle) == 444 &&
              offsetof(ClayWaterConstants, base_vertex) == 448 && offsetof(ClayWaterConstants, vertex_count) == 452);
inline constexpr uint32_t kClayWaterSlots = (sizeof(ClayWaterConstants) + kClayTerrainSlotBytes - 1) / kClayTerrainSlotBytes;   // 2
// The step's small slots in the frame's constant buffer, taken only in a frame whose scene verdict is kDrawn
// (PlanClayFrameSlots' and ClayFrameSmallSlots' `water_slots`): record i's constants at water0 + kClayWaterSlots *
// i; target k's sky constants at water0 + kClayWaterSlots * kMaxWaterRecords + k; the second frame block
// (WaterFrameBlock) at the next kClayFrameSlots slots.
inline constexpr uint32_t kClayWaterStepSlots = kClayWaterSlots * capture::kMaxWaterRecords + kMaxWaterTargets + kClayFrameSlots;
static_assert(kClayWaterSlots == 2 && kClayWaterStepSlots == 153);
constexpr uint32_t ClayWaterRecordSlot(const ClayFrameSlotPlan& plan, uint32_t i) { return plan.water0 + kClayWaterSlots * i; }
constexpr uint32_t ClayWaterTargetSlot(const ClayFrameSlotPlan& plan, uint32_t k) {
  return plan.water0 + kClayWaterSlots * capture::kMaxWaterRecords + k;
}
constexpr uint32_t ClayWaterFrameSlot(const ClayFrameSlotPlan& plan) {
  return plan.water0 + kClayWaterSlots * capture::kMaxWaterRecords + kMaxWaterTargets;
}
// A field of the swizzle word: 0 to 3 a channel of the texel, 4 the constant 0, 5 the constant 1.
constexpr bool WaterSwizzleUsable(uint32_t swizzle) {
  return swizzle < (1u << 9) && (swizzle & 7u) <= 5u && ((swizzle >> 3) & 7u) <= 5u && ((swizzle >> 6) & 7u) <= 5u;
}
// The swizzle word of a record's two textures as the pass uploads them (TextureCache::ResolveWater: the mask as
// R8, the normal map as BC5, views without a swizzle): the HOST channel of the first component the mask's fetch
// delivers and of the first two the normal map's does (the components the microcode reads: light_formulas.
// water_alpha, water_normal), by WaterHostChannel. With the captured constants ((0, 0, 0, 5) and (0, 1, 2, 3)):
// red; red and green.
constexpr uint32_t WaterSwizzleWord(const TextureFetch& mask, const TextureFetch& normal) {
  return WaterHostChannel(WaterDecode::kK8, mask.swizzle[0]) | (WaterHostChannel(WaterDecode::kDxn, normal.swizzle[0]) << 3) |
         (WaterHostChannel(WaterDecode::kDxn, normal.swizzle[1]) << 6);
}
// The state a surface record is drawn with, from its draw's own state words: the capture's `state` rule asked
// again (nothing reaches a pipeline on the strength of the capture's rule alone): valid, blended source alpha over
// inverse source alpha by addition, the depth tested "greater" and written under the reversed range (the RHI's
// "less" in the pass's convention: ledger ruling 27), four colour channels, the alpha test "greater" with a
// reference in [0, 1], no alpha-to-mask. `cull`: the record's own, in the pass's window space (ClayCull), which
// picks the step's pipeline; `alpha_ref`: the state's reference (not c141.w). usable false: the frame's water is
// not drawn (`pipeline`: the step has no pipeline of another state).
struct WaterSurfaceState {
  bool usable = false;
  capture::Cull cull = capture::Cull::kNone;
  float alpha_ref = 0.0f;
};
inline WaterSurfaceState WaterSurfaceStateOf(const capture::StateWords& words) {
  const capture::RenderState s = capture::DecodeRenderState(words);
  WaterSurfaceState out;
  if (!words.read || !s.valid || !s.depth_test || !s.depth_write || !s.depth_reversed || s.depth_func != capture::Compare::kGreater ||
      s.color_mask != 0xF || !s.alpha_test || s.alpha_func != capture::Compare::kGreater || !(s.alpha_ref >= 0.0f && s.alpha_ref <= 1.0f) ||
      s.alpha_to_mask || !s.blend || s.src != capture::Factor::kSrcAlpha || s.dst != capture::Factor::kInvSrcAlpha ||
      s.op != capture::BlendOp::kAdd) {
    return out;
  }
  out.usable = true;
  out.cull = ClayCull(s.cull, s.front_cw);
  out.alpha_ref = s.alpha_ref;
  return out;
}
// The constants of a surface record. False, `out` untouched, unless: the record is a valid surface record and
// `draw` is the draw record it names (its seq the record's place) with the row layout kDot and rows that are
// numbers within a matrix term's bound; the registers pass the capture's own rule again (capture::
// WaterRegistersBounded: nothing reaches a shader on the strength of the capture's rule alone; so c128.x, c136.x
// and c138.x are above 0 here, which the program's power relies on); the scale is in (0, 64] and the alpha
// reference a number in [0, 1]; the samplers are static samplers (0 to 3) and the swizzle's fields 0 to 5; and
// `vertex_count`, the vertices the draw's position buffer holds, is above 0 and at most capture::kMaxDrawCount.
// Components no formula reads are carried bit for bit. `alpha_ref`: the draw's own (DrawRecord::state_words).
inline bool MakeClayWaterConstants(const capture::WaterRecord& r, const capture::DrawRecord& draw, float alpha_ref, float out_scale,
                                   bool fog_on, const uint32_t samplers[3], uint32_t swizzle, uint32_t vertex_count, ClayWaterConstants* out) {
  if (!out || !samplers || !capture::WaterRecordValid(r) || r.kind != uint32_t(capture::WaterKind::kSurface)) return false;
  if (draw.seq != r.place || draw.layout != capture::TransformLayout::kDot) return false;
  if (vertex_count == 0 || vertex_count > capture::kMaxDrawCount) return false;
  for (float v : draw.rows) {
    if (!(std::fabs(v) <= capture::kWaterMatrixMax)) return false;   // (false for no number)
  }
  if (!capture::WaterRegistersBounded(r.vs, r.ps, r.pbool)) return false;
  if (!capture::WaterScaleBounded(out_scale) || !(alpha_ref >= 0.0f && alpha_ref <= 1.0f)) return false;
  if (samplers[0] > 3 || samplers[1] > 3 || samplers[2] > 3 || !WaterSwizzleUsable(swizzle)) return false;
  ClayWaterConstants c{};
  std::memcpy(c.rows, draw.rows, sizeof(c.rows));
  std::memcpy(c.vs, r.vs, sizeof(c.vs));
  for (uint32_t i = 0; i < kClayWaterPsRegisters; ++i) std::memcpy(c.ps[i], r.ps[kClayWaterPsFrom[i]], sizeof(c.ps[i]));
  c.alpha_ref = alpha_ref;
  c.out_scale = out_scale;
  c.sun_on = (r.pbool & capture::kWaterB136) != 0 ? 1u : 0u;
  c.fog_on = fog_on ? 1u : 0u;
  c.sampler_mask = samplers[0];
  c.sampler_normal = samplers[1];
  c.sampler_picture = samplers[2];
  c.swizzle = swizzle;
  c.base_vertex = draw.base_vertex;
  c.vertex_count = vertex_count;
  *out = c;
  return true;
}
// The second frame block (spec Design 7): the frame's with the exposure 1 and 1 / the target's size, under which
// the sky's own pixel program draws a target (the game's views are drawn at c20.w = 1). Every other field is the
// frame's bit for bit.
inline ClayFrameConstants WaterFrameBlock(const ClayFrameConstants& frame) {
  ClayFrameConstants block = frame;
  block.exposure = 1.0f;
  block.mask_du = block.mask_dv = 1.0f / float(kWaterTargetSize);
  return block;
}

// The mirrors' literals: the pixel shaders' own c254 and c255 (water_table.inc).
namespace water_detail {
constexpr uint32_t LiteralBits(uint32_t reg, uint32_t comp) {
  for (const capture::WaterTable::Literal& l : capture::kWaterTable.literal) {
    if (l.reg == reg) return l.bits[comp];
  }
  return 0xFFFFFFFFu;
}
}  // namespace water_detail
inline constexpr float kWaterTwo = 2.0f, kWaterHalf = 0.5f, kWaterOne = 1.0f, kWaterMinusOne = -1.0f, kWaterMinusTwo = -2.0f;
static_assert(water_detail::LiteralBits(254, 0) == 0u && water_detail::LiteralBits(254, 1) == 0x40000000u &&
              water_detail::LiteralBits(255, 0) == 0x3F000000u && water_detail::LiteralBits(255, 1) == 0xBF800000u &&
              water_detail::LiteralBits(255, 2) == 0x3F800000u && water_detail::LiteralBits(255, 3) == 0xC0000000u,
              "the mirrors' literals are not the table's");

// What becomes of a pixel of the surface (light_formulas.water_kept, kind "surface").
enum class WaterFate : uint8_t { kKilled, kSkipped, kShaded };
// Everything the pixel formula makes on its way (the pins hold each): what a shaded pixel has; zeros otherwise.
struct WaterPixelValues {
  WaterFate fate = WaterFate::kKilled;
  float alpha = 0.0f;
  float colour[3] = {};       // after the fog and the output scale, before the write's clamp
  float lookup[2] = {};       // slot 13's coordinates
  float lookup_near[2] = {};  // slot 14's
  float normal[3] = {};
  float fresnel = 0.0f, distance = 0.0f;
  float sun[3] = {};
  bool fogged = false;
  float fog_a = 0.0f, fog_s = 0.0f;   // the in-scatter table's and the transmittance table's coordinates
};
// What the surface's pixel is given: q the pixel's integral position (the floor of SV_Position.xy), place and the
// two scrolling coordinates from the vertex, a and b the normal map's two samples (x, y each), m the mask's
// sample, picture the body's target at the lookup, slot14 that slot's texel (ZERO in the native draw: spec ruling 21),
// scatter and transmit the frame's two fog tables at the pixel's coordinates.
struct WaterPixelIn {
  float q[2], place[3], a[2], b[2], m, picture[3];
  float scatter[3], transmit[3];
  float slot14[3] = {0.0f, 0.0f, 0.0f};
};

// WATER MIRRORS BEGIN (tests/test_water_literals.py reads the text between the two markers)
// Single precision, in the reference's order of operations (light_formulas.water_*, single=True, term for term;
// the HLSL's stand-in, tests/test_water_hlsl.py FIXTURE, is the same text). Contraction off: a fused multiply-add
// is not the console's rounded product and rounded sum.
// THE CONSOLE'S ZERO PRODUCT (a zero factor gives +0 whatever the other is) decides four points, and IEEE
// arithmetic has no such rule (ledger ruling 34). It is WRITTEN OUT here as in the HLSL:
//   - at the three reciprocal square roots (the normal, the view vector, the sun's second normal): a sum of squares
//     of exactly 0 gives the factor 0, so the vector is 0 as the console's 0 * infinity is, not "no number";
//   - at the power: a cosine of 0 gives 0, whatever the power. That is the console's value for every power above
//     0; at a power of exactly 0 the console gives 1 and this text 0, and no record has a power of 0
//     (capture::WaterRegistersBounded keeps c128.x, c136.x and c138.x above 0: MakeClayWaterConstants).
// A register is read by component, and only the components a formula reads: an unread one may be no number.
namespace water_detail {
inline float Sat(float x) { return x != x ? 0.0f : (x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x)); }   // (no number gives 0)
inline float Rsq(float x) { return x == 0.0f ? 0.0f : float(1.0 / std::sqrt(double(x))); }          // the guard: 0 of 0
inline float Sqrt(float x) { return float(std::sqrt(double(x))); }
}  // namespace water_detail

// oPos = (c0 . (p, 1), ..., c3 . (p, 1)), a row's four products summed in the microcode's order (z, x, y, w):
// light_formulas.water_vertex's "pos".
inline void WaterPositionMirror(const float p[3], const ClayWaterConstants& c, float pos[4]) {
#pragma clang fp contract(off)
  for (int i = 0; i < 4; ++i) {
    const float* row = c.rows[i];
    pos[i] = (((0.0f + row[2] * p[2]) + row[0] * p[0]) + row[1] * p[1]) + row[3] * kWaterOne;
  }
}
// o1: the two scrolling coordinates of the normal map (water_vertex's "coord").
inline void WaterCoordsMirror(const float p[3], const ClayWaterConstants& c, float coord[4]) {
#pragma clang fp contract(off)
  const float* c115 = c.vs[capture::kWatV115];
  const float* c116 = c.vs[capture::kWatV116];
  const float* c117 = c.vs[capture::kWatV117];
  const float* c118 = c.vs[capture::kWatV118];
  coord[0] = p[0] * c117[0] + c115[0];
  coord[1] = p[1] * c117[1] + c115[1];
  coord[2] = p[0] * c118[0] + c116[0];
  coord[3] = p[1] * c118[1] + c116[1];
}
// alpha = 1 - sat(c141.x - (1 - m) * c141.y) (water_alpha).
inline float WaterAlphaMirror(float m, const ClayWaterConstants& c) {
#pragma clang fp contract(off)
  const float* c141 = c.ps[kCwP141];
  const float inverse = kWaterOne + (-m);
  const float ramp = water_detail::Sat((-inverse) * c141[1] + c141[0]);
  return kWaterOne + (-ramp);
}
// The surface's fate: killed at an alpha of exactly 1; else skipped (colour 0 with its alpha) unless the alpha is
// above c141.w (water_kept). c141.w is the SHADER's threshold; whether the pixel is drawn at all is the state's
// alpha test against the draw's reference (WaterDrawn): two numbers, one unit in the last place apart in every
// captured row.
inline WaterFate WaterFateMirror(float alpha, const ClayWaterConstants& c) {
  if (alpha == kWaterOne) return WaterFate::kKilled;
  return alpha > c.ps[kCwP141][3] ? WaterFate::kShaded : WaterFate::kSkipped;
}
// Whether the state's alpha test ("greater" than the draw's reference) draws a pixel that was not killed.
inline bool WaterDrawn(WaterFate fate, float alpha, const ClayWaterConstants& c) { return fate != WaterFate::kKilled && alpha > c.alpha_ref; }

inline WaterPixelValues WaterPixelCore(const WaterPixelIn& in, const ClayWaterConstants& c, const ClayFogConstants& fog) {
#pragma clang fp contract(off)
  using water_detail::Rsq;
  using water_detail::Sat;
  using water_detail::Sqrt;
  WaterPixelValues out;
  out.alpha = WaterAlphaMirror(in.m, c);
  out.fate = WaterFateMirror(out.alpha, c);
  if (out.fate != WaterFate::kShaded) return out;   // (a skipped pixel's colour is 0 times the scale)
  const float* c0 = c.ps[kCwP0];
  const float* eye = c.ps[kCwP19];
  const float* c31 = c.ps[kCwP31];
  const float* c126 = c.ps[kCwP126];
  const float* c127 = c.ps[kCwP127];
  const float* c129 = c.ps[kCwP129];
  const float* c130 = c.ps[kCwP130];
  // The normal: s = (2a - 1) + (2b - 1); N = normalize(s / 2, (c128.x / 2) * (the two samples' own third components)).
  const float sx = in.a[0] * kWaterTwo + (in.b[0] * kWaterTwo + kWaterMinusTwo);
  const float sy = in.a[1] * kWaterTwo + (in.b[1] * kWaterTwo + kWaterMinusTwo);
  const float dax = in.a[0] * kWaterTwo + kWaterMinusOne, day = in.a[1] * kWaterTwo + kWaterMinusOne;
  const float dbx = in.b[0] * kWaterTwo + kWaterMinusOne, dby = in.b[1] * kWaterTwo + kWaterMinusOne;
  const float za = Sqrt(std::fabs(-(dax * dax + day * day) + kWaterOne));
  const float zb = Sqrt(std::fabs(-(dbx * dbx + dby * dby) + kWaterOne));
  const float nx = sx * kWaterHalf, ny = sy * kWaterHalf;
  const float nz = (kWaterHalf * c.ps[kCwP128][0]) * (za + zb);
  const float n_inv = Rsq(std::fabs(((0.0f + nz * nz) + nx * nx) + ny * ny));
  const float N[3] = {nx * n_inv, ny * n_inv, nz * n_inv};
  // The lookups: the pixel's screen place scaled about the centre by c31.xy, moved by the normal.
  const float px = std::fabs(in.q[0]) * c0[2], py = std::fabs(in.q[1]) * c0[3];
  const float u = ((-kWaterHalf + c0[0]) + px) * c31[0] + kWaterHalf;
  const float v = (c0[1] + (-kWaterHalf + py)) * c31[1] + kWaterHalf;
  out.lookup[0] = u + (c129[0] * kWaterHalf) * sx;
  out.lookup[1] = v + (c129[1] * kWaterHalf) * sy;
  out.lookup_near[0] = u + (sx * kWaterHalf) * c130[0];
  out.lookup_near[1] = v + (sy * kWaterHalf) * c130[1];
  // The view vector, the distance, the fresnel factor and the base colour.
  const float ex = -in.place[0] + eye[0], ey = -in.place[1] + eye[1], ez = -in.place[2] + eye[2];
  const float dd = std::fabs(((0.0f + ez * ez) + ex * ex) + ey * ey);
  const float e_inv = Rsq(dd);
  const float V[3] = {ex * e_inv, ey * e_inv, ez * e_inv};
  const float d = Sqrt(dd);
  const float cosine = ((0.0f + V[2] * N[2]) + V[0] * N[0]) + V[1] * N[1];
  const float F = Sat((-cosine + kWaterOne) + c.ps[kCwP116][0]);
  // The mix: beyond 1 / c140.x the base and the picture by sat(c131.x); nearer, slot 14 comes in.
  const float share = Sat(c.ps[kCwP131][0]);
  const float t = Sat(F + c.ps[kCwP117][0]);
  const float w = Sat(c.ps[kCwP140][0] * d);
  float colour[3];
  for (int i = 0; i < 3; ++i) {
    const float base = (-c127[i] + c126[i]) * F + c127[i];
    const float r = in.picture[i] * kWaterOne;   // (the shadow factor of bool b137: 1; such a draw gives no record)
    const float beyond = (r + (-base)) * share + base;
    const float within = ((r + (-in.slot14[i])) * t + (-base + in.slot14[i])) * share + base;
    colour[i] = w * (beyond + (-within)) + within;
  }
  // The sun's glint, under bool b136: nothing of c136, c137, c138, c142, c143 is touched without it.
  if (c.sun_on) {
    const float* c142 = c.ps[kCwP142];
    const float* c143 = c.ps[kCwP143];
    const float c137x = c.ps[kCwP137][0];
    const float z2 = c.ps[kCwP136][0] * N[2];
    const float n2_inv = Rsq(std::fabs(z2 * z2 + ((N[0] * N[0] + N[1] * N[1]) + 0.0f)));
    const float n2[3] = {N[0] * n2_inv, N[1] * n2_inv, z2 * n2_inv};
    const float once = ((0.0f + n2[2] * (-c142[2])) + n2[0] * (-c142[0])) + n2[1] * (-c142[1]);
    const float twice = once + once;
    const float W[3] = {(-n2[0]) * twice + (-c142[0]), (-n2[1]) * twice + (-c142[1]), (-n2[2]) * twice + (-c142[2])};
    const float glint = Sat(((0.0f + (-V[2]) * W[2]) + (-V[0]) * W[0]) + (-V[1]) * W[1]);
    float power = 0.0f;
    if (glint > 0.0f) {   // the guard: 0 on a cosine of 0
      const float log = float(std::log2(double(glint)));
      power = float(std::exp2(double(c.ps[kCwP138][0] * log)));
    }
    const float scaled = c137x * power;
    out.sun[0] = (c143[0] * c137x) * power;
    out.sun[1] = scaled * c143[1];
    out.sun[2] = scaled * c143[2];
    for (int i = 0; i < 3; ++i) colour[i] = colour[i] + out.sun[i];
  }
  // The fog (light_formulas.water_fog: the ground form in the microcode's own order), from the view vector and the
  // distance the pixel has: beyond the start alone, and only in a fogged frame.
  if (c.fog_on && fog.on) {
    const float past = -fog.start + d;
    if (past > 0.0f) {
      out.fogged = true;
      out.fog_a = Sat(((0.0f + (-V[2]) * fog.axis[2]) + (-V[0]) * fog.axis[0]) + (-V[1]) * fog.axis[1]);
      out.fog_s = fog.scale * past;
      const float blend = Sat(fog.slope * past);
      for (int i = 0; i < 3; ++i) {
        const float S = in.scatter[i], T = in.transmit[i];
        const float haze = blend * (S + (-fog.near_colour[i])) + fog.near_colour[i];
        float G = ((-T + kWaterOne) * fog.scatter) * haze + (-colour[i]);
        G = T * colour[i] + G;
        colour[i] = G * fog.amount + colour[i];
      }
    }
  }
  for (int i = 0; i < 3; ++i) {
    out.colour[i] = colour[i] * c.out_scale;
    out.normal[i] = N[i];
  }
  out.fresnel = F;
  out.distance = d;
  return out;
}
// What the program writes: `out` the colour as the lit draws write the linear target ((v > 0) ? min(v,
// kPostSceneMax) : 0 a channel), `alpha` the output's, `kept` whether the pixel is drawn: not killed, the state's
// alpha test passed (alpha > the draw's reference), and the colour and the alpha all finite. A pixel whose colour
// or alpha is no number is DISCARDED (ledger ruling 34: the draw is blended, and a no-number clamped to 0 would be
// a black pixel at alpha 0.997). A skipped pixel that passes the state's test is drawn with colour 0 at its alpha,
// as in the game.
inline void WaterPixelMirror(const WaterPixelIn& in, const ClayWaterConstants& c, const ClayFogConstants& fog, float out[3], float* alpha, bool* kept) {
  const WaterPixelValues p = WaterPixelCore(in, c, fog);
  const bool finite = std::isfinite(p.colour[0]) && std::isfinite(p.colour[1]) && std::isfinite(p.colour[2]) && std::isfinite(p.alpha);
  for (int i = 0; i < 3; ++i) {
    const float v = p.colour[i];
    out[i] = finite && v > 0.0f ? (v < kPostSceneMax ? v : kPostSceneMax) : 0.0f;
  }
  if (alpha) *alpha = p.alpha;
  if (kept) *kept = finite && WaterDrawn(p.fate, p.alpha, c);
}
// WATER MIRRORS END

// --- Phase 13c's rules (plan Task 15 found them; Tasks 16 and 17 wire them) ---
// 1. Which view fills which target. The game's k-th reflection view of a frame is the picture at the k-th
// smallest slot-13 base, which is target k of PlanWaterTargets. With as many views as targets, view k is target
// k's; with more or fewer nothing can be said and every target holds the sky alone (`target_of_view` all
// kNoWaterTarget, false). At most kMaxReflectionViews views a frame (spec ruling 19).
inline constexpr uint32_t kMaxReflectionViews = 8;
inline constexpr uint8_t kNoWaterTarget = 0xFF;
constexpr bool PlanWaterViews(uint32_t views, const WaterTargetPlan& plan, uint8_t target_of_view[kMaxReflectionViews]) {
  const bool tied = plan.ok && plan.count != 0 && views == plan.count;
  for (uint32_t k = 0; target_of_view && k < kMaxReflectionViews; ++k) target_of_view[k] = tied && k < views ? uint8_t(k) : kNoWaterTarget;
  return tied && target_of_view != nullptr;
}
// 2. The cull. A view's far terrain is drawn with the cull of THE VIEW RECORD'S OWN word (PA_SU_SC_MODE_CNTL,
// register 0x2205: bit 0 cull front, bit 1 cull back, bit 2 "clockwise faces are front"): 0x00218006 in 358 of
// 358 captured view rows, where the main scene's far terrain has 0x00218002. The mirror turns the handedness, so
// the game turns the winding: what a view draws of the ground is its upper side as the mirrored eye sees it. With
// the main scene's word instead the third spot's first view has 23,675 silhouette texels for the game's 2,327
// (a hill seen from below over half the picture). Not valid: both cull bits (as DecodeRenderState has it).
struct WaterViewCull {
  bool valid = false;
  capture::Cull cull = capture::Cull::kNone;
  bool front_cw = false;
};
constexpr WaterViewCull ReflectionViewCull(uint32_t su_sc_mode) {
  const bool front = (su_sc_mode & 1u) != 0, back = (su_sc_mode & 2u) != 0;
  if (front && back) return WaterViewCull{};
  return WaterViewCull{true, front ? capture::Cull::kFront : back ? capture::Cull::kBack : capture::Cull::kNone, (su_sc_mode & 4u) != 0};
}
// Whether a triangle that is clockwise on the screen (`clockwise`, in whatever screen convention the caller keeps
// for every camera) is drawn under a cull: a front face is one whose winding is the word's.
constexpr bool WaterTriangleDrawn(const WaterViewCull& cull, bool clockwise) {
  if (!cull.valid) return false;
  const bool front = clockwise == cull.front_cw;
  return cull.cull == capture::Cull::kNone || (cull.cull == capture::Cull::kBack ? front : !front);
}
// The handedness of a camera's rows: the sign of the determinant of rows 0, 1 and 3 over (x, y, z). The mirrored
// camera's is the main camera's turned (WaterViewRows negates one column and scales by positive factors), which is
// why its winding is. 0 for rows that are not independent or not numbers.
inline int ViewHandedness(const float rows[4][4]) {
  if (!rows) return 0;
  const double a[3] = {double(rows[0][0]), double(rows[0][1]), double(rows[0][2])};
  const double b[3] = {double(rows[1][0]), double(rows[1][1]), double(rows[1][2])};
  const double w[3] = {double(rows[3][0]), double(rows[3][1]), double(rows[3][2])};
  const double det = w[0] * (a[1] * b[2] - a[2] * b[1]) + w[1] * (a[2] * b[0] - a[0] * b[2]) + w[2] * (a[0] * b[1] - a[1] * b[0]);
  return det > 0.0 ? 1 : det < 0.0 ? -1 : 0;
}
// Whether a view record's cull is the main scene's far terrain's with the winding turned exactly as its camera
// turns the handedness: the same cull, and "clockwise is front" differing iff the two cameras' handedness does.
// A view whose word is anything else is not the mirror the targets assume (its target then holds the sky alone).
inline bool WaterViewCullConsistent(uint32_t main_word, const float main_rows[4][4], uint32_t view_word, const float view_rows[4][4]) {
  const WaterViewCull m = ReflectionViewCull(main_word), v = ReflectionViewCull(view_word);
  const int hm = ViewHandedness(main_rows), hv = ViewHandedness(view_rows);
  if (!m.valid || !v.valid || hm == 0 || hv == 0) return false;
  return m.cull == v.cull && (m.front_cw != v.front_cw) == (hm != hv);
}
// 3. The clip at the water's plane. A view draws nothing below the plane z = h of its target: the far terrain's
// mirrored vertex program writes the world position's z - h as SV_ClipDistance0 (without the cut the bridge's
// second view has 1,145 silhouette texels for the game's 548). Kept iff the distance is at least 0.
constexpr float WaterClipDistance(float world_z, float h) { return world_z - h; }
// And the mirrored sky's (plan Task 11, shaders::kSkyMirrorVs): the third component of the pixel's ray,
// ray0.z + X * rayx.z + Y * rayy.z with (X, Y) the vertex's clip position: the half of a target below the horizon
// is not drawn and stays 0, as in the game's pictures. The ray is linear in (X, Y), so the clip is exact.
inline float WaterSkyClipDistance(const float basis[3][3], float X, float Y) {
#pragma clang fp contract(off)
  return basis[0][2] + X * basis[1][2] + Y * basis[2][2];
}
// 4. The blur (pixel shader F186D27249B462F8 after vertex shader B2D99AC2C7FB40D6: the game blurs some views).
// One pass of SEVEN EQUAL TAPS: oC0.rgb = weight * the sum over k of the sharp picture at (u, v) + offset[k], alpha
// 0; the offsets are the xy of the blur row's c116 to c122 (captured: the centre and a hexagon of radius 2 / 256)
// and the weight its literal c255.x (1 / 7). (u, v) IS THE PIXEL'S INTEGRAL POSITION over the target's size, not
// the texel's centre: with it the game's own sharp and blurred pair agree within 1 of 255 at every texel (mean
// 0.031 to 0.045); at the centre the mean is 0.32 to 0.46. The taps are added in the microcode's order (c117,
// c116, c118 to c122); a tap that is not finite counts as 0 (light_formulas.water_blur7).
inline constexpr uint32_t kWaterBlurTaps = 7;
inline constexpr uint8_t kWaterBlurOrder[kWaterBlurTaps] = {1, 0, 2, 3, 4, 5, 6};
// What the captured rows hold, in texels of the 256 x 256 picture, and the weight (light_formulas.
// WATER_BLUR_TEXELS, WATER_BLUR_WEIGHT): for tests; a native blur takes the offsets from the view's own row.
inline constexpr float kWaterBlurTexels[kWaterBlurTaps][2] = {{0.0f, 0.0f},         {2.0f, 0.0f},          {1.0f, 1.7320508f}, {-1.0f, 1.7320508f},
                                                              {-2.0f, 0.0f},        {-1.0f, -1.7320508f}, {1.0f, -1.7320508f}};
inline constexpr float kWaterBlurWeight = 0.142857149f;
struct WaterBlurConstants {
  float offset[kWaterBlurTaps][4];   // c116..c122: the xy are the tap's offset in the picture's coordinate; z and w are 0 here
  float weight, inv_size;            // the literal c255.x; 1 / the target's size
  float pad[2];
};
static_assert(sizeof(WaterBlurConstants) == 128);
// The blur's constants from the row's seven registers (whole, as captured: only their xy are read) and its
// literal. False, `out` untouched, for an offset that is no number or beyond one picture in either direction, or a
// weight that is not in (0, 1].
inline bool MakeWaterBlurConstants(const float registers[kWaterBlurTaps][4], float weight, WaterBlurConstants* out) {
  if (!registers || !out || !(weight > 0.0f && weight <= 1.0f)) return false;
  WaterBlurConstants c{};
  for (uint32_t k = 0; k < kWaterBlurTaps; ++k) {
    if (!(std::fabs(registers[k][0]) <= 1.0f) || !(std::fabs(registers[k][1]) <= 1.0f)) return false;   // (false for no number)
    c.offset[k][0] = registers[k][0];
    c.offset[k][1] = registers[k][1];
  }
  c.weight = weight;
  c.inv_size = 1.0f / float(kWaterTargetSize);
  *out = c;
  return true;
}
// The coordinate a pixel of the blur reads about: its integral position (the floor of SV_Position.xy) over the size.
inline void WaterBlurCoord(float pixel_x, float pixel_y, const WaterBlurConstants& c, float uv[2]) {
  uv[0] = std::floor(pixel_x) * c.inv_size;
  uv[1] = std::floor(pixel_y) * c.inv_size;
}
// The blurred colour of the pixel at (pixel_x, pixel_y): `sample(u, v, rgb)` reads the sharp picture with a linear
// filter. In double, as the reference is; `out` is rgb and alpha 0.
template <typename Sample>
inline void WaterBlurMirror(float pixel_x, float pixel_y, const WaterBlurConstants& c, Sample&& sample, float out[4]) {
  float uv[2];
  WaterBlurCoord(pixel_x, pixel_y, c, uv);
  double sum[3] = {0.0, 0.0, 0.0};
  for (uint32_t i = 0; i < kWaterBlurTaps; ++i) {
    const uint32_t k = kWaterBlurOrder[i];
    float rgb[3] = {0.0f, 0.0f, 0.0f};
    sample(uv[0] + c.offset[k][0], uv[1] + c.offset[k][1], rgb);
    for (int j = 0; j < 3; ++j) sum[j] += std::isfinite(rgb[j]) ? double(rgb[j]) : 0.0;
  }
  for (int j = 0; j < 3; ++j) {
    const double v = sum[j] * double(c.weight);
    out[j] = std::isfinite(v) ? float(v) : 0.0f;
  }
  out[3] = 0.0f;
}

// The log's and F3's Water line. "Water: off" with the verdict off. Else the surface records the step drew of the
// game's surface draws seen, the targets, the dead draws left out, the frames since the start that were not drawn
// by the verdict's reason, and the draws that gave no record by the capture's.
inline std::string FormatWaterText(const ClayStats& st) {
  if (st.water == ClayWater::kOff) return "Water: off";
  char buf[160];
  std::snprintf(buf, sizeof(buf), "Water: drawn %u of %u, targets %u, opaque left out %u | not drawn {", st.water_drawn, st.water_seen,
                st.water_targets, st.water_left_out);
  std::string text = buf;
  bool first = true;
  for (size_t i = 0; i < size_t(ClayWater::kCount); ++i) {
    if (ClayWater(i) == ClayWater::kDrawn || ClayWater(i) == ClayWater::kOff || !st.water_frames[i]) continue;
    std::snprintf(buf, sizeof(buf), "%s%s: %u frames", first ? "" : ", ", ClayWaterName(ClayWater(i)), st.water_frames[i]);
    text += buf;
    first = false;
  }
  text += "} | ";
  text += capture::kWaterRecordReason;
  text += " {";
  first = true;
  for (size_t i = size_t(capture::WaterReason::kNone) + 1; i < size_t(capture::WaterReason::kCount); ++i) {
    if (!st.water_capture[i]) continue;
    std::snprintf(buf, sizeof(buf), "%s%s: %u", first ? "" : ", ", capture::WaterReasonName(capture::WaterReason(i)), st.water_capture[i]);
    text += buf;
    first = false;
  }
  return text + "}";
}

// --- Native wind (frame-map section 18, "Native wind"; docs/native-renderer/lighting-formulas.md, "Wind") ---
//
// The game sways its trunks, leaves and instanced plants in the vertex shader. The pass draws a
// record that has wind inputs (FrameScene::wind, ::shadow_wind) with ONE wind vertex program
// (shaders::kClayWindVs: the clay vertex shader's text under CLAY_WIND) in every pass that draws
// it: the colour pass, the depth pre-pass and its shadow map. The program moves the position by the
// family's function before the rows; the family is two bits of light_mode (kLightModeWind*). What
// it reads: the per-vertex wind words at t9 (wind_decode.h; a wind record is never a terrain
// record), and ClayWindConstants in the tail of the record's slot at b1. Everything else of the
// draw (the cut's test, the light's inputs, the normals) is of the position at rest (spec ruling
// 21). A record whose verdict is not kDrawn is today's record with today's program in every pass
// (DecideClayWind: all or none), never "the wind program with zero constants".
//
// Arithmetic (ledger ruling 35): the mirrors below and the three HLSL functions compute in single
// precision, in the order of operations of the reference (tools/xdk_sigmatch/light_formulas.py:
// wind_plant; wind_tree_terms, wind_tree_offset; wind_leaf_lift, wind_leaf), term for term; the
// clock is the game's single-precision register and no phase is reduced before it is narrowed. The
// tests hold the mirrors to the pins of tools/xdk_sigmatch/wind_pins.py within 2e-5 of a unit.
// Ledger ruling 36: where a leaf's moved offset has zero squared length in this precision the
// position is the moved pivot, and nothing is divided by that length.

// The wind's constants of one record: the tail of cbuffer Object under CLAY_WIND (o_wind_reg,
// o_wind_lit). reg: a tree's c15, c16, c17 as the capture read them; a plant's five registers
// packed: (c10.x, c25.x, c26.x, c26.y), (c21.xyz, c7.x + c7.y), (c22.xyz, 0). lit: the family's
// literals by SLOT, slot k at lit[k / 4][k % 4] (ledger ruling 31; wind_slot below), each the bits
// of the table row's literal; a slot the family does not have is zero.
struct ClayWindConstants {
  float reg[3][4];
  float lit[7][4];
};
inline constexpr uint32_t kClayWindOffset = 288;  // the block's place in the record's slot: behind cbuffer Object's own fields
static_assert(sizeof(ClayWindConstants) == 160 && kClayWindOffset == sizeof(ClayObjectConstants) &&
              kClayWindOffset + sizeof(ClayWindConstants) <= kClayObjectSlotBytes &&
              capture::kWindLiterals == 28 && sizeof(ClayWindConstants::lit) == capture::kWindLiterals * sizeof(float));
// The new large slots (kClayObjectSlotBytes each) a frame's wind records may take beyond the lit
// objects' own: an unlit record's, a lit plant's and a caster's. 4096 (2 MB): about ten times what
// a captured frame needs (132 to 228 scene draws and 110 to 228 casters). A record past it is at
// rest (ClayWind::kSlot).
inline constexpr uint32_t kClayWindSlotsMax = 4096;

// The literals' slots by name: light_formulas.WIND_LITERAL_ORDER, a tree's (a leaf's 28, of which
// a trunk has the first 18) and a plant's (7). tests/test_wind_literals.py holds both lists, and
// the HLSL's, to that order name by name: two literals of one value (flutter_y and one are both 1)
// are told apart by no pin.
namespace wind_slot {
enum Tree : uint32_t {
  half, base, rate1, rate2, two_pi, quarter, lean, gust, phase_x, phase_y, lo, amp, along, across, rate0, zero, sel_zero, sel_one,
  hundred, slack, phase_f, flutter_x, flutter_y, one, back, push_gust, lift_gust, rate_f, kTreeCount
};
enum Plant : uint32_t { p_lo, p_two_pi, p_inv_two_pi, p_gain, p_height, p_half, p_unit, kPlantCount };
}  // namespace wind_slot
static_assert(wind_slot::kTreeCount == capture::WindLiteralCount(capture::WindFamily::kLeaf) &&
              wind_slot::hundred == capture::WindLiteralCount(capture::WindFamily::kTrunk) &&
              wind_slot::kPlantCount == capture::WindLiteralCount(capture::WindFamily::kPlant));

// The constants of a record's inputs and its table row, `c7_sum` being a plant's c7.x + c7.y (the
// draw's offset: the constant part of every copy's seed), rounded to single precision.
inline ClayWindConstants PackClayWindConstants(const capture::WindInputs& wind, const capture::WindSpec& spec, float c7_sum) {
  ClayWindConstants c{};
  if (wind.family == capture::WindFamily::kPlant) {
    const float (*r)[4] = wind.reg;  // c10, c21, c22, c25, c26
    c.reg[0][0] = r[0][0]; c.reg[0][1] = r[3][0]; c.reg[0][2] = r[4][0]; c.reg[0][3] = r[4][1];
    c.reg[1][0] = r[1][0]; c.reg[1][1] = r[1][1]; c.reg[1][2] = r[1][2]; c.reg[1][3] = c7_sum;
    c.reg[2][0] = r[2][0]; c.reg[2][1] = r[2][1]; c.reg[2][2] = r[2][2]; c.reg[2][3] = 0.0f;
  } else {
    std::memcpy(c.reg, wind.reg, sizeof(c.reg));  // c15, c16, c17
  }
  const uint32_t count = spec.lit_count < capture::kWindLiterals ? spec.lit_count : capture::kWindLiterals;
  for (uint32_t k = 0; k < count; ++k) std::memcpy(&c.lit[k / 4][k % 4], &spec.lit_bits[k], sizeof(float));
  return c;
}
inline ClayWindConstants MakeClayWindConstants(const capture::WindInputs& wind, const capture::WindSpec& spec, const capture::DrawRecord& r) {
  return PackClayWindConstants(wind, spec, r.instances.offset[0] + r.instances.offset[1]);
}
// Whether every value of the block is a number (the registers are, by the capture's rule; the sum
// of a plant's offset is made here).
inline bool ClayWindConstantsFinite(const ClayWindConstants& c) {
  for (const auto& row : c.reg) {
    for (float v : row) {
      if (!std::isfinite(v)) return false;
    }
  }
  for (const auto& row : c.lit) {
    for (float v : row) {
      if (!std::isfinite(v)) return false;
    }
  }
  return true;
}

// What the pass knows of one record when it decides its wind, and the verdict: the first thing
// missing decides, and only a record with everything is moved (kDrawn), in every pass that draws
// it. on: the frame has the wind. has_inputs: the record has wind inputs. terrain: it is a terrain
// record (t9 is its normals': never moved). has_words: its wind words exist for every vertex it
// draws. has_colour_pipeline, has_depth_pipeline: the wind program's pipelines of the record's
// state (a caster: its map's, both; a record that is not in the pre-pass: its colour pipeline's,
// both). has_slot: a slot for its constants, which are all numbers.
struct ClayWindInputs {
  bool on, has_inputs, terrain, has_words, has_colour_pipeline, has_depth_pipeline, has_slot;
};
constexpr ClayWind DecideClayWind(const ClayWindInputs& in) {
  if (!in.on) return ClayWind::kOff;
  if (!in.has_inputs || in.terrain) return ClayWind::kNone;
  if (!in.has_words) return ClayWind::kBuffer;
  if (!in.has_colour_pipeline || !in.has_depth_pipeline) return ClayWind::kPipeline;
  if (!in.has_slot) return ClayWind::kSlot;
  return ClayWind::kDrawn;
}

namespace wind_detail {
inline float Frac(float x) { return x - std::floor(x); }
inline float Lit(const ClayWindConstants& c, uint32_t slot) { return c.lit[slot / 4][slot % 4]; }
// A phase as the tree shaders wrap it, in [-pi, pi): its sine is sin(2 pi q).
inline float TreePhase(float q, const ClayWindConstants& c) {
  return Frac(Frac(q) + Lit(c, wind_slot::half)) * Lit(c, wind_slot::two_pi) + Lit(c, wind_slot::lo);
}
}  // namespace wind_detail

// light_formulas.wind_plant: `p` the position at rest (in the world), `seed` A3 + B3 of the
// vertex's copy and `z` the mesh vertex's third coordinate (the two wind words), `c` a plant's
// constants. A vertex at or below its root stands, exactly.
inline void WindPlantMirror(const float p[3], float seed, float z, const ClayWindConstants& c, float out[3]) {
  using namespace wind_slot;
  using wind_detail::Frac;
  using wind_detail::Lit;
  const float c10x = c.reg[0][0], c25x = c.reg[0][1], c26x = c.reg[0][2], c26y = c.reg[0][3];
  const float f = Frac(c.reg[1][3] + seed);
  float theta = 1.0f - c10x * c25x * (f * Lit(c, p_gain) + 1.0f) + f;
  theta = Frac(theta * Lit(c, p_inv_two_pi) + Lit(c, p_half)) * Lit(c, p_two_pi) + Lit(c, p_lo);
  const float h = Lit(c, p_height) * z;
  const float s = (h < 0.0f ? 0.0f : h > 1.0f ? 1.0f : h) * std::cos(theta);
  for (int k = 0; k < 3; ++k) out[k] = p[k] + s * (c.reg[1][k] * c26x + c.reg[2][k] * c26y);
}

// light_formulas.wind_tree_offset (with wind_tree_terms): what the wind adds to a trunk vertex and
// to a leaf's pivot. `w` = (b, a, d, c): the four weights in the fetch's source order; `c` a tree's
// constants (c15, c16, c17).
inline void WindTreeOffsetMirror(const float w[4], const ClayWindConstants& c, float off[3]) {
  using namespace wind_slot;
  using wind_detail::Lit;
  using wind_detail::TreePhase;
  const float* c15 = c.reg[0];
  const float* c16 = c.reg[1];
  const float* c17 = c.reg[2];
  const float b = w[0], a = w[1], d = w[2], cc = w[3];
  const float t = c15[0];
  const float g = c17[3] * Lit(c, gust);
  const float s0 = std::sin(TreePhase(t * Lit(c, rate0), c));
  const float s1 = std::sin(TreePhase(b * Lit(c, rate0) + (d * c16[0] + ((c16[1] + Lit(c, rate1)) * t + g)), c));
  const float s2 = std::sin(TreePhase(d * c16[0] + ((c16[1] + Lit(c, rate2)) * t + g), c));
  const float dd = d * c17[2] * d;
  const float along_v =
      dd * Lit(c, along) * (Lit(c, amp) * s1 * (std::fabs(Lit(c, amp) * s0) * Lit(c, quarter) + Lit(c, base)) + c17[2] * Lit(c, lean));
  const float across_v = dd * Lit(c, across) * s2;
  const float s3 = std::sin(TreePhase(t * Lit(c, phase_y) + b, c));
  const float turn = c16[2] * t + g;
  const float s4 = std::sin(TreePhase((turn + Lit(c, phase_x)) * cc + b, c));
  const float s5 = std::sin(TreePhase((turn + Lit(c, phase_y)) * cc + b, c));
  const float scale = (Lit(c, amp) * s3 + Lit(c, half)) * a * (Lit(c, half) * c17[2]) * c15[2];
  const float u = Lit(c, amp) * s4 * scale, v = Lit(c, amp) * s5 * scale;
  off[0] = along_v * c17[0] - across_v * c17[1] + u;
  off[1] = along_v * c17[1] + across_v * c17[0] + v;
  off[2] = -(along_v * along_v) * Lit(c, quarter) - (u * u + v * v) * Lit(c, half);
}

// light_formulas.wind_trunk: p + the offset. A vertex whose four weights are zero stands, exactly.
inline void WindTrunkMirror(const float p[3], const float w[4], const ClayWindConstants& c, float out[3]) {
  float off[3];
  WindTreeOffsetMirror(w, c, off);
  for (int k = 0; k < 3; ++k) out[k] = p[k] + off[k];
}

// light_formulas.wind_leaf_lift: what the flutter adds to the third coordinate of P - Q. `v`: the
// flutter's three values (source x, y, z of fetch 5; ledger ruling 30).
inline float WindLeafLiftMirror(const float p[3], const float q[3], const float w[4], const float v[3], const ClayWindConstants& c) {
  using namespace wind_slot;
  using wind_detail::Frac;
  using wind_detail::Lit;
  using wind_detail::TreePhase;
  const float* c15 = c.reg[0];
  const float* c16 = c.reg[1];
  const float* c17 = c.reg[2];
  const float b = w[0], d = w[2];
  const float t = c15[0];
  const float g = c17[3] * Lit(c, gust);
  const float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
  const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
  const float clock = t + c17[3] * Lit(c, rate_f);
  const float s6 =
      std::sin(TreePhase(b * Lit(c, rate_f) + (((c16[1] + Lit(c, rate1)) * t + g) + c16[0] * d + Lit(c, phase_f)), c));
  const float s7 = std::sin(TreePhase((clock * Lit(c, flutter_x) + v[2]) + Lit(c, hundred) * v[0], c));
  const float turn = Frac(clock * Lit(c, flutter_y) + v[1]);  // (the shader doubles it and takes half of that)
  const float s8 = std::sin(Frac((turn + turn) * Lit(c, half) + Lit(c, half)) * Lit(c, two_pi) + Lit(c, lo));
  const float k = -(Lit(c, amp) * s6) > 0.0f ? Lit(c, back) * s6 : Lit(c, amp) * s6;
  const float push = Lit(c, amp) * s7 * std::max(length + Lit(c, slack), 0.0f) * c16[3];
  const float lift = c15[3] * (std::fabs(k) * Lit(c, amp) * s8) * k;
  return push * (c17[2] * Lit(c, push_gust) + Lit(c, one)) + lift * (c17[2] * Lit(c, lift_gust) + Lit(c, one));
}

// light_formulas.wind_leaf: the pivot sways as a trunk vertex does, and the vertex keeps its
// distance from it while its direction's third coordinate is lifted. `q` the pivot, `w` the four
// weights, `v` the flutter's three values. Ledger ruling 36: where the moved offset has zero
// squared length in this function's own precision (a vertex at its pivot whose lift is 0, or a
// square that underflows), out is the moved pivot, and nothing is divided by that length.
inline void WindLeafMirror(const float p[3], const float q[3], const float w[4], const float v[3], const ClayWindConstants& c, float out[3]) {
  float off[3];
  WindTreeOffsetMirror(w, c, off);
  const float pivot[3] = {q[0] + off[0], q[1] + off[1], q[2] + off[2]};
  float d[3] = {p[0] - q[0], p[1] - q[1], p[2] - q[2]};
  const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  d[2] += WindLeafLiftMirror(p, q, w, v, c);
  const float turned2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
  if (!(turned2 > 0.0f)) {
    for (int k = 0; k < 3; ++k) out[k] = pivot[k];
    return;
  }
  const float turned = std::sqrt(turned2);
  for (int k = 0; k < 3; ++k) out[k] = pivot[k] + d[k] / turned * length;
}

// An upper bound of how far the wind moves a vertex of a record (light_formulas.wind_reach_plant,
// wind_reach_tree, wind_reach_leaf: every sine at 1), computed in double and rounded up, so that it
// is never below the reference's: what a box of the record's rest positions is grown by
// (GrowBounds). A plant's is its registers' alone; a tree's needs the mesh's extent (ledger ruling
// 32: the largest weight; a leaf's largest |P - Q|), as the geometry cache gives it with the wind
// words. +infinity for no family and for anything that is no number or below zero (GrowBounds
// then gives no box: the record is kept wherever a box would decide).
inline float ClayWindReach(capture::WindFamily family, const ClayWindConstants& c, const WindExtent& extent) {
  using namespace wind_slot;
  const float inf = std::numeric_limits<float>::infinity();
  auto lit = [&](uint32_t slot) { return double(wind_detail::Lit(c, slot)); };
  double reach = -1.0;
  if (family == capture::WindFamily::kPlant) {
    double sum = 0.0;
    for (int k = 0; k < 3; ++k) {
      const double x = double(c.reg[1][k]) * double(c.reg[0][2]) + double(c.reg[2][k]) * double(c.reg[0][3]);
      sum += x * x;
    }
    reach = std::sqrt(sum);
  } else if (family == capture::WindFamily::kTrunk || family == capture::WindFamily::kLeaf) {
    const double c15z = c.reg[0][2], c15w = c.reg[0][3], c16w = c.reg[1][3], c17x = c.reg[2][0], c17y = c.reg[2][1], c17z = c.reg[2][2];
    const double w = std::fabs(double(extent.weight_max));
    const double dd = w * w * std::fabs(c17z);
    const double along_v = dd * std::fabs(lit(along)) *
                           (std::fabs(lit(amp)) * (std::fabs(lit(amp)) * std::fabs(lit(quarter)) + std::fabs(lit(base))) + std::fabs(c17z * lit(lean)));
    const double across_v = dd * std::fabs(lit(across));
    const double wobble = std::fabs(lit(amp)) * (std::fabs(lit(amp)) + std::fabs(lit(half))) * w * std::fabs(lit(half) * c17z) * std::fabs(c15z);
    const double flat = std::hypot(c17x, c17y) * std::hypot(along_v, across_v) + std::sqrt(2.0) * wobble;
    const double drop = std::fabs(lit(quarter)) * along_v * along_v + std::fabs(lit(half)) * 2.0 * wobble * wobble;
    reach = std::hypot(flat, drop);
    if (family == capture::WindFamily::kLeaf) {
      const double length = std::fabs(double(extent.length_max));
      const double k = std::max(std::fabs(lit(amp)), std::fabs(lit(back)));
      const double push = std::fabs(lit(amp)) * std::max(length + lit(slack), 0.0) * std::fabs(c16w);
      const double lift = std::fabs(c15w) * k * std::fabs(lit(amp)) * k;
      const double turn = push * (std::fabs(c17z * lit(push_gust)) + std::fabs(lit(one))) + lift * (std::fabs(c17z * lit(lift_gust)) + std::fabs(lit(one)));
      reach += std::min(2.0 * turn, 2.0 * length);
    }
  }
  if (!std::isfinite(reach) || reach < 0.0) return inf;
  float up = float(reach);
  if (double(up) < reach) up = std::nextafter(up, inf);
  return up;
}

// The log's and F3's Wind line. "Wind: off" with the switch off. With it on and a scene captured
// without the wind (the frames around a switch): that. Else the scene's records drawn moved, by
// family, the shadow records drawn moved, and what has a wind shader and is drawn at rest, by
// reason: the capture's reasons (capture::WindReasonName past "no-entry": scene and shadow draws
// together) and the pass's (buffer, pipeline, slot).
inline std::string FormatWindText(const ClayStats& st) {
  if (!st.wind_switch) return "Wind: off";
  if (!st.wind_scene) return "Wind: on, no wind inputs in the scene";
  const uint32_t trunk = st.wind_moved[size_t(capture::WindFamily::kTrunk)], leaf = st.wind_moved[size_t(capture::WindFamily::kLeaf)],
                 plant = st.wind_moved[size_t(capture::WindFamily::kPlant)];
  char buf[160];
  std::snprintf(buf, sizeof(buf), "Wind: on, moved %u (trunk %u, leaf %u, plant %u), casters %u | at rest: {", trunk + leaf + plant, trunk,
                leaf, plant, st.wind_casters);
  std::string text = buf;
  bool first = true;
  auto add = [&](const char* name, uint32_t n) {
    if (!n) return;
    std::snprintf(buf, sizeof(buf), "%s%s: %u", first ? "" : ", ", name, n);
    text += buf;
    first = false;
  };
  for (size_t i = size_t(capture::WindReason::kNoEntry) + 1; i < size_t(capture::WindReason::kCount); ++i) {
    add(capture::WindReasonName(capture::WindReason(i)), st.wind_capture_rest[i]);
  }
  for (ClayWind w : {ClayWind::kBuffer, ClayWind::kPipeline, ClayWind::kSlot}) add(ClayWindName(w), st.wind_rest[size_t(w)]);
  return text + "}";
}

// The log's and F3's Coverage line. "Coverage: off" with the switch off. With it on: the late records drawn at
// half, the early records drawn by their two parts and how many of those have their half-covered pixels in the
// tail, the coverage records drawn as today's by reason, and, in a frame whose sky step was moved before its
// first blend of a coverage record, over how many other drawn records it moved.
inline std::string FormatCoverageText(const ClayStats& st) {
  if (!st.coverage_switch) return "Coverage: off";
  char buf[192];
  std::snprintf(buf, sizeof(buf), "Coverage: on, late %u, early %u (%u in the tail) | not drawn: {", st.coverage_late, st.coverage_early,
                st.coverage_tail_draws);
  std::string text = buf;
  bool first = true;
  for (ClayCoverage c : {ClayCoverage::kFull, ClayCoverage::kPipeline}) {
    const uint32_t n = st.coverage_rest[size_t(c)];
    if (!n) continue;
    std::snprintf(buf, sizeof(buf), "%s%s: %s %u", first ? "" : ", ", kCoverageReason, ClayCoverageName(c), n);
    text += buf;
    first = false;
  }
  text += "}";
  if (st.coverage_sky_moved) {
    std::snprintf(buf, sizeof(buf), " | the sky step moved before the first blend, over %u other records (%u without depth write)",
                  st.coverage_sky_over, st.coverage_sky_over_no_depth);
    text += buf;
  }
  return text;
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

// The F3 state line: pipelines in the pass's two caches (with those of the wind's
// and the coverage's cache and the keys it refused, when there are any, and with
// the draws that found no pipeline, when there are any) and the drawn records with
// the alpha test applied, with a blend, and with an invalid state.
inline std::string FormatStateText(const ClayStats& st) {
  std::string text = "State: " + std::to_string(st.pipelines) + " pipelines";
  if (st.pipeline_variants || st.pipeline_refused) {
    text += " (wind and coverage " + std::to_string(st.pipeline_variants) + " of " + std::to_string(kMaxClayVariantPipelines);
    if (st.pipeline_refused) text += ", refused " + std::to_string(st.pipeline_refused);
    text += ")";
  }
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
  // The wind words (foliage plan Task 9): only in a frame that asked for some, so that a frame
  // without the wind has the line of before.
  std::string wind_words;
  {
    uint32_t refused = 0;
    for (size_t i = 1; i < size_t(WindBuild::kCount); ++i) refused += st.wind_build[i];
    if (st.wind_words || refused) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), " | wind words: %u records, %.1f MB", st.wind_words,
                    double(st.wind_words_bytes) / (1024.0 * 1024.0));
      wind_words = buf;
      bool first = true;
      for (size_t i = 1; i < size_t(WindBuild::kCount); ++i) {
        if (!st.wind_build[i]) continue;
        std::snprintf(buf, sizeof(buf), "%s%s: %u", first ? ", refused {" : ", ", WindBuildName(WindBuild(i)), st.wind_build[i]);
        wind_words += buf;
        first = false;
      }
      if (!first) wind_words += "}";
    }
  }
  char cap[64];
  std::snprintf(cap, sizeof(cap), "\nCapture: %.2f ms guest time per frame\n", scene.capture_ms);
  return text + geo + wind_words + "\n" + FormatStateText(st) + cap +
         (tex ? FormatTextureText(*tex, st.drawn, st.alpha_unknown, st.blend_unknown) : std::string("Textures: off"));
}

}  // namespace fable2::native::render
