#pragma once

// Per-frame timing window (pure, std only): the last sample plus the mean,
// median, percentiles and maximum of up to `capacity` samples since the last Reset.
// Used for the periodic guest work and capture time log lines. Single
// threaded: the owner adds once per frame and queries when it logs.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace fable2::diagnostics {

class WindowStats {
 public:
  explicit WindowStats(size_t capacity = 300) : capacity_(capacity) { samples_.reserve(capacity); }

  // Samples past the capacity are dropped (the last value still updates).
  void Add(double v) {
    last_ = v;
    if (samples_.size() < capacity_) samples_.push_back(v);
  }
  void Reset() {
    samples_.clear();
    last_ = 0;
  }

  size_t size() const { return samples_.size(); }
  bool full() const { return samples_.size() >= capacity_; }
  double last() const { return last_; }

  // Nearest-rank percentile, p in [0, 1] (0 with no samples). The median of
  // an even count is the upper middle sample.
  double Percentile(double p) const {
    if (samples_.empty()) return 0;
    scratch_ = samples_;
    const double clamped = std::min(1.0, std::max(0.0, p));
    const size_t k = std::min(scratch_.size() - 1, size_t(std::floor(clamped * double(scratch_.size()))));
    std::nth_element(scratch_.begin(), scratch_.begin() + k, scratch_.end());
    return scratch_[k];
  }
  double Median() const { return Percentile(0.5); }
  double Mean() const {
    double sum = 0;
    for (double v : samples_) sum += v;
    return samples_.empty() ? 0 : sum / double(samples_.size());
  }
  double Max() const {
    return samples_.empty() ? 0 : *std::max_element(samples_.begin(), samples_.end());
  }

 private:
  size_t capacity_;
  std::vector<double> samples_;
  mutable std::vector<double> scratch_;
  double last_ = 0;
};

}  // namespace fable2::diagnostics
