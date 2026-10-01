// Synthetic standalone test; no game or GPU.
#include <rex/graphics/native_suppress_policy.h>
#include <iostream>

using namespace rex::graphics::native_suppress;

int main() {
  // 1. Empty text suppresses nothing.
  PitchList none = ParsePitchList("");
  if (none.all || !none.pitches.empty() || none.invalid_tokens) return 1;
  if (ShouldSuppressPitch(none, none, 1280)) return 2;

  // 2. Explicit list with spaces.
  PitchList list = ParsePitchList(" 1280, 640 ,1024");
  if (list.pitches.size() != 3 || !Contains(list, 640) || Contains(list, 512)) return 3;

  // 3. Invalid and empty tokens are skipped and counted, valid ones kept.
  PitchList bad = ParsePitchList("1280,abc, 640,,99999999999,-5");
  if (bad.pitches.size() != 2 || !Contains(bad, 1280) || !Contains(bad, 640)) return 4;
  if (bad.invalid_tokens != 3) return 5;  // "abc", "99999999999", "-5"

  // 4. Garbage-only text never means "all".
  PitchList garbage = ParsePitchList("*x,?");
  if (garbage.all || garbage.invalid_tokens != 2) return 6;

  // 5. Wildcard suppresses everything except the keep list.
  PitchList all = ParsePitchList("*");
  PitchList keep = ParsePitchList("1024, 512");
  if (!all.all) return 7;
  if (!ShouldSuppressPitch(all, keep, 1280)) return 8;
  if (ShouldSuppressPitch(all, keep, 1024)) return 9;
  if (ShouldSuppressPitch(list, keep, 1024)) return 10;  // in both: keep wins

  std::cout << "PASS: empty, list, invalid tokens, garbage, wildcard + keep\n";
  return 0;
}
