#include "../../src/native/render/scene_dump.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
using namespace fable2::native::render;
namespace {
// One texel of an R8G8B8A8 row as the depth step writes a level (kClayDepthPackPs).
void PutLevel(uint8_t* at, uint32_t level) {
  at[0] = uint8_t(level & 255);
  at[1] = uint8_t((level >> 8) & 255);
  at[2] = uint8_t(level >> 16);
  at[3] = 255;
}
uint32_t Word(const std::vector<uint8_t>& file, size_t at) {
  uint32_t v;
  std::memcpy(&v, file.data() + at, 4);
  return v;
}
float Float(const std::vector<uint8_t>& file, size_t at) {
  float v;
  std::memcpy(&v, file.data() + at, 4);
  return v;
}
}  // namespace
int main() {
  float c[3];
  DecodeR11G11B10(0, c);
  if (c[0] != 0.0f || c[1] != 0.0f || c[2] != 0.0f) return 1;
  DecodeR11G11B10((15u << 6) | ((15u << 6) << 11) | ((15u << 5) << 22), c);   // exponent 15: 1.0 in all three
  if (c[0] != 1.0f || c[1] != 1.0f || c[2] != 1.0f) return 2;
  DecodeR11G11B10((16u << 6) | 32u, c);                                        // 2 * (1 + 32 / 64) = 3.0
  if (c[0] != 3.0f) return 3;
  DecodeR11G11B10(((16u << 5) | 16u) << 22, c);                                // blue: 2 * (1 + 16 / 32) = 3.0
  if (c[2] != 3.0f) return 4;
  DecodeR11G11B10(1u, c);                                                      // a denormal: 2^-14 / 64
  if (c[0] != std::ldexp(1.0f, -20)) return 5;
  if (DepthLevel24(0.0f) != 0 || DepthLevel24(1.0f) != 16777215u || DepthLevel24(2.0f) != 16777215u) return 6;
  if (DepthLevel24(-1.0f) != 0 || DepthLevel24(std::numeric_limits<float>::quiet_NaN()) != 0) return 7;
  for (uint32_t level : {0u, 1u, 255u, 256u, 65535u, 8388608u, 16777215u}) {
    if (DepthLevel24(DepthFromLevel24(level)) != level) return 8;
  }
  SceneDumpHeader h{kSceneDumpMagic, kSceneDumpVersion, 4, 2, 77, 77, kSceneDumpPost, 8.4f, 0, 0};
  std::vector<uint8_t> file(sizeof(h) + 4 * 2 * 12 + 4 * 2 * 4);
  std::memcpy(file.data(), &h, sizeof(h));
  SceneDumpHeader got{};
  if (!ParseSceneDump(file.data(), file.size(), &got) || got.scene_frame != 77) return 9;
  if (ParseSceneDump(file.data(), file.size() - 1, &got)) return 10;            // a short file
  h.flags |= kSceneDumpLayer;                                                    // the layer's planes are missing
  std::memcpy(file.data(), &h, sizeof(h));
  if (ParseSceneDump(file.data(), file.size(), &got)) return 11;
  h.flags = 0; h.width = 3;                                                      // an odd width with no layer: sizes still whole
  std::memcpy(file.data(), &h, sizeof(h));
  if (ParseSceneDump(file.data(), file.size(), &got)) return 12;                 // (the size no longer fits)

  // The rest is this task's own (the plan's cases end at 12).
  // The float target's other values: the channels' places in the word, the largest finite value,
  // and exponent 31 (an infinity with mantissa 0, else no number), which the file never holds.
  DecodeR11G11B10((15u << 6) << 11, c);
  if (c[0] != 0.0f || c[1] != 1.0f || c[2] != 0.0f) return 13;
  DecodeR11G11B10((30u << 6) | 63u, c);                                          // 2^15 * (1 + 63 / 64)
  if (c[0] != 65024.0f) return 14;
  DecodeR11G11B10(((30u << 5) | 31u) << 22, c);                                  // blue: 2^15 * (1 + 31 / 32)
  if (c[2] != 64512.0f) return 15;
  DecodeR11G11B10((31u << 6) | ((31u << 6 | 1u) << 11) | ((31u << 5) << 22), c);
  if (!std::isinf(c[0]) || !std::isnan(c[1]) || !std::isinf(c[2])) return 16;
  DecodeR11G11B10(63u | (1u << 22), c);                                          // the largest denormal; blue's smallest
  if (c[0] != std::ldexp(63.0f, -20) || c[2] != std::ldexp(1.0f, -19)) return 17;

  // The level: the nearest of 16777215 steps, a half rounded up, in the shader's own single
  // precision (x = d * 16777215; the whole part, one more when the rest is a half or above).
  if (DepthLevel24(0.5f) != 8388608u) return 18;                                 // 8388607.5: up
  if (DepthLevel24(std::nextafter(1.0f, 0.0f)) != 16777214u) return 19;          // just below the far plane: not the far level
  if (DepthLevel24(std::numeric_limits<float>::infinity()) != 16777215u) return 20;
  if (DepthLevel24(-std::numeric_limits<float>::infinity()) != 0 || DepthLevel24(-0.0f) != 0) return 21;
  if (DepthLevel24(1.4e-45f) != 0 || DepthLevel24(2.0e-8f) != 0 || DepthLevel24(4.0e-8f) != 1) return 22;
  if (DepthFromLevel24(16777215u) != 1.0f || DepthFromLevel24(0) != 0.0f || DepthFromLevel24(0xFFFFFFFFu) != 1.0f) return 23;
  // Odd levels in the upper half, where single precision holds whole numbers alone: `x + 0.5` would
  // round each to the even number above it.
  for (uint32_t level : {8388609u, 12345677u, 16777213u}) {
    if (DepthLevel24(DepthFromLevel24(level)) != level) return 36;
  }

  // The header's rules: magic, version, the sizes' bounds, unknown flags, the reserved word.
  {
    SceneDumpHeader good{kSceneDumpMagic, kSceneDumpVersion, 4, 2, 77, 78, kSceneDumpPost | kSceneDumpLayer | kSceneDumpPrepass, 8.4f, 3, 0};
    std::vector<uint8_t> f(SceneDumpBytes(4, 2, good.flags));
    if (f.size() != 48 + 4 * 2 * 12 + 4 * 2 * 4 + 2 * (2 * 1 * 4)) return 24;
    std::memcpy(f.data(), &good, sizeof(good));
    if (!ParseSceneDump(f.data(), f.size(), &got) || got.asked_frame != 78 || got.bad_values != 3 || got.flags != 7) return 25;
    if (ParseSceneDump(nullptr, f.size(), &got) || ParseSceneDump(f.data(), 47, &got) || ParseSceneDump(f.data(), f.size(), nullptr)) return 26;
    auto refused = [&](SceneDumpHeader bad, size_t bytes) {
      std::vector<uint8_t> g(bytes);
      std::memcpy(g.data(), &bad, sizeof(bad));
      SceneDumpHeader out{};
      return !ParseSceneDump(g.data(), g.size(), &out);
    };
    SceneDumpHeader bad = good;
    bad.magic ^= 1;
    if (!refused(bad, f.size())) return 27;
    bad = good;
    bad.version = 2;
    if (!refused(bad, f.size())) return 28;
    bad = good;
    bad.flags |= 8;                                                              // a flag this version does not know
    if (!refused(bad, f.size())) return 29;
    bad = good;
    bad.reserved = 1;
    if (!refused(bad, f.size())) return 30;
    bad = good;
    bad.width = 0;
    if (!refused(bad, sizeof(bad))) return 31;
    bad = good;
    bad.width = 5;                                                               // a layer needs even sizes
    if (!refused(bad, sizeof(bad) + 5 * 2 * 16 + 2 * (2 * 1 * 4))) return 32;
    bad = good;
    bad.width = kSceneDumpMaxSide + 1;
    if (!refused(bad, 64) || SceneDumpBytes(kSceneDumpMaxSide + 1, 2, 0) != 0 || SceneDumpBytes(0, 2, 0) != 0 ||
        SceneDumpBytes(5, 2, kSceneDumpLayer) != 0 || SceneDumpBytes(4, 2, 8) != 0) return 33;
    if (SceneDumpBytes(1120, 720, kSceneDumpLayer) != 48ull + 1120ull * 720 * 16 + 2ull * 560 * 360 * 4) return 34;
    // (a file one byte longer is not this file)
    f.push_back(0);
    if (ParseSceneDump(f.data(), f.size(), &got)) return 35;
  }

  // The file from the read-back rows: the head, the three floats a texel, the levels, the layer's
  // planes; rows of `pitch` bytes of which only the picture's are read; a value that is no number
  // or not finite is written as 0 and counted.
  {
    const uint32_t w = 4, hgt = 2, pitch = 32, lpitch = 16;
    std::vector<uint8_t> linear(pitch * hgt, 0xEE), depth(pitch * hgt, 0xEE), l0(lpitch, 0xEE), l1(lpitch, 0xEE);
    auto put = [&](std::vector<uint8_t>& rows, uint32_t p, uint32_t x, uint32_t y, uint32_t word) { std::memcpy(rows.data() + size_t(y) * p + 4 * x, &word, 4); };
    for (uint32_t y = 0; y < hgt; ++y) {
      for (uint32_t x = 0; x < w; ++x) {
        put(linear, pitch, x, y, (15u << 6) | (((16u << 6) | 32u) << 11));        // (1, 3, 0)
        PutLevel(depth.data() + size_t(y) * pitch + 4 * x, 1000u * (y * w + x) + 7);
      }
    }
    put(linear, pitch, 3, 1, (31u << 6) | (((31u << 6) | 5u) << 11) | (((16u << 5) | 16u) << 22));   // (inf, nan, 3)
    PutLevel(l0.data(), 16777215u);
    PutLevel(l0.data() + 4, 65536u);
    PutLevel(l1.data(), 255u);
    PutLevel(l1.data() + 4, 256u);
    SceneDumpPlanes planes;
    planes.linear = linear.data();
    planes.linear_pitch = pitch;
    planes.depth = depth.data();
    planes.depth_pitch = pitch;
    planes.layer[0] = l0.data();
    planes.layer[1] = l1.data();
    planes.layer_pitch = lpitch;
    std::vector<uint8_t> out;
    if (!PackSceneDump(w, hgt, 901, 900, kSceneDumpPost | kSceneDumpLayer, 8.4f, planes, &out)) return 40;
    if (out.size() != SceneDumpBytes(w, hgt, kSceneDumpLayer) || !ParseSceneDump(out.data(), out.size(), &got)) return 41;
    if (got.magic != kSceneDumpMagic || got.version != 1 || got.width != w || got.height != hgt || got.scene_frame != 901 || got.asked_frame != 900 ||
        got.flags != (kSceneDumpPost | kSceneDumpLayer) || got.exposure != 8.4f || got.bad_values != 2 || got.reserved != 0) return 42;
    if (std::memcmp(out.data(), "F2NS", 4) != 0) return 43;                       // the magic's bytes in the file's order
    size_t at = sizeof(SceneDumpHeader);
    if (Float(out, at) != 1.0f || Float(out, at + 4) != 3.0f || Float(out, at + 8) != 0.0f) return 44;
    const size_t last = at + (size_t(w) * hgt - 1) * 12;
    if (Float(out, last) != 0.0f || Float(out, last + 4) != 0.0f || Float(out, last + 8) != 3.0f) return 45;   // the two bad values as 0
    at += size_t(w) * hgt * 12;
    for (uint32_t i = 0; i < w * hgt; ++i) {
      if (Word(out, at + 4 * i) != 1000u * i + 7) return 46;                     // rows from the top, the pitch's rest unread
    }
    at += size_t(w) * hgt * 4;
    if (Word(out, at) != 16777215u || Word(out, at + 4) != 65536u || Word(out, at + 8) != 255u || Word(out, at + 12) != 256u) return 47;
    if (at + 16 != out.size()) return 48;
    // Without the layer's flag its planes are not read (and may be absent); with it they must be there.
    SceneDumpPlanes bare = planes;
    bare.layer[0] = bare.layer[1] = nullptr;
    if (!PackSceneDump(w, hgt, 5, 5, kSceneDumpPrepass, 1.0f, bare, &out) || out.size() != SceneDumpBytes(w, hgt, 0) ||
        !ParseSceneDump(out.data(), out.size(), &got) || got.flags != kSceneDumpPrepass) return 49;
    if (PackSceneDump(w, hgt, 5, 5, kSceneDumpLayer, 1.0f, bare, &out) || !out.empty()) return 50;
    // Fail closed: a missing plane, a pitch shorter than a row, sizes the file cannot have, no vector.
    SceneDumpPlanes bad = planes;
    bad.depth = nullptr;
    if (PackSceneDump(w, hgt, 5, 5, 0, 1.0f, bad, &out) || !out.empty()) return 51;
    bad = planes;
    bad.linear_pitch = 4 * w - 4;
    if (PackSceneDump(w, hgt, 5, 5, 0, 1.0f, bad, &out)) return 52;
    bad = planes;
    bad.layer_pitch = 4;
    if (PackSceneDump(w, hgt, 5, 5, kSceneDumpLayer, 1.0f, bad, &out)) return 53;
    if (PackSceneDump(0, hgt, 5, 5, 0, 1.0f, planes, &out) || PackSceneDump(5, hgt, 5, 5, kSceneDumpLayer, 1.0f, planes, &out) ||
        PackSceneDump(w, hgt, 5, 5, 8, 1.0f, planes, &out) || PackSceneDump(w, hgt, 5, 5, 0, 1.0f, planes, nullptr)) return 54;
    // An exposure that is no number is written as 0 (the head holds no value that is not finite).
    if (!PackSceneDump(w, hgt, 5, 5, 0, std::numeric_limits<float>::quiet_NaN(), planes, &out) || !ParseSceneDump(out.data(), out.size(), &got) ||
        got.exposure != 0.0f) return 55;
  }

  // The run's bounds (SceneDumpAsk): a request is taken while fewer than the limit were taken; one
  // that comes while another waits replaces it and is counted; frame 0 asks nothing.
  {
    SceneDumpCounts n;
    if (AskSceneDump(0, false, n) != SceneDumpAsk::kNoFrame || n.asked != 0) return 60;
    if (AskSceneDump(10, false, n) != SceneDumpAsk::kTaken || n.asked != 1 || n.replaced != 0) return 61;
    if (AskSceneDump(11, true, n) != SceneDumpAsk::kTaken || n.asked != 2 || n.replaced != 1) return 62;
    for (uint32_t i = 2; i < kSceneDumpMaxFiles; ++i) {
      if (AskSceneDump(20 + i, false, n) != SceneDumpAsk::kTaken) return 63;
    }
    if (n.asked != kSceneDumpMaxFiles) return 64;
    if (AskSceneDump(99, false, n) != SceneDumpAsk::kOverLimit || n.asked != kSceneDumpMaxFiles || n.over_limit != 1) return 65;
    if (AskSceneDump(99, true, n) != SceneDumpAsk::kOverLimit || n.over_limit != 2 || n.replaced != 1) return 66;
    // (the names the log writes)
    if (std::string(SceneDumpFaultName(SceneDumpFault::kObjects)) != "objects" || std::string(SceneDumpFaultName(SceneDumpFault::kBuffer)) != "buffer" ||
        std::string(SceneDumpFaultName(SceneDumpFault::kRead)) != "read" || std::string(SceneDumpFaultName(SceneDumpFault::kOverLimit)) != "over-limit" ||
        std::string(SceneDumpFaultName(SceneDumpFault::kReplaced)) != "replaced" || std::string(SceneDumpFaultName(SceneDumpFault::kLost)) != "lost") return 67;
    // Whether a Render serves the request: the scene asked for or a later one, never an earlier one.
    if (SceneDumpDue(0, 5) || SceneDumpDue(6, 5) || !SceneDumpDue(5, 5) || !SceneDumpDue(5, 9)) return 68;
    // Each capture asks once: when its first scene frame is known and is not the one last asked for.
    if (SceneDumpNewRequest(0, 0) || SceneDumpNewRequest(0, 7) || SceneDumpNewRequest(7, 7) || !SceneDumpNewRequest(7, 0) ||
        !SceneDumpNewRequest(9, 7)) return 69;
  }
  // The impostors' pictures beside a dump (sub-project 12, plan Task 14; criterion 6): native_scene_<stamp>.f2np.
  {
    const size_t n = kImpostorPictureBytes;
    if (n != 256u * 256u * 4u || sizeof(ImpostorPicturesHeader) != 24 || sizeof(ImpostorPictureHead) != 16) return 80;
    std::vector<uint8_t> a(n), b(n);
    for (size_t i = 0; i < n; ++i) { a[i] = uint8_t(i * 7 + 1); b[i] = uint8_t(i * 13 + 5); }
    const ImpostorPictureEntry two[2] = {{{0x12704000u, 422u, 0x1122334455667788ull}, a.data()}, {{0x1272F000u, 423u, 0x99AABBCCDDEEFF00ull}, b.data()}};
    std::vector<uint8_t> file;
    if (!PackImpostorPictures(4711, two, 2, &file) || file.size() != 24 + 2 * (16 + n)) return 81;
    if (file[0] != 'F' || file[1] != '2' || file[2] != 'N' || file[3] != 'P') return 82;
    ImpostorPicturesHeader head{};
    std::vector<ImpostorPictureHead> heads;
    std::vector<const uint8_t*> pictures;
    if (!ParseImpostorPictures(file.data(), file.size(), &head, &heads, &pictures)) return 83;
    if (head.magic != kImpostorPicturesMagic || head.version != 1 || head.scene_frame != 4711 || head.count != 2 || head.flags != 0) return 84;
    if (heads.size() != 2 || pictures.size() != 2 || heads[0].base != 0x12704000u || heads[0].place != 422 || heads[0].hash != 0x1122334455667788ull ||
        heads[1].base != 0x1272F000u || heads[1].place != 423 || heads[1].hash != 0x99AABBCCDDEEFF00ull) return 85;
    if (std::memcmp(pictures[0], a.data(), n) != 0 || std::memcmp(pictures[1], b.data(), n) != 0) return 86;
    // Refused: one byte short, one byte long, a count of 33, a wrong magic, a wrong version, a flag that has no meaning.
    if (ParseImpostorPictures(file.data(), file.size() - 1, &head, &heads, &pictures) || !heads.empty() || !pictures.empty()) return 87;
    std::vector<uint8_t> longer = file;
    longer.push_back(0);
    if (ParseImpostorPictures(longer.data(), longer.size(), &head, &heads, &pictures)) return 88;
    std::vector<uint8_t> bad = file;
    const uint32_t thirty_three = 33;
    std::memcpy(bad.data() + 16, &thirty_three, 4);
    if (ParseImpostorPictures(bad.data(), bad.size(), &head, &heads, &pictures)) return 89;
    bad = file;
    bad[3] = 'S';
    if (ParseImpostorPictures(bad.data(), bad.size(), &head, &heads, &pictures)) return 90;
    bad = file;
    bad[4] = 2;
    if (ParseImpostorPictures(bad.data(), bad.size(), &head, &heads, &pictures)) return 91;
    bad = file;
    bad[20] = 2;
    if (ParseImpostorPictures(bad.data(), bad.size(), &head, &heads, &pictures)) return 92;
    bad = file;
    bad[23] = 0x80;
    if (ParseImpostorPictures(bad.data(), bad.size(), &head, &heads, &pictures)) return 92;
    // A feed run's file says so (ledger ruling 47): bit 0 of the head's flags, the pictures as before; a file
    // packed without the word, and with it false, is the file of before byte for byte.
    {
      std::vector<uint8_t> fed, plain;
      if (kImpostorPicturesFromFeed != 1u || !PackImpostorPictures(4711, two, 2, &fed, true) || !PackImpostorPictures(4711, two, 2, &plain, false)) return 98;
      if (plain != file || fed.size() != file.size() || fed[20] != 1 || fed[21] != 0 || fed[22] != 0 || fed[23] != 0) return 99;
      fed[20] = 0;
      if (fed != file) return 100;
      fed[20] = 1;
      ImpostorPicturesHeader fh{};
      if (!ParseImpostorPictures(fed.data(), fed.size(), &fh, &heads, &pictures) || fh.flags != kImpostorPicturesFromFeed || fh.count != 2 ||
          heads.size() != 2 || std::memcmp(pictures[1], b.data(), n) != 0) return 101;
    }
    if (ParseImpostorPictures(nullptr, file.size(), &head, &heads, &pictures)) return 93;
    // Packing: no picture's bytes, more than 32, no file: nothing written. A file of no picture is a file.
    const ImpostorPictureEntry none[1] = {{{0x12704000u, 1u, 2ull}, nullptr}};
    if (PackImpostorPictures(1, none, 1, &file) || !file.empty()) return 94;
    std::vector<ImpostorPictureEntry> many(33, two[0]);
    if (PackImpostorPictures(1, many.data(), 33, &file) || !file.empty() || PackImpostorPictures(1, two, 2, nullptr)) return 95;
    if (!PackImpostorPictures(9, many.data(), 32, &file) || file.size() != 24 + 32 * (16 + n)) return 96;
    if (!PackImpostorPictures(9, nullptr, 0, &file) || file.size() != 24 || !ParseImpostorPictures(file.data(), file.size(), &head, &heads, &pictures) ||
        head.count != 0 || head.scene_frame != 9 || !heads.empty()) return 97;
  }
  std::cout << "PASS: scene dump\n";
  return 0;
}
