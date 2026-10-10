// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/quad_record.h"
#include "../../src/native/capture/post_block.h"
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

using namespace fable2::native::capture;

static const float kNaN = std::numeric_limits<float>::quiet_NaN();
static const float kInf = std::numeric_limits<float>::infinity();

static float Bits(uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}
static uint32_t BitsOf(float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof(bits));
  return bits;
}
static float Up(float v) { return std::nextafter(v, kInf); }
static float Down(float v) { return std::nextafter(v, -kInf); }

// Frame 1 of capture native_discovery_20261010_052158 (plan Task 10's run 3, towards the sun;
// out/native-evidence/post/t10_run3_quads.txt): the sun disc's quad, "indx" row n 55 with its
// "indxdata" row. Storage bytes as the game filled them (guest order), the vertex bank's c0..c3
// ("bank"), the pixel registers c20 and c31 ("pfull"), the fetch constant of slot 13 ("tf") and the
// state words ("rs": 0x2201, 0x2200, 0x2104, 0x2202, 0x210E).
static const char kSunDiscVerts[] =
    "C4CC22CE4331ACB944691F55000000003F800000C4BB68B443FFF0AB44691F553F8000003F800000"
    "C4B8DCDA42E7E096449C7C6A0000000000000000C4A822C043E11273449C7C6A3F80000000000000";
static const uint32_t kMatrixBits[16] = {0x3EBA04AA, 0x3F684A6D, 0x33BBAA18, 0xC3306FC9, 0x3F3E8525, 0xBE989184,
                                         0x3FC5551A, 0xC2B9462A, 0xBF52DBA3, 0x3EA8DAD8, 0x3EEC3766, 0x3DCDF642,
                                         0xBF52DA8E, 0x3EA8D9FA, 0x3EEC3630, 0x3E4D6100};
static const uint32_t kSunDiscC20[4] = {0x3BC20A12, 0x3D8FC764, 0x3DBABD2A, 0x3F800000};
static const uint32_t kSunDiscC31[4] = {0x3F606FAA, 0x3F30F4C4, 0x3EAEC826, 0x00000000};
static const uint32_t kSunDiscFetch[6] = {0x82000002, 0x14E80054, 0x001FE0FF, 0x00A80D10, 0x00000103, 0x14E90A18};

struct Inputs {
  const PostQuadEntry* entry = FindPostQuad(0x3E77B1AD4BC5B900ull);
  uint8_t vertices[80];
  uint16_t indices[6] = {0, 1, 2, 1, 3, 2};
  float view_proj[4][4];
  float ps[12][4] = {};
  uint32_t fetch[3][6] = {};
  QuadInputs in{};
  Inputs() {
    for (int i = 0; i < 80; ++i) {
      auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
      vertices[i] = uint8_t(digit(kSunDiscVerts[2 * i]) * 16 + digit(kSunDiscVerts[2 * i + 1]));
    }
    for (int i = 0; i < 16; ++i) view_proj[i / 4][i % 4] = Bits(kMatrixBits[i]);
    for (int i = 0; i < 4; ++i) {
      ps[0][i] = Bits(kSunDiscC20[i]);
      ps[1][i] = Bits(kSunDiscC31[i]);
    }
    std::memcpy(fetch[0], kSunDiscFetch, sizeof(kSunDiscFetch));
    in.vertices = vertices;
    in.vertex_bytes = sizeof(vertices);
    in.stride = 20;
    in.indices = indices;
    in.index_count = 6;
    in.view_proj = view_proj;
    in.ps = ps;
    in.ps_count = 2;
    in.fetch = fetch;
    in.fetch_count = 1;
    in.blend = 0x01010101;
    in.depth = 0x00708762;
    in.colour_mask = 0xF;
    in.colour_control = 0x0000000C;
    in.alpha_ref = 0x3F008081;
    in.vport_z_scale = 0xBF800000;  // ("rs" 2113 and 2206 of every captured furniture row: scale -1, VTE 0x43F)
    in.vte_cntl = 0x0000043F;
  }
  // Float `component` (0..4: x y z u v) of corner `corner`, stored high byte first.
  void Corner(int corner, int component, float v) {
    const uint32_t bits = BitsOf(v);
    for (int k = 0; k < 4; ++k) vertices[20 * corner + 4 * component + k] = uint8_t(bits >> (24 - 8 * k));
  }
  QuadRecord Make(QuadRecordReason* why = nullptr) const { return MakeQuadRecord(*entry, in, why); }
};

static bool AllZero(const QuadRecord& r) {
  static const QuadRecord zero{};
  return std::memcmp(&r, &zero, sizeof(r)) == 0;
}
// The record of `in` is refused for `want`, and is all zero.
static bool Refused(const Inputs& in, QuadRecordReason want) {
  QuadRecordReason why = QuadRecordReason::kNone;
  const QuadRecord r = in.Make(&why);
  return AllZero(r) && !QuadRecordValid(r) && why == want;
}

static int TableTests() {
  static_assert(kMaxQuadRecords == 24);
  static_assert(sizeof(QuadRecord) == 448, "the documents state the record's size");
  static_assert(size_t(PostQuadKind::kCount) == 8);
  static_assert(std::size(kPostQuadTable) == 8);
  // The eight kinds in the record's order, each with its machine-checked pair of shaders, where it
  // is drawn and how (tools/xdk_sigmatch/light_formulas.py, POST_QUAD_SHADERS).
  struct Want {
    uint64_t ps, vs;
    PostQuadKind kind;
    uint8_t layer;
    PostQuadTarget target;
    PostQuadBlend blend;
    bool test;
    uint8_t registers;
  };
  const uint64_t quad = 0x81BADE63A169E3DCull, cloud = 0xC304C6A1AF18B46Full;
  const Want want[8] = {
      {0x18D39DCEBEE4C2FCull, quad, PostQuadKind::kMoon, 0, PostQuadTarget::kScene, PostQuadBlend::kAlpha, true, 2},
      {0x61976295517D7015ull, cloud, PostQuadKind::kCloud, 0, PostQuadTarget::kScene, PostQuadBlend::kAlpha, true, 11},
      {0x04985A0F7296E130ull, quad, PostQuadKind::kGlare, 0, PostQuadTarget::kScene, PostQuadBlend::kAdd, false, 1},
      {0x8E4DBB9E9A63EF67ull, quad, PostQuadKind::kBeamSource, 1, PostQuadTarget::kLayer1, PostQuadBlend::kReplace, false, 1},
      {0xD72FD0903CCE7DD6ull, quad, PostQuadKind::kFlare, 1, PostQuadTarget::kLayer2, PostQuadBlend::kAdd, false, 1},
      {0x4B58837D42F08C89ull, quad, PostQuadKind::kMoonGlow, 1, PostQuadTarget::kLayer2, PostQuadBlend::kAlpha, true, 0},
      {0x3E77B1AD4BC5B900ull, quad, PostQuadKind::kSunDisc, 1, PostQuadTarget::kLayer2, PostQuadBlend::kAdd, true, 2},
      {0x3FF79524FCDB71C5ull, cloud, PostQuadKind::kCloudOccluder, 1, PostQuadTarget::kLayerDepth, PostQuadBlend::kDepth, true, 4},
  };
  for (int i = 0; i < 8; ++i) {
    const PostQuadEntry& e = kPostQuadTable[i];
    const Want& w = want[i];
    if (e.ps != w.ps || e.vs != w.vs || e.kind != w.kind || e.layer != w.layer || e.target != w.target || e.blend != w.blend ||
        e.alpha_test != w.test || e.register_count != w.registers || e.slot_count != 1 || e.slots[0] != 13) {
      return 1;
    }
    if (FindPostQuad(w.ps) != &e || int(e.kind) != i) return 2;
  }
  const uint8_t cloud_registers[11] = {20, 31, 46, 47, 51, 69, 70, 71, 77, 78, 79};
  if (std::memcmp(kPostQuadTable[1].registers, cloud_registers, sizeof(cloud_registers)) != 0) return 3;
  if (kPostQuadTable[6].registers[0] != 20 || kPostQuadTable[6].registers[1] != 31) return 4;
  // No other shader is a quad's: the post chain's own, the layer's depth, the tree occluder, the
  // previous frame, a vertex shader's hash, zero.
  for (uint64_t h : {kPostCompositePs, kPostBrightPs, kPostRadialPs, 0x5D6768847B0602B8ull, 0xF1EB2497108F159Dull,
                     0x686DE15FC84D8B73ull, quad, cloud, uint64_t(0)}) {
    if (FindPostQuad(h)) return 5;
  }
  const char* names[8] = {"moon", "cloud", "glare", "beam-source", "flare", "moon-glow", "sun-disc", "cloud-occluder"};
  for (int i = 0; i < 8; ++i) {
    if (std::string(PostQuadKindName(PostQuadKind(i))) != names[i]) return 6;
  }
  if (std::string(PostQuadKindName(PostQuadKind::kCount)) != "?") return 7;
  if (std::string(kQuadRecordReason) != "quad-record") return 8;
  return 0;
}

static int RecordTests() {
  const Inputs in;
  if (!in.entry) return 20;
  QuadRecordReason why = QuadRecordReason::kUnread;
  const QuadRecord r = in.Make(&why);
  if (!QuadRecordValid(r) || why != QuadRecordReason::kNone) return 21;
  if (r.ps_hash != 0x3E77B1AD4BC5B900ull || r.vs_hash != 0x81BADE63A169E3DCull || r.kind != PostQuadKind::kSunDisc || r.layer != 1 ||
      r.sequence != 0) {
    return 22;
  }
  // The corners as the report prints them: (-1633.09, 177.675, 932.49, 0, 1) (-1499.27, 511.88,
  // 932.49, 1, 1) (-1478.9, 115.939, 1251.89, 0, 0) (-1345.09, 450.144, 1251.89, 1, 0); by bits.
  const uint32_t corners[4][5] = {{0xC4CC22CE, 0x4331ACB9, 0x44691F55, 0x00000000, 0x3F800000},
                                  {0xC4BB68B4, 0x43FFF0AB, 0x44691F55, 0x3F800000, 0x3F800000},
                                  {0xC4B8DCDA, 0x42E7E096, 0x449C7C6A, 0x00000000, 0x00000000},
                                  {0xC4A822C0, 0x43E11273, 0x449C7C6A, 0x3F800000, 0x00000000}};
  for (int c = 0; c < 4; ++c) {
    for (int k = 0; k < 5; ++k) {
      if (BitsOf(r.corners[c][k]) != corners[c][k]) return 23;
    }
  }
  if (std::fabs(r.corners[0][0] + 1633.09f) > 0.01f || std::fabs(r.corners[3][2] - 1251.89f) > 0.01f) return 24;
  if (std::memcmp(r.view_proj, in.view_proj, sizeof(r.view_proj)) != 0) return 25;
  if (std::memcmp(r.ps, in.ps, sizeof(r.ps)) != 0 || r.ps[0][3] != 1.0f) return 26;
  if (std::memcmp(r.fetch, in.fetch, sizeof(r.fetch)) != 0) return 27;
  if (r.blend != 0x01010101 || r.depth != 0x00708762 || r.colour_mask != 0xF) return 28;
  // The draw's alpha test (ruling 23): on, greater, 0.502.
  if (r.alpha_control != 0xC || r.alpha_ref != Bits(0x3F008081) || !QuadAlphaTest(r)) return 29;

  // What the record does not take: registers and slots past the kind's counts are zero in it,
  // whatever the caller's arrays hold there.
  {
    Inputs more;
    for (int i = 2; i < 12; ++i) more.ps[i][0] = 7.0f;
    more.fetch[1][0] = more.fetch[2][5] = 0xFFFFFFFF;
    const QuadRecord m = more.Make();
    if (std::memcmp(&m, &r, sizeof(r)) != 0) return 30;
  }
  // The alpha test off: the reference register is stale (the sun disc's 0.502 at a glare's call),
  // so the record's reference is 0 and only the enable bit tells. A stale reference that is no
  // number does not refuse such a record.
  {
    Inputs glare;
    glare.entry = FindPostQuad(0x04985A0F7296E130ull);
    glare.in.ps_count = 1;
    glare.in.depth = 0x00708760;
    glare.in.colour_control = 0x00000004;
    QuadRecord g = glare.Make(&why);
    if (!QuadRecordValid(g) || g.kind != PostQuadKind::kGlare || g.layer != 0 || g.alpha_control != 0x4 || g.alpha_ref != 0.0f ||
        QuadAlphaTest(g)) {
      return 31;
    }
    glare.in.alpha_ref = BitsOf(kNaN);
    g = glare.Make(&why);
    if (!QuadRecordValid(g) || g.alpha_ref != 0.0f) return 32;
  }
  return 0;
}

static int RefusalTests() {
  // Nothing to read.
  {
    Inputs a;
    a.in.vertices = nullptr;
    if (!Refused(a, QuadRecordReason::kUnread)) return 40;
    Inputs b;
    b.in.indices = nullptr;
    if (!Refused(b, QuadRecordReason::kUnread)) return 41;
    Inputs c;
    c.in.view_proj = nullptr;
    if (!Refused(c, QuadRecordReason::kUnread)) return 42;
    Inputs d;
    d.in.ps = nullptr;
    if (!Refused(d, QuadRecordReason::kUnread)) return 43;
    Inputs e;
    e.in.fetch = nullptr;
    if (!Refused(e, QuadRecordReason::kUnread)) return 44;
  }
  // The shape: 4 vertices of 20 bytes and six indices, nothing else (a cube's 8 vertices and 30
  // indices, a HUD quad's stride).
  for (size_t bytes : {size_t(0), size_t(60), size_t(79), size_t(81), size_t(100), size_t(160)}) {
    Inputs s;
    s.in.vertex_bytes = bytes;
    if (!Refused(s, QuadRecordReason::kShape)) return 45;
  }
  for (uint32_t stride : {0u, 16u, 19u, 21u, 24u, 0xFFFFFFFFu}) {
    Inputs s;
    s.in.stride = stride;
    if (!Refused(s, QuadRecordReason::kShape)) return 46;
  }
  for (size_t count : {size_t(0), size_t(3), size_t(5), size_t(7), size_t(30)}) {
    Inputs s;
    s.in.index_count = count;
    if (!Refused(s, QuadRecordReason::kShape)) return 47;
  }
  // The six indices of plan Task 10's pattern, 0 1 2 1 3 2: each one changed, the HUD's pattern
  // (1 0 2 2 3 1), an index past the four vertices.
  for (int i = 0; i < 6; ++i) {
    Inputs s;
    s.indices[i] = uint16_t(s.indices[i] == 3 ? 0 : s.indices[i] + 1);
    if (!Refused(s, QuadRecordReason::kIndices)) return 48;
    Inputs t;
    t.indices[i] = 4;
    if (!Refused(t, QuadRecordReason::kIndices)) return 49;
    Inputs u;
    u.indices[i] = 0xFFFF;
    if (!Refused(u, QuadRecordReason::kIndices)) return 50;
  }
  {
    Inputs hud;
    const uint16_t pattern[6] = {1, 0, 2, 2, 3, 1};
    std::memcpy(hud.indices, pattern, sizeof(pattern));
    if (!Refused(hud, QuadRecordReason::kIndices)) return 51;
  }
  // The counts must be the kind's: the caller read what the table names, no more and no less.
  for (size_t n : {size_t(0), size_t(1), size_t(3), size_t(12)}) {
    Inputs s;
    s.in.ps_count = n;
    if (!Refused(s, QuadRecordReason::kUnread)) return 52;
  }
  for (size_t n : {size_t(0), size_t(2), size_t(3)}) {
    Inputs s;
    s.in.fetch_count = n;
    if (!Refused(s, QuadRecordReason::kUnread)) return 53;
  }
  // Every float finite and within 1e7: each of the 20 corner values, the 16 of the matrix, the 8
  // register components of the kind.
  const float bad[] = {kNaN, kInf, -kInf, Up(1e7f), Down(-1e7f), 3e38f};
  for (float v : bad) {
    for (int c = 0; c < 4; ++c) {
      for (int k = 0; k < 5; ++k) {
        Inputs s;
        s.Corner(c, k, v);
        if (!Refused(s, QuadRecordReason::kCorner)) return 54;
      }
    }
    for (int i = 0; i < 16; ++i) {
      Inputs s;
      s.view_proj[i / 4][i % 4] = v;
      if (!Refused(s, QuadRecordReason::kMatrix)) return 55;
    }
    for (int i = 0; i < 8; ++i) {
      Inputs s;
      s.ps[i / 4][i % 4] = v;
      if (!Refused(s, QuadRecordReason::kRegister)) return 56;
    }
  }
  // ... and the bound itself is inside.
  for (float v : {1e7f, -1e7f}) {
    Inputs s;
    s.Corner(2, 1, v);
    s.view_proj[1][3] = v;
    s.ps[1][2] = v;
    if (!QuadRecordValid(s.Make())) return 57;
  }
  // The state. The kind's blend is the draw's blend word, the colour mask all four channels (a
  // depth kind: none); another word is another draw than the table's.
  for (uint32_t blend : {0x00010001u, 0x07060706u, 0x0F0E0F0Eu, 0x01010100u, 0u}) {
    Inputs s;
    s.in.blend = blend;
    if (!Refused(s, QuadRecordReason::kState)) return 58;
  }
  for (uint32_t mask : {0u, 7u, 0xEu}) {
    Inputs s;
    s.in.colour_mask = mask;
    if (!Refused(s, QuadRecordReason::kState)) return 59;
  }
  {
    // (bits above the four channels are not the mask's)
    Inputs s;
    s.in.colour_mask = 0xFFFFFFFF;
    const QuadRecord r = s.Make();
    if (!QuadRecordValid(r) || r.colour_mask != 0xF) return 60;
  }
  {
    // Alpha blend and no blend, by their kinds; the layer's depth with colour mask 0 only.
    Inputs moon;
    moon.entry = FindPostQuad(0x18D39DCEBEE4C2FCull);
    moon.in.blend = 0x07060706;
    moon.in.alpha_ref = 0x3B808081;
    if (!QuadRecordValid(moon.Make())) return 61;
    moon.in.blend = 0x01010101;
    if (!Refused(moon, QuadRecordReason::kState)) return 62;
    Inputs beam;
    beam.entry = FindPostQuad(0x8E4DBB9E9A63EF67ull);
    beam.in.ps_count = 1;
    beam.in.blend = 0x00010001;
    beam.in.colour_control = 4;
    if (!QuadRecordValid(beam.Make())) return 63;
    beam.in.blend = 0x01010101;
    if (!Refused(beam, QuadRecordReason::kState)) return 64;
    Inputs occluder;
    occluder.entry = FindPostQuad(0x3FF79524FCDB71C5ull);
    occluder.in.ps_count = 4;
    occluder.in.colour_mask = 0;
    occluder.in.blend = 0x07060706;
    occluder.in.depth = 0x00708766;
    occluder.in.alpha_ref = 0x3E189899;
    const QuadRecord o = occluder.Make();
    if (!QuadRecordValid(o) || o.colour_mask != 0 || o.kind != PostQuadKind::kCloudOccluder) return 65;
    occluder.in.colour_mask = 0xF;
    if (!Refused(occluder, QuadRecordReason::kState)) return 66;
  }
  // The depth range: the record's depth word is read under the game's reversed range (window depth
  // 1 - z / w: PA_CL_VTE_CNTL bit 4 with a negative Z scale), which the record does not carry; a
  // call under another range gives none.
  for (uint32_t scale : {0x3F800000u, 0x00000000u, 0x80000000u, 0x7FC00000u, 0xFF800000u, 0x7F800000u}) {
    Inputs s;
    s.in.vport_z_scale = scale;
    if (!Refused(s, QuadRecordReason::kState)) return 82;
  }
  {
    Inputs s;
    s.in.vte_cntl = 0x0000042F;  // (the Z scale not applied)
    if (!Refused(s, QuadRecordReason::kState)) return 83;
    Inputs t;
    t.in.vport_z_scale = 0xC0000000;  // (-2: reversed all the same)
    t.in.vte_cntl = 0x10;
    if (!QuadRecordValid(t.Make())) return 84;
  }
  // Alpha-to-mask is no quad's state (the tree occluders have it).
  {
    Inputs s;
    s.in.colour_control = 0xAA00001C;
    if (!Refused(s, QuadRecordReason::kAlpha)) return 67;
  }
  // ... and the word's other bits (the dither offsets of an alpha-to-mask that is off) are not the record's.
  {
    Inputs s;
    s.in.colour_control = 0xAA00000C;
    const QuadRecord r = s.Make();
    if (!QuadRecordValid(r) || r.alpha_control != 0xC) return 85;
  }
  // The alpha test on: its reference a number in [0, 1]; any of the eight functions is carried.
  for (float v : {kNaN, kInf, -kInf, Down(0.0f), -0.5f, Up(1.0f), 2.0f}) {
    Inputs s;
    s.in.alpha_ref = BitsOf(v);
    if (!Refused(s, QuadRecordReason::kAlpha)) return 68;
  }
  for (float v : {0.0f, 1.0f, 0.5f}) {
    Inputs s;
    s.in.alpha_ref = BitsOf(v);
    const QuadRecord r = s.Make();
    if (!QuadRecordValid(r) || r.alpha_ref != v) return 69;
  }
  for (uint32_t func = 0; func < 8; ++func) {
    Inputs s;
    s.in.colour_control = 8 | func;
    const QuadRecord r = s.Make();
    if (!QuadRecordValid(r) || r.alpha_control != (8 | func)) return 70;
  }
  // The order of the rules: what cannot be read, the shape, the indices, the corners, the matrix,
  // the registers, the state, the alpha test.
  {
    Inputs s;
    s.in.alpha_ref = BitsOf(kNaN);
    if (!Refused(s, QuadRecordReason::kAlpha)) return 71;
    s.in.blend = 0;
    if (!Refused(s, QuadRecordReason::kState)) return 72;
    s.ps[0][0] = kNaN;
    if (!Refused(s, QuadRecordReason::kRegister)) return 73;
    s.view_proj[0][0] = kNaN;
    if (!Refused(s, QuadRecordReason::kMatrix)) return 74;
    s.Corner(0, 0, kNaN);
    if (!Refused(s, QuadRecordReason::kCorner)) return 75;
    s.indices[0] = 1;
    if (!Refused(s, QuadRecordReason::kIndices)) return 76;
    s.in.stride = 24;
    if (!Refused(s, QuadRecordReason::kShape)) return 77;
    s.in.vertices = nullptr;
    if (!Refused(s, QuadRecordReason::kUnread)) return 78;
  }
  // Without a place for the reason.
  {
    Inputs s;
    s.in.stride = 24;
    if (!AllZero(MakeQuadRecord(*s.entry, s.in, nullptr))) return 79;
  }
  const char* names[] = {"none", "unread", "shader", "storage", "shape", "indices", "corner", "matrix", "register", "state", "alpha"};
  static_assert(sizeof(names) / sizeof(names[0]) == size_t(QuadRecordReason::kCount));
  for (size_t i = 0; i < size_t(QuadRecordReason::kCount); ++i) {
    if (std::string(QuadRecordReasonName(QuadRecordReason(i))) != names[i]) return 80;
  }
  if (std::string(QuadRecordReasonName(QuadRecordReason::kCount)) != "?") return 81;
  return 0;
}

// The frame's choice (ChooseQuadSet): the set whole, or none of it.
static int SetTests() {
  uint32_t refused[size_t(QuadRecordReason::kCount)] = {};
  // Off; no furniture call; a whole set.
  if (ChooseQuadSet(false, 5, 5, refused, 0) != QuadSetReason::kOff) return 100;
  if (ChooseQuadSet(true, 0, 0, refused, 0) != QuadSetReason::kNoDraw) return 101;
  if (ChooseQuadSet(true, 17, 17, refused, 0) != QuadSetReason::kNone) return 102;
  if (ChooseQuadSet(true, 24, 24, refused, 0) != QuadSetReason::kNone) return 103;
  // One call more than the bound holds: none of the 24 either.
  if (ChooseQuadSet(true, 25, 24, refused, 1) != QuadSetReason::kOverLimit) return 104;
  // A call whose storage was not read by the frame's end (the last quad without a later draw,
  // or a record dropped for a frame that ended).
  if (ChooseQuadSet(true, 6, 5, refused, 0) != QuadSetReason::kPending) return 105;
  // A refused record, whatever else: by the reason first refused in the enum's order.
  refused[size_t(QuadRecordReason::kCorner)] = 1;
  if (ChooseQuadSet(true, 6, 5, refused, 0) != QuadSetReason::kRefused) return 106;
  if (ChooseQuadSet(true, 26, 24, refused, 1) != QuadSetReason::kRefused) return 107;
  if (ChooseQuadSet(true, 5, 5, refused, 0) != QuadSetReason::kRefused) return 108;
  if (ChooseQuadSet(false, 6, 5, refused, 0) != QuadSetReason::kOff) return 109;
  // Counts that cannot be (more taken than seen) are no set.
  const uint32_t none[size_t(QuadRecordReason::kCount)] = {};
  if (ChooseQuadSet(true, 3, 4, none, 0) != QuadSetReason::kPending) return 110;
  if (ChooseQuadSet(true, 3, 25, none, 0) != QuadSetReason::kOverLimit) return 111;
  const char* names[] = {"none", "off", "no-draw", "refused", "over-limit", "pending"};
  static_assert(sizeof(names) / sizeof(names[0]) == size_t(QuadSetReason::kCount));
  for (size_t i = 0; i < size_t(QuadSetReason::kCount); ++i) {
    if (std::string(QuadSetReasonName(QuadSetReason(i))) != names[i]) return 112;
  }
  // Where a kind may be read (ruling 24, per kind): the scene's kinds inside the main scene, the
  // layer's outside it at a target of half the scene's pitch. The reflection views' flares (pitch
  // 280) and a scene of unknown pitch give no layer quad.
  const PostQuadEntry& moon = kPostQuadTable[0];
  const PostQuadEntry& flare = kPostQuadTable[4];
  if (!QuadTargetMatches(moon, true, 1120, 1120) || QuadTargetMatches(moon, false, 1120, 1120) ||
      QuadTargetMatches(moon, false, 560, 1120)) {
    return 113;
  }
  if (!QuadTargetMatches(flare, false, 560, 1120) || QuadTargetMatches(flare, false, 280, 1120) ||
      QuadTargetMatches(flare, true, 560, 1120) || QuadTargetMatches(flare, false, 1120, 1120) ||
      QuadTargetMatches(flare, false, 560, 0) || QuadTargetMatches(flare, false, 0, 0) ||
      QuadTargetMatches(flare, false, 0, 1120)) {
    return 114;
  }
  // (half of the scene's pitch whatever it is: nothing carries 560; an odd pitch has no half)
  if (!QuadTargetMatches(flare, false, 640, 1280) || QuadTargetMatches(flare, false, 560, 1121)) return 115;
  // A furniture shader at a foreign target (review of plan Task 10): every layer kind at the
  // reflection views' pitch (280: D72F is drawn there), at the shadow maps' (1040: F1EB's) and at
  // the luminance buffer's (320: 5D67's), and every scene kind outside the scene at any of them, is
  // no quad of the frame. The two shaders that are no quad kind at all have no entry whatever the target.
  for (const PostQuadEntry& e : kPostQuadTable) {
    for (uint32_t pitch : {280u, 320u, 1040u, 1120u, 1280u, 0u}) {
      if (QuadTargetMatches(e, false, pitch, 1120)) return 116;
    }
    if (QuadTargetMatches(e, false, 560, 1120) != (e.layer == 1) || QuadTargetMatches(e, true, 1120, 1120) != (e.layer == 0)) return 117;
  }
  if (FindPostQuad(0xF1EB2497108F159Dull) || FindPostQuad(0x5D6768847B0602B8ull)) return 118;
  // The cloud occluder is a kind of its own, complete in the table (controller ruling 35: the layer's
  // depth is drawn with it): the layer's depth target, colour mask 0, its alpha test, four registers.
  const PostQuadEntry& occluder = kPostQuadTable[size_t(PostQuadKind::kCloudOccluder)];
  const uint8_t occluder_registers[4] = {31, 46, 47, 77};
  if (occluder.target != PostQuadTarget::kLayerDepth || occluder.layer != 1 || !occluder.alpha_test ||
      std::memcmp(occluder.registers, occluder_registers, sizeof(occluder_registers)) != 0) {
    return 119;
  }
  return 0;
}

// The radial blur's registers in the post block (SetPostRadial).
static int RadialTests() {
  PostBlock valid{};
  valid.valid = true;
  valid.parts = kPostPartBlur;
  valid.composite[0] = 3.0f;
  // Run 3's values: c31.xy (0.3821, 0.3938), c46.x 0.87, c47.x 0.935.
  const float c31[4] = {Bits(0x3EC39DE6), Bits(0x3EC9A5EE), 0.0f, 0.0f};
  const float c46[4] = {0.87f, 0.0f, 0.0f, 0.0f}, c47[4] = {0.935f, 0.0f, 0.0f, 0.0f};
  {
    PostBlock b = valid;
    if (!SetPostRadial(&b, c31, c46, c47)) return 200;
    if (b.radial[0] != c31[0] || b.radial[1] != c31[1] || b.radial[2] != 0.87f || b.radial[3] != 0.935f) return 201;
    if (b.parts != (kPostPartBlur | kPostPartRadial) || !b.valid || b.composite[0] != 3.0f) return 202;
  }
  auto refused = [&](const float* a, const float* b46, const float* b47) {
    PostBlock b = valid;
    b.radial[0] = 9.0f;  // (whatever was there: a refusal leaves the part clear and the four zero)
    b.parts |= kPostPartRadial;
    if (SetPostRadial(&b, a, b46, b47)) return false;
    for (float v : b.radial) {
      if (v != 0.0f) return false;
    }
    return b.parts == kPostPartBlur && b.valid;
  };
  if (!refused(nullptr, c46, c47) || !refused(c31, nullptr, c47) || !refused(c31, c46, nullptr)) return 203;
  if (SetPostRadial(nullptr, c31, c46, c47)) return 204;
  {
    // An invalid block takes nothing.
    PostBlock b{};
    if (SetPostRadial(&b, c31, c46, c47) || b.parts != 0 || b.radial[2] != 0.0f) return 205;
  }
  // c31.xy finite and in [-4, 4] (the blur's centre in the picture's units; outside the picture
  // when the sun is).
  for (int k = 0; k < 2; ++k) {
    for (float v : {kNaN, kInf, -kInf, Up(4.0f), Down(-4.0f), 100.0f}) {
      float a[4] = {c31[0], c31[1], 0.0f, 0.0f};
      a[k] = v;
      if (!refused(a, c46, c47)) return 206;
    }
    for (float v : {4.0f, -4.0f, 0.0f, 0.5f}) {
      float a[4] = {c31[0], c31[1], 0.0f, 0.0f};
      a[k] = v;
      PostBlock b = valid;
      if (!SetPostRadial(&b, a, c46, c47) || b.radial[k] != v) return 207;
    }
  }
  // A centre component of exactly 0 (c31.y is 0 in frame 8 of run 3's capture; review of plan Task
  // 10): a place like any other. Nothing here divides by it, and the game's blur (4BE4) only
  // subtracts the centre from the pixel's coordinate: the block and its radial part stand.
  {
    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f}, y0[4] = {c31[0], 0.0f, 0.0f, 0.0f}, minus[4] = {-0.0f, -0.0f, 0.0f, 0.0f};
    for (const float* a : {zero, y0, minus}) {
      PostBlock b = valid;
      if (!SetPostRadial(&b, a, c46, c47) || !b.valid || !(b.parts & kPostPartRadial) || b.radial[1] != 0.0f ||
          b.radial[2] != 0.87f) {
        return 212;
      }
    }
  }
  // c46.x and c47.x in (0, 1].
  for (float v : {kNaN, kInf, -kInf, 0.0f, -0.0f, -0.5f, Up(1.0f), 2.0f}) {
    const float w[4] = {v, 0.0f, 0.0f, 0.0f};
    if (!refused(c31, w, c47) || !refused(c31, c46, w)) return 208;
  }
  for (float v : {1.0f, Up(0.0f), 0.5f}) {
    const float w[4] = {v, 0.0f, 0.0f, 0.0f};
    PostBlock b = valid, c = valid;
    if (!SetPostRadial(&b, c31, w, c47) || b.radial[2] != v) return 209;
    if (!SetPostRadial(&c, c31, c46, w) || c.radial[3] != v) return 210;
  }
  // The components the block does not take may be anything.
  {
    const float a[4] = {0.5f, 0.5f, kNaN, kInf}, w46[4] = {0.87f, kNaN, kInf, -1.0f}, w47[4] = {0.935f, kNaN, 9.0f, kInf};
    PostBlock b = valid;
    if (!SetPostRadial(&b, a, w46, w47)) return 211;
  }
  static_assert(sizeof(PostBlock) == 2168);
  return 0;
}

int main() {
  // (an exit code would wrap at 256: the case's number is printed)
  int code = TableTests();
  if (!code) code = RecordTests();
  if (!code) code = RefusalTests();
  if (!code) code = SetTests();
  if (!code) code = RadialTests();
  if (code) {
    std::printf("FAIL: quad record case %d\n", code);
    return 1;
  }
  std::puts("PASS: quad records: the table's eight kinds, a captured sun disc's record, every refusal, the frame's set, the "
            "kinds' targets, the radial blur's registers");
  return 0;
}
