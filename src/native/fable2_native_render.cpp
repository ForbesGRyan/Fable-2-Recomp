#include "fable2_native_render.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include <rex/cvar.h>
#include <rex/graphics/native_guest_renderer.h>
#include <rex/graphics/native_rhi.h>
#include <rex/logging.h>

#include "capture.h"
#include "clay_pass.h"
#include "composite.h"
#include "d3d_messages.h"
#include "fable2_native_shaders.h"
#include "native_render_state.h"

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
  g_clay->Render(ctx.cmd, ctx.device, *scene, CurrentClayColor(), textures, CurrentClayLight(), CurrentShadows(), CurrentFog(), st,
                 tst);
  g_shadows_usable.store(g_clay->shadows_usable(), std::memory_order_relaxed);
  SetStatus(render::FormatStatusText(*scene, st, textures ? &tst : nullptr) + "\n" +
            render::FormatLightDrawnText(st) + "\n" + render::FormatShadowText(st) + "\n" + render::FormatFogText(st) + "\n" +
            render::FormatSkyText(st));
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

void OverlayCallback(const NativeGuestOutputRenderContext& ctx, void*) {
  const View view = CurrentView();
  if (!Usable(ctx) || !IsCompositeView(view)) return;  // off/pattern: record nothing
  LogD3dMessages();  // (of the frames before this one: the layer reports a call when it is executed)
  if (!RenderClay(ctx)) return;
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

std::string StatusText() {
  if (!g_installed.load(std::memory_order_acquire)) return {};
  // No native drawing in off/pattern view: never show stale clay stats.
  if (!g_latch.IsFailed() && !IsCompositeView(CurrentView())) return {};
  std::lock_guard<std::mutex> lock(g_status_mutex);
  return g_status;
}

void Install(rex::memory::Memory* memory) {
  capture::SetMemory(memory);  // always: discovery works with the renderer off
  if (!REXCVAR_GET(fable2_native_render)) {
    REXLOG_INFO("[native] native renderer disabled (fable2_native_render=false)");
    return;
  }
  g_view.store(ParseViewCvar(), std::memory_order_relaxed);
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
  // The order of the three stores (final review, M6). The capture latches them at a frame's boundary,
  // the records' word first (read with acquire), then the shadows' and the fog's. Switching ON: the
  // shadows' and the fog's words are stored BEFORE the records' (stored with release), so a boundary
  // that sees the records wanted sees the other two as well: no frame is recorded without its shadow
  // passes or its fog block because the boundary fell between two stores. Switching OFF: the records'
  // word first; a boundary between the stores then latches no records, and with no records the other
  // two words are not read at all. (A boundary that read the records' word just before it went off
  // may still latch one last frame with records and without fog: the view is off by then and the
  // frame is not drawn; were it drawn, it would be without fog and the pass would count it "fog-block".)
  const bool wanted = RecordsWanted(CurrentView(), g_latch.IsFailed());
  if (!wanted) capture::SetRecordsWanted(false);
  capture::SetShadowsWanted(ShadowsWanted(wanted));
  capture::SetFogWanted(FogWanted(wanted));
  if (wanted) capture::SetRecordsWanted(true);
  rex::graphics::RequestNativeGuestOutputPostProcess(wanted);
}

}  // namespace fable2::native
