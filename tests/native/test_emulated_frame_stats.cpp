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
  acc.AddCpTime(100, 20, 5, 7);
  acc.AddCpTime(50, 10, 1, 3);
  auto f5 = acc.CloseFrame(20'000'000);
  if (f5.cp_busy_ns != 150 || f5.cp_wait_reg_ns != 30 || f5.cp_idle_ns != 6 || f5.cp_present_ns != 10) return 10;
  auto f6 = acc.CloseFrame(21'000'000);
  if (f6.cp_busy_ns != 0 || f6.cp_wait_reg_ns != 0 || f6.cp_idle_ns != 0 || f6.cp_present_ns != 0) return 11;
  // Predicated tiling: bin-select writes, distinct non-zero selects = tiles.
  acc.AddBinSelect(0x1);
  acc.AddBinSelect(0x2);
  acc.AddBinSelect(0x1);  // repeat: not a new tile
  acc.AddBinSelect(0x0);  // zero select: not a tile
  acc.AddPredicatedPacket(true, true);
  acc.AddPredicatedPacket(true, false);
  acc.AddPredicatedPacket(false, true);
  auto f7 = acc.CloseFrame(22'000'000);
  if (f7.bin_select_writes != 4 || f7.tiles != 2) return 12;
  if (f7.tile_selects[0] != 0x1 || f7.tile_selects[1] != 0x2) return 20;
  if (f7.predicated_draws != 1 || f7.predicated_skips != 1) return 13;
  auto f8 = acc.CloseFrame(23'000'000);
  if (f8.bin_select_writes != 0 || f8.tiles != 0 || f8.predicated_draws != 0 || f8.predicated_skips != 0) return 14;
  for (uint64_t s = 1; s <= 20; ++s) acc.AddBinSelect(s);  // distinct tiles saturate
  auto f9 = acc.CloseFrame(24'000'000);
  if (f9.tiles != rex::graphics::EmulatedFrameStats::kMaxTiles || f9.bin_select_writes != 20) return 15;
  // Per-pitch extents: max window-scissor corner and viewport size.
  acc.AddDraw(1120, 1, 1120, 320, 1120, 320);
  acc.AddDraw(1120, 1, 1120, 312, 1120, 632);
  acc.AddDraw(640, 1);  // no extent recorded
  auto f10 = acc.CloseFrame(25'000'000);
  if (f10.pitch_count != 2 || f10.pitches[0] != 1120) return 16;
  if (f10.pitch_window_br[0][0] != 1120 || f10.pitch_window_br[0][1] != 320) return 17;
  if (f10.pitch_viewport[0][0] != 1120 || f10.pitch_viewport[0][1] != 632) return 18;
  if (f10.pitch_window_br[1][0] != 0 || f10.pitch_viewport[1][1] != 0) return 19;
  std::cout << "PASS: per-frame draws, time, intervals, pitch histogram + overflow, tiling, extents\n";
  return 0;
}
