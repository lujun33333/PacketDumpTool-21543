using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Threading;
using Microsoft.Win32;
using PacketDumpViewer.App.Models;
using PacketDumpViewer.App.Services;

namespace PacketDumpViewer.App.ViewModels;

public sealed class MainViewModel : ObservableObject
{
    private const int PageSize = 4096;
    private readonly DumpFolderScanner _scanner = new(new PacketIndexService());
    private readonly PayloadReadService _payloadReadService = new();
    private readonly HexViewService _hexViewService = new();
    private readonly ParserBridge _parserBridge = new();
    private readonly OnlineCaptureService _onlineCaptureService = new();
    private readonly LauncherInjectionService _launcherInjectionService = new();
    private readonly NativeModuleExtractor _nativeModuleExtractor = new();
    private readonly DispatcherTimer _onlineCaptureRefreshTimer = new();
    private readonly List<PacketRecord> _allPackets = [];

    private bool _isDumpViewerPageVisible = true;
    private bool _isWiresharkDecryptPageVisible;
    private string _activePageTitle = "Dump Viewer / Dump查看";
    private string _dumpDirectory = "";
    private string _searchText = "";
    private string _msgIdFilter = "";
    private string _selectedDirection = "全部";
    private string _minLengthFilter = "";
    private string _statusText = "请选择 packet_dumps_<pid> 目录。";
    private string _logStatusText = "trace: - | dump: - | parser_state: -";
    private string _payloadDirectoryPath = "";
    private PacketRecord? _selectedPacket;
    private long _currentOffset;
    private string _currentRangeText = "当前页: -";
    private string _jumpOffsetText = "";
    private string _parserStatusText = "not connected";
    private string _parserNoteText = "v1 暂不接入 DLL";
    private string _structureText = "No packet selected.";
    private string _cppBackendStatus = "在线后端：自写 key_probe.dll Hook；旧实验后端入口已移除。";
    private string _wiresharkStatusText = "在线抓包 UI 已就绪；主流程使用自写 C++ KeyProbe Hook 获取运行态 Session Key。";
    private string _wiresharkPlanText = BuildDefaultWiresharkPlanText();
    private string _wiresharkSessionKeyHex = "";
    private string _wiresharkSessionKeyAscii = "";
    private string _wiresharkSessionKeyStatus = "Session Key：未填写 / 未获取";
    private string _wiresharkDecryptedHexText = "在线解密 HEX 显示在这里。\r\n默认走内部Hook：接收从 Network.dll Unpack 写出明文，发送从 Network.dll Pack 写出明文 body；Top-Kart source object 作为可选补充。";
    private string _wiresharkStructureText = "结构体解析结果显示在这里。\r\n选中在线解密后的 packet 后，按 MsgID 复用现有 ParserBridge。";
    private string _selectedOnlinePacketDecodeText = "请选择上方包列表中的一条 packet。";
    private string _onlineProcessName = "GameApp.exe";
    private string _onlineLauncherPath = "";
    private string _onlineLauncherArguments = "";
    private string _onlineLauncherWorkDirectory = "";
    private string _onlineInjectionMode = "新GameApp出现立即注入（PacketDumpTool默认）";
    private string _onlineModuleName = "Network.dll";
    private string _onlineAdapterName = "自动选择 / Auto";
    private string _onlineCaptureEngine = "自动 / Auto";
    private string _onlineCaptureStatus = "在线抓包：未启动";
    private string _onlineCaptureEngineStatus = "抓包引擎：自动";
    private string _onlineKeyProbeStatus = "密钥模块：未接入";
    private string _onlineTopKartHookStatus = "Top-Kart Hook：未启动";
    private string _onlineBackendStatus = "后端：未启动";
    private string _onlineLauncherInjectionStatus = "注入：未启动";
    private string _onlinePacketsCountText = "Packets：0";
    private string _onlinePacketMsgIdSearch = "";
    private string _onlinePacketSortOrder = "包序列 ↑ 小到大";
    private WiresharkPacketPreviewRow? _selectedWiresharkPacketRow;
    private WiresharkPacketPreviewRow? _selectedTopKartPacketRow;
    private string _selectedTopKartPacketText = "Top-Kart 包会显示在这里；点击右侧 Top 包后，下方 HEX/Structure 跟随刷新。";
    private IReadOnlyList<CapturedPacketPreview> _lastOnlinePackets = [];
    private string _onlineStructureSelectionKey = "";
    private bool _isRefreshingOnlinePacketRows;

    public MainViewModel()
    {
        ShowDumpViewerCommand = new RelayCommand(ShowDumpViewerPage);
        ShowWiresharkDecryptCommand = new RelayCommand(ShowWiresharkDecryptPage);
        ChooseDirectoryCommand = new AsyncRelayCommand(ChooseDirectoryAsync);
        RescanCommand = new AsyncRelayCommand(ScanAsync, () => Directory.Exists(DumpDirectory));
        OpenPayloadDirectoryCommand = new RelayCommand(OpenPayloadDirectory, () => Directory.Exists(PayloadDirectoryPath));
        CopyFilePathCommand = new RelayCommand(CopySelectedFilePath, () => SelectedPacket is not null);
        PreviousPageCommand = new AsyncRelayCommand(PreviousPageAsync, () => SelectedPacket is not null && CurrentOffset > 0);
        NextPageCommand = new AsyncRelayCommand(NextPageAsync, () => SelectedPacket is not null);
        JumpOffsetCommand = new AsyncRelayCommand(JumpOffsetAsync, () => SelectedPacket is not null);
        CompareSiblingDumpsCommand = new AsyncRelayCommand(CompareSiblingDumpsAsync, () => SelectedPacket is not null);
        CopyWiresharkSessionKeyCommand = new RelayCommand(CopyWiresharkSessionKey, () => !string.IsNullOrWhiteSpace(WiresharkSessionKeyHex));
        SaveWiresharkSessionKeyCommand = new RelayCommand(SaveWiresharkSessionKey, () => !string.IsNullOrWhiteSpace(WiresharkSessionKeyHex));
        ClearWiresharkSessionKeyCommand = new RelayCommand(ClearWiresharkSessionKey, () => !string.IsNullOrWhiteSpace(WiresharkSessionKeyHex));
        SelectOnlineLauncherCommand = new RelayCommand(SelectOnlineLauncher);
        RefreshOnlineProcessCommand = new RelayCommand(RefreshOnlineProcess);
        StartOnlineCaptureCommand = new AsyncRelayCommand(StartOnlineCaptureAsync);
        StopOnlineCaptureCommand = new RelayCommand(StopOnlineCapture);
        ClearOnlineCaptureCommand = new RelayCommand(ClearOnlineCapture);
        ExportOnlineCaptureJsonCommand = new RelayCommand(ExportOnlineCaptureJson);
        ResetOnlinePacketPreviewRows();
        _onlineCaptureRefreshTimer.Interval = TimeSpan.FromSeconds(1);
        _onlineCaptureRefreshTimer.Tick += (_, _) =>
        {
            if (IsWiresharkDecryptPageVisible)
            {
                ApplyOnlineCaptureSnapshot(_onlineCaptureService.Refresh(OnlineProcessName, OnlineAdapterName, OnlineCaptureEngine, WiresharkSessionKeyHex));
            }
        };
    }

    public ObservableCollection<PacketRecord> Packets { get; } = [];
    public ObservableCollection<HexRow> HexRows { get; } = [];
    public ObservableCollection<ParsedFieldNode> StructureNodes { get; } = [];
    public ObservableCollection<ParsedFieldNode> OnlineStructureNodes { get; } = [];
    public ObservableCollection<string> DirectionOptions { get; } = ["全部"];
    public ObservableCollection<string> OnlineInjectionModeOptions { get; } =
    [
        "新GameApp出现立即注入（PacketDumpTool默认）",
        "等待Network.dll后注入（旧兼容）",
        "仅附加已有GameApp"
    ];
    public ObservableCollection<string> OnlineCaptureEngineOptions { get; } =
    [
        "自动 / Auto",
        "内部Hook明文",
        "Npcap 原始抓包",
        "Npcap TUN + 进程过滤（推荐）"
    ];
    public ObservableCollection<string> OnlinePacketSortOptions { get; } =
    [
        "包序列 ↑ 小到大",
        "包序列 ↓ 大到小"
    ];
    public ObservableCollection<WiresharkPacketPreviewRow> WiresharkPacketRows { get; } = [];
    public ObservableCollection<WiresharkPacketPreviewRow> TopKartPacketRows { get; } = [];

    public RelayCommand ShowDumpViewerCommand { get; }
    public RelayCommand ShowWiresharkDecryptCommand { get; }
    public AsyncRelayCommand ChooseDirectoryCommand { get; }
    public AsyncRelayCommand RescanCommand { get; }
    public RelayCommand OpenPayloadDirectoryCommand { get; }
    public RelayCommand CopyFilePathCommand { get; }
    public AsyncRelayCommand PreviousPageCommand { get; }
    public AsyncRelayCommand NextPageCommand { get; }
    public AsyncRelayCommand JumpOffsetCommand { get; }
    public AsyncRelayCommand CompareSiblingDumpsCommand { get; }
    public RelayCommand CopyWiresharkSessionKeyCommand { get; }
    public RelayCommand SaveWiresharkSessionKeyCommand { get; }
    public RelayCommand ClearWiresharkSessionKeyCommand { get; }
    public RelayCommand SelectOnlineLauncherCommand { get; }
    public RelayCommand RefreshOnlineProcessCommand { get; }
    public AsyncRelayCommand StartOnlineCaptureCommand { get; }
    public RelayCommand StopOnlineCaptureCommand { get; }
    public RelayCommand ClearOnlineCaptureCommand { get; }
    public RelayCommand ExportOnlineCaptureJsonCommand { get; }

    public bool IsDumpViewerPageVisible
    {
        get => _isDumpViewerPageVisible;
        private set => SetProperty(ref _isDumpViewerPageVisible, value);
    }

    public bool IsWiresharkDecryptPageVisible
    {
        get => _isWiresharkDecryptPageVisible;
        private set => SetProperty(ref _isWiresharkDecryptPageVisible, value);
    }

    public string ActivePageTitle
    {
        get => _activePageTitle;
        set => SetProperty(ref _activePageTitle, value);
    }

    public string DumpDirectory
    {
        get => _dumpDirectory;
        set
        {
            if (SetProperty(ref _dumpDirectory, value))
            {
                RaiseCommandStates();
            }
        }
    }

    public string SearchText
    {
        get => _searchText;
        set
        {
            if (SetProperty(ref _searchText, value))
            {
                ApplyFilters();
            }
        }
    }

    public string MsgIdFilter
    {
        get => _msgIdFilter;
        set
        {
            if (SetProperty(ref _msgIdFilter, value))
            {
                ApplyFilters();
            }
        }
    }

    public string SelectedDirection
    {
        get => _selectedDirection;
        set
        {
            if (SetProperty(ref _selectedDirection, value))
            {
                ApplyFilters();
            }
        }
    }

    public string MinLengthFilter
    {
        get => _minLengthFilter;
        set
        {
            if (SetProperty(ref _minLengthFilter, value))
            {
                ApplyFilters();
            }
        }
    }

    public PacketRecord? SelectedPacket
    {
        get => _selectedPacket;
        set
        {
            if (SetProperty(ref _selectedPacket, value))
            {
                _ = LoadSelectedPacketAsync(0);
                RaiseCommandStates();
                _ = RefreshParserAsync(compareSiblings: false);
            }
        }
    }

    public string StatusText
    {
        get => _statusText;
        set => SetProperty(ref _statusText, value);
    }

    public string LogStatusText
    {
        get => _logStatusText;
        set => SetProperty(ref _logStatusText, value);
    }

    public string PayloadDirectoryPath
    {
        get => _payloadDirectoryPath;
        set
        {
            if (SetProperty(ref _payloadDirectoryPath, value))
            {
                RaiseCommandStates();
            }
        }
    }

    public long CurrentOffset
    {
        get => _currentOffset;
        private set
        {
            if (SetProperty(ref _currentOffset, value))
            {
                RaiseCommandStates();
            }
        }
    }

    public string CurrentRangeText
    {
        get => _currentRangeText;
        set => SetProperty(ref _currentRangeText, value);
    }

    public string JumpOffsetText
    {
        get => _jumpOffsetText;
        set => SetProperty(ref _jumpOffsetText, value);
    }

    public string ParserStatusText
    {
        get => _parserStatusText;
        set => SetProperty(ref _parserStatusText, value);
    }

    public string ParserNoteText
    {
        get => _parserNoteText;
        set => SetProperty(ref _parserNoteText, value);
    }

    public string StructureText
    {
        get => _structureText;
        set => SetProperty(ref _structureText, value);
    }

    public string CppBackendStatus
    {
        get => _cppBackendStatus;
        set => SetProperty(ref _cppBackendStatus, value);
    }

    public string WiresharkStatusText
    {
        get => _wiresharkStatusText;
        set => SetProperty(ref _wiresharkStatusText, value);
    }

    public string WiresharkPlanText
    {
        get => _wiresharkPlanText;
        set => SetProperty(ref _wiresharkPlanText, value);
    }

    public string WiresharkSessionKeyHex
    {
        get => _wiresharkSessionKeyHex;
        set
        {
            if (!SetProperty(ref _wiresharkSessionKeyHex, NormalizeKeyHex(value)))
            {
                return;
            }

            UpdateWiresharkSessionKeyPreview();
            CopyWiresharkSessionKeyCommand.RaiseCanExecuteChanged();
            SaveWiresharkSessionKeyCommand.RaiseCanExecuteChanged();
            ClearWiresharkSessionKeyCommand.RaiseCanExecuteChanged();
        }
    }

    public string WiresharkSessionKeyAscii
    {
        get => _wiresharkSessionKeyAscii;
        private set => SetProperty(ref _wiresharkSessionKeyAscii, value);
    }

    public string WiresharkSessionKeyStatus
    {
        get => _wiresharkSessionKeyStatus;
        private set => SetProperty(ref _wiresharkSessionKeyStatus, value);
    }

    public string WiresharkDecryptedHexText
    {
        get => _wiresharkDecryptedHexText;
        set => SetProperty(ref _wiresharkDecryptedHexText, value);
    }

    public string WiresharkStructureText
    {
        get => _wiresharkStructureText;
        set => SetProperty(ref _wiresharkStructureText, value);
    }

    public string SelectedOnlinePacketDecodeText
    {
        get => _selectedOnlinePacketDecodeText;
        set => SetProperty(ref _selectedOnlinePacketDecodeText, value);
    }

    public string OnlineProcessName
    {
        get => _onlineProcessName;
        set => SetProperty(ref _onlineProcessName, value);
    }

    public string OnlineLauncherPath
    {
        get => _onlineLauncherPath;
        set
        {
            if (SetProperty(ref _onlineLauncherPath, value) &&
                string.IsNullOrWhiteSpace(OnlineLauncherWorkDirectory))
            {
                OnlineLauncherWorkDirectory = Path.GetDirectoryName(value) ?? "";
            }
        }
    }

    public string OnlineLauncherArguments
    {
        get => _onlineLauncherArguments;
        set => SetProperty(ref _onlineLauncherArguments, value);
    }

    public string OnlineLauncherWorkDirectory
    {
        get => _onlineLauncherWorkDirectory;
        set => SetProperty(ref _onlineLauncherWorkDirectory, value);
    }

    public string OnlineInjectionMode
    {
        get => _onlineInjectionMode;
        set => SetProperty(ref _onlineInjectionMode, value);
    }

    public string OnlineModuleName
    {
        get => _onlineModuleName;
        set => SetProperty(ref _onlineModuleName, value);
    }

    public string OnlineAdapterName
    {
        get => _onlineAdapterName;
        set => SetProperty(ref _onlineAdapterName, value);
    }

    public string OnlineCaptureEngine
    {
        get => _onlineCaptureEngine;
        set => SetProperty(ref _onlineCaptureEngine, value);
    }

    public string OnlineCaptureStatus
    {
        get => _onlineCaptureStatus;
        set => SetProperty(ref _onlineCaptureStatus, value);
    }

    public string OnlineCaptureEngineStatus
    {
        get => _onlineCaptureEngineStatus;
        set => SetProperty(ref _onlineCaptureEngineStatus, value);
    }

    public string OnlineKeyProbeStatus
    {
        get => _onlineKeyProbeStatus;
        set => SetProperty(ref _onlineKeyProbeStatus, value);
    }

    public string OnlineTopKartHookStatus
    {
        get => _onlineTopKartHookStatus;
        set => SetProperty(ref _onlineTopKartHookStatus, value);
    }

    public string OnlineBackendStatus
    {
        get => _onlineBackendStatus;
        set => SetProperty(ref _onlineBackendStatus, value);
    }

    public string OnlineLauncherInjectionStatus
    {
        get => _onlineLauncherInjectionStatus;
        set => SetProperty(ref _onlineLauncherInjectionStatus, value);
    }

    public string OnlinePacketsCountText
    {
        get => _onlinePacketsCountText;
        set => SetProperty(ref _onlinePacketsCountText, value);
    }

    public string OnlinePacketMsgIdSearch
    {
        get => _onlinePacketMsgIdSearch;
        set
        {
            if (SetProperty(ref _onlinePacketMsgIdSearch, value))
            {
                RefreshOnlinePacketRows();
            }
        }
    }

    public string OnlinePacketSortOrder
    {
        get => _onlinePacketSortOrder;
        set
        {
            if (SetProperty(ref _onlinePacketSortOrder, value))
            {
                RefreshOnlinePacketRows();
            }
        }
    }

    public WiresharkPacketPreviewRow? SelectedWiresharkPacketRow
    {
        get => _selectedWiresharkPacketRow;
        set
        {
            if (SetProperty(ref _selectedWiresharkPacketRow, value))
            {
                if (_isRefreshingOnlinePacketRows)
                {
                    return;
                }

                if (value?.Packet is not null && _selectedTopKartPacketRow is not null)
                {
                    _selectedTopKartPacketRow = null;
                    OnPropertyChanged(nameof(SelectedTopKartPacketRow));
                }

                RefreshSelectedOnlinePacketViews();
            }
        }
    }

    public WiresharkPacketPreviewRow? SelectedTopKartPacketRow
    {
        get => _selectedTopKartPacketRow;
        set
        {
            if (SetProperty(ref _selectedTopKartPacketRow, value))
            {
                if (_isRefreshingOnlinePacketRows)
                {
                    return;
                }

                if (value?.Packet is not null && _selectedWiresharkPacketRow is not null)
                {
                    _selectedWiresharkPacketRow = null;
                    OnPropertyChanged(nameof(SelectedWiresharkPacketRow));
                }

                SelectedTopKartPacketText = value?.Packet is null
                    ? "请选择右侧 Top-Kart 包。"
                    : BuildSelectedOnlinePacketDecodeText(value.Packet);
                RefreshSelectedOnlinePacketViews();
            }
        }
    }

    public string SelectedTopKartPacketText
    {
        get => _selectedTopKartPacketText;
        set => SetProperty(ref _selectedTopKartPacketText, value);
    }

    private async Task CompareSiblingDumpsAsync()
    {
        await RefreshParserAsync(compareSiblings: true);
    }

    private void ShowDumpViewerPage()
    {
        IsDumpViewerPageVisible = true;
        IsWiresharkDecryptPageVisible = false;
        ActivePageTitle = "Dump Viewer / Dump查看";
        StatusText = "已切回 Dump Viewer。";
    }

    private void ShowWiresharkDecryptPage()
    {
        IsDumpViewerPageVisible = false;
        IsWiresharkDecryptPageVisible = true;
        ActivePageTitle = "Online Packet Decrypt / 在线抓包解密";
        RefreshWiresharkPlanText();
        StatusText = "已切换到在线抓包解密页面。";
    }

    private async Task ChooseDirectoryAsync()
    {
        var dialog = new OpenFolderDialog
        {
            Title = "选择 packet_dumps_<pid> 目录",
            Multiselect = false
        };

        if (!string.IsNullOrWhiteSpace(DumpDirectory) && Directory.Exists(DumpDirectory))
        {
            dialog.InitialDirectory = DumpDirectory;
        }

        if (dialog.ShowDialog() == true)
        {
            DumpDirectory = dialog.FolderName;
            await ScanAsync();
        }
    }

    private void RefreshOnlineProcess()
    {
        ApplyOnlineCaptureSnapshot(_onlineCaptureService.Refresh(OnlineProcessName, OnlineAdapterName, OnlineCaptureEngine, WiresharkSessionKeyHex));
    }

    private void SelectOnlineLauncher()
    {
        var dialog = new OpenFileDialog
        {
            Title = "选择 QQSpeed 登陆器 / 启动器",
            Filter = "Executable (*.exe)|*.exe|All files (*.*)|*.*",
            CheckFileExists = true,
            Multiselect = false
        };

        var initialDirectory = GetExistingDirectoryFromFile(OnlineLauncherPath);
        if (!string.IsNullOrWhiteSpace(initialDirectory))
        {
            dialog.InitialDirectory = initialDirectory;
        }

        if (dialog.ShowDialog() == true)
        {
            OnlineLauncherPath = dialog.FileName;
            OnlineLauncherWorkDirectory = Path.GetDirectoryName(dialog.FileName) ?? "";
            OnlineLauncherInjectionStatus = "注入：已选择登陆器；启动后发现新 GameApp.exe 会立即注入。";
            WiresharkStatusText = $"已选择登陆器：{dialog.FileName}";
        }
    }

    private async Task StartOnlineCaptureAsync()
    {
        ApplyOnlineCaptureSnapshot(_onlineCaptureService.Start(OnlineProcessName, OnlineAdapterName, OnlineCaptureEngine, WiresharkSessionKeyHex));
        _onlineCaptureRefreshTimer.Start();
        await StartLauncherInjectionIfNeededAsync();
    }

    private async Task StartLauncherInjectionIfNeededAsync()
    {
        var attachExisting = IsOnlineAttachExistingMode(OnlineInjectionMode);
        if (!attachExisting && string.IsNullOrWhiteSpace(OnlineLauncherPath))
        {
            OnlineLauncherInjectionStatus = "注入：未选择登陆器；只抓已有 GameApp.exe。";
            return;
        }

        var keyProbeModule = _nativeModuleExtractor.PrepareModule(NativeModuleExtractor.KeyProbeModuleName);
        if (!keyProbeModule.Exists || string.IsNullOrWhiteSpace(keyProbeModule.ModulePath))
        {
            OnlineLauncherInjectionStatus = "注入：key_probe.dll 缺失";
            WiresharkStatusText = keyProbeModule.Detail;
            return;
        }

        OnlineLauncherInjectionStatus = attachExisting
            ? "注入：查找已有 GameApp 并注入..."
            : "注入：启动登陆器，等待新 GameApp...";
        var result = await _launcherInjectionService.StartLauncherAndInjectAsync(new LauncherInjectionRequest(
            OnlineLauncherPath,
            OnlineLauncherArguments,
            OnlineLauncherWorkDirectory,
            OnlineProcessName,
            OnlineModuleName,
            keyProbeModule.ModulePath,
            OnlineInjectionMode,
            TimeoutMs: 300000,
            PollMs: 50));

        OnlineLauncherInjectionStatus = result.Status;
        WiresharkStatusText = result.Detail;
    }

    private static bool IsOnlineAttachExistingMode(string mode) =>
        mode.Contains("已有", StringComparison.OrdinalIgnoreCase) ||
        mode.Contains("attach", StringComparison.OrdinalIgnoreCase);

    private void StopOnlineCapture()
    {
        _onlineCaptureRefreshTimer.Stop();
        ApplyOnlineCaptureSnapshot(_onlineCaptureService.Stop(OnlineAdapterName, OnlineCaptureEngine, WiresharkSessionKeyHex));
    }

    private void ClearOnlineCapture()
    {
        ResetOnlinePacketPreviewRows();
        ApplyOnlineCaptureSnapshot(_onlineCaptureService.Clear(OnlineProcessName, OnlineAdapterName, OnlineCaptureEngine, WiresharkSessionKeyHex));
    }

    private void ExportOnlineCaptureJson()
    {
        ApplyOnlineCaptureSnapshot(_onlineCaptureService.ExportJson(OnlineProcessName, OnlineAdapterName, OnlineCaptureEngine, WiresharkSessionKeyHex));
    }

    private void CopyWiresharkSessionKey()
    {
        if (string.IsNullOrWhiteSpace(WiresharkSessionKeyHex))
        {
            return;
        }

        Clipboard.SetText(WiresharkSessionKeyHex);
        WiresharkStatusText = "已复制 Session Key Hex。";
        StatusText = WiresharkStatusText;
    }

    private void SaveWiresharkSessionKey()
    {
        if (string.IsNullOrWhiteSpace(WiresharkSessionKeyHex))
        {
            return;
        }

        var directory = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "PacketDumpViewer",
            "keys");
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, "session_key_latest.txt");
        File.WriteAllText(path, WiresharkSessionKeyHex + Environment.NewLine, Encoding.ASCII);
        WiresharkStatusText = $"已保存 Session Key Hex：{path}";
        StatusText = WiresharkStatusText;
    }

    private void ClearWiresharkSessionKey()
    {
        WiresharkSessionKeyHex = "";
        WiresharkStatusText = "已清空 Session Key。";
        StatusText = WiresharkStatusText;
    }

    private void UpdateWiresharkSessionKeyPreview()
    {
        if (string.IsNullOrWhiteSpace(WiresharkSessionKeyHex))
        {
            WiresharkSessionKeyAscii = "";
            WiresharkSessionKeyStatus = "Session Key：未填写 / 未获取";
            return;
        }

        var bytes = TryParseHexBytes(WiresharkSessionKeyHex);
        if (bytes is null)
        {
            WiresharkSessionKeyAscii = "(hex 格式错误)";
            WiresharkSessionKeyStatus = "Session Key：格式错误";
            return;
        }

        WiresharkSessionKeyAscii = ToPrintableAscii(bytes);
        WiresharkSessionKeyStatus = bytes.Length == 16
            ? "Session Key：16 bytes OK"
            : $"Session Key：{bytes.Length} bytes（QQSpeed TEA 通常需要 16 bytes）";
    }

    private static string NormalizeKeyHex(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return "";
        }

        var hex = new string(value.Where(Uri.IsHexDigit).ToArray()).ToUpperInvariant();
        if (hex.Length == 0)
        {
            return "";
        }

        var builder = new StringBuilder(hex.Length + hex.Length / 2);
        for (var i = 0; i < hex.Length; i += 2)
        {
            if (builder.Length > 0)
            {
                builder.Append(' ');
            }

            builder.Append(hex[i]);
            if (i + 1 < hex.Length)
            {
                builder.Append(hex[i + 1]);
            }
        }

        return builder.ToString();
    }

    private static byte[]? TryParseHexBytes(string hexText)
    {
        var hex = new string(hexText.Where(Uri.IsHexDigit).ToArray());
        if (hex.Length == 0 || hex.Length % 2 != 0)
        {
            return null;
        }

        var bytes = new byte[hex.Length / 2];
        for (var i = 0; i < bytes.Length; i++)
        {
            if (!byte.TryParse(hex.AsSpan(i * 2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out bytes[i]))
            {
                return null;
            }
        }

        return bytes;
    }

    private static string ToPrintableAscii(byte[] bytes)
    {
        var chars = bytes.Select(value => value is >= 0x20 and <= 0x7E ? (char)value : '.').ToArray();
        return new string(chars);
    }

    private void ApplyOnlineCaptureSnapshot(OnlineCaptureSnapshot snapshot)
    {
        var detectedSessionKeyHex = snapshot.DetectedSessionKeyHex ?? "";
        var keyProbeFilledSessionKey =
            string.IsNullOrWhiteSpace(WiresharkSessionKeyHex) &&
            !string.IsNullOrWhiteSpace(detectedSessionKeyHex) &&
            TryParseHexBytes(detectedSessionKeyHex)?.Length == 16;
        if (keyProbeFilledSessionKey)
        {
            WiresharkSessionKeyHex = detectedSessionKeyHex;
        }

        OnlineCaptureStatus = snapshot.CaptureStatus;
        OnlineCaptureEngineStatus = snapshot.CaptureEngineStatus;
        OnlineKeyProbeStatus = snapshot.KeyProbeStatus;
        OnlineTopKartHookStatus = snapshot.TopKartHookStatus;
        OnlineBackendStatus = snapshot.BackendStatus;
        OnlinePacketsCountText = snapshot.PacketCountText;
        CppBackendStatus = snapshot.CppBackendStatus;
        WiresharkStatusText = keyProbeFilledSessionKey
            ? snapshot.StatusText + " KeyProbe 已填入 Session Key。"
            : snapshot.StatusText;
        UpdateOnlinePacketPreview(snapshot);
        if (SelectedWiresharkPacketRow?.Packet is null && SelectedTopKartPacketRow?.Packet is null)
        {
            SelectedOnlinePacketDecodeText = "请选择上方包列表中的一条 packet。";
            WiresharkDecryptedHexText = snapshot.DecryptedHexText;
            WiresharkStructureText = snapshot.StructureText;
            OnlineStructureNodes.Clear();
        }
        else
        {
            RefreshSelectedOnlinePacketViews();
        }
        RefreshWiresharkPlanText();
        StatusText = WiresharkStatusText;
    }

    private void UpdateOnlinePacketPreview(OnlineCaptureSnapshot snapshot)
    {
        _lastOnlinePackets = snapshot.RecentPackets;
        RefreshOnlinePacketRows();
    }

    private void RefreshOnlinePacketRows()
    {
        var selectedPacketIndex = SelectedWiresharkPacketRow?.PacketIndexNumber;
        var selectedTopPacketIndex = SelectedTopKartPacketRow?.PacketIndexNumber;
        var rows = BuildOnlinePacketRows(_lastOnlinePackets, OnlinePacketMsgIdSearch, OnlinePacketSortOrder, includeTopKart: false);
        var topRows = BuildOnlinePacketRows(_lastOnlinePackets, "", OnlinePacketSortOrder, onlyTopKart: true);

        _isRefreshingOnlinePacketRows = true;
        try
        {
            WiresharkPacketRows.Clear();
            TopKartPacketRows.Clear();

            foreach (var row in topRows)
            {
                TopKartPacketRows.Add(row);
            }

            var nextTopSelection = selectedTopPacketIndex is > 0
                ? topRows.FirstOrDefault(row => row.PacketIndexNumber == selectedTopPacketIndex)
                : null;
            SelectedTopKartPacketRow = nextTopSelection;

            if (topRows.Count == 0)
            {
                TopKartPacketRows.Add(new WiresharkPacketPreviewRow
                {
                    PacketIndex = "-",
                    Direction = "-",
                    MsgId = "-",
                    MsgType = "-",
                    MsgSeq = "-",
                    BodyLen = "-",
                    Length = "-",
                    Function = "等待 Top-Kart Hook 包"
                });
                SelectedTopKartPacketText = "Top-Kart Hook 还没有捕获到 source object 包。";
            }

            WiresharkPacketPreviewRow? nextMainSelection = null;
            if (rows.Count == 0)
            {
                WiresharkPacketRows.Add(new WiresharkPacketPreviewRow
                {
                    PacketIndex = "-",
                    Direction = "-",
                    MsgId = "-",
                    MsgType = "-",
                    MsgSeq = "-",
                    BodyLen = "-",
                    Length = "-",
                    Function = "等待 Network 明文包"
                });
            }
            else
            {
                foreach (var row in rows)
                {
                    WiresharkPacketRows.Add(row);
                }

                nextMainSelection =
                    selectedPacketIndex is > 0
                        ? rows.FirstOrDefault(row => row.PacketIndexNumber == selectedPacketIndex) ?? rows[0]
                        : rows[0];
            }

            SelectedWiresharkPacketRow = nextTopSelection?.Packet is not null ? null : nextMainSelection;
        }
        finally
        {
            _isRefreshingOnlinePacketRows = false;
        }

        RefreshSelectedOnlinePacketViews();
    }

    public static IReadOnlyList<WiresharkPacketPreviewRow> BuildOnlinePacketRows(
        IReadOnlyList<CapturedPacketPreview> packets,
        string msgIdSearch,
        string sortOrder,
        bool includeTopKart = true,
        bool onlyTopKart = false)
    {
        if (packets.Count == 0)
        {
            return [];
        }

        IEnumerable<CapturedPacketPreview> packetSource;
        if (onlyTopKart)
        {
            packetSource = packets.Where(IsTopKartPacket);
        }
        else
        {
            packetSource = FilterMainProtocolPackets(packets, includeTopKart);
        }

        IEnumerable<WiresharkPacketPreviewRow> rows = packetSource.Select(packet =>
        {
            var decode = packet.Decode ?? PacketDecodePreview.Waiting("waiting stream decoder");
            var isTopKartPacket = IsTopKartPacket(packet);
            var inferredOuterMsgId = decode.OuterMsgId ??
                                     (decode.MsgId.Equals("28346", StringComparison.OrdinalIgnoreCase) &&
                                      (decode.Status.Contains("raw-wrapper", StringComparison.OrdinalIgnoreCase) ||
                                       decode.Status.Contains("28346-wrapper", StringComparison.OrdinalIgnoreCase))
                                         ? 28346
                                         : null);
            var inferredWrapperKind = !string.IsNullOrWhiteSpace(decode.WrapperKind)
                ? decode.WrapperKind
                : inferredOuterMsgId == 28346 && decode.InnerMsgId is null
                    ? "28346-wrapper"
                    : "";
            return new WiresharkPacketPreviewRow
            {
                Packet = packet,
                IsStructureCandidate = OnlineCaptureService.IsStructureCandidate(packet),
                PacketIndexNumber = packet.PacketIndex,
                PacketIndex = packet.PacketIndex.ToString(),
                Direction = NormalizeDirection(packet.Direction),
                MsgId = decode.DisplayMsgId,
                EffectiveMsgId = decode.EffectiveMsgIdValue,
                OuterMsgId = inferredOuterMsgId,
                InnerMsgId = decode.InnerMsgId,
                WrapperKind = inferredWrapperKind,
                MsgType = decode.MsgType,
                MsgSeq = decode.MsgSeq,
                BodyLen = decode.BodyLen,
                Length = isTopKartPacket ? $"预览 {packet.PayloadLength:N0}B" : $"{packet.PayloadLength:N0} bytes",
                Function = BuildOnlinePacketEndpoint(packet, decode)
            };
        });

        var search = (msgIdSearch ?? "").Trim();
        if (!string.IsNullOrWhiteSpace(search))
        {
            var normalizedChainSearch = search.Replace(" ", "", StringComparison.Ordinal);
            rows = int.TryParse(search, NumberStyles.Integer, CultureInfo.InvariantCulture, out var searchedMsgId)
                ? searchedMsgId == 28346
                    ? rows.Where(row => row.OuterMsgId == 28346 || row.WrapperKind.Contains("28346", StringComparison.OrdinalIgnoreCase))
                    : rows.Where(row => row.EffectiveMsgId == searchedMsgId || row.InnerMsgId == searchedMsgId || row.MsgId.Equals(searchedMsgId.ToString(CultureInfo.InvariantCulture), StringComparison.OrdinalIgnoreCase))
                : rows.Where(row =>
                    row.MsgId.Replace(" ", "", StringComparison.Ordinal).Contains(normalizedChainSearch, StringComparison.OrdinalIgnoreCase) ||
                    row.WrapperKind.Contains(search, StringComparison.OrdinalIgnoreCase));
        }

        var descending = (sortOrder ?? "").Contains('↓', StringComparison.Ordinal) ||
                         (sortOrder ?? "").Contains("大到小", StringComparison.OrdinalIgnoreCase) ||
                         (sortOrder ?? "").Contains("desc", StringComparison.OrdinalIgnoreCase);
        rows = descending
            ? rows.OrderByDescending(row => row.PacketIndexNumber)
            : rows.OrderBy(row => row.PacketIndexNumber);

        return rows.ToArray();
    }

    private static IReadOnlyList<CapturedPacketPreview> FilterMainProtocolPackets(
        IReadOnlyList<CapturedPacketPreview> packets,
        bool includeTopKart)
    {
        if (packets.Count == 0)
        {
            return [];
        }

        var orderedPackets = packets.OrderBy(packet => packet.PacketIndex).ToArray();
        var visible = new List<CapturedPacketPreview>(orderedPackets.Length);
        var recent28346FinalFullPackets = new List<CapturedPacketPreview>();

        for (var i = 0; i < orderedPackets.Length; i++)
        {
            var packet = orderedPackets[i];
            if (IsTopKartPacket(packet))
            {
                if (includeTopKart)
                {
                    visible.Add(packet);
                }

                continue;
            }

            var decode = packet.Decode ?? PacketDecodePreview.Waiting("waiting stream decoder");
            var stage = ExtractInternalHookStage(packet);

            if (IsSend28346AuxiliaryStage(stage, decode))
            {
                continue;
            }

            if (IsReceive28346AuxiliaryStage(stage, decode))
            {
                continue;
            }

            if (IsReceiveInnerDispatchDuplicateBodyStage(stage, decode, orderedPackets, i))
            {
                continue;
            }

            if (IsDuplicateDirectReceiveAfter28346(packet, decode, recent28346FinalFullPackets))
            {
                continue;
            }

            visible.Add(packet);

            if (IsReceive28346FinalFullStage(stage, decode))
            {
                recent28346FinalFullPackets.Add(packet);
                if (recent28346FinalFullPackets.Count > 32)
                {
                    recent28346FinalFullPackets.RemoveAt(0);
                }
            }
        }

        return visible;
    }

    private static bool IsSend28346AuxiliaryStage(string stage, PacketDecodePreview decode)
    {
        if (string.IsNullOrWhiteSpace(stage))
        {
            return false;
        }

        if (stage.Equals("send_safe28346_builder_payload_full", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        return stage.Equals("send_serialize_body_plain", StringComparison.OrdinalIgnoreCase) &&
               (decode.EffectiveMsgIdValue == 28346 ||
                decode.OuterMsgId == 28346 ||
                decode.WrapperKind.Contains("28346-wrapper", StringComparison.OrdinalIgnoreCase));
    }

    private static bool IsReceive28346AuxiliaryStage(string stage, PacketDecodePreview decode)
    {
        if (string.IsNullOrWhiteSpace(stage))
        {
            return false;
        }

        if (stage.Equals("recv_special28346_before_full", StringComparison.OrdinalIgnoreCase) ||
            stage.Equals("recv_special28346_before_inner_body_at12", StringComparison.OrdinalIgnoreCase) ||
            stage.Equals("recv_special28346_after_inner_body_at12", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        return stage.StartsWith("recv_inner_dispatch_", StringComparison.OrdinalIgnoreCase) &&
               decode.EffectiveMsgIdValue == 28346;
    }

    private static bool IsReceiveInnerDispatchDuplicateBodyStage(
        string stage,
        PacketDecodePreview decode,
        IReadOnlyList<CapturedPacketPreview> orderedPackets,
        int currentIndex)
    {
        if (!stage.StartsWith("recv_inner_dispatch_", StringComparison.OrdinalIgnoreCase) ||
            decode.EffectiveMsgIdValue is null ||
            decode.EffectiveMsgIdValue == 28346)
        {
            return false;
        }

        var maxIndex = Math.Min(orderedPackets.Count, currentIndex + 6);
        for (var i = currentIndex + 1; i < maxIndex; i++)
        {
            var next = orderedPackets[i];
            var nextStage = ExtractInternalHookStage(next);
            if (!nextStage.Equals("recv_plain_full", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var nextDecode = next.Decode ?? PacketDecodePreview.Waiting("waiting stream decoder");
            if (nextDecode.EffectiveMsgIdValue == decode.EffectiveMsgIdValue)
            {
                return true;
            }
        }

        return false;
    }

    private static bool IsReceive28346FinalFullStage(string stage, PacketDecodePreview decode) =>
        stage.Equals("recv_special28346_after_full", StringComparison.OrdinalIgnoreCase) &&
        decode.OuterMsgId == 28346 &&
        decode.InnerMsgId is not null;

    private static bool IsDuplicateDirectReceiveAfter28346(
        CapturedPacketPreview packet,
        PacketDecodePreview decode,
        IReadOnlyList<CapturedPacketPreview> recent28346FinalFullPackets)
    {
        if (!ExtractInternalHookStage(packet).Equals("recv_plain_full", StringComparison.OrdinalIgnoreCase) ||
            decode.OuterMsgId is not null ||
            decode.InnerMsgId is not null ||
            recent28346FinalFullPackets.Count == 0)
        {
            return false;
        }

        foreach (var previous in recent28346FinalFullPackets)
        {
            var previousDecode = previous.Decode;
            if (previousDecode is null ||
                Math.Abs(packet.PacketIndex - previous.PacketIndex) > 8 ||
                previous.PayloadLength != packet.PayloadLength ||
                previousDecode.EffectiveMsgIdValue != decode.EffectiveMsgIdValue ||
                !string.Equals(previousDecode.MsgType, decode.MsgType, StringComparison.OrdinalIgnoreCase) ||
                !string.Equals(previousDecode.MsgSeq, decode.MsgSeq, StringComparison.OrdinalIgnoreCase) ||
                !string.Equals(previousDecode.BodyLen, decode.BodyLen, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var previousBytes = previousDecode.PlainPayloadBytes ?? previous.PayloadBytes;
            var currentBytes = decode.PlainPayloadBytes ?? packet.PayloadBytes;
            if (previousBytes.AsSpan().SequenceEqual(currentBytes))
            {
                return true;
            }
        }

        return false;
    }

    private static string ExtractInternalHookStage(CapturedPacketPreview packet)
    {
        const string prefix = "internal_hook_plain ";
        var summary = packet.Summary ?? "";
        var start = summary.IndexOf(prefix, StringComparison.OrdinalIgnoreCase);
        if (start < 0)
        {
            return "";
        }

        start += prefix.Length;
        var end = summary.IndexOf(" msg=", start, StringComparison.OrdinalIgnoreCase);
        return end > start ? summary[start..end].Trim() : summary[start..].Split(' ', StringSplitOptions.RemoveEmptyEntries).FirstOrDefault() ?? "";
    }

    private void RefreshSelectedOnlinePacketViews()
    {
        var selectedPacket = SelectedTopKartPacketRow?.Packet ?? SelectedWiresharkPacketRow?.Packet;
        if (selectedPacket is null)
        {
            SelectedOnlinePacketDecodeText = "请选择上方包列表中的一条 packet。";
            WiresharkDecryptedHexText = "请选择上方包列表中的一条 packet，HEX 会显示这里。";
            WiresharkStructureText = "结构体 / Structure\r\n请先在上方包列表选中一条在线包。";
            _onlineStructureSelectionKey = "";
            OnlineStructureNodes.Clear();
            return;
        }

        SelectedOnlinePacketDecodeText = BuildSelectedOnlinePacketDecodeText(selectedPacket);
        WiresharkDecryptedHexText = BuildSelectedOnlinePacketHexText(selectedPacket);
        var structureKey = BuildOnlineStructureSelectionKey(selectedPacket);
        if (string.Equals(_onlineStructureSelectionKey, structureKey, StringComparison.Ordinal))
        {
            return;
        }

        var structure = _onlineCaptureService.BuildStructureForPacket(selectedPacket);
        WiresharkStructureText = BuildCompactStructureSummary(selectedPacket, structure);
        var expandedKeys = CaptureExpandedNodeKeys(OnlineStructureNodes);
        OnlineStructureNodes.Clear();
        foreach (var node in structure.RootNodes)
        {
            RestoreExpandedNodeKeys(node, expandedKeys);
            OnlineStructureNodes.Add(node);
        }

        _onlineStructureSelectionKey = structureKey;
    }

    private static string BuildOnlineStructureSelectionKey(CapturedPacketPreview packet)
    {
        var decode = packet.Decode;
        var plainLength = decode?.PlainPayloadBytes?.Length ?? 0;
        return string.Join("|",
            packet.PacketIndex,
            packet.Direction,
            packet.PayloadLength,
            decode?.DisplayMsgId ?? "",
            decode?.EffectiveMsgId?.ToString(CultureInfo.InvariantCulture) ?? "",
            decode?.OuterMsgId?.ToString(CultureInfo.InvariantCulture) ?? "",
            decode?.InnerMsgId?.ToString(CultureInfo.InvariantCulture) ?? "",
            decode?.MsgType?.ToString(CultureInfo.InvariantCulture) ?? "",
            decode?.MsgSeq?.ToString(CultureInfo.InvariantCulture) ?? "",
            decode?.BodyLen?.ToString(CultureInfo.InvariantCulture) ?? "",
            decode?.WrapperKind ?? "",
            plainLength.ToString(CultureInfo.InvariantCulture));
    }

    private static HashSet<string> CaptureExpandedNodeKeys(IEnumerable<ParsedFieldNode> nodes)
    {
        var keys = new HashSet<string>(StringComparer.Ordinal);
        foreach (var node in nodes)
        {
            CaptureExpandedNodeKeys(node, keys);
        }

        return keys;
    }

    private static void CaptureExpandedNodeKeys(ParsedFieldNode node, ISet<string> keys)
    {
        if (node.IsExpanded)
        {
            keys.Add(BuildNodeStableKey(node));
        }

        foreach (var child in node.Children)
        {
            CaptureExpandedNodeKeys(child, keys);
        }
    }

    private static void RestoreExpandedNodeKeys(ParsedFieldNode node, ISet<string> keys)
    {
        node.IsExpanded = keys.Contains(BuildNodeStableKey(node));
        foreach (var child in node.Children)
        {
            RestoreExpandedNodeKeys(child, keys);
        }
    }

    private static string BuildNodeStableKey(ParsedFieldNode node) =>
        $"{node.Offset:X8}|{node.Size}|{node.Type}|{node.Name}";

    private static bool IsTopKartPacket(CapturedPacketPreview packet)
    {
        var decode = packet.Decode;
        return packet.Summary.Contains("topkart_source_object", StringComparison.OrdinalIgnoreCase) ||
               packet.Endpoint.Contains("Top-Kart", StringComparison.OrdinalIgnoreCase) ||
               decode?.Status.Contains("topkart-source-object", StringComparison.OrdinalIgnoreCase) == true ||
               decode?.Detail.Contains("Top-Kart.dll RequestGameServer", StringComparison.OrdinalIgnoreCase) == true;
    }

    public static string BuildCompactStructureSummary(CapturedPacketPreview packet, OnlineStructureResult structure)
    {
        var decode = packet.Decode ?? PacketDecodePreview.Waiting("waiting stream decoder");
        var builder = new StringBuilder();
        builder.Append($"#{packet.PacketIndex} {NormalizeDirection(packet.Direction)}");
        builder.Append($"; MsgID={decode.DisplayMsgId}; MsgType={decode.MsgType}; MsgSeq={decode.MsgSeq}; BodyLen={decode.BodyLen}; len={packet.PayloadLength:N0}");

        if (structure.RootNodes.Count > 0)
        {
            builder.Append($"\r\nparser: parsed; roots={structure.RootNodes.Count}; ");
            builder.Append(string.Join(", ", structure.RootNodes.Select(node => node.Name)));
        }
        else
        {
            var firstUsefulLine = structure.Text
                .Split(new[] { "\r\n", "\n" }, StringSplitOptions.RemoveEmptyEntries)
                .FirstOrDefault(line =>
                    line.Contains("不是", StringComparison.OrdinalIgnoreCase) ||
                    line.Contains("没有", StringComparison.OrdinalIgnoreCase) ||
                    line.Contains("不能", StringComparison.OrdinalIgnoreCase) ||
                    line.Contains("parser", StringComparison.OrdinalIgnoreCase));
            builder.Append("\r\nparser: no tree");
            if (!string.IsNullOrWhiteSpace(firstUsefulLine))
            {
                builder.Append("; ");
                builder.Append(firstUsefulLine.Trim());
            }
        }

        return builder.ToString();
    }

    private static string BuildSelectedOnlinePacketDecodeText(CapturedPacketPreview packet)
    {
        var decode = packet.Decode ?? PacketDecodePreview.Waiting("waiting stream decoder");
        var builder = new StringBuilder();
        builder.AppendLine($"选中包 #{packet.PacketIndex}");
        builder.AppendLine($"direction: {NormalizeDirection(packet.Direction)} ({packet.Direction})");
        builder.AppendLine($"length: {packet.PayloadLength:N0} bytes");
        builder.AppendLine($"endpoint/function: {BuildOnlinePacketEndpoint(packet, decode)}");
        builder.AppendLine($"msgid: {decode.DisplayMsgId}");
        if (!string.Equals(decode.DisplayMsgId, decode.MsgId, StringComparison.OrdinalIgnoreCase) ||
            decode.OuterMsgId is not null ||
            decode.InnerMsgId is not null)
        {
            builder.AppendLine($"effective msgid: {decode.EffectiveMsgIdValue?.ToString(CultureInfo.InvariantCulture) ?? decode.MsgId}");
            builder.AppendLine($"outer msgid: {decode.OuterMsgId?.ToString(CultureInfo.InvariantCulture) ?? "-"}");
            builder.AppendLine($"inner msgid: {decode.InnerMsgId?.ToString(CultureInfo.InvariantCulture) ?? "-"}");
        }

        builder.AppendLine($"msgtype: {decode.MsgType}");
        builder.AppendLine($"MsgSeq: {decode.MsgSeq}");
        builder.AppendLine($"BodyLen: {decode.BodyLen}");
        builder.AppendLine($"status: {decode.Status}");
        if (!string.IsNullOrWhiteSpace(decode.Detail))
        {
            builder.AppendLine($"detail: {OnlineCaptureService.SanitizeInternalDetail(decode.Detail)}");
        }

        if (IsTopKartPacket(packet))
        {
            builder.AppendLine("note: Top-Kart source object 现在只抓固定长度预览，不是 wire body 长度；真实发送体以 Network.dll Pack 明文为准。");
        }

        builder.AppendLine();
        builder.AppendLine(OnlineCaptureService.IsStructureCandidate(packet)
            ? "结构体：可进入 ParserBridge；下方 Structure 已按选中包刷新。"
            : "结构体：当前包不是可解析明文或缺少专用 MsgID parser；下方会显示 raw/fallback 说明。");
        return builder.ToString().TrimEnd();
    }

    private static string BuildSelectedOnlinePacketHexText(CapturedPacketPreview packet)
    {
        var decode = packet.Decode ?? PacketDecodePreview.Waiting("waiting stream decoder");
        var bytes = decode.PlainPayloadBytes is { Length: > 0 }
            ? decode.PlainPayloadBytes
            : packet.PayloadBytes;
        var label = decode.PlainPayloadBytes is { Length: > 0 }
            ? "解密/内部Hook明文"
            : "原始 payload";

        var builder = new StringBuilder();
        builder.AppendLine($"{label} HEX - selected packet #{packet.PacketIndex}");
        builder.AppendLine($"direction={NormalizeDirection(packet.Direction)}; msgid={decode.DisplayMsgId}; msgtype={decode.MsgType}; MsgSeq={decode.MsgSeq}; BodyLen={decode.BodyLen}; len={bytes.Length:N0} bytes");
        builder.AppendLine($"status={decode.Status}; {OnlineCaptureService.SanitizeInternalDetail(decode.Detail)}");
        builder.AppendLine();
        builder.AppendLine(FormatHexDump(bytes, maxBytes: 64 * 1024));
        return builder.ToString().TrimEnd();
    }

    private static string FormatHexDump(byte[] bytes, int maxBytes)
    {
        if (bytes.Length == 0)
        {
            return "(empty)";
        }

        var take = Math.Min(bytes.Length, maxBytes);
        var builder = new StringBuilder(take * 4);
        for (var offset = 0; offset < take; offset += 16)
        {
            var lineCount = Math.Min(16, take - offset);
            builder.Append(offset.ToString("X8", CultureInfo.InvariantCulture));
            builder.Append("  ");
            for (var i = 0; i < 16; i++)
            {
                if (i < lineCount)
                {
                    builder.Append(bytes[offset + i].ToString("X2", CultureInfo.InvariantCulture));
                    builder.Append(' ');
                }
                else
                {
                    builder.Append("   ");
                }
            }

            builder.Append(' ');
            for (var i = 0; i < lineCount; i++)
            {
                var value = bytes[offset + i];
                builder.Append(value is >= 0x20 and <= 0x7E ? (char)value : '.');
            }

            builder.AppendLine();
        }

        if (take < bytes.Length)
        {
            builder.AppendLine($"... truncated: shown {take:N0} / {bytes.Length:N0} bytes，避免 UI 卡顿。");
        }

        return builder.ToString().TrimEnd();
    }

    private static string NormalizeDirection(string direction)
    {
        return direction.Equals("C->S", StringComparison.OrdinalIgnoreCase) ? "TCP 发送" :
            direction.Equals("S->C", StringComparison.OrdinalIgnoreCase) ? "TCP 接收" :
            string.IsNullOrWhiteSpace(direction) ? "-" : direction;
    }

    private static string BuildOnlinePacketEndpoint(CapturedPacketPreview packet, PacketDecodePreview decode)
    {
        if (!string.IsNullOrWhiteSpace(packet.Endpoint) &&
            !packet.Endpoint.Contains("Network.dll Unpack", StringComparison.OrdinalIgnoreCase) &&
            !packet.Endpoint.Contains("Network.dll Pack", StringComparison.OrdinalIgnoreCase))
        {
            return packet.Endpoint;
        }

        var endpoint = ExtractEndpoint(packet.Summary, "");
        if (!string.IsNullOrWhiteSpace(endpoint))
        {
            return endpoint;
        }

        return packet.ConnectionKey.Equals("internal-hook", StringComparison.OrdinalIgnoreCase)
            ? "内部Hook明文（无TCP IP端点）"
            : string.IsNullOrWhiteSpace(decode.Detail) ? "-" : decode.Detail;
    }

    private static string ExtractEndpoint(string packetSummary, string fallback)
    {
        var tcpIndex = packetSummary.IndexOf("TCP ", StringComparison.OrdinalIgnoreCase);
        if (tcpIndex >= 0)
        {
            var payloadIndex = packetSummary.IndexOf(" payload=", tcpIndex, StringComparison.OrdinalIgnoreCase);
            if (payloadIndex > tcpIndex)
            {
                return packetSummary[(tcpIndex + 4)..payloadIndex].Trim();
            }
        }

        return fallback;
    }

    private void RefreshWiresharkPlanText()
    {
        WiresharkPlanText = BuildWiresharkPlanText(CppBackendStatus);
    }

    private static string BuildDefaultWiresharkPlanText()
    {
        return BuildWiresharkPlanText("在线后端：key_probe.dll Hook + 内部Hook明文；接收 Unpack / 发送 Pack。");
    }

    private static string BuildWiresharkPlanText(string cppBackendStatus)
    {
        return
            "在线抓包解密页面 v1.5\r\n" +
            "1. 主流程：先启动内部Hook后端 -> 启动登陆器 -> 等新 GameApp.exe -> 注入自写 key_probe.dll -> hook Network.dll Unpack/Pack；Top-Kart.dll 匹配时额外 hook RequestGameServer source object -> UI 实时显示明文/源对象。\r\n" +
            "2. 默认注入时机对齐 PacketDumpLauncher：新 GameApp.exe 出现立即注入；需要旧兼容时可切到等待 Network.dll 后注入。\r\n" +
            "3. 旧离线自动提 key 入口已移除；Session Key 只来自自写 Hook 或手动调试输入。\r\n" +
            $"4. online backend: {cppBackendStatus}\r\n" +
            "5. 调试口径：Session Key 在 Key 面板显示/复制/保存；状态区只显示来源、长度和校验状态。\r\n" +
            "6. 结构体解析跟随上方选中的包；28346 raw wrapper 只提示容器，inner 明文才进入 ParserBridge。\r\n";
    }

    private static string? GetExistingDirectoryFromFile(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return null;
        }

        var directory = File.Exists(path) ? Path.GetDirectoryName(path) : path;
        return !string.IsNullOrWhiteSpace(directory) && Directory.Exists(directory) ? directory : null;
    }

    private void ResetOnlinePacketPreviewRows()
    {
        WiresharkPacketRows.Clear();
        TopKartPacketRows.Clear();
        SelectedTopKartPacketText = "Top-Kart 包会显示在这里；点击右侧 Top 包后，下方 HEX/Structure 跟随刷新。";
        _onlineStructureSelectionKey = "";
        OnlineStructureNodes.Clear();
        WiresharkPacketRows.Add(new WiresharkPacketPreviewRow
        {
            PacketIndex = "-",
            Direction = "-",
            MsgId = "-",
            MsgType = "-",
            MsgSeq = "-",
            BodyLen = "-",
            Length = "-",
            Function = "等待在线抓包"
        });
        TopKartPacketRows.Add(new WiresharkPacketPreviewRow
        {
            PacketIndex = "-",
            Direction = "-",
            MsgId = "-",
            MsgType = "-",
            MsgSeq = "-",
            BodyLen = "-",
            Length = "-",
            Function = "等待 Top-Kart Hook 包"
        });
    }

    private async Task ScanAsync()
    {
        try
        {
            StatusText = "正在扫描 dump 目录...";
            var result = await Task.Run(() => _scanner.Scan(DumpDirectory));

            PayloadDirectoryPath = result.PayloadDirectoryPath;
            LogStatusText = result.LogStatusText;

            _allPackets.Clear();
            _allPackets.AddRange(result.Packets);

            DirectionOptions.Clear();
            DirectionOptions.Add("全部");
            foreach (var direction in _allPackets.Select(packet => packet.Direction).Distinct(StringComparer.OrdinalIgnoreCase).Order())
            {
                DirectionOptions.Add(direction);
            }

            if (!DirectionOptions.Contains(SelectedDirection))
            {
                SelectedDirection = "全部";
            }

            ApplyFilters();
            SelectedPacket = Packets.FirstOrDefault();

            StatusText = result.PayloadDirectoryExists
                ? $"已加载 {_allPackets.Count:N0} 个 payload；当前筛选共 {Packets.Count:N0} 条。"
                : "未找到 payloads 目录。";
        }
        catch (Exception ex)
        {
            _allPackets.Clear();
            Packets.Clear();
            HexRows.Clear();
            StructureNodes.Clear();
            SelectedPacket = null;
            PayloadDirectoryPath = "";
            LogStatusText = "trace: - | dump: - | parser_state: -";
            StatusText = ex.Message;
        }
        finally
        {
            RaiseCommandStates();
        }
    }

    private void ApplyFilters()
    {
        var packets = _allPackets.AsEnumerable();

        if (int.TryParse(MsgIdFilter.Trim(), out var msgId))
        {
            packets = packets.Where(packet => packet.MsgId == msgId);
        }

        if (!string.IsNullOrWhiteSpace(SelectedDirection) && SelectedDirection != "全部")
        {
            packets = packets.Where(packet => string.Equals(packet.Direction, SelectedDirection, StringComparison.OrdinalIgnoreCase));
        }

        if (long.TryParse(MinLengthFilter.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var minLength))
        {
            packets = packets.Where(packet => packet.DeclaredLength >= minLength);
        }

        var search = SearchText.Trim();
        if (!string.IsNullOrWhiteSpace(search))
        {
            packets = packets.Where(packet =>
                packet.FileName.Contains(search, StringComparison.OrdinalIgnoreCase) ||
                packet.Direction.Contains(search, StringComparison.OrdinalIgnoreCase) ||
                packet.SeqDisplay.Contains(search, StringComparison.OrdinalIgnoreCase) ||
                packet.MsgIdDisplay.Contains(search, StringComparison.OrdinalIgnoreCase));
        }

        var filtered = packets.ToList();
        Packets.Clear();
        foreach (var packet in filtered)
        {
            Packets.Add(packet);
        }

        if (SelectedPacket is not null && !Packets.Contains(SelectedPacket))
        {
            SelectedPacket = Packets.FirstOrDefault();
        }

        StatusText = $"已加载 {_allPackets.Count:N0} 个 payload；当前筛选共 {Packets.Count:N0} 条。";
    }

    private async Task LoadSelectedPacketAsync(long requestedOffset)
    {
        HexRows.Clear();
        CurrentRangeText = "当前页: -";

        var packet = SelectedPacket;
        if (packet is null)
        {
            return;
        }

        try
        {
            if (!File.Exists(packet.FullPath))
            {
                StatusText = $"payload 文件不存在: {packet.FullPath}";
                return;
            }

            var fileLength = new FileInfo(packet.FullPath).Length;
            var offset = ClampOffset(requestedOffset, fileLength);
            var bytes = await _payloadReadService.ReadPageAsync(packet.FullPath, offset, PageSize);
            foreach (var row in _hexViewService.BuildRows(bytes, offset))
            {
                HexRows.Add(row);
            }

            CurrentOffset = offset;
            CurrentRangeText = bytes.Length == 0
                ? "当前页: empty"
                : $"当前页: 0x{offset:X}-0x{offset + bytes.Length - 1:X}";
            JumpOffsetText = $"0x{offset:X}";
            StatusText = $"已打开 {packet.FileName}；读取当前页 {bytes.Length:N0} bytes。";
        }
        catch (Exception ex)
        {
            StatusText = ex.Message;
        }
        finally
        {
            RaiseCommandStates();
        }
    }

    private async Task PreviousPageAsync()
    {
        await LoadSelectedPacketAsync(Math.Max(0, CurrentOffset - PageSize));
    }

    private async Task NextPageAsync()
    {
        await LoadSelectedPacketAsync(CurrentOffset + PageSize);
    }

    private async Task JumpOffsetAsync()
    {
        if (!TryParseOffset(JumpOffsetText, out var offset))
        {
            StatusText = "跳转 Offset 格式错误，支持十进制或 0x 十六进制。";
            return;
        }

        await LoadSelectedPacketAsync(offset);
    }

    private static long ClampOffset(long offset, long fileLength)
    {
        if (fileLength <= 0)
        {
            return 0;
        }

        if (offset < 0)
        {
            return 0;
        }

        if (offset >= fileLength)
        {
            return ((fileLength - 1) / PageSize) * PageSize;
        }

        return offset;
    }

    private static bool TryParseOffset(string text, out long offset)
    {
        text = text.Trim();
        if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            return long.TryParse(text[2..], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out offset);
        }

        return long.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out offset);
    }

    private void OpenPayloadDirectory()
    {
        if (!Directory.Exists(PayloadDirectoryPath))
        {
            StatusText = "payloads 目录不存在。";
            return;
        }

        Process.Start(new ProcessStartInfo(PayloadDirectoryPath) { UseShellExecute = true });
    }

    private void CopySelectedFilePath()
    {
        if (SelectedPacket is null)
        {
            return;
        }

        Clipboard.SetText(SelectedPacket.FullPath);
        StatusText = "已复制当前 payload 文件路径。";
    }

    private async Task RefreshParserAsync(bool compareSiblings)
    {
        var packet = SelectedPacket;
        if (packet is null)
        {
            ApplyParserStatus(_parserBridge.GetStatus(null));
            return;
        }

        ParserStatusText = compareSiblings ? "comparing..." : "parsing...";
        ParserNoteText = compareSiblings ? "正在对照同级 packet_dumps_* 样本..." : "正在解析当前 payload...";

        ParserStatus status;
        try
        {
            status = await Task.Run(() => _parserBridge.GetStatus(packet, compareSiblings));
        }
        catch (Exception ex)
        {
            status = new ParserStatus
            {
                Status = "parser error",
                Note = ex.Message,
                StructureText = "Parser failed.",
                RootNodes =
                [
                    new ParsedFieldNode
                    {
                        Offset = 0,
                        Size = 0,
                        Type = "error",
                        Name = "ParserError",
                        Value = ex.GetType().Name,
                        Status = "Unaligned",
                        Note = ex.Message
                    }
                ]
            };
        }

        ApplyParserStatus(status);
    }

    private void ApplyParserStatus(ParserStatus status)
    {
        ParserStatusText = status.Status;
        ParserNoteText = status.Note;
        StructureText = status.StructureText;
        var expandedKeys = CaptureExpandedNodeKeys(StructureNodes);
        StructureNodes.Clear();
        foreach (var node in status.RootNodes)
        {
            RestoreExpandedNodeKeys(node, expandedKeys);
            StructureNodes.Add(node);
        }
    }

    private void RaiseCommandStates()
    {
        RescanCommand.RaiseCanExecuteChanged();
        OpenPayloadDirectoryCommand.RaiseCanExecuteChanged();
        CopyFilePathCommand.RaiseCanExecuteChanged();
        PreviousPageCommand.RaiseCanExecuteChanged();
        NextPageCommand.RaiseCanExecuteChanged();
        JumpOffsetCommand.RaiseCanExecuteChanged();
        CompareSiblingDumpsCommand.RaiseCanExecuteChanged();
    }
}

public sealed class WiresharkPacketPreviewRow
{
    public CapturedPacketPreview? Packet { get; init; }
    public bool IsStructureCandidate { get; init; }
    public int PacketIndexNumber { get; init; }
    public string PacketIndex { get; init; } = "";
    public string Direction { get; init; } = "";
    public string MsgId { get; init; } = "";
    public int? EffectiveMsgId { get; init; }
    public int? OuterMsgId { get; init; }
    public int? InnerMsgId { get; init; }
    public string WrapperKind { get; init; } = "";
    public string MsgType { get; init; } = "";
    public string MsgSeq { get; init; } = "";
    public string BodyLen { get; init; } = "";
    public string Length { get; init; } = "";
    public string Function { get; init; } = "";
}
