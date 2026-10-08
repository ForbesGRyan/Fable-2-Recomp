#pragma once

// Per-frame scene snapshot built on the game thread and consumed on the GPU
// thread (pure: no SDK/GPU deps). Pattern: skate3recomp's FrameScene.

#include <algorithm>
#include <iterator>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "../capture/draw_record.h"
#include "../capture/shader_tally.h"

namespace fable2::native::render {

inline constexpr size_t kSkippedTallySize = 32;
inline constexpr size_t kUntexturedTallySize = 64;
inline constexpr size_t kUntexturedTop = 8;

struct FrameScene {
  uint64_t frame = 0;
  std::vector<capture::DrawRecord> draws;
  // Side array of `draws`: the terrain inputs of its terrain records, each at
  // its record's terrain_index (TerrainOf). Built with the records, handed
  // over with them (FrameBuilder::Finish) and, like them, never written again.
  std::vector<capture::TerrainPatch> terrain;
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
  void Add(const capture::DrawRecord& r, const capture::TerrainPatch* terrain = nullptr) {
    if (!open_) return;
    ++captured_;
    capture::SkipReason skip = r.skip;
    if (skip == capture::SkipReason::kNone && terrain && terrain_.size() >= capture::kNoSideIndex) {
      skip = capture::SkipReason::kBadMemory;
    }
    if (skip == capture::SkipReason::kNone) {
      draws_.push_back(r);
      draws_.back().terrain_index = terrain ? uint16_t(terrain_.size()) : capture::kNoSideIndex;
      if (terrain) terrain_.push_back(*terrain);
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

  // The caller may fill the scene's remaining fields before publishing it.
  // The records and their side array move into the scene together and the
  // builder starts the next frame with new, empty ones: nothing of a finished
  // scene is written by the builder again.
  std::shared_ptr<FrameScene> Finish(uint64_t frame) {
    auto s = std::make_shared<FrameScene>();
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
    const size_t reserve = draws_.size();
    s->draws = std::move(draws_);
    draws_ = {};
    draws_.reserve(reserve);
    const size_t terrain_reserve = terrain_.size();
    s->terrain = std::move(terrain_);
    terrain_ = {};
    terrain_.reserve(terrain_reserve);
    captured_ = 0;
    std::fill(std::begin(skipped_), std::end(skipped_), 0u);
    open_ = false;
    return s;
  }

 private:
  bool open_ = false;
  uint32_t captured_ = 0;
  uint32_t skipped_[size_t(capture::SkipReason::kCount)] = {};
  capture::ShaderTally<kSkippedTallySize> skipped_by_vs_;
  uint32_t material_[size_t(capture::MaterialStatus::kCount)] = {};
  capture::ShaderTally<kUntexturedTallySize> untextured_ps_;
  std::vector<capture::DrawRecord> draws_;
  std::vector<capture::TerrainPatch> terrain_;  // side array of draws_ (FrameScene::terrain)
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
