// Synthetic standalone test for the per-frame timing window; no game or GPU.
#include "../../src/diagnostics/window_stats.h"

#include <cmath>
#include <iostream>

using fable2::diagnostics::WindowStats;

static bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
  WindowStats w(5);
  if (w.size() != 0 || w.full() || w.last() != 0 || w.Median() != 0 || w.Max() != 0) return 1;
  for (double v : {3.0, 1.0, 4.0, 1.0}) w.Add(v);
  if (w.size() != 4 || w.full() || !Near(w.last(), 1.0)) return 2;
  // Even count: the upper middle sample.
  if (!Near(w.Median(), 3.0) || !Near(w.Max(), 4.0)) return 3;
  w.Add(5.0);
  if (!w.full() || !Near(w.Median(), 3.0) || !Near(w.Max(), 5.0) || !Near(w.last(), 5.0)) return 4;
  if (!Near(w.Percentile(0.0), 1.0) || !Near(w.Percentile(1.0), 5.0)) return 5;
  if (!Near(w.Percentile(0.9), 5.0) || !Near(w.Percentile(0.5), 3.0)) return 6;
  if (!Near(w.Mean(), 14.0 / 5.0)) return 11;
  // Samples past the capacity are dropped until Reset; the last value still updates.
  w.Add(100.0);
  if (w.size() != 5 || !Near(w.Max(), 5.0) || !Near(w.last(), 100.0)) return 7;
  // Querying does not reorder what Add appended in a way that changes results.
  if (!Near(w.Median(), 3.0) || !Near(w.Median(), 3.0)) return 8;
  w.Reset();
  if (w.size() != 0 || w.full() || w.Median() != 0) return 9;
  w.Add(2.5);
  if (!Near(w.Median(), 2.5) || !Near(w.Max(), 2.5)) return 10;
  std::cout << "PASS: window stats\n";
  return 0;
}
