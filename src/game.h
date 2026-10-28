#pragma once
#include "selector.h"
#include <Windows.h>
#include <cstdint>
namespace diva {
class GameMemory:public Memory {
public:
    // 延迟到 DML PostInit 核查可执行文件与安装输入隔离。
    void Initialize();
    // 读取菜单字段与实际选中歌曲。
    Snapshot Read() override;
    // 写入菜单控制标记；参数 next 为已允许目标状态。
    bool Transition(int next) override;
    // 写入歌曲、难度、风格及原版筛选字段；参数 request 为已验证请求。
    bool Select(const Request& request) override;
    // 当前 PC 构建使用原界面定位，不自动退回切菜单方案。
    bool SupportsDirect() const override;
    // 调用 PC 版原生列表和高亮入口；参数 request 为已验证的定位目标。
    bool DirectSelect(const Request& request) override;
    bool supported=false;
    // 输入隔离不可用时禁止打开可编辑面板，防止未知版本输入穿透。
    bool inputReady=false;
    std::string diagnostic="等待版本检查。";
private:
    uintptr_t base=0, dataOffset=0,styleAddress=0;
    uintptr_t selectorGetter=0,selectorObject=0;
    Snapshot previous;
    uint64_t stableSince=0;
    uintptr_t styleWindowAddress=0;
    int preferredStyleOffset=0;
};
// 获取延迟初始化的游戏适配器。
GameMemory& Game();
}
