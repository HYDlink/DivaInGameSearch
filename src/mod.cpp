#include "host.h"
#include "game.h"
#include "module_locator.h"
#include "native_page_router.h"
#include "module_action.h"
#include "customization_item_profiles.h"
#include <fstream>
#include <atomic>
#include <memory>
#include <unordered_map>

namespace {
// 延迟创建定位状态，避免 DllMain 中初始化业务。
diva::Selector& Selection() { static diva::Selector* selector=new diva::Selector; return *selector; }
std::atomic<uint64_t> lastModuleLocateSerial{0};
std::atomic<uint64_t> lastModuleLocateUiGeneration{0};
uint64_t reportedModuleSerial=0;
std::string reportedModuleMessage;
// 原生预览临时让出搜索输入；只在独立读回原装备且同一预览事务完成后恢复面板。
bool nativePreviewSuspended=false;
uint64_t nativePreviewSerial=0;
// 按钮受理后在下一帧绘制前隐藏，避免关闭回调把刚接受的原生动作当作用户取消。
uint64_t pendingActionCloseSerial=0;
bool nativeRoutingAvailable=false;
uint64_t reportedSongSerial=0;
std::string reportedSongMessage;
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
    // 语言须先于 App() 确定：界面对象构造时就会取默认文案。
    diva::InitLocale(diva::fs::path(path).parent_path());
    auto& host=diva::App(); host.Init(diva::fs::path(path).parent_path(),diva::fs::path(executable).parent_path());
    host.diagnostic=Log;
    for(const auto& line:diva::LocaleDiagnostics()) Log(line);
    // 条件或卡片选择改变时只发布取消代数，原生操作始终留在模型帧处理。
    host.view.customization.invalidateModuleLocate=[] {diva::CancelNativePageRouter();};
    // 参数key是按钮所在卡片，不借用搜索选择或原生高亮。
    host.view.customization.actionCapability=[](const diva::CustomizationKey& key) {
        // 参数key为按钮所在卡片；目录人物仅提供切分类意图，原生目标与拥有状态仍在游戏帧重新核对。
        const auto catalog=diva::App().view.customization.catalog;
        if(catalog) for(const auto& item:catalog->items) if(item.key==key) {
            const auto domain=diva::CustomizationItemDomain(item);
            // 发型与道具走独立执行器能力，不借用模型动作缓存。
            if(domain && *domain!=diva::CustomizationDomain::Models) return diva::ReadCustomizationItemActionCapability(*domain,key);
            if(!domain) return diva::CustomizationActionCapability{};
            return diva::ReadCustomizationActionCapability(key,diva::ModuleSearchCharacter(item.character));
        }
        return diva::ReadCustomizationActionCapability(key);
    };
    // 参数page为自定义页快照；按来源歌曲PV演出角色与实际歌者槽位解析人物，只读推荐快照，不调用游戏函数。
    diva::ConfigureCustomizationSingerResolver([](const diva::NativePageSnapshot& page) -> std::optional<diva::CustomizationCharacter> {
        const auto recommendations=diva::ReadRecommendations();
        if(!recommendations || page.sourcePvid<=0) return std::nullopt;
        return diva::ResolvePerformerCharacter(*recommendations,page.sourcePvid,page.slot);
    });
    host.view.customization.singerProvider=diva::ReadCustomizationSingerContext;
    // 参数 catalog 为最新目录；发布模块 ID 到人物的只读映射，游戏帧线程据此按实际装备模型解析歌者。
    host.view.customization.onCatalogChanged=[](const std::shared_ptr<const diva::CustomizationCatalog>& catalog) {
        auto characters=std::make_shared<std::unordered_map<int,diva::CustomizationCharacter>>();
        if(catalog) for(const auto& item:catalog->items) {
            // 同一 ID 以首个生效定义为准；未知与共通不写入，避免把 ALL 当成具体人物。
            if(item.key.kind!=diva::CustomizationKind::Module || item.character==diva::CustomizationCharacter::Unknown
                || item.character==diva::CustomizationCharacter::All) continue;
            characters->try_emplace(item.key.id,item.character);
        }
        diva::PublishCustomizationModelCharacters(std::move(characters));
    };
    // 参数key/fingerprint为请求稳定键与目录指纹；网关据最新目录重算领域、部位和人物，指纹不符即拒绝。
    diva::ConfigureCustomizationTargetResolver([](const diva::CustomizationKey& key,const std::string& fingerprint) -> std::optional<diva::CustomizationTargetFacts> {
        const auto catalog=diva::App().view.customization.catalog;
        if(!catalog || catalog->catalogFingerprint!=fingerprint) return std::nullopt;
        for(const auto& item:catalog->items) if(item.key==key) {
            const auto domain=diva::CustomizationItemDomain(item);if(!domain) return std::nullopt;
            diva::CustomizationTargetFacts facts;facts.domain=*domain;facts.part=item.part;
            facts.character=*domain==diva::CustomizationDomain::Models?diva::ModuleSearchCharacter(item.character):item.character;
            return facts;
        }
        return std::nullopt;
    });
    host.view.customization.recommendationProvider=diva::ReadRecommendations;
    // 参数 request 为 UI 提交的稳定服装身份、角色分类及目录代数；原生上下文由定位器独立捕获。
    host.view.customization.submitModuleLocate=[](const diva::CustomizationLocateRequest& request) {
        std::string message;
        bool accepted=diva::SubmitCustomizationRequest(request,message);
        auto status=diva::PollNativePageRouter();
        if(accepted || status.serial==request.serial) {
            lastModuleLocateSerial.store(request.serial,std::memory_order_relaxed);
            lastModuleLocateUiGeneration.store(request.uiGeneration,std::memory_order_relaxed);
        }
        if(accepted) {
            // 新请求接管旧预览的面板恢复权，避免旧事务退出时把新操作提前重开或关闭。
            nativePreviewSuspended=false;nativePreviewSerial=0;
            if(request.action!=diva::CustomizationAction::Locate) pendingActionCloseSerial=request.serial;
        }
        diva::App().view.customization.status=message;
        Log(std::string(diva::CustomizationDomainName(request.domain))+"原生请求提交：ID="+std::to_string(request.key.id)+"；"+message);
        // 开发原因（缺失的原生证据等）只进入诊断日志，界面保持短句。
        if(status.serial==request.serial && !status.detail.empty()) Log("原生能力细节："+status.detail);
    };
    host.view.customization.submitModuleAction=host.view.customization.submitModuleLocate;
    // 发型与自定义道具使用独立注入口；同一网关按最新目录重算目标页，未认证能力明确拒绝。
    host.view.customization.submitItemRequest=host.view.customization.submitModuleLocate;
    host.view.invalidateSongNavigation=diva::CancelSongNavigation;
    // 参数milliseconds为共享路由Hook实测CPU时间，避免把其开销漏算到游戏本体。
    diva::ConfigureNativePerformance([trace=host.view.performance](double milliseconds) {
        trace->nativeMicroseconds.fetch_add(static_cast<uint64_t>(milliseconds*1000),std::memory_order_relaxed);
    });
    // 参数request/message为到达稳定选曲后才转交的原请求，保持既有独立读回路径。
    diva::ConfigureSongNavigation([](diva::Request request,std::string& message) {
        auto snapshot=diva::Game().Read();auto& selector=Selection();
        const bool accepted=selector.Submit(request,true,snapshot,GetTickCount64());
        message=selector.message;return accepted;
    },[] {Selection().Cancel();});
    // 打开面板时沿用游戏当前的难度和风格。
    // 首次打开时游戏已完成启动，读到的语言才可靠；界面、字体和歌曲来源名称随之切换。
    host.view.onFirstOpen=[] {
        auto& view=diva::App().view;
        const auto language=diva::Game().CurrentLanguage();
        Log("游戏语言："+(language.empty()?std::string("未读取到"):language));
        if(!diva::ApplyGameLanguage(view.home,language)) return;
        for(const auto& line:diva::LocaleDiagnostics()) Log(line);
        view.fontDirty=true;
        view.customization.contextReason=diva::Tr(diva::Tx::catalog_context_default);
        view.Refresh();
    };
    host.view.onOpen=[] {
        auto snapshot=diva::Game().Read(); auto& view=diva::App().view;
        const auto page=diva::CurrentNativePage();
        if(auto context=diva::ReadModuleLocatorContext()) view.ApplyOpeningContext(diva::SearchMode::Modules,context);
        // 在发型或自定义道具页打开时进入对应领域，保留各领域查询；只读页面，不改游戏状态。
        else if(page==diva::NativePage::Hair) view.ApplyOpeningContext(diva::SearchMode::Hair);
        else if(page==diva::NativePage::Accessory) view.ApplyOpeningContext(diva::SearchMode::Accessories);
        else if(diva::Game().IsSongSearchPage()) view.ApplyOpeningContext(diva::SearchMode::Songs);
        Log("打开搜索："+diva::DescribeSnapshot(snapshot)+"；"+diva::App().DescribeInputState());
        if(!diva::Game().inputReady) {
            view.open=false;Log(diva::Game().diagnostic);
            MessageBoxW(diva::App().window,diva::Wide(std::string(diva::Tr(diva::Tx::mod_input_isolation_failed))+"\n"+diva::Game().diagnostic).c_str(),diva::Wide(diva::Tr(diva::Tx::app_title)).c_str(),MB_OK|MB_ICONINFORMATION);
            return;
        }
        if(snapshot.supported) {
            if(snapshot.visibleDifficulty>=0 && snapshot.visibleDifficulty<5) view.currentDifficulty=snapshot.visibleDifficulty;
            if(snapshot.style>=0 && snapshot.style<3) { view.query.style=snapshot.style; view.dirty=true; }
        }
    };
    // 参数 mode 为搜索领域；只读当前原生页，决定第二下 Enter 是否需要跨页。
    host.view.onTargetPage=[](diva::SearchMode mode) {
        const auto page=diva::CurrentNativePage();
        if(mode==diva::SearchMode::Songs) return page==diva::NativePage::Song || diva::Game().IsSongSearchPage();
        return page==(mode==diva::SearchMode::Modules?diva::NativePage::Module:mode==diva::SearchMode::Hair?diva::NativePage::Hair:diva::NativePage::Accessory);
    };
    // 播放列表只定位歌曲，不要求当前选择难度有对应谱面。
    host.view.songLocateWithoutChart=[] {return diva::Game().Read().playlistSong;};
    // 参数 request/available 为界面验证后的动作与谱面可用性。
    host.view.submit=[](diva::Request request,bool available) {
        auto snapshot=diva::Game().Read();Log("定位请求：目标="+std::to_string(request.pvid)+"，难度="+std::to_string(request.difficulty)+"；"+diva::DescribeSnapshot(snapshot));
        std::string message;
        if(snapshot.playlistSong || (!nativeRoutingAvailable && diva::Game().IsSongSearchPage())) {
            auto& selector=Selection();selector.Submit(request,available,snapshot,GetTickCount64());message=selector.message;
        } else diva::SubmitSongNavigation(request,available,message);
        diva::App().view.status=message;Log(message);
    };
    Log("已初始化原生搜索 1.2.5，进程="+std::to_string(GetCurrentProcessId())+"，诊断日志追加写入；默认 Ctrl+F；曲库后台读取，无外部服务。");
}
// 所有 Mod 完成 Init 后再安装链式输入隔离。
__declspec(dllexport) void PostInit() {
    diva::Game().Initialize();
    Log(diva::Game().diagnostic);
    if(diva::InitializeModuleLocator(diva::Game().ExecutableBase())) {
        diva::InitializeRecommendations(diva::Game().ExecutableBase());
        nativeRoutingAvailable=diva::InitializeNativePageRouter(diva::Game().ExecutableBase());
        if(nativeRoutingAvailable) Log(diva::CustomizationItemProfileDiagnostics());
        Log(nativeRoutingAvailable?"歌曲、服装、发型与自定义道具原生双击跨页路由已初始化；各项定位能力以已登记原生规格为准。":"原生路由初始化失败；已认证目标页内定位仍可用。");
    }
    Log(diva::PollModuleLocator().message);
}
// DML 图形初始化；参数 chain/device/context 为加载器共享的 DX11 对象。
__declspec(dllexport) void D3DInit(IDXGISwapChain* chain,ID3D11Device* device,ID3D11DeviceContext* context) { diva::App().Attach(chain,device,context); }
// DML 每帧回调；参数 chain 为当前交换链，不进行阻塞等待或磁盘扫描。
__declspec(dllexport) void OnFrame(IDXGISwapChain* chain) {
    auto& host=diva::App(); host.PumpInputMessages();
    const auto callbackStart=diva::PerformanceNow();
    std::lock_guard lock(host.mutex);
    diva::PerformanceFrameScope performance(*host.view.performance,true,callbackStart);
    host.view.performance->current.frameLock=diva::PerformanceNow()-callbackStart;
    const auto now=GetTickCount64();
    // 模型推荐与发型当前歌者都依赖已加载的 PV 演出角色快照。
    diva::RequestRecommendations(host.view.open && (host.view.mode==diva::SearchMode::Modules || host.view.mode==diva::SearchMode::Hair));
    diva::TickModuleLocator(now,host.view.customization.generation);
    diva::TickNativePageRouter(now,host.view.customization.generation);
    diva::TickModuleAction(now,host.view.customization.generation);
    diva::TickSongNavigation(now,host.view.songCatalogGeneration,host.view.queryRevision);
    const auto songStatus=diva::PollSongNavigation();
    if(songStatus.serial && (reportedSongSerial!=songStatus.serial || reportedSongMessage!=songStatus.message)) {
        reportedSongSerial=songStatus.serial;reportedSongMessage=songStatus.message;host.view.status=songStatus.message;
        Log("歌曲跨页阶段："+songStatus.message);
    }
    auto moduleStatus=diva::PollNativePageRouter();
    if(pendingActionCloseSerial && moduleStatus.serial==pendingActionCloseSerial) {
        pendingActionCloseSerial=0;
        if(!moduleStatus.completed || moduleStatus.succeeded) {
            if(moduleStatus.action==diva::CustomizationAction::Preview) {
                nativePreviewSuspended=true;nativePreviewSerial=moduleStatus.serial;
            }
            host.view.open=false;host.view.ClearActions();host.view.ResetSongBodyClick();host.view.customization.ResetBodyClick();host.releaseUntil=now+120;
            Log("服装按钮动作已受理，立即隐藏搜索窗口并等待原生结果。");
        }
    }
    if(diva::IsRunningCustomizationPreview(moduleStatus)
        && !nativePreviewSuspended && host.view.open) {
        nativePreviewSuspended=true;nativePreviewSerial=moduleStatus.serial;
        host.view.open=false;host.view.ClearActions();host.releaseUntil=now+120;
        Log("原生模型预览已确认，临时让出输入并保留搜索条件、滚动与焦点。");
    }
    if(nativePreviewSuspended && moduleStatus.serial==nativePreviewSerial && moduleStatus.completed) {
        nativePreviewSuspended=false;nativePreviewSerial=0;
        if(moduleStatus.previewRestored && moduleStatus.succeeded && !host.view.open) {
            host.view.open=true;
            host.view.customization.State().focus=host.view.customization.textNavigation;
            host.view.customization.requestGridFocus=host.view.customization.gridNavigation;
            host.view.customization.State().scroll=false;
            Log("原生预览退出且原装备读回一致，恢复原搜索条件与滚动。");
        }
    }
    auto moduleSerial=lastModuleLocateSerial.load(std::memory_order_relaxed);
    auto moduleUiGeneration=lastModuleLocateUiGeneration.load(std::memory_order_relaxed);
    if(moduleSerial && moduleStatus.serial==moduleSerial && moduleStatus.uiGeneration==moduleUiGeneration
        && moduleUiGeneration==host.view.customization.generation) {
        if(reportedModuleSerial!=moduleSerial || reportedModuleMessage!=moduleStatus.message) {
            reportedModuleSerial=moduleSerial;reportedModuleMessage=moduleStatus.message;
            host.view.customization.status=moduleStatus.message;
            if(moduleStatus.completed || moduleStatus.phase==diva::ModuleLocatePhase::Confirming) Log("定制原生阶段："+moduleStatus.message+(moduleStatus.detail.empty()?"":"；细节："+moduleStatus.detail));
            // 定制页面定位或跨页成功后保留搜索窗口，以便直接继续预览和更换。
        }
    }
    if(host.view.open && !Selection().Busy()) diva::Game().Read();
    auto& selection=Selection(); bool busy=selection.Busy();auto previousMessage=selection.message;selection.Tick(diva::Game(),GetTickCount64());
    if(busy && previousMessage!=selection.message) Log("定位阶段："+selection.message+"；"+diva::DescribeSnapshot(diva::Game().Read()));
    if(busy) {
        host.view.status=selection.message;
        if(!selection.Busy()) {
            diva::CompleteSongNavigation(selection.succeeded,selection.message);
            Log(selection.message);
            if(selection.succeeded && host.view.closeOnSuccess) {
                // 隔离至少覆盖连按 Enter 的窗口，第二下不会漏给游戏开始演奏。
                host.view.open=false; host.view.ClearActions(); host.releaseUntil=std::max(GetTickCount64()+120,host.view.enterGuardUntil);
                Log("定位成功后关闭搜索："+host.DescribeInputState());
            }
        }
    }
    host.Frame(chain);
    // 参数为本帧 UI 更新后的目录代数；第二次同步用于立即丢弃绘制期间过期的定位请求。
    diva::TickModuleLocator(GetTickCount64(),host.view.customization.generation);
    diva::TickNativePageRouter(GetTickCount64(),host.view.customization.generation);
    diva::TickModuleAction(GetTickCount64(),host.view.customization.generation);
    diva::TickSongNavigation(GetTickCount64(),host.view.songCatalogGeneration,host.view.queryRevision);
}
// DML 尺寸回调；参数 chain 为已调整大小的交换链，本 Mod 不持有后缓冲引用。
__declspec(dllexport) void OnResize(IDXGISwapChain* chain) { (void)chain; }
}
