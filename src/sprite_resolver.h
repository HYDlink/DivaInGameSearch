#pragma once
#include "customization.h"
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace diva {
// 精灵数据库按精确名称查找的结果：Duplicate 表示同名多条，拒绝猜测索引。
enum class SpriteLookupStatus { Found, Missing, Duplicate, Failed };
struct SpriteLookup {
    SpriteLookupStatus status=SpriteLookupStatus::Failed;
    // 数据库记录的集合文件名与集合内真实精灵索引。
    std::string file;
    int index=-1;
    std::string reason;
};
// 解码后的 RGBA 像素；由共享指针持有，界面上传期间保持有效。
struct SpritePixels {
    std::shared_ptr<const std::vector<unsigned char>> rgba;
    int width=0,height=0;
};
// 有效内容的透明边界（像素坐标）；全透明时等于整张画布。
struct SpriteBounds { int x=0,y=0,width=0,height=0; };
// 解码与归档读取后端：生产绑定随包 CPK 桥接，隔离测试注入替身。
struct SpriteBackend {
    // 参数 database/name 为数据库字节与精确精灵名，返回集合文件与真实索引。
    std::function<SpriteLookup(const std::string& database,const std::string& name)> find;
    // 参数 farc/entry/reason 为归档字节、内部集合 BIN 名与失败原因。
    std::function<std::optional<std::string>(const std::string& farc,const std::string& entry,std::string& reason)> farcEntry;
    // 参数 spriteBinary/index/reason 为集合 BIN、真实索引与失败原因。
    std::function<std::optional<SpritePixels>(const std::string& spriteBinary,int index,std::string& reason)> decode;
};
// 一个已启用的 DML 资源根，按优先级从高到低排列。
struct SpriteModRoot {
    std::string label;
    // 参数 logical/exists 为相对逻辑路径与是否存在；存在但读取失败时返回空并置 exists=true。
    std::function<std::optional<std::string>(const std::string& logical,bool& exists)> read;
};
// 一次解析所需的资源环境：Mod 根、官方数据库与官方归档候选。
struct SpriteEnvironment {
    std::vector<SpriteModRoot> mods;
    // 官方数据库后缀按优先顺序尝试（本地化地区库优先，再通用库）。
    std::vector<std::string> officialDatabaseSuffixes;
    // 参数 suffix/label 为数据库后缀与来源说明输出；同后缀候选内容不一致时返回空，不任意挑选。
    std::function<std::optional<std::string>(const std::string& suffix,std::string& label)> officialDatabase;
    // 参数 farc/style/label/reason 为归档逻辑路径、图片风格、来源说明与失败原因。
    std::function<std::optional<std::string>(const std::string& farc,int style,std::string& label,std::string& reason)> officialArchive;
};
// 解析结果：fallback=true 表示专用图不可用而使用了同 ID 通用图，fallbackReason 保留专用图失败原因。
struct SpriteResult {
    SpritePixels pixels;
    SpriteBounds bounds;
    std::string source,spriteName,failure,fallbackReason;
    bool fallback=false;
    bool ok() const {return pixels.rgba && pixels.width>0 && pixels.height>0;}
};

// 参数 item/style 为实体与图片风格（1 MEGA39、2 Future Tone）；返回按优先顺序尝试的精确精灵名。
// 模型：SPR_SEL_MD%03dCMN_MD_IMG[_FT]；发型 KAMI 与四部位道具：SPR_CMNITM_THMB%03d_ITM_IMG[_FT]。FT 之后附同 ID 通用图。
std::vector<std::string> CustomizationSpriteNames(const CustomizationItem& item,int style);
// 参数 fileName 为数据库记录的集合文件名；返回 FARC 逻辑路径与归档内部集合 BIN 名。
std::pair<std::string,std::string> SpriteArchiveNames(const std::string& fileName);
// 参数 rgba/width/height/threshold 为像素与 alpha 阈值；返回有效内容边界，用于不拉伸、不裁轮廓的显示适配。
SpriteBounds ComputeSpriteBounds(const std::vector<unsigned char>& rgba,int width,int height,int threshold=8);
// 参数 backend/environment/names/style 为后端、资源环境、候选名与风格；按 DML 规则依次解析，首个成功者返回。
SpriteResult ResolveSprite(const SpriteBackend& backend,const SpriteEnvironment& environment,const std::vector<std::string>& names,int style);
}
