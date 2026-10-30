#define BCDEC_IMPLEMENTATION
#include "bcdec.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
// 参数 input/size/width/height/output/capacity 为BC7压缩块与受限RGBA目标；调用方拥有缓冲区。
extern "C" __declspec(dllexport) int __cdecl DivaDecodeBc7(const uint8_t* input,size_t size,int width,int height,uint8_t* output,size_t capacity) {
    if(!input || !output || width<=0 || height<=0 || width>8192 || height>8192) return -1;
    const size_t columns=(size_t(width)+3)/4,rows=(size_t(height)+3)/4;
    const size_t required=size_t(width)*size_t(height)*4;
    if(required>64u*1024u*1024u || capacity<required || size!=columns*rows*16) return -2;
    for(size_t y=0;y<rows;++y) for(size_t x=0;x<columns;++x) {
        uint8_t block[64]{};bcdec_bc7(input+(y*columns+x)*16,block,16);
        for(size_t row=0;row<4 && y*4+row<size_t(height);++row) {
            const size_t pixels=(size_t(width)-x*4)<4?(size_t(width)-x*4):4;
            std::memcpy(output+((y*4+row)*size_t(width)+x*4)*4,block+row*16,pixels*4);
        }
    }
    return 0;
}
