using System.IO;

namespace PacketDumpViewer.App.Services;

public sealed class PayloadReadService
{
    public async Task<byte[]> ReadPageAsync(
        string filePath,
        long offset,
        int pageSize,
        CancellationToken cancellationToken = default)
    {
        if (!File.Exists(filePath))
        {
            throw new FileNotFoundException("payload 文件不存在。", filePath);
        }

        if (pageSize <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(pageSize));
        }

        var fileInfo = new FileInfo(filePath);
        if (fileInfo.Length == 0 || offset >= fileInfo.Length)
        {
            return [];
        }

        offset = Math.Max(0, offset);
        var count = (int)Math.Min(pageSize, fileInfo.Length - offset);
        var buffer = new byte[count];

        await using var stream = new FileStream(
            filePath,
            FileMode.Open,
            FileAccess.Read,
            FileShare.ReadWrite | FileShare.Delete,
            bufferSize: 4096,
            options: FileOptions.Asynchronous | FileOptions.RandomAccess);

        stream.Seek(offset, SeekOrigin.Begin);
        var totalRead = 0;
        while (totalRead < count)
        {
            var read = await stream.ReadAsync(buffer.AsMemory(totalRead, count - totalRead), cancellationToken);
            if (read == 0)
            {
                break;
            }

            totalRead += read;
        }

        if (totalRead == buffer.Length)
        {
            return buffer;
        }

        Array.Resize(ref buffer, totalRead);
        return buffer;
    }
}
