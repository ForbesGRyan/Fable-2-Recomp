#pragma once

// Albedo material of a captured draw (pure: no SDK/GPU deps): the albedo
// texture's fetch constant, the UV element and a per-draw UV scale/offset
// folded from shader constants (ps-albedo.json, vs-transforms.json "uv").
// And the light inputs of a draw (the two tables' "light"): LightInputs,
// MakeLightInputs, at the end.

#include <cmath>
#include <cstddef>
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

// --- Light inputs (frame-map section 14; docs/native-renderer/lighting-formulas.md) ----------------
// What the light of one draw needs beyond the frame's light block (light_block.h): its family, and
// what its family reads per draw. Recorded by the capture into a side array of the frame
// (FrameScene::lights, DrawRecord::light_index); nothing is drawn from it by the capture.

// The family of a draw's pixel shader with its vertex shader (ps-albedo.json "light"): the closed
// formula the pair was checked against by tools/xdk_sigmatch/light_check.py, whose names these are.
// kTerrainLayer / kTerrainFar: terrain_layer / terrain_far. kObject, kTreeTrunk, kPlantLit:
// lit_object (props; trunks; instanced plants with a normal map). kPlant: plant. A family's two- and
// four-light form ("-multi") is the same value with LightInputs::multi set. kTreeLeaf (plan Task
// 8c) has no such form: a leaf's lights are its vertex shader's, and its pixel shader is one
// program for one, two and four of them.
enum class LightFamily : uint8_t {
  kNone, kTerrainLayer, kTerrainFar, kObject, kTreeTrunk, kPlant, kPlantLit, kTreeLeaf, kCount
};
// The generated table writes the family as 1 terrain-layer, 2 terrain-far, 3 object, 4 tree-trunk,
// 5 plant, 6 plant-lit, 7 tree-leaf (gen_albedo_table.py, LIGHT_FAMILIES) and capture.cpp casts it
// as LightFamily(code).
static_assert(LightFamily(1) == LightFamily::kTerrainLayer && LightFamily(2) == LightFamily::kTerrainFar &&
              LightFamily(3) == LightFamily::kObject && LightFamily(4) == LightFamily::kTreeTrunk &&
              LightFamily(5) == LightFamily::kPlant && LightFamily(6) == LightFamily::kPlantLit &&
              LightFamily(7) == LightFamily::kTreeLeaf && size_t(LightFamily::kCount) == 8);

// The light family of a pixel shader by the generated table alone (ps_albedo_table.inc, the
// FABLE2_PS_LIGHT lines; capture.cpp has the table's whole rows and the pairs a draw's family also
// needs): which of the game's forms a pixel shader is, whether or not the draw got light inputs.
// The pass asks it for the form of a draw's fog (render/clay_logic.h, DrawFogOf).
struct PsLightFamilyEntry {
  uint64_t ps_hash;
  LightFamily family;
};
inline constexpr PsLightFamilyEntry kPsLightFamilies[] = {
#define FABLE2_PS_ALBEDO(...)
#define FABLE2_PS_NO_ALBEDO(...)
#define FABLE2_PS_ALPHA(...)
#define FABLE2_PS_TERRAIN(...)
#define FABLE2_PS_LIGHT(H, F, M, FS) {H, LightFamily(F)},
#define FABLE2_PS_LIGHT_VS(...)
#include "ps_albedo_table.inc"
#undef FABLE2_PS_ALBEDO
#undef FABLE2_PS_NO_ALBEDO
#undef FABLE2_PS_ALPHA
#undef FABLE2_PS_TERRAIN
#undef FABLE2_PS_LIGHT
#undef FABLE2_PS_LIGHT_VS
};
// kNone for a shader without an entry.
constexpr LightFamily PsLightFamily(uint64_t ps_hash) {
  for (const PsLightFamilyEntry& e : kPsLightFamilies) {
    if (e.ps_hash == ps_hash) return e.family;
  }
  return LightFamily::kNone;
}

inline const char* LightFamilyName(LightFamily f) {
  switch (f) {
    case LightFamily::kNone: return "none";
    case LightFamily::kTerrainLayer: return "terrain-layer";
    case LightFamily::kTerrainFar: return "terrain-far";
    case LightFamily::kObject: return "object";
    case LightFamily::kTreeTrunk: return "tree-trunk";
    case LightFamily::kPlant: return "plant";
    case LightFamily::kPlantLit: return "plant-lit";
    case LightFamily::kTreeLeaf: return "tree-leaf";
    default: return "?";
  }
}

// Where a vertex shader takes what the light reads per vertex (vs-transforms.json "light"):
// kTerrain: nothing from a stream (the normal is the heightmap's). kStream: a colour stream of its
// own beside the normal. kProbe: four weights per vertex, combined with per-draw constants.
// kInstance: the instance's colour (and the mesh's normal through the instance's rows).
enum class LightVertexKind : uint8_t { kNone, kTerrain, kStream, kProbe, kInstance };
// The generated table writes the kind as 1 terrain, 2 stream, 3 probe, 4 instance
// (gen_transform_table.py, LIGHT_KINDS) and capture.cpp casts it as LightVertexKind(code).
static_assert(LightVertexKind(1) == LightVertexKind::kTerrain && LightVertexKind(2) == LightVertexKind::kStream &&
              LightVertexKind(3) == LightVertexKind::kProbe && LightVertexKind(4) == LightVertexKind::kInstance);

inline const char* LightVertexKindName(LightVertexKind k) {
  switch (k) {
    case LightVertexKind::kNone: return "none";
    case LightVertexKind::kTerrain: return "terrain";
    case LightVertexKind::kStream: return "stream";
    case LightVertexKind::kProbe: return "probe";
    case LightVertexKind::kInstance: return "instance";
    default: return "?";
  }
}

// Whether a family can stand on a vertex side of this kind (the forms the checked pairs have).
inline bool LightKindFits(LightFamily f, LightVertexKind k) {
  switch (f) {
    case LightFamily::kTerrainLayer:
    case LightFamily::kTerrainFar: return k == LightVertexKind::kTerrain;
    case LightFamily::kObject: return k == LightVertexKind::kStream || k == LightVertexKind::kProbe;
    case LightFamily::kTreeTrunk:
    case LightFamily::kTreeLeaf: return k == LightVertexKind::kProbe;
    case LightFamily::kPlant:
    case LightFamily::kPlantLit: return k == LightVertexKind::kInstance;
    default: return false;
  }
}

// Whether a family's PIXEL shader reads the light set's first light (c80, c87): at a draw of such a
// family, in its one-light form, those registers hold the frame's sun, and the capture may read the
// frame's light block there (capture.cpp, RecordDraw; light_block.h). Every family but the leaves:
// their pixel shader 0x7CD5... reads no light register at all (the leaf's light is computed by its
// vertex shader from the vertex side's copy of the set, c76.. / c83..), so what the pixel registers
// hold at a leaf draw was never held against the GPU and is not asked.
inline bool LightFamilyReadsSun(LightFamily f) {
  return f != LightFamily::kNone && f != LightFamily::kTreeLeaf && size_t(f) < size_t(LightFamily::kCount);
}

// Why a recorded draw has no light inputs. Such a draw keeps its material and is drawn without
// light; it is never lit from guessed data. kLightUnknown: its shaders have no family (no entry, or
// a pair that was not checked), or an input the family reads could not be read or is not usable.
// kNormalUnsupported: its normal element is not the one the table names, or is in a format no
// reader decodes. The third reason of the lighting, "light-block" (kLightBlockReason), is a
// frame's, not a draw's: the frame's light block was not read.
enum class LightReason : uint8_t { kNone, kLightUnknown, kNormalUnsupported, kCount };
inline constexpr const char* kLightBlockReason = "light-block";

inline const char* LightReasonName(LightReason r) {
  switch (r) {
    case LightReason::kNone: return "none";
    case LightReason::kLightUnknown: return "light-unknown";
    case LightReason::kNormalUnsupported: return "normal-unsupported";
    default: return "?";
  }
}

// ps-albedo.json "light": the family of a pixel shader, whether it is the "-multi" form, and the
// literal of the terrain layers' facing factor sat(scale * (N . c109)) (0 for every other family; a
// shader loads its literals itself, so no constant reader gives them).
struct PsLightSpec {
  uint64_t ps_hash;
  LightFamily family;
  bool multi;
  float facing_scale;
};
// One vertex shader a pixel shader's family was checked with (ps-albedo.json "light"."vertex").
struct PsLightPair {
  uint64_t ps_hash, vs_hash;
};

// A vertex fetch a light entry names: its index in DecodeVertexFetches order with the Xenos format
// and the offset in dwords the table's author saw; a mismatch at run time rejects the entry.
// fetch -1: none. `source`: the element's source components in the order the light's formula reads
// them (vs-transforms.json "light": "source"), as gen_transform_table.py's source_code writes them:
// two bits a component (0 to 3: x to w of the GPU's fetch) from bit 0, their number in the bits
// above (0x324 = x, y, z; 0x331 = y, x, w; 0x4C6 = z, y, x, w). LightSource reads it.
struct LightFetchSpec {
  int8_t fetch;
  uint8_t format;
  uint8_t offset_dwords;
  uint16_t source;
};
// vs-transforms.json "light". `second`: the colour stream (kStream), the probe weights (kProbe) or
// the instance colour (kInstance). a0_comp: the source component of the position fetch that holds
// the index of the draw's probe set (kProbe; -1 otherwise). scale_ref: the vertex constant the
// shader multiplies its world normal by, register * 4 + component (-1: none). power: the literal
// exponent of the colour stream (kStream; 0 otherwise).
struct VsLightSpec {
  uint64_t vs_hash;
  LightVertexKind kind;
  LightFetchSpec normal, second;
  int8_t a0_comp;
  int32_t scale_ref;
  float power;
};

// The Xenos vertex formats the light's elements have, and their size in a vertex (0: not one).
inline constexpr uint32_t kVertexFormat_8_8_8_8 = 6;
inline constexpr uint32_t kVertexFormat_10_11_11 = 16;
inline constexpr uint32_t kVertexFormat_16_16_16_16_FLOAT = 32;
inline constexpr uint32_t kVertexFormat_32_32_32_32_FLOAT = 38;
inline uint32_t LightElementBytes(uint32_t xenos_format) {
  switch (xenos_format) {
    case kVertexFormat_8_8_8_8:
    case kVertexFormat_10_11_11: return 4;
    case kVertexFormat_16_16_16_16_FLOAT: return 8;
    case kVertexFormat_32_32_32_32_FLOAT: return 16;
    default: return 0;
  }
}
// A normal is the 10_11_11 element of the object shaders or the half4 of the instanced plants.
inline bool IsLightNormalFormat(uint32_t xenos_format) {
  return xenos_format == kVertexFormat_10_11_11 || xenos_format == kVertexFormat_16_16_16_16_FLOAT;
}

// The source components a format's element has (x on, of the GPU's fetch): 0 for no element of the light.
inline uint32_t LightFormatComponents(uint32_t xenos_format) {
  switch (xenos_format) {
    case kVertexFormat_10_11_11: return 3;
    case kVertexFormat_8_8_8_8:
    case kVertexFormat_16_16_16_16_FLOAT:
    case kVertexFormat_32_32_32_32_FLOAT: return 4;
    default: return 0;
  }
}

// Bit c: a fetch with this destination swizzle writes source component c somewhere into its
// register (3 bits a register component: 0 to 3 source x to w, 4 zero, 5 one, 7 not written).
inline uint32_t FetchedComponents(uint32_t dst_swizzle) {
  uint32_t mask = 0;
  for (int c = 0; c < 4; ++c) {
    const uint32_t sel = (dst_swizzle >> (3 * c)) & 7u;
    if (sel < 4) mask |= 1u << sel;
  }
  return mask;
}

// The selector of an element as LightElement holds it, from a table entry's `spec`
// (LightFetchSpec::source) and the fetch it names. False, with `*source` untouched, unless the entry
// names exactly `count` components (a normal's three, a colour's three or four, the four weights),
// each a component the fetch's format has, none twice, and the fetch delivers each of them to the
// shader: a component the shader never receives is one it cannot be using, so the entry is not
// this shader's element (a shader that fetches two components of a normal and derives the third
// has another normal than the stored one).
// Where the fetch puts a component in its register is not asked: that is the destination swizzle,
// which differs between shaders that use one element in one way (the 10_11_11 normal is fetched
// xyz_ and _xyz, the plants' halves yxwz and yzxw, the weights xyzw and wzxy) and says nothing a
// reader of the stream needs.
inline bool LightSource(uint32_t spec, uint32_t count, const VertexFetch& f, uint8_t* source) {
  const uint32_t components = LightFormatComponents(f.format);
  if (components == 0 || count < 3 || count > 4 || (spec >> 8) != count) return false;
  if (count == 3 && ((spec >> 6) & 3u) != 0) return false;  // (no bits past the components named)
  const uint32_t fetched = FetchedComponents(f.dst_swizzle);
  uint32_t seen = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t c = (spec >> (2 * i)) & 3u;
    if (c >= components || ((seen >> c) & 1u) || !((fetched >> c) & 1u)) return false;
    seen |= 1u << c;
  }
  *source = uint8_t(spec & 0xFFu);
  return true;
}

// One vertex element as a later reader needs it: the stream from its bound offset, where the
// element lies in a vertex, how the fetch reads it, and which of its components the reader takes.
inline constexpr uint8_t kLightElementSigned = 1;
inline constexpr uint8_t kLightElementNormalized = 2;
inline constexpr uint8_t kLightElementEndianShift = 2;  // bits 2-3: the stream's endian (0 none, 1 8in16, 2 8in32, 3 16in32)
struct LightElement {
  BufferRef vb;
  uint16_t offset_bytes = 0;
  uint16_t stride_bytes = 0;
  // The element's SOURCE components (0 to 3: x to w of the GPU's fetch, before the fetch's
  // destination swizzle) in the order a reader takes them, two bits each from bit 0, and how many
  // there are (3 or 4): the table's (LightSource). A normal: its x, y, z. The colour stream: red,
  // green, blue, alpha. An instance's colour: red, green, blue. The probe weights: the four, in the
  // order of the probe constants' components. The fetch's destination swizzle is not kept: it is
  // not what a reader needs (the discovery row's "fetches" has it).
  uint8_t source = 0;
  uint8_t source_count = 0;
  uint8_t format = 0;    // Xenos vertex format
  uint8_t flags = 0;     // kLightElementSigned, kLightElementNormalized, the endian
};
static_assert(sizeof(LightElement) == 16);
// Source component i of the element's selector (i < source_count).
inline uint32_t LightSourceComponent(const LightElement& e, uint32_t i) { return (uint32_t(e.source) >> (2 * i)) & 3u; }

// The registers the light of a draw reads besides the frame's block (lighting-formulas.md, per
// family). Vertex constants, from the device's vertex bank (none is a register a buffer load can
// name: light_block.h, kBufferLoadRanges; frame-map section 14, "The vertex side"):
inline constexpr uint32_t kLightVsRotationReg = 4;      // c4..c6: the rows of the world rotation (xyz)
inline constexpr uint32_t kLightVsFogScaleReg = 8;      // c8.x: the scale of the trees' in-scatter (trunks and leaves; not the light's)
inline constexpr uint32_t kLightVsIndexOffsetReg = 20;  // c20.x: the colour stream's index offset (ECD6)
inline constexpr uint32_t kLightVsProbeReg = 28;        // c28..c39: three probe sets of four registers
inline constexpr uint32_t kLightProbeRegisters = 12;
inline constexpr uint32_t kLightVsBendReg = 45;         // c45: the plants' bend of the normal
inline constexpr uint32_t kLightVsWrapReg = 50;         // c50.w: the wrap of the leaves' front light
// Pixel constants, from the device's pixel bank (bank-only registers too):
inline constexpr uint32_t kLightPsSunScaleReg = 11;     // c11.x: terrain (layers: the ambient; far: the sun)
inline constexpr uint32_t kLightPsFadeReg = 26;         // c26.xyz: objects, trunks and leaves, with the output scale
inline constexpr uint32_t kLightPsAmbientReg = 27;      // c27.w: objects, trunks and leaves, on the ambient part
inline constexpr uint32_t kLightPsTintReg = 49;         // c49.xyz: objects, trunks and leaves, on the whole colour

// A run of registers of one bank (0 vertex, 1 pixel).
struct LightRegisterRun {
  uint8_t bank, reg, count;
};
inline constexpr size_t kMaxLightRegisterRuns = 7;
// A run the light does not need: vertex c8, whose x is the scale of the trees' in-scatter (fog
// sub-project, ruling B: the game's trunks and leaves fog in their vertex shaders, add = (1 - T) *
// tf4(angle) * c8.x; it was 1 in 1,968 of 1,968 captured rows). A tree draw whose c8 was not read,
// or is no number, keeps its light and is drawn without fog (LightInputs::tree_fog).
constexpr bool LightRunOptional(const LightRegisterRun& run) { return run.bank == 0 && run.reg == kLightVsFogScaleReg; }
// The registers a draw of this family and vertex kind reads: what the capture reads from the
// banks, and what MakeLightInputs requires to have been read (but for a run that is
// LightRunOptional). Returns the number of runs.
inline size_t LightRegisterRuns(const PsLightSpec& ps, const VsLightSpec& vs, LightRegisterRun out[kMaxLightRegisterRuns]) {
  size_t n = 0;
  auto add = [&](uint32_t bank, uint32_t reg, uint32_t count) { out[n++] = {uint8_t(bank), uint8_t(reg), uint8_t(count)}; };
  switch (ps.family) {
    case LightFamily::kTerrainLayer:
    case LightFamily::kTerrainFar:
      add(1, kLightPsSunScaleReg, 1);
      break;
    case LightFamily::kObject:
    case LightFamily::kTreeTrunk:
    case LightFamily::kTreeLeaf:
      add(0, kLightVsRotationReg, 3);
      if (vs.scale_ref >= 0 && vs.scale_ref < 1024) add(0, uint32_t(vs.scale_ref) / 4, 1);
      if (vs.kind == LightVertexKind::kStream) add(0, kLightVsIndexOffsetReg, 1);
      if (vs.kind == LightVertexKind::kProbe) add(0, kLightVsProbeReg, kLightProbeRegisters);
      if (ps.family == LightFamily::kTreeLeaf) add(0, kLightVsWrapReg, 1);
      add(1, kLightPsFadeReg, 2);  // c26, c27
      add(1, kLightPsTintReg, 1);
      if (ps.family != LightFamily::kObject) add(0, kLightVsFogScaleReg, 1);  // (optional: LightRunOptional)
      break;
    case LightFamily::kPlant:
    case LightFamily::kPlantLit:
      add(0, kLightVsBendReg, 1);
      break;
    default:
      break;
  }
  return n;
}

// The trees' in-scatter scale (vertex c8.x) is carried only inside [0, kTreeFogScaleMax], the range
// of its pixel twin c28.y (fog_block.h: the amounts are in [0, 4]); kTreeFogNone marks a draw
// without a usable one.
inline constexpr float kTreeFogScaleMax = 4.0f;
inline constexpr float kTreeFogNone = -1.0f;

// The light inputs of one recorded draw (an entry of FrameScene::lights). Sized for the largest
// family (the probe sets); a field a family does not read holds its neutral value.
struct LightInputs {
  LightFamily family = LightFamily::kNone;
  LightVertexKind kind = LightVertexKind::kNone;
  bool multi = false;   // the family's two- or four-light form: its first light need not be the sun
  int8_t a0_comp = -1;  // kProbe: source component (0-3) of the position element holding the probe set's index
  // The vertex normal's element (not kTerrain: a terrain normal comes from the patch's heightmap).
  // kInstance: in the mesh stream, to go through the instance's rows.
  LightElement normal;
  // kStream: the colour stream (8_8_8_8; red is the fetch's z), read at colour_first + the vertex
  // index. kProbe: the four weights (32_32_32_32_FLOAT) at the vertex index. kInstance: the instance's
  // colour (8_8_8_8) in the instance stream, at the copy's index.
  LightElement second;
  uint32_t colour_first = 0;    // kStream: vertex c20.x, a whole number; bounded by the stream at capture
  float colour_power = 0.0f;    // kStream: the literal the colour is raised to (2.2)
  float facing_scale = 0.0f;    // kTerrainLayer: the literal of sat(scale * (N . c109))
  // kObject, kTreeTrunk, kTreeLeaf: vertex c4..c6 (xyz), the rotation the shader puts its normal
  // through, and the constant it then multiplies by (c7.w; 1 where the shader has none).
  float world_rot[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
  float world_scale = 1.0f;
  float c11x = 1.0f;                    // terrain: pixel c11.x
  float c27w = 1.0f;                    // kObject, kTreeTrunk, kTreeLeaf: pixel c27.w
  float c49[3] = {1.0f, 1.0f, 1.0f};    // kObject, kTreeTrunk, kTreeLeaf: pixel c49.xyz
  float c26[3] = {1.0f, 1.0f, 1.0f};    // kObject, kTreeTrunk, kTreeLeaf: pixel c26.xyz
  // kProbe: vertex c28..c39 (the probe sets c[28 + a0]..c[31 + a0], a0 = 0, 4 or 8).
  // kInstance: [0] = vertex c45. Zero otherwise.
  float vs_const[kLightProbeRegisters][4] = {};
  // kTreeLeaf only (plan Task 8c; zero otherwise): the fourth components of vertex c4..c6, the
  // translation of the world position (the leaf's light takes the vector from the vertex to the
  // eye), and vertex c50.w, the wrap of its front light (0.4 in every row captured).
  float world_pos[3] = {};
  float wrap = 0.0f;
  // kTreeTrunk, kTreeLeaf (fog sub-project; kTreeFogNone otherwise): vertex c8.x, the scale of the
  // trees' in-scatter, when it was read, is a number and lies in [0, kTreeFogScaleMax]. kTreeFogNone:
  // the draw has no usable scale and is drawn without fog (counted by the pass); its light is whole.
  float tree_fog = kTreeFogNone;
  // A draw of a multi-light family or the leaves (local lights; 0xFFFF, kNoSideIndex of
  // draw_record.h, which includes this header, otherwise): the draw's light set in
  // FrameScene::light_sets (render::LightSetOf), kNoSideIndex none.
  uint16_t light_set = 0xFFFF;
  uint8_t light_set_pad[2] = {};
};
// Measured, so that a field added here is noticed: one entry per lit draw is copied into the frame
// on the guest thread (about 900 a frame at the bridge; 312 bytes before the leaves' four floats,
// 328 before the trees' fog scale, 332 before the light set's index). The draw record itself does
// not hold it and stays as it is.
static_assert(sizeof(LightInputs) == 336, "LightInputs grew or shrank: see the comment above");

// Registers of one constant bank as a draw's capture read them: `bank` is 256 x 4 host floats,
// bit r of `have` says register r was read for this draw (anything else in `bank` is stale).
struct RegisterView {
  const float* bank = nullptr;
  uint64_t have[4] = {};
  void Mark(uint32_t reg, uint32_t count) {
    for (uint32_t r = reg; r < 256 && r - reg < count; ++r) have[r / 64] |= 1ull << (r % 64);
  }
  bool Has(uint32_t reg, uint32_t count = 1) const {
    if (!bank || count == 0 || reg >= 256 || count > 256 - reg) return false;
    for (uint32_t r = reg; r < reg + count; ++r) {
      if (!((have[r / 64] >> (r % 64)) & 1)) return false;
    }
    return true;
  }
};

// The stream behind a fetch's slot as the capture resolved it (from the stream's bound offset on).
struct LightStream {
  bool resolved = false;
  BufferRef vb;
  uint8_t endian = 0;  // the fetch constant's endian
};

// The fetch a light entry names, or null: no such fetch, or its format or offset is not the one
// the entry was written for (another fetch order: never a wrong element).
inline const VertexFetch* LightFetch(const std::vector<VertexFetch>& fetches, const LightFetchSpec& spec) {
  if (spec.fetch < 0 || size_t(spec.fetch) >= fetches.size()) return nullptr;
  const VertexFetch& f = fetches[size_t(spec.fetch)];
  if (f.format != spec.format || f.offset_dwords != int32_t(spec.offset_dwords)) return nullptr;
  return &f;
}

// The element of a fetch in its stream, read through `source` (`count` components: LightSource).
// False when the element does not lie inside one vertex of the fetch's stride, or the stream is
// not resolved or holds no whole vertex.
inline bool MakeLightElement(const VertexFetch& f, uint8_t source, uint32_t count, const LightStream& stream,
                             LightElement* out) {
  const uint32_t bytes = LightElementBytes(f.format);
  if (bytes == 0 || f.offset_dwords < 0 || f.stride_dwords == 0 || f.stride_dwords > 255) return false;
  const uint32_t offset = uint32_t(f.offset_dwords) * 4, stride = f.stride_dwords * 4;
  if (offset + bytes > stride) return false;
  if (!stream.resolved || stream.vb.size < stride || stream.endian > 3) return false;
  out->vb = stream.vb;
  out->offset_bytes = uint16_t(offset);
  out->stride_bytes = uint16_t(stride);
  out->source = source;
  out->source_count = uint8_t(count);
  out->format = uint8_t(f.format);
  out->flags = uint8_t((f.is_signed ? kLightElementSigned : 0) | (f.normalized ? kLightElementNormalized : 0) |
                       (stream.endian << kLightElementEndianShift));
  return true;
}

// What the capture found for one draw.
struct LightDraw {
  const PsLightSpec* ps = nullptr;  // the pixel shader's entry, null without one
  const VsLightSpec* vs = nullptr;  // the vertex shader's entry
  bool pair = false;                // the two are a pair the table lists (PsLightPair)
  const std::vector<VertexFetch>* fetches = nullptr;  // the vertex shader's fetches
  LightStream normal_stream;        // the stream behind LightFetch(vs->normal)
  LightStream second_stream;        // ... behind LightFetch(vs->second)
  RegisterView vs_regs, ps_regs;    // the registers of LightRegisterRuns, as read
  // The largest vertex index the draw reads (index plus base vertex), -1 when it is not known.
  // For an instanced draw the index names a copy and a mesh vertex and is not a bound here.
  int64_t max_vertex = -1;
  bool instanced = false;           // the draw has its instance set (DrawInputs::instances.active)
};

// The colour stream's index offset is a whole number of vertices (c20.x is 0 for most meshes and
// up to several thousand where meshes share a stream: lighting-formulas.md, "Objects").
inline constexpr float kMaxLightColourFirst = 16777215.0f;

// The light inputs of a draw. On kNone `*out` is complete; otherwise `*out` is left default (no
// family) and the draw has no light. `why` (optional) receives a short word for the discovery rows.
// Fails closed: every register it copies was read and is finite, every element is the table's, is
// read through the table's source components and lies in its stream, and the two streams the
// position's range check does not cover (the colour stream, read at an offset, and the probe
// weights) hold the draw's largest vertex.
// What the world rotation is (a rotation, mirrored, skewed, without a direction) is not asked here:
// that is ClassifyRotation of render/normal_decode.h, which the clay pass runs on world_rot and
// world_scale when it prepares the draw and counts per kind and per vertex shader (ClayStats::
// light_rotation). The capture's only question is whether the nine values and the scale are
// numbers: a draw refused here for its rotation would be counted in two places, and a mirrored or
// skewed rotation is still a light the pass draws (with the game's own normalize(M n)).
inline LightReason MakeLightInputs(const LightDraw& d, LightInputs* out, const char** why = nullptr) {
  *out = LightInputs{};
  const char* unused = nullptr;
  const char*& word = why ? *why : unused;
  auto fail = [&](LightReason r, const char* w) {
    *out = LightInputs{};
    word = w;
    return r;
  };
  word = "";
  if (!d.ps || d.ps->family == LightFamily::kNone || size_t(d.ps->family) >= size_t(LightFamily::kCount)) {
    return fail(LightReason::kLightUnknown, "no-family");
  }
  if (!d.vs || !LightKindFits(d.ps->family, d.vs->kind)) return fail(LightReason::kLightUnknown, "vertex-shader");
  if (!d.pair) return fail(LightReason::kLightUnknown, "pair-not-checked");
  const PsLightSpec& ps = *d.ps;
  const VsLightSpec& vs = *d.vs;
  LightInputs& l = *out;  // (default so far; fail() makes it default again)
  l.family = ps.family;
  l.kind = vs.kind;
  l.multi = ps.multi;
  // Registers: all read, all finite.
  LightRegisterRun runs[kMaxLightRegisterRuns];
  const size_t n_runs = LightRegisterRuns(ps, vs, runs);
  for (size_t i = 0; i < n_runs; ++i) {
    if (LightRunOptional(runs[i])) continue;  // (the trees' fog scale: below)
    const RegisterView& view = runs[i].bank ? d.ps_regs : d.vs_regs;
    if (!view.Has(runs[i].reg, runs[i].count)) return fail(LightReason::kLightUnknown, "register-unread");
  }
  bool finite = true;
  auto take = [&](const RegisterView& view, uint32_t reg, uint32_t comp, float* to) {
    const float v = view.bank[reg * 4 + comp];
    finite = finite && std::isfinite(v);
    *to = v;
  };
  if (ps.family == LightFamily::kTerrainLayer || ps.family == LightFamily::kTerrainFar) {
    take(d.ps_regs, kLightPsSunScaleReg, 0, &l.c11x);
    if (ps.family == LightFamily::kTerrainLayer) {
      if (!(ps.facing_scale > 0.0f) || !std::isfinite(ps.facing_scale)) return fail(LightReason::kLightUnknown, "literal");
      l.facing_scale = ps.facing_scale;
    }
    if (!finite) return fail(LightReason::kLightUnknown, "register-not-finite");
    return LightReason::kNone;
  }
  if (!d.fetches) return fail(LightReason::kLightUnknown, "vertex-shader");
  const bool leaf = ps.family == LightFamily::kTreeLeaf;
  if (ps.family == LightFamily::kObject || ps.family == LightFamily::kTreeTrunk || leaf) {
    for (uint32_t row = 0; row < 3; ++row) {
      for (uint32_t c = 0; c < 3; ++c) take(d.vs_regs, kLightVsRotationReg + row, c, &l.world_rot[row][c]);
      // A leaf: the row's fourth component too, the world position's translation.
      if (leaf) take(d.vs_regs, kLightVsRotationReg + row, 3, &l.world_pos[row]);
    }
    if (leaf) take(d.vs_regs, kLightVsWrapReg, 3, &l.wrap);
    if (vs.scale_ref >= 0) {
      if (vs.scale_ref >= 1024) return fail(LightReason::kLightUnknown, "vertex-shader");
      take(d.vs_regs, uint32_t(vs.scale_ref) / 4, uint32_t(vs.scale_ref) % 4, &l.world_scale);
    }
    take(d.ps_regs, kLightPsAmbientReg, 3, &l.c27w);
    for (uint32_t c = 0; c < 3; ++c) {
      take(d.ps_regs, kLightPsTintReg, c, &l.c49[c]);
      take(d.ps_regs, kLightPsFadeReg, c, &l.c26[c]);
    }
    if (vs.kind == LightVertexKind::kProbe) {
      for (uint32_t r = 0; r < kLightProbeRegisters; ++r) {
        for (uint32_t c = 0; c < 4; ++c) take(d.vs_regs, kLightVsProbeReg + r, c, &l.vs_const[r][c]);
      }
    }
    // A tree's fog scale, outside `finite`: without it the draw is unfogged, not unlit.
    if (ps.family != LightFamily::kObject && d.vs_regs.Has(kLightVsFogScaleReg)) {
      const float c8x = d.vs_regs.bank[kLightVsFogScaleReg * 4];
      if (c8x >= 0.0f && c8x <= kTreeFogScaleMax) l.tree_fog = c8x;  // (false for a NaN)
    }
  } else {
    for (uint32_t c = 0; c < 4; ++c) take(d.vs_regs, kLightVsBendReg, c, &l.vs_const[0][c]);
  }
  if (!finite) return fail(LightReason::kLightUnknown, "register-not-finite");
  // The normal: the table's fetch, in a format a reader decodes, inside its vertex.
  const VertexFetch* nf = LightFetch(*d.fetches, vs.normal);
  if (!nf || !IsLightNormalFormat(nf->format)) return fail(LightReason::kNormalUnsupported, "normal-fetch");
  const uint32_t normal_bytes = LightElementBytes(nf->format);
  if (nf->offset_dwords < 0 || nf->stride_dwords == 0 || nf->stride_dwords > 255 ||
      uint32_t(nf->offset_dwords) * 4 + normal_bytes > nf->stride_dwords * 4) {
    return fail(LightReason::kNormalUnsupported, "normal-fetch");
  }
  // Its x, y, z are the three source components the entry names, and the shader gets all three.
  uint8_t source = 0;
  if (!LightSource(vs.normal.source, 3, *nf, &source)) return fail(LightReason::kNormalUnsupported, "normal-source");
  if (!MakeLightElement(*nf, source, 3, d.normal_stream, &l.normal)) return fail(LightReason::kLightUnknown, "normal-stream");
  const bool instance = vs.kind == LightVertexKind::kInstance;
  if (instance != d.instanced) return fail(LightReason::kLightUnknown, "instance-set");
  if (!instance) {
    if (d.max_vertex < 0) return fail(LightReason::kLightUnknown, "vertex-range");
    if ((uint64_t(d.max_vertex) + 1) * l.normal.stride_bytes > l.normal.vb.size) {
      return fail(LightReason::kLightUnknown, "normal-stream");
    }
  }
  // The second element: the colour stream, the probe weights or the instance colour.
  // The colour stream's red, green, blue and alpha; the four weights; an instance colour's red,
  // green and blue (its fourth byte is not the light's: the fetches leave it out).
  const VertexFetch* sf = LightFetch(*d.fetches, vs.second);
  const bool probe = vs.kind == LightVertexKind::kProbe;
  const uint32_t want = probe ? kVertexFormat_32_32_32_32_FLOAT : kVertexFormat_8_8_8_8;
  const uint32_t second_count = instance ? 3 : 4;
  if (!sf || sf->format != want) return fail(LightReason::kLightUnknown, probe ? "weights" : "colour");
  if (!LightSource(vs.second.source, second_count, *sf, &source)) {
    return fail(LightReason::kLightUnknown, probe ? "weights-source" : "colour-source");
  }
  if (!MakeLightElement(*sf, source, second_count, d.second_stream, &l.second)) {
    return fail(LightReason::kLightUnknown, probe ? "weights" : "colour");
  }
  if (vs.kind == LightVertexKind::kStream) {
    // The colour is read at the vertex index plus c20.x: a whole number, and the draw's largest
    // vertex must still lie inside the stream.
    float first = 0.0f;
    take(d.vs_regs, kLightVsIndexOffsetReg, 0, &first);
    if (!finite || !(first >= 0.0f) || first > kMaxLightColourFirst || first != std::floor(first)) {
      return fail(LightReason::kLightUnknown, "colour-offset");
    }
    l.colour_first = uint32_t(first);
    if ((uint64_t(l.colour_first) + uint64_t(d.max_vertex) + 1) * l.second.stride_bytes > l.second.vb.size) {
      return fail(LightReason::kLightUnknown, "colour-range");
    }
    if (!(vs.power > 0.0f) || !std::isfinite(vs.power)) return fail(LightReason::kLightUnknown, "literal");
    l.colour_power = vs.power;
  } else if (vs.kind == LightVertexKind::kProbe) {
    if ((uint64_t(d.max_vertex) + 1) * l.second.stride_bytes > l.second.vb.size) {
      return fail(LightReason::kLightUnknown, "weights-range");
    }
    if (vs.a0_comp < 0 || vs.a0_comp > 3) return fail(LightReason::kLightUnknown, "vertex-shader");
    l.a0_comp = vs.a0_comp;
  }
  return LightReason::kNone;
}

}  // namespace fable2::native::capture
