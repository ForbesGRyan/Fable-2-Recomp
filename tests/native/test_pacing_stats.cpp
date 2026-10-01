// Synthetic standalone test for the F3 overlay pacing helpers.
#include <rex/ui/overlay/pacing_stats.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

int main() {
  using namespace rex::ui;
  auto z = SummarizePacing(nullptr, 0);
  if (z.avg != 0 || z.min != 0 || z.max != 0 || z.low1 != 0) return 1;
  float zeros[4] = {0, 0, -1, 0};
  z = SummarizePacing(zeros, 4);
  if (z.avg != 0 || z.min != 0 || z.max != 0 || z.low1 != 0) return 2;

  float s[] = {10, 20, 30, 40};
  auto a = SummarizePacing(s, 4);
  if (!Near(a.avg, 25) || !Near(a.min, 10) || !Near(a.max, 40)) return 3;

  // Zeros are ignored.
  float s2[] = {0, 10, 0, 30};
  auto b = SummarizePacing(s2, 4);
  if (!Near(b.avg, 20) || !Near(b.min, 10) || !Near(b.max, 30)) return 4;

  // 100 samples: ranks 1..99 are 33.3, rank 100 is 50.0. Nearest-rank P99 =
  // ceil(0.99*100) = 99th smallest = 33.3 (the single outlier is the 1% tail).
  std::vector<float> v(99, 33.3f);
  v.push_back(50.0f);
  auto c = SummarizePacing(v.data(), v.size());
  if (!Near(c.low1, 33.3f) || !Near(c.max, 50.0f)) return 5;
  // 200 samples with 4 slow ones: P99 = ceil(198) = 198th smallest = 50.
  std::vector<float> w(196, 33.3f);
  for (int i = 0; i < 4; ++i) w.push_back(50.0f);
  auto d = SummarizePacing(w.data(), w.size());
  if (!Near(d.low1, 50.0f)) return 6;

  // (work, swap, limiter, cp_busy, cp_wait_reg, cp_present, cp_idle)
  if (std::strcmp(PacingVerdict(9, 24, 0, 4, 0, 0, 7), "paced (vsync/vblank)") != 0) return 7;
  if (std::strcmp(PacingVerdict(30, 3, 0, 10, 0, 0, 0), "guest-CPU-bound") != 0) return 8;
  if (std::strcmp(PacingVerdict(5, 2, 0, 20, 0, 0, 0), "GPU-emulation-bound") != 0) return 9;
  if (std::strcmp(PacingVerdict(0, 0, 0, 0, 0, 0, 0), "") != 0) return 10;
  // GPU wait_reg counts toward pacing.
  if (std::strcmp(PacingVerdict(10, 1, 0, 5, 15, 0, 0), "paced (vsync/vblank)") != 0) return 11;
  // Limiter/fence wait counts as guest waiting.
  if (std::strcmp(PacingVerdict(5, 1, 25, 3, 0, 0, 0), "paced (vsync/vblank)") != 0) return 12;
  // CP present time counts as waiting.
  if (std::strcmp(PacingVerdict(10, 1, 0, 5, 0, 15, 0), "paced (vsync/vblank)") != 0) return 13;
  // Without the limiter bucket the same frame is CPU-bound.
  if (std::strcmp(PacingVerdict(25, 1, 0, 3, 0, 0, 0), "guest-CPU-bound") != 0) return 14;
  std::cout << "PASS: pacing summary + verdict\n";
  return 0;
}
