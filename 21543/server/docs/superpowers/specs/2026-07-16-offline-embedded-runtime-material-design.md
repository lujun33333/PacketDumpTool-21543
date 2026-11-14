# Offline Embedded Runtime Material Design

## Scope

Replace the runtime dependency on `Config/LauncherRuntimeKeyring.bin` with a
release-specific master embedded in `SpeedServer.exe`. Keep the existing
`msgId=3` request/response wire format, RSA-OAEP response binding, 30-second
material TTL, Launcher memory-only bundle decoding, Hook behavior, GameKey
behavior, and endpoint policy unchanged.

VMProtect integration is explicitly out of scope for this phase.

## Release identity and derivation

Each offline release generates two independent 32-byte CSPRNG values:

- `releaseMaster`: secret, embedded only as split/masked shares in the server.
- `bundleId`: public release identity, stored in the runtime bundle header and
  embedded in the server.

The 32-byte bundle encryption key is RFC 5869 HKDF-SHA256:

```text
contentKey = HKDF-SHA256(
  IKM  = releaseMaster,
  salt = bundleId,
  info = ASCII("QQS-RUNTIME-V1"),
  len  = 32
)
```

Canonical test vector:

```text
releaseMaster = 000102030405060708090a0b0c0d0e0f
                101112131415161718191a1b1c1d1e1f
bundleId       = a0a1a2a3a4a5a6a7a8a9aaabacadaeaf
                 b0b1b2b3b4b5b6b7b8b9babbbcbdbebf
contentKey     = c1db7ad17a09e1cea10019adc1eeba63
                 b4a1b20f5516cf5add6f50811a4ba7d6
```

The server rejects a request unless its `bundleId` exactly matches the
embedded `bundleId`; it never derives keys for arbitrary bundle identities.

## Server runtime

In the production embedded path, `RuntimeMaterialService` stores the public
`bundleId` but not a reconstructed master or plaintext `contentKey`. `Issue()`
reconstructs the master into a local buffer, derives the content key, builds
the existing bound RSA-OAEP envelope, then clears the master, HKDF PRK, content
key, plaintext, and ciphertext buffers with `SecureZeroMemory`.

`UserServer.cpp` initializes the service from the embedded provider and no
longer resolves or reads `LauncherRuntimeKeyring.bin`.

## Build flow

A single Node release orchestrator:

1. Generates `releaseMaster` and `bundleId` in memory.
2. Derives `contentKey` and builds/verifies the encrypted Hook bundle.
3. Generates one temporary `LauncherReleaseMaterial.generated.inc` fragment
   containing two random XOR shares plus one derived share for
   `releaseMaster`, and the expected `bundleId`.
4. Builds the existing `LauncherRuntimeMaterial.cpp` with the temporary include
   directory passed through an MSBuild property; no generated `.cpp` or `.h`
   file is added.
5. Deletes the `.inc` and clears all owned secret buffers in a `finally` path.
6. Stages the matched server executable separately and publishes the verified
   runtime bundle to the Launcher resource path. Deployment remains a separate
   synchronized operation.

No runtime keyring or build-secret JSON is created. A non-secret build result
may contain hashes and the public `bundleId`, but it is not part of delivery.

## Failure behavior

- Missing embedded release material: reject `msgId=3` and disconnect using the
  existing failure path.
- Zero master/bundle identity: reject configuration.
- Wrong bundle identity: reject before HKDF.
- HKDF or RSA failure: clear all transient buffers and return no response.
- Partial build: leave the active server and previous Launcher bundle unchanged;
  publication occurs only after the staged server and authenticated bundle
  both verify.

## Verification

- Node and C++ implementations match the canonical HKDF vector.
- Server material unit test decrypts the `msgId=3` envelope and observes the
  HKDF-derived key.
- Wrong bundle ID is rejected.
- Static source checks prove no runtime keyring path/file loader remains.
- Release orchestrator creates no secret JSON or `.bin` keyring.
- Staged `SpeedServer.exe` and the published runtime bundle share the same
  `bundleId`.
