#pragma once
#include "core.h"
#include "i18n.h"
#include <optional>
#include <functional>
#include <memory>
#include <cstdint>

namespace diva {
// 服装和饰品拥有独立编号空间，零也是合法编号；发型与四部位道具同属 cstm_item 编号空间。
enum class CustomizationKind { Module, Accessory };
enum class CustomizationCharacter { Miku,Rin,Len,Luka,Neru,Haku,Kaito,Meiko,Sakine,Teto,All,Unknown,Derived,Random };
// 原始 parts 令牌：ZUJO/FACE/NECK/BACK 为道具部位，KAMI 为发型；未知令牌保留 Unknown，不猜测归属。
enum class CustomizationPart { Head,Face,Neck,Back,Hair,Unknown };
// 玩家可见的三个独立定制领域；实体身份仍为 CustomizationKey，领域只是投影，不另建 ID 空间。
enum class CustomizationDomain { Models, Hair, Accessories };
struct CustomizationKey { CustomizationKind kind=CustomizationKind::Module; int id=0; auto operator<=>(const CustomizationKey&) const=default; };
struct CustomizationSource { std::string id,name,modDirectory,includeRoot; bool currentMod=false; };
// 来源贡献的角色；逻辑判断只用枚举，显示文字由 CustomizationContributionRoleName 提供。
enum class CustomizationContributionRole { Definition, Name };
struct CustomizationContribution { std::string source; CustomizationContributionRole role=CustomizationContributionRole::Definition; bool effective=false; };
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
    std::string configurationKey,dmlFingerprint,catalogFingerprint,language=GameTableLanguage();
    bool stale=false;
    size_t orphanNames=0,unimportedArchives=0;
    bool importedDirectory=true;
};
struct CustomizationQuery {
    // 检索领域：模型=服装实体；发型=cstm_item 且 KAMI；道具=cstm_item 且四部位之一。
    CustomizationDomain domain=CustomizationDomain::Models;
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
// 参数 item 为实体；返回所属检索领域，未知部位的 cstm_item 不归入发型或道具。
std::optional<CustomizationDomain> CustomizationItemDomain(const CustomizationItem& item);
// 参数 domain 为检索领域，返回其实体种类（发型与道具均为 Accessory）。
CustomizationKind CustomizationDomainKind(CustomizationDomain domain);
// 参数 domain 为检索领域，返回状态数组下标 0/1/2。
size_t CustomizationDomainIndex(CustomizationDomain domain);
// 参数 domain 为检索领域，返回玩家可见类别名“模型／发型／自定义道具”。
const char* CustomizationDomainName(CustomizationDomain domain);
// 参数 domain 为检索领域，返回对应原生子页名称，用于跨页提示。
const char* CustomizationDomainPageName(CustomizationDomain domain);
// 参数 role 为来源贡献角色，返回玩家可见的角色名称。
const char* CustomizationContributionRoleName(CustomizationContributionRole role);
// 参数 item/catalog 为实体和来源快照，缓存规范化检索键。
void PrepareCustomizationItem(CustomizationItem& item,const CustomizationCatalog& catalog);
// 参数 catalog/query 为不可变快照和完整条件，结果为快照索引。
std::vector<size_t> SearchCustomization(const CustomizationCatalog& catalog,const CustomizationQuery& query);
// 官方 CPK 中的一份实体表：物理归档、内部条目、逻辑路径、UTF-8 文本和指纹。
struct OfficialEntityTable {
    fs::path cpkPath;
    std::string internalPath;
    std::string logicalPath;
    std::string tableText;
    std::string fingerprint;
};
// 官方实体表读取入口；生产使用随包 CPK 桥接，隔离测试注入合成表而不访问游戏目录。
struct OfficialTableSource {
    // 参数 game/home/diagnostics 为游戏目录、插件目录与诊断接收器；返回官方模型表候选。
    std::function<std::vector<OfficialEntityTable>(const fs::path&,const fs::path&,std::vector<std::string>&)> modules;
    // 参数同上；返回官方 gm_customize_item_id.bin 候选（发型与四部位道具）。
    std::function<std::vector<OfficialEntityTable>(const fs::path&,const fs::path&,std::vector<std::string>&)> customizeItems;
};
// 返回生产官方表来源：随包 CPK 桥接读取模型表及定制表。
OfficialTableSource ProductionOfficialTables();
// 参数 home/game 为插件和游戏目录；后台只读当前启用Mod归档与可选兼容导入。
// 参数language为显式目录语言；玩家界面传界面语言对应的语言表，审计未提供时使用导入设置或界面语言。
CustomizationCatalog LoadCustomizationCatalog(const fs::path& home,const fs::path& game,const std::string& language="");
// 参数 home/game/language 同上，official 为官方表来源；测试可注入合成官方表。
CustomizationCatalog LoadCustomizationCatalog(const fs::path& home,const fs::path& game,const std::string& language,const OfficialTableSource& official);
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
