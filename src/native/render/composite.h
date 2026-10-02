#pragma once

// Debug-view composite: draws the clay target over the guest output (which
// stays the emulated frame underneath). GPU (command-processor) thread only.

#include <rex/graphics/native_guest_renderer.h>
#include <rex/graphics/native_rhi.h>

#include "../native_render_state.h"

namespace fable2::native::render {

namespace nrhi = rex::graphics::nrhi;

class Composite {
 public:
  // Layout, shaders and pipeline for the guest output format. False on
  // failure (logged); call again to retry.
  bool Ensure(nrhi::Device* dev, nrhi::Format out_format);
  // `clay` must be in kPixelShaderResource; the guest output goes
  // kGuestOutput -> kRenderTarget -> kGuestOutput. view: overlay/split/native.
  void Draw(nrhi::Cmd* cmd, const rex::graphics::NativeGuestOutputRenderContext& ctx,
            nrhi::Texture* clay, View view);

 private:
  nrhi::Device* device_ = nullptr;
  nrhi::Format format_ = nrhi::Format::kUnknown;
  nrhi::BindingLayout* layout_ = nullptr;  // the RHI has no layout destruction
  nrhi::Shader* vs_ = nullptr;
  nrhi::Shader* ps_ = nullptr;
  nrhi::Pipeline* pipeline_ = nullptr;
  nrhi::Texture* view_texture_ = nullptr;  // texture view_ was created for
  nrhi::TextureView* view_ = nullptr;
};

}  // namespace fable2::native::render
