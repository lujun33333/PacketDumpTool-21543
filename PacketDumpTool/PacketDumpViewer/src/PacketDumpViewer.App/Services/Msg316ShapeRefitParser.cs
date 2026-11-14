using System.Buffers.Binary;
using System.Globalization;
using System.IO;
using System.Text;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class Msg316ShapeRefitParser
{
    private const int MaxReasonLength = 200;
    private const int MaxRefitItemCount = 4;

    static Msg316ShapeRefitParser()
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
    }

    public ParserStatus Parse(PacketRecord packet)
    {
        if (packet.MsgId != 316)
        {
            return Unsupported(packet, "不是 MsgID 316。");
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
        if (LooksClientToServer(packet.Direction) && TryParseRequest(data, packet, out var preferredRequestStatus))
        {
            return preferredRequestStatus;
        }

        if (TryParseResponse(data, packet, out var responseStatus))
        {
            return responseStatus;
        }

        if (TryParseRequest(data, packet, out var requestStatus))
        {
            return requestStatus;
        }

        return new ParserStatus
        {
            Status = "unsupported",
            Note = "MsgID 316 未能按 CResponseSaveShapeRefitPara 或 RequestSaveShapeRefitNew 对齐。",
            StructureText = "Unsupported Msg316 shape.",
            RootNodes =
            [
                Node(0, data.Length, "raw", "UnsupportedMsg316Payload", $"{data.Length:N0} bytes", "Unaligned",
                    "当前支持：官服 CResponseSaveShapeRefitPara 响应、21273 RequestSaveShapeRefitNew 请求；未对齐区域请看 Hex View")
            ]
        };
    }

    private static bool TryParseResponse(byte[] data, PacketRecord packet, out ParserStatus status)
    {
        status = default!;
        if (data.Length < 2 + 2 + 1 + 4)
        {
            return false;
        }

        var start = 0;
        int? declaredEnd = null;
        var prefix = U16(data, 0);
        if (prefix >= 16 && prefix <= data.Length)
        {
            start = 2;
            declaredEnd = prefix;
        }

        var cursor = start;
        if (!CanRead(data, cursor, 2 + 1 + 4))
        {
            return false;
        }

        var resultId = I16(data, cursor);
        var reasonLen = U8(data, cursor + 2);
        if (reasonLen > MaxReasonLength || !CanRead(data, cursor + 3, reasonLen + 4))
        {
            return false;
        }

        var response = Node(
            0,
            data.Length,
            "struct",
            "Msg316ShapeRefit",
            $"{data.Length:N0} bytes",
            "Verified",
            "按官服 CResponseSaveShapeRefitPara / 21273 RequestSaveShapeRefitNew 双形态解析；字段名来自反编译与 luc 消费点");

        response.AddChild(Node(0, 0, "note", "Evidence", "CResponseSaveShapeRefitPara", "Verified",
            @"F:\new1\dll\new\21273\ida_export_Network_20260613_125530\strings_redacted.csv: CResponseSaveShapeRefitPara + m_stRefitClientInfo fields; F:\new1\2\re\out\Network.FULL.topic_crypto.c: FUN_18017d580 print order; luc KartAvatarRefitProxy consumes m_shResultID/m_szReason/m_iKartItemID/m_iShapeSuitID"));

        if (declaredEnd is not null)
        {
            response.AddChild(Node(0, 2, "be:u16", "PacketBodyLengthPrefix", declaredEnd.Value.ToString(CultureInfo.InvariantCulture), "Verified",
                $"dump firstBody16={declaredEnd.Value}; class payload begins at 0x{start:X}"));
        }

        var root = response.AddChild(Node(start, 0, "struct", "CResponseSaveShapeRefitPara", "", "Verified",
            "S→C 响应；316 在当前 dump 中是保存赛车外形改装/皮肤响应，不按旧 RequestSaveShapeRefitNew 请求强套"));

        root.AddChild(Node(cursor, 2, "be:i16", "m_shResultID", resultId.ToString(CultureInfo.InvariantCulture), "Verified", ""));
        cursor += 2;
        root.AddChild(Node(cursor, 1, "u8", "m_uchReasonLen", reasonLen.ToString(CultureInfo.InvariantCulture), "Verified", ""));
        cursor += 1;
        root.AddChild(Node(cursor, reasonLen, "bytes/string", "m_szReason", DecodeText(data.AsSpan(cursor, reasonLen)), "Dynamic",
            reasonLen == 0 ? "ReasonLen=0" : "长度由 m_uchReasonLen 控制"));
        cursor += reasonLen;

        root.AddChild(Node(cursor, 4, "be:i32", "m_iKartItemID", ItemValue(I32(data, cursor)), "Dynamic", "luc 响应回调会读取 Param.m_iKartItemID"));
        cursor += 4;

        if (!TryAddKartRefitExInfo(root, data, ref cursor, "m_stKartRefitExInfo"))
        {
            return false;
        }

        if (!CanRead(data, cursor, 1))
        {
            return false;
        }

        var refitItemNumOffset = cursor;
        var refitItemNum = U8(data, cursor++);
        var countToParse = Math.Min((int)refitItemNum, MaxRefitItemCount);
        var refitArraySize = 1 + countToParse * 4;
        var refitArray = root.AddChild(Node(refitItemNumOffset, refitArraySize, "array", "m_aiRefitItemID", $"count={refitItemNum}", refitItemNum <= MaxRefitItemCount ? "Verified" : "Unaligned",
            refitItemNum <= MaxRefitItemCount ? "count from m_ucRefitItemNum" : "官方结构固定最多 4 个外改部位；超过 4 不继续扩展"));
        refitArray.AddChild(Node(refitItemNumOffset, 1, "u8", "m_ucRefitItemNum", refitItemNum.ToString(CultureInfo.InvariantCulture), "Verified", ""));
        for (var i = 0; i < countToParse; i++)
        {
            if (!CanRead(data, cursor, 4))
            {
                return false;
            }

            refitArray.AddChild(Node(cursor, 4, "be:i32", $"m_aiRefitItemID[{i}]", ItemValue(I32(data, cursor)), "Dynamic", ""));
            cursor += 4;
        }

        if (refitItemNum > MaxRefitItemCount)
        {
            return false;
        }

        if (!CanRead(data, cursor, 4))
        {
            return false;
        }

        root.AddChild(Node(cursor, 4, "be:i32", "m_iShapeSuitID", ItemValue(I32(data, cursor)), "Dynamic", "luc TrailBagAction.OnResponseSaveShapeRefit 读取 Param.m_iShapeSuitID"));
        cursor += 4;

        if (!TryAddRefitClientInfo(root, data, ref cursor))
        {
            return false;
        }

        if (declaredEnd is not null && cursor != declaredEnd.Value)
        {
            root.AddChild(Node(cursor, Math.Max(0, declaredEnd.Value - cursor), "raw", "UnalignedBeforeDeclaredEnd", $"{declaredEnd.Value - cursor} bytes", "Unaligned",
                $"当前结构结束 0x{cursor:X}，长度前缀结束 0x{declaredEnd.Value:X}；保留原始字节"));
        }

        if (declaredEnd is not null && declaredEnd.Value < data.Length)
        {
            response.AddChild(Node(declaredEnd.Value, data.Length - declaredEnd.Value, "raw", "TrailingBytesAfterBodyLen", Hex(data, declaredEnd.Value, data.Length - declaredEnd.Value), "Unaligned",
                "dump 捕获长度大于第一字段长度前缀；未被 CResponseSaveShapeRefitPara 消费，不强行命名"));
        }
        else if (cursor < data.Length)
        {
            response.AddChild(Node(cursor, data.Length - cursor, "raw", "TrailingBytesAfterResponse", Hex(data, cursor, data.Length - cursor), "Unaligned",
                "响应字段已结束，剩余字节未在反编译/luc 中确认"));
        }

        status = new ParserStatus
        {
            Status = "parsed",
            Note = $"MsgID 316 已按 CResponseSaveShapeRefitPara 响应解析；direction={packet.Direction}。",
            StructureText = "Msg316 ShapeRefit response structure tree loaded.",
            RootNodes = [response]
        };
        return true;
    }

    private static bool TryParseRequest(byte[] data, PacketRecord packet, out ParserStatus status)
    {
        status = default!;
        foreach (var start in CandidateStarts(data))
        {
            var cursor = start;
            if (!CanRead(data, cursor, 4 + 4 + 4 + 1))
            {
                continue;
            }

            var root = Node(
                0,
                data.Length,
                "struct",
                "Msg316ShapeRefit",
                $"{data.Length:N0} bytes",
                "Verified",
                "按 21273 Network.dll RequestSaveShapeRefitNew 写入顺序解析 C→S 请求");

            root.AddChild(Node(0, 0, "note", "Evidence", "RequestSaveShapeRefitNew", "Verified",
                @"F:\new1\dll\new\21273\ida_export_Network_20260613_125530\pseudoc\00159420_sub_180159420.c writes 3*u32 + u8 count + u32[count<=4] + u32; qqspeed_decoder.messages.py names the same request layout as Uin/Time/KartItemID/RefitItemNum/RefitItemID/ShapeSuitID"));

            if (start == 2)
            {
                root.AddChild(Node(0, 2, "be:u16", "PacketBodyLengthPrefix", U16(data, 0).ToString(CultureInfo.InvariantCulture), "Verified", ""));
            }

            var req = root.AddChild(Node(start, 0, "struct", "RequestSaveShapeRefitNew", "", "Verified", "C→S 请求/发送"));
            req.AddChild(Node(cursor, 4, "be:u32", "m_uiUin", U32(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
            cursor += 4;
            req.AddChild(Node(cursor, 4, "be:u32", "m_uiTime", U32(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
            cursor += 4;
            req.AddChild(Node(cursor, 4, "be:i32", "m_iKartItemID", ItemValue(I32(data, cursor)), "Dynamic", ""));
            cursor += 4;

            var refitNum = U8(data, cursor++);
            if (refitNum > MaxRefitItemCount || !CanRead(data, cursor, refitNum * 4 + 4))
            {
                continue;
            }

            req.AddChild(Node(cursor - 1, 1, "u8", "m_ucRefitItemNum", refitNum.ToString(CultureInfo.InvariantCulture), "Verified", ""));
            var arr = req.AddChild(Node(cursor, refitNum * 4, "array", "m_aiRefitItemID", $"count={refitNum}", "Verified", ""));
            for (var i = 0; i < refitNum; i++)
            {
                arr.AddChild(Node(cursor, 4, "be:i32", $"m_aiRefitItemID[{i}]", ItemValue(I32(data, cursor)), "Dynamic", ""));
                cursor += 4;
            }

            req.AddChild(Node(cursor, 4, "be:i32", "m_iShapeSuitID", ItemValue(I32(data, cursor)), "Dynamic", ""));
            cursor += 4;

            if (cursor < data.Length)
            {
                root.AddChild(Node(cursor, data.Length - cursor, "raw", "TrailingBytesAfterRequest", Hex(data, cursor, data.Length - cursor), "Unaligned",
                    "请求字段已结束，剩余字节未在 21273 写入顺序中确认"));
            }

            status = new ParserStatus
            {
                Status = "parsed",
                Note = $"MsgID 316 已按 RequestSaveShapeRefitNew 请求解析；direction={packet.Direction}。",
                StructureText = "Msg316 ShapeRefit request structure tree loaded.",
                RootNodes = [root]
            };
            return true;
        }

        return false;
    }

    private static bool TryAddRefitClientInfo(ParsedFieldNode parent, byte[] data, ref int cursor)
    {
        if (!CanRead(data, cursor, 2))
        {
            return false;
        }

        var start = cursor;
        var len = U16(data, cursor);
        if (len < 2 || !CanRead(data, start, len))
        {
            return false;
        }

        var end = start + len;
        var node = parent.AddChild(Node(start, len, "struct", "m_stRefitClientInfo", $"len={len}", "Verified",
            "字段名来自 CResponseSaveShapeRefitPara 反编译 Print 函数"));
        cursor += 2;

        AddU32(node, data, ref cursor, "m_uiUin", unsigned: true);
        AddI32(node, data, ref cursor, "m_iKartId", itemValue: true);
        AddI32(node, data, ref cursor, "m_iRefitCout");
        AddI16(node, data, ref cursor, "m_shMaxFlags");
        AddI16(node, data, ref cursor, "m_shWWeight");
        AddI16(node, data, ref cursor, "m_shSpeedWeight");
        AddI16(node, data, ref cursor, "m_shJetWeight");
        AddI16(node, data, ref cursor, "m_shSJetWeight");
        AddI16(node, data, ref cursor, "m_shAccuWeight");
        AddI32(node, data, ref cursor, "m_iShapeRefitCount");
        AddI32(node, data, ref cursor, "m_iKartHeadRefitItemID", itemValue: true);
        AddI32(node, data, ref cursor, "m_iKartTailRefitItemID", itemValue: true);
        AddI32(node, data, ref cursor, "m_iKartFlankRefitItemID", itemValue: true);
        AddI32(node, data, ref cursor, "m_iKartTireRefitItemID", itemValue: true);

        if (!TryAddKartRefitExInfo(node, data, ref cursor, "m_stRefitClientInfo.m_stKartRefitExInfo"))
        {
            return false;
        }

        AddI32(node, data, ref cursor, "m_iSecondRefitCount");
        AddI16(node, data, ref cursor, "m_shSpeed2Weight");
        AddI16(node, data, ref cursor, "m_shDriftVecWeight");
        AddI16(node, data, ref cursor, "m_shAdditionalZSpeedWeight");
        AddI16(node, data, ref cursor, "m_shAntiCollisionWeight");
        AddI16(node, data, ref cursor, "m_shLuckyValue");
        AddI16(node, data, ref cursor, "m_shRefitLuckyValueMaxWeight");
        AddI32(node, data, ref cursor, "m_iShapeSuitID", itemValue: true);
        AddU8(node, data, ref cursor, "m_ucLegendSuitLevel");
        AddI32(node, data, ref cursor, "m_iLegendSuitLevelChoice");
        AddI32(node, data, ref cursor, "m_iShapeLegendSuitID", itemValue: true);
        AddU8(node, data, ref cursor, "m_ucShapeOriginLevel");

        if (!CanRead(data, cursor, 8))
        {
            return false;
        }

        node.AddChild(Node(cursor, 8, "be:u64", "m_uRoleID", U64(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic",
            "反编译 Print 使用 %llu；旧私服源码曾写两个 u32，这里按官服字段合并为 u64"));
        cursor += 8;

        if (cursor < end)
        {
            node.AddChild(Node(cursor, end - cursor, "raw", "UnknownName_RefitClientInfoTail", Hex(data, cursor, end - cursor), "UnknownName",
                "长度块内剩余字节未在反编译 Print/luc 消费点确认"));
            cursor = end;
        }

        return cursor == end;
    }

    private static bool TryAddKartRefitExInfo(ParsedFieldNode parent, byte[] data, ref int cursor, string name)
    {
        if (!CanRead(data, cursor, 10))
        {
            return false;
        }

        var start = cursor;
        var len = U16(data, cursor);
        if (len != 10 || !CanRead(data, start, len))
        {
            return false;
        }

        var node = parent.AddChild(Node(start, len, "struct", name, $"len={len}", "Verified",
            "8 个 u8 字段：Speed/Jet/SJet/Accu Star + AddRatio"));
        cursor += 2;
        AddU8(node, data, ref cursor, "m_ucSpeedRefitStar");
        AddU8(node, data, ref cursor, "m_ucJetRefitStar");
        AddU8(node, data, ref cursor, "m_ucSJetRefitStar");
        AddU8(node, data, ref cursor, "m_ucAccuRefitStar");
        AddU8(node, data, ref cursor, "m_ucSpeedAddRatio");
        AddU8(node, data, ref cursor, "m_ucJetAddRatio");
        AddU8(node, data, ref cursor, "m_ucSJetAddRatio");
        AddU8(node, data, ref cursor, "m_ucAccuAddRatio");
        return cursor == start + len;
    }

    private static IEnumerable<int> CandidateStarts(byte[] data)
    {
        yield return 0;
        if (data.Length >= 2)
        {
            var prefix = U16(data, 0);
            if (prefix >= 16 && prefix <= data.Length)
            {
                yield return 2;
            }
        }
    }

    private static bool LooksClientToServer(string direction)
    {
        return direction.Contains("serialize", StringComparison.OrdinalIgnoreCase) ||
               direction.Contains("pack_", StringComparison.OrdinalIgnoreCase) ||
               direction.Contains("safe28346_builder", StringComparison.OrdinalIgnoreCase);
    }

    private static void AddU8(ParsedFieldNode parent, byte[] data, ref int cursor, string name)
    {
        parent.AddChild(Node(cursor, 1, "u8", name, U8(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        cursor += 1;
    }

    private static void AddI16(ParsedFieldNode parent, byte[] data, ref int cursor, string name)
    {
        parent.AddChild(Node(cursor, 2, "be:i16", name, I16(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        cursor += 2;
    }

    private static void AddI32(ParsedFieldNode parent, byte[] data, ref int cursor, string name, bool itemValue = false)
    {
        var value = I32(data, cursor);
        parent.AddChild(Node(cursor, 4, "be:i32", name, itemValue ? ItemValue(value) : value.ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        cursor += 4;
    }

    private static void AddU32(ParsedFieldNode parent, byte[] data, ref int cursor, string name, bool unsigned = false)
    {
        var value = U32(data, cursor);
        parent.AddChild(Node(cursor, 4, "be:u32", name, unsigned ? value.ToString(CultureInfo.InvariantCulture) : ItemValue(unchecked((int)value)), "Dynamic", ""));
        cursor += 4;
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

    private static ulong U64(byte[] data, int offset) => BinaryPrimitives.ReadUInt64BigEndian(data.AsSpan(offset, 8));

    private static string ItemValue(int itemId)
    {
        if (itemId <= 0)
        {
            return itemId.ToString(CultureInfo.InvariantCulture);
        }

        var name = ProtocolNameService.Shared.DescribeItemId(unchecked((uint)itemId));
        return name == "UnknownItem"
            ? itemId.ToString(CultureInfo.InvariantCulture)
            : $"{itemId} / {name}";
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

    private static string Hex(byte[] data, int offset, int length)
    {
        if (offset < 0 || offset >= data.Length || length <= 0)
        {
            return "";
        }

        var safe = Math.Min(length, data.Length - offset);
        return Convert.ToHexString(data, offset, safe);
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
            StructureText = "Not a MsgID 316 payload.",
            RootNodes =
            [
                Node(0, 0, "parser", "UnsupportedMsg316", $"MsgID {packet.MsgIdDisplay}", "not connected", note)
            ]
        };
    }
}
