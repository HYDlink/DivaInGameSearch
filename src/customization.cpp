#include "customization.h"
#include <algorithm>
#include <limits>
#include <sstream>

namespace diva {
std::optional<CustomizationContext> TryReadCustomizationContext() { return std::nullopt; }
CustomizationCapabilities GetCustomizationCapabilities() { return {}; }
CustomizationCharacter ParseCustomizationCharacter(const std::string& token) {
    const std::array<std::string,12> tokens={"MIKU","RIN","LEN","LUKA","NERU","HAKU","KAITO","MEIKO","SAKINE","TETO","ALL","UNKNOWN"};
    for(size_t i=0;i<tokens.size();++i) if(tokens[i]==token) return static_cast<CustomizationCharacter>(i);
    return CustomizationCharacter::Unknown;
}
CustomizationPart ParseCustomizationPart(const std::string& token) {
    if(token=="ZUJO") return CustomizationPart::Head;
    if(token=="FACE") return CustomizationPart::Face;
    if(token=="NECK") return CustomizationPart::Neck;
    if(token=="BACK") return CustomizationPart::Back;
    if(token=="KAMI") return CustomizationPart::Hair;
    return CustomizationPart::Unknown;
}
const char* CustomizationCharacterName(CustomizationCharacter value) {
    static const char* names[]={"初音未来","镜音铃","镜音连","巡音流歌","亚北音留","弱音白","KAITO","MEIKO","咲音美子","重音Teto","共通 ALL","未知角色","衍生角色","随机"};
    auto index=static_cast<size_t>(value); return names[std::min(index,size_t(13))];
}
// 参数 value 为原始角色，返回服装搜索使用的游戏界面分类。
CustomizationCharacter ModuleSearchCharacter(CustomizationCharacter value) {
    if(value==CustomizationCharacter::Neru || value==CustomizationCharacter::Haku || value==CustomizationCharacter::Sakine || value==CustomizationCharacter::Teto) return CustomizationCharacter::Derived;
    return value;
}
const char* CustomizationPartName(CustomizationPart value) {
    // 玩家界面沿用原生自定义道具页的“头／脸／胸口／背部”；内部 Neck 枚举保持不变。
    static const char* names[]={"头","脸","胸口","背部","发型","未知部位"};
    return names[std::min(static_cast<size_t>(value),size_t(5))];
}
std::optional<CustomizationDomain> CustomizationItemDomain(const CustomizationItem& item) {
    if(item.key.kind==CustomizationKind::Module) return CustomizationDomain::Models;
    if(item.part==CustomizationPart::Hair) return CustomizationDomain::Hair;
    if(item.part==CustomizationPart::Head || item.part==CustomizationPart::Face || item.part==CustomizationPart::Neck || item.part==CustomizationPart::Back) return CustomizationDomain::Accessories;
    return std::nullopt;
}
CustomizationKind CustomizationDomainKind(CustomizationDomain domain) {
    return domain==CustomizationDomain::Models?CustomizationKind::Module:CustomizationKind::Accessory;
}
size_t CustomizationDomainIndex(CustomizationDomain domain) {
    return domain==CustomizationDomain::Models?0:domain==CustomizationDomain::Hair?1:2;
}
const char* CustomizationDomainName(CustomizationDomain domain) {
    return domain==CustomizationDomain::Models?"模型":domain==CustomizationDomain::Hair?"发型":"自定义道具";
}
const char* CustomizationDomainPageName(CustomizationDomain domain) {
    return domain==CustomizationDomain::Models?"模型页":domain==CustomizationDomain::Hair?"发型页":"自定义道具页";
}
const char* CustomizationContributionRoleName(CustomizationContributionRole role) {
    return role==CustomizationContributionRole::Name?"名称":"定义";
}
namespace {
// 参数 normalized 为 Normalize 后文本；含平假名（片假名已统一为平假名）时才视为可靠假名来源。
bool ContainsKana(const std::string& normalized) {
    for(auto ch:Wide(normalized)) if(ch>=0x3041 && ch<=0x3096) return true;
    return false;
}
// 参数 key/text 为检索键缓冲及真实名称、别名或显式读音；只转写假名，汉字原样保留，不推测读音。
void AppendRomanized(std::string& key,const std::string& text) {
    if(text.empty() || !ContainsKana(Normalize(text))) return;
    const auto roman=Romanize(text);
    key+=" "+roman+" "+RomanizeVariant(roman);
}
}
void PrepareCustomizationItem(CustomizationItem& item,const CustomizationCatalog& catalog) {
    std::string key=item.name+" "+std::to_string(item.key.id);
    // 所有真实名称与别名都参与检索；Normalize统一英文大小写、全半角及平片假名。
    for(const auto& [language,name]:item.namesByLanguage) key+=" "+name;
    for(const auto& alias:item.aliases) key+=" "+alias;
    if(auto original=item.rawFields.find("name");original!=item.rawFields.end()) key+=" "+original->second;
    // 显式读音只来自导入清单或表字段 reading，不由汉字名称生成。
    const auto reading=item.rawFields.find("reading");
    if(reading!=item.rawFields.end()) key+=" "+reading->second;
    // 罗马音键只转写名称／别名／读音中的真实假名，罗马音拼写变体与歌曲检索一致。
    std::string romanized;
    AppendRomanized(romanized,item.name);
    for(const auto& [language,name]:item.namesByLanguage) AppendRomanized(romanized,name);
    for(const auto& alias:item.aliases) AppendRomanized(romanized,alias);
    if(reading!=item.rawFields.end()) AppendRomanized(romanized,reading->second);
    item.searchKey=Normalize(key+romanized);item.nameKey=Normalize(item.name);item.modKey.clear();
    for(const auto& contribution:item.sources) {
        auto found=std::find_if(catalog.sources.begin(),catalog.sources.end(),[&](const auto& source){return source.id==contribution.source;});
        item.modKey+=" "+contribution.source;
        if(found!=catalog.sources.end()) item.modKey+=" "+found->name+" "+found->modDirectory;
    }
    item.modKey=Normalize(item.modKey);
}
std::vector<size_t> SearchCustomization(const CustomizationCatalog& catalog,const CustomizationQuery& query) {
    std::vector<size_t> out;std::vector<std::string> terms;std::istringstream words(query.text);std::string word;
    while(words>>word) {auto term=Normalize(word);if(!term.empty()) terms.push_back(term); }
    for(size_t index=0;index<catalog.items.size();++index) {
        const auto& item=catalog.items[index];const auto domain=CustomizationItemDomain(item);
        if(!domain || *domain!=query.domain) continue;
        // 模型沿用原生模型页分类；发型与道具按原始人物过滤，共通 ALL 匹配任一具体人物。
        if(query.character) {
            if(query.domain==CustomizationDomain::Models) {if(ModuleSearchCharacter(item.character)!=*query.character) continue;}
            else if(item.character!=*query.character && !(item.character==CustomizationCharacter::All && *query.character!=CustomizationCharacter::Unknown)) continue;
        }
        if(query.domain==CustomizationDomain::Accessories && query.part && item.part!=*query.part) continue;
        if(!query.source.empty() && std::none_of(item.sources.begin(),item.sources.end(),[&](const auto& source){return query.source=="official"?source.source.starts_with("official:"):source.source==query.source;})) continue;
        const auto& key=query.modMode?item.modKey:item.searchKey;
        if(std::any_of(terms.begin(),terms.end(),[&](const auto& term){return key.find(term)==std::string::npos;})) continue;
        out.push_back(index);
    }
    std::sort(out.begin(),out.end(),[&](size_t left,size_t right){
        const auto& a=catalog.items[left];const auto& b=catalog.items[right];
        if(query.sort==1) { if(a.nameKey!=b.nameKey) return a.nameKey<b.nameKey; }
        if(query.sort==0) {if(a.sortIndex.has_value()!=b.sortIndex.has_value()) return a.sortIndex.has_value();if(a.sortIndex && a.sortIndex!=b.sortIndex) return *a.sortIndex<*b.sortIndex;}
        if(a.key.id!=b.key.id) return a.key.id<b.key.id;
        return a.definitionSource<b.definitionSource;
    });
    return out;
}
void NavigateCustomizationGrid(CustomizationGridCursor& cursor,size_t count,int columns,int pageRows,CustomizationNavigation action) {
    columns=std::max(1,columns);pageRows=std::max(1,pageRows);
    if(!count) { cursor={};return; }
    cursor.selected=std::min(cursor.selected,count-1);
    auto row=cursor.selected/size_t(columns),column=cursor.selected%size_t(columns);
    if(cursor.intendedColumn<0) cursor.intendedColumn=static_cast<int>(column);
    const auto lastRow=(count-1)/size_t(columns);
    switch(action) {
    case CustomizationNavigation::Home:cursor.selected=0;cursor.intendedColumn=0;return;
    case CustomizationNavigation::End:cursor.selected=count-1;cursor.intendedColumn=static_cast<int>(cursor.selected%columns);return;
    case CustomizationNavigation::Left: if(column) --cursor.selected;cursor.intendedColumn=static_cast<int>(cursor.selected%columns);return;
    case CustomizationNavigation::Right:if(column+1<size_t(columns) && cursor.selected+1<count) ++cursor.selected;cursor.intendedColumn=static_cast<int>(cursor.selected%columns);return;
    case CustomizationNavigation::Up:if(row) --row;break;
    case CustomizationNavigation::Down:row=std::min(row+1,lastRow);break;
    case CustomizationNavigation::PageUp:row-=std::min(row,size_t(pageRows));break;
    case CustomizationNavigation::PageDown:row=std::min(row+size_t(pageRows),lastRow);break;
    }
    cursor.selected=std::min(row*columns+size_t(std::clamp(cursor.intendedColumn,0,columns-1)),count-1);
}
}
