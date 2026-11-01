// 单帧只读D3D后缓冲采集；只Hook本Mod的公开OnFrame，结束立即撤销，不写游戏字段。
let captureListener = null;
// 参数object/index/result/args为COM对象、已知接口槽及精确函数签名。
function comMethod(object, index, result, args) {
    return new NativeFunction(object.readPointer().add(index * Process.pointerSize).readPointer(), result, ['pointer', ...args]);
}
// 参数chain/output为DML实际交换链与本项目内BMP文件；仅创建工具自己的读回纹理。
function readBackBuffer(chain, output) {
    const textureGuid = Memory.alloc(16);
    textureGuid.writeByteArray([0xF2,0xAA,0x15,0x6F,0x08,0xD2,0x89,0x4E,0x9A,0xB4,0x48,0x95,0x35,0xD3,0x4F,0x9C]);
    let texture = ptr(0), device = ptr(0), context = ptr(0), staging = ptr(0), mapped = false;
    const slot = Memory.alloc(Process.pointerSize);
    // 参数hr为COM结果，负数即停止本次采集。
    const check = hr => {if (hr < 0) throw new Error('D3D调用失败：' + (hr >>> 0).toString(16));};
    try {
        check(comMethod(chain,9,'int',['uint','pointer','pointer'])(chain,0,textureGuid,slot));texture=slot.readPointer();
        comMethod(texture,3,'void',['pointer'])(texture,slot);device=slot.readPointer();
        comMethod(device,40,'void',['pointer'])(device,slot);context=slot.readPointer();
        const desc=Memory.alloc(44);comMethod(texture,10,'void',['pointer'])(texture,desc);
        const width=desc.readU32(), height=desc.add(4).readU32(), format=desc.add(16).readU32();
        if(width<1 || height<1 || width>7680 || height>4320 || ![28,29,87,91].includes(format)
            || desc.add(20).readU32()!==1) throw new Error('后缓冲格式、采样数或尺寸不在截图支持范围');
        desc.add(8).writeU32(1);desc.add(12).writeU32(1);desc.add(28).writeU32(3);
        desc.add(32).writeU32(0);desc.add(36).writeU32(0x20000);desc.add(40).writeU32(0);
        check(comMethod(device,5,'int',['pointer','pointer','pointer'])(device,desc,ptr(0),slot));staging=slot.readPointer();
        comMethod(context,47,'void',['pointer','pointer'])(context,staging,texture);
        const map=Memory.alloc(24);check(comMethod(context,14,'int',['pointer','uint','uint','uint','pointer'])(context,staging,0,1,0,map));mapped=true;
        const source=map.readPointer(), pitch=map.add(8).readU32();
        if(pitch<width*4) throw new Error('D3D读回行跨度无效');
        const pixels=new Uint8Array(width*height*4);
        for(let row=0;row<height;++row) pixels.set(new Uint8Array(source.add(row*pitch).readByteArray(width*4)),row*width*4);
        for(let i=0;i<pixels.length;i+=4) {
            if(format===28 || format===29) {const red=pixels[i];pixels[i]=pixels[i+2];pixels[i+2]=red;}
            pixels[i+3]=255;
        }
        const header=new Uint8Array(54), view=new DataView(header.buffer);
        header[0]=66;header[1]=77;view.setUint32(2,54+pixels.length,true);view.setUint32(10,54,true);
        view.setUint32(14,40,true);view.setInt32(18,width,true);view.setInt32(22,-height,true);
        view.setUint16(26,1,true);view.setUint16(28,32,true);view.setUint32(34,pixels.length,true);
        const file=new File(output,'wb');try {file.write(header.buffer);file.write(pixels.buffer);file.flush();} finally {file.close();}
        return {说明:'真实交换链D3D11后缓冲单帧；未修改游戏对象、输入或装备',width,height,format,output};
    } finally {
        if(mapped) comMethod(context,15,'void',['pointer','uint'])(context,staging,0);
        for(const object of [staging,context,device,texture]) if(!object.isNull()) comMethod(object,2,'uint',[])(object);
    }
}
// 参数options.output只能指向本项目artifacts内的新BMP；五秒无实际帧即失败卸载。
function captureFrame(options) {
    const output=String(options.output||'').replace(/\\/g,'/');
    const root=String(options.root||'').replace(/\\/g,'/').replace(/\/$/,'')+'/';
    if(!root.endsWith('/artifacts/') || !output.startsWith(root) || output.includes('/../') || !output.endsWith('.bmp') || captureListener)
        throw new Error('截图路径或会话状态无效');
    return new Promise((resolve,reject)=>{
        let finished=false;
        // 参数error/value为本次单帧结果，确保只撤销本探针并只返回一次。
        const finish=(error,value)=>{
            if(finished) return;finished=true;clearTimeout(timer);
            if(captureListener) captureListener.detach();captureListener=null;
            if(error) reject(error);else resolve(value);
        };
        const timer=setTimeout(()=>finish(new Error('等待真实D3D游戏帧超时')),5000);
        captureListener=Interceptor.attach(Process.getModuleByName('DivaInGameSearch.dll').getExportByName('OnFrame'),{
            // 参数args[0]为加载器本帧实际交换链，不扫描堆或构造替代指针。
            onEnter(args) {this.chain=args[0];},
            // 在本Mod原始绘制完成后复制一帧；不替换或跳过原回调。
            onLeave() {
                if(finished) return;
                try {finish(null,readBackBuffer(this.chain,output));} catch(error) {finish(error);}
            }
        });
    });
}
rpc.exports={...rpc.exports,captureframe:captureFrame};
