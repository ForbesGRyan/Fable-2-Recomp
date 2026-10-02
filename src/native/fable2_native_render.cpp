#include "fable2_native_render.h"

#include <algorithm>
#include <atomic>
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
#include "fable2_native_shaders.h"
#include "native_render_state.h"

REXCVAR_DEFINE_BOOL(fable2_native_render, false, "Fable2",
                    "Register the native guest-output renderer at startup (restart "
                    "required). Seeded from fable2_config.toml [native] enabled.");
REXCVAR_DEFINE_BOOL(fable2_native_render_active, true, "Fable2",
                    "Live switch for the native renderer (F6 toggles).");
REXCVAR_DEFINE_STRING(fable2_native_render_mode, "overlay", "Fable2",
                      "overlay = grid drawn over the emulated frame; replace = native "
                      "test pattern replaces the frame.");
REXCVAR_DEFINE_STRING(fable2_native_clay_color, "clay", "Fable2",
                      "Native clay pass color: clay = one clay color; draw = a color per "
                      "draw; shader = a color per vertex shader.");
REXCVAR_DEFINE_INT32(fable2_native_geometry_budget_mb, 512, "Fable2",
                     "GPU memory budget (MB) for decoded native geometry; least recently "
                     "used buffers are evicted above it.");

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
  nrhi::Shader* grid_ps = nullptr;
  nrhi::Pipeline* pattern = nullptr;
  nrhi::Pipeline* grid = nullptr;
};

bool g_installed = false;
Resources g_res;  // command-processor thread only
FailureLatch g_latch;
EdgeDetector g_toggle;
std::atomic<uint32_t> g_frame{0};

// Clay pass state: command-processor thread only.
render::ClayPass g_clay;
std::shared_ptr<const render::FrameScene> g_clay_scene;  // last scene rendered
uint64_t g_clay_frames = 0;
double g_clay_window_max_ms = 0;  // worst hash+decode+record over the log window

// F3 status text, written on the command-processor thread, read by the UI.
std::mutex g_status_mutex;
std::string g_status;

void SetStatus(std::string text) {
  std::lock_guard<std::mutex> lock(g_status_mutex);
  g_status = std::move(text);
}

Mode CurrentMode() {
  static std::atomic<bool> warned{false};
  const ParsedMode parsed = ParseMode(REXCVAR_GET(fable2_native_render_mode));
  if (!parsed.recognized && !warned.exchange(true)) {
    REXLOG_WARN("[native] unknown fable2_native_render_mode '{}', using overlay",
                REXCVAR_GET(fable2_native_render_mode));
  }
  return parsed.mode;
}

void Fail(const char* what) {
  if (g_latch.Fail()) {
    REXLOG_ERROR("[native] {} failed; falling back to emulated output (F6 retries)", what);
    SetStatus(std::string("Native: ") + what + " failed (F6 retries)");
  }
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

bool EnsureResources(nrhi::Device* device, nrhi::Format format) {
  if (g_res.device == device && g_res.format == format && g_res.pattern && g_res.grid) {
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
  g_res.grid_ps = make_shader(nrhi::ShaderStage::kPixel, "fable2_grid_ps", shaders::kGridPs);
  if (!g_res.vs || !g_res.pattern_ps || !g_res.grid_ps) return Fail("shader compile"), false;

  nrhi::GraphicsPipelineDesc pipe;
  pipe.layout = g_res.layout;
  pipe.vs = g_res.vs;
  pipe.rtv_format = format;
  pipe.ps = g_res.pattern_ps;
  g_res.pattern = device->CreateGraphicsPipeline(pipe);
  pipe.ps = g_res.grid_ps;
  pipe.blend.enable = true;
  pipe.blend.src = nrhi::BlendFactor::kSrcAlpha;
  pipe.blend.dst = nrhi::BlendFactor::kInvSrcAlpha;
  g_res.grid = device->CreateGraphicsPipeline(pipe);
  if (!g_res.pattern || !g_res.grid) return Fail("pipeline creation"), false;
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
  return REXCVAR_GET(fable2_native_render_active) && !g_latch.IsFailed() &&
         ctx.backend == NativeGuestOutputBackend::kD3D12 && ctx.device && ctx.cmd &&
         ctx.guest_output;
}

bool RenderCallback(const NativeGuestOutputRenderContext& ctx, void*) {
  if (!Usable(ctx) || CurrentMode() != Mode::kReplace) return false;
  if (!EnsureResources(ctx.device, ctx.guest_output->format())) return false;
  DrawFullscreen(ctx, g_res.pattern);
  g_frame.fetch_add(1, std::memory_order_relaxed);
  return true;
}

// Clay pass into its own native target (the guest output is not touched;
// the composite comes later). Renders each published scene once. False on
// failure (latched).
bool RenderClay(const NativeGuestOutputRenderContext& ctx) {
  std::shared_ptr<const render::FrameScene> scene = capture::Publisher().Latest();
  if (!scene) return true;
  if (!g_clay.Ensure(ctx.device)) {
    Fail("clay pass");
    return false;
  }
  if (scene == g_clay_scene) return true;
  g_clay_scene = scene;
  ++g_clay_frames;
  const int32_t budget_mb = std::max<int32_t>(16, REXCVAR_GET(fable2_native_geometry_budget_mb));
  g_clay.geometry().BeginFrame(g_clay_frames, uint64_t(budget_mb) << 20);
  render::ClayStats st;
  g_clay.Render(ctx.cmd, ctx.device, *scene, CurrentClayColor(), st);
  SetStatus(render::FormatStatusText(*scene, st));
  const double cpu_ms = st.hash_ms + st.decode_ms + st.record_ms;
  g_clay_window_max_ms = std::max(g_clay_window_max_ms, cpu_ms);
  if (g_clay_frames % 300 == 0) {
    REXLOG_INFO(
        "[native] clay: drawn {} (deformed {}) of {} drawable, skipped_bad_index {} other {} | "
        "{} uploads, {} hits, {:.1f} MB resident | hash {:.2f} ms, decode {:.2f} ms, record "
        "{:.2f} ms (max total {:.2f} ms over 300) | scene frame {}",
        st.drawn, st.deformed, scene->draws.size(), st.skipped_bad_index, st.skipped_other,
        st.uploads, st.hits, double(st.resident_bytes) / (1024.0 * 1024.0), st.hash_ms,
        st.decode_ms, st.record_ms, g_clay_window_max_ms, scene->frame);
    g_clay_window_max_ms = 0;
  }
  return true;
}

void OverlayCallback(const NativeGuestOutputRenderContext& ctx, void*) {
  if (!Usable(ctx) || CurrentMode() != Mode::kOverlay) return;
  if (!RenderClay(ctx)) return;
  if (!EnsureResources(ctx.device, ctx.guest_output->format())) return;
  DrawFullscreen(ctx, g_res.grid);
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
  if (!g_installed) return {};
  std::lock_guard<std::mutex> lock(g_status_mutex);
  return g_status;
}

void Install(rex::memory::Memory* memory) {
  capture::SetMemory(memory);  // always: discovery works with the renderer off
  if (!REXCVAR_GET(fable2_native_render)) {
    REXLOG_INFO("[native] native renderer disabled (fable2_native_render=false)");
    return;
  }
  rex::graphics::SetNativeGuestOutputRenderer(&RenderCallback, nullptr);
  rex::graphics::SetNativeGuestOutputPostProcessor(&OverlayCallback, nullptr);
  g_installed = true;
  REXLOG_INFO("[native] native renderer installed (mode={}, F6 toggles)",
              REXCVAR_GET(fable2_native_render_mode));
}

void PollFrame() {
  if (!g_installed) return;
#ifdef _WIN32
  const bool down = WindowFocused() && (GetAsyncKeyState(kToggleVk) & 0x8000) != 0;
  if (g_toggle.Update(down)) {
    if (g_latch.IsFailed()) {
      g_latch.Clear();
      REXCVAR_SET(fable2_native_render_active, true);
      REXLOG_INFO("[native] F6: retrying native renderer");
    } else {
      const bool now_active = !REXCVAR_GET(fable2_native_render_active);
      REXCVAR_SET(fable2_native_render_active, now_active);
      REXLOG_INFO("[native] F6: native renderer {}", now_active ? "on" : "off");
    }
  }
#endif
  rex::graphics::RequestNativeGuestOutputPostProcess(
      REXCVAR_GET(fable2_native_render_active) && !g_latch.IsFailed() &&
      CurrentMode() == Mode::kOverlay);
}

}  // namespace fable2::native
