#include "host.h"
#include <imgui.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>

// 原生输入协议检查；参数 value/text 为条件与中文原因。
static void Check(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
// 用 Win32 消息和真实 DX11 验证共用宿主；参数 argc/argv 为项目目录。
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    try {
        Check(argc==2,"缺少项目目录"); auto& host=diva::App();host.preview=true;host.Init(argv[1],argv[1]);
        WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"DivaInputValidation";RegisterClassW(&wc);
        auto window=CreateWindowW(wc.lpszClassName,L"原生输入验证",WS_OVERLAPPEDWINDOW,0,0,1280,800,nullptr,nullptr,wc.hInstance,nullptr);
        DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=1;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=window;desc.SampleDesc.Count=1;desc.Windowed=TRUE;
        Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;Microsoft::WRL::ComPtr<IDXGISwapChain> chain;
        Check(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context)),"DX11 设备初始化失败");
        Check(host.Attach(chain.Get(),device.Get(),context.Get()),"宿主图形初始化失败");
        host.view.open=true;host.view.focus=true;
        host.Message(window,WM_IME_STARTCOMPOSITION,0,0); Check(host.view.composing,"未跟踪组合开始");
        host.Message(window,WM_KEYDOWN,VK_RETURN,0); Check(!host.view.requestEnter,"组合 Enter 触发定位");
        host.Message(window,WM_KEYDOWN,VK_ESCAPE,0); Check(!host.view.requestEscape && host.view.open,"组合 Esc 关闭面板");
        host.Message(window,WM_IME_ENDCOMPOSITION,0,0);host.Message(window,WM_KEYDOWN,VK_RETURN,0);Check(!host.view.requestEnter,"结束组合的 Enter 触发定位");
        host.Message(window,WM_KEYUP,VK_RETURN,0);host.Message(window,WM_KEYDOWN,VK_RETURN,0);Check(host.view.requestEnter,"非组合 Enter 未提交");host.view.requestEnter=false;
        host.Message(window,WM_KEYUP,VK_RETURN,0);
        host.Message(window,WM_KEYDOWN,VK_RETURN,0);host.view.requestEnter=false;host.view.open=false;host.releaseUntil=0;
        host.Frame(chain.Get());Check(host.captures,"关闭面板后长按 Enter 穿透");host.Message(window,WM_KEYUP,VK_RETURN,0);
        host.view.open=true;host.Message(window,WM_LBUTTONDOWN,0,0);host.view.open=false;
        host.Frame(chain.Get());Check(host.captures,"关闭面板后鼠标长按穿透");Check(host.Message(window,WM_LBUTTONUP,0,0)==0,"关闭后的鼠标释放未消费");host.view.open=true;
        host.Message(window,WM_KEYDOWN,VK_DOWN,0);Check(host.view.move==1,"方向键未改变高亮");host.view.move=0;
        Check(host.Message(window,WM_CHAR,L'中',0)==0,"中文字符未消费");
        host.Message(window,WM_KEYDOWN,'A',0);host.view.open=false;Check(host.Message(window,WM_KEYUP,'A',0)==0 && !host.swallowed['A'],"关闭后按键释放未隔离");
        host.view.open=true;host.Message(window,WM_KILLFOCUS,0,0);Check(!host.view.open && !host.view.composing && !host.captures,"失焦没有清理输入状态");
        host.view.open=true;host.Frame(chain.Get());host.Frame(chain.Get());
        context->OMSetRenderTargets(0,nullptr,nullptr);Check(SUCCEEDED(chain->ResizeBuffers(0,1024,768,DXGI_FORMAT_UNKNOWN,0)),"后缓冲未释放");host.Frame(chain.Get());
        host.view.fontSize=20;host.view.fontDirty=true;host.Frame(chain.Get());Check(!host.view.fontDirty,"字体未在安全帧重建");
        host.Shutdown();DestroyWindow(window);std::cout<<"原生输入协议通过：组合 Enter/Esc、提交释放屏障、方向键、中文消息、关闭释放、失焦、DX11 调整和字体重建。\n";return 0;
    } catch(const std::exception& error) { std::cout<<"原生输入验证失败："<<error.what()<<"\n";return 1; }
}
