// Synthetic standalone test; no game or GPU.
#include <rex/graphics/emulated_frame_stats.h>
#include <iostream>

int main() {
  rex::graphics::EmulatedFrameAccumulator acc;
  acc.AddDraw(1280, 100);
  acc.AddDraw(1280, 50);
  acc.AddDraw(640, 25);
  auto f1 = acc.CloseFrame(1'000'000);
  if (f1.frame != 1 || f1.draws != 3 || f1.draw_cpu_ns != 175) return 1;
  if (f1.swap_interval_ns != 0) return 2;  // first frame has no previous swap
  if (f1.pitch_count != 2 || f1.pitches[0] != 1280 || f1.pitch_draws[0] != 2) return 3;
  if (f1.pitches[1] != 640 || f1.pitch_draws[1] != 1) return 4;
  auto f2 = acc.CloseFrame(17'000'000);
  if (f2.frame != 2 || f2.draws != 0 || f2.swap_interval_ns != 16'000'000) return 5;
  for (uint32_t p = 0; p < 20; ++p) acc.AddDraw(100 + p, 1);  // overflow distinct pitches
  auto f3 = acc.CloseFrame(18'000'000);
  if (f3.pitch_count != 16 || f3.other_pitch_draws != 4 || f3.draws != 20) return 6;
  acc.AddCopy(10);
  acc.AddDraw(1280, 5);
  acc.AddCopy(32);
  auto f4 = acc.CloseFrame(19'000'000);
  if (f4.copies != 2 || f4.copy_cpu_ns != 42) return 7;
  if (f4.draws != 1 || f4.draw_cpu_ns != 5) return 8;
  if (f4.pitch_count != 1 || f4.pitch_draws[0] != 1 || f4.other_pitch_draws != 0) return 9;
  std::cout << "PASS: per-frame draws, time, intervals, pitch histogram + overflow\n";
  return 0;
}
