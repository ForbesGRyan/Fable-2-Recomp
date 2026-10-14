#include "../../src/native/render/presentation_shaders.h"
#include "../../src/native/capture/presentation_frame.h"
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
using Microsoft::WRL::ComPtr;
using fable2::native::capture::UiVertex;
// Render the actual producer shaders, then compare every texel with an
// independent bilinear/alpha-blend reference. No game or guest mask required.
int main(int argc,char** argv) {
  const bool varying=argc==2 && std::string(argv[1])=="gradient";
  assert(argc==1 || varying);
  ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
  assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
  const char* source=fable2::native::render::ui_shaders::kSource;
  auto compile=[&](const char* entry,const char* profile) {
    ComPtr<ID3DBlob> code,errors;
    auto hr=D3DCompile(source,std::strlen(source),entry,nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
    if(FAILED(hr)) std::cerr<<(errors ? static_cast<const char*>(errors->GetBufferPointer()):"mask compile failed");
    assert(SUCCEEDED(hr));return code;
  };
  auto vs_code=compile("mask_vertex","vs_5_0"),ps_code=compile("mask","ps_5_0");
  ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
  assert(SUCCEEDED(device->CreateVertexShader(vs_code->GetBufferPointer(),vs_code->GetBufferSize(),nullptr,&vs)));
  assert(SUCCEEDED(device->CreatePixelShader(ps_code->GetBufferPointer(),ps_code->GetBufferSize(),nullptr,&ps)));
  UiVertex corners[4]{};
  for(uint32_t i=0;i<4;i++) {
    corners[i].clip[0]=(i&1) ? 1:-1;corners[i].clip[1]=(i&2) ? -1:1;
    corners[i].clip[2]=.5f;corners[i].clip[3]=1;
    corners[i].uv[0]=(i&1) ? 1:0;corners[i].uv[1]=(i&2) ? 1:0;
  }
  constexpr uint32_t indices[]={1,0,2,2,3,1};UiVertex vertices[6];
  for(uint32_t i=0;i<6;i++) vertices[i]=corners[indices[i]];
  D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(vertices);bd.Usage=D3D11_USAGE_IMMUTABLE;
  bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=sizeof(UiVertex);
  D3D11_SUBRESOURCE_DATA data{};data.pSysMem=vertices;ComPtr<ID3D11Buffer> vb;
  assert(SUCCEEDED(device->CreateBuffer(&bd,&data,&vb)));
  D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sv.Buffer.NumElements=6;
  ComPtr<ID3D11ShaderResourceView> vertices_view;assert(SUCCEEDED(device->CreateShaderResourceView(vb.Get(),&sv,&vertices_view)));
  float constants[64]{};const float viewport[]={320,320,-180,180};std::copy(viewport,viewport+4,constants+36);
  constants[41]=0;constants[42]=1;constants[44]=0;constants[45]=constants[46]=1;
  // Draw CB: viewport at 144, alpha at 160, texinfo[0] at 176.
  bd={};bd.ByteWidth=256;bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  data.pSysMem=constants;ComPtr<ID3D11Buffer> cb;assert(SUCCEEDED(device->CreateBuffer(&bd,&data,&cb)));
  // A representable nonunit alpha isolates the blend factors from sampler
  // fraction precision. Varying alpha is covered by the pixel formula proof.
  float atlas[4][4]={{.13f,.27f,.91f,.5f},{.89f,.31f,.11f,.5f},
                         {.42f,.79f,.23f,.5f},{.07f,.13f,.61f,.5f}};
  if(varying) {const float alpha[]={.07f,.93f,.37f,.67f};for(uint32_t i=0;i<4;i++) atlas[i][3]=alpha[i];}
  D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=2;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
  td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  data.pSysMem=atlas;data.SysMemPitch=2*16;ComPtr<ID3D11Texture2D> image;
  assert(SUCCEEDED(device->CreateTexture2D(&td,&data,&image)));
  ComPtr<ID3D11ShaderResourceView> atlas_view;assert(SUCCEEDED(device->CreateShaderResourceView(image.Get(),nullptr,&atlas_view)));
  td.Width=640;td.Height=360;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target,readback;assert(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&target)));
  ComPtr<ID3D11RenderTargetView> rtv;assert(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&rtv)));
  td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  assert(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&readback)));
  D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
  ComPtr<ID3D11SamplerState> sampler;assert(SUCCEEDED(device->CreateSamplerState(&sd,&sampler)));
  D3D11_BLEND_DESC blend{};auto& rt=blend.RenderTarget[0];rt.BlendEnable=TRUE;rt.RenderTargetWriteMask=15;
  rt.SrcBlend=rt.SrcBlendAlpha=D3D11_BLEND_SRC_ALPHA;rt.DestBlend=rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
  rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
  ComPtr<ID3D11BlendState> bs;assert(SUCCEEDED(device->CreateBlendState(&blend,&bs)));
  D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;rd.ScissorEnable=TRUE;
  ComPtr<ID3D11RasterizerState> raster;assert(SUCCEEDED(device->CreateRasterizerState(&rd,&raster)));
  D3D11_VIEWPORT vp{0,0,640,360,0,1};D3D11_RECT scissor{0,0,640,360};
  const float clear[4]{};context->ClearRenderTargetView(rtv.Get(),clear);auto* render_target=rtv.Get();context->OMSetRenderTargets(1,&render_target,nullptr);
  context->OMSetBlendState(bs.Get(),nullptr,UINT32_MAX);context->RSSetState(raster.Get());context->RSSetViewports(1,&vp);context->RSSetScissorRects(1,&scissor);
  context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
  auto* buffer=cb.Get();context->VSSetConstantBuffers(0,1,&buffer);context->PSSetConstantBuffers(0,1,&buffer);
  auto* geometry=vertices_view.Get();context->VSSetShaderResources(3,1,&geometry);
  ID3D11ShaderResourceView* textures[]={atlas_view.Get(),atlas_view.Get(),atlas_view.Get()};context->PSSetShaderResources(0,3,textures);
  ID3D11SamplerState* samplers[]={sampler.Get(),sampler.Get(),sampler.Get(),sampler.Get()};context->PSSetSamplers(0,4,samplers);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->Draw(6,0);
  render_target=nullptr;context->OMSetRenderTargets(1,&render_target,nullptr);context->CopyResource(readback.Get(),target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)));
  uint32_t worst=0;uint64_t total_error=0;
  for(uint32_t y=0;y<360;y++) for(uint32_t x=0;x<640;x++) {
    const double sx=std::clamp(2*(x+.5)/640-.5,0.0,1.0),sy=std::clamp(2*(y+.5)/360-.5,0.0,1.0);
    double sample[4]{};
    for(uint32_t channel=0;channel<4;channel++) sample[channel]=
      atlas[0][channel]*(1-sx)*(1-sy)+atlas[1][channel]*sx*(1-sy)+atlas[2][channel]*(1-sx)*sy+atlas[3][channel]*sx*sy;
    auto* actual=static_cast<const uint8_t*>(mapped.pData)+uint64_t(y)*mapped.RowPitch+4*x;
    for(uint32_t channel=0;channel<4;channel++) {
      int expected=int(std::floor(std::clamp(sample[channel]*sample[3],0.0,1.0)*255+.5));
      uint32_t error=uint32_t(std::abs(int(actual[channel])-expected));worst=std::max(worst,error);
      total_error+=error;
      // The gradient includes finite-precision texture filtering and UNORM8
      // blending. Keep its measured two-level allowance distinct from the
      // binary-exact alpha test and from a paired in-game image gate.
      if(error>(varying ? 2u:1u)) {std::cerr<<"Mask texel "<<x<<','<<y<<" channel "<<channel<<": "<<int(actual[channel])<<" expected "<<expected<<'\n';context->Unmap(readback.Get(),0);return 1;}
    }
  }
  context->Unmap(readback.Get(),0);
  const double mean=double(total_error)/(640*360*4);assert(mean<=1);
  std::cout<<"Actual mask HLSL/linear sampling/RGBA8 "<<(varying ? "varying-alpha":"constant-alpha")<<" blend: 230400 texels PASS (max "<<worst<<"/255, mean "<<mean<<"/255)\n";
}
