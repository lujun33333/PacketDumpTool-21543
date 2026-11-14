using System.Buffers.Binary;
using System.Globalization;
using System.IO;
using System.Text;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class Msg509GameBeginParser
{
    private const int ExpandedStructSize = 0xA8F9;
    private const int OfficialNamedViewSize = 0xA890;
    private const int PlayerInfoStride21048Plus = 0x23A;
    private const int PlayerInfoLegacyNamedSize = 0x132;
    private const int FullBodyDelta = 0x0E;

    static Msg509GameBeginParser()
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
    }

    public ParserStatus Parse(PacketRecord packet)
    {
        if (packet.MsgId != 509)
        {
            return Unsupported(packet, "不是 MsgID 509。");
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
        if (TryParseExpanded(data, packet, out var expanded))
        {
            return expanded;
        }

        return ParseCompact(data, packet);
    }

    private static bool TryParseExpanded(byte[] data, PacketRecord packet, out ParserStatus status)
    {
        status = default!;

        var missingPrefix = 0;
        var variant = "expanded full";
        if (data.Length >= ExpandedStructSize - FullBodyDelta &&
            packet.Direction.Contains("after_body", StringComparison.OrdinalIgnoreCase))
        {
            missingPrefix = FullBodyDelta;
            variant = "special28346_after_body / partial; full offset = body offset + 0x0E";
        }
        else if (LooksLikeExpandedFull(data))
        {
            missingPrefix = 0;
            variant = "special28346_after_full / full offset";
        }
        else if (data.Length >= ExpandedStructSize && !LooksLikeCompact509(data))
        {
            missingPrefix = 0;
            variant = "expanded candidate";
        }
        else
        {
            return false;
        }

        var view = new ExpandedView(data, missingPrefix);
        var root = Node(
            0,
            ExpandedStructSize,
            "struct",
            "CNotifyGameBeginPara509",
            $"{data.Length:N0} bytes",
            "Verified",
            $"current 21273/Top-Kart expanded memory layout; {variant}; fixed dump size=0x{ExpandedStructSize:X}; named semantic view=0x{OfficialNamedViewSize:X}");

        root.AddChild(Node(0, 0, "note", "Evidence", "CNotifyGameBeginPara / CSSafeCNotifyGameBeginPara", "Verified",
            @"field order/offsets from F:\new1\tools\decode_509_struct_full.py; 21273 Top-Kart sub_180038F50 initializes 0xA8F9 bytes, CMessageHouse copies 0xA8F9, and consumes m_uiGameBeginSwitchFlag at 0xA3D9; dump samples are special28346_after_full_msg509_len43257"));

        if (missingPrefix != 0)
        {
            root.AddChild(Node(0, missingPrefix, "missing", "MissingFullPrefix", $"{missingPrefix} bytes", "Unaligned",
                "当前文件是 after_body，前 0x0E 字节已被 dump 层裁掉；请选择 special28346_after_full 可看到完整 509 头部字段"));
        }

        AddHeader(root, view);
        AddSafetyAndSync(root, view);
        AddAntiCheatAndNetwork(root, view);
        AddPlayerAndMapBlocks(root, view);
        AddModeAndOptionalBlocks(root, view);
        AddTail(root, view);

        status = new ParserStatus
        {
            Status = "parsed",
            Note = $"MsgID 509 已按 CNotifyGameBeginPara expanded layout 解析；direction={packet.Direction}；模式/人数由 m_ushBaseMode/m_ushSubMode/m_bCurrentPlayers/m_uchPlayerNums 等字段控制。",
            StructureText = "Msg509 GameBegin expanded structure tree loaded.",
            RootNodes = [root]
        };
        return true;
    }

    private static ParserStatus ParseCompact(byte[] data, PacketRecord packet)
    {
        var root = Node(
            0,
            data.Length,
            "struct",
            "CSSafeCNotifyGameBeginPara509_CompactWire",
            $"{data.Length:N0} bytes",
            "UnknownName",
            "这是 28346 内层解密前/compact CSSafe 509 wire，不是 expanded CNotifyGameBeginPara 内存结构");

        if (data.Length >= 2)
        {
            root.AddChild(Node(0, 2, "be:u16", "PacketBodyLengthPrefix", U16BE(data, 0).ToString(CultureInfo.InvariantCulture), "Verified",
                "BodyReader/现有离线 decoder 会跳过这个 2B 前缀"));
            root.AddChild(Node(2, Math.Min(data.Length - 2, 64), "bytes", "CompactPayloadHead", Hex(data, 2, Math.Min(data.Length - 2, 64)), "UnknownName",
                "compact 字节需要 CSSafe 509 transform；Viewer v1.3 先展示 expanded after_full 结构"));
        }

        if (data.Length > 128)
        {
            root.AddChild(Node(data.Length - 64, 64, "bytes", "CompactPayloadTail", Hex(data, data.Length - 64, 64), "UnknownName", ""));
        }

        root.AddChild(Node(0, 0, "note", "HowToGetFullStruct", "open sibling special28346_after_full_msg509_len43257.bin", "Verified",
            "当前 dump 同目录通常还有 special28346_after_full，它已经是 CNotifyGameBeginPara expanded 结构；那个文件可按官服 offset 完整展开"));

        return new ParserStatus
        {
            Status = "compact",
            Note = $"MsgID 509 当前文件是 compact CSSafe wire；direction={packet.Direction}；未在 C# Viewer 内执行 CSSafe 509 transform。",
            StructureText = "Msg509 compact CSSafe payload; open special28346_after_full for expanded struct.",
            RootNodes = [root]
        };
    }

    private static void AddHeader(ParsedFieldNode root, ExpandedView view)
    {
        var group = root.AddChild(Node(0x0000, 0x0113, "group", "HeaderAndTrack", "", "Verified",
            "基础开图字段；人数/地图/随机种子/赛道顺序"));
        AddU32(group, view, 0x0000, "m_unNormalLoadTime", "常见 10000/20000；开图加载时间");
        AddI32(group, view, 0x0004, "m_iMapID", "地图 ID");
        AddFixedString(group, view, 0x0008, 0x20, "m_szMapHash");
        AddU32(group, view, 0x0028, "m_unGameID", "");
        AddU32(group, view, 0x002C, "m_unSeed", "随机种子");
        AddU8(group, view, 0x0030, "m_bCurrentPlayers", "当前参与人数，影响后续 player arrays 的有效条目");
        AddU32Array(group, view, 0x0031, "m_aunRaceTrackOrders", 20, "fixed 20 slots");
        AddI16(group, view, 0x0081, "m_sTotalMapRound", "总圈数/地图轮数");
        AddU32(group, view, 0x0083, "m_unPropUpdateInterval", "");
        AddI32(group, view, 0x0087, "m_iRecord", "");
    }

    private static void AddSafetyAndSync(ParsedFieldNode root, ExpandedView view)
    {
        var group = root.AddChild(Node(0x008B, 0x04FD, "group", "AwardAndSyncCar", "", "Verified",
            "年兽奖励、同步车辆与地图校验块"));
        var nianCount = AddI8(group, view, 0x008B, "m_cNianShouTypeNum", "active count for m_astNianShouAwardInfo");
        AddArrayGroup(group, view, 0x008C, 0x1B, 5, Math.Max(0, nianCount), "m_astNianShouAwardInfo", "STNianShouAwardInfo509");

        var syncCarNum = AddU8(group, view, 0x0113, "m_uchSyncCarNum", "active count for m_astSyncCar");
        var syncCars = AddArrayGroup(group, view, 0x0114, 0x39, 20, syncCarNum, "m_astSyncCar", "STSyncCar509");
        for (var i = 0; i < Math.Min(syncCarNum, 20); i++)
        {
            var off = 0x0114 + i * 0x39;
            var car = syncCars.AddChild(Node(off, 0x39, "struct", $"m_astSyncCar[{i}]", "", "Verified", ""));
            AddU32(car, view, off + 0x00, "m_unPlayerUin", "");
            AddI32(car, view, off + 0x04, "m_nCurCarID", "当前赛车 ID");
            AddU8(car, view, off + 0x08, "m_cHasStoneInfo", "是否有宝石槽信息");
            AddRaw(car, view, off + 0x09, 0x30, "m_stSTKartStoneGrooveInfo", "STKartStoneGrooveInfo509 capacity block");
        }

        AddU8(group, view, 0x0588, "m_bReportDataFlag", "");
        var checkNum = AddU8(group, view, 0x0589, "m_byCheckDataNum", "active count for m_astMapCheckDiv");
        var check = AddArrayGroup(group, view, 0x058A, 0x04, 10, checkNum, "m_astMapCheckDiv", "STMapCheckDiv509");
        for (var i = 0; i < Math.Min(checkNum, 10); i++)
        {
            var off = 0x058A + i * 4;
            var item = check.AddChild(Node(off, 4, "struct", $"m_astMapCheckDiv[{i}]", "", "Verified", ""));
            AddI16(item, view, off, "m_shBeginIdx", "");
            AddI16(item, view, off + 2, "m_shEndIdx", "");
        }
    }

    private static void AddAntiCheatAndNetwork(ParsedFieldNode root, ExpandedView view)
    {
        var group = root.AddChild(Node(0x05B2, 0x01E4, "group", "NetworkCheckAndAntiCheat", "", "Verified",
            "P2P/TCP、MultiInfo、速度区间、内存检查、防漂移/碰撞模式参数"));
        AddU8(group, view, 0x05B2, "m_byP2PMode", "");
        AddU8(group, view, 0x05B3, "m_byTcpFrequence", "");
        var multiLen = AddU8(group, view, 0x05B4, "m_byMultiInfoLen", "m_abyMultiInfo valid length; mode/version can change this");
        AddRaw(group, view, 0x05B5, 100, "m_abyMultiInfo", $"valid={Math.Min(multiLen, 100)}; capacity=100");
        AddU8(group, view, 0x0619, "m_bFeedBackEnabled", "");
        var speedNum = AddU8(group, view, 0x061A, "m_u8SpeedSectionNum", "");
        AddU16Array(group, view, 0x061B, "m_au16SpeedSectionLowerBound", 10, speedNum);
        var normalSpeedNum = AddU8(group, view, 0x062F, "m_u8NormalSpeedSectionNum", "");
        AddU16Array(group, view, 0x0630, "m_au16NormalSpeedSectionLowerBound", 10, normalSpeedNum);
        var memNum = AddU8(group, view, 0x0644, "m_u8MemCheckInfoNum", "");
        AddArrayGroup(group, view, 0x0645, 0x2E, 5, memNum, "m_astMemCheckInfo", "STMemCheckInfo509");
        AddU8(group, view, 0x072B, "m_u8ExtraInfoInterval", "");
        AddU16(group, view, 0x072C, "m_u16OffsetThreshold", "");
        AddU32(group, view, 0x072E, "m_u32SpeedRatioThreshold1", "");
        AddU32(group, view, 0x0732, "m_u32SpeedRatioThreshold2", "");
        AddI32(group, view, 0x0736, "m_i32HideTaskId", "");
        AddU8(group, view, 0x073A, "m_u8HideTaskType", "");
        AddU32(group, view, 0x073B, "m_u32HideTaskParam1", "");
        var cpNum = AddU8(group, view, 0x073F, "m_u8ForceReportCPNum", "");
        AddU16Array(group, view, 0x0740, "m_au16ForceReportCP", 5, cpNum);
        AddU8(group, view, 0x074A, "m_ucCliReserveFlag", "");
        var antiDrift = AddU8(group, view, 0x074B, "m_u8EnableAntiDriftCheat", "0/1; active count for m_astAntiDriftCheatPara");
        AddArrayGroup(group, view, 0x074C, 0x1C, 1, antiDrift, "m_astAntiDriftCheatPara", "STAntiDriftCheatPara509");
        var crashMode = AddU8(group, view, 0x0768, "m_u8HasCrashModePara", "0/1; active count for m_astCrashModePara");
        AddArrayGroup(group, view, 0x0769, 0x1C, 1, crashMode, "m_astCrashModePara", "STCrashModePara509");
        AddI32(group, view, 0x0785, "m_i32FizzStarTaskId", "");
        AddU8(group, view, 0x0789, "m_u8FizzStarTaskType", "");
        AddU32(group, view, 0x078A, "m_u32FizzStarTaskParam1", "");
        AddU32(group, view, 0x078E, "m_u32LDMRecordID", "");
        AddU32(group, view, 0x0792, "m_u32GameSeq", "");
    }

    private static void AddPlayerAndMapBlocks(ParsedFieldNode root, ExpandedView view)
    {
        var group = root.AddChild(Node(0x0796, 0x8E0C - 0x0796, "group", "PlayersMapsNpcAndAvatar21048Plus", "", "Verified",
            "current 21048/21273 layout；玩家数组 stride=0x23A，后续 map/npc/avatar/tower/relay 大块只按已恢复锚点展开"));
        var playerNums = AddU8(group, view, 0x0796, "m_uchPlayerNums", "active count for m_astGameBeginPlayerInfo");
        var players = AddArrayGroup(group, view, 0x0797, PlayerInfoStride21048Plus, 20, playerNums, "m_astGameBeginPlayerInfo", "STGameBeginPlayerInfo509_21048Plus");
        for (var i = 0; i < Math.Min(playerNums, 20); i++)
        {
            AddPlayerInfo(players, view, 0x0797 + i * PlayerInfoStride21048Plus, i);
        }

        AddRawUnknown(group, view, 0x341F, 0x342F - 0x341F, "PlayerInfoTrailingReserved21048", "20 * 0x23A player array 后的固定保留区；当前样本通常为 0");
        var cheerNums = AddU8(group, view, 0x342F, "m_ucPlayCheerNums", "current 21048/21273 offset；cheer storage follows");
        AddRawUnknown(group, view, 0x3430, 0x3450 - 0x3430, "m_astCheersStorage21048", $"active={Math.Min(cheerNums, 6)}; byte-covered only; old 6*u32 layout is not trusted for 21273");
        var shuttleNum = AddU8(group, view, 0x3450, "m_ucShuttleMapNum", "active count for m_astShuttleMapList");
        var shuttle = AddArrayGroup(group, view, 0x3451, 0x19, 5, shuttleNum, "m_astShuttleMapList", "STShuttleMapInfo509_21048Plus");
        for (var i = 0; i < Math.Min(shuttleNum, 5); i++)
        {
            AddShuttleMap(shuttle, view, 0x3451 + i * 0x19, i);
        }

        AddI32(group, view, 0x34CE, "m_iCurrentMapInfoMapID", "current 21048 recovered offset");
        AddRawUnknown(group, view, 0x34D2, 0x8E0C - 0x34D2, "MapNpcAvatarTowerRelayStorage21048", "map-info/npc/avatar/tower/relay pre-seq storage；已 byte-covered，但未追到足够 21273 consumer 前不拆旧 0x201B/0x517C 布局");
    }

    private static void AddModeAndOptionalBlocks(ParsedFieldNode root, ExpandedView view)
    {
        var group = root.AddChild(Node(0x8E0C, ExpandedStructSize - 0x8E0C, "group", "ModeAndOptionalTail21048Plus", "", "Verified",
            "current 21048/21273 tail/control offsets；旧 0x7726/0x8AA7/0x8D0C 尾部偏移不再用于 0xA8F9 dump"));
        AddU8(group, view, 0x8E0C, "m_ucChangeCar", "");
        AddU32(group, view, 0x8E0D, "m_uiGameSeqIDHigh", "官服样本每局动态；位于 MonitorCheckPoint 前");
        AddU32(group, view, 0x8E11, "m_uiGameSeqIDLow", "官服样本每局动态；位于 MonitorCheckPoint 前");
        AddI32(group, view, 0x8E15, "m_iKubiBigCoinReplaceItem", "");
        AddI32(group, view, 0x8E19, "m_iTimerChallengeJumpLevel", "");
        AddU8(group, view, 0x8E1D, "m_ucShadowRunDelay", "");
        AddU16(group, view, 0x8E1E, "m_ushShadowCatchUpContinuesTime", "");
        var arrestNum = AddU8(group, view, 0x8E20, "m_ucArrestPlayerNums", "");
        AddRawUnknown(group, view, 0x8E21, 0x8E5D - 0x8E21, "m_astArrestRoleInfoStorage21048", $"active={Math.Min(arrestNum, 6)}; byte-covered storage; exact 21273 element fields not expanded");
        AddU16(group, view, 0x8E5D, "m_ushMonitorCheckPointBegin", "");
        AddU16(group, view, 0x8E5F, "m_ushMonitorCheckPointEnd", "");
        AddU8(group, view, 0x8E61, "m_ucMonitorLapCnt", "");
        AddU8(group, view, 0x8E62, "m_ucGameType", "");
        AddU16(group, view, 0x8E63, "m_ucPointID", "field name says uc; recovered consumer reads u16");
        AddU16(group, view, 0x8E65, "m_ushBaseMode", "base mode; 模式差异关键字段");
        AddU16(group, view, 0x8E67, "m_ushSubMode", "sub mode; 模式差异关键字段");
        AddU8(group, view, 0x8E69, "m_uchGameType", "");
        AddU16(group, view, 0x8E6A, "m_usReportPosSpan", "");
        AddI16(group, view, 0x8E6C, "m_sPropID", "");
        AddU32(group, view, 0x8E6E, "m_uiPropIndex", "");
        AddI16(group, view, 0x8E72, "m_sPropNum", "");
        AddU8(group, view, 0x8E74, "m_uchBaseGameModeEx", "");
        var paraNum = AddU8(group, view, 0x8E75, "m_ucParaNum", "");
        AddI32Array(group, view, 0x8E76, "m_iParaList", 20, paraNum);
        AddRawUnknown(group, view, 0x8EC6, 0x915F - 0x8EC6, "CheckpointStageCarCollectStorage21048", "checkpoint/stage/car-collect storage；当前只按 recovered scalar anchors 分段，不套旧尾部结构");
        AddU16(group, view, 0x915F, "m_ushReportAntiCollisionDataTime", "官服当前样本为 10；不是旧模板 3000");
        AddU32(group, view, 0x9161, "m_uiDuration", "");
        AddU32(group, view, 0x9165, "m_uiBeginCDTime", "");
        AddU32(group, view, 0x9169, "m_uiPropInteval", "");
        AddU32(group, view, 0x916D, "m_uiMoveFreq", "官服当前样本为 30");
        AddU8(group, view, 0x9171, "m_ucHaveGameLogicTask", "");
        AddRawUnknown(group, view, 0x9172, 0xA060 - 0x9172, "GameLogicBuffMapChallengeStorage21048", "gamelogic/buff/mapchallenge/anonymous/time-shuttle storage；只保留 byte coverage 和非零证据");
        var recordCheck = AddU8(group, view, 0xA060, "m_ucRecordCheckCondNum", "普通官服样本可为 3；自定义/12P/6P 当前样本常见 0");
        AddRecordCheckConds21048(group, view, recordCheck);
        AddU32(group, view, 0xA3D9, "m_uiGameBeginSwitchFlag", "开图功能开关 flags；官服样本非零，不再沿用旧 0x8AA7 偏移");
        AddU8(group, view, 0xA3DD, "m_ucTriggerVeggieDogTask", "");
        var crystal = AddU8(group, view, 0xA3DE, "m_ucHasQSpeedCrystalInfo", "");
        AddRawUnknown(group, view, 0xA3DF, 0xA418 - 0xA3DF, "m_stQSpeedCrystalInfoStorage21048", $"active={Math.Min(crystal, 1)}; storage byte-covered");
        AddU16(group, view, 0xA418, "m_ushBorderRaceType", "");
        var raceSync = AddU8(group, view, 0xA41A, "m_ucRaceSyncParaNum", "官服当前样本常见 4；后续为 u32 RaceSyncParaList");
        AddU32Array(group, view, 0xA41B, "m_aiRaceSyncParaList", 16, raceSync);
        AddRawUnknown(group, view, 0xA45B, 0xA7A7 - 0xA45B, "RaceSyncStorageTail21048", "RaceSyncParaList 后到 QingHua scalar 前的 storage；未确认语义不强行命名");
        AddU8(group, view, 0xA7A7, "m_ucQingHuaMysteryGroundDataNum", "");
        AddU32(group, view, 0xA7A8, "m_uiSpeclEffectSwitchFlag", "普通官服样本可为 14；自定义/12P/6P 当前样本可为 0");
        AddI32(group, view, 0xA7AC, "m_iNPCMoveFreq", "普通官服样本可为 30；自定义/12P/6P 当前样本可为 0");
        AddI32(group, view, 0xA7B0, "m_iYiGuaJiCheckType", "");
        var guaji = AddU8(group, view, 0xA7B4, "m_ucYiGuaJiCheckParaNum", "");
        AddRawUnknown(group, view, 0xA7B5, 0xA7BD - 0xA7B5, "m_stYiGuaJiCheckParaStorage21048", $"active={Math.Min(guaji, 1)}; capacity storage=8");
        AddU32(group, view, 0xA7BD, "m_uiPara1", "HasCustomScheme 前的保留参数；官服样本非零且随样本变化");
        AddU8(group, view, 0xA7C1, "m_ucOriginTeamID", "");
        AddU8(group, view, 0xA7C2, "m_ucIsLadderChallenge", "");
        var custom = AddU8(group, view, 0xA7C3, "m_ucHasCustomScheme", "0/1; custom room modes use m_stCustomScheme");
        AddCustomScheme(group, view, 0xA7C4, custom);
        AddRawUnknown(group, view, 0xA870, OfficialNamedViewSize - 0xA870, "OfficialSemanticViewTrailingReserved21048", "0xA870..0xA88F official semantic view trailing reserved");
        AddRawUnknown(group, view, OfficialNamedViewSize, ExpandedStructSize - OfficialNamedViewSize, "ExpandedScratchTail21048", "Top-Kart/Network 0xA8F9 expanded dump tail；当前未见稳定语义消费，保留 raw/nonzero 摘要");
    }

    private static void AddPlayerInfo(ParsedFieldNode parent, ExpandedView view, int off, int index)
    {
        var node = parent.AddChild(Node(off, PlayerInfoStride21048Plus, "struct", $"m_astGameBeginPlayerInfo[{index}]", "", "Verified",
            "current 21048/21273 stride=0x23A；前 0x132 字段来自已恢复旧公共前缀，后 0x108 只保留 raw 证据"));
        AddU32(node, view, off + 0x000, "m_uiUin", "");
        AddU32(node, view, off + 0x004, "m_uiChumCircleID", "");
        AddU8(node, view, off + 0x008, "m_uchSkillNums", "");
        AddRaw(node, view, off + 0x009, 0x30, "m_astChumCircleSkillInfo", "capacity=6; stride=8");
        AddI32(node, view, off + 0x039, "m_iWorldEscapeTaskID", "");
        AddU8(node, view, off + 0x03D, "m_stProfessionLicenseInfo.m_uchBigLevel", "");
        AddU8(node, view, off + 0x03E, "m_stProfessionLicenseInfo.m_uchSmallLevel", "");
        AddU8(node, view, off + 0x03F, "m_uchParaNum", "active count for m_stEffectGameBuffInfo");
        AddRaw(node, view, off + 0x040, 0x78, "m_stEffectGameBuffInfo", "capacity block");
        AddI32(node, view, off + 0x0B8, "m_iStarsFightingStar", "");
        AddU8(node, view, off + 0x0BC, "m_ucParaNum", "active count for m_iParaList");
        AddI32Array(node, view, off + 0x0BD, "m_iParaList", 20, ReadU8(view, off + 0x0BC));
        AddU32(node, view, off + 0x10D, "m_uiEquipSkillId", "");
        AddU8(node, view, off + 0x111, "m_ucGLSpeclEffectNum", "");
        AddRaw(node, view, off + 0x112, 0x20, "m_astGLSpeclEffectInfo", "capacity=4; stride=8");
        AddRawUnknown(node, view, off + PlayerInfoLegacyNamedSize, PlayerInfoStride21048Plus - PlayerInfoLegacyNamedSize, "m_stGameBeginPlayerInfoTail21048", "21048/21273 player record extra tail；byte-covered，未追到消费点前不乱命名");
    }

    private static void AddShuttleMap(ParsedFieldNode parent, ExpandedView view, int off, int index)
    {
        var node = parent.AddChild(Node(off, 0x19, "struct", $"m_astShuttleMapList[{index}]", "", "Verified", ""));
        AddI32(node, view, off + 0x00, "m_iMapID", "");
        AddU16(node, view, off + 0x04, "m_ushBeginIdx", "");
        AddU16(node, view, off + 0x08, "m_ushEndIdx", "");
        AddU8(node, view, off + 0x0C, "m_uchReverse", "");
        AddU16(node, view, off + 0x0D, "m_ushBeginCheckPointIdx", "");
        AddI32(node, view, off + 0x0F, "m_iProb", "");
        var tagNum = AddU8(node, view, off + 0x13, "m_uchTagNum", "");
        AddRaw(node, view, off + 0x14, 0x04, "m_astTagList", $"valid={Math.Min(tagNum, 4)}; capacity=4");
        AddU8(node, view, off + 0x18, "m_ucShuttleType", "");
    }

    private static void AddMapInfo(ParsedFieldNode parent, ExpandedView view, int off, int index)
    {
        var node = parent.AddChild(Node(off, 0x30, "struct", $"m_astMapInfo[{index}]", "", "Verified", ""));
        AddI32(node, view, off + 0x00, "m_iMapID", "");
        AddU8(node, view, off + 0x04, "m_bReportDataFlag", "");
        var checkNum = AddU8(node, view, off + 0x05, "m_byCheckDataNum", "");
        AddArrayGroup(node, view, off + 0x06, 0x04, 10, checkNum, "m_astMapCheckDiv", "STMapCheckDiv509");
        AddI16(node, view, off + 0x2E, "m_sTotalMapRound", "");
    }

    private static void AddCustomScheme(ParsedFieldNode parent, ExpandedView view, int off, int hasCustom)
    {
        var inactiveNonZero = hasCustom <= 0 && view.CanRead(off, 0xAC) && HasAnyNonZero(view, off, 0xAC);
        var node = parent.AddChild(Node(off, 0xAC, "struct", "m_stCustomScheme", $"active={hasCustom}; size=0xAC", hasCustom > 0 ? "Verified" : inactiveNonZero ? "UnknownName" : "Reserved",
            hasCustom > 0 ? "custom room mode data" : "m_ucHasCustomScheme=0; block exists but inactive/zero in current samples"));
        if (hasCustom <= 0)
        {
            if (inactiveNonZero)
            {
                AddRawUnknown(node, view, off, 0xAC, "m_stCustomSchemeInactiveNonZero", "m_ucHasCustomScheme=0 but storage is nonzero；保留证据，不自动推断 active");
            }

            return;
        }

        AddU32(node, view, off + 0x00, "m_uiSchemeID", "");
        AddFixedString(node, view, off + 0x04, 0x80, "m_szSchemeName");
        AddU32(node, view, off + 0x84, "m_uiCreateTime", "");
        AddU32(node, view, off + 0x88, "m_uiUpdateTime", "");
        AddI16(node, view, off + 0x8C, "m_sBaseGameMode", "");
        AddI16(node, view, off + 0x8E, "m_sSubGameMode", "");
        AddU8(node, view, off + 0x90, "m_ucAllocSvrType", "");
        AddU8(node, view, off + 0x91, "m_ucEliminationIntervalTs", "");
        AddU8(node, view, off + 0x92, "m_ucEliminationRanking", "");
        AddU8(node, view, off + 0x93, "m_ucRoundNum", "");
        AddU8(node, view, off + 0x94, "m_ucN2OMode", "");
        AddI32(node, view, off + 0x95, "m_iN2OEffect", "");
        AddU8(node, view, off + 0x99, "m_ucSmallSpray", "");
        AddU8(node, view, off + 0x9A, "m_ucCollision", "");
        AddU8(node, view, off + 0x9B, "m_ucGem", "");
        AddU8(node, view, off + 0x9C, "m_ucCostume", "");
        AddU8(node, view, off + 0x9D, "m_ucModification", "");
        var mapNum = AddU8(node, view, off + 0x9E, "m_ucMapNum", "");
        AddI32Array(node, view, off + 0x9F, "m_aiMapIDList", 3, mapNum);
        AddU8(node, view, off + 0xAB, "m_ucNoCollisionSeconds", "");
    }

    private static void AddTail(ParsedFieldNode root, ExpandedView view)
    {
        var dataOffset = ExpandedStructSize - view.MissingPrefix;
        if (dataOffset >= 0 && dataOffset < view.Data.Length)
        {
            root.AddChild(Node(ExpandedStructSize, view.Data.Length - dataOffset, "raw", "TailAfterCNotifyGameBeginPara509", $"{view.Data.Length - dataOffset:N0} bytes", "Unaligned",
                "超出 CNotifyGameBeginPara509 0xA8F9 dump 范围；保留为 dump/parser 额外尾部，不按官服字段命名"));
        }
    }

    private static ParsedFieldNode AddArrayGroup(ParsedFieldNode parent, ExpandedView view, int offset, int stride, int capacity, int activeCount, string name, string elementType)
    {
        var safeActive = Math.Clamp(activeCount, 0, capacity);
        var status = activeCount < 0 || activeCount > capacity ? "Unaligned" : "Verified";
        return parent.AddChild(Node(offset, stride * capacity, "array", name, $"active={activeCount}; visible={safeActive}; capacity={capacity}; stride=0x{stride:X}", status, elementType));
    }

    private static void AddU32Array(ParsedFieldNode parent, ExpandedView view, int offset, string name, int count, string note)
    {
        var arr = parent.AddChild(Node(offset, count * 4, "array", name, $"count={count}", "Verified", note));
        for (var i = 0; i < count; i++)
        {
            AddU32(arr, view, offset + i * 4, $"{name}[{i}]", "");
        }
    }

    private static void AddU32Array(ParsedFieldNode parent, ExpandedView view, int offset, string name, int capacity, int activeCount)
    {
        var count = Math.Clamp(activeCount, 0, capacity);
        var arr = parent.AddChild(Node(offset, capacity * 4, "array", name, $"active={activeCount}; visible={count}; capacity={capacity}", activeCount <= capacity ? "Verified" : "Unaligned", ""));
        for (var i = 0; i < count; i++)
        {
            AddU32(arr, view, offset + i * 4, $"{name}[{i}]", "");
        }
    }

    private static void AddI32Array(ParsedFieldNode parent, ExpandedView view, int offset, string name, int capacity, int activeCount)
    {
        var count = Math.Clamp(activeCount, 0, capacity);
        var arr = parent.AddChild(Node(offset, capacity * 4, "array", name, $"active={activeCount}; visible={count}; capacity={capacity}", activeCount <= capacity ? "Verified" : "Unaligned", ""));
        for (var i = 0; i < count; i++)
        {
            AddI32(arr, view, offset + i * 4, $"{name}[{i}]", "");
        }
    }

    private static void AddU16Array(ParsedFieldNode parent, ExpandedView view, int offset, string name, int capacity, int activeCount)
    {
        var count = Math.Clamp(activeCount, 0, capacity);
        var arr = parent.AddChild(Node(offset, capacity * 2, "array", name, $"active={activeCount}; visible={count}; capacity={capacity}", activeCount <= capacity ? "Verified" : "Unaligned", ""));
        for (var i = 0; i < count; i++)
        {
            AddU16(arr, view, offset + i * 2, $"{name}[{i}]", "");
        }
    }

    private static int AddU8(ParsedFieldNode parent, ExpandedView view, int offset, string name, string note)
    {
        if (!view.CanRead(offset, 1))
        {
            parent.AddChild(MissingNode(offset, 1, "u8", name, view));
            return 0;
        }

        var value = view.Data[view.ToDataOffset(offset)];
        parent.AddChild(Node(offset, 1, "u8", name, value.ToString(CultureInfo.InvariantCulture), "Verified", note));
        return value;
    }

    private static int AddI8(ParsedFieldNode parent, ExpandedView view, int offset, string name, string note)
    {
        if (!view.CanRead(offset, 1))
        {
            parent.AddChild(MissingNode(offset, 1, "i8", name, view));
            return 0;
        }

        var value = unchecked((sbyte)view.Data[view.ToDataOffset(offset)]);
        parent.AddChild(Node(offset, 1, "i8", name, value.ToString(CultureInfo.InvariantCulture), "Verified", note));
        return value;
    }

    private static void AddU16(ParsedFieldNode parent, ExpandedView view, int offset, string name, string note)
    {
        if (!view.CanRead(offset, 2))
        {
            parent.AddChild(MissingNode(offset, 2, "le:u16", name, view));
            return;
        }

        parent.AddChild(Node(offset, 2, "le:u16", name, U16LE(view, offset).ToString(CultureInfo.InvariantCulture), "Verified", note));
    }

    private static int AddU16(ParsedFieldNode parent, ExpandedView view, int offset, string name, string note, bool returnValue)
    {
        AddU16(parent, view, offset, name, note);
        return view.CanRead(offset, 2) ? U16LE(view, offset) : 0;
    }

    private static void AddI16(ParsedFieldNode parent, ExpandedView view, int offset, string name, string note)
    {
        if (!view.CanRead(offset, 2))
        {
            parent.AddChild(MissingNode(offset, 2, "le:i16", name, view));
            return;
        }

        parent.AddChild(Node(offset, 2, "le:i16", name, I16LE(view, offset).ToString(CultureInfo.InvariantCulture), "Verified", note));
    }

    private static void AddU32(ParsedFieldNode parent, ExpandedView view, int offset, string name, string note)
    {
        if (!view.CanRead(offset, 4))
        {
            parent.AddChild(MissingNode(offset, 4, "le:u32", name, view));
            return;
        }

        parent.AddChild(Node(offset, 4, "le:u32", name, U32LE(view, offset).ToString(CultureInfo.InvariantCulture), "Verified", note));
    }

    private static void AddI32(ParsedFieldNode parent, ExpandedView view, int offset, string name, string note)
    {
        if (!view.CanRead(offset, 4))
        {
            parent.AddChild(MissingNode(offset, 4, "le:i32", name, view));
            return;
        }

        parent.AddChild(Node(offset, 4, "le:i32", name, I32LE(view, offset).ToString(CultureInfo.InvariantCulture), "Verified", note));
    }

    private static void AddFixedString(ParsedFieldNode parent, ExpandedView view, int offset, int size, string name)
    {
        if (!view.CanRead(offset, size))
        {
            parent.AddChild(MissingNode(offset, size, "bytes/string", name, view));
            return;
        }

        var raw = view.Data.AsSpan(view.ToDataOffset(offset), size);
        parent.AddChild(Node(offset, size, "bytes/string", name, DecodeFixed(raw), "Verified", ""));
    }

    private static void AddRaw(ParsedFieldNode parent, ExpandedView view, int offset, int size, string name, string note)
    {
        if (!view.CanRead(offset, size))
        {
            parent.AddChild(MissingNode(offset, size, "raw", name, view));
            return;
        }

        var preview = Hex(view.Data, view.ToDataOffset(offset), Math.Min(size, 48));
        var suffix = size > 48 ? "..." : "";
        parent.AddChild(Node(offset, size, "raw", name, $"{preview}{suffix}", "Reserved", note));
    }

    private static void AddRawUnknown(ParsedFieldNode parent, ExpandedView view, int offset, int size, string name, string note)
    {
        if (size <= 0)
        {
            return;
        }

        if (!view.CanRead(offset, size))
        {
            parent.AddChild(MissingNode(offset, size, "raw", name, view));
            return;
        }

        var nonZero = CountNonZero(view, offset, size);
        var preview = Hex(view.Data, view.ToDataOffset(offset), Math.Min(size, 48));
        var suffix = size > 48 ? "..." : "";
        parent.AddChild(Node(offset, size, "raw", name, $"len={size}; nonzero={nonZero}; head={preview}{suffix}", nonZero == 0 ? "Reserved" : "UnknownName", note));
    }

    private static void AddRecordCheckConds21048(ParsedFieldNode parent, ExpandedView view, int activeCount)
    {
        const int offset = 0xA061;
        const int stride = 0x6F;
        const int capacity = 8;
        const int conditionOffset = 0x03;
        const int conditionStride = 0x1B;
        const int conditionCapacity = 4;

        var arr = AddArrayGroup(parent, view, offset, stride, capacity, activeCount, "m_astRecordCheckConds", "RecordCheckCondInfo509_21048");
        var safeActive = Math.Clamp(activeCount, 0, capacity);
        for (var i = 0; i < safeActive; i++)
        {
            var recOff = offset + i * stride;
            var item = arr.AddChild(Node(recOff, stride, "struct", $"m_astRecordCheckConds[{i}]", "", "Verified",
                "每条最多 4 个 CheckCondition，每个 0x1B，IntParas[6]"));
            AddU16(item, view, recOff, "m_usCheckRelation", "");
            var condNum = AddU8(item, view, recOff + 0x02, "m_ucConditionNum", "");
            var conds = AddArrayGroup(item, view, recOff + conditionOffset, conditionStride, conditionCapacity, condNum, "m_astCheckConditions", "CheckCondition509_21048");
            for (var j = 0; j < Math.Min(condNum, conditionCapacity); j++)
            {
                var condOff = recOff + conditionOffset + j * conditionStride;
                var cond = conds.AddChild(Node(condOff, conditionStride, "struct", $"m_astCheckConditions[{j}]", "", "Verified", ""));
                AddU16(cond, view, condOff, "m_usCheckType", "");
                var paraNum = AddU8(cond, view, condOff + 0x02, "m_ucIntParaNum", "");
                AddI32Array(cond, view, condOff + 0x03, "m_aiIntParas", 6, paraNum);
            }
        }

        for (var i = safeActive; i < capacity; i++)
        {
            var recOff = offset + i * stride;
            if (HasAnyNonZero(view, recOff, stride))
            {
                AddRawUnknown(arr, view, recOff, stride, $"m_astRecordCheckConds[{i}]_inactiveNonZero", "count says inactive but storage is nonzero；保留为未对齐证据");
            }
        }
    }

    private static int ReadU8(ExpandedView view, int offset)
    {
        return view.CanRead(offset, 1) ? view.Data[view.ToDataOffset(offset)] : 0;
    }

    private static bool LooksLikeExpandedFull(byte[] data)
    {
        if (data.Length < ExpandedStructSize)
        {
            return false;
        }

        var normalLoad = U32LE(data, 0);
        var mapId = I32LE(data, 4);
        var currentPlayers = data[0x30];
        var multiInfoLen = data[0x5B4];
        return normalLoad <= 120_000 &&
               mapId is >= 0 and <= 100_000 &&
               currentPlayers <= 20 &&
               multiInfoLen <= 100;
    }

    private static bool LooksLikeCompact509(byte[] data)
    {
        if (data.Length >= 4096)
        {
            return false;
        }

        if (data.Length < 4)
        {
            return true;
        }

        var prefix = U16BE(data, 0);
        return prefix <= data.Length && data.Skip(2).Take(32).Any(b => b != 0);
    }

    private static ushort U16BE(byte[] data, int offset) => BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(offset, 2));

    private static ushort U16LE(ExpandedView view, int offset) => BinaryPrimitives.ReadUInt16LittleEndian(view.Data.AsSpan(view.ToDataOffset(offset), 2));

    private static short I16LE(ExpandedView view, int offset) => BinaryPrimitives.ReadInt16LittleEndian(view.Data.AsSpan(view.ToDataOffset(offset), 2));

    private static uint U32LE(ExpandedView view, int offset) => BinaryPrimitives.ReadUInt32LittleEndian(view.Data.AsSpan(view.ToDataOffset(offset), 4));

    private static int I32LE(ExpandedView view, int offset) => BinaryPrimitives.ReadInt32LittleEndian(view.Data.AsSpan(view.ToDataOffset(offset), 4));

    private static uint U32LE(byte[] data, int offset) => BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(offset, 4));

    private static int I32LE(byte[] data, int offset) => BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(offset, 4));

    private static string DecodeFixed(ReadOnlySpan<byte> raw)
    {
        var end = raw.IndexOf((byte)0);
        if (end >= 0)
        {
            raw = raw[..end];
        }

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

    private static bool HasAnyNonZero(ExpandedView view, int offset, int size) => CountNonZero(view, offset, size) > 0;

    private static int CountNonZero(ExpandedView view, int offset, int size)
    {
        if (!view.CanRead(offset, size))
        {
            return 0;
        }

        var span = view.Data.AsSpan(view.ToDataOffset(offset), size);
        var count = 0;
        foreach (var b in span)
        {
            if (b != 0)
            {
                count++;
            }
        }

        return count;
    }

    private static ParsedFieldNode MissingNode(int offset, int size, string type, string name, ExpandedView view)
    {
        return Node(offset, size, type, name, "", "Unaligned",
            view.MissingPrefix > 0 && offset < view.MissingPrefix
                ? "after_body 文件缺少 full 前 0x0E 字节；请选择 after_full"
                : "payload 长度不足，无法读取该字段");
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
            StructureText = "Not a MsgID 509 payload.",
            RootNodes =
            [
                Node(0, 0, "parser", "UnsupportedMsg509", $"MsgID {packet.MsgIdDisplay}", "not connected", note)
            ]
        };
    }

    private sealed class ExpandedView(byte[] data, int missingPrefix)
    {
        public byte[] Data { get; } = data;
        public int MissingPrefix { get; } = missingPrefix;

        public int ToDataOffset(int fullOffset) => fullOffset - MissingPrefix;

        public bool CanRead(int fullOffset, int size)
        {
            var offset = ToDataOffset(fullOffset);
            return offset >= 0 && size >= 0 && offset + size <= Data.Length;
        }
    }
}
