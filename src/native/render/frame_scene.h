#pragma once

// Per-frame scene snapshot built on the game thread and consumed on the GPU
// thread (pure: no SDK/GPU deps). Pattern: skate3recomp's FrameScene.

#include <algorithm>
#include <array>
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
  // over with them (FrameBuilder::Finish) and, like them, not written again
  // while anything refers to the scene.
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

// The record and side-array buffers of scenes nothing refers to any more, kept
// for the builder to fill again: a new allocation of that size is new pages,
// and a frame's records then cost a page fault per page on the guest thread.
// The list has its own lock, so buffers come back from whichever thread drops
// a scene's last reference (usually the render thread) without the capture's
// scene mutex; nothing is freed or allocated while the list's lock is held.
// At most kMaxSpares pairs are kept. A buffer is in exactly one place: the
// builder, one scene, or this list.
class SceneBufferPool {
 public:
  static constexpr size_t kMaxSpares = 4;
  struct Buffers {
    std::vector<capture::DrawRecord> draws;
    std::vector<capture::TerrainPatch> terrain;
  };

  // The buffers of a scene whose last reference has dropped, with whatever
  // they hold (Take's caller clears them). Kept if the list has room, else
  // freed here, after the lock. Buffers without memory are not kept.
  void Return(Buffers&& buffers) {
    Buffers b = std::move(buffers);
    if (b.draws.capacity() == 0 && b.terrain.capacity() == 0) return;
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
  void Add(const capture::DrawRecord& r, const capture::TerrainPatch* terrain = nullptr) {
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
      uint16_t index = capture::kNoSideIndex;
      if (terrain) {
        index = uint16_t(terrain_.size());
        terrain_.push_back(*terrain);
      }
      draws_.push_back(r);
      draws_.back().terrain_index = index;
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

  // Frees the spare buffers (any thread; scenes that die later bring theirs).
  void ReleaseSpares() { pool_->Release(); }
  size_t spare_count() const { return pool_->size(); }

  // The caller may fill the scene's remaining fields before publishing it.
  // The records and their side array move into the scene together, and the
  // builder goes on with other buffers: a spare pair (SceneBufferPool), cleared
  // (the capacity stays), or new ones. Nothing of a scene is written by the
  // builder while a reference to the scene exists: its buffers come back only
  // from its deleter, which runs when the last reference has dropped, on that
  // thread. A frame without a record keeps the builder's buffers and gives the
  // scene none, so nothing circulates while records are off.
  // Capacity: a buffer grows only when a frame needs more than it holds, to
  // the size of the frame before (reserve) or by the vector's own growth within
  // a frame, so it stays within the largest frame recorded since it was
  // allocated times the vector's growth factor (1.5 here); it does not shrink.
  // At most kMaxSpares spare pairs exist besides the builder's and those of
  // the living scenes, and ReleaseSpares frees the spares.
  std::shared_ptr<FrameScene> Finish(uint64_t frame) {
    // (weak: a scene may outlive the builder, and then frees its buffers itself)
    std::shared_ptr<FrameScene> s(new FrameScene, [pool = std::weak_ptr<SceneBufferPool>(pool_)](FrameScene* scene) {
      if (const std::shared_ptr<SceneBufferPool> p = pool.lock()) {
        p->Return({std::move(scene->draws), std::move(scene->terrain)});
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
    if (!draws_.empty() || !terrain_.empty()) {
      const size_t records = draws_.size(), patches = terrain_.size();
      s->draws = std::move(draws_);
      s->terrain = std::move(terrain_);
      SceneBufferPool::Buffers next = pool_->Take();
      draws_ = std::move(next.draws);
      terrain_ = std::move(next.terrain);
      draws_.clear();
      terrain_.clear();
      draws_.reserve(records);  // (nothing happens when the spare is large enough)
      terrain_.reserve(patches);
    }
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
