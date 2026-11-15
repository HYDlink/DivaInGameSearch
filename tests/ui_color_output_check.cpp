#include "ui_color_output.h"
#include "ui.h"
#include "ui_color_profile.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
// 参数 value/message 为测试条件和中文失败原因。
static void Check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
// 参数 value/expected/tolerance/message 为实测值、标准值、允许误差和中文原因。
static void Near(float value,float expected,float tolerance,const char* message) {Check(std::abs(value-expected)<=tolerance,message);}
// 参数 value 为线性 SDR 分量；用 CPU 标准公式作为 GPU 转换的独立参考。
static float Linear(float value) {return value<=0.04045f?value/12.92f:std::pow((value+0.055f)/1.055f,2.4f);}
// 参数 nits 为绝对亮度；参考 ST.2084 常量，验证 203 尼特不会变成峰值白。
static float Pq(float nits) {
    const double p=std::pow(nits/10000.0,2610.0/16384.0);
    return static_cast<float>(std::pow((3424.0/4096.0+2413.0/128.0*p)/(1+2392.0/128.0*p),2523.0/32.0));
}
// 参数 encoded 为 PQ 分量；返回绝对亮度，用于在测试端模拟 SDR 截图的 80 尼特参考白。
static float FromPq(float encoded) {
    const double p=std::pow(std::max(0.0f,encoded),32.0/2523.0);
    return static_cast<float>(10000*std::pow(std::max(p-3424.0/4096.0,0.0)/(2413.0/128.0-2392.0/128.0*p),16384.0/2610.0));
}
// 参数 linear 为线性 SDR 分量；保留超过 1 的值，让回归明确发现亮部被过度抬升。
static float Srgb(float linear) {
    return linear<=0.0031308f?12.92f*linear:1.055f*std::pow(linear,1.0f/2.4f)-0.055f;
}
// 参数 value 为读回的半精度位值；不用渲染器实现解码，避免镜像测试。
static float Half(uint16_t value) {
    const int exponent=(value>>10)&31,mantissa=value&1023;
    const float magnitude=exponent?std::ldexp(1.0f+mantissa/1024.0f,exponent-15):std::ldexp(static_cast<float>(mantissa),-24);
    return value&0x8000?-magnitude:magnitude;
}
// 参数 device/context/texture 为 WARP 对象；只在自动测试执行同步读回，不进入游戏帧。
static std::vector<std::array<float,4>> Read(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;
    desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)),"创建读回纹理失败");
    context->CopyResource(staging.Get(),texture);D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"GPU 像素读回失败");
    std::vector<std::array<float,4>> values(desc.Width*desc.Height);
    for(UINT y=0;y<desc.Height;++y) for(UINT x=0;x<desc.Width;++x) {
        const auto* row=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch;
        auto& v=values[y*desc.Width+x];
        if(desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM) {
            uint32_t p;std::memcpy(&p,row+x*4,4);
            v={float(p&1023)/1023,float((p>>10)&1023)/1023,float((p>>20)&1023)/1023,float(p>>30)/3};
        } else if(desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT) {
            uint16_t p[4];std::memcpy(p,row+x*8,8);v={Half(p[0]),Half(p[1]),Half(p[2]),Half(p[3])};
        } else {
            const auto* p=row+x*4;v={p[0]/255.0f,p[1]/255.0f,p[2]/255.0f,p[3]/255.0f};
        }
    }
    context->Unmap(staging.Get(),0);return values;
}
// 参数 pixel/output/white 为 GPU 输出像素、编码和界面白；只用于离屏比较旧 SDR 渲染观感。
static std::array<float,3> SdrPixel(const std::array<float,4>& pixel,diva::UiColorOutput output,float white) {
    std::array<float,3> linear{};
    if(output==diva::UiColorOutput::Hdr10) {
        const float r=FromPq(pixel[0]),g=FromPq(pixel[1]),b=FromPq(pixel[2]);
        linear={(1.660491f*r-0.587641f*g-0.072850f*b)/white,
                (-0.124550f*r+1.132900f*g-0.008349f*b)/white,
                (-0.018151f*r-0.100579f*g+1.118730f*b)/white};
    } else linear={pixel[0]*80/white,pixel[1]*80/white,pixel[2]*80/white};
    return {Srgb(linear[0]),Srgb(linear[1]),Srgb(linear[2])};
}
// 参数 device/context/output/white/opacity/scale/bg 为设备、输出模式、参考白、透明度、DPI 和游戏底色。
// 用真实旧 SDR 后端作为基准，验证背景观感；另外与未启用背景兼容的 HDR 输出逐像素比较图片区域。
static void CheckBackgroundCompatibility(ID3D11Device* device,ID3D11DeviceContext* context,diva::UiColorOutput output,
                                         float white,float opacity,UINT scale,const std::array<unsigned char,3>& bg) {
    D3D11_TEXTURE2D_DESC desc{};desc.Width=160*scale;desc.Height=48*scale;desc.MipLevels=1;desc.ArraySize=1;
    desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    desc.Format=output==diva::UiColorOutput::Hdr10?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_R16G16B16A16_FLOAT;
    ComPtr<ID3D11Texture2D> buffer,reference;ComPtr<ID3D11RenderTargetView> target,referenceTarget;
    Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&buffer)),"创建背景兼容 HDR 目标失败");
    Check(SUCCEEDED(device->CreateRenderTargetView(buffer.Get(),nullptr,&target)),"创建背景兼容 HDR 视图失败");
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&reference)),"创建旧 SDR 对照目标失败");
    Check(SUCCEEDED(device->CreateRenderTargetView(reference.Get(),nullptr,&referenceTarget)),"创建旧 SDR 对照视图失败");
    const float referenceBg[4]={bg[0]/255.0f,bg[1]/255.0f,bg[2]/255.0f,1};
    float hdrBg[4]={0,0,0,1};const float r=Linear(referenceBg[0])*white,g=Linear(referenceBg[1])*white,b=Linear(referenceBg[2])*white;
    if(output==diva::UiColorOutput::Hdr10) {
        hdrBg[0]=Pq(0.627404f*r+0.329283f*g+0.043313f*b);
        hdrBg[1]=Pq(0.069097f*r+0.919540f*g+0.011362f*b);
        hdrBg[2]=Pq(0.016391f*r+0.088013f*g+0.895595f*b);
    } else {hdrBg[0]=r/80;hdrBg[1]=g/80;hdrBg[2]=b/80;}
    // 第二个 texel 带透明边缘；叠在不透明卡片上时应与本修复前的 HDR 路径逐像素一致。
    const unsigned char imagePixels[]={224,160,128,255,32,192,208,96};
    desc.Width=2;desc.Height=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{imagePixels,8,8};
    ComPtr<ID3D11Texture2D> image;ComPtr<ID3D11ShaderResourceView> imageView;
    Check(SUCCEEDED(device->CreateTexture2D(&desc,&initial,&image)),"创建背景保护图片失败");
    Check(SUCCEEDED(device->CreateShaderResourceView(image.Get(),nullptr,&imageView)),"创建背景保护图片视图失败");
    auto& io=ImGui::GetIO();io.DisplaySize={160,48};io.DisplayFramebufferScale={float(scale),float(scale)};io.DeltaTime=1.0f/60;
    ImGui_ImplDX11_NewFrame();ImGui::NewFrame();auto* list=ImGui::GetBackgroundDrawList();
    list->AddRectFilled({16,0},{144,48},IM_COL32(17,19,22,static_cast<int>(opacity*255+0.5f)));
    list->AddRectFilled({32,8},{64,40},IM_COL32(35,40,47,255));
    const auto textureId=static_cast<ImTextureID>(static_cast<void*>(imageView.Get()));
    list->AddImage(textureId,{32,8},{48,40},{0.25f,0.5f},{0.25f,0.5f});
    list->AddImage(textureId,{48,8},{64,40},{0.75f,0.5f},{0.75f,0.5f});
    list->AddText({88,8},IM_COL32_WHITE,"HDR");
    list->AddRectFilled({112,8},{120,16},IM_COL32_WHITE);
    // 主窗口外另有半透明图形，也必须保持此前线性混合，不能全局更改所有透明像素。
    list->AddRectFilled({0,8},{8,40},IM_COL32(128,200,64,128));ImGui::Render();
    auto* data=ImGui::GetDrawData();auto* rt=referenceTarget.Get();
    context->ClearRenderTargetView(referenceTarget.Get(),referenceBg);context->OMSetRenderTargets(1,&rt,nullptr);
    ImGui_ImplDX11_RenderDrawData(data);context->OMSetRenderTargets(0,nullptr,nullptr);
    const auto sdr=Read(device,context,reference.Get());
    diva::UiColorRenderer renderer;
    context->ClearRenderTargetView(target.Get(),hdrBg);
    Check(SUCCEEDED(renderer.Render(device,context,buffer.Get(),target.Get(),data,output,white)),"绘制既有 HDR 对照失败");
    const auto previous=Read(device,context,buffer.Get());
    context->ClearRenderTargetView(target.Get(),hdrBg);const ImVec4 bounds{16,0,144,48};
    Check(SUCCEEDED(renderer.Render(device,context,buffer.Get(),target.Get(),data,output,white,&bounds)),"绘制背景兼容输出失败");
    const auto corrected=Read(device,context,buffer.Get());const UINT width=160*scale;
    for(UINT x:{24u,80u,136u}) {
        const size_t at=32*scale*width+x*scale;const auto color=SdrPixel(corrected[at],output,white);
        for(int c=0;c<3;++c) Near(color[c],sdr[at][c],3.0f/255,"HDR 半透明背景仍未对齐旧 SDR 后端");
    }
    bool changed=false;
    for(UINT y=0;y<48*scale;++y) for(UINT x=0;x<width;++x) {
        const size_t at=y*width+x;
        const bool outside=x<16*scale || x>=144*scale;
        const bool card=x>=32*scale && x<64*scale && y>=8*scale && y<40*scale;
        const bool whiteText=x>=112*scale && x<120*scale && y>=8*scale && y<16*scale;
        if(outside || card || whiteText || opacity==1) Check(corrected[at]==previous[at],"背景兼容改动了图片、透明图片边缘、文字白或主窗口外像素");
        changed=changed || corrected[at]!=previous[at];
    }
    if(opacity<1) Check(changed,"半透明背景没有实际应用兼容修复");
}
// 参数 device/context/output 为真实 WARP 设备和目标模式；生产默认值必须保持 SDR 素材层次。
// 上一版虽然正确生成 203 尼特，却没有验证 80 尼特 SDR 参考下的实际观感，本回归覆盖这一缺口。
static void CheckSdrReferenceRoundTrip(ID3D11Device* device,ID3D11DeviceContext* context,diva::UiColorOutput output) {
    const bool hdr10=output==diva::UiColorOutput::Hdr10;
    // 同时覆盖截图中的深灰、常见肤色、青色，以及白衣的五档接近白色细节。
    const std::array<std::array<unsigned char,4>,10> colors{{
        {35,40,47,255},{128,128,128,255},{224,160,128,255},{32,192,208,255},
        {192,192,192,255},{224,224,224,255},{240,240,240,255},
        {248,248,248,255},{252,252,252,255},{255,255,255,255}}};
    D3D11_TEXTURE2D_DESC desc{};desc.Width=160;desc.Height=16;desc.MipLevels=1;desc.ArraySize=1;
    desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    desc.Format=hdr10?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_R16G16B16A16_FLOAT;
    ComPtr<ID3D11Texture2D> buffer;ComPtr<ID3D11RenderTargetView> target;
    Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&buffer)),"创建 SDR 参考白测试目标失败");
    Check(SUCCEEDED(device->CreateRenderTargetView(buffer.Get(),nullptr,&target)),"创建 SDR 参考白测试视图失败");
    const float clear[4]={0,0,0,1};context->ClearRenderTargetView(target.Get(),clear);
    desc.Width=static_cast<UINT>(colors.size());desc.Height=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{colors.data(),static_cast<UINT>(sizeof(colors)),static_cast<UINT>(sizeof(colors))};
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
    Check(SUCCEEDED(device->CreateTexture2D(&desc,&initial,&texture)),"创建亮部层次图片失败");
    Check(SUCCEEDED(device->CreateShaderResourceView(texture.Get(),nullptr,&view)),"创建亮部层次图片视图失败");
    auto& io=ImGui::GetIO();io.DisplaySize={160,16};io.DisplayFramebufferScale={1,1};io.DeltaTime=1.0f/60;
    ImGui_ImplDX11_NewFrame();ImGui::NewFrame();auto* list=ImGui::GetBackgroundDrawList();
    for(size_t i=0;i<colors.size();++i) {
        const float u=(static_cast<float>(i)+0.5f)/static_cast<float>(colors.size());
        list->AddImage(static_cast<ImTextureID>(static_cast<void*>(view.Get())),{float(i*16),0},{float((i+1)*16),16},{u,0.5f},{u,0.5f});
    }
    ImGui::Render();
    diva::View productionDefaults;diva::UiColorRenderer renderer;
    const auto lumaSdr=diva::ParseUiColorProfile("[Luma]\nDisplayMode=0\nUIPaperWhite=300\n");
    const auto white=diva::ResolveUiWhite(productionDefaults.uiWhiteNits,lumaSdr);
    Check(SUCCEEDED(renderer.Render(device,context,buffer.Get(),target.Get(),ImGui::GetDrawData(),output,white.nits)),"生产默认亮度绘制失败");
    const auto values=Read(device,context,buffer.Get());float previousHighlight=-1;
    for(size_t i=0;i<colors.size();++i) {
        const auto& encoded=values[8*160+i*16+8];std::array<float,3> linear{};
        if(hdr10) {
            const float r=FromPq(encoded[0]),g=FromPq(encoded[1]),b=FromPq(encoded[2]);
            linear={(1.660491f*r-0.587641f*g-0.072850f*b)/80,
                    (-0.124550f*r+1.132900f*g-0.008349f*b)/80,
                    (-0.018151f*r-0.100579f*g+1.118730f*b)/80};
        } else linear={encoded[0],encoded[1],encoded[2]};
        for(int c=0;c<3;++c) Near(Srgb(linear[c]),colors[i][c]/255.0f,0.008f,
            "生产默认界面白抬升了 SDR 图片，深灰、肤色或白衣亮部不能保持原值");
        if(i>=4) {
            const float highlight=Srgb(linear[0]);
            Check(highlight>previousHighlight,"白衣不同亮部层次合并，仍有过曝");
            Check(highlight<=1.008f,"白衣亮部超出 SDR 白，截图时仍会裁成纯白");
            previousHighlight=highlight;
        }
    }
}
// 参数 device/context/renderer/output/scale 为设备、适配器、输出模式和 DPI 缩放；验证真实纹理与透明合成。
static void CheckPixels(ID3D11Device* device,ID3D11DeviceContext* context,diva::UiColorRenderer& renderer,diva::UiColorOutput output,UINT scale) {
    const bool hdr10=output==diva::UiColorOutput::Hdr10;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=128*scale;desc.Height=16*scale;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=hdr10?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;desc.Usage=D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Texture2D> buffer;ComPtr<ID3D11RenderTargetView> target;
    Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&buffer)),"创建模拟 HDR 后缓冲失败");
    Check(SUCCEEDED(device->CreateRenderTargetView(buffer.Get(),nullptr,&target)),"创建模拟 HDR 绘制目标失败");
    const float bg[4]={hdr10?Pq(40):0.5f,hdr10?Pq(40):0.5f,hdr10?Pq(40):0.5f,1};
    context->ClearRenderTargetView(target.Get(),bg);const auto before=Read(device,context,buffer.Get());
    // 使用真正的图片 SRV，测试肤色与青色，不只检查 ImGui 顶点颜色。
    const unsigned char pixels[]={224,160,128,255,32,192,208,255};
    D3D11_TEXTURE2D_DESC image=desc;image.Width=2;image.Height=1;image.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    image.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA initial{pixels,8,8};
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
    Check(SUCCEEDED(device->CreateTexture2D(&image,&initial,&texture)),"创建测试图片失败");
    Check(SUCCEEDED(device->CreateShaderResourceView(texture.Get(),nullptr,&view)),"创建测试图片视图失败");
    auto& io=ImGui::GetIO();io.DisplaySize={128,16};io.DisplayFramebufferScale={float(scale),float(scale)};io.DeltaTime=1.0f/60;
    ImGui_ImplDX11_NewFrame();ImGui::NewFrame();auto* list=ImGui::GetBackgroundDrawList();
    list->AddRectFilled({0,0},{16,16},IM_COL32_WHITE);
    list->AddRectFilled({16,0},{32,16},IM_COL32(128,128,128,255));
    list->AddRectFilled({32,0},{48,16},IM_COL32(255,0,0,255));
    list->AddRectFilled({48,0},{64,16},IM_COL32(255,255,255,128));
    // 两色纹理固定采样各自 texel 中心，避免把 ImGui 线性采样的边界混色当作色彩转换误差。
    const auto textureId=static_cast<ImTextureID>(static_cast<void*>(view.Get()));
    list->AddImage(textureId,{64,0},{80,16},{0.25f,0.5f},{0.25f,0.5f});
    list->AddImage(textureId,{80,0},{96,16},{0.75f,0.5f},{0.75f,0.5f});
    ImGui::Render();
    // 设置非默认状态与 MRT，断言适配器结束后逐项恢复。
    auto* rt=target.Get();context->OMSetRenderTargets(1,&rt,nullptr);
    D3D11_VIEWPORT sentinel{3,2,12,8,0.2f,0.8f};context->RSSetViewports(1,&sentinel);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    auto* srv=view.Get();context->PSSetShaderResources(1,1,&srv);
    const float white=203;
    Check(SUCCEEDED(renderer.Render(device,context,buffer.Get(),target.Get(),ImGui::GetDrawData(),output,white)),"独立 HDR 合成失败");
    ComPtr<ID3D11RenderTargetView> restored;context->OMGetRenderTargets(1,&restored,nullptr);
    Check(restored.Get()==target.Get(),"未恢复原绘制目标");
    D3D11_VIEWPORT actual{};UINT count=1;context->RSGetViewports(&count,&actual);
    Check(std::memcmp(&sentinel,&actual,sizeof(actual))==0,"未恢复原视口");
    D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
    Check(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"未恢复原图元类型");
    ComPtr<ID3D11ShaderResourceView> restoredView;context->PSGetShaderResources(1,1,&restoredView);
    Check(restoredView.Get()==view.Get(),"未恢复其他 Mod 的纹理绑定");
    context->OMSetRenderTargets(0,nullptr,nullptr);
    const auto after=Read(device,context,buffer.Get());
    // 参数 x 为逻辑横坐标，采样中心避免边缘过滤误差。
    auto sample=[&](UINT x) {return after[8*scale*desc.Width+x*scale];};
    const float tolerance=hdr10?0.003f:0.007f;
    const float expectedWhite=hdr10?Pq(white):white/80;
    for(int c=0;c<3;++c) Near(sample(8)[c],expectedWhite,tolerance,"UI 白色未映射至 203 尼特");
    const float gray=Linear(128.0f/255);
    for(int c=0;c<3;++c) Near(sample(24)[c],hdr10?Pq(gray*white):gray*white/80,tolerance,"灰阶被重复伽马处理或过曝");
    const std::array<float,3> red2020{0.627404f,0.069097f,0.016391f};
    for(int c=0;c<3;++c) Near(sample(40)[c],hdr10?Pq(red2020[c]*white):(c==0?white/80:0),tolerance,"红色的 BT.709 至 BT.2020 转换错误");
    const float alpha=128.0f/255;
    for(int c=0;c<3;++c) Near(sample(56)[c],hdr10?Pq(40*(1-alpha)+white*alpha):0.5f*(1-alpha)+white/80*alpha,tolerance,"透明 UI 未在线性亮度空间合成");
    const float matrix[3][3]={{0.627404f,0.329283f,0.043313f},{0.069097f,0.919540f,0.011362f},{0.016391f,0.088013f,0.895595f}};
    for(int color=0;color<2;++color) {
        std::array<float,3> linear{};for(int c=0;c<3;++c) linear[c]=Linear(pixels[color*4+c]/255.0f);
        for(int c=0;c<3;++c) {
            float expected=linear[c]*white/80;
            if(hdr10) expected=Pq((matrix[c][0]*linear[0]+matrix[c][1]*linear[1]+matrix[c][2]*linear[2])*white);
            Near(sample(color==0?68:92)[c],expected,tolerance,"服装图片肤色或青色转换错误");
        }
    }
    // UI 没有覆盖的像素必须位值完全一致，不接受仅肉眼相似。
    for(UINT y=0;y<desc.Height;++y) for(UINT x=96*scale;x<desc.Width;++x)
        Check(before[y*desc.Width+x]==after[y*desc.Width+x],"透明区域改写了游戏画面");
    // 缺失模式、非法亮度和多采样在写目标之前拒绝。
    Check(FAILED(renderer.Render(device,context,buffer.Get(),target.Get(),ImGui::GetDrawData(),output,NAN)),"非法亮度未被拒绝");
    ID3D11ShaderResourceView* empty=nullptr;context->PSSetShaderResources(1,1,&empty);
}
// 测试入口不接收参数；WARP 离屏验证不能视为实际 Luma 或 HDR 显示器验收。
int main() {
    SetConsoleOutputCP(CP_UTF8);
    try {
        using diva::UiColorOutput;using diva::ResolveUiColorOutput;
        using diva::ParseUiColorProfile;using diva::ResolveUiWhite;
        Near(ResolveUiWhite(0,ParseUiColorProfile("[Luma]\nDisplayMode=0\nUIPaperWhite=400\n")).nits,80,0,"Luma SDR 模式被历史 HDR 白亮度覆盖");
        Near(ResolveUiWhite(0,ParseUiColorProfile("[Luma]\nDisplayMode=1\nUseOSReferenceWhiteLevel=0\nUIPaperWhite=270\n")).nits,270,0,"没有跟随实际 Luma HDR 配置");
        Near(ResolveUiWhite(0,ParseUiColorProfile("[Luma]\nDisplayMode=2\nUseOSReferenceWhiteLevel=0\nScenePaperWhite=180\nUIPaperWhite=270\n")).nits,180,0,"SDR 映射到 HDR 时未采用 Luma 场景白");
        auto systemWhite=ParseUiColorProfile("[Luma]\nDisplayMode=1\nUIPaperWhite=270\n");systemWhite.osWhite=240;
        Near(ResolveUiWhite(0,systemWhite).nits,240,0,"没有跟随 Luma 使用的系统白亮度");
        Near(ResolveUiWhite(150,systemWhite).nits,150,0,"玩家显式覆盖失效");
        Near(ResolveUiWhite(0,ParseUiColorProfile("[Other]\nDisplayMode=1\nUIPaperWhite=400\n")).nits,80,0,"误读其他插件配置");
        Near(ResolveUiWhite(0,ParseUiColorProfile("[Luma]\nDisplayMode=1\nUseOSReferenceWhiteLevel=0\nUIPaperWhite=nan\n")).nits,80,0,"无效白亮度未保守回退");
        Check(!ParseUiColorProfile("[Luma]\nDisplayMode=0\nDisplayMode=1\n").displayMode,"歧义显示模式未被拒绝");
        for(bool luma:{false,true}) {
            Check(ResolveUiColorOutput("auto",DXGI_FORMAT_R8G8B8A8_UNORM,luma)==UiColorOutput::Legacy,"普通八位 SDR 被误判");
            Check(ResolveUiColorOutput("sdr",DXGI_FORMAT_R10G10B10A2_UNORM,luma)==UiColorOutput::Legacy,"强制 SDR 回退无效");
        }
        Check(ResolveUiColorOutput("auto",DXGI_FORMAT_R10G10B10A2_UNORM,false)==UiColorOutput::Legacy,"未知十位 SDR 被误判为 HDR");
        Check(ResolveUiColorOutput("auto",DXGI_FORMAT_R16G16B16A16_FLOAT,false)==UiColorOutput::Legacy,"未知浮点输出被擅自转换");
        Check(ResolveUiColorOutput("auto",DXGI_FORMAT_R10G10B10A2_UNORM,true)==UiColorOutput::Hdr10,"已知 Luma 的 HDR10 未识别");
        Check(ResolveUiColorOutput("auto",DXGI_FORMAT_R16G16B16A16_FLOAT,true)==UiColorOutput::ScRgb,"已知 Luma 的 scRGB 未识别");
        Check(ResolveUiColorOutput("hdr10",DXGI_FORMAT_R10G10B10A2_UNORM,false)==UiColorOutput::Hdr10,"显式 HDR10 无效");
        Check(ResolveUiColorOutput("scrgb",DXGI_FORMAT_R16G16B16A16_FLOAT,false)==UiColorOutput::ScRgb,"显式 scRGB 无效");
        Check(ResolveUiColorOutput("hdr10",DXGI_FORMAT_R8G8B8A8_UNORM,true)==UiColorOutput::Legacy,"不匹配格式没有回退");
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"创建 WARP 设备失败");
        ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;ImGui::GetIO().LogFilename=nullptr;
        Check(ImGui_ImplDX11_Init(device.Get(),context.Get()),"测试 ImGui 后端初始化失败");
        diva::UiColorRenderer renderer;
        for(auto output:{UiColorOutput::Hdr10,UiColorOutput::ScRgb})
            for(float white:{80.0f,203.0f}) for(float opacity:{0.7f,0.97f,1.0f}) for(UINT scale:{1u,2u})
                for(const std::array<unsigned char,3> bg: {std::array<unsigned char,3>{255,255,255},{128,128,128},{240,80,32}})
                    CheckBackgroundCompatibility(device.Get(),context.Get(),output,white,opacity,scale,bg);
        for(auto output:{UiColorOutput::Hdr10,UiColorOutput::ScRgb}) CheckSdrReferenceRoundTrip(device.Get(),context.Get(),output);
        for(auto output:{UiColorOutput::Hdr10,UiColorOutput::ScRgb}) for(UINT scale:{1u,2u})
            CheckPixels(device.Get(),context.Get(),renderer,output,scale);
        renderer.Reset();CheckPixels(device.Get(),context.Get(),renderer,UiColorOutput::Hdr10,1);
        renderer.Reset();ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();context->ClearState();
        std::cout<<"UI 色彩适配通过：SDR 保守分流、图片亮部往返、HDR 背景对齐旧 SDR、图片与边缘像素保持、参考白、缩放和状态恢复。\n";
        std::cout<<"以上为 WARP 离屏像素读回，不代表 Luma 实机或 HDR 显示器验收。\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"UI 色彩验收失败："<<error.what()<<"\n";return 1;}
}
