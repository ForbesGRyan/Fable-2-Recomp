// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/draw_nesting.h"
#include <iostream>

using namespace fable2::native::capture;

int main() {
  DrawNesting n;
  // A lone draw is outermost.
  if (n.Enter()) return 1;  // Enter returns true when the draw is nested
  if (!n.Leave()) return 2;
  if (n.depth() != 0 || n.nested() != 0) return 3;

  // Draw A calls draw B which calls draw C: only A is outermost.
  if (n.Enter()) return 10;
  if (!n.Enter()) return 11;
  if (!n.Enter()) return 12;
  if (n.depth() != 3) return 13;
  if (n.Leave()) return 14;  // C
  if (n.Leave()) return 15;  // B
  if (!n.Leave()) return 16; // A
  if (n.nested() != 2) return 17;

  // A return without a matching call (hook enabled between call and return)
  // never underflows and counts as outermost.
  if (!n.Leave()) return 20;
  if (n.depth() != 0) return 21;
  if (n.Enter()) return 22;
  if (!n.Leave()) return 23;

  std::cout << "PASS: draw nesting\n";
  return 0;
}
