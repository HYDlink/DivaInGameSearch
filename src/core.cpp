#include "core.h"
#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace diva {
// 将有效 UTF-8 转换为 UTF-16；参数 text 为输入内容。
std::wstring Wide(const std::string& text) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0);
    std::wstring out(n, 0);
    if (n) MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), n);
    return out;
}
// 将 UTF-16 转换为 UTF-8；参数 text 为输入内容。
std::string Utf8(const std::wstring& text) {
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0);
    if (n) WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), n, nullptr, nullptr);
    return out;
}
// 规范化检索字符；参数 text 为 UTF-8 文本。
std::string Normalize(const std::string& text) {
    auto w = Wide(text);
    if (w.empty()) return {};
    int n = NormalizeString(NormalizationKC, w.data(), (int)w.size(), nullptr, 0);
    if (n > 0) {
        std::wstring tmp(n, 0);
        int count = NormalizeString(NormalizationKC, w.data(), (int)w.size(), tmp.data(), n);
        if (count > 0) { tmp.resize(count); w = tmp; }
    }
    n=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr,0);
    if(n>0) {
        std::wstring lower(n,0);
        if(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,w.data(),(int)w.size(),lower.data(),n,nullptr,nullptr,0)>0) w=lower;
    }
    std::wstring out;
    for (auto ch : w) {
        if (ch >= L'ァ' && ch <= L'ヶ') ch -= 0x60;
        if (ch >= L'A' && ch <= L'Z') ch += 32;
        if (ch == L'ō' || ch == L'ô') { out += L"ou"; continue; }
        if (ch == L'ū' || ch == L'û') { out += L"uu"; continue; }
        if (ch <= 0x20 || ch == L'・' || ch == L'_' || ch == L'-' || ch == L'\'' || ch == L'　') continue;
        out += ch;
    }
    return Utf8(out);
}
// 转写假名并处理拗音、促音和长音；参数 text 为有据可查的读音。
std::string Romanize(const std::string& text) {
    static const std::map<wchar_t, std::string> table = {
        {L'あ',"a"},{L'い',"i"},{L'う',"u"},{L'え',"e"},{L'お',"o"},
        {L'か',"ka"},{L'き',"ki"},{L'く',"ku"},{L'け',"ke"},{L'こ',"ko"},
        {L'さ',"sa"},{L'し',"shi"},{L'す',"su"},{L'せ',"se"},{L'そ',"so"},
        {L'た',"ta"},{L'ち',"chi"},{L'つ',"tsu"},{L'て',"te"},{L'と',"to"},
        {L'な',"na"},{L'に',"ni"},{L'ぬ',"nu"},{L'ね',"ne"},{L'の',"no"},
        {L'は',"ha"},{L'ひ',"hi"},{L'ふ',"fu"},{L'へ',"he"},{L'ほ',"ho"},
        {L'ま',"ma"},{L'み',"mi"},{L'む',"mu"},{L'め',"me"},{L'も',"mo"},
        {L'や',"ya"},{L'ゆ',"yu"},{L'よ',"yo"},{L'ら',"ra"},{L'り',"ri"},{L'る',"ru"},{L'れ',"re"},{L'ろ',"ro"},
        {L'わ',"wa"},{L'を',"wo"},{L'ん',"n"},{L'が',"ga"},{L'ぎ',"gi"},{L'ぐ',"gu"},{L'げ',"ge"},{L'ご',"go"},
        {L'ざ',"za"},{L'じ',"ji"},{L'ず',"zu"},{L'ぜ',"ze"},{L'ぞ',"zo"},{L'だ',"da"},{L'ぢ',"ji"},{L'づ',"zu"},{L'で',"de"},{L'ど',"do"},
        {L'ば',"ba"},{L'び',"bi"},{L'ぶ',"bu"},{L'べ',"be"},{L'ぼ',"bo"},{L'ぱ',"pa"},{L'ぴ',"pi"},{L'ぷ',"pu"},{L'ぺ',"pe"},{L'ぽ',"po"},
        {L'ぁ',"a"},{L'ぃ',"i"},{L'ぅ',"u"},{L'ぇ',"e"},{L'ぉ',"o"},{L'ゃ',"ya"},{L'ゅ',"yu"},{L'ょ',"yo"},{L'ゔ',"vu"}};
    auto w = Wide(Normalize(text));
    static const std::map<std::wstring,std::string> foreign={
        {L"しぇ","she"},{L"じぇ","je"},{L"ちぇ","che"},{L"てぃ","ti"},{L"でぃ","di"},{L"とぅ","tu"},{L"どぅ","du"},
        {L"ふぁ","fa"},{L"ふぃ","fi"},{L"ふぇ","fe"},{L"ふぉ","fo"},{L"うぃ","wi"},{L"うぇ","we"},{L"うぉ","wo"},
        {L"ゔぁ","va"},{L"ゔぃ","vi"},{L"ゔぇ","ve"},{L"ゔぉ","vo"}};
    std::string out;
    bool doubleNext = false;
    for (size_t i = 0; i < w.size(); ++i) {
        if (w[i] == L'っ') { doubleNext = true; continue; }
        if (w[i] == L'ー') { if (!out.empty()) out += out.back(); continue; }
        if(i+1<w.size()) {
            if(auto pair=foreign.find(w.substr(i,2));pair!=foreign.end()) {
                if(doubleNext) out+=pair->second.front(); out+=pair->second;doubleNext=false;++i;continue;
            }
        }
        auto it = table.find(w[i]);
        if (it == table.end()) { out += Utf8(std::wstring(1, w[i])); doubleNext = false; continue; }
        auto syllable = it->second;
        if (i + 1 < w.size() && (w[i+1] == L'ゃ' || w[i+1] == L'ゅ' || w[i+1] == L'ょ') && syllable.back() == 'i') {
            syllable.pop_back();
            if (syllable == "sh" || syllable == "ch" || syllable == "j") syllable += table.at(w[++i]).substr(1);
            else syllable += table.at(w[++i]);
        }
        if (doubleNext && !syllable.empty()) out += syllable.front();
        doubleNext = false; out += syllable;
    }
    return out;
}
// 生成常见罗马音变体；参数 text 为标准罗马音。
static std::string Variant(std::string text) {
    const std::pair<std::string,std::string> pairs[] = {{"shi","si"},{"chi","ti"},{"tsu","tu"},{"fu","hu"},{"ji","zi"},{"ou","o"},{"oo","o"},{"uu","u"}};
    for (const auto& [a,b] : pairs) { size_t p=0; while ((p=text.find(a,p))!=std::string::npos) { text.replace(p,a.size(),b); p+=b.size(); } }
    return text;
}
// 缓存歌曲和来源搜索键；参数 song 为待发布歌曲。
void PrepareSong(Song& song) {
    auto r = Romanize(song.reading);
    auto aliases=song.aliases; std::replace(aliases.begin(),aliases.end(),'|','\n');
    std::istringstream aliasLines(aliases); std::string line,aliasKey;
    while(std::getline(aliasLines,line)) aliasKey+=Normalize(line)+"\n";
    song.key = Normalize(song.name)+"\n"+Normalize(song.english)+"\n"+Normalize(song.reading)+"\n"+r+"\n"+Variant(r)+"\n"+aliasKey;
    song.modKey.clear();
    for (const auto& source : song.sources) song.modKey += Normalize(source)+"\n";
}
// 完整过滤后进行稳定全局排序；参数 catalog/query/favorites 为曲库、条件与收藏。
std::vector<Result> Search(const Catalog& catalog, const Query& query, const std::set<int>& favorites) {
    std::vector<Result> out;
    auto needle = Normalize(query.text);
    int style=std::clamp(query.style,0,2),difficulty=query.difficulty<0?-1:std::clamp(query.difficulty,0,4);
    std::istringstream tokens(query.text);
    std::vector<std::string> words; std::string word;
    while (tokens >> word) words.push_back(Normalize(word));
    for (size_t i=0;i<catalog.songs.size();++i) {
        const auto& s=catalog.songs[i];
        if (s.hidden || (query.favoritesOnly && !favorites.contains(s.id))) continue;
        if (!query.pack.empty() && std::find(s.sources.begin(),s.sources.end(),query.pack)==s.sources.end()) continue;
        const auto& charts=s.charts[style];
        if (difficulty>=0 && !charts[difficulty].available) continue;
        if (std::none_of(charts.begin(),charts.end(),[](const Chart& c){ return c.available; })) continue;
        const auto& key=query.modMode?s.modKey:s.key;
        auto id=std::to_string(s.id); int score=0;
        if (needle.empty()) score=1;
        else if (!query.modMode && (needle==id || needle=="pv"+id || needle=="pv"+std::string(id.size()<3?3-id.size():0,'0')+id || (needle.size()<=9 && std::all_of(needle.begin(),needle.end(),[](char c){ return c>='0'&&c<='9'; }) && std::stoi(needle)==s.id))) score=1000;
        else if (!query.modMode && (needle==Normalize(s.name) || needle==Normalize(s.english))) score=900;
        else if (key.find("\n"+needle+"\n")!=std::string::npos) score=850;
        else if (key.starts_with(needle)) score=700;
        else if (key.find(needle)!=std::string::npos) score=500;
        else if (!words.empty() && std::all_of(words.begin(),words.end(),[&](const std::string& t){ return key.find(t)!=std::string::npos; })) score=300;
        if (score) out.push_back({i,score});
    }
    std::stable_sort(out.begin(),out.end(),[&](const Result& a,const Result& b){
        const auto& x=catalog.songs[a.index]; const auto& y=catalog.songs[b.index];
        if (query.sort==0 && a.score!=b.score) return a.score>b.score;
        if (query.sort==2 && Normalize(x.name)!=Normalize(y.name)) return Normalize(x.name)<Normalize(y.name);
        if (query.sort==3) {
            int d=difficulty>=0?difficulty:3;
            auto lx=x.charts[style][d].level, ly=y.charts[style][d].level;
            if(lx!=ly) return lx>ly;
        }
        return x.id<y.id;
    }); return out;
}
// 读取收藏编号；参数 path 为收藏文件。
std::set<int> LoadFavorites(const fs::path& path) { std::ifstream in(path); std::set<int> out; int id; while(in>>id) if(id>0) out.insert(id); return out; }
// 使用临时文件替换保存收藏；参数 path/values 为文件与收藏集合。
bool SaveFavorites(const fs::path& path,const std::set<int>& values) {
    auto temp=path; temp += L".tmp";
    { std::ofstream out(temp,std::ios::trunc); for(int id:values) out<<id<<"\n"; if(!out.good()) return false; }
    return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
}
