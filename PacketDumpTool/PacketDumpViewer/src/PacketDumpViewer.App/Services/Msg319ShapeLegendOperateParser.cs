using System.Buffers.Binary;
using System.Globalization;
using System.IO;
using System.Text;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class Msg319ShapeLegendOperateParser
{
    private const int MaxReasonLength = 200;
    private const int MaxShapeInfoCount = 64;
    private const int MaxRefitClientInfoCount = 4;

    static Msg319ShapeLegendOperateParser()
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
    }

    public ParserStatus Parse(PacketRecord packet)
    {
        if (packet.MsgId != 319)
        {
            return Unsupported(packet, "不是 MsgID 319。");
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
            Note = $"MsgID 319 未能按 RequestShapeLegendOperatePara / CResponseShapeLegendOperatePara 对齐；direction={packet.Direction}。",
            StructureText = "Unsupported Msg319 payload.",
            RootNodes =
            [
                Node(0, data.Length, "raw", "UnsupportedMsg319Payload", $"{data.Length:N0} bytes", "Unaligned",
                    "没有足够证据按官服字段名展开；请看 Hex View 和同级 dump 对照")
            ]
        };
    }

    private static bool TryParseResponse(byte[] data, FrameView frame, PacketRecord packet, out ParserStatus status)
    {
        status = default!;
        var cursor = frame.PayloadStart;
        if (!CanRead(data, cursor, 2 + 1 + 1 + 2))
        {
            return false;
        }

        var resultId = U16(data, cursor);
        var reasonLen = U8(data, cursor + 2);
        if (reasonLen > MaxReasonLength || !CanRead(data, cursor + 3, reasonLen + 1 + 2))
        {
            return false;
        }

        var shapeNumOffset = cursor + 3 + reasonLen + 1;
        var shapeNum = U16(data, shapeNumOffset);
        if (shapeNum > MaxShapeInfoCount)
        {
            return false;
        }

        var root = RootNode(data, frame, "CResponseShapeLegendOperatePara");
        root.AddChild(EvidenceNode());
        AddPrefixNode(root, frame);

        var response = root.AddChild(Node(frame.PayloadStart, Math.Max(0, frame.DeclaredEnd - frame.PayloadStart), "struct", "CResponseShapeLegendOperatePara", "", "Verified",
            "S→C 响应/通知；当前 packet_dumps_* 可复现 0x001A/0x0025 length-prefix；字段名来自 21273 Network/Top-Kart 字符串与 Top-Kart 消费点"));

        response.AddChild(Node(cursor, 2, "be:u16", "m_usResultID", resultId.ToString(CultureInfo.InvariantCulture), "Verified",
            "Top-Kart/Network 输出名 m_usResultID/m_shResultID；当前样本为 0"));
        cursor += 2;

        response.AddChild(Node(cursor, 1, "u8", "m_uchReasonLen", reasonLen.ToString(CultureInfo.InvariantCulture), "Verified",
            "wire 中的 Reason 长度；expanded object 中 m_szReason 位于 Result 后"));
        cursor += 1;

        response.AddChild(Node(cursor, reasonLen, "bytes/string", "m_szReason", DecodeText(data.AsSpan(cursor, reasonLen)), reasonLen == 0 ? "Reserved" : "Dynamic",
            reasonLen == 0 ? "ReasonLen=0" : "长度由 m_uchReasonLen 控制"));
        cursor += reasonLen;

        var opType = U8(data, cursor);
        response.AddChild(Node(cursor, 1, "u8", "m_ucOpType", $"{opType} / {OpTypeName(opType)}", "Verified",
            "Top-Kart sub_180D3E860 读取 expanded object a2+203；Lua 回调字段名 m_ucOpType"));
        cursor += 1;

        response.AddChild(Node(cursor, 2, "be:u16", "m_usShapeNum", shapeNum.ToString(CultureInfo.InvariantCulture), "Verified",
            "Top-Kart sub_180D3E860 读取 expanded object a2+204，并按 m_astShapeInfo 循环"));
        cursor += 2;

        var shapeArrayStart = cursor;
        var shapeArray = response.AddChild(Node(shapeArrayStart, 0, "array", "m_astShapeInfo", $"count={shapeNum}", "Verified",
            "每个元素为 length-prefixed ShapeInfo；21273 字符串确认 m_iShapeID/m_ucShapeLevel/m_iShowChoice"));
        for (var i = 0; i < shapeNum; i++)
        {
            if (!TryAddShapeInfo(shapeArray, data, ref cursor, frame.DeclaredEnd, i))
            {
                return false;
            }
        }

        shapeArray.Value = $"count={shapeNum}; bytes={cursor - shapeArrayStart}";

        if (!CanRead(data, cursor, 4 + 1 + 1 + 4 + 4 + 4) || cursor + 18 > frame.DeclaredEnd)
        {
            return false;
        }

        response.AddChild(Node(cursor, 4, "be:i32", "m_iShapeID", ItemValue(I32(data, cursor)), "Dynamic",
            "响应主 ShapeID；Top-Kart Lua 输出字段名 m_iShapeID"));
        cursor += 4;

        response.AddChild(Node(cursor, 1, "u8", "m_ucShapeLevel", U8(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic",
            "响应主 ShapeLevel；Top-Kart Lua 输出字段名 m_ucShapeLevel"));
        cursor += 1;

        var hasRefitOffset = cursor;
        var hasRefit = U8(data, cursor);
        response.AddChild(Node(cursor, 1, "u8", "m_ucHasRefit", hasRefit.ToString(CultureInfo.InvariantCulture), hasRefit == 0 ? "Verified" : "Dynamic",
            "Network 字符串确认字段名；当前所有 Msg319 dump 均为 0，非 0 时后接 m_astRefitClientInfo 长度块"));
        cursor += 1;

        var minTail = 4 + 4 + 4;
        if (hasRefit > 0)
        {
            if (hasRefit > MaxRefitClientInfoCount)
            {
                response.AddChild(Node(hasRefitOffset, 1, "u8", "m_ucHasRefit_OutOfRange", hasRefit.ToString(CultureInfo.InvariantCulture), "Unaligned",
                    "当前 parser 最多展开 4 个 m_astRefitClientInfo；超过范围保留原始字节"));
                return false;
            }

            var refitArrayStart = cursor;
            var refitArray = response.AddChild(Node(refitArrayStart, 0, "array", "m_astRefitClientInfo", $"count={hasRefit}", "Dynamic",
                "当前 dump 未出现非 0 样本；按 316/319 共同的 KartRefitClientInfo 长度块 guarded 展开"));
            for (var i = 0; i < hasRefit; i++)
            {
                if (cursor + minTail >= frame.DeclaredEnd || !TryAddRefitClientInfo(refitArray, data, ref cursor, frame.DeclaredEnd, i))
                {
                    return false;
                }
            }

            refitArray.Value = $"count={hasRefit}; bytes={cursor - refitArrayStart}";
        }

        if (!CanRead(data, cursor, minTail) || cursor + minTail > frame.DeclaredEnd)
        {
            return false;
        }

        response.AddChild(Node(cursor, 4, "be:i32", "m_iNewShowChoice", I32(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic",
            "Network 字符串确认 m_iNewShowChoice；Top-Kart cmd=choose 路径读取 expanded object 对应字段"));
        cursor += 4;

        response.AddChild(Node(cursor, 4, "be:i32", "m_iDirectMaxLevelShapeID", ItemValue(I32(data, cursor)), "Dynamic",
            "Top-Kart Lua 输出字段名 m_iDirectMaxLevelShapeID；当前样本为 0"));
        cursor += 4;

        response.AddChild(Node(cursor, 4, "be:i32", "m_iDirectMaxShapePreLevel", I32(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic",
            "Top-Kart Lua 输出字段名 m_iDirectMaxShapePreLevel；当前样本为 0"));
        cursor += 4;

        AddDeclaredAndDumpTail(root, data, frame, cursor);

        status = new ParserStatus
        {
            Status = "parsed",
            Note = $"MsgID 319 已按 CResponseShapeLegendOperatePara 解析；direction={packet.Direction}；有效 wire 区间到 0x{frame.DeclaredEnd:X}。",
            StructureText = "Msg319 ShapeLegend response structure tree loaded.",
            RootNodes = [root]
        };
        return true;
    }

    private static bool TryParseRequest(byte[] data, FrameView frame, PacketRecord packet, out ParserStatus status)
    {
        status = default!;
        var cursor = frame.PayloadStart;
        if (!CanRead(data, cursor, 4 + 4 + 1 + 4 + 1 + 4))
        {
            return false;
        }

        var root = RootNode(data, frame, "RequestShapeLegendOperatePara");
        root.AddChild(EvidenceNode());
        AddPrefixNode(root, frame);

        var request = root.AddChild(Node(frame.PayloadStart, Math.Max(0, frame.DeclaredEnd - frame.PayloadStart), "struct", "RequestShapeLegendOperatePara", "", "Verified",
            "C→S 请求；21273 Network sub_1801596B0 写入 u32/u32/u8/u32/u8/u32，Top-Kart sub_180D3E7E0 由 Lua RequestShapeLegendOperatePara 构造"));

        request.AddChild(Node(cursor, 4, "be:u32", "m_uiUin", U32(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        cursor += 4;

        request.AddChild(Node(cursor, 4, "be:u32", "m_uiTime", U32(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic",
            "Top-Kart 请求侧填 GetTickCount/时间参数；不是 Unix 秒时不强行解释"));
        cursor += 4;

        var opType = U8(data, cursor);
        request.AddChild(Node(cursor, 1, "u8", "m_ucOpType", $"{opType} / {OpTypeName(opType)}", "Verified", ""));
        cursor += 1;

        request.AddChild(Node(cursor, 4, "be:i32", "m_iShapeID", ItemValue(I32(data, cursor)), "Dynamic", ""));
        cursor += 4;

        request.AddChild(Node(cursor, 1, "u8", "m_ucShapeLevel", U8(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        cursor += 1;

        request.AddChild(Node(cursor, 4, "be:i32", "m_iNewShowChoice", I32(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        cursor += 4;

        AddDeclaredAndDumpTail(root, data, frame, cursor);

        status = new ParserStatus
        {
            Status = "parsed",
            Note = $"MsgID 319 已按 RequestShapeLegendOperatePara 解析；direction={packet.Direction}。",
            StructureText = "Msg319 ShapeLegend request structure tree loaded.",
            RootNodes = [root]
        };
        return true;
    }

    private static bool TryAddShapeInfo(ParsedFieldNode parent, byte[] data, ref int cursor, int declaredEnd, int index)
    {
        if (!CanRead(data, cursor, 2) || cursor + 2 > declaredEnd)
        {
            return false;
        }

        var start = cursor;
        var len = U16(data, cursor);
        if (len < 11 || start + len > declaredEnd || !CanRead(data, start, len))
        {
            return false;
        }

        var end = start + len;
        var node = parent.AddChild(Node(start, len, "struct", $"m_astShapeInfo[{index}]", $"len={len}", "Verified",
            "Top-Kart sub_180D3E860 expanded array stride 9；wire 中为 2B length + 9B fields"));
        cursor += 2;

        node.AddChild(Node(cursor, 4, "be:i32", "m_iShapeID", ItemValue(I32(data, cursor)), "Dynamic", ""));
        cursor += 4;
        node.AddChild(Node(cursor, 1, "u8", "m_ucShapeLevel", U8(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        cursor += 1;
        node.AddChild(Node(cursor, 4, "be:i32", "m_iShowChoice", I32(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        cursor += 4;

        if (cursor < end)
        {
            node.AddChild(Node(cursor, end - cursor, "raw", "UnknownName_ShapeInfoTail", Hex(data, cursor, end - cursor), "UnknownName",
                "ShapeInfo 长度块内剩余字节未在 21273 字符串/消费点确认"));
            cursor = end;
        }

        return cursor == end;
    }

    private static bool TryAddRefitClientInfo(ParsedFieldNode parent, byte[] data, ref int cursor, int declaredEnd, int index)
    {
        if (!CanRead(data, cursor, 2) || cursor + 2 > declaredEnd)
        {
            return false;
        }

        var start = cursor;
        var len = U16(data, cursor);
        if (len < 90 || start + len > declaredEnd || !CanRead(data, start, len))
        {
            return false;
        }

        var end = start + len;
        var node = parent.AddChild(Node(start, len, "struct", $"m_astRefitClientInfo[{index}]", $"len={len}", "Dynamic",
            "当前 Msg319 dump 未覆盖 HasRefit 非 0；按 21273 Network m_astRefitClientInfo 字符串和 Msg316 同款字段 guarded 展开"));
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

        if (!TryAddKartRefitExInfo(node, data, ref cursor, end, "m_stKartRefitExInfo"))
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

        if (!CanRead(data, cursor, 8) || cursor + 8 > end)
        {
            return false;
        }

        node.AddChild(Node(cursor, 8, "be:u64", "m_uRoleID", U64(data, cursor).ToString(CultureInfo.InvariantCulture), "Dynamic",
            "反编译 Print 使用 %llu；旧源码两个 u32 在这里按官服字段合并为 u64"));
        cursor += 8;

        if (cursor < end)
        {
            node.AddChild(Node(cursor, end - cursor, "raw", "UnknownName_RefitClientInfoTail", Hex(data, cursor, end - cursor), "UnknownName",
                "长度块内剩余字节未在 21273 Network/Top-Kart 字符串确认"));
            cursor = end;
        }

        return cursor == end;
    }

    private static bool TryAddKartRefitExInfo(ParsedFieldNode parent, byte[] data, ref int cursor, int end, string name)
    {
        if (!CanRead(data, cursor, 10) || cursor + 10 > end)
        {
            return false;
        }

        var start = cursor;
        var len = U16(data, cursor);
        if (len != 10 || start + len > end)
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

    private static ParsedFieldNode RootNode(byte[] data, FrameView frame, string variant)
    {
        return Node(0, data.Length, "struct", $"Msg319{variant}", $"{data.Length:N0} bytes", "Verified",
            $"319 = ShapeLegendOperate / 传奇皮肤操作；declaredEnd=0x{frame.DeclaredEnd:X}");
    }

    private static ParsedFieldNode EvidenceNode()
    {
        return Node(0, 0, "note", "Evidence", "21273 Network.dll/Top-Kart.dll + current dump", "Verified",
            @"Request: Network 21273 sub_1801596B0 writes u32/u32/u8/u32/u8/u32; Top-Kart sub_180D3E7E0 sends MsgID 0x13F. Response: Network strings confirm CResponseShapeLegendOperatePara, m_astShapeInfo, m_ucHasRefit, m_iNewShowChoice, m_iDirectMaxLevelShapeID, m_iDirectMaxShapePreLevel; Top-Kart sub_180D3E860 consumes expanded object offsets a2+203 OpType/a2+204 ShapeNum/a2+206 ShapeInfo/a2+782 ShapeID/a2+786 ShapeLevel/a2+787 HasRefit/a2+878 NewShowChoice. Current packet_dumps_* show len42/len50 files with firstBody16=0x001A/0x0025; bytes after that prefix are dump trailing bytes, not formal wire fields.");
    }

    private static void AddPrefixNode(ParsedFieldNode root, FrameView frame)
    {
        if (frame.HasLengthPrefix)
        {
            root.AddChild(Node(0, 2, "be:u16", "PacketBodyLengthPrefix", frame.DeclaredEnd.ToString(CultureInfo.InvariantCulture), "Verified",
                "Network length-prefix/declared end；结构体正文从 0x00000002 开始，后续超过该边界的 dump 字节不强行命名"));
        }
    }

    private static void AddDeclaredAndDumpTail(ParsedFieldNode root, byte[] data, FrameView frame, int cursor)
    {
        if (cursor < frame.DeclaredEnd)
        {
            root.AddChild(Node(cursor, frame.DeclaredEnd - cursor, "raw", "UnparsedBytesBeforePacketBodyEnd", Hex(data, cursor, frame.DeclaredEnd - cursor), "UnknownName",
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
                "dump 文件长度大于 Network length-prefix；当前 Msg319 len42/len50 的尾部不属于 CResponseShapeLegendOperatePara wire 结构，不填成正式字段"));
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

    private static string OpTypeName(byte opType)
    {
        return opType switch
        {
            0 => "ENSLOT_Check",
            1 => "ENSLOT_LevelUp",
            2 => "ENSLOT_Choose",
            _ => "UnknownOpType"
        };
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

    private static string Hex(byte[] data, int offset, int length)
    {
        if (offset < 0 || offset >= data.Length || length <= 0)
        {
            return "";
        }

        var safe = Math.Min(length, data.Length - offset);
        return Convert.ToHexString(data, offset, safe);
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
            StructureText = "Not a MsgID 319 payload.",
            RootNodes =
            [
                Node(0, 0, "parser", "UnsupportedMsg319", $"MsgID {packet.MsgIdDisplay}", "not connected", note)
            ]
        };
    }

    private sealed record FrameView(bool HasLengthPrefix, int PayloadStart, int DeclaredEnd);
}
