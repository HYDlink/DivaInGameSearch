#include "ui.h"
#include <Windows.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
// 参数 condition/message 为断言与中文错误信息。
void Check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
// 参数 id/character/cos 为不同编号空间的测试实体，故意不令ModuleID等于COS。
diva::CustomizationItem Module(int id,diva::CustomizationCharacter character,const std::string& cos) {
    diva::CustomizationItem item;item.key.id=id;item.character=character;item.cos=cos;item.name="服装"+std::to_string(id);item.searchKey=diva::Normalize(item.name);return item;
}
// 参数 view 为真实视图，等待只用于隔离测试，生产帧不等待任务。
void Complete(diva::CustomizationView& view) {
    for(int i=0;i<2000;++i) {view.Poll();if(!view.SearchPending())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    throw std::runtime_error("推荐查询没有完成。");
}
}
// 验证共用多语检索、多角色和COS映射、歧义拒绝、旧快照失效与无导航查询。
int main() {
    SetConsoleOutputCP(CP_UTF8);
    try {
        using namespace diva;
        auto songs=std::make_shared<Catalog>();Song song;song.id=637;song.name="千本桜";song.english="Senbonzakura";song.aliases="千本樱";song.reading="せんぼんざくら";song.charts[0][2].available=true;PrepareSong(song);songs->songs.push_back(song);
        song.id=638;song.name="千本桜 別版";song.english="Senbonzakura edition";PrepareSong(song);songs->songs.push_back(song);
        auto modules=std::make_shared<CustomizationCatalog>();modules->items={Module(270,CustomizationCharacter::Miku,"COS_115"),Module(273,CustomizationCharacter::Rin,"COS_031"),Module(88,CustomizationCharacter::Kaito,"COS_024")};
        auto snapshot=std::make_shared<RecommendationSnapshot>();snapshot->ready=true;snapshot->generation=1;
        RecommendedPerformer miku;miku.type=0;miku.character=0;miku.costumes.fill(114);
        auto rin=miku;rin.character=1;rin.costumes.fill(30);auto kaito=miku;kaito.character=6;kaito.costumes.fill(23);
        snapshot->songs[637]={miku,rin,kaito,miku};snapshot->songs[638]={rin};
        auto mapped=ResolveRecommendedModules(*snapshot,637,*modules);
        Check(mapped.items==std::vector<size_t>({0,1,2}) && mapped.reasons.at(modules->items[0].key).size()==2,"多角色、去重或人物/COS映射错误");
        CustomizationQuery query;Query songQuery;std::set<int> favorites;
        for(const auto* text:{"千本桜","千本樱","Senbonzakura","せんぼんざくら","センボンザクラ","senbonzakura","637"}) {
            query.text=text;songQuery.text=text;const auto expected=Search(*songs,songQuery,favorites);
            const auto result=SearchModulesWithSongs(*modules,query,songs.get(),songQuery,favorites,snapshot.get());
            Check(!expected.empty() && result.matchedSongs==expected.size() && result.items.size()==3,"服装入口未共用歌曲多语/PVID检索或错误过滤其他人物");
        }
        auto duplicate=*modules;duplicate.items.push_back(Module(999,CustomizationCharacter::Rin,"COS_031"));
        mapped=ResolveRecommendedModules(*snapshot,637,duplicate);Check(mapped.items.size()==2 && !mapped.unresolved.empty(),"人物/COS歧义被猜测为任意ModuleID");
        auto unknown=*snapshot;unknown.songs[637][0].character=-1;unknown.songs[637][3].type=99;
        mapped=ResolveRecommendedModules(unknown,637,*modules);Check(mapped.items.size()==2 && mapped.unresolved.size()==2,"未知或伪人物被伪造为初音");
        auto pseudo=*snapshot;auto inherited=miku;inherited.type=2;inherited.character=-1;inherited.costumes.fill(114);inherited.pseudoSameId=0;pseudo.songs[268]={miku,inherited};
        mapped=ResolveRecommendedModules(pseudo,268,*modules);Check(mapped.items.size()==1 && mapped.reasons.at(modules->items[0].key).size()==2,"明确伪人物引用未关联到被引用人物");
        pseudo.songs[268][1].costumes.fill(30);mapped=ResolveRecommendedModules(pseudo,268,*modules);
        Check(mapped.items.size()==1 && mapped.reasons.at(modules->items[0].key).size()==1 && !mapped.unresolved.empty(),"伪人物错误复制前一项COS或把其COS映射成铃");
        pseudo.songs[268][1].pseudoSameId=1;mapped=ResolveRecommendedModules(pseudo,268,*modules);
        Check(!mapped.unresolved.empty(),"伪人物自身或向前无效引用被猜测为初音");
        auto variants=*snapshot;variants.songs[637][0].costumes[4]=30;
        mapped=ResolveRecommendedModules(variants,637,*modules);Check(!mapped.unresolved.empty(),"原生ENCORE被错误当成EX EXT并丢弃未映射分支");
        CustomizationView view;view.catalog=modules;view.songCatalog=songs;view.generation=1;view.recommendationProvider=[snapshot] {return snapshot;};
        int actions=0;view.submitModuleLocate=[&actions](const CustomizationLocateRequest&) {++actions;};
        strcpy_s(view.State().input,"637");view.QueueSearch(false);Complete(view);
        Check(view.State().results.size()==3 && actions==0 && view.State().recommendationReasons.size()==3,"真实视图查询未显示推荐或自动导航");
        strcpy_s(view.State().input,"不存在的歌曲");view.QueueSearch(false);Complete(view);Check(view.State().results.empty(),"旧查询推荐覆盖新输入");
        strcpy_s(view.State().input,"637");view.QueueSearch(false);Complete(view);
        auto notReady=std::make_shared<RecommendationSnapshot>();notReady->generation=2;notReady->reason="游戏数据库重建中";
        view.recommendationProvider=[notReady] {return notReady;};view.Poll();Complete(view);
        Check(view.State().results.empty() && !view.State().recommendationWarnings.empty() && actions==0,"推荐快照失效后仍显示旧目标");
        View parent;parent.catalog=songs;parent.query.modMode=true;parent.query.pack="不存在的曲包";parent.query.favoritesOnly=true;parent.query.style=0;parent.Poll();
        Check(parent.customization.songCatalog==songs && !parent.customization.songCriteria.modMode && parent.customization.songCriteria.pack.empty() && !parent.customization.songCriteria.favoritesOnly,"歌曲页隐藏筛选导致服装页按歌曲名称无法检索");
        std::cout<<"歌曲推荐服装回归通过：共用多语检索、多角色、唯一COS关联、歧义与过期保护，查询没有导航副作用。\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"歌曲推荐服装验收失败："<<error.what()<<"\n";return 1;}
}
