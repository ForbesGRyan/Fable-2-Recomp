// Standalone: clang++ -std=c++23 test_wind_decode.cpp
#include "../../src/native/render/wind_decode.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
using namespace fable2::native;
using capture::WindFamily;
using capture::WindInputs;
using render::WindBuild;
using render::WindExtent;

static void PutHalf(std::vector<uint8_t>& s, size_t at, uint16_t h) {
  s[at] = uint8_t(h >> 8);
  s[at + 1] = uint8_t(h);
}
static void PutFloat(std::vector<uint8_t>& s, size_t at, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  for (int k = 0; k < 4; ++k) s[at + size_t(k)] = uint8_t(u >> (24 - 8 * k));
}
// Halves used below: their bits.
static constexpr uint16_t kH0 = 0x0000, kH05 = 0x3800, kH1 = 0x3C00, kH2 = 0x4000, kH3 = 0x4200, kH4 = 0x4400, kHm15 = 0xBE00,
                          kHNaN = 0x7E00, kHInf = 0x7C00;

static capture::LightElement Element(uint32_t size, uint16_t offset, uint16_t stride, uint8_t format, uint8_t source, uint8_t count) {
  capture::LightElement e;
  e.vb = {0x1000000u, size};
  e.offset_bytes = offset;
  e.stride_bytes = stride;
  e.format = format;
  e.source = source;
  e.source_count = count;
  // as the fetches have them: the halves signed and not normalised, the bytes unsigned and normalised; endian 8in32
  e.flags = uint8_t((format == 6 ? capture::kLightElementNormalized : capture::kLightElementSigned) | (2u << capture::kLightElementEndianShift));
  return e;
}

static bool Near(float a, float b) { return std::fabs(a - b) <= 1e-6f; }

int main() {
  using render::WindWordsNeeded;
  using render::WindWordsPerVertex;
  // The words a vertex has (ledger ruling 30: a leaf ten) and what a draw reads.
  static_assert(WindWordsPerVertex(WindFamily::kTrunk) == 4 && WindWordsPerVertex(WindFamily::kLeaf) == 10 &&
                WindWordsPerVertex(WindFamily::kPlant) == 2 && WindWordsPerVertex(WindFamily::kNone) == 0 &&
                WindWordsPerVertex(WindFamily::kCount) == 0);
  static_assert(WindWordsNeeded(WindFamily::kTrunk, 3) == 12 && WindWordsNeeded(WindFamily::kLeaf, 3) == 30 &&
                WindWordsNeeded(WindFamily::kPlant, 3) == 6);
  static_assert(WindWordsNeeded(WindFamily::kLeaf, 0x20000000u) == 0);            // past 32 bits: none
  static_assert(WindWordsNeeded(WindFamily::kPlant, 0x7FFFFFFFu) == 0xFFFFFFFEu && WindWordsNeeded(WindFamily::kPlant, 0x80000000u) == 0);
  static_assert(WindWordsNeeded(WindFamily::kTrunk, 0x3FFFFFFFu) == 0xFFFFFFFCu && WindWordsNeeded(WindFamily::kTrunk, 0x40000000u) == 0);
  static_assert(WindWordsNeeded(WindFamily::kNone, 5) == 0 && WindWordsNeeded(WindFamily::kLeaf, 0) == 0);
  const char* names[] = {"none", "element", "stream", "value", "upload"};
  if (size_t(WindBuild::kCount) != 5) return 1;
  for (size_t i = 0; i < 5; ++i) {
    if (std::strcmp(render::WindBuildName(WindBuild(i)), names[i]) != 0) return 1;
  }
  const float kNaN = std::numeric_limits<float>::quiet_NaN();

  // --- a trunk: the 8_8_8_8 of three vertices, twelve values byte / 255 in the element's source order ---
  {
    const uint32_t stride = 12;
    std::vector<uint8_t> s(3 * stride, 0xEE);
    const uint8_t bytes[3][4] = {{10, 20, 30, 40}, {0, 255, 128, 1}, {200, 100, 50, 25}};   // in memory: source w, z, y, x
    for (int v = 0; v < 3; ++v) std::memcpy(&s[size_t(v) * stride + 4], bytes[v], 4);
    WindInputs w;
    w.family = WindFamily::kTrunk;
    w.element[capture::kWindWeights] = Element(uint32_t(s.size()), 4, stride, 6, 0xE4, 4);   // source x, y, z, w
    const uint8_t* streams[3] = {nullptr, s.data(), nullptr};
    size_t sizes[3] = {0, s.size(), 0};
    float out[16];
    for (float& f : out) f = -7.0f;
    WindExtent ext;
    ext.length_max = 5.0f;
    if (render::BuildTreeWindWords(w, streams, sizes, nullptr, 3, out, &ext) != WindBuild::kNone) return 10;
    for (int v = 0; v < 3; ++v) {
      for (int k = 0; k < 4; ++k) {
        if (!Near(out[v * 4 + k], float(bytes[v][3 - k]) / 255.0f)) return 11;
      }
    }
    if (out[12] != -7.0f) return 12;                                           // nothing past the twelve
    if (!Near(ext.weight_max, 1.0f) || ext.length_max != 0.0f) return 13;      // (255 of vertex 1; a trunk has no length)
    // the other order under another source: w, z, y, x is the memory's order
    w.element[capture::kWindWeights].source = 0x1B;
    if (render::BuildTreeWindWords(w, streams, sizes, nullptr, 3, out, &ext) != WindBuild::kNone) return 14;
    for (int v = 0; v < 3; ++v) {
      for (int k = 0; k < 4; ++k) {
        if (!Near(out[v * 4 + k], float(bytes[v][k]) / 255.0f)) return 14;
      }
    }
    // (the largest weight may be the fourth value: 40 of vertex 0 in this order)
    if (render::BuildTreeWindWords(w, streams, sizes, nullptr, 1, out, &ext) != WindBuild::kNone || !Near(ext.weight_max, 40.0f / 255.0f) ||
        !Near(out[3], 40.0f / 255.0f)) {
      return 14;
    }
    w.element[capture::kWindWeights].source = 0xE4;
    // two vertices alone: their largest weight
    if (render::BuildTreeWindWords(w, streams, sizes, nullptr, 1, out, &ext) != WindBuild::kNone || !Near(ext.weight_max, 40.0f / 255.0f)) return 15;
    // without an extent the words are the same
    for (float& f : out) f = -7.0f;
    if (render::BuildTreeWindWords(w, streams, sizes, nullptr, 3, out, nullptr) != WindBuild::kNone || !Near(out[11], 200.0f / 255.0f)) return 15;
    // one vertex past the stream; the last vertex cut short by a byte; no stream; no vertex; nowhere to write
    for (float& f : out) f = -7.0f;
    if (render::BuildTreeWindWords(w, streams, sizes, nullptr, 4, out, &ext) != WindBuild::kStream) return 16;
    if (out[12] != -7.0f) return 16;
    sizes[1] = s.size() - 5;   // (the element ends 4 bytes before the vertex does)
    for (float& f : out) f = -7.0f;
    if (render::BuildTreeWindWords(w, streams, sizes, nullptr, 3, out, &ext) != WindBuild::kStream || out[8] != -7.0f) return 16;
    sizes[1] = s.size() - 4;
    if (render::BuildTreeWindWords(w, streams, sizes, nullptr, 3, out, &ext) != WindBuild::kNone) return 16;
    sizes[1] = s.size();
    streams[1] = nullptr;
    if (render::BuildTreeWindWords(w, streams, sizes, nullptr, 3, out, &ext) != WindBuild::kStream) return 17;
    streams[1] = s.data();
    if (render::BuildTreeWindWords(w, streams, sizes, nullptr, 0, out, &ext) != WindBuild::kStream) return 17;
    if (render::BuildTreeWindWords(w, streams, sizes, nullptr, 3, nullptr, &ext) != WindBuild::kStream) return 17;
    if (render::BuildTreeWindWords(w, nullptr, sizes, nullptr, 3, out, &ext) != WindBuild::kStream) return 17;
    if (render::BuildTreeWindWords(w, streams, nullptr, nullptr, 3, out, &ext) != WindBuild::kStream) return 17;
    // an element no reader decodes: an unknown format, signed bytes, another endian, three components
    // where the weights are four, an element that does not lie in its vertex; and a family without elements
    auto refused = [&](const capture::LightElement& e, WindFamily family = WindFamily::kTrunk) {
      WindInputs b = w;
      b.family = family;
      b.element[capture::kWindWeights] = e;
      for (float& f : out) f = -7.0f;
      return render::BuildTreeWindWords(b, streams, sizes, nullptr, 3, out, &ext) == WindBuild::kElement && out[0] == -7.0f;
    };
    capture::LightElement e = w.element[capture::kWindWeights];
    e.format = 7;                                                    if (!refused(e)) return 18;
    e = w.element[capture::kWindWeights]; e.format = 32;             if (!refused(e)) return 18;   // (halves are not the weights)
    e = w.element[capture::kWindWeights]; e.flags |= capture::kLightElementSigned;  if (!refused(e)) return 18;
    e = w.element[capture::kWindWeights]; e.flags = uint8_t(capture::kLightElementNormalized | (1u << capture::kLightElementEndianShift));
                                                                     if (!refused(e)) return 18;
    e = w.element[capture::kWindWeights]; e.source = 0x24; e.source_count = 3;      if (!refused(e)) return 18;
    e = w.element[capture::kWindWeights]; e.offset_bytes = 10;       if (!refused(e)) return 18;
    e = w.element[capture::kWindWeights]; e.stride_bytes = 0;        if (!refused(e)) return 18;
    e = w.element[capture::kWindWeights];
    if (!refused(e, WindFamily::kPlant) || !refused(e, WindFamily::kNone) || !refused(e, WindFamily::kCount)) return 19;
  }

  // --- a leaf: the pivot (three halves, read as the position is: source y, x, w), the four weights, the three
  // flutter values (source x, y, z); ten words a vertex, and the largest distance of a vertex from its pivot ---
  {
    const uint32_t stride = 36;
    std::vector<uint8_t> s(3 * stride, 0);
    // the pivots' halves in memory (source y, x, w, z): what the reader gives is the first three
    const uint16_t pivot[3][4] = {{kH1, kH2, kH3, kHNaN}, {kH05, kHm15, kH0, kH4}, {kH4, kH4, kH4, kH0}};
    const float q[3][3] = {{1.0f, 2.0f, 3.0f}, {0.5f, -1.5f, 0.0f}, {4.0f, 4.0f, 4.0f}};
    const uint8_t weights[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
    const uint8_t flutter[3][4] = {{99, 13, 14, 15}, {99, 16, 17, 18}, {99, 19, 20, 21}};   // (source w, the first byte, is not read)
    for (int v = 0; v < 3; ++v) {
      for (int k = 0; k < 4; ++k) PutHalf(s, size_t(v) * stride + 20 + 2 * size_t(k), pivot[v][k]);
      std::memcpy(&s[size_t(v) * stride + 28], weights[v], 4);
      std::memcpy(&s[size_t(v) * stride + 32], flutter[v], 4);
    }
    WindInputs w;
    w.family = WindFamily::kLeaf;
    w.element[capture::kWindPivot] = Element(uint32_t(s.size()), 20, stride, 32, 0x31, 3);     // y, x, w
    w.element[capture::kWindWeights] = Element(uint32_t(s.size()), 28, stride, 6, 0xE4, 4);
    w.element[capture::kWindFlutter] = Element(uint32_t(s.size()), 32, stride, 6, 0x24, 3);    // x, y, z
    const uint8_t* streams[3] = {s.data(), s.data(), s.data()};
    size_t sizes[3] = {s.size(), s.size(), s.size()};
    // the positions as the record's buffer has them: vertex 0 at its pivot, vertex 1 three units along x and four
    // along z from it, vertex 2 one unit from it
    const capture::Float4 pos[3] = {{1.0f, 2.0f, 3.0f, 1.0f}, {3.5f, -1.5f, 4.0f, 1.0f}, {4.0f, 5.0f, 4.0f, 1.0f}};
    float out[40];
    for (float& f : out) f = -7.0f;
    WindExtent ext;
    if (render::BuildTreeWindWords(w, streams, sizes, pos, 3, out, &ext) != WindBuild::kNone) return 30;
    for (int v = 0; v < 3; ++v) {
      const float* o = out + v * 10;
      if (o[0] != q[v][0] || o[1] != q[v][1] || o[2] != q[v][2]) return 31;
      for (int k = 0; k < 4; ++k) {
        if (!Near(o[3 + k], float(weights[v][3 - k]) / 255.0f)) return 32;
      }
      for (int k = 0; k < 3; ++k) {
        if (!Near(o[7 + k], float(flutter[v][3 - k]) / 255.0f)) return 33;
      }
    }
    if (out[30] != -7.0f) return 34;
    if (!Near(ext.length_max, 5.0f) || !Near(ext.weight_max, 12.0f / 255.0f)) return 35;
    // (the fourth half of the first pivot is no number and is not read: the reader takes three)
    // a pivot's half that is no number, or not finite: no words, and nothing written from the failing vertex on
    for (uint16_t bad : {kHNaN, kHInf, uint16_t(kHInf | 0x8000)}) {
      std::vector<uint8_t> b = s;
      PutHalf(b, stride + 22, bad);
      const uint8_t* bs[3] = {b.data(), b.data(), b.data()};
      for (float& f : out) f = -7.0f;
      if (render::BuildTreeWindWords(w, bs, sizes, pos, 3, out, &ext) != WindBuild::kValue) return 36;
      for (int k = 10; k < 40; ++k) {
        if (out[k] != -7.0f) return 36;
      }
    }
    // a position that is no number (its distance from the pivot is none): no words either
    {
      capture::Float4 bad[3] = {pos[0], pos[1], pos[2]};
      bad[2].y = kNaN;
      for (float& f : out) f = -7.0f;
      if (render::BuildTreeWindWords(w, streams, sizes, bad, 3, out, &ext) != WindBuild::kValue || out[20] != -7.0f) return 37;
      // without positions a leaf's reach cannot be known: no words
      if (render::BuildTreeWindWords(w, streams, sizes, nullptr, 3, out, &ext) != WindBuild::kStream) return 37;
    }
    // each element alone: refused when it is not the family's, missing when its stream is
    for (uint32_t e : {uint32_t(capture::kWindPivot), uint32_t(capture::kWindWeights), uint32_t(capture::kWindFlutter)}) {
      WindInputs b = w;
      b.element[e].format = 16;
      if (render::BuildTreeWindWords(b, streams, sizes, pos, 3, out, &ext) != WindBuild::kElement) return 38;
      b = w;
      b.element[e].stride_bytes = 0;
      if (render::BuildTreeWindWords(b, streams, sizes, pos, 3, out, &ext) != WindBuild::kElement) return 38;
      const uint8_t* gone[3] = {s.data(), s.data(), s.data()};
      gone[e] = nullptr;
      if (render::BuildTreeWindWords(w, gone, sizes, pos, 3, out, &ext) != WindBuild::kStream) return 39;
      size_t cut[3] = {s.size(), s.size(), s.size()};
      cut[e] = 2 * stride + w.element[e].offset_bytes + 3;
      if (render::BuildTreeWindWords(w, streams, cut, pos, 3, out, &ext) != WindBuild::kStream) return 39;
    }
    {
      WindInputs b = w;
      b.element[capture::kWindPivot].source = 0xE4;   // four components: not a pivot
      b.element[capture::kWindPivot].source_count = 4;
      if (render::BuildTreeWindWords(b, streams, sizes, pos, 3, out, &ext) != WindBuild::kElement) return 38;
      b = w;
      b.element[capture::kWindFlutter].source = 0xE4;
      b.element[capture::kWindFlutter].source_count = 4;
      if (render::BuildTreeWindWords(b, streams, sizes, pos, 3, out, &ext) != WindBuild::kElement) return 38;
    }
    // the vertices the three streams all hold
    if (render::TreeWindVertices(w) != 3) return 40;
    {
      WindInputs b = w;
      b.element[capture::kWindFlutter].vb.size = 2 * stride + 35;   // the third vertex's flutter is cut
      if (render::TreeWindVertices(b) != 2) return 40;
      b.element[capture::kWindFlutter].vb.size = 2 * stride + 36;
      if (render::TreeWindVertices(b) != 3) return 40;
      b.element[capture::kWindPivot].vb.size = 27;
      if (render::TreeWindVertices(b) != 0) return 40;
      b = w;
      b.element[capture::kWindWeights].format = 7;
      if (render::TreeWindVertices(b) != 0) return 40;
      // the smallest of the three, whichever element it is; a stream that ends inside a vertex after the element
      b = w;
      b.element[capture::kWindWeights].vb.size = stride + 32;      // the second vertex's weights are whole, no third
      if (render::TreeWindVertices(b) != 2) return 41;
      b = w;
      b.element[capture::kWindPivot].vb.size = stride + 28;        // (64 bytes: one whole vertex and a pivot)
      if (render::TreeWindVertices(b) != 2) return 41;
      b.element[capture::kWindPivot].vb.size = stride + 27;
      if (render::TreeWindVertices(b) != 1) return 41;
      // an element no reader decodes holds no vertex: another number of components, no stride, signed bytes
      b = w;
      b.element[capture::kWindFlutter].source = 0xE4;
      b.element[capture::kWindFlutter].source_count = 4;
      if (render::TreeWindVertices(b) != 0) return 42;
      b = w;
      b.element[capture::kWindWeights].stride_bytes = 0;
      if (render::TreeWindVertices(b) != 0) return 42;
      b = w;
      b.element[capture::kWindWeights].flags |= capture::kLightElementSigned;
      if (render::TreeWindVertices(b) != 0) return 42;
      b = w;
      b.family = WindFamily::kTrunk;                 // a trunk asks for its weights alone
      b.element[capture::kWindPivot].vb.size = 0;
      if (render::TreeWindVertices(b) != 3) return 40;
      b.family = WindFamily::kPlant;
      if (render::TreeWindVertices(b) != 0) return 40;
    }
  }

  // --- a plant: two copies of a three-vertex mesh; a pair a flat position: (A3 + B3 of its copy, z of its vertex) ---
  {
    const capture::Float4 mesh[3] = {{0.0f, 0.0f, 0.125f, 1.0f}, {1.0f, 0.0f, 0.5f, 1.0f}, {0.0f, 1.0f, 0.875f, 1.0f}};
    const uint32_t stride = 48;
    std::vector<uint8_t> rows(2 * stride, 0);
    const float a3[2] = {10.0f, 1.5f}, b3[2] = {20.0f, -4.0f}, c3[2] = {77.0f, 88.0f};
    for (int copy = 0; copy < 2; ++copy) {
      PutFloat(rows, size_t(copy) * stride + 12, a3[copy]);    // the fourth component of the copy's first row
      PutFloat(rows, size_t(copy) * stride + 28, b3[copy]);    // ... of its second
      PutFloat(rows, size_t(copy) * stride + 44, c3[copy]);    // ... of its third: not read
      PutFloat(rows, size_t(copy) * stride + 0, 1.0f);
    }
    capture::InstanceSet s;
    s.active = true;
    s.rows_size = uint32_t(rows.size());
    for (int k = 0; k < 3; ++k) {
      s.rows[k].format = capture::PosFormat::kFloat4;
      s.rows[k].offset_bytes = 16u * uint32_t(k);
      s.rows[k].stride_bytes = stride;
    }
    s.count = 3.0f;
    s.inv_count = 1.0f / 3.0f;
    s.bias = 0.5f;
    s.flat_count = 6;
    float out[16];
    for (float& f : out) f = -7.0f;
    if (render::ExpandPlantWindWords(mesh, 3, rows.data(), rows.size(), s, 6, out) != WindBuild::kNone) return 50;
    for (int i = 0; i < 6; ++i) {
      const int copy = i / 3, vertex = i % 3;
      if (out[2 * i] != a3[copy] + b3[copy] || out[2 * i + 1] != mesh[vertex].z) return 51;
    }
    if (out[12] != -7.0f) return 52;
    // an index the mesh has no vertex for has no position either (ExpandInstances): zeros
    for (float& f : out) f = -7.0f;
    if (render::ExpandPlantWindWords(mesh, 2, rows.data(), rows.size(), s, 6, out) != WindBuild::kNone) return 53;
    if (out[4] != 0.0f || out[5] != 0.0f || out[10] != 0.0f || out[11] != 0.0f || out[2] != 30.0f || out[9] != 0.5f) return 53;
    // a copy past the instance stream: no words (all or nothing)
    for (float& f : out) f = -7.0f;
    if (render::ExpandPlantWindWords(mesh, 3, rows.data(), rows.size() - 1, s, 6, out) != WindBuild::kStream) return 54;
    if (render::ExpandPlantWindWords(mesh, 3, rows.data(), rows.size() - 1, s, 3, out) != WindBuild::kNone) return 54;   // (the first copy alone is whole)
    // (whichever of the three rows it is that lies past the stream: here the first row is stored last)
    {
      capture::InstanceSet t = s;
      t.rows[0].offset_bytes = 32;
      t.rows[2].offset_bytes = 0;
      if (render::ExpandPlantWindWords(mesh, 3, rows.data(), rows.size(), t, 6, out) != WindBuild::kNone) return 54;
      if (render::ExpandPlantWindWords(mesh, 3, rows.data(), rows.size() - 1, t, 6, out) != WindBuild::kStream) return 54;
    }
    // a seed or a height that is no number: no words
    {
      std::vector<uint8_t> bad = rows;
      PutFloat(bad, stride + 28, kNaN);
      if (render::ExpandPlantWindWords(mesh, 3, bad.data(), bad.size(), s, 6, out) != WindBuild::kValue) return 55;
      if (render::ExpandPlantWindWords(mesh, 3, bad.data(), bad.size(), s, 3, out) != WindBuild::kNone) return 55;   // (the draw does not name that copy)
      bad = rows;
      PutFloat(bad, 12, std::numeric_limits<float>::infinity());
      if (render::ExpandPlantWindWords(mesh, 3, bad.data(), bad.size(), s, 6, out) != WindBuild::kValue) return 55;
      bad = rows;                                    // (two numbers whose sum is none)
      PutFloat(bad, 12, 3.0e38f);
      PutFloat(bad, 28, 3.0e38f);
      if (render::ExpandPlantWindWords(mesh, 3, bad.data(), bad.size(), s, 6, out) != WindBuild::kValue) return 55;
      capture::Float4 bad_mesh[3] = {mesh[0], mesh[1], mesh[2]};
      bad_mesh[1].z = kNaN;
      if (render::ExpandPlantWindWords(bad_mesh, 3, rows.data(), rows.size(), s, 6, out) != WindBuild::kValue) return 55;
      bad_mesh[1].z = 0.5f;
      bad_mesh[1].x = kNaN;                          // (x and y are the position's: not read here)
      if (render::ExpandPlantWindWords(bad_mesh, 3, rows.data(), rows.size(), s, 6, out) != WindBuild::kNone) return 55;
    }
    // nothing to read or nowhere to write
    if (render::ExpandPlantWindWords(nullptr, 3, rows.data(), rows.size(), s, 6, out) != WindBuild::kStream ||
        render::ExpandPlantWindWords(mesh, 3, nullptr, rows.size(), s, 6, out) != WindBuild::kStream ||
        render::ExpandPlantWindWords(mesh, 3, rows.data(), rows.size(), s, 0, out) != WindBuild::kStream ||
        render::ExpandPlantWindWords(mesh, 3, rows.data(), rows.size(), s, 6, nullptr) != WindBuild::kStream) {
      return 56;
    }
    // The game's own bytes (capture native_discovery_20261010_182313, line 2538: grass 0x8123C16DBF583F92, copy
    // 168 of its instance stream; the review of Tasks 5 to 7, item 3): three rows of four halves, stride 28, read
    // under the stream's pair swap as source y, x, w, z (the instance entry's layout), so that a row's fourth
    // stored half is its fourth component. tools/xdk_sigmatch/wind_check.py (formula_inputs), which reads the
    // bytes its own way, gives A3 = 3.361328125 and B3 = 7.26171875 for this copy.
    {
      const uint8_t copy[28] = {0xA8, 0xAB, 0xC1, 0x57, 0x35, 0x07, 0x42, 0xB9, 0x41, 0x0E, 0x2C, 0xAB, 0x3B, 0x4F,
                                0x47, 0x43, 0xBB, 0x53, 0x34, 0xEC, 0x41, 0x05, 0xC4, 0x30, 0xFE, 0x75, 0x80, 0x92};
      capture::InstanceSet g;
      g.active = true;
      g.rows_size = 28;
      for (int k = 0; k < 3; ++k) {
        g.rows[k].format = capture::PosFormat::kHalf4;
        g.rows[k].swap16 = true;
        g.rows[k].swizzle = 1217;   // y, x, w, z
        g.rows[k].offset_bytes = 8u * uint32_t(k);
        g.rows[k].stride_bytes = 28;
      }
      g.count = 3.0f;
      g.inv_count = 1.0f / 3.0f;
      g.bias = 0.5f;
      g.flat_count = 3;
      float real[6];
      if (render::ExpandPlantWindWords(mesh, 3, copy, sizeof(copy), g, 3, real) != WindBuild::kNone) return 58;
      if (real[0] != 3.361328125f + 7.26171875f || real[1] != 0.125f || real[4] != real[0] || real[5] != 0.875f) return 58;
    }
    // an index the constants map to no copy: zeros (as its position is none)
    capture::InstanceSet odd = s;
    odd.first = -5.0f;
    for (float& f : out) f = -7.0f;
    if (render::ExpandPlantWindWords(mesh, 3, rows.data(), rows.size(), odd, 6, out) != WindBuild::kNone || out[0] != 0.0f || out[11] != 0.0f) return 57;
  }
  std::puts("PASS: wind words of trunks, leaves and instanced plants; their refusals; the extent");
  return 0;
}
