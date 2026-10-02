#pragma once

// Untextured "clay" rendering of the captured main scene into a native
// 1120x720 target (R8G8B8A8 color + D32 depth). Vertices are pulled from the
// geometry cache's StructuredBuffers by SV_VertexID and transformed by each
// draw's captured rows. GPU (command-processor) thread only; Cmd is valid
// only inside the SDK's guest-output callbacks.

#include <cstdint>

#include <rex/graphics/native_rhi.h>

#include "clay_logic.h"
#include "frame_scene.h"
#include "geometry_cache.h"

namespace fable2::native::render {

class ClayPass {
 public:
  static constexpr uint32_t kWidth = 1120;
  static constexpr uint32_t kHeight = 720;

  // Targets, binding layout, shaders and pipeline. False on any failure
  // (logged); call again to retry.
  bool Ensure(nrhi::Device* dev);
  void Render(nrhi::Cmd* cmd, nrhi::Device* dev, const FrameScene& scene, ClayColor color,
              ClayStats& st);
  nrhi::Texture* color() const { return color_; }  // left in kPixelShaderResource after Render
  nrhi::Texture* depth() const { return depth_; }  // likewise
  GeometryCache& geometry() { return geometry_; }

 private:
  void DestroyObjects();

  nrhi::Device* device_ = nullptr;
  bool ready_ = false;
  nrhi::BindingLayout* layout_ = nullptr;  // the RHI has no layout destruction
  nrhi::Shader* vs_ = nullptr;
  nrhi::Shader* ps_ = nullptr;
  nrhi::Pipeline* pipeline_ = nullptr;
  nrhi::Texture* color_ = nullptr;
  nrhi::Texture* depth_ = nullptr;
  GeometryCache geometry_;
};

}  // namespace fable2::native::render
