#pragma once
#include <array>
#include <cstring>
#include <vector>
#include <rex/graphics/native_guest_renderer.h>

namespace fable2::native::render {
// Diagnostic only: two images from one GPU callback, before split/overlay
// replaces the reference. Capture and callback identities stay distinct:
// the SDK callback does not expose the guest swap number.
class PresentationProbe {
 public:
  void Release(rex::graphics::nrhi::Device* dev) {
    if(dev) for(auto b:buffers_) dev->DestroyDeferred(b);
    buffers_[0]=buffers_[1]=buffers_[2]=nullptr; device_=nullptr; pending_=false;
  }
  void Retry(rex::graphics::nrhi::Device* dev) { Release(device_==dev ? dev : nullptr); }
  bool Capture(const rex::graphics::NativeGuestOutputRenderContext& ctx,
               rex::graphics::nrhi::Texture* native, uint64_t captured_frame,
               uint64_t asked_frame, rex::graphics::nrhi::Texture* coverage=nullptr) {
    namespace n=rex::graphics::nrhi;
    if(device_ && device_!=ctx.device) Release(nullptr);
    if(!asked_frame || asked_frame==asked_ || pending_ || files_>=8) return false;
    if(ctx.guest_output_width!=1280 || ctx.guest_output_height!=720 ||
       (ctx.guest_output->format()!=n::Format::kR8G8B8A8_UNORM &&
        ctx.guest_output->format()!=n::Format::kR10G10B10A2_UNORM)) return false;
    device_=ctx.device;
    n::BufferDesc bd; bd.heap=n::HeapKind::kReadback; bd.size=1280ull*720*4;
    for(auto& b:buffers_) if(!b) {
      b=device_->CreateBuffer(bd); if(!b) {Release(device_); return false;}
    }
    coverage_=coverage && coverage->format()==n::Format::kR8_UNORM;
    ctx.cmd->Barrier(native,n::ResourceState::kPixelShaderResource,n::ResourceState::kCopySource);
    ctx.cmd->Barrier(ctx.guest_output,n::ResourceState::kGuestOutput,n::ResourceState::kCopySource);
    ctx.cmd->FlushBarriers();
    ctx.cmd->CopyTextureToBuffer(buffers_[0],0,5120,native,0,1280,720);
    ctx.cmd->CopyTextureToBuffer(buffers_[1],0,5120,ctx.guest_output,0,1280,720);
    if(coverage_) {
      ctx.cmd->Barrier(coverage,n::ResourceState::kPixelShaderResource,n::ResourceState::kCopySource);ctx.cmd->FlushBarriers();
      ctx.cmd->CopyTextureToBuffer(buffers_[2],0,1280,coverage,0,1280,720);
      ctx.cmd->Barrier(coverage,n::ResourceState::kCopySource,n::ResourceState::kPixelShaderResource);
    }
    ctx.cmd->Barrier(native,n::ResourceState::kCopySource,n::ResourceState::kPixelShaderResource);
    ctx.cmd->Barrier(ctx.guest_output,n::ResourceState::kCopySource,n::ResourceState::kGuestOutput);
    ctx.cmd->FlushBarriers();
    // F2UI v2: the v1 planes, then (header[12]==1) an R8 UI coverage plane.
    header_={}; header_[0]=0x49553246; header_[1]=2; header_[12]=coverage_ ? 1:0;
    header_[2]=1280; header_[3]=720; header_[4]=uint32_t(native->format());
    header_[5]=uint32_t(ctx.guest_output->format());
    header_[6]=uint32_t(captured_frame); header_[7]=uint32_t(captured_frame>>32);
    header_[8]=uint32_t(asked_frame); header_[9]=uint32_t(asked_frame>>32);
    submission_=device_->CurrentSubmission();
    header_[10]=uint32_t(submission_); header_[11]=uint32_t(submission_>>32);
    asked_=asked_frame; pending_=true; return true;
  }
  bool Take(rex::graphics::nrhi::Device* dev,std::vector<uint8_t>* bytes) {
    if(device_!=dev) {if(device_) Release(nullptr); return false;}
    if(!pending_ || dev->CompletedSubmission()<submission_) return false;
    constexpr uint32_t size=1280*720*4,cover=1280*720;
    bytes->resize(64+2*size+(coverage_ ? cover:0)); std::memcpy(bytes->data(),header_.data(),64);
    for(uint32_t i=0;i<(coverage_ ? 3u:2u);i++) {
      const uint32_t plane=i<2 ? size:cover;
      dev->InvalidateForRead(buffers_[i],0,plane); const void* p=dev->Map(buffers_[i]);
      if(!p) {bytes->clear(); Release(dev);return false;}
      std::memcpy(bytes->data()+64+(i<2 ? i*size:2*size),p,plane); dev->Unmap(buffers_[i]);
    }
    pending_=false; ++files_;return true;
  }
 private:
  rex::graphics::nrhi::Device* device_=nullptr;
  rex::graphics::nrhi::Buffer* buffers_[3]{};
  bool coverage_=false;
  std::array<uint32_t,16> header_{};
  uint64_t asked_=0,submission_=0;
  uint32_t files_=0;
  bool pending_=false;
};
} // namespace fable2::native::render
