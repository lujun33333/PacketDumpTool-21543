namespace PacketDumpViewer.App.Models;

public sealed record CapturedPacketPreview(
    int PacketIndex,
    string Timestamp,
    string Direction,
    string Endpoint,
    int PayloadLength,
    string Summary,
    string HexPreview,
    string ConnectionKey,
    byte[] PayloadBytes,
    PacketDecodePreview? Decode = null);

public sealed record PacketDecodePreview(
    string MsgId,
    string MsgType,
    string MsgSeq,
    string BodyLen,
    string Status,
    string Detail,
    byte[]? PlainPayloadBytes = null,
    int? OuterMsgId = null,
    int? InnerMsgId = null,
    int? EffectiveMsgId = null,
    string? MsgIdDisplay = null,
    string WrapperKind = "")
{
    public static PacketDecodePreview Waiting(string detail) => new("-", "-", "-", "-", "waiting", detail);

    public string DisplayMsgId =>
        string.IsNullOrWhiteSpace(MsgIdDisplay) ? MsgId : MsgIdDisplay;

    public int? EffectiveMsgIdValue
    {
        get
        {
            if (EffectiveMsgId is not null)
            {
                return EffectiveMsgId;
            }

            return int.TryParse(MsgId, out var msgId) ? msgId : null;
        }
    }
}

public sealed record CaptureRequest(
    string ProcessName,
    string AdapterName,
    string CaptureEngine,
    string Filter);

public sealed record CaptureBackendState(
    string EngineName,
    bool IsAvailable,
    bool IsRunning,
    int PacketCount,
    int TotalFrames,
    int DataFrames,
    int EmptyFrames,
    string StatusText,
    string DetailText,
    string LastPacketSummary,
    IReadOnlyList<CapturedPacketPreview> RecentPackets)
{
    public static CaptureBackendState NotStarted(string engineName, string detail = "") =>
        new(engineName, true, false, 0, 0, 0, 0, $"{engineName}：未启动", detail, "", []);

    public static CaptureBackendState Unavailable(string engineName, string detail) =>
        new(engineName, false, false, 0, 0, 0, 0, $"{engineName}：不可用", detail, "", []);
}
