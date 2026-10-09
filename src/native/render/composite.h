#pragma once

// Debug-view composite: draws the clay target over the guest output (which
// stays the emulated frame underneath), or, for the shadow view, the clay
// pass's two shadow maps as grey depth over the whole output. GPU
// (command-processor) thread only.

#include <rex/graphics/native_guest_renderer.h>
#include <rex/graphics/native_rhi.h>

#include "../native_render_state.h"

namespace fable2::native::render {

namespace nrhi = rex::graphics::nrhi;

class Composite {
 public:
  // Layouts, shaders and pipelines (the clay composite's and the shadow view's)
  // for the guest output format. False on failure (logged); call again to retry.
  bool Ensure(nrhi::Device* dev, nrhi::Format out_format);
  // `clay` must be in kPixelShaderResource; the guest output goes
  // kGuestOutput -> kRenderTarget -> kGuestOutput. view: overlay/split/native.
  void Draw(nrhi::Cmd* cmd, const rex::graphics::NativeGuestOutputRenderContext& ctx,
            nrhi::Texture* clay, View view);
  // The shadow view (View::kShadow; shaders::kShadowViewPs): the two maps side
  // by side, opaque, over the whole guest output. `near_map`, `far_map`: the
  // clay pass's views of its maps (ClayPass::shadow_view), which must be in
  // kPixelShaderResource; `valid`: the maps were drawn for the frame shown
  // (ClayPass::shadow_maps_drawn). Without it, or without a view, neither map
  // is read and the squares are a flat colour. The guest output goes
  // kGuestOutput -> kRenderTarget -> kGuestOutput.
  void DrawShadowMaps(nrhi::Cmd* cmd, const rex::graphics::NativeGuestOutputRenderContext& ctx,
                      nrhi::TextureView* near_map, nrhi::TextureView* far_map, bool valid, uint32_t map_size);

 private:
  nrhi::Device* device_ = nullptr;
  nrhi::Format format_ = nrhi::Format::kUnknown;
  nrhi::BindingLayout* layout_ = nullptr;  // the RHI has no layout destruction
  nrhi::Shader* vs_ = nullptr;
  nrhi::Shader* ps_ = nullptr;
  nrhi::Pipeline* pipeline_ = nullptr;
  nrhi::BindingLayout* shadow_layout_ = nullptr;  // the RHI has no layout destruction
  nrhi::Shader* shadow_ps_ = nullptr;
  nrhi::Pipeline* shadow_pipeline_ = nullptr;
  nrhi::Texture* view_texture_ = nullptr;  // texture view_ was created for
  nrhi::TextureView* view_ = nullptr;
};

}  // namespace fable2::native::render
