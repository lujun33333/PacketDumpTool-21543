# Embedded native runtime design

## Goal

Ship `QQSpeedNativeBridge.dll` and `qqspeed_clean_hook_64.dll` without loose
copies in the installed `resources` tree, while preserving the proven Koffi +
`LoadLibraryW` launch path.

## Delivery contract

1. `qqspeed_clean_hook_64.dll` is compiled into `QQSpeedNativeBridge.dll` as an
   `RCDATA` resource.
2. The resulting bridge is compressed and authenticated with AES-256-GCM in a
   single `qqspeed-runtime.bundle` stored inside `app.asar`.
3. `extraResources` does not install either DLL and the packaged ASAR exposes no
   DLL entry.
4. The client-side encryption key is an extraction barrier only. It is not a
   secret against a local debugger or memory capture.
5. Native compilation writes the bridge only to the ignored MSBuild staging
   directory. The same `build:native` command verifies it, creates the bundle,
   and removes the staging DLL in `finally`; `resources/native` retains no
   loose bridge or legacy artifact manifest.

## Runtime contract

1. Main verifies and decrypts the bundle into a CSPRNG-named session directory
   under `%LOCALAPPDATA%\QQSpeedLauncher\runtime\sessions`.
2. Only `QQSpeedNativeBridge.dll`, the bridge-created Hook, and a strict lease
   file are allowed in a session directory.
3. Bridge extracts its embedded Hook with exclusive creation, flushes it,
   compares the persisted bytes with the resource, and holds a handle that
   denies write/delete sharing during the native session.
4. Existing remote `LoadLibraryW` injection and replacement-GameApp handling
   remain unchanged.

Windows still requires real files while Koffi and `LoadLibraryW` load the DLLs;
the design removes install-time plaintext exposure and bounds runtime residue,
but does not claim diskless loading.

## Lifecycle

- Failure, cancellation, and pre-game exits clean after the utility process
  unloads the bridge.
- Ready sessions keep their files until both utility and GameApp have exited.
- Closing the UI while GameApp runs starts a hidden detached, allowlist-only
  waiter which cleans after that exact process exits; forced termination or a
  missing PowerShell runtime is recovered by the next-start scavenger.
- Startup scavenging examines only first-level, marker-bearing, non-reparse
  session directories and deletes only the fixed allowlist. Unknown content is
  retained and logged rather than recursively removed.
- The uninstaller removes only native runtime roots, not account metadata or the
  DPAPI credential vault.

## Verification gates

- Unit tests cover container round-trip/tampering/limits and deletion bounds.
- Native build verification proves the RCDATA bytes equal the source Hook.
- Package scans prove there is no loose Native directory or DLL ASAR entry.
- Existing native boundary, credential, renderer, packaged, portable, Hook,
  login, `StartGame`, replacement-process, and ready-state smokes must remain
  green.
