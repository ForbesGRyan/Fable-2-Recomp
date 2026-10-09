#pragma once

// Debug-view composite: draws the clay target over the guest output (which
// stays the emulated frame underneath), or, for the shadow view, the clay
// pass's two shadow maps as grey depth and its screen mask over the whole
// output. GPU (command-processor) thread only.

#include <rex/graphics/native_guest_renderer.h>
#include <rex/graphics/native_rhi.h>

#include "../native_render_state.h"

namespace fable2::native::render {

namespace nrhi = rex::graphics::nrhi;

class Composite {
 public:
  // Layout, shaders and pipeline of the clay composite for the guest output
  // format (the shadow view's are made when it is first shown). False on
  // failure (logged); call again to retry.
  bool Ensure(nrhi::Device* dev, nrhi::Format out_format);
  // `clay` must be in kPixelShaderResource; the guest output goes
  // kGuestOutput -> kRenderTarget -> kGuestOutput. view: overlay/split/native.
  void Draw(nrhi::Cmd* cmd, const rex::graphics::NativeGuestOutputRenderContext& ctx,
            nrhi::Texture* clay, View view);
  // The shadow view (View::kShadow; shaders::kShadowViewPs): the two maps in a
  // column at the left and the screen mask beside them, opaque, over the whole
  // guest output. `near_map`, `far_map`, `mask`: the clay pass's views
  // (ClayPass::shadow_view, mask_view), which must be in kPixelShaderResource;
  // `maps_valid`, `mask_valid`: they were drawn for the frame shown
  // (ClayPass::shadow_maps_drawn, mask_drawn); what was not is a flat colour
  // and is not read. False, and nothing drawn, without all three views (the
  // shadows' objects do not exist: the shadows are off or could not be made)
  // or when the view's own pipeline cannot be made: the caller shows another
  // view. The guest output goes kGuestOutput -> kRenderTarget -> kGuestOutput.
  bool DrawShadowMaps(nrhi::Cmd* cmd, const rex::graphics::NativeGuestOutputRenderContext& ctx,
                      nrhi::TextureView* near_map, nrhi::TextureView* far_map, nrhi::TextureView* mask, bool maps_valid,
                      bool mask_valid, uint32_t map_size, uint32_t mask_width, uint32_t mask_height);

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
  bool shadow_failed_ = false;  // the shadow view's objects could not be made (logged once; not tried again)
  static constexpr uint32_t kShadowViewConstants = 8;  // cbuffer C of kShadowViewPs, in words
  bool EnsureShadowView();
  nrhi::Texture* view_texture_ = nullptr;  // texture view_ was created for
  nrhi::TextureView* view_ = nullptr;
};

}  // namespace fable2::native::render
