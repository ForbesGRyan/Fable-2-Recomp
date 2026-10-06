// Synthetic standalone test for record assembly with instancing; no game or GPU.
#include "../../src/native/capture/draw_record.h"
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
      uint8_t(SkipReason::kSkinUnsupported) != 9 || uint8_t(SkipReason::kCount) != 10) {
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
  std::puts("PASS: record assembly with instance sets, instance range check and cap, distance cut, skin");
  return 0;
}
