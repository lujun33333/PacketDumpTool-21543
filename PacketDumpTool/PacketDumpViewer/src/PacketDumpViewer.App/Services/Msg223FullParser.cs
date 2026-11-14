using System.IO;
using System.Text;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class Msg223FullParser
{
    public const int FullLengthCurrent21273 = 700668;
    public const int BodySliceLengthCurrent21273 = 700654;

    private const int ItemNumOffset = 0x00000000;
    private const int ItemArrayOffset = 0x00000002;
    private const int ItemCapacity = 8050;
    private const int ItemStride = 23;

    private const int TaskNumOffset = ItemArrayOffset + ItemCapacity * ItemStride; // 0x0002D340
    private const int TaskArrayOffset = TaskNumOffset + 2;
    private const int TaskCapacity = 1100;
    private const int TaskStride = 24;

    private const int PetNumOffset = TaskArrayOffset + TaskCapacity * TaskStride; // 0x00033A62
    private const int PetArrayOffset = PetNumOffset + 2;
    private const int PetCapacity = 500;
    private const int PetStride = 191;
    private const int PetSkillCapacity = 16;
    private const int PetSkillStride = 8;

    private const int MapNumOffset = PetArrayOffset + PetCapacity * PetStride; // 0x0004AF70
    private const int MapArrayOffset = MapNumOffset + 2;
    private const int MapCapacity = 500;
    private const int MapStride = 12;

    private const int OverdueItemNumOffset = MapArrayOffset + MapCapacity * MapStride; // 0x0004C6E2
    private const int OverdueItemArrayOffset = OverdueItemNumOffset + 1;
    private const int OverdueItemCapacity = 100;
    private const int OverdueItemStride = 12;

    private const int StoneKartNumOffset = OverdueItemArrayOffset + OverdueItemCapacity * OverdueItemStride; // 0x0004CB8B
    private const int StoneKartArrayOffset = StoneKartNumOffset + 4;
    private const int StoneKartCapacity = 8050;
    private const int StoneKartStride = 48;
    private const int StoneGrooveCapacity = 5;
    private const int StoneGrooveStride = 8;

    private const int HasNextOffset = StoneKartArrayOffset + StoneKartCapacity * StoneKartStride; // 0x000AB0F7
    private const int DecodeCheckFieldOffset = HasNextOffset + 1; // 0x000AB0F8

    private const int MaxExpandedRecords = 256;

    static Msg223FullParser()
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
    }

    public ParserStatus Parse(PacketRecord packet)
    {
        if (packet.MsgId != 223)
        {
            return Unsupported(packet, "不是 MsgID 223。");
        }

        if (!File.Exists(packet.FullPath))
        {
            return new ParserStatus
            {
                Status = "file missing",
                Note = "payload 文件不存在。",
                StructureText = "Payload file missing.",
                RootNodes =
                [
                    Node(0, 0, "error", "PayloadFileMissing", packet.FullPath, "Unaligned", "无法读取 payload 文件")
                ]
            };
        }

        var data = File.ReadAllBytes(packet.FullPath);
        var sourceName = packet.FileName;
        var sourceNote = "selected full payload";

        if (data.Length == BodySliceLengthCurrent21273)
        {
            if (!TryFindMatchingFullSibling(packet.FullPath, data, out var fullData, out var fullName))
            {
                return BuildBodySliceWarning(packet, data);
            }

            data = fullData;
            sourceName = fullName;
            sourceNote = "当前选择的是 body 切片；已在同目录匹配 *_full_msg223_len700668.bin 后按 full 结构解析";
        }
        else if (packet.Direction.Contains("_body", StringComparison.OrdinalIgnoreCase) && data.Length != FullLengthCurrent21273)
        {
            return Unsupported(packet, "MsgID 223 该文件是 body/inner/preview 视图，不是 700668 bytes expanded full；请选择 recv_plain_full 或 special28346_after_full。");
        }

        if (data.Length != FullLengthCurrent21273)
        {
            return Unsupported(packet, $"MsgID 223 目前只解析 21273/current 展开 full 长度 {FullLengthCurrent21273:N0} bytes；当前长度 {data.Length:N0} bytes。");
        }

        return ParseFullBytes(data, sourceName, packet.Direction, sourceNote);
    }

    private static ParserStatus ParseFullBytes(byte[] data, string sourceName, string sourceDirection, string sourceNote)
    {
        var root = Node(
            0,
            data.Length,
            "struct",
            "CSSafeCNotifyLoginMoreInfoPara",
            $"{data.Length:N0} bytes",
            "Verified",
            $"MsgID 223 NotifyLoginMoreInfo expanded full；source={sourceName}; {sourceNote}");

        AddEvidence(root, data, sourceDirection);
        AddItems(root, data);
        AddTasks(root, data);
        AddPets(root, data);
        AddMaps(root, data);
        AddOverdueItems(root, data);
        AddStoneKarts(root, data);
        AddControlTail(root, data);

        return new ParserStatus
        {
            Status = "parsed",
            Note = "MsgID 223 已按 21273 Network.dll writer 展开布局解析；body 切片不是权威起点，优先看 *_full_msg223_len700668.bin。",
            StructureText = "Msg223 structure tree loaded.",
            RootNodes = [root]
        };
    }

    private static void AddEvidence(ParsedFieldNode root, byte[] data, string sourceDirection)
    {
        root.AddChild(Node(
            0,
            data.Length,
            "coverage",
            "FullPayloadCoverage",
            $"covered=0x00000000-0x{data.Length - 1:X8}; len={data.Length:N0}/0x{data.Length:X}",
            "Verified",
            "21273 Network.dll sub_18032F740 writes exactly 700668 bytes: fixed slots + HasNext + decode check field"));

        root.AddChild(Node(
            0,
            0,
            "note",
            "BodySlicePolicy",
            "Msg223 full starts at ItemNum, not a 14-byte client header",
            sourceDirection.Contains("_body", StringComparison.OrdinalIgnoreCase) ? "Unaligned" : "Verified",
            "dump 的 recv_plain_body/special28346_after_body 会从 special-decoded full 里裁掉前 0x0E 字节；这会丢失 ItemNum 和第一个道具头部，不能当结构起点"));

        root.AddChild(Node(
            0,
            0,
            "note",
            "UnknownFieldPolicy",
            "only names backed by 21273 writer/source comments are named",
            "Verified",
            "未确认语义的尾字段保留 UnknownName/Reserved；不把 0 或 padding 强行命名成业务字段"));

        var layout = root.AddChild(Group(0, data.Length, "LayoutMap", "固定展开布局；offset 为 Msg223 full offset"));
        layout.AddChild(Node(ItemNumOffset, 2, "le:u16", "m_ushItemNum", U16(data, ItemNumOffset).ToString(), "Verified", "active item count"));
        layout.AddChild(Node(ItemArrayOffset, ItemCapacity * ItemStride, "array", "m_astPlayerDBItemInfo[]", $"capacity={ItemCapacity}; stride={ItemStride}", "Verified", "fixed item slots"));
        layout.AddChild(Node(TaskNumOffset, 2, "le:u16", "m_ushTaskNum", U16(data, TaskNumOffset).ToString(), "Verified", "active task count"));
        layout.AddChild(Node(TaskArrayOffset, TaskCapacity * TaskStride, "array", "m_astPlayerDBTaskInfo[]", $"capacity={TaskCapacity}; stride={TaskStride}", "Verified", "fixed task slots"));
        layout.AddChild(Node(PetNumOffset, 2, "le:u16", "m_usPetNum", U16(data, PetNumOffset).ToString(), "Verified", "active pet count"));
        layout.AddChild(Node(PetArrayOffset, PetCapacity * PetStride, "array", "m_astPetInfo[]", $"capacity={PetCapacity}; stride={PetStride}", "Verified", "fixed pet slots"));
        layout.AddChild(Node(MapNumOffset, 2, "le:u16", "m_usMapNum", U16(data, MapNumOffset).ToString(), "Verified", "active map record count"));
        layout.AddChild(Node(MapArrayOffset, MapCapacity * MapStride, "array", "m_astRecord[]", $"capacity={MapCapacity}; stride={MapStride}", "Verified", "fixed map slots"));
        layout.AddChild(Node(OverdueItemNumOffset, 1, "u8", "m_uchOverdueItemNum", U8(data, OverdueItemNumOffset).ToString(), "Verified", "active overdue item count"));
        layout.AddChild(Node(OverdueItemArrayOffset, OverdueItemCapacity * OverdueItemStride, "array", "m_astOverdueItem[]", $"capacity={OverdueItemCapacity}; stride={OverdueItemStride}", "Verified", "fixed overdue item slots"));
        layout.AddChild(Node(StoneKartNumOffset, 4, "le:u32", "m_iStoneKartNum", U32(data, StoneKartNumOffset).ToString(), "Verified", "active stone-kart count"));
        layout.AddChild(Node(StoneKartArrayOffset, StoneKartCapacity * StoneKartStride, "array", "m_astKartStoneGrooveInfo[]", $"capacity={StoneKartCapacity}; stride={StoneKartStride}", "Verified", "fixed stone kart slots"));
        layout.AddChild(Node(HasNextOffset, 1, "u8", "m_bHasNext", U8(data, HasNextOffset).ToString(), "Verified", "more 223 chunks follow"));
        layout.AddChild(Node(DecodeCheckFieldOffset, 4, "le:u32", "DecodeCheckField", Hex(data, DecodeCheckFieldOffset, 4), "UnknownName", "special28346 decode guard/check value copied by sub_18032F740; not a game business field"));
    }

    private static void AddItems(ParsedFieldNode root, byte[] data)
    {
        var count = U16(data, ItemNumOffset);
        Add(root, data, ItemNumOffset, 2, "le:u16", "m_ushItemNum", count.ToString(), "Verified", "active item count; current dumps usually 198/199/200/99");

        var array = AddArray(root, data, ItemArrayOffset, ItemCapacity, ItemStride, count, "m_astPlayerDBItemInfo[]", "m_ushItemNum", "fixed 8050 item slots; active count controls visible business records");
        var visible = VisibleCount(count, ItemCapacity);
        var expanded = Math.Min(visible, MaxExpandedRecords);
        for (var i = 0; i < expanded; i++)
        {
            var offset = ItemArrayOffset + i * ItemStride;
            var itemId = U32(data, offset);
            var itemName = ProtocolNameService.Shared.DescribeItemId(itemId);
            var item = array.AddChild(Node(
                offset,
                ItemStride,
                "group",
                $"m_astPlayerDBItemInfo[{i}]",
                $"ItemID={itemId} ({itemName}), Num={U32(data, offset + 4)}, Type={U16(data, offset + 21)}",
                "Dynamic",
                "active login-more item record; layout matches Msg100 item stride without per-record length prefix"));
            Add(item, data, offset + 0, 4, "le:u32", $"m_astPlayerDBItemInfo[{i}].m_iItemID", $"{itemId} ({itemName})", "Dynamic", "item id");
            Add(item, data, offset + 4, 4, "le:u32", $"m_astPlayerDBItemInfo[{i}].m_iItemNum", U32(data, offset + 4).ToString(), "Dynamic", "stack/count");
            Add(item, data, offset + 8, 4, "le:u32", $"m_astPlayerDBItemInfo[{i}].m_iAvailPeriod", PeriodValue(U32(data, offset + 8)), "Dynamic", "available period; 0 usually means permanent/default");
            Add(item, data, offset + 12, 1, "u8", $"m_astPlayerDBItemInfo[{i}].m_cStatus", U8(data, offset + 12).ToString(), "Dynamic", "item status");
            Add(item, data, offset + 13, 4, "le:u32", $"m_astPlayerDBItemInfo[{i}].m_iObtainTime", TimeValue(U32(data, offset + 13)), "Dynamic", "obtain unix time when plausible");
            Add(item, data, offset + 17, 4, "le:u32", $"m_astPlayerDBItemInfo[{i}].m_iOtherAttribute", U32(data, offset + 17).ToString(), "Dynamic", "other attribute");
            Add(item, data, offset + 21, 2, "le:u16", $"m_astPlayerDBItemInfo[{i}].m_shItemType", U16(data, offset + 21).ToString(), "Dynamic", "item type");
        }

        AddArrayTail(array, data, ItemArrayOffset, ItemCapacity, ItemStride, visible, expanded, "m_astPlayerDBItemInfo");
    }

    private static void AddTasks(ParsedFieldNode root, byte[] data)
    {
        var count = U16(data, TaskNumOffset);
        Add(root, data, TaskNumOffset, 2, "le:u16", "m_ushTaskNum", count.ToString(), "Verified", "active task count");

        var array = AddArray(root, data, TaskArrayOffset, TaskCapacity, TaskStride, count, "m_astPlayerDBTaskInfo[]", "m_ushTaskNum", "fixed 1100 task slots; field names from server source comment and 21273 writer order");
        var visible = VisibleCount(count, TaskCapacity);
        var expanded = Math.Min(visible, MaxExpandedRecords);
        for (var i = 0; i < expanded; i++)
        {
            var offset = TaskArrayOffset + i * TaskStride;
            var task = array.AddChild(Node(offset, TaskStride, "group", $"m_astPlayerDBTaskInfo[{i}]", $"TaskID={U32(data, offset)}, Status={U8(data, offset + 6)}", "Dynamic", "active task record"));
            Add(task, data, offset + 0, 4, "le:u32", $"m_astPlayerDBTaskInfo[{i}].m_iTaskID", U32(data, offset + 0).ToString(), "Dynamic", "task id");
            Add(task, data, offset + 4, 2, "le:u16", $"m_astPlayerDBTaskInfo[{i}].m_shOptionID", U16(data, offset + 4).ToString(), "Dynamic", "task option id");
            Add(task, data, offset + 6, 1, "u8", $"m_astPlayerDBTaskInfo[{i}].m_cStatus", U8(data, offset + 6).ToString(), "Dynamic", "task status");
            Add(task, data, offset + 7, 1, "u8", $"m_astPlayerDBTaskInfo[{i}].m_bPrevStatus", U8(data, offset + 7).ToString(), "Dynamic", "previous task status");
            Add(task, data, offset + 8, 4, "le:u32", $"m_astPlayerDBTaskInfo[{i}].m_iProgressRate", U32(data, offset + 8).ToString(), "Dynamic", "progress rate");
            Add(task, data, offset + 12, 4, "le:u32", $"m_astPlayerDBTaskInfo[{i}].m_iOther", U32(data, offset + 12).ToString(), "Dynamic", "other task value");
            Add(task, data, offset + 16, 4, "le:u32", $"m_astPlayerDBTaskInfo[{i}].m_uiTakeTime", TimeValue(U32(data, offset + 16)), "Dynamic", "take time");
            Add(task, data, offset + 20, 4, "le:u32", $"m_astPlayerDBTaskInfo[{i}].m_uiLastUpdateTime", TimeValue(U32(data, offset + 20)), "Dynamic", "last update time");
        }

        AddArrayTail(array, data, TaskArrayOffset, TaskCapacity, TaskStride, visible, expanded, "m_astPlayerDBTaskInfo");
    }

    private static void AddPets(ParsedFieldNode root, byte[] data)
    {
        var count = U16(data, PetNumOffset);
        Add(root, data, PetNumOffset, 2, "le:u16", "m_usPetNum", count.ToString(), "Verified", "active pet count");

        var array = AddArray(root, data, PetArrayOffset, PetCapacity, PetStride, count, "m_astPetInfo[]", "m_usPetNum", "fixed 500 pet slots; pet skill subarray is 16 * 8 in Msg223 writer");
        var visible = VisibleCount(count, PetCapacity);
        var expanded = Math.Min(visible, MaxExpandedRecords);
        for (var i = 0; i < expanded; i++)
        {
            var offset = PetArrayOffset + i * PetStride;
            var petId = U32(data, offset + 4);
            var petName = TextValue(data, offset + 8, 23);
            var level = U16(data, offset + 34);
            var skillNum = U16(data, offset + 49);
            var pet = array.AddChild(Node(
                offset,
                PetStride,
                "group",
                $"m_astPetInfo[{i}]",
                $"PetID={petId}, Name={petName}, Level={level}, SkillNum={skillNum}",
                "Dynamic",
                "active pet record; field names partly from Msg100/Network writer, unconfirmed tail stays UnknownName"));
            Add(pet, data, offset + 0, 4, "le:u32", $"m_astPetInfo[{i}].m_uiUin", U32(data, offset + 0).ToString(), "Dynamic", "owner uin");
            Add(pet, data, offset + 4, 4, "le:u32", $"m_astPetInfo[{i}].m_iPetId", petId.ToString(), "Dynamic", "pet id");
            Add(pet, data, offset + 8, 23, "gbk[23]", $"m_astPetInfo[{i}].m_szPetName", petName, "Dynamic", "pet name bytes; writer copies 23 bytes in Msg223 full");
            Add(pet, data, offset + 31, 1, "u8", $"m_astPetInfo[{i}].UnknownByte_31", U8(data, offset + 31).ToString(), "UnknownName", "present between pet name and level fields; semantic not confirmed");
            Add(pet, data, offset + 32, 2, "le:u16", $"m_astPetInfo[{i}].LevelLimit", U16(data, offset + 32).ToString(), "Dynamic", "field name inherited from Msg100 evidence");
            Add(pet, data, offset + 34, 2, "le:u16", $"m_astPetInfo[{i}].m_usLevel", level.ToString(), "Dynamic", "pet level");
            Add(pet, data, offset + 36, 4, "le:u32", $"m_astPetInfo[{i}].CurLevelExp", U32(data, offset + 36).ToString(), "Dynamic", "current level exp");
            Add(pet, data, offset + 40, 4, "le:u32", $"m_astPetInfo[{i}].CurLevelMaxExp", U32(data, offset + 40).ToString(), "Dynamic", "current level max exp");
            Add(pet, data, offset + 44, 1, "u8", $"m_astPetInfo[{i}].m_ucStatus", U8(data, offset + 44).ToString(), "Dynamic", "pet status");
            Add(pet, data, offset + 45, 2, "le:u16", $"m_astPetInfo[{i}].PL", U16(data, offset + 45).ToString(), "Dynamic", "PL field; exact UI label not confirmed");
            Add(pet, data, offset + 47, 2, "le:u16", $"m_astPetInfo[{i}].TotalPL", U16(data, offset + 47).ToString(), "Dynamic", "total PL field; exact UI label not confirmed");
            Add(pet, data, offset + 49, 2, "le:u16", $"m_astPetInfo[{i}].m_usPetSkillNum", skillNum.ToString(), "Dynamic", "active pet skill count");
            AddPetSkillList(pet, data, offset + 51, i, skillNum);
            Add(pet, data, offset + 179, 4, "le:u32", $"m_astPetInfo[{i}].m_iStrengLevel", U32(data, offset + 179).ToString(), "Dynamic", "field name from prior Network/Msg100 evidence");
            Add(pet, data, offset + 183, 1, "u8", $"m_astPetInfo[{i}].UnknownByte_183", U8(data, offset + 183).ToString(), "UnknownName", "writer leaves a single byte before 4-byte common tail; semantic not confirmed");
            Add(pet, data, offset + 184, 4, "le:u32", $"m_astPetInfo[{i}].CommonTailU32_184", U32(data, offset + 184).ToString(), "UnknownName", "Network.dll has common-info tail members, but this dword semantic is not yet pinned");
            Add(pet, data, offset + 188, 1, "u8", $"m_astPetInfo[{i}].CommonTailByte_188", U8(data, offset + 188).ToString(), "UnknownName", "pet tail byte");
            Add(pet, data, offset + 189, 1, "u8", $"m_astPetInfo[{i}].CommonTailByte_189", U8(data, offset + 189).ToString(), "UnknownName", "pet tail byte");
            Add(pet, data, offset + 190, 1, "u8", $"m_astPetInfo[{i}].CommonTailByte_190", U8(data, offset + 190).ToString(), "UnknownName", "pet tail byte");
        }

        AddArrayTail(array, data, PetArrayOffset, PetCapacity, PetStride, visible, expanded, "m_astPetInfo");
    }

    private static void AddPetSkillList(ParsedFieldNode pet, byte[] data, int offset, int petIndex, int skillNum)
    {
        var visible = Math.Min(skillNum, PetSkillCapacity);
        var group = pet.AddChild(Node(
            offset,
            PetSkillCapacity * PetSkillStride,
            "array",
            $"m_astPetInfo[{petIndex}].m_astPetSkillList[]",
            $"count={skillNum}; visible={visible}; capacity={PetSkillCapacity}; stride={PetSkillStride}",
            skillNum == 0 ? "EmptyArray" : "Dynamic",
            "Msg223 writer has 16 fixed pet-skill slots"));
        for (var i = 0; i < PetSkillCapacity; i++)
        {
            var status = i < visible ? "Dynamic" : "Reserved";
            var note = i < visible ? "active pet skill record" : "inactive fixed pet skill slot";
            var skillOffset = offset + i * PetSkillStride;
            var prefix = $"m_astPetInfo[{petIndex}].m_astPetSkillList[{i}]";
            var skill = group.AddChild(Node(skillOffset, PetSkillStride, "group", prefix, $"SkillID={U16(data, skillOffset)}, Value={U32(data, skillOffset + 4)}", status, note));
            Add(skill, data, skillOffset + 0, 2, "le:u16", $"{prefix}.m_usSkillID", U16(data, skillOffset + 0).ToString(), status, note);
            Add(skill, data, skillOffset + 2, 1, "u8", $"{prefix}.m_ucStatus", U8(data, skillOffset + 2).ToString(), status, note);
            Add(skill, data, skillOffset + 3, 1, "u8", $"{prefix}.m_cActive", U8(data, skillOffset + 3).ToString(), status, note);
            Add(skill, data, skillOffset + 4, 4, "le:u32", $"{prefix}.m_iValue", U32(data, skillOffset + 4).ToString(), status, note);
        }
    }

    private static void AddMaps(ParsedFieldNode root, byte[] data)
    {
        var count = U16(data, MapNumOffset);
        Add(root, data, MapNumOffset, 2, "le:u16", "m_usMapNum", count.ToString(), "Verified", "active map record count");

        var array = AddArray(root, data, MapArrayOffset, MapCapacity, MapStride, count, "m_astRecord[]", "m_usMapNum", "fixed 500 map records");
        var visible = VisibleCount(count, MapCapacity);
        var expanded = Math.Min(visible, MaxExpandedRecords);
        for (var i = 0; i < expanded; i++)
        {
            var offset = MapArrayOffset + i * MapStride;
            var map = array.AddChild(Node(offset, MapStride, "group", $"m_astRecord[{i}]", $"MapID={U32(data, offset)}, Record={U32(data, offset + 4)}", "Dynamic", "active map record"));
            Add(map, data, offset + 0, 4, "le:u32", $"m_astRecord[{i}].m_iMapID", U32(data, offset + 0).ToString(), "Dynamic", "map id");
            Add(map, data, offset + 4, 4, "le:u32", $"m_astRecord[{i}].m_iRecord", U32(data, offset + 4).ToString(), "Dynamic", "record value");
            Add(map, data, offset + 8, 4, "le:u32", $"m_astRecord[{i}].m_iLastUpdateTime", TimeValue(U32(data, offset + 8)), "Dynamic", "last update time");
        }

        AddArrayTail(array, data, MapArrayOffset, MapCapacity, MapStride, visible, expanded, "m_astRecord");
    }

    private static void AddOverdueItems(ParsedFieldNode root, byte[] data)
    {
        var count = U8(data, OverdueItemNumOffset);
        Add(root, data, OverdueItemNumOffset, 1, "u8", "m_uchOverdueItemNum", count.ToString(), "Verified", "active overdue item count; 21273 writer caps it at 100");

        var array = AddArray(root, data, OverdueItemArrayOffset, OverdueItemCapacity, OverdueItemStride, count, "m_astOverdueItem[]", "m_uchOverdueItemNum", "fixed 100 overdue item slots");
        var visible = VisibleCount(count, OverdueItemCapacity);
        var expanded = Math.Min(visible, MaxExpandedRecords);
        for (var i = 0; i < expanded; i++)
        {
            var offset = OverdueItemArrayOffset + i * OverdueItemStride;
            var itemId = U32(data, offset);
            var itemName = ProtocolNameService.Shared.DescribeItemId(itemId);
            var item = array.AddChild(Node(offset, OverdueItemStride, "group", $"m_astOverdueItem[{i}]", $"ItemID={itemId} ({itemName}), OverdueTime={U32(data, offset + 4)}", "Dynamic", "active overdue item record"));
            Add(item, data, offset + 0, 4, "le:u32", $"m_astOverdueItem[{i}].m_iItemID", $"{itemId} ({itemName})", "Dynamic", "overdue item id");
            Add(item, data, offset + 4, 4, "le:u32", $"m_astOverdueItem[{i}].m_iOverdueTime", TimeValue(U32(data, offset + 4)), "Dynamic", "overdue time");
            Add(item, data, offset + 8, 4, "le:u32", $"m_astOverdueItem[{i}].m_iOtherAttr", U32(data, offset + 8).ToString(), "Dynamic", "other overdue item attribute");
        }

        AddArrayTail(array, data, OverdueItemArrayOffset, OverdueItemCapacity, OverdueItemStride, visible, expanded, "m_astOverdueItem");
    }

    private static void AddStoneKarts(ParsedFieldNode root, byte[] data)
    {
        var count = U32(data, StoneKartNumOffset);
        Add(root, data, StoneKartNumOffset, 4, "le:u32", "m_iStoneKartNum", count.ToString(), "Verified", "active stone-kart count; 21273 writer caps it at 8050");

        var array = AddArray(root, data, StoneKartArrayOffset, StoneKartCapacity, StoneKartStride, count, "m_astKartStoneGrooveInfo[]", "m_iStoneKartNum", "fixed 8050 stone-kart slots; source comment names KartID/StoneGrooveNum/StoneGrooveInfo");
        var visible = VisibleCount(count, StoneKartCapacity);
        var expanded = Math.Min(visible, MaxExpandedRecords);
        for (var i = 0; i < expanded; i++)
        {
            var offset = StoneKartArrayOffset + i * StoneKartStride;
            var grooveCount = U32(data, offset + 4);
            var kart = array.AddChild(Node(offset, StoneKartStride, "group", $"m_astKartStoneGrooveInfo[{i}]", $"KartID={U32(data, offset)}, StoneGrooveNum={grooveCount}", "Dynamic", "active stone-kart record"));
            Add(kart, data, offset + 0, 4, "le:u32", $"m_astKartStoneGrooveInfo[{i}].m_iKartID", U32(data, offset + 0).ToString(), "Dynamic", "kart id");
            Add(kart, data, offset + 4, 4, "le:u32", $"m_astKartStoneGrooveInfo[{i}].m_iStoneGrooveNum", grooveCount.ToString(), "Dynamic", "active groove count capped at 5");
            var grooves = kart.AddChild(Node(offset + 8, StoneGrooveCapacity * StoneGrooveStride, "array", $"m_astKartStoneGrooveInfo[{i}].m_astStoneGrooveInfo[]", $"count={grooveCount}; capacity={StoneGrooveCapacity}; stride={StoneGrooveStride}", grooveCount == 0 ? "EmptyArray" : "Dynamic", "source comment names m_iStoneUseOccaType / m_iSkillStoneID"));
            for (var j = 0; j < StoneGrooveCapacity; j++)
            {
                var grooveOffset = offset + 8 + j * StoneGrooveStride;
                var status = j < grooveCount ? "Dynamic" : "Reserved";
                var note = j < grooveCount ? "active stone groove" : "inactive fixed groove slot";
                var prefix = $"m_astKartStoneGrooveInfo[{i}].m_astStoneGrooveInfo[{j}]";
                var groove = grooves.AddChild(Node(grooveOffset, StoneGrooveStride, "group", prefix, $"UseOccaType={U32(data, grooveOffset)}, SkillStoneID={U32(data, grooveOffset + 4)}", status, note));
                Add(groove, data, grooveOffset + 0, 4, "le:u32", $"{prefix}.m_iStoneUseOccaType", U32(data, grooveOffset + 0).ToString(), status, "field name from server source comment");
                Add(groove, data, grooveOffset + 4, 4, "le:u32", $"{prefix}.m_iSkillStoneID", U32(data, grooveOffset + 4).ToString(), status, "field name from server source comment");
            }
        }

        AddArrayTail(array, data, StoneKartArrayOffset, StoneKartCapacity, StoneKartStride, visible, expanded, "m_astKartStoneGrooveInfo");
    }

    private static void AddControlTail(ParsedFieldNode root, byte[] data)
    {
        Add(root, data, HasNextOffset, 1, "u8", "m_bHasNext", U8(data, HasNextOffset).ToString(), "Verified", "1 means more login-more chunks follow; 0 means final chunk");
        Add(root, data, DecodeCheckFieldOffset, 4, "le:u32", "DecodeCheckField", $"0x{U32(data, DecodeCheckFieldOffset):X8}", "UnknownName", "special28346 decode guard/check copied to tail by Network.dll; used for decode validation, not a business structure member");
    }

    private static ParsedFieldNode AddArray(ParsedFieldNode parent, byte[] data, int offset, int capacity, int stride, long count, string name, string countName, string note)
    {
        var visible = VisibleCount(count, capacity);
        var expanded = Math.Min(visible, MaxExpandedRecords);
        var status = count == 0 ? "EmptyArray" : count > capacity ? "Unaligned" : "Verified";
        var array = parent.AddChild(Node(
            offset,
            capacity * stride,
            "array",
            name,
            $"count={count}; visible={visible}; expanded={expanded}; capacity={capacity}; stride={stride}",
            status,
            $"{note}; count field={countName}"));
        array.AddChild(Node(
            offset,
            0,
            "policy",
            "FixedSlotArrayPolicy",
            count == 0
                ? "count=0 => no active records; following bytes are reserved fixed slots"
                : $"only first count records are active; unused slots remain reserved; UI expands at most {MaxExpandedRecords}",
            "Verified",
            "0 值槽位不代表业务字段缺失；官服 writer 固定展开 capacity 个槽"));
        if (count > capacity)
        {
            array.Status = "Unaligned";
            array.Note = $"{name} count={count} exceeds capacity={capacity}";
        }

        return array;
    }

    private static void AddArrayTail(ParsedFieldNode array, byte[] data, int arrayOffset, int capacity, int stride, int visible, int expanded, string namePrefix)
    {
        if (visible > expanded)
        {
            var omittedOffset = arrayOffset + expanded * stride;
            var omittedSize = (visible - expanded) * stride;
            AddRawBlock(array, data, omittedOffset, omittedSize, $"{namePrefix}[].ActiveRecordsOmittedByUI", $"active records {expanded}-{visible - 1} not expanded to keep UI responsive");
        }

        var reservedOffset = arrayOffset + visible * stride;
        var reservedSize = (capacity - visible) * stride;
        if (reservedSize > 0)
        {
            AddRawBlock(array, data, reservedOffset, reservedSize, $"{namePrefix}[].ReservedSlots", $"inactive fixed slots; active_count={visible}, capacity={capacity}");
        }
    }

    private static ParserStatus BuildBodySliceWarning(PacketRecord packet, byte[] data)
    {
        var root = Node(
            0,
            data.Length,
            "struct",
            "Msg223BodySliceMissingPrefix",
            $"{data.Length:N0} bytes",
            "Unaligned",
            "这个 body 文件是 full[0x0E:]，前 14 字节已经被 dump 视图裁掉；无法从当前文件还原 ItemNum 和第一个 item 头部");
        root.AddChild(Node(0, Math.Min(64, data.Length), "bytes", "SliceHead", Hex(data, 0, Math.Min(64, data.Length)), "Unaligned", "选择同目录 *_full_msg223_len700668.bin 可解析完整结构"));
        root.AddChild(Node(0, 0, "hint", "ExpectedFullFile", "*_recv_plain_full_msg223_len700668.bin 或 *_special28346_after_full_msg223_len700668.bin", "Verified", "Msg223 special decode 的 full 才是结构起点"));

        return new ParserStatus
        {
            Status = "unsupported body slice",
            Note = "MsgID 223 body 切片缺少前 0x0E 字节；未找到匹配 full sibling，不能完整解析。",
            StructureText = "Msg223 body slice is not authoritative.",
            RootNodes = [root]
        };
    }

    private static ParserStatus Unsupported(PacketRecord packet, string note)
    {
        return new ParserStatus
        {
            Status = "unsupported",
            Note = note,
            StructureText = "Not a supported Msg223 full payload.",
            RootNodes =
            [
                Node(0, (int)Math.Min(packet.FileSize, int.MaxValue), "parser", "Msg223ParserUnsupported", packet.FileName, "Unaligned", note)
            ]
        };
    }

    private static bool TryFindMatchingFullSibling(string selectedPath, byte[] bodySlice, out byte[] fullData, out string fullName)
    {
        fullData = [];
        fullName = "";

        if (bodySlice.Length != BodySliceLengthCurrent21273)
        {
            return false;
        }

        var dir = Path.GetDirectoryName(selectedPath);
        if (string.IsNullOrWhiteSpace(dir) || !Directory.Exists(dir))
        {
            return false;
        }

        foreach (var candidate in Directory.EnumerateFiles(dir, "*_msg223_len700668.bin"))
        {
            var name = Path.GetFileName(candidate);
            if (!name.Contains("_full_", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var bytes = File.ReadAllBytes(candidate);
            if (bytes.Length == FullLengthCurrent21273 && bytes.AsSpan(14).SequenceEqual(bodySlice))
            {
                fullData = bytes;
                fullName = name;
                return true;
            }
        }

        return false;
    }

    private static int VisibleCount(long count, int capacity)
    {
        if (count <= 0)
        {
            return 0;
        }

        return (int)Math.Min(count, capacity);
    }

    private static void Add(ParsedFieldNode parent, byte[] data, int offset, int size, string type, string name, string value, string status = "Verified", string note = "")
    {
        parent.AddChild(Node(offset, size, type, name, value, status, note));
    }

    private static ParsedFieldNode Group(int offset, int size, string name, string note, string status = "Verified")
    {
        return Node(offset, size, "group", name, "", status, note);
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

    private static void AddRawBlock(ParsedFieldNode parent, byte[] data, int offset, int size, string name, string note)
    {
        if (size <= 0)
        {
            return;
        }

        parent.AddChild(Node(offset, size, "raw", name, BlockSummary(data, offset, size), "Reserved", note));
    }

    private static string BlockSummary(byte[] data, int offset, int size)
    {
        var safeSize = Math.Max(0, Math.Min(size, data.Length - offset));
        if (safeSize == 0)
        {
            return "len=0";
        }

        var nonZero = 0;
        var firstNonZero = -1;
        var lastNonZero = -1;
        for (var i = 0; i < safeSize; i++)
        {
            if (data[offset + i] == 0)
            {
                continue;
            }

            nonZero++;
            firstNonZero = firstNonZero < 0 ? offset + i : firstNonZero;
            lastNonZero = offset + i;
        }

        var head = Hex(data, offset, Math.Min(safeSize, 16));
        return $"len={safeSize:N0}, nonzero={nonZero:N0}, firstNonZero={(firstNonZero < 0 ? "none" : $"0x{firstNonZero:X8}")}, lastNonZero={(lastNonZero < 0 ? "none" : $"0x{lastNonZero:X8}")}, head={head}";
    }

    private static string Hex(byte[] data, int offset, int size)
    {
        if (offset < 0 || offset >= data.Length || size <= 0)
        {
            return "";
        }

        var count = Math.Min(size, data.Length - offset);
        var hex = Convert.ToHexString(data, offset, count);
        return string.Join(' ', Enumerable.Range(0, hex.Length / 2).Select(i => hex.Substring(i * 2, 2)));
    }

    private static string TextValue(byte[] data, int offset, int size)
    {
        if (offset < 0 || offset >= data.Length || size <= 0)
        {
            return "";
        }

        var count = Math.Min(size, data.Length - offset);
        var raw = new byte[count];
        Array.Copy(data, offset, raw, 0, count);
        var zero = Array.IndexOf(raw, (byte)0);
        if (zero >= 0)
        {
            count = zero;
        }

        if (count == 0)
        {
            return "";
        }

        return Encoding.GetEncoding("GB18030").GetString(raw, 0, count);
    }

    private static string TimeValue(uint value)
    {
        if (value is >= 946684800 and <= 4102444800)
        {
            return $"{value} / {DateTimeOffset.FromUnixTimeSeconds(value):yyyy-MM-dd HH:mm:ss}";
        }

        return value.ToString();
    }

    private static string PeriodValue(uint value)
    {
        return value switch
        {
            0 => "0 / permanent-or-default",
            0xFFFFFFFF => "0xFFFFFFFF / unlimited",
            _ => value.ToString()
        };
    }

    private static byte U8(byte[] data, int offset)
    {
        return offset >= 0 && offset < data.Length ? data[offset] : (byte)0;
    }

    private static ushort U16(byte[] data, int offset)
    {
        return offset >= 0 && offset + 2 <= data.Length ? BitConverter.ToUInt16(data, offset) : (ushort)0;
    }

    private static uint U32(byte[] data, int offset)
    {
        return offset >= 0 && offset + 4 <= data.Length ? BitConverter.ToUInt32(data, offset) : 0;
    }
}
