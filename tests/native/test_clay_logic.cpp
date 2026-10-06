// Synthetic standalone test for the clay pass's pure helpers; no game or GPU.
#include "../../src/native/render/clay_logic.h"

#include <cstdio>
#include <cstring>
#include <iostream>
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
  // 40-43, 80-82: root constants match the HLSL cbuffer (28 dwords).
  {
    static_assert(sizeof(ClayConstants) == 112);
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
  // 102-121: instanced records: a flat stream of flat_count positions / UVs,
  // keyed by both streams, the row layouts, every constant and the flat count.
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
    a = i;
    a.instances.first = 3.0f;
    CHECK(118, !(UvKey(a) == uk));
    a = i;
    a.material.uv.comp_u = 1;
    CHECK(119, !(UvKey(a) == uk));
    a = i;
    a.instances.flat_count = 153;
    CHECK(120, !(UvKey(a) == uk));
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
  if (g_fail) return g_fail;
  std::cout << "PASS: clay color, keys, vertex counts, index ranges, constants, status text, "
               "uv keys, texture text, instanced keys\n";
  return 0;
}
