// 精灵解析链隔离回归：用替身数据库、归档与解码器验证 DML 优先级、普通库替换、追加库、FT 通用图回退与透明边界。
// 不读取游戏 CPK，也不调用随包桥接；替身格式只用于本测试，不代表真实 SpriteDatabase 二进制格式。
#include "sprite_resolver.h"
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
int checks=0;
// 参数 condition/message 为断言与中文失败原因。
void Check(bool condition,const char* message) {++checks;if(!condition) throw std::runtime_error(message);}
using namespace diva;
// 替身数据库：每行 "名称=集合文件:索引"；同名出现两次表示重复。
SpriteLookup FakeFind(const std::string& database,const std::string& name) {
    SpriteLookup lookup;lookup.status=SpriteLookupStatus::Missing;int found=0;size_t at=0;
    if(database=="broken") {lookup.status=SpriteLookupStatus::Failed;return lookup;}
    while(at<database.size()) {
        auto end=database.find('\n',at);if(end==std::string::npos) end=database.size();
        const auto line=database.substr(at,end-at);at=end+1;
        const auto equals=line.find('='),colon=line.rfind(':');
        if(equals==std::string::npos || colon==std::string::npos || line.substr(0,equals)!=name) continue;
        ++found;lookup.file=line.substr(equals+1,colon-equals-1);lookup.index=std::stoi(line.substr(colon+1));
    }
    if(found==1) lookup.status=SpriteLookupStatus::Found;
    else if(found>1) lookup.status=SpriteLookupStatus::Duplicate;
    return lookup;
}
int decodeCalls=0;
// 替身解码：集合内容 "bad" 表示解码失败；否则生成 8x4 画布，在 (2,1)-(5,2) 放不透明像素。
std::optional<SpritePixels> FakeDecode(const std::string& binary,int index,std::string& reason) {
    ++decodeCalls;
    if(binary.find("bad")!=std::string::npos) {reason="替身解码失败";return std::nullopt;}
    auto pixels=std::make_shared<std::vector<unsigned char>>(8*4*4,0);
    for(int y=1;y<=2;++y) for(int x=2;x<=5;++x) (*pixels)[static_cast<size_t>((y*8+x)*4+3)]=255;
    (*pixels)[0]=static_cast<unsigned char>(index);
    return SpritePixels{pixels,8,4};
}
// 替身归档：归档内容就是集合 BIN 内容，条目名只需匹配集合名。
std::optional<std::string> FakeFarc(const std::string& farc,const std::string& entry,std::string& reason) {
    if(farc.find("corrupt")!=std::string::npos) {reason="替身归档损坏";return std::nullopt;}
    return farc+"|"+entry;
}
// 参数 files 为逻辑路径到内容的映射；构造一个 Mod 资源根替身。
SpriteModRoot Root(const std::string& label,std::map<std::string,std::string> files) {
    return {label,[files](const std::string& logical,bool& exists) -> std::optional<std::string> {
        auto found=files.find(logical);exists=found!=files.end();if(!exists) return std::nullopt;
        if(found->second=="<unreadable>") return std::nullopt;
        return found->second;
    }};
}
SpriteBackend Backend() {return {FakeFind,FakeFarc,FakeDecode};}
// 参数 db/archives 为官方数据库内容与官方归档映射；构造仅含官方资源的环境。
SpriteEnvironment Official(std::string db,std::map<std::string,std::string> archives) {
    SpriteEnvironment environment;environment.officialDatabaseSuffixes={"rom_steam_cn/rom/2d/spr_db.bin","rom_steam/rom/2d/spr_db.bin"};
    environment.officialDatabase=[db](const std::string& suffix,std::string& label) -> std::optional<std::string> {
        if(suffix!="rom_steam/rom/2d/spr_db.bin") return std::nullopt;label="官方库";return db;
    };
    environment.officialArchive=[archives](const std::string& farc,int style,std::string& label,std::string& reason) -> std::optional<std::string> {
        (void)style;auto found=archives.find(farc);if(found==archives.end()) {reason="官方归档缺失";return std::nullopt;}
        label="官方归档 "+farc;return found->second;
    };
    return environment;
}
// 参数 kind/id/part 为实体；返回仅用于命名的实体。
CustomizationItem Item(CustomizationKind kind,int id,CustomizationPart part) {CustomizationItem item;item.key={kind,id};item.part=part;return item;}
}

// 验证命名规则、解析优先级、回退语义与透明边界。
int main() {
    try {
        // 命名：模型 MD_IMG，发型与四部位道具 CMNITM_THMB ITM_IMG；FT 之后附同 ID 通用图；至少三位不截断。
        Check(CustomizationSpriteNames(Item(CustomizationKind::Module,5,CustomizationPart::Unknown),1)==std::vector<std::string>{"SPR_SEL_MD005CMN_MD_IMG"},"模型 MEGA39 命名错误");
        Check(CustomizationSpriteNames(Item(CustomizationKind::Accessory,500,CustomizationPart::Hair),2)==std::vector<std::string>{"SPR_CMNITM_THMB500_ITM_IMG_FT","SPR_CMNITM_THMB500_ITM_IMG"},"发型 FT 命名或通用回退错误");
        Check(CustomizationSpriteNames(Item(CustomizationKind::Accessory,7,CustomizationPart::Neck),1)==std::vector<std::string>{"SPR_CMNITM_THMB007_ITM_IMG"},"道具三位补零错误");
        Check(CustomizationSpriteNames(Item(CustomizationKind::Accessory,1234,CustomizationPart::Back),1).front()=="SPR_CMNITM_THMB1234_ITM_IMG","四位ID被截断");
        Check(CustomizationSpriteNames(Item(CustomizationKind::Accessory,9,CustomizationPart::Unknown),1).empty() && CustomizationSpriteNames(Item(CustomizationKind::Module,1,CustomizationPart::Unknown),0).empty(),"未知部位或未确认风格仍生成图片名");
        Check(SpriteArchiveNames("spr_cmnitm_thmb500").first=="rom/2d/spr_cmnitm_thmb500.farc" && SpriteArchiveNames("SPR_CMNITM_THMB500.bin").second=="spr_cmnitm_thmb500.bin","集合文件到归档路径映射错误");

        const auto hair=CustomizationSpriteNames(Item(CustomizationKind::Accessory,500,CustomizationPart::Hair),2);
        // 官方：FT 专用图存在时直接使用，不回退。
        auto result=ResolveSprite(Backend(),Official("SPR_CMNITM_THMB500_ITM_IMG_FT=spr_cmnitm_thmb500:1\nSPR_CMNITM_THMB500_ITM_IMG=spr_cmnitm_thmb500:0",
            {{"rom/2d/spr_cmnitm_thmb500.farc","set500"}}),hair,2);
        Check(result.ok() && !result.fallback && result.spriteName=="SPR_CMNITM_THMB500_ITM_IMG_FT" && (*result.pixels.rgba)[0]==1,"FT专用图未使用真实索引");
        Check(result.bounds.x==2 && result.bounds.y==1 && result.bounds.width==4 && result.bounds.height==2,"透明边界计算错误");
        // 专用图缺失：回退通用图并保留原因。
        result=ResolveSprite(Backend(),Official("SPR_CMNITM_THMB500_ITM_IMG=spr_cmnitm_thmb500:0",{{"rom/2d/spr_cmnitm_thmb500.farc","set500"}}),hair,2);
        Check(result.ok() && result.fallback && result.spriteName=="SPR_CMNITM_THMB500_ITM_IMG" && result.fallbackReason.find("不存在")!=std::string::npos,"专用图缺失时未回退或丢失诊断");
        // 专用图明确解码失败：回退通用图，不冒充专用图成功。
        result=ResolveSprite(Backend(),Official("SPR_CMNITM_THMB500_ITM_IMG_FT=spr_bad_ft:0\nSPR_CMNITM_THMB500_ITM_IMG=spr_cmnitm_thmb500:0",
            {{"rom/2d/spr_bad_ft.farc","bad"},{"rom/2d/spr_cmnitm_thmb500.farc","set500"}}),hair,2);
        Check(result.ok() && result.fallback && result.fallbackReason.find("解码失败")!=std::string::npos,"专用图解码失败未回退或未说明");
        // 两者都失败：报告专用图原因，不返回图片。
        result=ResolveSprite(Backend(),Official("SPR_CMNITM_THMB500_ITM_IMG_FT=spr_bad_ft:0",{{"rom/2d/spr_bad_ft.farc","bad"}}),hair,2);
        Check(!result.ok() && result.failure.find("解码失败")!=std::string::npos,"全部失败时没有保留专用图原因");
        // 重复名称拒绝猜测。
        result=ResolveSprite(Backend(),Official("SPR_CMNITM_THMB500_ITM_IMG=a:0\nSPR_CMNITM_THMB500_ITM_IMG=b:1",{{"rom/2d/a.farc","x"},{"rom/2d/b.farc","y"}}),{"SPR_CMNITM_THMB500_ITM_IMG"},1);
        Check(!result.ok() && result.failure.find("重复")!=std::string::npos,"重复精灵名被任意选择");

        // DML：追加库按优先级；高优先 Mod 归档覆盖官方同逻辑归档。
        auto environment=Official("SPR_CMNITM_THMB600_ITM_IMG=spr_cmnitm_thmb600:0",{{"rom/2d/spr_cmnitm_thmb600.farc","official600"}});
        environment.mods={Root("高优先",{{"rom/2d/spr_cmnitm_thmb600.farc","mod600"}}),Root("低优先",{{"rom/2d/mod_spr_db.bin","SPR_CMNITM_THMB600_ITM_IMG=spr_low600:3"},{"rom/2d/spr_low600.farc","low600"}})};
        result=ResolveSprite(Backend(),environment,{"SPR_CMNITM_THMB600_ITM_IMG"},1);
        Check(result.ok() && (*result.pixels.rgba)[0]==3 && result.source.find("低优先")!=std::string::npos,"追加库映射未优先于官方普通库");
        environment.mods={Root("高优先",{{"rom/2d/mod_spr_db.bin","SPR_CMNITM_THMB600_ITM_IMG=spr_cmnitm_thmb600:2"},{"rom/2d/spr_cmnitm_thmb600.farc","mod600"}}),
            Root("低优先",{{"rom/2d/mod_spr_db.bin","SPR_CMNITM_THMB600_ITM_IMG=spr_low600:3"},{"rom/2d/spr_low600.farc","low600"}})};
        result=ResolveSprite(Backend(),environment,{"SPR_CMNITM_THMB600_ITM_IMG"},1);
        Check(result.ok() && (*result.pixels.rgba)[0]==2 && result.source.find("高优先")!=std::string::npos,"追加库未按DML优先级取高优先映射");
        // Mod 普通 spr_db 整库替换：不含目标时官方库退出，不拼接。
        environment.mods={Root("替换库",{{"rom/2d/spr_db.bin","OTHER=other:0"}})};
        result=ResolveSprite(Backend(),environment,{"SPR_CMNITM_THMB600_ITM_IMG"},1);
        Check(!result.ok(),"普通库替换后仍回落官方库");
        environment.mods={Root("替换库",{{"rom/2d/spr_db.bin","SPR_CMNITM_THMB600_ITM_IMG=spr_cmnitm_thmb600:4"}})};
        result=ResolveSprite(Backend(),environment,{"SPR_CMNITM_THMB600_ITM_IMG"},1);
        Check(result.ok() && (*result.pixels.rgba)[0]==4 && result.source.find("官方归档")!=std::string::npos,"替换库映射未使用或归档未回落官方CPK");
        // 存在但不可读的普通库阻止低优先普通库与官方库。
        environment.mods={Root("坏库",{{"rom/2d/spr_db.bin","<unreadable>"}}),Root("低优先",{{"rom/2d/spr_db.bin","SPR_CMNITM_THMB600_ITM_IMG=x:0"}})};
        result=ResolveSprite(Backend(),environment,{"SPR_CMNITM_THMB600_ITM_IMG"},1);
        Check(!result.ok() && result.failure.find("读取失败")!=std::string::npos,"不可读的高优先普通库被跳过");
        // 归档损坏不返回图片。
        environment.mods={Root("坏归档",{{"rom/2d/spr_cmnitm_thmb600.farc","corrupt"}})};
        result=ResolveSprite(Backend(),environment,{"SPR_CMNITM_THMB600_ITM_IMG"},1);
        Check(!result.ok() && result.failure.find("归档读取失败")!=std::string::npos,"损坏归档仍报告成功");
        // 全透明画布按整张显示。
        std::vector<unsigned char> empty(4*4*4,0);const auto bounds=ComputeSpriteBounds(empty,4,4);
        Check(bounds.x==0 && bounds.width==4 && bounds.height==4,"全透明图边界错误");
        std::cout<<"精灵解析链隔离回归通过："<<checks<<" 项；只使用替身数据库与解码器，未读取游戏资源。\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"精灵解析验证失败："<<error.what()<<"\n";return 1;}
}
