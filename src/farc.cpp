#include "farc.h"
#include "miniz_tinfl.h"
#include <fstream>
#include <vector>
#include <cstdint>
namespace diva {
namespace {
// 参数 data/offset 为已校验缓冲区及偏移，读取大端整数。
uint32_t Big(const std::string& data,size_t offset) {return (uint32_t(uint8_t(data[offset]))<<24)|(uint32_t(uint8_t(data[offset+1]))<<16)|(uint32_t(uint8_t(data[offset+2]))<<8)|uint8_t(data[offset+3]);}
// 参数 data 为解压文本，计算 gzip 的 CRC32 校验。
uint32_t Crc(const std::string& data) {uint32_t crc=~0u;for(unsigned char c:data) {crc^=c;for(int bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));}return ~crc;}
}
std::optional<std::string> ReadFarcEntry(const fs::path& path,const std::string& name,std::string& reason) {
    reason.clear();std::ifstream file(path,std::ios::binary|std::ios::ate);auto size=file.tellg();
    if(size<12 || size>64*1024*1024) {reason="归档不存在或大小超限";return {};}
    std::string data(static_cast<size_t>(size),'\0');file.seekg(0);if(!file.read(data.data(),size)) {reason="归档读取失败";return {};}
    return ReadFarcEntryMemory(data,name,reason);
}
std::optional<std::string> ReadFarcEntryMemory(const std::string& data,const std::string& name,std::string& reason) {
    reason.clear();
    if(data.size()<12 || data.size()>64*1024*1024) {reason="内存归档大小非法";return {};}
    bool compressed=data.compare(0,4,"FArC")==0;
    if(!compressed && data.compare(0,4,"FArc")!=0) {reason="不支持的归档格式（仅支持普通FArc/FArC）";return {};}
    size_t end=size_t(Big(data,4))+8;if(end<12 || end>data.size() || end>1024*1024) {reason="归档目录越界";return {};}
    for(size_t pos=12,entries=0;pos<end && entries<4096;++entries) {
        auto zero=data.find('\0',pos);size_t fields=compressed?12:8;
        if(zero==std::string::npos || zero>=end || zero-pos>1024 || fields>end-zero-1) {reason="归档条目目录损坏";return {};}
        auto entry=data.substr(pos,zero-pos);pos=zero+1;size_t offset=Big(data,pos),packed=Big(data,pos+4),unpacked=compressed?Big(data,pos+8):packed;pos+=fields;
        if(entry!=name) continue;
        if(offset<end || offset>data.size() || packed>data.size()-offset || unpacked>16*1024*1024) {reason="归档条目范围或解压大小非法";return {};}
        if(!compressed || packed==unpacked) return data.substr(offset,packed);
        auto input=data.substr(offset,packed);size_t start=10;
        if(input.size()<18 || uint8_t(input[0])!=31 || uint8_t(input[1])!=139 || uint8_t(input[2])!=8 || (uint8_t(input[3])&0xe0)) {reason="gzip头无效";return {};}
        auto flags=uint8_t(input[3]);if(flags&4) {if(start+2>input.size()-8) {reason="gzip扩展头截断";return {};}size_t extra=uint8_t(input[start])+(size_t(uint8_t(input[start+1]))<<8);start+=2+extra;}
        for(int flag:{8,16}) if(flags&flag) {auto z=input.find('\0',start);if(z==std::string::npos) {reason="gzip文本头截断";return {};}start=z+1;}
        if(flags&2) {
            if(start>input.size()-8 || input.size()-8-start<2) {reason="gzip头校验字段截断";return {};}
            const auto headerCrc=uint16_t(uint8_t(input[start]))|(uint16_t(uint8_t(input[start+1]))<<8);
            if(uint16_t(Crc(input.substr(0,start)))!=headerCrc) {reason="gzip头CRC校验失败";return {};}
            start+=2;
        }
        if(start>input.size()-8) {reason="gzip数据截断";return {};}
        std::string output(unpacked,'\0');size_t consumed=input.size()-8-start,produced=output.size();tinfl_decompressor state;tinfl_init(&state);
        auto status=tinfl_decompress(&state,reinterpret_cast<const mz_uint8*>(input.data()+start),&consumed,reinterpret_cast<mz_uint8*>(output.data()),reinterpret_cast<mz_uint8*>(output.data()),&produced,TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        uint32_t crc=0,length=0;for(int i=0;i<4;++i) {crc|=uint32_t(uint8_t(input[input.size()-8+i]))<<(8*i);length|=uint32_t(uint8_t(input[input.size()-4+i]))<<(8*i);}
        if(status!=TINFL_STATUS_DONE || produced!=unpacked || consumed!=input.size()-8-start || length!=unpacked || Crc(output)!=crc) {reason="gzip解压长度或校验失败";return {};}
        return output;
    }
    reason="归档未包含指定条目";return {};
}
}
