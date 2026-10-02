#include "geometry_cache.h"

#include <chrono>
#include <cstring>

#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif
#include <xxhash.h>

#include "../capture/guest_read.h"

namespace fable2::native::render {

namespace {

using Clock = std::chrono::steady_clock;

double Ms(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

uint64_t RoundUp256(uint64_t v) { return (v + 255) & ~uint64_t(255); }

}  // namespace

GeometryCache::GeometryCache() : index_(512ull << 20) {}

void GeometryCache::BeginFrame(uint64_t frame, uint64_t budget_bytes) {
  index_.set_budget_bytes(budget_bytes);
  index_.BeginFrame(frame);
  frame_hashes_.clear();
}

void GeometryCache::Release(nrhi::Device* dev) {
  if (dev) {
    for (auto& [id, e] : entries_) dev->DestroyDeferred(e.buffer);
  }
  entries_.clear();
  frame_hashes_.clear();
  index_ = GeometryCacheIndex(index_.budget_bytes());
}

nrhi::Buffer* GeometryCache::Upload(nrhi::Device* dev, const void* data, uint64_t bytes,
                                    uint64_t* alloc_bytes) {
  nrhi::BufferDesc desc;
  desc.size = RoundUp256(bytes);
  desc.heap = nrhi::HeapKind::kUpload;
  desc.bind_class = nrhi::BufferBindClass::kFull;
  nrhi::Buffer* buffer = dev->CreateBuffer(desc);
  if (!buffer) return nullptr;
  void* mapped = dev->Map(buffer);  // kept mapped (persistent mapping)
  if (!mapped) {
    dev->DestroyDeferred(buffer);
    return nullptr;
  }
  std::memcpy(mapped, data, bytes);
  *alloc_bytes = desc.size;
  return buffer;
}

uint32_t GeometryCache::Insert(nrhi::Device* dev, const GeoKey& key, uint64_t hash,
                               const Entry& e, uint64_t bytes) {
  evicted_.clear();
  const uint32_t id = index_.Insert(key, hash, bytes, &evicted_);
  for (uint32_t old : evicted_) {
    auto it = entries_.find(old);
    if (it == entries_.end()) continue;
    dev->DestroyDeferred(it->second.buffer);
    entries_.erase(it);
  }
  entries_[id] = e;
  return id;
}

nrhi::Buffer* GeometryCache::Positions(nrhi::Device* dev, const capture::DrawRecord& r,
                                       ClayStats& st) {
  const uint32_t count = PositionCount(r.vb.size, r.pos);
  if (count == 0) {
    ++st.skipped_other;
    return nullptr;
  }
  const auto t0 = Clock::now();
  const uint8_t* src = capture::ReadPhysical(r.vb.phys_addr, r.vb.size);
  if (!src) {
    st.hash_ms += Ms(t0, Clock::now());
    ++st.skipped_other;
    return nullptr;
  }
  const uint64_t range = (uint64_t(r.vb.phys_addr) << 32) | r.vb.size;
  uint64_t hash;
  if (auto it = frame_hashes_.find(range); it != frame_hashes_.end()) {
    hash = it->second;
  } else {
    hash = XXH3_64bits(src, r.vb.size);
    frame_hashes_.emplace(range, hash);
  }
  const GeoKey key = PositionKey(r);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end()) {
      ++st.hits;
      return it->second.buffer;
    }
  }

  positions_.resize(count);
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  if (capture::DecodePositions(src, r.vb.size, r.pos, 0, count, positions_.data())) {
    buffer = Upload(dev, positions_.data(), uint64_t(count) * sizeof(capture::Float4), &alloc);
  }
  if (buffer) {
    Insert(dev, key, hash, {buffer, count, 0}, alloc);
    ++st.uploads;
  } else {
    ++st.skipped_other;
  }
  st.decode_ms += Ms(t1, Clock::now());
  return buffer;
}

nrhi::Buffer* GeometryCache::Indices(nrhi::Device* dev, const capture::DrawRecord& r,
                                     uint32_t vertex_count, uint32_t* index_count, ClayStats& st) {
  const auto t0 = Clock::now();
  const uint8_t* data = nullptr;
  IndexBytes bytes;
  uint64_t hash = 0;
  if (r.indexed) {
    if (r.count == 0 || !ReferencedIndexBytes(r, &bytes) ||
        !(data = capture::ReadPhysical(r.ib.phys_addr + bytes.offset, bytes.size))) {
      st.hash_ms += Ms(t0, Clock::now());
      ++st.skipped_other;
      return nullptr;
    }
    hash = XXH3_64bits(data, bytes.size);
  }
  const GeoKey key = IndexKey(r);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);

  const Entry* entry = nullptr;
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end()) {
      ++st.hits;
      entry = &it->second;
    }
  }
  if (!entry) {
    indices_.clear();
    uint32_t max_index = 0;
    // data already points at the draw's first index; non-indexed draws
    // generate start + i.
    const capture::IndexSource src{data, bytes.size, r.index32};
    const bool built = capture::BuildTriangleList(src, r.indexed ? 0 : r.start, r.count, r.prim,
                                                  indices_, &max_index);
    nrhi::Buffer* buffer = nullptr;
    uint64_t alloc = 0;
    if (built && !indices_.empty()) {
      buffer = Upload(dev, indices_.data(), uint64_t(indices_.size()) * sizeof(uint32_t), &alloc);
    }
    if (!buffer) {
      indices_.clear();  // BuildTriangleList may leave a partial list on failure
      st.decode_ms += Ms(t1, Clock::now());
      ++st.skipped_other;
      return nullptr;
    }
    const uint32_t id = Insert(dev, key, hash, {buffer, uint32_t(indices_.size()), max_index}, alloc);
    entry = &entries_[id];
    ++st.uploads;
    st.decode_ms += Ms(t1, Clock::now());
  }
  // The renderer is the producer of kBadIndex for index ranges.
  if (!IndexRangeValid(entry->max_index, r.base_vertex, vertex_count)) {
    ++st.skipped_bad_index;
    return nullptr;
  }
  *index_count = entry->count;
  return entry->buffer;
}

}  // namespace fable2::native::render
