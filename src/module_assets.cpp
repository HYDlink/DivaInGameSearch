#include "module_assets.h"
#include "farc.h"
#include <Windows.h>
#include <toml.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <unordered_set>

namespace diva {
namespace {
constexpr size_t kMaximumCpkFiles = 128;
constexpr size_t kMaximumCpkEntries = 300000;
constexpr size_t kMaximumMatchedEntries = 256;
constexpr size_t kMaximumReadBytes = 64 * 1024 * 1024;
constexpr size_t kMaximumAggregateReadBytes = 256 * 1024 * 1024;
constexpr size_t kMaximumQueuedImages = 8;

// 参数 path 为文件系统路径；声明供 NativeAOT 加载器构造诊断使用。
std::string PathText(const fs::path& path);

// 返回当前插件模块目录；桥接会优先从主 DLL 同目录加载。
fs::path ModuleDirectory();

struct CpkApi {
    using Open = void* (__cdecl*)(const char*);
    using Count = int (__cdecl*)(void*);
    using GetName = int (__cdecl*)(void*, int, char*, int);
    using ReadEntry = int (__cdecl*)(void*, const char*, const unsigned char**, size_t*);
    using Free = void (__cdecl*)(void*);
    using Close = void (__cdecl*)(void*);
    using Version = int (__cdecl*)();
    using SpriteDatabaseFind = int (__cdecl*)(const unsigned char*, size_t, const char*, char*, int, int*);
    using SpriteDecode = int (__cdecl*)(const unsigned char*, size_t, int, unsigned char**, size_t*, int*, int*);
    using FarcRead = int (__cdecl*)(const unsigned char*,size_t,const char*,unsigned char**,size_t*);
    FarcRead farcRead=nullptr;

    HMODULE module = nullptr;
    Open open = nullptr;
    Count count = nullptr;
    GetName getName = nullptr;
    ReadEntry readEntry = nullptr;
    Free free = nullptr;
    Close close = nullptr;
    Version version = nullptr;
    SpriteDatabaseFind spriteDatabaseFind = nullptr;
    SpriteDecode spriteDecode = nullptr;
    std::mutex mutex;
    std::mutex nativeCallMutex;
    std::string loadedPath;

    // 参数 pluginRoot/diagnostics 为插件目录和中文诊断接收器；组件仅从随包 DLL 加载。
    bool Load(const fs::path& pluginRoot, std::vector<std::string>* diagnostics) {
        std::lock_guard lock(mutex);
        if (module)
            return true;

        std::vector<fs::path> candidates;
        if (const auto moduleDirectory=ModuleDirectory(); !moduleDirectory.empty()) {
            candidates.push_back(moduleDirectory/L"MmlCpkBridge.dll");
            candidates.push_back(moduleDirectory/L"assets"/L"MmlCpkBridge.dll");
            candidates.push_back(moduleDirectory/L"dependencies"/L"MmlCpkBridge.dll");
        }
        candidates.push_back(pluginRoot/L"MmlCpkBridge.dll");
        candidates.push_back(pluginRoot/L"assets"/L"MmlCpkBridge.dll");
        candidates.push_back(pluginRoot/L"dependencies"/L"MmlCpkBridge.dll");
        for (const auto& candidate:candidates) {
            std::error_code error;
            if (!fs::is_regular_file(candidate,error))
                continue;
            module=LoadLibraryExW(candidate.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (module) {
                loadedPath=PathText(candidate);
                break;
            }
        }

        if (!module) {
            if (diagnostics)
                diagnostics->push_back("未找到随包的 MmlCpkBridge.dll；官方 CPK 资源读取不可用。");
            return false;
        }

        open=reinterpret_cast<Open>(GetProcAddress(module,"DivaCpkOpen"));
        count=reinterpret_cast<Count>(GetProcAddress(module,"DivaCpkCount"));
        getName=reinterpret_cast<GetName>(GetProcAddress(module,"DivaCpkGetName"));
        readEntry=reinterpret_cast<ReadEntry>(GetProcAddress(module,"DivaCpkReadEntry"));
        free=reinterpret_cast<Free>(GetProcAddress(module,"DivaCpkFree"));
        close=reinterpret_cast<Close>(GetProcAddress(module,"DivaCpkClose"));
        version=reinterpret_cast<Version>(GetProcAddress(module,"DivaCpkBridgeVersion"));
        spriteDatabaseFind=reinterpret_cast<SpriteDatabaseFind>(GetProcAddress(module,"DivaSpriteDatabaseFind"));
        spriteDecode=reinterpret_cast<SpriteDecode>(GetProcAddress(module,"DivaSpriteDecode"));
        farcRead=reinterpret_cast<FarcRead>(GetProcAddress(module,"DivaFarcReadEntry"));
        if (!open || !count || !getName || !readEntry || !free || !close || !version || !spriteDatabaseFind || !spriteDecode || version()!=82380) {
            if (diagnostics)
                diagnostics->push_back("MmlCpkBridge.dll 接口版本不匹配或缺少精灵解码入口。");
            FreeLibrary(module);
            module=nullptr;
            open=nullptr;count=nullptr;getName=nullptr;readEntry=nullptr;free=nullptr;close=nullptr;version=nullptr;
            spriteDatabaseFind=nullptr;spriteDecode=nullptr;
            return false;
        }
        return true;
    }

    // 参数 pluginRoot 为插件目录；返回进程内固定 NativeAOT 桥接接口。
    static CpkApi& Instance() {
        static auto* api=new CpkApi;
        return *api;
    }
};

// 通过当前源地址获取宿主模块路径，支持开发版与发行版从 DLL 同目录加载桥接。
void ModuleAddressMarker() {}

// 获取当前插件模块所在目录；取不到路径时返回空路径并继续使用 pluginRoot 候选。
fs::path ModuleDirectory() {
    HMODULE module=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ModuleAddressMarker),&module))
        return {};
    std::vector<wchar_t> buffer(32768,L'\0');
    const DWORD length=GetModuleFileNameW(module,buffer.data(),static_cast<DWORD>(buffer.size()));
    if (length==0 || length>=buffer.size())
        return {};
    return fs::path(std::wstring(buffer.data(),length)).parent_path();
}

struct CpkHandle {
    CpkApi* api=nullptr;
    void* value=nullptr;
    // 参数 bridge/handle 为 NativeAOT 函数表和不透明 CPK 句柄。
    CpkHandle(CpkApi& bridge,void* handle):api(&bridge),value(handle) {}
    // 关闭 MikuMikuLibrary 打开的 CPK 流。
    ~CpkHandle() { if(value) api->close(value); }
    // 禁止复制不透明句柄，避免重复关闭。
    CpkHandle(const CpkHandle&)=delete;
    // 禁止复制赋值不透明句柄，避免重复关闭。
    CpkHandle& operator=(const CpkHandle&)=delete;
};

struct CpkRef {
    fs::path path;
    std::string internalPath;
    std::string logicalPath;
    std::string fingerprint;
};

// 参数 text 为 UTF-8 路径或归档名；统一斜线与 ASCII 大小写以匹配游戏逻辑路径。
std::string FoldPath(std::string text) {
    std::replace(text.begin(),text.end(),'\\','/');
    for (char& ch:text) if(static_cast<unsigned char>(ch)<0x80) ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

// 参数 path 为文件系统路径；保持非 ASCII 路径使用 UTF-8 编码。
std::string PathText(const fs::path& path) {
    const auto text=path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()),text.size());
}

// 参数 text 为 UTF-8 路径或配置字符串；转换时不经过系统 ANSI 代码页。
fs::path NativePath(const std::string& text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
}

// 参数 value/suffix 为区分大小写无关的逻辑路径与目标后缀。
bool EndsWith(const std::string& value,const std::string& suffix) {
    const auto a=FoldPath(value),b=FoldPath(suffix);
    return a.size()>=b.size() && a.compare(a.size()-b.size(),b.size(),b)==0;
}

// 参数 value/token 为逻辑路径与需匹配的目录片段。
bool HasSegment(const std::string& value,const std::string& token) {
    const auto path="/"+FoldPath(value)+"/";
    const auto segment="/"+FoldPath(token)+"/";
    return path.find(segment)!=std::string::npos;
}

// 参数 data 为归档或表内容；生成短本地指纹用于缓存失效，不承担安全校验。
std::string HashText(const std::string& data) {
    uint64_t hash=14695981039346656037ull;
    for(unsigned char byte:data) {hash^=byte;hash*=1099511628211ull;}
    std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;return out.str();
}

// 参数 path/internalPath/content 为资源文件、CPK 内路径和可选表文本；返回可读的稳定快照指纹。
std::string Fingerprint(const fs::path& path,const std::string& internalPath,const std::string& content={}) {
    std::error_code error;
    const auto size=fs::file_size(path,error);
    const auto stamp=fs::last_write_time(path,error).time_since_epoch().count();
    return HashText(PathText(path)+":"+std::to_string(size)+":"+std::to_string(stamp)+":"+internalPath+":"+HashText(content));
}

// 参数 root/relative 为受限根目录与相对资源路径；拒绝绝对路径和父目录逃逸。
std::optional<fs::path> Inside(const fs::path& root,const fs::path& relative) {
    if(relative.empty() || relative.is_absolute() || relative.has_root_name()) return {};
    for(const auto& part:relative) if(part=="..") return {};
    std::error_code error;
    const auto base=fs::weakly_canonical(root,error);if(error) return {};
    const auto target=fs::weakly_canonical(root/relative,error);if(error) return {};
    const auto sub=target.lexically_relative(base);
    if(sub.empty() && target!=base) return {};
    for(const auto& part:sub) if(part=="..") return {};
    return target;
}

// 参数 gameRoot/diagnostics 为游戏目录及中文诊断接收器；仅扫描游戏自带 CPK，不扫描 Mod 子目录。
std::vector<fs::path> FindOfficialCpks(const fs::path& gameRoot,std::vector<std::string>* diagnostics) {
    std::vector<fs::path> files;
    std::error_code error;
    if(!fs::is_directory(gameRoot,error)) {
        if(diagnostics) diagnostics->push_back("游戏目录不存在，无法扫描官方 CPK。");
        return files;
    }

    auto excluded=[&](const fs::path& path) {
        for(const auto& part:path) {
            const auto folded=FoldPath(PathText(part));
            if(folded=="mods" || folded=="build" || folded==".git" || folded=="divamodloader") return true;
        }
        return false;
    };
    for(fs::recursive_directory_iterator it(gameRoot,fs::directory_options::skip_permission_denied,error),end;
        it!=end && !error;it.increment(error)) {
        if(it->is_directory(error) && excluded(it->path().lexically_relative(gameRoot))) {it.disable_recursion_pending();continue;}
        if(it->is_regular_file(error) && FoldPath(PathText(it->path().extension()))==".cpk") {
            files.push_back(it->path());
            if(files.size()>=kMaximumCpkFiles) {
                if(diagnostics) diagnostics->push_back("官方 CPK 文件数量达到安全上限，只读取前 128 个候选。");
                break;
            }
        }
    }
    std::sort(files.begin(),files.end());
    return files;
}

// 参数 api/path/diagnostics 为桥接、CPK 路径和诊断接收器；读取每个 CPK 的条目名并应用安全上限。
std::vector<std::string> EnumerateCpk(CpkApi& api,const fs::path& path,std::vector<std::string>* diagnostics) {
    std::lock_guard nativeLock(api.nativeCallMutex);
    std::vector<std::string> names;
    const auto pathText=PathText(path);
    void* raw=api.open(pathText.c_str());
    if(!raw) {
        if(diagnostics) diagnostics->push_back("官方 CPK 无法打开："+PathText(path.filename()));
        return names;
    }
    CpkHandle handle(api,raw);
    const int count=api.count(raw);
    if(count<0 || static_cast<size_t>(count)>kMaximumCpkEntries) {
        if(diagnostics) diagnostics->push_back("官方 CPK 条目数量非法或超过上限："+PathText(path.filename()));
        return names;
    }
    names.reserve(static_cast<size_t>(count));
    for(int index=0;index<count;++index) {
        int length=api.getName(raw,index,nullptr,0);
        if(length<0 || length>32768) {
            if(diagnostics) diagnostics->push_back("CPK 条目名长度非法："+PathText(path.filename()));
            names.clear();return names;
        }
        std::string name(static_cast<size_t>(length)+1,'\0');
        const int copied=api.getName(raw,index,name.data(),static_cast<int>(name.size()));
        if(copied!=length) {
            if(diagnostics) diagnostics->push_back("CPK 条目名读取失败："+PathText(path.filename()));
            names.clear();return names;
        }
        name.resize(static_cast<size_t>(length));
        names.push_back(std::move(name));
    }
    return names;
}

// 参数 api/path/internalPath/diagnostics 为桥接、CPK路径、精确内部路径和诊断接收器。
std::optional<std::string> ReadCpkData(CpkApi& api,const fs::path& path,const std::string& internalPath,std::vector<std::string>* diagnostics) {
    std::lock_guard nativeLock(api.nativeCallMutex);
    const auto pathText=PathText(path);
    void* raw=api.open(pathText.c_str());
    if(!raw) {
        if(diagnostics) diagnostics->push_back("官方 CPK 无法打开："+PathText(path.filename()));
        return {};
    }
    CpkHandle handle(api,raw);
    const unsigned char* bytes=nullptr;size_t length=0;
    const int result=api.readEntry(raw,internalPath.c_str(),&bytes,&length);
    if(result!=0 || length>kMaximumReadBytes || (length && !bytes)) {
        if(diagnostics) diagnostics->push_back("官方 CPK 条目读取失败或超限："+PathText(path.filename())+" / "+internalPath);
        if(bytes) api.free(const_cast<unsigned char*>(bytes));
        return {};
    }
    std::string data;
    if(length) data.assign(reinterpret_cast<const char*>(bytes),length);
    if(bytes) api.free(const_cast<unsigned char*>(bytes));
    return data;
}

// 参数 gameRoot/pluginRoot/suffix/diagnostics 为游戏目录、插件目录、逻辑路径后缀和诊断接收器。
std::vector<OfficialCpkEntry> ReadEntriesInternal(const fs::path& gameRoot,const fs::path& pluginRoot,
                                                  const std::string& suffix,std::vector<std::string>& diagnostics) {
    std::vector<OfficialCpkEntry> output;
    auto& api=CpkApi::Instance();
    if(!api.Load(pluginRoot,&diagnostics)) return output;
    const auto cpks=FindOfficialCpks(gameRoot,&diagnostics);
    size_t totalBytes=0;
    for(const auto& cpk:cpks) {
        const auto names=EnumerateCpk(api,cpk,&diagnostics);
        size_t matched=0;
        for(const auto& name:names) {
            if(!EndsWith(name,suffix)) continue;
            if(output.size()>=kMaximumMatchedEntries) {
                diagnostics.push_back("官方 CPK 后缀匹配数量达到安全上限；已截断后续候选。");
                return output;
            }
            auto data=ReadCpkData(api,cpk,name,&diagnostics);
            if(!data) continue;
            if(totalBytes+data->size()>kMaximumAggregateReadBytes) {
                diagnostics.push_back("官方 CPK 后缀数据达到总读取上限；已截断后续候选。");
                return output;
            }
            totalBytes+=data->size();
            OfficialCpkEntry entry;
            entry.cpkPath=cpk;entry.internalPath=name;entry.logicalPath=name;entry.entryData=std::move(*data);
            entry.fingerprint=Fingerprint(cpk,name,entry.entryData);
            output.push_back(std::move(entry));
            ++matched;
        }
        if(matched)
            diagnostics.push_back("官方 CPK 条目候选："+PathText(cpk.filename())+" / 后缀 "+suffix+" / "+std::to_string(matched)+" 条");
    }
    return output;
}

// 参数 data 为 RGBA 字节；返回内容摘要以识别跨 CPK 的相同候选资源。
std::string DataFingerprint(const std::string& data) { return HashText(data); }

// 参数 api/database/name 为桥接、精灵数据库和目标 sprite 名；返回文件名与实际精灵索引。
std::optional<std::pair<std::string,int>> FindSprite(CpkApi& api,const std::string& database,const std::string& name) {
    std::lock_guard nativeLock(api.nativeCallMutex);
    int index=-1;
    int length=api.spriteDatabaseFind(reinterpret_cast<const unsigned char*>(database.data()),database.size(),name.c_str(),nullptr,0,&index);
    if(length<0 || length>32768) return {};
    std::string fileName(static_cast<size_t>(length)+1,'\0');
    const int result=api.spriteDatabaseFind(reinterpret_cast<const unsigned char*>(database.data()),database.size(),name.c_str(),fileName.data(),static_cast<int>(fileName.size()),&index);
    if(result!=length || index<0) return {};
    fileName.resize(static_cast<size_t>(length));
    return std::pair{std::move(fileName),index};
}

// 参数 fileName 为数据库保存的集合文件名；返回 FARC 逻辑路径及其内部 BIN 条目名。
std::pair<std::string,std::string> ArchiveNames(const std::string& fileName) {
    std::string value=FoldPath(fileName);
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

// 参数 data/index/api/width/height/source 为 sprite BIN、真实索引、桥接、输出大小和来源。
struct DecodedImage {
    std::shared_ptr<const std::vector<unsigned char>> pixels;
    int width=0,height=0;
    std::string source;
};

// 参数 api/spriteBinary/index/source 为桥接、MML SpriteSet BIN、SpriteDatabase 实际索引和资源来源。
std::shared_ptr<const DecodedImage> DecodeSprite(CpkApi& api,const std::string& spriteBinary,int index,const std::string& source,
                                                 std::string& failureReason) {
    std::lock_guard nativeLock(api.nativeCallMutex);
    unsigned char* raw=nullptr;size_t size=0;int width=0,height=0;
    const int result=api.spriteDecode(reinterpret_cast<const unsigned char*>(spriteBinary.data()),spriteBinary.size(),index,&raw,&size,&width,&height);
    if(result!=0 || !raw || size==0 || size>kMaximumReadBytes || width<=0 || height<=0 || static_cast<uint64_t>(width)*height*4!=size) {
        if(raw) api.free(raw);
        failureReason="SpriteDecode 返回码 "+std::to_string(result)+"；输出 "+std::to_string(width)+"x"+std::to_string(height)+" / "+std::to_string(size)+" 字节";
        return {};
    }
    auto image=std::make_shared<DecodedImage>();
    auto pixels=std::make_shared<std::vector<unsigned char>>(raw,raw+size);
    api.free(raw);
    image->pixels=std::move(pixels);image->width=width;image->height=height;image->source=source;
    return image;
}

struct AssetKey {
    CustomizationKey item;
    int style=0;
    auto operator<=>(const AssetKey&) const=default;
};

struct CacheEntry {
    std::shared_ptr<const DecodedImage> image;
    uint64_t lastUse=0;
};

struct PendingAsset {
    CustomizationItem item;
    int style=0;
};

// 参数 root/relative 为 DML 根目录和受限相对路径；canonical 检查阻止来自Mod链接的目录逃逸。
std::optional<fs::path> DmlInside(const fs::path& root,const std::string& relative) {
    return Inside(root,NativePath(relative));
}

// 参数 gameRoot 为游戏目录；读取 DML 当前 mods 相对路径，失败时使用官方默认值。
fs::path DmlModsRoot(const fs::path& gameRoot,std::vector<std::string>* diagnostics) {
    std::ifstream input(gameRoot/"config.toml",std::ios::binary);
    if(!input.good()) return gameRoot/"mods";
    try {
        const auto config=toml::parse(input,PathText(gameRoot/"config.toml"));
        const auto relative=config["mods"].value_or<std::string>("mods");
        auto resolved=Inside(gameRoot,NativePath(relative));
        if(resolved) return *resolved;
        if(diagnostics) diagnostics->push_back("DML mods 路径无效，图片仅检查官方 CPK。");
    } catch(const std::exception&) {
        if(diagnostics) diagnostics->push_back("DML 配置无法解析，图片仅检查官方 CPK。");
    }
    return gameRoot/"mods";
}

// 参数 root/source 为 Mod 集合目录和目录来源；返回其安全 include 根目录。
std::optional<fs::path> ModInclude(const fs::path& modsRoot,const CustomizationSource& source) {
    if(source.modDirectory.empty()) return {};
    auto mod=Inside(modsRoot,NativePath(source.modDirectory));
    if(!mod) return {};
    return DmlInside(*mod,source.includeRoot.empty()?".":source.includeRoot);
}

// 参数 relative/data 为文件路径和输出容量；读取单个安全图片资源，拒绝超大文件。
std::optional<std::string> ReadSmallFile(const fs::path& relative,size_t limit=kMaximumReadBytes) {
    std::ifstream input(relative,std::ios::binary|std::ios::ate);
    const auto length=input.tellg();
    if(length<0 || static_cast<uint64_t>(length)>limit) return {};
    std::string data(static_cast<size_t>(length),'\0');input.seekg(0);
    if(!data.empty() && !input.read(data.data(),static_cast<std::streamsize>(data.size()))) return {};
    return data;
}

// 参数 api/database/name 为 MML 接口、候选数据库文本和目标名；检查名字唯一性后返回已验证索引。
std::optional<std::pair<std::string,int>> LookupSprite(CpkApi& api,const std::string& database,const std::string& name,
                                                       std::vector<std::string>* diagnostics,const std::string& dbSource,
                                                       std::string* failureReason=nullptr) {
    std::lock_guard nativeLock(api.nativeCallMutex);
    int index=-1;
    int length=api.spriteDatabaseFind(reinterpret_cast<const unsigned char*>(database.data()),database.size(),name.c_str(),nullptr,0,&index);
    if(length==-2) {
        if(failureReason) *failureReason="SpriteDatabaseFind 返回 -2（目标精灵名不存在）";
        return {};
    }
    if(length==-4) {
        if(failureReason) *failureReason="SpriteDatabaseFind 返回 -4（目标精灵名重复，拒绝猜测）";
        if(diagnostics) diagnostics->push_back("精灵数据库存在重复名称，拒绝猜测索引："+name+" / "+dbSource);
        return {};
    }
    if(length<0 || length>32768) {
        if(failureReason) *failureReason="SpriteDatabaseFind 返回 "+std::to_string(length)+"（数据库读取或解析失败）";
        if(diagnostics) diagnostics->push_back("精灵数据库解析失败："+dbSource);
        return {};
    }
    std::string fileName(static_cast<size_t>(length)+1,'\0');
    const int result=api.spriteDatabaseFind(reinterpret_cast<const unsigned char*>(database.data()),database.size(),name.c_str(),fileName.data(),static_cast<int>(fileName.size()),&index);
    if(result!=length || index<0) {
        if(failureReason) *failureReason="SpriteDatabaseFind 输出阶段返回 "+std::to_string(result)+"，预期 "+std::to_string(length)+"，索引 "+std::to_string(index);
        return {};
    }
    fileName.resize(static_cast<size_t>(length));
    return std::pair{std::move(fileName),index};
}

using OfficialModuleFields = std::map<std::string,std::string>;

struct OfficialModuleFacts {
    std::set<int> ids;
    std::map<int,OfficialModuleFields> fieldsById;
    size_t duplicateIds=0;
};

// 从 UTF-8 module.<序号>.<字段>=值文本提取稳定ID与原始字段，供候选表比较使用。
OfficialModuleFacts ParseOfficialModuleFacts(const std::string& text) {
    std::map<int,OfficialModuleFields> byIndex;
    std::istringstream input(text);
    std::string line;
    while(std::getline(input,line)) {
        const auto first=line.find_first_not_of(" \t\r");
        if(first==std::string::npos || line[first]=='#') continue;
        const auto equals=line.find('=',first);
        if(equals==std::string::npos) continue;
        const std::string key=line.substr(first,equals-first);
        if(!key.starts_with("module.")) continue;
        const auto indexEnd=key.find('.',7);
        if(indexEnd==std::string::npos) continue;
        int index=-1;
        const auto parsed=std::from_chars(key.data()+7,key.data()+indexEnd,index);
        if(parsed.ec!=std::errc{} || parsed.ptr!=key.data()+indexEnd || index<0 || index>=100000) continue;
        const std::string field=key.substr(indexEnd+1);
        if(field.empty()) continue;
        const auto valueStart=line.find_first_not_of(" \t",equals+1);
        const auto valueEnd=line.find_last_not_of(" \t\r");
        const std::string value=valueStart==std::string::npos || valueEnd<valueStart?std::string{}:line.substr(valueStart,valueEnd-valueStart+1);
        byIndex[index].insert_or_assign(field,value);
    }

    OfficialModuleFacts facts;
    for(auto& [index,fields]:byIndex) {
        (void)index;
        auto idField=fields.find("id");
        if(idField==fields.end()) continue;
        int id=-1;
        const auto parsed=std::from_chars(idField->second.data(),idField->second.data()+idField->second.size(),id);
        if(parsed.ec!=std::errc{} || parsed.ptr!=idField->second.data()+idField->second.size() || id<0) continue;
        if(!facts.ids.insert(id).second) ++facts.duplicateIds;
        facts.fieldsById.insert_or_assign(id,std::move(fields));
    }
    return facts;
}

// 将缺失ID压缩为有限示例，避免诊断输出变成完整游戏元数据副本。
std::string IdExamples(const std::set<int>& ids) {
    if(ids.empty()) return "无";
    std::ostringstream output;
    size_t shown=0;
    for(int id:ids) {
        if(shown) output<<",";
        output<<id;
        if(++shown==8) break;
    }
    if(ids.size()>shown) output<<",…";
    return output.str();
}

// 对所有官方服装表成对比较ID集合及非名称字段，诊断中保留缺失与冲突示例但不猜来源优先级。
void CompareOfficialModuleTables(const std::vector<OfficialModuleTable>& tables,std::vector<std::string>& diagnostics) {
    if(tables.size()<2) return;
    std::vector<OfficialModuleFacts> facts;
    facts.reserve(tables.size());
    for(const auto& table:tables) facts.push_back(ParseOfficialModuleFacts(table.tableText));

    for(size_t left=0;left<tables.size();++left) {
        if(facts[left].duplicateIds)
            diagnostics.push_back("官方服装表存在重复稳定ID："+PathText(tables[left].cpkPath.filename())+" / "+tables[left].logicalPath+" / 重复 "+std::to_string(facts[left].duplicateIds)+" 个");
        for(size_t right=left+1;right<tables.size();++right) {
            std::set<int> onlyLeft,onlyRight,overlap;
            std::set_difference(facts[left].ids.begin(),facts[left].ids.end(),facts[right].ids.begin(),facts[right].ids.end(),std::inserter(onlyLeft,onlyLeft.end()));
            std::set_difference(facts[right].ids.begin(),facts[right].ids.end(),facts[left].ids.begin(),facts[left].ids.end(),std::inserter(onlyRight,onlyRight.end()));
            std::set_intersection(facts[left].ids.begin(),facts[left].ids.end(),facts[right].ids.begin(),facts[right].ids.end(),std::inserter(overlap,overlap.end()));

            size_t differingFields=0;
            std::set<int> conflictingIds;
            std::vector<std::string> examples;
            for(int id:overlap) {
                const auto& a=facts[left].fieldsById.at(id);
                const auto& b=facts[right].fieldsById.at(id);
                std::set<std::string> fieldNames;
                for(const auto& [field,value]:a) { (void)value; if(field!="name") fieldNames.insert(field); }
                for(const auto& [field,value]:b) { (void)value; if(field!="name") fieldNames.insert(field); }
                bool itemConflicts=false;
                for(const auto& field:fieldNames) {
                    const auto av=a.find(field),bv=b.find(field);
                    if(av!=a.end() && bv!=b.end() && av->second==bv->second) continue;
                    ++differingFields;
                    itemConflicts=true;
                    if(examples.size()<4) {
                        const std::string leftValue=av==a.end()?"<缺字段>":av->second;
                        const std::string rightValue=bv==b.end()?"<缺字段>":bv->second;
                        examples.push_back(std::to_string(id)+"."+field+"("+leftValue+" ↔ "+rightValue+")");
                    }
                }
                if(itemConflicts) conflictingIds.insert(id);
            }

            std::ostringstream message;
            message<<"官方服装表对比："<<PathText(tables[left].cpkPath.filename())<<"["<<tables[left].logicalPath<<"] ID="<<facts[left].ids.size()
                   <<" ↔ "<<PathText(tables[right].cpkPath.filename())<<"["<<tables[right].logicalPath<<"] ID="<<facts[right].ids.size()
                   <<"；交集="<<overlap.size()<<"，仅左="<<onlyLeft.size()<<"（"<<IdExamples(onlyLeft)<<"），仅右="<<onlyRight.size()<<"（"<<IdExamples(onlyRight)
                   <<"），非名称字段冲突="<<differingFields<<" 个字段/"<<conflictingIds.size()<<" 个ID";
            if(!examples.empty()) {
                message<<"，示例：";
                for(size_t i=0;i<examples.size();++i) { if(i) message<<"；"; message<<examples[i]; }
            }
            diagnostics.push_back(message.str());
        }
    }
}

} // namespace

// 参数 gameRoot/pluginRoot/suffix/diagnostics 为游戏目录、插件目录、逻辑路径后缀和中文诊断接收器。
std::vector<OfficialCpkEntry> ReadOfficialCpkEntries(const fs::path& gameRoot,const fs::path& pluginRoot,
                                                      const std::string& suffix,std::vector<std::string>& diagnostics) {
    if(suffix.empty()) {diagnostics.push_back("官方 CPK 查询后缀不能为空。");return {};}
    return ReadEntriesInternal(gameRoot,pluginRoot,suffix,diagnostics);
}

// 参数 gameRoot/pluginRoot/diagnostics 为游戏目录、插件目录和中文诊断接收器；读取全部官方基础及DLC实体表候选。
std::vector<OfficialModuleTable> ReadOfficialModuleTables(const fs::path& gameRoot,const fs::path& pluginRoot,
                                                           std::vector<std::string>& diagnostics) {
    std::vector<OfficialModuleTable> tables;
    auto archives=ReadEntriesInternal(gameRoot,pluginRoot,"gm_module_tbl.farc",diagnostics);
    for(auto& archive:archives) {
        std::string reason;
        auto text=ReadFarcEntryMemory(archive.entryData,"gm_module_id.bin",reason);
        if(!text) {
            diagnostics.push_back("官方服装表归档缺少可读 gm_module_id.bin："+PathText(archive.cpkPath.filename())+" / "+archive.internalPath+"："+reason);
            continue;
        }
        OfficialModuleTable table;
        table.cpkPath=archive.cpkPath;
        table.internalPath=archive.internalPath+"!/gm_module_id.bin";
        table.logicalPath=archive.logicalPath+"!/gm_module_id.bin";
        table.tableText=std::move(*text);
        table.fingerprint=Fingerprint(table.cpkPath,table.internalPath,table.tableText);
        diagnostics.push_back("官方服装表候选："+PathText(table.cpkPath.filename())+" / "+table.logicalPath+" / 文本 "+std::to_string(table.tableText.size())+" 字节 / 指纹 "+table.fingerprint);
        tables.push_back(std::move(table));
    }

    // 某些构建把表文本直接放入 CPK；只接受标准 UTF-8 文本，不解析归档结构。
    auto direct=ReadEntriesInternal(gameRoot,pluginRoot,"gm_module_id.bin",diagnostics);
    for(auto& entry:direct) {
        if(entry.entryData.find("module") == std::string::npos) continue;
        OfficialModuleTable table;
        table.cpkPath=entry.cpkPath;table.internalPath=entry.internalPath;table.logicalPath=entry.logicalPath;
        table.tableText=std::move(entry.entryData);table.fingerprint=Fingerprint(table.cpkPath,table.internalPath,table.tableText);
        diagnostics.push_back("官方直读服装表候选："+PathText(table.cpkPath.filename())+" / "+table.logicalPath+" / 文本 "+std::to_string(table.tableText.size())+" 字节 / 指纹 "+table.fingerprint);
        tables.push_back(std::move(table));
    }
    CompareOfficialModuleTables(tables,diagnostics);
    return tables;
}

struct ModuleAssets::Impl {
    fs::path gameRoot,pluginRoot,modsRoot;
    std::shared_ptr<const CustomizationCatalog> catalog;
    CpkApi* api=nullptr;
    std::vector<CpkRef> spriteDatabases,archives;
    std::mutex mutex,joinMutex;
    std::condition_variable wake;
    std::deque<PendingAsset> queue;
    std::set<AssetKey> queued,failed;
    // 后台单线程写入阶段诊断，只有完成后才在互斥区发布给界面。
    std::string lastFailure;
    std::map<AssetKey,std::string> failureReasons;
    std::map<AssetKey,CacheEntry> cache;
    std::thread worker;
    bool stopping=false;
    bool bridgeReady=false;
    size_t maxImages=48,maxBytes=96*1024*1024,currentBytes=0;
    uint64_t touchCounter=0;

    // 参数 game/plugin/catalog/diagnostics 为目录和不可变快照；建立只含必要资源名的CPK索引。
    Impl(const fs::path& game,const fs::path& plugin,std::shared_ptr<const CustomizationCatalog> snapshot,
         std::vector<std::string>& diagnostics)
        :gameRoot(game),pluginRoot(plugin),catalog(std::move(snapshot)),modsRoot(DmlModsRoot(game,&diagnostics)),api(&CpkApi::Instance()) {
        bridgeReady=api && api->Load(pluginRoot,&diagnostics);
        if(bridgeReady) {
            IndexOfficial(diagnostics);
            worker=std::thread([this]{Worker();});
        }
    }

    // 参数 diagnostics 为后台构造阶段的中文诊断；只保留 SpriteDatabase 和服装 sprite FARC 条目。
    void IndexOfficial(std::vector<std::string>& diagnostics) {
        for(const auto& cpk:FindOfficialCpks(gameRoot,&diagnostics)) {
            auto names=EnumerateCpk(*api,cpk,&diagnostics);
            size_t dbCount=0,archiveCount=0;
            for(const auto& name:names) {
                const auto folded=FoldPath(name);
                if(EndsWith(folded,"spr_db.bin") && folded.find("rom_steam")!=std::string::npos) {
                    spriteDatabases.push_back({cpk,name,name,Fingerprint(cpk,name)});++dbCount;
                } else if(folded.find("spr_sel_md")!=std::string::npos && EndsWith(folded,".farc")) {
                    archives.push_back({cpk,name,name,Fingerprint(cpk,name)});++archiveCount;
                }
            }
            if(dbCount || archiveCount)
                diagnostics.push_back("官方预览索引："+PathText(cpk.filename())+" / sprite DB "+std::to_string(dbCount)+" 个 / 服装归档 "+std::to_string(archiveCount)+" 个");
        }
    }

    // 参数 key 为稳定服装键与专用预览样式；统一淘汰超过数量或字节上限的旧缓存。
    void TrimCache() {
        while(!cache.empty() && (cache.size()>maxImages || currentBytes>maxBytes)) {
            auto oldest=std::min_element(cache.begin(),cache.end(),[](const auto& a,const auto& b){return a.second.lastUse<b.second.lastUse;});
            if(oldest==cache.end()) break;
            if(oldest->second.image && oldest->second.image->pixels) currentBytes-=oldest->second.image->pixels->size();
            cache.erase(oldest);
        }
    }

    // 参数 item/style 为稳定服装与图片专用样式；资源选择严格依赖 SpriteDatabase 名称及索引。
    std::shared_ptr<const DecodedImage> Build(const CustomizationItem& item,int style) {
        lastFailure="精灵数据库没有可用映射。";
        if(!bridgeReady || !api || !api->open || !api->readEntry || !api->free || !api->close ||
           !api->spriteDatabaseFind || !api->spriteDecode)
            return {};
        if(style!=static_cast<int>(ModuleSpriteStyle::Mega39) && style!=static_cast<int>(ModuleSpriteStyle::FutureTone)) return {};
        std::ostringstream spriteName;
        spriteName<<"SPR_SEL_MD"<<std::setw(3)<<std::setfill('0')<<item.key.id<<"CMN_";
        spriteName<<(style==static_cast<int>(ModuleSpriteStyle::Mega39)?"MD_IMG":"MD_IMG_FT");
        std::string targetName=spriteName.str();
        // FT专用精灵缺失时查同一数据库中的通用精灵，保留实际命中名称用于来源说明。
        auto lookupDatabase=[&](const std::string& bytes,const std::string& source) {
            auto match=LookupSprite(*api,bytes,targetName,nullptr,source,&lastFailure);
            if(!match && style==static_cast<int>(ModuleSpriteStyle::FutureTone) && targetName.ends_with("_FT")) {
                auto common=targetName.substr(0,targetName.size()-3);
                match=LookupSprite(*api,bytes,common,nullptr,source,&lastFailure);
                if(match) targetName=std::move(common);
            }
            return match;
        };

        std::optional<std::pair<std::string,int>> resolved;
        std::string databaseSource;
        std::string databaseBytes;
        bool ordinaryDatabaseReplaced=false;
        if(catalog) {
            // DML追加数据库在普通数据库之后合并；普通同逻辑文件只接受最高优先来源。
            for(const char* dbName:{"rom/2d/mod_spr_db.bin","rom/2d/spr_db.bin"}) {
            for(const auto& source:catalog->sources) {
                if(!source.currentMod) continue;
                auto include=ModInclude(modsRoot,source);if(!include) continue;
                auto path=Inside(*include,NativePath(dbName));
                if(!path) continue;
                std::error_code error;if(!fs::is_regular_file(*path,error)) continue;
                const bool ordinary=std::string(dbName)=="rom/2d/spr_db.bin";
                if(ordinary) ordinaryDatabaseReplaced=true;
                auto data=ReadSmallFile(*path,16*1024*1024);if(!data) {if(ordinary) break;continue;}
                auto match=lookupDatabase(*data,source.name);
                if(!match) {if(ordinary) break;continue;}
                resolved=std::move(match);databaseBytes=std::move(*data);
                databaseSource="Mod / "+source.name+" / "+dbName;
                break;
            }
            if(resolved) break;
            }
        }

        if(!resolved && !ordinaryDatabaseReplaced) {
            const std::string locale=catalog?FoldPath(catalog->language):"cn";
            const std::string localizedSuffix="rom_steam_"+locale+"/rom/2d/spr_db.bin";
            auto lookupOfficialDatabase=[&](const std::string& suffix)->bool {
                std::string candidateBytes,candidateSource;
                for(const auto& ref:spriteDatabases) {
                    if(!EndsWith(ref.internalPath,suffix)) continue;
                    auto data=ReadCpkData(*api,ref.path,ref.internalPath,nullptr);if(!data) continue;
                    if(!candidateBytes.empty() && DataFingerprint(candidateBytes)!=DataFingerprint(*data))
                        return false;
                    if(candidateBytes.empty()) {
                        candidateBytes=std::move(*data);
                        candidateSource="官方 CPK / "+PathText(ref.path.filename())+" / "+ref.internalPath;
                    }
                }
                if(candidateBytes.empty()) return false;
                auto match=lookupDatabase(candidateBytes,candidateSource);
                if(!match) return false;
                databaseBytes=std::move(candidateBytes);
                databaseSource=std::move(candidateSource);
                resolved=std::move(match);
                return true;
            };
            if(!lookupOfficialDatabase(localizedSuffix))
                lookupOfficialDatabase("rom_steam/rom/2d/spr_db.bin");
        }

        if(!resolved) return {};
        lastFailure="精灵已映射，但资源归档未匹配："+resolved->first;
        const auto [farcPath,internalBin]=ArchiveNames(resolved->first);
        std::string farcData,assetSource;

        if(catalog) {
            for(const auto& source:catalog->sources) {
                if(!source.currentMod) continue;
                auto include=ModInclude(modsRoot,source);if(!include) continue;
                auto archivePath=Inside(*include,NativePath(farcPath));
                if(!archivePath) continue;
                std::error_code error;if(!fs::is_regular_file(*archivePath,error)) continue;
                auto bytes=ReadSmallFile(*archivePath);if(!bytes) continue;
                farcData=std::move(*bytes);assetSource="Mod / "+source.name+" / "+farcPath;break;
            }
        }

        if(farcData.empty()) {
            std::vector<CpkRef> candidates;
            for(const auto& ref:archives) {
                const auto platform=FoldPath(ref.internalPath);
                if(style==static_cast<int>(ModuleSpriteStyle::FutureTone) && !platform.starts_with("rom_ps4/" ) && !platform.starts_with("rom_ps4_dlc/") && !platform.starts_with("rom_ps4_patch/") && !platform.starts_with("rom_ps4_fix/") && !platform.starts_with("rom_steam/")) continue;
                if(style==static_cast<int>(ModuleSpriteStyle::Mega39) && !platform.starts_with("rom_switch/") && !platform.starts_with("rom_steam/")) continue;
                if(EndsWith(ref.internalPath,farcPath)) candidates.push_back(ref);
                else if(farcPath.find('/')!=std::string::npos &&
                        FoldPath(ref.internalPath).find("/rom_ps4/")!=std::string::npos &&
                        EndsWith(ref.internalPath,fs::path(NativePath(farcPath)).filename().string())) candidates.push_back(ref);
            }
            if(candidates.empty()) return {};

            std::vector<std::pair<CpkRef,std::string>> loaded;
            for(const auto& ref:candidates) {
                auto bytes=ReadCpkData(*api,ref.path,ref.internalPath,nullptr);
                if(bytes) loaded.emplace_back(ref,std::move(*bytes));
            }
            if(loaded.empty()) return {};
            const auto canonicalHash=DataFingerprint(loaded.front().second);
            for(size_t i=1;i<loaded.size();++i) if(DataFingerprint(loaded[i].second)!=canonicalHash) {lastFailure="图片资源存在不同内容的同优先候选，拒绝猜测。";return {};}
            farcData=std::move(loaded.front().second);
            assetSource="官方 CPK / "+PathText(loaded.front().first.path.filename())+" / "+loaded.front().first.internalPath;
        }

        std::string reason;
        auto spriteBinary=ReadFarcEntryMemory(farcData,internalBin,reason);
        if(!spriteBinary && api->farcRead) {
            std::lock_guard nativeLock(api->nativeCallMutex);
            unsigned char* bytes=nullptr;size_t length=0;
            const auto result=api->farcRead(reinterpret_cast<const unsigned char*>(farcData.data()),farcData.size(),internalBin.c_str(),&bytes,&length);
            if(result==0 && bytes && length<=kMaximumReadBytes) spriteBinary=std::string(reinterpret_cast<const char*>(bytes),length);
            if(bytes) api->free(bytes);
            if(!spriteBinary) reason="MML扩展FARC读取返回 "+std::to_string(result);
        }
        if(!spriteBinary) {lastFailure="图片归档读取失败："+reason+" / "+assetSource;return {};}
        assetSource+=" / sprite "+targetName+" / "+std::to_string(resolved->second);
        auto image=DecodeSprite(*api,*spriteBinary,resolved->second,assetSource,reason);
        if(!image) lastFailure="图片像素解码失败："+reason+" / "+assetSource;
        return image;
    }

    // 后台单线程消费有限队列；CPK读取、FARC解压和MML像素解码均不运行在界面帧。
    void Worker() {
        for(;;) {
            PendingAsset request;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock,[&]{return stopping || !queue.empty();});
                if(stopping) return;
                request=std::move(queue.front());queue.pop_front();
                queued.erase({request.item.key,request.style});
            }

            std::shared_ptr<const DecodedImage> image;
            try { image=Build(request.item,request.style); }
            catch(const std::exception&) {lastFailure="图片资源读取出现异常，已中止本项。";}
            catch(...) {lastFailure="图片资源读取失败，已中止本项。";}
            std::lock_guard lock(mutex);
            const AssetKey key{request.item.key,request.style};
            if(image && image->pixels) {
                auto previous=cache.find(key);
                if(previous!=cache.end() && previous->second.image && previous->second.image->pixels)
                    currentBytes-=previous->second.image->pixels->size();
                currentBytes+=image->pixels->size();
                cache[key]={std::move(image),++touchCounter};
                failed.erase(key);
                TrimCache();
            } else {
                failed.insert(key);
                failureReasons[key]=lastFailure;
            }
        }
    }

    // 参数 item/requestVisible/style 为服装项、可见请求标记和专用样式；使用 try-lock 保证不阻塞渲染帧。
    ModuleAssetPreview Peek(const CustomizationItem& item,bool requestVisible,int style) {
        ModuleAssetPreview preview;
        if(style!=static_cast<int>(ModuleSpriteStyle::Mega39) && style!=static_cast<int>(ModuleSpriteStyle::FutureTone)) {
            preview.source="预览样式未确认；请选择 MEGA39/MM 或 Future Tone。";
            return preview;
        }
        if(!bridgeReady) {
            preview.source="官方资源桥接未加载；当前预览不可用。";
            return preview;
        }
        std::unique_lock lock(mutex,std::try_to_lock);
        if(!lock.owns_lock()) return preview;
        const AssetKey key{item.key,style};
        auto cached=cache.find(key);
        if(cached!=cache.end() && cached->second.image) {
            cached->second.lastUse=++touchCounter;
            preview.width=cached->second.image->width;
            preview.height=cached->second.image->height;
            preview.source=cached->second.image->source;
            preview.rgbaOwner=cached->second.image->pixels;
            preview.rgbaPixels=preview.rgbaOwner?preview.rgbaOwner->data():nullptr;
            return preview;
        }
        if(requestVisible && !stopping && !failed.contains(key) && !queued.contains(key) && queue.size()<kMaximumQueuedImages) {
            queue.push_back({item,style});queued.insert(key);wake.notify_one();
        }
        preview.source=failed.contains(key)?"图片不可用："+failureReasons[key]:"图片读取中。";
        return preview;
    }

    // 参数 maxImageCount/maxDecodedBytes 为 LRU 精灵数和解码像素总量上限。
    void SetLimits(size_t maxImageCount,size_t maxDecodedBytes) {
        std::lock_guard lock(mutex);
        maxImages=std::max<size_t>(1,maxImageCount);maxBytes=std::max<size_t>(4,maxDecodedBytes);TrimCache();
    }

    // 参数 wait 为是否等待当前后台条目；不等待时仍保留线程对象供析构安全 join。
    void Stop(bool wait) {
        {
            std::lock_guard lock(mutex);
            stopping=true;queue.clear();queued.clear();
        }
        wake.notify_all();
        if(wait) {
            std::lock_guard joinLock(joinMutex);
            if(worker.joinable()) worker.join();
        }
    }

    // 等待后台当前读取完成，保证句柄和缓存成员仍有效。
    ~Impl() { Stop(true); }
};

// 参数 gameRoot/pluginRoot/catalog/diagnostics 为资源目录和不可变目录快照；此构造应在宿主 future 后台执行。
ModuleAssets::ModuleAssets(const fs::path& gameRoot,const fs::path& pluginRoot,
                           std::shared_ptr<const CustomizationCatalog> catalog,std::vector<std::string>& diagnostics)
    :impl_(std::make_unique<Impl>(gameRoot,pluginRoot,std::move(catalog),diagnostics)) {}

// 释放资源对象；停止并等待受控后台解码线程。
ModuleAssets::~ModuleAssets()=default;

// 参数 item/requestVisible/style 为稳定服装项、是否排队请求可见资源和专用 sprite 风格令牌。
ModuleAssetPreview ModuleAssets::Peek(const CustomizationItem& item,bool requestVisible,int style) {
    return impl_?impl_->Peek(item,requestVisible,style):ModuleAssetPreview{};
}

// 参数 maxImages/maxDecodedBytes 为缓存精灵数和 RGBA 字节上限。
void ModuleAssets::SetCacheLimits(size_t maxImages,size_t maxDecodedBytes) {
    if(impl_) impl_->SetLimits(maxImages,maxDecodedBytes);
}

// 参数 wait 为是否等待当前后台条目；对象析构时总会安全等待并回收 worker。
void ModuleAssets::Shutdown(bool wait) {
    if(impl_) impl_->Stop(wait);
}

} // namespace diva
