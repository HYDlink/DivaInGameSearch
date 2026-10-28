#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#include <iostream>
#include <filesystem>
using Microsoft::WRL::ComPtr;
struct WindowSearch { DWORD pid;HWND window=nullptr; };
// 只按目标进程查找可见主窗口；参数 window/data 为枚举窗口与目标条件。
static BOOL CALLBACK FindWindowForProcess(HWND window,LPARAM data) {
    auto& search=*(WindowSearch*)data;DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid==search.pid && IsWindowVisible(window) && GetWindow(window,GW_OWNER)==nullptr) { search.window=window;return FALSE; }return TRUE;
}
// 只读截取游戏客户区；参数 argc/argv 为目标进程编号与 PNG 输出路径。
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(argc!=3) { std::cout<<"用法：NativeCapture.exe 游戏进程编号 输出图片路径\n";return 2; }
    WindowSearch search{(DWORD)wcstoul(argv[1],nullptr,10)};EnumWindows(FindWindowForProcess,(LPARAM)&search);
    if(!search.window) { std::cout<<"没有找到目标游戏窗口。\n";return 1; }
    RECT rect{};GetClientRect(search.window,&rect);int width=rect.right,height=rect.bottom;
    if(width<=0 || height<=0 || width>16384 || height>16384) { std::cout<<"游戏窗口尺寸异常。\n";return 1; }
    HDC screen=GetDC(nullptr);HDC memory=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,width,height);
    auto previous=SelectObject(memory,bitmap);bool ok=false;
    DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
    if(foregroundPid==search.pid) {
        POINT origin{};ClientToScreen(search.window,&origin);
        ok=BitBlt(memory,0,0,width,height,screen,origin.x,origin.y,SRCCOPY|CAPTUREBLT)!=0;
        std::cout<<"截图方式：GDI 屏幕读取，仅截取前台游戏客户区。\n";
    } else {
        ok=PrintWindow(search.window,memory,2)!=0;
        std::cout<<"游戏不在前台，使用 PrintWindow 只读取目标窗口；画面是否有效须检查图片。\n";
    }
    SelectObject(memory,previous);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    std::vector<BYTE> pixels((size_t)width*height*4);
    if(ok) ok=GetDIBits(memory,bitmap,0,height,pixels.data(),&info,DIB_RGB_COLORS)!=0;
    DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);
    if(!ok) { std::cout<<"原生截图调用失败。\n";return 1; }
    for(size_t i=3;i<pixels.size();i+=4) pixels[i]=255;
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(SUCCEEDED(hr)) hr=factory->CreateStream(&stream);
    if(SUCCEEDED(hr)) hr=stream->InitializeFromFilename(argv[2],GENERIC_WRITE);
    if(SUCCEEDED(hr)) hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
    if(SUCCEEDED(hr)) hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    if(SUCCEEDED(hr)) hr=encoder->CreateNewFrame(&frame,nullptr);
    if(SUCCEEDED(hr)) hr=frame->Initialize(nullptr);
    if(SUCCEEDED(hr)) hr=frame->SetSize(width,height);
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
    if(SUCCEEDED(hr)) hr=frame->SetPixelFormat(&format);
    if(SUCCEEDED(hr)) hr=frame->WritePixels(height,width*4,(UINT)pixels.size(),pixels.data());
    if(SUCCEEDED(hr)) hr=frame->Commit();if(SUCCEEDED(hr)) hr=encoder->Commit();
    if(FAILED(hr)) { std::cout<<"PNG 图片写入失败，错误码："<<std::hex<<hr<<"\n";return 1; }
    std::cout<<"游戏截图已保存，尺寸："<<width<<"×"<<height<<"。\n";return 0;
}
