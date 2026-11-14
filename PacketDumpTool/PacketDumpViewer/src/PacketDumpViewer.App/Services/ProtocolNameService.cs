using System.IO;
using System.Text.RegularExpressions;

namespace PacketDumpViewer.App.Services;

public sealed class ProtocolNameService
{
    private static readonly Regex MapLineRegex = new(
        @"^\s*0x(?<id>[0-9A-Fa-f]+):\s*\(""(?<name>[^""]+)""",
        RegexOptions.Compiled | RegexOptions.CultureInvariant);

    public static ProtocolNameService Shared { get; } = new();

    private readonly Dictionary<int, string> _systemMsgIds = [];
    private readonly Dictionary<int, string> _innerCmds = [];
    private readonly Dictionary<uint, string> _itemNames = [];

    private ProtocolNameService()
    {
        LoadCmdMap();
    }

    public string DescribeMsgId(int? msgId)
    {
        if (msgId is null)
        {
            return "Unknown";
        }

        if (msgId.Value == 316)
        {
            return "RequestSaveShapeRefitNew / CResponseSaveShapeRefitPara";
        }

        if (msgId.Value == 102)
        {
            return "CResponseGetRoomListPara / 房间列表";
        }

        if (msgId.Value == 103)
        {
            return "CSSafeCRequestCreateRoomPara / CResponseCreateRoomPara / 创建房间";
        }

        if (msgId.Value == 110)
        {
            return "CSSafeCRequestStartGamePara / CResponseStartGamePara / 开始游戏";
        }

        if (msgId.Value == 120)
        {
            return "CSSafeCRequestPrepareReadyPara / CResponsePrepareReadyPara / 准备加载";
        }

        if (msgId.Value == 509)
        {
            return "CSSafeCNotifyGameBeginPara / CNotifyGameBeginPara";
        }

        if (_systemMsgIds.TryGetValue(msgId.Value, out var systemName))
        {
            return systemName;
        }

        if (_innerCmds.TryGetValue(msgId.Value, out var innerName))
        {
            return innerName;
        }

        return msgId.Value switch
        {
            100 => "MsgID_0x64_LoginPara_like",
            102 => "CResponseGetRoomListPara / 房间列表",
            103 => "CSSafeCRequestCreateRoomPara / CResponseCreateRoomPara / 创建房间",
            110 => "CSSafeCRequestStartGamePara / CResponseStartGamePara / 开始游戏",
            120 => "CSSafeCRequestPrepareReadyPara / CResponsePrepareReadyPara / 准备加载",
            90 => "CResponseC2HCPickupItemPara",
            223 => "CNotifyLoginMoreInfoPara",
            316 => "RequestSaveShapeRefitNew / CResponseSaveShapeRefitPara",
            509 => "CSSafeCNotifyGameBeginPara / CNotifyGameBeginPara",
            24251 => "CRequest/CResponseQuickRPCPara",
            _ => "Unknown"
        };
    }

    public string DescribeItemId(uint itemId)
    {
        return _itemNames.TryGetValue(itemId, out var name) ? name : "UnknownItem";
    }

    private void LoadCmdMap()
    {
        var cmdMapPath = @"F:\new1\qqspeed_decoder\cmd_map.py";
        if (!File.Exists(cmdMapPath))
        {
            return;
        }

        var currentMap = "";
        foreach (var line in File.ReadLines(cmdMapPath))
        {
            if (line.StartsWith("INNER_CMD_HIGH:", StringComparison.Ordinal) ||
                line.StartsWith("INNER_CMD_MID:", StringComparison.Ordinal))
            {
                currentMap = "inner";
                continue;
            }

            if (line.StartsWith("SYSTEM_MSGID:", StringComparison.Ordinal))
            {
                currentMap = "system";
                continue;
            }

            if (line.StartsWith("ITEM_NAME:", StringComparison.Ordinal))
            {
                currentMap = "item";
                continue;
            }

            if (line.StartsWith("OUTER_CMD:", StringComparison.Ordinal) ||
                line.StartsWith("EXPERIENTIAL_CMD:", StringComparison.Ordinal) ||
                line.StartsWith("ITEM_CATEGORY:", StringComparison.Ordinal))
            {
                currentMap = "";
                continue;
            }

            if (currentMap.Length == 0)
            {
                continue;
            }

            if (currentMap == "item")
            {
                if (line.TrimStart().StartsWith('#'))
                {
                    continue;
                }

                foreach (Match itemMatch in Regex.Matches(line, @"(?<id>\d+):\s*""(?<name>[^""]+)"""))
                {
                    _itemNames[uint.Parse(itemMatch.Groups["id"].Value)] = itemMatch.Groups["name"].Value;
                }
                continue;
            }

            var match = MapLineRegex.Match(line);
            if (!match.Success)
            {
                continue;
            }

            var id = Convert.ToInt32(match.Groups["id"].Value, 16);
            var name = match.Groups["name"].Value;
            if (currentMap == "system")
            {
                _systemMsgIds[id] = name;
            }
            else
            {
                _innerCmds.TryAdd(id, name);
            }
        }
    }
}
