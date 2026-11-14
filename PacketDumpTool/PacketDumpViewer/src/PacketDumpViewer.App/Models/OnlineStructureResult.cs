namespace PacketDumpViewer.App.Models;

public sealed record OnlineStructureResult(
    string Text,
    IReadOnlyList<ParsedFieldNode> RootNodes);
