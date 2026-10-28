#include <Windows.h>
#include <iostream>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
// 在独立验证进程装载发行 DLL 并核对 DML 导出；参数 argc/argv 为 DLL 路径。
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if(argc!=2) { std::cout<<"请提供 DLL 路径。\n"; return 2; }
    auto dll=LoadLibraryW(argv[1]);
    if(!dll) { std::cout<<"DLL 装载失败，错误码："<<GetLastError()<<"\n"; return 1; }
    for(auto name:{"Init","PostInit","D3DInit","OnFrame","OnResize"}) if(!GetProcAddress(dll,name)) { std::cout<<"缺少加载器入口。\n"; return 1; }
    ((void(*)())GetProcAddress(dll,"Init"))();
    ((void(*)())GetProcAddress(dll,"PostInit"))();
    // 用隐藏的原生 DX11 窗口执行实际 DLL 图形回调，无需加载游戏。
    WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"DivaDllValidation"; RegisterClassW(&wc);
    auto window=CreateWindowW(wc.lpszClassName,L"DLL 原生验证",WS_OVERLAPPEDWINDOW,0,0,1280,800,nullptr,nullptr,wc.hInstance,nullptr);
    DXGI_SWAP_CHAIN_DESC desc{}; desc.BufferCount=1; desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow=window; desc.SampleDesc.Count=1; desc.Windowed=TRUE;
    Microsoft::WRL::ComPtr<ID3D11Device> device; Microsoft::WRL::ComPtr<ID3D11DeviceContext> context; Microsoft::WRL::ComPtr<IDXGISwapChain> chain;
    if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context))) { std::cout<<"DX11 验证设备创建失败。\n"; return 1; }
    ((void(*)(IDXGISwapChain*,ID3D11Device*,ID3D11DeviceContext*))GetProcAddress(dll,"D3DInit"))(chain.Get(),device.Get(),context.Get());
    auto onFrame=(void(*)(IDXGISwapChain*))GetProcAddress(dll,"OnFrame");onFrame(chain.Get());
    BYTE keys[256]{}; GetKeyboardState(keys); keys[VK_CONTROL]=0x80; SetKeyboardState(keys); SendMessageW(window,WM_KEYDOWN,'F',0); keys[VK_CONTROL]=0; SetKeyboardState(keys);
    for(int i=0;i<30;++i) onFrame(chain.Get());
    context->OMSetRenderTargets(0,nullptr,nullptr);
    if(FAILED(chain->ResizeBuffers(0,1024,768,DXGI_FORMAT_UNKNOWN,0))) { std::cout<<"DLL 持有后缓冲，尺寸调整失败。\n"; return 1; }
    ((void(*)(IDXGISwapChain*))GetProcAddress(dll,"OnResize"))(chain.Get()); onFrame(chain.Get());
    SendMessageW(window,WM_IME_STARTCOMPOSITION,0,0); SendMessageW(window,WM_KEYDOWN,VK_RETURN,0); SendMessageW(window,WM_IME_ENDCOMPOSITION,0,0); SendMessageW(window,WM_KEYUP,VK_RETURN,0); onFrame(chain.Get());
    std::cout<<"DLL 已装载，五个 DML 导出齐全；DX11 初始化、33 帧回调、尺寸恢复和 IME 消息执行完成。验证宿主没有访问游戏内存。\n";
    // 与实际 DML 一致，不支持运行途中 FreeLibrary；进程结束统一回收。
    return 0;
}
