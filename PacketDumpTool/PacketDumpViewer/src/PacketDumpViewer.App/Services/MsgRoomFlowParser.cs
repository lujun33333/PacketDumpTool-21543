using System.Buffers.Binary;
using System.Globalization;
using System.IO;
using System.Text;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class MsgRoomFlowParser
{
    private const int RoomNameBytes = 0x16;

    static MsgRoomFlowParser()
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
    }

    public ParserStatus Parse(PacketRecord packet)
    {
        if (packet.MsgId is not (102 or 103 or 110 or 120))
        {
            return Unsupported(packet, "不是 MsgID 102/103/110/120。");
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
        return packet.MsgId.Value switch
        {
            102 => ParseMsg102(data, packet),
            103 => ParseMsg103(data, packet),
            110 => ParseMsg110(data, packet),
            120 => ParseMsg120(data, packet),
            _ => Unsupported(packet, "未接入。")
        };
    }

    private static ParserStatus ParseMsg102(byte[] data, PacketRecord packet)
    {
        var root = Node(
            0,
            data.Length,
            "struct",
            "CResponseGetRoomListPara_Wire",
            $"{data.Length:N0} bytes",
            "Partial",
            "当前 dump 的 MsgID 102 inner_dispatch_body 与房间列表响应对齐，不再按 GameBaseRecordList 硬套；字段名来自 qqspeed_decoder/messages.py 与 dump 长度/count 闭环");

        root.AddChild(Node(0, 0, "note", "Evidence", "ResponseGetRoomList / _decode_room_list_entry_102", "Verified",
            @"F:\new1\qqspeed_decoder\messages.py decodes MsgID 102 as ResponseGetRoomList; current dumps show ResultID/CurTotalRoomNum/StartIdx/RoomNum + len-wrapped room entries, and room count consumes the payload exactly except documented trailing filter fields."));

        var start = AddLengthPrefix(root, data, "BodyLengthPrefix", "Msg102 response samples use first be:u16 == file length; class/wire payload begins at +0x02");
        var body = root.AddChild(Node(start, Math.Max(0, data.Length - start), "struct", "CResponseGetRoomListPara", "", "Partial",
            "wire multi-byte fields use BE; each room is a be:u16 length-wrapped block."));

        var cursor = start;
        AddI16BE(body, data, cursor, "m_shResultID", "Verified", "");
        cursor += 2;
        AddU16BE(body, data, cursor, "m_usCurTotalRoomNum", "Dynamic", "当前频道总房间数");
        cursor += 2;
        AddU16BE(body, data, cursor, "m_usStartIdx", "Dynamic", "");
        cursor += 2;
        var roomNum = CanRead(data, cursor, 2) ? U16BE(data, cursor) : 0;
        AddU16BE(body, data, cursor, "m_usRoomNum", "Verified", "后面 len-wrapped m_stRooms 条数");
        cursor += 2;

        var rooms = body.AddChild(Node(cursor, Math.Max(0, data.Length - cursor), "array", "m_stRooms", $"count={roomNum}", "Partial",
            "每个 Room entry 的第一个 be:u16 是 entry 总长度；列表只展开前 32 条，剩余保留 offset。"));
        var maxRooms = Math.Min(roomNum, (ushort)32);
        for (var i = 0; i < maxRooms && CanRead(data, cursor, 2); i++)
        {
            var entryLen = U16BE(data, cursor);
            if (entryLen < 2 || !CanRead(data, cursor, entryLen))
            {
                rooms.AddChild(Node(cursor, Math.Max(0, data.Length - cursor), "raw", $"Room[{i}].Unaligned", Hex(data, cursor, Math.Min(96, data.Length - cursor)), "Unaligned",
                    $"Room length {entryLen} exceeds payload or is invalid"));
                break;
            }

            var entry = rooms.AddChild(Node(cursor, entryLen, "struct", $"m_stRooms[{i}]", $"len={entryLen}", "Partial", ""));
            entry.AddChild(Node(cursor, 2, "be:u16", "m_usLen", entryLen.ToString(CultureInfo.InvariantCulture), "Verified", "length includes this 2-byte prefix"));
            var e = cursor + 2;
            AddU16BE(entry, data, e, "m_usRoomID", "Dynamic", "");
            AddI16BE(entry, data, e + 2, "m_sBaseGameMode", "Dynamic", "");
            AddI16BE(entry, data, e + 4, "m_sSubGameMode", "Dynamic", "");
            AddU8(entry, data, e + 6, "m_ucCurrentPlayerNum", "Dynamic", "");
            AddU8(entry, data, e + 7, "m_ucTotalSeatNum", "Dynamic", "");
            AddU8(entry, data, e + 8, "m_ucStatus", "Dynamic", "");
            AddU32BE(entry, data, e + 9, "m_uiMapID", "Dynamic", "");
            AddU16BE(entry, data, e + 13, "m_usFlag", "Dynamic", "21043+ 源码/decoder 使用 u16 RoomFlag，不是旧 u8");
            AddFixedString(entry, data, e + 15, RoomNameBytes, "m_szRoomName", "GBK room name; fixed 0x16 bytes");
            AddU8(entry, data, e + 37, "m_ucHasFBInfo", "Dynamic", "为 1 时后面有 FBInfo len-block；v1.2 暂不展开可变 FBInfo");
            if (CanRead(data, e + 38, Math.Min(48, entryLen - 40)))
            {
                entry.AddChild(Node(e + 38, Math.Min(48, entryLen - 40), "raw", "RoomExtraAfterKnownHead", Hex(data, e + 38, Math.Min(48, entryLen - 40)), "UnknownName",
                    "后续字段包括 m_ucSpeFlag/m_uiRoomNo/m_usSvrId/m_usSceneID/m_ucAllowAutoStart 等；FBInfo 可变段未定位前保留 raw"));
            }

            cursor += entryLen;
        }

        if (roomNum > maxRooms)
        {
            rooms.AddChild(Node(cursor, 0, "note", "RoomsFolded", $"{roomNum - maxRooms} room entries not expanded", "Partial", "为了 UI 不刷屏，仅展开前 32 条。"));
        }

        if (cursor < data.Length)
        {
            var tail = body.AddChild(Node(cursor, data.Length - cursor, "group", "ResponseGetRoomListTail", "", "Partial",
                "房间数组之后的过滤/模式字段，来自 qqspeed_decoder/messages.py；若长度不足则保留 raw。"));
            if (CanRead(data, cursor, 1))
            {
                var reasonLen = U8(data, cursor);
                tail.AddChild(Node(cursor, 1, "u8", "m_szReasonLen_orNextField", reasonLen.ToString(CultureInfo.InvariantCulture), "UnknownName",
                    "当前样本多为 0；官方 decoder 在这里先尝试 Reason string，然后继续读 Base/Sub/FB/Channel/GetType"));
                cursor += 1;
                if (reasonLen > 0 && CanRead(data, cursor, reasonLen))
                {
                    tail.AddChild(Node(cursor, reasonLen, "bytes/string", "m_szReason", DecodeText(data.AsSpan(cursor, reasonLen)), "Dynamic", ""));
                    cursor += reasonLen;
                }
            }

            AddI16BE(tail, data, cursor, "m_sBaseGameMode", "Dynamic", "tail filter field; offset depends on ReasonLen");
            AddI16BE(tail, data, cursor + 2, "m_sSubGameMode", "Dynamic", "tail filter field; offset depends on ReasonLen");
            AddU8(tail, data, cursor + 4, "m_ucFBAvailableLev", "Dynamic", "");
            AddU8(tail, data, cursor + 5, "m_ucFBLev", "Dynamic", "");
            AddU8(tail, data, cursor + 6, "m_ucFBSeason", "Dynamic", "");
            AddU16BE(tail, data, cursor + 7, "m_usChannelStatus", "Dynamic", "");
            AddU8(tail, data, cursor + 9, "m_ucRoomFlag", "Dynamic", "");
            AddU8(tail, data, cursor + 10, "m_ucGetType", "Dynamic", "");
            AddTrailing(tail, data, cursor + 11, "TrailingAfterMsg102TailFields", "未被当前 tail field 覆盖的字节保留 raw");
        }

        return new ParserStatus
        {
            Status = "parsed-partial",
            Note = $"MsgID 102 已按当前 dump 修正为房间列表响应；Room entry 可变尾部仍 UnknownName，不乱编。direction={packet.Direction}",
            StructureText = "Msg102 room-list response structure tree loaded.",
            RootNodes = [root]
        };
    }

    private static ParserStatus ParseMsg103(byte[] data, PacketRecord packet)
    {
        if (IsSafeSourceObject(packet.Direction))
        {
            return ParseMsg103RequestSourceObject(data, packet);
        }

        if (IsSafeCompact(packet.Direction))
        {
            return ParseSafeCompact(103, "CSSafeCRequestCreateRoomPara", data, packet,
                "compact CSSafe 103 body；打开同 seq 的 safe28346_builder_src_object_before/after 可看已还原的源对象字段");
        }

        return ParseMsg103Response(data, packet);
    }

    private static ParserStatus ParseMsg103RequestSourceObject(byte[] data, PacketRecord packet)
    {
        var root = Node(0, data.Length, "struct", "RequestCreateRoom103_SourceBuffer", $"{data.Length:N0} bytes", "Partial",
            "C→S 请求/发送；这是 Top-Kart 传给 28346 builder 的 plain source buffer 快照，不是 Network.dll CSSafe native object，也不是 compact wire。字段按 dump + Top-Kart sub_1807E19B0 + qqspeed_decoder 顺序对齐。");
        root.AddChild(Node(0, 0, "note", "Evidence", "Top-Kart sub_1807E19B0 / OnPreRequestCreateRoom / RequestCreateRoom decoder", "Verified",
            @"F:\new1\dll\new\21273\ida_export_Top-Kart_20260613_130744\pseudoc\007E19B0_sub_1807E19B0.c builds v26[87], writes Uin/GetTickCount, checks v26[53] SuggestMapFlag, clamps v26[69] when *(i16*)&v26[70] >= 400, then sub_180BDD400(103, v26...). F:\new1\qqspeed_decoder\messages.py has RequestCreateRoom field order."));

        AddU32LE(root, data, 0x00, "m_uiUin", "Dynamic", "");
        AddU32LE(root, data, 0x04, "m_uiTime", "Dynamic", "client/game time field from request object");
        AddU16LE(root, data, 0x08, "m_sBaseGameMode", "Dynamic", "");
        AddU16LE(root, data, 0x0A, "m_sSubGameMode", "Dynamic", "");
        AddU16LE(root, data, 0x0C, "m_usFlag", "Dynamic", "qqspeed_decoder 字段名；dump 中按 2 bytes 存放");
        AddFixedString(root, data, 0x0E, RoomNameBytes, "m_szRoomName", "GBK room name; fixed 0x16 bytes");
        AddU8(root, data, 0x24, "m_ucSeatNum", "Dynamic", "");
        AddRaw(root, data, 0x25, Math.Min(0x10, Math.Max(0, data.Length - 0x25)), "m_szPassword", "16-byte password slot; 当前样本多为 0，不打印为敏感文本");
        AddU8(root, data, 0x35, "m_ucIsNewerOpen_orSuggestMapFlag", "Dynamic",
            "字段顺序来自 RequestCreateRoom decoder；Top-Kart sub_1807E19B0 会按 SuggestMapFlag/baseMode 改写 v26[53]，所以这里同时备注 SuggestMapFlag 证据");
        AddU8(root, data, 0x36, "m_ucFBSeason", "Dynamic", "字段顺序来自 RequestCreateRoom decoder；多 dump 常见 5");
        AddU8(root, data, 0x37, "m_ucContex", "UnknownName", "字段名来自 decoder；当前样本多为 0，语义未消费闭环");
        AddU8(root, data, 0x38, "m_ucFBLev", "UnknownName", "字段名来自 decoder；当前样本多为 0");
        AddU8(root, data, 0x39, "m_ucSpeFlag", "UnknownName", "字段名来自 decoder；当前样本多为 0");
        AddU8(root, data, 0x3A, "m_ucMoreInfoNum", "Dynamic", "当前样本为 0，因此 MoreInfos 不展开");

        var extended = root.AddChild(Node(0x3B, Math.Min(0x25, Math.Max(0, data.Length - 0x3B)), "group", "CreateRoomExtendedFields", "", "Partial",
            "从 0x3B 开始是创建房间扩展区；Top-Kart 只直接证明 0x45/0x46 关系，其余字段按 decoder 名称但未全部消费闭环。"));
        AddI16LE(extended, data, 0x3B, "m_sCreateRoomType_candidate", "UnknownName", "decoder 顺序字段；样本多为 0，未找到 21273 消费点");
        AddI16LE(extended, data, 0x3D, "m_sServerMatchType_candidate", "UnknownName", "decoder 顺序字段；样本多为 0");
        AddI16LE(extended, data, 0x3F, "m_sTeamMatchType_candidate", "UnknownName", "decoder 顺序字段；样本多为 0");
        AddU8(extended, data, 0x41, "m_ucChatRoomType_candidate", "UnknownName", "decoder 顺序字段；Top-Kart 对 0x45/0x46 有额外写入，说明当前版本这一段可能存在偏移/模式差异");
        AddI16LE(extended, data, 0x42, "m_sSceneID_candidate", "UnknownName", "decoder 顺序字段；未与 Top-Kart 0x46 threshold 完全闭环");
        AddU8(extended, data, 0x44, "m_ucPresetOBNum_candidate", "UnknownName", "");
        AddU8(extended, data, 0x45, "m_ucRoomTypeOrAutoStart_21273", "Dynamic",
            "Top-Kart sub_1807E19B0: v20=v26[69]; if *(i16*)&v26[70] >= 400 then v20=6; v26[69]=v20。样本 8068_3806: 0x45=6, 0x46=400。真实名称仍待 LUC/Lua 消费点确认。");
        AddI16LE(extended, data, 0x46, "m_sSceneIDOrMapThreshold_21273", "Dynamic",
            "Top-Kart 直接读取 *(i16*)&v26[70] 与 400 比较；这是已确认 offset/类型，字段名语义仍未完全确认");
        AddU32LE(extended, data, 0x48, "m_uiCreateRoomTailValue_0", "UnknownName", "多 dump 常见 0 或 4；未找到消费点，不命名为业务字段");
        AddU32LE(extended, data, 0x58, "m_uiCreateRoomTailValue_1", "UnknownName", "多 dump 常见 15；未找到消费点，不命名为业务字段");

        if (data.Length > 0x60)
        {
            root.AddChild(Node(0x60, Math.Min(0x80, data.Length - 0x60), "bytes/string", "LuaEventOrDebugTail", DecodeText(data.AsSpan(0x60, Math.Min(0x80, data.Length - 0x60))), "UnknownName",
                "样本中可见 UI.children.AppEventHandle.OnPreRequestCreateRoom；属于栈/脚本桥接快照尾部，不作为 wire 字段"));
        }

        AddTrailing(root, data, 0xE0, "SourceBufferPreviewTail", "dump 是 512B preview，0x57 之后已经超出 Top-Kart v26[87] 明确 source buffer 范围；只作快照证据，不按协议字段命名");

        return new ParserStatus
        {
            Status = "parsed-partial",
            Note = $"MsgID 103 请求 source buffer 已按 21273 Top-Kart + dump 对齐；0x45/0x46 已补 offset 证据，未确认语义仍标 UnknownName。direction={packet.Direction}",
            StructureText = "Msg103 request source object structure tree loaded.",
            RootNodes = [root]
        };
    }

    private static ParserStatus ParseMsg103Response(byte[] data, PacketRecord packet)
    {
        var root = Node(0, data.Length, "struct", "CResponseCreateRoomPara_Wire", $"{data.Length:N0} bytes", "Partial",
            "S→C 响应/通知；按官服 CResponseCreateRoomPara 前部字段和当前 inner_dispatch_body dump 解析，后续扩展区未完全对齐则 raw 保留");
        root.AddChild(Node(0, 0, "note", "Evidence", "CResponseCreateRoomPara", "Verified",
            @"Network strings_redacted.csv has CResponseCreateRoomPara; Network.FULL.all_funcs.c print order: ResultID/RoomID/BaseMode/SubMode/Flag/RoomName/Seat/Reason/extended fields"));

        var start = AddLengthPrefix(root, data, "ResponseLen", "103 response samples have be:u16 declared class/wire length; bytes after declared end are kept as trailing raw");
        var body = root.AddChild(Node(start, Math.Max(0, data.Length - start), "struct", "CResponseCreateRoomPara", "", "Partial", "wire multi-byte fields use BE"));
        var cursor = start;

        AddU16BE(body, data, cursor, "m_nResultID", "Verified", "");
        cursor += 2;
        AddU16BE(body, data, cursor, "m_nRoomID", "Dynamic", "");
        cursor += 2;
        AddI16BE(body, data, cursor, "m_sBaseGameMode", "Dynamic", "");
        cursor += 2;
        AddI16BE(body, data, cursor, "m_sSubGameMode", "Dynamic", "");
        cursor += 2;
        AddU16BE(body, data, cursor, "m_bFlag", "Dynamic", "");
        cursor += 2;
        AddFixedString(body, data, cursor, RoomNameBytes, "m_szRoomName", "GBK room name; fixed 0x16 bytes");
        cursor += RoomNameBytes;
        AddU8(body, data, cursor++, "m_bSeatNum", "Dynamic", "");
        AddU8(body, data, cursor++, "m_bSeatID", "Dynamic", "");
        AddU8(body, data, cursor++, "m_bTeamID", "Dynamic", "");
        var reasonLenOffset = cursor;
        var reasonLen = CanRead(data, cursor, 1) ? U8(data, cursor) : 0;
        AddU8(body, data, cursor++, "m_bReasonLen", "Verified", "");
        if (CanRead(data, cursor, reasonLen))
        {
            body.AddChild(Node(cursor, reasonLen, "bytes/string", "m_szReason", DecodeText(data.AsSpan(cursor, reasonLen)), "Dynamic", reasonLen == 0 ? "ReasonLen=0" : "length controlled by m_bReasonLen"));
            cursor += reasonLen;
        }
        else
        {
            body.AddChild(Node(reasonLenOffset, Math.Max(0, data.Length - reasonLenOffset), "raw", "ReasonOutOfRange", $"{reasonLen} bytes requested", "Unaligned", "ReasonLen 超出 payload"));
        }

        if (CanRead(data, cursor, 1))
        {
            body.AddChild(Node(cursor, Math.Min(96, data.Length - cursor), "raw", "ExtendedCreateRoomFields", Hex(data, cursor, Math.Min(96, data.Length - cursor)), "UnknownName",
                "后续官方字段包括 m_bLoverRaceOrPro/m_iMapID/m_unServerTime/m_ucMoreInfoNum/m_sCreateRoomType 等；当前 wire 和 expanded print offset 不能直接混用，先保留 raw"));
        }

        AddDeclaredTrailing(root, data);

        return new ParserStatus
        {
            Status = "parsed-partial",
            Note = $"MsgID 103 响应已按 CResponseCreateRoomPara 前部字段解析；扩展区待继续对齐。direction={packet.Direction}",
            StructureText = "Msg103 response structure tree loaded.",
            RootNodes = [root]
        };
    }

    private static ParserStatus ParseMsg110(byte[] data, PacketRecord packet)
    {
        if (IsSafeSourceObject(packet.Direction))
        {
            var root = Node(0, data.Length, "struct", "RequestStartGame110_SourceBufferPreview", $"{data.Length:N0} bytes", "Partial",
                "C→S 请求/发送；这是 28346 builder 前的 source buffer 预览，不是 Network.dll CSSafe native object。当前只确认 Uin/Time，后续 21273 发送函数未定位到稳定 source 写入顺序，所以保留 raw。");
            root.AddChild(Node(0, 0, "note", "Evidence", "CSSafeCRequestStartGamePara RTTI + compact body", "Verified",
                @"Network.dll has CSSafeCRequestStartGamePara constructor/destructor and object size 0xA0; current dump source preview begins with Uin/GetTickCount but no exact Top-Kart sub_180BDD400(110,...) caller was found in exported pseudoc."));
            AddU32LE(root, data, 0x00, "m_uiUin", "Dynamic", "");
            AddU32LE(root, data, 0x04, "m_uiTime", "Dynamic", "");
            AddRaw(root, data, 0x08, Math.Min(0x98, Math.Max(0, data.Length - 0x08)), "StartGameSourceOrStackTail", "后续有指针/运行时快照特征；未找到 21273 caller/accessor 前不命名");
            AddTrailing(root, data, 0xA0, "SourceBufferPreviewTail", "文件是 512B preview；0xA0 后不按 CSSafeCRequestStartGamePara 字段解释");

            return new ParserStatus
            {
                Status = "parsed-partial",
                Note = $"MsgID 110 请求 source preview 只确认 Uin/Time；后续 raw 保留，避免乱编字段。direction={packet.Direction}",
                StructureText = "Msg110 request source object structure tree loaded.",
                RootNodes = [root]
            };
        }

        if (IsSafeCompact(packet.Direction))
        {
            return ParseSafeCompact(110, "CSSafeCRequestStartGamePara", data, packet,
                "compact CSSafe 110 body；不能直接 cast 成 native 对象；语义字段请看同 seq source_object_before/after");
        }

        var response = Node(0, data.Length, "struct", "CResponseStartGamePara_Wire", $"{data.Length:N0} bytes", "Partial",
            "S→C 响应；按官服 CResponseStartGamePara print order 解析确定的 result/reason，后续 small wire 附加区 raw 保留");
        response.AddChild(Node(0, 0, "note", "Evidence", "CResponseStartGamePara", "Verified",
            @"Network strings_redacted.csv has CResponseStartGamePara; Network.FULL.all_funcs.c print order: m_nResultID/m_bReasonLen/m_szReason/m_uchHasNoVipStartGameInfo"));
        var start = AddLengthPrefix(response, data, "ResponseLen", "110 samples have small be:u16 declared length; bytes after reason may be packed no-vip/start info");
        var body = response.AddChild(Node(start, Math.Max(0, data.Length - start), "struct", "CResponseStartGamePara", "", "Partial", "wire multi-byte fields use BE"));
        var cursor = start;
        AddU16BE(body, data, cursor, "m_nResultID", "Verified", "");
        cursor += 2;
        var reasonLen = CanRead(data, cursor, 1) ? U8(data, cursor) : 0;
        AddU8(body, data, cursor++, "m_bReasonLen", "Verified", "");
        if (CanRead(data, cursor, reasonLen))
        {
            body.AddChild(Node(cursor, reasonLen, "bytes/string", "m_szReason", DecodeText(data.AsSpan(cursor, reasonLen)), "Dynamic", reasonLen == 0 ? "ReasonLen=0" : "length controlled by m_bReasonLen"));
            cursor += reasonLen;
        }

        if (cursor < data.Length)
        {
            body.AddChild(Node(cursor, data.Length - cursor, "raw", "PackedStartGameExtra", Hex(data, cursor, data.Length - cursor), "UnknownName",
                "官服 expanded print 里 m_uchHasNoVipStartGameInfo 在 +0xCB；当前 inner_dispatch_body 是小 wire 形态，不能按 expanded offset 强套"));
        }

        return new ParserStatus
        {
            Status = "parsed-partial",
            Note = $"MsgID 110 响应已解析确定字段；extra 仍待按官服 accessor 继续展开。direction={packet.Direction}",
            StructureText = "Msg110 response structure tree loaded.",
            RootNodes = [response]
        };
    }

    private static ParserStatus ParseMsg120(byte[] data, PacketRecord packet)
    {
        if (IsSafeSourceObject(packet.Direction))
        {
            var root = Node(0, data.Length, "struct", "RequestPrepareReady120_SourceBufferPreview", $"{data.Length:N0} bytes", "Partial",
                "C→S 请求/发送；这是 Top-Kart 传给 28346 builder 的 source buffer 附近 512B 预览，不是 Network.dll CSSafe native object。0x00-0x2D 是高置信 source 字段；0x30 之后可能包含相邻栈工作缓冲，只作证据不强行当 wire 字段。");
            root.AddChild(Node(0, 0, "note", "Evidence", "Top-Kart sub_1807D6A50 / sub_18073BC30 / Network CSSafe constructor", "Verified",
                @"Top-Kart computes map/checkpoint/TerrainProp hashes, writes v50/v39 source buffer, then sub_180BDD400(120,...). Network.dll CSSafeCRequestPrepareReadyPara constructor only proves protected object layout after builder, not this plain source preview."));
            AddU32LE(root, data, 0x00, "m_uiUin", "Dynamic", "");
            AddU32LE(root, data, 0x04, "m_uiTime", "Dynamic", "");
            AddI32LE(root, data, 0x08, "m_iMapID", "Dynamic", "样本与 Map/MapXXX/checkpoint.luc 路径一致");
            AddAscii(root, data, 0x0C, 32, "m_szReportHash", "Dynamic", "Top-Kart 用三个资源 hash 计算 total hash 后 sprintf(\"%X\") 写入 32-byte reportHash；样本显示 8 位十六进制字符串");
            AddU8(root, data, 0x2C, "m_ucPrepareReadyFlag", "UnknownName", "Top-Kart 局部变量 v53/v41；来自 a5 或 LevelStream 分支，语义未完全确认");
            AddU8(root, data, 0x2D, "m_ucPvpOBOrRuntimeFlag", "UnknownName", "Top-Kart 局部变量 v54/v42 = sub_18094FC20(...)，语义未完全确认");
            if (CanRead(data, 0x2E, 2))
            {
                root.AddChild(Node(0x2E, 2, "raw", "m_reservedAfterReadyFlags", Hex(data, 0x2E, 2), "Reserved", "当前样本为 0/对齐空洞"));
            }

            var stackEvidence = root.AddChild(Node(0x30, Math.Min(0x170, Math.Max(0, data.Length - 0x30)), "group", "HashAndPathStackEvidence", "", "Evidence",
                "0x30 之后在 512B dump 中稳定出现 load-time/hash/path，但反编译显示这些可能是 source buffer 后面的相邻栈变量；保留为证据，不标成已确认协议字段。"));
            AddU32LE(stackEvidence, data, 0x30, "m_unNormalLoadTime_candidate", "UnknownName", "多 dump 稳定为 10000000，但 21273 source buffer offset 尚未与 builder accessor 闭环");
            AddU32LE(stackEvidence, data, 0x38, "m_dwMapFileHash_candidate", "Evidence", "Top-Kart 会计算当前 map 资源文件 hash；该 offset 与样本变化吻合，但可能是相邻栈工作变量");
            AddAscii(stackEvidence, data, 0x40, 32, "m_szSecondaryHash_candidate", "Evidence", "样本为第二个 8 位十六进制 hash 字符串；具体对应 checkpoint 或 TerrainProp 还需动态 accessor 证明");
            AddU32LE(stackEvidence, data, 0x58, "m_uiCheckpointPathLength_candidate", "Evidence", "样本为 59；后面出现 checkpoint.luc 路径");
            AddU32LE(stackEvidence, data, 0x60, "m_uiCheckpointPathCapacity_candidate", "Evidence", "样本为 63；像 string capacity/SSO 旁路证据，不按协议字段确认");
            AddFixedString(stackEvidence, data, 0x68, Math.Min(0x80, Math.Max(0, data.Length - 0x68)), "m_szCheckpointLucPath_stack", "ASCII path from Top-Kart hash calculation stack preview");
            AddFixedString(stackEvidence, data, 0x178, Math.Min(0x80, Math.Max(0, data.Length - 0x178)), "m_szTerrainPropTabLucPath_stack", "ASCII path from Top-Kart hash calculation stack preview");
            AddTrailing(root, data, 0x1F8, "SourceBufferPreviewTail", "dump 是 512B preview；后段不作为 Msg120 协议字段命名");

            return new ParserStatus
            {
                Status = "parsed-partial",
                Note = $"MsgID 120 请求 source buffer 已按 21273 Top-Kart hash/ready 链路修正；0x30+ 只作 stack evidence，不冒充确定字段。direction={packet.Direction}",
                StructureText = "Msg120 request source object structure tree loaded.",
                RootNodes = [root]
            };
        }

        if (IsSafeCompact(packet.Direction))
        {
            return ParseSafeCompact(120, "CSSafeCRequestPrepareReadyPara", data, packet,
                "compact CSSafe 120 body；不能直接 cast 成 native 对象；语义字段请看同 seq source_object_before/after");
        }

        var response = Node(0, data.Length, "struct", "CResponsePrepareReadyPara_Wire", $"{data.Length:N0} bytes", "Partial",
            "S→C 响应；如果出现 inner_dispatch_body_msg120，可按官服 CResponsePrepareReadyPara 前部字段解析");
        response.AddChild(Node(0, 0, "note", "Evidence", "CResponsePrepareReadyPara", "Verified",
            @"Network strings_redacted.csv has CResponsePrepareReadyPara and CSSafeCResponsePrepareReadyPara; print order: m_unUin/m_usResultID/m_iMapID/m_ucIsPvpOB/m_uiPara1"));
        var start = AddLengthPrefix(response, data, "ResponseLen", "optional be:u16 length prefix");
        var body = response.AddChild(Node(start, Math.Max(0, data.Length - start), "struct", "CResponsePrepareReadyPara", "", "Partial", "wire multi-byte fields use BE"));
        AddU32BE(body, data, start, "m_unUin", "Dynamic", "");
        AddU16BE(body, data, start + 4, "m_usResultID", "Verified", "");
        AddI32BE(body, data, start + 6, "m_iMapID", "Dynamic", "");
        AddU8(body, data, start + 10, "m_ucIsPvpOB", "Dynamic", "");
        AddU32BE(body, data, start + 11, "m_uiPara1", "UnknownName", "字段名来自官服 print order，语义由模式决定");
        AddTrailing(body, data, start + 15, "TrailingBytesAfterPrepareReadyResponse", "响应字段后剩余字节未确认");

        return new ParserStatus
        {
            Status = "parsed-partial",
            Note = $"MsgID 120 响应/候选已接入 CResponsePrepareReadyPara 前部字段；direction={packet.Direction}",
            StructureText = "Msg120 response structure tree loaded.",
            RootNodes = [response]
        };
    }

    private static ParserStatus ParseSafeCompact(int msgId, string className, byte[] data, PacketRecord packet, string note)
    {
        var root = Node(0, data.Length, "struct", $"{className}_CompactWire", $"{data.Length:N0} bytes", "UnknownName", note);
        root.AddChild(Node(0, 0, "note", "Evidence", "CSSafe compact body inside 28346", "Verified",
            @"F:\new1\tools\qqspeed_race_flow_structs.hpp notes 110/120 current captures use CSSafe compact payload; m_acBuffer cannot be directly reinterpreted as native semantic object"));
        root.AddChild(Node(0, Math.Min(64, data.Length), "bytes", "CompactPayloadHead", Hex(data, 0, Math.Min(64, data.Length)), "UnknownName",
            "需要恢复 class-specific CSSafe serializer/deserializer 后才能继续按字段拆"));
        if (data.Length > 64)
        {
            root.AddChild(Node(Math.Max(0, data.Length - 32), Math.Min(32, data.Length), "bytes", "CompactPayloadTail", Hex(data, Math.Max(0, data.Length - 32), Math.Min(32, data.Length)), "UnknownName", ""));
        }

        return new ParserStatus
        {
            Status = "compact",
            Note = $"MsgID {msgId} 当前文件是 CSSafe compact wire；已展示 raw 和源对象查看提示。direction={packet.Direction}",
            StructureText = $"Msg{msgId} compact CSSafe payload; open source object snapshot for current semantic anchors.",
            RootNodes = [root]
        };
    }

    private static int AddLengthPrefix(ParsedFieldNode root, byte[] data, string name, string note)
    {
        if (!CanRead(data, 0, 2))
        {
            return 0;
        }

        var prefix = U16BE(data, 0);
        if (prefix == data.Length || (prefix >= 2 && prefix <= data.Length))
        {
            root.AddChild(Node(0, 2, "be:u16", name, prefix.ToString(CultureInfo.InvariantCulture), "Verified", note));
            return 2;
        }

        return 0;
    }

    private static void AddDeclaredTrailing(ParsedFieldNode root, byte[] data)
    {
        if (!CanRead(data, 0, 2))
        {
            return;
        }

        var declared = U16BE(data, 0);
        var declaredEnd = declared == data.Length ? declared : declared + 2;
        if (declared > 0 && declaredEnd < data.Length)
        {
            root.AddChild(Node(declaredEnd, data.Length - declaredEnd, "raw", "TrailingBytesAfterDeclaredLen", Hex(data, declaredEnd, data.Length - declaredEnd), "Unaligned",
                "dump 文件大于第一长度字段声明范围；保留原始字节"));
        }
    }

    private static bool IsSafeSourceObject(string direction)
    {
        return direction.Contains("safe28346_builder_src_object", StringComparison.OrdinalIgnoreCase) ||
               direction.Contains("serialize_src_object", StringComparison.OrdinalIgnoreCase) ||
               direction.Contains("topkart_source_object", StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsSafeCompact(string direction)
    {
        return direction.Contains("safe28346_builder_inner_body", StringComparison.OrdinalIgnoreCase) ||
               direction.Contains("special28346_before_inner_body", StringComparison.OrdinalIgnoreCase);
    }

    private static void AddU8(ParsedFieldNode parent, byte[] data, int offset, string name, string status, string note)
    {
        if (CanRead(data, offset, 1))
        {
            parent.AddChild(Node(offset, 1, "u8", name, U8(data, offset).ToString(CultureInfo.InvariantCulture), status, note));
        }
    }

    private static void AddU16BE(ParsedFieldNode parent, byte[] data, int offset, string name, string status, string note)
    {
        if (CanRead(data, offset, 2))
        {
            parent.AddChild(Node(offset, 2, "be:u16", name, U16BE(data, offset).ToString(CultureInfo.InvariantCulture), status, note));
        }
    }

    private static void AddI16BE(ParsedFieldNode parent, byte[] data, int offset, string name, string status, string note)
    {
        if (CanRead(data, offset, 2))
        {
            parent.AddChild(Node(offset, 2, "be:i16", name, I16BE(data, offset).ToString(CultureInfo.InvariantCulture), status, note));
        }
    }

    private static void AddU32BE(ParsedFieldNode parent, byte[] data, int offset, string name, string status, string note)
    {
        if (CanRead(data, offset, 4))
        {
            parent.AddChild(Node(offset, 4, "be:u32", name, U32BE(data, offset).ToString(CultureInfo.InvariantCulture), status, note));
        }
    }

    private static void AddI32BE(ParsedFieldNode parent, byte[] data, int offset, string name, string status, string note)
    {
        if (CanRead(data, offset, 4))
        {
            parent.AddChild(Node(offset, 4, "be:i32", name, I32BE(data, offset).ToString(CultureInfo.InvariantCulture), status, note));
        }
    }

    private static void AddU16LE(ParsedFieldNode parent, byte[] data, int offset, string name, string status, string note)
    {
        if (CanRead(data, offset, 2))
        {
            parent.AddChild(Node(offset, 2, "le:u16", name, U16LE(data, offset).ToString(CultureInfo.InvariantCulture), status, note));
        }
    }

    private static void AddI16LE(ParsedFieldNode parent, byte[] data, int offset, string name, string status, string note)
    {
        if (CanRead(data, offset, 2))
        {
            parent.AddChild(Node(offset, 2, "le:i16", name, I16LE(data, offset).ToString(CultureInfo.InvariantCulture), status, note));
        }
    }

    private static void AddU32LE(ParsedFieldNode parent, byte[] data, int offset, string name, string status, string note)
    {
        if (CanRead(data, offset, 4))
        {
            parent.AddChild(Node(offset, 4, "le:u32", name, U32LE(data, offset).ToString(CultureInfo.InvariantCulture), status, note));
        }
    }

    private static void AddI32LE(ParsedFieldNode parent, byte[] data, int offset, string name, string status, string note)
    {
        if (CanRead(data, offset, 4))
        {
            parent.AddChild(Node(offset, 4, "le:i32", name, I32LE(data, offset).ToString(CultureInfo.InvariantCulture), status, note));
        }
    }

    private static void AddFixedString(ParsedFieldNode parent, byte[] data, int offset, int length, string name, string note)
    {
        if (CanRead(data, offset, length))
        {
            parent.AddChild(Node(offset, length, "bytes/string", name, DecodeText(data.AsSpan(offset, length)), "Dynamic", note));
        }
    }

    private static void AddAscii(ParsedFieldNode parent, byte[] data, int offset, int length, string name, string status, string note)
    {
        if (CanRead(data, offset, length))
        {
            parent.AddChild(Node(offset, length, "ascii", name, DecodeAscii(data.AsSpan(offset, length)), status, note));
        }
    }

    private static void AddRaw(ParsedFieldNode parent, byte[] data, int offset, int length, string name, string note)
    {
        if (length > 0 && CanRead(data, offset, length))
        {
            parent.AddChild(Node(offset, length, "raw", name, Hex(data, offset, Math.Min(length, 96)), "UnknownName", note));
        }
    }

    private static void AddTrailing(ParsedFieldNode parent, byte[] data, int offset, string name, string note)
    {
        if (offset < data.Length)
        {
            parent.AddChild(Node(offset, data.Length - offset, "raw", name, Hex(data, offset, Math.Min(data.Length - offset, 96)), "UnknownName", note));
        }
    }

    private static ParserStatus Unsupported(PacketRecord packet, string note)
    {
        return new ParserStatus
        {
            Status = "unsupported",
            Note = note,
            StructureText = "Unsupported room-flow packet.",
            RootNodes = [Node(0, 0, "parser", "UnsupportedRoomFlowPacket", $"MsgID {packet.MsgIdDisplay}", "Unaligned", note)]
        };
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

    private static bool CanRead(byte[] data, int offset, int count)
    {
        return offset >= 0 && count >= 0 && offset <= data.Length && data.Length - offset >= count;
    }

    private static byte U8(byte[] data, int offset)
    {
        return data[offset];
    }

    private static ushort U16BE(byte[] data, int offset)
    {
        return BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(offset, 2));
    }

    private static short I16BE(byte[] data, int offset)
    {
        return BinaryPrimitives.ReadInt16BigEndian(data.AsSpan(offset, 2));
    }

    private static uint U32BE(byte[] data, int offset)
    {
        return BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(offset, 4));
    }

    private static int I32BE(byte[] data, int offset)
    {
        return BinaryPrimitives.ReadInt32BigEndian(data.AsSpan(offset, 4));
    }

    private static ushort U16LE(byte[] data, int offset)
    {
        return BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(offset, 2));
    }

    private static short I16LE(byte[] data, int offset)
    {
        return BinaryPrimitives.ReadInt16LittleEndian(data.AsSpan(offset, 2));
    }

    private static uint U32LE(byte[] data, int offset)
    {
        return BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(offset, 4));
    }

    private static int I32LE(byte[] data, int offset)
    {
        return BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(offset, 4));
    }

    private static string DecodeText(ReadOnlySpan<byte> bytes)
    {
        var trimmed = TrimAtZero(bytes);
        if (trimmed.Length == 0)
        {
            return "";
        }

        try
        {
            return Encoding.GetEncoding(936).GetString(trimmed).TrimEnd('\0', ' ');
        }
        catch
        {
            return DecodeAscii(trimmed);
        }
    }

    private static string DecodeAscii(ReadOnlySpan<byte> bytes)
    {
        var trimmed = TrimAtZero(bytes);
        if (trimmed.Length == 0)
        {
            return "";
        }

        return Encoding.ASCII.GetString(trimmed).TrimEnd('\0', ' ');
    }

    private static ReadOnlySpan<byte> TrimAtZero(ReadOnlySpan<byte> bytes)
    {
        var end = bytes.IndexOf((byte)0);
        return end >= 0 ? bytes[..end] : bytes;
    }

    private static string Hex(byte[] data, int offset, int length)
    {
        if (length <= 0 || !CanRead(data, offset, length))
        {
            return "";
        }

        var builder = new StringBuilder(length * 3);
        for (var i = 0; i < length; i++)
        {
            if (i > 0)
            {
                builder.Append(' ');
            }

            builder.Append(data[offset + i].ToString("X2", CultureInfo.InvariantCulture));
        }

        return builder.ToString();
    }
}
