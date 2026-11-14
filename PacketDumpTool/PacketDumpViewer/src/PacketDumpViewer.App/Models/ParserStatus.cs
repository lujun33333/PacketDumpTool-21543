namespace PacketDumpViewer.App.Models;

public sealed class ParserStatus
{
    public required string Status { get; init; }
    public required string Note { get; init; }
    public required string StructureText { get; init; }
    public IReadOnlyList<ParsedFieldNode> RootNodes { get; init; } = [];
}
