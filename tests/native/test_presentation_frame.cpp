#include "../../src/native/capture/presentation_frame.h"
#include "../../src/native/capture/snow_inputs.h"
#include "../../src/native/capture/window_commands.h"
#include "../../src/native/render/texture_decode.h"
#include <cassert>
#include <limits>
#include <iostream>
using namespace fable2::native::capture;
int main() {
  auto commands=[](std::initializer_list<uint32_t> words) {
    std::vector<uint8_t> bytes;
    for(uint32_t w:words) for(int shift=24;shift>=0;shift-=8) bytes.push_back(uint8_t(w>>shift));
    return bytes;
  };
  WindowCommands window;
  // A payload which resembles a header must never be interpreted as one.
  auto unrelated=commands({0x00012000,0x00022080,0x20002000});
  assert(window.Apply(unrelated.data(),unrelated.size()/4) && !window.valid());
  auto direct=commands({0x00022080,0,0x80000000,0x02D00500});
  assert(window.Apply(direct.data(),4) && window.valid() && window.words[2]==0x02D00500);
  auto scissor=commands({0xC0032D00,0x00040080,0x7FFD7FFE,0x00140064,0x00C8012C});
  assert(window.Apply(scissor.data(),5) && window.words[0]==0x7FFD7FFE && window.words[1]==0x00140064);
  auto incomplete=commands({0x00022080,0,0});
  assert(!window.Apply(incomplete.data(),3) && !window.valid());
  assert(window.Apply(direct.data(),4) && window.valid());
  auto shader_constants=commands({0xC0035600,0x00002080,2,3,4});
  assert(window.Apply(shader_constants.data(),5) && window.words[0]==2 && window.words[2]==4);
  auto shader_predicated=commands({0xC0015601,0x00002082,9});
  assert(!window.Apply(shader_predicated.data(),3) && !window.valid());
  assert(window.Apply(direct.data(),4) && window.valid());
  auto indirect=commands({0xC0013F00,0,4});
  assert(!window.Apply(indirect.data(),3) && !window.valid());
  auto predicated=commands({0xC0032D01,0x00040080,0,0,0x20002000});
  assert(!window.Apply(predicated.data(),5) && !window.valid());
  auto type1=commands({0x40000000,0x00022080,0x00022080});
  assert(window.Apply(type1.data(),3) && !window.valid());
  for(uint32_t op:{0x25u,0x3Du,0x3Eu,0x4Fu,0x44u,0x45u,0x2Cu,0x48u,0x64u}) {
    assert(window.Apply(direct.data(),4) && window.valid());
    // A skipped or fetched write must not refresh an earlier known window.
    auto opaque=commands({0xC0000000|(op<<8),0});
    opaque.insert(opaque.end(),direct.begin(),direct.end());
    assert(!window.Apply(opaque.data(),opaque.size()/4) && !window.valid());
  }
  UiDraw clipped;
  assert(UiClipSupported(1u<<19) && UiDepthClip(1u<<19));
  assert(!UiClipSupported(0) && !UiClipSupported((1u<<19)|1));
  assert(UiClipSupported((1u<<16)|63) && !UiDepthClip((1u<<16)|63));
  clipped.screen_scissor[1]=(720u<<16)|1280;
  clipped.window_scissor[0]=(20u<<16)|100;
  clipped.window_scissor[1]=(200u<<16)|300;
  clipped.window_offset=(32765u<<16)|32766;  // (-2,-3), signed fifteen bits
  assert((UiScissorOf(clipped,1280,720)==std::array<int32_t,4>{98,17,298,197}));
  clipped.window_scissor[0]|=0x80000000;
  assert((UiScissorOf(clipped,1280,720)==std::array<int32_t,4>{100,20,300,200}));
  clipped.screen_scissor[0]=(30u<<16)|150;
  clipped.screen_scissor[1]=(170u<<16)|250;
  assert((UiScissorOf(clipped,1280,720)==std::array<int32_t,4>{150,30,250,170}));
  clipped.window_scissor[0]=(500u<<16)|500;
  assert((UiScissorOf(clipped,1280,720)==std::array<int32_t,4>{498,497,498,497}));
  clipped.viewport[0]=640;clipped.viewport[1]=640;clipped.viewport[2]=-360;clipped.viewport[3]=360;
  clipped.state.vte_cntl=0x43F;clipped.state.su_sc_mode=1u<<16;
  assert((UiViewportOf(clipped,true)==std::array<float,4>{640,638.5f,-360,357.5f}));
  clipped.state.vte_cntl=0;clipped.state.su_sc_mode=0;clipped.vertex_control=1;
  assert((UiViewportOf(clipped,true)==std::array<float,4>{1,0,1,0}));
  clipped.window_offset=0;clipped.screen_scissor[0]=0;clipped.screen_scissor[1]=(720u<<16)|1280;
  clipped.window_scissor[0]=0;clipped.window_scissor[1]=(720u<<16)|1280;
  clipped.clip_control=1u<<19;clipped.state.vte_cntl=15;
  clipped.viewport[0]=-100;clipped.viewport[1]=200.9f;clipped.viewport[2]=50;clipped.viewport[3]=100.8f;
  assert((UiScissorOf(clipped,1280,720)==std::array<int32_t,4>{100,50,300,150}));
  clipped.vertex_control=0;
  assert((UiScissorOf(clipped,1280,720,true)==std::array<int32_t,4>{101,51,301,151}));
  clipped.clip_control|=1u<<16;
  assert((UiScissorOf(clipped,1280,720)==std::array<int32_t,4>{0,0,1280,720}));
  clipped.clip_control=1u<<19;clipped.viewport[0]=std::numeric_limits<float>::quiet_NaN();
  assert((UiScissorOf(clipped,1280,720)==std::array<int32_t,4>{0,0,0,0}));
  assert(UiShaderKind(kUiSpriteVs,kUiSpritePs)==UiKind::kSprite);
  assert(UiShaderKind(kUiTextVs,kUiTextPs)==UiKind::kText);
  assert(UiShaderKind(kUiVideoVs,kUiVideoPs)==UiKind::kVideo);
  assert(UiShaderKind(kUiVideoVs,kUiFadePs)==UiKind::kFade);
  assert(UiShaderKind(kUiSpriteVs,kUiTextPs)==UiKind(0));
  assert(!UiFinite(std::numeric_limits<float>::quiet_NaN()));
  assert(!UiFinite(std::numeric_limits<float>::infinity()));
  UiFrame f; UiDraw d; d.kind=UiKind::kSprite; d.vertices.resize(3);
  assert(f.Add(d));assert(f.vertices==3);
  UiDraw invalid; invalid.vertices.resize(4);
  assert(!f.Add(invalid));assert(!f.complete && f.refused==1 && f.vertices==3);
  assert(!f.Add(d));assert(f.draws.size()==1);
  UiFrame full; full.vertices=kUiMaxVertices-2;
  assert(!full.Add(d));assert(!full.complete);
  UiFrame overflow; overflow.vertices=UINT32_MAX;
  assert(!overflow.Add(d));
  UiFrame video_bound; video_bound.plane_bytes=kUiMaxFramePlaneBytes;
  UiDraw plane_draw=d;plane_draw.planes[0].pixels.resize(1);
  assert(!video_bound.Add(plane_draw));
  auto asset=std::make_shared<fable2::native::render::TextureSnapshot>();
  asset->base={1,2,3,4};asset->hash=17;
  uint32_t fetch[6]={2,3,4,5,6,7};
  UiFrame textures;assert(textures.HoldTexture(fetch,asset));
  assert(textures.asset_bytes==4 && textures.assets[0].source->base[2]==3);
  assert(textures.ReuseTexture(fetch,*asset)==asset);
  auto changed=*asset;changed.base[2]=9;
  // Even an equal fingerprint cannot hide a changed atlas payload.
  assert(!textures.ReuseTexture(fetch,changed));
  changed=*asset;changed.mips={2};assert(!textures.ReuseTexture(fetch,changed));
  uint32_t other_fetch[6]={2,3,4,5,6,8};assert(!textures.ReuseTexture(other_fetch,*asset));
  UiFrame limit;limit.asset_bytes=kUiMaxAssetBytes;
  assert(!limit.HoldTexture(fetch,asset) && !limit.complete && limit.assets.empty());
  UiFrame asset_overflow;asset_overflow.asset_bytes=UINT32_MAX;
  assert(!asset_overflow.HoldTexture(fetch,asset));
  UiFrame missing;assert(!missing.HoldTexture(fetch,nullptr));
  d.textures[0]=asset;assert(textures.Add(d));asset.reset();
  auto held=std::make_shared<const UiFrame>(std::move(textures));
  d.textures[0].reset();assert(held->draws[0].textures[0]->base[3]==4);
  UiPublisher publisher;
  auto snapshot=std::make_shared<const UiFrame>(std::move(f));publisher.Publish(snapshot);
  auto kept=publisher.Latest();publisher.Publish(nullptr);
  assert(!publisher.Latest() && kept->draws.size()==1);
  using namespace fable2::native::render;
  assert(TexFormatFromXenos(59)==TexFormat::kDxt5a);
  assert(FormatInfo(TexFormat::kDxt5a).bytes_per_block==8);
  assert(TexFormatFromXenos(4)==TexFormat::k565);
  assert(FormatInfo(TexFormat::k565).bytes_per_block==2);
  UiFrame movie; UiDraw video;video.kind=UiKind::kVideo;video.vertices.resize(6);
  assert(movie.Add(video) && movie.standalone());
  assert(!held->standalone()); UiFrame menu;menu.menu=true;assert(menu.standalone());
  float ramp[256][3]{},inverse[256][4]{};
  for(uint32_t i=0;i<256;i++) {
    float x=float(i)/255;
    ramp[i][0]=x;ramp[i][1]=std::sqrt(x);ramp[i][2]=x*x;
  }
  assert(UiInverseRamp(ramp,inverse));
  for(uint32_t i=0;i<256;i++) for(uint32_t c=0;c<3;c++) {
    const float x=inverse[i][c]*255;const uint32_t lo=uint32_t(x),hi=std::min(lo+1,255u);
    const float displayed=ramp[lo][c]+(ramp[hi][c]-ramp[lo][c])*(x-lo);
    assert(std::abs(displayed-float(i)/255)<1e-5f);
  }
  ramp[100][0]=0;assert(!UiInverseRamp(ramp,inverse));
  assert(!UiInverseRamp(nullptr,inverse));
  UiFrame unfinished;assert(unfinished.Begin());unfinished.Seal();
  assert(!unfinished.complete && unfinished.pending==1);
  UiFrame completed;assert(completed.Begin());--completed.pending;assert(completed.Add(d));
  completed.Seal();assert(completed.complete);
  UiFrame weather;UiDraw snow;snow.weather=std::make_shared<const WeatherConstants>();snow.point_count=4096;
  for(uint32_t i=0;i<32;i++) assert(weather.AddParticle(snow));
  assert(weather.weather_points==131072 && weather.particle_vertices==0);
  assert(!weather.AddParticle(snow) && weather.complete && weather.particle_refused==1);
  UiFrame invalid_weather;snow.point_first=65535;assert(!invalid_weather.AddParticle(snow));
  snow.point_first=0;snow.point_count=0;assert(!invalid_weather.AddParticle(snow));
  WeatherConstants bank;bank.c[46][0]=bank.c[46][1]=16;bank.c[75][1]=100;
  assert(SnowInputsValid(bank,0x00100000));
  assert(!SnowInputsValid(bank,0x00000010));
  bank.c[46][0]=0;assert(!SnowInputsValid(bank,0x00100000));bank.c[46][0]=16;
  bank.c[75][1]=0;assert(!SnowInputsValid(bank,0x00100000));bank.c[75][1]=100;
  bank.c[73][2]=-1;assert(!SnowInputsValid(bank,0x00100000));bank.c[73][2]=0;
  bank.c[144][0]=std::numeric_limits<float>::quiet_NaN();assert(!SnowInputsValid(bank,0x00100000));
  UiFrame quiet;quiet.enabled=true;assert(UiCompositable(quiet,true) && !UiCompositable(quiet,false));
  assert(UiUnsupportedWorldPost(0x577CB5267A8C92C3ull) && UiUnsupportedWorldPost(0x15675EB73FB39170ull));
  assert(!UiUnsupportedWorldPost(0x37B186F4BD3288F7ull) && !UiUnsupportedWorldPost(kPostDofPs));
  for(uint64_t hash:{0x577CB5267A8C92C3ull,0x15675EB73FB39170ull}) {
    quiet.unsupported_world_post=hash;
    assert(!UiCompositable(quiet,true) && !quiet.world_post_supported());
    // Opaque UI-only menu and movie paths remain independent of world post.
    quiet.menu=true;assert(quiet.world_post_supported());quiet.menu=false;
    movie.enabled=true;movie.unsupported_world_post=hash;
    assert(movie.world_post_supported() && UiCompositable(movie,false));
  }
  quiet.unsupported_world_post=0;assert(UiCompositable(quiet,true));
  const UiFrame fresh;assert(!fresh.unsupported_world_post && fresh.world_post_supported());
  UiDraw depth_quad;
  const float corners[4][5]={{-1,-1,.5f,0,1},{-1,1,.5f,0,0},{1,-1,.5f,1,1},{1,1,.5f,1,0}};
  for(uint32_t index:{0u,1u,2u,1u,3u,2u}) {
    UiVertex vertex;std::copy(corners[index],corners[index]+3,vertex.clip);vertex.clip[3]=1;
    std::copy(corners[index]+3,corners[index]+5,vertex.uv);depth_quad.vertices.push_back(vertex);
  }
  assert(UiEffectDepthQuad(depth_quad.vertices));
  depth_quad.vertices[4].uv[0]=.5f;assert(!UiEffectDepthQuad(depth_quad.vertices));
  depth_quad.vertices[4].uv[0]=1;depth_quad.vertices[4].clip[3]=0;assert(!UiEffectDepthQuad(depth_quad.vertices));
  depth_quad.vertices[4].clip[3]=1;depth_quad.vertices.pop_back();assert(!UiEffectDepthQuad(depth_quad.vertices));
  quiet.menu=true;assert(!UiCompositable(quiet,true));quiet.menu=false;
  quiet.Refuse();assert(!UiCompositable(quiet,true));
  float tint[]={0,0,0,0};assert(UiBlackFade(tint));tint[3]=1;assert(UiBlackFade(tint));
  tint[0]=.1f;assert(!UiBlackFade(tint));tint[0]=0;tint[3]=-1;assert(!UiBlackFade(tint));
  std::cout<<"Presentation: bounds, refusal, snapshot lifetime, atlas formats and current world-post fallback pass\n";
}
