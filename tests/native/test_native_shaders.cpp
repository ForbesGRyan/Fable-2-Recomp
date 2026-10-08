// Compiles the native renderer's HLSL with D3DCompile (no GPU needed).
#include "../../src/native/fable2_native_shaders.h"
#include "../../src/native/render/clay_logic.h"  // ClayConstants (before windows.h: its min and max macros)
#include <windows.h>
#include <initguid.h>  // before d3d11shader.h: defines IID_ID3D11ShaderReflection here
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <cstring>
#include <iostream>
#include <string>

static bool Compile(const char* name, const char* src, const char* target) {
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  HRESULT hr = D3DCompile(src, strlen(src), name, nullptr, nullptr, "main", target,
                          D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
  if (FAILED(hr)) {
    std::cerr << name << " failed:\n"
              << (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "") << "\n";
  }
  if (code) code->Release();
  if (errors) errors->Release();
  return SUCCEEDED(hr);
}

// True when `src` compiles and its bytecode holds `count` consecutive copies
// of the dword `value` (an immediate constant the optimiser kept).
static bool CompiledHas(const char* name, const char* src, const char* target, uint32_t value, int count) {
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  HRESULT hr = D3DCompile(src, strlen(src), name, nullptr, nullptr, "main", target,
                          D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
  bool found = false;
  if (SUCCEEDED(hr) && code) {
    const auto* bytes = static_cast<const unsigned char*>(code->GetBufferPointer());
    const size_t size = code->GetBufferSize();
    for (size_t at = 0; !found && at + 4 * size_t(count) <= size; at += 4) {
      found = true;
      for (int k = 0; found && k < count; ++k) {
        uint32_t dword;
        std::memcpy(&dword, bytes + at + 4 * size_t(k), 4);
        found = dword == value;
      }
    }
  }
  if (code) code->Release();
  if (errors) errors->Release();
  return found;
}

// True when `src` compiles and the disassembly of its bytecode contains `needle`.
static bool DisassemblyHas(const char* name, const char* src, const char* target, const char* needle) {
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  ID3DBlob* text = nullptr;
  HRESULT hr = D3DCompile(src, strlen(src), name, nullptr, nullptr, "main", target,
                          D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
  bool found = false;
  if (SUCCEEDED(hr) && code &&
      SUCCEEDED(D3DDisassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &text)) && text) {
    // The listing is not always terminated: search a terminated copy.
    const std::string listing(static_cast<const char*>(text->GetBufferPointer()), text->GetBufferSize());
    found = listing.find(needle) != std::string::npos;
  }
  if (code) code->Release();
  if (errors) errors->Release();
  if (text) text->Release();
  return found;
}

// 0 when `src` compiles and its cbuffer `Terrain` has exactly the variables of kClayTerrainFields, each at
// the table's byte offset with the table's size, in a buffer of 160 bytes. Otherwise a number that says
// what differs: 1 no compile, 2 no reflection, 3 no such buffer, 4 another size or variable count,
// 10 + i for the table's entry i.
static int TerrainLayoutMismatch(const char* name, const char* src) {
  using namespace fable2::native::shaders;
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  HRESULT hr = D3DCompile(src, strlen(src), name, nullptr, nullptr, "main", "ps_5_0",
                          D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
  if (errors) errors->Release();
  if (FAILED(hr) || !code) return 1;
  ID3D11ShaderReflection* refl = nullptr;
  int result = 0;
  if (FAILED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_ID3D11ShaderReflection,
                        reinterpret_cast<void**>(&refl))) || !refl) {
    result = 2;
  } else {
    ID3D11ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByName("Terrain");
    D3D11_SHADER_BUFFER_DESC bd{};
    const unsigned count = sizeof(kClayTerrainFields) / sizeof(kClayTerrainFields[0]);
    if (!cb || FAILED(cb->GetDesc(&bd))) {
      result = 3;
    } else if (bd.Size != 160 || bd.Variables != count) {
      result = 4;
    } else {
      for (unsigned i = 0; i < count && !result; ++i) {
        ID3D11ShaderReflectionVariable* var = cb->GetVariableByName(kClayTerrainFields[i].name);
        D3D11_SHADER_VARIABLE_DESC vd{};
        if (!var || FAILED(var->GetDesc(&vd)) || vd.StartOffset != kClayTerrainFields[i].offset ||
            vd.Size != kClayTerrainFields[i].bytes) {
          result = 10 + int(i);
        }
      }
    }
  }
  if (refl) refl->Release();
  code->Release();
  return result;
}

// The clay shaders' cbuffer Draw (register b0) as the root constants fill it: every variable of
// ClayConstants (clay_logic.h) under its HLSL name, at the struct's offset, with its size.
struct DrawField {
  const char* name;
  size_t offset, bytes;
};
using fable2::native::render::ClayConstants;
static const DrawField kDrawFields[] = {
    {"r0", offsetof(ClayConstants, rows), 16},
    {"r1", offsetof(ClayConstants, rows) + 16, 16},
    {"r2", offsetof(ClayConstants, rows) + 32, 16},
    {"r3", offsetof(ClayConstants, rows) + 48, 16},
    {"layout", offsetof(ClayConstants, layout), sizeof(ClayConstants::layout)},
    {"base_vertex", offsetof(ClayConstants, base_vertex), sizeof(ClayConstants::base_vertex)},
    {"vertex_count", offsetof(ClayConstants, vertex_count), sizeof(ClayConstants::vertex_count)},
    {"color", offsetof(ClayConstants, color), sizeof(ClayConstants::color)},
    {"uv_xform", offsetof(ClayConstants, uv), sizeof(ClayConstants::uv)},
    {"textured", offsetof(ClayConstants, textured), sizeof(ClayConstants::textured)},
    {"sampler_index", offsetof(ClayConstants, sampler), sizeof(ClayConstants::sampler)},
    {"alpha_mode", offsetof(ClayConstants, alpha_mode), sizeof(ClayConstants::alpha_mode)},
    {"alpha_ref", offsetof(ClayConstants, alpha_ref), sizeof(ClayConstants::alpha_ref)},
    {"cut", offsetof(ClayConstants, cut), sizeof(ClayConstants::cut)},
    {"alpha_scale", offsetof(ClayConstants, alpha_scale), sizeof(ClayConstants::alpha_scale)},
    {"pad2", offsetof(ClayConstants, pad2), sizeof(ClayConstants::pad2)},
};

// 0 when `src` compiles for `target` and its cbuffer Draw has exactly the variables of kDrawFields, each at
// the struct's byte offset with its size, in a buffer of sizeof(ClayConstants). Otherwise as
// TerrainLayoutMismatch: 1 no compile, 2 no reflection, 3 no such buffer, 4 another size or variable
// count, 10 + i for entry i.
static int DrawLayoutMismatch(const char* name, const char* src, const char* target) {
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  HRESULT hr = D3DCompile(src, strlen(src), name, nullptr, nullptr, "main", target,
                          D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
  if (errors) errors->Release();
  if (FAILED(hr) || !code) return 1;
  ID3D11ShaderReflection* refl = nullptr;
  int result = 0;
  if (FAILED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_ID3D11ShaderReflection,
                        reinterpret_cast<void**>(&refl))) || !refl) {
    result = 2;
  } else {
    ID3D11ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByName("Draw");
    D3D11_SHADER_BUFFER_DESC bd{};
    const unsigned count = sizeof(kDrawFields) / sizeof(kDrawFields[0]);
    if (!cb || FAILED(cb->GetDesc(&bd))) {
      result = 3;
    } else if (bd.Size != sizeof(ClayConstants) || bd.Variables != count) {
      result = 4;
    } else {
      for (unsigned i = 0; i < count && !result; ++i) {
        ID3D11ShaderReflectionVariable* var = cb->GetVariableByName(kDrawFields[i].name);
        D3D11_SHADER_VARIABLE_DESC vd{};
        if (!var || FAILED(var->GetDesc(&vd)) || vd.StartOffset != kDrawFields[i].offset ||
            vd.Size != kDrawFields[i].bytes) {
          result = 10 + int(i);
        }
      }
    }
  }
  if (refl) refl->Release();
  code->Release();
  return result;
}

// 0 when `src` compiles and is bound as the pass binds the output mapping: a cbuffer `Map` of 16 bytes
// whose variable `exposure` is its first float (root constant word 0), the texture `linear_target` at t0,
// and nothing else (no sampler). Otherwise: 1 no compile, 2 no reflection, 3 no such buffer, 4 another
// size, 5 `exposure` elsewhere, 6 the texture is not at t0 or something else is bound.
static int MapLayoutMismatch(const char* name, const char* src) {
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  HRESULT hr = D3DCompile(src, strlen(src), name, nullptr, nullptr, "main", "ps_5_0",
                          D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
  if (errors) errors->Release();
  if (FAILED(hr) || !code) return 1;
  ID3D11ShaderReflection* refl = nullptr;
  int result = 0;
  if (FAILED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_ID3D11ShaderReflection,
                        reinterpret_cast<void**>(&refl))) || !refl) {
    result = 2;
  } else {
    ID3D11ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByName("Map");
    ID3D11ShaderReflectionVariable* var = cb ? cb->GetVariableByName("exposure") : nullptr;
    D3D11_SHADER_BUFFER_DESC bd{};
    D3D11_SHADER_VARIABLE_DESC vd{};
    D3D11_SHADER_INPUT_BIND_DESC tex{};
    D3D11_SHADER_DESC sd{};
    if (!cb || FAILED(cb->GetDesc(&bd))) {
      result = 3;
    } else if (bd.Size != 16) {
      result = 4;
    } else if (!var || FAILED(var->GetDesc(&vd)) || vd.StartOffset != 0 || vd.Size != 4) {
      result = 5;
    } else if (FAILED(refl->GetResourceBindingDescByName("linear_target", &tex)) || tex.Type != D3D_SIT_TEXTURE ||
               tex.BindPoint != 0 || FAILED(refl->GetDesc(&sd)) || sd.BoundResources != 2) {
      result = 6;
    }
  }
  if (refl) refl->Release();
  code->Release();
  return result;
}

int main() {
  using namespace fable2::native::shaders;
  if (!Compile("fullscreen_vs", kFullscreenVs, "vs_5_0")) return 1;
  if (!Compile("pattern_ps", kPatternPs, "ps_5_0")) return 2;
  if (!Compile("grid_ps", kGridPs, "ps_5_0")) return 3;
  if (!Compile("clay_vs", kClayVs, "vs_5_0")) return 5;
  if (!Compile("clay_ps", kClayPs, "ps_5_0")) return 6;
  if (!Compile("composite_ps", kCompositePs, "ps_5_0")) return 7;
  if (!Compile("clay_textured_ps", kClayTexturedPs, "ps_5_0")) return 8;
  // The clay vertex shader drops a vertex past the instancing shaders'
  // distance cut by writing a NaN position: the compiled code must still hold
  // the four NaN dwords (a compiler may fold NaN expressions away), and both
  // shaders that declare the full cbuffer must declare the cut.
  if (!CompiledHas("clay_vs", kClayVs, "vs_5_0", 0x7FC00000u, 4)) return 9;
  if (CompiledHas("clay_ps", kClayPs, "ps_5_0", 0x7FC00000u, 1)) return 10;   // control: no NaN elsewhere
  if (!strstr(kClayVs, "float4 cut;") || !strstr(kClayTexturedPs, "float4 cut;")) return 11;
  // The alpha fields (ClayConstants: mode and reference in the two words after the
  // sampler, the scale after the cut) are declared by every clay shader, and the
  // textured pixel shader applies the alpha test with a discard that survives compiling.
  for (const char* src : {kClayVs, kClayPs, kClayTexturedPs}) {
    const char* mode = strstr(src, "uint sampler_index; uint alpha_mode; float alpha_ref;");
    const char* cut = strstr(src, "float4 cut;");
    const char* scale = strstr(src, "float alpha_scale; uint3 pad2;");
    if (!mode || !cut || !scale || !(mode < cut && cut < scale)) return 12;
  }
  if (!strstr(kClayTexturedPs, "discard;") || strstr(kClayPs, "discard")) return 13;
  if (!DisassemblyHas("clay_textured_ps", kClayTexturedPs, "ps_5_0", "discard")) return 14;
  // Alpha-to-mask is the GPU's rule (clay_logic.h, AlphaToMaskCovers): the pixel's parity picks
  // one of four offsets from bits 16-23 of alpha_mode, the surface's sample count (bits 24-25:
  // 1 two samples, 2 four, anything else none) picks the form, the pixel is kept iff
  // alpha >= mask_base - offset * per_offset (0.5 and 1/8, 0.5 and 1/16, 1 and 1/4), and it comes
  // after the alpha test. No flat 0.5, and no form that ignores the sample count.
  {
    const char* test = strstr(kClayTexturedPs, "!alpha_passes(a, alpha_ref, alpha_mode & 7)");
    const char* index = strstr(kClayTexturedPs, "uint index = ((p.x & 1) << 1) | (p.y & 1);");
    const char* offset = strstr(kClayTexturedPs, "uint offset = (alpha_mode >> (16 + 2 * index)) & 3;");
    const char* samples = strstr(kClayTexturedPs, "uint samples = (alpha_mode >> 24) & 3;");
    const char* base = strstr(kClayTexturedPs, "float mask_base = (samples == 1 || samples == 2) ? 0.5 : 1.0;");
    const char* per = strstr(kClayTexturedPs,
                             "float per_offset = samples == 1 ? 0.125 : samples == 2 ? 0.0625 : 0.25;");
    const char* mask = strstr(kClayTexturedPs, "if (!(a >= mask_base - offset * per_offset)) discard;");
    if (!test || !index || !offset || !mask || !(test < index && index < offset && offset < mask)) return 17;
    if (!strstr(kClayTexturedPs, "uint2 p = uint2(pos.xy);") || strstr(kClayTexturedPs, "a < 0.5")) return 18;
    if (!samples || !base || !per || !(offset < samples && samples < base && base < per && per < mask)) return 19;
    if (strstr(kClayTexturedPs, "1.0 - offset * 0.25")) return 20;  // the one-sample form alone is gone
  }
  if (DisassemblyHas("clay_ps", kClayPs, "ps_5_0", "discard")) return 15;   // control
  if (!DisassemblyHas("grid_ps", kGridPs, "ps_5_0", "discard")) return 16;  // control: a known discard
  // The terrain material (frame-map section 13, "Terrain", "Native terrain material"): two more
  // pixel shaders. Both compile, declare the draw constants like the others and keep a discard
  // after compiling (the layer's cull of an absent layer; the alpha test in both).
  if (!Compile("clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0")) return 21;
  if (!Compile("clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0")) return 22;
  for (const char* src : {kClayTerrainLayerPs, kClayTerrainFarPs}) {
    const char* mode = strstr(src, "uint sampler_index; uint alpha_mode; float alpha_ref;");
    const char* cut = strstr(src, "float4 cut;");
    const char* scale = strstr(src, "float alpha_scale; uint3 pad2;");
    if (!mode || !cut || !scale || !(mode < cut && cut < scale)) return 23;
    if (!strstr(src, "discard;")) return 24;
  }
  if (!DisassemblyHas("clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0", "discard")) return 25;
  if (!DisassemblyHas("clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0", "discard")) return 26;
  // The terrain constants (cbuffer Terrain, register b1) lie where the table beside the HLSL says,
  // in both compiled shaders; ClayTerrainConstants is checked against the same table
  // (test_clay_logic.cpp). Control: a source with two fields exchanged is reported.
  if (int at = TerrainLayoutMismatch("clay_terrain_layer_ps", kClayTerrainLayerPs)) return 100 + at;
  if (int at = TerrainLayoutMismatch("clay_terrain_far_ps", kClayTerrainFarPs)) return 200 + at;
  {
    std::string swapped = kClayTerrainLayerPs;
    const std::string good = "float3 t_eye; float t_hole_ref;";
    const size_t at = swapped.find(good);
    if (at == std::string::npos) return 27;
    swapped.replace(at, good.size(), "float t_hole_ref; float3 t_eye;");
    if (TerrainLayoutMismatch("swapped_ps", swapped.c_str()) < 10) return 28;
  }
  if (!strstr(kClayTerrainLayerPs, "register(b1)") || !strstr(kClayTerrainLayerPs, "Texture2D t_colour : register(t4);") ||
      !strstr(kClayTerrainLayerPs, "Texture2D t_weights : register(t5);")) return 29;
  // The vertex shader hands every pixel shader the position it read (for terrain the world
  // position x, y, height); the two older pixel shaders do not take it.
  if (!strstr(kClayVs, "float3 world : TEXCOORD2;") || !strstr(kClayVs, "o.world = p.xyz;")) return 30;
  if (strstr(kClayPs, "TEXCOORD2") || strstr(kClayTexturedPs, "TEXCOORD2")) return 31;
  // The layer's terms, as the replay tool's --no-base variant has them (terrain_replay.py): the
  // detail colour at the world position, the weight c253.w * (tf18.w - c253.y) of the bilinear cell
  // value, the cull below c114.x, alpha = saturate(weight) * (1 - sat(d2 * c31.x + c31.y)).
  {
    const char* l = kClayTerrainLayerPs;
    const char* colour = strstr(l, "float3 colour = sample_colour(world.xy * t_detail_uv.xy + t_detail_uv.zw).rgb;");
    const char* wuv = strstr(l, "float2 wuv = world.xy * t_weight_uv.xy + t_weight_uv.zw;");
    const char* cull = strstr(l, "if (value < t_hole_ref) discard;");
    const char* weight = strstr(l, "float weight = t_weight_scale * (value - t_weight_bias);");
    const char* alpha = strstr(l, "float a = saturate(weight) * (1.0 - terrain_fade_in(world));");
    const char* fade = strstr(l, "return saturate(d2 * t_fade.x + t_fade.y);");
    if (!colour || !wuv || !cull || !weight || !alpha || !fade) return 32;
    if (!(wuv < cull && cull < weight && weight < alpha)) return 33;
    // The weight is read from component w with a linear filter at level 0.
    if (!strstr(l, "t_weights.SampleLevel(s_linear_clamp, wuv, 0).w") ||
        !strstr(l, "t_weights.SampleLevel(s_linear_wrap, wuv, 0).w")) return 34;
    // Dropped by the unlit rule: the base map (tf3) and its exponent and tint. The colour is squared as
    // the game squares it (A = tf0^2, the linear value the float target holds): once for the layer's
    // own colour and once more inside the steep-ground block's mix, below.
    {
      const char* lin = strstr(l, "float3 lin = colour * colour;");
      const char* square = strstr(l, "colour * colour");
      const char* second = square ? strstr(square + 1, "colour * colour") : nullptr;
      if (strstr(l, "pow(") || strstr(l, "t_base") || !lin || square != lin + strlen("float3 lin = ") || !second ||
          strstr(second + 1, "colour * colour") || !(colour < lin && lin < wuv)) return 35;
      if (!strstr(l, "return terrain_out(lin, a, pos, ndc);") || strstr(l, "terrain_out(colour")) return 35;
    }
    // The far shader: the far colour at the per-vertex coordinate, squared (F = tf13^2), times the
    // fade-in for the underlay (the game multiplies its linear colour by it).
    const char* f = kClayTerrainFarPs;
    {
      const char* far_colour = strstr(f, "float3 colour = sample_colour(uv).rgb;");
      const char* far_lin = strstr(f, "float3 lin = colour * colour;");
      const char* far_fade = strstr(f, "if (t_flags & 1) lin *= terrain_fade_in(world);");
      const char* far_out = strstr(f, "return terrain_out(lin, 1.0, pos, ndc);");
      if (!far_colour || !far_lin || !far_fade || !far_out ||
          !(far_colour < far_lin && far_lin < far_fade && far_fade < far_out)) return 36;
      if (strstr(f, "colour *= terrain_fade_in") || strstr(f, "terrain_out(colour")) return 36;
    }
    if (strstr(f, "t_hole_ref) discard") || strstr(f, "pow(")) return 37;  // the adaptive shader has no cull
    // Both end in the textured shader's alpha test, alpha-to-mask and output: the linear colour times
    // the square of the shade (the shade multiplies the display value), clamped to [0, 1], and the alpha,
    // which is not squared and is clamped only where it is written for the blend.
    for (const char* src : {l, f}) {
      const char* test = strstr(src, "if ((alpha_mode & 0x8) && !alpha_passes(a, alpha_ref, alpha_mode & 7)) discard;");
      const char* mask = strstr(src, "if (!(a >= mask_base - offset * per_offset)) discard;");
      const char* shade = strstr(src, "float shade = 0.75 + 0.25 * facet;");
      const char* out = strstr(src, "return float4(saturate(colour * (shade * shade)), (alpha_mode & 0x80) ? saturate(a) : 1.0);");
      if (!test || !mask || !shade || !out || !(test < mask && mask < shade && shade < out)) return 38;
      if (strstr(src, "a * a") || strstr(src, "colour * (0.75 + 0.25 * facet)")) return 38;
    }
  }
  // The steep-ground block of the layer shader (frame-map section 13, "Terrain", item 1; the reference
  // is terrain_replay.py's steep_mix and the numbers of TerrainSteepMix in test_terrain_patch.cpp). The
  // vertex shader hands on the game's vertex normal: (x, y) from the draw's per-vertex buffer, z formed
  // as the game's vertex shader forms it; (0, 0, 1) for every draw without the buffer.
  {
    const char* v = kClayVs;
    const char* decl = strstr(v, "float3 normal : TEXCOORD3;");
    const char* flat = strstr(v, "o.normal = float3(0, 0, 1);");
    const char* read = strstr(v, "float2 t = uvs[v];");
    const char* uv = strstr(v, "o.uv = t * uv_xform.xy + uv_xform.zw;");
    const char* z = strstr(v, "o.normal = float3(t, sqrt(abs(1.0 - dot(t, t))));");
    if (!decl || !flat || !read || !uv || !z || !(flat < read && read < uv && read < z)) return 39;
    if (strstr(kClayPs, "TEXCOORD3") || strstr(kClayTexturedPs, "TEXCOORD3") || strstr(kClayTerrainFarPs, "TEXCOORD3")) return 40;
    const char* l = kClayTerrainLayerPs;
    if (!strstr(l, "float3 world : TEXCOORD2, float3 normal : TEXCOORD3") || !strstr(l, "Texture2D t_side : register(t6);")) return 41;
    // One statement per term, in the shader's order: the two side projections and their derivatives
    // (outside the branch), the slope value, the branch below the threshold, the weights, the two
    // fetches (o0 with wx, o1 with wy), and the game's linear mix (no root: the target is linear).
    const char* colour = strstr(l, "float3 colour = sample_colour(world.xy * t_detail_uv.xy + t_detail_uv.zw).rgb;");
    const char* o0 = strstr(l, "float2 o0 = world.yz * t_side_uv;");
    const char* o1 = strstr(l, "float2 o1 = world.xz * t_side_uv;");
    const char* d0 = strstr(l, "float4 d0 = float4(ddx(o0), ddy(o0));");
    const char* d1 = strstr(l, "float4 d1 = float4(ddx(o1), ddy(o1));");
    const char* s = strstr(l, "float s = saturate(abs(normal.z) * t_slope.y + t_slope.x);");
    const char* branch = strstr(l, "[branch] if ((t_flags & 2) && s < t_steep_threshold) {");
    const char* inv = strstr(l, "float inv = 1.0 / (abs(normal.x) + t_steep_eps + abs(normal.y));");
    const char* wx = strstr(l, "float wx = abs(normal.x) * inv;");
    const char* wy = strstr(l, "float wy = abs(normal.y) * inv;");
    const char* side0 = strstr(l, "float3 side0 = sample_side(o0, d0).rgb;");
    const char* side1 = strstr(l, "float3 side1 = sample_side(o1, d1).rgb;");
    const char* mix = strstr(l, "lin = s * colour * colour + (1.0 - s) * (wy * side1 * side1 + wx * side0 * side0);");
    const char* cull = strstr(l, "if (value < t_hole_ref) discard;");
    if (!colour || !o0 || !o1 || !d0 || !d1 || !s || !branch || !inv || !wx || !wy || !side0 || !side1 || !mix || !cull) return 42;
    if (!(colour < o0 && o0 < d0 && o1 < d1 && d0 < branch && d1 < branch && s < branch && branch < inv && inv < wx &&
          inv < wy && wx < mix && wy < mix && side0 < mix && side1 < mix && branch < side0 && branch < side1)) return 43;
    // The fetches inside the branch take the derivatives formed outside it, with the side texture's own sampler.
    if (!strstr(l, "t_side.SampleGrad(s_point_wrap, at, d.xy, d.zw)") || !strstr(l, "t_side.SampleGrad(s_linear_wrap, at, d.xy, d.zw)") ||
        !strstr(l, "t_side.SampleGrad(s_point_clamp, at, d.xy, d.zw)") || !strstr(l, "t_side.SampleGrad(s_linear_clamp, at, d.xy, d.zw)") ||
        !strstr(l, "if (t_side_sampler == 0)")) return 44;
    if (!DisassemblyHas("clay_terrain_layer_ps", l, "ps_5_0", "sample_d") || strstr(l, "sqrt(")) return 45;
    // The far shader has no block: no side texture, no derivative fetch.
    if (strstr(kClayTerrainFarPs, "t_side :") || strstr(kClayTerrainFarPs, "sample_side") || strstr(kClayTerrainFarPs, "sqrt(") ||
        DisassemblyHas("clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0", "sample_d")) return 46;
    // The block's constants are in the table (and so at their offsets in both compiled shaders, above).
    bool side_uv = false, slope = false, threshold = false, eps = false, sampler = false;
    for (const ClayTerrainField& f : kClayTerrainFields) {
      side_uv = side_uv || (!strcmp(f.name, "t_side_uv") && f.bytes == 8);
      slope = slope || (!strcmp(f.name, "t_slope") && f.bytes == 8);
      threshold = threshold || (!strcmp(f.name, "t_steep_threshold") && f.bytes == 4);
      eps = eps || (!strcmp(f.name, "t_steep_eps") && f.bytes == 4);
      sampler = sampler || (!strcmp(f.name, "t_side_sampler") && f.bytes == 4);
    }
    if (!side_uv || !slope || !threshold || !eps || !sampler) return 47;
  }
  // The draw constants (cbuffer Draw, register b0) lie where ClayConstants has them, in the compiled
  // vertex shader and in each of the four compiled pixel shaders: the text checks above say that the
  // shaders declare the same fields, this says that the declaration is the struct's layout (144 bytes,
  // 16 variables). Controls: a source with two fields exchanged, and one with a field too few, are reported.
  {
    static_assert(sizeof(ClayConstants) == 144 && sizeof(kDrawFields) / sizeof(kDrawFields[0]) == 16);
    const struct {
      const char* name;
      const char* src;
      const char* target;
    } shaders[] = {{"clay_vs", kClayVs, "vs_5_0"},
                   {"clay_ps", kClayPs, "ps_5_0"},
                   {"clay_textured_ps", kClayTexturedPs, "ps_5_0"},
                   {"clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0"},
                   {"clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0"}};
    for (int i = 0; i < 5; ++i) {
      if (int at = DrawLayoutMismatch(shaders[i].name, shaders[i].src, shaders[i].target)) {
        // (the exit code has no room for both numbers)
        std::cerr << "cbuffer Draw of " << shaders[i].name << " is not ClayConstants: " << at
                  << (at >= 10 ? std::string(" (") + kDrawFields[at - 10].name + ")" : std::string()) << "\n";
        return 52 + i;
      }
    }
    std::string swapped = kClayTexturedPs;
    const std::string good = "uint alpha_mode; float alpha_ref;";
    size_t at = swapped.find(good);
    if (at == std::string::npos) return 48;
    swapped.replace(at, good.size(), "float alpha_ref; uint alpha_mode;");
    if (DrawLayoutMismatch("swapped_draw_ps", swapped.c_str(), "ps_5_0") != 10 + 11) return 49;  // alpha_mode
    std::string shorter = kClayTexturedPs;
    const std::string tail = "float alpha_scale; uint3 pad2;";
    at = shorter.find(tail);
    if (at == std::string::npos) return 50;
    shorter.replace(at, tail.size(), "float alpha_scale;");
    if (DrawLayoutMismatch("shorter_draw_ps", shorter.c_str(), "ps_5_0") != 4) return 51;
  }
  // The linear target (frame-map section 14, "Linear target"). Every clay pixel shader writes the
  // game's linear value: the square of the display value it wrote before, shade included (the shade
  // multiplied the display value), clamped to [0, 1] as the 8-bit target clamped it. Alpha is not
  // squared; the alpha test and alpha-to-mask read it as it is and come before the output; it is
  // clamped only where it is written for the blend (a float target does not clamp it).
  {
    const char* helper = "float3 clay_linear(float3 d) { d = saturate(d); return d * d; }";
    if (!strstr(kClayPs, helper) || !strstr(kClayTexturedPs, helper)) return 60;
    if (!strstr(kClayPs, "return float4(clay_linear(base * shade), 1.0);") ||
        strstr(kClayPs, "return float4(base * shade, 1.0);")) return 61;
    const char* t = kClayTexturedPs;
    const char* test = strstr(t, "!alpha_passes(a, alpha_ref, alpha_mode & 7)");
    const char* mask = strstr(t, "if (!(a >= mask_base - offset * per_offset)) discard;");
    const char* out = strstr(t, "return float4(clay_linear(t.rgb * (0.75 + 0.25 * facet)), (alpha_mode & 0x80) ? saturate(a) : 1.0);");
    const char* flat = strstr(t, "return float4(clay_linear(base * (0.35 + 0.65 * facet)), 1.0);");
    if (!test || !mask || !out || !flat || !(test < mask && mask < out && out < flat)) return 62;
    if (strstr(t, "return float4(t.rgb *") || strstr(t, "return float4(base *") || strstr(t, "a * a")) return 63;
    // The square root is the output mapping's alone: no clay pixel shader has one, in text or compiled.
    const struct {
      const char* name;
      const char* src;
    } clay_ps[] = {{"clay_ps", kClayPs},
                   {"clay_textured_ps", kClayTexturedPs},
                   {"clay_terrain_layer_ps", kClayTerrainLayerPs},
                   {"clay_terrain_far_ps", kClayTerrainFarPs}};
    for (const auto& ps : clay_ps) {
      if (strstr(ps.src, "sqrt(") || DisassemblyHas(ps.name, ps.src, "ps_5_0", "sqrt")) return 64;
    }
    // The clamps that guard the float target survive compiling where the value is not bounded by
    // construction: the terrain layer's colour (the steep-ground mix divides) and the textured draw's
    // colour and written alpha. (The compiler drops the clamp of flat clay and keeps the far colour's
    // upper bound only: it proves those ranges.)
    if (!DisassemblyHas("clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0", "mul_sat o0.xyz") ||
        !DisassemblyHas("clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0", "min o0.xyz") ||
        !DisassemblyHas("clay_textured_ps", kClayTexturedPs, "ps_5_0", "mul_sat r") ||
        !DisassemblyHas("clay_textured_ps", kClayTexturedPs, "ps_5_0", "mov_sat r")) return 69;
  }
  // The output mapping: one full-screen step from the linear target to the 8-bit target the composite
  // samples, display = sqrt(saturate(linear * exposure)), alpha 1 (OutputMap in clay_logic.h is the same
  // rule). The linear target is read texel for texel (no sampler). A negative value, a NaN and
  // 0 * infinity give 0 by a comparison and a select, which must survive compiling (a compiler may
  // fold what it takes for a clamp).
  {
    const char* m = kOutputMapPs;
    if (!Compile("output_map_ps", m, "ps_5_0")) return 65;
    const char* buffer = strstr(m, "cbuffer Map : register(b0) { float exposure; float3 map_pad; };");
    const char* texture = strstr(m, "Texture2D<float4> linear_target : register(t0);");
    const char* load = strstr(m, "float3 v = linear_target.Load(int3(pos.xy, 0)).rgb * exposure;");
    const char* guard = strstr(m, "v = (v > 0.0) ? min(v, 1.0) : float3(0.0, 0.0, 0.0);");
    const char* root = strstr(m, "return float4(sqrt(v), 1.0);");
    if (!buffer || !texture || !load || !guard || !root || !(load < guard && guard < root)) return 66;
    if (strstr(m, "Sample") || strstr(m, "SamplerState")) return 67;
    // (the compiler makes the select a comparison and a mask: lt, then and)
    if (!DisassemblyHas("output_map_ps", m, "ps_5_0", "sqrt o0.xyz") || !DisassemblyHas("output_map_ps", m, "ps_5_0", "lt r") ||
        !DisassemblyHas("output_map_ps", m, "ps_5_0", "and r") || !DisassemblyHas("output_map_ps", m, "ps_5_0", "ld_indexable") ||
        DisassemblyHas("output_map_ps", m, "ps_5_0", "sample")) return 68;
    if (int at = MapLayoutMismatch("output_map_ps", m)) return 70 + at;  // 71 to 76
    // Control: a source whose exposure is not the buffer's first word is reported.
    std::string moved = m;
    const std::string good = "float exposure; float3 map_pad;";
    const size_t at = moved.find(good);
    if (at == std::string::npos) return 77;
    moved.replace(at, good.size(), "float3 map_pad; float exposure;");
    if (MapLayoutMismatch("moved_map_ps", moved.c_str()) != 5) return 78;
    // The composite still samples the mapped 8-bit result as it sampled the clay target, and maps nothing.
    if (!strstr(kCompositePs, "return float4(clay.Sample(s, uv).rgb, alpha);") || strstr(kCompositePs, "sqrt")) return 79;
  }
  // Negative control: broken HLSL must fail (proves the harness reports errors).
  if (Compile("broken_ps", "float4 main() : SV_Target { return undefined_symbol; }", "ps_5_0"))
    return 4;
  std::cout << "PASS: fullscreen VS, pattern PS, grid PS, clay VS/PS/textured PS, composite PS compile; alpha fields declared, alpha test discards; terrain layer and far PS compile, discard, constants at the table's offsets; steep-ground block; draw constants at ClayConstants' offsets in all five clay shaders; linear outputs, output mapping; broken shader rejected\n";
  return 0;
}
