#pragma once

// Albedo material of a captured draw (pure: no SDK/GPU deps): the albedo
// texture's fetch constant, the UV element and a per-draw UV scale/offset
// folded from shader constants (ps-albedo.json, vs-transforms.json "uv").

#include <cstdint>
#include <vector>

#include "uv_decode.h"
#include "vfetch_decode.h"

namespace fable2::native::capture {

struct BufferRef {
  uint32_t phys_addr = 0;
  uint32_t size = 0;
};

enum class MaterialStatus : uint8_t {
  kTextured,           // capture: albedo and UVs resolved; renderer: drawn textured
  kTerrain,
  kPsUnknown,
  kNoAlbedo,
  kUvUnsupported,
  kFormatUnsupported,  // renderer
  kTexturePending,     // renderer
  kTextureDynamic,     // renderer
  kTextureBad,         // renderer
  kCount
};

inline const char* MaterialStatusName(MaterialStatus s) {
  switch (s) {
    case MaterialStatus::kTextured: return "textured";
    case MaterialStatus::kTerrain: return "terrain";
    case MaterialStatus::kPsUnknown: return "ps-unknown";
    case MaterialStatus::kNoAlbedo: return "no-albedo";
    case MaterialStatus::kUvUnsupported: return "uv-unsupported";
    case MaterialStatus::kFormatUnsupported: return "format-unsupported";
    case MaterialStatus::kTexturePending: return "texture-pending";
    case MaterialStatus::kTextureDynamic: return "texture-dynamic";
    case MaterialStatus::kTextureBad: return "texture-bad";
    default: return "?";
  }
}

struct Material {
  MaterialStatus status = MaterialStatus::kPsUnknown;
  uint64_t ps_hash = 0;
  uint32_t fetch[6] = {};  // albedo texture fetch constant, host-order dwords
  BufferRef uv_vb;         // stream holding the UV element
  UvLayout uv;
  float uv_xform[4] = {1.0f, 1.0f, 0.0f, 0.0f};  // u*x + z, v*y + w
};

// Constant reference in a UV transform: (negate << 11) | (bank << 10) | (reg * 4 + comp),
// bank 0 = vertex constants, 1 = pixel constants; kNoRef = identity.
inline constexpr int32_t kNoRef = -1;

struct UvStage {
  int32_t scale = kNoRef;
  int32_t offset = kNoRef;
};

// vs-transforms.json "uv": interpolator `interp` component `comp` is
// vertex fetch `fetch_index` (DecodeVertexFetches order) source component
// `src_comp`, through up to two stages. xenos_format/offset_dwords are what the
// tool saw for that fetch; a mismatch at runtime rejects the entry.
struct VsUvSpec {
  uint64_t vs_hash;
  uint8_t interp, comp;
  int8_t fetch_index;
  uint8_t src_comp;
  uint8_t xenos_format;
  int32_t offset_dwords;
  UvStage stages[2];
};

// ps-albedo.json: the albedo is texture fetch constant `slot`; its u and v
// coordinates are interpolator components through up to two stages each.
// slot -1: the shader samples no albedo.
struct AlbedoSpec {
  uint64_t ps_hash;
  int8_t slot;
  uint8_t u_interp, u_comp, v_interp, v_comp;
  UvStage u_stages[2];
  UvStage v_stages[2];
};

inline float RefValue(int32_t ref, float none, const float* vs_bank, const float* ps_bank) {
  if (ref < 0) return none;
  const float* bank = ((ref >> 10) & 1) ? ps_bank : vs_bank;
  const float v = bank[ref & 0x3FF];
  return ((ref >> 11) & 1) ? -v : v;
}

template <typename F>
inline void ForEachRef(const UvStage* stages, int n, F&& f) {
  for (int i = 0; i < n; ++i) {
    if (stages[i].scale >= 0) f(stages[i].scale);
    if (stages[i].offset >= 0) f(stages[i].offset);
  }
}

// x' = x * scale + offset per stage, VS stages first: folds into one affine map.
inline void ComposeAxis(const UvStage* vs, const UvStage* ps, const float* vs_bank, const float* ps_bank,
                        float* scale, float* offset) {
  float a = 1.0f, b = 0.0f;
  auto apply = [&](const UvStage& st) {
    const float s = RefValue(st.scale, 1.0f, vs_bank, ps_bank);
    const float o = RefValue(st.offset, 0.0f, vs_bank, ps_bank);
    a *= s;
    b = b * s + o;
  };
  for (int i = 0; i < 2; ++i) apply(vs[i]);
  for (int i = 0; i < 2; ++i) apply(ps[i]);
  *scale = a;
  *offset = b;
}

// Both axes must come from the same vertex fetch, which must match the format
// and offset the tool recorded (guards against a different fetch order).
inline bool ResolveUvFetch(const std::vector<VertexFetch>& fetches, const VsUvSpec& u, const VsUvSpec& v,
                           UvLayout* out) {
  if (u.fetch_index != v.fetch_index || u.fetch_index < 0 || size_t(u.fetch_index) >= fetches.size()) return false;
  const VertexFetch& f = fetches[size_t(u.fetch_index)];
  for (const VsUvSpec* s : {&u, &v}) {
    if (f.format != s->xenos_format || f.offset_dwords != s->offset_dwords) return false;
  }
  return UvLayoutFromFetch(f, u.src_comp, v.src_comp, out);
}

}  // namespace fable2::native::capture
