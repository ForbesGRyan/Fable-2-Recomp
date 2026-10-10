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
#include <vector>

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

// Whether an instruction of the compiled shader reads component `component` of the operand `operand`
// ("cb2[2]"): the operand followed by a swizzle that has the component.
static bool ReadsComponent(const std::string& listing, const char* operand, char component) {
  const std::string needle = std::string(operand) + ".";
  size_t at = 0;
  while (at < listing.size()) {
    size_t end = listing.find('\n', at);
    if (end == std::string::npos) end = listing.size();
    const size_t first = listing.find_first_not_of(" \t", at);
    if (first < end && listing[first] != '/' && listing.compare(first, 4, "dcl_") != 0) {
      for (size_t hit = listing.find(needle, first); hit != std::string::npos && hit < end; hit = listing.find(needle, hit + 1)) {
        for (size_t c = hit + needle.size(); c < end && listing[c] >= 'w' && listing[c] <= 'z'; ++c) {
          if (listing[c] == component) return true;
        }
      }
    }
    at = end + 1;
  }
  return false;
}

// The instruction and declaration lines of a listing (its comment lines, which name the buffers'
// variables, and its blank lines left out): what the GPU runs.
static std::string Code(const std::string& listing) {
  std::string code;
  size_t at = 0;
  while (at < listing.size()) {
    size_t end = listing.find('\n', at);
    if (end == std::string::npos) end = listing.size();
    const size_t first = listing.find_first_not_of(" \t\r", at);
    if (first < end && listing[first] != '/') code.append(listing, first, end - first).push_back('\n');
    at = end + 1;
  }
  return code;
}

// How often `needle` stands in `text`.
static int Count(const char* text, const char* needle) {
  int n = 0;
  for (const char* at = strstr(text, needle); at; at = strstr(at + 1, needle)) ++n;
  return n;
}

// The clay shader `src` as it was before the exposure came into its lit returns (commit 24c1751): the
// frame's field a padding word, the lit values written as they are. Empty when a text is not found.
static std::string WithoutExposure(const char* src, bool pixel_lit) {
  std::string text = src;
  const struct {
    const char* now;
    const char* before;
    bool wanted;
  } changes[] = {{"float f_exposure;", "float f_pad3;", true},
                 {"clay_lit(lit * f_exposure)", "clay_lit(lit)", false},
                 {"clay_lit(colour * f_exposure)", "clay_lit(colour)", false}};
  int returns = 0;
  for (const auto& c : changes) {
    const size_t at = text.find(c.now);
    if (at == std::string::npos) {
      if (c.wanted) return {};
      continue;
    }
    text.replace(at, strlen(c.now), c.before);
    if (!c.wanted) ++returns;
  }
  // (the unlit draws' fog, plan Task 6 of the fog sub-project, names the exposure in the shared light
  // text; no program without light_mode's bit 7 reaches it)
  const char* unlit_fog = "(f_fog_scatter * f_exposure * scale)";
  const size_t fog_at = text.find(unlit_fog);
  if (fog_at == std::string::npos) return {};
  text.replace(fog_at, strlen(unlit_fog), "(f_fog_scatter * scale)");
  if (returns != (pixel_lit ? 1 : 0) || text.find("f_exposure") != std::string::npos) return {};
  return text;
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

// The two post shaders as plan Task 5 of the post sub-project committed them (e7e5ff7; before the bloom and
// before the writes' own rounding): case 237 holds today's programs against them.
static const char* const kPostCompositeBefore = R"hlsl(
cbuffer Post : register(b0) {
  float p_c31x; float p_c46x; float p_c47x; float p_c77x;          // the composite 37B1: c31.x, c46.x, c47.x, c77.x
  float p_c77y; float p_c78x; float p_scene_max; float p_previous; // c77.y, c78.x; the scene's clamp; (plan Task 15)
  float p_b31x; float p_b46x; float p_b47x; float p_b77x;          // the bright pass EE92 (plan Task 8)
  float p_b77y; uint p_bloom_on; uint p_sun_on; uint p_pad;
  float4 p_blur[4];                                                // the blurs' weights (plan Task 8)
  float4 p_radial;                                                 // the radial blur's registers (plan Task 14)
  float4 p_table[128];                                             // the composite's table: texel i at [i >> 2][i & 3]
  float4 p_ramp[256];                                              // the scan-out ramp: entry i (rgb)
};
)hlsl" R"hlsl(
Texture2D<float4> scene_target : register(t0);
float post_lum(float3 c) { return dot(c, float3(0.2125, 0.7154, 0.0721)); }
float3 post_scene(float3 v) { return (v > 0.0) ? min(v, p_scene_max) : float3(0.0, 0.0, 0.0); }
float post_table(float u) {
  uint i = min(uint(512.0 * saturate(u) + 0.5), 511u);
  return p_table[i >> 2][i & 3];
}
float3 post_pow(float3 base, float exponent) {
  float3 b = abs(base);
  return (b > 0.0 && b < 3.0e38) ? exp2(exponent * log2(b)) : float3(0.0, 0.0, 0.0);
}
float3 PostComposite(float3 scene, float3 bloom) {
  float3 s = post_scene(scene);
  float L = post_lum(s);
  float e = p_c77y * post_table(p_c46x * L);
  float3 c = saturate(e * (L + saturate(p_c47x * L) * (s - L)));
  float3 b = saturate(bloom * p_c31x);
  float3 scr = c + b * (1.0 - c);
  float3 t = scr + p_c77x * (scr - post_lum(scr));
  return saturate(post_pow(t, p_c78x));
}
float4 main(float4 pos : SV_Position) : SV_Target {
  float3 scene = scene_target.Load(int3(pos.xy, 0)).rgb;
  return float4(PostComposite(scene, float3(0.0, 0.0, 0.0)), 1.0);
}
)hlsl";
static const char* const kPostRampBefore = R"hlsl(
cbuffer Post : register(b0) {
  float p_c31x; float p_c46x; float p_c47x; float p_c77x;          // the composite 37B1: c31.x, c46.x, c47.x, c77.x
  float p_c77y; float p_c78x; float p_scene_max; float p_previous; // c77.y, c78.x; the scene's clamp; (plan Task 15)
  float p_b31x; float p_b46x; float p_b47x; float p_b77x;          // the bright pass EE92 (plan Task 8)
  float p_b77y; uint p_bloom_on; uint p_sun_on; uint p_pad;
  float4 p_blur[4];                                                // the blurs' weights (plan Task 8)
  float4 p_radial;                                                 // the radial blur's registers (plan Task 14)
  float4 p_table[128];                                             // the composite's table: texel i at [i >> 2][i & 3]
  float4 p_ramp[256];                                              // the scan-out ramp: entry i (rgb)
};
)hlsl" R"hlsl(
Texture2D<float4> front_target : register(t0);
uint post_ramp_index(float v) { return min(uint(saturate(v) * 255.0 + 0.5), 255u); }
float4 main(float4 pos : SV_Position) : SV_Target {
  float3 v = front_target.Load(int3(pos.xy, 0)).rgb;
  return float4(p_ramp[post_ramp_index(v.r)].r, p_ramp[post_ramp_index(v.g)].g, p_ramp[post_ramp_index(v.b)].b, 1.0);
}
)hlsl";

// The smallest number of nested `if` blocks around an instruction `op` that names the texture at register
// t<reg> (0: one outside every block; -1: none at all), and how many such instructions there are.
static int TextureReadDepth(const std::string& listing, const char* op, int reg, int* count = nullptr) {
  const std::string needle = ", t" + std::to_string(reg) + ".";
  const size_t n = strlen(op);
  int depth = 0, least = -1, found = 0;
  size_t at = 0;
  while (at < listing.size()) {
    size_t end = listing.find('\n', at);
    if (end == std::string::npos) end = listing.size();
    const size_t first = listing.find_first_not_of(" \t", at);
    if (first < end && listing[first] != '/') {
      const std::string line = listing.substr(first, end - first);
      if (line.compare(0, 3, "if_") == 0) ++depth;
      if (line.compare(0, 5, "endif") == 0) --depth;
      if (line.compare(0, n, op) == 0 && line.find(needle) != std::string::npos) {
        ++found;
        least = least < 0 ? depth : (depth < least ? depth : least);
      }
    }
    at = end + 1;
  }
  if (count) *count = found;
  return least;
}

// The number of instruction lines of a listing (declarations and comments left out).
static int Instructions(const std::string& listing) {
  int n = 0;
  size_t at = 0;
  while (at < listing.size()) {
    size_t end = listing.find('\n', at);
    if (end == std::string::npos) end = listing.size();
    const size_t first = listing.find_first_not_of(" \t\r", at);
    if (first < end && listing[first] != '/' && listing.compare(first, 4, "dcl_") != 0 && listing.compare(first, 3, "ps_") != 0) ++n;
    at = end + 1;
  }
  return n;
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

// The clay shader `src` as it was before the unlit word (the post sub-project's plan Task 5): the frame's
// field a padding word, no unlit colour multiplied. Empty when nothing of it is found. `one`: the multiply
// is kept, by the literal 1 (what the word is in every frame without post).
static std::string WithoutPost(const char* src, bool one = false) {
  std::string text = src;
  const std::string field = "float f_post; ", mul = " * f_post";
  const size_t at = text.find(field);
  if (at == std::string::npos) return {};
  text.replace(at, field.size(), "float f_pad0;");
  for (size_t hit = text.find(mul); hit != std::string::npos; hit = text.find(mul, hit)) text.replace(hit, mul.size(), one ? " * 1.0" : "");
  if (text.find("f_post") != std::string::npos) return {};
  return text;
}

// Whether the compiled shader reads anything of the frame's block (cb2) but the unlit word, cb2[3].w
// (f_post: what an unlit colour is multiplied by, which every unlit program reads since the post chain).
static bool ReadsFrameBeyondPost(const std::string& listing) {
  std::string rest = listing;
  const std::string word = "cb2[3].";
  for (size_t at = rest.find(word); at != std::string::npos; at = rest.find(word, at)) {
    size_t end = at + word.size();
    while (end < rest.size() && rest[end] == 'w') ++end;
    const bool only_w = end > at + word.size() && (end >= rest.size() || rest[end] < 'x' || rest[end] > 'z');
    if (only_w) {
      rest.erase(at, end - at);
    } else {
      at = end;
    }
  }
  return ReadsBlock(rest, "cb2");
}

// Everything between a clay shader's inputs and its lit output as pinned text, exactly as in
// fable2_native_shaders.h: the tests above find statements by their text and so cannot see a statement
// that is added between them, before them, or inside a function they call. A change to a shader's
// light path has to be made here as well, on purpose. Six pieces:
//   kPinLight        the shared light block: cbuffer Frame and every light function, light_normalize to
//                    clay_lit, the plants' and the leaves' among them (in all four shaders that light);
//   kPinVsMain       the vertex shader's whole main (position, coordinates, the terrain normal, the
//                    object's normal, the vector of t, the stream colour and the probe colour; an
//                    instanced plant's normal, bend and colour; a leaf's normal, ambient and two light
//                    scalars);
//   kPinTexturedMain the textured pixel shader from clay_linear to the end of main (the object's, the
//                    plants' and the leaves' lit paths);
//   kPinTerrainCommon the terrain shaders' functions: sample_colour, sample_base, terrain_fade_in,
//                    alpha test and output (terrain_out);
//   kPinLayerMain    the layer shader from sample_side to the end of main;
//   kPinFarMain      the far shader's whole main.
// Outside the pins: the declarations (cbuffers, textures, samplers, buffers; held by reflection against
// their tables and the layout) and alpha_passes (it decides a discard, not a colour).
static const char kPinLight[] = R"pin(cbuffer Frame : register(b2) {
  float3 f_sun_dir; uint f_light_on;     // c80.xyz of a one-light draw (= c109.xyz), as it is; 1: the frame is lit
  float3 f_sun_col; float f_tint_pow;    // c87.xyz; c21.w
  float3 f_tint; float f_exposure;       // c21.xyz; the exposure, which the lit pixel paths alone multiply by
  float3 f_add; float f_post;            // c20.xyz; what every unlit colour is multiplied by (1 but in a post frame)
  float3 f_floor; float f_mask_du;       // c22.xyz; 1 / the target's width: with f_mask_dv the screen mask's coordinate
  float3 f_axis; float f_mask_dv;        // c23.xyz (= vertex c19.xyz); 1 / the target's height
  float3 f_eye; uint f_eye_on;           // c19.xyz, the eye (= vertex c9.xyz of the leaf shaders); 1: it is usable
#ifdef CLAY_FOG
  float3 f_fog_axis; uint f_fog_on;      // c66.xyz; 1: the frame is fogged (0: everything from here on is zero)
  float3 f_fog_near; float f_fog_start;  // c68.rgb; c67.x
  float f_fog_slope; float f_fog_scale; float f_fog_amount; float f_fog_scatter;  // c67.z; c67.w; c28.x; c28.y
  float4 f_inv0; float4 f_inv1; float4 f_inv2; float4 f_inv3;   // the frame's inverse view-projection, rows
  float4 f_scatter[64];                  // tf4: the in-scatter colour by the angle to the axis (rgb)
  float4 f_transmit[256];                // tf5: the transmittance by distance (rgb)
#endif
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
// The tree leaves (lighting-formulas.md, "Tree leaves"; the mirrors are in clay_logic.h). The game's leaf
// vertex shaders make the ambient and the light's two scalars on the side of the card the viewer sees
// (the cards are drawn without culling; the normal, used as it is, is turned to the viewer); its pixel
// shader adds them on the albedo, the back light on leaf texels only. Split the same way. The one
// division of the light is LeafLight's by 1 + wrap (the pass refuses a draw whose wrap is negative).
float2 LeafLight(float3 normal, float3 view, float3 light_dir, float wrap, float spot) {
  float3 n = (dot(view, normal) < 0.0) ? -normal : normal;
  float nl = dot(light_dir, n);
  float d = 1.0 + wrap;
  float front = (d != 0.0) ? saturate((nl + wrap) / d) : 0.0;
  float turn = 1.0 - saturate(dot(view, n));
  return float2(front, saturate(-nl) * (1.0 - turn * turn)) * spot;
}
float2 LeafMask(float mask, float gain, float bias) {
  float m1 = saturate(mask) * gain + bias;
  return float2(m1, m1 * gain + bias);
}
float3 LightLeaf(float3 A, float3 vs_ambient, float front, float back, float3 sun_col, float c27w, float m1, float m2,
                 float3 c49) {
  float green = (A.g >= A.r) ? 1.0 : 0.0;
  return (A * (vs_ambient * c27w + m1 * sun_col * front) + green * A * m2 * sun_col * back) * c49;
}
// What a lit draw writes: the value itself, a number, not negative, at most 64 (ClayLit in clay_logic.h).
float3 clay_lit(float3 v) { return (v > 0.0) ? min(v, 64.0) : float3(0.0, 0.0, 0.0); }
#ifdef CLAY_FOG
float3 fog_scatter(float u) {
  float x = clamp(saturate(u) * 64.0 - 0.5, 0.0, 63.0);
  uint i = min(uint(x), 63u);
  uint j = min(i + 1u, 63u);
  return lerp(f_scatter[i].rgb, f_scatter[j].rgb, x - float(i));
}
float3 fog_transmit(float u) {
  float x = clamp(saturate(u) * 256.0 - 0.5, 0.0, 255.0);
  uint i = min(uint(x), 255u);
  uint j = min(i + 1u, 255u);
  return lerp(f_transmit[i].rgb, f_transmit[j].rgb, x - float(i));
}
struct FogPoint { bool fogged; float a; float s; float t; };
FogPoint FogCoords(float4 pos) {
  FogPoint o;
  float4 S = float4(pos.x * (2.0 * f_mask_du) - 1.0, 1.0 - pos.y * (2.0 * f_mask_dv), pos.z, 1.0);
  float w = dot(f_inv3, S);
  bool placed = abs(w) > 0.0 && abs(w) < 3.0e38;
  float3 V = f_eye - float3(dot(f_inv0, S), dot(f_inv1, S), dot(f_inv2, S)) / (placed ? w : 1.0);
  float d2 = dot(V, V);
  bool reach = placed && d2 > 0.0 && d2 < 1.0e30;
  float q = reach ? d2 : 1.0;
  float inv = rsqrt(q);
  float dD = q * inv - f_fog_start;
  o.fogged = f_fog_on != 0 && reach && dD > 0.0;
  o.a = saturate(dot(V, f_fog_axis) * -inv);
  o.s = f_fog_scale * dD;
  o.t = saturate(f_fog_slope * dD);
  return o;
}
float3 FogForm(float3 c, FogPoint p, float scale, float form) {
  [branch] if (p.fogged && form != 2.0) {
    float3 S = fog_scatter(p.a);
    float3 T = fog_transmit(p.s);
    if (form == 1.0) {
      c = c * T + (1.0 - T) * S * scale;
    } else {
      float3 F = f_fog_near + p.t * (S - f_fog_near);
      float3 G = c * T + (1.0 - T) * f_fog_scatter * F;
      c = c + f_fog_amount * (G - c);
    }
  }
  return c;
}
float3 FogUnlit(float3 c, FogPoint p, float scale) {
  [branch] if (p.fogged) {
    float3 S = fog_scatter(p.a);
    float3 T = fog_transmit(p.s);
    float3 F = f_fog_near + p.t * (S - f_fog_near);
    float3 G = c * T + (1.0 - T) * (f_fog_scatter * f_exposure * scale) * F;
    c = clay_lit(c + f_fog_amount * (G - c));
  }
  return c;
}
float3 FogUnlitTree(float3 c, FogPoint p, float scale) {
  [branch] if (p.fogged) c = clay_lit(FogForm(c, p, scale, 1.0));
  return c;
}
#else
struct FogPoint { bool fogged; };
FogPoint FogCoords(float4 pos) { FogPoint o; o.fogged = false; return o; }
float3 FogForm(float3 c, FogPoint p, float scale, float form) { return c; }
float3 FogUnlit(float3 c, FogPoint p, float scale) { return c; }
float3 FogUnlitTree(float3 c, FogPoint p, float scale) { return c; }
#endif
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
  // (82: the object bit, the instance bit and the leaf bit. An object or trunk has the first alone.)
  [branch] if ((light_mode & 82) == 2 && has_light) {
    // The object's normal as the game's vertex shader exports it: (R n) * c7.w, not normalised.
    float3 n = normals[v];
    o.normal = float3(dot(o_rot0.xyz, n), dot(o_rot1.xyz, n), dot(o_rot2.xyz, n));
    o.axis = AmbientAxis(o.normal, f_axis);
    // t11 holds one word a vertex: the stream colour, or (light_mode bit 3) the vertex's probe set.
    uint word = colours[v];
    [branch] if (light_mode & 8) {
      // The probe colour as the game's vertex shaders form it, with no power:
      // c[28 + a0].xyz + (c[29 + a0] . w, c[30 + a0] . w, c[31 + a0] . w), a0 clamped to the last set.
      uint a0 = min(word, 8u);
      float4 w = probe_or_bend[v];
      o.vcolour = o_probe[a0].xyz + float3(dot(o_probe[a0 + 1], w), dot(o_probe[a0 + 2], w), dot(o_probe[a0 + 3], w));
    } else {
      o.vcolour = light_pow(float3(word & 255, (word >> 8) & 255, (word >> 16) & 255) / 255.0, o_colour_power);
    }
  }
  [branch] if ((light_mode & 82) == 18 && has_light) {
    // An instanced plant. Its normal is in the world already (normalize(M n) of its copy's rows): no
    // rows here. t11 holds the colour of the vertex's copy, t12 the copy's up axis (xyz) and the
    // vertex's height in the copy (w); the draw's bend constants c45.x, .y, .z are the fourth
    // components of the three rows. As the game's plant vertex shaders: the bend of the normal, and
    // the colour raised to c18.w (2.2 * c21.w) times c18.xyz (c21.xyz), plus c54.xyz (c20.xyz) where
    // the pixel shader has no ambient factor; where it has one (light_mode bit 5), the factor's
    // vector and no added light.
    float3 n = normals[v];
    o.normal = n;
    uint tone = colours[v];
    float4 b = probe_or_bend[v];
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
  // (90: those three bits and the probe bit. A leaf is a probe record with the leaf bit.)
  [branch] if ((light_mode & 90) == 74 && has_light) {
    // A tree leaf, as the game's leaf vertex shaders: the normal through the rows, as it is; the probe
    // colour (the set at t11 clamped, the weights at t12) raised to c18.w * 0.4545 (2.2 * c21.w * 0.4545)
    // times c18.xyz (c21.xyz) plus c54.xyz (c20.xyz); the world position (M p) / c7.w + c4..c6 .w and
    // the unit vector from it to the eye; and the sun's two scalars on the side of the card the viewer
    // sees (the frame's sun stands in the vertex side's light set: a direction, no cone, no range), in
    // the first two components of the bend's interpolator. A leaf has no vector of t.
    float3 n = normals[v];
    float3 N = float3(dot(o_rot0.xyz, n), dot(o_rot1.xyz, n), dot(o_rot2.xyz, n));
    o.normal = N;
    o.axis = float3(0, 0, 0);
    uint a0 = min(colours[v], 8u);
    float4 w = probe_or_bend[v];
    float3 probe = o_probe[a0].xyz + float3(dot(o_probe[a0 + 1], w), dot(o_probe[a0 + 2], w), dot(o_probe[a0 + 3], w));
    o.vcolour = InstanceAmbient(probe, f_tint, o_colour_power * f_tint_pow, f_add);
    float3 at = float3(dot(o_rot0.xyz, p.xyz), dot(o_rot1.xyz, p.xyz), dot(o_rot2.xyz, p.xyz)) * o_leaf.y +
                float3(o_rot0.w, o_rot1.w, o_rot2.w);
    o.bend = float4(LeafLight(N, light_normalize(f_eye - at), light_normalize(f_sun_dir), o_leaf.x, 1.0), 0, 0);
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
#ifdef CLAY_DEPTH_ONLY
    return float4(0.0, 0.0, 0.0, 0.0);
#else
    if (light_mode & 4) {
      float3 lit;
      // The sun's shadow at the pixel: every family multiplies its sun term by it, and nothing else.
      float mask = shadow_mask.SampleLevel(s_linear_clamp, (pos.xy - 0.5) * float2(f_mask_du, f_mask_dv), 0).r;
      // The fog's coordinates of the pixel, from its own depth.
      FogPoint fog = FogCoords(pos);
      // Bit 9: a lit draw whose own pixel shader in the game has no fog (clay_logic.h, DrawFogOf): none here.
      if (light_mode & 512) fog.fogged = false;
      [branch] if (light_mode & 16) {
        // An instanced plant: the normal bent as the game's plant pixel shaders bend it, then A times
        // the vertex side's ambient plus the sun, or (light_mode bit 5) the object form with the vertex
        // side's colour, c27.w = 1 and c49 = 1. These shaders read no c26.
        float3 Np = PlantNormal(normal, bend);
        if (light_mode & 32) {
          lit = LightPlantLit(clay_linear(t.rgb), vcolour, f_add, f_floor, AmbientT(axis, Np), dot(Np, f_sun_dir), f_sun_col,
                              SunGateSingle(mask, 1.0));
        } else {
          lit = LightPlant(clay_linear(t.rgb), vcolour, dot(Np, f_sun_dir), f_sun_col, SunGateSingle(mask, 1.0));
        }
        lit = FogForm(lit, fog, 0.0, 0.0);
      } else if (light_mode & 64) {
        // A tree leaf: the vertex side's ambient and the sun's two scalars (bend.x front, bend.y back)
        // on A, the back light on leaf texels only; the mask through the shader's two literals (0.6
        // and 0.4: once on the front light, twice on the back light; both factors are 1 at a mask
        // of 1); c27.w, c49 and c26 as an object's.
        float2 m = LeafMask(mask, 0.6, 0.4);
        lit = LightLeaf(clay_linear(t.rgb), vcolour, bend.x, bend.y, f_sun_col, o_c27w, m.x, m.y, o_c49);
        lit = FogForm(lit, fog, o_leaf.z, o_leaf.w) * o_c26;
      } else {
        float3 N = light_normalize(normal);
        lit = LightObject(clay_linear(t.rgb), vcolour, f_tint, f_tint_pow, f_add, f_floor, AmbientT(axis, normal),
                          dot(N, f_sun_dir), f_sun_col, o_c27w, SunGateSingle(mask, 1.0), o_c49);
        lit = FogForm(lit, fog, o_leaf.z, o_leaf.w) * o_c26;
      }
      return float4(clay_lit(lit * f_exposure), (alpha_mode & 0x80) ? saturate(a) : 1.0);
    }
    if (light_mode & 1024) {
      float3 hazed = FogUnlitTree(clay_linear(t.rgb * (0.75 + 0.25 * facet)) * f_post, FogCoords(pos), asfloat(pad2));
      return float4(hazed, (alpha_mode & 0x80) ? saturate(a) : 1.0);
    }
    if (light_mode & 128) {
      float3 hazed = FogUnlit(clay_linear(t.rgb * (0.75 + 0.25 * facet)) * f_post, FogCoords(pos), (light_mode & 256) ? saturate(a) : 1.0);
      return float4(hazed, (alpha_mode & 0x80) ? saturate(a) : 1.0);
    }
    return float4(clay_linear(t.rgb * (0.75 + 0.25 * facet)) * f_post, (alpha_mode & 0x80) ? saturate(a) : 1.0);
#endif
  }
  base = float3((color >> 16) & 255, (color >> 8) & 255, color & 255) / 255.0;
  if (light_mode & 1024) return float4(FogUnlitTree(clay_linear(base * (0.35 + 0.65 * facet)) * f_post, FogCoords(pos), asfloat(pad2)), 1.0);
  if (light_mode & 128) return float4(FogUnlit(clay_linear(base * (0.35 + 0.65 * facet)) * f_post, FogCoords(pos), 1.0), 1.0);
  return float4(clay_linear(base * (0.35 + 0.65 * facet)) * f_post, 1.0);
})pin";
static const char kPinTerrainCommon[] = R"pin(float4 sample_colour(float2 at) {
  if (t_colour_sampler == 0) return t_colour.Sample(s_point_wrap, at);
  if (t_colour_sampler == 1) return t_colour.Sample(s_linear_wrap, at);
  if (t_colour_sampler == 2) return t_colour.Sample(s_point_clamp, at);
  return t_colour.Sample(s_linear_clamp, at);
}
// What the console's fetch gives for a component whose sign field is 3 (gamma): the stored value through
// its piecewise linear curve (PwlGammaToLinear in clay_logic.h; light_formulas.pwl_gamma_to_linear).
float3 base_linear(float3 g) {
  g = saturate(g);
  bool3 high = g >= 96.0 / 255.0;
  float3 scale = high ? ((g >= 192.0 / 255.0) ? 8.0 / 1024.0 : 4.0 / 1024.0) : ((g >= 64.0 / 255.0) ? 2.0 / 1024.0 : 1.0 / 1024.0);
  float3 offset = high ? ((g >= 192.0 / 255.0) ? -1024.0 : -256.0) : ((g >= 64.0 / 255.0) ? -64.0 : 0.0);
  float3 l = g * (261120.0 * scale) + offset;
  return (l + trunc(l * scale)) / 1023.0;
}
// The base map at `at` with the screen derivatives d = (ddx, ddy) of the coordinate (the fetch is
// inside the light's branch, the derivatives are formed outside it). Its r, g, b where the draw's
// fetch constant has the gamma sign (t_base_gamma, a bit a component): through base_linear, after the
// filter, as the game's fetch gives them.
float4 sample_base(float2 at, float4 d) {
  float4 t;
  if (t_base_sampler == 0) t = t_base.SampleGrad(s_point_wrap, at, d.xy, d.zw);
  else if (t_base_sampler == 1) t = t_base.SampleGrad(s_linear_wrap, at, d.xy, d.zw);
  else if (t_base_sampler == 2) t = t_base.SampleGrad(s_point_clamp, at, d.xy, d.zw);
  else t = t_base.SampleGrad(s_linear_clamp, at, d.xy, d.zw);
  float3 l = base_linear(t.rgb);
  return float4((t_base_gamma & 1) ? l.r : t.r, (t_base_gamma & 2) ? l.g : t.g, (t_base_gamma & 4) ? l.b : t.b, t.a);
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
#ifdef CLAY_DEPTH_ONLY
  return float4(0.0, 0.0, 0.0, 0.0);
#else
  if (light_mode & 4) return float4(clay_lit(colour * f_exposure), (alpha_mode & 0x80) ? saturate(a) : 1.0);
  float shade = 0.75 + 0.25 * facet;
  // (a terrain draw without light under bit 7: FogUnlit on what it writes; for a far draw that fades
  // in, after its fade and not before it as the lit path: such a draw has not occurred in a run)
  if (light_mode & 128) return float4(FogUnlit(saturate(colour * (shade * shade)) * f_post, FogCoords(pos), 1.0), (alpha_mode & 0x80) ? saturate(a) : 1.0);
  return float4(saturate(colour * (shade * shade)) * f_post, (alpha_mode & 0x80) ? saturate(a) : 1.0);
#endif
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
    // The sun's shadow at the pixel, on the sun term with the facing factor.
    float mask = shadow_mask.SampleLevel(s_linear_clamp, (pos.xy - 0.5) * float2(f_mask_du, f_mask_dv), 0).r;
    lin = LightTerrainLayer(lin, sample_base(base_at, base_d).rgb, f_tint, f_tint_pow, f_add, f_floor,
                            AmbientT(axis, normal), dot(N, f_sun_dir), f_sun_col, t_c11x, SunGateSingle(mask, facing));
    // The fog (the ground form), on the layer's own colour before its blend, as the game fogs each layer.
    lin = FogForm(lin, FogCoords(pos), 0.0, 0.0);
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
    // The fog (the ground form) before the cross-fade: 2F8A multiplies by its fade after its fog block.
    lin = FogForm(lin, FogCoords(pos), 0.0, 0.0);
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

// The composite's text with the previous frame's share (cbuffer Post's p_previous, plan Task 15) a literal 0
// where the program tests it: the program of every frame that has no previous-frame blend (the block keeps
// its field). Empty when the test's text is not found (the caller fails).
static std::string PreviousOff(std::string text) {
  const std::string test = "if (p_previous > 0.0) return float4(PostBlended(pos), 1.0);";
  const size_t at = text.find(test);
  if (at == std::string::npos || text.find(test, at + 1) != std::string::npos) return std::string();
  text.replace(at, test.size(), "if (0.0 > 0.0) return float4(PostBlended(pos), 1.0);");
  return text;
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
  if (!strstr(kClayTerrainLayerPs, "register(b1)") || !strstr(kClayTerrainLayerPs, "Texture2D t_colour : register(t5);") ||
      !strstr(kClayTerrainLayerPs, "Texture2D t_weights : register(t6);")) return 29;
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
    // shade would shade it twice) and without the clamp at 1, times the frame's exposure and through
    // clay_lit (the exposure inside the bound, and on nothing else: the one call of clay_lit in each
    // shader is this one). The alpha is not squared, not lit, and clamped only where it is written for
    // the blend.
    for (const char* src : {l, f}) {
      const char* test = strstr(src, "if ((alpha_mode & 0x8) && !alpha_passes(a, alpha_ref, alpha_mode & 7)) discard;");
      const char* mask = strstr(src, "if (!(a >= mask_base - offset * per_offset)) discard;");
      const char* lit = strstr(src, "if (light_mode & 4) return float4(clay_lit(colour * f_exposure), (alpha_mode & 0x80) ? saturate(a) : 1.0);");
      const char* shade = strstr(src, "float shade = 0.75 + 0.25 * facet;");
      const char* out = strstr(src, "return float4(saturate(colour * (shade * shade)) * f_post, (alpha_mode & 0x80) ? saturate(a) : 1.0);");
      if (!test || !mask || !lit || !shade || !out || !(test < mask && mask < lit && lit < shade && shade < out)) return 38;
      if (strstr(src, "a * a") || strstr(src, "colour * (0.75 + 0.25 * facet)") || Count(src, "clay_lit(colour") != 1 ||
          Count(src, "clay_lit(") != 4 || Count(src, "f_exposure") != 3) return 38;  // (clay_lit: its definition, the lit return, the two unlit fogs' bounds; the exposure: the block, the lit return, the unlit fog's in-scatter)
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
    // The normals have a buffer of their own (t9): the UV buffer holds UVs only.
    if (!strstr(v, "StructuredBuffer<float2> terrain_normals : register(t9);") || strstr(v, "float2 t = uvs[v];")) return 39;
    if (strstr(kClayPs, "TEXCOORD3") || !strstr(kClayTerrainFarPs, "float3 world : TEXCOORD2, float3 normal : TEXCOORD3) : SV_Target {")) return 40;
    const char* l = kClayTerrainLayerPs;
    if (!strstr(l, "float3 world : TEXCOORD2, float3 normal : TEXCOORD3") || !strstr(l, "Texture2D t_side : register(t7);")) return 41;
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
    // The far shader has no block: no side texture (its one derivative fetch is the light's base map, at t8).
    if (strstr(kClayTerrainFarPs, "t_side :") || strstr(kClayTerrainFarPs, "sample_side") || CallsSqrt(kClayTerrainFarPs) ||
        DisassemblyHas("clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0", "t7")) return 46;
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
    // (the flat shader without its unlit fog's line: it then reads none of the fields left out)
    std::string shorter = Changed(kClayPs, "  if (light_mode & 128) return float4(FogUnlit(clay_linear(base * shade) * f_post, FogCoords(pos), 1.0), 1.0);\n", "");
    if (shorter.empty()) return 50;
    // (and without its unlit tree's line, which reads light_mode and pad2)
    shorter = Changed(shorter.c_str(), "  if (light_mode & 1024) return float4(FogUnlitTree(clay_linear(base * shade) * f_post, FogCoords(pos), asfloat(pad2)), 1.0);\n", "");
    if (shorter.empty()) return 50;
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
    if (!strstr(kClayPs, "return float4(clay_linear(base * shade) * f_post, 1.0);") ||
        strstr(kClayPs, "return float4(base * shade, 1.0);")) return 61;
    const char* t = kClayTexturedPs;
    const char* test = strstr(t, "!alpha_passes(a, alpha_ref, alpha_mode & 7)");
    const char* mask = strstr(t, "if (!(a >= mask_base - offset * per_offset)) discard;");
    const char* out = strstr(t, "return float4(clay_linear(t.rgb * (0.75 + 0.25 * facet)) * f_post, (alpha_mode & 0x80) ? saturate(a) : 1.0);");
    const char* flat = strstr(t, "return float4(clay_linear(base * (0.35 + 0.65 * facet)) * f_post, 1.0);");
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
    // the three shaders that light; the flat shader has them in its unlit fog alone (final review, M2:
    // an unlit fogged value is bounded like a lit one), and its program without that fog's bits has neither.
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
    // control (the bound's three-component literal: in the flat shader only behind its unlit fog's bits;
    // the flat program without them has no 64 at all, neither the bound nor the in-scatter table's size)
    if ((!DisassemblyHas("clay_ps_mode128", WithLightMode(kClayPs, 128).c_str(), "ps_5_0", "l(64.000000, 64.000000, 64.000000") &&
         !DisassemblyHas("clay_ps_mode128", WithLightMode(kClayPs, 128).c_str(), "ps_5_0", "64.000000, 64.000000, 64.000000)")) ||
        DisassemblyHas("clay_ps_mode0", WithLightMode(kClayPs, 0).c_str(), "ps_5_0", "64.000000")) return 69;
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
        // the tree leaves: the two scalars of the light on the side the viewer sees (the normal turned to
        // him, the wrapped cosine guarded where 1 + wrap is zero, the back light through the card), the
        // two factors of the mask, and the family's function with the back light on leaf texels only
        "float2 LeafLight(float3 normal, float3 view, float3 light_dir, float wrap, float spot) {",
        "float3 n = (dot(view, normal) < 0.0) ? -normal : normal;",
        "float nl = dot(light_dir, n);",
        "float d = 1.0 + wrap;",
        "float front = (d != 0.0) ? saturate((nl + wrap) / d) : 0.0;",
        "float turn = 1.0 - saturate(dot(view, n));",
        "return float2(front, saturate(-nl) * (1.0 - turn * turn)) * spot;",
        "float2 LeafMask(float mask, float gain, float bias) {",
        "float m1 = saturate(mask) * gain + bias;",
        "return float2(m1, m1 * gain + bias);",
        "float3 LightLeaf(float3 A, float3 vs_ambient, float front, float back, float3 sun_col, float c27w, float m1, float m2,",
        "float green = (A.g >= A.r) ? 1.0 : 0.0;",
        "return (A * (vs_ambient * c27w + m1 * sun_col * front) + green * A * m2 * sun_col * back) * c49;",
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
      // No clamp at 1 is on a lit value, and the light has one division, LeafLight's by 1 + wrap, under
      // its test (held just above).
      if (strstr(src, "saturate(lit") || strstr(src, "clay_linear(lit")) return 80;
      {
        const char* light_at = strstr(src, "float3 light_normalize(float3 v) {");
        const char* light_end = strstr(src, "float3 clay_lit(float3 v)");
        if (!light_at || !light_end) return 80;
        int divisions = 0;
        for (const char* c = light_at; c < light_end; ++c) {
          if (c[0] == '/' && c[1] != '/' && c[-1] != '/') ++divisions;
        }
        if (divisions != 1) return 80;
      }
    }
    // The flat shader lights nothing: its main names no light function, no lit output and no field of
    // the frame's block; its two tests of light_mode are the unlit fog's bits (cases 210 to 212: bit 7,
    // and bit 10 for an unlit tree), and the program of a draw without them reads nothing of the frame
    // (case 166) but the unlit word, f_post, which each of its three colours is multiplied by (the post
    // sub-project's plan Task 5; case 230).
    {
      const char* flat_main = strstr(kClayPs, "float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0) : SV_Target {");
      if (!flat_main || Count(flat_main, "f_") != 3 || Count(flat_main, "clay_linear(base * shade) * f_post") != 3 || strstr(flat_main, "Light") || strstr(flat_main, "clay_lit") || strstr(flat_main, "light_mode & 4") ||
          Count(flat_main, "light_mode") != 2 || !strstr(flat_main, "if (light_mode & 128) return") ||
          !strstr(flat_main, "if (light_mode & 1024) return")) return 81;
    }
    // The vertex shader: the object's normal through the draw's rows (the game's (R n) * c7.w), the
    // vector of t from the normal as it is, not normalised, and the frame's axis, and the stream
    // colour's red, green, blue (bytes 0 to 2) raised to the draw's power; the terrain's vector of t
    // only for a lit draw. A light buffer is read only below light_count.
    {
      const char* v = kClayVs;
      const char* range = strstr(v, "bool has_light = in_range && uint(v) < light_count;");
      const char* terrain = strstr(v, "if (light_mode & 4) o.axis = AmbientAxis(o.normal, f_axis);");
      const char* object = strstr(v, "if ((light_mode & 82) == 2 && has_light) {");
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
      const char* weights = strstr(v, "float4 w = probe_or_bend[v];");
      const char* sum = strstr(v, "o.vcolour = o_probe[a0].xyz + float3(dot(o_probe[a0 + 1], w), dot(o_probe[a0 + 2], w), dot(o_probe[a0 + 3], w));");
      const char* otherwise = sum ? strstr(sum, "} else {") : nullptr;
      if (!probe || !a0 || !weights || !sum || !otherwise) return 82;
      if (!(word < probe && probe < a0 && a0 < weights && weights < sum && sum < otherwise && otherwise < colour)) return 82;
      if (strstr(v, "light_pow(o_probe") || strstr(v, "saturate(o_probe") || strstr(v, "max(o_probe") || strstr(v, "abs(o_probe")) return 82;
      if (!strstr(v, "StructuredBuffer<float4> probe_or_bend : register(t12);") || !strstr(v, "float4 o_probe[12];") ||
          !strstr(kClayTexturedPs, "float4 o_probe[12];")) return 82;
      // (the vertex shader normalises nothing of the light: the pixel shaders do, after the interpolation)
      if (strstr(v, "normalize(o.normal)") || strstr(v, "normalize(n)") || strstr(v, "light_normalize(o.")) return 82;
      if (!strstr(v, "StructuredBuffer<float3> normals : register(t10);") ||
          !strstr(v, "StructuredBuffer<uint> colours : register(t11);")) return 82;
      // The object's block is not an instanced plant's: the plant's normal must not go through the rows
      // (the test of the block excludes the instance bit, and nothing else of the shader has the rows
      // but the leaf's block, which comes after the plant's and under its own test).
      const char* after_rows = rot + strlen("o.normal = float3(dot(o_rot0.xyz, n), dot(o_rot1.xyz, n), dot(o_rot2.xyz, n));");
      const char* leaf_test = strstr(v, "[branch] if ((light_mode & 90) == 74 && has_light) {");
      for (const char* row : {"o_rot0.xyz", "o_rot1.xyz", "o_rot2.xyz"}) {
        const char* again = strstr(after_rows, row);
        if (!leaf_test || !again || again < leaf_test) return 82;
      }
      if (strstr(v, "if ((light_mode & 2) && has_light)") || strstr(v, "if ((light_mode & 18) == 2 && has_light)") ||
          strstr(v, "if ((light_mode & 18) == 18 && has_light)")) return 82;
      // The instanced plant (plan Task 8b), after the object's block and under its own test: the
      // normal as it is, the colour of the copy and the bend inputs from the plant's own buffers, the
      // bend from the up axis, the height and the three bend constants (c45.x the strength, .y the
      // scale, .z the offset), the colour's exponent 2.2 * c21.w; then by family the vector of t with
      // the colour times the tint, or no vector and the colour times the tint plus the added light.
      const char* plant = strstr(v, "[branch] if ((light_mode & 82) == 18 && has_light) {");
      const char* pn = plant ? strstr(plant, "float3 n = normals[v];") : nullptr;
      const char* as_is = strstr(v, "o.normal = n;");
      const char* tone = strstr(v, "uint tone = colours[v];");
      const char* inputs = strstr(v, "float4 b = probe_or_bend[v];");
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
      // (since plan Task 8c three of each: the leaf's block has its own loads and its own ambient)
      if (count(main_at, "PlantBend(") != 1 || count(main_at, "PlantHeight(") != 1 || count(main_at, "InstanceAmbient(") != 3 ||
          count(main_at, "= normals[v]") != 3 || count(main_at, "colours[v]") != 3 || count(main_at, "probe_or_bend[v]") != 3 ||
          count(main_at, "terrain_normals[v]") != 1 || as_is < plant || tone < plant) return 99;
      // The tree leaf (plan Task 8c), after the plant's block and under its own test (the object, probe
      // and leaf bits without the instance bit): the normal through the rows AS IT IS (the game's leaf
      // vertex shaders do not normalise it), no vector of t, the probe colour of the clamped set and the
      // weights, the ambient with the exponent's factor and the frame's tint, exponent and added light,
      // the world position (M p) / c7.w + the translation, and the two scalars of the sun from the unit
      // vector to the eye, the unit sun direction and the wrap, with no cone or range (spot 1), in the
      // first two components of the bend.
      const char* leaf = strstr(v, "[branch] if ((light_mode & 90) == 74 && has_light) {");
      const char* ln = leaf ? strstr(leaf, "float3 n = normals[v];") : nullptr;
      const char* big_n = strstr(v, "float3 N = float3(dot(o_rot0.xyz, n), dot(o_rot1.xyz, n), dot(o_rot2.xyz, n));");
      const char* keep = strstr(v, "o.normal = N;");
      const char* no_t = keep ? strstr(keep, "o.axis = float3(0, 0, 0);") : nullptr;
      const char* set = strstr(v, "uint a0 = min(colours[v], 8u);");
      const char* lw = set ? strstr(set, "float4 w = probe_or_bend[v];") : nullptr;
      const char* lprobe = strstr(v, "float3 probe = o_probe[a0].xyz + float3(dot(o_probe[a0 + 1], w), dot(o_probe[a0 + 2], w), dot(o_probe[a0 + 3], w));");
      const char* lamb = strstr(v, "o.vcolour = InstanceAmbient(probe, f_tint, o_colour_power * f_tint_pow, f_add);");
      const char* where = strstr(v, "float3 at = float3(dot(o_rot0.xyz, p.xyz), dot(o_rot1.xyz, p.xyz), dot(o_rot2.xyz, p.xyz)) * o_leaf.y +");
      const char* shift = strstr(v, "float3(o_rot0.w, o_rot1.w, o_rot2.w);");
      const char* sides = strstr(v, "o.bend = float4(LeafLight(N, light_normalize(f_eye - at), light_normalize(f_sun_dir), o_leaf.x, 1.0), 0, 0);");
      if (!leaf || !ln || !big_n || !keep || !no_t || !set || !lw || !lprobe || !lamb || !where || !shift || !sides) return 166;
      if (!(amb < leaf && leaf < ln && ln < big_n && big_n < keep && keep < no_t && no_t < set && set < lw && lw < lprobe &&
            lprobe < lamb && lamb < where && where < shift && shift < sides && sides < strstr(leaf, "return o;"))) return 166;
      // (one call of LeafLight in main, and the leaf's normal is never normalised: N is used as made)
      if (count(main_at, "LeafLight(") != 1 || strstr(v, "light_normalize(N)") || strstr(v, "normalize(N)") ||
          count(main_at, "o_leaf.") != 2 || count(main_at, "f_eye") != 1) return 166;
      if (!strstr(v, "float4 o_leaf;") || !strstr(kClayTexturedPs, "float4 o_leaf;") || !strstr(v, "float3 f_eye; uint f_eye_on;")) return 166;
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
      // (c26 comes after the fog, which comes after the lit colour with its c49: the game's order)
      const char* rest = strstr(t, "dot(N, f_sun_dir), f_sun_col, o_c27w, SunGateSingle(mask, 1.0), o_c49);\n"
                                   "        lit = FogForm(lit, fog, o_leaf.z, o_leaf.w) * o_c26;");
      const char* out = strstr(t, "return float4(clay_lit(lit * f_exposure), (alpha_mode & 0x80) ? saturate(a) : 1.0);");
      const char* unlit = strstr(t, "return float4(clay_linear(t.rgb * (0.75 + 0.25 * facet)) * f_post, (alpha_mode & 0x80) ? saturate(a) : 1.0);");
      if (!mask || !branch || !n || !call || !rest || !out || !unlit) return 83;
      if (!(mask < branch && branch < n && n < call && call < rest && rest < out && out < unlit)) return 83;
      if (strstr(t, "clay_linear(t.rgb * lit") || strstr(t, "facet) * lit") || Count(t, "clay_lit(") != 4 ||
          Count(t, "f_exposure") != 3) return 83;  // (the block, the lit return, the unlit fog's in-scatter)
      // The instanced plants (plan Task 8b), before the object's path and under the instance bit: the
      // normal bent per pixel, then by family the object form of the vertex side's colour (the added
      // light, the floor, t from the vertex side's vector and the BENT normal) or A times the vertex
      // side's ambient plus the sun; the sun of both by the bent normal; the mask 1; no c26, c27 or c49.
      const char* declared = strstr(t, "float3 lit;");
      const char* plant = strstr(t, "[branch] if (light_mode & 16) {");
      const char* np = strstr(t, "float3 Np = PlantNormal(normal, bend);");
      const char* family = strstr(t, "if (light_mode & 32) {");
      const char* lit_call = strstr(t, "lit = LightPlantLit(clay_linear(t.rgb), vcolour, f_add, f_floor, AmbientT(axis, Np), dot(Np, f_sun_dir), f_sun_col,");
      const char* plant_call = strstr(t, "lit = LightPlant(clay_linear(t.rgb), vcolour, dot(Np, f_sun_dir), f_sun_col, SunGateSingle(mask, 1.0));");
      if (!declared || !plant || !np || !family || !lit_call || !plant_call) return 99;
      if (!(branch < declared && declared < plant && plant < np && np < family && family < lit_call && lit_call < plant_call &&
            plant_call < n && n < call)) return 99;
      // The tree leaf (plan Task 8c), between the plants' paths and the object's, under the leaf bit:
      // the mask's two factors of a mask of 1 with the shader's literals, and the family's function on
      // the vertex side's ambient and two scalars with the frame's sun colour, c27.w, c49, then c26.
      const char* leaf = strstr(t, "} else if (light_mode & 64) {");
      const char* mask_factors = strstr(t, "float2 m = LeafMask(mask, 0.6, 0.4);");
      const char* leaf_call = strstr(t, "lit = LightLeaf(clay_linear(t.rgb), vcolour, bend.x, bend.y, f_sun_col, o_c27w, m.x, m.y, o_c49);\n"
                                        "        lit = FogForm(lit, fog, o_leaf.z, o_leaf.w) * o_c26;");
      if (!leaf || !mask_factors || !leaf_call) return 167;
      if (!(plant_call < leaf && leaf < mask_factors && mask_factors < leaf_call && leaf_call < n)) return 167;
      // (the object's constants are read by the leaf's path and the object's alone: once each in each,
      // after the plants' paths)
      for (const char* field : {"o_c27w", "o_c49", "o_c26"}) {
        const char* at = strstr(strstr(t, "float4 main("), field);
        const char* second = at ? strstr(at + 1, field) : nullptr;
        if (!at || at < leaf || at > n || !second || second < n || strstr(second + 1, field)) return 99;
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
      const char* rest = strstr(l, "AmbientT(axis, normal), dot(N, f_sun_dir), f_sun_col, t_c11x, SunGateSingle(mask, facing));");
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
    // The base map is the fourth terrain texture (t8), read with its own sampler and the derivatives
    // formed outside the branch, in both terrain shaders.
    for (const char* src : {kClayTerrainLayerPs, kClayTerrainFarPs}) {
      if (!strstr(src, "Texture2D t_base : register(t8);") || !strstr(src, "if (t_base_sampler == 0)") ||
          !strstr(src, "t_base.SampleGrad(s_point_wrap, at, d.xy, d.zw)") ||
          !strstr(src, "t_base.SampleGrad(s_linear_wrap, at, d.xy, d.zw)") ||
          !strstr(src, "t_base.SampleGrad(s_point_clamp, at, d.xy, d.zw)") ||
          !strstr(src, "t_base.SampleGrad(s_linear_clamp, at, d.xy, d.zw)")) return 86;
    }
    if (BindPoint("clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0", "t_base") != 8 ||
        BindPoint("clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0", "t_base") != 8 ||
        BindPoint("clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0", "t_side") != 7 ||
        BindPoint("clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0", "t_colour") != 5 ||
        BindPoint("clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0", "t_weights") != 6 ||
        BindPoint("clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0", "t_colour") != 5 ||
        BindPoint("clay_textured_ps", kClayTexturedPs, "ps_5_0", "albedo") != 3 ||
        BindPoint("clay_vs", kClayVs, "vs_5_0", "terrain_normals") != 9 ||
        BindPoint("clay_vs", kClayVs, "vs_5_0", "normals") != 10 ||
        BindPoint("clay_vs", kClayVs, "vs_5_0", "colours") != 11 ||
        BindPoint("clay_vs", kClayVs, "vs_5_0", "probe_or_bend") != 12 ||
        BindPoint("clay_vs", kClayVs, "vs_5_0", "uvs") != 2) return 87;
    // The blocks' registers as the pass binds them: Frame at b2 in the four shaders that light, the
    // draw's slot at b1 (Object: the vertex shader and the textured pixel shader; Terrain: the two
    // terrain shaders); the flat shader binds neither.
    for (int i = 0; i < 4; ++i) {
      const char* target = i == 0 ? "vs_5_0" : "ps_5_0";
      if (BindPoint("lit", lit_shaders[i], target, "Frame") != 2) return 88;
      if (BindPoint("lit", lit_shaders[i], target, i < 2 ? "Object" : "Terrain") != 1) return 88;
    }
    // (the flat shader: the frame's block where the others have it, since the unlit draws' fog; no object block)
    if (BindPoint("clay_ps", kClayPs, "ps_5_0", "Frame") != 2 || BindPoint("clay_ps", kClayPs, "ps_5_0", "Object") != -1) return 88;
    // The frame's block and the object's block lie where the tables beside the HLSL say, in every
    // compiled shader that declares them, and the tables are the C++ structs' layouts
    // (ClayFrameConstants, ClayObjectConstants). Controls: sources with two fields exchanged are reported.
    {
      using fable2::native::render::ClayFrameConstants;
      using fable2::native::render::ClayObjectConstants;
      const unsigned frame_count = sizeof(kClayFrameFields) / sizeof(kClayFrameFields[0]);
      const unsigned object_count = sizeof(kClayObjectFields) / sizeof(kClayObjectFields[0]);
      using fable2::native::render::ClayFogConstants;
      static_assert(fable2::native::render::kClayFrameLightBytes == kClayFrameLightSize && sizeof(ClayFrameConstants) == kClayFrameSize &&
                    kClayFrameLightSize == 112 && kClayFrameSize == 5344 && sizeof(ClayObjectConstants) == 288);
      // The fog's tail of the frame's block (kClayFrameFogFields) against the C++ struct, and the whole
      // block as the lit pixel shaders declare it: the light's fields, then the tail's.
      const unsigned fog_count = sizeof(kClayFrameFogFields) / sizeof(kClayFrameFogFields[0]);
      {
        const size_t fog_at = offsetof(ClayFrameConstants, fog);
        const struct {
          const char* name;
          size_t offset, bytes;
        } fog_struct[] = {
            {"f_fog_axis", fog_at + offsetof(ClayFogConstants, axis), sizeof(ClayFogConstants::axis)},
            {"f_fog_on", fog_at + offsetof(ClayFogConstants, on), sizeof(ClayFogConstants::on)},
            {"f_fog_near", fog_at + offsetof(ClayFogConstants, near_colour), sizeof(ClayFogConstants::near_colour)},
            {"f_fog_start", fog_at + offsetof(ClayFogConstants, start), sizeof(ClayFogConstants::start)},
            {"f_fog_slope", fog_at + offsetof(ClayFogConstants, slope), sizeof(ClayFogConstants::slope)},
            {"f_fog_scale", fog_at + offsetof(ClayFogConstants, scale), sizeof(ClayFogConstants::scale)},
            {"f_fog_amount", fog_at + offsetof(ClayFogConstants, amount), sizeof(ClayFogConstants::amount)},
            {"f_fog_scatter", fog_at + offsetof(ClayFogConstants, scatter), sizeof(ClayFogConstants::scatter)},
            {"f_inv0", offsetof(ClayFrameConstants, inv_view_proj), 16},
            {"f_inv1", offsetof(ClayFrameConstants, inv_view_proj) + 16, 16},
            {"f_inv2", offsetof(ClayFrameConstants, inv_view_proj) + 32, 16},
            {"f_inv3", offsetof(ClayFrameConstants, inv_view_proj) + 48, 16},
            {"f_scatter", offsetof(ClayFrameConstants, scatter), sizeof(ClayFrameConstants::scatter)},
            {"f_transmit", offsetof(ClayFrameConstants, transmit), sizeof(ClayFrameConstants::transmit)},
        };
        if (fog_count != sizeof(fog_struct) / sizeof(fog_struct[0]) || fog_count != 14) return 89;
        for (unsigned i = 0; i < fog_count; ++i) {
          if (strcmp(kClayFrameFogFields[i].name, fog_struct[i].name) || kClayFrameFogFields[i].offset != fog_struct[i].offset ||
              kClayFrameFogFields[i].bytes != fog_struct[i].bytes) return 89;
        }
        // (the tables are arrays of float4 a register apart: 64 and 256 registers; the sky step will read
        // f_scatter at the same offset)
        if (kClayFrameFogFields[12].offset != 224 || kClayFrameFogFields[12].bytes != 64 * 16 || kClayFrameFogFields[13].offset != 1248 ||
            kClayFrameFogFields[13].bytes != 256 * 16 || kClayFrameFogFields[13].offset + kClayFrameFogFields[13].bytes != kClayFrameSize) return 89;
      }
      std::vector<fable2::native::shaders::ClayBufferField> frame_with_fog(kClayFrameFields, kClayFrameFields + 14);
      frame_with_fog.insert(frame_with_fog.end(), kClayFrameFogFields, kClayFrameFogFields + fog_count);
      const struct {
        const char* name;
        size_t offset, bytes;
      } frame_struct[] = {
          {"f_sun_dir", offsetof(ClayFrameConstants, sun_dir), sizeof(ClayFrameConstants::sun_dir)},
          {"f_light_on", offsetof(ClayFrameConstants, light_on), sizeof(ClayFrameConstants::light_on)},
          {"f_sun_col", offsetof(ClayFrameConstants, sun_col), sizeof(ClayFrameConstants::sun_col)},
          {"f_tint_pow", offsetof(ClayFrameConstants, tint_pow), sizeof(ClayFrameConstants::tint_pow)},
          {"f_tint", offsetof(ClayFrameConstants, tint), sizeof(ClayFrameConstants::tint)},
          {"f_exposure", offsetof(ClayFrameConstants, exposure), sizeof(ClayFrameConstants::exposure)},
          {"f_add", offsetof(ClayFrameConstants, add), sizeof(ClayFrameConstants::add)},
          {"f_post", offsetof(ClayFrameConstants, post), sizeof(ClayFrameConstants::post)},
          {"f_floor", offsetof(ClayFrameConstants, floor), sizeof(ClayFrameConstants::floor)},
          {"f_mask_du", offsetof(ClayFrameConstants, mask_du), sizeof(ClayFrameConstants::mask_du)},
          {"f_axis", offsetof(ClayFrameConstants, axis), sizeof(ClayFrameConstants::axis)},
          {"f_mask_dv", offsetof(ClayFrameConstants, mask_dv), sizeof(ClayFrameConstants::mask_dv)},
          {"f_eye", offsetof(ClayFrameConstants, eye), sizeof(ClayFrameConstants::eye)},
          {"f_eye_on", offsetof(ClayFrameConstants, eye_on), sizeof(ClayFrameConstants::eye_on)},
      }, object_struct[] = {
          {"o_rot0", offsetof(ClayObjectConstants, rot), 16},
          {"o_rot1", offsetof(ClayObjectConstants, rot) + 16, 16},
          {"o_rot2", offsetof(ClayObjectConstants, rot) + 32, 16},
          {"o_c49", offsetof(ClayObjectConstants, c49), sizeof(ClayObjectConstants::c49)},
          {"o_c27w", offsetof(ClayObjectConstants, c27w), sizeof(ClayObjectConstants::c27w)},
          {"o_c26", offsetof(ClayObjectConstants, c26), sizeof(ClayObjectConstants::c26)},
          {"o_colour_power", offsetof(ClayObjectConstants, colour_power), sizeof(ClayObjectConstants::colour_power)},
          {"o_probe", offsetof(ClayObjectConstants, probe), sizeof(ClayObjectConstants::probe)},
          {"o_leaf", offsetof(ClayObjectConstants, leaf), sizeof(ClayObjectConstants::leaf)},
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
      // The exposure (the final fix wave): in the frame's block at byte 44 (register 2, w), read by the
      // one lit return of each of the three pixel shaders that light and by nothing else: the vertex
      // shader declares the block and never names the field, the flat shader and the output mapping
      // have no such name (the mapping's own `exposure` is its cbuffer Map's first word, which the
      // pass sets to 1). So no unlit return and no clear can take it.
      if (strcmp(kClayFrameFields[5].name, "f_exposure") || kClayFrameFields[5].offset != 44 || kClayFrameFields[5].bytes != 4) return 89;
      // (since the unlit draws' fog the shared light text names it once more, in FogUnlit: the in-scatter an
      // unlit pixel gains is under the lit draws' exposure; the flat shader has that text and no other mention)
      if (Count(kClayVs, "f_exposure") != 2 || Count(kClayTexturedPs, "f_exposure") != 3 || Count(kClayTerrainLayerPs, "f_exposure") != 3 ||
          Count(kClayTerrainFarPs, "f_exposure") != 3 || Count(kClayPs, "f_exposure") != 2 || strstr(kOutputMapPs, "f_exposure") ||
          strstr(kCompositePs, "exposure")) return 89;
      for (const char* src : lit_shaders) {
        if (strstr(src, "float exposure") || strstr(src, "f_pad3")) return 89;
      }
      for (int i = 0; i < 4; ++i) {
        // The vertex shader declares the light's part; the three lit pixel shaders the whole block.
        const char* target = i == 0 ? "vs_5_0" : "ps_5_0";
        const int at = i == 0 ? BufferLayoutMismatch("lit", lit_shaders[i], target, "Frame", kClayFrameFields, frame_count, kClayFrameLightSize)
                              : BufferLayoutMismatch("lit", lit_shaders[i], target, "Frame", frame_with_fog.data(),
                                                     unsigned(frame_with_fog.size()), kClayFrameSize);
        if (at) {
          std::cerr << "cbuffer Frame of lit shader " << i << ": " << at << "\n";
          return 90 + i;
        }
      }
      // The depth-only programs of the three do not define CLAY_FOG: they have the light's part alone, or
      // (a program that reads nothing of the block keeps none of it: 3) no such buffer, as before the fog;
      // never the block with the tail.
      for (const char* src : {kClayTexturedDepthPs, kClayTerrainLayerDepthPs, kClayTerrainFarDepthPs}) {
        const int light_only = BufferLayoutMismatch("depth_only", src, "ps_5_0", "Frame", kClayFrameFields, frame_count, kClayFrameLightSize);
        if (light_only != 0 && light_only != 3) return 93;
        if (BufferLayoutMismatch("depth_only", src, "ps_5_0", "Frame", frame_with_fog.data(), unsigned(frame_with_fog.size()),
                                 kClayFrameSize) == 0) return 93;
        if (strstr(src, "#define CLAY_DEPTH_ONLY 1\n#ifndef CLAY_DEPTH_ONLY\n#define CLAY_FOG 1\n#endif\n") != src) return 93;
      }
      // Controls: two fields of the tail exchanged, and a table one texel short, are reported.
      {
        std::string swapped_fog = kClayTexturedPs;
        const std::string good_fog = "float f_fog_slope; float f_fog_scale;";
        size_t fog_at = swapped_fog.find(good_fog);
        if (fog_at == std::string::npos) return 96;
        swapped_fog.replace(fog_at, good_fog.size(), "float f_fog_scale; float f_fog_slope;");
        if (BufferLayoutMismatch("swapped_fog_ps", swapped_fog.c_str(), "ps_5_0", "Frame", frame_with_fog.data(),
                                 unsigned(frame_with_fog.size()), kClayFrameSize) < 10) return 96;
        std::string short_table = kClayTexturedPs;
        const std::string good_table = "float4 f_scatter[64];";
        fog_at = short_table.find(good_table);
        if (fog_at == std::string::npos) return 96;
        short_table.replace(fog_at, good_table.size(), "float4 f_scatter[63];");
        if (BufferLayoutMismatch("short_table_ps", short_table.c_str(), "ps_5_0", "Frame", frame_with_fog.data(),
                                 unsigned(frame_with_fog.size()), kClayFrameSize) == 0) return 96;
      }
      if (int at = BufferLayoutMismatch("clay_vs", kClayVs, "vs_5_0", "Object", kClayObjectFields, object_count, 288)) return 94;
      if (int at = BufferLayoutMismatch("clay_textured_ps", kClayTexturedPs, "ps_5_0", "Object", kClayObjectFields, object_count, 288)) return 95;
      std::string swapped = kClayTexturedPs;
      const std::string good = "float3 f_tint; float f_exposure;";
      size_t at = swapped.find(good);
      if (at == std::string::npos) return 96;
      swapped.replace(at, good.size(), "float f_exposure; float3 f_tint;");
      if (BufferLayoutMismatch("swapped_frame_ps", swapped.c_str(), "ps_5_0", "Frame", frame_with_fog.data(),
                               unsigned(frame_with_fog.size()), kClayFrameSize) < 10) return 96;
      swapped = kClayTexturedPs;
      const std::string object_good = "float3 o_c49; float o_c27w;";
      at = swapped.find(object_good);
      if (at == std::string::npos) return 97;
      swapped.replace(at, object_good.size(), "float o_c27w; float3 o_c49;");
      if (BufferLayoutMismatch("swapped_object_ps", swapped.c_str(), "ps_5_0", "Object", kClayObjectFields, object_count, 288) < 10) return 97;
      // (the probe registers are 192 bytes from byte 80, twelve float4: an array a register apart; the
      // leaf's register is the block's last, behind them; the frame's eye is its block's last register)
      if (object_count != 9 || strcmp(kClayObjectFields[7].name, "o_probe") || kClayObjectFields[7].offset != 80 ||
          kClayObjectFields[7].bytes != 192 || strcmp(kClayObjectFields[8].name, "o_leaf") || kClayObjectFields[8].offset != 272 ||
          kClayObjectFields[8].bytes != 16) return 97;
      if (frame_count != 14 || strcmp(kClayFrameFields[12].name, "f_eye") || kClayFrameFields[12].offset != 96 ||
          kClayFrameFields[12].bytes != 12 || strcmp(kClayFrameFields[13].name, "f_eye_on") || kClayFrameFields[13].offset != 108) return 97;
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
    // (the pre-pass's depth-only forms and the mask shader are held the same way in cases 192 to 196)
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
      const std::string good = "StructuredBuffer<uint> colours : register(t11);";
      size_t at = moved.find(good);
      if (at == std::string::npos) return 154;
      moved.replace(at, good.size(), "StructuredBuffer<uint> colours : register(t13);");
      if (BindingsMismatch("moved_vs", moved.c_str(), "vs_5_0", true) < 10) return 154;
      moved = kClayVs;
      moved.replace(at, good.size(), "StructuredBuffer<uint> colours : register(t3);");
      if (BindingsMismatch("pixel_register_vs", moved.c_str(), "vs_5_0", true) < 10) return 154;
      // The weights' register likewise: at a register behind the layout's last, and at a texture's.
      moved = kClayVs;
      const std::string weights = "StructuredBuffer<float4> probe_or_bend : register(t12);";
      at = moved.find(weights);
      if (at == std::string::npos) return 154;
      moved.replace(at, weights.size(), "StructuredBuffer<float4> probe_or_bend : register(t13);");
      if (BindingsMismatch("moved_weights_vs", moved.c_str(), "vs_5_0", true) < 10) return 154;
      moved = kClayVs;
      moved.replace(at, weights.size(), "StructuredBuffer<float4> probe_or_bend : register(t7);");
      if (BindingsMismatch("pixel_register_weights_vs", moved.c_str(), "vs_5_0", true) < 10) return 154;
    }
    // The registers the vertex shader may load from: the layout's seven buffer parameters, and no other.
    unsigned buffer_registers = 0;
    for (const fable2::native::render::ClayBinding& b : fable2::native::render::kClayBindings) {
      if (b.kind == fable2::native::render::ClayBindingKind::kBuffer) buffer_registers |= 1u << b.reg;
    }
    if (buffer_registers != ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 12))) return 156;
    const std::string vs = Disassembly("clay_vs", kClayVs, "vs_5_0");
    if (BufferLoadGuard(vs, 9) != 1 || BufferLoadGuard(vs, 10) != 1 || BufferLoadGuard(vs, 11) != 1 || BufferLoadGuard(vs, 12) != 1) {
      std::cerr << "the vertex shader loads a light buffer outside a branch: t9 " << BufferLoadGuard(vs, 9) << ", t10 "
                << BufferLoadGuard(vs, 10) << ", t11 " << BufferLoadGuard(vs, 11) << ", t12 " << BufferLoadGuard(vs, 12) << "\n";
      return 155;
    }
    if (BufferLoadGuard(vs, 0) != 0 || BufferLoadGuard(vs, 1) != 0 || BufferLoadGuard(vs, 2) != 0 ||
        BufferLoadGuard(vs, 13) != -1 || BufferLoadGuard(vs, 8) != -1 || !LoadsOnlyFrom(vs, buffer_registers)) return 156;
    // (LoadsOnlyFrom sees a load from a register it is not given)
    if (LoadsOnlyFrom(vs, buffer_registers & ~(1u << 12)) || LoadsOnlyFrom(vs, buffer_registers & ~(1u << 0))) return 156;
    if (!strstr(kClayVs, "[branch] if ((light_mode & 1) && has_light) {") ||
        !strstr(kClayVs, "[branch] if ((light_mode & 82) == 2 && has_light) {") ||
        !strstr(kClayVs, "[branch] if (light_mode & 8) {") ||
        !strstr(kClayVs, "[branch] if ((light_mode & 82) == 18 && has_light) {") ||
        !strstr(kClayVs, "[branch] if ((light_mode & 90) == 74 && has_light) {") ||
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
      const std::string early_normal = Changed(kClayVs, "  [branch] if ((light_mode & 82) == 2 && has_light) {",
                                               "  o.axis = normals[v];\n  [branch] if ((light_mode & 82) == 2 && has_light) {");
      const std::string early_weights = Changed(kClayVs, "  o.vcolour = float3(0, 0, 0);", "  o.vcolour = probe_or_bend[v].xyz;");
      const std::string outer_weights = Changed(kClayVs, "    [branch] if (light_mode & 8) {",
                                                "    o.axis += probe_or_bend[v].xyz;\n    [branch] if (light_mode & 8) {");
      const std::string normal_listing = Disassembly("early_normal_vs", early_normal.c_str(), "vs_5_0");
      const std::string weights_listing = Disassembly("early_weights_vs", early_weights.c_str(), "vs_5_0");
      const std::string outer_listing = Disassembly("outer_weights_vs", outer_weights.c_str(), "vs_5_0");
      if (early_normal.empty() || early_weights.empty() || outer_weights.empty() || normal_listing.empty() ||
          weights_listing.empty() || outer_listing.empty()) return 158;
      if (BufferLoadGuard(normal_listing, 10) != 0 || InnerLoadDepth(normal_listing, 10) != 0) return 158;
      // The default's load the compiler moves into the `else` of the object's test: inside a block, so
      // the two block counts are blind to it (it runs for every vertex that is NOT an object's). The
      // program of a draw without light shows it: with the mode fixed at 0 the weights are loaded. That
      // is the check every light_mode gets below, and why it is made on the fixed-mode programs.
      if (BufferLoadGuard(weights_listing, 12) != 1 ||
          !ReadsResource(Disassembly("early_weights_vs_mode0", WithLightMode(early_weights.c_str(), 0).c_str(), "vs_5_0"), 12) ||
          !ReadsResource(Disassembly("early_weights_vs_mode1", WithLightMode(early_weights.c_str(), 1).c_str(), "vs_5_0"), 12)) return 158;
      // (inside a test, so BufferLoadGuard alone lets it through: the depths are what tell)
      const std::string outer_depths = LoadDepths(outer_listing, 12);
      if (BufferLoadGuard(outer_listing, 12) != 1 || outer_depths.empty() || outer_depths[0] != '1' ||
          outer_depths == LoadDepths(vs, 12)) return 158;
      // The plant's bend inputs read before the plant's test (every draw would load them), and its
      // colour read in the object's block (every object would load a second word): both reported.
      const std::string early_bend = Changed(kClayVs, "  [branch] if ((light_mode & 82) == 18 && has_light) {",
                                             "  o.bend = probe_or_bend[v];\n  [branch] if ((light_mode & 82) == 18 && has_light) {");
      const std::string bend_listing = Disassembly("early_bend_vs", early_bend.c_str(), "vs_5_0");
      if (early_bend.empty() || bend_listing.empty() || BufferLoadGuard(bend_listing, 12) != 0) return 158;
      // The object's block, the plant's and the leaf's exclude each other in the compiled program: the
      // mode's object, instance and leaf bits (82) are compared with 2 for the first and with 18 for
      // the second, and those with the probe bit (90) with 74 for the third: one `and` of the mode
      // with (1, 82, 90) and one `ieq` with (2, 18, 74).
      // Controls, each a test that lets two blocks run for one mode, and each without that comparison:
      // an object's test without the other two bits (a plant and a leaf would run the object's block
      // too, and load their normals and their word twice); an object's test without the leaf bit (the
      // parent's: a leaf would run it too); a plant's test without the leaf bit; a leaf's test without
      // the instance bit (a mode with both would run the plant's block and the leaf's); a leaf's test
      // without the probe bit (the block would load weights where the pass binds none). (With the mode
      // fixed the first programs are the same: a later block overwrites all an earlier one gives.)
      const char* const object_test = "  [branch] if ((light_mode & 82) == 2 && has_light) {";
      const char* const plant_test = "  [branch] if ((light_mode & 82) == 18 && has_light) {";
      const char* const leaf_test = "  [branch] if ((light_mode & 90) == 74 && has_light) {";
      if (!HasLineWith(vs, "ieq", "l(2, 18, 74, 0)") || !HasLineWith(vs, "and", "l(1, 82, 90, 0)")) return 158;
      const struct {
        const char* name;
        const char* before;
        const char* after;
      } pairings[] = {
          {"both_vs", object_test, "  [branch] if ((light_mode & 2) && has_light) {"},
          {"object_and_leaf_vs", object_test, "  [branch] if ((light_mode & 18) == 2 && has_light) {"},
          {"plant_and_leaf_vs", plant_test, "  [branch] if ((light_mode & 18) == 18 && has_light) {"},
          {"leaf_and_plant_vs", leaf_test, "  [branch] if ((light_mode & 74) == 74 && has_light) {"},
          {"leaf_without_probe_vs", leaf_test, "  [branch] if ((light_mode & 82) == 66 && has_light) {"},
      };
      for (const auto& pairing : pairings) {
        const std::string changed = Changed(kClayVs, pairing.before, pairing.after);
        const std::string listing = Disassembly(pairing.name, changed.c_str(), "vs_5_0");
        if (changed.empty() || listing.empty() ||
            (HasLineWith(listing, "ieq", "l(2, 18, 74, 0)") && HasLineWith(listing, "and", "l(1, 82, 90, 0)"))) {
          std::cerr << "the control '" << pairing.name << "' is not reported\n";
          return 158;
        }
      }
      // What the first two pairings do, on the program of a leaf's mode (78): the object's block's
      // stream-colour or probe path runs before the leaf's. The leaf's block overwrites what it gave,
      // so the compiler drops it, and the programs are the leaf's own: the comparison above, on the
      // whole program, is what shows them. The last pairing shows on a fixed mode: an object-and-leaf
      // mode without the probe bit (70), for which the pass binds no weights, loads them.
      {
        const std::string loose_leaf = Changed(kClayVs, leaf_test, "  [branch] if ((light_mode & 82) == 66 && has_light) {");
        if (!ReadsResource(Disassembly("leaf_without_probe_vs_mode70", WithLightMode(loose_leaf.c_str(), 70).c_str(), "vs_5_0"), 12) ||
            ReadsResource(Disassembly("clay_vs_mode70", WithLightMode(kClayVs, 70).c_str(), "vs_5_0"), 12)) return 158;
      }
      // The leaf's weights read before the leaf's test (every draw would load them): reported. (Into an
      // output no block writes: one that the blocks overwrite the compiler moves into their `else`.)
      const std::string early_leaf = Changed(kClayVs, leaf_test, (std::string("  o.world += probe_or_bend[v].xyz;\n") + leaf_test).c_str());
      const std::string early_leaf_listing = Disassembly("early_leaf_vs", early_leaf.c_str(), "vs_5_0");
      if (early_leaf.empty() || early_leaf_listing.empty() || BufferLoadGuard(early_leaf_listing, 12) != 0) return 158;
    }
    // Every light load, by the blocks around it. The terrain normals: one load, in the terrain's block.
    // The normals and the word at t10: three loads each, one in the object's block, one in the
    // instanced plant's and one in the leaf's. t11: the weights inside BOTH of the object's tests (its
    // block's, which has the vertex bound, and the probe bit's), the plant's bend inputs in the plant's
    // block, and the leaf's weights in the leaf's block (whose one test has the probe bit).
    if (LoadDepths(vs, 9) != "1" || LoadDepths(vs, 10) != "1,1,1" || LoadDepths(vs, 11) != "1,1,1" || LoadDepths(vs, 12) != "2,1,1" ||
        InnerLoadDepth(vs, 12) != 1 || InnerLoadDepth(vs, 11) != 1 || InnerLoadDepth(vs, 10) != 1 || InnerLoadDepth(vs, 9) != 1) {
      std::cerr << "nesting of the light loads: t9 " << LoadDepths(vs, 9) << "; t10 " << LoadDepths(vs, 10) << "; t11 "
                << LoadDepths(vs, 11) << "; t12 " << LoadDepths(vs, 12) << "\n";
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
    // light) reads neither the frame's block (cb2: but for the unlit word f_post, cb2[3].w, which every
    // unlit pixel program multiplies its colour by since the post chain; case 230) nor, outside the
    // terrain shaders (whose cb1 is the terrain block), the object block (cb1), loads from no light buffer
    // the mode does not name, and does not sample the base map. So what an unlit draw shows depends on
    // nothing of b1, t7, t9 and t10, and of b2 on that one word, on any adapter.
    {
      const std::string vs0 = Disassembly("clay_vs_mode0", WithLightMode(kClayVs, 0).c_str(), "vs_5_0");
      const std::string vs1 = Disassembly("clay_vs_mode1", WithLightMode(kClayVs, 1).c_str(), "vs_5_0");
      if (vs0.empty() || vs1.empty()) return 160;
      if (ReadsBlock(vs0, "cb1") || ReadsBlock(vs0, "cb2") || ReadsResource(vs0, 9) || ReadsResource(vs0, 10) ||
          ReadsResource(vs0, 11) || ReadsResource(vs0, 12)) {
        std::cerr << "the vertex shader of a draw without light reads a light input\n";
        return 160;
      }
      if (ReadsBlock(vs1, "cb1") || ReadsBlock(vs1, "cb2") || !ReadsResource(vs1, 9) || ReadsResource(vs1, 10) ||
          ReadsResource(vs1, 11) || ReadsResource(vs1, 12)) return 160;
      // (what it always reads is still read: the draw's block, positions, indices)
      if (!ReadsBlock(vs0, "cb0") || !ReadsResource(vs0, 0) || !ReadsResource(vs0, 1)) return 160;
      const std::string textured0 = Disassembly("clay_textured_ps_mode0", WithLightMode(kClayTexturedPs, 0).c_str(), "ps_5_0");
      const std::string layer0 = Disassembly("clay_terrain_layer_ps_mode0", WithLightMode(kClayTerrainLayerPs, 0).c_str(), "ps_5_0");
      const std::string layer1 = Disassembly("clay_terrain_layer_ps_mode1", WithLightMode(kClayTerrainLayerPs, 1).c_str(), "ps_5_0");
      const std::string far0 = Disassembly("clay_terrain_far_ps_mode0", WithLightMode(kClayTerrainFarPs, 0).c_str(), "ps_5_0");
      if (textured0.empty() || layer0.empty() || layer1.empty() || far0.empty()) return 161;
      if (ReadsBlock(textured0, "cb1") || ReadsFrameBeyondPost(textured0) || ReadsFrameBeyondPost(layer0) ||
          ReadsFrameBeyondPost(layer1) || ReadsFrameBeyondPost(far0) || ReadsResource(layer0, 8) || ReadsResource(layer1, 8) ||
          ReadsResource(far0, 8) ||
          // (nor the screen mask at t4: an unlit pixel does not sample it)
          ReadsResource(textured0, 4) || ReadsResource(layer0, 4) || ReadsResource(layer1, 4) || ReadsResource(far0, 4)) {
        std::cerr << "a pixel shader of a draw without light reads a light input\n";
        return 161;
      }
      // (the unlit programs still read their own inputs: the albedo, the terrain block and colour; and
      // each the unlit word)
      if (!ReadsResource(textured0, 3) || !ReadsBlock(layer0, "cb1") || !ReadsResource(layer0, 5) ||
          !ReadsResource(far0, 5)) return 161;
      if (!ReadsComponent(textured0, "cb2[3]", 'w') || !ReadsComponent(layer0, "cb2[3]", 'w') || !ReadsComponent(layer1, "cb2[3]", 'w') ||
          !ReadsComponent(far0, "cb2[3]", 'w')) return 161;
      // Every light_mode there is (seven bits), on the program a draw with that mode runs: the vertex
      // shader loads from exactly the light registers at which the pass binds the draw's OWN buffer
      // (LightBufferSources, which is what clay_pass.cpp binds by), every such load is still inside a
      // test (the vertex bound is a root word), it loads from no register outside the layout, every
      // resource it declares has a parameter, and it has no loop.
      static_assert(fable2::native::render::kLightModeCount == 128);
      for (unsigned mode = 0; mode < fable2::native::render::kLightModeCount; ++mode) {
        // Which of the three mesh blocks the mode takes, by the shader's own tests: an object's or
        // trunk's (the object bit without the instance and leaf bits), an instanced plant's (with the
        // instance bit, without the leaf bit), a leaf's (with the probe and leaf bits, without the
        // instance bit), or none (a mode the pass never makes: its program loads nothing of t9 to t11).
        const bool object = (mode & 82) == 2, plant = (mode & 82) == 18, leaf = (mode & 90) == 74, probe = object && (mode & 8) != 0;
        const bool runs = object || plant || leaf;
        const std::string text = WithLightMode(kClayVs, mode);
        const std::string listing = Disassembly("clay_vs_mode", text.c_str(), "vs_5_0");
        if (listing.empty()) return 160;
        const fable2::native::render::ClayLightBuffers own = fable2::native::render::LightBufferSources(mode);
        for (unsigned i = 0; i < fable2::native::render::kClayLightBufferCount; ++i) {
          const bool named = own.source[i] != fable2::native::render::ClayLightBuffer::kPositions;
          const int reg = int(fable2::native::render::kClayBindings[fable2::native::render::kClayLightBufferParam0 + i].reg);
          // (a mode with the terrain bit and a mesh block, which the pass never makes, loads no
          // terrain normals: the block overwrites what they give; LightBufferSources names none there)
          const bool expected = named;
          if ((reg == 9 && runs && named) || ReadsResource(listing, reg) != expected || (expected && BufferLoadGuard(listing, reg) != 1)) {
            std::cerr << "light_mode " << mode << ": t" << reg << " is " << (named ? "" : "not ") << "the draw's own buffer, and the "
                      << "vertex shader " << (ReadsResource(listing, reg) ? "loads from it" : "does not load from it")
                      << " (guard " << BufferLoadGuard(listing, reg) << ")\n";
            return 160;
          }
        }
        if (!LoadsOnlyFrom(listing, buffer_registers) || LoopInstructions(listing) != 0 ||
            BindingsMismatch("clay_vs_mode", text.c_str(), "vs_5_0", true) != 0) return 160;
        // The draw's slot at b1 (cb1). Not read where no mesh block runs. An object or trunk: without
        // the probe bit only its first five registers (the rows, c49 / c27.w, c26 / the colour's power:
        // 80 bytes), by fixed index; with the probe bit seventeen, the probe registers by the clamped
        // set index. An instanced plant (the probe bit beside its bits changes nothing): the first
        // five by fixed index, of which it reads the rows' fourth components and the power: what a
        // small slot holds. A leaf: all eighteen, the probe registers by the clamped set index and
        // its own register (17: the wrap and the inverse scale) behind them.
        bool dynamic = false;
        const unsigned registers = DeclaredRegisters(listing, 1, &dynamic);
        if (registers != (leaf ? 18u : probe ? 17u : runs ? 5u : 0u) || dynamic != (probe || leaf) || ReadsBlock(listing, "cb1") != runs ||
            (listing.find("cb1[17]") != std::string::npos) != leaf) {
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
          const B want10 = plant ? B::kInstanceColours : (probe || leaf) ? B::kProbeSets : object ? B::kColours : B::kPositions;
          const B want11 = plant ? B::kBend : (probe || leaf) ? B::kProbeWeights : B::kPositions;
          if (own.source[2] != want10 || own.source[3] != want11 || (own.source[1] == B::kNormals) != runs) {
            std::cerr << "light_mode " << mode << ": the sources at t10 and t11 are not those of the path the vertex shader takes\n";
            return 160;
          }
        }
        // The rows (the xyz of the first three registers) are an object's, trunk's or leaf's; a plant
        // reads the three fourth components and no row: its normal goes through no rotation. A leaf
        // reads the fourth components too, as the world position's translation, and the frame's eye.
        const bool rows = HasLineWith(listing, "dp3", "cb1[0].xyzx") && HasLineWith(listing, "dp3", "cb1[1].xyzx") &&
                          HasLineWith(listing, "dp3", "cb1[2].xyzx");
        const bool bend_constants = HasLineWith(listing, "mad_sat", "cb1[1].w, cb1[2].w") && HasLineWith(listing, "mul", "cb1[0].wwww");
        const bool translation = listing.find("cb1[0].w") != std::string::npos && listing.find("cb1[1].w") != std::string::npos &&
                                 listing.find("cb1[2].w") != std::string::npos;
        if (rows != (object || leaf) || bend_constants != plant || translation != (plant || leaf) ||
            (listing.find("cb2[6]") != std::string::npos) != leaf ||
            (plant && (listing.find("cb1[0].x") != std::string::npos || listing.find("cb1[3]") != std::string::npos))) {
          std::cerr << "light_mode " << mode << ": rows " << rows << ", bend constants " << bend_constants << "\n";
          return 160;
        }
        // The frame's block (cb2): the axis (register 5), for a lit terrain draw, every object draw
        // and a plant of the family with the ambient factor; a plant without it reads the exponent,
        // the tint and the added light (registers 1 to 3) and no axis; a leaf reads those, the sun's
        // direction (0) and the eye (6), and no axis.
        const unsigned frame_registers = DeclaredRegisters(listing, 2);
        const unsigned frame_want = object ? 6u : plant ? ((mode & 32) ? 6u : 4u) : leaf ? 7u : (mode & 5) == 5 ? 6u : 0u;
        if (ReadsBlock(listing, "cb2") != (runs || (mode & 5) == 5) || frame_registers != frame_want ||
            (leaf && listing.find("cb2[5]") != std::string::npos)) {
          std::cerr << "light_mode " << mode << ": the vertex shader declares " << frame_registers << " registers of cb2\n";
          return 160;
        }
        // The set index is clamped where it is used (an unsigned minimum with the literal 8 survives
        // compiling), and the probe colour's program has no power in it (no log, no exp): the stream
        // colour's, the plant's and the leaf's (its ambient's) have.
        const bool clamp = HasOp(listing, "umin"), power = HasOp(listing, "log");
        if (clamp != (probe || leaf) || ((probe || leaf) && !HasLineWith(listing, "umin", "l(8)")) ||
            power != ((object && !probe) || plant || leaf)) {
          std::cerr << "light_mode " << mode << ": clamp " << clamp << ", power " << power << "\n";
          return 160;
        }
        // Whatever light buffer the program loads from, it compares the vertex with light_count first.
        const bool loads_light = ReadsResource(listing, 9) || ReadsResource(listing, 10) || ReadsResource(listing, 11) || ReadsResource(listing, 12);
        if (loads_light && !HasLineWith(listing, "ult", "cb0[8].z")) return 160;
        // The bend: a plant's program writes it from its inputs (and zeros for a vertex past its
        // buffers); a leaf's writes its two scalars in the first two components (zeros for a vertex
        // past its buffers) and zeros in the other two, with the one division of the light, saturated;
        // every other program writes zeros alone (so the interpolator a pixel shader may read is never
        // left unwritten, and never another draw's).
        const bool zero_bend = HasLineWith(listing, "mov", "o7.xyzw, l(0,0,0,0)");
        const bool leaf_bend = HasLineWith(listing, "mov", "o7.zw, l(0,0,0,0)") && HasLineWith(listing, "mov", "o7.xy, r") &&
                               HasOp(listing, "div_sat");
        if (zero_bend == leaf || leaf_bend != leaf || HasLineWith(listing, "mul", "o7.xyzw") != plant || (!leaf && HasOp(listing, "div_sat"))) {
          std::cerr << "light_mode " << mode << ": the bend output is not " << (plant ? "the plant's" : leaf ? "the leaf's" : "zero") << "\n";
          return 160;
        }
      }
      // The whole program (the mode a root word): all eighteen registers, the probe ones by the index.
      {
        bool dynamic = false;
        if (DeclaredRegisters(vs, 1, &dynamic) != 18 || !dynamic || !HasOp(vs, "umin") || !HasLineWith(vs, "umin", "l(8)")) return 160;
        // (the leaf's clamp too: without it its index is the buffer's word as it is, and one `umin` is left)
        const std::string loose_leaf = Changed(kClayVs, "uint a0 = min(colours[v], 8u);", "uint a0 = colours[v];");
        const std::string loose_78 = Disassembly("loose_leaf_vs_mode78", WithLightMode(loose_leaf.c_str(), 78).c_str(), "vs_5_0");
        if (loose_leaf.empty() || loose_78.empty() || HasOp(loose_78, "umin")) return 160;
        // Control: without the clamp the index is the buffer's word as it is (on a probe object's
        // program: the whole program has the leaf's clamp as well).
        const std::string loose = Changed(kClayVs, "uint a0 = min(word, 8u);", "uint a0 = word;");
        if (loose.empty() || HasOp(Disassembly("loose_vs_mode14", WithLightMode(loose.c_str(), 14).c_str(), "vs_5_0"), "umin") ||
            !HasOp(Disassembly("clay_vs_mode14", WithLightMode(kClayVs, 14).c_str(), "vs_5_0"), "umin")) return 160;
      }
      // The textured pixel shader reads the object's first five registers and, since the fog, its last
      // (17: the form of the draw's fog and the trees' scale), by fixed index, lit or not: the probe
      // registers between them are the vertex stage's and no pixel program names one.
      {
        const std::string lit14 = Disassembly("clay_textured_ps_mode14", WithLightMode(kClayTexturedPs, 14).c_str(), "ps_5_0");
        const std::string whole = Disassembly("clay_textured_ps", kClayTexturedPs, "ps_5_0");
        bool dynamic = true;
        if (lit14.empty() || whole.empty() || DeclaredRegisters(whole, 1, &dynamic) != 18 || dynamic ||
            DeclaredRegisters(lit14, 1) != 18) return 161;
        for (int reg = 5; reg < 17; ++reg) {
          if (whole.find("cb1[" + std::to_string(reg) + "]") != std::string::npos) return 161;
        }
        if (!ReadsComponent(lit14, "cb1[17]", 'w') || !ReadsComponent(lit14, "cb1[17]", 'z')) return 161;
        // A lit object is the same pixel program whichever way its colour was made.
        if (lit14 != Disassembly("clay_textured_ps_mode6", WithLightMode(kClayTexturedPs, 6).c_str(), "ps_5_0")) return 161;
      }
      // Every light_mode, pixel stage (the textured shader): no buffer load; the draw's slot (cb1) read
      // by a lit object, trunk or leaf alone (five registers: c27.w, c49, c26), never by a plant (its slot
      // holds vertex-stage values only) and never without light; the frame's block by every lit draw
      // (a plant without the ambient factor and a leaf: the sun's two registers, of which a leaf reads
      // the colour, and the tint's register for its fourth component, the exposure: three; a plant with
      // the factor also the added light and the floor: five); the exposure (cb2[2], component w) read
      // by every lit program and by no other; the bend input
      // (v7) declared by a lit plant and a lit leaf alone. And a mode's program is the program of its
      // lit, instance, plant-lit and leaf bits (the leaf bit only without the instance bit): nothing
      // else of the mode reaches the stage. Since the fog every lit program declares the whole block
      // (334 registers, indexed by a computed value: the two tables) and reads the fog's flag
      // (register 7, w), the eye (6) and the inverse view-projection's rows (10 to 13); a lit object,
      // trunk or leaf also the draw's last register (17: its fog's form and scale); a plant still reads
      // nothing of the draw's slot. No unlit program has any of it: an unlit program declares the block's
      // first four registers and reads the unlit word (register 3, w) alone. (A lit mode's program reads the
      // word too: `textured` is a root word, and the branch of a draw without an albedo, which no lit draw
      // takes, is in every program of this shader. Case 230 fixes that word as well.)
      {
        static std::string by_bits[128];
        for (unsigned mode = 0; mode < fable2::native::render::kLightModeCount; ++mode) {
          by_bits[mode] = Disassembly("clay_textured_ps_mode", WithLightMode(kClayTexturedPs, mode).c_str(), "ps_5_0");
          const std::string& listing = by_bits[mode];
          if (listing.empty() || listing.find("ld_structured") != std::string::npos || LoopInstructions(listing) != 0) return 161;
          const bool lit = (mode & 4) != 0, plant = lit && (mode & 16) != 0, plant_lit = plant && (mode & 32) != 0;
          const bool leaf = lit && !plant && (mode & 64) != 0;
          bool frame_dynamic = false;
          const unsigned slot = DeclaredRegisters(listing, 1), frame = DeclaredRegisters(listing, 2, &frame_dynamic);
          // (since the shadows: every lit program also reads the mask's coordinate, the fourth components
          // of the frame's registers 4 and 5, so it declares six registers of the block, and samples the
          // mask at t4; no unlit program does either)
          if (slot != (lit && !plant ? 18u : 0u) || ReadsBlock(listing, "cb1") != (lit && !plant) ||
              frame != (!lit ? 4u : kClayFrameSize / 16) || frame_dynamic != lit || ReadsFrameBeyondPost(listing) != lit ||
              !ReadsComponent(listing, "cb2[3]", 'w') ||
              ReadsComponent(listing, "cb2[7]", 'w') != lit || (listing.find("cb2[6]") != std::string::npos) != lit ||
              (listing.find("cb2[13]") != std::string::npos) != lit || (listing.find("cb1[17]") != std::string::npos) != (lit && !plant) ||
              ReadsComponent(listing, "cb2[2]", 'w') != lit ||
              ReadsComponent(listing, "cb2[4]", 'w') != lit || ReadsComponent(listing, "cb2[5]", 'w') != lit || ReadsResource(listing, 4) != lit ||
              (listing.find("sample_l") != std::string::npos) != lit ||
              (lit && ((plant && !plant_lit) || leaf) && (ReadsComponent(listing, "cb2[3]", 'x') || listing.find("cb2[4].x") != std::string::npos)) ||
              HasLineWith(listing, "dcl_input_ps", "v7.xyzw") != plant || HasLineWith(listing, "dcl_input_ps", "v7.xy") != (plant || leaf) ||
              (leaf && (listing.find("cb2[0]") != std::string::npos || !HasOp(listing, "ge")))) {
            std::cerr << "light_mode " << mode << ": the textured pixel shader declares " << slot << " registers of cb1 and " << frame
                      << " of cb2\n";
            return 161;
          }
          const unsigned canon = !lit ? 0u : plant ? (mode & 52u) : leaf ? 68u : 4u;
          if (canon < mode && listing != by_bits[canon]) return 161;
        }
        // (the four lit programs are four programs)
        if (by_bits[4] == by_bits[20] || by_bits[4] == by_bits[52] || by_bits[20] == by_bits[52] || by_bits[0] == by_bits[4] ||
            by_bits[68] == by_bits[4] || by_bits[68] == by_bits[20] || by_bits[68] == by_bits[52] || by_bits[68] == by_bits[0] ||
            by_bits[78] != by_bits[68]) return 161;
      }
      // The lit programs do read them (the check above is not empty): mode 6 a lit object, 5 lit ground.
      const std::string vs6 = Disassembly("clay_vs_mode6", WithLightMode(kClayVs, 6).c_str(), "vs_5_0");
      const std::string textured6 = Disassembly("clay_textured_ps_mode6", WithLightMode(kClayTexturedPs, 6).c_str(), "ps_5_0");
      const std::string layer5 = Disassembly("clay_terrain_layer_ps_mode5", WithLightMode(kClayTerrainLayerPs, 5).c_str(), "ps_5_0");
      if (!ReadsBlock(vs6, "cb1") || !ReadsBlock(vs6, "cb2") || !ReadsResource(vs6, 10) || !ReadsResource(vs6, 11) ||
          ReadsResource(vs6, 9) || ReadsResource(vs6, 12) || !ReadsBlock(textured6, "cb1") || !ReadsBlock(textured6, "cb2") ||
          !ReadsBlock(layer5, "cb2") || !ReadsResource(layer5, 8) || !ReadsResource(layer5, 4) || !ReadsResource(textured6, 4)) return 161;
      // Controls: an unlit textured colour that takes anything from the frame's block but the unlit word,
      // and a vertex output whose value without light comes from it, are reported.
      std::string leak = kClayTexturedPs;
      const std::string unlit = "return float4(clay_linear(t.rgb * (0.75 + 0.25 * facet)) * f_post,";
      size_t at = leak.find(unlit);
      if (at == std::string::npos) return 162;
      leak.replace(at, unlit.size(), "return float4(clay_linear(t.rgb * (0.75 + 0.25 * facet)) * f_post + f_tint,");
      const std::string leaking = Disassembly("leaking_ps", WithLightMode(leak.c_str(), 0).c_str(), "ps_5_0");
      if (leaking.empty() || !ReadsFrameBeyondPost(leaking)) return 162;
      leak = kClayVs;
      const std::string axis = "o.axis = float3(0, 0, 0);";
      at = leak.find(axis);
      if (at == std::string::npos) return 162;
      leak.replace(at, axis.size(), "o.axis = f_axis;");
      const std::string leaking_vs = Disassembly("leaking_vs", WithLightMode(leak.c_str(), 0).c_str(), "vs_5_0");
      if (leaking_vs.empty() || !ReadsBlock(leaking_vs, "cb2")) return 162;
    }
  }
  // 166: the exposure changes the program of no draw without light (the final fix wave). Each clay
  // shader against its own text as it was before (WithoutExposure: commit 24c1751's), compiled with
  // light_mode fixed: the vertex shader at every mode and as a whole (it never reads the field), the
  // three pixel shaders that light at every mode without bit 2: the same instructions, line for line.
  // So a frame with the light off, and every unlit draw of a lit frame, is drawn by the programs that
  // drew it before. Controls: the lit programs do differ (the comparison is not empty), by a read of
  // cb2[2].w, which the programs before did not have.
  {
    const struct {
      const char* name;
      const char* src;
      const char* target;
      bool pixel;
    } changed[] = {{"clay_vs", kClayVs, "vs_5_0", false},
                   {"clay_textured_ps", kClayTexturedPs, "ps_5_0", true},
                   {"clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0", true},
                   {"clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0", true}};
    for (const auto& s : changed) {
      const std::string before = WithoutExposure(s.src, s.pixel);
      if (before.empty()) return 166;
      if (!s.pixel && Code(Disassembly(s.name, s.src, s.target)) != Code(Disassembly("before", before.c_str(), s.target))) return 166;
      for (unsigned mode = 0; mode < fable2::native::render::kLightModeCount; ++mode) {
        if (s.pixel && (mode & 4)) continue;
        const std::string now = Code(Disassembly(s.name, WithLightMode(s.src, mode).c_str(), s.target));
        if (now.empty() || now != Code(Disassembly("before", WithLightMode(before.c_str(), mode).c_str(), s.target))) {
          std::cerr << s.name << " light_mode " << mode << ": the program without light is not the program before the exposure\n";
          return 166;
        }
      }
      if (s.pixel) {
        const std::string now = Disassembly(s.name, WithLightMode(s.src, 5).c_str(), s.target);
        const std::string was = Disassembly("before", WithLightMode(before.c_str(), 5).c_str(), s.target);
        if (Code(now) == Code(was) || !ReadsComponent(now, "cb2[2]", 'w') || ReadsComponent(was, "cb2[2]", 'w')) return 166;
      }
    }
    // (the mapping has no frame block at all; it still multiplies by its own first word, which the pass
    // sets to 1: kClayMapExposure. The flat shader has the block since the unlit draws' fog, and the
    // program of every light_mode without that fog's bit reads nothing of it but the unlit word: the old
    // flat program with that one multiply, case 230.)
    if (ReadsBlock(Disassembly("output_map_ps", kOutputMapPs, "ps_5_0"), "cb2") || fable2::native::render::kClayMapExposure != 1.0f) return 166;
    {
      const std::string flat0 = Code(Disassembly("clay_ps", WithLightMode(kClayPs, 0).c_str(), "ps_5_0"));
      if (flat0.empty() || ReadsFrameBeyondPost(flat0) || !ReadsComponent(flat0, "cb2[3]", 'w') || ReadsBlock(flat0, "cb1")) return 166;
      for (unsigned mode = 1; mode < fable2::native::render::kLightModeCount; ++mode) {
        if (Code(Disassembly("clay_ps", WithLightMode(kClayPs, mode).c_str(), "ps_5_0")) != flat0) return 166;
      }
      // (the whole program, the mode a root word, does read the block: the comparison is not empty)
      if (!ReadsFrameBeyondPost(Disassembly("clay_ps", kClayPs, "ps_5_0"))) return 166;
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
    const std::string halved = Changed(kClayTexturedPs, "      return float4(clay_lit(lit * f_exposure),", "      lit *= 0.5;\n      return float4(clay_lit(lit * f_exposure),");
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
    const std::string out_changed = Changed(kClayTerrainFarPs, "  if (light_mode & 4) return float4(clay_lit(colour * f_exposure),",
                                            "  colour *= 0.5;\n  if (light_mode & 4) return float4(clay_lit(colour * f_exposure),");
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
          {"sample_base", 2, "  if (t_base_sampler == 0) t = t_base.SampleGrad(s_point_wrap, at, d.xy, d.zw);",
           "  at = at * 0.5;\n  if (t_base_sampler == 0) t = t_base.SampleGrad(s_point_wrap, at, d.xy, d.zw);", 3},
          {"base_linear", 3, "  float3 l = g * (261120.0 * scale) + offset;", "  float3 l = g * (261120.0 * scale);", 3},
          {"layer normal", 2, "  [branch] if (light_mode & 4) {\n    float3 N = light_normalize(normal);\n    float facing",
           "  if (light_mode & 4) normal = -normal;\n  [branch] if (light_mode & 4) {\n    float3 N = light_normalize(normal);\n    float facing", 4},
          {"far squared", 3, "  float2 base_at = world.xy * t_base_uv.xy + t_base_uv.zw;",
           "  lin = lin * lin;\n  float2 base_at = world.xy * t_base_uv.xy + t_base_uv.zw;", 5},
          {"textured vcolour", 1, "    if (light_mode & 4) {\n      float3 lit;", "    vcolour *= 0.5;\n    if (light_mode & 4) {\n      float3 lit;", 2},
          {"far base_at", 3, "  float2 base_at = world.xy * t_base_uv.xy + t_base_uv.zw;", "  float2 base_at = uv;", 5},
          // ... and an insertion, a deletion and two reorderings inside the probe colour's block.
          {"probe insert", 0, "      float4 w = probe_or_bend[v];", "      float4 w = probe_or_bend[v];\n      w = abs(w);", 1},
          {"probe delete", 0, "o.vcolour = o_probe[a0].xyz + float3(", "o.vcolour = float3(", 1},
          {"probe rows", 0, "dot(o_probe[a0 + 1], w), dot(o_probe[a0 + 2], w),", "dot(o_probe[a0 + 2], w), dot(o_probe[a0 + 1], w),", 1},
          {"probe order", 0, "      uint a0 = min(word, 8u);\n      float4 w = probe_or_bend[v];",
           "      float4 w = probe_or_bend[v];\n      uint a0 = min(word, 8u);", 1},
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
          {"plant c26", 1, "f_sun_col, SunGateSingle(mask, 1.0));\n        }\n        lit = FogForm(lit, fog, 0.0, 0.0);\n      } else if (light_mode & 64) {",
           "f_sun_col, SunGateSingle(mask, 1.0));\n        }\n        lit = FogForm(lit, fog, 0.0, 0.0);\n        lit *= o_c26;\n      } else if (light_mode & 64) {", 2},
          {"PlantBend", 0, "  float h = saturate(height);", "  float h = height;", 6},
          {"PlantNormal", 1, "  float3 n = light_normalize(normal);\n  return light_normalize(n + (bend.xyz - n)",
           "  float3 n = normal;\n  return light_normalize(n + (bend.xyz - n)", 6},
          {"LightPlantLit", 1, "  return A * (vs_base * AmbientFactor(amb_floor, t) + add + light_sun(",
           "  return A * ((vs_base + add) * AmbientFactor(amb_floor, t) + light_sun(", 6},
          // The tree leaves (plan Task 8c). The functions: the normal not turned to the viewer, the
          // cosine not wrapped, the back light without the viewer's factor, the mask's second factor
          // not applied twice, the back light through every texel, c27.w on the sun too. The vertex
          // stage: the normal normalised (the game uses it as it is), the exponent without its factor,
          // the world position without its translation or with the scale left in, the vector FROM the
          // eye, the wrap dropped, the set not clamped, a vector of t made. The pixel stage: c26
          // dropped, the two scalars exchanged, another literal in the mask.
          {"LeafLight turn", 0, "  float3 n = (dot(view, normal) < 0.0) ? -normal : normal;", "  float3 n = normal;", 6},
          {"LeafLight wrap", 0, "saturate((nl + wrap) / d)", "saturate(nl)", 6},
          {"LeafLight back", 0, "saturate(-nl) * (1.0 - turn * turn)", "saturate(-nl)", 6},
          {"LeafMask", 1, "  return float2(m1, m1 * gain + bias);", "  return float2(m1, m1);", 6},
          {"LightLeaf green", 1, "  float green = (A.g >= A.r) ? 1.0 : 0.0;", "  float green = 1.0;", 6},
          {"LightLeaf c27w", 1, "(vs_ambient * c27w + m1 * sun_col * front)", "((vs_ambient + m1 * sun_col * front) * c27w)", 6},
          {"leaf normal", 0, "    o.normal = N;\n", "    N = light_normalize(N);\n    o.normal = N;\n", 1},
          {"leaf exponent", 0, "InstanceAmbient(probe, f_tint, o_colour_power * f_tint_pow, f_add);",
           "InstanceAmbient(probe, f_tint, f_tint_pow, f_add);", 1},
          {"leaf translation", 0, " * o_leaf.y +\n                float3(o_rot0.w, o_rot1.w, o_rot2.w);", " * o_leaf.y;", 1},
          {"leaf scale", 0, "dot(o_rot2.xyz, p.xyz)) * o_leaf.y +", "dot(o_rot2.xyz, p.xyz)) +", 1},
          {"leaf view", 0, "light_normalize(f_eye - at)", "light_normalize(at - f_eye)", 1},
          {"leaf wrap", 0, "o_leaf.x, 1.0), 0, 0);", "0.0, 1.0), 0, 0);", 1},
          {"leaf clamp", 0, "uint a0 = min(colours[v], 8u);", "uint a0 = colours[v];", 1},
          {"leaf axis", 0, "    o.normal = N;\n    o.axis = float3(0, 0, 0);", "    o.normal = N;\n    o.axis = AmbientAxis(N, f_axis);", 1},
          {"leaf c26", 1, "lit = FogForm(lit, fog, o_leaf.z, o_leaf.w) * o_c26;", "lit = FogForm(lit, fog, o_leaf.z, o_leaf.w);", 2},
          // The fog (fog sub-project, plan Task 4). The functions: the two tables' coordinates exchanged,
          // the start not subtracted, a table read half a texel off, the axis reversed, no blend from the
          // near colour, the amount dropped, the trees' form with the ground's scale, a pixel at the eye
          // let through. The pixel stage: a plant in the trees' form, c26 before the fog on a leaf (the
          // first of the two such lines), a layer without fog, the far ground fogged after its cross-fade,
          // the fog's coordinates from another depth.
          {"fog tables exchanged", 1, "    float3 S = fog_scatter(p.a);\n    float3 T = fog_transmit(p.s);",
           "    float3 S = fog_scatter(p.s);\n    float3 T = fog_transmit(p.a);", 6},
          {"fog start", 2, "  float dD = q * inv - f_fog_start;", "  float dD = q * inv;", 6},
          {"fog half a texel", 3, "saturate(u) * 256.0 - 0.5, 0.0, 255.0);", "saturate(u) * 256.0, 0.0, 255.0);", 6},
          {"fog axis", 1, "dot(V, f_fog_axis) * -inv);", "dot(V, f_fog_axis) * inv);", 6},
          {"fog near blend", 1, "float3 F = f_fog_near + p.t * (S - f_fog_near);", "float3 F = S;", 6},
          {"fog amount", 2, "c = c + f_fog_amount * (G - c);", "c = G;", 6},
          {"fog tree scale", 1, "c = c * T + (1.0 - T) * S * scale;", "c = c * T + (1.0 - T) * S * f_fog_scatter;", 6},
          {"fog at the eye", 1, "bool reach = placed && d2 > 0.0 && d2 < 1.0e30;", "bool reach = placed && d2 < 1.0e30;", 6},
          {"fog off ignored", 1, "o.fogged = f_fog_on != 0 && reach && dD > 0.0;", "o.fogged = reach && dD > 0.0;", 6},
          {"plant in the trees' form", 1, "lit = FogForm(lit, fog, 0.0, 0.0);", "lit = FogForm(lit, fog, o_leaf.z, o_leaf.w);", 2},
          {"leaf c26 before the fog", 1, "lit = FogForm(lit, fog, o_leaf.z, o_leaf.w) * o_c26;", "lit = FogForm(lit * o_c26, fog, o_leaf.z, o_leaf.w);", 2},
          {"layer without fog", 2, "    lin = FogForm(lin, FogCoords(pos), 0.0, 0.0);\n", "", 4},
          {"far fog after the fade", 3, "    lin = FogForm(lin, FogCoords(pos), 0.0, 0.0);\n  }\n  if (t_flags & 1) lin *= terrain_fade_in(world);",
           "  }\n  if (t_flags & 1) lin *= terrain_fade_in(world);\n  if (light_mode & 4) lin = FogForm(lin, FogCoords(pos), 0.0, 0.0);", 5},
          {"fog at another depth", 2, "lin = FogForm(lin, FogCoords(pos), 0.0, 0.0);", "lin = FogForm(lin, FogCoords(float4(pos.xy, 1.0, 1.0)), 0.0, 0.0);", 4},
          {"leaf sides", 1, "vcolour, bend.x, bend.y, f_sun_col,", "vcolour, bend.y, bend.x, f_sun_col,", 2},
          {"leaf mask literal", 1, "LeafMask(mask, 0.6, 0.4);", "LeafMask(mask, 0.6, 0.6);", 2},
          // The shadows (plan Task 5 of sub-project 8): the mask dropped from a family's sun term, put on
          // the whole colour, read at another place, or the leaf's taken as 1.
          {"object without the mask", 1, "o_c27w, SunGateSingle(mask, 1.0), o_c49)", "o_c27w, SunGateSingle(1.0, 1.0), o_c49)", 2},
          {"plant without the mask", 1, "f_sun_col, SunGateSingle(mask, 1.0));\n        }", "f_sun_col, SunGateSingle(1.0, 1.0));\n        }", 2},
          {"plant-lit without the mask", 1, "                              SunGateSingle(mask, 1.0));", "                              SunGateSingle(1.0, 1.0));", 2},
          {"leaf without the mask", 1, "LeafMask(mask, 0.6, 0.4);", "LeafMask(1.0, 0.6, 0.4);", 2},
          {"layer without the mask", 2, "SunGateSingle(mask, facing));", "SunGateSingle(1.0, facing));", 4},
          {"mask on the whole colour", 1, "      return float4(clay_lit(lit * f_exposure),", "      lit *= mask;\n      return float4(clay_lit(lit * f_exposure),", 2},
          {"mask at another place", 2, "(pos.xy - 0.5) * float2(f_mask_du, f_mask_dv), 0).r;", "(pos.xy - 0.5) * float2(f_mask_dv, f_mask_du), 0).r;", 4},
          {"mask at the pixel's centre", 2, "(pos.xy - 0.5) * float2(f_mask_du, f_mask_dv), 0).r;", "pos.xy * float2(f_mask_du, f_mask_dv), 0).r;", 4},
          {"mask on the far ground", 3, "dot(N, f_sun_dir), f_sun_col, t_c11x);", "dot(N, f_sun_dir), f_sun_col, t_c11x * f_mask_du);", 5},
          // The exposure (the final fix wave): dropped from a lit return, put outside the bound, put on
          // an unlit return (the textured colour, the flat colour of the textured shader, the ground).
          {"exposure dropped", 1, "clay_lit(lit * f_exposure)", "clay_lit(lit)", 2},
          {"ground exposure dropped", 2, "clay_lit(colour * f_exposure)", "clay_lit(colour)", 3},
          {"exposure outside the bound", 3, "clay_lit(colour * f_exposure)", "clay_lit(colour) * f_exposure", 3},
          {"exposure on the unlit colour", 1, "return float4(clay_linear(t.rgb * (0.75 + 0.25 * facet)) * f_post,",
           "return float4(clay_linear(t.rgb * (0.75 + 0.25 * facet)) * f_post * f_exposure,", 2},
          {"exposure on the flat colour", 1, "return float4(clay_linear(base * (0.35 + 0.65 * facet)) * f_post, 1.0);",
           "return float4(clay_linear(base * (0.35 + 0.65 * facet)) * f_post * f_exposure, 1.0);", 2},
          {"exposure on the unlit ground", 2, "return float4(saturate(colour * (shade * shade)) * f_post,",
           "return float4(saturate(colour * (shade * shade) * f_exposure),", 3},
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
  // The shadow maps' pixel shader (plan Task 4 of sub-project 8; clay_logic.h, "Native shadow maps").
  {
    if (!Compile("clay_shadow_ps", kClayShadowPs, "ps_5_0")) return 170;
    // Bound by the pass's layout like every clay shader, with the one cbuffer Draw at ClayConstants' offsets.
    if (int at = BindingsMismatch("clay_shadow_ps", kClayShadowPs, "ps_5_0", false)) return 171;
    if (int at = DrawLayoutMismatch("clay_shadow_ps", kClayShadowPs, "ps_5_0")) return 172;
    if (BindPoint("clay_shadow_ps", kClayShadowPs, "ps_5_0", "albedo") != 3) return 173;
    // It reads neither the frame's nor the draw's light block (it declares none), and no light field.
    if (strstr(kClayShadowPs, "register(b1)") || strstr(kClayShadowPs, "register(b2)") || strstr(kClayShadowPs, "light_mode &")) return 173;
    // Depth only: it writes the depth and no colour, and its discards survive compiling.
    const std::string listing = Disassembly("clay_shadow_ps", kClayShadowPs, "ps_5_0");
    if (listing.empty() || !HasLineWith(listing, "dcl_output", "oDepth") || HasLineWith(listing, "dcl_output", "o0")) return 174;
    if (listing.find("discard") == std::string::npos || LoopInstructions(listing) != 0) return 175;
    if (!strstr(kClayShadowPs, ": SV_Depth {") || strstr(kClayShadowPs, "SV_Target")) return 176;
    // Its inputs are the vertex shader's first three outputs, at the vertex shader's registers.
    if (!strstr(kClayShadowPs, "float main(float4 pos : SV_Position, float3 ndc : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Depth {")) return 177;
    unsigned vs_mask = 0, ps_mask = 0, none = 0;
    const int vs_uv = TexcoordRegister("clay_vs", kClayVs, "vs_5_0", true, 1, &vs_mask);
    const int ps_uv = TexcoordRegister("clay_shadow_ps", kClayShadowPs, "ps_5_0", false, 1, &ps_mask);
    if (vs_uv < 0 || vs_uv != ps_uv || (ps_mask & ~vs_mask) != 0) return 178;
    if (TexcoordRegister("clay_shadow_ps", kClayShadowPs, "ps_5_0", false, 2, &none) != -1) return 178;
    // The bias (ShadowMapDepth in clay_logic.h): the depth's larger derivative times the slope term (the
    // bits of pad2) plus the constant term (the bits of color), saturated, after the discards.
    const char* test = strstr(kClayShadowPs, "if ((alpha_mode & 0x8) && !alpha_passes(a, alpha_ref, alpha_mode & 7)) discard;");
    const char* mask = strstr(kClayShadowPs, "if (!(a >= 1.0 - offset * 0.25)) discard;");
    const char* slope = strstr(kClayShadowPs, "float slope = max(abs(ddx(pos.z)), abs(ddy(pos.z)));");
    const char* out = strstr(kClayShadowPs, "return saturate(pos.z + asfloat(pad2) * slope + asfloat(color));");
    if (!test || !mask || !slope || !out || !(test < mask && mask < slope && slope < out)) return 179;
    // Alpha-to-mask as on one sample (the map has one): the offset of the pixel's parity from bits 16-23,
    // no form by sample count, no flat threshold.
    if (!strstr(kClayShadowPs, "uint index = ((p.x & 1) << 1) | (p.y & 1);") ||
        !strstr(kClayShadowPs, "uint offset = (alpha_mode >> (16 + 2 * index)) & 3;") || strstr(kClayShadowPs, "samples") ||
        strstr(kClayShadowPs, "a < 0.5") || strstr(kClayShadowPs, "a >= 0.5")) {
      return 180;
    }
    // The alpha is the texture's component times the scale, as the textured shader forms it.
    if (!strstr(kClayShadowPs, "float a = alpha_scale;") ||
        !strstr(kClayShadowPs, "a *= comp == 0 ? t.x : comp == 1 ? t.y : comp == 2 ? t.z : t.w;")) return 181;
    // The compiled program reads both bias words and both derivatives.
    if (!HasOp(listing, "deriv_rtx") && !HasOp(listing, "deriv_rtx_coarse")) return 182;
    if (!HasOp(listing, "deriv_rty") && !HasOp(listing, "deriv_rty_coarse")) return 182;
    // The textured shader keeps the form by sample count; the shadow shader's one-sample form is its own.
    if (strstr(kClayTexturedPs, "1.0 - offset * 0.25")) return 183;
  }
  // The shadow view's pixel shader: the two maps at t0 and t1 and the mask at t2, read without a sampler,
  // eight constants.
  {
    if (!Compile("shadow_view_ps", kShadowViewPs, "ps_5_0")) return 184;
    if (BindPoint("shadow_view_ps", kShadowViewPs, "ps_5_0", "near_map") != 0 ||
        BindPoint("shadow_view_ps", kShadowViewPs, "ps_5_0", "far_map") != 1 ||
        BindPoint("shadow_view_ps", kShadowViewPs, "ps_5_0", "mask") != 2 ||
        BindPoint("shadow_view_ps", kShadowViewPs, "ps_5_0", "C") != 0) {
      return 185;
    }
    const std::string listing = Disassembly("shadow_view_ps", kShadowViewPs, "ps_5_0");
    if (listing.empty() || HasLineWith(listing, "dcl_sampler", "s") || listing.find("ld_indexable") == std::string::npos || LoopInstructions(listing) != 0) return 186;
    const ClayBufferField fields[] = {{"out_w", 0, 4},    {"out_h", 4, 4},   {"flags", 8, 4},     {"map_size", 12, 4},
                                      {"mask_w", 16, 4}, {"mask_h", 20, 4}, {"view_pad", 24, 8}};
    if (BufferLayoutMismatch("shadow_view_ps", kShadowViewPs, "ps_5_0", "C", fields, 7, 32)) return 187;
    // Grey is the stored depth and the mask's value; a frame without maps or without a mask shows none.
    if (!strstr(kShadowViewPs, "if ((flags & 1) == 0) return float4(0.0, 0.0, 0.25, 1.0);") ||
        !strstr(kShadowViewPs, "if ((flags & 2) == 0) return float4(0.0, 0.0, 0.25, 1.0);") ||
        !strstr(kShadowViewPs, "return float4(d, d, d, 1.0);") || !strstr(kShadowViewPs, "float m = saturate(mask.Load(int3(t, 0)).r);") ||
        !strstr(kShadowViewPs, "return float4(m, m, m, 1.0);")) return 188;
    // (the mask's flag is tested before the mask is read, the maps' before a map is)
    const char* maps_flag = strstr(kShadowViewPs, "if ((flags & 1) == 0)");
    const char* map_read = strstr(kShadowViewPs, "far_map.Load(");
    const char* mask_flag = strstr(kShadowViewPs, "if ((flags & 2) == 0)");
    const char* mask_read = strstr(kShadowViewPs, "mask.Load(");
    if (!maps_flag || !map_read || !mask_flag || !mask_read || !(maps_flag < map_read && map_read < mask_flag && mask_flag < mask_read)) return 188;
  }
  // 190 to 199: the shadowed sun (plan Task 5 of sub-project 8; clay_logic.h, "Native shadows").
  // 190, 191: the screen mask in the lit shaders. It is the second texture of the albedo's parameter (t4),
  // read once in each of the two shaders whose families have it (the textured shader: objects, trunks,
  // plants, leaves; the layer shader), by a lit pixel alone (per light_mode, above), with the linear
  // clamped sampler at the pixel's corner over the target's size (the mean of four mask texels, as the game's fetch); the far terrain has no mask and the
  // flat shader and the shadow maps' shader none either.
  {
    using namespace fable2::native::render;
    const char* read = "float mask = shadow_mask.SampleLevel(s_linear_clamp, (pos.xy - 0.5) * float2(f_mask_du, f_mask_dv), 0).r;";
    for (const char* src : {kClayTexturedPs, kClayTerrainLayerPs}) {
      if (!strstr(src, "Texture2D shadow_mask : register(t4);") || Count(src, read) != 1 || Count(src, "shadow_mask.") != 1 ||
          Count(src, "f_mask_du") != 3 || Count(src, "f_mask_dv") != 4) return 190;  // (the block names f_mask_dv twice; and
                                                                                    // once each in the fog's FogCoords, which
                                                                                    // takes the pixel's screen position by them)
    }
    if (BindPoint("clay_textured_ps", kClayTexturedPs, "ps_5_0", "shadow_mask") != int(kClayMaskRegister) ||
        BindPoint("clay_terrain_layer_ps", kClayTerrainLayerPs, "ps_5_0", "shadow_mask") != int(kClayMaskRegister) || kClayMaskRegister != 4) return 190;
    for (const char* src : {kClayTerrainFarPs, kClayPs, kClayShadowPs, kClayVs}) {
      if (strstr(src, "shadow_mask")) return 190;
    }
    // (the block's declaration, and the fog's FogCoords in the shared text, which the vertex shader does not compile)
    if (Count(kClayTerrainFarPs, "f_mask_du") != 2 || Count(kClayVs, "f_mask_du") != 2) return 190;
    if (BindPoint("clay_terrain_far_ps", kClayTerrainFarPs, "ps_5_0", "shadow_mask") != -1) return 190;
    // The mask multiplies each family's sun term and nothing else: the four gates and the leaf's factors
    // take it, no gate is left at a mask of 1, and the far terrain's function has none.
    const char* t = kClayTexturedPs;
    if (Count(t, "SunGateSingle(mask, 1.0)") != 3 || Count(t, "LeafMask(mask, 0.6, 0.4)") != 1 || strstr(t, "SunGateSingle(1.0") ||
        strstr(t, "LeafMask(1.0") || Count(kClayTerrainLayerPs, "SunGateSingle(mask, facing)") != 1 ||
        strstr(kClayTerrainLayerPs, "SunGateSingle(1.0") || strstr(kClayTerrainFarPs, "SunGateSingle(") != strstr(kClayTerrainFarPs, "SunGateSingle(float mask") ||
        !strstr(kClayTerrainFarPs, "c11x * light_sun(sun_col, n_dot_sun, 1.0)")) return 191;
    // The mask is read inside the lit branch, after the discards, before the families' calls.
    const char* discards = strstr(t, "if (!(a >= mask_base - offset * per_offset)) discard;");
    const char* branch = strstr(t, "if (light_mode & 4) {");
    const char* at = strstr(t, read);
    const char* first_call = strstr(t, "lit = LightPlantLit(");
    if (!discards || !branch || !at || !first_call || !(discards < branch && branch < at && at < first_call)) return 191;
    const char* l = kClayTerrainLayerPs;
    const char* l_branch = strstr(l, "[branch] if (light_mode & 4) {");
    const char* l_at = strstr(l, read);
    const char* l_call = strstr(l, "lin = LightTerrainLayer(");
    if (!l_branch || !l_at || !l_call || !(l_branch < l_at && l_at < l_call)) return 191;
    // Compiled: the lit ground's program samples t4 with the linear clamped sampler (s3) at level 0.
    const std::string layer5 = Disassembly("clay_terrain_layer_ps_mode5", WithLightMode(kClayTerrainLayerPs, 5).c_str(), "ps_5_0");
    if (layer5.empty() || !ReadsResource(layer5, 4) || !HasLineWith(layer5, "sample_l_indexable(texture2d)(float,float,float,float)", ", s3, l(0") ||
        !ReadsComponent(layer5, "cb2[4]", 'w') || !ReadsComponent(layer5, "cb2[5]", 'w')) return 191;
    const std::string textured6 = Disassembly("clay_textured_ps_mode6", WithLightMode(kClayTexturedPs, 6).c_str(), "ps_5_0");
    if (!HasLineWith(textured6, "sample_l_indexable(texture2d)(float,float,float,float)", ", s3, l(0")) return 191;
  }
  // 192 to 194: the depth pre-pass's shaders: each colour shader's own text under CLAY_DEPTH_ONLY. They
  // compile, bind inside the pass's layout, declare the draw's constants as the others, keep their
  // discards, have no loop, and read nothing of the light: not the frame's block, not the object's slot
  // (the terrain's they read: the weight and the fade make the alpha), not the mask, not the base map,
  // not the side texture; and they sample what makes their alpha (the albedo; the layer's weights).
  {
    const struct {
      const char* name;
      const char* colour;
      const char* depth;
    } pairs[] = {{"clay_textured_depth_ps", kClayTexturedPs, kClayTexturedDepthPs},
                 {"clay_terrain_layer_depth_ps", kClayTerrainLayerPs, kClayTerrainLayerDepthPs},
                 {"clay_terrain_far_depth_ps", kClayTerrainFarPs, kClayTerrainFarDepthPs}};
    const char* define = "#define CLAY_DEPTH_ONLY 1\n";
    for (int i = 0; i < 3; ++i) {
      const auto& p = pairs[i];
      if (strncmp(p.depth, define, strlen(define)) != 0 || strcmp(p.depth + strlen(define), p.colour) != 0) return 192;
      // (since the fog: the colour form defines CLAY_FOG unless it is the depth form, and the shared
      // light text has the fog's tail of the frame's block and the fog's functions under it, with
      // names that do nothing in its place otherwise)
      if (strstr(p.colour, "#define CLAY_DEPTH_ONLY") || Count(p.colour, "#ifdef CLAY_DEPTH_ONLY") != 1 || Count(p.colour, "#else") != 2 ||
          Count(p.colour, "#endif") != 4 || Count(p.colour, "#ifdef CLAY_FOG") != 2 || Count(p.colour, "#ifndef CLAY_DEPTH_ONLY") != 1 ||
          Count(p.colour, "#define CLAY_FOG 1") != 1 || strstr(p.colour, "#ifndef CLAY_DEPTH_ONLY") != p.colour) return 192;
      if (!Compile(p.name, p.depth, "ps_5_0")) return 192;
      if (BindingsMismatch(p.name, p.depth, "ps_5_0", false) != 0 || DrawLayoutMismatch(p.name, p.depth, "ps_5_0") != 0) return 193;
      const std::string listing = Disassembly(p.name, p.depth, "ps_5_0");
      if (listing.empty() || LoopInstructions(listing) != 0 || listing.find("discard") == std::string::npos ||
          listing.find("ld_structured") != std::string::npos) return 193;
      // (the textured form alone reads one word of the frame's block, the unlit word f_post, in the branch
      // of a draw without an albedo, which lies after the depth form's return and writes a colour no target
      // takes: the post sub-project's plan Task 5. The terrain forms return before any colour.)
      if ((i == 0 ? ReadsFrameBeyondPost(listing) : ReadsBlock(listing, "cb2")) || ReadsResource(listing, 4) || ReadsResource(listing, 7) ||
          ReadsResource(listing, 8)) {
        std::cerr << p.name << " reads a light input\n";
        return 194;
      }
      if (i == 0 && (ReadsBlock(listing, "cb1") || !ReadsResource(listing, 3) || HasLineWith(listing, "dcl_input_ps", "v7"))) return 194;
      if (i == 1 && (!ReadsBlock(listing, "cb1") || !ReadsResource(listing, 6) || ReadsResource(listing, 5))) return 194;
      // (every light_mode gives the same program: the mode reaches nothing of it)
      const std::string mode0 = Code(Disassembly(p.name, WithLightMode(p.depth, 0).c_str(), "ps_5_0"));
      for (unsigned mode : {4u, 5u, 6u, 14u, 22u, 54u, 78u, 127u}) {
        if (mode0.empty() || Code(Disassembly(p.name, WithLightMode(p.depth, mode).c_str(), "ps_5_0")) != mode0) return 194;
      }
      // The discards are the colour form's: the same number of them as its program without light has.
      auto discards = [](const std::string& text) {
        int n = 0;
        for (size_t at = text.find("discard"); at != std::string::npos; at = text.find("discard", at + 1)) ++n;
        return n;
      };
      if (discards(mode0) != discards(Code(Disassembly(p.name, WithLightMode(p.colour, 0).c_str(), "ps_5_0"))) || discards(mode0) == 0) return 194;
    }
    // The depth form returns after the discards and before anything of the light, in the text.
    const char* t = kClayTexturedPs;
    const char* discard = strstr(t, "if (!(a >= mask_base - offset * per_offset)) discard;");
    const char* guard = strstr(t, "#ifdef CLAY_DEPTH_ONLY\n    return float4(0.0, 0.0, 0.0, 0.0);\n#else\n    if (light_mode & 4) {");
    if (!discard || !guard || guard < discard) return 194;
    for (const char* src : {kClayTerrainLayerPs, kClayTerrainFarPs}) {
      const char* out_discard = strstr(src, "if (!(a >= mask_base - offset * per_offset)) discard;");
      const char* out_guard = strstr(src, "#ifdef CLAY_DEPTH_ONLY\n  return float4(0.0, 0.0, 0.0, 0.0);\n#else\n  if (light_mode & 4) return float4(clay_lit(");
      if (!out_discard || !out_guard || out_guard < out_discard) return 194;
    }
    // The layer's hole is cut before its alpha, in both forms (it is in main, above terrain_out).
    if (Disassembly("layer_depth", kClayTerrainLayerDepthPs, "ps_5_0").find("discard") == std::string::npos ||
        !strstr(kClayTerrainLayerDepthPs, "if (value < t_hole_ref) discard;")) return 194;
  }
  // 195 to 199: the mask step's shader (kClayMaskPs): the game's mask arithmetic, read from four
  // textures without a sampler, its constants at ClayMaskConstants' offsets.
  {
    using fable2::native::render::ClayMaskConstants;
    const char* m = kClayMaskPs;
    if (!Compile("clay_mask_ps", m, "ps_5_0")) return 195;
    if (BindPoint("clay_mask_ps", m, "ps_5_0", "Mask") != 0 || BindPoint("clay_mask_ps", m, "ps_5_0", "scene_depth") != 0 ||
        BindPoint("clay_mask_ps", m, "ps_5_0", "near_map") != 1 || BindPoint("clay_mask_ps", m, "ps_5_0", "far_map") != 2 ||
        BindPoint("clay_mask_ps", m, "ps_5_0", "rotation") != 3) return 195;
    const std::string listing = Disassembly("clay_mask_ps", m, "ps_5_0");
    if (listing.empty() || LoopInstructions(listing) != 0 || HasLineWith(listing, "dcl_sampler", "s") ||
        listing.find("sample") != std::string::npos || strstr(m, "Sample") || strstr(m, "SamplerState")) return 196;
    // Loads: the depth once, the tile once, the near map twelve times, the far map four times.
    auto loads = [&](int reg) {
      const std::string needle = ", t" + std::to_string(reg) + ".";
      int n = 0;
      for (size_t at = listing.find("ld_indexable"); at != std::string::npos; at = listing.find("ld_indexable", at + 1)) {
        const size_t end = listing.find('\n', at);
        if (listing.substr(at, end - at).find(needle) != std::string::npos) ++n;
      }
      return n;
    };
    if (loads(0) != 1 || loads(1) != 12 || loads(2) != 4 || loads(3) != 1) {
      std::cerr << "mask shader loads: depth " << loads(0) << ", near " << loads(1) << ", far " << loads(2) << ", tile " << loads(3) << "\n";
      return 196;
    }
    // The constants: the table is the struct, and the compiled buffer is the table (256 bytes).
    const unsigned count = sizeof(kClayMaskFields) / sizeof(kClayMaskFields[0]);
    const struct {
      const char* name;
      size_t offset, bytes;
    } mask_struct[] = {
        {"m_inv0", offsetof(ClayMaskConstants, inv_view_proj), 16},      {"m_inv1", offsetof(ClayMaskConstants, inv_view_proj) + 16, 16},
        {"m_inv2", offsetof(ClayMaskConstants, inv_view_proj) + 32, 16}, {"m_inv3", offsetof(ClayMaskConstants, inv_view_proj) + 48, 16},
        {"m_near0", offsetof(ClayMaskConstants, near_rows), 16},         {"m_near1", offsetof(ClayMaskConstants, near_rows) + 16, 16},
        {"m_near2", offsetof(ClayMaskConstants, near_rows) + 32, 16},    {"m_near3", offsetof(ClayMaskConstants, near_rows) + 48, 16},
        {"m_far0", offsetof(ClayMaskConstants, far_rows), 16},           {"m_far1", offsetof(ClayMaskConstants, far_rows) + 16, 16},
        {"m_far2", offsetof(ClayMaskConstants, far_rows) + 32, 16},      {"m_far3", offsetof(ClayMaskConstants, far_rows) + 48, 16},
        {"m_eye", offsetof(ClayMaskConstants, eye), sizeof(ClayMaskConstants::eye)},
        {"m_radius", offsetof(ClayMaskConstants, radius), sizeof(ClayMaskConstants::radius)},
        {"m_split", offsetof(ClayMaskConstants, split), sizeof(ClayMaskConstants::split)},
        {"m_inv_target", offsetof(ClayMaskConstants, inv_target), sizeof(ClayMaskConstants::inv_target)},
        {"m_tile_scale", offsetof(ClayMaskConstants, tile_scale), sizeof(ClayMaskConstants::tile_scale)},
        {"m_rotate", offsetof(ClayMaskConstants, rotate), sizeof(ClayMaskConstants::rotate)},
        {"m_map_size", offsetof(ClayMaskConstants, map_size), sizeof(ClayMaskConstants::map_size)},
        {"m_pad", offsetof(ClayMaskConstants, pad), sizeof(ClayMaskConstants::pad)},
    };
    if (count != sizeof(mask_struct) / sizeof(mask_struct[0]) || sizeof(ClayMaskConstants) != 256) return 197;
    for (unsigned i = 0; i < count; ++i) {
      if (strcmp(kClayMaskFields[i].name, mask_struct[i].name) || kClayMaskFields[i].offset != mask_struct[i].offset ||
          kClayMaskFields[i].bytes != mask_struct[i].bytes) return 197;
    }
    if (int at = BufferLayoutMismatch("clay_mask_ps", m, "ps_5_0", "Mask", kClayMaskFields, count, 256)) {
      std::cerr << "cbuffer Mask: " << at << "\n";
      return 197;
    }
    {
      std::string swapped = m;
      const std::string good = "float3 m_eye; float m_radius;";
      const size_t at = swapped.find(good);
      if (at == std::string::npos) return 197;
      swapped.replace(at, good.size(), "float m_radius; float3 m_eye;");
      if (BufferLayoutMismatch("swapped_mask_ps", swapped.c_str(), "ps_5_0", "Mask", kClayMaskFields, count, 256) < 10) return 197;
    }
    // The arithmetic, statement for statement (light_formulas.py: mask_0f99, mask_near, mask_far,
    // mask_combine, in the native direction): in the text's order.
    const char* statements[] = {
        "bool mask_number(float x) { return abs(x) < 3.0e38; }",
        // the near map: the rows, the division, the twelve rotated taps, the strict compare, the weight
        "float w = dot(m_near3, P);",
        "float3 p = float3(dot(m_near0, P), dot(m_near1, P), dot(m_near2, P)) / w;",
        "[unroll] for (int i = 0; i < 12; ++i) {",
        "float2 o = float2(rot.x * kMaskTaps[i].x + rot.y * kMaskTaps[i].y, rot.x * kMaskTaps[i].y - rot.y * kMaskTaps[i].x);",
        "float2 at = p.xy + m_radius * o;",
        "float2 texel = number ? clamp(floor(at * m_map_size), 0.0, last) : float2(0.0, 0.0);",
        "float stored = near_map.Load(int3(int2(texel), 0));",
        "lit += (!number || p.z < stored) ? 1.0 : 0.0;",
        "return lit * 0.0833333358168602;",
        // the far map: four texels, lower and higher, inclusive compare, the weights of frac
        "float w = dot(m_far3, P);",
        "float3 p = float3(dot(m_far0, P), dot(m_far1, P), dot(m_far2, P)) / w;",
        "float2 xy = p.xy * m_map_size;",
        "float2 cell = floor(xy);",
        "float2 f = xy - cell;",
        "int2 lo = int2(clamp(cell - 1.0, 0.0, last));",
        "int2 hi = int2(clamp(cell, 0.0, last));",
        "float lit00 = (p.z <= far_map.Load(int3(hi.x, hi.y, 0))) ? 1.0 : 0.0;",
        "float lit10 = (p.z <= far_map.Load(int3(lo.x, hi.y, 0))) ? 1.0 : 0.0;",
        "float lit11 = (p.z <= far_map.Load(int3(lo.x, lo.y, 0))) ? 1.0 : 0.0;",
        "float lit01 = (p.z <= far_map.Load(int3(hi.x, lo.y, 0))) ? 1.0 : 0.0;",
        "return f.x * f.y * lit00 + (1.0 - f.x) * f.y * lit10 + (1.0 - f.x) * (1.0 - f.y) * lit11 + f.x * (1.0 - f.y) * lit01;",
        // the blend and the fade to split.w
        "float w = (m_split.z > 0.0) ? saturate((d - m_split.x) / m_split.z) : ((d > m_split.x) ? 1.0 : 0.0);",
        "float m = near_term + w * (far_term - near_term);",
        "float fade = saturate(0.1 * (d - m_split.y));",
        "return m + fade * (m_split.w - m);",
        // the pixel: the depth at the pixel, the screen point, the world position, the distance, the two
        // predicates, the tile in the game's pixel units
        "uint2 pixel = uint2(pos.xy);",
        "float depth = scene_depth.Load(int3(pixel, 0));",
        "float4 S = float4((2.0 * pixel.x + 1.0) * m_inv_target.x - 1.0, 1.0 - (2.0 * pixel.y + 1.0) * m_inv_target.y, depth, 1.0);",
        "float beyond = m_split.w;",
        "float w = dot(m_inv3, S);",
        "if (!(abs(w) > 0.0 && mask_number(w))) return float4(beyond, beyond, beyond, 1.0);",
        "float3 world = float3(dot(m_inv0, S), dot(m_inv1, S), dot(m_inv2, S)) / w;",
        "float3 to_eye = world - m_eye;",
        "float d = sqrt(dot(to_eye, to_eye));",
        "if (!mask_number(d)) return float4(beyond, beyond, beyond, 1.0);",
        "[branch] if (m_split.x + m_split.z > d) {",
        "float2 rot = float2(1.0, 0.0);",
        "[branch] if (m_rotate != 0) {",
        "uint2 tile = uint2(floor((float2(pixel) + 0.5) * m_tile_scale)) % 32;",
        "rot = 2.0 * rotation.Load(int3(tile, 0)).xy - 1.0;",
        "near_term = MaskNear(P, rot);",
        "[branch] if (d > m_split.x && m_split.y + 10.0 > d) far_term = MaskFar(P);",
        "float m = MaskCombine(d, near_term, far_term);",
        "return float4(m, m, m, 1.0);",
    };
    const char* at = m;
    for (const char* statement : statements) {
      const char* found = strstr(at, statement);
      if (!found) {
        std::cerr << "the mask shader lacks (or has out of order): " << statement << "\n";
        return 198;
      }
      at = found + 1;
    }
    // The compare directions on the native maps and nothing else: one strict compare of the near map,
    // four inclusive ones of the far map, none the other way round; no fade to 1.
    if (Count(m, "p.z < stored") != 1 || Count(m, "p.z <= far_map.Load(") != 4 || strstr(m, "p.z > ") || strstr(m, "p.z >= ") ||
        strstr(m, "p.z <= stored") || strstr(m, "p.z < far_map") || strstr(m, "1.0 - p.z") || strstr(m, "(1.0 - m)")) return 198;
    // The twelve taps are the reference's literals (clay_logic.h, kMaskNearTaps: the same numbers).
    {
      using fable2::native::render::kMaskNearTaps;
      const char* table = strstr(m, "static const float2 kMaskTaps[12] = {");
      if (!table) return 199;
      const char* cursor = table;
      for (unsigned i = 0; i < 12; ++i) {
        cursor = strstr(cursor, "float2(");
        if (!cursor) return 199;
        cursor += strlen("float2(");
        char* end = nullptr;
        const double x = std::strtod(cursor, &end);
        if (!end || *end != ',') return 199;
        const double y = std::strtod(end + 1, &end);
        if (float(x) != kMaskNearTaps[i][0] || float(y) != kMaskNearTaps[i][1]) {
          std::cerr << "mask tap " << i << " is not the reference's\n";
          return 199;
        }
        cursor = end;
      }
      if (fable2::native::render::kMaskTapWeight != float(0.0833333358168602) || fable2::native::render::kMaskFadeSlope != 0.1f ||
          fable2::native::render::kMaskFarReach != 10.0f || fable2::native::render::kMaskTileSize != 32) return 199;
    }
    // Controls: a mask shader with a sampler, and one whose taps are a loop, are reported by the checks above.
    const std::string looped = Changed(m, "[unroll] for (int i = 0; i < 12; ++i) {", "[loop] for (int i = 0; i < int(m_map_size) && i < 12; ++i) {");
    if (looped.empty() || LoopInstructions(Disassembly("looped_mask_ps", looped.c_str(), "ps_5_0")) == 0) return 199;
  }
  // 200 to 204: the fog (fog sub-project, plan Task 4; clay_logic.h, "Native fog").
  {
    // The clay shader `src` without the fog: the text that does not define CLAY_FOG, which is the text
    // the shader had before the fog but for the fog's names, which then do nothing.
    auto without_fog = [](const char* src) { return Changed(src, "#define CLAY_FOG 1\n", ""); };
    // ... and with the frame's flag a constant 0 in the place of the block's word: the program of a
    // frame that has no fog (the switch off, no valid block, no view), with everything it cannot reach
    // folded away by the compiler.
    auto fog_off = [](const std::string& src) {
      std::string text = Changed(src.c_str(), "float3 f_fog_axis; uint f_fog_on;", "float3 f_fog_axis; uint f_fog_on_word;");
      if (text.empty()) return text;
      return Changed(text.c_str(), "float3 light_normalize(float3 v) {", "static const uint f_fog_on = 0;\nfloat3 light_normalize(float3 v) {");
    };
    const struct {
      const char* name;
      const char* src;
    } lit_ps[] = {{"clay_textured_ps", kClayTexturedPs}, {"clay_terrain_layer_ps", kClayTerrainLayerPs}, {"clay_terrain_far_ps", kClayTerrainFarPs}};
    // 200: the fog is in the colour programs of the pixel shaders that draw the scene and nowhere else:
    // the three that light, and since the unlit draws' fog (plan Task 6: cases 210 to 212) the flat
    // shader. The vertex shader, the shadow maps' shader and the three depth-only programs do not define
    // CLAY_FOG, so they are compiled from the text without it: the programs they were.
    for (const char* src : {kClayVs, kClayShadowPs}) {
      if (strstr(src, "#define CLAY_FOG")) return 200;
    }
    if (Count(kClayPs, "#define CLAY_FOG 1\n") != 1) return 200;
    for (const auto& s : lit_ps) {
      if (Count(s.src, "#define CLAY_FOG 1\n") != 1 || without_fog(s.src).empty() || strstr(without_fog(s.src).c_str(), "#define CLAY_FOG")) return 200;
    }
    // 201: fog off is the program without fog. For each of the three shaders and every light_mode: the
    // program with the frame's flag 0 is, line for line, the program compiled from the text without
    // the fog. So a frame without fog runs the instructions of the shader before the fog on its lit
    // pixels too, and what differs in the shipped program is the fog alone, behind the flag.
    // And without the flag folded: no program of a draw without light (bit 2 clear) differs at all.
    // (Restated for plan Task 6: "a draw without light" is here a draw without the unlit fog's bits, which
    // every one of these 128 modes is; a draw with them is fogged, and case 210 holds the same two
    // statements for it: with the flag 0 its program is the unlit program, line for line.)
    int lit_programs = 0;
    for (const auto& s : lit_ps) {
      const std::string plain = without_fog(s.src);
      const std::string off = fog_off(s.src);
      if (plain.empty() || off.empty()) return 201;
      for (unsigned mode = 0; mode < fable2::native::render::kLightModeCount; ++mode) {
        const std::string before = Code(Disassembly("without_fog", WithLightMode(plain.c_str(), mode).c_str(), "ps_5_0"));
        const std::string folded = Code(Disassembly("fog_off", WithLightMode(off.c_str(), mode).c_str(), "ps_5_0"));
        if (before.empty() || folded != before) {
          std::cerr << s.name << " light_mode " << mode << ": the program with the fog's flag 0 is not the program without the fog\n";
          return 201;
        }
        const std::string now = Code(Disassembly(s.name, WithLightMode(s.src, mode).c_str(), "ps_5_0"));
        if (now.empty()) return 201;
        if (!(mode & 4)) {
          if (now != before) {
            std::cerr << s.name << " light_mode " << mode << ": a program without light changed with the fog\n";
            return 201;
          }
        } else {
          // (the lit programs do differ: the comparison above is not empty)
          if (now == before) return 201;
          ++lit_programs;
        }
      }
    }
    if (lit_programs != 3 * 64) return 201;
    // Controls: a fog that does not ask the frame's flag, and one whose position is formed outside the
    // fog's names (so that it stays when they do nothing), are not the program without fog.
    {
      const std::string unflagged = Changed(kClayTerrainFarPs, "o.fogged = f_fog_on != 0 && reach && dD > 0.0;", "o.fogged = reach && dD > 0.0;");
      const std::string plain = without_fog(kClayTerrainFarPs);
      if (unflagged.empty() || fog_off(unflagged).empty()) return 201;
      if (Code(Disassembly("unflagged_fog", WithLightMode(fog_off(unflagged).c_str(), 5).c_str(), "ps_5_0")) ==
          Code(Disassembly("without_fog", WithLightMode(plain.c_str(), 5).c_str(), "ps_5_0"))) return 201;
      const std::string tinted = Changed(kClayTerrainFarPs, "    lin = FogForm(lin, FogCoords(pos), 0.0, 0.0);",
                                         "    lin = FogForm(lin, FogCoords(pos), 0.0, 0.0) * (1.0 + 0.001 * pos.z);");
      if (tinted.empty() || fog_off(tinted).empty()) return 201;
      if (Code(Disassembly("tinted_fog", WithLightMode(fog_off(tinted).c_str(), 5).c_str(), "ps_5_0")) ==
          Code(Disassembly("without_fog", WithLightMode(plain.c_str(), 5).c_str(), "ps_5_0"))) return 201;
    }
    // 202: what the lit programs gained, on the compiled code. They index the frame's block by a
    // computed value (the two tables), test before they do (the fog's reads are inside a branch: a
    // pixel that is not fogged reads no table), call no square root, have no loop, and read no
    // texture or buffer more than before.
    for (const auto& s : lit_ps) {
      const unsigned mode = s.src == kClayTexturedPs ? 6u : 5u;
      const std::string listing = Disassembly(s.name, WithLightMode(s.src, mode).c_str(), "ps_5_0");
      const std::string before = Disassembly("without_fog", WithLightMode(without_fog(s.src).c_str(), mode).c_str(), "ps_5_0");
      bool dynamic = false;
      if (DeclaredRegisters(listing, 2, &dynamic) != kClayFrameSize / 16 || !dynamic) return 202;
      const size_t table = listing.find(" + 14]");  // f_scatter starts at register 14 (byte 224)
      const size_t far_table = listing.find(" + 78]");  // f_transmit at register 78 (byte 1248)
      const size_t test = listing.find("\nif_");
      if (table == std::string::npos || far_table == std::string::npos || test == std::string::npos || test > table || test > far_table) return 202;
      if (HasOp(listing, "sqrt") || LoopInstructions(listing) != 0 || listing.find("ld_structured") != std::string::npos) return 202;
      // The guards as the compiler left them (review of plan Task 4, M1). The first index of a table
      // read is bounded by the float clamp of the texel coordinate (max with 0, min with N - 1, on a
      // saturated product) before it is made an integer: the integer minimum on it is removed as
      // implied; the second index keeps its integer minimum. The distance's bound and the frame's
      // flag are compared in the code.
      if (!HasLineWith(listing, "min", "l(63.000000)") || !HasLineWith(listing, "min", "l(255.000000)") ||
          !HasLineWith(listing, "umin", "l(63)") || !HasLineWith(listing, "umin", "l(255)") ||
          !HasLineWith(listing, "lt", "l(1000000015047466219876688855040.000000)") || !HasLineWith(listing, "ine", "cb2[7].w")) {
        std::cerr << s.name << ": a guard of the fog is not in the compiled program\n";
        return 202;
      }
      // Control: a table read without its float clamp has no such minimum.
      if (s.src == kClayTerrainFarPs) {
        const std::string unclamped = Changed(s.src, "float x = clamp(saturate(u) * 64.0 - 0.5, 0.0, 63.0);", "float x = u * 64.0 - 0.5;");
        if (unclamped.empty() || HasLineWith(Disassembly("unclamped_fog", WithLightMode(unclamped.c_str(), mode).c_str(), "ps_5_0"), "min", "l(63.000000)")) return 202;
      }
      for (int reg = 0; reg < 16; ++reg) {
        if (ReadsResource(listing, reg) != ReadsResource(before, reg)) return 202;
      }
      // The pixel's own depth: the program declares the position's z.
      if (!HasLineWith(listing, "dcl_input_ps_siv", "v0.xyz") && !HasLineWith(listing, "dcl_input_ps_siv", "v0.xyzw")) return 202;
    }
    // 203: the statements, in the text. Each family's fog comes after its lit colour and before c26 and
    // the exposure; a plant and the ground take the ground form, an object, a trunk and a leaf the
    // form their object block names; the far ground is fogged before its cross-fade.
    {
      const char* t = kClayTexturedPs;
      const char* coords = strstr(t, "FogPoint fog = FogCoords(pos);");
      const char* plant = strstr(t, "lit = FogForm(lit, fog, 0.0, 0.0);");
      const char* leaf_light = strstr(t, "lit = LightLeaf(");
      const char* leaf = strstr(t, "lit = FogForm(lit, fog, o_leaf.z, o_leaf.w) * o_c26;");
      const char* object_light = strstr(t, "lit = LightObject(");
      const char* object = leaf ? strstr(leaf + 1, "lit = FogForm(lit, fog, o_leaf.z, o_leaf.w) * o_c26;") : nullptr;
      const char* out = strstr(t, "return float4(clay_lit(lit * f_exposure),");
      const char* mask = strstr(t, "float mask = shadow_mask.SampleLevel(");
      if (!coords || !plant || !leaf_light || !leaf || !object_light || !object || !out || !mask) return 203;
      if (!(mask < coords && coords < plant && plant < leaf_light && leaf_light < leaf && leaf < object_light && object_light < object &&
            object < out)) return 203;
      // (final review, I1) A lit draw whose game shader has no fog: bit 9 marks its fog point unfogged,
      // right after the coordinates and before any family's form; no other shader reads the bit.
      const char* bare = strstr(t, "FogPoint fog = FogCoords(pos);\n"
                                   "      // Bit 9: a lit draw whose own pixel shader in the game has no fog (clay_logic.h, DrawFogOf): none here.\n"
                                   "      if (light_mode & 512) fog.fogged = false;\n");
      if (bare != coords || Count(t, "light_mode & 512") != 1) return 203;
      for (const char* src : {kClayPs, kClayTerrainLayerPs, kClayTerrainFarPs, kClayVs, kClayShadowPs, kClaySkyPs}) {
        if (strstr(src, "light_mode & 512")) return 203;
      }
      if (Count(t, "FogForm(") != 6 || Count(t, "FogCoords(") != 7 || Count(t, "o_c26") != 3) return 203;  // (FogForm: its two definitions, the three lit calls, the unlit tree's; FogCoords: two definitions, the lit one, the four unlit returns'; c26's declaration)
      const char* l = kClayTerrainLayerPs;
      const char* layer_light = strstr(l, "lin = LightTerrainLayer(");
      const char* layer_fog = strstr(l, "lin = FogForm(lin, FogCoords(pos), 0.0, 0.0);");
      const char* layer_out = strstr(l, "return terrain_out(lin, a, pos, ndc);");
      if (!layer_light || !layer_fog || !layer_out || !(layer_light < layer_fog && layer_fog < layer_out) || Count(l, "FogForm(") != 4) return 203;
      const char* f = kClayTerrainFarPs;
      const char* far_light = strstr(f, "lin = LightTerrainFar(");
      const char* far_fog = strstr(f, "lin = FogForm(lin, FogCoords(pos), 0.0, 0.0);");
      const char* fade = strstr(f, "if (t_flags & 1) lin *= terrain_fade_in(world);");
      if (!far_light || !far_fog || !fade || !(far_light < far_fog && far_fog < fade) || Count(f, "FogForm(") != 4) return 203;
    }
    // 204: the forms' numbers are the object block's (kTreeFogForm*), and the tables' sizes the frame's.
    {
      using namespace fable2::native::render;
      if (kTreeFogFormGround != 0.0f || kTreeFogFormTree != 1.0f || kTreeFogFormNone != 2.0f || kFogScatterTexels != 64 ||
          kFogTransmitTexels != 256 || kFogDistance2Max != 1.0e30f) return 204;
      const char* t = kClayTexturedPs;
      if (!strstr(t, "[branch] if (p.fogged && form != 2.0) {") || !strstr(t, "if (form == 1.0) {") ||
          !strstr(t, "float4 f_scatter[64];") || !strstr(t, "float4 f_transmit[256];") ||
          !strstr(t, "bool reach = placed && d2 > 0.0 && d2 < 1.0e30;") ||
          !strstr(t, "bool placed = abs(w) > 0.0 && abs(w) < 3.0e38;")) return 204;
    }
  }
  // 205 to 209: the sky step (fog sub-project, plan Task 5; clay_logic.h, "Native sky").
  {
    using fable2::native::render::ClaySkyConstants;
    const char* s = kClaySkyPs;
    // 205: both shaders compile; the vertex shader puts the triangle at the far depth (z = w = 1: the
    // value the depth is cleared to) and reads nothing but the vertex's number.
    if (!Compile("sky_vs", kSkyVs, "vs_5_0") || !Compile("clay_sky_ps", s, "ps_5_0")) return 205;
    {
      const std::string vs = Disassembly("sky_vs", kSkyVs, "vs_5_0");
      if (vs.empty() || !HasLineWith(vs, "mov", "o0.zw, l(0,0,1.000000,1.000000)") || vs.find("dcl_constantbuffer") != std::string::npos ||
          vs.find("dcl_resource") != std::string::npos || !strstr(kSkyVs, "float2(-1.0, 1.0), 1.0, 1.0);")) return 205;
      // Control: the output mapping's triangle is at depth 0, and the check sees the difference.
      if (HasLineWith(Disassembly("fullscreen_vs", kFullscreenVs, "vs_5_0"), "mov", "o0.zw, l(0,0,1.000000,1.000000)")) return 205;
    }
    // 206: the bindings of the sky's own layout: cbuffer Sky at b0, the frame's block at b2, the two
    // textures at t0 and t1, the four static samplers; the constants at ClaySkyConstants' offsets.
    if (BindPoint("clay_sky_ps", s, "ps_5_0", "Sky") != 0 || BindPoint("clay_sky_ps", s, "ps_5_0", "Frame") != 2 ||
        BindPoint("clay_sky_ps", s, "ps_5_0", "sky13") != 0 || BindPoint("clay_sky_ps", s, "ps_5_0", "sky14") != 1 ||
        BindPoint("clay_sky_ps", s, "ps_5_0", "s_point_wrap") != 0 || BindPoint("clay_sky_ps", s, "ps_5_0", "s_linear_wrap") != 1 ||
        BindPoint("clay_sky_ps", s, "ps_5_0", "s_point_clamp") != 2 || BindPoint("clay_sky_ps", s, "ps_5_0", "s_linear_clamp") != 3) return 206;
    {
      const unsigned count = sizeof(kClaySkyFields) / sizeof(kClaySkyFields[0]);
      const struct {
        const char* name;
        size_t offset, bytes;
      } sky_struct[] = {
          {"s_ray0", offsetof(ClaySkyConstants, ray0), sizeof(ClaySkyConstants::ray0)},
          {"s_c31x", offsetof(ClaySkyConstants, c31x), sizeof(ClaySkyConstants::c31x)},
          {"s_rayx", offsetof(ClaySkyConstants, rayx), sizeof(ClaySkyConstants::rayx)},
          {"s_c46x", offsetof(ClaySkyConstants, c46x), sizeof(ClaySkyConstants::c46x)},
          {"s_rayy", offsetof(ClaySkyConstants, rayy), sizeof(ClaySkyConstants::rayy)},
          {"s_c47x", offsetof(ClaySkyConstants, c47x), sizeof(ClaySkyConstants::c47x)},
          {"s_ext1", offsetof(ClaySkyConstants, ext1), sizeof(ClaySkyConstants::ext1)},
          {"s_c67y", offsetof(ClaySkyConstants, c67y), sizeof(ClaySkyConstants::c67y)},
          {"s_ext2", offsetof(ClaySkyConstants, ext2), sizeof(ClaySkyConstants::ext2)},
          {"s_textures", offsetof(ClaySkyConstants, textures), sizeof(ClaySkyConstants::textures)},
          {"s_fix13", offsetof(ClaySkyConstants, fix13), sizeof(ClaySkyConstants::fix13)},
          {"s_fix14", offsetof(ClaySkyConstants, fix14), sizeof(ClaySkyConstants::fix14)},
          {"s_sampler13", offsetof(ClaySkyConstants, sampler13), sizeof(ClaySkyConstants::sampler13)},
          {"s_sampler14", offsetof(ClaySkyConstants, sampler14), sizeof(ClaySkyConstants::sampler14)},
          {"s_pad", offsetof(ClaySkyConstants, pad), sizeof(ClaySkyConstants::pad)},
      };
      if (count != sizeof(sky_struct) / sizeof(sky_struct[0]) || sizeof(ClaySkyConstants) != 112) return 206;
      for (unsigned i = 0; i < count; ++i) {
        if (strcmp(kClaySkyFields[i].name, sky_struct[i].name) || kClaySkyFields[i].offset != sky_struct[i].offset ||
            kClaySkyFields[i].bytes != sky_struct[i].bytes) return 206;
      }
      if (int at = BufferLayoutMismatch("clay_sky_ps", s, "ps_5_0", "Sky", kClaySkyFields, count, 112)) {
        std::cerr << "cbuffer Sky: " << at << "\n";
        return 206;
      }
      const std::string swapped = Changed(s, "float3 s_ext1; float s_c67y;", "float s_c67y; float3 s_ext1;");
      if (swapped.empty() || BufferLayoutMismatch("swapped_sky_ps", swapped.c_str(), "ps_5_0", "Sky", kClaySkyFields, count, 112) < 7) return 206;
      // The frame's block is the lit shaders' block with the fog's tail, field for field (the sky reads
      // the in-scatter table at byte 224, the axis, the exposure and 1 / the target's size from it).
      const unsigned light = sizeof(kClayFrameFields) / sizeof(kClayFrameFields[0]), tail = sizeof(kClayFrameFogFields) / sizeof(kClayFrameFogFields[0]);
      std::vector<ClayBufferField> frame(kClayFrameFields, kClayFrameFields + light);
      frame.insert(frame.end(), kClayFrameFogFields, kClayFrameFogFields + tail);
      if (int at = BufferLayoutMismatch("clay_sky_ps", s, "ps_5_0", "Frame", frame.data(), unsigned(frame.size()), kClayFrameSize)) {
        std::cerr << "cbuffer Frame of the sky shader: " << at << "\n";
        return 206;
      }
      if (Count(s, "#define CLAY_FOG 1\n") != 1) return 206;
    }
    // 207: the compiled program. It reads the frame's block by a computed index (the in-scatter
    // table at register 14) and not beyond the table (78 registers: the transmittance table is not
    // read); no depth (the position's x and y alone), no buffer, no loop; the two textures only by
    // sample at an explicit level, each with each of the four samplers, all behind the branch on the
    // block's flag. The guards are in the code: the ray's length (two compares, one against 3e38),
    // the power's base, the arctangent's ratio, the table index's float and integer clamps (review of
    // plan Task 4, M1), the output's sign and ceiling.
    {
      const std::string listing = Disassembly("clay_sky_ps", s, "ps_5_0");
      bool dynamic = false;
      if (listing.empty() || DeclaredRegisters(listing, 2, &dynamic) != 78 || !dynamic || DeclaredRegisters(listing, 0) != 7) return 207;
      if (LoopInstructions(listing) != 0 || listing.find("ld_structured") != std::string::npos || listing.find("ld_indexable") != std::string::npos ||
          !HasLineWith(listing, "dcl_input_ps_siv", "v0.xy, position") || HasLineWith(listing, "dcl_input_ps_siv", "v0.xyz")) return 207;
      const size_t branch = listing.find("\nif_nz cb0[4].w");
      if (branch == std::string::npos || listing.find("\nif_") != branch) return 207;
      int samples[2] = {0, 0};
      for (size_t at = listing.find("sample", branch); at != std::string::npos; at = listing.find("sample", at + 1)) {
        const size_t end = listing.find('\n', at);
        const std::string line = listing.substr(at, end - at);
        if (line.rfind("sample_l_indexable", 0) != 0 || line.find("l(0.000000)") == std::string::npos) return 207;
        if (line.find(", t0.") != std::string::npos) ++samples[0];
        if (line.find(", t1.") != std::string::npos) ++samples[1];
      }
      if (samples[0] != 4 || samples[1] != 4 || listing.find("sample_") < branch) return 207;
      const size_t table = listing.find(" + 14]");
      if (table == std::string::npos || table > branch || listing.find(" + 78]") != std::string::npos) return 207;
      if (!HasLineWith(listing, "lt", "l(300000000549775575777803994281145270272.000000)") || !HasLineWith(listing, "rsq", "r0.w") ||
          !HasLineWith(listing, "lt", "l(0.000000), |r0.z|") || !HasLineWith(listing, "max", "l(0.000000)") ||
          !HasLineWith(listing, "min", "l(63.000000)") || !HasLineWith(listing, "umin", "l(63)") ||
          !HasLineWith(listing, "min", "l(64.000000, 64.000000, 64.000000, 0.000000)") ||
          !HasLineWith(listing, "lt", "l(0.000000, 0.000000, 0.000000, 0.000000), r0.xyzx") || !HasLineWith(listing, "dp3_sat", "cb2[7].xyzx")) return 207;
      // The arctangent's ratio (review of plan Task 5, M2): the one division of the program is 1 over
      // the larger of |x| and |y|, the line before it compares that divisor with 0, and the comparison's
      // result is and-ed onto the ratio within the next two lines: at a pole the ratio is 0, not 0 / 0.
      // Where the three lines stand is checked here; that the other guards above feed what they guard
      // is shown by reading the listing (the reviews), not by this test, which finds each instruction
      // anywhere in the program.
      auto ratio_guarded = [](const std::string& text) {
        const size_t div = text.find("\n  div ");
        if (div == std::string::npos || text.find("\n  div ", div + 1) != std::string::npos || text.find("\ndiv ") != std::string::npos) return false;
        const size_t div_end = text.find('\n', div + 1);
        const std::string division = text.substr(div + 1, div_end - div - 1);
        if (division.find("l(1.000000, 1.000000, 1.000000, 1.000000), r") == std::string::npos) return false;
        const std::string divisor = division.substr(division.rfind(", ") + 2);   // "r0.w"
        const size_t before = text.rfind('\n', div - 1);
        const std::string compare = text.substr(before + 1, div - before - 1);
        const size_t lt = compare.find("lt r");
        if (lt == std::string::npos || compare.find("l(0.000000), " + divisor) == std::string::npos) return false;
        const std::string flag = compare.substr(lt + 3, compare.find(',', lt) - lt - 3);   // "r2.w"
        size_t at = div_end;
        for (int line = 0; line < 2 && at != std::string::npos; ++line) {
          const size_t end = text.find('\n', at + 1);
          const std::string next = text.substr(at + 1, end - at - 1);
          if (next.find("and ") != std::string::npos && next.find(flag) != std::string::npos) return true;
          at = end;
        }
        return false;
      };
      if (!ratio_guarded(listing)) {
        std::cerr << "the sky's arctangent ratio is not guarded in the compiled program\n";
        return 207;
      }
      {
        const std::string bare = Changed(s, "float q = (hi > 0.0) ? lo * (1.0 / hi) : 0.0;", "float q = lo * (1.0 / hi);");
        if (bare.empty() || ratio_guarded(Disassembly("bare_ratio_sky", bare.c_str(), "ps_5_0"))) return 207;
      }
      // Controls: a sky that reads its textures outside the flag's branch, one whose ray is normalised
      // without a test, and one whose power has no test of its base are reported by the checks above.
      const std::string unbranched = Changed(s, "[branch] if (s_textures != 0) {", "{");
      if (unbranched.empty() || Disassembly("unbranched_sky", unbranched.c_str(), "ps_5_0").find("\nif_nz cb0[4].w") != std::string::npos) return 207;
      const std::string unguarded = Changed(s, "float3 n = light_normalize(s_ray0 + X * s_rayx + Y * s_rayy);", "float3 n = normalize(s_ray0 + X * s_rayx + Y * s_rayy);");
      if (unguarded.empty() || HasLineWith(Disassembly("unguarded_sky", unguarded.c_str(), "ps_5_0"), "lt", "l(300000000549775575777803994281145270272.000000)")) return 207;
      const std::string unpowered = Changed(s, "float k = (az > 0.0) ? exp2(0.20000000298023224 * log2(az)) : 0.0;", "float k = exp2(0.20000000298023224 * log2(az));");
      if (unpowered.empty() || HasLineWith(Disassembly("unpowered_sky", unpowered.c_str(), "ps_5_0"), "lt", "l(0.000000), |r0.z|")) return 207;
    }
    // 208: the arithmetic, statement for statement (light_formulas.py: sky, sky_latlong, sky_paths,
    // sky_extinction), in the text's order.
    {
      const char* statements[] = {
          "float3 a = abs(n);",
          "float hi = max(a.x, a.y);",
          "float lo = min(a.x, a.y);",
          "float q = (hi > 0.0) ? lo * (1.0 / hi) : 0.0;",
          "float q2 = q * q;",
          "float at = q * (q2 * (q2 * (q2 * (q2 * 0.02083509974181652 + -0.08513300120830536) + 0.18014100193977356) + -0.3302994966506958) + 0.9998660087585449);",
          "if (a.y > a.x) at = at + (-2.0 * at + 1.5707963705062866);",
          "if (n.x < 0.0) at = at + -3.1415927410125732;",
          "if (min(n.x, n.y) < 0.0 && max(n.x, n.y) >= 0.0) at = at - (at + at);",
          "float root = sqrt(abs(1.0 - a.z));",
          "float poly = a.z * (a.z * (a.z * -0.018729299306869507 + 0.07426100224256516) + -0.2121143937110901) + 1.5707287788391113;",
          "float ac = poly * root;",
          "if (n.z < 0.0) ac = ac + (-2.0 * root * poly + 3.1415927410125732);",
          "return float2(0.5 + at * 0.15915963053703308, (1.5707963705062866 - ac) * 0.6366385221481323);",
          "float k = (az > 0.0) ? exp2(0.20000000298023224 * log2(az)) : 0.0;",
          "float h = s_c67y * (1.0499999523162842 - k);",
          "return float2(h + (s_c31x - h) * k, h + (s_c46x - h) * k);",
          "return exp2(-1.4426950216293335 * (l.x * s_ext1 + l.y * s_ext2));",
          "float X = pos.x * (2.0 * f_mask_du) - 1.0;",
          "float Y = 1.0 - pos.y * (2.0 * f_mask_dv);",
          "float3 n = light_normalize(s_ray0 + X * s_rayx + Y * s_rayy);",
          "float3 S = fog_scatter(saturate(dot(n, f_fog_axis)));",
          "float3 E = SkyExtinction(SkyPaths(abs(n.z)));",
          "float3 sky = (1.0 - E) * S;",
          "[branch] if (s_textures != 0) {",
          "float2 uv = SkyLatLong(n);",
          "float4 s13 = sky_sample(sky13, s_sampler13, uv * s_fix13);",
          "float4 s14 = sky_sample(sky14, s_sampler14, uv * s_fix14);",
          "float3 col = s13.rgb * s13.rgb;",
          "col = col + s_c47x * (s14.rgb * s14.rgb - col);",
          "float alpha = s13.a + s_c47x * (s14.a - s13.a);",
          "sky = sky + alpha * (col - sky);",
          "return float4(clay_lit(sky * f_exposure), 1.0);",
      };
      const char* own = strstr(s, "cbuffer Sky : register(b0) {");
      if (!own) return 208;
      const char* at = own;
      for (const char* statement : statements) {
        const char* found = strstr(at, statement);
        if (!found) {
          std::cerr << "the sky shader lacks (or has out of order): " << statement << "\n";
          return 208;
        }
        at = found + 1;
      }
      // No exact arctangent or arcsine, no depth, no implicit level, no discard.
      if (strstr(own, "atan") || strstr(own, "asin") || strstr(own, "acos") || strstr(own, "pos.z") || strstr(own, ".Sample(") ||
          strstr(own, "discard") || Count(own, "SampleLevel(") != 4) return 208;
    }
    // 209: the literals are the mirror's (clay_logic.h: the reference's singles).
    {
      using namespace fable2::native::render;
      const struct {
        const char* text;
        double value;
      } literals[] = {{"0.9998660087585449", kSkyAtan[0]}, {"-0.3302994966506958", kSkyAtan[1]}, {"0.18014100193977356", kSkyAtan[2]},
                      {"-0.08513300120830536", kSkyAtan[3]}, {"0.02083509974181652", kSkyAtan[4]}, {"1.5707287788391113", kSkyAcos[0]},
                      {"-0.2121143937110901", kSkyAcos[1]}, {"0.07426100224256516", kSkyAcos[2]}, {"-0.018729299306869507", kSkyAcos[3]},
                      {"3.1415927410125732", kSkyPi}, {"1.5707963705062866", kSkyHalfPi}, {"0.15915963053703308", kSkyInvTwoPi},
                      {"0.6366385221481323", kSkyTwoOverPi}, {"0.20000000298023224", kSkyPathPower}, {"1.0499999523162842", kSkyPathHorizon},
                      {"1.4426950216293335", kSkyInvLn2}};
      for (const auto& l : literals) {
        if (!strstr(s, l.text) || std::strtod(l.text, nullptr) != l.value || double(float(l.value)) != l.value) {
          std::cerr << "sky literal " << l.text << "\n";
          return 209;
        }
      }
    }
  }
  // 210 to 212: the unlit draws' fog (fog sub-project, plan Task 6; clay_logic.h, "Unlit draws"): a draw
  // without light whose pixel shader has the game's fog block is fogged in a fogged frame, by two bits
  // of light_mode above the light's seven (128: the unlit fog; 256: its in-scatter times the alpha the
  // draw writes). Since the final review (M1, M2): an unlit trunk or leaf by a bit of its own (1024: the
  // trees' form, its scale in Draw's pad2), in the flat and the textured shader; both forms bounded.
  // 213: a lit draw whose game shader has no fog (512; final review, I1).
  {
    using fable2::native::render::kLightModeCount;
    static_assert(fable2::native::render::kLightModeUnlitFog == 128u && fable2::native::render::kLightModeUnlitFogAlpha == 256u &&
                  fable2::native::render::kLightModeLitNoFog == 512u && fable2::native::render::kLightModeUnlitFogTree == 1024u);
    auto fog_off = [](const char* src) {
      std::string text = Changed(src, "float3 f_fog_axis; uint f_fog_on;", "float3 f_fog_axis; uint f_fog_on_word;");
      if (text.empty()) return text;
      return Changed(text.c_str(), "float3 light_normalize(float3 v) {", "static const uint f_fog_on = 0;\nfloat3 light_normalize(float3 v) {");
    };
    const struct {
      const char* name;
      const char* src;
      bool alpha;   // the program reads the second bit (the textured shader: the only one that writes an alpha of its own)
      bool tree;    // the program reads the tree's bit (a trunk or a leaf is a mesh: never a terrain shader's draw)
    } unlit_ps[] = {{"clay_ps", kClayPs, false, true},
                    {"clay_textured_ps", kClayTexturedPs, true, true},
                    {"clay_terrain_layer_ps", kClayTerrainLayerPs, false, false},
                    {"clay_terrain_far_ps", kClayTerrainFarPs, false, false}};
    // 210: fog off: equal. For each of the four shaders that draw colour and every light_mode without
    // the lit bit: the program of a draw with the unlit fog's bits, with the frame's flag 0, is line for
    // line the program of the draw without them, which is the program the draw had before this fog
    // (cases 161 and 166 hold what that one reads: nothing of the frame). So a frame without fog draws
    // every unlit draw with the instructions it had. With the flag the root word it is, the program
    // differs (the comparison is not empty), by the fog alone, behind the flag.
    int fogged_programs = 0;
    for (const auto& s : unlit_ps) {
      const std::string off = fog_off(s.src);
      if (off.empty()) return 210;
      for (unsigned mode = 0; mode < kLightModeCount; ++mode) {
        if (mode & 4) continue;
        const std::string plain = Code(Disassembly(s.name, WithLightMode(s.src, mode).c_str(), "ps_5_0"));
        if (plain.empty()) return 210;
        std::string with[2];
        for (int i = 0; i < 2; ++i) {
          const unsigned bits = i ? 384u : 128u;
          with[i] = Code(Disassembly(s.name, WithLightMode(s.src, mode | bits).c_str(), "ps_5_0"));
          const std::string folded = Code(Disassembly("fog_off", WithLightMode(off.c_str(), mode | bits).c_str(), "ps_5_0"));
          if (with[i].empty() || folded != plain) {
            std::cerr << s.name << " light_mode " << (mode | bits) << ": with the fog's flag 0 the program is not the unlit program\n";
            return 210;
          }
          if (with[i] == plain) return 210;
          ++fogged_programs;
        }
        // (the second bit is read where the draw writes an alpha of its own, and nowhere else)
        if ((with[0] != with[1]) != s.alpha) return 210;
        // The tree's bit: read by the flat and the textured shader, whose program with the flag 0 is
        // again the unlit program; a terrain shader's program does not change with it at all.
        const std::string tree = Code(Disassembly(s.name, WithLightMode(s.src, mode | 1024u).c_str(), "ps_5_0"));
        const std::string tree_folded = Code(Disassembly("fog_off", WithLightMode(off.c_str(), mode | 1024u).c_str(), "ps_5_0"));
        if (tree.empty() || tree_folded != plain) {
          std::cerr << s.name << " light_mode " << (mode | 1024u) << ": with the fog's flag 0 the program is not the unlit program\n";
          return 210;
        }
        if ((tree != plain) != s.tree || (s.tree && (tree == with[0] || tree == with[1]))) return 210;
        if (s.tree) ++fogged_programs;
        // (bit 9 is a lit draw's: without the lit bit it is nothing)
        if (Code(Disassembly(s.name, WithLightMode(s.src, mode | 512u).c_str(), "ps_5_0")) != plain) return 210;
      }
      // The second bit alone is nothing: the program of the mode without either.
      if (Code(Disassembly(s.name, WithLightMode(s.src, 256).c_str(), "ps_5_0")) != Code(Disassembly(s.name, WithLightMode(s.src, 0).c_str(), "ps_5_0"))) return 210;
    }
    if (fogged_programs != 4 * 64 * 2 + 2 * 64) return 210;
    // The vertex shader, the shadow maps' shader and the three depth-only programs read neither bit.
    // (nor the two bits of the final review: 384 + 512 + 1024)
    for (unsigned mode : {0u, 1u, 2u, 5u, 22u, 78u}) {
      if (Code(Disassembly("clay_vs", WithLightMode(kClayVs, mode | 1920u).c_str(), "vs_5_0")) !=
          Code(Disassembly("clay_vs", WithLightMode(kClayVs, mode).c_str(), "vs_5_0"))) return 210;
    }
    for (const char* src : {kClayShadowPs, kClayTexturedDepthPs, kClayTerrainLayerDepthPs, kClayTerrainFarDepthPs}) {
      const std::string with = Code(Disassembly("depth", WithLightMode(src, 1920).c_str(), "ps_5_0"));
      if (with.empty() || with != Code(Disassembly("depth", WithLightMode(src, 0).c_str(), "ps_5_0"))) return 210;
    }
    // Controls: an unlit fog that does not ask the frame's flag, and one outside the fog's names, are
    // not the unlit program with the flag 0.
    {
      const std::string unflagged = Changed(kClayPs, "o.fogged = f_fog_on != 0 && reach && dD > 0.0;", "o.fogged = reach && dD > 0.0;");
      const std::string plain = Code(Disassembly("clay_ps", WithLightMode(kClayPs, 0).c_str(), "ps_5_0"));
      if (unflagged.empty() || fog_off(unflagged.c_str()).empty() ||
          Code(Disassembly("unflagged", WithLightMode(fog_off(unflagged.c_str()).c_str(), 128).c_str(), "ps_5_0")) == plain) return 210;
      const std::string tinted = Changed(kClayPs, "return float4(FogUnlit(clay_linear(base * shade) * f_post, FogCoords(pos), 1.0), 1.0);",
                                         "return float4(FogUnlit(clay_linear(base * shade) * f_post, FogCoords(pos), 1.0) * (1.0 + 0.001 * pos.z), 1.0);");
      if (tinted.empty() || fog_off(tinted.c_str()).empty() ||
          Code(Disassembly("tinted", WithLightMode(fog_off(tinted.c_str()).c_str(), 128).c_str(), "ps_5_0")) == plain) return 210;
    }
    // 211: what a fogged unlit program is, on the compiled code. It declares the frame's whole block,
    // indexed by a computed value (the two tables), reads the fog's flag, the exposure, the eye and the
    // inverse view-projection, tests before it reads a table, keeps the lit programs' clamps and
    // bounds, has no square root and no loop, and reads no texture more than the unlit program: not
    // the screen mask, not the base map. The flat and the textured shader read nothing of the draw's
    // slot (cb1: an unlit draw binds none of its own).
    for (const auto& s : unlit_ps) {
      const bool terrain = s.src == kClayTerrainLayerPs || s.src == kClayTerrainFarPs;
      for (unsigned bits : {128u, 384u, 1024u}) {
        if (bits == 1024u && !s.tree) continue;
        const std::string listing = Disassembly(s.name, WithLightMode(s.src, bits).c_str(), "ps_5_0");
        const std::string plain = Disassembly(s.name, WithLightMode(s.src, 0).c_str(), "ps_5_0");
        bool dynamic = false;
        if (DeclaredRegisters(listing, 2, &dynamic) != kClayFrameSize / 16 || !dynamic) return 211;
        const size_t table = listing.find(" + 14]"), far_table = listing.find(" + 78]"), test = listing.find("\nif_");
        if (table == std::string::npos || far_table == std::string::npos || test == std::string::npos || test > table || test > far_table) return 211;
        if (HasOp(listing, "sqrt") || LoopInstructions(listing) != 0 || listing.find("ld_structured") != std::string::npos) return 211;
        if (!HasLineWith(listing, "min", "l(63.000000)") || !HasLineWith(listing, "min", "l(255.000000)") ||
            !HasLineWith(listing, "umin", "l(63)") || !HasLineWith(listing, "umin", "l(255)") ||
            !HasLineWith(listing, "lt", "l(1000000015047466219876688855040.000000)") || !HasLineWith(listing, "ine", "cb2[7].w")) {
          std::cerr << s.name << ": a guard of the unlit fog is not in the compiled program\n";
          return 211;
        }
        if (ReadsComponent(listing, "cb2[2]", 'w') != (bits != 1024u) || listing.find("cb2[6]") == std::string::npos ||
            listing.find("cb2[13]") == std::string::npos) return 211;
        for (int reg = 0; reg < 16; ++reg) {
          if (ReadsResource(listing, reg) != ReadsResource(plain, reg)) return 211;
        }
        if (ReadsResource(listing, 4) || ReadsResource(listing, 8)) return 211;
        if (!terrain && ReadsBlock(listing, "cb1")) return 211;
        if (!HasLineWith(listing, "dcl_input_ps_siv", "v0.xyz") && !HasLineWith(listing, "dcl_input_ps_siv", "v0.xyzw")) return 211;
        // (none of the light's registers: the sun, the tint's colour, the added light, the floor; of the
        // added light's register it reads the fourth word, the unlit word its colour is multiplied by
        // before its fog)
        if (listing.find("cb2[0]") != std::string::npos || listing.find("cb2[1]") != std::string::npos ||
            ReadsComponent(listing, "cb2[2]", 'x') || ReadsComponent(listing, "cb2[3]", 'x') || !ReadsComponent(listing, "cb2[3]", 'w')) return 211;
        // (final review, M2) The fogged value is bounded as a lit one: not negative, at most 64.
        if (!HasLineWith(listing, "min", "l(64.000000") || HasLineWith(plain, "min", "l(64.000000")) return 211;
        // (final review, M1) The tree's form reads the draw's scale (Draw's pad2: cb0[8].w) and neither of
        // the ground form's amounts (c28.x, c28.y: cb2[9].z, .w) nor the near colour (cb2[8].xyz); the
        // ground form reads the amounts and not the draw's word. The exposure is in the tree's scale
        // already (the pass's product), so its program does not read the frame's.
        const bool tree = bits == 1024u;
        if (ReadsComponent(listing, "cb0[8]", 'w') != tree || ReadsComponent(listing, "cb2[9]", 'z') == tree ||
            ReadsComponent(listing, "cb2[9]", 'w') == tree || ReadsComponent(listing, "cb2[8]", 'x') == tree) {
          std::cerr << s.name << " light_mode " << bits << ": the unlit fog reads another form's constants\n";
          return 211;
        }
      }
    }
    {
      // Control: an unlit fog without the exposure does not read it.
      const std::string bare = Changed(kClayPs, "(f_fog_scatter * f_exposure * scale)", "(f_fog_scatter * scale)");
      if (bare.empty() || ReadsComponent(Disassembly("bare", WithLightMode(bare.c_str(), 128).c_str(), "ps_5_0"), "cb2[2]", 'w')) return 211;
      // Control: an unlit fog without its bound has no such minimum.
      const std::string unbounded = Changed(kClayPs, "    c = clay_lit(c + f_fog_amount * (G - c));", "    c = c + f_fog_amount * (G - c);");
      if (unbounded.empty() || HasLineWith(Disassembly("unbounded", WithLightMode(unbounded.c_str(), 128).c_str(), "ps_5_0"), "min", "l(64.000000")) return 211;
    }
    // 212: the statements, in the text. The unlit form is the ground form with the in-scatter times the
    // exposure and the draw's scale; each unlit return has its fogged twin before it, on the same colour;
    // the scale is the written alpha for the second bit in the textured shader and 1 everywhere else.
    {
      const char* body = strstr(kClayPs, "float3 FogUnlit(float3 c, FogPoint p, float scale) {\n  [branch] if (p.fogged) {\n"
                                         "    float3 S = fog_scatter(p.a);\n    float3 T = fog_transmit(p.s);\n"
                                         "    float3 F = f_fog_near + p.t * (S - f_fog_near);\n"
                                         "    float3 G = c * T + (1.0 - T) * (f_fog_scatter * f_exposure * scale) * F;\n"
                                         "    c = clay_lit(c + f_fog_amount * (G - c));\n  }\n  return c;\n}\n");
      if (!body) return 212;
      // The tree's: the trees' form (FogForm's form 1) with the scale it is given, bounded the same way.
      if (!strstr(kClayPs, "float3 FogUnlitTree(float3 c, FogPoint p, float scale) {\n"
                           "  [branch] if (p.fogged) c = clay_lit(FogForm(c, p, scale, 1.0));\n  return c;\n}\n")) return 212;
      const char* flat_tree = strstr(kClayPs, "if (light_mode & 1024) return float4(FogUnlitTree(clay_linear(base * shade) * f_post, FogCoords(pos), asfloat(pad2)), 1.0);");
      const char* flat_fog = strstr(kClayPs, "if (light_mode & 128) return float4(FogUnlit(clay_linear(base * shade) * f_post, FogCoords(pos), 1.0), 1.0);");
      const char* flat = strstr(kClayPs, "return float4(clay_linear(base * shade) * f_post, 1.0);");
      if (!flat_tree || !flat_fog || !flat || !(flat_tree < flat_fog && flat_fog < flat) || Count(kClayPs, "FogUnlit(") != 3 ||
          Count(kClayPs, "FogUnlitTree(") != 3 || Count(kClayPs, "light_mode &") != 2) return 212;
      const char* t = kClayTexturedPs;
      const char* lit = strstr(t, "return float4(clay_lit(lit * f_exposure),");
      const char* written = strstr(t, "float3 hazed = FogUnlit(clay_linear(t.rgb * (0.75 + 0.25 * facet)) * f_post, FogCoords(pos), (light_mode & 256) ? saturate(a) : 1.0);");
      // (the return right after it: the unlit tree's twin before it has the same statement)
      const char* tex_fog = written ? strstr(written, "return float4(hazed, (alpha_mode & 0x80) ? saturate(a) : 1.0);") : nullptr;
      const char* tex = strstr(t, "return float4(clay_linear(t.rgb * (0.75 + 0.25 * facet)) * f_post, (alpha_mode & 0x80) ? saturate(a) : 1.0);");
      const char* base_fog = strstr(t, "if (light_mode & 128) return float4(FogUnlit(clay_linear(base * (0.35 + 0.65 * facet)) * f_post, FogCoords(pos), 1.0), 1.0);");
      const char* base = strstr(t, "return float4(clay_linear(base * (0.35 + 0.65 * facet)) * f_post, 1.0);");
      if (!lit || !written || !tex_fog || !tex || !base_fog || !base || !(lit < written && written < tex_fog && tex_fog < tex && tex < base_fog && base_fog < base) ||
          Count(t, "FogUnlit(") != 4 || Count(t, "light_mode & 128") != 2 || Count(t, "light_mode & 256") != 1) return 212;
      // (the tree's twins: before the textured return's and before the flat return's, on the same colours)
      const char* tex_tree = strstr(t, "float3 hazed = FogUnlitTree(clay_linear(t.rgb * (0.75 + 0.25 * facet)) * f_post, FogCoords(pos), asfloat(pad2));");
      const char* base_tree = strstr(t, "if (light_mode & 1024) return float4(FogUnlitTree(clay_linear(base * (0.35 + 0.65 * facet)) * f_post, FogCoords(pos), asfloat(pad2)), 1.0);");
      if (!tex_tree || !base_tree || !(lit < tex_tree && tex_tree < written && tex < base_tree && base_tree < base_fog) ||
          Count(t, "FogUnlitTree(") != 4 || Count(t, "light_mode & 1024") != 2 || Count(t, "return float4(hazed, (alpha_mode & 0x80) ? saturate(a) : 1.0);") != 2) return 212;
      for (const char* src : {kClayTerrainLayerPs, kClayTerrainFarPs}) {
        const char* lit_out = strstr(src, "if (light_mode & 4) return float4(clay_lit(colour * f_exposure),");
        const char* fog = strstr(src, "if (light_mode & 128) return float4(FogUnlit(saturate(colour * (shade * shade)) * f_post, FogCoords(pos), 1.0), (alpha_mode & 0x80) ? saturate(a) : 1.0);");
        const char* out = strstr(src, "return float4(saturate(colour * (shade * shade)) * f_post, (alpha_mode & 0x80) ? saturate(a) : 1.0);");
        if (!lit_out || !fog || !out || !(lit_out < fog && fog < out) || Count(src, "FogUnlit(") != 3 || Count(src, "light_mode & 256") != 0 ||
            Count(src, "FogUnlitTree(") != 2 || Count(src, "light_mode & 1024") != 0) return 212;
      }
      // (the vertex shader's text has the function with the shared light text and never calls it)
      if (Count(kClayVs, "FogUnlit(") != 2 || strstr(kClayShadowPs, "FogUnlit(")) return 212;  // (the function and its stub)
    }
    // 213: a lit draw whose own pixel shader in the game has no fog (bit 9, 512; final review, I1): its
    // program is, line for line, the lit program compiled from the text without the fog, for every lit
    // mode of the textured shader (an object's, a trunk's, a plant's, a leaf's): it reads no table and
    // tests no flag of the fog. With the bit clear the same modes are fogged (they differ).
    {
      const std::string plain_src = Changed(kClayTexturedPs, "#define CLAY_FOG 1\n", "");
      if (plain_src.empty()) return 213;
      int bare_programs = 0;
      for (unsigned mode = 0; mode < kLightModeCount; ++mode) {
        if (!(mode & 4)) continue;
        const std::string before = Code(Disassembly("without_fog", WithLightMode(plain_src.c_str(), mode).c_str(), "ps_5_0"));
        const std::string bare = Code(Disassembly("clay_textured_ps", WithLightMode(kClayTexturedPs, mode | 512u).c_str(), "ps_5_0"));
        const std::string fogged = Code(Disassembly("clay_textured_ps", WithLightMode(kClayTexturedPs, mode).c_str(), "ps_5_0"));
        if (before.empty() || bare != before) {
          std::cerr << "clay_textured_ps light_mode " << (mode | 512u) << ": a lit draw without fog is not the lit program without the fog\n";
          return 213;
        }
        if (fogged == bare) return 213;
        ++bare_programs;
      }
      if (bare_programs != 64) return 213;
      // On the compiled code of the bare plant's two modes: no table read, of the frame's block the
      // light's part at most.
      for (unsigned mode : {22u | 512u, 54u | 512u}) {
        const std::string listing = Disassembly("clay_textured_ps", WithLightMode(kClayTexturedPs, mode).c_str(), "ps_5_0");
        bool dynamic = false;
        if (DeclaredRegisters(listing, 2, &dynamic) > kClayFrameLightSize / 16 || dynamic || listing.find(" + 14]") != std::string::npos ||
            listing.find(" + 78]") != std::string::npos) return 213;
      }
      // Control: a bit that does not reach the fog point leaves the fogged program.
      const std::string deaf = Changed(kClayTexturedPs, "      if (light_mode & 512) fog.fogged = false;\n", "");
      if (deaf.empty() || Code(Disassembly("deaf", WithLightMode(deaf.c_str(), 22u | 512u).c_str(), "ps_5_0")) ==
                              Code(Disassembly("without_fog", WithLightMode(plain_src.c_str(), 22u).c_str(), "ps_5_0"))) return 213;
    }
  }
  // 230, 231: the unlit word (the post sub-project's plan Task 5; spec criterion 8's first half). Since the
  // post chain every unlit colour is multiplied by the frame's word f_post (cbuffer Frame, byte 60, where a
  // padding word was), which is 1 in every frame but a post frame. Each clay shader against its own text
  // as it was before (WithoutPost), compiled with light_mode fixed, for every kind of program the modes
  // give (cases 161 and 210: a mode's program is that of a few of its bits) and for the unlit fog's bits:
  //   - with the word the literal 1 the program is the program before, instruction for instruction, in every
  //     mode: a frame without post is drawn by today's arithmetic (and x * 1 is x, bit for bit);
  //   - the vertex shader, the sky's shader and the two terrain shaders' lit programs are the programs
  //     before as they stand (they never read the word); the textured shader's lit programs are too once
  //     `textured` is fixed as well (a lit draw has an albedo: with `textured` a root word its program
  //     holds the branch of a draw without one, which reads the word and which no lit draw takes);
  //   - an unlit program differs from the one before by a read of cb2[3].w and by nothing else of the
  //     frame's block, in one multiply for each colour it can return and no other instruction; with the
  //     unlit fog's bits the compiler shares or folds the multiply among the fogged and the plain return,
  //     so there the count is held from above only.
  {
    const std::string vs_before = WithoutPost(kClayVs), sky_before = WithoutPost(kClaySkyPs);
    if (vs_before.empty() || sky_before.empty()) return 230;
    if (Code(Disassembly("clay_vs", kClayVs, "vs_5_0")) != Code(Disassembly("before", vs_before.c_str(), "vs_5_0")) ||
        Code(Disassembly("clay_sky_ps", kClaySkyPs, "ps_5_0")) != Code(Disassembly("before", sky_before.c_str(), "ps_5_0"))) return 230;
    for (unsigned mode : {0u, 1u, 5u, 6u, 14u, 22u, 54u, 78u}) {
      if (Code(Disassembly("clay_vs", WithLightMode(kClayVs, mode).c_str(), "vs_5_0")) !=
          Code(Disassembly("before", WithLightMode(vs_before.c_str(), mode).c_str(), "vs_5_0"))) return 230;
    }
    // (the shaders that are no scene pixel shader's colour program name no such word)
    if (strstr(kClayShadowPs, "f_post") || strstr(kOutputMapPs, "f_post") || strstr(kClayMaskPs, "f_post") || strstr(kCompositePs, "f_post") ||
        strstr(kClayPostCompositePs, "f_post") || strstr(kClayPostRampPs, "f_post") || strstr(kClayPostDownsamplePs, "f_post") ||
        strstr(kClayPostBrightPs, "f_post") || strstr(kClayPostBlurPs, "f_post")) return 230;
    // `textured` fixed at 1 beside the mode: the program of a draw with an albedo.
    auto with_albedo = [](std::string text) {
      const std::string field = "uint textured;", mode = "static const uint light_mode";
      const size_t at = text.find(field);
      if (at == std::string::npos) return std::string();
      text.replace(at, field.size(), "uint textured_word;");
      const size_t mode_at = text.find(mode);
      if (mode_at == std::string::npos) return std::string();
      text.insert(mode_at, "static const uint textured = 1;\n");
      return text;
    };
    auto lines_with = [](const std::string& code, const char* needle, const char* op, int* with_op) {
      int n = 0;
      size_t at = 0;
      while (at < code.size()) {
        size_t end = code.find('\n', at);
        if (end == std::string::npos) end = code.size();
        const std::string line = code.substr(at, end - at);
        if (needle == nullptr || line.find(needle) != std::string::npos) {
          if (line.compare(0, 4, "dcl_") != 0) {
            ++n;
            if (with_op && line.compare(0, strlen(op), op) == 0) ++*with_op;
          }
        }
        at = end + 1;
      }
      return n;
    };
    const struct {
      const char* name;
      const char* src;
      bool flat, textured;
      int unlit_returns;  // the colours a program without light and without the fog's bits can return
    } scene_ps[] = {{"clay_ps", kClayPs, true, false, 1},
                    {"clay_textured_ps", kClayTexturedPs, false, true, 2},
                    {"clay_terrain_layer_ps", kClayTerrainLayerPs, false, false, 1},
                    {"clay_terrain_far_ps", kClayTerrainFarPs, false, false, 1}};
    int same = 0, changed = 0;
    for (const auto& s : scene_ps) {
      const std::string before = WithoutPost(s.src), one = WithoutPost(s.src, true);
      if (before.empty() || one.empty() || Count(s.src, " * f_post") != (s.flat ? 3 : s.textured ? 6 : 2) || Count(s.src, "f_post") != Count(s.src, " * f_post") + 1) return 230;
      std::vector<unsigned> modes = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 14u, 20u, 22u, 52u, 54u, 68u, 70u, 78u, 127u};
      for (unsigned base : {0u, 1u, 2u, 4u, 5u, 6u, 22u, 54u, 78u}) {
        for (unsigned bits : {128u, 384u, 512u, 1024u}) modes.push_back(base | bits);
      }
      for (unsigned mode : modes) {
        const std::string now = Code(Disassembly(s.name, WithLightMode(s.src, mode).c_str(), "ps_5_0"));
        const std::string was = Code(Disassembly("before", WithLightMode(before.c_str(), mode).c_str(), "ps_5_0"));
        const std::string with_one = Code(Disassembly("one", WithLightMode(one.c_str(), mode).c_str(), "ps_5_0"));
        if (now.empty() || was.empty() || with_one != was) {
          std::cerr << s.name << " light_mode " << mode << ": with the unlit word 1 the program is not the program before it\n";
          return 230;
        }
        const bool lit = !s.flat && (mode & 4) != 0;
        if (lit && !s.textured) {
          if (now != was) {
            std::cerr << s.name << " light_mode " << mode << ": the lit program is not the program before the unlit word\n";
            return 231;
          }
          ++same;
          continue;
        }
        if (lit) {
          const std::string now_albedo = Code(Disassembly(s.name, with_albedo(WithLightMode(s.src, mode)).c_str(), "ps_5_0"));
          const std::string was_albedo = Code(Disassembly("before", with_albedo(WithLightMode(before.c_str(), mode)).c_str(), "ps_5_0"));
          if (now_albedo.empty() || now_albedo != was_albedo || ReadsComponent(now_albedo, "cb2[3]", 'w')) {
            std::cerr << s.name << " light_mode " << mode << ": the lit program of a draw with an albedo is not the program before the unlit word\n";
            return 231;
          }
          ++same;
          // (and with `textured` a root word it differs only by the word's read)
          if (now == was || !ReadsComponent(now, "cb2[3]", 'w') || ReadsComponent(was, "cb2[3]", 'w')) return 231;
          continue;
        }
        // Without light: one multiply by cb2[3].w for each colour the program can return, and no other
        // instruction more or less.
        int muls = 0;
        const int reads = lines_with(now, "cb2[3].w", "mul ", &muls);
        const bool fogged = (mode & (128u | 1024u)) != 0;
        const int more = lines_with(now, nullptr, "", nullptr) - lines_with(was, nullptr, "", nullptr);
        if (now == was || reads == 0 || ReadsComponent(was, "cb2[3]", 'w') || more < 0 || more > reads ||
            (!fogged && (muls != reads || more != reads || reads != s.unlit_returns || ReadsFrameBeyondPost(now)))) {
          std::cerr << s.name << " light_mode " << mode << ": the unlit program differs from the one before by more than the unlit word's "
                    << "multiply (" << reads << " reads, " << muls << " multiplies, " << lines_with(now, nullptr, "", nullptr) << " against "
                    << lines_with(was, nullptr, "", nullptr) << " instructions)\n";
          return 231;
        }
        ++changed;
      }
    }
    std::cout << "unlit word: " << same << " lit programs equal to those before it, " << changed << " unlit programs with its multiply alone\n";
    if (same == 0 || changed == 0) return 231;
    // Control: a word that reached a lit value would be seen (the lit terrain program would not be the one before).
    const std::string leak = Changed(kClayTerrainFarPs, "  if (light_mode & 4) return float4(clay_lit(colour * f_exposure),",
                                     "  if (light_mode & 4) return float4(clay_lit(colour * f_exposure) * f_post,");
    const std::string far_before = WithoutPost(kClayTerrainFarPs);
    if (leak.empty() || Code(Disassembly("leak", WithLightMode(leak.c_str(), 5).c_str(), "ps_5_0")) ==
                            Code(Disassembly("before", WithLightMode(far_before.c_str(), 5).c_str(), "ps_5_0"))) return 231;
  }
  // 232 to 241: the post steps (clay_logic.h, "Native post" and "Native bloom"): the composite and the ramp,
  // drawn in a post frame in place of the output mapping, whose text and program are untouched (cases 65 to
  // 78), and before them, with the bloom, the downsample, the bright pass and the two blurs (plan Task 8).
  {
    using fable2::native::render::ClayPostConstants;
    using fable2::native::render::ClayPostStepConstants;
    const unsigned post_count = sizeof(kClayPostFields) / sizeof(kClayPostFields[0]);
    const unsigned step_count = sizeof(kClayPostStepFields) / sizeof(kClayPostStepFields[0]);
    // The layout: the tables against the C++ structs, and against every compiled shader.
    const ClayBufferField post_struct[] = {
        {"p_c31x", offsetof(ClayPostConstants, composite) + 0, 4},  {"p_c46x", offsetof(ClayPostConstants, composite) + 4, 4},
        {"p_c47x", offsetof(ClayPostConstants, composite) + 8, 4},  {"p_c77x", offsetof(ClayPostConstants, composite) + 12, 4},
        {"p_c77y", offsetof(ClayPostConstants, composite) + 16, 4}, {"p_c78x", offsetof(ClayPostConstants, composite) + 20, 4},
        {"p_scene_max", offsetof(ClayPostConstants, scene_max), sizeof(ClayPostConstants::scene_max)},
        {"p_previous", offsetof(ClayPostConstants, previous), sizeof(ClayPostConstants::previous)},
        {"p_b31x", offsetof(ClayPostConstants, bright) + 0, 4},     {"p_b46x", offsetof(ClayPostConstants, bright) + 4, 4},
        {"p_b47x", offsetof(ClayPostConstants, bright) + 8, 4},     {"p_b77x", offsetof(ClayPostConstants, bright) + 12, 4},
        {"p_b77y", offsetof(ClayPostConstants, bright) + 16, 4},
        {"p_bloom_on", offsetof(ClayPostConstants, bloom_on), sizeof(ClayPostConstants::bloom_on)},
        {"p_sun_on", offsetof(ClayPostConstants, sun_on), sizeof(ClayPostConstants::sun_on)},
        {"p_pad", offsetof(ClayPostConstants, pad), sizeof(ClayPostConstants::pad)},
        {"p_blur", offsetof(ClayPostConstants, blur), sizeof(ClayPostConstants::blur)},
        {"p_radial", offsetof(ClayPostConstants, radial), sizeof(ClayPostConstants::radial)},
        {"p_table", offsetof(ClayPostConstants, table), sizeof(ClayPostConstants::table)},
        {"p_ramp", offsetof(ClayPostConstants, ramp), sizeof(ClayPostConstants::ramp)},
    };
    const ClayBufferField step_struct[] = {
        {"s_texel", offsetof(ClayPostStepConstants, texel), sizeof(ClayPostStepConstants::texel)},
        {"s_levels", offsetof(ClayPostStepConstants, levels), sizeof(ClayPostStepConstants::levels)},
        {"s_offset", offsetof(ClayPostStepConstants, offset), sizeof(ClayPostStepConstants::offset)},
        {"s_dir", offsetof(ClayPostStepConstants, dir), sizeof(ClayPostStepConstants::dir)},
        {"s_last", offsetof(ClayPostStepConstants, last), sizeof(ClayPostStepConstants::last)},
    };
    if (post_count != 20 || sizeof(post_struct) / sizeof(post_struct[0]) != post_count || kClayPostSize != sizeof(ClayPostConstants) ||
        kClayPostSize > fable2::native::render::kClayPostSlots * fable2::native::render::kClayTerrainSlotBytes) return 232;
    for (unsigned i = 0; i < post_count; ++i) {
      if (strcmp(kClayPostFields[i].name, post_struct[i].name) || kClayPostFields[i].offset != post_struct[i].offset ||
          kClayPostFields[i].bytes != post_struct[i].bytes) {
        std::cerr << "kClayPostFields[" << i << "] is not ClayPostConstants' field\n";
        return 232;
      }
    }
    // (the step's words: 8 of them, the layout's root constants at b1)
    if (step_count != 5 || sizeof(step_struct) / sizeof(step_struct[0]) != step_count || kClayPostStepSize != sizeof(ClayPostStepConstants) ||
        kClayPostStepSize != 8 * 4) return 232;
    for (unsigned i = 0; i < step_count; ++i) {
      if (strcmp(kClayPostStepFields[i].name, step_struct[i].name) || kClayPostStepFields[i].offset != step_struct[i].offset ||
          kClayPostStepFields[i].bytes != step_struct[i].bytes) {
        std::cerr << "kClayPostStepFields[" << i << "] is not ClayPostStepConstants' field\n";
        return 232;
      }
    }
    // 233: every step. Its two blocks at b0 and b1 with the tables' layouts; its first texture at t0; the
    // textures it reads and how (t0 by Load, or by the linear sampler s1 in the downsample; the composite's
    // bloom and the bright pass's two sun layers only behind their flag: inside an `if`); no sampler but s1,
    // no loop, nothing of the frame's block, alpha 1. And the write's own rounding (controller ruling 25) as
    // the program's last arithmetic in every step into a UNORM target: the value times the step's level
    // count plus a half, rounded down, divided by the level count (cb1[0].z); never in the downsample, whose
    // target is a float one.
    // (the composite: with the previous frame's share a literal 0, the program of a frame without that
    // blend; the blended form is case 253's. Its text still holds the blended function: four uses of
    // post_level where the other steps have two.)
    const std::string composite_plain = PreviousOff(kClayPostCompositePs);
    if (composite_plain.empty()) return 233;
    const struct {
      const char* name;
      const char* src;
      const char* texture;
      bool unorm;           // the step writes a UNORM target
      int loads0, samples0; // instructions that load / sample t0
      unsigned gated;       // bit r: t<r> is read, and only inside an `if`
      bool sampler;         // the step declares s1
    } steps[] = {{"clay_post_composite_ps", composite_plain.c_str(), "scene_target", true, 1, 0, 1u << 1, true},
                 {"clay_post_ramp_ps", kClayPostRampPs, "front_target", true, 1, 0, 0, false},
                 {"clay_post_downsample_ps", kClayPostDownsamplePs, "scene_target", false, 0, 4, 0, true},
                 {"clay_post_bright_ps", kClayPostBrightPs, "down_target", true, 1, 0, (1u << 1) | (1u << 2), false},
                 {"clay_post_blur_ps", kClayPostBlurPs, "blur_source", true, 13, 0, 0, false}};
    for (const auto& s : steps) {
      if (!Compile(s.name, s.src, "ps_5_0")) return 233;
      if (int at = BufferLayoutMismatch(s.name, s.src, "ps_5_0", "Post", kClayPostFields, post_count, kClayPostSize)) {
        std::cerr << s.name << ": cbuffer Post is not kClayPostFields (" << at << ")\n";
        return 233;
      }
      if (int at = BufferLayoutMismatch(s.name, s.src, "ps_5_0", "Step", kClayPostStepFields, step_count, kClayPostStepSize)) {
        std::cerr << s.name << ": cbuffer Step is not kClayPostStepFields (" << at << ")\n";
        return 233;
      }
      const std::string listing = Disassembly(s.name, s.src, "ps_5_0");
      int loads = 0, samples = 0;
      const int load_depth = TextureReadDepth(listing, "ld_indexable", 0, &loads);
      const int sample_depth = TextureReadDepth(listing, "sample_l", 0, &samples);
      if (listing.empty() || BindPoint(s.name, s.src, "ps_5_0", s.texture) != 0 || BindPoint(s.name, s.src, "ps_5_0", "Post") != 0 ||
          BindPoint(s.name, s.src, "ps_5_0", "Step") != 1 || loads != s.loads0 || samples != s.samples0 ||
          (loads && load_depth != 0) || (samples && sample_depth != 0) || LoopInstructions(listing) != 0 || ReadsBlock(listing, "cb2") ||
          !HasLineWith(listing, "mov", "o0.w, l(1.000000)") || listing.find("dcl_sampler s0") != std::string::npos ||
          (listing.find("dcl_sampler s1") != std::string::npos) != s.sampler || (strstr(s.src, "SamplerState") != nullptr) != s.sampler ||
          (s.sampler && BindPoint(s.name, s.src, "ps_5_0", "linear_clamp") != 1)) {
        std::cerr << s.name << ": the step's bindings or its reads of t0 are not the ones stated (" << loads << " loads, " << samples << " samples)\n";
        return 233;
      }
      for (int reg = 1; reg < 8; ++reg) {
        const bool wanted = (s.gated >> reg) & 1u;
        int n = 0;
        const int ld = TextureReadDepth(listing, "ld_indexable", reg, &n);
        int m = 0;
        const int smp = TextureReadDepth(listing, "sample_l", reg, &m);
        if (ReadsResource(listing, reg) != wanted || (wanted && ((n && ld < 1) || (m && smp < 1) || n + m != 1))) {
          std::cerr << s.name << ": t" << reg << " is read where it must not be, or outside its flag's test\n";
          return 233;
        }
      }
      // The shared functions are the block's, once; the rounding is the last statement of a UNORM step.
      for (const char* shared : {"float post_lum(float3 c) { return dot(c, float3(0.2125, 0.7154, 0.0721)); }",
                                 "float3 post_scene(float3 v) { return (v > 0.0) ? min(v, p_scene_max) : float3(0.0, 0.0, 0.0); }",
                                 "float3 post_unit(float3 v) { return (v > 0.0) ? min(v, 1.0) : float3(0.0, 0.0, 0.0); }",
                                 "float3 post_level(float3 v) { return floor(v * s_levels + 0.5) / s_levels; }"}) {
        if (Count(s.src, shared) != 1) return 233;
      }
      const bool rounds = HasLineWith(listing, "round_ni", "r0.xyz, r0.xyzx") && HasLineWith(listing, "div", "cb1[0].zzzz") && HasLineWith(listing, "div", "o0.xyz") &&
                          HasLineWith(listing, "mad", "cb1[0].zzzz, l(0.500000, 0.500000, 0.500000, 0.000000)");
      if (s.unorm != rounds || s.unorm != (Count(s.src, "  return float4(post_level(") == 1) || Count(s.src, "post_level(") != (s.src == composite_plain.c_str() ? 4 : s.unorm ? 2 : 1) ||
          (!s.unorm && (HasOp(listing, "div") || ReadsComponent(listing, "cb1[0]", 'z')))) {
        std::cerr << s.name << ": the write's own rounding is not where it must be\n";
        return 233;
      }
      // No literal of a size: neither the logical size nor the bloom's is in any step's text.
      for (const char* size : {"1120", "720", "280", "180", "1280"}) {
        if (strstr(s.src, size)) return 233;
      }
    }
    // The composite's statements, in the text (the reference's terms: light_formulas.post_composite), and
    // the table read at the NEAREST texel (controller ruling 19).
    {
      const char* c = kClayPostCompositePs;
      const char* statements[] = {
          "  uint i = min(uint(512.0 * saturate(u) + 0.5), 511u);\n  return p_table[i >> 2][i & 3];",
          "  return (b > 0.0 && b < 3.0e38) ? exp2(exponent * log2(b)) : float3(0.0, 0.0, 0.0);",
          "  float3 s = post_scene(scene);\n  float L = post_lum(s);\n  float e = p_c77y * post_table(p_c46x * L);\n"
          "  float3 c = saturate(e * (L + saturate(p_c47x * L) * (s - L)));\n  float3 b = saturate(bloom * p_c31x);\n"
          "  float3 scr = c + b * (1.0 - c);\n  float3 t = scr + p_c77x * (scr - post_lum(scr));\n  return saturate(post_pow(t, p_c78x));",
          "  float3 scene = scene_target.Load(int3(pos.xy, 0)).rgb;\n  float3 bloom = float3(0.0, 0.0, 0.0);\n"
          "  if (p_bloom_on != 0) bloom = bloom_target.SampleLevel(linear_clamp, (floor(pos.xy) + s_offset) * s_texel, 0.0).rgb;\n"
          "  return float4(post_level(PostComposite(scene, bloom)), 1.0);",
      };
      for (const char* statement : statements) {
        if (!strstr(c, statement)) {
          std::cerr << "the composite lacks: " << statement << "\n";
          return 234;
        }
      }
      // On the compiled code: the scene's rule (a comparison with 0 and a mask, the minimum with the
      // block's clamp), the saturated coordinate times 512 plus a half made an unsigned integer and
      // bounded at 511 before it indexes the table (register 9 on: byte 144), the power's two tests, and
      // the clamp before the rounding. The block is declared to the table's end and no further: the ramp is
      // not read. The bloom: its flag's test (cb0[3].y), and inside it one sample of t1 with s1 at the
      // pixel's floor plus the step's offset (cb1[0].w) times the step's texel size (cb1[0].xy).
      const std::string plain = PreviousOff(c);  // (the frame without the previous-frame blend: case 253 has the other)
      if (plain.empty()) return 234;
      const std::string listing = Disassembly("clay_post_composite_ps", plain.c_str(), "ps_5_0");
      bool dynamic = false;
      const size_t load = listing.find("ld_indexable"), scale = listing.find("l(512.000000), l(0.500000)"), to_uint = listing.find("ftou");
      const size_t bound = listing.find("umin"), table = listing.find(" + 9]"), round = listing.rfind("round_ni"), flag = listing.find("if_nz");
      const size_t sample = listing.find("sample_l");
      if (DeclaredRegisters(listing, 0, &dynamic) != 144 / 16 + 128 || !dynamic || DeclaredRegisters(listing, 1) != 1 || load == std::string::npos ||
          scale == std::string::npos || to_uint == std::string::npos || bound == std::string::npos || table == std::string::npos ||
          round == std::string::npos || flag == std::string::npos || sample == std::string::npos ||
          !(scale < to_uint && to_uint < bound && bound < table && table < round && flag < sample && sample < round)) {
        std::cerr << "the composite's compiled program lacks a guard or has them out of order\n";
        return 234;
      }
      if (!HasLineWith(listing, "min", "cb0[1].zzzz") || !HasLineWith(listing, "umin", "l(511)") || !HasLineWith(listing, "mul_sat", "cb0[0].yzyy") ||
          !HasLineWith(listing, "lt", "l(300000000549775575777803994281145270272.000000") ||
          !HasLineWith(listing, "mad", "l(512.000000), l(0.500000)") || !HasOp(listing, "log") || !HasOp(listing, "exp") || HasOp(listing, "sqrt") ||
          Count(listing.c_str(), "ftou") != 1 || Count(listing.c_str(), " + 9]") != 1 || !HasLineWith(listing, "if_nz", "cb0[3].y") ||
          !HasLineWith(listing, "sample_l_indexable(texture2d)(float,float,float,float)", "t1.xyzw, s1, l(0.000000)") || !ReadsComponent(listing, "cb1[0]", 'w') || !ReadsComponent(listing, "cb1[0]", 'x') ||
          !ReadsComponent(listing, "cb1[0]", 'y') || !HasOp(listing, "round_ni") || Count(listing.c_str(), "sample_l") != 1) return 234;
      // Controls: without the index's bound, without the scene's clamp, with the floor's addressing and
      // without the coordinate's saturate, each is seen.
      const std::string unbounded = Changed(plain.c_str(), "min(uint(512.0 * saturate(u) + 0.5), 511u)", "uint(512.0 * saturate(u) + 0.5)");
      const std::string unclamped = Changed(plain.c_str(), "(v > 0.0) ? min(v, p_scene_max) : float3(0.0, 0.0, 0.0)", "v");
      const std::string floored = Changed(plain.c_str(), "512.0 * saturate(u) + 0.5", "512.0 * saturate(u)");
      const std::string loose = Changed(plain.c_str(), "512.0 * saturate(u) + 0.5", "512.0 * u + 0.5");
      if (unbounded.empty() || unclamped.empty() || floored.empty() || loose.empty()) return 235;
      const std::string unbounded_l = Disassembly("unbounded", unbounded.c_str(), "ps_5_0"), unclamped_l = Disassembly("unclamped", unclamped.c_str(), "ps_5_0");
      const std::string floored_l = Disassembly("floored", floored.c_str(), "ps_5_0"), loose_l = Disassembly("loose", loose.c_str(), "ps_5_0");
      if (unbounded_l.empty() || HasOp(unbounded_l, "umin") || unclamped_l.empty() || HasLineWith(unclamped_l, "min", "cb0[1].zzzz") ||
          floored_l.empty() || HasLineWith(floored_l, "mad", "l(512.000000), l(0.500000)") || loose_l.empty() ||
          HasLineWith(loose_l, "mul_sat", "cb0[0].yzyy")) return 235;
    }
    // The ramp: the front target's value saturated (a value that is no number is 0), times 255 plus a
    // half, made an unsigned integer: at most 255, which the compiler takes from the saturate (it drops
    // the written minimum as implied, as it does the fog tables' first index), and the block is declared
    // to the ramp's last entry (register 137 on: byte 2,192; 256 entries). The entry is then rounded to the
    // mapped target's own level by the step.
    {
      const char* r = kClayPostRampPs;
      if (!strstr(r, "uint post_ramp_index(float v) { return min(uint(saturate(v) * 255.0 + 0.5), 255u); }") ||
          !strstr(r, "  float3 v = front_target.Load(int3(pos.xy, 0)).rgb;\n"
                     "  float3 mapped = float3(p_ramp[post_ramp_index(v.r)].r, p_ramp[post_ramp_index(v.g)].g, p_ramp[post_ramp_index(v.b)].b);\n"
                     "  return float4(post_level(saturate(mapped)), 1.0);")) return 236;
      const std::string listing = Disassembly("clay_post_ramp_ps", r, "ps_5_0");
      bool dynamic = false;
      const size_t load = listing.find("ld_indexable"), sat = listing.find("mov_sat"), scale = listing.find("l(255.000000, 255.000000, 255.000000, 0.000000), l(0.500000, 0.500000, 0.500000, 0.000000)");
      const size_t to_uint = listing.find("ftou"), entry = listing.find(" + 137]"), round = listing.find("round_ni");
      if (DeclaredRegisters(listing, 0, &dynamic) != 2192 / 16 + 256 || !dynamic || load == std::string::npos || sat == std::string::npos ||
          scale == std::string::npos || to_uint == std::string::npos || entry == std::string::npos || round == std::string::npos ||
          !(load < sat && sat < scale && scale < to_uint && to_uint < entry && entry < round) || Count(listing.c_str(), " + 137]") != 3 ||
          !HasLineWith(listing, "mov_sat", "cb0[r0.x + 137].x") || !HasLineWith(listing, "mov_sat", "cb0[r0.y + 137].y") ||
          !HasLineWith(listing, "mov_sat", "cb0[r0.z + 137].z")) {
        std::cerr << "the ramp's compiled program lacks its saturate or reads another entry\n";
        return 236;
      }
      // Control: without the saturate the written minimum is what bounds the index, and it is compiled.
      const std::string loose = Changed(r, "min(uint(saturate(v) * 255.0 + 0.5), 255u)", "min(uint(v * 255.0 + 0.5), 255u)");
      const std::string loose_l = Disassembly("loose_ramp", loose.c_str(), "ps_5_0");
      if (loose.empty() || loose_l.empty() || !HasLineWith(loose_l, "umin", "l(255")) return 236;
    }
    // 237: with the bloom's flag 0 the picture is plan Task 5's (spec criterion 15: bloom off is 10a's
    // picture), shown on the programs as Task 5 showed "post off" (cases 230, 231). The composite with the
    // flag the literal 0 reads no second texture, no sampler and nothing of the step but its level count;
    // and with the write's rounding taken out as well it is, instruction for instruction, the program of
    // Task 5's text (kPostCompositeBefore). So is the ramp's. What the two programs do more than Task 5's
    // is the rounding alone (ruling 25): three instructions each, the last three (the bounds below take 3 to 4 and 3 to 6).
    {
      auto bloom_off = [](std::string text) {
        const std::string field = "uint p_bloom_on;";
        const size_t at = text.find(field);
        if (at == std::string::npos) return std::string();
        text.replace(at, field.size(), "uint p_bloom_word;");
        const size_t block_end = text.find("float post_lum(");
        if (block_end == std::string::npos) return std::string();
        text.insert(block_end, "static const uint p_bloom_on = 0;\n");
        return text;
      };
      const std::string off = bloom_off(PreviousOff(kClayPostCompositePs));  // (and no previous-frame blend: case 253)
      const std::string off_plain = Changed(off.c_str(), "return float4(post_level(PostComposite(scene, bloom)), 1.0);", "return float4(PostComposite(scene, bloom), 1.0);");
      const std::string ramp_plain = Changed(kClayPostRampPs, "return float4(post_level(saturate(mapped)), 1.0);", "return float4(mapped, 1.0);");
      if (off.empty() || off_plain.empty() || ramp_plain.empty()) return 237;
      const std::string now_l = Disassembly("composite", PreviousOff(kClayPostCompositePs).c_str(), "ps_5_0"), off_l = Disassembly("bloom_off", off.c_str(), "ps_5_0");
      const std::string off_plain_l = Disassembly("bloom_off_plain", off_plain.c_str(), "ps_5_0");
      const std::string before_l = Disassembly("composite_before", kPostCompositeBefore, "ps_5_0");
      const std::string ramp_l = Disassembly("ramp", kClayPostRampPs, "ps_5_0"), ramp_plain_l = Disassembly("ramp_plain", ramp_plain.c_str(), "ps_5_0");
      const std::string ramp_before_l = Disassembly("ramp_before", kPostRampBefore, "ps_5_0");
      if (now_l.empty() || off_l.empty() || off_plain_l.empty() || before_l.empty() || ramp_l.empty() || ramp_plain_l.empty() || ramp_before_l.empty()) return 237;
      if (Code(off_plain_l) != Code(before_l)) {
        std::cerr << "the composite with the bloom's flag 0 and without the write's rounding is not plan Task 5's program\n";
        return 237;
      }
      if (Code(ramp_plain_l) != Code(ramp_before_l)) {
        std::cerr << "the ramp without the write's rounding is not plan Task 5's program\n";
        return 237;
      }
      // (the flag 0: no second texture, no sampler, no test; of the step's words the level count alone)
      if (ReadsResource(off_l, 1) || off_l.find("sample") != std::string::npos || off_l.find("dcl_sampler") != std::string::npos ||
          HasOp(off_l, "if_nz") || ReadsComponent(off_l, "cb1[0]", 'x') || ReadsComponent(off_l, "cb1[0]", 'y') || ReadsComponent(off_l, "cb1[0]", 'w') ||
          !ReadsComponent(off_l, "cb1[0]", 'z') || ReadsBlock(off_plain_l, "cb1") || ReadsBlock(ramp_plain_l, "cb1")) return 237;
      const int more_composite = Instructions(off_l) - Instructions(before_l), more_ramp = Instructions(ramp_l) - Instructions(ramp_before_l);
      std::cout << "bloom off: the composite and the ramp are plan Task 5's programs but for the write's rounding (" << more_composite << " and "
                << more_ramp << " instructions more); with the bloom's flag a root word the composite has " << Instructions(now_l) - Instructions(off_l)
                << " more\n";
      if (more_composite < 3 || more_composite > 4 || more_ramp < 3 || more_ramp > 6 || Instructions(now_l) <= Instructions(off_l)) return 237;
      // Control: a bloom that reached the picture with the flag 0 would be seen.
      const std::string leak = Changed(off.c_str(), "if (p_bloom_on != 0) bloom =", "bloom =");
      if (leak.empty() || !ReadsResource(Disassembly("leak", leak.c_str(), "ps_5_0"), 1)) return 237;
    }
    // 238: the downsample. Four linear samples of the scene at P + (2.5, 2.5), (2.5, 0.5), (0.5, 0.5),
    // (0.5, 2.5) texels with P = 4 * (the pixel's floor + the step's offset), the coordinates made with the
    // step's texel size; each sample through the scene's rule (the comparison with 0, the minimum with the
    // block's clamp: four of each); a quarter of the sum; nothing of the block but the clamp.
    {
      const char* d = kClayPostDownsamplePs;
      if (!strstr(d, "  return post_scene(scene_target.SampleLevel(linear_clamp, (p + offset) * s_texel, 0.0).rgb);") ||
          !strstr(d, "  float2 p = 4.0 * (floor(pos.xy) + s_offset);\n"
                     "  float3 sum = post_tap(p, float2(2.5, 2.5)) + post_tap(p, float2(2.5, 0.5)) + post_tap(p, float2(0.5, 0.5)) + post_tap(p, float2(0.5, 2.5));\n"
                     "  return float4(0.25 * sum, 1.0);")) return 238;
      const std::string listing = Disassembly("clay_post_downsample_ps", d, "ps_5_0");
      int mins = 0, lts = 0;
      {
        size_t at = 0;
        while (at < listing.size()) {
          size_t end = listing.find('\n', at);
          if (end == std::string::npos) end = listing.size();
          const size_t first = listing.find_first_not_of(" \t", at);
          if (first < end) {
            const std::string line = listing.substr(first, end - first);
            if (line.compare(0, 4, "min ") == 0 && line.find("cb0[1].zzzz") != std::string::npos) ++mins;
            if (line.compare(0, 3, "lt ") == 0 && line.find("l(0.000000, 0.000000, 0.000000, 0.000000)") != std::string::npos) ++lts;
          }
          at = end + 1;
        }
      }
      if (listing.empty() || mins != 4 || lts != 4 || Count(listing.c_str(), "sample_l") != 4 || HasOp(listing, "div") ||
          DeclaredRegisters(listing, 0) != 2 || DeclaredRegisters(listing, 1) != 1 || !ReadsComponent(listing, "cb1[0]", 'x') ||
          !ReadsComponent(listing, "cb1[0]", 'y') || !ReadsComponent(listing, "cb1[0]", 'w') || listing.find("2.500000") == std::string::npos ||
          listing.find("l(4.000000") == std::string::npos || listing.find("0.250000") == std::string::npos) {
        std::cerr << "the downsample's compiled program is not four clamped samples (" << mins << " minimums, " << lts << " comparisons)\n";
        return 238;
      }
      // Controls: a sample taken as it is has no clamp; a literal texel size reads no step word for it.
      const std::string raw = Changed(d, "return post_scene(scene_target.SampleLevel(linear_clamp, (p + offset) * s_texel, 0.0).rgb);",
                                      "return scene_target.SampleLevel(linear_clamp, (p + offset) * s_texel, 0.0).rgb;");
      const std::string sized = Changed(d, "(p + offset) * s_texel", "(p + offset) * float2(1.0 / 1120.0, 1.0 / 720.0)");
      const std::string raw_l = Disassembly("raw", raw.c_str(), "ps_5_0"), sized_l = Disassembly("sized", sized.c_str(), "ps_5_0");
      if (raw.empty() || sized.empty() || raw_l.empty() || sized_l.empty() || HasLineWith(raw_l, "min", "cb0[1].zzzz") ||
          ReadsComponent(sized_l, "cb1[0]", 'x')) return 238;
    }
    // 239: the bright pass. The downsample's texel through the scene's rule; the gain behind a test of
    // L > 0 (the game's shader multiplies 0 by 1 / 0 there); the sun's two layers only behind their flag
    // (cb0[3].z), each one Load; the division by c47.x; then the target's clamp (a value that is no number
    // is 0) and the rounding to its 1,023 levels.
    {
      const char* b = kClayPostBrightPs;
      if (!strstr(b, "  int3 at = int3(pos.xy, 0);\n  float3 d = post_scene(down_target.Load(at).rgb);\n  float L = post_lum(d);\n"
                     "  float gain = (L > 0.0) ? saturate(L * p_b77y - p_b77x) / L * p_b31x : 0.0;\n  float3 v = d * gain;\n"
                     "  if (p_sun_on != 0) v += sun_tf1.Load(at).rgb * 6.0 * p_b46x + sun_tf2.Load(at).rgb;\n"
                     "  return float4(post_level(post_unit(v / p_b47x)), 1.0);")) return 239;
      const std::string listing = Disassembly("clay_post_bright_ps", b, "ps_5_0");
      const size_t load = listing.find("ld_indexable"), clamp = listing.find("cb0[1].zzzz"), flag = listing.find("if_nz"), round = listing.rfind("round_ni");
      if (listing.empty() || load == std::string::npos || clamp == std::string::npos || flag == std::string::npos || round == std::string::npos ||
          !(load < clamp && clamp < flag && flag < round) || !HasLineWith(listing, "if_nz", "cb0[3].z") || !HasLineWith(listing, "min", "cb0[1].zzzz") ||
          !HasLineWith(listing, "lt", "l(0.000000)") || !ReadsComponent(listing, "cb0[2]", 'z') || !ReadsComponent(listing, "cb0[2]", 'w') ||
          !ReadsComponent(listing, "cb0[3]", 'x') || !ReadsComponent(listing, "cb0[2]", 'x') || !ReadsComponent(listing, "cb0[2]", 'y') ||
          listing.find("6.000000") == std::string::npos || !HasLineWith(listing, "min", "l(1.000000, 1.000000, 1.000000, 0.000000)") ||
          DeclaredRegisters(listing, 0) != 4 || ReadsComponent(listing, "cb0[3]", 'y')) {
        std::cerr << "the bright pass's compiled program lacks a guard or reads another register\n";
        return 239;
      }
      // Controls: without the test of L the gain is a bare division; without the target's clamp the value
      // is rounded as it is; with the sun's layers outside their flag they are read in every frame.
      const std::string bare = Changed(b, "(L > 0.0) ? saturate(L * p_b77y - p_b77x) / L * p_b31x : 0.0", "saturate(L * p_b77y - p_b77x) / L * p_b31x");
      const std::string open = Changed(b, "post_level(post_unit(v / p_b47x))", "post_level(v / p_b47x)");
      const std::string always = Changed(b, "if (p_sun_on != 0) v +=", "v +=");
      const std::string bare_l = Disassembly("bare", bare.c_str(), "ps_5_0"), open_l = Disassembly("open", open.c_str(), "ps_5_0");
      const std::string always_l = Disassembly("always", always.c_str(), "ps_5_0");
      if (bare.empty() || open.empty() || always.empty() || bare_l.empty() || open_l.empty() || always_l.empty() ||
          Instructions(bare_l) >= Instructions(listing) || HasLineWith(open_l, "min", "l(1.000000, 1.000000, 1.000000, 0.000000)") ||
          TextureReadDepth(always_l, "ld_indexable", 1) != 0 || HasOp(always_l, "if_nz")) return 239;
    }
    // 240: the blur, one program for both directions. Thirteen Loads of t0, every coordinate clamped
    // between 0 and the step's last texel (cb1[1].zw) after the tap's offset times the step's direction
    // (cb1[1].xy); the weights from the block's rows 4 to 7 (byte 64 on), chosen by the direction; the
    // target's clamp and the rounding. Nothing of the block but the weights.
    {
      const char* b = kClayPostBlurPs;
      if (!strstr(b, "  return w * blur_source.Load(int3(clamp(at + k * s_dir, int2(0, 0), s_last), 0)).rgb;") ||
          !strstr(b, "  bool vertical = s_dir.y != 0;\n  float4 w0 = vertical ? p_blur[2] : p_blur[0];\n  float4 w1 = vertical ? p_blur[3] : p_blur[1];\n"
                     "  float3 sum = post_blur_tap(at, 0, w0.x);\n") ||
          !strstr(b, "  return float4(post_level(post_unit(sum)), 1.0);") || Count(b, "post_blur_tap(at, ") != 13) return 240;
      for (int k = 1; k <= 6; ++k) {
        const char* weight[] = {"", "w0.y", "w0.z", "w0.w", "w1.x", "w1.y", "w1.z"};
        const std::string pair = "  sum += post_blur_tap(at, -" + std::to_string(k) + ", " + weight[k] + ") + post_blur_tap(at, " + std::to_string(k) + ", " +
                                 weight[k] + ");\n";
        if (!strstr(b, pair.c_str())) return 240;
      }
      const std::string listing = Disassembly("clay_post_blur_ps", b, "ps_5_0");
      int imax = 0, imin = 0;
      {
        size_t at = 0;
        while (at < listing.size()) {
          size_t end = listing.find('\n', at);
          if (end == std::string::npos) end = listing.size();
          const size_t first = listing.find_first_not_of(" \t", at);
          if (first < end) {
            // (the compiler bounds two taps in one instruction where it can: four components are two taps)
            const std::string line = listing.substr(first, end - first);
            const int taps = line.find(".xyzw, ") != std::string::npos ? 2 : 1;
            if (line.compare(0, 5, "imax ") == 0 && line.find("l(0, 0, 0, 0)") != std::string::npos) imax += taps;
            if (line.compare(0, 5, "imin ") == 0 && line.find("cb1[1].zw") != std::string::npos) imin += taps;
          }
          at = end + 1;
        }
      }
      if (listing.empty() || Count(listing.c_str(), "ld_indexable") != 13 || imax != 13 || imin != 13 || DeclaredRegisters(listing, 0) != 8 ||
          DeclaredRegisters(listing, 1) != 2 || !ReadsComponent(listing, "cb0[4]", 'x') || !ReadsComponent(listing, "cb0[6]", 'x') || !ReadsComponent(listing, "cb0[5]", 'z') || !ReadsComponent(listing, "cb0[7]", 'z') || !ReadsComponent(listing, "cb1[1]", 'x') ||
          !ReadsComponent(listing, "cb1[1]", 'y') || !HasLineWith(listing, "min", "l(1.000000, 1.000000, 1.000000, 0.000000)") ||
          listing.find("sample") != std::string::npos) {
        std::cerr << "the blur's compiled program is not thirteen clamped loads (" << imax << " lower and " << imin << " upper bounds)\n";
        return 240;
      }
      for (int reg = 0; reg < 4; ++reg) {
        for (char c : {'x', 'y', 'z', 'w'}) {
          if (ReadsComponent(listing, ("cb0[" + std::to_string(reg) + "]").c_str(), c)) return 240;
        }
      }
      // Control: a tap that is not clamped has no bound.
      const std::string free_tap = Changed(b, "clamp(at + k * s_dir, int2(0, 0), s_last)", "at + k * s_dir");
      const std::string free_l = Disassembly("free_tap", free_tap.c_str(), "ps_5_0");
      if (free_tap.empty() || free_l.empty() || HasOp(free_l, "imin") || HasOp(free_l, "imax")) return 240;
    }
  }
  // 241 (post sub-project, Task 8b): the base map's gamma sign. Every terrain row fetches tf3 with the
  // sign fields 3, 3, 3, 0, so the game's baked term is the stored texel through the console's piecewise
  // curve; the two terrain shaders take the base map's r, g, b through base_linear where the record's
  // word t_base_gamma (cbuffer Terrain, byte 188) has the component's bit. Held here: the curve's
  // statements with their literals (xenos.cpp, PWLGammaToLinear), that the lit programs read the word and
  // truncate and the programs without light do neither (they read no base map), and that the word decides
  // (the program without the selection does not read it).
  {
    for (const char* src : {kClayTerrainLayerPs, kClayTerrainFarPs}) {
      if (!strstr(src, "  g = saturate(g);\n  bool3 high = g >= 96.0 / 255.0;\n") ||
          !strstr(src, "  float3 scale = high ? ((g >= 192.0 / 255.0) ? 8.0 / 1024.0 : 4.0 / 1024.0) : ((g >= 64.0 / 255.0) ? 2.0 / 1024.0 : 1.0 / 1024.0);\n") ||
          !strstr(src, "  float3 offset = high ? ((g >= 192.0 / 255.0) ? -1024.0 : -256.0) : ((g >= 64.0 / 255.0) ? -64.0 : 0.0);\n") ||
          !strstr(src, "  float3 l = g * (261120.0 * scale) + offset;\n  return (l + trunc(l * scale)) / 1023.0;\n") ||
          !strstr(src, "  float3 l = base_linear(t.rgb);\n") || Count(src, "base_linear(") != 2 || 261120.0 != 255.0 * 1024.0) return 241;
      const std::string lit = Disassembly("gamma_lit", WithLightMode(src, 5).c_str(), "ps_5_0");
      const std::string unlit = Disassembly("gamma_unlit", WithLightMode(src, 0).c_str(), "ps_5_0");
      if (lit.empty() || unlit.empty() || !ReadsComponent(lit, "cb1[11]", 'w') || ReadsComponent(unlit, "cb1[11]", 'w') ||
          !HasOp(lit, "round_z") || HasOp(unlit, "round_z")) {
        std::cerr << "the base map's gamma word: the lit program does not read it, or the program without light does\n";
        return 241;
      }
      // Control: without the selection the lit program is another one and reads no such word.
      const std::string plain = Changed(src, "  return float4((t_base_gamma & 1) ? l.r : t.r, (t_base_gamma & 2) ? l.g : t.g, (t_base_gamma & 4) ? l.b : t.b, t.a);",
                                        "  return t;");
      const std::string plain_l = plain.empty() ? std::string() : Disassembly("gamma_plain", WithLightMode(plain.c_str(), 5).c_str(), "ps_5_0");
      if (plain_l.empty() || ReadsComponent(plain_l, "cb1[11]", 'w') || HasOp(plain_l, "round_z") || Code(plain_l) == Code(lit)) return 241;
    }
    bool named = false;
    for (const ClayTerrainField& f : kClayTerrainFields) named = named || (!strcmp(f.name, "t_base_gamma") && f.offset == 188 && f.bytes == 4);
    if (!named) return 241;
  }
  // 242 to 246: the quad step (sub-project 10, plan Task 13; clay_logic.h, "Native furniture").
  {
    using fable2::native::render::ClayQuadConstants;
    // 242: both programs compile.
    if (!Compile("clay_quad_vs", kClayQuadVs, "vs_5_0") || !Compile("clay_quad_ps", kClayQuadPs, "ps_5_0")) return 242;
    // 243: cbuffer Quad at b0 in both stages, at the table's offsets, which are ClayQuadConstants'; the
    // texture at t0, the four static samplers at s0..s3.
    {
      const unsigned count = sizeof(kClayQuadFields) / sizeof(kClayQuadFields[0]);
      const struct {
        const char* name;
        size_t offset, bytes;
      } quad_struct[] = {
          {"q_corner", offsetof(ClayQuadConstants, corner), sizeof(ClayQuadConstants::corner)},
          {"q_coord", offsetof(ClayQuadConstants, coord), sizeof(ClayQuadConstants::coord)},
          {"q_view_proj", offsetof(ClayQuadConstants, view_proj), sizeof(ClayQuadConstants::view_proj)},
          {"q_reg", offsetof(ClayQuadConstants, reg), sizeof(ClayQuadConstants::reg)},
          {"q_kind", offsetof(ClayQuadConstants, kind), sizeof(ClayQuadConstants::kind)},
          {"q_sampler", offsetof(ClayQuadConstants, sampler), sizeof(ClayQuadConstants::sampler)},
          {"q_alpha_test", offsetof(ClayQuadConstants, alpha_test), sizeof(ClayQuadConstants::alpha_test)},
          {"q_alpha_ref", offsetof(ClayQuadConstants, alpha_ref), sizeof(ClayQuadConstants::alpha_ref)},
          {"q_fix", offsetof(ClayQuadConstants, fix), sizeof(ClayQuadConstants::fix)},
          {"q_scene_max", offsetof(ClayQuadConstants, scene_max), sizeof(ClayQuadConstants::scene_max)},
          {"q_pad", offsetof(ClayQuadConstants, pad), sizeof(ClayQuadConstants::pad)},
      };
      if (count != sizeof(quad_struct) / sizeof(quad_struct[0]) || sizeof(ClayQuadConstants) != kClayQuadSize ||
          kClayQuadSize > fable2::native::render::kClayQuadSlots * fable2::native::render::kClayTerrainSlotBytes) return 243;
      for (unsigned i = 0; i < count; ++i) {
        if (strcmp(kClayQuadFields[i].name, quad_struct[i].name) || kClayQuadFields[i].offset != quad_struct[i].offset ||
            kClayQuadFields[i].bytes != quad_struct[i].bytes) {
          std::cerr << "kClayQuadFields[" << i << "] is not ClayQuadConstants' field\n";
          return 243;
        }
      }
      for (const char* stage : {"vs_5_0", "ps_5_0"}) {
        const char* src = stage[0] == 'v' ? kClayQuadVs : kClayQuadPs;
        if (int at = BufferLayoutMismatch("clay_quad", src, stage, "Quad", kClayQuadFields, count, kClayQuadSize)) {
          std::cerr << "cbuffer Quad (" << stage << "): " << at << "\n";
          return 243;
        }
        if (BindPoint("clay_quad", src, stage, "Quad") != 0) return 243;
      }
      // Control: two fields exchanged are seen.
      const std::string swapped = Changed(kClayQuadPs, "uint q_kind; uint q_sampler;", "uint q_sampler; uint q_kind;");
      if (swapped.empty() || BufferLayoutMismatch("swapped_quad_ps", swapped.c_str(), "ps_5_0", "Quad", kClayQuadFields, count, kClayQuadSize) == 0) return 243;
      if (BindPoint("clay_quad_ps", kClayQuadPs, "ps_5_0", "quad_tex") != 0 || BindPoint("clay_quad_ps", kClayQuadPs, "ps_5_0", "q_point_wrap") != 0 ||
          BindPoint("clay_quad_ps", kClayQuadPs, "ps_5_0", "q_linear_wrap") != 1 || BindPoint("clay_quad_ps", kClayQuadPs, "ps_5_0", "q_point_clamp") != 2 ||
          BindPoint("clay_quad_ps", kClayQuadPs, "ps_5_0", "q_linear_clamp") != 3) return 243;
    }
    // 244: the vertex shader: the game's six indices, the corner through the four rows (four dp4 against
    // cb0[8..11], the matrix at byte 128), its coordinate from cb0[4..7]; it reads no texture and no buffer
    // but its block, and the vertex's number is bounded before it indexes.
    {
      const std::string vs = Disassembly("clay_quad_vs", kClayQuadVs, "vs_5_0");
      if (vs.empty() || !strstr(kClayQuadVs, "static const uint order[6] = {0, 1, 2, 1, 3, 2};") ||
          !strstr(kClayQuadVs, "uint c = order[min(id, 5u)];") ||
          !strstr(kClayQuadVs, "o.pos = float4(dot(q_view_proj[0], p), dot(q_view_proj[1], p), dot(q_view_proj[2], p), dot(q_view_proj[3], p));") ||
          !strstr(kClayQuadVs, "float4 p = float4(q_corner[c].xyz, 1.0);") || !strstr(kClayQuadVs, "o.uv = q_coord[c].xy;")) return 244;
      if (vs.find("dcl_resource") != std::string::npos || vs.find("dcl_sampler") != std::string::npos || LoopInstructions(vs) != 0 ||
          !HasOp(vs, "umin")) return 244;
      for (const char* row : {"cb0[8]", "cb0[9]", "cb0[10]", "cb0[11]"}) {
        if (vs.find(row) == std::string::npos) return 244;
      }
      // (the registers and the state words of the block are the pixel stage's: not read here)
      for (const char* other : {"cb0[12]", "cb0[13]", "cb0[24]", "cb0[25]"}) {
        if (vs.find(other) != std::string::npos) return 244;
      }
      // Control: without the bound the index is the vertex's number as it comes.
      const std::string unbounded = Changed(kClayQuadVs, "order[min(id, 5u)]", "order[id]");
      if (unbounded.empty() || HasOp(Disassembly("unbounded_quad_vs", unbounded.c_str(), "vs_5_0"), "umin")) return 244;
    }
    // 245: the pixel shader's statements, and in its compiled code: one texture, sampled with its own
    // gradients under each of the four samplers (four sample_d, no other read of it), one discard, the
    // kind's registers alone (cb0[12] and cb0[13]: q_reg[0] and q_reg[1]), the clamp against the block's
    // q_scene_max (cb0[25].z), no loop.
    {
      const char* s = kClayQuadPs;
      if (!strstr(s, "  float2 at = uv * q_fix;\n  float4 t = quad_sample(at, ddx(at), ddy(at));\n") ||
          !strstr(s, "  t = (abs(t) < 3.0e38) ? t : float4(0.0, 0.0, 0.0, 0.0);\n") ||
          !strstr(s, "  if (q_kind == 1) {\n    rgb = t.rgb * q_reg[1].rgb * t.a * q_reg[0].w;\n    a = q_reg[1].a * t.a;\n  } else if (q_kind == 3) {\n"
                     "    rgb = t.rgb * q_reg[0].rgb;\n    a = 1.0;\n  }\n") ||
          !strstr(s, "  if (q_alpha_test != 0 && !(a > q_alpha_ref)) discard;\n") ||
          !strstr(s, "  rgb = (rgb > 0.0) ? min(rgb, q_scene_max) : float3(0.0, 0.0, 0.0);\n  return float4(rgb, saturate(a));\n")) return 245;
      const std::string ps = Disassembly("clay_quad_ps", s, "ps_5_0");
      if (ps.empty() || Count(ps.c_str(), "sample_d") != 4 || Count(ps.c_str(), "discard") != 1 || LoopInstructions(ps) != 0) return 245;
      if (Count(ps.c_str(), "dcl_resource_texture2d") != 1 || Count(ps.c_str(), "dcl_sampler") != 4) return 245;
      if (!ReadsComponent(ps, "cb0[12]", 'w') || !ReadsComponent(ps, "cb0[13]", 'w') || !ReadsComponent(ps, "cb0[25]", 'z') ||
          !ReadsComponent(ps, "cb0[24]", 'w') || !ReadsComponent(ps, "cb0[24]", 'z')) return 245;
      // (the corners, the matrix and the registers past the two are not the pixel stage's)
      for (const char* other : {"cb0[0]", "cb0[8]", "cb0[14]", "cb0[23]"}) {
        if (ps.find(other) != std::string::npos) return 245;
      }
      // The numbers of the two kinds are the capture's kinds plus one.
      static_assert(unsigned(fable2::native::capture::PostQuadKind::kMoon) + 1 == 1 && unsigned(fable2::native::capture::PostQuadKind::kGlare) + 1 == 3);
      // Controls: without the test there is no discard; without the clamp the block's bound is not read;
      // without the texel's guard the program is another one.
      const std::string untested = Changed(s, "  if (q_alpha_test != 0 && !(a > q_alpha_ref)) discard;\n", "");
      const std::string unclamped = Changed(s, "  rgb = (rgb > 0.0) ? min(rgb, q_scene_max) : float3(0.0, 0.0, 0.0);\n", "");
      const std::string unguarded = Changed(s, "  t = (abs(t) < 3.0e38) ? t : float4(0.0, 0.0, 0.0, 0.0);\n", "");
      if (untested.empty() || unclamped.empty() || unguarded.empty()) return 245;
      if (Count(Disassembly("untested_quad_ps", untested.c_str(), "ps_5_0").c_str(), "discard") != 0 ||
          ReadsComponent(Disassembly("unclamped_quad_ps", unclamped.c_str(), "ps_5_0"), "cb0[25]", 'z') ||
          Code(Disassembly("unguarded_quad_ps", unguarded.c_str(), "ps_5_0")) == Code(ps)) return 245;
    }
    // 246: the sun's switch off is the picture of before, program for program: the quad step's text is a
    // text of its own (no program that draws the scene or a post step holds a word of it, and the quad's
    // block is no other program's), so every other program is compiled from the text it had.
    for (const char* src : {kClayVs, kClayPs, kClayTexturedPs, kClayTerrainLayerPs, kClayTerrainFarPs, kClayShadowPs, kSkyVs, kClaySkyPs,
                            kClayMaskPs, kOutputMapPs, kClayPostCompositePs, kClayPostRampPs, kClayPostDownsamplePs, kClayPostBrightPs,
                            kClayPostBlurPs, kFullscreenVs}) {
      if (strstr(src, "q_") || strstr(src, "Quad") || strstr(src, "quad")) return 246;
    }
  }
  // 247 to 252: the sun layer (sub-project 10, plan Task 14; clay_logic.h, "Native sun layer").
  {
    using fable2::native::render::ClaySunQuadConstants;
    const unsigned post_fields = sizeof(kClayPostFields) / sizeof(kClayPostFields[0]);
    const unsigned step_fields = sizeof(kClayPostStepFields) / sizeof(kClayPostStepFields[0]);
    // 247: the six programs compile.
    if (!Compile("clay_sun_depth_ps", kClaySunDepthPs, "ps_5_0") || !Compile("clay_sun_reduce_ps", kClaySunReducePs, "ps_5_0") ||
        !Compile("clay_sun_radial_ps", kClaySunRadialPs, "ps_5_0") || !Compile("clay_sun_quad_vs", kClaySunQuadVs, "vs_5_0") ||
        !Compile("clay_sun_quad_ps", kClaySunQuadPs, "ps_5_0") || !Compile("clay_sun_occluder_ps", kClaySunOccluderPs, "ps_5_0")) return 247;
    // 248: the blocks. cbuffer SunQuad at b0 in the three quad programs, at the table's offsets, which are
    // ClaySunQuadConstants' and, field for field, the scene quads' (the same two slots of a record); the
    // texture at t0, the four static samplers at s0..s3. The three steps under the post layout: cbuffer
    // Step at b1 with the post steps' table; cbuffer Post at b0 in the one that reads it (the radial blur).
    {
      const unsigned count = sizeof(kClaySunQuadFields) / sizeof(kClaySunQuadFields[0]);
      const struct {
        const char* name;
        size_t offset, bytes;
      } sun_struct[] = {
          {"u_corner", offsetof(ClaySunQuadConstants, corner), sizeof(ClaySunQuadConstants::corner)},
          {"u_coord", offsetof(ClaySunQuadConstants, coord), sizeof(ClaySunQuadConstants::coord)},
          {"u_view_proj", offsetof(ClaySunQuadConstants, view_proj), sizeof(ClaySunQuadConstants::view_proj)},
          {"u_reg", offsetof(ClaySunQuadConstants, reg), sizeof(ClaySunQuadConstants::reg)},
          {"u_kind", offsetof(ClaySunQuadConstants, kind), sizeof(ClaySunQuadConstants::kind)},
          {"u_sampler", offsetof(ClaySunQuadConstants, sampler), sizeof(ClaySunQuadConstants::sampler)},
          {"u_alpha_test", offsetof(ClaySunQuadConstants, alpha_test), sizeof(ClaySunQuadConstants::alpha_test)},
          {"u_alpha_ref", offsetof(ClaySunQuadConstants, alpha_ref), sizeof(ClaySunQuadConstants::alpha_ref)},
          {"u_fix", offsetof(ClaySunQuadConstants, fix), sizeof(ClaySunQuadConstants::fix)},
          {"u_levels", offsetof(ClaySunQuadConstants, levels), sizeof(ClaySunQuadConstants::levels)},
          {"u_pad", offsetof(ClaySunQuadConstants, pad), sizeof(ClaySunQuadConstants::pad)},
      };
      const unsigned quad_count = sizeof(kClayQuadFields) / sizeof(kClayQuadFields[0]);
      if (count != sizeof(sun_struct) / sizeof(sun_struct[0]) || count != quad_count || sizeof(ClaySunQuadConstants) != kClaySunQuadSize ||
          kClaySunQuadSize != kClayQuadSize) return 248;
      for (unsigned i = 0; i < count; ++i) {
        if (strcmp(kClaySunQuadFields[i].name, sun_struct[i].name) || kClaySunQuadFields[i].offset != sun_struct[i].offset ||
            kClaySunQuadFields[i].bytes != sun_struct[i].bytes || kClaySunQuadFields[i].offset != kClayQuadFields[i].offset ||
            kClaySunQuadFields[i].bytes != kClayQuadFields[i].bytes) {
          std::cerr << "kClaySunQuadFields[" << i << "] is not ClaySunQuadConstants' field\n";
          return 248;
        }
      }
      const struct {
        const char* name;
        const char* src;
        const char* stage;
      } quads[] = {{"clay_sun_quad_vs", kClaySunQuadVs, "vs_5_0"}, {"clay_sun_quad_ps", kClaySunQuadPs, "ps_5_0"},
                   {"clay_sun_occluder_ps", kClaySunOccluderPs, "ps_5_0"}};
      for (const auto& q : quads) {
        if (int at = BufferLayoutMismatch(q.name, q.src, q.stage, "SunQuad", kClaySunQuadFields, count, kClaySunQuadSize)) {
          std::cerr << "cbuffer SunQuad (" << q.name << "): " << at << "\n";
          return 248;
        }
        if (BindPoint(q.name, q.src, q.stage, "SunQuad") != 0) return 248;
        if (q.stage[0] == 'p' && (BindPoint(q.name, q.src, q.stage, "quad_tex") != 0 || BindPoint(q.name, q.src, q.stage, "u_point_wrap") != 0 ||
                                  BindPoint(q.name, q.src, q.stage, "u_linear_wrap") != 1 || BindPoint(q.name, q.src, q.stage, "u_point_clamp") != 2 ||
                                  BindPoint(q.name, q.src, q.stage, "u_linear_clamp") != 3)) return 248;
      }
      const std::string swapped = Changed(kClaySunQuadPs, "float2 u_fix; float u_levels;", "float u_levels; float2 u_fix;");
      if (swapped.empty() || BufferLayoutMismatch("swapped_sun_quad_ps", swapped.c_str(), "ps_5_0", "SunQuad", kClaySunQuadFields, count, kClaySunQuadSize) == 0) return 248;
      for (const char* src : {kClaySunDepthPs, kClaySunReducePs, kClaySunRadialPs}) {
        if (int at = BufferLayoutMismatch("clay_sun_step", src, "ps_5_0", "Step", kClayPostStepFields, step_fields, kClayPostStepSize)) {
          std::cerr << "a sun step's cbuffer Step is not kClayPostStepFields (" << at << ")\n";
          return 248;
        }
        if (BindPoint("clay_sun_step", src, "ps_5_0", "Step") != 1) return 248;
      }
      if (int at = BufferLayoutMismatch("clay_sun_radial_ps", kClaySunRadialPs, "ps_5_0", "Post", kClayPostFields, post_fields, kClayPostSize)) {
        std::cerr << "the radial blur's cbuffer Post is not kClayPostFields (" << at << ")\n";
        return 248;
      }
      if (BindPoint("clay_sun_radial_ps", kClaySunRadialPs, "ps_5_0", "Post") != 0) return 248;
    }
    // 249: the layer's depth step. One Load of t0 at the pixel's position times the step's ratio, bounded by
    // its last texel (cb1[1]: s_dir and s_last; no size in the text), written as the depth, clamped; no
    // colour output, no sampler, nothing of the frame's or the post's block.
    {
      const char* d = kClaySunDepthPs;
      if (!strstr(d, "float main(float4 pos : SV_Position) : SV_Depth {\n  int2 at = min(int2(pos.xy) * s_dir, s_last);\n"
                     "  float d = sun_depth_source.Load(int3(at, 0)).r;\n  return (d > 0.0) ? min(d, 1.0) : 0.0;\n}")) return 249;
      const std::string listing = Disassembly("clay_sun_depth_ps", d, "ps_5_0");
      int loads = 0;
      const int depth = TextureReadDepth(listing, "ld_indexable", 0, &loads);
      if (listing.empty() || loads != 1 || depth != 0 || listing.find("oDepth") == std::string::npos || listing.find("dcl_output o0") != std::string::npos ||
          listing.find("dcl_sampler") != std::string::npos || ReadsBlock(listing, "cb0") || ReadsBlock(listing, "cb2") ||
          !ReadsComponent(listing, "cb1[1]", 'x') || !ReadsComponent(listing, "cb1[1]", 'z') || !HasOp(listing, "imin") || !HasOp(listing, "imul") ||
          LoopInstructions(listing) != 0 || BindPoint("clay_sun_depth_ps", d, "ps_5_0", "sun_depth_source") != 0) return 249;
      // Controls: without the bound no minimum is taken; a literal ratio reads nothing of the step's direction.
      const std::string unbounded = Changed(d, "min(int2(pos.xy) * s_dir, s_last)", "int2(pos.xy) * s_dir");
      const std::string literal = Changed(d, "int2(pos.xy) * s_dir", "int2(pos.xy) * 2");
      if (unbounded.empty() || literal.empty() || HasOp(Disassembly("unbounded_sun_depth", unbounded.c_str(), "ps_5_0"), "imin") ||
          ReadsComponent(Disassembly("literal_sun_depth", literal.c_str(), "ps_5_0"), "cb1[1]", 'x')) return 249;
      // No size of a target is in any of the layer's texts (a number standing alone: not digits of a literal).
      auto holds_number = [](const char* src, const char* number) {
        const size_t n = strlen(number);
        for (const char* at = strstr(src, number); at; at = strstr(at + 1, number)) {
          const bool before = at != src && (isdigit(static_cast<unsigned char>(at[-1])) || at[-1] == '.');
          const bool after = isdigit(static_cast<unsigned char>(at[n])) != 0;
          if (!before && !after) return true;
        }
        return false;
      };
      if (!holds_number("x * 560;", "560") || holds_number("0.1666666716337204", "720") || holds_number("15600", "560")) return 249;
      for (const char* size : {"1120", "720", "560", "360", "280", "180"}) {
        for (const char* src : {kClaySunDepthPs, kClaySunReducePs, kClaySunRadialPs, kClaySunQuadVs, kClaySunQuadPs, kClaySunOccluderPs}) {
          if (holds_number(src, size)) return 249;
        }
      }
    }
    // 250: the reduction and the radial blur. The reduction: one linear sample of t0 (s1) at the corner the
    // block's middle texels share, the ratio and the texel size from the step's words, the target's clamp
    // and the write's own rounding. The radial blur: one Load of t0 in a loop the compiler keeps, which
    // ends at fourteen (ige against 14), each tap's place bounded on both sides before it is made an integer
    // (a comparison against 0 and a minimum against the last texel, both before the ftoi), the registers
    // from the block's p_radial (cb0[8]) alone, the clamp and the rounding.
    {
      const char* r = kClaySunReducePs;
      if (!strstr(r, "  float2 ratio = float2(s_dir);\n  float3 v = reduce_source.SampleLevel(linear_clamp, (ratio * floor(pos.xy) + 0.5 * ratio) * s_texel, 0.0).rgb;\n"
                     "  return float4(post_level(post_unit(v)), 1.0);\n")) return 250;
      const std::string reduce = Disassembly("clay_sun_reduce_ps", r, "ps_5_0");
      int samples = 0;
      TextureReadDepth(reduce, "sample_l", 0, &samples);
      if (reduce.empty() || samples != 1 || LoopInstructions(reduce) != 0 || ReadsBlock(reduce, "cb0") || ReadsBlock(reduce, "cb2") ||
          !ReadsComponent(reduce, "cb1[1]", 'x') || !ReadsComponent(reduce, "cb1[0]", 'x') || !HasLineWith(reduce, "mov", "o0.w, l(1.000000)") ||
          !HasLineWith(reduce, "div", "cb1[0].zzzz") || !HasOp(reduce, "round_ni") || BindPoint("clay_sun_reduce_ps", r, "ps_5_0", "linear_clamp") != 1 ||
          BindPoint("clay_sun_reduce_ps", r, "ps_5_0", "reduce_source") != 0) return 250;
      const char* b = kClaySunRadialPs;
      if (!strstr(b, "  float2 uv = floor(pos.xy) * s_texel;\n  float2 centre = p_radial.xy;\n"
                     "  float s = (p_radial.w - 1.0) * (4.0 / 7.0) + 1.0;\n  float g = (p_radial.z - 1.0) * (4.0 / 7.0) + 1.0;\n") ||
          !strstr(b, "  for (int k = 0; k < 14; ++k) {\n    float2 p = (centre + (uv - centre) * sk) * size + 0.5;\n"
                     "    p = (p > 0.0) ? min(p, last) : float2(0.0, 0.0);\n"
                     "    sum += w * post_unit(radial_source.Load(int3(int2(p), 0)).rgb);\n    sk *= s;\n    w *= g;\n  }\n"
                     "  return float4(post_level(post_unit(sum * (4.0 / 7.0))), 1.0);\n") ||
          !strstr(b, "  float sk = 1.0;\n  float w = 0.5;\n")) return 250;
      const std::string radial = Disassembly("clay_sun_radial_ps", b, "ps_5_0");
      int loads = 0;
      const int depth = TextureReadDepth(radial, "ld_indexable", 0, &loads);
      if (radial.empty() || loads != 1 || depth != 0 || LoopInstructions(radial) == 0 || !HasLineWith(radial, "ige", "l(14)") ||
          !HasLineWith(radial, "min", "r0.xxxy") || !HasOp(radial, "ftoi") || radial.find("dcl_sampler") != std::string::npos ||
          ReadsBlock(radial, "cb2") || !ReadsComponent(radial, "cb0[8]", 'x') || !ReadsComponent(radial, "cb0[8]", 'y') ||
          !ReadsComponent(radial, "cb0[8]", 'z') || !ReadsComponent(radial, "cb0[8]", 'w') || !HasLineWith(radial, "mov", "o0.w, l(1.000000)") ||
          !HasLineWith(radial, "div", "cb1[0].zzzz") || !HasOp(radial, "round_ni") || BindPoint("clay_sun_radial_ps", b, "ps_5_0", "radial_source") != 0) {
        std::cerr << "the radial blur's compiled code: " << loads << " loads of t0\n";
        return 250;
      }
      // (nothing of the block but the radial registers: not the composite's, the bright pass's, the table or the ramp)
      for (const char* other : {"cb0[0]", "cb0[1]", "cb0[3]", "cb0[4]", "cb0[9]", "cb0[137]"}) {
        if (radial.find(other) != std::string::npos) return 250;
      }
      // Controls: without the tap's bound the program is another one; with thirteen taps the loop ends at thirteen.
      const std::string unbounded = Changed(b, "    p = (p > 0.0) ? min(p, last) : float2(0.0, 0.0);\n", "");
      const std::string thirteen = Changed(b, "k < 14", "k < 13");
      if (unbounded.empty() || thirteen.empty() || Code(Disassembly("unbounded_sun_radial", unbounded.c_str(), "ps_5_0")) == Code(radial)) return 250;
      const std::string fewer = Disassembly("thirteen_sun_radial", thirteen.c_str(), "ps_5_0");
      if (HasLineWith(fewer, "ige", "l(14)") || !HasLineWith(fewer, "ige", "l(13)")) return 250;
    }
    // 251: the layer quads. The vertex shader: the game's six indices, the corner through the four rows, its
    // coordinate and its own position passed on. The pixel shader: the four kinds' statements with the
    // shaders' own literals, one texture under the four samplers with its own gradients, the alpha test's
    // discard, the clamp at 1 and the write's own rounding with the block's level count (cb0[25].z). The
    // occluder's form: no output at all, the kind's four registers, the fade's two literals, the discard.
    {
      const char* v = kClaySunQuadVs;
      if (!strstr(v, "static const uint order[6] = {0, 1, 2, 1, 3, 2};") || !strstr(v, "uint c = order[min(id, 5u)];") ||
          !strstr(v, "o.pos = float4(dot(u_view_proj[0], p), dot(u_view_proj[1], p), dot(u_view_proj[2], p), dot(u_view_proj[3], p));") ||
          !strstr(v, "float4 p = float4(u_corner[c].xyz, 1.0);") || !strstr(v, "o.uv = u_coord[c].xy;") || !strstr(v, "o.world = u_corner[c].xyz;")) return 251;
      const std::string vs = Disassembly("clay_sun_quad_vs", v, "vs_5_0");
      if (vs.empty() || vs.find("dcl_resource") != std::string::npos || LoopInstructions(vs) != 0 || !HasOp(vs, "umin")) return 251;
      for (const char* row : {"cb0[8]", "cb0[9]", "cb0[10]", "cb0[11]"}) {
        if (vs.find(row) == std::string::npos) return 251;
      }
      for (const char* other : {"cb0[12]", "cb0[13]", "cb0[24]", "cb0[25]"}) {
        if (vs.find(other) != std::string::npos) return 251;
      }
      const char* s = kClaySunQuadPs;
      if (!strstr(s, "  if (u_kind == 4) {\n    rgb = t.rgb * 0.1666666716337204 * u_reg[0].rgb;\n    a = 1.0;\n  } else if (u_kind == 5) {\n"
                     "    rgb = t.a * u_reg[0].rgb;\n    a = 1.0;\n  } else if (u_kind == 6) {\n    rgb = t.rgb * 0.20000000298023224 * t.a;\n    a = t.a;\n"
                     "  } else if (u_kind == 7) {\n    rgb = t.rgb * u_reg[1].rgb * t.a * u_reg[0].w;\n    a = t.a;\n  } else {\n    known = false;\n  }\n") ||
          !strstr(s, "  if (!known || (u_alpha_test != 0 && !(a > u_alpha_ref))) discard;\n") ||
          !strstr(s, "  rgb = (rgb > 0.0) ? min(rgb, 1.0) : float3(0.0, 0.0, 0.0);\n  return float4(floor(rgb * u_levels + 0.5) / u_levels, saturate(a));\n") ||
          !strstr(s, "  return (abs(c) < 3.0e38) ? c : float4(0.0, 0.0, 0.0, 0.0);\n") ||
          !strstr(s, "  float2 at = i.uv * u_fix;\n  float4 t = sun_sample(at, ddx(at), ddy(at));\n")) return 251;
      static_assert(unsigned(fable2::native::capture::PostQuadKind::kBeamSource) + 1 == 4 && unsigned(fable2::native::capture::PostQuadKind::kFlare) + 1 == 5 &&
                    unsigned(fable2::native::capture::PostQuadKind::kMoonGlow) + 1 == 6 && unsigned(fable2::native::capture::PostQuadKind::kSunDisc) + 1 == 7 &&
                    unsigned(fable2::native::capture::PostQuadKind::kCloudOccluder) + 1 == 8);
      static_assert(fable2::native::render::kSunBeamScale == 0.1666666716337204f && fable2::native::render::kSunGlowScale == 0.20000000298023224f &&
                    fable2::native::render::kSunFadeRate == 0.0010000000474974513f && fable2::native::render::kSunFadeStart == 1000.0f);
      const std::string ps = Disassembly("clay_sun_quad_ps", s, "ps_5_0");
      if (ps.empty() || Count(ps.c_str(), "sample_d") != 4 || Count(ps.c_str(), "discard") < 1 || LoopInstructions(ps) != 0 ||
          Count(ps.c_str(), "dcl_resource_texture2d") != 1 || Count(ps.c_str(), "dcl_sampler") != 4 || !HasOp(ps, "round_ni")) return 251;
      if (!ReadsComponent(ps, "cb0[12]", 'w') || !ReadsComponent(ps, "cb0[13]", 'x') || !ReadsComponent(ps, "cb0[25]", 'z') ||
          !ReadsComponent(ps, "cb0[24]", 'w') || !ReadsComponent(ps, "cb0[24]", 'z') || !ReadsComponent(ps, "cb0[24]", 'x')) return 251;
      for (const char* other : {"cb0[0]", "cb0[8]", "cb0[14]", "cb0[15]", "cb0[23]"}) {
        if (ps.find(other) != std::string::npos) return 251;
      }
      const std::string untested = Changed(s, "  if (!known || (u_alpha_test != 0 && !(a > u_alpha_ref))) discard;\n", "");
      const std::string unrounded = Changed(s, "floor(rgb * u_levels + 0.5) / u_levels", "rgb");
      if (untested.empty() || unrounded.empty() || Count(Disassembly("untested_sun_quad_ps", untested.c_str(), "ps_5_0").c_str(), "discard") != 0 ||
          ReadsComponent(Disassembly("unrounded_sun_quad_ps", unrounded.c_str(), "ps_5_0"), "cb0[25]", 'z')) return 251;
      const char* o = kClaySunOccluderPs;
      if (strncmp(o, "#define CLAY_SUN_OCCLUDER 1\n", 28) != 0 || strcmp(o + 28, s) != 0) return 251;  // (one text, two programs)
      if (!strstr(o, "void main(SunQuadOut i) {\n  float2 at = (i.uv * u_reg[0].xy + u_reg[1].xy) * u_fix;\n  float4 t = sun_sample(at, ddx(at), ddy(at));\n"
                     "  float a = 0.0;\n  if (u_kind == 8) a = saturate(t.a * u_reg[3].x * (1.0 - saturate(0.0010000000474974513 * (length(i.world.xy - "
                     "u_reg[2].xy) - 1000.0))));\n  if (u_kind != 8 || u_alpha_test == 0 || !(a > u_alpha_ref)) discard;\n}")) return 251;
      const std::string occluder = Disassembly("clay_sun_occluder_ps", o, "ps_5_0");
      if (occluder.empty() || occluder.find("dcl_output") != std::string::npos || Count(occluder.c_str(), "discard") < 1 ||
          Count(occluder.c_str(), "sample_d") != 4 || LoopInstructions(occluder) != 0 || !ReadsComponent(occluder, "cb0[12]", 'x') ||
          !ReadsComponent(occluder, "cb0[13]", 'x') || !ReadsComponent(occluder, "cb0[14]", 'x') || !ReadsComponent(occluder, "cb0[15]", 'x') ||
          !ReadsComponent(occluder, "cb0[24]", 'w') || ReadsComponent(occluder, "cb0[25]", 'z')) return 251;
      // (the colour program writes a colour and reads none of the occluder's registers past its own two)
      if (ps.find("dcl_output o0") == std::string::npos) return 251;
    }
    // 252: the sun's switch off is the picture of before, program for program: the layer's programs are
    // texts of their own (no program that draws the scene, a post step or a scene quad holds a word of
    // them), so every other program, the bright pass with its sun flag among them, is compiled from the
    // text it had.
    for (const char* src : {kClayVs, kClayPs, kClayTexturedPs, kClayTerrainLayerPs, kClayTerrainFarPs, kClayShadowPs, kSkyVs, kClaySkyPs,
                            kClayMaskPs, kOutputMapPs, kClayPostCompositePs, kClayPostRampPs, kClayPostDownsamplePs, kClayPostBrightPs,
                            kClayPostBlurPs, kFullscreenVs, kClayQuadVs, kClayQuadPs}) {
      for (const char* word : {"SunQuad", "sun_sample", "u_reg", "u_kind", "u_levels", "radial_source", "reduce_source", "sun_depth_source", "CLAY_SUN"}) {
        if (strstr(src, word)) return 252;
      }
    }
    // (and the bright pass's own text still has the two layers behind the flag, as plan Task 8 wrote it)
    if (!strstr(kClayPostBrightPs, "  if (p_sun_on != 0) v += sun_tf1.Load(at).rgb * 6.0 * p_b46x + sun_tf2.Load(at).rgb;\n")) return 252;
  }
  // 253: the previous-frame blend in the composite (plan Task 15). With the share a literal 0 the program
  // is, instruction for instruction, the one of before the blend (the text of before with the share's test
  // in front); with the share a word of the block the blended form is behind a test of it (cb0[1].w above
  // 0), reads t2 by one Load there and nowhere else, bounds the previous texel on both sides, mixes by the
  // share bounded at 1 and rounds twice (the composite's level, then the mix's).
  {
    const char* c = kClayPostCompositePs;
    if (!strstr(c, "Texture2D<float4> previous_front : register(t2);") ||
        !strstr(c, "float3 PostBlended(float4 pos) {\n  float3 veil = float3(0.0, 0.0, 0.0);\n"
                   "  if (p_bloom_on != 0) veil = bloom_target.SampleLevel(linear_clamp, (floor(pos.xy) + s_offset) * s_texel, 0.0).rgb;\n"
                   "  float3 now = post_level(PostComposite(scene_target.Load(int3(pos.xy, 0)).rgb, veil));\n"
                   "  float3 before = previous_front.Load(int3(pos.xy, 0)).rgb;\n"
                   "  before = (before > 0.0 && before < 3.0e38) ? min(before, 1.0) : float3(0.0, 0.0, 0.0);\n"
                   "  return post_level(saturate(now + (before - now) * min(p_previous, 1.0)));\n}") ||
        !strstr(c, "float4 main(float4 pos : SV_Position) : SV_Target {\n  if (p_previous > 0.0) return float4(PostBlended(pos), 1.0);\n"
                   "  float3 scene = scene_target.Load(int3(pos.xy, 0)).rgb;")) return 253;
    const std::string off = PreviousOff(c);
    const std::string without = Changed(c, "  if (p_previous > 0.0) return float4(PostBlended(pos), 1.0);\n", "");
    if (off.empty() || without.empty()) return 253;
    const std::string now_l = Disassembly("composite", c, "ps_5_0"), off_l = Disassembly("previous_off", off.c_str(), "ps_5_0");
    const std::string without_l = Disassembly("previous_without", without.c_str(), "ps_5_0");
    if (now_l.empty() || off_l.empty() || without_l.empty()) return 253;
    if (Code(off_l) != Code(without_l)) {
      std::cerr << "the composite with the previous frame's share 0 is not the program without the blend\n";
      return 253;
    }
    if (ReadsResource(off_l, 2) || ReadsComponent(off_l, "cb0[1]", 'w')) return 253;
    int loads2 = 0;
    const int depth2 = TextureReadDepth(now_l, "ld_indexable", 2, &loads2);
    if (!ReadsResource(now_l, 2) || loads2 != 1 || depth2 < 1 || !ReadsComponent(now_l, "cb0[1]", 'w') || BindPoint("composite", c, "ps_5_0", "previous_front") != 2 ||
        Count(now_l.c_str(), "round_ni") < 3 || Instructions(now_l) <= Instructions(off_l)) {
      std::cerr << "the composite's blended form: t2 loaded " << loads2 << " times at depth " << depth2 << "\n";
      return 253;
    }
    // Controls: a blend that reached the picture with the share 0 would be seen; without the previous texel's
    // bounds, or the share's, the program is another one.
    const std::string leak = Changed(off.c_str(), "if (0.0 > 0.0) return", "return");
    const std::string unbounded = Changed(c, "  before = (before > 0.0 && before < 3.0e38) ? min(before, 1.0) : float3(0.0, 0.0, 0.0);\n", "");
    const std::string full = Changed(c, "* min(p_previous, 1.0)", "* p_previous");
    if (leak.empty() || unbounded.empty() || full.empty() || !ReadsResource(Disassembly("previous_leak", leak.c_str(), "ps_5_0"), 2) ||
        Code(Disassembly("previous_unbounded", unbounded.c_str(), "ps_5_0")) == Code(now_l) ||
        Code(Disassembly("previous_full", full.c_str(), "ps_5_0")) == Code(now_l)) return 253;
    std::cout << "previous-frame blend: with the share 0 the composite is the program of before; the blended form has "
              << Instructions(now_l) - Instructions(off_l) << " instructions more\n";
  }
  // Negative control: broken HLSL must fail (proves the harness reports errors).
  if (Compile("broken_ps", "float4 main() : SV_Target { return undefined_symbol; }", "ps_5_0"))
    return 4;
  std::cout << "PASS: fullscreen VS, pattern PS, grid PS, clay VS/PS/textured PS, composite PS compile; alpha fields declared, alpha test discards; terrain layer and far PS compile, discard, constants at the table's offsets; steep-ground block; draw constants at ClayConstants' offsets in all five clay shaders; linear outputs, output mapping; light functions, lit paths, plant paths, leaf paths, light blocks at their tables' offsets and registers, every light_mode's compiled program in both stages; shadow map PS (depth only, bias, alpha test, one-sample alpha-to-mask), shadow view PS; the screen mask in the lit shaders, the pre-pass's depth-only forms, the mask shader (constants, loads, arithmetic, taps); the fog: in the colour programs of the four shaders that draw the scene, fog off the program without fog at every light_mode, the tables read behind a test, the compiled clamps; the sky step: the far-depth triangle, the bindings and constants, the compiled guards, the arithmetic and its literals; the unlit draws' fog: with the frame's flag 0 the unlit program at every light_mode, the compiled guards and bounds, the statements, an unlit tree's form; a lit draw without fog: the lit program without the fog; broken shader rejected; the unlit word: with it 1 every program is the one before it, the lit programs untouched, one multiply in an unlit one; the post steps: the block and the step's words at the tables' offsets, the composite's statements, its guards and the nearest texel in the compiled code, the ramp's index, the writes' own rounding, with the bloom's flag 0 the programs of before but for that rounding; the bloom: the downsample's four samples, the bright pass's guards and the sun's layers behind their flag, the blur's thirteen clamped loads; the base map's gamma sign: the curve, the record's word read by the lit terrain programs alone; the quad step: the block at the table's offsets in both stages, the six vertices through the game's matrix, the two kinds' statements, one texture under four samplers, the alpha test's discard, the compiled clamp; the sun layer: the blocks at their tables' offsets, the depth step's one bounded load written as depth, the reduction's one sample, the radial blur's fourteen bounded loads, the four kinds' statements and the occluder's form, the writes' own rounding, no other program's text touched; the previous-frame blend: with the share 0 the composite of before, the blended form behind the share's test with one bounded load of the previous picture\n";
  return 0;
}
