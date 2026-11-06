#pragma once
#include "search_dispatch.h"
#include "performance_trace.h"
#include "core.h"
#include "i18n.h"
#include "selector.h"
#include "customization_ui.h"
#include "search_ui_style.h"
#include <functional>
#include <future>
#include <memory>
#include <chrono>

namespace diva {
// 连按两次 Enter 的最长间隔（毫秒），超过则视为两次独立按键。
inline constexpr uint64_t kDoubleEnterMs=400;
// 顶部四个独立领域：歌曲｜模型｜发型｜自定义道具；切换领域只改变插件界面，不操作游戏页面。
enum class SearchMode { Songs,Modules,Hair,Accessories };
// 参数 mode 为非歌曲领域，返回对应定制检索领域。
CustomizationDomain SearchModeDomain(SearchMode mode);
struct View {
    // 共用后台查询执行器，声明在视图状态之前以保证最后销毁。
    std::shared_ptr<SearchExecutor> searchExecutor=std::make_shared<SearchExecutor>();
    std::shared_ptr<PerformanceTrace> performance=std::make_shared<PerformanceTrace>();
    // 报告仅在显式导出时后台写入，不把逐字符日志放进游戏帧。
    std::future<std::string> performanceExport;
    SearchTiming searchTiming;
    // 界面可变状态；曲库通过后台任务产生不可变快照。
    fs::path home,game;
    std::shared_ptr<const Catalog> catalog;
    std::future<Catalog> pending;
    // 后台查询只读取不可变快照；版本号防止旧结果覆盖新输入。
    struct SearchReply {
        uint64_t revision=0;
        std::vector<Result> results;
        SearchTiming timing;
    };
    std::future<SearchReply> searching;
    uint64_t queryRevision=0,appliedRevision=0,searchingRevision=0;
    // 歌曲目录身份与主体首击证据，查询、布局或输入上下文改变即失效。
    uint64_t songCatalogGeneration=1,songClickRevision=0,songClickGeneration=0;
    int songClickPvid=0;
    double songClickTime=-1;
    float songClickX=0,songClickY=0,songListWidth=-1,songListScroll=-1;
    std::chrono::steady_clock::time_point queryDue{};
    Query query;
    std::vector<Result> results;
    std::set<int> favorites;
    char input[512]{};
    int selectedId=0,currentDifficulty=3;
    int shortcut=0x46;
    float fontSize=19, rowHeight=68, opacity=0.97f;
    bool open=false,focus=true,dirty=true,composing=false,imeGuard=false,closeOnSuccess=true;
    bool requestEnter=false,requestEscape=false,fontDirty=false;
    // 连按 Enter 的按下时刻与跨页标记；enterPvid/enterRevision 为第一下实际定位的目标，第二下必须一致。
    uint64_t enterPressTick=0,enterGuardUntil=0,enterRevision=0;
    int enterPvid=0;
    bool enterDouble=false;
    // Ctrl+F 全选与 Ctrl+←→ 移动光标的请求；Ctrl+P／Ctrl+E 请求对当前选中卡片执行预览、换装。
    SearchTextRequest textRequest;
    bool requestPreview=false,requestEquip=false;
    // Tab／Ctrl+Tab 请求切换顶部主标签的步数，在下一帧绘制前消费。
    int tabMove=0;
    // Ctrl+F9 请求录制性能数据；录制结束后自动导出一次。
    bool requestPerformanceRecord=false,performanceAutoExport=false;
    int move=0,difficultyMove=0;
    bool listNavigation=true,horizontalNavigation=true,scrollSelection=false;
    SearchMode mode=SearchMode::Songs;
    CustomizationView customization;
    // 在任何查询启动前绑定同一个执行器，不在绘制帧替换正在工作的线程所有者。
    View(){customization.searchExecutor=searchExecutor;customization.performance=performance;}
    std::vector<std::pair<CustomizationNavigation,int>> itemMoves;
    uint64_t serial=0;
    std::string status=Tr(Tx::song_status_building);
    std::string initialConfiguration;
    bool hasInitialConfiguration=false;
    std::function<void(Request,bool)> submit;
    // 只在播放列表歌曲选择页允许按歌曲身份定位，无需谱面难度。
    std::function<bool()> songLocateWithoutChart;
    std::function<void()> onOpen;
    // 本次运行首次打开面板时调用一次，早于其他打开逻辑，用于确定界面语言。
    std::function<void()> onFirstOpen;
    // 查询、目录、领域或输入上下文改变时取消尚未完成的歌曲路由。
    std::function<void()> invalidateSongNavigation;
    // 参数 mode 为搜索领域，返回游戏当前是否已在该领域的目标页；已在目标页时第二下 Enter 不再提交。
    std::function<bool(SearchMode)> onTargetPage;
    // 把已录制的样本交给后台写入带时间戳的文件，不在帧内格式化或写磁盘。
    void ExportPerformance();
    // 发起后台重建；不在绘制帧扫描磁盘。
    void Refresh();
    // 接收后台结果并维护有效选择。
    void Poll();
    // 立即调度完整快照查询，不在绘制帧执行搜索或等待。
    void Requery();
    // 标记查询变化；参数 debounce 为是否等待连续输入短暂停止，时长见 kSearchDebounce。
    void QueueSearch(bool debounce);
    // 当前结果是否仍在等待最新文本或筛选条件。
    bool SearchPending() const;
    // 消费列表方向键；左右只选择当前歌曲的可用难度。
    void Navigate();
    // 参数 next 为活动领域，互切清空尚未消费的动作，保留各自查询。
    void SetMode(SearchMode next);
    // 参数 step 为 1 切到下一个主标签、-1 切到上一个，首尾循环。
    void CycleMode(int step);
    // 每次打开记录定制上下文；不覆盖用户已选角色筛选。
    void OnOpened();
    // 参数 preferred/context 为已核实页面与可选角色；每次打开重新路由，保留各页搜索条件。
    void ApplyOpeningContext(SearchMode preferred,std::optional<CustomizationContext> context={});
    // 清除跨帧确认及方向请求，防止关闭或模式切换后漏到其他领域。
    void ClearActions();
    // 记录一次 Enter 按下；参数 now 为单调毫秒时刻。不在目标页时连按两次才获得跨页权限。
    void PressEnter(uint64_t now);
    // 聚焦当前领域的搜索框并全选文字，便于直接重新输入。
    void RequestSearchFocus();
    // 提交定位请求；参数 song/difficulty 为逻辑歌曲和确切难度。
    void Locate(const Song& song,int difficulty,SongOrigin origin=SongOrigin::Keyboard);
    // 清除歌曲首击证据，不改变当前查询和搜索选择。
    void ResetSongBodyClick();
    // 参数song为真实名称主体命中歌曲，按ImGui时间和距离认证同项左键双击。
    void ClickSongBody(const Song& song);
    // 绘制共享原生搜索界面。
    void Draw();
    // 重载可安全实时应用的界面配置。
    void ReloadStyle();
};
// 初始化共享字体和色彩；参数 size 为初始字体大小。
void SetupStyle(float size);
}
