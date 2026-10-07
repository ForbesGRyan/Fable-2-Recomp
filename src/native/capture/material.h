#pragma once

// Albedo material of a captured draw (pure: no SDK/GPU deps): the albedo
// texture's fetch constant, the UV element and a per-draw UV scale/offset
// folded from shader constants (ps-albedo.json, vs-transforms.json "uv").

#include <cmath>
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
  // Renderer, terrain records (clay_logic.h, "Terrain material"): the record's material inputs are
  // not usable (TerrainMaterialInputs::valid is false, or its pixel shader is not a terrain shader
  // of its builder); the record could be drawn with its material but another draw of its ground
  // square could not, so the whole square is flat clay.
  kTerrainInputs,
  kTerrainSquare,
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
    case MaterialStatus::kTerrainInputs: return "terrain-inputs";
    case MaterialStatus::kTerrainSquare: return "terrain-square";
    default: return "?";
  }
}

// Where the pixel shader's output alpha comes from (ps-albedo.json "alpha"): the literal 1, a
// component of the albedo fetch, or a component of another 2D fetch at the same UV. scale_ref
// are up to two pixel constant components the alpha is multiplied by, register * 4 + component
// (no bank or negate bit: always pixel constants), -1 for none. divide_colour: the shader divides
// its colour by the alpha, so the sampled colour is premultiplied by it.
enum class AlphaSource : uint8_t { kNone, kOne, kAlbedo, kFetch };
// The generated table writes the source as 0 one, 1 albedo, 2 fetch (gen_albedo_table.py,
// _ALPHA_SOURCES) and capture.cpp casts it as AlphaSource(code + 1).
static_assert(AlphaSource(0 + 1) == AlphaSource::kOne && AlphaSource(1 + 1) == AlphaSource::kAlbedo &&
              AlphaSource(2 + 1) == AlphaSource::kFetch);

struct AlphaSpec {
  AlphaSource source = AlphaSource::kNone;
  uint8_t slot = 0, component = 3;  // texture fetch constant slot; 0-3 = x-w
  int32_t scale_ref[2] = {-1, -1};
  bool divide_colour = false;
};

// The product of `n` (0 to 2) constants read for an alpha recipe's scale. False, leaving *out
// untouched, if any is not finite or the product overflows.
inline bool ComposeAlphaScale(const float* consts, int n, float* out) {
  float p = 1.0f;
  for (int i = 0; i < n; ++i) {
    if (!std::isfinite(consts[i])) return false;
    p *= consts[i];
  }
  if (!std::isfinite(p)) return false;
  *out = p;
  return true;
}

// ps-albedo.json "kind": what the shader draws. The terrain kinds (FABLE2_PS_TERRAIN, with the
// texture slots of the base map and of the detail or far colour texture) are a blended ground layer,
// the opaque far ground, and the far ground multiplied by the fade the layers leave (the underlay
// the layers add to); frame-map section 13, "Terrain".
enum class MaterialKind : uint8_t { kPlain, kTerrainLayer, kTerrainFar, kTerrainFarFade };
// The generated table writes the kind as 1 terrain-layer, 2 terrain-far, 3 terrain-far-fade
// (gen_albedo_table.py, _KINDS; 0 plain has no line) and capture.cpp casts it as MaterialKind(code).
static_assert(MaterialKind(0) == MaterialKind::kPlain && MaterialKind(1) == MaterialKind::kTerrainLayer &&
              MaterialKind(2) == MaterialKind::kTerrainFar && MaterialKind(3) == MaterialKind::kTerrainFarFade);

struct Material {
  MaterialStatus status = MaterialStatus::kPsUnknown;
  uint64_t ps_hash = 0;
  uint32_t fetch[6] = {};  // albedo texture fetch constant, host-order dwords
  BufferRef uv_vb;         // stream holding the UV element
  UvLayout uv;
  float uv_xform[4] = {1.0f, 1.0f, 0.0f, 0.0f};  // u*x + z, v*y + w
  AlphaSpec alpha;                               // kNone: no recipe
  float alpha_scale = 1.0f;                      // the scale constant's value at the draw; 1 without one
  MaterialKind kind = MaterialKind::kPlain;
};

// What the pass does with a draw's alpha, from the captured render state and the material:
// `alpha_test_or_mask` is alpha test or alpha-to-mask on, `colour_known` is that the draw's colour
// is the shader's own (a textured material), `has_recipe` is alpha.source != kNone.
enum class AlphaRule : uint8_t {
  kOpaque,             // neither test nor blend: alpha is ignored
  kApply,              // apply the test and/or the blend with the recipe's alpha
  kDrawAlphaUnknown,   // test or blend on without a recipe: drawn without the test / with alpha 1
  kSkipBlendUnknown    // blended and the colour unknown: not drawn
};

inline AlphaRule DecideAlpha(bool alpha_test_or_mask, bool blend, bool colour_known, bool has_recipe) {
  if (!alpha_test_or_mask && !blend) return AlphaRule::kOpaque;
  if (blend && !colour_known) return AlphaRule::kSkipBlendUnknown;
  return has_recipe ? AlphaRule::kApply : AlphaRule::kDrawAlphaUnknown;
}

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

// Each axis's vertex fetch must match the format and offset the tool recorded
// (guards against a different fetch order). The axes come from one fetch, or
// from two fetches of one stream with the same element format
// (UvLayoutFromFetches).
inline bool ResolveUvFetch(const std::vector<VertexFetch>& fetches, const VsUvSpec& u, const VsUvSpec& v,
                           UvLayout* out) {
  for (const VsUvSpec* s : {&u, &v}) {
    if (s->fetch_index < 0 || size_t(s->fetch_index) >= fetches.size()) return false;
    const VertexFetch& f = fetches[size_t(s->fetch_index)];
    if (f.format != s->xenos_format || f.offset_dwords != s->offset_dwords) return false;
  }
  return UvLayoutFromFetches(fetches[size_t(u.fetch_index)], fetches[size_t(v.fetch_index)], u.src_comp,
                             v.src_comp, out);
}

}  // namespace fable2::native::capture
