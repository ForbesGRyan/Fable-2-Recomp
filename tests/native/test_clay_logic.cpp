// Synthetic standalone test for the clay pass's pure helpers; no game or GPU.
#include "../../src/native/render/clay_logic.h"
#include "../../src/native/fable2_native_shaders.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <cmath>
#include <limits>
#include <string>
#include <thread>

using namespace fable2::native;
using namespace fable2::native::render;

// The two fog tables of a captured frame (kFixtureTf4, kFixtureTf5): cases 580 on.
#include "fog_table_fixture.inc"
// The composite's table the post pins were made with (kPinsPostTable): cases 600 on.
#include "post_pins_table.inc"

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

// --- the sun layer's cases (a function of their own: main's frame holds every other case's locals) ---
static void SunLayerTests() {
  // 640 to 652: the sun layer (sub-project 10, plan Task 14; clay_logic.h, "Native sun layer").
  {
    using capture::PostQuadKind;
    using capture::QuadRecord;
    const float kNaN = std::numeric_limits<float>::quiet_NaN(), kInf = std::numeric_limits<float>::infinity();
    auto bits = [](uint32_t b) {
      float f;
      std::memcpy(&f, &b, sizeof(f));
      return f;
    };
    // Frame 1 of capture native_discovery_20261010_052158 (towards the sun): the frame's quad matrix and its
    // layer records, made by the capture's own function from the rows' values: the first cloud occluder (n
    // 47), the beam source (n 53), the moon glow (n 54), the sun disc (n 55); a flare of capture
    // 20261010_034415 frame 1 (n 77: the sunward frame has none); and the scene's sun glare (n 43).
    const uint32_t matrix_bits[16] = {0x3EBA04AA, 0x3F684A6D, 0x33BBAA18, 0xC3306FC9, 0x3F3E8525, 0xBE989184, 0x3FC5551A, 0xC2B9462A,
                                      0xBF52DBA3, 0x3EA8DAD8, 0x3EEC3766, 0x3DCDF642, 0xBF52DA8E, 0x3EA8D9FA, 0x3EEC3630, 0x3E4D6100};
    float matrix[4][4];
    for (int i = 0; i < 16; ++i) matrix[i / 4][i % 4] = bits(matrix_bits[i]);
    struct Fetch { uint32_t w[6]; };
    auto record = [&](uint64_t ps, const char* verts, std::initializer_list<std::initializer_list<uint32_t>> registers, uint32_t blend,
                      uint32_t depth, uint32_t mask, uint32_t control, uint32_t ref, const Fetch& texture) {
      uint8_t vertices[80];
      auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
      for (int i = 0; i < 80; ++i) vertices[i] = uint8_t(digit(verts[2 * i]) * 16 + digit(verts[2 * i + 1]));
      const uint16_t indices[6] = {0, 1, 2, 1, 3, 2};
      float ps_regs[12][4] = {};
      size_t n = 0;
      for (const auto& r : registers) {
        int k = 0;
        for (uint32_t b : r) ps_regs[n][k++] = bits(b);
        ++n;
      }
      uint32_t fetch[3][6] = {};
      std::memcpy(fetch[0], texture.w, sizeof(fetch[0]));
      capture::QuadInputs in{};
      in.vertices = vertices;
      in.vertex_bytes = sizeof(vertices);
      in.stride = 20;
      in.indices = indices;
      in.index_count = 6;
      in.view_proj = matrix;
      in.ps = ps_regs;
      in.ps_count = n;
      in.fetch = fetch;
      in.fetch_count = 1;
      in.blend = blend;
      in.depth = depth;
      in.colour_mask = mask;
      in.colour_control = control;
      in.alpha_ref = ref;
      in.vport_z_scale = 0xBF800000;
      in.vte_cntl = 0x43F;
      const capture::PostQuadEntry* entry = capture::FindPostQuad(ps);
      return entry ? capture::MakeQuadRecord(*entry, in, nullptr) : QuadRecord{};
    };
    const Fetch glow_texture = {{0x82000002, 0x14E60054, 0x001FE0FF, 0x00A80D10, 0x00000103, 0x14E70A18}};
    const Fetch cloud_texture = {{0x84000002, 0x163E0054, 0x003FE1FF, 0x00A80D10, 0x00000143, 0x16420A18}};
    const QuadRecord occluder = record(0x3FF79524FCDB71C5ull,
                                       "C53B8001C53B800143FA00000000000000000000453B8001C53B800143FA00003F80000000000000"
                                       "C53B8001453B800143FA0000000000003F800000453B8001453B800143FA00003F8000003F800000",
                                       {{0x40A00000, 0x40A00000, 0x00000000, 0x00000000}, {0x3E8BA2EF, 0x3F0BA2EF, 0x00000000, 0x00000000},
                                        {0x42B431AC, 0x431E5EC9, 0x423CD3F5, 0x3F800000}, {0x3F000000, 0x00000000, 0x00000000, 0x00000000}},
                                       0x07060706, 0x00708766, 0x0, 0xC, 0x3E189899, cloud_texture);
    const QuadRecord beam = record(0x8E4DBB9E9A63EF67ull,
                                   "C50A1176C3B7A1D44392D8AA000000003F800000C4C0806644A2F86E4392D8B23F8000003F800000"
                                   "C4B3C528C428FC7444EC55E80000000000000000C44045444478C55344EC55EA3F80000000000000",
                                   {{0x3F51E970, 0x3F258124, 0x3EA3788B, 0x00000000}}, 0x00010001, 0x00708762, 0xF, 0x4, 0x3F008081, glow_texture);
    const QuadRecord glow = record(0x4B58837D42F08C89ull,
                                   "C524C8D8C3E53C87C559F34D3F0000003F800000C51232F4438E1A61C559F34D3F2000003F800000"
                                   "C50F5EACC414EA81C52D96EB3F00000000000000C4F99190431303CCC52D96EB3F20000000000000",
                                   {}, 0x07060706, 0x00708762, 0xF, 0xC, 0x3D20A0A1, glow_texture);
    const QuadRecord disc = record(0x3E77B1AD4BC5B900ull,
                                   "C4CC22CE4331ACB944691F55000000003F800000C4BB68B443FFF0AB44691F553F8000003F800000"
                                   "C4B8DCDA42E7E096449C7C6A0000000000000000C4A822C043E11273449C7C6A3F80000000000000",
                                   {{0x3BC20A12, 0x3D8FC764, 0x3DBABD2A, 0x3F800000}, {0x3F606FAA, 0x3F30F4C4, 0x3EAEC826, 0x00000000}},
                                   0x01010101, 0x00708762, 0xF, 0xC, 0x3F008081, glow_texture);
    const QuadRecord flare = record(0xD72FD0903CCE7DD6ull,
                                    "42E75881431E794C42249D81000000000000000042E7F815431DF01842249D813F80000000000000"
                                    "42E7891F431E876E42270E57000000003F80000042E828B3431DFE3A42270E573F8000003F800000",
                                    {{0x3ECF965C, 0x3E41BF89, 0x3CC36057, 0x3E85119D}}, 0x01010101, 0x00708762, 0xF, 0x4, 0x3F008081, glow_texture);
    const QuadRecord glare = record(0x04985A0F7296E130ull,
                                    "C5143602C3D96C434381B5C0000000003F800000C4CB7E8C44B1BB3F4381B5C83F8000003F800000"
                                    "C4BD592CC44274BA44FE3B5E0000000000000000C440D7674486DBF344FE3B603F80000000000000",
                                    {{0x3E7AC1BC, 0x3E45B561, 0x3DC3477C, 0x00000000}}, 0x01010101, 0x00708760, 0xF, 0x4, 0x3F008081, glow_texture);
    const QuadRecord* const layer_records[5] = {&occluder, &beam, &glow, &disc, &flare};
    for (const QuadRecord* q : layer_records) CHECK(640, capture::QuadRecordValid(*q) && q->layer == 1);
    CHECK(640, capture::QuadRecordValid(glare) && glare.layer == 0);
    const float unit[2] = {1.0f, 1.0f};

    // 640: the state a layer record is drawn with, the target of its kind, and the depth test's direction.
    {
      CHECK(640, SunQuadDrawState(occluder) == ClaySunState::kDepth && SunQuadDrawState(beam) == ClaySunState::kReplace &&
                     SunQuadDrawState(glow) == ClaySunState::kAlpha && SunQuadDrawState(disc) == ClaySunState::kAdd &&
                     SunQuadDrawState(flare) == ClaySunState::kAdd);
      // The scene's quad step draws none of them (their kinds have no place in it), and the layer none of the scene's.
      for (const QuadRecord* q : layer_records) CHECK(640, ClayQuadPlaceOf(q->kind) == ClayQuadPlace::kNotDrawn);
      CHECK(640, SunQuadDrawState(glare) == ClaySunState::kNone && QuadDrawState(disc) == ClayQuadState::kNone &&
                     QuadDrawState(flare) == ClayQuadState::kNone && QuadDrawState(beam) == ClayQuadState::kNone &&
                     QuadDrawState(occluder) == ClayQuadState::kNone);
      // The target is the kind's (a record does not carry it; the order of the records does not decide it).
      CHECK(640, SunQuadTarget(PostQuadKind::kCloudOccluder) == 0 && SunQuadTarget(PostQuadKind::kBeamSource) == 1 &&
                     SunQuadTarget(PostQuadKind::kFlare) == 2 && SunQuadTarget(PostQuadKind::kMoonGlow) == 2 &&
                     SunQuadTarget(PostQuadKind::kSunDisc) == 2);
      for (const QuadRecord* base : layer_records) {
        // No depth test; another function than greater-or-equal; a depth write a colour kind must not have,
        // or the lack of the one the occluder must have; a colour mask that is not the kind's; an alpha
        // function that is not "greater"; another kind's blend; a record that is not the layer's.
        QuadRecord q = *base;
        q.depth &= ~2u;
        CHECK(640, SunQuadDrawState(q) == ClaySunState::kNone);
        for (uint32_t func = 0; func < 8; ++func) {
          q = *base;
          q.depth = (q.depth & ~0x70u) | (func << 4);
          CHECK(640, (SunQuadDrawState(q) != ClaySunState::kNone) == (func == 6));
        }
        q = *base;
        q.depth ^= 4u;
        CHECK(640, SunQuadDrawState(q) == ClaySunState::kNone);
        q = *base;
        q.colour_mask ^= 0xFu;
        CHECK(640, SunQuadDrawState(q) == ClaySunState::kNone);
        q = *base;
        q.colour_mask = 0x7;
        CHECK(640, SunQuadDrawState(q) == ClaySunState::kNone);
        for (uint32_t func = 0; func < 8; ++func) {
          q = *base;
          q.alpha_control = 8u | func;
          CHECK(640, (SunQuadDrawState(q) != ClaySunState::kNone) == (func == 4));
        }
        q = *base;
        q.layer = 0;
        CHECK(640, SunQuadDrawState(q) == ClaySunState::kNone);
        if (base != &occluder) {
          for (uint32_t blend : {kQuadBlendReplace, kQuadBlendAdd, kQuadBlendAlpha, 0x00000000u}) {
            q = *base;
            q.blend = blend;
            CHECK(640, (SunQuadDrawState(q) != ClaySunState::kNone) == (blend == base->blend));
          }
        }
      }
      // The direction: the game's greater-or-equal on 1 - z / w is less-or-equal on z / w. The beam source's
      // corners lie at z / w 0.99997: in front of the cleared depth (the sky), behind everything nearer.
      double clip[4];
      QuadPositionMirror(beam.corners[0], matrix, clip);
      const float z = float(clip[2] / clip[3]);
      CHECK(640, clip[3] > 0.0 && z > 0.9999f && z < 1.0f);
      CHECK(640, SunDepthPasses(z, 1.0f) && SunDepthPasses(z, z) && !SunDepthPasses(z, 0.5f) && !SunDepthPasses(z, std::nextafter(z, 0.0f)));
      CHECK(640, !SunDepthPasses(kNaN, 1.0f) && !SunDepthPasses(0.5f, kNaN));
      // The same test in the game's terms, on both sides of the stored value.
      for (float stored : {0.0f, 0.25f, z, 1.0f}) {
        for (float quad : {0.0f, 0.25f, 0.5f, z, 1.0f}) CHECK(640, SunDepthPasses(quad, stored) == (1.0f - quad >= 1.0f - stored));
      }
    }

    // 641: a layer record's constants, each where the shader's block has it, and what gives none.
    {
      static_assert(sizeof(ClaySunQuadConstants) == 416 && offsetof(ClaySunQuadConstants, reg) == 192 && offsetof(ClaySunQuadConstants, kind) == 384 &&
                    offsetof(ClaySunQuadConstants, alpha_ref) == 396 && offsetof(ClaySunQuadConstants, fix) == 400 &&
                    offsetof(ClaySunQuadConstants, levels) == 408);
      CHECK(641, sizeof(ClaySunQuadConstants) <= size_t(kClayQuadSlots) * 256);
      ClaySunQuadConstants c;
      const float fix[2] = {0.5f, 0.25f};
      CHECK(641, MakeClaySunQuadConstants(beam, 3, fix, &c));
      CHECK(641, c.kind == 4 && c.sampler == 3 && c.alpha_test == 0 && c.alpha_ref == 0.0f && c.levels == 1023.0f && c.pad == 0 &&
                     c.fix[0] == 0.5f && c.fix[1] == 0.25f);
      CHECK(641, c.reg[0][0] == beam.ps[0][0] && c.reg[0][1] == beam.ps[0][1] && c.reg[0][2] == beam.ps[0][2] && c.reg[0][3] == 0.0f &&
                     c.reg[0][0] > 0.81f && c.reg[0][0] < 0.83f);
      for (int i = 0; i < 4; ++i) {
        CHECK(641, c.corner[i][0] == beam.corners[i][0] && c.corner[i][2] == beam.corners[i][2] && c.corner[i][3] == 1.0f &&
                       c.coord[i][0] == beam.corners[i][3] && c.coord[i][1] == beam.corners[i][4] && c.coord[i][2] == 0.0f);
        for (int k = 0; k < 4; ++k) CHECK(641, c.view_proj[i][k] == matrix[i][k]);
      }
      CHECK(641, MakeClaySunQuadConstants(flare, 1, unit, &c) && c.kind == 5 && c.reg[0][0] == flare.ps[0][0] && c.reg[0][3] == 0.0f &&
                     c.alpha_test == 0);
      CHECK(641, MakeClaySunQuadConstants(glow, 1, unit, &c) && c.kind == 6 && c.alpha_test == 1 && c.alpha_ref == bits(0x3D20A0A1));
      for (int i = 0; i < 12; ++i) {
        for (int k = 0; k < 4; ++k) CHECK(641, c.reg[i][k] == 0.0f);
      }
      // The sun disc: c20.w where the shader reads it (1 in the layer: not the scene's scale, and held to no
      // frame's), c31 beside it; its alpha test with the draw's reference.
      CHECK(641, MakeClaySunQuadConstants(disc, 1, unit, &c) && c.kind == 7 && c.reg[0][3] == 1.0f && c.reg[0][0] == 0.0f &&
                     c.reg[0][1] == 0.0f && c.reg[1][0] == disc.ps[1][0] && c.reg[1][2] == disc.ps[1][2] && c.reg[1][3] == 0.0f &&
                     c.alpha_test == 1 && c.alpha_ref == bits(0x3F008081));
      // A cloud occluder: tiling, scroll, the eye's x and y, c77.x; the draw's reference 0.149.
      CHECK(641, MakeClaySunQuadConstants(occluder, 1, unit, &c) && c.kind == 8 && c.reg[0][0] == 5.0f && c.reg[0][1] == 5.0f &&
                     c.reg[1][0] == bits(0x3E8BA2EF) && c.reg[1][1] == bits(0x3F0BA2EF) && c.reg[2][0] == bits(0x42B431AC) &&
                     c.reg[2][1] == bits(0x431E5EC9) && c.reg[2][2] == 0.0f && c.reg[2][3] == 0.0f && c.reg[3][0] == 0.5f &&
                     c.alpha_test == 1 && c.alpha_ref == bits(0x3E189899) && c.alpha_ref > 0.1489f && c.alpha_ref < 0.1491f);
      // What gives none, each with `out` untouched.
      auto refused = [&](const QuadRecord& q, const float* scale = nullptr) {
        ClaySunQuadConstants out;
        std::memset(&out, 0x5A, sizeof(out));
        ClaySunQuadConstants before = out;
        return !MakeClaySunQuadConstants(q, 0, scale ? scale : unit, &out) && std::memcmp(&out, &before, sizeof(out)) == 0;
      };
      CHECK(641, !MakeClaySunQuadConstants(beam, 0, unit, nullptr) && !MakeClaySunQuadConstants(beam, 0, nullptr, &c));
      CHECK(641, refused(QuadRecord{}) && refused(glare));
      for (const QuadRecord* base : layer_records) {
        for (float v : {kNaN, kInf, -kInf, 2e7f, -2e7f}) {
          for (int i = 0; i < 4; ++i) {
            for (int k = 0; k < 5; ++k) {
              QuadRecord q = *base;
              q.corners[i][k] = v;
              CHECK(641, refused(q));
            }
            for (int k = 0; k < 4; ++k) {
              QuadRecord q = *base;
              q.view_proj[i][k] = v;
              CHECK(641, refused(q));
            }
          }
        }
        for (float v : {kNaN, kInf, 0.0f, -1.0f, 65.0f}) {
          const float a[2] = {v, 1.0f}, b[2] = {1.0f, v};
          CHECK(641, refused(*base, a) && refused(*base, b));
        }
        // A texture that is fetched signed or through the gamma curve is not drawn as stored.
        for (uint32_t sign : {0x4u, 0x30u, 0x200u, 0x3FCu}) {
          QuadRecord q = *base;
          q.fetch[0][0] |= sign;
          CHECK(641, refused(q));
        }
      }
      for (float v : {kNaN, kInf, -kInf, -1e-6f, std::nextafter(64.0f, 100.0f), 1e6f}) {
        for (int k = 0; k < 3; ++k) {
          QuadRecord q = beam;
          q.ps[0][k] = v;
          CHECK(641, refused(q));
          q = flare;
          q.ps[0][k] = v;
          CHECK(641, refused(q));
          q = disc;
          q.ps[1][k] = v;
          CHECK(641, refused(q));
        }
        QuadRecord q = disc;
        q.ps[0][3] = v;
        CHECK(641, refused(q));
        q = occluder;
        q.ps[3][0] = v;
        CHECK(641, refused(q));
      }
      {
        // The bounds themselves pass; what the shaders do not read is neither judged nor handed over.
        QuadRecord q = beam;
        q.ps[0][0] = 64.0f;
        q.ps[0][3] = kNaN;
        CHECK(641, MakeClaySunQuadConstants(q, 0, unit, &c) && c.reg[0][0] == 64.0f && c.reg[0][3] == 0.0f);
        q = disc;
        q.ps[0][3] = 64.0f;
        q.ps[0][0] = kNaN;
        q.ps[1][3] = kNaN;
        CHECK(641, MakeClaySunQuadConstants(q, 0, unit, &c) && c.reg[0][3] == 64.0f && c.reg[0][0] == 0.0f && c.reg[1][3] == 0.0f);
        q = disc;
        q.ps[0][3] = 0.0f;
        CHECK(641, refused(q));
        // The sun disc's scale is its own: the layer's 1 is drawn whatever the frame's output scale is.
        q = disc;
        q.ps[0][3] = 7.6f;
        CHECK(641, MakeClaySunQuadConstants(q, 0, unit, &c) && c.reg[0][3] == 7.6f);
      }
      for (float v : {kNaN, kInf, 5000.0f, -5000.0f}) {
        for (int r = 0; r < 2; ++r) {
          for (int k = 0; k < 2; ++k) {
            QuadRecord q = occluder;
            q.ps[r][k] = v;
            CHECK(641, refused(q));
          }
        }
      }
      for (float v : {kNaN, kInf, 2e7f}) {
        for (int k = 0; k < 2; ++k) {
          QuadRecord q = occluder;
          q.ps[2][k] = v;
          CHECK(641, refused(q));
        }
      }
      {
        QuadRecord q = occluder;
        q.ps[0][0] = q.ps[1][1] = 4096.0f;
        q.ps[2][2] = kNaN;  // (the eye's height: not read)
        CHECK(641, MakeClaySunQuadConstants(q, 0, unit, &c) && c.reg[0][0] == 4096.0f && c.reg[2][2] == 0.0f);
        // An occluder without its alpha test would write its depth over the whole quad: not drawn.
        q = occluder;
        q.alpha_control = 0x4;
        CHECK(641, refused(q));
      }
      for (float v : {kNaN, kInf, -0.01f, 1.01f}) {
        for (const QuadRecord* base : {&occluder, &glow, &disc}) {
          QuadRecord q = *base;
          q.alpha_ref = v;
          CHECK(641, refused(q));
        }
      }
      // The scene's quads: the same rule for the texture's sign fields (review of plan Tasks 12 and 13, Minor 9).
      {
        ClayQuadConstants s;
        CHECK(641, MakeClayQuadConstants(glare, 16.0f, 0, unit, &s) && QuadTextureUnsigned(glare));
        for (uint32_t sign : {0x4u, 0x30u, 0x200u, 0x3FCu}) {
          QuadRecord q = glare;
          q.fetch[0][0] |= sign;
          CHECK(641, !QuadTextureUnsigned(q) && !MakeClayQuadConstants(q, 16.0f, 0, unit, &s));
        }
        QuadRecord q = glare;
        q.fetch[0][0] |= 0xFFFFFC00u;  // (bits outside the sign fields are not the rule's)
        CHECK(641, QuadTextureUnsigned(q));
      }
    }

    // 642: the layer quads' mirrors against the reference's numbers (tools/xdk_sigmatch/post_pins.py).
    {
      double worst = 0.0;
      auto close = [&](const double out[4], const double want[4]) {
        for (int i = 0; i < 4; ++i) {
          CHECK(642, std::isfinite(out[i]) && out[i] >= 0.0 && out[i] <= 1.0 && std::fabs(out[i] - want[i]) <= 1e-12);
          worst = std::max(worst, std::fabs(out[i] - want[i]));
        }
      };
      struct TwoPin { float t[4]; float c31[4]; double want[4]; };
      const TwoPin beam_pins[] = {
          // sun beam pins (light_formulas.post_beam_source, c255.x 0.1666666716337204f): the texel, c31, the value (rgb, alpha)
          {{1.0f, 1.0f, 1.0f, 1.0f}, {0.8199682235717773f, 0.6465017795562744f, 0.3192790448665619f, 0.0f}, {0.13666137466812245, 0.10775029980392148, 0.05321317573030315, 1.0}},
          {{0.5f, 0.25f, 0.125f, 0.75f}, {0.8199682235717773f, 0.6465017795562744f, 0.3192790448665619f, 0.0f}, {0.06833068733406122, 0.02693757495098037, 0.006651646966287894, 1.0}},
          {{1.0f, 0.5f, 0.25f, 0.0f}, {64.0f, 6.0f, 1.0f, 0.0f}, {1.0, 0.5000000149011612, 0.0416666679084301, 1.0}},
          {{kNaN, 0.5f, kInf, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f}, {0.0, 0.0833333358168602, 0.0, 1.0}},
          {{0.5f, 0.5f, 0.5f, 1.0f}, {1.0f, kNaN, 1.0f, 0.0f}, {0.0, 0.0, 0.0, 0.0}},
          {{0.5f, 0.5f, -0.5f, 1.0f}, {-1.0f, 1.0f, 1.0f, kNaN}, {0.0, 0.0833333358168602, 0.0, 1.0}},
      };
      for (const TwoPin& p : beam_pins) {
        double out[4];
        SunBeamMirror(p.t, p.c31, out);
        close(out, p.want);
      }
      const TwoPin flare_pins[] = {
          // sun flare pins (light_formulas.post_flare): the texel, c31, the value (rgb, alpha)
          {{0.10000000149011612f, 0.20000000298023224f, 0.30000001192092896f, 1.0f}, {0.40544402599334717f, 0.18920721113681793f, 0.023849649354815483f, 0.2599000036716461f}, {0.40544402599334717, 0.18920721113681793, 0.023849649354815483, 1.0}},
          {{0.8999999761581421f, 0.8999999761581421f, 0.8999999761581421f, 0.5f}, {0.40544402599334717f, 0.18920721113681793f, 0.023849649354815483f, 0.2599000036716461f}, {0.20272201299667358, 0.09460360556840897, 0.011924824677407742, 1.0}},
          {{0.0f, 0.0f, 0.0f, 1.0f}, {64.0f, 1.0f, 0.5f, 0.0f}, {1.0, 1.0, 0.5, 1.0}},
          {{0.5f, 0.5f, 0.5f, kNaN}, {1.0f, 1.0f, 1.0f, 0.0f}, {0.0, 0.0, 0.0, 1.0}},
          {{0.5f, 0.5f, 0.5f, kInf}, {1.0f, 1.0f, 1.0f, 0.0f}, {0.0, 0.0, 0.0, 1.0}},
          {{0.5f, 0.5f, 0.5f, 0.5f}, {1.0f, kInf, -1.0f, 0.0f}, {0.0, 0.0, 0.0, 0.0}},
      };
      for (const TwoPin& p : flare_pins) {
        double out[4];
        SunFlareMirror(p.t, p.c31, out);
        close(out, p.want);
      }
      struct GlowPin { float t[4]; double want[4]; };
      const GlowPin glow_pins[] = {
          // sun glow pins (light_formulas.post_moon_glow, c255.x 0.20000000298023224f): the texel, the value (rgb, alpha)
          {{1.0f, 1.0f, 1.0f, 1.0f}, {0.20000000298023224, 0.20000000298023224, 0.20000000298023224, 1.0}},
          {{0.5f, 0.25f, 0.125f, 0.75f}, {0.07500000111758709, 0.037500000558793545, 0.018750000279396772, 0.75}},
          {{1.0f, 1.0f, 1.0f, 0.03921568766236305f}, {0.007843137649344467, 0.007843137649344467, 0.007843137649344467, 0.03921568766236305}},
          {{0.20000000298023224f, 0.4000000059604645f, 0.6000000238418579f, 0.0f}, {0.0, 0.0, 0.0, 0.0}},
          {{kNaN, 0.5f, 0.5f, 1.0f}, {0.0, 0.10000000149011612, 0.10000000149011612, 1.0}},
          {{0.5f, 0.5f, 0.5f, kNaN}, {0.0, 0.0, 0.0, 0.0}},
          {{0.5f, kInf, -0.5f, 0.5f}, {0.05000000074505806, 0.0, 0.0, 0.5}},
          {{0.5f, 0.5f, 0.5f, 3.0f}, {0.30000000447034836, 0.30000000447034836, 0.30000000447034836, 1.0}},
      };
      for (const GlowPin& p : glow_pins) {
        double out[4];
        SunGlowMirror(p.t, out);
        close(out, p.want);
      }
      struct DiscPin { float t[4]; float c31[4]; float c20w; double want[4]; };
      const DiscPin disc_pins[] = {
          // sun disc pins (light_formulas.post_sun_disc): the texel, c31, c20.w, the value (rgb, alpha)
          {{1.0f, 1.0f, 1.0f, 1.0f}, {0.8767038583755493f, 0.691234827041626f, 0.34137076139450073f, 0.0f}, 1.0f, {0.8767038583755493, 0.691234827041626, 0.34137076139450073, 1.0}},
          {{0.5f, 0.25f, 0.125f, 0.75f}, {0.8767038583755493f, 0.691234827041626f, 0.34137076139450073f, 0.0f}, 1.0f, {0.328763946890831, 0.12960653007030487, 0.032003508880734444, 0.75}},
          {{1.0f, 1.0f, 1.0f, 0.501960813999176f}, {0.8767038583755493f, 0.691234827041626f, 0.34137076139450073f, 0.0f}, 1.0f, {0.44007098238640907, 0.3469727964463942, 0.17135474526510208, 0.501960813999176}},
          {{1.0f, 0.5f, 0.25f, 1.0f}, {2.0f, 1.0f, 0.5f, 0.0f}, 8.0f, {1.0, 1.0, 1.0, 1.0}},
          {{kNaN, 0.5f, 0.5f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f}, 1.0f, {0.0, 0.5, 0.5, 1.0}},
          {{0.5f, 0.5f, 0.5f, kNaN}, {1.0f, 1.0f, 1.0f, 0.0f}, 1.0f, {0.0, 0.0, 0.0, 0.0}},
          {{0.5f, 0.5f, 0.5f, 0.5f}, {1.0f, kNaN, 1.0f, 0.0f}, 1.0f, {0.0, 0.0, 0.0, 0.0}},
          {{0.5f, 0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f, 0.0f}, kInf, {0.0, 0.0, 0.0, 0.0}},
          {{0.5f, 0.5f, 0.5f, 0.5f}, {-1.0f, 1.0f, 1.0f, 0.0f}, 1.0f, {0.0, 0.25, 0.25, 0.5}},
      };
      for (const DiscPin& p : disc_pins) {
        double out[4];
        SunDiscMirror(p.t, p.c31, p.c20w, out);
        close(out, p.want);
      }
      struct OccluderPin { float alpha; float world[2]; float eye[2]; float c77x; double want; };
      const OccluderPin occluder_pins[] = {
          // sun occluder pins (light_formulas.post_cloud_occluder, c255 {1000.0f, 0.0f, 0.0010000000474974513f, 1.0f}): the alpha of the texel, the world x y, the eye c47.xy, c77.x, the alpha the test sees
          {1.0f, {90.0f, 158.0f}, {90.09701538085938f, 158.37025451660156f}, 0.5f, 0.5},
          {0.2980392277240753f, {90.0f, 158.0f}, {90.09701538085938f, 158.37025451660156f}, 0.5f, 0.14901961386203766},
          {0.29411765933036804f, {90.0f, 158.0f}, {90.09701538085938f, 158.37025451660156f}, 0.5f, 0.14705882966518402},
          {1.0f, {1290.0f, 958.0f}, {90.09701538085938f, 158.37025451660156f}, 0.5f, 0.2790327741682076},
          {1.0f, {3000.0f, -3000.0f}, {90.09701538085938f, 158.37025451660156f}, 0.5f, 0.0},
          {0.5f, {1090.0f, 158.0f}, {90.0f, 158.0f}, 2.0f, 1.0},
          {kNaN, {90.0f, 158.0f}, {90.09701538085938f, 158.37025451660156f}, 0.5f, 0.0},
          {1.0f, {kNaN, 158.0f}, {90.09701538085938f, 158.37025451660156f}, 0.5f, 0.0},
          {1.0f, {90.0f, 158.0f}, {kInf, 0.0f}, 0.5f, 0.0},
          {1.0f, {90.0f, 158.0f}, {90.09701538085938f, 158.37025451660156f}, kNaN, 0.0},
          {1.0f, {90.0f, 158.0f}, {90.09701538085938f, 158.37025451660156f}, -1.0f, 0.0},
      };
      for (const OccluderPin& p : occluder_pins) {
        const double a = SunOccluderAlphaMirror(p.alpha, p.world, p.eye, p.c77x);
        CHECK(642, a >= 0.0 && a <= 1.0 && std::fabs(a - p.want) <= 1e-12);
        worst = std::max(worst, std::fabs(a - p.want));
      }
      CHECK(642, std::size(beam_pins) == 6 && std::size(flare_pins) == 6 && std::size(glow_pins) == 8 && std::size(disc_pins) == 9 &&
                     std::size(occluder_pins) == 11);
      std::cout << "sun layer: the beam source's, the flare's, the moon glow's, the sun disc's and the cloud occluder's mirrors against the "
                   "reference, 40 points: worst difference " << worst << "\n";
      CHECK(642, worst < 1e-12);
      // The shaders' own literals are the captured ones.
      CHECK(642, kSunBeamScale == bits(0x3E2AAAAB) && kSunGlowScale == 0.2f && kSunFadeStart == 1000.0f && kSunFadeRate == 0.001f);
      // The captured records through their mirrors. The beam source at a white texel is c31 / 6; the sun
      // disc's alpha test (greater than 0.502) keeps the texture's core and drops its rim; the moon glow's
      // (0.0392) and the occluder's (0.149: a texel alpha of 77 / 255 at half is kept; 76 / 255, half of which is the reference itself, is not).
      const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
      double out[4];
      SunBeamMirror(white, beam.ps[0], out);
      CHECK(642, std::fabs(out[0] - 0.136661) < 1e-6 && out[3] == 1.0 && PostLevelIndex(float(out[0]), kSunLevels) == 140);
      const float rim[4] = {1.0f, 1.0f, 1.0f, 0.5f}, core[4] = {1.0f, 1.0f, 1.0f, 0.51f};
      SunDiscMirror(rim, disc.ps[1], disc.ps[0][3], out);
      CHECK(642, !QuadAlphaKept(capture::QuadAlphaTest(disc), float(out[3]), disc.alpha_ref));
      SunDiscMirror(core, disc.ps[1], disc.ps[0][3], out);
      CHECK(642, QuadAlphaKept(capture::QuadAlphaTest(disc), float(out[3]), disc.alpha_ref) && out[0] > 0.44 && out[0] < 0.45);
      const float under[2] = {90.0f, 158.0f};
      CHECK(642, QuadAlphaKept(true, float(SunOccluderAlphaMirror(77.0f / 255.0f, under, occluder.ps[2], occluder.ps[3][0])), occluder.alpha_ref) &&
                     !QuadAlphaKept(true, float(SunOccluderAlphaMirror(76.0f / 255.0f, under, occluder.ps[2], occluder.ps[3][0])), occluder.alpha_ref));
      // With the scene clouds' reference (0.0196) the same occluder would keep a texel of 11 / 255: the two
      // depths are not one (review of plan Task 10, Important 1).
      CHECK(642, SunOccluderAlphaMirror(11.0f / 255.0f, under, occluder.ps[2], occluder.ps[3][0]) > 0.0196 &&
                     !QuadAlphaKept(true, float(SunOccluderAlphaMirror(11.0f / 255.0f, under, occluder.ps[2], occluder.ps[3][0])), occluder.alpha_ref));
    }

    // 643: nothing that is no number leaves a mirror, and nothing outside the target's range.
    {
      const float values[] = {kNaN, kInf, -kInf, -1.0f, 0.0f, 1e-30f, 0.5f, 1.0f, 64.0f, 3e38f};
      int points = 0;
      for (float t0 : values) {
        for (float ta : values) {
          for (float c : values) {
            for (float s : values) {
              const float t[4] = {t0, 0.5f, ta, ta}, c31[4] = {c, 1.0f, c, c};
              double out[4];
              SunBeamMirror(t, c31, out);
              for (int i = 0; i < 4; ++i) CHECK(643, out[i] >= 0.0 && out[i] <= 1.0);
              SunFlareMirror(t, c31, out);
              for (int i = 0; i < 4; ++i) CHECK(643, out[i] >= 0.0 && out[i] <= 1.0);
              SunGlowMirror(t, out);
              for (int i = 0; i < 4; ++i) CHECK(643, out[i] >= 0.0 && out[i] <= 1.0);
              SunDiscMirror(t, c31, s, out);
              for (int i = 0; i < 4; ++i) CHECK(643, out[i] >= 0.0 && out[i] <= 1.0);
              const float world[2] = {t0, c}, eye[2] = {s, 0.5f};
              const double a = SunOccluderAlphaMirror(ta, world, eye, c);
              CHECK(643, a >= 0.0 && a <= 1.0);
              ++points;
            }
          }
        }
      }
      CHECK(643, points == 10000);
    }

    // The 10-bit picture of the pins (unorm_texel, 16 x 16) and a quarter of it.
    float unorm[16 * 16 * 3], quarter[16 * 16 * 3];
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 16; ++x) {
        for (int ch = 0; ch < 3; ++ch) {
          unorm[(y * 16 + x) * 3 + ch] = float(double((37 * x + 101 * y + 59 * ch) % 1024) / 1023.0);
          quarter[(y * 16 + x) * 3 + ch] = 0.25f * unorm[(y * 16 + x) * 3 + ch];
        }
      }
    }
    const PostPicture unorm_pic{unorm, 16, 16}, quarter_pic{quarter, 16, 16};

    // 644: the radial blur against the reference (light_formulas.post_radial): the captured registers of a
    // frame with the sun behind the camera (the centre in the picture's middle), a centre outside the
    // picture with both factors 1, and a centre on the picture's top edge (c31.y exactly 0, as captured).
    {
      struct RadialPin { uint32_t x, y; double out[3]; };
      static const RadialPin kReal[] = {
          // real radial pins (a quarter of unorm_texel as a point texture read at texel floor(u * 16 + 0.5), floor(v * 16 + 0.5)), c31.xy, c46.x, c47.x {0.5f, 0.5f, 0.8700000047683716f, 0.9350000619888306f}: the pixel (u = x / 16, v = y / 16), the value
          {0, 0, {0.12066428460653203, 0.15729953612402622, 0.19393479217571166}},
          {12, 3, {0.503901224504457, 0.5405364845864565, 0.4852271307974158}},
          {8, 8, {0.04967492491394564, 0.08631018499594534, 0.12294543561245769}},
          {15, 15, {0.4057886287771564, 0.4424238880905299, 0.21496519530881011}},
      };
      static const RadialPin kMade[] = {
          // made radial pins (a quarter of unorm_texel as a point texture read at texel floor(u * 16 + 0.5), floor(v * 16 + 0.5)), c31.xy, c46.x, c47.x {-0.30000001192092896f, 1.399999976158142f, 1.0f, 1.0f}: the pixel (u = x / 16, v = y / 16), the value
          {0, 0, {0.0, 0.057673510164022446, 0.11534702032804489}},
          {12, 3, {0.7302052974700928, 0.7878788113594055, 0.8455523252487183}},
          {8, 8, {0.07820136845111847, 0.1358748823404312, 0.19354838132858276}},
          {15, 15, {0.02150537632405758, 0.07917888462543488, 0.13685239851474762}},
      };
      static const RadialPin kEdge[] = {
          // edge radial pins (as the real ones), c31.xy, c46.x, c47.x {0.382099986076355f, 0.0f, 0.8700000047683716f, 0.9350000619888306f}: the pixel, the value
          {0, 0, {0.02273804999312984, 0.0593733069926832, 0.09600856633423856}},
          {12, 3, {0.4087836412407442, 0.44541889326901063, 0.4820541533510102}},
          {8, 8, {0.4539571963036139, 0.2714928571381059, 0.30812811353961067}},
          {15, 15, {0.37831467864579804, 0.4149499383286594, 0.32412777810549465}},
      };
      const float real[4] = {0.5f, 0.5f, 0.8700000047683716f, 0.9350000619888306f};
      const float made[4] = {-0.30000001192092896f, 1.399999976158142f, 1.0f, 1.0f};
      const float edge[4] = {0.382099986076355f, 0.0f, 0.8700000047683716f, 0.9350000619888306f};
      CHECK(644, SunRadialUsable(real) && SunRadialUsable(made) && SunRadialUsable(edge));
      double worst = 0.0;
      auto run = [&](const RadialPin* pins, size_t count, const float* radial) {
        CHECK(644, count == 4);
        for (size_t n = 0; n < count; ++n) {
          double out[3];
          SunRadialMirror(quarter_pic, pins[n].x, pins[n].y, radial, out);
          for (int i = 0; i < 3; ++i) {
            CHECK(644, out[i] >= 0.0 && out[i] <= 1.0);
            worst = std::fmax(worst, std::fabs(out[i] - pins[n].out[i]));
          }
        }
      };
      run(kReal, std::size(kReal), real);
      run(kMade, std::size(kMade), made);
      run(kEdge, std::size(kEdge), edge);
      std::printf("sun layer: the radial blur's mirror against the reference, 12 points: worst difference %.2e\n", worst);
      CHECK(644, worst < 1e-12);
      // The taps: the first at the pixel itself with half the weight, each next one nearer the centre by s
      // and lighter by g; the weights' sum times 4 / 7 is 2.54 with the captured c46.x (so the target's
      // clamp is part of the picture).
      SunRadialTap taps[kSunRadialTaps];
      SunRadialTaps(0.75, 0.25, real, taps);
      const double s = (double(real[3]) - 1.0) * (4.0 / 7.0) + 1.0, g = (double(real[2]) - 1.0) * (4.0 / 7.0) + 1.0;
      CHECK(644, taps[0].u == 0.75 && taps[0].v == 0.25 && taps[0].w == 0.5);
      double sum = 0.0;
      for (uint32_t k = 0; k < kSunRadialTaps; ++k) {
        CHECK(644, std::fabs(taps[k].u - (0.5 + 0.25 * std::pow(s, double(k)))) < 1e-15 &&
                       std::fabs(taps[k].v - (0.5 - 0.25 * std::pow(s, double(k)))) < 1e-15 && std::fabs(taps[k].w - 0.5 * std::pow(g, double(k))) < 1e-15);
        if (k) CHECK(644, taps[k].w < taps[k - 1].w && std::fabs(taps[k].u - 0.5) < std::fabs(taps[k - 1].u - 0.5));
        sum += taps[k].w;
      }
      CHECK(644, kSunRadialTaps == 14 && std::fabs(sum * kSunRadialNorm - 2.541) < 2e-3);
      // At the centre every tap is the centre: the value times the weights' sum, clamped at 1.
      double out[3];
      SunRadialMirror(unorm_pic, 8, 8, real, out);
      for (int i = 0; i < 3; ++i) CHECK(644, out[i] == std::fmin(1.0, double(unorm[(8 * 16 + 8) * 3 + i]) * sum * kSunRadialNorm) || std::fabs(out[i] - std::fmin(1.0, double(unorm[(8 * 16 + 8) * 3 + i]) * sum * kSunRadialNorm)) < 1e-12);
      // The texel a tap reads: the point fetch with its half texel, clamped to the source; texel 0 for a
      // coordinate that is no number or below the picture, the last beyond it.
      CHECK(644, SunRadialTexel(0.0, 16) == 0 && SunRadialTexel(0.5 / 16.0 - 1e-9, 16) == 0 && SunRadialTexel(0.5 / 16.0 + 1e-9, 16) == 1 &&
                     SunRadialTexel(0.5, 16) == 8 && SunRadialTexel(1.0, 16) == 15 && SunRadialTexel(40.0, 16) == 15 &&
                     SunRadialTexel(-3.0, 16) == 0 && SunRadialTexel(double(kNaN), 16) == 0 && SunRadialTexel(1e300, 16) == 15 &&
                     SunRadialTexel(0.3, 0) == 0 && SunRadialTexel(0.99, 1) == 0);
      // Registers outside their bounds, or no numbers: not usable, no tap, black.
      for (int i = 0; i < 4; ++i) {
        for (float v : {kNaN, kInf, -kInf, 4.5f, -4.5f}) {
          float bad[4] = {real[0], real[1], real[2], real[3]};
          bad[i] = v;
          CHECK(644, !SunRadialUsable(bad));
          SunRadialMirror(unorm_pic, 3, 3, bad, out);
          CHECK(644, out[0] == 0.0 && out[1] == 0.0 && out[2] == 0.0);
          SunRadialTaps(0.5, 0.5, bad, taps);
          for (const SunRadialTap& tap : taps) CHECK(644, tap.w == 0.0 && tap.u == 0.0 && tap.v == 0.0);
        }
      }
      for (int i = 2; i < 4; ++i) {
        for (float v : {0.0f, -0.5f, 1.0001f}) {
          float bad[4] = {real[0], real[1], real[2], real[3]};
          bad[i] = v;
          CHECK(644, !SunRadialUsable(bad));
        }
      }
      const float corner[4] = {4.0f, -4.0f, 1.0f, std::numeric_limits<float>::min()};
      CHECK(644, SunRadialUsable(corner) && !SunRadialUsable(nullptr));
      // A source with texels that are no numbers, and no source: numbers in the range.
      float broken[4 * 4 * 3];
      for (float& v : broken) v = kNaN;
      broken[0] = kInf;
      broken[5] = -3.0f;
      const PostPicture broken_pic{broken, 4, 4};
      for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
          SunRadialMirror(broken_pic, x, y, real, out);
          for (int i = 0; i < 3; ++i) CHECK(644, out[i] >= 0.0 && out[i] <= 1.0);
        }
      }
      SunRadialMirror(PostPicture{}, 1, 1, real, out);
      CHECK(644, out[0] == 0.0 && out[1] == 0.0 && out[2] == 0.0);
      // One texel that is no number among numbers counts as 0 and does not take the others' sum away: the
      // picture with it is the picture with a black texel in its place.
      {
        float holed[16 * 16 * 3], black[16 * 16 * 3];
        std::memcpy(holed, quarter, sizeof(holed));
        std::memcpy(black, quarter, sizeof(black));
        for (int ch = 0; ch < 3; ++ch) {
          holed[(8 * 16 + 9) * 3 + ch] = ch == 1 ? kInf : kNaN;
          black[(8 * 16 + 9) * 3 + ch] = 0.0f;
        }
        const PostPicture holed_pic{holed, 16, 16}, black_pic{black, 16, 16};
        double with_hole[3], with_black[3];
        int differ = 0;
        for (uint32_t y = 6; y < 11; ++y) {
          for (uint32_t x = 7; x < 12; ++x) {
            SunRadialMirror(holed_pic, x, y, real, with_hole);
            SunRadialMirror(black_pic, x, y, real, with_black);
            SunRadialMirror(quarter_pic, x, y, real, out);
            for (int i = 0; i < 3; ++i) {
              // (the pixel beside the centre reads the holed texel with every tap: black there, as the black one)
              CHECK(644, with_hole[i] == with_black[i] && (with_hole[i] > 0.0 || (x == 9 && y == 8)));
              differ += with_hole[i] != out[i];
            }
          }
        }
        CHECK(644, differ > 0);  // (the texel is read by these pixels: the case is not empty)
      }
    }

    // 645: the reduction against the reference (light_formulas.post_reduce): the mean of the pixel's four
    // texels at the game's ratio 2.
    {
      struct ReducePin { uint32_t x, y; double out[3]; };
      static const ReducePin kPins[] = {
          // reduce pins (unorm_texel, 16 x 16, linear, to 8 x 8): the pixel, the value
          {0, 0, {0.06744868215173483, 0.12512218859046698, 0.18279570154845715}},
          {3, 5, {0.2707722410559654, 0.32844575494527817, 0.3861192539334297}},
          {7, 7, {0.7047898373566568, 0.5122189661487937, 0.0694037158973515}},
      };
      double worst = 0.0;
      for (const ReducePin& p : kPins) {
        double out[3];
        SunReduceMirror(unorm_pic, p.x, p.y, 2, out);
        for (int i = 0; i < 3; ++i) worst = std::fmax(worst, std::fabs(out[i] - p.out[i]));
      }
      std::printf("sun layer: the reduction's mirror against the reference, 3 points: worst difference %.2e\n", worst);
      CHECK(645, std::size(kPins) == 3 && worst < 1e-12);
      double out[3];
      SunReduceMirror(unorm_pic, 2, 6, 2, out);
      for (int i = 0; i < 3; ++i) {
        const double mean = 0.25 * (double(unorm[(12 * 16 + 4) * 3 + i]) + double(unorm[(12 * 16 + 5) * 3 + i]) +
                                    double(unorm[(13 * 16 + 4) * 3 + i]) + double(unorm[(13 * 16 + 5) * 3 + i]));
        CHECK(645, std::fabs(out[i] - mean) < 1e-15);
      }
      // Another ratio reads the block's middle; ratio 0 counts as 1 (the texel itself).
      SunReduceMirror(unorm_pic, 1, 1, 4, out);
      CHECK(645, std::fabs(out[0] - 0.25 * (double(unorm[(5 * 16 + 5) * 3]) + double(unorm[(5 * 16 + 6) * 3]) + double(unorm[(6 * 16 + 5) * 3]) +
                                             double(unorm[(6 * 16 + 6) * 3]))) < 1e-15);
      SunReduceMirror(unorm_pic, 3, 3, 0, out);
      CHECK(645, out[0] == double(unorm[(3 * 16 + 3) * 3]));
      // Texels above 1, below 0 and no numbers: numbers in [0, 1].
      float wild[4 * 4 * 3];
      for (int i = 0; i < 48; ++i) wild[i] = (i % 5 == 0) ? kNaN : (i % 5 == 1) ? 7.0f : (i % 5 == 2) ? -2.0f : (i % 5 == 3) ? kInf : 0.5f;
      const PostPicture wild_pic{wild, 4, 4};
      for (uint32_t y = 0; y < 2; ++y) {
        for (uint32_t x = 0; x < 2; ++x) {
          SunReduceMirror(wild_pic, x, y, 2, out);
          for (int i = 0; i < 3; ++i) CHECK(645, out[i] >= 0.0 && out[i] <= 1.0);
        }
      }
      // What the step stores: the level of 1,023 nearest the mean (controller ruling 25).
      CHECK(645, PostLevelIndex(0.5f, kSunLevels) == 512 && PostLevel(0.25f * (1.0f + 2.0f / 1023.0f), kSunLevels) == 256.0f / 1023.0f);
    }

    // 646: the layer's depth: the scene depth's texel a layer texel takes, its value, and the steps' words.
    {
      // The game's every second texel: texel (2 x, 2 y) of the 1120 x 720 copy, by point; at another scale the
      // top left texel of the block; never beyond the last texel.
      CHECK(646, SunDepthTexel(0, 2, 1119) == 0 && SunDepthTexel(1, 2, 1119) == 2 && SunDepthTexel(559, 2, 1119) == 1118 &&
                     SunDepthTexel(560, 2, 1119) == 1119 && SunDepthTexel(359, 2, 719) == 718 && SunDepthTexel(7, 4, 2239) == 28 &&
                     SunDepthTexel(5, 1, 100) == 5 && SunDepthTexel(5, 0, 100) == 5 && SunDepthTexel(0xFFFFFFFFu, 0xFFFFFFFFu, 7) == 7);
      CHECK(646, SunDepthValue(0.25f) == 0.25f && SunDepthValue(1.0f) == 1.0f && SunDepthValue(1.5f) == 1.0f && SunDepthValue(-0.5f) == 0.0f &&
                     SunDepthValue(kInf) == 1.0f && SunDepthValue(-kInf) == 0.0f && SunDepthValue(kNaN) == 0.0f);
      // (a depth that is no number is the nearest: nothing of the layer passes it)
      CHECK(646, !SunDepthPasses(0.5f, SunDepthValue(kNaN)));
      static_assert(kSunLayerDivisor == 2 && kSunSmallDivisor == 4 && kSunLevels == kPostBloomLevels);
      const ClayPostStepConstants depth = MakeClaySunStep(ClaySunStep::kDepth, 1120, 720, 560, 360);
      CHECK(646, depth.dir[0] == 2 && depth.dir[1] == 2 && depth.last[0] == 1119 && depth.last[1] == 719 && depth.levels == 0.0f &&
                     depth.texel[0] == 1.0f / 1120.0f && depth.texel[1] == 1.0f / 720.0f && depth.offset == 0.0f);
      const ClayPostStepConstants reduce = MakeClaySunStep(ClaySunStep::kReduce, 560, 360, 280, 180);
      CHECK(646, reduce.dir[0] == 2 && reduce.dir[1] == 2 && reduce.levels == 1023.0f && reduce.texel[0] == 1.0f / 560.0f &&
                     reduce.texel[1] == 1.0f / 360.0f && reduce.last[0] == 559 && reduce.last[1] == 359);
      const ClayPostStepConstants radial = MakeClaySunStep(ClaySunStep::kRadial, 280, 180, 280, 180);
      CHECK(646, radial.dir[0] == 0 && radial.dir[1] == 0 && radial.levels == 1023.0f && radial.texel[0] == 1.0f / 280.0f &&
                     radial.texel[1] == 1.0f / 180.0f && radial.last[0] == 279 && radial.last[1] == 179);
      // A scene target of twice the size (supersampling later): the same layer, the ratio 4.
      const ClayPostStepConstants scaled = MakeClaySunStep(ClaySunStep::kDepth, 2240, 1440, 560, 360);
      CHECK(646, scaled.dir[0] == 4 && scaled.dir[1] == 4 && scaled.last[0] == 2239 && scaled.last[1] == 1439);
      // Sizes that do not divide, or none: a ratio of 1 and a size of 1, never a division by zero.
      const ClayPostStepConstants odd = MakeClaySunStep(ClaySunStep::kReduce, 561, 0, 280, 0);
      CHECK(646, odd.dir[0] == 1 && odd.dir[1] == 1 && odd.last[0] == 560 && odd.last[1] == 0 && odd.texel[1] == 1.0f);
    }

    // 647: the layer's plan: the set's layer records in the records' order, each with its state and the
    // target of its kind; the scene's records skipped; one that cannot be drawn and the layer has none.
    {
      const QuadRecord set[9] = {glare, occluder, occluder, occluder, beam, flare, glow, disc, glare};
      const ClaySunPlan plan = PlanClaySunQuads(set, 9);
      CHECK(647, plan.drawable && plan.count == 7 && plan.by_target[0] == 3 && plan.by_target[1] == 1 && plan.by_target[2] == 3);
      const uint8_t want_record[7] = {1, 2, 3, 4, 5, 6, 7}, want_target[7] = {0, 0, 0, 1, 2, 2, 2};
      const ClaySunState want_state[7] = {ClaySunState::kDepth, ClaySunState::kDepth, ClaySunState::kDepth, ClaySunState::kReplace,
                                          ClaySunState::kAdd,   ClaySunState::kAlpha, ClaySunState::kAdd};
      for (int i = 0; i < 7; ++i) {
        CHECK(647, plan.items[i].record == want_record[i] && plan.items[i].target == want_target[i] && plan.items[i].state == want_state[i] &&
                       plan.items[i].kind == set[want_record[i]].kind);
      }
      // The target is the kind's whatever the order: a beam source after the disc is still layer 1's.
      const QuadRecord shuffled[3] = {disc, beam, occluder};
      const ClaySunPlan other = PlanClaySunQuads(shuffled, 3);
      CHECK(647, other.drawable && other.count == 3 && other.items[0].target == 2 && other.items[1].target == 1 && other.items[2].target == 0);
      // No layer record (a set of the scene's alone; an empty set): nothing to draw, nothing wrong.
      const QuadRecord scene_only[2] = {glare, glare};
      const ClaySunPlan none = PlanClaySunQuads(scene_only, 2);
      CHECK(647, none.drawable && none.count == 0 && none.by_target[1] + none.by_target[2] == 0);
      const ClaySunPlan empty = PlanClaySunQuads(nullptr, 0);
      CHECK(647, empty.drawable && empty.count == 0);
      // One layer record that cannot be drawn (its state; a value; no record at all): none of the layer,
      // also not those before it. The scene's furniture has its own plan and is not asked here.
      QuadRecord bad_state = flare;
      bad_state.depth = 0x00708760;
      QuadRecord bad_value = occluder;
      bad_value.ps[3][0] = kNaN;
      for (const QuadRecord& bad : {bad_state, bad_value, QuadRecord{}}) {
        const QuadRecord with[4] = {occluder, beam, bad, disc};
        const ClaySunPlan p = PlanClaySunQuads(with, 4);
        CHECK(647, !p.drawable && p.count == 0 && p.by_target[0] == 0 && p.by_target[1] == 0 && p.by_target[2] == 0);
      }
      // ... and an undrawable flare leaves the scene's plan as it was (two verdicts).
      {
        const QuadRecord with[3] = {glare, bad_state, glare};
        CHECK(647, !PlanClaySunQuads(with, 3).drawable);
        const ClayQuadPlan scene_plan = PlanClayQuads(with, 3, 16.0f);
        CHECK(647, scene_plan.drawable && scene_plan.count == 2 && scene_plan.later == 1);
      }
      // More than the bound, or a count without records: nothing, not drawable (no index leaves the array).
      QuadRecord many[capture::kMaxQuadRecords + 1];
      for (QuadRecord& q : many) q = flare;
      const ClaySunPlan full = PlanClaySunQuads(many, capture::kMaxQuadRecords);
      CHECK(647, full.drawable && full.count == capture::kMaxQuadRecords && full.by_target[2] == capture::kMaxQuadRecords &&
                     full.items[capture::kMaxQuadRecords - 1].record == capture::kMaxQuadRecords - 1);
      CHECK(647, !PlanClaySunQuads(many, capture::kMaxQuadRecords + 1).drawable && !PlanClaySunQuads(nullptr, 3).drawable);
    }

    // 648: the decision, over every combination: the first thing missing decides; and the frame counts.
    {
      for (int b = 0; b < 256; ++b) {
        ClaySunInputs in;
        in.sun = b & 1;
        in.furniture = b & 2;
        in.bloom = b & 4;
        in.radial = b & 8;
        in.drawable = b & 16;
        in.quads = b & 32;
        in.objects = b & 64;
        in.textures = b & 128;
        const ClaySun want = !in.sun         ? ClaySun::kOff
                             : !in.furniture ? ClaySun::kNoFurniture
                             : !in.bloom     ? ClaySun::kNoBloom
                             : !in.radial    ? ClaySun::kNoRadial
                             : !in.drawable  ? ClaySun::kState
                             : !in.quads     ? ClaySun::kNoQuads
                             : !in.objects   ? ClaySun::kObjects
                             : !in.textures  ? ClaySun::kTexture
                                             : ClaySun::kDrawn;
        CHECK(648, DecideClaySun(in) == want);
        // A depth place that is not usable: its own verdict, after the set's and before the objects are asked.
        in.depth_place = false;
        const ClaySun without = (want == ClaySun::kObjects || want == ClaySun::kTexture || want == ClaySun::kDrawn) ? ClaySun::kDepthPlace : want;
        CHECK(648, DecideClaySun(in) == without);
      }
      static_assert(ClaySunInputs{}.depth_place);
      // With the switch off nothing else is asked: off for every input.
      static_assert(DecideClaySun(ClaySunInputs{}) == ClaySun::kOff);
      ClaySunInputs all;
      all.furniture = all.bloom = all.radial = all.drawable = all.quads = all.objects = all.textures = true;
      CHECK(648, DecideClaySun(all) == ClaySun::kOff);
      all.sun = true;
      CHECK(648, DecideClaySun(all) == ClaySun::kDrawn);
      ClaySunFrames n;
      for (ClaySun s : {ClaySun::kOff, ClaySun::kNoFurniture, ClaySun::kNoBloom, ClaySun::kNoRadial, ClaySun::kNoQuads, ClaySun::kState,
                        ClaySun::kObjects, ClaySun::kTexture, ClaySun::kDepthPlace, ClaySun::kDrawn, ClaySun::kCount}) {
        CountClaySunFrame(s, n);
      }
      CountClaySunFrame(ClaySun::kDrawn, n);
      CHECK(648, n.frames == 10 && n.by[size_t(ClaySun::kOff)] == 0 && n.by[size_t(ClaySun::kDrawn)] == 2 && n.by[size_t(ClaySun::kTexture)] == 1 &&
                     n.by[size_t(ClaySun::kNoFurniture)] == 1 && n.by[size_t(ClaySun::kNoBloom)] == 1 && n.by[size_t(ClaySun::kNoRadial)] == 1 &&
                     n.by[size_t(ClaySun::kNoQuads)] == 1 && n.by[size_t(ClaySun::kState)] == 1 && n.by[size_t(ClaySun::kObjects)] == 1 &&
                     n.by[size_t(ClaySun::kDepthPlace)] == 1);
    }

    // 652: where the layer's depth is taken among the scene's records (PlanSunDepthPlace; ruling 39): before
    // the first record a tree occluder of the game's layer names; after the last record in a frame without
    // one; and no layer at all when the frame's place cannot be trusted.
    {
      const uint32_t kNone = capture::kNoDepthPlace;
      // No occluder call: usable, after the last record (the frames by the trees; every frame before this rule).
      static_assert(PlanSunDepthPlace(0, 0, kNone, 950).usable && PlanSunDepthPlace(0, 0, kNone, 950).record == kNone);
      static_assert(PlanSunDepthPlace(0, 0, kNone, 0).usable);
      // One occluder that named record 700 of 950 (run 3's frame: one late leaf draw).
      static_assert(PlanSunDepthPlace(1, 0, 700, 950).usable && PlanSunDepthPlace(1, 0, 700, 950).record == 700);
      static_assert(PlanSunDepthPlace(3, 0, 0, 950).usable && PlanSunDepthPlace(3, 0, 0, 950).record == 0);
      static_assert(PlanSunDepthPlace(1, 0, 949, 950).usable);
      // Not usable: a call that failed (also beside one that named a record), a place beyond the records, a
      // call without a place, a place without a call.
      static_assert(!PlanSunDepthPlace(1, 1, kNone, 950).usable && !PlanSunDepthPlace(2, 1, 700, 950).usable);
      static_assert(!PlanSunDepthPlace(1, 0, 950, 950).usable && !PlanSunDepthPlace(1, 0, 951, 950).usable && !PlanSunDepthPlace(1, 0, 0, 0).usable);
      static_assert(!PlanSunDepthPlace(1, 0, kNone, 950).usable && !PlanSunDepthPlace(0, 0, 700, 950).usable);
      static_assert(!PlanSunDepthPlace(0, 1, kNone, 950).usable);
      // (a plan that is not usable names no record)
      static_assert(PlanSunDepthPlace(2, 1, 700, 950).record == kNone && PlanSunDepthPlace(1, 0, 950, 950).record == kNone);
      // The step is taken at the first drawn record at or beyond the plan's (the plan's own may not be drawable
      // this frame), never before it, and never in a frame whose place is after the last record.
      const SunDepthPlace at = PlanSunDepthPlace(1, 0, 700, 950);
      CHECK(652, !SunDepthBefore(at, 0) && !SunDepthBefore(at, 699) && SunDepthBefore(at, 700) && SunDepthBefore(at, 701) && SunDepthBefore(at, 949));
      const SunDepthPlace end = PlanSunDepthPlace(0, 0, kNone, 950);
      const SunDepthPlace bad = PlanSunDepthPlace(1, 1, 700, 950);
      for (size_t i : {size_t(0), size_t(700), size_t(949), size_t(0xFFFFFFFFu), ~size_t(0)}) {
        CHECK(652, !SunDepthBefore(end, i) && !SunDepthBefore(bad, i) && !SunDepthBefore(SunDepthPlace{}, i));
      }
      CHECK(652, SunDepthBefore(PlanSunDepthPlace(1, 0, 0, 1), 0));
      // (a plan that says it is not usable is not followed, whatever record it names)
      SunDepthPlace forged;
      forged.record = 700;
      CHECK(652, !SunDepthBefore(forged, 700) && !SunDepthBefore(forged, 900));
      // The verdict: a frame that would be drawn is not, for its depth place, and the Sun line says so.
      ClaySunInputs in;
      in.sun = in.furniture = in.bloom = in.radial = in.drawable = in.quads = in.objects = in.textures = true;
      in.depth_place = bad.usable;
      CHECK(652, DecideClaySun(in) == ClaySun::kDepthPlace && std::string(ClaySunName(ClaySun::kDepthPlace)) == "depth place");
      in.depth_place = at.usable;
      CHECK(652, DecideClaySun(in) == ClaySun::kDrawn);
    }

    // 649: the Sun line.
    {
      ClayStats st;
      CHECK(649, FormatSunText(st) == "Sun: off");
      st.sun_switch = true;
      st.sun = ClaySun::kDrawn;
      st.sun_drawn[0] = 3;
      st.sun_drawn[1] = 1;
      st.sun_drawn[2] = 2;
      st.sun_centre[0] = 0.3821f;
      st.sun_centre[1] = 0.3938f;
      st.sun_frames.frames = 900;
      st.sun_frames.by[size_t(ClaySun::kDrawn)] = 880;
      st.sun_frames.by[size_t(ClaySun::kTexture)] = 2;
      st.sun_frames.by[size_t(ClaySun::kNoFurniture)] = 18;
      st.post_previous = kPostPreviousWeight;
      CHECK(649, FormatSunText(st) ==
                     "Sun: on, drawn: occluders 3, layer 1 1, layer 2 2, centre (0.3821, 0.3938), previous frame's share 0.1020 | frames since start 900: drawn 880, "
                     "sun-layer {no furniture: 18, no bloom: 0, no radial registers: 0, no layer quad: 0, a record's state or values: 0, "
                     "post-objects: 0, quad-texture: 2, depth place: 0}");
      // (a layer whose depth was taken among the records says before which)
      st.sun_depth_record = 700;
      CHECK(649, FormatSunText(st).rfind("Sun: on, drawn: occluders 3, layer 1 1, layer 2 2, centre (0.3821, 0.3938), depth before record 700, previous frame's share", 0) == 0);
      st.sun_depth_record = capture::kNoDepthPlace;
      const std::pair<ClaySun, const char*> words[] = {{ClaySun::kNoFurniture, "not drawn (sun-layer: no furniture)"},
                                                       {ClaySun::kNoBloom, "not drawn (sun-layer: no bloom)"},
                                                       {ClaySun::kNoRadial, "not drawn (sun-layer: no radial registers)"},
                                                       {ClaySun::kNoQuads, "not drawn (sun-layer: no layer quad)"},
                                                       {ClaySun::kState, "not drawn (sun-layer: a record's state or values)"},
                                                       {ClaySun::kObjects, "not drawn (sun-layer: post-objects)"},
                                                       {ClaySun::kTexture, "not drawn (sun-layer: quad-texture)"},
                                                       {ClaySun::kDepthPlace, "not drawn (sun-layer: depth place)"},
                                                       {ClaySun::kOff, "not drawn (sun-layer: off)"}};
      for (const auto& [s, word] : words) {
        st.sun = s;
        CHECK(649, FormatSunText(st).rfind(std::string("Sun: on, ") + word + ", previous frame's share 0.1020 | frames", 0) == 0);
      }
      CHECK(649, std::string(kSunLayerReason) == "sun-layer");
      // (whole with every count at its largest and a centre that is no number)
      st.sun = ClaySun::kDrawn;
      st.sun_drawn[0] = st.sun_drawn[1] = st.sun_drawn[2] = ~0u;
      st.sun_centre[0] = kNaN;
      st.sun_centre[1] = kInf;
      st.sun_frames.frames = ~0ull;
      for (uint64_t& v : st.sun_frames.by) v = ~0ull;
      const std::string longest = FormatSunText(st);
      st.post_previous = 0.0f;
      CHECK(649, FormatSunText(st).find(", previous frame's share 0.0000 | frames") != std::string::npos);
      st.post_previous = std::numeric_limits<float>::quiet_NaN();
      CHECK(649, FormatSunText(st).find(", previous frame's share 0.0000 | frames") != std::string::npos);
      CHECK(649, longest.size() > 300 && longest.find("centre (0.0000, 0.0000)") != std::string::npos &&
                     longest.rfind("depth place: 18446744073709551615}") == longest.size() - 34);
    }

    // 650: the steps' constants with the sun's switch: the bright pass's sun flag only with the bloom's flag
    // and the block's radial part, the registers copied; and without the switch the constants of before.
    {
      capture::PostBlock b{};
      b.valid = true;
      const float composite[6] = {2.9736f, 0.25f, 50.0f, -0.00495f, 1.10216f, 0.45455f}, bright[5] = {1.4868f, 0.2f, 2.0f, 0.08761f, 0.13033f};
      std::memcpy(b.composite, composite, sizeof(composite));
      std::memcpy(b.bright, bright, sizeof(bright));
      for (float& t : b.table) t = 0.5f;
      for (int s2 = 0; s2 < 2; ++s2) {
        b.blur[s2][0] = 0.4f;
        for (int i = 1; i < 7; ++i) b.blur[s2][i] = 0.05f;
      }
      b.parts = capture::kPostPartBlur;
      static float ramp[256][3];
      for (int i = 0; i < 256; ++i) ramp[i][0] = ramp[i][1] = ramp[i][2] = float(i) / 255.0f;
      const float c31[4] = {0.3821f, 0.3938f, 0.0f, 0.0f}, c46[4] = {0.87f, 0.0f, 0.0f, 0.0f}, c47[4] = {0.935f, 0.0f, 0.0f, 0.0f};
      ClayPostConstants plain, off, on, no_bloom;
      CHECK(650, MakeClayPostConstants(b, ramp, true, true, &plain) && plain.sun_on == 0 && plain.radial[0] == 0.0f);  // (no radial part)
      CHECK(650, capture::SetPostRadial(&b, c31, c46, c47) && (b.parts & capture::kPostPartRadial));
      CHECK(650, MakeClayPostConstants(b, ramp, true, false, &off) && MakeClayPostConstants(b, ramp, true, true, &on) &&
                     MakeClayPostConstants(b, ramp, false, true, &no_bloom));
      CHECK(650, off.sun_on == 0 && on.sun_on == 1 && on.bloom_on == 1 && no_bloom.sun_on == 0 && no_bloom.bloom_on == 0);
      CHECK(650, on.radial[0] == 0.3821f && on.radial[1] == 0.3938f && on.radial[2] == 0.87f && on.radial[3] == 0.935f && SunRadialUsable(on.radial));
      // The switch changes the one word and nothing else.
      on.sun_on = 0;
      CHECK(650, std::memcmp(&on, &off, sizeof(on)) == 0);
      static_assert(offsetof(ClayPostConstants, sun_on) == 56 && offsetof(ClayPostConstants, radial) == 128);
    }

    // 651: where a layer quad's pixels are sampled (ruling 38): the game's integral window position, through a
    // viewport origin of half a pixel. The window positions are the reference's (post_sun_layer._screen on a
    // 560 x 360 layer: tests/test_post_literals.py holds the same four points).
    {
      static_assert(kSunQuadViewportOrigin == 0.5f && kSunQuadViewportOrigin == kShadowViewportOrigin);
      // (the centre of native pixel i is the game's sample point of pixel i)
      for (uint32_t i : {0u, 1u, 279u, 559u}) CHECK(651, SunQuadGameSample(i) == double(i));
      struct Pin { double ndc_x, ndc_y, x, y; };
      const Pin kPins[] = {{-1.0, 1.0, 0.0, 0.0}, {0.25, -0.5, 350.0, 270.0}, {-0.2358, 0.2124, 213.976, 141.768}, {1.0, -1.0, 560.0, 360.0}};
      for (const Pin& p : kPins) {
        const double x = SunQuadWindow(p.ndc_x, 560, false), y = SunQuadWindow(p.ndc_y, 360, true);
        // the reference's position, half a pixel on: its sample point k is the native centre k + 0.5
        CHECK(651, std::fabs(x - (p.x + 0.5)) < 1e-9 && std::fabs(y - (p.y + 0.5)) < 1e-9);
      }
      // A left edge at the game's window x = 213.3: the game's first covered pixel is 214 (its sample points are
      // the integers). Natively, by the centre rule, the edge moved by the origin gives 214; left where it is, 213.
      auto first_covered = [](double edge) { return uint32_t(std::ceil(edge - 0.5)); };  // (Direct3D: a pixel's centre)
      const double edge = 213.3;
      CHECK(651, uint32_t(std::ceil(edge)) == 214 && first_covered(edge + double(kSunQuadViewportOrigin)) == 214 && first_covered(edge) == 213);
    }
  }
}
// --- end of the sun layer's cases ---
// --- the previous-frame blend's cases (plan Task 15) ---
static void PreviousBlendTests() {
  const float kNaN = std::numeric_limits<float>::quiet_NaN(), kInf = std::numeric_limits<float>::infinity();
  // 660: the blend against the reference (light_formulas.post_previous_blend): the composite's level mixed
  // with the previous picture's by the share and stored as a level of 255; no share, or one that is no
  // number or outside (0, 1], is the composite's value bit for bit.
  {
    struct PreviousPin { float composite[3], previous[3], share; double want[3]; };
    static const PreviousPin kPins[] = {
        // previous blend pins (light_formulas.post_previous_blend, the weight 0.10196079313755035f): the composite, the previous picture, the share, the front value
        {{0.7843137383460999f, 0.3921568691730499f, 0.0f}, {0.3921568691730499f, 0.7843137383460999f, 1.0f}, 0.10196079313755035f, {0.7450980392156863, 0.43137254901960786, 0.10196078431372549}},
        {{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.10196079313755035f, {0.8980392156862745, 0.8980392156862745, 0.8980392156862745}},
        {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 0.10196079313755035f, {0.10196078431372549, 0.10196078431372549, 0.10196078431372549}},
        {{0.14509804546833038f, 0.14901961386203766f, 0.15294118225574493f}, {0.14509804546833038f, 0.14901961386203766f, 0.15294118225574493f}, 0.10196079313755035f, {0.1450980392156863, 0.14901960784313725, 0.15294117647058825}},
        {{0.501960813999176f, 0.501960813999176f, 0.501960813999176f}, {0.5215686559677124f, 0.5176470875740051f, 0.48627451062202454f}, 0.10196079313755035f, {0.5058823529411764, 0.5019607843137255, 0.5019607843137255}},
        {{0.7843137383460999f, 0.3921568691730499f, 0.19607843458652496f}, {0.3921568691730499f, 0.7843137383460999f, 0.9803921580314636f}, 0.0f, {0.7843137383460999, 0.3921568691730499, 0.19607843458652496}},
        {{0.7843137383460999f, 0.3921568691730499f, 0.19607843458652496f}, {0.3921568691730499f, 0.7843137383460999f, 0.9803921580314636f}, 1.0f, {0.39215686274509803, 0.7843137254901961, 0.9803921568627451}},
        {{0.7843137383460999f, 0.3921568691730499f, 0.19607843458652496f}, {kNaN, kInf, -1.0f}, 0.10196079313755035f, {0.7058823529411765, 0.35294117647058826, 0.17647058823529413}},
        {{0.7843137383460999f, 0.3921568691730499f, 0.19607843458652496f}, {0.3921568691730499f, 0.7843137383460999f, 0.9803921580314636f}, kNaN, {0.7843137383460999, 0.3921568691730499, 0.19607843458652496}},
        {{0.7843137383460999f, 0.3921568691730499f, 0.19607843458652496f}, {0.3921568691730499f, 0.7843137383460999f, 0.9803921580314636f}, -0.5f, {0.7843137383460999, 0.3921568691730499, 0.19607843458652496}},
        {{0.7843137383460999f, 0.3921568691730499f, 0.19607843458652496f}, {0.3921568691730499f, 0.7843137383460999f, 0.9803921580314636f}, 1.5f, {0.7843137383460999, 0.3921568691730499, 0.19607843458652496}},
    };
    CHECK(660, std::size(kPins) == 11);
    double worst = 0.0;
    for (const PreviousPin& p : kPins) {
      for (int i = 0; i < 3; ++i) {
        const double got = PostPreviousMirror(p.composite[i], p.previous[i], p.share);
        CHECK(660, got >= 0.0 && got <= 1.0);
        worst = std::fmax(worst, std::fabs(got - p.want[i]));
        if (!(p.share > 0.0f && p.share <= 1.0f)) CHECK(660, got == double(p.composite[i]));
      }
    }
    std::printf("post: the previous-frame blend's mirror against the reference, 33 values: worst difference %.2e\n", worst);
    CHECK(660, worst < 1e-12);
    CHECK(660, kPostPreviousWeight == 0.10196079313755035f && std::fabs(double(kPostPreviousWeight) - 26.0 / 255.0) < 1e-8);
    // The share 0 is today's value bit for bit, for every level and whatever the previous picture holds.
    for (uint32_t level = 0; level <= 255; ++level) {
      const float v = float(level) / 255.0f;
      for (float prev : {0.0f, 1.0f, 0.3f, kNaN, kInf}) {
        const double got = PostPreviousMirror(v, prev, 0.0f);
        const float back = float(got);
        CHECK(660, std::memcmp(&back, &v, sizeof(v)) == 0);
      }
    }
    // A standing picture is itself at every level; one level off stays where it is (the blend's rounding
    // holds it: within one level of the picture without the blend).
    for (uint32_t level = 0; level <= 255; ++level) {
      const float v = float(level) / 255.0f;
      CHECK(660, PostPreviousMirror(v, v, kPostPreviousWeight) == double(float(level)) / 255.0 ||
                     std::fabs(PostPreviousMirror(v, v, kPostPreviousWeight) - double(level) / 255.0) < 1e-9);
      if (level > 0) {
        const float below = float(level - 1) / 255.0f;
        CHECK(660, std::fabs(PostPreviousMirror(v, below, kPostPreviousWeight) - double(level) / 255.0) < 1e-9);
      }
    }
    // From black to white the picture comes within one level of the new one in a bounded number of frames.
    float held = 0.0f;
    int frames = 0;
    while (frames < 200 && held < 254.0f / 255.0f) {
      held = float(PostPreviousMirror(1.0f, held, kPostPreviousWeight));
      ++frames;
    }
    CHECK(660, frames >= 2 && frames <= 4 && held >= 254.0f / 255.0f);
    // Values that are no numbers: a number in the range.
    for (float c : {kNaN, kInf, -kInf, -1.0f, 2.0f, 0.5f}) {
      for (float p : {kNaN, kInf, -kInf, -1.0f, 2.0f, 0.5f}) {
        for (float s : {kNaN, kInf, -1.0f, 0.0f, 0.102f, 1.0f, 2.0f}) {
          const double got = PostPreviousMirror(c, p, s);
          CHECK(660, got >= 0.0 && got <= 1.0);
        }
      }
    }
  }
  // 661: which front target a frame writes, which it reads, and with what share (StepClayFront): the share
  // is the game's only in a post frame with the switch on and the second target, right after a post frame,
  // and not on the oracle feed; 0 in the first frame after the targets were made, after a frame without
  // post, and with the switch off. With the switch off the one target of before is written, every frame.
  {
    static_assert(StepClayFront(ClayFrontState{}, false, false, false, false).share == 0.0f);
    ClayFrontState s;  // (the targets were just made)
    ClayFrontStep a = StepClayFront(s, true, true, true, false);
    CHECK(661, a.write == 1 && a.read == 0 && a.share == 0.0f && a.next.current == 1 && a.next.valid);
    ClayFrontStep b = StepClayFront(a.next, true, true, true, false);
    CHECK(661, b.write == 0 && b.read == 1 && b.share == kPostPreviousWeight && b.next.current == 0 && b.next.valid);
    ClayFrontStep c = StepClayFront(b.next, true, true, true, false);
    CHECK(661, c.write == 1 && c.read == 0 && c.share == kPostPreviousWeight);
    // A frame without post: nothing is written, and the frame after it has no previous picture.
    ClayFrontStep fallback = StepClayFront(c.next, false, true, true, false);
    CHECK(661, fallback.share == 0.0f && !fallback.next.valid && fallback.next.current == c.next.current);
    ClayFrontStep after = StepClayFront(fallback.next, true, true, true, false);
    CHECK(661, after.share == 0.0f && after.write != after.read && after.next.valid);
    CHECK(661, StepClayFront(after.next, true, true, true, false).share == kPostPreviousWeight);
    // The switch off: the target it has, no read, no share; the next frame with the switch on blends with it.
    for (uint32_t current : {0u, 1u}) {
      for (bool valid : {false, true}) {
        const ClayFrontStep off = StepClayFront(ClayFrontState{current, valid}, true, false, true, false);
        CHECK(661, off.write == current && off.share == 0.0f && off.next.current == current && off.next.valid);
        const ClayFrontStep none = StepClayFront(ClayFrontState{current, valid}, true, true, false, false);
        CHECK(661, none.write == current && none.share == 0.0f && none.next.current == current);
      }
    }
    CHECK(661, StepClayFront(ClayFrontState{0, true}, true, false, false, false).write == 0);
    // After a stretch in which the pass was not rendered (the view off): no share in the first frame, the
    // targets in the order they had, and the frame after it blends again. Without a gap nothing changes.
    {
      static_assert(ClayFrontAfterGap(ClayFrontState{1, true}, false).valid && ClayFrontAfterGap(ClayFrontState{1, true}, false).current == 1);
      static_assert(!ClayFrontAfterGap(ClayFrontState{1, true}, true).valid && ClayFrontAfterGap(ClayFrontState{1, true}, true).current == 1);
      static_assert(!ClayFrontAfterGap(ClayFrontState{0, false}, false).valid);
      const ClayFrontStep back = StepClayFront(ClayFrontAfterGap(ClayFrontState{1, true}, true), true, true, true, false);
      CHECK(661, back.share == 0.0f && back.write == 0 && back.read == 1 && back.next.valid);
      CHECK(661, StepClayFront(ClayFrontAfterGap(back.next, false), true, true, true, false).share == kPostPreviousWeight);
      CHECK(661, StepClayFront(ClayFrontAfterGap(ClayFrontState{1, true}, false), true, true, true, false).share == kPostPreviousWeight);
      // (review of plan Tasks 16 and 17, Minor 5: a Render whose scene is not the one after the last rendered
      // scene's, by more than the bound, starts from a gap as well: the scenes between were never drawn. The
      // next frame, and up to kClaySceneGapFrames on, is no gap; no last scene, or a frame number that went
      // back, is one.)
      static_assert(kClaySceneGapFrames == 4);
      static_assert(ClaySceneGap(false, 0, 1) && ClaySceneGap(false, 7, 8));
      static_assert(!ClaySceneGap(true, 7, 8) && !ClaySceneGap(true, 7, 7) && !ClaySceneGap(true, 7, 11));
      static_assert(ClaySceneGap(true, 7, 12) && ClaySceneGap(true, 7, 6) && ClaySceneGap(true, 7, 900));
      static_assert(ClaySceneGap(true, ~0ull, 0) && !ClaySceneGap(true, ~0ull - 1, ~0ull));
      CHECK(661, StepClayFront(ClayFrontAfterGap(ClayFrontState{1, true}, ClaySceneGap(true, 100, 130)), true, true, true, false).share == 0.0f);
      CHECK(661, StepClayFront(ClayFrontAfterGap(ClayFrontState{1, true}, ClaySceneGap(true, 100, 101)), true, true, true, false).share == kPostPreviousWeight);
    }
    // The oracle feed: never a share, and its picture is no previous picture for the frame after it.
    const ClayFrontStep fed = StepClayFront(ClayFrontState{0, true}, true, true, true, true);
    CHECK(661, fed.share == 0.0f && fed.write == 1 && !fed.next.valid);
    // No target but the two: a state that is none of them is taken as the first.
    const ClayFrontStep wild = StepClayFront(ClayFrontState{7, true}, true, true, true, false);
    CHECK(661, wild.write < 2 && wild.read < 2 && wild.write != wild.read);
    // Over every input: the share is the weight exactly when all five hold.
    for (int bits = 0; bits < 64; ++bits) {
      const bool post = bits & 1, wanted = bits & 2, second = bits & 4, fed2 = bits & 8, valid = bits & 16;
      const ClayFrontStep st = StepClayFront(ClayFrontState{uint32_t((bits >> 5) & 1), valid}, post, wanted, second, fed2);
      CHECK(661, (st.share == kPostPreviousWeight) == (post && wanted && second && !fed2 && valid) && (st.share == 0.0f || st.share == kPostPreviousWeight));
      CHECK(661, st.write < 2 && st.read < 2 && st.next.current < 2 && (st.share == 0.0f || st.write != st.read));
    }
  }
}
// --- end of the previous-frame blend's cases ---
// --- the scene's clouds' cases (plan Task 16; clay_logic.h, "Native clouds") ---
static void CloudTests() {
  using capture::PostQuadKind;
  using capture::QuadRecord;
  const float kNaN = std::numeric_limits<float>::quiet_NaN(), kInf = std::numeric_limits<float>::infinity();
  auto bits = [](uint32_t b) {
    float f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
  };
  // The first cloud layer of capture native_discovery_20261010_093600, frame 1 (towards the sun), its
  // registers rounded as the pins have them; the matrix is a made one (the capture's is the scene quads').
  QuadRecord cloud{};
  cloud.ps_hash = capture::kPostQuadTable[size_t(PostQuadKind::kCloud)].ps;
  cloud.vs_hash = capture::kPostQuadTable[size_t(PostQuadKind::kCloud)].vs;
  cloud.kind = PostQuadKind::kCloud;
  cloud.layer = 0;
  const float corners[4][5] = {{-3000.0f, -3000.0f, 500.0f, 0.0f, 0.0f}, {3000.0f, -3000.0f, 500.0f, 1.0f, 0.0f},
                               {-3000.0f, 3000.0f, 500.0f, 0.0f, 1.0f}, {3000.0f, 3000.0f, 500.0f, 1.0f, 1.0f}};
  std::memcpy(cloud.corners, corners, sizeof(corners));
  for (int i = 0; i < 4; ++i) {
    for (int k = 0; k < 4; ++k) cloud.view_proj[i][k] = float(i == k) + 0.01f * float(i * 4 + k);
  }
  const float regs[11][4] = {{0.00654f, 0.07078f, 0.09179f, 7.42912f}, {5.0f, 5.0f, 0.0f, 0.0f}, {0.34148f, 0.68296f, 0.0f, 0.0f},
                             {0.99215f, 0.83292f, 0.40139f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.5f, 0.0f, 0.0f, 0.0f},
                             {0.5f, 0.0f, 0.0f, 0.0f}, {0.9f, 0.0f, 0.0f, 0.0f}, {-1189.1177f, 244.89037f, 1582.1741f, 1.0f},
                             {-0.82365f, 0.32979f, 0.46135f, 0.0f}, {90.09702f, 158.37025f, 47.20699f, 1.0f}};
  std::memcpy(cloud.ps, regs, sizeof(regs));
  const uint32_t fetch[6] = {0x84000002u, 0x163E0054u, 0x003FE1FFu, 0x00A80D10u, 0x00000143u, 0x16420A18u};
  std::memcpy(cloud.fetch[0], fetch, sizeof(fetch));
  cloud.blend = 0x07060706u;
  cloud.depth = 0x00708766u;
  cloud.colour_mask = 0xF;
  cloud.alpha_control = 12;
  cloud.alpha_ref = bits(0x3CA0A0A1u);  // 5 / 255
  const float kScale = 7.42912f;
  const float kUnit[2] = {1.0f, 1.0f};

  // 670: the state a cloud record is drawn with, and what gives none.
  {
    static_assert(unsigned(PostQuadKind::kCloud) == 1 && kCloudRegisters == 11 && kCloudStepRegister == 11);
    static_assert(capture::kPostQuadTable[size_t(PostQuadKind::kCloud)].register_count == kCloudRegisters);
    CHECK(670, capture::QuadRecordValid(cloud) && CloudDrawState(cloud) && cloud.alpha_ref > 0.0196f && cloud.alpha_ref < 0.0197f);
    // (the scene quads' own rule refuses it: the record writes depth; the clouds have their own)
    CHECK(670, QuadDrawState(cloud) == ClayQuadState::kNone && ClayQuadPlaceOf(PostQuadKind::kCloud) == ClayQuadPlace::kNotDrawn);
    QuadRecord q = cloud;
    q.depth = 0x00708762u;  // (the same test without the depth write: drawable as well; the pass writes none either way)
    CHECK(670, CloudDrawState(q));
    q = cloud; q.blend = kQuadBlendAdd;
    CHECK(670, !CloudDrawState(q));
    q = cloud; q.blend = 0x00010001u;
    CHECK(670, !CloudDrawState(q));
    q = cloud; q.colour_mask = 7;
    CHECK(670, !CloudDrawState(q));
    q = cloud; q.depth &= ~2u;  // no depth test
    CHECK(670, !CloudDrawState(q));
    for (uint32_t func = 0; func < 8; ++func) {
      q = cloud;
      q.depth = (q.depth & ~0x70u) | (func << 4);
      CHECK(670, CloudDrawState(q) == (func == 6));
    }
    q = cloud; q.alpha_control = 4;  // the test off
    CHECK(670, !CloudDrawState(q));
    for (uint32_t func = 0; func < 8; ++func) {
      q = cloud;
      q.alpha_control = 8 | func;
      CHECK(670, CloudDrawState(q) == (func == 4));
    }
  }

  // 671: a cloud record's constants, each at the place the shader reads, and what gives none.
  {
    ClayQuadConstants c{};
    const float fix[2] = {0.5f, 0.25f};
    CHECK(671, MakeClayCloudConstants(cloud, kScale, 5, fix, &c));
    CHECK(671, c.kind == 2 && c.sampler == 1 && c.alpha_test == 1 && c.alpha_ref == cloud.alpha_ref && c.fix[0] == 0.5f && c.fix[1] == 0.25f &&
                   c.scene_max == kPostSceneMax && c.pad == 0);
    for (int i = 0; i < 4; ++i) {
      CHECK(671, c.corner[i][0] == corners[i][0] && c.corner[i][1] == corners[i][1] && c.corner[i][2] == 500.0f && c.corner[i][3] == 1.0f &&
                     c.coord[i][0] == corners[i][3] && c.coord[i][1] == corners[i][4] && c.coord[i][2] == 0.0f && c.coord[i][3] == 0.0f);
      CHECK(671, std::memcmp(c.view_proj[i], cloud.view_proj[i], sizeof(c.view_proj[i])) == 0);
    }
    // the registers the shader reads, and nothing of what it does not read
    CHECK(671, c.reg[0][0] == 0.0f && c.reg[0][1] == 0.0f && c.reg[0][2] == 0.0f && c.reg[0][3] == 7.42912f);
    CHECK(671, c.reg[1][0] == 5.0f && c.reg[1][1] == 5.0f && c.reg[2][0] == 0.34148f && c.reg[2][1] == 0.68296f);
    CHECK(671, c.reg[3][0] == 0.99215f && c.reg[3][1] == 0.83292f && c.reg[3][2] == 0.40139f && c.reg[3][3] == 0.0f);
    CHECK(671, c.reg[4][0] == 1.0f && c.reg[5][0] == 0.5f && c.reg[6][0] == 0.5f && c.reg[7][0] == 0.9f);
    CHECK(671, c.reg[8][0] == -1189.1177f && c.reg[8][1] == 244.89037f && c.reg[8][2] == 1582.1741f && c.reg[8][3] == 0.0f);
    CHECK(671, c.reg[9][0] == -0.82365f && c.reg[9][1] == 0.32979f && c.reg[9][2] == 0.46135f);
    CHECK(671, c.reg[10][0] == 90.09702f && c.reg[10][1] == 158.37025f && c.reg[10][2] == 0.0f && c.reg[10][3] == 0.0f);
    // The base level's texel step: the texture is 512 x 512 by its fetch constant.
    CHECK(671, c.reg[11][0] == 1.0f / 512.0f && c.reg[11][1] == 1.0f / 512.0f && c.reg[11][2] == 0.0f && c.reg[11][3] == 0.0f);
    float step[2];
    const uint32_t wide[6] = {0, 0, (255u << 13) | 1023u, 0, 0, 0};
    CloudTexelStep(wide, step);
    CHECK(671, step[0] == 1.0f / 1024.0f && step[1] == 1.0f / 256.0f);
    const uint32_t one[6] = {0, 0, 0, 0, 0, 0}, largest[6] = {0, 0, 0xFFFFFFFFu, 0, 0, 0};
    CloudTexelStep(one, step);
    CHECK(671, step[0] == 1.0f && step[1] == 1.0f);
    CloudTexelStep(largest, step);
    CHECK(671, step[0] == 1.0f / 8192.0f && step[1] == 1.0f / 8192.0f);
    static_assert(offsetof(ClayQuadConstants, reg) == 192 && offsetof(ClayQuadConstants, kind) == 384 && sizeof(ClayQuadConstants) == 416);
    // What gives none, `out` untouched.
    ClayQuadConstants keep;
    std::memset(&keep, 0x5A, sizeof(keep));
    auto refused = [&](const QuadRecord& q, float scale, const float* f) {
      ClayQuadConstants out = keep;
      return !MakeClayCloudConstants(q, scale, 0, f, &out) && std::memcmp(&out, &keep, sizeof(out)) == 0;
    };
    CHECK(671, !MakeClayCloudConstants(cloud, kScale, 0, kUnit, nullptr) && refused(cloud, kScale, nullptr));
    CHECK(671, refused(QuadRecord{}, kScale, kUnit));
    QuadRecord q = cloud;
    q.kind = PostQuadKind::kMoon;
    CHECK(671, refused(q, kScale, kUnit));
    q = cloud; q.kind = PostQuadKind::kCloudOccluder;
    CHECK(671, refused(q, kScale, kUnit));
    q = cloud; q.layer = 1;
    CHECK(671, refused(q, kScale, kUnit));
    q = cloud; q.blend = kQuadBlendAdd;
    CHECK(671, refused(q, kScale, kUnit));
    q = cloud; q.fetch[0][0] |= 0x4u;  // a sign field
    CHECK(671, refused(q, kScale, kUnit));
    // the scale: the frame's, within a thousandth
    CHECK(671, refused(cloud, kScale * 1.002f, kUnit) && refused(cloud, kScale * 0.998f, kUnit) && !refused(cloud, kScale * 1.0005f, kUnit));
    CHECK(671, refused(cloud, 0.0f, kUnit) && refused(cloud, kNaN, kUnit) && refused(cloud, kInf, kUnit) && refused(cloud, 65.0f, kUnit));
    for (float bad : {kNaN, kInf, 0.0f, -1.0f, 64.5f}) {
      q = cloud; q.ps[kCloudC20][3] = bad;
      CHECK(671, refused(q, kScale, kUnit) && refused(q, bad, kUnit));
    }
    // every value the shader is handed, at each bound
    for (float bad : {kNaN, kInf, -kInf}) {
      for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 5; ++k) {
          q = cloud; q.corners[i][k] = bad;
          CHECK(671, refused(q, kScale, kUnit));
        }
        for (int k = 0; k < 4; ++k) {
          q = cloud; q.view_proj[i][k] = bad;
          CHECK(671, refused(q, kScale, kUnit));
        }
      }
      const std::pair<uint32_t, int> read[] = {{kCloudC31, 0}, {kCloudC31, 1}, {kCloudC46, 0}, {kCloudC46, 1}, {kCloudC47, 0}, {kCloudC47, 1},
                                               {kCloudC47, 2}, {kCloudC51, 0}, {kCloudC69, 0}, {kCloudC70, 0}, {kCloudC71, 0}, {kCloudC77, 0},
                                               {kCloudC77, 1}, {kCloudC77, 2}, {kCloudC78, 0}, {kCloudC78, 1}, {kCloudC78, 2}, {kCloudC79, 0},
                                               {kCloudC79, 1}};
      CHECK(671, std::size(read) == 19);
      for (const auto& [reg, k] : read) {
        q = cloud; q.ps[reg][k] = bad;
        CHECK(671, refused(q, kScale, kUnit));
      }
      // (a component the shader does not read is not judged and not handed on)
      q = cloud; q.ps[kCloudC31][3] = bad;
      ClayQuadConstants out{};
      CHECK(671, MakeClayCloudConstants(q, kScale, 0, kUnit, &out) && out.reg[kCloudC31][3] == 0.0f);
      q = cloud; q.alpha_ref = bad;
      CHECK(671, refused(q, kScale, kUnit));
      const float f0[2] = {bad, 1.0f}, f1[2] = {1.0f, bad};
      CHECK(671, refused(cloud, kScale, f0) && refused(cloud, kScale, f1));
    }
    q = cloud; q.ps[kCloudC31][0] = 4096.5f;
    CHECK(671, refused(q, kScale, kUnit));
    q = cloud; q.ps[kCloudC31][0] = -4096.0f;
    CHECK(671, !refused(q, kScale, kUnit));
    q = cloud; q.ps[kCloudC46][1] = 5000.0f;
    CHECK(671, refused(q, kScale, kUnit));
    q = cloud; q.ps[kCloudC47][2] = -0.01f;
    CHECK(671, refused(q, kScale, kUnit));
    q = cloud; q.ps[kCloudC47][0] = 64.5f;
    CHECK(671, refused(q, kScale, kUnit));
    for (uint32_t reg : {uint32_t(kCloudC51), uint32_t(kCloudC69), uint32_t(kCloudC71)}) {
      q = cloud; q.ps[reg][0] = -0.01f;
      CHECK(671, refused(q, kScale, kUnit));
      q = cloud; q.ps[reg][0] = 64.5f;
      CHECK(671, refused(q, kScale, kUnit));
      q = cloud; q.ps[reg][0] = 64.0f;
      CHECK(671, !refused(q, kScale, kUnit));
    }
    q = cloud; q.ps[kCloudC70][0] = -4097.0f;
    CHECK(671, refused(q, kScale, kUnit));
    q = cloud; q.ps[kCloudC70][0] = -0.5f;  // (a normal's third component may be negative)
    CHECK(671, !refused(q, kScale, kUnit));
    q = cloud; q.ps[kCloudC78][1] = 64.5f;
    CHECK(671, refused(q, kScale, kUnit));
    q = cloud; q.ps[kCloudC77][2] = 2.0e7f;
    CHECK(671, refused(q, kScale, kUnit));
    q = cloud; q.ps[kCloudC79][0] = -2.0e7f;
    CHECK(671, refused(q, kScale, kUnit));
    q = cloud; q.alpha_ref = 1.5f;
    CHECK(671, refused(q, kScale, kUnit));
    q = cloud; q.alpha_ref = -0.1f;
    CHECK(671, refused(q, kScale, kUnit));
    const float zero[2] = {0.0f, 1.0f}, large[2] = {1.0f, 64.5f};
    CHECK(671, refused(cloud, kScale, zero) && refused(cloud, kScale, large));
  }

  // 672: the mirror against the reference (light_formulas.post_cloud; tools/xdk_sigmatch/post_pins.py).
  {
    struct CloudPin { float t[4], n[4], P[3], c20w, c47[3], c51x, c69x, c70x, c71x, c77[3], c78[3], c79[2]; double want[4]; };
    const CloudPin cloud_pins[] = {
          // cloud pins (light_formulas.post_cloud, c254 {0.0f, 0.0010000000474974513f, 1000.0f, 1.0f}, c255.x 32.0f): the texel, its neighbours' alphas (+x, -x, +y, -y), the world position, c20.w, c47.rgb, c51.x, c69.x, c70.x, c71.x, c77.xyz, c78.xyz, c79.xy, the value (rgb, alpha)
          {{0.800000011920929f, 0.699999988079071f, 0.6000000238418579f, 0.5f}, {0.6000000238418579f, 0.4000000059604645f, 0.550000011920929f, 0.44999998807907104f}, {120.0f, -340.0f, 500.0f}, 7.429120063781738f, {0.9921500086784363f, 0.8329200148582458f, 0.4013899862766266f}, 1.0f, 0.5f, 0.5f, 0.8999999761581421f, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, {-0.8236500024795532f, 0.32978999614715576f, 0.4613499939441681f}, {90.0970230102539f, 158.37025451660156f}, {5.69638533504734, 4.93554945088943, 4.117140265998651, 0.25}},
          {{0.8999999761581421f, 0.8500000238418579f, 0.800000011920929f, 0.30000001192092896f}, {0.20000000298023224f, 0.5f, 0.10000000149011612f, 0.6000000238418579f}, {-800.0f, 300.0f, 350.0f}, 7.429120063781738f, {0.9921500086784363f, 0.8329200148582458f, 0.4013899862766266f}, 0.6000000238418579f, 0.5f, 0.20000000298023224f, 1.0f, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, {-0.8236500024795532f, 0.32978999614715576f, 0.4613499939441681f}, {90.0970230102539f, 158.37025451660156f}, {4.68091024468202, 4.319428986756659, 3.806626302729454, 0.15000000596046448}},
          {{0.5f, 0.5f, 0.5f, 0.8999999761581421f}, {0.8999999761581421f, 0.8999999761581421f, 0.8999999761581421f, 0.8999999761581421f}, {90.0f, 158.0f, 400.0f}, 7.429120063781738f, {0.9921500086784363f, 0.8329200148582458f, 0.4013899862766266f}, 0.8999999761581421f, 0.5f, 0.5f, 1.0f, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, {-0.8236500024795532f, 0.32978999614715576f, 0.4613499939441681f}, {90.0970230102539f, 158.37025451660156f}, {3.4955237005152973, 3.4710618779175255, 3.4047677661643365, 0.44999998807907104}},
          {{0.800000011920929f, 0.699999988079071f, 0.6000000238418579f, 1.0f}, {1.0f, 0.0f, 0.30000001192092896f, 0.30000001192092896f}, {1290.0f, 958.0f, 500.0f}, 7.429120063781738f, {0.9921500086784363f, 0.8329200148582458f, 0.4013899862766266f}, 1.0f, 0.5f, 0.5f, 0.8999999761581421f, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, {-0.8236500024795532f, 0.32978999614715576f, 0.4613499939441681f}, {90.0970230102539f, 158.37025451660156f}, {9.002811372184697, 7.364355350157521, 5.120390522781585, 0.2790327773426008}},
          {{0.800000011920929f, 0.699999988079071f, 0.6000000238418579f, 1.0f}, {1.0f, 0.0f, 0.30000001192092896f, 0.30000001192092896f}, {3000.0f, -3000.0f, 500.0f}, 7.429120063781738f, {0.9921500086784363f, 0.8329200148582458f, 0.4013899862766266f}, 1.0f, 0.5f, 0.5f, 0.8999999761581421f, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, {-0.8236500024795532f, 0.32978999614715576f, 0.4613499939441681f}, {90.0970230102539f, 158.37025451660156f}, {0.0, 0.0, 0.0, 0.0}},
          {{0.800000011920929f, 0.699999988079071f, 0.6000000238418579f, 0.0019000000320374966f}, {0.6000000238418579f, 0.4000000059604645f, 0.550000011920929f, 0.44999998807907104f}, {120.0f, -340.0f, 500.0f}, 7.429120063781738f, {0.9921500086784363f, 0.8329200148582458f, 0.4013899862766266f}, 1.0f, 0.5f, 0.5f, 0.8999999761581421f, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, {-0.8236500024795532f, 0.32978999614715576f, 0.4613499939441681f}, {90.0970230102539f, 158.37025451660156f}, {0.0, 0.0, 0.0, 0.0009500000160187483}},
          {{0.800000011920929f, 0.699999988079071f, 0.6000000238418579f, 0.002099999925121665f}, {0.6000000238418579f, 0.4000000059604645f, 0.550000011920929f, 0.44999998807907104f}, {120.0f, -340.0f, 500.0f}, 7.429120063781738f, {0.9921500086784363f, 0.8329200148582458f, 0.4013899862766266f}, 1.0f, 0.5f, 0.5f, 0.8999999761581421f, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, {-0.8236500024795532f, 0.32978999614715576f, 0.4613499939441681f}, {90.0970230102539f, 158.37025451660156f}, {6.042345126622573, 5.189681608466732, 4.222112899844386, 0.0010499999625608325}},
          {{0.5f, 0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f, 0.5f}, {120.0f, -340.0f, 500.0f}, 7.429120063781738f, {0.9921500086784363f, 0.8329200148582458f, 0.4013899862766266f}, 1.0f, 0.5f, 0.0f, 0.8999999761581421f, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, {-0.8236500024795532f, 0.32978999614715576f, 0.4613499939441681f}, {90.0970230102539f, 158.37025451660156f}, {3.3431039401397697, 3.3431039401397697, 3.3431039401397697, 0.25}},
          {{0.800000011920929f, 0.699999988079071f, 0.6000000238418579f, 0.5f}, {0.6000000238418579f, 0.4000000059604645f, 0.550000011920929f, 0.44999998807907104f}, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, 7.429120063781738f, {0.9921500086784363f, 0.8329200148582458f, 0.4013899862766266f}, 1.0f, 0.5f, 0.5f, 0.8999999761581421f, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, {-0.8236500024795532f, 0.32978999614715576f, 0.4613499939441681f}, {-1189.0f, 245.0f}, {5.348966383929441, 4.680345436489868, 4.011724887579342, 0.25}},
          {{1.0f, 1.0f, 1.0f, 0.20000000298023224f}, {0.0f, 1.0f, 0.0f, 1.0f}, {120.0f, -340.0f, 500.0f}, 64.0f, {4.0f, 4.0f, 4.0f}, 4.0f, 1.0f, 0.5f, 2.0f, {500.0f, -900.0f, -4000.0f}, {0.30000001192092896f, 0.20000000298023224f, -0.8999999761581421f}, {90.0970230102539f, 158.37025451660156f}, {31.875, 31.875, 31.875, 0.20000000298023224}},
          {{kNaN, 0.699999988079071f, kInf, 0.5f}, {0.6000000238418579f, kNaN, -kInf, 0.44999998807907104f}, {120.0f, -340.0f, 500.0f}, 7.429120063781738f, {0.9921500086784363f, 0.8329200148582458f, 0.4013899862766266f}, 1.0f, 0.5f, 0.5f, 0.8999999761581421f, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, {-0.8236500024795532f, 0.32978999614715576f, 0.4613499939441681f}, {90.0970230102539f, 158.37025451660156f}, {0.0, 6.038147882575574, 0.0, 0.25}},
          {{0.800000011920929f, 0.699999988079071f, 0.6000000238418579f, kNaN}, {0.6000000238418579f, 0.4000000059604645f, 0.550000011920929f, 0.44999998807907104f}, {120.0f, -340.0f, 500.0f}, 7.429120063781738f, {0.9921500086784363f, 0.8329200148582458f, 0.4013899862766266f}, 1.0f, 0.5f, 0.5f, 0.8999999761581421f, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, {-0.8236500024795532f, 0.32978999614715576f, 0.4613499939441681f}, {90.0970230102539f, 158.37025451660156f}, {0.0, 0.0, 0.0, 0.0}},
          {{0.800000011920929f, 0.699999988079071f, 0.6000000238418579f, 0.5f}, {0.6000000238418579f, 0.4000000059604645f, 0.550000011920929f, 0.44999998807907104f}, {kNaN, -340.0f, 500.0f}, 7.429120063781738f, {0.9921500086784363f, 0.8329200148582458f, 0.4013899862766266f}, 1.0f, 0.5f, 0.5f, 0.8999999761581421f, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, {-0.8236500024795532f, 0.32978999614715576f, 0.4613499939441681f}, {90.0970230102539f, 158.37025451660156f}, {0.0, 0.0, 0.0, 0.0}},
          {{0.800000011920929f, 0.699999988079071f, 0.6000000238418579f, 0.5f}, {0.6000000238418579f, 0.4000000059604645f, 0.550000011920929f, 0.44999998807907104f}, {120.0f, -340.0f, 500.0f}, kInf, {0.9921500086784363f, 0.8329200148582458f, 0.4013899862766266f}, 1.0f, 0.5f, 0.5f, 0.8999999761581421f, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, {-0.8236500024795532f, 0.32978999614715576f, 0.4613499939441681f}, {90.0970230102539f, 158.37025451660156f}, {0.0, 0.0, 0.0, 0.0}},
          {{0.800000011920929f, 0.699999988079071f, 0.6000000238418579f, 0.5f}, {0.6000000238418579f, 0.4000000059604645f, 0.550000011920929f, 0.44999998807907104f}, {120.0f, -340.0f, 500.0f}, 7.429120063781738f, {0.9921500086784363f, 0.8329200148582458f, 0.4013899862766266f}, 1.0f, 0.5f, 0.5f, 0.8999999761581421f, {-1189.11767578125f, 244.89036560058594f, 1582.174072265625f}, {kNaN, 0.30000001192092896f, 0.4000000059604645f}, {90.0970230102539f, 158.37025451660156f}, {0.0, 0.0, 0.0, 0.0}},
    };
    double worst = 0.0;
    for (const CloudPin& p : cloud_pins) {
      double got[4];
      CloudMirror(p.t, p.n, p.P, p.c20w, p.c47, p.c51x, p.c69x, p.c70x, p.c71x, p.c77, p.c78, p.c79, got);
      for (int i = 0; i < 4; ++i) {
        const double tolerance = 1e-12 * std::max(1.0, std::fabs(p.want[i]));
        CHECK(672, std::isfinite(got[i]) && got[i] >= 0.0 && got[i] <= (i == 3 ? 1.0 : double(kPostSceneMax)) && std::fabs(got[i] - p.want[i]) <= tolerance);
        worst = std::max(worst, std::fabs(got[i] - p.want[i]) / std::max(1.0, std::fabs(p.want[i])));
      }
    }
    CHECK(672, std::size(cloud_pins) == 15);
    std::cout << "clouds: the mirror against the reference, 15 points: worst relative difference " << worst << "\n";
    CHECK(672, worst < 1e-12);
    CHECK(672, kCloudThin == bits(0x3A83126Fu) && kCloudFadeRate == kCloudThin && kCloudFadeStart == 1000.0f && kCloudPower == 32.0f);
    // What the formula does, at points one can reason about. Straight under the eye, no slope: the normal is
    // straight up; the alpha is the texel's times c69.x.
    const float t[4] = {0.5f, 0.5f, 0.5f, 0.8f}, flat[4] = {0.8f, 0.8f, 0.8f, 0.8f};
    const float P[3] = {90.0f, 158.0f, 400.0f}, tint[3] = {1.0f, 1.0f, 1.0f}, eye[2] = {90.0f, 158.0f};
    const float above[3] = {90.0f, 158.0f, 2000.0f}, below[3] = {90.0f, 158.0f, -2000.0f}, view[3] = {0.0f, 0.0f, -1.0f};
    double lit[4], dark[4];
    CloudMirror(t, flat, P, 1.0f, tint, 1.0f, 0.5f, 0.5f, 0.0f, below, view, eye, lit);
    // Dn = (0, 0, 1) (from a point below), N = (0, 0, 1): b2 = 1, b1 = 0; a1 = sat(-N . view) = 1: l = 1 + 0^32 = 1.
    CHECK(672, std::fabs(lit[3] - 0.4) < 1e-7 && std::fabs(lit[0] - 0.5) < 1e-12 && lit[0] == lit[1] && lit[1] == lit[2]);
    CloudMirror(t, flat, P, 1.0f, tint, 1.0f, 0.5f, 0.5f, 0.0f, above, view, eye, dark);
    // from above: b1 = 1, b2 = 0: l = 1 - t.a = 0.2 (single precision's).
    CHECK(672, std::fabs(dark[0] - 0.5 * (1.0 - double(0.8f))) < 1e-12 && dark[3] == lit[3]);
    // c71.x is added whatever the light; c51.x and c20.w scale all of it.
    double more[4];
    CloudMirror(t, flat, P, 2.0f, tint, 3.0f, 0.5f, 0.5f, 0.25f, above, view, eye, more);
    CHECK(672, std::fabs(more[0] - (0.5 * (1.0 - double(0.8f)) + 0.125) * 6.0) < 1e-12);
    // A slope tilts the normal: the same pixel is no longer lit the same.
    const float slope[4] = {1.0f, 0.0f, 0.8f, 0.8f};
    double tilted[4];
    CloudMirror(t, slope, P, 1.0f, tint, 1.0f, 0.5f, 0.5f, 0.0f, below, view, eye, tilted);
    CHECK(672, tilted[0] < lit[0] && tilted[3] == lit[3]);
    // The neighbours' order: +x, -x, +y, -y (exchanging a pair turns the slope round: with the sun to one side
    // the two differ).
    const float side[3] = {-3000.0f, 158.0f, 400.0f}, sx[4] = {1.0f, 0.0f, 0.5f, 0.5f}, sx_back[4] = {0.0f, 1.0f, 0.5f, 0.5f};
    const float sy[4] = {0.5f, 0.5f, 1.0f, 0.0f};
    double a[4], b[4], c[4];
    CloudMirror(t, sx, P, 1.0f, tint, 1.0f, 0.5f, 0.5f, 0.0f, side, view, eye, a);
    CloudMirror(t, sx_back, P, 1.0f, tint, 1.0f, 0.5f, 0.5f, 0.0f, side, view, eye, b);
    CloudMirror(t, sy, P, 1.0f, tint, 1.0f, 0.5f, 0.5f, 0.0f, side, view, eye, c);
    CHECK(672, std::fabs(a[0] - b[0]) > 0.05 && std::fabs(a[0] - c[0]) > 0.05 && std::fabs(b[0] - c[0]) > 0.05);
    // The alpha test as the step applies it (the reference 5 / 255): a pixel at the reference is dropped.
    CHECK(672, QuadAlphaKept(true, float(lit[3]), cloud.alpha_ref) && !QuadAlphaKept(true, cloud.alpha_ref, cloud.alpha_ref) && !QuadAlphaKept(true, kNaN, cloud.alpha_ref));
  }

  // 673: nothing that is no number: every output a number in its range, whatever is no number among the inputs.
  {
    const float values[] = {kNaN, kInf, -kInf, 0.0f, -1.0f, 0.5f, 3.0e38f, -3.0e38f, 1.0e-30f, 2000.0f};
    uint32_t seed = 16;
    auto next = [&] {
      seed = seed * 1664525u + 1013904223u;
      return values[(seed >> 16) % std::size(values)];
    };
    for (int trial = 0; trial < 10000; ++trial) {
      float t[4], n[4], P[3], c47[3], c77[3], c78[3], c79[2];
      for (float& v : t) v = next();
      for (float& v : n) v = next();
      for (float& v : P) v = next();
      for (float& v : c47) v = next();
      for (float& v : c77) v = next();
      for (float& v : c78) v = next();
      for (float& v : c79) v = next();
      const float c20w = next(), c51x = next(), c69x = next(), c70x = next(), c71x = next();
      double out[4];
      CloudMirror(t, n, P, c20w, c47, c51x, c69x, c70x, c71x, c77, c78, c79, out);
      for (int i = 0; i < 4; ++i) CHECK(673, std::isfinite(out[i]) && out[i] >= 0.0 && out[i] <= (i == 3 ? 1.0 : double(kPostSceneMax)));
    }
  }

  // 674: the frame's plan: the cloud records of the set, in order; one that cannot be drawn: none.
  {
    QuadRecord moon{};
    moon.ps_hash = capture::kPostQuadTable[size_t(PostQuadKind::kMoon)].ps;
    moon.kind = PostQuadKind::kMoon;
    QuadRecord second = cloud, third = cloud;
    second.ps[kCloudC31][0] = 6.0f;
    third.ps[kCloudC31][0] = 7.0f;
    const QuadRecord set[] = {moon, cloud, second, QuadRecord{}, third, moon};
    const ClayCloudPlan plan = PlanClayClouds(set, std::size(set), kScale);
    CHECK(674, plan.drawable && plan.count == 3 && plan.records[0] == 1 && plan.records[1] == 2 && plan.records[2] == 4);
    // the set's other kinds (the glare, the layer's quads, the cloud OCCLUDERS) are not the clouds' to judge
    QuadRecord glare{}, occluder{};
    glare.ps_hash = capture::kPostQuadTable[size_t(PostQuadKind::kGlare)].ps;
    glare.kind = PostQuadKind::kGlare;
    occluder.ps_hash = capture::kPostQuadTable[size_t(PostQuadKind::kCloudOccluder)].ps;
    occluder.kind = PostQuadKind::kCloudOccluder;
    occluder.layer = 1;
    const QuadRecord mixed[] = {glare, cloud, occluder, second, occluder, third, glare};
    const ClayCloudPlan among = PlanClayClouds(mixed, std::size(mixed), kScale);
    CHECK(674, among.drawable && among.count == 3 && among.records[0] == 1 && among.records[1] == 3 && among.records[2] == 5);
    // no cloud in the set: drawable, none
    const QuadRecord bare[] = {moon, moon};
    const ClayCloudPlan none = PlanClayClouds(bare, 2, kScale);
    CHECK(674, none.drawable && none.count == 0);
    CHECK(674, PlanClayClouds(nullptr, 0, kScale).drawable && PlanClayClouds(nullptr, 0, kScale).count == 0);
    // one cloud that cannot be drawn takes all of them
    QuadRecord broken[] = {moon, cloud, second, third};
    broken[2].ps[kCloudC69][0] = kNaN;
    const ClayCloudPlan refused = PlanClayClouds(broken, 4, kScale);
    CHECK(674, !refused.drawable && refused.count == 0);
    // another frame's scale: none
    CHECK(674, !PlanClayClouds(set, std::size(set), kScale * 1.5f).drawable);
    // too many records, or none where some are named
    CHECK(674, !PlanClayClouds(set, capture::kMaxQuadRecords + 1, kScale).drawable && !PlanClayClouds(nullptr, 3, kScale).drawable);
    // a full set of clouds fits the plan
    QuadRecord full[capture::kMaxQuadRecords];
    for (QuadRecord& q : full) q = cloud;
    const ClayCloudPlan all = PlanClayClouds(full, capture::kMaxQuadRecords, kScale);
    CHECK(674, all.drawable && all.count == capture::kMaxQuadRecords && all.records[capture::kMaxQuadRecords - 1] == capture::kMaxQuadRecords - 1);
  }

  // 675: the verdict over every combination (the first thing missing decides), the frame counts, the line.
  {
    for (int b = 0; b < 64; ++b) {
      ClayCloudInputs in;
      in.furniture = b & 1;
      in.drawable = b & 2;
      in.any = b & 4;
      in.objects = b & 8;
      in.textures = b & 16;
      in.feed = b & 32;
      // (the feed's word comes last: a frame that would not have drawn its clouds anyway keeps its own reason)
      const ClayClouds want = !in.furniture  ? ClayClouds::kOff
                              : !in.drawable ? ClayClouds::kState
                              : !in.any      ? ClayClouds::kNone
                              : !in.objects  ? ClayClouds::kObjects
                              : !in.textures ? ClayClouds::kTexture
                              : in.feed      ? ClayClouds::kFeed
                                             : ClayClouds::kDrawn;
      CHECK(675, DecideClayClouds(in) == want);
    }
    // (review of plan Tasks 16 and 17, Minor 8: on the oracle feed with a sun part the clouds' slots are the
    // file's quads' and no cloud is drawn; the line must not read "drawn 3" then, and the frame is not counted drawn)
    {
      ClayCloudFrames fed;
      CountClayCloudFrame(ClayClouds::kFeed, fed);
      CHECK(675, fed.frames == 1 && fed.by[size_t(ClayClouds::kFeed)] == 1 && fed.by[size_t(ClayClouds::kDrawn)] == 0);
      ClayStats fst;
      fst.sun_switch = true;
      fst.clouds = ClayClouds::kFeed;
      fst.cloud_records = 3;
      fst.cloud_frames = fed;
      CHECK(675, FormatCloudText(fst) == "Clouds: not drawn (the oracle feed's sun part) | frames since start 1: drawn 0, no cloud record 0, "
                                         "a record's state or values 0, post-objects 0, quad-texture 0, the oracle feed's sun part 1");
    }
    ClayCloudFrames n;
    for (ClayClouds c : {ClayClouds::kOff, ClayClouds::kNone, ClayClouds::kState, ClayClouds::kObjects, ClayClouds::kTexture, ClayClouds::kDrawn,
                         ClayClouds::kCount}) {
      CountClayCloudFrame(c, n);
    }
    CountClayCloudFrame(ClayClouds::kDrawn, n);
    CHECK(675, n.frames == 6 && n.by[size_t(ClayClouds::kOff)] == 0 && n.by[size_t(ClayClouds::kDrawn)] == 2 && n.by[size_t(ClayClouds::kNone)] == 1 &&
                   n.by[size_t(ClayClouds::kState)] == 1 && n.by[size_t(ClayClouds::kObjects)] == 1 && n.by[size_t(ClayClouds::kTexture)] == 1);
    ClayStats st;
    CHECK(675, FormatCloudText(st) == "Clouds: off");
    st.sun_switch = true;
    st.clouds = ClayClouds::kDrawn;
    st.cloud_records = 3;
    st.cloud_frames.frames = 900;
    st.cloud_frames.by[size_t(ClayClouds::kDrawn)] = 890;
    st.cloud_frames.by[size_t(ClayClouds::kTexture)] = 3;
    st.cloud_frames.by[size_t(ClayClouds::kNone)] = 7;
    CHECK(675, FormatCloudText(st) == "Clouds: drawn 3 | frames since start 900: drawn 890, no cloud record 7, a record's state or values 0, "
                                      "post-objects 0, quad-texture 3");
    const std::pair<ClayClouds, const char*> words[] = {{ClayClouds::kOff, "Clouds: not drawn (no furniture) | frames"},
                                                        {ClayClouds::kNone, "Clouds: not drawn (no cloud record) | frames"},
                                                        {ClayClouds::kState, "Clouds: not drawn (a record's state or values) | frames"},
                                                        {ClayClouds::kObjects, "Clouds: not drawn (post-objects) | frames"},
                                                        {ClayClouds::kTexture, "Clouds: not drawn (quad-texture) | frames"}};
    for (const auto& [c, word] : words) {
      st.clouds = c;
      CHECK(675, FormatCloudText(st).rfind(word, 0) == 0);
    }
    st.clouds = ClayClouds::kDrawn;
    st.cloud_records = ~0u;
    st.cloud_frames.frames = ~0ull;
    for (uint64_t& v : st.cloud_frames.by) v = ~0ull;
    const std::string longest = FormatCloudText(st);
    // (with a fed frame counted the feed's count is the line's last word)
    CHECK(675, longest.size() > 200 && longest.find("quad-texture 18446744073709551615, ") != std::string::npos &&
                   longest.rfind("the oracle feed's sun part 18446744073709551615") == longest.size() - 47);
  }
}
// --- end of the clouds' cases ---
// 680-686: the wind words in the geometry cache (foliage plan Task 9): the box grown by a record's reach, the
// words' cache keys, the Geometry line.
static void WindWordTests() {
  using capture::WindFamily;
  using capture::WindInputs;
  // 680: GrowBounds. The box wider by exactly the reach in x, y, z; w as it was; an invalid box stays invalid;
  // a reach that is no number, not finite or below zero gives an invalid box (whoever asks then keeps the record).
  {
    PositionBounds box;
    const float lo[4] = {1.0f, 2.0f, 3.0f, 1.0f}, hi[4] = {4.0f, 5.0f, 6.0f, 1.0f};
    std::memcpy(box.lo, lo, sizeof(lo));
    std::memcpy(box.hi, hi, sizeof(hi));
    box.valid = true;
    const PositionBounds g = GrowBounds(box, 0.5f);
    CHECK(680, g.valid && g.lo[0] == 0.5f && g.lo[1] == 1.5f && g.lo[2] == 2.5f && g.lo[3] == 1.0f && g.hi[0] == 4.5f &&
                   g.hi[1] == 5.5f && g.hi[2] == 6.5f && g.hi[3] == 1.0f);
    const PositionBounds same = GrowBounds(box, 0.0f);
    CHECK(680, same.valid && std::memcmp(same.lo, box.lo, sizeof(box.lo)) == 0 && std::memcmp(same.hi, box.hi, sizeof(box.hi)) == 0);
    CHECK(680, !GrowBounds(PositionBounds{}, 0.5f).valid);
    for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity(), -0.25f}) {
      CHECK(680, !GrowBounds(box, bad).valid);
    }
    // (a grown box is never the narrower one: a record at the reach's edge is kept by the pre-pass's rule)
    const float eye[3] = {0.0f, 0.0f, 0.0f}, split[4] = {20.0f, 35.0f, 2.0f, 0.5f};
    float vp[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {1, 0, 0, 0}};   // w = x: the distance along x
    PositionBounds far;
    const float flo[4] = {81.0f, -1.0f, -1.0f, 1.0f}, fhi[4] = {90.0f, 1.0f, 1.0f, 1.0f};
    std::memcpy(far.lo, flo, sizeof(flo));
    std::memcpy(far.hi, fhi, sizeof(fhi));
    far.valid = true;
    const uint8_t less = uint8_t(capture::Compare::kLess);
    const bool beyond = PrepassBeyondReach(vp, far, vp, eye, split, less);
    const bool grown = PrepassBeyondReach(vp, GrowBounds(far, 40.0f), vp, eye, split, less);
    CHECK(680, beyond && !grown);
  }
  // 681-685: WindKey.
  {
    capture::DrawRecord r = BaseRecord();
    r.pos.format = capture::PosFormat::kHalf4;
    r.pos.stride_bytes = 36;
    r.pos.swap16 = true;
    r.vb = {0x200000, 3600};
    auto element = [&](uint16_t offset, uint8_t format, uint8_t source, uint8_t count) {
      capture::LightElement e;
      e.vb = {r.vb.phys_addr, r.vb.size};
      e.offset_bytes = offset;
      e.stride_bytes = 36;
      e.format = format;
      e.source = source;
      e.source_count = count;
      e.flags = 10;
      return e;
    };
    WindInputs trunk;
    trunk.family = WindFamily::kTrunk;
    trunk.spec = 3;
    trunk.element[capture::kWindWeights] = element(28, 6, 0xE4, 4);
    WindInputs leaf = trunk;
    leaf.family = WindFamily::kLeaf;
    leaf.element[capture::kWindPivot] = element(20, 32, 0x31, 3);
    leaf.element[capture::kWindFlutter] = element(32, 6, 0x24, 3);
    const GeoKey tk = WindKey(r, trunk), lk = WindKey(r, leaf);
    CHECK(681, tk.kind == 15 && lk.kind == 15 && tk.addr == r.vb.phys_addr && tk.size == r.vb.size && tk.stride == 36 && !(tk == lk));
    CHECK(681, !(tk == PositionKey(r, nullptr)) && !(tk == MeshPositionKey(r)));
    // Nothing of the draw's registers, of the row, or of the record's transform and material: the words are the mesh's.
    {
      WindInputs b = leaf;
      b.reg[0][0] = 99.0f;
      b.reg[2][3] = 198.0f;
      b.spec = 9;
      capture::DrawRecord q = r;
      q.rows[3] = 7.0f;
      q.vs_hash = 0x1234;
      q.material.ps_hash = 0x99;
      q.wind_index = 4;
      CHECK(682, WindKey(q, b) == lk);
    }
    // Every field that changes what a reader takes from an element changes the key, for each element the family reads.
    for (uint32_t e = 0; e < capture::kWindElements; ++e) {
      for (int field = 0; field < 7; ++field) {
        WindInputs b = leaf;
        capture::LightElement& el = b.element[e];
        switch (field) {
          case 0: el.vb.phys_addr += 0x100; break;
          case 1: el.vb.size += 36; break;
          case 2: el.stride_bytes += 4; break;
          case 3: el.offset_bytes += 4; break;
          case 4: el.format = uint8_t(el.format == 6 ? 32 : 6); break;
          case 5: el.source ^= 0x5; break;
          default: el.source_count = uint8_t(el.source_count == 3 ? 4 : 3); break;
        }
        CHECK(683, !(WindKey(r, b) == lk));
      }
    }
    // A leaf's extent is measured against its positions: their stream and layout are in a leaf's key.
    for (int field = 0; field < 6; ++field) {
      capture::DrawRecord q = r;
      switch (field) {
        case 0: q.vb.phys_addr += 0x100; break;
        case 1: q.vb.size += 36; break;
        case 2: q.pos.offset_bytes += 4; break;
        case 3: q.pos.swizzle ^= 0x9; break;
        case 4: q.pos.swap16 = !q.pos.swap16; break;
        default: q.pos.format = capture::PosFormat::kFloat3; break;
      }
      CHECK(684, !(WindKey(q, leaf) == lk));
    }
    {
      // (a trunk reads its weights alone: neither a pivot it does not have nor the positions)
      WindInputs b = trunk;
      b.element[capture::kWindPivot] = element(20, 32, 0x31, 3);
      capture::DrawRecord q = r;
      q.pos.swizzle ^= 0x9;
      CHECK(684, WindKey(q, b) == tk);
      b = trunk;
      b.element[capture::kWindWeights].offset_bytes = 24;
      CHECK(684, !(WindKey(r, b) == tk));
    }
    // A plant: the mesh's position stream with its layout and the instance set as the flat normals have it
    // (kind 16); not the set's offset, which moves positions only, and nothing of the registers.
    capture::DrawRecord i = BaseRecord();
    i.instances.active = true;
    i.instances.rows_addr = 0x5000;
    i.instances.rows_size = 280;
    i.instances.inv_count = 0.25f;
    i.instances.count = 4.0f;
    i.instances.bias = 0.5f;
    i.instances.flat_count = 40;
    WindInputs plant;
    plant.family = WindFamily::kPlant;
    const GeoKey pk = WindKey(i, plant);
    CHECK(685, pk.kind == 16 && pk.addr == i.vb.phys_addr && pk.size == i.vb.size && pk.stride == i.pos.stride_bytes &&
                   !(pk == InstanceBendKey(i)) && !(pk == PositionKey(i, nullptr)) && !(pk == MeshPositionKey(i)));
    {
      capture::DrawRecord i2 = i;
      i2.instances.offset[0] = 3.0f;
      WindInputs p2 = plant;
      p2.reg[0][0] = 60.0f;
      CHECK(685, WindKey(i2, p2) == pk);
    }
    for (int field = 0; field < 11; ++field) {
      capture::DrawRecord i2 = i;
      switch (field) {
        case 0: i2.instances.rows_addr = 0x6000; break;
        case 1: i2.instances.rows_size = 308; break;
        case 2: i2.instances.rows[1].offset_bytes = 8; break;
        case 3: i2.instances.inv_count = 0.2f; break;
        case 4: i2.instances.count = 5.0f; break;
        case 5: i2.instances.first = 2.0f; break;
        case 6: i2.instances.bias = 0.25f; break;
        case 7: i2.instances.flat_count = 44; break;
        case 8: i2.vb.phys_addr += 0x100; break;
        case 9: i2.pos.offset_bytes += 4; break;
        default: i2.pos.swizzle ^= 0x9; break;
      }
      CHECK(685, !(WindKey(i2, plant) == pk));
    }
    // No family: no key that could meet another's.
    WindInputs none;
    CHECK(685, WindKey(r, none).kind == 15 && !(WindKey(r, none) == tk) && !(WindKey(r, none) == lk));
  }
  // 686: the Geometry line. Without wind words it is the line of before, to the character; with them it names
  // the records served, the buffers' bytes and what was refused.
  {
    FrameScene s;
    ClayStats st;
    st.uploads = 12;
    st.hits = 1818;
    st.resident_bytes = 47710208;
    const std::string before = FormatStatusText(s, st, nullptr);
    CHECK(686, before.find("Geometry: 12 uploads, 1818 hits, 45.5 MB resident, instanced 0, skinned 0 | hash 0.00 ms, "
                           "decode 0.00 ms, record 0.00 ms\n") != std::string::npos &&
                   before.find("wind") == std::string::npos);
    st.wind_words = 228;
    st.wind_words_bytes = 3355443;
    const std::string with = FormatStatusText(s, st, nullptr);
    CHECK(686, with.find("record 0.00 ms | wind words: 228 records, 3.2 MB\n") != std::string::npos);
    st.wind_build[size_t(WindBuild::kStream)] = 2;
    st.wind_build[size_t(WindBuild::kValue)] = 1;
    const std::string refused = FormatStatusText(s, st, nullptr);
    CHECK(686, refused.find("record 0.00 ms | wind words: 228 records, 3.2 MB, refused {stream: 2, value: 1}\n") != std::string::npos);
    ClayStats only;
    only.wind_build[size_t(WindBuild::kElement)] = 4;
    CHECK(686, FormatStatusText(s, only, nullptr).find(" | wind words: 0 records, 0.0 MB, refused {element: 4}\n") != std::string::npos);
  }
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
    CHECK(51, ClayRecordsWithoutConstants(921, true) == 0 && ClayRecordsWithoutConstants(921, false) == 921);
    CHECK(51, ClayRecordsWithoutConstants(0, false) == 0 && ClayRecordsWithoutConstants(0, true) == 0);
    CHECK(51, ClayRecordsWithoutConstants(1, false) == 1);
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
    // With the switch off the pass asks the geometry cache for no light input (the final fix wave:
    // normals, colours, probe and bend buffers were built with the light off too, 20 MB at the
    // bridge), and the line says so instead of counting nothing.
    CHECK(531, !ClayLightInputsBuilt(ClayLight::kOff) && ClayLightInputsBuilt(ClayLight::kOn));
    CHECK(531, FormatLightText(st) == "Light inputs: not built (light off)");
    st.lit = 873;  // (whatever the counters hold: with the switch off nothing was asked for)
    CHECK(531, FormatLightText(st) == "Light inputs: not built (light off)");
    st = ClayStats{};
    st.light_switch = true;
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
    refused.light_switch = true;
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


    // The light switch (fable2_native_light): off or on (and their other words: case 575); anything else is off and not recognised.
    bool ok = false;
    CHECK(407, ParseClayLight("off", &ok) == ClayLight::kOff && ok);
    CHECK(408, ParseClayLight(" On ", &ok) == ClayLight::kOn && ok);
    CHECK(409, ParseClayLight("yes", &ok) == ClayLight::kOff && !ok);  // (the accepted words: case 575)
    CHECK(410, ParseClayLight("", &ok) == ClayLight::kOff && !ok && ParseClayLight("on", nullptr) == ClayLight::kOn);
    // The exposure of the output mapping: 1 with light off (the materials picture); with light on the
    // calibrated constant (plan Task 9: fitted on the dirt patch at the bridge against the emulated frame).
    CHECK(411, ClayExposure(ClayLight::kOff) == 1.0f && ClayExposure(ClayLight::kOn) == kLitExposure &&
                   kLitExposure == 2.22f);
    // The exposure reaches lit output only (the final fix wave: the mapping multiplied every pixel by it,
    // so the unlit draws and the clear of a lit frame came out sqrt(E) too bright: the clear at 30, 30,
    // 37). A lit draw writes ClayLit(v * E); the mapping's own word is 1 in every frame. So in a lit
    // frame the clear shows its levels (20, 20, 25), an unlit draw's level d comes back as d, and a lit
    // value v is shown as sqrt(min(v * E, 1)): the dirt patch's linear value under exposure 1 (0.14926,
    // run 341) at the emulated shots' mean (0.33206) within 1%.
    {
      const float E = ClayExposure(ClayLight::kOn);
      CHECK(418, kClayMapExposure == 1.0f);
      for (int i = 0; i < 3; ++i) CHECK(418, Level8(OutputMap(kClayClearLinear[i], kClayMapExposure)) == kClayClearLevels[i]);
      for (int d = 0; d < 256; ++d) CHECK(418, Level8(OutputMap(ClayLinear(float(d) / 255.0f), kClayMapExposure)) == d);
      for (float v : {0.0f, 0.001f, 0.14926f, 0.3f, 0.45f, 0.4505f, 0.5f, 2.5f, 100.0f}) {
        const float want = std::sqrt(std::min(v * E, 1.0f));
        CHECK(418, std::fabs(OutputMap(ClayLit(v * E), kClayMapExposure) - want) < 1e-6f);
      }
      CHECK(418, std::fabs(OutputMap(ClayLit(0.14926f * E), kClayMapExposure) - std::sqrt(0.33206f)) < 0.01f * std::sqrt(0.33206f));
      // (a lit value is brighter than the same value unlit by sqrt(E) on the display, and only it)
      CHECK(418, OutputMap(ClayLit(0.25f * E), kClayMapExposure) > OutputMap(0.25f, kClayMapExposure) * 1.48f &&
                     OutputMap(ClayLit(0.25f * E), kClayMapExposure) < OutputMap(0.25f, kClayMapExposure) * 1.50f);
    }

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
    static_assert(kClayFrameLightBytes == 112);  // (96 before the eye, plan Task 8c; the fog's tail follows: case 584)
    ClayFrameConstants frame = MakeClayFrameConstants(block, ClayLight::kOn);
    CHECK(440, frame.light_on == 1 && frame.tint_pow == 1.3f && frame.floor[0] == 0.0588f && frame.floor[1] == 0.0902f &&
                   frame.floor[2] == 0.0f);
    for (int i = 0; i < 3; ++i) {
      CHECK(441, frame.sun_dir[i] == block_sun[i] && frame.sun_col[i] == 0.1f + float(i) && frame.tint[i] == 1.1f + float(i) &&
                     frame.add[i] == 0.01f + float(i) && frame.axis[i] == 0.2f + float(i));
    }
    // The output scale (16) is not in the block the shaders get (a post frame's apart: case 608). The
    // exposure is: the lit pixel paths multiply their value by it (cbuffer Frame's f_exposure), and a lit
    // frame carries the calibrated constant. The unlit word (f_post) is 1: a frame without post.
    CHECK(442, frame.post == 1.0f && offsetof(ClayFrameConstants, post) == 60 && frame.mask_du == 0.0f && frame.mask_dv == 0.0f && frame.exposure == kLitExposure &&
                   frame.exposure == ClayExposure(ClayLight::kOn) && offsetof(ClayFrameConstants, exposure) == 44);
    // The switch off, or no block: nothing lit, everything zero, the exposure too (no draw reads it:
    // the frame is the materials picture, written and mapped as it is), but for the unlit word, which
    // every unlit colour is multiplied by: 1 in such a frame as in every frame without post.
    const ClayFrameConstants none{};
    {
      ClayFrameConstants zeroed = none;
      zeroed.post = 0.0f;
      const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&zeroed);
      bool all_zero = true;
      for (size_t i = 0; i < sizeof(zeroed); ++i) all_zero = all_zero && bytes[i] == 0;
      CHECK(443, all_zero && none.post == 1.0f);
    }
    frame = MakeClayFrameConstants(block, ClayLight::kOff);
    CHECK(443, std::memcmp(&frame, &none, sizeof(frame)) == 0 && frame.exposure == 0.0f && frame.post == 1.0f);
    capture::LightBlock invalid = block;
    invalid.valid = false;
    frame = MakeClayFrameConstants(invalid, ClayLight::kOn);
    CHECK(444, std::memcmp(&frame, &none, sizeof(frame)) == 0 && frame.exposure == 0.0f && frame.post == 1.0f);
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
    static_assert(sizeof(ClayObjectConstants) == 288 && offsetof(ClayObjectConstants, probe) == 80);
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
    // plants with and without the ambient factor, each with its two- and four-light form; since plan
    // Task 8c the tree leaves (probe colours; their lights are the vertex shader's, so the entry has
    // no -multi form); nothing else, and no family on another vertex kind than its own.
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
    CHECK(459, drawn(LightFamily::kTreeLeaf, LightVertexKind::kProbe, false) && !drawn(LightFamily::kTreeLeaf, LightVertexKind::kStream, false) &&
                   !drawn(LightFamily::kTreeLeaf, LightVertexKind::kInstance, false) && !drawn(LightFamily::kTreeLeaf, LightVertexKind::kTerrain, false) &&
                   !drawn(LightFamily::kTreeLeaf, LightVertexKind::kNone, false) && !drawn(LightFamily::kCount, LightVertexKind::kProbe, false));
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
                   u32("t_base_sampler") == 3 && f32("t_c11x", 0) == 0.75f && f32("t_facing_scale", 0) == 8.0f && u32("t_base_gamma") == 0);
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

    // 618-620 (post sub-project, Task 8b): the base map's gamma sign. Every terrain row fetches tf3 with the
    // sign fields 3, 3, 3, 0 (fetch dword 0, bits 2 to 9): the console's fetch gives r, g, b through its
    // piecewise curve. The mask is per component of the RESULT (the fields go through the constant's
    // swizzle, as the SDK's SwizzleSigns takes them); a signed or biased colour component is refused.
    {
      const uint32_t identity = 0x688u << 1;  // result x, y, z, w from stored x, y, z, w
      auto fetch = [&](uint32_t signs, uint32_t swizzle) {
        std::array<uint32_t, 6> fc{2u | (signs << 2), 0u, 0u, swizzle, 0u, 0u};
        return fc;
      };
      auto gamma = [&](uint32_t signs, uint32_t swizzle) { return TerrainBaseGamma(fetch(signs, swizzle).data()); };
      CHECK(618, gamma(0x00, identity) == 0 && gamma(0x3F, identity) == 7 && gamma(0x0C, identity) == 2 &&
                     gamma(0x03, identity) == 1 && gamma(0x30, identity) == 4);
      // (alpha's field is not a colour's: gamma, signed or biased there changes nothing)
      CHECK(618, gamma(0xC0, identity) == 0 && gamma(0x40, identity) == 0 && gamma(0xBF, identity) == 7);
      // (signed, biased: refused, in any colour component)
      CHECK(618, gamma(0x01, identity) == kTerrainBaseGammaRefused && gamma(0x08, identity) == kTerrainBaseGammaRefused &&
                     gamma(0x2F, identity) == kTerrainBaseGammaRefused && kTerrainBaseGammaRefused > 7);
      // (the swizzle: red from stored w takes w's field; a constant 0 or 1 has none)
      const uint32_t red_from_w = ((0x688u & ~7u) | 3u) << 1, red_one = ((0x688u & ~7u) | 5u) << 1;
      CHECK(618, gamma(0xC0, red_from_w) == 1 && gamma(0x03, red_from_w) == 0 && gamma(0x3F, red_one) == 6 &&
                     gamma(0x40, red_from_w) == kTerrainBaseGammaRefused);
      // The record's light constants carry the mask; a base map the pass cannot read as the game does: no light.
      capture::TerrainMaterialInputs signed_base = tm, gamma_base = tm;
      const std::array<uint32_t, 6> g = fetch(0x3F, identity), s = fetch(0x01, identity);
      std::copy(g.begin(), g.end(), std::begin(gamma_base.base_fetch));
      std::copy(s.begin(), s.end(), std::begin(signed_base.base_fetch));
      tc = MakeClayTerrainConstants(tm, capture::MaterialKind::kTerrainLayer, colour_fix, 1, 3);
      CHECK(619, u32("t_base_gamma") == 0 && SetClayTerrainLight(tc, gamma_base, tl, base_fix, 3) && u32("t_base_gamma") == 7 &&
                     f32("t_c11x", 0) == 0.75f);
      CHECK(619, SetClayTerrainLight(tc, tm, tl, base_fix, 3) && u32("t_base_gamma") == 0);
      CHECK(619, refused(signed_base, tl) && !refused(gamma_base, tl));
      // The curve (xenos.cpp, PWLGammaToLinear; light_formulas.pwl_gamma_to_linear; tests/test_terrain_replay.py
      // has the same points, worked by hand): with v = 255 g, 1023 * linear is v below 64, 2 v - 64 below 96,
      // 4 v - 256 below 192, else 8 v - 1024, each plus the whole part of itself times its slope / 1024.
      // At the knees and just above (review-task-8b, Minor 3): level 192 is on the last piece (512 + 4 = 516; on the
      // piece below it would be 514), level 96.5 on the third (130; with the knee at 97 it would be 129); levels 64
      // and 96 give the same number from either side.
      const float pins[][2] = {{0, 0}, {30, 30}, {70, 76}, {100, 144}, {150, 345}, {200, 580}, {255, 1023},
                               {192, 516}, {96.5f, 130}, {64, 64}, {96, 128}, {64.5f, 65}};
      bool curve = true;
      for (const auto& p : pins) curve = curve && std::fabs(PwlGammaToLinear(p[0] / 255.0f) - p[1] / 1023.0f) < 2e-6f;
      CHECK(620, curve && PwlGammaToLinear(1.0f) == 1.0f && PwlGammaToLinear(0.0f) == 0.0f);
      CHECK(620, PwlGammaToLinear(-0.5f) == 0.0f && PwlGammaToLinear(2.0f) == 1.0f && PwlGammaToLinear(nan) == 0.0f &&
                     PwlGammaToLinear(inf) == 1.0f && PwlGammaToLinear(-inf) == 0.0f);
      bool rising = true, below = true;
      for (int i = 1; i <= 1020; ++i) {
        const float a = PwlGammaToLinear(float(i - 1) / 1020.0f), b = PwlGammaToLinear(float(i) / 1020.0f);
        rising = rising && b >= a;
        below = below && (i == 1020 || b < float(i) / 1020.0f);
      }
      CHECK(620, rising && below);
    }

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
                   kLightModeInstance == 16u && kLightModePlantLit == 32u && kLightModeLeaf == 64u && kLightModeCount == 128u);
    // A lit mesh record's mode by its vertex kind and family: the colour stream's is what it was (6); a
    // probe object's or trunk's adds the probe bit (14); an instanced plant's adds the instance bit
    // (22) and, for the family whose pixel shader has the ambient factor, the plant-lit bit (54). No
    // other pair of a kind and a family has one.
    CHECK(467, LitObjectMode(LightVertexKind::kStream, LightFamily::kObject) == 6u &&
                   LitObjectMode(LightVertexKind::kProbe, LightFamily::kObject) == 14u &&
                   LitObjectMode(LightVertexKind::kProbe, LightFamily::kTreeTrunk) == 14u &&
                   LitObjectMode(LightVertexKind::kInstance, LightFamily::kPlant) == 22u &&
                   LitObjectMode(LightVertexKind::kInstance, LightFamily::kPlantLit) == 54u);
    // A leaf (plan Task 8c) is a probe record with the leaf bit (78); on no other kind.
    CHECK(467, LitObjectMode(LightVertexKind::kProbe, LightFamily::kTreeLeaf) == 78u &&
                   LitObjectMode(LightVertexKind::kProbe, LightFamily::kTreeLeaf) ==
                       (kLightModeObject | kLightModeLit | kLightModeProbe | kLightModeLeaf) &&
                   LitObjectMode(LightVertexKind::kStream, LightFamily::kTreeLeaf) == 0u &&
                   LitObjectMode(LightVertexKind::kInstance, LightFamily::kTreeLeaf) == 0u &&
                   LitObjectMode(LightVertexKind::kTerrain, LightFamily::kTreeLeaf) == 0u);
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
                   "Light: off, lit 0 (terrain-layer 0, terrain-far 0, object 0, tree-trunk 0, plant 0, plant-lit 0, tree-leaf 0), unlit 0");
    st.light_unlit[size_t(ClayUnlit::kOff)] = 948;
    CHECK(481, FormatLightDrawnText(st) ==
                   "Light: off, lit 0 (terrain-layer 0, terrain-far 0, object 0, tree-trunk 0, plant 0, plant-lit 0, tree-leaf 0), "
                   "unlit 948 {light-off: 948}");
    // The switch on and the texture path off: the light is of the material, so the frame is drawn
    // without it, and the line says why it reads "off".
    st.light_textures_off = true;
    CHECK(532, FormatLightDrawnText(st) ==
                   "Light: off (the switch is on; textures are off), lit 0 (terrain-layer 0, terrain-far 0, object 0, tree-trunk 0, plant 0, "
                   "plant-lit 0, tree-leaf 0), unlit 948 {light-off: 948}");
    // (the note belongs to "off" alone: a frame drawn with the light on never carries it)
    st.light_switch = true;
    CHECK(532, FormatLightDrawnText(st).starts_with("Light: on, block missing, lit 0 ("));
    st = ClayStats{};
    st.light_switch = true;
    st.light_unlit[size_t(ClayUnlit::kBlock)] = 12;
    CHECK(482, FormatLightDrawnText(st) ==
                   "Light: on, block missing, lit 0 (terrain-layer 0, terrain-far 0, object 0, tree-trunk 0, plant 0, plant-lit 0, tree-leaf 0), "
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
                   "plant-lit 0, tree-leaf 0), unlit 264 {light-unknown: 262, normal-unsupported: 2}, unknown {0x7CD57B81550F19E3: 3, "
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
    // A lit leaf (plan Task 8c) is a probe record: the same three buffers; the leaf bit names none.
    CHECK(489, is(LitObjectMode(capture::LightVertexKind::kProbe, capture::LightFamily::kTreeLeaf), B::kPositions, B::kNormals,
                  B::kProbeSets, B::kProbeWeights));
    CHECK(489, is(kLightModeLeaf, B::kPositions, B::kPositions, B::kPositions, B::kPositions) &&
                   is(kLightModeLeaf | kLightModeLit | kLightModeProbe, B::kPositions, B::kPositions, B::kPositions, B::kPositions));
    // The vertex shader's three mesh blocks exclude each other (an object's or trunk's: the object bit
    // without the instance and leaf bits; a plant's: with the instance bit, without the leaf bit; a
    // leaf's: with the probe and leaf bits, without the instance bit), and a mode none of them takes
    // (which the pass never makes) loads nothing: the positions at all of t9 to t11, whatever its
    // object bit says. The leaf bit beside the probe bit of an instanced plant changes nothing.
    CHECK(489, is(kLightModeObject | kLightModeLeaf, B::kPositions, B::kPositions, B::kPositions, B::kPositions) &&
                   is(kLightModeObject | kLightModeLeaf | kLightModeInstance, B::kPositions, B::kPositions, B::kPositions, B::kPositions) &&
                   is(kLightModeObject | kLightModeLeaf | kLightModeInstance | kLightModeProbe, B::kPositions, B::kPositions, B::kPositions,
                      B::kPositions) &&
                   is(kLightModeObject | kLightModeLeaf | kLightModeProbe | kLightModePlantLit, B::kPositions, B::kNormals, B::kProbeSets,
                      B::kProbeWeights) &&
                   is(kLightModeTerrainNormals | kLightModeObject | kLightModeLeaf, B::kTerrainNormals, B::kPositions, B::kPositions,
                      B::kPositions));
    // The terrain bit beside a mesh block (a mode the pass never makes): the block overwrites what
    // the terrain normals give and the compiled program loads none, so the mode names none: a mesh
    // record's normals (three floats a vertex) are never bound where two are read.
    CHECK(533, is(kLightModeTerrainNormals | kLightModeObject | kLightModeLit, B::kPositions, B::kNormals, B::kColours, B::kPositions) &&
                   is(kLightModeTerrainNormals | kLightModeObject | kLightModeProbe, B::kPositions, B::kNormals, B::kProbeSets,
                      B::kProbeWeights) &&
                   is(kLightModeTerrainNormals | kLightModeObject | kLightModeInstance, B::kPositions, B::kNormals, B::kInstanceColours,
                      B::kBend) &&
                   is(kLightModeTerrainNormals | kLightModeObject | kLightModeProbe | kLightModeLeaf, B::kPositions, B::kNormals,
                      B::kProbeSets, B::kProbeWeights));
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
    // Every mode (the numbers are the vertex shader's own tests of the mode: 82 the object, instance
    // and leaf bits, 90 those and the probe bit): t9 is the normals exactly where one of the three
    // mesh blocks runs, t8 the terrain normals exactly with the terrain bit, and t10 / t11 are a pair
    // of one kind, the kind of the block that runs.
    for (uint32_t mode = 0; mode < kLightModeCount; ++mode) {
      const ClayLightBuffers s = LightBufferSources(mode);
      const bool object = (mode & 82u) == 2u, plant = (mode & 82u) == 18u, leaf = (mode & 90u) == 74u;
      bool ok = int(object) + int(plant) + int(leaf) <= 1;
      ok = ok && (s.source[0] == B::kTerrainNormals) == ((mode & kLightModeTerrainNormals) != 0 && !object && !plant && !leaf) &&
           (s.source[1] == B::kNormals) == (object || plant || leaf);
      ok = ok && (s.source[0] == B::kTerrainNormals || s.source[0] == B::kPositions) && (s.source[1] == B::kNormals || s.source[1] == B::kPositions);
      if (!object && !plant && !leaf) ok = ok && s.source[2] == B::kPositions && s.source[3] == B::kPositions;
      if (plant) ok = ok && s.source[2] == B::kInstanceColours && s.source[3] == B::kBend;
      if (leaf) ok = ok && s.source[2] == B::kProbeSets && s.source[3] == B::kProbeWeights;
      if (object) {
        ok = ok && ((mode & kLightModeProbe) ? s.source[2] == B::kProbeSets && s.source[3] == B::kProbeWeights
                                             : s.source[2] == B::kColours && s.source[3] == B::kPositions);
      }
      CHECK(489, ok);
    }
    static_assert((kLightModeObject | kLightModeInstance | kLightModeLeaf) == 82u &&
                  (kLightModeObject | kLightModeInstance | kLightModeLeaf | kLightModeProbe) == 90u &&
                  (kLightModeObject | kLightModeLeaf | kLightModeProbe) == 74u);
    // The four registers are the layout's last four parameters, vertex stage, buffers; the layout has
    // twelve parameters (the backend's limit) and 56 of the root signature's 64 words.
    for (uint32_t i = 0; i < kClayLightBufferCount; ++i) {
      const ClayBinding& b = kClayBindings[kClayLightBufferParam0 + i];
      CHECK(488, b.kind == ClayBindingKind::kBuffer && b.reg == 9 + i && b.stage == ClayBindingStage::kVertex && b.count == 1);
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
                  sizeof(ClayTerrainConstants) <= kClayTerrainSlotBytes && kClayFrameLightBytes <= kClayTerrainSlotBytes &&
                  sizeof(ClayFrameConstants) <= kClayFrameSlots * kClayTerrainSlotBytes);
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
    static_assert(sizeof(ClayObjectConstants) == 288 && kClayPlantConstantBytes == 80 && kClayPlantConstantBytes <= kClayTerrainSlotBytes &&
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
    st.light_switch = true;
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

  // 520-539: the tree leaves (plan Task 8c; lighting-formulas.md, "Tree leaves").
  {
    using capture::LightFamily;
    using capture::LightVertexKind;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    auto close_to = [](float got, double want) { return std::fabs(double(got) - want) <= 2e-6 * std::max(1.0, std::fabs(want)); };
    auto close3 = [&](const Float3& got, double x, double y, double z) { return close_to(got.x, x) && close_to(got.y, y) && close_to(got.z, z); };
    auto finite3 = [](const Float3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    // The frame values, registers, weights and pins of tests/test_light_check.py, PinnedForTheRenderer
    // (leaf, leaf_cases, LEAF_PINS), composed as the clay shaders compose: the vertex side makes N = M n
    // as it is, the world position, the unit vector to the eye, the sun's two scalars, the probe colour
    // and the ambient; the pixel side multiplies the scalars by the sun's colour inside the family's
    // function, with the mask at 1, and applies c26.
    const Float3 tint{1.125f, 1.375f, 1.875f}, add{0.0068f, 0.071f, 0.092f};
    const Float3 sun_col{0.99f, 0.83f, 0.40f}, sun_dir{-0.6334f, 0.0346f, 0.7731f};
    const Float3 green{0.3f, 0.5f, 0.2f}, red{0.5f, 0.4f, 0.3f}, one{1.0f, 1.0f, 1.0f}, zero{0.0f, 0.0f, 0.0f};
    const Float3 n{-0.6f, 0.0f, 0.8f}, position{0.5f, -1.0f, 2.0f}, eye_front{1.5f, -10.0f, 13.0f}, eye_behind{0.0f, 12.0f, -5.0f};
    const float probe[12][4] = {{0.25f, 0.125f, 0.0625f, 9.0f}, {0.5f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.125f, 1.0f},
                                {0.125f, 0.25f, 0.0f, 9.0f},    {-0.25f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.25f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f},
                                {0.0625f, 0.5f, 0.25f, 9.0f},   {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};
    const float weights[4] = {0.5f, -0.25f, 1.0f, 0.125f};
    // 520: the literals. The 1 / 2.2 of the ambient exponent as the three vertex shaders' tables hold
    // it, the two of the pixel shader's mask, the mode bit.
    CHECK(520, kLeafGamma == 0.45454543828964233f && kLeafMaskGain == 0.6f && kLeafMaskBias == 0.4f && kLightModeLeaf == 64u &&
                   kLeafColourPower == kInstanceColourPower * kLeafGamma);
    // The inputs of one leaf draw: the rotation M_ROT of the reference (its rows over c7.w, as the
    // game's c4..c6 hold them), the translation, the wrap.
    auto inputs = [&](float scale, float wrap, float c27w, const Float3& c49, const Float3& c26) {
      capture::LightInputs l;
      l.family = LightFamily::kTreeLeaf;
      l.kind = LightVertexKind::kProbe;
      const float rot[3][3] = {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
      std::memcpy(l.world_rot, rot, sizeof(rot));
      l.world_scale = scale;
      l.world_pos[0] = 1.0f; l.world_pos[1] = 2.0f; l.world_pos[2] = 3.0f;
      l.wrap = wrap;
      l.c27w = c27w;
      l.c49[0] = c49.x; l.c49[1] = c49.y; l.c49[2] = c49.z;
      l.c26[0] = c26.x; l.c26[1] = c26.y; l.c26[2] = c26.z;
      std::memcpy(l.vs_const, probe, sizeof(probe));
      return l;
    };
    struct Leaf {
      bool ok;
      float front, back;
      Float3 amb, colour;
    };
    auto leaf = [&](const Float3& normal, const Float3& eye, const Float3& A, uint32_t a0, float scale, float wrap, float c27w,
                    const Float3& c49, const Float3& c26) {
      Leaf out{};
      ClayObjectConstants c;
      out.ok = MakeClayObjectConstants(inputs(scale, wrap, c27w, c49, c26), &c);
      // The vertex side.
      const Float3 N{c.rot[0][0] * normal.x + c.rot[0][1] * normal.y + c.rot[0][2] * normal.z,
                     c.rot[1][0] * normal.x + c.rot[1][1] * normal.y + c.rot[1][2] * normal.z,
                     c.rot[2][0] * normal.x + c.rot[2][1] * normal.y + c.rot[2][2] * normal.z};
      const Float3 view = LeafView(eye, LeafWorldPosition(c, position));
      const LeafSides s = LeafLight(N, view, LightNormalize(sun_dir), c.leaf[0], 1.0f);
      out.front = s.front;
      out.back = s.back;
      out.amb = InstanceAmbient(ProbeColour(c.probe, a0, weights), tint, c.colour_power * 1.3f, add);
      // The pixel side.
      const LeafMasks m = LeafMask(1.0f, kLeafMaskGain, kLeafMaskBias);
      const Float3 lit = LightLeaf(A, out.amb, s.front, s.back, sun_col, c.c27w, m.m1, m.m2, {c.c49[0], c.c49[1], c.c49[2]});
      out.colour = {lit.x * c.c26[0], lit.y * c.c26[1], lit.z * c.c26[2]};
      return out;
    };
    // 521: the viewer on the normal's side, the sun in front: the wrapped front light, no back light.
    Leaf f = leaf(n, eye_front, green, 0, 1.0f, 0.4f, 1.0f, one, one);
    CHECK(521, f.ok && close_to(f.front, 0.7126408495157472) && f.back == 0.0f &&
                   close3(f.amb, 0.46369198767397196, 0.16310554084116385, 0.5053389422232634) &&
                   close3(f.colour, 0.3507619286083685, 0.377298722969617, 0.15807905640591247));
    // 522: the viewer behind the card: the normal is turned to him, the sun is then behind it, and
    // the back light passes through a leaf texel (green >= red, equality included), not through a twig's.
    Leaf b = leaf(n, eye_behind, green, 0, 1.0f, 0.4f, 1.0f, one, one);
    CHECK(522, b.ok && b.front == 0.0f && close_to(b.back, 0.5975238349664942) &&
                   close3(b.colour, 0.31657217528724035, 0.329525161931677, 0.14886969524197222));
    b = leaf(n, eye_behind, red, 0, 1.0f, 0.4f, 1.0f, one, one);
    CHECK(522, b.front == 0.0f && close_to(b.back, 0.5975238349664942) &&
                   close3(b.colour, 0.23184599383698598, 0.06524221633646554, 0.15160168266697902));
    b = leaf(n, eye_behind, {0.4f, 0.4f, 0.2f}, 0, 1.0f, 0.4f, 1.0f, one, one);
    CHECK(522, close3(b.colour, 0.42209623371632055, 0.26362012954534164, 0.14886969524197222));
    // 523: c7.w = 0.5 (in the normal, which the game does not normalise, and not in the world
    // position), set 8, another wrap, c27.w on the ambient part only, c49 on the whole colour, c26 after.
    f = leaf(n, eye_front, green, 8, 0.5f, 0.25f, 0.5f, {1.0f, 0.5f, 2.0f}, {1.0f, 1.0f, 0.5f});
    CHECK(523, f.ok && close_to(f.front, 0.4390788757288185) && f.back == 0.0f &&
                   close3(f.amb, 1.2240510043759552, 0.8173572059344705, 3.6166064948512817) &&
                   close3(f.colour, 0.31401407674785237, 0.19327851745553865, 0.3967869595434337));
    b = leaf(n, eye_behind, green, 8, 0.5f, 0.25f, 0.5f, {1.0f, 0.5f, 2.0f}, {1.0f, 1.0f, 0.5f});
    CHECK(523, b.front == 0.0f && close_to(b.back, 0.22157000987666525) &&
                   close3(b.colour, 0.24941394358976282, 0.14814542779121684, 0.37938625027526146));
    // 524: a zero normal: front = wrap / (1 + wrap), no back light. A probe colour of zero in red and
    // negative in green (set 4): the power of the absolute value. The eye in the vertex: no view
    // direction, so no turn and no back light. No wrap: the plain clamped cosine.
    f = leaf(zero, eye_front, green, 0, 1.0f, 0.4f, 1.0f, one, one);
    CHECK(524, close_to(f.front, 0.28571428571428575) && f.back == 0.0f &&
                   close3(f.colour, 0.22396473915933449, 0.2001241989920105, 0.12392493130179555));
    f = leaf(n, eye_front, green, 4, 1.0f, 0.4f, 1.0f, one, one);
    CHECK(524, f.amb.x == 0.0068f && close_to(f.amb.y, 0.10840647433840495) && f.amb.z == 0.092f &&
                   close3(f.colour, 0.21369433230617693, 0.3499491897182375, 0.0754112679612598));
    f = leaf(n, {2.0f, 2.5f, 5.0f}, green, 0, 1.0f, 0.4f, 1.0f, one, one);
    CHECK(524, close_to(f.front, 0.7126408495157472) && f.back == 0.0f &&
                   close3(f.colour, 0.3507619286083685, 0.377298722969617, 0.15807905640591247));
    f = leaf(n, eye_front, green, 0, 1.0f, 0.0f, 1.0f, one, one);
    CHECK(524, close_to(f.front, 0.5976971893220461) && f.back == 0.0f &&
                   close3(f.colour, 0.31662366153083926, 0.32959710398923103, 0.14888356359041638));

    // 525: the pieces by hand. The light faces the leaf (L . N = 0.5), the viewer too (V . N = 0.5),
    // wrap 0.5: front = 1 / 1.5, no back light. The light behind it: front = sat(0 / 1.5) = 0, back =
    // 0.5 * (1 - (1 - 0.5)^2) = 0.375, times the spot. A normal that faces away from the viewer is turned.
    {
      const float r = std::sqrt(0.75f);
      LeafSides s = LeafLight({0.0f, 0.0f, 1.0f}, {r, 0.0f, 0.5f}, {0.0f, r, 0.5f}, 0.5f, 1.0f);
      CHECK(525, close_to(s.front, 2.0 / 3.0) && s.back == 0.0f);
      s = LeafLight({0.0f, 0.0f, 1.0f}, {r, 0.0f, 0.5f}, {0.0f, r, -0.5f}, 0.5f, 0.5f);
      CHECK(525, s.front == 0.0f && close_to(s.back, 0.1875));
      const LeafSides turned = LeafLight({0.0f, 0.0f, -1.0f}, {r, 0.0f, 0.5f}, {0.0f, r, -0.5f}, 0.5f, 0.5f);
      CHECK(525, turned.front == s.front && turned.back == s.back);
      // (the spot is on the front light too: half of 1 / 1.5)
      s = LeafLight({0.0f, 0.0f, 1.0f}, {r, 0.0f, 0.5f}, {0.0f, r, 0.5f}, 0.5f, 0.5f);
      CHECK(525, close_to(s.front, 1.0 / 3.0) && s.back == 0.0f);
      // The normal is used as it comes (not normalised): twice as long, twice the cosine.
      s = LeafLight({0.0f, 0.0f, 2.0f}, {r, 0.0f, 0.5f}, {0.0f, r, 0.25f}, 0.0f, 1.0f);
      CHECK(525, s.front == 0.5f && s.back == 0.0f);
      // The two factors of the mask: m1 = mask * gain + bias, m2 = m1 * gain + bias; with the shader's
      // literals a mask of 1 gives exactly (1, 1).
      LeafMasks m = LeafMask(0.5f, 0.5f, 0.5f);
      CHECK(525, m.m1 == 0.75f && m.m2 == 0.875f);
      m = LeafMask(1.0f, kLeafMaskGain, kLeafMaskBias);
      CHECK(525, m.m1 == 1.0f && m.m2 == 1.0f);
      m = LeafMask(0.0f, kLeafMaskGain, kLeafMaskBias);
      CHECK(525, m.m1 == 0.4f && close_to(m.m2, 0.64));
      m = LeafMask(7.0f, 0.5f, 0.0f);  // (the mask is clamped)
      CHECK(525, m.m1 == 0.5f && m.m2 == 0.25f);
      // (A * (amb * c27w + m1 * sun * front) + green * A * m2 * sun * back) * c49:
      // a * (0.5 * 0.5 + 0.75 * 2 * 0.5) = a; + a * 0.875 * 2 * 0.25 = 0.4375 a where green >= red.
      Float3 c = LightLeaf({0.25f, 0.5f, 0.25f}, {0.5f, 0.5f, 0.5f}, 0.5f, 0.25f, {2.0f, 2.0f, 2.0f}, 0.5f, 0.75f, 0.875f, {1.0f, 0.5f, 2.0f});
      CHECK(525, c.x == 0.359375f && c.y == 0.359375f && c.z == 0.71875f);
      c = LightLeaf({0.5f, 0.25f, 0.25f}, {0.5f, 0.5f, 0.5f}, 0.5f, 0.25f, {2.0f, 2.0f, 2.0f}, 0.5f, 0.75f, 0.875f, {1.0f, 0.5f, 2.0f});
      CHECK(525, c.x == 0.5f && c.y == 0.125f && c.z == 0.5f);
      // The world position and the view: (M p) / c7.w + T; the unit vector to the eye, zero in the eye.
      ClayObjectConstants k;
      CHECK(525, MakeClayObjectConstants(inputs(0.5f, 0.4f, 1.0f, one, one), &k));
      const Float3 w = LeafWorldPosition(k, position);
      CHECK(525, w.x == 2.0f && w.y == 2.5f && w.z == 5.0f);
      Float3 v = LeafView({2.0f, 2.5f, 9.0f}, w);
      CHECK(525, v.x == 0.0f && v.y == 0.0f && v.z == 1.0f);
      v = LeafView(w, w);
      CHECK(525, v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
    }
    // 526: zeros and values that are not numbers give numbers, from every function (review focus 2);
    // a wrap of -1 (the formula's denominator zero) gives no front light.
    {
      const Float3 bad{nan, inf, -inf};
      for (const LeafSides& s : {LeafLight(bad, bad, bad, nan, inf), LeafLight(zero, zero, zero, -1.0f, 1.0f),
                                 LeafLight(n, bad, n, 0.4f, nan), LeafLight(n, n, bad, inf, 1.0f),
                                 LeafLight({1.0e30f, 1.0e30f, 1.0e30f}, {1.0e30f, 1.0e30f, 1.0e30f}, {-1.0e30f, 1.0e30f, 1.0e30f}, 0.4f, 1.0e30f)}) {
        CHECK(526, std::isfinite(s.front) && std::isfinite(s.back));
      }
      LeafSides s = LeafLight({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, -1.0f, 1.0f);
      CHECK(526, s.front == 0.0f && s.back == 0.0f);
      // (also where the numerator is above zero, a normal longer than 1: the quotient would be +infinity,
      // which a clamp makes 1)
      s = LeafLight({0.0f, 0.0f, 2.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, -1.0f, 1.0f);
      CHECK(526, s.front == 0.0f && s.back == 0.0f);
      // The viewer's cosine is clamped before the back light's factor (a normal longer than 1 again:
      // V . N = 2 counts as 1, the factor is 1 and not 1 - (1 - 2)^2 = 0).
      s = LeafLight({0.0f, 0.0f, 4.0f}, {std::sqrt(0.75f), 0.0f, 0.5f}, {0.0f, std::sqrt(0.984375f), -0.125f}, 0.0f, 1.0f);
      CHECK(526, s.front == 0.0f && s.back == 0.5f);
      for (const LeafMasks& m : {LeafMask(nan, inf, nan), LeafMask(1.0f, 1.0e30f, 1.0e30f), LeafMask(0.5f, -inf, inf), LeafMask(1.0f, 3.0e38f, 3.0e38f)}) {
        CHECK(526, std::isfinite(m.m1) && std::isfinite(m.m2));
      }
      CHECK(526, finite3(LightLeaf(bad, bad, nan, inf, bad, inf, nan, inf, bad)) &&
                     finite3(LightLeaf({1.0e30f, 1.0e30f, 1.0e30f}, {1.0e30f, 1.0e30f, 1.0e30f}, 1.0e30f, 1.0e30f, {1.0e30f, 1.0e30f, 1.0e30f},
                                       1.0e30f, 1.0e30f, 1.0e30f, {1.0e30f, 1.0e30f, 1.0e30f})));
      CHECK(526, finite3(LeafView(bad, bad)) && finite3(LeafView({3.0e38f, -3.0e38f, 0.0f}, {-3.0e38f, 3.0e38f, 0.0f})));
      ClayObjectConstants k{};
      k.rot[0][0] = nan; k.rot[1][3] = inf; k.leaf[1] = nan;
      CHECK(526, finite3(LeafWorldPosition(k, bad)) && finite3(LeafWorldPosition(k, position)));
    }

    // 527: a leaf's constants: a trunk's (the rows of M, c49, c27.w, c26, the twelve probe registers),
    // and its own: the world translation in the rows' fourth components, the exponent's factor of the
    // ambient (2.2 * 0.4545, over the frame's c21.w) where a stream object has its colour's power, and
    // the wrap with the inverse of c7.w in one more register behind the probe registers.
    static_assert(sizeof(ClayObjectConstants) == 288 && offsetof(ClayObjectConstants, leaf) == 272 &&
                  sizeof(ClayObjectConstants) <= kClayObjectSlotBytes);
    ClayObjectConstants oc;
    const ClayObjectConstants empty{};
    const capture::LightInputs li = inputs(0.5f, 0.4f, 0.75f, {1.0f, 0.5f, 2.0f}, {0.25f, 1.0f, 0.5f});
    CHECK(527, MakeClayObjectConstants(li, &oc) && oc.rot[0][0] == 0.0f && oc.rot[0][1] == -0.5f && oc.rot[1][0] == 0.5f &&
                   oc.rot[2][2] == 0.5f && oc.rot[0][3] == 1.0f && oc.rot[1][3] == 2.0f && oc.rot[2][3] == 3.0f);
    CHECK(527, oc.c49[0] == 1.0f && oc.c49[1] == 0.5f && oc.c49[2] == 2.0f && oc.c27w == 0.75f && oc.c26[0] == 0.25f &&
                   oc.c26[1] == 1.0f && oc.c26[2] == 0.5f && oc.colour_power == kLeafColourPower &&
                   std::memcmp(oc.probe, probe, sizeof(probe)) == 0);
    // (leaf[2], leaf[3]: the trees' fog; these inputs have no scale: case 586)
    CHECK(527, oc.leaf[0] == 0.4f && oc.leaf[1] == 2.0f && oc.leaf[2] == 0.0f && oc.leaf[3] == kTreeFogFormNone);
    // No other kind of record carries the wrap or the inverse scale: a trunk with the same inputs has zeros there.
    {
      capture::LightInputs trunk = li;
      trunk.family = LightFamily::kTreeTrunk;
      ClayObjectConstants t;
      CHECK(527, MakeClayObjectConstants(trunk, &t) && t.rot[0][3] == 0.0f && t.rot[1][3] == 0.0f && t.rot[2][3] == 0.0f &&
                     t.leaf[0] == 0.0f && t.leaf[1] == 0.0f && t.leaf[2] == 0.0f && t.leaf[3] == kTreeFogFormNone && t.colour_power == 0.0f);
      capture::LightInputs object = li;
      object.family = LightFamily::kObject;
      CHECK(527, MakeClayObjectConstants(object, &t) && t.rot[1][3] == 0.0f && t.leaf[0] == 0.0f && t.leaf[1] == 0.0f &&
                     t.leaf[2] == 0.0f && t.leaf[3] == kTreeFogFormGround);
      // (and a value of theirs that is not a number does not refuse a trunk, which does not read it)
      trunk.world_pos[1] = nan;
      trunk.wrap = inf;
      CHECK(527, MakeClayObjectConstants(trunk, &t) && t.rot[1][3] == 0.0f && t.leaf[0] == 0.0f);
    }
    // 528: refused as a whole on one value that is not a number or not of usable size: the three of
    // the translation (a position: up to kLightPositionMax), the wrap (0 to kLightValueMax: the
    // formula divides by 1 + wrap), a scale without an inverse, and everything a trunk is refused for.
    for (int k = 0; k < 3; ++k) {
      for (float value : {nan, inf, -inf, 2.0f * kLightPositionMax, -2.0f * kLightPositionMax}) {
        capture::LightInputs bad = li;
        bad.world_pos[k] = value;
        oc.c27w = 5.0f;
        CHECK(528, !MakeClayObjectConstants(bad, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0);
      }
    }
    for (float value : {nan, inf, -inf, -0.25f, -1.0f, 2.0f * kLightValueMax}) {
      capture::LightInputs bad = li;
      bad.wrap = value;
      oc.c27w = 5.0f;
      CHECK(528, !MakeClayObjectConstants(bad, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0);
    }
    {
      capture::LightInputs edge = li;
      edge.world_pos[0] = kLightPositionMax;
      edge.world_pos[2] = -kLightPositionMax;
      edge.wrap = 0.0f;
      CHECK(528, MakeClayObjectConstants(edge, &oc) && oc.rot[0][3] == kLightPositionMax && oc.rot[2][3] == -kLightPositionMax &&
                     oc.leaf[0] == 0.0f);
      edge.wrap = kLightValueMax;
      CHECK(528, MakeClayObjectConstants(edge, &oc) && oc.leaf[0] == kLightValueMax);
      static_assert(kLightPositionMax == 1048576.0f);
    }
    // (5e-4: a rotation with a direction, whose inverse scale, 2000, is beyond what a shader gets)
    CHECK(528, ClassifyRotation(li.world_rot, 5.0e-4f) != RotationKind::kDegenerate);
    for (float scale : {0.0f, nan, inf, 1.0e-6f, 5.0e-4f}) {
      capture::LightInputs bad = li;
      bad.world_scale = scale;
      CHECK(528, !MakeClayObjectConstants(bad, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0);
    }
    for (int field = 0; field < 5; ++field) {
      for (float value : {nan, inf, 4.0f * kLightValueMax}) {
        capture::LightInputs bad = li;
        float* at[] = {&bad.world_rot[0][1], &bad.vs_const[11][3], &bad.c27w, &bad.c49[1], &bad.c26[2]};
        *at[field] = value;
        CHECK(528, !MakeClayObjectConstants(bad, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0);
      }
    }
    // The leaf family stands on the probe kind only.
    for (LightVertexKind kind : {LightVertexKind::kNone, LightVertexKind::kTerrain, LightVertexKind::kStream, LightVertexKind::kInstance}) {
      capture::LightInputs bad = li;
      bad.kind = kind;
      CHECK(528, !MakeClayObjectConstants(bad, &oc) && std::memcmp(&oc, &empty, sizeof(oc)) == 0 && !LightFamilyDrawn(bad));
    }
    CHECK(528, LightFamilyDrawn(li) && LightObjectReady(li.kind, true, false, true, false) && !LightObjectReady(li.kind, true, true, false, true));

    // 529: the frame's eye (pixel c19 of the light block; vertex c9 of the leaf shaders is that value
    // bit for bit in every leaf row compared). It is a position: usable up to kLightPositionMax. A
    // frame whose eye is not usable is still lit; only its leaves are not (LightFrameLights).
    capture::LightBlock block{};
    for (int i = 0; i < 3; ++i) {
      block.sun_dir[i] = i == 2 ? 1.0f : 0.0f;
      block.sun_col[i] = 0.5f;
      block.tint[i] = 1.0f;
      block.eye[i] = 50.0f + float(i);
    }
    block.tint_pow = 1.3f;
    block.valid = true;
    static_assert(kClayFrameLightBytes == 112 && offsetof(ClayFrameConstants, eye) == 96 && offsetof(ClayFrameConstants, eye_on) == 108);
    ClayFrameConstants frame = MakeClayFrameConstants(block, ClayLight::kOn);
    CHECK(529, frame.light_on == 1 && frame.eye_on == 1 && frame.eye[0] == 50.0f && frame.eye[1] == 51.0f && frame.eye[2] == 52.0f);
    for (LightFamily family : {LightFamily::kTerrainLayer, LightFamily::kTerrainFar, LightFamily::kObject, LightFamily::kTreeTrunk,
                               LightFamily::kPlant, LightFamily::kPlantLit, LightFamily::kTreeLeaf}) {
      CHECK(529, LightFrameLights(frame, family));
    }
    for (int k = 0; k < 3; ++k) {
      for (float value : {nan, inf, -inf, 2.0f * kLightPositionMax}) {
        capture::LightBlock far_eye = block;
        far_eye.eye[k] = value;
        frame = MakeClayFrameConstants(far_eye, ClayLight::kOn);
        CHECK(529, frame.light_on == 1 && frame.eye_on == 0 && frame.eye[0] == 0.0f && frame.eye[1] == 0.0f && frame.eye[2] == 0.0f);
        CHECK(529, !LightFrameLights(frame, LightFamily::kTreeLeaf) && LightFrameLights(frame, LightFamily::kObject) &&
                       LightFrameLights(frame, LightFamily::kPlantLit) && LightFrameLights(frame, LightFamily::kTerrainFar));
      }
    }
    {
      capture::LightBlock edge = block;
      edge.eye[1] = -kLightPositionMax;
      CHECK(529, MakeClayFrameConstants(edge, ClayLight::kOn).eye_on == 1);
      // No light, no eye: the switch off, or no block.
      const ClayFrameConstants none{};
      frame = MakeClayFrameConstants(block, ClayLight::kOff);
      CHECK(529, std::memcmp(&frame, &none, sizeof(frame)) == 0 && !LightFrameLights(frame, LightFamily::kTreeLeaf) &&
                     !LightFrameLights(frame, LightFamily::kObject));
      capture::LightBlock invalid = block;
      invalid.valid = false;
      frame = MakeClayFrameConstants(invalid, ClayLight::kOn);
      CHECK(529, std::memcmp(&frame, &none, sizeof(frame)) == 0);
      // (the eye alone lights nothing: a block the function does not make, an eye without a lit frame)
      ClayFrameConstants eye_only{};
      eye_only.eye_on = 1;
      CHECK(529, !LightFrameLights(eye_only, LightFamily::kTreeLeaf) && !LightFrameLights(eye_only, LightFamily::kObject));
    }

    // 530: the Light line counts the leaves as a family of their own.
    ClayStats st;
    st.light_switch = st.light_block = true;
    st.light_drawn[size_t(LightFamily::kTerrainLayer)] = 426;
    st.light_drawn[size_t(LightFamily::kTreeLeaf)] = 41;
    CHECK(530, FormatLightDrawnText(st).starts_with(
                   "Light: on, block valid, lit 467 (terrain-layer 426, terrain-far 0, object 0, tree-trunk 0, plant 0, plant-lit 0, "
                   "tree-leaf 41), unlit 0"));
  }

  // --- Native shadow maps (plan Task 4, sub-project 8; frame-map section 15) ---
  {
    using capture::CasterKind;
    using capture::Compare;
    using capture::Cull;
    using capture::ShadowCaster;
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    // 540: the maps' size is the game's, a constant.
    static_assert(kShadowMapSize == 1024 && kShadowMapTexelBytes == 4 && kShadowMapClear == 1.0f && kShadowViewportOrigin == 0.5f);
    CHECK(540, FormatShadowTargetText() == "shadow maps 2 x 1024 x 1024: depth D32_FLOAT 8.0 MB together");

    // 541: the depth convention at the range ends. The game: window depth 1 - z, cleared to 0,
    // greater-equal. Native: depth z, cleared to 1, less-equal. The two orders are one order.
    CHECK(541, ShadowGameDepth(0.0f) == 1.0f && ShadowGameDepth(1.0f) == 0.0f && ShadowNativeDepth(1.0f) == 0.0f &&
                   ShadowNativeDepth(0.0f) == 1.0f && ShadowNativeDepth(ShadowGameDepth(0.25f)) == 0.25f);
    CHECK(541, kShadowMapCompare == Compare::kLessEqual && MirrorCompare(Compare::kGreaterEqual) == kShadowMapCompare);
    CHECK(541, ShadowNativeDepth(0.0f) == kShadowMapClear);  // the game's clear, 0, is the native clear
    for (float a : {0.0f, 0.1f, 0.5f, 1.0f}) {
      for (float b : {0.0f, 0.1f, 0.7f, 1.0f}) {
        CHECK(541, (ShadowGameDepth(a) >= ShadowGameDepth(b)) == (a <= b));
      }
    }
    // The state of every shadow row (frame-map section 15: 0x2200 = 0x00708766 under 2113 = -1) is
    // the usable one; the scene's less-equal under the reversed range, an unreversed range, no
    // write, no test and an unread state are not.
    {
      capture::RenderState rs = State(0x00708766u, 0x00010001u, 0x00000004u, 0x00219806u, 0);
      CHECK(542, rs.valid && rs.depth_reversed && rs.depth_func == Compare::kGreaterEqual && ShadowDepthStateUsable(rs));
      capture::RenderState bad = rs;
      bad.depth_reversed = false;
      CHECK(542, !ShadowDepthStateUsable(bad));
      bad = rs;
      bad.depth_write = false;
      CHECK(542, !ShadowDepthStateUsable(bad));
      bad = rs;
      bad.depth_test = false;
      CHECK(542, !ShadowDepthStateUsable(bad));
      bad = rs;
      bad.valid = false;
      CHECK(542, !ShadowDepthStateUsable(bad));
      for (Compare f : {Compare::kNever, Compare::kLess, Compare::kEqual, Compare::kLessEqual, Compare::kGreater, Compare::kNotEqual,
                        Compare::kAlways}) {
        bad = rs;
        bad.depth_func = f;
        CHECK(542, !ShadowDepthStateUsable(bad));
      }
    }

    // 543: ShadowBias, the game's polygon offset on its window depth: (scale / 16) * slope + offset.
    // The near pass's pair (-32, -0.001) is D3D's slope scale -2 and bias -0.001.
    CHECK(543, ShadowBias(0.5f, 0.0f, -32.0f, -0.001f) == 0.5f - 0.001f);
    CHECK(543, std::fabs(ShadowBias(0.5f, 0.01f, -32.0f, -0.001f) - (0.5f - 0.02f - 0.001f)) < 1e-7f);
    CHECK(543, std::fabs(ShadowBias(0.5f, 0.01f, -80.0f, -0.005f) - (0.5f - 0.05f - 0.005f)) < 1e-7f);
    CHECK(543, ShadowBias(0.25f, 3.0f, 0.0f, 0.0f) == 0.25f && ShadowBias(1.0f, 0.0f, -32.0f, 0.0f) == 1.0f);
    // The terms the shader adds are the same step in the native direction: 1 - ShadowBias(1 - z).
    ShadowCaster near_caster;
    near_caster.biased = true;
    near_caster.bias_scale = -32.0f;
    near_caster.bias_offset = -0.001f;
    ShadowCaster far_caster = near_caster;
    far_caster.bias_scale = -80.0f;
    far_caster.bias_offset = -0.005f;
    ShadowBiasTerms terms;
    CHECK(544, MakeShadowBiasTerms(near_caster, &terms) && terms.slope == 2.0f && terms.constant == 0.001f);
    CHECK(544, MakeShadowBiasTerms(far_caster, &terms) && terms.slope == 5.0f && terms.constant == 0.005f);
    for (const ShadowCaster& c : {near_caster, far_caster}) {
      ShadowBiasTerms t;
      CHECK(544, MakeShadowBiasTerms(c, &t));
      for (float z : {0.0f, 0.1f, 0.33f, 0.5f, 0.9f}) {
        for (float slope : {0.0f, 0.001f, 0.01f}) {
          const float game = ShadowBias(ShadowGameDepth(z), slope, c.bias_scale, c.bias_offset);
          CHECK(544, std::fabs(ShadowMapDepth(z, slope, t) - ShadowNativeDepth(game)) < 1e-6f);
          // A caster moves away from the light: a larger native depth, a smaller game depth.
          CHECK(544, ShadowMapDepth(z, slope, t) > z && game < ShadowGameDepth(z));
        }
      }
    }
    // The range ends: the nearest depth gets the bias and nothing less, the farthest stays the clear
    // value (the result is saturated: never past 1, never below 0), a NaN gives 0.
    CHECK(545, MakeShadowBiasTerms(near_caster, &terms));
    CHECK(545, ShadowMapDepth(0.0f, 0.0f, terms) == 0.001f && ShadowMapDepth(1.0f, 0.0f, terms) == 1.0f &&
                   ShadowMapDepth(1.0f, 0.5f, terms) == 1.0f && ShadowMapDepth(0.9995f, 0.0f, terms) == 1.0f);
    {
      const ShadowBiasTerms towards{-2.0f, -0.001f};  // (a pair of the other sign: clamped at the light's end)
      CHECK(545, ShadowMapDepth(0.0f, 0.0f, towards) == 0.0f && ShadowMapDepth(0.0005f, 0.1f, towards) == 0.0f);
      CHECK(545, ShadowMapDepth(nan, 0.0f, terms) == 0.0f && ShadowMapDepth(0.5f, nan, terms) == 0.0f);
    }
    // A caster without the offset has none, whatever its registers hold (they keep stale values).
    {
      ShadowCaster plain = near_caster;
      plain.biased = false;
      plain.bias_scale = nan;
      CHECK(546, MakeShadowBiasTerms(plain, &terms) && terms.slope == 0.0f && terms.constant == 0.0f);
      CHECK(546, ShadowMapDepth(0.0f, 1.0f, terms) == 0.0f && ShadowMapDepth(0.5f, 1.0f, terms) == 0.5f);
      // A biased caster whose pair is not usable has no terms at all (and `out` is left alone).
      terms = ShadowBiasTerms{7.0f, 7.0f};
      for (float bad : {nan, inf, -inf, 2.0f * kShadowBiasScaleMax}) {
        ShadowCaster c = near_caster;
        c.bias_scale = bad;
        CHECK(546, !MakeShadowBiasTerms(c, &terms) && terms.slope == 7.0f && terms.constant == 7.0f);
      }
      for (float bad : {nan, inf, -inf, 1.5f}) {
        ShadowCaster c = near_caster;
        c.bias_offset = bad;
        CHECK(546, !MakeShadowBiasTerms(c, &terms) && terms.constant == 7.0f);
      }
      ShadowCaster edge = near_caster;
      edge.bias_scale = -kShadowBiasScaleMax;
      edge.bias_offset = kShadowBiasOffsetMax;
      CHECK(546, MakeShadowBiasTerms(edge, &terms) && terms.slope == kShadowBiasScaleMax / 16.0f && terms.constant == -1.0f);
    }

    // 547: alpha-to-mask on one sample from the 0x2202 word: alpha >= 1 - offset / 4, the offset of
    // the pixel's parity from the word's top byte. The leaf cards' word: 0.5 at every pixel.
    CHECK(547, ShadowAlphaToMaskOn(0xAA00001Cu) && !ShadowAlphaToMaskOn(0xAA000004u) && !ShadowAlphaToMaskOn(0x0000000Cu));
    for (uint32_t x = 0; x < 4; ++x) {
      for (uint32_t y = 0; y < 4; ++y) {
        CHECK(547, ShadowAlphaToMaskThreshold(0xAA00001Cu, x, y) == 0.5f);
        // The same rule as the scene's with one sample (AlphaToMaskCovers, samples 0), at the threshold.
        for (uint32_t word : {0xAA00001Cu, 0x8700001Cu, 0x0000001Cu, 0xFF00001Cu, 0x1B00001Cu}) {
          const float t = ShadowAlphaToMaskThreshold(word, x, y);
          CHECK(547, AlphaToMaskCovers(uint8_t(word >> 24), x, y, t, 0) &&
                         !AlphaToMaskCovers(uint8_t(word >> 24), x, y, t - 1.0f / 512.0f, 0));
        }
      }
    }
    // The dither pattern 0x87: 0.25, 0.75, 1.0, 0.5 for the parities 0 to 3 (index (x & 1) * 2 + (y & 1)).
    CHECK(547, ShadowAlphaToMaskThreshold(0x8700001Cu, 0, 0) == 0.25f && ShadowAlphaToMaskThreshold(0x8700001Cu, 0, 1) == 0.75f &&
                   ShadowAlphaToMaskThreshold(0x8700001Cu, 1, 0) == 1.0f && ShadowAlphaToMaskThreshold(0x8700001Cu, 1, 1) == 0.5f);
    CHECK(547, ShadowAlphaToMaskThreshold(0x0000001Cu, 3, 5) == 1.0f && ShadowAlphaToMaskThreshold(0xFF00001Cu, 2, 2) == 0.25f);

    // 548: the three pipelines, one a cull mode.
    static_assert(kShadowPipelines == 3 && ShadowPipelineSlot(Cull::kNone) == 0 && ShadowPipelineSlot(Cull::kFront) == 1 &&
                  ShadowPipelineSlot(Cull::kBack) == 2);

    // 549-553: what the pass does with a shadow record. The records' words are the rows' of
    // frame-map section 15 and docs/native-renderer/shadow-casters.json.
    auto shadow_record = [&](uint32_t color_control, uint32_t su_sc, float ref) {
      capture::DrawRecord r = BaseRecord();
      r.state_words.depth_control = 0x00708766u;
      r.state_words.color_control = color_control;
      r.state_words.su_sc_mode = su_sc;
      r.state_words.color_mask = 0;
      r.state_words.alpha_ref = FloatBits(ref);
      r.state_words.vport_z_scale = 0xBF800000u;
      r.state_words.vte_cntl = 0x0000043Fu;
      r.state_words.read = true;
      return r;
    };
    auto textured = [](capture::DrawRecord r, float scale) {
      r.material.status = capture::MaterialStatus::kTextured;
      r.material.alpha.source = capture::AlphaSource::kAlbedo;
      r.material.alpha.component = 3;
      r.material.alpha_scale = scale;
      return r;
    };
    ShadowCaster alpha_caster = near_caster;
    alpha_caster.kind = CasterKind::kAlphaTest;
    {
      // An opaque caster (static props, 0xF160...): solid, cull back under clockwise front faces, the near bias.
      const capture::DrawRecord r = shadow_record(0x00000004u, 0x00219806u, 0.0f);
      const ShadowDrawState d = DecideShadowDraw(r, near_caster);
      CHECK(549, d.skip == ShadowDrawSkip::kNone && d.cull == Cull::kBack && !d.textured && d.alpha.mode == 0 &&
                     d.bias.slope == 2.0f && d.bias.constant == 0.001f);
      // (counter-clockwise front faces: front and back trade places, as for the scene)
      CHECK(549, DecideShadowDraw(shadow_record(0x00000004u, 0x00219802u, 0.0f), near_caster).cull == Cull::kFront);
      // An opaque caster is solid whatever its state says of alpha: the game runs no pixel shader there.
      const ShadowDrawState t = DecideShadowDraw(shadow_record(0xAA00001Cu, 0x00219806u, 0.0f), near_caster);
      CHECK(549, t.skip == ShadowDrawSkip::kNone && !t.textured && t.alpha.mode == 0);
    }
    {
      // The leaf cards (0x4325... / 0xF1EB...): 0x2202 = 0xAA00001C, reference 0, no cull, scale c15.w.
      const capture::DrawRecord r = textured(shadow_record(0xAA00001Cu, 0x00219800u, 0.0f), 0.5f);
      const ShadowDrawState d = DecideShadowDraw(r, alpha_caster);
      CHECK(550, d.skip == ShadowDrawSkip::kNone && d.cull == Cull::kNone && d.textured);
      // Alpha test greater than the reference, then alpha-to-mask with the word's offsets as on ONE sample.
      CHECK(550, d.alpha.mode == (kAlphaTest | uint32_t(Compare::kGreater) | kAlphaFromAlbedo | (3u << 5) | kAlphaToMask |
                                  (0xAAu << kAlphaToMaskShift)));
      CHECK(550, d.alpha.ref == 0.0f && d.alpha.scale == 0.5f);
      CHECK(550, ((d.alpha.mode >> kAlphaToMaskSamplesShift) & 3u) == 0);
      // ... also when the record's surface word names two samples (the scene's): the map has one.
      capture::DrawRecord two = r;
      two.state_words.surface_info = 0x11810460u;
      CHECK(550, capture::DecodeRenderState(two.state_words).alpha_to_mask_samples == 1);
      CHECK(550, DecideShadowDraw(two, alpha_caster).alpha.mode == d.alpha.mode);
      // The threshold the mode stands for is the word's: 0.5 at every pixel.
      const uint8_t offsets = uint8_t(d.alpha.mode >> kAlphaToMaskShift);
      for (uint32_t x = 0; x < 2; ++x) {
        for (uint32_t y = 0; y < 2; ++y) {
          CHECK(550, AlphaToMaskCovers(offsets, x, y, 0.5f, 0) && !AlphaToMaskCovers(offsets, x, y, 0.499f, 0) &&
                         ShadowAlphaToMaskThreshold(r.state_words.color_control, x, y) == 0.5f);
        }
      }
    }
    {
      // A cut-out mesh (0x3A0F... / 0x3347...): alpha test greater 254/255, no alpha-to-mask.
      const float ref = 254.0f / 255.0f;
      const ShadowDrawState d = DecideShadowDraw(textured(shadow_record(0x0000000Cu, 0x00219806u, ref), 1.0f), alpha_caster);
      CHECK(551, d.skip == ShadowDrawSkip::kNone && d.textured && d.cull == Cull::kBack &&
                     d.alpha.mode == (kAlphaTest | uint32_t(Compare::kGreater) | kAlphaFromAlbedo | (3u << 5)) && d.alpha.ref == ref &&
                     d.alpha.scale == 1.0f);
      // An alpha-test caster whose state has neither test: solid, and it needs no texture.
      const ShadowDrawState s = DecideShadowDraw(shadow_record(0x00000004u, 0x00219806u, 0.0f), alpha_caster);
      CHECK(551, s.skip == ShadowDrawSkip::kNone && !s.textured && s.alpha.mode == 0);
    }
    {
      // Fail closed. An alpha-test caster without a usable material: not drawn (never solid).
      const capture::DrawRecord good = textured(shadow_record(0xAA00001Cu, 0x00219800u, 0.0f), 1.0f);
      capture::DrawRecord bad = good;
      bad.material.status = capture::MaterialStatus::kPsUnknown;
      CHECK(552, DecideShadowDraw(bad, alpha_caster).skip == ShadowDrawSkip::kAlpha);
      bad = good;
      bad.material.alpha.source = capture::AlphaSource::kNone;
      CHECK(552, DecideShadowDraw(bad, alpha_caster).skip == ShadowDrawSkip::kAlpha);
      for (float v : {nan, inf, -inf, 4096.0f}) {
        bad = good;
        bad.material.alpha_scale = v;
        CHECK(552, DecideShadowDraw(bad, alpha_caster).skip == ShadowDrawSkip::kAlpha);
      }
      for (int i = 0; i < 4; ++i) {
        bad = good;
        bad.material.uv_xform[i] = i % 2 ? nan : inf;
        CHECK(552, DecideShadowDraw(bad, alpha_caster).skip == ShadowDrawSkip::kAlpha);
      }
      // A row that is not finite: not drawn. A cut of +infinity is the record's "no cut" and is fine.
      for (int i : {0, 7, 15}) {
        bad = good;
        bad.rows[i] = i == 7 ? inf : nan;
        CHECK(552, DecideShadowDraw(bad, alpha_caster).skip == ShadowDrawSkip::kMatrix);
      }
      CHECK(552, good.cut[3] == capture::kNoCut && DecideShadowDraw(good, alpha_caster).skip == ShadowDrawSkip::kNone);
      // A bias that is not a number: not drawn (never unbiased instead).
      ShadowCaster no_bias = alpha_caster;
      no_bias.bias_scale = nan;
      CHECK(552, DecideShadowDraw(good, no_bias).skip == ShadowDrawSkip::kBias);
      // A state that was not read, another depth state, a caster kind that is none.
      bad = good;
      bad.state_words.read = false;
      CHECK(552, DecideShadowDraw(bad, alpha_caster).skip == ShadowDrawSkip::kUnread);
      bad = good;
      bad.state_words.depth_control = 0x00708736u;  // less-equal
      CHECK(552, DecideShadowDraw(bad, alpha_caster).skip == ShadowDrawSkip::kState);
      bad = good;
      bad.state_words.vport_z_scale = 0x3F800000u;  // not reversed
      CHECK(552, DecideShadowDraw(bad, alpha_caster).skip == ShadowDrawSkip::kState);
      ShadowCaster odd = alpha_caster;
      odd.kind = CasterKind(7);
      CHECK(552, DecideShadowDraw(good, odd).skip == ShadowDrawSkip::kTwin);
      // A skipped draw carries nothing a caller could draw with.
      const ShadowDrawState s = DecideShadowDraw(bad, alpha_caster);
      CHECK(552, !s.textured && s.alpha.mode == 0 && s.bias.slope == 0.0f && s.bias.constant == 0.0f);
      // (also one that is refused late, after its bias and cull were worked out)
      bad = textured(shadow_record(0xAA00001Cu, 0x00219806u, 0.0f), nan);
      const ShadowDrawState late = DecideShadowDraw(bad, alpha_caster);
      CHECK(552, late.skip == ShadowDrawSkip::kAlpha && late.bias.slope == 0.0f && late.bias.constant == 0.0f && late.cull == Cull::kNone &&
                     !late.textured && late.alpha.mode == 0);
    }
    {
      // 553: the root constants of a shadow draw: a clay draw's without light, the bias in `color`
      // (the constant term's bits) and `pad2` (the slope term's).
      capture::DrawRecord r = textured(shadow_record(0xAA00001Cu, 0x00219800u, 0.0f), 0.5f);
      r.base_vertex = 3;
      const ShadowDrawState d = DecideShadowDraw(r, alpha_caster);
      const float uv[4] = {2.0f, 3.0f, 0.25f, 0.5f};
      const ClayConstants c = MakeShadowConstants(r, 40, uv, true, 2, d.alpha, d.bias);
      CHECK(553, std::memcmp(c.rows, r.rows, sizeof(c.rows)) == 0 && c.base_vertex == 3 && c.vertex_count == 40 && c.textured == 1 &&
                     c.sampler == 2 && c.alpha_mode == d.alpha.mode && c.alpha_ref == 0.0f && c.alpha_scale == 0.5f &&
                     c.uv[0] == 2.0f && c.uv[3] == 0.5f && c.light_mode == 0 && c.light_count == 0);
      CHECK(553, c.color == FloatBits(0.001f) && c.pad2 == FloatBits(2.0f) && c.cut[3] == r.cut[3]);
      const ClayConstants solid = MakeShadowConstants(r, 40, uv, false, 0, ClayAlpha{}, ShadowBiasTerms{});
      CHECK(553, solid.textured == 0 && solid.alpha_mode == 0 && solid.color == 0 && solid.pad2 == 0);
    }

    // 554: the texel a point is drawn into is the texel the registers address. The numbers of
    // tests/test_shadow_matrices.py (light(), map_rows(), HALF), as test_shadow_block.cpp has them:
    // an orthographic light whose origin lies on a whole texel of clip space.
    {
      const float light[4][4] = {{0.052114f, 0.0f, 0.041577f, -6.732423f},
                                 {0.000898f, 0.066651f, -0.001126f, -10.841799f},
                                 {0.005422f, -0.000188f, -0.006796f, 0.737847f},
                                 {0.0f, 0.0f, 0.0f, 1.0f}};
      const float half = 0.5f / 1024.0f;
      float rows[4][4];
      for (int j = 0; j < 4; ++j) {
        rows[0][j] = 0.5f * light[0][j] + (0.5f + half) * light[3][j];
        rows[1][j] = -0.5f * light[1][j] + (0.5f + half) * light[3][j];
        rows[2][j] = light[2][j];
        rows[3][j] = light[3][j];
      }
      auto clip_of = [&](const float p[3], float clip[4]) {
        for (int i = 0; i < 4; ++i) {
          clip[i] = float(double(light[i][0]) * p[0] + double(light[i][1]) * p[1] + double(light[i][2]) * p[2] + double(light[i][3]));
        }
      };
      // The origin: the centre of a texel in the drawing and in the lookup, the same texel.
      const float origin[3] = {0.0f, 0.0f, 0.0f};
      float clip[4];
      double window[2], lookup[2];
      clip_of(origin, clip);
      CHECK(554, ShadowMapWindow(clip, window) && ShadowMapLookup(rows, origin, lookup));
      for (int a = 0; a < 2; ++a) {
        CHECK(554, std::fabs(window[a] - std::floor(window[a]) - 0.5) < 0.004 && std::fabs(lookup[a] - std::floor(lookup[a]) - 0.5) < 0.004);
        CHECK(554, std::floor(window[a]) == std::floor(lookup[a]));
      }
      // (the near map's numbers put it out of the map; a world point inside it, on the same lattice)
      // Points all over the map: the two positions are one (float rows: within 0.002 texel), and
      // the texel is the same wherever the point is not within that of a texel's edge.
      uint32_t seed = 12345u, compared = 0;
      for (int n = 0; n < 2000; ++n) {
        float p[3];
        for (float& v : p) {
          seed = seed * 1664525u + 1013904223u;
          v = float(seed >> 8) / 16777216.0f;
        }
        const float world[3] = {110.0f + 30.0f * p[0], 150.0f + 25.0f * p[1], 20.0f * p[2]};
        clip_of(world, clip);
        CHECK(554, ShadowMapWindow(clip, window) && ShadowMapLookup(rows, world, lookup));
        CHECK(554, std::fabs(window[0] - lookup[0]) < 0.002 && std::fabs(window[1] - lookup[1]) < 0.002);
        const bool inside = window[0] >= 0.0 && window[0] < 1024.0 && window[1] >= 0.0 && window[1] < 1024.0;
        const bool edge = std::fabs(window[0] - std::round(window[0])) < 0.002 || std::fabs(window[1] - std::round(window[1])) < 0.002;
        if (inside && !edge) {
          ++compared;
          CHECK(554, std::floor(window[0]) == std::floor(lookup[0]) && std::floor(window[1]) == std::floor(lookup[1]));
        }
      }
      CHECK(554, compared > 500);  // (the points do fall into the map)
      // Control: a viewport at the target's corner (no origin) would put the origin on a texel's
      // corner, half a texel from where the registers look.
      clip_of(origin, clip);
      CHECK(554, ShadowMapWindow(clip, window) && ShadowMapLookup(rows, origin, lookup));
      CHECK(554, std::fabs((window[0] - double(kShadowViewportOrigin)) - lookup[0] + 0.5) < 0.004);
      // The depth a lookup compares with is the registers' depth row, which is the clip z the map holds.
      CHECK(554, rows[2][3] == light[2][3] && rows[2][0] == light[2][0]);
      // The rows are homogeneous: all four times a factor address the same texel (u and v are divided by w).
      {
        float twice[4][4];
        for (int i = 0; i < 4; ++i) {
          for (int j = 0; j < 4; ++j) twice[i][j] = 2.0f * rows[i][j];
        }
        const float world[3] = {120.0f, 160.0f, 5.0f};
        double a[2], b[2];
        CHECK(554, ShadowMapLookup(rows, world, a) && ShadowMapLookup(twice, world, b) && std::fabs(a[0] - b[0]) < 1e-6 &&
                       std::fabs(a[1] - b[1]) < 1e-6);
      }
      // Nothing that is not finite, and no w of zero, has a place in the map.
      float bad_clip[4] = {0.0f, 0.0f, 0.5f, 0.0f};
      CHECK(554, !ShadowMapWindow(bad_clip, window));
      bad_clip[3] = 1.0f;
      bad_clip[1] = nan;
      CHECK(554, !ShadowMapWindow(bad_clip, window));
      float bad_rows[4][4];
      std::memcpy(bad_rows, rows, sizeof(rows));
      bad_rows[0][2] = inf;
      CHECK(554, !ShadowMapLookup(bad_rows, origin, lookup));
      std::memset(bad_rows, 0, sizeof(bad_rows));
      CHECK(554, !ShadowMapLookup(bad_rows, origin, lookup));
    }

    // 555: the Shadow line.
    {
      ClayStats st;
      // (the line's second half, which case 570 holds: here the pass drew no pre-pass and no mask)
      const std::string tail = " | pre-pass 0 draws, 0 beyond reach | mask white (off) | frames since start 0: shadow-block 0, shadow-view 0, unrotated 0, no-light 0, no-objects 0";
      CHECK(555, FormatShadowText(st) == "Shadow: off");
      st.shadow_switch = st.shadow_block = st.shadow_maps = true;
      st.shadow_reason = capture::ShadowBlockReason::kNone;
      st.shadow_records[0] = 110;
      st.shadow_drawn[0] = 110;
      st.shadow_records[1] = 123;
      st.shadow_drawn[1] = 123;
      st.shadow_passes = 2;
      const float split[4] = {20.0f, 35.0f, 2.0f, 0.477f};
      std::copy(split, split + 4, st.shadow_split);
      CHECK(555, FormatShadowText(st) ==
                     "Shadow: on, block valid, near 110 of 110 draws (skipped {}), far 123 of 123 draws (skipped {}), extra 0, "
                     "split (20, 35, 2, 0.4770)" + tail);
      CHECK(555, FormatShadowSkippedByVs(st).empty());
      // A partial pass under a valid block shows: the pass's reasons, the capture's, the extra draws, the passes.
      st.shadow_drawn[1] = 119;
      st.shadow_skipped[1][size_t(ShadowDrawSkip::kTexture)] = 3;
      st.shadow_skipped[1][size_t(ShadowDrawSkip::kGeometry)] = 1;
      st.shadow_capture_skipped[1][size_t(capture::ShadowSkip::kNoCaster)] = 2;
      st.shadow_capture_skipped[0][size_t(capture::SkipReason::kBadIndex)] = 1;
      st.shadow_extra = 14;
      st.shadow_passes = 3;
      st.shadow_skipped_by_vs.Add(0x432563420047C96Cull, uint8_t(ShadowDrawSkip::kTexture));
      st.shadow_skipped_by_vs.Add(0x432563420047C96Cull, uint8_t(ShadowDrawSkip::kTexture));
      st.shadow_skipped_by_vs.Add(0x3A0F9098B839DDBCull, uint8_t(ShadowDrawSkip::kTexture));
      st.shadow_skipped_by_vs.Add(0xF160B4DA459A6D40ull, uint8_t(ShadowDrawSkip::kGeometry));
      CHECK(555, FormatShadowText(st) ==
                     "Shadow: on, block valid, near 110 of 110 draws (skipped {capture bad-index: 1}), far 119 of 123 draws (skipped "
                     "{geometry: 1, texture: 3, capture no-caster: 2}), extra 14 (shadow-pass-extra), passes 3, split (20, 35, 2, 0.4770)" + tail);
      CHECK(555, FormatShadowSkippedByVs(st) ==
                     "0xF160B4DA459A6D40(geometry): 1, 0x432563420047C96C(texture): 2, 0x3A0F9098B839DDBC(texture): 1");
      // No block: the reason, the records the scene holds, nothing drawn.
      ClayStats none;
      none.shadow_switch = true;
      none.shadow_reason = capture::ShadowBlockReason::kProof;
      none.shadow_records[0] = 5;
      none.shadow_passes = 2;
      CHECK(555, FormatShadowText(none) ==
                     "Shadow: on, block invalid (shadow-block: proof), near 0 of 5 draws (skipped {}), far 0 of 0 draws (skipped {}), extra 0" + tail);
      // A valid block whose maps were not drawn says so.
      ClayStats dark = st;
      dark.shadow_maps = false;
      dark.shadow_light_off = true;
      CHECK(555, FormatShadowText(dark).ends_with("split (20, 35, 2, 0.4770) | maps not drawn: the frame has no light" + tail));
      // Every reason has a name.
      for (size_t i = 0; i < size_t(ShadowDrawSkip::kCount); ++i) CHECK(555, std::strcmp(ShadowDrawSkipName(ShadowDrawSkip(i)), "?") != 0);
      CHECK(555, std::strcmp(ShadowDrawSkipName(ShadowDrawSkip::kCount), "?") == 0);
    }

    // 560-572: the shadowed sun (plan Task 5 of the shadows): the binding table with the mask, the
    // pre-pass keys, the compare directions on the native maps, the mask's mirrors against the
    // reference's numbers (tests/test_light_check.py, MaskPinnedForTheRenderer, has the same inputs
    // and the same literals; out/tests/task5/mask_pins.py made them with light_formulas.py), the
    // frame's inverse view-projection, the mask constants, the Shadow line.
    {
      const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
      // 560: the albedo's parameter is the pair (albedo t3, mask t4); the terrain table and the
      // light's four buffers moved up by one register; still twelve parameters and 56 root words.
      {
        const ClayBinding& pair = kClayBindings[kClayAlbedoParam];
        CHECK(560, kClayAlbedoParam == 4 && pair.kind == ClayBindingKind::kTextures && pair.reg == 3 && pair.count == 2 &&
                       pair.stage == ClayBindingStage::kPixel && kClayMaskRegister == 4);
        const ClayBinding& terrain = kClayBindings[5];
        CHECK(560, terrain.kind == ClayBindingKind::kTextures && terrain.reg == 5 && terrain.count == 4 &&
                       terrain.stage == ClayBindingStage::kPixel);
        CHECK(560, kClayBindings[6].kind == ClayBindingKind::kConstantBuffer && kClayBindings[6].reg == 1 &&
                       kClayBindings[7].kind == ClayBindingKind::kConstantBuffer && kClayBindings[7].reg == 2);
        for (uint32_t i = 0; i < kClayLightBufferCount; ++i) CHECK(560, kClayBindings[kClayLightBufferParam0 + i].reg == 9 + i);
        CHECK(560, std::size(kClayBindings) == 12 && ClayRootWords() == 56);
        // No two parameters a stage sees share a texture register.
        for (size_t a = 0; a < std::size(kClayBindings); ++a) {
          for (size_t b = a + 1; b < std::size(kClayBindings); ++b) {
            const ClayBinding& x = kClayBindings[a];
            const ClayBinding& y = kClayBindings[b];
            const bool tx = x.kind == ClayBindingKind::kBuffer || x.kind == ClayBindingKind::kTextures;
            const bool ty = y.kind == ClayBindingKind::kBuffer || y.kind == ClayBindingKind::kTextures;
            if (!tx || !ty) continue;
            const uint32_t x1 = x.reg + (x.kind == ClayBindingKind::kTextures ? x.count : 1u);
            const uint32_t y1 = y.reg + (y.kind == ClayBindingKind::kTextures ? y.count : 1u);
            CHECK(560, x1 <= y.reg || y1 <= x.reg);
          }
        }
      }
      // 561: where a lit pixel reads the mask: 1 / the target's size, in a lit frame only.
      {
        ClayFrameConstants frame{};
        SetClayFrameMask(frame, 1120, 720);
        CHECK(561, frame.mask_du == 0.0f && frame.mask_dv == 0.0f);  // (no light: the block stays zero)
        frame.light_on = 1;
        SetClayFrameMask(frame, 0, 720);
        CHECK(561, frame.mask_du == 0.0f && frame.mask_dv == 0.0f);
        SetClayFrameMask(frame, 1120, 720);
        CHECK(561, frame.mask_du == 1.0f / 1120.0f && frame.mask_dv == 1.0f / 720.0f);
        SetClayFrameMask(frame, 2240, 1440);  // (nothing assumes the game's size)
        CHECK(561, frame.mask_du == 1.0f / 2240.0f && frame.mask_dv == 1.0f / 1440.0f);
        CHECK(561, offsetof(ClayFrameConstants, mask_du) == 76 && offsetof(ClayFrameConstants, mask_dv) == 92 &&
                       kClayFrameLightBytes == 112);
        // The pixel's centre times the two is the centre of the mask's texel: (i + 0.5) / width.
        CHECK(561, std::fabs((559.0f + 0.5f) * frame.mask_du * 2240.0f - 559.5f) < 1e-3f);
      }
      // 562: the pre-pass: records whose colour pipeline tests and writes depth; the key keeps the
      // shader kind, the function and the cull, writes no colour and has no blend. 36 keys at most.
      {
        PipelineKey colour;
        colour.textured = kTexturedTerrainLayer;
        colour.blend = 1;
        colour.src = uint8_t(capture::Factor::kSrcAlpha);
        colour.dst = uint8_t(capture::Factor::kInvSrcAlpha);
        colour.depth_func = uint8_t(capture::Compare::kLessEqual);
        colour.cull = uint8_t(capture::Cull::kBack);
        CHECK(562, InPrepass(colour));
        const PipelineKey k = PrepassKey(colour);
        const PipelineKey plain;
        CHECK(562, k.textured == kTexturedTerrainLayer && k.color_mask == 0 && k.blend == 0 && k.src == plain.src && k.dst == plain.dst &&
                       k.depth_test == 1 && k.depth_write == 1 && k.depth_func == uint8_t(capture::Compare::kLessEqual) &&
                       k.cull == uint8_t(capture::Cull::kBack));
        PipelineKey no_write = colour;
        no_write.depth_write = 0;
        PipelineKey no_test = colour;
        no_test.depth_test = 0;
        CHECK(562, !InPrepass(no_write) && !InPrepass(no_test) && InPrepass(PipelineKey{}));
        // Two colour keys that differ in blend or colour mask alone share a pre-pass key.
        PipelineKey other = colour;
        other.blend = 0;
        other.color_mask = 0x7;
        CHECK(562, PrepassKey(other) == k);
        std::vector<PipelineKey> keys;
        for (uint8_t textured = 0; textured < kClayPixelShaders; ++textured) {
          for (capture::Compare f : {capture::Compare::kAlways, capture::Compare::kLess, capture::Compare::kLessEqual}) {
            for (capture::Cull c : {capture::Cull::kNone, capture::Cull::kFront, capture::Cull::kBack}) {
              PipelineKey key;
              key.textured = textured;
              key.depth_func = uint8_t(f);
              key.cull = uint8_t(c);
              for (uint8_t blend = 0; blend < 2; ++blend) {
                key.blend = blend;
                CHECK(562, FindOrAddPipelineSlot(keys, PrepassKey(key), kMaxPrepassPipelines) != kNoPipelineSlot);
              }
            }
          }
        }
        CHECK(562, keys.size() == 36 && kMaxPrepassPipelines == 36);
      }
      // 563: the compare directions. The game: lit iff 1 - z > g (near), 1 - z >= g (far), g the map's
      // value 1 - z_caster. Native map: n = z_caster: lit iff z < n (near), z <= n (far).
      {
        CHECK(563, MaskNearLit(0.25, 0.75) && !MaskNearLit(0.75, 0.25) && !MaskNearLit(0.5, 0.5));
        CHECK(563, MaskFarLit(0.25, 0.75) && !MaskFarLit(0.75, 0.25) && MaskFarLit(0.5, 0.5));
        // The clear value (no caster) lights every receiver inside the range; a NaN lights none.
        CHECK(563, MaskNearLit(0.999, double(kShadowMapClear)) && MaskFarLit(1.0, double(kShadowMapClear)) && !MaskNearLit(0.5, double(nan)) &&
                       !MaskFarLit(0.5, double(nan)));
        // The same verdicts as the game's on the game's values, for depths a float holds exactly.
        for (float z : {0.125f, 0.25f, 0.5f, 0.75f}) {
          for (float caster : {0.125f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float g = ShadowGameDepth(caster);  // what the game's map holds
            CHECK(563, MaskNearLit(z, caster) == (1.0f - z > g) && MaskFarLit(z, caster) == (1.0f - z >= g));
          }
        }
      }
      // The reference's inputs: frame 1 of the bridge capture of 2026-10-09 (the view-projection's
      // inverse, the scene's c36..c43, c12, c19), every number single precision.
      const float inv_view_proj[4][4] = {{0.5143311619758606f, 0.08813292533159256f, -820.677490234375f, 821.5447387695312f},
                                         {-0.8844040036201477f, 0.05125229433178902f, -1573.3095703125f, 1573.8358154296875f},
                                         {8.367820555577055e-06f, 0.5663930773735046f, -501.68060302734375f, 501.51348876953125f},
                                         {1.6652556666940654e-07f, 7.833264703549503e-09f, -9.999471664428711f, 9.999671936035156f}};
      const float view_proj[4][4] = {{0.49135205149650574f, -0.8449383974075317f, 2.1847014863851655e-08f, 92.6156997680664f},
                                     {0.26610419154167175f, 0.15474604070186615f, 1.7101504802703857f, -131.98681640625f},
                                     {0.8508029580116272f, 0.49476245045661926f, -0.17715677618980408f, -138.88465881347656f},
                                     {0.8507859110832214f, 0.49475255608558655f, -0.17715322971343994f, -138.78187561035156f}};
      capture::ShadowBlock block{};
      {
        const float near_rows[4][4] = {{0.02583850733935833f, 0.0f, 0.021059028804302216f, -2.8588876724243164f},
                                       {-0.0006741421530023217f, -0.03331625089049339f, 0.0008271429105661809f, 5.92822265625f},
                                       {0.005491321906447411f, -0.0002783896343316883f, -0.006737612187862396f, 0.743424117565155f},
                                       {0.0f, 0.0f, 0.0f, 1.0f}};
        const float far_rows[4][4] = {{0.011073646135628223f, 0.0f, 0.009025298990309238f, -1.05615234375f},
                                      {-0.0002889182942453772f, -0.014278394170105457f, 0.0003544900973793119f, 2.937011480331421f},
                                      {0.004074114840477705f, -0.00020654266700148582f, -0.004998760763555765f, 0.6168575882911682f},
                                      {0.0f, 0.0f, 0.0f, 1.0f}};
        std::memcpy(block.world_to_map[0], near_rows, sizeof(near_rows));
        std::memcpy(block.world_to_map[1], far_rows, sizeof(far_rows));
        block.split[0] = 20.0f;
        block.split[1] = 35.0f;
        block.split[2] = 2.0f;
        block.split[3] = 0.48273685574531555f;
        block.texel[0] = block.texel[1] = 1.0f / 1024.0f;
        block.valid = true;
      }
      const float eye[3] = {82.07209777832031f, 157.33926391601562f, 50.17070388793945f};
      // 563 (continued): one world point as a caster and as a receiver, under the capture's near map.
      // Drawn (plan Task 4): the pass's light matrix (the registers' rows without their texture-space
      // mapping) puts it in a texel and the shader stores its depth with the game's bias. Looked up:
      // the registers' rows put it in the same texel with the same depth. With the bias it is lit by
      // both rules; without one the near rule shadows it and the far rule does not; a receiver behind
      // it is shadowed, one in front of it lit.
      {
        float rows[4][4], light[4][4];
        std::memcpy(rows, block.world_to_map[0], sizeof(rows));
        const float half = 0.5f / 1024.0f;
        for (int j = 0; j < 4; ++j) {
          light[0][j] = 2.0f * (rows[0][j] - (0.5f + half) * rows[3][j]);
          light[1][j] = -2.0f * (rows[1][j] - (0.5f + half) * rows[3][j]);
          light[2][j] = rows[2][j];
          light[3][j] = rows[3][j];
        }
        const float world[3] = {84.5f, 150.25f, 44.0f};
        float clip[4];
        for (int i = 0; i < 4; ++i) clip[i] = light[i][0] * world[0] + light[i][1] * world[1] + light[i][2] * world[2] + light[i][3];
        double window[2], lookup[2];
        CHECK(563, ShadowMapWindow(clip, window) && ShadowMapLookup(rows, world, lookup));
        CHECK(563, std::floor(window[0]) == std::floor(lookup[0]) && std::floor(window[1]) == std::floor(lookup[1]) &&
                       window[0] > 0.0 && window[0] < 1024.0 && window[1] > 0.0 && window[1] < 1024.0);
        const double P[4] = {world[0], world[1], world[2], 1.0};
        double uvz[3];
        CHECK(563, MaskProject(rows, P, uvz) && std::fabs(uvz[0] * 1024.0 - lookup[0]) < 1e-6 && std::fabs(uvz[1] * 1024.0 - lookup[1]) < 1e-6 &&
                       std::fabs(window[0] - lookup[0]) < 0.01 && std::fabs(window[1] - lookup[1]) < 0.01);
        const float z = clip[2] / clip[3];
        CHECK(563, std::fabs(uvz[2] - double(z)) < 1e-6 && z > 0.1f && z < 0.9f);
        ShadowCaster near_caster;
        near_caster.biased = true;
        near_caster.bias_scale = -32.0f;
        near_caster.bias_offset = -0.001f;
        ShadowBiasTerms terms, none;
        CHECK(563, MakeShadowBiasTerms(near_caster, &terms));
        const float slope = 0.0005f;
        const float stored = ShadowMapDepth(z, slope, terms);
        // (the same value by the game's own offset on the game's window depth)
        CHECK(563, std::fabs(stored - ShadowNativeDepth(ShadowBias(ShadowGameDepth(z), slope, -32.0f, -0.001f))) < 1e-6f && stored > z);
        CHECK(563, MaskNearLit(uvz[2], stored) && MaskFarLit(uvz[2], stored));
        const float unbiased = ShadowMapDepth(z, slope, none);
        CHECK(563, unbiased == z && !MaskNearLit(double(z), unbiased) && MaskFarLit(double(z), unbiased));
        CHECK(563, !MaskNearLit(uvz[2] + 0.01, stored) && !MaskFarLit(uvz[2] + 0.01, stored) && MaskNearLit(uvz[2] - 0.01, stored));
        // A whole map of that caster: the near term of the point is 1 (all twelve taps lit), of a point
        // 0.01 behind it 0; the far term likewise.
        const double rot[2] = {0.6, 0.8};
        auto flat = [&](int, int) { return stored; };
        CHECK(563, std::fabs(MaskNear(P, rows, flat, rot, double(kMaskNearRadius), kShadowMapSize) - 12.0 * double(kMaskTapWeight)) < 1e-12);
        float behind[4][4];
        std::memcpy(behind, rows, sizeof(rows));
        behind[2][3] += 0.01f;
        CHECK(563, MaskNear(P, behind, flat, rot, double(kMaskNearRadius), kShadowMapSize) == 0.0 &&
                       MaskFar(P, behind, flat, kShadowMapSize) == 0.0 && MaskFar(P, rows, flat, kShadowMapSize) == 1.0);
      }
      // The synthetic maps and tile of the reference (a value per texel that a float holds exactly).
      auto near_texel = [](int ix, int iy) { return float((ix * 37 + iy * 101) % 129) / 128.0f; };
      auto far_texel = [](int ix, int iy) { return float((ix * 53 + iy * 17 + 5) % 129) / 128.0f; };
      auto rot_texel = [](uint32_t tx, uint32_t ty, float out[2]) {
        out[0] = float((tx * 73 + ty * 29) % 256) / 255.0f;
        out[1] = float((tx * 31 + ty * 57) % 256) / 255.0f;
      };
      // 564: the two terms and the fade (mask_combine).
      {
        const struct {
          float d, near_term, far_term;
          double want;
        } pins[] = {{10.0f, 0.25f, 0.75f, 0.25},  {21.0f, 0.25f, 0.75f, 0.5},   {30.0f, 0.25f, 0.75f, 0.75},
                    {40.0f, 0.25f, 0.75f, 0.6163684258813922}, {50.0f, 0.25f, 0.75f, 0.48273685574531555},
                    {20.5f, 1.0f, 0.0f, 0.75},    {36.5f, 0.0f, 1.0f, 0.9224105272056241}};
        for (const auto& p : pins) CHECK(564, std::fabs(MaskCombine(p.d, p.near_term, p.far_term, block.split, kMaskFadeSlope) - p.want) < 1e-12);
        // Beyond the shadow distance the value is split.w, not 1.
        CHECK(564, MaskCombine(1000.0, 1.0, 1.0, block.split, kMaskFadeSlope) == double(block.split[3]) && block.split[3] < 0.5f);
        // A blend width that is not positive is a step at split.x; values that are no numbers give a number.
        const float step[4] = {20.0f, 35.0f, 0.0f, 0.5f};
        CHECK(564, MaskCombine(19.0, 0.25, 0.75, step, 0.1f) == 0.25 && MaskCombine(21.0, 0.25, 0.75, step, 0.1f) == 0.75);
        for (float bad : {0.0f, nan, inf, -inf}) {
          for (int at = 0; at < 4; ++at) {
            float s[4] = {20.0f, 35.0f, 2.0f, 0.5f};
            s[at] = bad;
            for (double d : {0.0, 21.0, double(nan), double(inf), double(-inf)}) {
              CHECK(564, std::isfinite(MaskCombine(d, 0.25, 0.75, s, 0.1f)) && std::isfinite(MaskCombine(d, double(bad), double(bad), s, bad)));
            }
          }
        }
        CHECK(564, kMaskTapWeight == 0.0833333358f && kMaskFarReach == 10.0f && kMaskFadeSlope == 0.1f && kMaskNearTapCount == 12 &&
                       kMaskNearRadius == 2.5f / 1024.0f && kMaskNearTaps[0][0] == -0.203345001f && kMaskNearTaps[11][1] == -0.932614982f &&
                       kMaskNearTaps[2][0] == 0.962339997f && kMaskNearTaps[2][1] == -0.194983006f);
      }
      // 565: the near and the far term of a world point under the world rows.
      {
        const struct {
          double P[4];
          double near_want, far_want;
        } pins[] = {{{84.5, 150.25, 44.0, 1.0}, 0.0833333358168602, 0.3243241310119629},
                    {{90.0, 140.0, 43.5, 1.0}, 0.0833333358168602, 0.6628598777695416},
                    {{70.125, 165.5, 46.0, 1.0}, 0.0833333358168602, 0.0},
                    {{100.0, 120.0, 40.0, 1.0}, 0.0, 0.8823890686035156}};
        const double rot[2] = {double(0.6f), double(0.8f)};
        for (const auto& p : pins) {
          CHECK(565, std::fabs(MaskNear(p.P, block.world_to_map[0], near_texel, rot, double(kMaskNearRadius), kShadowMapSize) - p.near_want) < 1e-12);
          CHECK(565, std::fabs(MaskFar(p.P, block.world_to_map[1], far_texel, kShadowMapSize) - p.far_want) < 1e-9);
        }
        // Constant maps (tests/test_light_check.py, MaskFormulas, in the native direction): a map of the
        // clear value lights all twelve taps, a map of 0 (a caster at the light) none.
        const float identity[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
        const double point[4] = {0.3, 0.6, 0.25, 1.0};
        const double unrotated[2] = {1.0, 0.0};
        auto constant = [](float v) { return [v](int, int) { return v; }; };
        CHECK(565, std::fabs(MaskNear(point, identity, constant(1.0f), unrotated, double(kMaskNearRadius), 1024) - 12.0 * double(kMaskTapWeight)) < 1e-12 &&
                       MaskNear(point, identity, constant(0.0f), unrotated, double(kMaskNearRadius), 1024) == 0.0);
        // A map that holds the point's own depth: shadow for the near map (strict), light for the far one.
        CHECK(565, MaskNear(point, identity, constant(0.25f), unrotated, double(kMaskNearRadius), 1024) == 0.0 &&
                       MaskFar(point, identity, constant(0.25f), 1024) == 1.0);
        // The far term's four texels and weights on an 8 x 8 map: x = 2.5, y = 3.25: texels 1, 2 by 2, 3.
        const double far_point[4] = {0.3125, 0.40625, 0.25, 1.0};
        const struct {
          int ix, iy;
          double weight;
        } texels[] = {{1, 2, 0.5 * 0.75}, {2, 2, 0.5 * 0.75}, {1, 3, 0.5 * 0.25}, {2, 3, 0.5 * 0.25}};
        for (const auto& t : texels) {
          int fetched = 0;
          auto one = [&](int ix, int iy) {
            ++fetched;
            return ix == t.ix && iy == t.iy ? 0.9f : 0.1f;
          };
          CHECK(565, std::fabs(MaskFar(far_point, identity, one, 8) - t.weight) < 1e-12 && fetched == 4);
        }
        // The rotation: (x, y) -> (a x + b y, a y - b x).
        const float offset[2] = {0.5f, -2.0f};
        const double turn[2] = {0.6, 0.8};
        double turned[2];
        MaskRotate(offset, turn, turned);
        CHECK(565, std::fabs(turned[0] + 1.3) < 1e-12 && std::fabs(turned[1] + 1.6) < 1e-12);
        // A far point whose texel is no number an integer holds: 1, and no fetch (the reference returns 1 there).
        {
          int fetched = 0;
          auto counted = [&](int, int) {
            ++fetched;
            return 0.0f;
          };
          const double huge[4] = {3.0e6, 0.5, 0.25, 1.0}, huge_v[4] = {0.5, -3.0e6, 0.25, 1.0};
          CHECK(565, MaskFar(huge, identity, counted, 1024) == 1.0 && MaskFar(huge_v, identity, counted, 1024) == 1.0 && fetched == 0);
          const double inside[4] = {0.5, 0.5, 0.25, 1.0};
          CHECK(565, MaskFar(inside, identity, counted, 1024) == 0.0 && fetched == 4);
        }
        // Clamp addressing: a tap outside the map reads the edge texel; the index of no number is 0.
        CHECK(565, MaskTexelIndex(-3.5, 1024) == 0 && MaskTexelIndex(1023.9, 1024) == 1023 && MaskTexelIndex(5000.0, 1024) == 1023 &&
                       MaskTexelIndex(17.99, 1024) == 17 && MaskTexelIndex(double(nan), 1024) == 0 && MaskTexelIndex(1e300, 1024) == 1023);
      }
      // 566: the mask of a pixel (mask_0f99), with and without the rotation tile, in every regime:
      // near only, the blend, far only, the fade, beyond, and the clear depth.
      ClayMaskConstants constants{};
      CHECK(566, MakeClayMaskConstants(block, inv_view_proj, eye, 1120, 720, true, &constants));
      {
        ClayMaskConstants plain = constants;
        plain.rotate = 0;
        const struct {
          uint32_t px, py;
          float depth;
          double d, rotated, unrotated;
        } pins[] = {{560, 600, 0.9821628332138062f, 5.999995277693097, 0.1666666716337204, 0.1666666716337204},
                    {300, 500, 0.9909855127334595f, 12.500029722610465, 0.0833333358168602, 0.1666666716337204},
                    {700, 420, 0.994562029838562f, 18.9999053127623, 0.0, 0.0},
                    {640, 400, 0.9950790405273438f, 20.499951274353368, 0.168626222122592, 0.10612419002461002},
                    {200, 380, 0.9944530129432678f, 21.500103049226347, 0.30675134735710674, 0.30675134735710674},
                    {900, 350, 0.9956575036048889f, 27.000158411112753, 0.0, 0.0},
                    {500, 340, 0.9970599412918091f, 34.00031121842236, 0.9111211026206659, 0.9111211026206659},
                    {333, 333, 0.9971696734428406f, 37.99976545013204, 0.183352533656075, 0.183352533656075},
                    {800, 300, 0.9975280165672302f, 44.00000617981933, 0.4344634749674494, 0.4344634749674494},
                    {100, 250, 0.9978243708610535f, 60.000769539675034, 0.48273685574531555, 0.48273685574531555},
                    {17, 5, 1.0f, 7584.099111323902, 0.48273685574531555, 0.48273685574531555},
                    {1119, 719, 1.0f, 7689.370675434363, 0.48273685574531555, 0.48273685574531555}};
        int differ = 0;
        for (const auto& p : pins) {
          int tile_reads = 0;
          auto counted = [&](uint32_t tx, uint32_t ty, float out[2]) {
            ++tile_reads;
            rot_texel(tx, ty, out);
          };
          const double with = MaskPixel(p.px, p.py, p.depth, constants, near_texel, far_texel, counted);
          CHECK(566, std::fabs(with - p.rotated) < 1e-9);
          // The tile is read once where the near map is read (d < 22) and nowhere else.
          CHECK(566, tile_reads == (p.d < 22.0 ? 1 : 0));
          tile_reads = 0;
          const double without = MaskPixel(p.px, p.py, p.depth, plain, near_texel, far_texel, counted);
          CHECK(566, std::fabs(without - p.unrotated) < 1e-9 && tile_reads == 0);
          if (std::fabs(with - without) > 1e-6) ++differ;
          // The world position the mirror forms is at the reference's distance.
          const double S[4] = {(2.0 * p.px + 1.0) * double(1.0f / 1120.0f) - 1.0, 1.0 - (2.0 * p.py + 1.0) * double(1.0f / 720.0f),
                               double(p.depth), 1.0};
          double world[3];
          CHECK(566, MaskProject(constants.inv_view_proj, S, world));
          const double d = std::sqrt((world[0] - eye[0]) * (world[0] - eye[0]) + (world[1] - eye[1]) * (world[1] - eye[1]) +
                                     (world[2] - eye[2]) * (world[2] - eye[2]));
          CHECK(566, std::fabs(d - p.d) < 1e-9 * p.d);
        }
        CHECK(566, differ == 2);  // (the rotation shows where the taps straddle texels)
        // The clear depth is a far pixel: split.w, the game's value there (not 1).
        CHECK(566, MaskPixel(0, 0, 1.0f, constants, near_texel, far_texel, rot_texel) == double(block.split[3]));
        // The tile is addressed in the game's pixel units: on a target of twice the size two native
        // pixels share a texel, and the tile repeats every 32 game pixels.
        CHECK(566, MaskTileTexel(0, 1.0f) == 0 && MaskTileTexel(31, 1.0f) == 31 && MaskTileTexel(32, 1.0f) == 0 && MaskTileTexel(1119, 1.0f) == 31 &&
                       MaskTileTexel(0, 0.5f) == 0 && MaskTileTexel(1, 0.5f) == 0 && MaskTileTexel(2, 0.5f) == 1 && MaskTileTexel(64, 0.5f) == 0 &&
                       MaskTileTexel(5, nan) == 0 && MaskTileTexel(5, inf) == 0 && MaskTileTexel(5, -1.0f) == 0);
        // On a target of another size the same screen point gives the same mask (normalised
        // addressing): pixel (2 px, 2 py) and its three neighbours of a 2240 x 1440 target surround
        // the centre of pixel (px, py); the world positions differ by a quarter pixel only.
        ClayMaskConstants twice{};
        CHECK(566, MakeClayMaskConstants(block, inv_view_proj, eye, 2240, 1440, false, &twice));
        CHECK(566, twice.inv_target[0] == 1.0f / 2240.0f && twice.tile_scale[0] == 0.5f && twice.tile_scale[1] == 0.5f &&
                       constants.tile_scale[0] == 1.0f && constants.tile_scale[1] == 1.0f && constants.radius == kMaskNearRadius &&
                       constants.map_size == 1024.0f && constants.rotate == 1 && twice.rotate == 0);
        CHECK(566, std::fabs(MaskPixel(1000, 500, 0.9978243708610535f, twice, near_texel, far_texel, rot_texel) - double(block.split[3])) < 1e-12);
      }
      // 567: guards (review focus 2). Zeros and values that are no numbers: the mask is a number in
      // [0, 1] for any depth; a pixel without a world position gets split.w.
      {
        for (float depth : {0.0f, 0.5f, 1.0f, 2.0f, -1.0f, nan, inf, -inf}) {
          const double m = MaskPixel(560, 360, depth, constants, near_texel, far_texel, rot_texel);
          CHECK(567, std::isfinite(m) && m >= 0.0 && m <= 1.0000001);
        }
        ClayMaskConstants zero{};
        CHECK(567, MaskPixel(3, 4, 0.5f, zero, near_texel, far_texel, rot_texel) == 0.0);  // (no position: split.w, which is 0 there)
        for (float bad : {0.0f, nan, inf, -inf}) {
          for (int what = 0; what < 6; ++what) {
            ClayMaskConstants c = constants;
            float* fields[] = {&c.inv_view_proj[3][2], &c.near_rows[3][3], &c.far_rows[2][1], &c.eye[1], &c.split[2], &c.radius};
            *fields[what] = bad;
            for (float depth : {0.9821628332138062f, 0.9956575036048889f, 1.0f}) {
              auto bad_texel = [&](int, int) { return bad; };
              const double m = MaskPixel(560, 600, depth, c, bad_texel, bad_texel, rot_texel);
              CHECK(567, std::isfinite(m) && m >= 0.0 && m <= 1.0000001);
            }
          }
          ClayMaskConstants c = constants;
          for (auto& row : c.inv_view_proj) {
            for (float& v : row) v = bad;
          }
          CHECK(567, MaskPixel(560, 600, 0.99f, c, near_texel, far_texel, rot_texel) == double(block.split[3]));
          // Rows that cannot project give 1 for their map (no shadow from a map that cannot be addressed), without a fetch.
          float rows[4][4] = {};
          rows[3][3] = bad;
          const double P[4] = {1.0, 2.0, 3.0, 1.0};
          const double rot[2] = {1.0, 0.0};
          int fetched = 0;
          auto never = [&](int, int) {
            ++fetched;
            return 0.0f;
          };
          if (bad == 0.0f || !std::isfinite(bad)) {
            CHECK(567, MaskNear(P, rows, never, rot, 0.01, 1024) == 1.0 && MaskFar(P, rows, never, 1024) == 1.0 && fetched == 0);
          }
          // A rotation or radius that is no number: the taps count as lit; a number comes out.
          const double bad_rot[2] = {double(bad), double(bad)};
          const double v = MaskNear(P, block.world_to_map[0], near_texel, bad_rot, double(bad), 1024);
          CHECK(567, std::isfinite(v) && v >= 0.0 && v <= 1.0001);
        }
        // The constants are refused, untouched, for a block that is not valid, a target without a
        // size, a matrix entry or split that is no number, an eye that is no position.
        ClayMaskConstants out{};
        const ClayMaskConstants before = out;
        capture::ShadowBlock invalid = block;
        invalid.valid = false;
        CHECK(567, !MakeClayMaskConstants(invalid, inv_view_proj, eye, 1120, 720, true, &out) &&
                       !MakeClayMaskConstants(block, inv_view_proj, eye, 0, 720, true, &out) &&
                       !MakeClayMaskConstants(block, inv_view_proj, eye, 1120, 0, true, &out) &&
                       !MakeClayMaskConstants(block, nullptr, eye, 1120, 720, true, &out));
        for (float bad : {nan, inf, -inf}) {
          float m[4][4];
          std::memcpy(m, inv_view_proj, sizeof(m));
          m[2][1] = bad;
          CHECK(567, !MakeClayMaskConstants(block, m, eye, 1120, 720, true, &out));
          capture::ShadowBlock b = block;
          b.world_to_map[1][0][3] = bad;
          CHECK(567, !MakeClayMaskConstants(b, inv_view_proj, eye, 1120, 720, true, &out));
          b = block;
          b.split[3] = bad;
          CHECK(567, !MakeClayMaskConstants(b, inv_view_proj, eye, 1120, 720, true, &out));
          const float bad_eye[3] = {eye[0], bad, eye[2]};
          CHECK(567, !MakeClayMaskConstants(block, inv_view_proj, bad_eye, 1120, 720, true, &out));
        }
        const float far_eye[3] = {eye[0], 2.0f * kLightPositionMax, eye[2]};
        CHECK(567, !MakeClayMaskConstants(block, inv_view_proj, far_eye, 1120, 720, true, &out) && std::memcmp(&out, &before, sizeof(out)) == 0);
      }
      // 568: the frame's inverse view-projection, from the records whose rows are world to clip (a
      // terrain record, an instanced record) and from a leaf's rows and world rows.
      {
        auto record = [&](const float m[4][4]) {
          capture::DrawRecord r = BaseRecord();
          for (int i = 0; i < 16; ++i) r.rows[i] = m[i / 4][i % 4];
          return r;
        };
        FrameScene scene;
        float inverse[4][4], back[4][4];
        FrameView info;
        // No record that gives a matrix: no view (an ordinary mesh has no world translation beside it).
        scene.draws.push_back(record(view_proj));
        CHECK(568, !FrameInverseViewProj(scene, inverse, &info) && info.why == FrameViewReason::kNoRecord && info.candidates == 0);
        // A terrain record: its rows.
        scene.draws.push_back(record(view_proj));
        scene.draws.back().terrain_index = 0;
        scene.terrain.push_back(capture::TerrainPatch{});
        CHECK(568, FrameInverseViewProj(scene, inverse, &info, back) && info.why == FrameViewReason::kNone && info.candidates == 1);
        CHECK(568, std::memcmp(back, view_proj, sizeof(back)) == 0);
        for (int i = 0; i < 4; ++i) {
          for (int j = 0; j < 4; ++j) {
            // (the reference's inverse, made in double by other code: equal to a float's precision)
            CHECK(568, std::fabs(inverse[i][j] - inv_view_proj[i][j]) <= 2e-6f * std::fabs(inv_view_proj[i][j]) + 1e-12f);
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) sum += double(view_proj[i][k]) * double(inverse[k][j]);
            CHECK(568, std::fabs(sum - (i == j ? 1.0 : 0.0)) < 2e-3);
          }
        }
        // An instanced record with the same rows, in the other layout (its rows are the transpose).
        capture::DrawRecord instanced = record(view_proj);
        for (int i = 0; i < 16; ++i) instanced.rows[i] = view_proj[i % 4][i / 4];
        instanced.layout = capture::TransformLayout::kCombine;
        instanced.instances.active = true;
        scene.draws.push_back(instanced);
        CHECK(568, FrameInverseViewProj(scene, inverse, &info) && info.candidates == 2);
        // A leaf: rows = view-projection * world; its world rows are beside it.
        const float world[3][4] = {{0.0f, -1.5f, 0.0f, 90.0f}, {1.5f, 0.0f, 0.0f, 150.0f}, {0.0f, 0.0f, 1.5f, 44.0f}};
        float leaf_rows[4][4];
        for (int i = 0; i < 4; ++i) {
          for (int j = 0; j < 4; ++j) {
            double sum = j == 3 ? double(view_proj[i][3]) : 0.0;
            for (int k = 0; k < 3; ++k) sum += double(view_proj[i][k]) * double(world[k][j]);
            leaf_rows[i][j] = float(sum);
          }
        }
        capture::LightInputs leaf;
        leaf.family = capture::LightFamily::kTreeLeaf;
        leaf.kind = capture::LightVertexKind::kProbe;
        for (int i = 0; i < 3; ++i) {
          for (int j = 0; j < 3; ++j) leaf.world_rot[i][j] = world[i][j];
          leaf.world_pos[i] = world[i][3];
        }
        scene.lights.push_back(leaf);
        scene.draws.push_back(record(leaf_rows));
        scene.draws.back().light_index = 0;
        CHECK(568, FrameInverseViewProj(scene, inverse, &info) && info.candidates == 3);
        // A trunk (another family) with odd rows gives no matrix and changes nothing.
        capture::LightInputs trunk = leaf;
        trunk.family = capture::LightFamily::kTreeTrunk;
        scene.lights.push_back(trunk);
        float odd[4][4];
        std::memcpy(odd, view_proj, sizeof(odd));
        odd[0][3] += 50.0f;
        scene.draws.push_back(record(odd));
        scene.draws.back().light_index = 1;
        CHECK(568, FrameInverseViewProj(scene, inverse, &info) && info.candidates == 3);
        // A leaf whose world rows are singular gives no matrix either (it is no disagreement).
        capture::LightInputs flat_leaf = leaf;
        for (int j = 0; j < 3; ++j) flat_leaf.world_rot[2][j] = 0.0f;
        scene.lights.push_back(flat_leaf);
        scene.draws.push_back(record(leaf_rows));
        scene.draws.back().light_index = 2;
        CHECK(568, FrameInverseViewProj(scene, inverse, &info) && info.candidates == 3);
        // One outlier among four: the majority's matrix stands and the outlier is counted (the review of
        // plan Task 5: one odd record must not turn the frame's shadows off). The tolerance from both
        // sides (1e-4 of the entry).
        FrameScene outlier = scene;
        capture::DrawRecord moved = record(view_proj);
        moved.terrain_index = 0;
        moved.rows[3] *= 1.0f + 5e-5f;  // the x row's translation
        outlier.draws.push_back(moved);
        CHECK(568, FrameInverseViewProj(outlier, inverse, &info) && info.candidates == 4 && info.outliers == 0);
        outlier.draws.back().rows[3] = view_proj[0][3] * (1.0f + 3e-4f);
        float kept[4][4];
        std::memcpy(kept, inverse, sizeof(kept));
        CHECK(568, FrameInverseViewProj(outlier, inverse, &info, back) && info.why == FrameViewReason::kNone && info.candidates == 4 &&
                       info.outliers == 1 && std::memcmp(kept, inverse, sizeof(kept)) == 0 && std::memcmp(back, view_proj, sizeof(back)) == 0);
        // (a direction entry that is nearly zero in the reference is held to the row's largest: 2e-8 against 0.84)
        outlier.draws.back().rows[3] = view_proj[0][3];
        outlier.draws.back().rows[2] = 1e-5f;
        CHECK(568, FrameInverseViewProj(outlier, inverse, &info) && info.outliers == 0);
        outlier.draws.back().rows[2] = 1e-3f;
        CHECK(568, FrameInverseViewProj(outlier, inverse, &info) && info.outliers == 1);
        // An outlier that comes first: one against one is no majority (fail closed), one against two is.
        FrameScene first;
        first.terrain.push_back(capture::TerrainPatch{});
        first.draws.push_back(record(odd));
        first.draws.back().terrain_index = 0;
        first.draws.push_back(record(view_proj));
        first.draws.back().terrain_index = 0;
        std::memcpy(kept, inverse, sizeof(kept));
        CHECK(568, !FrameInverseViewProj(first, inverse, &info) && info.why == FrameViewReason::kDisagree && info.candidates == 2 &&
                       std::memcmp(kept, inverse, sizeof(kept)) == 0);
        first.draws.push_back(record(view_proj));
        first.draws.back().terrain_index = 0;
        CHECK(568, FrameInverseViewProj(first, inverse, &info, back) && info.candidates == 3 && info.outliers == 1 &&
                       std::memcmp(back, view_proj, sizeof(back)) == 0);
        // Two against two, and three matrices that all differ: no majority, no view.
        first.draws.push_back(record(odd));
        first.draws.back().terrain_index = 0;
        CHECK(568, !FrameInverseViewProj(first, inverse, &info) && info.why == FrameViewReason::kDisagree && info.candidates == 4);
        {
          FrameScene three;
          three.terrain.push_back(capture::TerrainPatch{});
          for (float shift : {0.0f, 50.0f, 120.0f}) {
            float m[4][4];
            std::memcpy(m, view_proj, sizeof(m));
            m[0][3] += shift;
            three.draws.push_back(record(m));
            three.draws.back().terrain_index = 0;
          }
          CHECK(568, !FrameInverseViewProj(three, inverse, &info) && info.why == FrameViewReason::kDisagree && info.candidates == 3);
          // (the same three and two more of the first: three of five)
          for (int n = 0; n < 2; ++n) {
            three.draws.push_back(record(view_proj));
            three.draws.back().terrain_index = 0;
          }
          CHECK(568, FrameInverseViewProj(three, inverse, &info, back) && info.candidates == 5 && info.outliers == 2 &&
                         std::memcmp(back, view_proj, sizeof(back)) == 0);
        }
        // A matrix that is not finite, in the first record or a later one: no view.
        for (int which = 0; which < 2; ++which) {
          for (float bad : {nan, inf}) {
            FrameScene broken;
            broken.terrain.push_back(capture::TerrainPatch{});
            for (int n = 0; n < 2; ++n) {
              broken.draws.push_back(record(view_proj));
              broken.draws.back().terrain_index = 0;
            }
            broken.draws[which].rows[5] = bad;
            CHECK(568, !FrameInverseViewProj(broken, inverse, &info) && info.why == FrameViewReason::kDisagree);
          }
        }
        // (a single record whose matrix is not finite: the same reason, not a singular one)
        {
          FrameScene lone;
          lone.terrain.push_back(capture::TerrainPatch{});
          lone.draws.push_back(record(view_proj));
          lone.draws.back().terrain_index = 0;
          lone.draws.back().rows[10] = inf;
          CHECK(568, !FrameInverseViewProj(lone, inverse, &info) && info.why == FrameViewReason::kDisagree && info.candidates == 1 &&
                         info.outliers == 1);
          // (and among two good ones it is an outlier, counted)
          for (int n = 0; n < 2; ++n) {
            lone.draws.push_back(record(view_proj));
            lone.draws.back().terrain_index = 0;
          }
          CHECK(568, FrameInverseViewProj(lone, inverse, &info) && info.candidates == 3 && info.outliers == 1);
        }
        // A singular matrix (two equal rows; all zero): no inverse.
        float singular[4][4];
        std::memcpy(singular, view_proj, sizeof(singular));
        std::memcpy(singular[3], singular[2], sizeof(singular[3]));
        FrameScene degenerate;
        degenerate.terrain.push_back(capture::TerrainPatch{});
        degenerate.draws.push_back(record(singular));
        degenerate.draws.back().terrain_index = 0;
        CHECK(568, !FrameInverseViewProj(degenerate, inverse, &info) && info.why == FrameViewReason::kSingular && info.candidates == 1);
        const float zeros[4][4] = {};
        degenerate.draws[0] = record(zeros);
        degenerate.draws[0].terrain_index = 0;
        CHECK(568, !FrameInverseViewProj(degenerate, inverse, &info) && info.why == FrameViewReason::kSingular);
        float out[4][4];
        CHECK(568, !InvertMatrix4(zeros, out) && !InvertMatrix4(singular, out) && InvertMatrix4(view_proj, out));
        // A skipped record gives nothing. An empty scene has no view.
        FrameScene skipped;
        skipped.terrain.push_back(capture::TerrainPatch{});
        skipped.draws.push_back(record(view_proj));
        skipped.draws.back().terrain_index = 0;
        skipped.draws.back().skip = capture::SkipReason::kBadMemory;
        CHECK(568, !FrameInverseViewProj(skipped, inverse, &info) && info.why == FrameViewReason::kNoRecord);
        CHECK(568, !FrameInverseViewProj(FrameScene{}, inverse, &info) && info.why == FrameViewReason::kNoRecord);
        CHECK(568, std::strcmp(kShadowViewReason, "shadow-view") == 0 && std::strcmp(FrameViewReasonName(FrameViewReason::kDisagree), "disagree") == 0 &&
                       std::strcmp(FrameViewReasonName(FrameViewReason::kCount), "?") == 0);
      }
      // 569: the mask's and the leaves' sun factors at a mask of 1 are what the lighting picture had
      // (every pinned number of the light stands), and in full shadow the game's 0.4 and 0.64.
      {
        const LeafMasks lit = LeafMask(1.0f, kLeafMaskGain, kLeafMaskBias);
        const LeafMasks dark = LeafMask(0.0f, kLeafMaskGain, kLeafMaskBias);
        CHECK(569, lit.m1 == 1.0f && lit.m2 == 1.0f && std::fabs(dark.m1 - 0.4f) < 1e-7f && std::fabs(dark.m2 - 0.64f) < 1e-6f);
        CHECK(569, SunGateSingle(1.0f, 1.0f) == 1.0f && SunGateSingle(0.0f, 1.0f) == 0.0f && SunGateSingle(0.5f, 0.75f) == 0.375f);
        // A sun term under the mask: the object's colour with the mask at 0 is its ambient part alone.
        const Float3 A{0.5f, 0.4f, 0.3f}, v{0.2f, 0.3f, 0.4f}, tint{1.1f, 1.2f, 1.3f}, add{0.01f, 0.02f, 0.03f}, floor{0.05f, 0.09f, 0.0f};
        const Float3 sun{0.99f, 0.83f, 0.4f}, one{1.0f, 1.0f, 1.0f}, none{0.0f, 0.0f, 0.0f};
        const Float3 shadowed = LightObject(A, v, tint, 1.3f, add, floor, 0.7f, 0.6f, sun, 1.0f, SunGateSingle(0.0f, 1.0f), one);
        const Float3 no_sun = LightObject(A, v, tint, 1.3f, add, floor, 0.7f, 0.6f, none, 1.0f, 1.0f, one);
        const Float3 full = LightObject(A, v, tint, 1.3f, add, floor, 0.7f, 0.6f, sun, 1.0f, SunGateSingle(1.0f, 1.0f), one);
        CHECK(569, shadowed.x == no_sun.x && shadowed.y == no_sun.y && shadowed.z == no_sun.z && full.x > shadowed.x);
      }
      // 570: the Shadow line's second half: the pre-pass, the mask and the frame counts.
      {
        ClayStats st;
        st.shadow_switch = st.shadow_block = st.shadow_maps = true;
        st.shadow_reason = capture::ShadowBlockReason::kNone;
        st.shadow_passes = 2;
        st.prepass_drawn = 912;
        st.mask = ClayMask::kDrawn;
        st.mask_rotated = true;
        st.shadow_frames = 300;
        st.shadow_block_frames = 2;
        CHECK(570, FormatShadowText(st).ends_with(
                       " | pre-pass 912 draws, 0 beyond reach | mask drawn | frames since start 300: shadow-block 2, shadow-view 0, unrotated 0, no-light 0, no-objects 0"));
        st.prepass_culled = 61;
        CHECK(570, FormatShadowText(st).find(" | pre-pass 912 draws, 61 beyond reach | mask drawn | ") != std::string::npos);
        st.prepass_culled = 0;
        // (records whose matrix was not the frame's: on the line when there are any)
        st.mask_view_outliers = 3;
        CHECK(570, FormatShadowText(st).find(" | mask drawn (3 records with another view) | frames since start 300:") != std::string::npos);
        st.mask_view_outliers = 0;
        // Frames with the switch and the light on that had no light block, or no shadow objects: counted.
        st.shadow_nolight_frames = 7;
        st.shadow_objects_frames = 5;
        CHECK(570, FormatShadowText(st).ends_with(", unrotated 0, no-light 7, no-objects 5"));
        st.shadow_nolight_frames = st.shadow_objects_frames = 0;
        st.mask_rotated = false;
        st.prepass_skipped = 3;
        st.shadow_unrotated_frames = 300;
        CHECK(570, FormatShadowText(st).ends_with(
                       " | pre-pass 912 draws, 0 beyond reach (3 without a pipeline) | mask drawn, taps unrotated | frames since start 300: shadow-block 2, shadow-view 0, unrotated 300, no-light 0, no-objects 0"));
        st.mask = ClayMask::kView;
        st.mask_view = FrameViewReason::kDisagree;
        st.shadow_view_frames = 1;
        CHECK(570, FormatShadowText(st).find(" | mask white (shadow-view: disagree) | frames since start 300: shadow-block 2, shadow-view 1,") != std::string::npos);
        st.mask = ClayMask::kBlock;
        CHECK(570, FormatShadowText(st).find(" | mask white (shadow-block) | ") != std::string::npos);
        st.mask = ClayMask::kNoLight;
        CHECK(570, FormatShadowText(st).find(" | mask white (no light) | ") != std::string::npos);
        st.mask = ClayMask::kMaps;
        CHECK(570, FormatShadowText(st).find(" | mask white (maps not drawn) | ") != std::string::npos);
        st.mask = ClayMask::kObjects;
        CHECK(570, FormatShadowText(st).find(" | mask white (the shadows' objects could not be made: off until the pass is rebuilt or the game restarted) | ") != std::string::npos);
        st.mask = ClayMask::kOff;
        CHECK(570, FormatShadowText(st).find(" | mask white (off) | ") != std::string::npos);
        ClayStats off;
        CHECK(570, FormatShadowText(off) == "Shadow: off" && off.mask == ClayMask::kOff);
        CHECK(570, FormatMaskTargetText(1120, 720) == "shadow mask 1120 x 720: R8_UNORM 0.8 MB");
        // The split reasons of a record that is not drawn (the review of plan Task 4).
        CHECK(570, std::strcmp(ShadowDrawSkipName(ShadowDrawSkip::kTwin), "no-caster-entry") == 0 &&
                       std::strcmp(ShadowDrawSkipName(ShadowDrawSkip::kUnread), "state-unread") == 0 &&
                       std::strcmp(ShadowDrawSkipName(ShadowDrawSkip::kState), "state") == 0 && size_t(ShadowDrawSkip::kCount) == 11);
        // Which frames the two new counters take (CountsNoLightFrame, CountsNoObjectsFrame): only frames
        // with the switch and the light on.
        CHECK(570, CountsNoLightFrame(true, ClayMask::kNoLight) && !CountsNoLightFrame(false, ClayMask::kNoLight) &&
                       !CountsNoLightFrame(true, ClayMask::kDrawn) && !CountsNoLightFrame(true, ClayMask::kBlock));
        CHECK(570, CountsNoObjectsFrame(true, ClayMask::kObjects) && !CountsNoObjectsFrame(false, ClayMask::kObjects) &&
                       !CountsNoObjectsFrame(true, ClayMask::kMaps));
      }
      // 571: where a lit pixel reads the screen mask (the review of plan Task 5). The game's lit shaders
      // fetch tf8 with a linear filter at floor(pixel) * c0.zw + c0.xy, c0 = (0, 0, 1 / 1120, 1 / 720): the
      // pixel's corner, the mean of the four mask texels (px - 1, px) x (py - 1, py), clamped at the edge.
      {
        // The coordinate: the pixel's corner over the target's size, whatever the size is.
        ClayFrameConstants frame{};
        frame.light_on = 1;
        SetClayFrameMask(frame, 1120, 720);
        CHECK(571, MaskReadCoordinate(0.5f, frame.mask_du) == 0.0f && MaskReadCoordinate(559.5f, frame.mask_du) == 559.0f * frame.mask_du);
        CHECK(571, std::fabs(MaskReadCoordinate(559.5f, frame.mask_du) * 1120.0f - 559.0f) < 1e-3f);
        CHECK(571, std::fabs(MaskReadCoordinate(719.5f, frame.mask_dv) * 720.0f - 719.0f) < 1e-3f);
        // The four texels of a pixel and their mean.
        MaskReadTexels t = MaskRead(5, 7, 1120, 720);
        CHECK(571, t.x0 == 4 && t.x1 == 5 && t.y0 == 6 && t.y1 == 7);
        t = MaskRead(0, 0, 1120, 720);
        CHECK(571, t.x0 == 0 && t.x1 == 0 && t.y0 == 0 && t.y1 == 0);
        t = MaskRead(1119, 719, 1120, 720);
        CHECK(571, t.x0 == 1118 && t.x1 == 1119 && t.y0 == 718 && t.y1 == 719);
        t = MaskRead(3, 2, 1, 1);  // (the 1 x 1 white texture: its one texel, whatever the pixel is)
        CHECK(571, t.x0 == 0 && t.x1 == 0 && t.y0 == 0 && t.y1 == 0);
        const uint8_t mask[3 * 4] = {255, 0, 0, 0, /**/ 255, 255, 51, 0, /**/ 0, 0, 0, 102};
        CHECK(571, MaskReadValue(mask, 4, 3, 0, 0) == 1.0f);                              // the corner: one texel
        CHECK(571, std::fabs(MaskReadValue(mask, 4, 3, 1, 0) - 0.5f) < 1e-7f);             // top row: two texels
        CHECK(571, std::fabs(MaskReadValue(mask, 4, 3, 1, 1) - 0.75f) < 1e-7f);            // 255, 0, 255, 255
        CHECK(571, std::fabs(MaskReadValue(mask, 4, 3, 2, 1) - (255 + 51) / (4.0f * 255.0f)) < 1e-7f);
        CHECK(571, std::fabs(MaskReadValue(mask, 4, 3, 3, 2) - (51 + 102) / (4.0f * 255.0f)) < 1e-7f);
        CHECK(571, MaskReadValue(nullptr, 4, 3, 1, 1) == 1.0f && MaskReadValue(mask, 0, 3, 1, 1) == 1.0f);  // no mask: lit
        const uint8_t white[1] = {255};
        CHECK(571, MaskReadValue(white, 1, 1, 640, 360) == 1.0f);  // (a mask of 1 stays 1 exactly: the light's pinned numbers stand)
        // An edge one texel wide in the mask is a ramp of two pixels in the read (the game's softening).
        const uint8_t edge[1 * 4] = {0, 0, 255, 255};
        CHECK(571, MaskReadValue(edge, 4, 1, 1, 0) == 0.0f && std::fabs(MaskReadValue(edge, 4, 1, 2, 0) - 0.5f) < 1e-7f &&
                       MaskReadValue(edge, 4, 1, 3, 0) == 1.0f);
        // 572: the pre-pass's lighter draw (the review of plan Task 5, problem B): a textured record whose
        // alpha discards nothing is drawn as flat clay in the pre-pass (no texture read).
        PipelineKey colour;
        colour.textured = 1;
        colour.depth_func = 3;
        colour.cull = 2;
        ClayAlpha solid;
        CHECK(572, PrepassKey(colour, solid).textured == 0 && PrepassKey(colour, solid).depth_func == 3 && PrepassKey(colour, solid).cull == 2 &&
                       PrepassKey(colour, solid).color_mask == 0);
        ClayAlpha tested;
        tested.mode = kAlphaFromAlbedo | kAlphaTest | 4;
        CHECK(572, PrepassKey(colour, tested).textured == 1 && PrepassDiscards(tested) && !PrepassDiscards(solid));
        ClayAlpha to_mask;
        to_mask.mode = kAlphaFromAlbedo | kAlphaToMask;
        CHECK(572, PrepassKey(colour, to_mask).textured == 1 && PrepassDiscards(to_mask));
        ClayAlpha blended;  // (an alpha that is only written for the blend discards nothing)
        blended.mode = kAlphaFromAlbedo | kAlphaBlend;
        CHECK(572, PrepassKey(colour, blended).textured == 0);
        // A terrain record keeps its kind whatever the alpha says (a layer's hole and weight discard).
        colour.textured = 2;
        CHECK(572, PrepassKey(colour, solid).textured == 2);
        colour.textured = 3;
        CHECK(572, PrepassKey(colour, solid).textured == 3);
        colour.textured = 0;
        CHECK(572, PrepassKey(colour, tested).textured == 0);
      }
      // 573: a record wholly beyond the mask's reach is left out of the pre-pass (plan Task 5b, the
      // third spot's frame rate). Beyond split.y + 10 units from the eye the game's mask is split.w
      // whatever the depth (the fade is saturated there and neither map is read), which is also what
      // a pixel at the clear depth gets: such a record changes no pixel of the mask.
      {
        // The mask beyond the reach is split.w exactly, for any term values and any distance there.
        const float split[4] = {20.0f, 35.0f, 2.0f, 0.4771f};
        for (float d : {45.0f, 45.001f, 60.0f, 5000.0f}) {
          CHECK(573, float(MaskCombine(d, 0.0, 0.0, split, kMaskFadeSlope)) == split[3]);
          // (the two terms are not looked at there: fully lit, fully shadowed and mixed values alike)
          for (double near_term : {0.0, 0.3, 1.0}) {
            for (double far_term : {0.0, 0.7, 1.0}) {
              CHECK(573, float(MaskCombine(d, near_term, far_term, split, kMaskFadeSlope)) == split[3]);
            }
          }
        }
        CHECK(573, float(MaskCombine(44.9f, 0.0, 0.0, split, kMaskFadeSlope)) != split[3]);
        CHECK(573, PrepassReach(split) == 45.0f + kPrepassReachMargin && kPrepassReachMargin > 0.0f && kPrepassReachMargin <= 0.1f);
        // Bounds of a buffer's positions: the box of all four components; none for no vertex or a
        // value that is not finite.
        const capture::Float4 pts[3] = {{1.0f, -2.0f, 3.0f, 1.0f}, {-4.0f, 5.0f, 0.5f, 1.0f}, {2.0f, 2.0f, 9.0f, 1.0f}};
        const PositionBounds box = MakePositionBounds(pts, 3);
        CHECK(573, box.valid && box.lo[0] == -4.0f && box.hi[0] == 2.0f && box.lo[1] == -2.0f && box.hi[1] == 5.0f && box.lo[2] == 0.5f &&
                       box.hi[2] == 9.0f && box.lo[3] == 1.0f && box.hi[3] == 1.0f);
        CHECK(573, !MakePositionBounds(pts, 0).valid && !MakePositionBounds(nullptr, 3).valid && !PositionBounds{}.valid);
        for (float bad : {nan, inf, -inf}) {
          capture::Float4 broken[3] = {pts[0], pts[1], pts[2]};
          broken[1].y = bad;
          CHECK(573, !MakePositionBounds(broken, 3).valid);
        }
        // A view along +x from the eye at (10, 20, 30): clip w = 2 * (x - 10) (a w row that is not of
        // unit length: the distance is w over the row's length). A record's rows are that matrix
        // (world positions in its buffer).
        const float vp[4][4] = {{0.0f, 1.0f, 0.0f, -20.0f}, {0.0f, 0.0f, 1.0f, -30.0f}, {0.5f, 0.0f, 0.0f, 1.0f}, {2.0f, 0.0f, 0.0f, -20.0f}};
        const float eye573[3] = {10.0f, 20.0f, 30.0f};
        auto slab = [&](float x0, float x1) {
          PositionBounds b;
          b.valid = true;
          const float lo[4] = {x0, -500.0f, -500.0f, 1.0f}, hi[4] = {x1, 500.0f, 500.0f, 1.0f};
          std::memcpy(b.lo, lo, sizeof(lo));
          std::memcpy(b.hi, hi, sizeof(hi));
          return b;
        };
        const uint8_t less = uint8_t(capture::Compare::kLess), less_equal = uint8_t(capture::Compare::kLessEqual);
        // Nearest point 45.1 units along the view: beyond. 44.9: kept. A box that straddles: kept.
        CHECK(573, PrepassBeyondReach(vp, slab(10.0f + 45.1f, 900.0f), vp, eye573, split, less));
        CHECK(573, PrepassBeyondReach(vp, slab(10.0f + 45.1f, 900.0f), vp, eye573, split, less_equal));
        CHECK(573, !PrepassBeyondReach(vp, slab(10.0f + 44.9f, 900.0f), vp, eye573, split, less));
        CHECK(573, !PrepassBeyondReach(vp, slab(10.0f + 45.0f, 900.0f), vp, eye573, split, less));  // (inside the margin)
        CHECK(573, !PrepassBeyondReach(vp, slab(-100.0f, 900.0f), vp, eye573, split, less));
        CHECK(573, !PrepassBeyondReach(vp, slab(-900.0f, -100.0f), vp, eye573, split, less));  // behind the eye: kept
        // The distance is measured from the eye, not from the plane w = 0: with a w row whose value at
        // the eye is not 0 (here 10: clip w = 2 * x - 10 at the same eye) the eye's own w is taken
        // off. Without that the slab 44.9 units away would read 49.9 and be left out.
        {
          float off[4][4];
          std::memcpy(off, vp, sizeof(off));
          off[3][3] = -10.0f;
          CHECK(573, PrepassBeyondReach(off, slab(10.0f + 45.1f, 900.0f), off, eye573, split, less));
          CHECK(573, !PrepassBeyondReach(off, slab(10.0f + 44.9f, 900.0f), off, eye573, split, less));
          CHECK(573, !PrepassBeyondReach(off, slab(10.0f + 41.0f, 900.0f), off, eye573, split, less));
        }
        // The bound is the distance along the view axis, which is never more than the distance from
        // the eye: a point at view depth 45.2 is at least 45.2 units away, wherever it is sideways.
        {
          const float wx = 10.0f + 45.2f, wy = 400.0f, wz = -300.0f;
          const double dist = std::sqrt(double(wx - 10.0f) * (wx - 10.0f) + double(wy - 20.0f) * (wy - 20.0f) + double(wz - 30.0f) * (wz - 30.0f));
          CHECK(573, dist >= 45.2 && PrepassBeyondReach(vp, slab(wx, wx), vp, eye573, split, less));
        }
        // A mesh record: its rows are view-projection * world (a move of +100 in x), its box in object space.
        float mesh_rows[4][4];
        std::memcpy(mesh_rows, vp, sizeof(mesh_rows));
        for (int i = 0; i < 4; ++i) mesh_rows[i][3] += vp[i][0] * 100.0f;
        CHECK(573, PrepassBeyondReach(mesh_rows, slab(-44.0f, 5.0f), vp, eye573, split, less));    // world x from 56: 46 units
        CHECK(573, !PrepassBeyondReach(mesh_rows, slab(-46.0f, 5.0f), vp, eye573, split, less));   // world x from 54: 44 units
        // A w component that is not 1 is taken with the box (a 4D box, each term at its smaller end).
        {
          PositionBounds b = slab(10.0f + 50.0f, 900.0f);
          b.lo[3] = 0.5f;  // w down to 0.5: clip w falls by up to 10 (the row's -20 * 0.5 against -20 * 1 rises; the lower end counts)
          b.hi[3] = 3.0f;  // w up to 3: clip w = 2 x - 60 at its smallest
          CHECK(573, !PrepassBeyondReach(vp, b, vp, eye573, split, less));
        }
        // Kept, always: no bounds; a depth function that does not keep the nearer surface (a far record
        // drawn with "always" hides a near one in the pre-pass's depth); values that are not finite; a
        // view whose w row has no direction.
        CHECK(573, !PrepassBeyondReach(vp, PositionBounds{}, vp, eye573, split, less));
        for (capture::Compare f : {capture::Compare::kAlways, capture::Compare::kGreater, capture::Compare::kGreaterEqual,
                                   capture::Compare::kEqual, capture::Compare::kNotEqual, capture::Compare::kNever}) {
          CHECK(573, !PrepassBeyondReach(vp, slab(500.0f, 900.0f), vp, eye573, split, uint8_t(f)));
        }
        for (float bad : {nan, inf}) {
          float m[4][4];
          std::memcpy(m, vp, sizeof(m));
          m[3][1] = bad;
          CHECK(573, !PrepassBeyondReach(m, slab(500.0f, 900.0f), vp, eye573, split, less));
          CHECK(573, !PrepassBeyondReach(vp, slab(500.0f, 900.0f), m, eye573, split, less));
          const float bad_eye[3] = {10.0f, bad, 30.0f};
          CHECK(573, !PrepassBeyondReach(vp, slab(500.0f, 900.0f), vp, bad_eye, split, less));
          const float bad_split[4] = {20.0f, bad, 2.0f, 0.4771f};
          CHECK(573, !PrepassBeyondReach(vp, slab(500.0f, 900.0f), vp, eye573, bad_split, less));
          PositionBounds b = slab(500.0f, 900.0f);
          b.hi[2] = bad;
          CHECK(573, !PrepassBeyondReach(vp, b, vp, eye573, split, less));
        }
        float flat_view[4][4];
        std::memcpy(flat_view, vp, sizeof(flat_view));
        flat_view[3][0] = 0.0f;
        CHECK(573, !PrepassBeyondReach(vp, slab(500.0f, 900.0f), flat_view, eye573, split, less));
        // (a box with lo above hi is no box)
        PositionBounds turned = slab(900.0f, 500.0f);
        CHECK(573, !PrepassBeyondReach(vp, turned, vp, eye573, split, less));
        // 568 again: a frame whose records all agree is settled in one walk, and the count is the same.
        FrameScene agree;
        agree.terrain.push_back(capture::TerrainPatch{});
        for (int n = 0; n < 5; ++n) {
          capture::DrawRecord r = BaseRecord();
          for (int i = 0; i < 16; ++i) r.rows[i] = vp[i / 4][i % 4];
          r.terrain_index = 0;
          agree.draws.push_back(r);
        }
        float inv573[4][4], back573[4][4];
        FrameView info573;
        CHECK(573, FrameInverseViewProj(agree, inv573, &info573, back573) && info573.candidates == 5 && info573.outliers == 0 &&
                       std::memcmp(back573, vp, sizeof(vp)) == 0);
      }
      // 574: what the pass does about the frame's mask (DecideClayMask; the final review's fix wave):
      // one chain, first failure wins, and the maps are drawn only for a frame whose mask is.
      {
        ClayMaskInputs all;
        all.shadows = all.light = all.objects = all.block = all.light_block = all.constants = all.eye = all.finite = true;
        all.view = FrameViewReason::kNone;
        auto is = [](const ClayMaskInputs& in, ClayMask mask, FrameViewReason view) {
          const ClayMaskDecision d = DecideClayMask(in);
          return d.mask == mask && d.view == view;
        };
        CHECK(574, is(all, ClayMask::kDrawn, FrameViewReason::kNone));
        // (a default input is the switch off)
        CHECK(574, is(ClayMaskInputs{}, ClayMask::kOff, FrameViewReason::kNone));
        struct Row {
          bool ClayMaskInputs::*field;
          ClayMask mask;
          FrameViewReason view;
        };
        // In the chain's order: each row's field is false and every earlier row's field true.
        const Row rows[] = {
            {&ClayMaskInputs::shadows, ClayMask::kOff, FrameViewReason::kNone},
            {&ClayMaskInputs::light, ClayMask::kNoLight, FrameViewReason::kNone},
            {&ClayMaskInputs::objects, ClayMask::kObjects, FrameViewReason::kNone},
            {&ClayMaskInputs::block, ClayMask::kBlock, FrameViewReason::kNone},
            {&ClayMaskInputs::light_block, ClayMask::kNoLight, FrameViewReason::kNone},
            {&ClayMaskInputs::constants, ClayMask::kMaps, FrameViewReason::kNone},
            {&ClayMaskInputs::eye, ClayMask::kView, FrameViewReason::kEye},
            {&ClayMaskInputs::finite, ClayMask::kView, FrameViewReason::kSingular},
        };
        const size_t count = sizeof(rows) / sizeof(rows[0]);
        for (size_t i = 0; i < count; ++i) {
          // The row's failure alone, and with every later failure too: the first one decides.
          ClayMaskInputs one = all;
          one.*rows[i].field = false;
          CHECK(574, is(one, rows[i].mask, rows[i].view));
          ClayMaskInputs rest = all;
          for (size_t j = i; j < count; ++j) rest.*rows[j].field = false;
          CHECK(574, is(rest, rows[i].mask, rows[i].view));
        }
        // The frame's view: its reason is the mask's, after the eye and before the constants' check.
        for (FrameViewReason why : {FrameViewReason::kNoRecord, FrameViewReason::kDisagree, FrameViewReason::kSingular}) {
          ClayMaskInputs v = all;
          v.view = why;
          CHECK(574, is(v, ClayMask::kView, why));
          v.finite = false;
          CHECK(574, is(v, ClayMask::kView, why));
          v.eye = false;
          CHECK(574, is(v, ClayMask::kView, FrameViewReason::kEye));
          v.constants = false;
          CHECK(574, is(v, ClayMask::kMaps, FrameViewReason::kNone));
        }
        // Only a drawn mask has maps: every other verdict draws none (ClayMapsDrawn).
        for (size_t m = 0; m < size_t(ClayMask::kCount); ++m) {
          CHECK(574, ClayMapsDrawn(ClayMask(m)) == (ClayMask(m) == ClayMask::kDrawn));
        }
        // The frame counts follow from the verdict (CountClayMaskFrame). `shadows_on`: the switch and
        // the light's switch; without both nothing is counted.
        auto counts = [](bool shadows_on, ClayMask mask, bool rotated) {
          ClayMaskFrames n;
          CountClayMaskFrame(shadows_on, mask, rotated, n);
          return n;
        };
        auto same = [](const ClayMaskFrames& n, uint64_t frames, uint64_t block, uint64_t view, uint64_t unrotated, uint64_t nolight,
                       uint64_t objects) {
          return n.frames == frames && n.block == block && n.view == view && n.unrotated == unrotated && n.nolight == nolight &&
                 n.objects == objects;
        };
        CHECK(574, same(counts(true, ClayMask::kDrawn, true), 1, 0, 0, 0, 0, 0));
        CHECK(574, same(counts(true, ClayMask::kDrawn, false), 1, 0, 0, 1, 0, 0));
        CHECK(574, same(counts(true, ClayMask::kBlock, false), 1, 1, 0, 0, 0, 0));
        // (a valid block without the frame's constant buffer: counted with the frames without a block)
        CHECK(574, same(counts(true, ClayMask::kMaps, false), 1, 1, 0, 0, 0, 0));
        CHECK(574, same(counts(true, ClayMask::kView, false), 1, 0, 1, 0, 0, 0));
        CHECK(574, same(counts(true, ClayMask::kNoLight, false), 1, 0, 0, 0, 1, 0));
        CHECK(574, same(counts(true, ClayMask::kObjects, false), 1, 0, 0, 0, 0, 1));
        for (size_t m = 0; m < size_t(ClayMask::kCount); ++m) {
          CHECK(574, same(counts(false, ClayMask(m), false), 0, 0, 0, 0, 0, 0));
        }
        ClayMaskFrames sum;
        CountClayMaskFrame(true, ClayMask::kDrawn, true, sum);
        CountClayMaskFrame(true, ClayMask::kView, false, sum);
        CountClayMaskFrame(true, ClayMask::kView, false, sum);
        CHECK(574, same(sum, 3, 0, 2, 0, 0, 0));
      }
      // 575: the words of an on / off switch (fable2_native_light, fable2_native_shadows): on, true, 1
      // and off, false, 0, in any case and with blanks around; anything else is off and not recognised.
      {
        bool ok = false;
        for (const char* word : {"on", "true", "1", " TRUE ", "On"}) {
          ok = false;
          CHECK(575, ParseClayLight(word, &ok) == ClayLight::kOn && ok);
        }
        for (const char* word : {"off", "false", "0", " False ", "OFF"}) {
          ok = false;
          CHECK(575, ParseClayLight(word, &ok) == ClayLight::kOff && ok);
        }
        for (const char* word : {"", "yes", "no", "2", "onn", "tru", "-1", "o n"}) {
          ok = true;
          CHECK(575, ParseClayLight(word, &ok) == ClayLight::kOff && !ok);
        }
        CHECK(575, ParseClayLight("true", nullptr) == ClayLight::kOn && ParseClayLight("yes", nullptr) == ClayLight::kOff);
        // The warning names every accepted word.
        const std::string words = kClaySwitchWords;
        for (const char* word : {"on", "off", "true", "false", "1", "0"}) CHECK(575, words.find(word) != std::string::npos);
      }
      // 576: the Shadow line's frame counts are the pass's totals since it was made, and say so (the
      // capture's "shadow-block in N of M frames" is of one log window); a frame without the shadows'
      // objects says until when.
      {
        ClayStats st;
        st.shadow_switch = st.shadow_block = st.shadow_maps = true;
        st.shadow_passes = 2;
        st.mask = ClayMask::kDrawn;
        st.mask_rotated = true;
        st.shadow_frames = 900;
        st.shadow_block_frames = 4;
        CHECK(576, FormatShadowText(st).ends_with(
                       " | mask drawn | frames since start 900: shadow-block 4, shadow-view 0, unrotated 0, no-light 0, no-objects 0"));
        st.mask = ClayMask::kObjects;
        CHECK(576, FormatShadowText(st).find(" | mask white (the shadows' objects could not be made: off until the pass is rebuilt or "
                                             "the game restarted) | frames since start 900:") != std::string::npos);
      }
    }
  }
  // 580-583: native fog (fog sub-project, plan Task 4; frame-map section 16, "Native fog"): the mirrors
  // of the shaders' fog against the reference's numbers, the tables' read, the guards, the frame's
  // constants. Every line of numbers below was written by tools/xdk_sigmatch/fog_pins.py
  // (light_formulas.fog, vertex_fog with fog_apply, fog_coords, table_1d), and tests/test_fog_literals.py
  // holds the lines against that tool's output.
  {
    const float qnan = std::numeric_limits<float>::quiet_NaN(), pinf = std::numeric_limits<float>::infinity();
    // The block of frame 1 of capture 092459 (tests/native/test_fog_block.cpp has the same inputs): its
    // registers, its fetch constants and its tables' bytes through MakeFogBlock; and its eye, pixel c19.
    const float regs[6][4] = {{1.0f, 1.0f, 0.0f, 0.0f},
                              {0.000305443653f, 0.000393790426f, 0.00107105088f, 0.0f},
                              {0.00525102764f, 0.00596226612f, 0.00983294938f, 0.0f},
                              {-0.8295828104019165f, 0.04456644505262375f, 0.5566023588180542f, 0.0f},
                              {19.881736755371094f, 1900.0f, 1.0f, 0.0005000000237487257f},
                              {0.2666446566581726f, 0.39993393421173096f, 0.46666669845581055f, 0.0f}};
    const uint32_t fc4[6] = {0x00804956, 0x1FAB505D, 0x0000003F, 0x00280D11, 0x00000003, 0x00000018};
    const uint32_t fc5[6] = {0x02004802, 0x1FAB4086, 0x000000FF, 0x00280C14, 0x00000003, 0x00000018};
    const capture::FogBlock real = capture::MakeFogBlock(regs, fc4, fc5, kFixtureTf4, sizeof(kFixtureTf4), kFixtureTf5, sizeof(kFixtureTf5));
    const float eye[3] = {82.07209777832031f, 157.33926391601562f, 50.17070388793945f};
    CHECK(580, real.valid);
    // The made-up set: an amount below 1, a scale that is not 1, a slope that leaves pixels inside
    // the near blend, a nearer start and a table that ends sooner.
    capture::FogBlock made = real;
    made.amount[0] = 0.75f;
    made.amount[1] = 1.5f;
    made.range[0] = 12.5f;
    made.range[2] = 0.019999999552965164f;
    made.range[3] = 0.001500000013038516f;
    made.near_colour[0] = 0.6000000238418579f;
    made.near_colour[1] = 0.20000000298023224f;
    made.near_colour[2] = 0.8999999761581421f;
    static_assert(kFogScatterTexels == 64 && kFogTransmitTexels == 256);
    static_assert(sizeof(real.scatter) == kFogScatterTexels * 3 * sizeof(float) && sizeof(real.transmit) == kFogTransmitTexels * 3 * sizeof(float));
    auto close3 = [](const double got[3], const double want[3], double tol) {
      return std::fabs(got[0] - want[0]) <= tol && std::fabs(got[1] - want[1]) <= tol && std::fabs(got[2] - want[2]) <= tol;
    };
    auto same3 = [](const double got[3], const float want[3]) {
      return got[0] == double(want[0]) && got[1] == double(want[1]) && got[2] == double(want[2]);
    };
    auto finite3 = [](const double v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); };

    // 580: a table as the game's fetch reads it (light_formulas.table_1d): texel i of N has its centre
    // at (i + 0.5) / N, linear between two centres, the edge texel alone beyond the first and the last
    // centre and outside 0..1.
    {
      const struct {
        double u;
        double tf4[3], tf5[3];
      } pins[] = {
        // table pins: u, tf4(u), tf5(u)
            {-1.0, {0.30810546875, 0.384033203125, 0.46533203125}, {1.0, 1.0, 1.0}},
            {0.0, {0.30810546875, 0.384033203125, 0.46533203125}, {1.0, 1.0, 1.0}},
            {0.0078125, {0.30810546875, 0.384033203125, 0.46533203125}, {0.9803921580314636, 0.97843137383461, 0.9627451002597809}},
            {0.001953125, {0.30810546875, 0.384033203125, 0.46533203125}, {1.0, 1.0, 1.0}},
            {0.005859375, {0.30810546875, 0.384033203125, 0.46533203125}, {0.9843137264251709, 0.9843137264251709, 0.9725490212440491}},
            {0.127, {0.352375, 0.401923828125, 0.4612099609375}, {0.7450509955883026, 0.7136784484386444, 0.564564731836319}},
            {0.3, {0.431005859375, 0.4287109375, 0.4529296875}, {0.5282353222370147, 0.4811764717102051, 0.28509805202484134}},
            {0.5, {0.550537109375, 0.460693359375, 0.4366455078125}, {0.36274510622024536, 0.3117647171020508, 0.1352941244840622}},
            {0.9921875, {0.93310546875, 0.466552734375, 0.306396484375}, {0.14901961386203766, 0.11372549086809158, 0.0235294122248888}},
            {0.998, {0.93310546875, 0.466552734375, 0.306396484375}, {0.14901961386203766, 0.11372549086809158, 0.0235294122248888}},
            {1.0, {0.93310546875, 0.466552734375, 0.306396484375}, {0.14901961386203766, 0.11372549086809158, 0.0235294122248888}},
            {2.5, {0.93310546875, 0.466552734375, 0.306396484375}, {0.14901961386203766, 0.11372549086809158, 0.0235294122248888}},
      };
      CHECK(580, std::size(pins) == 12);
      for (const auto& p : pins) {
        double s[3], t[3];
        FogTableRead(real.scatter, kFogScatterTexels, p.u, s);
        FogTableRead(real.transmit, kFogTransmitTexels, p.u, t);
        CHECK(580, close3(s, p.tf4, 1e-12) && close3(t, p.tf5, 1e-12));
      }
      // Hand cases on a table of four texels, red = the texel's index: centres at 0.125, 0.375, 0.625, 0.875.
      const float four[4][3] = {{0.0f, 10.0f, -1.0f}, {1.0f, 20.0f, -2.0f}, {2.0f, 40.0f, -4.0f}, {3.0f, 80.0f, -8.0f}};
      const struct {
        double u, r, g;
      } hand[] = {{0.125, 0.0, 10.0}, {0.375, 1.0, 20.0}, {0.875, 3.0, 80.0}, {0.25, 0.5, 15.0}, {0.5, 1.5, 30.0}, {0.6875, 2.25, 50.0},
                  {0.0, 0.0, 10.0}, {0.1, 0.0, 10.0}, {-5.0, 0.0, 10.0}, {0.9, 3.0, 80.0}, {1.0, 3.0, 80.0}, {7.0, 3.0, 80.0},
                  {double(pinf), 3.0, 80.0}, {double(-pinf), 0.0, 10.0}, {double(qnan), 0.0, 10.0}, {1e300, 3.0, 80.0}};
      for (const auto& h : hand) {
        double v[3];
        FogTableRead(four, 4, h.u, v);
        CHECK(580, std::fabs(v[0] - h.r) < 1e-12 && std::fabs(v[1] - h.g) < 1e-12 && std::fabs(v[2] + h.g / 10.0) < 1e-12);
      }
      // A table of one texel, and no table: the texel, and zeros.
      double v[3] = {9.0, 9.0, 9.0};
      FogTableRead(four + 2, 1, 0.3, v);
      CHECK(580, v[0] == 2.0 && v[1] == 40.0 && v[2] == -4.0);
      FogTableRead(four, 0, 0.3, v);
      CHECK(580, v[0] == 0.0 && v[1] == 0.0 && v[2] == 0.0);
      FogTableRead(static_cast<const float (*)[3]>(nullptr), 4, 0.3, v);
      CHECK(580, v[0] == 0.0 && v[1] == 0.0 && v[2] == 0.0);
    }

    // 581: the two forms on the frame's registers and on the made-up set. The ground form
    // (light_formulas.fog): the blend from c68, the amount c28.x and the scale c28.y. The trees' form
    // (ruling B: vertex_fog at the pixel, then fog_apply): T = tf5, add = (1 - T) * tf4(angle) * c8.x,
    // no blend from c68 and no amount.
    struct FogPin {
      float P[3], C[3];
      int fogged;
      double coords[3], ground[3], tree[3], tree_half[3];
    };
    static const FogPin real_pins[] = {
      // real registers: c28 {1.0f, 1.0f} c66 {-0.8295828104019165f, 0.04456644505262375f, 0.5566023588180542f} c67 {19.881736755371094f, 1900.0f, 1.0f, 0.0005000000237487257f} c68 {0.2666446566581726f, 0.39993393421173096f, 0.46666669845581055f} eye {82.07209777832031f, 157.33926391601562f, 50.17070388793945f}
      // real pins: P, C, fogged, (a, s, t), the ground form, the trees' form with c8.x = 1.0 and 0.5
          {{82.07209777832031f, 167.33926391601562f, 50.17070388793945f}, {0.3499999940395355f, 0.30000001192092896f, 0.11999999731779099f}, 0, {0.0, 0.0, 0.0}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}},
          {{85.76200103759766f, 145.0395965576172f, 51.40066909790039f}, {0.3499999940395355f, 0.30000001192092896f, 0.11999999731779099f}, 0, {0.0, 0.0, 0.0}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}},
          {{101.40543365478516f, 162.17259216308594f, 46.304039001464844f}, {0.3499999940395355f, 0.30000001192092896f, 0.11999999731779099f}, 1, {0.0, 0.00020913206821352202, 0.4182641165605645}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}},
          {{131.84706115722656f, 154.665283203125f, 16.77456283569336f}, {0.05000000074505806f, 0.05999999865889549f, 0.019999999552965164f}, 1, {0.0, 0.02005913003092971, 1.0}, {0.06343188398347593, 0.07894057937347554, 0.06285761508737381}, {0.06343188398347593, 0.07894057937347554, 0.06285761508737381}, {0.05541493511107008, 0.06771671129357842, 0.04046643292250607}},
          {{-128.64193725585938f, 168.6591339111328f, 191.54769897460938f}, {0.05000000074505806f, 0.05999999865889549f, 0.019999999552965164f}, 1, {0.9999999965903957, 0.11705913595873045, 1.0}, {0.26102506168498507, 0.16830927224863582, 0.1383787970338749}, {0.26102506168498507, 0.16830927224863582, 0.1383787970338749}, {0.14953858269669848, 0.10616236815456304, 0.07505600975310686}},
          {{-265.9530334472656f, 350.6865539550781f, 88.84016418457031f}, {2.5f, 0.009999999776482582f, 0.0f}, 1, {0.797139967333287, 0.19005913670568128, 1.0}, {1.8925561844319032, 0.18710146578345407, 0.21317825728481826}, {1.8925561844319032, 0.18710146578345407, 0.21317825728481826}, {1.7641450836757622, 0.0966261223691406, 0.10658912864240913}},
          {{-396.5661315917969f, 348.7945556640625f, 911.719482421875f}, {0.0f, 0.0f, 0.0f}, 1, {0.8816161552519696, 0.4920591502991993, 1.0}, {0.5171694492099624, 0.32053205151109904, 0.30334836396928283}, {0.5171694492099624, 0.32053205151109904, 0.30334836396928283}, {0.2585847246049812, 0.16026602575554952, 0.15167418198464142}},
          {{-2556.724853515625f, -634.2998046875f, 1237.62939453125f}, {0.20000000298023224f, 0.20000000298023224f, 0.20000000298023224f}, 1, {0.9382541248743983, 1.4900591946531574, 1.0}, {0.7730160268230054, 0.438067643711012, 0.32767189312044176}, {0.7730160268230054, 0.438067643711012, 0.32767189312044176}, {0.401409975019763, 0.23040637111177933, 0.16618888781777133}},
          {{85.07149505615234f, 307.30926513671875f, 50.17070388793945f}, {0.10000000149011612f, 0.4000000059604645f, 0.05000000074505806f}, 1, {0.027969210765100838, 0.06505913079607037, 1.0}, {0.13147306663654246, 0.39783281450447827, 0.15997013659267717}, {0.13147306663654246, 0.39783281450447827, 0.15997013659267717}, {0.10842079420064522, 0.3657318823295919, 0.09835560328530862}},
          {{36.796142578125f, 232.7991943359375f, 69.03568267822266f}, {0.8999999761581421f, 0.800000011920929f, 0.699999988079071f}, 1, {0.5713713850435036, 0.03505913507372106, 1.0}, {0.8734792431753198, 0.7672579226373761, 0.6561861162372317}, {0.8734792431753198, 0.7672579226373761, 0.6561861162372317}, {0.8480038297821609, 0.7444912793695355, 0.6219550407331189}},
    };
    static const FogPin made_pins[] = {
      // made registers: c28 {0.75f, 1.5f} c66 {-0.8295828104019165f, 0.04456644505262375f, 0.5566023588180542f} c67 {12.5f, 1900.0f, 0.019999999552965164f, 0.001500000013038516f} c68 {0.6000000238418579f, 0.20000000298023224f, 0.8999999761581421f} eye {82.07209777832031f, 157.33926391601562f, 50.17070388793945f}
      // made pins: P, C, fogged, (a, s, t), the ground form, the trees' form with c8.x = 1.0 and 0.5
          {{82.07209777832031f, 167.33926391601562f, 50.17070388793945f}, {0.3499999940395355f, 0.30000001192092896f, 0.11999999731779099f}, 0, {0.0, 0.0, 0.0}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}},
          {{85.76200103759766f, 145.0395965576172f, 51.40066909790039f}, {0.3499999940395355f, 0.30000001192092896f, 0.11999999731779099f}, 1, {0.0, 0.0006000010289242136, 0.008000013470636403}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}},
          {{101.40543365478516f, 162.17259216308594f, 46.304039001464844f}, {0.3499999940395355f, 0.30000001192092896f, 0.11999999731779099f}, 1, {0.0, 0.011700001409597923, 0.15600001395176105}, {0.36060525638321894, 0.30107478224773515, 0.168038506758301}, {0.3487701678231873, 0.30279637308073587, 0.13960402417392803}, {0.3442479041115174, 0.2964066625514339, 0.12639589295341608}},
          {{131.84706115722656f, 154.665283203125f, 16.77456283569336f}, {0.05000000074505806f, 0.05999999865889549f, 0.019999999552965164f}, 1, {0.0, 0.0712499929869027, 0.9499998770001576}, {0.1016775168503703, 0.12718294931575358, 0.1730293965758769}, {0.09097297249134609, 0.11779226570679799, 0.14787139962642126}, {0.06651785969666313, 0.08354554480850344, 0.0810643274872189}},
          {{-128.64193725585938f, 168.6591339111328f, 191.54769897460938f}, {0.05000000074505806f, 0.05999999865889549f, 0.019999999552965164f}, 1, {0.9999999965903957, 0.36224999947801856, 1.0}, {0.586830517963803, 0.33895795754629465, 0.27501148590845426}, {0.5183437283760702, 0.2963362762071396, 0.2415203797094519}, {0.27091343304875587, 0.16072860857218482, 0.12302544472528652}},
          {{-265.9530334472656f, 350.6865539550781f, 88.84016418457031f}, {2.5f, 0.009999999776482582f, 0.0f}, 1, {0.797139967333287, 0.5812499932205529, 1.0}, {1.785568271985852, 0.39437449141459446, 0.3837268116562954}, {1.292073773087556, 0.3492086983281142, 0.34109049925004037}, {1.0367231835273092, 0.1759180746955966, 0.17054524962502018}},
          {{-396.5661315917969f, 348.7945556640625f, 911.719482421875f}, {0.0f, 0.0f, 0.0f}, 1, {0.8816161552519696, 1.4872499988436798, 1.0}, {0.781917550171365, 0.46836365133923985, 0.38718990054338165}, {0.6950378223745467, 0.4163232456348799, 0.34416880048300585}, {0.34751891118727335, 0.20816162281743994, 0.17208440024150293}},
          {{-2556.724853515625f, -634.2998046875f, 1237.62939453125f}, {0.20000000298023224f, 0.20000000298023224f, 0.20000000298023224f}, 1, {0.9382541248743983, 4.481250015723063, 1.0}, {0.9084665597147439, 0.5342966879777415, 0.4168661745623923}, {0.7730160268230054, 0.438067643711012, 0.32767189312044176}, {0.401409975019763, 0.23040637111177933, 0.16618888781777133}},
          {{85.07149505615234f, 307.30926513671875f, 50.17070388793945f}, {0.10000000149011612f, 0.4000000059604645f, 0.05000000074505806f}, 1, {0.027969210765100838, 0.20624999004363537, 1.0}, {0.20226165451748615, 0.454812140997435, 0.33660649420902666}, {0.17870317107257108, 0.3947088356229738, 0.29491819129018115}, {0.12105746995186602, 0.3163348185695224, 0.15769439055001283}},
          {{36.796142578125f, 232.7991943359375f, 69.03568267822266f}, {0.8999999761581421f, 0.800000011920929f, 0.699999988079071f}, 1, {0.5713713850435036, 0.11625000636904642, 1.0}, {0.8978384062430834, 0.7797534788888927, 0.6815879127916171}, {0.8268785829716828, 0.7113939430897623, 0.5877565776233448}, {0.7566392830053019, 0.6497832516346439, 0.5000626008842236}},
    };
    const ClayFogConstants real_c = MakeClayFogConstants(real, true), made_c = MakeClayFogConstants(made, true);
    CHECK(581, real_c.on == 1 && made_c.on == 1 && std::size(real_pins) == 10 && std::size(made_pins) == 10);
    {
      int fogged = 0, forms_differ = 0, scale_shows = 0, blend_shows = 0;
      for (int set = 0; set < 2; ++set) {
        const capture::FogBlock& b = set ? made : real;
        const ClayFogConstants& c = set ? made_c : real_c;
        for (const FogPin& p : set ? made_pins : real_pins) {
          const FogPoint at = FogCoords(p.P, eye, c);
          CHECK(581, at.fogged == (p.fogged != 0));
          const double coords[3] = {at.a, at.s, at.t};
          CHECK(581, close3(coords, p.coords, 1e-9));
          double ground[3], tree[3], tree_half[3];
          FogMirror(p.C, p.P, eye, c, b.scatter, b.transmit, ground);
          TreeFogMirror(p.C, p.P, eye, c, 1.0f, b.scatter, b.transmit, tree);
          TreeFogMirror(p.C, p.P, eye, c, 0.5f, b.scatter, b.transmit, tree_half);
          CHECK(581, close3(ground, p.ground, 1e-9) && close3(tree, p.tree, 1e-9) && close3(tree_half, p.tree_half, 1e-9));
          if (!p.fogged) {
            // Inside the start the colour is the one that went in, bit for bit, in both forms.
            CHECK(581, same3(ground, p.C) && same3(tree, p.C) && same3(tree_half, p.C));
            continue;
          }
          ++fogged;
          if (!close3(ground, tree, 1e-6)) ++forms_differ;
          if (!close3(tree, tree_half, 1e-6)) ++scale_shows;
          if (at.t > 0.0 && at.t < 1.0 && !close3(ground, tree, 1e-6)) ++blend_shows;
        }
      }
      // What the pins can tell apart: 17 of the 20 are past a start; with the frame's own registers
      // (amount 1, scale 1, the blend complete) the two forms agree, with the made-up ones they do not;
      // c8.x shows wherever the transmittance is below 1; two pins lie inside the near blend.
      CHECK(581, fogged == 17 && forms_differ == 8 && scale_shows == 15 && blend_shows == 2);
      for (const FogPin& p : real_pins) CHECK(581, close3(p.ground, p.tree, 1e-12));
    }

    // 595: an unlit draw's fog (plan Task 6; the controller's ruling after the review of plan Task 4):
    // the ground form on the unlit colour, with the in-scatter under the lit draws' exposure and, for a
    // shader of the blended form, times the pixel's alpha: light_formulas.fog with
    // scatter_scale = exposure * alpha. The same 20 pixels as case 581, with a made-up exposure and alpha.
    {
      static const struct {
        double plain[3], alpha[3];
      } unlit_pins[] = {
      // unlit pins, the pixels of the real set and then of the made one: the ground form with the in-scatter times the exposure 1.25, and times the alpha 0.4000000059604645 as well
          {{0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}},
          {{0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}},
          {{0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}},
          {{0.06744035841967885, 0.0845525134134241, 0.07405320616980769}, {0.055414935230531925, 0.06771671146082708, 0.04046643325616068}},
          {{0.31676830117912835, 0.1993827242956722, 0.1700401906742589}, {0.14953858435797648, 0.10616236908062408, 0.07505601069668992}},
          {{1.956761734809974, 0.23233913749061083, 0.2664728216060228}, {1.7641450855892367, 0.09662612371732829, 0.10658913023071093}},
          {{0.6464618115124531, 0.4006650643888738, 0.37918545496160355}, {0.2585847284581939, 0.1602660281436994, 0.15167418424476287}},
          {{0.9588190527246266, 0.5418982800106283, 0.40841339577177704}, {0.4014099805571247, 0.23040637420617344, 0.16618889022405564}},
          {{0.14299920285449108, 0.4138832805919214, 0.19077740324636147}, {0.10842079454415085, 0.36573188280793306, 0.0983556042034367}},
          {{0.8862169498718991, 0.7786412442712966, 0.6733016539892881}, {0.8480038301617743, 0.7444912797087849, 0.6219550412432017}},
          {{0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}},
          {{0.3499999940395355, 0.30000001192092896, 0.11999999731779099}, {0.3499999940395355, 0.30000001192092896, 0.11999999731779099}},
          {{0.3651830132765813, 0.30321529856046114, 0.18132542829059478}, {0.351449742732922, 0.2967937496860755, 0.14146466408969455}},
          {{0.11608513097227546, 0.14598515724534683, 0.2123635103700327}, {0.07286228903594033, 0.08957853401691653, 0.09436117015981324}},
          {{0.7260100590854174, 0.4152372705909567, 0.34166488683704727}, {0.3084714398684478, 0.1863993337302712, 0.14170468603769434}},
          {{1.9292029786134908, 0.4918504672078857, 0.47965851457036934}, {1.4982988630112222, 0.19942254273302262, 0.19186340868713525}},
          {{0.9773969377142062, 0.5854545641740497, 0.48398737567922706}, {0.3909587809114222, 0.23418182915920105, 0.1935949531564804}},
          {{1.1174949638540679, 0.6511061538148099, 0.5077003650451445}, {0.4904097576656281, 0.30067775978479816, 0.23519779630395787}},
          {{0.23468736139788277, 0.4988975255900014, 0.41379488212537135}, {0.13741024172305433, 0.36664137312614903, 0.1822297206767305}},
          {{0.9373480124741727, 0.8144094928323968, 0.7309157747073727}, {0.8188191949583827, 0.7104414520347143, 0.5829321904301907}},
      };
      CHECK(595, std::size(unlit_pins) == 20);
      int shown = 0, at_pin = 0;
      for (int set = 0; set < 2; ++set) {
        const capture::FogBlock& b = set ? made : real;
        const ClayFogConstants& c = set ? made_c : real_c;
        for (const FogPin& p : set ? made_pins : real_pins) {
          const auto& u = unlit_pins[at_pin++];
          double plain[3], alpha[3], lit[3], one[3];
          UnlitFogMirror(p.C, p.P, eye, c, 1.25f, 1.0f, b.scatter, b.transmit, plain);
          UnlitFogMirror(p.C, p.P, eye, c, 1.25f, 0.4000000059604645f, b.scatter, b.transmit, alpha);
          CHECK(595, close3(plain, u.plain, 1e-9) && close3(alpha, u.alpha, 1e-9));
          // With an exposure and an alpha of 1 it is the ground form itself, bit for bit.
          FogMirror(p.C, p.P, eye, c, b.scatter, b.transmit, lit);
          UnlitFogMirror(p.C, p.P, eye, c, 1.0f, 1.0f, b.scatter, b.transmit, one);
          CHECK(595, one[0] == lit[0] && one[1] == lit[1] && one[2] == lit[2]);
          if (!p.fogged) {
            CHECK(595, same3(plain, p.C) && same3(alpha, p.C));  // (inside the start: the colour that went in)
          } else if (!close3(plain, lit, 1e-6) && !close3(plain, alpha, 1e-6)) {
            ++shown;
          }
        }
      }
      CHECK(595, shown == 15);  // (wherever the transmittance is below 1)
      // Guards: a frame without fog, an exposure or an alpha that is no number, and no table leave the
      // colour as it went in; an alpha of 0 leaves the transmittance alone on the colour (no in-scatter).
      const float C[3] = {0.35f, 0.3f, 0.12f};
      const float far_point[3] = {eye[0] - 500.0f, eye[1] + 100.0f, eye[2] + 50.0f};
      double out[3];
      UnlitFogMirror(C, far_point, eye, ClayFogConstants{}, 1.25f, 1.0f, real.scatter, real.transmit, out);
      CHECK(595, same3(out, C));
      for (float bad : {qnan, pinf, -pinf}) {
        UnlitFogMirror(C, far_point, eye, real_c, bad, 1.0f, real.scatter, real.transmit, out);
        CHECK(595, same3(out, C));
        UnlitFogMirror(C, far_point, eye, real_c, 1.25f, bad, real.scatter, real.transmit, out);
        CHECK(595, same3(out, C));
      }
      UnlitFogMirror(C, far_point, eye, real_c, 1.25f, 1.0f, static_cast<const float (*)[3]>(nullptr), real.transmit, out);
      CHECK(595, same3(out, C));
      UnlitFogMirror(C, far_point, eye, real_c, 1.25f, 0.0f, real.scatter, real.transmit, out);
      {
        const FogPoint at = FogCoords(far_point, eye, real_c);
        double T[3];
        FogTableRead(real.transmit, kFogTransmitTexels, at.s, T);
        const double want[3] = {double(C[0]) * T[0], double(C[1]) * T[1], double(C[2]) * T[2]};
        CHECK(595, at.fogged && T[0] < 0.9 && close3(out, want, 1e-12));
      }
      // 3e38 for both: a number comes out (their product overflows a single, not the mirror's double).
      UnlitFogMirror(C, far_point, eye, real_c, 3e38f, 3e38f, real.scatter, real.transmit, out);
      CHECK(595, finite3(out));
    }

    // 596: the fog of a draw (DrawFogOf; final review I1, M1, M3, test gap 3): which draws are fogged,
    // in which form, and the bits of light_mode that say so. A frame without fog: nothing. A lit draw:
    // its family's form, unless its own pixel shader in the game has no fog (a shader outside the fog
    // table that is neither a terrain record's nor a tree's, whose fog is the vertex shader's): then
    // none, by a bit the lit path reads. An unlit draw: the unlit form for a shader of the fog table
    // (the blended form's alpha only where the program writes an alpha of its own), the trees' form
    // with the draw's scale for a trunk or a leaf that has one, and nothing otherwise, by reason.
    {
      using capture::LightFamily;
      static_assert(kLightModeUnlitFog == 128u && kLightModeUnlitFogAlpha == 256u && kLightModeLitNoFog == 512u &&
                    kLightModeUnlitFogTree == 1024u);
      static_assert((kLightModeCount & (kLightModeCount - 1)) == 0 && kLightModeUnlitFog == kLightModeCount);  // (above the light's seven bits)
      const uint64_t water = 0xF6D98C7B4D98438Bull, blended = 0x401A01FD4E5F8757ull, lake = 0x2B091BD0C45A7454ull,
                     sky = 0xF7601FBE485936FDull, far_ground = 0x2F8A0341E8C09698ull, bare_plant = 0x26052DEFBA688AF2ull,
                     trunk = 0x00E09D1BC5295D52ull, leaf = 0x7CD57B81550F19E3ull, object = 0x2FD24989AB04A475ull;
      // The table of the rule itself, every combination of its inputs that the names tell apart.
      const struct {
        bool fogged, lit, terrain;
        LightFamily family;
        bool in_table, alpha, own_alpha;
        float scale;
        DrawFog want;
      } rows[] = {
          // a frame without fog: nothing, whatever the draw
          {false, true, false, LightFamily::kPlant, false, false, true, capture::kTreeFogNone, DrawFog::kNone},
          {false, false, false, LightFamily::kNone, true, true, true, capture::kTreeFogNone, DrawFog::kNone},
          {false, false, false, LightFamily::kTreeLeaf, false, false, true, 1.0f, DrawFog::kNone},
          // lit: the family's form with a shader of the table
          {true, true, false, LightFamily::kObject, true, false, true, capture::kTreeFogNone, DrawFog::kLit},
          {true, true, false, LightFamily::kPlant, true, false, true, capture::kTreeFogNone, DrawFog::kLit},
          {true, true, false, LightFamily::kPlantLit, true, false, true, capture::kTreeFogNone, DrawFog::kLit},
          // lit, the shader not in the table: no fog for an object or a plant
          {true, true, false, LightFamily::kPlant, false, false, true, capture::kTreeFogNone, DrawFog::kLitNoFog},
          {true, true, false, LightFamily::kPlantLit, false, false, true, capture::kTreeFogNone, DrawFog::kLitNoFog},
          {true, true, false, LightFamily::kObject, false, false, true, capture::kTreeFogNone, DrawFog::kLitNoFog},
          // lit trees are not in the table and keep their form (the object block's: a scale or none)
          {true, true, false, LightFamily::kTreeTrunk, false, false, true, 1.0f, DrawFog::kLit},
          {true, true, false, LightFamily::kTreeLeaf, false, false, true, capture::kTreeFogNone, DrawFog::kLit},
          // a lit terrain record is fogged (every terrain shader is in the table: case 597)
          {true, true, true, LightFamily::kTerrainLayer, true, false, false, capture::kTreeFogNone, DrawFog::kLit},
          {true, true, true, LightFamily::kTerrainFar, false, false, false, capture::kTreeFogNone, DrawFog::kLit},
          // unlit, a shader of the table: plain; the blended form only with an alpha of the draw's own
          {true, false, false, LightFamily::kNone, true, false, true, capture::kTreeFogNone, DrawFog::kUnlitPlain},
          {true, false, false, LightFamily::kNone, true, true, true, capture::kTreeFogNone, DrawFog::kUnlitAlpha},
          {true, false, false, LightFamily::kNone, true, true, false, capture::kTreeFogNone, DrawFog::kUnlitPlain},
          {true, false, true, LightFamily::kTerrainFar, true, false, false, capture::kTreeFogNone, DrawFog::kUnlitPlain},
          {true, false, false, LightFamily::kObject, true, false, false, capture::kTreeFogNone, DrawFog::kUnlitPlain},
          // unlit trees: the trees' form with a usable scale, none without one
          {true, false, false, LightFamily::kTreeTrunk, false, false, true, 0.0f, DrawFog::kUnlitTree},
          {true, false, false, LightFamily::kTreeLeaf, false, false, false, capture::kTreeFogScaleMax, DrawFog::kUnlitTree},
          {true, false, false, LightFamily::kTreeLeaf, false, false, true, capture::kTreeFogNone, DrawFog::kUnlitTreeNoScale},
          {true, false, false, LightFamily::kTreeTrunk, false, false, true, 4.001f, DrawFog::kUnlitTreeNoScale},
          {true, false, false, LightFamily::kTreeTrunk, false, false, true, qnan, DrawFog::kUnlitTreeNoScale},
          {true, false, false, LightFamily::kTreeTrunk, false, false, true, pinf, DrawFog::kUnlitTreeNoScale},
          // unlit, no entry and no tree: nothing
          {true, false, false, LightFamily::kNone, false, false, true, 1.0f, DrawFog::kUnlitNoFog},
          {true, false, false, LightFamily::kPlant, false, false, true, capture::kTreeFogNone, DrawFog::kUnlitNoFog},
      };
      for (const auto& r : rows) {
        DrawFogInputs in;
        in.frame_fogged = r.fogged;
        in.lit = r.lit;
        in.terrain = r.terrain;
        in.family = r.family;
        in.in_table = r.in_table;
        in.alpha = r.alpha;
        in.own_alpha = r.own_alpha;
        in.tree_scale = r.scale;
        CHECK(596, DrawFogOf(in) == r.want);
      }
      // The bits: none for a draw that is fogged by its family or not at all; they never touch the
      // light's seven bits, and each form has its own.
      CHECK(596, DrawFogMode(DrawFog::kNone) == 0 && DrawFogMode(DrawFog::kLit) == 0 && DrawFogMode(DrawFog::kUnlitNoFog) == 0 &&
                     DrawFogMode(DrawFog::kUnlitTreeNoScale) == 0);
      CHECK(596, DrawFogMode(DrawFog::kLitNoFog) == 512u && DrawFogMode(DrawFog::kUnlitPlain) == 128u &&
                     DrawFogMode(DrawFog::kUnlitAlpha) == 384u && DrawFogMode(DrawFog::kUnlitTree) == 1024u);
      for (int f = 0; f < int(DrawFog::kCount); ++f) CHECK(596, (DrawFogMode(DrawFog(f)) & (kLightModeCount - 1)) == 0);
      // The scale an unlit tree's draw carries: its own times the frame's exposure, and 0 for every
      // other draw and for a product that is no number.
      CHECK(596, DrawFogScale(DrawFog::kUnlitTree, 0.75f, 1.25f) == 0.9375f && DrawFogScale(DrawFog::kUnlitTree, 0.75f, 1.0f) == 0.75f &&
                     DrawFogScale(DrawFog::kUnlitPlain, 0.75f, 1.25f) == 0.0f &&
                     DrawFogScale(DrawFog::kUnlitTreeNoScale, capture::kTreeFogNone, 1.25f) == 0.0f &&
                     DrawFogScale(DrawFog::kLit, 1.0f, 1.25f) == 0.0f && DrawFogScale(DrawFog::kUnlitTree, 0.75f, qnan) == 0.0f &&
                     DrawFogScale(DrawFog::kUnlitTree, 3e38f, 3e38f) == 0.0f);
      // A record's: the family and the entry by its pixel shader, the scale from its light inputs.
      capture::LightInputs tree_inputs;
      tree_inputs.family = LightFamily::kTreeTrunk;
      tree_inputs.tree_fog = 0.5f;
      capture::LightInputs bare_inputs = tree_inputs;
      bare_inputs.tree_fog = capture::kTreeFogNone;
      CHECK(596, RecordDrawFog(true, false, false, true, water, nullptr) == DrawFog::kUnlitPlain &&
                     RecordDrawFog(true, false, false, true, blended, nullptr) == DrawFog::kUnlitAlpha &&
                     RecordDrawFog(true, false, false, false, blended, nullptr) == DrawFog::kUnlitPlain &&  // (M3: a flat draw has no alpha)
                     RecordDrawFog(true, false, true, false, far_ground, nullptr) == DrawFog::kUnlitPlain);
      CHECK(596, RecordDrawFog(true, false, false, true, lake, nullptr) == DrawFog::kUnlitNoFog &&
                     RecordDrawFog(true, false, false, true, sky, nullptr) == DrawFog::kUnlitNoFog &&
                     RecordDrawFog(true, false, false, true, 0, nullptr) == DrawFog::kUnlitNoFog &&
                     RecordDrawFog(true, false, false, true, water + 1, nullptr) == DrawFog::kUnlitNoFog);
      CHECK(596, RecordDrawFog(false, false, false, true, water, nullptr) == DrawFog::kNone &&
                     RecordDrawFog(false, true, false, true, bare_plant, nullptr) == DrawFog::kNone);
      // I1: the plant form without fog is lit and not fogged; unlit it is not fogged either.
      CHECK(596, RecordDrawFog(true, true, false, true, bare_plant, nullptr) == DrawFog::kLitNoFog &&
                     RecordDrawFog(true, false, false, true, bare_plant, nullptr) == DrawFog::kUnlitNoFog &&
                     RecordDrawFog(true, true, false, true, object, nullptr) == DrawFog::kLit);
      // M1: a tree, lit or not, with light inputs or without.
      CHECK(596, RecordDrawFog(true, true, false, true, trunk, &tree_inputs) == DrawFog::kLit &&
                     RecordDrawFog(true, true, false, true, leaf, &bare_inputs) == DrawFog::kLit &&
                     RecordDrawFog(true, false, false, true, trunk, &tree_inputs) == DrawFog::kUnlitTree &&
                     RecordDrawFog(true, false, false, false, leaf, &tree_inputs) == DrawFog::kUnlitTree &&
                     RecordDrawFog(true, false, false, true, trunk, &bare_inputs) == DrawFog::kUnlitTreeNoScale &&
                     RecordDrawFog(true, false, false, true, leaf, nullptr) == DrawFog::kUnlitTreeNoScale);
      // Every entry of the fog table, unlit: its form; lit: its family's.
      int plain = 0, alpha = 0;
      for (const capture::FogShaderEntry& e : capture::kFogShaderTable) {
        CHECK(596, RecordDrawFog(true, false, false, true, e.ps_hash, nullptr) == (e.alpha ? DrawFog::kUnlitAlpha : DrawFog::kUnlitPlain) &&
                       RecordDrawFog(true, false, false, false, e.ps_hash, nullptr) == DrawFog::kUnlitPlain &&
                       RecordDrawFog(false, false, false, true, e.ps_hash, nullptr) == DrawFog::kNone &&
                       RecordDrawFog(true, true, false, true, e.ps_hash, nullptr) == DrawFog::kLit);
        ++(e.alpha ? alpha : plain);
      }
      CHECK(596, plain == 30 && alpha == 10);
      // A mode with the fog's bits names the light buffers of the mode without them (the vertex stage
      // reads none of the four).
      for (uint32_t mode : {0u, 1u, 6u, 14u, 22u, 54u}) {
        for (uint32_t bits : {128u, 384u, 512u, 1024u}) {
          const ClayLightBuffers with = LightBufferSources(mode | bits), without = LightBufferSources(mode);
          for (uint32_t i = 0; i < kClayLightBufferCount; ++i) CHECK(596, with.source[i] == without.source[i]);
        }
      }
      // The counts of the Fog line that follow from a drawn record's fog.
      {
        ClayStats st;
        for (DrawFog f : {DrawFog::kNone, DrawFog::kLit, DrawFog::kLitNoFog, DrawFog::kLitNoFog, DrawFog::kUnlitPlain, DrawFog::kUnlitAlpha,
                          DrawFog::kUnlitAlpha, DrawFog::kUnlitTree, DrawFog::kUnlitTreeNoScale, DrawFog::kUnlitNoFog, DrawFog::kUnlitNoFog,
                          DrawFog::kUnlitNoFog}) {
          CountDrawFog(f, st);
        }
        CHECK(596, st.fog_lit_none == 2 && st.fog_unlit == 4 && st.fog_unlit_alpha == 2 && st.fog_unlit_tree == 1 &&
                       st.fog_unlit_tree_none == 1 && st.fog_unlit_none == 3 && st.fog_trees == 0 && st.fog_trees_none == 0);
      }
    }

    // 597: the light table against the fog table (final review, test gap 3): which lit families the
    // game fogs. Every pixel shader with a light family is in the fog table, or is a tree's (its fog is
    // the vertex shader's), or is the one plant form without fog, 26052DEF...; every terrain shader is
    // in the table. A new shader of either table that breaks this fails here, where the rule is.
    {
      using capture::LightFamily;
      int entries = 0, in_table = 0, trees = 0, bare = 0;
      uint64_t bare_hash = 0;
      for (const capture::PsLightFamilyEntry& e : capture::kPsLightFamilies) {
        ++entries;
        const bool fogged = capture::FindFogShader(e.ps_hash) != nullptr;
        const bool tree = e.family == LightFamily::kTreeTrunk || e.family == LightFamily::kTreeLeaf;
        CHECK(597, e.family != LightFamily::kNone && size_t(e.family) < size_t(LightFamily::kCount));
        CHECK(597, capture::PsLightFamily(e.ps_hash) == e.family);
        CHECK(597, !(tree && fogged));  // (a tree's pixel shader reads no fog register)
        if (e.family == LightFamily::kTerrainLayer || e.family == LightFamily::kTerrainFar) CHECK(597, fogged);
        if (fogged) {
          ++in_table;
        } else if (tree) {
          ++trees;
        } else {
          ++bare;
          bare_hash = e.ps_hash;
          CHECK(597, e.family == LightFamily::kPlant);
        }
        // What the rule makes of it, lit: fogged unless it is the bare one.
        const DrawFog lit = RecordDrawFog(true, true, false, true, e.ps_hash, nullptr);
        CHECK(597, lit == (fogged || tree ? DrawFog::kLit : DrawFog::kLitNoFog));
      }
      CHECK(597, entries == 21 && in_table == 16 && trees == 4 && bare == 1 && bare_hash == 0x26052DEFBA688AF2ull);
      CHECK(597, capture::PsLightFamily(0) == LightFamily::kNone && capture::PsLightFamily(0xF6D98C7B4D98438Bull) == LightFamily::kNone &&
                     capture::PsLightFamily(0x7CD57B81550F19E3ull) == LightFamily::kTreeLeaf &&
                     capture::PsLightFamily(0x26052DEFBA688AF2ull) == LightFamily::kPlant);
    }

    // 598: the slot plan of the frame's constant buffer (final review, test gap 1): the frame's block at
    // the head, then the terrain records drawn with their material, the lit plants, the mask step's and
    // the sky step's constants, one small slot each, the post steps' 25 small slots (plan Task 5 of the
    // post sub-project), then the large slots. No two uses share a slot, and
    // the buffer sized for the frame (every terrain record, drawn with its material or not) holds them.
    {
      static_assert(kClayFrameSlots == 21);
      const ClayFrameSlotPlan none = PlanClayFrameSlots(0, 0);
      CHECK(598, kClayFrameTerrainSlot0 == 21 && none.plant0 == 21 && none.mask == 21 && none.sky == 22 && none.post == 23 &&
                     none.quad0 == 48 && none.object0 == 96);
      CHECK(598, ClayFrameSmallSlots(0, 0) == 96 && ClayFrameSmallSlots(7, 3) == 106 && kClayPostSlots == 25);
      const ClayFrameSlotPlan some = PlanClayFrameSlots(5, 3);
      CHECK(598, some.plant0 == 26 && some.mask == 29 && some.sky == 30 && some.post == 31 && some.quad0 == 56 && some.object0 == 104);
      for (uint32_t records : {0u, 1u, 5u, 600u}) {
        for (uint32_t drawn : {0u, 1u, 5u, 600u}) {
          if (drawn > records) continue;
          for (uint32_t plants : {0u, 1u, 140u}) {
            for (uint32_t objects : {0u, 1u, 200u}) {
              const ClayFrameSlotPlan p = PlanClayFrameSlots(drawn, plants);
              const uint64_t bytes = ClayConstantBufferBytes(ClayFrameSmallSlots(records, plants), objects);
              // In order, nothing shared: frame | terrain | plants | mask | sky | post | objects.
              CHECK(598, kClayFrameTerrainSlot0 + drawn == p.plant0 && p.plant0 + plants == p.mask && p.mask + 1 == p.sky &&
                             p.sky + 1 == p.post && p.post + kClayPostSlots == p.quad0 && p.quad0 + kClayQuadFrameSlots == p.object0);
              CHECK(598, p.object0 <= ClayFrameSmallSlots(records, plants));
              // The frame's block lies before the first other slot; the last byte any slot's block can
              // be read at lies inside the buffer (a small slot is bound at b1 too: cbuffer Object).
              CHECK(598, sizeof(ClayFrameConstants) <= ClaySmallSlotOffset(kClayFrameTerrainSlot0));
              CHECK(598, ClaySmallSlotOffset(p.sky) + kClayObjectSlotBytes <= bytes);
              CHECK(598, ClaySmallSlotOffset(p.mask) + sizeof(ClayMaskConstants) <= ClaySmallSlotOffset(p.sky) &&
                             ClaySmallSlotOffset(p.sky) + sizeof(ClaySkyConstants) <= ClaySmallSlotOffset(p.post) &&
                             ClaySmallSlotOffset(p.post) + sizeof(ClayPostConstants) <= ClaySmallSlotOffset(p.object0) &&
                             ClaySmallSlotOffset(p.post) + sizeof(ClayPostConstants) <= bytes);
              if (objects) CHECK(598, ClayObjectSlotOffset(p.object0, objects - 1) + kClayObjectSlotBytes <= bytes);
            }
          }
        }
      }
    }

    // 599: an unlit trunk's or leaf's fog (final review, M1): the trees' form on the unlit colour, the
    // in-scatter times the draw's scale and the frame's exposure (which an unlit value is not multiplied
    // by afterwards). With an exposure of 1 it is the trees' form itself; the in-scatter it adds is
    // linear in the exposure; guards as the trees' form.
    {
      int shown = 0;
      for (int set = 0; set < 2; ++set) {
        const capture::FogBlock& b = set ? made : real;
        const ClayFogConstants& c = set ? made_c : real_c;
        for (const FogPin& p : set ? made_pins : real_pins) {
          double tree[3], one[3], unlit[3], bare[3];
          TreeFogMirror(p.C, p.P, eye, c, 0.75f, b.scatter, b.transmit, tree);
          UnlitTreeFogMirror(p.C, p.P, eye, c, 0.75f, 1.0f, b.scatter, b.transmit, one);
          CHECK(599, one[0] == tree[0] && one[1] == tree[1] && one[2] == tree[2]);
          UnlitTreeFogMirror(p.C, p.P, eye, c, 0.75f, 1.25f, b.scatter, b.transmit, unlit);
          UnlitTreeFogMirror(p.C, p.P, eye, c, 0.0f, 1.25f, b.scatter, b.transmit, bare);  // (the colour through the transmittance alone)
          if (!p.fogged) {
            CHECK(599, same3(unlit, p.C) && same3(bare, p.C));
            continue;
          }
          bool differs = false;
          for (int k = 0; k < 3; ++k) {
            CHECK(599, std::fabs((unlit[k] - bare[k]) - 1.25 * (tree[k] - bare[k])) < 1e-12);
            differs = differs || std::fabs(unlit[k] - tree[k]) > 1e-6;
          }
          shown += differs ? 1 : 0;
        }
      }
      CHECK(599, shown == 15);  // (wherever the transmittance is below 1: as the ground form's pins, case 595)
      const float C[3] = {0.35f, 0.3f, 0.12f};
      const float far_point[3] = {eye[0] - 500.0f, eye[1] + 100.0f, eye[2] + 50.0f};
      double out[3];
      UnlitTreeFogMirror(C, far_point, eye, ClayFogConstants{}, 0.75f, 1.25f, real.scatter, real.transmit, out);
      CHECK(599, same3(out, C));
      for (float bad : {qnan, pinf, -pinf}) {
        UnlitTreeFogMirror(C, far_point, eye, real_c, bad, 1.25f, real.scatter, real.transmit, out);
        CHECK(599, same3(out, C));
        UnlitTreeFogMirror(C, far_point, eye, real_c, 0.75f, bad, real.scatter, real.transmit, out);
        CHECK(599, same3(out, C));
      }
      UnlitTreeFogMirror(C, far_point, eye, real_c, 3e38f, 3e38f, real.scatter, real.transmit, out);
      CHECK(599, finite3(out));
    }

    // 582: guards (review focus 2; review of plan Task 3, M2). Whatever goes in, a number comes out,
    // and a pixel the fog cannot be computed for keeps its colour.
    {
      const float C[3] = {0.35f, 0.3f, 0.12f};
      const float far_point[3] = {eye[0] - 500.0f, eye[1] + 100.0f, eye[2] + 50.0f};
      double out[3], tree[3];
      auto both = [&](const float colour[3], const float P[3], const float e[3], const ClayFogConstants& c, const capture::FogBlock& b,
                      float c8x = 1.0f) {
        FogMirror(colour, P, e, c, b.scatter, b.transmit, out);
        TreeFogMirror(colour, P, e, c, c8x, b.scatter, b.transmit, tree);
        return finite3(out) && finite3(tree);
      };
      auto unfogged = [&] { return same3(out, C) && same3(tree, C); };
      // The far point is fogged (so that the cases below show something when they leave it alone).
      CHECK(582, both(C, far_point, eye, real_c, real) && !unfogged() && FogCoords(far_point, eye, real_c).fogged);
      // d = 0, the pixel at the eye: inside any start, a start of 0 included (dD > 0 is false); no
      // direction is formed.
      CHECK(582, both(C, eye, eye, real_c, real) && unfogged() && !FogCoords(eye, eye, real_c).fogged);
      ClayFogConstants at_eye = real_c;
      at_eye.start = 0.0f;
      CHECK(582, both(C, eye, eye, at_eye, real) && unfogged() && !FogCoords(eye, eye, at_eye).fogged);
      {
        const float beside[3] = {eye[0] + 0.001f, eye[1], eye[2]};  // (with a start of 0 the nearest pixel is fogged, and is a number)
        CHECK(582, both(C, beside, eye, at_eye, real) && FogCoords(beside, eye, at_eye).fogged);
      }
      // A start below 0 (the block refuses one; fed by hand): the pixel at the eye would be past it,
      // but it has no direction: unfogged. (The reference reads the angle as 0 there and fogs; the
      // product never has such a start.)
      {
        ClayFogConstants below = real_c;
        below.start = -1.0f;
        CHECK(582, both(C, eye, eye, below, real) && unfogged() && !FogCoords(eye, eye, below).fogged);
      }
      // Exactly at the start (dD = 0) a pixel is not fogged: the game's test is dD > 0. A pixel 5 units
      // from an eye at the origin, with a start of 5 and tables whose first texels are not neutral.
      {
        capture::FogBlock b = real;
        for (auto& texel : b.transmit) texel[0] = texel[1] = texel[2] = 0.5f;
        ClayFogConstants c = real_c;
        c.start = 5.0f;
        const float origin[3] = {0.0f, 0.0f, 0.0f}, at_start[3] = {3.0f, 4.0f, 0.0f}, past[3] = {3.0f, 4.0f, 0.125f};
        CHECK(582, both(C, at_start, origin, c, b) && unfogged() && !FogCoords(at_start, origin, c).fogged);
        CHECK(582, both(C, past, origin, c, b) && !unfogged() && FogCoords(past, origin, c).fogged);
      }
      // A position or an eye that is no number, in any component: unfogged.
      for (float bad : {qnan, pinf, -pinf}) {
        for (int k = 0; k < 3; ++k) {
          float P[3] = {far_point[0], far_point[1], far_point[2]}, e[3] = {eye[0], eye[1], eye[2]};
          P[k] = bad;
          CHECK(582, both(C, P, eye, real_c, real) && unfogged() && !FogCoords(P, eye, real_c).fogged);
          e[k] = bad;
          CHECK(582, both(C, far_point, e, real_c, real) && unfogged());
        }
      }
      // A position beyond the distance the fog is computed for (kFogDistance2Max: its square would not
      // be a number in single precision): unfogged. Just inside it: fogged with the table's last texel,
      // as any pixel beyond the table's end in that direction.
      {
        static_assert(kFogDistance2Max == 1e30f);
        const float beyond[3] = {eye[0] - 2e15f, eye[1], eye[2]}, huge[3] = {3e38f, -3e38f, 3e38f};
        CHECK(582, both(C, beyond, eye, real_c, real) && unfogged());
        CHECK(582, both(C, huge, eye, real_c, real) && unfogged());
        const float inside[3] = {eye[0] - 5e14f, eye[1], eye[2]}, end[3] = {eye[0] - 5000.0f, eye[1], eye[2]};
        double ref[3], ref_tree[3];
        FogMirror(C, end, eye, real_c, real.scatter, real.transmit, ref);
        TreeFogMirror(C, end, eye, real_c, 1.0f, real.scatter, real.transmit, ref_tree);
        CHECK(582, both(C, inside, eye, real_c, real) && !unfogged() && close3(out, ref, 1e-9) && close3(tree, ref_tree, 1e-9));
        const FogPoint at = FogCoords(inside, eye, real_c);
        CHECK(582, at.fogged && at.s > 1e9 && at.t == 1.0 && std::isfinite(at.s));
      }
      // A zero axis (the block refuses one; the function is finite all the same): the angle reads 0,
      // the table's first texel.
      {
        ClayFogConstants c = real_c;
        c.axis[0] = c.axis[1] = c.axis[2] = 0.0f;
        const FogPoint at = FogCoords(far_point, eye, c);
        CHECK(582, both(C, far_point, eye, c, real) && at.fogged && at.a == 0.0);
      }
      // A constant that is no number (never made by MakeClayFogConstants; fed here by hand): unfogged.
      for (float bad : {qnan, pinf, -pinf}) {
        for (int what = 0; what < 11; ++what) {
          ClayFogConstants c = made_c;
          float* fields[] = {&c.axis[0], &c.axis[1], &c.axis[2], &c.near_colour[0], &c.near_colour[1], &c.near_colour[2],
                             &c.start, &c.slope, &c.scale, &c.amount, &c.scatter};
          *fields[what] = bad;
          CHECK(582, both(C, far_point, eye, c, made));
          // (the trees' form reads neither c68, the slope nor the two amounts: it may fog; the ground form may not)
          CHECK(582, same3(out, C));
          if (what < 3 || what == 6 || what == 8) CHECK(582, same3(tree, C));
        }
      }
      // A table texel that is no number (the block refuses one): unfogged where it is read.
      for (float bad : {qnan, pinf, -pinf}) {
        capture::FogBlock b = real;
        for (auto& texel : b.scatter) texel[1] = bad;
        CHECK(582, both(C, far_point, eye, real_c, b) && unfogged());
        b = real;
        for (auto& texel : b.transmit) texel[2] = bad;
        CHECK(582, both(C, far_point, eye, real_c, b) && unfogged());
      }
      // The largest values the block lets through (kFogRegisterLimit, a half float's 65,504, amounts of
      // 4) give numbers: no product overflows.
      {
        capture::FogBlock b = real;
        for (auto& texel : b.scatter) texel[0] = texel[1] = texel[2] = 65504.0f;
        for (auto& texel : b.transmit) texel[0] = texel[1] = texel[2] = 0.0f;
        b.amount[0] = b.amount[1] = 4.0f;
        b.range[0] = 0.0f;
        b.range[2] = b.range[3] = capture::kFogRegisterLimit;
        for (float& v : b.near_colour) v = -capture::kFogRegisterLimit;
        const ClayFogConstants c = MakeClayFogConstants(b, true);
        const float bright[3] = {64.0f, 64.0f, 64.0f};
        CHECK(582, c.on == 1);
        FogMirror(bright, far_point, eye, c, b.scatter, b.transmit, out);
        TreeFogMirror(bright, far_point, eye, c, capture::kTreeFogScaleMax, b.scatter, b.transmit, tree);
        CHECK(582, finite3(out) && finite3(tree) && std::fabs(tree[0] - 4.0 * 65504.0) < 1e-6);
      }
      // A colour channel that is no number counts as 0, fogged or not (as the light functions' LightFinite).
      for (float bad : {qnan, pinf, -pinf}) {
        const float colour[3] = {bad, 0.3f, bad};
        const float zeroed[3] = {0.0f, 0.3f, 0.0f};
        double want[3], want_tree[3];
        FogMirror(zeroed, far_point, eye, real_c, real.scatter, real.transmit, want);
        TreeFogMirror(zeroed, far_point, eye, real_c, 1.0f, real.scatter, real.transmit, want_tree);
        CHECK(582, both(colour, far_point, eye, real_c, real) && close3(out, want, 0.0) && close3(tree, want_tree, 0.0));
        CHECK(582, both(colour, eye, eye, real_c, real) && same3(out, zeroed) && same3(tree, zeroed));
      }
      // A tree's scale that is no number: unfogged (the pass never passes one: LightInputs::tree_fog).
      for (float bad : {qnan, pinf, -pinf}) {
        TreeFogMirror(C, far_point, eye, real_c, bad, real.scatter, real.transmit, tree);
        CHECK(582, same3(tree, C));
      }
      // Without the fog (constants that are off, whatever else they hold): the colour, in both forms.
      {
        ClayFogConstants off = made_c;
        off.on = 0;
        CHECK(582, both(C, far_point, eye, off, made) && unfogged() && !FogCoords(far_point, eye, off).fogged);
        CHECK(582, both(C, far_point, eye, ClayFogConstants{}, real) && unfogged());
      }
      // No table: unfogged.
      FogMirror(C, far_point, eye, real_c, static_cast<const float (*)[3]>(nullptr), real.transmit, out);
      TreeFogMirror(C, far_point, eye, real_c, 1.0f, real.scatter, static_cast<const float (*)[3]>(nullptr), tree);
      CHECK(582, unfogged());
    }

    // 583: the frame's fog constants from its block. On only for a wanted, valid block whose every value
    // is a number inside the block's own bounds; else all zero (a frame drawn without fog).
    {
      static_assert(sizeof(ClayFogConstants) == 48 && offsetof(ClayFogConstants, on) == 12 && offsetof(ClayFogConstants, near_colour) == 16 &&
                    offsetof(ClayFogConstants, start) == 28 && offsetof(ClayFogConstants, slope) == 32 && offsetof(ClayFogConstants, scale) == 36 &&
                    offsetof(ClayFogConstants, amount) == 40 && offsetof(ClayFogConstants, scatter) == 44);
      const ClayFogConstants zero{};
      auto is_zero = [&](const ClayFogConstants& c) { return std::memcmp(&c, &zero, sizeof(c)) == 0; };
      CHECK(583, real_c.axis[0] == real.axis[0] && real_c.axis[1] == real.axis[1] && real_c.axis[2] == real.axis[2] && real_c.on == 1 &&
                     real_c.near_colour[0] == real.near_colour[0] && real_c.near_colour[1] == real.near_colour[1] &&
                     real_c.near_colour[2] == real.near_colour[2] && real_c.start == real.range[0] && real_c.slope == real.range[2] &&
                     real_c.scale == real.range[3] && real_c.amount == real.amount[0] && real_c.scatter == real.amount[1]);
      CHECK(583, made_c.start == 12.5f && made_c.slope == 0.019999999552965164f && made_c.scale == 0.001500000013038516f &&
                     made_c.amount == 0.75f && made_c.scatter == 1.5f && made_c.near_colour[2] == 0.8999999761581421f);
      CHECK(583, is_zero(MakeClayFogConstants(real, false)) && is_zero(MakeClayFogConstants(capture::FogBlock{}, true)));
      {
        capture::FogBlock b = real;
        b.valid = false;  // (a block that says it is not valid is not read, whatever it holds)
        CHECK(583, is_zero(MakeClayFogConstants(b, true)));
      }
      // The sky's numbers in the block (c67.y, c64, c65) are not the fog's: nothing of them is judged.
      {
        capture::FogBlock b = real;
        b.range[1] = -5.0f;
        b.ext[0][0] = qnan;
        b.ext[1][2] = -pinf;
        const ClayFogConstants c = MakeClayFogConstants(b, true);
        CHECK(583, c.on == 1 && std::memcmp(&c, &real_c, sizeof(c)) == 0);
      }
      // A block marked valid that holds what MakeFogBlock would have refused: no fog.
      for (float bad : {qnan, pinf, -pinf, 2e6f, -2e6f}) {
        for (int what = 0; what < 11; ++what) {
          capture::FogBlock b = real;
          float* fields[] = {&b.axis[0], &b.axis[1], &b.axis[2], &b.near_colour[0], &b.near_colour[1], &b.near_colour[2],
                             &b.range[0], &b.range[2], &b.range[3], &b.amount[0], &b.amount[1]};
          *fields[what] = bad;
          CHECK(583, is_zero(MakeClayFogConstants(b, true)));
        }
      }
      {
        capture::FogBlock b = real;
        b.range[0] = -0.5f;  // a start below 0
        CHECK(583, is_zero(MakeClayFogConstants(b, true)));
        b = real;
        b.range[2] = -0.5f;  // a slope below 0
        CHECK(583, is_zero(MakeClayFogConstants(b, true)));
        b = real;
        b.range[3] = 0.0f;   // a distance scale that is not above 0
        CHECK(583, is_zero(MakeClayFogConstants(b, true)));
        for (int k = 0; k < 2; ++k) {
          b = real;
          b.amount[k] = 4.5f;
          CHECK(583, is_zero(MakeClayFogConstants(b, true)));
          b.amount[k] = -0.5f;
          CHECK(583, is_zero(MakeClayFogConstants(b, true)));
          b.amount[k] = 4.0f;
          CHECK(583, MakeClayFogConstants(b, true).on == 1);
          b.amount[k] = 0.0f;
          CHECK(583, MakeClayFogConstants(b, true).on == 1);
        }
      }
      // A tree draw's scale: usable inside [0, kTreeFogScaleMax]; the marker of a draw without one is not.
      CHECK(583, TreeFogScaleUsable(0.0f) && TreeFogScaleUsable(1.0f) && TreeFogScaleUsable(capture::kTreeFogScaleMax) &&
                     !TreeFogScaleUsable(capture::kTreeFogNone) && !TreeFogScaleUsable(-0.001f) && !TreeFogScaleUsable(4.001f) &&
                     !TreeFogScaleUsable(qnan) && !TreeFogScaleUsable(pinf) && !TreeFogScaleUsable(-pinf) &&
                     !TreeFogScaleUsable(capture::LightInputs{}.tree_fog));
    }

    // 584: the frame's block with the fog (cbuffer Frame: the light's 112 bytes, which every clay shader
    // declares, then the fog's tail, which only the lit pixel shaders declare: the fog's constants, the
    // frame's inverse view-projection and the two tables as arrays; the controller's ruling: no binding
    // parameter is free, so no textures). SetClayFrameFog fills the tail from a valid block or leaves
    // every byte of it zero: nothing of an earlier frame can stay (plan, Review Focus 1).
    // The matrices: frame 1 of the bridge capture of 2026-10-09 (case 563's), the same eye.
    const float fog_inv[4][4] = {{0.5143311619758606f, 0.08813292533159256f, -820.677490234375f, 821.5447387695312f},
                                 {-0.8844040036201477f, 0.05125229433178902f, -1573.3095703125f, 1573.8358154296875f},
                                 {8.367820555577055e-06f, 0.5663930773735046f, -501.68060302734375f, 501.51348876953125f},
                                 {1.6652556666940654e-07f, 7.833264703549503e-09f, -9.999471664428711f, 9.999671936035156f}};
    const float fog_vp[4][4] = {{0.49135205149650574f, -0.8449383974075317f, 2.1847014863851655e-08f, 92.6156997680664f},
                                {0.26610419154167175f, 0.15474604070186615f, 1.7101504802703857f, -131.98681640625f},
                                {0.8508029580116272f, 0.49476245045661926f, -0.17715677618980408f, -138.88465881347656f},
                                {0.8507859110832214f, 0.49475255608558655f, -0.17715322971343994f, -138.78187561035156f}};
    capture::LightBlock light_block{};
    for (int i = 0; i < 3; ++i) {
      light_block.sun_dir[i] = i == 2 ? 1.0f : 0.0f;
      light_block.sun_col[i] = 1.0f;
      light_block.tint[i] = 1.0f;
      light_block.eye[i] = eye[i];
    }
    light_block.tint_pow = 1.3f;
    light_block.valid = true;
    auto lit_frame = [&] {
      ClayFrameConstants c = MakeClayFrameConstants(light_block, ClayLight::kOn);
      SetClayFrameMask(c, 1120, 720);
      return c;
    };
    auto tail_is_zero = [](const ClayFrameConstants& c) {
      static const unsigned char zero[sizeof(ClayFrameConstants) - kClayFrameLightBytes] = {};
      return std::memcmp(reinterpret_cast<const unsigned char*>(&c) + kClayFrameLightBytes, zero, sizeof(zero)) == 0;
    };
    {
      static_assert(sizeof(ClayFrameConstants) == 5344 && kClayFrameLightBytes == 112 && offsetof(ClayFrameConstants, fog) == 112 &&
                    offsetof(ClayFrameConstants, inv_view_proj) == 160 && offsetof(ClayFrameConstants, scatter) == 224 &&
                    offsetof(ClayFrameConstants, transmit) == 1248 && sizeof(ClayFrameConstants::scatter) == 64 * 16 &&
                    sizeof(ClayFrameConstants::transmit) == 256 * 16);
      // The frame's block is the head of the frame's constant buffer: 21 small slots, bound at b2 at
      // offset 0; the other small slots follow it.
      static_assert(kClayFrameSlots == 21 && kClayFrameSlots * kClayTerrainSlotBytes >= sizeof(ClayFrameConstants) &&
                    (kClayFrameSlots - 1) * kClayTerrainSlotBytes < sizeof(ClayFrameConstants));
      CHECK(584, ClaySmallSlotOffset(kClayFrameSlots) == 5376 && ClayConstantBufferBytes(kClayFrameSlots + 1, 0) == 65536);
      // A frame as MakeClayFrameConstants makes it has no fog: the tail is zero.
      ClayFrameConstants c = lit_frame();
      CHECK(584, c.light_on == 1 && c.eye_on == 1 && tail_is_zero(c) && c.fog.on == 0);
      // Filled from a valid block: the constants, the matrix, and every texel (rgb; the fourth float 0).
      CHECK(584, SetClayFrameFog(c, real, true, fog_inv));
      CHECK(584, std::memcmp(&c.fog, &real_c, sizeof(real_c)) == 0 && std::memcmp(c.inv_view_proj, fog_inv, sizeof(fog_inv)) == 0);
      bool tables = true;
      for (uint32_t i = 0; i < kFogScatterTexels; ++i) {
        tables = tables && c.scatter[i][0] == real.scatter[i][0] && c.scatter[i][1] == real.scatter[i][1] &&
                 c.scatter[i][2] == real.scatter[i][2] && c.scatter[i][3] == 0.0f;
      }
      for (uint32_t i = 0; i < kFogTransmitTexels; ++i) {
        tables = tables && c.transmit[i][0] == real.transmit[i][0] && c.transmit[i][1] == real.transmit[i][1] &&
                 c.transmit[i][2] == real.transmit[i][2] && c.transmit[i][3] == 0.0f;
      }
      CHECK(584, tables && c.scatter[63][0] == 0.93310546875f && c.transmit[255][2] == 6.0f / 255.0f);
      // The light's part is not touched.
      const ClayFrameConstants plain = lit_frame();
      CHECK(584, std::memcmp(&c, &plain, kClayFrameLightBytes) == 0);
      // The same constants given a frame that has no fog: every byte of the tail is zero again,
      // whatever the reason. (The pass makes its constants anew each frame; this holds also for one
      // that would not.)
      const ClayFrameConstants filled = c;
      auto refused = [&](const capture::FogBlock& b, bool wanted, const float inverse[4][4], const ClayFrameConstants& from) {
        ClayFrameConstants f = from;
        return !SetClayFrameFog(f, b, wanted, inverse) && tail_is_zero(f) && std::memcmp(&f, &from, kClayFrameLightBytes) == 0;
      };
      CHECK(584, !tail_is_zero(filled));
      CHECK(584, refused(real, false, fog_inv, filled));                 // the switch off
      CHECK(584, refused(capture::FogBlock{}, true, fog_inv, filled));   // no block
      CHECK(584, refused(real, true, nullptr, filled));                  // no view
      {
        capture::FogBlock b = real;
        b.valid = false;
        CHECK(584, refused(b, true, fog_inv, filled));
        b = real;
        b.range[3] = 0.0f;  // (a value MakeClayFogConstants refuses)
        CHECK(584, refused(b, true, fog_inv, filled));
        for (float bad : {qnan, pinf, -pinf}) {
          b = real;
          b.scatter[17][1] = bad;
          CHECK(584, refused(b, true, fog_inv, filled));
          b = real;
          b.transmit[200][0] = bad;
          CHECK(584, refused(b, true, fog_inv, filled));
          float m[4][4];
          std::memcpy(m, fog_inv, sizeof(m));
          m[3][2] = bad;
          CHECK(584, refused(real, true, m, filled));
        }
      }
      {
        // A frame without light, without an eye or without the target's size has no fog.
        ClayFrameConstants f = filled;
        f.light_on = 0;
        CHECK(584, refused(real, true, fog_inv, f));
        f = filled;
        f.eye_on = 0;
        CHECK(584, refused(real, true, fog_inv, f));
        f = filled;
        f.mask_du = 0.0f;
        CHECK(584, refused(real, true, fog_inv, f));
        f = filled;
        f.mask_dv = qnan;
        CHECK(584, refused(real, true, fog_inv, f));
        CHECK(584, refused(real, true, fog_inv, ClayFrameConstants{}));
      }
      // Another frame's block replaces the first one's wholly.
      ClayFrameConstants again = filled;
      CHECK(584, SetClayFrameFog(again, made, true, fog_inv) && again.fog.start == 12.5f && again.fog.amount == 0.75f);
    }

    // 585: the decision about the frame's fog, one chain in which the first thing missing decides, and
    // the pass's frame counts and line that follow from it.
    {
      ClayFogInputs all;
      all.fog = all.light = all.light_block = all.block = all.values = all.eye = all.filled = true;
      auto is = [](const ClayFogInputs& in, ClayFog fog, FrameViewReason view) {
        const ClayFogDecision d = DecideClayFog(in);
        return d.fog == fog && d.view == view;
      };
      CHECK(585, is(all, ClayFog::kDrawn, FrameViewReason::kNone) && is(ClayFogInputs{}, ClayFog::kOff, FrameViewReason::kNone));
      const struct {
        bool ClayFogInputs::*field;
        ClayFog fog;
        FrameViewReason view;
      } rows[] = {{&ClayFogInputs::fog, ClayFog::kOff, FrameViewReason::kNone},
                  {&ClayFogInputs::light, ClayFog::kNoLight, FrameViewReason::kNone},
                  {&ClayFogInputs::light_block, ClayFog::kNoLight, FrameViewReason::kNone},
                  {&ClayFogInputs::block, ClayFog::kBlock, FrameViewReason::kNone},
                  {&ClayFogInputs::values, ClayFog::kValues, FrameViewReason::kNone},
                  {&ClayFogInputs::eye, ClayFog::kView, FrameViewReason::kEye},
                  {&ClayFogInputs::filled, ClayFog::kValues, FrameViewReason::kNone}};
      for (size_t i = 0; i < std::size(rows); ++i) {
        // That one thing missing alone; and that one with everything behind it in the chain missing too.
        ClayFogInputs one = all;
        one.*rows[i].field = false;
        CHECK(585, is(one, rows[i].fog, rows[i].view));
        ClayFogInputs rest = all;
        for (size_t j = i; j < std::size(rows); ++j) rest.*rows[j].field = false;
        if (i < 6) rest.view = FrameViewReason::kDisagree;  // (the view is asked after the eye and before the fill)
        CHECK(585, is(rest, rows[i].fog, rows[i].view));
      }
      for (FrameViewReason why : {FrameViewReason::kNoRecord, FrameViewReason::kDisagree, FrameViewReason::kSingular}) {
        ClayFogInputs v = all;
        v.view = why;
        CHECK(585, is(v, ClayFog::kView, why));
        v.filled = false;  // (the view decides before the fill)
        CHECK(585, is(v, ClayFog::kView, why));
        v.eye = false;
        CHECK(585, is(v, ClayFog::kView, FrameViewReason::kEye));
      }
      // The counts: frames with the fog's switch on (every verdict but kOff), and those of them without
      // fog, by what was missing. "fog-block" is the three reasons together; a frame drawn without light
      // (the light's switch off among them: final review, M5) is counted "no-light".
      auto counts = [](ClayFog fog) {
        ClayFogFrames n;
        CountClayFogFrame(fog, n);
        return n;
      };
      auto same = [](const ClayFogFrames& n, uint64_t frames, uint64_t block, uint64_t values, uint64_t view, uint64_t nolight) {
        return n.frames == frames && n.block == block && n.values == values && n.view == view && n.nolight == nolight;
      };
      CHECK(585, same(counts(ClayFog::kDrawn), 1, 0, 0, 0, 0) && same(counts(ClayFog::kBlock), 1, 1, 0, 0, 0) &&
                     same(counts(ClayFog::kValues), 1, 0, 1, 0, 0) && same(counts(ClayFog::kView), 1, 0, 0, 1, 0) &&
                     same(counts(ClayFog::kNoLight), 1, 0, 0, 0, 1));
      CHECK(585, same(counts(ClayFog::kOff), 0, 0, 0, 0, 0) && same(counts(ClayFog::kCount), 0, 0, 0, 0, 0));
      {
        // The light's switch off with the fog's on: the verdict is kNoLight, and the frame is counted.
        ClayFogInputs dark = all;
        dark.light = false;
        CHECK(585, same(counts(DecideClayFog(dark).fog), 1, 0, 0, 0, 1));
        dark = all;
        dark.fog = false;
        CHECK(585, same(counts(DecideClayFog(dark).fog), 0, 0, 0, 0, 0));
      }
      ClayFogFrames sum;
      CountClayFogFrame(ClayFog::kDrawn, sum);
      CountClayFogFrame(ClayFog::kView, sum);
      CountClayFogFrame(ClayFog::kBlock, sum);
      CountClayFogFrame(ClayFog::kOff, sum);
      CountClayFogFrame(ClayFog::kNoLight, sum);
      CHECK(585, same(sum, 4, 1, 0, 1, 1) && ClayFogBlockFrames(sum) == 2);
      // The line.
      ClayStats st;
      CHECK(585, FormatFogText(st) == "Fog: off");
      st.fog_switch = true;
      st.fog_block = true;
      st.fog = ClayFog::kDrawn;
      st.fog_trees = 52;
      st.fog_trees_none = 3;
      st.fog_frames = 900;
      st.fog_block_frames = 4;
      st.fog_values_frames = 5;
      st.fog_view_frames = 2;
      st.fog_nolight_frames = 1;
      CHECK(585, FormatFogText(st) == "Fog: on, block valid, drawn | trees 52 with a scale, 3 without | "
                                      "unlit 0 fogged (0 by their alpha, 0 trees by their scale), 0 trees without a scale, "
                                      "0 not in the fog table | lit 0 not fogged (no fog in their pixel shader) | "
                                      "0 records with another view | "
                                      "frames since start 900: fog-block 11 {block: 4, values: 5, view: 2}, no-light 1");
      // (plan Task 6: the unlit draws of a fogged frame, and the records fogged with another view's
      // position: review of plan Task 4, M3)
      // (the final review's fixes: an unlit tree with its scale, one without, a lit draw whose pixel
      // shader has no fog; "without a fog block" became "not in the fog table")
      st.fog_unlit = 24;
      st.fog_unlit_alpha = 10;
      st.fog_unlit_tree = 1;
      st.fog_unlit_tree_none = 3;
      st.fog_unlit_none = 2;
      st.fog_lit_none = 4;
      st.fog_view_outliers = 7;
      CHECK(585, FormatFogText(st) == "Fog: on, block valid, drawn | trees 52 with a scale, 3 without | "
                                      "unlit 24 fogged (10 by their alpha, 1 trees by their scale), 3 trees without a scale, "
                                      "2 not in the fog table | lit 4 not fogged (no fog in their pixel shader) | "
                                      "7 records with another view | "
                                      "frames since start 900: fog-block 11 {block: 4, values: 5, view: 2}, no-light 1");
      {
        // The line is whole with the largest counts (final review, M8: the buffer).
        ClayStats big = st;
        big.fog_trees = big.fog_trees_none = big.fog_unlit = big.fog_unlit_alpha = big.fog_unlit_tree = big.fog_unlit_tree_none =
            big.fog_unlit_none = big.fog_lit_none = big.fog_view_outliers = 0xFFFFFFFFu;
        big.fog_frames = big.fog_block_frames = big.fog_values_frames = big.fog_view_frames = big.fog_nolight_frames =
            0xFFFFFFFFFFFFFFFFull;
        CHECK(585, FormatFogText(big).ends_with(", no-light 18446744073709551615"));
      }
      st.fog = ClayFog::kView;
      st.fog_view = FrameViewReason::kNoRecord;
      CHECK(585, FormatFogText(st).starts_with("Fog: on, block valid, not drawn (fog-block: view: no-record) | trees 52 with a scale"));
      st.fog = ClayFog::kValues;
      CHECK(585, FormatFogText(st).starts_with("Fog: on, block valid, not drawn (fog-block: values) | "));
      st.fog = ClayFog::kBlock;
      st.fog_block = false;
      st.fog_reason = capture::FogBlockReason::kNoDraw;
      CHECK(585, FormatFogText(st).starts_with("Fog: on, block invalid (fog-block: no-draw), not drawn (fog-block: block) | "));
      st.fog = ClayFog::kNoLight;
      CHECK(585, FormatFogText(st).starts_with("Fog: on, block invalid (fog-block: no-draw), not drawn (no light) | "));
      CHECK(585, std::string(capture::kFogBlockReason) == "fog-block");
    }

    // 586: the form of a draw's fog in its object block. An object: the ground form. A trunk or a leaf
    // with a usable scale: the trees' form with that scale. A trunk or a leaf without one: no fog for
    // the draw (never the ground form, never a scale of 1).
    {
      static_assert(kTreeFogFormGround == 0.0f && kTreeFogFormTree == 1.0f && kTreeFogFormNone == 2.0f);
      capture::LightInputs l;
      l.family = capture::LightFamily::kTreeTrunk;
      l.kind = capture::LightVertexKind::kProbe;
      ClayObjectConstants oc;
      CHECK(586, MakeClayObjectConstants(l, &oc) && oc.leaf[2] == 0.0f && oc.leaf[3] == kTreeFogFormNone);
      for (float scale : {0.0f, 0.625f, 1.0f, 4.0f}) {
        l.tree_fog = scale;
        CHECK(586, MakeClayObjectConstants(l, &oc) && oc.leaf[2] == scale && oc.leaf[3] == kTreeFogFormTree && oc.leaf[0] == 0.0f);
      }
      for (float bad : {qnan, pinf, -pinf, -0.5f, 4.5f, capture::kTreeFogNone}) {
        l.tree_fog = bad;
        CHECK(586, MakeClayObjectConstants(l, &oc) && oc.leaf[2] == 0.0f && oc.leaf[3] == kTreeFogFormNone);
      }
      l.family = capture::LightFamily::kTreeLeaf;
      l.tree_fog = 0.5f;
      CHECK(586, MakeClayObjectConstants(l, &oc) && oc.leaf[2] == 0.5f && oc.leaf[3] == kTreeFogFormTree && oc.leaf[1] == 1.0f);
      l.family = capture::LightFamily::kObject;  // (an object carries no scale; one set by hand is not read)
      CHECK(586, MakeClayObjectConstants(l, &oc) && oc.leaf[2] == 0.0f && oc.leaf[3] == kTreeFogFormGround);
      l.kind = capture::LightVertexKind::kStream;
      l.colour_power = 2.2f;
      CHECK(586, MakeClayObjectConstants(l, &oc) && oc.leaf[2] == 0.0f && oc.leaf[3] == kTreeFogFormGround);
      // An instanced plant's block ends before the leaf's register (kClayPlantConstantBytes): its
      // shader path takes the ground form without reading it.
      static_assert(offsetof(ClayObjectConstants, leaf) >= kClayPlantConstantBytes);
    }

    // 587: a pixel's world position from its own SV_Position through the frame's inverse
    // view-projection (the controller's ruling: one path for every family; the mask step's
    // arithmetic), in single precision as the shader computes it, and the fog of a pixel from it.
    {
      ClayFrameConstants c = lit_frame();
      CHECK(587, SetClayFrameFog(c, real, true, fog_inv));
      // A world point drawn through the frame's view-projection, as the vertex shader and the
      // rasteriser do it (clip = rows . p, single precision; window = the pixel's centre; depth z / w).
      auto window = [&](const double world[3], float pos[3]) {
        float clip[4];
        for (int r = 0; r < 4; ++r) {
          clip[r] = fog_vp[r][0] * float(world[0]) + fog_vp[r][1] * float(world[1]) + fog_vp[r][2] * float(world[2]) + fog_vp[r][3];
        }
        if (!(clip[3] > 0.0f)) return false;
        pos[0] = (clip[0] / clip[3] * 0.5f + 0.5f) * 1120.0f;
        pos[1] = (0.5f - clip[1] / clip[3] * 0.5f) * 720.0f;
        pos[2] = clip[2] / clip[3];
        return pos[0] >= 0.0f && pos[0] <= 1120.0f && pos[1] >= 0.0f && pos[1] <= 720.0f && pos[2] >= 0.0f && pos[2] <= 1.0f;
      };
      // The view's forward and two sideways directions, from the matrix itself: rows 3 (w) and 0, 1.
      const double fwd_len = std::sqrt(double(fog_vp[3][0]) * fog_vp[3][0] + double(fog_vp[3][1]) * fog_vp[3][1] + double(fog_vp[3][2]) * fog_vp[3][2]);
      const double fwd[3] = {fog_vp[3][0] / fwd_len, fog_vp[3][1] / fwd_len, fog_vp[3][2] / fwd_len};
      const double side_len = std::sqrt(double(fog_vp[0][0]) * fog_vp[0][0] + double(fog_vp[0][1]) * fog_vp[0][1] + double(fog_vp[0][2]) * fog_vp[0][2]);
      const double side[3] = {fog_vp[0][0] / side_len, fog_vp[0][1] / side_len, fog_vp[0][2] / side_len};
      // Points at distances over the range the tables span (the start, 19.9, to 2,000 units past it)
      // and beyond, straight ahead and towards the picture's edges: the rebuilt position's error in
      // its distance from the eye, absolute and as a share of the transmittance table's texel (7.8 units).
      double worst = 0.0, worst_at = 0.0, worst_near = 0.0, worst_share = 0.0, worst_1000 = 0.0, worst_place = 0.0;
      int seen = 0;
      for (double d = 1.0; d <= 2600.0; d *= 1.02) {
        for (double off : {-0.6, -0.3, 0.0, 0.3, 0.6}) {
          const double dir[3] = {fwd[0] + off * side[0], fwd[1] + off * side[1], fwd[2] + off * side[2] + 0.2 * off};
          const double len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
          const double world[3] = {eye[0] + dir[0] / len * d, eye[1] + dir[1] / len * d, eye[2] + dir[2] / len * d};
          float pos[3], P[3];
          if (!window(world, pos)) continue;
          ++seen;
          CHECK(587, FogPixelWorld(c, pos[0], pos[1], pos[2], P));
          // (the distance the vertex shader's own single-precision point has, so that the error is the rebuild's)
          const double true_d = std::sqrt((double(float(world[0])) - eye[0]) * (double(float(world[0])) - eye[0]) +
                                          (double(float(world[1])) - eye[1]) * (double(float(world[1])) - eye[1]) +
                                          (double(float(world[2])) - eye[2]) * (double(float(world[2])) - eye[2]));
          const double got = std::sqrt((double(P[0]) - eye[0]) * (double(P[0]) - eye[0]) + (double(P[1]) - eye[1]) * (double(P[1]) - eye[1]) +
                                       (double(P[2]) - eye[2]) * (double(P[2]) - eye[2]));
          const double err = std::fabs(got - true_d);
          // The position itself, as a share of its distance (a point rebuilt on the wrong side of the
          // picture has the right distance and the wrong place).
          const double place = std::sqrt((double(P[0]) - double(float(world[0]))) * (double(P[0]) - double(float(world[0]))) +
                                         (double(P[1]) - double(float(world[1]))) * (double(P[1]) - double(float(world[1]))) +
                                         (double(P[2]) - double(float(world[2]))) * (double(P[2]) - double(float(world[2])))) / true_d;
          if (d <= 2020.0 && place > worst_place) worst_place = place;
          if (d <= 2020.0 && err > worst) {
            worst = err;
            worst_at = d;
          }
          if (d <= 40.0 && err > worst_near) worst_near = err;
          if (d <= 1000.0 && err > worst_1000) worst_1000 = err;
        }
      }
      worst_share = worst / (1.0 / (double(real.range[3]) * kFogTransmitTexels));
      std::printf("fog: the rebuilt position's distance error over %d points to 2,020 units (the bridge frame's matrices, single "
                  "precision): worst %.3f units at %.0f units (%.3f of a transmittance texel of %.1f units); worst within 1,000 units %.3f, "
                  "within 40 units %.5f\n",
                  seen, worst, worst_at, worst_share, 1.0 / (double(real.range[3]) * kFogTransmitTexels), worst_1000, worst_near);
      CHECK(587, seen > 900);
      // Measured: 2.2 units within 1,000 and 6.1 units at 2,007 units, 0.79 of a texel of the transmittance table (the depth is
      // z / w in single precision with the near plane at about 0.1 units, and the inverse's rows cancel);
      // near the start, where a pixel changes from unfogged to fogged, under 0.01 units. What the fog
      // bears: less than one texel of the table over the tables' span (a step of 1 / 255 in T at most).
      CHECK(587, worst < 7.8 && worst_1000 < 3.0 && worst_near < 0.01 && worst_place < 0.005);
      // The pixel's fog from that position is the mirror's at the true point, within what the position's
      // error moves the tables' coordinates by; inside the start it is the colour, bit for bit.
      const float C[3] = {0.35f, 0.3f, 0.12f};
      int fogged = 0;
      for (double d : {5.0, 15.0, 25.0, 60.0, 254.0, 600.0, 1500.0}) {
        const double world[3] = {eye[0] + fwd[0] * d, eye[1] + fwd[1] * d, eye[2] + fwd[2] * d};
        float pos[3];
        CHECK(587, window(world, pos));
        const float Pw[3] = {float(world[0]), float(world[1]), float(world[2])};
        double want[3], want_tree[3], got[3], got_tree[3], got_none[3];
        FogMirror(C, Pw, eye, c.fog, c.scatter, c.transmit, want);
        TreeFogMirror(C, Pw, eye, c.fog, 0.5f, c.scatter, c.transmit, want_tree);
        FogPixelMirror(C, pos[0], pos[1], pos[2], c, kTreeFogFormGround, 0.0f, got);
        FogPixelMirror(C, pos[0], pos[1], pos[2], c, kTreeFogFormTree, 0.5f, got_tree);
        FogPixelMirror(C, pos[0], pos[1], pos[2], c, kTreeFogFormNone, 0.5f, got_none);
        CHECK(587, close3(got, want, 2e-3) && close3(got_tree, want_tree, 2e-3) && same3(got_none, C));
        if (d < 19.0) {
          CHECK(587, same3(got, C) && same3(got_tree, C));
        } else if (d > 21.0) {
          ++fogged;
          if (d > 100.0) CHECK(587, !close3(got, got_tree, 1e-4) && !same3(got, C));
        }
      }
      CHECK(587, fogged == 5);
      // The frame's tables read as tables of four floats a texel: the block's read, texel for texel.
      for (double u : {0.0, 0.127, 0.5, 0.998, 2.5}) {
        double a[3], b[3];
        FogTableRead(c.scatter, kFogScatterTexels, u, a);
        FogTableRead(real.scatter, kFogScatterTexels, u, b);
        CHECK(587, close3(a, b, 0.0));
        FogTableRead(c.transmit, kFogTransmitTexels, u, a);
        FogTableRead(real.transmit, kFogTransmitTexels, u, b);
        CHECK(587, close3(a, b, 0.0));
      }
      // Guards: a frame without fog gives the colour for any pixel; a pixel whose position cannot be
      // rebuilt (w = 0 or no number) is unfogged; a depth or a position that is no number: a number comes out.
      double out[3];
      const ClayFrameConstants off = lit_frame();
      FogPixelMirror(C, 560.5f, 360.5f, 0.999f, off, kTreeFogFormGround, 0.0f, out);
      CHECK(587, same3(out, C));
      FogPixelMirror(C, 560.5f, 360.5f, 0.999f, off, kTreeFogFormTree, 1.0f, out);
      CHECK(587, same3(out, C));
      {
        ClayFrameConstants flat = c;
        for (float& v : flat.inv_view_proj[3]) v = 0.0f;
        float P[3];
        CHECK(587, !FogPixelWorld(flat, 560.5f, 360.5f, 0.999f, P));
        FogPixelMirror(C, 560.5f, 360.5f, 0.999f, flat, kTreeFogFormGround, 0.0f, out);
        CHECK(587, same3(out, C));
      }
      for (float bad : {qnan, pinf, -pinf}) {
        for (int what = 0; what < 3; ++what) {
          float pos[3] = {560.5f, 360.5f, 0.999f};
          pos[what] = bad;
          for (float form : {kTreeFogFormGround, kTreeFogFormTree}) {
            FogPixelMirror(C, pos[0], pos[1], pos[2], c, form, 1.0f, out);
            CHECK(587, same3(out, C));
          }
        }
        FogPixelMirror(C, 560.5f, 360.5f, 0.999f, c, bad, 1.0f, out);  // (a form that is no number is not the trees': the ground form)
        CHECK(587, finite3(out));
      }
      // The clear depth (a pixel nothing was drawn at is not shaded at all; a far-plane fragment is):
      // a number, the table's far end.
      FogPixelMirror(C, 560.5f, 360.5f, 1.0f, c, kTreeFogFormGround, 0.0f, out);
      CHECK(587, finite3(out));
    }

    // 588-594: the native sky (fog sub-project, plan Task 5; frame-map section 16, "Native sky"): the
    // mirror of the sky step's shader against the reference's numbers, its guards, the sky's own rules
    // and constants, the decision, the step's place among the records, and the view rays. Every line
    // of numbers of case 588 was written by tools/xdk_sigmatch/sky_pins.py (light_formulas.sky,
    // sky_latlong, sky_paths, sky_extinction), and tests/test_sky_literals.py holds the lines against
    // that tool's output.
    {
      // The sky block of every captured row, and a made-up set with other paths, extinctions and a
      // texture blend that is not 0 (the captured frames' textures are zeros and their blend is 0).
      const float c31[4] = {150.0f, 0.0f, 0.0f, 0.0f}, c46[4] = {100.0f, 0.0f, 0.0f, 0.0f}, c47[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      const uint32_t fc13[6] = {0x00002002, 0x1A544086, 0x0001E00F, 0x00280C14, 0x00000003, 0x00000200};
      const capture::SkyBlock real_sky = capture::MakeSkyBlock(c31, c46, c47, fc13, fc13);
      CHECK(588, real_sky.valid && real.valid);
      capture::FogBlock made_fog = real;
      const float made_ext[2][3] = {{0.0007999999797903001f, 0.0010999999940395355f, 0.0019000000320374966f},
                                    {0.004000000189989805f, 0.007000000216066837f, 0.012000000104308128f}};
      std::memcpy(made_fog.ext, made_ext, sizeof(made_ext));
      made_fog.range[1] = 1500.0f;
      capture::SkyBlock made_sky = real_sky;
      made_sky.c31x = 120.5f;
      made_sky.c46x = 80.25f;
      made_sky.c47x = 0.25f;
      float basis[3][3];
      CHECK(588, SkyRayBasis(fog_vp, basis));
      ClaySkyConstants real_k, made_k;
      CHECK(588, MakeClaySkyConstants(real, real_sky, basis, &real_k) && MakeClaySkyConstants(made_fog, made_sky, basis, &made_k));
      // sky texture samples: tf13 {0.6000000238418579f, 0.5f, 0.8999999761581421f, 0.3499999940395355f} tf14 {0.20000000298023224f, 0.800000011920929f, 0.4000000059604645f, 0.75f}
      const float s13[4] = {0.6000000238418579f, 0.5f, 0.8999999761581421f, 0.3499999940395355f}, s14[4] = {0.20000000298023224f, 0.800000011920929f, 0.4000000059604645f, 0.75f};
      struct SkyPin {
        float n[3];
        double uv[2], a, l[2], E[3], clear[3], first[3], both[3];
      };
      static const SkyPin real_sky_pins[] = {
        // real sky registers: c31.x 150.0f c46.x 100.0f c47.x 0.0f c64 {0.0003054436529055238f, 0.0003937904257327318f, 0.0010710508795455098f} c65 {0.0052510276436805725f, 0.005962266121059656f, 0.009832949377596378f} c66 {-0.8295828104019165f, 0.04456644505262375f, 0.5566023588180542f} c67.y 1900.0f
        // real sky pins: n, (u, v), a, (l1, l2), E, the clear sky, with tf13 alone, with both textures
        {{0.0f, 0.0f, 1.0f}, {0.5, 1.0000294799147724}, 0.5566023588180542, {150.0, 100.0}, {0.5650057874113371, 0.5192887629442311, 0.31855741465598497}, {0.2538611605969858, 0.22348437954236802, 0.29243710770514025}, {0.29100976376898435, 0.2327648465444938, 0.4735841019030559}, {0.29100976376898435, 0.2327648465444938, 0.4735841019030559}},
        {{0.0f, 0.0f, -2.5f}, {0.5, -1.0000294799147724}, 0.0, {150.0, 100.0}, {0.5650057874113371, 0.5192887629442311, 0.31855741465598497}, {0.13402409577316712, 0.18460907614470812, 0.317097062418382}, {0.21311567091921763, 0.2074958991043, 0.48961307261364784}, {0.21311567091921763, 0.2074958991043, 0.48961307261364784}},
        {{9.999999974752427e-07f, 0.0f, 1.0f}, {0.5, 1.0000288432783284}, 0.5566015292349675, {149.9999999999945, 99.9999999999995}, {0.5650057874113396, 0.5192887629442338, 0.3185574146559884}, {0.2538609576110735, 0.22348436708018202, 0.2924371960353331}, {0.29100963182814016, 0.23276483844407284, 0.4735841593176817}, {0.29100963182814016, 0.23276483844407284, 0.4735841593176817}},
        {{1.0f, 0.0f, 0.0f}, {0.5, 4.303145910000694e-05}, 0.0, {1994.99990940094, 1994.99990940094}, {1.533826938868475e-05, 3.1117289128203197e-06, 3.5693078421318173e-10}, {0.3081007429453202, 0.3840320081177783, 0.46533203108390864}, {0.3262654926186948, 0.3371208060754489, 0.5859658031297894}, {0.3262654926186948, 0.3371208060754489, 0.5859658031297894}},
        {{0.0f, 3.0f, 0.0f}, {0.7500073699786931, 4.303145910000694e-05}, 0.04456644505262375, {1994.99990940094, 1994.99990940094}, {1.533826938868475e-05, 3.1117289128203197e-06, 3.5693078421318173e-10}, {0.3210648524735525, 0.38944465542306, 0.4641834703014241}, {0.3346921638893179, 0.3406390268561439, 0.5852192386143285}, {0.3346921638893179, 0.3406390268561439, 0.5852192386143285}},
        {{0.0f, -1.0f, 0.0f}, {0.2499926300213069, 4.303145910000694e-05}, 0.0, {1994.99990940094, 1994.99990940094}, {1.533826938868475e-05, 3.1117289128203197e-06, 3.5693078421318173e-10}, {0.3081007429453202, 0.3840320081177783, 0.46533203108390864}, {0.3262654926186948, 0.3371208060754489, 0.5859658031297894}, {0.3262654926186948, 0.3371208060754489, 0.5859658031297894}},
        {{-1.0f, 0.0f, 0.0f}, {1.0000147399573862, 4.303145910000694e-05}, 0.8295828104019165, {1994.99990940094, 1994.99990940094}, {1.533826938868475e-05, 3.1117289128203197e-06, 3.5693078421318173e-10}, {0.7698184637375876, 0.47021338056887546, 0.36966162161745747}, {0.6263820138857207, 0.3931386986823431, 0.5237800364063561}, {0.6263820138857207, 0.3931386986823431, 0.5237800364063561}},
        {{-1.0f, 9.999999974752427e-07f, 0.0f}, {1.000014580819082, 4.303145910000694e-05}, 0.8295828549679467, {1994.99990940094, 1994.99990940094}, {1.533826938868475e-05, 3.1117289128203197e-06, 3.5693078421318173e-10}, {0.7698185027322658, 0.47021338056887546, 0.36966160769057305}, {0.6263820392322618, 0.3931386986823431, 0.5237800273538811}, {0.6263820392322618, 0.3931386986823431, 0.5237800273538811}},
        {{-1.0f, -9.999999974752427e-07f, 0.0f}, {-1.4580819082010699e-05, 4.303145910000694e-05}, 0.8295827658350567, {1994.99990940094, 1994.99990940094}, {1.533826938868475e-05, 3.1117289128203197e-06, 3.5693078421318173e-10}, {0.7698184247421833, 0.47021338056887546, 0.36966163554460113}, {0.6263819885387076, 0.3931386986823431, 0.5237800454589996}, {0.6263819885387076, 0.3931386986823431, 0.5237800454589996}},
        {{-2.0f, 0.0f, 1.0f}, {1.0000147399573862, 0.29520738196904805}, 0.9909215649603816, {183.81333871991245, 141.2463426959595}, {0.4503036900564933, 0.4007040298037657, 0.20479586950784565}, {0.5121200219182144, 0.279638736240336, 0.24413535900492508}, {0.45887802516712567, 0.26926517873287903, 0.44218796496001517}, {0.45887802516712567, 0.26926517873287903, 0.44218796496001517}},
        {{-1.0f, -0.0010000000474974513f, -0.4000000059604645f}, {0.00014439830214268978, -0.24226883488723971}, 0.5634897500861816, {201.4792224305203, 160.46484099369525}, {0.4048875707461826, 0.3548448383849493, 0.16635401635179214}, {0.34961117307280654, 0.3000738076906502, 0.35685717542995454}, {0.35324727244898235, 0.2825479752973858, 0.5154571463081588}, {0.35324727244898235, 0.2825479752973858, 0.5154571463081588}},
        {{1.0f, 1.0f, 0.0f}, {0.6250055037586475, 4.303145910000694e-05}, 0.0, {1994.99990940094, 1994.99990940094}, {1.533826938868475e-05, 3.1117289128203197e-06, 3.5693078421318173e-10}, {0.3081007429453202, 0.3840320081177783, 0.46533203108390864}, {0.3262654926186948, 0.3371208060754489, 0.5859658031297894}, {0.3262654926186948, 0.3371208060754489, 0.5859658031297894}},
        {{-1.0f, 1.0f, 0.20000000298023224f}, {0.8750092361987387, 0.0894015812521043}, 0.6899668171778474, {332.92397226021956, 299.1786961904603}, {0.1877422821962996, 0.14735874078857344, 0.036942691562578914}, {0.5393166773906158, 0.3996043722907808, 0.39040678815084817}, {0.4765558513862913, 0.34724284288071905, 0.5372643947767108}, {0.4765558513862913, 0.34724284288071905, 0.5372643947767108}},
        {{1.0f, -1.0f, -0.30000001192092896f}, {0.3749944962413525, -0.13305374263823294}, 0.0, {273.5183915578653, 237.01109884864053}, {0.2649812339363854, 0.21852996188075402, 0.07255065904121051}, {0.22646330145807658, 0.3001104418851499, 0.43157188570982735}, {0.2732011551653894, 0.28257178752402895, 0.5640217084354104}, {0.2732011551653894, 0.28257178752402895, 0.5640217084354104}},
        {{-1.0f, -1.0f, 0.0f}, {0.12499076380126134, 4.303145910000694e-05}, 0.5550903952809012, {1994.99990940094, 1994.99990940094}, {1.533826938868475e-05, 3.1117289128203197e-06, 3.5693078421318173e-10}, {0.582737221527922, 0.4648548939706132, 0.42938040683410006}, {0.504779205334347, 0.38965568236153353, 0.5625972471531254}, {0.504779205334347, 0.38965568236153353, 0.5625972471531254}},
        {{0.30000001192092896f, -0.4000000059604645f, -0.800000011920929f}, {0.35241236724137226, -0.6443869497228895}, 0.0, {150.21510305125338, 101.8369815804109}, {0.5595451555736591, 0.5135887425105261, 0.3127829162486742}, {0.13570654630518608, 0.1867980732497418, 0.3197841214917058}, {0.21420926377505814, 0.20891874723561932, 0.4913596610273244}, {0.21420926377505814, 0.20891874723561932, 0.4913596610273244}},
        {{0.6000000238418579f, 0.20000000298023224f, 0.05000000074505806f}, {0.5512110117509568, 0.05019502850077647}, 0.0, {429.6414681698631, 399.5608046629108}, {0.10759984623130171, 0.07796646259024827, 0.01241277643328472}, {0.27495336768947687, 0.35409149276014146, 0.45955596877884747}, {0.3047196985048229, 0.3176594709145256, 0.5822113625970716}, {0.3047196985048229, 0.3176594709145256, 0.5822113625970716}},
        {{0.10000000149011612f, -0.20000000298023224f, 0.9700000286102295f}, {0.32378708273902457, 0.8557921521422289}, 0.4500852879987637, {149.7666362134995, 100.02485094234035}, {0.5649723324333389, 0.519259542605633, 0.3185591942598743}, {0.22503646201192062, 0.21764304080225, 0.3009864228895406}, {0.2722737095168834, 0.2289679763286, 0.47914115682387404}, {0.2722737095168834, 0.2289679763286, 0.47914115682387404}},
        {{-0.5f, 0.699999988079071f, 0.3499999940395355f}, {0.8487253694971866, 0.24602960975560556}, 0.6899888956580524, {199.97998959139392, 158.84534956139584}, {0.4085324510937295, 0.3584993479679675, 0.1692959349368103}, {0.39272667668683825, 0.30065006977038217, 0.33674934532014694}, {0.3812723500550914, 0.28292254565264635, 0.5023870566169317}, {0.3812723500550914, 0.28292254565264635, 0.5023870566169317}},
        {{-0.8295828104019165f, 0.04456644505262375f, 0.5566023588180542f}, {0.9914737269217622, 0.3758235687626616}, 0.9999999965903963, {167.14958579923868, 122.67836901924377}, {0.498953547977844, 0.45056047135474336, 0.25024473882487724}, {0.4675291844796583, 0.25634251446315565, 0.22972237616571756}, {0.42989398056628214, 0.2541226344388555, 0.4328195260286222}, {0.42989398056628214, 0.2541226344388555, 0.4328195260286222}},
        {{0.8295828104019165f, -0.04456644505262375f, -0.5566023588180542f}, {0.49145898696437595, -0.37582356876266154}, 0.0, {167.14958579923868, 122.67836901924377}, {0.498953547977844, 0.45056047135474336, 0.25024473882487724}, {0.15437515196581078, 0.2110030221091281, 0.34888513862299414}, {0.22634385756573777, 0.22465196413849317, 0.5102753223361174}, {0.22634385756573777, 0.22465196413849317, 0.5102753223361174}},
        {{-0.8999999761581421f, 0.30000001192092896f, 0.44999998807907104f}, {0.9488037238579108, 0.28200501792848803}, 0.9623481140649474, {187.59340609581147, 145.38736225581818}, {0.44010926097019865, 0.39035034636351773, 0.19582994301753295}, {0.5033685810174762, 0.2852864428195614, 0.2576607000838273}, {0.4531895885294832, 0.2729361880430385, 0.4509794367419189}, {0.4531895885294832, 0.2729361880430385, 0.4509794367419189}},
      };
      static const SkyPin made_sky_pins[] = {
        // made sky registers: c31.x 120.5f c46.x 80.25f c47.x 0.25f c64 {0.0007999999797903001f, 0.0010999999940395355f, 0.0019000000320374966f} c65 {0.004000000189989805f, 0.007000000216066837f, 0.012000000104308128f} c66 {-0.8295828104019165f, 0.04456644505262375f, 0.5566023588180542f} c67.y 1500.0f
        // made sky pins: n, (u, v), a, (l1, l2), E, the clear sky, with tf13 alone, with both textures
        {{0.0f, 0.0f, 1.0f}, {0.5, 1.0000294799147724}, 0.5566023588180542, {120.5, 80.25}, {0.6587573595218957, 0.49942391872854114, 0.3036286117443316}, {0.19914805818087636, 0.23271961691988374, 0.29884371633054635}, {0.2554462468723977, 0.23876775089492533, 0.4777483975477562}, {0.23553144142810867, 0.28437079093859496, 0.4557390284756989}},
        {{0.0f, 0.0f, -2.5f}, {0.5, -1.0000294799147724}, 0.0, {120.5, 80.25}, {0.6587573595218957, 0.49942391872854114, 0.3036286117443316}, {0.10513872370199404, 0.19223783589843865, 0.32404391260139254}, {0.19434017890078487, 0.2124545929896958, 0.49412852527401113}, {0.1838263070444689, 0.26210581119583254, 0.469599136537318}},
        {{9.999999974752427e-07f, 0.0f, 1.0f}, {0.5, 1.0000288432783284}, 0.5566015292349675, {120.49999999999545, 80.24999999999947}, {0.6587573595218996, 0.4994239187285454, 0.3036286117443361}, {0.1991478989432424, 0.23271960394271143, 0.2988438065958453}, {0.2554461433679347, 0.23876774245976323, 0.4777484562202011}, {0.23553135384740925, 0.28437078380115016, 0.45573907812161374}},
        {{1.0f, 0.0f, 0.0f}, {0.5, 4.303145910000694e-05}, 0.0, {1574.9999284744263, 1574.9999284744263}, {0.0005208753359983021, 2.880635635923183e-06, 3.1060505565704907e-10}, {0.3079449842104419, 0.3840320968652697, 0.4653320311054655}, {0.32616424944009553, 0.33712086376131883, 0.5859658031438013}, {0.2953697512307299, 0.3675926555849767, 0.5473076023461652}},
        {{0.0f, 3.0f, 0.0f}, {0.7500073699786931, 4.303145910000694e-05}, 0.04456644505262375, {1574.9999284744263, 1574.9999284744263}, {0.0005208753359983021, 2.880635635923183e-06, 3.1060505565704907e-10}, {0.3209025398002461, 0.3894447454213817, 0.4641834703229278}, {0.3345866606507013, 0.34063908535505355, 0.5852192386283059}, {0.30249640686304696, 0.37056961231503477, 0.546675893910635}},
        {{0.0f, -1.0f, 0.0f}, {0.2499926300213069, 4.303145910000694e-05}, 0.0, {1574.9999284744263, 1574.9999284744263}, {0.0005208753359983021, 2.880635635923183e-06, 3.1060505565704907e-10}, {0.3079449842104419, 0.3840320968652697, 0.4653320311054655}, {0.32616424944009553, 0.33712086376131883, 0.5859658031438013}, {0.2953697512307299, 0.3675926555849767, 0.5473076023461652}},
        {{-1.0f, 0.0f, 0.0f}, {1.0000147399573862, 4.303145910000694e-05}, 0.8295828104019165, {1574.9999284744263, 1574.9999284744263}, {0.0005208753359983021, 2.880635635923183e-06, 3.1060505565704907e-10}, {0.7694292860002947, 0.47021348923236456, 0.3696616216345823}, {0.6261290483541606, 0.39313876931361164, 0.5237800364174872}, {0.5491861192781444, 0.41499242177213974, 0.4946888767094994}},
        {{-1.0f, 9.999999974752427e-07f, 0.0f}, {1.000014580819082, 4.303145910000694e-05}, 0.8295828549679467, {1574.9999284744263, 1574.9999284744263}, {0.0005208753359983021, 2.880635635923183e-06, 3.1060505565704907e-10}, {0.7694293249752595, 0.47021348923236456, 0.3696616077076979}, {0.626129073687888, 0.39313876931361164, 0.5237800273650123}, {0.5491861407143753, 0.41499242177213974, 0.4946888690497129}},
        {{-1.0f, -9.999999974752427e-07f, 0.0f}, {-1.4580819082010699e-05, 4.303145910000694e-05}, 0.8295827658350567, {1574.9999284744263, 1574.9999284744263}, {0.0005208753359983021, 2.880635635923183e-06, 3.1060505565704907e-10}, {0.7694292470246045, 0.47021348923236456, 0.369661635561726}, {0.6261290230199618, 0.39313876931361164, 0.5237800454701307}, {0.5491860978415146, 0.41499242177213974, 0.4946888843694285}},
        {{-2.0f, 0.0f, 1.0f}, {1.0000147399573862, 0.29520738196904805}, 0.9909215649603816, {146.8856846135584, 112.61925281427628}, {0.5666657984277277, 0.38677620551203584, 0.1958289256463402}, {0.4037122258104862, 0.28613762723444947, 0.2468882974956379}, {0.3884129570509416, 0.27348945791778917, 0.4439773749953873}, {0.34804173453886716, 0.31375069685040324, 0.42716354788424044}},
        {{-1.0f, -0.0010000000474974513f, -0.4000000059604645f}, {0.00014439830214268978, -0.24226883488723971}, 0.5634897500861816, {160.76787883120508, 127.75130177456091}, {0.5274930850399643, 0.34263011641530117, 0.15905987345926595}, {0.27758401388342585, 0.30575510476365103, 0.35997956464655967}, {0.3064296185465696, 0.2862408184286995, 0.517486699317563}, {0.27867121741514694, 0.324540309579161, 0.4893637453228048}},
        {{1.0f, 1.0f, 0.0f}, {0.6250055037586475, 4.303145910000694e-05}, 0.0, {1574.9999284744263, 1574.9999284744263}, {0.0005208753359983021, 2.880635635923183e-06, 3.1060505565704907e-10}, {0.3079449842104419, 0.3840320968652697, 0.4653320311054655}, {0.32616424944009553, 0.33712086376131883, 0.5859658031438013}, {0.2953697512307299, 0.3675926555849767, 0.5473076023461652}},
        {{-1.0f, 1.0f, 0.20000000298023224f}, {0.8750092361987387, 0.0894015812521043}, 0.6899668171778474, {264.23780799991596, 237.07286076375976}, {0.313585975127861, 0.1422496540790311, 0.03519236395956155}, {0.4557599430502759, 0.40199882994279973, 0.3911163407098901}, {0.4222439735670334, 0.34879924036880344, 0.5377256039443173}, {0.37666797925342294, 0.3774743588579358, 0.5064889722968288}},
        {{1.0f, -1.0f, -0.30000001192092896f}, {0.3749944962413525, -0.13305374263823294}, 0.0, {217.4535070846456, 188.06513645371967}, {0.39604661086348786, 0.21105113444187545, 0.06925615258657403}, {0.1860813420630562, 0.30298255994212153, 0.43310492509032955}, {0.24695288131793092, 0.28443866427817965, 0.5650181840418744}, {0.2283447475048948, 0.3230154099149256, 0.529582693893774}},
        {{-1.0f, -1.0f, 0.0f}, {0.12499076380126134, 4.303145910000694e-05}, 0.5550903952809012, {1574.9999284744263, 1574.9999284744263}, {0.0005208753359983021, 2.880635635923183e-06, 3.1060505565704907e-10}, {0.5824426217436957, 0.4648550013957882, 0.4293804068539914}, {0.5045877154728439, 0.38965575218789794, 0.5625972471660549}, {0.44634345310111945, 0.41204525343806847, 0.5275342088471382}},
        {{0.30000001192092896f, -0.4000000059604645f, -0.800000011920929f}, {0.35241236724137226, -0.6443869497228895}, 0.0, {120.60238219635612, 81.65799441232794}, {0.6550040299078279, 0.49447010298587146, 0.29848355035332913}, {0.10629514508210966, 0.19414026562578715, 0.3264380744693737}, {0.19509185280475283, 0.21369117232381168, 0.4956847305024692}, {0.1844623388087021, 0.2631521475543787, 0.4709159255754103}},
        {{0.6000000238418579f, 0.20000000298023224f, 0.05000000074505806f}, {0.5512110117509568, 0.05019502850077647}, 0.0, {340.4413550904442, 316.22642096734756}, {0.21496956895436894, 0.07516493331249, 0.011777602894718167}, {0.24187216894032873, 0.3551673730223275, 0.4598515353717449}, {0.2832169191206973, 0.3183587930913593, 0.5824034808842167}, {0.25902970253679913, 0.35171705734232317, 0.5442933296681192}},
        {{0.10000000149011612f, -0.20000000298023224f, 0.9700000286102295f}, {0.32378708273902457, 0.8557921521422289}, 0.4500852879987637, {120.30502913508991, 80.2628919918068}, {0.6588261431233047, 0.49948596302060855, 0.30369412387339223}, {0.17648660856893222, 0.22659502710220822, 0.3075521940085286}, {0.24071630448956124, 0.23478676747693084, 0.4834089080903513}, {0.2230676440402348, 0.2810022665114944, 0.46052869123751905}},
        {{-0.5f, 0.699999988079071f, 0.3499999940395355f}, {0.8487253694971866, 0.24602960975560556}, 0.6899888956580524, {159.5892741839267, 126.47588895977827}, {0.5306911913108374, 0.34615125836962074, 0.1618750913080816}, {0.31161487915469976, 0.30643720963924154, 0.3397575937912887}, {0.3285496811757374, 0.286684186601899, 0.5043424181411045}, {0.2973881934664774, 0.32491546726378506, 0.4782416612620065}},
        {{-0.8295828104019165f, 0.04456644505262375f, 0.5566023588180542f}, {0.9914737269217622, 0.3758235687626616}, 0.9999999965903963, {133.80926569709348, 98.00993618919759}, {0.6070845739533555, 0.4346301867620815, 0.23922354379562275}, {0.3666315328003602, 0.263774832299234, 0.23309923157629237}, {0.3643105063733415, 0.2589536410766065, 0.4350144820656235}, {0.32764735321753424, 0.3014511595360652, 0.41957956156695847}},
        {{0.8295828104019165f, -0.04456644505262375f, -0.5566023588180542f}, {0.49145898696437595, -0.37582356876266154}, 0.0, {133.80926569709348, 98.00993618919759}, {0.6070845739533555, 0.4346301867620815, 0.23922354379562275}, {0.12105939152120737, 0.2171207803279409, 0.35401365369275956}, {0.20468861307816813, 0.22862850701718618, 0.5136088571620332}, {0.19258267441620716, 0.27579143074329415, 0.48608249427154504}},
        {{-0.8999999761581421f, 0.30000001192092896f, 0.44999998807907104f}, {0.9488037238579108, 0.28200501792848803}, 0.9623481140649474, {149.85494031951404, 115.87907502831945}, {0.5579977975966879, 0.37681762169580774, 0.18725699862093623}, {0.39738114228482, 0.29161910102592453, 0.26040752065470935}, {0.38429775272152233, 0.27705241591492014, 0.4527648701293646}, {0.3445596385714486, 0.3167655074602186, 0.43459912068216533}},
      };
      CHECK(588, std::size(real_sky_pins) == 22 && std::size(made_sky_pins) == 22);
      // 588: the lat-long coordinate by the game's polynomials, the two paths, E, and the sky without
      // a texture, with the first alone and with both, on both register sets.
      {
        int textures_show = 0, second_shows = 0;
        for (int set = 0; set < 2; ++set) {
          const ClaySkyConstants& k = set ? made_k : real_k;
          for (const SkyPin& p : set ? made_sky_pins : real_sky_pins) {
            const double len = std::sqrt(double(p.n[0]) * p.n[0] + double(p.n[1]) * p.n[1] + double(p.n[2]) * p.n[2]);
            const double u[3] = {p.n[0] / len, p.n[1] / len, p.n[2] / len};
            double uv[2], l[2], E[3], clear[3], first[3], both[3];
            SkyLatLong(u, uv);
            CHECK(588, std::fabs(uv[0] - p.uv[0]) <= 1e-12 && std::fabs(uv[1] - p.uv[1]) <= 1e-12);
            SkyPaths(std::fabs(u[2]), k, l);
            CHECK(588, std::fabs(l[0] - p.l[0]) <= 1e-9 && std::fabs(l[1] - p.l[1]) <= 1e-9);
            SkyExtinction(l, k, E);
            CHECK(588, close3(E, p.E, 1e-12));
            SkyMirror(p.n, k, real.axis, real.scatter, nullptr, nullptr, clear);
            SkyMirror(p.n, k, real.axis, real.scatter, s13, nullptr, first);
            SkyMirror(p.n, k, real.axis, real.scatter, s13, s14, both);
            CHECK(588, close3(clear, p.clear, 1e-12) && close3(first, p.first, 1e-12) && close3(both, p.both, 1e-12));
            // The clear-sky term is (1 - E) * the table at the pin's coordinate.
            double S[3];
            FogTableRead(real.scatter, kFogScatterTexels, p.a, S);
            for (int i = 0; i < 3; ++i) CHECK(588, std::fabs(clear[i] - (1.0 - p.E[i]) * S[i]) <= 1e-12);
            if (!close3(first, clear, 1e-6)) ++textures_show;
            if (!close3(both, first, 1e-6)) ++second_shows;
            // Textures of zeros (alpha 0) give the clear-sky term exactly: the captured frames' case.
            const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            double zeroed[3];
            SkyMirror(p.n, k, real.axis, real.scatter, zero, zero, zeroed);
            CHECK(588, zeroed[0] == clear[0] && zeroed[1] == clear[1] && zeroed[2] == clear[2]);
          }
        }
        // What the pins can tell apart: the texture term shows in all 44; the second texture only
        // with the made-up blend (with c47.x = 0 it is not read).
        CHECK(588, textures_show == 44 && second_shows == 22);
        for (const SkyPin& p : real_sky_pins) CHECK(588, close3(p.first, p.both, 0.0));
        // The named places. The poles: u = 0.5 (the console's zero product), v just beyond 1 and -1.
        CHECK(588, real_sky_pins[0].uv[0] == 0.5 && real_sky_pins[0].uv[1] > 1.00002 && real_sky_pins[0].uv[1] < 1.00004 &&
                       real_sky_pins[1].uv[0] == 0.5 && real_sky_pins[1].uv[1] == -real_sky_pins[0].uv[1]);
        // The zenith's paths are c31.x and c46.x themselves; the horizon's both 1.05 * c67.y.
        CHECK(588, real_sky_pins[0].l[0] == 150.0 && real_sky_pins[0].l[1] == 100.0 && made_sky_pins[0].l[0] == 120.5 &&
                       made_sky_pins[0].l[1] == 80.25);
        CHECK(588, std::fabs(real_sky_pins[3].l[0] - 1.05 * 1900.0) < 1e-3 && real_sky_pins[3].l[0] == real_sky_pins[3].l[1] &&
                       std::fabs(made_sky_pins[3].l[0] - 1.05 * 1500.0) < 1e-3);
        // The seam on -x: on the half plane y = 0 and just north of it u is one whole turn (1.0000147),
        // just south it is -0.0000147; v does not jump.
        CHECK(588, real_sky_pins[6].uv[0] > 1.0000147 && real_sky_pins[6].uv[0] < 1.0000148 && real_sky_pins[7].uv[0] > 1.0000145 &&
                       real_sky_pins[7].uv[0] < real_sky_pins[6].uv[0] && real_sky_pins[8].uv[0] < -0.0000145 &&
                       real_sky_pins[8].uv[0] > -0.0000148 && real_sky_pins[7].uv[1] == real_sky_pins[8].uv[1]);
        // ... and the sky itself does not jump there (the coordinate only matters to a texture).
        CHECK(588, close3(real_sky_pins[7].clear, real_sky_pins[8].clear, 1e-6));
        // The coordinate is not atan2 / asin: within 5e-5 of them and not equal (lighting-formulas.md).
        {
          const double n[3] = {0.6, 0.64, 0.48};
          double uv[2];
          SkyLatLong(n, uv);
          const double pi = 3.14159265358979323846;
          const double exact_u = 0.5 + std::atan2(n[1], n[0]) / (2.0 * pi), exact_v = std::asin(n[2]) * 2.0 / pi;
          CHECK(588, std::fabs(uv[0] - exact_u) < 5e-5 && std::fabs(uv[1] - exact_v) < 5e-5 && std::fabs(uv[0] - exact_u) > 1e-9 &&
                         std::fabs(uv[1] - exact_v) > 1e-9);
        }
        // The literals are the reference's singles.
        CHECK(588, kSkyAtan[0] == double(0.9998660087585449f) && kSkyAtan[4] == double(0.02083509974181652f) &&
                       kSkyAcos[0] == double(1.5707287788391113f) && kSkyAcos[3] == double(-0.018729299306869507f) &&
                       kSkyPi == double(3.1415927410125732f) && kSkyHalfPi == double(1.5707963705062866f) &&
                       kSkyInvTwoPi == double(0.15915963053703308f) && kSkyTwoOverPi == double(0.6366385221481323f) &&
                       kSkyPathPower == double(0.2f) && kSkyPathHorizon == double(1.05f) && kSkyInvLn2 == double(1.4426950216293335f));
      }

      // 589: guards. Whatever goes in, a number comes out; a ray without a length or that is no
      // number counts as (0, 0, 0): the table's first texel and the horizon's paths.
      {
        double out[3], want[3];
        const float none[3] = {0.0f, 0.0f, 0.0f};
        SkyMirror(none, real_k, real.axis, real.scatter, nullptr, nullptr, want);
        CHECK(589, finite3(want) && std::fabs(want[0] - (1.0 - real_sky_pins[3].E[0]) * double(real.scatter[0][0])) < 1e-12);
        for (float bad : {qnan, pinf, -pinf}) {
          for (int at = 0; at < 3; ++at) {
            float n[3] = {0.3f, -0.4f, 0.8f};
            n[at] = bad;
            SkyMirror(n, real_k, real.axis, real.scatter, s13, s14, out);
            CHECK(589, finite3(out));
            SkyMirror(n, real_k, real.axis, real.scatter, nullptr, nullptr, out);
            CHECK(589, close3(out, want, 0.0));
          }
        }
        const float huge[3] = {3e30f, 3e30f, 3e30f};  // (its squared length overflows a single, not a double: a direction all the same)
        SkyMirror(huge, real_k, real.axis, real.scatter, nullptr, nullptr, out);
        CHECK(589, finite3(out));
        const float n[3] = {-0.5f, 0.7f, 0.35f};
        // Every constant, the axis and every sample that is no number: a number comes out.
        for (float bad : {qnan, pinf, -pinf, 3e38f, -3e38f}) {
          for (size_t word = 0; word < sizeof(ClaySkyConstants) / 4; ++word) {
            ClaySkyConstants k = made_k;
            if (word * 4 >= offsetof(ClaySkyConstants, textures)) break;  // (the words behind are no floats of the arithmetic)
            std::memcpy(reinterpret_cast<unsigned char*>(&k) + word * 4, &bad, 4);
            SkyMirror(n, k, real.axis, real.scatter, s13, s14, out);
            CHECK(589, finite3(out));
            float ray[3];
            SkyPixelRay(k, 100.5f, 200.5f, 1.0f / 1120.0f, 1.0f / 720.0f, ray);
            SkyMirror(ray, k, real.axis, real.scatter, s13, s14, out);
            CHECK(589, finite3(out));
          }
          for (int at = 0; at < 3; ++at) {
            float axis[3] = {real.axis[0], real.axis[1], real.axis[2]};
            axis[at] = bad;
            SkyMirror(n, made_k, axis, real.scatter, s13, s14, out);
            CHECK(589, finite3(out));
          }
          for (int at = 0; at < 4; ++at) {
            float a[4] = {s13[0], s13[1], s13[2], s13[3]}, b[4] = {s14[0], s14[1], s14[2], s14[3]};
            a[at] = bad;
            SkyMirror(n, made_k, real.axis, real.scatter, a, s14, out);
            CHECK(589, finite3(out));
            b[at] = bad;
            SkyMirror(n, made_k, real.axis, real.scatter, s13, b, out);
            CHECK(589, finite3(out));
          }
          float table[64][3];
          std::memcpy(table, real.scatter, sizeof(table));
          table[0][1] = table[40][0] = table[63][2] = bad;
          for (const float* dir : {none, n, real.axis}) {
            SkyMirror(dir, made_k, real.axis, table, s13, s14, out);
            CHECK(589, finite3(out));
          }
        }
        // No table: the clear-sky term is 0, the texture term stays.
        SkyMirror(n, made_k, real.axis, static_cast<const float (*)[3]>(nullptr), nullptr, nullptr, out);
        CHECK(589, out[0] == 0.0 && out[1] == 0.0 && out[2] == 0.0);
        // Inside the rules the clear-sky term lies between 0 and the table's texel, for every pin.
        for (const SkyPin& p : made_sky_pins) {
          double S[3];
          FogTableRead(real.scatter, kFogScatterTexels, p.a, S);
          for (int i = 0; i < 3; ++i) CHECK(589, p.E[i] >= 0.0 && p.E[i] <= 1.0 && p.clear[i] >= 0.0 && p.clear[i] <= S[i]);
        }
        // The lat-long coordinate of (0, 0, 0) and of a pole is a number.
        const double zero[3] = {0.0, 0.0, 0.0};
        double uv[2];
        SkyLatLong(zero, uv);
        CHECK(589, uv[0] == 0.5 && std::isfinite(uv[1]) && std::fabs(uv[1]) < 1e-4);
      }

      // 590: the sky's own rules (review of plan Task 3, I1; of plan Task 4, M2: their signs and their
      // magnitudes are judged here, not in the fog block) and the sky's constants.
      {
        CHECK(590, SkyRules(real, real_sky) == SkyRule::kNone && SkyRules(made_fog, made_sky) == SkyRule::kNone);
        auto rule = [&](void (*change)(capture::FogBlock&, capture::SkyBlock&)) {
          capture::FogBlock f = real;
          capture::SkyBlock s = real_sky;
          change(f, s);
          ClaySkyConstants k;
          std::memset(&k, 0x5A, sizeof(k));
          ClaySkyConstants before = k;
          const SkyRule r = SkyRules(f, s);
          // (a refused value gives no constants, and nothing is written)
          if (r != SkyRule::kNone && (MakeClaySkyConstants(f, s, basis, &k) || std::memcmp(&k, &before, sizeof(k)) != 0)) return SkyRule::kCount;
          if (r == SkyRule::kNone && !MakeClaySkyConstants(f, s, basis, &k)) return SkyRule::kCount;
          return r;
        };
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock&) { f.range[1] = -0.001f; }) == SkyRule::kHeight);
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock&) { f.range[1] = 0.0f; }) == SkyRule::kNone);
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock&) { f.range[1] = 1e6f; }) == SkyRule::kNone);
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock&) { f.range[1] = 1.0001e6f; }) == SkyRule::kValue);
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock&) { f.range[1] = -3e38f; }) == SkyRule::kValue);
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock&) { f.ext[0][0] = -1e-6f; }) == SkyRule::kExtinction);
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock&) { f.ext[0][2] = -1e-6f; }) == SkyRule::kExtinction);
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock&) { f.ext[1][1] = -1e-6f; }) == SkyRule::kExtinction);
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock&) { f.ext[1][0] = 0.0f; }) == SkyRule::kNone);
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock&) { f.ext[1][0] = 1e7f; }) == SkyRule::kValue);  // (M2: the fog block lets it through)
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock&) { f.ext[0][1] = std::numeric_limits<float>::quiet_NaN(); }) == SkyRule::kValue);
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock&) { f.ext[0][1] = std::numeric_limits<float>::infinity(); }) == SkyRule::kValue);
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock&) { f.axis[2] = std::numeric_limits<float>::quiet_NaN(); }) == SkyRule::kValue);
        CHECK(590, rule([](capture::FogBlock&, capture::SkyBlock& s) { s.c31x = -1.0f; }) == SkyRule::kPath);
        CHECK(590, rule([](capture::FogBlock&, capture::SkyBlock& s) { s.c46x = -1.0f; }) == SkyRule::kPath);
        CHECK(590, rule([](capture::FogBlock&, capture::SkyBlock& s) { s.c31x = 3e38f; }) == SkyRule::kValue);  // (the sky block has no upper bound)
        CHECK(590, rule([](capture::FogBlock&, capture::SkyBlock& s) { s.c46x = std::numeric_limits<float>::infinity(); }) == SkyRule::kValue);
        CHECK(590, rule([](capture::FogBlock&, capture::SkyBlock& s) { s.c47x = 1.001f; }) == SkyRule::kBlend);
        CHECK(590, rule([](capture::FogBlock&, capture::SkyBlock& s) { s.c47x = -0.001f; }) == SkyRule::kBlend);
        CHECK(590, rule([](capture::FogBlock&, capture::SkyBlock& s) { s.c47x = 1.0f; }) == SkyRule::kNone);
        CHECK(590, rule([](capture::FogBlock&, capture::SkyBlock& s) { s.c47x = std::numeric_limits<float>::quiet_NaN(); }) == SkyRule::kValue);
        // The order: a value, the height, the extinctions, the paths, the blend.
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock& s) { f.range[1] = -1.0f; f.ext[0][0] = -1.0f; s.c31x = -1.0f; s.c47x = 2.0f; }) == SkyRule::kHeight);
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock& s) { f.ext[0][0] = -1.0f; s.c31x = -1.0f; s.c47x = 2.0f; }) == SkyRule::kExtinction);
        CHECK(590, rule([](capture::FogBlock&, capture::SkyBlock& s) { s.c31x = -1.0f; s.c47x = 2.0f; }) == SkyRule::kPath);
        CHECK(590, rule([](capture::FogBlock& f, capture::SkyBlock& s) { f.range[1] = -1.0f; s.c47x = 3e38f; }) == SkyRule::kValue);
        CHECK(590, std::string(SkyRuleName(SkyRule::kHeight)) == "height" && std::string(SkyRuleName(SkyRule::kExtinction)) == "extinction" &&
                       std::string(SkyRuleName(SkyRule::kPath)) == "path" && std::string(SkyRuleName(SkyRule::kBlend)) == "blend" &&
                       std::string(SkyRuleName(SkyRule::kValue)) == "value" && std::string(SkyRuleName(SkyRule::kNone)) == "none");
        // The constants: the blocks' values in their places, no textures; nothing from an invalid block.
        static_assert(sizeof(ClaySkyConstants) == 112 && sizeof(ClaySkyConstants) <= kClayTerrainSlotBytes);
        CHECK(590, made_k.c31x == 120.5f && made_k.c46x == 80.25f && made_k.c47x == 0.25f && made_k.c67y == 1500.0f &&
                       std::memcmp(made_k.ext1, made_ext[0], 12) == 0 && std::memcmp(made_k.ext2, made_ext[1], 12) == 0 &&
                       std::memcmp(made_k.ray0, basis[0], 12) == 0 && std::memcmp(made_k.rayx, basis[1], 12) == 0 &&
                       std::memcmp(made_k.rayy, basis[2], 12) == 0 && made_k.textures == 0 && made_k.fix13[0] == 1.0f && made_k.fix14[1] == 1.0f);
        ClaySkyConstants k = made_k;
        CHECK(590, !MakeClaySkyConstants(capture::FogBlock{}, real_sky, basis, &k) && !MakeClaySkyConstants(real, capture::SkyBlock{}, basis, &k) &&
                       !MakeClaySkyConstants(real, real_sky, nullptr, &k) && !MakeClaySkyConstants(real, real_sky, basis, nullptr) &&
                       std::memcmp(&k, &made_k, sizeof(k)) == 0);
        float bad_basis[3][3];
        std::memcpy(bad_basis, basis, sizeof(basis));
        bad_basis[2][1] = qnan;
        CHECK(590, !MakeClaySkyConstants(real, real_sky, bad_basis, &k) && std::memcmp(&k, &made_k, sizeof(k)) == 0);
        // The textures: the second is read only with a blend above 0; the term is on only with every
        // texture that is read.
        CHECK(590, !SkySecondTextureRead(0.0f) && SkySecondTextureRead(0.25f) && SkySecondTextureRead(1.0f) && !SkySecondTextureRead(-1.0f));
        CHECK(590, SkyTexturesUsable(true, false, 0.0f) && SkyTexturesUsable(true, true, 0.5f) && !SkyTexturesUsable(true, false, 0.5f) &&
                       !SkyTexturesUsable(false, true, 0.0f) && !SkyTexturesUsable(false, true, 0.5f) && !SkyTexturesUsable(false, false, 0.0f));
        const float fix_a[2] = {0.5f, 0.75f}, fix_b[2] = {1.0f, 0.25f}, fix_bad[2] = {qnan, 1.0f};
        SetClaySkyTextures(k, 1, fix_a, 7, fix_b);
        CHECK(590, k.textures == 1 && k.sampler13 == 1 && k.sampler14 == 3 && k.fix13[1] == 0.75f && k.fix14[1] == 0.25f);
        SetClaySkyTextures(k, 1, fix_a, 2, fix_bad);
        CHECK(590, k.textures == 0 && k.sampler13 == 0 && k.sampler14 == 0 && k.fix13[0] == 1.0f && k.fix14[0] == 1.0f);
        SetClaySkyTextures(k, 1, nullptr, 2, fix_b);
        CHECK(590, k.textures == 0);
      }

      // 591: the decision about the frame's sky, one chain in which the first thing missing decides;
      // the counts and the line that follow from it.
      {
        ClaySkyInputs all;
        all.fog = ClayFog::kDrawn;
        all.block = all.ray = all.constants = all.objects = true;
        auto is = [](const ClaySkyInputs& in, ClaySky sky, FrameViewReason view = FrameViewReason::kNone, SkyRule rule = SkyRule::kNone) {
          const ClaySkyDecision d = DecideClaySky(in);
          return d.sky == sky && d.view == view && d.rule == rule;
        };
        CHECK(591, is(all, ClaySky::kDrawn) && is(ClaySkyInputs{}, ClaySky::kOff));
        // The fog's verdict first: its switch off, no light, no fog block or values, no single view;
        // each whatever else the frame has or lacks.
        const struct {
          ClayFog fog;
          ClaySky sky;
        } fogs[] = {{ClayFog::kOff, ClaySky::kOff}, {ClayFog::kNoLight, ClaySky::kNoLight}, {ClayFog::kBlock, ClaySky::kNoFog},
                    {ClayFog::kValues, ClaySky::kNoFog}, {ClayFog::kCount, ClaySky::kOff}};
        for (const auto& f : fogs) {
          ClaySkyInputs in = all;
          in.fog = f.fog;
          CHECK(591, is(in, f.sky));
          in.block = in.ray = in.constants = in.objects = false;
          in.rule = SkyRule::kHeight;
          CHECK(591, is(in, f.sky));
        }
        for (FrameViewReason why : {FrameViewReason::kNoRecord, FrameViewReason::kDisagree, FrameViewReason::kSingular, FrameViewReason::kEye}) {
          ClaySkyInputs in = all;
          in.fog = ClayFog::kView;
          in.fog_view = why;
          CHECK(591, is(in, ClaySky::kView, why));
          in.block = false;
          CHECK(591, is(in, ClaySky::kView, why));
        }
        // Then the sky's own: no valid sky block; a rule; no ray; no constants; no objects.
        {
          ClaySkyInputs in = all;
          in.block = false;
          CHECK(591, is(in, ClaySky::kBlock));
          in.rule = SkyRule::kExtinction;
          in.ray = in.constants = in.objects = false;
          CHECK(591, is(in, ClaySky::kBlock));
          in.block = true;
          CHECK(591, is(in, ClaySky::kRules, FrameViewReason::kNone, SkyRule::kExtinction));
          for (SkyRule r : {SkyRule::kValue, SkyRule::kHeight, SkyRule::kPath, SkyRule::kBlend}) {
            ClaySkyInputs one = all;
            one.rule = r;
            CHECK(591, is(one, ClaySky::kRules, FrameViewReason::kNone, r));
          }
          in.rule = SkyRule::kNone;
          CHECK(591, is(in, ClaySky::kView, FrameViewReason::kSingular));
          in.ray = true;
          // (review of plan Task 5, M3: a frame without its constant buffer is "no objects", not a refused value)
          CHECK(591, is(in, ClaySky::kObjects));
          in.constants = true;
          CHECK(591, is(in, ClaySky::kObjects));
          in.objects = true;
          CHECK(591, is(in, ClaySky::kDrawn));
        }
        // The counts: frames with the fog's switch on, and those without a sky by
        // reason; "sky-block" is the block and the rules; "sky-texture" a drawn sky without its textures.
        auto counts = [](ClaySky sky, bool missing) {
          ClaySkyFrames n;
          CountClaySkyFrame(sky, missing, n);
          return n;
        };
        auto same = [](const ClaySkyFrames& n, uint64_t frames, uint64_t nofog, uint64_t view, uint64_t block, uint64_t rules, uint64_t objects,
                       uint64_t texture) {
          return n.frames == frames && n.nofog == nofog && n.view == view && n.block == block && n.rules == rules && n.objects == objects &&
                 n.texture == texture;
        };
        CHECK(591, same(counts(ClaySky::kDrawn, false), 1, 0, 0, 0, 0, 0, 0) && same(counts(ClaySky::kDrawn, true), 1, 0, 0, 0, 0, 0, 1) &&
                       same(counts(ClaySky::kNoFog, false), 1, 1, 0, 0, 0, 0, 0) && same(counts(ClaySky::kNoLight, false), 1, 1, 0, 0, 0, 0, 0) &&
                       same(counts(ClaySky::kView, false), 1, 0, 1, 0, 0, 0, 0) && same(counts(ClaySky::kBlock, false), 1, 0, 0, 1, 0, 0, 0) &&
                       same(counts(ClaySky::kRules, false), 1, 0, 0, 0, 1, 0, 0) && same(counts(ClaySky::kObjects, false), 1, 0, 0, 0, 0, 1, 0));
        // (a sky that is not drawn has no texture count, whatever the flag says; nothing is counted with the fog's switch off: every other verdict is a frame with it on,
        // a frame without light among them: final review, M5)
        CHECK(591, same(counts(ClaySky::kBlock, true), 1, 0, 0, 1, 0, 0, 0));
        CHECK(591, same(counts(ClaySky::kOff, true), 0, 0, 0, 0, 0, 0, 0) && same(counts(ClaySky::kCount, true), 0, 0, 0, 0, 0, 0, 0));
        ClaySkyFrames sum;
        CountClaySkyFrame(ClaySky::kBlock, false, sum);
        CountClaySkyFrame(ClaySky::kRules, false, sum);
        CountClaySkyFrame(ClaySky::kDrawn, false, sum);
        CHECK(591, same(sum, 3, 0, 0, 1, 1, 0, 0) && ClaySkyBlockFrames(sum) == 2);
        // The line.
        ClayStats st;
        CHECK(591, FormatSkyText(st) == "Sky: off");
        st.fog_switch = true;
        st.sky_block = true;
        st.sky = ClaySky::kDrawn;
        st.sky_before = 612;
        st.sky_ray[0] = 0.851f;
        st.sky_ray[1] = 0.495f;
        st.sky_ray[2] = -0.177f;
        st.sky_axis_cos = -0.783f;
        st.sky_before_no_depth = 2;
        st.sky_frames = 900;
        st.sky_block_frames = 4;
        st.sky_rules_frames = 5;
        st.sky_texture_frames = 3;
        st.sky_nofog_frames = 6;
        st.sky_view_frames = 2;
        st.sky_objects_frames = 1;
        CHECK(591, FormatSkyText(st) == "Sky: on, block valid, drawn | view (0.851, 0.495, -0.177), to the fog's axis -0.783 | before it 612 records, 2 without depth write, "
                                        "then the last record | "
                                        "frames since start 900: sky-block 9 {block: 4, rules: 5}, sky-texture 3, no-fog 6, view 2, no-objects 1, early 0");
        // (review of plan Task 5, M1: the pixel shader of the record at the sky's place, and the frames
        // whose sky was drawn before a terrain record)
        st.sky_place_record = true;
        st.sky_place_ps = 0x401A01FD4E5F8757ull;
        st.sky_early_frames = 7;
        CHECK(591, FormatSkyText(st) == "Sky: on, block valid, drawn | view (0.851, 0.495, -0.177), to the fog's axis -0.783 | before it 612 records, 2 without depth write, "
                                        "then pixel shader 0x401A01FD4E5F8757 | "
                                        "frames since start 900: sky-block 9 {block: 4, rules: 5}, sky-texture 3, no-fog 6, view 2, no-objects 1, early 7");
        {
          ClaySkyFrames n;
          CountClaySkyFrame(ClaySky::kDrawn, false, n, true);
          CountClaySkyFrame(ClaySky::kDrawn, false, n, false);
          CountClaySkyFrame(ClaySky::kBlock, false, n, true);   // (no sky: nothing was drawn early)
          CountClaySkyFrame(ClaySky::kOff, false, n, true);     // (the switch off: not counted at all)
          CHECK(591, n.frames == 3 && n.early == 1);
        }
        st.sky_textured = true;
        CHECK(591, FormatSkyText(st).starts_with("Sky: on, block valid, drawn with its textures | "));
        st.sky_textured = false;
        st.sky_texture_missing = true;
        CHECK(591, FormatSkyText(st).starts_with("Sky: on, block valid, drawn without its textures (sky-texture) | "));
        st.sky = ClaySky::kRules;
        st.sky_rule = SkyRule::kExtinction;
        CHECK(591, FormatSkyText(st).starts_with("Sky: on, block valid, not drawn (sky-block: extinction) | "));
        st.sky = ClaySky::kView;
        st.sky_view = FrameViewReason::kNoRecord;
        CHECK(591, FormatSkyText(st).starts_with("Sky: on, block valid, not drawn (view: no-record) | "));
        st.sky = ClaySky::kNoFog;
        CHECK(591, FormatSkyText(st).starts_with("Sky: on, block valid, not drawn (no fog: fog-block) | "));
        st.sky = ClaySky::kNoLight;
        CHECK(591, FormatSkyText(st).starts_with("Sky: on, block valid, not drawn (no light) | "));
        st.sky = ClaySky::kObjects;
        CHECK(591, FormatSkyText(st).starts_with("Sky: on, block valid, not drawn (no objects) | "));
        st.sky = ClaySky::kBlock;
        st.sky_block = false;
        st.sky_reason = capture::SkyBlockReason::kNoDraw;
        CHECK(591, FormatSkyText(st).starts_with("Sky: on, block invalid (sky-block: no-draw), not drawn (sky-block: block) | "));
        CHECK(591, std::string(capture::kSkyBlockReason) == "sky-block" && std::string(kSkyTextureReason) == "sky-texture");
      }

      // 592: the sky's place among the colour pass's records: before the first drawn record that
      // blends and is no terrain record, after the last record when there is none. Shown on record
      // lists by playing the pass: a pixel's depth and the last thing drawn at it.
      {
        const SkyOrderItem opaque{true, false, false, true}, layer{true, true, true, true}, blended{true, false, true, false},
            blended_depth{true, false, true, true}, opaque_no_depth{true, false, false, false}, not_drawn{false, false, true, false},
            flat_terrain{true, true, false, true};
        auto place = [](std::initializer_list<SkyOrderItem> list) { return SkyPlace(list.begin(), list.size()); };
        // The scene's order at the bridge: opaque, terrain layers, [sky], the blended group with opaque draws in it.
        SkyOrder o = place({opaque, opaque, layer, layer, blended, opaque, blended});
        CHECK(592, o.place == 4 && o.before == 4 && o.before_no_depth == 0 && !o.early);
        // Early (review of plan Task 5, M1): a drawn terrain record at or after the place. The sky is
        // then drawn before ground the game draws before its sky; everything after is drawn over it.
        o = place({opaque, layer, blended, layer, opaque});
        CHECK(592, o.place == 2 && o.before == 2 && o.early);
        o = place({opaque, layer, blended, flat_terrain});
        CHECK(592, o.place == 2 && o.early);
        o = place({opaque, layer, blended, SkyOrderItem{false, true, true, true}, opaque});
        CHECK(592, o.place == 2 && !o.early);  // (a terrain record that is not drawn has no say)
        o = place({opaque, layer, opaque});
        CHECK(592, o.place == 3 && !o.early);
        // No blended record: after everything. No terrain. No records at all. Only blended ones.
        o = place({opaque, layer, opaque});
        CHECK(592, o.place == 3 && o.before == 3 && o.before_no_depth == 0);
        o = place({opaque, opaque, blended});
        CHECK(592, o.place == 2 && o.before == 2);
        o = SkyPlace(nullptr, 0);
        CHECK(592, o.place == 0 && o.before == 0 && o.before_no_depth == 0);
        o = place({blended, blended_depth});
        CHECK(592, o.place == 0 && o.before == 0);
        // A record that is not drawn has no say; a terrain record never is the place, blended or flat.
        o = place({not_drawn, opaque, not_drawn, layer, flat_terrain, not_drawn});
        CHECK(592, o.place == 6 && o.before == 3);
        o = place({not_drawn, opaque, not_drawn, blended_depth, layer});
        CHECK(592, o.place == 3 && o.before == 1);
        // Records before the place that write no depth are counted.
        o = place({opaque_no_depth, opaque, opaque_no_depth, blended});
        CHECK(592, o.place == 3 && o.before == 3 && o.before_no_depth == 2);
        // ... a terrain layer that writes none among them (the captured layers all write depth).
        const SkyOrderItem layer_no_depth{true, true, true, false};
        o = place({opaque, layer_no_depth, layer, blended});
        CHECK(592, o.place == 3 && o.before == 3 && o.before_no_depth == 1);
        // The property, on every list of up to six records of the seven kinds: play the pass on one
        // pixel that every drawn record covers. The sky lands only if the depth is still clear at its
        // place. After the pass: the pixel never shows the sky when a record that writes depth was
        // drawn before the place or any record was drawn at or after it; no blended record that is
        // no terrain record lies before the place.
        const SkyOrderItem kinds[7] = {opaque, layer, blended, blended_depth, opaque_no_depth, not_drawn, flat_terrain};
        long lists = 0, sky_shown = 0;
        for (int len = 0; len <= 6; ++len) {
          int total = 1;
          for (int i = 0; i < len; ++i) total *= 7;
          for (int code = 0; code < total; ++code) {
            SkyOrderItem list[6];
            for (int i = 0, c = code; i < len; ++i, c /= 7) list[i] = kinds[c % 7];
            const SkyOrder at = SkyPlace(list, size_t(len));
            CHECK(592, at.place <= size_t(len));
            bool depth_clear = true, sky_on_top = false, covered_record = false;
            uint32_t before = 0, no_depth = 0;
            for (int i = 0; i <= len; ++i) {
              if (size_t(i) == at.place && depth_clear) {
                // (the sky: tested against the clear value, no depth write; what was drawn here before is covered)
                covered_record = before != 0;
                sky_on_top = true;
              }
              if (i == len || !list[i].drawn) continue;
              if (size_t(i) < at.place) {
                CHECK(592, !(list[i].blend && !list[i].terrain));  // no blended draw before the sky
                ++before;
                if (!list[i].depth_write) ++no_depth;
              }
              if (size_t(i) >= at.place) sky_on_top = false;  // drawn after the sky: over it
              if (list[i].depth_write) depth_clear = false;
            }
            CHECK(592, at.before == before && at.before_no_depth == no_depth);
            // The sky covered a record only if no record before it wrote depth: all of them are counted.
            if (covered_record) CHECK(592, at.before_no_depth == at.before && at.before != 0);
            if (at.before_no_depth == 0) CHECK(592, !covered_record);
            // Whatever is drawn at or after the place is never under the sky.
            bool any_after = false;
            for (int i = int(at.place); i < len; ++i) any_after = any_after || list[i].drawn;
            if (any_after) CHECK(592, !sky_on_top);
            ++lists;
            if (sky_on_top) ++sky_shown;
          }
        }
        CHECK(592, lists == 137257 && sky_shown > 0);
      }

      // 593: the view rays from the frame's view-projection (the bridge frame's matrices). A ray is
      // the direction seen at its screen point: put through the matrix as a direction, it comes out
      // at that point, at the picture's centre, edges and corners alike; it points forward; it does
      // not read the depth row or the translation.
      {
        auto screen_error = [&](const double n[3], double X, double Y) {
          // (the direction's clip position: rows 0, 1, 3 on (n, 0); the error in pixels of a 1120 x 720 picture)
          double c[4];
          for (int r = 0; r < 4; ++r) c[r] = double(fog_vp[r][0]) * n[0] + double(fog_vp[r][1]) * n[1] + double(fog_vp[r][2]) * n[2];
          if (!(c[3] > 0.0)) return 1e9;
          return std::fmax(std::fabs(c[0] / c[3] - X) * 560.0, std::fabs(c[1] / c[3] - Y) * 360.0);
        };
        CHECK(593, std::fabs(std::sqrt(double(basis[0][0]) * basis[0][0] + double(basis[0][1]) * basis[0][1] + double(basis[0][2]) * basis[0][2]) - 1.0) < 1e-6);
        double worst = 0.0, worst_inverse = 0.0, worst_angle = 0.0;
        int points = 0;
        const ClayFrameConstants frame = lit_frame();
        for (float py : {0.5f, 1.5f, 180.5f, 360.5f, 540.5f, 718.5f, 719.5f}) {
          for (float px : {0.5f, 1.5f, 280.5f, 560.5f, 840.5f, 1118.5f, 1119.5f}) {
            float ray[3];
            SkyPixelRay(real_k, px, py, frame.mask_du, frame.mask_dv, ray);
            const double X = double(px) * 2.0 / 1120.0 - 1.0, Y = 1.0 - double(py) * 2.0 / 720.0;
            const double n[3] = {ray[0], ray[1], ray[2]};
            const double err = screen_error(n, X, Y);
            if (err > worst) worst = err;
            // The same ray through the single-precision inverse, as a shader would form it from two
            // depths of the pixel (0.25 and 0.75): the same line, with the inverse's cancellation.
            float P[2][3];
            for (int k = 0; k < 2; ++k) {
              const float S[4] = {px * (2.0f * frame.mask_du) - 1.0f, 1.0f - py * (2.0f * frame.mask_dv), k ? 0.75f : 0.25f, 1.0f};
              float h[4];
              for (int r = 0; r < 4; ++r) h[r] = fog_inv[r][0] * S[0] + fog_inv[r][1] * S[1] + fog_inv[r][2] * S[2] + fog_inv[r][3] * S[3];
              for (int i = 0; i < 3; ++i) P[k][i] = h[i] / h[3];
            }
            const double m[3] = {double(P[1][0]) - P[0][0], double(P[1][1]) - P[0][1], double(P[1][2]) - P[0][2]};
            const double inverse_err = screen_error(m, X, Y);
            if (inverse_err > worst_inverse) worst_inverse = inverse_err;
            const double dot = (n[0] * m[0] + n[1] * m[1] + n[2] * m[2]) /
                               std::sqrt((n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) * (m[0] * m[0] + m[1] * m[1] + m[2] * m[2]));
            const double angle = std::acos(std::fmin(1.0, dot));
            if (angle > worst_angle) worst_angle = angle;
            ++points;
          }
        }
        std::printf("sky: the view ray's error over %d pixels (centre, edges, corners; the bridge frame's matrices): worst %.6f pixel "
                    "from the view-projection's rows; %.4f pixel for the same ray through the single-precision inverse at two depths "
                    "(the two rays differ by at most %.2e radian)\n",
                    points, worst, worst_inverse, worst_angle);
        // Measured: under a thousandth of a pixel anywhere on the picture; the inverse's route is
        // the same line within its own error (a few hundredths of a pixel).
        CHECK(593, points == 49 && worst < 1e-3 && worst_inverse < 0.5 && worst_angle < 1e-3);
        // The basis does not read the depth row or the translation column: change both, the same rays.
        {
          float other[4][4];
          std::memcpy(other, fog_vp, sizeof(other));
          for (int j = 0; j < 4; ++j) other[2][j] = 17.0f * float(j + 1);
          for (int i = 0; i < 4; ++i) other[i][3] = -1000.0f + 600.0f * float(i);
          float b2[3][3];
          CHECK(593, SkyRayBasis(other, b2) && std::memcmp(b2, basis, sizeof(basis)) == 0);
        }
        // A matrix with the other handedness of its rows still gives the forward ray; a matrix
        // without one, or with an entry that is no number, gives none and writes nothing.
        {
          float flipped[4][4];
          std::memcpy(flipped, fog_vp, sizeof(flipped));
          for (int j = 0; j < 4; ++j) flipped[0][j] = -flipped[0][j];
          float b2[3][3];
          CHECK(593, SkyRayBasis(flipped, b2));
          const double fwd = double(b2[0][0]) * fog_vp[3][0] + double(b2[0][1]) * fog_vp[3][1] + double(b2[0][2]) * fog_vp[3][2];
          CHECK(593, fwd > 0.0 && std::fabs(double(b2[0][0]) - basis[0][0]) < 1e-6 && std::fabs(double(b2[0][2]) - basis[0][2]) < 1e-6);
          float untouched[3][3];
          std::memset(untouched, 0x5A, sizeof(untouched));
          float before[3][3];
          std::memcpy(before, untouched, sizeof(before));
          float flat[4][4];
          std::memcpy(flat, fog_vp, sizeof(flat));
          for (int j = 0; j < 4; ++j) flat[3][j] = flat[0][j];  // (the w row is the x row: no forward direction)
          CHECK(593, !SkyRayBasis(flat, untouched) && std::memcmp(untouched, before, sizeof(before)) == 0);
          const float zero[4][4] = {};
          CHECK(593, !SkyRayBasis(zero, untouched) && !SkyRayBasis(nullptr, untouched) && !SkyRayBasis(fog_vp, nullptr));
          for (float bad : {qnan, pinf, -pinf}) {
            for (int r : {0, 1, 3}) {
              float m[4][4];
              std::memcpy(m, fog_vp, sizeof(m));
              m[r][1] = bad;
              CHECK(593, !SkyRayBasis(m, untouched));
            }
          }
          CHECK(593, std::memcmp(untouched, before, sizeof(before)) == 0);
        }
      }

      // 594: the sky of a pixel as the step computes it: the pixel's ray in single precision, then
      // the mirror with the frame's block (the fog's axis and in-scatter table in its tail).
      {
        ClayFrameConstants frame = lit_frame();
        CHECK(594, SetClayFrameFog(frame, real, true, fog_inv));
        double worst = 0.0;
        for (float py : {0.5f, 200.5f, 360.5f, 719.5f}) {
          for (float px : {0.5f, 560.5f, 1119.5f}) {
            // The true direction of the pixel, from the view-projection's rows in double.
            const double X = double(px) * 2.0 / 1120.0 - 1.0, Y = 1.0 - double(py) * 2.0 / 720.0;
            double a[3], b[3];
            for (int i = 0; i < 3; ++i) {
              a[i] = double(fog_vp[0][i]) - X * double(fog_vp[3][i]);
              b[i] = double(fog_vp[1][i]) - Y * double(fog_vp[3][i]);
            }
            double t[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
            const double forward = t[0] * fog_vp[3][0] + t[1] * fog_vp[3][1] + t[2] * fog_vp[3][2];
            const float true_ray[3] = {float(forward > 0.0 ? t[0] : -t[0]), float(forward > 0.0 ? t[1] : -t[1]), float(forward > 0.0 ? t[2] : -t[2])};
            double want[3], got[3], got_tex[3], want_tex[3];
            SkyMirror(true_ray, real_k, real.axis, real.scatter, nullptr, nullptr, want);
            SkyPixelMirror(px, py, frame, real_k, nullptr, nullptr, got);
            SkyMirror(true_ray, made_k, real.axis, real.scatter, s13, s14, want_tex);
            SkyPixelMirror(px, py, frame, made_k, s13, s14, got_tex);
            for (int i = 0; i < 3; ++i) {
              worst = std::fmax(worst, std::fmax(std::fabs(got[i] - want[i]), std::fabs(got_tex[i] - want_tex[i])));
            }
            CHECK(594, finite3(got) && got[0] > 0.0 && got[2] > 0.0);
          }
        }
        std::printf("sky: a pixel's sky from its single-precision ray against the true ray's, 12 pixels: worst difference %.2e\n", worst);
        CHECK(594, worst < 1e-5);
        // The bridge's view looks away from the fog's axis and slightly down: the picture's top row is
        // above the horizon, its bottom row below, and the centre's ray is the matrix's forward axis.
        float top[3], bottom[3], centre[3];
        SkyPixelRay(real_k, 560.0f, 0.5f, frame.mask_du, frame.mask_dv, top);
        SkyPixelRay(real_k, 560.0f, 719.5f, frame.mask_du, frame.mask_dv, bottom);
        SkyPixelRay(real_k, 560.0f, 360.0f, frame.mask_du, frame.mask_dv, centre);
        CHECK(594, top[2] > 0.0f && bottom[2] < 0.0f && top[2] > centre[2] && centre[2] > bottom[2]);
        const double fwd_len = std::sqrt(double(fog_vp[3][0]) * fog_vp[3][0] + double(fog_vp[3][1]) * fog_vp[3][1] + double(fog_vp[3][2]) * fog_vp[3][2]);
        for (int i = 0; i < 3; ++i) CHECK(594, std::fabs(double(centre[i]) - fog_vp[3][i] / fwd_len) < 1e-5);
        // A frame without the fog's tail (no sky is drawn of it): the mirror still gives numbers.
        const ClayFrameConstants off = lit_frame();
        double out[3];
        SkyPixelMirror(560.5f, 100.5f, off, real_k, s13, s14, out);
        CHECK(594, finite3(out));
        for (float bad : {qnan, pinf, -pinf}) {
          SkyPixelMirror(bad, 100.5f, frame, real_k, s13, s14, out);
          CHECK(594, finite3(out));
          SkyPixelMirror(560.5f, bad, frame, real_k, nullptr, nullptr, out);
          CHECK(594, finite3(out));
        }
      }
    }
  }
  // 600-619: the native post (plan Task 5 of the post sub-project; clay_logic.h, "Native post"): the frame's
  // decision, the unlit scale, the table's addressing, the composite's and the ramp's mirrors against the
  // reference's numbers (tools/xdk_sigmatch/post_pins.py; tests/test_post_literals.py holds the lines below to
  // its output), the step's constants, the frame's two words and the Post line.
  {
    const float kNaN = std::numeric_limits<float>::quiet_NaN(), kInf = std::numeric_limits<float>::infinity();
    auto in01 = [](double v) { return v >= 0.0 && v <= 1.0; };  // (false for a value that is no number)
    // The excerpt's table decoded as the block decodes it: texel i the float stored high byte first.
    float table[512];
    for (uint32_t i = 0; i < 512; ++i) {
      const unsigned char* p = kPinsPostTable + 4 * i;
      const uint32_t bits = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
      std::memcpy(&table[i], &bits, 4);
    }
    // A block of a register set with that table, and a ramp of 256 rising entries (a channel apart).
    auto block_of = [&](const float composite[6], const float bright[5]) {
      capture::PostBlock b{};
      std::memcpy(b.composite, composite, sizeof(b.composite));
      std::memcpy(b.bright, bright, sizeof(b.bright));
      std::memcpy(b.table, table, sizeof(b.table));
      b.valid = true;
      return b;
    };
    static float ramp[256][3];
    for (int i = 0; i < 256; ++i) {
      for (int c = 0; c < 3; ++c) ramp[i][c] = float(i * 4 + (i > 127 ? c : 0)) / 1023.0f;
    }
    // real registers: composite c31.x, c46.x, c47.x, c77.x, c77.y, c78.x {2.972994565963745f, 0.25f, 50.0f, -0.005211588926613331f, 1.10260808467865f, 0.45454543828964233f}; bright c31.x, c46.x, c47.x, c77.x, c77.y {1.4864972829818726f, 0.20000000298023224f, 2.0f, 0.08694084733724594f, 0.11846417933702469f}
    const float real_composite[6] = {2.972994565963745f, 0.25f, 50.0f, -0.005211588926613331f, 1.10260808467865f, 0.45454543828964233f};
    const float real_bright[5] = {1.4864972829818726f, 0.20000000298023224f, 2.0f, 0.08694084733724594f, 0.11846417933702469f};
    // made registers: composite c31.x, c46.x, c47.x, c77.x, c77.y, c78.x {1.5f, 0.5f, 3.0f, 0.25f, 0.8999999761581421f, 0.6000000238418579f}; bright c31.x, c46.x, c47.x, c77.x, c77.y {2.0f, 0.5f, 1.5f, 0.019999999552965164f, 0.20000000298023224f}
    const float made_composite[6] = {1.5f, 0.5f, 3.0f, 0.25f, 0.8999999761581421f, 0.6000000238418579f};
    const float made_bright[5] = {2.0f, 0.5f, 1.5f, 0.019999999552965164f, 0.20000000298023224f};
    const capture::PostBlock real_block = block_of(real_composite, real_bright), made_block = block_of(made_composite, made_bright);

    // 600: the decision, every combination of its seven inputs: one chain, the first thing missing
    // decides, and only the frame that lacks nothing is a post frame. With the switch off the verdict
    // is kOff whatever else holds: the frame is today's (frame-map section 17, "Native composite").
    {
      int drawn = 0, off = 0;
      for (uint32_t bits = 0; bits < 128; ++bits) {
        ClayPostInputs in;
        in.post = bits & 1;
        in.light = bits & 2;
        in.lit = bits & 4;
        in.block = bits & 8;
        in.ramp = bits & 16;
        in.objects = bits & 32;
        in.buffer = bits & 64;
        const ClayPost want = !in.post                 ? ClayPost::kOff
                              : !in.light || !in.lit   ? ClayPost::kNoLight
                              : !in.block              ? ClayPost::kBlock
                              : !in.ramp               ? ClayPost::kRamp
                              : !in.objects            ? ClayPost::kObjects
                              : !in.buffer             ? ClayPost::kBuffer
                                                       : ClayPost::kDrawn;
        const ClayPost got = DecideClayPost(in).post;
        CHECK(600, got == want);
        CHECK(600, (got == ClayPost::kDrawn) == (bits == 127) && (got == ClayPost::kOff) == !(bits & 1));
        drawn += got == ClayPost::kDrawn;
        off += got == ClayPost::kOff;
      }
      CHECK(600, drawn == 1 && off == 64 && DecideClayPost(ClayPostInputs{}).post == ClayPost::kOff);
      static_assert(DecideClayPost(ClayPostInputs{true, true, true, true, true, true, true}).post == ClayPost::kDrawn);
      static_assert(DecideClayPost(ClayPostInputs{false, true, true, true, true, true, true}).post == ClayPost::kOff);
      // The frame counts follow from the verdict; a frame with the switch off is not counted.
      ClayPostFrames n;
      for (ClayPost p : {ClayPost::kOff, ClayPost::kNoLight, ClayPost::kBlock, ClayPost::kBlock, ClayPost::kRamp, ClayPost::kObjects,
                         ClayPost::kBuffer, ClayPost::kDrawn, ClayPost::kDrawn, ClayPost::kDrawn, ClayPost::kCount}) {
        CountClayPostFrame(p, n);
      }
      CHECK(600, n.frames == 9 && n.nolight == 1 && n.block == 2 && n.ramp == 1 && n.objects == 1 && n.buffer == 1 && n.drawn == 3);
      CHECK(600, std::string(capture::kPostBlockReason) == "post-block" && std::string(kPostRampReason) == "post-ramp" &&
                     std::string(kPostObjectsReason) == "post-objects");
    }

    // 601: the table's addressing (controller ruling 19): the NEAREST texel, floor(u * 512 + 0.5) clamped to
    // 511, not floor(u * 512). The pins at (k + 0.5) / 512 -+ 1e-6 are the telling ones (a floor would read
    // k on both sides); those at k / 512 lie in the middle of texel k.
    {
      static const struct { float u; uint32_t texel; } kPins[] = {
        // table texel pins: u, the texel of a 512-texel table (light_formulas.post_table_texel: floor(u * 512 + 0.5), clamped)
        {-9.999999974752427e-07f, 0},
        {0.0f, 0},
        {9.999999974752427e-07f, 0},
        {0.0019521249923855066f, 1},
        {0.001953125f, 1},
        {0.0019541250076144934f, 1},
        {0.0039052499923855066f, 2},
        {0.00390625f, 2},
        {0.00390724977478385f, 2},
        {0.023436499759554863f, 12},
        {0.0234375f, 12},
        {0.023438500240445137f, 12},
        {0.2499990016222f, 128},
        {0.25f, 128},
        {0.2500010132789612f, 128},
        {0.9980458617210388f, 511},
        {0.998046875f, 511},
        {0.9980478882789612f, 511},
        {0.9999989867210388f, 511},
        {1.0f, 511},
        {1.0000009536743164f, 511},
        {0.0009755624923855066f, 0},
        {0.0009775625076144934f, 1},
        {0.0029286874923855066f, 1},
        {0.0029306875076144934f, 2},
        {0.024413062259554863f, 12},
        {0.024415062740445137f, 13},
        {0.2490224391222f, 127},
        {0.2490244358778f, 128},
        {0.2509755492210388f, 128},
        {0.2509775757789612f, 129},
        {0.9970692992210388f, 510},
        {0.9970713257789612f, 511},
        {0.9990224242210388f, 511},
        {0.9990244507789612f, 511},
        {-1.0f, 0},
        {2.5f, 511},
        {1.0000000150474662e+30f, 511},
        {kNaN, 0},
        {kInf, 511},
        {-kInf, 0},
      };
      CHECK(601, std::size(kPins) == 41);
      int halves = 0;
      for (const auto& p : kPins) {
        CHECK(601, PostTableTexel(p.u) == p.texel);
        CHECK(601, PostTableTexel(p.u) < capture::kPostTableTexels);
        // (the pins a floor would fail: a coordinate inside the table whose nearest texel is not its floor)
        if (p.u > 0.0f && p.u < 0.99f && uint32_t(std::floor(double(p.u) * 512.0)) != p.texel) ++halves;
      }
      CHECK(601, halves >= 8);
      // Every texel is reached, in order, from the centre of its own interval; no index leaves the table
      // for any float, the largest and the smallest included.
      for (uint32_t k = 0; k < 512; ++k) CHECK(601, PostTableTexel(float(k) / 512.0f) == k);
      for (float u : {kNaN, kInf, -kInf, 3.4e38f, -3.4e38f, 1e-45f, -0.0f, 0.99999994f, 1.0000001f}) {
        CHECK(601, PostTableTexel(u) < capture::kPostTableTexels);
      }
      CHECK(601, PostTableTexel(kNaN) == 0 && PostTableTexel(kInf) == 511 && PostTableTexel(1e-45f) == 0);
      static_assert(kPostUnlitTexel == 128 && capture::kPostTableTexels == 512);
    }

    // 602: the unlit scale (spec ruling 9): 1 / (c77.y * T[128]), at most 64; 64 for a gain that is no
    // number or not above 1 / 64. A frame without a block has the word of a frame without post: 1.
    {
      static const struct { float c77y, t128; double scale; } kPins[] = {
        // unlit scale pins: c77.y, T[128], the scale (the two sets with the excerpt's table, then made-up pairs)
        {1.10260808467865f, 0.24269776046276093f, 3.7369136159839127},  // real
        {0.8999999761581421f, 0.24269776046276093f, 4.578168082090692},  // made
        {1.0f, 0.0f, 64.0},
        {1.0f, 0.015625f, 64.0},
        {1.0f, 0.019999999552965164f, 50.00000111758712},
        {64.0f, 3.999000072479248f, 0.0039072267358857575},
        {kNaN, 0.25f, 64.0},
        {1.0f, kNaN, 64.0},
        {-1.0f, 0.25f, 64.0},
        {kInf, 0.25f, 64.0},
      };
      CHECK(602, std::size(kPins) == 10);
      for (size_t i = 0; i < std::size(kPins); ++i) {
        capture::PostBlock b{};
        b.valid = true;
        b.composite[4] = kPins[i].c77y;
        b.table[kPostUnlitTexel] = kPins[i].t128;
        const float got = PostUnlitScale(b);
        CHECK(602, std::fabs(double(got) - kPins[i].scale) <= 1e-6 * kPins[i].scale);
        CHECK(602, got > 0.0f && got <= kPostUnlitMax);
      }
      CHECK(602, std::fabs(double(PostUnlitScale(real_block)) - 3.7369136159839127) < 4e-6 &&
                     std::fabs(double(PostUnlitScale(made_block)) - 4.578168082090692) < 5e-6);
      CHECK(602, PostUnlitScale(capture::PostBlock{}) == 1.0f && kPostUnlitMax == 64.0f && kPostUnlitMax <= kClayLitMax);
    }

    // 603: the ramp's entry of a front-target value: min(255, floor(v * 255 + 0.5)), 0 for a value that is
    // no number or not above 0; and the mirror's read of one channel.
    {
      static const struct { float v; uint32_t entry; } kPins[] = {
        // ramp pins: the front target's value, the ramp's entry (min(255, floor(v * 255 + 0.5)); 0 for no number)
        {0.0f, 0},
        {0.001959784422069788f, 0},
        {0.001961784204468131f, 1},
        {0.09019608050584793f, 23},
        {0.5f, 128},
        {0.39407843351364136f, 100},
        {0.39415687322616577f, 101},
        {0.9980382323265076f, 254},
        {0.9980401992797852f, 255},
        {1.0f, 255},
        {1.5f, 255},
        {-0.25f, 0},
        {kNaN, 0},
        {kInf, 255},
        {-kInf, 0},
      };
      CHECK(603, std::size(kPins) == 15);
      float channel[256];
      for (int i = 0; i < 256; ++i) channel[i] = float(i) * 0.001f + 0.5f;
      for (const auto& p : kPins) {
        CHECK(603, PostRampIndex(p.v) == p.entry && PostRampIndex(p.v) < 256);
        CHECK(603, PostRampMirror(p.v, channel) == channel[p.entry]);
      }
      // Every 8-bit level of the front target reads its own entry.
      for (uint32_t n = 0; n < 256; ++n) CHECK(603, PostRampIndex(float(n) / 255.0f) == n);
      for (float v : {3.4e38f, -3.4e38f, 1e-45f}) CHECK(603, PostRampIndex(v) < 256);
    }

    // 604-607: the step's constants and the composite's mirror against the reference.
    {
      static_assert(sizeof(ClayPostConstants) == 6288 && kClayPostSlots == 25 && offsetof(ClayPostConstants, scene_max) == 24 &&
                    offsetof(ClayPostConstants, previous) == 28 && offsetof(ClayPostConstants, bright) == 32 &&
                    offsetof(ClayPostConstants, bloom_on) == 52 && offsetof(ClayPostConstants, sun_on) == 56 &&
                    offsetof(ClayPostConstants, blur) == 64 && offsetof(ClayPostConstants, radial) == 128 &&
                    offsetof(ClayPostConstants, table) == 144 && offsetof(ClayPostConstants, ramp) == 2192);
      ClayPostConstants real_k, made_k;
      CHECK(604, MakeClayPostConstants(real_block, ramp, false, false, &real_k) && MakeClayPostConstants(made_block, ramp, false, false, &made_k));
      CHECK(604, real_k.scene_max == 31.875f && real_k.scene_max == kPostSceneMax && real_k.previous == 0.0f && real_k.bloom_on == 0 &&
                     real_k.sun_on == 0 && real_k.pad == 0);
      for (int i = 0; i < 6; ++i) CHECK(604, real_k.composite[i] == real_composite[i] && made_k.composite[i] == made_composite[i]);
      for (int i = 0; i < 5; ++i) CHECK(604, real_k.bright[i] == real_bright[i]);
      // Texel i at [i >> 2][i & 3]; entry i's rgb at [i], the fourth float 0.
      for (uint32_t i = 0; i < 512; ++i) CHECK(604, real_k.table[i >> 2][i & 3] == table[i]);
      for (int i = 0; i < 256; ++i) {
        CHECK(604, real_k.ramp[i][0] == ramp[i][0] && real_k.ramp[i][1] == ramp[i][1] && real_k.ramp[i][2] == ramp[i][2] && real_k.ramp[i][3] == 0.0f);
      }
      CHECK(604, table[0] == 0.0f && table[128] == 0.24269776046276093f);
      // Without the blur part the weights are zero and the bloom's flag stays 0 whatever is asked; with it
      // the fourteen weights lie in four rows (x: 0 to 3 and 4 to 6, then y) and the flag follows the ask.
      capture::PostBlock blurred = real_block;
      for (int s = 0; s < 2; ++s) {
        for (int i = 0; i < 7; ++i) blurred.blur[s][i] = 0.01f * float(1 + i + 7 * s);
      }
      ClayPostConstants k;
      CHECK(604, MakeClayPostConstants(blurred, ramp, true, true, &k) && k.bloom_on == 0 && k.sun_on == 0 && k.blur[0][0] == 0.0f);
      blurred.parts = capture::kPostPartBlur;
      CHECK(604, MakeClayPostConstants(blurred, ramp, true, true, &k) && k.bloom_on == 1 && k.sun_on == 0);
      auto w = [](int n) { return 0.01f * float(n); };
      CHECK(604, k.blur[0][0] == w(1) && k.blur[0][3] == w(4) && k.blur[1][0] == w(5) && k.blur[1][2] == w(7) && k.blur[1][3] == 0.0f &&
                     k.blur[2][0] == w(8) && k.blur[2][3] == w(11) && k.blur[3][0] == w(12) && k.blur[3][2] == w(14) && k.blur[3][3] == 0.0f);
      CHECK(604, MakeClayPostConstants(blurred, ramp, false, false, &k) && k.bloom_on == 0 && k.blur[0][0] == w(1));
      // A weight outside 0..1, or no number: it is 0 in the constants AND the bloom's flag stays 0 (no kernel short
      // of a tap is drawn on the strength of the flag: review-task-8, Minor 1); the frame is still a post frame.
      for (float bad : {1.0005f, -0.01f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        for (int at : {0, 6, 7, 13}) {
          capture::PostBlock off = blurred;
          off.blur[at / 7][at % 7] = bad;
          CHECK(604, MakeClayPostConstants(off, ramp, true, true, &k) && k.bloom_on == 0 && k.sun_on == 0 &&
                         k.blur[2 * (at / 7) + ((at % 7) >> 2)][(at % 7) & 3] == 0.0f);
        }
      }
      CHECK(604, MakeClayPostConstants(blurred, ramp, true, true, &k) && k.bloom_on == 1);

      // 605: what is refused, each with the constants left all zero: an invalid block, no ramp, a register
      // that is no number or outside the block's own bound (checked again here: no value reaches a shader
      // on the strength of a flag), a texel outside [0, 4), a ramp entry outside [0, 1] or out of order.
      const ClayPostConstants zero{};
      auto refused = [&](const capture::PostBlock& b, const float (*r)[3]) {
        ClayPostConstants out;
        std::memset(&out, 0xFF, sizeof(out));
        return !MakeClayPostConstants(b, r, false, false, &out) && std::memcmp(&out, &zero, sizeof(out)) == 0;
      };
      capture::PostBlock bad = real_block;
      bad.valid = false;
      CHECK(605, refused(bad, ramp) && refused(real_block, nullptr) && !MakeClayPostConstants(real_block, ramp, false, false, nullptr));
      const struct { int index; float value; } kBadComposite[] = {
          {0, kNaN}, {0, -0.001f}, {0, 64.5f}, {1, 0.0f}, {1, kInf}, {1, 64.5f}, {2, -1.0f}, {2, 2e6f}, {3, 4.5f}, {3, -4.5f}, {3, kNaN},
          {4, 0.0f}, {4, 64.5f}, {4, -kInf}, {5, 0.0f}, {5, 1.0001f}, {5, kNaN}};
      for (const auto& c : kBadComposite) {
        bad = real_block;
        bad.composite[c.index] = c.value;
        CHECK(605, refused(bad, ramp));
      }
      const struct { int index; float value; } kBadBright[] = {{0, kNaN}, {0, 64.5f}, {1, -0.5f}, {2, 0.0f}, {2, 65.0f}, {3, 5.0f}, {4, 0.0f}, {4, kInf}};
      for (const auto& c : kBadBright) {
        bad = real_block;
        bad.bright[c.index] = c.value;
        CHECK(605, refused(bad, ramp));
      }
      for (float texel : {kNaN, kInf, -kInf, -1e-6f, 4.0f}) {
        for (uint32_t at : {0u, 128u, 511u}) {
          bad = real_block;
          bad.table[at] = texel;
          CHECK(605, refused(bad, ramp));
        }
      }
      // (the bounds themselves are inside)
      bad = real_block;
      bad.composite[0] = 64.0f;
      bad.composite[1] = 64.0f;
      bad.composite[2] = 1e6f;
      bad.composite[3] = -4.0f;
      bad.composite[4] = 64.0f;
      bad.composite[5] = 1.0f;
      bad.table[511] = std::nextafter(4.0f, 0.0f);
      ClayPostConstants edge_k;
      CHECK(605, MakeClayPostConstants(bad, ramp, false, false, &edge_k));
      static float bad_ramp[256][3];
      for (float entry : {kNaN, kInf, -0.001f, 1.001f}) {
        std::memcpy(bad_ramp, ramp, sizeof(bad_ramp));
        bad_ramp[200][1] = entry;
        CHECK(605, refused(real_block, bad_ramp));
      }
      std::memcpy(bad_ramp, ramp, sizeof(bad_ramp));
      bad_ramp[100][2] = bad_ramp[99][2] - 0.001f;  // out of order
      CHECK(605, refused(real_block, bad_ramp) && !PostRampUsable(bad_ramp));
      std::memcpy(bad_ramp, ramp, sizeof(bad_ramp));
      bad_ramp[100][2] = bad_ramp[99][2];  // (equal neighbours are in order: a flat stretch)
      CHECK(605, MakeClayPostConstants(real_block, bad_ramp, false, false, &k));
      CHECK(605, PostRampUsable(ramp) && PostRampUsable(bad_ramp) && !PostRampUsable(nullptr));

      // 606: the composite against the reference (light_formulas.post_composite), both register sets, with
      // the table's texel each point reads. Among the points: a scene channel that is no number, an
      // infinity, a negative one, a value above the scene's clamp, and a bloom that is no number.
      struct CompositePin { float scene[3], bloom[3]; uint32_t texel; double out[3]; };
      static const CompositePin kReal[] = {
        // real composite pins (the excerpt's table): scene, bloom, the table's texel, the front value
        {{0.6000000238418579f, 0.550000011920929f, 0.4000000059604645f}, {0.0f, 0.0f, 0.0f}, 70, {0.4214398567838915, 0.4051769275027731, 0.350885269768029}},
        {{2.5f, 2.4000000953674316f, 2.200000047683716f}, {0.05000000074505806f, 0.05000000074505806f, 0.05999999865889549f}, 308, {0.9088072669106293, 0.8952616204735997, 0.872425955666359}},
        {{0.003000000026077032f, 0.0020000000949949026f, 0.0010000000474974513f}, {0.0f, 0.0f, 0.0f}, 0, {0.0, 0.0, 0.0}},
        {{0.004000000189989805f, 0.0044999998062849045f, 0.004000000189989805f}, {0.0f, 0.0f, 0.0f}, 1, {0.03029541777417399, 0.03064171611944725, 0.03029541777417399}},
        {{0.019999999552965164f, 0.029999999329447746f, 0.009999999776482582f}, {0.009999999776482582f, 0.0f, 0.0f}, 3, {0.21033910517523582, 0.08422364060150728, 0.0516774235894812}},
        {{0.20000000298023224f, 0.05000000074505806f, 0.30000001192092896f}, {0.0f, 0.0f, 0.0f}, 13, {0.24265157730704884, 0.12967586916339888, 0.29164382390294946}},
        {{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, 128, {1.0, 1.0, 1.0}},
        {{3.9000000953674316f, 3.9000000953674316f, 3.9000000953674316f}, {0.0f, 0.0f, 0.0f}, 499, {1.0, 1.0, 1.0}},
        {{40.0f, 0.5f, 0.5f}, {0.0f, 0.0f, 0.0f}, 511, {0.9984631056366018, 0.4561292391910362, 0.4561292391910362}},
        {{0.0f, 0.0f, 0.0f}, {0.20000000298023224f, 0.30000001192092896f, 0.4000000059604645f}, 0, {0.790301372085062, 0.949187898597747, 0.9996126329972699}},
        {{kNaN, 0.5f, 0.5f}, {0.10000000149011612f, 0.10000000149011612f, 0.10000000149011612f}, 50, {0.5764660021784105, 0.6456876995275248, 0.6456876995275248}},
        {{kInf, -1.0f, 0.20000000298023224f}, {kNaN, kInf, -0.5f}, 511, {0.9998412735077766, 0.9998412735077766, 0.3085638410837448}},
      };
      static const CompositePin kMade[] = {
        // made composite pins (the excerpt's table): scene, bloom, the table's texel, the front value
        {{0.6000000238418579f, 0.550000011920929f, 0.4000000059604645f}, {0.0f, 0.0f, 0.0f}, 141, {0.301511444844308, 0.28265719769909525, 0.22010954390127868}},
        {{2.5f, 2.4000000953674316f, 2.200000047683716f}, {0.05000000074505806f, 0.05000000074505806f, 0.05999999865889549f}, 511, {0.8407916234366455, 0.8180950360626932, 0.7762553481150124}},
        {{0.003000000026077032f, 0.0020000000949949026f, 0.0010000000474974513f}, {0.0f, 0.0f, 0.0f}, 1, {0.005791209344443126, 0.0057782112648116744, 0.0057651936615754785}},
        {{0.004000000189989805f, 0.0044999998062849045f, 0.004000000189989805f}, {0.0f, 0.0f, 0.0f}, 1, {0.00884786129232474, 0.008857824759342634, 0.00884786129232474}},
        {{0.019999999552965164f, 0.029999999329447746f, 0.009999999776482582f}, {0.009999999776482582f, 0.0f, 0.0f}, 7, {0.10143626444081504, 0.033907748794771896, 0.03203083407406161}},
        {{0.20000000298023224f, 0.05000000074505806f, 0.30000001192092896f}, {0.0f, 0.0f, 0.0f}, 26, {0.11159444714009235, 0.08139620594050266, 0.12896402461293227}},
        {{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, 256, {1.0, 1.0, 1.0}},
        {{3.9000000953674316f, 3.9000000953674316f, 3.9000000953674316f}, {0.0f, 0.0f, 0.0f}, 511, {1.0, 1.0, 1.0}},
        {{40.0f, 0.5f, 0.5f}, {0.0f, 0.0f, 0.0f}, 511, {1.0, 0.24960538349993153, 0.24960538349993153}},
        {{0.0f, 0.0f, 0.0f}, {0.20000000298023224f, 0.30000001192092896f, 0.4000000059604645f}, 0, {0.45357830497898444, 0.6236753233075342, 0.7670635229785596}},
        {{kNaN, 0.5f, 0.5f}, {0.10000000149011612f, 0.10000000149011612f, 0.10000000149011612f}, 101, {0.29701177328174616, 0.43015871661694743, 0.43015871661694743}},
        {{kInf, -1.0f, 0.20000000298023224f}, {kNaN, kInf, -0.5f}, 511, {1.0, 1.0, 0.33403624790515585}},
      };
      CHECK(606, std::size(kReal) == 12 && std::size(kMade) == 12);
      double worst = 0.0;
      int texel0 = 0, half = 0;
      for (int set = 0; set < 2; ++set) {
        const ClayPostConstants& c = set ? made_k : real_k;
        for (const CompositePin& p : set ? kMade : kReal) {
          double out[3];
          PostCompositeMirror(p.scene, p.bloom, c, out);
          const double u = PostCompositeCoordinate(p.scene, c);
          CHECK(606, PostTableTexel(float(u)) == p.texel);
          for (int i = 0; i < 3; ++i) {
            CHECK(606, in01(out[i]));
            worst = std::fmax(worst, std::fabs(out[i] - p.out[i]));
          }
          texel0 += p.texel == 0;
          half += p.texel != uint32_t(std::floor(u * 512.0)) && p.texel < 511;
        }
      }
      std::printf("post: the composite's mirror against the reference, 24 points: worst difference %.2e\n", worst);
      CHECK(606, worst < 1e-12);
      // (points in the table's texel 0, which is 0: black; and points whose nearest texel is not the floor)
      CHECK(606, texel0 >= 2 && half >= 4);

      // 607: nothing that is no number leaves the composite (review focus 2). The rule: a scene channel
      // not above 0 (a NaN and a negative among them) is 0, one above 31.875 (an infinity among them) is
      // 31.875; a bloom channel that is no number is 0; a register or a table texel that is no number gives
      // black. Every output is a number in [0, 1] for every combination below, with every register set,
      // the one at the block's bounds included, and a table whose texels are at their bound.
      const float kValues[] = {kNaN, kInf, -kInf, -1.0f, -0.0f, 0.0f, 1e-45f, 1e-6f, 0.004f, 0.5f, 1.0f, 3.9f, 31.875f, 32.0f, 1e30f, 3.4e38f};
      ClayPostConstants high_k = real_k;
      for (auto& row : high_k.table) {
        for (float& t : row) t = std::nextafter(4.0f, 0.0f);
      }
      long checked = 0;
      for (const ClayPostConstants* c : {&real_k, &made_k, &edge_k, &high_k}) {
        for (float r : kValues) {
          for (float g : kValues) {
            for (float b : {kNaN, kInf, -2.0f, 0.0f, 0.3f, 40.0f}) {
              for (float bloom : {kNaN, kInf, -kInf, -0.5f, 0.0f, 0.2f, 5.0f}) {
                const float scene[3] = {r, g, b}, bl[3] = {bloom, 0.1f, bloom};
                double out[3] = {-1.0, -1.0, -1.0};
                PostCompositeMirror(scene, bl, *c, out);
                CHECK(607, in01(out[0]) && in01(out[1]) && in01(out[2]));
                CHECK(607, PostTableTexel(float(PostCompositeCoordinate(scene, *c))) < capture::kPostTableTexels);
                ++checked;
              }
            }
          }
        }
      }
      CHECK(607, checked == 4 * 16 * 16 * 6 * 7);
      // A scene that is no number is the black scene's pixel, an infinity the clamp's: the rule, channel by channel.
      {
        const float none[3] = {0.0f, 0.0f, 0.0f};
        const float nan_scene[3] = {kNaN, 0.5f, 0.25f}, zero_scene[3] = {0.0f, 0.5f, 0.25f}, neg_scene[3] = {-3.0f, 0.5f, 0.25f};
        const float inf_scene[3] = {kInf, 0.5f, 0.25f}, max_scene[3] = {31.875f, 0.5f, 0.25f}, over_scene[3] = {1e30f, 0.5f, 0.25f};
        double a[3], b[3], c[3];
        PostCompositeMirror(nan_scene, none, real_k, a);
        PostCompositeMirror(zero_scene, none, real_k, b);
        PostCompositeMirror(neg_scene, none, real_k, c);
        CHECK(607, std::memcmp(a, b, sizeof(a)) == 0 && std::memcmp(c, b, sizeof(c)) == 0);
        PostCompositeMirror(inf_scene, none, real_k, a);
        PostCompositeMirror(max_scene, none, real_k, b);
        PostCompositeMirror(over_scene, none, real_k, c);
        CHECK(607, std::memcmp(a, b, sizeof(a)) == 0 && std::memcmp(c, b, sizeof(c)) == 0);
        CHECK(607, PostScene(kNaN) == 0.0 && PostScene(-kInf) == 0.0 && PostScene(kInf) == 31.875 && PostScene(2.5f) == 2.5 &&
                       PostScene(-0.0f) == 0.0);
        // A register or a texel that is no number (which MakeClayPostConstants refuses; set by hand): black.
        const float grey[3] = {0.6f, 0.55f, 0.4f};
        for (int i = 0; i < 6; ++i) {
          for (float value : {kNaN, kInf, -kInf}) {
            ClayPostConstants broken = real_k;
            broken.composite[i] = value;
            PostCompositeMirror(grey, none, broken, a);
            CHECK(607, a[0] == 0.0 && a[1] == 0.0 && a[2] == 0.0);
          }
        }
        ClayPostConstants broken = real_k;
        for (auto& row : broken.table) {
          for (float& t : row) t = kNaN;
        }
        PostCompositeMirror(grey, none, broken, a);
        CHECK(607, a[0] == 0.0 && a[1] == 0.0 && a[2] == 0.0);
        // The clear colour of a post frame. Its scene luminance is 0.0064: above 1 / 256, so under the
        // nearest-texel rule it reads the table's texel 1, not texel 0 (the floor would read 0, and spec
        // Design 6 says black): it comes out nearly black, not black. Stated in frame-map section 17.
        PostCompositeMirror(kClayClearLinear, none, real_k, a);
        const double clear_L = kClayClearLinear[0] * 0.2125 + kClayClearLinear[1] * 0.7154 + kClayClearLinear[2] * 0.0721;
        std::printf("post: the clear colour through the real set's composite: scene luminance %.5f, texel %u, front (%.4f, %.4f, %.4f)\n",
                    clear_L, PostTableTexel(float(PostCompositeCoordinate(kClayClearLinear, real_k))), a[0], a[1], a[2]);
        CHECK(607, clear_L > 1.0 / 256.0 && clear_L < 1.0 / 128.0 && PostTableTexel(float(PostCompositeCoordinate(kClayClearLinear, real_k))) == 1);
        CHECK(607, a[0] > 0.0 && a[0] < 0.06 && a[2] > a[0] && a[2] < 0.06);
        // The unlit scale U is the reciprocal of the composite's gain at scene luminance 1 (texel 128 with
        // c46.x = 0.25). An unlit grey v is written as v * U; the one that lands on scene luminance 1
        // (v = 1 / U) comes out as (gain * 1)^c78.x = v^c78.x, which is what a flat table gives every unlit
        // value (the blind run). An unlit white lands at scene luminance U, far up the table: with this
        // rising table (a read-back run's) it is at the front target's ceiling.
        const float U = PostUnlitScale(real_block);
        const float one[3] = {1.0f, 1.0f, 1.0f};
        PostCompositeMirror(one, none, real_k, a);
        CHECK(607, PostTableTexel(float(PostCompositeCoordinate(one, real_k))) == kPostUnlitTexel &&
                       std::fabs(a[0] - std::pow(1.0 / double(U), double(real_composite[5]))) < 1e-6 && a[0] > 0.5 && a[0] < 0.6);
        const float white[3] = {U, U, U};
        PostCompositeMirror(white, none, real_k, a);
        std::printf("post: an unlit white through the real set's composite: %.4f (scene %.4f, texel %u)\n", a[0], double(U),
                    PostTableTexel(float(PostCompositeCoordinate(white, real_k))));
        CHECK(607, a[0] > 0.9 && a[0] <= 1.0);
      }
    }

    // 608: the frame's two words (cbuffer Frame's f_exposure and f_post). Without post: the exposure the
    // calibrated constant in a lit frame and the unlit word 1 in EVERY frame, a frame without light
    // included (never 0: every unlit colour is multiplied by it). In a post frame: the game's scale c20.w
    // and the block's unlit scale. Never the game's scale without the block's pairing, never 2.22 with it.
    {
      capture::LightBlock light{};
      for (int i = 0; i < 3; ++i) {
        light.sun_dir[i] = 0.5f;
        light.sun_col[i] = 0.8f;
        light.tint[i] = 1.0f;
        light.axis[i] = 0.3f;
        light.eye[i] = 10.0f;
      }
      light.tint_pow = 1.0f;
      light.out_scale = 8.4413f;
      light.valid = true;
      static_assert(offsetof(ClayFrameConstants, post) == 60 && offsetof(ClayFrameConstants, exposure) == 44);
      const ClayFrameConstants lit = MakeClayFrameConstants(light, ClayLight::kOn);
      const ClayFrameConstants unlit = MakeClayFrameConstants(light, ClayLight::kOff);
      const ClayFrameConstants fresh{};
      CHECK(608, lit.light_on == 1 && lit.exposure == kLitExposure && lit.post == 1.0f);
      CHECK(608, unlit.light_on == 0 && unlit.exposure == 0.0f && unlit.post == 1.0f && fresh.post == 1.0f && fresh.exposure == 0.0f);
      // A post frame: the block pairs with the frame's scale (the bright pass's c77.y is 1 / c20.w).
      capture::PostBlock paired = real_block;
      paired.bright[4] = 1.0f / light.out_scale;
      ClayFrameConstants frame = MakeClayFrameConstants(light, ClayLight::kOn);
      CHECK(608, SetClayFramePost(frame, paired, light.out_scale) && frame.exposure == 8.4413f && frame.post == PostUnlitScale(paired) &&
                     frame.post > 3.7f && frame.post < 3.8f);
      // (nothing else of the frame's block changed)
      ClayFrameConstants same = frame;
      same.exposure = lit.exposure;
      same.post = lit.post;
      CHECK(608, std::memcmp(&same, &lit, sizeof(same)) == 0);
      // Refused, with the two words left as a frame without post has them: a frame without light, an
      // invalid block, a scale the block does not pair with, a scale that is no number or not usable.
      auto untouched = [&](ClayFrameConstants f, const capture::PostBlock& b, float scale) {
        const ClayFrameConstants before = f;
        return !SetClayFramePost(f, b, scale) && std::memcmp(&f, &before, sizeof(f)) == 0;
      };
      capture::PostBlock invalid = paired;
      invalid.valid = false;
      CHECK(608, untouched(unlit, paired, light.out_scale) && untouched(lit, invalid, light.out_scale) &&
                     untouched(lit, paired, 16.0f) && untouched(lit, paired, kNaN) && untouched(lit, paired, kInf) &&
                     untouched(lit, paired, 0.0f) && untouched(lit, paired, -8.4413f));
      capture::PostBlock huge = paired;
      huge.bright[4] = 1.0f / 2048.0f;  // (pairs with a scale beyond what a light value may be)
      CHECK(608, untouched(lit, huge, 2048.0f));
    }

    // 609: the Post line.
    {
      ClayStats st;
      CHECK(609, FormatPostText(st) == "Post: off");
      st.post_switch = true;
      st.post = ClayPost::kDrawn;
      st.post_block = true;
      st.post_age = 2;
      st.post_scale = 8.4413f;
      st.post_unlit = 3.7369f;
      st.post_frames = 900;
      st.post_drawn_frames = 880;
      st.post_block_frames = 7;
      st.post_ramp_frames = 6;
      st.post_objects_frames = 5;
      st.post_nolight_frames = 2;
      st.post_buffer_frames = 0;
      std::string t = FormatPostText(st);
      CHECK(609, t == "Post: on, block valid, age 2, drawn, bloom off | c20.w 8.4413, unlit scale 3.7369 | frames since start 900: "
                      "drawn 880 (bloom 0, no-weights 0), post-block 7, post-ramp 6, post-objects 5, no-light 2, no-buffer 0");
      // The bloom's verdict of the frame and its counts (plan Task 8).
      st.bloom = ClayBloom::kOn;
      st.bloom_frames = 870;
      st.bloom_noweights_frames = 4;
      t = FormatPostText(st);
      CHECK(609, t.find("age 2, drawn, bloom on | c20.w") != std::string::npos && t.find(": drawn 880 (bloom 870, no-weights 4), post-block 7,") != std::string::npos);
      st.bloom = ClayBloom::kNoWeights;
      CHECK(609, FormatPostText(st).find("age 2, drawn, bloom no-weights | c20.w") != std::string::npos);
      // The unlit scale is the composite's gain at scene luminance 1 only while c46.x is 0.25 (the table's
      // texel 128); with another c46.x the line says at which luminance it is (review of plan Task 5, Minor 8).
      CHECK(609, t.find("c46.x") == std::string::npos && st.post_coord == kPostUnlitCoord && kPostUnlitCoord == 0.25f);
      st.post_coord = 0.5f;
      CHECK(609, FormatPostText(st).find("unlit scale 3.7369 (c46.x 0.5000: the gain at scene luminance 0.500, not 1) | frames") != std::string::npos);
      st.post_coord = kPostUnlitCoord;
      st.post = ClayPost::kBlock;
      st.post_block = false;
      st.post_reason = capture::PostBlockReason::kNoDraw;
      t = FormatPostText(st);
      CHECK(609, t.find("Post: on, block invalid (post-block: no-draw), not drawn (post-block) | today's mapping, exposure 2.22 | frames") == 0);
      CHECK(609, t.find("bloom o") == std::string::npos && t.find("bloom no") == std::string::npos);  // (a frame that is not drawn has no bloom verdict)
      // A valid block the pass could not use: the line says which of the pass's rules refused it (review of
      // plan Task 5, Minor 11: a pairing fallback must be told from a refused value).
      st.post_block = true;
      st.post_fault = ClayPostFault::kValues;
      CHECK(609, FormatPostText(st).find("block valid, age 2, not drawn (post-block: values)") != std::string::npos);
      st.post_fault = ClayPostFault::kPairing;
      CHECK(609, FormatPostText(st).find("block valid, age 2, not drawn (post-block: pairing)") != std::string::npos);
      st.post_fault = ClayPostFault::kAge;
      CHECK(609, FormatPostText(st).find("block valid, age 2, not drawn (post-block: age)") != std::string::npos);
      st.post_fault = ClayPostFault::kWords;
      CHECK(609, FormatPostText(st).find("block valid, age 2, not drawn (post-block: words)") != std::string::npos);
      st.post = ClayPost::kRamp;
      CHECK(609, FormatPostText(st).find(", not drawn (post-ramp) |") != std::string::npos);
      st.post = ClayPost::kObjects;
      CHECK(609, FormatPostText(st).find(", not drawn (post-objects) |") != std::string::npos);
      st.post = ClayPost::kNoLight;
      CHECK(609, FormatPostText(st).find(", not drawn (no light) |") != std::string::npos);
      st.post = ClayPost::kBuffer;
      CHECK(609, FormatPostText(st).find(", not drawn (no constant buffer) |") != std::string::npos);
      // (the line with every count at its largest is whole)
      st.post_frames = st.post_drawn_frames = st.post_block_frames = st.post_ramp_frames = st.post_objects_frames = st.post_nolight_frames =
          st.post_buffer_frames = st.bloom_frames = st.bloom_noweights_frames = ~0ull;
      st.post_coord = -3.4e38f;
      st.post_age = ~0u;
      st.post = ClayPost::kDrawn;
      st.post_scale = st.post_unlit = -3.4e38f;
      t = FormatPostText(st);
      CHECK(609, t.size() > 100 && t.rfind("no-buffer 18446744073709551615") == t.size() - 30);
    }

    // 610-617: the native bloom (plan Task 8 of the post sub-project; clay_logic.h, "Native bloom"): the level
    // a UNORM target stores (controller ruling 25), the downsample's, the bright pass's and the blurs' mirrors
    // against the reference's numbers (post_pins.py; test_post_literals.py holds the lines), the place of the
    // composite's bloom sample, nothing that is no number (spec criterion 14), the steps' words, the verdict.
    {
      auto in_scene = [](double v) { return v >= 0.0 && v <= 31.875; };  // (false for a value that is no number)
      // The pins' pictures (post_pins.py: scene_texel, unorm_texel), made here by the same integer formulas.
      static float scene_px[16 * 8 * 3], unorm_px[16 * 16 * 3];
      for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 16; ++x) {
          for (int c = 0; c < 3; ++c) scene_px[(y * 16 + x) * 3 + c] = float((7 * x + 13 * y + 5 * c) % 32) / 8.0f;
        }
      }
      auto set_scene = [&](int x, int y, float r, float g, float b) {
        float* t = scene_px + (y * 16 + x) * 3;
        t[0] = r, t[1] = g, t[2] = b;
      };
      set_scene(0, 0, kNaN, 1.0f, 1.0f);
      set_scene(2, 0, kInf, 1.0f, 1.0f);
      set_scene(0, 2, -1.0f, 1.0f, 1.0f);
      set_scene(10, 6, 40.0f, -kInf, kNaN);
      for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
          for (int c = 0; c < 3; ++c) unorm_px[(y * 16 + x) * 3 + c] = float(double((37 * x + 101 * y + 59 * c) % 1024) / 1023.0);
        }
      }
      const PostPicture scene_pic{scene_px, 16, 8}, unorm_pic{unorm_px, 16, 16};
      const float real_blur[2][7] = {{0.20737524330615997f, 0.18119974434375763f, 0.12088140845298767f, 0.06156909465789795f, 0.023942377418279648f, 0.00710842153057456f, 0.0016113145975396037f},
                                     {0.1818615347146988f, 0.16401202976703644f, 0.12030407786369324f, 0.07177194207906723f, 0.03482561185956001f, 0.013743981719017029f, 0.004411600064486265f}};
      const float made_blur[2][7] = {{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
                                     {0.07692307978868484f, 0.07692307978868484f, 0.07692307978868484f, 0.07692307978868484f, 0.07692307978868484f, 0.07692307978868484f, 0.07692307978868484f}};
      // The two register sets' constants with their blurs' weights and the bloom asked for.
      auto bloom_constants = [&](capture::PostBlock b, const float blur[2][7]) {
        std::memcpy(b.blur, blur, sizeof(b.blur));
        b.parts = capture::kPostPartBlur;
        ClayPostConstants k;
        CHECK(611, MakeClayPostConstants(b, ramp, true, false, &k) && k.bloom_on == 1 && k.sun_on == 0);
        return k;
      };
      const ClayPostConstants real_k = bloom_constants(real_block, real_blur), made_k = bloom_constants(made_block, made_blur);

      // 610: the level a UNORM target stores, written by the shader itself (controller ruling 25: the
      // picture must not depend on a card's own rounding): floor(sat(v) * levels + 0.5) / levels, the
      // reference's rounding (light_formulas.post_ramp_index for 8 bits, post_replay.q10 for 10). Every
      // level is its own; and the stored value survives the conversion the real card was found to make
      // (review of plan Task 6: the float cut to 12 fractional bits, then the nearest level).
      {
        static_assert(kPostFrontLevels == 255 && kPostBloomLevels == 1023);
        for (uint32_t levels : {kPostFrontLevels, kPostBloomLevels}) {
          for (uint32_t n = 0; n <= levels; ++n) {
            const float v = float(n) / float(levels);
            CHECK(610, PostLevelIndex(v, levels) == n && PostLevel(v, levels) == v);
            const double cut = std::floor(double(PostLevel(v, levels)) * 4096.0) / 4096.0;
            CHECK(610, uint32_t(std::floor(cut * double(levels) + 0.5)) == n);
            if (n < levels) {
              CHECK(610, PostLevelIndex((float(n) + 0.49f) / float(levels), levels) == n);
              CHECK(610, PostLevelIndex((float(n) + 0.51f) / float(levels), levels) == n + 1);
            }
          }
          for (float v : {kNaN, -kInf, -1.0f, -0.0f, 0.0f, 1e-45f}) CHECK(610, PostLevelIndex(v, levels) == 0 && PostLevel(v, levels) == 0.0f);
          for (float v : {kInf, 3.4e38f, 1.0f, 1.5f}) CHECK(610, PostLevelIndex(v, levels) == levels && PostLevel(v, levels) == 1.0f);
        }
        // (8 bits: the ramp's entry of the value is the level it was stored as)
        for (float v : {0.0f, 0.001959784422069788f, 0.001961784204468131f, 0.39407843351364136f, 0.39415687322616577f, 0.5f, 0.9980401992797852f, kNaN, kInf}) {
          CHECK(610, PostLevelIndex(v, kPostFrontLevels) == PostRampIndex(v) && PostRampIndex(PostLevel(v, kPostFrontLevels)) == PostRampIndex(v));
        }
        // (no levels: a float target, which no step rounds; the mirror gives 0, never a division by 0)
        CHECK(610, PostLevelIndex(0.5f, 0) == 0 && PostLevel(0.5f, 0) == 0.0f);
      }

      // 611: the downsample against the reference (light_formulas.post_downsample), both conventions of the
      // pixel's position. The scene holds a NaN, an infinity, a negative texel and one above the clamp.
      {
        static const struct { uint32_t x, y; double bias; double out[3]; } kPins[] = {
          // downsample pins (scene_texel, 16 x 8, linear): the pixel, the bias, the value
          {0, 0, 0.0, {8.21875, 1.15625, 1.3125}},
          {1, 0, 0.0, {2.0, 1.625, 1.25}},
          {2, 1, 0.0, {9.34375, 1.84375, 1.3125}},
          {3, 1, 0.0, {1.5, 2.125, 1.75}},
          {0, 0, 0.5, {1.5, 2.125, 1.75}},
          {1, 0, 0.5, {2.0, 1.625, 2.25}},
          {2, 1, 0.5, {8.53125, 1.03125, 1.5}},
          {3, 1, 0.5, {2.25, 1.875, 1.5}},
        };
        CHECK(611, std::size(kPins) == 8);
        double worst = 0.0;
        for (const auto& p : kPins) {
          double out[3];
          PostDownsampleMirror(scene_pic, p.x, p.y, float(p.bias), kPostSceneMax, out);
          for (int i = 0; i < 3; ++i) {
            CHECK(611, in_scene(out[i]));
            worst = std::fmax(worst, std::fabs(out[i] - p.out[i]));
          }
        }
        CHECK(611, worst < 1e-12);
        // The convention the pass draws with: the pixel's integral position (the four texels (0, 0), (2, 0),
        // (0, 2), (2, 2) of the pixel's 4 x 4 block), and a quarter of the logical size.
        static_assert(kPostDownsampleBias == 0.0f && kPostBloomDivisor == 4);
        double out[3], by_hand[3] = {0.0, 0.0, 0.0};
        PostDownsampleMirror(scene_pic, 1, 1, kPostDownsampleBias, kPostSceneMax, out);
        for (int ty : {4, 6}) {
          for (int tx : {4, 6}) {
            for (int c = 0; c < 3; ++c) by_hand[c] += 0.25 * double(scene_px[(ty * 16 + tx) * 3 + c]);
          }
        }
        CHECK(611, out[0] == by_hand[0] && out[1] == by_hand[1] && out[2] == by_hand[2]);
        // A picture's texel and its linear read are clamped to the edge; a picture without texels reads 0.
        float t[3];
        double l[3];
        PostPictureTexel(unorm_pic, -5, 99, t);
        CHECK(611, t[0] == unorm_px[(15 * 16 + 0) * 3] && t[2] == unorm_px[(15 * 16 + 0) * 3 + 2]);
        PostPictureLinear(unorm_pic, 1e9, -1e9, l);
        CHECK(611, l[0] == double(unorm_px[15 * 3]) && l[1] == double(unorm_px[15 * 3 + 1]));
        PostPictureLinear(unorm_pic, double(kNaN), 3.5, l);
        CHECK(611, l[0] == double(unorm_px[(3 * 16 + 0) * 3]));
        PostPictureTexel(PostPicture{}, 0, 0, t);
        PostPictureLinear(PostPicture{}, 0.5, 0.5, l);
        CHECK(611, t[0] == 0.0f && t[1] == 0.0f && t[2] == 0.0f && l[0] == 0.0 && l[1] == 0.0 && l[2] == 0.0);
      }

      // 612: the bright pass against the reference (light_formulas.post_bright), both register sets, with
      // the sun's two layers as the reference takes them (the pins give them values: the sun's flag set by
      // hand; without it they are not read). Among the points: L = 0, the clamp, values that are no numbers.
      {
        struct BrightPin { float d[3], tf1[3], tf2[3]; double out[3]; };
        static const BrightPin kReal[] = {
          // real bright pins: d, tf1, tf2, the bright target's value
          {{2.0f, 1.5f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.09379039276015376, 0.07034279457011532, 0.04689519638007688}},
          {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0, 0.0, 0.0}},
          {{3.0f, 3.0f, 3.0f}, {0.10000000149011612f, 0.10000000149011612f, 0.10000000149011612f}, {0.20000000298023224f, 0.25f, 0.30000001192092896f}, {0.3595263576775233, 0.38452635618740716, 0.40952636214787164}},
          {{0.0f, 0.0f, 0.0f}, {0.10000000149011612f, 0.20000000298023224f, 0.30000001192092896f}, {0.05000000074505806f, 0.0f, 0.4000000059604645f}, {0.08500000216066839, 0.12000000357627871, 0.38000001281499873}},
          {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0, 0.0, 0.0}},
          {{31.875f, 31.875f, 31.875f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.7432486414909363, 0.7432486414909363, 0.7432486414909363}},
          {{kNaN, 1.0f, 1.0f}, {kInf, 0.0f, 0.0f}, {0.0f, -1.0f, kNaN}, {0.0, 0.0, 0.005992890613482019}},
          {{-2.0f, kInf, 0.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0, 1.0, 0.016271176736956737}},
        };
        static const BrightPin kMade[] = {
          // made bright pins: d, tf1, tf2, the bright target's value
          {{2.0f, 1.5f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.49936739290382626, 0.3745255446778697, 0.24968369645191313}},
          {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.1066666692495346, 0.1066666692495346, 0.1066666692495346}},
          {{3.0f, 3.0f, 3.0f}, {0.10000000149011612f, 0.10000000149011612f, 0.10000000149011612f}, {0.20000000298023224f, 0.25f, 0.30000001192092896f}, {1.0, 1.0, 1.0}},
          {{0.0f, 0.0f, 0.0f}, {0.10000000149011612f, 0.20000000298023224f, 0.30000001192092896f}, {0.05000000074505806f, 0.0f, 0.4000000059604645f}, {0.23333333681027094, 0.4000000059604645, 0.8666666944821676}},
          {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0, 0.0, 0.0}},
          {{31.875f, 31.875f, 31.875f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0, 1.0, 1.0}},
          {{kNaN, 1.0f, 1.0f}, {kInf, 0.0f, 0.0f}, {0.0f, -1.0f, kNaN}, {0.0, 0.0, 0.23280423753476018}},
          {{-2.0f, kInf, 0.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0, 1.0, 0.029189292929514058}},
        };
        CHECK(612, std::size(kReal) == 8 && std::size(kMade) == 8);
        double worst = 0.0;
        int below = 0;
        for (int set = 0; set < 2; ++set) {
          ClayPostConstants sunny = set ? made_k : real_k;
          sunny.sun_on = 1;
          const ClayPostConstants& plain = set ? made_k : real_k;
          for (const BrightPin& p : set ? kMade : kReal) {
            double out[3], dry[3], none[3];
            PostBrightMirror(p.d, p.tf1, p.tf2, sunny, out);
            for (int i = 0; i < 3; ++i) {
              CHECK(612, in01(out[i]));
              worst = std::fmax(worst, std::fabs(out[i] - p.out[i]));
            }
            // Without the sun's flag the two layers are not read: the result is that of two black layers.
            const float black[3] = {0.0f, 0.0f, 0.0f};
            PostBrightMirror(p.d, p.tf1, p.tf2, plain, dry);
            PostBrightMirror(p.d, black, black, sunny, none);
            CHECK(612, std::memcmp(dry, none, sizeof(dry)) == 0 && in01(dry[0]) && in01(dry[1]) && in01(dry[2]));
            below += set == 0 && p.d[0] == 0.5f && dry[0] == 0.0 && dry[1] == 0.0 && dry[2] == 0.0;
          }
        }
        std::printf("post: the bright pass's mirror against the reference, 16 points: worst difference %.2e\n", worst);
        // (the real set's point below the threshold is black: the bloom takes only what is above c77.x)
        CHECK(612, worst < 1e-12 && below == 1);
      }

      // 613: the two blurs against the reference (light_formulas.post_blur13): 13 point taps along x, along
      // y, clamped to the edge, the weights from the constants' four rows.
      {
        struct BlurPin { uint32_t x, y; double out[3]; };
        static const BlurPin kRealX[] = {
          // real blur pins along x (unorm_texel, 16 x 16, point), weights {0.20737524330615997f, 0.18119974434375763f, 0.12088140845298767f, 0.06156909465789795f, 0.023942377418279648f, 0.00710842153057456f, 0.0016113145975396037f}: the pixel, the value
          {0, 0, {0.027077248821215653, 0.08475075677923871, 0.1424242650386999}},
          {7, 7, {0.7289586054778714, 0.3976772754153809, 0.27397391791108006}},
          {3, 12, {0.29383258829226144, 0.3515060931646031, 0.4091796010198255}},
          {15, 15, {0.39112789151367205, 0.1464249554322099, 0.14246918446430037}},
        };
        static const BlurPin kRealY[] = {
          // real blur pins along y (unorm_texel, 16 x 16, point), weights {0.1818615347146988f, 0.16401202976703644f, 0.12030407786369324f, 0.07177194207906723f, 0.03482561185956001f, 0.013743981719017029f, 0.004411600064486265f}: the pixel, the value
          {0, 0, {0.0843570138782874, 0.14203052274906286, 0.19970403567237777}},
          {7, 7, {0.5348124321450349, 0.41044663055240405, 0.468120139676623}},
          {3, 12, {0.40969386942411135, 0.3955252788128213, 0.418339137573319}},
          {15, 15, {0.3466174807590675, 0.404290992581688, 0.2977921455835798}},
        };
        static const BlurPin kMadeX[] = {
          // made blur pins along x (unorm_texel, 16 x 16, point), weights {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}: the pixel, the value
          {0, 0, {0.0, 0.057673510164022446, 0.11534702032804489}},
          {7, 7, {0.9442815184593201, 0.0009775171056389809, 0.05865102633833885}},
          {3, 12, {0.29227760434150696, 0.3499511182308197, 0.40762463212013245}},
          {15, 15, {0.02150537632405758, 0.07917888462543488, 0.13685239851474762}},
        };
        static const BlurPin kMadeY[] = {
          // made blur pins along y (unorm_texel, 16 x 16, point), weights {0.07692307978868484f, 0.07692307978868484f, 0.07692307978868484f, 0.07692307978868484f, 0.07692307978868484f, 0.07692307978868484f, 0.07692307978868484f}: the pixel, the value
          {0, 0, {0.15948568018959625, 0.21715919020963795, 0.2748327042415309}},
          {7, 7, {0.4822919213135505, 0.4629671557693894, 0.5206406674371558}},
          {3, 12, {0.5547033732073341, 0.5353786130361882, 0.5160538549426079}},
          {15, 15, {0.3240093361360558, 0.38168284916498585, 0.36235808555511023}},
        };
        double worst = 0.0;
        auto run = [&](const BlurPin* pins, size_t count, const ClayPostConstants& k, bool vertical) {
          CHECK(613, count == 4);
          for (size_t n = 0; n < count; ++n) {
            double out[3];
            PostBlurMirror(unorm_pic, pins[n].x, pins[n].y, k, vertical, out);
            for (int i = 0; i < 3; ++i) {
              CHECK(613, in01(out[i]));
              worst = std::fmax(worst, std::fabs(out[i] - pins[n].out[i]));
            }
          }
        };
        run(kRealX, std::size(kRealX), real_k, false);
        run(kRealY, std::size(kRealY), real_k, true);
        run(kMadeX, std::size(kMadeX), made_k, false);
        run(kMadeY, std::size(kMadeY), made_k, true);
        std::printf("post: the blurs' mirror against the reference, 16 points: worst difference %.2e\n", worst);
        CHECK(613, worst < 1e-12);
        // The weights' places: EA83's seven in rows 0 and 1 (the centre first), 28DB's in rows 2 and 3.
        CHECK(613, real_k.blur[0][0] == real_blur[0][0] && real_k.blur[1][2] == real_blur[0][6] && real_k.blur[2][0] == real_blur[1][0] &&
                       real_k.blur[3][2] == real_blur[1][6] && real_k.blur[1][3] == 0.0f && real_k.blur[3][3] == 0.0f);
        // The centre alone (the made set along x) is the texel itself.
        double out[3];
        PostBlurMirror(unorm_pic, 5, 9, made_k, false, out);
        CHECK(613, out[0] == double(unorm_px[(9 * 16 + 5) * 3]) && out[2] == double(unorm_px[(9 * 16 + 5) * 3 + 2]));
      }

      // 614: where the composite samples the bloom: at the scene's own interpolated coordinate with the
      // fetch's half texel (the game's 37B1, instruction 6: tfetch2D tf2, OffsetX = OffsetY = 0.5), which at
      // pixel x of the scene is bloom texel coordinate x / 4 + 0.5, linear: pixel 0 reads texel 0 itself,
      // pixel 4 texel 1, pixel 2 the mean of the two; not the centres aligned ((x + 0.5) / 4).
      {
        static_assert(kPostBloomFetch == 0.5f);
        CHECK(614, PostBloomCoordinate(0) == 0.5 && PostBloomCoordinate(4) == 1.5 && PostBloomCoordinate(2) == 1.0 &&
                       PostBloomCoordinate(1119) == 280.25);
        double out[3];
        PostBloomFetchMirror(unorm_pic, 0, 0, out);
        CHECK(614, out[0] == double(unorm_px[0]) && out[1] == double(unorm_px[1]) && out[2] == double(unorm_px[2]));
        PostBloomFetchMirror(unorm_pic, 4, 8, out);
        CHECK(614, out[0] == double(unorm_px[(2 * 16 + 1) * 3]) && out[2] == double(unorm_px[(2 * 16 + 1) * 3 + 2]));
        PostBloomFetchMirror(unorm_pic, 2, 0, out);
        CHECK(614, std::fabs(out[0] - 0.5 * (double(unorm_px[0]) + double(unorm_px[3]))) < 1e-15);
        // (the last pixels read the last texel: clamped)
        PostBloomFetchMirror(unorm_pic, 63, 63, out);
        CHECK(614, out[0] == double(unorm_px[(15 * 16 + 15) * 3]));
        // The composite with the bloom it reads there: the screen blend lifts every channel, never above 1.
        const float grey[3] = {0.6f, 0.55f, 0.4f}, none[3] = {0.0f, 0.0f, 0.0f};
        const float veil[3] = {float(out[0]), float(out[1]), float(out[2])};
        double with[3], without[3];
        PostCompositeMirror(grey, veil, real_k, with);
        PostCompositeMirror(grey, none, real_k, without);
        CHECK(614, with[0] > without[0] && with[1] > without[1] && with[2] > without[2] && in01(with[0]) && in01(with[1]) && in01(with[2]));
      }

      // 615: nothing that is no number (spec criterion 14; review focus 2): a scene texel of NaN, of
      // infinity, a negative one, L = 0, weights at their bounds, registers at theirs. Every output of every
      // step's mirror is a number in its target's range, and the level stored for it is one of the target's.
      {
        const float kValues[] = {kNaN, kInf, -kInf, -1.0f, -0.0f, 0.0f, 1e-45f, 1e-6f, 0.5f, 1.0f, 3.9f, 31.875f, 32.0f, 3.4e38f};
        long checked = 0;
        // The downsample: a scene of one value, and that value among ordinary texels.
        static float flat[16 * 8 * 3];
        for (float v : kValues) {
          for (int mixed = 0; mixed < 2; ++mixed) {
            for (size_t i = 0; i < std::size(flat); ++i) flat[i] = (mixed && (i % 5) != 0) ? 0.25f * float(i % 7) : v;
            for (float bias : {0.0f, 0.5f}) {
              for (uint32_t x = 0; x < 4; ++x) {
                for (uint32_t y = 0; y < 2; ++y) {
                  double out[3] = {-1.0, -1.0, -1.0};
                  PostDownsampleMirror(PostPicture{flat, 16, 8}, x, y, bias, kPostSceneMax, out);
                  CHECK(615, in_scene(out[0]) && in_scene(out[1]) && in_scene(out[2]));
                  ++checked;
                }
              }
            }
          }
        }
        double out[3];
        for (size_t i = 0; i < std::size(flat); ++i) flat[i] = kNaN;
        PostDownsampleMirror(PostPicture{flat, 16, 8}, 1, 1, 0.0f, kPostSceneMax, out);
        CHECK(615, out[0] == 0.0 && out[1] == 0.0 && out[2] == 0.0);
        for (size_t i = 0; i < std::size(flat); ++i) flat[i] = kInf;
        PostDownsampleMirror(PostPicture{flat, 16, 8}, 1, 1, 0.0f, kPostSceneMax, out);
        CHECK(615, out[0] == 31.875 && out[1] == 31.875 && out[2] == 31.875);
        // (a pixel outside the target and a position that is no number read the edge: numbers)
        PostDownsampleMirror(scene_pic, 4000000000u, 4000000000u, kNaN, kPostSceneMax, out);
        CHECK(615, in_scene(out[0]) && in_scene(out[1]) && in_scene(out[2]));
        // The bright pass: every value in every channel, the layers that are no numbers, both flags, the
        // two sets and registers at the bounds MakeClayPostConstants allows (the divisor c47.x at its
        // smallest, 1e-3: everything above the threshold is at the target's ceiling).
        ClayPostConstants edge_k = real_k;
        const float edge_bright[5] = {64.0f, 64.0f, 1e-3f, -4.0f, 64.0f};
        std::memcpy(edge_k.bright, edge_bright, sizeof(edge_bright));
        ClayPostConstants low_k = real_k;
        const float low_bright[5] = {0.0f, 0.0f, 64.0f, 4.0f, std::nextafter(0.0f, 1.0f)};
        std::memcpy(low_k.bright, low_bright, sizeof(low_bright));
        for (ClayPostConstants k : {real_k, made_k, edge_k, low_k}) {
          for (uint32_t sun = 0; sun < 2; ++sun) {
            k.sun_on = sun;
            for (float r : kValues) {
              for (float g : kValues) {
                for (float layer : {kNaN, kInf, -kInf, -1.0f, 0.0f, 0.3f}) {
                  const float d[3] = {r, g, 0.25f}, tf1[3] = {layer, 0.1f, layer}, tf2[3] = {0.2f, layer, layer};
                  out[0] = out[1] = out[2] = -1.0;
                  PostBrightMirror(d, tf1, tf2, k, out);
                  CHECK(615, in01(out[0]) && in01(out[1]) && in01(out[2]));
                  for (int i = 0; i < 3; ++i) CHECK(615, PostLevelIndex(float(out[i]), kPostBloomLevels) <= kPostBloomLevels);
                  ++checked;
                }
              }
            }
          }
        }
        // L = 0 (a black texel; the microcode multiplies 0 by 1 / 0): the scene term is 0, exactly; so is
        // that of a texel that is no number and of a negative one.
        const float black[3] = {0.0f, 0.0f, 0.0f}, some[3] = {0.1f, 0.2f, 0.3f};
        const float nan_d[3] = {kNaN, kNaN, kNaN}, neg_d[3] = {-1.0f, -kInf, -0.0f};
        for (const float* d : {black, nan_d, neg_d}) {
          PostBrightMirror(d, some, some, real_k, out);
          CHECK(615, out[0] == 0.0 && out[1] == 0.0 && out[2] == 0.0);
          PostBrightMirror(d, some, some, edge_k, out);
          CHECK(615, out[0] == 0.0 && out[1] == 0.0 && out[2] == 0.0);
        }
        // A register that is no number, or a divisor not above 0 (which MakeClayPostConstants refuses; set
        // by hand): black.
        const float bright_d[3] = {3.0f, 3.0f, 3.0f};
        for (int i = 0; i < 5; ++i) {
          for (float value : {kNaN, kInf, -kInf}) {
            ClayPostConstants broken = real_k;
            broken.bright[i] = value;
            PostBrightMirror(bright_d, some, some, broken, out);
            CHECK(615, out[0] == 0.0 && out[1] == 0.0 && out[2] == 0.0);
          }
        }
        for (float divisor : {0.0f, -2.0f}) {
          ClayPostConstants broken = real_k;
          broken.bright[2] = divisor;
          PostBrightMirror(bright_d, some, some, broken, out);
          CHECK(615, out[0] == 0.0 && out[1] == 0.0 && out[2] == 0.0);
        }
        // The blurs: a source of one value (a 10-bit target holds only numbers in [0, 1]; the mirror takes
        // anything), weights all at their upper bound, all at their lower, and weights that are no numbers.
        ClayPostConstants heavy_k = real_k, none_k = real_k, nan_k = real_k;
        for (int r = 0; r < 4; ++r) {
          for (int c = 0; c < 4; ++c) {
            heavy_k.blur[r][c] = 1.0f;
            none_k.blur[r][c] = 0.0f;
            nan_k.blur[r][c] = (r + c) % 2 ? kNaN : kInf;
          }
        }
        static float source[16 * 16 * 3];
        for (float v : {kNaN, kInf, -kInf, -1.0f, 0.0f, 0.5f, 1.0f, 2.0f}) {
          for (size_t i = 0; i < std::size(source); ++i) source[i] = (i % 3) == 1 ? 0.25f : v;
          const ClayPostConstants* const sets[] = {&real_k, &made_k, &heavy_k, &none_k, &nan_k};
          for (const ClayPostConstants* k : sets) {
            for (int vertical = 0; vertical < 2; ++vertical) {
              for (uint32_t at : {0u, 7u, 15u, 4000000000u}) {
                out[0] = out[1] = out[2] = -1.0;
                PostBlurMirror(PostPicture{source, 16, 16}, at, 15 - (at & 15), *k, vertical != 0, out);
                CHECK(615, in01(out[0]) && in01(out[1]) && in01(out[2]));
                ++checked;
              }
            }
          }
        }
        for (size_t i = 0; i < std::size(source); ++i) source[i] = 0.5f;
        PostBlurMirror(PostPicture{source, 16, 16}, 7, 7, heavy_k, false, out);
        CHECK(615, out[0] == 1.0 && out[1] == 1.0 && out[2] == 1.0);  // (13 taps of 0.5 at weight 1: the target's ceiling)
        PostBlurMirror(PostPicture{source, 16, 16}, 7, 7, none_k, true, out);
        CHECK(615, out[0] == 0.0 && out[1] == 0.0 && out[2] == 0.0);
        PostBlurMirror(PostPicture{source, 16, 16}, 7, 7, nan_k, true, out);
        CHECK(615, out[0] == 0.0 && out[1] == 0.0 && out[2] == 0.0);
        // (a weight that is no number counts as 0, an infinite one too: not the target's ceiling)
        ClayPostConstants inf_k = real_k;
        for (auto& row : inf_k.blur) {
          for (float& w : row) w = kInf;
        }
        PostBlurMirror(PostPicture{source, 16, 16}, 7, 7, inf_k, false, out);
        CHECK(615, out[0] == 0.0 && out[1] == 0.0 && out[2] == 0.0);
        CHECK(615, checked == 14 * 2 * 2 * 4 * 2 + 4 * 2 * 14 * 14 * 6 + 8 * 5 * 2 * 4);
      }

      // 616: a step's own words (cbuffer Step, b1 of the post layout): the reciprocal of its source's size
      // (texel sizes are constants of the step, never literals of a shader: review focus 6), the level
      // count of its target's format, the step's offset (the downsample's pixel convention; the composite's
      // bloom fetch, in pixels), a blur's direction and its source's last texel.
      {
        static_assert(sizeof(ClayPostStepConstants) == 32 && offsetof(ClayPostStepConstants, texel) == 0 &&
                      offsetof(ClayPostStepConstants, levels) == 8 && offsetof(ClayPostStepConstants, offset) == 12 &&
                      offsetof(ClayPostStepConstants, dir) == 16 && offsetof(ClayPostStepConstants, last) == 24);
        const ClayPostStepConstants down = MakeClayPostStep(ClayPostStep::kDownsample, 1120, 720, 0);
        CHECK(616, down.texel[0] == 1.0f / 1120.0f && down.texel[1] == 1.0f / 720.0f && down.levels == 0.0f && down.offset == kPostDownsampleBias &&
                       down.dir[0] == 0 && down.dir[1] == 0 && down.last[0] == 1119 && down.last[1] == 719);
        const ClayPostStepConstants bright = MakeClayPostStep(ClayPostStep::kBright, 280, 180, kPostBloomLevels);
        CHECK(616, bright.texel[0] == 1.0f / 280.0f && bright.levels == 1023.0f && bright.offset == 0.0f && bright.dir[0] == 0 && bright.dir[1] == 0);
        const ClayPostStepConstants bx = MakeClayPostStep(ClayPostStep::kBlurX, 280, 180, kPostBloomLevels);
        const ClayPostStepConstants by = MakeClayPostStep(ClayPostStep::kBlurY, 280, 180, kPostBloomLevels);
        CHECK(616, bx.dir[0] == 1 && bx.dir[1] == 0 && by.dir[0] == 0 && by.dir[1] == 1 && bx.last[0] == 279 && bx.last[1] == 179 &&
                       by.last[0] == 279 && by.last[1] == 179 && bx.levels == 1023.0f && by.levels == 1023.0f && bx.offset == 0.0f);
        const ClayPostStepConstants comp = MakeClayPostStep(ClayPostStep::kComposite, 1120, 720, kPostFrontLevels);
        // (the bloom's fetch: half a bloom texel is two pixels of the scene)
        CHECK(616, comp.texel[0] == 1.0f / 1120.0f && comp.texel[1] == 1.0f / 720.0f && comp.levels == 255.0f && comp.offset == 2.0f &&
                       comp.offset == kPostBloomFetch * float(kPostBloomDivisor) && comp.dir[0] == 0 && comp.dir[1] == 0);
        // (the coordinate the shader makes of them is the mirror's: (x + offset) * texel in the scene's
        // normalised coordinate is x / 4 + 0.5 in bloom texels)
        for (uint32_t x : {0u, 1u, 2u, 4u, 559u, 1119u}) {
          CHECK(616, std::fabs((double(x) + double(comp.offset)) / 1120.0 * 280.0 - PostBloomCoordinate(x)) < 1e-9);
        }
        const ClayPostStepConstants ramp_step = MakeClayPostStep(ClayPostStep::kRamp, 1120, 720, kPostFrontLevels);
        CHECK(616, ramp_step.levels == 255.0f && ramp_step.offset == 0.0f && ramp_step.dir[0] == 0);
        // A size of 0 gives no infinity and no negative texel.
        const ClayPostStepConstants none = MakeClayPostStep(ClayPostStep::kBlurX, 0, 0, kPostBloomLevels);
        CHECK(616, none.texel[0] == 1.0f && none.texel[1] == 1.0f && none.last[0] == 0 && none.last[1] == 0);
      }

      // 617: the bloom's verdict of a frame (ClayBloom; the Post line counts it): on only in a post frame
      // with the switch on and the blurs' weights in the steps' constants; a post frame with the switch on
      // and no weights is drawn without bloom and counted (no-weights); everything else is off.
      {
        for (int bits = 0; bits < 8; ++bits) {
          const bool post_frame = bits & 1, wanted = bits & 2, weights = bits & 4;
          const ClayBloom want = !post_frame || !wanted ? ClayBloom::kOff : weights ? ClayBloom::kOn : ClayBloom::kNoWeights;
          CHECK(617, DecideClayBloom(post_frame, wanted, weights) == want);
        }
        static_assert(DecideClayBloom(true, true, true) == ClayBloom::kOn && DecideClayBloom(false, true, true) == ClayBloom::kOff &&
                      DecideClayBloom(true, false, true) == ClayBloom::kOff && DecideClayBloom(true, true, false) == ClayBloom::kNoWeights);
        CHECK(617, std::string(ClayBloomName(ClayBloom::kOn)) == "on" && std::string(ClayBloomName(ClayBloom::kOff)) == "off" &&
                       std::string(ClayBloomName(ClayBloom::kNoWeights)) == "no-weights" && std::string(ClayBloomName(ClayBloom::kCount)) == "off");
        ClayPostFrames n;
        for (ClayBloom b : {ClayBloom::kOff, ClayBloom::kOn, ClayBloom::kOn, ClayBloom::kNoWeights, ClayBloom::kCount}) CountClayBloomFrame(b, n);
        CHECK(617, n.bloom == 2 && n.noweights == 1 && n.frames == 0 && n.drawn == 0);
        // The constants' flag is the verdict's `weights`: set only with the switch and the block's part.
        capture::PostBlock with_weights = real_block;
        std::memcpy(with_weights.blur, real_blur, sizeof(with_weights.blur));
        with_weights.parts = capture::kPostPartBlur;
        ClayPostConstants k;
        CHECK(617, MakeClayPostConstants(with_weights, ramp, true, false, &k) && k.bloom_on == 1 &&
                       DecideClayBloom(true, true, k.bloom_on != 0) == ClayBloom::kOn);
        CHECK(617, MakeClayPostConstants(real_block, ramp, true, false, &k) && k.bloom_on == 0 &&
                       DecideClayBloom(true, true, k.bloom_on != 0) == ClayBloom::kNoWeights);
        CHECK(617, MakeClayPostConstants(with_weights, ramp, false, false, &k) && k.bloom_on == 0 &&
                       DecideClayBloom(true, false, k.bloom_on != 0) == ClayBloom::kOff);
        // Why the pass cannot use a scene's block (the Post line's word; review of plan Task 5, Minor 11).
        capture::PostBlock paired = real_block;
        paired.bright[4] = 1.0f / 8.4413f;
        capture::PostBlock invalid = paired;
        invalid.valid = false;
        CHECK(617, PostBlockFault(paired, 0, true, 8.4413f) == ClayPostFault::kNone &&
                       PostBlockFault(paired, capture::kPostBlockMaxAge, true, 8.4413f) == ClayPostFault::kNone &&
                       PostBlockFault(invalid, 0, true, 8.4413f) == ClayPostFault::kMissing &&
                       PostBlockFault(paired, capture::kPostBlockMaxAge + 1, true, 8.4413f) == ClayPostFault::kAge &&
                       PostBlockFault(paired, 0, false, 8.4413f) == ClayPostFault::kPairing &&
                       PostBlockFault(paired, 0, true, 16.0f) == ClayPostFault::kPairing &&
                       PostBlockFault(paired, 0, true, kNaN) == ClayPostFault::kPairing &&
                       PostBlockFault(invalid, 99, false, kNaN) == ClayPostFault::kMissing &&
                       PostBlockFault(paired, 99, false, 16.0f) == ClayPostFault::kAge);
        CHECK(617, std::string(ClayPostFaultName(ClayPostFault::kAge)) == "age" && std::string(ClayPostFaultName(ClayPostFault::kPairing)) == "pairing" &&
                       std::string(ClayPostFaultName(ClayPostFault::kValues)) == "values" && std::string(ClayPostFaultName(ClayPostFault::kWords)) == "words" &&
                       std::string(ClayPostFaultName(ClayPostFault::kMissing)) == "none" && std::string(ClayPostFaultName(ClayPostFault::kNone)) == "none");
      }
    }
  }
  // 630 to 639: the scene's furniture (sub-project 10, plan Task 13; clay_logic.h, "Native furniture").
  {
    using capture::PostQuadKind;
    using capture::QuadRecord;
    using capture::QuadSetReason;
    const float kNaN = std::numeric_limits<float>::quiet_NaN(), kInf = std::numeric_limits<float>::infinity();
    auto bits = [](uint32_t b) {
      float f;
      std::memcpy(&f, &b, sizeof(f));
      return f;
    };
    // Frame 1 of capture native_discovery_20261010_052158 (towards the sun): the matrix of every quad of the
    // frame (vertex c0..c3) and three records, made by the capture's own function from the rows' values:
    // the moon (n 35), the sun's glare (n 43), a cloud (n 36: a kind this build does not draw).
    const uint32_t matrix_bits[16] = {0x3EBA04AA, 0x3F684A6D, 0x33BBAA18, 0xC3306FC9, 0x3F3E8525, 0xBE989184, 0x3FC5551A, 0xC2B9462A,
                                      0xBF52DBA3, 0x3EA8DAD8, 0x3EEC3766, 0x3DCDF642, 0xBF52DA8E, 0x3EA8D9FA, 0x3EEC3630, 0x3E4D6100};
    float matrix[4][4];
    for (int i = 0; i < 16; ++i) matrix[i / 4][i % 4] = bits(matrix_bits[i]);
    auto record = [&](uint64_t ps, const char* verts, std::initializer_list<std::initializer_list<uint32_t>> registers, uint32_t blend,
                      uint32_t depth, uint32_t control, uint32_t ref) {
      uint8_t vertices[80];
      auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
      for (int i = 0; i < 80; ++i) vertices[i] = uint8_t(digit(verts[2 * i]) * 16 + digit(verts[2 * i + 1]));
      const uint16_t indices[6] = {0, 1, 2, 1, 3, 2};
      float ps_regs[12][4] = {};
      size_t n = 0;
      for (const auto& r : registers) {
        int k = 0;
        for (uint32_t b : r) ps_regs[n][k++] = bits(b);
        ++n;
      }
      const uint32_t fetch[3][6] = {{0x82000002, 0x14E60054, 0x001FE0FF, 0x00A80D10, 0x00000103, 0x14E70A18}};
      capture::QuadInputs in{};
      in.vertices = vertices;
      in.vertex_bytes = sizeof(vertices);
      in.stride = 20;
      in.indices = indices;
      in.index_count = 6;
      in.view_proj = matrix;
      in.ps = ps_regs;
      in.ps_count = n;
      in.fetch = fetch;
      in.fetch_count = 1;
      in.blend = blend;
      in.depth = depth;
      in.colour_mask = 0xF;
      in.colour_control = control;
      in.alpha_ref = ref;
      in.vport_z_scale = 0xBF800000;
      in.vte_cntl = 0x43F;
      const capture::PostQuadEntry* entry = capture::FindPostQuad(ps);
      return entry ? capture::MakeQuadRecord(*entry, in, nullptr) : QuadRecord{};
    };
    const QuadRecord moon = record(0x18D39DCEBEE4C2FCull,
                                   "C51FC8D6C3BF64BAC55467C13F0000003F800000C511D86A432E38E9C55467C13F2000003F800000"
                                   "C50FB936C3F2D716C53322773F00000000000000C501C8CA428EA862C53322773F20000000000000",
                                   {{0x3BC20A12, 0x3D8FC764, 0x3DBABD2A, 0x40F5882E}, {0x3EBD3D25, 0x3EBD3D25, 0x3EBD3D25, 0x00000000}},
                                   0x07060706, 0x00708762, 0xC, 0x3B808081);
    const QuadRecord glare = record(0x04985A0F7296E130ull,
                                    "C5143602C3D96C434381B5C0000000003F800000C4CB7E8C44B1BB3F4381B5C83F8000003F800000"
                                    "C4BD592CC44274BA44FE3B5E0000000000000000C440D7674486DBF344FE3B603F80000000000000",
                                    {{0x3E7AC1BC, 0x3E45B561, 0x3DC3477C, 0x00000000}}, 0x01010101, 0x00708760, 0x4, 0x3F008081);
    const QuadRecord cloud = record(0x61976295517D7015ull,
                                    "C53B8001C53B800143FA00000000000000000000453B8001C53B800143FA00003F80000000000000"
                                    "C53B8001453B800143FA0000000000003F800000453B8001453B800143FA00003F8000003F800000",
                                    {{0, 0, 0, 0x40F5882E}, {0x40A00000, 0x40A00000, 0, 0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}},
                                    0x07060706, 0x00708766, 0xC, 0x3CA0A0A1);
    const float scale = bits(0x40F5882E);  // the frame's c20.w, 7.6728735
    const float unit[2] = {1.0f, 1.0f};
    CHECK(630, capture::QuadRecordValid(moon) && capture::QuadRecordValid(glare) && capture::QuadRecordValid(cloud));

    // 630: where a kind is drawn by this build, and the state the pass draws a record with.
    {
      CHECK(630, ClayQuadPlaceOf(PostQuadKind::kMoon) == ClayQuadPlace::kAfterSky &&
                     ClayQuadPlaceOf(PostQuadKind::kGlare) == ClayQuadPlace::kAfterRecords);
      for (PostQuadKind k : {PostQuadKind::kCloud, PostQuadKind::kBeamSource, PostQuadKind::kFlare, PostQuadKind::kMoonGlow,
                             PostQuadKind::kSunDisc, PostQuadKind::kCloudOccluder, PostQuadKind::kCount}) {
        CHECK(630, ClayQuadPlaceOf(k) == ClayQuadPlace::kNotDrawn);
      }
      CHECK(630, QuadDrawState(moon) == ClayQuadState::kAlphaDepth && QuadDrawState(glare) == ClayQuadState::kAdd);
      // The moon's state, each word changed: a depth write, no depth test, another function (the game's
      // greater-or-equal alone is the pass's less-or-equal), another blend, a colour channel missing, an
      // alpha function that is not "greater".
      for (uint32_t depth : {0x00708766u, 0x00708760u, 0x00708752u, 0x00708772u, 0x00708722u, 0x00708742u}) {
        QuadRecord q = moon;
        q.depth = depth;
        CHECK(630, QuadDrawState(q) == ClayQuadState::kNone);
      }
      for (uint32_t blend : {0x01010101u, 0x00010001u, 0x07060707u, 0u}) {
        QuadRecord q = moon;
        q.blend = blend;
        CHECK(630, QuadDrawState(q) == ClayQuadState::kNone);
      }
      for (uint32_t control : {0x8u, 0x9u, 0xAu, 0xBu, 0xDu, 0xEu, 0xFu}) {
        QuadRecord q = moon, g = glare;
        q.alpha_control = g.alpha_control = control;
        CHECK(630, QuadDrawState(q) == ClayQuadState::kNone && QuadDrawState(g) == ClayQuadState::kNone);
      }
      {
        // (with the test off its function is nothing: any of the eight)
        QuadRecord g = glare;
        for (uint32_t func = 0; func < 8; ++func) {
          g.alpha_control = func;
          CHECK(630, QuadDrawState(g) == ClayQuadState::kAdd);
        }
        g = glare;
        g.colour_mask = 7;
        CHECK(630, QuadDrawState(g) == ClayQuadState::kNone);
        // The glare: additive with no depth test, whatever the depth word's other bits; with a test it is another draw.
        g = glare;
        g.depth = 0x00708764;
        CHECK(630, QuadDrawState(g) == ClayQuadState::kAdd);
        g.depth = 0x00708762;
        CHECK(630, QuadDrawState(g) == ClayQuadState::kNone);
        g = glare;
        g.blend = 0x07060706;
        CHECK(630, QuadDrawState(g) == ClayQuadState::kNone);
      }
      static_assert(QuadDrawState(QuadRecord{}) == ClayQuadState::kNone);
    }

    // 631: a record's constants, each where the shader's block has it.
    {
      static_assert(sizeof(ClayQuadConstants) == 416 && kClayQuadSlots == 2 && kClayQuadFrameSlots == 48 &&
                    kClayQuadSlots * kClayTerrainSlotBytes >= sizeof(ClayQuadConstants) &&
                    offsetof(ClayQuadConstants, coord) == 64 && offsetof(ClayQuadConstants, view_proj) == 128 &&
                    offsetof(ClayQuadConstants, reg) == 192 && offsetof(ClayQuadConstants, kind) == 384 &&
                    offsetof(ClayQuadConstants, alpha_ref) == 396 && offsetof(ClayQuadConstants, fix) == 400 &&
                    offsetof(ClayQuadConstants, scene_max) == 408);
      ClayQuadConstants c;
      const float fix[2] = {0.5f, 0.25f};
      CHECK(631, MakeClayQuadConstants(moon, scale, 3, fix, &c));
      for (int i = 0; i < 4; ++i) {
        CHECK(631, c.corner[i][0] == moon.corners[i][0] && c.corner[i][1] == moon.corners[i][1] && c.corner[i][2] == moon.corners[i][2] &&
                       c.corner[i][3] == 1.0f && c.coord[i][0] == moon.corners[i][3] && c.coord[i][1] == moon.corners[i][4] &&
                       c.coord[i][2] == 0.0f && c.coord[i][3] == 0.0f);
      }
      CHECK(631, std::memcmp(c.view_proj, matrix, sizeof(matrix)) == 0);
      // The moon's registers: c20.w alone of c20 (the scale), then c31.
      CHECK(631, c.reg[0][0] == 0.0f && c.reg[0][1] == 0.0f && c.reg[0][2] == 0.0f && c.reg[0][3] == scale &&
                     c.reg[1][0] == bits(0x3EBD3D25) && c.reg[1][3] == 0.0f && c.reg[2][0] == 0.0f);
      CHECK(631, c.kind == 1 && c.sampler == 3 && c.alpha_test == 1 && c.alpha_ref == bits(0x3B808081) && c.fix[0] == 0.5f &&
                     c.fix[1] == 0.25f && c.scene_max == 31.875f && c.pad == 0);
      CHECK(631, MakeClayQuadConstants(glare, scale, 6, unit, &c));
      // The glare: c31.rgb; no alpha test, and no reference (the record's is 0 with the test off).
      CHECK(631, c.kind == 3 && c.sampler == 2 && c.alpha_test == 0 && c.alpha_ref == 0.0f && c.reg[0][0] == bits(0x3E7AC1BC) &&
                     c.reg[0][1] == bits(0x3E45B561) && c.reg[0][2] == bits(0x3DC3477C) && c.reg[0][3] == 0.0f && c.reg[1][0] == 0.0f);
      // The glare does not ask for the frame's scale (its c31 carries the brightness).
      CHECK(631, MakeClayQuadConstants(glare, 0.0f, 0, unit, &c) && MakeClayQuadConstants(glare, kNaN, 0, unit, &c));
    }

    // 632: what gives no constants, each with `out` untouched.
    {
      auto refused = [&](const QuadRecord& q, float out_scale = 7.6728735f, const float* fix = nullptr) {
        ClayQuadConstants c;
        std::memset(&c, 0x5A, sizeof(c));
        ClayQuadConstants before = c;
        const float one[2] = {1.0f, 1.0f};
        return !MakeClayQuadConstants(q, out_scale, 0, fix ? fix : one, &c) && std::memcmp(&c, &before, sizeof(c)) == 0;
      };
      CHECK(632, refused(QuadRecord{}) && refused(cloud) && !MakeClayQuadConstants(moon, scale, 0, unit, nullptr) &&
                     !MakeClayQuadConstants(moon, scale, 0, nullptr, nullptr));
      {
        ClayQuadConstants c;
        CHECK(632, !MakeClayQuadConstants(moon, scale, 0, nullptr, &c));
      }
      // A kind this build does not draw gives none, also with a state the pass could draw (a flare as the
      // glare is drawn): its formula is not in the step.
      {
        QuadRecord flare = glare;
        flare.kind = PostQuadKind::kFlare;
        CHECK(632, QuadDrawState(flare) == ClayQuadState::kAdd && refused(flare));
        const QuadRecord set[2] = {glare, flare};
        const ClayQuadPlan plan = PlanClayQuads(set, 2, scale);
        CHECK(632, plan.drawable && plan.count == 1 && plan.later == 1);
      }
      // A state the pass has no pipeline for.
      {
        QuadRecord q = moon;
        q.depth = 0x00708766;
        CHECK(632, refused(q));
        q = glare;
        q.blend = 0x07060706;
        CHECK(632, refused(q));
      }
      // Every corner value and matrix component: no number, or beyond the bound.
      for (float v : {kNaN, kInf, -kInf, 2e7f, -2e7f}) {
        for (int i = 0; i < 4; ++i) {
          for (int k = 0; k < 5; ++k) {
            QuadRecord q = glare;
            q.corners[i][k] = v;
            CHECK(632, refused(q));
          }
          for (int k = 0; k < 4; ++k) {
            QuadRecord q = moon;
            q.view_proj[i][k] = v;
            CHECK(632, refused(q));
          }
        }
      }
      // The registers: a colour component in [0, 64]; the moon's scale in (0, 64].
      for (float v : {kNaN, kInf, -kInf, -1e-6f, std::nextafter(64.0f, 100.0f), 1e6f}) {
        for (int k = 0; k < 4; ++k) {
          QuadRecord q = moon;
          q.ps[1][k] = v;
          CHECK(632, refused(q));
        }
        for (int k = 0; k < 3; ++k) {
          QuadRecord q = glare;
          q.ps[0][k] = v;
          CHECK(632, refused(q));
        }
        QuadRecord q = moon;
        q.ps[0][3] = v;
        CHECK(632, refused(q, v));
      }
      {
        QuadRecord q = moon, g = glare;
        q.ps[1][0] = q.ps[1][3] = 64.0f;
        g.ps[0][2] = 64.0f;
        g.ps[0][3] = kNaN;  // (not read by the glare's shader, not judged, not handed over)
        q.ps[0][0] = kNaN;  // (c20.x: likewise for the moon)
        ClayQuadConstants c;
        CHECK(632, MakeClayQuadConstants(q, scale, 0, unit, &c) && c.reg[1][0] == 64.0f && c.reg[0][0] == 0.0f);
        CHECK(632, MakeClayQuadConstants(g, scale, 0, unit, &c) && c.reg[0][2] == 64.0f && c.reg[0][3] == 0.0f);
        q = moon;
        q.ps[0][3] = 0.0f;
        CHECK(632, refused(q, 0.0f));
      }
      // The moon's scale is the frame's: within a thousandth of it, both sides; another frame's is refused,
      // and so is a frame without a scale.
      {
        ClayQuadConstants c;
        CHECK(632, MakeClayQuadConstants(moon, scale * 1.0009f, 0, unit, &c) && MakeClayQuadConstants(moon, scale * 0.9991f, 0, unit, &c));
        CHECK(632, c.reg[0][3] == scale);  // (the record's own, the game's)
        CHECK(632, refused(moon, scale * 1.0011f) && refused(moon, scale * 0.9989f) && refused(moon, 16.0f) && refused(moon, 0.0f) &&
                       refused(moon, kNaN) && refused(moon, kInf) && refused(moon, -scale));
      }
      // An alpha reference in use, in [0, 1].
      for (float v : {kNaN, kInf, -0.01f, 1.01f}) {
        QuadRecord q = moon;
        q.alpha_ref = v;
        CHECK(632, refused(q));
      }
      // The texture's scale.
      for (float v : {kNaN, kInf, 0.0f, -1.0f, 65.0f}) {
        const float a[2] = {v, 1.0f}, b[2] = {1.0f, v};
        CHECK(632, refused(moon, scale, a) && refused(glare, scale, b));
      }
    }

    // 633: the mirrors against the reference's numbers (tools/xdk_sigmatch/post_pins.py).
    {
      struct MoonPin { float t[4]; float c31[4]; float c20w; double want[4]; };
      const MoonPin moon_pins[] = {
          // quad moon pins (light_formulas.post_moon): the texel, c31, c20.w, the value (rgb, alpha)
          {{0.5f, 0.25f, 0.125f, 0.75f}, {0.3696109354496002f, 0.3696109354496002f, 0.3696109354496002f, 0.0f}, 7.672873497009277f, {1.0634917315560166, 0.5317458657780083, 0.26587293288900415, 0.0}},
          {{0.5f, 0.25f, 0.125f, 0.75f}, {0.3696109354496002f, 0.3696109354496002f, 0.3696109354496002f, 1.0f}, 7.672873497009277f, {1.0634917315560166, 0.5317458657780083, 0.26587293288900415, 0.75}},
          {{1.0f, 0.800000011920929f, 0.6000000238418579f, 1.0f}, {0.8999999761581421f, 0.699999988079071f, 0.5f, 0.5f}, 16.0f, {14.399999618530273, 8.959999980926511, 4.800000190734863, 0.5}},
          {{1.0f, 1.0f, 1.0f, 1.0f}, {4.0f, 0.5f, 0.25f, 3.0f}, 16.0f, {31.875, 8.0, 4.0, 1.0}},
          {{0.20000000298023224f, 0.4000000059604645f, 0.6000000238418579f, 0.003921568859368563f}, {1.0f, 1.0f, 1.0f, 1.0f}, 8.449999809265137f, {0.00662745132149361, 0.01325490264298722, 0.019882354458264424, 0.003921568859368563}},
          {{kNaN, 0.5f, 0.5f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 8.0f, {0.0, 4.0, 4.0, 1.0}},
          {{0.5f, 0.5f, 0.5f, kNaN}, {1.0f, 1.0f, 1.0f, 1.0f}, 8.0f, {0.0, 0.0, 0.0, 0.0}},
          {{0.5f, kInf, -0.5f, 0.5f}, {1.0f, 1.0f, 1.0f, 1.0f}, 8.0f, {2.0, 0.0, 0.0, 0.5}},
          {{0.5f, 0.5f, 0.5f, 0.5f}, {1.0f, kNaN, 1.0f, 1.0f}, 8.0f, {0.0, 0.0, 0.0, 0.0}},
          {{0.5f, 0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f, 1.0f}, kInf, {0.0, 0.0, 0.0, 0.0}},
          {{0.5f, 0.5f, 0.5f, 0.5f}, {-1.0f, 1.0f, 1.0f, -1.0f}, 8.0f, {0.0, 2.0, 2.0, 0.0}},
      };
      double worst = 0.0;
      for (const MoonPin& p : moon_pins) {
        double out[4];
        QuadMoonMirror(p.t, p.c31, p.c20w, out);
        for (int i = 0; i < 4; ++i) {
          CHECK(633, std::isfinite(out[i]) && std::fabs(out[i] - p.want[i]) <= 1e-12 * std::max(1.0, std::fabs(p.want[i])));
          worst = std::max(worst, std::fabs(out[i] - p.want[i]));
          CHECK(633, out[i] >= 0.0 && out[i] <= (i < 3 ? 31.875 : 1.0));
        }
      }
      struct GlarePin { float t[4]; float c31[4]; double want[4]; };
      const GlarePin glare_pins[] = {
          // quad glare pins (light_formulas.post_glare): the texel, c31, the value (rgb, alpha)
          {{0.5f, 0.25f, 0.125f, 0.75f}, {0.2448834776878357f, 0.19307471811771393f, 0.0953512117266655f, 0.0f}, {0.12244173884391785, 0.04826867952942848, 0.011918901465833187, 1.0}},
          {{1.0f, 1.0f, 1.0f, 0.0f}, {0.0006944444612599909f, 0.0006944444612599909f, 0.0006944444612599909f, 0.0f}, {0.0006944444612599909, 0.0006944444612599909, 0.0006944444612599909, 1.0}},
          {{1.0f, 0.5f, 0.25f, 1.0f}, {64.0f, 2.0f, 1.0f, 0.0f}, {31.875, 1.0, 0.25, 1.0}},
          {{kNaN, 0.5f, kInf, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f}, {0.0, 0.5, 0.0, 1.0}},
          {{0.5f, 0.5f, 0.5f, 1.0f}, {1.0f, kNaN, 1.0f, 0.0f}, {0.0, 0.0, 0.0, 0.0}},
          {{0.5f, 0.5f, -0.5f, 1.0f}, {-1.0f, 1.0f, 1.0f, kNaN}, {0.0, 0.5, 0.0, 1.0}},
      };
      for (const GlarePin& p : glare_pins) {
        double out[4];
        QuadGlareMirror(p.t, p.c31, out);
        for (int i = 0; i < 4; ++i) {
          CHECK(633, std::isfinite(out[i]) && std::fabs(out[i] - p.want[i]) <= 1e-12 * std::max(1.0, std::fabs(p.want[i])));
          worst = std::max(worst, std::fabs(out[i] - p.want[i]));
        }
      }
      struct PositionPin { float corner[3]; double want[4]; };
      const PositionPin captured_pins[] = {
          // quad position pins, the captured matrix (light_formulas.post_quad_position): the corner, the clip position
          {{-2371.37548828125f, -434.8460388183594f, 259.419921875f}, {-1432.5703270674471, -1327.9451350190102, 1929.59304817058, 1929.6543738733762}},
          {{-2076.54931640625f, 71.32887268066406f, -2866.154052734375f}, {-866.1594391513121, -6077.933593238473, 411.6738536749581, 411.7655744922058}},
          {{0.0f, 0.0f, 0.0f}, {-176.43666076660156, -92.63703918457031, 0.10056735575199127, 0.20056533813476562}},
          {{3.0f, -4.0f, 5.5f}, {-178.97625237323606, -80.73331326246262, -1.1521220356225967, -1.0520988702774048}},
      };
      const PositionPin made_pins[] = {
          // quad position pins, the made matrix (light_formulas.post_quad_position): the corner, the clip position
          {{-2371.37548828125f, -434.8460388183594f, 259.419921875f}, {-3618.918212890625, -740.9821166992188, -145.61352386325598, -145.51352386176586}},
          {{-2076.54931640625f, 71.32887268066406f, -2866.154052734375f}, {-2395.2854614257812, -1291.4192810058594, -3107.7904991135, -3107.69049911201}},
          {{0.0f, 0.0f, 0.0f}, {3.0, -1.0, 0.10000000149011612, 0.20000000298023224}},
          {{3.0f, -4.0f, 5.5f}, {6.125, -6.25, 4.975000001490116, 5.075000002980232}},
      };
      const float made[4][4] = {{1.5f, 0.0f, -0.25f, 3.0f}, {0.0f, 2.0f, 0.5f, -1.0f}, {0.125f, 0.25f, 1.0f, 0.1f}, {0.125f, 0.25f, 1.0f, 0.2f}};
      for (int set = 0; set < 2; ++set) {
        for (const PositionPin& p : set ? made_pins : captured_pins) {
          double out[4];
          QuadPositionMirror(p.corner, set ? made : matrix, out);
          for (int i = 0; i < 4; ++i) {
            CHECK(633, std::fabs(out[i] - p.want[i]) <= 1e-12 * std::max(1.0, std::fabs(p.want[i])));
            worst = std::max(worst, std::fabs(out[i] - p.want[i]) / std::max(1.0, std::fabs(p.want[i])));
          }
        }
      }
      std::cout << "furniture: the moon's, the glare's and the corner's mirrors against the reference, " << std::size(moon_pins) << " + "
                << std::size(glare_pins) << " + " << std::size(captured_pins) + std::size(made_pins) << " points: worst difference "
                << worst << "\n";
      // The captured moon by day: c31.w is 0, so its alpha is 0 and the draw's alpha test (greater than
      // 1 / 255) keeps no pixel; the same moon lit would be kept. The sun's glare has no test.
      double out[4];
      const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
      QuadMoonMirror(white, moon.ps[1], moon.ps[0][3], out);
      CHECK(633, out[3] == 0.0 && !QuadAlphaKept(capture::QuadAlphaTest(moon), float(out[3]), moon.alpha_ref) && out[0] > 2.8 && out[0] < 2.9);
      const float lit[4] = {moon.ps[1][0], moon.ps[1][1], moon.ps[1][2], 1.0f};
      QuadMoonMirror(white, lit, moon.ps[0][3], out);
      CHECK(633, out[3] == 1.0 && QuadAlphaKept(true, float(out[3]), moon.alpha_ref));
      CHECK(633, QuadAlphaKept(false, 0.0f, 0.5f) && QuadAlphaKept(false, kNaN, 0.5f) && !QuadAlphaKept(true, kNaN, 0.5f) &&
                     !QuadAlphaKept(true, 0.5f, 0.5f) && QuadAlphaKept(true, std::nextafter(0.5f, 1.0f), 0.5f));
      // A corner of the sun's glare through the frame's matrix lands inside the picture, in front of the eye.
      QuadPositionMirror(glare.corners[0], matrix, out);
      CHECK(633, out[3] > 0.0 && std::fabs(out[0] / out[3]) < 1.0 && std::fabs(out[1] / out[3]) < 1.0 && out[2] / out[3] > 0.0 &&
                     out[2] / out[3] < 1.0);
    }

    // 634: nothing that is no number leaves a mirror, and nothing outside the target's range.
    {
      const float values[] = {kNaN, kInf, -kInf, -1.0f, 0.0f, 1e-30f, 0.5f, 1.0f, 64.0f, 3e38f};
      int points = 0;
      for (float t0 : values) {
        for (float ta : values) {
          for (float c : values) {
            for (float s : values) {
              const float t[4] = {t0, 0.5f, ta, ta}, c31[4] = {c, 1.0f, c, c};
              double out[4];
              QuadMoonMirror(t, c31, s, out);
              for (int i = 0; i < 4; ++i) CHECK(634, out[i] >= 0.0 && out[i] <= (i < 3 ? 31.875 : 1.0));
              QuadGlareMirror(t, c31, out);
              for (int i = 0; i < 4; ++i) CHECK(634, out[i] >= 0.0 && out[i] <= (i < 3 ? 31.875 : 1.0));
              ++points;
            }
          }
        }
      }
      CHECK(634, points == 10000);
    }

    // 635: the frame's plan: the records of the kinds this build draws, in the records' order, each with
    // its state and place; the others counted; one that cannot be drawn and the frame draws none.
    {
      const QuadRecord set[5] = {moon, cloud, glare, cloud, glare};
      const ClayQuadPlan plan = PlanClayQuads(set, 5, scale);
      CHECK(635, plan.drawable && plan.count == 3 && plan.later == 2);
      CHECK(635, plan.items[0].record == 0 && plan.items[0].state == ClayQuadState::kAlphaDepth && plan.items[0].place == ClayQuadPlace::kAfterSky);
      CHECK(635, plan.items[1].record == 2 && plan.items[1].state == ClayQuadState::kAdd && plan.items[1].place == ClayQuadPlace::kAfterRecords);
      CHECK(635, plan.items[2].record == 4 && plan.items[2].place == ClayQuadPlace::kAfterRecords);
      // An empty set, and one of other kinds only: nothing to draw, nothing wrong.
      const ClayQuadPlan empty = PlanClayQuads(nullptr, 0, scale);
      CHECK(635, empty.drawable && empty.count == 0 && empty.later == 0);
      const QuadRecord clouds[2] = {cloud, cloud};
      const ClayQuadPlan other = PlanClayQuads(clouds, 2, scale);
      CHECK(635, other.drawable && other.count == 0 && other.later == 2);
      // One record of a drawn kind that cannot be drawn (its state; its values; another frame's scale; a
      // record that is none): none of the set, also not those before it.
      QuadRecord bad_state = glare;
      bad_state.depth = 0x00708762;
      QuadRecord bad_value = glare;
      bad_value.ps[0][1] = kNaN;
      for (const QuadRecord& bad : {bad_state, bad_value, QuadRecord{}}) {
        const QuadRecord with[3] = {moon, glare, bad};
        const ClayQuadPlan none = PlanClayQuads(with, 3, scale);
        CHECK(635, !none.drawable && none.count == 0 && none.later == 0);
      }
      const ClayQuadPlan stale = PlanClayQuads(set, 5, 16.0f);
      CHECK(635, !stale.drawable && stale.count == 0);
      // More than the bound, or a count without records: nothing, not drawable (no index leaves the array).
      QuadRecord many[capture::kMaxQuadRecords + 1];
      for (QuadRecord& q : many) q = glare;
      const ClayQuadPlan full = PlanClayQuads(many, capture::kMaxQuadRecords, scale);
      CHECK(635, full.drawable && full.count == capture::kMaxQuadRecords && full.items[capture::kMaxQuadRecords - 1].record == capture::kMaxQuadRecords - 1);
      const ClayQuadPlan over = PlanClayQuads(many, capture::kMaxQuadRecords + 1, scale);
      CHECK(635, !over.drawable && over.count == 0);
      CHECK(635, !PlanClayQuads(nullptr, 3, scale).drawable);
    }

    // 636: the decision, over every combination: the first thing missing decides.
    {
      for (int bits5 = 0; bits5 < 32; ++bits5) {
        for (QuadSetReason set : {QuadSetReason::kNone, QuadSetReason::kOff, QuadSetReason::kNoDraw, QuadSetReason::kRefused,
                                  QuadSetReason::kOverLimit, QuadSetReason::kPending, QuadSetReason::kCount}) {
          ClayFurnitureInputs in;
          in.sun = bits5 & 1;
          in.post_frame = bits5 & 2;
          in.sky = bits5 & 4;
          in.drawable = bits5 & 8;
          in.objects = bits5 & 16;
          in.set = set;
          const ClayFurniture want = !in.sun                             ? ClayFurniture::kOff
                                     : !in.post_frame                    ? ClayFurniture::kNoPost
                                     : !in.sky                           ? ClayFurniture::kNoSky
                                     : set == QuadSetReason::kNoDraw     ? ClayFurniture::kNoQuads
                                     : set != QuadSetReason::kNone       ? ClayFurniture::kRecords
                                     : !in.drawable                      ? ClayFurniture::kState
                                     : !in.objects                       ? ClayFurniture::kObjects
                                                                         : ClayFurniture::kDrawn;
          CHECK(636, DecideClayFurniture(in) == want);
        }
      }
      // With the switch off nothing else is asked: off for every input.
      static_assert(DecideClayFurniture(ClayFurnitureInputs{}) == ClayFurniture::kOff);
      ClayFurnitureInputs all;
      all.post_frame = all.sky = all.drawable = all.objects = true;
      all.set = QuadSetReason::kNone;
      CHECK(636, DecideClayFurniture(all) == ClayFurniture::kOff);
      all.sun = true;
      CHECK(636, DecideClayFurniture(all) == ClayFurniture::kDrawn);
      ClayFurnitureFrames n;
      for (ClayFurniture f : {ClayFurniture::kOff, ClayFurniture::kDrawn, ClayFurniture::kNoPost, ClayFurniture::kNoSky, ClayFurniture::kNoQuads,
                              ClayFurniture::kRecords, ClayFurniture::kState, ClayFurniture::kObjects, ClayFurniture::kCount}) {
        CountClayFurnitureFrame(f, false, n);
      }
      CountClayFurnitureFrame(ClayFurniture::kDrawn, true, n);
      CountClayFurnitureFrame(ClayFurniture::kRecords, true, n);  // (a texture is missed only in a drawn frame)
      CHECK(636, n.frames == 9 && n.drawn == 2 && n.texture == 1 && n.nopost == 1 && n.nosky == 1 && n.noquads == 1 && n.records == 2 &&
                     n.state == 1 && n.objects == 1);
    }

    // 637: the Furniture line.
    {
      ClayStats st;
      CHECK(637, FormatFurnitureText(st) == "Furniture: off");
      st.sun_switch = true;
      st.quad_set = QuadSetReason::kNone;
      st.quad_records = 12;
      st.furniture = ClayFurniture::kDrawn;
      st.quad_drawn_moon = 1;
      st.quad_drawn_glare = 2;
      st.quad_texture = 0;
      st.quad_later = 9;
      st.furniture_frames = 900;
      st.furniture_drawn_frames = 890;
      st.furniture_texture_frames = 3;
      st.furniture_nopost_frames = 4;
      st.furniture_nosky_frames = 2;
      st.furniture_noquads_frames = 1;
      st.furniture_records_frames = 2;
      st.furniture_state_frames = 1;
      CHECK(637, FormatFurnitureText(st) ==
                     "Furniture: on, quads whole (12), drawn: moon 1, glare 2, quad-texture 0, other kinds 9 | frames since start 900: "
                     "drawn 890 (quad-texture 3), no-post 4, no-sky 2, no-quad 1, quad-record 2, state 1, post-objects 0");
      st.quad_set = QuadSetReason::kPending;
      st.furniture = ClayFurniture::kRecords;
      CHECK(637, FormatFurnitureText(st).rfind("Furniture: on, quads none (quad-record: pending), not drawn (quad-record) | frames", 0) == 0);
      st.quad_set = QuadSetReason::kNone;
      st.furniture = ClayFurniture::kState;
      CHECK(637, FormatFurnitureText(st).find("whole (12), not drawn (quad-record: a record's state or values) |") != std::string::npos);
      const std::pair<ClayFurniture, const char*> words[] = {{ClayFurniture::kNoPost, "not drawn (no post frame)"},
                                                             {ClayFurniture::kNoSky, "not drawn (no sky)"},
                                                             {ClayFurniture::kNoQuads, "not drawn (no quad)"},
                                                             {ClayFurniture::kObjects, "not drawn (post-objects)"},
                                                             {ClayFurniture::kOff, "not drawn (off)"}};
      for (const auto& [f, word] : words) {
        st.furniture = f;
        CHECK(637, FormatFurnitureText(st).find(word) != std::string::npos);
      }
      // (whole with every count at its largest)
      st.furniture = ClayFurniture::kDrawn;
      st.quad_records = st.quad_drawn_moon = st.quad_drawn_glare = st.quad_texture = st.quad_later = ~0u;
      st.furniture_frames = st.furniture_drawn_frames = st.furniture_texture_frames = st.furniture_nopost_frames = st.furniture_nosky_frames =
          st.furniture_noquads_frames = st.furniture_records_frames = st.furniture_state_frames = st.furniture_objects_frames = ~0ull;
      const std::string longest = FormatFurnitureText(st);
      CHECK(637, longest.size() > 300 && longest.rfind("post-objects 18446744073709551615") == longest.size() - 33);
    }

    // 638: the frame's constant buffer has the quads' slots: 48 small ones before the objects', a quad's two
    // inside them for every record of a full set.
    {
      const ClayFrameSlotPlan p = PlanClayFrameSlots(5, 3);
      CHECK(638, p.quad0 == p.post + kClayPostSlots && p.object0 == p.quad0 + kClayQuadFrameSlots);
      for (uint32_t i = 0; i < capture::kMaxQuadRecords; ++i) {
        CHECK(638, ClaySmallSlotOffset(p.quad0 + kClayQuadSlots * i) + sizeof(ClayQuadConstants) <=
                       ClaySmallSlotOffset(p.quad0 + kClayQuadSlots * (i + 1)));
      }
      CHECK(638, ClaySmallSlotOffset(p.quad0 + kClayQuadSlots * capture::kMaxQuadRecords) == ClaySmallSlotOffset(p.object0));
      CHECK(638, p.object0 <= ClayFrameSmallSlots(5, 3) && ClayFrameSmallSlots(0, 0) == 96);
    }

    // 639: the bloom's verdict when its objects are missing has a word of its own (review of plan Task 8,
    // Minor 2): not "no-weights", and counted as neither.
    {
      for (ClayBloom b : {ClayBloom::kOff, ClayBloom::kNoWeights, ClayBloom::kOn}) {
        CHECK(639, ClayBloomWithObjects(b, true) == b);
        CHECK(639, ClayBloomWithObjects(b, false) == (b == ClayBloom::kOn ? ClayBloom::kNoObjects : b));
      }
      CHECK(639, std::string(ClayBloomName(ClayBloom::kNoObjects)) == "no-objects");
      ClayPostFrames n;
      CountClayBloomFrame(ClayBloom::kNoObjects, n);
      CHECK(639, n.bloom == 0 && n.noweights == 0);
    }
  }

  // (on a thread of its own: main's frame leaves too little of the stack for another function's locals)
  std::thread(SunLayerTests).join();
  std::thread(PreviousBlendTests).join();
  std::thread(CloudTests).join();
  std::thread(WindWordTests).join();
  if (g_fail) return g_fail;
  std::cout << "PASS: clay color, keys, vertex counts, index ranges, constants, status text, "
               "uv keys, texture text, instanced keys, pipeline keys and slots, draw state, alpha constants, "
               "alpha-to-mask, terrain keys, terrain draw state, terrain constants, terrain squares, "
               "terrain text, normal and colour keys, light text, linear target and output mapping, light functions "
               "against the reference's numbers, frame and object and terrain light constants, lit squares, Light line, light buffer sources, "
               "probe colours and constants, constant buffer slots, plant light functions and constants and bend key, "
               "leaf light functions and constants and the frame's eye, shadow maps: depth convention, bias, alpha-to-mask "
               "threshold, draw rules, constants, texel mapping, Shadow line; shadowed sun: the mask in the binding table, "
               "pre-pass keys, compare directions, mask mirrors against the reference's numbers, guards, the frame's inverse "
               "view-projection, mask constants; native fog: the tables' read, the ground form and the trees' form "
               "against the reference's numbers, guards, the frame's fog constants, the frame's block with the "
               "fog's tail, the decision and the Fog line, the form of a draw's fog, a pixel's position from its depth, "
               "an unlit draw's fog against the reference's numbers and which draws take it; "
               "native sky: the lat-long coordinate, the paths and the sky against the reference's numbers, guards, the sky's "
               "rules and constants, the decision and the Sky line, the step's place among the records, the view rays; "
               "native post: the decision, the table's nearest texel, the unlit scale, the ramp's entry and the composite "
               "against the reference's numbers, nothing that is no number, the steps' constants, the frame's two words, "
               "the Post line; native bloom: the level a target stores, the downsample, the bright pass and the blurs "
               "against the reference's numbers, the place of the bloom's sample, nothing that is no number, the steps' "
               "words, the verdict; native furniture: the state a record is drawn with, its constants and what gives none, "
               "the moon's, the glare's and the corner's mirrors against the reference's numbers, nothing that is no "
               "number, the frame's plan, the decision and the Furniture line, the quads' slots; native sun layer: "
               "a layer record's state, target and constants and what gives none, the depth test's direction, the five "
               "quad formulas, the radial blur and the reduction against the reference's numbers, nothing that is no "
               "number, the layer's depth texel and the steps' words, the layer's plan, the decision and the Sun line, "
               "the sun's flag in the steps' constants; the previous-frame blend against the reference's numbers, the share 0 "
               "bit for bit, the front targets' exchange; the scene's clouds: a record's state and constants and what gives none, "
               "the mirror against the reference's numbers, nothing that is no number, the plan, the verdict and the Clouds line; "
               "wind words: the box grown by the reach, the words' keys, the Geometry line\n";

  return 0;
}
