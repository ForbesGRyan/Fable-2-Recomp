#include "composite.h"

#include <cstring>

#include <rex/logging.h>

#include "../fable2_native_shaders.h"

namespace fable2::native::render {

bool Composite::Ensure(nrhi::Device* dev, nrhi::Format out_format) {
  if (pipeline_ && dev == device_ && out_format == format_) return true;
  if (device_) {  // device or format changed: drop the old objects
    device_->DestroyDeferred(pipeline_);
    device_->DestroyDeferred(vs_);
    device_->DestroyDeferred(ps_);
    if (view_) device_->DestroyDeferred(view_);
  }
  pipeline_ = nullptr;
  vs_ = ps_ = nullptr;
  view_ = nullptr;
  view_texture_ = nullptr;
  device_ = dev;
  format_ = out_format;

  nrhi::BindingLayoutDesc ld;
  ld.params[0].kind = nrhi::BindingParamKind::kConstants;
  ld.params[0].shader_register = 0;
  ld.params[0].count = 4;
  ld.params[0].visibility = nrhi::Visibility::kPixel;
  ld.params[1].kind = nrhi::BindingParamKind::kTextureTable;
  ld.params[1].shader_register = 0;
  ld.params[1].count = 1;
  ld.params[1].visibility = nrhi::Visibility::kPixel;
  ld.param_count = 2;
  ld.static_samplers[0].shader_register = 0;
  ld.static_samplers[0].filter = nrhi::Filter::kLinear;
  ld.static_samplers[0].address = nrhi::AddressMode::kClamp;
  ld.static_sampler_count = 1;
  ld.allow_input_layout = false;
  layout_ = dev->CreateBindingLayout(ld);
  if (!layout_) {
    REXLOG_ERROR("[native] composite: binding layout failed");
    return false;
  }

  nrhi::ShaderDesc sd;
  sd.entry_point = "main";
  sd.stage = nrhi::ShaderStage::kVertex;
  sd.name = "fable2_fullscreen_vs";
  sd.hlsl_source = shaders::kFullscreenVs;
  vs_ = dev->CreateShader(sd);
  sd.stage = nrhi::ShaderStage::kPixel;
  sd.name = "fable2_composite_ps";
  sd.hlsl_source = shaders::kCompositePs;
  ps_ = dev->CreateShader(sd);
  if (!vs_ || !ps_) {
    REXLOG_ERROR("[native] composite: shader compile failed");
    return false;
  }

  nrhi::GraphicsPipelineDesc pipe;
  pipe.layout = layout_;
  pipe.vs = vs_;
  pipe.ps = ps_;
  pipe.rtv_format = out_format;
  pipe.blend.enable = true;
  pipe.blend.src = nrhi::BlendFactor::kSrcAlpha;
  pipe.blend.dst = nrhi::BlendFactor::kInvSrcAlpha;
  pipeline_ = dev->CreateGraphicsPipeline(pipe);
  if (!pipeline_) {
    REXLOG_ERROR("[native] composite: pipeline creation failed");
    return false;
  }
  return true;
}

void Composite::Draw(nrhi::Cmd* cmd, const rex::graphics::NativeGuestOutputRenderContext& ctx,
                     nrhi::Texture* clay, View view) {
  if (!pipeline_ || !clay || ctx.device != device_) return;
  if (clay != view_texture_) {
    if (view_) device_->DestroyDeferred(view_);
    view_ = device_->CreateTextureView(clay, nrhi::TextureViewDesc{});
    view_texture_ = clay;
  }
  if (!view_) return;

  nrhi::Texture* out = ctx.guest_output;
  const float w = float(ctx.guest_output_width);
  const float h = float(ctx.guest_output_height);
  uint32_t mode = 3;
  float alpha = 1.0f;
  if (view == View::kOverlay) {
    mode = 1;
    alpha = 0.5f;
  } else if (view == View::kSplit) {
    mode = 2;
  }
  uint32_t params[4];
  std::memcpy(&params[0], &w, 4);
  std::memcpy(&params[1], &h, 4);
  params[2] = mode;
  std::memcpy(&params[3], &alpha, 4);

  cmd->Barrier(out, nrhi::ResourceState::kGuestOutput, nrhi::ResourceState::kRenderTarget);
  cmd->FlushBarriers();
  cmd->SetRenderTargets(out, nullptr);
  cmd->SetViewport({0.0f, 0.0f, w, h, 0.0f, 1.0f});
  cmd->SetScissor({0, 0, int32_t(w), int32_t(h)});
  cmd->SetBindingLayout(layout_);
  cmd->SetPipeline(pipeline_);
  cmd->SetRootConstants(0, 4, params, 0);
  cmd->SetTexture(1, view_);
  cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);
  cmd->Draw(3, 0);
  cmd->Barrier(out, nrhi::ResourceState::kRenderTarget, nrhi::ResourceState::kGuestOutput);
  cmd->FlushBarriers();
}

}  // namespace fable2::native::render
