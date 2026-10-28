#pragma once
#include "core.h"
#include "selector.h"
#include <functional>
#include <future>
#include <memory>

namespace diva {
struct View {
    // 界面可变状态；曲库通过后台任务产生不可变快照。
    fs::path home,game;
    std::shared_ptr<const Catalog> catalog;
    std::future<Catalog> pending;
    Query query;
    std::vector<Result> results;
    std::set<int> favorites;
    char input[512]{};
    int selectedId=0,currentDifficulty=3;
    int shortcut=0x46;
    float fontSize=19, rowHeight=68, opacity=0.97f;
    bool open=false,focus=true,dirty=true,composing=false,imeGuard=false,closeOnSuccess=true;
    bool requestEnter=false,requestEscape=false,fontDirty=false;
    int move=0;
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
    // 对完整快照重建查询结果。
    void Requery();
    // 提交定位请求；参数 song/difficulty 为逻辑歌曲和确切难度。
    void Locate(const Song& song,int difficulty);
    // 绘制共享原生搜索界面。
    void Draw();
    // 重载可安全实时应用的界面配置。
    void ReloadStyle();
};
// 初始化共享字体和色彩；参数 size 为初始字体大小。
void SetupStyle(float size);
}
