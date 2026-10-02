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

GeometryCache::GeometryCache(uint64_t budget_bytes) : index_(budget_bytes) {}

void GeometryCache::BeginFrame(uint64_t frame, uint64_t budget_bytes) {
  index_.set_budget_bytes(budget_bytes);
  index_.BeginFrame(frame);
  frame_hashes_.clear();
}

void GeometryCache::Release(nrhi::Device* dev) {
  destroy_.clear();
  pool_.Drain(&destroy_);
  if (dev) {
    for (auto& [id, e] : entries_) dev->DestroyDeferred(e.buffer);
    for (nrhi::Buffer* b : destroy_) dev->DestroyDeferred(b);
  }
  destroy_.clear();
  entries_.clear();
  frame_hashes_.clear();
  index_ = GeometryCacheIndex(index_.budget_bytes());
}

void GeometryCache::Retire(nrhi::Device* dev, nrhi::Buffer* buffer, uint64_t bytes) {
  destroy_.clear();
  pool_.Retire(buffer, bytes, dev->CurrentSubmission(), &destroy_);
  for (nrhi::Buffer* b : destroy_) dev->DestroyDeferred(b);
  destroy_.clear();
}

nrhi::Buffer* GeometryCache::Upload(nrhi::Device* dev, const void* data, uint64_t bytes,
                                    uint64_t* alloc_bytes) {
  // A retired buffer of the same size whose last use has completed.
  if (auto reused = pool_.Take(RoundUp256(bytes), dev->CompletedSubmission())) {
    if (void* mapped = dev->Map(*reused)) {
      std::memcpy(mapped, data, bytes);
      *alloc_bytes = RoundUp256(bytes);
      return *reused;
    }
    dev->DestroyDeferred(*reused);
  }
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
    Retire(dev, it->second.buffer, it->second.bytes);
    entries_.erase(it);
  }
  entries_[id] = e;
  return id;
}

uint64_t GeometryCache::FrameHash(uint32_t phys_addr, uint32_t size, const uint8_t* data) {
  const uint64_t range = (uint64_t(phys_addr) << 32) | size;
  if (auto it = frame_hashes_.find(range); it != frame_hashes_.end()) return it->second;
  const uint64_t hash = XXH3_64bits(data, size);
  frame_hashes_.emplace(range, hash);
  return hash;
}

// Terrain patch runs: the grid is rebuilt from the heightmap when the map's
// bytes or any patch parameter (the key) change.
nrhi::Buffer* GeometryCache::TerrainPositions(nrhi::Device* dev, const capture::DrawRecord& r,
                                              uint32_t* vertex_count, ClayStats& st) {
  const capture::HeightMap& m = r.terrain.map;
  const auto t0 = Clock::now();
  const uint8_t* src = m.size ? capture::ReadPhysical(m.phys_addr, m.size) : nullptr;
  if (!src) {
    st.hash_ms += Ms(t0, Clock::now());
    ++st.skipped_other;
    return nullptr;
  }
  const uint64_t hash = FrameHash(m.phys_addr, m.size, src);
  const GeoKey key = PositionKey(r);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end()) {
      ++st.hits;
      *vertex_count = it->second.count;
      return it->second.buffer;
    }
  }
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  if (capture::BuildTerrainPositions(r.terrain, src, m.size, capture::kTerrainGrid, &positions_)) {
    buffer = Upload(dev, positions_.data(), uint64_t(positions_.size()) * sizeof(capture::Float4),
                    &alloc);
  }
  if (buffer) {
    const uint32_t count = uint32_t(positions_.size());
    Insert(dev, key, hash, {buffer, count, 0, alloc}, alloc);
    *vertex_count = count;
    ++st.uploads;
  } else {
    ++st.skipped_other;
  }
  st.decode_ms += Ms(t1, Clock::now());
  return buffer;
}

nrhi::Buffer* GeometryCache::Positions(nrhi::Device* dev, const capture::DrawRecord& r,
                                       uint32_t* vertex_count, ClayStats& st) {
  if (r.terrain.active) return TerrainPositions(dev, r, vertex_count, st);
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
  uint64_t hash = FrameHash(r.vb.phys_addr, r.vb.size, src);
  // A skinned stream also depends on its bone palette (animated per frame).
  const uint8_t* palette = nullptr;
  if (r.skin.active) {
    palette = capture::ReadPhysical(r.skin.palette_addr, r.skin.palette_size);
    if (!palette) {
      st.hash_ms += Ms(t0, Clock::now());
      ++st.skipped_other;
      return nullptr;
    }
    hash ^= XXH3_64bits(palette, r.skin.palette_size) * 0x9E3779B97F4A7C15ull;
  }
  const GeoKey key = PositionKey(r);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end()) {
      ++st.hits;
      *vertex_count = it->second.count;
      return it->second.buffer;
    }
  }

  positions_.resize(count);
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  bool decoded = capture::DecodePositions(src, r.vb.size, r.pos, 0, count, positions_.data());
  if (decoded && r.skin.active) {
    decoded = capture::SkinPositions(src, r.vb.size, palette, r.skin.palette_size, r.skin,
                                     r.pos.stride_bytes, 0, count, positions_.data());
  }
  if (decoded) {
    buffer = Upload(dev, positions_.data(), uint64_t(count) * sizeof(capture::Float4), &alloc);
  }
  if (buffer) {
    Insert(dev, key, hash, {buffer, count, 0, alloc}, alloc);
    *vertex_count = count;
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
  if (r.indexed && !r.terrain.active) {
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
    bool built;
    if (r.terrain.active) {
      capture::BuildGridIndices(capture::kTerrainGrid, r.terrain.patches, &indices_);
      max_index = r.terrain.patches * (capture::kTerrainGrid + 1) * (capture::kTerrainGrid + 1) - 1;
      built = r.terrain.patches != 0;
    } else {
      const capture::IndexSource src{data, bytes.size, r.index32};
      built = capture::BuildTriangleList(src, r.indexed ? 0 : r.start, r.count, r.prim, indices_,
                                         &max_index);
    }
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
    const uint32_t id =
        Insert(dev, key, hash, {buffer, uint32_t(indices_.size()), max_index, alloc}, alloc);
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
