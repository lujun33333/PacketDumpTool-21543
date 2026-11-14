# VMP Hook Endpoint Lock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Subagent execution is disabled for this task.

**Goal:** Make only `ReleaseVmp|x64` enforce the canonical game endpoint and VMProtect integrity, while pinning the protected Hook artifact in the Electron launcher.

**Architecture:** The Hook keeps the existing MFC connection interception, but the protected configuration validates the launcher mapping against an internally reconstructed endpoint and fails closed for redirect candidates. The launcher packages one explicitly supplied VMProtect output and validates it against an ASAR-protected SHA-256 manifest before staging or injection.

**Tech Stack:** MSVC x64 C++/MFC/Winsock, VMProtect SDK/Ultra markers, PowerShell smoke tests, Electron 35, Node.js, electron-builder.

## Global Constraints

- Canonical endpoint: `43.226.61.100:29999`.
- Redirect source allowlist: `113.96.13.205`, `121.14.75.60`, `120.232.19.139`; source ports `10000-10005`.
- Only `ReleaseVmp|x64` enforces the endpoint and calls `VMProtectIsProtected()` / `VMProtectIsValidImageCRC()`.
- `Debug|x64`, `Release|x64`, and `ReleasePlain|x64` retain existing local behavior.
- Electron packaging is validation-only and must never build, protect, find, or copy a Hook from an old source directory.
- No game server code or protocol changes.
- The source directories are not Git repositories; verification snapshots replace commit steps.

---

### Task 1: Add failing Hook policy tests

**Files:**
- Create: `F:\PacketDumpTool\21446\新建文件夹\qqspeed_clean_hook\tests\verify_release_vmp_endpoint_lock.ps1`
- Modify: `F:\PacketDumpTool\21446\新建文件夹\qqspeed_clean_hook\tests\verify_connect_redirect_allowlist.ps1`

**Interfaces:**
- Consumes: current `connect_redirect.cpp`, `vmp_protection.h`, `.vcxproj`, and `.vmp` project.
- Produces: a static policy gate that rejects missing ReleaseVmp-only guards, canonical endpoint checks, fail-closed behavior, or missing Ultra markers.

- [ ] Write a PowerShell test that requires `QQSPEED_VMP_SERVER_LOCK_ENABLED`, encoded canonical octets/port, `VMProtectIsProtected`, `VMProtectIsValidImageCRC`, `WSAEACCES`, and the expected marker names.
- [ ] Require the project matrix to define `QQSPEED_VMP_PROTECTION` only in `ReleaseVmp|x64`.
- [ ] Run:

```powershell
& 'F:\PacketDumpTool\21446\新建文件夹\qqspeed_clean_hook\tests\verify_release_vmp_endpoint_lock.ps1'
```

Expected before implementation: non-zero exit identifying the missing lock policy.

---

### Task 2: Implement ReleaseVmp-only endpoint enforcement

**Files:**
- Modify: `F:\PacketDumpTool\21446\新建文件夹\qqspeed_clean_hook\vmp_protection.h`
- Modify: `F:\PacketDumpTool\21446\新建文件夹\qqspeed_clean_hook\network\connect_redirect.cpp`

**Interfaces:**
- Consumes: `SpeedHookCfg_<pid>` containing `Host[64]` and `Port`.
- Produces: `ValidateProtectedRedirectTarget(const SpeedHookConfig&, char*, size_t, unsigned short&) -> bool` and fail-closed redirect behavior in VMP builds.

- [ ] Add the compile-time configuration gate:

```cpp
#if defined(QQSPEED_VMP_PROTECTION)
#define QQSPEED_VMP_SERVER_LOCK_ENABLED 1
#else
#define QQSPEED_VMP_SERVER_LOCK_ENABLED 0
#endif
```

- [ ] Add `noinline` Ultra-marked functions that reconstruct `43.226.61.100`, validate mapping Host/Port, verify VMProtect/CRC, and build the canonical IPv4 destination.
- [ ] Keep the existing shared-config assignment in non-VMP builds.
- [ ] In the MFC candidate branch, return `0` with `WSAEACCES` when protected validation fails instead of falling through to the original endpoint.
- [ ] Apply the same fail-closed return to the existing generic `connect`, `WSAConnect`, `sendto`, and `WSASendTo` candidate wrappers without installing new broad hooks.
- [ ] Run both Hook policy tests; expected: PASS.

---

### Task 3: Expand VMProtect coverage and build the protected artifact

**Files:**
- Modify: `F:\PacketDumpTool\21446\新建文件夹\qqspeed_clean_hook\x64\ReleaseVmp\qqspeed_clean_hook_64.dll.vmp`
- Output: `F:\PacketDumpTool\21446\新建文件夹\qqspeed_clean_hook\x64\ReleaseVmp\qqspeed_clean_hook_64.vmp.dll`

**Interfaces:**
- Consumes: marker names emitted by Task 2.
- Produces: x64 VMProtect Hook DLL with Ultra coverage on each server-lock decision unit.

- [ ] Add procedures for `Login.Redirect.Lock.Decode`, `Login.Redirect.Lock.ValidateConfig`, `Login.Redirect.Lock.VerifyVmp`, and `Login.Redirect.Lock.Decide`, all with `CompilationType="2"`.
- [ ] Build:

```powershell
msbuild 'F:\PacketDumpTool\21446\新建文件夹\qqspeed_clean_hook\qqspeed_clean_hook.sln' /t:Build /p:Configuration=ReleaseVmp /p:Platform=x64 /m
```

- [ ] Run the installed VMProtect console against the existing `.vmp` project.
- [ ] Verify the input and output are distinct x64 DLLs and record SHA-256, size, and timestamp.
- [ ] Re-run the policy tests against the source and protection project.

---

### Task 4: Pin the formal native runtime in the launcher

**Files:**
- Modify: `F:\PacketDumpTool\21043版本\MysqlCodes-1.5\LauncherElectron\resources\native\runtime-artifacts.json`
- Modify: `F:\PacketDumpTool\21043版本\MysqlCodes-1.5\LauncherElectron\src\main\native-core.js`
- Modify: `F:\PacketDumpTool\21043版本\MysqlCodes-1.5\LauncherElectron\src\scripts\verify-hook.cjs`
- Modify: `F:\PacketDumpTool\21043版本\MysqlCodes-1.5\LauncherElectron\src\scripts\smoke-runtime-artifacts.cjs`
- Modify: `F:\PacketDumpTool\21043版本\MysqlCodes-1.5\LauncherElectron\package.json`

**Interfaces:**
- Consumes: the explicitly selected VMP Hook, NativeBridge DLL, INI, and schema-versioned manifest.
- Produces: immutable expected file descriptors read from `app.asar`, checked before and after LocalAppData staging.

- [ ] Add the exact manifest to Electron `files` so ASAR integrity protects it.
- [ ] Change runtime staging to compare the three native files against the manifest, not against hashes calculated from the source files themselves.
- [ ] Keep prepack/predist validation read-only; reject any mismatch before electron-builder starts.
- [ ] Explicitly place the new VMP output at `resources/native/qqspeed_clean_hook_64.dll` and update the manifest hashes after all native builds finish.
- [ ] Run `npm run check:runtime-artifacts`, mutate-test a temporary copy, and require mismatch rejection.

---

### Task 5: Verify and package

**Files:**
- Output: `F:\PacketDumpTool\21043版本\MysqlCodes-1.5\LauncherElectron\dist\QQSpeedLauncher-Portable-1.0.0-x64.exe`

**Interfaces:**
- Consumes: protected Hook, pinned manifest, built NativeBridge, Electron application.
- Produces: one x64 Portable launcher and an evidence block with hashes.

- [ ] Run `npm run check`, `npm run verify:runtime`, endpoint, credential, lifecycle, renderer, fuse, and release smoke tests.
- [ ] Snapshot the source Hook size/mtime/SHA-256 before packaging.
- [ ] Run `npm run dist:portable`.
- [ ] Confirm the source Hook snapshot is unchanged after packaging.
- [ ] Inspect `dist/win-unpacked/resources/Native` and verify all three files against the pinned manifest.
- [ ] Launch the unpacked app for a smoke test; if game credentials and live server are available, verify initial connection and an internal reconnect. Otherwise report dynamic game validation as unverified rather than claiming success.
