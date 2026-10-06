// Compiles the native renderer's HLSL with D3DCompile (no GPU needed).
#include "../../src/native/fable2_native_shaders.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <cstring>
#include <iostream>

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
  // Negative control: broken HLSL must fail (proves the harness reports errors).
  if (Compile("broken_ps", "float4 main() : SV_Target { return undefined_symbol; }", "ps_5_0"))
    return 4;
  std::cout << "PASS: fullscreen VS, pattern PS, grid PS, clay VS/PS/textured PS, composite PS compile; broken shader rejected\n";
  return 0;
}
