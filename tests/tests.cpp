#include "core.h"
#include "selector.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

static int checks=0;
// 检查条件，失败抛出中文说明；参数 value/text 为断言与原因。
static void Check(bool value,const char* text) { ++checks; if(!value) throw std::runtime_error(text); }
// 在测试隔离目录写入夹具；参数 path/text 为目标与 UTF-8 内容。
static void Write(const diva::fs::path& path,const std::string& text) { diva::fs::create_directories(path.parent_path()); std::ofstream out(path,std::ios::binary); out<<text; }
// 提供可控内存协议，记录阶段调用。
struct FakeMemory:diva::Memory {
    diva::Snapshot state{true,5,6,1,728,3,0,48,3};
    int transitions=0,selections=0,failSelections=0; bool fail=false,directEnabled=false;
    // 返回本帧状态副本。
    diva::Snapshot Read() override { return state; }
    // 记录目标菜单；参数 next 为菜单编号。
    bool Transition(int next) override { ++transitions;state.current=next;state.next=next+1;state.changing=1;return !fail; }
    // 模拟请求字段写入；参数 request 为定位动作。
    bool Select(const diva::Request& request) override { ++selections;if(failSelections>0) { --failSelections;return false; }state.pvid=request.pvid;state.difficulty=request.difficulty;state.style=request.style;return !fail; }
    // 按测试条件启用原界面路径。
    bool SupportsDirect() const override { return directEnabled; }
    // 只模拟目标请求缓存更新，实际高亮仍必须独立读回；参数 request 为目标。
    bool DirectSelect(const diva::Request& request) override { return Select(request); }
};
// 验证 Unicode、读音、全局过滤和排序。
static void TestSearch() {
    using namespace diva;
    Check(Normalize("ＡＢＣ　１２３")=="abc123","全半角规范化失败");
    Check(Normalize("メルト")==Normalize("めると"),"平片假名统一失败");
    Check(Romanize("せんぼんざくら")=="senbonzakura","假名转写失败");
    Check(Romanize("きゃっと")=="kyatto","拗音促音失败");
    Check(Romanize("シェイク")=="sheiku" && Romanize("ファインダー")=="faindaa","外来语假名转写失败");
    Check(Normalize("ÉTOILE")==Normalize("étoile"),"Unicode 大小写统一失败");
    Catalog c;
    for(int id:{637,2,1}) {
        Song s; s.id=id; s.name=id==637?"千本桜":"恋は戦争"; s.english=id==637?"Senbonzakura":"Love is War";
        s.reading=id==637?"センボンザクラ":"こいはせんそう"; s.aliases=id==637?"千本樱":"恋战"; s.sources={id==637?"曲包甲":"曲包乙"};
        s.charts[0][3]={float(id%10),"rom/script/x.dsc",s.sources.front(),true}; PrepareSong(s); c.songs.push_back(s);
    }
    Query q; q.text="千本樱"; Check(Search(c,q,{}).size()==1,"中文别名失败");
    q.text="ＳＥＮＢＯＮＺＡＫＵＲＡ"; Check(Search(c,q,{}).size()==1,"英文兼容字符失败");
    q.text="せんぼんざくら"; Check(Search(c,q,{}).size()==1,"假名检索失败");
    q.text="koi ha senso"; Check(Search(c,q,{}).size()==2,"长音与多词检索失败");
    q.text="000637"; Check(Search(c,q,{}).size()==1,"前导零编号失败");
    q.text="PV_637"; Check(Search(c,q,{}).size()==1,"带前缀编号失败");
    q.text="love war"; Check(Search(c,q,{}).size()==2,"多词匹配失败");
    q.text="曲包甲"; q.modMode=true; Check(Search(c,q,{}).size()==1,"Mod 名搜索失败");
    q={};q.favoritesOnly=true;q.difficulty=3; Check(Search(c,q,{637}).size()==1,"组合过滤失败");
    q.difficulty=4; Check(Search(c,q,{637}).empty(),"缺失难度过滤失败");
    q={}; q.style=1; Check(Search(c,q,{}).empty(),"未生效风格过滤失败");
    q={}; q.sort=1; auto results=Search(c,q,{}); Check(c.songs[results.front().index].id==1,"全局编号排序失败");
    q.sort=3; results=Search(c,q,{}); Check(c.songs[results.front().index].id==637,"等级全局排序失败");
    q.text="不存在的歌曲"; Check(Search(c,q,{}).empty(),"无结果处理失败");
    Catalog names;
    for(int id:{2,3,1}) {
        Song song; song.id=id; song.name=id==2?"Ｂ":id==3?"a":"Ａ";
        song.charts[0][3].available=true; PrepareSong(song); names.songs.push_back(std::move(song));
    }
    q={}; q.sort=2; results=Search(names,q,{});
    Check(names.songs[results[0].index].id==1 && names.songs[results[1].index].id==3 && names.songs[results[2].index].id==2,"名称排序未统一全角与大小写或稳定编号");
    q.text="a"; results=Search(names,q,{});
    Check(results.size()==2 && results[0].score==900 && results[1].score==900,"缓存名称精确匹配改变相关度");
}
// 验证真实格式 priority、enabled、include、字段覆盖与 NC 继承；参数 root 为本项目根目录。
static void TestCatalog(const diva::fs::path& root) {
    using namespace diva; auto test=root/L"tests/runtime";
    fs::create_directories(test); auto h=test/L"home",g=test/L"game";
    Write(h/L"data/base_pv_db.txt","pv_001.song_name=原名\npv_001.song_name_reading=あいう\npv_001.difficulty.extreme.length=1\npv_001.difficulty.extreme.0.script_file_name=rom/script/base.dsc\npv_001.difficulty.extreme.0.level=PV_LV_08_5\n");
    Write(g/L"config.toml","enabled=true\nmods='mods'\npriority=['高','禁用','低','NC']\n");
    Write(g/L"mods/高/config.toml","enabled=true\nname='高优先级'\ninclude=['子目录']\n");
    Write(g/L"mods/高/子目录/rom/mod_pv_db.txt","pv_001.song_name=实际名称\npv_001.difficulty.hard.length=1\npv_001.difficulty.hard.0.script_file_name=rom/script/mod.dsc\npv_001.difficulty.hard.0.level=PV_LV_07_0\n");
    Write(g/L"mods/高/子目录/rom/script/mod.dsc","测试脚本占位，不执行");
    Write(g/L"mods/低/config.toml","include=['.']\nname='低优先级'\n");
    Write(g/L"mods/低/rom/mod_pv_db.txt","pv_001.song_name=不应覆盖\npv_001.song_name_en=English Alias\n");
    Write(g/L"mods/禁用/config.toml","enabled=false\ninclude=['.']\n");
    Write(g/L"mods/禁用/rom/mod_pv_db.txt","pv_001.song_name=禁用错误\npv_900.song_name=不应加载\n");
    Write(g/L"mods/未列出/config.toml","include=['.']\n");
    Write(g/L"mods/未列出/rom/mod_pv_db.txt","pv_901.song_name=不在优先级中\n");
    Write(g/L"mods/NC/config.toml","include=['.']\ndll=['NewClassics.dll']\n");
    Write(g/L"mods/NC/rom/nc_db.toml","[[songs]]\nid=1\n[[songs.extreme]]\nstyle='ARCADE'\n[[songs.extreme]]\nstyle='CONSOLE'\nscript_file_name='rom/script_nc/nc.dsc'\nlevel='PV_LV_09_5'\n");
    Write(g/L"mods/NC/rom/script_nc/nc.dsc","测试脚本占位，不执行");
    Write(h/L"aliases.toml","[aliases]\n1='中文别名'\n");
    auto c=LoadCatalog(h,g);
    Check(c.songs.size()==1,"禁用或非 priority 来源被加载");
    const auto& song=c.songs[0];
    Check(song.name=="实际名称","DML 首项优先或 include 解析失败");
    Check(song.english=="English Alias","逐字段合并丢失名称");
    Check(song.charts[0][2].source=="高优先级" && song.charts[0][2].available,"脚本实际来源解析失败");
    Check(song.charts[0][3].available && song.charts[0][3].level==8.5f,"NC 无脚本继承失败");
    Check(song.charts[1][3].available && song.charts[1][3].level==9.5f,"NC 风格分离失败");
    Check(!song.charts[2][3].available,"未定义 NC 风格误生效");
    Check(song.aliases=="中文别名","用户别名加载失败");
    Check(SaveFavorites(h/L"favorites.txt",{1,637}) && LoadFavorites(h/L"favorites.txt").size()==2,"收藏持久化失败");
    Write(g/L"mods/高/子目录/rom/mod_pv_db.txt","pv_001.song_name=重建名称\npv_001.difficulty.extreme.length=0\n");
    auto changed=LoadCatalog(h,g);
    Check(changed.songs[0].name=="重建名称" && !changed.songs[0].charts[0][3].available,"索引刷新未丢弃过期字段");
    Check(changed.configurationKey!=c.configurationKey,"数据库变化未改变启动指纹");
}
// 验证跨帧等待、拒绝、超时和独立读回。
static void TestSelector() {
    using namespace diva; FakeMemory m; Selector s; Request r{637,4,1,1};
    auto bad=m.state; bad.current=7; Check(!s.Submit(r,true,bad,0),"演奏状态未拒绝");
    bad=m.state; bad.supported=false; Check(!s.Submit(r,true,bad,0),"未知版本未拒绝");
    Check(!s.Submit(r,false,m.state,0),"缺失难度未拒绝");
    Check(s.Submit(r,true,m.state,0),"有效请求未接受");
    Check(!s.Submit(r,true,m.state,1),"重复请求未拒绝");
    s.Tick(m,10); Check(m.transitions==1 && m.selections==0,"准备菜单未先执行");
    s.Tick(m,109); Check(m.selections==0,"跨帧等待不足 100 毫秒");
    s.Tick(m,110); Check(m.transitions==2 && m.selections==1 && s.Busy(),"选曲提交顺序失败");
    m.state.current=5; m.state.next=6; m.state.visiblePvid=48;
    s.Tick(m,300); s.Tick(m,310); s.Tick(m,320); Check(s.Busy(),"仅请求字段一致时误报成功");
    m.state.visiblePvid=637; m.state.visibleDifficulty=4; s.Tick(m,330); s.Tick(m,340); Check(s.Busy(),"缺少连续读回确认"); s.Tick(m,350);
    Check(!s.Busy() && s.succeeded,"真实字段一致后未确认成功");
    m.state={true,5,6,1,728,3,0,48,3}; Check(s.Submit(r,true,m.state,1000),"后续请求未接受"); s.Tick(m,1001);s.Tick(m,1101);s.Tick(m,7001);
    Check(!s.Busy() && !s.succeeded,"超时没有停止或残留成功");
    m.state={true,5,6,1,728,3,0,48,3}; s.Submit(r,true,m.state,8000); s.Tick(m,8001);m.state.current=7;s.Tick(m,8200); Check(!s.Busy(),"离开允许菜单未取消");
    m.state={true,5,6,1,728,3,0,48,3};m.fail=true;s.Submit(r,true,m.state,9000);s.Tick(m,9001);Check(!s.Busy() && !s.succeeded,"内存写入失败未停止");m.fail=false;
    m.state={true,5,6,1,728,3,0,48,3};s.Submit(r,true,m.state,10000);m.state.supported=false;s.Tick(m,10001);Check(!s.Busy(),"执行中版本失效未停止");
    // 原样使用 01:26:58 与 01:29:24 实机采样：5/6、控制 1、实际 48，旧请求 728/3。
    FakeMemory normal;Selector ordinary;normal.state={true,5,6,1,728,3,0,48,2};Request hard{438,2,0,2};
    Check(ordinary.Submit(hard,true,normal.state,11000),"真实普通选曲采样被误拒绝");
    ordinary.Tick(normal,11001);normal.state.changing=2;ordinary.Tick(normal,11200);Check(normal.selections==0,"菜单仍在处理请求时写入了歌曲");
    normal.state.changing=1;normal.state.stable=false;ordinary.Tick(normal,11210);Check(normal.selections==0,"菜单尚未稳定时写入了歌曲");
    normal.state.stable=true;ordinary.Tick(normal,11400);Check(normal.selections==1 && normal.transitions==2,"普通选曲准备后未提交");
    normal.state.current=5;normal.state.next=6;normal.state.visiblePvid=438;normal.state.visibleDifficulty=3;
    ordinary.Tick(normal,11600);ordinary.Tick(normal,11610);ordinary.Tick(normal,11620);Check(ordinary.Busy(),"实际难度不一致时误报成功");
    normal.state.visibleDifficulty=2;ordinary.Tick(normal,11630);ordinary.Tick(normal,11640);ordinary.Tick(normal,11650);Check(ordinary.succeeded,"普通选曲独立读回一致后未成功");
    normal.state={true,5,6,1,438,2,0,438,2};normal.state.mode=0x101;Check(!ordinary.Submit(hard,true,normal.state,12000),"PV 观看模式未被拒绝");
    normal.state.mode=0;normal.state.changing=2;Check(!ordinary.Submit(hard,true,normal.state,12001),"过渡菜单未被拒绝");
    normal.state.changing=1;normal.state.visiblePvid=-1;Check(!ordinary.Submit(hard,true,normal.state,12002),"误用演奏对象的负 PVID 未拒绝");
    FakeMemory recover;Selector retry;recover.state={true,5,6,1,728,3,0,48,2};retry.Submit(hard,true,recover.state,13000);retry.Tick(recover,13001);
    recover.failSelections=1;retry.Tick(recover,13200);Check(retry.Busy() && recover.state.current==5 && recover.state.pvid==48 && recover.state.difficulty==2,"写入失败后未请求恢复原高亮");
    retry.Tick(recover,13210);Check(!retry.Busy() && !retry.succeeded && retry.message.find("已返回原选曲")!=std::string::npos,"恢复完成被误报为目标定位成功");
    FakeMemory inPlace;Selector native;inPlace.directEnabled=true;inPlace.state={true,5,6,1,728,3,0,48,2};native.Submit(hard,true,inPlace.state,14000);native.Tick(inPlace,14001);
    Check(inPlace.transitions==0 && inPlace.selections==1,"原界面定位不应发出顶层菜单切换");
    native.Tick(inPlace,14200);Check(native.Busy(),"原界面定位仅缓存更新时误报成功");
    inPlace.state.visiblePvid=438;inPlace.state.visibleDifficulty=2;native.Tick(inPlace,14210);native.Tick(inPlace,14220);native.Tick(inPlace,14230);Check(native.succeeded && inPlace.transitions==0,"原界面高亮确认失败");
}
// 运行全部原生测试；参数 argc/argv 提供本项目根目录以约束夹具输出。
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    try { if(argc!=2) throw std::runtime_error("缺少项目目录参数"); TestSearch(); TestCatalog(argv[1]); TestSelector(); std::cout<<"全部通过："<<checks<<" 项原生核心断言。\n"; return 0; }
    catch(const std::exception& error) { std::cout<<"验证失败："<<error.what()<<"\n"; return 1; }
}
