using PacketDumpViewer.App.Models;
using PacketDumpViewer.App.Services;
using PacketDumpViewer.App.ViewModels;

var packets = new[]
{
    Packet(
        index: 10,
        direction: "S->C",
        endpoint: "127.0.0.1:33392 -> 121.14.75.34:10012",
        length: 90,
        decode: new PacketDecodePreview("100", "0", "7", "76", "plain", "ok", [0x00])),
    Packet(
        index: 2,
        direction: "C->S",
        endpoint: "127.0.0.1:33392 -> 121.14.75.34:10012",
        length: 24,
        decode: new PacketDecodePreview("90", "0", "1", "10", "plain", "ok", [0x00])),
    Packet(
        index: 11,
        direction: "S->C",
        endpoint: "127.0.0.1:33392 -> 121.14.75.34:10012",
        length: 32213,
        decode: new PacketDecodePreview("28346", "0", "8", "32199", "internal/raw-wrapper", "special wrapper", null))
};

var ascending = MainViewModel.BuildOnlinePacketRows(packets, "", "包序列 ↑ 小到大");
AssertSequence("ascending numeric packet index", ascending.Select(row => row.PacketIndex), ["2", "10", "11"]);

var descending = MainViewModel.BuildOnlinePacketRows(packets, "", "包序列 ↓ 大到小");
AssertSequence("descending numeric packet index", descending.Select(row => row.PacketIndex), ["11", "10", "2"]);

var msg28346 = MainViewModel.BuildOnlinePacketRows(packets, "28346", "包序列 ↑ 小到大");
AssertEqual("msgid search count", 1, msg28346.Count);
AssertEqual("msgid search keeps 28346", "28346", msg28346[0].MsgId);

AssertEqual(
    "packet function column is endpoint/ip",
    "127.0.0.1:33392 -> 121.14.75.34:10012",
    ascending[0].Function);

AssertEqual("row keeps original packet", 2, ascending[0].Packet?.PacketIndex);
AssertEqual("normal plaintext row is structure candidate", true, ascending[1].IsStructureCandidate);
AssertEqual("raw 28346 wrapper is not structure candidate", false, descending[0].IsStructureCandidate);

var splitPackets = new[]
{
    Packet(
        index: 1,
        direction: "S->C",
        endpoint: "Network.dll Unpack",
        length: 10,
        decode: new PacketDecodePreview("100", "1", "2", "10", "internal/plain-recv-full", "Network plain", [0x00])),
    Packet(
        index: 2,
        direction: "C->S",
        endpoint: "内部Hook / Top-Kart.dll RequestGameServer",
        length: 512,
        decode: new PacketDecodePreview("21100", "-", "-", "source-preview", "internal/topkart-source-object", "Top-Kart.dll RequestGameServer source object preview", [0x00]))
};
var networkOnlyRows = MainViewModel.BuildOnlinePacketRows(splitPackets, "", "包序列 ↑ 小到大", includeTopKart: false);
AssertSequence("network list excludes top-kart packets", networkOnlyRows.Select(row => row.MsgId), ["100"]);
var topOnlyRows = MainViewModel.BuildOnlinePacketRows(splitPackets, "", "包序列 ↑ 小到大", onlyTopKart: true);
AssertSequence("top list shows top-kart packets", topOnlyRows.Select(row => row.MsgId), ["21100"]);
AssertEqual("top row length is preview label", "预览 512B", topOnlyRows[0].Length);
var exactMsg100 = MainViewModel.BuildOnlinePacketRows(splitPackets, "100", "包序列 ↑ 小到大");
AssertSequence("numeric msgid search is exact", exactMsg100.Select(row => row.MsgId), ["100"]);

var linkedPackets = new[]
{
    Packet(
        index: 21,
        direction: "S->C",
        endpoint: "Network.dll Unpack",
        length: 12,
        decode: new PacketDecodePreview("316", "1", "2", "12", "internal/plain-recv-full", "direct 316", [0x00], EffectiveMsgId: 316, MsgIdDisplay: "316")),
    Packet(
        index: 22,
        direction: "S->C",
        endpoint: "Network.dll SpecialDecode",
        length: 317329,
        decode: new PacketDecodePreview("100", "1", "2", "317329", "internal/28346-inner", "28346 inner 100", [0x00], OuterMsgId: 28346, InnerMsgId: 100, EffectiveMsgId: 100, MsgIdDisplay: "28346 -> 100", WrapperKind: "28346-inner")),
    Packet(
        index: 23,
        direction: "S->C",
        endpoint: "Network.dll SpecialDecode",
        length: 700668,
        decode: new PacketDecodePreview("223", "2", "2", "700668", "internal/28346-inner", "28346 inner 223", [0x00], OuterMsgId: 28346, InnerMsgId: 223, EffectiveMsgId: 223, MsgIdDisplay: "28346 -> 223", WrapperKind: "28346-inner")),
    Packet(
        index: 24,
        direction: "S->C",
        endpoint: "Network.dll Unpack",
        length: 32213,
        decode: new PacketDecodePreview("28346", "0", "8", "32199", "internal/raw-wrapper", "outer 28346", null, OuterMsgId: 28346, EffectiveMsgId: 28346, MsgIdDisplay: "28346", WrapperKind: "28346-wrapper")),
};
var linkedRows = MainViewModel.BuildOnlinePacketRows(linkedPackets, "", "包序列 ↑ 小到大");
AssertSequence("msgid chain display", linkedRows.Select(row => row.MsgId), ["316", "28346 -> 100", "28346 -> 223", "28346"]);
AssertSequence("search 100 finds direct/effective inner", MainViewModel.BuildOnlinePacketRows(linkedPackets, "100", "包序列 ↑ 小到大").Select(row => row.MsgId), ["28346 -> 100"]);
AssertSequence("search 28346 finds wrapper chain only", MainViewModel.BuildOnlinePacketRows(linkedPackets, "28346", "包序列 ↑ 小到大").Select(row => row.MsgId), ["28346 -> 100", "28346 -> 223", "28346"]);
AssertSequence("search 316 stays direct", MainViewModel.BuildOnlinePacketRows(linkedPackets, "316", "包序列 ↑ 小到大").Select(row => row.MsgId), ["316"]);
AssertSequence("search chain exact", MainViewModel.BuildOnlinePacketRows(linkedPackets, "28346->100", "包序列 ↑ 小到大").Select(row => row.MsgId), ["28346 -> 100"]);

var canonical28346Payload = new byte[] { 0x10, 0x20, 0x30 };
var noisy28346Flow = new[]
{
    Packet(
        index: 30,
        direction: "S->C",
        endpoint: "Network.dll InnerDispatch",
        length: 32213,
        decode: new PacketDecodePreview("28346", "-", "-", "32213", "internal/raw-wrapper", "recv inner dispatch wrapper", null, OuterMsgId: 28346, EffectiveMsgId: 28346, MsgIdDisplay: "28346", WrapperKind: "28346-wrapper"),
        summary: "internal_hook_plain recv_inner_dispatch_body msg=28346 len=32213"),
    Packet(
        index: 31,
        direction: "S->C",
        endpoint: "Network.dll SpecialDecode",
        length: 32213,
        decode: new PacketDecodePreview("28346", "-", "-", "32213", "internal/raw-wrapper", "before full wrapper", null, OuterMsgId: 28346, EffectiveMsgId: 28346, MsgIdDisplay: "28346", WrapperKind: "28346-wrapper"),
        summary: "internal_hook_plain recv_special28346_before_full msg=28346 len=32213"),
    Packet(
        index: 32,
        direction: "S->C",
        endpoint: "Network.dll SpecialDecode",
        length: 16113,
        decode: new PacketDecodePreview("100", "-", "2", "16113", "internal/28346-evidence", "pre-decode inner evidence", [0x01], OuterMsgId: 28346, InnerMsgId: 100, EffectiveMsgId: 100, MsgIdDisplay: "28346 -> 100", WrapperKind: "28346-inner-evidence"),
        summary: "internal_hook_plain recv_special28346_before_inner_body_at12 msg=100 len=16113"),
    Packet(
        index: 33,
        direction: "S->C",
        endpoint: "Network.dll SpecialDecode",
        length: 317329,
        decode: new PacketDecodePreview("100", "1", "2", "317329", "internal/28346-inner", "final decoded full payload", canonical28346Payload, OuterMsgId: 28346, InnerMsgId: 100, EffectiveMsgId: 100, MsgIdDisplay: "28346 -> 100", WrapperKind: "28346-inner"),
        summary: "internal_hook_plain recv_special28346_after_full msg=100 len=317329",
        payloadBytes: canonical28346Payload),
    Packet(
        index: 34,
        direction: "S->C",
        endpoint: "Network.dll SpecialDecode",
        length: 30046,
        decode: new PacketDecodePreview("24118", "-", "2", "30046", "internal/28346-evidence", "wrongly sliced after_full evidence", [0x02], OuterMsgId: 28346, InnerMsgId: 24118, EffectiveMsgId: 24118, MsgIdDisplay: "28346 -> 24118", WrapperKind: "28346-inner-evidence"),
        summary: "internal_hook_plain recv_special28346_after_inner_body_at12 msg=24118 len=30046"),
    Packet(
        index: 35,
        direction: "S->C",
        endpoint: "Network.dll Unpack",
        length: 317329,
        decode: new PacketDecodePreview("100", "1", "2", "317329", "internal/plain-recv-full", "duplicate final plaintext", canonical28346Payload),
        summary: "internal_hook_plain recv_plain_full msg=100 len=317329",
        payloadBytes: canonical28346Payload),
    Packet(
        index: 36,
        direction: "S->C",
        endpoint: "Network.dll Unpack",
        length: 12,
        decode: new PacketDecodePreview("316", "1", "9", "12", "internal/plain-recv-full", "direct 316", [0x03]),
        summary: "internal_hook_plain recv_plain_full msg=316 len=12"),
    Packet(
        index: 37,
        direction: "S->C",
        endpoint: "Network.dll InnerDispatch",
        length: 3092,
        decode: new PacketDecodePreview("541", "2", "0", "3092", "internal/plain-recv-full", "real inner dispatch msg", [0x04]),
        summary: "internal_hook_plain recv_inner_dispatch_body msg=541 len=3092")
};
var canonicalRows = MainViewModel.BuildOnlinePacketRows(noisy28346Flow, "", "包序列 ↑ 小到大");
AssertSequence("28346 flow hides evidence rows and duplicate direct full", canonicalRows.Select(row => row.MsgId), ["28346 -> 100", "316", "541"]);
AssertSequence("search 100 returns canonical 28346 inner only", MainViewModel.BuildOnlinePacketRows(noisy28346Flow, "100", "包序列 ↑ 小到大").Select(row => row.MsgId), ["28346 -> 100"]);
AssertSequence("search fake after_inner msg returns nothing", MainViewModel.BuildOnlinePacketRows(noisy28346Flow, "24118", "包序列 ↑ 小到大").Select(row => row.MsgId), []);
AssertSequence("search 28346 returns canonical final inner only", MainViewModel.BuildOnlinePacketRows(noisy28346Flow, "28346", "包序列 ↑ 小到大").Select(row => row.MsgId), ["28346 -> 100"]);

var mixedHeaderlessStages = new[]
{
    Packet(
        index: 65,
        direction: "C->S",
        endpoint: "Network.dll Pack",
        length: 11,
        decode: new PacketDecodePreview("1401", "0", "1030921579", "11", "internal/plain-send-body", "full send body", [0x00]),
        summary: "internal_hook_plain send_plain_body msg=1401 type=0 seq=1030921579 len=11"),
    Packet(
        index: 66,
        direction: "C->S",
        endpoint: "Network.dll 28346 Builder",
        length: 32213,
        decode: new PacketDecodePreview("28346", "wrapper", "1030921579", "32213", "internal/raw-wrapper", "builder wrapper", null, OuterMsgId: 28346, EffectiveMsgId: 28346, MsgIdDisplay: "28346", WrapperKind: "28346-wrapper"),
        summary: "internal_hook_plain send_safe28346_builder_payload_full msg=28346 type=-1 seq=1030921579 len=32213"),
    Packet(
        index: 67,
        direction: "C->S",
        endpoint: "Network.dll 28346 Builder",
        length: 81,
        decode: new PacketDecodePreview("24251", "body", "1030921579", "81", "internal/28346-inner", "builder inner body only", [0x01], OuterMsgId: 28346, InnerMsgId: 24251, EffectiveMsgId: 24251, MsgIdDisplay: "28346 -> 24251", WrapperKind: "28346-inner"),
        summary: "internal_hook_plain send_safe28346_builder_inner_body_at12 msg=24251 type=-1 seq=1030921579 len=81"),
    Packet(
        index: 68,
        direction: "C->S",
        endpoint: "Network.dll SerializeBody",
        length: 96,
        decode: new PacketDecodePreview("28346", "body", "body", "96", "internal/raw-wrapper", "SerializeBody 28346 wrapper body", null, OuterMsgId: 28346, EffectiveMsgId: 28346, MsgIdDisplay: "28346", WrapperKind: "28346-wrapper"),
        summary: "internal_hook_plain send_serialize_body_plain msg=28346 type=-1 seq=-1 len=96"),
    Packet(
        index: 70,
        direction: "S->C",
        endpoint: "Network.dll InnerDispatch",
        length: 706,
        decode: new PacketDecodePreview("24313", "body", "body", "706", "internal/plain-inner-dispatch", "inner dispatch body only", [0x02]),
        summary: "internal_hook_plain recv_inner_dispatch_body msg=24313 type=-1 seq=-1 len=706"),
    Packet(
        index: 71,
        direction: "S->C",
        endpoint: "Network.dll InnerDispatch",
        length: 1634,
        decode: new PacketDecodePreview("24313", "body", "body", "1634", "internal/plain-inner-dispatch", "inner dispatch out full duplicate", [0x03]),
        summary: "internal_hook_plain recv_inner_dispatch_out_full msg=24313 type=-1 seq=-1 len=1634"),
    Packet(
        index: 72,
        direction: "S->C",
        endpoint: "Network.dll Unpack",
        length: 1634,
        decode: new PacketDecodePreview("24313", "2", "-", "1634", "internal/plain-recv-full", "final recv full", [0x04]),
        summary: "internal_hook_plain recv_plain_full msg=24313 type=2 seq=-1 len=1634")
};
var mixedRows = MainViewModel.BuildOnlinePacketRows(mixedHeaderlessStages, "", "包序列 ↑ 小到大");
AssertSequence("main list hides 28346 wrapper/serialize evidence and recv duplicate body", mixedRows.Select(row => row.PacketIndex), ["65", "67", "72"]);
AssertSequence("28346 builder inner stays visible as chain", mixedRows.Select(row => row.MsgId), ["1401", "28346 -> 24251", "24313"]);
AssertEqual("body-only inner row labels missing msgtype", "body", mixedRows[1].MsgType);
AssertEqual("recv full row keeps real msgtype", "2", mixedRows[2].MsgType);

var structureService = new OnlineCaptureService();
var tempPayload99 = Path.Combine(
    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
    "PacketDumpViewer",
    "online_payloads",
    "network_bin_online_99_online_recv_full_msg541_len1.bin");
if (File.Exists(tempPayload99))
{
    File.Delete(tempPayload99);
}

var onlineStructure = structureService.BuildStructureForPacket(Packet(
    index: 99,
    direction: "S->C",
    endpoint: "internal",
    length: 1,
    decode: new PacketDecodePreview("541", "2", "0", "1", "internal/plain-recv-full", "Network.dll Unpack Hook plaintext; file=internal_packet_99_recv_plain_full_msg541_len1.bin", [0x00])));
AssertEqual("online structure hides temp bin path", false, onlineStructure.Text.Contains(".bin", StringComparison.OrdinalIgnoreCase));
AssertEqual("online structure exposes tree/fallback nodes", true, onlineStructure.RootNodes.Count > 0);
AssertEqual("online temp bin is deleted after parse", false, File.Exists(tempPayload99));
AssertEqual("sanitize internal detail removes file marker", "Network.dll Unpack Hook plaintext（内部明文）", OnlineCaptureService.SanitizeInternalDetail("Network.dll Unpack Hook plaintext; file=internal_packet_1.bin"));

var msg100Bytes = new byte[317_329];
var onlineMsg100 = structureService.BuildStructureForPacket(Packet(
    index: 100,
    direction: "S->C",
    endpoint: "internal",
    length: msg100Bytes.Length,
    decode: new PacketDecodePreview("100", "1", "2", msg100Bytes.Length.ToString(), "internal/plain-recv-full", "Network.dll Unpack Hook plaintext; file=internal_packet_100_recv_plain_full_msg100_len317329.bin", msg100Bytes)));
AssertEqual("online msg100 full enters structure parser", true, onlineMsg100.RootNodes.Any(node => node.Name == "CSSafeCResponseLoginPara"));
AssertEqual("online msg100 is not unsupported", false, onlineMsg100.Text.Contains("Not a MsgID 100 full payload.", StringComparison.OrdinalIgnoreCase));
var compactMsg100 = MainViewModel.BuildCompactStructureSummary(
    Packet(
        index: 100,
        direction: "S->C",
        endpoint: "internal",
        length: msg100Bytes.Length,
        decode: new PacketDecodePreview("100", "1", "2", msg100Bytes.Length.ToString(), "internal/plain-recv-full", "Network.dll Unpack Hook plaintext", msg100Bytes)),
    onlineMsg100);
AssertEqual("compact structure summary does not dump parser text", false, compactMsg100.Contains("Network.dll", StringComparison.OrdinalIgnoreCase));
AssertEqual("compact structure summary shows roots", true, compactMsg100.Contains("roots=", StringComparison.OrdinalIgnoreCase));
var onlineMsg100Via28346 = structureService.BuildStructureForPacket(Packet(
    index: 101,
    direction: "S->C",
    endpoint: "internal",
    length: msg100Bytes.Length,
    decode: new PacketDecodePreview("100", "1", "2", msg100Bytes.Length.ToString(), "internal/28346-inner", "Network.dll SpecialDecode full buffer", msg100Bytes, OuterMsgId: 28346, InnerMsgId: 100, EffectiveMsgId: 100, MsgIdDisplay: "28346 -> 100", WrapperKind: "28346-inner")));
AssertEqual("online msg100 via 28346 keeps route title", true, onlineMsg100Via28346.Text.Contains("MsgID 100 via 28346 wrapper", StringComparison.OrdinalIgnoreCase));

var msg100RequestBytes = new byte[1_694];
msg100RequestBytes[0] = 0x06;
msg100RequestBytes[1] = 0x9E;
msg100RequestBytes[0x66] = 0x00;
msg100RequestBytes[0x67] = 0x20;
msg100RequestBytes[0x88] = 0x00;
msg100RequestBytes[0x89] = 0xE0;
msg100RequestBytes[0x16F] = 0x01;
msg100RequestBytes[0x170] = 0x01;
msg100RequestBytes[0x171] = 0xD0;
msg100RequestBytes[0x342] = 0x01;
msg100RequestBytes[0x343] = 0x03;
msg100RequestBytes[0x344] = 0x59;
var onlineMsg100SendBody = structureService.BuildStructureForPacket(Packet(
    index: 5,
    direction: "C->S",
    endpoint: "internal",
    length: msg100RequestBytes.Length,
    decode: new PacketDecodePreview("100", "-", "-", msg100RequestBytes.Length.ToString(), "internal/plain-send-body", "Network.dll SerializeBody Hook plaintext; file=internal_packet_5_send_serialize_body_plain_msg100_len1694.bin", msg100RequestBytes)));
AssertEqual("online msg100 send body gets request parser tree", true, onlineMsg100SendBody.RootNodes.Any(node => node.Name == "CSSafeCRequestLoginPara"));
AssertEqual("online msg100 send body explains serialize body", true, onlineMsg100SendBody.Text.Contains("SerializeBody", StringComparison.OrdinalIgnoreCase));
AssertEqual("online msg100 send body no longer says unsupported full", false, onlineMsg100SendBody.Text.Contains("Not a MsgID 100 full payload.", StringComparison.OrdinalIgnoreCase));

static CapturedPacketPreview Packet(
    int index,
    string direction,
    string endpoint,
    int length,
    PacketDecodePreview decode,
    string? summary = null,
    byte[]? payloadBytes = null) =>
    new(
        PacketIndex: index,
        Timestamp: "00:00:00.000",
        Direction: direction,
        Endpoint: endpoint,
        PayloadLength: length,
        Summary: summary ?? $"test payload={length}",
        HexPreview: "00",
        ConnectionKey: "test",
        PayloadBytes: payloadBytes ?? [0x00],
        Decode: decode);

static void AssertSequence<T>(string name, IEnumerable<T> actual, IReadOnlyList<T> expected)
{
    var actualArray = actual.ToArray();
    if (actualArray.Length != expected.Count || actualArray.Where((value, index) => !EqualityComparer<T>.Default.Equals(value, expected[index])).Any())
    {
        throw new InvalidOperationException($"{name}: expected [{string.Join(", ", expected)}], got [{string.Join(", ", actualArray)}]");
    }
}

static void AssertEqual<T>(string name, T expected, T actual)
{
    if (!EqualityComparer<T>.Default.Equals(expected, actual))
    {
        throw new InvalidOperationException($"{name}: expected [{expected}], got [{actual}]");
    }
}
