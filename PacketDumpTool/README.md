# PacketDumpTool

## Purpose

`PacketDumpLauncher.exe` starts an external launcher, waits for a new target process such as `GameApp.exe`, then injects `PacketDumpHook.dll` after the target loads `Network.dll`.

This tool only dumps packet data. It does not redirect `connect`, hook `COPYDATA`, or force login logic.

## Output

Build outputs:

```powershell
E:\src\tmp\PacketDumpTool\x64\Release\PacketDumpLauncher.exe
E:\src\tmp\PacketDumpTool\x64\Release\PacketDumpHook.dll
```

Runtime files are written under the injected DLL directory, grouped by process:

```text
packet_dumps_<pid>\
  PacketDumpHook_<pid>.log
  network_trace_<pid>.log
  network_dump_<pid>.log
  payloads\
    network_bin_<pid>_<seq>_<dir>_msg<id>_len<len>.bin
```

Binary payload dumps intentionally live in `payloads\` so they do not mix with the root release directory or the text trace files.

## Usage

```powershell
cd E:\src\tmp\PacketDumpTool\x64\Release
.\PacketDumpLauncher.exe --launcher "D:\OtherLauncher\Launcher.exe"
```

Common options:

```text
--launcher <path>   Required external launcher path
--args <text>       Full command-line arguments passed to the launcher
--workdir <path>    Launcher working directory; default is launcher exe directory
--process <name>    Target process name; default GameApp.exe
--module <name>     Module to wait for; default Network.dll
--dll <path>        DLL to inject; default PacketDumpHook.dll next to this exe
--timeout <ms>      Wait timeout; default 120000
--poll <ms>         Poll interval; default 250
```

## Current Network.dll

The active hook build is aligned to the current runtime Network.dll:

```text
Path: F:\QQ飞车\Releasephysx27_64\Network.dll
MD5:  0679c6a324679bbe967e7e8771cf2dc3
SizeOfImage: 0x846000
```

Previous MD5s `164213ab8c4b6def1bac8c441d2b1b50`,
`ce97d03ca74b530bbbdafb069ca2b3b8`,
`8fd89fce68cea837d7047f373f355857`,
`fe179ea08c8c9d1d543e4775b0a3c1a5`, and legacy MD5s
`3805d35c45d14bb70fef5abb0722aa0c`,
`bd24352812012d52249365d5fce5d64e`, and
`7707ae852fb354782294d3c09b8d5d33` remain in `network_dump.cpp`
for compatibility and reference.

Key dump directions include:

```text
recv_raw_packet
recv_plain_full
recv_plain_body
special28346_before_full
special28346_after_full
inner_dispatch_body
inner_dispatch_mode0_fullmsg_body_minus14
pack_before_body
pack_after_packet
safe28346_builder_payload_full
safe28346_builder_inner_body_at12
serialize_body_plain
serialize_body_28346_inner_body_at12
login_cache_decode_before
login_cache_decode_after
```

For msg `28346` construction, start with:

```text
safe28346_builder_payload_full
serialize_body_plain
pack_after_packet
special28346_before_full
inner_dispatch_mode0_fullmsg_body_minus14
```

For login msg `100` state comparison, prefer the cache transform dumps over raw
`inner_dispatch_out_full_msg100`:

```text
login_cache_decode_before   encoded cache before native 0x3AFE80 decoding
login_cache_decode_after    decoded cache consumed by login initialization
```

Treasure-box responses `168` and `190` are also explicitly dumped at the
working parser-queue interception point. This provides
`parser_queue_dispatch_body_msg168` / `msg190` as a fallback if a future
Network.dll update moves the direct inner-dispatch call sites again.

The decode hook is installed by patching the two direct login-initialization call
sites, not by patching the `0x3AFE80` function entry. Each direction is limited
to the first few calls per process.

## Build

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' E:\src\tmp\PacketDumpTool\PacketDumpTool.sln /p:Configuration=Release /p:Platform=x64 /m
```
