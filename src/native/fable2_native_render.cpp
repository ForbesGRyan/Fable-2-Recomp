#include "fable2_native_render.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/graphics/native_guest_renderer.h>
#include <rex/graphics/native_rhi.h>
#include <rex/logging.h>

#include "capture.h"
#include "clay_pass.h"
#include "composite.h"
#include "d3d_messages.h"
#include "fable2_native_shaders.h"
#include "native_render_state.h"
#include "post_oracle.h"

REXCVAR_DEFINE_BOOL(fable2_native_render, false, "Fable2",
                    "Register the native guest-output renderer at startup (restart "
                    "required). Seeded from fable2_config.toml [native] enabled.");
REXCVAR_DEFINE_STRING(fable2_native_view, "off", "Fable2",
                      "Native debug view: off, overlay, split, native, pattern (F6 cycles those), or shadow "
                      "(the clay pass's two shadow maps as grey depth and its screen mask; by name only; a frame "
                      "without maps or mask, as every frame with the shadows off, shows flat panels; until the first "
                      "frame drawn with shadows, and when their objects could not be made, it shows the native picture).");
REXCVAR_DEFINE_STRING(fable2_native_clay_color, "clay", "Fable2",
                      "Native clay pass color: clay = one clay color; draw = a color per "
                      "draw; shader = a color per vertex shader. (With the fog on, a flat draw whose "
                      "shader the game fogs shows its color through the haze; fable2_native_fog=off "
                      "gives the plain colors.)");
REXCVAR_DEFINE_INT32(fable2_native_geometry_budget_mb, 256, "Fable2",
                     "GPU memory budget (MB) for decoded native geometry; least recently "
                     "used buffers are evicted above it.");
REXCVAR_DEFINE_BOOL(fable2_native_textures, true, "Fable2",
                    "Native clay pass: draw albedo textures on draws whose material was "
                    "resolved (false = flat clay).");
REXCVAR_DEFINE_INT32(fable2_native_texture_upload_mb, 8, "Fable2",
                     "Guest texture bytes (MB) decoded and uploaded per frame by the native "
                     "clay pass; draws over it stay clay this frame (texture-pending).");
REXCVAR_DEFINE_INT32(fable2_native_texture_budget_mb, 512, "Fable2",
                     "GPU memory budget (MB) for native albedo textures; least recently used "
                     "textures are evicted above it.");
REXCVAR_DEFINE_STRING(fable2_native_light, "on", "Fable2",
                      "Native clay pass light: on (or true, 1) = terrain, objects, tree trunks, leaves and plants are lit by the "
                      "game's ambient, baked and sun light (the sun shadowed when fable2_native_shadows is on; one fixed exposure "
                      "on the lit draws, fitted on the bridge's dirt patch; unlit draws are shown as with the light off); "
                      "off (or false, 0) = the unlit materials picture, without shadows.");
REXCVAR_DEFINE_STRING(fable2_native_shadows, "on", "Fable2",
                      "Native sun shadows: on (or true, 1) = with a composite view and the light on, the capture also records the "
                      "game's two shadow passes and the frame's shadow block, and the clay pass draws its two shadow "
                      "maps from them, a depth pre-pass and the game's screen mask, and shadows the sun of its lit draws "
                      "(fable2_native_view=shadow shows the maps and the mask); off (or false, 0) = none of it is read, made or "
                      "drawn: the lighting picture.");
REXCVAR_DEFINE_STRING(fable2_native_fog, "on", "Fable2",
                      "Native fog and sky: on (or true, 1) = with a composite view and the light on, the capture also reads the "
                      "frame's fog block (the game's six fog registers and its two fog tables) and its sky block at the game's "
                      "own draws, and the clay pass fogs its draws with the game's arithmetic and tables wherever the draw's "
                      "own pixel shader in the game has fog (lit draws by their family's form, unlit draws by the same form on "
                      "their unlit colour, trees by their vertex shader's form; a draw whose game shader has none is left "
                      "unfogged), and draws the game's sky where no record is drawn; off (or false, 0) = none of it is read or "
                      "drawn: the shadows' picture. On by default since the fog's validation (sub-project 9; frame map "
                      "section 16).");
REXCVAR_DEFINE_STRING(fable2_native_post, "off", "Fable2",
                      "Native post chain (sub-project 10; default off, on recommended: frame map section 17, \"Cost: the two timing "
                      "sessions\"): on (or true, 1) = with a composite view and the light on, "
                      "the capture also reads the frame's post block at the game's own post rectangles (the registers of its "
                      "composite, bright pass and blurs and the composite's 512-texel exposure table), and the clay pass draws "
                      "a frame that has one through the game's own composite and the scan-out gamma ramp: its lit draws write "
                      "the game's output scale in place of the fixed exposure, its unlit draws one scale of the frame, and the "
                      "clear colour comes out nearly black (the bloom is fable2_native_bloom); a frame without a block, a ramp or light is drawn "
                      "as with the switch off; off (or false, 0; the default) = none of it is read or drawn: the fog's "
                      "picture, the fixed exposure and the square-root mapping.");
REXCVAR_DEFINE_STRING(fable2_native_bloom, "off", "Fable2",
                      "Native bloom (sub-project 10, phase 10b; default off, on recommended with the post; needs "
                      "fable2_native_post=on): on (or true, 1) = a post "
                      "frame whose block carries the game's blur weights also draws the game's bloom between its scene and its "
                      "composite (the 4 x 4 downsample, the bright pass and the two 13-tap blurs, in targets a quarter of the "
                      "native target's logical size), and the composite screens it over the picture; off (or false, 0; the "
                      "default) = none of it is made or drawn: the picture of fable2_native_post alone. Frame map section 17.");
REXCVAR_DEFINE_STRING(fable2_native_sun, "off", "Fable2",
                      "Native sky furniture and sun layer (sub-project 10, phase 10c; needs fable2_native_post=on, and "
                      "fable2_native_bloom=on for the sun layer): on (or true, 1) = the capture also reads the frame's quad "
                      "records (the game's moon, cloud, glare, beam-source, flare, moon-glow, sun-disc and cloud-occluder quads "
                      "at the indexed builder's calls, at most 24 a frame), the radial blur's registers and the place of the "
                      "game's late records, and a post frame with a sky draws the moon, the clouds and the glare in its scene, "
                      "the game's half-size sun layer (beams, flares, moon glow, sun disc) into the bloom's bright pass, and "
                      "mixes a tenth of the previous frame's picture as the game does; off (or false, 0; the default) = none "
                      "of it is read or drawn: the picture of fable2_native_post and fable2_native_bloom alone. Off by default "
                      "because towards the sun the native picture is not yet the game's (the native scene is too dark there): "
                      "frame map section 17, \"Validation (10c)\".");
REXCVAR_DEFINE_STRING(fable2_native_wind, "off", "Fable2",
                      "Native wind (sub-project 11, phase 11a; not yet validated): on (or true, 1) = with a composite view "
                      "the capture also reads the wind inputs of the draws whose vertex shader sways (trunks, leaves, "
                      "instanced plants and their shadow casters: three to five vertex registers a draw, the shader's "
                      "literal table once a frame, a tree's vertex elements) into a side array of the frame, and the native "
                      "view draws those records moved as the game's vertex shader moves them, in colour, depth and shadow "
                      "(F3: the Wind line); off (or false, 0; the default) = none of it is read, made or drawn. Frame map "
                      "section 18.");
REXCVAR_DEFINE_STRING(fable2_native_scene_dump, "off", "Fable2",
                      "Diagnostic of the native renderer (frame map section 18, \"Stored native scene\"): on (or true, 1) = "
                      "each discovery capture made while the native picture is drawn also writes the native scene of its "
                      "first frame before post to logs/native_scene_<stamp>.f2ns (the linear target, the scene's depth and, "
                      "with the sun layer drawn, the layer's depth; about 15 MB a file, at most 8 a run), for "
                      "tools/xdk_sigmatch to lay beside the game's scene of the same frame; off (or false, 0; the default) = "
                      "nothing of it is made, read or drawn.");
REXCVAR_DEFINE_STRING(fable2_exposure_readback, "off", "Fable2",
                      "The game's exposure in a normal run: on (or true, 1) = the game's luminance resolve (two buffers in turn, "
                      "0x1A385000 and 0x1A34F000) is added to readback_resolve_force_addresses at startup, so that the game "
                      "measures its own picture and adapts its exposure as under --readback_resolve=full (the emulated picture's "
                      "brightness and contrast change with it; one wait for the GPU a frame); off (or false, 0) = not read back: "
                      "the game stays at its blind exposure (the default: frame map section 17, \"Cost: the two timing sessions\"). "
                      "Read once at startup, with the native renderer on or off; a "
                      "--readback_resolve_force_addresses given by hand wins.");
REXCVAR_DEFINE_STRING(fable2_native_post_oracle, "", "Fable2",
                      "Diagnostic of the native post chain (frame map section 17, \"Oracle feed\"): a path prefix. Empty (the "
                      "default): nothing. Set, with fable2_native_post=on: <prefix>.scene and <prefix>.block (written by "
                      "tools/xdk_sigmatch/post_replay.py export from a read-back capture) are read once at startup; if both are "
                      "valid, the post steps of every post frame run on that stored scene with its stored block and the live "
                      "ramp in place of the frame's own (the native view then shows that one still picture), and 30 such frames "
                      "after the first the mapped target is written once to logs/native_post_dump_<stamp>.rgba for "
                      "post_replay.py compare. A file that is missing or refused: one log line, and nothing changes.");
REXCVAR_DEFINE_BOOL(fable2_native_d3d_messages, false, "Fable2",
                    "Write the Direct3D debug layer's errors, corruption and warnings to the game log "
                    "(the layer is the SDK's --d3d12_debug=true; without it there is nothing to write). "
                    "For diagnosing a rejected call or a lost device; off by default.");

namespace fable2::native {
namespace nrhi = rex::graphics::nrhi;
using rex::graphics::NativeGuestOutputBackend;
using rex::graphics::NativeGuestOutputRenderContext;

namespace {

constexpr int kToggleVk = 0x75;  // VK_F6

struct Resources {
  nrhi::Device* device = nullptr;
  nrhi::Format format = nrhi::Format::kUnknown;
  nrhi::BindingLayout* layout = nullptr;
  nrhi::Shader* vs = nullptr;
  nrhi::Shader* pattern_ps = nullptr;
  nrhi::Pipeline* pattern = nullptr;
};

std::atomic<bool> g_installed{false};
// The parsed fable2_native_view. The string cvar is read and written only on
// the game thread (Install, F6 in PollFrame); the GPU and UI threads read this.
std::atomic<View> g_view{View::kOff};
// SetView (autoplay thread) leaves the requested view here; PollFrame applies it on the
// game thread like an F6 press. -1 = none.
std::atomic<int> g_pending_view{-1};
// Number of view changes applied, and its value at the last [native] clay: line.
std::atomic<uint32_t> g_view_changes{0};
uint32_t g_view_changes_logged = 0;  // command-processor thread only
Resources g_res;  // command-processor thread only
FailureLatch g_latch;
EdgeDetector g_toggle;
std::atomic<uint32_t> g_frame{0};
// ClayPass::shadows_usable as of the last clay frame (written on the command-processor thread, read
// by the game thread for the capture's switch): false once the pass's shadow objects have failed.
std::atomic<bool> g_shadows_usable{true};

// Clay pass state: command-processor thread only.
render::Composite g_composite;
std::unique_ptr<render::ClayPass> g_clay;  // created on first use (cvars parsed)
std::shared_ptr<const render::FrameScene> g_clay_scene;  // last scene rendered
uint64_t g_clay_targets = 0;  // targets generation g_clay_scene was drawn into
uint64_t g_clay_frames = 0;
// A presented frame went by without a clay Render (the view was off or not usable, or no scene was there):
// the next Render is told, so that the previous-frame blend does not mix a picture from before the gap.
bool g_clay_gap = true;
// The guest frame number of the last scene the pass rendered, and whether there was one: a scene that is
// not the next few after it starts from a gap as well (render::ClaySceneGap).
uint64_t g_clay_scene_frame = 0;
bool g_clay_scene_rendered = false;
double g_clay_window_max_ms = 0;  // worst hash+decode+record over the log window
// Texture upload burst (frames with uploads or pending textures in a row),
// logged so area loads show how uploads spread over frames.
struct UploadBurst {
  uint64_t frames = 0, uploads = 0, bytes = 0;
  std::chrono::steady_clock::time_point start;
};
UploadBurst g_burst;
// The debug layer's messages (fable2_native_d3d_messages). Read on the command-processor thread
// with each composite frame, and once more where a frame may not come again: when the renderer
// latches a failure (which is how a removed device shows here: the SDK hands this module no
// removal callback) and at shutdown (the game thread). The mutex is for those two.
std::mutex g_d3d_messages_mutex;
D3dMessageLog g_d3d_messages;
bool g_d3d_messages_started = false;
uint64_t g_d3d_messages_logged = 0;  // g_clay_frames at the last count line
void PollD3dMessagesOnce(const char* when);

// The scan-out gamma ramp (SetGammaRampReader; post discovery, frame-map section 17). The reader
// is set before Install registers the callbacks; the rest is the command-processor thread's.
std::atomic<bool (*)(void*, uint32_t[256][3], uint32_t*)> g_ramp_reader{nullptr};
std::atomic<void*> g_ramp_user{nullptr};
struct RampLog {
  uint32_t last[256][3] = {};
  bool have_last = false;
  uint64_t reads = 0, failed = 0, changes = 0;
  uint64_t not_table = 0;  // reads at a front buffer whose format takes the SDK's other ramp (RampTableApplies)
  uint64_t full_lines = 0;  // tables logged in full (RampLoggedInFull)
  bool last_valid = false;  // `last` passed MakePostRamp's rule (g_post_ramp holds its floats)
  bool said_none = false;
};
RampLog g_ramp;
// The ramp of this composite frame as the clay pass takes it (ReadRamp, before RenderClay): the table
// the emulated picture of the frame went through, as floats, valid only when it passed its rule
// (MakePostRamp); invalid when no reader is registered or the reader gave none.
PostRamp g_post_ramp;

// The oracle feed (fable2_native_post_oracle; post_oracle.h). Read and parsed once in Install, before
// the callbacks are registered, and never changed after: the command-processor thread reads it.
// g_oracle_feed.scene points into g_oracle_scene_bytes. g_oracle_valid: both files were taken.
std::vector<uint8_t> g_oracle_scene_bytes;
std::vector<uint8_t> g_oracle_sun_bytes;  // (the optional third file, <prefix>.sun: g_oracle_feed.sun.depth points into it)
render::OracleFeed g_oracle_feed;
bool g_oracle_valid = false;
bool g_oracle_dump_written = false;  // command-processor thread only
// The stored native scene (fable2_native_scene_dump): the capture's first scene frame a dump was last
// asked for (0: none yet), so that each capture asks once. Command-processor thread only.
uint64_t g_scene_dump_asked = 0;

// F3 status text, written on the command-processor thread, read by the UI.
std::mutex g_status_mutex;
std::string g_status;

void SetStatus(std::string text) {
  std::lock_guard<std::mutex> lock(g_status_mutex);
  g_status = std::move(text);
}

// Game thread only: parses the string cvar.
View ParseViewCvar() {
  const std::string text = REXCVAR_GET(fable2_native_view);
  const ParsedView parsed = ParseView(text);
  if (!parsed.recognized) {
    REXLOG_WARN("[native] unknown fable2_native_view '{}', using off", text);
  }
  return parsed.view;
}

// Any thread.
View CurrentView() { return g_view.load(std::memory_order_relaxed); }

void Fail(const char* what) {
  if (g_latch.Fail()) {
    REXLOG_ERROR("[native] {} failed; falling back to emulated output (F6 retries)", what);
    SetStatus(std::string("Native: ") + what + " failed (F6 retries)");
    PollD3dMessagesOnce("at the failure");  // (what the layer said of it; no composite frame may follow)
  }
}

uint64_t GeometryBudgetBytes() {
  return uint64_t(std::max<int32_t>(16, REXCVAR_GET(fable2_native_geometry_budget_mb))) << 20;
}

uint64_t TextureBudgetBytes() {
  return uint64_t(std::max<int32_t>(16, REXCVAR_GET(fable2_native_texture_budget_mb))) << 20;
}

uint64_t TextureUploadBytes() {
  return uint64_t(std::max<int32_t>(1, REXCVAR_GET(fable2_native_texture_upload_mb))) << 20;
}

render::ClayColor CurrentClayColor() {
  static std::atomic<bool> warned{false};
  bool recognized = true;
  const render::ClayColor color =
      render::ParseClayColor(REXCVAR_GET(fable2_native_clay_color), &recognized);
  if (!recognized && !warned.exchange(true)) {
    REXLOG_WARN("[native] unknown fable2_native_clay_color '{}', using clay",
                REXCVAR_GET(fable2_native_clay_color));
  }
  return color;
}

render::ClayLight CurrentClayLight() {
  static std::atomic<bool> warned{false};
  bool recognized = true;
  const render::ClayLight light = render::ParseClayLight(REXCVAR_GET(fable2_native_light), &recognized);
  if (!recognized && !warned.exchange(true)) {
    REXLOG_WARN("[native] unknown fable2_native_light '{}', using off (accepted: {})", REXCVAR_GET(fable2_native_light),
                render::kClaySwitchWords);
  }
  return light;
}

// fable2_native_shadows: read like the light's switch (the same words, kClaySwitchWords, so its
// parser is used); anything else is off, with one warning that names the words.
bool CurrentShadows() {
  static std::atomic<bool> warned{false};
  bool recognized = true;
  const bool on = render::ParseClayLight(REXCVAR_GET(fable2_native_shadows), &recognized) == render::ClayLight::kOn;
  if (!recognized && !warned.exchange(true)) {
    REXLOG_WARN("[native] unknown fable2_native_shadows '{}', using off (accepted: {})", REXCVAR_GET(fable2_native_shadows),
                render::kClaySwitchWords);
  }
  return on;
}

// Whether the capture takes the game's shadow passes: only for a frame that could be drawn with
// them, which is a lit one (the light on, and textures, without which nothing is lit) of a pass
// whose shadow objects have not failed (ShadowRecordsWanted).
bool ShadowsWanted(bool records_wanted) {
  return ShadowRecordsWanted(records_wanted, CurrentShadows(), REXCVAR_GET(fable2_native_textures),
                             CurrentClayLight() == render::ClayLight::kOn, g_shadows_usable.load(std::memory_order_relaxed));
}

// fable2_native_fog: read like the shadows' switch (the same parser and words); anything else is
// off, with one warning that names the words.
bool CurrentFog() {
  static std::atomic<bool> warned{false};
  bool recognized = true;
  const bool on = render::ParseClayLight(REXCVAR_GET(fable2_native_fog), &recognized) == render::ClayLight::kOn;
  if (!recognized && !warned.exchange(true)) {
    REXLOG_WARN("[native] unknown fable2_native_fog '{}', using off (accepted: {})", REXCVAR_GET(fable2_native_fog),
                render::kClaySwitchWords);
  }
  return on;
}

// Whether the capture reads the frame's fog and sky blocks: only for a frame that could be drawn
// with them, which is a lit one (FogRecordsWanted).
bool FogWanted(bool records_wanted) {
  return FogRecordsWanted(records_wanted, CurrentFog(), REXCVAR_GET(fable2_native_textures),
                          CurrentClayLight() == render::ClayLight::kOn);
}

// fable2_native_post: read like the shadows' switch (the same parser and words); anything else is
// off, with one warning that names the words.
bool CurrentPost() {
  static std::atomic<bool> warned{false};
  bool recognized = true;
  const bool on = render::ParseClayLight(REXCVAR_GET(fable2_native_post), &recognized) == render::ClayLight::kOn;
  if (!recognized && !warned.exchange(true)) {
    REXLOG_WARN("[native] unknown fable2_native_post '{}', using off (accepted: {})", REXCVAR_GET(fable2_native_post),
                render::kClaySwitchWords);
  }
  return on;
}

// fable2_native_bloom: read like the post's switch; anything else is off, with one warning that names
// the words. It acts only in a post frame (ClayBloom): nothing of the capture depends on it (the
// blurs' weights are part of the post block, read with fable2_native_post).
bool CurrentBloom() {
  static std::atomic<bool> warned{false};
  bool recognized = true;
  const bool on = render::ParseClayLight(REXCVAR_GET(fable2_native_bloom), &recognized) == render::ClayLight::kOn;
  if (!recognized && !warned.exchange(true)) {
    REXLOG_WARN("[native] unknown fable2_native_bloom '{}', using off (accepted: {})", REXCVAR_GET(fable2_native_bloom),
                render::kClaySwitchWords);
  }
  return on;
}

// Whether the capture reads the frame's post block: only for a frame that could be drawn through
// the game's composite, which is a lit one (PostRecordsWanted).
bool PostWanted(bool records_wanted) {
  return PostRecordsWanted(records_wanted, CurrentPost(), REXCVAR_GET(fable2_native_textures),
                           CurrentClayLight() == render::ClayLight::kOn);
}

// fable2_native_sun: read like the post's switch; anything else is off, with one warning that names
// the words. The capture reads the quad records and the radial blur's registers with it
// (SunRecordsWanted).
bool CurrentSun() {
  static std::atomic<bool> warned{false};
  bool recognized = true;
  const bool on = render::ParseClayLight(REXCVAR_GET(fable2_native_sun), &recognized) == render::ClayLight::kOn;
  if (!recognized && !warned.exchange(true)) {
    REXLOG_WARN("[native] unknown fable2_native_sun '{}', using off (accepted: {})", REXCVAR_GET(fable2_native_sun),
                render::kClaySwitchWords);
  }
  return on;
}

// fable2_native_wind: read like the sun's switch; anything else is off, with one warning that names
// the words. The capture reads the draws' wind inputs with it, in a frame with records.
bool CurrentWind() {
  static std::atomic<bool> warned{false};
  bool recognized = true;
  const bool on = render::ParseClayLight(REXCVAR_GET(fable2_native_wind), &recognized) == render::ClayLight::kOn;
  if (!recognized && !warned.exchange(true)) {
    REXLOG_WARN("[native] unknown fable2_native_wind '{}', using off (accepted: {})", REXCVAR_GET(fable2_native_wind),
                render::kClaySwitchWords);
  }
  return on;
}

// fable2_native_scene_dump (a diagnostic): read like the sun's switch; anything else is off, with one
// warning that names the words.
bool CurrentSceneDump() {
  static std::atomic<bool> warned{false};
  bool recognized = true;
  const bool on = render::ParseClayLight(REXCVAR_GET(fable2_native_scene_dump), &recognized) == render::ClayLight::kOn;
  if (!recognized && !warned.exchange(true)) {
    REXLOG_WARN("[native] unknown fable2_native_scene_dump '{}', using off (accepted: {})", REXCVAR_GET(fable2_native_scene_dump),
                render::kClaySwitchWords);
  }
  return on;
}

// fable2_native_post_oracle: the two files of the prefix, read whole and parsed (game thread, once).
// Anything missing or refused leaves the feed off, with one line that names the file and the fault.
void LoadOracle() {
  const std::string prefix = REXCVAR_GET(fable2_native_post_oracle);
  if (prefix.empty()) return;
  // (null: the file was read whole; otherwise what failed. Review of plan Task 6, Minor 5: the line
  // names the file and the cause.)
  auto read = [](const std::string& path, std::vector<uint8_t>* out) -> const char* {
    out->clear();
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return "could not be opened";
    // (bounded: a scene file is 9.7 MB; nothing larger than 64 MB is read)
    constexpr size_t kMaxBytes = size_t(64) << 20;
    uint8_t buf[65536];
    const char* why = nullptr;
    for (size_t n; (n = std::fread(buf, 1, sizeof(buf), fp)) > 0;) {
      if (out->size() + n > kMaxBytes) {
        why = "is larger than 64 MB";
        break;
      }
      out->insert(out->end(), buf, buf + n);
    }
    if (!why && std::ferror(fp)) why = "could not be read to its end";
    std::fclose(fp);
    if (why) out->clear();
    return why;
  };
  std::vector<uint8_t> block_bytes;
  for (const char* suffix : {".scene", ".block"}) {
    const std::string path = prefix + suffix;
    if (const char* why = read(path, suffix[1] == 's' ? &g_oracle_scene_bytes : &block_bytes)) {
      REXLOG_WARN("[native] post oracle: {} {}; the feed is off", path, why);
      g_oracle_scene_bytes = {};
      return;
    }
  }
  render::OracleFault scene_why = render::OracleFault::kNone, block_why = render::OracleFault::kNone;
  capture::PostBlockReason rule = capture::PostBlockReason::kNone;
  render::OracleFeed feed;
  feed.scene = render::ParseOracleScene(g_oracle_scene_bytes.data(), g_oracle_scene_bytes.size(), &scene_why);
  feed.block = render::ParseOracleBlock(block_bytes.data(), block_bytes.size(), &feed.c20w, &block_why, &rule);
  if (!feed.scene.valid || !feed.block.valid) {
    REXLOG_WARN("[native] post oracle: {}: scene {} ({} bytes), block {}{}{} ({} bytes); the feed is off", prefix,
                feed.scene.valid ? "valid" : render::OracleFaultName(scene_why), g_oracle_scene_bytes.size(),
                feed.block.valid ? "valid" : render::OracleFaultName(block_why),
                block_why == render::OracleFault::kBlock ? ": " : "",
                block_why == render::OracleFault::kBlock ? capture::PostBlockReasonName(rule) : "", block_bytes.size());
    g_oracle_scene_bytes = {};
    return;
  }
  // The optional third file, <prefix>.sun (plan Task 14: the sun layer on the feed): the scene's depth and
  // the layer's quads. Missing: the feed has no sun layer and says so. Present and refused: the same, with
  // the fault's word; the scene and the block are fed as without it.
  std::string sun_text = "no sun file";
  if (const char* why = read(prefix + ".sun", &g_oracle_sun_bytes)) {
    sun_text = std::string("sun file ") + why + ": the feed has no sun layer";
  } else {
    render::OracleFault sun_why = render::OracleFault::kNone;
    capture::QuadRecordReason quad_why = capture::QuadRecordReason::kNone;
    feed.sun = render::ParseOracleSun(g_oracle_sun_bytes.data(), g_oracle_sun_bytes.size(), &sun_why, &quad_why);
    if (feed.sun.valid) {
      sun_text = "sun file valid: the scene's depth and " + std::to_string(feed.sun.count) +
                 " layer quads (with fable2_native_sun=on the layer is drawn from them, each quad with the texture of the "
                 "live frame's quad of its kind, and layer 1, tf1 and tf2 are written beside the dump)";
    } else {
      sun_text = std::string("sun file refused (") + render::OracleFaultName(sun_why) +
                 (sun_why == render::OracleFault::kQuad ? std::string(": ") + capture::QuadRecordReasonName(quad_why) : std::string()) +
                 "): the feed has no sun layer";
      g_oracle_sun_bytes = {};
    }
  }
  g_oracle_feed = feed;
  g_oracle_valid = true;
  REXLOG_INFO("[native] post oracle: {}: scene {} x {} ({} values a half-float texture changes), block c20.w {:.5f}, parts {}; "
              "with fable2_native_post=on the post steps of every post frame read this scene and block, and the mapped target is "
              "written once to logs/native_post_dump_<stamp>.rgba; {}",
              prefix, feed.scene.width, feed.scene.height, render::OracleSceneHalfChanged(feed.scene), feed.c20w,
              unsigned(feed.block.parts), sun_text);
}

// The stored native scene's file, once the copies of a requested dump have run:
// logs/native_scene_<stamp>.f2ns (the pass's TakeSceneDump gives the file's bytes once a dump). The
// stamp is the time of writing; a file is paired with its capture by the frame number in its head.
// Command-processor thread. Not reached without the switch.
void WriteSceneDump(nrhi::Device* device) {
  if (!g_clay) return;
  std::vector<uint8_t> file;
  if (!g_clay->TakeSceneDump(device, &file)) return;
  char name[64];
  const std::time_t t = std::time(nullptr);
  std::strftime(name, sizeof(name), "native_scene_%Y%m%d_%H%M%S.f2ns", std::localtime(&t));
  const std::filesystem::path dir = rex::filesystem::GetExecutableFolder() / "logs";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  // (the wide path, as for the oracle feed's dump: a folder outside the ANSI code page must not throw here)
  const std::filesystem::path full = dir / name;
  std::FILE* fp = _wfopen(full.c_str(), L"wb");
  const bool ok = fp && std::fwrite(file.data(), 1, file.size(), fp) == file.size();
  if (fp) std::fclose(fp);
  const render::SceneDumpHeader& head = g_clay->scene_dump_head();
  if (ok) {
    REXLOG_INFO("[native] scene dump: frame {} (asked {}), flags {}, bad values {} -> logs/{} ({} bytes, {} x {}, exposure {:.5f}; "
                "file {} of at most {} this run)", head.scene_frame, head.asked_frame, head.flags, head.bad_values, name, file.size(),
                head.width, head.height, head.exposure, g_clay->scene_dump_counts().written, render::kSceneDumpMaxFiles);
  } else {
    REXLOG_ERROR("[native] scene dump: refused (write): logs/{} could not be written (frame {}, {} bytes)", name, head.scene_frame,
                 file.size());
  }
}

// The oracle feed's dump, once its copy has run: logs/native_post_dump_<stamp>.rgba (the pass's
// TakePostOracleDump gives the file's bytes once). Command-processor thread.
void WriteOracleDump(nrhi::Device* device) {
  if (!g_oracle_valid || g_oracle_dump_written || !g_clay) return;
  std::vector<uint8_t> file;
  if (!g_clay->TakePostOracleDump(device, &file)) {
    // (a dump that failed, its buffer not made or not read, is over too: nothing more is asked.
    // Review of plan Task 6, Minor 8.)
    if (g_clay->PostOracleDumpDone()) g_oracle_dump_written = true;
    return;
  }
  g_oracle_dump_written = true;
  char name[64];
  const std::time_t t = std::time(nullptr);
  std::strftime(name, sizeof(name), "native_post_dump_%Y%m%d_%H%M%S.rgba", std::localtime(&t));
  const std::filesystem::path dir = rex::filesystem::GetExecutableFolder() / "logs";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  // (the wide path: a folder outside the ANSI code page must not throw here, on the command-processor
  // thread; the name itself is ASCII. Review of plan Task 6, Minor 4.)
  const std::filesystem::path full = dir / name;
  std::FILE* fp = _wfopen(full.c_str(), L"wb");
  const bool ok = fp && std::fwrite(file.data(), 1, file.size(), fp) == file.size();
  if (fp) std::fclose(fp);
  if (ok) {
    REXLOG_INFO("[native] post oracle: wrote logs/{} ({} bytes: the mapped target, {} x {} RGBA) at clay frame {}", name, file.size(),
                render::ClayPass::kWidth, render::ClayPass::kHeight, g_clay_frames);
  } else {
    REXLOG_WARN("[native] post oracle: logs/{} could not be written", name);
  }
  // The sun layer's three pictures of the same frame, when the feed drew the layer (plan Task 14): layer 1
  // (its alpha says where the beam source was drawn), and the bright pass's tf1 and tf2, as the words of
  // R10G10B10A2_UNORM. Beside the dump, under its name.
  static constexpr const char* kLayerNames[render::kOracleLayerDumps] = {"layer1", "tf1", "tf2"};
  for (size_t i = 0; i < render::kOracleLayerDumps; ++i) {
    std::vector<uint8_t> layer;
    if (!g_clay->TakePostOracleLayerDump(device, i, &layer)) continue;
    std::string layer_name(name);
    layer_name = layer_name.substr(0, layer_name.size() - 5) + "_" + kLayerNames[i] + ".r10";
    const std::filesystem::path layer_full = dir / layer_name;
    std::FILE* lp = _wfopen(layer_full.c_str(), L"wb");
    const bool layer_ok = lp && std::fwrite(layer.data(), 1, layer.size(), lp) == layer.size();
    if (lp) std::fclose(lp);
    if (layer_ok) {
      REXLOG_INFO("[native] post oracle: wrote logs/{} ({} bytes: the sun layer's {}, R10G10B10A2 words)", layer_name, layer.size(),
                  kLayerNames[i]);
    } else {
      REXLOG_WARN("[native] post oracle: logs/{} could not be written", layer_name);
    }
  }
}

bool EnsureResources(nrhi::Device* device, nrhi::Format format) {
  if (g_res.device == device && g_res.format == format && g_res.pattern) {
    return true;
  }
  g_res = {};
  g_res.device = device;
  g_res.format = format;

  nrhi::BindingLayoutDesc layout_desc;
  layout_desc.params[0].kind = nrhi::BindingParamKind::kConstants;
  layout_desc.params[0].shader_register = 0;
  layout_desc.params[0].count = 4;
  layout_desc.params[0].visibility = nrhi::Visibility::kPixel;
  layout_desc.param_count = 1;
  layout_desc.allow_input_layout = false;
  g_res.layout = device->CreateBindingLayout(layout_desc);
  if (!g_res.layout) return Fail("binding layout"), false;

  auto make_shader = [&](nrhi::ShaderStage stage, const char* name, const char* src) {
    nrhi::ShaderDesc desc;
    desc.stage = stage;
    desc.name = name;
    desc.hlsl_source = src;
    desc.entry_point = "main";
    return device->CreateShader(desc);
  };
  g_res.vs = make_shader(nrhi::ShaderStage::kVertex, "fable2_fullscreen_vs", shaders::kFullscreenVs);
  g_res.pattern_ps = make_shader(nrhi::ShaderStage::kPixel, "fable2_pattern_ps", shaders::kPatternPs);
  if (!g_res.vs || !g_res.pattern_ps) return Fail("shader compile"), false;

  nrhi::GraphicsPipelineDesc pipe;
  pipe.layout = g_res.layout;
  pipe.vs = g_res.vs;
  pipe.rtv_format = format;
  pipe.ps = g_res.pattern_ps;
  g_res.pattern = device->CreateGraphicsPipeline(pipe);
  if (!g_res.pattern) return Fail("pipeline creation"), false;
  return true;
}

void DrawFullscreen(const NativeGuestOutputRenderContext& ctx, nrhi::Pipeline* pipeline) {
  nrhi::Cmd* cmd = ctx.cmd;
  nrhi::Texture* out = ctx.guest_output;
  const float w = float(ctx.guest_output_width);
  const float h = float(ctx.guest_output_height);
  cmd->Barrier(out, nrhi::ResourceState::kGuestOutput, nrhi::ResourceState::kRenderTarget);
  cmd->FlushBarriers();
  cmd->SetRenderTargets(out, nullptr);
  cmd->SetViewport({0.0f, 0.0f, w, h, 0.0f, 1.0f});
  cmd->SetScissor({0, 0, int32_t(w), int32_t(h)});
  cmd->SetBindingLayout(g_res.layout);
  cmd->SetPipeline(pipeline);
  const float params[4] = {float(g_frame.load(std::memory_order_relaxed)), w, h, 0.0f};
  cmd->SetRootConstants(0, 4, params, 0);
  cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);
  cmd->Draw(3, 0);
  cmd->Barrier(out, nrhi::ResourceState::kRenderTarget, nrhi::ResourceState::kGuestOutput);
  cmd->FlushBarriers();
}

bool Usable(const NativeGuestOutputRenderContext& ctx) {
  return !g_latch.IsFailed() && ctx.backend == NativeGuestOutputBackend::kD3D12 && ctx.device && ctx.cmd &&
         ctx.guest_output;
}

bool RenderCallback(const NativeGuestOutputRenderContext& ctx, void*) {
  if (!Usable(ctx) || CurrentView() != View::kPattern) return false;
  if (!EnsureResources(ctx.device, ctx.guest_output->format())) return false;
  DrawFullscreen(ctx, g_res.pattern);
  g_frame.fetch_add(1, std::memory_order_relaxed);
  return true;
}

// Clay pass into its own native target (the guest output is not touched;
// the composite draws it). Renders each published scene once. False on
// failure (latched).
bool RenderClay(const NativeGuestOutputRenderContext& ctx) {
  std::shared_ptr<const render::FrameScene> scene = capture::Publisher().Latest();
  if (!scene) {
    // No records yet (just switched from a view without a consumer): show the
    // emulated frame rather than a stale clay target (or stale F3 stats).
    if (g_clay_scene) SetStatus({});
    g_clay_scene = nullptr;
    g_clay_gap = true;
    return true;
  }
  if (!g_clay) {
    g_clay = std::make_unique<render::ClayPass>(GeometryBudgetBytes(), TextureBudgetBytes());
  }
  // Read once per frame: picks the draw path.
  const bool textures = REXCVAR_GET(fable2_native_textures);
  if (!g_clay->Ensure(ctx.device)) {
    Fail("clay pass");
    return false;
  }
  // (before the unchanged-scene return below: a rebuilt pass tries its shadows anew, and the capture
  // must record their passes again for that)
  g_shadows_usable.store(g_clay->shadows_usable(), std::memory_order_relaxed);
  // New targets (F6 retry, device change) start undefined: redraw even if
  // the scene has not changed.
  if (scene == g_clay_scene && g_clay->targets_generation() == g_clay_targets) return true;
  g_clay_scene = scene;
  g_clay_targets = g_clay->targets_generation();
  ++g_clay_frames;
  g_clay->geometry().BeginFrame(g_clay_frames, GeometryBudgetBytes());
  g_clay->textures().BeginFrame(g_clay_frames, TextureBudgetBytes(), TextureUploadBytes());
  render::ClayStats st;
  render::TextureStats tst;
  // The post chain: the switch as it is now and this frame's ramp (null without a valid one: the frame
  // is then drawn without post, "post-ramp"), the bloom's switch and the sun's (the scene's furniture and
  // the sun layer).
  render::ClayPostRequest post;
  post.post = CurrentPost();
  post.bloom = CurrentBloom();
  post.sun = CurrentSun();
  post.ramp = g_post_ramp.valid ? g_post_ramp.rgb : nullptr;
  post.gap = g_clay_gap || render::ClaySceneGap(g_clay_scene_rendered, g_clay_scene_frame, scene->frame);
  g_clay_gap = false;
  g_clay_scene_frame = scene->frame;
  g_clay_scene_rendered = true;
  // (the oracle feed: null unless its switch is set and both files were valid)
  g_clay->SetPostOracle(g_oracle_valid ? &g_oracle_feed : nullptr);
  // The stored native scene (a diagnostic; nothing of it without its switch): once for each discovery
  // capture, when the scene of its first frame is known, the pass is asked for a dump of that scene. The
  // capture stores the number before it publishes the scene, so the Render of that very scene, below,
  // already has the request; a Render that got a later scene dumps that one and says so.
  if (CurrentSceneDump()) {
    const uint64_t first = capture::DiscoveryFirstSceneFrame();
    if (render::SceneDumpNewRequest(first, g_scene_dump_asked)) {
      g_scene_dump_asked = first;
      g_clay->RequestSceneDump(first);
    }
  }
  g_clay->Render(ctx.cmd, ctx.device, *scene, CurrentClayColor(), textures, CurrentClayLight(), CurrentShadows(), CurrentFog(), CurrentWind(), post,
                 st, tst);
  g_shadows_usable.store(g_clay->shadows_usable(), std::memory_order_relaxed);
  SetStatus(render::FormatStatusText(*scene, st, textures ? &tst : nullptr) + "\n" +
            render::FormatLightDrawnText(st) + "\n" + render::FormatShadowText(st) + "\n" + render::FormatFogText(st) + "\n" +
            render::FormatSkyText(st) + "\n" + render::FormatPostText(st) + "\n" + render::FormatFurnitureText(st) + "\n" + render::FormatSunText(st) + "\n" + render::FormatCloudText(st) + "\n" + render::FormatWindText(st));
  const double cpu_ms = st.hash_ms + st.decode_ms + st.record_ms + tst.decode_ms;
  const uint32_t pending = tst.status[size_t(capture::MaterialStatus::kTexturePending)];
  if (textures && (tst.uploads || pending)) {
    if (g_burst.frames == 0) g_burst.start = std::chrono::steady_clock::now();
    ++g_burst.frames;
    g_burst.uploads += tst.uploads;
    g_burst.bytes += tst.upload_bytes;
    if (g_burst.frames <= 8 || g_burst.frames % 30 == 0) {
      REXLOG_INFO("[native] textures: burst frame {}: uploads {} ({:.1f} MB), pending {}",
                  g_burst.frames, tst.uploads, double(tst.upload_bytes) / (1024.0 * 1024.0),
                  pending);
    }
  } else if (g_burst.frames) {
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                      g_burst.start).count();
    REXLOG_INFO("[native] textures: burst done: {} frames ({:.2f} s), {} uploads ({:.1f} MB)",
                g_burst.frames, secs, g_burst.uploads, double(g_burst.bytes) / (1024.0 * 1024.0));
    g_burst = {};
  }
  g_clay_window_max_ms = std::max(g_clay_window_max_ms, cpu_ms);
  if (g_clay_frames % 300 == 0) {
    // The window's view: the view at its end, marked when it changed during the window.
    const uint32_t view_changes = g_view_changes.load(std::memory_order_relaxed);
    const bool view_changed = view_changes != g_view_changes_logged;
    g_view_changes_logged = view_changes;
    REXLOG_INFO(
        "[native] clay: view {}{} | drawn {} (deformed {}) of {} drawable, skipped_bad_index {} other {} | "
        "textured {} of {} | {} uploads, {} hits, {:.1f} MB resident, instanced {}, skinned {} | "
        "hash {:.2f} ms, decode {:.2f} ms, record {:.2f} ms (max total {:.2f} ms over 300) | "
        "scene frame {}",
        ViewName(CurrentView()), view_changed ? " (changed)" : "", st.drawn, st.deformed,
        scene->draws.size(), st.skipped_bad_index, st.skipped_other,
        tst.textured, st.drawn, st.uploads, st.hits,
        double(st.resident_bytes) / (1024.0 * 1024.0), st.instanced, st.skinned, st.hash_ms,
        st.decode_ms, st.record_ms, g_clay_window_max_ms, scene->frame);
    if (textures) {
      REXLOG_INFO("[native] clay: {}",
                  render::FormatTextureText(tst, st.drawn, st.alpha_unknown, st.blend_unknown));
      REXLOG_INFO("[native] clay: {}", render::FormatTerrainText(st, tst));
      // The light's vertex inputs in the geometry cache.
      REXLOG_INFO("[native] clay: {}", render::FormatLightText(st));
    }
    // What was drawn lit and what was not, by reason.
    REXLOG_INFO("[native] clay: {}", render::FormatLightDrawnText(st));
    // The shadows: what was drawn into each map, what was not and why; the pre-pass and the mask.
    REXLOG_INFO("[native] clay: {}", render::FormatShadowText(st));
    // The fog: the frame's block, what the pass did about it, and the frames without fog by reason.
    REXLOG_INFO("[native] clay: {}", render::FormatFogText(st));
    // The sky: the frame's sky block, what the pass did about it, its place, and the frames without a sky by reason.
    REXLOG_INFO("[native] clay: {}", render::FormatSkyText(st));
    // The post chain: the frame's post block, what the pass did about it, and the frames without post by reason.
    REXLOG_INFO("[native] clay: {}", render::FormatPostText(st));
    // The scene's furniture: the frame's quad set, what the pass drew of it, and the frames without furniture by reason.
    REXLOG_INFO("[native] clay: {}", render::FormatFurnitureText(st));
    REXLOG_INFO("[native] clay: {}", render::FormatSunText(st));
    // The scene's clouds: what the pass did about them, and the frames by verdict.
    REXLOG_INFO("[native] clay: {}", render::FormatCloudText(st));
    // The wind: the records and casters drawn moved, and those with a wind shader drawn at rest by reason. Only
    // with the switch on: with it off the log is the one of before the wind.
    if (st.wind_switch) REXLOG_INFO("[native] clay: {}", render::FormatWindText(st));
    if (const std::string by_vs = render::FormatShadowSkippedByVs(st); !by_vs.empty()) {
      REXLOG_INFO("[native] clay: shadow draws skipped by vs {{{}}}", by_vs);
    }

    REXLOG_INFO("[native] clay: {} | alpha-unknown {}, blend-unknown {}", render::FormatStateText(st),
                st.alpha_unknown, st.blend_unknown);
    const std::string by_ps = render::FormatUnknownByPs(st);
    if (!by_ps.empty()) {
      REXLOG_INFO("[native] clay: {}{}", by_ps,
                  st.unknown_by_ps.Other() ? " (+ more shaders)" : "");
    }
    // The scan-out ramp: how often it was read and how often it was another table (nothing with the
    // post's switch off: the ramp is not read then).
    if (RampReadWanted(CurrentPost()) && g_ramp_reader.load(std::memory_order_acquire)) {
      REXLOG_INFO("[native] ramp: changes {} (reads {}, without a table {})", g_ramp.changes, g_ramp.reads, g_ramp.failed);
    }
    g_clay_window_max_ms = 0;
  }
  return true;
}

// fable2_native_d3d_messages: what the Direct3D debug layer stored since the last frame, as log
// lines, and the count with every 300th clay frame's lines. Command-processor thread.
void LogD3dMessages() {
  if (!REXCVAR_GET(fable2_native_d3d_messages)) return;
  std::lock_guard<std::mutex> lock(g_d3d_messages_mutex);
  if (!g_d3d_messages_started) {
    g_d3d_messages_started = true;
    // The queue answering says only that dxgidebug.dll is there; the layer itself is the SDK's switch.
    REXLOG_INFO("[native] {}", D3dMessagesStartText(render::D3dMessagesAvailable(),
                                                    rex::cvar::GetFlagByName("d3d12_debug") == "true"));
  }
  render::PollD3dMessages(g_d3d_messages, [](const std::string& line) { REXLOG_WARN("[native] {}", line); });
  if (g_clay_frames != g_d3d_messages_logged && g_clay_frames % 300 == 0) {
    g_d3d_messages_logged = g_clay_frames;
    REXLOG_INFO("[native] {}", g_d3d_messages.Summary());
  }
}

// One more read of the layer's queue, with the count: where no composite frame may follow.
void PollD3dMessagesOnce(const char* when) {
  if (!REXCVAR_GET(fable2_native_d3d_messages)) return;
  std::lock_guard<std::mutex> lock(g_d3d_messages_mutex);
  render::PollD3dMessages(g_d3d_messages, [](const std::string& line) { REXLOG_WARN("[native] {}", line); });
  REXLOG_INFO("[native] {} ({})", g_d3d_messages.Summary(), when);
}

// The guest's scan-out ramp as the SDK holds it at this composite frame (the table the emulated
// picture of this frame went through: the callback runs at the end of the swap blit, on its thread).
// Only with fable2_native_post on (RampReadWanted): with it off nothing is read and nothing logged.
// Logged in full at the first read and at the changes after it, up to kRampFullLogs
// lines in a run (later changes are only counted: "[native] ramp: changes ..."). Read before the clay
// pass renders the frame, which draws a post frame through it (g_post_ramp); the clay frame's number in
// its lines is that of the last frame rendered before the read. A table read while the front buffer's
// format takes the SDK's piecewise ramp is not the picture's ramp (RampTableApplies): the frame has
// none and is drawn without post ("post-ramp"), said once.
void ReadRamp() {
  g_post_ramp.valid = false;  // (until this frame's table is read and passes its rule)
  if (!RampReadWanted(CurrentPost())) return;
  const auto reader = g_ramp_reader.load(std::memory_order_acquire);
  if (!reader) {
    if (!g_ramp.said_none) REXLOG_INFO("[native] ramp: no reader registered; the scan-out ramp is not read");
    g_ramp.said_none = true;
    return;
  }
  uint32_t now[256][3] = {};
  uint32_t front_format = ~0u;  // (no format: a reader that names none gives no table)
  if (!reader(g_ramp_user.load(std::memory_order_acquire), now, &front_format)) {
    if (g_ramp.failed++ == 0) REXLOG_WARN("[native] ramp: the reader gave no table at clay frame {}", g_clay_frames);
    return;
  }
  ++g_ramp.reads;
  if (!RampTableApplies(front_format)) {
    if (g_ramp.not_table++ == 0) {
      REXLOG_WARN("[native] ramp: the front buffer's format {} takes the SDK's piecewise ramp, not the 256-entry table, at clay "
                  "frame {}: such frames are drawn without post (post-ramp)",
                  front_format, g_clay_frames);
    }
    return;
  }
  if (g_ramp.have_last && SameRamp(now, g_ramp.last)) {
    g_post_ramp.valid = g_ramp.last_valid;  // (the table of the last read: its floats are still there)
    return;
  }
  g_post_ramp = MakePostRamp(now);
  g_ramp.last_valid = g_post_ramp.valid;
  if (g_ramp.have_last) ++g_ramp.changes;
  g_ramp.have_last = true;
  std::copy(&now[0][0], &now[0][0] + 256 * 3, &g_ramp.last[0][0]);
  if (!RampLoggedInFull(g_ramp.full_lines)) {
    if (g_ramp.full_lines++ == kRampFullLogs) {
      REXLOG_INFO("[native] ramp: changed again at clay frame {}; {} tables are in the log, later changes are counted only",
                  g_clay_frames, kRampFullLogs);
    }
    return;
  }
  ++g_ramp.full_lines;
  REXLOG_INFO("[native] ramp: read at clay frame {}: valid {}, {}", g_clay_frames, g_post_ramp.valid ? 1 : 0,
              FormatRampHex(now));
}

void OverlayCallback(const NativeGuestOutputRenderContext& ctx, void*) {
  const View view = CurrentView();
  if (!Usable(ctx) || !IsCompositeView(view)) {  // off/pattern: record nothing
    g_clay_gap = true;
    return;
  }
  LogD3dMessages();  // (of the frames before this one: the layer reports a call when it is executed)
  ReadRamp();  // (before the clay pass: a post frame is drawn through this frame's ramp)
  if (!RenderClay(ctx)) return;
  WriteOracleDump(ctx.device);  // (the oracle feed's one file; nothing without its switch)
  if (CurrentSceneDump()) WriteSceneDump(ctx.device);  // (the stored native scene's files; nothing without its switch)
  if (!g_clay_scene) return;  // no scene captured yet: leave the emulated frame
  if (!g_composite.Ensure(ctx.device, ctx.guest_output->format())) {
    Fail("composite");
    return;
  }
  if (view == View::kShadow) {
    // The pass's two maps and its mask (in kPixelShaderResource since its Render), each a flat colour
    // for a frame without it (every frame with the shadows off, once the objects exist). Without the
    // shadows' objects (no frame was drawn with shadows yet, or they could not be made) there is
    // nothing to show: the native picture stands in.
    if (!g_composite.DrawShadowMaps(ctx.cmd, ctx, g_clay->shadow_view(0), g_clay->shadow_view(1), g_clay->mask_view(),
                                    g_clay->shadow_maps_drawn(), g_clay->mask_drawn(), render::kShadowMapSize,
                                    render::ClayPass::kWidth, render::ClayPass::kHeight)) {
      g_composite.Draw(ctx.cmd, ctx, g_clay->color(), View::kNative);
    }
  } else {
    g_composite.Draw(ctx.cmd, ctx, g_clay->color(), view);
  }
  g_frame.fetch_add(1, std::memory_order_relaxed);
}

bool WindowFocused() {
#ifdef _WIN32
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
#else
  return true;
#endif
}

}  // namespace

// fable2_exposure_readback: read like the light's switch (the same parser and words); anything else
// is off, with one warning that names the words. Not the renderer's: the application seeds the SDK's
// forced read-back list from it once (Fable2App::OnPostSetup), with the renderer installed or not.
bool ExposureReadbackWanted() {
  static std::atomic<bool> warned{false};
  bool recognized = true;
  const bool on = render::ParseClayLight(REXCVAR_GET(fable2_exposure_readback), &recognized) == render::ClayLight::kOn;
  if (!recognized && !warned.exchange(true)) {
    REXLOG_WARN("[native] unknown fable2_exposure_readback '{}', using off (accepted: {})",
                REXCVAR_GET(fable2_exposure_readback), render::kClaySwitchWords);
  }
  return on;
}

std::string StatusText() {
  if (!g_installed.load(std::memory_order_acquire)) return {};
  // No native drawing in off/pattern view: never show stale clay stats.
  if (!g_latch.IsFailed() && !IsCompositeView(CurrentView())) return {};
  std::lock_guard<std::mutex> lock(g_status_mutex);
  return g_status;
}

void SetGammaRampReader(bool (*reader)(void* user, uint32_t out_rgb10[256][3], uint32_t* out_front_format), void* user) {
  g_ramp_user.store(user, std::memory_order_release);
  g_ramp_reader.store(reader, std::memory_order_release);
}

void Install(rex::memory::Memory* memory) {
  capture::SetMemory(memory);  // always: discovery works with the renderer off
  if (!REXCVAR_GET(fable2_native_render)) {
    REXLOG_INFO("[native] native renderer disabled (fable2_native_render=false)");
    return;
  }
  g_view.store(ParseViewCvar(), std::memory_order_relaxed);
  LoadOracle();  // (before the callbacks exist: the feed is never written again)
  rex::graphics::SetNativeGuestOutputRenderer(&RenderCallback, nullptr);
  rex::graphics::SetNativeGuestOutputPostProcessor(&OverlayCallback, nullptr);
  g_installed.store(true, std::memory_order_release);
  REXLOG_INFO("[native] native renderer installed (view={}, F6 cycles views)",
              ViewName(CurrentView()));
  REXLOG_INFO("[native] light: {} (fable2_native_light)",
              CurrentClayLight() == render::ClayLight::kOn ? "on" : "off");
  REXLOG_INFO("[native] shadows: {} (fable2_native_shadows; on: the two shadow maps, a depth pre-pass and the screen mask "
              "are drawn and the lit draws' sun is shadowed)",
              CurrentShadows() ? "on" : "off");
  REXLOG_INFO("[native] fog: {} (fable2_native_fog; on: the frame's fog and sky blocks are read with the light on and "
              "the lit draws, and the unlit ones whose shader has the game's fog block, are fogged and the sky is drawn behind them)",
              CurrentFog() ? "on" : "off");
  REXLOG_INFO("[native] post: {} (fable2_native_post; on: the frame's post block is read with the light on, and a frame "
              "that has one is drawn through the game's composite and the scan-out ramp)",
              CurrentPost() ? "on" : "off");
  REXLOG_INFO("[native] bloom: {} (fable2_native_bloom; on: a post frame with the game's blur weights also draws the game's "
              "bloom, in targets a quarter of the logical size)",
              CurrentBloom() ? "on" : "off");
  REXLOG_INFO("[native] sun: {} (fable2_native_sun; on: with the post block the frame's quad records, the game's sky "
              "furniture and sun-layer quads, and the radial blur's registers are read)",
              CurrentSun() ? "on" : "off");
  // (off by default and in the making: its line only when it is on, so that a normal run's log is the one of before)
  if (CurrentWind()) {
    REXLOG_INFO("[native] wind: on (fable2_native_wind; the wind inputs of the draws whose vertex shader sways are read in a "
                "frame with records, and the native view draws those records moved)");
  }
  // (a diagnostic: its line only when it is on, so that a normal run's log is the one of before)
  if (CurrentSceneDump()) {
    REXLOG_INFO("[native] scene dump: on (fable2_native_scene_dump; each discovery capture made in a native view writes the native "
                "scene of its first frame before post to logs/native_scene_<stamp>.f2ns, at most {} files a run)",
                render::kSceneDumpMaxFiles);
  }
}

void Shutdown() {
  if (!g_installed.load(std::memory_order_acquire)) return;
  PollD3dMessagesOnce("at shutdown");
}

// Game thread only: the string cvar is touched here only.
void ApplyView(View next, const char* source) {
  // Setting the view it is in changes nothing: the clay line is not marked "(changed)" for it.
  const bool changes = ViewChanges(CurrentView(), next);
  REXCVAR_SET(fable2_native_view, std::string(ViewName(next)));
  g_view.store(next, std::memory_order_relaxed);
  if (changes) g_view_changes.fetch_add(1, std::memory_order_relaxed);
  REXLOG_INFO("[native] {}: view {}{}", source, ViewName(next), changes ? "" : " (unchanged)");
}

void SetView(View view) {
  if (!g_installed.load(std::memory_order_acquire)) {
    REXLOG_INFO("[native] SetView({}): native renderer not installed; ignored", ViewName(view));
    return;
  }
  g_pending_view.store(int(view), std::memory_order_release);
}

bool ArmDiscovery(uint32_t frames, uint32_t every) {
  return capture::ArmDiscoveryNow(frames, every);
}

void PollFrame() {
  if (!g_installed.load(std::memory_order_acquire)) return;
  {
    // Frame times per view (ViewFrameLog; game thread). Costs a timestamp a frame.
    static ViewFrameLog frames;
    static const std::chrono::steady_clock::time_point epoch = std::chrono::steady_clock::now();
    const double now_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - epoch).count();
    const std::string line = frames.Add(CurrentView(), now_ms);
    if (!line.empty()) REXLOG_INFO("[native] frames: {}", line);
  }
  if (const int pending = g_pending_view.exchange(-1, std::memory_order_acq_rel); pending >= 0) {
    ApplyView(View(pending), "autoplay");
  }
#ifdef _WIN32
  const bool down = WindowFocused() && (GetAsyncKeyState(kToggleVk) & 0x8000) != 0;
  if (g_toggle.Update(down)) {
    if (g_latch.IsFailed()) {
      g_latch.Clear();
      SetStatus({});
      REXLOG_INFO("[native] F6: retrying native renderer");
    } else {
      ApplyView(NextView(CurrentView()), "F6");
    }
  }
#endif
  // The order of the five stores (final review, M6). The capture latches them at a frame's boundary,
  // the records' word first (read with acquire), then the shadows', the fog's, the post block's and the quads'.
  // Switching ON: the shadows', the fog's, the post block's and the quads' words are stored BEFORE the records'
  // (stored with release), so a boundary that sees the records wanted sees the others as well: no frame is recorded without its shadow
  // passes or its fog block because the boundary fell between two stores. Switching OFF: the records'
  // word first; a boundary between the stores then latches no records, and with no records the other
  // words are not read at all. (A boundary that read the records' word just before it went off
  // may still latch one last frame with records and without fog: the view is off by then and the
  // frame is not drawn; were it drawn, it would be without fog and the pass would count it "fog-block".)
  const bool wanted = RecordsWanted(CurrentView(), g_latch.IsFailed());
  if (!wanted) capture::SetRecordsWanted(false);
  capture::SetShadowsWanted(ShadowsWanted(wanted));
  capture::SetFogWanted(FogWanted(wanted));
  capture::SetPostWanted(PostWanted(wanted));
  capture::SetSunWanted(SunRecordsWanted(PostWanted(wanted), CurrentSun()));
  // (the wind's word like the others: before the records' when switching on, after it when switching off)
  capture::SetWindWanted(wanted && CurrentWind());
  if (wanted) capture::SetRecordsWanted(true);
  rex::graphics::RequestNativeGuestOutputPostProcess(wanted);
}

}  // namespace fable2::native
