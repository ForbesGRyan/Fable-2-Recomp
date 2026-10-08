#include "clay_pass.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include <rex/logging.h>

#include "../capture/xdk_hook_ids.h"
#include "../fable2_native_shaders.h"

namespace fable2::native::render {

namespace {
// The draws' colour target and the output mapping's (clay_logic.h, "Linear
// target and output mapping": kClayLinearTexelBytes, kClayMappedTexelBytes).
// Every pipeline of the cache renders into the linear format, so PipelineKey
// does not hold it.
constexpr nrhi::Format kLinearFormat = nrhi::Format::kR11G11B10_FLOAT;
constexpr nrhi::Format kMappedFormat = nrhi::Format::kR8G8B8A8_UNORM;
// The output mapping's binding parameters: its constants (word 0 the exposure)
// and the linear target at t0.
constexpr uint32_t kMapParamConstants = 0, kMapParamLinear = 1, kMapConstantCount = 4;
constexpr uint32_t kConstantCount = sizeof(ClayConstants) / 4;  // 36
static_assert(kConstantCount == 36);
// Binding parameters: indices into kClayBindings (clay_logic.h), from which
// Ensure creates the layout. Root signature cost: 54 of 64 words (the reason
// the light's per-draw values are in a constant buffer slot and not in the
// root constants). kParamTerrainConstants is register b1: cbuffer Terrain of a
// terrain draw, cbuffer Object of a lit object.
constexpr uint32_t kParamConstants = 0, kParamPositions = 1, kParamIndices = 2, kParamUvs = 3,
                   kParamAlbedo = 4, kParamTerrainTextures = 5, kParamTerrainConstants = 6,
                   kParamFrameConstants = 7, kParamTerrainNormals = 8, kParamNormals = 9, kParamColours = 10;
constexpr uint32_t kParamCount = uint32_t(std::size(kClayBindings));
// The terrain shaders' table: colour, weights, side texture, base map (t4 to t7).
constexpr uint32_t kTerrainTableSize = 4;
constexpr bool ParamIs(uint32_t param, ClayBindingKind kind, uint32_t reg, uint32_t count) {
  return param < kParamCount && kClayBindings[param].kind == kind && kClayBindings[param].reg == reg &&
         kClayBindings[param].count == count;
}
static_assert(kParamCount == 11 && kParamCount <= nrhi::kMaxBindingParams);
static_assert(ParamIs(kParamConstants, ClayBindingKind::kConstants, 0, kConstantCount));
static_assert(ParamIs(kParamPositions, ClayBindingKind::kBuffer, 0, 1) && ParamIs(kParamIndices, ClayBindingKind::kBuffer, 1, 1) &&
              ParamIs(kParamUvs, ClayBindingKind::kBuffer, 2, 1));
static_assert(ParamIs(kParamAlbedo, ClayBindingKind::kTextures, 3, 1) &&
              ParamIs(kParamTerrainTextures, ClayBindingKind::kTextures, 4, kTerrainTableSize));
static_assert(ParamIs(kParamTerrainConstants, ClayBindingKind::kConstantBuffer, 1, 1) &&
              ParamIs(kParamFrameConstants, ClayBindingKind::kConstantBuffer, 2, 1));
static_assert(ParamIs(kParamTerrainNormals, ClayBindingKind::kBuffer, 8, 1) && ParamIs(kParamNormals, ClayBindingKind::kBuffer, 9, 1) &&
              ParamIs(kParamColours, ClayBindingKind::kBuffer, 10, 1) && kParamTerrainNormals == kClayLightBufferParam0);
// The terrain constant buffer's size is a multiple of this (256 slots), so the
// pool's same-size reuse holds while the number of terrain draws moves.
constexpr uint64_t kTerrainBufferStep = 256ull * kClayTerrainSlotBytes;
// Cache slot of the flat terrain default pipeline, after the four of `textured`.
constexpr size_t kTerrainFlatSlot = kClayPixelShaders;

// PipelineKey values (capture enums) as the RHI's. A key holds only values the
// RHI has (MakePipelineKey); the defaults below are never reached with one.
nrhi::BlendFactor ToFactor(uint8_t f) {
  switch (capture::Factor(f)) {
    case capture::Factor::kZero: return nrhi::BlendFactor::kZero;
    case capture::Factor::kSrcAlpha: return nrhi::BlendFactor::kSrcAlpha;
    case capture::Factor::kInvSrcAlpha: return nrhi::BlendFactor::kInvSrcAlpha;
    default: return nrhi::BlendFactor::kOne;
  }
}

nrhi::BlendOp ToBlendOp(uint8_t op) {
  return capture::BlendOp(op) == capture::BlendOp::kMin ? nrhi::BlendOp::kMin : nrhi::BlendOp::kAdd;
}

nrhi::CompareFunc ToCompare(uint8_t f) {
  switch (capture::Compare(f)) {
    case capture::Compare::kAlways: return nrhi::CompareFunc::kAlways;
    case capture::Compare::kLessEqual: return nrhi::CompareFunc::kLessEqual;
    default: return nrhi::CompareFunc::kLess;
  }
}

// PipelineKey::cull is in the pass's window space with clockwise front faces
// (ClayCull), which is the RHI's (D3D12's default front face).
nrhi::CullMode ToCull(uint8_t c) {
  switch (capture::Cull(c)) {
    case capture::Cull::kFront: return nrhi::CullMode::kFront;
    case capture::Cull::kBack: return nrhi::CullMode::kBack;
    default: return nrhi::CullMode::kNone;
  }
}
}  // namespace

void ClayPass::DestroyObjects() {
  if (device_) {
    for (nrhi::Pipeline* p : pipelines_) device_->DestroyDeferred(p);
    device_->DestroyDeferred(vs_);
    for (nrhi::Shader* ps : ps_) device_->DestroyDeferred(ps);
    device_->DestroyDeferred(map_pipeline_);
    device_->DestroyDeferred(map_vs_);
    device_->DestroyDeferred(map_ps_);
    terrain_destroy_.clear();
    terrain_pool_.Drain(&terrain_destroy_);
    for (nrhi::Buffer* b : terrain_destroy_) device_->DestroyDeferred(b);
    device_->DestroyDeferred(terrain_cb_);
    device_->DestroyDeferred(linear_view_);
    device_->DestroyDeferred(linear_);
    device_->DestroyDeferred(color_);
    device_->DestroyDeferred(depth_);
    device_->DestroyDeferred(white_view_);
    device_->DestroyDeferred(white_);
    device_->DestroyDeferred(white_staging_);
  }
  pipeline_keys_.clear();
  pipelines_.clear();
  vs_ = nullptr;
  for (nrhi::Shader*& ps : ps_) ps = nullptr;
  map_pipeline_ = nullptr;
  map_vs_ = map_ps_ = nullptr;
  terrain_destroy_.clear();
  terrain_cb_ = nullptr;
  linear_view_ = nullptr;
  linear_ = color_ = depth_ = nullptr;
  white_ = nullptr;
  white_view_ = nullptr;
  white_staging_ = nullptr;
  ready_ = false;
}

// The default key (an invalid state) is the pass's pipeline before render
// states: opaque, depth Less with write, no cull.
nrhi::Pipeline* ClayPass::CreatePipeline(nrhi::Device* dev, const PipelineKey& key) {
  nrhi::GraphicsPipelineDesc pipe;
  pipe.layout = layout_;
  pipe.vs = vs_;
  pipe.ps = ps_[key.textured < kClayPixelShaders ? key.textured : 0];
  pipe.rtv_format = kLinearFormat;
  pipe.dsv_format = nrhi::Format::kD32_FLOAT;
  pipe.blend.enable = key.blend != 0;
  pipe.blend.src = ToFactor(key.src);
  pipe.blend.dst = ToFactor(key.dst);
  pipe.blend.op = ToBlendOp(key.op);
  pipe.blend.src_alpha = ToFactor(key.src_alpha);
  pipe.blend.dst_alpha = ToFactor(key.dst_alpha);
  pipe.blend.op_alpha = ToBlendOp(key.op_alpha);
  pipe.blend.write_mask = key.color_mask;  // 0 with depth write: a depth-only draw
  pipe.depth.test_enable = key.depth_test != 0;
  pipe.depth.write_enable = key.depth_write != 0;
  pipe.depth.func = ToCompare(key.depth_func);
  pipe.cull = ToCull(key.cull);
  return dev->CreateGraphicsPipeline(pipe);
}

nrhi::Pipeline* ClayPass::TryPipeline(nrhi::Device* dev, const PipelineKey& key) {
  const size_t slot = FindOrAddPipelineSlot(pipeline_keys_, key);
  if (slot == kNoPipelineSlot) return nullptr;
  if (slot == pipelines_.size()) {  // a new key
    pipelines_.push_back(CreatePipeline(dev, key));
    if (!pipelines_.back()) {
      REXLOG_ERROR("[native] clay: pipeline creation failed for a render state (its draws use the default)");
    }
  }
  return pipelines_[slot];
}

nrhi::Pipeline* ClayPass::PipelineFor(nrhi::Device* dev, const PipelineKey& key, ClayStats& st,
                                      nrhi::Pipeline* fallback) {
  if (nrhi::Pipeline* pipeline = TryPipeline(dev, key)) return pipeline;
  ++st.pipeline_overflow;
  return fallback ? fallback : pipelines_[key.textured < kClayPixelShaders ? key.textured : 0];
}

// The 1x1 white albedo for untextured draws. Its texel is staged here and
// copied by the next Render (Ensure has no command list).
bool ClayPass::CreateWhite(nrhi::Device* dev) {
  nrhi::TextureDesc desc;
  desc.width = desc.height = 1;
  desc.format = nrhi::Format::kR8G8B8A8_UNORM;
  desc.initial_state = nrhi::ResourceState::kCopyDest;
  white_ = dev->CreateTexture(desc);
  if (!white_) return false;
  white_view_ = dev->CreateTextureView(white_, nrhi::TextureViewDesc{});
  nrhi::BufferDesc bd;
  bd.size = nrhi::kRowPitchAlignment;
  bd.heap = nrhi::HeapKind::kUpload;
  bd.bind_class = nrhi::BufferBindClass::kCopySrc;
  white_staging_ = dev->CreateBuffer(bd);
  if (!white_view_ || !white_staging_) return false;
  void* mapped = dev->Map(white_staging_);
  if (!mapped) return false;
  std::memset(mapped, 0xFF, 4);
  dev->Unmap(white_staging_);
  return true;
}

bool ClayPass::Ensure(nrhi::Device* dev) {
  if (ready_ && dev == device_) return true;
  if (dev != device_) {
    // A different device: the old objects are not ours to destroy through it.
    geometry_.Release(nullptr);
    textures_.Release(nullptr);
    pipeline_keys_.clear();
    pipelines_.clear();
    vs_ = nullptr;
    for (nrhi::Shader*& ps : ps_) ps = nullptr;
    map_pipeline_ = nullptr;
    map_vs_ = map_ps_ = nullptr;
    terrain_destroy_.clear();
    terrain_pool_.Drain(&terrain_destroy_);
    terrain_destroy_.clear();
    terrain_cb_ = nullptr;
    linear_view_ = nullptr;
    linear_ = color_ = depth_ = nullptr;
    white_ = nullptr;
    white_view_ = nullptr;
    white_staging_ = nullptr;
    layout_ = nullptr;
    map_layout_ = nullptr;
    device_ = dev;
  } else {
    DestroyObjects();  // retry after a partial failure
  }
  ready_ = false;

  if (!layout_) {
    // The layout is kClayBindings (clay_logic.h), parameter for parameter: the
    // tests hold every register the compiled shaders declare against that table.
    nrhi::BindingLayoutDesc desc;
    for (uint32_t p = 0; p < kParamCount; ++p) {
      const ClayBinding& b = kClayBindings[p];
      desc.params[p].kind = b.kind == ClayBindingKind::kConstants         ? nrhi::BindingParamKind::kConstants
                            : b.kind == ClayBindingKind::kConstantBuffer ? nrhi::BindingParamKind::kConstantBuffer
                            : b.kind == ClayBindingKind::kBuffer         ? nrhi::BindingParamKind::kBufferSrv
                                                                         : nrhi::BindingParamKind::kTextureTable;
      desc.params[p].shader_register = b.reg;
      desc.params[p].count = b.count;
      desc.params[p].visibility = b.stage == ClayBindingStage::kVertex  ? nrhi::Visibility::kVertex
                                  : b.stage == ClayBindingStage::kPixel ? nrhi::Visibility::kPixel
                                                                        : nrhi::Visibility::kAll;
    }
    desc.param_count = kParamCount;
    // s<i>: bit 0 linear, bit 1 clamp (SamplerIndex).
    static_assert(kClayStaticSamplers == 4);
    for (uint32_t i = 0; i < 4; ++i) {
      desc.static_samplers[i].shader_register = i;
      desc.static_samplers[i].filter = (i & 1) ? nrhi::Filter::kLinear : nrhi::Filter::kPoint;
      desc.static_samplers[i].address = (i & 2) ? nrhi::AddressMode::kClamp : nrhi::AddressMode::kWrap;
    }
    desc.static_sampler_count = 4;
    desc.allow_input_layout = false;
    layout_ = dev->CreateBindingLayout(desc);
    if (!layout_) {
      REXLOG_ERROR("[native] clay: binding layout creation failed");
      return false;
    }
  }

  nrhi::ShaderDesc vs;
  vs.stage = nrhi::ShaderStage::kVertex;
  vs.name = "fable2_clay_vs";
  vs.hlsl_source = shaders::kClayVs;
  vs.entry_point = "main";
  vs_ = dev->CreateShader(vs);
  if (!vs_) {
    REXLOG_ERROR("[native] clay: shader compile failed");
    return false;
  }
  static constexpr const char* kPsNames[kClayPixelShaders] = {"fable2_clay_ps", "fable2_clay_textured_ps",
                                                              "fable2_clay_terrain_layer_ps",
                                                              "fable2_clay_terrain_far_ps"};
  static constexpr const char* kPsSources[kClayPixelShaders] = {shaders::kClayPs, shaders::kClayTexturedPs,
                                                                shaders::kClayTerrainLayerPs,
                                                                shaders::kClayTerrainFarPs};
  for (uint8_t textured = 0; textured < kClayPixelShaders; ++textured) {
    nrhi::ShaderDesc sd;
    sd.stage = nrhi::ShaderStage::kPixel;
    sd.name = kPsNames[textured];
    sd.hlsl_source = kPsSources[textured];
    sd.entry_point = "main";
    ps_[textured] = dev->CreateShader(sd);
    if (!ps_[textured]) {
      REXLOG_ERROR("[native] clay: pixel shader compile failed");
      return false;
    }
  }
  // Cache slots 0 to 3: the default pipelines (PipelineFor falls back to them);
  // slot 4: the default of a flat terrain draw (kTerrainFlatSlot).
  for (uint8_t slot = 0; slot <= kClayPixelShaders; ++slot) {
    PipelineKey key = TerrainFlatDefaultKey();
    if (slot < kClayPixelShaders) {
      key = PipelineKey{};
      key.textured = slot;
    }
    pipeline_keys_.push_back(key);
    pipelines_.push_back(CreatePipeline(dev, key));
    if (!pipelines_.back()) {
      REXLOG_ERROR("[native] clay: pipeline creation failed");
      return false;
    }
  }
  if (!CreateWhite(dev)) {
    REXLOG_ERROR("[native] clay: white texture creation failed");
    return false;
  }

  // The output mapping: a full-screen triangle from the linear target into the
  // mapped one. Not in the pipeline cache (it is no draw's state).
  if (!map_layout_) {
    nrhi::BindingLayoutDesc desc;
    desc.params[kMapParamConstants].kind = nrhi::BindingParamKind::kConstants;
    desc.params[kMapParamConstants].shader_register = 0;
    desc.params[kMapParamConstants].count = kMapConstantCount;
    desc.params[kMapParamConstants].visibility = nrhi::Visibility::kPixel;
    desc.params[kMapParamLinear].kind = nrhi::BindingParamKind::kTextureTable;
    desc.params[kMapParamLinear].shader_register = 0;
    desc.params[kMapParamLinear].count = 1;
    desc.params[kMapParamLinear].visibility = nrhi::Visibility::kPixel;
    desc.param_count = 2;
    desc.allow_input_layout = false;
    map_layout_ = dev->CreateBindingLayout(desc);
    if (!map_layout_) {
      REXLOG_ERROR("[native] clay: output mapping binding layout creation failed");
      return false;
    }
  }
  nrhi::ShaderDesc map_shader;
  map_shader.entry_point = "main";
  map_shader.stage = nrhi::ShaderStage::kVertex;
  map_shader.name = "fable2_fullscreen_vs";
  map_shader.hlsl_source = shaders::kFullscreenVs;
  map_vs_ = dev->CreateShader(map_shader);
  map_shader.stage = nrhi::ShaderStage::kPixel;
  map_shader.name = "fable2_output_map_ps";
  map_shader.hlsl_source = shaders::kOutputMapPs;
  map_ps_ = dev->CreateShader(map_shader);
  if (!map_vs_ || !map_ps_) {
    REXLOG_ERROR("[native] clay: output mapping shader compile failed");
    return false;
  }
  nrhi::GraphicsPipelineDesc map_pipe;
  map_pipe.layout = map_layout_;
  map_pipe.vs = map_vs_;
  map_pipe.ps = map_ps_;
  map_pipe.rtv_format = kMappedFormat;
  map_pipeline_ = dev->CreateGraphicsPipeline(map_pipe);
  if (!map_pipeline_) {
    REXLOG_ERROR("[native] clay: output mapping pipeline creation failed");
    return false;
  }

  // The three targets, always together: a new linear target comes with a new
  // mapped one, and targets_generation_ makes the caller render into both
  // before anything samples the mapped one.
  nrhi::TextureDesc linear;
  linear.width = kWidth;
  linear.height = kHeight;
  linear.format = kLinearFormat;
  linear.usage = nrhi::kTextureUsageRenderTarget;
  linear.initial_state = nrhi::ResourceState::kPixelShaderResource;
  for (int i = 0; i < 4; ++i) linear.clear_color[i] = kClayClearLinear[i];
  linear_ = dev->CreateTexture(linear);
  linear_view_ = linear_ ? dev->CreateTextureView(linear_, nrhi::TextureViewDesc{}) : nullptr;
  nrhi::TextureDesc color;
  color.width = kWidth;
  color.height = kHeight;
  color.format = kMappedFormat;
  color.usage = nrhi::kTextureUsageRenderTarget;
  color.initial_state = nrhi::ResourceState::kPixelShaderResource;
  color_ = dev->CreateTexture(color);  // never cleared: the mapping writes every pixel
  nrhi::TextureDesc depth;
  depth.width = kWidth;
  depth.height = kHeight;
  depth.format = nrhi::Format::kD32_FLOAT;
  depth.usage = nrhi::kTextureUsageDepthStencil;
  depth.initial_state = nrhi::ResourceState::kPixelShaderResource;
  depth.clear_depth = 1.0f;
  depth_ = dev->CreateTexture(depth);
  if (!linear_ || !linear_view_ || !color_ || !depth_) {
    REXLOG_ERROR("[native] clay: target creation failed");
    return false;
  }
  ready_ = true;
  ++targets_generation_;
  REXLOG_INFO("[native] clay: {} (generation {})", FormatTargetText(kWidth, kHeight), targets_generation_);
  return true;
}

nrhi::TextureView* ClayPass::ResolveTerrainTexture(nrhi::Cmd* cmd, nrhi::Device* dev, const uint32_t fetch[6],
                                                   bool memo, uint32_t* sampler, float* uv_fix,
                                                   capture::MaterialStatus* status, TextureStats& tst) {
  if (memo) {
    for (const TerrainTexture& t : terrain_textures_) {
      if (std::memcmp(t.fetch, fetch, sizeof(t.fetch)) != 0) continue;
      *sampler = t.sampler;
      uv_fix[0] = t.uv_fix[0];
      uv_fix[1] = t.uv_fix[1];
      *status = t.status;
      return t.view;
    }
  }
  TerrainTexture t;
  std::memcpy(t.fetch, fetch, sizeof(t.fetch));
  capture::Material m;  // the cache reads the fetch constant only
  std::memcpy(m.fetch, fetch, sizeof(m.fetch));
  t.view = textures_.Resolve(cmd, dev, m, &t.sampler, t.uv_fix, &t.status, tst);
  if (memo) terrain_textures_.push_back(t);
  *sampler = t.sampler;
  uv_fix[0] = t.uv_fix[0];
  uv_fix[1] = t.uv_fix[1];
  *status = t.status;
  return t.view;
}

void ClayPass::PrepareLight(nrhi::Device* dev, const capture::DrawRecord& r, const capture::LightInputs& light,
                            Prepared& p, ClayStats& st) {
  using capture::LightReason;
  ++st.lit;
  p.light = &light;
  // One element for every vertex the draw's indices name (an instanced draw: its flat positions).
  const uint32_t need =
      r.instances.active ? p.vertex_count : LightVertexNeed(p.max_index, r.base_vertex, p.vertex_count);
  LightReason why = LightReason::kLightUnknown;
  uint32_t normal_count = 0, colour_count = 0;
  p.normals = geometry_.Normals(dev, r, light, need, st, &why, &normal_count);
  p.light_count = normal_count;
  if (p.normals && light.kind == capture::LightVertexKind::kStream) {
    // The family's baked term is the stream's colour: without it the record has no light.
    p.colours = geometry_.Colours(dev, r, light, need, st, &colour_count);
    if (!p.colours) {
      p.normals = nullptr;
      why = LightReason::kLightUnknown;
    }
    p.light_count = std::min(normal_count, colour_count);
  } else if (p.normals && (light.kind == capture::LightVertexKind::kProbe || light.kind == capture::LightVertexKind::kInstance)) {
    // The other two vertex kinds: the baked term is made of the probe weights (with the draw's
    // probe constants) or is the copy's colour. Built and resident; no shader reads them yet and
    // no draw binds them (light_mode stays 0 for these records: they are not lit).
    LightSecond refused = LightSecond::kNone;
    uint32_t second_count = 0;
    if (light.kind == capture::LightVertexKind::kProbe) {
      p.probe = geometry_.Probe(dev, r, light, need, st, &refused, &second_count);
      p.probe_count = second_count;
    } else {
      p.colours = geometry_.InstanceColours(dev, r, light, need, st, &refused, &second_count);
    }
    if (!p.probe && !p.colours) {
      p.normals = nullptr;
      why = LightReason::kLightUnknown;
      if (size_t(refused) < size_t(LightSecond::kCount)) ++st.light_second[size_t(refused)];
    }
    p.light_count = std::min(normal_count, second_count);
  }
  if (!p.normals) {
    p.light_why = why;
    if (why == LightReason::kNormalUnsupported) p.unlit = ClayUnlit::kNormal;
    p.light_count = 0;
    ++st.light_missing[size_t(why)];
    st.light_missing_by_vs.Add(r.vs_hash, uint8_t(why));
    return;
  }
  ++st.light_ready;
  // The world rotation the draw's normals will go through: what it is, counted, never assumed.
  if (light.family == capture::LightFamily::kObject || light.family == capture::LightFamily::kTreeTrunk) {
    const RotationKind kind = ClassifyRotation(light.world_rot, light.world_scale);
    ++st.light_rotation[size_t(kind)];
    if (kind != RotationKind::kRotation) st.light_rotation_by_vs.Add(r.vs_hash, uint8_t(kind));
  }
  // The light itself: in a lit frame, for a family the pass lights, when every value of the
  // draw's is usable (a rotation without a direction is not: counted above, drawn unlit).
  ClayObjectConstants constants;
  if (frame_.light_on && LightFamilyDrawn(light) && p.colours && MakeClayObjectConstants(light, &constants)) {
    p.lit = true;
    p.slot = uint32_t(object_constants_.size());
    object_constants_.push_back(constants);
  }
}

void ClayPass::PrepareTerrain(nrhi::Cmd* cmd, nrhi::Device* dev, const capture::DrawRecord& r,
                              const capture::TerrainPatch& terrain, const capture::LightInputs* light,
                              bool textured, ClayStats& st, TextureStats& tst) {
  using capture::MaterialStatus;
  Prepared p;
  p.r = &r;
  p.terrain = true;
  uint32_t buffer_vertices = 0;
  p.positions = geometry_.Positions(dev, r, &terrain, &buffer_vertices, st);
  if (p.positions) {
    p.vertex_count = DrawVertexCount(buffer_vertices, RecordVertexCount(r, &terrain));
    p.indices = geometry_.Indices(dev, r, &terrain, p.vertex_count, &p.index_count, st);
  }
  // The adaptive builder draws the far ground and the layers' underlay; every
  // other terrain record is a layer of the layer builder.
  const bool adaptive = r.func_id == capture::kHook_DrawIndx_82207C30;
  TerrainPlanItem item;
  item.patch = &terrain;  // into the scene, which outlives this Render
  item.layer = !adaptive;
  item.drawable = p.positions && p.indices;
  // A record without geometry makes its square fall back like one without its
  // material: the Terrain line names it by what was missing.
  if (textured && !item.drawable) ++(p.positions ? st.terrain_no_indices : st.terrain_no_positions);
  if (textured && item.drawable) {
    const capture::TerrainMaterialInputs& m = terrain.material;
    const capture::MaterialKind kind = r.material.kind;
    const capture::RenderState rs = capture::DecodeRenderState(r.state_words);
    MaterialStatus colour = MaterialStatus::kTextured, weight = MaterialStatus::kTextured;
    MaterialStatus side = MaterialStatus::kTextured;
    bool far_uvs = true, normals = true;
    // Nothing is asked of the caches for a record whose state or inputs are not usable.
    if (TerrainStateUsable(rs) &&
        TerrainMaterialStatus(m, kind, adaptive, colour, weight, far_uvs) == MaterialStatus::kTextured) {
      // The layers share a few detail textures and one weight atlas; most far draws have their own colour.
      p.albedo = ResolveTerrainTexture(cmd, dev, m.detail_fetch, !adaptive, &p.sampler, p.uv_fix, &colour, tst);
      if (adaptive) {
        p.uvs = p.albedo ? geometry_.TerrainFarUvs(dev, terrain, p.vertex_count, st) : nullptr;
        far_uvs = p.uvs != nullptr;
      } else {
        float fix[2];  // the weights' coordinate is in texels of the guest extent; the decode pads nothing
        p.weights = ResolveTerrainTexture(cmd, dev, m.weight_fetch, true, &p.weight_sampler, fix, &weight, tst);
        if (m.steep) {
          // The steep-ground block: the layer's side texture (a few are shared by all layers) and
          // the game's vertex normals at the grid points (one buffer for all layers of a square).
          p.side = ResolveTerrainTexture(cmd, dev, m.side_fetch, true, &p.side_sampler, p.side_fix, &side, tst);
          p.normals = p.side ? geometry_.TerrainNormals(dev, terrain, p.vertex_count, st) : nullptr;
          normals = p.normals != nullptr;
        }
      }
    }
    p.status = TerrainStateUsable(rs) ? TerrainMaterialStatus(m, kind, adaptive, colour, weight, far_uvs)
                                      : MaterialStatus::kTerrainInputs;
    // A layer whose shader has the block is drawn with it or not with its material at all.
    if (p.status == MaterialStatus::kTextured) p.status = TerrainSteepStatus(m, side, normals);
    if (p.status == MaterialStatus::kTextured) {
      // The pipeline of its state. Without one (the cache is full, its creation
      // failed) the record cannot be drawn as captured either: the default
      // pipeline would draw a layer opaque with depth less. Counted in the
      // State line's overflow, as a record that draws with a default pipeline is.
      p.pipeline = TryPipeline(dev, DecideDrawState(rs, r.material, TerrainTextured(kind), false).key);
      if (!p.pipeline) {
        p.status = MaterialStatus::kTerrainInputs;
        p.no_pipeline = true;
        ++st.pipeline_overflow;
      }
    }
    item.usable = p.status == MaterialStatus::kTextured;
    if (!item.usable) ++st.terrain_unusable[size_t(p.status)];
    if (item.usable && light) {
      // The light's vertex normals: the grid points' (n.x, n.y) of every terrain record, the
      // buffer the steep-ground block has already asked for when the layer has the block.
      // (Counted in FinishTerrain, when the plan has said that the record keeps its material.)
      p.light = light;
      if (!p.normals) p.normals = geometry_.TerrainNormals(dev, terrain, p.vertex_count, st);
      if (!p.normals) {
        // No map the reader decodes (no texture in the slot, another format), or one it could not read.
        capture::NormalMap nm;
        p.light_why = capture::DecodeNormalMap(m.normal_fetch, &nm) ? capture::LightReason::kLightUnknown
                                                                    : capture::LightReason::kNormalUnsupported;
        if (p.light_why == capture::LightReason::kNormalUnsupported) p.unlit = ClayUnlit::kNormal;
      } else if (frame_.light_on && LightFamilyDrawn(*light) && m.base_valid) {
        // The light itself, in a lit frame: the base map tf3 (the baked term; a few are shared by
        // all records of a region) and the record's light constants. Without either the record is
        // not lit, and with it its square (PlanTerrain).
        MaterialStatus base = MaterialStatus::kTextured;
        p.base = ResolveTerrainTexture(cmd, dev, m.base_fetch, true, &p.base_sampler, p.base_fix, &base, tst);
        ClayTerrainConstants probe{};
        item.light = p.base != nullptr && base == MaterialStatus::kTextured &&
                     SetClayTerrainLight(probe, m, *light, p.base_fix, p.base_sampler);
        if (!item.light) p.base = nullptr;
      }
    }
  }
  p.plan = uint32_t(terrain_items_.size());
  terrain_items_.push_back(item);
  prepared_.push_back(p);
}

void ClayPass::FinishTerrain(nrhi::Device* dev, bool textured, ClayStats& st, TextureStats& tst) {
  using capture::MaterialStatus;
  uint8_t* slots = nullptr;
  // The frame's constant buffer: for the terrain material and for the light, and taken for every
  // frame that draws anything, so that the two registers the vertex shader declares (b1, b2) are
  // bound at every draw, whether or not the draw reads them.
  const bool wanted = !prepared_.empty();
  if (wanted) {
    // Slot 0 the frame's light, one slot per terrain record (at most that many are drawn with
    // the material), one per lit object.
    const uint64_t count = 1 + uint64_t(terrain_items_.size()) + object_constants_.size();
    const uint64_t bytes = (count * kClayTerrainSlotBytes + kTerrainBufferStep - 1) /
                           kTerrainBufferStep * kTerrainBufferStep;
    if (auto reused = terrain_pool_.Take(bytes, dev->CompletedSubmission())) {
      terrain_cb_ = *reused;
    } else {
      nrhi::BufferDesc desc;
      desc.size = bytes;
      desc.heap = nrhi::HeapKind::kUpload;
      desc.bind_class = nrhi::BufferBindClass::kFull;
      terrain_cb_ = dev->CreateBuffer(desc);
    }
    terrain_cb_bytes_ = bytes;
    slots = terrain_cb_ ? static_cast<uint8_t*>(dev->Map(terrain_cb_)) : nullptr;  // kept mapped
    if (!slots) {
      // No constants, no material: every square is flat this frame. And no light: the frame is
      // drawn as one without a light block, every draw unlit.
      dev->DestroyDeferred(terrain_cb_);
      terrain_cb_ = nullptr;
      for (Prepared& p : prepared_) {
        p.lit = false;
        if (p.terrain && p.status == MaterialStatus::kTextured) {
          p.status = MaterialStatus::kTextureBad;
          ++st.terrain_unusable[size_t(p.status)];
        }
      }
      for (TerrainPlanItem& item : terrain_items_) item.usable = false;
      frame_ = ClayFrameConstants{};
    }
  }
  if (textured) {
    PlanTerrain(terrain_items_, &terrain_scratch_);
  } else {
    // The texture path is off: every terrain record is flat clay, drawn opaque.
    for (TerrainPlanItem& item : terrain_items_) {
      item.action = item.drawable ? TerrainAction::kFlat : TerrainAction::kSkip;
    }
  }
  terrain_sets_.clear();
  uint32_t slot = 1;  // (slot 0: the frame's light)
  for (Prepared& p : prepared_) {
    if (!p.terrain) continue;
    const TerrainPlanItem& item = terrain_items_[p.plan];
    const capture::DrawRecord& r = *p.r;
    if (item.action == TerrainAction::kSkip) {
      // (a record without geometry was counted by the geometry cache)
      if (item.drawable) ++st.terrain_skipped;
      p.pipeline = nullptr;
      continue;
    }
    const bool material = item.action == TerrainAction::kMaterial;
    if (material) {
      p.textured = TerrainTextured(r.material.kind);
    } else {
      p.albedo = p.weights = p.side = p.base = nullptr;
      p.uvs = nullptr;
      p.normals = nullptr;  // (flat clay has no light)
      p.light = nullptr;
    }
    // The light inputs of a record drawn with its material, as PrepareLight counts a mesh record's.
    if (material && p.light) {
      ++st.lit;
      if (p.normals) {
        ++st.light_ready;
      } else {
        ++st.light_missing[size_t(p.light_why)];
        st.light_missing_by_vs.Add(r.vs_hash, uint8_t(p.light_why));
      }
    }
    const DrawState d = DecideDrawState(capture::DecodeRenderState(r.state_words), r.material, p.textured, !material);
    p.alpha = d.alpha;
    // With the material: the pipeline PrepareTerrain found. Flat: the layer's
    // depth and cull without its blend, or else the flat terrain default, which
    // also tests less-equal (the flat draw lies at its underlay's depth). A
    // record that found no pipeline for its material is in the overflow count
    // already and is not counted again here.
    if (!material) {
      const uint32_t counted = st.pipeline_overflow;
      p.pipeline = PipelineFor(dev, d.key, st, pipelines_[kTerrainFlatSlot]);
      if (p.no_pipeline) st.pipeline_overflow = counted;
    }
    if (d.alpha_tested) ++st.alpha_tested;
    if (d.blended) ++st.blended;
    if (d.state_invalid) ++st.state_invalid;
    if (d.alpha_unknown) {
      ++st.alpha_unknown;
      st.unknown_by_ps.Add(r.material.ps_hash, kTallyAlphaUnknown);
    }
    if (material) {
      p.slot = slot++;
      // The weights are read with a linear filter whatever the fetch constant says (it is the
      // vertex shader's, point sampled at the cell's texel centres).
      // (a layer with the steep-ground block has its side texture and normals here: TerrainSteepStatus)
      ClayTerrainConstants c = MakeClayTerrainConstants(item.patch->material, r.material.kind, p.uv_fix,
                                                        p.sampler, p.weight_sampler | 1u,
                                                        p.side ? p.side_fix : nullptr, p.side_sampler);
      // Lit when the plan says its square is (a far draw: when it can be), with the light's constants.
      p.lit = item.light && p.light && p.base && p.normals &&
              SetClayTerrainLight(c, item.patch->material, *p.light, p.base_fix, p.base_sampler);
      if (!p.lit) p.base = nullptr;
      std::memcpy(slots + size_t(p.slot) * kClayTerrainSlotBytes, &c, sizeof(c));
      const std::array<nrhi::TextureView*, 4> set{p.albedo, p.weights, p.side, p.base};
      if (std::find(terrain_sets_.begin(), terrain_sets_.end(), set) == terrain_sets_.end()) {
        terrain_sets_.push_back(set);
      }
    }
    if (textured) {
      // The final status of a drawn terrain record: textured, its own reason, or its square's.
      const MaterialStatus status = material ? MaterialStatus::kTextured
                                    : p.status == MaterialStatus::kTextured ? MaterialStatus::kTerrainSquare
                                                                            : p.status;
      ++tst.status[size_t(status)];
      ++tst.terrain;
      if (material) {
        ++tst.textured;
        ++tst.terrain_textured;
      }
    }
  }
  st.terrain_tuples = uint32_t(terrain_sets_.size());
  // The lit objects' slots follow the terrain's; slot 0 is the frame's light.
  object_slot0_ = slot;
  if (slots) {
    std::memcpy(slots, &frame_, sizeof(frame_));
    for (size_t i = 0; i < object_constants_.size(); ++i) {
      std::memcpy(slots + (size_t(object_slot0_) + i) * kClayTerrainSlotBytes, &object_constants_[i],
                  sizeof(ClayObjectConstants));
    }
  }
}

void ClayPass::Render(nrhi::Cmd* cmd, nrhi::Device* dev, const FrameScene& scene, ClayColor color,
                      bool textured, ClayLight light, ClayStats& st, TextureStats& tst) {
  if (!ready_ || dev != device_) return;
  // A latched texture cache (RHI failure) resolves nothing: skip the UV decode
  // too, and report the latch on F3.
  textured = textured && !textures_.latched();
  const auto t0 = std::chrono::steady_clock::now();
  const double hash0 = st.hash_ms, decode0 = st.decode_ms, tex0 = tst.decode_ms;
  // The frame's light: of this scene's own block or none (never another frame's). The light is
  // of the material, so without the texture path there is none.
  if (!textured) light = ClayLight::kOff;
  frame_ = MakeClayFrameConstants(scene.light, light);
  object_constants_.clear();
  object_slot0_ = 0;

  // Geometry, albedo and render state of every drawable record. Texture
  // uploads record copies and barriers, so this runs before the render targets
  // are bound; pipelines are created here too.
  prepared_.clear();
  terrain_items_.clear();
  terrain_textures_.clear();
  for (const capture::DrawRecord& r : scene.draws) {
    if (r.terrain_index != capture::kNoSideIndex) {
      // A terrain record: its inputs are in the scene's side array. One whose
      // index the scene does not hold (the builder makes none) has nothing it
      // could be drawn from.
      if (const capture::TerrainPatch* terrain = TerrainOf(scene, r)) {
        PrepareTerrain(cmd, dev, r, *terrain, LightOf(scene, r), textured, st, tst);
      } else {
        ++st.skipped_other;
      }
      continue;
    }
    const capture::RenderState rs = capture::DecodeRenderState(r.state_words);
    const bool may_texture = textured && r.material.status == capture::MaterialStatus::kTextured;
    auto leave_out = [&] {
      ++st.blend_unknown;
      st.unknown_by_ps.Add(r.material.ps_hash, kTallyBlendUnknown);
    };
    // A blended draw that cannot be textured is left out before any geometry
    // is built for it.
    if (!may_texture && DecideDrawState(rs, r.material, 0, false).skip) {
      leave_out();
      continue;
    }
    Prepared p;
    p.r = &r;
    uint32_t buffer_vertices = 0;
    p.positions = geometry_.Positions(dev, r, nullptr, &buffer_vertices, st);
    if (!p.positions) continue;
    p.vertex_count = DrawVertexCount(buffer_vertices, RecordVertexCount(r, nullptr));
    p.indices = geometry_.Indices(dev, r, nullptr, p.vertex_count, &p.index_count, st, &p.max_index);
    if (!p.indices) continue;
    capture::MaterialStatus status = r.material.status;  // capture statuses pass through
    if (may_texture) {
      // UVs first: a stream that does not decode costs no texture upload.
      p.uvs = geometry_.Uvs(dev, r, p.vertex_count, st);
      if (!p.uvs) {
        status = capture::MaterialStatus::kUvUnsupported;
      } else {
        p.albedo = textures_.Resolve(cmd, dev, r.material, &p.sampler, p.uv_fix, &status, tst);
        if (!p.albedo) p.uvs = nullptr;
      }
    }
    const DrawState d = DecideDrawState(rs, r.material, p.albedo ? 1 : 0, false);
    if (d.skip) {  // blended, and its texture is not usable this frame
      leave_out();
      continue;
    }
    if (textured) {
      if (size_t(status) < size_t(capture::MaterialStatus::kCount)) ++tst.status[size_t(status)];
      if (p.albedo) ++tst.textured;
    }
    p.alpha = d.alpha;
    p.pipeline = PipelineFor(dev, d.key, st);
    if (d.alpha_tested) ++st.alpha_tested;
    if (d.blended) ++st.blended;
    if (d.state_invalid) ++st.state_invalid;
    if (d.alpha_unknown) {
      ++st.alpha_unknown;
      st.unknown_by_ps.Add(r.material.ps_hash, kTallyAlphaUnknown);
    }
    // A record drawn with its texture and with light inputs: its normals and colours (the light
    // is of the material; a record that stays flat clay has none).
    if (p.albedo) {
      if (const capture::LightInputs* inputs = LightOf(scene, r)) PrepareLight(dev, r, *inputs, p, st);
    }
    prepared_.push_back(p);
  }
  FinishTerrain(dev, textured, st, tst);
  // (after FinishTerrain: without the frame's constant buffer the frame has no light)
  st.light_switch = light == ClayLight::kOn;
  st.light_block = frame_.light_on != 0;
  if (st.light_block) {
    std::copy(std::begin(frame_.sun_dir), std::end(frame_.sun_dir), std::begin(st.light_sun_dir));
    std::copy(std::begin(frame_.sun_col), std::end(frame_.sun_col), std::begin(st.light_sun_col));
  }
  // A slot whose creation failed holds no pipeline.
  st.pipelines = uint32_t(pipelines_.size() - std::count(pipelines_.begin(), pipelines_.end(),
                                                             static_cast<nrhi::Pipeline*>(nullptr)));

  if (white_staging_) {
    cmd->CopyBufferToTexture(white_, 0, 0, white_staging_, 0, nrhi::kRowPitchAlignment, 1, 1, 1);
    cmd->Barrier(white_, nrhi::ResourceState::kCopyDest, nrhi::ResourceState::kPixelShaderResource);
    dev->DestroyDeferred(white_staging_);
    white_staging_ = nullptr;
  }
  cmd->Barrier(linear_, nrhi::ResourceState::kPixelShaderResource,
               nrhi::ResourceState::kRenderTarget);
  cmd->Barrier(depth_, nrhi::ResourceState::kPixelShaderResource,
               nrhi::ResourceState::kDepthWrite);
  cmd->FlushBarriers();
  cmd->SetRenderTargets(linear_, depth_);
  cmd->ClearRenderTarget(linear_, kClayClearLinear);
  cmd->ClearDepth(depth_, 1.0f);
  cmd->SetViewport({0.0f, 0.0f, float(kWidth), float(kHeight), 0.0f, 1.0f});
  cmd->SetScissor({0, 0, int32_t(kWidth), int32_t(kHeight)});
  cmd->SetBindingLayout(layout_);
  cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);
  // The terrain table and constant buffer hold something from the first draw on
  // (only the terrain shaders read them, and each terrain draw binds its own).
  {
    nrhi::TextureView* const whites[kTerrainTableSize] = {white_view_, white_view_, white_view_, white_view_};
    cmd->SetTextures(kParamTerrainTextures, whites, kTerrainTableSize);
  }
  // The frame's light at b2 for the whole frame (slot 0), and b1 on something
  // from the first draw on: only a terrain draw and a lit object read b1, and
  // each binds its own slot.
  if (terrain_cb_) {
    cmd->SetConstantBuffer(kParamTerrainConstants, terrain_cb_, 0);
    cmd->SetConstantBuffer(kParamFrameConstants, terrain_cb_, 0);
  }

  nrhi::Pipeline* bound = nullptr;
  for (const Prepared& p : prepared_) {
    if (!p.pipeline) continue;  // a terrain record that is not drawn (FinishTerrain)
    const capture::DrawRecord& r = *p.r;
    if (p.pipeline != bound) {
      cmd->SetPipeline(p.pipeline);
      bound = p.pipeline;
    }
    // A terrain record with its material has its textures in the terrain table;
    // a far one reads its colour coordinates from t2 like a textured draw. Its
    // vertex normals (as they are: no transform) are at t8 when the steep-ground
    // block or the light reads them.
    const bool terrain_material = p.textured != 0;
    const bool has_albedo = p.albedo != nullptr && !terrain_material;
    const bool has_uvs = has_albedo || (p.textured == kTexturedTerrainFar && p.uvs != nullptr);
    uint32_t light_mode = 0, light_count = 0;
    if (terrain_material && p.normals && (p.side || p.lit)) {
      light_mode |= kLightModeTerrainNormals;
      light_count = p.vertex_count;  // (TerrainNormals gives one per grid point or nothing)
    }
    if (p.lit) {
      light_mode |= kLightModeLit;
      if (!p.terrain) {
        light_mode |= kLightModeObject;
        light_count = p.light_count;
      }
    }
    float uv[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    if (has_albedo) {
      // BC textures are padded to whole blocks: scale into the guest extent.
      const float* x = r.material.uv_xform;
      uv[0] = x[0] * p.uv_fix[0];
      uv[1] = x[1] * p.uv_fix[1];
      uv[2] = x[2] * p.uv_fix[0];
      uv[3] = x[3] * p.uv_fix[1];
    } else if (p.textured == kTexturedTerrainFar) {
      uv[0] = p.uv_fix[0];
      uv[1] = p.uv_fix[1];
    }
    const ClayConstants constants = MakeClayConstants(r, p.vertex_count, ClayColorValue(color, r),
                                                      uv, has_uvs, p.sampler, p.alpha, light_mode, light_count);
    cmd->SetRootConstants(kParamConstants, kConstantCount, &constants, 0);
    cmd->SetBufferSrv(kParamPositions, p.positions, 0);
    cmd->SetBufferSrv(kParamIndices, p.indices, 0);
    // Draws without UVs never read t2; bind the (in-range) positions.
    cmd->SetBufferSrv(kParamUvs, has_uvs ? p.uvs : p.positions, 0);
    cmd->SetTexture(kParamAlbedo, has_albedo ? p.albedo : white_view_);
    if (terrain_material) {
      // The white view stands in for the weights of a far draw, for the side
      // texture of a draw without the steep-ground block and for the base map
      // of a draw without light: the backend skips a null entry of a table.
      nrhi::TextureView* const set[kTerrainTableSize] = {p.albedo, p.weights ? p.weights : white_view_,
                                                         p.side ? p.side : white_view_,
                                                         p.base ? p.base : white_view_};
      cmd->SetTextures(kParamTerrainTextures, set, kTerrainTableSize);
      cmd->SetConstantBuffer(kParamTerrainConstants, terrain_cb_, uint64_t(p.slot) * kClayTerrainSlotBytes);
    }
    // The light's three vertex buffers, at EVERY draw: the draw's own where its light_mode names
    // them, else the draw's positions, as for t2. A buffer bound by address has no bounds check
    // on the GPU, so nothing may stay bound from another draw: this draw's vertex indices would
    // address that draw's (smaller) buffer. The positions hold an element of 16 bytes for every
    // vertex the draw can name, more than any element of the three (kClayLightElementBytesMax),
    // so a load the shader made for this draw would lie inside them (it makes none: the loads are
    // inside branches on light_mode).
    // Which of the three is the draw's own: LightBufferSources (clay_logic.h). A buffer the
    // mode names is there (the mode is only set with it); were it not, the positions stand in.
    const ClayLightBuffers light_buffers = LightBufferSources(light_mode);
    for (uint32_t i = 0; i < 3; ++i) {
      nrhi::Buffer* own = light_buffers.source[i] == ClayLightBuffer::kColours     ? p.colours
                          : light_buffers.source[i] == ClayLightBuffer::kPositions ? p.positions
                                                                                   : p.normals;
      cmd->SetBufferSrv(kClayLightBufferParam0 + i, own ? own : p.positions, 0);
    }
    if (light_mode & kLightModeObject) {
      cmd->SetConstantBuffer(kParamTerrainConstants, terrain_cb_,
                             (uint64_t(object_slot0_) + p.slot) * kClayTerrainSlotBytes);
    }

    cmd->Draw(p.index_count, 0);
    ++st.drawn;
    // The light of the drawn record: its family's, or why it has none.
    if (p.lit) {
      ++st.light_drawn[size_t(p.light->family)];
    } else {
      const ClayUnlit why = light != ClayLight::kOn ? ClayUnlit::kOff
                            : !frame_.light_on      ? ClayUnlit::kBlock
                                                    : p.unlit;
      ++st.light_unlit[size_t(why)];
      if (why == ClayUnlit::kUnknown || why == ClayUnlit::kNormal) {
        st.light_unlit_by_ps.Add(r.material.ps_hash, uint8_t(why));
      }
    }
    if (r.deformed) ++st.deformed;
    // Which builder made the positions (the order of GeometryCache::Positions).
    if (!p.terrain) {
      if (r.instances.active) {
        ++st.instanced;
      } else if (r.skin.active) {
        ++st.skinned;
      }
    }
  }

  // The output mapping: every pixel of the mapped target from the linear target
  // of this Render (cleared and drawn above), so the composite never samples a
  // mapped target older than its linear one, or one that was never written.
  cmd->Barrier(linear_, nrhi::ResourceState::kRenderTarget,
               nrhi::ResourceState::kPixelShaderResource);
  cmd->Barrier(depth_, nrhi::ResourceState::kDepthWrite,
               nrhi::ResourceState::kPixelShaderResource);
  cmd->Barrier(color_, nrhi::ResourceState::kPixelShaderResource,
               nrhi::ResourceState::kRenderTarget);
  cmd->FlushBarriers();
  cmd->SetRenderTargets(color_, nullptr);
  cmd->SetViewport({0.0f, 0.0f, float(kWidth), float(kHeight), 0.0f, 1.0f});
  cmd->SetScissor({0, 0, int32_t(kWidth), int32_t(kHeight)});
  cmd->SetBindingLayout(map_layout_);
  cmd->SetPipeline(map_pipeline_);
  // The exposure: the one source, applied only here (1 for a frame drawn without light).
  const float map_constants[kMapConstantCount] = {ClayFrameExposure(frame_), 0.0f, 0.0f, 0.0f};
  cmd->SetRootConstants(kMapParamConstants, kMapConstantCount, map_constants, 0);
  cmd->SetTexture(kMapParamLinear, linear_view_);
  cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);
  cmd->Draw(3, 0);
  cmd->Barrier(color_, nrhi::ResourceState::kRenderTarget,
               nrhi::ResourceState::kPixelShaderResource);
  cmd->FlushBarriers();

  if (terrain_cb_) {
    // In use by this submission: reusable once it has completed.
    terrain_destroy_.clear();
    terrain_pool_.Retire(terrain_cb_, terrain_cb_bytes_, dev->CurrentSubmission(), &terrain_destroy_);
    for (nrhi::Buffer* b : terrain_destroy_) dev->DestroyDeferred(b);
    terrain_destroy_.clear();
    terrain_cb_ = nullptr;
  }

  st.resident_bytes = geometry_.resident_bytes();
  tst.resident_bytes = textures_.resident_bytes();
  tst.latched = textures_.latched();
  const double total =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  st.record_ms += std::max(0.0, total - (st.hash_ms - hash0) - (st.decode_ms - decode0) -
                                    (tst.decode_ms - tex0));
}

}  // namespace fable2::native::render
