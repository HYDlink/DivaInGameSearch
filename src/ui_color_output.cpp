#include "ui_color_output.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <d3dcompiler.h>
#include <cmath>

namespace diva {
using Microsoft::WRL::ComPtr;
// 参数 mode/format/knownLuma 为显式配置、真实格式和已知插件证据；未知组合保留原 SDR 路径。
UiColorOutput ResolveUiColorOutput(std::string_view mode,DXGI_FORMAT format,bool knownLuma) {
    if(mode=="sdr") return UiColorOutput::Legacy;
    if(mode=="auto" && !knownLuma) return UiColorOutput::Legacy;
    if((mode=="hdr10" || mode=="auto") && format==DXGI_FORMAT_R10G10B10A2_UNORM) return UiColorOutput::Hdr10;
    if((mode=="scrgb" || mode=="auto") && format==DXGI_FORMAT_R16G16B16A16_FLOAT) return UiColorOutput::ScRgb;
    return UiColorOutput::Legacy;
}
// 官方工程 TargetName 为 Luma-Project Diva Mega Mix，扩展名通常为 .addon；重命名构建由配置显式指定。
bool HasKnownDivaLuma() {
    return GetModuleHandleW(L"Luma-Project Diva Mega Mix.addon")!=nullptr
        || GetModuleHandleW(L"Luma-Project Diva Mega Mix.addon64")!=nullptr;
}
// 参数 output 为已决策模式，只用于开发诊断。
const char* UiColorOutputName(UiColorOutput output) {
    switch(output) {
    case UiColorOutput::Hdr10:return "HDR10 色彩适配";
    case UiColorOutput::ScRgb:return "线性 scRGB 色彩适配";
    default:return "原有 SDR 直绘";
    }
}
namespace {
// 全屏合成使用无顶点缓冲三角形；参数 id 为系统提供的顶点序号。
constexpr char shader[]=R"(
cbuffer Settings : register(b0) { uint outputMode; float whiteNits; float2 padding; float4 backgroundBounds; };
Texture2D<float4> uiLayer : register(t0);
Texture2D<float4> gameLayer : register(t1);
// 参数 id 为全屏三角形的顶点序号。
float4 VS(uint id : SV_VertexID) : SV_Position {
    return float4(id==2 ? 3.0 : -1.0,id==1 ? 3.0 : -1.0,0,1);
}
// 参数 v 为 sRGB 编码的直通颜色，返回线性 BT.709。
float3 DecodeSrgb(float3 v) {
    return lerp(v/12.92,pow(max((v+0.055)/1.055,0),2.4),step(0.04045,v));
}
// 参数 v 为相对界面白的线性颜色；扩展符号和超白范围，不截断游戏 HDR 高光或宽色域分量。
float3 EncodeSrgbExtended(float3 v) {
    float3 a=abs(v);
    return sign(v)*lerp(a*12.92,1.055*pow(a,1.0/2.4)-0.055,step(0.0031308,a));
}
// 参数 v 为扩展 sRGB 混合结果；保留负值与超白，供背景兼容路径还原线性亮度。
float3 DecodeSrgbExtended(float3 v) { return sign(v)*DecodeSrgb(abs(v)); }
// 参数 v 为 PQ 编码的 BT.2020，返回绝对亮度，单位尼特。
float3 DecodePq(float3 v) {
    float3 p=pow(max(v,0),1.0/(2523.0/32.0));
    return 10000.0*pow(max(p-3424.0/4096.0,0)/max(2413.0/128.0-2392.0/128.0*p,1e-6),1.0/(2610.0/16384.0));
}
// 参数 nits 为线性 BT.2020 的绝对亮度，返回 PQ 编码。
float3 EncodePq(float3 nits) {
    float3 p=pow(saturate(nits/10000.0),2610.0/16384.0);
    return pow((3424.0/4096.0+2413.0/128.0*p)/(1.0+2392.0/128.0*p),2523.0/32.0);
}
// 参数 pos 为输出像素位置；先还原 SDR 图层预乘色，再在线性空间做透明合成。
float4 PS(float4 pos : SV_Position) : SV_Target {
    int3 coord=int3(int2(pos.xy),0);
    float4 ui=uiLayer.Load(coord);
    // 完全透明处不写回，保留游戏原始位值，避免整屏色彩往返误差。
    if(ui.a<=0) discard;
    float4 bg=gameLayer.Load(coord);
    float3 color=DecodeSrgb(saturate(ui.rgb/max(ui.a,1e-6)));
    float3 mixed;
    const float3x3 to2020=float3x3(0.627404,0.329283,0.043313,
                                  0.069097,0.919540,0.011362,
                                  0.016391,0.088013,0.895595);
    // 仅对主窗口内仍有游戏透底的像素复现旧 SDR 混合；不透明图片、文字和卡片不进入此分支。
    bool matchBackground=ui.a<1 && all(pos.xy>=backgroundBounds.xy) && all(pos.xy<backgroundBounds.zw);
    if(matchBackground) {
        float3 game709;
        if(outputMode==1) {
            const float3x3 to709=float3x3(1.660491,-0.587641,-0.072850,
                                         -0.124550,1.132900,-0.008349,
                                         -0.018151,-0.100579,1.118730);
            game709=mul(to709,DecodePq(bg.rgb))/whiteNits;
        } else game709=bg.rgb*(80.0/whiteNits);
        // ui.rgb 已在 SDR 图层预乘 alpha；只按旧公式补入游戏透底，不再重复乘 alpha。
        float3 legacy=DecodeSrgbExtended(ui.rgb+EncodeSrgbExtended(game709)*(1-ui.a));
        mixed=outputMode==1?EncodePq(mul(to2020,legacy)*whiteNits):legacy*(whiteNits/80.0);
    } else if(outputMode==1) {
        mixed=EncodePq(lerp(DecodePq(bg.rgb),mul(to2020,color)*whiteNits,ui.a));
    } else {
        // scRGB 的 1.0 表示 80 尼特；游戏超白和负色值保留在线性合成中。
        mixed=lerp(bg.rgb,color*(whiteNits/80.0),ui.a);
    }
    return float4(mixed,ui.a+bg.a*(1-ui.a));
}
)";

// 保存单个着色阶段的动态链接实例；参数 Shader 为对应 DX11 着色器接口。
template<class Shader> struct ShaderState {
    ComPtr<Shader> shader;
    ID3D11ClassInstance* instances[256]{};
    UINT count=256;
    // 释放 GetShader 返回的实例引用。
    ~ShaderState() { for(UINT i=0;i<count;++i) if(instances[i]) instances[i]->Release(); }
};
// 合成器仅改写这些图形状态；析构时完整恢复，避免影响其他 Mod 的下一次绘制。
struct GraphicsState {
    ID3D11DeviceContext* context;
    ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ComPtr<ID3D11DepthStencilView> depthView;
    ShaderState<ID3D11VertexShader> vs;
    ShaderState<ID3D11PixelShader> ps;
    ShaderState<ID3D11GeometryShader> gs;
    ShaderState<ID3D11HullShader> hs;
    ShaderState<ID3D11DomainShader> ds;
    ID3D11ShaderResourceView* resources[2]{};
    ComPtr<ID3D11Buffer> constant;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11InputLayout> layout;
    FLOAT factor[4]{};
    UINT mask=0,stencil=0,viewportCount=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    // 参数 value 为当前即时上下文，读取状态只增加临时 COM 引用。
    explicit GraphicsState(ID3D11DeviceContext* value):context(value) {
        context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,targets,&depthView);
        context->VSGetShader(&vs.shader,vs.instances,&vs.count);context->PSGetShader(&ps.shader,ps.instances,&ps.count);
        context->GSGetShader(&gs.shader,gs.instances,&gs.count);context->HSGetShader(&hs.shader,hs.instances,&hs.count);
        context->DSGetShader(&ds.shader,ds.instances,&ds.count);
        context->PSGetShaderResources(0,2,resources);context->PSGetConstantBuffers(0,1,&constant);
        context->OMGetBlendState(&blend,factor,&mask);context->OMGetDepthStencilState(&depth,&stencil);
        context->RSGetState(&raster);context->RSGetViewports(&viewportCount,viewports);
        context->IAGetInputLayout(&layout);context->IAGetPrimitiveTopology(&topology);
    }
    // 先解绑本层 SRV，再恢复调用者输出和各着色阶段。
    ~GraphicsState() {
        ID3D11ShaderResourceView* empty[2]{};context->PSSetShaderResources(0,2,empty);
        context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,targets,depthView.Get());
        context->VSSetShader(vs.shader.Get(),vs.instances,vs.count);context->PSSetShader(ps.shader.Get(),ps.instances,ps.count);
        context->GSSetShader(gs.shader.Get(),gs.instances,gs.count);context->HSSetShader(hs.shader.Get(),hs.instances,hs.count);
        context->DSSetShader(ds.shader.Get(),ds.instances,ds.count);
        context->PSSetShaderResources(0,2,resources);auto* cb=constant.Get();context->PSSetConstantBuffers(0,1,&cb);
        context->OMSetBlendState(blend.Get(),factor,mask);context->OMSetDepthStencilState(depth.Get(),stencil);
        context->RSSetState(raster.Get());context->RSSetViewports(viewportCount,viewports);
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(topology);
        for(auto* target:targets) if(target) target->Release();
        for(auto* resource:resources) if(resource) resource->Release();
    }
};
}
// 只释放本 Mod 的中间纹理和着色器，不持有或改写游戏后缓冲。
void UiColorRenderer::Reset() {
    layer.Reset();background.Reset();layerTarget.Reset();layerView.Reset();backgroundView.Reset();
    vertex.Reset();pixel.Reset();constants.Reset();blend.Reset();depth.Reset();raster.Reset();owner.Reset();
    width=height=0;format=DXGI_FORMAT_UNKNOWN;
}
// 参数 device/desc 为真实设备与单采样后缓冲；任何失败都清理半成品并允许后续重试。
HRESULT UiColorRenderer::Prepare(ID3D11Device* device,const D3D11_TEXTURE2D_DESC& desc) {
    if(owner.Get()!=device) Reset();
    HRESULT hr=S_OK;
    if(!vertex) {
        ComPtr<ID3DBlob> vs,ps,error;
        hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"VS","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&error);
        if(SUCCEEDED(hr)) hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PS","ps_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&error);
        if(SUCCEEDED(hr)) hr=device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex);
        if(SUCCEEDED(hr)) hr=device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel);
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=32;cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(SUCCEEDED(hr)) hr=device->CreateBuffer(&cb,nullptr,&constants);
        D3D11_BLEND_DESC bd{};bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        if(SUCCEEDED(hr)) hr=device->CreateBlendState(&bd,&blend);
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=FALSE;dd.DepthFunc=D3D11_COMPARISON_ALWAYS;
        if(SUCCEEDED(hr)) hr=device->CreateDepthStencilState(&dd,&depth);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        if(SUCCEEDED(hr)) hr=device->CreateRasterizerState(&rd,&raster);
        if(FAILED(hr)) {Reset();return hr;}
        owner=device;
    }
    if(width==desc.Width && height==desc.Height && format==desc.Format && layer && backgroundView) return S_OK;
    layer.Reset();background.Reset();layerTarget.Reset();layerView.Reset();backgroundView.Reset();width=height=0;
    D3D11_TEXTURE2D_DESC ld{};ld.Width=desc.Width;ld.Height=desc.Height;ld.MipLevels=1;ld.ArraySize=1;
    ld.Format=DXGI_FORMAT_R8G8B8A8_UNORM;ld.SampleDesc.Count=1;ld.Usage=D3D11_USAGE_DEFAULT;
    ld.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    hr=device->CreateTexture2D(&ld,nullptr,&layer);
    if(SUCCEEDED(hr)) hr=device->CreateRenderTargetView(layer.Get(),nullptr,&layerTarget);
    if(SUCCEEDED(hr)) hr=device->CreateShaderResourceView(layer.Get(),nullptr,&layerView);
    ld.Format=desc.Format;ld.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    if(SUCCEEDED(hr)) hr=device->CreateTexture2D(&ld,nullptr,&background);
    if(SUCCEEDED(hr)) hr=device->CreateShaderResourceView(background.Get(),nullptr,&backgroundView);
    if(FAILED(hr)) {Reset();return hr;}
    width=desc.Width;height=desc.Height;format=desc.Format;return S_OK;
}
// 参数含当前帧所有借用对象；先绘制透明 SDR 图层，再在线性空间与游戏副本合成。
HRESULT UiColorRenderer::Render(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* buffer,
                              ID3D11RenderTargetView* target,ImDrawData* data,UiColorOutput output,float whiteNits,
                              const ImVec4* backgroundBounds) {
    if(!device || !context || !buffer || !target || !data || output==UiColorOutput::Legacy) return E_INVALIDARG;
    if(!std::isfinite(whiteNits) || whiteNits<80 || whiteNits>10000) return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc{};buffer->GetDesc(&desc);
    if(desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1) return DXGI_ERROR_UNSUPPORTED;
    if((output==UiColorOutput::Hdr10 && desc.Format!=DXGI_FORMAT_R10G10B10A2_UNORM)
        || (output==UiColorOutput::ScRgb && desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)) return DXGI_ERROR_UNSUPPORTED;
    if(data->DisplaySize.x<=0 || data->DisplaySize.y<=0 || data->TotalVtxCount==0) return S_OK;
    HRESULT hr=Prepare(device,desc);if(FAILED(hr)) return hr;
    GraphicsState saved(context);
    context->OMSetRenderTargets(0,nullptr,nullptr);
    context->CopyResource(background.Get(),buffer);
    const float clear[4]{};context->ClearRenderTargetView(layerTarget.Get(),clear);
    auto* rt=layerTarget.Get();context->OMSetRenderTargets(1,&rt,nullptr);
    ImGui_ImplDX11_RenderDrawData(data);
    context->OMSetRenderTargets(1,&target,nullptr);
    ImVec4 bounds{};
    if(backgroundBounds && backgroundBounds->z>backgroundBounds->x && backgroundBounds->w>backgroundBounds->y) {
        bounds={(backgroundBounds->x-data->DisplayPos.x)*data->FramebufferScale.x,
                (backgroundBounds->y-data->DisplayPos.y)*data->FramebufferScale.y,
                (backgroundBounds->z-data->DisplayPos.x)*data->FramebufferScale.x,
                (backgroundBounds->w-data->DisplayPos.y)*data->FramebufferScale.y};
    }
    const struct {UINT output;float white;float padding[2];ImVec4 bounds;} settings{output==UiColorOutput::Hdr10?1u:2u,whiteNits,{0,0},bounds};
    context->UpdateSubresource(constants.Get(),0,nullptr,&settings,0,0);
    auto* cb=constants.Get();context->PSSetConstantBuffers(0,1,&cb);
    ID3D11ShaderResourceView* views[]={layerView.Get(),backgroundView.Get()};context->PSSetShaderResources(0,2,views);
    context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);
    context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
    const float factor[4]{};context->OMSetBlendState(blend.Get(),factor,0xffffffff);
    context->OMSetDepthStencilState(depth.Get(),0);context->RSSetState(raster.Get());
    D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};context->RSSetViewports(1,&viewport);
    context->Draw(3,0);return S_OK;
}
}
