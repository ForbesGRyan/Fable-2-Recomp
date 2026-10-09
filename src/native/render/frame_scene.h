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
#include "../capture/light_block.h"
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
