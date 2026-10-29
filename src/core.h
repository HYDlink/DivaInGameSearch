#pragma once
#include <array>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace diva {
namespace fs = std::filesystem;
struct Chart {
    // 实际等级、脚本和生效提供方；空脚本表示该风格难度不可用。
    float level = 0;
    std::string script, source;
    bool available = false;
};
struct Song {
    // 逻辑歌曲编号，不使用物理脚本编号代替。
    int id = 0;
    std::string name, english, reading, aliases, source, key, modKey;
    // 曲库构建时缓存名称规范化，避免逐字检索和排序重复调用 Unicode API。
    std::string nameKey,englishKey;
    std::vector<std::string> sources;
    std::array<std::array<Chart, 5>, 3> charts;
    bool hidden = false;
};
struct Catalog {
    // 不可变曲库快照与中文诊断信息。
    std::vector<Song> songs;
    std::vector<std::string> packs, diagnostics;
    std::string configurationKey;
    bool requiresRestart=false;
    double milliseconds = 0;
};
struct Query {
    // 查询文本、来源过滤、搜索模式、风格、难度和全局排序。
    std::string text, pack;
    bool modMode = false, favoritesOnly = false;
    int style = 0, difficulty = -1, sort = 0;
};
struct Result { size_t index; int score; };
// 将 UTF-8 文本转换为 Windows Unicode；参数 text 为输入文本。
std::wstring Wide(const std::string& text);
// 将 Windows Unicode 转为 UTF-8；参数 text 为输入文本。
std::string Utf8(const std::wstring& text);
// 统一兼容字符、大小写、假名和分隔符；参数 text 为待检索文本。
std::string Normalize(const std::string& text);
// 从可靠假名读音产生罗马音，不推测汉字读音；参数 text 为数据库读音。
std::string Romanize(const std::string& text);
// 构建检索键；参数 song 为完整合并后的歌曲。
void PrepareSong(Song& song);
// 在完整曲库执行过滤与排序；参数 catalog/query/favorites 分别为快照、条件和收藏。
std::vector<Result> Search(const Catalog& catalog, const Query& query, const std::set<int>& favorites);
// 后台读取基础元数据与实际 DML 配置；参数 home/game 为本 Mod 和游戏根目录。
Catalog LoadCatalog(const fs::path& home, const fs::path& game);
// 解析单个 PVDB；参数 path/source 为文件与来源，返回歌曲属性映射。
using Fields = std::map<int, std::map<std::string, std::pair<std::string, std::string>>>;
Fields ReadPvdb(const fs::path& path, const std::string& source);
// 安全解析数值等级；参数 text 为 PV_LV_XX_X 或数字。
float ParseLevel(const std::string& text);
// 读取本地收藏；参数 path 为 UTF-8 文本文件。
std::set<int> LoadFavorites(const fs::path& path);
// 原子保存收藏；参数 path/values 为目标文件与编号集合。
bool SaveFavorites(const fs::path& path, const std::set<int>& values);
}
