#pragma once
#include "ui.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <atomic>
#include <mutex>

namespace diva {
struct Host {
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
    std::array<bool,256> swallowed{};
    std::array<bool,3> swallowedMouse{};
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
