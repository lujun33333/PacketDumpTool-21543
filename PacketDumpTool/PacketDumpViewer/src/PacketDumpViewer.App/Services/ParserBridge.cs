using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class ParserBridge
{
    private readonly Msg100FullParser _msg100FullParser = new();
    private readonly Msg100RequestLoginParser _msg100RequestLoginParser = new();
    private readonly Msg223FullParser _msg223FullParser = new();
    private readonly Msg24251QuickRpcParser _msg24251QuickRpcParser = new();
    private readonly Msg28421ReplayOperateParser _msg28421ReplayOperateParser = new();
    private readonly Msg316ShapeRefitParser _msg316ShapeRefitParser = new();
    private readonly Msg319ShapeLegendOperateParser _msg319ShapeLegendOperateParser = new();
    private readonly Msg509GameBeginParser _msg509GameBeginParser = new();
    private readonly MsgRoomFlowParser _msgRoomFlowParser = new();
    private readonly StructOracleReadService _structOracleReadService = new();

    public ParserStatus GetStatus(PacketRecord? packet, bool compareSiblings = false)
    {
        if (packet is not null && packet.MsgId == 100)
        {
            if (Msg100FullParser.IsMsg100FullCandidate(packet))
            {
                return WithRuntimeOracle(packet, _msg100FullParser.Parse(packet, compareSiblings));
            }

            if (Msg100RequestLoginParser.IsCandidate(packet))
            {
                return WithRuntimeOracle(packet, _msg100RequestLoginParser.Parse(packet));
            }

            return WithRuntimeOracle(packet, BuildFallbackRawParserStatus(packet));
        }

        if (packet is not null && packet.MsgId == 223)
        {
            return WithRuntimeOracle(packet, _msg223FullParser.Parse(packet));
        }

        if (packet is not null && packet.MsgId == 24251)
        {
            return WithRuntimeOracle(packet, _msg24251QuickRpcParser.Parse(packet));
        }

        if (packet is not null && packet.MsgId == 28421)
        {
            return WithRuntimeOracle(packet, _msg28421ReplayOperateParser.Parse(packet));
        }

        if (packet is not null && packet.MsgId == 316)
        {
            return WithRuntimeOracle(packet, _msg316ShapeRefitParser.Parse(packet));
        }

        if (packet is not null && packet.MsgId == 319)
        {
            return WithRuntimeOracle(packet, _msg319ShapeLegendOperateParser.Parse(packet));
        }

        if (packet is not null && packet.MsgId == 509)
        {
            return WithRuntimeOracle(packet, _msg509GameBeginParser.Parse(packet));
        }

        if (packet is not null && packet.MsgId is 102 or 103 or 110 or 120)
        {
            return WithRuntimeOracle(packet, _msgRoomFlowParser.Parse(packet));
        }

        return WithRuntimeOracle(packet, BuildFallbackRawParserStatus(packet));
    }

    private static ParserStatus BuildFallbackRawParserStatus(PacketRecord? packet)
    {
        if (packet is null)
        {
            return new ParserStatus
            {
                Status = "no packet",
                Note = "No packet selected.",
                StructureText = "No packet selected.",
                RootNodes = []
            };
        }

        var structureText =
            $"RawPlainPayload / 原始明文载荷\r\n" +
            $"MsgID={packet.MsgIdDisplay}; direction={packet.Direction}; seq={packet.Seq}; len={packet.FileSize:N0} bytes\r\n" +
            "当前 MsgID 还没有专用官服结构体 parser，所以不会乱编字段名。\r\n" +
            "处理方式：HEX 区可查看真实字节；Structure 先保留 RawBlock，后续按反编译/luc/dump 对齐后再升成 m_ 字段。";

        return new ParserStatus
        {
            Status = "raw fallback",
            Note = "No dedicated parser for this MsgID yet; fallback is raw payload, not guessed structure.",
            StructureText = structureText,
            RootNodes =
            [
                new ParsedFieldNode
                {
                    Offset = 0,
                    Size = checked((int)Math.Min(packet.FileSize, int.MaxValue)),
                    Type = "raw",
                    Name = "RawPlainPayload",
                    Value = $"{packet.FileSize:N0} bytes",
                    Status = "UnknownName",
                    Note = "未找到专用 MsgID parser；保留 raw，等待按官服反编译/luc/dump 对齐"
                }
            ]
        };
    }

    private ParserStatus WithRuntimeOracle(PacketRecord? packet, ParserStatus parsed)
    {
        var oracle = _structOracleReadService.TryLoad(packet);
        if (oracle is null)
        {
            return parsed;
        }

        var roots = new List<ParsedFieldNode> { oracle.RootNode };
        roots.AddRange(parsed.RootNodes);

        return new ParserStatus
        {
            Status = parsed.Status == "not connected"
                ? "runtime oracle"
                : $"{parsed.Status} + runtime oracle",
            Note = $"{parsed.Note}; {oracle.Summary}",
            StructureText = $"{oracle.Summary}{Environment.NewLine}{parsed.StructureText}",
            RootNodes = roots
        };
    }
}
