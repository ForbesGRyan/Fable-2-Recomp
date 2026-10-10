#pragma once

// Per-frame scene snapshot built on the game thread and consumed on the GPU
// thread (pure: no SDK/GPU deps). Pattern: skate3recomp's FrameScene.

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "../capture/draw_record.h"
#include "../capture/fog_block.h"
#include "../capture/light_block.h"
#include "../capture/post_block.h"
#include "../capture/quad_record.h"
#include "../capture/shader_tally.h"
#include "../capture/shadow_block.h"

namespace fable2::native::render {

inline constexpr size_t kSkippedTallySize = 32;
inline constexpr size_t kUntexturedTallySize = 64;
inline constexpr size_t kUntexturedTop = 8;
// The most records one shadow pass's list holds (FrameScene::shadow_draws): the
// passes have 110 to 228 draws in the captures of frame-map section 15. A pass
// with more is recorded up to here and the rest counted (over-limit).
inline constexpr size_t kMaxShadowRecords = 1024;

struct FrameScene {
  uint64_t frame = 0;
  std::vector<capture::DrawRecord> draws;
  // Side array of `draws`: the terrain inputs of its terrain records, each at
  // its record's terrain_index (TerrainOf). Built with the records, handed
  // over with them (FrameBuilder::Finish) and, like them, not written again
  // while anything refers to the scene.
  std::vector<capture::TerrainPatch> terrain;
  // Side array of `draws`: the light inputs of its records that have a light
  // family and readable inputs, each at its record's light_index (LightOf).
  // Built, handed over and left alone like `terrain`.
  std::vector<capture::LightInputs> lights;
  // The frame's light block (light_block.h): the sun and the ambient constants
  // every lit shader shares, read once in the frame, at the first main-scene
  // draw of a one-light family at which the light set was loaded in this scene
  // and holds the sun first. Not valid (all zero) when no such draw gave one:
  // a frame without records, a frame the view was switched on in after its
  // scene began, a scene without a sun. A consumer then draws the frame
  // without light ("light-block") and never with another frame's block.
  // light_tries: how many reads were made that gave no block, one per light
  // set at most and eight a frame at most (light_block.h, LightBlockTries).
  capture::LightBlock light{};
  uint32_t light_tries = 0;
  // The game's two sun shadow passes of the frame (frame-map section 15), drawn
  // before the main scene: [0] the near map's draws, [1] the far map's, in the
  // order the game issued them, each a DrawRecord as a scene draw's
  // (draw_record.h, "Shadow-pass records": which fields the shadow pass reads).
  // They are lists of their own: no shadow record is in `draws`, and none
  // counts in `captured`, `skipped` or the material and light tallies.
  // shadow_casters[c][i] is the twin of shadow_draws[c][i]: the caster's kind
  // and whether the polygon offset applies to it. A twin array by the record's
  // own index, not an index in the record like terrain_index: every shadow
  // record has an entry, and the record stays as it is.
  // At most kMaxShadowRecords a list. Built, handed over and left alone like
  // `draws`.
  std::vector<capture::DrawRecord> shadow_draws[2];
  std::vector<capture::ShadowCaster> shadow_casters[2];
  // The frame's shadow block (shadow_block.h): the two light matrices, the
  // split distances, the texel sizes and the two passes' bias. Valid only when
  // the scene's registers were read and passed their rules, both passes have a
  // record that gives its light matrix, each such matrix is the registers' one
  // (the frame's proof, within 1e-4: kShadowProofTolerance) and each pass's records hold one polygon
  // offset. Otherwise all zero, with the reason in shadow_reason (kOff: no
  // shadow records were taken this frame): a consumer then draws the frame
  // without shadows ("shadow-block"), never with another frame's block or maps.
  capture::ShadowBlock shadow{};
  capture::ShadowBlockReason shadow_reason = capture::ShadowBlockReason::kOff;
  // Shadow records were taken this frame (the native view, the light and the
  // shadows on at the frame's begin).
  bool shadow_records = false;
  // Shadow-pass draws that were not recorded, per pass by reason (index: a
  // SkipReason's or a ShadowSkip's number) and, both passes together, per
  // (vertex shader hash, that number as the kind).
  uint32_t shadow_skipped[2][capture::kShadowSkipCount] = {};
  capture::ShaderTally<kSkippedTallySize> shadow_skipped_by_vs;
  // Draws on the shadow map that belong to neither pass ("shadow-pass-extra":
  // a third or later pass, a run after its pass was interrupted, draws before
  // the frame's first clear), and the passes the frame opened (clears of the map).
  uint32_t shadow_extra = 0;
  uint32_t shadow_passes = 0;
  // The frame's fog block (fog_block.h): the game's six fog registers and its two
  // tables, decoded, read once in the frame at the first main-scene colour draw
  // whose pixel shader has the game's fog block (capture::FindFogShader). Valid
  // only when that read passed capture::MakeFogBlock's rules; otherwise all
  // zero, with the reason in fog_reason (kOff: the fog was not wanted this
  // frame and nothing was read; kNoDraw: no such draw in the frame; else what
  // the last read failed on): a consumer then draws the frame without fog
  // ("fog-block"), never with another frame's registers or tables.
  // fog_tries: reads that gave no block (at most eight a frame).
  // fog_source: where the block's tables were read (address and content hash of
  // each), for the log and the discovery frame row only; zero without a block.
  capture::FogBlock fog{};
  capture::FogBlockReason fog_reason = capture::FogBlockReason::kOff;
  capture::FogSource fog_source{};
  uint32_t fog_tries = 0;
  // The frame's sky block (fog_block.h): three registers and two fetch constants
  // of the game's sky draw, read at the main scene's call of the indexed builder
  // 0x82217EE8 whose pixel shader is the sky's. Invalid (all zero) with the
  // reason in sky_reason, as the fog block ("sky-block"); sky_tries: reads at a
  // sky draw that gave none.
  capture::SkyBlock sky{};
  capture::SkyBlockReason sky_reason = capture::SkyBlockReason::kOff;
  uint32_t sky_tries = 0;
  // The fog and the sky were wanted this frame (the native view, the light and
  // the fog's switch on at the frame's begin).
  bool fog_records = false;
  // The frame's post block (post_block.h): the registers of the game's
  // composite, bright pass and blurs and the composite's table, decoded, read
  // at the frame's post rectangles outside the main scene. Valid only when a
  // block passed capture::MakePostBlock's rules AND pairs with this frame's
  // light block (capture::PostBlockPairs with light.out_scale): the frame's
  // own (post_age 0), or, when the frame read none that serves, the last valid
  // one of an earlier frame while that is at most capture::kPostBlockMaxAge
  // frames old (post_age 1 to 4: how many frames ago it was read). Otherwise
  // all zero, with the reason in post_reason (kOff: the post chain was not
  // wanted this frame and nothing was read; capture::ChoosePostBlock has the
  // others): a consumer then draws the frame without post ("post-block"),
  // never with a table older than the bound or one that does not pair. A frame
  // without a valid light block has no post block.
  // post_tries: reads at this frame's composite that gave no block (at most
  // eight a frame). post_source: where the block's table was read (address and
  // content hash), for the log and the discovery frame row only; zero without
  // a block.
  capture::PostBlock post{};
  capture::PostBlockReason post_reason = capture::PostBlockReason::kOff;
  uint32_t post_age = 0;
  capture::PostSource post_source{};
  uint32_t post_tries = 0;
  // The post block was wanted this frame (the native view, the light and the
  // post switch on at the frame's begin).
  bool post_records = false;
  // The frame's quad records (quad_record.h): the game's sky furniture and the quads of its sun
  // layer, one for each call of the indexed builder 0x82217EE8 that ran a shader of the generated
  // table at its kind's target, in the order the game issued them (a record's `sequence` is its
  // index here): the scene's kinds inside the main scene, the layer's after it. A bounded block of
  // the scene's own (at most capture::kMaxQuadRecords entries, allocated only for a frame that has
  // a set), not a part of `draws`: no DrawRecord grows for it.
  // `quads` is the frame's set, and it is whole or empty: it holds quad_count records, which is
  // not 0 only with quad_reason kNone, which says that every furniture call of the frame gave a
  // record and all of them fit. Otherwise it is empty and quad_reason says why (kOff: the quads were
  // not wanted and nothing was read; capture::ChooseQuadSet has the others): a consumer then draws
  // no furniture at all ("quad-record"), never the part of a set that was read, and never another
  // frame's (the flares come and go from one frame to the next).
  // quad_seen: the frame's furniture calls, quad_kinds: by kind; quad_taken: those that gave a
  // record that was kept; quad_refused: those that gave none, by reason; quad_over: records past
  // the bound.
  std::vector<capture::QuadRecord> quads;
  uint32_t quad_count = 0;
  capture::QuadSetReason quad_reason = capture::QuadSetReason::kOff;
  uint32_t quad_seen = 0;
  uint32_t quad_taken = 0;
  uint32_t quad_over = 0;
  uint32_t quad_kinds[size_t(capture::PostQuadKind::kCount)] = {};
  uint32_t quad_refused[size_t(capture::QuadRecordReason::kCount)] = {};
  // The sun layer's depth place (draw_record.h, "The sun layer's depth place"): the game's tree occluders of
  // this frame, each matched to the record of `draws` it draws again. layer_occluders: the occluder calls
  // seen (only with the quads wanted); layer_occluders_failed: those that named no single record (not read,
  // no twin, more than one, past capture::kMaxLayerOccluders); layer_depth_place: the smallest index in
  // `draws` among the twins, capture::kNoDepthPlace when no occluder named one. A consumer takes the sun
  // layer's depth before that record, after the last record when the frame had no occluder call, and draws
  // no layer when a call failed (render::PlanSunDepthPlace).
  uint32_t layer_occluders = 0;
  uint32_t layer_occluders_failed = 0;
  uint32_t layer_depth_place = capture::kNoDepthPlace;
  // The quads were wanted this frame (the native view, the light, the post chain and the sun's
  // switch on at the frame's begin).
  bool quad_records = false;
  // The drawable records with light inputs by family (index: LightFamily), how
  // many of them are a family's two- or four-light form, the records without
  // inputs by reason (index: LightReason), and the pixel shaders of those,
  // by count descending.
  uint32_t light_family[size_t(capture::LightFamily::kCount)] = {};
  uint32_t light_multi = 0;
  uint32_t light_reason[size_t(capture::LightReason::kCount)] = {};
  std::vector<std::pair<uint64_t, uint32_t>> light_unknown_ps;
  uint32_t captured = 0;
  uint32_t skipped[size_t(capture::SkipReason::kCount)] = {};
  // Skipped draws per (vertex shader hash, skip reason as the kind), for the
  // coverage log; unsupported-prim draws are broken down by hook instead.
  capture::ShaderTally<kSkippedTallySize> skipped_by_vs;
  // Capture-side material statuses of the drawable records.
  uint32_t material[size_t(capture::MaterialStatus::kCount)] = {};
  // Top pixel shader hashes of drawable records left untextured by the
  // capture (ps-unknown / no-albedo / uv-unsupported), by count descending.
  std::vector<std::pair<uint64_t, uint32_t>> untextured_ps;
  // False when no consumer wanted records this frame (debug view off): the
  // main-scene draws were only counted (captured), draws/skipped are empty.
  bool records = true;
  // Guest-thread time spent in the capture layer during this frame.
  double capture_ms = 0;
};

// The terrain inputs of record `r` of `scene`. Null for a record that is not a
// terrain record and for an index the scene's array does not hold (a record of
// another scene): the array is never read past its end. The pointer is into
// the scene and lives as long as it.
inline const capture::TerrainPatch* TerrainOf(const FrameScene& scene, const capture::DrawRecord& r) {
  if (r.terrain_index == capture::kNoSideIndex || r.terrain_index >= scene.terrain.size()) return nullptr;
  return &scene.terrain[r.terrain_index];
}

// The light inputs of record `r` of `scene`, with TerrainOf's rules: null for a
// record without inputs and for an index the scene's array does not hold.
inline const capture::LightInputs* LightOf(const FrameScene& scene, const capture::DrawRecord& r) {
  if (r.light_index == capture::kNoSideIndex || r.light_index >= scene.lights.size()) return nullptr;
  return &scene.lights[r.light_index];
}

// "name: n" pairs of one pass's skipped shadow draws by reason.
inline std::string ShadowSkipped(const FrameScene& scene, size_t cascade) {
  std::string out;
  char buf[64];
  for (size_t i = 1; cascade < 2 && i < capture::kShadowSkipCount; ++i) {
    if (!scene.shadow_skipped[cascade][i]) continue;
    std::snprintf(buf, sizeof(buf), "%s%s: %u", out.empty() ? "" : ", ", capture::ShadowSkipName(uint8_t(i)),
                  scene.shadow_skipped[cascade][i]);
    out += buf;
  }
  return out;
}

// The frame's shadow capture as the log writes it after "shadow: ":
//   block <valid|invalid(reason)>, near N (skipped {...}), far M (skipped {...}), extra K
// N, M: the records of the two passes; K: the draws on the map outside them,
// with "(shadow-pass-extra)" when there are any. "off" for a frame in which no
// shadow records were taken.
inline std::string ShadowLine(const FrameScene& scene) {
  if (!scene.shadow_records) return "off";
  std::string out = "block ";
  if (scene.shadow.valid) {
    out += "valid";
  } else {
    out += "invalid(";
    out += capture::ShadowBlockReasonName(scene.shadow_reason);
    out += ")";
  }
  char buf[64];
  const char* names[2] = {"near", "far"};
  for (size_t c = 0; c < 2; ++c) {
    std::snprintf(buf, sizeof(buf), ", %s %zu (skipped {", names[c], scene.shadow_draws[c].size());
    out += buf;
    out += ShadowSkipped(scene, c);
    out += "})";
  }
  std::snprintf(buf, sizeof(buf), ", extra %u", scene.shadow_extra);
  out += buf;
  if (scene.shadow_extra) {
    out += " (";
    out += capture::kShadowPassExtraReason;
    out += ")";
  }
  return out;
}

// "0xHASH(reason): n" pairs of the skipped shadow draws by vertex shader, the
// reasons in their order and within one the most frequent shader first.
inline std::string ShadowSkippedByVs(const FrameScene& scene) {
  std::string out;
  char buf[96];
  capture::ShaderTally<kSkippedTallySize>::Entry top[kSkippedTallySize];
  for (size_t kind = 1; kind < capture::kShadowSkipCount; ++kind) {
    const size_t n = scene.shadow_skipped_by_vs.Top(uint8_t(kind), top, kSkippedTallySize);
    for (size_t i = 0; i < n; ++i) {
      std::snprintf(buf, sizeof(buf), "%s0x%016llX(%s): %u", out.empty() ? "" : ", ",
                    static_cast<unsigned long long>(top[i].hash), capture::ShadowSkipName(uint8_t(kind)), top[i].count);
      out += buf;
    }
  }
  return out;
}

// The frame's fog and sky capture as the log writes it after "fog: ":
//   block valid, tries T, c28 (x, y) c66 (x, y, z) c67 (x, y, z, w) c68 (r, g, b) tf4 0x<address> <content> tf5 0x<address>
//   <content> | sky valid, tries T, c31.x a c46.x b c47.x c tf13 0x<dword 1> tf14 0x<dword 1>
// with "invalid(<reason>), tries T" in the place of a block that is missing
// (capture::FogBlockReasonName, SkyBlockReasonName). <address>, <content>: where
// each table was read and the hash of its bytes (FrameScene::fog_source), which
// a discovery capture of the same frame names the table by. The sky's two
// textures are named by their fetch constants' second dword (address, endian
// and format). "off" for a frame in which the fog was not wanted.
inline std::string FogLine(const FrameScene& scene) {
  if (!scene.fog_records) return "off";
  char buf[512];  // (sixteen %g values of at most 13 characters each, two addresses, two hashes and the fixed text: about 360)
  std::string out = "block ";
  if (scene.fog.valid) {
    const capture::FogBlock& f = scene.fog;
    std::snprintf(buf, sizeof(buf),
                  "valid, tries %u, c28 (%g, %g) c66 (%g, %g, %g) c67 (%g, %g, %g, %g) c68 (%g, %g, %g) tf4 0x%08X %016llX "
                  "tf5 0x%08X %016llX",
                  scene.fog_tries, f.amount[0], f.amount[1], f.axis[0], f.axis[1], f.axis[2], f.range[0], f.range[1], f.range[2],
                  f.range[3], f.near_colour[0], f.near_colour[1], f.near_colour[2], scene.fog_source.base[0],
                  static_cast<unsigned long long>(scene.fog_source.content[0]), scene.fog_source.base[1],
                  static_cast<unsigned long long>(scene.fog_source.content[1]));
  } else {
    std::snprintf(buf, sizeof(buf), "invalid(%s), tries %u", capture::FogBlockReasonName(scene.fog_reason), scene.fog_tries);
  }
  out += buf;
  out += " | sky ";
  if (scene.sky.valid) {
    std::snprintf(buf, sizeof(buf), "valid, tries %u, c31.x %g c46.x %g c47.x %g tf13 0x%08X tf14 0x%08X", scene.sky_tries,
                  scene.sky.c31x, scene.sky.c46x, scene.sky.c47x, scene.sky.fetch[0][1], scene.sky.fetch[1][1]);
  } else {
    std::snprintf(buf, sizeof(buf), "invalid(%s), tries %u", capture::SkyBlockReasonName(scene.sky_reason), scene.sky_tries);
  }
  out += buf;
  return out;
}

// The frame's post capture as the log writes it after "post: ":
//   block valid, age A, tries T, c20.w s, 37B1 (c31.x, c46.x, c47.x, c77.x, c77.y, c78.x), EE92 (c31.x, c46.x, c47.x,
//   c77.x, c77.y), table 0x<address> <content>, parts P
// with "block invalid(<reason>), tries T" for a frame without one
// (capture::PostBlockReasonName). A: 0 for the frame's own block, else how many
// frames ago the block was read. s: the frame's light block's output scale,
// which the block pairs with. <address>, <content>: where the table was read
// and the hash of its bytes (FrameScene::post_source), which a discovery
// capture of the frame names the table by. P: capture::kPostPartBlur when the
// blurs' weights are there. "off" for a frame in which the post block was not
// wanted.
inline std::string PostLine(const FrameScene& scene) {
  if (!scene.post_records) return "off";
  char buf[384];  // (twelve %g values of at most 13 characters each, an address, a hash and the fixed text: about 260)
  if (scene.post.valid) {
    const capture::PostBlock& p = scene.post;
    std::snprintf(buf, sizeof(buf),
                  "block valid, age %u, tries %u, c20.w %g, 37B1 (%g, %g, %g, %g, %g, %g), EE92 (%g, %g, %g, %g, %g), "
                  "table 0x%08X %016llX, parts %u",
                  scene.post_age, scene.post_tries, scene.light.out_scale, p.composite[0], p.composite[1], p.composite[2],
                  p.composite[3], p.composite[4], p.composite[5], p.bright[0], p.bright[1], p.bright[2], p.bright[3], p.bright[4],
                  scene.post_source.base, static_cast<unsigned long long>(scene.post_source.content), unsigned(p.parts));
  } else {
    std::snprintf(buf, sizeof(buf), "block invalid(%s), tries %u", capture::PostBlockReasonName(scene.post_reason),
                  scene.post_tries);
  }
  return buf;
}

// The frame's quad records as the log writes them after "quads: ":
//   set whole, N records {moon: 1, cloud: 3, ...}
//   set none(<reason>), seen S, taken T, refused {<reason>: n, ...}, over K {moon: 1, ...}
// N: the records of the frame's set, by kind in the braces. Without a set: why
// (capture::QuadSetReasonName), the frame's furniture calls, how many gave a record that was kept
// ("taken", left out with no call), those that gave none by reason and the records past the bound
// (each left out when there is none), and the calls by kind. "off" for a frame in which the quads
// were not wanted.
inline std::string QuadLine(const FrameScene& scene) {
  if (!scene.quad_records) return "off";
  char buf[96];
  std::string kinds;
  for (size_t i = 0; i < size_t(capture::PostQuadKind::kCount); ++i) {
    if (!scene.quad_kinds[i]) continue;
    std::snprintf(buf, sizeof(buf), "%s%s: %u", kinds.empty() ? "" : ", ", capture::PostQuadKindName(capture::PostQuadKind(i)),
                  scene.quad_kinds[i]);
    kinds += buf;
  }
  std::string out;
  if (scene.quad_reason == capture::QuadSetReason::kNone) {
    std::snprintf(buf, sizeof(buf), "set whole, %u records", scene.quad_count);
    out = buf;
  } else {
    std::snprintf(buf, sizeof(buf), "set none(%s), seen %u", capture::QuadSetReasonName(scene.quad_reason), scene.quad_seen);
    out = buf;
    if (scene.quad_seen) {
      std::snprintf(buf, sizeof(buf), ", taken %u", scene.quad_taken);
      out += buf;
    }
    std::string refused;
    for (size_t i = 1; i < size_t(capture::QuadRecordReason::kCount); ++i) {
      if (!scene.quad_refused[i]) continue;
      std::snprintf(buf, sizeof(buf), "%s%s: %u", refused.empty() ? "" : ", ",
                    capture::QuadRecordReasonName(capture::QuadRecordReason(i)), scene.quad_refused[i]);
      refused += buf;
    }
    if (!refused.empty()) out += ", refused {" + refused + "}";
    if (scene.quad_over) {
      std::snprintf(buf, sizeof(buf), ", over %u", scene.quad_over);
      out += buf;
    }
  }
  if (!kinds.empty()) out += " {" + kinds + "}";
  // (the layer's tree occluders, only in a frame that had one: the place the layer's depth is taken at)
  if (scene.layer_occluders) {
    if (scene.layer_depth_place != capture::kNoDepthPlace) {
      std::snprintf(buf, sizeof(buf), ", tree occluders %u (%u failed), depth before record %u", scene.layer_occluders,
                    scene.layer_occluders_failed, scene.layer_depth_place);
    } else {
      std::snprintf(buf, sizeof(buf), ", tree occluders %u (%u failed), no depth place", scene.layer_occluders,
                    scene.layer_occluders_failed);
    }
    out += buf;
  }
  return out;
}

// The record and side-array buffers of scenes nothing refers to any more, kept
// for the builder to fill again: a new allocation of that size is new pages,
// and a frame's records then cost a page fault per page on the guest thread.
// The list has its own lock, so buffers come back from whichever thread drops
// a scene's last reference (usually the render thread) without the capture's
// scene mutex; nothing is freed or allocated while the list's lock is held.
// At most kMaxSpares sets (records, terrain inputs, light inputs, the two
// shadow passes' records and casters) are kept. A buffer is in exactly one
// place: the builder, one scene, or this list.
class SceneBufferPool {
 public:
  static constexpr size_t kMaxSpares = 4;
  struct Buffers {
    std::vector<capture::DrawRecord> draws;
    std::vector<capture::TerrainPatch> terrain;
    std::vector<capture::LightInputs> lights;
    std::vector<capture::DrawRecord> shadow_draws[2];
    std::vector<capture::ShadowCaster> shadow_casters[2];

    bool HasMemory() const {
      return draws.capacity() != 0 || terrain.capacity() != 0 || lights.capacity() != 0 ||
             shadow_draws[0].capacity() != 0 || shadow_draws[1].capacity() != 0 ||
             shadow_casters[0].capacity() != 0 || shadow_casters[1].capacity() != 0;
    }
  };

  // The buffers of a scene whose last reference has dropped, with whatever
  // they hold (Take's caller clears them). Kept if the list has room, else
  // freed here, after the lock. Buffers without memory are not kept.
  void Return(Buffers&& buffers) {
    Buffers b = std::move(buffers);
    if (!b.HasMemory()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ < kMaxSpares) std::swap(b, spares_[count_++]);  // (the slot held nothing)
  }

  // A spare pair, the one returned last, or a pair without memory.
  Buffers Take() {
    Buffers b;
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ != 0) std::swap(b, spares_[--count_]);
    return b;
  }

  // Frees every spare (after the lock).
  void Release() {
    std::array<Buffers, kMaxSpares> drop;
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < count_; ++i) std::swap(drop[i], spares_[i]);
    count_ = 0;
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
  }

 private:
  mutable std::mutex mutex_;
  std::array<Buffers, kMaxSpares> spares_;  // [0, count_) hold buffers; the rest hold nothing
  size_t count_ = 0;
};

class FrameBuilder {
 public:
  void Open() { open_ = true; }
  void Close() { open_ = false; }
  bool InMainScene() const { return open_; }
  uint32_t NextSeq() const { return captured_; }

  // `terrain`: the terrain inputs of the draw (capture::TerrainInputsOf), null
  // for a draw that has none. A drawable record's are copied to the frame's
  // side array and the record gets their index; whatever index `r` came with
  // is not kept. An index is 16 bits and kNoSideIndex means none, so a frame
  // holds at most kNoSideIndex terrain records: one more is skipped as
  // bad-memory (the capture's reason for a count past a cap) and counted like
  // any skipped draw, so that no index wraps onto another record's inputs.
  // `light`: the draw's light inputs (capture::MakeLightInputs), null for a
  // draw without them, and then `light_reason` says why. A drawable record's
  // are copied to the frame's light array and the record gets their index. A
  // record without inputs (none given, or inputs without a family) is counted
  // by its reason, light-unknown when none was given, and by its pixel shader.
  // The light array holds kNoSideIndex entries too; a lit record past that is
  // kept without inputs and counted light-unknown (the light never removes a
  // draw; a terrain record cannot be drawn without its patch, this can).
  void Add(const capture::DrawRecord& r, const capture::TerrainPatch* terrain = nullptr,
           const capture::LightInputs* light = nullptr,
           capture::LightReason light_reason = capture::LightReason::kLightUnknown) {
    if (!open_) return;
    ++captured_;
    capture::SkipReason skip = r.skip;
    if (skip == capture::SkipReason::kNone && terrain && terrain_.size() >= capture::kNoSideIndex) {
      skip = capture::SkipReason::kBadMemory;
    }
    if (skip == capture::SkipReason::kNone) {
      // The patch first: if the record then cannot be appended, the frame holds
      // a patch no record names. The other order would leave a record naming an
      // entry that the next terrain record's patch then fills.
      // The same for the light inputs.
      uint16_t index = capture::kNoSideIndex;
      if (terrain) {
        index = uint16_t(terrain_.size());
        terrain_.push_back(*terrain);
      }
      const bool lit = light && light->family != capture::LightFamily::kNone &&
                       size_t(light->family) < size_t(capture::LightFamily::kCount) &&
                       lights_.size() < capture::kNoSideIndex;
      uint16_t light_index = capture::kNoSideIndex;
      if (lit) {
        light_index = uint16_t(lights_.size());
        lights_.push_back(*light);
      }
      draws_.push_back(r);
      draws_.back().terrain_index = index;
      draws_.back().light_index = light_index;
      if (lit) {
        ++light_family_[size_t(light->family)];
        light_multi_ += light->multi ? 1 : 0;
      } else {
        const bool given = !light && light_reason != capture::LightReason::kNone &&
                           size_t(light_reason) < size_t(capture::LightReason::kCount);
        ++light_reason_[size_t(given ? light_reason : capture::LightReason::kLightUnknown)];
        light_unknown_ps_.Add(r.material.ps_hash, 0);
      }
      const capture::MaterialStatus m = r.material.status;
      if (size_t(m) < size_t(capture::MaterialStatus::kCount)) ++material_[size_t(m)];
      if (m == capture::MaterialStatus::kPsUnknown || m == capture::MaterialStatus::kNoAlbedo ||
          m == capture::MaterialStatus::kUvUnsupported) {
        untextured_ps_.Add(r.material.ps_hash, 0);
      }
    } else {
      ++skipped_[size_t(skip)];
      if (skip != capture::SkipReason::kUnsupportedPrim) {
        skipped_by_vs_.Add(r.vs_hash, uint8_t(skip));
      }
    }
  }

  // A main-scene draw counted without a record (no consumer this frame).
  void CountUnrecorded() {
    if (open_) ++captured_;
  }

  // The frame's light block: the first valid one offered inside the scene is
  // kept (FrameScene::light). CountLightTry: a read that gave none (the
  // capture makes one per light set: light_block.h, LightBlockTries).
  void SetLight(const capture::LightBlock& block) {
    if (open_ && block.valid && !light_.valid) light_ = block;
  }
  void CountLightTry() {
    if (open_) ++light_tries_;
  }
  bool HasLight() const { return light_.valid; }

  // --- The two shadow passes (FrameScene::shadow_draws, ::shadow) ---
  // Whether shadow records are taken from now on (the capture sets it at a
  // frame boundary). Off: every call below does nothing, and the frame's scene
  // says kOff.
  void ShadowsOn(bool on) { shadows_on_ = on; }

  // One draw of shadow pass `cascade` (0 near, 1 far), outside the main scene.
  // `caster`: its twin entry. `skip`: 0, or the reason the capture found for
  // not recording it (a ShadowSkip's or a SkipReason's number), which comes
  // before the record's own reason (r.skip); a number that is no reason counts
  // as bad-memory. A drawable record is appended with its caster (the record's
  // seq is its place in the list) and counts for the pass's bias; `pass_matrix`
  // (optional) is the light matrix the draw implies (shadow_block.h,
  // PassMatrix), and the first one given for a pass is the pass's in the
  // frame's proof. A list holds kMaxShadowRecords: a record past that is
  // counted over-limit, like any skipped draw by reason and vertex shader, and
  // nothing of a skipped draw is kept. Returns whether the record was appended.
  bool AddShadow(size_t cascade, const capture::DrawRecord& r, const capture::ShadowCaster& caster, uint8_t skip = 0,
                 const float (*pass_matrix)[4] = nullptr) {
    if (!shadows_on_ || cascade >= 2) return false;
    uint8_t code = skip != 0 ? skip : uint8_t(r.skip);
    if (code >= capture::kShadowSkipCount) code = uint8_t(capture::SkipReason::kBadMemory);
    std::vector<capture::DrawRecord>& draws = shadow_draws_[cascade];
    if (code == 0 && draws.size() >= kMaxShadowRecords) code = uint8_t(capture::ShadowSkip::kOverLimit);
    if (code != 0) {
      ++shadow_skipped_[cascade][code];
      shadow_skipped_by_vs_.Add(r.vs_hash, code);
      return false;
    }
    // Room first, for both lists, so that a record never lacks its caster: grown
    // by half as the frame's other buffers are, and never past the limit.
    std::vector<capture::ShadowCaster>& casters = shadow_casters_[cascade];
    if (draws.size() == draws.capacity()) draws.reserve(ShadowCapacity(draws.size() + 1));
    if (casters.size() == casters.capacity()) casters.reserve(ShadowCapacity(casters.size() + 1));
    casters.push_back(caster);
    draws.push_back(r);
    draws.back().seq = uint32_t(draws.size() - 1);
    draws.back().terrain_index = capture::kNoSideIndex;
    draws.back().light_index = capture::kNoSideIndex;
    capture::ShadowPassProof& proof = shadow_proof_[cascade];
    proof.bias.Add(caster.biased, caster.bias_scale, caster.bias_offset);
    if (pass_matrix && !proof.have_matrix) {
      proof.have_matrix = true;
      std::memcpy(proof.matrix, pass_matrix, sizeof(proof.matrix));
    }
    return true;
  }
  // A draw on the shadow map that is in neither pass (ShadowPass::kExtra), and
  // a pass opened (a clear of the map).
  void CountShadowExtra() {
    if (shadows_on_) ++shadow_extra_;
  }
  void CountShadowPass() {
    if (shadows_on_) ++shadow_passes_;
  }
  // The registers' part of the frame's shadow block (capture::MakeShadowBlock):
  // the first valid one offered in the frame is kept; Finish makes the frame's
  // block of it and the passes (capture::FinishShadowBlock).
  void SetShadowRegisters(const capture::ShadowBlock& registers) {
    if (shadows_on_ && registers.valid && !shadow_registers_.valid) shadow_registers_ = registers;
  }
  bool HasShadowRegisters() const { return shadow_registers_.valid; }

  // --- The fog and sky blocks (FrameScene::fog, ::sky) ---
  // Whether the fog and the sky are read from now on (the capture sets it at a
  // frame boundary). Off: every call below does nothing, and the frame's scene
  // says kOff.
  void FogOn(bool on) { fog_on_ = on; }
  // The frame's fog block with where its tables were read: the first valid one
  // offered inside the scene is kept. CountFogTry: a read that gave none, with
  // why (capture::MakeFogBlock's reason, or kUnread); the last one's reason is
  // the frame's when it ends without a block. A read after the block is not counted.
  void SetFog(const capture::FogBlock& block, const capture::FogSource& source) {
    if (open_ && fog_on_ && block.valid && !fog_.valid) {
      fog_ = block;
      fog_source_ = source;
    }
  }
  void CountFogTry(capture::FogBlockReason why) {
    if (open_ && fog_on_ && !fog_.valid) {
      ++fog_tries_;
      fog_reason_ = why;
    }
  }
  bool HasFog() const { return fog_.valid; }
  // The same for the sky block.
  void SetSky(const capture::SkyBlock& block) {
    if (open_ && fog_on_ && block.valid && !sky_.valid) sky_ = block;
  }
  void CountSkyTry(capture::SkyBlockReason why) {
    if (open_ && fog_on_ && !sky_.valid) {
      ++sky_tries_;
      sky_reason_ = why;
    }
  }
  bool HasSky() const { return sky_.valid; }

  // --- The post block (FrameScene::post) ---
  // Whether the post block is read from now on (the capture sets it at a frame
  // boundary). Off: every call below does nothing, the frame's scene says kOff,
  // and Finish drops the block kept from earlier frames.
  void PostOn(bool on) { post_on_ = on; }
  // The block read at this frame's composite with where its table was read: the
  // first valid one offered in the frame is kept. The game issues its post
  // rectangles after the main scene, so these two do not ask for an open scene
  // (as the shadow passes' calls do not): the frame is whatever lies between
  // two calls of Finish. CountPostTry: a read that gave none, with why
  // (capture::MakePostBlock's reason, or kUnread); the last one's reason is the
  // frame's when it ends without a block of its own and none kept serves. A
  // read after the block is not counted.
  void SetPost(const capture::PostBlock& block, const capture::PostSource& source) {
    if (post_on_ && block.valid && !post_.valid) {
      post_ = block;
      post_source_ = source;
    }
  }
  void CountPostTry(capture::PostBlockReason why) {
    if (post_on_ && !post_.valid) {
      ++post_tries_;
      post_reason_ = why;
    }
  }
  bool HasPost() const { return post_.valid; }

  // --- The sun layer's depth place (FrameScene::layer_depth_place) ---
  // One tree occluder call of the frame's sun layer, after the main scene: `occluder` the record
  // AssembleRecord makes of the call, null when the call could not be read. Its twin among the frame's
  // records so far (capture::FindOccluderTwin) gives the place; a call without a single twin, or past
  // capture::kMaxLayerOccluders, is counted failed. Nothing with the quads off.
  void LayerOccluderSeen(const capture::DrawRecord* occluder) {
    if (!quads_on_) return;
    ++layer_occluders_;
    uint32_t index = capture::kNoDepthPlace;
    if (layer_occluders_ > capture::kMaxLayerOccluders || !occluder ||
        capture::FindOccluderTwin(draws_.data(), draws_.size(), *occluder, &index) != capture::OccluderTwin::kFound) {
      ++layer_occluders_failed_;
      return;
    }
    if (index < layer_depth_place_) layer_depth_place_ = index;
  }

  // --- The quad records (FrameScene::quads) ---
  // Whether the quads are read from now on (the capture sets it at a frame boundary). Off: every
  // call below does nothing, and the frame's scene says kOff.
  void QuadsOn(bool on) { quads_on_ = on; }
  // A furniture call of the frame: a call of the indexed builder that ran a table shader at its
  // kind's target. Its storage is filled by the game after the builder returns, so its record
  // comes later (AddQuad), when the drawing thread enters its next hooked draw; a call that is
  // refused at once is handed over at once. Counted here so that a frame which ends before a
  // record arrived knows it lacks one. The layer's quads come after the main scene: like the post
  // block's calls, these two do not ask for an open scene.
  void QuadSeen(capture::PostQuadKind kind) {
    if (!quads_on_) return;
    ++quad_seen_;
    if (kind < capture::PostQuadKind::kCount) ++quad_kinds_[size_t(kind)];
  }
  // The record of a call counted by QuadSeen, or its refusal: `why` kNone with a valid record of a
  // kind takes it, at the next place among the frame's (its `sequence`); anything else is a
  // refusal, counted by its reason ("unread" for a record that is none without a reason, and for
  // a reason that is none). A valid record past kMaxQuadRecords is counted over the bound and not
  // kept. Nothing invalid is ever stored.
  void AddQuad(const capture::QuadRecord& record, capture::QuadRecordReason why) {
    if (!quads_on_) return;
    const bool good = why == capture::QuadRecordReason::kNone && capture::QuadRecordValid(record) &&
                      record.kind < capture::PostQuadKind::kCount;
    if (!good) {
      const bool named = why != capture::QuadRecordReason::kNone && why < capture::QuadRecordReason::kCount;
      ++quad_refused_[size_t(named ? why : capture::QuadRecordReason::kUnread)];
      return;
    }
    if (quad_taken_ >= capture::kMaxQuadRecords) {
      ++quad_over_;
      return;
    }
    quads_[quad_taken_] = record;
    quads_[quad_taken_].sequence = uint16_t(quad_taken_);
    ++quad_taken_;
  }

  // Frees the spare buffers (any thread; scenes that die later bring theirs).
  void ReleaseSpares() { pool_->Release(); }
  size_t spare_count() const { return pool_->size(); }

  // The caller may fill the scene's remaining fields before publishing it.
  // The records and their side arrays move into the scene together, and the
  // builder goes on with other buffers: a spare set (SceneBufferPool), cleared
  // (the capacity stays), or new ones. Nothing of a scene is written by the
  // builder while a reference to the scene exists: its buffers come back only
  // from its deleter, which runs when the last reference has dropped, on that
  // thread. A frame without a record keeps the builder's buffers and gives the
  // scene none, so nothing circulates while records are off.
  // Capacity: a buffer grows only when a frame needs more than it holds: to
  // the size of the frame before and half as much again (ReserveWithSlack), or
  // by the vector's own growth within a frame (1.5 here too). So it stays
  // within 1.5 times the largest frame recorded since it was allocated, and a
  // frame that creeps up by a record at a time finds room instead of a new
  // allocation under the capture's scene mutex each time; it does not shrink.
  // At most kMaxSpares spare sets exist besides the builder's and those of
  // the living scenes, and ReleaseSpares frees the spares.
  std::shared_ptr<FrameScene> Finish(uint64_t frame) {
    // (weak: a scene may outlive the builder, and then frees its buffers itself)
    std::shared_ptr<FrameScene> s(new FrameScene, [pool = std::weak_ptr<SceneBufferPool>(pool_)](FrameScene* scene) {
      if (const std::shared_ptr<SceneBufferPool> p = pool.lock()) {
        SceneBufferPool::Buffers back;
        back.draws = std::move(scene->draws);
        back.terrain = std::move(scene->terrain);
        back.lights = std::move(scene->lights);
        for (size_t c = 0; c < 2; ++c) {
          back.shadow_draws[c] = std::move(scene->shadow_draws[c]);
          back.shadow_casters[c] = std::move(scene->shadow_casters[c]);
        }
        p->Return(std::move(back));
      }
      delete scene;
    });
    s->frame = frame;
    s->captured = captured_;
    std::copy(std::begin(skipped_), std::end(skipped_), std::begin(s->skipped));
    s->skipped_by_vs = skipped_by_vs_;
    skipped_by_vs_.Clear();
    std::copy(std::begin(material_), std::end(material_), std::begin(s->material));
    std::fill(std::begin(material_), std::end(material_), 0u);
    capture::ShaderTally<kUntexturedTallySize>::Entry top[kUntexturedTop];
    const size_t n = untextured_ps_.Top(0, top, kUntexturedTop);
    for (size_t i = 0; i < n; ++i) s->untextured_ps.emplace_back(top[i].hash, top[i].count);
    untextured_ps_.Clear();
    s->light = light_;
    light_ = capture::LightBlock{};
    s->light_tries = light_tries_;
    light_tries_ = 0;
    std::copy(std::begin(light_family_), std::end(light_family_), std::begin(s->light_family));
    std::fill(std::begin(light_family_), std::end(light_family_), 0u);
    s->light_multi = light_multi_;
    light_multi_ = 0;
    std::copy(std::begin(light_reason_), std::end(light_reason_), std::begin(s->light_reason));
    std::fill(std::begin(light_reason_), std::end(light_reason_), 0u);
    const size_t unknown = light_unknown_ps_.Top(0, top, kUntexturedTop);
    for (size_t i = 0; i < unknown; ++i) s->light_unknown_ps.emplace_back(top[i].hash, top[i].count);
    light_unknown_ps_.Clear();
    // The shadow passes: the frame's block from the registers and the passes'
    // proof, then every count; the next frame starts with none of it.
    s->shadow_records = shadows_on_;
    if (shadows_on_) {
      s->shadow = capture::FinishShadowBlock(shadow_registers_, shadow_proof_, capture::kShadowProofTolerance,
                                             &s->shadow_reason);
      for (size_t c = 0; c < 2; ++c) {
        std::copy(std::begin(shadow_skipped_[c]), std::end(shadow_skipped_[c]), std::begin(s->shadow_skipped[c]));
      }
      s->shadow_skipped_by_vs = shadow_skipped_by_vs_;
      s->shadow_extra = shadow_extra_;
      s->shadow_passes = shadow_passes_;
    }
    shadow_registers_ = capture::ShadowBlock{};
    for (size_t c = 0; c < 2; ++c) {
      shadow_proof_[c] = capture::ShadowPassProof{};
      std::fill(std::begin(shadow_skipped_[c]), std::end(shadow_skipped_[c]), 0u);
    }
    shadow_skipped_by_vs_.Clear();
    shadow_extra_ = 0;
    shadow_passes_ = 0;
    // The fog and sky blocks: the frame's, or the reason it has none; the next
    // frame starts with neither.
    s->fog_records = fog_on_;
    if (fog_on_) {
      s->fog_tries = fog_tries_;
      s->sky_tries = sky_tries_;
      if (fog_.valid) {
        s->fog = fog_;
        s->fog_source = fog_source_;
        s->fog_reason = capture::FogBlockReason::kNone;
      } else {
        s->fog_reason = fog_tries_ ? fog_reason_ : capture::FogBlockReason::kNoDraw;
      }
      if (sky_.valid) {
        s->sky = sky_;
        s->sky_reason = capture::SkyBlockReason::kNone;
      } else {
        s->sky_reason = sky_tries_ ? sky_reason_ : capture::SkyBlockReason::kNoDraw;
      }
    }
    if (fog_.valid) fog_ = capture::FogBlock{};  // (4 KB: cleared only when a frame set it; never set with the fog off)
    fog_source_ = capture::FogSource{};
    fog_reason_ = capture::FogBlockReason::kNoDraw;
    fog_tries_ = 0;
    sky_ = capture::SkyBlock{};
    sky_reason_ = capture::SkyBlockReason::kNoDraw;
    sky_tries_ = 0;
    // The post block: the frame's own if it pairs with the frame's light block
    // (s->light, set above), else the last valid one of an earlier frame within
    // its age bound, else none with the reason (capture::ChoosePostBlock).
    s->post_records = post_on_;
    if (post_on_) {
      s->post_tries = post_tries_;
      const capture::PostChoice choice = capture::ChoosePostBlock(
          post_, post_tries_, post_reason_, post_kept_, post_kept_age_, s->light.valid ? s->light.out_scale : 0.0f);
      if (choice.pick == capture::PostPick::kOwn) {
        s->post = post_;
        s->post_source = post_source_;
      } else if (choice.pick == capture::PostPick::kKept) {
        s->post = post_kept_;
        s->post_source = post_kept_source_;
      }
      s->post_age = choice.age;
      s->post_reason = choice.reason;
    }
    // What is kept for the frames after this one: the block this frame read
    // (valid by its own rules, whether or not it paired here: the last valid
    // one), one frame old at the next Finish; else the kept one, a frame older,
    // until it has been found too old once (the frame that says kStale), after
    // which nothing is kept. With the post block off nothing is kept at all: no
    // table outlives a stretch in which nothing was read.
    if (post_on_ && post_.valid) {
      post_kept_ = post_;
      post_kept_source_ = post_source_;
      post_kept_age_ = 1;
    } else if (post_kept_.valid) {
      if (!post_on_ || post_kept_age_ > capture::kPostBlockMaxAge) {
        post_kept_ = capture::PostBlock{};
        post_kept_source_ = capture::PostSource{};
        post_kept_age_ = 0;
      } else {
        ++post_kept_age_;
      }
    }
    if (post_.valid) post_ = capture::PostBlock{};  // (2 KB: cleared only when a frame set it)
    post_source_ = capture::PostSource{};
    post_reason_ = capture::PostBlockReason::kNoDraw;
    post_tries_ = 0;
    // The quad records: the frame's set if it is whole (capture::ChooseQuadSet), else none of them
    // with the reason; the counts either way. The next frame starts with none of it.
    s->quad_records = quads_on_;
    if (quads_on_) {
      // (a record past the bound was a call that gave one: the choice is told of all of them)
      s->quad_reason = capture::ChooseQuadSet(true, quad_seen_, quad_taken_ + quad_over_, quad_refused_, quad_over_);
      s->quad_seen = quad_seen_;
      s->quad_taken = quad_taken_;
      s->quad_over = quad_over_;
      std::copy(std::begin(quad_kinds_), std::end(quad_kinds_), std::begin(s->quad_kinds));
      std::copy(std::begin(quad_refused_), std::end(quad_refused_), std::begin(s->quad_refused));
      if (s->quad_reason == capture::QuadSetReason::kNone) {
        // (one allocation of at most 24 records, 10,752 bytes, in a frame that has a set)
        s->quad_count = quad_taken_;
        s->quads.assign(quads_, quads_ + quad_taken_);
      }
    }
    // The layer's depth place, as the frame's occluder calls gave it; the next frame starts with none.
    if (quads_on_) {
      s->layer_occluders = layer_occluders_;
      s->layer_occluders_failed = layer_occluders_failed_;
      s->layer_depth_place = layer_depth_place_;
    }
    layer_occluders_ = layer_occluders_failed_ = 0;
    layer_depth_place_ = capture::kNoDepthPlace;
    quad_seen_ = quad_taken_ = quad_over_ = 0;
    std::fill(std::begin(quad_kinds_), std::end(quad_kinds_), 0u);
    std::fill(std::begin(quad_refused_), std::end(quad_refused_), 0u);
    if (!draws_.empty() || !terrain_.empty() || !lights_.empty() || !shadow_draws_[0].empty() || !shadow_draws_[1].empty()) {
      const size_t shadows[2] = {shadow_draws_[0].size(), shadow_draws_[1].size()};
      const size_t records = draws_.size(), patches = terrain_.size(), lit = lights_.size();
      s->draws = std::move(draws_);
      s->terrain = std::move(terrain_);
      s->lights = std::move(lights_);
      SceneBufferPool::Buffers next = pool_->Take();
      draws_ = std::move(next.draws);
      terrain_ = std::move(next.terrain);
      lights_ = std::move(next.lights);
      draws_.clear();
      terrain_.clear();
      lights_.clear();
      ReserveWithSlack(draws_, records);
      ReserveWithSlack(terrain_, patches);
      ReserveWithSlack(lights_, lit);
      for (size_t c = 0; c < 2; ++c) {
        s->shadow_draws[c] = std::move(shadow_draws_[c]);
        s->shadow_casters[c] = std::move(shadow_casters_[c]);
        shadow_draws_[c] = std::move(next.shadow_draws[c]);
        shadow_casters_[c] = std::move(next.shadow_casters[c]);
        shadow_draws_[c].clear();
        shadow_casters_[c].clear();
        if (shadow_draws_[c].capacity() < shadows[c]) shadow_draws_[c].reserve(ShadowCapacity(shadows[c]));
        if (shadow_casters_[c].capacity() < shadows[c]) shadow_casters_[c].reserve(ShadowCapacity(shadows[c]));
      }
    }
    captured_ = 0;
    std::fill(std::begin(skipped_), std::end(skipped_), 0u);
    open_ = false;
    return s;
  }

 private:
  // Room for `n` entries, the size of the frame before: nothing happens when
  // the buffer holds them; a buffer that has to be replaced gets half as much
  // again, so that the frames after it, a few records larger, fit as well.
  template <typename T>
  static void ReserveWithSlack(std::vector<T>& v, size_t n) {
    if (v.capacity() < n) v.reserve(n + n / 2);
  }
  // The capacity a shadow list is given when it needs room for `n` entries:
  // half as much again, as above, but never more than a list may hold.
  static size_t ShadowCapacity(size_t n) { return std::min(n + n / 2 + 8, kMaxShadowRecords); }

  bool open_ = false;
  uint32_t captured_ = 0;
  uint32_t skipped_[size_t(capture::SkipReason::kCount)] = {};
  capture::ShaderTally<kSkippedTallySize> skipped_by_vs_;
  uint32_t material_[size_t(capture::MaterialStatus::kCount)] = {};
  capture::ShaderTally<kUntexturedTallySize> untextured_ps_;
  std::vector<capture::DrawRecord> draws_;
  std::vector<capture::TerrainPatch> terrain_;  // side array of draws_ (FrameScene::terrain)
  std::vector<capture::LightInputs> lights_;    // side array of draws_ (FrameScene::lights)
  capture::LightBlock light_{};                 // the frame's light block so far (FrameScene::light)
  uint32_t light_tries_ = 0;
  uint32_t light_family_[size_t(capture::LightFamily::kCount)] = {};
  uint32_t light_multi_ = 0;
  uint32_t light_reason_[size_t(capture::LightReason::kCount)] = {};
  capture::ShaderTally<kUntexturedTallySize> light_unknown_ps_;
  // The shadow passes of the frame so far (FrameScene::shadow_draws, ...).
  bool shadows_on_ = false;
  std::vector<capture::DrawRecord> shadow_draws_[2];
  std::vector<capture::ShadowCaster> shadow_casters_[2];  // twins of shadow_draws_
  capture::ShadowBlock shadow_registers_{};               // the registers' part of the block
  capture::ShadowPassProof shadow_proof_[2];
  uint32_t shadow_skipped_[2][capture::kShadowSkipCount] = {};
  capture::ShaderTally<kSkippedTallySize> shadow_skipped_by_vs_;
  uint32_t shadow_extra_ = 0;
  uint32_t shadow_passes_ = 0;
  // The fog and sky blocks of the frame so far (FrameScene::fog, ::sky).
  bool fog_on_ = false;
  capture::FogBlock fog_{};
  capture::FogSource fog_source_{};
  capture::FogBlockReason fog_reason_ = capture::FogBlockReason::kNoDraw;
  uint32_t fog_tries_ = 0;
  capture::SkyBlock sky_{};
  capture::SkyBlockReason sky_reason_ = capture::SkyBlockReason::kNoDraw;
  uint32_t sky_tries_ = 0;
  // The post block of the frame so far (FrameScene::post), and the last valid
  // one of an earlier frame with its age in frames (0: none is kept).
  bool post_on_ = false;
  capture::PostBlock post_{};
  capture::PostSource post_source_{};
  capture::PostBlockReason post_reason_ = capture::PostBlockReason::kNoDraw;
  uint32_t post_tries_ = 0;
  capture::PostBlock post_kept_{};
  capture::PostSource post_kept_source_{};
  uint32_t post_kept_age_ = 0;
  // The quad records of the frame so far (FrameScene::quads): [0, quad_taken_) are held.
  bool quads_on_ = false;
  capture::QuadRecord quads_[capture::kMaxQuadRecords] = {};
  uint32_t quad_seen_ = 0;
  uint32_t quad_taken_ = 0;
  uint32_t quad_over_ = 0;
  uint32_t quad_kinds_[size_t(capture::PostQuadKind::kCount)] = {};
  uint32_t quad_refused_[size_t(capture::QuadRecordReason::kCount)] = {};
  // The sun layer's tree occluders of the frame so far (FrameScene::layer_depth_place).
  uint32_t layer_occluders_ = 0;
  uint32_t layer_occluders_failed_ = 0;
  uint32_t layer_depth_place_ = capture::kNoDepthPlace;
  // Shared with the deleters of this builder's scenes. Never reassigned, and
  // locked inside: ReleaseSpares and spare_count need no lock of the caller's.
  std::shared_ptr<SceneBufferPool> pool_ = std::make_shared<SceneBufferPool>();
};

class ScenePublisher {
 public:
  void Publish(std::shared_ptr<const FrameScene> s) {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_ = std::move(s);
  }
  std::shared_ptr<const FrameScene> Latest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
  }

 private:
  mutable std::mutex mutex_;
  std::shared_ptr<const FrameScene> latest_;
};

}  // namespace fable2::native::render
