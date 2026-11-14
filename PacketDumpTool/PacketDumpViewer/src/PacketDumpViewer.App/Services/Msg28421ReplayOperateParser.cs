using System.Buffers.Binary;
using System.Globalization;
using System.IO;
using System.Text;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class Msg28421ReplayOperateParser
{
    private const int MinReplayInfoSize = 53;
    private const int MaxReplayInfoCount = 128;

    static Msg28421ReplayOperateParser()
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
    }

    public ParserStatus Parse(PacketRecord packet)
    {
        if (packet.MsgId != 28421)
        {
            return Unsupported(packet, "不是 MsgID 28421。");
        }

        if (!File.Exists(packet.FullPath))
        {
            return new ParserStatus
            {
                Status = "file missing",
                Note = "payload 文件不存在。",
                StructureText = "Payload file missing.",
                RootNodes = [Node(0, 0, "error", "PayloadFileMissing", packet.FullPath, "Unaligned", "无法读取 payload 文件")]
            };
        }

        var data = File.ReadAllBytes(packet.FullPath);
        var frame = DetectFrame(data);

        if (LooksClientToServer(packet.Direction) && TryParseRequest(data, frame, packet, out var preferredRequest))
        {
            return preferredRequest;
        }

        if (TryParseResponse(data, frame, packet, out var response))
        {
            return response;
        }

        if (TryParseRequest(data, frame, packet, out var request))
        {
            return request;
        }

        return new ParserStatus
        {
            Status = "unsupported",
            Note = $"MsgID 28421 未能按 RequestReplayOperate / ResponseReplayOperate 对齐；direction={packet.Direction}。",
            StructureText = "Unsupported Msg28421 replay payload.",
            RootNodes =
            [
                Node(0, data.Length, "raw", "UnsupportedMsg28421Payload", $"{data.Length:N0} bytes", "Unaligned",
                    "没有足够证据按官服字段名展开；请看 Hex View 和同级 dump 对照")
            ]
        };
    }

    private static bool TryParseResponse(byte[] data, FrameView frame, PacketRecord packet, out ParserStatus status)
    {
        status = default!;
        var cursor = frame.PayloadStart;
        if (!CanRead(data, cursor, 4 + 2 + 1 + 4 + 4))
        {
            return false;
        }

        var cmd = U8(data, cursor + 6);
        var replayNum = I32(data, cursor + 11);
        if (cmd > 64 || replayNum < 0 || replayNum > MaxReplayInfoCount)
        {
            return false;
        }

        var root = RootNode(data, frame, "ResponseReplayOperate");
        root.AddChild(EvidenceNode());

        if (frame.HasLengthPrefix)
        {
            root.AddChild(Node(0, 2, "be:u16", "PacketBodyLengthPrefix", frame.DeclaredEnd.ToString(CultureInfo.InvariantCulture), "Verified",
                "Network length-prefix/declared end；结构体正文从 0x00000002 开始，后续超过该边界的 dump 字节不强行命名"));
        }

        var response = root.AddChild(Node(frame.PayloadStart, Math.Max(0, frame.DeclaredEnd - frame.PayloadStart), "struct", "ResponseReplayOperate", "", "Verified",
            "S→C 响应/通知；字段顺序以 21273 Top-Kart OnResponseReplayOperate 消费点、当前新版本 dump 边界和历史 m_ 命名交叉核对为准"));

        response.AddChild(Node(cursor, 4, "be:u32", "m_uiUin", U32(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic",
            "dump 日志 arg7 / pcaps7 field_0 可对齐；不同账号会变化"));
        cursor += 4;

        response.AddChild(Node(cursor, 2, "be:i16", "m_shResultID", I16(data, cursor).ToString(CultureInfo.InvariantCulture), "Verified",
            "Top-Kart 在 a2+4 检查 ResultID；当前官服样本为 0"));
        cursor += 2;

        response.AddChild(Node(cursor, 1, "u8", "m_ucCmd", cmd.ToString(CultureInfo.InvariantCulture), "Verified",
            "Top-Kart sub_180D15B10 读取 a2+6 作为 replay operate cmd；当前 dump 为 20"));
        cursor += 1;

        response.AddChild(Node(cursor, 4, "be:i32", "m_iPara0", I32(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic",
            "旧私服示例曾固定写 99；官服当前样本为 0，不按旧值硬填"));
        cursor += 4;

        response.AddChild(Node(cursor, 4, "be:i32", "m_iReplayNum", replayNum.ToString(CultureInfo.InvariantCulture), "Verified",
            "Top-Kart OnResponseReplayOperate 日志第三个参数读取 a2+11；控制后续 ReplayInfo 数组数量"));
        cursor += 4;

        var arrayStart = cursor;
        var array = response.AddChild(Node(arrayStart, 0, "array", "m_astReplayInfo", $"count={replayNum}", "Verified",
            "每个元素是带 2B 长度前缀的 ReplayInfo；字节形态与当前 dump/21273 28421 嵌套块一致，m_ 字段名只在不冲突时参考历史命名"));
        for (var i = 0; i < replayNum; i++)
        {
            if (!TryReadReplayInfo(data, cursor, frame.DeclaredEnd, $"m_astReplayInfo[{i}]", out var info, out var next))
            {
                return false;
            }

            array.AddChild(info);
            cursor = next;
        }

        array.Value = $"count={replayNum}; bytes={cursor - arrayStart}";

        if (!CanRead(data, cursor, 1) || cursor + 1 > frame.DeclaredEnd)
        {
            return false;
        }

        var urlLen = U8(data, cursor);
        response.AddChild(Node(cursor, 1, "u8", "m_ucUrlTmplLen", urlLen.ToString(CultureInfo.InvariantCulture), "Verified",
            "旧 PacketStruct 写在 ResponseReplayOperate 尾部；Top-Kart 消费 expanded object 的 a2+71715"));
        cursor += 1;

        if (urlLen > 0)
        {
            if (!CanRead(data, cursor, urlLen) || cursor + urlLen > frame.DeclaredEnd)
            {
                return false;
            }

            response.AddChild(Node(cursor, urlLen, "bytes/string", "m_szUrlTmpl", DecodeText(data.AsSpan(cursor, urlLen)), "Dynamic",
                "长度由 m_ucUrlTmplLen 控制；Top-Kart 会把非空 URL 模板缓存到客户端对象"));
            cursor += urlLen;
        }

        AddDeclaredAndDumpTail(root, data, frame, cursor);

        status = new ParserStatus
        {
            Status = "parsed",
            Note = $"MsgID 28421 已按 ResponseReplayOperate 解析；direction={packet.Direction}；当前 dump 的有效 wire 区间到 0x{frame.DeclaredEnd:X}。",
            StructureText = "Msg28421 ResponseReplayOperate structure tree loaded.",
            RootNodes = [root]
        };
        return true;
    }

    private static bool TryParseRequest(byte[] data, FrameView frame, PacketRecord packet, out ParserStatus status)
    {
        status = default!;
        var cursor = frame.PayloadStart;
        if (!CanRead(data, cursor, 4 + 1 + 4 + 2))
        {
            return false;
        }

        var cmd = U8(data, cursor + 4);
        if (cmd > 64)
        {
            return false;
        }

        var root = RootNode(data, frame, "RequestReplayOperate");
        root.AddChild(EvidenceNode());

        if (frame.HasLengthPrefix)
        {
            root.AddChild(Node(0, 2, "be:u16", "PacketBodyLengthPrefix", frame.DeclaredEnd.ToString(CultureInfo.InvariantCulture), "Verified",
                "Network.dll serializer 写入的 body 长度前缀；结构体正文从 0x00000002 开始"));
        }

        var request = root.AddChild(Node(frame.PayloadStart, Math.Max(0, frame.DeclaredEnd - frame.PayloadStart), "struct", "RequestReplayOperate", "", "Verified",
            "C→S 请求/发送；当前 parser 预留请求形态，真实对齐仍以新版本 dump/21273 serializer 为准"));

        request.AddChild(Node(cursor, 4, "be:u32", "m_uiUin", U32(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        cursor += 4;
        request.AddChild(Node(cursor, 1, "u8", "m_ucCmd", cmd.ToString(CultureInfo.InvariantCulture), "Verified", ""));
        cursor += 1;
        request.AddChild(Node(cursor, 4, "be:i32", "m_iPara0", I32(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        cursor += 4;

        if (!TryReadReplayInfo(data, cursor, frame.DeclaredEnd, "m_stReqReplayInfo", out var reqInfo, out var next))
        {
            return false;
        }

        request.AddChild(reqInfo);
        cursor = next;

        if (!CanRead(data, cursor, 4) || cursor + 4 > frame.DeclaredEnd)
        {
            return false;
        }

        request.AddChild(Node(cursor, 4, "be:i32", "m_iPara1", I32(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        cursor += 4;

        AddDeclaredAndDumpTail(root, data, frame, cursor);

        status = new ParserStatus
        {
            Status = "parsed",
            Note = $"MsgID 28421 已按 RequestReplayOperate 解析；direction={packet.Direction}。",
            StructureText = "Msg28421 RequestReplayOperate structure tree loaded.",
            RootNodes = [root]
        };
        return true;
    }

    private static bool TryReadReplayInfo(byte[] data, int offset, int declaredEnd, string name, out ParsedFieldNode node, out int next)
    {
        node = default!;
        next = offset;

        if (!CanRead(data, offset, 2))
        {
            return false;
        }

        var length = U16(data, offset);
        if (length < MinReplayInfoSize || offset + length > declaredEnd)
        {
            return false;
        }

        var end = offset + length;
        var cursor = offset + 2;
        node = Node(offset, length, "struct", name, $"len={length}", "Verified",
            "ReplayInfo 长度块；当前 dump 可按 Uuid/GIdx/FileMd5/NickName17/6*u32/Other/KeyBuffer/KeyHash/ReplayVersion 对齐");

        node.AddChild(Node(offset, 2, "be:u16", "m_usReplayInfoLen", length.ToString(CultureInfo.InvariantCulture), "Verified", ""));

        if (!AddVarBytes(node, data, ref cursor, end, 2, 0x40, "m_szUuid", "m_usUuidLen"))
        {
            return false;
        }

        if (!AddVarBytes(node, data, ref cursor, end, 2, 0x0A, "m_szGIdx", "m_usGIdxLen"))
        {
            return false;
        }

        if (!AddVarBytes(node, data, ref cursor, end, 1, 0x21, "m_szFileMd5", "m_ucFileMd5Len"))
        {
            return false;
        }

        if (!CanRead(data, cursor, 17) || cursor + 17 > end)
        {
            return false;
        }

        node.AddChild(Node(cursor, 17, "bytes/string", "m_szNickName", DecodeFixed(data.AsSpan(cursor, 17)), CountNonZero(data, cursor, 17) == 0 ? "Reserved" : "Dynamic",
            "21273 serializer 固定复制 17 bytes；旧 PacketStruct 注释为 NickName"));
        cursor += 17;

        AddI32(node, data, ref cursor, end, "m_iBaseMode");
        AddI32(node, data, ref cursor, end, "m_iSubMode");
        AddI32(node, data, ref cursor, end, "m_iChannel");
        AddI32(node, data, ref cursor, end, "m_iMapID");
        AddU32(node, data, ref cursor, end, "m_uiFinTime");
        AddU32(node, data, ref cursor, end, "m_uiCTime");

        if (!CanRead(data, cursor, 1) || cursor + 1 > end)
        {
            return false;
        }

        node.AddChild(Node(cursor, 1, "u8", "m_ucOther", U8(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        cursor += 1;

        if (!AddVarBytes(node, data, ref cursor, end, 1, 0xFF, "m_acKeyBuffer", "m_ucKeyBufferLen", valueAsHex: true))
        {
            return false;
        }

        if (!AddVarBytes(node, data, ref cursor, end, 1, 0x40, "m_acKeyHash", "m_ucKeyHashLen", valueAsHex: true))
        {
            return false;
        }

        if (!CanRead(data, cursor, 2) || cursor + 2 > end)
        {
            return false;
        }

        node.AddChild(Node(cursor, 2, "be:u16", "m_usReplayVersion", U16(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        cursor += 2;

        if (cursor < end)
        {
            node.AddChild(Node(cursor, end - cursor, "raw", "UnknownReplayInfoTail", RawSummary(data, cursor, end - cursor), "UnknownName",
                "长度块还有剩余字节，但未在 21273 serializer / Top-Kart 消费点确认字段名"));
            cursor = end;
        }

        next = end;
        return true;
    }

    private static bool AddVarBytes(ParsedFieldNode parent, byte[] data, ref int cursor, int end, int lenSize, int maxLen, string valueName, string lengthName, bool valueAsHex = false)
    {
        if (!CanRead(data, cursor, lenSize) || cursor + lenSize > end)
        {
            return false;
        }

        var lengthOffset = cursor;
        var length = lenSize == 2 ? U16(data, cursor) : U8(data, cursor);
        if (length > maxLen)
        {
            return false;
        }

        parent.AddChild(Node(lengthOffset, lenSize, lenSize == 2 ? "be:u16" : "u8", lengthName, length.ToString(CultureInfo.InvariantCulture), "Verified", ""));
        cursor += lenSize;

        if (!CanRead(data, cursor, length) || cursor + length > end)
        {
            return false;
        }

        var value = length == 0
            ? ""
            : valueAsHex
                ? Convert.ToHexString(data, cursor, length)
                : DecodeText(data.AsSpan(cursor, length));
        parent.AddChild(Node(cursor, length, valueAsHex ? "bytes" : "bytes/string", valueName, value, length == 0 ? "Reserved" : "Dynamic",
            $"length from {lengthName}"));
        cursor += length;
        return true;
    }

    private static void AddI32(ParsedFieldNode parent, byte[] data, ref int cursor, int end, string name)
    {
        if (!CanRead(data, cursor, 4) || cursor + 4 > end)
        {
            parent.AddChild(Node(cursor, 4, "be:i32", name, "", "Unaligned", "payload 长度不足"));
            cursor = end + 1;
            return;
        }

        parent.AddChild(Node(cursor, 4, "be:i32", name, I32(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        cursor += 4;
    }

    private static void AddU32(ParsedFieldNode parent, byte[] data, ref int cursor, int end, string name)
    {
        if (!CanRead(data, cursor, 4) || cursor + 4 > end)
        {
            parent.AddChild(Node(cursor, 4, "be:u32", name, "", "Unaligned", "payload 长度不足"));
            cursor = end + 1;
            return;
        }

        var value = U32(data, cursor);
        var formatted = IsLikelyUnixTime(value)
            ? $"{value} / {DateTimeOffset.FromUnixTimeSeconds(value):yyyy-MM-dd HH:mm:ss}"
            : value.ToString(CultureInfo.InvariantCulture);
        parent.AddChild(Node(cursor, 4, "be:u32", name, formatted, "Dynamic", ""));
        cursor += 4;
    }

    private static ParsedFieldNode RootNode(byte[] data, FrameView frame, string variant)
    {
        return Node(0, data.Length, "struct", $"Msg28421{variant}", $"{data.Length:N0} bytes", "Verified",
            $"新版本 dump/21273 对齐：28421 = ReplayOperate；cmd_map 旧别名 CNotifyGuildBroadcast 仅作索引；declaredEnd=0x{frame.DeclaredEnd:X}");
    }

    private static ParsedFieldNode EvidenceNode()
    {
        return Node(0, 0, "note", "Evidence", "21273 Network.dll/Top-Kart.dll + current dump", "Verified",
            @"S→C 主证据：Top-Kart 21273 sub_180D15B10 消费 expanded object 的 a2+4 ResultID/a2+6 Cmd/a2+11 ReplayNum/a2+71715 UrlTmplLen；当前 packet_dumps_* 的 28421 length-prefix=0x0051，pcaps7 S2C 28421 body_len=81。Network 21273 sub_180078540/sub_18009C180 是 28421 serializer/请求侧证据，只辅助确认 length-prefix 与嵌套块形态，不拿它覆盖 S→C 响应字段。历史 PacketStruct/ShadowChallenge 只用于 m_ 字段名参考，不覆盖新版本 dump。");
    }

    private static void AddDeclaredAndDumpTail(ParsedFieldNode root, byte[] data, FrameView frame, int cursor)
    {
        if (cursor < frame.DeclaredEnd)
        {
            root.AddChild(Node(cursor, frame.DeclaredEnd - cursor, "raw", "UnparsedBytesBeforePacketBodyEnd", RawSummary(data, cursor, frame.DeclaredEnd - cursor), "UnknownName",
                $"当前 parser 结束于 0x{cursor:X}，长度前缀结束于 0x{frame.DeclaredEnd:X}；保留不强行命名"));
        }
        else if (cursor > frame.DeclaredEnd)
        {
            root.AddChild(Node(frame.DeclaredEnd, 0, "error", "ParserOverranPacketBodyEnd", $"cursor=0x{cursor:X}", "Unaligned",
                "当前字段解释超过长度前缀边界"));
        }

        if (frame.DeclaredEnd < data.Length)
        {
            root.AddChild(Node(frame.DeclaredEnd, data.Length - frame.DeclaredEnd, "raw", "DumpTrailingBytesAfterPacketBodyLength", RawSummary(data, frame.DeclaredEnd, data.Length - frame.DeclaredEnd), "Unaligned",
                "dump 文件长度大于 Network length-prefix；pcaps7 的同类 S2C 28421 body_len=81，当前 len98 的后 17 字节不属于 ResponseReplayOperate wire 结构，不填成正式字段"));
        }
    }

    private static FrameView DetectFrame(byte[] data)
    {
        if (data.Length >= 2)
        {
            var prefix = U16(data, 0);
            if (prefix >= 16 && prefix <= data.Length)
            {
                return new FrameView(true, 2, prefix);
            }
        }

        return new FrameView(false, 0, data.Length);
    }

    private static bool LooksClientToServer(string direction)
    {
        return direction.Contains("serialize", StringComparison.OrdinalIgnoreCase) ||
               direction.Contains("pack_", StringComparison.OrdinalIgnoreCase) ||
               direction.Contains("safe28346_builder", StringComparison.OrdinalIgnoreCase);
    }

    private static bool CanRead(byte[] data, int offset, int size)
    {
        return offset >= 0 && size >= 0 && offset + size <= data.Length;
    }

    private static byte U8(byte[] data, int offset) => data[offset];

    private static ushort U16(byte[] data, int offset) => BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(offset, 2));

    private static short I16(byte[] data, int offset) => BinaryPrimitives.ReadInt16BigEndian(data.AsSpan(offset, 2));

    private static uint U32(byte[] data, int offset) => BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(offset, 4));

    private static int I32(byte[] data, int offset) => BinaryPrimitives.ReadInt32BigEndian(data.AsSpan(offset, 4));

    private static bool IsLikelyUnixTime(uint value)
    {
        return value is > 1_500_000_000 and < 2_100_000_000;
    }

    private static int CountNonZero(byte[] data, int offset, int length)
    {
        var count = 0;
        var safe = Math.Min(length, data.Length - offset);
        for (var i = 0; i < safe; i++)
        {
            if (data[offset + i] != 0)
            {
                count++;
            }
        }

        return count;
    }

    private static string DecodeFixed(ReadOnlySpan<byte> raw)
    {
        var end = raw.IndexOf((byte)0);
        if (end >= 0)
        {
            raw = raw[..end];
        }

        return raw.Length == 0 ? "" : DecodeText(raw);
    }

    private static string DecodeText(ReadOnlySpan<byte> raw)
    {
        if (raw.Length == 0)
        {
            return "";
        }

        try
        {
            return new UTF8Encoding(false, true).GetString(raw);
        }
        catch (DecoderFallbackException)
        {
            return Encoding.GetEncoding("GB18030").GetString(raw);
        }
    }

    private static string RawSummary(byte[] data, int offset, int length)
    {
        if (offset < 0 || offset >= data.Length || length <= 0)
        {
            return "";
        }

        var safe = Math.Min(length, data.Length - offset);
        var previewLen = Math.Min(safe, 64);
        var hex = Convert.ToHexString(data, offset, previewLen);
        var suffix = safe > previewLen ? "..." : "";
        var ascii = ToAsciiPreview(data, offset, previewLen);
        return $"len={safe}; nonzero={CountNonZero(data, offset, safe)}; head={hex}{suffix}; ascii={ascii}";
    }

    private static string ToAsciiPreview(byte[] data, int offset, int length)
    {
        var chars = new char[length];
        for (var i = 0; i < length; i++)
        {
            var b = data[offset + i];
            chars[i] = b is >= 0x20 and <= 0x7E ? (char)b : '.';
        }

        return new string(chars);
    }

    private static ParsedFieldNode Node(long offset, int size, string type, string name, string value, string status, string note)
    {
        return new ParsedFieldNode
        {
            Offset = offset,
            Size = size,
            Type = type,
            Name = name,
            Value = value,
            Status = status,
            Note = note
        };
    }

    private static ParserStatus Unsupported(PacketRecord packet, string note)
    {
        return new ParserStatus
        {
            Status = "unsupported",
            Note = note,
            StructureText = "Not a MsgID 28421 payload.",
            RootNodes =
            [
                Node(0, 0, "parser", "UnsupportedMsg28421", $"MsgID {packet.MsgIdDisplay}", "not connected", note)
            ]
        };
    }

    private sealed record FrameView(bool HasLengthPrefix, int PayloadStart, int DeclaredEnd);
}
