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

// The normal elements of the captured draws (frame-map section 14, "Light inputs"): 10_11_11 at
// byte 8 of a 20-byte vertex, signed, normalised, endian 8in32, fetched xyz_ (0xECD6...).
static capture::LightElement Element10(uint32_t size, uint16_t stride = 20, uint16_t offset = 8) {
  capture::LightElement e;
  e.vb = {0x1000, size};
  e.offset_bytes = offset;
  e.stride_bytes = stride;
  e.swizzle = 0xE88;
  e.format = uint8_t(capture::kVertexFormat_10_11_11);
  e.flags = uint8_t(capture::kLightElementSigned | capture::kLightElementNormalized | (2u << capture::kLightElementEndianShift));
  return e;
}
// 16_16_16_16_FLOAT at byte 8 of the 24-byte mesh vertex of an instanced plant, fetched yxwz.
static capture::LightElement ElementHalf(uint32_t size) {
  capture::LightElement e;
  e.vb = {0x2000, size};
  e.offset_bytes = 8;
  e.stride_bytes = 24;
  e.swizzle = 0x4C1;
  e.format = uint8_t(capture::kVertexFormat_16_16_16_16_FLOAT);
  e.flags = uint8_t(capture::kLightElementSigned | (2u << capture::kLightElementEndianShift));
  return e;
}
// The colour stream of 0xECD6...: 8_8_8_8 in a stream of its own, 4 bytes an element, fetched zyxw.
static capture::LightElement ElementColour(uint32_t size) {
  capture::LightElement e;
  e.vb = {0x3000, size};
  e.offset_bytes = 0;
  e.stride_bytes = 4;
  e.swizzle = 0x60A;
  e.format = uint8_t(capture::kVertexFormat_8_8_8_8);
  e.flags = uint8_t(capture::kLightElementNormalized | (2u << capture::kLightElementEndianShift));
  return e;
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
  e.swizzle = uint16_t(u32());
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
    CHECK(1, DecodeNormal(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, 1, 0, 0));
    PutBe32(w, Pack(0, -1023, 0));  // -y
    CHECK(2, DecodeNormal(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, 0, -1, 0));
    PutBe32(w, Pack(0, 0, 511));  // +z: the positive maximum of the 10-bit field
    CHECK(3, DecodeNormal(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, 0, 0, 1));
    // The fields are told apart by their place: x in the low bits, z in the high ones.
    PutBe32(w, 0x000003FFu);
    CHECK(4, DecodeNormal(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, 1, 0, 0));
    PutBe32(w, 0x7FC00000u);
    CHECK(5, DecodeNormal(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, 0, 0, 1));
    PutBe32(w, 0x001FF800u);
    CHECK(6, DecodeNormal(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, 0, 1, 0));
    // A diagonal: (600 / 1023, -400 / 1023, 300 / 511), normalised. The fields have different
    // maxima, so each is divided by its own.
    {
      PutBe32(w, Pack(600, -400, 300));
      const float x = 600.0f / 1023.0f, y = -400.0f / 1023.0f, z = 300.0f / 511.0f;
      const float len = std::sqrt(x * x + y * y + z * z);
      CHECK(7, DecodeNormal(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, x / len, y / len, z / len));
      CHECK(8, Near(n[0] * n[0] + n[1] * n[1] + n[2] * n[2], 1.0f));
    }
    // The most negative code is -1, as the code one above it (the SDK's max(-1, v / maximum)).
    PutBe32(w, Pack(-1024, 0, 0));
    CHECK(9, DecodeNormal(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, -1, 0, 0));
    PutBe32(w, Pack(-1024, 0, -512));
    CHECK(10, DecodeNormal(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, -std::sqrt(0.5f), 0, -std::sqrt(0.5f)));
    PutBe32(w, Pack(-1024, 0, -511));  // z = -1 exactly here too: both -1
    CHECK(11, DecodeNormal(capture::kVertexFormat_10_11_11, w, 4, n) && Near3(n, -std::sqrt(0.5f), 0, -std::sqrt(0.5f)));
    // Nothing from a zero vector, short data, no data, another format.
    PutBe32(w, 0);
    n[0] = n[1] = n[2] = 7.0f;
    CHECK(12, !DecodeNormal(capture::kVertexFormat_10_11_11, w, 4, n));
    PutBe32(w, Pack(1023, 0, 0));
    CHECK(13, !DecodeNormal(capture::kVertexFormat_10_11_11, w, 3, n));
    CHECK(14, !DecodeNormal(capture::kVertexFormat_10_11_11, nullptr, 4, n));
    CHECK(15, !DecodeNormal(0, w, 4, n));
    CHECK(16, !DecodeNormal(17, w, 4, n));  // k_11_11_10: another split of the bits
    CHECK(17, !DecodeNormal(capture::kVertexFormat_8_8_8_8, w, 4, n) && !DecodeNormal(capture::kVertexFormat_32_32_32_32_FLOAT, w, 4, n));
    // The bytes are the guest's: big-endian. The same bytes read the other way round are another normal.
    const uint8_t be[4] = {0x00, 0x00, 0x03, 0xFF};
    CHECK(18, DecodeNormal(capture::kVertexFormat_10_11_11, be, 4, n) && Near3(n, 1, 0, 0));
  }

  // --- DecodeNormal: the half-float element of the instanced meshes (memory order x, y, z, unused) ---
  {
    uint8_t h[8];
    float n[3];
    PutHalf4(h, 0.0f, 0.0f, 2.0f, 9.0f);  // not unit length: normalised; the fourth half is not the normal's
    CHECK(20, DecodeNormal(capture::kVertexFormat_16_16_16_16_FLOAT, h, 8, n) && Near3(n, 0, 0, 1));
    PutHalf4(h, 3.0f, -4.0f, 0.0f, 0.0f);
    CHECK(21, DecodeNormal(capture::kVertexFormat_16_16_16_16_FLOAT, h, 8, n) && Near3(n, 0.6f, -0.8f, 0));
    CHECK(22, !DecodeNormal(capture::kVertexFormat_16_16_16_16_FLOAT, h, 7, n));
    PutHalf4(h, 0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(23, !DecodeNormal(capture::kVertexFormat_16_16_16_16_FLOAT, h, 8, n));
    PutBe16(h, 0x7C00);  // infinity: no direction
    CHECK(24, !DecodeNormal(capture::kVertexFormat_16_16_16_16_FLOAT, h, 8, n));
    PutHalf4(h, 1.0f, 0.0f, 0.0f, 0.0f);
    PutBe16(h + 2, 0x7E00);  // a NaN
    CHECK(25, !DecodeNormal(capture::kVertexFormat_16_16_16_16_FLOAT, h, 8, n));
  }

  // --- Which source components are a normal: one place, by the element's format ---
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
    CHECK(29, DecodeNormal(capture::kVertexFormat_16_16_16_16_FLOAT, h, 8, n) && Near3(n, 1 / len, 2 / len, 4 / len));
  }

  // --- Which elements are read as a normal ---
  {
    const capture::LightElement e = Element10(200);
    CHECK(30, NormalElementSupported(e) && NormalElementCount(e) == 10);
    capture::LightElement a = e;
    a.swizzle = 0x447;  // _xyz: the two skinned shaders
    CHECK(31, NormalElementSupported(a));
    a = e;
    a.swizzle = 0xE80;  // xxz_: y is not fetched
    CHECK(32, !NormalElementSupported(a));
    a = e;
    a.swizzle = 0xFC8;  // xy__: z is not fetched
    CHECK(33, !NormalElementSupported(a));
    a = e;
    a.swizzle = 0xE8C;  // 0yz_: a constant where x would be is not x
    CHECK(51, !NormalElementSupported(a));
    a = e;
    a.swizzle = 0xE45;  // 1xy_: z is not fetched, whatever constant the register gets
    CHECK(52, !NormalElementSupported(a));
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
    a = h;
    a.swizzle = 0x611;  // yzxw: 0x48D3... and 0xFC4F... fetch the same element this way
    CHECK(46, NormalElementSupported(a));
    a = h;
    a.swizzle = 0x688;  // xyzw: every component fetched
    CHECK(47, NormalElementSupported(a));
    a = h;
    a.swizzle = 0xE41;  // yxy_: w, the third half in memory, is not fetched
    CHECK(48, !NormalElementSupported(a));
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
    a.swizzle = 0xFC8;
    CHECK(71, !DecodeNormals(s.data(), s.size(), a, 0, 5, out));
    // The destination swizzle does not change the components: _xyz is the same normal.
    a = e;
    a.swizzle = 0x447;
    CHECK(72, DecodeNormals(s.data(), s.size(), a, 0, 3, out) && Near3(out[0], 1, 0, 0) && Near3(out[2], 0, 0, -1));

    // Half floats: memory order, whatever the fetch's swizzle (0x4C1 and 0x611 are the two seen).
    capture::LightElement h = ElementHalf(48);
    std::vector<uint8_t> m(48, 0);
    PutHalf4(&m[8], 0.0f, 3.0f, 4.0f, 0.5f);
    PutHalf4(&m[32], -1.0f, 0.0f, 0.0f, 0.25f);
    CHECK(73, DecodeNormals(m.data(), m.size(), h, 0, 2, out) && Near3(out[0], 0, 0.6f, 0.8f) && Near3(out[1], -1, 0, 0));
    h.swizzle = 0x611;
    CHECK(74, DecodeNormals(m.data(), m.size(), h, 0, 2, out) && Near3(out[0], 0, 0.6f, 0.8f) && Near3(out[1], -1, 0, 0));
    CHECK(75, !DecodeNormals(m.data(), 39, h, 0, 2, out));
  }

  // --- RotateNormal ---
  {
    float out[3];
    // A rotation by 90 degrees about z: x goes to y.
    const float rz[3][3] = {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}};
    const float x[3] = {1, 0, 0}, z[3] = {0, 0, 1};
    RotateNormal(rz, x, out);
    CHECK(80, Near3(out, 0, 1, 0));
    RotateNormal(rz, z, out);
    CHECK(81, Near3(out, 0, 0, 1));
    // A rotation times one scale: the direction is the rotation's, the length 1.
    const float rs[3][3] = {{0, -3, 0}, {3, 0, 0}, {0, 0, 3}};
    const float d[3] = {0.6f, 0.0f, 0.8f};
    RotateNormal(rs, d, out);
    CHECK(82, Near3(out, 0, 0.6f, 0.8f));
    // A scale that is not uniform: the plane x + z = 0 has the normal (1, 0, 1) and the tangents
    // (1, 0, -1) and (0, 1, 0). Stretched four times along x, the tangents are (4, 0, -1) and
    // (0, 1, 0); the normal has to stay perpendicular to both, which the matrix itself does not do.
    const float sx[3][3] = {{4, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const float n[3] = {std::sqrt(0.5f), 0.0f, std::sqrt(0.5f)};
    const float t1[3] = {4, 0, -1}, t2[3] = {0, 1, 0};
    RotateNormal(sx, n, out);
    CHECK(83, Near(Dot(out, t1), 0.0f) && Near(Dot(out, t2), 0.0f) && Near(Dot(out, out), 1.0f));
    CHECK(84, Near3(out, 1.0f / std::sqrt(17.0f), 0.0f, 4.0f / std::sqrt(17.0f)));
    {
      const float plain[3] = {4 * n[0], n[1], n[2]};  // (the matrix applied as it is: not perpendicular)
      CHECK(85, std::fabs(Dot(plain, t1)) > 1.0f);
    }
    // The same under a rotation after the scale: perpendicular to the transformed tangents.
    const float rsx[3][3] = {{0, -1, 0}, {4, 0, 0}, {0, 0, 1}};  // rz * sx
    const float rt1[3] = {0, 4, -1}, rt2[3] = {-1, 0, 0};
    RotateNormal(rsx, n, out);
    CHECK(86, Near(Dot(out, rt1), 0.0f) && Near(Dot(out, rt2), 0.0f) && Near(Dot(out, out), 1.0f) && out[2] > 0.0f);
    // A mirror in x: the normal goes with the surface (x turns, the rest stays) and does not point
    // into it. The cofactor matrix alone would give the opposite direction.
    const float mx[3][3] = {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    RotateNormal(mx, d, out);
    CHECK(87, Near3(out, -0.6f, 0, 0.8f));
    RotateNormal(mx, z, out);
    CHECK(88, Near3(out, 0, 0, 1));
    // A mirror with a scale that is not uniform.
    const float mxs[3][3] = {{-4, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const float mt1[3] = {-4, 0, -1};
    RotateNormal(mxs, n, out);
    CHECK(89, Near(Dot(out, mt1), 0.0f) && out[0] < 0.0f && out[2] > 0.0f && Near(Dot(out, out), 1.0f));
    // A singular matrix has no inverse: the matrix itself is applied (a flattened mesh keeps the
    // normals of what is left of it).
    const float flat[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 0}};
    RotateNormal(flat, d, out);
    CHECK(90, Near3(out, 1, 0, 0));
    // A result without a direction is (0, 0, 1): the zero matrix, a zero normal, values that are not finite.
    const float none[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    RotateNormal(none, d, out);
    CHECK(91, out[0] == 0.0f && out[1] == 0.0f && out[2] == 1.0f);
    RotateNormal(flat, z, out);
    CHECK(92, out[0] == 0.0f && out[1] == 0.0f && out[2] == 1.0f);
    const float zero[3] = {0, 0, 0};
    RotateNormal(rz, zero, out);
    CHECK(93, out[0] == 0.0f && out[1] == 0.0f && out[2] == 1.0f);
    for (float bad : {nan, inf, -inf}) {
      float m[3][3] = {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}};
      m[1][1] = bad;
      RotateNormal(m, d, out);
      CHECK(94, out[0] == 0.0f && out[1] == 0.0f && out[2] == 1.0f);
      const float bn[3] = {bad, 0.0f, 1.0f};
      RotateNormal(rz, bn, out);
      CHECK(95, out[0] == 0.0f && out[1] == 0.0f && out[2] == 1.0f);
    }
    // Small and large matrices keep their direction (the determinant of a scale of 1/1000 is 1e-9).
    const float tiny[3][3] = {{0, -1e-3f, 0}, {1e-3f, 0, 0}, {0, 0, 1e-3f}};
    RotateNormal(tiny, d, out);
    CHECK(96, Near3(out, 0, 0.6f, 0.8f));
    const float big[3][3] = {{0, -1e6f, 0}, {1e6f, 0, 0}, {0, 0, 1e6f}};
    RotateNormal(big, d, out);
    CHECK(97, Near3(out, 0, 0.6f, 0.8f));
    // Below the determinant's bound the matrix itself is used: for a rotation times one scale the same direction.
    const float tinier[3][3] = {{0, -1e-5f, 0}, {1e-5f, 0, 0}, {0, 0, 1e-5f}};
    RotateNormal(tinier, d, out);
    CHECK(98, Near3(out, 0, 0.6f, 0.8f));
    // The bound is 1e-12: a small stretch above it (determinant 4e-9) is still inverted, one below
    // it (4e-15) is applied as it is, which for a stretch is the other direction.
    const float small_stretch[3][3] = {{4e-3f, 0, 0}, {0, 1e-3f, 0}, {0, 0, 1e-3f}};
    RotateNormal(small_stretch, n, out);
    CHECK(136, Near3(out, 1.0f / std::sqrt(17.0f), 0.0f, 4.0f / std::sqrt(17.0f), 1e-5f));
    const float smaller_stretch[3][3] = {{4e-5f, 0, 0}, {0, 1e-5f, 0}, {0, 0, 1e-5f}};
    RotateNormal(smaller_stretch, n, out);
    CHECK(137, Near3(out, 4.0f / std::sqrt(17.0f), 0.0f, 1.0f / std::sqrt(17.0f), 1e-5f));
    // A matrix with nothing zero in it (every cofactor counts; its determinant is 17.2): the normal
    // of a plane stays perpendicular to both of the plane's transformed tangents and on the side
    // the transformed normal points to.
    {
      const float g[3][3] = {{0.5f, 3.0f, 1.0f}, {2.0f, 0.25f, 5.0f}, {1.5f, 1.0f, 0.75f}};
      const float pn[3] = {2.0f / 3.0f, -1.0f / 3.0f, 2.0f / 3.0f};
      const float ta[3] = {1.0f, 2.0f, 0.0f}, tb[3] = {0.0f, 2.0f, 1.0f};  // both perpendicular to pn
      float ma[3], mb[3], mn[3];
      for (int r = 0; r < 3; ++r) {
        ma[r] = Dot(g[r], ta);
        mb[r] = Dot(g[r], tb);
        mn[r] = Dot(g[r], pn);
      }
      RotateNormal(g, pn, out);
      CHECK(138, std::fabs(Dot(out, ma)) < 1e-5f && std::fabs(Dot(out, mb)) < 1e-5f && Near(Dot(out, out), 1.0f, 1e-5f));
      // (the determinant of g is positive: the normal keeps the side of M n)
      CHECK(139, Dot(out, mn) > 0.0f);
      // Its mirror image: perpendicular still, and on the mirrored side.
      float gm[3][3];
      for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) gm[r][k] = r == 1 ? -g[r][k] : g[r][k];
      }
      float mma[3], mmb[3], mmn[3];
      for (int r = 0; r < 3; ++r) {
        mma[r] = Dot(gm[r], ta);
        mmb[r] = Dot(gm[r], tb);
        mmn[r] = Dot(gm[r], pn);
      }
      RotateNormal(gm, pn, out);
      CHECK(155, std::fabs(Dot(out, mma)) < 1e-5f && std::fabs(Dot(out, mmb)) < 1e-5f && Dot(out, mmn) > 0.0f);
    }

    // The two steps of it: NormalMatrix, then TransformNormal, which says when there is no direction.
    float nm[3][3];
    NormalMatrix(sx, nm);
    CHECK(99, Near(nm[0][0], 1.0f) && Near(nm[1][1], 4.0f) && Near(nm[2][2], 4.0f) && Near(nm[0][1], 0.0f));
    CHECK(100, TransformNormal(nm, n, out) && Near3(out, 1.0f / std::sqrt(17.0f), 0.0f, 4.0f / std::sqrt(17.0f)));
    out[0] = 5.0f;
    CHECK(101, !TransformNormal(nm, zero, out) && out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f);
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
    CHECK(142, Near3(out[2], 1, 0, 0) && Near3(out[3], 1 / std::sqrt(17.0f), 0, 4 / std::sqrt(17.0f)));  // copy 1
    CHECK(143, Near3(out[4], -1, 0, 0) && Near3(out[5], -h, 0, h));                     // copy 2
    CHECK(144, IsZero(out[6]) && IsZero(out[7]));                                        // copy 3: no direction
    CHECK(145, IsZero(out[8]) && IsZero(out[9]));                                        // copy 4: past the stream
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
    // Which source components are red, green, blue, alpha is said in one place, by what the element
    // is: z, y, x, w. The fetch's destination swizzle does not order the bytes: fetched xyzw, the
    // same colour.
    {
      uint8_t comp[4] = {9, 9, 9, 9};
      ColourSourceComponents(comp);
      CHECK(176, comp[0] == 2 && comp[1] == 1 && comp[2] == 0 && comp[3] == 3);
    }
    a = e;
    a.swizzle = 0x688;
    CHECK(177, ColourElementSupported(a) && DecodeColours(s, sizeof(s), a, 0, 1, out) && out[0] == 0xFF302010u);
    // Not a colour a reader decodes: a component the fetch does not deliver (a constant in its
    // place, or left as it was), another format, signed, integers.
    a = e;
    a.swizzle = 0xA0A;  // zyx1
    CHECK(178, !ColourElementSupported(a) && !DecodeColours(s, sizeof(s), a, 4, 1, out));
    a = e;
    a.swizzle = 0xE0A;  // zyx_
    CHECK(179, !ColourElementSupported(a) && !DecodeColours(s, sizeof(s), a, 0, 1, out) && ColourCount(a, 0, 100) == 0);
    a = e;
    a.format = uint8_t(capture::kVertexFormat_10_11_11);
    CHECK(180, !ColourElementSupported(a) && !DecodeColours(s, sizeof(s), a, 0, 1, out));
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
  std::cout << "PASS: normal decode, normal transform, skinned and instanced normals, colour stream\n";
  return 0;
}
