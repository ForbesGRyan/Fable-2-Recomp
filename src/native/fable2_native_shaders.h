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
// pixel shader writes linear colour into the pass's float target (frame-map
// section 14, "Linear target"). An unlit draw writes the square of the display
// value it wrote into the 8-bit target before, which is the game's own
// A = albedo^2. The facet shade multiplied the display value, so it multiplies
// before the square (clay_linear; ClayLinear in clay_logic.h); kOutputMapPs
// takes the root. That value is clamped to [0, 1] as the 8-bit target clamped
// it, so that no NaN, negative value or value over 1 is written: a float target
// would keep a NaN and blending would spread it (the albedo textures are UNORM
// and the shade is finite, so none arises; where the compiler can prove the
// range it drops the clamp, and it keeps it for the terrain layers' mix). A lit
// draw (light_mode bit 2, below) writes its light function's value times the
// frame's exposure (Frame's f_exposure; no other value takes it: an unlit draw
// and the clear are written as they are and the output mapping multiplies by
// 1, so lit and unlit values have one unit in the target and a blend of the
// two is consistent), without the facet shade and without the clamp at 1,
// through clay_lit: a number, not negative, at most 64. Alpha is not squared;
// the alpha test and alpha-to-mask read it as it is, and it is clamped where it
// is written for the blend (the 8-bit target clamped it there; the float
// target has no alpha channel and no sign: a blend's terms are all products of
// values and factors that are not negative). Every clay
// shader declares the one cbuffer Draw (ClayConstants). The vertex shader also
// hands on the position it read (`world`: for a terrain record the world
// position x, y, height), which only the terrain pixel shaders below use.
//
// The light (frame-map section 14, "Native light"; clay_logic.h, "Native
// light"). Draw's light_mode says what the draw has: bit 0, the game's terrain
// vertex normal, whose (x, y) the draw's buffer at t9 holds per grid point
// (GeometryCache::TerrainNormals) and whose z is formed here as the game's
// vertex shader forms it, for the steep-ground block and for the light; bit 1,
// an object's stream normal at t10 and its word at t11, the normal put through
// the draw's rows of cbuffer Object (b1) as the game puts it through c4..c6
// times c7.w (a bone-skinned mesh's normals are in the buffer after the bones
// and take the rows like any other); bit 3 (with bit 1), the word is not the
// stream colour but the vertex's probe set, and the colour is the probe colour
// of the draw's twelve probe registers (Object's o_probe, vertex c28..c39) and
// the vertex's four weights at t12, as the game's probe vertex shaders form
// it, with no power (the pixel function's pow(|x|, c21.w) is the only one it
// takes; the game takes that absolute value too, so a colour that the signed
// weights make zero or negative is lit as the game lights it); bit 2, the
// draw is lit: the vertex shader also makes the vector of the ambient factor
// (AmbientAxis with the frame's axis) and, without bit 3, the colour-stream
// colour raised to the vertex shader's literal, and the pixel shader calls its
// family's function with the frame's block (cbuffer Frame, b2) and the draw's
// values. Bit 4 (with bit 1): an instanced plant; its normal at t10 is in the
// world already and takes no rows, the word at t11 is the colour of the
// vertex's copy, and t12 (probe_or_bend) holds the copy's up axis and the
// vertex's height, the inputs of the bent normal; bit 5 (with bit 4): the
// plant's pixel shader has the ambient factor. Bit 6 (with bits 1 and 3): a
// tree leaf, a probe record whose vertex stage also makes the sun's two
// scalars on the side of the card the viewer sees. The three mesh blocks of
// the vertex shader exclude each other by these bits (clay_logic.h,
// LightModeBlockOf). A light buffer is read only for a vertex below
// light_count, and only by a draw whose light_mode names it: the reads are
// inside [branch] blocks (an object's weights inside two: the object's and
// bit 3's), because a buffer bound through a root descriptor has no bounds
// check and the compiler otherwise turns `if` into a select and loads all
// four buffers for every vertex of every draw (the tests hold the compiled
// shader to this, for each of the 128 values of light_mode: seven bits). The
// positions (t0), the indices (t1) and the texture coordinates (t2) are
// loaded outside any branch, by design: every draw binds its own three, and
// the index range of a record is checked at both ends before it is drawn
// (IndexRangeValid). The pass also binds something that covers the
// draw's vertices at all four light registers at every draw (clay_pass.cpp).
// A draw without bit 2 reads neither Frame nor Object and is the unlit colour.
// kClayFrameFields and kClayObjectFields are the two buffers' layouts, which the
// tests hold against the compiled shaders and the C++ structs.
struct ClayBufferField {
  const char* name;  // the variable in the cbuffer
  unsigned offset;   // bytes from the start of the buffer
  unsigned bytes;
};
inline constexpr ClayBufferField kClayFrameFields[] = {
    {"f_sun_dir", 0, 12},  {"f_light_on", 12, 4},  {"f_sun_col", 16, 12}, {"f_tint_pow", 28, 4},
    {"f_tint", 32, 12},    {"f_exposure", 44, 4},  {"f_add", 48, 12},     {"f_pad0", 60, 4},
    {"f_floor", 64, 12},   {"f_mask_du", 76, 4},   {"f_axis", 80, 12},    {"f_mask_dv", 92, 4},
    {"f_eye", 96, 12},     {"f_eye_on", 108, 4},
};
inline constexpr ClayBufferField kClayObjectFields[] = {
    {"o_rot0", 0, 16}, {"o_rot1", 16, 16}, {"o_rot2", 32, 16},        {"o_c49", 48, 12},
    {"o_c27w", 60, 4}, {"o_c26", 64, 12},  {"o_colour_power", 76, 4}, {"o_probe", 80, 192},
    {"o_leaf", 272, 16},
};

#define FABLE2_CLAY_DRAW_HLSL R"hlsl(
cbuffer Draw : register(b0) { float4 r0; float4 r1; float4 r2; float4 r3; uint layout; int base_vertex;
                              uint vertex_count; uint color; float4 uv_xform; uint textured;
                              uint sampler_index; uint alpha_mode; float alpha_ref;
                              float4 cut; float alpha_scale; uint light_mode; uint light_count; uint pad2; };
)hlsl"

// The frame's light block (ClayFrameConstants) and the light functions: the functions of
// tools/xdk_sigmatch/light_formulas.py term for term (docs/native-renderer/lighting-formulas.md), with the
// argument lists of their mirrors in clay_logic.h. Guards: a vector without a length (or with one that is
// not a number) normalises to zero, never to a made-up direction, so a zero normal gives no sun and t = 0;
// the power of a zero base is 0; t and n_dot_sun are saturated; nothing is divided but the leaves' wrapped
// cosine (LeafLight).
#define FABLE2_CLAY_LIGHT_HLSL R"hlsl(
cbuffer Frame : register(b2) {
  float3 f_sun_dir; uint f_light_on;     // c80.xyz of a one-light draw (= c109.xyz), as it is; 1: the frame is lit
  float3 f_sun_col; float f_tint_pow;    // c87.xyz; c21.w
  float3 f_tint; float f_exposure;       // c21.xyz; the exposure, which the lit pixel paths alone multiply by
  float3 f_add; float f_pad0;            // c20.xyz (c20.w, the output scale, is not applied)
  float3 f_floor; float f_mask_du;       // c22.xyz; 1 / the target's width: with f_mask_dv the screen mask's coordinate
  float3 f_axis; float f_mask_dv;        // c23.xyz (= vertex c19.xyz); 1 / the target's height
  float3 f_eye; uint f_eye_on;           // c19.xyz, the eye (= vertex c9.xyz of the leaf shaders); 1: it is usable
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
)hlsl"

#define FABLE2_CLAY_OBJECT_HLSL R"hlsl(
cbuffer Object : register(b1) {
  float4 o_rot0; float4 o_rot1; float4 o_rot2;   // rows of M = c4..c6.xyz * c7.w (xyz); an instanced plant: no
                                                 // rows, and in the three w its bend constants c45.x, .y, .z;
                                                 // a leaf: in the three w the world translation c4..c6 .w
  float3 o_c49; float o_c27w;                    // pixel c49.xyz (the whole colour); c27.w (the ambient part)
  float3 o_c26; float o_colour_power;            // pixel c26.xyz (with the output scale); the stream colour's power
                                                 // (an instanced plant: 2.2, the factor of c21.w in its colour's
                                                 // exponent; a leaf: 2.2 * 0.4545, that of its ambient's)
  float4 o_probe[12];                            // vertex c28..c39: three probe sets of four (base, rows of r, g, b)
  float4 o_leaf;                                 // a leaf: x vertex c50.w (the wrap of the front light), y 1 / c7.w
};
)hlsl"

inline constexpr const char* kClayVs = FABLE2_CLAY_DRAW_HLSL FABLE2_CLAY_LIGHT_HLSL FABLE2_CLAY_OBJECT_HLSL R"hlsl(
StructuredBuffer<float4> positions : register(t0);
StructuredBuffer<uint> indices : register(t1);
StructuredBuffer<float2> uvs : register(t2);
StructuredBuffer<float2> terrain_normals : register(t9);
StructuredBuffer<float3> normals : register(t10);
StructuredBuffer<uint> colours : register(t11);          // a stream colour, a probe set, or the colour of a plant's copy
StructuredBuffer<float4> probe_or_bend : register(t12);  // a probe object's or leaf's weights, or a plant's bend inputs (up axis, height)
struct VsOut {
  float4 pos : SV_Position; float3 ndc : TEXCOORD0; float2 uv : TEXCOORD1;
  float3 world : TEXCOORD2;
  float3 normal : TEXCOORD3;
  float3 axis : TEXCOORD4;
  float3 vcolour : TEXCOORD5;
  float4 bend : TEXCOORD6;
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
}
)hlsl";

inline constexpr const char* kClayPs = FABLE2_CLAY_DRAW_HLSL R"hlsl(
float3 clay_linear(float3 d) { d = saturate(d); return d * d; }
float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0) : SV_Target {
  float3 n = normalize(cross(ddx(ndc), ddy(ndc)));
  float shade = 0.35 + 0.65 * saturate(abs(dot(n, normalize(float3(0.4, 0.6, -0.7)))));
  float3 base = float3((color >> 16) & 255, (color >> 8) & 255, color & 255) / 255.0;
  return float4(clay_linear(base * shade), 1.0);
}
)hlsl";

// A lit textured draw (light_mode bit 2: an object or a tree trunk; lighting-formulas.md, "Objects",
// "Tree trunks"): colour = (A * c27.w * (pow(vcolour, c21.w) * c21.xyz * L + c20.xyz) + A * c87.xyz *
// sat(N . c80.xyz) * mask) * c49.xyz, times c26.xyz, with A = albedo^2 (after the division by the
// alpha where the shader has it), vcolour the vertex shader's colour (the stream colour raised to its
// literal, or the probe colour as it is), N the interpolated M n normalised, L from
// t = sat(normalize(axis) . N), and the mask the frame's screen mask at the pixel (t4: the sun's shadow,
// 1 = lit; the 1 x 1 white texture in a frame without one; clay_logic.h, "Native shadows"), read at the
// pixel's centre over the target's size, by a lit pixel only. The pixel shader is the same program for
// both kinds of colour and reads none of the probe registers.
// kClayTexturedDepthPs is the same text compiled with CLAY_DEPTH_ONLY: the shader of the depth pre-pass
// for a record of this kind. It makes the same alpha and discards the same pixels (the alpha test,
// alpha-to-mask) and then returns: no light, no mask, no colour (the draw writes none).
#define FABLE2_CLAY_TEXTURED_HLSL FABLE2_CLAY_DRAW_HLSL FABLE2_CLAY_LIGHT_HLSL FABLE2_CLAY_OBJECT_HLSL R"hlsl(
Texture2D albedo : register(t3);
Texture2D shadow_mask : register(t4);
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
float3 clay_linear(float3 d) { d = saturate(d); return d * d; }
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
      float mask = shadow_mask.SampleLevel(s_linear_clamp, pos.xy * float2(f_mask_du, f_mask_dv), 0).r;
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
      } else if (light_mode & 64) {
        // A tree leaf: the vertex side's ambient and the sun's two scalars (bend.x front, bend.y back)
        // on A, the back light on leaf texels only; the mask through the shader's two literals (0.6
        // and 0.4: once on the front light, twice on the back light; both factors are 1 at a mask
        // of 1); c27.w, c49 and c26 as an object's.
        float2 m = LeafMask(mask, 0.6, 0.4);
        lit = LightLeaf(clay_linear(t.rgb), vcolour, bend.x, bend.y, f_sun_col, o_c27w, m.x, m.y, o_c49) * o_c26;
      } else {
        float3 N = light_normalize(normal);
        lit = LightObject(clay_linear(t.rgb), vcolour, f_tint, f_tint_pow, f_add, f_floor, AmbientT(axis, normal),
                          dot(N, f_sun_dir), f_sun_col, o_c27w, SunGateSingle(mask, 1.0), o_c49) * o_c26;
      }
      return float4(clay_lit(lit * f_exposure), (alpha_mode & 0x80) ? saturate(a) : 1.0);
    }
    return float4(clay_linear(t.rgb * (0.75 + 0.25 * facet)), (alpha_mode & 0x80) ? saturate(a) : 1.0);
#endif
  }
  base = float3((color >> 16) & 255, (color >> 8) & 255, color & 255) / 255.0;
  return float4(clay_linear(base * (0.35 + 0.65 * facet)), 1.0);
}
)hlsl"
inline constexpr const char* kClayTexturedPs = FABLE2_CLAY_TEXTURED_HLSL;
inline constexpr const char* kClayTexturedDepthPs = "#define CLAY_DEPTH_ONLY 1\n" FABLE2_CLAY_TEXTURED_HLSL;

// Terrain material of the clay pass (frame-map section 13, "Terrain", "Native
// terrain material"; the reference is tools/xdk_sigmatch/terrain_replay.py,
// variant --no-base, and with the light --lit). The game's terrain shaders keep, of everything that is
// not light, sun or fog, one colour texture per draw; this is that colour
// squared, as the game squares it (A = tf0^2, F = tf13^2): the clay target is
// linear like the game's, so the draws' captured blend adds the layers of a
// square up as the game adds them, and the far colour's fade multiplies the
// linear value as the game's does:
//   layer (kClayTerrainLayerPs, builder 8221C9C8): colour = tf0^2, tf0 at
//     (c8.y * x, -c8.y * y); alpha = saturate(weight) * fade with
//     weight = c253.w * (tf18.w - c253.y), tf18 read per pixel in the layer's
//     weight cell with a linear filter (the game reads it at its vertices, which
//     are the cell's texel centres), and fade = 1 - sat(d2 * c31.x + c31.y), d2
//     the squared distance from the eye c19 to c4..c6 * (x, y, height, 1); the
//     pixel is discarded where tf18 < c114.x (the game's cull of a layer that
//     is absent there). The draw's captured blend adds the layers up.
//   far (kClayTerrainFarPs, builder 82207C30): colour = tf13^2, tf13 at the
//     coordinate the vertex carries (read from the game's two tables when the grid
//     is built, TerrainFarUvs), alpha 1; with t_flags bit 0 (shader 2F8A, the
//     layers' underlay) times sat(d2 * c31.x + c31.y).
//   steep ground (t_flags bit 1: the three layer shaders paired with vertex
//     shader C30A): where s = sat(|n.z| * c45.y + c45.x) is below the shader's
//     threshold the colour is the game's linear mix,
//     s * tf0^2 + (1 - s) * (wy * tf13(o1)^2 + wx * tf13(o0)^2), with
//     wx, wy = |n.x|, |n.y| / (|n.x| + |n.y| + small term), tf13 the layer's side
//     texture at the side projections o0 = (c8.x * y, -c8.x * height) and
//     o1 = (c8.x * x, -c8.x * height) of the world position, and n the game's
//     vertex normal interpolated over the grid (kClayVs). The game skips the
//     block from the threshold on; the mix is continuous there (1 - s is at
//     most 1/256).
// The light (light_mode bit 2; lighting-formulas.md, "Terrain layers", "Far terrain"), with the
// colour above as A or F, the base map tf3 at (world - heightmap offset) * c14.xy as the baked term,
// N the vertex normal interpolated and normalised, and for the layers the screen mask at the pixel
// (t4, as kClayTexturedPs reads it; the far terrain has none: its sun is scaled by c11.x):
//   layer  A * c11.x * (pow(tf3, c21.w) * c21.xyz * L + c20.xyz) + A * c87.xyz * sat(N . c80.xyz) *
//          sat(8 * (N . c109.xyz)) * mask, L from t = sat(normalize(axis) . N), axis the vertex shader's
//          lerp(n, c19, (1 + n . c19) / 2) interpolated;
//   far    F * (pow(tf3, c21.w) * c21.xyz * L + c20.xyz) + F * c11.x * c87.xyz * sat(N . c80.xyz),
//          L from t = sat(N . c23.xyz); the underlay's fade multiplies the lit colour.
// Not read: the fog, the normal maps tf2 and tf14, the lanterns of the two- and
// four-light shaders.
// kClayTerrainLayerDepthPs and kClayTerrainFarDepthPs are the two texts compiled with CLAY_DEPTH_ONLY,
// the depth pre-pass's shaders for terrain records: the same alpha (the layer's weight times its fade, the
// hole's discard) and the same alpha test and alpha-to-mask, then a return without light or colour.
// Both shaders end in kClayTexturedPs's alpha test, alpha-to-mask and output
// (unlit: the softened facet shade, squared here because it multiplies the display
// value and the colour is linear: ClayLinearShaded in clay_logic.h; lit: the value
// through clay_lit; the alpha written when the draw blends). The
// constants are cbuffer Terrain at b1, a constant buffer with one 256-byte slot
// per terrain draw (ClayTerrainConstants in clay_logic.h); kClayTerrainFields
// below is its layout, which the tests hold against both the compiled shaders
// and the C++ struct. Textures: t5 the colour (tf0 or tf13), t6 the weight
// cells (white for a far draw), t7 the side texture of a layer with the
// steep-ground block (white otherwise), t8 the base map of a lit draw (white
// otherwise); t4 the screen mask (the layers); static samplers as kClayTexturedPs.
using ClayTerrainField = ClayBufferField;
inline constexpr ClayTerrainField kClayTerrainFields[] = {
    {"t_detail_uv", 0, 16},      {"t_weight_uv", 16, 16},     {"t_world0", 32, 16},        {"t_world1", 48, 16},
    {"t_world2", 64, 16},        {"t_eye", 80, 12},           {"t_hole_ref", 92, 4},       {"t_fade", 96, 8},
    {"t_weight_scale", 104, 4},  {"t_weight_bias", 108, 4},   {"t_colour_sampler", 112, 4}, {"t_weight_sampler", 116, 4},
    {"t_flags", 120, 4},         {"t_side_sampler", 124, 4},  {"t_side_uv", 128, 8},       {"t_slope", 136, 8},
    {"t_steep_threshold", 144, 4}, {"t_steep_eps", 148, 4},   {"t_pad", 152, 8},           {"t_base_uv", 160, 16},
    {"t_base_sampler", 176, 4},  {"t_c11x", 180, 4},          {"t_facing_scale", 184, 4},  {"t_light_pad", 188, 4},
};

#define FABLE2_CLAY_TERRAIN_COMMON_HLSL FABLE2_CLAY_DRAW_HLSL FABLE2_CLAY_LIGHT_HLSL R"hlsl(
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
  float4 t_base_uv;     // lit: tf3 coordinate of the world position
  uint t_base_sampler; float t_c11x; float t_facing_scale; uint t_light_pad;   // lit: c11.x; layer: the literal 8
};
Texture2D t_colour : register(t5);
Texture2D t_weights : register(t6);
Texture2D t_base : register(t8);
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
#ifdef CLAY_DEPTH_ONLY
  return float4(0.0, 0.0, 0.0, 0.0);
#else
  if (light_mode & 4) return float4(clay_lit(colour * f_exposure), (alpha_mode & 0x80) ? saturate(a) : 1.0);
  float shade = 0.75 + 0.25 * facet;
  return float4(saturate(colour * (shade * shade)), (alpha_mode & 0x80) ? saturate(a) : 1.0);
#endif
}
)hlsl"

#define FABLE2_CLAY_TERRAIN_LAYER_HLSL FABLE2_CLAY_TERRAIN_COMMON_HLSL R"hlsl(
Texture2D t_side : register(t7);
Texture2D shadow_mask : register(t4);
// The side texture at `at` with the screen derivatives d = (ddx, ddy) of the coordinate.
float4 sample_side(float2 at, float4 d) {
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
    float mask = shadow_mask.SampleLevel(s_linear_clamp, pos.xy * float2(f_mask_du, f_mask_dv), 0).r;
    lin = LightTerrainLayer(lin, sample_base(base_at, base_d).rgb, f_tint, f_tint_pow, f_add, f_floor,
                            AmbientT(axis, normal), dot(N, f_sun_dir), f_sun_col, t_c11x, SunGateSingle(mask, facing));
  }
  return terrain_out(lin, a, pos, ndc);
}
)hlsl"
inline constexpr const char* kClayTerrainLayerPs = FABLE2_CLAY_TERRAIN_LAYER_HLSL;
inline constexpr const char* kClayTerrainLayerDepthPs = "#define CLAY_DEPTH_ONLY 1\n" FABLE2_CLAY_TERRAIN_LAYER_HLSL;

#define FABLE2_CLAY_TERRAIN_FAR_HLSL FABLE2_CLAY_TERRAIN_COMMON_HLSL R"hlsl(
float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0, float2 uv : TEXCOORD1,
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
}
)hlsl"
inline constexpr const char* kClayTerrainFarPs = FABLE2_CLAY_TERRAIN_FAR_HLSL;
inline constexpr const char* kClayTerrainFarDepthPs = "#define CLAY_DEPTH_ONLY 1\n" FABLE2_CLAY_TERRAIN_FAR_HLSL;

// The shadow maps' pixel shader (render/clay_pass.cpp; clay_logic.h, "Native shadow maps"): depth only, no
// colour output. It is drawn with kClayVs, whose first three outputs it takes (the registers are assigned
// by position), in a draw without light (light_mode 0). It writes the pixel's depth with the game's
// polygon offset: the rasterised depth (clip z, the native map's direction: 0 at the light's near plane, 1
// the clear value) plus the slope term times the depth's larger screen derivative plus the constant term,
// saturated (ShadowMapDepth in clay_logic.h). The two terms are the bits of Draw's `pad2` (slope) and
// `color` (constant), which a depth-only draw has no other use for (MakeShadowConstants); both are 0 for a
// draw without the offset. A caster whose alpha decides (textured != 0: the leaf cards, the cut-out meshes,
// the bushes) samples its texture at t3 as kClayTexturedPs does and discards by the alpha test (alpha_mode
// bits 0-3, alpha_ref) and then by alpha-to-mask (bit 8) as on ONE sample a pixel, which is what a map has:
// kept iff alpha >= 1 - offset / 4, the offset of the pixel's parity from bits 16-23 (the leaf cards' 0xAA:
// 0.5 everywhere; ShadowAlphaToMaskThreshold). The sample count's bits (24-25) are not read. Writing the
// depth turns early depth tests off for these draws; the maps are small.
inline constexpr const char* kClayShadowPs = FABLE2_CLAY_DRAW_HLSL R"hlsl(
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
float main(float4 pos : SV_Position, float3 ndc : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Depth {
  if (textured != 0) {
    float4 t;
    if (sampler_index == 0) t = albedo.Sample(s_point_wrap, uv);
    else if (sampler_index == 1) t = albedo.Sample(s_linear_wrap, uv);
    else if (sampler_index == 2) t = albedo.Sample(s_point_clamp, uv);
    else t = albedo.Sample(s_linear_clamp, uv);
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
      if (!(a >= 1.0 - offset * 0.25)) discard;
    }
  }
  float slope = max(abs(ddx(pos.z)), abs(ddy(pos.z)));
  return saturate(pos.z + asfloat(pad2) * slope + asfloat(color));
}
)hlsl";

#undef FABLE2_CLAY_TERRAIN_FAR_HLSL
#undef FABLE2_CLAY_TERRAIN_LAYER_HLSL
#undef FABLE2_CLAY_TERRAIN_COMMON_HLSL
#undef FABLE2_CLAY_TEXTURED_HLSL
#undef FABLE2_CLAY_OBJECT_HLSL
#undef FABLE2_CLAY_LIGHT_HLSL
#undef FABLE2_CLAY_DRAW_HLSL

// The screen mask of the sun's shadow (render/clay_pass.cpp; clay_logic.h, "Native shadows"; kFullscreenVs
// draws it): one full-screen step into a single-channel target of the clay target's size, 1 = lit. It is
// the arithmetic of the game's mask shader 0x0F99810F072BF0FC, light_formulas.py's mask_0f99 (mask_near,
// mask_far, mask_combine; docs/native-renderer/lighting-formulas.md, "Shadows") term for term, in the
// native form (MaskPixel, MaskNear, MaskFar, MaskCombine in clay_logic.h are the mirrors, held to the
// reference's numbers):
//   depth = the pre-pass depth at the pixel (0 at the near plane, 1 the clear value)
//   S     = (X, Y, depth, 1), (X, Y) the pixel's centre in -1..1, Y up
//   P     = m_inv . S / w, the world position;  d = |P - m_eye|
//   near  = 0;  where d < split.x + split.z:  (a, b) = 2 * tile - 1, the tile's texel of the pixel (in the
//           game's pixel units, modulo 32), or (1, 0) without the tile;  (u, v, z) = m_near . P / w;
//           the share of the twelve taps at (u, v) + m_radius * (a x + b y, a y - b x) with z < the near
//           map's texel there (a point fetch, clamped: the texel of floor(coordinate * size))
//   far   = 0;  where split.x < d < split.y + 10:  (u, v, z) = m_far . P / w;  (x, y) = (u, v) * size;
//           the texels floor(x) - 1, floor(x) by floor(y) - 1, floor(y), each 1 where z <= the far map's
//           texel, weighted by frac(x) and frac(y) towards the higher texel
//   mask  = lerp(lerp(near, far, sat((d - split.x) / split.z)), split.w, sat(0.1 * (d - split.y)))
// The maps hold the caster's depth z, 1 where none is, so the game's 1 - z > map and 1 - z >= map are
// z < map and z <= map here. Every pixel is computed, one at the clear depth too: it is a far pixel and
// gets split.w, as in the game (NOT 1). The four textures are read with Load: no sampler, no filter of
// the hardware. Guards: a w that is zero or no number gives split.w for the pixel and 1 for a map; a tap
// whose coordinate is no number counts as lit; the texel indices are clamped as floats before they are
// integers. The twelve taps are unrolled: the compiled shader has no loop.
// kClayMaskFields is cbuffer Mask's layout (ClayMaskConstants in clay_logic.h), which the tests hold
// against the compiled shader and the C++ struct.
inline constexpr ClayBufferField kClayMaskFields[] = {
    {"m_inv0", 0, 16},     {"m_inv1", 16, 16},    {"m_inv2", 32, 16},       {"m_inv3", 48, 16},
    {"m_near0", 64, 16},   {"m_near1", 80, 16},   {"m_near2", 96, 16},      {"m_near3", 112, 16},
    {"m_far0", 128, 16},   {"m_far1", 144, 16},   {"m_far2", 160, 16},      {"m_far3", 176, 16},
    {"m_eye", 192, 12},    {"m_radius", 204, 4},  {"m_split", 208, 16},     {"m_inv_target", 224, 8},
    {"m_tile_scale", 232, 8}, {"m_rotate", 240, 4}, {"m_map_size", 244, 4}, {"m_pad", 248, 8},
};
inline constexpr const char* kClayMaskPs = R"hlsl(
cbuffer Mask : register(b0) {
  float4 m_inv0; float4 m_inv1; float4 m_inv2; float4 m_inv3;      // the inverse view-projection, rows
  float4 m_near0; float4 m_near1; float4 m_near2; float4 m_near3;  // world to the near map (u, v, depth, w): c36..c39
  float4 m_far0; float4 m_far1; float4 m_far2; float4 m_far3;      // world to the far map: c40..c43
  float3 m_eye; float m_radius;                                    // c19.xyz; the taps' radius in the map's units (2.5 texels)
  float4 m_split;                                                  // c12
  float2 m_inv_target; float2 m_tile_scale;                        // 1 / the target's size; game pixels a native pixel
  uint m_rotate; float m_map_size; float2 m_pad;                   // 1: the tile is bound; the maps' size in texels
};
Texture2D<float> scene_depth : register(t0);
Texture2D<float> near_map : register(t1);
Texture2D<float> far_map : register(t2);
Texture2D<float4> rotation : register(t3);
static const float2 kMaskTaps[12] = {
  float2(-0.20334500074386597, 0.6207159757614136), float2(0.18546099960803986, -0.893123984336853),
  float2(0.9623399972915649, -0.19498300552368164), float2(0.5194560289382935, 0.7670220136642456),
  float2(-0.6959139704704285, 0.45713698863983154), float2(0.5074309706687927, 0.06442499905824661),
  float2(-0.8401439785957336, -0.07357999682426453), float2(0.8964200019836426, 0.41245800256729126),
  float2(0.4734340012073517, -0.4800260066986084), float2(-0.7915589809417725, -0.5977100133895874),
  float2(-0.32621198892593384, -0.40580999851226807), float2(-0.32194000482559204, -0.9326149821281433),
};
// A number of usable size: false for a NaN (no comparison holds for it) and for infinity.
bool mask_number(float x) { return abs(x) < 3.0e38; }
float MaskNear(float4 P, float2 rot) {
  float w = dot(m_near3, P);
  if (!(abs(w) > 0.0 && mask_number(w))) return 1.0;
  float3 p = float3(dot(m_near0, P), dot(m_near1, P), dot(m_near2, P)) / w;
  if (!(mask_number(p.x) && mask_number(p.y) && mask_number(p.z))) return 1.0;
  float last = m_map_size - 1.0;
  float lit = 0.0;
  [unroll] for (int i = 0; i < 12; ++i) {
    float2 o = float2(rot.x * kMaskTaps[i].x + rot.y * kMaskTaps[i].y, rot.x * kMaskTaps[i].y - rot.y * kMaskTaps[i].x);
    float2 at = p.xy + m_radius * o;
    bool number = mask_number(at.x) && mask_number(at.y);
    float2 texel = number ? clamp(floor(at * m_map_size), 0.0, last) : float2(0.0, 0.0);
    float stored = near_map.Load(int3(int2(texel), 0));
    lit += (!number || p.z < stored) ? 1.0 : 0.0;
  }
  return lit * 0.0833333358168602;
}
float MaskFar(float4 P) {
  float w = dot(m_far3, P);
  if (!(abs(w) > 0.0 && mask_number(w))) return 1.0;
  float3 p = float3(dot(m_far0, P), dot(m_far1, P), dot(m_far2, P)) / w;
  if (!(mask_number(p.x) && mask_number(p.y) && mask_number(p.z))) return 1.0;
  float2 xy = p.xy * m_map_size;
  if (!(abs(xy.x) < 2147483648.0 && abs(xy.y) < 2147483648.0)) return 1.0;
  float2 cell = floor(xy);
  float2 f = xy - cell;
  float last = m_map_size - 1.0;
  int2 lo = int2(clamp(cell - 1.0, 0.0, last));
  int2 hi = int2(clamp(cell, 0.0, last));
  float lit00 = (p.z <= far_map.Load(int3(hi.x, hi.y, 0))) ? 1.0 : 0.0;
  float lit10 = (p.z <= far_map.Load(int3(lo.x, hi.y, 0))) ? 1.0 : 0.0;
  float lit11 = (p.z <= far_map.Load(int3(lo.x, lo.y, 0))) ? 1.0 : 0.0;
  float lit01 = (p.z <= far_map.Load(int3(hi.x, lo.y, 0))) ? 1.0 : 0.0;
  return f.x * f.y * lit00 + (1.0 - f.x) * f.y * lit10 + (1.0 - f.x) * (1.0 - f.y) * lit11 + f.x * (1.0 - f.y) * lit01;
}
float MaskCombine(float d, float near_term, float far_term) {
  float w = (m_split.z > 0.0) ? saturate((d - m_split.x) / m_split.z) : ((d > m_split.x) ? 1.0 : 0.0);
  float m = near_term + w * (far_term - near_term);
  float fade = saturate(0.1 * (d - m_split.y));
  return m + fade * (m_split.w - m);
}
float4 main(float4 pos : SV_Position) : SV_Target {
  uint2 pixel = uint2(pos.xy);
  float depth = scene_depth.Load(int3(pixel, 0));
  float4 S = float4((2.0 * pixel.x + 1.0) * m_inv_target.x - 1.0, 1.0 - (2.0 * pixel.y + 1.0) * m_inv_target.y, depth, 1.0);
  float beyond = m_split.w;
  float w = dot(m_inv3, S);
  if (!(abs(w) > 0.0 && mask_number(w))) return float4(beyond, beyond, beyond, 1.0);
  float3 world = float3(dot(m_inv0, S), dot(m_inv1, S), dot(m_inv2, S)) / w;
  float3 to_eye = world - m_eye;
  float d = sqrt(dot(to_eye, to_eye));
  if (!mask_number(d)) return float4(beyond, beyond, beyond, 1.0);
  float4 P = float4(world, 1.0);
  float near_term = 0.0;
  float far_term = 0.0;
  [branch] if (m_split.x + m_split.z > d) {
    float2 rot = float2(1.0, 0.0);
    [branch] if (m_rotate != 0) {
      uint2 tile = uint2(floor((float2(pixel) + 0.5) * m_tile_scale)) % 32;
      rot = 2.0 * rotation.Load(int3(tile, 0)).xy - 1.0;
    }
    near_term = MaskNear(P, rot);
  }
  [branch] if (d > m_split.x && m_split.y + 10.0 > d) far_term = MaskFar(P);
  float m = MaskCombine(d, near_term, far_term);
  return float4(m, m, m, 1.0);
}
)hlsl";

// Output mapping of the clay pass (render/clay_pass.cpp; kFullscreenVs draws it): the linear target's
// value times the buffer's first word, as a display value in the 8-bit target the composite samples:
// display = sqrt(saturate(linear * exposure)), alpha 1. The pass sets that word to 1 in every frame
// (kClayMapExposure): the exposure of a lit frame is in the lit draws' own values (Frame's f_exposure),
// so that it does not reach the unlit draws and the clear. The linear target is read texel for texel (the
// two targets have one size). A negative value, a NaN (no comparison holds for it) and 0 * infinity
// give 0, +infinity gives 1: nothing but a value in [0, 1] reaches the root (OutputMap in clay_logic.h
// is the same rule).
inline constexpr const char* kOutputMapPs = R"hlsl(
cbuffer Map : register(b0) { float exposure; float3 map_pad; };
Texture2D<float4> linear_target : register(t0);
float4 main(float4 pos : SV_Position) : SV_Target {
  float3 v = linear_target.Load(int3(pos.xy, 0)).rgb * exposure;
  v = (v > 0.0) ? min(v, 1.0) : float3(0.0, 0.0, 0.0);
  return float4(sqrt(v), 1.0);
}
)hlsl";

// Debug views (render/composite.cpp, fable2_native_view): the clay pass's mapped (8-bit, display) target
// drawn over the guest output. mode 1 overlay (alpha 0.5), 2 split (right half only), 3 native (opaque).
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

// The shadow view (render/composite.cpp, fable2_native_view=shadow; evidence shots, no key): the pass's two
// shadow maps and its screen mask over the whole output. Left, a column of two squares of side out_h / 2:
// the near map above the far map. Right of it, in what is left of the width, the mask, as large as fits
// with the clay target's shape, centred.
// The maps: grey = the stored depth as it is: 0 (black) at the light's near plane, growing with the
// distance from the light, 1 (white) where no caster is (the clear value); so casters are grey shapes on
// white, darker the nearer they are to the light. A screen pixel shows the smallest depth (the nearest
// caster) of the 2 x 2 texels at its place, so that a caster a texel wide stays visible when 1024 texels
// are shown on fewer pixels; casters look up to a texel fatter for it.
// The mask: grey = its value, white lit, black in full shadow, the mid grey of split.w beyond the shadow
// distance and on the sky; the texel at the pixel's place (a point read: the tap pattern is not smoothed).
// All three are read without a sampler. Outside the panels: dark blue-grey. flags bit 0 clear (the maps
// were not drawn for the frame shown: no shadow block, the shadows or the light off): both squares flat
// dark blue; bit 1 clear (no mask was written for the frame): the mask's panel flat dark blue; never a
// map or a mask of another frame.
inline constexpr const char* kShadowViewPs = R"hlsl(
cbuffer C : register(b0) { float out_w; float out_h; uint flags; float map_size; float mask_w; float mask_h; float2 view_pad; };
Texture2D<float> near_map : register(t0);
Texture2D<float> far_map : register(t1);
Texture2D<float4> mask : register(t2);
float4 main(float4 pos : SV_Position) : SV_Target {
  float side = out_h * 0.5;
  if (pos.x < side) {
    if (pos.y >= 2.0 * side) return float4(0.02, 0.02, 0.05, 1.0);
    if ((flags & 1) == 0) return float4(0.0, 0.0, 0.25, 1.0);
    bool far_side = pos.y >= side;
    float2 at = float2(pos.x, far_side ? pos.y - side : pos.y) / side;
    float last = map_size - 1.0;
    int2 t0 = int2(min(at * map_size, last));
    int2 t1 = min(t0 + 1, int2(last, last));
    float d;
    if (far_side) {
      d = min(min(far_map.Load(int3(t0, 0)), far_map.Load(int3(t1.x, t0.y, 0))),
              min(far_map.Load(int3(t0.x, t1.y, 0)), far_map.Load(int3(t1, 0))));
    } else {
      d = min(min(near_map.Load(int3(t0, 0)), near_map.Load(int3(t1.x, t0.y, 0))),
              min(near_map.Load(int3(t0.x, t1.y, 0)), near_map.Load(int3(t1, 0))));
    }
    d = saturate(d);
    return float4(d, d, d, 1.0);
  }
  float room_w = out_w - side;
  float scale = min(room_w / mask_w, out_h / mask_h);
  float2 size = float2(mask_w, mask_h) * scale;
  float2 at = (pos.xy - float2(side + (room_w - size.x) * 0.5, (out_h - size.y) * 0.5)) / size;
  if (at.x < 0.0 || at.x >= 1.0 || at.y < 0.0 || at.y >= 1.0) return float4(0.02, 0.02, 0.05, 1.0);
  if ((flags & 2) == 0) return float4(0.0, 0.0, 0.25, 1.0);
  int2 t = int2(min(at * float2(mask_w, mask_h), float2(mask_w - 1.0, mask_h - 1.0)));
  float m = saturate(mask.Load(int3(t, 0)).r);
  return float4(m, m, m, 1.0);
}
)hlsl";

}  // namespace fable2::native::shaders
