#include "host.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <imm.h>
#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
namespace diva {
// 上下文作用域在回调完成后恢复其他 Mod 的上下文。
struct ContextScope {
    ImGuiContext* previous;
    // 参数 context 为本 Mod 上下文。
    explicit ContextScope(void* context):previous(ImGui::GetCurrentContext()) { ImGui::SetCurrentContext((ImGuiContext*)context); }
    // 恢复回调进入前的上下文。
    ~ContextScope() { ImGui::SetCurrentContext(previous); }
};
// 只在加载器已离开装载锁后创建宿主。
Host& App() { static Host* host=new Host; return *host; }
// 初始化曲库、收藏和 ImGui；参数 home/game 为工作资源与游戏目录。
void Host::Init(const fs::path& home,const fs::path& game) {
    std::lock_guard lock(mutex);
    if(context) return;
    view.home=home; view.game=game; view.ReloadStyle();
    auto old=ImGui::GetCurrentContext(); context=ImGui::CreateContext();
    SetupStyle(view.fontSize); view.fontDirty=false; ImGui::SetCurrentContext(old); view.Refresh();
}
// 绑定加载器的图形对象；参数 chain/newDevice/newContext 为交换链及设备。
bool Host::Attach(IDXGISwapChain* chain,ID3D11Device* newDevice,ID3D11DeviceContext* newContext) {
    std::lock_guard lock(mutex); if(!context || !chain) return false;
    ContextScope scope(context); DXGI_SWAP_CHAIN_DESC desc{}; if(FAILED(chain->GetDesc(&desc))) return false;
    if(initialized) {
        if(device==newDevice && window==desc.OutputWindow) return true;
        ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); initialized=false;
        if(previous && (WNDPROC)GetWindowLongPtrW(window,GWLP_WNDPROC)==SearchWndProc) {
            SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)previous);previous=nullptr;
        }
    }
    if(window!=desc.OutputWindow) previous=nullptr;
    window=desc.OutputWindow; device=newDevice; deviceContext=newContext;
    if(!ImGui_ImplWin32_Init(window)) return false;
    if(!ImGui_ImplDX11_Init(device,deviceContext)) { ImGui_ImplWin32_Shutdown(); return false; }
    // 等所有 Mod 的 D3DInit 都完成后，在首个 OnFrame 安装窗口链头，避免歌词窗口先消费快捷键。
    initialized=true; return true;
}
// 转发窗口消息；参数 hwnd/message/w/l 为系统消息。
LRESULT CALLBACK SearchWndProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) { return App().Message(hwnd,message,w,l); }
// 截获完整输入序列，同时保留系统 IME 默认处理；参数 hwnd/message/w/l 为原始消息。
LRESULT Host::Message(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    std::lock_guard lock(mutex);
    // 关闭面板仍消费已捕获按键的释放，防止游戏收到孤立 key-up。
    bool key=message==WM_KEYDOWN || message==WM_SYSKEYDOWN || message==WM_KEYUP || message==WM_SYSKEYUP;
    bool down=message==WM_KEYDOWN || message==WM_SYSKEYDOWN;
    if(!context || !initialized) return previous?CallWindowProcW(previous,hwnd,message,w,l):DefWindowProcW(hwnd,message,w,l);
    ContextScope scope(context);
    if(key && down && w==(WPARAM)view.shortcut && (GetKeyState(VK_CONTROL)&0x8000) && !(l&(1LL<<30))) {
        if(!view.composing) {
            view.open=!view.open; view.focus=view.open; view.requestEnter=view.requestEscape=false;
            captures=view.open; releaseUntil=GetTickCount64()+120;
            if(view.open && view.onOpen) view.onOpen();
            captures=view.open;
            ImGui::GetIO().ClearInputKeys();
        }
        swallowed[w&255]=true; return 0;
    }
    if(message==WM_KILLFOCUS) {
        view.composing=false; view.imeGuard=false; view.open=false; captures=false;
        view.requestEnter=view.requestEscape=false; swallowed.fill(false); swallowedMouse.fill(false);
        ImGui::GetIO().ClearInputKeys();
    }
    if(view.open) {
        if(message==WM_LBUTTONDOWN) swallowedMouse[0]=true;
        if(message==WM_RBUTTONDOWN) swallowedMouse[1]=true;
        if(message==WM_MBUTTONDOWN) swallowedMouse[2]=true;
        if(message==WM_LBUTTONUP) swallowedMouse[0]=false;
        if(message==WM_RBUTTONUP) swallowedMouse[1]=false;
        if(message==WM_MBUTTONUP) swallowedMouse[2]=false;
        if(message==WM_IME_STARTCOMPOSITION) { view.composing=true; view.imeGuard=true; }
        if(message==WM_IME_ENDCOMPOSITION) { view.composing=false; view.imeGuard=true; }
        if(key && down) {
            if(w!=VK_PROCESSKEY) swallowed[w&255]=true;
            bool ime=view.composing || view.imeGuard;
            if(!ime && !(l&(1LL<<30))) {
                if(w==VK_RETURN) view.requestEnter=true;
                if(w==VK_ESCAPE) view.requestEscape=true;
            }
            if(!ime && w==VK_UP) view.move=-1;
            if(!ime && w==VK_DOWN) view.move=1;
            // IME 组合键不交给 ImGui 导航，但系统仍需接收以取消组词和显示候选。
            if(ime && (w==VK_RETURN || w==VK_ESCAPE || w==VK_SPACE || w==VK_UP || w==VK_DOWN || w==VK_LEFT || w==VK_RIGHT)) return DefWindowProcW(hwnd,message,w,l);
        }
        if(key && !down && !view.composing && (w==VK_RETURN || w==VK_ESCAPE || w==VK_SPACE || w==VK_PROCESSKEY)) view.imeGuard=false;
        ImGui_ImplWin32_WndProcHandler(hwnd,message,w,l);
        if(message>=WM_IME_STARTCOMPOSITION && message<=WM_IME_COMPOSITION) return DefWindowProcW(hwnd,message,w,l);
        if(message>=WM_IME_SETCONTEXT && message<=WM_IME_KEYUP) return DefWindowProcW(hwnd,message,w,l);
        if(key || message==WM_CHAR || message==WM_SYSCHAR || message==WM_INPUT || (message>=WM_MOUSEFIRST && message<=WM_MOUSELAST)) {
            if(key && !down) swallowed[w&255]=false;
            return message==WM_INPUT?DefWindowProcW(hwnd,message,w,l):0;
        }
    } else if(message==WM_LBUTTONUP && swallowedMouse[0]) { swallowedMouse[0]=false; return 0; }
    else if(message==WM_RBUTTONUP && swallowedMouse[1]) { swallowedMouse[1]=false; return 0; }
    else if(message==WM_MBUTTONUP && swallowedMouse[2]) { swallowedMouse[2]=false; return 0; }
    else if(key && !down && swallowed[w&255]) { swallowed[w&255]=false; return 0; }
    else if(message==WM_CHAR && GetTickCount64()<releaseUntil) return 0;
    return previous?CallWindowProcW(previous,hwnd,message,w,l):DefWindowProcW(hwnd,message,w,l);
}
// 绘制时短暂持有后缓冲，不妨碍 DML 的 ResizeBuffers；参数 chain 为交换链。
void Host::Frame(IDXGISwapChain* chain) {
    std::lock_guard lock(mutex); if(!initialized) return;
    if(!preview && !previous) {
        auto current=(WNDPROC)GetWindowLongPtrW(window,GWLP_WNDPROC);
        if(current==SearchWndProc) { view.open=false;view.status="窗口输入链状态异常，已停止搜索。";return; }
        SetLastError(0);
        auto old=SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)SearchWndProc);
        if(!old) { view.open=false;view.status="窗口输入隔离安装失败，已停止搜索。";return; }
        previous=(WNDPROC)old;
    }
    ContextScope scope(context); view.Poll();
    bool wasOpen=view.open;
    // 关闭后持续隔离仍按住的按键和鼠标，不能仅靠固定延迟放行，避免 Enter 长按开始演奏。
    captures=view.open || GetTickCount64()<releaseUntil || std::any_of(swallowed.begin(),swallowed.end(),[](bool value){ return value; }) || std::any_of(swallowedMouse.begin(),swallowedMouse.end(),[](bool value){ return value; });
    if(!view.open && !preview) return;
    if(view.fontDirty) {
        ImGui_ImplDX11_InvalidateDeviceObjects(); ImGui::GetIO().Fonts->Clear(); SetupStyle(view.fontSize); view.fontDirty=false;
    }
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    view.Draw(); ImGui::Render();
    if(wasOpen && !view.open) { releaseUntil=GetTickCount64()+120; ImGui::GetIO().ClearInputKeys(); }
    ID3D11Texture2D* buffer=nullptr; ID3D11RenderTargetView* target=nullptr;
    if(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer)))) {
        device->CreateRenderTargetView(buffer,nullptr,&target); buffer->Release();
    }
    if(target) {
        ID3D11RenderTargetView* previousTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{}; ID3D11DepthStencilView* depth=nullptr;
        deviceContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,previousTargets,&depth);
        deviceContext->OMSetRenderTargets(1,&target,nullptr); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        deviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,previousTargets,depth);
        for(auto rt:previousTargets) if(rt) rt->Release(); if(depth) depth->Release(); target->Release();
    }
}
// 仅在宿主控制的正常关闭流程清理。
void Host::Shutdown() {
    std::lock_guard lock(mutex); if(!context) return;
    ContextScope scope(context);
    if(initialized) { ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); initialized=false; }
    if(previous && (WNDPROC)GetWindowLongPtrW(window,GWLP_WNDPROC)==SearchWndProc) SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)previous);
    // 独立预览进程退出后由操作系统回收上下文，避免恢复已销毁的上下文。
}
}
