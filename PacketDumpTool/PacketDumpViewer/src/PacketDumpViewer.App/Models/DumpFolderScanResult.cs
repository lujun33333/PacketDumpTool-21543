namespace PacketDumpViewer.App.Models;

public sealed class DumpFolderScanResult
{
    public required string RootPath { get; init; }
    public required string PayloadDirectoryPath { get; init; }
    public bool PayloadDirectoryExists { get; init; }
    public bool NetworkTraceLogExists { get; init; }
    public bool NetworkDumpLogExists { get; init; }
    public bool NetworkParserStateLogExists { get; init; }
    public required IReadOnlyList<PacketRecord> Packets { get; init; }

    public string LogStatusText =>
        $"trace: {(NetworkTraceLogExists ? "OK" : "missing")} | " +
        $"dump: {(NetworkDumpLogExists ? "OK" : "missing")} | " +
        $"parser_state: {(NetworkParserStateLogExists ? "OK" : "missing")}";
}
