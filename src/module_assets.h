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

// 官方服装实体表的物理归档、内部条目、逻辑路径、UTF-8 文本和文件指纹。
struct OfficialModuleTable {
    fs::path cpkPath;
    std::string internalPath;
    std::string logicalPath;
    std::string tableText;
    std::string fingerprint;
};

// 已解码精灵的轻量视图；rgbaPixels 由 rgbaOwner 持有，调用方需保留 owner 到上传结束。
struct ModuleAssetPreview {
    int width = 0;
    int height = 0;
    std::string source;
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

    // 参数 item/requestVisible/style 为稳定服装项、是否排队请求可见资源和专用 sprite 风格令牌；false 只返回元数据和缓存。
    // style=1 查 MD_IMG（MEGA39/MM），style=2 查 MD_IMG_FT；style=0 或其他值明确返回风格未确认，不猜测图片。
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
