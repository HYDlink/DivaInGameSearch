#pragma once
#include "core.h"
#include <optional>
#include <memory>
#include <cstdint>

namespace diva {
// 服装和饰品拥有独立编号空间，零也是合法编号。
enum class CustomizationKind { Module, Accessory };
enum class CustomizationCharacter { Miku,Rin,Len,Luka,Neru,Haku,Kaito,Meiko,Sakine,Teto,All,Unknown,Derived,Random };
enum class CustomizationPart { Head,Face,Neck,Back,Unknown };
struct CustomizationKey { CustomizationKind kind=CustomizationKind::Module; int id=0; auto operator<=>(const CustomizationKey&) const=default; };
struct CustomizationSource { std::string id,name,modDirectory,includeRoot; bool currentMod=false; };
struct CustomizationContribution { std::string source,role; bool effective=false; };
struct PreviewDescriptor { enum class State { Unavailable }; State state=State::Unavailable; std::string style,source,assetKey; };
struct CustomizationItem {
    CustomizationKey key;
    CustomizationCharacter character=CustomizationCharacter::Unknown;
    CustomizationPart part=CustomizationPart::Unknown;
    std::string characterToken,partToken,name,definitionSource,nameSource,cos,metadataState="已导入";
    std::optional<int> sortIndex,objectId,bindModule;
    std::map<std::string,std::string> namesByLanguage,rawFields;
    std::vector<std::string> aliases;
    std::vector<CustomizationContribution> sources;
    std::string searchKey,modKey,nameKey;
    PreviewDescriptor preview;
};
struct CustomizationCatalog {
    std::vector<CustomizationItem> items;
    std::vector<CustomizationSource> sources;
    std::vector<std::string> diagnostics;
    std::string configurationKey,dmlFingerprint,catalogFingerprint,language="cn";
    bool stale=false;
    size_t orphanNames=0,unimportedArchives=0;
    bool importedDirectory=true;
};
struct CustomizationQuery {
    CustomizationKind kind=CustomizationKind::Module;
    std::string text,source;
    std::optional<CustomizationCharacter> character=CustomizationCharacter::Miku;
    std::optional<CustomizationPart> part;
    bool modMode=false;
    int sort=0;
};
// 只读菜单上下文；未经验证的返回值不能用于原生定位。
struct CustomizationContext { CustomizationCharacter character=CustomizationCharacter::Unknown; int slot=0; uint64_t generation=0; bool verified=false; };
struct CustomizationCapabilities { bool canReadContext=false,canLocate=false,canPreviewNative=false; };
// 本轮安全占位：没有游戏读取适配时返回未知。
std::optional<CustomizationContext> TryReadCustomizationContext();
// 本轮所有原生能力均禁用。
CustomizationCapabilities GetCustomizationCapabilities();
// 参数 token 为原始表 token；未知值保留为 Unknown。
CustomizationCharacter ParseCustomizationCharacter(const std::string& token);
CustomizationPart ParseCustomizationPart(const std::string& token);
// 参数 value 为角色或部位，返回中文显示名。
const char* CustomizationCharacterName(CustomizationCharacter value);
// 参数 value 为原始角色；服装搜索将衍生角色归入独立界面分类。
CustomizationCharacter ModuleSearchCharacter(CustomizationCharacter value);
const char* CustomizationPartName(CustomizationPart value);
// 参数 item/catalog 为实体和来源快照，缓存规范化检索键。
void PrepareCustomizationItem(CustomizationItem& item,const CustomizationCatalog& catalog);
// 参数 catalog/query 为不可变快照和完整条件，结果为快照索引。
std::vector<size_t> SearchCustomization(const CustomizationCatalog& catalog,const CustomizationQuery& query);
// 参数 home/game 为插件和游戏目录；后台只读当前启用Mod归档与可选兼容导入。
// 参数language为已认证游戏当前语言；独立预览未提供时使用导入设置或中文。
CustomizationCatalog LoadCustomizationCatalog(const fs::path& home,const fs::path& game,const std::string& language="");
// 参数data为PC官方经典字符串表；越界、无终止符或无效UTF-8时拒绝整表。
std::map<int,std::string> ReadCustomizationStrings(const std::string& data);
// 参数 path/kind/source/diagnostics 为已解包文本、数据库种类、来源与诊断接收器。
std::vector<CustomizationItem> ReadCustomizationTable(const fs::path& path,CustomizationKind kind,const std::string& source,std::vector<std::string>& diagnostics);
// 参数 text/kind/source/diagnostics 为受限内存文本、实体种类、来源和诊断；用于官方CPK中的表。
std::vector<CustomizationItem> ReadCustomizationText(const std::string& text,CustomizationKind kind,const std::string& source,std::vector<std::string>& diagnostics);
enum class CustomizationNavigation { Up,Down,Left,Right,Home,End,PageUp,PageDown };
struct CustomizationGridCursor { size_t selected=0; int intendedColumn=-1; };
// 参数 cursor/count/columns/pageRows/action 为游标、结果数、列数、页行数和单次有序动作。
void NavigateCustomizationGrid(CustomizationGridCursor& cursor,size_t count,int columns,int pageRows,CustomizationNavigation action);
}
