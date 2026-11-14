# Top-Kart Optional Hook Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给 `PacketDumpViewer.exe` 的在线抓包链路增加 Top-Kart.dll 可选语义层 Hook 接入口，用来补齐 Network.dll 明文里缺失的上层对象/字段证据。

**Architecture:** 保持一个自写 `key_probe.dll`：已验证的 Network.dll Unpack/Pack Hook 继续作为主链路；Top-Kart.dll 只做可选层，模块不存在、版本不匹配或函数签名未确认时不强行 Hook、不影响抓包。C# UI/服务层只识别新的 Top-Kart stage 并复用现有 Packet/Structure 显示。

**Tech Stack:** C++17/MSVC native DLL、C# WPF/.NET 8、IDA 21273 Top-Kart export、PacketDumpViewer 现有 ParserBridge。

---

### Task 1: Evidence scan for Top-Kart hook points

**Files:**
- Read: `F:\new1\dll\new\21273\ida_export_Top-Kart_20260613_130744\functions.csv`
- Read: `F:\new1\dll\new\21273\ida_export_Top-Kart_20260613_130744\callgraph.csv`
- Read: `F:\new1\dll\new\21273\ida_export_Top-Kart_20260613_130744\pseudoc\*.c`

- [ ] **Step 1: Locate known send/source-building candidates**

Run:

```powershell
rg -n "sub_1807E19B0|1807E19B0|RequestCreateRoom|RequestStartGame|28346|6EBA|CSSafe|MsgID|msgid|m_ast|m_iTaskID" "F:\new1\dll\new\21273\ida_export_Top-Kart_20260613_130744"
```

Expected: one or more candidate functions or no stable candidate. If no stable candidate is found, only implement module/status scaffolding and parser/UI stage recognition.

- [ ] **Step 2: Verify call signature before hooking**

Inspect candidate pseudocode and callgraph. Do not create a detour unless the source buffer pointer and length are both visible and stable. Record the evidence in the final reply.

### Task 2: Add clean optional Top-Kart status path

**Files:**
- Modify: `F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\native\key_probe\key_probe.cpp`

- [ ] **Step 1: Add module detection**

Add `FindLoadedModuleByName(L"Top-Kart.dll")` polling in the existing worker/status flow. It must report one of:

```text
TopKart: module missing
TopKart: module loaded, hook disabled (unverified)
TopKart: hook installed
```

- [ ] **Step 2: Do not force unsafe hooks**

If the Top-Kart RVA/prototype is not verified, leave hook disabled and keep Network hooks alive. No crash, no forced detour.

### Task 3: Add Top-Kart source stage recognition

**Files:**
- Modify: `F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\src\PacketDumpViewer.App\Services\CaptureBackends\InternalHookCaptureBackend.cs`
- Modify: `F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\src\PacketDumpViewer.App\Services\Parsers\MsgRoomFlowParser.cs`
- Modify: `F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\src\PacketDumpViewer.App\Services\OnlineCaptureService.cs`

- [ ] **Step 1: Recognize future Top-Kart payload files**

Treat stages containing `topkart_source_object` as internal plaintext source records. Direction should be derived from `send`/`recv` when present; otherwise keep `unknown`.

- [ ] **Step 2: Reuse existing structure display**

Extend room/parser source-object detection so `topkart_source_object` uses the same safe-source parsing path as `safe28346_builder_src_object` and `serialize_src_object`.

- [ ] **Step 3: Update UI text only where needed**

Status/help text should say: `Network.dll Unpack/Pack + optional Top-Kart semantic source`. Do not add another confusing required selector.

### Task 4: Verification

**Files:**
- Build: `F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\native\key_probe\key_probe.vcxproj`
- Build: `F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\PacketDumpViewer.sln`

- [ ] **Step 1: Native build**

Run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\native\key_probe\key_probe.vcxproj" /p:Configuration=Release /p:Platform=x64 /m:1
```

Expected: `0 Error(s)`.

- [ ] **Step 2: WPF build**

Run:

```powershell
dotnet build "F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\PacketDumpViewer.sln" -c Release
```

Expected: `Build succeeded`.

- [ ] **Step 3: Test project**

Run:

```powershell
dotnet test "F:\PacketDumpTool\PacketDumpTool\PacketDumpViewer\PacketDumpViewer.sln" -c Release --no-build
```

Expected: tests pass or no tests discovered without build failure.

