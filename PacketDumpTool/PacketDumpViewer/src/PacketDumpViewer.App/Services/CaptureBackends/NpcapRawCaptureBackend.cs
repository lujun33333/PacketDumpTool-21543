using System.Net.NetworkInformation;
using System.Text;
using PacketDotNet;
using PacketDumpViewer.App.Models;
using SharpPcap;

namespace PacketDumpViewer.App.Services.CaptureBackends;

public sealed class NpcapRawCaptureBackend : ICaptureBackend
{
    private ICaptureDevice? _device;
    private int _packetCount;
    private int _tcpFrameCount;
    private int _emptyTcpFrameCount;
    private int _filteredOutDataFrameCount;
    private int _unknownOwnerDataFrameCount;
    private string _lastPacketSummary = "";
    private string _detailText = "Npcap raw backend ready.";
    private bool _isRunning;
    private readonly object _recentPacketsLock = new();
    private readonly Queue<CapturedPacketPreview> _recentPackets = new();
    private readonly TcpOwnerPidLookup _tcpOwnerPidLookup = new();

    public string EngineName => "Npcap 原始抓包";

    public CaptureBackendState CurrentState => new(
        EngineName,
        IsAvailable: true,
        IsRunning: _isRunning,
        PacketCount: _packetCount,
        TotalFrames: _tcpFrameCount,
        DataFrames: _packetCount,
        EmptyFrames: _emptyTcpFrameCount,
        StatusText: _isRunning ? "Npcap：抓包中" : "Npcap：未启动",
        DetailText: BuildLiveDetailText(),
        LastPacketSummary: _lastPacketSummary,
        RecentPackets: SnapshotRecentPackets());

    public CaptureBackendState Start(CaptureRequest request)
    {
        Stop();
        _packetCount = 0;
        _tcpFrameCount = 0;
        _emptyTcpFrameCount = 0;
        _filteredOutDataFrameCount = 0;
        _unknownOwnerDataFrameCount = 0;
        _lastPacketSummary = "";
        ClearRecentPackets();
        _tcpOwnerPidLookup.Configure(request.ProcessName);

        try
        {
            var devices = CaptureDeviceList.Instance;
            if (devices.Count == 0)
            {
                _detailText = "未枚举到 Npcap/WinPcap 设备；请确认 Npcap 已安装并允许当前进程访问。";
                return CaptureBackendState.Unavailable(EngineName, _detailText);
            }

            var selection = SelectDevice(devices, request.AdapterName);
            _device = selection.Device;
            if (_device is null)
            {
                _detailText = $"未匹配到网卡：{request.AdapterName}\r\n{selection.DeviceListText}";
                return CaptureBackendState.Unavailable(EngineName, _detailText);
            }

            _device.OnPacketArrival += OnPacketArrival;
            _device.Open(DeviceModes.Promiscuous, read_timeout: 1000);
            _device.Filter = string.IsNullOrWhiteSpace(request.Filter) ? "tcp" : request.Filter;
            _device.StartCapture();
            _isRunning = true;
            _detailText = $"device={_device.Name}; desc={_device.Description}; filter={_device.Filter}\r\nselected={selection.Reason}\r\nprocessFilter={request.ProcessName}; ownerPid=enabled; 口径：Packets=目标进程或未知归属的 TCP payload；EmptyFrames=空 ACK/控制帧。\r\n{selection.DeviceListText}";
            return CurrentState with { StatusText = "Npcap：抓包中" };
        }
        catch (Exception ex)
        {
            SafeCloseDevice();
            _detailText = $"Npcap start failed: {ex.GetType().Name}: {ex.Message}";
            return CaptureBackendState.Unavailable(EngineName, _detailText);
        }
    }

    public CaptureBackendState Stop()
    {
        SafeCloseDevice();
        _isRunning = false;
        _detailText = string.IsNullOrWhiteSpace(_detailText) ? "Npcap stopped." : _detailText;
        return CurrentState with { StatusText = "Npcap：已停止" };
    }

    public CaptureBackendState Clear()
    {
        _packetCount = 0;
        _tcpFrameCount = 0;
        _emptyTcpFrameCount = 0;
        _filteredOutDataFrameCount = 0;
        _unknownOwnerDataFrameCount = 0;
        _lastPacketSummary = "";
        ClearRecentPackets();
        return CurrentState;
    }

    public void Dispose()
    {
        Stop();
    }

    private void OnPacketArrival(object sender, PacketCapture capture)
    {
        try
        {
            var raw = capture.GetPacket();
            var summary = TryBuildDataPacketSummary(raw);
            Interlocked.Increment(ref _tcpFrameCount);
            if (summary.PayloadLength <= 0)
            {
                Interlocked.Increment(ref _emptyTcpFrameCount);
                return;
            }

            if (summary.Owner.Kind == OwnerMatchKind.OtherProcess)
            {
                Interlocked.Increment(ref _filteredOutDataFrameCount);
                return;
            }

            if (summary.Owner.Kind == OwnerMatchKind.UnknownOwner || summary.Owner.Kind == OwnerMatchKind.NoTargetProcess)
            {
                Interlocked.Increment(ref _unknownOwnerDataFrameCount);
            }

            var packetIndex = Interlocked.Increment(ref _packetCount);
            _lastPacketSummary = $"{summary.Text} {summary.Owner.Note} packetIndex={packetIndex} totalFrames={_tcpFrameCount} emptyFrames={_emptyTcpFrameCount}";
            AddRecentPacket(new CapturedPacketPreview(
                packetIndex,
                raw.Timeval.Date.ToString("HH:mm:ss.fff"),
                summary.Owner.Direction,
                summary.Endpoint,
                summary.PayloadLength,
                _lastPacketSummary,
                ToHexPreview(summary.PayloadData),
                summary.ConnectionKey,
                summary.PayloadData));
        }
        catch (Exception ex)
        {
            _lastPacketSummary = $"packet parse failed: {ex.GetType().Name}";
        }
    }

    private DataPacketSummary TryBuildDataPacketSummary(RawCapture raw)
    {
        var packet = Packet.ParsePacket(raw.LinkLayerType, raw.Data);
        var ip = packet.Extract<IPPacket>();
        var tcp = packet.Extract<TcpPacket>();
        if (ip is null || tcp is null)
        {
            return new DataPacketSummary(0, new TcpOwnerMatch(OwnerMatchKind.UnknownOwner, null, "-", "pid=unknown"), "", "", Array.Empty<byte>(), $"{raw.Timeval.Date:HH:mm:ss.fff} {raw.LinkLayerType} len={raw.Data.Length}");
        }

        var payloadLen = tcp.PayloadData?.Length ?? 0;
        var endpoint = $"{ip.SourceAddress}:{tcp.SourcePort} -> {ip.DestinationAddress}:{tcp.DestinationPort}";
        var owner = payloadLen > 0
            ? _tcpOwnerPidLookup.Match(ip.SourceAddress, tcp.SourcePort, ip.DestinationAddress, tcp.DestinationPort)
            : new TcpOwnerMatch(OwnerMatchKind.UnknownOwner, null, "-", "pid=unknown");
        return new DataPacketSummary(
            payloadLen,
            owner,
            endpoint,
            $"{ip.SourceAddress}:{tcp.SourcePort}",
            tcp.PayloadData ?? Array.Empty<byte>(),
            $"{raw.Timeval.Date:HH:mm:ss.fff} TCP {endpoint} payload={payloadLen}");
    }

    private static NpcapDeviceSelection SelectDevice(CaptureDeviceList devices, string adapterName)
    {
        var normalized = adapterName.Trim();
        var hints = NetworkInterface.GetAllNetworkInterfaces()
            .Select(networkInterface => new NetworkInterfaceHint(
                networkInterface.Name,
                networkInterface.Description,
                networkInterface.OperationalStatus,
                networkInterface.GetIPProperties().UnicastAddresses.Any(address => address.Address.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork),
                networkInterface.GetIPProperties().GatewayAddresses.Any()))
            .ToArray();

        var scored = devices
            .Select(device => ScoreDevice(device, hints))
            .OrderByDescending(item => item.Score)
            .ToArray();

        if (!string.IsNullOrWhiteSpace(normalized) &&
            !normalized.Equals("自动选择 / Auto", StringComparison.OrdinalIgnoreCase))
        {
            var byText = scored.FirstOrDefault(item =>
                ContainsIgnoreCase(item.Device.Name, normalized) ||
                ContainsIgnoreCase(item.Device.Description, normalized));
            if (byText?.Device is not null)
            {
                return new NpcapDeviceSelection(byText.Device, $"manual match '{normalized}', score={byText.Score}", BuildDeviceListText(scored));
            }

            return new NpcapDeviceSelection(null, $"manual match failed '{normalized}'", BuildDeviceListText(scored));
        }

        var selected = scored.FirstOrDefault(item => item.Device is not null && item.Score > -500);
        return selected?.Device is null
            ? new NpcapDeviceSelection(null, "no device", BuildDeviceListText(scored))
            : new NpcapDeviceSelection(
            selected.Device,
            $"auto score={selected.Score}",
            BuildDeviceListText(scored));
    }

    private static ScoredDevice ScoreDevice(ICaptureDevice device, IReadOnlyCollection<NetworkInterfaceHint> hints)
    {
        var text = $"{device.Name} {device.Description}";
        var score = 0;

        if (AnyContains(text, "mihomo", "meta tunnel", "wintun", "wireguard", "clash", "sing-box", "tailscale", "tun ", " tunnel", "tap-windows", "tap adapter"))
        {
            score += 300;
        }

        if (AnyContains(text, "wi-fi", "wifi", "wireless", "wlan", "ethernet", "realtek", "killer"))
        {
            score += 80;
        }

        if (AnyContains(text, "radmin", "vpn"))
        {
            score += 40;
        }

        if (AnyContains(text, "wan miniport", "loopback", "bluetooth", "hyper-v", "wsl", "teredo", "isatap", "pseudo-interface"))
        {
            score -= 1000;
        }

        foreach (var hint in hints)
        {
            if (!ContainsIgnoreCase(text, hint.Name) && !ContainsIgnoreCase(text, hint.Description))
            {
                continue;
            }

            if (hint.Status == OperationalStatus.Up)
            {
                score += 80;
            }

            if (hint.HasIPv4)
            {
                score += 30;
            }

            if (hint.HasGateway)
            {
                score += 30;
            }

            if (AnyContains($"{hint.Name} {hint.Description}", "mihomo", "meta tunnel", "wintun", "tun", "clash"))
            {
                score += 250;
            }
        }

        return new ScoredDevice(device, score);
    }

    private static bool AnyContains(string text, params string[] values)
    {
        return values.Any(value => text.Contains(value, StringComparison.OrdinalIgnoreCase));
    }

    private static string BuildDeviceListText(IReadOnlyList<ScoredDevice> scored)
    {
        var builder = new StringBuilder();
        builder.AppendLine("可用 Npcap 网卡候选：");
        foreach (var item in scored.Take(8))
        {
            builder.AppendLine($"score={item.Score} desc={item.Device.Description} name={item.Device.Name}");
        }

        return builder.ToString().TrimEnd();
    }

    private static bool ContainsIgnoreCase(string? text, string value)
    {
        return !string.IsNullOrWhiteSpace(text) &&
               text.Contains(value, StringComparison.OrdinalIgnoreCase);
    }

    private sealed record NetworkInterfaceHint(string Name, string Description, OperationalStatus Status, bool HasIPv4, bool HasGateway);
    private sealed record ScoredDevice(ICaptureDevice Device, int Score);
    private sealed record NpcapDeviceSelection(ICaptureDevice? Device, string Reason, string DeviceListText);
    private void AddRecentPacket(CapturedPacketPreview packet)
    {
        lock (_recentPacketsLock)
        {
            _recentPackets.Enqueue(packet);
        }
    }

    private CapturedPacketPreview[] SnapshotRecentPackets()
    {
        lock (_recentPacketsLock)
        {
            return _recentPackets.ToArray();
        }
    }

    private void ClearRecentPackets()
    {
        lock (_recentPacketsLock)
        {
            _recentPackets.Clear();
        }
    }

    private static string ToHexPreview(byte[] payload)
    {
        if (payload.Length == 0)
        {
            return "";
        }

        return string.Join(" ", payload.Take(64).Select(value => value.ToString("X2")));
    }

    private string BuildLiveDetailText()
    {
        return $"{_detailText}\r\nownerPidMap={_tcpOwnerPidLookup.LastStatus}\r\nfilteredOtherProcess={_filteredOutDataFrameCount}; unknownOwnerKept={_unknownOwnerDataFrameCount}";
    }

    private sealed record DataPacketSummary(int PayloadLength, TcpOwnerMatch Owner, string Endpoint, string Source, byte[] PayloadData, string Text)
    {
        public string ConnectionKey => string.IsNullOrWhiteSpace(Source) ? Endpoint : Source;
    }

    private void SafeCloseDevice()
    {
        if (_device is null)
        {
            return;
        }

        try
        {
            _device.OnPacketArrival -= OnPacketArrival;
            if (_device.Started)
            {
                _device.StopCapture();
            }

            _device.Close();
        }
        catch
        {
            // Stop/cleanup must not crash UI.
        }
        finally
        {
            _device = null;
            _isRunning = false;
        }
    }
}
