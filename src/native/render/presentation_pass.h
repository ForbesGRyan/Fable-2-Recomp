#pragma once
#include <array>
#include <string>
#include <vector>
#include "../capture/presentation_frame.h"
#include "texture_cache.h"

namespace fable2::native::render {
class PresentationPass {
 public:
  bool Render(nrhi::Cmd* cmd, nrhi::Device* dev, const capture::UiFrame& frame,
              nrhi::TextureView* world, const float (*ramp)[3],bool world_is_display=false);
  nrhi::Texture* color() const { return output_; }
  void Gap() { previous_frame_ = 0; }
  void Release(nrhi::Device* dev);
  void Retry(nrhi::Device* dev) { Release(device_==dev ? dev : nullptr); }
  bool failed() const { return failed_ || textures_.latched(); }
  // The screen rectangles {kind,x0,y0,x1,y1} of the last natively composed
  // frame's draws (1280x720 window space, clipped by each draw's scissor), for
  // the readback comparison's UI-only regions. Diagnostic only.
  struct DrawRect { uint32_t kind; float x0, y0, x1, y1; };
  const std::vector<DrawRect>& last_rects() const { return last_rects_; }
  uint64_t last_rects_frame() const { return last_rects_frame_; }
  // Diagnostic coverage plane (R8, 1280x720): the sprite/text draws' alpha
  // accumulated with over blending. Rendered only while wanted (the readback
  // dump); null otherwise.
  void SetCoverageWanted(bool wanted) { coverage_wanted_ = wanted; }
  nrhi::Texture* coverage() const { return coverage_wanted_ ? coverage_ : nullptr; }
 private:
  std::vector<DrawRect> last_rects_;
  uint64_t last_rects_frame_ = 0;
  bool coverage_wanted_ = false;
  nrhi::Texture* coverage_ = nullptr;
  nrhi::Shader* coverage_ps_[2]{};
  nrhi::Pipeline* coverage_pipeline_[2]{};
  bool Ensure(nrhi::Device* dev);
  nrhi::Pipeline* Pipeline(const capture::UiDraw& draw);
  nrhi::Device* device_ = nullptr;
  nrhi::BindingLayout* layout_ = nullptr;
  nrhi::Shader* vs_ = nullptr;
  nrhi::Shader* full_vs_ = nullptr;
  nrhi::Shader* mask_vs_=nullptr;
  nrhi::Shader* mask_ps_=nullptr;
  nrhi::Pipeline* mask_pipeline_=nullptr;
  // One 640x360 scratch target stands for the game's shared EDRAM mask target:
  // each mask's clear rectangle and copy quad draw into it in order, then it
  // is resolved into that mask's own texture (masks_[m]).
  nrhi::Shader* mask_clear_ps_=nullptr;
  nrhi::Shader* mask_resolve_ps_=nullptr;
  nrhi::Pipeline* mask_clear_opaque_=nullptr;
  nrhi::Pipeline* mask_clear_blend_=nullptr;
  nrhi::Pipeline* mask_resolve_=nullptr;
  nrhi::Texture* mask_scratch_=nullptr;
  nrhi::TextureView* mask_scratch_view_=nullptr;
  nrhi::Texture* masks_[capture::kUiMaxMasks]{};
  nrhi::TextureView* mask_views_[capture::kUiMaxMasks]{};
  nrhi::Shader* ps_[capture::kUiKindCount]{};
  nrhi::Pipeline* copy_ = nullptr;
  nrhi::Pipeline* history_ = nullptr;
  nrhi::Pipeline* display_ = nullptr;
  nrhi::Shader* full_ps_[3]{};
  struct Entry { PipelineKey key; bool depth_clip; nrhi::Pipeline* pipeline; };
  std::vector<Entry> pipelines_;
  nrhi::Texture* target_ = nullptr;
  nrhi::TextureView* target_view_ = nullptr;
  nrhi::Texture* front_[2]{};
  nrhi::TextureView* front_view_[2]{};
  nrhi::Texture* output_ = nullptr;
  nrhi::Texture* depth_ = nullptr;
  nrhi::Texture* white_ = nullptr;
  nrhi::TextureView* white_view_ = nullptr;
  bool white_uploaded_ = false;
  bool failed_ = false;
  uint64_t previous_frame_ = 0;
  uint32_t front_write_ = 0;
  std::string last_reason_;
  uint32_t log_lines_ = 0;
  uint32_t diagnostic_modes_ = 0;
  TextureCache textures_{64ull << 20};
};
}  // namespace fable2::native::render
