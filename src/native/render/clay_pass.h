#pragma once

// "Clay" rendering of the captured main scene into a native 1120x720 target
// (R8G8B8A8 color + D32 depth). Vertices are pulled from the geometry cache's
// StructuredBuffers by SV_VertexID and transformed by each draw's captured
// rows. With textures on, draws whose capture resolved an albedo and UVs
// sample it (texture cache, unlit, softened facet shade); every other draw
// keeps the flat clay colour. Each draw uses the game's own render state
// (DrawRecord::state_words): blend, colour mask, depth and cull through a
// pipeline of a small cache keyed by state (PipelineKey), the alpha test as a
// discard in the textured pixel shader (DecideDrawState has the rules for a
// draw whose alpha or colour is not known). GPU (command-processor) thread
// only; Cmd is valid only inside the SDK's guest-output callbacks.

#include <cstdint>
#include <vector>

#include <rex/graphics/native_rhi.h>

#include "clay_logic.h"
#include "frame_scene.h"
#include "geometry_cache.h"
#include "texture_cache.h"

namespace fable2::native::render {

class ClayPass {
 public:
  static constexpr uint32_t kWidth = 1120;
  static constexpr uint32_t kHeight = 720;

  ClayPass(uint64_t geometry_budget_bytes, uint64_t texture_budget_bytes)
      : geometry_(geometry_budget_bytes), textures_(texture_budget_bytes) {}

  // Targets, binding layout, shaders (both pixel shaders: kClayPs for flat
  // draws, kClayTexturedPs for textured ones) and the two default pipelines.
  // False on any failure (logged); call again to retry.
  bool Ensure(nrhi::Device* dev);
  // Bumped each time Ensure creates new targets: their contents are
  // undefined until the next Render.
  uint64_t targets_generation() const { return targets_generation_; }
  // `textured`: draws may sample their albedo (fable2_native_textures). tst
  // receives the texture counters and the final material status of every
  // drawn record (untouched when textured is false).
  void Render(nrhi::Cmd* cmd, nrhi::Device* dev, const FrameScene& scene, ClayColor color,
              bool textured, ClayStats& st, TextureStats& tst);
  nrhi::Texture* color() const { return color_; }  // left in kPixelShaderResource after Render
  nrhi::Texture* depth() const { return depth_; }  // likewise
  GeometryCache& geometry() { return geometry_; }
  TextureCache& textures() { return textures_; }

 private:
  // One drawable record of the frame, with its buffers and albedo.
  struct Prepared {
    const capture::DrawRecord* r = nullptr;
    nrhi::Buffer* positions = nullptr;
    nrhi::Buffer* indices = nullptr;
    nrhi::Buffer* uvs = nullptr;  // nullptr: flat clay
    nrhi::TextureView* albedo = nullptr;
    uint32_t vertex_count = 0, index_count = 0, sampler = 0;
    float uv_fix[2] = {1.0f, 1.0f};
    nrhi::Pipeline* pipeline = nullptr;  // by the record's render state
    ClayAlpha alpha;
  };

  void DestroyObjects();
  nrhi::Pipeline* CreatePipeline(nrhi::Device* dev, const PipelineKey& key);
  // The cached pipeline of `key`, created on first use. The default pipeline
  // of the key's `textured` (counted in st.pipeline_overflow) when the cache
  // is full or the pipeline could not be created.
  nrhi::Pipeline* PipelineFor(nrhi::Device* dev, const PipelineKey& key, ClayStats& st);
  bool CreateWhite(nrhi::Device* dev);

  nrhi::Device* device_ = nullptr;
  bool ready_ = false;
  uint64_t targets_generation_ = 0;
  nrhi::BindingLayout* layout_ = nullptr;  // the RHI has no layout destruction
  nrhi::Shader* vs_ = nullptr;
  nrhi::Shader* ps_[2] = {};  // by PipelineKey::textured: kClayPs, kClayTexturedPs
  // The pipeline cache: pipelines_[i] draws pipeline_keys_[i] (nullptr: its
  // creation failed). Slots 0 and 1 are the default pipelines of `textured` 0
  // and 1 (Ensure). At most kMaxClayPipelines, never evicted within a run.
  std::vector<PipelineKey> pipeline_keys_;
  std::vector<nrhi::Pipeline*> pipelines_;
  nrhi::Texture* color_ = nullptr;
  nrhi::Texture* depth_ = nullptr;
  // 1x1 white texture bound at t3 for untextured draws; its contents are
  // copied from white_staging_ by the first Render after creation.
  nrhi::Texture* white_ = nullptr;
  nrhi::TextureView* white_view_ = nullptr;
  nrhi::Buffer* white_staging_ = nullptr;
  GeometryCache geometry_;
  TextureCache textures_;
  std::vector<Prepared> prepared_;
};

}  // namespace fable2::native::render
