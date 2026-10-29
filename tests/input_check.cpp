#include "host.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <wrl/client.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

// 原生输入协议检查；参数 value/text 为条件与中文原因。
static void Check(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
static std::array<bool,256> physicalKeys{};
// 参数 key 为模拟物理状态的虚拟键；使测试不依赖运行机器恰好按住哪些键。
static bool KeyDown(int key) { return physicalKeys.at(key); }
static int forwarded=0;
// 参数 hwnd/message/w/l 为转发消息，记录宿主是否保留了前一 Mod 的处理链。
static LRESULT CALLBACK PreviousProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    (void)hwnd; (void)message; (void)w; (void)l; ++forwarded; return 77;
}
// 用 Win32 消息和真实 DX11 验证共用宿主；参数 argc/argv 为项目目录。
int wmain(int argc,wchar_t** argv) {
    // 测试进程崩溃时交由退出码和调试器取证，不弹出阻塞用户桌面的系统错误框。
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    SetConsoleOutputCP(CP_UTF8);
    try {
        bool messagesOnly=argc==3 && std::wstring(argv[1])==L"--messages-only";
        Check(argc==2 || messagesOnly,"缺少项目目录");
        auto root=argv[messagesOnly?2:1]; auto& host=diva::App();host.preview=true;host.Init(root,root);
        HWND window=nullptr;
        Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;Microsoft::WRL::ComPtr<IDXGISwapChain> chain;
        if(messagesOnly) {
            // 无图形设备时仍使用真实 Win32 消息后端，单独验收输入生命周期。
            auto oldContext=ImGui::GetCurrentContext(); ImGui::SetCurrentContext((ImGuiContext*)host.context);
            Check(ImGui_ImplWin32_Init(nullptr),"Win32 消息后端初始化失败"); host.initialized=true;
            ImGui::SetCurrentContext(oldContext);
        } else {
        WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"DivaInputValidation";RegisterClassW(&wc);
        window=CreateWindowW(wc.lpszClassName,L"原生输入验证",WS_OVERLAPPEDWINDOW,0,0,1280,800,nullptr,nullptr,wc.hInstance,nullptr);
        DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=1;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=window;desc.SampleDesc.Count=1;desc.Windowed=TRUE;
        Check(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context)),"DX11 设备初始化失败");
        Check(host.Attach(chain.Get(),device.Get(),context.Get()),"宿主图形初始化失败");
        }
        host.previous=PreviousProc;
        std::vector<std::string> diagnostics;
        host.diagnostic=[&diagnostics](const std::string& text) { diagnostics.push_back(text); };
        host.view.open=true;host.view.focus=true;
        host.Message(window,WM_IME_STARTCOMPOSITION,0,0); Check(host.view.composing,"未跟踪组合开始");
        host.Message(window,WM_KEYDOWN,VK_RETURN,0); Check(!host.view.requestEnter,"组合 Enter 触发定位");
        host.Message(window,WM_KEYDOWN,VK_ESCAPE,0); Check(!host.view.requestEscape && host.view.open,"组合 Esc 关闭面板");
        host.Message(window,WM_IME_ENDCOMPOSITION,0,0);host.Message(window,WM_KEYDOWN,VK_RETURN,0);Check(!host.view.requestEnter,"结束组合的 Enter 触发定位");
        host.Message(window,WM_KEYUP,VK_RETURN,0);host.Message(window,WM_KEYDOWN,VK_RETURN,0);Check(host.view.requestEnter,"非组合 Enter 未提交");host.view.requestEnter=false;
        host.Message(window,WM_KEYUP,VK_RETURN,0);
        host.Message(window,WM_KEYUP,VK_ESCAPE,0);
        host.Message(window,WM_KEYDOWN,VK_RETURN,0);host.view.requestEnter=false;host.view.open=false;host.releaseUntil=0;
        physicalKeys[VK_RETURN]=true;host.UpdateInputCapture(1000,KeyDown);Check(host.captures,"关闭面板后长按 Enter 穿透");
        host.UpdateInputCapture(60000,KeyDown);Check(host.captures,"长按 Enter 被固定超时放行");
        host.Message(window,WM_KEYUP,VK_RETURN,0);physicalKeys[VK_RETURN]=false;
        host.UpdateInputCapture(60001,KeyDown);Check(!host.captures,"关闭后的 Enter 释放仍留下全局隔离");
        host.view.open=true;host.Message(window,WM_LBUTTONDOWN,0,0);host.view.open=false;
        physicalKeys[VK_LBUTTON]=true;host.UpdateInputCapture(1000,KeyDown);Check(host.captures,"关闭面板后鼠标长按穿透");
        Check(host.Message(window,WM_LBUTTONUP,0,0)==0,"关闭后的鼠标释放未消费");physicalKeys[VK_LBUTTON]=false;
        host.UpdateInputCapture(1001,KeyDown);Check(!host.captures,"鼠标释放后仍隔离游戏输入");host.view.open=true;
        host.Message(window,WM_KEYDOWN,VK_DOWN,1);host.Message(window,WM_KEYDOWN,VK_DOWN,(1LL<<30)|3);
        Check(host.view.move==4,"连续方向消息或 repeat count 丢失");host.view.move=0;
        host.Message(window,WM_KEYDOWN,VK_RIGHT,1);host.Message(window,WM_KEYDOWN,VK_RIGHT,(1LL<<30)|2);
        Check(host.view.difficultyMove==3,"左右难度动作没有累加");host.view.difficultyMove=0;
        host.view.listNavigation=false;
        host.Message(window,WM_KEYDOWN,VK_DOWN,1);host.Message(window,WM_KEYDOWN,VK_RIGHT,1);
        host.Message(window,WM_KEYDOWN,VK_RETURN,1);
        Check(host.view.move==0 && host.view.difficultyMove==0 && !host.view.requestEnter && !host.view.requestEscape,"控件导航误触发列表动作");
        host.Message(window,WM_KEYDOWN,VK_ESCAPE,1);Check(host.view.requestEscape,"无弹窗时控件焦点阻止 Esc 关闭搜索");host.view.requestEscape=false;
        host.view.listNavigation=true;
        host.view.SetMode(diva::SearchMode::Modules);host.view.listNavigation=true;host.view.horizontalNavigation=false;
        host.Message(window,WM_KEYDOWN,VK_LEFT,1);host.Message(window,WM_KEYDOWN,VK_HOME,1);
        Check(host.view.itemMoves.empty() && !host.view.difficultyMove,"服装文本左右或 Home 被当作格子导航");
        host.Message(window,WM_KEYDOWN,VK_DOWN,1);host.view.horizontalNavigation=true;
        host.Message(window,WM_KEYDOWN,VK_RIGHT,(1LL<<30)|2);host.Message(window,WM_KEYDOWN,VK_PRIOR,1);
        Check(host.view.itemMoves.size()==3 && host.view.itemMoves[0].first==diva::CustomizationNavigation::Down
            && host.view.itemMoves[1].first==diva::CustomizationNavigation::Right && host.view.itemMoves[1].second==2
            && host.view.itemMoves[2].first==diva::CustomizationNavigation::PageUp,"格子动作顺序、重复或翻页路由错误");
        host.view.SetMode(diva::SearchMode::Songs);Check(host.view.itemMoves.empty(),"切回歌曲残留定制导航动作");
        Check(host.Message(window,WM_CHAR,L'中',0)==0,"中文字符未消费");
        host.Message(window,WM_KEYDOWN,'A',0);host.view.open=false;Check(host.Message(window,WM_KEYUP,'A',0)==0 && !host.swallowed['A'],"关闭后按键释放未隔离");
        // 模拟 IME 改写 key-up 的键值，或后续窗口 hook 消费了释放：A 的记录仍在但物理键已松开。
        host.ResetInputState("测试隔离场景切换");host.view.open=true;
        host.Message(window,WM_KEYDOWN,'A',0);host.Message(window,WM_KEYUP,VK_PROCESSKEY,0);
        host.view.open=false;host.releaseUntil=2000;host.UpdateInputCapture(1999,KeyDown);
        Check(host.swallowed['A'] && host.captures,"120 毫秒释放保护未保留");
        host.UpdateInputCapture(2000,KeyDown);Check(!host.swallowed['A'] && !host.captures,"丢失或 IME 改写的 key-up 导致永久隔离");
        int delayedBefore=forwarded;Check(host.Message(window,WM_KEYUP,'A',0)==0 && forwarded==delayedBefore,"物理松开后迟到的释放穿透旧输入链");
        host.view.open=true;host.Message(window,WM_KEYDOWN,'A',0);host.view.open=false;host.UpdateInputCapture(2000,KeyDown);
        Check(host.Message(window,WM_KEYDOWN,'A',0)==77 && host.Message(window,WM_KEYUP,'A',0)==77,"缺失释放的旧记录破坏新一轮按下和释放");
        Check(!diagnostics.empty() && std::any_of(diagnostics.begin(),diagnostics.end(),[](const std::string& text) { return text.find("输入释放恢复")!=std::string::npos; }),"残留恢复未留下诊断");
        int before=forwarded;Check(host.Message(window,WM_KEYDOWN,'B',0)==77 && forwarded==before+1,"搜索关闭后未保留前一 Mod 的输入链");
        host.view.open=true;host.Message(window,WM_RBUTTONDOWN,0,0);host.view.open=false;
        host.UpdateInputCapture(2001,KeyDown);Check(!host.swallowedMouse[1] && !host.captures,"丢失鼠标释放导致永久隔离");
        Check(host.Message(window,WM_RBUTTONUP,0,0)==0,"物理松开后迟到的鼠标释放穿透");
        host.view.open=true;host.view.move=2;host.view.difficultyMove=-1;host.releaseUntil=GetTickCount64()+60000;
        host.Message(window,WM_KILLFOCUS,0,0);host.UpdateInputCapture(GetTickCount64(),KeyDown);
        Check(!host.view.open && !host.view.composing && !host.captures && !host.releaseUntil && !host.view.move && !host.view.difficultyMove,"失焦后重新隔离或保留导航动作");
        // 截图覆盖层可能截走释放，且仅发失活消息而没有 WM_KILLFOCUS。
        for(auto message:{WM_ACTIVATEAPP,WM_ACTIVATE}) {
            host.view.open=true;host.view.requestEnter=true;host.view.move=1;
            host.swallowed[VK_CONTROL]=true;host.swallowed['A']=true;host.swallowedMouse[0]=true;
            host.releaseUntil=GetTickCount64()+60000;host.captures=true;
            int beforeDeactivate=forwarded;
            Check(host.Message(window,message,0,0)==77 && forwarded==beforeDeactivate+1,"失活通知没有保留前窗口链");
            host.UpdateInputCapture(GetTickCount64(),KeyDown);
            Check(!host.view.open && !host.captures && !host.releaseUntil && !host.view.requestEnter
                && !host.view.move && !host.swallowed[VK_CONTROL] && !host.swallowed['A'] && !host.swallowedMouse[0],"仅收到失活通知仍残留输入隔离");
        }
        host.view.open=true;host.UpdateInputCapture(GetTickCount64(),KeyDown);
        host.Message(window,WM_ACTIVATEAPP,TRUE,0);host.Message(window,WM_ACTIVATE,WA_ACTIVE,0);
        Check(host.view.open && host.captures,"恢复激活错误关闭仍打开的搜索");
        host.ResetInputState("截图失活回归完成");
        if(messagesOnly) {
            auto oldContext=ImGui::GetCurrentContext(); ImGui::SetCurrentContext((ImGuiContext*)host.context);
            ImGui_ImplWin32_Shutdown(); host.initialized=false; ImGui::SetCurrentContext(oldContext);
            // 用尚未完成的查询覆盖快速退出场景，确认返回前不会遗留仍在使用 CRT 的工作线程。
            std::atomic_bool backgroundFinished=false;
            host.view.searching=std::async(std::launch::async,[&backgroundFinished] {
                Sleep(20); backgroundFinished=true; return diva::View::SearchReply{};
            });
            host.Shutdown(); host.diagnostic={};
            Check(backgroundFinished,"受控退出没有等待后台查询完成");
            std::cout<<"Win32 消息协议通过：IME、按住保护、缺失/迟到释放、窗口链、歌曲/格子路由、模式切换与失焦；未执行 DX11 验收。\n";
            return 0;
        }
        host.view.open=true;host.Frame(chain.Get());host.Frame(chain.Get());
        // 在真实 ImGui 弹窗上下文发送 Esc，优先交给弹窗，不关闭整个面板。
        auto oldContext=ImGui::GetCurrentContext();ImGui::SetCurrentContext((ImGuiContext*)host.context);
        ImGui::NewFrame();ImGui::Begin("输入弹窗测试");ImGui::OpenPopup("下拉测试");
        Check(ImGui::BeginPopup("下拉测试"),"测试弹窗未打开");
        host.Message(window,WM_KEYDOWN,VK_ESCAPE,1);Check(!host.view.requestEscape && host.view.open,"弹窗 Esc 误关闭整个搜索");
        ImGui::CloseCurrentPopup();ImGui::EndPopup();ImGui::End();ImGui::Render();
        ImGui::SetCurrentContext(oldContext);host.Message(window,WM_KEYUP,VK_ESCAPE,1);
        context->OMSetRenderTargets(0,nullptr,nullptr);Check(SUCCEEDED(chain->ResizeBuffers(0,1024,768,DXGI_FORMAT_UNKNOWN,0)),"后缓冲未释放");host.Frame(chain.Get());
        host.view.fontSize=20;host.view.fontDirty=true;host.Frame(chain.Get());Check(!host.view.fontDirty,"字体未在安全帧重建");
        host.Shutdown();host.diagnostic={};DestroyWindow(window);std::cout<<"原生输入协议通过：组合 Enter/Esc、长按释放屏障、丢失释放恢复、窗口链转发、导航累加、中文消息、失焦、DX11 调整和字体重建。\n";return 0;
    } catch(const std::exception& error) { std::cout<<"原生输入验证失败："<<error.what()<<"\n";return 1; }
}
