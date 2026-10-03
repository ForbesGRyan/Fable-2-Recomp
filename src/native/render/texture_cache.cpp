#include "texture_cache.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iterator>

#include <rex/graphics/pipeline/texture/util.h>
#include <rex/logging.h>

#include "../capture/guest_read.h"

namespace fable2::native::render {

namespace {

namespace texture_util = rex::graphics::texture_util;
namespace xenos = rex::graphics::xenos;
using capture::MaterialStatus;
using Clock = std::chrono::steady_clock;

double Ms(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

// D3D12 placed-footprint offsets must be 512-byte aligned.
constexpr uint64_t kCopyOffsetAlignment = 512;
// Guest bytes per region (base or mips) above which a fetch constant is taken
// as garbage (texture-bad / base only). The D4 census's largest albedo is
// 1024x1024 DXT1 (512 KB); a 2048x2048 8_8_8_8 base (16 MB) and a 4096x4096
// one (64 MB) still fit.
constexpr uint64_t kMaxRegionBytes = 64ull << 20;

uint64_t AlignUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

nrhi::Format HostFormat(TexFormat f) {
  switch (f) {
    case TexFormat::kDxt1: return nrhi::Format::kBC1_UNORM;
    case TexFormat::kDxt23: return nrhi::Format::kBC2_UNORM;
    case TexFormat::kDxt45: return nrhi::Format::kBC3_UNORM;
    case TexFormat::k8888: return nrhi::Format::kR8G8B8A8_UNORM;
    default: return nrhi::Format::kUnknown;
  }
}

nrhi::Swizzle HostSwizzle(uint8_t s) {
  switch (s) {
    case 0: return nrhi::Swizzle::kX;
    case 1: return nrhi::Swizzle::kY;
    case 2: return nrhi::Swizzle::kZ;
    case 3: return nrhi::Swizzle::kW;
    case 5: return nrhi::Swizzle::kOne;
    default: return nrhi::Swizzle::kZero;
  }
}

// One host level: size in blocks, staging row pitch and offset.
struct HostLevel {
  uint32_t blocks_w, blocks_h, row_pitch;
  uint64_t offset;
};

}  // namespace

TextureCache::TextureCache(uint64_t budget_bytes) : index_(budget_bytes) {}

void TextureCache::BeginFrame(uint64_t frame, uint64_t budget_bytes, uint64_t upload_bytes_per_frame) {
  frame_ = frame + 1;
  index_.set_budget_bytes(budget_bytes);
  index_.BeginFrame(frame_);
  upload_.BeginFrame(upload_bytes_per_frame);
  if (frame_ % kStateTtlFrames == 0) {
    for (auto it = states_.begin(); it != states_.end();) {
      it = frame_ - it->second.last_frame > kStateTtlFrames ? states_.erase(it) : std::next(it);
    }
  }
}

void TextureCache::Release(nrhi::Device* dev) {
  destroy_.clear();
  pool_.Drain(&destroy_);
  if (dev) {
    for (auto& [id, e] : entries_) {
      dev->DestroyDeferred(e.view);
      dev->DestroyDeferred(e.tex);
    }
    for (nrhi::Texture* t : destroy_) dev->DestroyDeferred(t);
  }
  destroy_.clear();
  entries_.clear();
  states_.clear();
  scratch_ = {};
  index_ = GeometryCacheIndex(index_.budget_bytes());
}

void TextureCache::Latch(const char* what) {
  if (!latched_) REXLOG_ERROR("[native] textures: {} failed, texture path off", what);
  latched_ = true;
}

void TextureCache::Retire(nrhi::Device* dev, nrhi::Texture* tex, uint64_t bytes) {
  destroy_.clear();
  pool_.Retire(tex, bytes, dev->CurrentSubmission(), &destroy_);
  for (nrhi::Texture* t : destroy_) dev->DestroyDeferred(t);
  destroy_.clear();
}

// A retired texture of the same shape whose last use has completed. The pool
// matches by byte size only; for the same width, height and format the byte
// size also fixes the mip count, so a size match with the same width, height
// and format is the same shape. Same-size textures of another shape are put
// back (they are completed, so submission 0).
nrhi::Texture* TextureCache::TakeRetired(nrhi::Device* dev, const nrhi::TextureDesc& desc,
                                         uint64_t bytes) {
  nrhi::Texture* found = nullptr;
  nrhi::Texture* other[4];
  uint32_t other_count = 0;
  while (other_count < 4) {
    auto t = pool_.Take(bytes, dev->CompletedSubmission());
    if (!t) break;
    if ((*t)->width() == desc.width && (*t)->height() == desc.height && (*t)->format() == desc.format) {
      found = *t;
      break;
    }
    other[other_count++] = *t;
  }
  destroy_.clear();
  for (uint32_t i = 0; i < other_count; ++i) pool_.Retire(other[i], bytes, 0, &destroy_);
  for (nrhi::Texture* t : destroy_) dev->DestroyDeferred(t);
  destroy_.clear();
  return found;
}

// Guest storage of every level to upload. Level 0 is read from the base
// address with the base layout; levels 1+ from the mip address with the mip
// layouts. Levels at or past the packed level live in the packed tail stored
// like the packed level (layout.base for level 0 when the whole texture is a
// tail, mips[packed] otherwise), at the block offset GetPackedMipOffset gives.
bool TextureCache::BuildPlan(const TextureFetch& t, Plan* plan) {
  const TexFormatInfo fi = FormatInfo(t.format);
  const uint32_t levels = std::min(LevelCount(t), kMaxLevels);
  const xenos::TextureFormat format = xenos::TextureFormat(t.xenos_format);
  const texture_util::TextureGuestLayout layout = texture_util::GetGuestTextureLayout(
      xenos::DataDimension::k2DOrStacked, t.pitch_texels >> 5, t.width, t.height, 1, t.tiled, format,
      t.packed_mips, true, levels - 1);
  const bool has_packed = layout.packed_level != UINT32_MAX;

  // Guest bytes a storage level may touch (0 = layout failed). Linear row
  // pitches are the SDK's (GetGuestTextureLayout): the base level's is the
  // fetch pitch aligned to 32 blocks, the mips' are also 256-byte aligned.
  auto storage = [&](const texture_util::TextureGuestLayout::Level& s, LevelLayout* l) -> uint64_t {
    if (!s.row_pitch_bytes || !s.x_extent_blocks || !s.y_extent_blocks) return 0;
    if (t.tiled) {
      l->row_pitch_bytes = s.row_pitch_bytes;
      l->pitch_blocks = s.row_pitch_bytes >> fi.bpb_log2;
      return texture_util::GetTiledAddressUpperBound2D(s.x_extent_blocks, s.y_extent_blocks,
                                                       l->pitch_blocks, fi.bpb_log2);
    }
    l->row_pitch_bytes = s.row_pitch_bytes;
    l->pitch_blocks = l->row_pitch_bytes >> fi.bpb_log2;
    return uint64_t(l->row_pitch_bytes) * (s.y_extent_blocks - 1) +
           uint64_t(s.x_extent_blocks) * fi.bytes_per_block;
  };

  Plan p;
  p.levels = levels;
  uint64_t mip_extent = 0;
  for (uint32_t lv = 0; lv < levels; ++lv) {
    LevelSource& src = p.src[lv];
    src.from_mips = lv != 0;
    const bool packed = has_packed && lv >= layout.packed_level;
    const uint32_t storage_level = packed ? layout.packed_level : lv;
    const auto& s = lv == 0 ? layout.base : layout.mips[storage_level];
    src.layout.offset = lv == 0 ? 0 : layout.mip_offsets_bytes[storage_level];
    src.layout.width_blocks = (LevelExtent(t.width, lv) + fi.block - 1) / fi.block;
    src.layout.height_blocks = (LevelExtent(t.height, lv) + fi.block - 1) / fi.block;
    if (packed) {
      uint32_t z = 0;
      texture_util::GetPackedMipOffset(t.width, t.height, 1, format, lv, src.layout.x_blocks,
                                       src.layout.y_blocks, z);
    }
    const uint64_t extent = storage(s, &src.layout);
    if (lv == 0) {
      if (extent == 0 || extent > kMaxRegionBytes) return false;
      p.base_extent = uint32_t(extent);
      continue;
    }
    if (extent == 0) {
      p.base_only = true;
      break;
    }
    mip_extent = std::max(mip_extent, uint64_t(src.layout.offset) + extent);
  }
  if (mip_extent > kMaxRegionBytes) p.base_only = true;
  if (p.base_only) {
    p.levels = 1;
    mip_extent = 0;
  }
  p.mip_extent = uint32_t(mip_extent);
  *plan = p;
  return true;
}

nrhi::TextureView* TextureCache::Resolve(nrhi::Cmd* cmd, nrhi::Device* dev,
                                         const capture::Material& m, uint32_t* sampler,
                                         float* uv_fix, MaterialStatus* status, TextureStats& st) {
  const auto t0 = Clock::now();
  nrhi::TextureView* view = ResolveImpl(cmd, dev, m, sampler, uv_fix, status, st);
  st.decode_ms += Ms(t0, Clock::now());
  st.resident = uint32_t(entries_.size());
  st.resident_bytes = index_.resident_bytes();
  return view;
}

nrhi::TextureView* TextureCache::ResolveImpl(nrhi::Cmd* cmd, nrhi::Device* dev,
                                             const capture::Material& m, uint32_t* sampler,
                                             float* uv_fix, MaterialStatus* status,
                                             TextureStats& st) {
  auto fail = [status](MaterialStatus s) -> nrhi::TextureView* {
    *status = s;
    return nullptr;
  };
  if (latched_) return fail(MaterialStatus::kTextureBad);

  TextureFetch t;
  switch (DecodeTextureFetch(m.fetch, &t)) {
    case FetchError::kNone: break;
    case FetchError::kFormat:
    case FetchError::kNot2D: return fail(MaterialStatus::kFormatUnsupported);
    default: return fail(MaterialStatus::kTextureBad);
  }

  Plan plan;
  if (!BuildPlan(t, &plan)) return fail(MaterialStatus::kTextureBad);
  if (plan.base_only) ++st.base_only;

  const GeoKey key{t.base_phys, plan.base_extent, t.xenos_format | (t.width << 8),
                   uint32_t(TextureIdentity(m.fetch)), 2};

  // Guest bytes, bounds-checked against the physical heap. An unreadable mip
  // region falls back to the base level only (like a failed mip layout).
  const uint8_t* base = nullptr;
  const uint8_t* mips = nullptr;
  auto read = [&]() {
    base = capture::ReadPhysical(t.base_phys, plan.base_extent);
    if (!base) return false;
    if (plan.mip_extent) {
      mips = capture::ReadPhysical(t.mip_phys, plan.mip_extent);
      if (!mips) {
        plan.levels = 1;
        plan.mip_extent = 0;
        plan.base_only = true;
        ++st.base_only;
      }
    }
    return true;
  };

  // The sample hash is taken once per frame per texture (many draws share one).
  TextureState& state = states_[key];
  if (state.last_frame != frame_) {
    if (!read()) return fail(MaterialStatus::kTextureBad);
    uint64_t hash = SampleHash(base, plan.base_extent);
    if (mips) hash ^= SampleHash(mips, plan.mip_extent) * 0x9E3779B97F4A7C15ull;
    NoteSample(state, hash, frame_);
  }
  if (state.dynamic) return fail(MaterialStatus::kTextureDynamic);
  if (KnownBad(state)) return fail(MaterialStatus::kTextureBad);  // these contents failed to decode

  nrhi::TextureView* view = nullptr;
  const LookupResult found = index_.Lookup(key, state.sample_hash);
  if (found.hit) {
    if (auto it = entries_.find(found.id); it != entries_.end()) {
      view = it->second.view;
      uv_fix[0] = it->second.uv_fix[0];
      uv_fix[1] = it->second.uv_fix[1];
    }
  }
  if (!view) {
    // Changed, new or evicted.
    if (!base && !read()) return fail(MaterialStatus::kTextureBad);
    const uint64_t guest_bytes = uint64_t(plan.base_extent) + plan.mip_extent;
    if (!upload_.TryTake(guest_bytes)) return fail(MaterialStatus::kTexturePending);
    view = Upload(cmd, dev, t, plan, base, mips, key, state.sample_hash, uv_fix, status);
    if (!view) {
      // Upload set *status. A decode failure is not retried (nor charged to
      // the upload budget again) until the contents change; RHI failures latch.
      if (!latched_) MarkBad(state);
      return nullptr;
    }
    ++st.uploads;
    st.upload_bytes += guest_bytes;
  }

  *sampler = SamplerIndex(t);
  if (IsMirror(t.clamp_x) || IsMirror(t.clamp_y)) ++st.mirror;
  *status = MaterialStatus::kTextured;
  return view;
}

nrhi::TextureView* TextureCache::Upload(nrhi::Cmd* cmd, nrhi::Device* dev, const TextureFetch& t,
                                        const Plan& plan, const uint8_t* base, const uint8_t* mips,
                                        const GeoKey& key, uint64_t hash, float* uv_fix,
                                        MaterialStatus* status) {
  const TexFormatInfo fi = FormatInfo(t.format);
  const uint32_t host_w = (t.width + fi.block - 1) / fi.block * fi.block;
  const uint32_t host_h = (t.height + fi.block - 1) / fi.block * fi.block;

  // Staging layout: each level at a 512-byte aligned offset, rows of
  // UploadRowPitch bytes, host-sized (guest blocks beyond the host size are
  // dropped, host blocks beyond the guest size stay zero).
  HostLevel host[kMaxLevels];
  uint64_t staging_bytes = 0, tex_bytes = 0;
  for (uint32_t lv = 0; lv < plan.levels; ++lv) {
    HostLevel& h = host[lv];
    h.blocks_w = (LevelExtent(host_w, lv) + fi.block - 1) / fi.block;
    h.blocks_h = (LevelExtent(host_h, lv) + fi.block - 1) / fi.block;
    h.row_pitch = UploadRowPitch(h.blocks_w, fi.bytes_per_block);
    h.offset = AlignUp(staging_bytes, kCopyOffsetAlignment);
    staging_bytes = h.offset + uint64_t(h.row_pitch) * h.blocks_h;
    tex_bytes += uint64_t(h.blocks_w) * h.blocks_h * fi.bytes_per_block;
  }

  scratch_.assign(size_t(staging_bytes), 0);
  for (uint32_t lv = 0; lv < plan.levels; ++lv) {
    LevelLayout l = plan.src[lv].layout;
    l.width_blocks = std::min(l.width_blocks, host[lv].blocks_w);
    l.height_blocks = std::min(l.height_blocks, host[lv].blocks_h);
    const bool from_mips = plan.src[lv].from_mips;
    if (!UntileLevel(from_mips ? mips : base, from_mips ? plan.mip_extent : plan.base_extent, l, fi,
                     t.tiled, t.endian, scratch_.data() + host[lv].offset, host[lv].row_pitch)) {
      *status = MaterialStatus::kTextureBad;
      return nullptr;
    }
  }

  auto latch = [&](const char* what) -> nrhi::TextureView* {
    Latch(what);
    *status = MaterialStatus::kTextureBad;
    return nullptr;
  };

  nrhi::BufferDesc bd;
  bd.size = staging_bytes;
  bd.heap = nrhi::HeapKind::kUpload;
  bd.bind_class = nrhi::BufferBindClass::kCopySrc;
  nrhi::Buffer* staging = dev->CreateBuffer(bd);
  if (!staging) return latch("staging buffer creation");
  void* mapped = dev->Map(staging);
  if (!mapped) {
    dev->DestroyDeferred(staging);
    return latch("staging buffer map");
  }
  std::memcpy(mapped, scratch_.data(), size_t(staging_bytes));
  dev->Unmap(staging);
  if (scratch_.capacity() > (32u << 20)) scratch_ = {};

  nrhi::TextureDesc desc;
  desc.kind = nrhi::TextureKind::k2D;
  desc.width = host_w;
  desc.height = host_h;
  desc.mip_levels = plan.levels;
  desc.format = HostFormat(t.format);
  desc.initial_state = nrhi::ResourceState::kCopyDest;
  nrhi::Texture* tex = TakeRetired(dev, desc, tex_bytes);
  const bool reused = tex != nullptr;
  if (!tex) tex = dev->CreateTexture(desc);
  if (!tex) {
    dev->DestroyDeferred(staging);
    return latch("texture creation");
  }

  nrhi::TextureViewDesc vd;
  vd.dimension = nrhi::ViewDimension::k2D;
  vd.format = nrhi::Format::kUnknown;
  vd.base_mip = 0;
  vd.mip_levels = ~0u;
  for (int i = 0; i < 4; ++i) vd.swizzle[i] = HostSwizzle(t.swizzle[i]);
  nrhi::TextureView* view = dev->CreateTextureView(tex, vd);
  if (!view) {
    dev->DestroyDeferred(staging);
    dev->DestroyDeferred(tex);
    return latch("texture view creation");
  }

  if (reused) cmd->Barrier(tex, nrhi::ResourceState::kPixelShaderResource, nrhi::ResourceState::kCopyDest);
  cmd->FlushBarriers();
  for (uint32_t lv = 0; lv < plan.levels; ++lv) {
    cmd->CopyBufferToTexture(tex, lv, 0, staging, host[lv].offset, host[lv].row_pitch,
                             host[lv].blocks_w * fi.block, host[lv].blocks_h * fi.block, 1);
  }
  cmd->Barrier(tex, nrhi::ResourceState::kCopyDest, nrhi::ResourceState::kPixelShaderResource);
  cmd->FlushBarriers();
  dev->DestroyDeferred(staging);

  // Replaces this key's previous texture (if any) and evicts LRU entries not
  // used this frame; their views die with the current submission and their
  // textures go to the reuse pool.
  evicted_.clear();
  const uint32_t id = index_.Insert(key, hash, tex_bytes, &evicted_);
  for (uint32_t old : evicted_) {
    auto it = entries_.find(old);
    if (it == entries_.end()) continue;
    dev->DestroyDeferred(it->second.view);
    Retire(dev, it->second.tex, it->second.bytes);
    entries_.erase(it);
  }
  Entry& e = entries_[id];
  e.tex = tex;
  e.view = view;
  e.bytes = tex_bytes;
  e.uv_fix[0] = float(t.width) / float(host_w);
  e.uv_fix[1] = float(t.height) / float(host_h);
  uv_fix[0] = e.uv_fix[0];
  uv_fix[1] = e.uv_fix[1];
  return view;
}

}  // namespace fable2::native::render
