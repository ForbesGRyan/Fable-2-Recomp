// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/instance_expand.h"
#include <cmath>
#include <cstring>
#include <vector>

using namespace fable2::native::capture;

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
static void PutBeF(uint8_t* p, float f) {
  uint32_t u; std::memcpy(&u, &f, 4);
  p[0] = uint8_t(u >> 24); p[1] = uint8_t(u >> 16); p[2] = uint8_t(u >> 8); p[3] = uint8_t(u);
}
static void PutRow(uint8_t* p, float a, float b, float c, float d) {
  PutBeF(p, a); PutBeF(p + 4, b); PutBeF(p + 8, c); PutBeF(p + 12, d);
}

int main() {
  // 4 vertices per copy, first copy 2, rounding bias 0.5 (shader 0x8123...:
  // copy = trunc((i + bias) * inv_count) + trunc(first), vertex = i - trunc(...) * count).
  InstanceSet s;
  s.active = true;
  s.inv_count = 0.25f; s.count = 4.0f; s.first = 2.0f; s.bias = 0.5f;
  uint32_t copy = 0, vertex = 0;
  if (!InstanceIndex(s, 0, &copy, &vertex) || copy != 2 || vertex != 0) return 1;
  if (!InstanceIndex(s, 3, &copy, &vertex) || copy != 2 || vertex != 3) return 2;
  if (!InstanceIndex(s, 4, &copy, &vertex) || copy != 3 || vertex != 0) return 3;
  if (!InstanceIndex(s, 5, &copy, &vertex) || copy != 3 || vertex != 1) return 4;
  // The bias matters when 1/count is not exact: 3 vertices per copy, inv 0.3333.
  InstanceSet t = s;
  t.inv_count = 0.3333f; t.count = 3.0f; t.first = 0.0f;
  if (!InstanceIndex(t, 3, &copy, &vertex) || copy != 1 || vertex != 0) return 5;
  if (!InstanceIndex(t, 2, &copy, &vertex) || copy != 0 || vertex != 2) return 6;
  // Garbage constants never produce a mapping.
  InstanceSet g = s;
  g.inv_count = std::nanf("");
  if (InstanceIndex(g, 1, &copy, &vertex)) return 7;
  g = s; g.first = 1e9f;
  if (InstanceIndex(g, 1, &copy, &vertex)) return 8;
  g = s; g.first = -3.0f;
  if (InstanceIndex(g, 1, &copy, &vertex)) return 9;

  // --- bounds (capture-side) ---
  if (!InstanceBoundsOk(s, 7, 4, 4)) return 10;        // copies 2..3 of 4, vertices 0..3 of 4
  if (InstanceBoundsOk(s, 8, 4, 4)) return 11;         // index 8 -> copy 4: past the stream
  if (InstanceBoundsOk(s, 7, 4, 3)) return 12;         // 4 vertices per copy, mesh has 3
  g = s; g.count = 0.0f;
  if (InstanceBoundsOk(g, 7, 4, 4)) return 13;
  g = s; g.inv_count = 0.5f;                           // disagrees with count 4
  if (InstanceBoundsOk(g, 7, 4, 4)) return 14;
  g = s; g.inv_count = 0.0f;
  if (InstanceBoundsOk(g, 7, 4, 4)) return 15;

  // --- expansion: float4 rows, 48 bytes per copy (rows at 0, 16, 32) ---
  for (int k = 0; k < 3; ++k) {
    s.rows[k].format = PosFormat::kFloat4;
    s.rows[k].stride_bytes = 48;
    s.rows[k].offset_bytes = uint32_t(16 * k);
  }
  s.offset[0] = 100.0f; s.offset[1] = 0.0f; s.offset[2] = 0.0f;
  std::vector<uint8_t> rows(4 * 48, 0);
  // Copy 2: translate x by 10. Copy 3: scale by 2.
  PutRow(&rows[2 * 48], 1, 0, 0, 10); PutRow(&rows[2 * 48 + 16], 0, 1, 0, 0); PutRow(&rows[2 * 48 + 32], 0, 0, 1, 0);
  PutRow(&rows[3 * 48], 2, 0, 0, 0); PutRow(&rows[3 * 48 + 16], 0, 2, 0, 0); PutRow(&rows[3 * 48 + 32], 0, 0, 2, 0);
  if (InstanceCopies(s) != 0) return 16;               // no stream attached yet
  s.rows_size = uint32_t(rows.size());
  if (InstanceCopies(s) != 4) return 17;
  const Float4 mesh[4] = {{1, 2, 3, 1}, {0, 1, 0, 1}, {5, 5, 5, 1}, {-1, 0, 2, 1}};
  std::vector<Float4> out(10);
  if (!ExpandInstances(mesh, 4, rows.data(), rows.size(), s, 10, out.data())) return 18;
  // i 0: copy 2, vertex 0 -> (1 + 10 + 100, 2, 3).
  if (!Near(out[0].x, 111) || !Near(out[0].y, 2) || !Near(out[0].z, 3) || !Near(out[0].w, 1)) return 19;
  // i 5: copy 3, vertex 1 -> (0 + 100, 2, 0).
  if (!Near(out[5].x, 100) || !Near(out[5].y, 2) || !Near(out[5].z, 0)) return 20;
  // i 8, 9: copy 4 is past the stream -> NaN (culled), no out-of-range read.
  if (!std::isnan(out[8].x) || !std::isnan(out[9].x)) return 21;
  // A mesh shorter than the vertices per copy: those vertices are NaN.
  if (!ExpandInstances(mesh, 3, rows.data(), rows.size(), s, 8, out.data())) return 22;
  if (!std::isnan(out[3].x) || std::isnan(out[2].x)) return 23;
  if (ExpandInstances(mesh, 4, rows.data(), rows.size(), s, 0, out.data())) return 24;  // nothing to build
  if (ExpandInstances(mesh, 4, nullptr, 0, s, 4, out.data())) return 25;

  // --- flat UVs follow the mesh vertex ---
  const Float2 uv[4] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
  std::vector<Float2> fuv(10);
  ExpandInstanceUvs(uv, 4, s, 10, fuv.data());
  if (!Near(fuv[5].u, 1) || !Near(fuv[5].v, 0) || !Near(fuv[7].u, 1) || !Near(fuv[7].v, 1)) return 26;

  // --- row selection from the decoded fetches ---
  // Fetches: 0-2 instance rows (slot 94, stride 7 dwords, half4 at 0/2/4), 3 mesh position (slot 95).
  std::vector<VertexFetch> f(4);
  for (int k = 0; k < 3; ++k) {
    f[k].format = 32; f[k].fetch_slot = 94; f[k].stride_dwords = 7; f[k].offset_dwords = 2 * k;
    f[k].mini = k != 0;
  }
  f[3].format = 32; f[3].fetch_slot = 95; f[3].stride_dwords = 6;
  InstanceSpec spec{{0, 1, 2}, {0, 0, 0}, 48, 49, 50, 0.5f, 28};
  InstanceSet sel;
  uint32_t slot = 0;
  if (!SelectInstanceRows(f, spec, 95, &sel, &slot) || slot != 94) return 27;
  if (sel.rows[1].offset_bytes != 8 || sel.rows[2].stride_bytes != 28 || sel.bias != 0.5f) return 28;
  // A swizzle override replaces the row fetch's own.
  InstanceSpec swz = spec; swz.row_swizzle[1] = 0xAC1;
  if (!SelectInstanceRows(f, swz, 95, &sel, &slot) || sel.rows[1].swizzle != 0xAC1) return 29;
  // Rejections: rows in the mesh's own stream, a missing fetch, rows from two streams, a non-position format.
  if (SelectInstanceRows(f, spec, 94, &sel, &slot)) return 30;
  InstanceSpec bad = spec; bad.row_fetch[2] = 9;
  if (SelectInstanceRows(f, bad, 95, &sel, &slot)) return 31;
  std::vector<VertexFetch> two = f; two[2].fetch_slot = 93;
  if (SelectInstanceRows(two, spec, 95, &sel, &slot)) return 32;
  std::vector<VertexFetch> fmt = f; fmt[1].format = 6;
  if (SelectInstanceRows(fmt, spec, 95, &sel, &slot)) return 33;

  // --- bounds: every index maps inside its own copy, not only the largest ---
  // inv_count 0.2475 agrees with count 4 within 1%, but t = trunc((i + 0.5) *
  // 0.2475) falls behind i / 4 from index 52 on: the shader's own math sends
  // index 399 to copy 98, vertex 7 of a 4-vertex mesh.
  InstanceSet d = s;
  d.first = 0.0f; d.inv_count = 0.2475f;
  if (!InstanceIndex(d, 399, &copy, &vertex) || copy != 98 || vertex != 7) return 34;
  if (InstanceBoundsOk(d, 399, 100, 4)) return 35;
  if (InstanceBoundsOk(d, 399, 100, 400)) return 36;   // nor with a mesh that happens to hold a vertex 7
  if (!InstanceBoundsOk(d, 51, 100, 4)) return 37;     // indices 0..51 map exactly
  if (InstanceBoundsOk(d, 52, 100, 4)) return 38;      // index 52 -> copy 12, vertex 4
  // An inverse that runs ahead: the largest index (52, first of copy 13) maps
  // well, but index 51 is taken into copy 13 too (vertex -1).
  d.inv_count = 0.2525f;
  if (!InstanceIndex(d, 52, &copy, &vertex) || copy != 13 || vertex != 0) return 39;
  if (InstanceIndex(d, 51, &copy, &vertex)) return 40;
  if (InstanceBoundsOk(d, 52, 100, 4)) return 41;
  if (!InstanceBoundsOk(d, 50, 100, 4)) return 42;
  // The rounded inverse of the index-math case above (count 3, inv_count
  // 0.3333f, bias 0.5) maps exactly for indices 0..5000, that is 5001 indices
  // or 1667 copies; index 5001 is the first it sends to the wrong copy.
  if (!InstanceBoundsOk(t, 5000, 2000, 3)) return 43;
  if (InstanceBoundsOk(t, 5001, 2000, 3)) return 44;
  // The float nearest 1/3 has no such limit: exact up to the copy cap.
  InstanceSet e = t;
  e.inv_count = 1.0f / 3.0f;
  if (!InstanceBoundsOk(e, 3 * kMaxInstanceCopies - 1, kMaxInstanceCopies, 3)) return 45;
  if (InstanceBoundsOk(e, 3 * kMaxInstanceCopies, 70000, 3)) return 46;
  // Large indices: index + 0.5 is exact in a float below 2^23 only. Past it
  // the sum rounds up, and the last index of a copy lands in the next one.
  InstanceSet w = t;
  w.count = 1024.0f; w.inv_count = 1.0f / 1024.0f;
  if (!InstanceBoundsOk(w, 8388607, 20000, 1024)) return 47;    // copy 8191, vertex 1023
  if (InstanceIndex(w, 16777215, &copy, &vertex)) return 48;    // t = 16384: vertex -1
  if (InstanceBoundsOk(w, 16777215, 20000, 1024)) return 49;
  return 0;
}
