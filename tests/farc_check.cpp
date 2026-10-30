#include "farc.h"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
// 参数 value/message 为检查结果和中文诊断。
void Check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
// 参数 data/value 为归档缓冲区和待追加的大端整数。
void Big(std::string& data,uint32_t value) {for(int shift:{24,16,8,0}) data.push_back(char(value>>shift));}
// 参数 gzip/unpacked 为压缩流和归档声明的解压字节数；构造单条普通FArC夹具。
std::string GzipArchive(const std::string& gzip,uint32_t unpacked=3) {
    std::string data="FArC";Big(data,33);Big(data,16);data+="gm_module_id.bin";data.push_back(0);
    Big(data,41);Big(data,static_cast<uint32_t>(gzip.size()));Big(data,unpacked);data+=gzip;return data;
}
// 参数 path/data 为隔离夹具路径和二进制内容。
void Write(const diva::fs::path& path,const std::string& data) {std::ofstream stream(path,std::ios::binary);stream.write(data.data(),data.size());}
}
// 独立验证条目精确选择、目录越界、大小限制和不支持格式。
int main() {
    auto path=diva::fs::temp_directory_path()/"diva-farc-check.farc";
    try {
        std::string data="FArc";Big(data,29);Big(data,16);data+="gm_module_id.bin";data.push_back(0);Big(data,37);Big(data,3);data+="abc";
        Write(path,data);std::string reason;auto text=diva::ReadFarcEntry(path,"gm_module_id.bin",reason);
        Check(text && *text=="abc","普通归档条目读取失败");
        Check(!diva::ReadFarcEntry(path,"missing.bin",reason),"不存在的条目误读");
        auto broken=data;broken[4]=127;Write(path,broken);Check(!diva::ReadFarcEntry(path,"gm_module_id.bin",reason),"目录越界未拒绝");
        broken=data;broken.resize(38);Write(path,broken);Check(!diva::ReadFarcEntry(path,"gm_module_id.bin",reason),"截断条目未拒绝");
        broken=data;broken.replace(0,4,"FARC");Write(path,broken);Check(!diva::ReadFarcEntry(path,"gm_module_id.bin",reason),"加密格式未拒绝");
        const unsigned char gzipBytes[]={0x1f,0x8b,8,0,0,0,0,0,0,10,0x4b,0x4c,0x4a,6,0,0xc2,0x41,0x24,0x35,3,0,0,0};
        std::string gzip(reinterpret_cast<const char*>(gzipBytes),sizeof(gzipBytes));auto zipped=GzipArchive(gzip);Write(path,zipped);
        text=diva::ReadFarcEntry(path,"gm_module_id.bin",reason);Check(text && *text=="abc","gzip归档读取失败");
        broken=gzip;broken[0]=0;Write(path,GzipArchive(broken));Check(!diva::ReadFarcEntry(path,"gm_module_id.bin",reason),"损坏gzip头未拒绝");
        broken=gzip;broken[broken.size()-8]^=1;Write(path,GzipArchive(broken));Check(!diva::ReadFarcEntry(path,"gm_module_id.bin",reason),"损坏gzip数据CRC未拒绝");
        broken=gzip;broken.back()^=1;Write(path,GzipArchive(broken));Check(!diva::ReadFarcEntry(path,"gm_module_id.bin",reason),"损坏gzip解压长度未拒绝");
        Write(path,GzipArchive(gzip,4));Check(!diva::ReadFarcEntry(path,"gm_module_id.bin",reason),"FArC声明的解压长度不匹配未拒绝");
        Write(path,GzipArchive(gzip,16*1024*1024+1));Check(!diva::ReadFarcEntry(path,"gm_module_id.bin",reason),"超过解压大小上限的条目未拒绝");
        broken=gzip;broken[3]=2;broken.insert(10,"\0\0",2);Write(path,GzipArchive(broken));
        Check(!diva::ReadFarcEntry(path,"gm_module_id.bin",reason),"无效gzip可选头CRC未拒绝");
        diva::fs::remove(path);std::cout<<"归档边界回归通过。\n";return 0;
    } catch(const std::exception& error) {diva::fs::remove(path);std::cerr<<error.what()<<"\n";return 1;}
}
