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
// The output mapping's binding parameters: its constants (word 0 the factor of
// the linear value, 1 in every frame: kClayMapExposure) and the linear target at t0.
constexpr uint32_t kMapParamConstants = 0, kMapParamLinear = 1, kMapConstantCount = 4;
// The mask step's binding parameters: its constants (ClayMaskConstants, a slot of the frame's
// constant buffer) at b0 and its table at t0 to t3: the scene's depth, the near map, the far map,
// the rotation tile.
constexpr uint32_t kMaskParamConstants = 0, kMaskParamTextures = 1, kMaskTableSize = 4;
constexpr uint32_t kConstantCount = sizeof(ClayConstants) / 4;  // 36
static_assert(kConstantCount == 36);
// Binding parameters: indices into kClayBindings (clay_logic.h), from which
// Ensure creates the layout. Root signature cost: 56 of 64 words (the reason
// the light's per-draw values are in a constant buffer slot and not in the
// root constants); twelve parameters, the backend's limit. kParamAlbedo is a
// pair: the draw's albedo (t3) and the frame's screen mask (t4; SetTexturePair
// at every draw). kParamTerrainConstants
// is register b1: cbuffer Terrain of a terrain draw, cbuffer Object of a lit
// object, trunk or plant. kParamColours (t11) and kParamProbeWeights (t12)
// are by the draw's kind: a stream colour, a probe set or the colour of a
// plant's copy; the probe weights or a plant's bend inputs.
constexpr uint32_t kParamConstants = 0, kParamPositions = 1, kParamIndices = 2, kParamUvs = 3,
                   kParamAlbedo = 4, kParamTerrainTextures = 5, kParamTerrainConstants = 6,
                   kParamFrameConstants = 7, kParamTerrainNormals = 8, kParamNormals = 9, kParamColours = 10,
                   kParamProbeWeights = 11;
constexpr uint32_t kParamCount = uint32_t(std::size(kClayBindings));
// The terrain shaders' table: colour, weights, side texture, base map (t5 to t8).
constexpr uint32_t kTerrainTableSize = 4;
constexpr bool ParamIs(uint32_t param, ClayBindingKind kind, uint32_t reg, uint32_t count) {
  return param < kParamCount && kClayBindings[param].kind == kind && kClayBindings[param].reg == reg &&
         kClayBindings[param].count == count;
}
static_assert(kParamCount == 12 && kParamCount <= nrhi::kMaxBindingParams);
static_assert(ParamIs(kParamConstants, ClayBindingKind::kConstants, 0, kConstantCount));
static_assert(ParamIs(kParamPositions, ClayBindingKind::kBuffer, 0, 1) && ParamIs(kParamIndices, ClayBindingKind::kBuffer, 1, 1) &&
              ParamIs(kParamUvs, ClayBindingKind::kBuffer, 2, 1));
static_assert(ParamIs(kParamAlbedo, ClayBindingKind::kTextures, 3, 2) && kParamAlbedo == kClayAlbedoParam &&
              ParamIs(kParamTerrainTextures, ClayBindingKind::kTextures, 5, kTerrainTableSize));
static_assert(ParamIs(kParamTerrainConstants, ClayBindingKind::kConstantBuffer, 1, 1) &&
              ParamIs(kParamFrameConstants, ClayBindingKind::kConstantBuffer, 2, 1));
static_assert(ParamIs(kParamTerrainNormals, ClayBindingKind::kBuffer, 9, 1) && ParamIs(kParamNormals, ClayBindingKind::kBuffer, 10, 1) &&
              ParamIs(kParamColours, ClayBindingKind::kBuffer, 11, 1) && ParamIs(kParamProbeWeights, ClayBindingKind::kBuffer, 12, 1) &&
              kParamTerrainNormals == kClayLightBufferParam0 && kParamProbeWeights + 1 == kClayLightBufferParam0 + kClayLightBufferCount);
// The offsets the pass binds from are ones the RHI accepts: the constant buffer's slots and the
// probe buffer's second region (LightBufferOffset) are multiples of its alignment.
static_assert(kClayTerrainSlotBytes % nrhi::kBufferOffsetAlignment == 0 && kClayObjectSlotBytes % nrhi::kBufferOffsetAlignment == 0 &&
              kProbeRegionAlignment % nrhi::kBufferOffsetAlignment == 0);
static_assert(kMaskTableSize <= nrhi::kMaxTextureTableSize && kTerrainTableSize <= nrhi::kMaxTextureTableSize);
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
  DestroyShadowObjects();  // (first: its view of depth_ and its pipelines of vs_ and layout_)
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
nrhi::Pipeline* ClayPass::CreatePipeline(nrhi::Device* dev, const PipelineKey& key, bool depth_only) {
  nrhi::GraphicsPipelineDesc pipe;
  pipe.layout = layout_;
  pipe.vs = vs_;
  const uint8_t kind = key.textured < kClayPixelShaders ? key.textured : 0;
  // (the pre-pass: the kind's depth-only form; flat clay discards nothing and keeps its shader)
  pipe.ps = depth_only && depth_ps_[kind] ? depth_ps_[kind] : ps_[kind];
  if (depth_only && kind != 0 && !depth_ps_[kind]) return nullptr;
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
    ForgetShadowObjects();
    shadow_objects_failed_ = false;  // (another device: its shadows are tried anew)
    white_ = nullptr;
    white_view_ = nullptr;
    white_staging_ = nullptr;
    layout_ = nullptr;
    map_layout_ = nullptr;
    mask_layout_ = nullptr;
    device_ = dev;
  } else {
    DestroyObjects();  // retry after a partial failure
    // (the F6 retry after a failure of the pass rebuilds everything, so the shadows' objects are
    // tried anew with the rest. A failure of the shadows' objects alone does not latch the renderer
    // and F6 does not come here for it: those stay off until this path, another device or a restart.)
    shadow_objects_failed_ = false;
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
  // (the shadows' maps and mask are made, and logged, on the first frame that draws them: EnsureShadowObjects)
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
    // probe constants) or is the copy's colour. An instanced plant also needs the inputs of its
    // bent normal (the copy's up axis and the vertex's height): its colours AND its bend inputs, or
    // neither and no light.
    LightSecond refused = LightSecond::kNone;
    uint32_t second_count = 0;
    if (light.kind == capture::LightVertexKind::kProbe) {
      p.probe = geometry_.Probe(dev, r, light, need, st, &refused, &second_count);
      p.probe_count = second_count;
    } else {
      p.colours = geometry_.InstanceColours(dev, r, light, need, st, &refused, &second_count);
      if (p.colours) {
        uint32_t bend_count = 0;
        uint64_t bend_bytes = 0;
        p.bend = geometry_.InstanceBend(dev, r, need, st, &refused, &bend_count, &bend_bytes);
        if (p.bend) {
          ++st.light_bend;
          st.light_bend_bytes += bend_bytes;
          second_count = std::min(second_count, bend_count);
        } else {
          p.colours = nullptr;
        }
      }
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
  if (light.family == capture::LightFamily::kObject || light.family == capture::LightFamily::kTreeTrunk ||
      light.family == capture::LightFamily::kTreeLeaf) {
    const RotationKind kind = ClassifyRotation(light.world_rot, light.world_scale);
    ++st.light_rotation[size_t(kind)];
    if (kind != RotationKind::kRotation) st.light_rotation_by_vs.Add(r.vs_hash, uint8_t(kind));
  }
  // The light itself: in a lit frame, for a family the pass lights, with the buffers of the
  // record's OWN vertex kind (LightObjectReady: a colour stream's colours; the probe buffer; an
  // instance set's colours and bend inputs), when every value of the draw's is usable (a rotation
  // without a direction is not: counted above, drawn unlit; one probe register or bend constant
  // that is not a number of usable size: the whole block is refused). An instanced plant's values
  // fit a small slot (kClayPlantConstantBytes); an object's, trunk's or leaf's take a large one. A
  // leaf (a probe record) also needs the frame's eye (LightFrameLights).
  ClayObjectConstants constants;
  if (LightFrameLights(frame_, light.family) && LightFamilyDrawn(light) &&
      LightObjectReady(light.kind, p.normals != nullptr, p.colours != nullptr, p.probe != nullptr, p.bend != nullptr) &&
      MakeClayObjectConstants(light, &constants)) {
    p.lit = true;
    std::vector<ClayObjectConstants>& list = light.kind == capture::LightVertexKind::kInstance ? plant_constants_ : object_constants_;
    p.slot = uint32_t(list.size());
    list.push_back(constants);
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
  p.positions = geometry_.Positions(dev, r, &terrain, &buffer_vertices, st, &p.bounds);
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
  // bound at every draw, whether or not the draw reads them. A frame whose buffer cannot be made
  // draws nothing (Render, ClayRecordsWithoutConstants).
  // (the shadow draws bind it too: the clay vertex shader declares b1 and b2 in every draw)
  const bool wanted = !prepared_.empty() || !shadow_prepared_[0].empty() || !shadow_prepared_[1].empty();
  if (wanted) {
    // Slot 0 the frame's light, one slot per terrain record (at most that many are drawn with the
    // material) and one per lit plant, 256 bytes each; then one slot of 512 bytes per lit object or
    // trunk. Sized so that the largest block a shader declares at b1 lies inside the buffer from
    // the start of every slot (ClayConstantBufferBytes).
    // (and one small slot for the mask step's constants, used or not)
    const uint64_t bytes = ClayConstantBufferBytes(2 + uint64_t(terrain_items_.size()) + plant_constants_.size(),
                                                   object_constants_.size());
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
    terrain_cb_mapped_ = slots;
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
  // The lit plants' small slots follow the terrain's, and the lit objects' and trunks' slots
  // (kClayObjectSlotBytes each) follow the small slots that are in use: slot 0, the frame's light,
  // the terrain's, the plants' and the mask's. (`slot` is then at most 2 + terrain_items_.size() +
  // plant_constants_.size(), the count the buffer was sized with.)
  plant_slot0_ = slot;
  slot += uint32_t(plant_constants_.size());
  mask_slot_ = slot++;  // the mask step's constants (Render writes them when the frame has a mask)
  object_slot0_ = slot;
  if (slots) {
    std::memcpy(slots, &frame_, sizeof(frame_));
    for (size_t i = 0; i < plant_constants_.size(); ++i) {
      // (what a plant's shader path reads: the block's first bytes)
      std::memcpy(slots + ClaySmallSlotOffset(plant_slot0_ + i), &plant_constants_[i], kClayPlantConstantBytes);
    }
    for (size_t i = 0; i < object_constants_.size(); ++i) {
      std::memcpy(slots + ClayObjectSlotOffset(object_slot0_, i), &object_constants_[i], sizeof(ClayObjectConstants));
    }
  }
}

void ClayPass::PrepareShadows(nrhi::Cmd* cmd, nrhi::Device* dev, const FrameScene& scene, bool draw, ClayStats& st,
                              TextureStats& tst) {
  for (std::vector<ShadowPrepared>& list : shadow_prepared_) list.clear();
  shadow_wanted_ = draw;
  // What the frame holds, whether or not it is drawn: the Shadow line.
  st.shadow_block = scene.shadow.valid;
  st.shadow_reason = scene.shadow_reason;
  st.shadow_extra = scene.shadow_extra;
  st.shadow_passes = scene.shadow_passes;
  std::copy(std::begin(scene.shadow.split), std::end(scene.shadow.split), std::begin(st.shadow_split));
  for (size_t c = 0; c < 2; ++c) {
    st.shadow_records[c] = uint32_t(scene.shadow_draws[c].size());
    std::copy(std::begin(scene.shadow_skipped[c]), std::end(scene.shadow_skipped[c]), std::begin(st.shadow_capture_skipped[c]));
  }
  if (!draw) return;
  // The geometry cache's counters of the casters: its time and its uploads and hits are the
  // frame's; a caster whose index list is bad is a shadow skip (geometry), not a scene record's.
  ClayStats gst;
  for (size_t c = 0; c < 2; ++c) {
    const std::vector<capture::DrawRecord>& draws = scene.shadow_draws[c];
    const std::vector<capture::ShadowCaster>& casters = scene.shadow_casters[c];
    for (size_t i = 0; i < draws.size(); ++i) {
      const capture::DrawRecord& r = draws[i];
      auto skip = [&](ShadowDrawSkip why) {
        ++st.shadow_skipped[c][size_t(why)];
        st.shadow_skipped_by_vs.Add(r.vs_hash, uint8_t(why));
      };
      if (i >= casters.size()) {  // a record without its twin entry (the builder makes none)
        skip(ShadowDrawSkip::kTwin);
        continue;
      }
      const ShadowDrawState d = DecideShadowDraw(r, casters[i]);
      if (d.skip != ShadowDrawSkip::kNone) {
        skip(d.skip);
        continue;
      }
      ShadowPrepared p;
      p.vs_hash = r.vs_hash;
      uint32_t buffer_vertices = 0, vertex_count = 0;
      p.positions = geometry_.Positions(dev, r, nullptr, &buffer_vertices, gst);
      if (p.positions) {
        vertex_count = DrawVertexCount(buffer_vertices, RecordVertexCount(r, nullptr));
        p.indices = geometry_.Indices(dev, r, nullptr, vertex_count, &p.index_count, gst);
      }
      if (!p.positions || !p.indices) {
        skip(ShadowDrawSkip::kGeometry);
        continue;
      }
      float uv[4] = {1.0f, 1.0f, 0.0f, 0.0f};
      uint32_t sampler = 0;
      if (d.textured) {
        // The coordinates first, as for a scene record: a stream that does not decode costs no upload.
        float uv_fix[2] = {1.0f, 1.0f};
        capture::MaterialStatus status = r.material.status;
        p.uvs = geometry_.Uvs(dev, r, vertex_count, gst);
        if (p.uvs) p.albedo = textures_.Resolve(cmd, dev, r.material, &sampler, uv_fix, &status, tst);
        if (!p.uvs || !p.albedo) {
          skip(ShadowDrawSkip::kTexture);
          continue;
        }
        const float* x = r.material.uv_xform;  // (finite: DecideShadowDraw)
        uv[0] = x[0] * uv_fix[0];
        uv[1] = x[1] * uv_fix[1];
        uv[2] = x[2] * uv_fix[0];
        uv[3] = x[3] * uv_fix[1];
      }
      p.pipeline = shadow_pipelines_[ShadowPipelineSlot(d.cull)];
      if (!p.pipeline) {
        skip(ShadowDrawSkip::kPipeline);
        continue;
      }
      p.constants = MakeShadowConstants(r, vertex_count, uv, d.textured, sampler, d.alpha, d.bias);
      shadow_prepared_[c].push_back(p);
    }
  }
  st.hash_ms += gst.hash_ms;
  st.decode_ms += gst.decode_ms;
  st.uploads += gst.uploads;
  st.hits += gst.hits;
}

void ClayPass::DrawShadows(nrhi::Cmd* cmd, ClayStats& st) {
  shadow_drawn_ = false;
  if (!shadow_wanted_) return;
  // Without the frame's constant buffer no clay draw can be made (b1 and b2 would be unset): the
  // maps are not touched and are not the frame's (shadow_drawn_ stays false: nothing may sample
  // or show them), and every prepared record is counted.
  const bool constants = terrain_cb_ != nullptr;
  if (!constants) {
    for (size_t c = 0; c < 2; ++c) {
      for (const ShadowPrepared& p : shadow_prepared_[c]) {
        ++st.shadow_skipped[c][size_t(ShadowDrawSkip::kConstants)];
        st.shadow_skipped_by_vs.Add(p.vs_hash, uint8_t(ShadowDrawSkip::kConstants));
      }
    }
    return;
  }
  cmd->SetBindingLayout(layout_);
  cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);
  {
    nrhi::TextureView* const whites[kTerrainTableSize] = {white_view_, white_view_, white_view_, white_view_};
    cmd->SetTextures(kParamTerrainTextures, whites, kTerrainTableSize);
  }
  cmd->SetConstantBuffer(kParamTerrainConstants, terrain_cb_, 0);
  cmd->SetConstantBuffer(kParamFrameConstants, terrain_cb_, 0);
  bool light_bound = false;  // (the light's four registers: once for both maps, below)
  for (size_t c = 0; c < 2; ++c) {
    // The map is a depth target for its own draws only; a shadow draw samples the caster's
    // texture or the white one, never a map.
    cmd->Barrier(shadow_map_[c], nrhi::ResourceState::kPixelShaderResource, nrhi::ResourceState::kDepthWrite);
    cmd->FlushBarriers();
    cmd->SetRenderTargets(nullptr, shadow_map_[c]);
    cmd->ClearDepth(shadow_map_[c], kShadowMapClear);
    // The origin of half a pixel puts the game's texel centres on the native ones (clay_logic.h,
    // "Native shadow maps", Texels); the scissor is the map.
    cmd->SetViewport({kShadowViewportOrigin, kShadowViewportOrigin, float(kShadowMapSize), float(kShadowMapSize), 0.0f, 1.0f});
    cmd->SetScissor({0, 0, int32_t(kShadowMapSize), int32_t(kShadowMapSize)});
    nrhi::Pipeline* bound = nullptr;
    for (const ShadowPrepared& p : shadow_prepared_[c]) {
      if (p.pipeline != bound) {
        cmd->SetPipeline(p.pipeline);
        bound = p.pipeline;
      }
      cmd->SetRootConstants(kParamConstants, kConstantCount, &p.constants, 0);
      cmd->SetBufferSrv(kParamPositions, p.positions, 0);
      cmd->SetBufferSrv(kParamIndices, p.indices, 0);
      cmd->SetBufferSrv(kParamUvs, p.uvs ? p.uvs : p.positions, 0);
      // (the pair's second entry, the screen mask, is not read by the maps' shader: white)
      cmd->SetTexturePair(kParamAlbedo, p.albedo ? p.albedo : white_view_, white_view_);
      // The light's four registers: a shadow draw has no light (light_mode 0) and loads from none
      // of them, so they are bound once (to the first draw's positions: a live buffer), not at
      // every draw.
      if (!light_bound) {
        for (uint32_t i = 0; i < kClayLightBufferCount; ++i) cmd->SetBufferSrv(kClayLightBufferParam0 + i, p.positions, 0);
        light_bound = true;
      }
      cmd->Draw(p.index_count, 0);
      ++st.shadow_drawn[c];
    }
    cmd->Barrier(shadow_map_[c], nrhi::ResourceState::kDepthWrite, nrhi::ResourceState::kPixelShaderResource);
    cmd->FlushBarriers();
  }
  shadow_drawn_ = true;
  st.shadow_maps = true;
}

void ClayPass::DrawPrepared(nrhi::Cmd* cmd, const Prepared& p, ClayColor color, bool depth_only, nrhi::TextureView* mask,
                            uint32_t* light_mode_out, bool* light_bound) {
  const capture::DrawRecord& r = *p.r;
  if (depth_only && p.prepass_flat) {
    // A pre-pass draw whose pixels are all kept (flat clay, or a textured record whose alpha
    // discards nothing; never a terrain record): the flat kind's pipeline, the positions and the
    // indices. No coordinates (t2 gets the positions, as for every draw without them), no albedo
    // (the white pair, which the flat shader does not read), no light.
    const float uv[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    const ClayConstants constants = MakeClayConstants(r, p.vertex_count, ClayColorValue(color, r), uv, false, 0, ClayAlpha{}, 0, 0);
    cmd->SetRootConstants(kParamConstants, kConstantCount, &constants, 0);
    cmd->SetBufferSrv(kParamPositions, p.positions, 0);
    cmd->SetBufferSrv(kParamIndices, p.indices, 0);
    cmd->SetBufferSrv(kParamUvs, p.positions, 0);
    cmd->SetTexturePair(kParamAlbedo, white_view_, white_view_);
    if (!light_bound || !*light_bound) {
      for (uint32_t i = 0; i < kClayLightBufferCount; ++i) cmd->SetBufferSrv(kClayLightBufferParam0 + i, p.positions, 0);
      if (light_bound) *light_bound = true;
    }
    if (light_mode_out) *light_mode_out = 0;
    cmd->Draw(p.index_count, 0);
    return;
  }
  // A terrain record with its material has its textures in the terrain table;
  // a far one reads its colour coordinates from t2 like a textured draw. Its
  // vertex normals (as they are: no transform) are at t9 when the steep-ground
  // block or the light reads them.
  const bool terrain_material = p.textured != 0;
  const bool has_albedo = p.albedo != nullptr && !terrain_material;
  const bool has_uvs = has_albedo || (p.textured == kTexturedTerrainFar && p.uvs != nullptr);
  uint32_t light_mode = 0, light_count = 0;
  // (the pre-pass draws depth alone: no steep-ground colour and no light, so no light input at all)
  if (!depth_only) {
    if (terrain_material && p.normals && (p.side || p.lit)) {
      light_mode |= kLightModeTerrainNormals;
      light_count = p.vertex_count;  // (TerrainNormals gives one per grid point or nothing)
    }
    if (p.lit) {
      if (p.terrain) {
        light_mode |= kLightModeLit;
      } else {
        // An object or trunk: by its vertex kind, the stream colour or the probe colour. An
        // instanced plant: by its family, without or with the ambient factor. A leaf: a probe
        // record with the leaf bit.
        light_mode |= LitObjectMode(p.light->kind, p.light->family);
        light_count = p.light_count;  // the smallest count of its normals and its kind's other buffers
      }
    }
  }
  if (light_mode_out) *light_mode_out = light_mode;
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
  // The albedo and the screen mask (t3, t4): both entries at every draw; the white texture where
  // the draw has no albedo, and as the mask of a frame without one and of every pre-pass draw.
  cmd->SetTexturePair(kParamAlbedo, has_albedo ? p.albedo : white_view_, mask ? mask : white_view_);
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
  // The light's four vertex buffers, at EVERY draw: the draw's own where its light_mode names
  // them, else the draw's positions, as for t2. A buffer bound by address has no bounds check
  // on the GPU, so nothing may stay bound from another draw: this draw's vertex indices would
  // address that draw's (smaller) buffer. The positions hold an element of 16 bytes for every
  // vertex the draw can name, as much as the largest element of the four
  // (kClayLightElementBytesMax), so a load the shader made for this draw would lie inside them
  // (it makes none: the loads are inside branches on light_mode).
  // Which of the four is the draw's own: LightBufferSources (clay_logic.h). A probe object's
  // two are the two regions of its probe buffer: the weights from byte 0 at t12, the set
  // indices at t11 from LightBufferOffset, the offset the buffer was built with
  // (p.probe_count, not the draw's light_count). An instanced plant's are its copies' colours
  // at t11 and its bend inputs at t12, both from byte 0. A buffer the mode names is there (the
  // mode is only set with it); were it not, the positions stand in, from byte 0.
  const ClayLightBuffers light_buffers = LightBufferSources(light_mode);
  for (uint32_t i = 0; i < kClayLightBufferCount; ++i) {
    nrhi::Buffer* own = nullptr;
    switch (light_buffers.source[i]) {
      case ClayLightBuffer::kTerrainNormals:
      case ClayLightBuffer::kNormals: own = p.normals; break;
      case ClayLightBuffer::kColours:
      case ClayLightBuffer::kInstanceColours: own = p.colours; break;
      case ClayLightBuffer::kProbeSets:
      case ClayLightBuffer::kProbeWeights: own = p.probe; break;
      case ClayLightBuffer::kBend: own = p.bend; break;
      case ClayLightBuffer::kPositions: break;
    }
    // (a depth-only draw has light_mode 0 and loads from none of the four: once a pass is enough,
    // so that the registers hold a live buffer)
    if (depth_only && light_bound && *light_bound) break;
    const uint64_t offset = own ? LightBufferOffset(light_buffers.source[i], p.probe_count) : 0;
    cmd->SetBufferSrv(kClayLightBufferParam0 + i, own ? own : p.positions, offset);
  }
  if (depth_only && light_bound) *light_bound = true;
  if (light_mode & kLightModeObject) {
    // The draw's slot at b1: a plant's small one among the small slots, an object's, trunk's or
    // leaf's large one behind them.
    const uint64_t at = (light_mode & kLightModeInstance) ? ClaySmallSlotOffset(uint64_t(plant_slot0_) + p.slot)
                                                          : ClayObjectSlotOffset(object_slot0_, p.slot);
    cmd->SetConstantBuffer(kParamTerrainConstants, terrain_cb_, at);
  }
  cmd->Draw(p.index_count, 0);
}

const PipelineKey* ClayPass::KeyOf(nrhi::Pipeline* pipeline) const {
  for (size_t i = 0; i < pipelines_.size(); ++i) {
    if (pipelines_[i] == pipeline && pipeline) return &pipeline_keys_[i];
  }
  return nullptr;
}

nrhi::Pipeline* ClayPass::PrepassPipeline(nrhi::Device* dev, nrhi::Pipeline* colour, const ClayAlpha& alpha, bool* flat) {
  const PipelineKey* key = KeyOf(colour);
  if (!key || !InPrepass(*key)) return nullptr;
  // (a textured record whose alpha discards nothing is drawn as flat clay: no texture read)
  const PipelineKey prepass = PrepassKey(*key, alpha);
  *flat = prepass.textured == 0;
  const size_t slot = FindOrAddPipelineSlot(prepass_keys_, prepass, kMaxPrepassPipelines);
  if (slot == kNoPipelineSlot) return nullptr;
  if (slot == prepass_pipelines_.size()) {  // a new key
    prepass_pipelines_.push_back(CreatePipeline(dev, prepass_keys_[slot], true));
    if (!prepass_pipelines_.back()) {
      REXLOG_ERROR("[native] clay: pre-pass pipeline creation failed for a render state (its draws are not in the pre-pass)");
    }
  }
  return prepass_pipelines_[slot];
}

nrhi::TextureView* ClayPass::UpdateRotation(nrhi::Cmd* cmd, nrhi::Device* dev) {
  uint8_t tile[kRotationTileBytes];
  // Read and checked every frame: the address is a constant of the captures, not a record's.
  if (!textures_.ReadRotationTile(tile)) return nullptr;
  if (rotation_valid_ && rotation_view_ && std::memcmp(tile, rotation_bytes_, sizeof(tile)) == 0) return rotation_view_;
  rotation_valid_ = false;
  if (!rotation_) {
    nrhi::TextureDesc desc;
    desc.width = desc.height = kRotationTileSize;
    desc.format = nrhi::Format::kR8G8_UNORM;
    desc.initial_state = nrhi::ResourceState::kCopyDest;
    rotation_ = dev->CreateTexture(desc);
    rotation_view_ = rotation_ ? dev->CreateTextureView(rotation_, nrhi::TextureViewDesc{}) : nullptr;
    if (!rotation_ || !rotation_view_) {
      dev->DestroyDeferred(rotation_view_);
      dev->DestroyDeferred(rotation_);
      rotation_ = nullptr;
      rotation_view_ = nullptr;
      return nullptr;
    }
  } else {
    cmd->Barrier(rotation_, nrhi::ResourceState::kPixelShaderResource, nrhi::ResourceState::kCopyDest);
  }
  // 32 rows of 64 bytes, each at the RHI's row pitch.
  static_assert(kRotationTileSize * 2 <= nrhi::kRowPitchAlignment);
  nrhi::BufferDesc bd;
  bd.size = uint64_t(nrhi::kRowPitchAlignment) * kRotationTileSize;
  bd.heap = nrhi::HeapKind::kUpload;
  bd.bind_class = nrhi::BufferBindClass::kCopySrc;
  nrhi::Buffer* staging = dev->CreateBuffer(bd);
  uint8_t* mapped = staging ? static_cast<uint8_t*>(dev->Map(staging)) : nullptr;
  if (mapped) {
    for (uint32_t y = 0; y < kRotationTileSize; ++y) {
      std::memcpy(mapped + size_t(y) * nrhi::kRowPitchAlignment, tile + size_t(y) * kRotationTileSize * 2, kRotationTileSize * 2);
    }
    dev->Unmap(staging);
    cmd->FlushBarriers();
    cmd->CopyBufferToTexture(rotation_, 0, 0, staging, 0, nrhi::kRowPitchAlignment, kRotationTileSize, kRotationTileSize, 1);
  }
  // (back to a shader resource in either case: with a failed staging buffer its contents are not the tile's)
  cmd->Barrier(rotation_, nrhi::ResourceState::kCopyDest, nrhi::ResourceState::kPixelShaderResource);
  cmd->FlushBarriers();
  dev->DestroyDeferred(staging);
  if (!mapped) return nullptr;
  std::memcpy(rotation_bytes_, tile, sizeof(tile));
  rotation_valid_ = true;
  return rotation_view_;
}

void ClayPass::ForgetShadowObjects() {
  for (nrhi::Pipeline*& p : shadow_pipelines_) p = nullptr;
  shadow_ps_ = nullptr;
  for (size_t c = 0; c < 2; ++c) {
    shadow_view_[c] = nullptr;
    shadow_map_[c] = nullptr;
    shadow_prepared_[c].clear();
  }
  shadow_wanted_ = shadow_drawn_ = false;
  depth_view_ = nullptr;
  mask_ = nullptr;
  mask_view_ = nullptr;
  mask_drawn_ = false;
  mask_ps_ = nullptr;
  mask_pipeline_ = nullptr;
  for (nrhi::Shader*& ps : depth_ps_) ps = nullptr;
  prepass_keys_.clear();
  prepass_pipelines_.clear();
  rotation_ = nullptr;
  rotation_view_ = nullptr;
  rotation_valid_ = false;
  shadow_objects_ = false;
}

void ClayPass::DestroyShadowObjects() {
  if (device_) {
    for (nrhi::Pipeline* p : shadow_pipelines_) device_->DestroyDeferred(p);
    device_->DestroyDeferred(shadow_ps_);
    for (size_t c = 0; c < 2; ++c) {
      device_->DestroyDeferred(shadow_view_[c]);
      device_->DestroyDeferred(shadow_map_[c]);
    }
    device_->DestroyDeferred(depth_view_);
    device_->DestroyDeferred(mask_view_);
    device_->DestroyDeferred(mask_);
    device_->DestroyDeferred(mask_pipeline_);
    device_->DestroyDeferred(mask_ps_);
    for (nrhi::Pipeline* p : prepass_pipelines_) device_->DestroyDeferred(p);
    for (nrhi::Shader* ps : depth_ps_) device_->DestroyDeferred(ps);
    device_->DestroyDeferred(rotation_view_);
    device_->DestroyDeferred(rotation_);
  }
  ForgetShadowObjects();
}

bool ClayPass::EnsureShadowObjects(nrhi::Device* dev) {
  if (shadow_objects_) return true;
  if (shadow_objects_failed_ || !ready_ || dev != device_) return false;
  auto fail = [&](const char* what) {
    REXLOG_ERROR("[native] clay: {} failed: the native shadows are off until the pass is rebuilt (another device, or an F6 retry "
                 "after a failure of the renderer) or the game is restarted; the lighting picture is drawn and the capture "
                 "records no shadow passes", what);
    DestroyShadowObjects();
    shadow_objects_failed_ = true;
    return false;
  };
  auto pixel_shader = [&](const char* name, const char* source) {
    nrhi::ShaderDesc sd;
    sd.stage = nrhi::ShaderStage::kPixel;
    sd.name = name;
    sd.hlsl_source = source;
    sd.entry_point = "main";
    return dev->CreateShader(sd);
  };
  // The shadow maps' pixel shader and their three depth-only pipelines, one a cull mode: the clay
  // vertex shader under the pass's layout, no colour target, the maps' depth function with write.
  // Not in the pipeline cache (no scene draw's state; never an overflow).
  shadow_ps_ = pixel_shader("fable2_clay_shadow_ps", shaders::kClayShadowPs);
  if (!shadow_ps_) return fail("shadow pixel shader compile");
  static constexpr capture::Cull kCulls[kShadowPipelines] = {capture::Cull::kNone, capture::Cull::kFront, capture::Cull::kBack};
  for (capture::Cull cull : kCulls) {
    nrhi::GraphicsPipelineDesc pipe;
    pipe.layout = layout_;
    pipe.vs = vs_;
    pipe.ps = shadow_ps_;
    pipe.rtv_format = nrhi::Format::kUnknown;
    pipe.dsv_format = nrhi::Format::kD32_FLOAT;
    pipe.depth.test_enable = true;
    pipe.depth.write_enable = true;
    pipe.depth.func = ToCompare(uint8_t(kShadowMapCompare));
    pipe.cull = ToCull(uint8_t(cull));
    nrhi::Pipeline* pipeline = dev->CreateGraphicsPipeline(pipe);
    if (!pipeline) return fail("shadow pipeline creation");
    shadow_pipelines_[ShadowPipelineSlot(cull)] = pipeline;
  }
  // The two shadow maps, of a size of their own (the game's 1024, whatever the window is). Their
  // contents are undefined until a Render draws them (shadow_drawn_).
  for (size_t c = 0; c < 2; ++c) {
    nrhi::TextureDesc map;
    map.width = map.height = kShadowMapSize;
    map.format = nrhi::Format::kD32_FLOAT;
    map.usage = nrhi::kTextureUsageDepthStencil;
    map.initial_state = nrhi::ResourceState::kPixelShaderResource;
    map.clear_depth = kShadowMapClear;
    shadow_map_[c] = dev->CreateTexture(map);
    shadow_view_[c] = shadow_map_[c] ? dev->CreateTextureView(shadow_map_[c], nrhi::TextureViewDesc{}) : nullptr;
    if (!shadow_map_[c] || !shadow_view_[c]) return fail("shadow map creation");
  }
  // The scene's depth as the mask step reads it, and the mask: one byte a pixel of the clay
  // target's size, undefined until a Render writes it (mask_drawn_).
  depth_view_ = dev->CreateTextureView(depth_, nrhi::TextureViewDesc{});
  nrhi::TextureDesc mask;
  mask.width = kWidth;
  mask.height = kHeight;
  mask.format = nrhi::Format::kR8_UNORM;
  mask.usage = nrhi::kTextureUsageRenderTarget;
  mask.initial_state = nrhi::ResourceState::kPixelShaderResource;
  for (float& c : mask.clear_color) c = 1.0f;
  mask_ = dev->CreateTexture(mask);
  mask_view_ = mask_ ? dev->CreateTextureView(mask_, nrhi::TextureViewDesc{}) : nullptr;
  if (!depth_view_ || !mask_ || !mask_view_) return fail("mask target creation");
  // The mask step: its constants (a slot of the frame's constant buffer) at b0 and its four
  // textures at t0 to t3 (the depth, the near map, the far map, the rotation tile), all read
  // with Load: no sampler. A full-screen triangle (the output mapping's vertex shader).
  if (!mask_layout_) {
    nrhi::BindingLayoutDesc desc;
    desc.params[kMaskParamConstants].kind = nrhi::BindingParamKind::kConstantBuffer;
    desc.params[kMaskParamConstants].shader_register = 0;
    desc.params[kMaskParamConstants].count = 1;
    desc.params[kMaskParamConstants].visibility = nrhi::Visibility::kPixel;
    desc.params[kMaskParamTextures].kind = nrhi::BindingParamKind::kTextureTable;
    desc.params[kMaskParamTextures].shader_register = 0;
    desc.params[kMaskParamTextures].count = kMaskTableSize;
    desc.params[kMaskParamTextures].visibility = nrhi::Visibility::kPixel;
    desc.param_count = 2;
    desc.allow_input_layout = false;
    mask_layout_ = dev->CreateBindingLayout(desc);
    if (!mask_layout_) return fail("mask binding layout creation");
  }
  mask_ps_ = pixel_shader("fable2_clay_mask_ps", shaders::kClayMaskPs);
  if (!mask_ps_) return fail("mask shader compile");
  nrhi::GraphicsPipelineDesc mask_pipe;
  mask_pipe.layout = mask_layout_;
  mask_pipe.vs = map_vs_;
  mask_pipe.ps = mask_ps_;
  mask_pipe.rtv_format = nrhi::Format::kR8_UNORM;
  mask_pipeline_ = dev->CreateGraphicsPipeline(mask_pipe);
  if (!mask_pipeline_) return fail("mask pipeline creation");
  // The pre-pass's depth-only forms of the three pixel shaders that can discard.
  static constexpr const char* kDepthNames[kClayPixelShaders] = {nullptr, "fable2_clay_textured_depth_ps",
                                                                 "fable2_clay_terrain_layer_depth_ps",
                                                                 "fable2_clay_terrain_far_depth_ps"};
  static constexpr const char* kDepthSources[kClayPixelShaders] = {nullptr, shaders::kClayTexturedDepthPs,
                                                                   shaders::kClayTerrainLayerDepthPs,
                                                                   shaders::kClayTerrainFarDepthPs};
  for (uint8_t textured = 1; textured < kClayPixelShaders; ++textured) {
    depth_ps_[textured] = pixel_shader(kDepthNames[textured], kDepthSources[textured]);
    if (!depth_ps_[textured]) return fail("pre-pass pixel shader compile");
  }
  shadow_wanted_ = shadow_drawn_ = mask_drawn_ = false;
  shadow_objects_ = true;
  REXLOG_INFO("[native] clay: {}", FormatShadowTargetText());
  REXLOG_INFO("[native] clay: {}", FormatMaskTargetText(kWidth, kHeight));
  return true;
}

void ClayPass::Render(nrhi::Cmd* cmd, nrhi::Device* dev, const FrameScene& scene, ClayColor color,
                      bool textured, ClayLight light, bool shadows, ClayStats& st, TextureStats& tst) {
  if (!ready_ || dev != device_) return;
  // A latched texture cache (RHI failure) resolves nothing: skip the UV decode
  // too, and report the latch on F3.
  textured = textured && !textures_.latched();
  const auto t0 = std::chrono::steady_clock::now();
  const double hash0 = st.hash_ms, decode0 = st.decode_ms, tex0 = tst.decode_ms;
  // The frame's light: of this scene's own block or none (never another frame's). The light is
  // of the material, so without the texture path there is none (the Light line says so).
  st.light_textures_off = light == ClayLight::kOn && !textured;
  if (!textured) light = ClayLight::kOff;
  frame_ = MakeClayFrameConstants(scene.light, light);
  SetClayFrameMask(frame_, kWidth, kHeight);  // (where a lit pixel reads the screen mask)
  // With the light off no light input is asked of the geometry cache: every record is prepared as
  // one without light inputs (its light buffers null, its light_mode without an object bit, the
  // positions at t9 to t12 at its draw; a steep layer's terrain normals are the material's and
  // are built as before). A frame cannot be lit without them: `lit` needs the record's own
  // buffers (PrepareLight, FinishTerrain), which the first frame with the light on builds before
  // its draws.
  const bool light_inputs = ClayLightInputsBuilt(light);
  object_constants_.clear();
  plant_constants_.clear();
  object_slot0_ = plant_slot0_ = 0;

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
        PrepareTerrain(cmd, dev, r, *terrain, light_inputs ? LightOf(scene, r) : nullptr, textured, st, tst);
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
    p.positions = geometry_.Positions(dev, r, nullptr, &buffer_vertices, st, &p.bounds);
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
    // is of the material; a record that stays flat clay has none; none with the light off).
    if (p.albedo && light_inputs) {
      if (const capture::LightInputs* inputs = LightOf(scene, r)) PrepareLight(dev, r, *inputs, p, st);
    }
    prepared_.push_back(p);
  }
  // The frame's mask is decided first (DecideClayMask: one chain, the first thing missing decides),
  // and the two maps are prepared and drawn only for a frame whose mask will be drawn: nothing else
  // reads them. Everything but the frame's constant buffer is known here: the switches, the frame's
  // own shadow block (never another frame's maps or matrices), its light block and eye, its one
  // inverse view-projection, the mask's constants made of them (the tile's flag is set below), and,
  // asked last so that nothing is made for a frame that cannot use it, the shadows' objects. The
  // shadow records are prepared before FinishTerrain, which takes the frame's constant buffer for
  // their draws too; with that buffer known the verdict is final.
  const bool shadows_on = shadows && light == ClayLight::kOn;
  ClayMaskInputs mask_in;
  mask_in.shadows = shadows;
  mask_in.light = light == ClayLight::kOn;
  mask_in.objects = !shadow_objects_failed_;
  mask_in.block = scene.shadow.valid;
  mask_in.light_block = frame_.light_on != 0;
  mask_in.constants = true;  // (FinishTerrain, below)
  mask_in.eye = frame_.eye_on != 0;
  mask_in.finite = true;
  float mask_inverse[4][4] = {};
  float mask_view_proj[4][4] = {};
  FrameView mask_frame_view;
  ClayMaskConstants mask_constants{};
  if (DecideClayMask(mask_in).mask == ClayMask::kDrawn) {
    if (!FrameInverseViewProj(scene, mask_inverse, &mask_frame_view, mask_view_proj)) {
      mask_in.view = mask_frame_view.why;
    } else {
      mask_in.finite = MakeClayMaskConstants(scene.shadow, mask_inverse, frame_.eye, kWidth, kHeight, false, &mask_constants);
    }
  }
  if (DecideClayMask(mask_in).mask == ClayMask::kDrawn) mask_in.objects = EnsureShadowObjects(dev);
  PrepareShadows(cmd, dev, scene, ClayMapsDrawn(DecideClayMask(mask_in).mask), st, tst);
  // "Shadow: on": the switch, or a scene that holds shadow records all the same.
  st.shadow_switch = shadows || scene.shadow_records;
  FinishTerrain(dev, textured, st, tst);
  // (after FinishTerrain: without the frame's constant buffer the frame has no light)
  mask_in.light_block = frame_.light_on != 0;
  mask_in.constants = terrain_cb_ != nullptr;
  const ClayMaskDecision mask_decision = DecideClayMask(mask_in);
  st.shadow_light_off = scene.shadow.valid && (light != ClayLight::kOn || !frame_.light_on);
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

  // The frame's mask: drawn only for a lit frame of its own valid block which has one inverse
  // view-projection, an eye and the frame's constant buffer (mask_decision, above; the maps are
  // drawn for exactly these frames). Everything else is the white mask, by reason (ClayMask). What
  // a drawn mask needs is made here, before any target is bound: the rotation tile may be uploaded
  // and the pre-pass's pipelines made.
  mask_drawn_ = false;
  const ClayMask mask = mask_decision.mask;
  bool mask_rotated = false;
  if (mask == ClayMask::kDrawn) {
    st.mask_view_outliers = mask_frame_view.outliers;
    nrhi::TextureView* const tile = UpdateRotation(cmd, dev);
    mask_rotated = tile != nullptr;
    mask_constants.rotate = mask_rotated ? 1u : 0u;  // (MakeClayMaskConstants' flag: the tile is bound)
    std::memcpy(terrain_cb_mapped_ + ClaySmallSlotOffset(mask_slot_), &mask_constants, sizeof(mask_constants));
    // The pre-pass's pipelines, one a depth-writing record (a few keys for all of them). A
    // record wholly beyond the mask's reach gets none: it is left out of the pre-pass and
    // counted (PrepassBeyondReach: the mask is the same without it).
    for (Prepared& p : prepared_) {
      if (!p.pipeline) continue;
      const PipelineKey* key = KeyOf(p.pipeline);
      if (!key || !InPrepass(*key)) continue;
      float m[4][4];
      RecordMatrix(*p.r, m);
      if (PrepassBeyondReach(m, p.bounds, mask_view_proj, frame_.eye, scene.shadow.split, key->depth_func)) {
        p.prepass_culled = true;
        ++st.prepass_culled;
        continue;
      }
      p.prepass = PrepassPipeline(dev, p.pipeline, p.alpha, &p.prepass_flat);
    }
  }
  st.mask = mask;
  st.mask_view = mask_decision.view;
  st.mask_rotated = mask_rotated;
  // The pass's frame counts follow from the verdict (CountClayMaskFrame).
  CountClayMaskFrame(shadows_on, mask, mask_rotated, shadow_counts_);
  st.shadow_nolight_frames = shadow_counts_.nolight;
  st.shadow_objects_frames = shadow_counts_.objects;
  st.shadow_frames = shadow_counts_.frames;
  st.shadow_block_frames = shadow_counts_.block;
  st.shadow_view_frames = shadow_counts_.view;
  st.shadow_unrotated_frames = shadow_counts_.unrotated;

  // The shadow maps first (the game's order), each bound only for its own draws and back in
  // kPixelShaderResource before anything of the scene is bound. (Prepared only for a frame whose
  // mask was to be drawn; a frame that then got no constant buffer draws none: DrawShadows.)
  DrawShadows(cmd, st);
  cmd->Barrier(linear_, nrhi::ResourceState::kPixelShaderResource,
               nrhi::ResourceState::kRenderTarget);
  cmd->Barrier(depth_, nrhi::ResourceState::kPixelShaderResource,
               nrhi::ResourceState::kDepthWrite);
  cmd->FlushBarriers();
  // Without the frame's constant buffer neither b1 nor b2 can be bound, and then no record is
  // drawn: the frame is the cleared target, mapped below as always (the native view shows the
  // clear colour, not a stale frame).
  const uint32_t drawable = uint32_t(std::count_if(prepared_.begin(), prepared_.end(),
                                                    [](const Prepared& p) { return p.pipeline != nullptr; }));
  const uint32_t left_out = ClayRecordsWithoutConstants(drawable, terrain_cb_ != nullptr);
  if (left_out) {
    st.skipped_other += left_out;
    if (!no_constants_logged_) {
      no_constants_logged_ = true;
      REXLOG_ERROR("[native] clay: the frame's constant buffer could not be made: {} records not drawn "
                   "(the clay frame is empty; logged once)", left_out);
    }
  }
  // The scene's targets and the bindings every clay draw of the frame has. (SetBindingLayout
  // resets them, so this is done again after the mask step, which has a layout of its own.)
  auto begin_scene = [&] {
    cmd->SetRenderTargets(linear_, depth_);
    cmd->ClearDepth(depth_, 1.0f);
    cmd->SetViewport({0.0f, 0.0f, float(kWidth), float(kHeight), 0.0f, 1.0f});
    cmd->SetScissor({0, 0, int32_t(kWidth), int32_t(kHeight)});
    cmd->SetBindingLayout(layout_);
    cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);
    // The terrain table and constant buffer hold something from the first draw on
    // (only the terrain shaders read them, and each terrain draw binds its own).
    nrhi::TextureView* const whites[kTerrainTableSize] = {white_view_, white_view_, white_view_, white_view_};
    cmd->SetTextures(kParamTerrainTextures, whites, kTerrainTableSize);
    // The frame's light at b2 for the whole frame (slot 0), and b1 on something
    // from the first draw on: only a terrain draw and a lit object read b1, and
    // each binds its own slot.
    if (terrain_cb_) {
      cmd->SetConstantBuffer(kParamTerrainConstants, terrain_cb_, 0);
      cmd->SetConstantBuffer(kParamFrameConstants, terrain_cb_, 0);
    }
  };
  cmd->SetRenderTargets(linear_, depth_);
  cmd->ClearRenderTarget(linear_, kClayClearLinear);
  nrhi::TextureView* mask_bound = white_view_;
  // (a drawn mask has the frame's constant buffer, DecideClayMask: no record is left out in its frame)
  if (mask == ClayMask::kDrawn) {
    // 1. The depth pre-pass: every record that writes depth, depth only, in the scene's order.
    begin_scene();
    nrhi::Pipeline* bound = nullptr;
    // The light's four registers are bound once in the pre-pass (to the first draw's positions):
    // every pre-pass draw has light_mode 0, whose compiled vertex shader loads from none of them
    // (test_native_shaders.cpp holds that for every mode), so no draw can read another's buffer.
    bool light_bound = false;
    for (const Prepared& p : prepared_) {
      if (!p.pipeline) continue;
      if (!p.prepass) {
        // (a record that writes no depth has no pre-pass pipeline and is no skip)
        if (p.prepass_culled) continue;
        if (const PipelineKey* key = KeyOf(p.pipeline); key && InPrepass(*key)) ++st.prepass_skipped;
        continue;
      }
      if (p.prepass != bound) {
        cmd->SetPipeline(p.prepass);
        bound = p.prepass;
      }
      DrawPrepared(cmd, p, color, true, white_view_, nullptr, &light_bound);
      ++st.prepass_drawn;
    }
    // 2. The mask: one full-screen step from the pre-pass depth and the two maps. The depth is a
    // shader resource while it is read and the mask a target while it is written; neither is both.
    cmd->Barrier(depth_, nrhi::ResourceState::kDepthWrite, nrhi::ResourceState::kPixelShaderResource);
    cmd->Barrier(mask_, nrhi::ResourceState::kPixelShaderResource, nrhi::ResourceState::kRenderTarget);
    cmd->FlushBarriers();
    cmd->SetRenderTargets(mask_, nullptr);
    cmd->SetViewport({0.0f, 0.0f, float(kWidth), float(kHeight), 0.0f, 1.0f});
    cmd->SetScissor({0, 0, int32_t(kWidth), int32_t(kHeight)});
    cmd->SetBindingLayout(mask_layout_);
    cmd->SetPipeline(mask_pipeline_);
    cmd->SetConstantBuffer(kMaskParamConstants, terrain_cb_, ClaySmallSlotOffset(mask_slot_));
    {
      // (the white texture stands in for the tile of a frame without it: the shader's flag keeps it unread)
      nrhi::TextureView* const inputs[kMaskTableSize] = {depth_view_, shadow_view_[0], shadow_view_[1],
                                                         mask_rotated ? rotation_view_ : white_view_};
      cmd->SetTextures(kMaskParamTextures, inputs, kMaskTableSize);
    }
    cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);
    cmd->Draw(3, 0);
    cmd->Barrier(mask_, nrhi::ResourceState::kRenderTarget, nrhi::ResourceState::kPixelShaderResource);
    cmd->Barrier(depth_, nrhi::ResourceState::kPixelShaderResource, nrhi::ResourceState::kDepthWrite);
    cmd->FlushBarriers();
    mask_drawn_ = true;
    mask_bound = mask_view_;
    if (!mask_logged_) {
      // Once: the first frame with a mask (the Shadow line comes only every 300 frames, which a slow
      // adapter may not reach).
      mask_logged_ = true;
      REXLOG_INFO("[native] clay: first shadow mask drawn: pre-pass {} draws ({} without a pipeline), taps {}, scene frame {}",
                  st.prepass_drawn, st.prepass_skipped, mask_rotated ? "rotated" : "unrotated", scene.frame);
    }
  }
  // 3. The colour pass, as always: from a cleared depth, each record with its captured state. The
  // pre-pass's depth was the mask's alone.
  begin_scene();

  nrhi::Pipeline* bound = nullptr;
  for (const Prepared& p : prepared_) {
    if (left_out) break;        // (no constant buffer: nothing is drawn)
    if (!p.pipeline) continue;  // a terrain record that is not drawn (FinishTerrain)
    const capture::DrawRecord& r = *p.r;
    if (p.pipeline != bound) {
      cmd->SetPipeline(p.pipeline);
      bound = p.pipeline;
    }
    uint32_t light_mode = 0;
    DrawPrepared(cmd, p, color, false, mask_bound, &light_mode, nullptr);
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
  // The mapping's own word is 1 in every frame: the exposure of a lit frame is in the lit draws'
  // values (the frame's block), so the unlit draws and the clear are mapped as they are.
  const float map_constants[kMapConstantCount] = {kClayMapExposure, 0.0f, 0.0f, 0.0f};
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
    terrain_cb_mapped_ = nullptr;
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
