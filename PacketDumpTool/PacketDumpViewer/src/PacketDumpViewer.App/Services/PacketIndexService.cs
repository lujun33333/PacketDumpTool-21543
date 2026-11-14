using System.IO;
using System.Text.RegularExpressions;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class PacketIndexService
{
    private static readonly Regex PayloadNameRegex = new(
        @"^network_bin_(?<pid>\d+)_(?<seq>\d+)_(?<direction>.+)_msg(?<msgId>\d+)_len(?<len>\d+)\.bin$",
        RegexOptions.Compiled | RegexOptions.CultureInvariant | RegexOptions.IgnoreCase);

    public PacketRecord Parse(string filePath, string dumpRootPath)
    {
        var fileInfo = new FileInfo(filePath);
        var match = PayloadNameRegex.Match(fileInfo.Name);
        if (!match.Success)
        {
            return PacketRecord.Unknown(filePath, dumpRootPath);
        }

        return new PacketRecord
        {
            FullPath = fileInfo.FullName,
            RelativePath = Path.GetRelativePath(dumpRootPath, fileInfo.FullName),
            FileName = fileInfo.Name,
            Pid = int.Parse(match.Groups["pid"].Value),
            Seq = int.Parse(match.Groups["seq"].Value),
            Direction = match.Groups["direction"].Value,
            MsgId = int.Parse(match.Groups["msgId"].Value),
            DeclaredLength = long.Parse(match.Groups["len"].Value),
            FileSize = fileInfo.Exists ? fileInfo.Length : 0,
            IsUnknown = false
        };
    }
}
