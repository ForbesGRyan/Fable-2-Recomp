#pragma once

// Bookkeeping for decoded guest geometry on the GPU (pure: no SDK/GPU deps):
// key + content hash -> id, byte budget, LRU eviction that never evicts an
// entry used in the current frame. An insert that would pass the budget
// evicts in one pass: entries that neither this frame nor the previous one
// used go down to a low-water mark, so the inserts after it do not search the
// index again; entries of the previous frame go only as far as the insert
// needs.

#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
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

  void BeginFrame(uint64_t frame) {
    frame_ = frame;
    nothing_idle_ = false;
  }

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
    // (once a pass has left nothing idle, the frame's later inserts cannot
    // evict either: an entry only becomes idle again in a later frame)
    if (resident_ + bytes > budget_ && !nothing_idle_) Evict(bytes, evicted);
    const uint32_t id = next_id_++;
    entries_[key] = {content_hash, bytes, frame_, id};
    resident_ += bytes;
    return id;
  }

  uint64_t resident_bytes() const { return resident_; }
  uint64_t budget_bytes() const { return budget_; }
  // What an insert past the budget evicts down to, of the entries that
  // neither this frame nor the previous one used: 15/16 of the budget.
  uint64_t low_water_bytes() const { return budget_ - budget_ / 16; }
  void set_budget_bytes(uint64_t b) { budget_ = b; }
  size_t size() const { return entries_.size(); }

 private:
  struct Entry {
    uint64_t hash;
    uint64_t bytes;
    uint64_t last_used;
    uint32_t id;
  };
  using Map = std::unordered_map<GeoKey, Entry, GeoKeyHash>;

  // Makes room for `incoming` bytes: evicts least recently used entries
  // (ties: lowest id first). One scan of the index collects the entries not
  // used in the current frame; those in use are never evicted, so when
  // everything is in use the index goes over budget.
  //  - Entries that the previous frame did not use either go until the total
  //    is at the low-water mark. The per-draw keys of instanced streams leave
  //    such entries behind that only this removes, and a scan per insert at
  //    the budget cost tens of milliseconds a frame; one pass to the mark
  //    pays for the many inserts that follow.
  //  - Entries last used in the previous frame go only until the insert fits
  //    the budget. In the middle of a frame they are mostly entries that a
  //    later draw of this frame will look up; taking them to the mark would
  //    rebuild a live set that sits near the budget in every frame.
  // The heap pops older entries first, so once an entry of the previous frame
  // is on top everything left is one too.
  void Evict(uint64_t incoming, std::vector<uint32_t>* evicted) {
    std::vector<Map::iterator> idle;
    for (auto e = entries_.begin(); e != entries_.end(); ++e) {
      if (e->second.last_used != frame_) idle.push_back(e);
    }
    // A heap with the least recently used entry on top: only the entries
    // that leave are ordered.
    const auto later = [](const Map::iterator& a, const Map::iterator& b) {
      return a->second.last_used != b->second.last_used ? a->second.last_used > b->second.last_used
                                                        : a->second.id > b->second.id;
    };
    std::make_heap(idle.begin(), idle.end(), later);
    const uint64_t previous = frame_ ? frame_ - 1 : 0;  // (frame 0 has no previous frame)
    while (!idle.empty()) {
      std::pop_heap(idle.begin(), idle.end(), later);
      const Map::iterator victim = idle.back();
      const bool recent = victim->second.last_used >= previous;
      if (resident_ + incoming <= (recent ? budget_ : low_water_bytes())) break;
      idle.pop_back();
      resident_ -= victim->second.bytes;
      if (evicted) evicted->push_back(victim->second.id);
      entries_.erase(victim);  // erasing one element leaves the other iterators valid
    }
    nothing_idle_ = idle.empty();
  }

  Map entries_;
  uint64_t budget_;
  uint64_t resident_ = 0;
  uint64_t frame_ = 0;
  // A pass of this frame ended with no idle entry left (reset by BeginFrame).
  bool nothing_idle_ = false;
  uint32_t next_id_ = 1;
};

// Buffers whose cache entry was replaced, kept for reuse instead of being
// destroyed and created again (buffer creation dominates re-decoding
// animated streams). A buffer is reused only for the same size and only once
// the submission it was retired in has completed. Over the byte budget the
// oldest are handed back for destruction.
template <typename T>
class RetirePool {
 public:
  explicit RetirePool(uint64_t budget_bytes) : budget_(budget_bytes) {}

  void Retire(T obj, uint64_t bytes, uint64_t submission, std::vector<T>* destroy) {
    if (bytes > budget_) {
      destroy->push_back(obj);
      return;
    }
    items_.push_back({obj, bytes, submission});
    bytes_ += bytes;
    while (bytes_ > budget_ && !items_.empty()) {
      bytes_ -= items_.front().bytes;
      destroy->push_back(items_.front().obj);
      items_.pop_front();
    }
  }

  std::optional<T> Take(uint64_t bytes, uint64_t completed_submission) {
    for (auto it = items_.begin(); it != items_.end(); ++it) {
      if (it->bytes == bytes && it->submission <= completed_submission) {
        T obj = it->obj;
        bytes_ -= it->bytes;
        items_.erase(it);
        return obj;
      }
    }
    return std::nullopt;
  }

  void Drain(std::vector<T>* out) {
    for (const Item& i : items_) out->push_back(i.obj);
    items_.clear();
    bytes_ = 0;
  }

  uint64_t bytes() const { return bytes_; }

 private:
  struct Item {
    T obj;
    uint64_t bytes;
    uint64_t submission;
  };
  std::deque<Item> items_;
  uint64_t budget_;
  uint64_t bytes_ = 0;
};

// Decoded streams kept for the current frame only, keyed like the cache (key
// plus content hash): a mesh that many instanced draws expand is decoded once
// per frame, not once per draw. Under a byte budget; what does not fit is not
// stored and the caller decodes into its own buffer. Stored vectors stay
// where they are until Clear or until their key is stored again.
template <typename T>
class FrameMemo {
 public:
  explicit FrameMemo(uint64_t budget_bytes) : budget_(budget_bytes) {}

  void Clear() {
    entries_.clear();
    bytes_ = 0;
  }

  const std::vector<T>* Find(const GeoKey& key, uint64_t content_hash) const {
    const auto it = entries_.find(key);
    return it != entries_.end() && it->second.hash == content_hash ? &it->second.data : nullptr;
  }

  // Whether a vector of `count` elements would be stored now.
  bool Fits(uint64_t count) const { return bytes_ + count * sizeof(T) <= budget_; }

  // Stores `data` (replacing what the key held) and returns the stored
  // vector, or nullptr with `data` untouched if it does not fit.
  const std::vector<T>* Store(const GeoKey& key, uint64_t content_hash, std::vector<T>&& data) {
    if (const auto it = entries_.find(key); it != entries_.end()) {
      bytes_ -= uint64_t(it->second.data.size()) * sizeof(T);
      entries_.erase(it);
    }
    if (!Fits(data.size())) return nullptr;
    Entry& e = entries_[key];
    e.hash = content_hash;
    e.data = std::move(data);
    bytes_ += uint64_t(e.data.size()) * sizeof(T);
    return &e.data;
  }

  uint64_t bytes() const { return bytes_; }

 private:
  struct Entry {
    uint64_t hash = 0;
    std::vector<T> data;
  };
  std::unordered_map<GeoKey, Entry, GeoKeyHash> entries_;
  uint64_t budget_;
  uint64_t bytes_ = 0;
};

}  // namespace fable2::native::render
