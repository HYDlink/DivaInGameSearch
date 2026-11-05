// 由 locales/*.toml 生成文本 ID 枚举和内嵌语言数据；任何一致性错误都会让构建失败。
#include <toml.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
namespace fs=std::filesystem;
using Flat=std::map<std::string,std::string>;

// 参数 table/prefix 为当前 TOML 表和键名前缀，out 收集展开结果，file/errors 用于报错。
static void Flatten(const toml::table& table,const std::string& prefix,Flat& out,const std::string& file,std::vector<std::string>& errors) {
    for(const auto& [key,node]:table) {
        const std::string name=prefix+std::string(key.str());
        if(name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos) errors.push_back(file+"：键名只能用小写字母、数字和下划线："+name);
        if(auto child=node.as_table()) Flatten(*child,name+"_",out,file,errors);
        else if(auto value=node.value<std::string>()) {
            if(!out.emplace(name,*value).second) errors.push_back(file+"：展开后键名重复："+name);
        }
        else errors.push_back(file+"：值必须是字符串："+name);
    }
}
// 参数 text 为模板文本，返回其中 {name} 形式的占位符集合。
static std::set<std::string> Placeholders(const std::string& text) {
    std::set<std::string> out;
    for(size_t open=text.find('{');open!=std::string::npos;open=text.find('{',open+1)) {
        const size_t close=text.find('}',open);
        if(close==std::string::npos) break;
        const auto name=text.substr(open+1,close-open-1);
        if(!name.empty() && name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos) out.insert(name);
    }
    return out;
}
// 参数 text 为任意 UTF-8 文本，返回可放入 C++ 字符串字面量的形式。
static std::string Escape(const std::string& text) {
    std::string out;
    for(char c:text) {
        if(c=='\\') out+="\\\\";
        else if(c=='"') out+="\\\"";
        else if(c=='\n') out+="\\n";
        else if(c=='\r') out+="\\r";
        else if(c=='\t') out+="\\t";
        else out+=c;
    }
    return out;
}
// 参数 path/content 为输出文件和内容；用二进制写入，保持 UTF-8 原样。
static void WriteFile(const fs::path& path,const std::string& content) {
    std::ofstream out(path,std::ios::binary);
    out<<content;
    if(!out.good()) throw std::runtime_error("写入失败："+path.string());
}

// 参数 argv[1]/argv[2] 为语言目录和输出目录。
int main(int argc,char** argv) {
    if(argc!=3) {std::cerr<<"用法：I18nGen <locales 目录> <输出目录>\n";return 2;}
    const fs::path localeDir=argv[1],outDir=argv[2];
    std::map<std::string,Flat> locales;
    std::vector<std::string> errors;
    for(const auto& entry:fs::directory_iterator(localeDir)) {
        if(entry.path().extension()!=".toml") continue;
        const auto code=entry.path().stem().string();
        try {
            std::ifstream stream(entry.path(),std::ios::binary);
            Flatten(toml::parse(stream,entry.path().string()),"",locales[code],code,errors);
        } catch(const toml::parse_error& error) {
            std::ostringstream text;text<<error;errors.push_back(code+"：TOML 解析失败："+text.str());
        }
    }
    if(!locales.contains("zh-CN") || !locales.contains("en")) errors.push_back("缺少 zh-CN.toml 或 en.toml");
    else {
        const auto& source=locales["zh-CN"];
        for(const auto& [code,values]:locales) {
            if(code=="zh-CN") continue;
            for(const auto& [key,text]:values) {
                auto found=source.find(key);
                if(found==source.end()) errors.push_back(code+"：源语言没有这个键："+key);
                else if(Placeholders(text)!=Placeholders(found->second)) errors.push_back(code+"：占位符与源语言不一致："+key);
            }
            // en 是所有语言的回退，必须完整；其他语言允许缺失，缺失时回退 en。
            if(code=="en") for(const auto& [key,text]:source) if(!values.contains(key)) errors.push_back("en：缺少键："+key);
        }
    }
    if(!errors.empty()) {
        for(const auto& error:errors) std::cerr<<"i18n 错误："<<error<<"\n";
        return 1;
    }
    const auto& source=locales["zh-CN"];
    std::ostringstream keys,data;
    keys<<"// 由 I18nGen 生成，请勿手改；修改 locales/*.toml。\n#pragma once\nnamespace diva {\nenum class Tx : int {\n";
    for(const auto& [key,text]:source) keys<<"    "<<key<<",\n";
    keys<<"};\ninline constexpr int kTxCount="<<source.size()<<";\n}\n";
    data<<"// 由 I18nGen 生成，请勿手改；修改 locales/*.toml。\nstatic const char* const kTxNames[]={\n";
    for(const auto& [key,text]:source) data<<"    \""<<key<<"\",\n";
    data<<"};\n";
    int index=0;
    std::ostringstream table;
    for(const auto& [code,values]:locales) {
        data<<"static const char* const kLocaleValues"<<index<<"[]={\n";
        for(const auto& [key,text]:source) {
            auto found=values.find(key);
            if(found==values.end()) data<<"    nullptr,\n";
            else data<<"    \""<<Escape(found->second)<<"\",\n";
        }
        data<<"};\n";
        table<<"    {\""<<code<<"\",kLocaleValues"<<index<<"},\n";
        ++index;
    }
    data<<"struct EmbeddedLocale { const char* code; const char* const* values; };\nstatic const EmbeddedLocale kEmbeddedLocales[]={\n"<<table.str()<<"};\n";
    fs::create_directories(outDir);
    WriteFile(outDir/"i18n_keys.h",keys.str());
    WriteFile(outDir/"i18n_data.inc",data.str());
    std::cout<<"i18n：已生成 "<<source.size()<<" 个文本 ID，"<<locales.size()<<" 种语言。\n";
    return 0;
}
