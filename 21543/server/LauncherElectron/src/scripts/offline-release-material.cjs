'use strict';

const { hkdfSync, randomBytes } = require('node:crypto');

const RELEASE_MATERIAL_BYTES = 32;
const HKDF_INFO = Buffer.from('QQS-RUNTIME-V1', 'ascii');

function copyExactNonZero(value, label) {
  if (!Buffer.isBuffer(value) || value.length !== RELEASE_MATERIAL_BYTES) {
    throw new Error(`${label} must be a 32-byte Buffer.`);
  }
  const copy = Buffer.from(value);
  let aggregate = 0;
  for (const byte of copy) aggregate |= byte;
  if (aggregate === 0) {
    copy.fill(0);
    throw new Error(`${label} must not be all zero.`);
  }
  return copy;
}

function deriveContentKey(releaseMasterInput, bundleIdInput) {
  let releaseMaster;
  let bundleId;
  try {
    releaseMaster = copyExactNonZero(releaseMasterInput, 'Release master');
    bundleId = copyExactNonZero(bundleIdInput, 'Bundle id');
    return Buffer.from(hkdfSync('sha256', releaseMaster, bundleId, HKDF_INFO, RELEASE_MATERIAL_BYTES));
  } finally {
    releaseMaster?.fill(0);
    bundleId?.fill(0);
  }
}

function generateReleaseMaterial(randomBytesFn = randomBytes) {
  const releaseMaster = copyExactNonZero(
    randomBytesFn(RELEASE_MATERIAL_BYTES),
    'Generated release master'
  );
  let bundleId;
  try {
    bundleId = copyExactNonZero(
      randomBytesFn(RELEASE_MATERIAL_BYTES),
      'Generated bundle id'
    );
    if (releaseMaster.equals(bundleId)) {
      throw new Error('Generated release master and bundle id must be independent.');
    }
    return { releaseMaster, bundleId };
  } catch (error) {
    releaseMaster.fill(0);
    bundleId?.fill(0);
    throw error;
  }
}

function isAllZero(value) {
  let aggregate = 0;
  for (const byte of value) aggregate |= byte;
  return aggregate === 0;
}

function splitReleaseMaster(releaseMasterInput, randomBytesFn = randomBytes) {
  let releaseMaster;
  let shareA;
  let shareB;
  let shareC;
  let completed = false;

  try {
    releaseMaster = copyExactNonZero(releaseMasterInput, 'Release master');
    shareA = copyExactNonZero(
      randomBytesFn(RELEASE_MATERIAL_BYTES),
      'Release master share A'
    );
    shareB = copyExactNonZero(
      randomBytesFn(RELEASE_MATERIAL_BYTES),
      'Release master share B'
    );
    shareC = Buffer.alloc(RELEASE_MATERIAL_BYTES);
    // Avoid emitting an unmasked master when a deterministic/test RNG returns
    // identical masks. Real release builds use crypto.randomBytes.
    if (shareA.equals(releaseMaster)) shareA[0] ^= 0xa5;
    if (shareB.equals(releaseMaster) || shareB.equals(shareA)) shareB[0] ^= 0x5a;

    for (let attempt = 0; attempt < RELEASE_MATERIAL_BYTES; attempt += 1) {
      for (let index = 0; index < RELEASE_MATERIAL_BYTES; index += 1) {
        shareC[index] = releaseMaster[index] ^ shareA[index] ^ shareB[index];
      }
      if (!isAllZero(shareC) && !shareC.equals(releaseMaster)) {
        completed = true;
        return [shareA, shareB, shareC];
      }
      shareB[attempt] ^= (attempt + 1);
    }
    throw new Error('Unable to mask the release master.');
  } finally {
    releaseMaster?.fill(0);
    if (!completed) {
      shareA?.fill(0);
      shareB?.fill(0);
      shareC?.fill(0);
    }
  }
}

function renderByteArray(name, value) {
  const rows = [];
  for (let offset = 0; offset < value.length; offset += 8) {
    rows.push(
      `    ${Array.from(value.subarray(offset, offset + 8), (byte) =>
        `0x${byte.toString(16).padStart(2, '0')}`).join(', ')}`
    );
  }
  return [
    `static volatile const std::uint8_t ${name}[${RELEASE_MATERIAL_BYTES}] = {`,
    rows.join(',\n'),
    '};'
  ].join('\n');
}

function parseRenderedByteArray(source, name, label) {
  const declaration = new RegExp(
    `static\\s+volatile\\s+const\\s+std::uint8_t\\s+${name}\\s*` +
    `\\[${RELEASE_MATERIAL_BYTES}\\]\\s*=\\s*\\{([\\s\\S]*?)\\};`,
    'g'
  );
  const matches = [...source.matchAll(declaration)];
  if (matches.length !== 1) {
    throw new Error(`Embedded release material ${label} is missing or duplicated.`);
  }
  const body = matches[0][1];
  const tokens = [...body.matchAll(/0x([0-9a-f]{2})/gi)];
  const residue = body.replace(/0x[0-9a-f]{2}/gi, '').replace(/[\s,]/g, '');
  if (tokens.length !== RELEASE_MATERIAL_BYTES || residue) {
    throw new Error(`Embedded release material ${label} must contain exactly 32 bytes.`);
  }
  const value = Buffer.from(tokens.map((token) => Number.parseInt(token[1], 16)));
  if (isAllZero(value)) {
    value.fill(0);
    throw new Error(`Embedded release material ${label} must not be all zero.`);
  }
  return value;
}

function parseEmbeddedReleaseSource(source) {
  if (typeof source !== 'string' || !source.trim()) {
    throw new Error('Embedded release material source is required.');
  }
  let bundleId;
  let shareA;
  let shareB;
  let shareC;
  let releaseMaster;
  let completed = false;
  try {
    bundleId = parseRenderedByteArray(source, 'kEmbeddedBundleId', 'bundle id');
    shareA = parseRenderedByteArray(source, 'kReleaseMasterShareA', 'share A');
    shareB = parseRenderedByteArray(source, 'kReleaseMasterShareB', 'share B');
    shareC = parseRenderedByteArray(source, 'kReleaseMasterShareC', 'share C');
    releaseMaster = Buffer.alloc(RELEASE_MATERIAL_BYTES);
    for (let index = 0; index < RELEASE_MATERIAL_BYTES; index += 1) {
      releaseMaster[index] = shareA[index] ^ shareB[index] ^ shareC[index];
    }
    if (isAllZero(releaseMaster)) {
      throw new Error('Embedded release material reconstructs an all-zero release master.');
    }
    completed = true;
    return { releaseMaster, bundleId };
  } finally {
    shareA?.fill(0);
    shareB?.fill(0);
    shareC?.fill(0);
    if (!completed) {
      releaseMaster?.fill(0);
      bundleId?.fill(0);
    }
  }
}

function renderEmbeddedReleaseSource(material, randomBytesFn = randomBytes) {
  let releaseMaster;
  let bundleId;
  let shares;
  try {
    releaseMaster = copyExactNonZero(material?.releaseMaster, 'Release master');
    bundleId = copyExactNonZero(material?.bundleId, 'Bundle id');
    shares = splitReleaseMaster(releaseMaster, randomBytesFn);
    return [
      '// Generated local release material. Do not distribute this source file.',
      renderByteArray('kEmbeddedBundleId', bundleId),
      renderByteArray('kReleaseMasterShareA', shares[0]),
      renderByteArray('kReleaseMasterShareB', shares[1]),
      renderByteArray('kReleaseMasterShareC', shares[2]),
      ''
    ].join('\n\n');
  } finally {
    releaseMaster?.fill(0);
    bundleId?.fill(0);
    if (shares) {
      for (const share of shares) share.fill(0);
    }
  }
}

module.exports = {
  HKDF_INFO,
  RELEASE_MATERIAL_BYTES,
  deriveContentKey,
  generateReleaseMaterial,
  parseEmbeddedReleaseSource,
  renderEmbeddedReleaseSource,
  splitReleaseMaster
};
