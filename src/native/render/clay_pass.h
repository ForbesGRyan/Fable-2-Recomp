#pragma once

// "Clay" rendering of the captured main scene into a native 1120x720 target
// (R8G8B8A8 color + D32 depth). Vertices are pulled from the geometry cache's
// StructuredBuffers by SV_VertexID and transformed by each draw's captured
// rows. With textures on, draws whose capture resolved an albedo and UVs
// sample it (texture cache, unlit, softened facet shade); terrain records are
// drawn with the terrain material (the layer's detail colour, on steep ground
// mixed with its side texture, weighted by its weight cell and the distance
// fade, or the far colour; clay_logic.h, "Terrain
// material"), a ground square as a whole or not at all (PlanTerrain); every
// other draw keeps the flat clay colour. Each draw uses the game's own render state
// (DrawRecord::state_words): blend, colour mask, depth and cull through a
// pipeline of a small cache keyed by state (PipelineKey), the alpha test as a
// discard in the textured pixel shader (DecideDrawState has the rules for a
// draw whose alpha or colour is not known). GPU (command-processor) thread
// only; Cmd is valid only inside the SDK's guest-output callbacks.

#include <array>
#include <cstdint>
#include <utility>
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

  // Targets, binding layout, shaders (the four pixel shaders: kClayPs for flat
  // draws, kClayTexturedPs for textured ones, kClayTerrainLayerPs and
  // kClayTerrainFarPs for terrain), their four default pipelines and the
  // default pipeline of a flat terrain draw. False on any failure (logged);
  // call again to retry.
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
    nrhi::Pipeline* pipeline = nullptr;  // by the record's render state; nullptr: not drawn
    ClayAlpha alpha;
    // A terrain record. With its material (`textured` 2 or 3): `albedo` is the
    // colour texture (tf0 or tf13) with `sampler` and `uv_fix`, `weights` the
    // layer's weight cells, `uvs` a far draw's colour coordinates, `slot` its
    // constants in the frame's terrain constant buffer. A layer with the
    // steep-ground block also has `side`, its side texture with `side_sampler`
    // and `side_fix`, and in `uvs` the vertex normals of its grid points.
    // `status`: kTextured when the material is usable, else why not
    // (TerrainMaterialStatus, TerrainSteepStatus).
    bool terrain = false;
    uint8_t textured = 0;  // PipelineKey::textured when drawn with the terrain material
    nrhi::TextureView* weights = nullptr;
    nrhi::TextureView* side = nullptr;
    uint32_t weight_sampler = 0, side_sampler = 0, plan = 0, slot = 0;
    float side_fix[2] = {1.0f, 1.0f};
    capture::MaterialStatus status = capture::MaterialStatus::kTerrain;
  };
  // A texture resolved for a terrain layer this frame (many layers share a few).
  struct TerrainTexture {
    uint32_t fetch[6] = {};
    nrhi::TextureView* view = nullptr;
    uint32_t sampler = 0;
    float uv_fix[2] = {1.0f, 1.0f};
    capture::MaterialStatus status = capture::MaterialStatus::kTextureBad;
  };

  void DestroyObjects();
  nrhi::Pipeline* CreatePipeline(nrhi::Device* dev, const PipelineKey& key);
  // The cached pipeline of `key`, created on first use; nullptr when the cache
  // is full or the pipeline could not be created.
  nrhi::Pipeline* TryPipeline(nrhi::Device* dev, const PipelineKey& key);
  // TryPipeline, or else (counted in st.pipeline_overflow) `fallback`, by
  // default the default pipeline of the key's `textured`.
  nrhi::Pipeline* PipelineFor(nrhi::Device* dev, const PipelineKey& key, ClayStats& st,
                              nrhi::Pipeline* fallback = nullptr);
  bool CreateWhite(nrhi::Device* dev);
  // Terrain records (Render). PrepareTerrain: geometry, and with textures on the
  // textures, far coordinates or vertex normals, pipeline and status of the record's material
  // (a record whose state cannot be drawn as captured, or that finds no
  // pipeline, has none); adds its entry to prepared_ and terrain_items_.
  // FinishTerrain, after every record:
  // which records are drawn with the material, as flat clay or not at all
  // (PlanTerrain), their pipelines, constants and counters.
  void PrepareTerrain(nrhi::Cmd* cmd, nrhi::Device* dev, const capture::DrawRecord& r, bool textured,
                      ClayStats& st, TextureStats& tst);
  void FinishTerrain(nrhi::Device* dev, bool textured, ClayStats& st, TextureStats& tst);
  // The texture behind a fetch constant, through the texture cache; with `memo`
  // looked up once per frame (terrain_textures_).
  nrhi::TextureView* ResolveTerrainTexture(nrhi::Cmd* cmd, nrhi::Device* dev, const uint32_t fetch[6], bool memo,
                                           uint32_t* sampler, float* uv_fix, capture::MaterialStatus* status,
                                           TextureStats& tst);

  nrhi::Device* device_ = nullptr;
  bool ready_ = false;
  uint64_t targets_generation_ = 0;
  nrhi::BindingLayout* layout_ = nullptr;  // the RHI has no layout destruction
  nrhi::Shader* vs_ = nullptr;
  // By PipelineKey::textured: kClayPs, kClayTexturedPs, kClayTerrainLayerPs, kClayTerrainFarPs.
  nrhi::Shader* ps_[kClayPixelShaders] = {};
  // The pipeline cache: pipelines_[i] draws pipeline_keys_[i] (nullptr: its
  // creation failed). Slots 0 to 3 are the default pipelines of `textured` 0
  // to 3, slot 4 that of a flat terrain draw (TerrainFlatDefaultKey; Ensure).
  // At most kMaxClayPipelines, never evicted within a run.
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
  // Terrain of the frame being rendered: one plan item per terrain record
  // (Prepared::plan), the layers' textures, the texture sets bound, and the
  // constant buffer (one kClayTerrainSlotBytes slot per terrain record,
  // persistently mapped). The buffer is in use until the frame's submission has
  // completed, so each frame takes one from the pool or a new one and returns it
  // at the end of Render.
  static constexpr uint64_t kTerrainPoolBytes = 4ull << 20;
  std::vector<TerrainPlanItem> terrain_items_;
  TerrainPlanScratch terrain_scratch_;  // PlanTerrain's storage, kept between frames
  std::vector<TerrainTexture> terrain_textures_;
  std::vector<std::array<nrhi::TextureView*, 3>> terrain_sets_;  // colour, weights, side
  RetirePool<nrhi::Buffer*> terrain_pool_{kTerrainPoolBytes};
  std::vector<nrhi::Buffer*> terrain_destroy_;
  nrhi::Buffer* terrain_cb_ = nullptr;
  uint64_t terrain_cb_bytes_ = 0;
};

}  // namespace fable2::native::render
