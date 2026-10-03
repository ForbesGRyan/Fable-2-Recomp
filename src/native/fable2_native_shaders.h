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

// Clay pass (native scene, render/clay_pass.cpp): vertex pulling from StructuredBuffers
// (float4 positions, uint32 triangle-list indices, float2 UVs), transformed by the
// draw's captured rows; flat shading from screen-space derivatives. Textured
// draws (kClayTexturedPs) sample the albedo at t3 with static sampler
// s<sampler_index> (bit 0 linear, bit 1 clamp) and keep a softened facet shade.
inline constexpr const char* kClayVs = R"hlsl(
cbuffer Draw : register(b0) {
  float4 r0; float4 r1; float4 r2; float4 r3;   // captured rows
  uint layout;        // 0 dot, 1 combine
  int base_vertex;
  uint vertex_count;  // positions (and UVs) in the buffers
  uint color;         // 0xRRGGBB
  float4 uv_xform;    // u * x + z, v * y + w
  uint textured;      // 1: uvs holds this draw's UVs
  uint sampler_index;
  uint2 pad;
};
StructuredBuffer<float4> positions : register(t0);
StructuredBuffer<uint> indices : register(t1);
StructuredBuffer<float2> uvs : register(t2);
struct VsOut { float4 pos : SV_Position; float3 ndc : TEXCOORD0; float2 uv : TEXCOORD1; };
VsOut main(uint vid : SV_VertexID) {
  VsOut o;
  int v = int(indices[vid]) + base_vertex;
  bool in_range = v >= 0 && uint(v) < vertex_count;
  float4 p = in_range ? positions[v] : float4(0, 0, 0, 0);
  float4 c = layout == 0 ? float4(dot(r0, p), dot(r1, p), dot(r2, p), dot(r3, p))
                         : p.x * r0 + p.y * r1 + p.z * r2 + p.w * r3;
  o.pos = c;
  o.ndc = float3(c.xy / max(abs(c.w), 1e-6), c.w * 0.01);
  o.uv = float2(0, 0);
  if (textured != 0 && in_range) o.uv = uvs[v] * uv_xform.xy + uv_xform.zw;
  return o;
}
)hlsl";

inline constexpr const char* kClayPs = R"hlsl(
cbuffer Draw : register(b0) { float4 r0; float4 r1; float4 r2; float4 r3; uint layout; int base_vertex; uint vertex_count; uint color; };
float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0) : SV_Target {
  float3 n = normalize(cross(ddx(ndc), ddy(ndc)));
  float shade = 0.35 + 0.65 * saturate(abs(dot(n, normalize(float3(0.4, 0.6, -0.7)))));
  float3 base = float3((color >> 16) & 255, (color >> 8) & 255, color & 255) / 255.0;
  return float4(base * shade, 1.0);
}
)hlsl";

inline constexpr const char* kClayTexturedPs = R"hlsl(
cbuffer Draw : register(b0) { float4 r0; float4 r1; float4 r2; float4 r3; uint layout; int base_vertex;
                              uint vertex_count; uint color; float4 uv_xform; uint textured; uint sampler_index; uint2 pad; };
Texture2D albedo : register(t3);
SamplerState s_point_wrap : register(s0);
SamplerState s_linear_wrap : register(s1);
SamplerState s_point_clamp : register(s2);
SamplerState s_linear_clamp : register(s3);
float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target {
  float3 n = normalize(cross(ddx(ndc), ddy(ndc)));
  float facet = saturate(abs(dot(n, normalize(float3(0.4, 0.6, -0.7)))));
  float3 base;
  if (textured != 0) {
    float4 t;
    if (sampler_index == 0) t = albedo.Sample(s_point_wrap, uv);
    else if (sampler_index == 1) t = albedo.Sample(s_linear_wrap, uv);
    else if (sampler_index == 2) t = albedo.Sample(s_point_clamp, uv);
    else t = albedo.Sample(s_linear_clamp, uv);
    return float4(t.rgb * (0.75 + 0.25 * facet), 1.0);
  }
  base = float3((color >> 16) & 255, (color >> 8) & 255, color & 255) / 255.0;
  return float4(base * (0.35 + 0.65 * facet), 1.0);
}
)hlsl";

// Debug views (render/composite.cpp, fable2_native_view): the clay target drawn over the guest output.
// mode 1 overlay (alpha 0.5), 2 split (right half only), 3 native (opaque).
inline constexpr const char* kCompositePs = R"hlsl(
cbuffer C : register(b0) { float out_w; float out_h; uint mode; float alpha; };
Texture2D clay : register(t0);
SamplerState s : register(s0);
float4 main(float4 pos : SV_Position) : SV_Target {
  float2 uv = pos.xy / float2(out_w, out_h);
  if (mode == 2 && uv.x < 0.5) discard;   // split: left half stays emulated
  return float4(clay.Sample(s, uv).rgb, alpha);
}
)hlsl";

}  // namespace fable2::native::shaders
