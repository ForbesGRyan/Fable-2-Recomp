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
// draw's captured rows; flat shading from screen-space derivatives.
// A vertex farther from `cut.xyz` than sqrt(cut.w) gets a NaN position, which
// drops every triangle it belongs to, as the game's instancing shaders do
// (DrawRecord::cut); cut.w is +inf for every other draw. Textured
// draws (kClayTexturedPs) sample the albedo at t3 with static sampler
// s<sampler_index> (bit 0 linear, bit 1 clamp) and keep a softened facet shade.
// Their alpha (clay_logic.h, ClayAlpha) is an albedo component or 1, times
// alpha_scale; alpha_mode says whether it is tested against alpha_ref (bits 0-2
// the compare function, bit 3 the test), then tested as alpha-to-mask (bit 8:
// kept iff alpha >= mask_base - offset * per_offset, the offset being the two bits
// of bits 16-23 that the pixel's parity picks, mask_base and per_offset those of the
// surface's sample count in bits 24-25: 0.5 and 1/8 for two samples, 0.5 and
// 1/16 for four, else 1 and 1/4; AlphaToMaskCovers in clay_logic.h), written for
// blending (bit 7; else 1 is written), and whether the colour is first divided
// by the albedo's alpha (bit 9). Flat draws (kClayPs) have no alpha. Every clay
// shader declares the one cbuffer Draw (ClayConstants). The vertex shader also
// hands on the position it read (`world`: for a terrain record the world
// position x, y, height), which only the terrain pixel shaders below take, and
// `normal`: (0, 0, 1), or for a terrain layer with the steep-ground block the
// game's vertex normal, whose (x, y) the draw's buffer at t2 holds in place of
// UVs (GeometryCache::TerrainNormals) and whose z is formed here as the game's
// vertex shader forms it; only kClayTerrainLayerPs takes it.
inline constexpr const char* kClayVs = R"hlsl(
cbuffer Draw : register(b0) {
  float4 r0; float4 r1; float4 r2; float4 r3;   // captured rows
  uint layout;        // 0 dot, 1 combine
  int base_vertex;
  uint vertex_count;  // positions (and UVs) in the buffers
  uint color;         // 0xRRGGBB
  float4 uv_xform;    // u * x + z, v * y + w
  uint textured;      // 1: uvs holds this draw's UVs
  uint sampler_index; uint alpha_mode; float alpha_ref;
  float4 cut;         // eye xyz, squared distance (+inf: no cut)
  float alpha_scale; uint3 pad2;
};
StructuredBuffer<float4> positions : register(t0);
StructuredBuffer<uint> indices : register(t1);
StructuredBuffer<float2> uvs : register(t2);
struct VsOut {
  float4 pos : SV_Position; float3 ndc : TEXCOORD0; float2 uv : TEXCOORD1;
  float3 world : TEXCOORD2;
  float3 normal : TEXCOORD3;
};
VsOut main(uint vid : SV_VertexID) {
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
  o.normal = float3(0, 0, 1);
  if (textured != 0 && in_range) {
    float2 t = uvs[v];
    o.uv = t * uv_xform.xy + uv_xform.zw;
    o.normal = float3(t, sqrt(abs(1.0 - dot(t, t))));
  }
  o.world = p.xyz;
  return o;
}
)hlsl";

inline constexpr const char* kClayPs = R"hlsl(
cbuffer Draw : register(b0) { float4 r0; float4 r1; float4 r2; float4 r3; uint layout; int base_vertex;
                              uint vertex_count; uint color; float4 uv_xform; uint textured;
                              uint sampler_index; uint alpha_mode; float alpha_ref;
                              float4 cut; float alpha_scale; uint3 pad2; };
float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0) : SV_Target {
  float3 n = normalize(cross(ddx(ndc), ddy(ndc)));
  float shade = 0.35 + 0.65 * saturate(abs(dot(n, normalize(float3(0.4, 0.6, -0.7)))));
  float3 base = float3((color >> 16) & 255, (color >> 8) & 255, color & 255) / 255.0;
  return float4(base * shade, 1.0);
}
)hlsl";

inline constexpr const char* kClayTexturedPs = R"hlsl(
cbuffer Draw : register(b0) { float4 r0; float4 r1; float4 r2; float4 r3; uint layout; int base_vertex;
                              uint vertex_count; uint color; float4 uv_xform; uint textured;
                              uint sampler_index; uint alpha_mode; float alpha_ref;
                              float4 cut; float alpha_scale; uint3 pad2; };
Texture2D albedo : register(t3);
SamplerState s_point_wrap : register(s0);
SamplerState s_linear_wrap : register(s1);
SamplerState s_point_clamp : register(s2);
SamplerState s_linear_clamp : register(s3);
// The GPU's compare functions: never, less, equal, less-equal, greater, not-equal, greater-equal, always.
bool alpha_passes(float a, float ref, uint func) {
  if (func == 0) return false;
  if (func == 1) return a < ref;
  if (func == 2) return a == ref;
  if (func == 3) return a <= ref;
  if (func == 4) return a > ref;
  if (func == 5) return a != ref;
  if (func == 6) return a >= ref;
  return true;
}
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
    return float4(t.rgb * (0.75 + 0.25 * facet), (alpha_mode & 0x80) ? a : 1.0);
  }
  base = float3((color >> 16) & 255, (color >> 8) & 255, color & 255) / 255.0;
  return float4(base * (0.35 + 0.65 * facet), 1.0);
}
)hlsl";

// Terrain material of the clay pass (frame-map section 13, "Terrain", "Native
// terrain material"; the reference is tools/xdk_sigmatch/terrain_replay.py,
// variant --no-base). The game's terrain shaders keep, of everything that is
// not light, sun or fog, one colour texture per draw; this is that colour as
// stored (the game squares it in its linear space; the clay target holds
// display values like every other textured draw):
//   layer (kClayTerrainLayerPs, builder 8221C9C8): colour = tf0 at
//     (c8.y * x, -c8.y * y); alpha = saturate(weight) * fade with
//     weight = c253.w * (tf18.w - c253.y), tf18 read per pixel in the layer's
//     weight cell with a linear filter (the game reads it at its vertices, which
//     are the cell's texel centres), and fade = 1 - sat(d2 * c31.x + c31.y), d2
//     the squared distance from the eye c19 to c4..c6 * (x, y, height, 1); the
//     pixel is discarded where tf18 < c114.x (the game's cull of a layer that
//     is absent there). The draw's captured blend adds the layers up.
//   far (kClayTerrainFarPs, builder 82207C30): colour = tf13 at the coordinate
//     the vertex carries (read from the game's two tables when the grid is built,
//     TerrainFarUvs), alpha 1; with t_flags bit 0 (shader 2F8A, the layers'
//     underlay) times sat(d2 * c31.x + c31.y).
//   steep ground (t_flags bit 1: the three layer shaders paired with vertex
//     shader C30A): where s = sat(|n.z| * c45.y + c45.x) is below the shader's
//     threshold the colour is the display value of the game's linear mix,
//     sqrt(s * tf0^2 + (1 - s) * (wy * tf13(o1)^2 + wx * tf13(o0)^2)), with
//     wx, wy = |n.x|, |n.y| / (|n.x| + |n.y| + small term), tf13 the layer's side
//     texture at the side projections o0 = (c8.x * y, -c8.x * height) and
//     o1 = (c8.x * x, -c8.x * height) of the world position, and n the game's
//     vertex normal interpolated over the grid (kClayVs). The game skips the
//     block from the threshold on; the mix is continuous there (1 - s is at
//     most 1/256).
// Not read: the base map tf3 with its exponent and tint, every light, the fog
// and the normal maps tf2 and tf14 (they are lighting; the frame map lists them).
// Both shaders end in kClayTexturedPs's alpha test, alpha-to-mask and output
// (the softened facet shade; the alpha written when the draw blends). The
// constants are cbuffer Terrain at b1, a constant buffer with one 256-byte slot
// per terrain draw (ClayTerrainConstants in clay_logic.h); kClayTerrainFields
// below is its layout, which the tests hold against both the compiled shaders
// and the C++ struct. Textures: t4 the colour (tf0 or tf13), t5 the weight
// cells (white for a far draw), t6 the side texture of a layer with the
// steep-ground block (white otherwise); static samplers as kClayTexturedPs.
struct ClayTerrainField {
  const char* name;  // the variable in cbuffer Terrain
  unsigned offset;   // bytes from the start of the buffer
  unsigned bytes;
};
inline constexpr ClayTerrainField kClayTerrainFields[] = {
    {"t_detail_uv", 0, 16},      {"t_weight_uv", 16, 16},     {"t_world0", 32, 16},        {"t_world1", 48, 16},
    {"t_world2", 64, 16},        {"t_eye", 80, 12},           {"t_hole_ref", 92, 4},       {"t_fade", 96, 8},
    {"t_weight_scale", 104, 4},  {"t_weight_bias", 108, 4},   {"t_colour_sampler", 112, 4}, {"t_weight_sampler", 116, 4},
    {"t_flags", 120, 4},         {"t_side_sampler", 124, 4},  {"t_side_uv", 128, 8},       {"t_slope", 136, 8},
    {"t_steep_threshold", 144, 4}, {"t_steep_eps", 148, 4},   {"t_pad", 152, 8},
};

#define FABLE2_CLAY_TERRAIN_COMMON_HLSL R"hlsl(
cbuffer Draw : register(b0) { float4 r0; float4 r1; float4 r2; float4 r3; uint layout; int base_vertex;
                              uint vertex_count; uint color; float4 uv_xform; uint textured;
                              uint sampler_index; uint alpha_mode; float alpha_ref;
                              float4 cut; float alpha_scale; uint3 pad2; };
cbuffer Terrain : register(b1) {
  float4 t_detail_uv;   // layer: tf0 coordinate of the world position, x * [0] + [2], y * [1] + [3]
  float4 t_weight_uv;   // layer: tf18 coordinate of the world position (the layer's weight cell)
  float4 t_world0; float4 t_world1; float4 t_world2;   // rows c4..c6 (far: identity)
  float3 t_eye; float t_hole_ref;                      // c19.xyz; layer: c114.x
  float2 t_fade; float t_weight_scale; float t_weight_bias;   // c31.xy; layer: c253.w, c253.y
  uint t_colour_sampler; uint t_weight_sampler; uint t_flags; uint t_side_sampler;   // t_flags bit 0: the far colour
                                                                // fades in; bit 1: the layer has the steep-ground block
  float2 t_side_uv; float2 t_slope;                    // steep: (c8.x, -c8.x) into the side texture; c45.xy
  float t_steep_threshold; float t_steep_eps; uint2 t_pad;   // steep: the shader's two literals
};
Texture2D t_colour : register(t4);
Texture2D t_weights : register(t5);
SamplerState s_point_wrap : register(s0);
SamplerState s_linear_wrap : register(s1);
SamplerState s_point_clamp : register(s2);
SamplerState s_linear_clamp : register(s3);
// The GPU's compare functions: never, less, equal, less-equal, greater, not-equal, greater-equal, always.
bool alpha_passes(float a, float ref, uint func) {
  if (func == 0) return false;
  if (func == 1) return a < ref;
  if (func == 2) return a == ref;
  if (func == 3) return a <= ref;
  if (func == 4) return a > ref;
  if (func == 5) return a != ref;
  if (func == 6) return a >= ref;
  return true;
}
float4 sample_colour(float2 at) {
  if (t_colour_sampler == 0) return t_colour.Sample(s_point_wrap, at);
  if (t_colour_sampler == 1) return t_colour.Sample(s_linear_wrap, at);
  if (t_colour_sampler == 2) return t_colour.Sample(s_point_clamp, at);
  return t_colour.Sample(s_linear_clamp, at);
}
// sat(d2 * c31.x + c31.y): 0 where the layers are whole, 1 where they are gone.
float terrain_fade_in(float3 world) {
  float4 p = float4(world, 1.0);
  float3 e = t_eye - float3(dot(t_world0, p), dot(t_world1, p), dot(t_world2, p));
  float d2 = dot(e, e);
  return saturate(d2 * t_fade.x + t_fade.y);
}
// kClayTexturedPs's alpha test, alpha-to-mask and output for a colour and an alpha.
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
  return float4(colour * (0.75 + 0.25 * facet), (alpha_mode & 0x80) ? a : 1.0);
}
)hlsl"

inline constexpr const char* kClayTerrainLayerPs = FABLE2_CLAY_TERRAIN_COMMON_HLSL R"hlsl(
Texture2D t_side : register(t6);
// The side texture at `at` with the screen derivatives d = (ddx, ddy) of the coordinate.
float4 sample_side(float2 at, float4 d) {
  if (t_side_sampler == 0) return t_side.SampleGrad(s_point_wrap, at, d.xy, d.zw);
  if (t_side_sampler == 1) return t_side.SampleGrad(s_linear_wrap, at, d.xy, d.zw);
  if (t_side_sampler == 2) return t_side.SampleGrad(s_point_clamp, at, d.xy, d.zw);
  return t_side.SampleGrad(s_linear_clamp, at, d.xy, d.zw);
}
float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0, float2 uv : TEXCOORD1,
            float3 world : TEXCOORD2, float3 normal : TEXCOORD3) : SV_Target {
  float3 colour = sample_colour(world.xy * t_detail_uv.xy + t_detail_uv.zw).rgb;
  // The steep-ground block. The side projections and their derivatives are formed for every pixel,
  // so that the two fetches can stay inside the branch.
  float2 o0 = world.yz * t_side_uv;
  float2 o1 = world.xz * t_side_uv;
  float4 d0 = float4(ddx(o0), ddy(o0));
  float4 d1 = float4(ddx(o1), ddy(o1));
  float s = saturate(abs(normal.z) * t_slope.y + t_slope.x);
  [branch] if ((t_flags & 2) && s < t_steep_threshold) {
    float inv = 1.0 / (abs(normal.x) + t_steep_eps + abs(normal.y));
    float wx = abs(normal.x) * inv;
    float wy = abs(normal.y) * inv;
    float3 side0 = sample_side(o0, d0).rgb;
    float3 side1 = sample_side(o1, d1).rgb;
    colour = sqrt(s * colour * colour + (1.0 - s) * (wy * side1 * side1 + wx * side0 * side0));
  }
  float2 wuv = world.xy * t_weight_uv.xy + t_weight_uv.zw;
  float value = (t_weight_sampler & 2) ? t_weights.SampleLevel(s_linear_clamp, wuv, 0).w
                                       : t_weights.SampleLevel(s_linear_wrap, wuv, 0).w;
  if (value < t_hole_ref) discard;
  float weight = t_weight_scale * (value - t_weight_bias);
  float a = saturate(weight) * (1.0 - terrain_fade_in(world));
  return terrain_out(colour, a, pos, ndc);
}
)hlsl";

inline constexpr const char* kClayTerrainFarPs = FABLE2_CLAY_TERRAIN_COMMON_HLSL R"hlsl(
float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0, float2 uv : TEXCOORD1,
            float3 world : TEXCOORD2) : SV_Target {
  float3 colour = sample_colour(uv).rgb;
  if (t_flags & 1) colour *= terrain_fade_in(world);
  return terrain_out(colour, 1.0, pos, ndc);
}
)hlsl";

#undef FABLE2_CLAY_TERRAIN_COMMON_HLSL

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
