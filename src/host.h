#pragma once
#include "ui.h"
#include "module_assets.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <atomic>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace diva {
struct Host {
    using ModuleAssetsBuild = std::pair<std::shared_ptr<ModuleAssets>,std::vector<std::string>>;
    struct PreviewTextureEntry {
        ID3D11ShaderResourceView* view=nullptr;
        int width=0,height=0;
        size_t bytes=0;
        uint64_t lastUsedFrame=0;
    };
    // 各 Mod 使用独立 ImGui 上下文；回调结束前恢复调用者上下文。
    View view;
    void* context=nullptr;
    HWND window=nullptr;
    WNDPROC previous=nullptr;
    ID3D11Device* device=nullptr;
    ID3D11DeviceContext* deviceContext=nullptr;
    std::recursive_mutex mutex;
    std::atomic<bool> captures{false};
    bool initialized=false,preview=false;
    uint64_t releaseUntil=0;
    std::shared_ptr<ModuleAssets> moduleAssets;
    std::future<ModuleAssetsBuild> moduleAssetsBuilding;
    std::vector<std::future<void>> moduleAssetsRetirements;
    std::vector<std::shared_ptr<ModuleAssets>> moduleAssetsRetiredPending;
    std::map<CustomizationKey,PreviewTextureEntry> modulePreviewTextures;
    size_t modulePreviewBytes=0;
    uint64_t observedModuleCatalogGeneration=UINT64_MAX;
    uint64_t moduleAssetsBuildingGeneration=0,moduleAssetsActiveGeneration=UINT64_MAX,moduleAssetsFailedGeneration=UINT64_MAX;
    uint64_t modulePreviewFrame=0;
    // 图片样式独立于歌曲风格，变更后清理当前设备的旧图片缓存。
    int modulePreviewStyle=0;
    std::array<bool,256> swallowed{};
    std::array<bool,3> swallowedMouse{};
    // 物理状态先确认松开时，继续消费稍后到达的释放；新按下会取消旧记录。
    std::array<bool,256> releasedKeys{};
    std::array<bool,3> releasedMouse{};
    // 参数 text 为不包含输入文字的状态摘要；游戏宿主负责写入诊断日志。
    std::function<void(const std::string& text)> diagnostic;
    // 汇总当前隔离原因，便于区分面板、释放保护和残留按键。
    std::string DescribeInputState() const;
    // 关闭面板并清空隔离；参数 reason 为失焦、重绑或失败等清理原因。
    void ResetInputState(const char* reason);
    // 参数 now 为单调毫秒，keyDown 可注入物理按键状态以复现丢失释放消息。
    void UpdateInputCapture(uint64_t now,bool (*keyDown)(int)=nullptr);
    // 根据服装目录代数异步构建资源索引，并在当前帧接入完成的快照。
    void SyncModuleAssets();
    // 参数 assets 为旧目录或过时代数的资源对象；后台停止并销毁，避免帧线程等待。
    void RetireModuleAssets(std::shared_ptr<ModuleAssets> assets);
    // 推进资源回收任务；参数无，仅在正常帧中启动与回收有限任务。
    void PumpModuleAssetRetirements();
    // 释放本 Mod 创建的 DX11 预览纹理；调用时必须不处于 ImGui 绘制提交中。
    void ClearPreviewTextures();
    // 参数 key/pixels/width/height 为服装键和 RGBA 缓冲及尺寸；返回本 Mod 创建的 DX11 SRV。
    ImTextureID UploadPreviewPixels(const CustomizationKey& key,const unsigned char* pixels,int width,int height);
    // 初始化本 Mod 的数据和上下文；参数 home/game 为资源和游戏目录。
    void Init(const fs::path& home,const fs::path& game);
    // 绑定已有 DX11 会话；参数 chain/device/context 为加载器提供对象。
    bool Attach(IDXGISwapChain* chain,ID3D11Device* device,ID3D11DeviceContext* context);
    // 每帧绘制并恢复原有图形状态；参数 chain 为当前交换链。
    void Frame(IDXGISwapChain* chain);
    // 处理本窗口输入；参数 hwnd/message/w/l 为原始 Win32 消息。
    LRESULT Message(HWND hwnd,UINT message,WPARAM w,LPARAM l);
    // 正常退出预览时清理资源；游戏进程退出不在 DllMain 等待后台任务。
    void Shutdown();
};
// 获取进程内唯一的本 Mod 宿主，避免装载期业务初始化。
Host& App();
// 把窗口消息转交独立宿主；参数 hwnd/message/w/l 为 Win32 消息。
LRESULT CALLBACK SearchWndProc(HWND hwnd,UINT message,WPARAM w,LPARAM l);
}
