using System.Globalization;
using System.IO;
using System.Text;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services.CaptureBackends;

public sealed class InternalHookCaptureBackend : ICaptureBackend
{
    private readonly object _lock = new();
    private readonly HashSet<string> _seenEvents = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, long> _sessionIndexBaselines = new(StringComparer.OrdinalIgnoreCase);
    private readonly Queue<CapturedPacketPreview> _recentPackets = new();
    private bool _isRunning;
    private int _packetCount;
    private int _totalFrames;
    private string _lastPacketSummary = "";
    private string _detailText = "内部Hook后端就绪；等待 key_probe.dll 注入 GameApp 后写出 Network.dll Unpack/Pack 明文；Top-Kart source object 为可选补充。";
    private DateTime _startUtc = DateTime.MinValue;
    private string _processName = "GameApp.exe";

    public string EngineName => "内部Hook明文";

    public CaptureBackendState CurrentState
    {
        get
        {
            if (_isRunning)
            {
                RefreshFromDisk();
            }

            lock (_lock)
            {
                return new CaptureBackendState(
                    EngineName,
                    IsAvailable: true,
                    IsRunning: _isRunning,
                    PacketCount: _packetCount,
                    TotalFrames: _totalFrames,
                    DataFrames: _packetCount,
                    EmptyFrames: 0,
                    StatusText: _isRunning ? "内部Hook：监听明文中" : "内部Hook：未启动",
                    DetailText: _detailText,
                    LastPacketSummary: _lastPacketSummary,
                    RecentPackets: _recentPackets.ToArray());
            }
        }
    }

    public CaptureBackendState Start(CaptureRequest request)
    {
        lock (_lock)
        {
            _processName = string.IsNullOrWhiteSpace(request.ProcessName) ? "GameApp.exe" : request.ProcessName.Trim();
            _isRunning = true;
            _packetCount = 0;
            _totalFrames = 0;
            _lastPacketSummary = "";
            _seenEvents.Clear();
            _recentPackets.Clear();
            _sessionIndexBaselines.Clear();
            _startUtc = DateTime.UtcNow;
            CaptureExistingSessionBaselines();
            _detailText = $"内部Hook已启动：等待 {_processName} 注入 key_probe.dll 后，从 Network.dll Unpack/Pack 直接读取接收/发送明文；Top-Kart.dll RequestGameServer source object 可选补充；不走外部抓包驱动。";
        }

        RefreshFromDisk();
        return CurrentState;
    }

    public CaptureBackendState Stop()
    {
        lock (_lock)
        {
            _isRunning = false;
            _detailText = string.IsNullOrWhiteSpace(_detailText) ? "内部Hook已停止。" : _detailText;
        }

        return CurrentState with { StatusText = "内部Hook：已停止" };
    }

    public CaptureBackendState Clear()
    {
        lock (_lock)
        {
            _packetCount = 0;
            _totalFrames = 0;
            _lastPacketSummary = "";
            _seenEvents.Clear();
            _recentPackets.Clear();
            if (_isRunning)
            {
                _sessionIndexBaselines.Clear();
                CaptureExistingSessionBaselines();
            }

            _detailText = "内部Hook预览已清空；不会删除 GameApp 内部Hook写出的本地明文文件。";
        }

        return CurrentState;
    }

    public void Dispose()
    {
        Stop();
    }

    private void RefreshFromDisk()
    {
        var root = GetInternalHookRoot();
        if (!Directory.Exists(root))
        {
            lock (_lock)
            {
                _detailText = $"内部Hook目录不存在：{root}；先启动/注入 GameApp。";
            }

            return;
        }

        var sessionDirs = Directory.EnumerateDirectories(root, "session_*")
            .Select(path => new DirectoryInfo(path))
            .Where(dir => dir.Exists)
            .Where(ShouldPollSession)
            .OrderBy(dir => dir.LastWriteTimeUtc < _startUtc ? 1 : 0)
            .ThenByDescending(dir => dir.LastWriteTimeUtc)
            .Take(8)
            .ToArray();

        if (sessionDirs.Length == 0)
        {
            lock (_lock)
            {
                _detailText = $"内部Hook未发现 session 目录：{root}；等待 key_probe.dll 注入 GameApp。";
            }

            return;
        }

        var imported = 0;
        foreach (var session in sessionDirs.OrderBy(dir => dir.LastWriteTimeUtc))
        {
            imported += ImportSession(session.FullName);
        }

        lock (_lock)
        {
            var sessionList = string.Join("; ", sessionDirs.Select(dir => $"{dir.Name} {dir.LastWriteTime:HH:mm:ss}"));
            _detailText = $"内部Hook目录={root}; sessions={sessionList}; 本轮新增={imported}; 说明：数据来自 Network.dll Unpack/Pack 明文；如存在 topkart_source_object，则为 Top-Kart 语义源对象预览。";
        }
    }

    private int ImportSession(string sessionDirectory)
    {
        var indexPath = Path.Combine(sessionDirectory, "packets.tsv");
        if (!File.Exists(indexPath))
        {
            return 0;
        }

        var startOffset = GetSessionIndexBaseline(sessionDirectory);
        var indexLength = new FileInfo(indexPath).Length;
        if (startOffset > indexLength)
        {
            startOffset = 0;
        }

        var imported = 0;
        foreach (var line in ReadSharedLines(indexPath, startOffset))
        {
            if (string.IsNullOrWhiteSpace(line))
            {
                continue;
            }

            var parts = line.Split('\t');
            if (parts.Length < 9)
            {
                continue;
            }

            var eventKey = $"{indexPath}|{parts[0]}|{parts[8]}";
            lock (_lock)
            {
                if (!_seenEvents.Add(eventKey))
                {
                    continue;
                }
            }

            if (!TryParseEvent(sessionDirectory, parts, out var packet))
            {
                continue;
            }

            AddRecentPacket(packet);
            imported++;
        }

        SetSessionIndexBaseline(sessionDirectory, new FileInfo(indexPath).Length);
        return imported;
    }

    private void CaptureExistingSessionBaselines()
    {
        var root = GetInternalHookRoot();
        if (!Directory.Exists(root))
        {
            return;
        }

        foreach (var sessionDirectory in Directory.EnumerateDirectories(root, "session_*"))
        {
            var indexPath = Path.Combine(sessionDirectory, "packets.tsv");
            var length = File.Exists(indexPath) ? new FileInfo(indexPath).Length : 0;
            _sessionIndexBaselines[sessionDirectory] = length;
        }
    }

    private bool ShouldPollSession(DirectoryInfo sessionDirectory)
    {
        lock (_lock)
        {
            return sessionDirectory.LastWriteTimeUtc >= _startUtc ||
                   _sessionIndexBaselines.ContainsKey(sessionDirectory.FullName);
        }
    }

    private long GetSessionIndexBaseline(string sessionDirectory)
    {
        lock (_lock)
        {
            return _sessionIndexBaselines.TryGetValue(sessionDirectory, out var offset) ? offset : 0;
        }
    }

    private void SetSessionIndexBaseline(string sessionDirectory, long offset)
    {
        lock (_lock)
        {
            _sessionIndexBaselines[sessionDirectory] = offset;
        }
    }

    private bool TryParseEvent(string sessionDirectory, string[] parts, out CapturedPacketPreview packet)
    {
        packet = default!;
        if (!int.TryParse(parts[3], NumberStyles.Integer, CultureInfo.InvariantCulture, out var msgId) ||
            !int.TryParse(parts[4], NumberStyles.Integer, CultureInfo.InvariantCulture, out var msgType) ||
            !int.TryParse(parts[5], NumberStyles.Integer, CultureInfo.InvariantCulture, out var msgSeq) ||
            !int.TryParse(parts[6], NumberStyles.Integer, CultureInfo.InvariantCulture, out var bodyLen))
        {
            return false;
        }

        var payloadFileName = parts[8];
        var payloadPath = Path.Combine(sessionDirectory, "payloads", payloadFileName);
        if (!File.Exists(payloadPath))
        {
            return false;
        }

        byte[] payloadBytes;
        try
        {
            payloadBytes = File.ReadAllBytes(payloadPath);
        }
        catch
        {
            return false;
        }

        int packetIndex;
        lock (_lock)
        {
            packetIndex = ++_packetCount;
            _totalFrames = _packetCount;
        }

        var stage = parts[2];
        var isTopKartStage = stage.Contains("topkart_source_object", StringComparison.OrdinalIgnoreCase);
        var isSafe28346BuilderStage = stage.Contains("safe28346_builder", StringComparison.OrdinalIgnoreCase);
        var isSerialize28346Stage = stage.Contains("serialize_body", StringComparison.OrdinalIgnoreCase);
        var isSpecial28346Stage = stage.Contains("special28346", StringComparison.OrdinalIgnoreCase);
        var isUnpack28346Stage = stage.Contains("recv_28346_plain_full", StringComparison.OrdinalIgnoreCase);
        var isInnerDispatchStage = stage.Contains("inner_dispatch", StringComparison.OrdinalIgnoreCase);
        var is28346InnerBodyStage = stage.Contains("inner_body_at12", StringComparison.OrdinalIgnoreCase);
        var isSendStage = stage.StartsWith("send_", StringComparison.OrdinalIgnoreCase) ||
                          stage.Contains("pack", StringComparison.OrdinalIgnoreCase);
        var isRecvStage = stage.StartsWith("recv_", StringComparison.OrdinalIgnoreCase) ||
                          stage.Contains("unpack", StringComparison.OrdinalIgnoreCase);
        var direction = isSendStage ? "C->S" : isRecvStage ? "S->C" : "-";
        var source = isTopKartStage
            ? "内部Hook / Top-Kart.dll RequestGameServer"
            : isSafe28346BuilderStage ? "内部Hook / Network.dll 28346 Builder"
            : isSerialize28346Stage ? "内部Hook / Network.dll SerializeBody"
            : isSpecial28346Stage ? "内部Hook / Network.dll 28346 SpecialDecode"
            : isInnerDispatchStage ? "内部Hook / Network.dll InnerDispatch"
            : isSendStage ? "内部Hook / Network.dll Pack" : "内部Hook / Network.dll Unpack";
        var plainKind = isTopKartStage
            ? "internal/topkart-source-object"
            : isSafe28346BuilderStage ? (is28346InnerBodyStage ? "internal/28346-inner" : "internal/28346-wrapper")
            : isSerialize28346Stage ? (is28346InnerBodyStage ? "internal/28346-inner" : "internal/plain-send-body")
            : isSpecial28346Stage ? (is28346InnerBodyStage ? "internal/28346-inner" : "internal/plain-recv-full")
            : isInnerDispatchStage ? "internal/plain-inner-dispatch"
            : isSendStage ? "internal/plain-send-body" : "internal/plain-recv-full";
        var msgChain = BuildMsgChain(stage, msgId, is28346InnerBodyStage, isSpecial28346Stage, isSafe28346BuilderStage, isSerialize28346Stage, isUnpack28346Stage);
        var isRaw28346Wrapper =
            stage.Contains("raw_28346_packet", StringComparison.OrdinalIgnoreCase) ||
            msgChain.WrapperKind.Equals("28346-wrapper", StringComparison.OrdinalIgnoreCase);
        var isHeaderlessBodyStage = IsHeaderlessBodyStage(stage, is28346InnerBodyStage);
        var missingHeaderLabel = isRaw28346Wrapper ? "wrapper" : isHeaderlessBodyStage ? "body" : "-";
        var displayMsgType = msgType < 0 ? missingHeaderLabel : msgType.ToString(CultureInfo.InvariantCulture);
        var displayMsgSeq = msgSeq < 0 ? (isHeaderlessBodyStage ? "body" : "-") : msgSeq.ToString(CultureInfo.InvariantCulture);
        var displayBodyLen = isTopKartStage ? "source-preview" : bodyLen.ToString(CultureInfo.InvariantCulture);
        var plainDetail = isTopKartStage
            ? $"Top-Kart.dll RequestGameServer source object preview; not wire body; file={payloadFileName}"
            : isSafe28346BuilderStage && is28346InnerBodyStage
                ? $"Network.dll 28346 Builder inner body at +0x0C; true inner MsgID={msgId}; file={payloadFileName}"
            : isSafe28346BuilderStage
                ? $"Network.dll 28346 Builder full safe payload; outer shell MsgID=28346; file={payloadFileName}"
            : isSerialize28346Stage && is28346InnerBodyStage
                ? $"Network.dll SerializeBody 28346 inner body at +0x0C; true inner MsgID={msgId}; file={payloadFileName}"
            : isSpecial28346Stage && is28346InnerBodyStage
                ? $"Network.dll SpecialDecode inner body at +0x0C; true inner MsgID={msgId}; file={payloadFileName}"
            : isSpecial28346Stage
                ? $"Network.dll SpecialDecode full buffer; may be 28346 shell or rewritten inner full; file={payloadFileName}"
            : isUnpack28346Stage
                ? $"Network.dll Unpack Hook plaintext from outer MsgID=28346; true inner MsgID={msgId}; file={payloadFileName}"
            : isInnerDispatchStage
                ? $"Network.dll InnerDispatch plaintext body; true inner MsgID={msgId}; file={payloadFileName}"
            : isSendStage
                ? $"Network.dll Pack Hook plaintext body; file={payloadFileName}"
                : $"Network.dll Unpack Hook plaintext; file={payloadFileName}";
        if (isHeaderlessBodyStage && (msgType < 0 || msgSeq < 0))
        {
            plainDetail += "; hook stage is body-only: CSHEAD/MSGHEAD is not present here, so msgtype/MsgSeq are unavailable";
        }

        var summary = $"internal_hook_plain {parts[2]} msg={msgId} type={msgType} seq={msgSeq} len={payloadBytes.Length} file={payloadFileName}";
        var decodeStatus = isRaw28346Wrapper
            ? "internal/raw-wrapper"
            : msgChain.WrapperKind.Equals("28346-inner", StringComparison.OrdinalIgnoreCase)
                ? "internal/28346-inner"
                : plainKind;
        var decode = new PacketDecodePreview(
            msgId.ToString(CultureInfo.InvariantCulture),
            displayMsgType,
            displayMsgSeq,
            displayBodyLen,
            decodeStatus,
            isRaw28346Wrapper
                ? $"28346 outer wrapper/shell, not the final structure; choose *_inner_body_at12 or InnerDispatch row for real inner MsgID; file={payloadFileName}"
                : plainDetail,
            isRaw28346Wrapper ? null : payloadBytes,
            msgChain.OuterMsgId,
            msgChain.InnerMsgId,
            msgChain.EffectiveMsgId,
            msgChain.Display,
            msgChain.WrapperKind);

        packet = new CapturedPacketPreview(
            packetIndex,
            DateTime.Now.ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture),
            direction,
            source,
            payloadBytes.Length,
            summary,
            ToHexPreview(payloadBytes),
            "internal-hook",
            payloadBytes,
            decode);
        return true;
    }

    private static bool IsHeaderlessBodyStage(string stage, bool is28346InnerBodyStage)
    {
        return is28346InnerBodyStage ||
               stage.Equals("send_serialize_body_plain", StringComparison.OrdinalIgnoreCase) ||
               stage.Equals("recv_inner_dispatch_body", StringComparison.OrdinalIgnoreCase) ||
               stage.Equals("recv_inner_dispatch_out_full", StringComparison.OrdinalIgnoreCase);
    }

    private static MsgChain BuildMsgChain(
        string stage,
        int msgId,
        bool is28346InnerBodyStage,
        bool isSpecial28346Stage,
        bool isSafe28346BuilderStage,
        bool isSerialize28346Stage,
        bool isUnpack28346Stage)
    {
        var isRawWrapperStage = stage.Contains("raw_28346_packet", StringComparison.OrdinalIgnoreCase);
        var isExplicit28346Inner =
            is28346InnerBodyStage ||
            isUnpack28346Stage ||
            (isSpecial28346Stage && stage.Contains("after", StringComparison.OrdinalIgnoreCase) && msgId != 28346);
        var isExplicit28346Wrapper =
            isRawWrapperStage ||
            (msgId == 28346 &&
             !isExplicit28346Inner &&
             (isSafe28346BuilderStage || isSerialize28346Stage || isSpecial28346Stage || stage.Contains("28346", StringComparison.OrdinalIgnoreCase)));

        if (isExplicit28346Inner && msgId > 0)
        {
            return new MsgChain(
                OuterMsgId: 28346,
                InnerMsgId: msgId,
                EffectiveMsgId: msgId,
                Display: $"28346 -> {msgId.ToString(CultureInfo.InvariantCulture)}",
                WrapperKind: "28346-inner");
        }

        if (isExplicit28346Wrapper)
        {
            return new MsgChain(
                OuterMsgId: 28346,
                InnerMsgId: null,
                EffectiveMsgId: 28346,
                Display: "28346",
                WrapperKind: "28346-wrapper");
        }

        return new MsgChain(
            OuterMsgId: null,
            InnerMsgId: null,
            EffectiveMsgId: msgId,
            Display: msgId.ToString(CultureInfo.InvariantCulture),
            WrapperKind: "");
    }

    private void AddRecentPacket(CapturedPacketPreview packet)
    {
        lock (_lock)
        {
            _recentPackets.Enqueue(packet);

            _lastPacketSummary = packet.Summary;
        }
    }

    private static IEnumerable<string> ReadSharedLines(string path, long startOffset)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        if (startOffset > 0)
        {
            stream.Seek(Math.Min(startOffset, stream.Length), SeekOrigin.Begin);
        }

        using var reader = new StreamReader(stream, Encoding.ASCII, detectEncodingFromByteOrderMarks: false);
        while (!reader.EndOfStream)
        {
            yield return reader.ReadLine() ?? "";
        }
    }

    private static string GetInternalHookRoot() =>
        Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "PacketDumpViewer",
            "internal_hook");

    private static string ToHexPreview(byte[] payload)
    {
        if (payload.Length == 0)
        {
            return "";
        }

        return string.Join(" ", payload.Take(64).Select(value => value.ToString("X2", CultureInfo.InvariantCulture)));
    }

    private sealed record MsgChain(
        int? OuterMsgId,
        int? InnerMsgId,
        int EffectiveMsgId,
        string Display,
        string WrapperKind);
}
