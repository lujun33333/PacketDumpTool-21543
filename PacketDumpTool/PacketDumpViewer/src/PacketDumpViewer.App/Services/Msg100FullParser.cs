using System.Collections.ObjectModel;
using System.IO;
using System.Text;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class Msg100FullParser
{
    public const int FullLengthCurrent3805 = 317329;

    private const int LoginPlainMinLen = 0x35;
    private const int PlayerBaseOffset = 0x35;
    private const int ItemNumOffset = 0x156;
    private const int ItemArrayOffset = 0x158;
    private const int ItemStride = 23;
    private const int RecomCountryOffset = 0x33BB8;
    private const int ServerTimeOffset = 0x33BB9;
    private const int ReasonLenOffset = 0x33BBD;
    private const int MapNumOffset = 0x33D8C;
    private const int MapArrayOffset = 0x33D8E;
    private const int MapStride = 12;
    private const int PetNumOffset = 0x354FE;
    private const int PetArrayOffset = 0x35500;
    private const int PetStride = 191;
    private const int TailOffset = 0x4CA0C;
    private const int OverdueNumOffset = 0x4CA2E;
    private const int OverdueArrayOffset = 0x4CA2F;
    private const int OverdueStride = 12;
    private const int PostOverdueOffset = 0x4CEDF;
    private const int FilterHasMoreInfoOffset = 315168; // 0x4CF20
    private const int ExtendItemNumOffset = 0x4CF30;
    private const int PostExtendTailOffset = 315187; // 0x4CF33
    private const int PersonalGardenOffset = 315260; // 0x4CF7C
    private const int ConsumeVipOffset = 315283; // 0x4CF93
    private const int EmperorInfoOffset = 315304; // 0x4CFA8
    private const int EmperorOtherInfoOffset = 315314; // 0x4CFB2
    private const int RankedMatchGradeInfoOffset = 315392; // 0x4D000
    private const int TopKartStringBlock1LenOffset = 315421; // 0x4D01D
    private const int TopKartStringBlock1Offset = 315422; // 0x4D01E
    private const int TopKartStringBlock2LenOffset = 315622; // 0x4D0E6
    private const int TopKartStringBlock2Offset = 315623; // 0x4D0E7
    private const int IpCityLenOffset = 315832; // 0x4D1B8
    private const int IpCityOffset = 315833; // 0x4D1B9
    private const int TopKartStringBlock3LenOffset = 316033; // 0x4D281
    private const int TopKartStringBlock3Offset = 316034; // 0x4D282
    private const int TopKartStringBlock4LenOffset = 316098; // 0x4D2C2
    private const int TopKartStringBlock4Offset = 316099; // 0x4D2C3
    private const int TopKartFinalBufferOffset = 316305; // 0x4D391
    private const int TaskCountObjectOffset = 0x310328;
    private const int TaskArrayObjectOffset = 0x310330;
    private const int TaskSafeObjectStride = 320;
    private const int TaskSafeObjectCapacity = 1100;
    private const int TaskWireRecordStride = 24;
    private static readonly int[] PlayerBaseHeadU32Indexes = Enumerable.Range(0, 23).ToArray();
    private static readonly string[] PlayerBaseHeadU32Names =
    [
        "m_stPlayerDBBaseInfo.m_iLicense",
        "m_stPlayerDBBaseInfo.m_unExperience",
        "m_stPlayerDBBaseInfo.m_iSuperMoney",
        "m_stPlayerDBBaseInfo.m_iMoney",
        "m_stPlayerDBBaseInfo.m_unWinNum",
        "m_stPlayerDBBaseInfo.m_uiSecondNum",
        "m_stPlayerDBBaseInfo.m_uiThirdNum",
        "m_stPlayerDBBaseInfo.m_unTotalNum",
        "m_stPlayerDBBaseInfo.m_iCurHonor",
        "m_stPlayerDBBaseInfo.m_uiTotalHonor",
        "m_stPlayerDBBaseInfo.m_iTodayHonor",
        "m_stPlayerDBBaseInfo.m_iRelaxTime",
        "m_stPlayerDBBaseInfo.m_uiMonthDurationBefore",
        "m_stPlayerDBBaseInfo.m_uiMonthDurationCur",
        "m_stPlayerDBBaseInfo.m_uiCharm",
        "m_stPlayerDBBaseInfo.m_uiDurationGame",
        "m_stPlayerDBBaseInfo.m_uiDanceExp",
        "m_stPlayerDBBaseInfo.m_iCoupons",
        "m_stPlayerDBBaseInfo.m_uiAdmiration",
        "m_stPlayerDBBaseInfo.m_iLuckMoney",
        "m_stPlayerDBBaseInfo.m_uiTeamWorkExp",
        "m_stPlayerDBBaseInfo.m_uiAchievePoint",
        "m_stPlayerDBBaseInfo.m_uiRegTime"
    ];

    private static readonly string[] PlayerBaseTailFieldNames =
    [
        "m_stPlayerDBBaseInfo.m_uiLuckyMatchPoint",
        "m_stPlayerDBBaseInfo.m_uiLuckyMatchNum",
        "m_stPlayerDBBaseInfo.m_ushLuckyMatchFirstNum",
        "m_stPlayerDBBaseInfo.m_bLuckyMatchBestRand",
        "m_stPlayerDBBaseInfo.m_uiLuckyMatchWinNum",
        "m_stPlayerDBBaseInfo.m_uiFizzPointTotal",
        "m_stPlayerDBBaseInfo.m_ushFizzPointDaily",
        "m_stPlayerDBBaseInfo.m_ushFizzPointWeekly",
        "m_stPlayerDBBaseInfo.m_ushFizzPointLastWeek",
        "m_stPlayerDBBaseInfo.m_bFizzLotteryStatus",
        "m_stPlayerDBBaseInfo.m_uiFizzLastUpdateTime",
        "m_stPlayerDBBaseInfo.m_u32SNSLocaleCode",
        "m_stPlayerDBBaseInfo.m_i32AuctionRightUnlockTime",
        "m_stPlayerDBBaseInfo.m_ucReserveFlag",
        "m_stPlayerDBBaseInfo.m_i32CurConsumeScore",
        "m_stPlayerDBBaseInfo.m_i32HistoryConsumeScore",
        "m_stPlayerDBBaseInfo.m_u8CrashModeSponsorID",
        "m_stPlayerDBBaseInfo.m_ui32Popularity",
        "m_stPlayerDBBaseInfo.m_uiLadderMatchAchievePoint",
        "m_stPlayerDBBaseInfo.m_u32Cash",
        "m_stPlayerDBBaseInfo.m_uiPlayerGPMIdentity",
        "m_stPlayerDBBaseInfo.m_uiPointTotalScore",
        "m_stPlayerDBBaseInfo.m_totalMapMedalNum",
        "m_stPlayerDBBaseInfo.m_lastMapMedalNum",
        "m_stPlayerDBBaseInfo.m_curMapMedalNum",
        "m_stPlayerDBBaseInfo.m_curMapMedalSeasonId",
        "m_stPlayerDBBaseInfo.m_lastClearMapMedalTime",
        "m_stPlayerDBBaseInfo.m_max2048Score",
        "m_stPlayerDBBaseInfo.m_curPlay2048LeftNum",
        "m_stPlayerDBBaseInfo.m_uiCheerValue",
        "m_stPlayerDBBaseInfo.m_ushSeasonID",
        "m_stPlayerDBBaseInfo.m_uiLastCheerValue",
        "m_stPlayerDBBaseInfo.m_iSpeedBean",
        "m_stPlayerDBBaseInfo.m_iSpeedCoin"
    ];

    private static readonly byte[] PlayerBaseTailFieldSizes =
    [
        4, 4, 2, 1, 4, 4, 2, 2, 2, 1, 4, 4, 4, 1, 4, 4, 1,
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 2, 4, 4, 4
    ];

    static Msg100FullParser()
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
    }

    public ParserStatus Parse(PacketRecord packet, bool compareSiblings)
    {
        if (!IsMsg100FullCandidate(packet))
        {
            return new ParserStatus
            {
                Status = "unsupported",
                Note = "MsgID 100 只解析 len317329 的 full 形态；body/serialize 仅作辅助证据。",
                StructureText = "Not a MsgID 100 full payload."
            };
        }

        if (!File.Exists(packet.FullPath))
        {
            return new ParserStatus
            {
                Status = "file missing",
                Note = "payload 文件不存在。",
                StructureText = "Payload file missing."
            };
        }

        var data = File.ReadAllBytes(packet.FullPath);
        var result = ParseBytes(data, Path.GetFileName(packet.FullPath), packet.Direction);

        if (compareSiblings)
        {
            var comparison = new MultiDumpComparisonService(this).CompareSiblingDumps(packet, result.RootNodes);
            return new ParserStatus
            {
                Status = comparison.Status,
                Note = comparison.Note,
                StructureText = comparison.StructureText,
                RootNodes = result.RootNodes
            };
        }

        MarkSingleSample(result.RootNodes);
        return result;
    }

    public ParserStatus ParseBytes(byte[] data, string sourceName, string sourceDirection = "")
    {
        var isCacheView = sourceDirection.Contains("login_cache_decode", StringComparison.OrdinalIgnoreCase) ||
                          sourceName.Contains("login_cache_decode", StringComparison.OrdinalIgnoreCase);
        var root = new ParsedFieldNode
        {
            Offset = 0,
            Size = data.Length,
            Type = "struct",
            Name = "CSSafeCResponseLoginPara",
            Value = $"{data.Length:N0} bytes",
            Status = data.Length == FullLengthCurrent3805 ? "SingleSample" : "Unaligned",
            Note = data.Length == FullLengthCurrent3805
                ? isCacheView
                    ? "full offset; body offset = full offset - 0x0E; login_cache_decode 是过滤/缓存视图，结构偏移可用，但道具/宠物数量可能已被 cache merge 改写"
                    : "full offset; body offset = full offset - 0x0E; standard full sample"
                : $"expected {FullLengthCurrent3805:N0} bytes; source={sourceName}"
        };
        AddParseEvidenceNotes(root, data, sourceName, sourceDirection);
        AddTaskBlockEvidence(root);

        if (data.Length < LoginPlainMinLen)
        {
            root.AddChild(Node(0, data.Length, "raw", "TruncatedPayload", Hex(data, 0, Math.Min(data.Length, 32)), "Unaligned", "payload shorter than login header"));
            return new ParserStatus
            {
                Status = "unaligned",
                Note = "Msg100 payload 太短，无法解析。",
                StructureText = "Truncated Msg100 payload.",
                RootNodes = [root]
            };
        }

        AddLoginHeader(root, data);
        AddPlayerBase(root, data);
        AddItems(root, data);
        AddMiddleBlocks(root, data);
        AddMaps(root, data);
        AddPets(root, data);
        AddTail(root, data);
        AddTailGroups(root, data);

        return new ParserStatus
        {
            Status = data.Length == FullLengthCurrent3805 ? "parsed" : "parsed with warning",
            Note = data.Length == FullLengthCurrent3805
                ? "MsgID 100 full parser；点击“对照同级dump”可标注 Verified/Dynamic/Unaligned。"
                : "长度不是 current-3805 full 标准，字段可能错位。",
            StructureText = "Msg100 structure tree loaded.",
            RootNodes = [root]
        };
    }

    public static bool IsMsg100FullCandidate(PacketRecord packet)
    {
        if (packet.MsgId != 100 || packet.DeclaredLength != FullLengthCurrent3805)
        {
            return false;
        }

        return packet.Direction.Contains("recv_plain_full", StringComparison.OrdinalIgnoreCase) ||
               packet.Direction.Contains("special28346_after_full", StringComparison.OrdinalIgnoreCase) ||
               packet.Direction.Contains("login_cache_decode_before", StringComparison.OrdinalIgnoreCase) ||
               packet.Direction.Contains("login_cache_decode_after", StringComparison.OrdinalIgnoreCase) ||
               packet.Direction.Contains("online_recv_full", StringComparison.OrdinalIgnoreCase) ||
               packet.Direction.Contains("plain-recv-full", StringComparison.OrdinalIgnoreCase);
    }

    private static void AddParseEvidenceNotes(ParsedFieldNode root, byte[] data, string sourceName, string sourceDirection)
    {
        var status = data.Length == FullLengthCurrent3805 ? "SingleSample" : "Unaligned";
        root.AddChild(Node(
            0,
            data.Length,
            "coverage",
            "FullPayloadCoverage",
            $"covered=0x00000000-0x{Math.Max(0, data.Length - 1):X8}; len={data.Length:N0}/0x{data.Length:X}; expected={FullLengthCurrent3805:N0}/0x{FullLengthCurrent3805:X}",
            status,
            data.Length == FullLengthCurrent3805
                ? "已按 317329 bytes full payload 作为结构起点；317315 bytes body 只能辅助验证，body offset = full offset - 0x0E"
                : "不是 current-3805 标准 full 长度，后续字段可能错位"));

        root.AddChild(Node(
            0,
            0,
            "note",
            "UnknownFieldPolicy",
            "字段优先显示 Network.dll / 反编译里的 C++ 成员路径；UnknownName=offset/size/raw value known, meaning not proven",
            "SingleSample",
            "例如 m_astPlayerDBTaskInfo[].m_iTaskID 这种格式；没有证据的字段不自造业务名"));

        root.AddChild(Node(
            0,
            0,
            "note",
            "EvidenceSources",
            $"source={sourceName}; direction={sourceDirection}; name-basis=F:\\new1\\dll\\new\\21273\\Network.dll strings/RTTI + Top-Kart.dll Lua export; offset-basis=full dump + Network/Top-Kart object offsets",
            "SingleSample",
            "字段名优先来自 Network.dll debug strings / 反编译对象名；Top-Kart Lua export/消费点只在有 a2+offset 或构造边界时用于证明 full offset"));

        root.AddChild(Node(
            0,
            0,
            "note",
            "OfficialZeroCountPolicy",
            "count=0 means empty array; do not synthesize element[0] fields",
            "Verified",
            "官服结构表达：数量字段为 0 时，数组为空；只有已证明 full 固定槽位的区域才显示 ReservedSlots，动态数组不生成假元素"));
    }

    private static void AddTaskBlockEvidence(ParsedFieldNode root)
    {
        var group = root.AddChild(Node(
            0,
            0,
            "evidence",
            "m_astPlayerDBTaskInfo[]",
            $"before_inner obj+0x{TaskCountObjectOffset:X} taskNum=0 in current decodable dumps; obj+0x{TaskArrayObjectOffset:X} array capacity={TaskSafeObjectCapacity}",
            "Verified",
            "官服 count=0 表示空任务数组；字段名/记录顺序已由 21273 Network.dll / IDA export 证明；非零记录的 full offset 仍需 taskNum>0 样本"));

        group.AddChild(Node(
            0,
            0,
            "count",
            "m_shTaskNum",
            $"0; before_inner object offset obj+0x{TaskCountObjectOffset:X}; not a full payload offset",
            "Verified",
            "count 字段已解码为 0；这表示没有序列化任何 m_astPlayerDBTaskInfo[i] 实例"));

        group.AddChild(Node(
            0,
            0,
            "array",
            "m_astPlayerDBTaskInfo[0..]",
            "count=0; no task records serialized in current dumps",
            "EmptyArray",
            "不能把不存在的 record 填成 m_astPlayerDBTaskInfo[0].m_iTaskID=0；那会错误表示存在第 0 条任务"));

        var template = group.AddChild(Node(
            0,
            TaskWireRecordStride,
            "template",
            "m_astPlayerDBTaskInfo[i] record layout",
            $"record stride={TaskWireRecordStride}; applies only when m_shTaskNum > 0",
            "Template",
            "这是 record-relative 模板，不是当前 full payload 中已经存在的数据"));

        AddTaskFieldEvidence(template, 0x00, 4, "le:i32", "m_astPlayerDBTaskInfo[i].m_iTaskID");
        AddTaskFieldEvidence(template, 0x04, 2, "le:i16", "m_astPlayerDBTaskInfo[i].m_shOptionID");
        AddTaskFieldEvidence(template, 0x06, 1, "i8", "m_astPlayerDBTaskInfo[i].m_cStatus");
        AddTaskFieldEvidence(template, 0x07, 1, "u8", "m_astPlayerDBTaskInfo[i].m_bPrevStatus");
        AddTaskFieldEvidence(template, 0x08, 4, "le:i32", "m_astPlayerDBTaskInfo[i].m_iProgressRate");
        AddTaskFieldEvidence(template, 0x0C, 4, "le:i32", "m_astPlayerDBTaskInfo[i].m_iOther");
        AddTaskFieldEvidence(template, 0x10, 4, "le:u32", "m_astPlayerDBTaskInfo[i].m_uiTakeTime");
        AddTaskFieldEvidence(template, 0x14, 4, "le:u32", "m_astPlayerDBTaskInfo[i].m_uiLastUpdateTime");
    }

    private static void AddTaskFieldEvidence(ParsedFieldNode parent, int recordOffset, int size, string type, string name)
    {
        parent.AddChild(Node(
            0,
            size,
            type,
            name,
            $"record+0x{recordOffset:X2}; record stride={TaskWireRecordStride}; safe-object stride={TaskSafeObjectStride}",
            "Template",
            "字段名来自 Network.dll debug strings；顺序来自 sub_1800D2CB0/CNotifyAddTaskInfoPara 打印循环；full offset 待非零 task dump 证明"));
    }

    private static void AddLoginHeader(ParsedFieldNode root, byte[] data)
    {
        var group = root.AddChild(Group(0x00, LoginPlainMinLen, "LoginHeader", "登录头部基础字段"));
        Add(group, data, 0x00, 2, "le:u16", "ResultID", U16(data, 0x00).ToString());
        Add(group, data, 0x02, 1, "u8", "KeyGameDataLength", U8(data, 0x02).ToString());
        Add(group, data, 0x03, 16, "bytes", "KeyGameData", Hex(data, 0x03, 16), "Verified", "敏感性较低的会话 key 明文字节；仅本地查看器显示");
        Add(group, data, 0x13, 16, "bytes", "ReservedAfterKey", Hex(data, 0x13, 16), "Reserved", "官服 full plain 中 Key 后固定保留区");
        Add(group, data, 0x23, 4, "le:u32", "Uin", U32(data, 0x23).ToString());
        Add(group, data, 0x27, 4, "le:u32", "Identity", $"0x{U32(data, 0x27):X8}");
        Add(group, data, 0x2B, 2, "le:u16", "ConnID", U16(data, 0x2B).ToString());
        Add(group, data, 0x2D, 4, "le:u32", "LoginType", U32(data, 0x2D).ToString());
        Add(group, data, 0x31, 4, "le:u32", "GameSvrIdentity", U32(data, 0x31).ToString());
    }

    private static void AddPlayerBase(ParsedFieldNode root, byte[] data)
    {
        var nickname = TextValue(data, PlayerBaseOffset, 17);
        var signature = TextValue(data, 0xA4, 64);
        var group = root.AddChild(Node(
            PlayerBaseOffset,
            ItemNumOffset - PlayerBaseOffset,
            "struct",
            "m_stPlayerDBBaseInfo",
            $"nick={nickname}; gender={U8(data, 0x46)}; country={U8(data, 0x47)}; signature={signature}",
            "SingleSample",
            "字段名来自 Network.dll debug strings；full offset 仍以官服 dump 为准"));
        var cursor = PlayerBaseOffset;

        Add(group, data, cursor, 17, "gbk[17]", "m_stPlayerDBBaseInfo.m_szNickname", nickname);
        cursor += 17;
        AddCursor(group, data, ref cursor, 1, "u8", "m_stPlayerDBBaseInfo.m_bGender");
        AddCursor(group, data, ref cursor, 1, "u8", "m_stPlayerDBBaseInfo.m_bCountry");

        for (var i = 0; i < PlayerBaseHeadU32Indexes.Length; i++)
        {
            AddCursor(group, data, ref cursor, 4, "le:u32", PlayerBaseHeadU32Names[i]);
        }

        Add(group, data, cursor, 64, "gbk[64]", "m_stPlayerDBBaseInfo.m_szSignature", signature);
        cursor += 64;

        for (var i = 0; i < PlayerBaseTailFieldSizes.Length; i++)
        {
            var size = PlayerBaseTailFieldSizes[i];
            AddCursor(group, data, ref cursor, size, FieldType(size), PlayerBaseTailFieldNames[i]);
        }

        if (cursor != ItemNumOffset)
        {
            group.Status = "Unaligned";
            group.Note = $"PlayerBase cursor=0x{cursor:X}, expected ItemNum offset=0x{ItemNumOffset:X}";
        }
    }

    private static void AddItems(ParsedFieldNode root, byte[] data)
    {
        var count = U16(data, ItemNumOffset);
        Add(root, data, ItemNumOffset, 2, "le:u16", "m_iItemNum", count.ToString(), "SingleSample", "Network.dll 字符串存在 m_iItemNum；该处为 Msg100 full 中道具数量锚点");

        var arrayEnd = RecomCountryOffset;
        var maxRecords = Math.Max(0, (arrayEnd - ItemArrayOffset) / ItemStride);
        var visibleCount = Math.Min(count, maxRecords);
        var array = root.AddChild(Node(
            ItemArrayOffset,
            arrayEnd - ItemArrayOffset,
            "array",
            "m_astPlayerDBItemInfo[]",
            $"count={count}; visible={visibleCount}; capacity={maxRecords}; stride={ItemStride}",
            "SingleSample",
            "字段名来自 Network.dll: m_astPlayerDBItemInfo[%d].*；full offset; body offset = full offset - 0x0E"));
        AddCountedArrayPolicy(array, "m_astPlayerDBItemInfo[]", "m_iItemNum", count, visibleCount, maxRecords, ItemStride, fixedSlotsKnown: true);
        for (var i = 0; i < visibleCount; i++)
        {
            var offset = ItemArrayOffset + i * ItemStride;
            var itemId = U32(data, offset + 0);
            var itemNum = U32(data, offset + 4);
            var availPeriod = U32(data, offset + 8);
            var status = U8(data, offset + 12);
            var obtainTime = U32(data, offset + 13);
            var otherAttribute = U32(data, offset + 17);
            var itemType = U16(data, offset + 21);
            var itemName = ProtocolNameService.Shared.DescribeItemId(itemId);
            var item = array.AddChild(Node(
                offset,
                ItemStride,
                "group",
                $"m_astPlayerDBItemInfo[{i}]",
                $"ItemID={itemId} ({itemName}), Num={itemNum}, Status={status}, Type={itemType}, Period={PeriodValue(availPeriod)}",
                "SingleSample",
                "Login item record; expand for raw fields"));
            Add(item, data, offset + 0, 4, "le:u32", $"m_astPlayerDBItemInfo[{i}].m_iItemID", ItemIdValue(itemId));
            Add(item, data, offset + 4, 4, "le:u32", $"m_astPlayerDBItemInfo[{i}].m_iItemNum", itemNum.ToString());
            Add(item, data, offset + 8, 4, "le:u32", $"m_astPlayerDBItemInfo[{i}].m_iAvailPeriod", PeriodValue(availPeriod));
            Add(item, data, offset + 12, 1, "u8", $"m_astPlayerDBItemInfo[{i}].m_bStatus", ItemStatusValue(status));
            Add(item, data, offset + 13, 4, "le:u32", $"m_astPlayerDBItemInfo[{i}].m_unObtainTime", TimeValue(obtainTime));
            Add(item, data, offset + 17, 4, "le:u32", $"m_astPlayerDBItemInfo[{i}].m_iOtherAttribute", otherAttribute.ToString());
            Add(item, data, offset + 21, 2, "le:u16", $"m_astPlayerDBItemInfo[{i}].m_ushItemType", itemType.ToString());
        }

        var usedEnd = ItemArrayOffset + visibleCount * ItemStride;
        if (usedEnd < arrayEnd)
        {
            AddRawBlock(array, data, usedEnd, arrayEnd - usedEnd, "m_astPlayerDBItemInfo[].ReservedSlots", $"未使用固定槽位；active_count={visibleCount}, capacity={maxRecords}");
        }
        if (count > maxRecords)
        {
            array.Status = "Unaligned";
            array.Note = $"ItemNum={count} exceeds capacity={maxRecords}";
        }
    }

    private static void AddMiddleBlocks(ParsedFieldNode root, byte[] data)
    {
        Add(root, data, RecomCountryOffset, 1, "u8", "RecomCountry", U8(data, RecomCountryOffset).ToString());
        Add(root, data, ServerTimeOffset, 4, "le:u32", "ServerTime", TimeValue(U32(data, ServerTimeOffset)));
        var reasonLen = U8(data, ReasonLenOffset);
        Add(root, data, ReasonLenOffset, 1, "u8", "ReasonLen", reasonLen.ToString());
        var reasonOffset = ReasonLenOffset + 1;
        var reasonSize = Math.Min(reasonLen, Math.Max(0, MapNumOffset - reasonOffset));
        Add(root, data, reasonOffset, reasonSize, $"bytes[{reasonSize}]", "Reason", TextOrHexValue(data, reasonOffset, reasonSize), "SingleSample", "反编译 plain ResponseLogin 顺序：ReasonLen 后接 Reason；full 展开区域固定占位");

        const int preMapUnknownU32Offset = MapNumOffset - 16;
        if (reasonOffset + reasonSize < preMapUnknownU32Offset)
        {
            AddRawBlock(root, data, reasonOffset + reasonSize, preMapUnknownU32Offset - (reasonOffset + reasonSize), "PlayerRelationInfoExpandedOrPadding", "反编译顺序为 PlayerRelationInfo + 4 个 u32；full 展开中该区域固定占位，非零 run 单独列出");
        }

        Add(root, data, preMapUnknownU32Offset + 0, 4, "le:u32", "PreMapUnknownU32_1", U32(data, preMapUnknownU32Offset + 0).ToString(), "UnknownName", "来自反编译 plain ResponseLogin 顺序；语义未确认");
        Add(root, data, preMapUnknownU32Offset + 4, 4, "le:u32", "PreMapUnknownU32_2", U32(data, preMapUnknownU32Offset + 4).ToString(), "UnknownName", "来自反编译 plain ResponseLogin 顺序；语义未确认");
        Add(root, data, preMapUnknownU32Offset + 8, 4, "le:u32", "PreMapUnknownU32_3", U32(data, preMapUnknownU32Offset + 8).ToString(), "UnknownName", "来自反编译 plain ResponseLogin 顺序；语义未确认");
        Add(root, data, preMapUnknownU32Offset + 12, 4, "le:u32", "PreMapUnknownU32_4", U32(data, preMapUnknownU32Offset + 12).ToString(), "UnknownName", "来自反编译 plain ResponseLogin 顺序；语义未确认");
    }

    private static void AddMaps(ParsedFieldNode root, byte[] data)
    {
        var count = U16(data, MapNumOffset);
        Add(root, data, MapNumOffset, 2, "le:u16", "m_astRecordCount", count.ToString(), "UnknownName", "Network.dll 有 m_astRecord[%d].* 字段名；count 名称未找到直接字符串，暂不伪造官方名");
        var arrayEnd = PetNumOffset;
        var maxRecords = Math.Max(0, (arrayEnd - MapArrayOffset) / MapStride);
        var visibleCount = Math.Min(count, maxRecords);
        var array = root.AddChild(Node(
            MapArrayOffset,
            arrayEnd - MapArrayOffset,
            "array",
            "m_astRecord[]",
            $"count={count}; visible={visibleCount}; capacity={maxRecords}; stride={MapStride}",
            "SingleSample",
            "official fixed map slots"));
        AddCountedArrayPolicy(array, "m_astRecord[]", "m_astRecordCount", count, visibleCount, maxRecords, MapStride, fixedSlotsKnown: true);
        for (var i = 0; i < visibleCount; i++)
        {
            var offset = MapArrayOffset + i * MapStride;
            var mapId = U32(data, offset + 0);
            var record = U32(data, offset + 4);
            var map = array.AddChild(Node(
                offset,
                MapStride,
                "group",
                $"m_astRecord[{i}]",
                $"MapID={mapId}, Record={record}",
                "SingleSample",
                "Map record; expand for raw tail"));
            Add(map, data, offset + 0, 4, "le:u32", $"m_astRecord[{i}].m_i32MapId", mapId.ToString());
            Add(map, data, offset + 4, 4, "le:u32", $"m_astRecord[{i}].m_u32Record", record.ToString());
            Add(map, data, offset + 8, 4, "bytes", $"m_astRecord[{i}].ReservedTail", Hex(data, offset + 8, 4), "Reserved", "官服展开保留尾/模板字节；未找到对应成员名");
        }

        var usedEnd = MapArrayOffset + visibleCount * MapStride;
        if (usedEnd < arrayEnd)
        {
            AddRawBlock(array, data, usedEnd, arrayEnd - usedEnd, "m_astRecord[].ReservedSlots", $"未使用固定槽位；active_count={visibleCount}, capacity={maxRecords}");
        }
        if (count > maxRecords)
        {
            array.Status = "Unaligned";
            array.Note = $"MapNum={count} exceeds capacity={maxRecords}";
        }
    }

    private static void AddPets(ParsedFieldNode root, byte[] data)
    {
        var count = U16(data, PetNumOffset);
        Add(root, data, PetNumOffset, 2, "le:u16", "m_usPetNum", count.ToString());
        var maxRecords = Math.Max(0, (TailOffset - PetArrayOffset) / PetStride);
        var visibleCount = Math.Min(count, maxRecords);
        var array = root.AddChild(Node(
            PetArrayOffset,
            TailOffset - PetArrayOffset,
            "array",
            "m_astPetInfo[]",
            $"count={count}; visible={visibleCount}; capacity={maxRecords}; stride={PetStride}",
            "SingleSample",
            "official fixed pet slots"));
        AddCountedArrayPolicy(array, "m_astPetInfo[]", "m_usPetNum", count, visibleCount, maxRecords, PetStride, fixedSlotsKnown: true);
        for (var i = 0; i < visibleCount; i++)
        {
            var offset = PetArrayOffset + i * PetStride;
            var petId = U32(data, offset + 4);
            var petName = TextValue(data, offset + 8, 24);
            var levelLimit = U16(data, offset + 32);
            var level = U16(data, offset + 34);
            var status = U8(data, offset + 44);
            var skillNum = U16(data, offset + 49);
            var pet = array.AddChild(Node(
                offset,
                PetStride,
                "group",
                $"m_astPetInfo[{i}]",
                $"PetID={petId}, Name={petName}, Level={level}/{levelLimit}, Status={status}, SkillNum={skillNum}",
                "SingleSample",
                "Pet record; fixed 191-byte official slot"));
            Add(pet, data, offset + 0, 4, "le:u32", $"m_astPetInfo[{i}].m_uiUin", U32(data, offset + 0).ToString());
            Add(pet, data, offset + 4, 4, "le:u32", $"m_astPetInfo[{i}].m_iPetId", petId.ToString());
            Add(pet, data, offset + 8, 24, "gbk[24]", $"m_astPetInfo[{i}].m_szPetName", petName);
            Add(pet, data, offset + 32, 2, "le:u16", "LevelLimit", U16(data, offset + 32).ToString());
            Add(pet, data, offset + 34, 2, "le:u16", $"m_astPetInfo[{i}].m_usLevel", U16(data, offset + 34).ToString());
            Add(pet, data, offset + 36, 4, "le:u32", "CurLevelExp", U32(data, offset + 36).ToString());
            Add(pet, data, offset + 40, 4, "le:u32", "CurLevelMaxExp", U32(data, offset + 40).ToString());
            Add(pet, data, offset + 44, 1, "u8", $"m_astPetInfo[{i}].m_ucStatus", status.ToString());
            Add(pet, data, offset + 45, 2, "le:u16", "PL", U16(data, offset + 45).ToString());
            Add(pet, data, offset + 47, 2, "le:u16", "TotalPL", U16(data, offset + 47).ToString());
            Add(pet, data, offset + 49, 2, "le:u16", $"m_astPetInfo[{i}].m_usPetSkillNum", skillNum.ToString());
            AddPetSkillArea(pet, data, offset + 51, i, skillNum);
            Add(pet, data, offset + 75, 104, "bytes", "DefaultArea", BlockSummary(data, offset + 75, 104), "Reserved", "官服模板默认区");
            Add(pet, data, offset + 179, 4, "le:u32", $"m_astPetInfo[{i}].m_iStrengLevel", U32(data, offset + 179).ToString(), "SingleSample", "字段名来自 Network.dll debug strings；offset 需继续用更多样本确认");
            Add(pet, data, offset + 183, 1, "u8", $"m_astPetInfo[{i}].m_ucTopStatus", U8(data, offset + 183).ToString(), "SingleSample", "字段名来自 Network.dll debug strings；offset 需继续用更多样本确认");
            Add(pet, data, offset + 184, 2, "bytes", "TemplateFF", Hex(data, offset + 184, 2), "Reserved", "模板默认 FF FF");
            Add(pet, data, offset + 186, 5, "bytes", $"m_astPetInfo[{i}].CommonTailUnknown", Hex(data, offset + 186, 5), "UnknownName", "Network.dll 还有 stCommonInfo / PKStar / PetSkinID / RoleID 字符串，但该 5 字节尾部未完成对齐");
        }

        var usedEnd = PetArrayOffset + visibleCount * PetStride;
        if (usedEnd < TailOffset)
        {
            AddRawBlock(array, data, usedEnd, TailOffset - usedEnd, "m_astPetInfo[].ReservedSlots", $"未使用固定槽位；active_count={visibleCount}, capacity={maxRecords}");
        }
        if (count > maxRecords)
        {
            array.Status = "Unaligned";
            array.Note = $"PetNum={count} exceeds capacity={maxRecords}";
        }
    }

    private static void AddPetSkillArea(ParsedFieldNode pet, byte[] data, int offset, int petIndex, int skillNum)
    {
        const int skillCapacity = 3;
        const int skillStride = 8;
        var visibleSkillCount = Math.Min(skillNum, skillCapacity);
        var group = pet.AddChild(Node(
            offset,
            24,
            "array",
            $"m_astPetInfo[{petIndex}].m_astPetSkillList[]",
            $"count={skillNum}; visible={visibleSkillCount}; capacity={skillCapacity}; stride={skillStride}",
            skillNum == 0 ? "EmptyArray" : "SingleSample",
            "3 fixed pet skill slots; count=0 means active skill list empty, remaining slots are defaults"));
        AddCountedArrayPolicy(group, $"m_astPetInfo[{petIndex}].m_astPetSkillList[]", "m_usPetSkillNum", skillNum, visibleSkillCount, skillCapacity, skillStride, fixedSlotsKnown: true);
        for (var i = 0; i < skillCapacity; i++)
        {
            var skillOffset = offset + i * skillStride;
            var prefix = $"m_astPetInfo[{petIndex}].m_astPetSkillList[{i}]";
            var slotStatus = i < visibleSkillCount ? "SingleSample" : "Reserved";
            var slotNote = i < visibleSkillCount ? "active pet skill record" : "inactive fixed skill slot; not an active skill";
            var skill = group.AddChild(Group(skillOffset, skillStride, prefix, slotNote, slotStatus));
            Add(skill, data, skillOffset + 0, 2, "le:u16", $"{prefix}.m_usSkillID", U16(data, skillOffset + 0).ToString(), slotStatus, slotNote);
            Add(skill, data, skillOffset + 2, 1, "u8", $"{prefix}.m_ucStatus", U8(data, skillOffset + 2).ToString(), slotStatus, slotNote);
            Add(skill, data, skillOffset + 3, 1, "u8", $"{prefix}.m_cActive", U8(data, skillOffset + 3).ToString(), slotStatus, slotNote);
            Add(skill, data, skillOffset + 4, 4, "le:u32", $"{prefix}.m_iValue", U32(data, skillOffset + 4).ToString(), slotStatus, slotNote);
        }
    }

    private static void AddTail(ParsedFieldNode root, byte[] data)
    {
        var tail = root.AddChild(Group(TailOffset, ExtendItemNumOffset - TailOffset, "TailBeforeExtendItem", "尾部已知字段和未命名 raw block"));
        Add(tail, data, TailOffset + 0, 1, "u8", "IsInTopList", U8(data, TailOffset + 0).ToString());
        Add(tail, data, TailOffset + 1, 4, "le:u32", "LastLoginTime", TimeValue(U32(data, TailOffset + 1)));
        Add(tail, data, TailOffset + 5, 4, "le:u32", "MasterPoint", U32(data, TailOffset + 5).ToString());
        Add(tail, data, TailOffset + 9, 1, "u8", "LoginRealNameFlag", U8(data, TailOffset + 9).ToString());
        Add(tail, data, TailOffset + 10, 24, "bytes", "ExRightFlag", Hex(data, TailOffset + 10, 24), "SingleSample", "字段名/长度来自 ResponseLogin 反编译顺序；bit 语义未逐位拆分");

        var overdueCount = U8(data, OverdueNumOffset);
        Add(tail, data, OverdueNumOffset, 1, "u8", "m_u8OverdueItemNum", overdueCount.ToString(), "Verified", "Network.dll 21273 字符串直接确认：m_u8OverdueItemNum");
        var overdue = tail.AddChild(Group(OverdueArrayOffset, PostOverdueOffset - OverdueArrayOffset, "m_astOverdueItem[]", $"count={overdueCount}; stride={OverdueStride}"));
        var maxOverdue = Math.Max(0, (PostOverdueOffset - OverdueArrayOffset) / OverdueStride);
        var visibleOverdue = Math.Min(overdueCount, maxOverdue);
        AddCountedArrayPolicy(overdue, "m_astOverdueItem[]", "m_u8OverdueItemNum", overdueCount, visibleOverdue, maxOverdue, OverdueStride, fixedSlotsKnown: true);
        for (var i = 0; i < visibleOverdue; i++)
        {
            var offset = OverdueArrayOffset + i * OverdueStride;
            var itemId = U32(data, offset + 0);
            var overdueTime = U32(data, offset + 4);
            var itemName = ProtocolNameService.Shared.DescribeItemId(itemId);
            var item = overdue.AddChild(Node(
                offset,
                OverdueStride,
                "group",
                $"m_astOverdueItem[{i}]",
                $"ItemID={itemId} ({itemName}), OverdueTime={overdueTime}",
                "SingleSample",
                "Overdue item record; expand for raw fields"));
            Add(item, data, offset + 0, 4, "le:u32", $"m_astOverdueItem[{i}].m_iItemID", ItemIdValue(itemId));
            Add(item, data, offset + 4, 4, "le:u32", $"m_astOverdueItem[{i}].m_iOverdueTime", PeriodValue(overdueTime));
            Add(item, data, offset + 8, 4, "le:u32", $"m_astOverdueItem[{i}].m_iOtherAttr", U32(data, offset + 8).ToString());
        }
        var overdueUsedEnd = OverdueArrayOffset + visibleOverdue * OverdueStride;
        if (overdueUsedEnd < PostOverdueOffset)
        {
            AddRawBlock(overdue, data, overdueUsedEnd, PostOverdueOffset - overdueUsedEnd, "m_astOverdueItem[].ReservedSlots", $"未使用固定槽位；active_count={visibleOverdue}, capacity={maxOverdue}");
        }

        Add(tail, data, PostOverdueOffset + 0, 4, "le:u32", "StoneKartNum", U32(data, PostOverdueOffset + 0).ToString());
        Add(tail, data, PostOverdueOffset + 4, 4, "le:u32", "LockedMapID", U32(data, PostOverdueOffset + 4).ToString());
        Add(tail, data, PostOverdueOffset + 8, 4, "le:u32", "TotalGuildProsperity", U32(data, PostOverdueOffset + 8).ToString());
        Add(tail, data, PostOverdueOffset + 12, 4, "le:u32", "ClientStatusContex", U32(data, PostOverdueOffset + 12).ToString());
        Add(tail, data, PostOverdueOffset + 16, 1, "u8", "IsHoneyMoon", U8(data, PostOverdueOffset + 16).ToString());
        Add(tail, data, PostOverdueOffset + 17, 1, "u8", "HasMoreInfoOriginalZero", U8(data, PostOverdueOffset + 17).ToString(), "SingleSample", "旧位置/原始槽；current-3805 authoritative copy is at 0x4CF20");
        Add(tail, data, PostOverdueOffset + 18, 2, "le:u16", "CurSvrIDOriginal", U16(data, PostOverdueOffset + 18).ToString(), "SingleSample", "旧位置 CurSvrID；current-3805 authoritative copy is at 0x4CF21");
        AddRawBlock(tail, data, PostOverdueOffset + 20, FilterHasMoreInfoOffset - (PostOverdueOffset + 20), "PreFilterHasMoreInfo.RawBlock", "官方模板/保留区；非零 run 单独列出，未乱命名");
        Add(tail, data, FilterHasMoreInfoOffset, 1, "u8", "m_u8HasMoreInfo", U8(data, FilterHasMoreInfoOffset).ToString(), "Verified", "Network.dll 字符串有 m_u8HasMoreInfo；0x4CF20 是 current-3805 full offset");
        Add(tail, data, FilterHasMoreInfoOffset + 1, 2, "le:u16", "m_sCurSvrID", U16(data, FilterHasMoreInfoOffset + 1).ToString(), "Verified", "Network.dll 字符串直接确认：m_sCurSvrID");
        Add(tail, data, FilterHasMoreInfoOffset + 3, 4, "le:u32", "m_stNobleInfo.m_u32NobleID", U32(data, FilterHasMoreInfoOffset + 3).ToString());
        Add(tail, data, FilterHasMoreInfoOffset + 7, 1, "u8", "m_stNobleInfo.m_u8NobleLevel", U8(data, FilterHasMoreInfoOffset + 7).ToString());
        Add(tail, data, FilterHasMoreInfoOffset + 8, 4, "le:u32", "m_stNobleInfo.m_u32NoblePoint", U32(data, FilterHasMoreInfoOffset + 8).ToString());
        Add(tail, data, FilterHasMoreInfoOffset + 12, 4, "le:u32", "m_stNobleInfo.m_u32NobleLeftDays", U32(data, FilterHasMoreInfoOffset + 12).ToString());
    }

    private static void AddTailGroups(ParsedFieldNode root, byte[] data)
    {
        Add(root, data, ExtendItemNumOffset, 2, "le:u16", "m_ushExtendItemNum", U16(data, ExtendItemNumOffset).ToString(), "Verified", "Network.dll 字符串直接确认：m_ushExtendItemNum");
        Add(root, data, ExtendItemNumOffset + 2, 1, "u8", "m_u8UpdateOnlineInfoInterval", U8(data, ExtendItemNumOffset + 2).ToString(), "Verified", "Network.dll 字符串直接确认：m_u8UpdateOnlineInfoInterval");

        var tail = root.AddChild(Group(
            PostExtendTailOffset,
            Math.Max(0, data.Length - PostExtendTailOffset),
            "PostExtendTail",
            "按 21273 Network.dll decode 写入 offset + Top-Kart CResponseLoginPara 消费 offset 重排；未能证明名称的范围保留 UnknownName"));

        AddFixedGroupFields(tail, data, PostExtendTailOffset, "m_stGuildVipBaseInfo", "Network.dll strings + decode offset",
        [
            ("m_u8GuildVipLevel", 1),
            ("m_u32GuildVipPoint", 4)
        ]);
        AddFixedGroupFields(tail, data, PostExtendTailOffset + 5, "m_stGuildVipOtherInfo", "Network.dll strings + decode offset",
        [
            ("m_u32GuildVipLeftDays", 4),
            ("m_u8CanReceiveGift", 1)
        ]);

        Add(tail, data, 315197, 1, "u8", "m_u8HaveLDMBaseInfo", U8(data, 315197).ToString(), "Verified", "同族 Network.dll 字符串确认 LDM base info flag；21273 decode 写入 0x04CF3D");
        AddFixedGroupFields(tail, data, 315198, "m_stPlayerLDMBaseInfo[0]", "仅在 m_u8HaveLDMBaseInfo != 0 时有效；固定容量 1，count=0 时不要合成业务值",
        [
            ("m_u8Grade", 1),
            ("m_u32Score", 4),
            ("m_u32MaxScore", 4),
            ("m_u32MaxGrade", 4)
        ], U8(data, 315197) == 0 ? "EmptyArray" : "Verified");

        AddRawBlock(tail, data, 315211, PersonalGardenOffset - 315211, "PostExtendTail.UnknownTopKartSpan_04CF4B_04CF7B", "Top-Kart/Network 构造边界已知，但当前未把每个字段名安全映射到 full offset；先不乱命名");

        AddFixedGroupFields(tail, data, PersonalGardenOffset, "m_astPersonalGardenBaseInfo[0]", "Top-Kart Lua export uses personalGardenBaseInfo at a2+315260",
        [
            ("m_u8GardenLevel", 1),
            ("m_u32GardenPoint", 4),
            ("m_u8GrowRate", 1),
            ("m_u32GardenExpireTime", 4),
            ("m_stSimpleInfo.m_uiWeekPopularity", 4),
            ("m_stSimpleInfo.m_uiTotalPopularity", 4),
            ("m_stSimpleInfo.m_uiLastUpdatePopularityTime", 4),
            ("m_stSimpleInfo.m_ucPrivateType", 1)
        ]);
        AddFixedGroupFields(tail, data, ConsumeVipOffset, "m_astConsumeVipInfo", "Top-Kart Lua export uses m_astConsumeVipInfo at a2+315283",
        [
            ("m_VipLevel", 4),
            ("m_CharmValueOfMonth", 4),
            ("m_SearchTreasureNums", 4),
            ("m_GetTreasureNums", 4),
            ("m_uCharmCareerLevel", 4),
            ("m_bShowConsumeVip", 1)
        ]);
        AddFixedGroupFields(tail, data, EmperorInfoOffset, "m_stEmperorInfo", "Network.dll strings confirm fields; offset follows 21273 object boundary",
        [
            ("m_u8EmperorLevel", 1),
            ("m_u32EmperorPoint", 4),
            ("m_u32EmperorLeftDays", 4),
            ("m_u8EmperorGrowRate", 1)
        ]);
        AddFixedGroupFields(tail, data, EmperorOtherInfoOffset, "m_stEmperorOtherInfo", "Top-Kart Lua export helper confirms field names",
        [
            ("m_u32ExpiredTime", 4),
            ("m_u8ShowExpireTips", 1)
        ]);

        Add(tail, data, 315319, 4, "le:u32", "m_uiShuttleScoreWeek", U32(data, 315319).ToString(), "Verified", "Network.dll string + 21273 decode offset");
        AddRawBlock(tail, data, 315323, 8, "PostExtendTail.UnknownTopKartSpan_04CFBB_04CFC2", "固定边界已知；字段名未安全确认");
        AddFixedGroupFields(tail, data, 315331, "m_stGansterScoreInfo", "Top-Kart Lua export helper confirms field names; object boundary aligns at 0x04CFC3",
        [
            ("m_iGansterSeasonID", 4),
            ("m_uiGansterScore", 4),
            ("m_uiPoliceScore", 4),
            ("m_uiTotalGansterScore", 4)
        ]);
        Add(tail, data, 315347, 4, "le:u32", "m_uiNPCEliminateWinTimes", U32(data, 315347).ToString(), "Verified", "Top-Kart Lua export order: m_uiNPCEliminateWinTimes before m_iOlympicId");
        Add(tail, data, 315351, 4, "le:i32", "m_iOlympicId", U32(data, 315351).ToString(), "Verified", "Top-Kart Lua export order: m_iOlympicId after m_uiNPCEliminateWinTimes");
        AddRawBlock(tail, data, 315355, RankedMatchGradeInfoOffset - 315355, "PostExtendTail.UnknownTopKartSpan_04CFDB_04CFFF", "Top-Kart/Network 构造边界已知；疑似活动/房间/开关类字段，未逐项证明 full offset");

        AddFixedGroupFields(tail, data, RankedMatchGradeInfoOffset, "m_stRankedMatchGradeInfo", "Top-Kart Lua export calls sub_18008B790(a2+315392)",
        [
            ("m_uiGradeLevel", 4),
            ("m_uiChildLevel", 4),
            ("m_uiGradeScore", 4),
            ("m_uiMaxGradeScore", 4),
            ("m_uiTotalScore", 4)
        ]);
        AddRawBlock(tail, data, 315412, TopKartStringBlock1LenOffset - 315412, "PostExtendTail.UnknownTopKartSpan_04D014_04D01C", "这一段 Top-Kart 有消费点，但字段名/语义未完全确认");

        AddFixedStringBlock(tail, data, TopKartStringBlock1LenOffset, TopKartStringBlock1Offset, 200, "TopKartStringBlock_001", "UnknownName", "Network.dll decode 写入 len+bytes；未找到可信成员名，不能命名为 URL/token");
        AddFixedStringBlock(tail, data, TopKartStringBlock2LenOffset, TopKartStringBlock2Offset, 200, "TopKartStringBlock_002", "UnknownName", "Network.dll decode 写入 len+bytes；未找到可信成员名，不能命名为 URL/token");
        Add(tail, data, 315823, 1, "u8", "TopKartLateFlag_04D1B7", U8(data, 315823).ToString(), "UnknownName", "21273 constructor/decode 边界已知；语义未确认");
        Add(tail, data, 315824, 2, "le:u16", "TopKartPreIpCityU16_04D1B8", U16(data, 315824).ToString(), "UnknownName", "21273 constructor/decode 边界已知；语义未确认");
        Add(tail, data, 315826, 1, "u8", "TopKartPreIpCityByte_04D1BA", U8(data, 315826).ToString(), "UnknownName", "21273 constructor/decode 边界已知；语义未确认");
        Add(tail, data, 315827, 4, "le:u32", "TopKartPreIpCityU32_04D1BB", U32(data, 315827).ToString(), "UnknownName", "21273 constructor/decode 边界已知；语义未确认");
        Add(tail, data, 315831, 1, "u8", "TopKartPreIpCityByte_04D1BF", U8(data, 315831).ToString(), "UnknownName", "Network.dll decode 写入 0x04D1BF；紧邻 m_ucIpCityLen");
        Add(tail, data, IpCityLenOffset, 1, "u8", "m_ucIpCityLen", U8(data, IpCityLenOffset).ToString(), "Verified", "Network.dll string + decode offset；Top-Kart Lua export then reads m_szIpCity at a2+315833");
        var ipCityLen = Math.Min((int)U8(data, IpCityLenOffset), 200);
        Add(tail, data, IpCityOffset, 200, "gbk[200]", "m_szIpCity", ipCityLen == 0 ? "<empty>" : TextValue(data, IpCityOffset, ipCityLen), "Verified", "Top-Kart Lua export uses a2+315833 for m_szIpCity");

        AddFixedStringBlock(tail, data, TopKartStringBlock3LenOffset, TopKartStringBlock3Offset, 64, "TopKartStringBlock_003", "UnknownName", "Network.dll decode 写入 len+bytes；未找到可信成员名");
        AddFixedStringBlock(tail, data, TopKartStringBlock4LenOffset, TopKartStringBlock4Offset, 192, "TopKartStringBlock_004", "UnknownName", "Network.dll decode 写入 len+bytes；未找到可信成员名");
        Add(tail, data, 316291, 8, "bytes[8]", "TopKartLateQword_04D37B", Hex(data, 316291, 8), "UnknownName", "21273 constructor/decode 固定边界；语义未确认");
        Add(tail, data, 316299, 4, "le:u32", "TopKartLateU32_04D383", U32(data, 316299).ToString(), "UnknownName", "21273 constructor/decode 固定边界；语义未确认");
        Add(tail, data, 316303, 2, "le:u16", "TopKartLateU16_04D387", U16(data, 316303).ToString(), "UnknownName", "21273 constructor/decode 固定边界；语义未确认");
        Add(tail, data, TopKartFinalBufferOffset, Math.Max(0, data.Length - TopKartFinalBufferOffset), $"bytes[{Math.Max(0, data.Length - TopKartFinalBufferOffset)}]", "TopKartFinalBuffer_04D391", BlockSummary(data, TopKartFinalBufferOffset, Math.Max(0, data.Length - TopKartFinalBufferOffset)), "UnknownName", "固定容量尾部缓冲区；只标边界，不猜业务名");
    }

    private static void AddFixedGroupFields(
        ParsedFieldNode parent,
        byte[] data,
        int offset,
        string name,
        string note,
        (string Name, int Size)[] fields,
        string status = "Verified")
    {
        var size = fields.Sum(field => field.Size);
        var group = parent.AddChild(Group(offset, size, name, note, status));
        var cursor = offset;
        foreach (var field in fields)
        {
            var fieldName = field.Name.Contains('.') ? field.Name : $"{name}.{field.Name}";
            AddCursor(group, data, ref cursor, field.Size, FieldType(field.Size), fieldName, status);
        }
    }

    private static void AddFixedStringBlock(
        ParsedFieldNode parent,
        byte[] data,
        int lenOffset,
        int bufferOffset,
        int capacity,
        string name,
        string status,
        string note)
    {
        var rawLen = U8(data, lenOffset);
        var usedLen = Math.Min(rawLen, capacity);
        Add(parent, data, lenOffset, 1, "u8", $"{name}.Length", rawLen.ToString(), status, note);
        Add(
            parent,
            data,
            bufferOffset,
            capacity,
            $"bytes[{capacity}]",
            name,
            BlockSummary(data, bufferOffset, capacity),
            CountNonZero(data, bufferOffset, capacity) == 0 ? "Reserved" : status,
            $"{note}; used_len={usedLen}, capacity={capacity}");
    }

    private static void AddPersonalGarden(ParsedFieldNode parent, byte[] data, ref int cursor)
    {
        var group = parent.AddChild(Group(cursor, 1 + 4 + 1 + 4 + 4 + 4 + 4 + 1, "m_astPersonalGardenBaseInfo[]", "Personal garden info; field names from Network.dll debug strings"));
        AddCursor(group, data, ref cursor, 1, "u8", "m_astPersonalGardenBaseInfo[].m_u8GardenLevel");
        AddCursor(group, data, ref cursor, 4, "le:u32", "m_astPersonalGardenBaseInfo[].m_u32GardenPoint");
        AddCursor(group, data, ref cursor, 1, "u8", "m_astPersonalGardenBaseInfo[].m_u8GrowRate");
        AddCursor(group, data, ref cursor, 4, "le:u32", "m_astPersonalGardenBaseInfo[].m_u32GardenExpireTime");
        AddGroupFields(group, data, ref cursor, "m_astPersonalGardenBaseInfo[].m_stSimpleInfo", [("m_uiWeekPopularity", 4), ("m_uiTotalPopularity", 4), ("m_uiLastUpdatePopularityTime", 4), ("m_ucPrivateType", 1)]);
    }

    private static void AddGroupFields(ParsedFieldNode parent, byte[] data, ref int cursor, string name, (string Name, int Size)[] fields, string status = "SingleSample")
    {
        var size = fields.Sum(field => field.Size);
        var group = parent.AddChild(Group(cursor, size, name, "", status));
        foreach (var field in fields)
        {
            var fieldName = field.Name.Contains('.') ? field.Name : $"{name}.{field.Name}";
            AddCursor(group, data, ref cursor, field.Size, FieldType(field.Size), fieldName, status);
        }
    }

    private static void AddCountedArrayPolicy(
        ParsedFieldNode array,
        string arrayName,
        string countName,
        int count,
        int visibleCount,
        int capacity,
        int stride,
        bool fixedSlotsKnown)
    {
        var defaultSlots = Math.Max(0, capacity - visibleCount);
        var status = count == 0 ? "EmptyArray" : "Verified";
        var note = fixedSlotsKnown
            ? "官服 count 控制 active records；full 中已证明固定槽位容量，未使用槽位显示 ReservedSlots"
            : "官服 count 控制 active records；count=0 时不生成 element[0]";
        array.AddChild(Node(
            array.Offset,
            0,
            "count-policy",
            $"{arrayName}.CountPolicy",
            $"{countName}={count}; active_records={visibleCount}; default_slots={defaultSlots}; capacity={capacity}; stride={stride}",
            status,
            note));

        if (count == 0)
        {
            array.AddChild(Node(
                array.Offset,
                0,
                "array",
                $"{arrayName}.ActiveRecords",
                "empty",
                "EmptyArray",
                $"{countName}=0，所以没有 active element；固定槽位只作为 default/reserved 区域"));
        }
    }

    private static void AddCursor(ParsedFieldNode parent, byte[] data, ref int cursor, int size, string type, string name, string status = "SingleSample")
    {
        var value = FieldValue(data, cursor, size, name);
        Add(parent, data, cursor, size, type, name, value, status);
        cursor += size;
    }

    private static string FieldValue(byte[] data, int offset, int size, string name)
    {
        return size switch
        {
            1 => U8(data, offset).ToString(),
            2 => U16(data, offset).ToString(),
            4 when LooksLikeTimeField(name) => TimeValue(U32(data, offset)),
            4 => U32(data, offset).ToString(),
            _ => Hex(data, offset, size)
        };
    }

    private static bool LooksLikeTimeField(string name)
    {
        return name.Contains("time", StringComparison.OrdinalIgnoreCase) ||
               name.Contains("expire", StringComparison.OrdinalIgnoreCase) ||
               name.Contains("period", StringComparison.OrdinalIgnoreCase);
    }

    private static string FieldType(int size) => size switch
    {
        1 => "u8",
        2 => "le:u16",
        4 => "le:u32",
        _ => "bytes"
    };

    private static ParsedFieldNode Group(int offset, int size, string name, string note, string status = "SingleSample")
    {
        return Node(offset, size, "group", name, "", status, note);
    }

    private static void Add(ParsedFieldNode parent, byte[] data, int offset, int size, string type, string name, string value, string status = "SingleSample", string note = "")
    {
        parent.AddChild(Node(offset, size, type, name, offset + size <= data.Length ? value : "<out-of-range>", offset + size <= data.Length ? status : "Unaligned", note));
    }

    private static ParsedFieldNode Node(long offset, int size, string type, string name, string value, string status, string note = "")
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

    private static void MarkSingleSample(IReadOnlyList<ParsedFieldNode> nodes)
    {
        foreach (var node in Flatten(nodes))
        {
            if (node.Status is not ("Reserved" or "UnknownName" or "Unaligned" or "Verified" or "EmptyArray" or "Template"))
            {
                node.Status = "SingleSample";
            }
        }
    }

    internal static IEnumerable<ParsedFieldNode> Flatten(IEnumerable<ParsedFieldNode> nodes)
    {
        foreach (var node in nodes)
        {
            yield return node;
            foreach (var child in Flatten(node.Children))
            {
                yield return child;
            }
        }
    }

    private static byte U8(byte[] data, int offset) => offset >= 0 && offset < data.Length ? data[offset] : (byte)0;

    private static ushort U16(byte[] data, int offset)
    {
        return offset >= 0 && offset + 2 <= data.Length
            ? BitConverter.ToUInt16(data, offset)
            : (ushort)0;
    }

    private static uint U32(byte[] data, int offset)
    {
        return offset >= 0 && offset + 4 <= data.Length
            ? BitConverter.ToUInt32(data, offset)
            : 0;
    }

    private static string Hex(byte[] data, int offset, int size)
    {
        if (offset < 0 || offset >= data.Length || size <= 0)
        {
            return "";
        }
        return BitConverter.ToString(data, offset, Math.Min(size, data.Length - offset)).Replace("-", " ");
    }

    private static string BlockSummary(byte[] data, int offset, int size)
    {
        if (offset < 0 || offset >= data.Length || size <= 0)
        {
            return "empty";
        }

        var count = Math.Min(size, data.Length - offset);
        var nonZero = 0;
        for (var i = 0; i < count; i++)
        {
            if (data[offset + i] != 0)
            {
                nonZero++;
            }
        }

        var preview = Hex(data, offset, Math.Min(16, count));
        var ascii = Encoding.ASCII.GetString(data, offset, Math.Min(16, count)).Select(ch => ch is >= ' ' and <= '~' ? ch : '.').ToArray();
        return $"len={count:N0}, nonzero={nonZero:N0}, head={preview}, ascii={new string(ascii)}";
    }

    private static void AddRawBlock(ParsedFieldNode parent, byte[] data, int offset, int size, string name, string note)
    {
        if (size <= 0)
        {
            return;
        }

        var nonZero = CountNonZero(data, offset, size);
        var status = nonZero == 0 ? "Reserved" : "UnknownName";
        var block = parent.AddChild(Node(offset, size, "raw", name, BlockSummary(data, offset, size), status, note));
        if (nonZero == 0)
        {
            return;
        }

        var runIndex = 0;
        foreach (var (start, length) in NonZeroRuns(data, offset, size))
        {
            if (runIndex >= 48)
            {
                block.AddChild(Node(start, 0, "note", "MoreNonZeroRuns", "hidden", "UnknownName", "为避免 UI 爆炸，仅显示前 48 个非零 run"));
                break;
            }

            var runName = $"{name}.NonZeroRun_{runIndex:D3}";
            var value = TextOrHexValue(data, start, length);
            block.AddChild(Node(start, length, $"bytes[{length}]", runName, value, "UnknownName", "dump 中非零，但反编译/多 dump 尚未确认语义"));
            runIndex++;
        }
    }

    private static int CountNonZero(byte[] data, int offset, int size)
    {
        if (offset < 0 || offset >= data.Length || size <= 0)
        {
            return 0;
        }

        var count = Math.Min(size, data.Length - offset);
        var nonZero = 0;
        for (var i = 0; i < count; i++)
        {
            if (data[offset + i] != 0)
            {
                nonZero++;
            }
        }
        return nonZero;
    }

    private static IEnumerable<(int Start, int Length)> NonZeroRuns(byte[] data, int offset, int size)
    {
        if (offset < 0 || offset >= data.Length || size <= 0)
        {
            yield break;
        }

        var end = Math.Min(data.Length, offset + size);
        var cursor = offset;
        while (cursor < end)
        {
            while (cursor < end && data[cursor] == 0)
            {
                cursor++;
            }

            if (cursor >= end)
            {
                yield break;
            }

            var start = cursor;
            while (cursor < end && data[cursor] != 0)
            {
                cursor++;
            }

            yield return (start, cursor - start);
        }
    }

    private static string TextOrHexValue(byte[] data, int offset, int size)
    {
        if (offset < 0 || offset >= data.Length || size <= 0)
        {
            return "";
        }

        var count = Math.Min(size, data.Length - offset);
        var bytes = data.AsSpan(offset, count);
        var printable = 0;
        for (var i = 0; i < bytes.Length; i++)
        {
            if (bytes[i] is >= 0x20 and <= 0x7E)
            {
                printable++;
            }
        }

        if (count >= 4 && printable * 100 / count >= 70)
        {
            return $"text={TextValue(data, offset, count)}; hex={Hex(data, offset, Math.Min(count, 32))}";
        }

        return Hex(data, offset, Math.Min(count, 64));
    }

    private static string TextValue(byte[] data, int offset, int size)
    {
        if (offset < 0 || offset >= data.Length || size <= 0)
        {
            return "";
        }

        var count = Math.Min(size, data.Length - offset);
        var slice = data.AsSpan(offset, count);
        var zero = slice.IndexOf((byte)0);
        if (zero >= 0)
        {
            slice = slice[..zero];
        }

        if (slice.Length == 0)
        {
            return "<empty>";
        }

        try
        {
            return Encoding.GetEncoding(936).GetString(slice).Trim();
        }
        catch
        {
            return Encoding.UTF8.GetString(slice).Trim();
        }
    }

    private static string TimeValue(uint value)
    {
        if (value == 0)
        {
            return "0";
        }

        if (value == uint.MaxValue)
        {
            return "0xFFFFFFFF / permanent";
        }

        if (value is >= 946684800 and <= 2147483647)
        {
            var date = DateTimeOffset.FromUnixTimeSeconds(value).LocalDateTime.ToString("yyyy-MM-dd HH:mm:ss");
            return $"{value} / {date}";
        }

        return value.ToString();
    }

    private static string PeriodValue(uint value)
    {
        if (value == uint.MaxValue)
        {
            return "0xFFFFFFFF / permanent";
        }

        if (value == 0)
        {
            return "0";
        }

        return TimeValue(value);
    }

    private static string ItemStatusValue(byte status)
    {
        var text = status switch
        {
            0 => "normal",
            1 => "equipped/active?",
            _ => "unknown"
        };
        return $"{status} / {text}";
    }

    private static string ItemIdValue(uint itemId)
    {
        var itemName = ProtocolNameService.Shared.DescribeItemId(itemId);
        return itemName == "UnknownItem" ? itemId.ToString() : $"{itemId} ({itemName})";
    }
}
