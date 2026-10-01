#include "../../src/native/capture/discovery_format.h"
#include <iostream>
int main() {
  const uint32_t v[2] = {0x1, 0xDEADBEEF};
  if (fable2::native::capture::HexDwords(v, 2) != "[\"0x00000001\",\"0xDEADBEEF\"]") return 1;
  if (fable2::native::capture::HexDwords(nullptr, 2) != "null") return 2;
  std::cout << "PASS: discovery format\n";
  return 0;
}
