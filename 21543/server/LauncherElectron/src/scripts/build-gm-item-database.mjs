import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { parseOfficialItemList } from '../shared/gm-item-catalog.js';
import { writeItemCatalogDatabase } from '../shared/gm-item-database.js';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const projectRoot = path.resolve(scriptDirectory, '..', '..');

function readArgument(name) {
  const index = process.argv.indexOf(name);
  if (index === -1) return '';
  const value = process.argv[index + 1];
  if (!value || value.startsWith('--')) {
    throw new Error(`${name} requires a path`);
  }
  return value;
}

const sourceArgument = readArgument('--source');
if (!sourceArgument) {
  throw new Error('Usage: node src/scripts/build-gm-item-database.mjs --source <itemlist.save> [--output <gm-items.sqlite3>]');
}

const outputArgument = readArgument('--output');
const sourcePath = path.resolve(sourceArgument);
const outputPath = outputArgument
  ? path.resolve(outputArgument)
  : path.join(projectRoot, 'resources', 'gm-items.sqlite3');

const source = readFileSync(sourcePath);
const catalog = parseOfficialItemList(source);
catalog.sourceSha256 = createHash('sha256').update(source).digest('hex');

writeItemCatalogDatabase(outputPath, catalog);

const counts = new Map();
for (const item of catalog.items) {
  counts.set(item.category, (counts.get(item.category) || 0) + 1);
}

console.log(`GM item catalog created: ${outputPath}`);
console.log(`Source version: ${catalog.version}`);
console.log(`Imported rows: ${catalog.items.length}`);
for (const [category, count] of [...counts].sort((left, right) => left[0].localeCompare(right[0], 'zh-CN'))) {
  console.log(`${category}: ${count}`);
}
