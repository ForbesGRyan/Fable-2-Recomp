// Synthetic standalone test for the clay pass's pure helpers; no game or GPU.
#include "../../src/native/render/clay_logic.h"
#include "../../src/native/fable2_native_shaders.h"

#include <cstdio>
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
    CHECK(32, IndexRangeValid(99, 0, 100));
    CHECK(33, !IndexRangeValid(100, 0, 100));
    CHECK(34, IndexRangeValid(49, 50, 100));
    CHECK(35, !IndexRangeValid(50, 50, 100));
    CHECK(36, !IndexRangeValid(0, -1, 100));
    CHECK(37, IndexRangeValid(10, -10, 100));
    CHECK(38, !IndexRangeValid(0xFFFFFFFFu, 0x7FFFFFFF, 0xFFFFFFFFu));
    CHECK(39, !IndexRangeValid(0, 0, 0));
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
    CHECK(121, IndexRangeValid(151, 0, RecordVertexCount(i, nullptr)) &&
                   !IndexRangeValid(152, 0, RecordVertexCount(i, nullptr)));
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
    static_assert(sizeof(ClayTerrainConstants) == 160 && kClayTerrainSlotBytes == 256);
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
  }
  if (g_fail) return g_fail;
  std::cout << "PASS: clay color, keys, vertex counts, index ranges, constants, status text, "
               "uv keys, texture text, instanced keys, pipeline keys and slots, draw state, alpha constants, "
               "alpha-to-mask, terrain keys, terrain draw state, terrain constants, terrain squares, "
               "terrain text, normal and colour keys, light text\n";
  return 0;
}
