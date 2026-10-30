#include "host.h"
#include "game.h"
#include "module_locator.h"
#include <fstream>
#include <atomic>

namespace {
// 延迟创建定位状态，避免 DllMain 中初始化业务。
diva::Selector& Selection() { static diva::Selector* selector=new diva::Selector; return *selector; }
std::atomic<uint64_t> lastModuleLocateSerial{0};
std::atomic<uint64_t> lastModuleLocateUiGeneration{0};
uint64_t reportedModuleSerial=0;
std::string reportedModuleMessage;
// 把中文诊断写到本 Mod 目录；参数 text 为记录内容。
void Log(const std::string& text) {
    static std::mutex mutex; std::lock_guard lock(mutex);
    SYSTEMTIME time{};GetLocalTime(&time);
    char prefix[48]{};snprintf(prefix,sizeof(prefix),"%04d-%02d-%02d %02d:%02d:%02d.%03d ",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
    std::ofstream out(diva::App().view.home/"search-diagnostics.log",std::ios::app);out<<prefix<<text<<"\n";out.flush();
    if(!out.good()) OutputDebugStringW(diva::Wide("DIVA 搜索诊断写入失败："+text+"\n").c_str());
}
}
extern "C" {
// DML 初始化入口：只在加载器回调阶段解析路径并启动后台索引。
__declspec(dllexport) void Init() {
    HMODULE self=nullptr; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)&Init,&self);
    wchar_t path[32768]{}; GetModuleFileNameW(self,path,32768);
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768);
    auto& host=diva::App(); host.Init(diva::fs::path(path).parent_path(),diva::fs::path(executable).parent_path());
    host.diagnostic=Log;
    // 条件或卡片选择改变时只发布取消代数，原生操作始终留在模型帧处理。
    host.view.customization.invalidateModuleLocate=[] {diva::CancelModuleLocate();};
    // 参数 request 为 UI 提交的稳定服装身份、角色分类及目录代数；原生上下文由定位器独立捕获。
    host.view.customization.submitModuleLocate=[](const diva::CustomizationLocateRequest& request) {
        std::string message;
        bool accepted=diva::SubmitModuleLocate(request,message);
        auto status=diva::PollModuleLocator();
        if(accepted || status.serial==request.serial) {
            lastModuleLocateSerial.store(request.serial,std::memory_order_relaxed);
            lastModuleLocateUiGeneration.store(request.uiGeneration,std::memory_order_relaxed);
        }
        diva::App().view.customization.status=message;
        Log("服装原生定位提交：ID="+std::to_string(request.key.id)+"；"+message);
    };
    // 打开面板时沿用游戏当前的难度和风格。
    host.view.onOpen=[] {
        auto snapshot=diva::Game().Read(); auto& view=diva::App().view;
        if(auto context=diva::ReadModuleLocatorContext()) view.ApplyOpeningContext(diva::SearchMode::Modules,context);
        else if(diva::Game().IsSongSearchPage()) view.ApplyOpeningContext(diva::SearchMode::Songs);
        Log("打开搜索："+diva::DescribeSnapshot(snapshot)+"；"+diva::App().DescribeInputState());
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
    Log("已初始化原生搜索 1.0.6，进程="+std::to_string(GetCurrentProcessId())+"，诊断日志追加写入；默认 Ctrl+F；曲库后台读取，无外部服务。");
}
// 所有 Mod 完成 Init 后再安装链式输入隔离。
__declspec(dllexport) void PostInit() {
    diva::Game().Initialize();
    Log(diva::Game().diagnostic);
    diva::InitializeModuleLocator(diva::Game().ExecutableBase());
    Log(diva::PollModuleLocator().message);
}
// DML 图形初始化；参数 chain/device/context 为加载器共享的 DX11 对象。
__declspec(dllexport) void D3DInit(IDXGISwapChain* chain,ID3D11Device* device,ID3D11DeviceContext* context) { diva::App().Attach(chain,device,context); }
// DML 每帧回调；参数 chain 为当前交换链，不进行阻塞等待或磁盘扫描。
__declspec(dllexport) void OnFrame(IDXGISwapChain* chain) {
    auto& host=diva::App(); std::lock_guard lock(host.mutex);
    const auto now=GetTickCount64();
    diva::TickModuleLocator(now,host.view.customization.generation);
    auto moduleStatus=diva::PollModuleLocator();
    auto moduleSerial=lastModuleLocateSerial.load(std::memory_order_relaxed);
    auto moduleUiGeneration=lastModuleLocateUiGeneration.load(std::memory_order_relaxed);
    if(moduleSerial && moduleStatus.serial==moduleSerial && moduleStatus.uiGeneration==moduleUiGeneration
        && moduleUiGeneration==host.view.customization.generation) {
        if(reportedModuleSerial!=moduleSerial || reportedModuleMessage!=moduleStatus.message) {
            reportedModuleSerial=moduleSerial;reportedModuleMessage=moduleStatus.message;
            host.view.customization.status=moduleStatus.message;
            if(moduleStatus.completed || moduleStatus.phase==diva::ModuleLocatePhase::Confirming) Log("服装定位阶段："+moduleStatus.message);
            if(moduleStatus.completed && moduleStatus.succeeded && host.view.closeOnSuccess) {
                host.view.open=false;host.view.ClearActions();host.releaseUntil=GetTickCount64()+120;
                diva::CancelModuleLocate();
                Log("服装定位成功后关闭搜索："+host.DescribeInputState());
            }
        }
    }
    if(host.view.open && !Selection().Busy()) diva::Game().Read();
    auto& selection=Selection(); bool busy=selection.Busy();auto previousMessage=selection.message;selection.Tick(diva::Game(),GetTickCount64());
    if(busy && previousMessage!=selection.message) Log("定位阶段："+selection.message+"；"+diva::DescribeSnapshot(diva::Game().Read()));
    if(busy) {
        host.view.status=selection.message;
        if(!selection.Busy()) {
            Log(selection.message);
            if(selection.succeeded && host.view.closeOnSuccess) {
                host.view.open=false; host.view.ClearActions(); host.releaseUntil=GetTickCount64()+120;
                Log("定位成功后关闭搜索："+host.DescribeInputState());
            }
        }
    }
    host.Frame(chain);
    // 参数为本帧 UI 更新后的目录代数；第二次同步用于立即丢弃绘制期间过期的定位请求。
    diva::TickModuleLocator(GetTickCount64(),host.view.customization.generation);
}
// DML 尺寸回调；参数 chain 为已调整大小的交换链，本 Mod 不持有后缓冲引用。
__declspec(dllexport) void OnResize(IDXGISwapChain* chain) { (void)chain; }
}
