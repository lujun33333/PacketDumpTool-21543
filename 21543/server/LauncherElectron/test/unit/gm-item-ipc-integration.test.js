import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import test from 'node:test';

const root = resolve(import.meta.dirname, '..', '..');
const read = (relativePath) => readFileSync(resolve(root, relativePath), 'utf8');

function sourceBetween(source, startMarker, endMarker) {
  const start = source.indexOf(startMarker);
  const end = source.indexOf(endMarker, start + startMarker.length);
  assert.ok(start >= 0, `missing start marker: ${startMarker}`);
  assert.ok(end > start, `missing end marker: ${endMarker}`);
  return source.slice(start, end);
}

test('main owns the GM database and authenticates both query IPC handlers', () => {
  const main = read('src/main/index.js');
  assert.match(main, /import\s+\{\s*createGmItemQueryService\s*\}\s+from\s+'\.\/gm-item-query-service\.js'/);
  assert.match(main, /app\.isPackaged[\s\S]*?process\.resourcesPath[\s\S]*?'gm-items\.sqlite3'/);
  assert.match(main, /function assertGmItemAccess\(\)[\s\S]*?!launcherAuthenticatedUser[\s\S]*?请先登录后使用 GM 道具查询/);

  const catalogHandler = sourceBetween(
    main,
    "ipcMain.handle('launcher:get-gm-catalog'",
    "ipcMain.handle('launcher:query-gm-items'"
  );
  assert.match(catalogHandler, /assertTrustedSender\(event, 'launcher:get-gm-catalog'\)/);
  assert.match(catalogHandler, /assertGmItemAccess\(\)/);
  assert.match(catalogHandler, /getGmItemQueryService\(\)\.getCatalog/);
  assert.ok(catalogHandler.indexOf('assertGmItemAccess()') < catalogHandler.indexOf('getGmItemQueryService()'));

  const queryHandler = sourceBetween(
    main,
    "ipcMain.handle('launcher:query-gm-items'",
    "ipcMain.handle('launcher:get-state'"
  );
  assert.match(queryHandler, /assertTrustedSender\(event, 'launcher:query-gm-items'\)/);
  assert.match(queryHandler, /assertGmItemAccess\(\)/);
  assert.match(queryHandler, /getGmItemQueryService\(\)\.queryItems/);
  assert.ok(queryHandler.indexOf('assertGmItemAccess()') < queryHandler.indexOf('getGmItemQueryService()'));
  assert.match(main, /before-quit[\s\S]*?closeGmItemQueryService\(\)/);
});

test('preload exposes only named GM catalog and query functions', () => {
  const preload = read('src/preload/preload.cjs');
  assert.match(
    preload,
    /getGmCatalog:\s*\(primaryCategory\s*=\s*''\)\s*=>\s*ipcRenderer\.invoke\('launcher:get-gm-catalog',\s*\{\s*primaryCategory\s*\}\)/
  );
  assert.match(
    preload,
    /queryGmItems:\s*\(query\)\s*=>\s*ipcRenderer\.invoke\('launcher:query-gm-items',\s*query\)/
  );
  assert.doesNotMatch(preload, /gmDatabasePath|DatabaseSync|node:sqlite/);
});

test('electron-builder ships only the read-only GM SQLite file as an extra resource', () => {
  const packageJson = JSON.parse(read('package.json'));
  assert.deepEqual(packageJson.build?.extraResources, [
    { from: 'resources/gm-items.sqlite3', to: 'gm-items.sqlite3' }
  ]);
  assert.equal(
    packageJson.build.extraResources.some((entry) => /\.(?:dll|bundle)$/iu.test(String(entry?.from || entry))),
    false
  );
});
