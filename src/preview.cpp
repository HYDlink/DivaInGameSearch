#include "host.h"
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>
#include <Psapi.h>
#include <imgui.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;
// 为独立预览生成大列表和超长名称，不进入 DLL 或发行曲库。
static diva::Catalog StressCatalog() {
    diva::Catalog catalog;
    for(int i=0;i<10000;++i) {
        diva::Song song;song.id=10000+i;
        song.name=i%3==0?"超长原名与中日文字混排测试 · 初音ミクの楽曲検索 / A very long song title that must stay within the title column / 固定难度列不可被挤走":i%3==1?"没有别名的歌曲":"五种难度与多个来源";
        song.aliases=i%3==1?"":"中文通俗名和别名仅在固定宽度内展示，悬停可查看完整内容";
        song.source="压力测试（非游戏曲库）";song.sources={song.source,"附加谱面来源"};
        for(int d=0;d<5;++d) if(i%3!=1 || d==3) song.charts[0][d]={float(d+4),"仅预览，不执行脚本",song.source,true};
        diva::PrepareSong(song);catalog.songs.push_back(std::move(song));
    }
    catalog.packs={"压力测试（非游戏曲库）","附加谱面来源"};return catalog;
}
// 离线预览明确复用报告637的六角色原始值，仅用于共用UI验证，不冒充当前游戏快照。
static std::shared_ptr<const diva::RecommendationSnapshot> DemoRecommendations() {
    auto snapshot=std::make_shared<diva::RecommendationSnapshot>();snapshot->ready=true;snapshot->generation=1;
    const int characters[]={0,1,2,3,7,6},costumes[]={114,30,25,28,22,23};
    for(int i=0;i<6;++i) {diva::RecommendedPerformer value;value.type=0;value.character=characters[i];value.costumes.fill(costumes[i]);snapshot->songs[637].push_back(value);}
    return snapshot;
}
// 仅在显式传入 --customization-demo 时生成测试卡片，不属于游戏或发行元数据。
static diva::CustomizationCatalog DemoCustomizationCatalog() {
    diva::CustomizationCatalog catalog;
    catalog.sources={{"demo:a","演示来源 A","DemoA",".",false},{"demo:b","演示来源 B","DemoB",".",false}};
    for(auto kind:{diva::CustomizationKind::Module,diva::CustomizationKind::Accessory}) for(int n=0;n<42;++n) {
        diva::CustomizationItem item; item.key={kind,900000+n};
        item.name=(kind==diva::CustomizationKind::Module?"演示服装 ":"演示饰品 ")+std::to_string(n+1);
        if(n%7==0) item.name+=" · 超长名称、角色与来源裁剪测试";
        item.character=n%6==5?diva::CustomizationCharacter::Rin:diva::CustomizationCharacter::Miku;
        if(kind==diva::CustomizationKind::Accessory) {
            item.part=static_cast<diva::CustomizationPart>(n%4);
            if(n%4==0) item.character=diva::CustomizationCharacter::All;
        }
        item.definitionSource=item.nameSource=n%2?"demo:a":"demo:b";
        item.sources.push_back({item.definitionSource,"演示定义",true}); item.sortIndex=n;
        diva::PrepareCustomizationItem(item,catalog); catalog.items.push_back(std::move(item));
    }
    catalog.diagnostics={"显式演示数据：不来自游戏，不定位、不装备，卡片仅使用占位预览。"};
    return catalog;
}
// 保存真实原生 DX11 后缓冲为 PNG；参数 chain/device/context/path 为图形对象和输出文件。
static bool Screenshot(IDXGISwapChain* chain,ID3D11Device* device,ID3D11DeviceContext* context,const diva::fs::path& path) {
    ComPtr<ID3D11Texture2D> back,staging;
    if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&back)))) return false;
    D3D11_TEXTURE2D_DESC desc{}; back->GetDesc(&desc); desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) return false;
    context->CopyResource(staging.Get(),back.Get()); D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) return false;
    ComPtr<IWICImagingFactory> factory; ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICStream> stream; ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(SUCCEEDED(hr)) hr=factory->CreateStream(&stream);
    if(SUCCEEDED(hr)) hr=stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE);
    if(SUCCEEDED(hr)) hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
    if(SUCCEEDED(hr)) hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    if(SUCCEEDED(hr)) hr=encoder->CreateNewFrame(&frame,nullptr);
    if(SUCCEEDED(hr)) hr=frame->Initialize(nullptr);
    if(SUCCEEDED(hr)) hr=frame->SetSize(desc.Width,desc.Height);
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppRGBA;
    if(SUCCEEDED(hr)) hr=frame->SetPixelFormat(&format);
    // PNG 编码器可协商为 BGRA，按实际格式转换，避免截图红蓝互换。
    std::vector<BYTE> pixels;
    BYTE* data=(BYTE*)mapped.pData;
    if(format==GUID_WICPixelFormat32bppBGRA) {
        pixels.resize((size_t)mapped.RowPitch*desc.Height); memcpy(pixels.data(),data,pixels.size());
        for(UINT y=0;y<desc.Height;++y) for(UINT x=0;x<desc.Width;++x) std::swap(pixels[(size_t)y*mapped.RowPitch+x*4],pixels[(size_t)y*mapped.RowPitch+x*4+2]);
        data=pixels.data();
    }
    if(SUCCEEDED(hr)) hr=frame->WritePixels(desc.Height,mapped.RowPitch,mapped.RowPitch*desc.Height,data);
    if(SUCCEEDED(hr)) hr=frame->Commit(); if(SUCCEEDED(hr)) hr=encoder->Commit();
    context->Unmap(staging.Get(),0); return SUCCEEDED(hr);
}
// 独立预览窗口消息；参数 hwnd/message/w/l 为系统消息。
static LRESULT CALLBACK PreviewWindow(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return diva::App().Message(hwnd,message,w,l);
}
// 原生预览入口；参数 instance/previous/command/show 为 Windows 启动参数。
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR command,int show) {
    (void)previous; (void)command; (void)show; CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    auto home=diva::fs::path(executable).parent_path(),game=home; diva::fs::path output=diva::fs::current_path()/"artifacts/native-preview.png";
    bool smoke=false,stress=false,stressApplied=false,customizationDemo=false,recommendationDemo=false,recommendationApplied=false;
    // 指定真实来源进行离线界面检查；条件只影响独立Preview，不操作游戏。
    std::string moduleSource;bool moduleSourceApplied=false;
    // 可选初始检索文本，用共用原生界面离线检查实际歌曲或服装结果。
    std::string initialQuery;bool initialQueryApplied=false;
    auto searchMode=diva::SearchMode::Songs;
    for(int i=1;i<argc;++i) {
        std::wstring arg=argv[i];
        if(arg==L"--smoke") smoke=true;
        else if(arg==L"--stress") stress=true;
        else if(arg==L"--customization-demo") {customizationDemo=true;searchMode=diva::SearchMode::Modules;}
        else if(arg==L"--customization") searchMode=diva::SearchMode::Modules;
        else if(arg==L"--module-source" && i+1<argc) {moduleSource=diva::Utf8(argv[++i]);searchMode=diva::SearchMode::Modules;}
        else if(arg==L"--query" && i+1<argc) initialQuery=diva::Utf8(argv[++i]);
        else if(arg==L"--recommendation-demo") {recommendationDemo=true;searchMode=diva::SearchMode::Modules;}
        else if(arg==L"--accessories") searchMode=diva::SearchMode::Accessories;
        else if(arg==L"--home" && i+1<argc) home=argv[++i];
        else if(arg==L"--game" && i+1<argc) game=argv[++i];
        else if(arg==L"--output" && i+1<argc) output=argv[++i];
    } LocalFree(argv);
    auto& host=diva::App(); host.preview=true; host.Init(home,game); host.view.open=true; host.view.OnOpened(); host.view.SetMode(searchMode);
    // 推荐样例在目录就绪后才填入文本，先避免空输入框占用ImGui编辑缓冲。
    if(recommendationDemo || !initialQuery.empty()) {host.view.focus=false;host.view.customization.State().focus=false;}
    if(customizationDemo) {
        auto& view=host.view.customization;
        view.catalog=std::make_shared<diva::CustomizationCatalog>(DemoCustomizationCatalog()); ++view.generation;
        view.status="演示数据：仅用于检查格子与键盘，不是游戏物品；原生定位和模型预览尚未接入。";
    }
    // 预览动作只记录请求，参数 request/available 为共享界面实际提交的动作。
    host.view.submit=[&](diva::Request request,bool available) {
        host.view.status="预览定位：PVID "+std::to_string(request.pvid)+" / 难度 "+std::to_string(request.difficulty)+" / 风格 "+std::to_string(request.style)+(available?" / 可用":" / 缺失");
    };
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc=PreviewWindow; wc.hInstance=instance; wc.lpszClassName=L"DivaSearchPreview"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW); RegisterClassExW(&wc);
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"DIVA 游戏内搜索 · 原生预览",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1440,960,nullptr,nullptr,instance,nullptr);
    DXGI_SWAP_CHAIN_DESC desc{}; desc.BufferCount=2; desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow=window; desc.SampleDesc.Count=1; desc.Windowed=TRUE; desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; ComPtr<IDXGISwapChain> chain;
    // 自动截图使用确定性的 WARP；交互预览仍使用硬件 DX11，失败时才回退。
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,smoke?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context);
    if(FAILED(hr)) hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context);
    if(FAILED(hr) || !host.Attach(chain.Get(),device.Get(),context.Get())) return 2;
    ShowWindow(window,SW_SHOWNOACTIVATE);
    MSG msg{}; int readyFrames=0; bool saved=false; auto start=GetTickCount64(); UINT oldWidth=0,oldHeight=0;
    double totalFrameMs=0;int measuredFrames=0;
    while(msg.message!=WM_QUIT) {
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if(msg.message==WM_QUIT) break;
        RECT rect{}; GetClientRect(window,&rect); UINT width=rect.right,height=rect.bottom;
        if(width && height && (width!=oldWidth || height!=oldHeight)) {
            context->OMSetRenderTargets(0,nullptr,nullptr); chain->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0); oldWidth=width; oldHeight=height;
        }
        ComPtr<ID3D11Texture2D> back; ComPtr<ID3D11RenderTargetView> target;
        if(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&back))) && SUCCEEDED(device->CreateRenderTargetView(back.Get(),nullptr,&target))) {
            float color[]={0.016f,0.024f,0.038f,1}; context->ClearRenderTargetView(target.Get(),color);
        }
        target.Reset(); back.Reset();
        if(stress && !stressApplied && host.view.catalog) { host.view.catalog=std::make_shared<diva::Catalog>(StressCatalog());host.view.dirty=true;host.view.status="一万首合成曲目压力预览，不是游戏数据。";stressApplied=true; }
        if(!initialQuery.empty() && !initialQueryApplied && host.view.catalog && (searchMode==diva::SearchMode::Songs || host.view.customization.catalog)) {
            if(searchMode==diva::SearchMode::Songs) {strcpy_s(host.view.input,initialQuery.c_str());host.view.focus=true;host.view.QueueSearch(false);}
            else {auto& view=host.view.customization;strcpy_s(view.State().input,initialQuery.c_str());view.State().focus=true;view.QueueSearch(false);}
            initialQueryApplied=true;
        }
        if(!moduleSource.empty() && !moduleSourceApplied && host.view.customization.catalog) {
            auto& view=host.view.customization;
            // 参数source为真实目录来源，按完整名称选择，避免用显示文本猜资源路径。
            const auto source=std::find_if(view.catalog->sources.begin(),view.catalog->sources.end(),[&](const diva::CustomizationSource& source) {return source.name==moduleSource;});
            if(source!=view.catalog->sources.end()) {view.State().query.source=source->id;view.State().query.character.reset();view.QueueSearch(false);}
            moduleSourceApplied=true;
        }
        if(recommendationDemo && !recommendationApplied && host.view.catalog && host.view.customization.catalog) {
            auto& view=host.view.customization;const auto snapshot=DemoRecommendations();
            // 捕获离线值快照；独立预览不访问游戏进程或调用原生动作。
            view.recommendationProvider=[snapshot] {return snapshot;};
            // 参数item为共用曲库歌曲，离线样例使用实际曲名验证直接输入歌曲名称的布局。
            const auto song=std::find_if(host.view.catalog->songs.begin(),host.view.catalog->songs.end(),[](const diva::Song& item){return item.id==637;});
            const auto queryText=song==host.view.catalog->songs.end()?std::string("637"):song->name;
            strcpy_s(view.State().input,queryText.c_str());view.State().focus=true;view.QueueSearch(false);
            view.status="离线推荐样例：共用目录与界面，不是实机功能验收。";recommendationApplied=true;
        }
        auto frameStart=std::chrono::steady_clock::now();host.Frame(chain.Get());
        if(readyFrames>5) { totalFrameMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStart).count();++measuredFrames; }
        // 服装截图等待真实目录和至少一张解码上传的图片，不能以歌曲目录就绪冒充图片通过。
        bool previewReady=host.view.catalog!=nullptr;
        if(searchMode!=diva::SearchMode::Songs && !customizationDemo)
            previewReady=previewReady && host.view.customization.catalog && !host.view.customization.SearchPending() && !host.modulePreviewTextures.empty() && GetTickCount64()-start>5000;
        if(previewReady) ++readyFrames;
        // 自动截图包含选中行，用于检查难度按钮与行底色的辨识度；只影响独立预览。
        if(smoke && readyFrames==1 && !host.view.results.empty()) host.view.selectedId=host.view.catalog->songs[host.view.results.front().index].id;
        if(smoke && readyFrames==30) {
            diva::fs::create_directories(output.parent_path()); saved=Screenshot(chain.Get(),device.Get(),context.Get(),output);
            std::ofstream log(home/(stress?"artifacts/stress-preview.log":"artifacts/preview-verification.log"));
            PROCESS_MEMORY_COUNTERS memory{};GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));
            log<<"原生 DX11 共用界面已运行。\n曲库歌曲："<<host.view.catalog->songs.size()<<"\n可见查询结果："<<host.view.results.size()<<"\n截图保存："<<(saved?"成功":"失败")<<"\n";
            if(searchMode!=diva::SearchMode::Songs) log<<"当前领域："<<(searchMode==diva::SearchMode::Modules?"服装":"饰品")<<"\n定制可见结果："<<host.view.customization.State().results.size()<<"\n显式演示数据："<<customizationDemo<<"\n";
            if(recommendationDemo) log<<"歌曲推荐关系：离线637六角色样例，不是当前游戏快照。\n";
            log<<"截图图形设备：Windows WARP 软件 DX11。\n";
            log<<"可见界面 CPU 平均帧耗时（不含 Present 等待）："<<(measuredFrames?totalFrameMs/measuredFrames:0)<<" 毫秒\n工作集："<<memory.WorkingSetSize/1048576.0<<" MiB\n以上为独立预览测量，不代表真实游戏帧耗时。\n";
            auto previousContext=ImGui::GetCurrentContext();ImGui::SetCurrentContext((ImGuiContext*)host.context);
            auto draw=ImGui::GetDrawData();log<<"绘制顶点："<<draw->TotalVtxCount<<"，索引："<<draw->TotalIdxCount<<"\n";
            ImGui::SetCurrentContext(previousContext);
            PostMessageW(window,WM_CLOSE,0,0);
        }
        chain->Present(1,0);
        if(smoke && GetTickCount64()-start>60000) { PostMessageW(window,WM_CLOSE,0,0); break; }
    }
    host.Shutdown(); DestroyWindow(window); CoUninitialize(); return smoke && !saved?3:0;
}
