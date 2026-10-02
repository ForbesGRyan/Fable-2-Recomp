#pragma once

// Decoded guest geometry on the GPU for the clay pass: float4 positions and
// uint32 triangle-list indices in persistently mapped upload-heap buffers,
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
  };

  nrhi::Buffer* Upload(nrhi::Device* dev, const void* data, uint64_t bytes, uint64_t* alloc_bytes);
  uint32_t Insert(nrhi::Device* dev, const GeoKey& key, uint64_t hash, const Entry& e,
                  uint64_t bytes);

  GeometryCacheIndex index_;
  std::unordered_map<uint32_t, Entry> entries_;
  // Content hash per guest range, computed once per frame (many draws share
  // one vertex stream).
  std::unordered_map<uint64_t, uint64_t> frame_hashes_;
  std::vector<capture::Float4> positions_;
  std::vector<uint32_t> indices_;
  std::vector<uint32_t> evicted_;
};

}  // namespace fable2::native::render
