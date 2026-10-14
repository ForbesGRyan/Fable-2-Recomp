// Included inside capture.cpp's private namespace, after its guest readers and
// shader/constant tracking. No second interpretation of those readers here.
UiPublisher g_presentation_publisher;
UiFrame g_presentation;
void SetPresentationEffectGain(float gain) {g_presentation.effect_gain=gain;}
void SetPresentationDof(const DofBlock& block) {g_presentation.dof_requested=true;g_presentation.dof=block;}
struct WindowCapture {
  WindowCommands state;
  uint32_t device=0, entry[2]{};
  bool entry_valid[2]{};
  WindowCommands virtual_window;
  uint64_t virtual_frame=0;
};
thread_local WindowCapture t_window;
struct MaskCapture {
  uint32_t device=0,entry=0,end=0;
  uint64_t frame=0,select_frame=0;
  uint32_t select=0;
  bool entry_valid=false,clear_pending=false,clear_ready=false,copy_pending=false;
  float clear[4]{};
  int32_t clear_rect[4]{};
  uint32_t clear_blend=0;
  uint32_t quad_pointer=0,flush_pointer=0;  // diagnosis: device command pointer at the quad's validation and at its flush
  UiMask mask;
  uint32_t reset_device=0,reset_entry=0;
  bool reset_entry_valid=false;
};
thread_local MaskCapture t_mask;
bool ReadWindowCommands(uint32_t device,UiDraw& draw,bool stitched=false);
bool UiMaskPredicate(uint32_t device) {
  uint32_t lo=0,hi=0;
  return t_mask.select_frame==g_swaps.load(std::memory_order_relaxed)+1 && t_mask.select!=0 &&
    ReadVirtualBe32(device+xdk::kDeviceBinMaskLoOffset,&lo) && lo==UINT32_MAX &&
    ReadVirtualBe32(device+xdk::kDeviceBinMaskHiOffset,&hi) && hi==0;
}
// Live diagnosis of a refused mask clear: the reason, once per frame per thread.
void NoteMaskClearRefusal(uint32_t reason,const LastArgs& args) {
  static thread_local uint64_t frame=0;static thread_local uint32_t lines=0;
  const uint64_t now=g_swaps.load(std::memory_order_relaxed)+1;
  if(frame!=now) {frame=now;lines=0;}
  if(lines++<2) REXLOG_INFO("[native] presentation mask: clear refused at frame {} (reason {}, args {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X})",
    now,reason,args.r[1],args.r[2],args.r[3],args.r[4],args.r[5],args.r[6],args.r[7]);
}
void BeginUiMaskClear(const LastArgs& args) {
  t_mask.clear_pending=false;
  const bool mask_target=args.r[4]==360 && args.r[5]==0x0A000280 && args.r[6]==0x5A0;
  // The second, depth-only clear doesn't replace the colour clear.
  if(args.r[7]!=0) return;  // the routine's second call (r10 8777) is expected
  t_mask.clear_ready=false;
  if(args.r[1]!=1 || !mask_target || args.r[6]!=0x5A0) {if(mask_target) NoteMaskClearRefusal(9,args);return;}
  if(uint64_t(args.r[2])+16>uint64_t(UINT32_MAX)+1 || uint64_t(args.r[3])+16>uint64_t(UINT32_MAX)+1) {NoteMaskClearRefusal(1,args);return;}
  uint32_t mask=0,blend=0,control=0;
  if(!ReadVirtualBe32(args.r[0]+xdk::DeviceRegShadowOffset(xdk::kRegColorMask),&mask) || (mask&15)!=15 ||
     !ReadVirtualBe32(args.r[0]+xdk::DeviceRegShadowOffset(xdk::kRegBlendControl0),&blend) ||
     !ReadVirtualBe32(args.r[0]+xdk::DeviceRegShadowOffset(xdk::kRegColorControl),&control) || (control&24)) {
    NoteMaskClearRefusal(2,args);
    REXLOG_INFO("[native] presentation mask: clear state at frame {}: mask {:08X} blend {:08X} control {:08X}",g_swaps.load(std::memory_order_relaxed)+1,mask,blend,control);
    return;
  }
  UiDraw window;
  if(!ReadWindowCommands(args.r[0],window)) {NoteMaskClearRefusal(3,args);return;}
  for(uint32_t i=0;i<2;i++) if(!ReadVirtualBe32(args.r[0]+xdk::DeviceRegShadowOffset(0x200E+i),&window.screen_scissor[i])) {NoteMaskClearRefusal(4,args);return;}
  if(UiScissorOf(window,640,360)!=std::array<int32_t,4>{0,0,640,360}) {NoteMaskClearRefusal(5,args);return;}
  // One rectangle {x1,y1,x2,y2} inside the target: the Maps view clears it
  // whole, the pause menu a band (rows 352-360) under the copy quad.
  for(uint32_t i=0;i<4;i++) {
    uint32_t value=0;
    if(!ReadVirtualBe32(args.r[2]+4*i,&value) || value>(i&1 ? 360u:640u)) {NoteMaskClearRefusal(6,args);return;}
    t_mask.clear_rect[i]=int32_t(value);
  }
  if(t_mask.clear_rect[0]>=t_mask.clear_rect[2] || t_mask.clear_rect[1]>=t_mask.clear_rect[3]) {NoteMaskClearRefusal(6,args);return;}
  for(uint32_t i=0;i<4;i++) {
    uint32_t bits=0;if(!ReadVirtualBe32(args.r[3]+4*i,&bits)) {NoteMaskClearRefusal(7,args);return;}
    std::memcpy(t_mask.clear+i,&bits,4);
    if(!UiFinite(t_mask.clear[i],1) || t_mask.clear[i]<0) {NoteMaskClearRefusal(8,args);return;}
  }
  // The clear draws its rectangle under the current blend: One/Zero, or the
  // copy quad's SrcAlpha/InvSrcAlpha left set by the previous mask. The native
  // producer applies the same blend over the frame's shared scratch target.
  if(blend!=0x00010001 && blend!=0x07060706) {
    NoteMaskClearRefusal(2,args);
    REXLOG_INFO("[native] presentation mask: clear blend at frame {}: blend {:08X} alpha {}",g_swaps.load(std::memory_order_relaxed)+1,blend,t_mask.clear[3]);
    return;
  }
  t_mask.clear_blend=blend;
  t_mask.clear_pending=true;
}
void BeginUiMaskCommands(uint32_t device) {
  const uint64_t frame=g_swaps.load(std::memory_order_relaxed)+1;
  if(t_mask.device!=device || t_mask.frame!=frame) {
    // Keep only this frame's independently observed selection, never an image.
    const auto selection=t_mask.select;const auto selection_frame=t_mask.select_frame;
    t_mask={};t_mask.device=device;t_mask.frame=frame;
    t_mask.select=selection;t_mask.select_frame=selection_frame;
  }
  if(t_mask.copy_pending) {
    UiMask mask=std::move(t_mask.mask);t_mask.copy_pending=false;
    uint32_t current=0,first=0,count=0;
    UiMaskCopy copy;
    const bool pointer=ReadVirtualBe32(device+xdk::kDeviceCommandWriteOffset,&current);
    const auto span=pointer ? ClassifyStream(t_mask.end,current,2048,&first,&count):StreamSpan::kLong;
    bool valid=pointer && span==StreamSpan::kDwords;
    const uint8_t* bytes=valid ? ReadVirtual(first,count*4):nullptr;
    const bool parsed=valid && copy.Read(bytes,count);
    valid=parsed;
    // The emitted fetch is type 3, byte address bits 31:2, six dwords/endian 2.
    const uint8_t* rectangle=valid ? ReadPhysical(copy.vertex_fetch[0]&~3u,24):nullptr;
    float xy[6]{};
    if(rectangle) for(uint32_t i=0;i<6;i++) {uint32_t bits=LoadBe32(rectangle+4*i);std::memcpy(xy+i,&bits,4);}
    valid=valid && rectangle && UiMaskCopyRectangle(xy);
    std::lock_guard<std::mutex> guard(g_scene_mutex);
    if(g_presentation_frame.load(std::memory_order_relaxed) && frame==g_swaps.load(std::memory_order_relaxed)+1) {
      const uint32_t pending_before=g_presentation.mask_pending;
      if(g_presentation.mask_pending) --g_presentation.mask_pending;else valid=false;
      if(valid && g_presentation.masks.size()<kUiMaxMasks) {mask.base=copy.base;g_presentation.masks.push_back(std::move(mask));}
      else {
        g_presentation.Refuse();
        static thread_local uint64_t lines_frame=0;static thread_local uint32_t lines=0;
        if(lines_frame!=frame) {lines_frame=frame;lines=0;}
        if(lines++<4) REXLOG_WARN("[native] presentation mask: resolve ownership refused at frame {} (span {} from {:08X} to {:08X}, {} dwords at {:08X}, parsed {}, fetch {:08X}, rectangle {}, pending {}; quad entry {:08X}, device pointer at quad {:08X}, at flush {:08X}), rectangle [{},{},{},{},{},{}]",
          frame,int(span),t_mask.end,current,count,first,parsed,copy.vertex_fetch[0],rectangle!=nullptr,pending_before,t_mask.entry,t_mask.quad_pointer,t_mask.flush_pointer,xy[0],xy[1],xy[2],xy[3],xy[4],xy[5]);
        if(lines==1 && bytes && !parsed) {
          // Diagnosis: the refused span's packets, once per frame per thread.
          std::vector<uint32_t> v(count);for(uint32_t i=0;i<count;i++) v[i]=LoadBe32(bytes+4*i);
          REXLOG_INFO("[native] presentation mask: refused span at frame {}: {}",frame,HexDwords(v.data(),v.size()));
        }
      }
    }
  }
  t_mask.entry_valid=ReadVirtualBe32(device+xdk::kDeviceCommandWriteOffset,&t_mask.entry);
}
void EndUiMaskClear(uint32_t device) {
  if(!t_mask.clear_pending) return;
  t_mask.clear_pending=false;
  uint32_t end=0,first=0,count=0;
  bool valid=t_mask.entry_valid && t_mask.device==device &&
    ReadVirtualBe32(device+xdk::kDeviceCommandWriteOffset,&end) &&
    ClassifyStream(t_mask.entry,end,512,&first,&count)==StreamSpan::kDwords;
  const uint8_t* bytes=valid ? ReadVirtual(first,count*4):nullptr;
  const bool predicate=UiMaskPredicate(device);
  t_mask.clear_ready=valid && UiMaskClearCommands(bytes,count,predicate);
  if(!t_mask.clear_ready) {
    static thread_local uint64_t frame=0;static thread_local uint32_t lines=0;
    const uint64_t now=g_swaps.load(std::memory_order_relaxed)+1;
    if(frame!=now) {frame=now;lines=0;}
    if(lines++<2) {
      uint32_t v[32]{};const uint32_t n=bytes ? std::min<uint32_t>(count,32):0;
      for(uint32_t i=0;i<n;i++) v[i]=LoadBe32(bytes+4*i);
      const std::string words=HexDwords(bytes ? v:nullptr,n);
      REXLOG_INFO("[native] presentation mask: clear commands refused at frame {} (span {}, entry {:08X} end {:08X} count {}, predicate {}, select {:08X}/{}): {}",
        now,valid,t_mask.entry,end,count,predicate,t_mask.select,t_mask.select_frame,words);
    }
  }
}
void ObserveVirtualWindow(const LastArgs& args) {
  if(t_window.device!=args.r[0]) {t_window={};t_window.device=args.r[0];}
  t_window.virtual_window.Invalidate();
  const uint8_t* flag=ReadTilingFlag(args.r[0]);
  if(!flag || !TilingActive(*flag)) return;
  // 821F97E8 receives the full rectangle in r4..r7. Its tiled branch intersects
  // this with each tile and subtracts the tile origin (821F9940..821F9A18).
  // Native world rendering owns one full target, so use that pre-tile rectangle
  // with zero tile origin. Do not reuse a window from an earlier frame.
  for(uint32_t i=1;i<=4;i++) if(args.r[i]>16383) return;
  t_window.virtual_window.words[0]=0;
  t_window.virtual_window.words[1]=(args.r[2]<<16)|args.r[1];
  t_window.virtual_window.words[2]=(args.r[4]<<16)|args.r[3];
  t_window.virtual_window.known=7;
  t_window.virtual_frame=g_swaps.load(std::memory_order_relaxed)+1;
}
void ObserveWindowBuffers(const LastArgs& args) {
  // A submitted indirect body can replace bin control. A later direct untiled
  // reset must establish it again before a predicated mask clear/draw.
  t_mask.select_frame=0;
  const uint32_t device=args.r[0],list=args.r[1],buffers=args.r[2];
  if(t_window.device!=device) {t_window={};t_window.device=device;}
  // 82286318 reads (dword count, GPU address) records from r4, then emits
  // INDIRECT_BUFFER packets. Own their submitted contents, not an inferred
  // register reset at every command-buffer flush.
  if(buffers>64 || uint64_t(list)+uint64_t(buffers)*8>uint64_t(UINT32_MAX)+1) {t_window.state.Invalidate();return;}
  uint32_t total=0;
  for(uint32_t i=0;i<buffers;i++) {
    uint32_t count=0,address=0;
    if(!ReadVirtualBe32(list+i*8,&count) || !ReadVirtualBe32(list+i*8+4,&address)) {t_window.state.Invalidate();return;}
    count&=0xFFFFFF;
    if(count>65536-total) {t_window.state.Invalidate();return;}
    total+=count;
    const uint8_t* bytes=ReadPhysical(xdk::GpuAddress(address),count*4);
    if(!bytes || !t_window.state.Apply(bytes,count)) {t_window.state.Invalidate();return;}
  }
}
void BeginWindowCommands(uint32_t device,uint32_t slot) {
  if(t_window.device!=device) { t_window={};t_window.device=device; }
  t_window.entry_valid[slot]=ReadVirtualBe32(device+xdk::kDeviceCommandWriteOffset,&t_window.entry[slot]);
}
void EndWindowCommands(uint32_t device,uint32_t slot) {
  uint32_t end=0,first=0,count=0;
  if(!t_window.entry_valid[slot] || t_window.device!=device ||
     !ReadVirtualBe32(device+xdk::kDeviceCommandWriteOffset,&end)) {t_window.state.Invalidate();return;}
  t_window.entry_valid[slot]=false;
  const StreamSpan span=ClassifyStream(t_window.entry[slot],end,8192,&first,&count);
  if(span==StreamSpan::kEmpty) return;
  if(span!=StreamSpan::kDwords) {t_window.state.Invalidate();return;}
  const uint8_t* bytes=ReadVirtual(first,count*4);
  if(!bytes) {t_window.state.Invalidate();return;}
  t_window.state.Apply(bytes,count);
}
bool ReadWindowCommands(uint32_t device,UiDraw& draw,bool stitched) {
  const uint8_t* flag=stitched ? ReadTilingFlag(device):nullptr;
  const WindowCommands& state=flag && TilingActive(*flag) && t_window.virtual_frame==g_swaps.load(std::memory_order_relaxed)+1
    ? t_window.virtual_window:t_window.state;
  if(t_window.device!=device || !state.valid()) return false;
  draw.window_offset=state.words[0];
  draw.window_scissor[0]=state.words[1];
  draw.window_scissor[1]=state.words[2];
  return true;
}
struct PendingUi {
  bool valid = false;
  bool effect_depth = false;
  bool mask=false;
  uint32_t mask_device=0,mask_end=0;
  float mask_clear[4]{};
  int32_t mask_clear_rect[4]{};
  uint32_t mask_clear_blend=0;
  uint64_t frame = 0;
  uint32_t vertex_storage = 0, index_storage = 0, vertices = 0, indices = 0, stride = 0;
  UiDraw draw;
  float matrix[4][4]{};
  float offset[4]{};
};
bool ReadPresentationClip(uint32_t device,UiDraw& draw) {
  return ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x2204),&draw.clip_control) &&
         UiClipSupported(draw.clip_control);
}
thread_local PendingUi t_pending_ui;
inline constexpr uint8_t kPendingUi = 4;

bool ReadUiLiterals(uint32_t obj, float out[3][4]) {
  uint32_t off = 0, base = 0, size = 0;
  const uint64_t header = uint64_t(obj) + kPsLayout.header_offset;
  if (!obj || header + xdk::kShaderLiteralTableField + 4 > uint64_t(UINT32_MAX)+1 ||
      uint64_t(obj) + 4*kPsLayout.base_dword + 4 > uint64_t(UINT32_MAX)+1 ||
      !ReadVirtualBe32(uint32_t(header) + xdk::kShaderLiteralTableField, &off) ||
      !ReadVirtualBe32(obj + 4 * kPsLayout.base_dword, &base) || !off) return false;
  const uint64_t table = header + off;
  if (table + xdk::kLiteralTableEntriesOffset > UINT32_MAX ||
      !ReadVirtualBe32(uint32_t(table) + xdk::kLiteralTableSizeOffset, &size) ||
      !size || size > 8 * kMaxLiteralGuardRuns) return false;
  const uint8_t* entries = ReadVirtual(uint32_t(table) + xdk::kLiteralTableEntriesOffset, size);
  if (!entries) return false;
  xdk::LiteralRun runs[kMaxLiteralGuardRuns];
  const uint32_t count = xdk::ParseLiteralRuns(entries, size, runs, kMaxLiteralGuardRuns);
  uint32_t have = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const auto& run = runs[i];
    // Pixel literal tables number cN as 256 + N (the video table starts at 508).
    for (uint32_t reg = 509; reg <= 511; ++reg) {
      if (reg < run.first_register || uint64_t(reg - run.first_register) * 4 + 4 > run.dwords) continue;
      const uint64_t at = uint64_t(base) + run.offset + uint64_t(reg - run.first_register) * 16;
      if (at + 16 > uint64_t(UINT32_MAX)+1) return false;
      const uint8_t* p = ReadPhysical(xdk::GpuAddress(uint32_t(at)), 16);
      if (!p) return false;
      for (uint32_t k = 0; k < 4; ++k) {
        const uint32_t bits = LoadBe32(p + 4*k);
        std::memcpy(&out[reg-509][k], &bits, 4);
      }
      have |= 1u << (reg-509);
    }
  }
  return have == 7;
}

bool ReadUiVertexLiterals(const VsChoice& choice,UiKind kind) {
  uint64_t regs[4]{};
  if(!ReadLiteralRegisters(kVsLayout,choice.obj,choice.variant,0,regs) ||
     (regs[0]&(kind==UiKind::kSprite ? 15ull : 1ull<<8))) return false;
  if(kind!=UiKind::kText) return true;
  // Text alone reads VS c255.x. The formula's two is the object's literal,
  // rather than an assumed value from a later draw's device bank.
  if(!(regs[3]&(1ull<<63))) return false;
  uint32_t off=0,base=0,size=0;
  const uint64_t header=uint64_t(choice.obj)+kVsLayout.header_offset;
  if(header+xdk::kShaderLiteralTableField+8*choice.variant+4>uint64_t(UINT32_MAX)+1 ||
     !ReadVirtualBe32(uint32_t(header)+xdk::kShaderLiteralTableField+8*choice.variant,&off) || !off ||
     !ReadVirtualBe32(choice.obj+4*kVsLayout.base_dword,&base)) return false;
  const uint64_t table=header+off;
  if(table+xdk::kLiteralTableEntriesOffset>UINT32_MAX ||
     !ReadVirtualBe32(uint32_t(table)+xdk::kLiteralTableSizeOffset,&size) || !size || size>8*kMaxLiteralGuardRuns) return false;
  const uint8_t* entries=ReadVirtual(uint32_t(table)+xdk::kLiteralTableEntriesOffset,size);
  if(!entries) return false;
  xdk::LiteralRun runs[kMaxLiteralGuardRuns];
  const uint32_t count=xdk::ParseLiteralRuns(entries,size,runs,kMaxLiteralGuardRuns);
  // The table size also covers a zero-count terminator and following lists.
  // ParseLiteralRuns follows the first list, exactly as the XDK uploader does.
  if(!count) return false;
  bool found=false;
  for(uint32_t i=0;i<count;i++) {
    const auto& run=runs[i];
    if(run.first_register>255 || 4ull*(255-run.first_register)>=run.dwords) continue;
    const uint64_t at=uint64_t(base)+run.offset+16ull*(255-run.first_register);
    if(at+4>uint64_t(UINT32_MAX)+1) return false;
    const uint8_t* p=ReadPhysical(xdk::GpuAddress(uint32_t(at)),4);
    if(!p || LoadBe32(p)!=0x40000000) return false;
    found=true;
  }
  return found;
}

void FlushPendingUi() {
  PendingUi pending = std::move(t_pending_ui);
  t_pending_ui = {};
  t_pending_draws &= uint8_t(~kPendingUi);
  if (!pending.valid) return;
  bool valid = true;
  const uint8_t* vertices = ReadVirtual(pending.vertex_storage, pending.vertices * pending.stride);
  const uint8_t* indices = ReadVirtual(pending.index_storage, pending.indices * 2);
  if (!vertices || !indices) valid = false;
  auto f32 = [](const uint8_t* p) {
    const uint32_t bits = LoadBe32(p);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
  };
  if (valid) pending.draw.vertices.reserve(pending.indices);
  for (uint32_t i = 0; valid && i < pending.indices; ++i) {
    const uint32_t index = uint32_t(indices[2*i]) << 8 | indices[2*i+1];
    constexpr uint32_t mask_indices[]={1,0,2,2,3,1};
    if(pending.mask && index!=mask_indices[i]) {valid=false;break;}
    if (index >= pending.vertices) { valid = false; break; }
    const uint8_t* p = vertices + index * pending.stride;
    UiVertex v;
    const float position[4] = {f32(p), f32(p+4), f32(p+8), 1.0f};
    for (float value : position) valid = valid && UiFinite(value);
    if(pending.mask) {
      const float uv[]={f32(p+12),f32(p+16)};
      valid=valid && UiMaskVertexOf(position,uv,pending.matrix,&v);
    } else if (pending.draw.kind == UiKind::kSprite) {
      for (uint32_t row = 0; row < 4; ++row) {
        for (uint32_t k = 0; k < 4; ++k) v.clip[row] += pending.matrix[row][k] * position[k];
        v.uv[row] = f32(p + 48 + 4*row);
      }
      const uint32_t colour = LoadBe32(p+68);
      v.colour[0] = float((colour >> 16) & 255)/255;
      v.colour[1] = float((colour >> 8) & 255)/255;
      v.colour[2] = float(colour & 255)/255;
      v.colour[3] = float(colour >> 24)/255;
      v.extra[3] = f32(p+64);
    } else {
      for (uint32_t k = 0; k < 4; ++k) v.clip[k] = position[k];
      if (pending.draw.kind == UiKind::kVideo || pending.draw.kind==UiKind::kFade) {
        v.clip[0] = position[0]*pending.offset[0] + pending.offset[2];
        v.clip[1] = position[1]*pending.offset[1] + pending.offset[3];
      } else {
        v.clip[0] += pending.offset[0];
        v.clip[1] += pending.offset[1];
      }
      v.uv[0] = f32(p+12); v.uv[1] = f32(p+16);
    }
    for (float value : v.clip) valid = valid && UiFinite(value);
    for (float value : v.uv) valid = valid && UiFinite(value, 64);
    valid = valid && UiFinite(v.extra[3], 32);
    if (valid) pending.draw.vertices.push_back(v);
  }
  if (valid && pending.draw.kind == UiKind::kVideo) {
    for (uint32_t i = 0; valid && i < 3; ++i) {
      render::TextureFetch texture;
      const uint32_t* fetch = pending.draw.fetch[i];
      valid = render::WaterTextureFetch(fetch, render::WaterDecode::kK8, &texture) == render::FetchError::kNone;
      if (!valid) break;
      // K8 is deliberately outside the ordinary albedo format set. Its movie
      // planes still have the same base-level row/tile layout as the SDK.
      const uint64_t extent = texture.tiled
        ? render::TiledUpperBound2D(texture.width,texture.height,texture.pitch_texels,0)
        : uint64_t((texture.pitch_texels+31)&~31u)*(texture.height-1)+texture.width;
      if (texture.endian != 0 || ((fetch[3] >> 10)&7) != 0 ||
          ((fetch[0] >> 2)&3) != 0) { valid = false; break; }
      if (!extent || extent > kUiMaxPlaneBytes) { valid = false; break; }
      const uint8_t* bytes = ReadPhysical(texture.base_phys, uint32_t(extent));
      UiPlane& plane = pending.draw.planes[i];
      plane.width = texture.width; plane.height = texture.height;
      valid = bytes && render::DecodeK8Level(bytes, uint32_t(extent), texture.width, texture.height,
                                           texture.pitch_texels, texture.tiled, &plane.pixels);
    }
  }
  std::lock_guard<std::mutex> guard(g_scene_mutex);
  if (!g_presentation_frame.load(std::memory_order_relaxed) || pending.frame != g_swaps.load(std::memory_order_relaxed) + 1) return;
  if(pending.mask) {
    t_mask.copy_pending=false;
    if(valid) {
      auto source=std::make_shared<render::TextureSnapshot>();
      valid=render::TextureCache::CaptureSnapshot(pending.draw.fetch[0],8ull<<20,source.get());
      if(valid) pending.draw.textures[0]=g_presentation.ReuseTexture(pending.draw.fetch[0],*source);
      if(valid && !pending.draw.textures[0]) {
        valid=g_presentation.HoldTexture(pending.draw.fetch[0],source);
        if(valid) pending.draw.textures[0]=std::move(source);
      }
    }
    if(valid) {
      t_mask.mask={};t_mask.mask.draw=std::move(pending.draw);
      std::copy(pending.mask_clear,pending.mask_clear+4,t_mask.mask.clear);
      std::copy(pending.mask_clear_rect,pending.mask_clear_rect+4,t_mask.mask.clear_rect);
      t_mask.mask.clear_blend=pending.mask_clear_blend;
      t_mask.end=pending.mask_end;t_mask.copy_pending=true;
      ReadVirtualBe32(pending.mask_device+xdk::kDeviceCommandWriteOffset,&t_mask.flush_pointer);
    }
    else {if(g_presentation.mask_pending) --g_presentation.mask_pending;g_presentation.Refuse();}
    return;
  }
  if(pending.effect_depth) {
    if(valid && UiEffectDepthQuad(pending.draw.vertices))
      g_presentation.effect_depth=std::make_shared<const UiDraw>(std::move(pending.draw));
    else g_presentation.effect_depth.reset();
    return;
  }
  if(!g_presentation.pending) {g_presentation.Refuse();return;}
  --g_presentation.pending;
  if(valid && pending.draw.kind!=UiKind::kVideo) {
    for(uint32_t i=0;valid && i<3;i++) {
      bool needed=i==0;
      if(pending.draw.kind==UiKind::kSprite) needed|=(i==1 && pending.draw.registers[2][0]==1) || (i==2 && pending.draw.registers[5][0]==1);
      else if(pending.draw.kind==UiKind::kText) needed|=i==1 && pending.draw.registers[5][0]==1;
      if(!needed) continue;
      const bool mask=(pending.draw.kind==UiKind::kSprite && i==2 && pending.draw.registers[5][0]==1) ||
        (pending.draw.kind==UiKind::kText && i==1 && pending.draw.registers[5][0]==1);
      if(mask) {
        valid=UiMaskFetch(pending.draw.fetch[i]);
        if(valid) {
          const uint32_t base=pending.draw.fetch[i][1]&~4095u;
          for(size_t m=g_presentation.masks.size();m>0;m--) if(g_presentation.masks[m-1].base==base) {pending.draw.masks[i]=uint32_t(m-1);break;}
          valid=pending.draw.masks[i]!=UINT32_MAX;
        }
        if(!valid) {static thread_local uint32_t lines=0;if(lines++<12) REXLOG_WARN("[native] presentation mask: consumer lacks its own-frame producer at frame {}",pending.frame);}
        continue;
      }
      auto source=std::make_shared<render::TextureSnapshot>();
      valid=render::TextureCache::CaptureSnapshot(pending.draw.fetch[i],8ull<<20,source.get());
      if(valid) pending.draw.textures[i]=g_presentation.ReuseTexture(pending.draw.fetch[i],*source);
      if(pending.draw.textures[i]) continue;
      if(valid) valid=g_presentation.HoldTexture(pending.draw.fetch[i],source);
      if(valid) pending.draw.textures[i]=std::move(source);
    }
  }
  if (valid) g_presentation.Add(std::move(pending.draw));
  else {
    g_presentation.Refuse();
    if(pending.frame%120==0) REXLOG_WARN("[native] presentation capture: frame {} rejected filled geometry/video planes (kind {})",pending.frame,uint32_t(pending.draw.kind));
  }
}

void ObserveUiDraw(uint32_t id, const LastArgs& args, uint32_t device) {
  if (id != kHook_DrawIndx_82217EE8 && id != kHook_D3DDevice_BeginVertices) return;
  DeviceSnapshot snapshot;
  uint32_t ps_obj = 0;
  VsChoice choice;
  if (!ReadDevice(device, &snapshot) || !ChoosePs(snapshot, &ps_obj)) return;
  const PsInfo* ps = LookupPs(ps_obj);
  if (!ps) return;
  const uint64_t ps_hash = ps->hash;
  const uint64_t frame = g_swaps.load(std::memory_order_relaxed) + 1;
  const auto current = [&] {
    return g_presentation_frame.load(std::memory_order_relaxed) &&
           frame == g_swaps.load(std::memory_order_relaxed) + 1;
  };
  if(UiUnsupportedWorldPost(ps_hash)) {
    std::lock_guard<std::mutex> guard(g_scene_mutex);
    if(current()) {
      g_presentation.unsupported_world_post=ps_hash;
      static thread_local uint32_t lines=0;
      if(lines++<8) REXLOG_INFO("[native] presentation capture: current distortion composite {:016X} requires its own producer",ps_hash);
    }
    return;
  }
  if(ps_hash==kUiMaskPs) {
    PendingUi pending;pending.mask=true;pending.frame=frame;pending.mask_device=device;
    pending.vertices=args.r[3];pending.indices=args.r[4];pending.stride=args.r[6];
    const VsInfo* vs=ChooseVs(snapshot,&choice) ? LookupVs(choice):nullptr;
    uint32_t vdwords=0,idwords=0,returned=0;
    // The first failed step is named in the refusal line (live diagnosis).
    uint32_t step=0,failed=0;bool valid=true;
    const auto check=[&](bool ok) {++step;if(valid && !ok) {valid=false;failed=step;return false;}return valid;};
    check(id==kHook_DrawIndx_82217EE8 && vs && vs->hash==kUiMaskVs);                       // 1
    check((args.r[1]&63)==4 && args.r[2]==0 && (args.r[5]&4)==0);                           // 2
    check(pending.vertices==4 && pending.indices==6 && pending.stride==20);                 // 3
    check(t_mask.device==device && t_mask.frame==frame);                                    // 4
    check(t_mask.clear_ready);                                                              // 5
    check(ReadVirtualBe32(device+xdk::kDeviceBeginVerticesStorageOffset,&pending.vertex_storage) &&
      ReadVirtualBe32(device+xdk::kDeviceIndexedIndexStorageOffset,&pending.index_storage) &&
      ReadVirtualBe32(device+xdk::kDeviceBeginVerticesDwordsOffset,&vdwords) &&
      ReadVirtualBe32(device+xdk::kDeviceIndexedIndexDwordsOffset,&idwords));              // 6
    check(ReadVirtualBe32(args.r[7],&returned) && returned==pending.index_storage);         // 7
    check(vdwords==IndxVertexDwords(4,20) && idwords==IndxIndexDwords(6,false));            // 8
    std::copy(t_mask.clear,t_mask.clear+4,pending.mask_clear);
    std::copy(t_mask.clear_rect,t_mask.clear_rect+4,pending.mask_clear_rect);
    pending.mask_clear_blend=t_mask.clear_blend;t_mask.clear_ready=false;
    ReadStateWords(device,&pending.draw.state);
    check(ReadPresentationClip(device,pending.draw));                                       // 9
    check(ReadVirtualBe32(device+xdk::kDeviceSurfaceInfoOffset,&pending.draw.surface_info) &&
      ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(xdk::kRegColorInfo),&pending.draw.colour_info) &&
      ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x2302),&pending.draw.vertex_control)); // 10
    check(ReadConstantRegisters(VertexBank(device),0,4,&pending.matrix[0][0]));             // 11
    check(ReadTextureFetch(device,13,pending.draw.fetch[0]));                               // 12
    uint64_t literal_regs[4]{};
    check(ReadLiteralRegisters(kVsLayout,choice.obj,choice.variant,0,literal_regs) && !(literal_regs[0]&15)); // 13
    check(ReadLiteralRegisters(kPsLayout,ps_obj,0,256,literal_regs) &&
      literal_regs[0]==0 && literal_regs[1]==0 && literal_regs[2]==0 && literal_regs[3]==0); // 14
    const auto* fetch=pending.draw.fetch[0];
    check(fetch[0]==0x84004802 && (fetch[1]&4095)==0x54 && fetch[1]>>12 &&
      fetch[2]==0x003FE1FF && fetch[3]==0x00A80D10 && fetch[4]==3 && fetch[5]==0x218);       // 15
    bool regs=true;
    for(uint32_t i=0;regs && i<4;i++) {
      uint32_t bits=0;regs=ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x210F+i),&bits);
      std::memcpy(&pending.draw.viewport[i],&bits,4);
    }
    for(uint32_t i=0;regs && i<2;i++) regs=ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x200E+i),&pending.draw.screen_scissor[i]);
    check(regs);                                                                            // 16
    check(ReadWindowCommands(device,pending.draw));                                         // 17
    check(UiMaskState(pending.draw));                                                       // 18
    bool finite=true;
    for(const auto& row:pending.matrix) for(float f:row) finite=finite && UiFinite(f);
    check(finite);                                                                          // 19
    uint32_t first=0,count=0;
    ReadVirtualBe32(device+xdk::kDeviceCommandWriteOffset,&t_mask.quad_pointer);
    check(t_mask.entry_valid && ReadVirtualBe32(device+xdk::kDeviceBeginVerticesWriteOffset,&pending.mask_end) &&
      ClassifyStream(t_mask.entry,pending.mask_end,8192,&first,&count)==StreamSpan::kDwords); // 20
    check(UiMaskDrawCommands(valid ? ReadVirtual(first,count*4):nullptr,count,UiMaskPredicate(device))); // 21
    if(valid) {
      std::lock_guard<std::mutex> guard(g_scene_mutex);
      if(!current() || !g_presentation.complete) return;
      if(g_presentation.masks.size()+g_presentation.mask_pending>=kUiMaxMasks) {g_presentation.Refuse();return;}
      ++g_presentation.mask_pending;
      pending.valid=true;t_pending_ui=std::move(pending);t_pending_draws|=kPendingUi;
    }
    else {
      // Every state/viewport/surface read above ran regardless of earlier steps.
      // A draw of this shader pair with another shape is not a UI mask producer:
      // count it and leave the frame; its consumers (if any) fail closed on base.
      const bool shaped=UiMaskState(pending.draw);
      std::lock_guard<std::mutex> guard(g_scene_mutex);
      if(current()) {if(shaped) g_presentation.Refuse();else ++g_presentation.foreign_masks;}
      static thread_local uint64_t lines_frame=0;static thread_local uint32_t lines=0;
      if(lines_frame!=frame) {lines_frame=frame;lines=0;}
      if(!shaped) {if(lines++<2) REXLOG_INFO("[native] presentation mask: foreign {}x{} draw of the mask shaders at frame {} ignored (surface {:08X}, blend {:08X})",
        2*int(pending.draw.viewport[0]),2*int(pending.draw.viewport[3]),frame,pending.draw.surface_info,pending.draw.state.blend_control);}
      else if(lines++<8) REXLOG_WARN("[native] presentation mask: quad/clear ownership refused at frame {} (step {}, select {:08X}, select frame {}, state {:08X}/{:08X}/{:08X}/{:08X}/{:08X}/{:08X}, surface {:08X} colour {:08X} clip {:08X} vp [{},{},{},{}])",
        frame,failed,t_mask.select,t_mask.select_frame,pending.draw.state.depth_control,pending.draw.state.blend_control,
        pending.draw.state.color_control,pending.draw.state.color_mask,pending.draw.state.su_sc_mode,pending.draw.state.vte_cntl,
        pending.draw.surface_info,pending.draw.colour_info,pending.draw.clip_control,pending.draw.viewport[0],pending.draw.viewport[1],pending.draw.viewport[2],pending.draw.viewport[3]);
    }
    return;
  }
  if(ps_hash==0x5D6768847B0602B8ull) {
    uint32_t surface=0,fetch[6]{},mask=0;
    PendingUi pending;pending.effect_depth=true;pending.frame=frame;
    pending.vertices=args.r[3];pending.indices=args.r[4];pending.stride=args.r[6];
    const VsInfo* vs=ChooseVs(snapshot,&choice) ? LookupVs(choice):nullptr;
    bool valid=id==kHook_DrawIndx_82217EE8 && vs && vs->hash==0xD5E12A97B3A84881ull &&
      (args.r[1]&63)==4 && args.r[2]==0 && (args.r[5]&4)==0 &&
      pending.vertices==4 && pending.indices==6 && pending.stride==20 &&
      ReadVirtualBe32(device+xdk::kDeviceSurfaceInfoOffset,&surface) && (surface&16383)==560 &&
      ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(xdk::kRegColorMask),&mask) && mask==0 &&
      ReadTextureFetch(device,13,fetch) && (fetch[0]&3)==2 && (fetch[1]&63)==23 &&
      ((fetch[5]>>9)&3)==1 && (fetch[2]&8191)+1==1120 && ((fetch[2]>>13)&8191)+1==720 &&
      ((fetch[3]>>19)&15)==0;
    uint32_t vdwords=0,idwords=0,returned=0;
    valid=valid && ReadVirtualBe32(device+xdk::kDeviceBeginVerticesStorageOffset,&pending.vertex_storage) &&
      ReadVirtualBe32(device+xdk::kDeviceIndexedIndexStorageOffset,&pending.index_storage) &&
      ReadVirtualBe32(device+xdk::kDeviceBeginVerticesDwordsOffset,&vdwords) &&
      ReadVirtualBe32(device+xdk::kDeviceIndexedIndexDwordsOffset,&idwords) &&
      ReadVirtualBe32(args.r[7],&returned) && returned==pending.index_storage &&
      vdwords==IndxVertexDwords(4,20) && idwords==IndxIndexDwords(6,false);
    ReadStateWords(device,&pending.draw.state);
    valid=valid && pending.draw.state.read && ReadPresentationClip(device,pending.draw) &&
      pending.draw.state.vte_cntl==0x43F &&
      ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x2302),&pending.draw.vertex_control);
    for(uint32_t i=0;valid && i<4;i++) {
      uint32_t bits=0;valid=ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x210F+i),&bits);
      std::memcpy(&pending.draw.viewport[i],&bits,4);
    }
    valid=valid && pending.draw.viewport[0]==280 && pending.draw.viewport[1]==280 &&
      pending.draw.viewport[2]==-180 && pending.draw.viewport[3]==180 &&
      ReadWindowCommands(device,pending.draw) && pending.draw.window_offset==0;
    for(uint32_t i=0;valid && i<2;i++)
      valid=ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x200E+i),&pending.draw.screen_scissor[i]);
    valid=valid && UiScissorOf(pending.draw,560,360)==std::array<int32_t,4>{0,0,560,360};
    std::lock_guard<std::mutex> guard(g_scene_mutex);
    if(!current()) return;
    g_presentation.effect_depth.reset();
    if(valid) {pending.valid=true;t_pending_ui=std::move(pending);t_pending_draws|=kPendingUi;}
    return;
  }
  if (ps_hash == 0x93E7F7B5D05626AAull) {
    float power[4]{};uint32_t fetch[6]{};
    const bool read = ReadPixelConstant(31, power) && power[0] > 0 && power[0] <= 8 &&
      ReadTextureFetch(device,0,fetch) && (fetch[0]&3)==2 && (fetch[1]&63)==32 &&
      ((fetch[5]>>9)&3)==1 && (fetch[2]&8191)+1==1280 && ((fetch[2]>>13)&8191)+1==720;
    std::lock_guard<std::mutex> guard(g_scene_mutex);
    if (!current()) return;
    if (read) { g_presentation.menu = true; g_presentation.menu_power = power[0];g_presentation.menu_float10=true; }
    else g_presentation.Refuse();
    return;
  }
  if (ps_hash == 0x686DE15FC84D8B73ull) {
    uint32_t blend = 0, bits = 0, colour = 0, history_fetch[6]{};
    float share = 0;
    const bool read = ReadVirtualBe32(device + xdk::DeviceRegShadowOffset(xdk::kRegBlendControl0), &blend) &&
                      ReadVirtualBe32(device + xdk::DeviceRegShadowOffset(xdk::kRegBlendRed + 3), &bits) &&
                      ReadVirtualBe32(device + xdk::DeviceRegShadowOffset(xdk::kRegColorInfo), &colour) &&
                      ReadTextureFetch(device,13,history_fetch) &&
                      ((colour>>16)&15)==2 && (history_fetch[0]&3)==2 &&
                      (history_fetch[1]&63)==6 && ((history_fetch[5]>>9)&3)==1 &&
                      (history_fetch[2]&8191)+1==1280 && ((history_fetch[2]>>13)&8191)+1==720;
    std::memcpy(&share, &bits, 4);
    if (blend == 0x0F0E0F0Eu) {
      std::lock_guard<std::mutex> guard(g_scene_mutex);
      if (!current()) return;
      if (read && share >= 0 && share <= 1) g_presentation.previous_share = share;
      else g_presentation.Refuse();
    }
    return;
  }
  if (ps_hash != kUiSpritePs && ps_hash != kUiTextPs && ps_hash != kUiVideoPs && ps_hash!=kUiFadePs) return;
  const VsInfo* vs = ChooseVs(snapshot, &choice) ? LookupVs(choice) : nullptr;
  const UiKind kind = UiShaderKind(vs ? vs->hash : 0, ps_hash);
  PendingUi pending;
  pending.frame = frame;
  pending.draw.kind = kind;
  pending.vertices = args.r[3]; pending.indices = args.r[4]; pending.stride = args.r[6];
  bool valid = kind != UiKind(0) && id == kHook_DrawIndx_82217EE8 &&
               (args.r[1] & 63) == 4 && args.r[2] == 0 && (args.r[5] & 4) == 0 &&
               pending.vertices > 0 && pending.vertices <= kUiMaxDrawVertices &&
               pending.indices > 0 && pending.indices <= kUiMaxDrawVertices && pending.indices % 3 == 0 &&
               pending.stride == (kind == UiKind::kSprite ? 72u : 20u);
  uint32_t vdwords = 0, idwords = 0, returned = 0;
  valid = valid && ReadVirtualBe32(device+xdk::kDeviceBeginVerticesStorageOffset,&pending.vertex_storage) &&
          ReadVirtualBe32(device+xdk::kDeviceIndexedIndexStorageOffset,&pending.index_storage) &&
          ReadVirtualBe32(device+xdk::kDeviceBeginVerticesDwordsOffset,&vdwords) &&
          ReadVirtualBe32(device+xdk::kDeviceIndexedIndexDwordsOffset,&idwords) &&
          ReadVirtualBe32(args.r[7],&returned) && returned == pending.index_storage &&
          vdwords == IndxVertexDwords(pending.vertices,pending.stride) && idwords == IndxIndexDwords(pending.indices,false);
  ReadStateWords(device, &pending.draw.state);
  valid = valid && pending.draw.state.read && ReadPresentationClip(device,pending.draw);
  valid=valid && (pending.draw.state.vte_cntl&0x700)==0x400;
  valid = valid && ReadVirtualBe32(device+xdk::kDeviceSurfaceInfoOffset,&pending.draw.surface_info) &&
          ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x2302),&pending.draw.vertex_control) &&
          ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(xdk::kRegColorInfo),&pending.draw.colour_info);
  uint32_t failed_at = valid ? 0 : 1;
  valid=valid && ReadUiVertexLiterals(choice,kind);
  if (kind == UiKind::kSprite) valid = valid && ReadConstantRegisters(VertexBank(device),0,4,&pending.matrix[0][0]);
  else {
    // ReadConstantRegisters addresses its output by absolute register number;
    // it requires the full bank scratch, not a four-float destination.
    valid = valid && ReadBankRegisters(device,8,1);
    if(valid) {
      std::memcpy(pending.offset,t_bank+32,sizeof(pending.offset));
      if(kind==UiKind::kText) {pending.offset[0]*=2;pending.offset[1]*=2;}
    }
  }
  if (!valid && !failed_at) failed_at = 2;
  constexpr uint32_t registers[] = {0,30,31,46,47,77};
  for (uint32_t i = 0; valid && i < std::size(registers); ++i) {
    if (kind == UiKind::kVideo && i != 2) continue;
    if (kind==UiKind::kFade && i!=2) continue;
    valid = ReadPixelConstant(registers[i],pending.draw.registers[i]);
  }
  if (!valid && !failed_at) failed_at = 3;
  if(kind==UiKind::kFade) valid=valid && UiBlackFade(pending.draw.registers[2]) && ReadPixelConstant(20,pending.draw.registers[0]);
  else valid = valid && ReadUiLiterals(ps_obj,&pending.draw.registers[6]);
  uint64_t ps_literals[4]{};
  valid=valid && ReadLiteralRegisters(kPsLayout,ps_obj,0,256,ps_literals);
  uint64_t dynamic_mask=1ull<<31;
  if(kind==UiKind::kFade) dynamic_mask|=1ull<<20;
  else if(kind!=UiKind::kVideo) dynamic_mask|=(1ull<<0)|(1ull<<30)|(1ull<<46)|(1ull<<47);
  valid=valid && !(ps_literals[0]&dynamic_mask) && (kind==UiKind::kVideo || kind==UiKind::kFade || !(ps_literals[1]&(1ull<<13)));
  if(kind!=UiKind::kVideo && kind!=UiKind::kFade) valid=valid && pending.draw.registers[8][0]==1 && pending.draw.registers[8][1]==0;
  if (!valid && !failed_at) failed_at = 4;
  for (uint32_t i = 0; valid && i < 3; ++i) {
    if (kind == UiKind::kText && i == 2) continue;
    if(kind==UiKind::kFade && i!=0) continue;
    valid = ReadTextureFetch(device,kind == UiKind::kVideo ? i : 13+i,pending.draw.fetch[i]);
  }
  if (!valid && !failed_at) failed_at = 5;
  for (uint32_t i = 0; valid && i < 4; ++i) {
    uint32_t bits = 0;
    valid = ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x210F+i),&bits);
    std::memcpy(&pending.draw.viewport[i],&bits,4);
  }
  for (uint32_t i = 0; valid && i < 2; ++i)
    valid = ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x200E+i),&pending.draw.screen_scissor[i]);
  // UI menus also stitch XDK tiles into one full native target. Their viewport
  // and logical scissor precede the physical tile's offset/intersection.
  valid=valid && ReadWindowCommands(device,pending.draw,true);
  for (const auto& row : pending.matrix) for (float f : row) valid = valid && UiFinite(f);
  for (const auto& row : pending.draw.registers) for (float f : row) valid = valid && UiFinite(f, 65536);
  for (float f : pending.offset) valid = valid && UiFinite(f);
  for (float f : pending.draw.viewport) valid = valid && UiFinite(f,16384);
  if (!valid) {
    static thread_local uint32_t logged = 0;static thread_local uint64_t last=0;
    if (logged++ < 24 || (pending.frame % 120 == 0 && last!=pending.frame)) {
      last=pending.frame;
      REXLOG_WARN("[native] presentation capture: frame {} kind {} rejected at {} window {}/{}/{} tiling {} (1 storage/state, 2 VS, 3 PS, 4 literals, 5 fetch, 0 viewport/values)",
        pending.frame,uint32_t(kind),failed_at,t_window.state.known,t_window.virtual_window.known,t_window.virtual_frame,ReadTilingFlag(device) ? unsigned(*ReadTilingFlag(device)):0);}
    std::lock_guard<std::mutex> guard(g_scene_mutex);
    if (!current()) return;
    g_presentation.Refuse();
    return;
  }
  {
    std::lock_guard<std::mutex> guard(g_scene_mutex);
    if(!current() || !g_presentation.Begin()) return;
  }
  pending.valid = true;
  t_pending_ui = std::move(pending);
  t_pending_draws |= kPendingUi;
}

// Only the exact captured billboard variant is accepted. Its own literal table
// replaces c252..255; a table that overwrites a dynamic curve or light is refused.
bool ReadParticleLiterals(const VsChoice& choice,float (*bank)[4],bool snow=false) {
  uint32_t off=0,base=0,size=0;
  const uint64_t header=uint64_t(choice.obj)+kVsLayout.header_offset;
  if(header+xdk::kShaderLiteralTableField+8*choice.variant+4>uint64_t(UINT32_MAX)+1 ||
     !ReadVirtualBe32(uint32_t(header)+xdk::kShaderLiteralTableField+8*choice.variant,&off) || !off ||
     !ReadVirtualBe32(choice.obj+4*kVsLayout.base_dword,&base)) return false;
  const uint64_t table=header+off;
  if(table+xdk::kLiteralTableEntriesOffset>UINT32_MAX ||
     !ReadVirtualBe32(uint32_t(table)+xdk::kLiteralTableSizeOffset,&size) || !size || size>8*kMaxLiteralGuardRuns) return false;
  const uint8_t* entries=ReadVirtual(uint32_t(table)+xdk::kLiteralTableEntriesOffset,size);
  if(!entries) return false;
  xdk::LiteralRun runs[kMaxLiteralGuardRuns];
  const uint32_t count=xdk::ParseLiteralRuns(entries,size,runs,kMaxLiteralGuardRuns);
  const uint32_t first=snow ? 244:252;
  uint32_t have=0;
  for(uint32_t i=0;i<count;i++) {
    const auto& run=runs[i];
    if(run.first_register<first || run.dwords%4 || uint64_t(run.first_register)+run.dwords/4>256) return false;
    const uint64_t address=uint64_t(base)+run.offset;
    if(address+4ull*run.dwords>uint64_t(UINT32_MAX)+1) return false;
    const uint8_t* p=ReadPhysical(xdk::GpuAddress(uint32_t(address)),4*run.dwords);
    if(!p) return false;
    for(uint32_t word=0;word<run.dwords;word++) {
      const uint32_t reg=run.first_register+word/4,bits=LoadBe32(p+word*4);
      std::memcpy(&bank[reg][word%4],&bits,4);have|=1u<<(reg-first);
    }
  }
  constexpr float expected[4][4]={{0,0,0,0},{-.5f,.1f,0,0},{4,6.28318548f,1,.159154937f},{.25f,.5f,-2,-3.14159274f}};
  return snow ? have==4095 && std::memcmp(bank+244,kSnowLiterals,sizeof(kSnowLiterals))==0 :
    have==15 && std::memcmp(bank+252,expected,sizeof(expected))==0;
}

void ObserveSnowDraw(const LastArgs& args,uint32_t device,bool in_scene,const DeviceSnapshot& snapshot,uint32_t ps_obj,const PsInfo& ps) {
  const uint64_t frame=g_swaps.load(std::memory_order_relaxed)+1;
  const auto current=[&] {return g_presentation_frame.load(std::memory_order_relaxed) && frame==g_swaps.load(std::memory_order_relaxed)+1;};
  UiDraw draw;draw.particle_textures=1;draw.point_first=args.r[2];draw.point_count=args.r[3];
  VsChoice choice;const VsInfo* vs=ChooseVs(snapshot,&choice) ? LookupVs(choice):nullptr;
  uint32_t surface=0;
  if(!in_scene || ps.hash!=0x718C12FF9E1B888Bull || !vs || vs->hash!=0x19A01C01290E20A7ull ||
     !ReadVirtualBe32(device+xdk::kDeviceSurfaceInfoOffset,&surface) || (surface&16383)!=1120) {
    std::lock_guard<std::mutex> guard(g_scene_mutex);if(current()) ++g_presentation.particle_unsupported;return;
  }
  auto bank=std::make_shared<WeatherConstants>();
  bool valid=draw.point_count && draw.point_count<=4096 && uint64_t(draw.point_first)+draw.point_count<=65536 &&
    ReadBankRegisters(device,0,256) && ReadParticleLiterals(choice,reinterpret_cast<float (*)[4]>(t_bank),true);
  if(valid) for(uint32_t reg=0;reg<256;reg++) if(SnowRegisterUsed(reg)) std::copy(t_bank+reg*4,t_bank+reg*4+4,bank->c[reg]);
  ReadStateWords(device,&draw.state);valid=valid && draw.state.read && draw.state.vte_cntl==0x43F && ReadPresentationClip(device,draw);
  valid=valid && ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x2281),&draw.point_minmax) &&
    SnowInputsValid(*bank,draw.point_minmax) &&
    ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x2302),&draw.vertex_control) &&
    ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(xdk::kRegColorInfo),&draw.colour_info) && draw.colour_info==0x00030000;
  draw.surface_info=surface;
  for(uint32_t i=0;valid && i<4;i++) {
    uint32_t bits=0;valid=ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x210F+i),&bits);
    std::memcpy(&draw.viewport[i],&bits,4);valid=valid && UiFinite(draw.viewport[i],16384);
  }
  for(uint32_t i=0;valid && i<2;i++) valid=ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x200E+i),&draw.screen_scissor[i]);
  valid=valid && ReadWindowCommands(device,draw,true);
  uint64_t literal_regs[4]{};float literals[3][4]{};
  constexpr float expected[3][4]={{0,0,0,0},{0,0,0,0},{-1,2,0,0}};
  valid=valid && ReadLiteralRegisters(kPsLayout,ps_obj,0,256,literal_regs) &&
    !(literal_regs[0]&((1ull<<20)|(1ull<<31))) && ReadUiLiterals(ps_obj,literals) &&
    std::memcmp(literals,expected,sizeof(expected))==0 && ReadPixelConstant(20,draw.registers[0]) &&
    ReadPixelConstant(31,draw.registers[2]) && UiFinite(draw.registers[0][3],65536) && draw.registers[0][3]>=0;
  for(uint32_t i=0;i<3;i++) valid=valid && UiFinite(draw.registers[2][i],65536);
  valid=valid && ReadTextureFetch(device,13,draw.fetch[0]);
  std::shared_ptr<render::TextureSnapshot> source;
  if(valid) {source=std::make_shared<render::TextureSnapshot>();valid=render::TextureCache::CaptureSnapshot(draw.fetch[0],8ull<<20,source.get());}
  std::lock_guard<std::mutex> guard(g_scene_mutex);if(!current()) return;
  if(valid) {
    draw.textures[0]=g_presentation.ReuseTexture(draw.fetch[0],*source);
    if(!draw.textures[0]) {
      const uint64_t bytes=source->base.size()+source->mips.size();
      valid=bytes && g_presentation.asset_bytes<=kUiMaxAssetBytes && bytes<=kUiMaxAssetBytes-g_presentation.asset_bytes && g_presentation.assets.size()<kUiMaxAssets;
      if(valid) {UiAsset asset;std::copy(draw.fetch[0],draw.fetch[0]+6,asset.fetch.begin());asset.source=source;
        g_presentation.assets.push_back(std::move(asset));g_presentation.asset_bytes+=uint32_t(bytes);draw.textures[0]=std::move(source);}
    }
  }
  if(valid) {draw.weather=std::move(bank);g_presentation.AddParticle(std::move(draw));}
  else {++g_presentation.particle_refused;static thread_local uint32_t lines=0;
    if(lines++<8) REXLOG_WARN("[native] snow capture: rejected frame {} points {} colour {} window-known {}",frame,draw.point_count,draw.colour_info,t_window.virtual_window.known);}
}

void ObserveParticleDraw(uint32_t id,const LastArgs& args,uint32_t device,bool in_scene) {
  if(id!=kHook_D3DDevice_DrawVertices || ((args.r[1]&63)!=13 && (args.r[1]&63)!=1)) return;
  DeviceSnapshot snapshot;uint32_t ps_obj=0;VsChoice choice;
  if(!ReadDevice(device,&snapshot) || !ChoosePs(snapshot,&ps_obj)) return;
  const PsInfo* ps=LookupPs(ps_obj);
  if(!ps) return;
  if((args.r[1]&63)==1) {ObserveSnowDraw(args,device,in_scene,snapshot,ps_obj,*ps);return;}
  const bool soft=ps->hash==0xFF19CE696F916E0Cull;
  const uint32_t textures=ps->hash==0xDBFD88A80EDBCE36ull || soft ? 1 : ps->hash==0xC300519EC8915346ull ? 2 : ps->hash==0xA753A2072F78F00Full ? 3 : 0;
  // Half-resolution effects own a separate colour/depth target before post.
  const uint64_t frame=g_swaps.load(std::memory_order_relaxed)+1;
  const auto current=[&] {return g_presentation_frame.load(std::memory_order_relaxed) && frame==g_swaps.load(std::memory_order_relaxed)+1;};
  uint32_t surface=0;
  const bool surface_read=ReadVirtualBe32(device+xdk::kDeviceSurfaceInfoOffset,&surface);
  const bool world=surface_read && in_scene && (surface&16383)==1120;
  const bool half=surface_read && !in_scene && (surface&16383)==560 && !soft;
  const bool title=surface_read && !in_scene && (surface&16383)==1280 && textures==2;
  if((!world && !title && !half) || !textures) {
    std::lock_guard<std::mutex> guard(g_scene_mutex);
    if(current()) ++g_presentation.particle_unsupported;return;
  }
  UiDraw draw;draw.particle_textures=textures;draw.particle_soft=soft;draw.particle_half=half;draw.kind=title ? UiKind::kParticle:UiKind(0);
  const VsInfo* vs=ChooseVs(snapshot,&choice) ? LookupVs(choice) : nullptr;
  const uint32_t start=args.r[2],count=args.r[3];
  uint32_t first_byte=0,byte_count=0;
  StreamView stream;
  bool valid=vs && vs->hash==(soft ? 0x563E3BE17857DB59ull:0x29B6506FBACEB93Aull) && count && count<=4096 && count%4==0 &&
    start%4==0 && uint64_t(start)+count<=65536 && !ResolveStream(snapshot,95,&stream) && stream.fc_match &&
    snapshot.stream_stride_dw[0]==12 && (stream.fc1&3)==2 && stream.offset<=stream.size &&
    ParticleStreamSpan(start,count,stream.size-stream.offset,&first_byte,&byte_count);
  const uint8_t* bytes=valid ? ReadPhysical(stream.base+stream.offset+first_byte,byte_count) : nullptr;
  uint32_t failed_at=valid && bytes ? 0:1;
  valid=valid && bytes && ReadBankRegisters(device,0,256) && ReadParticleLiterals(choice,reinterpret_cast<float (*)[4]>(t_bank));
  if(!valid && !failed_at) failed_at=2;
  ReadStateWords(device,&draw.state);valid=valid && draw.state.read && ReadPresentationClip(device,draw);
  valid=valid && (draw.state.vte_cntl&0x700)==0x400;
  valid=valid && ReadVirtualBe32(device+xdk::kDeviceSurfaceInfoOffset,&draw.surface_info) &&
    ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x2302),&draw.vertex_control) &&
    ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(xdk::kRegColorInfo),&draw.colour_info);
  for(uint32_t i=0;valid && i<4;i++) {
    uint32_t bits=0;valid=ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x210F+i),&bits);
    std::memcpy(&draw.viewport[i],&bits,4);valid=valid && UiFinite(draw.viewport[i],16384);
  }
  for(uint32_t i=0;valid && i<2;i++) valid=ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x200E+i),&draw.screen_scissor[i]);
  valid=valid && ReadWindowCommands(device,draw,world);
  if(half) {
    uint32_t zoffset=0;
    valid=valid && draw.colour_info==0 && draw.viewport[0]==280 && draw.viewport[1]==280 &&
      draw.viewport[2]==-180 && draw.viewport[3]==180 && draw.state.vte_cntl==0x43F &&
      draw.state.vport_z_scale==0xBF800000 &&
      ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x2114),&zoffset) && zoffset==0x3F800000;
  }
  if(!valid && !failed_at) failed_at=3;
  // Pixel c20.w is captured, even in a native fallback frame whose exposure is
  // deliberately replaced by the native world's exposure at render time.
  valid=valid && ReadPixelConstant(20,draw.registers[0]);
  uint64_t literal_regs[4]{};
  valid=valid && ReadLiteralRegisters(kPsLayout,ps_obj,0,256,literal_regs) &&
    !(literal_regs[0]&((1ull<<20)|(textures>=2 ? (1ull<<31)|(1ull<<46) : 0)));
  if(textures>=2) valid=valid && ReadPixelConstant(31,draw.registers[2]) && ReadPixelConstant(46,draw.registers[3]);
  valid=valid && UiFinite(draw.registers[0][3],65536) && draw.registers[0][3]>=0;
  if(textures>=2) for(uint32_t row:{2u,3u}) for(uint32_t component=0;component<2;component++)
    valid=valid && UiFinite(draw.registers[row][component],65536);
  if(soft) {
    uint32_t depth_fetch[6]{},zoffset=0;
    valid=valid && ReadTextureFetch(device,13,depth_fetch) && (depth_fetch[0]&3)==2 &&
      (depth_fetch[1]&63)==23 && ((depth_fetch[5]>>9)&3)==1 && ((depth_fetch[3]>>1)&7)==0 &&
      (depth_fetch[2]&8191)+1==1120 && ((depth_fetch[2]>>13)&8191)+1==720 &&
      draw.colour_info==0x000C0000 && draw.state.vte_cntl==0x0000043F &&
      ReadVirtualBe32(device+xdk::DeviceRegShadowOffset(0x2114),&zoffset) && zoffset==0x3F800000 &&
      draw.state.vport_z_scale==0xBF800000;
    float literals[3][4]{};
    constexpr float expected[3][4]={{0,0,0,0},{2,.5f,0,0},{0,1,.01f,-1}};
    valid=valid && !(literal_regs[0]&((1ull<<0)|(1ull<<31))) && !(literal_regs[1]&(15ull<<52)) &&
      ReadUiLiterals(ps_obj,literals) && std::memcmp(literals,expected,sizeof(expected))==0 &&
      ReadPixelConstant(0,draw.particle_projection[0]) && ReadPixelConstant(31,draw.registers[2]);
    constexpr float viewport_uv[4]={0,0,1.0f/1120,1.0f/720};
    valid=valid && std::memcmp(draw.particle_projection[0],viewport_uv,sizeof(viewport_uv))==0 &&
      UiFinite(draw.registers[2][0],65536) && draw.registers[2][0]>=0;
    for(uint32_t row=0;valid && row<4;row++) {
      valid=ReadPixelConstant(116+row,draw.particle_projection[1+row]);
      for(float value:draw.particle_projection[1+row]) valid=valid && UiFinite(value);
    }
  }
  if(!valid && !failed_at) failed_at=4;
  for(uint32_t i=0;valid && i<textures;i++) valid=ReadTextureFetch(device,i,draw.fetch[i]);
  if(valid) draw.vertices.reserve(count*3/2);
  for(uint32_t q=0;valid && q<count;q+=4) {
    UiVertex quad[4];
    for(uint32_t v=0;valid && v<4;v++) {
      float values[8];
      for(uint32_t k=0;k<8;k++) {const uint32_t bits=LoadBe32(bytes+(q/4)*48+k*4);std::memcpy(values+k,&bits,4);}
      valid=ParticleVertexOf(start+q+v,values,values+4,reinterpret_cast<const float (*)[4]>(t_bank),quad+v);
    }
    if(valid) for(uint32_t v:{0u,1u,2u,0u,2u,3u}) draw.vertices.push_back(quad[v]);
  }
  if(!valid && !failed_at) failed_at=5;
  std::lock_guard<std::mutex> guard(g_scene_mutex);
  if(!current()) return;
  for(uint32_t i=0;valid && i<textures;i++) {
    auto source=std::make_shared<render::TextureSnapshot>();
    valid=render::TextureCache::CaptureSnapshot(draw.fetch[i],8ull<<20,source.get());
    if(valid) draw.textures[i]=g_presentation.ReuseTexture(draw.fetch[i],*source);
    if(draw.textures[i]) continue;
    // Particle failure cannot invalidate already captured HUD geometry.
    const uint64_t extent=source->base.size()+source->mips.size();
    valid=valid && extent && g_presentation.asset_bytes<=kUiMaxAssetBytes &&
      extent<=kUiMaxAssetBytes-g_presentation.asset_bytes && g_presentation.assets.size()<kUiMaxAssets;
    if(valid) {UiAsset asset;std::copy(draw.fetch[i],draw.fetch[i]+6,asset.fetch.begin());asset.source=source;
      g_presentation.assets.push_back(std::move(asset));g_presentation.asset_bytes+=uint32_t(extent);draw.textures[i]=std::move(source);}
  }
  if(valid) {
    if(title) g_presentation.Add(std::move(draw));else g_presentation.AddParticle(std::move(draw));
  } else {if(title) g_presentation.Refuse();else ++g_presentation.particle_refused;
    static thread_local uint32_t lines=0;
    if(lines++<8) REXLOG_WARN("[native] particle capture: rejected frame {} VS {} PS {} count {} stage {} window-known {}",frame,vs ? vs->hash:0,ps->hash,count,failed_at,t_window.state.known);}
}

void FinishUiFrame(uint64_t frame) {
  if(!g_presentation_frame.load(std::memory_order_relaxed) &&
     !g_presentation_wanted.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> guard(g_scene_mutex);
  g_presentation.Seal();  // a pending draw on another thread is a partial frame
  g_presentation.frame = frame;
  g_presentation.enabled = g_presentation_frame.load(std::memory_order_relaxed);
  g_presentation_publisher.Publish(g_presentation.enabled ? std::make_shared<const UiFrame>(std::move(g_presentation)) : nullptr);
  g_presentation = {};
  g_presentation_frame.store(g_presentation_wanted.load(std::memory_order_relaxed),std::memory_order_relaxed);
}
