using System.IO;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class DumpFolderScanner
{
    private readonly PacketIndexService _packetIndexService;

    public DumpFolderScanner(PacketIndexService packetIndexService)
    {
        _packetIndexService = packetIndexService;
    }

    public DumpFolderScanResult Scan(string dumpRootPath)
    {
        if (string.IsNullOrWhiteSpace(dumpRootPath))
        {
            throw new InvalidOperationException("请选择 dump 目录。");
        }

        var root = Path.GetFullPath(dumpRootPath);
        if (!Directory.Exists(root))
        {
            throw new DirectoryNotFoundException($"目录不存在: {root}");
        }

        var payloadDirectory = Path.Combine(root, "payloads");
        var payloadsExist = Directory.Exists(payloadDirectory);
        var packets = payloadsExist
            ? Directory.EnumerateFiles(payloadDirectory, "*.bin", SearchOption.TopDirectoryOnly)
                .Select(path => _packetIndexService.Parse(path, root))
                .OrderBy(packet => packet.Seq ?? int.MaxValue)
                .ThenBy(packet => packet.MsgId ?? int.MaxValue)
                .ThenBy(packet => packet.Direction, StringComparer.OrdinalIgnoreCase)
                .ThenBy(packet => packet.FileName, StringComparer.OrdinalIgnoreCase)
                .ToList()
            : [];

        return new DumpFolderScanResult
        {
            RootPath = root,
            PayloadDirectoryPath = payloadDirectory,
            PayloadDirectoryExists = payloadsExist,
            NetworkTraceLogExists = Directory.EnumerateFiles(root, "network_trace_*.log").Any(),
            NetworkDumpLogExists = Directory.EnumerateFiles(root, "network_dump_*.log").Any(),
            NetworkParserStateLogExists = Directory.EnumerateFiles(root, "network_parser_state_*.log").Any(),
            Packets = packets
        };
    }
}
