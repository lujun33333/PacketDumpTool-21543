using System.Buffers.Binary;
using System.Globalization;
using System.IO;
using System.Text;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class Msg24251QuickRpcParser
{
    private const int PlainFullLength = 0x7E10; // 32,272 bytes: QuickRPC plain projection with response para1.
    private const int PlainBodySliceLength = PlainFullLength - 0x0E; // dump body view = full[0x0E:].
    private const int PlainBodyLenOffset = 0x10A;
    private const int PlainBodyOffset = 0x10C;
    private const int PlainResponsePara1Offset = 0x7E0C;
    private const int MaxQuickRpcBody = 32_000;
    private const int MaxTlvDepth = 10;
    private const int MaxNodes = 4_000;

    private static readonly byte[] S1 =
    [
        0xd7, 0x56, 0x82, 0xdc, 0x83, 0x02, 0x8f, 0x29, 0x35, 0x04, 0x21, 0x71, 0x79, 0x9e, 0x92, 0x7f,
        0xcb, 0x97, 0x6a, 0x51, 0x05, 0xc7, 0x6f, 0xe6, 0x40, 0x63, 0x7e, 0x34, 0x5b, 0x47, 0x07, 0x78,
        0x5a, 0x96, 0xb8, 0xb9, 0x2c, 0x99, 0x5e, 0x6e, 0xd1, 0x75, 0x41, 0x61, 0x24, 0x5f, 0x4a, 0xaa,
        0x4b, 0xcf, 0x0e, 0xd4, 0x86, 0x5d, 0xba, 0x1d, 0x3f, 0x2b, 0xdf, 0x62, 0xf0, 0x33, 0x00, 0x55,
        0xca, 0xfc, 0x19, 0xac, 0xf3, 0x66, 0x23, 0x69, 0xbc, 0xeb, 0x46, 0xf8, 0x9c, 0x50, 0x87, 0x4d,
        0x6d, 0x10, 0x8e, 0x88, 0xbe, 0x1b, 0xb5, 0xda, 0x4e, 0x1a, 0x13, 0xcc, 0x22, 0x09, 0xad, 0xa4,
        0x9d, 0x30, 0xa6, 0xe5, 0x7d, 0xfa, 0xc9, 0x17, 0x12, 0xc2, 0xfd, 0xe1, 0xbb, 0xe7, 0x0b, 0x98,
        0xbf, 0xbd, 0x11, 0x37, 0xc0, 0x7c, 0xf7, 0x95, 0xb6, 0xdd, 0x49, 0xf4, 0x81, 0x2a, 0x9f, 0x1c,
        0xfb, 0x8d, 0x9a, 0x72, 0x7b, 0x57, 0x7a, 0x43, 0xb3, 0xa9, 0x53, 0xe4, 0x59, 0x20, 0x2f, 0xa8,
        0xf6, 0x74, 0x36, 0xa0, 0x85, 0xf1, 0xa7, 0x14, 0x70, 0x31, 0x84, 0x0c, 0xb2, 0xa5, 0xdb, 0xe8,
        0x16, 0xae, 0x3d, 0x25, 0xb1, 0xcd, 0x9b, 0x03, 0x67, 0x15, 0x5c, 0xea, 0x1f, 0x39, 0xa1, 0x44,
        0x0a, 0x8b, 0x76, 0xde, 0x60, 0x65, 0x93, 0xf2, 0x64, 0xd5, 0xc1, 0xc8, 0x4c, 0x06, 0x4f, 0xb7,
        0xed, 0xfe, 0xe0, 0xf9, 0xa2, 0x18, 0x48, 0x91, 0xce, 0x1e, 0x3c, 0xb4, 0x6c, 0x42, 0x54, 0x94,
        0xe3, 0x28, 0xe9, 0x01, 0x27, 0xec, 0x0d, 0x45, 0xff, 0x26, 0xef, 0xe2, 0x8a, 0xab, 0xd9, 0xf5,
        0x08, 0xc4, 0xaf, 0x32, 0xc5, 0x6b, 0x80, 0xc6, 0xc3, 0x58, 0xee, 0xa3, 0x3e, 0x2d, 0x0f, 0x89,
        0x3a, 0xb0, 0xd2, 0xd3, 0x38, 0x73, 0xd8, 0xd0, 0x8c, 0x77, 0x90, 0x52, 0x3b, 0xd6, 0x2e, 0x68,
    ];

    static Msg24251QuickRpcParser()
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
    }

    public ParserStatus Parse(PacketRecord packet)
    {
        if (packet.MsgId != 24251)
        {
            return Unsupported(packet, "不是 MsgID 24251。");
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
        var sourceName = packet.FileName;
        var sourceNote = "selected payload";

        if (IsBodySliceCandidate(packet, data) &&
            TryFindMatchingPlainFullSibling(packet.FullPath, data, out var fullData, out var fullName))
        {
            data = fullData;
            sourceName = fullName;
            sourceNote = "当前选择的是 body 切片；已匹配同目录 *_special28346_after_full_msg24251_len32272.bin 后按 full offset 解析";
        }

        if (TryParsePlainLayout(data, 0, sourceName, sourceNote, out var plainStatus))
        {
            return plainStatus;
        }

        if (IsBodySliceCandidate(packet, data) &&
            TryParsePlainLayout(data, 0x0E, sourceName, "未找到 full sibling；按 body slice 的 full offset=body offset+0x0E 降级解析，头部 0x00-0x0D 已丢失", out plainStatus))
        {
            return plainStatus;
        }

        if (TryParseEncodedQuickRpc(data, out var decoded, out var error))
        {
            return BuildEncodedStatus(decoded, packet.FileName, packet.Direction);
        }

        return new ParserStatus
        {
            Status = "unsupported",
            Note = $"MsgID 24251 未识别为 current QuickRPC raw m_acBuffer 或 plain projection：{error}",
            StructureText = "Unsupported Msg24251 shape.",
            RootNodes =
            [
                Node(0, data.Length, "raw", "UnsupportedMsg24251Payload", $"{data.Length:N0} bytes", "Unaligned",
                    "支持形态：raw m_acBuffer / safe28346_builder_inner_body_at12 / special28346_after_full plain / 可匹配 full 的 body slice")
            ]
        };
    }

    private static bool TryParsePlainLayout(byte[] data, int fullOffsetShift, string sourceName, string sourceNote, out ParserStatus status)
    {
        status = default!;
        var bodyLenFileOffset = PlainBodyLenOffset - fullOffsetShift;
        var bodyFileOffset = PlainBodyOffset - fullOffsetShift;

        if (bodyLenFileOffset < 0 || bodyFileOffset < 0 || bodyLenFileOffset + 2 > data.Length)
        {
            return false;
        }

        var bodyLen = U16(data, bodyLenFileOffset);
        if (bodyLen == 0 || bodyLen > MaxQuickRpcBody)
        {
            return false;
        }

        if (bodyFileOffset >= data.Length)
        {
            return false;
        }

        var availableBody = Math.Min(bodyLen, data.Length - bodyFileOffset);
        if (availableBody <= 0)
        {
            return false;
        }

        var body = data.AsSpan(bodyFileOffset, availableBody).ToArray();
        if (!TryDecodeQuickRpcBodyHeader(body, 0, availableBody, out var bodyHead))
        {
            return false;
        }

        var isTruncated = availableBody < bodyLen;
        var root = Node(
            0,
            data.Length,
            "struct",
            "Msg24251QuickRPCPlainFull",
            $"{data.Length:N0} bytes",
            isTruncated ? "Unaligned" : "Verified",
            $"current 0x5EBB CSSafe QuickRPC plain projection; source={sourceName}; {sourceNote}");

        root.AddChild(Node(
            0,
            0,
            "note",
            "Evidence",
            "CSSafeCRequestQuickRPCPara / CSSafeCResponseQuickRPCPara",
            "Verified",
            "F:\\new1\\qqspeed_decoder\\quickrpc.py: plain head/body offsets; F:\\new1\\dll\\new\\21273 IDA export registers 24251 to QuickRPC classes"));

        if (fullOffsetShift == 0)
        {
            AddPlainHead(root, data);
        }
        else
        {
            root.AddChild(Node(
                0,
                fullOffsetShift,
                "missing",
                "PlainHeadPrefixMissing",
                $"body slice removed first 0x{fullOffsetShift:X} bytes",
                "Unaligned",
                "建议选择 special28346_after_full；body slice 只能恢复 body_len/body.msg，不能恢复 m_uiUin/m_uiTime 等头字段"));
        }

        root.AddChild(Node(
            PlainBodyLenOffset,
            2,
            "le:u16",
            "m_stBody.m_ushBodyLen",
            bodyLen.ToString(CultureInfo.InvariantCulture),
            isTruncated ? "Unaligned" : "Verified",
            isTruncated ? $"declared body length {bodyLen}, file only has {availableBody} bytes from body offset" : "decoded body.msg byte count"));

        var bodyNode = root.AddChild(Node(
            PlainBodyOffset,
            availableBody,
            "bytes",
            "m_stBody.m_acBody",
            $"{bodyHead.HeaderName}; body_len={bodyLen}; available={availableBody}",
            isTruncated ? "Unaligned" : "Verified",
            "QuickRPC body.msg: method/header name + TLV stream"));

        AddBodyHeaderAndTlv(bodyNode, body, PlainBodyOffset, bodyHead, keyIndexBase: 0, isTruncated);

        if (fullOffsetShift == 0 && data.Length >= PlainResponsePara1Offset + 4)
        {
            root.AddChild(Node(
                PlainResponsePara1Offset,
                4,
                "le:u32",
                "m_uiPara1",
                U32(data, PlainResponsePara1Offset).ToString(CultureInfo.InvariantCulture),
                "Dynamic",
                "response-only tail field; request/plain preview may leave it zero or absent"));
        }

        status = new ParserStatus
        {
            Status = isTruncated ? "partial" : "parsed",
            Note = isTruncated
                ? "MsgID 24251 plain projection 已部分解析；该 src_object preview 长度不足，完整 body 请看 *_inner_body_at12 或 after_full。"
                : "MsgID 24251 current QuickRPC plain projection 已解析。",
            StructureText = "Msg24251 QuickRPC structure tree loaded.",
            RootNodes = [root]
        };
        return true;
    }

    private static ParserStatus BuildEncodedStatus(QuickRpcDecoded decoded, string sourceName, string sourceDirection)
    {
        var root = Node(
            0,
            decoded.RawLen,
            "struct",
            "Msg24251QuickRPCRawBuffer",
            $"{decoded.RawLen:N0} bytes",
            decoded.Remaining == 0 ? "Verified" : "Dynamic",
            $"current raw m_acBuffer decoded from {sourceName}; offsets under DecodedPlainProjection are logical plain offsets");

        root.AddChild(Node(
            0,
            0,
            "note",
            "Evidence",
            "Network.dll QuickRPC body/head Deserialize transforms",
            "Verified",
            "byte transforms ported from F:\\new1\\qqspeed_decoder\\quickrpc.py and 21273 IDA pseudoc sub_18031ACE0/sub_18031AE20/sub_180309830"));

        var raw = root.AddChild(Group(0, decoded.RawLen, "RawEncodedEnvelope", "encoded varint/body/head fields in selected payload"));
        raw.AddChild(Node(0, decoded.BodyLenVarintSize, "encoded-varint", "m_stBody.m_ushBodyLen", decoded.BodyLen.ToString(CultureInfo.InvariantCulture), "Verified", "decoded by _dec_body_len_byte"));
        raw.AddChild(Node(decoded.BodyEncodedOffset, decoded.BodyLen, "encoded-bytes", "m_stBody.m_acBody", $"{decoded.BodyHead.HeaderName}; decoded_len={decoded.Body.Length}", "Verified", "each byte decoded by _dec_body_msg_byte"));
        raw.AddChild(Node(decoded.BodyExtraCountOffset, 1, "encoded-u8", "m_stBody.RandomExtraCount", decoded.BodyExtraCount.ToString(CultureInfo.InvariantCulture), "Dynamic", "randomized safe-vector tail, not business structure"));
        raw.AddChild(Node(decoded.HeadTimeOffset, decoded.HeadEncodedSize, "encoded", "m_stHead", $"uin={decoded.Uin}; time={decoded.Time}; version={decoded.Version}; compress={decoded.Compress}; optLen={decoded.Options.Length}", "Verified", "decoded QuickRPC head"));
        if (decoded.ResponseTail is { } rawTail)
        {
            raw.AddChild(Node(rawTail.TailOffset, rawTail.Consumed, "encoded", "ResponseTail", $"m_uiPara1={rawTail.Para1}; count={rawTail.CountText}", rawTail.IsRequestShaped ? "Dynamic" : "Verified", rawTail.Note));
        }

        if (decoded.Remaining > 0)
        {
            raw.AddChild(Node(decoded.Consumed, decoded.Remaining, "bytes", "EncodedTailRemaining", $"{decoded.Remaining} bytes", "Dynamic", "request-shaped/randomized tail not fully consumed by response tail parser"));
        }

        foreach (var warning in decoded.Warnings)
        {
            root.AddChild(Node(0, 0, "warning", "QuickRPCDecodeWarning", warning, "Dynamic", ""));
        }

        var plain = root.AddChild(Group(0, PlainResponsePara1Offset + 4, "DecodedPlainProjection", "logical plain offsets produced by CSSafe QuickRPC decode"));
        plain.AddChild(Node(0, 4, "le:u32", "m_stHead.m_uiUin", decoded.Uin.ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        plain.AddChild(Node(4, 4, "le:u32", "m_stHead.m_uiTime", decoded.Time.ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        plain.AddChild(Node(8, 1, "u8", "m_stHead.m_byVersion", decoded.Version.ToString(CultureInfo.InvariantCulture), "Verified", ""));
        plain.AddChild(Node(9, 1, "u8", "m_stHead.m_byCompress", decoded.Compress.ToString(CultureInfo.InvariantCulture), "Verified", ""));
        plain.AddChild(Node(0x0A, 1, "u8", "m_stHead.m_byOptLength", decoded.Options.Length.ToString(CultureInfo.InvariantCulture), "Verified", ""));
        plain.AddChild(Node(PlainBodyLenOffset, 2, "le:u16", "m_stBody.m_ushBodyLen", decoded.BodyLen.ToString(CultureInfo.InvariantCulture), "Verified", ""));
        var bodyNode = plain.AddChild(Node(PlainBodyOffset, decoded.Body.Length, "bytes", "m_stBody.m_acBody", decoded.BodyHead.HeaderName, "Verified", "decoded body.msg"));
        AddBodyHeaderAndTlv(bodyNode, decoded.Body, PlainBodyOffset, decoded.BodyHead, keyIndexBase: 0, isTruncated: false);
        if (decoded.ResponseTail is { IsRequestShaped: false } plainTail)
        {
            plain.AddChild(Node(PlainResponsePara1Offset, 4, "le:u32", "m_uiPara1", plainTail.Para1.ToString(CultureInfo.InvariantCulture), "Dynamic", "response-only tail"));
        }

        return new ParserStatus
        {
            Status = "parsed",
            Note = $"MsgID 24251 current QuickRPC raw m_acBuffer 已解码；direction={sourceDirection}。",
            StructureText = "Msg24251 QuickRPC raw buffer decoded.",
            RootNodes = [root]
        };
    }

    private static void AddPlainHead(ParsedFieldNode root, byte[] data)
    {
        var head = root.AddChild(Group(0, PlainBodyLenOffset, "QuickRPCHead", "plain head fields before m_stBody"));
        head.AddChild(Node(0, 4, "le:u32", "m_stHead.m_uiUin", U32(data, 0).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        head.AddChild(Node(4, 4, "le:u32", "m_stHead.m_uiTime", U32(data, 4).ToString(CultureInfo.InvariantCulture), "Dynamic", ""));
        head.AddChild(Node(8, 1, "u8", "m_stHead.m_byVersion", U8(data, 8).ToString(CultureInfo.InvariantCulture), "Verified", ""));
        head.AddChild(Node(9, 1, "u8", "m_stHead.m_byCompress", U8(data, 9).ToString(CultureInfo.InvariantCulture), "Verified", ""));
        var optLen = U8(data, 0x0A);
        head.AddChild(Node(0x0A, 1, "u8", "m_stHead.m_byOptLength", optLen.ToString(CultureInfo.InvariantCulture), "Verified", ""));
        if (optLen > 0 && data.Length >= 0x0B + optLen)
        {
            head.AddChild(Node(0x0B, optLen, "bytes", "m_stHead.m_abyOption", Hex(data, 0x0B, optLen), "Dynamic", ""));
        }
        var reservedStart = 0x0B + optLen;
        if (reservedStart < PlainBodyLenOffset)
        {
            head.AddChild(Node(reservedStart, PlainBodyLenOffset - reservedStart, "reserved", "HeadReservedOrPadding", $"size={PlainBodyLenOffset - reservedStart}", "Reserved", "fixed gap before body_len in plain projection"));
        }
    }

    private static void AddBodyHeaderAndTlv(ParsedFieldNode parent, byte[] body, long bodyFullOffset, QuickRpcBodyHead bodyHead, int keyIndexBase, bool isTruncated)
    {
        parent.AddChild(Node(bodyFullOffset, bodyHead.HeaderNameBytes, "ascii", "HeaderName", bodyHead.HeaderName, "Verified", "QuickRPC method/header name"));
        parent.AddChild(Node(bodyFullOffset + bodyHead.HeaderNameBytes, 2, "bytes", "HeaderSeparator", "00 00", "Verified", ""));
        if (bodyHead.HasInitialValue)
        {
            parent.AddChild(Node(bodyFullOffset + bodyHead.InitialValueOffset, 9, "tlv:value", "InitialValue", bodyHead.InitialValueText, "Dynamic", "optional F6 double directly after method header"));
        }

        var payloadOffset = bodyHead.PayloadOffset;
        var payloadLength = Math.Max(0, body.Length - payloadOffset);
        var tlv = parent.AddChild(Node(bodyFullOffset + payloadOffset, payloadLength, "tlv", "QuickRPCPayload", $"{payloadLength:N0} bytes", isTruncated ? "Unaligned" : "Verified", "F6=value, FA=text, FB=key index, FD=fixed block, F9=marker"));
        var state = new TlvParseState(keyIndexBase);
        ParseTlvBlock(tlv, body, payloadOffset, body.Length, bodyFullOffset, state, depth: 0);

        var dict = parent.AddChild(Node(bodyFullOffset + payloadOffset, 0, "dict", "StringDict", $"{state.Strings.Count} strings", "Verified", $"FB key index base={keyIndexBase}"));
        var maxDict = Math.Min(state.Strings.Count, 120);
        for (var i = 0; i < maxDict; i++)
        {
            dict.AddChild(Node(0, 0, "string", $"StringDict[{i + keyIndexBase}]", Truncate(state.Strings[i], 160), "Verified", ""));
        }
        if (state.Strings.Count > maxDict)
        {
            dict.AddChild(Node(0, 0, "summary", "StringDictMore", $"{state.Strings.Count - maxDict} more strings", "Dynamic", ""));
        }
    }

    private static void ParseTlvBlock(ParsedFieldNode parent, byte[] data, int start, int end, long displayBase, TlvParseState state, int depth)
    {
        var pos = start;
        while (pos < end)
        {
            if (state.NodeCount++ > MaxNodes)
            {
                parent.AddChild(Node(displayBase + pos, end - pos, "summary", "NodeLimitReached", $"{end - pos} bytes not expanded", "Dynamic", "UI 防卡顿限制；Hex View 仍可查看原始字节"));
                return;
            }

            if (depth > MaxTlvDepth)
            {
                parent.AddChild(Node(displayBase + pos, end - pos, "raw", "MaxDepthRawBlock", $"{end - pos} bytes", "Dynamic", "nested FD depth limit"));
                return;
            }

            var tokenStart = pos;
            if (!TryReadToken(data, ref pos, end, state, mutateDictionary: true, out var token))
            {
                parent.AddChild(Node(displayBase + tokenStart, end - tokenStart, "error", "TruncatedTLVToken", Hex(data, tokenStart, Math.Min(16, end - tokenStart)), "Unaligned", "TLV token truncated"));
                return;
            }

            if (token.Kind is TlvKind.Text or TlvKind.KeyIndex)
            {
                var name = token.Text ?? $"KeyIndex_{token.Number}";
                if (TryPeekToken(data, pos, end, state, out var next) && next.Kind == TlvKind.FixedLen)
                {
                    _ = TryReadToken(data, ref pos, end, state, mutateDictionary: true, out var fixedToken);
                    var blockLen = checked((int)Math.Min(fixedToken.Number, Math.Max(0, end - pos)));
                    var status = fixedToken.Number <= (uint)Math.Max(0, end - pos) ? "Verified" : "Unaligned";
                    var group = parent.AddChild(Node(displayBase + token.Start, token.Length + fixedToken.Length + blockLen, "group", SanitizeName(name), $"len={fixedToken.Number}", status, "named FD block"));
                    ParseTlvBlock(group, data, pos, pos + blockLen, displayBase, state, depth + 1);
                    pos += blockLen;
                    continue;
                }

                if (TryReadToken(data, ref pos, end, state, mutateDictionary: true, out var valueToken))
                {
                    parent.AddChild(Node(displayBase + token.Start, token.Length + valueToken.Length, ValueType(valueToken), SanitizeName(name), FormatTokenValue(valueToken), "Dynamic", ValueNote(valueToken)));
                    continue;
                }

                parent.AddChild(Node(displayBase + token.Start, token.Length, "field", SanitizeName(name), "", "Unaligned", "field name without value"));
                continue;
            }

            if (token.Kind == TlvKind.Value && TryPeekToken(data, pos, end, state, out var entryLen) && entryLen.Kind == TlvKind.FixedLen)
            {
                _ = TryReadToken(data, ref pos, end, state, mutateDictionary: true, out var fixedToken);
                var blockLen = checked((int)Math.Min(fixedToken.Number, Math.Max(0, end - pos)));
                var status = fixedToken.Number <= (uint)Math.Max(0, end - pos) ? "Verified" : "Unaligned";
                var group = parent.AddChild(Node(displayBase + token.Start, token.Length + fixedToken.Length + blockLen, "entry", $"Entry[{FormatTokenValue(token)}]", $"len={fixedToken.Number}", status, "array/table entry: value(index) + FD(length)"));
                ParseTlvBlock(group, data, pos, pos + blockLen, displayBase, state, depth + 1);
                pos += blockLen;
                continue;
            }

            if (token.Kind == TlvKind.FixedLen)
            {
                var blockLen = checked((int)Math.Min(token.Number, Math.Max(0, end - pos)));
                var status = token.Number <= (uint)Math.Max(0, end - pos) ? "Verified" : "Unaligned";
                var group = parent.AddChild(Node(displayBase + token.Start, token.Length + blockLen, "block", $"Block_0x{token.Start:X}", $"len={token.Number}", status, "anonymous FD block"));
                ParseTlvBlock(group, data, pos, pos + blockLen, displayBase, state, depth + 1);
                pos += blockLen;
                continue;
            }

            parent.AddChild(Node(displayBase + token.Start, token.Length, ValueType(token), TokenName(token), FormatTokenValue(token), token.Kind == TlvKind.Raw ? "UnknownName" : "Dynamic", ValueNote(token)));
        }
    }

    private static bool TryParseEncodedQuickRpc(byte[] data, out QuickRpcDecoded decoded, out string error)
    {
        decoded = default!;
        error = "";
        var pos = 0;
        var warnings = new List<string>();

        try
        {
            var bodyLenStart = pos;
            var bodyLen = (int)(ReadVarint(data, ref pos, DecBodyLenByte, maxBits: 16) & 0xFFFF);
            if (bodyLen == 0 || bodyLen > MaxQuickRpcBody)
            {
                error = $"body_len {bodyLen} invalid";
                return false;
            }

            if (pos + bodyLen > data.Length)
            {
                error = $"body_len overflow: pos={pos}, body_len={bodyLen}, raw_len={data.Length}";
                return false;
            }

            var bodyStart = pos;
            var body = new byte[bodyLen];
            for (var i = 0; i < body.Length; i++)
            {
                body[i] = (byte)DecBodyMsgByte(data[pos + i]);
            }
            pos += bodyLen;

            if (!TryDecodeQuickRpcBodyHeader(body, 0, body.Length, out var bodyHead))
            {
                error = "decoded body.msg has no printable QuickRPC method header";
                return false;
            }

            if (pos >= data.Length)
            {
                error = "missing body extra count";
                return false;
            }

            var bodyExtraCountOffset = pos;
            var bodyExtraCount = DecBodyCountByte(data[pos++]);
            var bodyExtras = new List<uint>();
            for (var i = 0; i < SafeCount(bodyExtraCount, warnings, "body_extra"); i++)
            {
                bodyExtras.Add(ReadVarint(data, ref pos, DecBodyExtraByte));
            }

            var headTimeOffset = pos;
            var time = ReadVarint(data, ref pos, DecHeadTimeByte);
            if (pos + 3 > data.Length)
            {
                error = "truncated head scalar fields";
                return false;
            }

            var version = DecHeadVersionByte(data[pos++]);
            var optLen = DecHeadOptLenByte(data[pos++]);
            if (pos + optLen > data.Length)
            {
                error = $"head option overflow: opt_len={optLen}, pos={pos}";
                return false;
            }

            var options = new byte[optLen];
            for (var i = 0; i < options.Length; i++)
            {
                options[i] = (byte)DecHeadOptionByte(data[pos + i]);
            }
            pos += optLen;

            var compress = DecHeadCompressByte(data[pos++]);
            var uin = ReadVarint(data, ref pos, DecHeadUinByte);
            if (pos >= data.Length)
            {
                error = "missing head extra count";
                return false;
            }

            var headExtraCount = DecHeadCountByte(data[pos++]);
            var headExtras = new List<uint>();
            for (var i = 0; i < SafeCount(headExtraCount, warnings, "head_extra"); i++)
            {
                headExtras.Add(ReadVarint(data, ref pos, DecHeadOptionByte));
            }

            QuickRpcResponseTail? responseTail = null;
            if (pos < data.Length)
            {
                var tailStart = pos;
                try
                {
                    var p2 = pos;
                    var para1 = ReadVarint(data, ref p2, DecResponsePara1Byte);
                    int? count = null;
                    var requestShaped = false;
                    if (p2 < data.Length)
                    {
                        count = DecResponseCountByte(data[p2++]);
                        if (count < 0x11)
                        {
                            for (var i = 0; i < count.Value; i++)
                            {
                                _ = ReadVarint(data, ref p2, DecResponseExtraByte);
                            }
                        }
                        else
                        {
                            requestShaped = true;
                            warnings.Add($"response_extra count {count} >= 17; request-shaped tail?");
                        }
                    }

                    responseTail = new QuickRpcResponseTail(
                        tailStart,
                        requestShaped ? 0 : p2 - tailStart,
                        para1,
                        count,
                        requestShaped,
                        requestShaped ? "request-shaped/random tail; response extra parser intentionally stopped" : "response-only tail parsed");

                    if (!requestShaped && count is not null)
                    {
                        pos = p2;
                    }
                }
                catch (Exception ex)
                {
                    warnings.Add($"response tail parse skipped: {ex.Message}");
                }
            }

            decoded = new QuickRpcDecoded(
                data.Length,
                bodyLen,
                pos,
                data.Length - pos,
                bodyLenStart,
                bodyStart - bodyLenStart,
                bodyStart,
                bodyExtraCountOffset,
                bodyExtraCount,
                bodyExtras,
                headTimeOffset,
                Math.Max(0, pos - headTimeOffset),
                uin,
                time,
                version,
                compress,
                options,
                headExtraCount,
                headExtras,
                body,
                bodyHead,
                responseTail,
                warnings);
            return true;
        }
        catch (Exception ex)
        {
            error = ex.Message;
            return false;
        }
    }

    private static bool TryDecodeQuickRpcBodyHeader(byte[] body, int start, int end, out QuickRpcBodyHead header)
    {
        header = default!;
        var marker = -1;
        for (var i = start; i + 1 < end && i < start + 128; i++)
        {
            if (body[i] == 0 && body[i + 1] == 0)
            {
                marker = i;
                break;
            }
        }

        if (marker <= start)
        {
            return false;
        }

        var nameBytes = body.AsSpan(start, marker - start).ToArray();
        if (!nameBytes.All(b => b is >= 0x20 and <= 0x7E))
        {
            return false;
        }

        var name = Encoding.ASCII.GetString(nameBytes);
        var pos = marker + 2;
        var hasInitial = false;
        var initialOffset = 0;
        var initialText = "";
        if (pos + 9 <= end && body[pos] == 0xF6)
        {
            var value = BitConverter.ToDouble(body, pos + 1);
            hasInitial = true;
            initialOffset = pos;
            initialText = FormatDouble(value);
            pos += 9;
        }

        header = new QuickRpcBodyHead(name, nameBytes.Length, pos, hasInitial, initialOffset, initialText);
        return true;
    }

    private static bool TryFindMatchingPlainFullSibling(string selectedPath, byte[] bodySlice, out byte[] fullData, out string fullName)
    {
        fullData = [];
        fullName = "";
        var dir = Path.GetDirectoryName(selectedPath);
        if (string.IsNullOrWhiteSpace(dir) || !Directory.Exists(dir))
        {
            return false;
        }

        foreach (var candidate in Directory.EnumerateFiles(dir, "*special28346_after_full_msg24251_len32272.bin"))
        {
            var data = File.ReadAllBytes(candidate);
            if (data.Length == PlainFullLength && data.AsSpan(0x0E).SequenceEqual(bodySlice))
            {
                fullData = data;
                fullName = Path.GetFileName(candidate);
                return true;
            }
        }

        return false;
    }

    private static bool IsBodySliceCandidate(PacketRecord packet, byte[] data)
    {
        return data.Length == PlainBodySliceLength ||
               packet.Direction.Contains("_body", StringComparison.OrdinalIgnoreCase);
    }

    private static uint ReadVarint(byte[] data, ref int pos, Func<int, int> decoder, int maxBits = 32)
    {
        uint value = 0;
        var shift = 0;
        var start = pos;
        while (pos < data.Length && shift < maxBits + 7)
        {
            var b = decoder(data[pos++]) & 0xFF;
            value |= (uint)(b & 0x7F) << shift;
            if (b < 0x80)
            {
                return value;
            }
            shift += 7;
        }

        throw new InvalidDataException($"varint decode failed at {start}, pos={pos}, len={data.Length}");
    }

    private static int SafeCount(int count, List<string> warnings, string field)
    {
        if (count < 0x11)
        {
            return count;
        }

        warnings.Add($"{field} count {count} >= 17; tail parsing stopped");
        return 0;
    }

    private static int DecBodyLenByte(int w)
    {
        var b = U8(w << 2);
        var idx = U8((b >> 5) | (((w >> 6) | b) << 3));
        var t = S1[S1[idx] ^ 0xFA];
        return Ror(t, 2);
    }

    private static int DecBodyMsgByte(int w)
    {
        var b = Ror(w, 3);
        b = MixPairs(b);
        b = U8(Ror(b, 3) + 0x7A);
        return U8(S1[S1[NibSwap(b)]] - 1);
    }

    private static int DecBodyCountByte(int w)
    {
        var x = S1[U8(w + 0x8E)];
        var y = U8(Rol(x, 3) ^ 0x5F);
        var z = S1[S1[y]];
        return Ror(MixPairs(z), 2);
    }

    private static int DecBodyExtraByte(int w)
    {
        var b = U8(BitPerm(w) + 0x1D);
        var b2 = U8(b << 1);
        var x = U8((b2 >> 4) | (((b >> 7) | b2) << 4));
        return Rol(BitPerm(x), 2);
    }

    private static int DecHeadTimeByte(int w)
    {
        var idx = U8((w << 6) | ((w ^ 0x1C) >> 2));
        var b = S1[S1[idx]];
        return S1[b ^ 0x28];
    }

    private static int DecHeadVersionByte(int w)
    {
        var a = S1[S1[w] ^ 0x94];
        var b = U8(Ror(a, 1) - 0x2C);
        return Rol(S1[S1[b]], 3);
    }

    private static int DecHeadOptLenByte(int w)
    {
        var a = S1[w];
        var b = U8(Ror(NibSwap(a), 3) + 0x18);
        b = U8(Ror(b, 5) + 0x28);
        return U8(Ror(b, 5) + 0x45);
    }

    private static int DecHeadOptionByte(int w)
    {
        return S1[S1[U8((U8(Ror(w, 2) + 0x24) ^ 1) + 0xB7)] ^ 0xEC];
    }

    private static int DecHeadCompressByte(int w)
    {
        var b = BitPerm(U8(~w));
        return U8(BitPerm(Rol(b, 6)) + 0x0B);
    }

    private static int DecHeadUinByte(int w)
    {
        return U8(~BitPerm(NibSwap(w)) + 100);
    }

    private static int DecHeadCountByte(int w)
    {
        var b = U8(w + 0x4F);
        var x = U8(Rol(b, 1) + 0x2B);
        return BitPerm(U8(~BitPerm(x)));
    }

    private static int DecResponsePara1Byte(int w) => U8(w - 0x0D);

    private static int DecResponseCountByte(int w)
    {
        var b = U8(Ror(w, 1) + 0x85);
        return Rol(S1[S1[U8(Rol(b, 2) + 0xA6)]], 3);
    }

    private static int DecResponseExtraByte(int w)
    {
        var b = Rol(S1[w], 4);
        b = MixPairs(b);
        var hi = b >> 4;
        b = U8(((hi | (b << 4)) >> 2) | (hi << 6));
        b = U8(NibSwap(b) - 0x1E);
        return MixPairs(b);
    }

    private static int U8(int x) => x & 0xFF;

    private static int Rol(int x, int n) => U8((x << n) | (x >> (8 - n)));

    private static int Ror(int x, int n) => U8((x >> n) | (x << (8 - n)));

    private static int NibSwap(int x) => U8((x >> 4) | (x << 4));

    private static int MixPairs(int x) => U8(((x & 0xD5) << 1) | ((x >> 1) & 0x55));

    private static int BitPerm(int x)
    {
        return U8((((x * 0x8020 & 0x88440) | (x * 0x802 & 0x22110)) * 0x10101) >> 16);
    }

    private static bool TryReadToken(byte[] data, ref int pos, int end, TlvParseState state, bool mutateDictionary, out TlvToken token)
    {
        token = default;
        if (pos >= end)
        {
            return false;
        }

        var start = pos;
        var tag = data[pos++];
        switch (tag)
        {
            case 0xF6:
                if (pos + 8 > end) return false;
                var value = BitConverter.ToDouble(data, pos);
                pos += 8;
                token = new TlvToken(TlvKind.Value, start, 9, tag, 0, null, value);
                return true;

            case 0xFA:
                if (pos >= end) return false;
                var len = data[pos++];
                if (pos + len > end) return false;
                var text = DecodeText(data.AsSpan(pos, len));
                pos += len;
                if (mutateDictionary)
                {
                    state.Strings.Add(text);
                }
                token = new TlvToken(TlvKind.Text, start, 2 + len, tag, (uint)len, text, null);
                return true;

            case 0xFB:
                if (pos >= end) return false;
                var idx = data[pos++];
                token = new TlvToken(TlvKind.KeyIndex, start, 2, tag, idx, state.Resolve(idx), null);
                return true;

            case 0xFD:
                if (pos + 4 > end) return false;
                var n = BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(pos, 4));
                pos += 4;
                token = new TlvToken(TlvKind.FixedLen, start, 5, tag, n, null, null);
                return true;

            case 0xF9:
                token = new TlvToken(TlvKind.Marker, start, 1, tag, 0, null, null);
                return true;

            default:
                token = new TlvToken(TlvKind.Raw, start, 1, tag, tag, null, null);
                return true;
        }
    }

    private static bool TryPeekToken(byte[] data, int pos, int end, TlvParseState state, out TlvToken token)
    {
        return TryReadToken(data, ref pos, end, state, mutateDictionary: false, out token);
    }

    private static string DecodeText(ReadOnlySpan<byte> raw)
    {
        if (raw.ToArray().All(b => b is >= 0x20 and <= 0x7E))
        {
            return Encoding.ASCII.GetString(raw);
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

    private static string ValueType(TlvToken token)
    {
        return token.Kind switch
        {
            TlvKind.Value => "tlv:value",
            TlvKind.Text => "tlv:text",
            TlvKind.KeyIndex => "tlv:key",
            TlvKind.FixedLen => "tlv:len",
            TlvKind.Marker => "tlv:marker",
            _ => "tlv:raw"
        };
    }

    private static string TokenName(TlvToken token)
    {
        return token.Kind switch
        {
            TlvKind.Value => $"Value_0x{token.Start:X}",
            TlvKind.Text => $"Text_0x{token.Start:X}",
            TlvKind.KeyIndex => $"KeyIndex_{token.Number}",
            TlvKind.FixedLen => $"FixedLen_0x{token.Start:X}",
            TlvKind.Marker => $"Marker_0x{token.Start:X}",
            _ => $"Raw_0x{token.Tag:X2}_0x{token.Start:X}"
        };
    }

    private static string FormatTokenValue(TlvToken token)
    {
        return token.Kind switch
        {
            TlvKind.Value => FormatDouble(token.DoubleValue ?? 0),
            TlvKind.Text => token.Text ?? "",
            TlvKind.KeyIndex => token.Text ?? $"<key_index={token.Number}>",
            TlvKind.FixedLen => token.Number.ToString(CultureInfo.InvariantCulture),
            TlvKind.Marker => "F9",
            _ => $"0x{token.Number:X2}"
        };
    }

    private static string ValueNote(TlvToken token)
    {
        return token.Kind switch
        {
            TlvKind.KeyIndex => $"FB references StringDict[{token.Number}]",
            TlvKind.Text => "FA text; also appended to StringDict",
            TlvKind.Value => "F6 IEEE-754 double, shown as integer when exact",
            TlvKind.FixedLen => "FD big-endian block length",
            TlvKind.Marker => "F9 marker",
            _ => "unknown single-byte tag"
        };
    }

    private static string FormatDouble(double value)
    {
        if (double.IsFinite(value) && Math.Abs(value - Math.Round(value)) < 0.0000001)
        {
            return ((long)Math.Round(value)).ToString(CultureInfo.InvariantCulture);
        }

        return value.ToString("R", CultureInfo.InvariantCulture);
    }

    private static string SanitizeName(string name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return "Unnamed";
        }

        var chars = name.Select(ch => char.IsLetterOrDigit(ch) || ch is '_' or '.' or '[' or ']' ? ch : '_').ToArray();
        return new string(chars);
    }

    private static string Truncate(string value, int max)
    {
        return value.Length <= max ? value : value[..max] + "...";
    }

    private static byte U8(byte[] data, int offset) => data[offset];

    private static ushort U16(byte[] data, int offset) => BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(offset, 2));

    private static uint U32(byte[] data, int offset) => BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(offset, 4));

    private static string Hex(byte[] data, int offset, int length)
    {
        if (offset < 0 || offset >= data.Length || length <= 0)
        {
            return "";
        }

        var safe = Math.Min(length, data.Length - offset);
        return Convert.ToHexString(data, offset, safe);
    }

    private static ParsedFieldNode Group(long offset, int size, string name, string note)
    {
        return Node(offset, size, "group", name, "", "Verified", note);
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
            StructureText = "Not a MsgID 24251 payload.",
            RootNodes =
            [
                Node(0, 0, "parser", "UnsupportedMsg24251", $"MsgID {packet.MsgIdDisplay}", "not connected", note)
            ]
        };
    }

    private enum TlvKind
    {
        Value,
        Text,
        KeyIndex,
        FixedLen,
        Marker,
        Raw
    }

    private sealed class TlvParseState(int keyIndexBase)
    {
        public int KeyIndexBase { get; } = keyIndexBase;
        public List<string> Strings { get; } = [];
        public int NodeCount { get; set; }

        public string? Resolve(uint index)
        {
            var actual = checked((int)index) - KeyIndexBase;
            return actual >= 0 && actual < Strings.Count ? Strings[actual] : null;
        }
    }

    private readonly record struct TlvToken(
        TlvKind Kind,
        int Start,
        int Length,
        int Tag,
        uint Number,
        string? Text,
        double? DoubleValue);

    private readonly record struct QuickRpcBodyHead(
        string HeaderName,
        int HeaderNameBytes,
        int PayloadOffset,
        bool HasInitialValue,
        int InitialValueOffset,
        string InitialValueText);

    private readonly record struct QuickRpcResponseTail(
        int TailOffset,
        int Consumed,
        uint Para1,
        int? Count,
        bool IsRequestShaped,
        string Note)
    {
        public string CountText => Count?.ToString(CultureInfo.InvariantCulture) ?? "missing";
    }

    private readonly record struct QuickRpcDecoded(
        int RawLen,
        int BodyLen,
        int Consumed,
        int Remaining,
        int BodyLenOffset,
        int BodyLenVarintSize,
        int BodyEncodedOffset,
        int BodyExtraCountOffset,
        int BodyExtraCount,
        IReadOnlyList<uint> BodyExtras,
        int HeadTimeOffset,
        int HeadEncodedSize,
        uint Uin,
        uint Time,
        int Version,
        int Compress,
        byte[] Options,
        int HeadExtraCount,
        IReadOnlyList<uint> HeadExtras,
        byte[] Body,
        QuickRpcBodyHead BodyHead,
        QuickRpcResponseTail? ResponseTail,
        IReadOnlyList<string> Warnings);
}
