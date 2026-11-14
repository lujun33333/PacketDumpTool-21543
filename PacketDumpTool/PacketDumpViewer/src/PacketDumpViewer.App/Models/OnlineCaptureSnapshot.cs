namespace PacketDumpViewer.App.Models;

public sealed record NativeModuleStatus(
    string ModuleName,
    string? ModulePath,
    bool Exists,
    string Detail);

public sealed record KeyProbeSnapshot(
    string ProcessStatus,
    string KeyProbeStatus,
    string Detail,
    bool ProcessFound,
    bool NativeModuleReady,
    string? NativeModulePath,
    string? SessionKeyHex = null);

public sealed record OnlineCaptureSnapshot(
    string CaptureStatus,
    string CaptureEngineStatus,
    string KeyProbeStatus,
    string TopKartHookStatus,
    string BackendStatus,
    string PacketCountText,
    string StatusText,
    string DecryptedHexText,
    string StructureText,
    string LastPacketSummary,
    string CppBackendStatus,
    IReadOnlyList<CapturedPacketPreview> RecentPackets,
    string? DetectedSessionKeyHex = null);
