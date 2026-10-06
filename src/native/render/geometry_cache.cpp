#include "geometry_cache.h"

#include <chrono>
#include <cstring>
#include <utility>

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

// Content hash of a buffer built from two guest ranges (vertex stream and
// bone palette, mesh stream and instance stream): first ^ (second * this).
constexpr uint64_t kSecondStreamMix = 0x9E3779B97F4A7C15ull;

}  // namespace

GeometryCache::GeometryCache(uint64_t budget_bytes) : index_(budget_bytes) {}

void GeometryCache::BeginFrame(uint64_t frame, uint64_t budget_bytes) {
  index_.set_budget_bytes(budget_bytes);
  index_.BeginFrame(frame);
  frame_hashes_.clear();
  mesh_position_memo_.Clear();
  mesh_uv_memo_.Clear();
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
  mesh_position_memo_.Clear();
  mesh_uv_memo_.Clear();
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

// An instanced draw's mesh stream: its first `count` vertices, decoded. Many
// draws expand one mesh (each with its own instance set and flat stream), so
// the decoded vertices are kept for the frame under the stream's key and
// content hash; what is past the memo's budget is decoded into the scratch
// vector for this draw alone. The count is not in the key: a draw that asks
// one stream for another count finds the size differs, decodes its own count
// and replaces what the key held.
const capture::Float4* GeometryCache::MeshPositions(const capture::DrawRecord& r,
                                                    const uint8_t* mesh, uint64_t mesh_hash,
                                                    uint32_t count) {
  const GeoKey key = MeshPositionKey(r);
  // (the size check: another count of the same stream, or a layout-hash
  // collision with another layout and so another count)
  if (const auto* found = mesh_position_memo_.Find(key, mesh_hash); found && found->size() == count) {
    return found->data();
  }
  if (mesh_position_memo_.Fits(count)) {
    std::vector<capture::Float4> decoded(count);
    if (!capture::DecodePositions(mesh, r.vb.size, r.pos, 0, count, decoded.data())) return nullptr;
    if (const auto* stored = mesh_position_memo_.Store(key, mesh_hash, std::move(decoded))) {
      return stored->data();
    }
  }
  mesh_positions_.resize(count);
  return capture::DecodePositions(mesh, r.vb.size, r.pos, 0, count, mesh_positions_.data())
             ? mesh_positions_.data()
             : nullptr;
}

const capture::Float2* GeometryCache::MeshUvs(const capture::DrawRecord& r, const uint8_t* mesh,
                                              uint64_t mesh_hash, uint32_t count) {
  const capture::BufferRef& vb = r.material.uv_vb;
  const GeoKey key = MeshUvKey(r);
  if (const auto* found = mesh_uv_memo_.Find(key, mesh_hash); found && found->size() == count) {
    return found->data();
  }
  if (mesh_uv_memo_.Fits(count)) {
    std::vector<capture::Float2> decoded(count);
    if (!capture::DecodeUvs(mesh, vb.size, r.material.uv, 0, count, decoded.data())) return nullptr;
    if (const auto* stored = mesh_uv_memo_.Store(key, mesh_hash, std::move(decoded))) {
      return stored->data();
    }
  }
  mesh_uvs_.resize(count);
  return capture::DecodeUvs(mesh, vb.size, r.material.uv, 0, count, mesh_uvs_.data())
             ? mesh_uvs_.data()
             : nullptr;
}

// Instanced draws: positions[i] = rows(copy(i)) * mesh[vertex(i)] + offset for
// every flat index i (instance_expand.h). The content hash covers the mesh
// stream and the instance stream; the key holds the constants. Only the
// vertices of one copy are decoded from the mesh stream (InstanceMeshCount).
nrhi::Buffer* GeometryCache::InstancedPositions(nrhi::Device* dev, const capture::DrawRecord& r,
                                                uint32_t* vertex_count, ClayStats& st) {
  const capture::InstanceSet& s = r.instances;
  const uint32_t mesh_count = InstanceMeshCount(PositionCount(r.vb.size, r.pos), s);
  // The capture caps flat_count (InstanceRangeSkip); nothing larger is built.
  if (mesh_count == 0 || s.rows_size == 0 || s.flat_count == 0 ||
      s.flat_count > capture::kMaxDrawCount) {
    ++st.skipped_other;
    return nullptr;
  }
  const auto t0 = Clock::now();
  const uint8_t* mesh = capture::ReadPhysical(r.vb.phys_addr, r.vb.size);
  const uint8_t* rows = mesh ? capture::ReadPhysical(s.rows_addr, s.rows_size) : nullptr;
  if (!rows) {
    st.hash_ms += Ms(t0, Clock::now());
    ++st.skipped_other;
    return nullptr;
  }
  const uint64_t mesh_hash = FrameHash(r.vb.phys_addr, r.vb.size, mesh);
  const uint64_t hash = mesh_hash ^ (FrameHash(s.rows_addr, s.rows_size, rows) * kSecondStreamMix);
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
  positions_.resize(s.flat_count);
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  const capture::Float4* decoded = MeshPositions(r, mesh, mesh_hash, mesh_count);
  if (decoded && capture::ExpandInstances(decoded, mesh_count, rows, s.rows_size, s, s.flat_count,
                                          positions_.data())) {
    buffer = Upload(dev, positions_.data(), uint64_t(s.flat_count) * sizeof(capture::Float4), &alloc);
  }
  if (buffer) {
    Insert(dev, key, hash, {buffer, s.flat_count, 0, alloc}, alloc);
    *vertex_count = s.flat_count;
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
  if (r.instances.active) return InstancedPositions(dev, r, vertex_count, st);
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
    hash ^= XXH3_64bits(palette, r.skin.palette_size) * kSecondStreamMix;
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

// Instanced draws: uvs[i] = mesh_uv[vertex(i)] for every flat index i. The
// instance stream does not enter: the content hash is the mesh UV stream's and
// the key (UvKey) holds what maps an index to a mesh vertex, so draws whose
// copies moved or changed keep their UV buffer. Only the UVs of one copy's
// vertices are decoded (InstanceMeshCount).
nrhi::Buffer* GeometryCache::InstancedUvs(nrhi::Device* dev, const capture::DrawRecord& r,
                                          uint32_t vertex_count, ClayStats& st) {
  const capture::BufferRef& vb = r.material.uv_vb;
  const capture::InstanceSet& s = r.instances;
  const uint32_t mesh_count = InstanceMeshCount(UvCount(vb.size, r.material.uv), s);
  if (mesh_count == 0 || s.flat_count < vertex_count || s.flat_count > capture::kMaxDrawCount) {
    return nullptr;
  }
  const auto t0 = Clock::now();
  const uint8_t* mesh = capture::ReadPhysical(vb.phys_addr, vb.size);
  if (!mesh) {
    st.hash_ms += Ms(t0, Clock::now());
    return nullptr;
  }
  const uint64_t hash = FrameHash(vb.phys_addr, vb.size, mesh);
  const GeoKey key = UvKey(r);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end() && it->second.count >= vertex_count) {
      ++st.hits;
      return it->second.buffer;
    }
  }
  uvs_.resize(s.flat_count);
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  const capture::Float2* decoded =
      s.flat_count != 0 ? MeshUvs(r, mesh, hash, mesh_count) : nullptr;
  if (decoded) {
    capture::ExpandInstanceUvs(decoded, mesh_count, s, s.flat_count, uvs_.data());
    buffer = Upload(dev, uvs_.data(), uint64_t(s.flat_count) * sizeof(capture::Float2), &alloc);
  }
  if (buffer) {
    Insert(dev, key, hash, {buffer, s.flat_count, 0, alloc}, alloc);
    ++st.uploads;
  }
  st.decode_ms += Ms(t1, Clock::now());
  return buffer;
}

nrhi::Buffer* GeometryCache::Uvs(nrhi::Device* dev, const capture::DrawRecord& r,
                                 uint32_t vertex_count, ClayStats& st) {
  if (r.instances.active) return InstancedUvs(dev, r, vertex_count, st);
  const capture::BufferRef& vb = r.material.uv_vb;
  const uint32_t count = UvCount(vb.size, r.material.uv);
  if (count == 0 || count < vertex_count) return nullptr;
  const auto t0 = Clock::now();
  // The UV stream may differ from the position stream and is not range
  // checked at capture: read exactly uv_vb, decode within it.
  const uint8_t* src = capture::ReadPhysical(vb.phys_addr, vb.size);
  if (!src) {
    st.hash_ms += Ms(t0, Clock::now());
    return nullptr;
  }
  const uint64_t hash = FrameHash(vb.phys_addr, vb.size, src);
  const GeoKey key = UvKey(r);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end() && it->second.count >= vertex_count) {
      ++st.hits;
      return it->second.buffer;
    }
  }
  uvs_.resize(count);
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  if (capture::DecodeUvs(src, vb.size, r.material.uv, 0, count, uvs_.data())) {
    buffer = Upload(dev, uvs_.data(), uint64_t(count) * sizeof(capture::Float2), &alloc);
  }
  if (buffer) {
    Insert(dev, key, hash, {buffer, count, 0, alloc}, alloc);
    ++st.uploads;
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
