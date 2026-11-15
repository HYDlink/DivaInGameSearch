#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <string_view>

struct ImDrawData;
struct ImVec4;

namespace diva {
enum class UiColorOutput { Legacy, Hdr10, ScRgb };

// 参数 mode/format/knownLuma 为配置、实际后缓冲格式和已加载的已知 Luma 插件标记。
// 十位格式本身不代表 HDR；自动模式仅对已知 Luma 的匹配格式启用适配。
UiColorOutput ResolveUiColorOutput(std::string_view mode,DXGI_FORMAT format,bool knownLuma);
// 只检查官方构建命名的 DIVA Luma 模块，不扫描磁盘或修改其他插件。
bool HasKnownDivaLuma();
// 参数 output 为适配模式，返回中文诊断名称。
const char* UiColorOutputName(UiColorOutput output);

// 独立 SDR 图层与 HDR 合成器；不持有交换链后缓冲，不替换 ImGui 后端。
class UiColorRenderer {
public:
    // 释放本适配层的设备资源；设备重绑或回到 SDR 时调用。
    void Reset();
    // 参数 device/context/buffer/target/data 为当前帧对象，output/whiteNits 为输出模式和 UI 白亮度。
    // backgroundBounds 为可选主窗口逻辑边界；仅其半透明透底按旧 SDR 观感合成，空值保留原路径。
    // 调用方只对 HDR 调用；失败前不会向游戏目标提交不完整结果。
    HRESULT Render(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* buffer,
                   ID3D11RenderTargetView* target,ImDrawData* data,UiColorOutput output,float whiteNits,
                   const ImVec4* backgroundBounds=nullptr);
private:
    // 参数 device/desc 为当前设备及后缓冲描述；尺寸或格式改变时重建本层离屏纹理。
    HRESULT Prepare(ID3D11Device* device,const D3D11_TEXTURE2D_DESC& desc);
    Microsoft::WRL::ComPtr<ID3D11Device> owner;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> layer,background;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> layerTarget;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> layerView,backgroundView;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;
    UINT width=0,height=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
};
}
