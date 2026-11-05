// 多语言运行时验收：默认语言、回退、模板替换、外部语言文件和伪翻译。
#include "i18n.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <Windows.h>
namespace fs=std::filesystem;
using namespace diva;
static int failures=0;
// 参数 condition/message 为断言条件和失败说明。
static void Check(bool condition,const char* message) {
    if(condition) return;
    ++failures;std::printf("失败：%s\n",message);
}
// 参数 path/text 为测试文件路径和 UTF-8 内容。
static void Write(const fs::path& path,const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path,std::ios::binary)<<text;
}
int main() {
    SetConsoleOutputCP(CP_UTF8);
    auto root=fs::temp_directory_path()/("diva-i18n-check-"+std::to_string(GetCurrentProcessId()));
    fs::remove_all(root);

    // 未初始化时固定为源语言，现有测试依赖这一点。
    Check(CurrentLocale()=="zh-CN","未初始化时不是 zh-CN");
    Check(std::string(Tr(Tx::tab_songs))=="歌曲","未初始化时没有返回中文");

    InitLocale(root,"en");
    Check(CurrentLocale()=="en","显式 en 没有生效");
    Check(std::string(Tr(Tx::tab_songs))=="Songs","en 文本不对");
    Check(Trf(Tx::song_results,{{"count","3"}})=="Results: 3","模板替换不对");
    // 值里的花括号不能被二次替换，未提供的占位符保持原样，便于发现遗漏。
    Check(Trf(Tx::song_results,{{"count","{count}"}})=="Results: {count}","占位符值被二次替换");
    Check(Trf(Tx::song_results,{})=="Results: {count}","缺少参数时没有保留占位符");

    InitLocale(root,"xx-unknown");
    Check(CurrentLocale()=="en","未知语言没有回退 en");
    InitLocale(root,"../evil");
    Check(CurrentLocale()=="en","非法语言代码没有回退 en");

    // settings.ini 决定默认语言；显式参数优先。
    Write(root/"settings.ini","# comment\nfont_size=19\nlanguage=zh-CN\r\n");
    InitLocale(root);
    Check(CurrentLocale()=="zh-CN","没有读取 settings.ini 的 language");
    InitLocale(root,"en");
    Check(CurrentLocale()=="en","显式语言没有优先于设置文件");

    // 外部语言文件：部分覆盖、缺失回退 en、不合规条目被跳过而不是崩溃。
    Write(root/"lang/fr.toml","[tab]\nsongs = \"Chansons\"\nno_such_key = \"x\"\n[song]\nresults = \"{wrong} résultats\"\n");
    InitLocale(root,"fr");
    Check(CurrentLocale()=="fr","外部语言文件没有生效");
    Check(std::string(Tr(Tx::tab_songs))=="Chansons","外部翻译没有覆盖");
    Check(std::string(Tr(Tx::tab_modules))=="Modules","缺失的键没有回退 en");
    Check(std::string(Tr(Tx::song_results))=="Results: {count}","占位符不一致的翻译没有被跳过");
    bool unknownKey=false,badPlaceholder=false;
    for(const auto& line:LocaleDiagnostics()) {
        unknownKey=unknownKey || line.find("tab_no_such_key")!=std::string::npos;
        badPlaceholder=badPlaceholder || line.find("song_results")!=std::string::npos;
    }
    Check(unknownKey && badPlaceholder,"不合规条目没有写入诊断");

    // 损坏的语言文件不能影响启动。
    Write(root/"lang/de.toml","[tab\nbroken");
    InitLocale(root,"de");
    Check(CurrentLocale()=="en","损坏的语言文件没有回退 en");

    // 伪翻译加长文本并保留占位符。
    InitLocale(root,"qps");
    Check(CurrentLocale()=="qps","伪翻译没有生效");
    Check(std::string(Tr(Tx::tab_songs)).find("Songs")!=std::string::npos,"伪翻译丢失原文");
    Check(Trf(Tx::song_results,{{"count","3"}}).find("Results: 3")!=std::string::npos,"伪翻译破坏了占位符");

    std::error_code error;fs::remove_all(root,error);
    if(failures) return 1;
    std::printf("多语言运行时验收通过。\n");
    return 0;
}
