# PacketDumpViewer v1

C# WPF + .NET 8 local dump folder viewer for `packet_dumps_<pid>` output.

## Scope

- Read-only viewer.
- Does not load `PacketDumpHook.dll`.
- Does not inject into the game process.
- Does not modify `PacketDumpTool.sln`, `network_dump.cpp`, or existing dump logic.
- v1 indexes payload file names and shows paged hex content.
- v1.1 adds an offline MsgID 100 full-payload structure tree and sibling dump comparison.

## Input

Select a dump folder such as:

```text
F:\PacketDumpTool\PacketDumpTool\x64\Release\packet_dumps_28908
```

Expected payload pattern:

```text
payloads\network_bin_<pid>_<seq>_<direction>_msg<msgId>_len<len>.bin
```

`direction` may contain underscores, for example:

```text
recv_plain_body
special28346_after_inner_body_at12
```

## Build

```powershell
& 'C:\Program Files\dotnet\dotnet.exe' build .\PacketDumpViewer.sln -c Release
```

Output:

```text
src\PacketDumpViewer.App\bin\Release\net8.0-windows\PacketDumpViewer.exe
```

## MsgID 100 parser

The first structured parser targets current-3805 full payloads:

```text
*_msg100_len317329.bin
```

Supported primary directions:

```text
recv_plain_full
special28346_after_full
login_cache_decode_before
login_cache_decode_after
```

The structure tree uses full offsets. For body-slice dumps, convert offsets as:

```text
body offset = full offset - 0x0E
```

Fields that cannot be named or aligned safely are kept as `UnknownName`, `Unaligned`, or `Reserved`.

## Direction labels

Packet list direction rows are bilingual and include a flow hint:

- `recv_*`, `inner_dispatch_*`, `special28346_after_*`, `login_cache_decode_*`: usually `S→C 响应/通知`.
- `serialize_*`, `pack_*`, `safe28346_builder_*`: usually `C→S 请求/发送`.

These labels describe the dump stage, not a guaranteed protocol-level request/response semantic.
