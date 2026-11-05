#pragma once
#include "i18n_keys.h"
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>
namespace diva {
// 模板占位符的名称和值；值已转为 UTF-8 文本。
struct TrArg { std::string_view name; std::string value; };
// 参数 id 为文本 ID，返回当前语言文本；指针在进程存活期内有效。未初始化时返回 zh-CN。
const char* Tr(Tx id);
// 参数 id/args 为文本 ID 和占位符值，返回替换 {name} 后的整句。翻译不得作为 printf 格式串使用。
std::string Trf(Tx id,std::initializer_list<TrArg> args);
// 参数 home/requested 为 Mod 目录和显式语言；requested 为空时读 settings.ini 的 language，auto 跟随系统语言。
// 只在启动后台线程之前调用；语言在进程内不再切换。
void InitLocale(const std::filesystem::path& home,std::string_view requested={});
// 返回实际生效的语言代码，未初始化时为 zh-CN。
const std::string& CurrentLocale();
// 返回当前界面语言对应的游戏语言表代码：简中 cn、日文 jp、其余 en。
const char* GameTableLanguage();
// 当前界面是否优先显示英文歌名；简中和日文界面仍以原名为主。
bool PrefersEnglishTitles();
// 返回最近一次 InitLocale 的诊断信息，写入诊断日志，不显示给玩家。
const std::vector<std::string>& LocaleDiagnostics();
}
