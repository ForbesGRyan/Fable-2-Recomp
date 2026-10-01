#pragma once

// Bookkeeping for decoded guest geometry on the GPU (pure: no SDK/GPU deps):
// key + content hash -> id, byte budget, LRU eviction that never evicts an
// entry used in the current frame.

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace fable2::native::render {

struct GeoKey {
  uint32_t addr = 0, size = 0, stride = 0, extra = 0;
  uint8_t kind = 0;  // 0 = positions, 1 = indices
  bool operator==(const GeoKey&) const = default;
};

struct GeoKeyHash {
  size_t operator()(const GeoKey& k) const {
    uint64_t h = 1469598103934665603ull;
    for (uint64_t v : {uint64_t(k.addr), uint64_t(k.size), uint64_t(k.stride), uint64_t(k.extra),
                       uint64_t(k.kind)}) {
      h = (h ^ v) * 1099511628211ull;
    }
    return size_t(h);
  }
};

struct LookupResult {
  bool hit;
  uint32_t id;
};

class GeometryCacheIndex {
 public:
  explicit GeometryCacheIndex(uint64_t budget_bytes) : budget_(budget_bytes) {}

  void BeginFrame(uint64_t frame) { frame_ = frame; }

  LookupResult Lookup(const GeoKey& key, uint64_t content_hash) {
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.hash != content_hash) return {false, 0};
    it->second.last_used = frame_;
    return {true, it->second.id};
  }

  uint32_t Insert(const GeoKey& key, uint64_t content_hash, uint64_t bytes,
                  std::vector<uint32_t>* evicted) {
    auto it = entries_.find(key);
    if (it != entries_.end()) {
      resident_ -= it->second.bytes;
      if (evicted) evicted->push_back(it->second.id);
      entries_.erase(it);
    }
    while (resident_ + bytes > budget_) {
      auto victim = entries_.end();
      for (auto e = entries_.begin(); e != entries_.end(); ++e) {
        if (e->second.last_used == frame_) continue;
        if (victim == entries_.end() || e->second.last_used < victim->second.last_used ||
            (e->second.last_used == victim->second.last_used && e->second.id < victim->second.id)) {
          victim = e;
        }
      }
      if (victim == entries_.end()) break;  // all in use this frame: go over budget
      resident_ -= victim->second.bytes;
      if (evicted) evicted->push_back(victim->second.id);
      entries_.erase(victim);
    }
    const uint32_t id = next_id_++;
    entries_[key] = {content_hash, bytes, frame_, id};
    resident_ += bytes;
    return id;
  }

  uint64_t resident_bytes() const { return resident_; }
  uint64_t budget_bytes() const { return budget_; }
  void set_budget_bytes(uint64_t b) { budget_ = b; }
  size_t size() const { return entries_.size(); }

 private:
  struct Entry {
    uint64_t hash;
    uint64_t bytes;
    uint64_t last_used;
    uint32_t id;
  };
  std::unordered_map<GeoKey, Entry, GeoKeyHash> entries_;
  uint64_t budget_;
  uint64_t resident_ = 0;
  uint64_t frame_ = 0;
  uint32_t next_id_ = 1;
};

}  // namespace fable2::native::render
