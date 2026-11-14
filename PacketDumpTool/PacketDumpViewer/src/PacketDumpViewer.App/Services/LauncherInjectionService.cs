using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace PacketDumpViewer.App.Services;

public sealed record LauncherInjectionRequest(
    string LauncherPath,
    string LauncherArguments,
    string WorkingDirectory,
    string TargetProcessName,
    string ModuleName,
    string DllPath,
    string InjectionMode,
    int TimeoutMs,
    int PollMs);

public sealed record LauncherInjectionResult(
    bool Success,
    int? LauncherPid,
    int? TargetPid,
    string Status,
    string Detail);

public sealed class LauncherInjectionService
{
    private const uint ProcessCreateThread = 0x0002;
    private const uint ProcessQueryInformation = 0x0400;
    private const uint ProcessVmOperation = 0x0008;
    private const uint ProcessVmWrite = 0x0020;
    private const uint ProcessVmRead = 0x0010;
    private const uint MemCommit = 0x1000;
    private const uint MemReserve = 0x2000;
    private const uint MemRelease = 0x8000;
    private const uint PageReadWrite = 0x04;
    private const uint WaitObject0 = 0;
    private const uint TokenAdjustPrivileges = 0x0020;
    private const uint TokenQuery = 0x0008;
    private const uint SePrivilegeEnabled = 0x00000002;

    public async Task<LauncherInjectionResult> StartLauncherAndInjectAsync(LauncherInjectionRequest request)
    {
        var detail = new StringBuilder();
        var attachExisting = IsAttachExistingMode(request.InjectionMode);
        if (!attachExisting && !File.Exists(request.LauncherPath))
        {
            return new LauncherInjectionResult(false, null, null, "注入：登陆器不存在", $"launcher not found: {request.LauncherPath}");
        }

        if (!File.Exists(request.DllPath))
        {
            return new LauncherInjectionResult(false, null, null, "注入：key_probe.dll 不存在", $"dll not found: {request.DllPath}");
        }

        var processName = Path.GetFileNameWithoutExtension(request.TargetProcessName);
        var initialPids = SnapshotProcessIdsByName(processName);
        detail.AppendLine($"initial {processName} pids={string.Join(",", initialPids)}");
        detail.AppendLine($"inject dll={request.DllPath}");
        detail.AppendLine($"mode={request.InjectionMode}");
        EnableDebugPrivilege(detail);

        Process? launcherProcess = null;
        if (!attachExisting)
        {
            var startInfo = new ProcessStartInfo
            {
                FileName = request.LauncherPath,
                Arguments = request.LauncherArguments ?? "",
                UseShellExecute = false,
                WorkingDirectory = string.IsNullOrWhiteSpace(request.WorkingDirectory)
                    ? Path.GetDirectoryName(request.LauncherPath) ?? ""
                    : request.WorkingDirectory
            };

            detail.AppendLine($"starting launcher={startInfo.FileName}");
            launcherProcess = Process.Start(startInfo);
            if (launcherProcess is null)
            {
                return new LauncherInjectionResult(false, null, null, "注入：登陆器启动失败", detail.ToString().TrimEnd());
            }

            detail.AppendLine($"launcher pid={launcherProcess.Id}");
        }
        else
        {
            detail.AppendLine("attach existing mode: launcher not started");
        }

        var waitModule = IsWaitModuleMode(request.InjectionMode);
        var attempted = new HashSet<int>();
        var reported = new HashSet<int>();
        var timeoutMs = Math.Max(1000, request.TimeoutMs);
        var pollMs = Math.Clamp(request.PollMs, 20, 5000);
        var sw = Stopwatch.StartNew();

        while (sw.ElapsedMilliseconds <= timeoutMs)
        {
            foreach (var process in Process.GetProcessesByName(processName))
            {
                using (process)
                {
                    if (!attachExisting && initialPids.Contains(process.Id))
                    {
                        continue;
                    }

                    if (attempted.Contains(process.Id))
                    {
                        continue;
                    }

                    if (reported.Add(process.Id))
                    {
                        detail.AppendLine($"target found pid={process.Id} elapsedMs={sw.ElapsedMilliseconds}");
                    }

                    if (waitModule && !HasModuleLoaded(process, request.ModuleName))
                    {
                        continue;
                    }

                    if (waitModule)
                    {
                        detail.AppendLine($"module {request.ModuleName} loaded pid={process.Id} elapsedMs={sw.ElapsedMilliseconds}");
                    }
                    else
                    {
                        detail.AppendLine($"inject-on-process-found pid={process.Id} elapsedMs={sw.ElapsedMilliseconds}");
                    }

                    attempted.Add(process.Id);
                    var injectDetail = InjectDll(process.Id, request.DllPath);
                    detail.AppendLine(injectDetail.Detail);
                    if (injectDetail.Success)
                    {
                        return new LauncherInjectionResult(
                            true,
                            launcherProcess?.Id,
                            process.Id,
                            $"注入：成功 pid={process.Id}",
                            detail.ToString().TrimEnd());
                    }
                }
            }

            await Task.Delay(pollMs).ConfigureAwait(false);
        }

        return new LauncherInjectionResult(
            false,
            launcherProcess?.Id,
            null,
            "注入：超时未找到可注入 GameApp",
            detail.ToString().TrimEnd());
    }

    private static bool IsWaitModuleMode(string mode) =>
        mode.Contains("Network", StringComparison.OrdinalIgnoreCase) ||
        mode.Contains("模块", StringComparison.OrdinalIgnoreCase);

    private static bool IsAttachExistingMode(string mode) =>
        mode.Contains("已有", StringComparison.OrdinalIgnoreCase) ||
        mode.Contains("attach", StringComparison.OrdinalIgnoreCase);

    private static HashSet<int> SnapshotProcessIdsByName(string processName)
    {
        var result = new HashSet<int>();
        foreach (var process in Process.GetProcessesByName(processName))
        {
            using (process)
            {
                result.Add(process.Id);
            }
        }

        return result;
    }

    private static bool HasModuleLoaded(Process process, string moduleName)
    {
        try
        {
            foreach (ProcessModule module in process.Modules)
            {
                if (string.Equals(module.ModuleName, moduleName, StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
            }
        }
        catch
        {
            return false;
        }

        return false;
    }

    private static LauncherInjectionResult InjectDll(int pid, string dllPath)
    {
        var detail = new StringBuilder();
        var process = OpenProcess(
            ProcessCreateThread | ProcessQueryInformation | ProcessVmOperation | ProcessVmWrite | ProcessVmRead,
            false,
            (uint)pid);
        if (process == IntPtr.Zero)
        {
            return new LauncherInjectionResult(false, null, pid, $"注入：OpenProcess 失败 pid={pid}", $"OpenProcess error={Marshal.GetLastWin32Error()}");
        }

        IntPtr remotePath = IntPtr.Zero;
        IntPtr thread = IntPtr.Zero;
        try
        {
            var pathBytes = Encoding.Unicode.GetBytes(dllPath + "\0");
            remotePath = VirtualAllocEx(process, IntPtr.Zero, (UIntPtr)pathBytes.Length, MemCommit | MemReserve, PageReadWrite);
            if (remotePath == IntPtr.Zero)
            {
                return new LauncherInjectionResult(false, null, pid, $"注入：VirtualAllocEx 失败 pid={pid}", $"VirtualAllocEx error={Marshal.GetLastWin32Error()}");
            }

            if (!WriteProcessMemory(process, remotePath, pathBytes, (UIntPtr)pathBytes.Length, out _))
            {
                return new LauncherInjectionResult(false, null, pid, $"注入：WriteProcessMemory 失败 pid={pid}", $"WriteProcessMemory error={Marshal.GetLastWin32Error()}");
            }

            var kernel32 = GetModuleHandle("kernel32.dll");
            var loadLibraryW = GetProcAddress(kernel32, "LoadLibraryW");
            if (loadLibraryW == IntPtr.Zero)
            {
                return new LauncherInjectionResult(false, null, pid, "注入：找不到 LoadLibraryW", $"GetProcAddress error={Marshal.GetLastWin32Error()}");
            }

            thread = CreateRemoteThread(process, IntPtr.Zero, 0, loadLibraryW, remotePath, 0, IntPtr.Zero);
            if (thread == IntPtr.Zero)
            {
                return new LauncherInjectionResult(false, null, pid, $"注入：CreateRemoteThread 失败 pid={pid}", $"CreateRemoteThread error={Marshal.GetLastWin32Error()}");
            }

            var wait = WaitForSingleObject(thread, 15000);
            _ = GetExitCodeThread(thread, out var remoteResult);
            if (wait != WaitObject0 || remoteResult == 0)
            {
                return new LauncherInjectionResult(false, null, pid, $"注入：LoadLibraryW 失败 pid={pid}", $"LoadLibraryW wait={wait} result=0x{remoteResult:X8}");
            }

            detail.AppendLine($"Injected pid={pid} dll={dllPath} module=0x{remoteResult:X8}");
            return new LauncherInjectionResult(true, null, pid, $"注入：成功 pid={pid}", detail.ToString().TrimEnd());
        }
        finally
        {
            if (thread != IntPtr.Zero)
            {
                CloseHandle(thread);
            }

            if (remotePath != IntPtr.Zero)
            {
                VirtualFreeEx(process, remotePath, UIntPtr.Zero, MemRelease);
            }

            CloseHandle(process);
        }
    }

    private static void EnableDebugPrivilege(StringBuilder detail)
    {
        if (!OpenProcessToken(GetCurrentProcess(), TokenAdjustPrivileges | TokenQuery, out var token))
        {
            detail.AppendLine($"SeDebugPrivilege: OpenProcessToken failed error={Marshal.GetLastWin32Error()}");
            return;
        }

        try
        {
            if (!LookupPrivilegeValue(null, "SeDebugPrivilege", out var luid))
            {
                detail.AppendLine($"SeDebugPrivilege: LookupPrivilegeValue failed error={Marshal.GetLastWin32Error()}");
                return;
            }

            var privileges = new TokenPrivileges
            {
                PrivilegeCount = 1,
                Luid = luid,
                Attributes = SePrivilegeEnabled
            };

            if (!AdjustTokenPrivileges(token, false, ref privileges, 0, IntPtr.Zero, IntPtr.Zero))
            {
                detail.AppendLine($"SeDebugPrivilege: AdjustTokenPrivileges failed error={Marshal.GetLastWin32Error()}");
            }
            else
            {
                detail.AppendLine("SeDebugPrivilege: requested");
            }
        }
        finally
        {
            CloseHandle(token);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Luid
    {
        public uint LowPart;
        public int HighPart;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct TokenPrivileges
    {
        public uint PrivilegeCount;
        public Luid Luid;
        public uint Attributes;
    }

    [DllImport("kernel32.dll")]
    private static extern IntPtr GetCurrentProcess();

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool OpenProcessToken(IntPtr processHandle, uint desiredAccess, out IntPtr tokenHandle);

    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern bool LookupPrivilegeValue(string? systemName, string name, out Luid luid);

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool AdjustTokenPrivileges(IntPtr tokenHandle, bool disableAllPrivileges, ref TokenPrivileges newState, uint bufferLength, IntPtr previousState, IntPtr returnLength);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint desiredAccess, bool inheritHandle, uint processId);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, UIntPtr size, uint allocationType, uint protect);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool VirtualFreeEx(IntPtr process, IntPtr address, UIntPtr size, uint freeType);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool WriteProcessMemory(IntPtr process, IntPtr baseAddress, byte[] buffer, UIntPtr size, out UIntPtr written);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr GetModuleHandle(string moduleName);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi)]
    private static extern IntPtr GetProcAddress(IntPtr module, string procName);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr CreateRemoteThread(IntPtr process, IntPtr threadAttributes, uint stackSize, IntPtr startAddress, IntPtr parameter, uint creationFlags, IntPtr threadId);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetExitCodeThread(IntPtr thread, out uint exitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);
}
