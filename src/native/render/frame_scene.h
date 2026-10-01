#pragma once

// Per-frame scene snapshot built on the game thread and consumed on the GPU
// thread (pure: no SDK/GPU deps). Pattern: skate3recomp's FrameScene.

#include <algorithm>
#include <iterator>
#include <memory>
#include <mutex>
#include <vector>

#include "../capture/draw_record.h"

namespace fable2::native::render {

struct FrameScene {
  uint64_t frame = 0;
  std::vector<capture::DrawRecord> draws;
  uint32_t captured = 0;
  uint32_t skipped[size_t(capture::SkipReason::kCount)] = {};
};

class FrameBuilder {
 public:
  void Open() { open_ = true; }
  void Close() { open_ = false; }
  bool InMainScene() const { return open_; }
  uint32_t NextSeq() const { return captured_; }

  void Add(const capture::DrawRecord& r) {
    if (!open_) return;
    ++captured_;
    if (r.skip == capture::SkipReason::kNone) {
      draws_.push_back(r);
    } else {
      ++skipped_[size_t(r.skip)];
    }
  }

  std::shared_ptr<const FrameScene> Finish(uint64_t frame) {
    auto s = std::make_shared<FrameScene>();
    s->frame = frame;
    s->captured = captured_;
    std::copy(std::begin(skipped_), std::end(skipped_), std::begin(s->skipped));
    const size_t reserve = draws_.size();
    s->draws = std::move(draws_);
    draws_ = {};
    draws_.reserve(reserve);
    captured_ = 0;
    std::fill(std::begin(skipped_), std::end(skipped_), 0u);
    open_ = false;
    return s;
  }

 private:
  bool open_ = false;
  uint32_t captured_ = 0;
  uint32_t skipped_[size_t(capture::SkipReason::kCount)] = {};
  std::vector<capture::DrawRecord> draws_;
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
