import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
const hookPath = join(root, 'resources', 'native', 'qqspeed_clean_hook_64.dll');

function inspectPe(buffer) {
  assert.equal(buffer.toString('ascii', 0, 2), 'MZ');
  const pe = buffer.readUInt32LE(0x3c);
  assert.equal(buffer.toString('ascii', pe, pe + 4), 'PE\0\0');
  assert.equal(buffer.readUInt16LE(pe + 4), 0x8664);
  const sectionCount = buffer.readUInt16LE(pe + 6);
  const characteristics = buffer.readUInt16LE(pe + 22);
  const optionalSize = buffer.readUInt16LE(pe + 20);
  const optional = pe + 24;
  assert.equal(buffer.readUInt16LE(optional), 0x20b);
  const directory = (index) => ({
    rva: buffer.readUInt32LE(optional + 112 + index * 8),
    size: buffer.readUInt32LE(optional + 116 + index * 8)
  });
  const sections = [];
  let cursor = optional + optionalSize;
  for (let index = 0; index < sectionCount; index += 1, cursor += 40) {
    sections.push({
      name: buffer.toString('ascii', cursor, cursor + 8).replace(/\0+$/, ''),
      virtualSize: buffer.readUInt32LE(cursor + 8),
      rva: buffer.readUInt32LE(cursor + 12),
      rawSize: buffer.readUInt32LE(cursor + 16),
      rawOffset: buffer.readUInt32LE(cursor + 20),
      characteristics: buffer.readUInt32LE(cursor + 36)
    });
  }
  return {
    characteristics,
    entryRva: buffer.readUInt32LE(optional + 16),
    sectionAlignment: buffer.readUInt32LE(optional + 32),
    sizeOfImage: buffer.readUInt32LE(optional + 56),
    sizeOfHeaders: buffer.readUInt32LE(optional + 60),
    numberOfDirectories: buffer.readUInt32LE(optional + 108),
    sections,
    exception: directory(3),
    relocations: directory(5),
    tls: directory(9),
    delayImports: directory(13)
  };
}

test('pinned VMProtect Hook satisfies the remote manual-map PE contract', () => {
  const bytes = readFileSync(hookPath);
  const pe = inspectPe(bytes);

  assert.ok(bytes.length < 128 * 1024 * 1024, 'Hook payload exceeds the bundle/mapper limit');
  assert.ok((pe.characteristics & 0x2000) !== 0, 'Hook input must be a DLL');
  assert.ok(pe.sectionAlignment >= 0x1000);
  assert.ok(pe.sizeOfImage > 0 && pe.sizeOfImage <= 128 * 1024 * 1024);
  assert.ok(pe.sizeOfHeaders <= bytes.length);
  assert.ok(pe.numberOfDirectories >= 16);
  assert.ok(pe.sections.length > 0 && pe.sections.length <= 96);
  for (const section of pe.sections) {
    const mappedSize = Math.max(section.virtualSize, section.rawSize);
    assert.ok(section.rva + mappedSize <= pe.sizeOfImage, `${section.name} exceeds SizeOfImage`);
    if (section.rawSize > 0) {
      assert.ok(section.rawOffset + section.rawSize <= bytes.length, `${section.name} raw data exceeds file`);
    }
  }
  const text = pe.sections.find((section) => section.name === '.text');
  assert.equal(text?.rawSize, 0, 'the pinned release must be the protected VMProtect output, not ReleasePlain');
  assert.ok(pe.sections.some((section) =>
    section.rawSize > 0 &&
    (section.characteristics & 0x20000000) !== 0 &&
    pe.entryRva >= section.rva &&
    pe.entryRva < section.rva + Math.max(section.virtualSize, section.rawSize)
  ), 'the protected entry point must be backed by executable raw data');
  assert.ok(pe.exception.rva > 0 && pe.exception.size >= 12, 'x64 unwind metadata is required');
  assert.equal(pe.exception.size % 12, 0, 'x64 unwind directory must contain complete RUNTIME_FUNCTION entries');
  assert.ok(pe.exception.rva + pe.exception.size <= pe.sizeOfImage);
  assert.ok(pe.relocations.rva > 0 && pe.relocations.size > 0, 'DIR64 relocation metadata is required');
  assert.ok(pe.relocations.rva + pe.relocations.size <= pe.sizeOfImage);
  assert.deepEqual(
    pe.tls,
    { rva: 0, size: 0 },
    'persistent static TLS is unsupported for a memory-mapped image; use the FLS-compatible build'
  );
  assert.deepEqual(pe.delayImports, { rva: 0, size: 0 }, 'delay imports are unsupported by the remote mapper');
});
