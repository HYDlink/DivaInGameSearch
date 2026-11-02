#pragma once
#include "performance_trace.h"
#include "search_dispatch.h"
#include "customization.h"
#include "customization_action.h"
#include "recommendation.h"
#include <imgui.h>
#include <future>
#include <chrono>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace diva {
// 描述资源适配器注入的服装预览；同步上传期间由 rgbaOwner 保持像素缓冲有效。
struct CustomizationPreview {
    ImTextureID texture=nullptr;
    // 像素缓冲所有者；同步纹理上传完成前保持共享引用。
    std::shared_ptr<const std::vector<unsigned char>> rgbaOwner;
    const unsigned char* rgbaPixels=nullptr;
    int width=0,height=0;
    std::string source;
};

// 模型、发型、自定义道具三个领域分别保留搜索、筛选、选择与滚动状态；不调用歌曲定位器。
struct CustomizationView {
    // 共用后台查询执行器，声明在视图状态之前以保证最后销毁。
    std::shared_ptr<SearchExecutor> searchExecutor=std::make_shared<SearchExecutor>();
    std::shared_ptr<PerformanceTrace> performance=std::make_shared<PerformanceTrace>();
    struct ModeState {
        CustomizationQuery query;
        char input[512]{};
        std::vector<size_t> results;
        CustomizationGridCursor cursor;
        uint64_t revision=1,applied=0;
        SearchTiming searchTiming;
        std::chrono::steady_clock::time_point due{};
        bool scroll=true,focus=true,details=false,detailsRequested=false,locateRequested=false;
        float scrollY=0;
        std::map<CustomizationKey,std::vector<std::string>> recommendationReasons;
        std::vector<std::string> recommendationWarnings;
        size_t matchedSongs=0;
    };
    struct SearchReply { size_t mode=0; uint64_t revision=0,generation=0; std::vector<size_t> results; SearchTiming timing; RecommendedModules recommendations; };
    // 共用歌曲目录、检索规则与收藏；独立歌曲条件不继承歌曲页的Mod检索/曲包/收藏筛选，仅同步风格。
    std::shared_ptr<const Catalog> songCatalog,observedSongCatalog;
    Query songCriteria;
    std::set<int> songFavorites;
    std::string observedSongCriteria;
    uint64_t observedRecommendationGeneration=0;
    // 无游戏的共用预览可以不注入读取器；实际DLL注入只读快照函数。
    std::function<std::shared_ptr<const RecommendationSnapshot>()> recommendationProvider;
    struct GridCell {
        size_t resultIndex=0;
        int row=0,startColumn=0,span=1;
        ImVec2 center{};
    };
    fs::path home,game;
    std::shared_ptr<const CustomizationCatalog> catalog;
    std::future<CustomizationCatalog> pending;
    std::future<SearchReply> searching;
    // 下标见 CustomizationDomainIndex：0 模型、1 发型、2 自定义道具。
    std::array<ModeState,3> states;
    CustomizationDomain domain=CustomizationDomain::Models;
    uint64_t generation=0,runningRevision=0,runningGeneration=0,locateSerial=0,drawFrame=0,locateRequestFrame=0,locateRequestRevision=0;
    size_t runningMode=0;
    bool composing=false;
    std::string status,contextReason="搜索默认全部角色；打开时保留筛选。";
    std::string initialCatalogFingerprint;
    bool hasInitialCatalogFingerprint=false,catalogRequiresRestart=false;
    int columns=1,pageRows=1;
    // 独立预览样式：1为MEGA39，2为Future Tone；不借用歌曲模式枚举。
    int previewStyle=2;
    float cardWidth=1.0f,cardHeight=344.0f,rowHeight=356.0f,gridScrollY=0;
    int previousLayoutColumns=0;
    std::vector<int> previousLayoutSpans;
    float previousGridWidth=0.0f;
    std::vector<GridCell> gridCells;
    std::vector<std::vector<size_t>> gridRows;
    // 仅保留布局所需尺寸，避免跨帧持有解码像素和纹理；资源发布时失效。
    std::vector<CustomizationPreview> layoutPreviews;
    uint64_t previewMetadataGeneration=0,layoutMetadataGeneration=~uint64_t(0),layoutRevision=~uint64_t(0),layoutCatalogGeneration=~uint64_t(0);
    int layoutStyle=-1;
    CustomizationDomain layoutDomain=CustomizationDomain::Accessories;
    float layoutWidth=-1;
    std::array<std::map<std::string,size_t>,3> sourceAssociationCounts;
    std::optional<CustomizationKey> hoveredKey;
    std::chrono::steady_clock::time_point hoverSince{};
    bool textNavigation=false,gridNavigation=false,controlNavigation=false;
    // 从文本框使用上下浏览后转入卡片焦点，使随后的左右键能移动卡片。
    bool requestGridFocus=false;
    // 首击只绑定真实主体、领域及当前查询；滚动、失焦、重排或按钮输入立即作废。
    std::optional<CustomizationKey> bodyClickKey,pendingLocateKey;
    CustomizationDomain bodyClickDomain=CustomizationDomain::Models;
    uint64_t bodyClickRevision=0,bodyClickGeneration=0;
    double bodyClickTime=-1;
    ImVec2 bodyClickPosition{};
    bool bodyDoubleClick=false;
    bool suppressActionRelease=false;
    std::optional<CustomizationKey> actionPressKey;
    CustomizationOrigin locateOrigin=CustomizationOrigin::Keyboard;
    std::function<CustomizationActionCapability(const CustomizationKey&)> actionCapability;
    std::function<void(const CustomizationLocateRequest&)> submitModuleAction;
    // 以稳定服装项为参数，返回轻量缓存快照；requestVisible=true 时可异步请求可见项及邻近项资源。
    std::function<CustomizationPreview(const CustomizationItem&,bool requestVisible)> previewProvider;
    // 参数 key/pixels/width/height 为服装身份与 RGBA 像素；回调应缓存上传纹理，避免逐帧重复创建。
    std::function<ImTextureID(const CustomizationKey&,const unsigned char*,int,int)> uploadPreviewPixels;
    // 参数 request 包含服装稳定键、角色映射、目录指纹和 UI 代数/序号；由原生适配器执行非阻塞提交。
    std::function<void(const CustomizationLocateRequest&)> submitModuleLocate;
    // 参数 request 为发型或自定义道具的定位／动作请求；与模型定位器分开注入，后端须独立核对目标页与能力。
    std::function<void(const CustomizationLocateRequest&)> submitItemRequest;
    // 任一定制领域的搜索条件或选择变化时立即作废已提交请求；回调不得阻塞界面线程。
    std::function<void()> invalidateModuleLocate;

    // 初始化模式种类，默认不插入任何演示数据。
    CustomizationView();
    // 参数为插件和游戏目录，后台加载；已有加载任务时不重复启动。
    void Refresh(const fs::path& home,const fs::path& game);
    // 参数 home/game 为插件和游戏目录；首次使用时异步启动一次加载。
    void EnsureLoaded(const fs::path& home,const fs::path& game);
    // 接收已完成任务，启动到期搜索，不等待后台任务。
    void Poll();
    // 参数 domain 为当前领域；切换只保留各自条件，不操作游戏页面。
    void SetDomain(CustomizationDomain domain);
    // 参数context仅更新游戏角色说明；不重置已选搜索筛选，首次默认全部角色。
    void OnOpen(std::optional<CustomizationContext> context);
    // 在外层现有窗口内绘制，不自行 Begin 主窗口。
    void Draw();
    // 参数 action 为有序单次动作；按当前文本或卡片焦点解释。
    bool Navigate(CustomizationNavigation action);
    // 文本与卡片能接收上下；Combo 和其他控件不能接管列表导航。
    bool WantsListNavigation() const;
    // 文本和卡片焦点均支持左右浏览结果，与歌曲搜索一致。
    bool WantsHorizontalNavigation() const;
    // Enter 在最新查询完成后提交当前领域所选项目的原位定位。
    void RequestDetails();
    // 参数 key/origin 为所选项目与输入来源；在最新结果有效后调用当前领域注入的定位适配器。
    void RequestLocate(const CustomizationKey& key,CustomizationOrigin origin=CustomizationOrigin::Footer);
    // 参数 key/action 为按钮所在卡片的稳定身份与原生动作，不改变搜索选择。
    void RequestAction(const CustomizationKey& key,CustomizationAction action);
    // 清除主体双击链；失焦、IME、滚动、目录及布局变更共用此入口。
    void ResetBodyClick();
    // Esc 优先清除等待中的定位，返回是否已消费；Combo 由 ImGui 原生取消。
    bool HandleEscape();
    // 当前模式条件变化后递增版本；参数 debounce 决定是否等候连续输入停止150毫秒。
    void QueueSearch(bool debounce);
    // 判断当前模式是否仍在等待最新条件的结果。
    bool SearchPending() const;
    // 返回当前模式的可变状态，供桥接与专用检查使用。
    ModeState& State();
    // 返回当前模式的只读状态。
    const ModeState& State() const;
};
}
