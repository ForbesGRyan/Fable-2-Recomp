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
// range it drops the clamp, and it keeps it for the terrain layers' mix). The
// clamped value is then multiplied by the frame's unlit word (Frame's f_post:
// 1 in every frame but a post frame, where it is the frame's unlit scale, a
// number in (0, 64]: clay_logic.h, "Native post"), before the draw's fog. A lit
// draw (light_mode bit 2, below) writes its light function's value times the
// frame's exposure (Frame's f_exposure; no other value takes it: an unlit draw
// takes the unlit word and the clear is written as it is, and the output
// mapping multiplies by 1, so lit and unlit values have one unit in the target
// and a blend of the two is consistent; in a post frame the exposure is the
// game's own output scale c20.w and the game's composite follows in place of
// the mapping), without the facet shade and without the clamp at 1,
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
// A draw without bit 2 reads nothing of Object and of Frame only the unlit word
// (f_post, in its pixel shader; with the fog's bits also the fog's fields) and is
// the unlit colour.
// kClayFrameFields and kClayObjectFields are the two buffers' layouts, which the
// tests hold against the compiled shaders and the C++ structs.
struct ClayBufferField {
  const char* name;  // the variable in the cbuffer
  unsigned offset;   // bytes from the start of the buffer
  unsigned bytes;
};
inline constexpr ClayBufferField kClayFrameFields[] = {
    {"f_sun_dir", 0, 12},  {"f_light_on", 12, 4},  {"f_sun_col", 16, 12}, {"f_tint_pow", 28, 4},
    {"f_tint", 32, 12},    {"f_exposure", 44, 4},  {"f_add", 48, 12},     {"f_post", 60, 4},
    {"f_floor", 64, 12},   {"f_mask_du", 76, 4},   {"f_axis", 80, 12},    {"f_mask_dv", 92, 4},
    {"f_eye", 96, 12},     {"f_eye_on", 108, 4},
};
// The fog's tail of cbuffer Frame (ClayFrameConstants from `fog` on; clay_logic.h, "Native fog"), which
// the colour programs of the four pixel shaders that draw the scene declare (CLAY_FOG: the three that
// light and, since the unlit draws' fog, the flat shader) and the sky step's shader: the vertex shader, the
// shadow maps' shader and the depth-only programs declare the light's 112 bytes and are the programs they
// were. The fog's
// constants, the rows of the frame's inverse view-projection, and the game's two fog tables as arrays
// (64 and 256 texels, rgb): no binding parameter was left for two textures, and an array read by hand
// is the game's table read exactly (the controller's ruling of 2026-10-09). All zero in a frame
// without fog: f_fog_on = 0 and no pixel reads the rest.
inline constexpr ClayBufferField kClayFrameFogFields[] = {
    {"f_fog_axis", 112, 12},  {"f_fog_on", 124, 4},     {"f_fog_near", 128, 12},   {"f_fog_start", 140, 4},
    {"f_fog_slope", 144, 4},  {"f_fog_scale", 148, 4},  {"f_fog_amount", 152, 4},  {"f_fog_scatter", 156, 4},
    {"f_inv0", 160, 16},      {"f_inv1", 176, 16},      {"f_inv2", 192, 16},       {"f_inv3", 208, 16},
    {"f_scatter", 224, 1024}, {"f_transmit", 1248, 4096},
};
inline constexpr unsigned kClayFrameLightSize = 112, kClayFrameSize = 5344;
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
// The fog (clay_logic.h, "Native fog"; light_formulas.py: fog, vertex_fog with fog_apply, table_1d): under
// CLAY_FOG, which the pixel shaders that draw the scene define for their colour programs
// (FABLE2_CLAY_FOG_HLSL: the three that light and the flat shader). FogCoords
// rebuilds the pixel's world position from its own SV_Position through the frame's inverse
// view-projection (the mask step's arithmetic; FogPixelWorld is the mirror) and gives the fog's three
// coordinates; FogForm applies the form the draw has: the ground form (0), the trees' (1, with the draw's
// scale), or none (2). fog_scatter and fog_transmit read the two tables as the game's fetch does: texel
// centres at (i + 0.5) / N, linear, clamped (FogTableRead). The bound of the first index i is the float
// clamp of the texel coordinate to 0..N - 1 before it is made an integer (saturate, then max and min,
// which give a number for one that is none): the compiler removes the integer minimum on i as implied by
// it and keeps the one on i + 1 (review of plan Task 4, M1; the shader test holds the compiled clamps).
// Guards, each a comparison that is false for a
// value that is no number (no test a compiler could fold away): a w that is zero or not below 3e38, a
// squared distance that is not above 0 and below 1e30 (kFogDistance2Max), a distance not past the start,
// and the frame's flag leave the pixel as it is, and its tables unread ([branch]). The distance is
// d2 * rsqrt(d2): the pixel shaders call no square root (the tests hold that of their colour paths).
// Without CLAY_FOG the same names do nothing: the programs are those without fog.
// FogUnlit (clay_logic.h, "Unlit draws"; UnlitFogMirror; light_formulas.fog with its scatter_scale): the
// ground form for a draw the pass does not light, on its unlit colour: the in-scatter times the frame's
// exposure, which every lit value is multiplied by after its fog and an unlit value is not, and times
// `scale` (1, or the alpha the draw writes for a shader of the game's blended form). Called only under
// light_mode's bit 7 (128), which the pass sets for an unlit draw of a fogged frame whose pixel shader
// has the game's fog block; with the bit clear, and with the frame's flag clear, the unlit programs are
// what they were (the shader test compares them line for line). The fogged value is bounded like a lit
// one (clay_lit: a number, not negative, at most 64), inside the branch: with an amount c28.x above 1
// (the block allows up to 4) the blend could otherwise go below 0, and a blended draw would darken
// what is under it (final review, M2).
// FogUnlitTree (final review, M1; UnlitTreeFogMirror): an unlit trunk or leaf, under light_mode's bit 10
// (1024, without bits 7 and 8): the trees' form (FogForm's form 1) on the unlit colour, bounded the same
// way. `scale`: the draw's in-scatter scale (vertex c8.x) times the frame's exposure, which the pass
// multiplies on the CPU and hands over as the float bits of Draw's `pad2` (in a colour draw the word has
// no other use; the shadow maps' shader reads its own slope bias there).
// Bit 9 (512), read by the lit path of the textured shader alone: the draw is lit and its own pixel
// shader in the game has no fog (the plant form 26052DEF...): its fog point is marked unfogged, and its
// program is then the lit program without the fog (the shader test compares the two).
#define FABLE2_CLAY_LIGHT_HLSL R"hlsl(
cbuffer Frame : register(b2) {
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
  float4 o_leaf;                                 // a leaf: x vertex c50.w (the wrap of the front light), y 1 / c7.w;
                                                 // the draw's fog: w the form (0 the ground form, 1 the trees', 2
                                                 // none), z the trees' scale (vertex c8.x)
#ifdef CLAY_WIND
  float4 o_wind_reg[3];                          // the wind vertex program alone (kClayWindVs): a tree's vertex c15,
                                                 // c16, c17; an instanced plant's (c10.x, c25.x, c26.x, c26.y),
                                                 // (c21.xyz, c7.x + c7.y), (c22.xyz, 0)
  float4 o_wind_lit[7];                          // the family's literals by slot (render::ClayWindConstants)
#endif
};
)hlsl"

#define FABLE2_CLAY_VS_HLSL FABLE2_CLAY_DRAW_HLSL FABLE2_CLAY_LIGHT_HLSL FABLE2_CLAY_OBJECT_HLSL R"hlsl(
StructuredBuffer<float4> positions : register(t0);
StructuredBuffer<uint> indices : register(t1);
StructuredBuffer<float2> uvs : register(t2);
#ifdef CLAY_WIND
// The wind (clay_logic.h, "Native wind"; docs/native-renderer/lighting-formulas.md, "Wind"): only under
// CLAY_WIND, which kClayWindVs alone defines. The per-vertex wind words at t9 (render/wind_decode.h: a trunk
// vertex 4 floats, a leaf vertex 10, an instanced plant's flat position 2) and the draw's wind constants in the
// tail of cbuffer Object. The three functions are light_formulas.wind_plant, wind_trunk (wind_tree_terms,
// wind_tree_offset) and wind_leaf (wind_leaf_lift) term for term, in single precision in the reference's order
// of operations and marked precise (ledger ruling 35); their C++ mirrors are WindPlantMirror, WindTrunkMirror
// and WindLeafMirror. A literal is read by its slot's name and no number of the table stands here as text: the
// names below are light_formulas.WIND_LITERAL_ORDER in its order (W_: a tree's 28, of which a trunk has the
// first 18; P_: a plant's 7), which tests/test_wind_literals.py holds name by name.
StructuredBuffer<float> wind_words : register(t9);
#define W_half o_wind_lit[0].x
#define W_base o_wind_lit[0].y
#define W_rate1 o_wind_lit[0].z
#define W_rate2 o_wind_lit[0].w
#define W_two_pi o_wind_lit[1].x
#define W_quarter o_wind_lit[1].y
#define W_lean o_wind_lit[1].z
#define W_gust o_wind_lit[1].w
#define W_phase_x o_wind_lit[2].x
#define W_phase_y o_wind_lit[2].y
#define W_lo o_wind_lit[2].z
#define W_amp o_wind_lit[2].w
#define W_along o_wind_lit[3].x
#define W_across o_wind_lit[3].y
#define W_rate0 o_wind_lit[3].z
#define W_zero o_wind_lit[3].w
#define W_sel_zero o_wind_lit[4].x
#define W_sel_one o_wind_lit[4].y
#define W_hundred o_wind_lit[4].z
#define W_slack o_wind_lit[4].w
#define W_phase_f o_wind_lit[5].x
#define W_flutter_x o_wind_lit[5].y
#define W_flutter_y o_wind_lit[5].z
#define W_one o_wind_lit[5].w
#define W_back o_wind_lit[6].x
#define W_push_gust o_wind_lit[6].y
#define W_lift_gust o_wind_lit[6].z
#define W_rate_f o_wind_lit[6].w
#define P_lo o_wind_lit[0].x
#define P_two_pi o_wind_lit[0].y
#define P_inv_two_pi o_wind_lit[0].z
#define P_gain o_wind_lit[0].w
#define P_height o_wind_lit[1].x
#define P_half o_wind_lit[1].y
#define P_unit o_wind_lit[1].z
float WindFrac(float x) { return x - floor(x); }
// A phase as the tree shaders wrap it, in [-pi, pi): its sine is sin(2 pi q).
float WindPhase(float q) { return WindFrac(WindFrac(q) + W_half) * W_two_pi + W_lo; }
// An instanced plant: o_wind_reg = (c10.x, c25.x, c26.x, c26.y), (c21.xyz, c7.x + c7.y), (c22.xyz, 0); `seed` A3 + B3
// of the vertex's copy, `z` the mesh vertex's third coordinate. A vertex at or below its root stands.
float3 WindPlant(float3 p, float seed, float z) {
  precise float f = WindFrac(o_wind_reg[1].w + seed);
  precise float theta = 1.0 - o_wind_reg[0].x * o_wind_reg[0].y * (f * P_gain + 1.0) + f;
  theta = WindFrac(theta * P_inv_two_pi + P_half) * P_two_pi + P_lo;
  precise float s = saturate(P_height * z) * cos(theta);
  precise float3 moved = p + s * (o_wind_reg[1].xyz * o_wind_reg[0].z + o_wind_reg[2].xyz * o_wind_reg[0].w);
  return moved;
}
// What the wind adds to a trunk vertex and to a leaf's pivot: o_wind_reg = c15, c16, c17; `w` = (b, a, d, c),
// the four weights in the fetch's source order.
float3 WindTreeOffset(float4 w) {
  float3 c15 = o_wind_reg[0].xyz;
  float4 c16 = o_wind_reg[1];
  float4 c17 = o_wind_reg[2];
  precise float t = c15.x;
  precise float g = c17.w * W_gust;
  precise float s0 = sin(WindPhase(t * W_rate0));
  precise float s1 = sin(WindPhase(w.x * W_rate0 + (w.z * c16.x + ((c16.y + W_rate1) * t + g))));
  precise float s2 = sin(WindPhase(w.z * c16.x + ((c16.y + W_rate2) * t + g)));
  precise float dd = w.z * c17.z * w.z;
  precise float along = dd * W_along * (W_amp * s1 * (abs(W_amp * s0) * W_quarter + W_base) + c17.z * W_lean);
  precise float across = dd * W_across * s2;
  precise float s3 = sin(WindPhase(t * W_phase_y + w.x));
  precise float turn = c16.z * t + g;
  precise float s4 = sin(WindPhase((turn + W_phase_x) * w.w + w.x));
  precise float s5 = sin(WindPhase((turn + W_phase_y) * w.w + w.x));
  precise float scale = (W_amp * s3 + W_half) * w.y * (W_half * c17.z) * c15.z;
  precise float u = W_amp * s4 * scale;
  precise float v = W_amp * s5 * scale;
  precise float3 off = float3(along * c17.x - across * c17.y + u, along * c17.y + across * c17.x + v,
                              -(along * along) * W_quarter - (u * u + v * v) * W_half);
  return off;
}
float3 WindTrunk(float3 p, float4 w) {
  precise float3 moved = p + WindTreeOffset(w);
  return moved;
}
// A leaf: its pivot `q` sways as a trunk vertex does, and the vertex keeps its distance from it while the third
// coordinate of its direction is lifted by the flutter (`v`: its three values, source x, y, z). Where the moved
// offset has zero squared length in this precision (a vertex at its pivot whose lift is 0) the position is the
// moved pivot, and nothing is divided by that length (ledger ruling 36).
float3 WindLeaf(float3 p, float3 q, float4 w, float3 v) {
  float4 c15 = o_wind_reg[0];
  float4 c16 = o_wind_reg[1];
  float4 c17 = o_wind_reg[2];
  precise float3 pivot = q + WindTreeOffset(w);
  precise float3 d = p - q;
  precise float len = sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
  precise float t = c15.x;
  precise float g = c17.w * W_gust;
  precise float clock = t + c17.w * W_rate_f;
  precise float s6 = sin(WindPhase(w.x * W_rate_f + (((c16.y + W_rate1) * t + g) + c16.x * w.z + W_phase_f)));
  precise float s7 = sin(WindPhase((clock * W_flutter_x + v.z) + W_hundred * v.x));
  precise float turn = WindFrac(clock * W_flutter_y + v.y);
  precise float s8 = sin(WindFrac((turn + turn) * W_half + W_half) * W_two_pi + W_lo);
  precise float k = (-(W_amp * s6) > 0.0) ? W_back * s6 : W_amp * s6;
  precise float push = W_amp * s7 * max(len + W_slack, 0.0) * c16.w;
  precise float lift = c15.w * (abs(k) * W_amp * s8) * k;
  d.z += push * (c17.z * W_push_gust + W_one) + lift * (c17.z * W_lift_gust + W_one);
  precise float turned2 = d.x * d.x + d.y * d.y + d.z * d.z;
  precise float3 moved = pivot;
  [branch] if (turned2 > 0.0) moved = pivot + d / sqrt(turned2) * len;
  return moved;
}
#else
StructuredBuffer<float2> terrain_normals : register(t9);
#endif
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
#ifdef CLAY_WIND
  // The wind: the position that goes through the rows is the moved one, for a vertex the draw names (below
  // vertex_count: the wind words hold one run for every such vertex, and are read nowhere else) of a draw whose
  // light_mode has a wind family (bits 12 and 13: 1 a trunk, 2 a leaf, 3 an instanced plant). The cut below, the
  // world position handed on and every light input stay those of the position at rest.
  uint wind_family = (light_mode >> 12) & 3;
  [branch] if (in_range && wind_family != 0) {
    precise float3 moved = p.xyz;
    [branch] if (wind_family == 1) {
      uint at = uint(v) * 4;
      moved = WindTrunk(p.xyz, float4(wind_words[at], wind_words[at + 1], wind_words[at + 2], wind_words[at + 3]));
    } else {
      [branch] if (wind_family == 2) {
        uint at = uint(v) * 10;
        moved = WindLeaf(p.xyz, float3(wind_words[at], wind_words[at + 1], wind_words[at + 2]),
                         float4(wind_words[at + 3], wind_words[at + 4], wind_words[at + 5], wind_words[at + 6]),
                         float3(wind_words[at + 7], wind_words[at + 8], wind_words[at + 9]));
      } else {
        uint at = uint(v) * 2;
        moved = WindPlant(p.xyz, wind_words[at], wind_words[at + 1]);
      }
    }
    float4 m = float4(moved, p.w);
    c = layout == 0 ? float4(dot(r0, m), dot(r1, m), dot(r2, m), dot(r3, m)) : m.x * r0 + m.y * r1 + m.z * r2 + m.w * r3;
  }
#endif
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
#ifndef CLAY_WIND
  [branch] if ((light_mode & 1) && has_light) {
    // The terrain normal as the game's vertex shaders form it, not normalised (the pixel shader does).
    float2 t = terrain_normals[v];
    o.normal = float3(t, sqrt(abs(1.0 - dot(t, t))));
    if (light_mode & 4) o.axis = AmbientAxis(o.normal, f_axis);
  }
#endif
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
)hlsl"
inline constexpr const char* kClayVs = FABLE2_CLAY_VS_HLSL;
// The wind vertex program (clay_logic.h, "Native wind"): the clay vertex shader's own text under CLAY_WIND, in
// which t9 is the draw's wind words (a wind record is never a terrain record: the terrain block is not
// compiled), cbuffer Object ends in the wind's constants (448 bytes: kClayWindObjectFields), and the position
// that goes through the rows is moved by the family's function. The pass draws a record with it only together
// with the record's wind words, its wind constants and light_mode's wind bits, in every pass that draws the
// record (colour, depth pre-pass, shadow map). Without the define every program is the one it was (the shader
// test holds the compiled bytes of all nine clay programs to those of before the wind).
inline constexpr const char* kClayWindVs = "#define CLAY_WIND 1\n" FABLE2_CLAY_VS_HLSL;
// cbuffer Object's tail under CLAY_WIND (render::ClayWindConstants at render::kClayWindOffset).
inline constexpr ClayBufferField kClayWindObjectFields[] = {{"o_wind_reg", 288, 48}, {"o_wind_lit", 336, 112}};
inline constexpr unsigned kClayWindObjectSize = 448;

// The colour programs of the pixel shaders that draw the scene define CLAY_FOG: the fog's tail of
// cbuffer Frame and the fog's functions (the depth-only programs do not, and are the programs without fog).
#define FABLE2_CLAY_FOG_HLSL "#ifndef CLAY_DEPTH_ONLY\n#define CLAY_FOG 1\n#endif\n"

// Flat clay: the record's debug colour, shaded by the facet. It has no light. Since the unlit draws'
// fog (clay_logic.h, "Unlit draws") it declares the frame's block for one thing: under light_mode's
// bit 7 the colour is fogged (FogUnlit, scale 1: a flat draw writes no alpha of its own), under bit 10
// with the trees' form (FogUnlitTree: an unlit trunk or leaf drawn flat). With both bits clear it reads
// the frame's unlit word alone (f_post, which its colour is multiplied by). (So in a fogged frame a flat draw of such a shader shows its debug colour through
// the haze; --fable2_native_fog=off gives the plain colours.)
inline constexpr const char* kClayPs = FABLE2_CLAY_FOG_HLSL FABLE2_CLAY_DRAW_HLSL FABLE2_CLAY_LIGHT_HLSL R"hlsl(
float3 clay_linear(float3 d) { d = saturate(d); return d * d; }
float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0) : SV_Target {
  float3 n = normalize(cross(ddx(ndc), ddy(ndc)));
  float shade = 0.35 + 0.65 * saturate(abs(dot(n, normalize(float3(0.4, 0.6, -0.7)))));
  float3 base = float3((color >> 16) & 255, (color >> 8) & 255, color & 255) / 255.0;
  if (light_mode & 1024) return float4(FogUnlitTree(clay_linear(base * shade) * f_post, FogCoords(pos), asfloat(pad2)), 1.0);
  if (light_mode & 128) return float4(FogUnlit(clay_linear(base * shade) * f_post, FogCoords(pos), 1.0), 1.0);
  return float4(clay_linear(base * shade) * f_post, 1.0);
}
)hlsl";

// A lit textured draw (light_mode bit 2: an object or a tree trunk; lighting-formulas.md, "Objects",
// "Tree trunks"): colour = (A * c27.w * (pow(vcolour, c21.w) * c21.xyz * L + c20.xyz) + A * c87.xyz *
// sat(N . c80.xyz) * mask) * c49.xyz, times c26.xyz, with A = albedo^2 (after the division by the
// alpha where the shader has it), vcolour the vertex shader's colour (the stream colour raised to its
// literal, or the probe colour as it is), N the interpolated M n normalised, L from
// t = sat(normalize(axis) . N), and the mask the frame's screen mask at the pixel (t4: the sun's shadow,
// 1 = lit; the 1 x 1 white texture in a frame without one; clay_logic.h, "Native shadows"), read as the
// game's lit shaders fetch tf8: with the linear clamped sampler at the pixel's CORNER over the target's
// size ((pos.xy - 0.5): the game's floor(pixel) * c0.zw + c0.xy), which is the mean of the four mask
// texels (px - 1, px) x (py - 1, py) (MaskReadValue in clay_logic.h), by a lit pixel only. The pixel
// shader is the same program for both kinds of colour and reads none of the probe registers.
// kClayTexturedDepthPs is the same text compiled with CLAY_DEPTH_ONLY: the shader of the depth pre-pass
// for a record of this kind. It makes the same alpha and discards the same pixels (the alpha test,
// alpha-to-mask) and then returns: no light, no mask, no colour (the draw writes none).
// The fog (clay_logic.h, "Native fog"): after the family's lit colour, an object's and a tree's c49
// included, and before c26 and the exposure, which stands in for the game's output scale: the game's
// place. A plant and an object take the ground form; a trunk and a leaf the trees' own, from the object
// block's last register (an instanced plant's block ends before it and is not read there).
// A draw without light (light_mode bit 2 clear) under bit 7: its unlit colour through FogUnlit, with the
// alpha it writes as the in-scatter's scale under bit 8 (the game's blended form); under bit 10 (an unlit
// trunk or leaf) through FogUnlitTree. A lit draw under bit 9 is not fogged (its game shader has no fog).
#define FABLE2_CLAY_TEXTURED_HLSL FABLE2_CLAY_FOG_HLSL FABLE2_CLAY_DRAW_HLSL FABLE2_CLAY_LIGHT_HLSL FABLE2_CLAY_OBJECT_HLSL R"hlsl(
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
#ifdef CLAY_COVERAGE
#define main clay_textured
#endif
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
#ifdef CLAY_COVERAGE
    {
      uint2 cp = uint2(pos.xy);
      uint cindex = ((cp.x & 1) << 1) | (cp.y & 1);
      float t0 = 0.5 - float((alpha_mode >> (16 + 2 * cindex)) & 3) * 0.125;
      int covered = (a >= t0 ? 1 : 0) + (a >= t0 + 0.5 ? 1 : 0);
      if ((alpha_mode & 0x100) == 0 || ((alpha_mode >> 24) & 3) != 1 || covered != 1) discard;
    }
#endif
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
}
#ifdef CLAY_COVERAGE
#undef main
float4 main(float4 pos : SV_Position, float3 ndc : TEXCOORD0, float2 uv : TEXCOORD1, float3 world : TEXCOORD2,
            float3 normal : TEXCOORD3, float3 axis : TEXCOORD4, float3 vcolour : TEXCOORD5,
            float4 bend : TEXCOORD6) : SV_Target {
  return float4(clay_textured(pos, ndc, uv, world, normal, axis, vcolour, bend).rgb, 0.5);
}
#endif
)hlsl"
inline constexpr const char* kClayTexturedPs = FABLE2_CLAY_TEXTURED_HLSL;
inline constexpr const char* kClayTexturedDepthPs = "#define CLAY_DEPTH_ONLY 1\n" FABLE2_CLAY_TEXTURED_HLSL;
// The half program of a coverage record (clay_logic.h, "Coverage"; sub-project 11, phase 11b): the textured
// shader's own text under CLAY_COVERAGE, in which, after the alpha test and today's alpha-to-mask discard, a pixel
// is kept only where exactly ONE of the scene's two samples is covered (t0 = 0.5 - offset / 8 the first sample's
// threshold, t0 + 0.5 the second's: covered = (a >= t0) + (a >= t0 + 0.5), CoveredSamples), and only under
// alpha-to-mask on two samples (anything else is discarded: the pass gives this program to no other draw), and
// the output's alpha is the constant one half whatever path made the colour (the entry is a wrapper about the
// text's own main). The pass blends it source alpha over inverse source alpha: the game's resolve of one covered
// sample of two. Without the define the text compiles to the programs it compiled to before (the shader test
// holds the bytes of the colour and the depth-only program).
inline constexpr const char* kClayTexturedHalfPs = "#define CLAY_COVERAGE 1\n" FABLE2_CLAY_TEXTURED_HLSL;

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
// tf3 here is what the game's fetch gives: every terrain row's fetch constant has the gamma sign on
// r, g, b (sign fields 3, 3, 3, 0), so the console takes the filtered texel through its piecewise
// linear curve before the shader sees it (base_linear, under the record's word t_base_gamma:
// TerrainBaseGamma in clay_logic.h; frame-map section 17, "The base map's gamma sign"). Read as stored,
// the ambient part of all lit ground was 1.8 to 2.5 times the game's.
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
    {"t_base_sampler", 176, 4},  {"t_c11x", 180, 4},          {"t_facing_scale", 184, 4},  {"t_base_gamma", 188, 4},
};

#define FABLE2_CLAY_TERRAIN_COMMON_HLSL FABLE2_CLAY_FOG_HLSL FABLE2_CLAY_DRAW_HLSL FABLE2_CLAY_LIGHT_HLSL R"hlsl(
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
  uint t_base_sampler; float t_c11x; float t_facing_scale; uint t_base_gamma;  // lit: c11.x; layer: the literal 8;
                                                       // bits 0 to 2: tf3's r, g, b are fetched with the gamma sign
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
    float mask = shadow_mask.SampleLevel(s_linear_clamp, (pos.xy - 0.5) * float2(f_mask_du, f_mask_dv), 0).r;
    lin = LightTerrainLayer(lin, sample_base(base_at, base_d).rgb, f_tint, f_tint_pow, f_add, f_floor,
                            AmbientT(axis, normal), dot(N, f_sun_dir), f_sun_col, t_c11x, SunGateSingle(mask, facing));
    // The fog (the ground form), on the layer's own colour before its blend, as the game fogs each layer.
    lin = FogForm(lin, FogCoords(pos), 0.0, 0.0);
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
    // The fog (the ground form) before the cross-fade: 2F8A multiplies by its fade after its fog block.
    lin = FogForm(lin, FogCoords(pos), 0.0, 0.0);
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

// The sky step (render/clay_pass.cpp; clay_logic.h, "Native sky"): one full-screen triangle in the colour
// pass, after the opaque records and the terrain layers and before the first blended record (SkyPlace).
// kSkyVs puts it at the far depth of the pass's convention: clip z / w = 1, the value the depth is
// cleared to. The pipeline tests less-or-equal without depth write and without blend, so the triangle
// lands exactly where the depth is still the clear value: where nothing was drawn.
inline constexpr const char* kSkyVs = R"hlsl(
float4 main(uint id : SV_VertexID) : SV_Position {
  float2 uv = float2((id << 1) & 2, id & 2);
  return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 1.0, 1.0);
}
)hlsl";

// The sky's pixel shader: the game's 0xF7601FBE485936FD (light_formulas.py: sky, sky_latlong, sky_paths,
// sky_extinction; docs/native-renderer/lighting-formulas.md, "The sky") term for term; SkyMirror,
// SkyLatLong, SkyPaths, SkyExtinction and SkyPixelRay in clay_logic.h are the mirrors, held to the
// reference's numbers. Its own layout: cbuffer Sky at b0 (ClaySkyConstants, a small slot of the frame's
// constant buffer; kClaySkyFields is its layout), the frame's block at b2 with the fog's tail (the
// in-scatter table read by fog_scatter as the lit shaders read it, the axis c66, the exposure, 1 / the
// target's size), the two sky textures at t0 and t1, the four static samplers.
//   n     = the pixel's view ray: s_ray0 + X * s_rayx + Y * s_rayy, (X, Y) the pixel's centre in -1..1, Y
//           up; it reads no depth (SkyRayBasis); normalised
//   S     = fog_scatter(sat(n . c66));  k = |n.z|^0.2;  h = c67.y * (1.05 - k)
//   E     = exp2(-1.4426950 * ((h + (c31.x - h) * k) * c64 + (h + (c46.x - h) * k) * c65))
//   sky   = (1 - E) * S, and with the textures: lerp(sky, lerp(tf13^2, tf14^2, c47.x), their alpha)
// The lat-long coordinate is the game's own arithmetic with its literals (the exact singles), not atan2 /
// asin. The textures are sampled at an explicit level 0 (the game's fetch computes its level, and its
// derivative of u is a whole turn on the seam at -x); wrap or clamp as their fetch constants say, one
// mode for both axes (the static samplers'). Out through the exposure of the lit draws (the game's
// c20.w). Guards, each a comparison that is false for a value that is no number: a ray without a length
// or beyond 3e38 squared is (0, 0, 0) (light_normalize); the power of a zero |n.z| is 0; the ratio of the
// arctangent is 0 where both of |x|, |y| are 0 (the console's zero product); the textures are read inside
// a branch on the block's flag; clay_lit gives 0 for a result that is no number and at most 64.
inline constexpr ClayBufferField kClaySkyFields[] = {
    {"s_ray0", 0, 12},    {"s_c31x", 12, 4},    {"s_rayx", 16, 12},     {"s_c46x", 28, 4},
    {"s_rayy", 32, 12},   {"s_c47x", 44, 4},    {"s_ext1", 48, 12},     {"s_c67y", 60, 4},
    {"s_ext2", 64, 12},   {"s_textures", 76, 4}, {"s_fix13", 80, 8},    {"s_fix14", 88, 8},
    {"s_sampler13", 96, 4}, {"s_sampler14", 100, 4}, {"s_pad", 104, 8},
};
inline constexpr const char* kClaySkyPs = "#define CLAY_FOG 1\n" FABLE2_CLAY_LIGHT_HLSL R"hlsl(
cbuffer Sky : register(b0) {
  float3 s_ray0; float s_c31x;          // the view ray of a pixel: s_ray0 + X * s_rayx + Y * s_rayy; c31.x
  float3 s_rayx; float s_c46x;          // c46.x
  float3 s_rayy; float s_c47x;          // c47.x: the blend from the first texture to the second
  float3 s_ext1; float s_c67y;          // c64.rgb; c67.y
  float3 s_ext2; uint s_textures;       // c65.rgb; 1: the two textures are bound and read
  float2 s_fix13; float2 s_fix14;       // host texture scales
  uint s_sampler13; uint s_sampler14; float2 s_pad;   // static samplers of the two textures
};
Texture2D sky13 : register(t0);
Texture2D sky14 : register(t1);
SamplerState s_point_wrap : register(s0);
SamplerState s_linear_wrap : register(s1);
SamplerState s_point_clamp : register(s2);
SamplerState s_linear_clamp : register(s3);
float2 SkyLatLong(float3 n) {
  float3 a = abs(n);
  float hi = max(a.x, a.y);
  float lo = min(a.x, a.y);
  float q = (hi > 0.0) ? lo * (1.0 / hi) : 0.0;
  float q2 = q * q;
  float at = q * (q2 * (q2 * (q2 * (q2 * 0.02083509974181652 + -0.08513300120830536) + 0.18014100193977356) + -0.3302994966506958) + 0.9998660087585449);
  if (a.y > a.x) at = at + (-2.0 * at + 1.5707963705062866);
  if (n.x < 0.0) at = at + -3.1415927410125732;
  if (min(n.x, n.y) < 0.0 && max(n.x, n.y) >= 0.0) at = at - (at + at);
  float root = sqrt(abs(1.0 - a.z));
  float poly = a.z * (a.z * (a.z * -0.018729299306869507 + 0.07426100224256516) + -0.2121143937110901) + 1.5707287788391113;
  float ac = poly * root;
  if (n.z < 0.0) ac = ac + (-2.0 * root * poly + 3.1415927410125732);
  return float2(0.5 + at * 0.15915963053703308, (1.5707963705062866 - ac) * 0.6366385221481323);
}
float2 SkyPaths(float az) {
  float k = (az > 0.0) ? exp2(0.20000000298023224 * log2(az)) : 0.0;
  float h = s_c67y * (1.0499999523162842 - k);
  return float2(h + (s_c31x - h) * k, h + (s_c46x - h) * k);
}
float3 SkyExtinction(float2 l) {
  return exp2(-1.4426950216293335 * (l.x * s_ext1 + l.y * s_ext2));
}
float4 sky_sample(Texture2D t, uint index, float2 uv) {
  float4 c;
  if (index == 0) c = t.SampleLevel(s_point_wrap, uv, 0);
  else if (index == 1) c = t.SampleLevel(s_linear_wrap, uv, 0);
  else if (index == 2) c = t.SampleLevel(s_point_clamp, uv, 0);
  else c = t.SampleLevel(s_linear_clamp, uv, 0);
  return c;
}
float4 main(float4 pos : SV_Position) : SV_Target {
  float X = pos.x * (2.0 * f_mask_du) - 1.0;
  float Y = 1.0 - pos.y * (2.0 * f_mask_dv);
  float3 n = light_normalize(s_ray0 + X * s_rayx + Y * s_rayy);
  float3 S = fog_scatter(saturate(dot(n, f_fog_axis)));
  float3 E = SkyExtinction(SkyPaths(abs(n.z)));
  float3 sky = (1.0 - E) * S;
  [branch] if (s_textures != 0) {
    float2 uv = SkyLatLong(n);
    float4 s13 = sky_sample(sky13, s_sampler13, uv * s_fix13);
    float4 s14 = sky_sample(sky14, s_sampler14, uv * s_fix14);
    float3 col = s13.rgb * s13.rgb;
    col = col + s_c47x * (s14.rgb * s14.rgb - col);
    float alpha = s13.a + s_c47x * (s14.a - s13.a);
    sky = sky + alpha * (col - sky);
  }
  return float4(clay_lit(sky * f_exposure), 1.0);
}
)hlsl";

#undef FABLE2_CLAY_TERRAIN_FAR_HLSL
#undef FABLE2_CLAY_TERRAIN_LAYER_HLSL
#undef FABLE2_CLAY_TERRAIN_COMMON_HLSL
#undef FABLE2_CLAY_TEXTURED_HLSL
#undef FABLE2_CLAY_FOG_HLSL
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
// so that it does not reach the unlit draws and the clear. Not drawn in a post frame, which ends in the
// two post steps below instead; its text and its word are what they were. The linear target is read texel for texel (the
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

// The post steps (render/clay_pass.cpp; clay_logic.h, "Native post" and "Native bloom"; frame-map section 17,
// "Native composite" and "Native bloom"; kFullscreenVs draws them): in a post frame the game's composite and
// the scan-out ramp in place of kOutputMapPs, and with the bloom before them its four steps. All declare
// cbuffer Post at b0 of the post layout (ClayPostConstants, kClayPostSlots small slots of the frame's constant
// buffer; kClayPostFields is its layout, which the tests hold against the compiled shaders and the C++
// struct) and cbuffer Step at b1 (ClayPostStepConstants, the step's own 8 words; kClayPostStepFields), and
// read their first texture at t0. The table and the ramp are arrays of the block, read by index: the game's
// point fetch exactly. No shader holds a size: texel sizes are the step's words.
//   The level of a UNORM target is written by the step itself (controller ruling 25; clay_logic.h,
//   PostLevel): the last statement of every step but the downsample (a float target) is
//   post_level(v) = floor(v * s_levels + 0.5) / s_levels of a value already in [0, 1], s_levels the level
//   count of the target's format (255, 1023), so that the stored value has no fraction for a card to round
//   its own way.
//   kClayPostCompositePs: the arithmetic of the game's 0x37B186F4BD3288F7 (light_formulas.post_composite;
//     PostCompositeMirror is the mirror, held to the reference's numbers) from the linear target into the
//     front target (8 bits, before the ramp):
//       s = the scene texel, each channel (v > 0) ? min(v, p_scene_max) : 0      p_scene_max: 31.875
//       L = lum(s);  e = p_c77y * table[min(511, uint(512 * saturate(p_c46x * L) + 0.5))]
//       c = sat(e * (L + sat(p_c47x * L) * (s - L)));  b = sat(bloom * p_c31x);  scr = c + b * (1 - c)
//       t = scr + p_c77x * (scr - lum(scr));  out = sat(|t| ^ p_c78x), alpha 1
//     The table is read at the NEAREST texel (controller ruling 19: the game's fetch has a half-texel
//     offset), not at the floor. The bloom is (0, 0, 0) unless the block's bloom flag is set (ClayBloom::kOn);
//     then it is the blurred bright pass at t1, sampled linearly (s1, clamped) at the scene's interpolated
//     coordinate plus the fetch's half bloom texel, (floor(pos.xy) + s_offset) * s_texel with s_offset 2 pixels
//     (clay_logic.h, kPostBloomFetch). With the flag 0 the program does what plan Task 5's did, and the
//     picture is that one's. Guards, each a comparison that is false for a value that is no number: a scene channel
//     that is not above 0 is 0 (a NaN, a negative value), one above the clamp is the clamp (an infinity);
//     the coordinate is saturated before it is made an integer and the index bounded after; the power of a
//     base that is not above 0 or not below 3e38 is 0. With registers and texels inside the block's bounds
//     (MakeClayPostConstants) every term is then a number, and the 8-bit target holds nothing else.
//     The previous-frame blend (plan Task 15; clay_logic.h, "The previous-frame blend"; the game's pass F,
//     light_formulas.post_previous_blend; PostPreviousMirror): only with p_previous above 0, the level
//     just made is mixed with the other front target's texel (t2, Load; taken into [0, 1], a value that is
//     no number as 0) by p_previous (at most 1) and the level of the mix is written:
//       out = post_level(sat(now + (before - now) * p_previous)),  now = post_level(PostComposite(...))
//     With p_previous 0 (every frame with fable2_native_sun off, the first post frame, the frame after one
//     without post, the oracle feed) the first statement's test fails, t2 is not read, and what follows is
//     the text of before, statement for statement (the blended form is a function of its own, PostBlended).
//   kClayPostRampPs: each channel of the front target's texel through the guest's 256-entry gamma ramp, as
//     the SDK's swap blit applies it to the emulated picture (light_formulas.post_ramp; PostRampIndex,
//     PostRampMirror): out.c = p_ramp[min(255, uint(saturate(v.c) * 255 + 0.5))].c, alpha 1, into the mapped
//     target the composite views sample.
//   The bloom's three programs (plan Task 8; the game's 87E8, EE92, EA83 and 28DB; light_formulas.
//   post_downsample, post_bright, post_blur13; PostDownsampleMirror, PostBrightMirror, PostBlurMirror):
//   kClayPostDownsamplePs: the scene (t0) into the quarter-size float target: a quarter of the sum of four
//     linear samples (s1, clamped) at P + (2.5, 2.5), (2.5, 0.5), (0.5, 0.5), (0.5, 2.5) texels, P = 4 *
//     (floor(pos.xy) + s_offset), each sample read as post_scene (0 unless above 0, at most p_scene_max). With
//     s_offset 0 (kPostDownsampleBias) the four are texel centres: four texels of the pixel's sixteen.
//   kClayPostBrightPs: the downsample's texel (t0, Load) into the first 10-bit target: d = post_scene(texel),
//     L = lum(d), v = d * (L > 0 ? sat(L * p_b77y - p_b77x) / L * p_b31x : 0); with the sun's flag
//     (plan Task 14) plus tf1 * 6 * p_b46x + tf2 (t1, t2: unread without the flag, the white texture bound);
//     out = post_level(post_unit(v / p_b47x)).
//   kClayPostBlurPs: one program for both blurs, the direction in the step's words: 13 Loads of t0 at
//     at + k * s_dir, k = -6..6, each coordinate clamped to [0, s_last], weighted p_blur's row 0 and 1 (along
//     x) or 2 and 3 (along y) by |k|; out = post_level(post_unit(sum)).
//   post_unit(v) = (v > 0) ? min(v, 1) : 0: a value that is no number is 0, so none enters or leaves a blur.
inline constexpr ClayBufferField kClayPostFields[] = {
    {"p_c31x", 0, 4},     {"p_c46x", 4, 4},       {"p_c47x", 8, 4},     {"p_c77x", 12, 4},
    {"p_c77y", 16, 4},    {"p_c78x", 20, 4},      {"p_scene_max", 24, 4}, {"p_previous", 28, 4},
    {"p_b31x", 32, 4},    {"p_b46x", 36, 4},      {"p_b47x", 40, 4},    {"p_b77x", 44, 4},
    {"p_b77y", 48, 4},    {"p_bloom_on", 52, 4},  {"p_sun_on", 56, 4},  {"p_pad", 60, 4},
    {"p_blur", 64, 64},   {"p_radial", 128, 16},  {"p_table", 144, 2048}, {"p_ramp", 2192, 4096},
};
inline constexpr unsigned kClayPostSize = 6288;
// cbuffer Step (b1 of the post layout): render::ClayPostStepConstants.
inline constexpr ClayBufferField kClayPostStepFields[] = {
    {"s_texel", 0, 8}, {"s_levels", 8, 4}, {"s_offset", 12, 4}, {"s_dir", 16, 8}, {"s_last", 24, 8},
};
inline constexpr unsigned kClayPostStepSize = 32;

#define FABLE2_CLAY_POST_HLSL R"hlsl(
cbuffer Post : register(b0) {
  float p_c31x; float p_c46x; float p_c47x; float p_c77x;          // the composite 37B1: c31.x, c46.x, c47.x, c77.x
  float p_c77y; float p_c78x; float p_scene_max; float p_previous; // c77.y, c78.x; the scene's clamp; (plan Task 15)
  float p_b31x; float p_b46x; float p_b47x; float p_b77x;          // the bright pass EE92: c31.x, c46.x, c47.x, c77.x
  float p_b77y; uint p_bloom_on; uint p_sun_on; uint p_pad;        // c77.y; the bloom's flag; the sun's (plan Task 14)
  float4 p_blur[4];                                                // the blurs' weights: EA83's in rows 0 and 1, 28DB's in 2 and 3
  float4 p_radial;                                                 // the radial blur's registers (plan Task 14)
  float4 p_table[128];                                             // the composite's table: texel i at [i >> 2][i & 3]
  float4 p_ramp[256];                                              // the scan-out ramp: entry i (rgb)
};
cbuffer Step : register(b1) {
  float2 s_texel;   // 1 / the width and height of the texture the step addresses by coordinate
  float s_levels;   // the level count of the step's target's format (255, 1023); 0 for a float target
  float s_offset;   // the downsample's pixel position; the composite's bloom fetch in pixels
  int2 s_dir;       // a blur's direction in texels: (1, 0) along x, (0, 1) along y
  int2 s_last;      // the last texel of a blur's source
};
float post_lum(float3 c) { return dot(c, float3(0.2125, 0.7154, 0.0721)); }
float3 post_scene(float3 v) { return (v > 0.0) ? min(v, p_scene_max) : float3(0.0, 0.0, 0.0); }
float3 post_unit(float3 v) { return (v > 0.0) ? min(v, 1.0) : float3(0.0, 0.0, 0.0); }
float3 post_level(float3 v) { return floor(v * s_levels + 0.5) / s_levels; }
)hlsl"

inline constexpr const char* kClayPostCompositePs = FABLE2_CLAY_POST_HLSL R"hlsl(
Texture2D<float4> scene_target : register(t0);
Texture2D<float4> bloom_target : register(t1);
Texture2D<float4> previous_front : register(t2);
SamplerState linear_clamp : register(s1);
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
float3 PostBlended(float4 pos) {
  float3 veil = float3(0.0, 0.0, 0.0);
  if (p_bloom_on != 0) veil = bloom_target.SampleLevel(linear_clamp, (floor(pos.xy) + s_offset) * s_texel, 0.0).rgb;
  float3 now = post_level(PostComposite(scene_target.Load(int3(pos.xy, 0)).rgb, veil));
  float3 before = previous_front.Load(int3(pos.xy, 0)).rgb;
  before = (before > 0.0 && before < 3.0e38) ? min(before, 1.0) : float3(0.0, 0.0, 0.0);
  return post_level(saturate(now + (before - now) * min(p_previous, 1.0)));
}
float4 main(float4 pos : SV_Position) : SV_Target {
  if (p_previous > 0.0) return float4(PostBlended(pos), 1.0);
  float3 scene = scene_target.Load(int3(pos.xy, 0)).rgb;
  float3 bloom = float3(0.0, 0.0, 0.0);
  if (p_bloom_on != 0) bloom = bloom_target.SampleLevel(linear_clamp, (floor(pos.xy) + s_offset) * s_texel, 0.0).rgb;
  return float4(post_level(PostComposite(scene, bloom)), 1.0);
}
)hlsl";

inline constexpr const char* kClayPostRampPs = FABLE2_CLAY_POST_HLSL R"hlsl(
Texture2D<float4> front_target : register(t0);
uint post_ramp_index(float v) { return min(uint(saturate(v) * 255.0 + 0.5), 255u); }
float4 main(float4 pos : SV_Position) : SV_Target {
  float3 v = front_target.Load(int3(pos.xy, 0)).rgb;
  float3 mapped = float3(p_ramp[post_ramp_index(v.r)].r, p_ramp[post_ramp_index(v.g)].g, p_ramp[post_ramp_index(v.b)].b);
  return float4(post_level(saturate(mapped)), 1.0);
}
)hlsl";

inline constexpr const char* kClayPostDownsamplePs = FABLE2_CLAY_POST_HLSL R"hlsl(
Texture2D<float4> scene_target : register(t0);
SamplerState linear_clamp : register(s1);
float3 post_tap(float2 p, float2 offset) {
  return post_scene(scene_target.SampleLevel(linear_clamp, (p + offset) * s_texel, 0.0).rgb);
}
float4 main(float4 pos : SV_Position) : SV_Target {
  float2 p = 4.0 * (floor(pos.xy) + s_offset);
  float3 sum = post_tap(p, float2(2.5, 2.5)) + post_tap(p, float2(2.5, 0.5)) + post_tap(p, float2(0.5, 0.5)) + post_tap(p, float2(0.5, 2.5));
  return float4(0.25 * sum, 1.0);
}
)hlsl";

inline constexpr const char* kClayPostBrightPs = FABLE2_CLAY_POST_HLSL R"hlsl(
Texture2D<float4> down_target : register(t0);
Texture2D<float4> sun_tf1 : register(t1);
Texture2D<float4> sun_tf2 : register(t2);
float4 main(float4 pos : SV_Position) : SV_Target {
  int3 at = int3(pos.xy, 0);
  float3 d = post_scene(down_target.Load(at).rgb);
  float L = post_lum(d);
  float gain = (L > 0.0) ? saturate(L * p_b77y - p_b77x) / L * p_b31x : 0.0;
  float3 v = d * gain;
  if (p_sun_on != 0) v += sun_tf1.Load(at).rgb * 6.0 * p_b46x + sun_tf2.Load(at).rgb;
  return float4(post_level(post_unit(v / p_b47x)), 1.0);
}
)hlsl";

inline constexpr const char* kClayPostBlurPs = FABLE2_CLAY_POST_HLSL R"hlsl(
Texture2D<float4> blur_source : register(t0);
float3 post_blur_tap(int2 at, int k, float w) {
  return w * blur_source.Load(int3(clamp(at + k * s_dir, int2(0, 0), s_last), 0)).rgb;
}
float4 main(float4 pos : SV_Position) : SV_Target {
  int2 at = int2(pos.xy);
  bool vertical = s_dir.y != 0;
  float4 w0 = vertical ? p_blur[2] : p_blur[0];
  float4 w1 = vertical ? p_blur[3] : p_blur[1];
  float3 sum = post_blur_tap(at, 0, w0.x);
  sum += post_blur_tap(at, -1, w0.y) + post_blur_tap(at, 1, w0.y);
  sum += post_blur_tap(at, -2, w0.z) + post_blur_tap(at, 2, w0.z);
  sum += post_blur_tap(at, -3, w0.w) + post_blur_tap(at, 3, w0.w);
  sum += post_blur_tap(at, -4, w1.x) + post_blur_tap(at, 4, w1.x);
  sum += post_blur_tap(at, -5, w1.y) + post_blur_tap(at, 5, w1.y);
  sum += post_blur_tap(at, -6, w1.z) + post_blur_tap(at, 6, w1.z);
  return float4(post_level(post_unit(sum)), 1.0);
}
)hlsl";

// The sun layer's three steps under the post layout (plan Task 14; described with the layer's other programs
// below, "The sun layer"): the layer's depth, the reduction, the radial blur. They share the post steps' text
// above (cbuffer Post, cbuffer Step, post_unit, post_level), which is unchanged; in cbuffer Step they read
// s_dir as the size ratio of the step's source to its target.
inline constexpr const char* kClaySunDepthPs = FABLE2_CLAY_POST_HLSL R"hlsl(
Texture2D<float4> sun_depth_source : register(t0);
float main(float4 pos : SV_Position) : SV_Depth {
  int2 at = min(int2(pos.xy) * s_dir, s_last);
  float d = sun_depth_source.Load(int3(at, 0)).r;
  return (d > 0.0) ? min(d, 1.0) : 0.0;
}
)hlsl";

inline constexpr const char* kClaySunReducePs = FABLE2_CLAY_POST_HLSL R"hlsl(
Texture2D<float4> reduce_source : register(t0);
SamplerState linear_clamp : register(s1);
float4 main(float4 pos : SV_Position) : SV_Target {
  float2 ratio = float2(s_dir);
  float3 v = reduce_source.SampleLevel(linear_clamp, (ratio * floor(pos.xy) + 0.5 * ratio) * s_texel, 0.0).rgb;
  return float4(post_level(post_unit(v)), 1.0);
}
)hlsl";

inline constexpr const char* kClaySunRadialPs = FABLE2_CLAY_POST_HLSL R"hlsl(
Texture2D<float4> radial_source : register(t0);
float4 main(float4 pos : SV_Position) : SV_Target {
  float2 last = float2(s_last);
  float2 size = last + 1.0;
  float2 uv = floor(pos.xy) * s_texel;
  float2 centre = p_radial.xy;
  float s = (p_radial.w - 1.0) * (4.0 / 7.0) + 1.0;
  float g = (p_radial.z - 1.0) * (4.0 / 7.0) + 1.0;
  float3 sum = float3(0.0, 0.0, 0.0);
  float sk = 1.0;
  float w = 0.5;
  for (int k = 0; k < 14; ++k) {
    float2 p = (centre + (uv - centre) * sk) * size + 0.5;
    p = (p > 0.0) ? min(p, last) : float2(0.0, 0.0);
    sum += w * post_unit(radial_source.Load(int3(int2(p), 0)).rgb);
    sk *= s;
    w *= g;
  }
  return float4(post_level(post_unit(sum * (4.0 / 7.0))), 1.0);
}
)hlsl";

#undef FABLE2_CLAY_POST_HLSL

// The quad step (render/clay_pass.cpp; clay_logic.h, "Native furniture"; sub-project 10, plan Task 13):
// one of the game's furniture quads, drawn again from its quad record in the colour pass. Its own layout:
// cbuffer Quad at b0 for both stages (render::ClayQuadConstants, two small slots of the frame's constant
// buffer; kClayQuadFields is its layout), the frame's block at b2 (declared by the layout; no program of
// this build reads it), a table of three textures at t0 (the quad's own at t0; the white texture in the
// others), the four static samplers.
//   kClayQuadVs: six vertices by SV_VertexID in the game's order 0 1 2 1 3 2 (capture::kQuadIndices), each a
//     corner of the record through the game's own matrix: clip = (c0 . p, c1 . p, c2 . p, c3 . p), p = (x, y,
//     z, 1), as the game's 81BADE63A169E3DC (light_formulas.post_quad_position; QuadPositionMirror), with the
//     corner's coordinate. The clip position is the game's: its z / w is the pass's depth (0 near, 1 far).
//   kClayQuadPs: the kind's formula by q_kind (capture::PostQuadKind + 1), on the texel of t0 sampled at the
//     coordinate times the host texture's scale, with the texture's own gradients:
//       1, the moon (18D39DCEBEE4C2FC, post_moon):   rgb = tex.rgb * c31.rgb * tex.a * c20.w, alpha = c31.w * tex.a
//       3, the glare (04985A0F7296E130, post_glare): rgb = tex.rgb * c31.rgb,                 alpha = 1
//     (q_reg holds the kind's registers in the record's order: the moon's c20 and c31, the glare's c31), any
//     other word: nothing (0, alpha 0). Then the draw's own alpha test, as the game's state has it: with
//     q_alpha_test the pixel is discarded unless its alpha is above q_alpha_ref. Out: each colour channel
//     (v > 0) ? min(v, q_scene_max) : 0 and the alpha saturated, so that nothing that is no number is written
//     (a texel's channel that is no number reads 0: the first statement after the sample). The blend and
//     the depth test are the pipeline's (ClayQuadState).
inline constexpr ClayBufferField kClayQuadFields[] = {
    {"q_corner", 0, 64},    {"q_coord", 64, 64},     {"q_view_proj", 128, 64}, {"q_reg", 192, 192},
    {"q_kind", 384, 4},     {"q_sampler", 388, 4},   {"q_alpha_test", 392, 4}, {"q_alpha_ref", 396, 4},
    {"q_fix", 400, 8},      {"q_scene_max", 408, 4}, {"q_pad", 412, 4},
};
inline constexpr unsigned kClayQuadSize = 416;

#define FABLE2_CLAY_QUAD_HLSL R"hlsl(
cbuffer Quad : register(b0) {
  float4 q_corner[4];      // the corners' positions: x y z, 1
  float4 q_coord[4];       // their coordinates: u v
  float4 q_view_proj[4];   // vertex c0..c3 at the game's call
  float4 q_reg[12];        // the kind's pixel registers, in the record's order
  uint q_kind; uint q_sampler; uint q_alpha_test; float q_alpha_ref;   // the formula; the static sampler; the alpha test
  float2 q_fix; float q_scene_max; uint q_pad;                         // the host texture's scale; the scene's clamp
};
)hlsl"

inline constexpr const char* kClayQuadVs = FABLE2_CLAY_QUAD_HLSL R"hlsl(
struct QuadOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
QuadOut main(uint id : SV_VertexID) {
  static const uint order[6] = {0, 1, 2, 1, 3, 2};
  uint c = order[min(id, 5u)];
  float4 p = float4(q_corner[c].xyz, 1.0);
  QuadOut o;
  o.pos = float4(dot(q_view_proj[0], p), dot(q_view_proj[1], p), dot(q_view_proj[2], p), dot(q_view_proj[3], p));
  o.uv = q_coord[c].xy;
  return o;
}
)hlsl";

inline constexpr const char* kClayQuadPs = FABLE2_CLAY_QUAD_HLSL R"hlsl(
Texture2D quad_tex : register(t0);
SamplerState q_point_wrap : register(s0);
SamplerState q_linear_wrap : register(s1);
SamplerState q_point_clamp : register(s2);
SamplerState q_linear_clamp : register(s3);
float4 quad_sample(float2 at, float2 dx, float2 dy) {
  float4 c;
  if (q_sampler == 0) c = quad_tex.SampleGrad(q_point_wrap, at, dx, dy);
  else if (q_sampler == 1) c = quad_tex.SampleGrad(q_linear_wrap, at, dx, dy);
  else if (q_sampler == 2) c = quad_tex.SampleGrad(q_point_clamp, at, dx, dy);
  else c = quad_tex.SampleGrad(q_linear_clamp, at, dx, dy);
  return c;
}
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
  float2 at = uv * q_fix;
  float4 t = quad_sample(at, ddx(at), ddy(at));
  t = (abs(t) < 3.0e38) ? t : float4(0.0, 0.0, 0.0, 0.0);
  float3 rgb = float3(0.0, 0.0, 0.0);
  float a = 0.0;
  if (q_kind == 1) {
    rgb = t.rgb * q_reg[1].rgb * t.a * q_reg[0].w;
    a = q_reg[1].a * t.a;
  } else if (q_kind == 3) {
    rgb = t.rgb * q_reg[0].rgb;
    a = 1.0;
  }
  if (q_alpha_test != 0 && !(a > q_alpha_ref)) discard;
  rgb = (rgb > 0.0) ? min(rgb, q_scene_max) : float3(0.0, 0.0, 0.0);
  return float4(rgb, saturate(a));
}
)hlsl";

#undef FABLE2_CLAY_QUAD_HLSL

// The sun layer (render/clay_pass.cpp; clay_logic.h, "Native sun layer"; sub-project 10, plan Task 14): the
// game's half-size layer of beams, flares, moon glow and sun disc, drawn again from the frame's quad records
// and fed to the bright pass (kClayPostBrightPs, whose text is unchanged: its sun flag was there). Six
// programs, none of them made, bound or drawn with fable2_native_sun off, and no other program's text
// changed for them (the first three stand above, with the post steps whose shared text they use):
//   kClaySunDepthPs (the post layout; kFullscreenVs): the layer's depth from the scene's, depth only. A
//     texel of the layer takes the scene depth's texel s_dir * floor(pos.xy), by Load, never beyond s_last
//     (the game's 5D67 takes every second texel of its depth copy by point: SunDepthTexel; s_dir is the
//     size ratio, so no size is in the text). Out: SV_Depth = (d > 0) ? min(d, 1) : 0 (SunDepthValue).
//   kClaySunQuadVs, kClaySunQuadPs, kClaySunOccluderPs (the quad step's layout; cbuffer SunQuad at b0,
//     render::ClaySunQuadConstants: the scene quads' block with the target's level count; kClaySunQuadFields):
//     a layer quad, six vertices as kClayQuadVs makes them, with the corner's own position passed on as
//     the pixel's world position (the game's C304C6A1AF18B46F does the same for the clouds). The pixel
//     shader's formula by q_kind (capture::PostQuadKind + 1), on the texel of t0:
//       4, the beam source (8E4D, post_beam_source):  rgb = tex.rgb * (1 / 6) * c31.rgb,          alpha 1
//       5, a flare (D72F, post_flare):                rgb = tex.a * c31.rgb,                      alpha 1
//       6, the moon glow (4B58, post_moon_glow):      rgb = tex.rgb * 0.2 * tex.a,                alpha tex.a
//       7, the sun disc (3E77, post_sun_disc):        rgb = tex.rgb * c31.rgb * tex.a * c20.w,    alpha tex.a
//     any other word: the pixel is dropped. Then the draw's own alpha test (dropped unless the alpha is
//     above q_alpha_ref). Out: each channel (v > 0) ? min(v, 1) : 0, as a level of the target's format,
//     floor(v * q_levels + 0.5) / q_levels (controller ruling 25), the alpha saturated. The blend and the
//     depth test are the pipeline's (ClaySunState).
//     kClaySunOccluderPs is the same text compiled with CLAY_SUN_OCCLUDER: kind 8, a cloud occluder (3FF7,
//     post_cloud_occluder), drawn with no colour target. It writes nothing but the depth of the pixels it
//     keeps: alpha = tex(uv * c31.xy + c46.xy).a * c77.x * (1 - sat(0.001 * (|P.xy - c47.xy| - 1000))), kept
//     where that is above q_alpha_ref (the draw's reference, 0.149 captured); any other kind, or a draw
//     without the alpha test, keeps no pixel.
//   kClaySunReducePs (the post layout): a picture at 1 / s_dir of its size, one linear sample (s1, clamped)
//     at (s_dir * floor(pos.xy) + s_dir / 2) * s_texel: with the ratio 2 the corner four texels share,
//     their mean (the game's 0A96, post_reduce). Out: post_level(post_unit(v)).
//   kClaySunRadialPs (the post layout): one pass of the radial blur (4BE4, post_radial) about p_radial.xy:
//     14 Loads of t0, tap k at texel floor(min(max((c + (uv - c) * s^k) * size + 0.5, 0), s_last)) with the
//     weight g^k / 2, uv = floor(pos.xy) * s_texel, s = 1 + (4 / 7)(p_radial.w - 1), g = 1 + (4 / 7)
//     (p_radial.z - 1); out = post_level(post_unit(sum * 4 / 7)). The tap's place is bounded by a
//     comparison that is false for a value that is no number, so no index leaves the source.
inline constexpr ClayBufferField kClaySunQuadFields[] = {
    {"u_corner", 0, 64},    {"u_coord", 64, 64},     {"u_view_proj", 128, 64}, {"u_reg", 192, 192},
    {"u_kind", 384, 4},     {"u_sampler", 388, 4},   {"u_alpha_test", 392, 4}, {"u_alpha_ref", 396, 4},
    {"u_fix", 400, 8},      {"u_levels", 408, 4},    {"u_pad", 412, 4},
};
inline constexpr unsigned kClaySunQuadSize = 416;

#define FABLE2_CLAY_SUN_QUAD_HLSL R"hlsl(
cbuffer SunQuad : register(b0) {
  float4 u_corner[4];      // the corners' positions: x y z, 1
  float4 u_coord[4];       // their coordinates: u v
  float4 u_view_proj[4];   // vertex c0..c3 at the game's call
  float4 u_reg[12];        // the kind's pixel registers, in the record's order
  uint u_kind; uint u_sampler; uint u_alpha_test; float u_alpha_ref;   // the formula; the static sampler; the alpha test
  float2 u_fix; float u_levels; uint u_pad;                            // the host texture's scale; the target's level count
};
struct SunQuadOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float3 world : TEXCOORD1; };
)hlsl"

inline constexpr const char* kClaySunQuadVs = FABLE2_CLAY_SUN_QUAD_HLSL R"hlsl(
SunQuadOut main(uint id : SV_VertexID) {
  static const uint order[6] = {0, 1, 2, 1, 3, 2};
  uint c = order[min(id, 5u)];
  float4 p = float4(u_corner[c].xyz, 1.0);
  SunQuadOut o;
  o.pos = float4(dot(u_view_proj[0], p), dot(u_view_proj[1], p), dot(u_view_proj[2], p), dot(u_view_proj[3], p));
  o.uv = u_coord[c].xy;
  o.world = u_corner[c].xyz;
  return o;
}
)hlsl";

#define FABLE2_CLAY_SUN_QUAD_PS_HLSL FABLE2_CLAY_SUN_QUAD_HLSL R"hlsl(
Texture2D quad_tex : register(t0);
SamplerState u_point_wrap : register(s0);
SamplerState u_linear_wrap : register(s1);
SamplerState u_point_clamp : register(s2);
SamplerState u_linear_clamp : register(s3);
float4 sun_sample(float2 at, float2 dx, float2 dy) {
  float4 c;
  if (u_sampler == 0) c = quad_tex.SampleGrad(u_point_wrap, at, dx, dy);
  else if (u_sampler == 1) c = quad_tex.SampleGrad(u_linear_wrap, at, dx, dy);
  else if (u_sampler == 2) c = quad_tex.SampleGrad(u_point_clamp, at, dx, dy);
  else c = quad_tex.SampleGrad(u_linear_clamp, at, dx, dy);
  return (abs(c) < 3.0e38) ? c : float4(0.0, 0.0, 0.0, 0.0);
}
#ifdef CLAY_SUN_OCCLUDER
void main(SunQuadOut i) {
  float2 at = (i.uv * u_reg[0].xy + u_reg[1].xy) * u_fix;
  float4 t = sun_sample(at, ddx(at), ddy(at));
  float a = 0.0;
  if (u_kind == 8) a = saturate(t.a * u_reg[3].x * (1.0 - saturate(0.0010000000474974513 * (length(i.world.xy - u_reg[2].xy) - 1000.0))));
  if (u_kind != 8 || u_alpha_test == 0 || !(a > u_alpha_ref)) discard;
}
#else
float4 main(SunQuadOut i) : SV_Target {
  float2 at = i.uv * u_fix;
  float4 t = sun_sample(at, ddx(at), ddy(at));
  float3 rgb = float3(0.0, 0.0, 0.0);
  float a = 0.0;
  bool known = true;
  if (u_kind == 4) {
    rgb = t.rgb * 0.1666666716337204 * u_reg[0].rgb;
    a = 1.0;
  } else if (u_kind == 5) {
    rgb = t.a * u_reg[0].rgb;
    a = 1.0;
  } else if (u_kind == 6) {
    rgb = t.rgb * 0.20000000298023224 * t.a;
    a = t.a;
  } else if (u_kind == 7) {
    rgb = t.rgb * u_reg[1].rgb * t.a * u_reg[0].w;
    a = t.a;
  } else {
    known = false;
  }
  if (!known || (u_alpha_test != 0 && !(a > u_alpha_ref))) discard;
  rgb = (rgb > 0.0) ? min(rgb, 1.0) : float3(0.0, 0.0, 0.0);
  return float4(floor(rgb * u_levels + 0.5) / u_levels, saturate(a));
}
#endif
)hlsl"

inline constexpr const char* kClaySunQuadPs = FABLE2_CLAY_SUN_QUAD_PS_HLSL;
inline constexpr const char* kClaySunOccluderPs = "#define CLAY_SUN_OCCLUDER 1\n" FABLE2_CLAY_SUN_QUAD_PS_HLSL;

#undef FABLE2_CLAY_SUN_QUAD_PS_HLSL
#undef FABLE2_CLAY_SUN_QUAD_HLSL

// The scene's clouds (render/clay_pass.cpp; clay_logic.h, "Native clouds"; sub-project 10, plan Task 16): the
// game's three cloud layers, drawn again from the frame's quad records in the colour pass, after the moon.
// Two programs under the quad step's layout, neither made, bound or drawn with fable2_native_sun off, and no
// other program's text changed for them:
//   kClayCloudVs: six vertices as kClayQuadVs makes them, with the corner's own position passed on as the
//     pixel's world position (the game's C304C6A1AF18B46F).
//   kClayCloudPs (61976295517D7015, post_cloud; cbuffer Cloud at b0: render::ClayQuadConstants with the
//     cloud's registers, kClayCloudFields): at = (uv * c31.xy + c46.xy) scaled into the host texture;
//     t = the texel at `at`, sampled with `at`'s own gradients under the sampler the cache names;
//     alpha = t.a * c69.x * (1 - sat(0.001 * (|P.xy - c79.xy| - 1000))); the pixel is dropped unless the word
//     is a cloud's (2), the alpha test is on and the alpha is above k_alpha_ref (the draw's own reference).
//     Where alpha is above 0.001: g = t.a one texel OF THE BASE LEVEL on less one back, in x and in y (the
//     twelfth register holds that step; the four samples keep the pixel's gradients: the emulator's fetch
//     offset), N = normalize(g.x, g.y, c70.x), Dn = normalize(P - c77.xyz), a1 = sat(-N . c78.xyz),
//     b1 = sat(-Dn . N), b2 = sat(Dn . N), l = b1^2 (1 - t.a) + b2 (1 + (1 - a1)^32),
//     rgb = (l c47.rgb + c71.x) c51.x t.rgb c20.w; else rgb = 0. Out: each channel (v > 0) ? min(v,
//     k_scene_max) : 0, the alpha saturated. A vector of length 0 normalises to 0; a texel's channel that is
//     no number reads 0. The blend (by the alpha) and the depth test are the pipeline's; it writes no depth.
// The registers, in the record's order: 0 c20, 1 c31, 2 c46, 3 c47, 4 c51, 5 c69, 6 c70, 7 c71, 8 c77, 9 c78,
// 10 c79 (render::kCloudC20 and on); 11: the base level's texel step, x y (render::kCloudStepRegister).
inline constexpr ClayBufferField kClayCloudFields[] = {
    {"k_corner", 0, 64},    {"k_coord", 64, 64},     {"k_view_proj", 128, 64}, {"k_reg", 192, 192},
    {"k_kind", 384, 4},     {"k_sampler", 388, 4},   {"k_alpha_test", 392, 4}, {"k_alpha_ref", 396, 4},
    {"k_fix", 400, 8},      {"k_scene_max", 408, 4}, {"k_pad", 412, 4},
};
inline constexpr unsigned kClayCloudSize = 416;

#define FABLE2_CLAY_CLOUD_HLSL R"hlsl(
cbuffer Cloud : register(b0) {
  float4 k_corner[4];      // the corners' positions: x y z, 1
  float4 k_coord[4];       // their coordinates: u v
  float4 k_view_proj[4];   // vertex c0..c3 at the game's call
  float4 k_reg[12];        // the cloud's pixel registers, in the record's order; the twelfth: the texel step
  uint k_kind; uint k_sampler; uint k_alpha_test; float k_alpha_ref;   // the formula; the static sampler; the alpha test
  float2 k_fix; float k_scene_max; uint k_pad;                         // the host texture's scale; the scene's clamp
};
struct CloudOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float3 world : TEXCOORD1; };
)hlsl"

inline constexpr const char* kClayCloudVs = FABLE2_CLAY_CLOUD_HLSL R"hlsl(
CloudOut main(uint id : SV_VertexID) {
  static const uint order[6] = {0, 1, 2, 1, 3, 2};
  uint c = order[min(id, 5u)];
  float4 p = float4(k_corner[c].xyz, 1.0);
  CloudOut o;
  o.pos = float4(dot(k_view_proj[0], p), dot(k_view_proj[1], p), dot(k_view_proj[2], p), dot(k_view_proj[3], p));
  o.uv = k_coord[c].xy;
  o.world = k_corner[c].xyz;
  return o;
}
)hlsl";

inline constexpr const char* kClayCloudPs = FABLE2_CLAY_CLOUD_HLSL R"hlsl(
Texture2D cloud_tex : register(t0);
SamplerState k_point_wrap : register(s0);
SamplerState k_linear_wrap : register(s1);
SamplerState k_point_clamp : register(s2);
SamplerState k_linear_clamp : register(s3);
float4 cloud_sample(float2 at, float2 dx, float2 dy) {
  float4 c;
  if (k_sampler == 0) c = cloud_tex.SampleGrad(k_point_wrap, at, dx, dy);
  else if (k_sampler == 1) c = cloud_tex.SampleGrad(k_linear_wrap, at, dx, dy);
  else if (k_sampler == 2) c = cloud_tex.SampleGrad(k_point_clamp, at, dx, dy);
  else c = cloud_tex.SampleGrad(k_linear_clamp, at, dx, dy);
  return (abs(c) < 3.0e38) ? c : float4(0.0, 0.0, 0.0, 0.0);
}
float3 cloud_unit(float3 v) {
  float n2 = dot(v, v);
  return (n2 > 0.0 && n2 < 3.0e38) ? v * rsqrt(n2) : float3(0.0, 0.0, 0.0);
}
float4 main(CloudOut i) : SV_Target {
  float2 at = (i.uv * k_reg[1].xy + k_reg[2].xy) * k_fix;
  float2 dx = ddx(at);
  float2 dy = ddy(at);
  float4 t = cloud_sample(at, dx, dy);
  float fade = 1.0 - saturate(0.0010000000474974513 * (length(i.world.xy - k_reg[10].xy) - 1000.0));
  float alpha = t.a * k_reg[5].x * fade;
  if (k_kind != 2 || k_alpha_test == 0 || !(alpha > k_alpha_ref)) discard;
  float3 rgb = float3(0.0, 0.0, 0.0);
  if (alpha > 0.0010000000474974513) {
    float2 step = k_reg[11].xy * k_fix;
    float gx = cloud_sample(at + float2(step.x, 0.0), dx, dy).a - cloud_sample(at - float2(step.x, 0.0), dx, dy).a;
    float gy = cloud_sample(at + float2(0.0, step.y), dx, dy).a - cloud_sample(at - float2(0.0, step.y), dx, dy).a;
    float3 n = cloud_unit(float3(gx, gy, k_reg[6].x));
    float3 d = cloud_unit(i.world - k_reg[8].xyz);
    float a1 = saturate(-dot(n, k_reg[9].xyz));
    float towards = dot(d, n);
    float b1 = saturate(-towards);
    float b2 = saturate(towards);
    float base = abs(1.0 - a1);
    float lobe = (base > 0.0) ? pow(base, 32.0) : 0.0;
    float l = b1 * b1 * (1.0 - t.a) + (b2 + b2 * lobe);
    rgb = (l * k_reg[3].rgb + k_reg[7].x) * k_reg[4].x * t.rgb * k_reg[0].w;
  }
  rgb = (rgb > 0.0) ? min(rgb, k_scene_max) : float3(0.0, 0.0, 0.0);
  return float4(rgb, saturate(alpha));
}
)hlsl";

#undef FABLE2_CLAY_CLOUD_HLSL

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

// The stored native scene's depth step (render/clay_pass.cpp, the scene dump; render/scene_dump.h;
// frame-map section 18, "Stored native scene"): a diagnostic, compiled, bound and drawn only for a
// requested dump with --fable2_native_scene_dump=on, and a text of its own (no other program holds
// a word of it). Under the output mapping's layout (kFullscreenVs draws it; the layout's constants
// are not read): one depth texture at t0, read texel for texel by Load (the target has the
// source's size), written into an R8G8B8A8 target as the depth's 24-bit level, the low byte in r,
// the middle in g, the high in b, alpha 1. The level is render::DepthLevel24's, statement for
// statement: 0 unless the depth is above 0 (a value that is no number fails the comparison), else
// the nearest whole number to min(d, 1) * 16777215, a half rounded up, at most 16777215. Each
// byte is a whole level of the 8-bit target (b / 255), so the write is the rounded level itself.
inline constexpr const char* kClayDepthPackPs = R"hlsl(
Texture2D<float4> pack_depth : register(t0);
float4 main(float4 pos : SV_Position) : SV_Target {
  float d = pack_depth.Load(int3(int2(pos.xy), 0)).r;
  uint level = 0u;
  if (d > 0.0) {
    float x = min(d, 1.0) * 16777215.0;
    level = uint(x);
    if (x - float(level) >= 0.5) level += 1u;
    level = min(level, 16777215u);
  }
  return float4(float(level & 255u), float((level >> 8) & 255u), float(level >> 16), 255.0) / 255.0;
}
)hlsl";

}  // namespace fable2::native::shaders
