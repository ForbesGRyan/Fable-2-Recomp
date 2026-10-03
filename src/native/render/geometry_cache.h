#pragma once

// Decoded guest geometry on the GPU for the clay pass: float4 positions,
// float2 UVs and uint32 triangle-list indices in persistently mapped upload-heap buffers,
// read by the vertex shader as StructuredBuffers (the RHI has 16-bit index
// buffers only and no buffer-to-buffer copy). Keyed by GeometryCacheIndex
// (guest range + layout + content hash) with an LRU byte budget.
// GPU (command-processor) thread only.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <rex/graphics/native_rhi.h>

#include "../capture/draw_record.h"
#include "clay_logic.h"
#include "geometry_cache_index.h"

namespace fable2::native::render {

namespace nrhi = rex::graphics::nrhi;

class GeometryCache {
 public:
  explicit GeometryCache(uint64_t budget_bytes);

  // Returns the GPU buffer holding float4 positions / uint32 indices, or
  // nullptr (draw skipped). *vertex_count receives the number of positions
  // the returned buffer actually holds.
  nrhi::Buffer* Positions(nrhi::Device* dev, const capture::DrawRecord& r, uint32_t* vertex_count,
                          ClayStats& st);
  nrhi::Buffer* Indices(nrhi::Device* dev, const capture::DrawRecord& r, uint32_t vertex_count,
                        uint32_t* index_count, ClayStats& st);
  // float2 UVs of the record's material (material.uv_vb / material.uv), one
  // per vertex of the UV stream; nullptr when the stream is unreadable, does
  // not decode or holds fewer than `vertex_count` UVs (the shader reads
  // uvs[v] for every v < vertex_count). Reads are bounded by uv_vb.size.
  nrhi::Buffer* Uvs(nrhi::Device* dev, const capture::DrawRecord& r, uint32_t vertex_count,
                    ClayStats& st);
  void BeginFrame(uint64_t frame, uint64_t budget_bytes);
  // DestroyDeferred all (dev may be nullptr: forget the buffers without
  // destroying them, e.g. after a device change).
  void Release(nrhi::Device* dev);

  uint64_t resident_bytes() const { return index_.resident_bytes(); }

 private:
  struct Entry {
    nrhi::Buffer* buffer = nullptr;
    uint32_t count = 0;      // vertices or indices
    uint32_t max_index = 0;  // indices only
    uint64_t bytes = 0;      // allocation size
  };
  // Replaced buffers, reused by Upload (animated skinned streams re-decode
  // every frame; creating a buffer costs far more than filling one).
  static constexpr uint64_t kPoolBudgetBytes = 32ull << 20;

  nrhi::Buffer* Upload(nrhi::Device* dev, const void* data, uint64_t bytes, uint64_t* alloc_bytes);
  // Content hash of a guest range, computed once per frame.
  uint64_t FrameHash(uint32_t phys_addr, uint32_t size, const uint8_t* data);
  nrhi::Buffer* TerrainPositions(nrhi::Device* dev, const capture::DrawRecord& r,
                                 uint32_t* vertex_count, ClayStats& st);
  uint32_t Insert(nrhi::Device* dev, const GeoKey& key, uint64_t hash, const Entry& e,
                  uint64_t bytes);

  void Retire(nrhi::Device* dev, nrhi::Buffer* buffer, uint64_t bytes);

  GeometryCacheIndex index_;
  RetirePool<nrhi::Buffer*> pool_{kPoolBudgetBytes};
  std::vector<nrhi::Buffer*> destroy_;
  std::unordered_map<uint32_t, Entry> entries_;
  // Content hash per guest range, computed once per frame (many draws share
  // one vertex stream).
  std::unordered_map<uint64_t, uint64_t> frame_hashes_;
  std::vector<capture::Float4> positions_;
  std::vector<capture::Float2> uvs_;
  std::vector<uint32_t> indices_;
  std::vector<uint32_t> evicted_;
};

}  // namespace fable2::native::render
