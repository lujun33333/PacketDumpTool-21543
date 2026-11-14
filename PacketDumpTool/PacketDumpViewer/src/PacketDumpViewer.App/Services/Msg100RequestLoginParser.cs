using System.Globalization;
using System.IO;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class Msg100RequestLoginParser
{
    public static bool IsCandidate(PacketRecord packet)
    {
        if (packet.MsgId != 100)
        {
            return false;
        }

        return packet.Direction.Contains("serialize_body", StringComparison.OrdinalIgnoreCase) ||
               packet.Direction.Contains("plain-send-body", StringComparison.OrdinalIgnoreCase) ||
               packet.Direction.Contains("send_body", StringComparison.OrdinalIgnoreCase) ||
               packet.Direction.Contains("online_send", StringComparison.OrdinalIgnoreCase);
    }

    public ParserStatus Parse(PacketRecord packet)
    {
        if (!File.Exists(packet.FullPath))
        {
            return new ParserStatus
            {
                Status = "file missing",
                Note = "payload 文件不存在。",
                StructureText = "Payload file missing.",
                RootNodes = []
            };
        }

        var data = File.ReadAllBytes(packet.FullPath);
        return ParseBytes(data, packet.Direction);
    }

    public ParserStatus ParseBytes(byte[] data, string sourceDirection = "")
    {
        var root = new ParsedFieldNode
        {
            Offset = 0,
            Size = data.Length,
            Type = "struct",
            Name = "CSSafeCRequestLoginPara",
            Value = $"{data.Length:N0} bytes",
            Status = data.Length >= 2 && ReadU16BE(data, 0) == data.Length ? "Verified" : "Unaligned",
            Note = "C->S MsgID 100 request body; Network.dll SerializeBody case 100 -> sub_1800D1190; body-only，不含 CSHEAD/MSGHEAD"
        };
        root.IsExpanded = true;

        if (data.Length < 2)
        {
            root.AddChild(Node(0, data.Length, "raw", "TruncatedPayload", $"{data.Length:N0} bytes", "Unaligned", "payload shorter than serialized length field"));
            return Result(root, "unaligned", "MsgID 100 request body 太短，无法解析。");
        }

        var cursor = 0;
        var declaredLength = AddU16BE(root, data, ref cursor, "SerializedLength", "sub_1800D1190 最后写入的 body 总长度，BE u16，通常等于 payload 文件长度");
        if (declaredLength != data.Length)
        {
            root.AddChild(Node(0, 0, "note", "LengthMismatch", $"declared={declaredLength}; actual={data.Length}", "Unaligned", "长度字段与实际 payload 长度不一致"));
        }

        AddU32BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0000", "object+0x0000；语义待继续从 21273 Network.dll xref 确认");
        AddU32BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0004", "object+0x0004；语义待继续从 21273 Network.dll xref 确认");
        AddU32BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0008", "object+0x0008；当前样本值常见为 21273，但未直接提升为字段名");
        AddBytes(root, data, ref cursor, 20, "CSSafeCRequestLoginPara.m_field_000C_bytes20", "object+0x000C；固定 20 bytes，由 sub_18000F9A0 memcpy 写入");
        AddU8(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0020", "object+0x0020");
        AddU8(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0021", "object+0x0021");
        AddU32BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0022", "object+0x0022");
        AddU32BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0026", "object+0x0026");
        AddU32BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_002A", "object+0x002A");
        AddU32BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_002E", "object+0x002E");
        AddU8(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0032", "object+0x0032");
        AddU32BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0033", "object+0x0033");
        AddU32BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0037", "object+0x0037");
        AddU16BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_003B", "object+0x003B");

        var len1 = AddU8(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_003D_len", "object+0x003D；后接 object+0x003E 变长 bytes");
        AddVariableBytes(root, data, ref cursor, len1, "CSSafeCRequestLoginPara.m_field_003E_bytes", "由 m_field_003D_len 决定长度");

        AddU32BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_013E", "object+0x013E");
        AddNestedFiveU32Block(root, data, ref cursor);
        AddU32BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0156", "object+0x0156");
        AddU8(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_015A", "object+0x015A");

        var len2 = AddU16BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_015B_len", "object+0x015B；cap=512；后接 object+0x015D bytes");
        AddVariableBytes(root, data, ref cursor, Math.Min(len2, 512), "CSSafeCRequestLoginPara.m_field_015D_bytes", len2 > 512 ? "长度被官服代码限制到 512" : "由 m_field_015B_len 决定长度");

        AddU32BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_035D", "object+0x035D");
        AddU8(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0361", "object+0x0361");
        var len3 = AddU16BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0362_len", "object+0x0362；cap=100；后接 object+0x0364 bytes");
        AddVariableBytes(root, data, ref cursor, Math.Min(len3, 100), "CSSafeCRequestLoginPara.m_field_0364_bytes", len3 > 100 ? "长度被官服代码限制到 100" : "由 m_field_0362_len 决定长度");

        var len4 = AddU16BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_03C8_len", "object+0x03C8；cap=256；后接 object+0x03CA bytes");
        AddVariableBytes(root, data, ref cursor, Math.Min(len4, 256), "CSSafeCRequestLoginPara.m_field_03CA_bytes", len4 > 256 ? "长度被官服代码限制到 256" : "由 m_field_03C8_len 决定长度");

        AddU32BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_04CA", "object+0x04CA");
        AddU8(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_04CE", "object+0x04CE");
        AddU8(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_04CF", "object+0x04CF");
        var len5 = AddU16BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_04D0_len", "object+0x04D0；cap=2048；后接 object+0x04D2 bytes");
        AddVariableBytes(root, data, ref cursor, Math.Min(len5, 2048), "CSSafeCRequestLoginPara.m_field_04D2_bytes", len5 > 2048 ? "长度被官服代码限制到 2048" : "由 m_field_04D0_len 决定长度");

        AddU8(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0CD2", "object+0x0CD2");
        var len6 = AddU16BE(root, data, ref cursor, "CSSafeCRequestLoginPara.m_field_0CD3_len", "object+0x0CD3；cap=4096；后接 object+0x0CD5 bytes");
        AddVariableBytes(root, data, ref cursor, Math.Min(len6, 4096), "CSSafeCRequestLoginPara.m_field_0CD5_bytes", len6 > 4096 ? "长度被官服代码限制到 4096" : "由 m_field_0CD3_len 决定长度");

        root.AddChild(Node(
            0,
            data.Length,
            "coverage",
            "SerializedBodyCoverage",
            $"cursor={cursor:N0}; len={data.Length:N0}",
            cursor == data.Length ? "Verified" : "Unaligned",
            cursor == data.Length
                ? "当前 parser 按 sub_1800D1190 写入顺序完整覆盖此发送 body"
                : "当前 parser 未完整覆盖或字段长度异常，需要回到反编译继续核对"));

        var status = cursor == data.Length && declaredLength == data.Length ? "parsed" : "parsed with warning";
        var note = "MsgID 100 send/request parser；字段顺序来自 21273 Network.dll sub_1800D1190，未知语义保留 m_field_XXXX，不乱命名。";
        return Result(root, status, note);
    }

    private static ParserStatus Result(ParsedFieldNode root, string status, string note) =>
        new()
        {
            Status = status,
            Note = note,
            StructureText = "Msg100 request SerializeBody structure tree loaded.",
            RootNodes = [root]
        };

    private static void AddNestedFiveU32Block(ParsedFieldNode parent, byte[] data, ref int cursor)
    {
        var blockOffset = cursor;
        var declaredBlockLength = CanRead(data, cursor, 2) ? ReadU16BE(data, cursor) : 0;
        var block = Node(
            blockOffset,
            Math.Min(Math.Max(declaredBlockLength, 2), Math.Max(0, data.Length - blockOffset)),
            "group",
            "CSSafeCRequestLoginPara.m_field_0142_block",
            $"{declaredBlockLength} bytes",
            declaredBlockLength == 22 ? "Verified" : "Unaligned",
            "sub_18002A6A0 写入：be:u16 length + 5 个 be:u32；字段语义待 xref 确认");
        block.IsExpanded = true;
        parent.AddChild(block);

        AddU16BE(block, data, ref cursor, "m_field_0142_block.length", "expected 22 bytes");
        AddU32BE(block, data, ref cursor, "m_field_0142_block.u32_0", "source object+0x0142");
        AddU32BE(block, data, ref cursor, "m_field_0142_block.u32_1", "source object+0x0146");
        AddU32BE(block, data, ref cursor, "m_field_0142_block.u32_2", "source object+0x014A");
        AddU32BE(block, data, ref cursor, "m_field_0142_block.u32_3", "source object+0x014E");
        AddU32BE(block, data, ref cursor, "m_field_0142_block.u32_4", "source object+0x0152");
    }

    private static int AddU8(ParsedFieldNode parent, byte[] data, ref int cursor, string name, string note)
    {
        var offset = cursor;
        var value = CanRead(data, cursor, 1) ? data[cursor].ToString(CultureInfo.InvariantCulture) : "<truncated>";
        parent.AddChild(Node(offset, CanRead(data, cursor, 1) ? 1 : 0, "u8", name, value, CanRead(data, cursor, 1) ? "Verified" : "Unaligned", note));
        cursor += CanRead(data, cursor, 1) ? 1 : 0;
        return value == "<truncated>" ? 0 : int.Parse(value, CultureInfo.InvariantCulture);
    }

    private static int AddU16BE(ParsedFieldNode parent, byte[] data, ref int cursor, string name, string note)
    {
        var offset = cursor;
        var ok = CanRead(data, cursor, 2);
        var value = ok ? ReadU16BE(data, cursor) : 0;
        parent.AddChild(Node(offset, ok ? 2 : 0, "be:u16", name, ok ? value.ToString(CultureInfo.InvariantCulture) : "<truncated>", ok ? "Verified" : "Unaligned", note));
        cursor += ok ? 2 : 0;
        return value;
    }

    private static long AddU32BE(ParsedFieldNode parent, byte[] data, ref int cursor, string name, string note)
    {
        var offset = cursor;
        var ok = CanRead(data, cursor, 4);
        var value = ok ? ReadU32BE(data, cursor) : 0;
        parent.AddChild(Node(offset, ok ? 4 : 0, "be:u32", name, ok ? value.ToString(CultureInfo.InvariantCulture) : "<truncated>", ok ? "Verified" : "Unaligned", note));
        cursor += ok ? 4 : 0;
        return value;
    }

    private static void AddBytes(ParsedFieldNode parent, byte[] data, ref int cursor, int size, string name, string note)
    {
        var offset = cursor;
        var available = Math.Max(0, Math.Min(size, data.Length - cursor));
        parent.AddChild(Node(offset, available, "bytes", name, $"{available:N0}/{size:N0} bytes", available == size ? "Verified" : "Unaligned", $"{note}; raw content 看右侧 Hex，结构树不展开登录敏感字节"));
        cursor += available;
    }

    private static void AddVariableBytes(ParsedFieldNode parent, byte[] data, ref int cursor, int declaredSize, string name, string note)
    {
        var offset = cursor;
        var available = Math.Max(0, Math.Min(declaredSize, data.Length - cursor));
        parent.AddChild(Node(offset, available, "bytes", name, $"{available:N0}/{declaredSize:N0} bytes", available == declaredSize ? "Verified" : "Unaligned", $"{note}; raw content 看右侧 Hex，结构树只显示范围/长度"));
        cursor += available;
    }

    private static ParsedFieldNode Node(long offset, int size, string type, string name, string value, string status, string note) =>
        new()
        {
            Offset = offset,
            Size = size,
            Type = type,
            Name = name,
            Value = value,
            Status = status,
            Note = note
        };

    private static bool CanRead(byte[] data, int offset, int size) =>
        offset >= 0 && size >= 0 && offset <= data.Length - size;

    private static int ReadU16BE(byte[] data, int offset) =>
        (data[offset] << 8) | data[offset + 1];

    private static uint ReadU32BE(byte[] data, int offset) =>
        ((uint)data[offset] << 24) |
        ((uint)data[offset + 1] << 16) |
        ((uint)data[offset + 2] << 8) |
        data[offset + 3];
}
