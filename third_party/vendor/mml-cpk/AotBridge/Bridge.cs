using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using MikuMikuLibrary.Archives;
using MikuMikuLibrary.Archives.CriMw;
using MikuMikuLibrary.Databases;
using MikuMikuLibrary.IO;
using MikuMikuLibrary.Sprites;
using MikuMikuLibrary.Textures;

/// <summary>将固定版本 MikuMikuLibrary CPK 读取能力导出为无托管运行时依赖的 C ABI。</summary>
public static unsafe class MmlCpkBridge
{
    private const int MaximumEntryBytes = 64 * 1024 * 1024;
    /// <summary>固定版本bcdec原生桥接；input/output分别为BC7块和已分配RGBA缓冲区。</summary>
    [DllImport("DivaTextureDecoder",EntryPoint="DivaDecodeBc7",CallingConvention=CallingConvention.Cdecl)]
    private static extern int DecodeBc7(byte* input,nuint length,int width,int height,byte* output,nuint capacity);

    private sealed class ArchiveContext
    {
        public required CpkArchive Archive { get; init; }
        public required string[] Names { get; init; }
        public object Gate { get; } = new();
    }

    /// <summary>打开只读 CPK 并返回不透明句柄；pathUtf8 是 UTF-8 零结尾路径，失败返回零。</summary>
    [UnmanagedCallersOnly(EntryPoint = "DivaCpkOpen", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static nint Open(byte* pathUtf8)
    {
        try
        {
            string path = Marshal.PtrToStringUTF8((nint)pathUtf8) ?? string.Empty;
            CpkArchive archive = BinaryFile.Load<CpkArchive>(path);
            var context = new ArchiveContext { Archive = archive, Names = archive.FileNames.ToArray() };
            return GCHandle.ToIntPtr(GCHandle.Alloc(context, GCHandleType.Normal));
        }
        catch
        {
            return 0;
        }
    }

    /// <summary>返回句柄中的条目数量；handle 由 Open 创建，失败返回负值。</summary>
    [UnmanagedCallersOnly(EntryPoint = "DivaCpkCount", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Count(nint handle)
    {
        try
        {
            ArchiveContext? context = GetContext(handle);
            return context?.Names.Length ?? -1;
        }
        catch
        {
            return -1;
        }
    }

    /// <summary>复制指定索引条目名到 UTF-8 缓冲区；capacity 为零时只返回所需字节数。</summary>
    [UnmanagedCallersOnly(EntryPoint = "DivaCpkGetName", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetName(nint handle, int index, byte* buffer, int capacity)
    {
        try
        {
            ArchiveContext? context = GetContext(handle);
            if (context is null)
                return -1;
            if ((uint)index >= (uint)context.Names.Length)
                return -2;

            byte[] name = Encoding.UTF8.GetBytes(context.Names[index]);
            if (buffer == null || capacity == 0)
                return name.Length;
            if (capacity <= name.Length)
                return -3;

            name.CopyTo(new Span<byte>(buffer, capacity));
            buffer[name.Length] = 0;
            return name.Length;
        }
        catch
        {
            return -1;
        }
    }

    /// <summary>读取单个条目到原生分配缓冲区；调用方必须用 Free 释放，单条目最多 64 MiB。</summary>
    [UnmanagedCallersOnly(EntryPoint = "DivaCpkReadEntry", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int ReadEntry(nint handle, byte* nameUtf8, byte** output, nuint* outputLength)
    {
        if (output != null)
            *output = null;
        if (outputLength != null)
            *outputLength = 0;

        try
        {
            ArchiveContext? context = GetContext(handle);
            string name = Marshal.PtrToStringUTF8((nint)nameUtf8) ?? string.Empty;
            if (context is null || string.IsNullOrEmpty(name) || output == null || outputLength == null)
                return -1;

            lock (context.Gate)
            {
                if (!context.Archive.Contains(name))
                    return -2;

                using Stream source = context.Archive.Open(name, EntryStreamMode.OriginalStream).Source;
                using var managed = new MemoryStream();
                byte[] chunk = new byte[64 * 1024];
                int read;
                while ((read = source.Read(chunk, 0, chunk.Length)) > 0)
                {
                    if (managed.Length + read > MaximumEntryBytes)
                        return -3;
                    managed.Write(chunk, 0, read);
                }

                byte[] bytes = managed.ToArray();
                void* memory = NativeMemory.Alloc((nuint)Math.Max(bytes.Length, 1));
                if (memory == null)
                    return -4;
                bytes.AsSpan().CopyTo(new Span<byte>(memory, bytes.Length));
                *output = (byte*)memory;
                *outputLength = (nuint)bytes.Length;
                return 0;
            }
        }
        catch
        {
            return -5;
        }
    }

    /// <summary>释放 ReadEntry 分配的原生内存；memory 允许为空。</summary>
    [UnmanagedCallersOnly(EntryPoint = "DivaCpkFree", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void Free(void* memory)
    {
        NativeMemory.Free(memory);
    }

    /// <summary>释放 CPK 句柄及其文件资源；handle 允许为空。</summary>
    [UnmanagedCallersOnly(EntryPoint = "DivaCpkClose", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void Close(nint handle)
    {
        try
        {
            if (handle == 0)
                return;

            GCHandle root = GCHandle.FromIntPtr(handle);
            if (root.Target is ArchiveContext context)
            {
                lock (context.Gate)
                    context.Archive.Dispose();
            }
            root.Free();
        }
        catch
        {
            // C ABI 清理函数不把托管异常穿过原生边界。
        }
    }

    /// <summary>返回固定上游提交号，便于宿主记录组件诊断。</summary>
    [UnmanagedCallersOnly(EntryPoint = "DivaCpkBridgeVersion", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Version() => 82380;
    /// <summary>复用MML读取内存FARC指定条目；data/name为受限归档字节和精确文件名，output由Free释放。</summary>
    [UnmanagedCallersOnly(EntryPoint = "DivaFarcReadEntry", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int FarcReadEntry(byte* data,nuint size,byte* name,byte** output,nuint* length)
    {
        if(output==null || length==null) return -1;
        *output=null;*length=0;
        try
        {
            if(data==null || size==0 || size>MaximumEntryBytes || name==null) return -1;
            using var memory=new MemoryStream(new ReadOnlySpan<byte>(data,(int)size).ToArray(),false);
            using FarcArchive archive=BinaryFile.Load<FarcArchive>(memory,true);
            string entryName=Marshal.PtrToStringUTF8((nint)name)??string.Empty;
            if(!archive.FileNames.Contains(entryName,StringComparer.OrdinalIgnoreCase)) return -2;
            using Stream entry=archive.Open(entryName,EntryStreamMode.OriginalStream).Source;
            using var bytes=new MemoryStream();var buffer=new byte[65536];int count;
            while((count=entry.Read(buffer,0,buffer.Length))>0)
            {
                if(bytes.Length+count>MaximumEntryBytes) return -3;
                bytes.Write(buffer,0,count);
            }
            var result=bytes.ToArray();void* allocation=NativeMemory.Alloc((nuint)result.Length);
            if(allocation==null) return -4;
            result.AsSpan().CopyTo(new Span<byte>(allocation,result.Length));
            *output=(byte*)allocation;*length=(nuint)result.Length;return 0;
        }
        catch {return -5;}
    }

    /// <summary>按 SpriteDatabase 中的名称查找实际资源集合文件名与精灵索引。</summary>
    [UnmanagedCallersOnly(EntryPoint = "DivaSpriteDatabaseFind", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SpriteDatabaseFind(byte* data, nuint dataLength, byte* spriteNameUtf8,
        byte* fileNameOutput, int outputCapacity, int* spriteIndex)
    {
        if (spriteIndex != null)
            *spriteIndex = -1;

        try
        {
            if (data == null || dataLength == 0 || dataLength > 16 * 1024 * 1024 || spriteNameUtf8 == null || spriteIndex == null)
                return -1;

            string spriteName = Marshal.PtrToStringUTF8((nint)spriteNameUtf8) ?? string.Empty;
            using var stream = new MemoryStream(new ReadOnlySpan<byte>(data, (int)dataLength).ToArray(), false);
            using SpriteDatabase database = BinaryFile.Load<SpriteDatabase>(stream, true);

            string? matchedFileName = null;
            int matchedIndex = -1;
            foreach (SpriteSetInfo set in database.SpriteSets)
            {
                foreach (SpriteInfo sprite in set.Sprites)
                {
                    if (!string.Equals(sprite.Name, spriteName, StringComparison.OrdinalIgnoreCase))
                        continue;
                    if (matchedFileName != null)
                        return -4;
                    matchedFileName = set.FileName;
                    matchedIndex = sprite.Index;
                }
            }

            if (matchedFileName == null)
                return -2;

            byte[] fileName = Encoding.UTF8.GetBytes(matchedFileName);
            if (fileNameOutput == null || outputCapacity == 0)
            {
                *spriteIndex = matchedIndex;
                return fileName.Length;
            }
            if (outputCapacity <= fileName.Length)
                return -3;

            fileName.CopyTo(new Span<byte>(fileNameOutput, outputCapacity));
            fileNameOutput[fileName.Length] = 0;
            *spriteIndex = matchedIndex;
            return fileName.Length;
        }
        catch
        {
            return -1;
        }
    }

    /// <summary>输出 SpriteDatabase 中匹配筛选文本的精灵名、资源文件和索引，仅供资源诊断。</summary>
    [UnmanagedCallersOnly(EntryPoint = "DivaSpriteDatabaseList", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SpriteDatabaseList(byte* data, nuint dataLength, byte* filterUtf8,
        byte* output, int outputCapacity)
    {
        try
        {
            if (data == null || dataLength == 0 || dataLength > 16 * 1024 * 1024 || filterUtf8 == null || outputCapacity < 0)
                return -1;

            string filter = Marshal.PtrToStringUTF8((nint)filterUtf8) ?? string.Empty;
            using var stream = new MemoryStream(new ReadOnlySpan<byte>(data, (int)dataLength).ToArray(), false);
            using SpriteDatabase database = BinaryFile.Load<SpriteDatabase>(stream, true);
            var result = new StringBuilder();
            foreach (SpriteSetInfo set in database.SpriteSets)
            foreach (SpriteInfo sprite in set.Sprites)
            {
                if (!sprite.Name.Contains(filter, StringComparison.OrdinalIgnoreCase))
                    continue;
                result.Append(sprite.Name).Append('\t').Append(set.FileName).Append('\t').Append(sprite.Index).Append('\n');
                if (result.Length > 4 * 1024 * 1024)
                    return -2;
            }

            byte[] bytes = Encoding.UTF8.GetBytes(result.ToString());
            if (output == null || outputCapacity == 0)
                return bytes.Length;
            if (outputCapacity <= bytes.Length)
                return -3;
            bytes.CopyTo(new Span<byte>(output, outputCapacity));
            output[bytes.Length] = 0;
            return bytes.Length;
        }
        catch
        {
            return -4;
        }
    }

    /// <summary>按精灵集合实际矩形裁切并返回 RGBA 像素；不支持的纹理格式返回负值。</summary>
    [UnmanagedCallersOnly(EntryPoint = "DivaSpriteDecode", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SpriteDecode(byte* data, nuint dataLength, int spriteIndex,
        byte** output, nuint* outputLength, int* outputWidth, int* outputHeight)
    {
        if (output != null)
            *output = null;
        if (outputLength != null)
            *outputLength = 0;
        if (outputWidth != null)
            *outputWidth = 0;
        if (outputHeight != null)
            *outputHeight = 0;

        try
        {
            if (data == null || dataLength == 0 || dataLength > MaximumEntryBytes || spriteIndex < 0 ||
                output == null || outputLength == null || outputWidth == null || outputHeight == null)
                return -1;

            using var stream = new MemoryStream(new ReadOnlySpan<byte>(data, (int)dataLength).ToArray(), false);
            using SpriteSet spriteSet = BinaryFile.Load<SpriteSet>(stream, true);
            if (spriteIndex >= spriteSet.Sprites.Count)
                return -2;

            Sprite sprite = spriteSet.Sprites[spriteIndex];
            if (sprite.TextureIndex >= spriteSet.TextureSet.Textures.Count)
                return -3;

            Texture texture = spriteSet.TextureSet.Textures[(int)sprite.TextureIndex];
            SubTexture subTexture = texture[0, 0];
            int textureWidth = subTexture.Width;
            int textureHeight = subTexture.Height;
            long fullImageBytes = (long)textureWidth * textureHeight * 4;
            if (textureWidth <= 0 || textureHeight <= 0 || textureWidth > 8192 || textureHeight > 8192 ||
                fullImageBytes <= 0 || fullImageBytes > MaximumEntryBytes)
                return -4;

            int x = (int)MathF.Round(sprite.X, MidpointRounding.AwayFromZero);
            int y = (int)MathF.Round(sprite.Y, MidpointRounding.AwayFromZero);
            int width = (int)MathF.Round(sprite.Width, MidpointRounding.AwayFromZero);
            int height = (int)MathF.Round(sprite.Height, MidpointRounding.AwayFromZero);
            if (x < 0 || y < 0 || width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
                (long)x + width > textureWidth || (long)y + height > textureHeight ||
                (long)width * height * 4 > MaximumEntryBytes)
                return -4;

            byte[] fullImage = texture.IsYCbCr ? DecodeYCbCr(texture) : DecodeTexture(subTexture);
            if (fullImage.Length == 0)
                return -5;

            byte[] cropped = new byte[checked(width * height * 4)];
            for (int row = 0; row < height; row++)
            {
                // SpriteCropper 采用 Rotate180FlipX；其等价结果是按行垂直翻转。
                int sourceY = textureHeight - 1 - (y + row);
                Buffer.BlockCopy(fullImage, (sourceY * textureWidth + x) * 4,
                    cropped, row * width * 4, width * 4);
            }

            void* memory = NativeMemory.Alloc((nuint)cropped.Length);
            if (memory == null)
                return -6;
            cropped.AsSpan().CopyTo(new Span<byte>(memory, cropped.Length));
            *output = (byte*)memory;
            *outputLength = (nuint)cropped.Length;
            *outputWidth = width;
            *outputHeight = height;
            return 0;
        }
        catch
        {
            return -7;
        }
    }

    /// <summary>释放 SpriteDecode 分配的 RGBA 像素内存；memory 允许为空。</summary>
    [UnmanagedCallersOnly(EntryPoint = "DivaSpriteFree", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void SpriteFree(void* memory)
    {
        NativeMemory.Free(memory);
    }

    /// <summary>把不透明句柄解析为受控 CPK 上下文；参数 handle 为 Open 返回的原生句柄。</summary>
    private static ArchiveContext? GetContext(nint handle)
    {
        if (handle == 0)
            return null;
        return GCHandle.FromIntPtr(handle).Target as ArchiveContext;
    }

    /// <summary>解码BC5双通道；texture为经过尺寸与字节边界检查的子纹理。</summary>
    private static float[] DecodeBc5(SubTexture texture)
    {
        int width=texture.Width,height=texture.Height;
        if(width<=0 || height<=0 || (long)width*height*8>MaximumEntryBytes) return Array.Empty<float>();
        int columns=(width+3)/4,rows=(height+3)/4;
        if((long)columns*rows*16!=texture.Data.Length) return Array.Empty<float>();
        var result=new float[width*height*2];
        var palette=new float[8];
        for(int by=0;by<rows;++by) for(int bx=0;bx<columns;++bx) for(int channel=0;channel<2;++channel)
        {
            int offset=(by*columns+bx)*16+channel*8;
            palette[0]=texture.Data[offset]/255f;palette[1]=texture.Data[offset+1]/255f;
            if(texture.Data[offset]>texture.Data[offset+1])
                for(int i=1;i<=6;++i) palette[i+1]=((7-i)*palette[0]+i*palette[1])/7;
            else
            {
                for(int i=1;i<=4;++i) palette[i+1]=((5-i)*palette[0]+i*palette[1])/5;
                palette[6]=0;palette[7]=1;
            }
            ulong indices=0;for(int i=0;i<6;++i) indices|=(ulong)texture.Data[offset+2+i]<<(8*i);
            for(int y=0;y<4;++y) for(int x=0;x<4;++x)
            {
                int px=bx*4+x,py=by*4+y;
                if(px<width && py<height) result[(py*width+px)*2+channel]=palette[(int)((indices>>(3*(y*4+x)))&7)];
            }
        }
        return result;
    }

    /// <summary>按固定上游TextureDecoderCore的YCbCr系数合成RGBA；texture含亮度/透明度与色度两张BC5平面。</summary>
    private static byte[] DecodeYCbCr(Texture texture)
    {
        var luma=texture[0];var chroma=texture[1];
        var first=DecodeBc5(luma);var second=DecodeBc5(chroma);
        if(first.Length==0 || second.Length==0) return Array.Empty<byte>();
        int width=luma.Width,height=luma.Height,cw=chroma.Width,ch=chroma.Height;
        var output=new byte[checked(width*height*4)];
        for(int y=0;y<height;++y) for(int x=0;x<width;++x)
        {
            float sx=Math.Clamp((x+0.5f)*cw/width-0.5f,0,cw-1),sy=Math.Clamp((y+0.5f)*ch/height-0.5f,0,ch-1);
            int x0=(int)sx,y0=(int)sy,x1=Math.Min(x0+1,cw-1),y1=Math.Min(y0+1,ch-1);
            float fx=sx-x0,fy=sy-y0;
            float cb=(second[(y0*cw+x0)*2]*(1-fx)+second[(y0*cw+x1)*2]*fx)*(1-fy)+(second[(y1*cw+x0)*2]*(1-fx)+second[(y1*cw+x1)*2]*fx)*fy;
            float cr=(second[(y0*cw+x0)*2+1]*(1-fx)+second[(y0*cw+x1)*2+1]*fx)*(1-fy)+(second[(y1*cw+x0)*2+1]*(1-fx)+second[(y1*cw+x1)*2+1]*fx)*fy;
            cb=cb*1.003922f-0.503929f;cr=cr*1.003922f-0.503929f;
            int p=y*width+x;float luminance=first[p*2];
            output[p*4]=(byte)(Math.Clamp(luminance+1.5748f*cr,0,1)*255);
            output[p*4+1]=(byte)(Math.Clamp(luminance-0.1873f*cb-0.4681f*cr,0,1)*255);
            output[p*4+2]=(byte)(Math.Clamp(luminance+1.8556f*cb,0,1)*255);
            output[p*4+3]=(byte)(Math.Clamp(first[p*2+1],0,1)*255);
        }
        return output;
    }

    /// <summary>将 MML 精灵纹理中常见的未压缩、DXT1、DXT3 和 DXT5 格式转换为 RGBA。</summary>
    private static byte[] DecodeTexture(SubTexture texture)
    {
        int width = texture.Width;
        int height = texture.Height;
        long pixelCount = (long)width * height;
        long outputByteCount = pixelCount * 4;
        if (width <= 0 || height <= 0 || width > 8192 || height > 8192 || pixelCount <= 0 ||
            outputByteCount <= 0 || outputByteCount > MaximumEntryBytes)
            return Array.Empty<byte>();

        byte[] output = new byte[(int)outputByteCount];
        byte[] input = texture.Data;
        if(texture.Format==TextureFormat.BC7)
        {
            fixed(byte* source=input) fixed(byte* destination=output)
                if(DecodeBc7(source,(nuint)input.Length,width,height,destination,(nuint)output.Length)!=0) return Array.Empty<byte>();
            return output;
        }

        if (texture.Format == TextureFormat.RGBA8)
        {
            if (input.Length != output.Length)
                return Array.Empty<byte>();
            Buffer.BlockCopy(input, 0, output, 0, output.Length);
            return output;
        }

        if (texture.Format == TextureFormat.RGB8)
        {
            if (input.Length != pixelCount * 3)
                return Array.Empty<byte>();
            for (int pixel = 0; pixel < pixelCount; pixel++)
            {
                output[pixel * 4] = input[pixel * 3];
                output[pixel * 4 + 1] = input[pixel * 3 + 1];
                output[pixel * 4 + 2] = input[pixel * 3 + 2];
                output[pixel * 4 + 3] = 255;
            }
            return output;
        }

        if (texture.Format is not (TextureFormat.DXT1 or TextureFormat.DXT1a or TextureFormat.DXT3 or TextureFormat.DXT5))
            return Array.Empty<byte>();

        int blockBytes = texture.Format is TextureFormat.DXT1 or TextureFormat.DXT1a ? 8 : 16;
        int blocksWide = Math.Max(1, (width + 3) / 4);
        int blocksHigh = Math.Max(1, (height + 3) / 4);
        if (input.Length != (long)blocksWide * blocksHigh * blockBytes)
            return Array.Empty<byte>();

        int offset = 0;
        for (int blockY = 0; blockY < blocksHigh; blockY++)
        for (int blockX = 0; blockX < blocksWide; blockX++)
        {
            DecodeDxtBlock(input, offset, texture.Format, blockX * 4, blockY * 4, width, height, output);
            offset += blockBytes;
        }
        return output;
    }

    /// <summary>解码一个 4×4 DXT 块并裁去越界像素；参数 input/offset/format/origin/size/output 描述源块与目标图。</summary>
    private static void DecodeDxtBlock(byte[] input, int offset, TextureFormat format,
        int originX, int originY, int width, int height, byte[] output)
    {
        int colorOffset = offset + (format is TextureFormat.DXT1 or TextureFormat.DXT1a ? 0 : 8);
        ushort color0 = BitConverter.ToUInt16(input, colorOffset);
        ushort color1 = BitConverter.ToUInt16(input, colorOffset + 2);
        Span<byte> colors = stackalloc byte[16];
        Decode565(color0, colors.Slice(0, 4));
        Decode565(color1, colors.Slice(4, 4));
        colors[3] = colors[7] = 255;

        bool transparent = (format is TextureFormat.DXT1 or TextureFormat.DXT1a) && color0 <= color1;
        for (int channel = 0; channel < 3; channel++)
        {
            if (transparent)
            {
                colors[8 + channel] = (byte)((colors[channel] + colors[4 + channel]) / 2);
                colors[12 + channel] = 0;
            }
            else
            {
                colors[8 + channel] = (byte)((2 * colors[channel] + colors[4 + channel]) / 3);
                colors[12 + channel] = (byte)((colors[channel] + 2 * colors[4 + channel]) / 3);
            }
        }
        colors[11] = 255;
        colors[15] = transparent ? (byte)0 : (byte)255;

        ulong alphaIndices = 0;
        Span<byte> alphaPalette = stackalloc byte[8];
        if (format == TextureFormat.DXT5)
        {
            byte a0 = input[offset];
            byte a1 = input[offset + 1];
            alphaPalette[0] = a0;
            alphaPalette[1] = a1;
            if (a0 > a1)
                for (int i = 1; i <= 6; i++) alphaPalette[i + 1] = (byte)(((7 - i) * a0 + i * a1) / 7);
            else
            {
                for (int i = 1; i <= 4; i++) alphaPalette[i + 1] = (byte)(((5 - i) * a0 + i * a1) / 5);
                alphaPalette[6] = 0;
                alphaPalette[7] = 255;
            }
            for (int i = 0; i < 6; i++) alphaIndices |= (ulong)input[offset + 2 + i] << (8 * i);
        }

        uint colorIndices = BitConverter.ToUInt32(input, colorOffset + 4);
        for (int pixel = 0; pixel < 16; pixel++)
        {
            int x = originX + pixel % 4;
            int y = originY + pixel / 4;
            if (x >= width || y >= height)
                continue;

            int colorIndex = (int)((colorIndices >> (pixel * 2)) & 3);
            int destination = (y * width + x) * 4;
            output[destination] = colors[colorIndex * 4];
            output[destination + 1] = colors[colorIndex * 4 + 1];
            output[destination + 2] = colors[colorIndex * 4 + 2];

            if (format == TextureFormat.DXT3)
            {
                int alphaByte = input[offset + pixel / 2];
                int alpha = (pixel & 1) == 0 ? alphaByte & 0x0F : alphaByte >> 4;
                output[destination + 3] = (byte)(alpha * 17);
            }
            else if (format == TextureFormat.DXT5)
                output[destination + 3] = alphaPalette[(int)((alphaIndices >> (pixel * 3)) & 7)];
            else
                output[destination + 3] = colors[colorIndex * 4 + 3];
        }
    }

    /// <summary>将 RGB565 端点扩展到 8 位 RGB；参数 color 为 16 位颜色，output 为四字节像素。</summary>
    private static void Decode565(ushort color, Span<byte> output)
    {
        int red = (color >> 11) & 31;
        int green = (color >> 5) & 63;
        int blue = color & 31;
        output[0] = (byte)((red << 3) | (red >> 2));
        output[1] = (byte)((green << 2) | (green >> 4));
        output[2] = (byte)((blue << 3) | (blue >> 2));
        output[3] = 255;
    }
}
