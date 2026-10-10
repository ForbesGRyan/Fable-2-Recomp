#pragma once

// "Clay" rendering of the captured main scene into native 1120x720 targets:
// linear colour (R11G11B10_FLOAT) with D32 depth, then one full-screen
// output mapping, display = sqrt(saturate(linear)), into the 8-bit
// target (R8G8B8A8) the composite views sample (clay_logic.h, "Linear target
// and output mapping"); or, in a post frame (fable2_native_post; clay_logic.h,
// "Native post"), the game's composite into an 8-bit front target and the
// scan-out ramp from it into that same mapped target. Vertices are pulled from the geometry cache's
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
// colour. With the light and the shadows' switch on, a frame that has a valid
// shadow block (FrameScene::shadow) and everything else its mask needs
// (clay_logic.h, DecideClayMask) has its sun shadowed as the game shadows it
// (clay_logic.h, "Native shadow maps", "Native shadows"): the game's two sun
// shadow maps are drawn first, from the frame's shadow records into two depth
// targets of the pass's own; the scene's depth-writing records are drawn once
// depth-only; one full-screen step writes the screen mask from that depth and
// the two maps with the game's mask arithmetic; and the colour pass, drawn as
// always from a cleared depth, reads the mask at each lit pixel. In every
// other frame neither the maps nor the mask are drawn and the mask is the
// 1 x 1 white texture: the lighting picture. Everything the
// shadows alone need (the maps, the mask target, their shaders and
// pipelines) is made on the first frame that wants it; if that fails the
// shadows are off until the pass is rebuilt (another device, an F6 retry after
// a failure of the renderer) and the pass draws on without them. GPU
// (command-processor) thread only; Cmd is valid only inside the SDK's
// guest-output callbacks.

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include <rex/graphics/native_rhi.h>

#include "clay_logic.h"
#include "frame_scene.h"
#include "geometry_cache.h"
#include "post_oracle.h"
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
  // by reason (ClayStats::light_drawn, light_unlit). `shadows`: fable2_native_shadows.
  // On, in a frame drawn with light whose shadow block is valid and whose mask can
  // be drawn (DecideClayMask): the maps, the pre-pass and the mask are drawn and
  // the lit draws' sun is shadowed. Otherwise none of it is drawn and the mask is
  // white; st says which (ClayStats::mask). `fog`: fable2_native_fog as it is read now. On, in a
  // frame drawn with light that has a valid fog block, an eye and one inverse view-projection
  // (DecideClayFog): the frame's constants carry the fog's tail, every drawn record is fogged as the
  // game's own pixel shader fogs it or not (DrawFogOf: lit and unlit draws), and the sky is drawn.
  // Otherwise the tail is zero and no pixel is; st says which (ClayStats::fog). `post`:
  // fable2_native_post as it is read now, and the frame's scan-out ramp. On, in a frame drawn with
  // light whose scene has a post block that pairs with its c20.w, with a usable ramp, the post steps'
  // objects and the frame's constant buffer (DecideClayPost): a POST FRAME, whose lit draws write the
  // game's own scale, whose unlit draws write the frame's unlit scale, and which ends in the game's
  // composite and the ramp. Every other frame is today's: the fixed exposure, the unlit word 1, the
  // output mapping; st says which and why (ClayStats::post). With `post.sun` (fable2_native_sun), in a
  // post frame with a sky whose scene has a whole set of quad records, the scene's furniture is drawn
  // from them: the moon after the sky step, the glare after the last record (ClayFurniture; st says
  // which and why). With it off nothing of the quad step is made, bound or drawn. With it, in a frame
  // whose furniture is drawn, whose bloom is on and whose post block has the radial blur's registers, the
  // sun layer is drawn from the set's layer records and feeds the bright pass (ClaySun; st says which
  // and why, "sun-layer"); in every other frame the bright pass's sun flag is 0 and nothing of the layer
  // is made, bound or drawn.
  void Render(nrhi::Cmd* cmd, nrhi::Device* dev, const FrameScene& scene, ClayColor color,
              bool textured, ClayLight light, bool shadows, bool fog, const ClayPostRequest& post, ClayStats& st,
              TextureStats& tst);
  // The mapped target (display values, 8 bits): what the composite views
  // sample. Left in kPixelShaderResource after Render, and written by the same
  // Render that drew the linear target (by the output mapping, or by the ramp
  // step of a post frame: every Render writes every pixel of it).
  nrhi::Texture* color() const { return color_; }
  nrhi::Texture* depth() const { return depth_; }  // left in kPixelShaderResource after Render
  // The two shadow maps (0 near, 1 far): kShadowMapSize squared, D32_FLOAT, viewed as R32_FLOAT;
  // depth in the pass's direction (clay_logic.h, "Native shadow maps": 0 at the light, 1 where no
  // caster is). Made on the first frame that draws them (null before, and when making them
  // failed) and destroyed with the pass's other targets; their size is not the window's.
  // In kPixelShaderResource whenever Render is not running: Render binds one as a depth target
  // only while it draws that map, before anything of the scene is drawn, and no draw of the pass
  // samples a map while one is bound for writing.
  // shadow_maps_drawn: the last Render cleared and drew both for its scene. False: their contents
  // are another frame's or undefined (new targets, or a frame whose mask is not drawn: the light or
  // the shadows off, no valid shadow block, no light block, no view) and must not be shown or sampled.
  nrhi::TextureView* shadow_view(size_t cascade) const { return cascade < 2 ? shadow_view_[cascade] : nullptr; }
  bool shadow_maps_drawn() const { return shadow_drawn_; }
  // The screen mask (R8, the clay target's size; 1 = lit by the sun), made with the maps. In
  // kPixelShaderResource whenever Render is not running. mask_drawn: the last Render wrote it for
  // its scene; false: its contents are another frame's or undefined and must not be shown.
  nrhi::TextureView* mask_view() const { return mask_view_; }
  bool mask_drawn() const { return mask_drawn_; }
  // False once making the shadows' objects has failed, until the pass is rebuilt (Ensure on another
  // device or after a failure): the pass then draws no shadows, and the capture need record none.
  bool shadows_usable() const { return !shadow_objects_failed_; }
  GeometryCache& geometry() { return geometry_; }
  TextureCache& textures() { return textures_; }
  // The oracle feed (post_oracle.h; fable2_native_post_oracle: a diagnostic, spec ruling 17). `feed`: a
  // parsed scene and block, the caller's, which outlive every Render; null (as it is unless the switch is
  // set and both files were valid): nothing of the feed is reached. With a feed, in a POST FRAME only
  // (every rule of a frame without post stands), the scene is uploaded once as a kWidth x kHeight
  // R16G16B16A16_FLOAT texture and the composite (with the bloom also the downsample: every step that reads
  // the scene) reads it in place of the linear target, with the steps'
  // constants made of the feed's block and the frame's own ramp; the scene's draws are drawn as always and
  // nothing reads them. In the frame kOracleDumpAfter fed frames after the upload's the mapped target is
  // copied to a read-back buffer, once in the pass's life. A failure (the texture, the buffer) turns the
  // feed off until the pass is rebuilt, with one log line; the renderer goes on from its own scene.
  void SetPostOracle(const OracleFeed* feed) { oracle_ = feed; }
  // The dump of the mapped target: true once, with `file` the dump file's bytes (PackOracleDump), when the
  // copy's submission has completed; false in every other call (none pending, not yet completed, another
  // device, or the buffer could not be read: logged). Command-processor thread, outside Render.
  bool TakePostOracleDump(nrhi::Device* dev, std::vector<uint8_t>* file);
  // Whether the dump is over: taken, or failed (its buffer could not be made or read): nothing more
  // comes, and the caller need not ask again (review of plan Task 6, Minor 8).
  bool PostOracleDumpDone() const { return oracle_dump_state_ == OracleDump::kDone; }
  // The sun layer's pictures of the dump's frame, when the feed has a sun part and the layer was drawn from
  // it (plan Task 14): `index` 0 layer 1 (kWidth / 2 x kHeight / 2; its alpha is set where the beam source
  // was drawn), 1 the bright pass's tf1, 2 its tf2 (kWidth / 4 x kHeight / 4), each as PackOracleDump's
  // bytes with the magic "F2OL" (the words of R10G10B10A2_UNORM). True once for each, when the copy's
  // submission has completed; false in every other call. Command-processor thread, outside Render.
  bool TakePostOracleLayerDump(nrhi::Device* dev, size_t index, std::vector<uint8_t>* file);

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
    // The record's pipeline of the depth pre-pass (PrepassKey of its colour pipeline's key); nullptr:
    // the record writes no depth, the frame has no pre-pass, or no such pipeline could be made.
    nrhi::Pipeline* prepass = nullptr;
    PositionBounds bounds;      // the box of its position buffer (GeometryCache::Positions)
    bool prepass_culled = false;  // left out of the pre-pass: wholly beyond the mask's reach
    bool prepass_flat = false;  // its pre-pass draw is the flat kind's (PrepassKey with the record's alpha)
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
    // Only a lit record's buffers are bound (`lit`, below; the normals at t10):
    // a colour stream's colours at t11; the probe buffer's set indices at t11
    // (from ProbeSetOffset(probe_count)) and its weights at t12; an instanced
    // plant's colours at t11 and its bend inputs at t12.
    // `light_count`: the elements the buffers hold, the smallest of the
    // normals' count and the other buffers' (the shader reads them for a
    // vertex below it). `probe_count`: the vertices the probe buffer was built with, which
    // is where its second region starts. `max_index`: the largest index of the
    // record's list.
    const capture::LightInputs* light = nullptr;
    // The light inputs of a mesh record for its FOG, whether or not its light's buffers were asked
    // or built (a record drawn as flat clay has no `light`): an unlit trunk or leaf is fogged with
    // the scale they carry (DrawFogOf). Null with the light off and for a record without inputs.
    const capture::LightInputs* fog_light = nullptr;
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
  // What the shadows alone need: the two maps, their pixel shader and pipelines, the view of the
  // scene's depth, the mask target, the mask step's layout, shader and pipeline, and the pre-pass's
  // depth-only pixel shaders. EnsureShadowObjects makes them on the first frame that wants them;
  // false when they could not be made, now or before: then they are not tried again until the pass
  // is rebuilt (Ensure: another device, or the retry after a failure of the pass; one log line) and
  // nothing of them is left. Destroyed with the pass's other objects.
  bool EnsureShadowObjects(nrhi::Device* dev);
  void DestroyShadowObjects();
  void ForgetShadowObjects();
  // What the sky step alone needs (clay_logic.h, "Native sky"): its vertex and pixel shader, its
  // layout and its pipeline. EnsureSkyObjects makes them on the first frame that draws a sky; false
  // when they could not be made, now or before: then they are not tried again until the pass is
  // rebuilt (one log line) and the frames have no sky (ClaySky::kObjects). Destroyed with the
  // pass's other objects; the sky's two textures are the texture cache's.
  bool EnsureSkyObjects(nrhi::Device* dev);
  void DestroySkyObjects();
  void ForgetSkyObjects();
  // What the post steps alone need (clay_logic.h, "Native post"): their layout, the two pixel shaders,
  // the front target with its view and the two pipelines. EnsurePostObjects makes them on the first
  // frame that could be a post frame; false when they could not be made, now or before: then they
  // are not tried again until the pass is rebuilt (one log line) and every frame is today's
  // (ClayPost::kObjects, "post-objects"): the renderer goes on. Destroyed with the pass's other objects.
  bool EnsurePostObjects(nrhi::Device* dev);
  // The second front target (plan Task 15): made on the first post frame that wants the previous-frame
  // blend; false when it could not be made, now or before (one log line; the blend is then off).
  bool EnsureFrontPrevious(nrhi::Device* dev);
  void DestroyPostObjects();
  void ForgetPostObjects();
  // What the bloom's steps need besides (clay_logic.h, "Native bloom"): three pixel shaders, three
  // targets a quarter of the logical size (the downsample's float one, the bright pass's and the
  // blurs' two 10-bit ones) with their views, three pipelines; the layout is the post steps'.
  // EnsureBloomObjects makes them on the first frame that could be a post frame with
  // fable2_native_bloom on (after EnsurePostObjects); false when they could not be made, now or
  // before: then they are not tried again until the pass is rebuilt (one log line), and every frame
  // with the bloom's switch on is today's ("post-objects"); with the switch off the post goes on
  // without them. Destroyed with the pass's other objects.
  bool EnsureBloomObjects(nrhi::Device* dev);
  void DestroyBloomObjects();
  void ForgetBloomObjects();
  // What the quad step alone needs (clay_logic.h, "Native furniture"): its layout, its vertex and pixel
  // shader and one pipeline for each state a quad is drawn with (ClayQuadState). EnsureQuadObjects makes
  // them on the first frame that draws furniture; false when they could not be made, now or before: then
  // they are not tried again until the pass is rebuilt (one log line) and no frame has furniture
  // (ClayFurniture::kObjects, "post-objects"): the post chain and the renderer go on. With
  // fable2_native_sun off nothing of them is made. Destroyed with the pass's other objects; a quad's
  // texture is the texture cache's.
  bool EnsureQuadObjects(nrhi::Device* dev);
  void DestroyQuadObjects();
  void ForgetQuadObjects();
  // What the sun layer alone needs (clay_logic.h, "Native sun layer"): six programs, the layer's depth
  // target, its two pictures and three quarter-size targets with their views, a view of the scene's depth,
  // and seven pipelines (the depth step, the reduction, the radial blur under the post layout; one a state
  // of ClaySunState under the quad step's layout). EnsureSunObjects makes them on the first frame that
  // draws the layer (the post steps' and the quad step's objects exist then); false when they could not be
  // made, now or before: then they are not tried again until the pass is rebuilt (one log line) and no
  // frame has a sun layer (ClaySun::kObjects, "post-objects"): the bloom, the furniture and the renderer
  // go on. With fable2_native_sun off nothing of them is made. Destroyed with the pass's other objects.
  bool EnsureSunObjects(nrhi::Device* dev);
  void DestroySunObjects();
  void ForgetSunObjects();
  // The oracle feed's depth (OracleFeed::sun): a kWidth x kHeight R32_FLOAT texture filled once from the
  // file (a copy and barriers on `cmd`: before render targets are bound); false when it could not be
  // made, now or before: the feed then has no sun layer. CopyOracleLayerDumps: the layer's three pictures,
  // resting in kPixelShaderResource, into read-back buffers, and back.
  bool EnsureOracleSunDepth(nrhi::Cmd* cmd, nrhi::Device* dev);
  void CopyOracleLayerDumps(nrhi::Cmd* cmd, nrhi::Device* dev);
  // The oracle feed's texture (SetPostOracle): made and filled from the feed's scene on the first fed
  // frame (a copy and barriers on `cmd`: called before render targets are bound); false when it could
  // not be made, now or before (oracle_failed_). CopyOracleDump: the mapped target, resting in
  // kPixelShaderResource, into a new read-back buffer (rows of OracleDumpPitch(kWidth) bytes), and back to
  // its resting state. Both are destroyed with the pass's other objects; a copy still pending then is
  // dropped and made again by the rebuilt pass.
  bool EnsureOracleTexture(nrhi::Cmd* cmd, nrhi::Device* dev);
  void CopyOracleDump(nrhi::Cmd* cmd, nrhi::Device* dev);
  void DestroyOracleObjects();
  void ForgetOracleObjects();
  // The pre-pass pipeline of a colour pipeline (PrepassKey), from the pre-pass's own cache; nullptr
  // when the colour pipeline is not one of the cache or does not write depth, or on a failure.
  nrhi::Pipeline* PrepassPipeline(nrhi::Device* dev, nrhi::Pipeline* colour, const ClayAlpha& alpha, bool* flat);
  // The key of a pipeline of the colour pass's cache; nullptr for any other pointer.
  const PipelineKey* KeyOf(nrhi::Pipeline* pipeline) const;
  // The game's rotation tile as a 32 x 32 R8G8 texture: read from guest memory and checked every
  // frame (TextureCache::ReadRotationTile), uploaded when its bytes change. nullptr: it could not
  // be read or is not the tile, or the upload failed: the mask's taps are then unrotated. Records
  // a copy and barriers on `cmd`: call before render targets are bound.
  nrhi::TextureView* UpdateRotation(nrhi::Cmd* cmd, nrhi::Device* dev);
  // `depth_only`: a pipeline of the pre-pass (ps_ by the key's kind are the colour shaders,
  // depth_ps_ their depth-only forms).
  nrhi::Pipeline* CreatePipeline(nrhi::Device* dev, const PipelineKey& key, bool depth_only = false);
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
  // `draw` (the frame's mask is to be drawn, ClayMapsDrawn: the shadows' and the light's switches,
  // a valid shadow block, a light block, an eye, one view and the shadows' objects), for every shadow record
  // what DecideShadowDraw says, its geometry and, for a caster whose alpha decides, its
  // coordinates and texture; a record that lacks any of it is counted by reason and vertex shader
  // and not drawn (never as a solid caster). DrawShadows, after FinishTerrain (the frame's constant
  // buffer) and before the scene's targets are bound: clears both maps and draws the prepared
  // records, each map bound as the depth target for its own draws only, and leaves both in
  // kPixelShaderResource. Without `draw` neither map is touched and shadow_drawn_ is false.
  void PrepareShadows(nrhi::Cmd* cmd, nrhi::Device* dev, const FrameScene& scene, bool draw, ClayStats& st, TextureStats& tst);
  void DrawShadows(nrhi::Cmd* cmd, ClayStats& st);
  // One prepared record, drawn with its colour pipeline, or (`depth_only`) with its pre-pass
  // pipeline: the same buffers, constants and textures but for the light (none: light_mode 0) and
  // the mask (white). `mask`: what the colour draw binds as the screen mask. `fog_out`: the fog of
  // the colour draw (DrawFogOf; kNone for a pre-pass draw).
  void DrawPrepared(nrhi::Cmd* cmd, const Prepared& p, ClayColor color, bool depth_only, nrhi::TextureView* mask,
                    uint32_t* light_mode_out, DrawFog* fog_out, bool* light_bound);
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
  // by EnsureShadowObjects and destroyed with the targets above. shadow_wanted_: this Render draws
  // the maps; shadow_drawn_: the last one did. shadow_prepared_: the records it draws, per map.
  nrhi::Texture* shadow_map_[2] = {};
  nrhi::TextureView* shadow_view_[2] = {};
  nrhi::Shader* shadow_ps_ = nullptr;
  nrhi::Pipeline* shadow_pipelines_[kShadowPipelines] = {};
  bool shadow_wanted_ = false, shadow_drawn_ = false;
  std::vector<ShadowPrepared> shadow_prepared_[2];
  // The rest of the shadows' objects (EnsureShadowObjects). shadow_objects_: they exist;
  // shadow_objects_failed_: making them failed once (not tried again until Ensure rebuilds the pass). depth_view_: depth_ as the
  // mask step reads it. mask_ / mask_view_: the screen mask; mask_drawn_: the last Render wrote it.
  // The mask step has its own layout (its constants in a slot of the frame's constant buffer at b0;
  // the depth, the two maps and the rotation tile at t0 to t3), shader and pipeline, outside the
  // pipeline cache, and is drawn with map_vs_. depth_ps_: the depth-only forms of ps_[1..3]
  // ([0] is unused: flat clay has no discard, its pre-pass pipeline takes ps_[0]). The pre-pass's
  // pipeline cache: prepass_pipelines_[i] draws prepass_keys_[i], at most kMaxPrepassPipelines.
  bool shadow_objects_ = false, shadow_objects_failed_ = false;
  nrhi::TextureView* depth_view_ = nullptr;
  nrhi::Texture* mask_ = nullptr;
  nrhi::TextureView* mask_view_ = nullptr;
  bool mask_drawn_ = false;
  bool mask_logged_ = false;  // the first drawn mask was logged
  nrhi::BindingLayout* mask_layout_ = nullptr;  // the RHI has no layout destruction
  nrhi::Shader* mask_ps_ = nullptr;
  nrhi::Pipeline* mask_pipeline_ = nullptr;
  nrhi::Shader* depth_ps_[kClayPixelShaders] = {};
  std::vector<PipelineKey> prepass_keys_;
  std::vector<nrhi::Pipeline*> prepass_pipelines_;
  // The rotation tile on the GPU and the bytes it was uploaded with (rotation_valid_: it holds them).
  nrhi::Texture* rotation_ = nullptr;
  nrhi::TextureView* rotation_view_ = nullptr;
  uint8_t rotation_bytes_[kRotationTileBytes] = {};
  bool rotation_valid_ = false;
  // The pass's frame counts for the Shadow line (ClayStats::shadow_frames and the five beside it),
  // since the pass was made.
  ClayMaskFrames shadow_counts_;
  // The same for the Fog line (ClayStats::fog_frames and the four beside it).
  ClayFogFrames fog_counts_;
  // What the pass last logged about the fog, and how often: a line when its verdict changes (the
  // first fogged frame; a frame that lost its fog and why), at most kFogLogLines of them.
  ClayFog fog_logged_ = ClayFog::kOff;
  uint32_t fog_log_lines_ = 0;
  bool unlit_fog_logged_ = false;  // (the one line of the first frame whose unlit draws were fogged)
  // This Render's mask: what is bound as the screen mask at the colour draws (the mask target's
  // view or the white texture), and the frame's slot of the mask's constants.
  uint32_t mask_slot_ = 0;
  // The sky step (EnsureSkyObjects): its own layout (the frame's block at b2, its constants in a
  // slot of the frame's constant buffer at b0, the two sky textures at t0 and t1, the four static
  // samplers), shaders and pipeline, outside the pipeline cache. sky_slot_: this Render's slot of
  // the sky's constants. sky_order_: the records' kinds for SkyPlace (kept for its storage).
  bool sky_objects_ = false, sky_objects_failed_ = false;
  nrhi::BindingLayout* sky_layout_ = nullptr;  // the RHI has no layout destruction
  nrhi::Shader* sky_vs_ = nullptr;
  nrhi::Shader* sky_ps_ = nullptr;
  nrhi::Pipeline* sky_pipeline_ = nullptr;
  uint32_t sky_slot_ = 0;
  std::vector<SkyOrderItem> sky_order_;
  // The pass's frame counts for the Sky line, and what it last logged about the sky (a line when
  // the verdict changes, at most kSkyLogLines of them).
  ClaySkyFrames sky_counts_;
  ClaySky sky_logged_ = ClaySky::kOff;
  bool sky_textured_logged_ = false;  // (of the last logged drawn sky: with its textures or without)
  uint32_t sky_log_lines_ = 0;
  // The post steps (EnsurePostObjects): one layout for every step (the steps' constants, a run of
  // small slots of the frame's constant buffer, at b0; 8 words of a step's own constants at b1; a
  // table of 4 textures at t0; static samplers s0 point clamp, s1 linear clamp; all for the pixel
  // stage), the composite's and the ramp's shaders and pipelines (drawn with map_vs_), and the front
  // target: the composite's 8-bit result before the ramp, the clay target's size, in
  // kPixelShaderResource whenever Render is not running and written only by a post frame (no step
  // reads it in any other). post_slot_: this Render's first slot of the steps' constants.
  bool post_objects_ = false, post_objects_failed_ = false;
  nrhi::BindingLayout* post_layout_ = nullptr;  // the RHI has no layout destruction
  nrhi::Shader* post_composite_ps_ = nullptr;
  nrhi::Shader* post_ramp_ps_ = nullptr;
  nrhi::Pipeline* post_composite_pipeline_ = nullptr;
  nrhi::Pipeline* post_ramp_pipeline_ = nullptr;
  // (two since plan Task 15, the previous-frame blend: front_[1] is made on the first post frame with
  // fable2_native_sun on, EnsureFrontPrevious, and never without the switch; front_state_ says which the
  // last post frame wrote and whether it holds a previous picture: StepClayFront. front_second_failed_:
  // making the second failed; the blend is off until the pass is rebuilt, the post chain goes on.)
  nrhi::Texture* front_[2] = {};
  nrhi::TextureView* front_view_[2] = {};
  ClayFrontState front_state_;
  bool front_second_failed_ = false;
  uint32_t post_slot_ = 0;
  // The bloom's steps (EnsureBloomObjects): the downsample's, the bright pass's and the blur's shaders
  // and pipelines (the post layout, map_vs_), and three targets of kWidth / 4 x kHeight / 4 whatever
  // the scene target's size (ledger ruling 6): bloom_d_ (the downsample, a float target in the
  // scene's range), bloom_p_ (the bright pass, then the blur along y: what the composite samples) and
  // bloom_q_ (the blur along x), the two 10-bit. Each rests in kPixelShaderResource and is a target
  // only for its own step; none is read in a frame that did not write it.
  bool bloom_objects_ = false, bloom_objects_failed_ = false;
  nrhi::Shader* post_downsample_ps_ = nullptr;
  nrhi::Shader* post_bright_ps_ = nullptr;
  nrhi::Shader* post_blur_ps_ = nullptr;
  nrhi::Pipeline* post_downsample_pipeline_ = nullptr;
  nrhi::Pipeline* post_bright_pipeline_ = nullptr;
  nrhi::Pipeline* post_blur_pipeline_ = nullptr;
  nrhi::Texture* bloom_d_ = nullptr;
  nrhi::Texture* bloom_p_ = nullptr;
  nrhi::Texture* bloom_q_ = nullptr;
  nrhi::TextureView* bloom_d_view_ = nullptr;
  nrhi::TextureView* bloom_p_view_ = nullptr;
  nrhi::TextureView* bloom_q_view_ = nullptr;
  // The pass's frame counts for the Post line, and what it last logged about the post (a line when
  // the verdict or the bloom's changes, at most kPostLogLines of them).
  ClayPostFrames post_counts_;
  ClayPost post_logged_ = ClayPost::kOff;
  ClayBloom bloom_logged_ = ClayBloom::kOff;
  uint32_t post_log_lines_ = 0;
  // The quad step (EnsureQuadObjects): its own layout (a quad's constants, two small slots of the
  // frame's constant buffer, at b0 for both stages; the frame's block at b2; a table of 3 textures at
  // t0; the four static samplers), its shaders and its pipelines by state (quad_pipelines_[0], no
  // state, is never made), outside the pipeline cache. quad_slot0_: this Render's first slot of the
  // quads' constants (quad i's: quad_slot0_ + kClayQuadSlots * i). The pass's frame counts for the
  // Furniture line, and what it last logged about the furniture (a line when the verdict or what was
  // drawn changes, at most kFurnitureLogLines of them).
  bool quad_objects_ = false, quad_objects_failed_ = false;
  nrhi::BindingLayout* quad_layout_ = nullptr;  // the RHI has no layout destruction
  nrhi::Shader* quad_vs_ = nullptr;
  nrhi::Shader* quad_ps_ = nullptr;
  nrhi::Pipeline* quad_pipelines_[size_t(ClayQuadState::kCount)] = {};
  uint32_t quad_slot0_ = 0;
  ClayFurnitureFrames furniture_counts_;
  ClayFurniture furniture_logged_ = ClayFurniture::kOff;
  uint32_t furniture_drew_logged_[3] = {};  // (of the last logged frame: moons drawn, glares drawn, quads without their texture)
  // (two bounds: lines for a change of the verdict, and lines for a change of what a drawn frame drew,
  // which in play changes with the camera; review of plan Tasks 12 and 13, Minor 6)
  uint32_t furniture_log_lines_ = 0, furniture_count_lines_ = 0;
  // The sun layer (EnsureSunObjects). sun_depth_: the layer's depth, kWidth / 2 x kHeight / 2 D32_FLOAT, a
  // depth target for the layer's three steps and never sampled. sun_scene_depth_view_: depth_ as the
  // layer's depth step reads it. sun_layer_[0], [1]: layer 1 and layer 2, the same size, R10G10B10A2_UNORM.
  // sun_small_[0..2]: kWidth / 4 x kHeight / 4, the same format: [0] layer 1 reduced, then the second
  // radial pass's result, which is the bright pass's tf1; [1] the first radial pass's; [2] layer 2 reduced,
  // the bright pass's tf2. Each rests in kPixelShaderResource and is a target only for its own step; none
  // is read in a frame that did not write it. sun_pipelines_: by ClaySunState ([0], no state, is never
  // made). The pass's frame counts for the Sun line and what it last logged (a line when the verdict
  // changes, at most kSunLogLines of them).
  bool sun_objects_ = false, sun_objects_failed_ = false;
  nrhi::Shader* sun_depth_ps_ = nullptr;
  nrhi::Shader* sun_reduce_ps_ = nullptr;
  nrhi::Shader* sun_radial_ps_ = nullptr;
  nrhi::Shader* sun_quad_vs_ = nullptr;
  nrhi::Shader* sun_quad_ps_ = nullptr;
  nrhi::Shader* sun_occluder_ps_ = nullptr;
  nrhi::Pipeline* sun_depth_pipeline_ = nullptr;
  nrhi::Pipeline* sun_reduce_pipeline_ = nullptr;
  nrhi::Pipeline* sun_radial_pipeline_ = nullptr;
  nrhi::Pipeline* sun_pipelines_[size_t(ClaySunState::kCount)] = {};
  nrhi::Texture* sun_depth_ = nullptr;
  nrhi::TextureView* sun_scene_depth_view_ = nullptr;
  nrhi::Texture* sun_layer_[2] = {};
  nrhi::TextureView* sun_layer_view_[2] = {};
  nrhi::Texture* sun_small_[3] = {};
  nrhi::TextureView* sun_small_view_[3] = {};
  ClaySunFrames sun_counts_;
  ClaySun sun_logged_ = ClaySun::kOff;
  uint32_t sun_log_lines_ = 0;
  // The oracle feed (SetPostOracle; null: off). oracle_tex_ / oracle_view_: the stored scene, in
  // kPixelShaderResource from its upload on. oracle_frames_: the frames fed since the upload, the
  // upload's own included (it stops counting past the dump's frame). oracle_dump_: the read-back buffer
  // of the mapped target while its copy is pending, readable once the submission oracle_dump_submission_
  // has completed. oracle_dump_state_: none yet, pending, or done (taken, or failed: not made again).
  enum class OracleDump : uint8_t { kNone, kPending, kDone };
  const OracleFeed* oracle_ = nullptr;
  nrhi::Texture* oracle_tex_ = nullptr;
  nrhi::TextureView* oracle_view_ = nullptr;
  bool oracle_failed_ = false;
  uint32_t oracle_frames_ = 0;
  nrhi::Buffer* oracle_dump_ = nullptr;
  uint64_t oracle_dump_submission_ = 0;
  OracleDump oracle_dump_state_ = OracleDump::kNone;
  // The feed's sun part: its depth as a texture (in kPixelShaderResource from its upload on;
  // oracle_sun_failed_: it could not be made, and the feed has no sun layer until the pass is rebuilt), and
  // the read-back buffers of the layer's three pictures, copied with the dump and readable with it.
  nrhi::Texture* oracle_depth_tex_ = nullptr;
  nrhi::TextureView* oracle_depth_view_ = nullptr;
  bool oracle_sun_failed_ = false;
  nrhi::Buffer* oracle_layer_dump_[kOracleLayerDumps] = {};
  nrhi::BindingLayout* map_layout_ = nullptr;  // the RHI has no layout destruction
  nrhi::Shader* map_vs_ = nullptr;
  nrhi::Shader* map_ps_ = nullptr;
  nrhi::Pipeline* map_pipeline_ = nullptr;
  // 1x1 white texture bound at t3 for untextured draws and at t4 as the screen
  // mask of a frame without one; its contents are copied from white_staging_ by
  // the first Render after creation.
  nrhi::Texture* white_ = nullptr;
  nrhi::TextureView* white_view_ = nullptr;
  nrhi::Buffer* white_staging_ = nullptr;
  GeometryCache geometry_;
  TextureCache textures_;
  std::vector<Prepared> prepared_;
  // Terrain of the frame being rendered: one plan item per terrain record
  // (Prepared::plan), the layers' textures, the texture sets bound, and the
  // frame's constant buffer, persistently mapped: slots of
  // kClayTerrainSlotBytes, the first kClayFrameSlots the frame's block (ClayFrameConstants,
  // cbuffer Frame: its light and the fog's tail), then one per terrain record drawn with its material
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
  uint8_t* terrain_cb_mapped_ = nullptr;  // the buffer's memory while this Render has it (write only)
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
