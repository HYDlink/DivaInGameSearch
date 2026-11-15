#include "ui_color_profile.h"
#include <charconv>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

namespace diva {
namespace {
// 参数 value 为配置字段；去除空白、行尾与注释，不改变实际字段内容。
std::string_view Trim(std::string_view value) {
    const auto start=value.find_first_not_of(" \t\r\n");if(start==value.npos) return {};
    value.remove_prefix(start);return value.substr(0,value.find_last_not_of(" \t\r\n")+1);
}
// 参数 value 为完整数字字段，拒绝非有限值和尾随垃圾。
template<class T> std::optional<T> Number(std::string_view value) {
    T parsed{};const auto result=std::from_chars(value.data(),value.data()+value.size(),parsed);
    if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size() || !std::isfinite(static_cast<double>(parsed))) return {};
    return parsed;
}
// 参数 value 为白亮度；只接受 HDR10 标准绝对亮度范围内的有效值。
bool ValidWhite(const std::optional<float>& value) {return value && std::isfinite(*value) && *value>=80 && *value<=10000;}
// 参数 window 为当前游戏窗口；根据显示路径查询 Windows 标准 SDR 白亮度，不猜测显示器。
std::optional<float> OsWhite(HWND window) {
    if(!IsWindow(window)) return {};
    MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);
    if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)) return {};
    // 显示拓扑可能在查询间变化；仅进行有限重试，不等待显示器稳定。
    for(int attempt=0;attempt<2;++attempt) {
        UINT32 pathCount=0,modeCount=0;
        if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount)!=ERROR_SUCCESS) return {};
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        const auto result=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);
        if(result==ERROR_INSUFFICIENT_BUFFER) continue;
        if(result!=ERROR_SUCCESS) return {};
        for(UINT32 i=0;i<pathCount;++i) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
            name.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(name),paths[i].sourceInfo.adapterId,paths[i].sourceInfo.id};
            if(DisplayConfigGetDeviceInfo(&name.header)!=ERROR_SUCCESS || _wcsicmp(name.viewGdiDeviceName,monitor.szDevice)!=0) continue;
            DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
            white.header={DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,sizeof(white),paths[i].targetInfo.adapterId,paths[i].targetInfo.id};
            if(DisplayConfigGetDeviceInfo(&white.header)==ERROR_SUCCESS) {
                // Windows API 的 1000 单位表示标准 80 尼特。
                const float nits=white.SDRWhiteLevel*kSdrReferenceWhiteNits/1000.0f;
                if(ValidWhite(nits)) return nits;
            }
        }
        return {};
    }
    return {};
}
}
// 参数 text 为只读 INI 文本；相同目标字段重复时拒绝该字段，避免使用歧义配置。
UiColorProfile ParseUiColorProfile(std::string_view text) {
    UiColorProfile result;bool inLuma=false;unsigned seenMode=0,seenUi=0,seenScene=0,seenOs=0;
    while(!text.empty()) {
        const auto end=text.find('\n');auto line=Trim(text.substr(0,end));
        if(end==text.npos) text={};else text.remove_prefix(end+1);
        if(line.starts_with("\xef\xbb\xbf")) line.remove_prefix(3);
        if(line.empty() || line.front()==';' || line.front()=='#') continue;
        if(line.front()=='[') {inLuma=line=="[Luma]";continue;}
        if(!inLuma) continue;
        const auto eq=line.find('=');if(eq==line.npos) continue;
        const auto key=Trim(line.substr(0,eq));auto value=line.substr(eq+1);
        value=Trim(value.substr(0,value.find_first_of(";#")));
        if(key=="DisplayMode") {++seenMode;result.displayMode=Number<int>(value);}
        if(key=="UIPaperWhite") {++seenUi;result.uiWhite=Number<float>(value);}
        if(key=="ScenePaperWhite") {++seenScene;result.sceneWhite=Number<float>(value);}
        if(key=="UseOSReferenceWhiteLevel") {++seenOs;const auto enabled=Number<int>(value);result.useOsWhite=!enabled || *enabled!=0;}
    }
    if(seenMode!=1 || !result.displayMode || *result.displayMode<0 || *result.displayMode>2) result.displayMode.reset();
    if(seenUi!=1) result.uiWhite.reset();if(seenScene!=1) result.sceneWhite.reset();
    if(seenOs>1) {result.displayMode.reset();result.useOsWhite=true;}
    return result;
}
// 参数 file/window 为明确来源；限制配置大小并在后台解析，失败时返回未知快照。
UiColorProfile ReadUiColorProfile(const std::filesystem::path& file,HWND window) {
    std::error_code error;const auto size=std::filesystem::file_size(file,error);
    if(error || size>1024*1024) return {};
    std::ifstream input(file,std::ios::binary);if(!input) return {};
    std::string text(static_cast<size_t>(size),'\0');input.read(text.data(),static_cast<std::streamsize>(text.size()));
    if(!input && !text.empty()) return {};
    auto result=ParseUiColorProfile(text);
    if(result.displayMode && *result.displayMode!=0 && result.useOsWhite) result.osWhite=OsWhite(window);
    return result;
}
// 参数 requested/profile 为可选手动覆盖和 Luma 设置；优先级与 Luma 官方显示模式规则对应。
UiWhiteDecision ResolveUiWhite(float requested,const UiColorProfile& profile) {
    if(std::isfinite(requested) && requested>=80 && requested<=10000) return {requested,"本 Mod 手动覆盖"};
    if(!profile.displayMode) return {};
    if(*profile.displayMode==0) return {kSdrReferenceWhiteNits,"Luma SDR 模式规定的标准参考白"};
    if(profile.useOsWhite) {
        if(ValidWhite(profile.osWhite)) return {*profile.osWhite,"Luma 跟随 Windows SDR 白亮度"};
        return {kSdrReferenceWhiteNits,"无法查询 Luma 使用的系统白亮度，使用标准 SDR 参考白"};
    }
    const auto value=*profile.displayMode==2?profile.sceneWhite:profile.uiWhite;
    if(ValidWhite(value)) return {*value,*profile.displayMode==2?"Luma SDR 映射到 HDR 的场景白配置":"Luma HDR 界面白配置"};
    return {kSdrReferenceWhiteNits,"Luma 白亮度配置缺失或无效，使用标准 SDR 参考白"};
}
}
