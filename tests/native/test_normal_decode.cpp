// Synthetic standalone test; no game or GPU. With FABLE2_NORMALS_BIN set: real streams of a
// discovery capture (see "Real streams" at the end).
#include "../../src/native/render/normal_decode.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace fable2::native;
using namespace fable2::native::render;

static int g_fail = 0;
#define CHECK(id, cond)                                         \
  do {                                                          \
    if (!(cond)) {                                              \
      std::cerr << "FAIL case " << (id) << ": " #cond "\n";     \
      if (!g_fail) g_fail = (id);                               \
    }                                                           \
  } while (0)

static void PutBe16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
static void PutBe32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
static bool Near(float a, float b, float eps = 1e-6f) { return std::fabs(a - b) <= eps; }
static bool Near3(const float v[3], float x, float y, float z, float eps = 1e-6f) {
  return Near(v[0], x, eps) && Near(v[1], y, eps) && Near(v[2], z, eps);
}
static bool Near3(const Float3& v, float x, float y, float z, float eps = 1e-6f) {
  return Near(v.x, x, eps) && Near(v.y, y, eps) && Near(v.z, z, eps);
}
static bool IsZero(const Float3& v) { return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f; }
static float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// A 10_11_11 word from its three signed fields: x 11 bits at bit 0, y 11 bits at bit 11, z 10 bits
// at bit 22 (the SDK's interpreter.cpp and dxbc_translator_fetch.cpp, VertexFormat::k_10_11_11).
static uint32_t Pack(int x, int y, int z) {
  return (uint32_t(x) & 0x7FFu) | ((uint32_t(y) & 0x7FFu) << 11) | ((uint32_t(z) & 0x3FFu) << 22);
}

// Half-float bits of small values with short mantissas (exact).
static uint16_t Half(float f) {
  if (f == 0.0f) return 0;
  const uint16_t sign = f < 0 ? 0x8000 : 0;
  f = std::fabs(f);
  int e = 0;
  const float m = std::frexp(f, &e);  // f = m * 2^e, m in [0.5, 1)
  const uint16_t exp = uint16_t(e - 1 + 15);
  const uint16_t man = uint16_t((m * 2.0f - 1.0f) * 1024.0f + 0.5f);
  return uint16_t(sign | (exp << 10) | man);
}
static void PutHalf4(uint8_t* p, float a, float b, float c, float d) {
  PutBe16(p, Half(a)); PutBe16(p + 2, Half(b)); PutBe16(p + 4, Half(c)); PutBe16(p + 6, Half(d));
}

// A selector as LightElement holds it: the source components (0 to 3) in the reader's order.
static void SetSource(capture::LightElement& e, const std::vector<int>& comps) {
  e.source = 0;
  e.source_count = uint8_t(comps.size());
  int i = 0;
  for (int c : comps) e.source = uint8_t(e.source | (c << (2 * i++)));
}

// The normal elements of the captured draws (frame-map section 14, "Light inputs"): 10_11_11 at
// byte 8 of a 20-byte vertex, signed, normalised, endian 8in32, source x, y, z (the table's "xyz").
static capture::LightElement Element10(uint32_t size, uint16_t stride = 20, uint16_t offset = 8) {
  capture::LightElement e;
  e.vb = {0x1000, size};
  e.offset_bytes = offset;
  e.stride_bytes = stride;
  SetSource(e, {0, 1, 2});
  e.format = uint8_t(capture::kVertexFormat_10_11_11);
  e.flags = uint8_t(capture::kLightElementSigned | capture::kLightElementNormalized | (2u << capture::kLightElementEndianShift));
  return e;
}
// 16_16_16_16_FLOAT at byte 8 of the 24-byte mesh vertex of an instanced plant, source y, x, w
// (the table's "yxw": the first three halves in memory).
static capture::LightElement ElementHalf(uint32_t size) {
  capture::LightElement e;
  e.vb = {0x2000, size};
  e.offset_bytes = 8;
  e.stride_bytes = 24;
  SetSource(e, {1, 0, 3});
  e.format = uint8_t(capture::kVertexFormat_16_16_16_16_FLOAT);
  e.flags = uint8_t(capture::kLightElementSigned | (2u << capture::kLightElementEndianShift));
  return e;
}
// The colour stream of 0xECD6...: 8_8_8_8 in a stream of its own, 4 bytes an element, source z, y,
// x, w (the table's "zyxw").
static capture::LightElement ElementColour(uint32_t size) {
  capture::LightElement e;
  e.vb = {0x3000, size};
  e.offset_bytes = 0;
  e.stride_bytes = 4;
  SetSource(e, {2, 1, 0, 3});
  e.format = uint8_t(capture::kVertexFormat_8_8_8_8);
  e.flags = uint8_t(capture::kLightElementNormalized | (2u << capture::kLightElementEndianShift));
  return e;
}

// One normal of an element of `format` as the table's entries of that format select it.
static bool NormalOfFormat(uint32_t format, const uint8_t* p, size_t avail, float n[3]) {
  capture::LightElement e = format == capture::kVertexFormat_16_16_16_16_FLOAT ? ElementHalf(24) : Element10(20);
  e.format = uint8_t(format);
  return DecodeNormal(e, p, avail, n);
}

// The light entries of the committed table (vs-transforms.json "light", as the capture reads them).
static const capture::VsLightSpec kVsLight[] = {
#define FABLE2_VS_TRANSFORM(H, B, L, P, D)
#define FABLE2_VS_POS_SWIZZLE(H, S)
#define FABLE2_VS_SKIN(H, I, W, R0, R1, R2, S0, S1, S2, N, IC0, WC0, IC1, WC1, IC2, WC2, IC3, WC3)
#define FABLE2_VS_INSTANCE(H, R0, R1, R2, S0, S1, S2, INV, CNT, FIRST, BIAS, OFF, CE, CD)
#define FABLE2_VS_TERRAIN(H, G, CE, HS, O, TO, TS, PO, F)
#define FABLE2_VS_UV(H, I, C, F, S, FM, O, S0, O0, S1, O1)
#define FABLE2_VS_LIGHT(H, K, NF, NM, NO, NS, SF, SM, SO, SS, A0, SC, PW)                                    \
  {H, capture::LightVertexKind(K), {int8_t(NF), uint8_t(NM), uint8_t(NO), uint16_t(NS)},                       \
   {int8_t(SF), uint8_t(SM), uint8_t(SO), uint16_t(SS)}, int8_t(A0), SC, PW},
#include "../../src/native/capture/vs_transform_table.inc"
#undef FABLE2_VS_TRANSFORM
#undef FABLE2_VS_POS_SWIZZLE
#undef FABLE2_VS_SKIN
#undef FABLE2_VS_INSTANCE
#undef FABLE2_VS_TERRAIN
#undef FABLE2_VS_UV
#undef FABLE2_VS_LIGHT
};
static const capture::VsLightSpec* VsLight(uint64_t hash) {
  for (const capture::VsLightSpec& v : kVsLight) {
    if (v.vs_hash == hash) return &v;
  }
  return nullptr;
}

// One element of a real draw: the shader's fetch as the capture row lists it ("fetches": fmt, off
// and stride in dwords, swz, signed, norm; the stream's endian is 2 in all), the element's bytes
// in the dumped stream, and what the formulas take from it (lighting-formulas.md).
struct RealElement {
  uint64_t vs;
  bool second;      // the entry's second element (colour or weights); else its normal
  int fetch;        // the fetch's ordinal in the row
  uint32_t format, offset, stride, swizzle;
  bool is_signed, normalized;
  uint32_t count;   // components the reader takes
  uint8_t bytes[16];
  float want[4];
};
// Through the table's entry and the capture's own functions: the element as the capture would
// record it, or false when the entry does not fit the fetch.
static bool RealElementOf(const RealElement& r, capture::LightElement* e) {
  const capture::VsLightSpec* vs = VsLight(r.vs);
  if (!vs) return false;
  const capture::LightFetchSpec& spec = r.second ? vs->second : vs->normal;
  if (spec.fetch != r.fetch || spec.format != r.format || spec.offset_dwords != r.offset) return false;
  capture::VertexFetch f;
  f.format = r.format;
  f.offset_dwords = int32_t(r.offset);
  f.stride_dwords = r.stride;
  f.dst_swizzle = r.swizzle;
  f.is_signed = r.is_signed;
  f.normalized = r.normalized;
  uint8_t source = 0;
  if (!capture::LightSource(spec.source, r.count, f, &source)) return false;
  return capture::MakeLightElement(f, source, r.count, {true, {0x1000, r.stride * 4}, 2}, e);
}

static capture::PosLayout HalfRow(uint32_t offset, uint32_t stride) {
  capture::PosLayout l;
  l.format = capture::PosFormat::kHalf4;
  l.offset_bytes = offset;
  l.stride_bytes = stride;
  l.swizzle = 0x688;  // memory order: the row as written by PutHalf4
  return l;
}

static bool ReadFile(const std::string& path, std::vector<uint8_t>* out) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  out->resize(n > 0 ? size_t(n) : 0);
  const bool ok = out->empty() || std::fread(out->data(), 1, out->size(), f) == out->size();
  std::fclose(f);
  return ok;
}

// One bundle of out/native-evidence/lighting/task5a/normal_survey.py --export: a mesh of a capture
// with its streams and the triangles of its sampled indices. Builds the positions and the normals
// with the functions the geometry cache calls and compares each triangle's face normal with the
// mean of its three vertex normals. Returns the mean |cos|, or a negative number on a bad bundle.
static double RealBundle(const std::string& path) {
  std::vector<uint8_t> file;
  if (!ReadFile(path, &file) || file.size() < 8 + 15 * 4 || std::memcmp(file.data(), "F2NB0001", 8) != 0) return -1.0;
  size_t at = 8;
  bool ok = true;
  auto u32 = [&]() -> uint32_t {
    uint32_t v = 0;
    if (at + 4 > file.size()) { ok = false; return 0; }
    std::memcpy(&v, file.data() + at, 4);
    at += 4;
    return v;
  };
  auto f32 = [&]() -> float {
    const uint32_t u = u32();
    float f;
    std::memcpy(&f, &u, 4);
    return f;
  };
  auto layout = [&]() {
    capture::PosLayout l;
    l.format = capture::PosFormatFromXenos(u32());
    l.offset_bytes = u32();
    l.stride_bytes = u32();
    l.swizzle = u32();
    l.swap16 = u32() != 0;
    return l;
  };
  const uint32_t kind = u32(), tri_count = u32();
  const capture::PosLayout pos = layout();
  capture::LightElement e;
  e.format = uint8_t(u32());
  e.offset_bytes = uint16_t(u32());
  e.stride_bytes = uint16_t(u32());
  // The bundle holds the fetch's destination swizzle (the capture row's, of the time the element
  // carried it). The selector is the table's for the element's format, "xyz" or "yxw" (every
  // committed entry of a format names the same: the table's rows below), admitted as the capture
  // admits it: the fetch delivers the three components.
  capture::VertexFetch fetch;
  fetch.format = e.format;
  fetch.dst_swizzle = u32();
  const uint32_t spec = e.format == capture::kVertexFormat_16_16_16_16_FLOAT ? 0x331u : 0x324u;
  if (!capture::LightSource(spec, 3, fetch, &e.source)) return -11.0;
  e.source_count = 3;
  e.flags = uint8_t(u32());
  const uint32_t vb_size = u32(), nb_size = u32(), aux_size = u32();
  capture::BoneSkin skin;
  capture::InstanceSet inst;
  if (kind == 1) {
    skin.active = true;
    skin.bones = u32();
    skin.index_offset_bytes = u32();
    for (uint32_t& s : skin.index_shift) s = u32();
    skin.index_endian = u32();
    skin.weighted = u32() != 0;
    skin.weight_offset_bytes = u32();
    for (uint32_t& s : skin.weight_shift) s = u32();
    skin.bone_stride = u32();
    for (capture::PosLayout& r : skin.rows) r = layout();
    skin.palette_size = aux_size;
  } else if (kind == 2) {
    inst.active = true;
    for (capture::PosLayout& r : inst.rows) r = layout();
    inst.inv_count = f32();
    inst.count = f32();
    inst.first = f32();
    inst.bias = f32();
    for (float& o : inst.offset) o = f32();
    inst.flat_count = u32();
    inst.rows_size = aux_size;
  } else if (kind != 0) {
    return -2.0;
  }
  if (!ok || uint64_t(at) + vb_size + nb_size + aux_size + uint64_t(tri_count) * 12 != file.size()) return -3.0;
  const uint8_t* vb = file.data() + at;
  const uint8_t* nb = vb + vb_size;
  const uint8_t* aux = nb + nb_size;
  at += size_t(vb_size) + nb_size + aux_size;
  e.vb = {0, nb_size};
  if (!NormalElementSupported(e)) return -4.0;

  std::vector<capture::Float4> positions;
  std::vector<Float3> normals;
  const uint32_t stream_count = capture::PositionBytes(pos.format) && pos.stride_bytes && vb_size >= pos.offset_bytes + capture::PositionBytes(pos.format)
                                    ? (vb_size - pos.offset_bytes - capture::PositionBytes(pos.format)) / pos.stride_bytes + 1
                                    : 0;
  const uint32_t normal_count = NormalElementCount(e);
  if (stream_count == 0 || normal_count == 0) return -5.0;
  if (kind == 2) {
    const uint32_t mesh_count = std::min(std::min(stream_count, normal_count), uint32_t(inst.count));
    std::vector<capture::Float4> mesh(mesh_count);
    std::vector<Float3> mesh_normals(mesh_count);
    positions.resize(inst.flat_count);
    normals.resize(inst.flat_count);
    if (!capture::DecodePositions(vb, vb_size, pos, 0, mesh_count, mesh.data()) ||
        !DecodeNormals(nb, nb_size, e, 0, mesh_count, mesh_normals.data()) ||
        !capture::ExpandInstances(mesh.data(), mesh_count, aux, aux_size, inst, inst.flat_count, positions.data()) ||
        !ExpandInstanceNormals(mesh_normals.data(), mesh_count, aux, aux_size, inst, inst.flat_count, normals.data())) {
      return -6.0;
    }
  } else {
    const uint32_t count = std::min(stream_count, normal_count);
    positions.resize(count);
    normals.resize(count);
    if (!capture::DecodePositions(vb, vb_size, pos, 0, count, positions.data()) ||
        !DecodeNormals(nb, nb_size, e, 0, count, normals.data())) {
      return -7.0;
    }
    if (kind == 1 && (!capture::SkinPositions(vb, vb_size, aux, aux_size, skin, pos.stride_bytes, 0, count, positions.data()) ||
                      !SkinNormals(vb, vb_size, aux, aux_size, skin, pos.stride_bytes, 0, count, normals.data()))) {
      return -8.0;
    }
  }
  double sum_abs = 0.0, sum = 0.0;
  uint32_t counted = 0, without = 0;
  for (uint32_t t = 0; t < tri_count; ++t) {
    const uint32_t i[3] = {u32(), u32(), u32()};
    if (i[0] >= positions.size() || i[1] >= positions.size() || i[2] >= positions.size()) return -9.0;
    const capture::Float4 p0 = positions[i[0]], p1 = positions[i[1]], p2 = positions[i[2]];
    const double e1[3] = {double(p1.x) - p0.x, double(p1.y) - p0.y, double(p1.z) - p0.z};
    const double e2[3] = {double(p2.x) - p0.x, double(p2.y) - p0.y, double(p2.z) - p0.z};
    const double f[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
    double m[3] = {0, 0, 0};
    bool every = true;
    for (uint32_t k : i) {
      every = every && !IsZero(normals[k]);
      m[0] += normals[k].x; m[1] += normals[k].y; m[2] += normals[k].z;
    }
    const double fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]), ml = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    const double l1 = std::sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]), l2 = std::sqrt(e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2]);
    if (!every) ++without;
    if (!every || !(fl > 1e-12) || !(ml > 1e-12) || !(l1 >= 1e-6) || !(l2 >= 1e-6)) continue;  // (NaN positions too)
    const double c = (f[0] * m[0] + f[1] * m[1] + f[2] * m[2]) / (fl * ml);
    sum_abs += std::fabs(c);
    sum += c;
    ++counted;
  }
  if (!ok || counted == 0) return -10.0;
  static const char* const kKinds[3] = {"static", "skinned", "instanced"};
  std::printf("normals of %s: %s, %zu vertices, %u of %u triangles (%u with a vertex without a direction): "
              "mean |cos| %.4f, mean cos %+.4f\n",
              path.c_str(), kKinds[kind], positions.size(), counted, tri_count, without, sum_abs / counted, sum / counted);
  return sum_abs / counted;
}

int main() {
  const float inf = std::numeric_limits<float>::infinity();
  volatile float not_finite = NAN;  // volatile: no constant folding
  const float nan = not_finite;

  // --- DecodeNormal: hand-packed 10_11_11 words (big-endian guest dwords) ---
  {
    uint8_t w[4];
    float n[3];
    PutBe32(w, Pack(1023, 0, 0));  // +x: the positive maximum of an 11-bit field
    CHECK(1, NormalOfFormat(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, 1, 0, 0));
    PutBe32(w, Pack(0, -1023, 0));  // -y
    CHECK(2, NormalOfFormat(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, 0, -1, 0));
    PutBe32(w, Pack(0, 0, 511));  // +z: the positive maximum of the 10-bit field
    CHECK(3, NormalOfFormat(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, 0, 0, 1));
    // The fields are told apart by their place: x in the low bits, z in the high ones.
    PutBe32(w, 0x000003FFu);
    CHECK(4, NormalOfFormat(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, 1, 0, 0));
    PutBe32(w, 0x7FC00000u);
    CHECK(5, NormalOfFormat(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, 0, 0, 1));
    PutBe32(w, 0x001FF800u);
    CHECK(6, NormalOfFormat(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, 0, 1, 0));
    // A diagonal: (600 / 1023, -400 / 1023, 300 / 511), normalised. The fields have different
    // maxima, so each is divided by its own.
    {
      PutBe32(w, Pack(600, -400, 300));
      const float x = 600.0f / 1023.0f, y = -400.0f / 1023.0f, z = 300.0f / 511.0f;
      const float len = std::sqrt(x * x + y * y + z * z);
      CHECK(7, NormalOfFormat(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, x / len, y / len, z / len));
      CHECK(8, Near(n[0] * n[0] + n[1] * n[1] + n[2] * n[2], 1.0f));
    }
    // The most negative code is -1, as the code one above it (the SDK's max(-1, v / maximum)).
    PutBe32(w, Pack(-1024, 0, 0));
    CHECK(9, NormalOfFormat(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, -1, 0, 0));
    PutBe32(w, Pack(-1024, 0, -512));
    CHECK(10, NormalOfFormat(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, -std::sqrt(0.5f), 0, -std::sqrt(0.5f)));
    PutBe32(w, Pack(-1024, 0, -511));  // z = -1 exactly here too: both -1
    CHECK(11, NormalOfFormat(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, -std::sqrt(0.5f), 0, -std::sqrt(0.5f)));
    // Nothing from a zero vector, short data, no data, another format.
    PutBe32(w, 0);
    n[0] = n[1] = n[2] = 7.0f;
    CHECK(12, !NormalOfFormat(capture::kVertexFormat_10_11_11, w, 4, n));
    PutBe32(w, Pack(1023, 0, 0));
    CHECK(13, !NormalOfFormat(capture::kVertexFormat_10_11_11, w, 3, n));
    CHECK(14, !NormalOfFormat(capture::kVertexFormat_10_11_11, nullptr, 4, n));
    CHECK(15, !NormalOfFormat(0, w, 4, n));
    CHECK(16, !NormalOfFormat(17, w, 4, n));  // k_11_11_10: another split of the bits
    CHECK(17, !NormalOfFormat(capture::kVertexFormat_8_8_8_8, w, 4, n) && !NormalOfFormat(capture::kVertexFormat_32_32_32_32_FLOAT, w, 4, n));
    // The bytes are the guest's: big-endian. The same bytes read the other way round are another normal.
    const uint8_t be[4] = {0x00, 0x00, 0x03, 0xFF};
    CHECK(18, NormalOfFormat(capture::kVertexFormat_10_11_11, be, 4, n) && Near3(n, 1, 0, 0));
  }

  // --- DecodeNormal: the half-float element of the instanced meshes (memory order x, y, z, unused) ---
  {
    uint8_t h[8];
    float n[3];
    PutHalf4(h, 0.0f, 0.0f, 2.0f, 9.0f);  // not unit length: normalised; the fourth half is not the normal's
    CHECK(20, NormalOfFormat(capture::kVertexFormat_16_16_16_16_FLOAT, h, 8, n) && Near3(n, 0, 0, 1));
    PutHalf4(h, 3.0f, -4.0f, 0.0f, 0.0f);
    CHECK(21, NormalOfFormat(capture::kVertexFormat_16_16_16_16_FLOAT, h, 8, n) && Near3(n, 0.6f, -0.8f, 0));
    CHECK(22, !NormalOfFormat(capture::kVertexFormat_16_16_16_16_FLOAT, h, 7, n));
    PutHalf4(h, 0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(23, !NormalOfFormat(capture::kVertexFormat_16_16_16_16_FLOAT, h, 8, n));
    PutBe16(h, 0x7C00);  // infinity: no direction
    CHECK(24, !NormalOfFormat(capture::kVertexFormat_16_16_16_16_FLOAT, h, 8, n));
    PutHalf4(h, 1.0f, 0.0f, 0.0f, 0.0f);
    PutBe16(h + 2, 0x7E00);  // a NaN
    CHECK(25, !NormalOfFormat(capture::kVertexFormat_16_16_16_16_FLOAT, h, 8, n));
  }

  // --- Which source components are a normal, by the element's format: what a selector is held against ---
  {
    uint8_t c[3] = {9, 9, 9};
    CHECK(26, NormalSourceComponents(capture::kVertexFormat_10_11_11, c) && c[0] == 0 && c[1] == 1 && c[2] == 2);
    // The halves: source y, x, w. Under the 8in32 pair swap the source components of the halves
    // in memory (0, 1, 2, 3) are (y, x, w, z): the normal is the first three halves, as stored.
    CHECK(27, NormalSourceComponents(capture::kVertexFormat_16_16_16_16_FLOAT, c) && c[0] == 1 && c[1] == 0 && c[2] == 3);
    c[0] = c[1] = c[2] = 9;
    CHECK(28, !NormalSourceComponents(capture::kVertexFormat_8_8_8_8, c) && !NormalSourceComponents(0, c) &&
                  !NormalSourceComponents(17, c) && !NormalSourceComponents(capture::kVertexFormat_32_32_32_32_FLOAT, c) && c[0] == 9);
    // Three different halves tell the components apart: memory (1, 2, 4, 8) is the normal (1, 2, 4) / sqrt 21.
    uint8_t h[8];
    float n[3];
    PutHalf4(h, 1.0f, 2.0f, 4.0f, 8.0f);
    const float len = std::sqrt(21.0f);
    CHECK(29, NormalOfFormat(capture::kVertexFormat_16_16_16_16_FLOAT, h, 8, n) && Near3(n, 1 / len, 2 / len, 4 / len));
  }

  // --- Which elements are read as a normal ---
  {
    const capture::LightElement e = Element10(200);
    CHECK(30, NormalElementSupported(e) && NormalElementCount(e) == 10);
    capture::LightElement a = e;
    // The selector is held against the format: only source x, y, z is a 10_11_11 normal. Another
    // order, a fourth component, a component twice, none: no normals, never the bytes another way.
    for (const std::vector<int>& other : std::vector<std::vector<int>>{{1, 0, 2}, {2, 1, 0}, {0, 2, 1}, {1, 2, 0}, {2, 0, 1},
                                             {0, 1, 3}, {0, 0, 2}, {0, 1, 2, 3}, {0, 1}, {}}) {
      a = e;
      SetSource(a, other);
      CHECK(31, !NormalElementSupported(a) && NormalElementCount(a) == 0);
    }
    a = e;
    a.source_count = 4;  // (three components and a count of four)
    CHECK(32, !NormalElementSupported(a));
    a = e;
    a.source = uint8_t(e.source | 0x40);  // bits past the three components
    CHECK(33, !NormalElementSupported(a));
    a = e;
    a.flags = uint8_t(e.flags & ~capture::kLightElementSigned);  // unsigned: 0..1, another meaning
    CHECK(34, !NormalElementSupported(a));
    a = e;
    a.flags = uint8_t(e.flags & ~capture::kLightElementNormalized);  // integers
    CHECK(35, !NormalElementSupported(a));
    for (uint32_t endian : {0u, 1u, 3u}) {  // only 8in32 is read (the guest's big-endian dword)
      a = e;
      a.flags = uint8_t((e.flags & 3u) | (endian << capture::kLightElementEndianShift));
      CHECK(36, !NormalElementSupported(a));
    }
    a = e;
    a.format = 17;  // k_11_11_10
    CHECK(37, !NormalElementSupported(a));
    a = e;
    a.format = uint8_t(capture::kVertexFormat_8_8_8_8);
    CHECK(38, !NormalElementSupported(a));
    a = e;
    a.stride_bytes = 0;
    CHECK(39, !NormalElementSupported(a) && NormalElementCount(a) == 0);
    a = e;
    a.offset_bytes = 18;  // the element would end past its vertex
    CHECK(40, !NormalElementSupported(a) && NormalElementCount(a) == 0);
    // Counts: every vertex whose element lies inside the stream.
    a = e;
    a.vb.size = 12;  // one element, the vertex itself cut short
    CHECK(41, NormalElementCount(a) == 1);
    a.vb.size = 11;
    CHECK(42, NormalElementCount(a) == 0);
    a.vb.size = 31;
    CHECK(43, NormalElementCount(a) == 1);
    a.vb.size = 32;
    CHECK(44, NormalElementCount(a) == 2);

    const capture::LightElement h = ElementHalf(240);
    CHECK(45, NormalElementSupported(h) && NormalElementCount(h) == 10);
    // The halves: only source y, x, w. What the registers of 0x48D3... and 0xFC4F... hold in x, y, z
    // (their fetch is yzxw: source y, z, x) is not the normal, nor is the 10_11_11 order.
    for (const std::vector<int>& other : std::vector<std::vector<int>>{{1, 2, 0}, {0, 1, 2}, {0, 1, 3}, {1, 0, 2}, {3, 0, 1},
                                             {1, 0, 3, 2}, {1, 1, 3}}) {
      a = h;
      SetSource(a, other);
      CHECK(46, !NormalElementSupported(a) && NormalElementCount(a) == 0);
    }
    a = h;
    a.flags = uint8_t(1u << capture::kLightElementEndianShift);  // 8in16
    CHECK(49, !NormalElementSupported(a));
    a = h;
    a.flags = uint8_t(2u << capture::kLightElementEndianShift);  // sign and scale mean nothing for floats
    CHECK(50, NormalElementSupported(a));
  }

  // --- DecodeNormals: a stream ---
  {
    const capture::LightElement e = Element10(100);  // five vertices of 20 bytes
    std::vector<uint8_t> s(100, 0xEE);
    PutBe32(&s[8], Pack(1023, 0, 0));
    PutBe32(&s[28], Pack(0, 1023, 0));
    PutBe32(&s[48], Pack(0, 0, -511));
    PutBe32(&s[68], 0);  // no direction
    PutBe32(&s[88], Pack(700, 700, 0));
    Float3 out[6];
    for (Float3& o : out) o = {9, 9, 9};
    CHECK(60, DecodeNormals(s.data(), s.size(), e, 0, 5, out));
    CHECK(61, Near3(out[0], 1, 0, 0) && Near3(out[1], 0, 1, 0) && Near3(out[2], 0, 0, -1));
    CHECK(62, IsZero(out[3]));  // a vertex without a direction: zero, not a guess, and the others stand
    CHECK(63, Near3(out[4], std::sqrt(0.5f), std::sqrt(0.5f), 0) && out[5].x == 9);
    // From a first vertex on.
    CHECK(64, DecodeNormals(s.data(), s.size(), e, 3, 2, out) && IsZero(out[0]) && Near3(out[1], std::sqrt(0.5f), std::sqrt(0.5f), 0));
    // Bounded by the stream: one vertex too many, a stream cut inside the last element, no stream.
    CHECK(65, !DecodeNormals(s.data(), s.size(), e, 0, 6, out));
    CHECK(66, !DecodeNormals(s.data(), 91, e, 0, 5, out) && DecodeNormals(s.data(), 92, e, 0, 5, out));
    CHECK(67, !DecodeNormals(nullptr, s.size(), e, 0, 5, out));
    CHECK(68, !DecodeNormals(s.data(), s.size(), e, 0xFFFFFFFFu, 2, out));  // no wrap of the vertex index
    CHECK(69, DecodeNormals(s.data(), s.size(), e, 0, 0, out));
    // An element no reader decodes: nothing, never the bytes read another way.
    capture::LightElement a = e;
    a.format = uint8_t(capture::kVertexFormat_8_8_8_8);
    CHECK(70, !DecodeNormals(s.data(), s.size(), a, 0, 5, out));
    a = e;
    SetSource(a, {1, 0, 2});  // a selector that is not the format's
    out[0] = {9, 9, 9};
    CHECK(71, !DecodeNormals(s.data(), s.size(), a, 0, 5, out) && out[0].x == 9);
    {
      float n[3] = {9, 9, 9};
      CHECK(72, !DecodeNormal(a, &s[8], 4, n) && n[0] == 9 && DecodeNormal(e, &s[8], 4, n) && Near3(n, 1, 0, 0));
      CHECK(76, !DecodeNormal(e, nullptr, 4, n) && !DecodeNormal(e, &s[8], 3, n));
    }

    // Half floats: the first three halves in memory (source y, x, w).
    capture::LightElement h = ElementHalf(48);
    std::vector<uint8_t> m(48, 0);
    PutHalf4(&m[8], 0.0f, 3.0f, 4.0f, 0.5f);
    PutHalf4(&m[32], -1.0f, 0.0f, 0.0f, 0.25f);
    CHECK(73, DecodeNormals(m.data(), m.size(), h, 0, 2, out) && Near3(out[0], 0, 0.6f, 0.8f) && Near3(out[1], -1, 0, 0));
    SetSource(h, {1, 2, 0});  // what a yzxw fetch leaves in its register's x, y, z
    CHECK(74, !DecodeNormals(m.data(), m.size(), h, 0, 2, out));
    h = ElementHalf(48);
    CHECK(75, !DecodeNormals(m.data(), 39, h, 0, 2, out));
  }

  // --- TransformNormal: normalize(M n), the matrix as it is (the game's arithmetic) ---
  // (The inverse transpose, which keeps a normal perpendicular under a scale that is not uniform, is
  // used by no draw: NormalMatrix and RotateNormal were removed in the final fix wave.)
  {
    float out[3];
    // A rotation by 90 degrees about z: x goes to y.
    const float rz[3][3] = {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}};
    const float x[3] = {1, 0, 0}, z[3] = {0, 0, 1};
    CHECK(80, TransformNormal(rz, x, out) && Near3(out, 0, 1, 0));
    CHECK(81, TransformNormal(rz, z, out) && Near3(out, 0, 0, 1));
    // A rotation times one scale: the direction is the rotation's, the length 1.
    const float rs[3][3] = {{0, -3, 0}, {3, 0, 0}, {0, 0, 3}};
    const float d[3] = {0.6f, 0.0f, 0.8f};
    CHECK(82, TransformNormal(rs, d, out) && Near3(out, 0, 0.6f, 0.8f));
    // A scale that is not uniform: the matrix as it is, normalised. The plane x + z = 0 stretched four
    // times along x has the tangent (4, 0, -1); M n = (4, 0, 1) / sqrt(17) is NOT perpendicular to it
    // (the game's normal is not either: the same three dot products).
    const float sx[3][3] = {{4, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const float n[3] = {std::sqrt(0.5f), 0.0f, std::sqrt(0.5f)};
    const float t1[3] = {4, 0, -1};
    CHECK(83, TransformNormal(sx, n, out) && Near3(out, 4.0f / std::sqrt(17.0f), 0.0f, 1.0f / std::sqrt(17.0f)) &&
                  Near(Dot(out, out), 1.0f) && std::fabs(Dot(out, t1)) > 1.0f);
    // A mirror in x: x turns, the rest stays (sign included: for an orthonormal matrix this is the
    // perpendicular normal).
    const float mx[3][3] = {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    CHECK(87, TransformNormal(mx, d, out) && Near3(out, -0.6f, 0, 0.8f));
    CHECK(88, TransformNormal(mx, z, out) && Near3(out, 0, 0, 1));
    // Every entry counts (a matrix with nothing zero in it), rows times the vector.
    const float g[3][3] = {{0.5f, 3.0f, 1.0f}, {2.0f, 0.25f, 5.0f}, {1.5f, 1.0f, 0.75f}};
    const float pn[3] = {2.0f / 3.0f, -1.0f / 3.0f, 2.0f / 3.0f};
    {
      float mn[3];
      for (int r = 0; r < 3; ++r) mn[r] = Dot(g[r], pn);
      const float len = std::sqrt(Dot(mn, mn));
      CHECK(89, TransformNormal(g, pn, out) && Near3(out, mn[0] / len, mn[1] / len, mn[2] / len, 1e-5f));
    }
    // A matrix that flattens the normal, the zero matrix, a zero normal, values that are not finite:
    // no direction, false and (0, 0, 0).
    const float flat[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 0}};
    CHECK(90, TransformNormal(flat, d, out) && Near3(out, 1, 0, 0));
    const float none[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    out[0] = out[1] = out[2] = 5.0f;
    CHECK(91, !TransformNormal(none, d, out) && out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f);
    out[0] = out[1] = out[2] = 5.0f;
    CHECK(92, !TransformNormal(flat, z, out) && out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f);
    const float zero[3] = {0, 0, 0};
    out[0] = 5.0f;
    CHECK(101, !TransformNormal(rz, zero, out) && out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f);
    for (float bad : {nan, inf, -inf}) {
      float m[3][3] = {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}};
      m[1][1] = bad;
      out[0] = out[1] = out[2] = 5.0f;
      CHECK(94, !TransformNormal(m, d, out) && out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f);
      const float bn[3] = {bad, 0.0f, 1.0f};
      out[0] = out[1] = out[2] = 5.0f;
      CHECK(95, !TransformNormal(rz, bn, out) && out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f);
    }
    // Small and large matrices keep their direction.
    const float tiny[3][3] = {{0, -1e-5f, 0}, {1e-5f, 0, 0}, {0, 0, 1e-5f}};
    CHECK(96, TransformNormal(tiny, d, out) && Near3(out, 0, 0.6f, 0.8f));
    const float big[3][3] = {{0, -1e6f, 0}, {1e6f, 0, 0}, {0, 0, 1e6f}};
    CHECK(97, TransformNormal(big, d, out) && Near3(out, 0, 0.6f, 0.8f));
  }

  // --- ClassifyRotation: what a draw's c4..c6 times c7.w is ---
  {
    const float rz[3][3] = {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}};
    CHECK(102, ClassifyRotation(rz, 1.0f) == RotationKind::kRotation);
    // The captured form: the rows carry the object's scale and c7.w its reciprocal (a trunk at 0.67).
    const float scaled[3][3] = {{0, -1.5f, 0}, {1.5f, 0, 0}, {0, 0, 1.5f}};
    CHECK(103, ClassifyRotation(scaled, 1.0f / 1.5f) == RotationKind::kRotation);
    // The same rows with a scale that is not their reciprocal (an identity scale on scaled rows): not assumed away.
    CHECK(104, ClassifyRotation(scaled, 1.0f) == RotationKind::kSkewed);
    CHECK(105, ClassifyRotation(rz, 0.5f) == RotationKind::kSkewed);
    const float stretch[3][3] = {{4, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    CHECK(106, ClassifyRotation(stretch, 1.0f) == RotationKind::kSkewed);
    const float mirror[3][3] = {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    CHECK(107, ClassifyRotation(mirror, 1.0f) == RotationKind::kMirrored);
    CHECK(108, ClassifyRotation(rz, -1.0f) == RotationKind::kMirrored);  // a negative c7.w turns the normal
    const float mirror_stretch[3][3] = {{-4, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    CHECK(109, ClassifyRotation(mirror_stretch, 1.0f) == RotationKind::kSkewed);
    const float zero[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    const float flat[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 0}};
    CHECK(130, ClassifyRotation(zero, 1.0f) == RotationKind::kDegenerate && ClassifyRotation(flat, 1.0f) == RotationKind::kDegenerate &&
                   ClassifyRotation(rz, 0.0f) == RotationKind::kDegenerate);
    for (float bad : {nan, inf, -inf}) {
      float m[3][3] = {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}};
      CHECK(131, ClassifyRotation(m, bad) == RotationKind::kDegenerate);
      m[2][0] = bad;
      CHECK(132, ClassifyRotation(m, 1.0f) == RotationKind::kDegenerate);
    }
    // Within the tolerance of the captured rows (5e-7) and well past it.
    const float near_rot[3][3] = {{0, -1.0004f, 0}, {1.0004f, 0, 0}, {0, 0, 1}};
    const float off_rot[3][3] = {{0, -1.01f, 0}, {1.01f, 0, 0}, {0, 0, 1}};
    CHECK(133, ClassifyRotation(near_rot, 1.0f) == RotationKind::kRotation && ClassifyRotation(off_rot, 1.0f) == RotationKind::kSkewed);
    const float shear[3][3] = {{1, 0.1f, 0}, {0, 1, 0}, {0, 0, 1}};
    CHECK(134, ClassifyRotation(shear, 1.0f) == RotationKind::kSkewed);
    // Rows of unit length that are not at right angles, and rows at right angles of other lengths.
    const float leaning[3][3] = {{1, 0, 0}, {0.6f, 0.8f, 0}, {0, 0, 1}};
    CHECK(156, ClassifyRotation(leaning, 1.0f) == RotationKind::kSkewed);
    const float long_row[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1.1f}};
    CHECK(157, ClassifyRotation(long_row, 1.0f) == RotationKind::kSkewed);
    // An entry that is not finite is degenerate also where the determinant comes out infinite and not NaN.
    const float one_inf[3][3] = {{inf, 1, 1}, {1, 1, 2}, {1, 3, 1}};
    CHECK(158, ClassifyRotation(one_inf, 1.0f) == RotationKind::kDegenerate);
    CHECK(135, std::string(RotationKindName(RotationKind::kRotation)) == "rotation" &&
                   std::string(RotationKindName(RotationKind::kMirrored)) == "mirrored" &&
                   std::string(RotationKindName(RotationKind::kSkewed)) == "skewed" &&
                   std::string(RotationKindName(RotationKind::kDegenerate)) == "degenerate" && size_t(RotationKind::kCount) == 4);
  }

  // --- SkinNormals: the bones of the positions (bone_skin.h), applied to the normals ---
  {
    // Vertices of 16 bytes: 10_11_11 normal at 0, bone indices at 4, weights at 8.
    capture::BoneSkin s;
    s.active = true;
    s.bones = 2;
    s.weighted = true;
    s.index_offset_bytes = 4;
    s.weight_offset_bytes = 8;
    s.index_shift[0] = 0; s.index_shift[1] = 8;
    s.weight_shift[0] = 0; s.weight_shift[1] = 8;
    s.index_endian = 2;
    s.bone_stride = 24;
    for (int k = 0; k < 3; ++k) s.rows[k] = HalfRow(uint32_t(8 * k), 24);
    // Bone 0: a rotation by 90 degrees about z with a translation (which a normal ignores).
    // Bone 1: the identity. Bone 2: x stretched four times. Bone 3: a mirror in x.
    std::vector<uint8_t> palette(4 * 24, 0);
    PutHalf4(&palette[0], 0, -1, 0, 5); PutHalf4(&palette[8], 1, 0, 0, 6); PutHalf4(&palette[16], 0, 0, 1, 7);
    PutHalf4(&palette[24], 1, 0, 0, 0); PutHalf4(&palette[32], 0, 1, 0, 0); PutHalf4(&palette[40], 0, 0, 1, 0);
    PutHalf4(&palette[48], 4, 0, 0, 0); PutHalf4(&palette[56], 0, 1, 0, 0); PutHalf4(&palette[64], 0, 0, 1, 0);
    PutHalf4(&palette[72], -1, 0, 0, 0); PutHalf4(&palette[80], 0, 1, 0, 0); PutHalf4(&palette[88], 0, 0, 1, 0);
    auto vertex = [](uint8_t* p, uint32_t normal, uint32_t bone0, uint32_t bone1, uint32_t w0, uint32_t w1) {
      PutBe32(p, normal);
      PutBe32(p + 4, bone0 | (bone1 << 8));
      PutBe32(p + 8, w0 | (w1 << 8));
      PutBe32(p + 12, 0);
    };
    std::vector<uint8_t> vb(7 * 16, 0);
    vertex(&vb[0], Pack(1023, 0, 0), 0, 9, 255, 0);      // bone 0 alone; the unused influence names no bone
    vertex(&vb[16], Pack(1023, 0, 0), 1, 0, 255, 0);     // the identity
    vertex(&vb[32], Pack(723, 0, 361), 2, 0, 255, 0);    // the stretch: about (1, 0, 1) / sqrt 2
    vertex(&vb[48], Pack(614, 0, 409), 3, 0, 255, 0);    // the mirror: (0.6, 0, 0.8)
    vertex(&vb[64], Pack(1023, 0, 0), 0, 1, 128, 127);   // half of each: no rotation any more
    vertex(&vb[80], Pack(1023, 0, 0), 7, 0, 255, 0);     // a bone past the palette
    vertex(&vb[96], Pack(1023, 0, 0), 0, 1, 0, 0);       // every weight zero
    capture::LightElement e = Element10(uint32_t(vb.size()), 16, 0);
    Float3 n[7];
    CHECK(110, DecodeNormals(vb.data(), vb.size(), e, 0, 7, n));
    CHECK(111, SkinNormals(vb.data(), vb.size(), palette.data(), palette.size(), s, 16, 0, 7, n));
    CHECK(112, Near3(n[0], 0, 1, 0));
    CHECK(113, Near3(n[1], 1, 0, 0));
    {
      // A bone that stretches x four times: the game's shaders put the normal through the blended
      // matrix as it is (Nb = B3 n; 0xA1F7... instr 19 to 21), and so does this: (1, 0, 1) / sqrt 2
      // becomes (4, 0, 1) / sqrt 17. It is not the inverse transpose's (1, 0, 4) / sqrt 17, which
      // would be perpendicular to the stretched surface and is not what the game lights with.
      const float got[3] = {n[2].x, n[2].y, n[2].z}, stretched_tangent[3] = {4, 0, -1};
      CHECK(114, Near3(n[2], 4.0f / std::sqrt(17.0f), 0.0f, 1.0f / std::sqrt(17.0f), 2e-3f) && Near(Dot(got, got), 1.0f));
      CHECK(159, std::fabs(Dot(got, stretched_tangent)) > 3.0f && !Near3(n[2], 1.0f / std::sqrt(17.0f), 0.0f, 4.0f / std::sqrt(17.0f), 0.1f));
    }
    CHECK(115, Near3(n[3], -0.6f, 0, 0.8f, 1e-3f));
    {
      // 128/255 of the rotation and 127/255 of the identity: a matrix that is no rotation; the
      // normal is the matrix's own product with it, of unit length: (b, a, 0) / |(b, a)|.
      const float a = 128.0f / 255.0f, b = 127.0f / 255.0f;
      const float len = std::sqrt(a * a + b * b);
      CHECK(116, Near3(n[4], b / len, a / len, 0.0f, 1e-5f) && Near(n[4].x * n[4].x + n[4].y * n[4].y, 1.0f, 1e-5f));
    }
    CHECK(117, IsZero(n[5]) && IsZero(n[6]));  // no position there either (SkinPositions: NaN)
    // The same vertices as SkinPositions sees them: a vertex gets a normal where it gets a position.
    {
      capture::Float4 p[7];
      for (capture::Float4& q : p) q = {1, 2, 3, 1};
      CHECK(118, capture::SkinPositions(vb.data(), vb.size(), palette.data(), palette.size(), s, 16, 0, 7, p));
      for (int i = 0; i < 7; ++i) CHECK(119, std::isnan(p[i].x) == IsZero(n[i]));
      CHECK(120, Near(p[0].x, -2 + 5) && Near(p[0].y, 1 + 6) && Near(p[0].z, 3 + 7));  // (the refactored blend)
    }
    // A zero normal stays zero under any bone.
    Float3 z[1] = {{0, 0, 0}};
    CHECK(121, SkinNormals(vb.data(), vb.size(), palette.data(), palette.size(), s, 16, 0, 1, z) && IsZero(z[0]));
    // Fails, as SkinPositions does: a word outside the stream, no whole bone, a skin that is not active.
    CHECK(122, !SkinNormals(vb.data(), 7 * 16 - 5, palette.data(), palette.size(), s, 16, 0, 7, n));
    CHECK(123, !SkinNormals(vb.data(), vb.size(), palette.data(), 23, s, 16, 0, 7, n));
    CHECK(124, !SkinNormals(vb.data(), vb.size(), nullptr, 0, s, 16, 0, 7, n));
    capture::BoneSkin off = s;
    off.active = false;
    CHECK(125, !SkinNormals(vb.data(), vb.size(), palette.data(), palette.size(), off, 16, 0, 7, n));
    off = s;
    off.bones = 5;
    CHECK(126, !SkinNormals(vb.data(), vb.size(), palette.data(), palette.size(), off, 16, 0, 7, n));
    CHECK(127, !SkinNormals(vb.data(), vb.size(), palette.data(), palette.size(), s, 0, 0, 7, n));
    // From a first vertex on.
    Float3 one[1] = {{1, 0, 0}};
    CHECK(128, SkinNormals(vb.data(), vb.size(), palette.data(), palette.size(), s, 16, 3, 1, one) && Near3(one[0], -1, 0, 0));
  }

  // --- ExpandInstanceNormals: the instance's rows (instance_expand.h), applied to the mesh's normals ---
  {
    capture::InstanceSet s;
    s.active = true;
    s.inv_count = 0.5f;
    s.count = 2.0f;
    s.first = 0.0f;
    s.bias = 0.5f;
    s.offset[0] = 100.0f;  // (the offset moves positions only)
    for (int k = 0; k < 3; ++k) s.rows[k] = HalfRow(uint32_t(8 * k), 24);
    // Copy 0: the rotation about z. Copy 1: x stretched four times. Copy 2: a mirror in x. Copy 3: zero.
    std::vector<uint8_t> rows(4 * 24, 0);
    PutHalf4(&rows[0], 0, -1, 0, 5); PutHalf4(&rows[8], 1, 0, 0, 6); PutHalf4(&rows[16], 0, 0, 1, 7);
    PutHalf4(&rows[24], 4, 0, 0, 0); PutHalf4(&rows[32], 0, 1, 0, 0); PutHalf4(&rows[40], 0, 0, 1, 0);
    PutHalf4(&rows[48], -1, 0, 0, 0); PutHalf4(&rows[56], 0, 1, 0, 0); PutHalf4(&rows[64], 0, 0, 1, 0);
    s.rows_size = uint32_t(rows.size());
    const float h = std::sqrt(0.5f);
    const Float3 mesh[2] = {{1, 0, 0}, {h, 0, h}};
    Float3 out[10];
    for (Float3& o : out) o = {9, 9, 9};
    CHECK(140, ExpandInstanceNormals(mesh, 2, rows.data(), rows.size(), s, 10, out));
    CHECK(141, Near3(out[0], 0, 1, 0) && Near3(out[1], 0, h, h));                       // copy 0
    {
      // A copy that stretches x four times: the game's plant shaders put the normal through the
      // instance's rows as they are and normalise (0x8123... instr 22 to 24, 30, 35, 36; ruling of
      // 2026-10-08), and so does this: (1, 0, 1) / sqrt 2 becomes (4, 0, 1) / sqrt 17. It is not the
      // inverse transpose's (1, 0, 4) / sqrt 17, which is perpendicular to the stretched surface
      // (tangent (4, 0, -1)) and is not what the game lights with.
      const float got[3] = {out[3].x, out[3].y, out[3].z}, stretched_tangent[3] = {4, 0, -1};
      CHECK(142, Near3(out[2], 1, 0, 0) && Near3(out[3], 4 / std::sqrt(17.0f), 0, 1 / std::sqrt(17.0f)) && Near(Dot(got, got), 1.0f));
      CHECK(280, std::fabs(Dot(got, stretched_tangent)) > 3.0f && !Near3(out[3], 1 / std::sqrt(17.0f), 0, 4 / std::sqrt(17.0f), 0.1f));
    }
    CHECK(143, Near3(out[4], -1, 0, 0) && Near3(out[5], -h, 0, h));                     // copy 2
    CHECK(144, IsZero(out[6]) && IsZero(out[7]));                                        // copy 3: no direction
    CHECK(145, IsZero(out[8]) && IsZero(out[9]));                                        // copy 4: past the stream
    {
      // Rows that are no rotation, no scale and no mirror alone (a shear with a mirror and a
      // stretch): the product of the rows with the normal, row by row, of unit length.
      std::vector<uint8_t> skew(24, 0);
      PutHalf4(&skew[0], -2, 0.5f, 0, 3); PutHalf4(&skew[8], 0, 1, 0.25f, 3); PutHalf4(&skew[16], 0.5f, 0, 4, 3);
      capture::InstanceSet one = s;
      one.inv_count = 1.0f;
      one.count = 1.0f;
      const Float3 n[1] = {{0.6f, 0.0f, 0.8f}};
      Float3 got[1];
      // M n = (-2 * 0.6, 0.25 * 0.8, 0.5 * 0.6 + 4 * 0.8) = (-1.2, 0.2, 3.5).
      const float len = std::sqrt(1.2f * 1.2f + 0.2f * 0.2f + 3.5f * 3.5f);
      CHECK(281, ExpandInstanceNormals(n, 1, skew.data(), skew.size(), one, 1, got) &&
                     Near3(got[0], -1.2f / len, 0.2f / len, 3.5f / len, 1e-5f));
    }
    // The flat order is that of the positions: index i is the same copy and vertex in both.
    {
      const capture::Float4 pm[2] = {{1, 0, 0, 1}, {0, 0, 1, 1}};
      capture::Float4 p[10];
      CHECK(146, capture::ExpandInstances(pm, 2, rows.data(), rows.size(), s, 10, p));
      CHECK(147, Near(p[0].x, 105) && Near(p[0].y, 7) && Near(p[5].x, 100) && Near(p[5].z, 1) && std::isnan(p[8].x));
    }
    // A vertex past the mesh's normals (the stream holds fewer than the copy's count): zero.
    CHECK(148, ExpandInstanceNormals(mesh, 1, rows.data(), rows.size(), s, 4, out) && Near3(out[0], 0, 1, 0) && IsZero(out[1]) &&
                   Near3(out[2], 1, 0, 0) && IsZero(out[3]));
    // A mesh normal without a direction stays zero.
    const Float3 none[2] = {{0, 0, 0}, {0, 0, 1}};
    CHECK(149, ExpandInstanceNormals(none, 2, rows.data(), rows.size(), s, 2, out) && IsZero(out[0]) && Near3(out[1], 0, 0, 1));
    // Constants that map no index (garbage): every normal zero, nothing read.
    capture::InstanceSet bad = s;
    bad.inv_count = nan;
    CHECK(150, ExpandInstanceNormals(mesh, 2, rows.data(), rows.size(), bad, 4, out) && IsZero(out[0]) && IsZero(out[3]));
    CHECK(151, !ExpandInstanceNormals(nullptr, 2, rows.data(), rows.size(), s, 4, out));
    CHECK(152, !ExpandInstanceNormals(mesh, 2, nullptr, rows.size(), s, 4, out));
    CHECK(153, !ExpandInstanceNormals(mesh, 2, rows.data(), rows.size(), s, 0, out));
    CHECK(154, !ExpandInstanceNormals(mesh, 2, rows.data(), rows.size(), s, 4, nullptr));
  }

  // --- The colour stream of 0xECD6... ---
  {
    const capture::LightElement e = ElementColour(24);  // six colours
    CHECK(160, ColourElementSupported(e));
    // A D3DCOLOR in guest memory is A, R, G, B. The shader's register after the fetch zyxw holds
    // (R, G, B, A): red is the fetched z (lighting-formulas.md, "Objects").
    const uint8_t s[24] = {0xFF, 0x10, 0x20, 0x30, 0x80, 0xA0, 0xB0, 0xC0, 0x00, 0x00, 0x00, 0x00,
                           0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x02, 0x03, 0x04, 0x40, 0x50, 0x60, 0x70};
    uint32_t out[7];
    for (uint32_t& o : out) o = 0xDEADBEEFu;
    CHECK(161, DecodeColours(s, sizeof(s), e, 0, 6, out));
    CHECK(162, out[0] == 0xFF302010u);  // bytes R, G, B, A = 10, 20, 30, FF
    CHECK(163, out[1] == 0x80C0B0A0u && out[2] == 0u && out[3] == 0xFFFFFFFFu && out[4] == 0x01040302u && out[6] == 0xDEADBEEFu);
    {
      uint8_t bytes[4];
      std::memcpy(bytes, &out[0], 4);  // as a buffer of R8G8B8A8 on a little-endian host
      CHECK(164, bytes[0] == 0x10 && bytes[1] == 0x20 && bytes[2] == 0x30 && bytes[3] == 0xFF);
    }
    // Read at the vertex index plus the draw's offset (vertex c20.x).
    CHECK(165, DecodeColours(s, sizeof(s), e, 4, 2, out) && out[0] == 0x01040302u && out[1] == 0x40706050u);
    // Bounded by the stream's size: one colour too many, an offset past the stream, a cut stream.
    CHECK(166, !DecodeColours(s, sizeof(s), e, 0, 7, out));
    CHECK(167, !DecodeColours(s, sizeof(s), e, 5, 2, out) && DecodeColours(s, sizeof(s), e, 5, 1, out));
    CHECK(168, !DecodeColours(s, sizeof(s), e, 6, 1, out));
    CHECK(169, !DecodeColours(s, 23, e, 0, 6, out) && DecodeColours(s, 23, e, 0, 5, out));
    CHECK(170, !DecodeColours(s, sizeof(s), e, 0xFFFFFFFFu, 2, out));  // no wrap
    CHECK(171, !DecodeColours(nullptr, sizeof(s), e, 0, 1, out));
    CHECK(172, DecodeColours(s, sizeof(s), e, 0, 0, out));
    // Counts: the colours from `first` to the end of the stream, at most `limit`.
    CHECK(173, ColourCount(e, 0, 100) == 6 && ColourCount(e, 0, 4) == 4 && ColourCount(e, 4, 100) == 2 &&
                   ColourCount(e, 5, 100) == 1 && ColourCount(e, 6, 100) == 0 && ColourCount(e, 0xFFFFFFFFu, 100) == 0);
    capture::LightElement a = e;
    a.vb.size = 23;
    CHECK(174, ColourCount(a, 0, 100) == 5);
    a.vb.size = 3;
    CHECK(175, ColourCount(a, 0, 100) == 0);
    // Which source components are red, green, blue, alpha, by what the element is: z, y, x, w. The
    // element's selector is held against it.
    {
      uint8_t comp[4] = {9, 9, 9, 9};
      ColourSourceComponents(comp);
      CHECK(176, comp[0] == 2 && comp[1] == 1 && comp[2] == 0 && comp[3] == 3);
    }
    CHECK(177, ColourElementSupported(e) && DecodeColours(s, sizeof(s), e, 0, 1, out) && out[0] == 0xFF302010u);
    // Not a colour a reader decodes: another selector (memory order, red and blue exchanged; three
    // components, an instance colour's; a component twice), another format, signed, integers.
    for (const std::vector<int>& other : std::vector<std::vector<int>>{{0, 1, 2, 3}, {3, 2, 1, 0}, {2, 1, 0}, {2, 1, 0, 0},
                                             {2, 1, 3, 0}, {}}) {
      a = e;
      SetSource(a, other);
      out[0] = 7;
      CHECK(178, !ColourElementSupported(a) && !DecodeColours(s, sizeof(s), a, 0, 1, out) && out[0] == 7 &&
                     ColourCount(a, 0, 100) == 0);
    }
    a = e;
    a.format = uint8_t(capture::kVertexFormat_10_11_11);
    CHECK(180, !ColourElementSupported(a) && !DecodeColours(s, sizeof(s), a, 0, 1, out));
    // The format alone: elements a reader DOES read, given the colour's own selector (the case above
    // is an unsigned 10_11_11, which no reader reads whatever the colour rule says).
    a = ElementHalf(24);
    SetSource(a, {2, 1, 0, 3});
    out[0] = 7;
    CHECK(187, ElementReadable(a) && !ColourElementSupported(a) && ColourCount(a, 0, 100) == 0 &&
                   !DecodeColours(s, sizeof(s), a, 0, 1, out) && out[0] == 7);
    // Each of the four places of the selector alone: any other component there is no colour.
    for (int place = 0; place < 4; ++place) {
      const int right[4] = {2, 1, 0, 3};
      for (int other = 0; other < 4; ++other) {
        if (other == right[place]) continue;
        std::vector<int> sel(right, right + 4);
        sel[size_t(place)] = other;
        a = e;
        SetSource(a, sel);
        CHECK(188, !ColourElementSupported(a) && ColourCount(a, 0, 100) == 0);
      }
    }
    // More or fewer components than four with the right ones in front.
    for (const std::vector<int>& count : std::vector<std::vector<int>>{{2}, {2, 1}, {2, 1, 0}}) {
      a = e;
      SetSource(a, count);
      CHECK(189, !ColourElementSupported(a));
    }
    a = e;
    a.flags = uint8_t(e.flags | capture::kLightElementSigned);
    CHECK(181, !ColourElementSupported(a));
    a = e;
    a.flags = uint8_t(e.flags & ~capture::kLightElementNormalized);
    CHECK(182, !ColourElementSupported(a));
    a = e;
    a.stride_bytes = 0;
    CHECK(183, !ColourElementSupported(a) && ColourCount(a, 0, 100) == 0);
    a = e;
    a.offset_bytes = 2;  // past its 4-byte element
    CHECK(184, !ColourElementSupported(a));
    for (uint32_t endian : {0u, 1u, 3u}) {
      a = e;
      a.flags = uint8_t(capture::kLightElementNormalized | (endian << capture::kLightElementEndianShift));
      CHECK(185, !ColourElementSupported(a));
    }
    // A colour inside a wider vertex.
    a = e;
    a.stride_bytes = 12;
    a.offset_bytes = 8;
    CHECK(186, ColourElementSupported(a) && ColourCount(a, 0, 100) == 2 && DecodeColours(s, sizeof(s), a, 1, 1, out) &&
                   out[0] == 0x40706050u);
  }

  // --- An element through its selector (DecodeElement): the generic read ---
  {
    // 10_11_11: three components, in any order the selector names.
    capture::LightElement e = Element10(20);
    uint8_t w[16];
    float v[4] = {9, 9, 9, 9};
    PutBe32(w, Pack(1023, -1023, 511));
    CHECK(210, ElementReadable(e) && DecodeElement(e, w, 4, v) == 3 && v[0] == 1.0f && v[1] == -1.0f && v[2] == 1.0f && v[3] == 9);
    PutBe32(w, Pack(600, -400, 300));
    SetSource(e, {2, 0, 1});
    CHECK(211, DecodeElement(e, w, 4, v) == 3 && Near(v[0], 300.0f / 511.0f) && Near(v[1], 600.0f / 1023.0f) && Near(v[2], -400.0f / 1023.0f));
    // Bounded: the element's bytes, a pointer.
    v[0] = 9;
    CHECK(212, DecodeElement(e, w, 3, v) == 0 && DecodeElement(e, nullptr, 4, v) == 0 && v[0] == 9);
    // The format has three components: a selector naming w, four components, one twice, too few.
    for (const std::vector<int>& bad : std::vector<std::vector<int>>{{0, 1, 3}, {0, 1, 2, 3}, {0, 0, 1}, {0, 1}, {}}) {
      SetSource(e, bad);
      CHECK(213, !ElementReadable(e) && DecodeElement(e, w, 4, v) == 0);
    }
    e = Element10(20);
    e.source_count = 5;
    CHECK(214, !ElementReadable(e));
    e = Element10(20);
    e.source = uint8_t(e.source | 0x80);  // a fourth component's bits with a count of three
    CHECK(215, !ElementReadable(e));
    // Halves: source x, y = the low and the high half of the first dword, z, w of the second, so
    // the halves in memory (1, 2, 4, 8) are source (2, 1, 8, 4).
    capture::LightElement h = ElementHalf(24);
    PutHalf4(w, 1.0f, 2.0f, 4.0f, 8.0f);
    SetSource(h, {0, 1, 2, 3});
    CHECK(216, DecodeElement(h, w, 8, v) == 4 && v[0] == 2.0f && v[1] == 1.0f && v[2] == 8.0f && v[3] == 4.0f);
    SetSource(h, {1, 0, 3});
    v[3] = 9;
    CHECK(217, DecodeElement(h, w, 8, v) == 3 && v[0] == 1.0f && v[1] == 2.0f && v[2] == 4.0f && v[3] == 9 && DecodeElement(h, w, 7, v) == 0);
    // 8_8_8_8, unsigned, normalised: source x is the last byte in memory.
    capture::LightElement c = ElementColour(4);
    const uint8_t argb[4] = {0xFF, 0x33, 0x66, 0x99};
    CHECK(218, DecodeElement(c, argb, 4, v) == 4 && Near(v[0], 0.2f) && Near(v[1], 0.4f) && Near(v[2], 0.6f) && v[3] == 1.0f);
    SetSource(c, {2, 1, 0});  // an instance's colour: red, green, blue
    v[3] = 9;
    CHECK(219, DecodeElement(c, argb, 4, v) == 3 && Near(v[0], 0.2f) && Near(v[2], 0.6f) && v[3] == 9);
    capture::LightElement a = c;
    a.flags = uint8_t(c.flags | capture::kLightElementSigned);
    CHECK(220, !ElementReadable(a) && DecodeElement(a, argb, 4, v) == 0);
    a = c;
    a.flags = uint8_t(c.flags & ~capture::kLightElementNormalized);
    CHECK(221, !ElementReadable(a));
    // Four floats in memory order; sign and scale flags mean nothing for them.
    capture::LightElement f;
    f.vb = {0x4000, 16};
    f.stride_bytes = 16;
    f.format = uint8_t(capture::kVertexFormat_32_32_32_32_FLOAT);
    f.flags = uint8_t(capture::kLightElementSigned | (2u << capture::kLightElementEndianShift));
    SetSource(f, {0, 1, 2, 3});
    const float values[4] = {0.25f, -0.5f, 3.0f, -8.0f};
    for (int k = 0; k < 4; ++k) {
      uint32_t bits;
      std::memcpy(&bits, &values[k], 4);
      PutBe32(w + 4 * k, bits);
    }
    CHECK(222, DecodeElement(f, w, 16, v) == 4 && v[0] == 0.25f && v[1] == -0.5f && v[2] == 3.0f && v[3] == -8.0f);
    SetSource(f, {3, 2, 0, 1});
    CHECK(223, DecodeElement(f, w, 16, v) == 4 && v[0] == -8.0f && v[1] == 3.0f && v[2] == 0.25f && v[3] == -0.5f &&
                   DecodeElement(f, w, 15, v) == 0);
    // Only the guest's big-endian dwords, inside the vertex, of a format the light has.
    for (uint32_t endian : {0u, 1u, 3u}) {
      a = f;
      a.flags = uint8_t(endian << capture::kLightElementEndianShift);
      CHECK(224, !ElementReadable(a) && DecodeElement(a, w, 16, v) == 0);
    }
    a = f;
    a.offset_bytes = 4;
    CHECK(225, !ElementReadable(a));
    a = f;
    a.stride_bytes = 0;
    CHECK(226, !ElementReadable(a));
    a = f;
    a.format = 7;  // 2_10_10_10
    CHECK(227, !ElementReadable(a));
  }

  // --- The committed table's selectors on elements of real draws ---
  // Bytes of dumped streams of capture bridge C (native_discovery_20261007_225337.jsonl, frame 1;
  // run 290, made with the user present: RUN UNCHECKED), as
  // out/native-evidence/lighting/task4_fix1_elements.py prints them
  // (task4_fix1_elements_bridge_C.txt): per element the stream's file, the vertex and the byte. Each
  // goes through the table's entry of its vertex shader and the fetch of its row, as the capture
  // makes the element, and through the reader.
  {
    const float h0 = 0.69140625f, h1 = 0.72216796875f, h2 = -0.00239372253f;  // halves 0x3988, 0x39C7, 0x98E7
    const RealElement real[] = {
        // Object normal, 0xECD6... (ps 0x648B...), fetched xyz_: 1A6470C0_0004932C.bin, vertex 9, byte 188.
        {0xECD66A10092E6562ull, false, 1, 16, 2, 5, 0xE88, true, true, 3, {0xFF, 0xA0, 0x17, 0xEA},
         {-22.0f / 1023.0f, -1022.0f / 1023.0f, -2.0f / 511.0f}},
        // Skinned object normal, 0xD4D5... (ps 0x8D90...), fetched _xyz: 1A951940_00016E14.bin, vertex 0, byte 8.
        {0xD4D558DA6A82BDC8ull, false, 1, 16, 2, 7, 0x447, true, true, 3, {0x0A, 0x76, 0x94, 0x33},
         {-973.0f / 1023.0f, -302.0f / 1023.0f, 41.0f / 511.0f}},
        // Plant normal, 0x48D3... (ps 0x2605...), fetched yzxw: 1C44BB00_000011D0.bin, vertex 0, byte 8.
        {0x48D30ECCF684F488ull, false, 5, 32, 2, 6, 0x611, true, false, 3, {0x39, 0x88, 0x39, 0xC7, 0x98, 0xE7, 0x3C, 0x00},
         {h0, h1, h2}},
        // Probe weights of a skinned shader, 0xD4D5..., fetched wzxy: 1AAC9300_0000D130.bin, vertex 0, byte 0.
        {0xD4D558DA6A82BDC8ull, true, 6, 38, 0, 4, 0x213, true, false, 4,
         {0x3D, 0x94, 0xA4, 0xA6, 0x3B, 0x2C, 0xED, 0x3C, 0x3C, 0x86, 0x42, 0x7F, 0xBC, 0x49, 0xFF, 0xA1},
         {0.07258f, 0.002639f, 0.016389f, -0.012329f}},
        // Stream colour, 0xECD6... (ps 0x7892...), fetched zyxw: 1B215E40_00000840.bin, vertex 0, byte 0.
        {0xECD66A10092E6562ull, true, 4, 6, 0, 1, 0x60A, false, true, 4, {0xFF, 0x67, 0x6D, 0x7B},
         {0x67 / 255.0f, 0x6D / 255.0f, 0x7B / 255.0f, 1.0f}},
        // Instance colour, 0x48D3..., fetched _zyx: 1BDE7E80_000002D8.bin, vertex 0, byte 24.
        {0x48D30ECCF684F488ull, true, 3, 6, 6, 7, 0x057, false, true, 3, {0xFE, 0x87, 0x97, 0x9D},
         {0x87 / 255.0f, 0x97 / 255.0f, 0x9D / 255.0f}},
    };
    int index = 0;
    for (const RealElement& r : real) {
      capture::LightElement e;
      float v[4] = {9, 9, 9, 9};
      const bool made = RealElementOf(r, &e);
      CHECK(230 + index, made);
      const uint32_t bytes = capture::LightElementBytes(r.format);
      CHECK(240 + index, made && DecodeElement(e, r.bytes, bytes, v) == r.count);
      for (uint32_t k = 0; k < r.count; ++k) CHECK(250 + index, Near(v[k], r.want[k], 2e-6f));
      if (!r.second) {
        // A normal: unit length as stored (to the element's precision), and the reader's own.
        const float len = std::sqrt(r.want[0] * r.want[0] + r.want[1] * r.want[1] + r.want[2] * r.want[2]);
        float n[3];
        CHECK(260 + index, std::fabs(len - 1.0f) < 2e-3f && NormalElementSupported(e) && DecodeNormal(e, r.bytes, bytes, n) &&
                               Near3(n, r.want[0] / len, r.want[1] / len, r.want[2] / len, 2e-6f));
      }
      ++index;
    }
    // The stream colour through its own reader: red in the low byte.
    {
      capture::LightElement e;
      uint32_t packed = 0;
      CHECK(270, RealElementOf(real[4], &e) && ColourElementSupported(e) && DecodeColours(real[4].bytes, 4, e, 0, 1, &packed) &&
                     packed == 0xFF7B6D67u);
    }
    // What the fetch leaves in its register's x, y, z is not the element's vector: for 0x48D3...
    // (yzxw) that is source y, z, x = (0.691, 1.0, 0.722), no unit vector (the second and third
    // differ from the normal's), and the skinned shaders' _xyz register has no x from the fetch.
    {
      capture::LightElement e;
      float v[4];
      CHECK(271, RealElementOf(real[2], &e));
      e.source = 1 | (2 << 2) | (0 << 4);
      CHECK(272, !NormalElementSupported(e) && DecodeElement(e, real[2].bytes, 8, v) == 3 && v[0] == h0 && v[1] == 1.0f && v[2] == h1);
    }
    // An entry that does not fit its fetch gives no element: another count, a component the fetch
    // does not deliver (the instance colour's fourth byte: its fetches are zyx_ and _zyx).
    {
      capture::LightElement e;
      RealElement r = real[5];
      r.count = 4;
      CHECK(273, !RealElementOf(r, &e));
      r = real[5];
      r.swizzle = 0xFCA;  // zy__: red is not fetched
      CHECK(274, !RealElementOf(r, &e));
    }
    // Every committed entry: a normal's selector is the one this file knows for the format, a stream
    // colour's is ColourSourceComponents, and they are the same for every entry of a format.
    int entries = 0;
    for (const capture::VsLightSpec& v : kVsLight) {
      if (v.kind == capture::LightVertexKind::kTerrain) continue;
      ++entries;
      uint8_t comp[3] = {9, 9, 9};
      CHECK(275, NormalSourceComponents(v.normal.format, comp) && (v.normal.source >> 8) == 3 &&
                     (v.normal.source & 0xFF) == (comp[0] | (comp[1] << 2) | (comp[2] << 4)));
      if (v.kind == capture::LightVertexKind::kStream) CHECK(276, v.second.source == 0x4C6);
      if (v.kind == capture::LightVertexKind::kProbe) CHECK(277, v.second.source == 0x4E4);
      if (v.kind == capture::LightVertexKind::kInstance) CHECK(278, v.second.source == 0x306);
    }
    CHECK(279, entries == 15);  // (12 before the three leaf vertex shaders, plan Task 8c: probe entries)
  }

  // --- The probe weights (lighting-formulas.md, "Objects", probe_colour) ---
  {
    // Four floats at the vertex index in a stream of their own, 16 bytes a vertex, source x, y, z, w
    // (the table's "xyzw": memory order).
    auto weights_element = [](uint32_t size) {
      capture::LightElement e;
      e.vb = {0x4000, size};
      e.stride_bytes = 16;
      e.format = uint8_t(capture::kVertexFormat_32_32_32_32_FLOAT);
      e.flags = uint8_t(capture::kLightElementSigned | (2u << capture::kLightElementEndianShift));
      SetSource(e, {0, 1, 2, 3});
      return e;
    };
    auto put_float = [](uint8_t* p, float f) {
      uint32_t bits;
      std::memcpy(&bits, &f, 4);
      PutBe32(p, bits);
    };
    const capture::LightElement e = weights_element(64);
    {
      uint8_t comp[4] = {9, 9, 9, 9};
      WeightsSourceComponents(comp);
      CHECK(300, comp[0] == 0 && comp[1] == 1 && comp[2] == 2 && comp[3] == 3);
    }
    CHECK(301, WeightsElementSupported(e) && WeightsCount(e) == 4);
    std::vector<uint8_t> s(64);
    for (int i = 0; i < 16; ++i) put_float(&s[size_t(4 * i)], 0.125f * float(i) - 0.5f);  // -0.5, -0.375, ... 1.375
    capture::Float4 w[5];
    for (capture::Float4& x : w) x = {9, 9, 9, 9};
    CHECK(302, DecodeWeights(s.data(), s.size(), e, 0, 4, w) == LightSecond::kNone);
    CHECK(303, w[0].x == -0.5f && w[0].y == -0.375f && w[0].z == -0.25f && w[0].w == -0.125f && w[3].x == 1.0f &&
                   w[3].w == 1.375f && w[4].x == 9);
    // From a first vertex on.
    CHECK(304, DecodeWeights(s.data(), s.size(), e, 2, 2, w) == LightSecond::kNone && w[0].x == 0.5f && w[1].w == 1.375f);
    // Bounded by the stream: one vertex too many, a cut stream, a first vertex near 2^32 (no wrap), no stream.
    CHECK(305, DecodeWeights(s.data(), s.size(), e, 0, 5, w) == LightSecond::kStream &&
                   DecodeWeights(s.data(), s.size(), e, 3, 2, w) == LightSecond::kStream &&
                   DecodeWeights(s.data(), 63, e, 0, 4, w) == LightSecond::kStream &&
                   DecodeWeights(s.data(), 63, e, 0, 3, w) == LightSecond::kNone &&
                   DecodeWeights(s.data(), s.size(), e, 0xFFFFFFFFu, 2, w) == LightSecond::kStream &&
                   DecodeWeights(nullptr, s.size(), e, 0, 1, w) == LightSecond::kStream);
    // Counts: whole elements in the stream.
    {
      capture::LightElement a = e;
      a.vb.size = 63;
      CHECK(306, WeightsCount(a) == 3);
      a.vb.size = 15;
      CHECK(307, WeightsCount(a) == 0);
      a = e;
      a.stride_bytes = 24;  // inside a wider vertex
      a.offset_bytes = 8;
      a.vb.size = 72;
      CHECK(308, WeightsElementSupported(a) && WeightsCount(a) == 3 && DecodeWeights(s.data(), s.size(), a, 1, 1, w) == LightSecond::kNone &&
                     w[0].x == 0.5f);  // bytes 32 to 47: floats 8 to 11
    }
    // No value that is not a number of usable size reaches a buffer: the whole decode fails.
    for (float bad : {nan, inf, -inf, 1025.0f, -1e30f}) {
      std::vector<uint8_t> t = s;
      put_float(&t[36], bad);  // vertex 2, the second weight
      CHECK(309, DecodeWeights(t.data(), t.size(), e, 0, 4, w) == LightSecond::kValue &&
                     DecodeWeights(t.data(), t.size(), e, 0, 2, w) == LightSecond::kNone &&
                     DecodeWeights(t.data(), t.size(), e, 3, 1, w) == LightSecond::kNone);
    }
    {
      std::vector<uint8_t> t = s;
      put_float(&t[0], kProbeWeightMax);
      put_float(&t[4], -kProbeWeightMax);
      put_float(&t[8], -0.0f);
      CHECK(310, DecodeWeights(t.data(), t.size(), e, 0, 1, w) == LightSecond::kNone && w[0].x == 1024.0f && w[0].y == -1024.0f);
    }
    // Not the weights a reader decodes: another selector (the register order of the skinned
    // shaders' fetch wzxy, two exchanged, three components, one twice), another format, another
    // endian, an element past its vertex.
    for (const std::vector<int>& other : std::vector<std::vector<int>>{{3, 2, 0, 1}, {1, 0, 2, 3}, {0, 1, 3, 2}, {3, 2, 1, 0},
                                             {0, 1, 2}, {0, 1, 2, 2}, {}}) {
      capture::LightElement a = e;
      SetSource(a, other);
      w[0].x = 7;
      CHECK(311, !WeightsElementSupported(a) && WeightsCount(a) == 0 &&
                     DecodeWeights(s.data(), s.size(), a, 0, 1, w) == LightSecond::kElement && w[0].x == 7);
    }
    {
      capture::LightElement a = ElementHalf(64);
      SetSource(a, {0, 1, 2, 3});
      CHECK(312, !WeightsElementSupported(a) && DecodeWeights(s.data(), s.size(), a, 0, 1, w) == LightSecond::kElement);
      a = ElementColour(64);
      SetSource(a, {0, 1, 2, 3});
      CHECK(313, !WeightsElementSupported(a));
      for (uint32_t endian : {0u, 1u, 3u}) {
        a = e;
        a.flags = uint8_t(endian << capture::kLightElementEndianShift);
        CHECK(314, !WeightsElementSupported(a));
      }
      a = e;
      a.offset_bytes = 4;
      CHECK(315, !WeightsElementSupported(a));
      a = e;
      a.stride_bytes = 0;
      CHECK(316, !WeightsElementSupported(a) && WeightsCount(a) == 0);
    }
    CHECK(317, DecodeWeights(s.data(), s.size(), e, 0, 0, w) == LightSecond::kNone);
  }

  // --- The probe set index: the position element's source component the table names ---
  {
    // A 16_16_16_16_FLOAT position at byte 0 of a 12-byte vertex, the stream's endian 8in32 (swap16):
    // the halves in memory are x, y, z and the set index, which is the fetch's source z (2).
    capture::PosLayout pos;
    pos.format = capture::PosFormat::kHalf4;
    pos.stride_bytes = 12;
    pos.swizzle = 0xAC1;  // the table's override: (y, x, w, one) of the source; the set index is in no register
    pos.swap16 = true;
    {
      uint8_t comp = 9;
      CHECK(320, ProbeSetSourceComponent(pos, &comp) && comp == 2);
      capture::PosLayout other = pos;
      other.swap16 = false;  // 8in16: the fourth half would be source w; not a layout seen
      comp = 9;
      CHECK(321, !ProbeSetSourceComponent(other, &comp) && comp == 9);
      for (capture::PosFormat f : {capture::PosFormat::kFloat3, capture::PosFormat::kFloat4, capture::PosFormat::kShort4,
                                   capture::PosFormat::kUnknown}) {
        other = pos;
        other.format = f;
        CHECK(322, !ProbeSetSourceComponent(other, &comp) && !ProbeSetSupported(other, 2));
      }
    }
    // The table's component is held against the format: only source z.
    CHECK(323, ProbeSetSupported(pos, 2));
    for (int comp : {-1, 0, 1, 3, 4, 127}) CHECK(324, !ProbeSetSupported(pos, comp));
    std::vector<uint8_t> vb(6 * 12, 0);
    const float sets[6] = {0.0f, 4.0f, 8.0f, 8.0f, 0.0f, 4.0f};
    for (int v = 0; v < 6; ++v) PutHalf4(&vb[size_t(12 * v)], 1.5f + float(v), -2.0f, 0.25f, sets[v]);
    uint32_t a0[7];
    for (uint32_t& x : a0) x = 77;
    CHECK(325, DecodeProbeSets(vb.data(), vb.size(), pos, 2, 0, 6, a0) == LightSecond::kNone);
    CHECK(326, a0[0] == 0 && a0[1] == 4 && a0[2] == 8 && a0[3] == 8 && a0[4] == 0 && a0[5] == 4 && a0[6] == 77);
    // Whatever the fetch's destination swizzle is (the four shaders': zxwy, wxyz, yxwz; the table's override).
    for (uint32_t swizzle : {0x2C2u, 0x443u, 0x4C1u, 0x688u, 0xFFFu}) {
      capture::PosLayout other = pos;
      other.swizzle = swizzle;
      uint32_t b[6];
      CHECK(327, DecodeProbeSets(vb.data(), vb.size(), other, 2, 0, 6, b) == LightSecond::kNone && b[1] == 4 && b[2] == 8 && b[4] == 0);
    }
    // From a first vertex on; inside a wider vertex.
    CHECK(328, DecodeProbeSets(vb.data(), vb.size(), pos, 2, 2, 3, a0) == LightSecond::kNone && a0[0] == 8 && a0[1] == 8 && a0[2] == 0);
    {
      capture::PosLayout wide = pos;
      wide.stride_bytes = 24;
      wide.offset_bytes = 12;  // vertices 1, 3, 5 of the stream above
      CHECK(329, DecodeProbeSets(vb.data(), vb.size(), wide, 2, 0, 3, a0) == LightSecond::kNone && a0[0] == 4 && a0[1] == 8 && a0[2] == 4);
    }
    // Any value but exactly 0, 4 or 8 in any vertex: no sets at all (a value in between is the
    // sign of another element, not something to round as the game's address register would).
    {
      const uint16_t half_nan = 0x7E00, half_inf = 0x7C00, half_tiny = 0x0001, half_minus_zero = 0x8000;
      for (uint16_t bits : {Half(2.0f), Half(4.5f), Half(-4.0f), Half(12.0f), Half(1.0f), Half(3.5f), Half(7.75f), half_nan, half_inf,
                            half_tiny}) {
        std::vector<uint8_t> t = vb;
        PutBe16(&t[3 * 12 + 6], bits);  // vertex 3
        CHECK(330, DecodeProbeSets(t.data(), t.size(), pos, 2, 0, 6, a0) == LightSecond::kProbeSet &&
                       DecodeProbeSets(t.data(), t.size(), pos, 2, 0, 3, a0) == LightSecond::kNone &&
                       DecodeProbeSets(t.data(), t.size(), pos, 2, 4, 2, a0) == LightSecond::kNone);
      }
      std::vector<uint8_t> t = vb;
      PutBe16(&t[6], half_minus_zero);  // -0 is 0
      CHECK(331, DecodeProbeSets(t.data(), t.size(), pos, 2, 0, 1, a0) == LightSecond::kNone && a0[0] == 0);
    }
    // The other components are not the set: the position's own x, y, z here are 1.5 on, -2, 0.25.
    CHECK(332, DecodeProbeSets(vb.data(), vb.size(), pos, 0, 0, 1, a0) == LightSecond::kElement &&
                   DecodeProbeSets(vb.data(), vb.size(), pos, 3, 0, 1, a0) == LightSecond::kElement &&
                   DecodeProbeSets(vb.data(), vb.size(), pos, -1, 0, 1, a0) == LightSecond::kElement);
    // Bounded by the stream.
    CHECK(333, DecodeProbeSets(vb.data(), vb.size(), pos, 2, 0, 7, a0) == LightSecond::kStream &&
                   DecodeProbeSets(vb.data(), vb.size() - 5, pos, 2, 0, 6, a0) == LightSecond::kStream &&
                   DecodeProbeSets(vb.data(), vb.size() - 4, pos, 2, 0, 6, a0) == LightSecond::kNone &&
                   DecodeProbeSets(vb.data(), vb.size(), pos, 2, 0xFFFFFFFFu, 2, a0) == LightSecond::kStream &&
                   DecodeProbeSets(nullptr, vb.size(), pos, 2, 0, 1, a0) == LightSecond::kStream);
    {
      capture::PosLayout none = pos;
      none.stride_bytes = 0;
      CHECK(334, DecodeProbeSets(vb.data(), vb.size(), none, 2, 0, 1, a0) == LightSecond::kElement);
    }

    // The probe buffer: the weights (float4 a vertex) from byte 0, the sets (one word a vertex) from
    // the next multiple of 256 bytes on (a bind offset's alignment).
    CHECK(335, ProbeSetOffset(0) == 0 && ProbeSetOffset(1) == 256 && ProbeSetOffset(16) == 256 && ProbeSetOffset(17) == 512 &&
                   ProbeSetOffset(1000) == 16128 && ProbeBufferBytes(1) == 260 && ProbeBufferBytes(16) == 320 &&
                   ProbeBufferBytes(1000) == 20128 && ProbeSetOffset(0xFFFFFFFFu) == 0x1000000000ull);
    capture::LightElement e;
    e.vb = {0x4000, 96};
    e.stride_bytes = 16;
    e.format = uint8_t(capture::kVertexFormat_32_32_32_32_FLOAT);
    e.flags = uint8_t(capture::kLightElementSigned | (2u << capture::kLightElementEndianShift));
    SetSource(e, {0, 1, 2, 3});
    std::vector<uint8_t> ws(96);
    for (int i = 0; i < 24; ++i) {
      const float f = 0.25f * float(i) - 1.0f;
      uint32_t bits;
      std::memcpy(&bits, &f, 4);
      PutBe32(&ws[size_t(4 * i)], bits);
    }
    std::vector<uint8_t> built(3, 0xEE);
    CHECK(336, BuildProbe(ws.data(), ws.size(), e, vb.data(), vb.size(), pos, 2, 6, &built) == LightSecond::kNone &&
                   built.size() == 256 + 24);
    if (built.size() == 280) {
      float f[24];
      uint32_t words[6];
      std::memcpy(f, built.data(), sizeof(f));
      std::memcpy(words, built.data() + 256, sizeof(words));
      bool same = true;
      for (int i = 0; i < 24; ++i) same = same && f[i] == 0.25f * float(i) - 1.0f;
      CHECK(337, same && words[0] == 0 && words[1] == 4 && words[2] == 8 && words[3] == 8 && words[4] == 0 && words[5] == 4);
      bool padded = true;
      for (size_t i = 96; i < 256; ++i) padded = padded && built[i] == 0;
      CHECK(338, padded);
    }
    // Fewer vertices than the streams hold.
    CHECK(339, BuildProbe(ws.data(), ws.size(), e, vb.data(), vb.size(), pos, 2, 2, &built) == LightSecond::kNone && built.size() == 264);
    // All or nothing: one bad set, one bad weight, a stream too short, an element or a component
    // no reader decodes, no vertex.
    {
      std::vector<uint8_t> t = vb;
      PutBe16(&t[5 * 12 + 6], Half(2.0f));
      CHECK(340, BuildProbe(ws.data(), ws.size(), e, t.data(), t.size(), pos, 2, 6, &built) == LightSecond::kProbeSet && built.empty());
      CHECK(341, BuildProbe(ws.data(), ws.size(), e, t.data(), t.size(), pos, 2, 5, &built) == LightSecond::kNone && built.size() == 276);
      t = ws;
      PutBe32(&t[5 * 16 + 12], 0x7FC00000u);  // a NaN
      CHECK(342, BuildProbe(t.data(), t.size(), e, vb.data(), vb.size(), pos, 2, 6, &built) == LightSecond::kValue && built.empty());
      CHECK(343, BuildProbe(ws.data(), ws.size() - 1, e, vb.data(), vb.size(), pos, 2, 6, &built) == LightSecond::kStream && built.empty() &&
                     BuildProbe(ws.data(), ws.size(), e, vb.data(), vb.size() - 5, pos, 2, 6, &built) == LightSecond::kStream &&
                     BuildProbe(ws.data(), ws.size(), e, vb.data(), vb.size(), pos, 2, 7, &built) == LightSecond::kStream);
      capture::LightElement a = e;
      SetSource(a, {3, 2, 0, 1});
      CHECK(344, BuildProbe(ws.data(), ws.size(), a, vb.data(), vb.size(), pos, 2, 6, &built) == LightSecond::kElement && built.empty() &&
                     BuildProbe(ws.data(), ws.size(), e, vb.data(), vb.size(), pos, 3, 6, &built) == LightSecond::kElement);
      CHECK(345, BuildProbe(ws.data(), ws.size(), e, vb.data(), vb.size(), pos, 2, 0, &built) == LightSecond::kStream && built.empty() &&
                     BuildProbe(ws.data(), ws.size(), e, vb.data(), vb.size(), pos, 2, 6, nullptr) == LightSecond::kStream);
    }
    CHECK(346, std::string(LightSecondName(LightSecond::kNone)) == "none" && std::string(LightSecondName(LightSecond::kElement)) == "element" &&
                   std::string(LightSecondName(LightSecond::kStream)) == "stream" &&
                   std::string(LightSecondName(LightSecond::kProbeSet)) == "probe-set" &&
                   std::string(LightSecondName(LightSecond::kValue)) == "value" &&
                   std::string(LightSecondName(LightSecond::kUpload)) == "upload" && size_t(LightSecond::kCount) == 6);
  }

  // --- The instance colours (lighting-formulas.md, "Instanced plants") ---
  {
    // 8_8_8_8 at byte 24 of the 28-byte instance record, unsigned, normalised; red, green, blue are
    // the fetch's source z, y, x (the table's "zyx"): the bytes in memory are (not fetched), R, G, B.
    auto colour_element = [](uint32_t size) {
      capture::LightElement e;
      e.vb = {0x5000, size};
      e.offset_bytes = 24;
      e.stride_bytes = 28;
      e.format = uint8_t(capture::kVertexFormat_8_8_8_8);
      e.flags = uint8_t(capture::kLightElementNormalized | (2u << capture::kLightElementEndianShift));
      SetSource(e, {2, 1, 0});
      return e;
    };
    {
      uint8_t comp[3] = {9, 9, 9};
      InstanceColourSourceComponents(comp);
      CHECK(350, comp[0] == 2 && comp[1] == 1 && comp[2] == 0);
    }
    // Three copies of 28 bytes; a draw of two vertices a copy.
    std::vector<uint8_t> rows(3 * 28, 0);
    const uint8_t c0[4] = {0xFE, 0x10, 0x20, 0x30}, c1[4] = {0x00, 0xFF, 0x80, 0x01}, c2[4] = {0xFE, 0x87, 0x97, 0x9D};
    std::memcpy(&rows[24], c0, 4);
    std::memcpy(&rows[52], c1, 4);
    std::memcpy(&rows[80], c2, 4);
    const capture::LightElement e = colour_element(uint32_t(rows.size()));
    CHECK(351, InstanceColourElementSupported(e));
    capture::InstanceSet s;
    s.active = true;
    s.inv_count = 0.5f;
    s.count = 2.0f;
    s.first = 0.0f;
    s.bias = 0.5f;
    for (int k = 0; k < 3; ++k) s.rows[k] = HalfRow(uint32_t(8 * k), 28);
    s.rows_size = uint32_t(rows.size());
    uint32_t out[9];
    for (uint32_t& o : out) o = 0xDEADBEEFu;
    CHECK(352, ExpandInstanceColours(rows.data(), rows.size(), e, s, 6, out));
    // Red in the low byte, then green and blue; the fourth byte, which no fetch delivers, is not carried.
    CHECK(353, out[0] == 0x302010u && out[1] == 0x302010u && out[2] == 0x0180FFu && out[3] == 0x0180FFu && out[4] == 0x9D9787u &&
                   out[5] == 0x9D9787u && out[6] == 0xDEADBEEFu);
    // A draw that names a copy past the stream has no colours: none is made up for it.
    CHECK(354, !ExpandInstanceColours(rows.data(), rows.size(), e, s, 7, out) && !ExpandInstanceColours(rows.data(), rows.size(), e, s, 8, out));
    {
      uint8_t bytes[4];
      std::memcpy(bytes, &out[4], 4);  // as a buffer of R8G8B8A8 on a little-endian host, like the colour stream's
      CHECK(355, bytes[0] == 0x87 && bytes[1] == 0x97 && bytes[2] == 0x9D && bytes[3] == 0);
    }
    // The flat order is that of the positions: index i is the same copy in both.
    {
      for (int copy = 0; copy < 3; ++copy) {
        PutHalf4(&rows[size_t(28 * copy)], 1, 0, 0, float(10 * copy));
        PutHalf4(&rows[size_t(28 * copy + 8)], 0, 1, 0, 0);
        PutHalf4(&rows[size_t(28 * copy + 16)], 0, 0, 1, 0);
      }
      const capture::Float4 mesh[2] = {{0, 0, 0, 1}, {1, 0, 0, 1}};
      capture::Float4 p[8];
      CHECK(356, capture::ExpandInstances(mesh, 2, rows.data(), rows.size(), s, 8, p) && Near(p[4].x, 20) && Near(p[5].x, 21) &&
                     std::isnan(p[6].x) && ExpandInstanceColours(rows.data(), rows.size(), e, s, 6, out) && out[4] == 0x9D9787u);
    }
    // The first copy of the draw.
    {
      capture::InstanceSet from = s;
      from.first = 2.0f;
      CHECK(357, ExpandInstanceColours(rows.data(), rows.size(), e, from, 2, out) && out[0] == 0x9D9787u && out[1] == 0x9D9787u &&
                     !ExpandInstanceColours(rows.data(), rows.size(), e, from, 3, out));
    }
    // Bounded by the stream: a copy whose colour lies past it (its rows may still be there) has
    // none, and the draw that names it no colours.
    CHECK(358, !ExpandInstanceColours(rows.data(), rows.size() - 1, e, s, 6, out) &&
                   ExpandInstanceColours(rows.data(), rows.size() - 1, e, s, 4, out) && out[2] == 0x0180FFu && out[3] == 0x0180FFu);
    // An index the constants map to no copy (garbage constants: every index) has no position
    // either: its colour is zero, nothing is read.
    {
      capture::InstanceSet bad = s;
      bad.inv_count = nan;
      out[0] = out[3] = 5;
      CHECK(359, ExpandInstanceColours(rows.data(), rows.size(), e, bad, 4, out) && out[0] == 0 && out[3] == 0);
    }
    CHECK(360, !ExpandInstanceColours(nullptr, rows.size(), e, s, 4, out) && !ExpandInstanceColours(rows.data(), rows.size(), e, s, 0, out) &&
                   !ExpandInstanceColours(rows.data(), rows.size(), e, s, 4, nullptr));
    // Not an instance colour a reader decodes: another selector (the colour stream's four
    // components, memory order, red and blue exchanged, one twice), another format, signed, integers.
    for (const std::vector<int>& other : std::vector<std::vector<int>>{{2, 1, 0, 3}, {0, 1, 2}, {1, 2, 3}, {2, 0, 1}, {2, 1, 1},
                                             {2, 1}, {}}) {
      capture::LightElement a = e;
      SetSource(a, other);
      out[0] = 7;
      CHECK(361, !InstanceColourElementSupported(a) && !ExpandInstanceColours(rows.data(), rows.size(), a, s, 4, out) && out[0] == 7);
    }
    {
      // (elements a reader does read, with the instance colour's selector: only the format differs)
      capture::LightElement a = Element10(20);
      SetSource(a, {2, 1, 0});
      CHECK(362, ElementReadable(a) && !InstanceColourElementSupported(a));
      a = ElementHalf(24);
      SetSource(a, {2, 1, 0});
      CHECK(368, ElementReadable(a) && !InstanceColourElementSupported(a));
      a = e;
      a.flags = uint8_t(e.flags | capture::kLightElementSigned);
      CHECK(363, !InstanceColourElementSupported(a));
      a = e;
      a.flags = uint8_t(e.flags & ~capture::kLightElementNormalized);
      CHECK(364, !InstanceColourElementSupported(a));
      a = e;
      a.offset_bytes = 26;  // past its 28-byte record
      CHECK(365, !InstanceColourElementSupported(a));
      for (uint32_t endian : {0u, 1u, 3u}) {
        a = e;
        a.flags = uint8_t(capture::kLightElementNormalized | (endian << capture::kLightElementEndianShift));
        CHECK(366, !InstanceColourElementSupported(a));
      }
      // The colour stream's reader does not take an instance colour, nor this one a stream colour.
      CHECK(367, !ColourElementSupported(e) && !InstanceColourElementSupported(ElementColour(4)));
    }
  }

  // --- The bend inputs of the instanced plants (lighting-formulas.md, "Instanced plants", plant_normal) ---
  // Per flat position: the copy's up axis U, the third COLUMN of its rows' 3 x 3, normalised
  // (0x8123... instr 40 to 42, 45, 46, 48, 49, 51), and the mesh vertex's z in the copy's space (38).
  {
    capture::InstanceSet s;
    s.active = true;
    s.inv_count = 0.5f;
    s.count = 2.0f;
    s.first = 0.0f;
    s.bias = 0.5f;
    s.offset[0] = 100.0f;  // (the offset moves positions only)
    for (int k = 0; k < 3; ++k) s.rows[k] = HalfRow(uint32_t(8 * k), 24);
    // Copy 0: the rotation about z. Copy 1: rows that are no rotation, third column (3, 0, 4), third
    // row (0, 0, 4). Copy 2: a third column of zeros (the copy is flat). Three copies in the stream.
    std::vector<uint8_t> rows(3 * 24, 0);
    PutHalf4(&rows[0], 0, -1, 0, 5); PutHalf4(&rows[8], 1, 0, 0, 6); PutHalf4(&rows[16], 0, 0, 1, 7);
    PutHalf4(&rows[24], 1, 0, 3, 0); PutHalf4(&rows[32], 0, 2, 0, 0); PutHalf4(&rows[40], 0, 0, 4, 9);
    PutHalf4(&rows[48], 1, 0, 0, 0); PutHalf4(&rows[56], 0, 1, 0, 0); PutHalf4(&rows[64], 2, 3, 0, 0);
    s.rows_size = uint32_t(rows.size());
    const capture::Float4 mesh[2] = {{1, 2, 0.25f, 1}, {5, 6, -0.5f, 1}};
    auto is = [](const capture::Float4& v, float x, float y, float z, float w) {
      return Near(v.x, x) && Near(v.y, y) && Near(v.z, z) && v.w == w;
    };
    auto zero4 = [](const capture::Float4& v) { return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f && v.w == 0.0f; };
    capture::Float4 out[9];
    for (capture::Float4& o : out) o = {9, 9, 9, 9};
    CHECK(400, ExpandInstanceBend(mesh, 2, rows.data(), rows.size(), s, 8, out) == LightSecond::kNone);
    // The height is the position's third component as it is, no other one (x, y and w differ from it).
    CHECK(401, is(out[0], 0, 0, 1, 0.25f) && is(out[1], 0, 0, 1, -0.5f));
    // The third column, not the third row ((0, 0, 1) here), and of unit length.
    CHECK(402, is(out[2], 0.6f, 0, 0.8f, 0.25f) && is(out[3], 0.6f, 0, 0.8f, -0.5f));
    // A copy without an up axis: no direction is made up; the height is the vertex's all the same.
    CHECK(403, is(out[4], 0, 0, 0, 0.25f) && is(out[5], 0, 0, 0, -0.5f) && out[4].x == 0.0f && out[4].z == 0.0f);
    // A copy past the stream has no position either: all four zero. Nothing is written past the count.
    CHECK(404, zero4(out[6]) && zero4(out[7]) && out[8].x == 9 && out[8].w == 9);
    // The flat order is that of the positions: index i is the same copy and vertex in both.
    {
      capture::Float4 p[8];
      CHECK(405, capture::ExpandInstances(mesh, 2, rows.data(), rows.size(), s, 8, p));
      // copy 1, vertex 1: (5 + 3 * -0.5 + 100, 12, -2 + 9); copy 3: no position
      CHECK(405, Near(p[3].x, 103.5f) && Near(p[3].y, 12) && Near(p[3].z, 7) && std::isnan(p[6].x) && is(out[3], 0.6f, 0, 0.8f, -0.5f));
    }
    // The first copy of the draw.
    {
      capture::InstanceSet from = s;
      from.first = 1.0f;
      CHECK(406, ExpandInstanceBend(mesh, 2, rows.data(), rows.size(), from, 4, out) == LightSecond::kNone &&
                     is(out[0], 0.6f, 0, 0.8f, 0.25f) && is(out[3], 0, 0, 0, -0.5f));
    }
    // A vertex past the mesh's positions (the stream holds fewer than the copy's count): zero.
    CHECK(407, ExpandInstanceBend(mesh, 1, rows.data(), rows.size(), s, 4, out) == LightSecond::kNone && is(out[0], 0, 0, 1, 0.25f) &&
                   zero4(out[1]) && is(out[2], 0.6f, 0, 0.8f, 0.25f) && zero4(out[3]));
    // Bounded by the stream: a copy whose rows are cut is past it.
    CHECK(408, ExpandInstanceBend(mesh, 2, rows.data(), rows.size() - 1, s, 6, out) == LightSecond::kNone && is(out[3], 0.6f, 0, 0.8f, -0.5f) &&
                   zero4(out[4]) && zero4(out[5]));
    // A height that is not a number, in a vertex the draw names: no buffer (no value is made up for
    // it, and none that is not a number reaches a buffer). In a vertex the draw does not name: nothing.
    for (float bad_z : {nan, inf, -inf}) {
      const capture::Float4 bad_mesh[2] = {{1, 2, 0.25f, 1}, {5, 6, bad_z, 1}};
      for (capture::Float4& o : out) o = {9, 9, 9, 9};
      CHECK(409, ExpandInstanceBend(bad_mesh, 2, rows.data(), rows.size(), s, 6, out) == LightSecond::kValue);
      CHECK(409, ExpandInstanceBend(bad_mesh, 2, rows.data(), rows.size(), s, 2, out) == LightSecond::kValue);
      CHECK(409, ExpandInstanceBend(bad_mesh, 2, rows.data(), rows.size(), s, 1, out) == LightSecond::kNone && is(out[0], 0, 0, 1, 0.25f));
      // (the vertex's other components are not the bend's: a position that is not a number in x is
      // dropped by the position's own NaN)
      const capture::Float4 bad_x[2] = {{bad_z, 2, 0.25f, 1}, {5, bad_z, -0.5f, bad_z}};
      CHECK(409, ExpandInstanceBend(bad_x, 2, rows.data(), rows.size(), s, 4, out) == LightSecond::kNone && is(out[1], 0, 0, 1, -0.5f));
    }
    // A height of any finite size is kept as it is (the shader saturates the blend it makes of it).
    {
      const capture::Float4 big[2] = {{0, 0, 65504.0f, 1}, {0, 0, -3.0e38f, 1}};
      CHECK(410, ExpandInstanceBend(big, 2, rows.data(), rows.size(), s, 2, out) == LightSecond::kNone && out[0].w == 65504.0f &&
                     out[1].w == -3.0e38f);
    }
    // A copy whose third column is not a number has no up axis (and no position): zero direction.
    {
      std::vector<uint8_t> wild = rows;
      PutBe16(&wild[4], 0x7E00);  // copy 0, row 0, z: a NaN half
      CHECK(411, ExpandInstanceBend(mesh, 2, wild.data(), wild.size(), s, 4, out) == LightSecond::kNone && is(out[0], 0, 0, 0, 0.25f) &&
                     is(out[2], 0.6f, 0, 0.8f, 0.25f));
      PutBe16(&wild[4], 0x7C00);  // +infinity
      CHECK(411, ExpandInstanceBend(mesh, 2, wild.data(), wild.size(), s, 2, out) == LightSecond::kNone && is(out[1], 0, 0, 0, -0.5f));
    }
    // Constants that map no index (garbage): every entry zero, nothing read.
    {
      capture::InstanceSet bad = s;
      bad.inv_count = nan;
      for (capture::Float4& o : out) o = {9, 9, 9, 9};
      CHECK(412, ExpandInstanceBend(mesh, 2, rows.data(), rows.size(), bad, 4, out) == LightSecond::kNone && zero4(out[0]) && zero4(out[3]));
    }
    CHECK(413, ExpandInstanceBend(nullptr, 2, rows.data(), rows.size(), s, 4, out) == LightSecond::kStream &&
                   ExpandInstanceBend(mesh, 2, nullptr, rows.size(), s, 4, out) == LightSecond::kStream &&
                   ExpandInstanceBend(mesh, 2, rows.data(), rows.size(), s, 0, out) == LightSecond::kStream &&
                   ExpandInstanceBend(mesh, 2, rows.data(), rows.size(), s, 4, nullptr) == LightSecond::kStream);
    // The element: four host floats a flat position, as large as a position's (the rule for whatever
    // is bound at a light register).
    static_assert(sizeof(capture::Float4) == 16);
  }

  // --- The committed table on the weights, the set index and the instance colour of real draws ---
  // Bytes of dumped streams of the bridge capture of plan Task 7's runs
  // (native_discovery_20261008_055155.jsonl, frame 1; run 314, made with the user present: RUN
  // UNCHECKED), as out/native-evidence/lighting/task5b/probe_instance_survey.py --bytes prints them
  // (probe_instance_survey_055155.txt): per element the stream's file, the vertex or copy and the
  // byte. Each goes through the table's entry of its vertex shader and the fetch of its row, as the
  // capture makes the element, and through the reader that holds the selector against the format:
  // an entry of any of these shaders that named other components would fail here.
  {
    struct RealProbe {
      RealElement weights;
      uint32_t pos_stride;   // the row's "pos": 16_16_16_16_FLOAT at byte 0, endian 8in32
      uint8_t pos_bytes[8];  // the position element of the same vertex
      uint32_t set;
    };
    const RealProbe probes[] = {
        // 0x475E... (ps 0x00E0...), weights fetched xyzw: 1C256480_00006B60.bin, vertex 3, byte 48;
        // position 1C247280_0000F198.bin, byte 108.
        {{0x475EC9F795E5EDBBull, true, 5, 38, 0, 4, 0x688, true, false, 4,
          {0x3E, 0xCE, 0x36, 0x66, 0x3F, 0x08, 0xDF, 0xAF, 0x3F, 0x2A, 0x77, 0x47, 0x3E, 0xD8, 0xD8, 0x2C},
          {0.402758777f, 0.534663141f, 0.665882528f, 0.423524261f}},
         36, {0xC4, 0x18, 0xC3, 0x89, 0x46, 0xDD, 0x44, 0x00}, 4},
        // 0x79EA... (ps 0x2FD2...), xyzw: 1BE87880_00000640.bin, vertex 3, byte 48; position
        // 1BE87040_000007D0.bin, byte 60.
        {{0x79EAC49585797037ull, true, 4, 38, 0, 4, 0x688, true, false, 4,
          {0xBF, 0x73, 0x86, 0xF2, 0xBE, 0xCD, 0x25, 0x32, 0xBE, 0x7F, 0x8D, 0x78, 0xB2, 0x20, 0x7C, 0x54},
          {-0.951277852f, -0.400674403f, -0.249563098f, -9.34149469e-09f}},
         20, {0x01, 0x6A, 0x89, 0xCC, 0x24, 0xB2, 0x00, 0x00}, 0},
        // 0xA1F7... (ps 0x8D90...), wzxy: 1BFD9040_00000F00.bin, vertex 3, byte 48; position
        // 1C118900_00001A40.bin, byte 84.
        {{0xA1F7E9885EC466DFull, true, 5, 38, 0, 4, 0x213, true, false, 4,
          {0x3D, 0xC6, 0x67, 0x21, 0x3E, 0x13, 0x3E, 0x26, 0x3F, 0x1C, 0x31, 0x4F, 0xBC, 0x68, 0xD3, 0x2D},
          {0.0968763903f, 0.143791765f, 0.610127389f, -0.0142105045f}},
         28, {0xA7, 0x97, 0x38, 0x9B, 0x3B, 0xD6, 0x44, 0x00}, 4},
        // 0xD4D5... (ps 0x8D90...), wzxy: 1AAC9300_0000D130.bin, vertex 3, byte 48; position
        // 1A951940_00016E14.bin, byte 84.
        {{0xD4D558DA6A82BDC8ull, true, 6, 38, 0, 4, 0x213, true, false, 4,
          {0x3F, 0x5D, 0x86, 0x5A, 0x3E, 0x44, 0x5E, 0xE7, 0xBE, 0x01, 0x7C, 0x87, 0x3F, 0x0B, 0x07, 0x3D},
          {0.865331292f, 0.191768274f, -0.126451597f, 0.543079197f}},
         28, {0xC0, 0x61, 0xB3, 0x02, 0x3B, 0x2D, 0x00, 0x00}, 0},
    };
    for (const RealProbe& p : probes) {
      capture::LightElement e;
      capture::Float4 w = {9, 9, 9, 9};
      const bool made = RealElementOf(p.weights, &e);
      CHECK(370, made && WeightsElementSupported(e));
      CHECK(371, made && DecodeWeights(p.weights.bytes, 16, e, 0, 1, &w) == LightSecond::kNone && w.x == p.weights.want[0] &&
                     w.y == p.weights.want[1] && w.z == p.weights.want[2] && w.w == p.weights.want[3]);
      // The set index through the entry's a0 component and the row's position layout.
      const capture::VsLightSpec* vs = VsLight(p.weights.vs);
      capture::PosLayout pos;
      pos.format = capture::PosFormatFromXenos(32);
      pos.stride_bytes = p.pos_stride;
      pos.swizzle = 0xAC1;
      CHECK(372, vs && capture::ApplyFetchEndian(&pos, 2) && ProbeSetSupported(pos, vs->a0_comp));
      uint32_t set = 77;
      CHECK(373, vs && DecodeProbeSets(p.pos_bytes, 8, pos, vs->a0_comp, 0, 1, &set) == LightSecond::kNone && set == p.set);
    }
    // What the skinned shaders' fetch leaves in its register (wzxy) is not the weights' order: taken
    // so, the element is refused.
    {
      capture::LightElement e;
      CHECK(374, RealElementOf(probes[3].weights, &e));
      SetSource(e, {3, 2, 0, 1});
      capture::Float4 w;
      CHECK(375, !WeightsElementSupported(e) && DecodeWeights(probes[3].weights.bytes, 16, e, 0, 1, &w) == LightSecond::kElement);
    }

    // The instance colour of every plant vertex shader (fetch 3 of each row: 8_8_8_8 at dword 6 of
    // the 7-dword record, unsigned, normalised; fetched zyx_ or _zyx).
    const RealElement colours[] = {
        // 0x33C0... (ps 0x165E...), zyx_: 1BCA9C40_000000FC.bin, copy 4, byte 136.
        {0x33C00226C154C1F2ull, true, 3, 6, 6, 7, 0xE0A, false, true, 3, {0xFE, 0x72, 0x7E, 0x7F}, {0x72, 0x7E, 0x7F}},
        // 0x36B5... (ps 0x165E...), zyx_: 1BE06640_000000C4.bin, copy 0, byte 24.
        {0x36B543CECAE5C781ull, true, 3, 6, 6, 7, 0xE0A, false, true, 3, {0xFE, 0x89, 0x98, 0x9F}, {0x89, 0x98, 0x9F}},
        // 0x48D3... (ps 0x2605...), _zyx: 1BDE7E80_000002D8.bin, copy 0, byte 24.
        {0x48D30ECCF684F488ull, true, 3, 6, 6, 7, 0x057, false, true, 3, {0xFE, 0x87, 0x97, 0x9D}, {0x87, 0x97, 0x9D}},
        // 0x6AD4... (ps 0x165E...), _zyx: 1BE06640_000000C4.bin, copy 5, byte 164.
        {0x6AD4108C3FF07966ull, true, 3, 6, 6, 7, 0x057, false, true, 3, {0xFE, 0x87, 0x97, 0xAA}, {0x87, 0x97, 0xAA}},
        // 0x8123... (ps 0xA7E4...), zyx_: 1BD684C0_0000127C.bin, copy 153, byte 4308.
        {0x8123C16DBF583F92ull, true, 3, 6, 6, 7, 0xE0A, false, true, 3, {0xFE, 0x78, 0x84, 0x96}, {0x78, 0x84, 0x96}},
        // 0xB636... (ps 0x165E...), zyx_: 1BCA9D80_0000008C.bin, copy 4, byte 136.
        {0xB636821F95DC9D8Eull, true, 3, 6, 6, 7, 0xE0A, false, true, 3, {0xFE, 0x86, 0x94, 0xA5}, {0x86, 0x94, 0xA5}},
        // 0xFC4F... (ps 0xA7E4...), _zyx: 1BD211C0_000012B4.bin, copy 0, byte 24.
        {0xFC4F2EF6930768BEull, true, 3, 6, 6, 7, 0x057, false, true, 3, {0xFE, 0x68, 0x71, 0x6C}, {0x68, 0x71, 0x6C}},
    };
    capture::InstanceSet one;
    one.active = true;
    one.inv_count = 1.0f;
    one.count = 1.0f;
    one.bias = 0.5f;
    for (const RealElement& r : colours) {
      capture::LightElement e;
      const bool made = RealElementOf(r, &e);
      CHECK(376, made && InstanceColourElementSupported(e) && e.offset_bytes == 24 && e.stride_bytes == 28);
      uint8_t record[28] = {};
      std::memcpy(record + 24, r.bytes, 4);
      uint32_t packed = 7;
      CHECK(377, made && ExpandInstanceColours(record, sizeof(record), e, one, 1, &packed) &&
                     packed == (uint32_t(r.want[0]) | (uint32_t(r.want[1]) << 8) | (uint32_t(r.want[2]) << 16)));
    }
    // Every committed entry of a kind: the weights' selector is WeightsSourceComponents, an
    // instance colour's InstanceColourSourceComponents, the set's component the one this file
    // knows for the position's format.
    int probe_entries = 0, instance_entries = 0;
    for (const capture::VsLightSpec& v : kVsLight) {
      if (v.kind == capture::LightVertexKind::kProbe) {
        ++probe_entries;
        uint8_t comp[4];
        WeightsSourceComponents(comp);
        CHECK(378, v.second.format == capture::kVertexFormat_32_32_32_32_FLOAT && (v.second.source >> 8) == 4 &&
                       (v.second.source & 0xFF) == (comp[0] | (comp[1] << 2) | (comp[2] << 4) | (comp[3] << 6)) && v.a0_comp == 2);
      } else if (v.kind == capture::LightVertexKind::kInstance) {
        ++instance_entries;
        uint8_t comp[3];
        InstanceColourSourceComponents(comp);
        CHECK(379, v.second.format == capture::kVertexFormat_8_8_8_8 && (v.second.source >> 8) == 3 &&
                       (v.second.source & 0xFF) == (comp[0] | (comp[1] << 2) | (comp[2] << 4)) && v.a0_comp == -1);
      }
    }
    CHECK(380, probe_entries == 7 && instance_entries == 7);  // (four probe entries before the three leaf shaders')
  }

  // --- Real streams (else SKIPPED, loudly) --- FABLE2_NORMALS_BIN: one or more bundles written by
  // out/native-evidence/lighting/task5a/normal_survey.py --export from a discovery capture, separated
  // by ';'. Each is one mesh draw: its position layout, its normal element, its streams (for a
  // skinned draw its bone palette and skin layout, for an instanced draw its instance stream and
  // set) and the triangles of the draw's sampled indices. The decoded normals, skinned and
  // instanced as the geometry cache builds them, must agree with the triangles' face normals:
  // mean |cos| above 0.9 over the bundles (frame-map section 14 has the figures per mesh).
  {
    const char* list = std::getenv("FABLE2_NORMALS_BIN");
    if (!list || !*list) {
      std::printf("SKIPPED: real normal streams (set FABLE2_NORMALS_BIN to bundles of normal_survey.py --export)\n");
    } else {
      std::string all(list);
      double sum = 0.0;
      int count = 0;
      for (size_t at = 0; at <= all.size();) {
        const size_t end = std::min(all.find(';', at), all.size());
        const std::string path = all.substr(at, end - at);
        at = end + 1;
        if (path.empty()) continue;
        const double mean = RealBundle(path);
        if (mean < 0.0) {
          std::fprintf(stderr, "bundle %s: error %.0f\n", path.c_str(), mean);
          CHECK(200, false);
          continue;
        }
        // Every mesh: a coarse, smoothly shaded one reads as low as 0.78 (its triangles' own three
        // normals are far apart); a wrong component order reads 0.1 to 0.77 over a shader's meshes.
        CHECK(201, mean > 0.7);
        sum += mean;
        ++count;
      }
      CHECK(202, count > 0 && sum / count > 0.9);
      if (count) std::printf("real normal streams: %d bundles, mean of their mean |cos| %.4f\n", count, sum / count);
    }
  }

  if (g_fail) return g_fail;
  std::cout << "PASS: normal decode, normal transform, skinned and instanced normals, colour stream, probe weights and sets, bend inputs, "
               "instance colours\n";
  return 0;
}
