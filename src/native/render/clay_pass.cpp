#include "clay_pass.h"

#include <chrono>

#include <rex/logging.h>

#include "../fable2_native_shaders.h"

namespace fable2::native::render {

namespace {
constexpr float kClearColor[4] = {0.08f, 0.08f, 0.1f, 1.0f};
}  // namespace

void ClayPass::DestroyObjects() {
  if (device_) {
    device_->DestroyDeferred(pipeline_);
    device_->DestroyDeferred(vs_);
    device_->DestroyDeferred(ps_);
    device_->DestroyDeferred(color_);
    device_->DestroyDeferred(depth_);
  }
  pipeline_ = nullptr;
  vs_ = ps_ = nullptr;
  color_ = depth_ = nullptr;
  ready_ = false;
}

bool ClayPass::Ensure(nrhi::Device* dev) {
  if (ready_ && dev == device_) return true;
  if (dev != device_) {
    // A different device: the old objects are not ours to destroy through it.
    geometry_.Release(nullptr);
    pipeline_ = nullptr;
    vs_ = ps_ = nullptr;
    color_ = depth_ = nullptr;
    layout_ = nullptr;
    device_ = dev;
  } else {
    DestroyObjects();  // retry after a partial failure
  }
  ready_ = false;

  if (!layout_) {
    nrhi::BindingLayoutDesc desc;
    desc.params[0].kind = nrhi::BindingParamKind::kConstants;
    desc.params[0].shader_register = 0;
    desc.params[0].count = 20;
    desc.params[0].visibility = nrhi::Visibility::kAll;
    desc.params[1].kind = nrhi::BindingParamKind::kBufferSrv;
    desc.params[1].shader_register = 0;
    desc.params[1].visibility = nrhi::Visibility::kAll;
    desc.params[2].kind = nrhi::BindingParamKind::kBufferSrv;
    desc.params[2].shader_register = 1;
    desc.params[2].visibility = nrhi::Visibility::kAll;
    desc.param_count = 3;
    desc.allow_input_layout = false;
    layout_ = dev->CreateBindingLayout(desc);
    if (!layout_) {
      REXLOG_ERROR("[native] clay: binding layout creation failed");
      return false;
    }
  }

  auto make_shader = [&](nrhi::ShaderStage stage, const char* name, const char* src) {
    nrhi::ShaderDesc desc;
    desc.stage = stage;
    desc.name = name;
    desc.hlsl_source = src;
    desc.entry_point = "main";
    return dev->CreateShader(desc);
  };
  vs_ = make_shader(nrhi::ShaderStage::kVertex, "fable2_clay_vs", shaders::kClayVs);
  ps_ = make_shader(nrhi::ShaderStage::kPixel, "fable2_clay_ps", shaders::kClayPs);
  if (!vs_ || !ps_) {
    REXLOG_ERROR("[native] clay: shader compile failed");
    return false;
  }

  nrhi::GraphicsPipelineDesc pipe;
  pipe.layout = layout_;
  pipe.vs = vs_;
  pipe.ps = ps_;
  pipe.rtv_format = nrhi::Format::kR8G8B8A8_UNORM;
  pipe.dsv_format = nrhi::Format::kD32_FLOAT;
  pipe.depth.test_enable = true;
  pipe.depth.write_enable = true;
  pipe.depth.func = nrhi::CompareFunc::kLess;
  pipe.cull = nrhi::CullMode::kNone;
  pipeline_ = dev->CreateGraphicsPipeline(pipe);
  if (!pipeline_) {
    REXLOG_ERROR("[native] clay: pipeline creation failed");
    return false;
  }

  nrhi::TextureDesc color;
  color.width = kWidth;
  color.height = kHeight;
  color.format = nrhi::Format::kR8G8B8A8_UNORM;
  color.usage = nrhi::kTextureUsageRenderTarget;
  color.initial_state = nrhi::ResourceState::kPixelShaderResource;
  for (int i = 0; i < 4; ++i) color.clear_color[i] = kClearColor[i];
  color_ = dev->CreateTexture(color);
  nrhi::TextureDesc depth;
  depth.width = kWidth;
  depth.height = kHeight;
  depth.format = nrhi::Format::kD32_FLOAT;
  depth.usage = nrhi::kTextureUsageDepthStencil;
  depth.initial_state = nrhi::ResourceState::kPixelShaderResource;
  depth.clear_depth = 1.0f;
  depth_ = dev->CreateTexture(depth);
  if (!color_ || !depth_) {
    REXLOG_ERROR("[native] clay: target creation failed");
    return false;
  }
  ready_ = true;
  return true;
}

void ClayPass::Render(nrhi::Cmd* cmd, nrhi::Device* dev, const FrameScene& scene, ClayColor color,
                      ClayStats& st) {
  if (!ready_ || dev != device_) return;
  const auto t0 = std::chrono::steady_clock::now();
  const double hash0 = st.hash_ms, decode0 = st.decode_ms;

  cmd->Barrier(color_, nrhi::ResourceState::kPixelShaderResource,
               nrhi::ResourceState::kRenderTarget);
  cmd->Barrier(depth_, nrhi::ResourceState::kPixelShaderResource,
               nrhi::ResourceState::kDepthWrite);
  cmd->FlushBarriers();
  cmd->SetRenderTargets(color_, depth_);
  cmd->ClearRenderTarget(color_, kClearColor);
  cmd->ClearDepth(depth_, 1.0f);
  cmd->SetViewport({0.0f, 0.0f, float(kWidth), float(kHeight), 0.0f, 1.0f});
  cmd->SetScissor({0, 0, int32_t(kWidth), int32_t(kHeight)});
  cmd->SetBindingLayout(layout_);
  cmd->SetPipeline(pipeline_);
  cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);

  for (const capture::DrawRecord& r : scene.draws) {
    nrhi::Buffer* positions = geometry_.Positions(dev, r, st);
    if (!positions) continue;
    const uint32_t vertex_count = PositionCount(r.vb.size, r.pos);
    uint32_t index_count = 0;
    nrhi::Buffer* indices = geometry_.Indices(dev, r, vertex_count, &index_count, st);
    if (!indices) continue;
    const ClayConstants constants = MakeClayConstants(r, vertex_count, ClayColorValue(color, r));
    cmd->SetRootConstants(0, 20, &constants, 0);
    cmd->SetBufferSrv(1, positions, 0);
    cmd->SetBufferSrv(2, indices, 0);
    cmd->Draw(index_count, 0);
    ++st.drawn;
    if (r.deformed) ++st.deformed;
  }

  cmd->Barrier(color_, nrhi::ResourceState::kRenderTarget,
               nrhi::ResourceState::kPixelShaderResource);
  cmd->Barrier(depth_, nrhi::ResourceState::kDepthWrite,
               nrhi::ResourceState::kPixelShaderResource);
  cmd->FlushBarriers();

  st.resident_bytes = geometry_.resident_bytes();
  const double total =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  st.record_ms += std::max(0.0, total - (st.hash_ms - hash0) - (st.decode_ms - decode0));
}

}  // namespace fable2::native::render
