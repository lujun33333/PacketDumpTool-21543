# Offline Embedded Runtime Material Implementation Plan

> **For agentic workers:** Execute inline in this task; subagent dispatch is not permitted for this workspace. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove `LauncherRuntimeKeyring.bin` and derive the existing `msgId=3` content key from a release-specific master embedded in `SpeedServer.exe`.

**Architecture:** The existing Node native-build orchestrator owns the random release material in memory, renders one temporary masked `.inc`, builds a matched staged server executable and encrypted Launcher runtime bundle, then deletes the include. No generated C++/H file is retained.

**Tech Stack:** Node.js 20 crypto, C++17, Windows CNG/BCrypt, MSBuild v143, Node test runner.

## Global Constraints

- Do not add VMProtect to SpeedServer in this phase.
- Do not change GameKey, Hook endpoint locking, Loader Ready, or the wire format.
- Do not stop or replace the currently running server without separate approval.
- Do not create a runtime keyring or build-secret JSON.
- Keep active artifacts unchanged until a matched staged pair verifies.

---

### Task 1: Cross-language HKDF contract

**Files:**
- Create: `LauncherElectron/src/scripts/offline-release-material.cjs`
- Create: `LauncherElectron/test/unit/offline-release-material.test.js`
- Modify: `server/Server/LauncherRuntimeMaterial.h`
- Modify: `server/Server/LauncherRuntimeMaterial.cpp`
- Modify: `server/Server/LauncherRuntimeMaterialTests.cpp`

**Interfaces:**
- Node produces `deriveContentKey(releaseMaster, bundleId): Buffer`.
- C++ produces `DeriveContentKey(releaseMaster, bundleId, contentKey): bool`.

- [ ] Add the canonical vector test expecting `c1db7ad17a09e1cea10019adc1eeba63b4a1b20f5516cf5add6f50811a4ba7d6`.
- [ ] Run the focused Node/C++ tests and observe failure because HKDF APIs are absent.
- [ ] Implement Extract/Expand with HMAC-SHA256 and exact ASCII info bytes.
- [ ] Run focused tests and confirm both implementations match.

### Task 2: Embedded release provider and server state

**Files:**
- Modify: `server/Server/LauncherRuntimeMaterial.h`
- Modify: `server/Server/LauncherRuntimeMaterial.cpp`
- Modify: `server/Server/LauncherRuntimeMaterialTests.cpp`
- Modify: `server/Server/UserServer.cpp`

**Interfaces:**
- Provider: `LoadEmbeddedReleaseMaterial(bundleId, releaseMaster): bool`.
- Service: `ConfigureFromEmbeddedRelease(error): bool`.

- [ ] Add tests proving embedded configuration succeeds, derives the expected key, rejects a changed bundle ID, and clears state.
- [ ] Run the server test and observe failure because the embedded provider path is absent.
- [ ] Replace stored `contentKey_` with `releaseMaster_`; derive a local content key inside `Issue()` and clear it on every path.
- [ ] Remove `RuntimeMaterialKeyringPath`, `ConfigureFromDpapiFile`, filesystem/DPAPI parsing, and the `Config/LauncherRuntimeKeyring.bin` dependency.
- [ ] Run the server test and confirm the existing RSA-OAEP envelope remains compatible.

### Task 3: Memory-only release material generator

**Files:**
- Modify: `LauncherElectron/src/scripts/offline-release-material.cjs`
- Modify: `LauncherElectron/test/unit/offline-release-material.test.js`

**Interfaces:**
- `generateReleaseMaterial(randomBytesFn)` returns owned 32-byte master/id buffers.
- `renderEmbeddedReleaseSource(material, randomBytesFn)` returns one `.inc` fragment without containing the contiguous master.

- [ ] Add tests for exact lengths, non-zero random material, deterministic injected RNG, three-share reconstruction, and absence of raw master hex/text.
- [ ] Run tests and observe the renderer/generator failures.
- [x] Implement `LoadEmbeddedReleaseMaterial` in the existing `.cpp` with volatile masked shares and transient reconstruction.
- [ ] Run tests and clear all test-owned secret buffers.

### Task 4: Bundle builder without secret JSON

**Files:**
- Modify: `LauncherElectron/src/scripts/build-runtime-bundle.cjs`
- Modify: `LauncherElectron/src/scripts/build-native.cjs`
- Modify: `LauncherElectron/test/unit/native-runtime-bundle-build.test.js`
- Remove: `LauncherElectron/src/scripts/provision-server-runtime-keyring.cjs`
- Replace: `LauncherElectron/test/unit/server-keyring-provision.test.js` with a no-keyring contract test.

**Interfaces:**
- Bundle builds accept only in-memory `bundleId` and `contentKey` buffers.
- Standalone verification inspects the authenticated header structure and pinned Hook identity without loading a secret file.

- [ ] Add tests rejecting missing in-memory material and all secret-file/environment inputs.
- [ ] Run tests and observe current JSON loader behavior failing the new contract.
- [ ] Remove `QQS_RUNTIME_BUNDLE_SECRET_FILE`, `loadBuildSecret`, and server keyring provisioning.
- [ ] Keep full decrypt-and-byte verification in the in-process build call while the derived key is available.
- [ ] Run all Launcher unit tests.

### Task 5: Matched staged release orchestration

**Files:**
- Modify: `LauncherElectron/src/scripts/build-native.cjs`
- Modify: `LauncherElectron/test/unit/native-build-memory-only.test.js`
- Modify: `LauncherElectron/package.json`
- Modify: `server/Server/SpeedServer.vcxproj`

**Interfaces:**
- Command: `npm run build:native`.
- Environment may specify only paths/output locations, never secret bytes.
- Output: staged `SpeedServer.exe` and matched Launcher `qqspeed-runtime.bundle`.

- [ ] Add a dry-run test asserting the MSBuild property points to a temporary generated source and no secret appears in arguments/output.
- [ ] Run the test and observe failure because the orchestrator does not exist.
- [x] Implement in-process generation, temporary-include cleanup, native bundle build, server MSBuild, and staging publication.
- [x] Add the conditional generated include directory to `SpeedServer.vcxproj`; non-release/test builds retain the unavailable fallback.
- [x] Run dry-run, then a real staged Release x64 build.

### Task 6: Verification and delivery audit

**Files:**
- Modify only files required by failures discovered in this feature.

- [ ] Run the focused C++ material test.
- [ ] Run all Launcher unit tests and `npm run check`.
- [ ] Build a staged pair and verify matching bundle ID/hashes.
- [ ] Search source and staged delivery for `LauncherRuntimeKeyring.bin`, DPAPI keyring parsing, build-secret JSON, or leaked master/content key values.
- [ ] Confirm the active running server and active Launcher bundle were not replaced.
- [ ] Record exact changed files, verification results, deployment steps, and remaining real-login validation.
