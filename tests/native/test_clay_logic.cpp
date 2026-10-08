// Synthetic standalone test for the clay pass's pure helpers; no game or GPU.
#include "../../src/native/render/clay_logic.h"
#include "../../src/native/fable2_native_shaders.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <cmath>
#include <limits>
#include <string>

using namespace fable2::native;
using namespace fable2::native::render;

static int g_fail = 0;
#define CHECK(id, cond)                                         \
  do {                                                          \
    if (!(cond)) {                                              \
      std::cerr << "FAIL case " << (id) << ": " #cond "\n";     \
      g_fail = (id);                                            \
    }                                                           \
  } while (0)

static capture::DrawRecord BaseRecord() {
  capture::DrawRecord r;
  r.seq = 7;
  r.prim = capture::kPrimTriangleList;
  r.indexed = true;
  r.index32 = false;
  r.start = 6;
  r.count = 12;
  r.base_vertex = 0;
  r.ib = {0x1000, 64};
  r.vb = {0x2000, 1600};
  r.pos.format = capture::PosFormat::kFloat3;
  r.pos.stride_bytes = 16;
  r.pos.offset_bytes = 0;
  r.vs_hash = 0x1234567890ABCDEFull;
  for (int i = 0; i < 16; ++i) r.rows[i] = float(i);
  return r;
}

// A decoded render state from real register words (frame-map section 13), under
// the reversed depth range every sampled draw has.
static capture::RenderState State(uint32_t depth, uint32_t blend, uint32_t alpha, uint32_t cull,
                                  uint32_t mask = 0xF, float ref = 0.0f) {
  capture::StateWords w;
  w.depth_control = depth;
  w.blend_control = blend;
  w.color_control = alpha;
  w.su_sc_mode = cull;
  w.color_mask = mask;
  w.alpha_ref = FloatBits(ref);
  w.vport_z_scale = 0xBF800000u;
  w.vte_cntl = 0x0000043Fu;
  w.read = true;
  return capture::DecodeRenderState(w);
}

// What a float colour channel with `mantissa_bits` stored bits keeps of a value in [0, 1]: 10 bits for a
// channel of R16G16B16A16_FLOAT, 6 for red and green and 5 for blue of R11G11B10_FLOAT (every one with
// five exponent bits, least normal 2^-14, denormals below). `nearest`: rounded to the nearest stored
// value, else toward zero (a GPU may do either when it writes a target).
static float StoredFloat(float v, int mantissa_bits, bool nearest) {
  if (!(v > 0.0f)) return 0.0f;
  int e = int(std::floor(std::log2(double(v))));
  if (e < -14) e = -14;
  const double step = std::ldexp(1.0, e - mantissa_bits);
  const double q = double(v) / step;
  return float((nearest ? std::floor(q + 0.5) : std::floor(q)) * step);
}

// The 8-bit level a UNORM target stores for `v` (rounded to nearest).
static int Level8(float v) {
  const float c = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
  return int(std::floor(c * 255.0f + 0.5f));
}

int main() {
  // 1-4: clay color cvar parsing.
  {
    bool ok = false;
    CHECK(1, ParseClayColor("clay", &ok) == ClayColor::kClay && ok);
    CHECK(2, ParseClayColor(" Draw ", &ok) == ClayColor::kDraw && ok);
    CHECK(3, ParseClayColor("SHADER", &ok) == ClayColor::kShader && ok);
    CHECK(4, ParseClayColor("rainbow", &ok) == ClayColor::kClay && !ok);
  }
  // 5-9: colors.
  {
    capture::DrawRecord r = BaseRecord();
    CHECK(5, ClayColorValue(ClayColor::kClay, r) == 0xB8B0A0u);
    const uint32_t d = ClayColorValue(ClayColor::kDraw, r);
    CHECK(6, (d & 0x404040u) == 0x404040u && (d >> 24) == 0);
    CHECK(7, d == (0x404040u | (Hash32(uint64_t(r.seq)) & 0xBFBFBFu)));
    const uint32_t s = ClayColorValue(ClayColor::kShader, r);
    CHECK(8, s == (0x404040u | (Hash32(r.vs_hash) & 0xBFBFBFu)));
    capture::DrawRecord r2 = r;
    r2.seq = 8;
    CHECK(9, ClayColorValue(ClayColor::kDraw, r2) != d &&
                 ClayColorValue(ClayColor::kShader, r2) == s);
  }
  // 10-13: vertex count from the stream size and position layout.
  {
    capture::PosLayout l;
    l.format = capture::PosFormat::kFloat3;
    l.stride_bytes = 16;
    l.offset_bytes = 0;
    CHECK(10, PositionCount(1600, l) == 100);
    CHECK(11, PositionCount(1611, l) == 100 && PositionCount(1612, l) == 101);  // 12-byte tail fits
    l.offset_bytes = 8;                        // last vertex at 1584+8 needs 12 -> 1604 > 1600
    CHECK(12, PositionCount(1600, l) == 99);
    l.stride_bytes = 0;
    CHECK(13, PositionCount(1600, l) == 0);
    l.stride_bytes = 16;
    l.format = capture::PosFormat::kUnknown;
    CHECK(14, PositionCount(1600, l) == 0);
    l.format = capture::PosFormat::kFloat4;
    l.offset_bytes = 0;
    CHECK(15, PositionCount(8, l) == 0);
  }
  // 16-20: position keys: anything that changes decoding changes the key.
  {
    capture::DrawRecord r = BaseRecord();
    const GeoKey k = PositionKey(r, nullptr);
    CHECK(16, k.addr == 0x2000 && k.size == 1600 && k.stride == 16 && k.kind == 0);
    capture::DrawRecord a = r;
    a.pos.offset_bytes = 4;
    CHECK(17, !(PositionKey(a, nullptr) == k));
    a = r;
    a.pos.swizzle = 0x11;
    CHECK(18, !(PositionKey(a, nullptr) == k));
    a = r;
    a.pos.swap16 = true;
    CHECK(19, !(PositionKey(a, nullptr) == k));
    a = r;
    a.pos.exp_adjust = -3;
    CHECK(20, !(PositionKey(a, nullptr) == k));
    a = r;
    a.pos.format = capture::PosFormat::kFloat4;
    CHECK(21, !(PositionKey(a, nullptr) == k));
    a = r;
    a.pos.is_signed = false;
    a.pos.format = capture::PosFormat::kShort4;
    capture::DrawRecord b = a;
    b.pos.is_signed = true;
    CHECK(22, !(PositionKey(a, nullptr) == PositionKey(b, nullptr)));
    a = r;
    a.seq = 99;
    a.base_vertex = 5;  // irrelevant to the decoded stream
    CHECK(23, PositionKey(a, nullptr) == k);
  }
  // 24-29: index keys and the referenced index byte range.
  {
    capture::DrawRecord r = BaseRecord();
    const GeoKey k = IndexKey(r, nullptr);
    CHECK(24, k.addr == 0x1000 && k.size == 64 && k.stride == 0 && k.kind == 1);
    capture::DrawRecord a = r;
    a.start = 0;
    CHECK(25, !(IndexKey(a, nullptr) == k));
    a = r;
    a.index32 = true;
    CHECK(26, !(IndexKey(a, nullptr) == k) && IndexKey(a, nullptr).stride == 1);
    a = r;
    a.prim = capture::kPrimTriangleStrip;
    CHECK(27, !(IndexKey(a, nullptr) == k));
    a = r;
    a.indexed = false;
    const GeoKey n = IndexKey(a, nullptr);
    CHECK(28, n.addr == 0 && n.size == 0 && n.kind == 1);
    IndexBytes ib;
    CHECK(29, ReferencedIndexBytes(r, &ib) && ib.offset == 12 && ib.size == 24);
    a = r;
    a.index32 = true;  // (6 + 12) * 4 = 72 > 64
    CHECK(30, !ReferencedIndexBytes(a, &ib));
    a = r;
    a.start = 0xFFFFFFF0u;  // overflow must not wrap
    CHECK(31, !ReferencedIndexBytes(a, &ib));
  }
  // 32-37: index range validation (the renderer's kBadIndex rule).
  {
    CHECK(32, IndexRangeValid(0, 99, 0, 100));
    CHECK(33, !IndexRangeValid(0, 100, 0, 100));
    CHECK(34, IndexRangeValid(0, 49, 50, 100));
    CHECK(35, !IndexRangeValid(0, 50, 50, 100));
    CHECK(36, !IndexRangeValid(0, 0, -1, 100));
    // The low end: the shader loads every vertex of the list, so the lowest must not lie before the
    // buffer's start. A negative base vertex is fine when every index is at least its size.
    CHECK(37, IndexRangeValid(10, 10, -10, 100) && IndexRangeValid(10, 109, -10, 100));
    CHECK(37, !IndexRangeValid(9, 10, -10, 100) && !IndexRangeValid(0, 10, -10, 100) && !IndexRangeValid(0, 109, -1, 100));
    CHECK(37, !IndexRangeValid(10, 110, -10, 100));
    CHECK(38, !IndexRangeValid(0, 0xFFFFFFFFu, 0x7FFFFFFF, 0xFFFFFFFFu));
    CHECK(38, !IndexRangeValid(0, 5, INT32_MIN, 100) && IndexRangeValid(0x80000000u, 0x80000005u, INT32_MIN, 100));
    CHECK(39, !IndexRangeValid(0, 0, 0, 0));
    // A list whose smallest index is above its largest is no list.
    CHECK(39, !IndexRangeValid(5, 4, 0, 100) && IndexRangeValid(5, 5, 0, 100));
  }
  // The frame without its constant buffer: every drawable record is left out, none otherwise.
  {
    CHECK(40, ClayRecordsWithoutConstants(921, true) == 0 && ClayRecordsWithoutConstants(921, false) == 921);
    CHECK(40, ClayRecordsWithoutConstants(0, false) == 0 && ClayRecordsWithoutConstants(0, true) == 0);
    CHECK(40, ClayRecordsWithoutConstants(1, false) == 1);
  }
  // 40-43, 80-82, 130-133: root constants match the HLSL cbuffer (36 dwords).
  {
    static_assert(sizeof(ClayConstants) == 144);
    {
      const float inf = std::numeric_limits<float>::infinity();
      const float one[4] = {1.0f, 1.0f, 0.0f, 0.0f};
      capture::DrawRecord plain = BaseRecord();
      const ClayConstants p = MakeClayConstants(plain, 10, 0, one, false, 0);
      // 130: a record without a cut never cuts: the squared distance is +inf.
      CHECK(130, plain.cut[3] == inf && p.cut[3] == inf && !(1e30f > p.cut[3]));
      capture::DrawRecord cut = BaseRecord();
      cut.cut[0] = 82.0f; cut.cut[1] = 157.25f; cut.cut[2] = 50.0f; cut.cut[3] = 1062.5f;
      const ClayConstants c = MakeClayConstants(cut, 10, 0, one, false, 0);
      uint32_t dw[32];
      std::memcpy(dw, &c, sizeof(dw));
      float f[4];
      std::memcpy(f, &dw[28], sizeof(f));
      // 131: the cut is the eighth float4 of the cbuffer.
      CHECK(131, f[0] == 82.0f && f[1] == 157.25f && f[2] == 50.0f && f[3] == 1062.5f);
      // 132, 133: the camera moving must not rebuild anything: no cache key
      // (and so no content hash, which follows the keyed streams) takes it.
      capture::DrawRecord a = BaseRecord();
      a.material.uv_vb = a.vb;
      a.material.uv.format = capture::UvFormat::kHalf2;
      a.material.uv.stride_bytes = 16;
      a.instances.active = true;
      a.instances.rows_addr = 0x7000;
      a.instances.rows_size = 28 * 40;
      a.instances.flat_count = 64;
      capture::DrawRecord b = a;
      b.cut[0] = 82.0f; b.cut[1] = 157.25f; b.cut[2] = 50.0f; b.cut[3] = 1062.5f;
      CHECK(132, PositionKey(a, nullptr) == PositionKey(b, nullptr) && UvKey(a) == UvKey(b) && IndexKey(a, nullptr) == IndexKey(b, nullptr));
      CHECK(133, MeshPositionKey(a) == MeshPositionKey(b) && MeshUvKey(a) == MeshUvKey(b));
    }
    capture::DrawRecord r = BaseRecord();
    r.layout = capture::TransformLayout::kCombine;
    r.base_vertex = -3;
    const float uv[4] = {2.0f, -1.0f, 0.25f, 0.5f};
    const ClayConstants c = MakeClayConstants(r, 100, 0xB8B0A0u, uv, true, 3);
    uint32_t dw[28];
    std::memcpy(dw, &c, sizeof(dw));
    float f5;
    std::memcpy(&f5, &dw[5], 4);
    CHECK(40, f5 == 5.0f);
    CHECK(41, dw[16] == 1 && int32_t(dw[17]) == -3);
    CHECK(42, dw[18] == 100 && dw[19] == 0xB8B0A0u);
    float fuv[4];
    std::memcpy(fuv, &dw[20], sizeof(fuv));
    CHECK(80, fuv[0] == 2.0f && fuv[1] == -1.0f && fuv[2] == 0.25f && fuv[3] == 0.5f);
    CHECK(81, dw[24] == 1 && dw[25] == 3 && dw[26] == 0 && dw[27] == 0);
    r.layout = capture::TransformLayout::kDot;
    const ClayConstants flat = MakeClayConstants(r, 1, 0, uv, false, 2);
    CHECK(43, flat.layout == 0);
    CHECK(82, flat.textured == 0 && flat.sampler == 2);
  }
  // 83-86: UV stream keys.
  {
    capture::DrawRecord r = BaseRecord();
    r.material.uv_vb = {0x5000, 3200};
    r.material.uv.format = capture::UvFormat::kHalf2;
    r.material.uv.offset_bytes = 12;
    r.material.uv.stride_bytes = 32;
    r.material.uv.comp_u = 0;
    r.material.uv.comp_v = 1;
    const GeoKey k = UvKey(r);
    CHECK(83, k.addr == 0x5000 && k.size == 3200 && k.stride == 32);
    capture::DrawRecord same = r;
    CHECK(84, UvKey(same) == k);
    capture::DrawRecord a = r;
    a.material.uv.comp_u = 1;
    CHECK(85, !(UvKey(a) == k));
    a = r;
    a.material.uv.swap16 = true;
    CHECK(86, !(UvKey(a) == k));
    // 127-129: v taken from another element of the vertex changes the key,
    // and the count covers the later of the two elements.
    a = r;
    a.material.uv.v_element_delta = 8;
    CHECK(127, !(UvKey(a) == k));
    CHECK(128, UvCount(3200, r.material.uv) == 100 && UvCount(3200 - 16, r.material.uv) == 100 &&
                   UvCount(3200 - 17, r.material.uv) == 99);
    CHECK(129, UvCount(3200, a.material.uv) == 100 && UvCount(3200 - 8, a.material.uv) == 100 &&
                   UvCount(3200 - 9, a.material.uv) == 99 && UvCount(23, a.material.uv) == 0);
    // Never the key of positions or indices of the same stream, nor of a
    // terrain grid's indices.
    capture::DrawRecord s = r;
    s.material.uv_vb = s.vb;
    s.material.uv.stride_bytes = s.pos.stride_bytes;
    const capture::DrawRecord t = BaseRecord();
    capture::TerrainPatch tp;  // a terrain record's inputs (the scene's side array: TerrainOf)
    tp.active = true;
    tp.patches = 1;
    CHECK(87, !(UvKey(s) == PositionKey(s, nullptr)) && !(UvKey(s) == IndexKey(s, nullptr)) &&
                  UvKey(s).kind != IndexKey(t, &tp).kind && UvKey(s).kind != PositionKey(t, &tp).kind);
  }
  // 88-90: F3 texture line.
  {
    TextureStats ts;
    ts.textured = 80;
    ts.status[size_t(capture::MaterialStatus::kTextured)] = 80;
    ts.status[size_t(capture::MaterialStatus::kPsUnknown)] = 15;
    ts.status[size_t(capture::MaterialStatus::kTexturePending)] = 5;
    ts.resident = 42;
    ts.resident_bytes = 12ull * 1024 * 1024;
    ts.uploads = 3;
    ts.upload_bytes = 2ull * 1024 * 1024;
    ts.decode_ms = 1.234;
    const std::string t = FormatTextureText(ts, 100);
    CHECK(88, t.find("textured 80 of 100 drawn (80%, 80% non-terrain)") != std::string::npos);
    CHECK(89, t.find("top untextured: ps-unknown 15, texture-pending 5") != std::string::npos);
    const std::string want =
        "Textures: textured 80 of 100 drawn (80%, 80% non-terrain), resident 42 (12.0 MB), uploads 3 (2.0 MB), "
        "decode 1.23 ms | top untextured: ps-unknown 15, texture-pending 5";
    CHECK(90, t == want);
    if (t != want) std::cerr << "got:  " << t << "\nwant: " << want << "\n";
    TextureStats none;
    CHECK(91, FormatTextureText(none, 0).find("textured 0 of 0 drawn (0%, 0% non-terrain)") !=
                  std::string::npos);
    // Top three only, by count.
    TextureStats many;
    many.status[size_t(capture::MaterialStatus::kTerrain)] = 4;
    many.status[size_t(capture::MaterialStatus::kNoAlbedo)] = 9;
    many.status[size_t(capture::MaterialStatus::kUvUnsupported)] = 2;
    many.status[size_t(capture::MaterialStatus::kTextureBad)] = 1;
    CHECK(92, FormatTextureText(many, 16).find(
                  "top untextured: no-albedo 9, terrain 4, uv-unsupported 2") != std::string::npos &&
                  FormatTextureText(many, 16).find("texture-bad") == std::string::npos);
    // Success criterion 2 counts non-terrain draws: the bridge scene draws
    // 567 terrain patches, so the overall share alone reads about 13%.
    TextureStats bridge;
    bridge.textured = 87;
    bridge.status[size_t(capture::MaterialStatus::kTextured)] = 87;
    bridge.status[size_t(capture::MaterialStatus::kTerrain)] = 567;
    bridge.terrain = 567;
    bridge.status[size_t(capture::MaterialStatus::kNoAlbedo)] = 13;
    CHECK(94, FormatTextureText(bridge, 667).find("textured 87 of 667 drawn (13%, 87% non-terrain)") !=
                  std::string::npos);
    // Only terrain drawn: no division by zero.
    TextureStats terrain_only;
    terrain_only.status[size_t(capture::MaterialStatus::kTerrain)] = 5;
    terrain_only.terrain = 5;
    CHECK(95, FormatTextureText(terrain_only, 5).find("(0%, 0% non-terrain)") != std::string::npos);
    // A latched texture path (RHI failure) is visible on F3.
    TextureStats latched;
    latched.latched = true;
    latched.textured = 3;
    CHECK(96, FormatTextureText(latched, 10) == "Textures: off (latched)");
    FrameScene ls;
    ClayStats lst;
    const std::string lt = FormatStatusText(ls, lst, &latched);
    CHECK(97, lt.size() > 24 && lt.compare(lt.size() - 24, 24, "\nTextures: off (latched)") == 0);
  }
  // 44-48: F3 status text.
  {
    FrameScene s;
    s.captured = 1477;
    s.skipped[size_t(capture::SkipReason::kUnsupportedPrim)] = 324;
    s.skipped[size_t(capture::SkipReason::kNoTransform)] = 229;
    s.skipped[size_t(capture::SkipReason::kBadIndex)] = 3;
    s.skipped[size_t(capture::SkipReason::kNoStream)] = 1;
    ClayStats st;
    st.drawn = 915;
    st.deformed = 5;
    st.skipped_bad_index = 2;
    st.skipped_other = 3;
    st.uploads = 12;
    st.hits = 1818;
    st.resident_bytes = 45ull * 1024 * 1024 + 512 * 1024;
    st.hash_ms = 0.84;
    st.decode_ms = 0.126;
    st.record_ms = 0.5;
    s.capture_ms = 0.4251;
    TextureStats ts;
    ts.textured = 900;
    ts.status[size_t(capture::MaterialStatus::kTextured)] = 900;
    ts.status[size_t(capture::MaterialStatus::kTerrain)] = 15;
    ts.terrain = 15;
    const std::string t = FormatStatusText(s, st, &ts);
    const std::string want =
        "Native: captured 1477, drawn 915 (deformed 5), skipped 562 "
        "(top: unsupported-prim 324, no-transform 229, bad-index 5)\n"
        "Geometry: 12 uploads, 1818 hits, 45.5 MB resident, instanced 0, skinned 0 | hash 0.84 ms, "
        "decode 0.13 ms, record 0.50 ms\n"
        "State: 0 pipelines, alpha test 0, blended 0, invalid 0\n"
        "Capture: 0.43 ms guest time per frame\n"
        "Textures: textured 900 of 915 drawn (98%, 100% non-terrain), resident 0 (0.0 MB), uploads 0 (0.0 MB), "
        "decode 0.00 ms | top untextured: terrain 15";
    CHECK(44, t == want);
    if (t != want) std::cerr << "got:  " << t << "\nwant: " << want << "\n";
    FrameScene empty;
    ClayStats none;
    const std::string e = FormatStatusText(empty, none, nullptr);
    CHECK(45, e.rfind("Native: captured 0, drawn 0 (deformed 0), skipped 0\n", 0) == 0);
    // Texture path off (fable2_native_textures=false or latched).
    CHECK(93, e.size() > 14 && e.compare(e.size() - 14, 14, "\nTextures: off") == 0);
    // Renderer-side "other" skips appear as their own reason.
    FrameScene o;
    o.captured = 10;
    ClayStats so;
    so.drawn = 6;
    so.skipped_other = 4;
    const std::string ot = FormatStatusText(o, so, nullptr);
    CHECK(46, ot.find("skipped 4 (top: render-other 4)") != std::string::npos);
    // Draws built by the instanced and skinned position paths.
    ClayStats paths;
    paths.instanced = 3;
    paths.skinned = 2;
    const std::string pt = FormatStatusText(empty, paths, nullptr);
    CHECK(98, pt.find("MB resident, instanced 3, skinned 2 | hash") != std::string::npos);
    // The new skip reasons are counted and named like the others.
    FrameScene n;
    n.captured = 9;
    n.skipped[size_t(capture::SkipReason::kInstanceUnsupported)] = 5;
    n.skipped[size_t(capture::SkipReason::kSkinUnsupported)] = 4;
    CHECK(99, FormatStatusText(n, none, nullptr)
                      .find("skipped 9 (top: instance-unsupported 5, skin-unsupported 4)") != std::string::npos);
  }
  // 47-50: the shader's vertex count never exceeds the uploaded buffer.
  {
    CHECK(47, DrawVertexCount(100, 100) == 100);
    CHECK(48, DrawVertexCount(60, 100) == 60);   // smaller buffer under a key collision
    CHECK(49, DrawVertexCount(100, 40) == 40);
    CHECK(50, DrawVertexCount(0, 100) == 0);
  }
  // 60-69: terrain records: vertex count, position and index keys.
  {
    capture::DrawRecord t = BaseRecord();
    t.prim = capture::kPrimQuadPatch;
    t.indexed = false;
    t.vb = {};
    // The record's terrain inputs, which the scene holds in its side array (TerrainOf).
    capture::TerrainPatch tp;
    tp.active = true;
    tp.patch = 2;
    tp.patches = 3;
    tp.cols = 2;
    tp.inv_cols = 0.5f;
    tp.cell[0] = tp.cell[1] = 8;
    tp.map.phys_addr = 0x1BD0C000;
    tp.map.size = 0x5000;
    constexpr uint32_t kPts = (capture::kTerrainGrid + 1) * (capture::kTerrainGrid + 1);
    CHECK(60, RecordVertexCount(t, &tp) == 3 * kPts);
    CHECK(61, RecordVertexCount(BaseRecord(), nullptr) == 100);
    const GeoKey k = PositionKey(t, &tp);
    CHECK(62, k.addr == 0x1BD0C000 && k.size == 0x5000 && k.kind == 2 && k.stride == capture::kTerrainGrid);
    capture::TerrainPatch a = tp;
    a.patch = 3;
    CHECK(63, !(PositionKey(t, &a) == k));
    a = tp;
    a.origin[1] = 16;
    CHECK(64, !(PositionKey(t, &a) == k));
    a = tp;
    a.height_scale = 2;
    CHECK(65, !(PositionKey(t, &a) == k));
    a = tp;
    a.map.endian = 1;
    CHECK(66, !(PositionKey(t, &a) == k));
    const GeoKey ik = IndexKey(t, &tp);
    CHECK(67, ik.kind == 3 && ik.stride == capture::kTerrainGrid && ik.addr == 0);
    a = tp;
    a.patch = 9;  // same grid shape: same indices
    CHECK(68, IndexKey(t, &a) == ik);
    a.patches = 1;
    CHECK(69, !(IndexKey(t, &a) == ik));
    // A terrain record whose inputs the scene does not hold has no vertices (it has no
    // vertex stream) and no terrain key: nothing is built for it from other data.
    CHECK(334, RecordVertexCount(t, nullptr) == 0 && PositionKey(t, nullptr).kind == 0 &&
                   PositionKey(t, nullptr).size == 0 && IndexKey(t, nullptr).kind == 1);
  }
  // 70-72: rigid skin layouts are part of the position key.
  {
    capture::DrawRecord r = BaseRecord();
    capture::DrawRecord s = r;
    s.skin.active = true;
    s.skin.palette_addr = 0x3000;
    s.skin.palette_size = 168;
    s.skin.bone_stride = 24;
    CHECK(70, !(PositionKey(s, nullptr) == PositionKey(r, nullptr)) && PositionKey(s, nullptr).kind == 0);
    capture::DrawRecord s2 = s;
    s2.skin.palette_addr = 0x4000;
    CHECK(71, !(PositionKey(s2, nullptr) == PositionKey(s, nullptr)));
    s2 = s;
    s2.skin.index_offset_bytes = 12;
    CHECK(72, !(PositionKey(s2, nullptr) == PositionKey(s, nullptr)));
    s2 = s;
    s2.skin.weight_shift[1] = 8;
    CHECK(73, !(PositionKey(s2, nullptr) == PositionKey(s, nullptr)));
  }
  // 100-101: the skip reasons of sub-project 5.
  {
    CHECK(100, std::string(capture::SkipReasonName(capture::SkipReason::kInstanceUnsupported)) ==
                   "instance-unsupported");
    CHECK(101, std::string(capture::SkipReasonName(capture::SkipReason::kSkinUnsupported)) ==
                   "skin-unsupported");
  }
  // 102-121, 134-141: instanced records: a flat stream of flat_count positions
  // keyed by both streams, the row layouts, every constant and the flat count;
  // flat UVs keyed by the mesh UVs and what maps an index to a mesh vertex.
  {
    capture::DrawRecord r = BaseRecord();
    r.material.uv_vb = r.vb;
    r.material.uv.format = capture::UvFormat::kHalf2;
    r.material.uv.offset_bytes = 12;
    r.material.uv.stride_bytes = 16;
    capture::DrawRecord i = r;
    i.instances.active = true;
    i.instances.rows_addr = 0x7000;
    i.instances.rows_size = 28 * 40;
    for (int k = 0; k < 3; ++k) {
      i.instances.rows[k].format = capture::PosFormat::kHalf4;
      i.instances.rows[k].stride_bytes = 28;
      i.instances.rows[k].offset_bytes = uint32_t(8 * k);
    }
    i.instances.inv_count = 0.25f;
    i.instances.count = 4.0f;
    i.instances.first = 2.0f;
    i.instances.bias = 0.5f;
    i.instances.flat_count = 152;
    const GeoKey k = PositionKey(i, nullptr);
    CHECK(102, k.kind == 5 && k.addr == 0x2000 && k.size == 1600 && k.stride == 16);
    CHECK(103, PositionKey(r, nullptr).kind == 0 && !(PositionKey(r, nullptr) == k));
    capture::DrawRecord a = i;
    a.instances.first = 3.0f;  // identical except the first copy
    CHECK(104, PositionKey(a, nullptr).kind == 5 && !(PositionKey(a, nullptr) == k));
    a = i;
    a.instances.rows_addr = 0x8000;
    CHECK(105, !(PositionKey(a, nullptr) == k));
    a = i;
    a.instances.rows_size = 28 * 41;
    CHECK(106, !(PositionKey(a, nullptr) == k));
    a = i;
    a.instances.rows[2].swizzle = 0x11;
    CHECK(107, !(PositionKey(a, nullptr) == k));
    a = i;
    a.instances.inv_count = 0.2f;
    CHECK(108, !(PositionKey(a, nullptr) == k));
    a = i;
    a.instances.count = 5.0f;
    CHECK(109, !(PositionKey(a, nullptr) == k));
    a = i;
    a.instances.bias = 0.25f;
    CHECK(110, !(PositionKey(a, nullptr) == k));
    a = i;
    a.instances.offset[1] = 8.0f;
    CHECK(111, !(PositionKey(a, nullptr) == k));
    a = i;
    a.instances.flat_count = 153;
    CHECK(112, !(PositionKey(a, nullptr) == k));
    a = i;
    a.pos.swizzle = 0x11;  // the mesh layout still counts
    CHECK(113, !(PositionKey(a, nullptr) == k));
    a = i;
    a.seq = 99;  // irrelevant to the flat stream
    CHECK(114, PositionKey(a, nullptr) == k);
    // The flat stream holds flat_count positions, not the mesh's.
    CHECK(115, RecordVertexCount(i, nullptr) == 152 && RecordVertexCount(r, nullptr) == 100);
    // Flat UVs: their own kind, never the mesh's UV buffer or the flat positions.
    const GeoKey uk = UvKey(i);
    CHECK(116, uk.kind == 6 && uk.addr == 0x2000 && uk.size == 1600 && uk.stride == 16);
    CHECK(117, UvKey(r).kind == 4 && !(UvKey(r) == uk) && !(uk == k));
    // uvs[i] = mesh_uv[vertex(i)], and vertex(i) takes the index, the bias,
    // inv_count and count only (instance_expand.h): what places the copies
    // (the first copy, the instance stream, its row layouts, the offset)
    // leaves the flat UVs as they are, so those draws share one UV buffer
    // while their flat positions differ.
    a = i;
    a.instances.first = 3.0f;
    CHECK(118, UvKey(a) == uk && !(PositionKey(a, nullptr) == k));
    a = i;
    a.instances.rows_addr = 0x8000;
    CHECK(134, UvKey(a) == uk && !(PositionKey(a, nullptr) == k));
    a = i;
    a.instances.rows_size = 28 * 41;
    CHECK(135, UvKey(a) == uk && !(PositionKey(a, nullptr) == k));
    a = i;
    a.instances.rows[2].swizzle = 0x11;
    a.instances.rows[1].offset_bytes = 12;
    CHECK(136, UvKey(a) == uk && !(PositionKey(a, nullptr) == k));
    a = i;
    a.instances.offset[1] = 8.0f;
    CHECK(137, UvKey(a) == uk && !(PositionKey(a, nullptr) == k));
    // What changes the vertex an index maps to, or how many UVs are built, changes the key.
    a = i;
    a.instances.inv_count = 0.2f;
    CHECK(138, UvKey(a).kind == 6 && !(UvKey(a) == uk));
    a = i;
    a.instances.count = 5.0f;
    CHECK(139, !(UvKey(a) == uk));
    a = i;
    a.instances.bias = 0.25f;
    CHECK(140, !(UvKey(a) == uk));
    a = i;
    a.instances.flat_count = 153;
    CHECK(120, !(UvKey(a) == uk));
    // The mesh's UV layout and stream still count.
    a = i;
    a.material.uv.comp_u = 1;
    CHECK(119, !(UvKey(a) == uk));
    a = i;
    a.material.uv_vb.phys_addr = 0x6000;
    CHECK(141, !(UvKey(a) == uk));
    // The mesh stream on its own (decoded once per frame for all the draws
    // that expand it): the plain keys, whatever the instance set.
    CHECK(122, MeshPositionKey(i) == PositionKey(r, nullptr) && MeshPositionKey(i).kind == 0);
    CHECK(123, MeshUvKey(i) == UvKey(r) && MeshUvKey(i).kind == 4);
    a = i;
    a.instances.first = 3.0f;
    a.instances.flat_count = 99;
    a.instances.rows_addr = 0x9000;
    CHECK(124, MeshPositionKey(a) == MeshPositionKey(i) && MeshUvKey(a) == MeshUvKey(i));
    a = i;
    a.pos.swizzle = 0x11;
    a.material.uv.comp_u = 1;
    CHECK(125, !(MeshPositionKey(a) == MeshPositionKey(i)) && !(MeshUvKey(a) == MeshUvKey(i)));
    a = i;
    a.vb.size = 3200;
    a.material.uv_vb.size = 3200;
    CHECK(126, !(MeshPositionKey(a) == MeshPositionKey(i)) && !(MeshUvKey(a) == MeshUvKey(i)));
    // The renderer's index rule runs on the flat count: index 151 is the last.
    CHECK(121, IndexRangeValid(0, 151, 0, RecordVertexCount(i, nullptr)) &&
                   !IndexRangeValid(0, 152, 0, RecordVertexCount(i, nullptr)));
  }
  // 142-149: an instanced draw decodes no more of its mesh stream than one
  // copy has vertices (every index maps to a vertex below count), and never
  // more than the stream holds.
  {
    capture::InstanceSet s;
    s.count = 12.0f;
    CHECK(142, InstanceMeshCount(100, s) == 12);
    CHECK(143, InstanceMeshCount(8, s) == 8 && InstanceMeshCount(0, s) == 0);
    s.count = 100.0f;
    CHECK(144, InstanceMeshCount(100, s) == 100);
    // Counts the capture rejects (InstanceBoundsOk) never reach the cast: no
    // vertex below a count under one or a NaN, no bound from one past 2^32.
    s.count = 0.5f;
    CHECK(145, InstanceMeshCount(100, s) == 0);
    s.count = std::numeric_limits<float>::quiet_NaN();
    CHECK(146, InstanceMeshCount(100, s) == 0);
    s.count = -4.0f;
    CHECK(147, InstanceMeshCount(100, s) == 0);
    s.count = 4294967296.0f;
    CHECK(148, InstanceMeshCount(100, s) == 100);
    s.count = std::numeric_limits<float>::infinity();
    CHECK(149, InstanceMeshCount(100, s) == 100);
  }
  // 150-169: pipeline keys from decoded render states (real words: frame-map section 13).
  {
    using capture::Compare;
    using capture::Cull;
    using capture::Factor;
    const PipelineKey def;
    CHECK(150, MakePipelineKey(capture::DefaultRenderState(), 0) == def && def.textured == 0 && def.blend == 0 &&
                   def.src == 1 && def.dst == 0 && def.color_mask == 0xF && def.depth_test == 1 &&
                   def.depth_write == 1 && def.depth_func == uint8_t(Compare::kLess) && def.cull == 0);
    // An invalid state (unread words; garbage in a field) is the default key of its `textured`.
    capture::RenderState bad = capture::DecodeRenderState(capture::StateWords{});
    CHECK(151, !bad.valid && MakePipelineKey(bad, 0) == def);
    bad = State(0x00708767, 0x07060706, 0x0000000C, 0x00218006);
    bad.valid = false;
    PipelineKey def1;
    def1.textured = 1;
    CHECK(152, MakePipelineKey(bad, 0) == def && MakePipelineKey(bad, 1) == def1 && !(def1 == def));
    // The alpha test is a discard in the pixel shader: reference, function and
    // alpha-to-mask are constants, not pipeline state.
    const capture::RenderState opaque = State(0x00708767, 0x00010001, 0x00000004, 0x00218006);
    const PipelineKey k = MakePipelineKey(opaque, 1);
    CHECK(153, MakePipelineKey(State(0x00708767, 0x00010001, 0x8700001C, 0x00218006, 0xF, 0.0f), 1) == k &&
                   MakePipelineKey(State(0x00708767, 0x00010001, 0x0000000E, 0x00218006, 0xF, 0.5f), 1) == k);
    // The opaque mesh state: no blend, depth greater-equal with write under a
    // reversed range is less-equal on the pass's own depth, back faces culled.
    CHECK(154, k.textured == 1 && k.blend == 0 && k.color_mask == 0xF && k.depth_test == 1 && k.depth_write == 1 &&
                   k.depth_func == uint8_t(Compare::kLessEqual) && k.cull == uint8_t(Cull::kBack));
    // Every key field tells two states apart.
    CHECK(155, !(MakePipelineKey(opaque, 0) == k));
    const PipelineKey fur = MakePipelineKey(State(0x00708767, 0x07060706, 0x0000000E, 0x00218006), 1);
    CHECK(156, fur.blend == 1 && fur.src == uint8_t(Factor::kSrcAlpha) && fur.dst == uint8_t(Factor::kInvSrcAlpha) &&
                   fur.op == 0 && fur.src_alpha == fur.src && fur.dst_alpha == fur.dst && fur.op_alpha == 0 &&
                   !(fur == k));
    const PipelineKey add = MakePipelineKey(State(0x00708767, 0x01060106, 0x00000004, 0x00218006), 1);
    const PipelineKey premul = MakePipelineKey(State(0x00708767, 0x07010701, 0x00000004, 0x00218006), 1);
    CHECK(157, add.dst == uint8_t(Factor::kOne) && !(add == fur) && premul.src == uint8_t(Factor::kOne) &&
                   !(premul == fur));
    // Colour and alpha halves separately (words no capture holds), and the operation.
    const PipelineKey a_src = MakePipelineKey(State(0x00708767, 0x07010706, 0x00000004, 0x00218006), 1);
    const PipelineKey a_dst = MakePipelineKey(State(0x00708767, 0x01060706, 0x00000004, 0x00218006), 1);
    const PipelineKey op = MakePipelineKey(State(0x00708767, 0x07060746, 0x00000004, 0x00218006), 1);
    const PipelineKey a_op = MakePipelineKey(State(0x00708767, 0x07460706, 0x00000004, 0x00218006), 1);
    CHECK(158, a_src.src_alpha == uint8_t(Factor::kOne) && a_src.src == fur.src && !(a_src == fur) &&
                   a_dst.dst_alpha == uint8_t(Factor::kOne) && a_dst.dst == fur.dst && !(a_dst == fur));
    CHECK(159, op.op == uint8_t(capture::BlendOp::kMin) && op.op_alpha == 0 && !(op == fur) &&
                   a_op.op_alpha == uint8_t(capture::BlendOp::kMin) && a_op.op == 0 && !(a_op == fur));
    const PipelineKey mask0 = MakePipelineKey(State(0x00708767, 0x00010001, 0x00000004, 0x00218006, 0x0), 1);
    CHECK(160, mask0.color_mask == 0 && mask0.depth_write == 1 && !(mask0 == k));
    const PipelineKey no_write = MakePipelineKey(State(0x00708763, 0x00010001, 0x00000004, 0x00218006), 1);
    const PipelineKey greater = MakePipelineKey(State(0x00708747, 0x00010001, 0x00000004, 0x00218006), 1);
    const PipelineKey no_test = MakePipelineKey(State(0x00708765, 0x00010001, 0x00000004, 0x00218006), 1);
    CHECK(161, no_write.depth_write == 0 && no_write.depth_func == k.depth_func && !(no_write == k));
    CHECK(162, greater.depth_func == uint8_t(Compare::kLess) && greater.depth_write == 1 && !(greater == k));
    // Without the depth test nothing is written and the function does not matter.
    CHECK(163, no_test.depth_test == 0 && no_test.depth_write == 0 && no_test.depth_func == def.depth_func &&
                   !(no_test == k));
    // Cull after the front-face bit: meshes cull back faces with clockwise front
    // faces, terrain with counter-clockwise ones, foliage culls nothing.
    const PipelineKey terrain = MakePipelineKey(State(0x00708767, 0x00010001, 0x00000004, 0x00218002), 1);
    const PipelineKey two_sided = MakePipelineKey(State(0x00708767, 0x00010001, 0x00000004, 0x00218000), 1);
    CHECK(164, terrain.cull == uint8_t(Cull::kFront) && two_sided.cull == uint8_t(Cull::kNone) &&
                   !(terrain == k) && !(two_sided == k) && !(terrain == two_sided));
    CHECK(165, ClayCull(Cull::kBack, true) == Cull::kBack && ClayCull(Cull::kFront, true) == Cull::kFront &&
                   ClayCull(Cull::kBack, false) == Cull::kFront && ClayCull(Cull::kFront, false) == Cull::kBack &&
                   ClayCull(Cull::kNone, true) == Cull::kNone && ClayCull(Cull::kNone, false) == Cull::kNone);
    // The depth function is mirrored only under a reversed range.
    CHECK(166, MirrorCompare(Compare::kGreater) == Compare::kLess &&
                   MirrorCompare(Compare::kGreaterEqual) == Compare::kLessEqual &&
                   MirrorCompare(Compare::kLess) == Compare::kGreater &&
                   MirrorCompare(Compare::kLessEqual) == Compare::kGreaterEqual &&
                   MirrorCompare(Compare::kEqual) == Compare::kEqual &&
                   MirrorCompare(Compare::kNotEqual) == Compare::kNotEqual &&
                   MirrorCompare(Compare::kAlways) == Compare::kAlways &&
                   MirrorCompare(Compare::kNever) == Compare::kNever);
    capture::RenderState forward = opaque;
    forward.depth_reversed = false;
    forward.depth_func = Compare::kLessEqual;
    CHECK(167, DepthSupported(forward) && MakePipelineKey(forward, 1).depth_func == uint8_t(Compare::kLessEqual));
    // A depth function the RHI cannot express (greater-equal on the pass's own
    // depth, equal, never): the default depth state.
    forward.depth_func = Compare::kGreaterEqual;
    forward.depth_write = false;
    CHECK(168, !DepthSupported(forward) && MakePipelineKey(forward, 1).depth_func == def.depth_func &&
                   MakePipelineKey(forward, 1).depth_write == 1 && MakePipelineKey(forward, 1).depth_test == 1 &&
                   MakePipelineKey(forward, 1).cull == k.cull);
    capture::RenderState equal = opaque;
    equal.depth_func = Compare::kEqual;
    capture::RenderState never = opaque;
    never.depth_func = Compare::kNever;
    capture::RenderState always = opaque;
    always.depth_func = Compare::kAlways;
    CHECK(169, !DepthSupported(equal) && !DepthSupported(never) && DepthSupported(always) &&
                   DepthSupported(opaque) && MakePipelineKey(always, 1).depth_func == uint8_t(Compare::kAlways));
    // A blend state the RHI cannot express (the constant-colour factors of the
    // walking capture, a subtract): never in a key.
    const capture::RenderState constant = State(0x00708767, 0x0D0C0D0C, 0x00000004, 0x00218006);
    const capture::RenderState subtract = State(0x00708767, 0x07260726, 0x00000004, 0x00218006);
    CHECK(170, constant.valid && constant.blend && !BlendSupported(constant) && MakePipelineKey(constant, 1) == k);
    CHECK(171, subtract.valid && subtract.blend && !BlendSupported(subtract) &&
                   MakePipelineKey(subtract, 1) == k && BlendSupported(opaque) &&
                   BlendSupported(State(0x00708767, 0x07060706, 0x0000000E, 0x00218006)));
    // 214-216: exactly the factors and operations the RHI has (zero, one, source
    // alpha and its inverse; add, min), over every value the decoder accepts.
    for (uint32_t f : {0u, 1u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u}) {
      const bool want = f == 0 || f == 1 || f == 6 || f == 7;
      CHECK(214, RhiFactor(Factor(f)) == want);
      // In each of the four factor positions of a blend word that is otherwise expressible.
      for (uint32_t shift : {0u, 8u, 16u, 24u}) {
        const uint32_t word = (0x07060706u & ~(0x1Fu << shift)) | (f << shift);
        const capture::RenderState st = State(0x00708767, word, 0x00000004, 0x00218006);
        CHECK(215, st.valid && BlendSupported(st) == want);
      }
    }
    for (uint32_t o = 0; o <= 4; ++o) {
      const bool want = o == 0 || o == 2;
      CHECK(216, RhiBlendOp(capture::BlendOp(o)) == want);
      for (uint32_t shift : {5u, 21u}) {
        const capture::RenderState st = State(0x00708767, 0x07060706u | (o << shift), 0x00000004, 0x00218006);
        CHECK(216, st.valid && BlendSupported(st) == want);
      }
    }
  }
  // 172-177: the pipeline cache's slots are bounded (Review Focus 2).
  {
    std::vector<PipelineKey> keys;
    PipelineKey a;
    PipelineKey b;
    b.cull = 2;
    CHECK(172, FindOrAddPipelineSlot(keys, a) == 0 && FindOrAddPipelineSlot(keys, b) == 1 && keys.size() == 2);
    CHECK(173, FindOrAddPipelineSlot(keys, a) == 0 && FindOrAddPipelineSlot(keys, b) == 1 && keys.size() == 2);
    // Appends until the bound, then no slot for a new key, while the existing
    // ones are still found and nothing is replaced.
    std::vector<PipelineKey> small;
    PipelineKey k;
    for (uint8_t i = 0; i < 4; ++i) {
      k.color_mask = i;
      CHECK(174, FindOrAddPipelineSlot(small, k, 4) == i);
    }
    k.color_mask = 9;
    CHECK(175, FindOrAddPipelineSlot(small, k, 4) == kNoPipelineSlot && small.size() == 4);
    CHECK(176, FindOrAddPipelineSlot(small, k, 4) == kNoPipelineSlot && small.size() == 4);
    for (uint8_t i = 0; i < 4; ++i) {
      k.color_mask = i;
      CHECK(177, FindOrAddPipelineSlot(small, k, 4) == i && small[i].color_mask == i);
    }
    std::vector<PipelineKey> full;
    for (size_t i = 0; i < kMaxClayPipelines + 10; ++i) {
      k.color_mask = uint8_t(i & 0xF);
      k.src = uint8_t(i >> 4);
      const size_t slot = FindOrAddPipelineSlot(full, k);
      CHECK(178, slot == (i < kMaxClayPipelines ? i : kNoPipelineSlot));
    }
    CHECK(179, full.size() == kMaxClayPipelines && kMaxClayPipelines == 64);
  }
  // 180-199: what the pass does with a record's state and material (Review Focus 3).
  {
    using capture::AlphaSource;
    capture::Material none;  // no recipe
    capture::Material recipe;
    recipe.alpha.source = AlphaSource::kAlbedo;
    recipe.alpha.component = 3;
    recipe.alpha_scale = 0.75f;
    const capture::RenderState opaque = State(0x00708767, 0x00010001, 0x00000004, 0x00218006);
    const capture::RenderState foliage = State(0x00708767, 0x00010001, 0x8700001C, 0x00218000, 0xF, 0.0f);
    const capture::RenderState prop = State(0x00708767, 0x00010001, 0x0000000C, 0x00218006, 0xF, 128.0f / 255.0f);
    const capture::RenderState fur = State(0x00708767, 0x07060706, 0x0000000E, 0x00218006, 0xF, 16.0f / 255.0f);
    const capture::RenderState layer = State(0x00708767, 0x01060106, 0x00000004, 0x00218002);
    // Opaque: mode and reference zero, scale one, nothing counted.
    DrawState d = DecideDrawState(opaque, recipe, 1, false);
    CHECK(180, !d.skip && d.alpha.mode == 0 && d.alpha.ref == 0.0f && d.alpha.scale == 1.0f && !d.alpha_tested &&
                   !d.blended && !d.alpha_unknown && !d.state_invalid && d.key == MakePipelineKey(opaque, 1));
    // A blended draw whose colour is unknown is not drawn, with or without a recipe.
    CHECK(181, DecideDrawState(fur, none, 0, false).skip && DecideDrawState(fur, recipe, 0, false).skip);
    // An alpha-tested draw without a recipe: drawn without the test, alpha 1, counted.
    d = DecideDrawState(foliage, none, 1, false);
    CHECK(182, !d.skip && d.alpha_unknown && !d.alpha_tested && !d.blended && d.alpha.mode == 0 &&
                   d.alpha.scale == 1.0f && d.key == MakePipelineKey(foliage, 1));
    d = DecideDrawState(prop, none, 0, false);
    CHECK(183, !d.skip && d.alpha_unknown && d.alpha.mode == 0 && d.key.textured == 0);
    // The flat shader has no alpha source: a recipe does not apply to a flat draw.
    d = DecideDrawState(prop, recipe, 0, false);
    CHECK(184, !d.skip && d.alpha_unknown && !d.alpha_tested && d.alpha.mode == 0 && d.alpha.scale == 1.0f);
    // A blended draw with colour but no recipe: drawn with the blend and alpha 1, counted.
    d = DecideDrawState(fur, none, 1, false);
    CHECK(185, !d.skip && d.alpha_unknown && d.blended && !d.alpha_tested && d.alpha.mode == 0 &&
                   d.alpha.scale == 1.0f && d.key.blend == 1);
    // Recipe and alpha test: function, test bit, source and component, reference and scale.
    d = DecideDrawState(prop, recipe, 1, false);
    CHECK(186, !d.skip && d.alpha_tested && !d.alpha_unknown && !d.blended &&
                   d.alpha.mode == (4u | kAlphaTest | kAlphaFromAlbedo | (3u << 5)) &&
                   d.alpha.ref == 128.0f / 255.0f && d.alpha.scale == 0.75f && d.key.blend == 0);
    // Alpha-to-mask is a second test; the fur shells blend and test with greater-equal.
    d = DecideDrawState(foliage, recipe, 1, false);
    // The four dither offsets of the state (0x87 here) travel in bits 16-23.
    CHECK(187, d.alpha_tested &&
                   d.alpha.mode == (4u | kAlphaTest | kAlphaFromAlbedo | (3u << 5) | kAlphaToMask | (0x87u << 16)) &&
                   d.alpha.ref == 0.0f && d.key.cull == 0 && kAlphaToMaskShift == 16);
    d = DecideDrawState(fur, recipe, 1, false);
    CHECK(188, !d.skip && d.alpha_tested && d.blended && !d.alpha_unknown &&
                   d.alpha.mode == (6u | kAlphaTest | kAlphaFromAlbedo | (3u << 5) | kAlphaBlend) &&
                   d.alpha.ref == 16.0f / 255.0f && d.key.blend == 1);
    // Alpha-to-mask without the test bit is still a test; a component other than w; the literal 1.
    capture::RenderState mask_only = opaque;
    mask_only.alpha_to_mask = true;
    capture::Material red = recipe;
    red.alpha.component = 0;
    d = DecideDrawState(mask_only, red, 1, false);
    // Offsets 0: the threshold is 1.0 at every pixel; the flag alone says the mask is on.
    CHECK(189, d.alpha_tested && !d.alpha_unknown && d.alpha.mode == (kAlphaFromAlbedo | kAlphaToMask));
    mask_only.alpha_to_mask_offsets = 0xAA;
    d = DecideDrawState(mask_only, red, 1, false);
    CHECK(217, d.alpha_tested && d.alpha.mode == (kAlphaFromAlbedo | kAlphaToMask | (0xAAu << 16)));
    // Offsets without the enable never reach the constants.
    mask_only.alpha_to_mask = false;
    mask_only.alpha_test = true;
    mask_only.alpha_func = capture::Compare::kGreater;
    d = DecideDrawState(mask_only, red, 1, false);
    CHECK(218, d.alpha.mode == (4u | kAlphaTest | kAlphaFromAlbedo));
    capture::Material one;
    one.alpha.source = AlphaSource::kOne;
    one.alpha_scale = 0.25f;
    d = DecideDrawState(prop, one, 1, false);
    CHECK(190, d.alpha_tested && !d.alpha_unknown && d.alpha.mode == (4u | kAlphaTest) && d.alpha.scale == 0.25f);
    // Another fetch as the alpha source is not bound by the pass: no recipe.
    capture::Material fetch;
    fetch.alpha.source = AlphaSource::kFetch;
    fetch.alpha.slot = 2;
    d = DecideDrawState(prop, fetch, 1, false);
    CHECK(191, !d.skip && d.alpha_unknown && !d.alpha_tested && d.alpha.mode == 0);
    // The colour division belongs to the shader, whatever the state.
    capture::Material divide = recipe;
    divide.alpha.divide_colour = true;
    CHECK(192, DecideDrawState(opaque, divide, 1, false).alpha.mode == kAlphaDivide &&
                   (DecideDrawState(foliage, divide, 1, false).alpha.mode & kAlphaDivide) != 0 &&
                   DecideDrawState(opaque, divide, 0, false).alpha.mode == 0 &&
                   !DecideDrawState(opaque, divide, 1, false).alpha_tested);
    // A blend state the RHI cannot express: left out even with colour and a recipe.
    const capture::RenderState constant = State(0x00708767, 0x0D0C0D0C, 0x00000004, 0x00218006);
    CHECK(193, DecideDrawState(constant, recipe, 1, false).skip && DecideDrawState(constant, none, 0, false).skip);
    // An invalid state: the default state, drawn, counted (Review Focus 1).
    capture::RenderState bad = fur;
    bad.valid = false;
    d = DecideDrawState(bad, recipe, 1, false);
    PipelineKey def1;
    def1.textured = 1;
    CHECK(194, !d.skip && d.state_invalid && d.key == def1 && d.alpha.mode == 0 && !d.blended && !d.alpha_tested &&
                   !d.alpha_unknown);
    d = DecideDrawState(capture::DecodeRenderState(capture::StateWords{}), none, 0, false);
    CHECK(195, !d.skip && d.state_invalid && d.key == PipelineKey{});
    // An unsupported depth function: the default depth state, counted, still drawn.
    capture::RenderState equal = opaque;
    equal.depth_func = capture::Compare::kEqual;
    d = DecideDrawState(equal, none, 1, false);
    CHECK(196, !d.skip && d.state_invalid && d.key.depth_func == uint8_t(capture::Compare::kLess) &&
                   d.key.depth_write == 1 && d.key.cull == uint8_t(capture::Cull::kBack));
    // With the depth test off the function is not used: nothing is invalid.
    equal.depth_test = false;
    d = DecideDrawState(equal, none, 1, false);
    CHECK(219, DepthSupported(equal) && !d.skip && !d.state_invalid && d.key.depth_test == 0 &&
                   d.key.depth_write == 0 && d.key.depth_func == PipelineKey{}.depth_func);
    // Colour mask 0 with depth write: a depth-only draw is drawn, also when its
    // state blends and its colour is unknown (it writes none).
    d = DecideDrawState(State(0x00708767, 0x00010001, 0x00000004, 0x00218006, 0x0), none, 0, false);
    CHECK(197, !d.skip && d.key.color_mask == 0 && d.key.depth_write == 1 && !d.blended);
    d = DecideDrawState(State(0x00708767, 0x07060706, 0x00000004, 0x00218006, 0x0), none, 0, false);
    PipelineKey depth_only;  // nothing of the captured blend stays in the key
    depth_only.color_mask = 0;
    depth_only.depth_func = uint8_t(capture::Compare::kLessEqual);
    depth_only.cull = uint8_t(capture::Cull::kBack);
    CHECK(198, !d.skip && d.key == depth_only && !d.blended && !d.alpha_unknown);
    // A terrain patch drawn as flat clay is drawn opaque: its blend is the
    // layer's own, which flat clay does not have.
    d = DecideDrawState(layer, none, 0, true);
    PipelineKey flat;  // the layer's depth and cull, none of its blend factors
    flat.depth_func = uint8_t(capture::Compare::kLessEqual);
    flat.cull = uint8_t(capture::Cull::kFront);
    CHECK(199, !d.skip && !d.blended && !d.alpha_unknown && d.key == flat &&
                   DecideDrawState(layer, none, 0, false).skip);
  }
  // 220-226: alpha-to-mask as the GPU does it without multisampling: a pixel is
  // covered iff alpha >= 1 - offset / 4, with the offset of its parity
  // (index = (x & 1) * 2 + (y & 1), two bits each from bit 0).
  {
    const float below = 0.99999994f;  // the float just below 1
    auto at = [](float t, float* lo, float* hi) {
      *lo = std::nextafter(t, 0.0f);
      *hi = std::nextafter(t, 2.0f);
    };
    // 0xAA: 0.5 at every pixel.
    for (uint32_t x = 0; x < 2; ++x) {
      for (uint32_t y = 0; y < 2; ++y) {
        float lo, hi;
        at(0.5f, &lo, &hi);
        CHECK(220, !AlphaToMaskCovers(0xAA, x, y, lo) && AlphaToMaskCovers(0xAA, x, y, 0.5f) &&
                       AlphaToMaskCovers(0xAA, x, y, hi));
      }
    }
    // 0x87: 0.25 (x even, y even), 0.75 (even, odd), 1.0 (odd, even), 0.5 (odd, odd).
    const struct { uint32_t x, y; float t; } dither[] = {{0, 0, 0.25f}, {0, 1, 0.75f}, {1, 0, 1.0f}, {1, 1, 0.5f}};
    for (const auto& c : dither) {
      float lo, hi;
      at(c.t, &lo, &hi);
      CHECK(221, !AlphaToMaskCovers(0x87, c.x, c.y, lo) && AlphaToMaskCovers(0x87, c.x, c.y, c.t) &&
                     AlphaToMaskCovers(0x87, c.x, c.y, hi));
      // Only the parity of the pixel counts.
      CHECK(222, AlphaToMaskCovers(0x87, c.x + 1118, c.y + 718, c.t) &&
                     !AlphaToMaskCovers(0x87, c.x + 1118, c.y + 718, lo));
    }
    // How many of a 2x2 block a fading draw keeps: none below 0.25, then one, two, three, all at 1.
    auto covered = [](uint8_t offsets, float a) {
      int n = 0;
      for (uint32_t x = 0; x < 2; ++x) {
        for (uint32_t y = 0; y < 2; ++y) n += AlphaToMaskCovers(offsets, x, y, a) ? 1 : 0;
      }
      return n;
    };
    CHECK(223, covered(0x87, 0.1878f) == 0 && covered(0x87, 0.2599f) == 1 && covered(0x87, 0.4910f) == 1 &&
                   covered(0x87, 0.5f) == 2 && covered(0x87, 0.884f) == 3 && covered(0x87, below) == 3 &&
                   covered(0x87, 1.0f) == 4 && covered(0xAA, 0.4910f) == 0 && covered(0xAA, 0.5f) == 4);
    // The alpha is not clamped; a NaN covers nothing; offsets 0 need alpha 1.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(224, covered(0x87, nan) == 0 && covered(0xAA, nan) == 0 && covered(0xFF, nan) == 0);
    CHECK(225, covered(0x87, 7.5f) == 4 && covered(0x87, -1.0f) == 0 &&
                   covered(0x87, std::numeric_limits<float>::infinity()) == 4);
    CHECK(226, covered(0x00, below) == 0 && covered(0x00, 1.0f) == 4 && covered(0xFF, 0.25f) == 4 &&
                   covered(0xFF, std::nextafter(0.25f, 0.0f)) == 0 &&
                   // each pixel its own offset: 3, 2, 1, 0 for indices 0 to 3
                   AlphaToMaskCovers(0x1B, 0, 0, 0.25f) && !AlphaToMaskCovers(0x1B, 0, 1, 0.25f) &&
                   AlphaToMaskCovers(0x1B, 0, 1, 0.5f) && !AlphaToMaskCovers(0x1B, 1, 0, 0.5f) &&
                   AlphaToMaskCovers(0x1B, 1, 0, 0.75f) && !AlphaToMaskCovers(0x1B, 1, 1, 0.75f));
  }
  // 227-236: alpha-to-mask under the surface's own sample count (RB_SURFACE_INFO bits 16-17:
  // 0 none, 1 two samples, 2 four). The clay target has one sample per pixel: a pixel is kept
  // iff at least half of the game's samples would be covered. Two samples:
  // alpha >= 0.5 - offset / 8; four: alpha >= 0.5 - offset / 16; none: alpha >= 1 - offset / 4.
  {
    // Offsets byte, pixel, sample count, threshold: kept at the threshold, not one float below.
    const struct { uint8_t offsets; uint32_t x, y, samples; float t; } table[] = {
        // 0x87: offsets 3, 1, 0, 2 for indices 0 to 3.
        {0x87, 0, 0, 0, 0.25f},   {0x87, 0, 1, 0, 0.75f},   {0x87, 1, 0, 0, 1.0f}, {0x87, 1, 1, 0, 0.5f},
        {0x87, 0, 0, 1, 0.125f},  {0x87, 0, 1, 1, 0.375f},  {0x87, 1, 0, 1, 0.5f}, {0x87, 1, 1, 1, 0.25f},
        {0x87, 0, 0, 2, 0.3125f}, {0x87, 0, 1, 2, 0.4375f}, {0x87, 1, 0, 2, 0.5f}, {0x87, 1, 1, 2, 0.375f},
        // 0xAA: offset 2 at every pixel.
        {0xAA, 0, 0, 0, 0.5f},    {0xAA, 1, 1, 0, 0.5f},    {0xAA, 0, 0, 1, 0.25f}, {0xAA, 0, 1, 1, 0.25f},
        {0xAA, 1, 0, 1, 0.25f},   {0xAA, 1, 1, 1, 0.25f},   {0xAA, 0, 0, 2, 0.375f}, {0xAA, 1, 0, 2, 0.375f},
        // 0x1B: each pixel its own offset, 3, 2, 1, 0; 0x00 and 0xFF: the ends of the range.
        {0x1B, 0, 0, 1, 0.125f},  {0x1B, 0, 1, 1, 0.25f},   {0x1B, 1, 0, 1, 0.375f}, {0x1B, 1, 1, 1, 0.5f},
        {0x1B, 0, 0, 2, 0.3125f}, {0x1B, 0, 1, 2, 0.375f},  {0x1B, 1, 0, 2, 0.4375f}, {0x1B, 1, 1, 2, 0.5f},
        {0x00, 1, 1, 0, 1.0f},    {0x00, 1, 1, 1, 0.5f},    {0x00, 1, 1, 2, 0.5f},
        {0xFF, 0, 1, 0, 0.25f},   {0xFF, 0, 1, 1, 0.125f},  {0xFF, 0, 1, 2, 0.3125f},
        // The field's fourth value is no sample count: the no-multisampling form.
        {0x87, 0, 0, 3, 0.25f},   {0x87, 0, 1, 3, 0.75f},   {0x87, 1, 0, 3, 1.0f}, {0x87, 1, 1, 3, 0.5f},
    };
    for (const auto& c : table) {
      const float lo = std::nextafter(c.t, 0.0f), hi = std::nextafter(c.t, 2.0f);
      CHECK(227, AlphaToMaskCovers(c.offsets, c.x, c.y, c.t, c.samples) &&
                     !AlphaToMaskCovers(c.offsets, c.x, c.y, lo, c.samples) &&
                     AlphaToMaskCovers(c.offsets, c.x, c.y, hi, c.samples));
      // Only the parity of the pixel counts, under every sample count.
      CHECK(228, AlphaToMaskCovers(c.offsets, c.x + 1118, c.y + 718, c.t, c.samples) &&
                     !AlphaToMaskCovers(c.offsets, c.x + 1118, c.y + 718, lo, c.samples));
    }
    // The sample count defaults to none.
    CHECK(229, AlphaToMaskCovers(0x87, 1, 0, 1.0f) && !AlphaToMaskCovers(0x87, 1, 0, 0.99f) &&
                   AlphaToMaskCovers(0x87, 1, 0, 0.99f, 1));
    auto covered = [](uint8_t offsets, float a, uint32_t samples) {
      int n = 0;
      for (uint32_t x = 0; x < 2; ++x) {
        for (uint32_t y = 0; y < 2; ++y) n += AlphaToMaskCovers(offsets, x, y, a, samples) ? 1 : 0;
      }
      return n;
    };
    // The main scene's case (two samples, 0x87): a texel with alpha 0.5 or 0.99 keeps all four
    // pixel positions, where the one-sample form opens one or two holes per 2x2 block.
    CHECK(230, covered(0x87, 0.5f, 1) == 4 && covered(0x87, 0.99f, 1) == 4 && covered(0x87, 1.0f, 1) == 4 &&
                   covered(0x87, 0.5f, 0) == 2 && covered(0x87, 0.99f, 0) == 3 &&
                   covered(0x87, 0.5f, 2) == 4 && covered(0x87, 0.99f, 2) == 4);
    // A fading draw under two samples: none below 0.125, then one more pixel every eighth; under
    // four from 0.3125 every sixteenth; 0xAA cuts at 0.25 and at 0.375.
    CHECK(231, covered(0x87, 0.1249f, 1) == 0 && covered(0x87, 0.125f, 1) == 1 && covered(0x87, 0.25f, 1) == 2 &&
                   covered(0x87, 0.375f, 1) == 3 && covered(0x87, 0.4999f, 1) == 3 &&
                   covered(0x87, 0.3124f, 2) == 0 && covered(0x87, 0.3125f, 2) == 1 && covered(0x87, 0.375f, 2) == 2 &&
                   covered(0x87, 0.4375f, 2) == 3 && covered(0x87, 0.4999f, 2) == 3);
    CHECK(232, covered(0xAA, 0.2499f, 1) == 0 && covered(0xAA, 0.25f, 1) == 4 && covered(0xAA, 0.3749f, 2) == 0 &&
                   covered(0xAA, 0.375f, 2) == 4);
    // The alpha is not clamped and a NaN covers nothing, under every sample count.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (uint32_t samples = 0; samples < 4; ++samples) {
      CHECK(233, covered(0x87, nan, samples) == 0 && covered(0xFF, nan, samples) == 0 &&
                     covered(0x87, -1.0f, samples) == 0 && covered(0x87, 7.5f, samples) == 4 &&
                     covered(0x00, std::numeric_limits<float>::infinity(), samples) == 4);
    }
    // The rule against the emulator's own per-sample thresholds (SDK dxbc_translator_om.cpp
    // CompletePixelShader_AlphaToMask: a sample is covered iff alpha >= base - offset * scale;
    // one sample base 1 scale 1/4, two bases 0.5 and 1 scale 1/8, four bases 0.75, 0.25, 0.5
    // and 1 scale 1/16): kept iff at least half of the samples are covered, for every offset
    // and every alpha on a grid of sixty-fourths, one float below and one above.
    const struct { int count; float base[4]; float scale; } forms[] = {
        {1, {1.0f, 0.0f, 0.0f, 0.0f}, 0.25f},
        {2, {0.5f, 1.0f, 0.0f, 0.0f}, 0.125f},
        {4, {0.75f, 0.25f, 0.5f, 1.0f}, 0.0625f},
    };
    bool agrees = true;
    for (uint32_t samples = 0; samples < 3; ++samples) {
      const auto& f = forms[samples];
      for (uint32_t offset = 0; offset < 4; ++offset) {
        for (int k = -4; k <= 68; ++k) {
          const float grid = float(k) / 64.0f;
          for (float a : {std::nextafter(grid, -1.0f), grid, std::nextafter(grid, 2.0f)}) {
            int n = 0;
            for (int s = 0; s < f.count; ++s) n += a >= f.base[s] - float(offset) * f.scale ? 1 : 0;
            // Index 3 (x odd, y odd) reads bits 6-7 of the offsets byte.
            agrees = agrees && AlphaToMaskCovers(uint8_t(offset << 6), 1, 1, a, samples) == (2 * n >= f.count);
          }
        }
      }
    }
    CHECK(234, agrees);
    // The sample count travels in bits 24-25 of the mode, only with alpha-to-mask, and is no
    // part of the pipeline key: it adds no pipeline.
    capture::Material recipe;
    recipe.alpha.source = capture::AlphaSource::kAlbedo;
    recipe.alpha.component = 3;
    capture::RenderState foliage = State(0x00708767, 0x00010001, 0x8700001C, 0x00218000, 0xF, 0.0f);
    const DrawState none = DecideDrawState(foliage, recipe, 1, false);
    const uint32_t base_mode = 4u | kAlphaTest | kAlphaFromAlbedo | (3u << 5) | kAlphaToMask | (0x87u << 16);
    foliage.alpha_to_mask_samples = 1;
    const DrawState two = DecideDrawState(foliage, recipe, 1, false);
    foliage.alpha_to_mask_samples = 2;
    const DrawState four = DecideDrawState(foliage, recipe, 1, false);
    CHECK(235, kAlphaToMaskSamplesShift == 24 && none.alpha.mode == base_mode &&
                   two.alpha.mode == (base_mode | (1u << 24)) && four.alpha.mode == (base_mode | (2u << 24)) &&
                   two.alpha_tested && !two.alpha_unknown && !two.state_invalid && two.key == none.key &&
                   four.key == none.key && MakePipelineKey(foliage, 1) == none.key);
    std::vector<PipelineKey> keys;
    FindOrAddPipelineSlot(keys, none.key);
    FindOrAddPipelineSlot(keys, two.key);
    FindOrAddPipelineSlot(keys, four.key);
    capture::RenderState prop = State(0x00708767, 0x00010001, 0x0000000C, 0x00218006, 0xF, 128.0f / 255.0f);
    prop.alpha_to_mask_samples = 1;  // not what the decoder gives: the mode still takes it only with the mask
    CHECK(236, keys.size() == 1 &&
                   DecideDrawState(prop, recipe, 1, false).alpha.mode == (4u | kAlphaTest | kAlphaFromAlbedo | (3u << 5)) &&
                   DecideDrawState(foliage, capture::Material{}, 1, false).alpha.mode == 0);
  }
  // 200-204: the alpha fields of the root constants (36 dwords).
  {
    static_assert(sizeof(ClayConstants) == 144);
    const float uv[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    capture::DrawRecord r = BaseRecord();
    r.cut[0] = 1.0f; r.cut[1] = 2.0f; r.cut[2] = 3.0f; r.cut[3] = 4.0f;
    uint32_t dw[36];
    const ClayConstants plain = MakeClayConstants(r, 10, 0, uv, true, 1);
    std::memcpy(dw, &plain, sizeof(dw));
    // An opaque draw: mode and reference zero, scale one.
    CHECK(200, dw[26] == 0 && dw[27] == 0 && dw[32] == FloatBits(1.0f) && dw[33] == 0 && dw[34] == 0 &&
                   dw[35] == 0);
    ClayAlpha a;
    a.mode = 4u | kAlphaTest | kAlphaFromAlbedo | (3u << 5);
    a.ref = 128.0f / 255.0f;
    a.scale = 0.75f;
    const ClayConstants c = MakeClayConstants(r, 10, 0, uv, true, 1, a);
    std::memcpy(dw, &c, sizeof(dw));
    CHECK(201, dw[24] == 1 && dw[25] == 1 && dw[26] == a.mode && dw[27] == FloatBits(128.0f / 255.0f));
    CHECK(202, dw[28] == FloatBits(1.0f) && dw[31] == FloatBits(4.0f));
    CHECK(203, dw[32] == FloatBits(0.75f) && dw[33] == 0 && dw[34] == 0 && dw[35] == 0);
    CHECK(204, kAlphaTest == 8u && kAlphaFromAlbedo == 16u && kAlphaBlend == 128u && kAlphaToMask == 256u &&
                   kAlphaDivide == 512u);
  }
  // 205-213: F3 text with the state counters and the two new reasons.
  {
    FrameScene s;
    s.captured = 985;
    s.skipped[size_t(capture::SkipReason::kUnsupportedPrim)] = 30;
    s.skipped[size_t(capture::SkipReason::kDepthOnly)] = 3;
    ClayStats st;
    st.drawn = 920;
    st.blend_unknown = 27;
    st.alpha_unknown = 140;
    st.alpha_tested = 12;
    st.blended = 5;
    st.state_invalid = 2;
    st.pipelines = 9;
    TextureStats ts;
    ts.textured = 325;
    ts.status[size_t(capture::MaterialStatus::kTextured)] = 325;
    ts.status[size_t(capture::MaterialStatus::kTerrain)] = 564;
    ts.terrain = 564;
    ts.status[size_t(capture::MaterialStatus::kNoAlbedo)] = 31;
    const std::string t = FormatStatusText(s, st, &ts);
    const std::string want =
        "Native: captured 985, drawn 920 (deformed 0), skipped 60 "
        "(top: unsupported-prim 30, blend-unknown 27, depth-only 3)\n"
        "Geometry: 0 uploads, 0 hits, 0.0 MB resident, instanced 0, skinned 0 | hash 0.00 ms, "
        "decode 0.00 ms, record 0.00 ms\n"
        "State: 9 pipelines, alpha test 12, blended 5, invalid 2\n"
        "Capture: 0.00 ms guest time per frame\n"
        "Textures: textured 325 of 920 drawn (35%, 91% non-terrain), resident 0 (0.0 MB), uploads 0 (0.0 MB), "
        "decode 0.00 ms | top untextured: terrain 564, alpha-unknown 140, no-albedo 31";
    CHECK(205, t == want);
    if (t != want) std::cerr << "got:  " << t << "\nwant: " << want << "\n";
    // The two reasons rank with the texture statuses, by count.
    CHECK(206, FormatTextureText(ts, 920, 0, 600).find("top untextured: blend-unknown 600, terrain 564, no-albedo 31") !=
                   std::string::npos);
    CHECK(207, FormatTextureText(ts, 920, 0, 0).find("unknown") == std::string::npos &&
                   FormatTextureText(ts, 920).find("unknown") == std::string::npos);
    TextureStats only;
    only.textured = 4;
    only.status[size_t(capture::MaterialStatus::kTextured)] = 4;
    CHECK(208, FormatTextureText(only, 4, 3, 1).find("| top untextured: alpha-unknown 3, blend-unknown 1") !=
                   std::string::npos);
    // Draws past the pipeline cache are named on the state line.
    st.pipelines = 64;
    st.pipeline_overflow = 7;
    CHECK(209, FormatStateText(st) == "State: 64 pipelines (overflow 7), alpha test 12, blended 5, invalid 2");
    CHECK(210, FormatStateText(ClayStats{}) == "State: 0 pipelines, alpha test 0, blended 0, invalid 0");
    // With the texture path off the state line is still there.
    const std::string off = FormatStatusText(s, st, nullptr);
    CHECK(211, off.find("\nState: 64 pipelines (overflow 7), alpha test 12, blended 5, invalid 2\nCapture: ") !=
                   std::string::npos && off.find("blend-unknown 27") != std::string::npos);
    // The log's breakdown of the two reasons by pixel shader.
    ClayStats by;
    CHECK(212, FormatUnknownByPs(by).empty());
    for (int i = 0; i < 3; ++i) by.unknown_by_ps.Add(0x00E09D1BC5295D52ull, kTallyAlphaUnknown);
    by.unknown_by_ps.Add(0x7CD57B81550F19E3ull, kTallyAlphaUnknown);
    for (int i = 0; i < 15; ++i) by.unknown_by_ps.Add(0x014F8A02DB7B19CAull, kTallyBlendUnknown);
    CHECK(213, FormatUnknownByPs(by) ==
                   "alpha-unknown by ps {0x00E09D1BC5295D52: 3, 0x7CD57B81550F19E3: 1} "
                   "blend-unknown by ps {0x014F8A02DB7B19CA: 15}");
    if (g_fail == 213) std::cerr << "got:  " << FormatUnknownByPs(by) << "\n";
  }
  // 240-292: the terrain material (frame-map section 13, "Terrain", "Native terrain material").
  {
    using capture::Compare;
    using capture::Cull;
    using capture::Factor;
    using capture::MaterialKind;
    using capture::MaterialStatus;
    // Real words of the bridge capture of session 1: every layer draw has depth 0x00708767 and
    // blends source alpha / one (0x01060106) or, as the first layer of a square without an
    // underlay, source alpha / zero (0x00060006); the adaptive draws have 0x00708766 and do not
    // blend; all cull with 0x00218002.
    const capture::RenderState add = State(0x00708767, 0x01060106, 0x00000004, 0x00218002);
    const capture::RenderState replace = State(0x00708767, 0x00060006, 0x00000004, 0x00218002);
    const capture::RenderState far_state = State(0x00708766, 0x00010001, 0x00000004, 0x00218002);
    const capture::Material none;
    // 240-243: pipeline keys of the two terrain pixel shaders.
    const PipelineKey ka = MakePipelineKey(add, kTexturedTerrainLayer);
    const PipelineKey kr = MakePipelineKey(replace, kTexturedTerrainLayer);
    const PipelineKey kf = MakePipelineKey(far_state, kTexturedTerrainFar);
    CHECK(240, kTexturedTerrainLayer == 2 && kTexturedTerrainFar == 3 && kClayPixelShaders == 4);
    CHECK(241, ka.textured == 2 && ka.blend == 1 && ka.src == uint8_t(Factor::kSrcAlpha) &&
                   ka.dst == uint8_t(Factor::kOne) && ka.op == 0 && ka.depth_test == 1 && ka.depth_write == 1 &&
                   ka.depth_func == uint8_t(Compare::kLessEqual) && ka.cull == uint8_t(Cull::kFront));
    CHECK(242, kr.textured == 2 && kr.blend == 1 && kr.src == uint8_t(Factor::kSrcAlpha) &&
                   kr.dst == uint8_t(Factor::kZero) && !(kr == ka));
    CHECK(243, kf.textured == 3 && kf.blend == 0 && kf.depth_write == 1 &&
                   kf.depth_func == uint8_t(Compare::kLessEqual) && !(kf == MakePipelineKey(far_state, 1)) &&
                   !(kf == MakePipelineKey(far_state, kTexturedTerrainLayer)));
    // 244-250: a terrain record drawn with its material keeps the captured blend and writes the
    // shader's own alpha (weight x fade); it has no alpha recipe and is not alpha-unknown.
    DrawState d = DecideDrawState(add, none, kTexturedTerrainLayer, false);
    CHECK(244, !d.skip && d.blended && !d.alpha_unknown && !d.alpha_tested && !d.state_invalid &&
                   d.alpha.mode == kAlphaBlend && d.alpha.scale == 1.0f && d.key == ka);
    d = DecideDrawState(replace, none, kTexturedTerrainLayer, false);
    CHECK(245, !d.skip && d.blended && !d.alpha_unknown && d.alpha.mode == kAlphaBlend && d.key == kr);
    d = DecideDrawState(far_state, none, kTexturedTerrainFar, false);
    CHECK(246, !d.skip && !d.blended && !d.alpha_unknown && !d.alpha_tested && d.alpha.mode == 0 && d.key == kf);
    // A recipe on the record is not the terrain shaders': no albedo component, no scale, no division.
    capture::Material recipe;
    recipe.alpha.source = capture::AlphaSource::kAlbedo;
    recipe.alpha.divide_colour = true;
    recipe.alpha_scale = 0.5f;
    d = DecideDrawState(add, recipe, kTexturedTerrainLayer, false);
    CHECK(247, d.alpha.mode == kAlphaBlend && d.alpha.scale == 1.0f);
    // An alpha test on a layer (no captured terrain row has one) tests the shader's alpha.
    d = DecideDrawState(State(0x00708767, 0x01060106, 0x0000000C, 0x00218002, 0xF, 0.25f), none,
                        kTexturedTerrainLayer, false);
    CHECK(248, !d.skip && d.alpha_tested && d.blended && !d.alpha_unknown &&
                   d.alpha.mode == (4u | kAlphaTest | kAlphaBlend) && d.alpha.ref == 0.25f);
    // A state that cannot be used as captured (unread, a depth function or a blend the RHI cannot
    // express) leaves a terrain record without its material: the default pipeline is opaque with
    // depth less, under which a layer over its underlay fails the depth test at equal depth and a
    // first layer writes its colour unweighted. The record is then one of a square that falls back.
    capture::RenderState bad = add;
    bad.valid = false;
    capture::RenderState equal = add;
    equal.depth_func = Compare::kEqual;
    const capture::RenderState constant = State(0x00708767, 0x0D0C0D0C, 0x00000004, 0x00218002);
    CHECK(249, TerrainStateUsable(add) && TerrainStateUsable(replace) && TerrainStateUsable(far_state) &&
                   !TerrainStateUsable(bad) && !TerrainStateUsable(equal) && !TerrainStateUsable(constant));
    // Its flat draw tests less-equal with depth write, not the default less: it lies at the depth
    // of the underlay it has to cover. Everything else of an unread state is the default.
    d = DecideDrawState(bad, none, 0, true);
    PipelineKey flat_default;
    flat_default.depth_func = uint8_t(Compare::kLessEqual);
    CHECK(294, !d.skip && d.state_invalid && d.key == flat_default && d.key == TerrainFlatDefaultKey() &&
                   d.key.depth_test == 1 && d.key.depth_write == 1 && d.key.blend == 0 && d.alpha.mode == 0);
    d = DecideDrawState(equal, none, 0, true);
    CHECK(295, !d.skip && d.state_invalid && d.key.depth_func == uint8_t(Compare::kLessEqual) &&
                   d.key.depth_write == 1 && d.key.cull == uint8_t(Cull::kFront) && d.key.blend == 0);
    // Any other flat draw with an unread state keeps the default state (Review Focus 1).
    CHECK(296, DecideDrawState(bad, none, 0, false).key == PipelineKey{} &&
                   DecideDrawState(bad, none, 0, false).state_invalid);
    // The same record as flat clay (its square fell back): opaque, with the layer's depth and cull.
    d = DecideDrawState(add, none, 0, true);
    CHECK(250, !d.skip && !d.blended && !d.alpha_unknown && d.key.textured == 0 && d.key.blend == 0 &&
                   d.key.depth_func == uint8_t(Compare::kLessEqual) && d.key.cull == uint8_t(Cull::kFront) &&
                   d.alpha.mode == 0);
    // 329-332: combinations the comments of DecideDrawState state (no captured terrain row has them).
    // A layer with the alpha test and alpha-to-mask (the foliage words, two samples) on its blend:
    // both apply to the shader's own alpha, with the offsets and the sample count; the record's
    // recipe adds no albedo component, no scale and no division; the pipeline is the layer's.
    capture::RenderState masked = State(0x00708767, 0x01060106, 0x8700001C, 0x00218002, 0xF, 0.25f);
    masked.alpha_to_mask_samples = 1;
    d = DecideDrawState(masked, recipe, kTexturedTerrainLayer, false);
    CHECK(329, !d.skip && d.alpha_tested && d.blended && !d.alpha_unknown && !d.state_invalid &&
                   d.alpha.mode == (4u | kAlphaTest | kAlphaToMask | (0x87u << kAlphaToMaskShift) |
                                    (1u << kAlphaToMaskSamplesShift) | kAlphaBlend) &&
                   d.alpha.ref == 0.25f && d.alpha.scale == 1.0f && d.key == ka);
    // Alpha-to-mask alone, on the far shader (alpha 1): applied and counted as a test, nothing else.
    capture::RenderState far_masked = State(0x00708766, 0x00010001, 0xAA000014, 0x00218002);
    d = DecideDrawState(far_masked, none, kTexturedTerrainFar, false);
    CHECK(330, !d.skip && d.alpha_tested && !d.blended && !d.alpha_unknown &&
                   d.alpha.mode == (kAlphaToMask | (0xAAu << kAlphaToMaskShift)) && d.key == kf);
    // A colour mask of 0 writes no colour, so the blend does not matter: the record is drawn for
    // its depth with the opaque factors, is not counted as blended and is not left out.
    d = DecideDrawState(State(0x00708767, 0x01060106, 0x00000004, 0x00218002, 0x0), none, kTexturedTerrainLayer, false);
    {
      PipelineKey depth_only;  // opaque factors, the layer's depth and cull
      depth_only.textured = kTexturedTerrainLayer;
      depth_only.color_mask = 0;
      depth_only.depth_func = ka.depth_func;
      depth_only.cull = ka.cull;
      CHECK(331, !d.skip && !d.blended && !d.alpha_unknown && !d.alpha_tested && !d.state_invalid &&
                     d.alpha.mode == 0 && d.key == depth_only && d.key.depth_write == 1);
    }
    // The flat fallback of an alpha-tested layer: flat clay has no alpha to test, so it is drawn
    // without the test, opaque, with the layer's depth and cull, and counted alpha-unknown like
    // any flat draw whose state tests (the recipe, its scale and its division are not used).
    d = DecideDrawState(masked, recipe, 0, true);
    CHECK(332, !d.skip && !d.alpha_tested && !d.blended && d.alpha_unknown && !d.state_invalid &&
                   d.alpha.mode == 0 && d.alpha.ref == 0.0f && d.alpha.scale == 1.0f && d.key.textured == 0 &&
                   d.key.blend == 0 && d.key.depth_func == uint8_t(Compare::kLessEqual) && d.key.depth_write == 1 &&
                   d.key.cull == uint8_t(Cull::kFront) && d.key == DecideDrawState(add, none, 0, true).key);

    // 251-258: the terrain constant block (register b1) holds the inputs at the offsets the HLSL
    // declares: the table beside the HLSL (shaders::kClayTerrainFields; test_native_shaders.cpp
    // checks it against the compiled shaders) names where each value must be.
    capture::TerrainMaterialInputs m;
    m.valid = true;
    for (int i = 0; i < 4; ++i) {
      m.detail_uv[i] = 10.0f + float(i);
      m.weight_uv[i] = 20.0f + float(i);
    }
    for (int i = 0; i < 12; ++i) m.world[i] = 30.0f + float(i);
    m.eye[0] = 50.0f; m.eye[1] = 51.0f; m.eye[2] = 52.0f;
    m.hole_ref = 53.0f;
    m.fade[0] = 54.0f; m.fade[1] = 55.0f;
    m.weight_scale = 56.0f;
    m.weight_bias = 57.0f;
    const float fix[2] = {0.5f, 0.25f};
    static_assert(sizeof(ClayTerrainConstants) == 192 && kClayTerrainSlotBytes == 256);
    ClayTerrainConstants c = MakeClayTerrainConstants(m, MaterialKind::kTerrainLayer, fix, 1, 3);
    auto field = [](const char* name) -> const shaders::ClayTerrainField* {
      for (const shaders::ClayTerrainField& f : shaders::kClayTerrainFields) {
        if (std::strcmp(f.name, name) == 0) return &f;
      }
      return nullptr;
    };
    auto floats = [&](const char* name, std::initializer_list<float> want) {
      const shaders::ClayTerrainField* f = field(name);
      if (!f || f->bytes != 4 * want.size() || f->offset + f->bytes > sizeof(c)) return false;
      size_t at = f->offset;
      for (float w : want) {
        float got;
        std::memcpy(&got, reinterpret_cast<const unsigned char*>(&c) + at, 4);
        if (got != w) return false;
        at += 4;
      }
      return true;
    };
    auto word = [&](const char* name, uint32_t want) {
      const shaders::ClayTerrainField* f = field(name);
      if (!f || f->bytes != 4 || f->offset + 4 > sizeof(c)) return false;
      uint32_t got;
      std::memcpy(&got, reinterpret_cast<const unsigned char*>(&c) + f->offset, 4);
      return got == want;
    };
    // The detail coordinate is scaled into the host texture's extent (BC textures are padded to
    // whole blocks), like the albedo UV transform.
    CHECK(251, floats("t_detail_uv", {5.0f, 2.75f, 6.0f, 3.25f}));
    CHECK(252, floats("t_weight_uv", {20.0f, 21.0f, 22.0f, 23.0f}));
    CHECK(253, floats("t_world0", {30.0f, 31.0f, 32.0f, 33.0f}) && floats("t_world1", {34.0f, 35.0f, 36.0f, 37.0f}) &&
                   floats("t_world2", {38.0f, 39.0f, 40.0f, 41.0f}));
    CHECK(254, floats("t_eye", {50.0f, 51.0f, 52.0f}) && floats("t_hole_ref", {53.0f}) &&
                   floats("t_fade", {54.0f, 55.0f}) && floats("t_weight_scale", {56.0f}) &&
                   floats("t_weight_bias", {57.0f}));
    CHECK(255, word("t_colour_sampler", 1) && word("t_weight_sampler", 3) && word("t_flags", 0) &&
                   word("t_side_sampler", 0));
    {
      // The table covers the block in order, without a gap or an overlap.
      uint32_t at = 0;
      bool ok = true;
      for (const shaders::ClayTerrainField& f : shaders::kClayTerrainFields) {
        ok = ok && f.offset == at;
        at += f.bytes;
      }
      CHECK(256, ok && at == sizeof(ClayTerrainConstants));
    }
    // The fading far kind (2F8A, the layers' underlay) sets the flag, the plain far kind does not.
    capture::TerrainMaterialInputs fm = m;
    fm.adaptive = true;
    c = MakeClayTerrainConstants(fm, MaterialKind::kTerrainFarFade, fix, 2, 0);
    CHECK(257, kTerrainFadeIn == 1u && word("t_flags", kTerrainFadeIn) && word("t_colour_sampler", 2) &&
                   floats("t_fade", {54.0f, 55.0f}) && floats("t_eye", {50.0f, 51.0f, 52.0f}));
    c = MakeClayTerrainConstants(fm, MaterialKind::kTerrainFar, fix, 2, 0);
    CHECK(258, word("t_flags", 0));

    // 301-304: the steep-ground block's constants (a layer of a shader paired with C30A). The side
    // coordinate's scale goes into the host texture's extent like the detail coordinate's; without the
    // block the fields are zero and the flag is clear.
    c = MakeClayTerrainConstants(m, MaterialKind::kTerrainLayer, fix, 1, 3);
    CHECK(301, floats("t_side_uv", {0.0f, 0.0f}) && floats("t_slope", {0.0f, 0.0f}) &&
                   floats("t_steep_threshold", {0.0f}) && floats("t_steep_eps", {0.0f}) && word("t_flags", 0));
    capture::TerrainMaterialInputs sm = m;
    sm.steep = true;
    sm.side_uv[0] = 0.125f;
    sm.side_uv[1] = -0.125f;
    sm.slope[0] = -7.5f;
    sm.slope[1] = 10.0f;
    sm.steep_threshold = 0.99609375f;
    sm.steep_eps = 0.0001f;
    const float side_fix[2] = {0.5f, 0.75f};
    c = MakeClayTerrainConstants(sm, MaterialKind::kTerrainLayer, fix, 1, 3, side_fix, 2);
    CHECK(302, kTerrainSteep == 2u && word("t_flags", kTerrainSteep) && word("t_side_sampler", 2) &&
                   floats("t_side_uv", {0.0625f, -0.09375f}) && floats("t_slope", {-7.5f, 10.0f}) &&
                   floats("t_steep_threshold", {0.99609375f}) && floats("t_steep_eps", {0.0001f}));
    // The rest of the block is what it is without it.
    CHECK(303, floats("t_detail_uv", {5.0f, 2.75f, 6.0f, 3.25f}) && word("t_colour_sampler", 1) &&
                   word("t_weight_sampler", 3) && floats("t_fade", {54.0f, 55.0f}));
    // Without a side texture resolved the block is off, whatever the inputs say; and a side texture
    // does not switch it on for inputs without the block, or for a far kind.
    c = MakeClayTerrainConstants(sm, MaterialKind::kTerrainLayer, fix, 1, 3);
    CHECK(304, word("t_flags", 0) && floats("t_side_uv", {0.0f, 0.0f}) && floats("t_steep_threshold", {0.0f}));
    c = MakeClayTerrainConstants(m, MaterialKind::kTerrainLayer, fix, 1, 3, side_fix, 2);
    CHECK(316, word("t_flags", 0) && word("t_side_sampler", 0) && floats("t_side_uv", {0.0f, 0.0f}));
    c = MakeClayTerrainConstants(sm, MaterialKind::kTerrainFarFade, fix, 1, 3, side_fix, 2);
    CHECK(317, word("t_flags", kTerrainFadeIn) && floats("t_slope", {0.0f, 0.0f}));

    // 259-265: whether a record is drawn with its material this frame, and the reason when not.
    const MaterialStatus ok = MaterialStatus::kTextured;
    CHECK(259, TerrainMaterialStatus(m, MaterialKind::kTerrainLayer, false, ok, ok, false) == ok);
    capture::TerrainMaterialInputs invalid = m;
    invalid.valid = false;
    CHECK(260, TerrainMaterialStatus(invalid, MaterialKind::kTerrainLayer, false, ok, ok, false) ==
                   MaterialStatus::kTerrainInputs);
    // The pixel shader must be a terrain shader of the record's builder.
    CHECK(261, TerrainMaterialStatus(m, MaterialKind::kPlain, false, ok, ok, false) == MaterialStatus::kTerrainInputs &&
                   TerrainMaterialStatus(m, MaterialKind::kTerrainFar, false, ok, ok, true) ==
                       MaterialStatus::kTerrainInputs &&
                   TerrainMaterialStatus(fm, MaterialKind::kTerrainLayer, true, ok, ok, true) ==
                       MaterialStatus::kTerrainInputs &&
                   TerrainMaterialStatus(m, MaterialKind::kTerrainLayer, true, ok, ok, true) ==
                       MaterialStatus::kTerrainInputs);
    // A texture that is not usable gives its reason: the colour texture's first, then the weights'.
    CHECK(262, TerrainMaterialStatus(m, MaterialKind::kTerrainLayer, false, MaterialStatus::kTexturePending,
                                     MaterialStatus::kTextureBad, false) == MaterialStatus::kTexturePending &&
                   TerrainMaterialStatus(m, MaterialKind::kTerrainLayer, false, ok,
                                         MaterialStatus::kFormatUnsupported, false) ==
                       MaterialStatus::kFormatUnsupported &&
                   TerrainMaterialStatus(m, MaterialKind::kTerrainLayer, false, MaterialStatus::kTextureDynamic, ok,
                                         false) == MaterialStatus::kTextureDynamic);
    // A far draw has no weights; it needs its coordinates from the two tables.
    CHECK(263, TerrainMaterialStatus(fm, MaterialKind::kTerrainFar, true, ok, MaterialStatus::kTextureBad, true) == ok &&
                   TerrainMaterialStatus(fm, MaterialKind::kTerrainFarFade, true, ok, ok, true) == ok &&
                   TerrainMaterialStatus(fm, MaterialKind::kTerrainFar, true, ok, ok, false) ==
                       MaterialStatus::kUvUnsupported &&
                   TerrainMaterialStatus(fm, MaterialKind::kTerrainFar, true, MaterialStatus::kTextureBad, ok, true) ==
                       MaterialStatus::kTextureBad);
    // 305-306: the steep-ground block's part of a layer's status. A record without the block needs
    // nothing more; one with it needs its side texture (the cache's reason when not usable) and the
    // normals of its grid points (uv-unsupported: the normal map could not be read or decoded).
    CHECK(305, TerrainSteepStatus(m, MaterialStatus::kTexturePending, false) == ok &&
                   TerrainSteepStatus(sm, ok, true) == ok);
    CHECK(306, TerrainSteepStatus(sm, MaterialStatus::kTexturePending, true) == MaterialStatus::kTexturePending &&
                   TerrainSteepStatus(sm, MaterialStatus::kFormatUnsupported, false) == MaterialStatus::kFormatUnsupported &&
                   TerrainSteepStatus(sm, MaterialStatus::kTextureBad, true) == MaterialStatus::kTextureBad &&
                   TerrainSteepStatus(sm, ok, false) == MaterialStatus::kUvUnsupported);
    CHECK(264, TerrainTextured(MaterialKind::kTerrainLayer) == kTexturedTerrainLayer &&
                   TerrainTextured(MaterialKind::kTerrainFar) == kTexturedTerrainFar &&
                   TerrainTextured(MaterialKind::kTerrainFarFade) == kTexturedTerrainFar);
    CHECK(265, std::string(capture::MaterialStatusName(MaterialStatus::kTerrainInputs)) == "terrain-inputs" &&
                   std::string(capture::MaterialStatusName(MaterialStatus::kTerrainSquare)) == "terrain-square");

    // 266-279: ground squares and the all-or-nothing fallback of a square (Review Focus 5).
    auto patch_of = [](float ox, float oy, float first, uint32_t patches, float cols, float cell) {
      capture::TerrainPatch t;
      t.active = true;
      t.patch = first;
      t.patches = patches;
      t.cols = cols;
      t.inv_cols = 1.0f / cols;
      t.cell[0] = t.cell[1] = cell;
      t.origin[0] = ox;
      t.origin[1] = oy;
      return t;
    };
    // Tile (80, 144) of the layer builder, 2 x 2 sub-patches of 8 units: A is sub-patch 0, B is 1.
    const capture::TerrainPatch a = patch_of(80, 144, 0, 1, 2, 8), b = patch_of(80, 144, 1, 1, 2, 8);
    const capture::TerrainPatch a3 = patch_of(80, 144, 3, 1, 2, 8);
    const capture::TerrainPatch ab = patch_of(80, 144, 0, 2, 2, 8);    // one layer draw over A and B
    const capture::TerrainPatch out = patch_of(112, 144, 0, 1, 2, 8);  // a square the underlay does not reach
    // An adaptive draw of 12 patches of a region at (48, 96), 6 columns: rows 6 and 7, x 48 to 96,
    // y 144 to 160: it lies under the whole tile.
    const capture::TerrainPatch under = patch_of(48, 96, 36, 12, 6, 8);
    const capture::TerrainPatch away = patch_of(512, 0, 0, 6, 6, 8);
    const capture::TerrainPatch coarse = patch_of(64, 128, 0, 1, 1, 32);  // one 32-unit patch, x 64 to 96, y 128 to 160
    CHECK(266, TerrainPatchRect(a, 0) == (TerrainRect{80, 144, 8, 8}) && TerrainPatchRect(a3, 0) == (TerrainRect{88, 152, 8, 8}) &&
                   TerrainPatchRect(ab, 1) == TerrainPatchRect(b, 0));
    CHECK(267, TerrainPatchRect(under, 4) == TerrainPatchRect(a, 0) && TerrainPatchRect(under, 11) == TerrainPatchRect(a3, 0) &&
                   TerrainPatchRect(coarse, 0) == (TerrainRect{64, 128, 32, 32}));
    auto item = [](const capture::TerrainPatch& t, bool layer, bool usable = true, bool drawable = true) {
      TerrainPlanItem i;
      i.patch = &t;
      i.layer = layer;
      i.usable = usable;
      i.drawable = drawable;
      return i;
    };
    // The actions in draw order: M with the material, F as flat clay, S left out.
    auto plan = [](std::vector<TerrainPlanItem> items) {
      PlanTerrain(items);
      std::string s;
      for (const TerrainPlanItem& i : items) {
        s += i.action == TerrainAction::kMaterial ? 'M' : i.action == TerrainAction::kFlat ? 'F' : 'S';
      }
      return s;
    };
    const bool L = true, F = false;
    // Everything usable: every draw with its material.
    CHECK(268, plan({item(under, F), item(away, F), item(a, L), item(b, L), item(a, L), item(b, L), item(a, L)}) ==
                   "MMMMMMM");
    // One layer of A cannot be drawn with the material (say its texture is pending): A is one flat
    // draw, its first, and its other layers are left out; B, the underlay and the rest keep theirs.
    CHECK(269, plan({item(under, F), item(away, F), item(a, L), item(b, L), item(a, L, false), item(b, L), item(a, L)}) ==
                   "MMFMSMS");
    // Whichever layer of the square it is.
    CHECK(270, plan({item(under, F), item(a, L, false), item(a, L), item(a, L)}) == "MFSS" &&
                   plan({item(under, F), item(a, L), item(a, L), item(a, L, false)}) == "MFSS");
    // An underlay that cannot be drawn with the material is the one flat draw of every square it
    // covers: all their layers are left out. A square outside it and a far draw elsewhere keep theirs.
    CHECK(271, plan({item(under, F, false), item(away, F), item(a, L), item(b, L), item(a, L), item(out, L)}) ==
                   "FMSSSM");
    // A far draw without a usable material that lies under no square is flat by itself.
    CHECK(272, plan({item(under, F), item(away, F, false), item(a, L)}) == "MFM");
    // A square without an underlay (its first layer replaces): the same rule.
    CHECK(273, plan({item(a, L), item(a, L, false), item(a, L)}) == "FSS" && plan({item(a, L), item(a, L), item(a, L)}) == "MMM");
    // A layer whose geometry could not be built is no flat draw: the square's next layer is.
    CHECK(274, plan({item(a, L, true, false), item(a, L), item(a, L)}) == "SFS");
    CHECK(275, plan({item(a, L, true, false), item(a, L, false, false)}) == "SS");
    // An underlay without geometry draws nothing: its squares are flat by their first layers.
    CHECK(276, plan({item(under, F, true, false), item(a, L), item(a, L), item(out, L)}) == "SFSM");
    // A layer draw over two squares ties them together: when B falls back, A does too, and no square
    // gets a layer with the material over flat clay (A is covered by two opaque flat draws here).
    CHECK(277, plan({item(a, L), item(ab, L), item(b, L, false), item(b, L)}) == "FFSS" &&
                   plan({item(a, L), item(ab, L), item(b, L), item(b, L)}) == "MMMM");
    CHECK(278, plan({}).empty());
    // A record with more patches than a grid is built for (kMaxTerrainPatches; the capture admits
    // counts up to 4,194,304) has no geometry and no squares: it is not walked. As a layer it
    // makes no square fall back.
    const capture::TerrainPatch huge = patch_of(80, 144, 0, 4194304, 2, 8);
    const capture::TerrainPatch over = patch_of(80, 144, 0, capture::kMaxTerrainPatches + 1, 2, 8);
    const capture::TerrainPatch most = patch_of(80, 144, 0, capture::kMaxTerrainPatches, 2, 8);
    CHECK(297, capture::kMaxTerrainPatches == 4096 && plan({item(huge, L, false, false), item(a, L)}) == "SM" &&
                   plan({item(over, L, false, false), item(a, L)}) == "SM");
    // As an underlay it cannot say which squares it lies under, and it draws nothing: every square
    // of the frame is bad, as the squares over an underlay without geometry are (276), also one a
    // grid of its first patches would not have reached (`out`). Other far draws keep their own state.
    CHECK(319, plan({item(huge, F, false, false), item(a, L)}) == "SF" &&
                   plan({item(over, F, false, false), item(a, L)}) == "SF" &&
                   plan({item(over, F, false, false), item(away, F), item(a, L), item(a, L), item(out, L), item(b, L, true, false)}) ==
                       "SMFSFS" &&
                   plan({item(a, L), item(over, F, true, false), item(out, L)}) == "FSF");
    // (No geometry is built above the bound, so such a record is never drawable; were it, the rule
    // would be the same: it is no underlay to add to.)
    CHECK(333, plan({item(over, F), item(a, L)}) == "MF");
    // At the bound the record still has its squares (A is its first patch).
    CHECK(298, plan({item(most, L, false), item(a, L)}) == "FS" && plan({item(most, F, false), item(a, L)}) == "FS");
    // The plan's storage can be kept between frames: a second plan in the same scratch object
    // sees nothing of the first.
    {
      TerrainPlanScratch scratch;
      std::vector<TerrainPlanItem> one = {item(under, F, false), item(a, L), item(b, L)};
      PlanTerrain(one, &scratch);
      std::vector<TerrainPlanItem> two = {item(a, L), item(b, L), item(a, L)};
      PlanTerrain(two, &scratch);
      std::vector<TerrainPlanItem> three = {item(a, L), item(a, L, false), item(out, L)};
      PlanTerrain(three, &scratch);
      CHECK(299, one[0].action == TerrainAction::kFlat && one[1].action == TerrainAction::kSkip &&
                     two[0].action == TerrainAction::kMaterial && two[1].action == TerrainAction::kMaterial &&
                     two[2].action == TerrainAction::kMaterial && three[0].action == TerrainAction::kFlat &&
                     three[1].action == TerrainAction::kSkip && three[2].action == TerrainAction::kMaterial);
    }
    // The underlay of a square is found by the square's centre: a coarser patch covers the 8-unit
    // squares inside it and no square beside it.
    CHECK(279, plan({item(coarse, F, false), item(a, L), item(a3, L), item(out, L)}) == "FSSM");
    // 320-325: the work budget. Linking a layer patch to its square costs kTerrainPlanLinkCost steps,
    // testing one square against one patch of an underlay without its material one step; a frame
    // that would take more than kTerrainPlanBudget steps is not walked: every square is bad (each
    // layer that has geometry is flat clay, the far draws keep their own state). A real frame is
    // far below it: the bridge has 492 layer records of one patch (31,488 steps) and, were all its
    // 75 far records (1,606 patches) without material under its about 100 squares, about 160,000 more.
    CHECK(320, kTerrainPlanBudget == 1048576 && kTerrainPlanLinkCost == 64 &&
                   492 * kTerrainPlanLinkCost + 1606 * 100 < kTerrainPlanBudget / 5);
    {
      // 16,384 layer patches are the most a frame can link (4 records of 4096): walked as before.
      // `row` is 4096 squares in one row, x 0 to 32768; `one` is a square away from it.
      const capture::TerrainPatch row = patch_of(0, 0, 0, 4096, 4096, 8);
      const capture::TerrainPatch one = patch_of(0, 512, 0, 1, 1, 8);
      TerrainPlanScratch scratch;
      std::vector<TerrainPlanItem> at(4, item(row, L));
      PlanTerrain(at, &scratch);
      CHECK(321, scratch.squares.size() == 4096 && at[0].action == TerrainAction::kMaterial &&
                     at[3].action == TerrainAction::kMaterial);
      // One patch more: nothing is walked (no square is made), every layer is flat or, without
      // geometry, left out; the far draws are what they are by themselves.
      std::vector<TerrainPlanItem> more(4, item(row, L));
      more.push_back(item(one, L));
      more.push_back(item(one, L, true, false));
      more.push_back(item(away, F));
      more.push_back(item(away, F, false));
      more.push_back(item(away, F, true, false));
      PlanTerrain(more, &scratch);
      CHECK(322, scratch.squares.empty() && scratch.link.empty());
      CHECK(323, more[0].action == TerrainAction::kFlat && more[3].action == TerrainAction::kFlat &&
                     more[4].action == TerrainAction::kFlat && more[5].action == TerrainAction::kSkip &&
                     more[6].action == TerrainAction::kMaterial && more[7].action == TerrainAction::kFlat &&
                     more[8].action == TerrainAction::kSkip);
      // The underlay's part: 4096 squares (a row of 4095 and `one`) cost 262,144 steps to link; an
      // underlay without material of 192 patches under the row's first squares tests 786,432
      // square-patch pairs: 1,048,576 steps, the budget exactly, walked. The row's one layer draw
      // ties its squares together and is the flat draw of those the underlay does not cover; `one`
      // lies under no underlay and keeps its material.
      const capture::TerrainPatch row4095 = patch_of(0, 0, 0, 4095, 4096, 8);
      const capture::TerrainPatch under192 = patch_of(0, 0, 0, 192, 4096, 8), under193 = patch_of(0, 0, 0, 193, 4096, 8);
      std::vector<TerrainPlanItem> fits = {item(under192, F, false), item(row4095, L), item(one, L)};
      PlanTerrain(fits, &scratch);
      CHECK(324, scratch.squares.size() == 4096 && fits[0].action == TerrainAction::kFlat &&
                     fits[1].action == TerrainAction::kFlat && fits[2].action == TerrainAction::kMaterial);
      // One patch more is over it: the frame is not planned by squares, every layer is flat.
      std::vector<TerrainPlanItem> not_fits = {item(under193, F, false), item(row4095, L), item(one, L)};
      PlanTerrain(not_fits, &scratch);
      CHECK(325, not_fits[0].action == TerrainAction::kFlat && not_fits[1].action == TerrainAction::kFlat &&
                     not_fits[2].action == TerrainAction::kFlat);
      // An underlay with its material costs nothing: the same frame with it usable is planned.
      std::vector<TerrainPlanItem> usable = {item(under193, F), item(row4095, L), item(one, L)};
      PlanTerrain(usable, &scratch);
      CHECK(326, usable[0].action == TerrainAction::kMaterial && usable[1].action == TerrainAction::kMaterial &&
                     usable[2].action == TerrainAction::kMaterial);
    }
    // 313-315: a layer with the steep-ground block whose side texture is not usable this frame, or whose
    // normals could not be built (TerrainSteepStatus), is a layer without its material: its square is
    // one flat draw, its other layers are left out, and the neighbouring square keeps its material.
    {
      const bool pending = TerrainSteepStatus(sm, MaterialStatus::kTexturePending, true) == ok;
      CHECK(313, !pending && plan({item(under, F), item(a, L), item(a, L, pending), item(b, L), item(a, L)}) == "MFSMS");
      const bool no_normals = TerrainSteepStatus(sm, ok, false) == ok;
      CHECK(314, !no_normals && plan({item(a, L, no_normals), item(a, L), item(b, L)}) == "FSM");
      CHECK(315, plan({item(a, L, TerrainSteepStatus(sm, ok, true) == ok), item(a, L)}) == "MM");
    }

    // 280-286: F3 and log text with terrain drawn with its material.
    TextureStats ts;
    ts.textured = 907;
    ts.status[size_t(MaterialStatus::kTextured)] = 907;
    ts.status[size_t(MaterialStatus::kNoAlbedo)] = 13;
    ts.status[size_t(MaterialStatus::kTerrainSquare)] = 4;
    ts.status[size_t(MaterialStatus::kTexturePending)] = 3;
    ts.terrain = 567;
    ts.terrain_textured = 560;
    // 907 of 927 drawn; without the 567 terrain records 347 of 360.
    const std::string t = FormatTextureText(ts, 927);
    CHECK(280, t.find("textured 907 of 927 drawn (97%, 96% non-terrain)") != std::string::npos);
    CHECK(281, t.find("top untextured: no-albedo 13, terrain-square 4, texture-pending 3") != std::string::npos);
    TextureStats only;
    only.textured = 5;
    only.terrain = 5;
    only.terrain_textured = 5;
    CHECK(282, FormatTextureText(only, 5).find("textured 5 of 5 drawn (100%, 0% non-terrain)") != std::string::npos);
    TextureStats inputs;
    inputs.status[size_t(MaterialStatus::kTerrainInputs)] = 9;
    inputs.terrain = 9;
    CHECK(283, FormatTextureText(inputs, 9).find("top untextured: terrain-inputs 9") != std::string::npos);
    // Layers left out with their square are skips of the Native line.
    FrameScene s;
    s.captured = 600;
    ClayStats st;
    st.drawn = 560;
    st.terrain_skipped = 7;
    CHECK(284, FormatStatusText(s, st, nullptr).find("skipped 7 (top: terrain-square 7)") != std::string::npos);
    st.terrain_tuples = 78;
    CHECK(285, FormatTerrainText(st, ts) ==
                   "Terrain: material 560 of 567 drawn, flat 7, left out 7, texture sets 78");
    // Every terrain record that could not have its material is named with its own reason, also
    // when it was left out or its square's flat draw is another record (those show as
    // terrain-square elsewhere): most first.
    st.terrain_unusable[size_t(MaterialStatus::kTexturePending)] = 2;
    st.terrain_unusable[size_t(MaterialStatus::kTerrainInputs)] = 3;
    CHECK(300, FormatTerrainText(st, ts) ==
                   "Terrain: material 560 of 567 drawn, flat 7, left out 7, texture sets 78 | "
                   "without material: terrain-inputs 3, texture-pending 2");
    // A record whose geometry could not be built makes its square fall back as well: it is named
    // by what was missing, its positions (the heightmap or the upload) or its indices, in the
    // same list (equal counts: the material reasons first).
    st.terrain_no_positions = 4;
    st.terrain_no_indices = 2;
    CHECK(327, FormatTerrainText(st, ts) ==
                   "Terrain: material 560 of 567 drawn, flat 7, left out 7, texture sets 78 | "
                   "without material: no-positions 4, terrain-inputs 3, texture-pending 2, no-indices 2");
    {
      ClayStats geo;
      geo.terrain_no_indices = 1;
      CHECK(328, FormatTerrainText(geo, TextureStats{}) ==
                     "Terrain: material 0 of 0 drawn, flat 0, left out 0, texture sets 0 | without material: no-indices 1");
    }
    CHECK(286, FormatTerrainText(ClayStats{}, TextureStats{}) ==
                   "Terrain: material 0 of 0 drawn, flat 0, left out 0, texture sets 0");

    // 287-292: the cache key of a far draw's colour coordinates (built from the two tables).
    static const uint32_t kTableU[6] = {0x07004802, 0x1B9D6058, 0x0005E32F, 0x01001400, 0x00000000, 0x00000218};
    static const uint32_t kTableV[6] = {0x07004802, 0x1B9EC058, 0x0005E32F, 0x01001400, 0x00000000, 0x00000218};
    const capture::DrawRecord rec = BaseRecord();  // (the position and index keys take the record too)
    capture::TerrainPatch r = under;
    r.map.phys_addr = 0x1B770000;
    r.map.size = 0x138C00;
    r.material = fm;
    std::memcpy(r.material.far_u_fetch, kTableU, sizeof(kTableU));
    std::memcpy(r.material.far_v_fetch, kTableV, sizeof(kTableV));
    r.material.far_table[0] = 16.0f / 17;
    r.material.far_table[1] = 0.5f / 17;
    r.material.far_table[2] = r.material.far_table[3] = 1.0f / 48;
    r.material.far_patch[0] = 12;
    r.material.far_patch[1] = 18;
    const GeoKey k = TerrainFarUvKey(r);
    CHECK(287, k.kind == 7 && k.addr == 0x1B9D6000 && k.stride == capture::kTerrainGrid && !(k == PositionKey(rec, &r)) &&
                   !(k == IndexKey(rec, &r)) && TerrainFarUvKey(r) == k);
    capture::TerrainPatch v = r;
    v.patch = 37;
    CHECK(288, !(TerrainFarUvKey(v) == k));
    v = r;
    v.patches = 6;
    CHECK(289, !(TerrainFarUvKey(v) == k));
    v = r;
    v.material.far_patch[1] = 12;
    capture::TerrainPatch w = r;
    w.material.far_table[0] = 0.5f;
    CHECK(290, !(TerrainFarUvKey(v) == k) && !(TerrainFarUvKey(w) == k));
    v = r;
    v.material.far_v_fetch[1] = 0x1B9ED058;  // another v table
    w = r;
    w.material.far_u_fetch[2] = 0x0005E32E;  // another size
    CHECK(291, !(TerrainFarUvKey(v) == k) && !(TerrainFarUvKey(w) == k));
    v = r;
    v.material.far_v_fetch[2] = 0x0005E32E;  // the v table's size
    w = r;
    w.material.far_u_fetch[0] = 0x06C04802;  // the u table's pitch
    CHECK(293, !(TerrainFarUvKey(v) == k) && !(TerrainFarUvKey(w) == k));
    // What changes every frame without changing the coordinates is not in the key.
    v = r;
    v.material.eye[0] += 1.0f;
    v.material.fade[1] = 0.0f;
    v.height_scale = 2.0f;
    CHECK(292, TerrainFarUvKey(v) == k);

    // 307-312: the cache key of a steep layer's vertex normals (built from the normal map at the
    // grid points): the map (fetch dwords 0 to 2), the draw's patches and what places a grid point on
    // the map. What the normals do not depend on stays out: the height scale, the layer's textures
    // and constants (every layer of a square shares one buffer), the eye.
    static const uint32_t kTf17[6] = {0x87004802, 0x1B8AA071, 0x00600300, 0x01000D10, 0x00000000, 0x00000218};
    capture::TerrainPatch n = under;
    n.map.phys_addr = 0x1B770000;
    n.map.size = 0x138C00;
    n.material = sm;
    std::memcpy(n.material.normal_fetch, kTf17, sizeof(kTf17));
    const GeoKey nk = TerrainNormalKey(n);
    CHECK(307, nk.kind == 8 && nk.addr == 0x1B8AA000 && nk.stride == capture::kTerrainGrid && !(nk == PositionKey(rec, &n)) &&
                   !(nk == IndexKey(rec, &n)) && !(nk == TerrainFarUvKey(n)) && TerrainNormalKey(n) == nk);
    v = n;
    v.material.normal_fetch[1] = 0x1B8AB071;  // another map
    w = n;
    w.material.normal_fetch[2] = 0x00600301;  // another size
    CHECK(308, !(TerrainNormalKey(v) == nk) && !(TerrainNormalKey(w) == nk));
    v = n;
    v.material.normal_fetch[1] = 0x1B8AA031;  // the same address read with another byte order
    CHECK(318, !(TerrainNormalKey(v) == nk) && TerrainNormalKey(v).addr == nk.addr);
    v = n;
    v.material.normal_fetch[0] = 0x86C04802;  // another pitch
    w = n;
    w.material.normal_fetch[0] = 0x87000002;  // other clamp modes
    CHECK(309, !(TerrainNormalKey(v) == nk) && !(TerrainNormalKey(w) == nk));
    v = n;
    v.patch += 1.0f;
    w = n;
    w.patches += 1;
    CHECK(310, !(TerrainNormalKey(v) == nk) && !(TerrainNormalKey(w) == nk));
    {
      // Each number that places a grid point on the map.
      bool all = true;
      for (int field = 0; field < 10; ++field) {
        capture::TerrainPatch x = n;
        float* at[10] = {&x.cols, &x.inv_cols, &x.cell[0], &x.cell[1],
                         &x.origin[0], &x.origin[1], &x.tex_offset[0], &x.tex_offset[1],
                         &x.tex_scale[0], &x.tex_scale[1]};
        *at[field] += 0.25f;
        all = all && !(TerrainNormalKey(x) == nk);
      }
      CHECK(311, all);
    }
    v = n;
    v.height_scale = 2.0f;
    v.material.eye[0] += 1.0f;
    v.material.detail_fetch[1] ^= 0x1000u;   // another layer of the same square
    v.material.side_fetch[1] ^= 0x1000u;
    v.material.weight_uv[2] += 0.5f;
    v.material.slope[0] = 0.0f;
    v.map.phys_addr = 0x1B780000;            // (the heights are the position buffer's)
    CHECK(312, TerrainNormalKey(v) == nk);
    // The same buffer serves a layer without the block and a draw of the adaptive builder that
    // has the same patches (the key has nothing of the block).
    v = n;
    v.material.steep = false;
    CHECK(340, TerrainNormalKey(v) == nk);
    v.material.adaptive = true;
    CHECK(341, TerrainNormalKey(v) == nk);
  }

  // 350-369: normal and colour keys (normal_decode.h; frame-map section 14, "Normals").
  {
    capture::DrawRecord r = BaseRecord();
    capture::LightInputs l;
    l.family = capture::LightFamily::kObject;
    l.kind = capture::LightVertexKind::kStream;
    l.normal.vb = r.vb;
    l.normal.offset_bytes = 8;
    l.normal.stride_bytes = 20;
    l.normal.source = 0x24;  // source x, y, z
    l.normal.source_count = 3;
    l.normal.format = uint8_t(capture::kVertexFormat_10_11_11);
    l.normal.flags = 11;
    l.second.vb = {0x9000, 400};
    l.second.stride_bytes = 4;
    l.second.source = 0xC6;  // source z, y, x, w
    l.second.source_count = 4;
    l.second.format = uint8_t(capture::kVertexFormat_8_8_8_8);
    l.second.flags = 10;
    const GeoKey k = NormalKey(r, l);
    CHECK(350, k.kind == 9 && k == MeshNormalKey(l) && k.addr == r.vb.phys_addr && k.size == r.vb.size && k.stride == 20);
    // Its own kind: never the positions', the UVs' or the indices' entry of the same stream.
    CHECK(351, !(k == PositionKey(r, nullptr)) && !(k == UvKey(r)) && !(k == IndexKey(r, nullptr)) && k.kind != PositionKey(r, nullptr).kind);
    // What changes the decode changes the key: the stream, where the element lies, its format.
    capture::LightInputs a = l;
    a.normal.vb.phys_addr = 0x2400;
    CHECK(352, !(NormalKey(r, a) == k));
    a = l;
    a.normal.vb.size = 1620;
    CHECK(353, !(NormalKey(r, a) == k));
    a = l;
    a.normal.offset_bytes = 12;
    CHECK(354, !(NormalKey(r, a) == k));
    a = l;
    a.normal.stride_bytes = 28;
    CHECK(355, !(NormalKey(r, a) == k));
    a = l;
    a.normal.format = uint8_t(capture::kVertexFormat_16_16_16_16_FLOAT);
    CHECK(356, !(NormalKey(r, a) == k));
    // The source components the reader takes: another selector is another decode of the same bytes.
    a = l;
    a.normal.source = 0x21;  // source y, x, z
    CHECK(376, !(NormalKey(r, a) == k));
    a = l;
    a.normal.source_count = 4;
    CHECK(377, !(NormalKey(r, a) == k));
    // Nothing of the draw: the mesh's normals are one entry however it is placed, turned, lit or
    // coloured, and whatever shader fetches them (the flags only admit the element, and where a
    // shader's fetch puts the components in its register is not in the element).
    a = l;
    a.world_rot[0][0] = 0.5f;
    a.world_rot[1][2] = -0.5f;
    a.world_scale = 2.0f;
    a.c27w = 0.25f;
    a.c49[1] = 3.0f;
    a.colour_first = 77;
    a.family = capture::LightFamily::kTreeTrunk;
    a.kind = capture::LightVertexKind::kProbe;
    a.multi = true;
    a.vs_const[3][2] = 9.0f;
    a.second.vb.phys_addr = 0xA000;
    a.normal.flags = 9;
    capture::DrawRecord moved = r;
    moved.rows[3] = 12.0f;
    moved.seq = 99;
    moved.start = 60;
    moved.count = 30;
    moved.vs_hash = 0x1111;
    moved.material.ps_hash = 0x2222;
    CHECK(357, NormalKey(moved, a) == k);
    // A skinned stream: the bone layout, the palette and the stream the bone words come from.
    capture::DrawRecord s = r;
    s.skin.active = true;
    s.skin.palette_addr = 0x3000;
    s.skin.palette_size = 168;
    s.skin.bone_stride = 24;
    const GeoKey sk = NormalKey(s, l);
    CHECK(358, sk.kind == 9 && !(sk == k) && !(sk == PositionKey(s, nullptr)));
    capture::DrawRecord s2 = s;
    s2.skin.palette_addr = 0x4000;
    CHECK(359, !(NormalKey(s2, l) == sk));
    s2 = s;
    s2.skin.weight_shift[1] = 8;
    CHECK(360, !(NormalKey(s2, l) == sk));
    s2 = s;
    s2.vb.phys_addr = 0x2800;  // (the normal's stream as it was: the bone words are another stream's)
    CHECK(361, !(NormalKey(s2, l) == sk));
    s2 = s;
    s2.pos.stride_bytes = 32;
    CHECK(362, !(NormalKey(s2, l) == sk));
    // The same skin fields change the position key (one list for both: SkinHash).
    s2 = s;
    s2.skin.index_shift[2] = 16;
    CHECK(363, !(NormalKey(s2, l) == sk) && !(PositionKey(s2, nullptr) == PositionKey(s, nullptr)) &&
                   SkinHash(s2.skin) != SkinHash(s.skin));
    // An instanced draw: its instance set, but not the offset, which moves positions only.
    capture::DrawRecord i = r;
    i.instances.active = true;
    i.instances.rows_addr = 0x5000;
    i.instances.rows_size = 280;
    i.instances.inv_count = 0.25f;
    i.instances.count = 4.0f;
    i.instances.bias = 0.5f;
    i.instances.flat_count = 40;
    const GeoKey ik = NormalKey(i, l);
    CHECK(364, ik.kind == 10 && !(ik == k) && !(ik == PositionKey(i, nullptr)) && !(ik == UvKey(i)));
    capture::DrawRecord i2 = i;
    i2.instances.offset[1] = 55.0f;
    CHECK(365, NormalKey(i2, l) == ik && !(PositionKey(i2, nullptr) == PositionKey(i, nullptr)));
    for (int field = 0; field < 7; ++field) {
      i2 = i;
      switch (field) {
        case 0: i2.instances.rows_addr = 0x6000; break;
        case 1: i2.instances.rows_size = 308; break;
        case 2: i2.instances.rows[1].offset_bytes = 8; break;
        case 3: i2.instances.inv_count = 0.2f; break;
        case 4: i2.instances.count = 5.0f; break;
        case 5: i2.instances.first = 2.0f; break;
        default: i2.instances.flat_count = 44; break;
      }
      CHECK(366, !(NormalKey(i2, l) == ik));
    }
    i2 = i;
    i2.instances.bias = 0.25f;
    CHECK(367, !(NormalKey(i2, l) == ik));

    // The colour buffer: the stream, where the element lies, the draw's offset into it, the count.
    const GeoKey ck = ColourKey(l, 80);
    CHECK(368, ck.kind == 11 && ck.addr == 0x9000 && ck.size == 400 && ck.stride == 4 && !(ck == k));
    a = l;
    a.colour_first = 20;  // another mesh's colours in a shared stream
    CHECK(369, !(ColourKey(a, 80) == ck) && !(ColourKey(l, 81) == ck));
    a = l;
    a.second.vb.phys_addr = 0xA000;  // a mesh drawn with two colour streams is two entries
    CHECK(370, !(ColourKey(a, 80) == ck));
    a = l;
    a.second.vb.size = 404;
    CHECK(371, !(ColourKey(a, 80) == ck));
    a = l;
    a.second.stride_bytes = 8;
    CHECK(372, !(ColourKey(a, 80) == ck));
    {
      capture::LightInputs b = a;
      b.second.offset_bytes = 4;  // the same stream and stride, the element elsewhere in the vertex
      CHECK(374, !(ColourKey(b, 80) == ColourKey(a, 80)));
      b = a;
      b.second.source = 0xE4;  // the same element taken in memory order: other colours
      CHECK(378, !(ColourKey(b, 80) == ColourKey(a, 80)));
      b = a;
      b.second.source_count = 3;
      CHECK(379, !(ColourKey(b, 80) == ColourKey(a, 80)));
    }
    // Nothing else of the draw: the normal, the rotation, the power the shader raises it to.
    a = l;
    a.normal.vb.phys_addr = 0x7000;
    a.world_rot[2][2] = -1.0f;
    a.colour_power = 2.2f;
    a.c26[0] = 0.5f;
    CHECK(373, ColourKey(a, 80) == ck);

    // The probe buffer (the four weights and the set index of every vertex of a mesh): the weights'
    // stream and element, and the position stream and what places the set index in a vertex.
    capture::LightInputs pl = l;
    pl.family = capture::LightFamily::kTreeTrunk;
    pl.kind = capture::LightVertexKind::kProbe;
    pl.a0_comp = 2;
    pl.second.vb = {0xB000, 1280};
    pl.second.offset_bytes = 0;
    pl.second.stride_bytes = 16;
    pl.second.source = 0xE4;  // source x, y, z, w
    pl.second.source_count = 4;
    pl.second.format = uint8_t(capture::kVertexFormat_32_32_32_32_FLOAT);
    pl.second.flags = 9;
    const GeoKey pk = ProbeKey(r, pl);
    CHECK(335, pk.kind == 12 && pk.addr == 0xB000 && pk.size == 1280 && pk.stride == 16 && !(pk == k) && !(pk == ck) &&
                   !(pk == PositionKey(r, nullptr)));
    for (int field = 0; field < 13; ++field) {
      capture::LightInputs b = pl;
      capture::DrawRecord q = r;
      switch (field) {
        case 0: b.second.vb.phys_addr = 0xC000; break;
        case 1: b.second.vb.size = 1296; break;
        case 2: b.second.stride_bytes = 20; break;
        case 3: b.second.offset_bytes = 4; break;
        case 4: b.second.source = 0x4B; break;  // the register order of the skinned shaders' fetch
        case 5: b.second.source_count = 3; break;
        case 6: b.second.format = uint8_t(capture::kVertexFormat_16_16_16_16_FLOAT); break;
        case 7: b.a0_comp = 3; break;
        case 8: q.vb.phys_addr = 0x2800; break;
        case 9: q.vb.size += 20; break;
        case 10: q.pos.stride_bytes += 4; break;
        case 11: q.pos.offset_bytes += 4; break;
        default: q.pos.swap16 = !q.pos.swap16; break;
      }
      CHECK(336, !(ProbeKey(q, b) == pk));
    }
    {
      capture::DrawRecord q = r;
      q.pos.format = q.pos.format == capture::PosFormat::kHalf4 ? capture::PosFormat::kShort4 : capture::PosFormat::kHalf4;
      CHECK(337, !(ProbeKey(q, pl) == pk));
    }
    // Nothing else of the draw: not the probe constants (the draw's), the rotation, the normal, the
    // pixel constants, where the fetch puts the position in its register; and not the bones: the
    // weights are the bind mesh's, one entry whatever the palette holds.
    {
      capture::LightInputs b = pl;
      capture::DrawRecord q = r;
      b.vs_const[0][0] = 0.25f;
      b.vs_const[11][3] = -3.0f;
      b.world_rot[1][1] = 0.5f;
      b.world_scale = 1.5f;
      b.c27w = 0.5f;
      b.c49[2] = 2.0f;
      b.multi = true;
      b.family = capture::LightFamily::kObject;
      b.normal.vb.phys_addr = 0x7000;
      b.normal.offset_bytes = 12;
      b.second.flags = 8;
      q.pos.swizzle = 0x443;
      q.rows[5] = 3.0f;
      q.seq = 12;
      q.start = 9;
      q.count = 33;
      q.vs_hash = 0x1111;
      q.material.ps_hash = 0x2222;
      q.skin.active = true;
      q.skin.palette_addr = 0x3000;
      q.skin.palette_size = 168;
      q.skin.bone_stride = 24;
      CHECK(338, ProbeKey(q, b) == pk);
    }

    // An instanced draw's flat colours: the instance stream's colour element and the instance set
    // as the flat normals have it (not the offset, which moves positions only).
    capture::LightInputs il = l;
    il.family = capture::LightFamily::kPlant;
    il.kind = capture::LightVertexKind::kInstance;
    il.second.vb = {0x5000, 280};
    il.second.offset_bytes = 24;
    il.second.stride_bytes = 28;
    il.second.source = 0x06;  // source z, y, x
    il.second.source_count = 3;
    il.second.format = uint8_t(capture::kVertexFormat_8_8_8_8);
    il.second.flags = 10;
    const GeoKey ick = InstanceColourKey(i, il);
    CHECK(339, ick.kind == 13 && ick.addr == 0x5000 && ick.size == 280 && ick.stride == 28 && !(ick == ik) && !(ick == pk) &&
                   !(ick == PositionKey(i, nullptr)) && !(ick == UvKey(i)) && !(ick == ColourKey(il, 40)));
    i2 = i;
    i2.instances.offset[2] = -9.0f;
    CHECK(342, InstanceColourKey(i2, il) == ick && !(PositionKey(i2, nullptr) == PositionKey(i, nullptr)));
    for (int field = 0; field < 8; ++field) {
      i2 = i;
      switch (field) {
        case 0: i2.instances.rows_addr = 0x6000; break;
        case 1: i2.instances.rows_size = 308; break;
        case 2: i2.instances.rows[1].offset_bytes = 8; break;
        case 3: i2.instances.inv_count = 0.2f; break;
        case 4: i2.instances.count = 5.0f; break;
        case 5: i2.instances.first = 2.0f; break;
        case 6: i2.instances.bias = 0.25f; break;
        default: i2.instances.flat_count = 44; break;
      }
      // (the same fields as the flat normals' key: an entry of each per instance set)
      CHECK(343, !(InstanceColourKey(i2, il) == ick) && !(NormalKey(i2, l) == ik));
    }
    for (int field = 0; field < 7; ++field) {
      capture::LightInputs b = il;
      switch (field) {
        case 0: b.second.vb.phys_addr = 0x6000; break;
        case 1: b.second.vb.size = 308; break;
        case 2: b.second.stride_bytes = 32; break;
        case 3: b.second.offset_bytes = 20; break;
        case 4: b.second.source = 0x24; break;  // memory order: other colours of the same bytes
        case 5: b.second.source_count = 4; break;
        default: b.second.format = uint8_t(capture::kVertexFormat_10_11_11); break;
      }
      CHECK(344, !(InstanceColourKey(i, b) == ick));
    }
    {
      // Nothing of the mesh or of the light's constants: the colour is the copy's.
      capture::LightInputs b = il;
      capture::DrawRecord q = i;
      b.normal.vb.phys_addr = 0x7000;
      b.normal.offset_bytes = 4;
      b.vs_const[0][1] = -200.0f;
      b.multi = true;
      b.family = capture::LightFamily::kPlantLit;
      q.vb.phys_addr = 0x2800;
      q.pos.stride_bytes += 4;
      q.rows[2] = 4.0f;
      q.vs_hash = 0x1111;
      CHECK(345, InstanceColourKey(q, b) == ick);
    }
  }

  // 380-389: the light line.
  {
    ClayStats st;
    CHECK(380, FormatLightText(st) == "Light inputs: ready 0 of 0 lit records | world rotation: rotation 0, mirrored 0, "
                                      "skewed 0, degenerate 0");
    st.lit = 873;
    st.light_ready = 870;
    st.light_rotation[size_t(RotationKind::kRotation)] = 172;
    st.light_rotation[size_t(RotationKind::kSkewed)] = 2;
    st.light_rotation_by_vs.Add(0xECD66A10092E6562ull, uint8_t(RotationKind::kSkewed));
    st.light_rotation_by_vs.Add(0xECD66A10092E6562ull, uint8_t(RotationKind::kSkewed));
    st.light_missing[size_t(capture::LightReason::kNormalUnsupported)] = 2;
    st.light_missing[size_t(capture::LightReason::kLightUnknown)] = 1;
    st.light_missing_by_vs.Add(0x79EAC49585797037ull, uint8_t(capture::LightReason::kNormalUnsupported));
    st.light_missing_by_vs.Add(0x79EAC49585797037ull, uint8_t(capture::LightReason::kNormalUnsupported));
    st.light_missing_by_vs.Add(0xECD66A10092E6562ull, uint8_t(capture::LightReason::kLightUnknown));
    CHECK(381, FormatLightText(st) ==
                   "Light inputs: ready 870 of 873 lit records | world rotation: rotation 172, mirrored 0, skewed 2, "
                   "degenerate 0 | skewed by vs {0xECD66A10092E6562: 2} | light-unknown 1 by vs {0xECD66A10092E6562: 1} | "
                   "normal-unsupported 2 by vs {0x79EAC49585797037: 2}");
    // Why a probe or an instance colour buffer was refused (those records are among light-unknown):
    // only the causes that occurred, in the enum's order; nothing when none did.
    ClayStats refused;
    refused.lit = 10;
    refused.light_ready = 4;
    refused.light_missing[size_t(capture::LightReason::kLightUnknown)] = 6;
    for (int n = 0; n < 6; ++n) refused.light_missing_by_vs.Add(0x475EC9F795E5EDBBull, uint8_t(capture::LightReason::kLightUnknown));
    refused.light_second[size_t(LightSecond::kProbeSet)] = 3;
    refused.light_second[size_t(LightSecond::kStream)] = 2;
    refused.light_second[size_t(LightSecond::kUpload)] = 1;
    CHECK(382, FormatLightText(refused) ==
                   "Light inputs: ready 4 of 10 lit records | world rotation: rotation 0, mirrored 0, skewed 0, degenerate 0 | "
                   "light-unknown 6 by vs {0x475EC9F795E5EDBB: 6} | probe or instance buffer refused: stream 2, probe-set 3, "
                   "upload 1");
    refused.light_second[size_t(LightSecond::kNone)] = 9;  // (never counted; never printed)
    refused.light_second[size_t(LightSecond::kElement)] = 1;
    refused.light_second[size_t(LightSecond::kValue)] = 4;
    CHECK(383, FormatLightText(refused).ends_with("| probe or instance buffer refused: element 1, stream 2, probe-set 3, value 4, "
                                                  "upload 1"));
    CHECK(384, FormatLightText(st).find("refused") == std::string::npos);
    // The probe buffer's weights are bounded like the light's constants.
    CHECK(385, kProbeWeightMax == kLightValueMax);
  }

  // 390-417: the linear target and the output mapping (frame-map section 14, "Linear target").
  {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    // display = sqrt(saturate(linear * E)).
    CHECK(390, OutputMap(0.0f, 1.0f) == 0.0f && OutputMap(0.25f, 1.0f) == 0.5f && OutputMap(1.0f, 1.0f) == 1.0f &&
                   OutputMap(4.0f, 1.0f) == 1.0f);
    CHECK(391, OutputMap(0.0625f, 4.0f) == 0.5f && OutputMap(0.25f, 0.25f) == 0.25f && OutputMap(0.5f, 8.0f) == 1.0f);
    // Nothing but a value in [0, 1] comes out: a negative value, a NaN and 0 * infinity give 0, +infinity 1.
    CHECK(392, OutputMap(-0.25f, 1.0f) == 0.0f && OutputMap(-inf, 1.0f) == 0.0f && OutputMap(nan, 1.0f) == 0.0f &&
                   OutputMap(1.0f, nan) == 0.0f && OutputMap(0.25f, -1.0f) == 0.0f);
    CHECK(393, OutputMap(inf, 1.0f) == 1.0f && OutputMap(inf, 0.0f) == 0.0f && OutputMap(0.0f, inf) == 0.0f &&
                   OutputMap(-0.0f, 1.0f) == 0.0f && !std::signbit(OutputMap(-0.0f, 1.0f)));

    // What a clay pixel shader writes for a display value: the value the 8-bit target held (clamped to
    // [0, 1]), squared; never a NaN, a negative value or more than 1.
    CHECK(394, ClayLinear(0.0f) == 0.0f && ClayLinear(0.5f) == 0.25f && ClayLinear(1.0f) == 1.0f);
    CHECK(395, ClayLinear(2.0f) == 1.0f && ClayLinear(inf) == 1.0f && ClayLinear(-0.5f) == 0.0f &&
                   ClayLinear(-inf) == 0.0f && ClayLinear(nan) == 0.0f);
    // The terrain shaders have the linear value itself and shade it: v * shade^2, clamped the same way.
    CHECK(396, ClayLinearShaded(0.25f, 0.5f) == 0.0625f && ClayLinearShaded(1.0f, 1.0f) == 1.0f &&
                   ClayLinearShaded(4.0f, 1.0f) == 1.0f && ClayLinearShaded(-1.0f, 1.0f) == 0.0f &&
                   ClayLinearShaded(nan, 1.0f) == 0.0f && ClayLinearShaded(1.0f, nan) == 0.0f &&
                   ClayLinearShaded(inf, 0.0f) == 0.0f);

    // The format (R11G11B10_FLOAT: red and green have 6 mantissa bits, blue 5; no sign, no alpha),
    // measured: each of the 256 levels the 8-bit target could hold, squared, stored in the channel,
    // mapped and stored in 8 bits again, for either way a GPU may round when it writes the target.
    // Red and green come back at most one level off; blue at most one level off when rounded to
    // nearest and at most three when rounded toward zero (two levels, 183 and 199). A half-float
    // channel (R16G16B16A16_FLOAT, 2 to 3 ms a frame more in one set of game runs, and not taken)
    // returns every level as itself.
    {
      const int bits[3] = {6, 5, 10};  // red and green; blue; a half-float channel
      int exact[3][2] = {}, worst[3][2] = {}, three_off[2] = {};
      for (int f = 0; f < 3; ++f) {
        for (int mode = 0; mode < 2; ++mode) {
          for (int k = 0; k < 256; ++k) {
            const float stored = StoredFloat(ClayLinear(float(k) / 255.0f), bits[f], mode == 0);
            const int off = std::abs(Level8(OutputMap(stored, 1.0f)) - k);
            exact[f][mode] += off == 0 ? 1 : 0;
            worst[f][mode] = std::max(worst[f][mode], off);
            if (f == 1 && off == 3) ++three_off[mode];
          }
        }
      }
      CHECK(397, worst[0][0] == 1 && worst[0][1] == 1 && worst[1][0] == 1 && worst[1][1] == 3 &&
                     three_off[0] == 0 && three_off[1] == 2);
      CHECK(398, exact[0][0] == 245 && exact[0][1] == 193 && exact[1][0] == 187 && exact[1][1] == 123 &&
                     exact[2][0] == 256 && exact[2][1] == 256 && worst[2][0] == 0 && worst[2][1] == 0);
      std::cout << "linear target, 8-bit levels that come back as themselves of 256 (rounded to nearest / toward zero), "
                << "largest error in levels: red and green (11-bit float) " << exact[0][0] << " / " << exact[0][1] << ", "
                << worst[0][0] << " / " << worst[0][1] << "; blue (10-bit float) " << exact[1][0] << " / " << exact[1][1]
                << ", " << worst[1][0] << " / " << worst[1][1] << "; a half-float channel " << exact[2][0] << " / "
                << exact[2][1] << ", " << worst[2][0] << " / " << worst[2][1] << "\n";
    }

    // The facet shade (0.35 + 0.65 f for flat clay, 0.75 + 0.25 f for a textured draw) multiplied the
    // display value in the 8-bit target, so the linear value is the square of that product: the mapping
    // then gives the product back. Exactly as real numbers; through the target's channels a shaded value
    // lands at most two levels (red, green) or three (blue) from where the 8-bit target had it, far
    // below the eight levels the comparison with the materials picture counts.
    {
      float exact = 0.0f;
      const int bits[2] = {6, 5};  // red and green; blue
      int total = 0, off[2][2][4] = {};  // [channel][rounding][levels off, 3 = three or more]
      int farthest = 0;
      for (int textured = 0; textured < 2; ++textured) {
        for (float facet : {0.0f, 0.3f, 0.77f, 1.0f}) {
          const float shade = textured ? 0.75f + 0.25f * facet : 0.35f + 0.65f * facet;
          for (int k = 0; k < 256; ++k) {
            const float c = float(k) / 255.0f;
            const float display = c * shade;  // what the shaders wrote before
            exact = std::max(exact, std::fabs(OutputMap(ClayLinear(display), 1.0f) - display));
            exact = std::max(exact, std::fabs(OutputMap(ClayLinearShaded(c * c, shade), 1.0f) - display));
            ++total;
            for (int f = 0; f < 2; ++f) {
              for (int mode = 0; mode < 2; ++mode) {
                const int was = Level8(display);
                const int is = Level8(OutputMap(StoredFloat(ClayLinear(display), bits[f], mode == 0), 1.0f));
                const int d = std::abs(was - is);
                ++off[f][mode][d < 3 ? d : 3];
                farthest = std::max(farthest, d);
              }
            }
          }
        }
      }
      CHECK(399, exact < 1e-6f);
      CHECK(400, total == 2048 && farthest == 3 && off[0][0][2] == 0 && off[0][0][3] == 0 && off[0][1][3] == 0 &&
                     off[1][0][3] == 0 && off[0][1][2] < 16 && off[1][0][2] < 16 && off[1][1][3] < 32);
      std::cout << "shaded values through the target, of " << total << " (one / two / three levels off): red and green "
                << off[0][0][1] << " / " << off[0][0][2] << " / " << off[0][0][3] << " rounded to nearest, "
                << off[0][1][1] << " / " << off[0][1][2] << " / " << off[0][1][3] << " toward zero; blue "
                << off[1][0][1] << " / " << off[1][0][2] << " / " << off[1][0][3] << " and " << off[1][1][1] << " / "
                << off[1][1][2] << " / " << off[1][1][3] << "\n";
      // Control: the shade applied to the linear value as it is would be a different picture
      // (white flat clay facing away: the root of 0.35 instead of 0.35).
      CHECK(401, Level8(OutputMap(ClayLinear(1.0f) * 0.35f, 1.0f)) - Level8(0.35f) > 8);
    }

    // Every blend into the target rounds again. Five additive layers of one colour and equal weight
    // (a ground square has about five layer draws; a pixel usually has one or two with a weight),
    // stored after each: the mapped sum is at most three levels (red, green) or six (blue, rounded
    // toward zero) from the colour, still under eight.
    {
      const int bits[2] = {6, 5};
      int worst[2][2] = {};
      for (int f = 0; f < 2; ++f) {
        for (int mode = 0; mode < 2; ++mode) {
          for (int k = 1; k < 256; ++k) {
            const float part = ClayLinear(float(k) / 255.0f) / 5.0f;
            float sum = 0.0f;
            for (int layer = 0; layer < 5; ++layer) sum = StoredFloat(sum + part, bits[f], mode == 0);
            worst[f][mode] = std::max(worst[f][mode], std::abs(Level8(OutputMap(sum, 1.0f)) - k));
          }
        }
      }
      CHECK(416, worst[0][0] <= 2 && worst[0][1] <= 3 && worst[1][0] <= 3 && worst[1][1] <= 6);
      std::cout << "five equal additive layers stored after each blend, largest error in levels (nearest / toward zero): "
                << "red and green " << worst[0][0] << " / " << worst[0][1] << ", blue " << worst[1][0] << " / "
                << worst[1][1] << "\n";
    }

    // Terrain layers add linear values with their captured blend (the game's own sum). Where one layer
    // has the whole weight, or both have one colour, the mapped value is what the display-space sum was;
    // in between it is brighter, never darker (0.3 against 0.6 at half weight: 0.474 where it was 0.45).
    {
      float band = 0.0f;
      bool never_darker = true;
      for (int i = 0; i <= 16; ++i) {
        const float w = float(i) / 16.0f;
        const float was = w * 0.3f + (1.0f - w) * 0.6f;
        const float is = OutputMap(w * ClayLinear(0.3f) + (1.0f - w) * ClayLinear(0.6f), 1.0f);
        never_darker = never_darker && is >= was - 1e-6f;
        band = std::max(band, is - was);
      }
      CHECK(402, never_darker && band > 0.02f && band < 0.03f);
      CHECK(403, std::fabs(OutputMap(1.0f * ClayLinear(0.3f) + 0.0f * ClayLinear(0.6f), 1.0f) - 0.3f) < 1e-6f &&
                     std::fabs(OutputMap(0.5f * ClayLinear(0.4f) + 0.5f * ClayLinear(0.4f), 1.0f) - 0.4f) < 1e-6f);
      // A sum over 1, which the float target keeps where the 8-bit one clamped, is 1 after the mapping.
      CHECK(404, OutputMap(ClayLinear(0.9f) + ClayLinear(0.9f), 1.0f) == 1.0f);
    }

    // The clear colour: the levels the 8-bit target showed, as linear values the target's channels hold
    // exactly (so that the clear does not depend on how a GPU rounds); they come back as those levels.
    CHECK(405, kClayClearLevels[0] == 20 && kClayClearLevels[1] == 20 && kClayClearLevels[2] == 25 &&
                   kClayClearLinear[3] == 1.0f);
    for (int i = 0; i < 3; ++i) {
      const int bits = i == 2 ? 5 : 6;
      CHECK(406, Level8(OutputMap(kClayClearLinear[i], 1.0f)) == kClayClearLevels[i]);
      CHECK(415, StoredFloat(kClayClearLinear[i], bits, true) == kClayClearLinear[i] &&
                     StoredFloat(kClayClearLinear[i], bits, false) == kClayClearLinear[i]);
      // (of the values the channel holds, the one nearest the level's square)
      CHECK(417, StoredFloat(ClayLinear(float(kClayClearLevels[i]) / 255.0f), bits, true) == kClayClearLinear[i]);
    }


    // The light switch (fable2_native_light): off or on; anything else is off and not recognised.
    bool ok = false;
    CHECK(407, ParseClayLight("off", &ok) == ClayLight::kOff && ok);
    CHECK(408, ParseClayLight(" On ", &ok) == ClayLight::kOn && ok);
    CHECK(409, ParseClayLight("true", &ok) == ClayLight::kOff && !ok);
    CHECK(410, ParseClayLight("", &ok) == ClayLight::kOff && !ok && ParseClayLight("on", nullptr) == ClayLight::kOn);
    // The exposure of the output mapping: 1 with light off (the materials picture), and with light on
    // 1 as well while it is not calibrated (plan Task 9 fits it against the emulated frame).
    CHECK(411, ClayExposure(ClayLight::kOff) == 1.0f && ClayExposure(ClayLight::kOn) == 1.0f);

    // Target memory at the pass's size, and the line that reports it.
    CHECK(412, kClayLinearTexelBytes == 4 && kClayMappedTexelBytes == 4 && kClayDepthTexelBytes == 4 &&
                   ClayTargetBytes(1120, 720, kClayLinearTexelBytes) == 3225600ull &&
                   ClayTargetBytes(1120, 720, 8) == 6451200ull);
    CHECK(413, FormatTargetText(1120, 720) ==
                   "targets 1120 x 720: linear R11G11B10_FLOAT 3.1 MB, mapped R8G8B8A8_UNORM 3.1 MB, "
                   "depth D32_FLOAT 3.1 MB");
    // The linear format is one for every pipeline of the pass, so the key has no field for it: the
    // same keys, and the same number of pipelines, as with the 8-bit target.
    CHECK(414, sizeof(PipelineKey) == 13);
  }

  // 420-489: the light (frame-map section 14, "Native light").
  {
    using capture::LightFamily;
    using capture::LightVertexKind;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    // Within single precision of the reference's double (relative, with a floor for values at 0).
    auto close_to = [](float got, double want) { return std::fabs(double(got) - want) <= 2e-6 * std::max(1.0, std::fabs(want)); };
    auto close3 = [&](const Float3& got, double x, double y, double z) { return close_to(got.x, x) && close_to(got.y, y) && close_to(got.z, z); };
    auto finite3 = [](const Float3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };

    // 420-429: the mirrors against the numbers of tools/xdk_sigmatch/light_formulas.py: the inputs and
    // the literals of tests/test_light_check.py, PinnedForTheRenderer, composed as the shaders compose.
    const Float3 tint{1.125f, 1.375f, 1.875f}, add{0.0068f, 0.071f, 0.092f}, floor{0.0588f, 0.0902f, 0.0f};
    const Float3 sun_col{0.99f, 0.83f, 0.40f}, sun_dir{-0.6334f, 0.0346f, 0.7731f}, axis{-0.6332f, 0.0344f, 0.7732f};
    const float tint_pow = 1.3f;
    const Float3 A{0.36f, 0.25f, 0.16f}, baked{0.5f, 0.6f, 0.7f};
    CHECK(420, close3(AmbientFactor(floor, 0.5f), 0.29410000000000003, 0.31765, 0.25));
    CHECK(420, close3(AmbientAxis({-0.25f, 0.5f, 0.75f}, axis), -0.5863346399999999, 0.09134288000000004, 0.77036264));
    CHECK(420, close3(LightPow({0.5f, -0.25f, 0.0f}, 1.3f), 0.40612619817811774, 0.16493848884661177, 0.0));
    struct Layer {
      float t, nds, gate;
      Float3 colour;
    };
    auto layer = [&](float x, float y, float c11x) {
      const Float3 n{x, y, std::sqrt(std::fabs(1.0f - x * x - y * y))};  // the vertex shader's normal
      Layer out;
      out.t = AmbientT(AmbientAxis(n, axis), n);
      out.nds = LightDot(LightNormalize(n), sun_dir);
      out.gate = SunGateSingle(1.0f, FacingGate(out.nds, 8.0f));
      out.colour = LightTerrainLayer(A, baked, tint, tint_pow, add, floor, out.t, out.nds, sun_col, c11x, out.gate);
      return out;
    };
    Layer got = layer(-0.25f, 0.5f, 1.0f);
    CHECK(421, close_to(got.t, 0.8464635611181413) && close_to(got.nds, 0.8166706563559398) && got.gate == 1.0f &&
                   close3(got.colour, 0.41410209137988163, 0.31851544194361503, 0.20218335739892443));
    got = layer(0.75f, -0.5f, 1.0f);
    CHECK(422, close_to(got.t, 0.7762633015448206) && close_to(got.nds, -0.15758788016712524) && got.gate == 0.0f &&
                   close3(got.colour, 0.10540540036087104, 0.13071754433579474, 0.12842166302336305));
    got = layer(0.6f, 0.55f, 0.75f);
    CHECK(423, close_to(got.t, 0.6809123554006105) && close_to(got.nds, 0.08812051374294312) && close_to(got.gate, 0.704964109943545) &&
                   close3(got.colour, 0.08506196574192819, 0.09415262382898765, 0.08062917350272386));
    got = layer(0.9f, 0.8f, 1.0f);
    CHECK(424, close_to(got.t, 0.8191864712988252) && close_to(got.nds, -0.017243666506090518) && got.gate == 0.0f &&
                   close3(got.colour, 0.11600703589238615, 0.14174207696615995, 0.14134348129986526));
    auto far_colour = [&](float x, float y, float c11x, float* t, float* nds) {
      const Float3 N = LightNormalize({x, y, std::sqrt(std::fabs(1.0f - x * x - y * y))});
      *t = LightDot(N, axis);
      *nds = LightDot(N, sun_dir);
      return LightTerrainFar({0.3f, 0.3f, 0.2f}, baked, tint, tint_pow, add, floor, *t, *nds, sun_col, c11x);
    };
    float t = 0, nds = 0;
    Float3 c = far_colour(-0.25f, 0.5f, 0.48f, &t, &nds);
    CHECK(425, close_to(t, 0.8166035719756988) && close_to(nds, 0.8166706563559398) &&
                   close3(c, 0.21255202935748654, 0.26688275988662435, 0.20704296707043351));
    c = far_colour(0.75f, -0.5f, 0.48f, &t, &nds);
    CHECK(426, close_to(t, -0.15729457889693604) && close_to(nds, -0.15758788016712524) &&
                   close3(c, 0.010099574402844745, 0.040452572541314194, 0.0184));
    struct Object {
      float t, nds;
      Float3 vcolour, colour;
    };
    auto object = [&](const Float3& n, const float m[3][3], int r, int g, int b, float c27w, const Float3& c49,
                      const Float3& c26) {
      const Float3 o1{m[0][0] * n.x + m[0][1] * n.y + m[0][2] * n.z, m[1][0] * n.x + m[1][1] * n.y + m[1][2] * n.z,
                      m[2][0] * n.x + m[2][1] * n.y + m[2][2] * n.z};  // the game's (R n) * c7.w
      Object out;
      out.t = AmbientT(AmbientAxis(o1, axis), o1);
      out.nds = LightDot(LightNormalize(o1), sun_dir);
      out.vcolour = VertexColour({float(r) / 255.0f, float(g) / 255.0f, float(b) / 255.0f}, 2.2f);
      const Float3 lit = LightObject({0.5f, 0.4f, 0.3f}, out.vcolour, tint, tint_pow, add, floor, out.t, out.nds, sun_col,
                                     c27w, SunGateSingle(1.0f, 1.0f), c49);
      out.colour = {lit.x * c26.x, lit.y * c26.y, lit.z * c26.z};
      return out;
    };
    const float m_rot[3][3] = {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}};
    const float m_skew[3][3] = {{1, 0, 0}, {0, 2, 0}, {0, 0, 0.5f}};
    const float m_mirror[3][3] = {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const Float3 n{-0.6f, 0.0f, 0.8f}, one{1.0f, 1.0f, 1.0f};
    Object o = object(n, m_rot, 200, 128, 64, 1.0f, one, one);
    CHECK(427, close_to(o.t, 0.727362161936653) && close_to(o.nds, 0.59772) &&
                   close3(o.vcolour, 0.5859729839496614, 0.2195197180748679, 0.04777575355617064) &&
                   close3(o.colour, 0.4555938921109685, 0.2706276044156476, 0.10503584306025268));
    o = object(n, m_rot, 200, 128, 64, 0.5f, {1.0f, 0.5f, 2.0f}, {1.0f, 1.0f, 0.5f});
    CHECK(428, close3(o.colour, 0.3757326460554843, 0.1172676611039119, 0.08838112153012634));
    o = object(n, m_skew, 200, 128, 64, 1.0f, one, one);
    CHECK(429, close_to(o.t, 0.9653736259006114) && close_to(o.nds, 0.9558593781353147) &&
                   close3(o.colour, 0.7393441381594212, 0.41761079399085327, 0.1523604668465132));
    o = object(n, m_mirror, 200, 128, 64, 1.0f, one, one);
    CHECK(429, close_to(o.t, 0.660085140197664) && close_to(o.nds, 0.23844000000000004) &&
                   close3(o.colour, 0.2530825970423436, 0.14484074306862768, 0.06091490419395856));
    // A zero normal is no direction: no sun, t = 0 (the ambient at its floor). A black colour: the
    // added light and the sun remain.
    o = object({0.0f, 0.0f, 0.0f}, m_rot, 200, 128, 64, 1.0f, one, one);
    CHECK(430, o.t == 0.0f && o.nds == 0.0f && close3(o.colour, 0.01990975864609052, 0.03531010782579682, 0.0276));
    o = object(n, m_rot, 0, 0, 0, 1.0f, one, one);
    CHECK(431, o.vcolour.x == 0.0f && o.vcolour.y == 0.0f && o.vcolour.z == 0.0f &&
                   close3(o.colour, 0.2992714, 0.22684304, 0.09932640000000001));

    // 432-439: zeros and values that are not numbers give numbers, from every function (review
    // focus 2); the same cases as test_light_check.py, test_zeros_and_non_finite_inputs_give_finite_numbers.
    CHECK(432, LightSat(nan) == 0.0f && LightSat(inf) == 1.0f && LightSat(-inf) == 0.0f && LightSat(0.25f) == 0.25f);
    const Float3 zero{0.0f, 0.0f, 0.0f}, bad{nan, inf, -inf};
    Float3 v = LightNormalize(zero);
    CHECK(433, v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
    v = LightNormalize({nan, 1.0f, 0.0f});
    CHECK(433, v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
    v = LightNormalize({3.0e30f, 3.0e30f, 0.0f});  // (the squared length is not a number of the type)
    CHECK(433, v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
    v = LightNormalize({inf, 0.0f, 0.0f});
    CHECK(433, v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
    v = LightNormalize({0.0f, -inf, 1.0f});
    CHECK(433, v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
    v = LightNormalize({0.0f, 3.0f, 4.0f});
    CHECK(433, v.x == 0.0f && close_to(v.y, 0.6) && close_to(v.z, 0.8));
    CHECK(434, AmbientT(zero, {0.0f, 0.0f, 1.0f}) == 0.0f && AmbientT({0.0f, 0.0f, 1.0f}, {inf, 0.0f, 0.0f}) == 0.0f &&
                   AmbientT({0.0f, 0.0f, 2.0f}, {0.0f, 0.0f, 0.5f}) == 1.0f);
    // t is clamped: a vector that points away from the normal gives 0, not a negative factor.
    CHECK(434, AmbientT({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}) == 0.0f && AmbientT({0.6f, 0.0f, -0.8f}, {0.0f, 0.0f, 1.0f}) == 0.0f);
    v = LightPow({0.0f, -0.25f, nan}, 2.0f);
    CHECK(435, v.x == 0.0f && v.y == 0.0625f && v.z == 0.0f);
    v = LightPow({0.5f, 0.5f, 0.5f}, nan);
    CHECK(435, v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
    v = LightPow({inf, 1.0e30f, 0.5f}, 8.0f);  // (an overflow is 0, not infinity)
    CHECK(435, v.x == 0.0f && v.y == 0.0f && v.z == 0.00390625f);
    // A zero base is 0 whatever the exponent (0^0 would be 1), and an exponent that is not a number
    // gives 0 whatever the base (1^infinity would be 1): the reference's rules.
    v = LightPow({0.0f, 0.5f, 1.0f}, 0.0f);
    CHECK(435, v.x == 0.0f && v.y == 1.0f && v.z == 1.0f);
    v = LightPow({1.0f, 1.0f, 0.5f}, inf);
    CHECK(435, v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
    v = AmbientFactor({0.1f, nan, inf}, nan);
    CHECK(436, v.x == 0.1f && v.y == 0.0f && v.z == 0.0f);
    v = AmbientFactor({0.2f, 0.2f, 0.2f}, 7.0f);
    CHECK(436, v.x == 1.0f && v.y == 1.0f && v.z == 1.0f);
    CHECK(437, FacingGate(nan, 8.0f) == 0.0f && FacingGate(inf, nan) == 0.0f && FacingGate(0.0625f, 8.0f) == 0.5f &&
                   SunGateSingle(nan, inf) == 0.0f && SunGateSingle(0.5f, 0.5f) == 0.25f && finite3(AmbientAxis(bad, bad)));
    CHECK(438, finite3(LightTerrainLayer({0.0f, 1.0f, inf}, {0.0f, -1.0f, nan}, {1.0f, nan, 1.0f}, 1.3f, {0.0f, 0.0f, inf},
                                         {0.0f, nan, 0.0f}, nan, inf, {nan, 1.0f, 1.0f}, nan, nan)) &&
                   finite3(LightTerrainFar({0.0f, 1.0f, inf}, {0.0f, -1.0f, nan}, {1.0f, nan, 1.0f}, 1.3f, {0.0f, 0.0f, inf},
                                           {0.0f, nan, 0.0f}, nan, inf, {nan, 1.0f, 1.0f}, nan)) &&
                   finite3(LightObject(bad, bad, bad, inf, bad, bad, inf, nan, bad, nan, inf, bad)) &&
                   finite3(LightObject({1.0e30f, 1.0e30f, 1.0e30f}, one, {1.0e30f, 1.0e30f, 1.0e30f}, 1.0f, zero, zero, 1.0f, 1.0f,
                                       one, 1.0e30f, 1.0f, {1.0e30f, 1.0e30f, 1.0e30f})) &&
                   finite3(VertexColour(bad, nan)));
    v = LightObject(one, zero, one, 1.3f, zero, zero, 0.0f, 0.0f, one, 1.0f, 1.0f, one);
    CHECK(438, v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
    // What a lit draw writes: the value, not clamped at 1; nothing that is not a number, nothing
    // negative, nothing above the bound. The bound keeps a pixel's sum of blended draws finite in the
    // target (64,512 is the largest value of its 10-bit channel).
    CHECK(439, ClayLit(0.25f) == 0.25f && ClayLit(2.5f) == 2.5f && ClayLit(1000.0f) == kClayLitMax && ClayLit(inf) == kClayLitMax &&
                   ClayLit(nan) == 0.0f && ClayLit(-1.0f) == 0.0f && ClayLit(-inf) == 0.0f && ClayLit(0.0f) == 0.0f &&
                   kClayLitMax == 64.0f && 1008.0f * kClayLitMax <= 64512.0f);
    // ... and the mapping still gives a display value for anything it holds.
    CHECK(439, OutputMap(ClayLit(2.5f), 1.0f) == 1.0f && OutputMap(ClayLit(0.25f), 1.0f) == 0.5f &&
                   OutputMap(ClayLit(2.5f), 0.1f) == 0.5f);

    // 440-449: the frame's constants from the light block.
    capture::LightBlock block{};
    const float block_sun[3] = {-0.6334f, 0.0346f, 0.7731f};
    for (int i = 0; i < 3; ++i) {
      block.sun_dir[i] = block_sun[i];
      block.sun_col[i] = 0.1f + float(i);
      block.tint[i] = 1.1f + float(i);
      block.add[i] = 0.01f + float(i);
      block.axis[i] = 0.2f + float(i);
      block.eye[i] = 50.0f + float(i);
    }
    block.tint_pow = 1.3f;
    block.out_scale = 16.0f;
    block.floor[0] = 0.0588f;
    block.floor[1] = 0.0902f;
    block.valid = true;
    static_assert(sizeof(ClayFrameConstants) == 96);
    ClayFrameConstants frame = MakeClayFrameConstants(block, ClayLight::kOn);
    CHECK(440, frame.light_on == 1 && frame.tint_pow == 1.3f && frame.floor[0] == 0.0588f && frame.floor[1] == 0.0902f &&
                   frame.floor[2] == 0.0f);
    for (int i = 0; i < 3; ++i) {
      CHECK(441, frame.sun_dir[i] == block_sun[i] && frame.sun_col[i] == 0.1f + float(i) && frame.tint[i] == 1.1f + float(i) &&
                     frame.add[i] == 0.01f + float(i) && frame.axis[i] == 0.2f + float(i));
    }
    // The output scale (16) is not in the block the shaders get, and no exposure is: one source.
    CHECK(442, frame.pad0 == 0.0f && frame.pad1 == 0.0f && frame.pad2 == 0.0f && frame.pad3 == 0.0f &&
                   ClayFrameExposure(frame) == ClayExposure(ClayLight::kOn));
    // The switch off, or no block: nothing lit, everything zero, exposure 1 (the materials picture).
    const ClayFrameConstants none{};
    frame = MakeClayFrameConstants(block, ClayLight::kOff);
    CHECK(443, std::memcmp(&frame, &none, sizeof(frame)) == 0 && ClayFrameExposure(frame) == ClayExposure(ClayLight::kOff));
    capture::LightBlock invalid = block;
    invalid.valid = false;
    frame = MakeClayFrameConstants(invalid, ClayLight::kOn);
    CHECK(444, std::memcmp(&frame, &none, sizeof(frame)) == 0);
    frame = MakeClayFrameConstants(capture::LightBlock{}, ClayLight::kOn);
    CHECK(444, std::memcmp(&frame, &none, sizeof(frame)) == 0);
    // A value that is not a number or not of usable size never reaches a shader: the frame is not lit.
    for (int field = 0; field < 8; ++field) {
      for (float value : {nan, inf, -inf, 2.0f * kLightValueMax}) {
        capture::LightBlock b = block;
        float* at[] = {&b.sun_dir[1], &b.sun_col[2], &b.tint[0], &b.add[1], &b.axis[2], &b.floor[0], &b.floor[1], &b.tint_pow};
        *at[field] = value;
        frame = MakeClayFrameConstants(b, ClayLight::kOn);
        CHECK(445, std::memcmp(&frame, &none, sizeof(frame)) == 0);
      }
    }
    capture::LightBlock edge = block;
    edge.tint_pow = 0.0f;
    CHECK(446, MakeClayFrameConstants(edge, ClayLight::kOn).light_on == 0);
    edge.tint_pow = 8.0f;
    edge.tint[0] = kLightValueMax;
    edge.add[0] = -kLightValueMax;
    CHECK(446, MakeClayFrameConstants(edge, ClayLight::kOn).light_on == 1);
    edge.tint_pow = 8.5f;
    CHECK(446, MakeClayFrameConstants(edge, ClayLight::kOn).light_on == 0);
    CHECK(447, LightValueUsable(0.0f) && LightValueUsable(-1024.0f) && !LightValueUsable(1024.5f) && !LightValueUsable(nan) &&
                   !LightValueUsable(inf));

    // 450-459: a lit object's constants.
    capture::LightInputs obj;
    obj.family = LightFamily::kObject;
    obj.kind = LightVertexKind::kStream;
    obj.colour_power = 2.2f;
    const float rot[3][3] = {{0.0f, -2.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f}};
    std::memcpy(obj.world_rot, rot, sizeof(rot));
    obj.world_scale = 0.5f;
    obj.c27w = 0.75f;
    obj.c49[0] = 1.0f; obj.c49[1] = 0.5f; obj.c49[2] = 2.0f;
    obj.c26[0] = 0.25f; obj.c26[1] = 1.0f; obj.c26[2] = 0.5f;
    static_assert(sizeof(ClayObjectConstants) == 272 && offsetof(ClayObjectConstants, probe) == 80);
    ClayObjectConstants oc;
    CHECK(450, MakeClayObjectConstants(obj, &oc));
    // (a colour-stream object has no probe registers: the block's are zero, whatever the inputs hold)
    {
      capture::LightInputs stray = obj;
      stray.vs_const[3][2] = 0.5f;
      ClayObjectConstants got;
      bool zero = MakeClayObjectConstants(stray, &got);
      for (int r = 0; r < 12; ++r) {
        for (int k = 0; k < 4; ++k) zero = zero && got.probe[r][k] == 0.0f;
      }
      CHECK(450, zero);
    }
    // M = world_rot * world_scale, rows, the fourth component 0.
    CHECK(451, oc.rot[0][0] == 0.0f && oc.rot[0][1] == -1.0f && oc.rot[0][2] == 0.0f && oc.rot[0][3] == 0.0f &&
                   oc.rot[1][0] == 1.0f && oc.rot[1][1] == 0.0f && oc.rot[1][3] == 0.0f && oc.rot[2][2] == 1.0f && oc.rot[2][3] == 0.0f);
    CHECK(452, oc.c49[0] == 1.0f && oc.c49[1] == 0.5f && oc.c49[2] == 2.0f && oc.c27w == 0.75f && oc.c26[0] == 0.25f &&
                   oc.c26[1] == 1.0f && oc.c26[2] == 0.5f && oc.colour_power == 2.2f);
    // The scale is inside the matrix, so its sign counts: a negative c7.w turns the normal, as in the game.
    capture::LightInputs neg = obj;
    neg.world_scale = -0.5f;
    CHECK(453, ClassifyRotation(neg.world_rot, neg.world_scale) == RotationKind::kMirrored && MakeClayObjectConstants(neg, &oc) &&
                   oc.rot[0][1] == 1.0f && oc.rot[1][0] == -1.0f && oc.rot[2][2] == -1.0f);
    // A skewed rotation is lit with the game's own M n; one without a direction is not lit.
    capture::LightInputs skew = obj;
    skew.world_rot[1][0] = 4.0f;
    CHECK(454, ClassifyRotation(skew.world_rot, skew.world_scale) == RotationKind::kSkewed && MakeClayObjectConstants(skew, &oc) &&
                   oc.rot[1][0] == 2.0f);
    capture::LightInputs flat = obj;
    flat.world_scale = 0.0f;
    const ClayObjectConstants empty{};
    CHECK(455, !MakeClayObjectConstants(flat, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0);
    flat = obj;
    flat.world_rot[2][0] = flat.world_rot[2][1] = flat.world_rot[2][2] = 0.0f;
    CHECK(455, !MakeClayObjectConstants(flat, &oc));
    // Values that are not numbers, a power outside (0, 8], another family or vertex kind: not lit.
    for (int field = 0; field < 5; ++field) {
      for (float value : {nan, inf, 4.0f * kLightValueMax}) {
        capture::LightInputs b = obj;
        float* at[] = {&b.world_rot[0][1], &b.world_scale, &b.c27w, &b.c49[1], &b.c26[2]};
        *at[field] = value;
        CHECK(456, !MakeClayObjectConstants(b, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0);
      }
    }
    for (float power : {0.0f, -2.2f, 8.5f, nan}) {
      capture::LightInputs b = obj;
      b.colour_power = power;
      CHECK(457, !MakeClayObjectConstants(b, &oc));
    }
    // A trunk has no colour stream, and an instanced or terrain kind is no object: not lit.
    capture::LightInputs other = obj;
    other.family = LightFamily::kTreeTrunk;
    CHECK(458, !MakeClayObjectConstants(other, &oc) && !LightFamilyDrawn(other));
    other = obj;
    other.kind = LightVertexKind::kInstance;
    CHECK(458, !MakeClayObjectConstants(other, &oc) && !LightFamilyDrawn(other));
    other.kind = LightVertexKind::kTerrain;
    CHECK(458, !MakeClayObjectConstants(other, &oc) && !LightFamilyDrawn(other));
    other.kind = LightVertexKind::kNone;
    CHECK(458, !MakeClayObjectConstants(other, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0);
    // The families the pass lights: the terrain layers, the far terrain, the objects with a colour
    // stream or with probe colours, the trunks (probe colours) and, since plan Task 8b, the instanced
    // plants with and without the ambient factor, each with its two- and four-light form; nothing
    // else (the leaves have no family yet), and no family on another vertex kind than its own.
    auto drawn = [](LightFamily f, LightVertexKind k, bool multi) {
      capture::LightInputs l;
      l.family = f;
      l.kind = k;
      l.multi = multi;
      return LightFamilyDrawn(l);
    };
    CHECK(459, drawn(LightFamily::kTerrainLayer, LightVertexKind::kTerrain, false) &&
                   drawn(LightFamily::kTerrainLayer, LightVertexKind::kTerrain, true) &&
                   drawn(LightFamily::kTerrainFar, LightVertexKind::kTerrain, false) &&
                   drawn(LightFamily::kObject, LightVertexKind::kStream, false) &&
                   drawn(LightFamily::kObject, LightVertexKind::kStream, true));
    CHECK(459, drawn(LightFamily::kObject, LightVertexKind::kProbe, false) && drawn(LightFamily::kObject, LightVertexKind::kProbe, true) &&
                   drawn(LightFamily::kTreeTrunk, LightVertexKind::kProbe, false) && drawn(LightFamily::kTreeTrunk, LightVertexKind::kProbe, true));
    CHECK(459, drawn(LightFamily::kPlant, LightVertexKind::kInstance, false) && drawn(LightFamily::kPlant, LightVertexKind::kInstance, true) &&
                   drawn(LightFamily::kPlantLit, LightVertexKind::kInstance, false) && drawn(LightFamily::kPlantLit, LightVertexKind::kInstance, true));
    CHECK(459, !drawn(LightFamily::kTreeTrunk, LightVertexKind::kStream, false) &&
                   !drawn(LightFamily::kPlant, LightVertexKind::kStream, false) && !drawn(LightFamily::kPlantLit, LightVertexKind::kProbe, false) &&
                   !drawn(LightFamily::kPlantLit, LightVertexKind::kTerrain, false) && !drawn(LightFamily::kTreeTrunk, LightVertexKind::kInstance, false) &&
                   !drawn(LightFamily::kPlant, LightVertexKind::kProbe, false) && !drawn(LightFamily::kObject, LightVertexKind::kInstance, false) &&
                   !drawn(LightFamily::kNone, LightVertexKind::kNone, false) && !drawn(LightFamily::kTerrainLayer, LightVertexKind::kStream, false) &&
                   !drawn(LightFamily::kTerrainLayer, LightVertexKind::kProbe, false) && !drawn(LightFamily::kObject, LightVertexKind::kTerrain, false));

    // 460-464: the light's part of the terrain constants, at the offsets the HLSL declares.
    capture::TerrainMaterialInputs tm;
    tm.valid = true;
    tm.base_valid = true;
    for (int i = 0; i < 4; ++i) tm.base_uv[i] = 8.0f + float(i);
    capture::LightInputs tl;
    tl.family = LightFamily::kTerrainLayer;
    tl.kind = LightVertexKind::kTerrain;
    tl.c11x = 0.75f;
    tl.facing_scale = 8.0f;
    const float colour_fix[2] = {1.0f, 1.0f}, base_fix[2] = {0.5f, 0.25f};
    ClayTerrainConstants tc = MakeClayTerrainConstants(tm, capture::MaterialKind::kTerrainLayer, colour_fix, 1, 3);
    auto at = [&](const char* name) -> const unsigned char* {
      for (const shaders::ClayTerrainField& f : shaders::kClayTerrainFields) {
        if (std::strcmp(f.name, name) == 0) return reinterpret_cast<const unsigned char*>(&tc) + f.offset;
      }
      return nullptr;
    };
    auto f32 = [&](const char* name, int i) {
      float got = nan;
      if (const unsigned char* p = at(name)) std::memcpy(&got, p + 4 * i, 4);
      return got;
    };
    auto u32 = [&](const char* name) {
      uint32_t got = ~0u;
      if (const unsigned char* p = at(name)) std::memcpy(&got, p, 4);
      return got;
    };
    // (without the light the block's light fields are zero)
    CHECK(460, f32("t_base_uv", 0) == 0.0f && f32("t_c11x", 0) == 0.0f && f32("t_facing_scale", 0) == 0.0f && u32("t_base_sampler") == 0);
    CHECK(461, SetClayTerrainLight(tc, tm, tl, base_fix, 3));
    // The base coordinate is scaled into the host texture's extent like the detail coordinate.
    CHECK(461, f32("t_base_uv", 0) == 4.0f && f32("t_base_uv", 1) == 2.25f && f32("t_base_uv", 2) == 5.0f && f32("t_base_uv", 3) == 2.75f &&
                   u32("t_base_sampler") == 3 && f32("t_c11x", 0) == 0.75f && f32("t_facing_scale", 0) == 8.0f && u32("t_light_pad") == 0);
    capture::LightInputs tf = tl;
    tf.family = LightFamily::kTerrainFar;
    tf.facing_scale = 0.0f;
    tf.c11x = 0.48f;
    tc = MakeClayTerrainConstants(tm, capture::MaterialKind::kTerrainFar, colour_fix, 1, 3);
    CHECK(462, SetClayTerrainLight(tc, tm, tf, base_fix, 1) && f32("t_c11x", 0) == 0.48f && f32("t_facing_scale", 0) == 0.0f &&
                   u32("t_base_sampler") == 1);
    // Without the base map's inputs, a layer's facing literal, a usable c11.x or a terrain family: no
    // light, and the block is left as it was.
    const ClayTerrainConstants before = MakeClayTerrainConstants(tm, capture::MaterialKind::kTerrainLayer, colour_fix, 1, 3);
    auto refused = [&](const capture::TerrainMaterialInputs& m, const capture::LightInputs& l) {
      ClayTerrainConstants k = before;
      return !SetClayTerrainLight(k, m, l, base_fix, 3) && std::memcmp(&k, &before, sizeof(k)) == 0;
    };
    capture::TerrainMaterialInputs no_base = tm;
    no_base.base_valid = false;
    capture::TerrainMaterialInputs not_valid = tm;
    not_valid.valid = false;
    capture::TerrainMaterialInputs wild = tm;
    wild.base_uv[2] = inf;
    CHECK(463, refused(no_base, tl) && refused(not_valid, tl) && refused(wild, tl));
    capture::LightInputs no_facing = tl;
    no_facing.facing_scale = 0.0f;
    capture::LightInputs bad_c11 = tl;
    bad_c11.c11x = nan;
    capture::LightInputs not_terrain = tl;
    not_terrain.family = LightFamily::kObject;
    capture::LightInputs wrong_kind = tl;
    wrong_kind.kind = LightVertexKind::kStream;
    CHECK(464, refused(tm, no_facing) && refused(tm, bad_c11) && refused(tm, not_terrain) && refused(tm, wrong_kind) &&
                   refused(tm, capture::LightInputs{}));

    // 465-469: the draw's light words, and how many elements the light's buffers must hold.
    const capture::DrawRecord r = BaseRecord();
    const float uv[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    ClayConstants dc = MakeClayConstants(r, 100, 0, uv, true, 1);
    CHECK(465, dc.light_mode == 0 && dc.light_count == 0 && dc.pad2 == 0);
    dc = MakeClayConstants(r, 100, 0, uv, true, 1, ClayAlpha{}, kLightModeObject | kLightModeLit, 60);
    CHECK(466, dc.light_mode == 6 && dc.light_count == 60);
    // Never more than the positions, and nothing without a mode.
    dc = MakeClayConstants(r, 100, 0, uv, true, 1, ClayAlpha{}, kLightModeTerrainNormals, 500);
    CHECK(466, dc.light_mode == 1 && dc.light_count == 100);
    dc = MakeClayConstants(r, 100, 0, uv, true, 1, ClayAlpha{}, 0, 500);
    CHECK(466, dc.light_mode == 0 && dc.light_count == 0);
    CHECK(467, kLightModeTerrainNormals == 1u && kLightModeObject == 2u && kLightModeLit == 4u && kLightModeProbe == 8u &&
                   kLightModeInstance == 16u && kLightModePlantLit == 32u && kLightModeCount == 64u);
    // A lit mesh record's mode by its vertex kind and family: the colour stream's is what it was (6); a
    // probe object's or trunk's adds the probe bit (14); an instanced plant's adds the instance bit
    // (22) and, for the family whose pixel shader has the ambient factor, the plant-lit bit (54). No
    // other pair of a kind and a family has one.
    CHECK(467, LitObjectMode(LightVertexKind::kStream, LightFamily::kObject) == 6u &&
                   LitObjectMode(LightVertexKind::kProbe, LightFamily::kObject) == 14u &&
                   LitObjectMode(LightVertexKind::kProbe, LightFamily::kTreeTrunk) == 14u &&
                   LitObjectMode(LightVertexKind::kInstance, LightFamily::kPlant) == 22u &&
                   LitObjectMode(LightVertexKind::kInstance, LightFamily::kPlantLit) == 54u);
    CHECK(467, LitObjectMode(LightVertexKind::kInstance, LightFamily::kObject) == 0u &&
                   LitObjectMode(LightVertexKind::kInstance, LightFamily::kTreeTrunk) == 0u &&
                   LitObjectMode(LightVertexKind::kStream, LightFamily::kTreeTrunk) == 0u &&
                   LitObjectMode(LightVertexKind::kStream, LightFamily::kPlant) == 0u &&
                   LitObjectMode(LightVertexKind::kProbe, LightFamily::kPlantLit) == 0u &&
                   LitObjectMode(LightVertexKind::kTerrain, LightFamily::kTerrainLayer) == 0u &&
                   LitObjectMode(LightVertexKind::kTerrain, LightFamily::kObject) == 0u &&
                   LitObjectMode(LightVertexKind::kNone, LightFamily::kNone) == 0u &&
                   LitObjectMode(LightVertexKind::kInstance, LightFamily::kNone) == 0u);
    dc = MakeClayConstants(r, 100, 0, uv, true, 1, ClayAlpha{}, LitObjectMode(LightVertexKind::kProbe, LightFamily::kObject), 60);
    CHECK(467, dc.light_mode == 14 && dc.light_count == 60);
    // The draw's largest vertex plus one (the capture's rule for the streams), not the stream's size:
    // a mesh of 28 vertices in a position stream of 528 needs 28 normals and colours.
    CHECK(468, LightVertexNeed(27, 0, 528) == 28 && LightVertexNeed(27, 100, 528) == 128 && LightVertexNeed(527, 0, 528) == 528 &&
                   LightVertexNeed(0, 0, 1) == 1);
    CHECK(469, LightVertexNeed(528, 0, 528) == 0 && LightVertexNeed(27, 501, 528) == 0 && LightVertexNeed(3, -4, 528) == 0 &&
                   LightVertexNeed(3, -3, 528) == 1 && LightVertexNeed(0, 0, 0) == 0 && LightVertexNeed(600, -100, 528) == 501 &&
                   LightVertexNeed(3, -10, 528) == 0);

    // 470-476: the light of a ground square is all or nothing (PlanTerrain).
    auto patch_of = [](float ox, float oy, float patch, float cols, uint32_t patches) {
      capture::TerrainPatch p;
      p.active = true;
      p.patch = patch;
      p.patches = patches;
      p.cols = cols;
      p.inv_cols = 1.0f / cols;
      p.cell[0] = p.cell[1] = 8.0f;
      p.origin[0] = ox;
      p.origin[1] = oy;
      return p;
    };
    const capture::TerrainPatch sq_a = patch_of(80, 144, 0, 6, 1), sq_b = patch_of(88, 144, 0, 6, 1);
    const capture::TerrainPatch sq_ab = patch_of(80, 144, 0, 6, 2);   // one draw over both squares
    const capture::TerrainPatch under = patch_of(48, 96, 72, 12, 12);   // twelve squares at y 144, x 48 to 144
    auto item = [](const capture::TerrainPatch& p, bool layer, bool light, bool usable = true) {
      TerrainPlanItem i;
      i.patch = &p;
      i.layer = layer;
      i.usable = usable;
      i.light = light;
      return i;
    };
    // L lit, u drawn with its material without light, - not drawn with its material (and not lit:
    // X would be a record lit without its material).
    auto lights = [](std::vector<TerrainPlanItem> items) {
      PlanTerrain(items);
      std::string s;
      for (const TerrainPlanItem& i : items) {
        s += i.action != TerrainAction::kMaterial ? (i.light ? 'X' : '-') : i.light ? 'L' : 'u';
      }
      return s;
    };

    const bool kLayer = true, kFarDraw = false;
    CHECK(470, lights({item(under, kFarDraw, true), item(sq_a, kLayer, true), item(sq_a, kLayer, true), item(sq_b, kLayer, true)}) == "LLLL");
    // One layer of a square cannot be lit: every layer of that square is unlit; the other square and
    // the underlay keep their light.
    CHECK(471, lights({item(under, kFarDraw, true), item(sq_a, kLayer, true), item(sq_a, kLayer, false), item(sq_b, kLayer, true)}) == "LuuL");
    CHECK(471, lights({item(sq_a, kLayer, false), item(sq_a, kLayer, true), item(sq_a, kLayer, true)}) == "uuu");
    // A layer draw of several patches ties its squares together, in whichever order they come.
    CHECK(472, lights({item(sq_ab, kLayer, true), item(sq_a, kLayer, true), item(sq_b, kLayer, false)}) == "uuu");
    CHECK(472, lights({item(sq_b, kLayer, false), item(sq_a, kLayer, true), item(sq_ab, kLayer, true)}) == "uuu");
    {
      // A chain that one pass over the records in their order does not finish: X over squares a and
      // b, Y over b and c, Z on c without light. Z darkens c; in the first pass X sees a and b still
      // lit and Y then darkens b; only a second pass takes the light from X (and with it from a).
      const capture::TerrainPatch sq_bc = patch_of(88, 144, 0, 6, 2), sq_c = patch_of(96, 144, 0, 6, 1);
      CHECK(477, lights({item(sq_ab, kLayer, true), item(sq_bc, kLayer, true), item(sq_c, kLayer, false)}) == "uuu");
      // A layer on a alone, drawn first, loses its light through the same chain.
      CHECK(477, lights({item(sq_a, kLayer, true), item(sq_ab, kLayer, true), item(sq_bc, kLayer, true), item(sq_c, kLayer, false)}) == "uuuu");
      // The chain does not reach a square no draw ties to it.
      const capture::TerrainPatch sq_e = patch_of(112, 144, 0, 6, 1);
      CHECK(477, lights({item(sq_ab, kLayer, true), item(sq_bc, kLayer, true), item(sq_c, kLayer, false), item(sq_e, kLayer, true)}) == "uuuL");
    }
    // An underlay without light does not take the light from the layers over it, nor the reverse.
    CHECK(473, lights({item(under, kFarDraw, false), item(sq_a, kLayer, true)}) == "uL" &&
                   lights({item(under, kFarDraw, true), item(sq_a, kLayer, false)}) == "Lu");
    // A record that is not drawn with its material has no light, and a square that fell back has none.
    CHECK(474, lights({item(under, kFarDraw, true), item(sq_a, kLayer, true), item(sq_a, kLayer, true, false), item(sq_b, kLayer, true)}) == "L--L");
    CHECK(474, lights({item(under, kFarDraw, true, false), item(sq_a, kLayer, true)}) == "--");
    {
      // Over the plan's budget every layer is flat: none is lit; a far draw keeps its own.
      capture::TerrainPatch row = patch_of(0, 0, 0, 4096, 4096);
      std::vector<TerrainPlanItem> many(5, item(row, kLayer, true));
      many.push_back(item(under, kFarDraw, true));
      PlanTerrain(many);
      bool none_lit = true;
      for (size_t i = 0; i < 5; ++i) none_lit = none_lit && !many[i].light && many[i].action == TerrainAction::kFlat;
      CHECK(475, none_lit && many[5].light && many[5].action == TerrainAction::kMaterial);
    }
    {
      // The scratch storage keeps no light state between frames.
      TerrainPlanScratch scratch;
      std::vector<TerrainPlanItem> first = {item(sq_a, kLayer, false), item(sq_a, kLayer, true)};
      PlanTerrain(first, &scratch);
      std::vector<TerrainPlanItem> second = {item(sq_a, kLayer, true), item(sq_a, kLayer, true)};
      PlanTerrain(second, &scratch);
      CHECK(476, !first[0].light && !first[1].light && second[0].light && second[1].light);
    }

    // 480-489: the Light line.
    ClayStats st;
    CHECK(480, FormatLightDrawnText(st) ==
                   "Light: off, lit 0 (terrain-layer 0, terrain-far 0, object 0, tree-trunk 0, plant 0, plant-lit 0), unlit 0");
    st.light_unlit[size_t(ClayUnlit::kOff)] = 948;
    CHECK(481, FormatLightDrawnText(st) ==
                   "Light: off, lit 0 (terrain-layer 0, terrain-far 0, object 0, tree-trunk 0, plant 0, plant-lit 0), "
                   "unlit 948 {light-off: 948}");
    st = ClayStats{};
    st.light_switch = true;
    st.light_unlit[size_t(ClayUnlit::kBlock)] = 12;
    CHECK(482, FormatLightDrawnText(st) ==
                   "Light: on, block missing, lit 0 (terrain-layer 0, terrain-far 0, object 0, tree-trunk 0, plant 0, plant-lit 0), "
                   "unlit 12 {light-block: 12}");
    st = ClayStats{};
    st.light_switch = st.light_block = true;
    st.light_drawn[size_t(LightFamily::kTerrainLayer)] = 492;
    st.light_drawn[size_t(LightFamily::kTerrainFar)] = 72;
    st.light_drawn[size_t(LightFamily::kObject)] = 120;
    st.light_unlit[size_t(ClayUnlit::kUnknown)] = 262;
    st.light_unlit[size_t(ClayUnlit::kNormal)] = 2;
    for (int i = 0; i < 3; ++i) st.light_unlit_by_ps.Add(0x7CD57B81550F19E3ull, uint8_t(ClayUnlit::kUnknown));
    st.light_unlit_by_ps.Add(0x401A000000000001ull, uint8_t(ClayUnlit::kUnknown));
    for (int i = 0; i < 2; ++i) st.light_unlit_by_ps.Add(0x8D900846800943C8ull, uint8_t(ClayUnlit::kNormal));
    st.light_sun_dir[0] = -0.6334f; st.light_sun_dir[1] = 0.0346f; st.light_sun_dir[2] = 0.7731f;
    st.light_sun_col[0] = 0.99f; st.light_sun_col[1] = 0.8306f; st.light_sun_col[2] = 0.4f;
    CHECK(483, FormatLightDrawnText(st) ==
                   "Light: on, block valid, lit 684 (terrain-layer 492, terrain-far 72, object 120, tree-trunk 0, plant 0, "
                   "plant-lit 0), unlit 264 {light-unknown: 262, normal-unsupported: 2}, unknown {0x7CD57B81550F19E3: 3, "
                   "0x8D900846800943C8: 2, 0x401A000000000001: 1}, sun (-0.633, 0.035, 0.773) colour (0.990, 0.831, 0.400)");
    CHECK(484, std::string(ClayUnlitName(ClayUnlit::kOff)) == "light-off" && std::string(ClayUnlitName(ClayUnlit::kBlock)) == "light-block" &&
                   std::string(ClayUnlitName(ClayUnlit::kUnknown)) == "light-unknown" &&
                   std::string(ClayUnlitName(ClayUnlit::kNormal)) == "normal-unsupported");
  }
  // The light's four buffer registers (t8 to t11): every draw binds all four, its own buffer where its
  // light_mode names it and its positions where not. (A buffer bound by address has no bounds check: one
  // left bound by another draw is read past its end; the first lit build's three runs ended so.)
  {
    using B = ClayLightBuffer;
    auto is = [](uint32_t mode, B t8, B t9, B t10, B t11) {
      const ClayLightBuffers s = LightBufferSources(mode);
      return s.source[0] == t8 && s.source[1] == t9 && s.source[2] == t10 && s.source[3] == t11;
    };
    static_assert(kClayLightBufferCount == 4 && sizeof(ClayLightBuffers::source) == 4);
    // No light input: the positions at all four (an unlit textured draw, a flat draw, light off).
    CHECK(485, is(0, B::kPositions, B::kPositions, B::kPositions, B::kPositions));
    // The lit bit alone names no buffer, and neither does the probe bit without the object bit.
    CHECK(485, is(kLightModeLit, B::kPositions, B::kPositions, B::kPositions, B::kPositions));
    CHECK(485, is(kLightModeProbe, B::kPositions, B::kPositions, B::kPositions, B::kPositions));
    CHECK(485, is(kLightModeProbe | kLightModeLit, B::kPositions, B::kPositions, B::kPositions, B::kPositions));
    // Terrain (steep ground without light, and lit): its normals at t8 only.
    CHECK(486, is(kLightModeTerrainNormals, B::kTerrainNormals, B::kPositions, B::kPositions, B::kPositions));
    CHECK(486, is(kLightModeTerrainNormals | kLightModeLit, B::kTerrainNormals, B::kPositions, B::kPositions, B::kPositions));
    CHECK(486, is(kLightModeTerrainNormals | kLightModeProbe, B::kTerrainNormals, B::kPositions, B::kPositions, B::kPositions));
    // A lit object with a colour stream: its normals at t9 and its colours at t10; the positions at t8
    // and at t11 (it has no weights).
    CHECK(487, is(kLightModeObject | kLightModeLit, B::kPositions, B::kNormals, B::kColours, B::kPositions));
    CHECK(487, is(kLightModeObject, B::kPositions, B::kNormals, B::kColours, B::kPositions));
    // A lit object with probe colours: its normals at t9, the set indices of its probe buffer at t10
    // (never a colour buffer: an instanced record's colours are no stream colours), its weights at t11.
    CHECK(489, is(LitObjectMode(capture::LightVertexKind::kProbe, capture::LightFamily::kTreeTrunk), B::kPositions, B::kNormals,
                  B::kProbeSets, B::kProbeWeights));
    CHECK(489, is(kLightModeObject | kLightModeProbe, B::kPositions, B::kNormals, B::kProbeSets, B::kProbeWeights));
    CHECK(489, is(LitObjectMode(capture::LightVertexKind::kStream, capture::LightFamily::kObject), B::kPositions, B::kNormals,
                  B::kColours, B::kPositions));
    // A lit instanced plant (plan Task 8b), with or without the ambient factor: its flat normals at
    // t9, its copies' colours at t10 (the instance set's buffer, never a colour stream's or a probe
    // buffer's region) and its bend inputs at t11.
    CHECK(489, is(LitObjectMode(capture::LightVertexKind::kInstance, capture::LightFamily::kPlant), B::kPositions, B::kNormals,
                  B::kInstanceColours, B::kBend));
    CHECK(489, is(LitObjectMode(capture::LightVertexKind::kInstance, capture::LightFamily::kPlantLit), B::kPositions, B::kNormals,
                  B::kInstanceColours, B::kBend));
    CHECK(489, is(kLightModeObject | kLightModeInstance, B::kPositions, B::kNormals, B::kInstanceColours, B::kBend));
    // The instance bit and the plant-lit bit name no buffer without the object bit ...
    CHECK(489, is(kLightModeInstance, B::kPositions, B::kPositions, B::kPositions, B::kPositions) &&
                   is(kLightModeInstance | kLightModePlantLit | kLightModeLit, B::kPositions, B::kPositions, B::kPositions, B::kPositions) &&
                   is(kLightModeObject | kLightModePlantLit | kLightModeLit, B::kPositions, B::kNormals, B::kColours, B::kPositions) &&
                   is(kLightModeTerrainNormals | kLightModeInstance, B::kTerrainNormals, B::kPositions, B::kPositions, B::kPositions));
    // ... and with the probe bit beside it (a mode the pass never makes) the instance bit decides, as
    // in the vertex shader, whose instance branch comes first.
    CHECK(489, is(kLightModeObject | kLightModeInstance | kLightModeProbe, B::kPositions, B::kNormals, B::kInstanceColours, B::kBend));
    // Every mode: t9 is the normals exactly with the object bit, t8 the terrain normals exactly with
    // the terrain bit, and t10 / t11 are a pair of one kind.
    for (uint32_t mode = 0; mode < kLightModeCount; ++mode) {
      const ClayLightBuffers s = LightBufferSources(mode);
      const bool object = (mode & kLightModeObject) != 0;
      bool ok = (s.source[0] == B::kTerrainNormals) == ((mode & kLightModeTerrainNormals) != 0) && (s.source[1] == B::kNormals) == object;
      ok = ok && (s.source[0] == B::kTerrainNormals || s.source[0] == B::kPositions) && (s.source[1] == B::kNormals || s.source[1] == B::kPositions);
      if (!object) ok = ok && s.source[2] == B::kPositions && s.source[3] == B::kPositions;
      if (object && (mode & kLightModeInstance)) ok = ok && s.source[2] == B::kInstanceColours && s.source[3] == B::kBend;
      if (object && !(mode & kLightModeInstance)) {
        ok = ok && ((mode & kLightModeProbe) ? s.source[2] == B::kProbeSets && s.source[3] == B::kProbeWeights
                                             : s.source[2] == B::kColours && s.source[3] == B::kPositions);
      }
      CHECK(489, ok);
    }
    // The four registers are the layout's last four parameters, vertex stage, buffers; the layout has
    // twelve parameters (the backend's limit) and 56 of the root signature's 64 words.
    for (uint32_t i = 0; i < kClayLightBufferCount; ++i) {
      const ClayBinding& b = kClayBindings[kClayLightBufferParam0 + i];
      CHECK(488, b.kind == ClayBindingKind::kBuffer && b.reg == 8 + i && b.stage == ClayBindingStage::kVertex && b.count == 1);
    }
    CHECK(488, std::size(kClayBindings) == 12 && kClayLightBufferParam0 + kClayLightBufferCount == std::size(kClayBindings) &&
                   ClayRootWords() == 56);
    // The stand-in holds as much per vertex as the largest of the four elements (the float4 weights).
    CHECK(488, kClayPositionElementBytes == 16 && kClayLightElementBytesMax == 16 && kProbeWeightElementBytes == 16 &&
                   kProbeSetElementBytes == 4);
    // Where each source lies in its buffer: the probe buffer's two regions, the weights from byte 0
    // and the set indices from the aligned offset behind them; every other source from byte 0.
    CHECK(488, LightBufferOffset(B::kProbeSets, 1000) == ProbeSetOffset(1000) && LightBufferOffset(B::kProbeSets, 1000) == 16128 &&
                   LightBufferOffset(B::kProbeSets, 16) == 256 && LightBufferOffset(B::kProbeSets, 17) == 512 &&
                   LightBufferOffset(B::kProbeWeights, 1000) == 0 && LightBufferOffset(B::kColours, 1000) == 0 &&
                   LightBufferOffset(B::kNormals, 1000) == 0 && LightBufferOffset(B::kTerrainNormals, 1000) == 0 &&
                   LightBufferOffset(B::kPositions, 1000) == 0 && LightBufferOffset(B::kInstanceColours, 1000) == 0 &&
                   LightBufferOffset(B::kBend, 1000) == 0);
    // The plants' two elements keep to the stand-in's size as well: one uint and one float4 a flat position.
    CHECK(488, kInstanceColourElementBytes == 4 && kBendElementBytes == 16 && kBendElementBytes <= kClayLightElementBytesMax);
    for (uint32_t count : {1u, 16u, 17u, 1000u, 65536u}) {
      // (a multiple of the RHI's offset alignment, behind the weights, the sets inside the buffer)
      CHECK(488, LightBufferOffset(B::kProbeSets, count) % 256 == 0 && LightBufferOffset(B::kProbeSets, count) >= uint64_t(count) * 16 &&
                     LightBufferOffset(B::kProbeSets, count) + uint64_t(count) * 4 == ProbeBufferBytes(count));
    }
  }
  // 490-499: probe colours (plan Task 8a; lighting-formulas.md, "Objects", probe_colour).
  {
    using capture::LightFamily;
    using capture::LightVertexKind;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    auto close_to = [](float got, double want) { return std::fabs(double(got) - want) <= 2e-6 * std::max(1.0, std::fabs(want)); };
    auto close3 = [&](const Float3& got, double x, double y, double z) { return close_to(got.x, x) && close_to(got.y, y) && close_to(got.z, z); };
    // The registers, weights and frame values of tests/test_light_check.py, PinnedForTheRenderer (PROBE,
    // WEIGHTS, probe_obj): three sets of four registers (base, then the rows of red, green, blue).
    const float probe[12][4] = {{0.25f, 0.125f, 0.0625f, 9.0f}, {0.5f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.125f, 1.0f},
                                {0.125f, 0.25f, 0.0f, 9.0f},    {-0.25f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.25f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f},
                                {0.0625f, 0.5f, 0.25f, 9.0f},   {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};
    const float weights[4] = {0.5f, -0.25f, 1.0f, 0.125f};
    // 490: the colour itself, exactly (the inputs are binary fractions): base + (rows . weights), the
    // base's fourth component not read, no power, no clamp (a colour above 1, a zero, a negative).
    Float3 v = ProbeColour(probe, 0, weights);
    CHECK(490, v.x == 0.5f && v.y == -0.125f && v.z == 0.3125f);
    v = ProbeColour(probe, 4, weights);
    CHECK(490, v.x == 0.0f && v.y == -0.0625f && v.z == 0.0f);
    v = ProbeColour(probe, 8, weights);
    CHECK(490, v.x == 1.0625f && v.y == 0.625f && v.z == 1.625f);
    // The fourth weight and the fourth component of a row count (a four-component dot product).
    {
      const float w3[4] = {0.5f, -0.25f, 1.0f, 0.0f};
      v = ProbeColour(probe, 0, w3);
      CHECK(490, v.x == 0.5f && v.y == -0.125f && v.z == 0.1875f);
    }
    // 491: the set index is clamped to the last set (the buffer holds 0, 4 or 8 only; the clamp keeps
    // any other word inside the twelve registers), as the shader clamps it.
    CHECK(491, ProbeSetClamp(0) == 0 && ProbeSetClamp(4) == 4 && ProbeSetClamp(8) == 8 && ProbeSetClamp(9) == 8 &&
                   ProbeSetClamp(12) == 8 && ProbeSetClamp(0xFFFFFFFFu) == 8 && ProbeSetClamp(5) == 5 &&
                   kProbeSetMax + 3 == capture::kLightProbeRegisters - 1);
    v = ProbeColour(probe, 0xFFFFFFFFu, weights);
    CHECK(491, v.x == 1.0625f && v.y == 0.625f && v.z == 1.625f);
    // Values that are not numbers give numbers (review focus 2).
    {
      const float bad[12][4] = {{nan, inf, -inf, 0.0f}, {nan, 0.0f, 0.0f, 0.0f}, {inf, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, -inf}};
      const float bad_w[4] = {1.0f, nan, 0.0f, inf};
      v = ProbeColour(bad, 0, bad_w);
      CHECK(491, std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z));
      // A base that is not a number counts as 0 by itself, as in the reference (`_finite(base[i])` before
      // the sum): the rows' part stays.
      const float no_base[12][4] = {{nan, inf, -inf, 0.0f}, {0.5f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.125f, 1.0f}};
      v = ProbeColour(no_base, 0, weights);
      CHECK(491, v.x == 0.25f && v.y == -0.25f && v.z == 0.25f);
    }
    // 492-494: the object function on a probe colour, against the reference's numbers. The vertex side
    // applies no power; the pixel side raises the ABSOLUTE value to c21.w, as the game does (its
    // `log r, r_abs`): a negative colour lights like its mirror image, a zero has no baked term.
    const Float3 tint{1.125f, 1.375f, 1.875f}, add{0.0068f, 0.071f, 0.092f}, floor{0.0588f, 0.0902f, 0.0f};
    const Float3 sun_col{0.99f, 0.83f, 0.40f}, sun_dir{-0.6334f, 0.0346f, 0.7731f}, axis{-0.6332f, 0.0344f, 0.7732f};
    const Float3 one{1.0f, 1.0f, 1.0f};
    auto lit = [&](const Float3& vcolour, float c27w, const Float3& c49, const Float3& c26) {
      const Float3 o1{0.0f, -0.6f, 0.8f};  // M_ROT times (-0.6, 0, 0.8)
      const float t = AmbientT(AmbientAxis(o1, axis), o1);
      const float nds = LightDot(LightNormalize(o1), sun_dir);
      const Float3 c = LightObject({0.5f, 0.4f, 0.3f}, vcolour, tint, 1.3f, add, floor, t, nds, sun_col, c27w, SunGateSingle(1.0f, 1.0f), c49);
      return Float3{c.x * c26.x, c.y * c26.y, c.z * c26.z};
    };
    CHECK(492, close3(lit(ProbeColour(probe, 0, weights), 1.0f, one, one), 0.426458072138784, 0.24789964999454064, 0.16493019528946776));
    CHECK(492, close3(lit(ProbeColour(probe, 8, weights), 1.0f, one, one), 0.6381219686383413, 0.3974707094581953, 0.6587403749904683));
    CHECK(492, close3(lit(ProbeColour(probe, 8, weights), 0.5f, {1.0f, 0.5f, 2.0f}, {1.0f, 1.0f, 0.5f}), 0.46699668431917063,
                      0.14897843736454883, 0.3652333874952342));
    // Set 4: red exactly zero, green -0.0625 (the captured meshes go to -0.062).
    CHECK(493, close3(lit(ProbeColour(probe, 4, weights), 1.0f, one, one), 0.2992714, 0.23539468096360217, 0.09932640000000001));
    // Zero, a rounding residue of either sign, the genuinely negative value: numbers, the same for
    // either sign, and never less than the colour without a baked term.
    const Float3 neg = lit({0.0f, -1e-8f, -0.06f}, 1.0f, one, one), pos = lit({0.0f, 1e-8f, 0.06f}, 1.0f, one, one);
    const Float3 none = lit({0.0f, 0.0f, 0.0f}, 1.0f, one, one);
    CHECK(494, close3(neg, 0.2992714, 0.2268430400125143, 0.10700391302815564) && neg.x == pos.x && neg.y == pos.y && neg.z == pos.z);
    CHECK(494, neg.x == none.x && neg.y >= none.y && neg.z > none.z && close3(none, 0.2992714, 0.22684304, 0.09932640000000001));
    v = LightPow({0.0f, -1e-8f, -0.06f}, 1.3f);
    CHECK(494, v.x == 0.0f && std::fabs(double(v.y) - 3.9810717055349695e-11) < 1e-16 && close_to(v.z, 0.025798628902309793) && v.y > 0.0f);

    // 495-497: a probe object's constants: the object's values and the twelve registers, or nothing.
    capture::LightInputs obj;
    obj.family = LightFamily::kObject;
    obj.kind = LightVertexKind::kProbe;
    const float rot[3][3] = {{0.0f, -2.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f}};
    std::memcpy(obj.world_rot, rot, sizeof(rot));
    obj.world_scale = 0.5f;
    obj.c27w = 0.75f;
    obj.c49[0] = 1.0f; obj.c49[1] = 0.5f; obj.c49[2] = 2.0f;
    obj.c26[0] = 0.25f; obj.c26[1] = 1.0f; obj.c26[2] = 0.5f;
    std::memcpy(obj.vs_const, probe, sizeof(probe));
    static_assert(sizeof(obj.vs_const) == sizeof(ClayObjectConstants::probe) && sizeof(ClayObjectConstants::probe) == 192);
    ClayObjectConstants oc;
    const ClayObjectConstants empty{};
    CHECK(495, MakeClayObjectConstants(obj, &oc) && std::memcmp(oc.probe, probe, sizeof(probe)) == 0);
    CHECK(495, oc.rot[0][1] == -1.0f && oc.rot[1][0] == 1.0f && oc.rot[2][2] == 1.0f && oc.rot[0][3] == 0.0f && oc.c49[1] == 0.5f &&
                   oc.c27w == 0.75f && oc.c26[0] == 0.25f);
    // (the colour stream's power is not a probe object's: whatever the inputs hold, the block's is 0)
    CHECK(495, oc.colour_power == 0.0f);
    obj.colour_power = 2.2f;
    CHECK(495, MakeClayObjectConstants(obj, &oc) && oc.colour_power == 0.0f);
    obj.colour_power = nan;
    CHECK(495, MakeClayObjectConstants(obj, &oc) && oc.colour_power == 0.0f);
    obj.colour_power = 0.0f;
    // A trunk is the same block; its scale (c7.w, 0.67 to 1.53 in the captures) is inside the rows.
    capture::LightInputs trunk = obj;
    trunk.family = LightFamily::kTreeTrunk;
    trunk.world_scale = 1.5f;
    CHECK(495, MakeClayObjectConstants(trunk, &oc) && oc.rot[0][1] == -3.0f && oc.rot[2][2] == 3.0f &&
                   std::memcmp(oc.probe, probe, sizeof(probe)) == 0);
    // A skinned object's rows are its world rotation as it is (the shader has no c7).
    capture::LightInputs skinned = obj;
    skinned.world_scale = 1.0f;
    CHECK(495, MakeClayObjectConstants(skinned, &oc) && oc.rot[0][1] == -2.0f && oc.rot[1][0] == 2.0f);
    // 496: refused as a whole on one value that is not a number or not of usable size, in any of the
    // forty-eight probe components and in every value a stream object's block is refused for.
    for (int reg = 0; reg < 12; ++reg) {
      for (int k = 0; k < 4; ++k) {
        for (float value : {nan, inf, -inf, 2.0f * kLightValueMax, -2.0f * kLightValueMax}) {
          capture::LightInputs b = obj;
          b.vs_const[reg][k] = value;
          oc.c27w = 5.0f;
          CHECK(496, !MakeClayObjectConstants(b, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0);
        }
      }
    }
    {
      capture::LightInputs edge = obj;
      edge.vs_const[11][3] = kLightValueMax;
      edge.vs_const[0][0] = -kLightValueMax;
      CHECK(496, MakeClayObjectConstants(edge, &oc) && oc.probe[11][3] == kLightValueMax && oc.probe[0][0] == -kLightValueMax);
    }
    for (int field = 0; field < 5; ++field) {
      for (float value : {nan, inf, 4.0f * kLightValueMax}) {
        capture::LightInputs b = obj;
        float* at[] = {&b.world_rot[0][1], &b.world_scale, &b.c27w, &b.c49[1], &b.c26[2]};
        *at[field] = value;
        CHECK(497, !MakeClayObjectConstants(b, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0);
      }
    }
    capture::LightInputs flat = obj;
    flat.world_scale = 0.0f;  // (a rotation without a direction)
    CHECK(497, !MakeClayObjectConstants(flat, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0);
    // Probe colours belong to the object and trunk families only.
    for (LightFamily family : {LightFamily::kNone, LightFamily::kTerrainLayer, LightFamily::kTerrainFar, LightFamily::kPlant,
                               LightFamily::kPlantLit}) {
      capture::LightInputs b = obj;
      b.family = family;
      CHECK(497, !MakeClayObjectConstants(b, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0 && !LightFamilyDrawn(b));
    }

    // 498: which records the pass lights, by vertex kind: the normals and the kind's OWN other buffers
    // (the arguments: normals, colours, probe, bend). A probe record has no colour buffer and a stream
    // record no probe buffer; an instanced record needs its copies' colours AND its bend inputs
    // (plan Task 8b), and neither of them lights a record of another kind.
    CHECK(498, LightObjectReady(LightVertexKind::kStream, true, true, false, false) &&
                   LightObjectReady(LightVertexKind::kProbe, true, false, true, false));
    CHECK(498, !LightObjectReady(LightVertexKind::kStream, true, false, true, true) && !LightObjectReady(LightVertexKind::kProbe, true, true, false, true) &&
                   !LightObjectReady(LightVertexKind::kStream, false, true, true, true) && !LightObjectReady(LightVertexKind::kProbe, false, true, true, true) &&
                   !LightObjectReady(LightVertexKind::kStream, true, false, false, false) &&
                   !LightObjectReady(LightVertexKind::kProbe, true, false, false, false));
    CHECK(498, LightObjectReady(LightVertexKind::kInstance, true, true, false, true) && LightObjectReady(LightVertexKind::kInstance, true, true, true, true));
    CHECK(498, !LightObjectReady(LightVertexKind::kInstance, true, true, true, false) && !LightObjectReady(LightVertexKind::kInstance, true, false, true, true) &&
                   !LightObjectReady(LightVertexKind::kInstance, false, true, true, true) &&
                   !LightObjectReady(LightVertexKind::kInstance, true, false, false, false));
    CHECK(498, !LightObjectReady(LightVertexKind::kTerrain, true, true, true, true) && !LightObjectReady(LightVertexKind::kNone, true, true, true, true));

    // 499: the frame's constant buffer. Slot 0 (the frame's light) and one slot per terrain record are
    // 256 bytes; a lit object's slot is 512 (cbuffer Object is 272 bytes with the probe registers) and
    // the objects' slots follow the small ones. The buffer holds every slot and, from the start of
    // EVERY slot, the largest block a shader declares at b1 (the vertex shader declares Object for
    // every draw, whatever is bound there), in whole steps.
    static_assert(kClayTerrainSlotBytes == 256 && kClayObjectSlotBytes == 512 && sizeof(ClayObjectConstants) <= kClayObjectSlotBytes &&
                  sizeof(ClayTerrainConstants) <= kClayTerrainSlotBytes && sizeof(ClayFrameConstants) <= kClayTerrainSlotBytes);
    CHECK(499, ClayObjectSlotOffset(1, 0) == 256 && ClayObjectSlotOffset(565, 0) == 144640 && ClayObjectSlotOffset(565, 1) == 145152 &&
                   ClayObjectSlotOffset(565, 170) == 231680 && ClayObjectSlotOffset(0x100000, 0x100000) == 0x30000000ull);
    CHECK(499, kClayConstantBufferStep == 65536);
    for (uint32_t small : {1u, 2u, 255u, 256u, 257u, 565u, 4096u}) {
      for (uint32_t objects : {0u, 1u, 2u, 127u, 128u, 129u, 170u, 4096u}) {
        const uint64_t bytes = ClayConstantBufferBytes(small, objects);
        bool ok = bytes % kClayConstantBufferStep == 0 && bytes % 256 == 0;
        // Every slot the pass can bind at b1 or b2, with the largest block from its start inside.
        ok = ok && uint64_t(small - 1) * kClayTerrainSlotBytes + kClayObjectSlotBytes <= bytes;
        if (objects) ok = ok && ClayObjectSlotOffset(small, objects - 1) + kClayObjectSlotBytes <= bytes;
        // ... and less than one step beyond that.
        const uint64_t need = std::max(uint64_t(small - 1) * kClayTerrainSlotBytes + kClayObjectSlotBytes,
                                       objects ? ClayObjectSlotOffset(small, objects - 1) + kClayObjectSlotBytes : uint64_t(0));
        ok = ok && bytes - need < kClayConstantBufferStep;
        // The offsets are multiples of the RHI's alignment and the object slots do not overlap the small ones.
        ok = ok && ClayObjectSlotOffset(small, 0) == uint64_t(small) * kClayTerrainSlotBytes && ClayObjectSlotOffset(small, objects) % 256 == 0 &&
             ClayObjectSlotOffset(small, 1) - ClayObjectSlotOffset(small, 0) == kClayObjectSlotBytes;
        CHECK(499, ok);
      }
    }
    // The case the padding is for: 256 small slots end exactly on a step; a view of the object block
    // from the last of them would end 256 bytes past it.
    CHECK(499, ClayConstantBufferBytes(256, 0) == 131072 && ClayConstantBufferBytes(255, 0) == 65536 && ClayConstantBufferBytes(1, 0) == 65536 &&
                   ClayConstantBufferBytes(1, 127) == 65536 && ClayConstantBufferBytes(1, 128) == 131072 &&
                   ClayConstantBufferBytes(565, 170) == 262144);
  }
  // 500-519: the instanced plants (plan Task 8b; lighting-formulas.md, "Instanced plants").
  {
    using capture::LightFamily;
    using capture::LightVertexKind;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    auto close_to = [](float got, double want) { return std::fabs(double(got) - want) <= 2e-6 * std::max(1.0, std::fabs(want)); };
    auto close3 = [&](const Float3& got, double x, double y, double z) { return close_to(got.x, x) && close_to(got.y, y) && close_to(got.z, z); };
    auto finite3 = [](const Float3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    // The frame values, the bend constants, the up axis and the pins of tests/test_light_check.py,
    // PinnedForTheRenderer (plant, plant_lit, PLANT_PINS), composed as the clay shaders compose: the
    // vertex side makes the height blend, the bend and the colour, the pixel side the bent normal and
    // the family's function with the mask at 1.
    const Float3 tint{1.125f, 1.375f, 1.875f}, add{0.0068f, 0.071f, 0.092f}, floor{0.0588f, 0.0902f, 0.0f};
    const Float3 sun_col{0.99f, 0.83f, 0.40f}, sun_dir{-0.6334f, 0.0346f, 0.7731f}, axis{-0.6332f, 0.0344f, 0.7732f};
    const Float3 A{0.5f, 0.4f, 0.3f}, up{0.28f, 0.0f, 0.96f}, n{0.6f, 0.0f, 0.8f}, zero{0.0f, 0.0f, 0.0f};
    const float c45[4] = {1.0f, -2.0f, 1.5f, 0.0f};
    // The exponent of the instance colour: the vertex copy c18.w = 2.2 * pixel c21.w.
    CHECK(500, kInstanceColourPower == 2.2f);
    const float plant_pow = kInstanceColourPower * 1.3f;
    struct Plant {
      float h;
      Float3 normal, amb, colour;
    };
    auto plant = [&](const Float3& normal, float z, int r, int g, int b, const float c[4], const Float3& u) {
      Plant out;
      out.h = PlantHeight(z, c[1], c[2]);
      out.normal = PlantNormal(normal, PlantBend(normal, u, out.h, c[0]));
      out.amb = InstanceAmbient({float(r) / 255.0f, float(g) / 255.0f, float(b) / 255.0f}, tint, plant_pow, add);
      out.colour = LightPlant(A, out.amb, LightDot(out.normal, sun_dir), sun_col, SunGateSingle(1.0f, 1.0f));
      return out;
    };
    struct PlantLit {
      float t, nds;
      Float3 colour;
    };
    auto plant_lit = [&](const Float3& normal, float z, int r, int g, int b, const float c[4], const Float3& u) {
      PlantLit out;
      const Float3 bent = PlantNormal(normal, PlantBend(normal, u, PlantHeight(z, c[1], c[2]), c[0]));
      // The vertex side exports the colour without the added light; the pixel side applies L and adds it.
      const Float3 base = InstanceAmbient({float(r) / 255.0f, float(g) / 255.0f, float(b) / 255.0f}, tint, plant_pow, zero);
      out.t = AmbientT(AmbientAxis(normal, axis), bent);
      out.nds = LightDot(bent, sun_dir);
      out.colour = LightPlantLit(A, base, add, floor, out.t, out.nds, sun_col, SunGateSingle(1.0f, 1.0f));
      return out;
    };
    // 500-502: family plant. Half way up, at the foot (the up axis itself), at the top (the vertex
    // normal), and with half the strength.
    Plant p = plant(n, 0.5f, 200, 128, 64, c45, up);
    CHECK(500, p.h == 0.5f && close3(p.normal, 0.5257311121191336, 0.0, 0.8506508083520399) &&
                   close3(p.amb, 0.5683564165336911, 0.2625218355265196, 0.12797253813103704) &&
                   close3(p.colour, 0.44487503475959345, 0.2127892319794812, 0.07734856786179545));
    p = plant(n, 0.25f, 200, 128, 64, c45, up);
    CHECK(501, p.h == 1.0f && close3(p.normal, 0.28, 0.0, 0.96) &&
                   close3(p.colour, 0.5637660882668456, 0.29253030221060783, 0.10617064143931111));
    p = plant(n, 0.75f, 200, 128, 64, c45, up);
    CHECK(501, p.h == 0.0f && close3(p.normal, 0.6, 0.0, 0.8) &&
                   close3(p.colour, 0.40220600826684555, 0.18417081421060788, 0.06700456143931112));
    {
      const float weak[4] = {0.5f, -2.0f, 1.5f, 0.0f};
      p = plant(n, 0.5f, 200, 128, 64, weak, up);
      CHECK(502, p.h == 0.5f && close3(p.normal, 0.5793187229742429, 0.0, 0.8151011085819305) &&
                     close3(p.colour, 0.414469231283684, 0.19239584459765913, 0.06997746398884772));
      // The two stages by themselves, exactly (binary fractions): the bend is (strength * bent,
      // strength * h), and the pixel side blends by sat of its fourth component.
      const capture::Float4 bend = PlantBend({0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.5f, 0.5f);
      CHECK(502, bend.x == 0.0f && bend.y == 0.5f && bend.z == 0.0f && bend.w == 0.25f);
      const capture::Float4 over = PlantBend({0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 7.0f, 4.0f);  // (h saturates before the strength)
      CHECK(502, over.y == 4.0f && over.w == 4.0f);
      const Float3 full = PlantNormal({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f, 3.0f});  // sat(3) = 1: the bend's direction
      CHECK(502, full.x == 0.0f && full.y == 0.0f && full.z == 1.0f);
      const Float3 scaled = PlantNormal({5.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f});
      CHECK(502, scaled.x == 1.0f && scaled.y == 0.0f && scaled.z == 0.0f);
      // The interpolated normal is normalised BEFORE the blend, as the game's pixel shaders do it
      // (0xA7E4... instr 12, 13, 15, then 16, 17): (2, 0, 0) is (1, 0, 0), half way to (0, 2, 0) is
      // (0.5, 1, 0); blended as it came it would be (1, 1, 0).
      const Float3 first = PlantNormal({2.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f, 0.5f});
      CHECK(502, close3(first, 0.4472135954999579, 0.8944271909999159, 0.0));
      CHECK(502, PlantHeight(0.5f, -2.0f, 1.5f) == 0.5f && PlantHeight(0.25f, -2.0f, 1.5f) == 1.0f && PlantHeight(0.0f, -2.0f, 1.5f) == 1.0f &&
                     PlantHeight(0.75f, -2.0f, 1.5f) == 0.0f && PlantHeight(9.0f, -2.0f, 1.5f) == 0.0f && PlantHeight(9.0f, 0.0f, 0.0f) == 0.0f);
    }
    // 503: family plant-lit: the object form with the instance colour, c27.w = 1 and c49 = 1, t from the
    // vertex side's axis (of the UNBENT normal) and the bent normal.
    PlantLit q = plant_lit(n, 0.5f, 200, 128, 64, c45, up);
    CHECK(503, close_to(q.t, 0.7248367390246099) && close_to(q.nds, 0.32464005352070285) &&
                   close3(q.colour, 0.3194501361635652, 0.17970944830063978, 0.07222667160009423));
    q = plant_lit(n, 0.25f, 200, 128, 64, c45, up);
    CHECK(503, close_to(q.t, 0.8820928926290964) && close_to(q.nds, 0.564824) &&
                   close3(q.colour, 0.5051217140439017, 0.27706333165698416, 0.10377581868510069));
    q = plant_lit(n, 0.75f, 200, 128, 64, c45, up);
    CHECK(503, close_to(q.t, 0.660085140197664) && close_to(q.nds, 0.23844000000000004) &&
                   close3(q.colour, 0.25308259704234354, 0.14484074306862768, 0.06091490419395856));
    // 504: c45.x == 0: the set does not bend, whatever the height says; a negative strength neither.
    {
      const float unbent[4] = {0.0f, -2.0f, 1.5f, 0.0f}, none[4] = {0.0f, 0.0f, 0.0f, 0.0f}, back[4] = {-1.0f, -2.0f, 1.5f, 0.0f};
      p = plant(n, 0.25f, 200, 128, 64, unbent, up);
      CHECK(504, p.h == 1.0f && close3(p.normal, 0.6, 0.0, 0.8) &&
                     close3(p.colour, 0.40220600826684555, 0.18417081421060788, 0.06700456143931112));
      const capture::Float4 bend = PlantBend(n, up, 1.0f, 0.0f);
      CHECK(504, bend.x == 0.0f && bend.y == 0.0f && bend.z == 0.0f && bend.w == 0.0f);
      q = plant_lit(n, 0.25f, 200, 128, 64, none, up);
      CHECK(504, close_to(q.t, 0.660085140197664) && close_to(q.nds, 0.23844000000000004) &&
                     close3(q.colour, 0.25308259704234354, 0.14484074306862768, 0.06091490419395856));
      p = plant(n, 0.25f, 200, 128, 64, back, up);
      CHECK(504, close3(p.normal, 0.6, 0.0, 0.8));
    }
    // 505: an instance colour of 0: the power of a zero base is 0; the added light and the sun remain.
    p = plant(n, 0.5f, 0, 0, 0, c45, up);
    CHECK(505, p.amb.x == 0.0068f && p.amb.y == 0.071f && p.amb.z == 0.092f &&
                   close3(p.colour, 0.1640968264927479, 0.13618049776887337, 0.06655680642248434));
    q = plant_lit(n, 0.5f, 0, 0, 0, c45, up);
    CHECK(505, close3(q.colour, 0.1640968264927479, 0.13618049776887337, 0.06655680642248434));
    {
      const Float3 black = InstanceAmbient(zero, tint, plant_pow, zero);
      CHECK(505, black.x == 0.0f && black.y == 0.0f && black.z == 0.0f);
    }
    // 506: a zero normal (a vertex without a direction). Bent, it is the up axis where h > 0; where
    // nothing bends it it stays no direction: no sun (plant), t = 0 and the ambient at its floor (plant-lit).
    p = plant(zero, 0.5f, 200, 128, 64, c45, up);
    CHECK(506, close3(p.normal, 0.28, 0.0, 0.96) && close3(p.colour, 0.5637660882668456, 0.29253030221060783, 0.10617064143931111));
    p = plant(zero, 0.75f, 200, 128, 64, c45, up);
    CHECK(506, p.normal.x == 0.0f && p.normal.y == 0.0f && p.normal.z == 0.0f &&
                   close3(p.colour, 0.28417820826684553, 0.10500873421060784, 0.03839176143931111));
    q = plant_lit(zero, 0.75f, 200, 128, 64, c45, up);
    CHECK(506, q.t == 0.0f && q.nds == 0.0f && close3(q.colour, 0.019909758646090518, 0.03531010782579682, 0.0276));
    // 507: a copy without an up axis (the buffer holds zero for it): the vertex normal's direction.
    p = plant(n, 0.5f, 200, 128, 64, c45, zero);
    CHECK(507, close3(p.normal, 0.6, 0.0, 0.8) && close3(p.colour, 0.40220600826684555, 0.18417081421060788, 0.06700456143931112));
    // 508: zeros and values that are not numbers give numbers, from every function (review focus 2).
    {
      const Float3 bad{nan, inf, -inf};
      CHECK(508, PlantHeight(nan, 1.0f, 0.5f) == 0.0f && PlantHeight(inf, 0.0f, 0.5f) == 0.0f && PlantHeight(inf, 1.0f, 0.5f) == 1.0f &&
                     PlantHeight(-inf, 1.0f, 0.5f) == 0.0f && PlantHeight(1.0f, nan, 0.0f) == 0.0f && PlantHeight(1.0f, 1.0f, nan) == 0.0f);
      for (const capture::Float4& b : {PlantBend(bad, bad, nan, inf), PlantBend(zero, zero, nan, nan), PlantBend(n, bad, 0.5f, 1.0f),
                                       PlantBend(bad, up, 0.5f, 1.0f), PlantBend(n, up, inf, -inf)}) {
        CHECK(508, std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.z) && std::isfinite(b.w));
      }
      CHECK(508, finite3(PlantNormal(bad, {nan, inf, -inf, nan})) && finite3(PlantNormal(n, {nan, 0.0f, 0.0f, 0.5f})) &&
                     finite3(PlantNormal(zero, {0.0f, 0.0f, 0.0f, inf})) && finite3(PlantNormal(n, {inf, inf, inf, inf})));
      CHECK(508, finite3(InstanceAmbient(bad, bad, nan, bad)) && finite3(InstanceAmbient({0.5f, 0.5f, 0.5f}, bad, inf, bad)) &&
                     finite3(InstanceAmbient({1.0e30f, 0.5f, 0.0f}, {1.0e30f, 1.0f, 1.0f}, 8.0f, {1.0e38f, 1.0e38f, 1.0e38f})));
      // (a product that leaves single precision, in each channel: a number, not infinity)
      CHECK(508, finite3(InstanceAmbient({1.0e30f, 1.0e30f, 1.0e30f}, {1.0e30f, 1.0e30f, 1.0e30f}, 1.0f, zero)) &&
                     finite3(InstanceAmbient({1.0f, 1.0f, 1.0f}, {3.0e38f, 3.0e38f, 3.0e38f}, 1.0f, {3.0e38f, 3.0e38f, 3.0e38f})));
      CHECK(508, finite3(LightPlant(bad, bad, nan, bad, nan)) && finite3(LightPlant({1.0e30f, 1.0e30f, 1.0e30f}, {1.0e30f, 1.0e30f, 1.0e30f},
                                                                                 1.0f, {1.0e30f, 1.0e30f, 1.0e30f}, 1.0e30f)) &&
                     finite3(LightPlantLit(bad, bad, bad, bad, nan, inf, bad, nan)) &&
                     finite3(LightPlantLit({1.0e30f, 1.0e30f, 1.0e30f}, {1.0e30f, 1.0e30f, 1.0e30f}, {1.0e30f, 1.0e30f, 1.0e30f}, zero, 1.0f,
                                           1.0f, {1.0e30f, 1.0e30f, 1.0e30f}, 1.0e30f)));
      // By hand (binary fractions): A * (amb + sun_col * sat(n_dot_sun) * gate); A * (base * L + add + sun).
      const Float3 by_hand = LightPlant({0.5f, 0.5f, 0.5f}, {0.25f, 0.5f, 1.0f}, 0.5f, {2.0f, 2.0f, 2.0f}, 0.5f);
      CHECK(508, by_hand.x == 0.375f && by_hand.y == 0.5f && by_hand.z == 0.75f);
      // floor 0.25, t 0.5: L = 0.25 + 0.75 * 0.25 = 0.4375; 0.5 * (2 * 0.4375 + 0.125 + 3 * 0.5 * 0.5) = 0.875
      const Float3 lit_by_hand = LightPlantLit({0.5f, 0.5f, 0.5f}, {2.0f, 2.0f, 2.0f}, {0.125f, 0.125f, 0.125f}, {0.25f, 0.25f, 0.25f},
                                               0.5f, 0.5f, {3.0f, 3.0f, 3.0f}, 0.5f);
      CHECK(508, lit_by_hand.x == 0.875f && lit_by_hand.y == 0.875f && lit_by_hand.z == 0.875f);
      // A sun behind the leaf lights nothing; the ambient stays.
      const Float3 behind = LightPlant({0.5f, 0.5f, 0.5f}, {0.25f, 0.5f, 1.0f}, -0.5f, {2.0f, 2.0f, 2.0f}, 1.0f);
      CHECK(508, behind.x == 0.125f && behind.y == 0.25f && behind.z == 0.5f);
    }

    // 509-512: a lit plant's constants. The bend constants c45.xyz ride in the fourth components of
    // the three rows of cbuffer Object, which an instanced record does not use otherwise (its normals
    // are in the world already), with the exponent's factor in colour_power: everything the plant's
    // shader path reads lies in the block's first 80 bytes, so a plant has a small slot.
    capture::LightInputs pl;
    pl.family = LightFamily::kPlant;
    pl.kind = LightVertexKind::kInstance;
    pl.vs_const[0][0] = 1.0f; pl.vs_const[0][1] = -200.0f; pl.vs_const[0][2] = 100.0f; pl.vs_const[0][3] = 7.0f;
    ClayObjectConstants oc;
    const ClayObjectConstants empty{};
    static_assert(sizeof(ClayObjectConstants) == 272 && kClayPlantConstantBytes == 80 && kClayPlantConstantBytes <= kClayTerrainSlotBytes &&
                  offsetof(ClayObjectConstants, colour_power) + 4 == kClayPlantConstantBytes &&
                  offsetof(ClayObjectConstants, probe) == kClayPlantConstantBytes);
    CHECK(509, MakeClayObjectConstants(pl, &oc) && oc.rot[0][3] == 1.0f && oc.rot[1][3] == -200.0f && oc.rot[2][3] == 100.0f &&
                   oc.colour_power == kInstanceColourPower);
    {
      // Nothing else: no rotation, none of an object's pixel constants, and above all no probe
      // register (the inputs hold c45 where a probe record holds c28).
      ClayObjectConstants want{};
      want.rot[0][3] = 1.0f; want.rot[1][3] = -200.0f; want.rot[2][3] = 100.0f;
      want.colour_power = kInstanceColourPower;
      CHECK(509, std::memcmp(&oc, &want, sizeof(oc)) == 0);
      capture::LightInputs lit = pl;
      lit.family = LightFamily::kPlantLit;
      lit.multi = true;
      ClayObjectConstants got;
      CHECK(509, MakeClayObjectConstants(lit, &got) && std::memcmp(&got, &want, sizeof(got)) == 0);
    }
    // What an instanced record does not read does not refuse it and is not carried: its world rotation
    // (none: a degenerate one here), an object's pixel constants, the stream colour's power, c45.w.
    {
      capture::LightInputs b = pl;
      std::memset(b.world_rot, 0, sizeof(b.world_rot));
      b.world_scale = nan;
      b.c27w = inf;
      b.c49[1] = nan;
      b.c26[2] = -inf;
      b.colour_power = nan;
      b.vs_const[0][3] = inf;
      b.vs_const[5][1] = nan;
      ClayObjectConstants got;
      CHECK(510, MakeClayObjectConstants(b, &got) && std::memcmp(&got, &oc, sizeof(got)) == 0);
    }
    // Refused as a whole on one bend constant that is not a number of usable size.
    for (int k = 0; k < 3; ++k) {
      for (float value : {nan, inf, -inf, 2.0f * kLightValueMax, -2.0f * kLightValueMax}) {
        capture::LightInputs b = pl;
        b.vs_const[0][k] = value;
        oc.c27w = 5.0f;
        CHECK(511, !MakeClayObjectConstants(b, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0);
      }
    }
    {
      capture::LightInputs edge = pl;
      edge.vs_const[0][1] = -kLightValueMax;
      edge.vs_const[0][2] = kLightValueMax;
      CHECK(511, MakeClayObjectConstants(edge, &oc) && oc.rot[1][3] == -kLightValueMax && oc.rot[2][3] == kLightValueMax);
    }
    // The plant families stand on the instance kind only, and the instance kind carries them only.
    for (LightVertexKind kind : {LightVertexKind::kNone, LightVertexKind::kTerrain, LightVertexKind::kStream, LightVertexKind::kProbe}) {
      capture::LightInputs b = pl;
      b.kind = kind;
      CHECK(512, !MakeClayObjectConstants(b, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0 && !LightFamilyDrawn(b));
      b.family = LightFamily::kPlantLit;
      CHECK(512, !MakeClayObjectConstants(b, &oc) && !LightFamilyDrawn(b));
    }
    for (LightFamily family : {LightFamily::kNone, LightFamily::kTerrainLayer, LightFamily::kTerrainFar, LightFamily::kObject,
                               LightFamily::kTreeTrunk}) {
      capture::LightInputs b = pl;
      b.family = family;
      CHECK(512, !MakeClayObjectConstants(b, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0 && !LightFamilyDrawn(b));
    }
    // An object's block keeps the fourth components of its rows zero, as before.
    {
      capture::LightInputs obj;
      obj.family = LightFamily::kObject;
      obj.kind = LightVertexKind::kStream;
      obj.colour_power = 2.2f;
      obj.vs_const[0][0] = 1.0f; obj.vs_const[0][1] = -200.0f; obj.vs_const[0][2] = 100.0f;
      CHECK(512, MakeClayObjectConstants(obj, &oc) && oc.rot[0][3] == 0.0f && oc.rot[1][3] == 0.0f && oc.rot[2][3] == 0.0f);
    }

    // 513: the plants' slots. A lit plant record takes a small slot (256 bytes) among the frame's and
    // the terrain's, before the objects' 512-byte slots: the bridge's frame of plan Task 8a's runs
    // (565 small slots, 174 objects) with its 135 plant records, the third spot's and the trees'.
    CHECK(513, ClaySmallSlotOffset(0) == 0 && ClaySmallSlotOffset(1) == 256 && ClaySmallSlotOffset(700) == 179200 &&
                   ClaySmallSlotOffset(700) == ClayObjectSlotOffset(700, 0));
    CHECK(513, ClayConstantBufferBytes(565 + 135, 174) == 327680 && ClayConstantBufferBytes(557 + 152, 210) == 327680 &&
                   ClayConstantBufferBytes(447 + 74, 276) == 327680 && ClayConstantBufferBytes(565, 174) == 262144);

    // 514-516: the key of an instanced record's bend inputs (kind 14): the mesh's position stream with
    // its layout, and the instance set as the flat normals have it (not the offset).
    capture::DrawRecord i = BaseRecord();
    i.instances.active = true;
    i.instances.rows_addr = 0x5000;
    i.instances.rows_size = 280;
    i.instances.inv_count = 0.25f;
    i.instances.count = 4.0f;
    i.instances.bias = 0.5f;
    i.instances.flat_count = 40;
    capture::LightInputs il;
    il.family = LightFamily::kPlant;
    il.kind = LightVertexKind::kInstance;
    il.normal.vb = {i.vb.phys_addr, i.vb.size};
    il.normal.offset_bytes = 8;
    il.normal.stride_bytes = 24;
    il.normal.format = uint8_t(capture::kVertexFormat_16_16_16_16_FLOAT);
    il.second.vb = {0x5000, 280};
    il.second.offset_bytes = 24;
    il.second.stride_bytes = 28;
    il.second.format = uint8_t(capture::kVertexFormat_8_8_8_8);
    const GeoKey bk = InstanceBendKey(i);
    CHECK(514, bk.kind == 14 && bk.addr == i.vb.phys_addr && bk.size == i.vb.size && bk.stride == i.pos.stride_bytes &&
                   !(bk == PositionKey(i, nullptr)) && !(bk == MeshPositionKey(i)) && !(bk == NormalKey(i, il)) &&
                   !(bk == InstanceColourKey(i, il)) && !(bk == UvKey(i)));
    {
      capture::DrawRecord i2 = i;
      i2.instances.offset[2] = -9.0f;  // (moves positions only)
      CHECK(514, InstanceBendKey(i2) == bk && !(PositionKey(i2, nullptr) == PositionKey(i, nullptr)));
    }
    for (int field = 0; field < 8; ++field) {
      capture::DrawRecord i2 = i;
      switch (field) {
        case 0: i2.instances.rows_addr = 0x6000; break;
        case 1: i2.instances.rows_size = 308; break;
        case 2: i2.instances.rows[1].offset_bytes = 8; break;
        case 3: i2.instances.inv_count = 0.2f; break;
        case 4: i2.instances.count = 5.0f; break;
        case 5: i2.instances.first = 2.0f; break;
        case 6: i2.instances.bias = 0.25f; break;
        default: i2.instances.flat_count = 44; break;
      }
      // (the same fields as the flat normals' key: an entry of each per instance set)
      CHECK(515, !(InstanceBendKey(i2) == bk) && !(NormalKey(i2, il) == NormalKey(i, il)));
    }
    for (int field = 0; field < 7; ++field) {
      capture::DrawRecord i2 = i;
      switch (field) {
        case 0: i2.vb.phys_addr += 0x100; break;
        case 1: i2.vb.size += 24; break;
        case 2: i2.pos.stride_bytes += 4; break;
        case 3: i2.pos.offset_bytes += 4; break;
        case 4: i2.pos.swizzle ^= 0x9; break;  // (which stored component is the position's z)
        case 5: i2.pos.swap16 = !i2.pos.swap16; break;
        default: i2.pos.format = i2.pos.format == capture::PosFormat::kHalf4 ? capture::PosFormat::kFloat3 : capture::PosFormat::kHalf4; break;
      }
      CHECK(515, !(InstanceBendKey(i2) == bk));
    }
    {
      // Nothing of the light's constants, of the material or of the draw's transform: the bend
      // constants c45 are the draw's and in no buffer.
      capture::DrawRecord q = i;
      q.rows[2] = 4.0f;
      q.vs_hash = 0x1111;
      q.material.ps_hash = 0x2222;
      q.cut[3] = 5.0f;
      CHECK(516, InstanceBendKey(q) == bk);
    }

    // 517: the light line names the bend inputs of the frame's plant records when there are any.
    ClayStats st;
    st.lit = 873;
    st.light_ready = 873;
    st.light_rotation[size_t(RotationKind::kRotation)] = 174;
    CHECK(517, FormatLightText(st) == "Light inputs: ready 873 of 873 lit records | world rotation: rotation 174, mirrored 0, "
                                      "skewed 0, degenerate 0");
    st.light_bend = 135;
    st.light_bend_bytes = 4300800;
    CHECK(517, FormatLightText(st) == "Light inputs: ready 873 of 873 lit records | world rotation: rotation 174, mirrored 0, "
                                      "skewed 0, degenerate 0 | bend inputs: 135 records, 4.1 MB");
    st.light_second[size_t(LightSecond::kValue)] = 2;
    CHECK(517, FormatLightText(st).ends_with("| probe or instance buffer refused: value 2 | bend inputs: 135 records, 4.1 MB"));
  }
  if (g_fail) return g_fail;
  std::cout << "PASS: clay color, keys, vertex counts, index ranges, constants, status text, "
               "uv keys, texture text, instanced keys, pipeline keys and slots, draw state, alpha constants, "
               "alpha-to-mask, terrain keys, terrain draw state, terrain constants, terrain squares, "
               "terrain text, normal and colour keys, light text, linear target and output mapping, light functions "
               "against the reference's numbers, frame and object and terrain light constants, lit squares, Light line, light buffer sources, "
               "probe colours and constants, constant buffer slots, plant light functions and constants and bend key\n";

  return 0;
}
