using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.MemoryMappedFiles;
using PacketDumpViewer.App.Models;

namespace PacketDumpViewer.App.Services;

public sealed class KeyProbeManager
{
    private const string SharedMappingName = "Local\\PacketDumpViewer_KeyProbe_SessionKey_v2";
    private const uint SharedMagic = 0x4B505651u;
    private const uint SharedVersion = 2u;
    private const long SharedKeyStateSize = 48;
    private const long OffsetMagic = 0;
    private const long OffsetVersion = 4;
    private const long OffsetHasSessionKey = 8;
    private const long OffsetSessionKey = 12;
    private const long OffsetKeySource = 28;
    private const long OffsetWorkerState = 32;
    private const long OffsetScanAttempts = 36;
    private const long OffsetRemotePid = 40;
    private const long OffsetLastError = 44;

    private readonly NativeModuleExtractor _nativeModuleExtractor;

    public KeyProbeManager(NativeModuleExtractor nativeModuleExtractor)
    {
        _nativeModuleExtractor = nativeModuleExtractor;
    }

    public KeyProbeSnapshot Refresh(string processName)
    {
        return Probe(processName, "refresh");
    }

    public KeyProbeSnapshot Start(string processName)
    {
        return Probe(processName, "start");
    }

    public KeyProbeSnapshot Stop()
    {
        return new KeyProbeSnapshot(
            "进程：未检查",
            "KeyProbe：已停止",
            "KeyProbe phase1 stopped; no injected module is tracked by UI.",
            false,
            false,
            null);
    }

    private KeyProbeSnapshot Probe(string processName, string action)
    {
        var normalizedProcessName = NormalizeProcessName(processName);
        var keyProbeModule = _nativeModuleExtractor.PrepareModule(NativeModuleExtractor.KeyProbeModuleName);
        var processes = Process.GetProcessesByName(normalizedProcessName);
        try
        {
            var processFound = processes.Length > 0;
            var processStatus = processFound
                ? $"进程：找到 {processes.Length} 个 {normalizedProcessName}.exe"
                : $"进程：未找到 {normalizedProcessName}.exe";
            var pidPreview = processFound
                ? string.Join(", ", processes.Take(3).Select(process => process.Id.ToString()))
                : "-";

            if (!processFound)
            {
                return new KeyProbeSnapshot(
                    processStatus,
                    "KeyProbe：等待 GameApp.exe",
                    $"{action}: target process not found; native={keyProbeModule.Detail}; pid={pidPreview}",
                    false,
                    keyProbeModule.Exists,
                    keyProbeModule.ModulePath);
            }

            if (!keyProbeModule.Exists)
            {
                return new KeyProbeSnapshot(
                    processStatus,
                    "KeyProbe：等待自写 key_probe.dll",
                    $"{action}: target process found, but self-written key_probe.dll is not packaged yet; pid={pidPreview}; native={keyProbeModule.Detail}",
                    true,
                    false,
                    null);
            }

            var nativeStatus = TryReadSharedStatus(out var sessionKeyHex);
            return new KeyProbeSnapshot(
                processStatus,
                BuildKeyProbeStatus(nativeStatus, sessionKeyHex),
                $"{action}: self-written key_probe.dll ready; pid={pidPreview}; shared_status={nativeStatus}; native={keyProbeModule.Detail}",
                true,
                true,
                keyProbeModule.ModulePath,
                sessionKeyHex);
        }
        finally
        {
            foreach (var process in processes)
            {
                process.Dispose();
            }
        }
    }

    private static string NormalizeProcessName(string processName)
    {
        var trimmed = processName.Trim();
        if (string.IsNullOrWhiteSpace(trimmed))
        {
            return "GameApp";
        }

        return Path.GetFileNameWithoutExtension(trimmed);
    }

    private static string BuildKeyProbeStatus(string nativeStatus, string? sessionKeyHex)
    {
        if (!string.IsNullOrWhiteSpace(sessionKeyHex))
        {
            if (nativeStatus.Contains("network_unpack_hook", StringComparison.OrdinalIgnoreCase))
            {
                return "KeyProbe：已读取 Session Key（Network.dll Unpack Hook）";
            }

            if (nativeStatus.Contains("qqspeed_gamesocket_init_hook", StringComparison.OrdinalIgnoreCase))
            {
                return "KeyProbe：已读取 Session Key（Network.dll CGameSocket::Init Hook）";
            }

            if (nativeStatus.Contains("qqspeed_gamesocket_layout", StringComparison.OrdinalIgnoreCase))
            {
                return "KeyProbe：已读取 Session Key（Network.dll CGameSocket布局）";
            }

            return "KeyProbe：已读取 Session Key（已填入 Key 面板）";
        }

        if (nativeStatus.Contains("worker=running", StringComparison.OrdinalIgnoreCase))
        {
            return $"KeyProbe：已注入，正在扫描 Session Key{BuildDiagHint(nativeStatus)}";
        }

        if (nativeStatus.Contains("worker=found", StringComparison.OrdinalIgnoreCase))
        {
            return "KeyProbe：已注入，但 Session Key 读取异常";
        }

        if (nativeStatus.Contains("worker=not_found", StringComparison.OrdinalIgnoreCase))
        {
            return "KeyProbe：已注入，未找到 Session Key（可能未登录完成/需重新注入）";
        }

        if (nativeStatus.Contains("worker=start_failed", StringComparison.OrdinalIgnoreCase))
        {
            return "KeyProbe：注入后 worker 启动失败";
        }

        if (nativeStatus.Contains("worker=not_started", StringComparison.OrdinalIgnoreCase))
        {
            return "KeyProbe：未确认注入（远程 worker 未启动）";
        }

        return "KeyProbe：自写Hook DLL 就绪（等待注入/运行态读取）";
    }

    private static string BuildDiagHint(string nativeStatus)
    {
        if (nativeStatus.Contains("topkart=hook_installed", StringComparison.OrdinalIgnoreCase))
        {
            return "（Top-Kart源对象Hook已安装）";
        }

        if (nativeStatus.Contains("topkart=hook_disabled", StringComparison.OrdinalIgnoreCase))
        {
            return "（Top-Kart版本不匹配，已跳过）";
        }

        if (nativeStatus.Contains("Network.dll not loaded", StringComparison.OrdinalIgnoreCase))
        {
            return "（Network.dll未加载）";
        }

        if (nativeStatus.Contains("hook install failed", StringComparison.OrdinalIgnoreCase))
        {
            return "（Hook安装失败，看诊断日志）";
        }

        if (nativeStatus.Contains("network_unpack_hook called", StringComparison.OrdinalIgnoreCase))
        {
            return "（Unpack已触发，校验Key中）";
        }

        if (nativeStatus.Contains("gamesocket_init_hook called", StringComparison.OrdinalIgnoreCase))
        {
            return "（GameSocket Init已触发，校验Key中）";
        }

        if (nativeStatus.Contains("network hooks active", StringComparison.OrdinalIgnoreCase) ||
            nativeStatus.Contains("hook installed", StringComparison.OrdinalIgnoreCase))
        {
            return "（Hook已安装，等登录解包触发）";
        }

        return string.Empty;
    }

    private static string TryReadSharedStatus(out string? sessionKeyHex)
    {
        sessionKeyHex = null;
        try
        {
            using var mapping = MemoryMappedFile.OpenExisting(SharedMappingName, MemoryMappedFileRights.Read);
            using var view = mapping.CreateViewAccessor(0, SharedKeyStateSize, MemoryMappedFileAccess.Read);

            var magic = view.ReadUInt32(OffsetMagic);
            var version = view.ReadUInt32(OffsetVersion);
            if (magic != SharedMagic || version != SharedVersion)
            {
                return $"shared_map=invalid; magic=0x{magic:X8}; version={version}; worker=not_started; session_key=missing; source=none";
            }

            var hasSessionKey = view.ReadInt32(OffsetHasSessionKey) != 0;
            var keySource = view.ReadInt32(OffsetKeySource);
            var workerState = view.ReadInt32(OffsetWorkerState);
            var scanAttempts = view.ReadInt32(OffsetScanAttempts);
            var remotePid = view.ReadInt32(OffsetRemotePid);
            var lastError = view.ReadInt32(OffsetLastError);

            if (hasSessionKey)
            {
                var keyBytes = new byte[16];
                view.ReadArray(OffsetSessionKey, keyBytes, 0, keyBytes.Length);
                sessionKeyHex = string.Join(" ", keyBytes.Select(value => value.ToString("X2", CultureInfo.InvariantCulture)));
            }

            var diagTail = ReadDiagTailForPid(remotePid);
            return
                $"shared_map=ready; worker={WorkerStateText(workerState)}; attempts={scanAttempts}; remote_pid={remotePid}; " +
                $"last_error={lastError}; session_key={(hasSessionKey ? "present" : "missing")}; source={KeySourceText(keySource)}; diag_tail={diagTail}";
        }
        catch (FileNotFoundException)
        {
            return "shared_map=missing; worker=not_started; attempts=0; remote_pid=0; session_key=missing; source=none; diag_tail=none";
        }
        catch (IOException ex)
        {
            return $"shared_map=unavailable; worker=not_started; session_key=missing; source=none; error={ex.GetType().Name}: {ex.Message}";
        }
        catch (UnauthorizedAccessException ex)
        {
            return $"shared_map=denied; worker=not_started; session_key=missing; source=none; error={ex.GetType().Name}: {ex.Message}";
        }
        catch (Exception ex)
        {
            return $"shared_map=error; worker=not_started; session_key=missing; source=none; error={ex.GetType().Name}: {ex.Message}";
        }
    }

    private static string WorkerStateText(int workerState) => workerState switch
    {
        1 => "running",
        2 => "found",
        3 => "not_found",
        4 => "start_failed",
        _ => "not_started",
    };

    private static string KeySourceText(int keySource) => keySource switch
    {
        1 => "manual_test",
        2 => "launcher_shared_map",
        3 => "runtime_memory_scan",
        4 => "qqspeed_gamesocket_layout",
        5 => "network_unpack_hook",
        6 => "qqspeed_gamesocket_init_hook",
        _ => "none",
    };

    private static string ReadDiagTailForPid(int remotePid)
    {
        if (remotePid <= 0)
        {
            return "none";
        }

        try
        {
            var path = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "PacketDumpViewer",
                "native",
                $"key_probe_diag_{remotePid.ToString(CultureInfo.InvariantCulture)}.txt");
            if (!File.Exists(path))
            {
                return "none";
            }

            var tail = File.ReadLines(path)
                .Where(line => !string.IsNullOrWhiteSpace(line))
                .LastOrDefault();
            return string.IsNullOrWhiteSpace(tail) ? "none" : tail.Trim();
        }
        catch (Exception ex)
        {
            return $"diag_unavailable:{ex.GetType().Name}";
        }
    }
}
