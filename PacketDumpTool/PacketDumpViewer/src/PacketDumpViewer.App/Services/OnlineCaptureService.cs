using System.Text;
using System.IO;
using System.Net.NetworkInformation;
using PacketDumpViewer.App.Models;
using PacketDumpViewer.App.Services.CaptureBackends;

namespace PacketDumpViewer.App.Services;

public sealed class OnlineCaptureService
{
    private readonly NativeModuleExtractor _nativeModuleExtractor = new();
    private readonly KeyProbeManager _keyProbeManager;
    private readonly OnlineQqSpeedStreamDecoder _streamDecoder = new();
    private readonly ParserBridge _parserBridge = new();
    private readonly Dictionary<string, ICaptureBackend> _captureBackends;
    private ICaptureBackend? _activeBackend;
    private bool _isStarted;

    public OnlineCaptureService()
    {
        _keyProbeManager = new KeyProbeManager(_nativeModuleExtractor);
        var internalHook = new InternalHookCaptureBackend();
        var npcap = new NpcapRawCaptureBackend();
        _captureBackends = new Dictionary<string, ICaptureBackend>(StringComparer.OrdinalIgnoreCase)
        {
            ["内部Hook明文"] = internalHook,
            ["Npcap TUN + 进程过滤（推荐）"] = npcap,
            ["Npcap 原始抓包"] = npcap,
            ["Npcap TUN网卡调试"] = npcap,
            ["自动 / Auto"] = internalHook
        };
    }

    public OnlineCaptureSnapshot Refresh(string processName, string adapterName, string captureEngine, string sessionKeyHex)
    {
        var keyProbe = _keyProbeManager.Refresh(processName);
        var backendState = GetBackendState(captureEngine);
        return BuildSnapshot(
            keyProbe,
            backendState,
            adapterName,
            captureEngine,
            sessionKeyHex,
            BuildCaptureStatus(keyProbe, backendState),
            "刷新在线抓包状态完成。");
    }

    public OnlineCaptureSnapshot Start(string processName, string adapterName, string captureEngine, string sessionKeyHex)
    {
        _isStarted = true;
        captureEngine = NormalizeSupportedCaptureEngine(captureEngine);
        var keyProbe = _keyProbeManager.Start(processName);
        var request = new CaptureRequest(processName, adapterName, captureEngine, "tcp");
        var backend = ResolveBackendForStart(captureEngine);
        _activeBackend = backend;
        var backendState = backend.Start(request);

        var captureStatus = backendState.IsRunning ? "在线抓包：抓包中" : "在线抓包：后端未启动";

        return BuildSnapshot(
            keyProbe,
            backendState,
            adapterName,
            captureEngine,
            sessionKeyHex,
            captureStatus,
            $"在线抓包控制链路已启动；当前引擎={NormalizeCaptureEngine(captureEngine)}；实际后端={backendState.EngineName}；{backendState.StatusText}。");
    }

    public OnlineCaptureSnapshot Stop(string adapterName, string captureEngine, string sessionKeyHex)
    {
        _isStarted = false;
        var keyProbe = _keyProbeManager.Stop();
        var backendState = (_activeBackend ?? ResolveBackend(captureEngine)).Stop();
        return BuildSnapshot(
            keyProbe,
            backendState,
            adapterName,
            captureEngine,
            sessionKeyHex,
            "在线抓包：已停止",
            "已停止在线抓包后端。");
    }

    public OnlineCaptureSnapshot Clear(string processName, string adapterName, string captureEngine, string sessionKeyHex)
    {
        var keyProbe = _keyProbeManager.Refresh(processName);
        var backendState = (_activeBackend ?? ResolveBackend(captureEngine)).Clear();
        return BuildSnapshot(
            keyProbe,
            backendState,
            adapterName,
            captureEngine,
            sessionKeyHex,
            _isStarted ? "在线抓包：已清空预览" : "在线抓包：未启动",
            "已清空在线抓包计数与最近包预览。");
    }

    public OnlineCaptureSnapshot ExportJson(string processName, string adapterName, string captureEngine, string sessionKeyHex)
    {
        var keyProbe = _keyProbeManager.Refresh(processName);
        var backendState = GetBackendState(captureEngine);
        return BuildSnapshot(
            keyProbe,
            backendState,
            adapterName,
            captureEngine,
            sessionKeyHex,
            _isStarted ? "在线抓包：导出预留" : "在线抓包：未启动",
            "导出 JSON 已预留；后续直接导出当前在线包列表与明文结构结果。");
    }

    private OnlineCaptureSnapshot BuildSnapshot(
        KeyProbeSnapshot keyProbe,
        CaptureBackendState backendState,
        string adapterName,
        string captureEngine,
        string sessionKeyHex,
        string captureStatus,
        string statusText)
    {
        var effectiveSessionKeyHex = string.IsNullOrWhiteSpace(sessionKeyHex)
            ? keyProbe.SessionKeyHex ?? ""
            : sessionKeyHex;
        var decodedPackets = _streamDecoder.DecodeSnapshot(backendState.RecentPackets, effectiveSessionKeyHex);
        var captureEngineStatus = BuildCaptureEngineStatus(captureEngine, backendState);
        var backendStatus = backendState.StatusText;
        var packetCountText = $"Packets：{backendState.PacketCount} / TCP帧：{backendState.TotalFrames} / 空ACK：{backendState.EmptyFrames}";
        var cppBackendStatus = BuildCppBackendStatus(captureEngine, keyProbe, backendState);

        return new OnlineCaptureSnapshot(
            captureStatus,
            captureEngineStatus,
            keyProbe.KeyProbeStatus,
            BuildTopKartHookStatus(keyProbe),
            backendStatus,
            packetCountText,
            statusText,
            BuildHexPreview(adapterName, captureEngine, keyProbe, backendState, decodedPackets, effectiveSessionKeyHex),
            BuildStructurePreview(captureEngine, keyProbe, backendState, decodedPackets),
            backendState.LastPacketSummary,
            cppBackendStatus,
            decodedPackets,
            keyProbe.SessionKeyHex);
    }

    private static string BuildCaptureEngineStatus(string captureEngine, CaptureBackendState backendState)
    {
        var tunSuffix = DetectTunStatus().IsActive ? " / TUN已检测" : "";
        var actual = $" / 实际：{backendState.EngineName}";
        return NormalizeCaptureEngine(captureEngine) switch
        {
            "内部Hook明文" => $"抓包引擎：内部Hook明文（Network Unpack/Pack；不走外部抓包驱动）{actual}",
            "Npcap 原始抓包" or "Npcap TUN网卡调试" or "Npcap TUN + 进程过滤（推荐）" => $"抓包引擎：Npcap TUN + GameApp进程过滤{tunSuffix}{actual}",
            _ => $"抓包引擎：自动（内部Hook明文）{actual}"
        };
    }

    private static string BuildCppBackendStatus(
        string captureEngine,
        KeyProbeSnapshot keyProbe,
        CaptureBackendState backendState)
    {
        var builder = new StringBuilder();
        builder.AppendLine("在线后端：自写 key_probe.dll Hook + PacketDumpViewer.exe C# 解析。");
        builder.AppendLine($"capture engine: {NormalizeCaptureEngine(captureEngine)}");
        builder.AppendLine($"key_probe.dll: {(keyProbe.NativeModuleReady ? "ready" : "missing")}");
        builder.AppendLine($"backend: {backendState.StatusText}; packets={backendState.PacketCount}; tcpFrames={backendState.TotalFrames}; emptyAck={backendState.EmptyFrames}");
        builder.AppendLine($"backend detail: {backendState.DetailText}");
        builder.AppendLine("主路径：内部Hook明文；接收 hook Network.dll Unpack，发送 hook Network.dll Pack；Top-Kart Hook 单独显示为可选语义补充；Npcap 仅作为原始 TCP 备用。");
        return builder.ToString().TrimEnd();
    }

    private static string BuildTopKartHookStatus(KeyProbeSnapshot keyProbe)
    {
        var text = $"{keyProbe.KeyProbeStatus}; {keyProbe.Detail}";
        if (text.Contains("topkart=hook_installed", StringComparison.OrdinalIgnoreCase) ||
            text.Contains("Top-Kart源对象Hook已安装", StringComparison.OrdinalIgnoreCase))
        {
            return "Top-Kart Hook：已安装";
        }

        if (text.Contains("topkart=hook_disabled", StringComparison.OrdinalIgnoreCase) ||
            text.Contains("Top-Kart版本不匹配", StringComparison.OrdinalIgnoreCase))
        {
            return "Top-Kart Hook：版本不匹配已跳过";
        }

        if (text.Contains("topkart=module_seen", StringComparison.OrdinalIgnoreCase) ||
            text.Contains("topkart=module_wait", StringComparison.OrdinalIgnoreCase) ||
            text.Contains("Top-Kart.dll not loaded", StringComparison.OrdinalIgnoreCase))
        {
            return "Top-Kart Hook：等待/检查 Top-Kart.dll";
        }

        if (text.Contains("worker=running", StringComparison.OrdinalIgnoreCase) ||
            text.Contains("worker=found", StringComparison.OrdinalIgnoreCase))
        {
            return "Top-Kart Hook：可选，等待模块";
        }

        return "Top-Kart Hook：未启动";
    }

    private static string BuildHexPreview(
        string adapterName,
        string captureEngine,
        KeyProbeSnapshot keyProbe,
        CaptureBackendState backendState,
        IReadOnlyList<CapturedPacketPreview> decodedPackets,
        string sessionKeyHex)
    {
        var builder = new StringBuilder();
        builder.AppendLine("在线解密 HEX / Phase1 status");
        builder.AppendLine($"engine: {NormalizeCaptureEngine(captureEngine)}");
        var tunStatus = DetectTunStatus();
        builder.AppendLine($"tun: {(tunStatus.IsActive ? "active" : "not detected")} {tunStatus.Summary}");
        builder.AppendLine($"adapter: {NormalizeAdapterName(adapterName)}");
        builder.AppendLine(keyProbe.ProcessStatus);
        builder.AppendLine(keyProbe.KeyProbeStatus);
        builder.AppendLine($"backend: {backendState.StatusText}");
        builder.AppendLine($"packets(data payload): {backendState.PacketCount}");
        builder.AppendLine($"tcp frames: {backendState.TotalFrames}; empty ack/control: {backendState.EmptyFrames}; data frames: {backendState.DataFrames}");
        if (!string.IsNullOrWhiteSpace(backendState.LastPacketSummary))
        {
            builder.AppendLine($"last: {SanitizeInternalSummary(backendState.LastPacketSummary)}");
        }

        if (decodedPackets.Count > 0)
        {
            var latest = decodedPackets[^1];
            builder.AppendLine();
            builder.AppendLine($"latest payload hex preview #{latest.PacketIndex} ({latest.PayloadLength} bytes):");
            builder.AppendLine(string.IsNullOrWhiteSpace(latest.HexPreview) ? "(empty)" : latest.HexPreview);
            if (latest.Decode is not null)
            {
                builder.AppendLine($"latest qqspeed decode: msgid={latest.Decode.DisplayMsgId}; msgtype={latest.Decode.MsgType}; MsgSeq={latest.Decode.MsgSeq}; BodyLen={latest.Decode.BodyLen}; status={latest.Decode.Status}");
                builder.AppendLine($"decode detail: {SanitizeInternalDetail(latest.Decode.Detail)}");
            }
        }

        builder.AppendLine($"detail: {backendState.DetailText}");
        builder.AppendLine();
        builder.AppendLine("Key / 解密流程：");
        builder.AppendLine($"1. 当前 Session Key 输入状态：{BuildSessionKeyInputStatus(sessionKeyHex)}。");
        builder.AppendLine("2. 获取 key 的准确路线：登录响应里下发 16B session key；在线模式由自写 key_probe.dll 从 GameApp/Network 运行态读出并填入这里。");
        builder.AppendLine("3. 当前已实现：手动填入 Session Key Hex 后，mode!=0 TCP frame 会走 C# QQSpeed TEA-CBC 解密，再解析 MsgID/msgtype/MsgSeq/BodyLen。");
        builder.AppendLine("4. Auto 默认使用内部Hook明文：key_probe.dll 注入 GameApp 后 hook Network.dll Unpack/Pack；Top-Kart Hook 状态在顶部右侧单独显示。");
        builder.AppendLine("5. Npcap 仍保留为原始 TCP 备用；走 Npcap 时才需要 TCP stream 重组和 Session Key TEA 解密。");
        builder.AppendLine("6. 内部Hook/Npcap 只是抓包来源，结构体统一交给当前 MsgID parser；不适配的 MsgID/方向后续补 parser。");
        builder.AppendLine();
        builder.AppendLine("调试口径：Session Key 在 Key 面板显示/保存；运行状态日志只显示 ready/missing/verified。");
        return builder.ToString().TrimEnd();
    }

    private static string BuildSessionKeyInputStatus(string sessionKeyHex)
    {
        var hex = new string((sessionKeyHex ?? "").Where(Uri.IsHexDigit).ToArray());
        return hex.Length switch
        {
            0 => "未填写",
            32 => "16 bytes OK，可用于 TEA 解密",
            _ when hex.Length % 2 != 0 => $"hex 长度为奇数({hex.Length})，格式错误",
            _ => $"{hex.Length / 2} bytes，QQSpeed TEA 需要 16 bytes"
        };
    }

    private string BuildStructurePreview(
        string captureEngine,
        KeyProbeSnapshot keyProbe,
        CaptureBackendState backendState,
        IReadOnlyList<CapturedPacketPreview> decodedPackets)
    {
        var builder = new StringBuilder();
        builder.AppendLine("结构体 / Structure");
        var latestPlain = decodedPackets.LastOrDefault(IsStructureCandidate);
        builder.AppendLine(latestPlain is null
            ? "当前还没有可解析在线明文 packet；内部Hook明文会自动进入结构体解析，Npcap 加密包需要 Session Key。"
            : BuildStructureTextForPacket(latestPlain));
        builder.AppendLine();
        builder.AppendLine("Pipeline:");
        builder.AppendLine("PacketDumpViewer.exe");
        builder.AppendLine("  -> OnlineCaptureService");
        builder.AppendLine($"  -> CaptureEngine [{NormalizeCaptureEngine(captureEngine)}]");
        builder.AppendLine($"  -> CaptureBackend [{backendState.EngineName}; {backendState.StatusText}; packets={backendState.PacketCount}; tcpFrames={backendState.TotalFrames}; emptyAck={backendState.EmptyFrames}]");
        builder.AppendLine($"  -> TcpStreamReassembler [decodedRows={decodedPackets.Count}; sessionKey=Key panel]");
        builder.AppendLine($"  -> KeyProbeManager [{keyProbe.KeyProbeStatus}]");
        builder.AppendLine("  -> ParserBridge by MsgID");
        return builder.ToString().TrimEnd();
    }

    public string BuildStructureTextForPacket(CapturedPacketPreview? packet) =>
        BuildStructureForPacket(packet).Text;

    public OnlineStructureResult BuildStructureForPacket(CapturedPacketPreview? packet)
    {
        if (packet is null)
        {
            return new OnlineStructureResult("请先在上方包列表选中一条在线包。", []);
        }

        var builder = new StringBuilder();
        builder.AppendLine($"选中在线包: #{packet.PacketIndex}; direction={packet.Direction}; len={packet.PayloadLength:N0} bytes");

        var decode = packet.Decode;
        if (decode is null)
        {
            builder.AppendLine("decode: waiting；还没有 MsgID/msgtype/MsgSeq/BodyLen。");
            builder.AppendLine("说明：这通常是 TCP stream 未重组完成，或者只是原始 TCP payload。");
            return new OnlineStructureResult(builder.ToString().TrimEnd(), []);
        }

        builder.AppendLine($"MsgID={decode.DisplayMsgId}; MsgType={decode.MsgType}; MsgSeq={decode.MsgSeq}; BodyLen={decode.BodyLen}; Status={decode.Status}");
        if (!string.Equals(decode.DisplayMsgId, decode.MsgId, StringComparison.OrdinalIgnoreCase) ||
            decode.OuterMsgId is not null ||
            decode.InnerMsgId is not null)
        {
            builder.AppendLine($"EffectiveMsgID={decode.EffectiveMsgIdValue?.ToString() ?? decode.MsgId}; OuterMsgID={decode.OuterMsgId?.ToString() ?? "-"}; InnerMsgID={decode.InnerMsgId?.ToString() ?? "-"}; Wrapper={decode.WrapperKind}");
        }

        if (!string.IsNullOrWhiteSpace(decode.Detail))
        {
            builder.AppendLine(SanitizeInternalDetail(decode.Detail));
        }

        if (IsRawWrapperPacket(packet, decode))
        {
            builder.AppendLine();
            builder.AppendLine("这是 28346 外层容器/外壳包，不是 inner 明文业务结构体。");
            builder.AppendLine("正确查看方式：选择同 seq/相邻出现的 28346 -> MsgID 内层包，结构体会按右侧真实 MsgID 解析。");
            builder.AppendLine("raw preview:");
            builder.AppendLine(string.IsNullOrWhiteSpace(packet.HexPreview) ? "(empty)" : packet.HexPreview);
            return new OnlineStructureResult(builder.ToString().TrimEnd(), []);
        }

        if (decode.PlainPayloadBytes is not { Length: > 0 } plainPayload)
        {
            builder.AppendLine();
            builder.AppendLine("当前包没有可交给 ParserBridge 的 full 明文 payload。");
            builder.AppendLine("如果是 Npcap 加密包，请先填入 Session Key；如果是空 ACK/控制帧，本身没有结构体。");
            return new OnlineStructureResult(builder.ToString().TrimEnd(), []);
        }

        var effectiveMsgId = decode.EffectiveMsgIdValue;
        if (effectiveMsgId is null)
        {
            builder.AppendLine();
            builder.AppendLine("MsgID 不是数字，不能进入结构体解析。");
            return new OnlineStructureResult(builder.ToString().TrimEnd(), []);
        }

        builder.AppendLine();
        builder.AppendLine("结构体解析结果：");
        builder.AppendLine(decode.OuterMsgId == 28346 && decode.InnerMsgId is not null
            ? $"parser route: MsgID {effectiveMsgId.Value} via 28346 wrapper"
            : $"parser route: MsgID {effectiveMsgId.Value} direct Network plaintext");
        PacketRecord? onlineRecord = null;
        try
        {
            onlineRecord = CreateOnlinePacketRecord(packet, effectiveMsgId.Value, plainPayload);
            var parserStatus = _parserBridge.GetStatus(onlineRecord, compareSiblings: false);
            var rootNodes = parserStatus.RootNodes.ToArray();
            builder.AppendLine($"parser status: {parserStatus.Status}");
            builder.AppendLine($"parser note: {parserStatus.Note}");
            if (rootNodes.Length > 0)
            {
                builder.AppendLine($"结构体树已加载：{rootNodes.Length} 个根节点，见下方可展开 TreeView。");
                builder.AppendLine($"root: {string.Join(", ", rootNodes.Select(node => node.Name))}");
            }
            else
            {
                builder.AppendLine(parserStatus.StructureText);
            }

            return new OnlineStructureResult(builder.ToString().TrimEnd(), rootNodes);
        }
        catch (Exception ex)
        {
            builder.AppendLine($"parser error: {ex.GetType().Name}: {ex.Message}");
            builder.AppendLine("说明：在线明文已拿到，但当前 MsgID/长度/方向不适配已有 dump parser，需要按该 MsgID 补 parser。");
        }
        finally
        {
            DeleteOnlineTempPayload(onlineRecord);
        }

        return new OnlineStructureResult(builder.ToString().TrimEnd(), []);
    }

    public static string SanitizeInternalDetail(string detail)
    {
        if (string.IsNullOrWhiteSpace(detail))
        {
            return "";
        }

        var value = detail;
        foreach (var fileMarker in new[] { "; file=", " file=" })
        {
            var fileIndex = value.IndexOf(fileMarker, StringComparison.OrdinalIgnoreCase);
            if (fileIndex >= 0)
            {
                value = value[..fileIndex];
                break;
            }
        }

        return value.Replace("Network.dll Unpack Hook plaintext", "Network.dll Unpack Hook plaintext（内部明文）", StringComparison.OrdinalIgnoreCase)
            .Replace("Network.dll Pack Hook plaintext", "Network.dll Pack Hook plaintext（内部明文）", StringComparison.OrdinalIgnoreCase)
            .Replace("Top-Kart.dll RequestGameServer source object", "Top-Kart.dll RequestGameServer source object（源对象预览）", StringComparison.OrdinalIgnoreCase);
    }

    private static string SanitizeInternalSummary(string summary)
    {
        if (string.IsNullOrWhiteSpace(summary))
        {
            return "";
        }

        var fileIndex = summary.IndexOf(" file=", StringComparison.OrdinalIgnoreCase);
        return fileIndex >= 0 ? summary[..fileIndex] : summary;
    }

    public static bool IsStructureCandidate(CapturedPacketPreview packet) =>
        packet.Decode is { PlainPayloadBytes.Length: > 0 } decode &&
        !IsRawWrapperPacket(packet, decode) &&
        decode.EffectiveMsgIdValue is not null;

    public static bool IsRawWrapperPacket(CapturedPacketPreview packet, PacketDecodePreview decode) =>
        decode.Status.Contains("raw-wrapper", StringComparison.OrdinalIgnoreCase) ||
        decode.WrapperKind.Equals("28346-wrapper", StringComparison.OrdinalIgnoreCase) ||
        packet.Summary.Contains("recv_raw_28346_packet", StringComparison.OrdinalIgnoreCase);

    private static PacketRecord CreateOnlinePacketRecord(CapturedPacketPreview packet, int msgId, byte[] plainPayload)
    {
        var directory = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "PacketDumpViewer",
            "online_payloads");
        Directory.CreateDirectory(directory);

        var stage = ExtractInternalStage(packet);
        var direction = packet.Summary.Contains("topkart_source_object", StringComparison.OrdinalIgnoreCase) ||
                        packet.Decode?.Status.Contains("topkart-source-object", StringComparison.OrdinalIgnoreCase) == true
            ? "online_send_topkart_source_object"
            : !string.IsNullOrWhiteSpace(stage)
                ? stage
            : packet.Direction.Equals("C->S", StringComparison.OrdinalIgnoreCase)
                ? "online_send_full"
                : packet.Direction.Equals("S->C", StringComparison.OrdinalIgnoreCase)
                    ? "online_recv_full"
                    : "online_unknown_full";
        var fileName = $"network_bin_online_{packet.PacketIndex}_{direction}_msg{msgId}_len{plainPayload.Length}.bin";
        var fullPath = Path.Combine(directory, fileName);
        File.WriteAllBytes(fullPath, plainPayload);

        return new PacketRecord
        {
            FullPath = fullPath,
            RelativePath = fileName,
            FileName = fileName,
            Direction = direction,
            Seq = packet.PacketIndex,
            MsgId = msgId,
            DeclaredLength = plainPayload.Length,
            FileSize = plainPayload.Length,
            IsUnknown = false
        };
    }

    private static string ExtractInternalStage(CapturedPacketPreview packet)
    {
        const string marker = "internal_hook_plain ";
        var summary = packet.Summary ?? "";
        var markerIndex = summary.IndexOf(marker, StringComparison.OrdinalIgnoreCase);
        if (markerIndex < 0)
        {
            return "";
        }

        var start = markerIndex + marker.Length;
        var end = summary.IndexOf(" msg=", start, StringComparison.OrdinalIgnoreCase);
        if (end <= start)
        {
            return "";
        }

        return summary[start..end].Trim();
    }

    private static void DeleteOnlineTempPayload(PacketRecord? record)
    {
        if (record is null || string.IsNullOrWhiteSpace(record.FullPath))
        {
            return;
        }

        try
        {
            var directory = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "PacketDumpViewer",
                "online_payloads");
            var fullPath = Path.GetFullPath(record.FullPath);
            var rootPath = Path.GetFullPath(directory);
            if (fullPath.StartsWith(rootPath, StringComparison.OrdinalIgnoreCase) && File.Exists(fullPath))
            {
                File.Delete(fullPath);
            }
        }
        catch
        {
            // Best-effort cleanup only; parser result has already been materialized.
        }
    }

    private CaptureBackendState GetBackendState(string captureEngine)
    {
        return (_activeBackend ?? ResolveBackend(captureEngine)).CurrentState;
    }

    private ICaptureBackend ResolveBackendForStart(string captureEngine)
    {
        captureEngine = NormalizeSupportedCaptureEngine(captureEngine);
        if (!IsAutoCaptureEngine(captureEngine))
        {
            return ResolveBackend(captureEngine);
        }

        return ResolveBackend("内部Hook明文");
    }

    private ICaptureBackend ResolveBackend(string captureEngine)
    {
        captureEngine = NormalizeSupportedCaptureEngine(captureEngine);
        var normalized = NormalizeCaptureEngine(captureEngine);
        if (_captureBackends.TryGetValue(normalized, out var backend))
        {
            return backend;
        }

        return _captureBackends["自动 / Auto"];
    }

    private static string NormalizeAdapterName(string adapterName)
    {
        return string.IsNullOrWhiteSpace(adapterName) ? "自动选择 / Auto" : adapterName.Trim();
    }

    private static string NormalizeCaptureEngine(string captureEngine)
    {
        var trimmed = captureEngine.Trim();
        return string.IsNullOrWhiteSpace(trimmed) ? "自动 / Auto" : trimmed;
    }

    private static bool IsAutoCaptureEngine(string captureEngine)
    {
        return NormalizeCaptureEngine(captureEngine).Equals("自动 / Auto", StringComparison.OrdinalIgnoreCase);
    }

    private static string NormalizeSupportedCaptureEngine(string captureEngine)
    {
        var normalized = NormalizeCaptureEngine(captureEngine);
        if (normalized.Contains("内部", StringComparison.OrdinalIgnoreCase) ||
            normalized.Contains("Hook", StringComparison.OrdinalIgnoreCase))
        {
            return "内部Hook明文";
        }

        return normalized;
    }

    private static string BuildCaptureStatus(KeyProbeSnapshot keyProbe, CaptureBackendState backendState)
    {
        if (backendState.IsRunning)
        {
            return "在线抓包：抓包中";
        }

        if (!backendState.IsAvailable)
        {
            return "在线抓包：后端不可用";
        }

        return backendState.TotalFrames > 0 || backendState.PacketCount > 0
            ? "在线抓包：已停止"
            : $"在线抓包：未启动（请点“启动在线抓包”）；{keyProbe.ProcessStatus}";
    }

    private static TunDetectionStatus DetectTunStatus()
    {
        try
        {
            var candidates = NetworkInterface.GetAllNetworkInterfaces()
                .Where(item => item.OperationalStatus == OperationalStatus.Up)
                .Select(item => $"{item.Name} {item.Description}".Trim())
                .Where(text => ContainsAny(text, "mihomo", "meta tunnel", "wintun", "wireguard", "clash", "sing-box", "tailscale", "tun", "tunnel"))
                .Take(4)
                .ToArray();

            return candidates.Length == 0
                ? new TunDetectionStatus(false, "")
                : new TunDetectionStatus(true, string.Join(" | ", candidates));
        }
        catch (Exception ex)
        {
            return new TunDetectionStatus(false, $"detect failed: {ex.GetType().Name}");
        }
    }

    private static bool ContainsAny(string text, params string[] needles)
    {
        return needles.Any(needle => text.Contains(needle, StringComparison.OrdinalIgnoreCase));
    }

    private sealed record TunDetectionStatus(bool IsActive, string Summary);
}
