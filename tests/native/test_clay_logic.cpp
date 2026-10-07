// Synthetic standalone test for the clay pass's pure helpers; no game or GPU.
#include "../../src/native/render/clay_logic.h"

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
    const GeoKey k = PositionKey(r);
    CHECK(16, k.addr == 0x2000 && k.size == 1600 && k.stride == 16 && k.kind == 0);
    capture::DrawRecord a = r;
    a.pos.offset_bytes = 4;
    CHECK(17, !(PositionKey(a) == k));
    a = r;
    a.pos.swizzle = 0x11;
    CHECK(18, !(PositionKey(a) == k));
    a = r;
    a.pos.swap16 = true;
    CHECK(19, !(PositionKey(a) == k));
    a = r;
    a.pos.exp_adjust = -3;
    CHECK(20, !(PositionKey(a) == k));
    a = r;
    a.pos.format = capture::PosFormat::kFloat4;
    CHECK(21, !(PositionKey(a) == k));
    a = r;
    a.pos.is_signed = false;
    a.pos.format = capture::PosFormat::kShort4;
    capture::DrawRecord b = a;
    b.pos.is_signed = true;
    CHECK(22, !(PositionKey(a) == PositionKey(b)));
    a = r;
    a.seq = 99;
    a.base_vertex = 5;  // irrelevant to the decoded stream
    CHECK(23, PositionKey(a) == k);
  }
  // 24-29: index keys and the referenced index byte range.
  {
    capture::DrawRecord r = BaseRecord();
    const GeoKey k = IndexKey(r);
    CHECK(24, k.addr == 0x1000 && k.size == 64 && k.stride == 0 && k.kind == 1);
    capture::DrawRecord a = r;
    a.start = 0;
    CHECK(25, !(IndexKey(a) == k));
    a = r;
    a.index32 = true;
    CHECK(26, !(IndexKey(a) == k) && IndexKey(a).stride == 1);
    a = r;
    a.prim = capture::kPrimTriangleStrip;
    CHECK(27, !(IndexKey(a) == k));
    a = r;
    a.indexed = false;
    const GeoKey n = IndexKey(a);
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
      CHECK(132, PositionKey(a) == PositionKey(b) && UvKey(a) == UvKey(b) && IndexKey(a) == IndexKey(b));
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
    capture::DrawRecord t = BaseRecord();
    t.terrain.active = true;
    t.terrain.patches = 1;
    CHECK(87, !(UvKey(s) == PositionKey(s)) && !(UvKey(s) == IndexKey(s)) &&
                  UvKey(s).kind != IndexKey(t).kind && UvKey(s).kind != PositionKey(t).kind);
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
    bridge.status[size_t(capture::MaterialStatus::kNoAlbedo)] = 13;
    CHECK(94, FormatTextureText(bridge, 667).find("textured 87 of 667 drawn (13%, 87% non-terrain)") !=
                  std::string::npos);
    // Only terrain drawn: no division by zero.
    TextureStats terrain_only;
    terrain_only.status[size_t(capture::MaterialStatus::kTerrain)] = 5;
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
    t.terrain.active = true;
    t.terrain.patch = 2;
    t.terrain.patches = 3;
    t.terrain.cols = 2;
    t.terrain.inv_cols = 0.5f;
    t.terrain.cell[0] = t.terrain.cell[1] = 8;
    t.terrain.map.phys_addr = 0x1BD0C000;
    t.terrain.map.size = 0x5000;
    constexpr uint32_t kPts = (capture::kTerrainGrid + 1) * (capture::kTerrainGrid + 1);
    CHECK(60, RecordVertexCount(t) == 3 * kPts);
    CHECK(61, RecordVertexCount(BaseRecord()) == 100);
    const GeoKey k = PositionKey(t);
    CHECK(62, k.addr == 0x1BD0C000 && k.size == 0x5000 && k.kind == 2 && k.stride == capture::kTerrainGrid);
    capture::DrawRecord a = t;
    a.terrain.patch = 3;
    CHECK(63, !(PositionKey(a) == k));
    a = t;
    a.terrain.origin[1] = 16;
    CHECK(64, !(PositionKey(a) == k));
    a = t;
    a.terrain.height_scale = 2;
    CHECK(65, !(PositionKey(a) == k));
    a = t;
    a.terrain.map.endian = 1;
    CHECK(66, !(PositionKey(a) == k));
    const GeoKey ik = IndexKey(t);
    CHECK(67, ik.kind == 3 && ik.stride == capture::kTerrainGrid && ik.addr == 0);
    a = t;
    a.terrain.patch = 9;  // same grid shape: same indices
    CHECK(68, IndexKey(a) == ik);
    a.terrain.patches = 1;
    CHECK(69, !(IndexKey(a) == ik));
  }
  // 70-72: rigid skin layouts are part of the position key.
  {
    capture::DrawRecord r = BaseRecord();
    capture::DrawRecord s = r;
    s.skin.active = true;
    s.skin.palette_addr = 0x3000;
    s.skin.palette_size = 168;
    s.skin.bone_stride = 24;
    CHECK(70, !(PositionKey(s) == PositionKey(r)) && PositionKey(s).kind == 0);
    capture::DrawRecord s2 = s;
    s2.skin.palette_addr = 0x4000;
    CHECK(71, !(PositionKey(s2) == PositionKey(s)));
    s2 = s;
    s2.skin.index_offset_bytes = 12;
    CHECK(72, !(PositionKey(s2) == PositionKey(s)));
    s2 = s;
    s2.skin.weight_shift[1] = 8;
    CHECK(73, !(PositionKey(s2) == PositionKey(s)));
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
    const GeoKey k = PositionKey(i);
    CHECK(102, k.kind == 5 && k.addr == 0x2000 && k.size == 1600 && k.stride == 16);
    CHECK(103, PositionKey(r).kind == 0 && !(PositionKey(r) == k));
    capture::DrawRecord a = i;
    a.instances.first = 3.0f;  // identical except the first copy
    CHECK(104, PositionKey(a).kind == 5 && !(PositionKey(a) == k));
    a = i;
    a.instances.rows_addr = 0x8000;
    CHECK(105, !(PositionKey(a) == k));
    a = i;
    a.instances.rows_size = 28 * 41;
    CHECK(106, !(PositionKey(a) == k));
    a = i;
    a.instances.rows[2].swizzle = 0x11;
    CHECK(107, !(PositionKey(a) == k));
    a = i;
    a.instances.inv_count = 0.2f;
    CHECK(108, !(PositionKey(a) == k));
    a = i;
    a.instances.count = 5.0f;
    CHECK(109, !(PositionKey(a) == k));
    a = i;
    a.instances.bias = 0.25f;
    CHECK(110, !(PositionKey(a) == k));
    a = i;
    a.instances.offset[1] = 8.0f;
    CHECK(111, !(PositionKey(a) == k));
    a = i;
    a.instances.flat_count = 153;
    CHECK(112, !(PositionKey(a) == k));
    a = i;
    a.pos.swizzle = 0x11;  // the mesh layout still counts
    CHECK(113, !(PositionKey(a) == k));
    a = i;
    a.seq = 99;  // irrelevant to the flat stream
    CHECK(114, PositionKey(a) == k);
    // The flat stream holds flat_count positions, not the mesh's.
    CHECK(115, RecordVertexCount(i) == 152 && RecordVertexCount(r) == 100);
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
    CHECK(118, UvKey(a) == uk && !(PositionKey(a) == k));
    a = i;
    a.instances.rows_addr = 0x8000;
    CHECK(134, UvKey(a) == uk && !(PositionKey(a) == k));
    a = i;
    a.instances.rows_size = 28 * 41;
    CHECK(135, UvKey(a) == uk && !(PositionKey(a) == k));
    a = i;
    a.instances.rows[2].swizzle = 0x11;
    a.instances.rows[1].offset_bytes = 12;
    CHECK(136, UvKey(a) == uk && !(PositionKey(a) == k));
    a = i;
    a.instances.offset[1] = 8.0f;
    CHECK(137, UvKey(a) == uk && !(PositionKey(a) == k));
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
    CHECK(122, MeshPositionKey(i) == PositionKey(r) && MeshPositionKey(i).kind == 0);
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
    CHECK(121, IndexRangeValid(151, 0, RecordVertexCount(i)) &&
                   !IndexRangeValid(152, 0, RecordVertexCount(i)));
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
  if (g_fail) return g_fail;
  std::cout << "PASS: clay color, keys, vertex counts, index ranges, constants, status text, "
               "uv keys, texture text, instanced keys, pipeline keys and slots, draw state, alpha constants, "
               "alpha-to-mask\n";
  return 0;
}
