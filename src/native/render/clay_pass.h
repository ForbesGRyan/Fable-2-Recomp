#pragma once

// "Clay" rendering of the captured main scene into native 1120x720 targets:
// linear colour (R11G11B10_FLOAT) with D32 depth, then one full-screen
// output mapping, display = sqrt(saturate(linear)), into the 8-bit
// target (R8G8B8A8) the composite views sample (clay_logic.h, "Linear target
// and output mapping"). Vertices are pulled from the geometry cache's
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
// draw whose alpha or colour is not known). With the light on, in a frame
// that has a light block, the terrain, the objects (with a colour stream or
// with probe colours) and the tree trunks are lit by the game's ambient, baked
// and sun light (clay_logic.h, "Native light"); every other draw is the unlit
// colour. With the light on, in a frame that has a valid shadow block
// (FrameScene::shadow), the game's two sun shadow maps are drawn first, from
// the frame's shadow records into two depth targets of the pass's own
// (clay_logic.h, "Native shadow maps"); nothing samples them yet but the
// shadow view (composite.h). GPU (command-processor) thread
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
  // default pipeline of a flat terrain draw, and the output mapping's layout,
  // shaders and pipeline. False on any failure (logged); call again to retry.
  bool Ensure(nrhi::Device* dev);
  // Bumped each time Ensure creates new targets (all three together): their
  // contents are undefined until the next Render, which writes every pixel of
  // the linear target (clear, draws) and then every pixel of the mapped one.
  uint64_t targets_generation() const { return targets_generation_; }
  // `textured`: draws may sample their albedo (fable2_native_textures). tst
  // receives the texture counters and the final material status of every
  // drawn record (untouched when textured is false). `light`:
  // fable2_native_light. On, and the scene has a light block the pass can use
  // (MakeClayFrameConstants): the families the pass lights are drawn lit, their
  // values times the frame's exposure. Every other draw, and every draw of a
  // frame without light, is the unlit colour. st receives what was lit and what was not,
  // by reason (ClayStats::light_drawn, light_unlit).
  void Render(nrhi::Cmd* cmd, nrhi::Device* dev, const FrameScene& scene, ClayColor color,
              bool textured, ClayLight light, ClayStats& st, TextureStats& tst);
  // The mapped target (display values, 8 bits): what the composite views
  // sample. Left in kPixelShaderResource after Render, and written by the same
  // Render that drew the linear target.
  nrhi::Texture* color() const { return color_; }
  nrhi::Texture* depth() const { return depth_; }  // left in kPixelShaderResource after Render
  // The two shadow maps (0 near, 1 far): kShadowMapSize squared, D32_FLOAT, viewed as R32_FLOAT;
  // depth in the pass's direction (clay_logic.h, "Native shadow maps": 0 at the light, 1 where no
  // caster is). Created and destroyed with the pass's other targets, whatever the window's size.
  // In kPixelShaderResource whenever Render is not running: Render binds one as a depth target
  // only while it draws that map, before anything of the scene is drawn, and no draw of the pass
  // samples a map while one is bound for writing.
  // shadow_maps_drawn: the last Render cleared and drew both for its scene. False: their contents
  // are another frame's or undefined (new targets, no valid shadow block, the light or the
  // shadows off) and must not be shown or sampled.
  nrhi::TextureView* shadow_view(size_t cascade) const { return cascade < 2 ? shadow_view_[cascade] : nullptr; }
  bool shadow_maps_drawn() const { return shadow_drawn_; }
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
    // (TerrainMaterialStatus, TerrainSteepStatus). `no_pipeline`: it was usable
    // but for a pipeline of its state (counted in the pipeline overflow).
    bool terrain = false, no_pipeline = false;
    uint8_t textured = 0;  // PipelineKey::textured when drawn with the terrain material
    nrhi::TextureView* weights = nullptr;
    nrhi::TextureView* side = nullptr;
    uint32_t weight_sampler = 0, side_sampler = 0, plan = 0, slot = 0;
    float side_fix[2] = {1.0f, 1.0f};
    capture::MaterialStatus status = capture::MaterialStatus::kTerrain;
    // The light's vertex inputs of a record that has light inputs (`light`:
    // LightOf) and its material: `normals` (a mesh record: float3,
    // GeometryCache::Normals; a terrain record: float2 (n.x, n.y) per grid
    // point, GeometryCache::TerrainNormals, which a layer with the steep-ground
    // block has with or without light inputs), and the second buffer of the
    // record's vertex kind: `colours` for the family with a colour stream
    // (GeometryCache::Colours) and for an instanced record (the copies'
    // colours per flat position, GeometryCache::InstanceColours), `probe` for
    // the probe kind (GeometryCache::Probe: the weights from byte 0, the set
    // indices from ProbeSetOffset(probe_count) on), and for an instanced
    // record also `bend` (the inputs of the plants' bent normal per flat
    // position, GeometryCache::InstanceBend). For a mesh record the normals
    // and its kind's other buffers or none of them: nullptr when one could not
    // be built (counted in ClayStats::light_missing; `light_why`).
    // Only a lit record's buffers are bound (`lit`, below): a colour stream's
    // colours at t10; the probe buffer's set indices at t10 (from
    // ProbeSetOffset(probe_count)) and its weights at t11; an instanced
    // plant's colours at t10 and its bend inputs at t11.
    // `light_count`: the elements the buffers hold, the smallest of the
    // normals' count and the other buffers' (the shader reads them for a
    // vertex below it). `probe_count`: the vertices the probe buffer was built with, which
    // is where its second region starts. `max_index`: the largest index of the
    // record's list.
    const capture::LightInputs* light = nullptr;
    nrhi::Buffer* normals = nullptr;
    nrhi::Buffer* colours = nullptr;
    nrhi::Buffer* probe = nullptr;
    nrhi::Buffer* bend = nullptr;
    uint32_t light_count = 0, max_index = 0, probe_count = 0;
    capture::LightReason light_why = capture::LightReason::kLightUnknown;
    // `lit`: the record is drawn lit by its family's function (the frame is
    // lit, the family is one the pass lights, everything it reads is there,
    // and a terrain layer's square is lit as a whole). A lit terrain record has
    // its base map in `base` with `base_sampler` and `base_fix`, and the
    // light's constants in its slot; a lit object's or trunk's constants are
    // entry `slot` of object_constants_, a lit plant's of plant_constants_.
    // `unlit`: why not, for a record that is not lit in a lit frame.
    bool lit = false;
    ClayUnlit unlit = ClayUnlit::kUnknown;
    nrhi::TextureView* base = nullptr;
    uint32_t base_sampler = 0;
    float base_fix[2] = {1.0f, 1.0f};
  };
  // One shadow record of the frame that is drawn into its map, with its buffers, its texture (a
  // caster whose alpha decides; else null) and its root constants (MakeShadowConstants).
  struct ShadowPrepared {
    nrhi::Buffer* positions = nullptr;
    nrhi::Buffer* indices = nullptr;
    nrhi::Buffer* uvs = nullptr;
    nrhi::TextureView* albedo = nullptr;
    nrhi::Pipeline* pipeline = nullptr;
    uint32_t index_count = 0;
    uint64_t vs_hash = 0;
    ClayConstants constants;
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
  // Terrain records (Render; `terrain`: the record's inputs in the scene's side
  // array, TerrainOf). PrepareTerrain: geometry, and with textures on the
  // textures, far coordinates or vertex normals, pipeline and status of the record's material
  // (a record whose state cannot be drawn as captured, or that finds no
  // pipeline, has none); adds its entry to prepared_ and terrain_items_.
  // FinishTerrain, after every record:
  // which records are drawn with the material, as flat clay or not at all
  // (PlanTerrain), their pipelines, constants and counters.
  // `light`: the record's light inputs (LightOf), null without them and with the light off; a record
  // with them and a usable material also gets its vertex normals (Prepared::normals).
  // In a lit frame a record with light inputs of a family the pass lights also
  // gets its base map, and its plan item says whether its light can be drawn.
  // FinishTerrain also takes the frame's constant buffer (frame_cb_), marks
  // the lit records (a square as a whole: PlanTerrain), writes every slot and
  // counts the terrain records' light inputs (after the plan: a record whose
  // square fell back is not counted).
  void PrepareTerrain(nrhi::Cmd* cmd, nrhi::Device* dev, const capture::DrawRecord& r,
                      const capture::TerrainPatch& terrain, const capture::LightInputs* light, bool textured,
                      ClayStats& st, TextureStats& tst);
  void FinishTerrain(nrhi::Device* dev, bool textured, ClayStats& st, TextureStats& tst);
  // The shadow maps (Render). PrepareShadows, with the scene's other records and before any target
  // is bound (a caster's texture may be uploaded here): the frame's shadow counters, and, when
  // `draw` (the frame is drawn with light and its shadow block is valid), for every shadow record
  // what DecideShadowDraw says, its geometry and, for a caster whose alpha decides, its
  // coordinates and texture; a record that lacks any of it is counted by reason and vertex shader
  // and not drawn (never as a solid caster). DrawShadows, after FinishTerrain (the frame's constant
  // buffer) and before the scene's targets are bound: clears both maps and draws the prepared
  // records, each map bound as the depth target for its own draws only, and leaves both in
  // kPixelShaderResource. Without `draw` neither map is touched and shadow_drawn_ is false.
  void PrepareShadows(nrhi::Cmd* cmd, nrhi::Device* dev, const FrameScene& scene, bool draw, ClayStats& st, TextureStats& tst);
  void DrawShadows(nrhi::Cmd* cmd, ClayStats& st);
  // The light's vertex inputs of a textured mesh record with light inputs:
  // its normals and the other buffers of its vertex kind (a colour stream's
  // colours, the probe weights and sets, an instance set's colours and bend
  // inputs), from the geometry cache into `p`; counts the record in st (lit,
  // light_ready or light_missing by reason and vertex shader, light_second,
  // light_bend, light_rotation). In a lit frame a record of a family the pass
  // lights whose constants can be made (MakeClayObjectConstants) is marked lit
  // and gets its entry of object_constants_ (an object or trunk) or of
  // plant_constants_ (an instanced plant).
  void PrepareLight(nrhi::Device* dev, const capture::DrawRecord& r, const capture::LightInputs& light, Prepared& p,
                    ClayStats& st);
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
  // The three targets, created and destroyed together (Ensure, DestroyObjects):
  // linear_ is what the draws render into (with depth_), color_ what the output
  // mapping writes from it at the end of each Render. linear_view_ is the
  // mapping's view of linear_; the mapping has its own layout (its constants,
  // word 0 always 1, in root constants, the linear target at t0), shaders and pipeline, outside the
  // pipeline cache.
  nrhi::Texture* linear_ = nullptr;
  nrhi::TextureView* linear_view_ = nullptr;
  nrhi::Texture* color_ = nullptr;
  nrhi::Texture* depth_ = nullptr;
  // The two shadow maps with their views, the shadow pixel shader (kClayShadowPs, drawn with vs_
  // under layout_) and the three depth-only pipelines, one a cull mode (ShadowPipelineSlot): made
  // and destroyed with the targets above. shadow_wanted_: this Render draws the maps;
  // shadow_drawn_: the last one did. shadow_prepared_: the records it draws, per map.
  nrhi::Texture* shadow_map_[2] = {};
  nrhi::TextureView* shadow_view_[2] = {};
  nrhi::Shader* shadow_ps_ = nullptr;
  nrhi::Pipeline* shadow_pipelines_[kShadowPipelines] = {};
  bool shadow_wanted_ = false, shadow_drawn_ = false;
  std::vector<ShadowPrepared> shadow_prepared_[2];
  nrhi::BindingLayout* map_layout_ = nullptr;  // the RHI has no layout destruction
  nrhi::Shader* map_vs_ = nullptr;
  nrhi::Shader* map_ps_ = nullptr;
  nrhi::Pipeline* map_pipeline_ = nullptr;
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
  // frame's constant buffer, persistently mapped: slots of
  // kClayTerrainSlotBytes, slot 0 the frame's light (ClayFrameConstants,
  // cbuffer Frame), then one per terrain record drawn with its material
  // (cbuffer Terrain), then one per lit plant (the first bytes of cbuffer
  // Object; ClaySmallSlotOffset); behind them one slot of kClayObjectSlotBytes
  // per lit object or trunk (cbuffer Object; ClayObjectSlotOffset). The buffer is in use until the frame's
  // submission has completed, so each frame takes one from the pool or a new
  // one and returns it at the end of Render.
  static constexpr uint64_t kTerrainPoolBytes = 4ull << 20;
  std::vector<TerrainPlanItem> terrain_items_;
  TerrainPlanScratch terrain_scratch_;  // PlanTerrain's storage, kept between frames
  std::vector<TerrainTexture> terrain_textures_;
  std::vector<std::array<nrhi::TextureView*, 4>> terrain_sets_;  // colour, weights, side, base
  RetirePool<nrhi::Buffer*> terrain_pool_{kTerrainPoolBytes};
  std::vector<nrhi::Buffer*> terrain_destroy_;
  nrhi::Buffer* terrain_cb_ = nullptr;
  uint64_t terrain_cb_bytes_ = 0;
  // The light of the frame being rendered: its constants (light_on 0: no draw
  // is lit), the lit objects' and trunks' constants and the lit plants', each
  // in prepare order (Prepared::slot), the small slot of the first plant
  // (ClaySmallSlotOffset(plant_slot0_ + slot) is a plant's) and the number of
  // small slots in use before the first object (the frame's, the terrain's and
  // the plants': ClayObjectSlotOffset(object_slot0_, slot) is an object's).
  ClayFrameConstants frame_{};
  std::vector<ClayObjectConstants> object_constants_;
  std::vector<ClayObjectConstants> plant_constants_;
  uint32_t object_slot0_ = 0, plant_slot0_ = 0;
  bool no_constants_logged_ = false;
};

}  // namespace fable2::native::render
