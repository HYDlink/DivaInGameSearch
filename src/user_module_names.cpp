#include "customization_names.h"
#include <Windows.h>
#include <toml.hpp>
#include <atomic>
#include <charconv>
#include <fstream>
#include <optional>

namespace diva {
namespace {
// 用户名称文件只处理模型稳定ID，不与发型、饰品共用编号；限制异常输入和生成文件大小。
constexpr size_t kMaximumFileBytes=8*1024*1024,kMaximumNameBytes=1024;
constexpr const char* kUserFile="module_names.toml";
constexpr const char* kReferenceFile="module_names_reference.toml";
constexpr const char* kUserSource="user:module-names";
std::atomic<uint64_t> referenceSequence{0};

// 参数 text 为UTF-8参照信息；将控制字符改为空格，确保用户名称不能产生新的配置行。
std::string CommentText(std::string text) {
    for(auto& ch:text) if(static_cast<unsigned char>(ch)<0x20 || ch==0x7f) ch=' ';
    return text;
}

// 参数 item/out 为最终物品定义与目录；返回便于用户辨认的来源显示名。
std::string DefinitionLabel(const CustomizationItem& item,const CustomizationCatalog& out) {
    if(item.definitionSource.starts_with("official:")) return Tr(Tx::source_official);
    for(const auto& source:out.sources) if(source.id==item.definitionSource) return source.name;
    return item.definitionSource;
}

// 参数 items/out 为尚未应用用户显示名的有效实体与目录；生成空白名称行及稳定ID、原名等参照注释。
std::string ReferenceRows(const CustomizationItemMap& items,const CustomizationCatalog& out) {
    std::string rows="[module]\n";
    for(const auto& [key,item]:items) if(key.kind==CustomizationKind::Module) {
        const auto original=item.rawFields.find("name");
        rows+=std::to_string(key.id)+" = \"\" # "+CommentText(item.name)+" | "+
            CommentText(original==item.rawFields.end()?std::string{}:original->second)+" | "+
            CommentText(CustomizationCharacterName(item.character))+" | "+CommentText(DefinitionLabel(item,out))+" | "+CommentText(item.cos)+"\n";
        if(rows.size()>kMaximumFileBytes) return {};
    }
    return rows=="[module]\n"?std::string{}:rows;
}

// 参数 path 为用户或参照文件；以有界方式读取原始字节，文件缺失、过大或读取失败返回空状态。
std::optional<std::string> ReadText(const fs::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    const auto size=input.tellg();if(size<0 || size>static_cast<std::streamoff>(kMaximumFileBytes)) return {};
    std::string text(static_cast<size_t>(size),'\0');input.seekg(0);
    if(size && !input.read(text.data(),size)) return {};
    return text;
}

// 参数 path/text 为新文件与UTF-8内容；仅在文件不存在时创建，永不覆盖用户已经填写的名称表。
bool CreateText(const fs::path& path,const std::string& text) {
    if(text.size()>kMaximumFileBytes) return false;
    const HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    DWORD written=0;const bool okay=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr) && written==text.size();
    CloseHandle(file);
    if(!okay) DeleteFileW(path.c_str());
    return okay;
}

// 参数 home/rows/out 为插件目录、参照行和诊断；用户模板只首次创建，自动参照表通过临时文件替换。
void UpdateReferences(const fs::path& home,const std::string& rows,CustomizationCatalog& out) {
    if(rows.empty()) return;
    std::error_code error;fs::create_directories(home,error);
    if(error) {out.diagnostics.push_back("无法创建服装名称表目录；已有搜索目录仍可使用。");return;}
    const auto userPath=home/kUserFile;
    if(!fs::exists(userPath,error) && !CreateText(userPath,"\xEF\xBB\xBF"+std::string(Tr(Tx::module_names_user_header))+rows))
        out.diagnostics.push_back("未能创建自定义服装名称表：module_names.toml。");
    const auto referencePath=home/kReferenceFile;
    const auto reference="\xEF\xBB\xBF"+std::string(Tr(Tx::module_names_reference_header))+rows;
    if(const auto previous=ReadText(referencePath);previous && *previous==reference) return;
    auto temporary=referencePath;
    temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++referenceSequence);
    if(!CreateText(temporary,reference)) {out.diagnostics.push_back("未能更新自动服装参照表；用户名称表保持原样。");return;}
    if(!MoveFileExW(temporary.c_str(),referencePath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());out.diagnostics.push_back("自动服装参照表替换失败；用户名称表保持原样。");
    }
}

// 参数 text 为完整编号文本；仅接受非负十进制稳定ID，拒绝溢出、符号和尾随字符。
std::optional<int> ModuleId(std::string_view text) {
    if(text.empty() || (text.size()>1 && text.front()=='0')) return {};
    int id=-1;const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),id);
    if(error!=std::errc() || end!=text.data()+text.size() || id<0 || id>100000) return {};
    return id;
}

// 参数 text 为用户名称；空白表示使用默认名称，控制字符与超长名称拒绝，不影响其他有效行。
bool ValidName(const std::string& text) {
    if(text.size()>kMaximumNameBytes || text.find_first_not_of(' ')==std::string::npos) return false;
    for(const auto ch:text) if(static_cast<unsigned char>(ch)<0x20 || ch==0x7f) return false;
    return true;
}
}

// 参数 items/home/out 为有效实体、插件目录与目录诊断；用户显示名拥有最高优先级，原显示名保留为检索别名。
void ApplyUserModuleNames(CustomizationItemMap& items,const fs::path& home,CustomizationCatalog& out) {
    UpdateReferences(home,ReferenceRows(items,out),out);
    const auto path=home/kUserFile;
    std::error_code error;if(!fs::exists(path,error)) return;
    const auto text=ReadText(path);
    if(!text) {out.diagnostics.push_back("自定义服装名称表无法读取或超过8MiB；使用原有名称。");return;}
    if(!text->empty() && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text->data(),static_cast<int>(text->size()),nullptr,0)) {
        out.diagnostics.push_back("自定义服装名称表不是有效UTF-8；使用原有名称。");return;
    }
    toml::table config;
    try {config=toml::parse(*text);}
    catch(const std::exception&) {out.diagnostics.push_back("自定义服装名称表TOML语法错误；使用原有名称，未改写用户文件。");return;}
    const auto modules=config["module"].as_table();
    if(!modules) {out.diagnostics.push_back("自定义服装名称表缺少[module]分组；使用原有名称。");return;}
    size_t applied=0,invalid=0,unknown=0;
    for(const auto& [number,value]:*modules) {
        const auto id=ModuleId(number.str());const auto name=value.value<std::string>();
        if(!id || !name) {++invalid;continue;}
        if(name->empty() || name->find_first_not_of(' ')==std::string::npos) continue;
        if(!ValidName(*name)) {++invalid;continue;}
        const auto found=items.find({CustomizationKind::Module,*id});
        if(found==items.end()) {++unknown;continue;}
        auto& item=found->second;item.aliases.push_back(item.name);item.name=*name;item.nameSource=kUserSource;
        for(auto& contribution:item.sources) if(contribution.role==CustomizationContributionRole::Name) contribution.effective=false;
        item.sources.push_back({kUserSource,CustomizationContributionRole::Name,true});++applied;
    }
    if(applied) out.sources.push_back({kUserSource,Tr(Tx::module_names_source),"","",false});
    // 用户名称不参与实体指纹，改名刷新不应被误判为游戏资源变更；目录代数仍会使旧查询失效。
    out.diagnostics.push_back("用户服装名称：采用 "+std::to_string(applied)+" 件，无效行 "+std::to_string(invalid)+
        " 行，未知编号 "+std::to_string(unknown)+" 个；仅影响搜索面板。");
}
}
