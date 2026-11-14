using System.Diagnostics;
using System.IO;
using System.Net;
using System.Runtime.InteropServices;

namespace PacketDumpViewer.App.Services.CaptureBackends;

internal sealed class TcpOwnerPidLookup
{
    private const int AfInet = 2;
    private readonly object _sync = new();
    private readonly Dictionary<ConnectionKey, int> _ownerByConnection = new();
    private readonly HashSet<int> _targetPids = [];
    private string _targetProcessName = "GameApp.exe";
    private DateTime _lastRefreshUtc = DateTime.MinValue;
    private string _lastStatus = "process map not refreshed";

    public string LastStatus
    {
        get
        {
            lock (_sync)
            {
                return _lastStatus;
            }
        }
    }

    public void Configure(string processName)
    {
        lock (_sync)
        {
            _targetProcessName = string.IsNullOrWhiteSpace(processName) ? "GameApp.exe" : Path.GetFileName(processName.Trim());
            _lastRefreshUtc = DateTime.MinValue;
            _ownerByConnection.Clear();
            _targetPids.Clear();
            _lastStatus = "process map configured";
        }
    }

    public TcpOwnerMatch Match(IPAddress sourceAddress, int sourcePort, IPAddress destinationAddress, int destinationPort)
    {
        RefreshIfNeeded();

        var source = sourceAddress.ToString();
        var destination = destinationAddress.ToString();
        lock (_sync)
        {
            if (_targetPids.Count == 0)
            {
                return new TcpOwnerMatch(OwnerMatchKind.NoTargetProcess, null, "-", "未找到目标进程");
            }

            if (_ownerByConnection.TryGetValue(new ConnectionKey(source, sourcePort, destination, destinationPort), out var outboundPid))
            {
                return _targetPids.Contains(outboundPid)
                    ? new TcpOwnerMatch(OwnerMatchKind.TargetProcess, outboundPid, "C->S", $"pid={outboundPid}")
                    : new TcpOwnerMatch(OwnerMatchKind.OtherProcess, outboundPid, "-", $"pid={outboundPid}");
            }

            if (_ownerByConnection.TryGetValue(new ConnectionKey(destination, destinationPort, source, sourcePort), out var inboundPid))
            {
                return _targetPids.Contains(inboundPid)
                    ? new TcpOwnerMatch(OwnerMatchKind.TargetProcess, inboundPid, "S->C", $"pid={inboundPid}")
                    : new TcpOwnerMatch(OwnerMatchKind.OtherProcess, inboundPid, "-", $"pid={inboundPid}");
            }

            return new TcpOwnerMatch(OwnerMatchKind.UnknownOwner, null, "-", "pid=unknown");
        }
    }

    private void RefreshIfNeeded()
    {
        var now = DateTime.UtcNow;
        lock (_sync)
        {
            if ((now - _lastRefreshUtc).TotalMilliseconds < 800)
            {
                return;
            }

            _lastRefreshUtc = now;
        }

        var processName = Path.GetFileNameWithoutExtension(_targetProcessName);
        var targetPids = Process.GetProcessesByName(processName)
            .Select(process => process.Id)
            .ToHashSet();

        var ownerByConnection = ReadTcpOwnerTable();
        lock (_sync)
        {
            _targetPids.Clear();
            foreach (var pid in targetPids)
            {
                _targetPids.Add(pid);
            }

            _ownerByConnection.Clear();
            foreach (var item in ownerByConnection)
            {
                _ownerByConnection[item.Key] = item.Value;
            }

            _lastStatus = $"target={_targetProcessName}; pids={string.Join(",", _targetPids)}; tcpOwnerRows={_ownerByConnection.Count}";
        }
    }

    private static Dictionary<ConnectionKey, int> ReadTcpOwnerTable()
    {
        var bufferSize = 0;
        _ = GetExtendedTcpTable(IntPtr.Zero, ref bufferSize, true, AfInet, TcpTableClass.TcpTableOwnerPidAll, 0);
        if (bufferSize <= 0)
        {
            return [];
        }

        var buffer = Marshal.AllocHGlobal(bufferSize);
        try
        {
            var result = GetExtendedTcpTable(buffer, ref bufferSize, true, AfInet, TcpTableClass.TcpTableOwnerPidAll, 0);
            if (result != 0)
            {
                return [];
            }

            var rows = Marshal.ReadInt32(buffer);
            var rowPtr = IntPtr.Add(buffer, sizeof(int));
            var rowSize = Marshal.SizeOf<MibTcpRowOwnerPid>();
            var map = new Dictionary<ConnectionKey, int>();
            for (var i = 0; i < rows; i++)
            {
                var row = Marshal.PtrToStructure<MibTcpRowOwnerPid>(IntPtr.Add(rowPtr, i * rowSize));
                var localAddress = new IPAddress(row.LocalAddress).ToString();
                var remoteAddress = new IPAddress(row.RemoteAddress).ToString();
                var localPort = ConvertPort(row.LocalPort);
                var remotePort = ConvertPort(row.RemotePort);
                map[new ConnectionKey(localAddress, localPort, remoteAddress, remotePort)] = (int)row.OwningPid;
            }

            return map;
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    private static int ConvertPort(uint port)
    {
        var bytes = BitConverter.GetBytes(port);
        return (bytes[0] << 8) + bytes[1];
    }

    [DllImport("iphlpapi.dll", SetLastError = true)]
    private static extern uint GetExtendedTcpTable(
        IntPtr tcpTable,
        ref int tcpTableLength,
        [MarshalAs(UnmanagedType.Bool)] bool sort,
        int ipVersion,
        TcpTableClass tcpTableClass,
        uint reserved);

    private readonly record struct ConnectionKey(string LocalAddress, int LocalPort, string RemoteAddress, int RemotePort);

    private enum TcpTableClass
    {
        TcpTableOwnerPidAll = 5
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MibTcpRowOwnerPid
    {
        public uint State;
        public uint LocalAddress;
        public uint LocalPort;
        public uint RemoteAddress;
        public uint RemotePort;
        public uint OwningPid;
    }
}

internal enum OwnerMatchKind
{
    TargetProcess,
    OtherProcess,
    UnknownOwner,
    NoTargetProcess
}

internal sealed record TcpOwnerMatch(OwnerMatchKind Kind, int? Pid, string Direction, string Note);
