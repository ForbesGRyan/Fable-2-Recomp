// Synthetic standalone test for record assembly with instancing; no game or GPU.
#include "../../src/native/capture/draw_record.h"
#include "../../src/native/capture/water_record.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace fable2::native::capture;

// An indexed draw AssembleRecord accepts.
static DrawInputs Good() {
  static float bank[256 * 4] = {};
  static const TransformInfo t{4, TransformLayout::kDot, -1};
  for (int i = 0; i < 16; ++i) bank[4 * 4 + i] = float(i + 1);
  DrawInputs in{};
  in.func_id = 2; in.prim = kPrimTriangleList; in.start = 0; in.count = 30; in.indexed = true;
  in.have_shader = true; in.vs_hash = 0x8123; in.have_pos = true;
  in.pos.format = PosFormat::kFloat3; in.pos.stride_bytes = 12;
  in.have_vb = true; in.vb = {0x1000, 1200}; in.have_ib = true; in.ib = {0x2000, 60};
  in.transform = &t; in.bank = bank;
  return in;
}

// 4 vertices per copy from copy 2 on, float4 rows, 48 bytes per copy, `copies`
// copies in the stream (shader 0x8123...: instance_expand.h).
static InstanceSet Set(uint32_t copies) {
  InstanceSet s;
  s.active = true;
  s.rows_addr = 0x7000;
  s.rows_size = 48 * copies;
  for (int k = 0; k < 3; ++k) {
    s.rows[k].format = PosFormat::kFloat4;
    s.rows[k].stride_bytes = 48;
    s.rows[k].offset_bytes = uint32_t(16 * k);
  }
  s.inv_count = 0.25f; s.count = 4.0f; s.first = 2.0f; s.bias = 0.5f;
  s.offset[0] = 100.0f; s.offset[1] = -3.0f; s.offset[2] = 7.0f;
  return s;
}

int main() {
  // --- skip reasons: appended, so the earlier values do not move ---
  if (std::strcmp(SkipReasonName(SkipReason::kInstanceUnsupported), "instance-unsupported") != 0) return 1;
  if (std::strcmp(SkipReasonName(SkipReason::kSkinUnsupported), "skin-unsupported") != 0) return 2;
  if (uint8_t(SkipReason::kNoStream) != 7 || uint8_t(SkipReason::kInstanceUnsupported) != 8 ||
      uint8_t(SkipReason::kSkinUnsupported) != 9 || uint8_t(SkipReason::kDepthOnly) != 10 ||
      uint8_t(SkipReason::kCount) != 11) {
    return 3;
  }
  if (std::strcmp(SkipReasonName(SkipReason::kNoStream), "no-stream") != 0) return 4;

  // --- AssembleRecord carries the instance set ---
  DrawInputs in = Good();
  in.instance_shader = true;
  in.instances = Set(4);
  in.instances.flat_count = 8;
  DrawRecord r = AssembleRecord(in, 7);
  if (r.skip != SkipReason::kNone || !r.instances.active) return 5;
  if (r.instances.rows_addr != 0x7000 || r.instances.rows_size != 192 || r.instances.flat_count != 8) return 6;
  if (r.instances.inv_count != 0.25f || r.instances.count != 4.0f || r.instances.first != 2.0f ||
      r.instances.bias != 0.5f) {
    return 7;
  }
  if (r.instances.offset[0] != 100.0f || r.instances.offset[1] != -3.0f || r.instances.offset[2] != 7.0f) return 8;
  if (r.instances.rows[2].format != PosFormat::kFloat4 || r.instances.rows[2].offset_bytes != 32 ||
      r.instances.rows[1].stride_bytes != 48) {
    return 9;
  }
  // The mesh stream stays the record's vertex stream.
  if (r.vb.phys_addr != 0x1000 || r.vb.size != 1200 || r.pos.stride_bytes != 12) return 10;
  // A draw of any other shader has no instance set.
  if (AssembleRecord(Good(), 0).skip != SkipReason::kNone || AssembleRecord(Good(), 0).instances.active) return 11;

  // --- an instanced shader whose set was not built is never drawn ---
  in = Good();
  in.instance_shader = true;  // the table entry did not match the decoded fetches
  r = AssembleRecord(in, 0);
  if (r.skip != SkipReason::kInstanceUnsupported || r.instances.active) return 12;
  // An unsupported primitive and an unread shader are found first.
  in.prim = 8;
  if (AssembleRecord(in, 0).skip != SkipReason::kUnsupportedPrim) return 13;
  in = Good(); in.instance_shader = true; in.have_shader = false;
  if (AssembleRecord(in, 0).skip != SkipReason::kUnknownShader) return 14;
  // Every other way the instanced draw is not established is
  // instance-unsupported, whatever the capture stopped at: the entry's mesh
  // fetch is not a position (or its endian is not readable) ...
  in = Good(); in.instance_shader = true; in.have_pos = false;
  if (AssembleRecord(in, 0).skip != SkipReason::kInstanceUnsupported) return 15;
  // ... the mesh stream does not resolve ...
  in = Good(); in.instance_shader = true; in.have_vb = false;
  if (AssembleRecord(in, 0).skip != SkipReason::kInstanceUnsupported) return 16;
  in = Good(); in.instance_shader = true; in.have_pos = false; in.have_vb = false; in.have_ib = false;
  if (AssembleRecord(in, 0).skip != SkipReason::kInstanceUnsupported) return 22;
  // ... there is no index buffer, or the draw is not indexed at all ...
  in = Good(); in.instance_shader = true; in.have_ib = false;
  if (AssembleRecord(in, 0).skip != SkipReason::kInstanceUnsupported) return 23;
  in = Good(); in.instance_shader = true; in.indexed = false; in.have_ib = false;
  if (AssembleRecord(in, 0).skip != SkipReason::kInstanceUnsupported) return 24;
  // ... or the mesh stream is empty.
  in = Good(); in.instance_shader = true; in.vb.size = 0;
  if (AssembleRecord(in, 0).skip != SkipReason::kInstanceUnsupported) return 25;
  // The same inputs of a shader without an instance entry keep their reasons.
  in = Good(); in.have_pos = false;
  if (AssembleRecord(in, 0).skip != SkipReason::kUnknownPosFormat) return 26;
  in = Good(); in.have_vb = false;
  if (AssembleRecord(in, 0).skip != SkipReason::kNoStream) return 27;
  in = Good(); in.have_ib = false;
  if (AssembleRecord(in, 0).skip != SkipReason::kNoStream) return 28;
  // It also comes before the transform: the positions are the problem.
  in = Good(); in.instance_shader = true; in.transform = nullptr;
  if (AssembleRecord(in, 0).skip != SkipReason::kInstanceUnsupported) return 17;
  // A built set with no transform is no-transform, with no set in the record.
  in = Good(); in.instance_shader = true; in.instances = Set(4); in.instances.flat_count = 8;
  in.transform = nullptr;
  r = AssembleRecord(in, 0);
  if (r.skip != SkipReason::kNoTransform || r.instances.active) return 18;
  // A capture-side reason (bad-index, bad-memory) replaces instance-unsupported.
  if (ResolveSkip(SkipReason::kInstanceUnsupported, SkipReason::kBadIndex) != SkipReason::kBadIndex) return 19;
  if (ResolveSkip(SkipReason::kNone, SkipReason::kInstanceUnsupported) != SkipReason::kInstanceUnsupported) return 20;
  if (ResolveSkip(SkipReason::kUnsupportedPrim, SkipReason::kInstanceUnsupported) != SkipReason::kUnsupportedPrim) {
    return 21;
  }

  // --- capture-side range check: the flat count and the draw-count cap ---
  // Copies 2..3 of 4 and vertices 0..3 of 4: indices 0..7.
  InstanceSet s = Set(4);
  if (InstanceRangeSkip(&s, 7, 0, 4) != SkipReason::kNone || s.flat_count != 8) return 30;
  // The base vertex is part of the vertex index the shader maps.
  s = Set(4);
  if (InstanceRangeSkip(&s, 3, 4, 4) != SkipReason::kNone || s.flat_count != 8) return 31;
  s = Set(4);
  if (InstanceRangeSkip(&s, 4, 4, 4) != SkipReason::kBadIndex || s.flat_count != 0) return 32;  // copy 4 of 4
  s = Set(4);
  if (InstanceRangeSkip(&s, 8, 0, 4) != SkipReason::kBadIndex || s.flat_count != 0) return 33;
  s = Set(4);
  if (InstanceRangeSkip(&s, 7, 0, 3) != SkipReason::kBadIndex) return 34;   // 4 per copy, the mesh holds 3
  s = Set(4);
  if (InstanceRangeSkip(&s, 7, -1, 4) != SkipReason::kBadIndex) return 35;  // negative base vertex
  s = Set(4);
  if (InstanceRangeSkip(&s, -1, 0, 4) != SkipReason::kBadIndex) return 36;  // only reset indices
  s = Set(0);
  if (InstanceRangeSkip(&s, 0, 0, 4) != SkipReason::kBadIndex) return 37;   // no copy in the stream
  // Garbage constants: bad-index, and no flat stream is sized from them.
  s = Set(4); s.count = 0.0f;
  if (InstanceRangeSkip(&s, 7, 0, 4) != SkipReason::kBadIndex || s.flat_count != 0) return 38;
  s = Set(4); s.inv_count = 0.0f;
  if (InstanceRangeSkip(&s, 7, 0, 4) != SkipReason::kBadIndex || s.flat_count != 0) return 39;
  s = Set(4); s.inv_count = std::nanf("");
  if (InstanceRangeSkip(&s, 7, 0, 4) != SkipReason::kBadIndex || s.flat_count != 0) return 40;
  s = Set(4); s.count = std::nanf("");
  if (InstanceRangeSkip(&s, 7, 0, 4) != SkipReason::kBadIndex || s.flat_count != 0) return 41;
  s = Set(4); s.inv_count = 0.5f;  // disagrees with count 4
  if (InstanceRangeSkip(&s, 7, 0, 4) != SkipReason::kBadIndex || s.flat_count != 0) return 42;
  s = Set(4); s.first = 1e9f;      // a huge first copy
  if (InstanceRangeSkip(&s, 7, 0, 4) != SkipReason::kBadIndex || s.flat_count != 0) return 43;
  s = Set(4); s.first = std::numeric_limits<float>::infinity();
  if (InstanceRangeSkip(&s, 7, 0, 4) != SkipReason::kBadIndex || s.flat_count != 0) return 44;
  s = Set(4); s.count = 2.5f; s.inv_count = 0.4f;  // not a whole vertex count
  if (InstanceRangeSkip(&s, 7, 0, 4) != SkipReason::kBadIndex) return 45;
  // A count no integer holds (its inverse agrees, so only the mesh size rejects it).
  s = Set(4); s.count = 1e30f; s.inv_count = 1e-30f;
  if (InstanceRangeSkip(&s, 7, 0, 4) != SkipReason::kBadIndex || s.flat_count != 0) return 50;
  s = Set(4); s.count = std::numeric_limits<float>::infinity(); s.inv_count = 0.0f;
  if (InstanceRangeSkip(&s, 7, 0, 4) != SkipReason::kBadIndex || s.flat_count != 0) return 51;
  // An inverse inside the 1% agreement that sends an index past the mesh
  // (count 4, inv_count 0.2475: index 399 -> vertex 7; instance_expand.h).
  s = Set(100); s.first = 0.0f; s.inv_count = 0.2475f;
  if (InstanceRangeSkip(&s, 399, 0, 4) != SkipReason::kBadIndex || s.flat_count != 0) return 52;
  s = Set(100); s.first = 0.0f; s.inv_count = 0.2475f;
  if (InstanceRangeSkip(&s, 51, 0, 4) != SkipReason::kNone || s.flat_count != 52) return 53;
  // A 32-bit index plus the base vertex does not wrap into range.
  s = Set(4);
  if (InstanceRangeSkip(&s, 0xFFFFFFFFll, 0x7FFFFFFF, 4) != SkipReason::kBadIndex || s.flat_count != 0) return 46;
  // The cap: 1024 vertices per copy from copy 0, 5000 copies. Index 4194303 is
  // the last one a flat stream of kMaxDrawCount positions holds.
  InstanceSet big = Set(5000);
  big.count = 1024.0f; big.inv_count = 1.0f / 1024.0f; big.first = 0.0f;
  s = big;
  if (InstanceRangeSkip(&s, int64_t(kMaxDrawCount) - 1, 0, 1024) != SkipReason::kNone ||
      s.flat_count != kMaxDrawCount) {
    return 47;
  }
  s = big;  // in range of the copies (4096 of 5000) and the mesh, but over the cap
  if (InstanceRangeSkip(&s, int64_t(kMaxDrawCount), 0, 1024) != SkipReason::kBadMemory || s.flat_count != 0) {
    return 48;
  }
  s = big;
  if (InstanceRangeSkip(&s, int64_t(kMaxDrawCount) - 1, 1, 1024) != SkipReason::kBadMemory || s.flat_count != 0) {
    return 49;
  }
  // --- the instancing shaders' distance cut (eye xyz, squared distance) ---
  const float inf = std::numeric_limits<float>::infinity();
  const float nan = std::numeric_limits<float>::quiet_NaN();
  // No cut unless the capture found one: every record starts at +inf.
  if (DrawRecord{}.cut[3] != inf || DrawInputs{}.cut[3] != inf) return 60;
  in = Good();
  in.instance_shader = true;
  in.instances = Set(4);
  in.instances.flat_count = 8;
  if (AssembleRecord(in, 0).cut[3] != inf) return 61;      // an entry without "cut"
  in.cut[0] = 82.0f; in.cut[1] = 157.25f; in.cut[2] = 50.0f; in.cut[3] = 1062.5f;
  r = AssembleRecord(in, 0);
  if (r.skip != SkipReason::kNone || r.cut[0] != 82.0f || r.cut[1] != 157.25f || r.cut[2] != 50.0f ||
      r.cut[3] != 1062.5f) {
    return 62;
  }
  in.cut[3] = 0.0f;                                        // a zero distance is a cut, not garbage
  if (AssembleRecord(in, 0).cut[3] != 0.0f) return 63;
  // Garbage constants never cut: the record carries +inf.
  for (float bad : {nan, -1.0f, -inf, inf}) {
    in.cut[3] = bad;
    r = AssembleRecord(in, 0);
    if (r.skip != SkipReason::kNone || r.cut[3] != inf) return 64;
  }
  in.cut[3] = 1062.5f;
  for (float bad : {nan, inf, -inf}) {
    in.cut[1] = bad;
    r = AssembleRecord(in, 0);
    if (r.cut[3] != inf || !std::isfinite(r.cut[0]) || !std::isfinite(r.cut[1]) || !std::isfinite(r.cut[2])) return 65;
  }
  // Only an instanced record takes it: the same constants on a plain draw or
  // on a skipped one leave +inf.
  in = Good();
  in.cut[0] = 1.0f; in.cut[3] = 4.0f;
  if (AssembleRecord(in, 0).cut[3] != inf) return 66;
  in.instance_shader = true;                               // set not built: instance-unsupported
  r = AssembleRecord(in, 0);
  if (r.skip != SkipReason::kInstanceUnsupported || r.cut[3] != inf) return 67;
  // The table's references default to "no cut".
  const InstanceSpec plain_spec{{0, 1, 2}, {0, 0, 0}, 48, 49, 50, 0.5f, 28};
  const InstanceSpec cut_spec{{0, 1, 2}, {0, 0, 0}, 48, 49, 50, 0.5f, 28, 36, 54};
  if (plain_spec.cut_eye_ref != -1 || plain_spec.cut_dist2_ref != -1) return 68;
  if (cut_spec.cut_eye_ref != 36 || cut_spec.cut_dist2_ref != 54) return 69;
  // --- weighted skinning: an active skin is not deformed; a skin shader without one is skin-unsupported ---
  static const TransformInfo deformed_t{4, TransformLayout::kDot, -1, true};
  in = Good();
  in.transform = &deformed_t;
  r = AssembleRecord(in, 0);
  if (r.skip != SkipReason::kNone || !r.deformed) return 70;  // no skin: the table flag applies
  in.skin.active = true;
  r = AssembleRecord(in, 0);
  if (r.skip != SkipReason::kNone || r.deformed || !r.skin.active || r.cut[3] != inf) return 71;
  in = Good();
  in.skin_shader = true;                                    // layout or palette not established
  r = AssembleRecord(in, 0);
  if (r.skip != SkipReason::kSkinUnsupported) return 72;
  in.have_pos = false;                                      // whatever the capture stopped at
  if (AssembleRecord(in, 0).skip != SkipReason::kSkinUnsupported) return 73;
  in = Good(); in.skin_shader = true; in.have_vb = false;
  if (AssembleRecord(in, 0).skip != SkipReason::kSkinUnsupported) return 74;
  in = Good(); in.skin_shader = true; in.skin.active = true;
  if (AssembleRecord(in, 0).skip != SkipReason::kNone) return 75;
  in = Good(); in.skin_shader = true; in.have_shader = false;
  if (AssembleRecord(in, 0).skip != SkipReason::kUnknownShader) return 76;
  in = Good(); in.skin_shader = true; in.prim = 0xFFFF;
  if (AssembleRecord(in, 0).skip != SkipReason::kUnsupportedPrim) return 77;
  // An instanced record keeps the table's flag (wind sway is not modelled): only a skin clears it.
  in = Good();
  in.transform = &deformed_t;
  in.instance_shader = true;
  in.instances = Set(4);
  in.instances.flat_count = 8;
  r = AssembleRecord(in, 0);
  if (r.skip != SkipReason::kNone || !r.instances.active || r.skin.active || !r.deformed) return 78;
  // --- render state words and depth-only draws ---
  {
    StateWords w;
    w.read = true;
    w.color_control = 0x8700001C; w.alpha_ref = 0x3F008081; w.blend_control = 0x07060706;
    w.color_mask = 0x5; w.depth_control = 0x00708767; w.su_sc_mode = 0x00218006;
    w.vport_z_scale = 0xBF800000; w.vte_cntl = 0x0000043F; w.bin_mask_lo = 0x15555555;
    w.surface_info = 0x11810460;
    auto same = [&](const StateWords& a) {
      return a.read && a.color_control == w.color_control && a.alpha_ref == w.alpha_ref &&
             a.blend_control == w.blend_control && a.color_mask == w.color_mask &&
             a.depth_control == w.depth_control && a.su_sc_mode == w.su_sc_mode &&
             a.vport_z_scale == w.vport_z_scale && a.vte_cntl == w.vte_cntl && a.bin_mask_lo == w.bin_mask_lo &&
             a.surface_info == w.surface_info;
    };
    if (DrawRecord{}.state_words.read || DrawInputs{}.state_words.read || DrawInputs{}.depth_only) return 80;
    in = Good(); in.state_words = w;                        // plain
    r = AssembleRecord(in, 0);
    if (r.skip != SkipReason::kNone || !same(r.state_words)) return 81;
    in = Good(); in.state_words = w;                        // instanced
    in.instance_shader = true; in.instances = Set(4); in.instances.flat_count = 8;
    r = AssembleRecord(in, 0);
    if (r.skip != SkipReason::kNone || !r.instances.active || !same(r.state_words)) return 82;
    in = Good(); in.state_words = w;                        // skinned
    in.skin_shader = true; in.skin.active = true;
    r = AssembleRecord(in, 0);
    if (r.skip != SkipReason::kNone || !same(r.state_words)) return 83;
    in = Good(); in.state_words = w;                        // terrain
    in.prim = kPrimQuadPatch; in.terrain_shader = true; in.terrain.active = true; in.indexed = false;
    r = AssembleRecord(in, 0);
    if (r.skip != SkipReason::kNone || !same(r.state_words)) return 84;
    // A depth-only draw is skipped; the reason is named and counted like the others.
    if (std::strcmp(SkipReasonName(SkipReason::kDepthOnly), "depth-only") != 0) return 85;
    in = Good(); in.depth_only = true; in.state_words = w;
    r = AssembleRecord(in, 0);
    if (r.skip != SkipReason::kDepthOnly || !same(r.state_words)) return 86;
    in.prim = 0xFFFF;                                       // unsupported-prim wins
    if (AssembleRecord(in, 0).skip != SkipReason::kUnsupportedPrim) return 87;
    in = Good(); in.depth_only = true; in.have_shader = false;
    if (AssembleRecord(in, 0).skip != SkipReason::kUnknownShader) return 88;  // unknown-shader wins
    // Before the instance and skin checks.
    in = Good(); in.depth_only = true; in.instance_shader = true;
    if (AssembleRecord(in, 0).skip != SkipReason::kDepthOnly) return 89;
    in = Good(); in.depth_only = true; in.skin_shader = true;
    if (AssembleRecord(in, 0).skip != SkipReason::kDepthOnly) return 90;
    in = Good(); in.depth_only = true; in.prim = kPrimQuadPatch; in.terrain_shader = true;  // terrain inactive
    if (AssembleRecord(in, 0).skip != SkipReason::kDepthOnly) return 91;
    // Capture-side reasons replace it like any other assembled reason.
    if (ResolveSkip(SkipReason::kDepthOnly, SkipReason::kBadIndex) != SkipReason::kBadIndex) return 92;
  }
  // --- the record's size and its side index ---
  // A terrain draw's inputs (452 bytes) are in the frame's side array, not in every record.
  if (sizeof(DrawRecord) > 640) {
    std::printf("FAIL: sizeof(DrawRecord) = %zu, above 640\n", sizeof(DrawRecord));
    return 100;
  }
  // No record holds an index until the frame gives it one (FrameBuilder::Add): not a
  // default record, not a plain one, not an assembled terrain one.
  if (kNoSideIndex != 0xFFFF || DrawRecord{}.terrain_index != kNoSideIndex) return 101;
  if (AssembleRecord(Good(), 0).terrain_index != kNoSideIndex) return 102;
  // The second side index (the light inputs, FrameScene::lights) lies beside the first, in what
  // was padding: the record is 616 bytes with both, and no record holds a light index either
  // until the frame gives it one.
  if (sizeof(DrawRecord) != 616 || offsetof(DrawRecord, light_index) != offsetof(DrawRecord, terrain_index) + 2 ||
      sizeof(DrawRecord{}.light_index) != 2) {
    std::printf("FAIL: sizeof(DrawRecord) = %zu, light_index at %zu, terrain_index at %zu\n", sizeof(DrawRecord),
                offsetof(DrawRecord, light_index), offsetof(DrawRecord, terrain_index));
    return 109;
  }
  if (DrawRecord{}.light_index != kNoSideIndex || AssembleRecord(Good(), 0).light_index != kNoSideIndex) return 110;
  // The third side index (the wind inputs, FrameScene::wind and ::shadow_wind; foliage spec ruling
  // 7) lies in the record's tail padding, after `skip`: the record is still 616 bytes, and no
  // record holds a wind index until the frame gives it one.
  if (sizeof(DrawRecord) != 616 || offsetof(DrawRecord, wind_index) != 614 || sizeof(DrawRecord{}.wind_index) != 2 ||
      offsetof(DrawRecord, wind_index) <= offsetof(DrawRecord, skip)) {
    std::printf("FAIL: sizeof(DrawRecord) = %zu, wind_index at %zu, skip at %zu\n", sizeof(DrawRecord),
                offsetof(DrawRecord, wind_index), offsetof(DrawRecord, skip));
    return 150;
  }
  if (DrawRecord{}.wind_index != kNoSideIndex || AssembleRecord(Good(), 0).wind_index != kNoSideIndex) return 150;
  {
    // (also an instanced record and a skipped one)
    DrawInputs t = Good();
    t.have_pos = false;
    if (AssembleRecord(t, 0).skip == SkipReason::kNone || AssembleRecord(t, 0).wind_index != kNoSideIndex) return 150;
  }
  {
    DrawInputs t = Good();
    t.prim = kPrimQuadPatch; t.terrain_shader = true; t.indexed = false;
    t.terrain.active = true; t.terrain.patch = 3; t.terrain.patches = 2;
    r = AssembleRecord(t, 0);
    if (r.skip != SkipReason::kNone || r.terrain_index != kNoSideIndex) return 103;
    // The inputs the frame keeps for the record: the draw's own patch.
    if (!IsTerrainDraw(t) || TerrainInputsOf(t) != &t.terrain) return 104;
    // A patch that was not built (unhandled heightmap): none, and the draw is skipped.
    t.terrain.active = false;
    if (TerrainInputsOf(t) || AssembleRecord(t, 0).skip != SkipReason::kUnknownPosFormat) return 105;
    // A patch draw whose shader has no terrain entry: none.
    t.terrain.active = true; t.terrain_shader = false;
    if (IsTerrainDraw(t) || TerrainInputsOf(t)) return 106;
    // Terrain fields on an ordinary draw are ignored, also with a terrain shader bound
    // (the primitive decides).
    t = Good(); t.terrain.active = true;
    if (IsTerrainDraw(t) || TerrainInputsOf(t)) return 107;
    t.terrain_shader = true;
    if (IsTerrainDraw(t) || TerrainInputsOf(t) || AssembleRecord(t, 0).skip != SkipReason::kNone) return 108;
  }
  // Shadow-pass records (frame_scene.h, FrameScene::shadow_draws) are DrawRecords: the record does
  // not grow for them. What a caster needs beyond the record is its twin entry, ShadowCaster.
  {
    static_assert(sizeof(DrawRecord) == 616);
    // (sub-project 12 adds nothing to it: an impostor call makes no draw record and changes none; its record is
    // capture::ImpostorRecord, in a block of the scene's own: spec ruling 10)
    static_assert(sizeof(DrawRecord) == 616, "sub-project 12 adds nothing to it");
    if (sizeof(ShadowCaster) != 12) return 120;
    const ShadowCaster c;
    if (c.kind != CasterKind::kOpaque || c.biased || c.bias_scale != 0.0f || c.bias_offset != 0.0f) return 121;
    // The kinds as tools/xdk_sigmatch/gen_caster_table.py writes them (KINDS: 0 opaque, 1 alpha-test).
    if (uint8_t(CasterKind::kOpaque) != 0 || uint8_t(CasterKind::kAlphaTest) != 1) return 122;
    // A shadow draw's skip reason is a record's (SkipReason) or one of the pass's own, numbered
    // after them, so that one tally holds both; every code has a name.
    if (uint8_t(ShadowSkip::kNoCaster) != uint8_t(SkipReason::kCount) || kShadowSkipCount != size_t(SkipReason::kCount) + 4) return 123;
    if (std::strcmp(ShadowSkipName(uint8_t(SkipReason::kBadIndex)), "bad-index") != 0 ||
        std::strcmp(ShadowSkipName(uint8_t(SkipReason::kSkinUnsupported)), "skin-unsupported") != 0 ||
        std::strcmp(ShadowSkipName(uint8_t(ShadowSkip::kNoCaster)), "no-caster") != 0 ||
        std::strcmp(ShadowSkipName(uint8_t(ShadowSkip::kAlphaInputs)), "alpha-inputs") != 0 ||
        std::strcmp(ShadowSkipName(uint8_t(ShadowSkip::kStateUnread)), "state-unread") != 0 ||
        std::strcmp(ShadowSkipName(uint8_t(ShadowSkip::kOverLimit)), "over-limit") != 0 ||
        std::strcmp(ShadowSkipName(uint8_t(ShadowSkip::kCount)), "?") != 0 || std::strcmp(ShadowSkipName(255), "?") != 0) {
      return 124;
    }
    // What a caster's record holds in fields it already has: the 0x2202 word (alpha test, its
    // function, alpha-to-mask and its offsets), the reference, the cull and the offset enables.
    DrawInputs s = Good();
    s.state_words.read = true;
    s.state_words.color_control = 0xAA00001C;  // the leaf cards: test greater, alpha-to-mask, offsets 0xAA
    s.state_words.alpha_ref = 0;
    s.state_words.depth_control = 0x00708766;
    s.state_words.su_sc_mode = 0x00219800;     // no cull, the offset on for both faces
    s.state_words.color_mask = 0;
    s.state_words.vport_z_scale = 0xBF800000;  // -1: the reversed range
    s.state_words.vte_cntl = 0x0000043F;
    const DrawRecord leaf = AssembleRecord(s, 3);
    const RenderState st = DecodeRenderState(leaf.state_words);
    if (leaf.skip != SkipReason::kNone || leaf.state_words.color_control != 0xAA00001C) return 125;
    if (!st.valid || !st.alpha_test || st.alpha_func != Compare::kGreater || st.alpha_ref != 0.0f || !st.alpha_to_mask ||
        st.alpha_to_mask_offsets != 0xAA || st.cull != Cull::kNone || st.depth_func != Compare::kGreaterEqual ||
        !st.depth_write || !st.depth_reversed || st.color_mask != 0) {
      return 126;
    }
    // A shadow draw is never "depth-only" (that reason is the main scene's: a pre-pass draw).
    if (s.depth_only) return 127;
  }
  {
    // The sun layer's tree occluder (frame-map section 17, "The layer's depth place"): the game draws a late
    // leaf draw of its scene again into the layer's depth, with the same index buffer, range, vertex buffer
    // and transform rows and another vertex shader. FindOccluderTwin names the scene's record it redraws.
    static float other_bank[256 * 4] = {};
    static const TransformInfo t{4, TransformLayout::kDot, -1};
    for (int i = 0; i < 16; ++i) other_bank[4 * 4 + i] = float(i + 1);
    other_bank[4 * 4 + 3] = 99.0f;  // (another instance of the mesh: one transform component apart)
    DrawInputs leaf = Good();
    leaf.count = 18722;
    leaf.ib = {0x1C2E9380, 37444};
    leaf.vb = {0x1C25E780, 393336};
    leaf.vs_hash = 0xA5846836C90E1192ull;
    DrawInputs instance = leaf;      // the same mesh before the depth copy, at another place
    instance.transform = &t;
    instance.bank = other_bank;
    DrawInputs occluder_in = leaf;   // the occluder: the late draw's geometry with the occluder's vertex shader
    occluder_in.vs_hash = 0x432563420047C96Cull;
    const DrawRecord occluder = AssembleRecord(occluder_in, 0);
    if (occluder.skip != SkipReason::kNone) return 128;
    std::vector<DrawRecord> draws;
    draws.push_back(AssembleRecord(Good(), 0));
    draws.push_back(AssembleRecord(instance, 1));
    draws.push_back(AssembleRecord(Good(), 2));
    draws.push_back(AssembleRecord(leaf, 3));
    draws.push_back(AssembleRecord(Good(), 4));
    uint32_t at = 77;
    if (FindOccluderTwin(draws.data(), draws.size(), occluder, &at) != OccluderTwin::kFound || at != 3) return 129;
    // Each thing that makes it another draw: no twin, and the index is left as it was.
    auto alone = [&](DrawRecord changed) {
      uint32_t index = 77;
      return FindOccluderTwin(draws.data(), draws.size(), changed, &index) == OccluderTwin::kNone && index == 77;
    };
    DrawRecord c = occluder;
    c.ib.phys_addr += 2;
    if (!alone(c)) return 130;
    c = occluder; c.ib.size -= 2;
    if (!alone(c)) return 130;
    c = occluder; c.vb.phys_addr += 4;
    if (!alone(c)) return 130;
    c = occluder; c.vb.size += 4;
    if (!alone(c)) return 130;
    c = occluder; c.start = 3;
    if (!alone(c)) return 130;
    c = occluder; c.count -= 1;
    if (!alone(c)) return 130;
    c = occluder; c.base_vertex = 1;
    if (!alone(c)) return 130;
    c = occluder; c.prim = c.prim + 1;
    if (!alone(c)) return 130;
    c = occluder; c.index32 = true;
    if (!alone(c)) return 130;
    c = occluder; c.indexed = false;
    if (!alone(c)) return 130;
    c = occluder; c.layout = TransformLayout::kCombine;
    if (!alone(c)) return 130;
    for (int i = 0; i < 16; ++i) {
      c = occluder;
      c.rows[i] += 0.5f;
      if (!alone(c)) return 131;
    }
    // An occluder that is no record (it could not be read), or one whose positions are built another way:
    // no twin. A scene record that is skipped is no twin either: nothing of it is drawn.
    c = occluder; c.skip = SkipReason::kNoStream;
    if (!alone(c)) return 132;
    c = occluder; c.instances.active = true;
    if (!alone(c)) return 132;
    c = occluder; c.skin.active = true;
    if (!alone(c)) return 132;
    draws[3].skip = SkipReason::kDepthOnly;
    if (!alone(occluder)) return 133;
    draws[3].skip = SkipReason::kNone;
    // Two records it could be: ambiguous, and no index.
    draws.push_back(AssembleRecord(leaf, 5));
    at = 77;
    if (FindOccluderTwin(draws.data(), draws.size(), occluder, &at) != OccluderTwin::kAmbiguous || at != 77) return 134;
    // No records, no array, nowhere to put the index.
    if (FindOccluderTwin(draws.data(), 0, occluder, &at) != OccluderTwin::kNone) return 135;
    if (FindOccluderTwin(nullptr, 4, occluder, &at) != OccluderTwin::kNone) return 135;
    if (FindOccluderTwin(draws.data(), 4, occluder, nullptr) != OccluderTwin::kNone) return 135;
    static_assert(kNoDepthPlace == 0xFFFFFFFFu && kTreeOccluderPs == 0xF1EB2497108F159Dull && kMaxLayerOccluders == 16);
    // The drawing thread hands the frame's builder one call past the bound (the builder counts that one
    // failed: the frame then has no place) and looks at none after it (final code review, Minor 7).
    if (!LayerOccluderLooked(0) || !LayerOccluderLooked(kMaxLayerOccluders - 1) || !LayerOccluderLooked(kMaxLayerOccluders)) return 136;
    if (LayerOccluderLooked(kMaxLayerOccluders + 1) || LayerOccluderLooked(~0u)) return 137;
  }
  // 151 (sub-project 13, plan Task 9): the water records are a side block of their own; the draw record a water
  // draw makes is the one of before, byte for byte in size and in where its last member lies. A water record
  // names its draw by DrawRecord::seq, and what it does not carry (the state words with the alpha test's
  // reference) is that draw record's.
  {
    static_assert(sizeof(DrawRecord) == 616 && offsetof(DrawRecord, wind_index) == 614, "the water sub-project must not move the draw record");
    static_assert(sizeof(WaterRecord) == 584 && offsetof(WaterRecord, place) == 16 && offsetof(WaterRecord, vs) == 32 &&
                  offsetof(WaterRecord, ps) == 96 && offsetof(WaterRecord, fetch) == 480 && offsetof(WaterRecord, fog_base) == 576);
    DrawInputs in = Good();
    in.state_words.read = true;
    in.state_words.alpha_ref = 0x3D70F0F2u;
    const DrawRecord r = AssembleRecord(in, 1628);
    WaterRecord w{};
    w.place = r.seq;
    if (w.place != 1628 || r.state_words.alpha_ref != 0x3D70F0F2u || sizeof(r) != 616 || sizeof(w) != 584) return 151;
  }
  std::puts("PASS: record assembly with instance sets, instance range check and cap, distance cut, skin, render state, depth-only, side indices, shadow casters, occluder twin, the sizes beside the water record's");
  return 0;
}
