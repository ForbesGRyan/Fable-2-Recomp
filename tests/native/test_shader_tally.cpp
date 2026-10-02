// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/shader_tally.h"
#include "../../src/native/render/frame_scene.h"
#include <cstdio>

using namespace fable2::native;
using capture::ShaderTally;
using capture::SkipReason;

int main() {
  ShaderTally<4> t;
  using E = ShaderTally<4>::Entry;
  t.Add(0xA, 1);
  t.Add(0xB, 1);
  t.Add(0xB, 1);
  t.Add(0xC, 2);
  t.Add(0xA, 2);  // same hash, other kind: its own entry
  E top[4];
  if (t.Top(1, top, 4) != 2) return 1;
  if (top[0].hash != 0xB || top[0].count != 2 || top[1].hash != 0xA || top[1].count != 1) return 2;
  if (t.Top(2, top, 4) != 2 || top[0].hash != 0xA || top[1].hash != 0xC) return 3;  // tie: lower hash
  if (t.Top(1, top, 1) != 1 || top[0].hash != 0xB) return 4;
  if (t.Top(3, top, 4) != 0) return 5;
  if (t.Total(1) != 3 || t.Total(2) != 2) return 6;
  // Full: a new pair goes to Other, existing pairs still count.
  t.Add(0xD, 1);
  if (t.Other() != 1 || t.size() != 4) return 7;
  t.Add(0xB, 1);
  if (t.Total(1) != 4 || t.Other() != 1) return 8;
  t.Clear();
  if (t.size() != 0 || t.Other() != 0 || t.Top(1, top, 4) != 0) return 9;

  // FrameBuilder tallies skipped draws by shader and reason, except
  // unsupported-prim (broken down by hook); Finish moves the tally.
  render::FrameBuilder b;
  b.Open();
  capture::DrawRecord r;
  r.vs_hash = 0xD4D5;
  r.skip = SkipReason::kNoTransform;
  b.Add(r);
  b.Add(r);
  r.vs_hash = 0x1234;
  r.skip = SkipReason::kUnsupportedPrim;
  b.Add(r);
  r.vs_hash = 0xECD6;
  r.skip = SkipReason::kNone;
  b.Add(r);
  auto s = b.Finish(1);
  using F = ShaderTally<render::kSkippedTallySize>::Entry;
  F f[4];
  if (s->skipped_by_vs.Top(uint8_t(SkipReason::kNoTransform), f, 4) != 1) return 10;
  if (f[0].hash != 0xD4D5 || f[0].count != 2) return 11;
  if (s->skipped_by_vs.Total(uint8_t(SkipReason::kUnsupportedPrim)) != 0) return 12;
  if (s->skipped_by_vs.size() != 1 || s->draws.size() != 1) return 13;
  b.Open();
  if (b.Finish(2)->skipped_by_vs.size() != 0) return 14;
  std::puts("PASS: shader tally, frame builder skipped-by-shader");
  return 0;
}
