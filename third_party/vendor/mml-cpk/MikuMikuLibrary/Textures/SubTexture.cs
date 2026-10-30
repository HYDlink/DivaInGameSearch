using MikuMikuLibrary.IO.Common;

namespace MikuMikuLibrary.Textures;

public class SubTexture
{
    public int Width { get; private set; }
    public int Height { get; private set; }
    public TextureFormat Format { get; private set; }
    public byte[] Data { get; private set; }

    internal void Read(EndianBinaryReader reader)
    {
        int signature = reader.ReadInt32();

        if (signature != 0x02505854)
            throw new InvalidDataException("Invalid signature (expected TXP with type 2)");

        Width = reader.ReadInt32();
        Height = reader.ReadInt32();
        Format = (TextureFormat)reader.ReadInt32();
        reader.SeekCurrent(4); // ID

        int dataSize = reader.ReadInt32();
        // 原始纹理也须在读取字节前检查尺寸与剩余数据，不能只限制最后裁片。
        if (Width <= 0 || Height <= 0 || Width > 8192 || Height > 8192 ||
            (long)Width * Height * 4 > 64L * 1024 * 1024 || dataSize < 0 ||
            dataSize > 64 * 1024 * 1024 || dataSize > reader.BaseStream.Length - reader.BaseStream.Position)
            throw new InvalidDataException("纹理尺寸或数据长度超出安全范围。");
        Data = reader.ReadBytes(dataSize);
    }

    internal void Write(EndianBinaryWriter writer, int id)
    {
        writer.Write(0x02505854);
        writer.Write(Width);
        writer.Write(Height);
        writer.Write((int)Format);
        writer.Write(id);
        writer.Write(Data.Length);
        writer.Write(Data);
    }

    internal SubTexture(EndianBinaryReader reader)
    {
        Read(reader);
    }

    public SubTexture(int width, int height, TextureFormat format)
    {
        Width = Math.Max(1, width);
        Height = Math.Max(1, height);
        Format = format;
        Data = new byte[TextureFormatUtilities.CalculateDataSize(width, height, format)];
    }
}
