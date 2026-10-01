#pragma once

// HLSL (SM 5.0) for the native renderer plumbing tests, compiled at runtime
// by the D3D12 RHI (D3DCompile). Entry point "main" for all shaders.

namespace fable2::native::shaders {

// Fullscreen triangle from SV_VertexID (no vertex buffer).
inline constexpr const char* kFullscreenVs = R"hlsl(
struct VsOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};
VsOut main(uint id : SV_VertexID) {
  VsOut o;
  float2 uv = float2((id << 1) & 2, id & 2);
  o.uv = uv;
  o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  return o;
}
)hlsl";

// Replace mode: 32px checkerboard plus an orange bar sweeping left to right.
inline constexpr const char* kPatternPs = R"hlsl(
cbuffer Params : register(b0) { float4 params; }  // frame, width, height, 0
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
  uint2 cell = uint2(pos.xy) / 32;
  float checker = ((cell.x + cell.y) & 1) ? 0.85 : 0.25;
  float bar_x = frac(params.x / 240.0) * params.y;
  float3 color = abs(pos.x - bar_x) < 16.0 ? float3(1.0, 0.45, 0.0)
                                           : float3(checker, checker, checker);
  return float4(color, 1.0);
}
)hlsl";

// Overlay mode: translucent green 64px grid drawn over the emulated frame.
inline constexpr const char* kGridPs = R"hlsl(
cbuffer Params : register(b0) { float4 params; }  // frame, width, height, 0
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
  bool on_line = fmod(pos.x, 64.0) < 1.0 || fmod(pos.y, 64.0) < 1.0;
  if (!on_line) discard;
  return float4(0.0, 1.0, 0.4, 0.5);
}
)hlsl";

}  // namespace fable2::native::shaders
