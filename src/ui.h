#pragma once
#include "core.h"
#include "selector.h"
#include "customization_ui.h"
#include <functional>
#include <future>
#include <memory>
#include <chrono>

namespace diva {
enum class SearchMode { Songs,Modules,Accessories };
struct View {
    // 界面可变状态；曲库通过后台任务产生不可变快照。
    fs::path home,game;
    std::shared_ptr<const Catalog> catalog;
    std::future<Catalog> pending;
    // 后台查询只读取不可变快照；版本号防止旧结果覆盖新输入。
    struct SearchReply {
        uint64_t revision=0;
        std::vector<Result> results;
    };
    std::future<SearchReply> searching;
    uint64_t queryRevision=0,appliedRevision=0,searchingRevision=0;
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
    int move=0,difficultyMove=0;
    bool listNavigation=true,horizontalNavigation=true,scrollSelection=false;
    SearchMode mode=SearchMode::Songs;
    CustomizationView customization;
    std::vector<std::pair<CustomizationNavigation,int>> itemMoves;
    uint64_t serial=0;
    std::string status="正在建立本地曲库……";
    std::string initialConfiguration;
    bool hasInitialConfiguration=false;
    std::function<void(Request,bool)> submit;
    std::function<void()> onOpen;
    // 发起后台重建；不在绘制帧扫描磁盘。
    void Refresh();
    // 接收后台结果并维护有效选择。
    void Poll();
    // 立即调度完整快照查询，不在绘制帧执行搜索或等待。
    void Requery();
    // 标记查询变化；参数 debounce 为是否等待连续输入停止 150 毫秒。
    void QueueSearch(bool debounce);
    // 当前结果是否仍在等待最新文本或筛选条件。
    bool SearchPending() const;
    // 消费列表方向键；左右只选择当前歌曲的可用难度。
    void Navigate();
    // 参数 next 为活动领域，互切清空尚未消费的动作，保留各自查询。
    void SetMode(SearchMode next);
    // 每次打开采样已验证的定制角色；当前适配未接线时明确回退初音未来。
    void OnOpened();
    // 清除跨帧确认及方向请求，防止关闭或模式切换后漏到其他领域。
    void ClearActions();
    // 提交定位请求；参数 song/difficulty 为逻辑歌曲和确切难度。
    void Locate(const Song& song,int difficulty);
    // 绘制共享原生搜索界面。
    void Draw();
    // 重载可安全实时应用的界面配置。
    void ReloadStyle();
};
// 初始化共享字体和色彩；参数 size 为初始字体大小。
void SetupStyle(float size);
// 让随仓库的 ImGui 1.88 将普通按钮和下拉框纳入 Tab 焦点循环；在窗口控件前调用。
void EnableControlTabNavigation();
}
