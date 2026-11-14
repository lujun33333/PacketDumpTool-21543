import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

const root = new URL('../../', import.meta.url);
const [html, styles, renderer, preload, main, portableRuntimeSmoke, releaseSmoke] = await Promise.all([
  readFile(new URL('src/renderer/index.html', root), 'utf8'),
  readFile(new URL('src/renderer/styles.css', root), 'utf8'),
  readFile(new URL('src/renderer/app.js', root), 'utf8'),
  readFile(new URL('src/preload/preload.cjs', root), 'utf8'),
  readFile(new URL('src/main/index.js', root), 'utf8'),
  readFile(new URL('src/scripts/smoke-portable-runtime-x64.cjs', root), 'utf8'),
  readFile(new URL('src/scripts/smoke-release.cjs', root), 'utf8')
]);

test('one official close dialog is shared by the login and authenticated pages', () => {
  assert.equal((html.match(/id="closeConfirmModal"/g) || []).length, 1);
  assert.ok(html.indexOf('id="closeConfirmModal"') > html.indexOf('id="homeView"'));
  for (const id of [
    'closeConfirmModal',
    'closeConfirmPanel',
    'closeConfirmDismiss',
    'closeActionTray',
    'closeActionQuit',
    'closeRemember',
    'closeConfirmBtn'
  ]) {
    assert.match(html, new RegExp(`id="${id}"`), `missing #${id}`);
  }
  assert.match(html, /id="closeActionTray"[^>]*value="tray"[^>]*checked/);
  assert.match(html, /您点击了【关闭】，您希望：/);
  assert.match(html, />最小化系统托盘</);
  assert.match(html, />退出启动器</);
  assert.match(html, />不再提醒</);
  assert.match(html, /id="closeConfirmBtn"[^>]*>确定</);
});

test('dialog styling reuses official launcher assets and compact flat layout', () => {
  assert.match(styles, /\.close-confirm-panel\s*\{[^}]*width:\s*min\(92vw,\s*460px\)[^}]*min-height:\s*280px/s);
  assert.match(styles, /\.close-confirm-action\s*\{[^}]*display:\s*inline-flex/s);
  assert.match(styles, /button-main-Dju-8LS7\.png/);
  assert.match(styles, /global-checkbox-default-nuCS9DD-\.png/);
  assert.match(styles, /global-checkbox-default-checked-DRrrQZux\.png/);
  assert.doesNotMatch(styles, /\.close-confirm-action\s*\{[^}]*border-radius:\s*(?:8|10|12|16)px/s);
});

test('title-bar close and native Alt+F4 request share the same renderer decision flow', () => {
  assert.match(renderer, /from '\.\/close-preference\.js'/);
  assert.match(renderer, /function requestLauncherClose\(/);
  assert.match(renderer, /function openCloseDialog\(/);
  assert.match(renderer, /async function confirmLauncherClose\(/);
  assert.match(renderer, /\$\('closeBtn'\)\.addEventListener\('click',\s*requestLauncherClose\)/);
  assert.match(renderer, /api\.onCloseRequested\(requestLauncherClose\)/);
  assert.match(renderer, /api\.onClosePreferenceReset\(/);
  assert.match(renderer, /api\.applyCloseAction\(action\)/);
  assert.doesNotMatch(renderer, /\$\('closeBtn'\)\.addEventListener\('click',\s*\(\)\s*=>\s*api\.close\(\)\)/);

  assert.match(preload, /window:close-requested/);
  assert.match(preload, /window:close-preference-reset/);
  assert.match(preload, /window:apply-close-action/);
  assert.match(main, /mainWindow\.webContents\.send\('window:close-requested'\)/);
});

test('game start continues to hide for loading without entering close confirmation', () => {
  const start = main.slice(
    main.indexOf("ipcMain.handle('launcher:start-game'"),
    main.indexOf("ipcMain.handle('window:minimize'")
  );
  assert.match(start, /hideForGameLoading\(/);
  assert.doesNotMatch(start, /close-requested|requestRendererCloseDecision/);
});

test('automated runtime cleanup explicitly quits without waiting on the user close prompt', () => {
  assert.match(portableRuntimeSmoke, /qqspeedLauncher\.applyCloseAction\(['"]quit['"]\)/);
  assert.doesNotMatch(portableRuntimeSmoke, /qqspeedLauncher\.close\(\)/);
});

test('release smoke requests the shared close flow before applying its tray choice', () => {
  assert.match(releaseSmoke, /qqspeedLauncher\.close\(\)/);
  assert.match(releaseSmoke, /closeRequest\?\.requested/);
  assert.match(releaseSmoke, /qqspeedLauncher\.applyCloseAction\(['"]tray['"]\)/);
});
