#pragma once

#include <optional>
#include "customization.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace diva {

// 官方 CPK 条目及其游戏内逻辑路径；entryData 只在显式读取时保留。
struct OfficialCpkEntry {
    fs::path cpkPath;
    std::string internalPath;
    std::string logicalPath;
    std::string entryData;
    std::string fingerprint;
};

// 官方服装实体表的物理归档、内部条目、逻辑路径、UTF-8 文本和文件指纹；与定制表共用同一结构。
using OfficialModuleTable = OfficialEntityTable;

// 已解码精灵的轻量视图；rgbaPixels 由 rgbaOwner 持有，调用方需保留 owner 到上传结束。
struct ModuleAssetPreview {
    int width = 0;
    int height = 0;
    std::string source;
    // 专用图不可用时回退同 ID 通用图；fallbackReason 保留专用图失败原因。
    bool fallback = false;
    std::string fallbackReason;
    // 图片仍在后台读取时为 true；界面据此显示占位，不比较显示文字。
    bool loading = false;
    // 有效内容的透明边界；宽高为 0 表示未知，按整张画布显示。
    int contentX = 0, contentY = 0, contentWidth = 0, contentHeight = 0;
    void* texture = nullptr;
    const unsigned char* rgbaPixels = nullptr;
    std::shared_ptr<const std::vector<unsigned char>> rgbaOwner;
};

// 图片专用风格令牌；不得直接把歌曲模式 ARCADE/CONSOLE/MIXED 当成此令牌。
enum class ModuleSpriteStyle : int { Unspecified = 0, Mega39 = 1, FutureTone = 2 };

// 参数 gameRoot/pluginRoot 为游戏目录和插件目录；diagnostics 接收中文读取诊断。
std::vector<OfficialModuleTable> ReadOfficialModuleTables(
    const fs::path& gameRoot,
    const fs::path& pluginRoot,
    std::vector<std::string>& diagnostics);

// 参数 gameRoot/pluginRoot 为游戏目录和插件目录；diagnostics 接收中文读取诊断。
// 读取全部官方 gm_customize_item_tbl.farc 内的 gm_customize_item_id.bin（发型 KAMI 与四部位道具）。
std::vector<OfficialEntityTable> ReadOfficialCustomizeItemTables(
    const fs::path& gameRoot,
    const fs::path& pluginRoot,
    std::vector<std::string>& diagnostics);

// 参数 gameRoot/pluginRoot/suffix 为游戏目录、插件目录和区分大小写无关的逻辑路径后缀；
// diagnostics 接收中文读取诊断，单条目限制和总数量限制由实现统一执行。
std::vector<OfficialCpkEntry> ReadOfficialCpkEntries(
    const fs::path& gameRoot,
    const fs::path& pluginRoot,
    const std::string& suffix,
    std::vector<std::string>& diagnostics);

// 参数 gameRoot/pluginRoot/catalog 为游戏目录、插件目录和服装来源快照；diagnostics 接收初始化诊断。
// 构造会扫描CPK目录，调用方必须放到后台任务；Peek 使用非阻塞缓存查询。
class ModuleAssets {
public:
    // 参数 gameRoot/pluginRoot/catalog 为游戏目录、插件目录和服装快照；diagnostics 接收初始化诊断。
    ModuleAssets(const fs::path& gameRoot,
                 const fs::path& pluginRoot,
                 std::shared_ptr<const CustomizationCatalog> catalog,
                 std::vector<std::string>& diagnostics);

    // 等待正常销毁时停止有限后台任务；进程卸载期间不在 DllMain 调用析构。
    ~ModuleAssets();

    ModuleAssets(const ModuleAssets&) = delete;
    ModuleAssets& operator=(const ModuleAssets&) = delete;

    // 参数 item/requestVisible/style 为稳定实体、是否排队请求可见资源和专用 sprite 风格令牌；false 只返回元数据和缓存。
    // 模型查 MD_IMG[_FT]，发型与道具查 CMNITM_THMB ITM_IMG[_FT]；style=1 为 MEGA39/MM，style=2 为 Future Tone（缺专用图回退同 ID 通用图）。
    ModuleAssetPreview Peek(const CustomizationItem& item, bool requestVisible, int style = 0);

    // 参数 maxImages/maxDecodedBytes 为缓存精灵数和RGBA字节上限；会淘汰最久未访问项。
    void SetCacheLimits(size_t maxImages, size_t maxDecodedBytes);

    // 显式停止后台队列；参数 wait 为是否等待当前单条解码完成。
    void Shutdown(bool wait = true);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
