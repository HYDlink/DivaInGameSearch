#pragma once
#include "customization.h"
#include <future>
#include <chrono>
#include <array>

namespace diva {
// 两种定制模式分别保留搜索、筛选、选择与滚动状态；不调用歌曲定位器。
struct CustomizationView {
    struct ModeState {
        CustomizationQuery query;
        char input[512]{};
        std::vector<size_t> results;
        CustomizationGridCursor cursor;
        uint64_t revision=1,applied=0;
        std::chrono::steady_clock::time_point due{};
        bool scroll=true,focus=true,details=false,detailsRequested=false;
        float scrollY=0;
    };
    struct SearchReply { size_t mode=0; uint64_t revision=0,generation=0; std::vector<size_t> results; };
    fs::path home,game;
    std::shared_ptr<const CustomizationCatalog> catalog;
    std::future<CustomizationCatalog> pending;
    std::future<SearchReply> searching;
    std::array<ModeState,2> states;
    CustomizationKind kind=CustomizationKind::Module;
    uint64_t generation=0,runningRevision=0,runningGeneration=0;
    size_t runningMode=0;
    bool composing=false;
    std::string status,contextReason="无法读取当前角色，默认初音未来";
    int columns=1,pageRows=1;
    bool textNavigation=false,gridNavigation=false,controlNavigation=false;
    // 初始化模式种类，默认不插入任何演示数据。
    CustomizationView();
    // 参数为插件和游戏目录，后台加载；已有加载任务时不重复启动。
    void Refresh(const fs::path& home,const fs::path& game);
    void EnsureLoaded(const fs::path& home,const fs::path& game);
    // 接收已完成任务，启动到期搜索，不等待后台任务。
    void Poll();
    // 参数 kind 为当前模式；模式切换保留各自条件。
    void SetKind(CustomizationKind kind);
    // 参数 context 为已验证上下文；未知时默认初音未来并说明原因。
    void OnOpen(std::optional<CustomizationContext> context);
    // 在外层现有窗口内绘制，不自行 Begin 主窗口。
    void Draw();
    // 参数 action 为有序单次动作；按当前文本或格子焦点解释。
    bool Navigate(CustomizationNavigation action);
    // 文本与格子能接收上下；Combo 和详情弹层不能接管导航。
    bool WantsListNavigation() const;
    // 仅格子焦点接管左右；搜索文本保留光标移动。
    bool WantsHorizontalNavigation() const;
    // 最新结果的 Enter 仅打开详情。
    void RequestDetails();
    // Esc 优先关闭详情，返回是否已消费；Combo 由 ImGui 原生取消。
    bool HandleEscape();
    // 当前模式条件变化后递增版本；参数 debounce 决定是否等候150毫秒。
    void QueueSearch(bool debounce);
    // 判断当前模式是否仍在等待最新条件的结果。
    bool SearchPending() const;
    // 返回当前模式的可变状态，供桥接与专用测试使用。
    ModeState& State();
    // 返回当前模式的只读状态。
    const ModeState& State() const;
};
}
