// Synthetic standalone test; no game or GPU.
#include "../../src/native/native_render_state.h"
#include <iostream>

using namespace fable2::native;

int main() {
  // View parsing and cycling
  auto pv = ParseView("  Split ");
  if (pv.view != View::kSplit || !pv.recognized) return 1;
  pv = ParseView("bogus");
  if (pv.view != View::kOff || pv.recognized) return 2;
  if (ParseView("pattern").view != View::kPattern) return 3;
  // The shadow view: set by name (the cvar, a plan's VIEW=shadow), a composite view (the clay pass
  // renders and records are wanted), and no key leads to it: F6 cycles the other five and leaves it.
  pv = ParseView(" Shadow");
  if (pv.view != View::kShadow || !pv.recognized || std::string(ViewName(View::kShadow)) != "shadow") return 40;
  for (View from : {View::kOff, View::kOverlay, View::kSplit, View::kNative, View::kPattern}) {
    if (NextView(from) == View::kShadow) return 41;
  }
  if (NextView(View::kShadow) != View::kOff) return 42;
  if (!IsCompositeView(View::kShadow) || !RecordsWanted(View::kShadow, false) || RecordsWanted(View::kShadow, true)) return 43;
  if (!ViewChanges(View::kNative, View::kShadow) || ViewChanges(View::kShadow, View::kShadow)) return 44;
  View v = View::kOff;
  const View expect[] = {View::kOverlay, View::kSplit, View::kNative, View::kPattern, View::kOff};
  for (View e : expect) { v = NextView(v); if (v != e) return 4; }
  if (std::string(ViewName(View::kNative)) != "native") return 5;

  // EdgeDetector: only up->down transitions fire.
  EdgeDetector edge;
  if (edge.Update(false)) return 6;
  if (!edge.Update(true)) return 7;
  if (edge.Update(true)) return 8;   // held
  if (edge.Update(false)) return 9;  // release
  if (!edge.Update(true)) return 10; // pressed again

  // FailureLatch: first Fail() reports, later ones don't; Clear() re-arms.
  FailureLatch latch;
  if (latch.IsFailed()) return 11;
  if (!latch.Fail()) return 12;
  if (latch.Fail()) return 13;
  if (!latch.IsFailed()) return 14;
  latch.Clear();
  if (latch.IsFailed()) return 15;
  if (!latch.Fail()) return 16;

  // Capture builds draw records only for a consumer: a composite view with
  // the latch clear.
  if (IsCompositeView(View::kOff) || IsCompositeView(View::kPattern)) return 17;
  if (!IsCompositeView(View::kOverlay) || !IsCompositeView(View::kSplit) ||
      !IsCompositeView(View::kNative)) return 18;
  if (!RecordsWanted(View::kSplit, false) || RecordsWanted(View::kSplit, true)) return 19;
  if (RecordsWanted(View::kOff, false) || RecordsWanted(View::kPattern, false)) return 20;
  // The capture takes the game's shadow passes only for a frame the pass could draw with shadows:
  // records wanted, the shadows' and the light's switches and the textures on, and the pass's
  // shadow objects not failed (after "the native shadows are off" nothing more is recorded for them).
  if (!ShadowRecordsWanted(true, true, true, true, true)) return 57;
  for (int off = 0; off < 5; ++off) {
    if (ShadowRecordsWanted(off != 0, off != 1, off != 2, off != 3, off != 4)) return 58;
  }
  if (ShadowRecordsWanted(false, false, false, false, false)) return 59;
  // The capture reads the frame's fog and sky blocks only for a frame the pass could draw with
  // them: records wanted, the fog's and the light's switches and the textures on.
  if (!FogRecordsWanted(true, true, true, true)) return 60;
  for (int off = 0; off < 4; ++off) {
    if (FogRecordsWanted(off != 0, off != 1, off != 2, off != 3)) return 61;
  }
  if (FogRecordsWanted(false, false, false, false)) return 62;
  // The capture reads the frame's post block only for a frame the pass could draw through the
  // game's composite: records wanted, the post's and the light's switches and the textures on.
  if (!PostRecordsWanted(true, true, true, true)) return 240;
  for (int off = 0; off < 4; ++off) {
    if (PostRecordsWanted(off != 0, off != 1, off != 2, off != 3)) return 241;
  }
  if (PostRecordsWanted(false, false, false, false)) return 242;

  // Setting the view to what it already is (a plan's VIEW= token) is no change: it is not counted
  // for the clay log's "(changed)" mark. Every F6 step is one.
  if (ViewChanges(View::kNative, View::kNative) || ViewChanges(View::kOff, View::kOff)) return 21;
  if (!ViewChanges(View::kOff, View::kNative) || !ViewChanges(View::kSplit, View::kOverlay)) return 22;
  for (View from : {View::kOff, View::kOverlay, View::kSplit, View::kNative, View::kPattern}) {
    if (!ViewChanges(from, NextView(from))) return 23;
  }

  // Guest frame times per view (the "[native] frames:" log line). An interval is the time between two
  // consecutive frames of one view; a line comes when a view holds kWindow of them, and at a switch.
  {
    ViewFrameLog log;
    if (!log.Add(View::kNative, 1000.0).empty()) return 24;  // the first frame has no interval
    double t = 1000.0;
    for (size_t i = 1; i < ViewFrameLog::kWindow; ++i) {
      t += 40.0;
      if (!log.Add(View::kNative, t).empty()) return 25;
    }
    t += 40.0;
    if (log.Add(View::kNative, t) !=
        "view native (window): 150 frames, mean 40.00 ms (25.0 fps), median 40.00 ms, p90 40.00 ms, max 40.00 ms") {
      return 26;
    }
    // The next window starts empty: 9 frames of 30 ms, 9 of 36 and 2 of 50, then the view changes.
    for (int i = 0; i < 20; ++i) {
      t += (i == 5 || i == 12) ? 50.0 : i < 10 ? 30.0 : 36.0;
      if (!log.Add(View::kNative, t).empty()) return 27;
    }
    t += 70.0;  // the frame of the switch: its interval belongs to neither view
    if (log.Add(View::kOff, t) !=
        "view native (until the switch): 20 frames, mean 34.70 ms (28.8 fps), median 36.00 ms, p90 50.00 ms, max 50.00 ms") {
      return 28;
    }
    // A stretch shorter than kMinFrames is not reported, and leaves nothing behind.
    for (size_t i = 0; i + 1 < ViewFrameLog::kMinFrames; ++i) {
      t += 33.0;
      if (!log.Add(View::kOff, t).empty()) return 29;
    }
    t += 33.0;
    if (!log.Add(View::kSplit, t).empty()) return 30;
    for (size_t i = 1; i < ViewFrameLog::kWindow; ++i) {
      t += 25.0;
      if (!log.Add(View::kSplit, t).empty()) return 31;
    }
    t += 25.0;
    if (log.Add(View::kSplit, t) !=
        "view split (window): 150 frames, mean 25.00 ms (40.0 fps), median 25.00 ms, p90 25.00 ms, max 25.00 ms") {
      return 32;
    }
    // Exactly kMinFrames intervals at a switch are reported; a clock that steps back counts as 0.
    for (size_t i = 0; i < ViewFrameLog::kMinFrames; ++i) {
      t += i == 0 ? -5.0 : 10.0;
      if (!log.Add(View::kSplit, t).empty()) return 33;
    }
    if (log.Add(View::kNative, t + 10.0) !=
        "view split (until the switch): 10 frames, mean 9.00 ms (111.1 fps), median 10.00 ms, p90 10.00 ms, max 10.00 ms") {
      return 34;
    }
    // The median of an even count is the upper middle value (index n / 2), the largest is the one
    // largest value (here it stands alone), and a stretch whose intervals are all zero (a clock that
    // stands still) has no rate, not an infinite one.
    ViewFrameLog two;
    double u = 0.0;
    two.Add(View::kNative, u);
    for (int i = 0; i < 10; ++i) {
      u += i == 2 ? 30.0 : i < 6 ? 10.0 : 20.0;   // (five of 10, four of 20, one of 30, not in order)
      if (!two.Add(View::kNative, u).empty()) return 35;
    }
    if (two.Add(View::kOff, u + 1.0) !=
        "view native (until the switch): 10 frames, mean 16.00 ms (62.5 fps), median 20.00 ms, p90 30.00 ms, max 30.00 ms") {
      return 36;
    }
    ViewFrameLog still;
    for (int i = 0; i <= 10; ++i) {
      if (!still.Add(View::kSplit, 500.0).empty()) return 37;
    }
    if (still.Add(View::kOff, 500.0) !=
        "view split (until the switch): 10 frames, mean 0.00 ms (0.0 fps), median 0.00 ms, p90 0.00 ms, max 0.00 ms") {
      return 38;
    }
  }

  // The Direct3D debug layer's messages in the game log (fable2_native_d3d_messages): errors,
  // corruption and warnings as lines, at most kPerId of one severity and id and kTotal in all; every
  // message is counted, written or not, and the summary says so.
  {
    D3dMessageLog log;
    if (log.Summary() != "d3d messages: errors 0, warnings 0") return 40;
    if (log.Add(D3dSeverity::kError, 1328, "CreateCommittedResource: bad") != "d3d ERROR id 1328 (seen 1): CreateCommittedResource: bad") return 41;
    if (log.Add(D3dSeverity::kWarning, 1328, "w") != "d3d warning id 1328 (seen 1): w") return 42;      // (its own count)
    if (log.Add(D3dSeverity::kCorruption, 7, "c") != "d3d CORRUPTION id 7 (seen 1): c") return 43;
    if (!log.Add(D3dSeverity::kOther, 9, "info").empty()) return 44;                                      // not written, not counted
    for (uint32_t i = 2; i <= D3dMessageLog::kPerId; ++i) {
      if (log.Add(D3dSeverity::kError, 1328, "x") != "d3d ERROR id 1328 (seen " + std::to_string(i) + "): x") return 45;
    }
    if (!log.Add(D3dSeverity::kError, 1328, "x").empty() || !log.Add(D3dSeverity::kError, 1328, "x").empty()) return 46;
    if (log.Add(D3dSeverity::kError, 1329, "y") != "d3d ERROR id 1329 (seen 1): y") return 47;          // another id still writes
    if (log.Summary() != "d3d messages: errors 8, warnings 1 (CORRUPTION 1)") return 48;
    // A text with line breaks or control characters is one line; a long one is cut.
    if (log.Add(D3dSeverity::kWarning, 5, "a\r\nb\tc") != "d3d warning id 5 (seen 1): a  b c") return 49;
    const std::string cut = log.Add(D3dSeverity::kWarning, 6, std::string(5000, 'z'));
    if (cut.size() != std::string("d3d warning id 6 (seen 1): ").size() + D3dMessageLog::kTextMax || cut.back() != 'z') return 50;
    // No more than kTotal lines in a run, whatever comes; the counts go on.
    D3dMessageLog many;
    uint32_t lines = 0;
    for (int32_t id = 0; id < 1000; ++id) lines += many.Add(D3dSeverity::kError, id, "e").empty() ? 0 : 1;
    if (lines != D3dMessageLog::kTotal || many.Summary() != "d3d messages: errors 1000, warnings 0") return 51;
    if (D3dMessageLog::kPerId != 4 || D3dMessageLog::kTotal != 400) return 52;
    // The line written when the cvar is first seen on: the queue (dxgidebug.dll answered) and the
    // layer itself (--d3d12_debug) are two things, and only both together report anything.
    if (D3dMessagesStartText(true, true) !=
        "d3d messages: on (fable2_native_d3d_messages); the queue is available; the debug layer is on (--d3d12_debug=true)") return 53;
    if (D3dMessagesStartText(true, false) !=
        "d3d messages: on (fable2_native_d3d_messages); the queue is available; the debug layer is OFF "
        "(--d3d12_debug=true is not set: nothing will be reported)") return 54;
    if (D3dMessagesStartText(false, true) !=
        "d3d messages: on (fable2_native_d3d_messages); the queue is NOT available (dxgidebug.dll gave no information queue: "
        "nothing will be reported); the debug layer is on (--d3d12_debug=true)") return 55;
    if (D3dMessagesStartText(false, false).find("NOT available") == std::string::npos ||
        D3dMessagesStartText(false, false).find("is OFF") == std::string::npos) return 56;
  }

  {
    // The scan-out gamma ramp (frame-map section 17): 256 entries of 10 bits a channel as the SDK
    // holds them, to floats; valid only when every entry is at most 1023 and no channel decreases.
    static uint32_t in[256][3];
    for (uint32_t i = 0; i < 256; ++i) {
      in[i][0] = i * 4;          // red: 0 .. 1020
      in[i][1] = i < 255 ? i * 4 : 1023;
      in[i][2] = i / 2;          // blue: steps, never down
    }
    PostRamp r = MakePostRamp(in);
    if (!r.valid || r.rgb[0][0] != 0.0f || r.rgb[255][1] != 1.0f || r.rgb[1][0] != 4.0f / 1023.0f ||
        r.rgb[255][2] != 127.0f / 1023.0f || r.rgb[3][2] != r.rgb[2][2]) return 60;
    // A constant ramp is in order (nothing decreases).
    static uint32_t flat[256][3] = {};
    if (!MakePostRamp(flat).valid) return 61;
    // One entry above 10 bits: not valid, and nothing of it is handed on.
    in[200][1] = 1024;
    r = MakePostRamp(in);
    if (r.valid || r.rgb[10][0] != 0.0f || r.rgb[255][1] != 0.0f) return 62;
    in[200][1] = 800;
    if (!MakePostRamp(in).valid) return 63;
    // One channel decreasing once, at the last entry as anywhere: not valid.
    in[255][2] = 126;
    if (MakePostRamp(in).valid) return 64;
    in[255][2] = 127;
    in[1][0] = 0; in[0][0] = 1;
    if (MakePostRamp(in).valid) return 65;
    in[0][0] = 0; in[1][0] = 4;
    if (!MakePostRamp(in).valid) return 66;
    // The log's text: 256 entries of nine hex digits, rrrgggbbb; a value that does not fit three digits is FFF.
    const std::string text = FormatRampHex(in);
    if (text.size() != 256 * 9 || text.substr(0, 9) != "000000000" || text.substr(9, 9) != "004004000" ||
        text.substr(255 * 9, 9) != "3FC3FF07F") return 67;
    in[2][2] = 0x12345;
    if (FormatRampHex(in).substr(18, 9) != "008008FFF") return 68;
    // Whether two reads differ (the change count of the log).
    static uint32_t other[256][3];
    for (uint32_t i = 0; i < 256; ++i) for (int c = 0; c < 3; ++c) other[i][c] = in[i][c];
    if (!SameRamp(in, other)) return 69;
    other[255][2] ^= 1;
    if (SameRamp(in, other)) return 70;
    // The log has the table in full at the first read and at the first kRampFullLogs - 1 changes;
    // later changes are counted only (a fade done with the ramp changes it every frame).
    if (kRampFullLogs != 16) return 71;
    if (!RampLoggedInFull(0) || !RampLoggedInFull(15) || RampLoggedInFull(16) || RampLoggedInFull(~0ull)) return 72;
  }

  std::cout << "PASS: view parsing/cycling, F6 edge detection, failure latch, frame times per view, d3d message log, scan-out ramp\n";
  return 0;
}
