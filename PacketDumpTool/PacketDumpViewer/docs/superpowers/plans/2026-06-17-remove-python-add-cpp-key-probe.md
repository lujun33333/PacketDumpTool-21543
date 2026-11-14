# Remove Python and Add C++ KeyProbe Hook Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** PacketDumpViewer online decrypt must stop depending on Python/SAV/pcap key extraction and use a first-party C++ hook module path instead.

**Architecture:** WPF remains the single EXE UI. Online decrypt uses C# TEA/decode and a native `key_probe.dll` boundary for runtime key acquisition. This pass creates the C++ KeyProbe scaffold and removes Python-facing UI/code; actual Network.dll offset hook can be filled after runtime offset validation.

**Tech Stack:** C# WPF .NET 8, C++17 MSVC x64 DLL, C ABI boundary, Windows native module copy-to-output.

---

### Task 1: Remove Python/SAV/pcap UI and ViewModel paths

**Files:**
- Modify: `F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\src\PacketDumpViewer.App\MainWindow.xaml`
- Modify: `F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\src\PacketDumpViewer.App\ViewModels\MainViewModel.cs`

- [ ] Replace `Key / 离线调试入口` with `KeyProbe / 自写Hook`.
- [ ] Remove `SAV`, `pcap 登录响应自动提取`, `PythonDecoderDirectory`, `StartWiresharkPythonDecrypt`, `OpenPythonDecoderDirectory`, and Python process execution.
- [ ] Keep manual `Session Key Hex` input only as debugging fallback.

### Task 2: Add native C++ key_probe.dll scaffold

**Files:**
- Create: `F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\native\key_probe\key_probe.cpp`
- Create: `F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\native\key_probe\key_probe.vcxproj`

- [ ] Export stable C ABI functions:
  - `KeyProbe_Version`
  - `KeyProbe_GetStatus`
  - `KeyProbe_SetSessionKeyForTest`
  - `KeyProbe_GetSessionKey`
  - `KeyProbe_ClearSessionKey`
- [ ] Keep no STL in exported ABI.
- [ ] Do not hardcode unverified Network.dll offsets as authoritative.

### Task 3: Wire C# native module status to self-written hook

**Files:**
- Modify: `F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\src\PacketDumpViewer.App\Services\KeyProbeManager.cs`
- Modify: `F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\src\PacketDumpViewer.App\Services\OnlineCaptureService.cs`
- Modify: `F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\src\PacketDumpViewer.App\PacketDumpViewer.App.csproj`

- [ ] Make status say `自写Hook DLL 就绪/缺失`.
- [ ] Copy `native\key_probe.dll` into WPF output when present.
- [ ] Keep UI key display and C# decrypt path.

### Task 4: Verify

**Commands:**
- Build WPF: `dotnet build F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\PacketDumpViewer.sln -c Release`
- Build native DLL if MSBuild exists: `MSBuild.exe F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\native\key_probe\key_probe.vcxproj /p:Configuration=Release /p:Platform=x64`
- Launch EXE smoke: start and verify process stays running.
