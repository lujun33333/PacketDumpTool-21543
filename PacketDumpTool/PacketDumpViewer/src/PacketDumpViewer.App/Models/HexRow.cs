namespace PacketDumpViewer.App.Models;

public sealed class HexRow
{
    public required string Offset { get; init; }
    public required string HexBytes { get; init; }
    public required string Ascii { get; init; }
}
