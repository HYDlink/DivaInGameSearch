#include "i18n.h"
#include <toml.hpp>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <set>
#include <sstream>
#include <Windows.h>
namespace diva {
namespace {
#include "i18n_data.inc"
namespace fs=std::filesystem;
// 语言表；文本指针对外长期有效，因此生效后不释放。
struct LocaleTable { std::string code; std::vector<std::string> values; };
std::atomic<const LocaleTable*> current{nullptr};
std::vector<std::string> diagnostics;

// 参数 code 为语言代码，返回内嵌文本数组，不存在时为 nullptr。
const char* const* FindEmbedded(std::string_view code) {
    for(const auto& locale:kEmbeddedLocales) if(code==locale.code) return locale.values;
    return nullptr;
}
// 返回内嵌源语言文本；未调用 InitLocale 时使用，测试因此固定为 zh-CN。
const char* const* SourceValues() {
    static const char* const* values=FindEmbedded("zh-CN");
    return values;
}
// 参数 name 为展开后的键名，返回文本 ID 下标，不存在时为 -1。
int FindKey(std::string_view name) {
    const auto begin=std::begin(kTxNames),end=std::end(kTxNames);
    const auto found=std::lower_bound(begin,end,name,[](const char* left,std::string_view right){return std::string_view(left)<right;});
    return found!=end && name==*found?static_cast<int>(found-begin):-1;
}
// 参数 text 为模板文本，返回其中 {name} 形式的占位符集合。
std::set<std::string> Placeholders(std::string_view text) {
    std::set<std::string> out;
    for(size_t open=text.find('{');open!=std::string_view::npos;open=text.find('{',open+1)) {
        const size_t close=text.find('}',open);
        if(close==std::string_view::npos) break;
        const auto name=text.substr(open+1,close-open-1);
        if(!name.empty() && name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==std::string_view::npos) out.insert(std::string(name));
    }
    return out;
}
// 参数 home 为 Mod 目录，返回 settings.ini 的 language 值，未设置时为空。
std::string ReadLanguageSetting(const fs::path& home) {
    std::ifstream in(home/"settings.ini");std::string line;
    while(std::getline(in,line)) {
        if(!line.starts_with("language=")) continue;
        auto value=line.substr(9);
        while(!value.empty() && (value.back()=='\r' || value.back()==' ')) value.pop_back();
        return value;
    }
    return {};
}
// 按 Windows 界面语言选择语言代码；只支持简中、日文和英文，其余语言使用英文。
std::string SystemLocale() {
    switch(PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_CHINESE: return "zh-CN";
    case LANG_JAPANESE: return "ja";
    default: return "en";
    }
}
// 参数 text 为英文文本，返回加长约 40% 并带重音字符的伪翻译，用于检查布局和遗漏；保留占位符。
std::string Pseudo(const std::string& text) {
    return "[ÀÉ "+text+" "+std::string(std::max<size_t>(2,text.size()*2/5),'~')+"]";
}
// 参数 table/prefix/values 为 TOML 表、键名前缀和待覆盖文本；不合规的条目跳过并写入诊断。
void ApplyExternal(const toml::table& table,const std::string& prefix,std::vector<std::string>& values) {
    for(const auto& [key,node]:table) {
        const std::string name=prefix+std::string(key.str());
        if(auto child=node.as_table()) {ApplyExternal(*child,name+"_",values);continue;}
        const int index=FindKey(name);
        auto value=node.value<std::string>();
        if(index<0) diagnostics.push_back("语言文件键不存在，已跳过："+name);
        else if(!value) diagnostics.push_back("语言文件值不是字符串，已跳过："+name);
        else if(Placeholders(*value)!=Placeholders(SourceValues()[index])) diagnostics.push_back("语言文件占位符与源语言不一致，已跳过："+name);
        else values[index]=*value;
    }
}
}
const char* Tr(Tx id) {
    const int index=static_cast<int>(id);
    if(const auto* table=current.load(std::memory_order_acquire)) return table->values[index].c_str();
    return SourceValues()[index];
}
std::string Trf(Tx id,std::initializer_list<TrArg> args) {
    const std::string_view text=Tr(id);
    std::string out;out.reserve(text.size()+32);
    size_t position=0;
    while(position<text.size()) {
        const size_t open=text.find('{',position);
        if(open==std::string_view::npos) break;
        out+=text.substr(position,open-position);
        position=open;
        const size_t close=text.find('}',open);
        const auto name=close==std::string_view::npos?std::string_view{}:text.substr(open+1,close-open-1);
        const auto arg=std::find_if(args.begin(),args.end(),[&](const TrArg& candidate){return candidate.name==name;});
        // 只扫描模板本身，值里的花括号（如歌名）不会被二次替换。
        if(arg!=args.end()) {out+=arg->value;position=close+1;}
        else {out+='{';position=open+1;}
    }
    out+=text.substr(position);
    return out;
}
void InitLocale(const std::filesystem::path& home,std::string_view requested) {
    diagnostics.clear();
    std::string wanted(requested);
    if(wanted.empty()) wanted=ReadLanguageSetting(home);
    if(wanted.empty() || wanted=="auto") wanted=SystemLocale();
    // 语言代码会拼进文件路径，只接受字母、数字和连字符。
    const bool validCode=wanted.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-")==std::string::npos;
    const auto* english=FindEmbedded("en");
    const auto* embedded=validCode?FindEmbedded(wanted):nullptr;
    auto table=std::make_unique<LocaleTable>();
    table->code=embedded?wanted:"en";
    table->values.resize(kTxCount);
    for(int i=0;i<kTxCount;++i) table->values[i]=embedded && embedded[i]?embedded[i]:english[i];
    bool known=embedded!=nullptr;
    if(wanted=="qps") {
        for(int i=0;i<kTxCount;++i) table->values[i]=Pseudo(english[i]);
        table->code=wanted;known=true;
    }
    else if(validCode) {
        // 外部语言文件覆盖内嵌文本，翻译者不用编译就能试用自己的翻译。
        const auto path=home/"lang"/(wanted+".toml");
        std::error_code error;
        if(fs::exists(path,error)) try {
            std::ifstream stream(path,std::ios::binary);
            ApplyExternal(toml::parse(stream,path.string()),"",table->values);
            table->code=wanted;known=true;
        } catch(const toml::parse_error& parseError) {
            std::ostringstream text;text<<parseError;
            diagnostics.push_back("语言文件解析失败："+text.str());
        }
    }
    if(!known) diagnostics.push_back("没有语言 "+wanted+" 的文本，已回退 en。");
    diagnostics.push_back("界面语言："+table->code);
    // 有意不释放旧表：Tr 返回的指针可能仍被保存。
    current.store(table.release(),std::memory_order_release);
}
bool ApplyGameLanguage(const std::filesystem::path& home,std::string_view gameLanguage) {
    const auto setting=ReadLanguageSetting(home);
    if((!setting.empty() && setting!="auto") || gameLanguage.empty()) return false;
    const std::string locale=gameLanguage=="cn" || gameLanguage=="tw"?"zh-CN":gameLanguage=="jp"?"ja":"en";
    if(locale==CurrentLocale()) return false;
    InitLocale(home,locale);
    return true;
}
const std::string& CurrentLocale() {
    static const std::string source="zh-CN";
    const auto* table=current.load(std::memory_order_acquire);
    return table?table->code:source;
}
const char* GameTableLanguage() {
    const auto& code=CurrentLocale();
    return code=="zh-CN"?"cn":code=="ja"?"jp":"en";
}
bool PrefersEnglishTitles() {
    const auto& code=CurrentLocale();
    return code!="zh-CN" && code!="ja";
}
const std::vector<std::string>& LocaleDiagnostics() {return diagnostics;}
}
