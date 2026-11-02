#include "sprite_resolver.h"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace diva {
namespace {
// 参数 text 为路径或文件名；统一斜线与 ASCII 大小写以匹配游戏逻辑路径。
std::string Fold(std::string text) {
    std::replace(text.begin(),text.end(),'\\','/');
    for(char& ch:text) if(static_cast<unsigned char>(ch)<0x80) ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}
// 参数 lookup 为数据库查找结果，返回给诊断使用的中文原因。
std::string LookupReason(const SpriteLookup& lookup,const std::string& name,const std::string& label) {
    if(!lookup.reason.empty()) return lookup.reason+" / "+label;
    switch(lookup.status) {
    case SpriteLookupStatus::Missing: return "精灵名不存在："+name+" / "+label;
    case SpriteLookupStatus::Duplicate: return "精灵名重复，拒绝猜测索引："+name+" / "+label;
    default: return "精灵数据库读取或解析失败："+label;
    }
}
// 参数 backend/environment/name/style 为后端、资源环境、单个精确名与风格；只尝试这一个名称。
SpriteResult ResolveOne(const SpriteBackend& backend,const SpriteEnvironment& environment,const std::string& name,int style) {
    SpriteResult result;result.spriteName=name;result.failure="精灵数据库没有可用映射："+name;
    std::optional<SpriteLookup> resolved;std::string databaseLabel;
    bool ordinaryReplaced=false;
    // DML 追加库 mod_spr_db 先按优先级合并；普通 spr_db 只接受最高优先来源，并使官方普通库整库退出。
    for(const char* database:{"rom/2d/mod_spr_db.bin","rom/2d/spr_db.bin"}) {
        const bool ordinary=std::string(database)=="rom/2d/spr_db.bin";
        for(const auto& root:environment.mods) {
            bool exists=false;auto bytes=root.read?root.read(database,exists):std::nullopt;
            if(!exists) continue;
            if(ordinary) ordinaryReplaced=true;
            if(!bytes) {result.failure="Mod 精灵数据库读取失败："+root.label+" / "+database;if(ordinary) break;continue;}
            const auto lookup=backend.find(*bytes,name);
            if(lookup.status!=SpriteLookupStatus::Found) {result.failure=LookupReason(lookup,name,root.label+" / "+database);if(ordinary) break;continue;}
            resolved=lookup;databaseLabel="Mod / "+root.label+" / "+database;break;
        }
        if(resolved) break;
    }
    if(!resolved && !ordinaryReplaced && environment.officialDatabase) for(const auto& suffix:environment.officialDatabaseSuffixes) {
        std::string label;auto bytes=environment.officialDatabase(suffix,label);
        if(!bytes) continue;
        const auto lookup=backend.find(*bytes,name);
        if(lookup.status!=SpriteLookupStatus::Found) {result.failure=LookupReason(lookup,name,label);continue;}
        resolved=lookup;databaseLabel=label;break;
    }
    if(!resolved) return result;
    const auto [farc,entry]=SpriteArchiveNames(resolved->file);
    result.failure="精灵已映射，但资源归档未匹配："+resolved->file;
    std::optional<std::string> archive;std::string archiveLabel;
    // 归档同样按 DML 优先级取最高的同逻辑路径文件；没有 Mod 文件时才查官方 CPK。
    for(const auto& root:environment.mods) {
        bool exists=false;auto bytes=root.read?root.read(farc,exists):std::nullopt;
        if(!exists || !bytes) continue;
        archive=std::move(bytes);archiveLabel="Mod / "+root.label+" / "+farc;break;
    }
    if(!archive && environment.officialArchive) {
        std::string reason;archive=environment.officialArchive(farc,style,archiveLabel,reason);
        if(!archive) {if(!reason.empty()) result.failure=reason;return result;}
    }
    if(!archive) return result;
    std::string reason;
    auto binary=backend.farcEntry?backend.farcEntry(*archive,entry,reason):std::nullopt;
    if(!binary) {result.failure="图片归档读取失败："+reason+" / "+archiveLabel;return result;}
    auto pixels=backend.decode?backend.decode(*binary,resolved->index,reason):std::nullopt;
    if(!pixels || !pixels->rgba || pixels->width<=0 || pixels->height<=0
        || pixels->rgba->size()!=static_cast<size_t>(pixels->width)*static_cast<size_t>(pixels->height)*4) {
        result.failure="图片像素解码失败："+reason+" / "+archiveLabel+" / "+name;return result;
    }
    result.pixels=std::move(*pixels);
    result.bounds=ComputeSpriteBounds(*result.pixels.rgba,result.pixels.width,result.pixels.height);
    result.source=archiveLabel+" / sprite "+name+" / "+std::to_string(resolved->index);
    result.failure.clear();
    (void)databaseLabel;
    return result;
}
}

std::vector<std::string> CustomizationSpriteNames(const CustomizationItem& item,int style) {
    if(style!=1 && style!=2) return {};
    const auto domain=CustomizationItemDomain(item);if(!domain) return {};
    std::ostringstream base;
    if(*domain==CustomizationDomain::Models) base<<"SPR_SEL_MD"<<std::setw(3)<<std::setfill('0')<<item.key.id<<"CMN_MD_IMG";
    else base<<"SPR_CMNITM_THMB"<<std::setw(3)<<std::setfill('0')<<item.key.id<<"_ITM_IMG";
    const auto common=base.str();
    if(style==1) return {common};
    return {common+"_FT",common};
}

std::pair<std::string,std::string> SpriteArchiveNames(const std::string& fileName) {
    std::string value=Fold(fileName);
    const auto slash=value.find_last_of('/');
    std::string base=slash==std::string::npos?value:value.substr(slash+1);
    if(base.ends_with(".farc")) base.resize(base.size()-5);
    else if(base.ends_with(".bin")) base.resize(base.size()-4);
    std::string relative=value;
    if(relative.ends_with(".farc")) relative.resize(relative.size()-5);
    else if(relative.ends_with(".bin")) relative.resize(relative.size()-4);
    if(relative.find('/')==std::string::npos) relative="rom/2d/"+relative;
    return {relative+".farc",base+".bin"};
}

SpriteBounds ComputeSpriteBounds(const std::vector<unsigned char>& rgba,int width,int height,int threshold) {
    SpriteBounds bounds{0,0,width,height};
    if(width<=0 || height<=0 || rgba.size()<static_cast<size_t>(width)*static_cast<size_t>(height)*4) return bounds;
    int left=width,top=height,right=-1,bottom=-1;
    for(int y=0;y<height;++y) {
        const auto* row=rgba.data()+static_cast<size_t>(y)*static_cast<size_t>(width)*4;
        for(int x=0;x<width;++x) if(row[static_cast<size_t>(x)*4+3]>threshold) {
            left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
        }
    }
    if(right<0) return bounds;
    return {left,top,right-left+1,bottom-top+1};
}

SpriteResult ResolveSprite(const SpriteBackend& backend,const SpriteEnvironment& environment,const std::vector<std::string>& names,int style) {
    SpriteResult first;
    if(names.empty() || !backend.find) {first.failure="图片样式或实体领域没有已知精灵命名。";return first;}
    for(size_t index=0;index<names.size();++index) {
        auto result=ResolveOne(backend,environment,names[index],style);
        if(result.ok()) {
            // 专用图缺失或明确解码失败才回退同 ID 通用图；保留专用图原因，不冒充专用图成功。
            if(index>0) {result.fallback=true;result.fallbackReason=first.failure;}
            return result;
        }
        if(index==0) first=std::move(result);
    }
    return first;
}
}
