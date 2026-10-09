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
// A third range (a skinned record's normals: the normal's stream, the stream of
// the bone words, the palette): ... ^ (third * this).
constexpr uint64_t kThirdStreamMix = 0xC2B2AE3D27D4EB4Full;

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
                                              const capture::TerrainPatch& t, uint32_t* vertex_count,
                                              ClayStats& st, PositionBounds* bounds) {
  const capture::HeightMap& m = t.map;
  const auto t0 = Clock::now();
  const uint8_t* src = m.size ? capture::ReadPhysical(m.phys_addr, m.size) : nullptr;
  if (!src) {
    st.hash_ms += Ms(t0, Clock::now());
    ++st.skipped_other;
    return nullptr;
  }
  const uint64_t hash = FrameHash(m.phys_addr, m.size, src);
  const GeoKey key = PositionKey(r, &t);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end()) {
      ++st.hits;
      *vertex_count = it->second.count;
      if (bounds) *bounds = it->second.bounds;
      return it->second.buffer;
    }
  }
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  if (capture::BuildTerrainPositions(t, src, m.size, capture::kTerrainGrid, &positions_)) {
    buffer = Upload(dev, positions_.data(), uint64_t(positions_.size()) * sizeof(capture::Float4),
                    &alloc);
  }
  if (buffer) {
    const uint32_t count = uint32_t(positions_.size());
    Entry e{buffer, count, 0, alloc};
    e.bounds = MakePositionBounds(positions_.data(), count);
    if (bounds) *bounds = e.bounds;
    Insert(dev, key, hash, e, alloc);
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
                                                uint32_t* vertex_count, ClayStats& st, PositionBounds* bounds) {
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
  const GeoKey key = PositionKey(r, nullptr);  // (never a terrain record)
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end()) {
      ++st.hits;
      *vertex_count = it->second.count;
      if (bounds) *bounds = it->second.bounds;
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
    Entry e{buffer, s.flat_count, 0, alloc};
    e.bounds = MakePositionBounds(positions_.data(), s.flat_count);
    if (bounds) *bounds = e.bounds;
    Insert(dev, key, hash, e, alloc);
    *vertex_count = s.flat_count;
    ++st.uploads;
  } else {
    ++st.skipped_other;
  }
  st.decode_ms += Ms(t1, Clock::now());
  return buffer;
}

nrhi::Buffer* GeometryCache::Positions(nrhi::Device* dev, const capture::DrawRecord& r,
                                       const capture::TerrainPatch* terrain, uint32_t* vertex_count,
                                       ClayStats& st, PositionBounds* bounds) {
  if (bounds) *bounds = PositionBounds{};
  if (terrain) return TerrainPositions(dev, r, *terrain, vertex_count, st, bounds);
  if (r.instances.active) return InstancedPositions(dev, r, vertex_count, st, bounds);
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
    // (once per frame and palette: the record's normals and the other draws of the skeleton ask again)
    hash ^= FrameHash(r.skin.palette_addr, r.skin.palette_size, palette) * kSecondStreamMix;
  }
  const GeoKey key = PositionKey(r, nullptr);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end()) {
      ++st.hits;
      *vertex_count = it->second.count;
      if (bounds) *bounds = it->second.bounds;
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
    Entry e{buffer, count, 0, alloc};
    e.bounds = MakePositionBounds(positions_.data(), count);
    if (bounds) *bounds = e.bounds;
    Insert(dev, key, hash, e, alloc);
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

nrhi::Buffer* GeometryCache::TerrainFarUvs(nrhi::Device* dev, const capture::TerrainPatch& t,
                                           uint32_t vertex_count, ClayStats& st) {
  // A table of garbage constants can claim hundreds of MB; the largest seen is 86 KB.
  constexpr uint32_t kMaxTableBytes = 16u << 20;
  const capture::TerrainMaterialInputs& m = t.material;
  capture::HeightMap um, vm;
  if (!t.active || !m.valid || !m.adaptive || !capture::DecodeHeightMap(m.far_u_fetch, &um) ||
      !capture::DecodeHeightMap(m.far_v_fetch, &vm) || um.size == 0 || vm.size == 0 ||
      um.size > kMaxTableBytes || vm.size > kMaxTableBytes) {
    return nullptr;
  }
  const auto t0 = Clock::now();
  const uint8_t* u = capture::ReadPhysical(um.phys_addr, um.size);
  const uint8_t* v = u ? capture::ReadPhysical(vm.phys_addr, vm.size) : nullptr;
  if (!v) {
    st.hash_ms += Ms(t0, Clock::now());
    return nullptr;
  }
  const uint64_t hash = FrameHash(um.phys_addr, um.size, u) ^
                        (FrameHash(vm.phys_addr, vm.size, v) * kSecondStreamMix);
  const GeoKey key = TerrainFarUvKey(t);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end() && it->second.count >= vertex_count) {
      ++st.hits;
      return it->second.buffer;
    }
  }
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  if (capture::BuildTerrainFarUvs(t, um, u, um.size, vm, v, vm.size, capture::kTerrainGrid, &uvs_) &&
      uvs_.size() >= vertex_count) {
    buffer = Upload(dev, uvs_.data(), uint64_t(uvs_.size()) * sizeof(capture::Float2), &alloc);
  }
  if (buffer) {
    Insert(dev, key, hash, {buffer, uint32_t(uvs_.size()), 0, alloc}, alloc);
    ++st.uploads;
  }
  st.decode_ms += Ms(t1, Clock::now());
  return buffer;
}

nrhi::Buffer* GeometryCache::TerrainNormals(nrhi::Device* dev, const capture::TerrainPatch& t,
                                            uint32_t vertex_count, ClayStats& st) {
  // A map of garbage constants can claim hundreds of MB; the one seen is 803 KB.
  constexpr uint32_t kMaxMapBytes = 16u << 20;
  const capture::TerrainMaterialInputs& m = t.material;
  capture::NormalMap nm;
  // (a record without a normal map has a zero fetch constant, which is no map)
  if (!t.active || !m.valid || !capture::DecodeNormalMap(m.normal_fetch, &nm) || nm.size == 0 ||
      nm.size > kMaxMapBytes) {
    return nullptr;
  }
  const auto t0 = Clock::now();
  const uint8_t* src = capture::ReadPhysical(nm.phys_addr, nm.size);
  if (!src) {
    st.hash_ms += Ms(t0, Clock::now());
    return nullptr;
  }
  const uint64_t hash = FrameHash(nm.phys_addr, nm.size, src);
  const GeoKey key = TerrainNormalKey(t);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end() && it->second.count >= vertex_count) {
      ++st.hits;
      return it->second.buffer;
    }
  }
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  if (capture::BuildTerrainNormals(t, nm, src, nm.size, capture::kTerrainGrid, &uvs_) &&
      uvs_.size() >= vertex_count) {
    buffer = Upload(dev, uvs_.data(), uint64_t(uvs_.size()) * sizeof(capture::Float2), &alloc);
  }
  if (buffer) {
    Insert(dev, key, hash, {buffer, uint32_t(uvs_.size()), 0, alloc}, alloc);
    ++st.uploads;
  }
  st.decode_ms += Ms(t1, Clock::now());
  return buffer;
}

// Instanced draws: normals[i] = the mesh normal of vertex(i) through the 3 x 3
// of copy(i)'s rows as it is, for every flat index i (ExpandInstanceNormals). The
// content hash covers the normal's stream and the instance stream. Only the
// normals of one copy's vertices are decoded (InstanceMeshCount): a few
// hundred at most, so they are not kept for the frame as the positions are.
nrhi::Buffer* GeometryCache::InstancedNormals(nrhi::Device* dev, const capture::DrawRecord& r,
                                              const capture::LightInputs& light, uint32_t vertex_count,
                                              ClayStats& st) {
  const capture::LightElement& e = light.normal;
  const capture::InstanceSet& s = r.instances;
  const uint32_t mesh_count = InstanceMeshCount(NormalElementCount(e), s);
  if (mesh_count == 0 || s.rows_size == 0 || s.flat_count == 0 || s.flat_count < vertex_count ||
      s.flat_count > capture::kMaxDrawCount) {
    return nullptr;
  }
  const auto t0 = Clock::now();
  const uint8_t* mesh = capture::ReadPhysical(e.vb.phys_addr, e.vb.size);
  const uint8_t* rows = mesh ? capture::ReadPhysical(s.rows_addr, s.rows_size) : nullptr;
  if (!rows) {
    st.hash_ms += Ms(t0, Clock::now());
    return nullptr;
  }
  const uint64_t hash = FrameHash(e.vb.phys_addr, e.vb.size, mesh) ^
                        (FrameHash(s.rows_addr, s.rows_size, rows) * kSecondStreamMix);
  const GeoKey key = NormalKey(r, light);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end() && it->second.count >= vertex_count) {
      ++st.hits;
      return it->second.buffer;
    }
  }
  mesh_normals_.resize(mesh_count);
  normals_.resize(s.flat_count);
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  if (DecodeNormals(mesh, e.vb.size, e, 0, mesh_count, mesh_normals_.data()) &&
      ExpandInstanceNormals(mesh_normals_.data(), mesh_count, rows, s.rows_size, s, s.flat_count, normals_.data())) {
    buffer = Upload(dev, normals_.data(), uint64_t(s.flat_count) * sizeof(Float3), &alloc);
  }
  if (buffer) {
    Insert(dev, key, hash, {buffer, s.flat_count, 0, alloc}, alloc);
    ++st.uploads;
  }
  st.decode_ms += Ms(t1, Clock::now());
  return buffer;
}

nrhi::Buffer* GeometryCache::Normals(nrhi::Device* dev, const capture::DrawRecord& r,
                                     const capture::LightInputs& light, uint32_t need, ClayStats& st,
                                     capture::LightReason* why, uint32_t* out_count) {
  using capture::LightReason;
  using capture::LightVertexKind;
  const capture::LightElement& e = light.normal;
  *out_count = 0;
  *why = LightReason::kNormalUnsupported;
  if (light.kind == LightVertexKind::kNone || light.kind == LightVertexKind::kTerrain || !NormalElementSupported(e)) {
    return nullptr;
  }
  *why = LightReason::kLightUnknown;
  // The inputs are an instance's exactly when the record has its instance set (MakeLightInputs).
  if ((light.kind == LightVertexKind::kInstance) != r.instances.active) return nullptr;
  if (r.instances.active) {
    nrhi::Buffer* flat = InstancedNormals(dev, r, light, need, st);
    if (flat) *out_count = r.instances.flat_count;
    return flat;
  }
  // A skinned record's normals follow its positions vertex for vertex: no more than those.
  // Every vertex the draw names has to be there (`need`), not every vertex of the position
  // stream: the capture's own rule for the normal's stream.
  uint32_t count = NormalElementCount(e);
  if (r.skin.active) count = std::min(count, PositionCount(r.vb.size, r.pos));
  if (need == 0 || count < need) return nullptr;
  const auto t0 = Clock::now();
  // The normal's stream is read as the light inputs name it (the position's in every draw seen,
  // but not range checked with it at capture): exactly its range, decoded within it.
  const uint8_t* src = capture::ReadPhysical(e.vb.phys_addr, e.vb.size);
  const uint8_t* vb = nullptr;
  const uint8_t* palette = nullptr;
  uint64_t hash = 0;
  bool readable = src != nullptr;
  if (readable) hash = FrameHash(e.vb.phys_addr, e.vb.size, src);
  if (readable && r.skin.active) {
    // The bone words are the position stream's, the bones the palette's.
    vb = capture::ReadPhysical(r.vb.phys_addr, r.vb.size);
    palette = vb ? capture::ReadPhysical(r.skin.palette_addr, r.skin.palette_size) : nullptr;
    readable = palette != nullptr;
    if (readable) {
      hash ^= (FrameHash(r.skin.palette_addr, r.skin.palette_size, palette) * kSecondStreamMix) ^
              (FrameHash(r.vb.phys_addr, r.vb.size, vb) * kThirdStreamMix);
    }
  }
  if (!readable) {
    st.hash_ms += Ms(t0, Clock::now());
    return nullptr;
  }
  const GeoKey key = NormalKey(r, light);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end() && it->second.count >= need) {
      ++st.hits;
      *out_count = it->second.count;
      return it->second.buffer;
    }
  }
  normals_.resize(count);
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  bool decoded = DecodeNormals(src, e.vb.size, e, 0, count, normals_.data());
  if (decoded && r.skin.active) {
    decoded = SkinNormals(vb, r.vb.size, palette, r.skin.palette_size, r.skin, r.pos.stride_bytes, 0, count,
                          normals_.data());
  }
  if (decoded) buffer = Upload(dev, normals_.data(), uint64_t(count) * sizeof(Float3), &alloc);
  if (buffer) {
    Insert(dev, key, hash, {buffer, count, 0, alloc}, alloc);
    ++st.uploads;
    *out_count = count;
  }
  st.decode_ms += Ms(t1, Clock::now());
  return buffer;
}

nrhi::Buffer* GeometryCache::Colours(nrhi::Device* dev, const capture::DrawRecord& r,
                                     const capture::LightInputs& light, uint32_t need, ClayStats& st,
                                     uint32_t* out_count) {
  const capture::LightElement& e = light.second;
  *out_count = 0;
  if (light.kind != capture::LightVertexKind::kStream || r.instances.active) return nullptr;
  // As many colours as the stream holds from the draw's offset on, at most the record's
  // positions; fewer than the draw's indices name (`need`: the stream ends first, or the offset
  // lies past it) is none. Meshes share a colour stream, so one that ends before the position
  // stream's last vertex is the rule, not a fault.
  const uint32_t count = ColourCount(e, light.colour_first, PositionCount(r.vb.size, r.pos));
  if (need == 0 || count < need) return nullptr;
  const auto t0 = Clock::now();
  const uint8_t* src = capture::ReadPhysical(e.vb.phys_addr, e.vb.size);
  if (!src) {
    st.hash_ms += Ms(t0, Clock::now());
    return nullptr;
  }
  // (the whole stream: meshes that share it share the hash of the frame)
  const uint64_t hash = FrameHash(e.vb.phys_addr, e.vb.size, src);
  const GeoKey key = ColourKey(light, count);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end() && it->second.count >= need) {
      ++st.hits;
      *out_count = it->second.count;
      return it->second.buffer;
    }
  }
  colours_.resize(count);
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  if (DecodeColours(src, e.vb.size, e, light.colour_first, count, colours_.data())) {
    buffer = Upload(dev, colours_.data(), uint64_t(count) * sizeof(uint32_t), &alloc);
  }
  if (buffer) {
    Insert(dev, key, hash, {buffer, count, 0, alloc}, alloc);
    ++st.uploads;
    *out_count = count;
  }
  st.decode_ms += Ms(t1, Clock::now());
  return buffer;
}

// The probe buffer's second region is bound from an offset (normal_decode.h, ProbeSetOffset).
static_assert(kProbeRegionAlignment == nrhi::kBufferOffsetAlignment);

nrhi::Buffer* GeometryCache::Probe(nrhi::Device* dev, const capture::DrawRecord& r, const capture::LightInputs& light,
                                   uint32_t need, ClayStats& st, LightSecond* why, uint32_t* out_count) {
  const capture::LightElement& e = light.second;
  *out_count = 0;
  *why = LightSecond::kElement;
  if (light.kind != capture::LightVertexKind::kProbe || r.instances.active || !WeightsElementSupported(e) ||
      !ProbeSetSupported(r.pos, light.a0_comp)) {
    return nullptr;
  }
  *why = LightSecond::kStream;
  // One entry for every vertex both streams hold: the mesh's, whichever part of it a draw names
  // (and, for a skinned mesh, whatever its palette holds: the weights are the bind mesh's).
  const uint32_t count = std::min(WeightsCount(e), PositionCount(r.vb.size, r.pos));
  if (need == 0 || count < need) return nullptr;
  const auto t0 = Clock::now();
  const uint8_t* src = capture::ReadPhysical(e.vb.phys_addr, e.vb.size);
  const uint8_t* vb = src ? capture::ReadPhysical(r.vb.phys_addr, r.vb.size) : nullptr;
  if (!vb) {
    st.hash_ms += Ms(t0, Clock::now());
    return nullptr;
  }
  const uint64_t hash = FrameHash(e.vb.phys_addr, e.vb.size, src) ^
                        (FrameHash(r.vb.phys_addr, r.vb.size, vb) * kSecondStreamMix);
  const GeoKey key = ProbeKey(r, light);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end() && it->second.count >= need) {
      ++st.hits;
      *out_count = it->second.count;
      *why = LightSecond::kNone;
      return it->second.buffer;
    }
  }
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  // All or nothing: one vertex whose set index is not 0, 4 or 8, or one weight that is not a
  // number of usable size, and the mesh has no buffer.
  *why = BuildProbe(src, e.vb.size, e, vb, r.vb.size, r.pos, light.a0_comp, count, &probe_);
  if (*why == LightSecond::kNone) {
    buffer = Upload(dev, probe_.data(), uint64_t(probe_.size()), &alloc);
    if (!buffer) *why = LightSecond::kUpload;
  }
  if (buffer) {
    Insert(dev, key, hash, {buffer, count, 0, alloc}, alloc);
    ++st.uploads;
    *out_count = count;
  }
  st.decode_ms += Ms(t1, Clock::now());
  return buffer;
}

nrhi::Buffer* GeometryCache::InstanceColours(nrhi::Device* dev, const capture::DrawRecord& r,
                                             const capture::LightInputs& light, uint32_t need, ClayStats& st,
                                             LightSecond* why, uint32_t* out_count) {
  const capture::LightElement& e = light.second;
  const capture::InstanceSet& s = r.instances;
  *out_count = 0;
  *why = LightSecond::kElement;
  if (light.kind != capture::LightVertexKind::kInstance || !s.active || !InstanceColourElementSupported(e)) return nullptr;
  *why = LightSecond::kStream;
  // The capture caps flat_count (InstanceRangeSkip); nothing larger is built.
  if (need == 0 || s.flat_count < need || s.flat_count > capture::kMaxDrawCount) return nullptr;
  const auto t0 = Clock::now();
  // The element's stream as the light inputs name it (the instance set's in every draw seen):
  // exactly its range, read within it.
  const uint8_t* src = capture::ReadPhysical(e.vb.phys_addr, e.vb.size);
  if (!src) {
    st.hash_ms += Ms(t0, Clock::now());
    return nullptr;
  }
  const uint64_t hash = FrameHash(e.vb.phys_addr, e.vb.size, src);
  const GeoKey key = InstanceColourKey(r, light);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end() && it->second.count >= need) {
      ++st.hits;
      *out_count = it->second.count;
      *why = LightSecond::kNone;
      return it->second.buffer;
    }
  }
  colours_.resize(s.flat_count);
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  // (false: a copy the draw names has its colour past the stream)
  if (ExpandInstanceColours(src, e.vb.size, e, s, s.flat_count, colours_.data())) {
    buffer = Upload(dev, colours_.data(), uint64_t(s.flat_count) * sizeof(uint32_t), &alloc);
    *why = buffer ? LightSecond::kNone : LightSecond::kUpload;
  }
  if (buffer) {
    Insert(dev, key, hash, {buffer, s.flat_count, 0, alloc}, alloc);
    ++st.uploads;
    *out_count = s.flat_count;
  }
  st.decode_ms += Ms(t1, Clock::now());
  return buffer;
}

// Instanced plants: bend[i] = (the up axis of copy(i), the height of vertex(i) in its copy) for
// every flat index i (ExpandInstanceBend). Like the flat positions, of the mesh's position stream
// and the instance stream (the content hash covers both; the key holds the mesh's layout and the
// instance set without its offset). The mesh is the one the flat positions were expanded from this
// frame, so its decoded vertices come from the frame's memo.
nrhi::Buffer* GeometryCache::InstanceBend(nrhi::Device* dev, const capture::DrawRecord& r, uint32_t need, ClayStats& st,
                                          LightSecond* why, uint32_t* out_count, uint64_t* out_bytes) {
  const capture::InstanceSet& s = r.instances;
  *out_count = 0;
  *out_bytes = 0;
  *why = LightSecond::kStream;
  const uint32_t mesh_count = s.active ? InstanceMeshCount(PositionCount(r.vb.size, r.pos), s) : 0;
  // The capture caps flat_count (InstanceRangeSkip); nothing larger is built.
  if (mesh_count == 0 || s.rows_size == 0 || need == 0 || s.flat_count < need || s.flat_count > capture::kMaxDrawCount) {
    return nullptr;
  }
  const auto t0 = Clock::now();
  const uint8_t* mesh = capture::ReadPhysical(r.vb.phys_addr, r.vb.size);
  const uint8_t* rows = mesh ? capture::ReadPhysical(s.rows_addr, s.rows_size) : nullptr;
  if (!rows) {
    st.hash_ms += Ms(t0, Clock::now());
    return nullptr;
  }
  const uint64_t mesh_hash = FrameHash(r.vb.phys_addr, r.vb.size, mesh);
  const uint64_t hash = mesh_hash ^ (FrameHash(s.rows_addr, s.rows_size, rows) * kSecondStreamMix);
  const GeoKey key = InstanceBendKey(r);
  const LookupResult found = index_.Lookup(key, hash);
  const auto t1 = Clock::now();
  st.hash_ms += Ms(t0, t1);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end() && it->second.count >= need) {
      ++st.hits;
      *out_count = it->second.count;
      *out_bytes = it->second.bytes;
      *why = LightSecond::kNone;
      return it->second.buffer;
    }
  }
  bend_.resize(s.flat_count);
  nrhi::Buffer* buffer = nullptr;
  uint64_t alloc = 0;
  // All or nothing: a height that is not a number in a vertex the draw names, and it has no buffer.
  const capture::Float4* decoded = MeshPositions(r, mesh, mesh_hash, mesh_count);
  if (decoded) {
    *why = ExpandInstanceBend(decoded, mesh_count, rows, s.rows_size, s, s.flat_count, bend_.data());
    if (*why == LightSecond::kNone) {
      buffer = Upload(dev, bend_.data(), uint64_t(s.flat_count) * sizeof(capture::Float4), &alloc);
      if (!buffer) *why = LightSecond::kUpload;
    }
  }
  if (buffer) {
    Insert(dev, key, hash, {buffer, s.flat_count, 0, alloc}, alloc);
    ++st.uploads;
    *out_count = s.flat_count;
    *out_bytes = alloc;
  }
  st.decode_ms += Ms(t1, Clock::now());
  return buffer;
}

nrhi::Buffer* GeometryCache::Indices(nrhi::Device* dev, const capture::DrawRecord& r,
                                     const capture::TerrainPatch* terrain, uint32_t vertex_count,
                                     uint32_t* index_count, ClayStats& st, uint32_t* out_max_index) {
  const auto t0 = Clock::now();
  const uint8_t* data = nullptr;
  IndexBytes bytes;
  uint64_t hash = 0;
  if (r.indexed && !terrain) {
    if (r.count == 0 || !ReferencedIndexBytes(r, &bytes) ||
        !(data = capture::ReadPhysical(r.ib.phys_addr + bytes.offset, bytes.size))) {
      st.hash_ms += Ms(t0, Clock::now());
      ++st.skipped_other;
      return nullptr;
    }
    hash = XXH3_64bits(data, bytes.size);
  }
  const GeoKey key = IndexKey(r, terrain);
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
    uint32_t max_index = 0, min_index = 0;
    // data already points at the draw's first index; non-indexed draws
    // generate start + i.
    bool built;
    if (terrain) {
      capture::BuildGridIndices(capture::kTerrainGrid, terrain->patches, &indices_);
      max_index = terrain->patches * (capture::kTerrainGrid + 1) * (capture::kTerrainGrid + 1) - 1;
      built = terrain->patches != 0;
    } else {
      const capture::IndexSource src{data, bytes.size, r.index32};
      built = capture::BuildTriangleList(src, r.indexed ? 0 : r.start, r.count, r.prim, indices_,
                                         &max_index, &min_index);
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
        Insert(dev, key, hash, {buffer, uint32_t(indices_.size()), max_index, alloc, min_index}, alloc);
    entry = &entries_[id];
    ++st.uploads;
    st.decode_ms += Ms(t1, Clock::now());
  }
  // The renderer is the producer of kBadIndex for index ranges: the lowest and the highest
  // vertex of the list, since the shader loads every vertex it is given.
  if (!IndexRangeValid(entry->min_index, entry->max_index, r.base_vertex, vertex_count)) {
    ++st.skipped_bad_index;
    return nullptr;
  }
  *index_count = entry->count;
  if (out_max_index) *out_max_index = entry->max_index;
  return entry->buffer;
}


}  // namespace fable2::native::render
