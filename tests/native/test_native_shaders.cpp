// Compiles the native renderer's HLSL with D3DCompile (no GPU needed).
#include "../../src/native/fable2_native_shaders.h"
#include "../../src/native/render/clay_logic.h"  // ClayConstants (before windows.h: its min and max macros)
#include <windows.h>
#include <initguid.h>  // before d3d11shader.h: defines IID_ID3D11ShaderReflection here
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <cstdlib>
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

// 0 when `src` compiles for `target` and its cbuffer `buffer` has exactly the variables of `fields`, each
// at the table's byte offset with the table's size, in a buffer of `size` bytes. Otherwise a number that
// says what differs: 1 no compile, 2 no reflection, 3 no such buffer, 4 another size or variable count,
// 10 + i for the table's entry i.
static int BufferLayoutMismatch(const char* name, const char* src, const char* target, const char* buffer,
                                const fable2::native::shaders::ClayBufferField* fields, unsigned count,
                                unsigned size) {
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
    ID3D11ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByName(buffer);
    D3D11_SHADER_BUFFER_DESC bd{};
    if (!cb || FAILED(cb->GetDesc(&bd))) {
      result = 3;
    } else if (bd.Size != size || bd.Variables != count) {
      result = 4;
    } else {
      for (unsigned i = 0; i < count && !result; ++i) {
        ID3D11ShaderReflectionVariable* var = cb->GetVariableByName(fields[i].name);
        D3D11_SHADER_VARIABLE_DESC vd{};
        if (!var || FAILED(var->GetDesc(&vd)) || vd.StartOffset != fields[i].offset || vd.Size != fields[i].bytes) {
          result = 10 + int(i);
        }
      }
    }
  }
  if (refl) refl->Release();
  code->Release();
  return result;
}

// The terrain constants: cbuffer Terrain against kClayTerrainFields, 192 bytes.
static int TerrainLayoutMismatch(const char* name, const char* src) {
  using namespace fable2::native::shaders;
  return BufferLayoutMismatch(name, src, "ps_5_0", "Terrain", kClayTerrainFields,
                              sizeof(kClayTerrainFields) / sizeof(kClayTerrainFields[0]), 192);
}

// The register a resource of the compiled shader is bound at, -1 when the shader binds no resource of
// that name (it is declared and not used, or not declared), -2 when the shader does not compile.
static int BindPoint(const char* name, const char* src, const char* target, const char* resource) {
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  HRESULT hr = D3DCompile(src, strlen(src), name, nullptr, nullptr, "main", target,
                          D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
  if (errors) errors->Release();
  if (FAILED(hr) || !code) return -2;
  ID3D11ShaderReflection* refl = nullptr;
  int result = -1;
  D3D11_SHADER_INPUT_BIND_DESC desc{};
  if (SUCCEEDED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_ID3D11ShaderReflection,
                           reinterpret_cast<void**>(&refl))) && refl &&
      SUCCEEDED(refl->GetResourceBindingDescByName(resource, &desc))) {
    result = int(desc.BindPoint);
  }
  if (refl) refl->Release();
  code->Release();
  return result;
}

// The disassembly of the compiled `src`, or an empty string when it does not compile.
static std::string Disassembly(const char* name, const char* src, const char* target) {
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  ID3DBlob* text = nullptr;
  std::string listing;
  HRESULT hr = D3DCompile(src, strlen(src), name, nullptr, nullptr, "main", target, 0, 0, &code, &errors);  // (the runtime's flags)
  if (SUCCEEDED(hr) && code &&
      SUCCEEDED(D3DDisassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &text)) && text) {
    listing.assign(static_cast<const char*>(text->GetBufferPointer()), text->GetBufferSize());
  }
  if (code) code->Release();
  if (errors) errors->Release();
  if (text) text->Release();
  return listing;
}

// Instructions of a listing that can run for long or jump: loops, calls and labels. A clay shader has none:
// nothing in it iterates, so no constant can make it run long.
static int LoopInstructions(const std::string& listing) {
  int count = 0;
  size_t at = 0;
  while (at < listing.size()) {
    size_t end = listing.find('\n', at);
    if (end == std::string::npos) end = listing.size();
    size_t first = listing.find_first_not_of(" \t", at);
    if (first < end && listing[first] != '/') {
      for (const char* op : {"loop", "endloop", "call", "callc", "label", "break", "breakc", "continue", "continuec", "switch"}) {
        const size_t n = strlen(op);
        if (listing.compare(first, n, op) == 0 && (first + n >= end || listing[first + n] == ' ' || listing[first + n] == '\r')) ++count;
      }
    }
    at = end + 1;
  }
  return count;
}

// How the compiled shader loads from the structured buffer at register `reg`: -1 it does not, 0 at least
// one load is outside every conditional block (it runs for every vertex of every draw), 1 every load is
// inside an `if` block (it runs only where the block's condition holds).
static int BufferLoadGuard(const std::string& listing, int reg) {
  const std::string needle = ", t" + std::to_string(reg) + ".";
  int depth = 0, loads = 0, unguarded = 0;
  size_t at = 0;
  while (at < listing.size()) {
    size_t end = listing.find('\n', at);
    if (end == std::string::npos) end = listing.size();
    const size_t first = listing.find_first_not_of(" \t", at);
    if (first < end && listing[first] != '/') {
      const std::string line = listing.substr(first, end - first);
      if (line.compare(0, 3, "if_") == 0) ++depth;
      if (line.compare(0, 5, "endif") == 0) --depth;
      if (line.compare(0, 13, "ld_structured") == 0 && line.find(needle) != std::string::npos) {
        ++loads;
        if (depth <= 0) ++unguarded;
      }
    }
    at = end + 1;
  }
  return loads == 0 ? -1 : unguarded ? 0 : 1;
}

// Whether the compiled shader has an instruction `op` (the first word of an instruction line).
static bool HasOp(const std::string& listing, const char* op) {
  const size_t n = strlen(op);
  size_t at = 0;
  while (at < listing.size()) {
    size_t end = listing.find('\n', at);
    if (end == std::string::npos) end = listing.size();
    const size_t first = listing.find_first_not_of(" \t", at);
    if (first < end && listing.compare(first, n, op) == 0 && first + n < end && listing[first + n] == ' ') return true;
    at = end + 1;
  }
  return false;
}

// The smallest number of nested `if` blocks around a load from the structured buffer at register `reg`
// (0: a load outside every block; -1: no load at all).
static int InnerLoadDepth(const std::string& listing, int reg) {
  const std::string needle = ", t" + std::to_string(reg) + ".";
  int depth = 0, least = -1;
  size_t at = 0;
  while (at < listing.size()) {
    size_t end = listing.find('\n', at);
    if (end == std::string::npos) end = listing.size();
    const size_t first = listing.find_first_not_of(" \t", at);
    if (first < end && listing[first] != '/') {
      const std::string line = listing.substr(first, end - first);
      if (line.compare(0, 3, "if_") == 0) ++depth;
      if (line.compare(0, 5, "endif") == 0) --depth;
      if (line.compare(0, 13, "ld_structured") == 0 && line.find(needle) != std::string::npos) {
        least = least < 0 ? depth : (depth < least ? depth : least);
      }
    }
    at = end + 1;
  }
  return least;
}

// The numbers of nested `if` blocks around each load from the structured buffer at register `reg`, in
// the listing's order, as text ("1" one load in one block; "2,1" two loads, the first inside two blocks;
// "" no load).
static std::string LoadDepths(const std::string& listing, int reg) {
  const std::string needle = ", t" + std::to_string(reg) + ".";
  std::string depths;
  int depth = 0;
  size_t at = 0;
  while (at < listing.size()) {
    size_t end = listing.find('\n', at);
    if (end == std::string::npos) end = listing.size();
    const size_t first = listing.find_first_not_of(" \t", at);
    if (first < end && listing[first] != '/') {
      const std::string line = listing.substr(first, end - first);
      if (line.compare(0, 3, "if_") == 0) ++depth;
      if (line.compare(0, 5, "endif") == 0) --depth;
      if (line.compare(0, 13, "ld_structured") == 0 && line.find(needle) != std::string::npos) {
        depths += (depths.empty() ? "" : ",") + std::to_string(depth);
      }
    }
    at = end + 1;
  }
  return depths;
}

// Whether the compiled shader has an instruction or declaration `op` (the line's first word) whose line
// contains `needle`.
static bool HasLineWith(const std::string& listing, const char* op, const char* needle) {
  const size_t n = strlen(op);
  size_t at = 0;
  while (at < listing.size()) {
    size_t end = listing.find('\n', at);
    if (end == std::string::npos) end = listing.size();
    const size_t first = listing.find_first_not_of(" \t", at);
    if (first < end && listing.compare(first, n, op) == 0 && first + n < end && listing[first + n] == ' ' &&
        listing.substr(first, end - first).find(needle) != std::string::npos) {
      return true;
    }
    at = end + 1;
  }
  return false;
}

// The register of the compiled shader's output (`output`) or input parameter TEXCOORD<index> with its
// component mask in `mask`; -1 when it has none, -2 when the shader does not compile or reflect.
static int TexcoordRegister(const char* name, const char* src, const char* target, bool output, unsigned index, unsigned* mask) {
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  HRESULT hr = D3DCompile(src, strlen(src), name, nullptr, nullptr, "main", target, 0, 0, &code, &errors);
  if (errors) errors->Release();
  if (FAILED(hr) || !code) return -2;
  ID3D11ShaderReflection* refl = nullptr;
  int result = -2;
  D3D11_SHADER_DESC sd{};
  if (SUCCEEDED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_ID3D11ShaderReflection,
                           reinterpret_cast<void**>(&refl))) && refl && SUCCEEDED(refl->GetDesc(&sd))) {
    result = -1;
    const unsigned count = output ? sd.OutputParameters : sd.InputParameters;
    for (unsigned i = 0; i < count; ++i) {
      D3D11_SIGNATURE_PARAMETER_DESC pd{};
      if (FAILED(output ? refl->GetOutputParameterDesc(i, &pd) : refl->GetInputParameterDesc(i, &pd))) continue;
      if (pd.SemanticName && !strcmp(pd.SemanticName, "TEXCOORD") && pd.SemanticIndex == index) {
        result = int(pd.Register);
        *mask = pd.Mask;
      }
    }
  }
  if (refl) refl->Release();
  code->Release();
  return result;
}

// Whether the compiled shader reads the constant buffer `block` ("cb1", "cb2") in any instruction.
static bool ReadsBlock(const std::string& listing, const char* block) {
  const std::string needle = std::string(block) + "[";
  size_t at = 0;
  while (at < listing.size()) {
    size_t end = listing.find('\n', at);
    if (end == std::string::npos) end = listing.size();
    const size_t first = listing.find_first_not_of(" \t", at);
    if (first < end && listing[first] != '/' && listing.compare(first, 4, "dcl_") != 0 &&
        listing.substr(first, end - first).find(needle) != std::string::npos) {
      return true;
    }
    at = end + 1;
  }
  return false;
}

// Whether the compiled shader samples or loads the texture or buffer at register t<reg>.
static bool ReadsResource(const std::string& listing, int reg) {
  const std::string needle = ", t" + std::to_string(reg) + ".";
  size_t at = 0;
  while (at < listing.size()) {
    size_t end = listing.find('\n', at);
    if (end == std::string::npos) end = listing.size();
    const size_t first = listing.find_first_not_of(" \t", at);
    if (first < end && listing[first] != '/' && listing.compare(first, 4, "dcl_") != 0 &&
        listing.substr(first, end - first).find(needle) != std::string::npos) {
      return true;
    }
    at = end + 1;
  }
  return false;
}

// Whether every structured-buffer load of the compiled shader is from one of the registers of `allowed`
// (bit r: register t<r>): no load from a register the caller does not name.
static bool LoadsOnlyFrom(const std::string& listing, unsigned allowed) {
  size_t at = 0;
  while (at < listing.size()) {
    size_t end = listing.find('\n', at);
    if (end == std::string::npos) end = listing.size();
    const size_t first = listing.find_first_not_of(" \t", at);
    if (first < end && listing.compare(first, 13, "ld_structured") == 0) {
      const std::string line = listing.substr(first, end - first);
      bool named = false;
      for (unsigned reg = 0; reg < 32; ++reg) {
        if ((allowed >> reg) & 1u) named = named || line.find(", t" + std::to_string(reg) + ".") != std::string::npos;
      }
      if (!named) return false;
    }
    at = end + 1;
  }
  return true;
}

// The number of 16-byte registers the compiled shader declares of constant buffer `index` (the
// `dcl_constantbuffer CB<index>[N]` line: the shader reads no register of the buffer from N on), with
// `dynamic` whether it indexes the buffer by a computed value. 0 when it declares no such buffer.
static unsigned DeclaredRegisters(const std::string& listing, int index, bool* dynamic = nullptr) {
  const std::string needle = "dcl_constantbuffer CB" + std::to_string(index) + "[";
  const size_t at = listing.find(needle);
  if (dynamic) *dynamic = false;
  if (at == std::string::npos) return 0;
  size_t end = listing.find('\n', at);
  if (end == std::string::npos) end = listing.size();
  if (dynamic) *dynamic = listing.substr(at, end - at).find("dynamicIndexed") != std::string::npos;
  return unsigned(std::strtoul(listing.c_str() + at + needle.size(), nullptr, 10));
}

// The clay shader `src` with the draw's light_mode a compile-time constant instead of the root word: the
// program a draw with that light_mode runs, with everything it cannot reach folded away by the compiler.
// Empty when the cbuffer's text is not found (the caller fails).
static std::string WithLightMode(const char* src, unsigned mode) {
  std::string text = src;
  const std::string field = "uint light_mode; uint light_count; uint pad2; };";
  const size_t at = text.find(field);
  if (at == std::string::npos) return {};
  text.replace(at, field.size(), "uint light_mode_word; uint light_count; uint pad2; };\nstatic const uint light_mode = " +
                                     std::to_string(mode) + ";");
  return text;
}

// Everything between a clay shader's inputs and its lit output as pinned text, exactly as in
// fable2_native_shaders.h: the tests above find statements by their text and so cannot see a statement
// that is added between them, before them, or inside a function they call. A change to a shader's
// light path has to be made here as well, on purpose. Six pieces:
//   kPinLight        the shared light block: cbuffer Frame and every light function, light_normalize to
//                    clay_lit, the plants' among them (in all four shaders that light);
//   kPinVsMain       the vertex shader's whole main (position, coordinates, the terrain normal, the
//                    object's normal, the vector of t, the stream colour and the probe colour; an
//                    instanced plant's normal, bend and colour);
//   kPinTexturedMain the textured pixel shader from clay_linear to the end of main (the object's and
//                    the plants' lit paths);
//   kPinTerrainCommon the terrain shaders' functions: sample_colour, sample_base, terrain_fade_in,
//                    alpha test and output (terrain_out);
//   kPinLayerMain    the layer shader from sample_side to the end of main;
//   kPinFarMain      the far shader's whole main.
// Outside the pins: the declarations (cbuffers, textures, samplers, buffers; held by reflection against
// their tables and the layout) and alpha_passes (it decides a discard, not a colour).
static const char kPinLight[] = R"pin(cbuffer Frame : register(b2) {
  float3 f_sun_dir; uint f_light_on;     // c80.xyz of a one-light draw (= c109.xyz), as it is; 1: the frame is lit
  float3 f_sun_col; float f_tint_pow;    // c87.xyz; c21.w
  float3 f_tint; float f_pad3;           // c21.xyz (the exposure is the output mapping's alone: cbuffer Map)
  float3 f_add; float f_pad0;            // c20.xyz (c20.w, the output scale, is not applied)
  float3 f_floor; float f_pad1;          // c22.xyz
  float3 f_axis; float f_pad2;           // c23.xyz (= vertex c19.xyz)
};
float3 light_normalize(float3 v) {
  float n2 = dot(v, v);
  return (n2 > 0.0 && n2 < 3.0e38) ? v * rsqrt(n2) : float3(0.0, 0.0, 0.0);
}
float3 light_pow(float3 base, float exponent) {
  float3 b = abs(base);
  return (b > 0.0) ? exp2(exponent * log2(b)) : float3(0.0, 0.0, 0.0);
}
float3 AmbientFactor(float3 amb_floor, float t) {
  t = saturate(t);
  return amb_floor + (1.0 - amb_floor) * (t * t);
}
float3 AmbientAxis(float3 normal, float3 axis) {
  float k = 0.5 + 0.5 * dot(normal, axis);
  return normal * (1.0 - k) + axis * k;
}
float AmbientT(float3 axis_vector, float3 normal) {
  return saturate(dot(light_normalize(axis_vector), light_normalize(normal)));
}
float FacingGate(float n_dot_c109, float k) { return saturate(k * n_dot_c109); }
float SunGateSingle(float mask, float facing) { return saturate(facing) * saturate(mask); }
float3 light_ambient(float3 base, float3 tint, float tint_pow, float3 add, float3 amb_floor, float t) {
  return light_pow(base, tint_pow) * tint * AmbientFactor(amb_floor, t) + add;
}
float3 light_sun(float3 sun_col, float n_dot_sun, float sun_gate) {
  return sun_col * (saturate(n_dot_sun) * sun_gate);
}
float3 LightTerrainLayer(float3 A, float3 baked, float3 tint, float tint_pow, float3 add, float3 amb_floor, float t,
                         float n_dot_sun, float3 sun_col, float c11x, float sun_gate) {
  return A * (c11x * light_ambient(baked, tint, tint_pow, add, amb_floor, t) + light_sun(sun_col, n_dot_sun, sun_gate));
}
float3 LightTerrainFar(float3 F, float3 baked, float3 tint, float tint_pow, float3 add, float3 amb_floor, float t,
                       float n_dot_sun, float3 sun_col, float c11x) {
  return F * (light_ambient(baked, tint, tint_pow, add, amb_floor, t) + c11x * light_sun(sun_col, n_dot_sun, 1.0));
}
float3 LightObject(float3 A, float3 vcolour, float3 tint, float tint_pow, float3 add, float3 amb_floor, float t,
                   float n_dot_sun, float3 sun_col, float c27w, float sun_gate, float3 c49) {
  return A * (c27w * light_ambient(vcolour, tint, tint_pow, add, amb_floor, t) + light_sun(sun_col, n_dot_sun, sun_gate)) * c49;
}
// The instanced plants (lighting-formulas.md, "Instanced plants"; the mirrors are in clay_logic.h). The
// game's vertex shaders make the colour of the vertex's copy and the bend of the normal, its pixel
// shaders the bent normal and the colour: the functions are split the same way.
float3 InstanceAmbient(float3 instance_colour, float3 tint, float tint_pow, float3 add) {
  return light_pow(instance_colour, tint_pow) * tint + add;
}
float PlantHeight(float z, float scale, float offset) { return saturate(z * scale + offset); }
float4 PlantBend(float3 normal, float3 up, float height, float strength) {
  float h = saturate(height);
  return float4(light_normalize(normal + (up - normal) * h), h) * strength;
}
float3 PlantNormal(float3 normal, float4 bend) {
  float3 n = light_normalize(normal);
  return light_normalize(n + (bend.xyz - n) * saturate(bend.w));
}
float3 LightPlant(float3 A, float3 vs_ambient, float n_dot_sun, float3 sun_col, float sun_gate) {
  return A * (vs_ambient + light_sun(sun_col, n_dot_sun, sun_gate));
}
float3 LightPlantLit(float3 A, float3 vs_base, float3 add, float3 amb_floor, float t, float n_dot_sun, float3 sun_col,
                     float sun_gate) {
  return A * (vs_base * AmbientFactor(amb_floor, t) + add + light_sun(sun_col, n_dot_sun, sun_gate));
}
// What a lit draw writes: the value itself, a number, not negative, at most 64 (ClayLit in clay_logic.h).
float3 clay_lit(float3 v) { return (v > 0.0) ? min(v, 64.0) : float3(0.0, 0.0, 0.0); }
)pin";
static const char kPinVsMain[] = R"pin(VsOut main(uint vid : SV_VertexID) {
  VsOut o;
  int v = int(indices[vid]) + base_vertex;
  bool in_range = v >= 0 && uint(v) < vertex_count;
  float4 p = in_range ? positions[v] : float4(0, 0, 0, 0);
  float4 c = layout == 0 ? float4(dot(r0, p), dot(r1, p), dot(r2, p), dot(r3, p))
                         : p.x * r0 + p.y * r1 + p.z * r2 + p.w * r3;
  float3 to_eye = cut.xyz - p.xyz;
  if (dot(to_eye, to_eye) > cut.w) c = asfloat(uint4(0x7FC00000, 0x7FC00000, 0x7FC00000, 0x7FC00000));
  o.pos = c;
  o.ndc = float3(c.xy / max(abs(c.w), 1e-6), c.w * 0.01);
  o.uv = float2(0, 0);
  if (textured != 0 && in_range) o.uv = uvs[v] * uv_xform.xy + uv_xform.zw;
  o.world = p.xyz;
  // An object's normal without its buffer is no direction (no sun, the ambient at its floor); any
  // other draw's is the flat (0, 0, 1) of a draw without the steep-ground block.
  o.normal = (light_mode & 2) ? float3(0, 0, 0) : float3(0, 0, 1);
  o.axis = float3(0, 0, 0);
  o.vcolour = float3(0, 0, 0);
  o.bend = float4(0, 0, 0, 0);
  bool has_light = in_range && uint(v) < light_count;
  [branch] if ((light_mode & 1) && has_light) {
    // The terrain normal as the game's vertex shaders form it, not normalised (the pixel shader does).
    float2 t = terrain_normals[v];
    o.normal = float3(t, sqrt(abs(1.0 - dot(t, t))));
    if (light_mode & 4) o.axis = AmbientAxis(o.normal, f_axis);
  }
  // (18: the object bit and the instance bit. An object or trunk has the first alone.)
  [branch] if ((light_mode & 18) == 2 && has_light) {
    // The object's normal as the game's vertex shader exports it: (R n) * c7.w, not normalised.
    float3 n = normals[v];
    o.normal = float3(dot(o_rot0.xyz, n), dot(o_rot1.xyz, n), dot(o_rot2.xyz, n));
    o.axis = AmbientAxis(o.normal, f_axis);
    // t10 holds one word a vertex: the stream colour, or (light_mode bit 3) the vertex's probe set.
    uint word = colours[v];
    [branch] if (light_mode & 8) {
      // The probe colour as the game's vertex shaders form it, with no power:
      // c[28 + a0].xyz + (c[29 + a0] . w, c[30 + a0] . w, c[31 + a0] . w), a0 clamped to the last set.
      uint a0 = min(word, 8u);
      float4 w = probe_weights[v];
      o.vcolour = o_probe[a0].xyz + float3(dot(o_probe[a0 + 1], w), dot(o_probe[a0 + 2], w), dot(o_probe[a0 + 3], w));
    } else {
      o.vcolour = light_pow(float3(word & 255, (word >> 8) & 255, (word >> 16) & 255) / 255.0, o_colour_power);
    }
  }
  [branch] if ((light_mode & 18) == 18 && has_light) {
    // An instanced plant. Its normal is in the world already (normalize(M n) of its copy's rows): no
    // rows here. t10 holds the colour of the vertex's copy, t11 the copy's up axis (xyz) and the
    // vertex's height in the copy (w); the draw's bend constants c45.x, .y, .z are the fourth
    // components of the three rows. As the game's plant vertex shaders: the bend of the normal, and
    // the colour raised to c18.w (2.2 * c21.w) times c18.xyz (c21.xyz), plus c54.xyz (c20.xyz) where
    // the pixel shader has no ambient factor; where it has one (light_mode bit 5), the factor's
    // vector and no added light.
    float3 n = normals[v];
    o.normal = n;
    uint tone = colours[v];
    float4 b = probe_weights[v];
    o.bend = PlantBend(n, b.xyz, PlantHeight(b.w, o_rot1.w, o_rot2.w), o_rot0.w);
    float3 colour = float3(tone & 255, (tone >> 8) & 255, (tone >> 16) & 255) / 255.0;
    [branch] if (light_mode & 32) {
      o.axis = AmbientAxis(n, f_axis);
      o.vcolour = InstanceAmbient(colour, f_tint, o_colour_power * f_tint_pow, float3(0, 0, 0));
    } else {
      o.axis = float3(0, 0, 0);
      o.vcolour = InstanceAmbient(colour, f_tint, o_colour_power * f_tint_pow, f_add);
    }
  }
  return o;
})pin";
static const char kPinTexturedMain[] = R"pin(float3 clay_linear(float3 d) { d = saturate(d); return d * d; }
float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0, float2 uv : TEXCOORD1, float3 world : TEXCOORD2,
            float3 normal : TEXCOORD3, float3 axis : TEXCOORD4, float3 vcolour : TEXCOORD5,
            float4 bend : TEXCOORD6) : SV_Target {
  float3 n = normalize(cross(ddx(ndc), ddy(ndc)));
  float facet = saturate(abs(dot(n, normalize(float3(0.4, 0.6, -0.7)))));
  float3 base;
  if (textured != 0) {
    float4 t;
    if (sampler_index == 0) t = albedo.Sample(s_point_wrap, uv);
    else if (sampler_index == 1) t = albedo.Sample(s_linear_wrap, uv);
    else if (sampler_index == 2) t = albedo.Sample(s_point_clamp, uv);
    else t = albedo.Sample(s_linear_clamp, uv);
    if (alpha_mode & 0x200) t.rgb = saturate(t.rgb / max(t.a, 1.0 / 255.0));
    float a = alpha_scale;
    if (alpha_mode & 0x10) {
      uint comp = (alpha_mode >> 5) & 3;
      a *= comp == 0 ? t.x : comp == 1 ? t.y : comp == 2 ? t.z : t.w;
    }
    if ((alpha_mode & 0x8) && !alpha_passes(a, alpha_ref, alpha_mode & 7)) discard;
    if (alpha_mode & 0x100) {
      uint2 p = uint2(pos.xy);
      uint index = ((p.x & 1) << 1) | (p.y & 1);
      uint offset = (alpha_mode >> (16 + 2 * index)) & 3;
      uint samples = (alpha_mode >> 24) & 3;
      float mask_base = (samples == 1 || samples == 2) ? 0.5 : 1.0;
      float per_offset = samples == 1 ? 0.125 : samples == 2 ? 0.0625 : 0.25;
      if (!(a >= mask_base - offset * per_offset)) discard;
    }
    if (light_mode & 4) {
      float3 lit;
      [branch] if (light_mode & 16) {
        // An instanced plant: the normal bent as the game's plant pixel shaders bend it, then A times
        // the vertex side's ambient plus the sun, or (light_mode bit 5) the object form with the vertex
        // side's colour, c27.w = 1 and c49 = 1. These shaders read no c26.
        float3 Np = PlantNormal(normal, bend);
        if (light_mode & 32) {
          lit = LightPlantLit(clay_linear(t.rgb), vcolour, f_add, f_floor, AmbientT(axis, Np), dot(Np, f_sun_dir), f_sun_col,
                              SunGateSingle(1.0, 1.0));
        } else {
          lit = LightPlant(clay_linear(t.rgb), vcolour, dot(Np, f_sun_dir), f_sun_col, SunGateSingle(1.0, 1.0));
        }
      } else {
        float3 N = light_normalize(normal);
        lit = LightObject(clay_linear(t.rgb), vcolour, f_tint, f_tint_pow, f_add, f_floor, AmbientT(axis, normal),
                          dot(N, f_sun_dir), f_sun_col, o_c27w, SunGateSingle(1.0, 1.0), o_c49) * o_c26;
      }
      return float4(clay_lit(lit), (alpha_mode & 0x80) ? saturate(a) : 1.0);
    }
    return float4(clay_linear(t.rgb * (0.75 + 0.25 * facet)), (alpha_mode & 0x80) ? saturate(a) : 1.0);
  }
  base = float3((color >> 16) & 255, (color >> 8) & 255, color & 255) / 255.0;
  return float4(clay_linear(base * (0.35 + 0.65 * facet)), 1.0);
})pin";
static const char kPinTerrainCommon[] = R"pin(float4 sample_colour(float2 at) {
  if (t_colour_sampler == 0) return t_colour.Sample(s_point_wrap, at);
  if (t_colour_sampler == 1) return t_colour.Sample(s_linear_wrap, at);
  if (t_colour_sampler == 2) return t_colour.Sample(s_point_clamp, at);
  return t_colour.Sample(s_linear_clamp, at);
}
// The base map at `at` with the screen derivatives d = (ddx, ddy) of the coordinate (the fetch is
// inside the light's branch, the derivatives are formed outside it).
float4 sample_base(float2 at, float4 d) {
  if (t_base_sampler == 0) return t_base.SampleGrad(s_point_wrap, at, d.xy, d.zw);
  if (t_base_sampler == 1) return t_base.SampleGrad(s_linear_wrap, at, d.xy, d.zw);
  if (t_base_sampler == 2) return t_base.SampleGrad(s_point_clamp, at, d.xy, d.zw);
  return t_base.SampleGrad(s_linear_clamp, at, d.xy, d.zw);
}
// sat(d2 * c31.x + c31.y): 0 where the layers are whole, 1 where they are gone.
float terrain_fade_in(float3 world) {
  float4 p = float4(world, 1.0);
  float3 e = t_eye - float3(dot(t_world0, p), dot(t_world1, p), dot(t_world2, p));
  float d2 = dot(e, e);
  return saturate(d2 * t_fade.x + t_fade.y);
}
// kClayTexturedPs's alpha test, alpha-to-mask and output for a linear colour and an alpha.
float4 terrain_out(float3 colour, float a, float4 pos, float3 ndc) {
  float3 n = normalize(cross(ddx(ndc), ddy(ndc)));
  float facet = saturate(abs(dot(n, normalize(float3(0.4, 0.6, -0.7)))));
  if ((alpha_mode & 0x8) && !alpha_passes(a, alpha_ref, alpha_mode & 7)) discard;
  if (alpha_mode & 0x100) {
    uint2 p = uint2(pos.xy);
    uint index = ((p.x & 1) << 1) | (p.y & 1);
    uint offset = (alpha_mode >> (16 + 2 * index)) & 3;
    uint samples = (alpha_mode >> 24) & 3;
    float mask_base = (samples == 1 || samples == 2) ? 0.5 : 1.0;
    float per_offset = samples == 1 ? 0.125 : samples == 2 ? 0.0625 : 0.25;
    if (!(a >= mask_base - offset * per_offset)) discard;
  }
  if (light_mode & 4) return float4(clay_lit(colour), (alpha_mode & 0x80) ? saturate(a) : 1.0);
  float shade = 0.75 + 0.25 * facet;
  return float4(saturate(colour * (shade * shade)), (alpha_mode & 0x80) ? saturate(a) : 1.0);
}
)pin";
static const char kPinLayerMain[] = R"pin(float4 sample_side(float2 at, float4 d) {
  if (t_side_sampler == 0) return t_side.SampleGrad(s_point_wrap, at, d.xy, d.zw);
  if (t_side_sampler == 1) return t_side.SampleGrad(s_linear_wrap, at, d.xy, d.zw);
  if (t_side_sampler == 2) return t_side.SampleGrad(s_point_clamp, at, d.xy, d.zw);
  return t_side.SampleGrad(s_linear_clamp, at, d.xy, d.zw);
}
float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0, float2 uv : TEXCOORD1,
            float3 world : TEXCOORD2, float3 normal : TEXCOORD3, float3 axis : TEXCOORD4) : SV_Target {
  float3 colour = sample_colour(world.xy * t_detail_uv.xy + t_detail_uv.zw).rgb;
  float3 lin = colour * colour;
  // The steep-ground block. The side projections and their derivatives are formed for every pixel,
  // so that the two fetches can stay inside the branch; the base map's coordinate likewise.
  float2 o0 = world.yz * t_side_uv;
  float2 o1 = world.xz * t_side_uv;
  float4 d0 = float4(ddx(o0), ddy(o0));
  float4 d1 = float4(ddx(o1), ddy(o1));
  float2 base_at = world.xy * t_base_uv.xy + t_base_uv.zw;
  float4 base_d = float4(ddx(base_at), ddy(base_at));
  float s = saturate(abs(normal.z) * t_slope.y + t_slope.x);
  [branch] if ((t_flags & 2) && s < t_steep_threshold) {
    float inv = 1.0 / (abs(normal.x) + t_steep_eps + abs(normal.y));
    float wx = abs(normal.x) * inv;
    float wy = abs(normal.y) * inv;
    float3 side0 = sample_side(o0, d0).rgb;
    float3 side1 = sample_side(o1, d1).rgb;
    lin = s * colour * colour + (1.0 - s) * (wy * side1 * side1 + wx * side0 * side0);
  }
  float2 wuv = world.xy * t_weight_uv.xy + t_weight_uv.zw;
  float value = (t_weight_sampler & 2) ? t_weights.SampleLevel(s_linear_clamp, wuv, 0).w
                                       : t_weights.SampleLevel(s_linear_wrap, wuv, 0).w;
  if (value < t_hole_ref) discard;
  float weight = t_weight_scale * (value - t_weight_bias);
  float a = saturate(weight) * (1.0 - terrain_fade_in(world));
  [branch] if (light_mode & 4) {
    float3 N = light_normalize(normal);
    float facing = FacingGate(dot(N, f_sun_dir), t_facing_scale);
    lin = LightTerrainLayer(lin, sample_base(base_at, base_d).rgb, f_tint, f_tint_pow, f_add, f_floor,
                            AmbientT(axis, normal), dot(N, f_sun_dir), f_sun_col, t_c11x, SunGateSingle(1.0, facing));
  }
  return terrain_out(lin, a, pos, ndc);
})pin";
static const char kPinFarMain[] = R"pin(float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0, float2 uv : TEXCOORD1,
            float3 world : TEXCOORD2, float3 normal : TEXCOORD3) : SV_Target {
  float3 colour = sample_colour(uv).rgb;
  float3 lin = colour * colour;
  float2 base_at = world.xy * t_base_uv.xy + t_base_uv.zw;
  float4 base_d = float4(ddx(base_at), ddy(base_at));
  [branch] if (light_mode & 4) {
    float3 N = light_normalize(normal);
    lin = LightTerrainFar(lin, sample_base(base_at, base_d).rgb, f_tint, f_tint_pow, f_add, f_floor, dot(N, f_axis),
                          dot(N, f_sun_dir), f_sun_col, t_c11x);
  }
  if (t_flags & 1) lin *= terrain_fade_in(world);
  return terrain_out(lin, 1.0, pos, ndc);
})pin";

// `src` with the first `before` replaced by `after`; empty when `before` is not in it.
static std::string Changed(const char* src, const char* before, const char* after) {
  std::string text = src;
  const size_t at = text.find(before);
  if (at == std::string::npos) return {};
  text.replace(at, strlen(before), after);
  return text;
}

// 0 when the pinned pieces are in their shaders; else the number of the first that is not: 1 the vertex
// shader's main, 2 the textured shader's, 3 the terrain shaders' common functions, 4 the layer shader's
// main, 5 the far shader's, 6 the shared light block (in any of the four).
static int PinsMissing(const char* vs, const char* textured, const char* layer, const char* far_ps) {
  if (!strstr(vs, kPinVsMain)) return 1;
  if (!strstr(textured, kPinTexturedMain)) return 2;
  if (!strstr(layer, kPinTerrainCommon) || !strstr(far_ps, kPinTerrainCommon)) return 3;
  if (!strstr(layer, kPinLayerMain)) return 4;
  if (!strstr(far_ps, kPinFarMain)) return 5;
  for (const char* src : {vs, textured, layer, far_ps}) {
    if (!strstr(src, kPinLight)) return 6;
  }
  return 0;
}

// 0 when every resource the compiled shader binds has a parameter of the pass's layout (kClayBindings in
// clay_logic.h) of its kind, at its register, visible to its stage: a constant buffer a kConstants or
// kConstantBuffer entry at its b register, a structured buffer a kBuffer entry at its t register, a
// texture a kTextures entry whose range holds its t register, a sampler one of the static ones (pixel
// stage). Otherwise: 1 no compile, 2 no reflection, 10 + i for the shader's resource i.
static int BindingsMismatch(const char* name, const char* src, const char* target, bool vertex) {
  using namespace fable2::native::render;
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  HRESULT hr = D3DCompile(src, strlen(src), name, nullptr, nullptr, "main", target, 0, 0, &code, &errors);
  if (errors) errors->Release();
  if (FAILED(hr) || !code) return 1;
  ID3D11ShaderReflection* refl = nullptr;
  int result = 0;
  D3D11_SHADER_DESC sd{};
  if (FAILED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_ID3D11ShaderReflection,
                        reinterpret_cast<void**>(&refl))) || !refl || FAILED(refl->GetDesc(&sd))) {
    result = 2;
  } else {
    for (unsigned i = 0; i < sd.BoundResources && !result; ++i) {
      D3D11_SHADER_INPUT_BIND_DESC bd{};
      if (FAILED(refl->GetResourceBindingDesc(i, &bd))) {
        result = 10 + int(i);
        break;
      }
      bool found = false;
      if (bd.Type == D3D_SIT_SAMPLER) {
        found = !vertex && bd.BindPoint < kClayStaticSamplers;
      } else {
        for (const ClayBinding& b : kClayBindings) {
          const bool stage = b.stage == ClayBindingStage::kAll || (b.stage == ClayBindingStage::kVertex) == vertex;
          bool kind = false;
          if (bd.Type == D3D_SIT_CBUFFER) {
            kind = (b.kind == ClayBindingKind::kConstants || b.kind == ClayBindingKind::kConstantBuffer) && b.reg == bd.BindPoint;
          } else if (bd.Type == D3D_SIT_STRUCTURED) {
            kind = b.kind == ClayBindingKind::kBuffer && b.reg == bd.BindPoint;
          } else if (bd.Type == D3D_SIT_TEXTURE) {
            kind = b.kind == ClayBindingKind::kTextures && bd.BindPoint >= b.reg && bd.BindPoint < unsigned(b.reg) + b.count;
          }
          found = found || (stage && kind && bd.BindCount == 1);
        }
      }
      if (!found) {
        std::cerr << name << ": resource " << bd.Name << " (type " << bd.Type << ", register " << bd.BindPoint
                  << ") has no parameter in the pass's layout for its stage\n";
        result = 10 + int(i);
      }
    }
  }
  if (refl) refl->Release();
  code->Release();
  return result;
}

// True when `src` calls sqrt (the HLSL intrinsic; rsqrt is another one).
static bool CallsSqrt(const char* src) {
  for (const char* at = strstr(src, "sqrt("); at; at = strstr(at + 1, "sqrt(")) {
    if (at == src || at[-1] != 'r') return true;
  }
  return false;
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
    {"light_mode", offsetof(ClayConstants, light_mode), sizeof(ClayConstants::light_mode)},
    {"light_count", offsetof(ClayConstants, light_count), sizeof(ClayConstants::light_count)},
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
  // sampler, the scale after the cut, then the light's two words) are declared by every clay shader,
  // and the textured pixel shader applies the alpha test with a discard that survives compiling.
  for (const char* src : {kClayVs, kClayPs, kClayTexturedPs}) {
    const char* mode = strstr(src, "uint sampler_index; uint alpha_mode; float alpha_ref;");
    const char* cut = strstr(src, "float4 cut;");
    const char* scale = strstr(src, "float alpha_scale; uint light_mode; uint light_count; uint pad2;");
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
    const char* scale = strstr(src, "float alpha_scale; uint light_mode; uint light_count; uint pad2;");
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
  // position x, y, height); the flat pixel shader takes nothing past its facet input. A pixel shader's
  // inputs are a prefix of the vertex shader's outputs, in their order (the registers are assigned by
  // position): the textured shader takes all of them for the light, the far shader up to the normal,
  // the layer shader up to the axis.
  if (!strstr(kClayVs, "float3 world : TEXCOORD2;") || !strstr(kClayVs, "o.world = p.xyz;")) return 30;
  if (strstr(kClayPs, "TEXCOORD1") || strstr(kClayPs, "TEXCOORD2")) return 31;
  {
    const char* out = strstr(kClayVs, "float4 pos : SV_Position; float3 ndc : TEXCOORD0; float2 uv : TEXCOORD1;");
    const char* world = strstr(kClayVs, "float3 world : TEXCOORD2;");
    const char* normal = strstr(kClayVs, "float3 normal : TEXCOORD3;");
    const char* axis = strstr(kClayVs, "float3 axis : TEXCOORD4;");
    const char* vcolour = strstr(kClayVs, "float3 vcolour : TEXCOORD5;");
    const char* bend = strstr(kClayVs, "float4 bend : TEXCOORD6;");
    if (!out || !world || !normal || !axis || !vcolour || !bend ||
        !(out < world && world < normal && normal < axis && axis < vcolour && vcolour < bend)) return 31;
    const char* head = strstr(kClayTexturedPs, "float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0, float2 uv : TEXCOORD1, "
                                               "float3 world : TEXCOORD2,");
    const char* tail = strstr(kClayTexturedPs, "float3 normal : TEXCOORD3, float3 axis : TEXCOORD4, float3 vcolour : TEXCOORD5,");
    const char* last = strstr(kClayTexturedPs, "float4 bend : TEXCOORD6) : SV_Target {");
    if (!head || !tail || !last || !(head < tail && tail < last)) return 31;
    // The plants' bend (plan Task 8b) is the vertex shader's last output and the textured shader's last
    // input, four components at one register in both compiled shaders; the other pixel shaders do not
    // take it, and a draw that is no plant gets zeros there (per light_mode, below).
    unsigned vs_mask = 0, ps_mask = 0, none = 0;
    const int vs_reg = TexcoordRegister("clay_vs", kClayVs, "vs_5_0", true, 6, &vs_mask);
    const int ps_reg = TexcoordRegister("clay_textured_ps", kClayTexturedPs, "ps_5_0", false, 6, &ps_mask);
    if (vs_reg < 0 || vs_reg != ps_reg || vs_mask != 0xF || ps_mask != 0xF) return 98;
    if (TexcoordRegister("clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0", false, 6, &none) != -1 ||
        TexcoordRegister("clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0", false, 6, &none) != -1 ||
        TexcoordRegister("clay_ps", kClayPs, "ps_5_0", false, 6, &none) != -1 ||
        TexcoordRegister("clay_vs", kClayVs, "vs_5_0", true, 7, &none) != -1) return 98;
    // (the vertex colour, one register before it, likewise: three components)
    if (TexcoordRegister("clay_vs", kClayVs, "vs_5_0", true, 5, &vs_mask) != TexcoordRegister("clay_textured_ps", kClayTexturedPs, "ps_5_0", false, 5, &ps_mask) ||
        vs_mask != 0x7 || ps_mask != 0x7) return 98;
  }
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
    // The colour is squared as the game squares it (A = tf0^2, the linear value the float target holds):
    // once for the layer's own colour and once more inside the steep-ground block's mix, below. The
    // base map (tf3) with its exponent and tint is read by the light alone (further down): the unlit
    // colour has none of it, and the only power is the light's own function.
    {
      const char* lin = strstr(l, "float3 lin = colour * colour;");
      const char* square = strstr(l, "colour * colour");
      const char* second = square ? strstr(square + 1, "colour * colour") : nullptr;
      const char* lit = strstr(l, "[branch] if (light_mode & 4) {");
      const char* base = strstr(l, "sample_base(base_at, base_d)");
      if (strstr(l, " pow(") || !lin || square != lin + strlen("float3 lin = ") || !second ||
          strstr(second + 1, "colour * colour") || !(colour < lin && lin < wuv)) return 35;
      if (!lit || !base || !(lit < base) || strstr(base + 1, "sample_base(base_at, base_d)")) return 35;
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
    if (strstr(f, "t_hole_ref) discard") || strstr(f, " pow(")) return 37;  // the adaptive shader has no cull
    // Both end in the textured shader's alpha test, alpha-to-mask and output. Unlit: the linear colour
    // times the square of the shade (the shade multiplies the display value), clamped to [0, 1]. Lit: the
    // light function's value as it is, without the facet shade (a lit draw is shaded by its light; the
    // shade would shade it twice) and without the clamp at 1, through clay_lit. The alpha is not squared,
    // not lit, and clamped only where it is written for the blend.
    for (const char* src : {l, f}) {
      const char* test = strstr(src, "if ((alpha_mode & 0x8) && !alpha_passes(a, alpha_ref, alpha_mode & 7)) discard;");
      const char* mask = strstr(src, "if (!(a >= mask_base - offset * per_offset)) discard;");
      const char* lit = strstr(src, "if (light_mode & 4) return float4(clay_lit(colour), (alpha_mode & 0x80) ? saturate(a) : 1.0);");
      const char* shade = strstr(src, "float shade = 0.75 + 0.25 * facet;");
      const char* out = strstr(src, "return float4(saturate(colour * (shade * shade)), (alpha_mode & 0x80) ? saturate(a) : 1.0);");
      if (!test || !mask || !lit || !shade || !out || !(test < mask && mask < lit && lit < shade && shade < out)) return 38;
      if (strstr(src, "a * a") || strstr(src, "colour * (0.75 + 0.25 * facet)") || strstr(src, "clay_lit(colour * ")) return 38;
    }
  }
  // The steep-ground block of the layer shader (frame-map section 13, "Terrain", item 1; the reference
  // is terrain_replay.py's steep_mix and the numbers of TerrainSteepMix in test_terrain_patch.cpp). The
  // vertex shader hands on the game's vertex normal: (x, y) from the draw's per-vertex buffer, z formed
  // as the game's vertex shader forms it; (0, 0, 1) for every draw without the buffer.
  {
    const char* v = kClayVs;
    const char* decl = strstr(v, "float3 normal : TEXCOORD3;");
    const char* flat = strstr(v, "o.normal = (light_mode & 2) ? float3(0, 0, 0) : float3(0, 0, 1);");
    const char* range = strstr(v, "bool has_light = in_range && uint(v) < light_count;");
    const char* mode = strstr(v, "if ((light_mode & 1) && has_light) {");
    const char* read = strstr(v, "float2 t = terrain_normals[v];");
    const char* uv = strstr(v, "if (textured != 0 && in_range) o.uv = uvs[v] * uv_xform.xy + uv_xform.zw;");
    const char* z = strstr(v, "o.normal = float3(t, sqrt(abs(1.0 - dot(t, t))));");
    if (!decl || !flat || !range || !mode || !read || !uv || !z || !(flat < range && range < mode && mode < read && read < z)) return 39;
    // The normals have a buffer of their own (t8): the UV buffer holds UVs only.
    if (!strstr(v, "StructuredBuffer<float2> terrain_normals : register(t8);") || strstr(v, "float2 t = uvs[v];")) return 39;
    if (strstr(kClayPs, "TEXCOORD3") || !strstr(kClayTerrainFarPs, "float3 world : TEXCOORD2, float3 normal : TEXCOORD3) : SV_Target {")) return 40;
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
    if (!DisassemblyHas("clay_terrain_layer_ps", l, "ps_5_0", "sample_d") || CallsSqrt(l)) return 45;
    // The far shader has no block: no side texture (its one derivative fetch is the light's base map, at t7).
    if (strstr(kClayTerrainFarPs, "t_side :") || strstr(kClayTerrainFarPs, "sample_side") || CallsSqrt(kClayTerrainFarPs) ||
        DisassemblyHas("clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0", "t6")) return 46;
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
  // 18 variables). Controls: a source with two fields exchanged, and one with a field too few, are reported.
  {
    static_assert(sizeof(ClayConstants) == 144 && sizeof(kDrawFields) / sizeof(kDrawFields[0]) == 18);
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
    std::string shorter = kClayPs;  // (the flat shader: it reads none of the fields left out)
    const std::string tail = "float alpha_scale; uint light_mode; uint light_count; uint pad2;";
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
      if (CallsSqrt(ps.src) || DisassemblyHas(ps.name, ps.src, "ps_5_0", "sqrt")) return 64;
    }
    // The clamps that guard the float target survive compiling. Unlit (the 8-bit target's clamp to
    // [0, 1], where the value is not bounded by construction): the terrain colours times the squared
    // shade, the textured draw's colour and its written alpha. (The compiler drops the clamp of flat
    // clay: it proves that range.) Lit (clay_lit: a number, not negative, at most 64): the comparison
    // with 0 and its mask, which turn a NaN and a negative value into 0, and the upper bound, in each of
    // the three shaders that light; the flat shader has neither.
    if (!DisassemblyHas("clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0", "mul_sat r") ||
        !DisassemblyHas("clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0", "mul_sat r") ||
        !DisassemblyHas("clay_textured_ps", kClayTexturedPs, "ps_5_0", "mul_sat r") ||
        !DisassemblyHas("clay_textured_ps", kClayTexturedPs, "ps_5_0", "mov_sat r")) return 69;
    for (int i = 1; i < 4; ++i) {
      if (!DisassemblyHas(clay_ps[i].name, clay_ps[i].src, "ps_5_0", "l(64.000000, 64.000000, 64.000000") &&
          !DisassemblyHas(clay_ps[i].name, clay_ps[i].src, "ps_5_0", "64.000000, 64.000000, 64.000000)")) return 69;
      if (!DisassemblyHas(clay_ps[i].name, clay_ps[i].src, "ps_5_0", "lt r") ||
          !DisassemblyHas(clay_ps[i].name, clay_ps[i].src, "ps_5_0", "min r")) return 69;
    }
    if (DisassemblyHas("clay_ps", kClayPs, "ps_5_0", "64.000000")) return 69;  // control
  }
  // The light (frame-map section 14, "Native light"): the functions of light_formulas.py, statement for
  // statement, in every shader that lights; who calls which with what; where the frame's and the draw's
  // values come from; and that nothing of it touches a draw without light_mode bit 2.
  {
    const char* lit_shaders[] = {kClayVs, kClayTexturedPs, kClayTerrainLayerPs, kClayTerrainFarPs};
    const char* statements[] = {
        // normalize: zero for a vector without a length (never a made-up direction), and for one whose
        // length is not a number
        "return (n2 > 0.0 && n2 < 3.0e38) ? v * rsqrt(n2) : float3(0.0, 0.0, 0.0);",
        // |base|^exponent as the game computes it; 0 for a zero base
        "float3 b = abs(base);",
        "return (b > 0.0) ? exp2(exponent * log2(b)) : float3(0.0, 0.0, 0.0);",
        // L = floor + (1 - floor) * t^2, t saturated
        "float3 AmbientFactor(float3 amb_floor, float t) {",
        "t = saturate(t);",
        "return amb_floor + (1.0 - amb_floor) * (t * t);",
        // lerp(normal, axis, (1 + normal . axis) / 2), not normalised
        "float k = 0.5 + 0.5 * dot(normal, axis);",
        "return normal * (1.0 - k) + axis * k;",
        "return saturate(dot(light_normalize(axis_vector), light_normalize(normal)));",
        "float FacingGate(float n_dot_c109, float k) { return saturate(k * n_dot_c109); }",
        "float SunGateSingle(float mask, float facing) { return saturate(facing) * saturate(mask); }",
        "return light_pow(base, tint_pow) * tint * AmbientFactor(amb_floor, t) + add;",
        "return sun_col * (saturate(n_dot_sun) * sun_gate);",
        // the three families, with the argument lists of light_formulas.py
        "float3 LightTerrainLayer(float3 A, float3 baked, float3 tint, float tint_pow, float3 add, float3 amb_floor, float t,",
        "                         float n_dot_sun, float3 sun_col, float c11x, float sun_gate) {",
        "return A * (c11x * light_ambient(baked, tint, tint_pow, add, amb_floor, t) + light_sun(sun_col, n_dot_sun, sun_gate));",
        "float3 LightTerrainFar(float3 F, float3 baked, float3 tint, float tint_pow, float3 add, float3 amb_floor, float t,",
        "                       float n_dot_sun, float3 sun_col, float c11x) {",
        "return F * (light_ambient(baked, tint, tint_pow, add, amb_floor, t) + c11x * light_sun(sun_col, n_dot_sun, 1.0));",
        "float3 LightObject(float3 A, float3 vcolour, float3 tint, float tint_pow, float3 add, float3 amb_floor, float t,",
        "                   float n_dot_sun, float3 sun_col, float c27w, float sun_gate, float3 c49) {",
        "return A * (c27w * light_ambient(vcolour, tint, tint_pow, add, amb_floor, t) + light_sun(sun_col, n_dot_sun, sun_gate)) * c49;",
        // the instanced plants: the vertex side's colour, height blend and bend, the pixel side's bent
        // normal, and the two families' functions
        "float3 InstanceAmbient(float3 instance_colour, float3 tint, float tint_pow, float3 add) {",
        "return light_pow(instance_colour, tint_pow) * tint + add;",
        "float PlantHeight(float z, float scale, float offset) { return saturate(z * scale + offset); }",
        "float4 PlantBend(float3 normal, float3 up, float height, float strength) {",
        "float h = saturate(height);",
        "return float4(light_normalize(normal + (up - normal) * h), h) * strength;",
        "float3 PlantNormal(float3 normal, float4 bend) {",
        "float3 n = light_normalize(normal);",
        "return light_normalize(n + (bend.xyz - n) * saturate(bend.w));",
        "float3 LightPlant(float3 A, float3 vs_ambient, float n_dot_sun, float3 sun_col, float sun_gate) {",
        "return A * (vs_ambient + light_sun(sun_col, n_dot_sun, sun_gate));",
        "float3 LightPlantLit(float3 A, float3 vs_base, float3 add, float3 amb_floor, float t, float n_dot_sun, float3 sun_col,",
        "return A * (vs_base * AmbientFactor(amb_floor, t) + add + light_sun(sun_col, n_dot_sun, sun_gate));",
        // what a lit draw writes
        "float3 clay_lit(float3 v) { return (v > 0.0) ? min(v, 64.0) : float3(0.0, 0.0, 0.0); }",
    };
    for (const char* src : lit_shaders) {
      for (const char* statement : statements) {
        if (!strstr(src, statement)) {
          std::cerr << "a shader that lights lacks: " << statement << "\n";
          return 80;
        }
      }
      // Nothing is divided in the light, and no clamp at 1 is on a lit value.
      if (strstr(src, "saturate(lit") || strstr(src, "clay_linear(lit")) return 80;
    }
    // The flat shader has no light at all: no block, no function.
    if (strstr(kClayPs, "Frame") || strstr(kClayPs, "light_mode &") || strstr(kClayPs, "clay_lit")) return 81;
    // The vertex shader: the object's normal through the draw's rows (the game's (R n) * c7.w), the
    // vector of t from the normal as it is, not normalised, and the frame's axis, and the stream
    // colour's red, green, blue (bytes 0 to 2) raised to the draw's power; the terrain's vector of t
    // only for a lit draw. A light buffer is read only below light_count.
    {
      const char* v = kClayVs;
      const char* range = strstr(v, "bool has_light = in_range && uint(v) < light_count;");
      const char* terrain = strstr(v, "if (light_mode & 4) o.axis = AmbientAxis(o.normal, f_axis);");
      const char* object = strstr(v, "if ((light_mode & 18) == 2 && has_light) {");
      const char* n = strstr(v, "float3 n = normals[v];");
      const char* rot = strstr(v, "o.normal = float3(dot(o_rot0.xyz, n), dot(o_rot1.xyz, n), dot(o_rot2.xyz, n));");
      const char* axis = rot ? strstr(rot, "o.axis = AmbientAxis(o.normal, f_axis);") : nullptr;  // (the object's)
      const char* word = strstr(v, "uint word = colours[v];");
      const char* colour = strstr(v, "o.vcolour = light_pow(float3(word & 255, (word >> 8) & 255, (word >> 16) & 255) / 255.0, o_colour_power);");
      if (!range || !terrain || !object || !n || !rot || !axis || !word || !colour) return 82;
      if (!(range < terrain && terrain < object && object < n && n < rot && rot < axis && axis < word && word < colour)) return 82;
      // The probe colour (light_mode bit 3), between the word's load and the stream colour: the set
      // clamped to the last one, the weights from their own buffer, base + (rows . weights) with
      // four-component dot products, and NO power (the pixel shader's is the only one it takes).
      const char* probe = strstr(v, "[branch] if (light_mode & 8) {");
      const char* a0 = strstr(v, "uint a0 = min(word, 8u);");
      const char* weights = strstr(v, "float4 w = probe_weights[v];");
      const char* sum = strstr(v, "o.vcolour = o_probe[a0].xyz + float3(dot(o_probe[a0 + 1], w), dot(o_probe[a0 + 2], w), dot(o_probe[a0 + 3], w));");
      const char* otherwise = sum ? strstr(sum, "} else {") : nullptr;
      if (!probe || !a0 || !weights || !sum || !otherwise) return 82;
      if (!(word < probe && probe < a0 && a0 < weights && weights < sum && sum < otherwise && otherwise < colour)) return 82;
      if (strstr(v, "light_pow(o_probe") || strstr(v, "saturate(o_probe") || strstr(v, "max(o_probe") || strstr(v, "abs(o_probe")) return 82;
      if (!strstr(v, "StructuredBuffer<float4> probe_weights : register(t11);") || !strstr(v, "float4 o_probe[12];") ||
          !strstr(kClayTexturedPs, "float4 o_probe[12];")) return 82;
      // (the vertex shader normalises nothing of the light: the pixel shaders do, after the interpolation)
      if (strstr(v, "normalize(o.normal)") || strstr(v, "normalize(n)") || strstr(v, "light_normalize(o.")) return 82;
      if (!strstr(v, "StructuredBuffer<float3> normals : register(t9);") ||
          !strstr(v, "StructuredBuffer<uint> colours : register(t10);")) return 82;
      // The object's block is not an instanced plant's: the plant's normal must not go through the rows
      // (the test of the block excludes the instance bit, and nothing else of the shader has the rows).
      const char* after_rows = rot + strlen("o.normal = float3(dot(o_rot0.xyz, n), dot(o_rot1.xyz, n), dot(o_rot2.xyz, n));");
      if (strstr(v, "if ((light_mode & 2) && has_light)") || strstr(after_rows, "o_rot0.xyz") || strstr(after_rows, "o_rot1.xyz") ||
          strstr(after_rows, "o_rot2.xyz")) return 82;
      // The instanced plant (plan Task 8b), after the object's block and under its own test: the
      // normal as it is, the colour of the copy and the bend inputs from the plant's own buffers, the
      // bend from the up axis, the height and the three bend constants (c45.x the strength, .y the
      // scale, .z the offset), the colour's exponent 2.2 * c21.w; then by family the vector of t with
      // the colour times the tint, or no vector and the colour times the tint plus the added light.
      const char* plant = strstr(v, "[branch] if ((light_mode & 18) == 18 && has_light) {");
      const char* pn = plant ? strstr(plant, "float3 n = normals[v];") : nullptr;
      const char* as_is = strstr(v, "o.normal = n;");
      const char* tone = strstr(v, "uint tone = colours[v];");
      const char* inputs = strstr(v, "float4 b = probe_weights[v];");
      const char* bent = strstr(v, "o.bend = PlantBend(n, b.xyz, PlantHeight(b.w, o_rot1.w, o_rot2.w), o_rot0.w);");
      const char* bytes = strstr(v, "float3 colour = float3(tone & 255, (tone >> 8) & 255, (tone >> 16) & 255) / 255.0;");
      const char* family = strstr(v, "[branch] if (light_mode & 32) {");
      const char* lit_axis = strstr(v, "o.axis = AmbientAxis(n, f_axis);");
      const char* lit_colour = strstr(v, "o.vcolour = InstanceAmbient(colour, f_tint, o_colour_power * f_tint_pow, float3(0, 0, 0));");
      const char* no_axis = lit_colour ? strstr(lit_colour, "o.axis = float3(0, 0, 0);") : nullptr;
      const char* amb = strstr(v, "o.vcolour = InstanceAmbient(colour, f_tint, o_colour_power * f_tint_pow, f_add);");
      const char* zero = strstr(v, "o.bend = float4(0, 0, 0, 0);");
      if (!plant || !pn || !as_is || !tone || !inputs || !bent || !bytes || !family || !lit_axis || !lit_colour || !no_axis || !amb || !zero) return 99;
      if (!(zero < range && colour < plant && plant < pn && pn < as_is && as_is < tone && tone < inputs && inputs < bent && bent < bytes &&
            bytes < family && family < lit_axis && lit_axis < lit_colour && lit_colour < no_axis && no_axis < amb)) return 99;
      // (one bend, one height blend and two colour statements in main, two loads from each of the
      // object's and the plant's three buffers, and no plant statement before the plant's test)
      const char* main_at = strstr(v, "VsOut main(");
      auto count = [](const char* from, const char* needle) {
        int found = 0;
        for (const char* at = from ? strstr(from, needle) : nullptr; at; at = strstr(at + 1, needle)) ++found;
        return found;
      };
      if (count(main_at, "PlantBend(") != 1 || count(main_at, "PlantHeight(") != 1 || count(main_at, "InstanceAmbient(") != 2 ||
          count(main_at, "= normals[v]") != 2 || count(main_at, "colours[v]") != 2 || count(main_at, "probe_weights[v]") != 2 ||
          count(main_at, "terrain_normals[v]") != 1 || as_is < plant || tone < plant) return 99;
    }
    // The object (kClayTexturedPs): A is the albedo squared, clamped to [0, 1] before the square as the
    // materials picture clamps it, and the light multiplies it outside the square; the normal is
    // normalised per pixel; t from the interpolated vector; the sun's direction and colour, the tint,
    // exponent, added light and floor from the frame; c27.w, c49 and c26 from the draw; the mask is 1;
    // after the alpha test and alpha-to-mask, before the unlit output.
    {
      const char* t = kClayTexturedPs;
      const char* mask = strstr(t, "if (!(a >= mask_base - offset * per_offset)) discard;");
      const char* branch = strstr(t, "if (light_mode & 4) {");
      const char* n = strstr(t, "float3 N = light_normalize(normal);");
      const char* call = strstr(t, "lit = LightObject(clay_linear(t.rgb), vcolour, f_tint, f_tint_pow, f_add, f_floor, AmbientT(axis, normal),");
      const char* rest = strstr(t, "dot(N, f_sun_dir), f_sun_col, o_c27w, SunGateSingle(1.0, 1.0), o_c49) * o_c26;");
      const char* out = strstr(t, "return float4(clay_lit(lit), (alpha_mode & 0x80) ? saturate(a) : 1.0);");
      const char* unlit = strstr(t, "return float4(clay_linear(t.rgb * (0.75 + 0.25 * facet)), (alpha_mode & 0x80) ? saturate(a) : 1.0);");
      if (!mask || !branch || !n || !call || !rest || !out || !unlit) return 83;
      if (!(mask < branch && branch < n && n < call && call < rest && rest < out && out < unlit)) return 83;
      if (strstr(t, "clay_linear(t.rgb * lit") || strstr(t, "facet) * lit")) return 83;
      // The instanced plants (plan Task 8b), before the object's path and under the instance bit: the
      // normal bent per pixel, then by family the object form of the vertex side's colour (the added
      // light, the floor, t from the vertex side's vector and the BENT normal) or A times the vertex
      // side's ambient plus the sun; the sun of both by the bent normal; the mask 1; no c26, c27 or c49.
      const char* declared = strstr(t, "float3 lit;");
      const char* plant = strstr(t, "[branch] if (light_mode & 16) {");
      const char* np = strstr(t, "float3 Np = PlantNormal(normal, bend);");
      const char* family = strstr(t, "if (light_mode & 32) {");
      const char* lit_call = strstr(t, "lit = LightPlantLit(clay_linear(t.rgb), vcolour, f_add, f_floor, AmbientT(axis, Np), dot(Np, f_sun_dir), f_sun_col,");
      const char* plant_call = strstr(t, "lit = LightPlant(clay_linear(t.rgb), vcolour, dot(Np, f_sun_dir), f_sun_col, SunGateSingle(1.0, 1.0));");
      if (!declared || !plant || !np || !family || !lit_call || !plant_call) return 99;
      if (!(branch < declared && declared < plant && plant < np && np < family && family < lit_call && lit_call < plant_call &&
            plant_call < n && n < call)) return 99;
      // (the object's constants are read by the object's path alone: once each, after the plants' paths)
      for (const char* field : {"o_c27w", "o_c49", "o_c26"}) {
        const char* at = strstr(strstr(t, "float4 main("), field);
        if (!at || at < n || strstr(at + 1, field)) return 99;
      }
    }
    // The terrain layer: the lit colour replaces the unlit one after the alpha is formed (the alpha is
    // never lit), with the base map at the base coordinate, the facing factor of the sun's direction
    // with the draw's literal, c11.x on the ambient part, the mask 1.
    {
      const char* l = kClayTerrainLayerPs;
      const char* at = strstr(l, "float2 base_at = world.xy * t_base_uv.xy + t_base_uv.zw;");
      const char* d = strstr(l, "float4 base_d = float4(ddx(base_at), ddy(base_at));");
      const char* cull = strstr(l, "if (value < t_hole_ref) discard;");
      const char* alpha = strstr(l, "float a = saturate(weight) * (1.0 - terrain_fade_in(world));");
      const char* branch = strstr(l, "[branch] if (light_mode & 4) {");
      const char* n = strstr(l, "float3 N = light_normalize(normal);");
      const char* facing = strstr(l, "float facing = FacingGate(dot(N, f_sun_dir), t_facing_scale);");
      const char* call = strstr(l, "lin = LightTerrainLayer(lin, sample_base(base_at, base_d).rgb, f_tint, f_tint_pow, f_add, f_floor,");
      const char* rest = strstr(l, "AmbientT(axis, normal), dot(N, f_sun_dir), f_sun_col, t_c11x, SunGateSingle(1.0, facing));");
      const char* out = strstr(l, "return terrain_out(lin, a, pos, ndc);");
      if (!at || !d || !cull || !alpha || !branch || !n || !facing || !call || !rest || !out) return 84;
      if (!(at < d && d < cull && alpha < branch && branch < n && n < facing && facing < call && call < rest && rest < out)) return 84;
      if (!strstr(l, "float3 world : TEXCOORD2, float3 normal : TEXCOORD3, float3 axis : TEXCOORD4) : SV_Target {")) return 84;
    }
    // The far terrain: t from the normal and the frame's axis as they are (no vertex vector), c11.x on
    // the sun, no facing factor; the underlay's fade multiplies the lit colour.
    {
      const char* f = kClayTerrainFarPs;
      const char* lin = strstr(f, "float3 lin = colour * colour;");
      const char* branch = strstr(f, "[branch] if (light_mode & 4) {");
      const char* n = strstr(f, "float3 N = light_normalize(normal);");
      const char* call = strstr(f, "lin = LightTerrainFar(lin, sample_base(base_at, base_d).rgb, f_tint, f_tint_pow, f_add, f_floor, dot(N, f_axis),");
      const char* rest = strstr(f, "dot(N, f_sun_dir), f_sun_col, t_c11x);");
      const char* fade = strstr(f, "if (t_flags & 1) lin *= terrain_fade_in(world);");
      if (!lin || !branch || !n || !call || !rest || !fade ||
          !(lin < branch && branch < n && n < call && call < rest && rest < fade)) return 85;
      if (strstr(f, "FacingGate(dot") || strstr(f, "AmbientT(axis")) return 85;
    }
    // The base map is the fourth terrain texture (t7), read with its own sampler and the derivatives
    // formed outside the branch, in both terrain shaders.
    for (const char* src : {kClayTerrainLayerPs, kClayTerrainFarPs}) {
      if (!strstr(src, "Texture2D t_base : register(t7);") || !strstr(src, "if (t_base_sampler == 0)") ||
          !strstr(src, "t_base.SampleGrad(s_point_wrap, at, d.xy, d.zw)") ||
          !strstr(src, "t_base.SampleGrad(s_linear_wrap, at, d.xy, d.zw)") ||
          !strstr(src, "t_base.SampleGrad(s_point_clamp, at, d.xy, d.zw)") ||
          !strstr(src, "t_base.SampleGrad(s_linear_clamp, at, d.xy, d.zw)")) return 86;
    }
    if (BindPoint("clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0", "t_base") != 7 ||
        BindPoint("clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0", "t_base") != 7 ||
        BindPoint("clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0", "t_side") != 6 ||
        BindPoint("clay_vs", kClayVs, "vs_5_0", "terrain_normals") != 8 ||
        BindPoint("clay_vs", kClayVs, "vs_5_0", "normals") != 9 ||
        BindPoint("clay_vs", kClayVs, "vs_5_0", "colours") != 10 ||
        BindPoint("clay_vs", kClayVs, "vs_5_0", "probe_weights") != 11 ||
        BindPoint("clay_vs", kClayVs, "vs_5_0", "uvs") != 2) return 87;
    // The blocks' registers as the pass binds them: Frame at b2 in the four shaders that light, the
    // draw's slot at b1 (Object: the vertex shader and the textured pixel shader; Terrain: the two
    // terrain shaders); the flat shader binds neither.
    for (int i = 0; i < 4; ++i) {
      const char* target = i == 0 ? "vs_5_0" : "ps_5_0";
      if (BindPoint("lit", lit_shaders[i], target, "Frame") != 2) return 88;
      if (BindPoint("lit", lit_shaders[i], target, i < 2 ? "Object" : "Terrain") != 1) return 88;
    }
    if (BindPoint("clay_ps", kClayPs, "ps_5_0", "Frame") != -1 || BindPoint("clay_ps", kClayPs, "ps_5_0", "Object") != -1) return 88;
    // The frame's block and the object's block lie where the tables beside the HLSL say, in every
    // compiled shader that declares them, and the tables are the C++ structs' layouts
    // (ClayFrameConstants, ClayObjectConstants). Controls: sources with two fields exchanged are reported.
    {
      using fable2::native::render::ClayFrameConstants;
      using fable2::native::render::ClayObjectConstants;
      const unsigned frame_count = sizeof(kClayFrameFields) / sizeof(kClayFrameFields[0]);
      const unsigned object_count = sizeof(kClayObjectFields) / sizeof(kClayObjectFields[0]);
      static_assert(sizeof(ClayFrameConstants) == 96 && sizeof(ClayObjectConstants) == 272);
      const struct {
        const char* name;
        size_t offset, bytes;
      } frame_struct[] = {
          {"f_sun_dir", offsetof(ClayFrameConstants, sun_dir), sizeof(ClayFrameConstants::sun_dir)},
          {"f_light_on", offsetof(ClayFrameConstants, light_on), sizeof(ClayFrameConstants::light_on)},
          {"f_sun_col", offsetof(ClayFrameConstants, sun_col), sizeof(ClayFrameConstants::sun_col)},
          {"f_tint_pow", offsetof(ClayFrameConstants, tint_pow), sizeof(ClayFrameConstants::tint_pow)},
          {"f_tint", offsetof(ClayFrameConstants, tint), sizeof(ClayFrameConstants::tint)},
          {"f_pad3", offsetof(ClayFrameConstants, pad3), sizeof(ClayFrameConstants::pad3)},
          {"f_add", offsetof(ClayFrameConstants, add), sizeof(ClayFrameConstants::add)},
          {"f_pad0", offsetof(ClayFrameConstants, pad0), sizeof(ClayFrameConstants::pad0)},
          {"f_floor", offsetof(ClayFrameConstants, floor), sizeof(ClayFrameConstants::floor)},
          {"f_pad1", offsetof(ClayFrameConstants, pad1), sizeof(ClayFrameConstants::pad1)},
          {"f_axis", offsetof(ClayFrameConstants, axis), sizeof(ClayFrameConstants::axis)},
          {"f_pad2", offsetof(ClayFrameConstants, pad2), sizeof(ClayFrameConstants::pad2)},
      }, object_struct[] = {
          {"o_rot0", offsetof(ClayObjectConstants, rot), 16},
          {"o_rot1", offsetof(ClayObjectConstants, rot) + 16, 16},
          {"o_rot2", offsetof(ClayObjectConstants, rot) + 32, 16},
          {"o_c49", offsetof(ClayObjectConstants, c49), sizeof(ClayObjectConstants::c49)},
          {"o_c27w", offsetof(ClayObjectConstants, c27w), sizeof(ClayObjectConstants::c27w)},
          {"o_c26", offsetof(ClayObjectConstants, c26), sizeof(ClayObjectConstants::c26)},
          {"o_colour_power", offsetof(ClayObjectConstants, colour_power), sizeof(ClayObjectConstants::colour_power)},
          {"o_probe", offsetof(ClayObjectConstants, probe), sizeof(ClayObjectConstants::probe)},
      };
      if (frame_count != sizeof(frame_struct) / sizeof(frame_struct[0]) ||
          object_count != sizeof(object_struct) / sizeof(object_struct[0])) return 89;
      for (unsigned i = 0; i < frame_count; ++i) {
        if (strcmp(kClayFrameFields[i].name, frame_struct[i].name) || kClayFrameFields[i].offset != frame_struct[i].offset ||
            kClayFrameFields[i].bytes != frame_struct[i].bytes) return 89;
      }
      for (unsigned i = 0; i < object_count; ++i) {
        if (strcmp(kClayObjectFields[i].name, object_struct[i].name) || kClayObjectFields[i].offset != object_struct[i].offset ||
            kClayObjectFields[i].bytes != object_struct[i].bytes) return 89;
      }
      // The exposure is the mapping's alone: the frame's block has none.
      for (const fable2::native::shaders::ClayBufferField& f : kClayFrameFields) {
        if (strstr(f.name, "exposure")) return 89;
      }
      for (const char* src : lit_shaders) {
        if (strstr(src, "f_exposure") || strstr(src, "float exposure")) return 89;
      }
      for (int i = 0; i < 4; ++i) {
        const char* target = i == 0 ? "vs_5_0" : "ps_5_0";
        if (int at = BufferLayoutMismatch("lit", lit_shaders[i], target, "Frame", kClayFrameFields, frame_count, 96)) {
          std::cerr << "cbuffer Frame of lit shader " << i << ": " << at << "\n";
          return 90 + i;
        }
      }
      if (int at = BufferLayoutMismatch("clay_vs", kClayVs, "vs_5_0", "Object", kClayObjectFields, object_count, 272)) return 94;
      if (int at = BufferLayoutMismatch("clay_textured_ps", kClayTexturedPs, "ps_5_0", "Object", kClayObjectFields, object_count, 272)) return 95;
      std::string swapped = kClayTexturedPs;
      const std::string good = "float3 f_tint; float f_pad3;";
      size_t at = swapped.find(good);
      if (at == std::string::npos) return 96;
      swapped.replace(at, good.size(), "float f_pad3; float3 f_tint;");
      if (BufferLayoutMismatch("swapped_frame_ps", swapped.c_str(), "ps_5_0", "Frame", kClayFrameFields, frame_count, 96) < 10) return 96;
      swapped = kClayTexturedPs;
      const std::string object_good = "float3 o_c49; float o_c27w;";
      at = swapped.find(object_good);
      if (at == std::string::npos) return 97;
      swapped.replace(at, object_good.size(), "float o_c27w; float3 o_c49;");
      if (BufferLayoutMismatch("swapped_object_ps", swapped.c_str(), "ps_5_0", "Object", kClayObjectFields, object_count, 272) < 10) return 97;
      // (the probe registers are the block's last 192 bytes, twelve float4: an array a register apart)
      if (object_count != 8 || strcmp(kClayObjectFields[7].name, "o_probe") || kClayObjectFields[7].offset != 80 ||
          kClayObjectFields[7].bytes != 192) return 97;
    }
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
  // What a draw can make the GPU do (after the device hung on the first lit frame of three runs,
  // 2026-10-08). (1) No clay shader has a loop, a call or a jump: nothing in it iterates, so no constant
  // of a draw can make it run long. (2) Every register a compiled shader declares has a parameter of the
  // pass's layout for its stage (kClayBindings, which clay_pass.cpp creates the layout from): no shader
  // reads a register nothing binds. (3) A structured buffer bound by address has no bounds check on the
  // GPU. The compiler turns a plain `if` around a buffer load into an unconditional load and a select,
  // so every vertex of every draw would load from all four light buffers, whatever is bound there; the
  // light's loads (t8 to t11) are therefore inside [branch] blocks and stay there after compiling. The
  // loads of t0, t1 and t2 are unconditional by design: the pass binds the draw's own buffers there
  // (for a draw without UVs its positions, which cover its vertices).
  {
    const struct {
      const char* name;
      const char* src;
      const char* target;
      bool vertex;
    } all[] = {{"clay_vs", kClayVs, "vs_5_0", true},
               {"clay_ps", kClayPs, "ps_5_0", false},
               {"clay_textured_ps", kClayTexturedPs, "ps_5_0", false},
               {"clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0", false},
               {"clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0", false},
               {"output_map_ps", kOutputMapPs, "ps_5_0", false},
               {"fullscreen_vs", kFullscreenVs, "vs_5_0", true}};
    for (const auto& s : all) {
      const std::string listing = Disassembly(s.name, s.src, s.target);
      if (listing.empty()) return 150;
      if (LoopInstructions(listing) != 0) {
        std::cerr << s.name << " has a loop, a call or a jump\n";
        return 151;
      }
    }
    // Control: a shader that iterates up to a constant is reported.
    const char* looping = "cbuffer C : register(b0) { uint n; }; float4 main(float4 p : SV_Position) : SV_Target {\n"
                          "  float s = 0; for (uint i = 0; i < n; ++i) s += sin(float(i) + p.x); return float4(s, 0, 0, 1); }";
    if (LoopInstructions(Disassembly("looping_ps", looping, "ps_5_0")) == 0) return 152;
    for (int i = 0; i < 5; ++i) {
      if (int at = BindingsMismatch(all[i].name, all[i].src, all[i].target, all[i].vertex)) {
        std::cerr << all[i].name << ": binding mismatch " << at << "\n";
        return 153;
      }
    }
    // Controls: a buffer at a register the layout does not have, and one at a pixel-only register read by
    // the vertex stage, are reported.
    {
      std::string moved = kClayVs;
      const std::string good = "StructuredBuffer<uint> colours : register(t10);";
      size_t at = moved.find(good);
      if (at == std::string::npos) return 154;
      moved.replace(at, good.size(), "StructuredBuffer<uint> colours : register(t12);");
      if (BindingsMismatch("moved_vs", moved.c_str(), "vs_5_0", true) < 10) return 154;
      moved = kClayVs;
      moved.replace(at, good.size(), "StructuredBuffer<uint> colours : register(t3);");
      if (BindingsMismatch("pixel_register_vs", moved.c_str(), "vs_5_0", true) < 10) return 154;
      // The weights' register likewise: at a register behind the layout's last, and at a texture's.
      moved = kClayVs;
      const std::string weights = "StructuredBuffer<float4> probe_weights : register(t11);";
      at = moved.find(weights);
      if (at == std::string::npos) return 154;
      moved.replace(at, weights.size(), "StructuredBuffer<float4> probe_weights : register(t12);");
      if (BindingsMismatch("moved_weights_vs", moved.c_str(), "vs_5_0", true) < 10) return 154;
      moved = kClayVs;
      moved.replace(at, weights.size(), "StructuredBuffer<float4> probe_weights : register(t7);");
      if (BindingsMismatch("pixel_register_weights_vs", moved.c_str(), "vs_5_0", true) < 10) return 154;
    }
    // The registers the vertex shader may load from: the layout's seven buffer parameters, and no other.
    unsigned buffer_registers = 0;
    for (const fable2::native::render::ClayBinding& b : fable2::native::render::kClayBindings) {
      if (b.kind == fable2::native::render::ClayBindingKind::kBuffer) buffer_registers |= 1u << b.reg;
    }
    if (buffer_registers != ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11))) return 156;
    const std::string vs = Disassembly("clay_vs", kClayVs, "vs_5_0");
    if (BufferLoadGuard(vs, 8) != 1 || BufferLoadGuard(vs, 9) != 1 || BufferLoadGuard(vs, 10) != 1 || BufferLoadGuard(vs, 11) != 1) {
      std::cerr << "the vertex shader loads a light buffer outside a branch: t8 " << BufferLoadGuard(vs, 8) << ", t9 "
                << BufferLoadGuard(vs, 9) << ", t10 " << BufferLoadGuard(vs, 10) << ", t11 " << BufferLoadGuard(vs, 11) << "\n";
      return 155;
    }
    if (BufferLoadGuard(vs, 0) != 0 || BufferLoadGuard(vs, 1) != 0 || BufferLoadGuard(vs, 2) != 0 ||
        BufferLoadGuard(vs, 12) != -1 || !LoadsOnlyFrom(vs, buffer_registers)) return 156;
    // (LoadsOnlyFrom sees a load from a register it is not given)
    if (LoadsOnlyFrom(vs, buffer_registers & ~(1u << 11)) || LoadsOnlyFrom(vs, buffer_registers & ~(1u << 0))) return 156;
    if (!strstr(kClayVs, "[branch] if ((light_mode & 1) && has_light) {") ||
        !strstr(kClayVs, "[branch] if ((light_mode & 18) == 2 && has_light) {") ||
        !strstr(kClayVs, "[branch] if (light_mode & 8) {") ||
        !strstr(kClayVs, "[branch] if ((light_mode & 18) == 18 && has_light) {") ||
        !strstr(kClayVs, "[branch] if (light_mode & 32) {") ||
        !strstr(kClayTexturedPs, "[branch] if (light_mode & 16) {")) return 157;
    // Controls: a load that is outside its test is reported. (Whether the compiler takes a load out of a
    // plain `if` is its own choice: it did for the text of the three hung runs, where the blocks were
    // small, and for the present text it does not, with or without the attributes. The attribute is the
    // promise in the source; what is held is the compiled program, above and per light_mode below. So
    // the controls put the loads outside by hand.) A normal read before the object's test; the weights
    // read as a vertex output's default; the weights read inside the object's test but before the probe
    // bit's, where every colour-stream object would load them from a t11 that is not a weights buffer.
    {
      const std::string early_normal = Changed(kClayVs, "  [branch] if ((light_mode & 18) == 2 && has_light) {",
                                               "  o.axis = normals[v];\n  [branch] if ((light_mode & 18) == 2 && has_light) {");
      const std::string early_weights = Changed(kClayVs, "  o.vcolour = float3(0, 0, 0);", "  o.vcolour = probe_weights[v].xyz;");
      const std::string outer_weights = Changed(kClayVs, "    [branch] if (light_mode & 8) {",
                                                "    o.axis += probe_weights[v].xyz;\n    [branch] if (light_mode & 8) {");
      const std::string normal_listing = Disassembly("early_normal_vs", early_normal.c_str(), "vs_5_0");
      const std::string weights_listing = Disassembly("early_weights_vs", early_weights.c_str(), "vs_5_0");
      const std::string outer_listing = Disassembly("outer_weights_vs", outer_weights.c_str(), "vs_5_0");
      if (early_normal.empty() || early_weights.empty() || outer_weights.empty() || normal_listing.empty() ||
          weights_listing.empty() || outer_listing.empty()) return 158;
      if (BufferLoadGuard(normal_listing, 9) != 0 || InnerLoadDepth(normal_listing, 9) != 0) return 158;
      // The default's load the compiler moves into the `else` of the object's test: inside a block, so
      // the two block counts are blind to it (it runs for every vertex that is NOT an object's). The
      // program of a draw without light shows it: with the mode fixed at 0 the weights are loaded. That
      // is the check every light_mode gets below, and why it is made on the fixed-mode programs.
      if (BufferLoadGuard(weights_listing, 11) != 1 ||
          !ReadsResource(Disassembly("early_weights_vs_mode0", WithLightMode(early_weights.c_str(), 0).c_str(), "vs_5_0"), 11) ||
          !ReadsResource(Disassembly("early_weights_vs_mode1", WithLightMode(early_weights.c_str(), 1).c_str(), "vs_5_0"), 11)) return 158;
      // (inside a test, so BufferLoadGuard alone lets it through: the depths are what tell)
      const std::string outer_depths = LoadDepths(outer_listing, 11);
      if (BufferLoadGuard(outer_listing, 11) != 1 || outer_depths.empty() || outer_depths[0] != '1' ||
          outer_depths == LoadDepths(vs, 11)) return 158;
      // The plant's bend inputs read before the plant's test (every draw would load them), and its
      // colour read in the object's block (every object would load a second word): both reported.
      const std::string early_bend = Changed(kClayVs, "  [branch] if ((light_mode & 18) == 18 && has_light) {",
                                             "  o.bend = probe_weights[v];\n  [branch] if ((light_mode & 18) == 18 && has_light) {");
      const std::string bend_listing = Disassembly("early_bend_vs", early_bend.c_str(), "vs_5_0");
      if (early_bend.empty() || bend_listing.empty() || BufferLoadGuard(bend_listing, 11) != 0) return 158;
      // The object's block and the plant's exclude each other in the compiled program: the mode's
      // object and instance bits (18) are compared with 2 for the one and with 18 for the other.
      // Control: an object's test without the instance bit (a plant would run the object's block too,
      // and load its normals and its word twice) has no such comparison. (With the mode fixed the two
      // programs are the same: the plant's block overwrites all the object's block gives.)
      const std::string both = Changed(kClayVs, "  [branch] if ((light_mode & 18) == 2 && has_light) {", "  [branch] if ((light_mode & 2) && has_light) {");
      const std::string both_listing = Disassembly("both_vs", both.c_str(), "vs_5_0");
      if (both.empty() || both_listing.empty() || !HasLineWith(vs, "ieq", "2, 18)") || !HasLineWith(vs, "and", "1, 18)") ||
          HasLineWith(both_listing, "ieq", "2, 18)")) return 158;
    }
    // Every light load, by the blocks around it. The terrain normals: one load, in the terrain's block.
    // The normals and the word at t10: two loads each, one in the object's block and one in the
    // instanced plant's. t11: the weights inside BOTH of the object's tests (its block's, which has the
    // vertex bound, and the probe bit's), and the plant's bend inputs in the plant's block.
    if (LoadDepths(vs, 8) != "1" || LoadDepths(vs, 9) != "1,1" || LoadDepths(vs, 10) != "1,1" || LoadDepths(vs, 11) != "2,1" ||
        InnerLoadDepth(vs, 11) != 1 || InnerLoadDepth(vs, 10) != 1 || InnerLoadDepth(vs, 9) != 1 || InnerLoadDepth(vs, 8) != 1) {
      std::cerr << "nesting of the light loads: t8 " << LoadDepths(vs, 8) << "; t9 " << LoadDepths(vs, 9) << "; t10 "
                << LoadDepths(vs, 10) << "; t11 " << LoadDepths(vs, 11) << "\n";
      return 155;
    }
    // The bound and the clamp, in the compiled program (they are the reason a load is inside the draw's
    // buffer): the vertex is compared with light_count (word 2 of the block's register 8) by an unsigned
    // less-than, and the probe set with the literal 8 by an unsigned minimum.
    if (!HasLineWith(vs, "ult", "cb0[8].z") || !HasLineWith(vs, "umin", "l(8)")) return 155;
    // No pixel shader loads from a buffer bound by address at all.
    for (int i = 1; i < 6; ++i) {
      if (Disassembly(all[i].name, all[i].src, all[i].target).find("ld_structured") != std::string::npos) return 159;
    }
    // A draw without light reads no light input in either stage. Shown on the program such a draw runs:
    // each shader compiled with light_mode fixed (0: no light and no steep ground; 1: steep ground without
    // light) reads neither the frame's block (cb2) nor, outside the terrain shaders (whose cb1 is the
    // terrain block), the object block (cb1), loads from no light buffer the mode does not name, and does
    // not sample the base map. So what an unlit draw shows cannot depend on what b1, b2, t7, t9 and t10
    // hold, on any adapter.
    {
      const std::string vs0 = Disassembly("clay_vs_mode0", WithLightMode(kClayVs, 0).c_str(), "vs_5_0");
      const std::string vs1 = Disassembly("clay_vs_mode1", WithLightMode(kClayVs, 1).c_str(), "vs_5_0");
      if (vs0.empty() || vs1.empty()) return 160;
      if (ReadsBlock(vs0, "cb1") || ReadsBlock(vs0, "cb2") || ReadsResource(vs0, 8) || ReadsResource(vs0, 9) ||
          ReadsResource(vs0, 10) || ReadsResource(vs0, 11)) {
        std::cerr << "the vertex shader of a draw without light reads a light input\n";
        return 160;
      }
      if (ReadsBlock(vs1, "cb1") || ReadsBlock(vs1, "cb2") || !ReadsResource(vs1, 8) || ReadsResource(vs1, 9) ||
          ReadsResource(vs1, 10) || ReadsResource(vs1, 11)) return 160;
      // (what it always reads is still read: the draw's block, positions, indices)
      if (!ReadsBlock(vs0, "cb0") || !ReadsResource(vs0, 0) || !ReadsResource(vs0, 1)) return 160;
      const std::string textured0 = Disassembly("clay_textured_ps_mode0", WithLightMode(kClayTexturedPs, 0).c_str(), "ps_5_0");
      const std::string layer0 = Disassembly("clay_terrain_layer_ps_mode0", WithLightMode(kClayTerrainLayerPs, 0).c_str(), "ps_5_0");
      const std::string layer1 = Disassembly("clay_terrain_layer_ps_mode1", WithLightMode(kClayTerrainLayerPs, 1).c_str(), "ps_5_0");
      const std::string far0 = Disassembly("clay_terrain_far_ps_mode0", WithLightMode(kClayTerrainFarPs, 0).c_str(), "ps_5_0");
      if (textured0.empty() || layer0.empty() || layer1.empty() || far0.empty()) return 161;
      if (ReadsBlock(textured0, "cb1") || ReadsBlock(textured0, "cb2") || ReadsBlock(layer0, "cb2") ||
          ReadsBlock(layer1, "cb2") || ReadsBlock(far0, "cb2") || ReadsResource(layer0, 7) || ReadsResource(layer1, 7) ||
          ReadsResource(far0, 7)) {
        std::cerr << "a pixel shader of a draw without light reads a light input\n";
        return 161;
      }
      // (the unlit programs still read their own inputs: the albedo, the terrain block and colour)
      if (!ReadsResource(textured0, 3) || !ReadsBlock(layer0, "cb1") || !ReadsResource(layer0, 4) ||
          !ReadsResource(far0, 4)) return 161;
      // Every light_mode there is (six bits), on the program a draw with that mode runs: the vertex
      // shader loads from exactly the light registers at which the pass binds the draw's OWN buffer
      // (LightBufferSources, which is what clay_pass.cpp binds by), every such load is still inside a
      // test (the vertex bound is a root word), it loads from no register outside the layout, every
      // resource it declares has a parameter, and it has no loop.
      static_assert(fable2::native::render::kLightModeCount == 64);
      for (unsigned mode = 0; mode < fable2::native::render::kLightModeCount; ++mode) {
        const std::string text = WithLightMode(kClayVs, mode);
        const std::string listing = Disassembly("clay_vs_mode", text.c_str(), "vs_5_0");
        if (listing.empty()) return 160;
        const fable2::native::render::ClayLightBuffers own = fable2::native::render::LightBufferSources(mode);
        for (unsigned i = 0; i < fable2::native::render::kClayLightBufferCount; ++i) {
          const bool named = own.source[i] != fable2::native::render::ClayLightBuffer::kPositions;
          const int reg = int(fable2::native::render::kClayBindings[fable2::native::render::kClayLightBufferParam0 + i].reg);
          // (a mode with both the terrain and the object bit, which the pass never makes, need not load
          // the terrain normals: the object's block overwrites what they give; it must not load more)
          const bool expected = named && !(reg == 8 && (mode & 3) == 3);
          if (ReadsResource(listing, reg) != expected || (expected && BufferLoadGuard(listing, reg) != 1)) {
            std::cerr << "light_mode " << mode << ": t" << reg << " is " << (named ? "" : "not ") << "the draw's own buffer, and the "
                      << "vertex shader " << (ReadsResource(listing, reg) ? "loads from it" : "does not load from it")
                      << " (guard " << BufferLoadGuard(listing, reg) << ")\n";
            return 160;
          }
        }
        if (!LoadsOnlyFrom(listing, buffer_registers) || LoopInstructions(listing) != 0 ||
            BindingsMismatch("clay_vs_mode", text.c_str(), "vs_5_0", true) != 0) return 160;
        // The draw's slot at b1 (cb1). Not read without the object bit. An object or trunk (the object
        // bit without the instance bit): without the probe bit only its first five registers (the rows,
        // c49 / c27.w, c26 / the colour's power: 80 bytes), by fixed index; with the probe bit all
        // seventeen, the probe registers by the clamped set index. An instanced plant (both bits; the
        // probe bit beside them changes nothing): the first five by fixed index, of which it reads the
        // rows' fourth components and the power: what a small slot holds.
        bool dynamic = false;
        const unsigned registers = DeclaredRegisters(listing, 1, &dynamic);
        const bool any = (mode & 2) != 0, plant = any && (mode & 16) != 0, object = any && !plant, probe = object && (mode & 8) != 0;
        if (registers != (probe ? 17u : any ? 5u : 0u) || dynamic != probe || ReadsBlock(listing, "cb1") != any) {
          std::cerr << "light_mode " << mode << ": the vertex shader declares " << registers << " registers of cb1"
                    << (dynamic ? ", indexed by a value" : "") << "\n";
          return 160;
        }
        // What the pass binds at t10 and t11 is of the kind whose path the program takes (the three
        // compiled marks of the paths are held just below: the rows, the clamp, the bend constants):
        // an object's stream colours and nothing of its own at t11; a probe object's two regions; a
        // plant's colours and bend inputs, also where the probe bit stands beside the instance bit.
        {
          using B = fable2::native::render::ClayLightBuffer;
          const B want10 = plant ? B::kInstanceColours : probe ? B::kProbeSets : object ? B::kColours : B::kPositions;
          const B want11 = plant ? B::kBend : probe ? B::kProbeWeights : B::kPositions;
          if (own.source[2] != want10 || own.source[3] != want11) {
            std::cerr << "light_mode " << mode << ": the sources at t10 and t11 are not those of the path the vertex shader takes\n";
            return 160;
          }
        }
        // The rows (the xyz of the first three registers) are an object's or trunk's; a plant reads the
        // three fourth components and no row: its normal goes through no rotation.
        const bool rows = HasLineWith(listing, "dp3", "cb1[0].xyzx") && HasLineWith(listing, "dp3", "cb1[1].xyzx") &&
                          HasLineWith(listing, "dp3", "cb1[2].xyzx");
        const bool bend_constants = HasLineWith(listing, "mad_sat", "cb1[1].w, cb1[2].w") && HasLineWith(listing, "mul", "cb1[0].wwww");
        if (rows != object || bend_constants != plant || (plant && (listing.find("cb1[0].x") != std::string::npos ||
                                                                    listing.find("cb1[3]") != std::string::npos))) {
          std::cerr << "light_mode " << mode << ": rows " << rows << ", bend constants " << bend_constants << "\n";
          return 160;
        }
        // The frame's block (cb2): the axis (register 5), for a lit terrain draw, every object draw
        // and a plant of the family with the ambient factor; a plant without it reads the exponent,
        // the tint and the added light (registers 1 to 3) and no axis.
        const unsigned frame_registers = DeclaredRegisters(listing, 2);
        const unsigned frame_want = object ? 6u : plant ? ((mode & 32) ? 6u : 4u) : (mode & 5) == 5 ? 6u : 0u;
        if (ReadsBlock(listing, "cb2") != (any || (mode & 5) == 5) || frame_registers != frame_want) {
          std::cerr << "light_mode " << mode << ": the vertex shader declares " << frame_registers << " registers of cb2\n";
          return 160;
        }
        // The set index is clamped where it is used (an unsigned minimum with the literal 8 survives
        // compiling), and the probe colour's program has no power in it (no log, no exp): the stream
        // colour's and the plant's have.
        const bool clamp = HasOp(listing, "umin"), power = HasOp(listing, "log");
        if (clamp != probe || (probe && !HasLineWith(listing, "umin", "l(8)")) || power != ((object && !probe) || plant)) {
          std::cerr << "light_mode " << mode << ": clamp " << clamp << ", power " << power << "\n";
          return 160;
        }
        // Whatever light buffer the program loads from, it compares the vertex with light_count first.
        const bool loads_light = ReadsResource(listing, 8) || ReadsResource(listing, 9) || ReadsResource(listing, 10) || ReadsResource(listing, 11);
        if (loads_light && !HasLineWith(listing, "ult", "cb0[8].z")) return 160;
        // The bend: a plant's program writes it from its inputs (and zeros for a vertex past its
        // buffers); every other program writes zeros alone (so the interpolator a pixel shader may
        // read is never left unwritten, and never another draw's).
        if (!HasLineWith(listing, "mov", "o7.xyzw, l(0,0,0,0)") || HasLineWith(listing, "mul", "o7.xyzw") != plant) {
          std::cerr << "light_mode " << mode << ": the bend output is not " << (plant ? "the plant's" : "zero") << "\n";
          return 160;
        }
      }
      // The whole program (the mode a root word): all seventeen registers, the probe ones by the index.
      {
        bool dynamic = false;
        if (DeclaredRegisters(vs, 1, &dynamic) != 17 || !dynamic || !HasOp(vs, "umin") || !HasLineWith(vs, "umin", "l(8)")) return 160;
        // Control: without the clamp the index is the buffer's word as it is.
        const std::string loose = Changed(kClayVs, "uint a0 = min(word, 8u);", "uint a0 = word;");
        if (loose.empty() || HasOp(Disassembly("loose_vs", loose.c_str(), "vs_5_0"), "umin")) return 160;
      }
      // The textured pixel shader reads the object's first five registers only, lit or not: the probe
      // registers are the vertex stage's.
      {
        const std::string lit14 = Disassembly("clay_textured_ps_mode14", WithLightMode(kClayTexturedPs, 14).c_str(), "ps_5_0");
        const std::string whole = Disassembly("clay_textured_ps", kClayTexturedPs, "ps_5_0");
        bool dynamic = true;
        if (lit14.empty() || whole.empty() || DeclaredRegisters(whole, 1, &dynamic) != 5 || dynamic ||
            DeclaredRegisters(lit14, 1) != 5) return 161;
        // A lit object is the same pixel program whichever way its colour was made.
        if (lit14 != Disassembly("clay_textured_ps_mode6", WithLightMode(kClayTexturedPs, 6).c_str(), "ps_5_0")) return 161;
      }
      // Every light_mode, pixel stage (the textured shader): no buffer load; the draw's slot (cb1) read
      // by a lit object or trunk alone (five registers: c27.w, c49, c26), never by a plant (its slot
      // holds vertex-stage values only) and never without light; the frame's block by every lit draw
      // (a plant without the ambient factor: the sun's two registers; with it also the added light and
      // the floor: five); the bend input (v7) declared by a lit plant alone. And a mode's program is
      // the program of its lit, instance and plant-lit bits: nothing else of the mode reaches the stage.
      {
        std::string by_bits[64];
        for (unsigned mode = 0; mode < fable2::native::render::kLightModeCount; ++mode) {
          by_bits[mode] = Disassembly("clay_textured_ps_mode", WithLightMode(kClayTexturedPs, mode).c_str(), "ps_5_0");
          const std::string& listing = by_bits[mode];
          if (listing.empty() || listing.find("ld_structured") != std::string::npos || LoopInstructions(listing) != 0) return 161;
          const bool lit = (mode & 4) != 0, plant = lit && (mode & 16) != 0, plant_lit = plant && (mode & 32) != 0;
          const unsigned slot = DeclaredRegisters(listing, 1), frame = DeclaredRegisters(listing, 2);
          if (slot != (lit && !plant ? 5u : 0u) || ReadsBlock(listing, "cb1") != (lit && !plant) ||
              frame != (!lit ? 0u : plant && !plant_lit ? 2u : 5u) || ReadsBlock(listing, "cb2") != lit ||
              HasLineWith(listing, "dcl_input_ps", "v7.xyzw") != plant) {
            std::cerr << "light_mode " << mode << ": the textured pixel shader declares " << slot << " registers of cb1 and " << frame
                      << " of cb2\n";
            return 161;
          }
          const unsigned canon = !lit ? 0u : plant ? (mode & 52u) : 4u;
          if (canon < mode && listing != by_bits[canon]) return 161;
        }
        // (the three lit programs are three programs)
        if (by_bits[4] == by_bits[20] || by_bits[4] == by_bits[52] || by_bits[20] == by_bits[52] || by_bits[0] == by_bits[4]) return 161;
      }
      // The lit programs do read them (the check above is not empty): mode 6 a lit object, 5 lit ground.
      const std::string vs6 = Disassembly("clay_vs_mode6", WithLightMode(kClayVs, 6).c_str(), "vs_5_0");
      const std::string textured6 = Disassembly("clay_textured_ps_mode6", WithLightMode(kClayTexturedPs, 6).c_str(), "ps_5_0");
      const std::string layer5 = Disassembly("clay_terrain_layer_ps_mode5", WithLightMode(kClayTerrainLayerPs, 5).c_str(), "ps_5_0");
      if (!ReadsBlock(vs6, "cb1") || !ReadsBlock(vs6, "cb2") || !ReadsResource(vs6, 9) || !ReadsResource(vs6, 10) ||
          ReadsResource(vs6, 8) || ReadsResource(vs6, 11) || !ReadsBlock(textured6, "cb1") || !ReadsBlock(textured6, "cb2") ||
          !ReadsBlock(layer5, "cb2") || !ReadsResource(layer5, 7)) return 161;
      // Controls: an unlit textured colour that takes anything from the frame's block, and a vertex
      // output whose value without light comes from it, are reported.
      std::string leak = kClayTexturedPs;
      const std::string unlit = "return float4(clay_linear(t.rgb * (0.75 + 0.25 * facet)),";
      size_t at = leak.find(unlit);
      if (at == std::string::npos) return 162;
      leak.replace(at, unlit.size(), "return float4(clay_linear(t.rgb * (0.75 + 0.25 * facet)) + f_tint,");
      const std::string leaking = Disassembly("leaking_ps", WithLightMode(leak.c_str(), 0).c_str(), "ps_5_0");
      if (leaking.empty() || !ReadsBlock(leaking, "cb2")) return 162;
      leak = kClayVs;
      const std::string axis = "o.axis = float3(0, 0, 0);";
      at = leak.find(axis);
      if (at == std::string::npos) return 162;
      leak.replace(at, axis.size(), "o.axis = f_axis;");
      const std::string leaking_vs = Disassembly("leaking_vs", WithLightMode(leak.c_str(), 0).c_str(), "vs_5_0");
      if (leaking_vs.empty() || !ReadsBlock(leaking_vs, "cb2")) return 162;
    }
  }
  // 163, 164: the shaders' light paths are the pinned text (the six pieces above), and a statement
  // added to one of them, or one of them changed, is seen. The first five controls are changes that
  // every test above lets through: a lit object halved, the layer's normal negated, the lit far colour
  // squared, the stream colour not raised to its power, the object's rows transposed.
  {
    if (int at = PinsMissing(kClayVs, kClayTexturedPs, kClayTerrainLayerPs, kClayTerrainFarPs)) {
      std::cerr << "a lit block is not the pinned text: block " << at << "\n";
      return 163;
    }
    const std::string halved = Changed(kClayTexturedPs, "      return float4(clay_lit(lit),", "      lit *= 0.5;\n      return float4(clay_lit(lit),");
    const std::string negated = Changed(kClayTerrainLayerPs, "    float facing = FacingGate(", "    N = -N;\n    float facing = FacingGate(");
    const std::string squared = Changed(kClayTerrainFarPs, "  if (t_flags & 1) lin *= terrain_fade_in(world);",
                                        "  if (light_mode & 4) lin = lin * lin;\n  if (t_flags & 1) lin *= terrain_fade_in(world);");
    const std::string no_power = Changed(kClayVs, " / 255.0, o_colour_power);", " / 255.0, 1.0);");
    const std::string transposed = Changed(kClayVs, "float3(dot(o_rot0.xyz, n), dot(o_rot1.xyz, n), dot(o_rot2.xyz, n));",
                                           "o_rot0.xyz * n.x + o_rot1.xyz * n.y + o_rot2.xyz * n.z;");
    // The probe colour's: a row's dot product dropped, a power put on it, the set not clamped, the
    // weights read as the word's register would be (the wrong vertex bound is the pass's, not here).
    const std::string no_row = Changed(kClayVs, "dot(o_probe[a0 + 2], w), dot(o_probe[a0 + 3], w));", "dot(o_probe[a0 + 2], w), 0.0);");
    const std::string powered = Changed(kClayVs, "      o.vcolour = o_probe[a0].xyz + float3(",
                                        "      o.vcolour = light_pow(o_probe[a0].xyz, 2.2) + float3(");
    const std::string loose = Changed(kClayVs, "uint a0 = min(word, 8u);", "uint a0 = word;");
    const std::string three = Changed(kClayVs, "dot(o_probe[a0 + 1], w),", "dot(o_probe[a0 + 1].xyz, w.xyz),");
    if (no_row.empty() || powered.empty() || loose.empty() || three.empty()) return 164;
    if (!Compile("no_row_vs", no_row.c_str(), "vs_5_0") || !Compile("powered_vs", powered.c_str(), "vs_5_0") ||
        !Compile("loose_vs", loose.c_str(), "vs_5_0") || !Compile("three_vs", three.c_str(), "vs_5_0")) return 164;
    for (const std::string* changed : {&no_row, &powered, &loose, &three}) {
      if (PinsMissing(changed->c_str(), kClayTexturedPs, kClayTerrainLayerPs, kClayTerrainFarPs) != 1) return 164;
    }
    if (halved.empty() || negated.empty() || squared.empty() || no_power.empty() || transposed.empty()) return 164;
    // (each still compiles: it is a change a compiler would not stop)
    if (!Compile("halved_ps", halved.c_str(), "ps_5_0") || !Compile("negated_ps", negated.c_str(), "ps_5_0") ||
        !Compile("squared_ps", squared.c_str(), "ps_5_0") || !Compile("no_power_vs", no_power.c_str(), "vs_5_0") ||
        !Compile("transposed_vs", transposed.c_str(), "vs_5_0")) return 164;
    if (PinsMissing(kClayVs, halved.c_str(), kClayTerrainLayerPs, kClayTerrainFarPs) != 2 ||
        PinsMissing(kClayVs, kClayTexturedPs, negated.c_str(), kClayTerrainFarPs) != 4 ||
        PinsMissing(kClayVs, kClayTexturedPs, kClayTerrainLayerPs, squared.c_str()) != 5 ||
        PinsMissing(no_power.c_str(), kClayTexturedPs, kClayTerrainLayerPs, kClayTerrainFarPs) != 1 ||
        PinsMissing(transposed.c_str(), kClayTexturedPs, kClayTerrainLayerPs, kClayTerrainFarPs) != 1) return 164;
    // A statement added to the terrain shaders' common output (before its lit return) is seen too.
    const std::string out_changed = Changed(kClayTerrainFarPs, "  if (light_mode & 4) return float4(clay_lit(colour),",
                                            "  colour *= 0.5;\n  if (light_mode & 4) return float4(clay_lit(colour),");
    if (out_changed.empty() || PinsMissing(kClayVs, kClayTexturedPs, kClayTerrainLayerPs, out_changed.c_str()) != 3) return 164;
    // 165: changes placed just OUTSIDE the lit branches, which the pins of the branches alone let
    // through (the re-review of plan Task 7's fix round: nine such changes, each compiles): a statement
    // added inside a light function or inside sample_base, and a statement added above a shader's lit
    // branch. Each is reported, by the piece it is in.
    {
      const struct {
        const char* name;
        int shader;  // 0 vertex, 1 textured, 2 layer, 3 far
        const char* before;
        const char* after;
        int piece;
      } outside[] = {
          {"LightObject", 1, "  return A * (c27w * light_ambient(vcolour,", "  A *= 0.5;\n  return A * (c27w * light_ambient(vcolour,", 6},
          {"LightTerrainLayer", 2, "  return A * (c11x * light_ambient(baked,", "  A *= 0.5;\n  return A * (c11x * light_ambient(baked,", 6},
          {"light_sun", 3, "  return sun_col * (saturate(n_dot_sun) * sun_gate);",
           "  n_dot_sun = 1.0;\n  return sun_col * (saturate(n_dot_sun) * sun_gate);", 6},
          {"AmbientAxis", 0, "  float k = 0.5 + 0.5 * dot(normal, axis);", "  axis = -axis;\n  float k = 0.5 + 0.5 * dot(normal, axis);", 6},
          {"sample_base", 2, "  if (t_base_sampler == 0) return t_base.SampleGrad(s_point_wrap, at, d.xy, d.zw);",
           "  at = at * 0.5;\n  if (t_base_sampler == 0) return t_base.SampleGrad(s_point_wrap, at, d.xy, d.zw);", 3},
          {"layer normal", 2, "  [branch] if (light_mode & 4) {\n    float3 N = light_normalize(normal);\n    float facing",
           "  if (light_mode & 4) normal = -normal;\n  [branch] if (light_mode & 4) {\n    float3 N = light_normalize(normal);\n    float facing", 4},
          {"far squared", 3, "  float2 base_at = world.xy * t_base_uv.xy + t_base_uv.zw;",
           "  lin = lin * lin;\n  float2 base_at = world.xy * t_base_uv.xy + t_base_uv.zw;", 5},
          {"textured vcolour", 1, "    if (light_mode & 4) {\n      float3 lit;", "    vcolour *= 0.5;\n    if (light_mode & 4) {\n      float3 lit;", 2},
          {"far base_at", 3, "  float2 base_at = world.xy * t_base_uv.xy + t_base_uv.zw;", "  float2 base_at = uv;", 5},
          // ... and an insertion, a deletion and two reorderings inside the probe colour's block.
          {"probe insert", 0, "      float4 w = probe_weights[v];", "      float4 w = probe_weights[v];\n      w = abs(w);", 1},
          {"probe delete", 0, "o.vcolour = o_probe[a0].xyz + float3(", "o.vcolour = float3(", 1},
          {"probe rows", 0, "dot(o_probe[a0 + 1], w), dot(o_probe[a0 + 2], w),", "dot(o_probe[a0 + 2], w), dot(o_probe[a0 + 1], w),", 1},
          {"probe order", 0, "      uint a0 = min(word, 8u);\n      float4 w = probe_weights[v];",
           "      float4 w = probe_weights[v];\n      uint a0 = min(word, 8u);", 1},
          // ... a statement before the light path of the vertex stage, and one in the unlit path's place.
          {"vertex position", 0, "  o.world = p.xyz;", "  o.world = p.xyz * 2.0;", 1},
          {"terrain axis", 0, "    if (light_mode & 4) o.axis = AmbientAxis(o.normal, f_axis);", "    o.axis = AmbientAxis(o.normal, f_axis);", 1},
          // The instanced plants (plan Task 8b). The vertex stage: the strength dropped, the height taken
          // from another component, the normal put through the object's rows, the exponent without its
          // factor, the added light in the family that has none there, the vector of t not bent towards
          // the axis. The pixel stage: the sun by the unbent normal, t by the unbent normal, a c26 on a
          // plant. The functions: the height blend not saturated in the bend, the normal not normalised
          // before the blend, the ambient factor on the added light.
          {"plant strength", 0, "PlantHeight(b.w, o_rot1.w, o_rot2.w), o_rot0.w);", "PlantHeight(b.w, o_rot1.w, o_rot2.w), 1.0);", 1},
          {"plant height", 0, "PlantHeight(b.w,", "PlantHeight(b.z,", 1},
          {"plant rows", 0, "    o.normal = n;\n    uint tone",
           "    o.normal = float3(dot(o_rot0.xyz, n), dot(o_rot1.xyz, n), dot(o_rot2.xyz, n));\n    uint tone", 1},
          {"plant exponent", 0, "InstanceAmbient(colour, f_tint, o_colour_power * f_tint_pow, f_add);",
           "InstanceAmbient(colour, f_tint, f_tint_pow, f_add);", 1},
          {"plant-lit added light", 0, "o_colour_power * f_tint_pow, float3(0, 0, 0));", "o_colour_power * f_tint_pow, f_add);", 1},
          {"plant-lit axis", 0, "      o.axis = AmbientAxis(n, f_axis);\n      o.vcolour = InstanceAmbient", "      o.axis = n;\n      o.vcolour = InstanceAmbient", 1},
          {"plant sun", 1, "lit = LightPlant(clay_linear(t.rgb), vcolour, dot(Np, f_sun_dir),",
           "lit = LightPlant(clay_linear(t.rgb), vcolour, dot(normal, f_sun_dir),", 2},
          {"plant-lit t", 1, "AmbientT(axis, Np)", "AmbientT(axis, normal)", 2},
          {"plant c26", 1, "f_sun_col, SunGateSingle(1.0, 1.0));\n        }\n      } else {",
           "f_sun_col, SunGateSingle(1.0, 1.0));\n        }\n        lit *= o_c26;\n      } else {", 2},
          {"PlantBend", 0, "  float h = saturate(height);", "  float h = height;", 6},
          {"PlantNormal", 1, "  float3 n = light_normalize(normal);\n  return light_normalize(n + (bend.xyz - n)",
           "  float3 n = normal;\n  return light_normalize(n + (bend.xyz - n)", 6},
          {"LightPlantLit", 1, "  return A * (vs_base * AmbientFactor(amb_floor, t) + add + light_sun(",
           "  return A * ((vs_base + add) * AmbientFactor(amb_floor, t) + light_sun(", 6},
      };
      const char* sources[4] = {kClayVs, kClayTexturedPs, kClayTerrainLayerPs, kClayTerrainFarPs};
      for (const auto& m : outside) {
        const std::string text = Changed(sources[m.shader], m.before, m.after);
        if (text.empty() || !Compile(m.name, text.c_str(), m.shader == 0 ? "vs_5_0" : "ps_5_0")) {
          std::cerr << "the control '" << m.name << "' is not a change that compiles\n";
          return 165;
        }
        const char* with[4] = {kClayVs, kClayTexturedPs, kClayTerrainLayerPs, kClayTerrainFarPs};
        with[m.shader] = text.c_str();
        const int got = PinsMissing(with[0], with[1], with[2], with[3]);
        if (got != m.piece) {
          std::cerr << "the control '" << m.name << "' is reported as piece " << got << ", not " << m.piece << "\n";
          return 165;
        }
      }
    }
  }
  // Negative control: broken HLSL must fail (proves the harness reports errors).
  if (Compile("broken_ps", "float4 main() : SV_Target { return undefined_symbol; }", "ps_5_0"))
    return 4;
  std::cout << "PASS: fullscreen VS, pattern PS, grid PS, clay VS/PS/textured PS, composite PS compile; alpha fields declared, alpha test discards; terrain layer and far PS compile, discard, constants at the table's offsets; steep-ground block; draw constants at ClayConstants' offsets in all five clay shaders; linear outputs, output mapping; light functions, lit paths, plant paths, light blocks at their tables' offsets and registers, every light_mode's compiled program in both stages; broken shader rejected\n";
  return 0;
}
