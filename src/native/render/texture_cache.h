#pragma once

// Albedo textures on the GPU for the clay pass: a 2D Xenos fetch constant's
// guest levels (base, mips, packed mip tail) untiled into an upload staging
// buffer and copied into a sampled texture (DXT -> BC1/2/3, 8_8_8_8 -> RGBA8,
// fetch swizzle applied through the view). Keyed by GeoKey (base address, base
// extent, format, width, fetch identity); a sparse sample hash per frame
// detects changed contents (texture_residency.h), re-uploads are rate-limited
// by a per-frame budget of guest bytes, and memory is an LRU byte budget
// (GeometryCacheIndex). Any RHI failure latches the texture path off.
// GPU (command-processor) thread only.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <rex/graphics/native_rhi.h>

#include "../capture/material.h"
#include "clay_logic.h"  // TextureStats
#include "geometry_cache_index.h"
#include "texture_decode.h"
#include "texture_residency.h"

namespace fable2::native::render {

namespace nrhi = rex::graphics::nrhi;

class TextureCache {
 public:
  explicit TextureCache(uint64_t budget_bytes);

  // frame: the caller's frame counter (monotonic; consecutive frames differ by 1).
  void BeginFrame(uint64_t frame, uint64_t budget_bytes, uint64_t upload_bytes_per_frame);
  // Resolves (uploading if needed and allowed) the albedo of `m`. Returns the
  // view and sampler index, or nullptr with *status set (format-unsupported,
  // texture-pending, texture-dynamic, texture-bad). Records copy commands and
  // barriers on `cmd`: call before render targets are bound.
  // uv_fix[2] receives (w / host_w, h / host_h): BC textures are created with
  // their base size rounded up to a multiple of 4.
  // Updates st.uploads/upload_bytes/mirror/base_only/decode_ms and
  // st.resident/resident_bytes; textured and status[] are the caller's.
  nrhi::TextureView* Resolve(nrhi::Cmd* cmd, nrhi::Device* dev, const capture::Material& m,
                             uint32_t* sampler, float* uv_fix, capture::MaterialStatus* status,
                             TextureStats& st);
  // DestroyDeferred all (dev may be nullptr: forget the objects without
  // destroying them, e.g. after a device change). The latch stays set.
  void Release(nrhi::Device* dev);
  bool latched() const { return latched_; }

  uint64_t resident_bytes() const { return index_.resident_bytes(); }

 private:
  static constexpr uint32_t kMaxLevels = 16;
  // Replaced/evicted textures kept for reuse by a texture of the same shape.
  static constexpr uint64_t kPoolBudgetBytes = 64ull << 20;
  // Change-tracking states not observed for this many frames are dropped.
  static constexpr uint64_t kStateTtlFrames = 600;

  // Where one host level comes from: the base region (base address) or the
  // mip region (mip address), and its storage layout inside that region.
  struct LevelSource {
    bool from_mips = false;
    LevelLayout layout;  // offset = byte offset inside the region
  };
  struct Plan {
    uint32_t levels = 0;
    uint32_t base_extent = 0;  // guest bytes read from the base address
    uint32_t mip_extent = 0;   // guest bytes read from the mip address (0 = none)
    bool base_only = false;    // mip layout failed: base level only
    LevelSource src[kMaxLevels];
  };
  struct Entry {
    nrhi::Texture* tex = nullptr;
    nrhi::TextureView* view = nullptr;
    uint64_t bytes = 0;  // host texture bytes
    float uv_fix[2] = {1.0f, 1.0f};
  };

  nrhi::TextureView* ResolveImpl(nrhi::Cmd* cmd, nrhi::Device* dev, const capture::Material& m,
                                 uint32_t* sampler, float* uv_fix, capture::MaterialStatus* status,
                                 TextureStats& st);
  static bool BuildPlan(const TextureFetch& t, Plan* plan);
  nrhi::TextureView* Upload(nrhi::Cmd* cmd, nrhi::Device* dev, const TextureFetch& t,
                            const Plan& plan, const uint8_t* base, const uint8_t* mips,
                            const GeoKey& key, uint64_t hash, float* uv_fix,
                            capture::MaterialStatus* status);
  nrhi::Texture* TakeRetired(nrhi::Device* dev, const nrhi::TextureDesc& desc, uint64_t bytes);
  void Retire(nrhi::Device* dev, nrhi::Texture* tex, uint64_t bytes);
  void Latch(const char* what);

  GeometryCacheIndex index_;
  UploadBudget upload_{0};
  RetirePool<nrhi::Texture*> pool_{kPoolBudgetBytes};
  std::unordered_map<uint32_t, Entry> entries_;
  std::unordered_map<GeoKey, TextureState, GeoKeyHash> states_;
  std::vector<nrhi::Texture*> destroy_;
  std::vector<uint32_t> evicted_;
  std::vector<uint8_t> scratch_;  // untiled levels, copied into the staging buffer
  uint64_t frame_ = 1;            // caller frame + 1 (NoteSample: 0 = never observed)
  bool latched_ = false;
};

}  // namespace fable2::native::render
