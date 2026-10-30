#include "module_assets.h"
#include <Windows.h>
#include <bcrypt.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <thread>
#include <set>

using Microsoft::WRL::ComPtr;

// 将已经裁好的 RGBA 像素写成 PNG，保留原始分辨率以便人工核对。
bool WritePng(const diva::fs::path& path,int width,int height,const std::vector<unsigned char>& pixels) {
    if(width<=0 || height<=0 || pixels.size()!=static_cast<size_t>(width)*height*4) return false;
    ComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))) return false;
    ComPtr<IWICStream> stream;
    if(FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> encoder;
    if(FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder)) || FAILED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))) return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    if(FAILED(encoder->CreateNewFrame(&frame,&properties)) || FAILED(frame->Initialize(properties.Get()))) return false;
    if(FAILED(frame->SetSize(static_cast<UINT>(width),static_cast<UINT>(height)))) return false;
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
    if(FAILED(frame->SetPixelFormat(&format)) || format!=GUID_WICPixelFormat32bppBGRA) return false;
    auto encoded=pixels;
    for(size_t index=0;index<encoded.size();index+=4) std::swap(encoded[index],encoded[index+2]);
    const UINT stride=static_cast<UINT>(width*4);
    const UINT bytes=static_cast<UINT>(pixels.size());
    if(FAILED(frame->WritePixels(static_cast<UINT>(height),stride,bytes,encoded.data()))) return false;
    return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

// 以 Windows CNG 计算像素原始字节的 SHA-256。
std::string PixelSha256(const std::vector<unsigned char>& pixels) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return {};
    DWORD objectLength=0,resultLength=0;
    if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectLength),sizeof(objectLength),&resultLength,0)<0) {
        BCryptCloseAlgorithmProvider(algorithm,0);return {};
    }
    std::vector<unsigned char> object(objectLength);
    std::array<unsigned char,32> digest{};
    if(BCryptCreateHash(algorithm,&hash,object.data(),objectLength,nullptr,0,0)<0 ||
       BCryptHashData(hash,const_cast<PUCHAR>(pixels.data()),static_cast<ULONG>(pixels.size()),0)<0 ||
       BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0) {
        if(hash) BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);return {};
    }
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    static constexpr char hex[]="0123456789abcdef";
    std::string output;output.reserve(digest.size()*2);
    for(unsigned char value:digest) {output.push_back(hex[value>>4]);output.push_back(hex[value&15]);}
    return output;
}

// 在服装目录快照中按稳定 ID 取得当前有效记录。
const diva::CustomizationItem* FindItem(const diva::CustomizationCatalog& catalog,int id) {
    for(const auto& item:catalog.items) if(item.key.kind==diva::CustomizationKind::Module && item.key.id==id) return &item;
    return nullptr;
}

// 等待后台读取完成，输出来源、尺寸、像素摘要与可供人工查看的 PNG 路径。
bool AuditPreview(diva::ModuleAssets& assets,const diva::CustomizationItem& item,int style,const diva::fs::path& outputDirectory) {
    diva::ModuleAssetPreview preview;
    for(int attempt=0;attempt<600;++attempt) {
        preview=assets.Peek(item,true,style);
        if(preview.rgbaOwner && preview.rgbaPixels && preview.width>0 && preview.height>0) break;
        if(preview.source.starts_with("图片不可用：")) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const std::string styleName=style==static_cast<int>(diva::ModuleSpriteStyle::Mega39)?"MEGA39":"FT";
    std::cout<<"样本 ID "<<item.key.id<<" / "<<styleName<<" / "<<preview.width<<"x"<<preview.height<<" / "<<preview.source;
    if(!preview.rgbaOwner || !preview.rgbaPixels) {std::cout<<" / 未解码\n";return false;}
    const auto& pixels=*preview.rgbaOwner;
    const auto digest=PixelSha256(pixels);
    const auto filename=std::to_wstring(item.key.id)+L"_"+std::wstring(styleName.begin(),styleName.end())+L".png";
    const auto path=outputDirectory/filename;
    if(!WritePng(path,preview.width,preview.height,pixels)) {std::cout<<" / PNG 写入失败\n";return false;}
    std::cout<<" / 像素 SHA-256 "<<digest<<" / PNG "<<diva::Utf8(path.wstring())<<"\n";
    return true;
}

// 从明确的 PC 地区官方表构建样本；不混用 PS4 或 Switch 候选。
std::map<int,diva::CustomizationItem> OfficialSamples(const std::vector<diva::OfficialModuleTable>& tables,
    const std::vector<int>& wanted,std::vector<std::string>& diagnostics) {
    std::map<int,diva::CustomizationItem> output;
    for(const auto& table:tables) {
        const bool base=table.cpkPath.filename()=="diva_main_region.cpk" && table.internalPath=="rom_steam_region/rom/gm_module_tbl.farc!/gm_module_id.bin";
        const bool dlc=table.cpkPath.filename()=="diva_dlc00_region.cpk" && table.internalPath=="rom_steam_region_dlc/rom/gm_module_tbl.farc!/gm_module_id.bin";
        if(!base && !dlc) continue;
        auto items=diva::ReadCustomizationText(table.tableText,diva::CustomizationKind::Module,base?"audit:official-pc":"audit:official-pc-dlc",diagnostics);
        for(auto& item:items) if(std::find(wanted.begin(),wanted.end(),item.key.id)!=wanted.end()) output.insert_or_assign(item.key.id,std::move(item));
    }
    return output;
}

int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if(argc<5) {std::cout<<"参数：模块目录 游戏目录 插件目录 输出目录\n";return 2;}
    const diva::fs::path home(argv[1]),game(argv[2]),plugin(argv[3]),outputDirectory(argv[4]);
    std::error_code error;diva::fs::create_directories(outputDirectory,error);
    if(error) {std::cout<<"无法创建审计输出目录。\n";return 3;}
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);

    std::vector<std::string> diagnostics;
    auto catalog=diva::LoadCustomizationCatalog(home,game);
    auto officialTables=diva::ReadOfficialModuleTables(game,plugin,diagnostics);
    const std::vector<int> officialIds={0,316,394};
    const std::vector<int> modIds={1400,6400,997};
    auto officialItems=OfficialSamples(officialTables,officialIds,diagnostics);
    for(const auto& diagnostic:diagnostics) std::cout<<diagnostic<<"\n";
    std::cout<<"目录快照：服装/饰品总数 "<<catalog.items.size()<<"，有效来源 "<<catalog.sources.size()<<"\n";

    auto officialCatalog=std::make_shared<diva::CustomizationCatalog>();
    officialCatalog->language="cn";
    std::vector<std::string> assetDiagnostics;
    diva::ModuleAssets officialAssets(game,plugin,officialCatalog,assetDiagnostics);
    diva::ModuleAssets modAssets(game,plugin,std::make_shared<diva::CustomizationCatalog>(catalog),assetDiagnostics);
    int decodedSamples=0;
    for(const auto& id:officialIds) {
        auto item=officialItems.find(id);
        if(item==officialItems.end()) {std::cout<<"官方 PC 地区表缺少样本 ID "<<id<<"\n";continue;}
        bool decoded=false;
        for(int style:{static_cast<int>(diva::ModuleSpriteStyle::Mega39),static_cast<int>(diva::ModuleSpriteStyle::FutureTone)})
            decoded=AuditPreview(officialAssets,item->second,style,outputDirectory) || decoded;
        if(decoded) ++decodedSamples;
    }
    for(const auto& id:modIds) {
        const auto* item=FindItem(catalog,id);
        if(!item) {std::cout<<"当前目录快照缺少 Mod 样本 ID "<<id<<"\n";continue;}
        std::cout<<"Mod 样本元数据：ID "<<id<<" / 来源 "<<item->definitionSource<<" / COS "<<item->cos<<" / 名称 "<<item->name<<"\n";
        bool decoded=false;
        for(int style:{static_cast<int>(diva::ModuleSpriteStyle::Mega39),static_cast<int>(diva::ModuleSpriteStyle::FutureTone)})
            decoded=AuditPreview(modAssets,*item,style,outputDirectory) || decoded;
        if(decoded) ++decodedSamples;
    }
    for(const auto& diagnostic:assetDiagnostics) std::cout<<diagnostic<<"\n";
    // 可选参数--all-characters抽取每个原始角色的首件实体，独立检查图片链，不调用原生定位。
    bool characterImagesPassed=true;
    if(argc>=6 && std::wstring(argv[5])==L"--all-characters") {
        std::set<diva::CustomizationCharacter> checked;
        const auto characterDirectory=outputDirectory/"characters";diva::fs::create_directories(characterDirectory);
        for(const auto& item:catalog.items) {
            if(item.key.kind!=diva::CustomizationKind::Module || item.character==diva::CustomizationCharacter::Unknown || !checked.insert(item.character).second) continue;
            std::cout<<"角色图片抽查："<<item.characterToken<<" / ID "<<item.key.id<<"\n";
            bool decoded=false;
            for(int style:{static_cast<int>(diva::ModuleSpriteStyle::Mega39),static_cast<int>(diva::ModuleSpriteStyle::FutureTone)}) decoded=AuditPreview(modAssets,item,style,characterDirectory) || decoded;
            characterImagesPassed=characterImagesPassed && decoded;
        }
        std::cout<<"原始角色图片抽查数："<<checked.size()<<"；全部有可解码版本："<<(characterImagesPassed?"是":"否")<<"\n";
    }
    officialAssets.Shutdown(true);modAssets.Shutdown(true);
    CoUninitialize();
    std::cout<<"已解码样本："<<decodedSamples<<" / 6；图片必须另行打开检查。\n";
    return decodedSamples==6 && characterImagesPassed?0:1;
}
