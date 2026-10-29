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
    return CustomizationPart::Unknown;
}
const char* CustomizationCharacterName(CustomizationCharacter value) {
    static const char* names[]={"初音未来","镜音铃","镜音连","巡音流歌","亚北音留","弱音白","KAITO","MEIKO","咲音美子","重音Teto","共通 ALL","未知角色"};
    auto index=static_cast<size_t>(value); return names[std::min(index,size_t(11))];
}
const char* CustomizationPartName(CustomizationPart value) {
    static const char* names[]={"头部","脸部","胸颈","背部","未知部位"};
    return names[std::min(static_cast<size_t>(value),size_t(4))];
}
void PrepareCustomizationItem(CustomizationItem& item,const CustomizationCatalog& catalog) {
    std::string key=item.name+" "+std::to_string(item.key.id);
    for(const auto& [language,name]:item.namesByLanguage) { (void)language;key+=" "+name; }
    for(const auto& name:item.aliases) key+=" "+name;
    item.searchKey=Normalize(key);item.nameKey=Normalize(item.name);item.modKey.clear();
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
        const auto& item=catalog.items[index]; if(item.key.kind!=query.kind) continue;
        if(query.character && item.character!=*query.character && !(query.kind==CustomizationKind::Accessory && item.character==CustomizationCharacter::All && *query.character!=CustomizationCharacter::Unknown)) continue;
        if(query.kind==CustomizationKind::Accessory && query.part && item.part!=*query.part) continue;
        if(!query.source.empty() && std::none_of(item.sources.begin(),item.sources.end(),[&](const auto& source){return source.source==query.source;})) continue;
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
