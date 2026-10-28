#include "host.h"
#include "game.h"
#include <fstream>

namespace {
// 延迟创建定位状态，避免 DllMain 中初始化业务。
diva::Selector& Selection() { static diva::Selector* selector=new diva::Selector; return *selector; }
// 把中文诊断写到本 Mod 目录；参数 text 为记录内容。
void Log(const std::string& text) {
    SYSTEMTIME time{};GetLocalTime(&time);
    char prefix[48]{};snprintf(prefix,sizeof(prefix),"%04d-%02d-%02d %02d:%02d:%02d.%03d ",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
    std::ofstream out(diva::App().view.home/"search-diagnostics.log",std::ios::app);out<<prefix<<text<<"\n";
}
}
extern "C" {
// DML 初始化入口：只在加载器回调阶段解析路径并启动后台索引。
__declspec(dllexport) void Init() {
    HMODULE self=nullptr; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)&Init,&self);
    wchar_t path[32768]{}; GetModuleFileNameW(self,path,32768);
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768);
    auto& host=diva::App(); host.Init(diva::fs::path(path).parent_path(),diva::fs::path(executable).parent_path());
    // 打开面板时沿用游戏当前的难度和风格。
    host.view.onOpen=[] {
        auto snapshot=diva::Game().Read(); auto& view=diva::App().view;
        Log("打开搜索："+diva::DescribeSnapshot(snapshot));
        if(!diva::Game().inputReady) {
            view.open=false;Log(diva::Game().diagnostic);
            MessageBoxW(diva::App().window,diva::Wide("无法安全隔离当前游戏输入，搜索面板未打开。\n"+diva::Game().diagnostic).c_str(),L"DIVA 游戏内搜索",MB_OK|MB_ICONINFORMATION);
            return;
        }
        if(snapshot.supported) {
            if(snapshot.visibleDifficulty>=0 && snapshot.visibleDifficulty<5) view.currentDifficulty=snapshot.visibleDifficulty;
            if(snapshot.style>=0 && snapshot.style<3) { view.query.style=snapshot.style; view.dirty=true; }
        }
    };
    // 参数 request/available 为界面验证后的动作与谱面可用性。
    host.view.submit=[](diva::Request request,bool available) {
        auto snapshot=diva::Game().Read();Log("定位请求：目标="+std::to_string(request.pvid)+"，难度="+std::to_string(request.difficulty)+"；"+diva::DescribeSnapshot(snapshot));
        auto& selector=Selection(); selector.Submit(request,available,snapshot,GetTickCount64());
        diva::App().view.status=selector.message; Log(selector.message);
    };
    Log("已初始化原生搜索 1.0.0，默认 Ctrl+F；曲库后台读取，无外部服务。");
}
// 所有 Mod 完成 Init 后再安装链式输入隔离。
__declspec(dllexport) void PostInit() { diva::Game().Initialize(); Log(diva::Game().diagnostic); }
// DML 图形初始化；参数 chain/device/context 为加载器共享的 DX11 对象。
__declspec(dllexport) void D3DInit(IDXGISwapChain* chain,ID3D11Device* device,ID3D11DeviceContext* context) { diva::App().Attach(chain,device,context); }
// DML 每帧回调；参数 chain 为当前交换链，不进行阻塞等待或磁盘扫描。
__declspec(dllexport) void OnFrame(IDXGISwapChain* chain) {
    auto& host=diva::App(); std::lock_guard lock(host.mutex);
    if(host.view.open && !Selection().Busy()) diva::Game().Read();
    auto& selection=Selection(); bool busy=selection.Busy();auto previousMessage=selection.message;selection.Tick(diva::Game(),GetTickCount64());
    if(busy && previousMessage!=selection.message) Log("定位阶段："+selection.message+"；"+diva::DescribeSnapshot(diva::Game().Read()));
    if(busy) {
        host.view.status=selection.message;
        if(!selection.Busy()) {
            Log(selection.message);
            if(selection.succeeded && host.view.closeOnSuccess) { host.view.open=false; host.releaseUntil=GetTickCount64()+120; }
        }
    }
    host.Frame(chain);
}
// DML 尺寸回调；参数 chain 为已调整大小的交换链，本 Mod 不持有后缓冲引用。
__declspec(dllexport) void OnResize(IDXGISwapChain* chain) { (void)chain; }
}
