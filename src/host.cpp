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
// 参数 key 为 Win32 虚拟键；仅查看当前物理状态，不使用全局竞争的低位事件标记。
static bool PhysicalKeyDown(int key) { return (GetAsyncKeyState(key)&0x8000)!=0; }
// 只记录输入隔离摘要，不写入查询内容或逐键记录。
std::string Host::DescribeInputState() const {
    return "面板="+std::to_string(view.open)+"，隔离="+std::to_string(captures.load())
        +"，待释放键="+std::to_string(std::count(swallowed.begin(),swallowed.end(),true))
        +"，待释放鼠标="+std::to_string(std::count(swallowedMouse.begin(),swallowedMouse.end(),true))
        +"，IME="+std::to_string(view.composing)+"/"+std::to_string(view.imeGuard)
        +"，释放保护="+std::to_string(GetTickCount64()<releaseUntil);
}
// 参数 reason 为调用点给出的清理原因；不修改其他 Mod 的窗口或原生输入链。
void Host::ResetInputState(const char* reason) {
    bool hadState=view.open || captures || releaseUntil || std::any_of(swallowed.begin(),swallowed.end(),[](bool value){ return value; })
        || std::any_of(swallowedMouse.begin(),swallowedMouse.end(),[](bool value){ return value; });
    view.open=false; view.composing=false; view.imeGuard=false;
    view.ClearActions(); view.listNavigation=true; view.horizontalNavigation=view.mode==SearchMode::Songs;
    releaseUntil=0; swallowed.fill(false); swallowedMouse.fill(false); captures=false;
    releasedKeys.fill(false); releasedMouse.fill(false);
    if(context) { ContextScope scope(context); ImGui::GetIO().ClearInputKeys(); }
    if(hadState && diagnostic) diagnostic(std::string("输入状态清理：")+reason+"；"+DescribeInputState());
}
// 关闭后允许补收丢失的释放；参数 now/keyDown 为时刻及可选测试探针。
void Host::UpdateInputCapture(uint64_t now,bool (*keyDown)(int)) {
    size_t recovered=0;
    if(!view.open && now>=releaseUntil) {
        if(!keyDown) keyDown=PhysicalKeyDown;
        for(size_t i=0;i<swallowed.size();++i) if(swallowed[i] && !keyDown((int)i)) { swallowed[i]=false; releasedKeys[i]=true; ++recovered; }
        const int mouseKeys[]={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON};
        for(size_t i=0;i<swallowedMouse.size();++i) if(swallowedMouse[i] && !keyDown(mouseKeys[i])) { swallowedMouse[i]=false; releasedMouse[i]=true; ++recovered; }
    }
    bool capture=view.open || now<releaseUntil || std::any_of(swallowed.begin(),swallowed.end(),[](bool value){ return value; })
        || std::any_of(swallowedMouse.begin(),swallowedMouse.end(),[](bool value){ return value; });
    bool changed=captures.exchange(capture)!=capture;
    if(recovered && diagnostic) diagnostic("输入释放恢复：已松开但未收到释放消息的项目="+std::to_string(recovered)+"；"+DescribeInputState());
    if(changed && diagnostic) diagnostic("输入隔离切换："+DescribeInputState());
}
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
        ResetInputState("图形设备或窗口重绑");
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
    // 新一轮按下必须完整转发，不能因上一次缺失释放而吞掉新一轮释放。
    if(key && down) releasedKeys[w&255]=false;
    if(message==WM_LBUTTONDOWN) releasedMouse[0]=false;
    if(message==WM_RBUTTONDOWN) releasedMouse[1]=false;
    if(message==WM_MBUTTONDOWN) releasedMouse[2]=false;
    if(key && down && w==(WPARAM)view.shortcut && (GetKeyState(VK_CONTROL)&0x8000) && !(l&(1LL<<30))) {
        if(!view.composing) {
            view.open=!view.open; view.focus=view.open; view.ClearActions();
            view.listNavigation=true; view.horizontalNavigation=view.mode==SearchMode::Songs; releaseUntil=GetTickCount64()+120;
            captures=true;
            if(view.open) view.OnOpened();
            if(view.open && view.onOpen) view.onOpen();
            ImGui::GetIO().ClearInputKeys();
        }
        swallowed[w&255]=true; UpdateInputCapture(GetTickCount64());
        if(diagnostic) diagnostic("搜索快捷键："+DescribeInputState());
        return 0;
    }
    if(message==WM_KILLFOCUS) {
        ResetInputState("窗口失焦");
    }
    // 截图覆盖层可能只发送顶层失活，不能只依赖编辑焦点的 WM_KILLFOCUS。
    if(message==WM_ACTIVATEAPP && !w) ResetInputState("应用失活");
    if(message==WM_ACTIVATE && LOWORD(w)==WA_INACTIVE) ResetInputState("窗口失活");
    if(view.open) {
        bool popup=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup);
        bool navigate=view.listNavigation && !popup;
        if(message==WM_LBUTTONDOWN) swallowedMouse[0]=true;
        if(message==WM_RBUTTONDOWN) swallowedMouse[1]=true;
        if(message==WM_MBUTTONDOWN) swallowedMouse[2]=true;
        if(message==WM_LBUTTONUP) swallowedMouse[0]=releasedMouse[0]=false;
        if(message==WM_RBUTTONUP) swallowedMouse[1]=releasedMouse[1]=false;
        if(message==WM_MBUTTONUP) swallowedMouse[2]=releasedMouse[2]=false;
        if(key && !down) releasedKeys[w&255]=false;
        if(message==WM_IME_STARTCOMPOSITION) { view.composing=true; view.imeGuard=true; }
        if(message==WM_IME_ENDCOMPOSITION) { view.composing=false; view.imeGuard=true; }
        if(key && down) {
            if(w!=VK_PROCESSKEY) swallowed[w&255]=true;
            bool ime=view.composing || view.imeGuard;
            if(!ime && !(l&(1LL<<30))) {
                if(navigate && w==VK_RETURN) view.requestEnter=true;
                if(!popup && w==VK_ESCAPE) view.requestEscape=true;
            }
            if(!ime && navigate) {
                int repeats=std::max(1,(int)(l&0xFFFF));
                if(view.mode==SearchMode::Songs) {
                    if(w==VK_UP) view.move-=repeats;
                    if(w==VK_DOWN) view.move+=repeats;
                    if(w==VK_LEFT) view.difficultyMove-=repeats;
                    if(w==VK_RIGHT) view.difficultyMove+=repeats;
                } else {
                    if(w==VK_UP) view.itemMoves.emplace_back(CustomizationNavigation::Up,repeats);
                    if(w==VK_DOWN) view.itemMoves.emplace_back(CustomizationNavigation::Down,repeats);
                    if(view.horizontalNavigation) {
                        if(w==VK_LEFT) view.itemMoves.emplace_back(CustomizationNavigation::Left,repeats);
                        if(w==VK_RIGHT) view.itemMoves.emplace_back(CustomizationNavigation::Right,repeats);
                        if(w==VK_HOME) view.itemMoves.emplace_back(CustomizationNavigation::Home,1);
                        if(w==VK_END) view.itemMoves.emplace_back(CustomizationNavigation::End,1);
                        if(w==VK_PRIOR) view.itemMoves.emplace_back(CustomizationNavigation::PageUp,repeats);
                        if(w==VK_NEXT) view.itemMoves.emplace_back(CustomizationNavigation::PageDown,repeats);
                    }
                    if(view.horizontalNavigation && w==VK_SPACE && !(l&(1LL<<30))) view.requestEnter=true;
                }
            }
            // IME 组合键不交给 ImGui 导航，但系统仍需接收以取消组词和显示候选。
            if(ime && (w==VK_RETURN || w==VK_ESCAPE || w==VK_SPACE || w==VK_UP || w==VK_DOWN || w==VK_LEFT || w==VK_RIGHT)) return DefWindowProcW(hwnd,message,w,l);
        }
        if(key && !down && !view.composing && (w==VK_RETURN || w==VK_ESCAPE || w==VK_SPACE || w==VK_PROCESSKEY)) view.imeGuard=false;
        // 列表快捷键只进入 View 的动作队列，避免同时触发 ImGui 光标或焦点导航。
        if(key && !view.composing && !view.imeGuard
            && ((navigate && (w==VK_UP || w==VK_DOWN || w==VK_RETURN
                || (view.horizontalNavigation && (w==VK_LEFT || w==VK_RIGHT
                    || (view.mode!=SearchMode::Songs && (w==VK_HOME || w==VK_END || w==VK_PRIOR || w==VK_NEXT || w==VK_SPACE))))))
                || (!popup && w==VK_ESCAPE))) {
            if(!down) swallowed[w&255]=false;
            return 0;
        }
        ImGui_ImplWin32_WndProcHandler(hwnd,message,w,l);
        if(message>=WM_IME_STARTCOMPOSITION && message<=WM_IME_COMPOSITION) return DefWindowProcW(hwnd,message,w,l);
        if(message>=WM_IME_SETCONTEXT && message<=WM_IME_KEYUP) return DefWindowProcW(hwnd,message,w,l);
        if(key || message==WM_CHAR || message==WM_SYSCHAR || message==WM_INPUT || (message>=WM_MOUSEFIRST && message<=WM_MOUSELAST)) {
            if(key && !down) swallowed[w&255]=false;
            return message==WM_INPUT?DefWindowProcW(hwnd,message,w,l):0;
        }
    } else if(message==WM_LBUTTONUP && (swallowedMouse[0] || releasedMouse[0])) { swallowedMouse[0]=releasedMouse[0]=false; return 0; }
    else if(message==WM_RBUTTONUP && (swallowedMouse[1] || releasedMouse[1])) { swallowedMouse[1]=releasedMouse[1]=false; return 0; }
    else if(message==WM_MBUTTONUP && (swallowedMouse[2] || releasedMouse[2])) { swallowedMouse[2]=releasedMouse[2]=false; return 0; }
    else if(key && !down && (swallowed[w&255] || releasedKeys[w&255])) { swallowed[w&255]=releasedKeys[w&255]=false; return 0; }
    else if(message==WM_CHAR && GetTickCount64()<releaseUntil) return 0;
    return previous?CallWindowProcW(previous,hwnd,message,w,l):DefWindowProcW(hwnd,message,w,l);
}
// 绘制时短暂持有后缓冲，不妨碍 DML 的 ResizeBuffers；参数 chain 为交换链。
void Host::Frame(IDXGISwapChain* chain) {
    std::lock_guard lock(mutex); if(!initialized) return;
    if(!preview && !previous) {
        auto current=(WNDPROC)GetWindowLongPtrW(window,GWLP_WNDPROC);
        if(current==SearchWndProc) { ResetInputState("窗口输入链状态异常");view.status="窗口输入链状态异常，已停止搜索。";return; }
        SetLastError(0);
        auto old=SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)SearchWndProc);
        if(!old) { ResetInputState("窗口输入隔离安装失败");view.status="窗口输入隔离安装失败，已停止搜索。";return; }
        previous=(WNDPROC)old;
    }
    ContextScope scope(context); view.Poll();
    bool wasOpen=view.open;
    // 关闭后持续隔离仍按住的按键和鼠标，不能仅靠固定延迟放行，避免 Enter 长按开始演奏。
    UpdateInputCapture(GetTickCount64());
    if(!view.open && !preview) return;
    if(view.fontDirty) {
        ImGui_ImplDX11_InvalidateDeviceObjects(); ImGui::GetIO().Fonts->Clear(); SetupStyle(view.fontSize); view.fontDirty=false;
    }
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    view.Draw(); ImGui::Render();
    if(wasOpen && !view.open) {
        view.ClearActions(); releaseUntil=GetTickCount64()+120; ImGui::GetIO().ClearInputKeys();
        if(diagnostic) diagnostic("搜索面板关闭："+DescribeInputState());
    }
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
    ResetInputState("宿主关闭");
    // 仅受控的预览或测试退出调用此处；等待副本任务结束，防止工作线程跨过 CRT 静态资源销毁。
    // 游戏帧和 DllMain 不调用 Shutdown，不能把这些等待挪到游戏生命周期回调。
    if(view.pending.valid()) view.pending.wait();
    if(view.searching.valid()) view.searching.wait();
    if(view.customization.pending.valid()) view.customization.pending.wait();
    if(view.customization.searching.valid()) view.customization.searching.wait();
    ContextScope scope(context);
    if(initialized) { ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); initialized=false; }
    if(previous && (WNDPROC)GetWindowLongPtrW(window,GWLP_WNDPROC)==SearchWndProc) SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)previous);
    // 独立预览进程退出后由操作系统回收上下文，避免恢复已销毁的上下文。
}
}
