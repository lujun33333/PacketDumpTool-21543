namespace PacketDumpViewer.App.Services;

public static class QqTeaCipher
{
    private const uint Delta = 0x9E3779B9;
    private const int TailPadLength = 7;

    public static byte[] Decrypt(ReadOnlySpan<byte> cipher, ReadOnlySpan<byte> key, bool strict = true)
    {
        if (key.Length != 16)
        {
            throw new ArgumentException("QQSpeed TEA key must be 16 bytes.", nameof(key));
        }

        if (cipher.Length < 16 || cipher.Length % 8 != 0)
        {
            throw new ArgumentException($"QQSpeed TEA cipher length is illegal: {cipher.Length}", nameof(cipher));
        }

        var k = new uint[4];
        for (var i = 0; i < k.Length; i++)
        {
            k[i] = ReadU32Be(key[(i * 4)..]);
        }

        var prevCrypt = new byte[8];
        var prevPre = new byte[8];
        var output = new byte[cipher.Length];

        Span<byte> mixed = stackalloc byte[8];
        Span<byte> decryptedPre = stackalloc byte[8];
        for (var offset = 0; offset < cipher.Length; offset += 8)
        {
            for (var i = 0; i < 8; i++)
            {
                mixed[i] = (byte)(cipher[offset + i] ^ prevPre[i]);
            }

            DecryptBlock(mixed, k, decryptedPre);

            for (var i = 0; i < 8; i++)
            {
                output[offset + i] = (byte)(decryptedPre[i] ^ prevCrypt[i]);
                prevCrypt[i] = cipher[offset + i];
                prevPre[i] = decryptedPre[i];
            }
        }

        var padLength = output[0] & 0x07;
        var bodyStart = 1 + padLength + 2;
        var bodyEnd = output.Length - TailPadLength;
        if (bodyEnd < bodyStart)
        {
            throw new InvalidOperationException($"QQSpeed TEA plain is too short: total={output.Length}, pad={padLength}");
        }

        if (strict)
        {
            for (var i = bodyEnd; i < output.Length; i++)
            {
                if (output[i] != 0)
                {
                    throw new InvalidOperationException("QQSpeed TEA tail check failed.");
                }
            }
        }

        return output[bodyStart..bodyEnd];
    }

    private static void DecryptBlock(ReadOnlySpan<byte> block, IReadOnlyList<uint> key, Span<byte> output)
    {
        var y = ReadU32Be(block);
        var z = ReadU32Be(block[4..]);
        var sum = Delta << 4;

        for (var i = 0; i < 16; i++)
        {
            z -= (((y << 4) + key[2]) ^ (y + sum) ^ ((y >> 5) + key[3]));
            y -= (((z << 4) + key[0]) ^ (z + sum) ^ ((z >> 5) + key[1]));
            sum -= Delta;
        }

        WriteU32Be(output, y);
        WriteU32Be(output[4..], z);
    }

    private static uint ReadU32Be(ReadOnlySpan<byte> data) =>
        ((uint)data[0] << 24) |
        ((uint)data[1] << 16) |
        ((uint)data[2] << 8) |
        data[3];

    private static void WriteU32Be(Span<byte> output, uint value)
    {
        output[0] = (byte)(value >> 24);
        output[1] = (byte)(value >> 16);
        output[2] = (byte)(value >> 8);
        output[3] = (byte)value;
    }
}
