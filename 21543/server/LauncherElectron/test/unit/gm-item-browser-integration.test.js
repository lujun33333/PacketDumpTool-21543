import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import test from 'node:test';

import { resolveGmGridLayout } from '../../src/renderer/gm-item-browser.js';

const root = resolve(import.meta.dirname, '..', '..');
const read = (relativePath) => readFileSync(resolve(root, relativePath), 'utf8');

function finalCssRule(css, selector) {
  const marker = `\n${selector} {`;
  const start = css.lastIndexOf(marker);
  assert.notEqual(start, -1, `missing ${selector}`);
  const bodyStart = start + marker.length;
  return css.slice(bodyStart, css.indexOf('}', bodyStart));
}

test('renderer exposes an authenticated dock GM browser with two text category levels', () => {
  const html = read('src/renderer/index.html');
  assert.match(html, /img-src[^;]*https:\/\/iips\.speed\.qq\.com/);
  assert.match(html, /id="gmItemBrowserBtn"[^>]*class="home-dock-button hidden"/);
  assert.doesNotMatch(html, /id="gmItemBrowserMenuBtn"/);
  assert.match(html, /id="gmItemBrowserModal"[^>]*role="dialog"[^>]*aria-modal="true"/);
  for (const id of [
    'gmPrimaryCategories',
    'gmSecondaryCategories',
    'gmSecondaryCategory',
    'gmKeyword',
    'gmGender',
    'gmSortDirection',
    'gmQueryBtn',
    'gmResetBtn',
    'gmItemGrid',
    'gmResultSummary',
    'gmPrevPageBtn',
    'gmNextPageBtn',
    'gmModalCloseBtn'
  ]) {
    assert.match(html, new RegExp(`id="${id}"`), id);
  }
  assert.match(html, /<option value="asc">ID 从小到大<\/option>/);
  assert.match(html, /<option value="desc">ID 从大到小<\/option>/);
  assert.doesNotMatch(html, /gm-category-icon/);
});

test('renderer drives linked categories, safe images, responsive pages, and logout cleanup', () => {
  const app = read('src/renderer/app.js');
  assert.match(app, /from '\.\/gm-item-browser\.js'/);
  assert.match(app, /api\.getGmCatalog\(/);
  assert.match(app, /api\.queryGmItems\(/);
  assert.match(app, /function renderGmPrimaryCategories/);
  assert.match(app, /function renderGmSecondaryCategories/);
  assert.match(app, /formatGmSubcategoryLabel\(/);
  assert.match(app, /createGmItemImageUrl\(/);
  assert.match(app, /new ResizeObserver\(/);
  assert.match(app, /resolveGmGridLayout\(/);
  assert.match(app, /function closeGmItemBrowser/);
  assert.match(app, /const gmItemBrowserBtn = \$\('gmItemBrowserBtn'\)/);
  assert.match(app, /gmItemBrowserBtn\.addEventListener\('click',\s*\(\) => void openGmItemBrowser\(\)\)/);
  assert.match(app, /gmSecondaryCategories\.replaceChildren/);
  assert.match(app, /dataGmSecondary|dataset\.gmSecondary/);
  assert.match(app, /className = 'gm-item-card-category'/);
  assert.match(app, /gmItemBrowserBtn\.classList\.toggle/);
  assert.match(app, /gmSecondaryCategories\.addEventListener\('click'/);
  assert.match(
    app,
    /gmSecondaryCategory\.addEventListener\('change',[\s\S]*?secondaryCategory = gmSecondaryCategory\.value[\s\S]*?page = 1[\s\S]*?renderGmSecondaryCategories\(\)[\s\S]*?queryGmItems\(\)/
  );
  assert.match(
    app,
    /function trapGmModalFocus[\s\S]*?!element\.classList\.contains\('sr-only'\)[\s\S]*?element\.getAttribute\('aria-hidden'\) !== 'true'[\s\S]*?element\.getAttribute\('tabindex'\) !== '-1'/
  );
  assert.match(
    app,
    /api\.queryGmItems\(\{\s*primaryCategory:[\s\S]*?secondaryCategory:[\s\S]*?gender:[\s\S]*?keyword:[\s\S]*?sortDirection:[\s\S]*?page:[\s\S]*?pageSize:/
  );
  assert.match(app, /gmSecondaryCategory\.disabled = gmBrowserState\.busy \|\| !gmBrowserState\.primaryCategory/);
  assert.match(app, /gmPrevPageBtn\.disabled = gmBrowserState\.busy \|\| gmBrowserState\.page <= 1/);
  assert.match(app, /gmNextPageBtn\.disabled = gmBrowserState\.busy/);
  assert.doesNotMatch(app, /gmItemBrowserMenuBtn/);
  const logoutStart = app.indexOf('if (typeof api?.onUserLogout');
  const logoutEnd = app.indexOf('\nfunction refreshHomeWhenVisible', logoutStart);
  const logoutHandler = app.slice(logoutStart, logoutEnd);
  assert.ok(logoutStart >= 0 && logoutEnd > logoutStart);
  assert.match(
    logoutHandler,
    /closeGmItemBrowser\(\{\s*reset:\s*true,\s*restoreFocus:\s*false\s*\}\)/
  );
  assert.match(
    logoutHandler,
    /synchronizeAuthoritativeSession\(session\)[\s\S]*?focusLoginUsernameIfVisible\(\{\s*shell,\s*loginView,\s*username\s*\}\)/
  );
  assert.ok(
    logoutHandler.indexOf('closeGmItemBrowser') < logoutHandler.indexOf('synchronizeAuthoritativeSession')
  );
  assert.ok(
    logoutHandler.indexOf('synchronizeAuthoritativeSession') < logoutHandler.indexOf('focusLoginUsernameIfVisible')
  );
  assert.match(app, /secondaryCategory:\s*''[\s\S]*?page:\s*1/);
  assert.match(app, /sortDirection:\s*'asc'/);
});

test('GM uses a no-blur crystal panel and a three-row variable-column card grid', () => {
  const css = read('src/renderer/styles.css');
  assert.match(finalCssRule(css, '.gm-item-modal'), /backdrop-filter:\s*none/);
  assert.match(finalCssRule(css, '.gm-item-panel'), /backdrop-filter:\s*none/);
  assert.match(css, /\.gm-primary-categories\s*\{[^}]*width:/s);
  assert.match(css, /\.gm-primary-category\s*\{[^}]*text-align:\s*left/s);
  assert.match(css, /\.gm-secondary-categories\s*\{[^}]*display:\s*flex/s);
  assert.doesNotMatch(css, /\.gm-category-icon/);
  assert.match(css, /\.gm-item-grid\s*\{[^}]*grid-template-columns:\s*repeat\(var\(--gm-columns\),\s*minmax\(0,\s*1fr\)\)/s);
  assert.match(css, /\.gm-item-grid\s*\{[^}]*grid-template-rows:\s*repeat\(3,\s*minmax\(0,\s*1fr\)\)/s);
  assert.match(css, /\.gm-item-card-category\s*\{/);
  assert.match(css, /\.gm-item-card-image\s*\{[^}]*object-fit:\s*contain/s);
  assert.match(finalCssRule(css, '.gm-results-viewport'), /overflow-x:\s*hidden/);
  assert.match(finalCssRule(css, '.gm-results-viewport'), /overflow-y:\s*auto/);
  assert.match(
    css,
    /@media\s*\(max-height:\s*730px\)[\s\S]*?\.gm-item-grid\s*\{[^}]*min-height:\s*320px[^}]*gap:\s*4px/s
  );
  assert.match(
    css,
    /@media\s*\(max-height:\s*730px\)[\s\S]*?\.gm-item-card\s*\{[^}]*grid-template-rows:\s*minmax\(32px,\s*1fr\)\s+auto\s+auto\s+auto\s+20px[^}]*gap:\s*1px[^}]*padding:\s*3px/s
  );
  assert.match(
    css,
    /@media\s*\(max-height:\s*730px\)[\s\S]*?\.gm-item-copy\s*\{[^}]*height:\s*20px/s
  );
});

test('GM cards have a safe height budget on both sides of the compact breakpoint', () => {
  const css = read('src/renderer/styles.css');
  assert.match(finalCssRule(css, '.gm-item-panel'), /height:\s*min\(88vh,\s*780px\)/);
  assert.match(finalCssRule(css, '.gm-item-main'), /padding:\s*14px 16px 12px/);
  assert.match(finalCssRule(css, '.gm-item-main'), /grid-template-rows:\s*auto auto 20px minmax\(0,\s*1fr\) auto/);
  assert.match(finalCssRule(css, '.gm-item-card'), /grid-template-rows:\s*minmax\(42px,\s*1fr\) auto auto auto 24px/);
  assert.match(finalCssRule(css, '.gm-item-card'), /gap:\s*2px/);
  assert.match(finalCssRule(css, '.gm-item-card'), /padding:\s*5px/);
  assert.match(finalCssRule(css, '.gm-item-grid'), /gap:\s*clamp\(5px,\s*0\.62vw,\s*9px\)/);
  assert.match(finalCssRule(css, '.gm-item-footer'), /min-height:\s*42px/);
  assert.match(
    css,
    /@media\s*\(max-height:\s*730px\)\s*\{[\s\S]*?\.gm-item-panel\s*\{[^}]*height:\s*92vh/s
  );
  assert.match(
    css,
    /@media\s*\(max-height:\s*730px\)[\s\S]*?\.gm-item-main\s*\{[^}]*padding-block:\s*10px 8px/s
  );
  assert.match(
    css,
    /@media\s*\(max-height:\s*730px\)[\s\S]*?\.gm-filter-form button\s*\{[^}]*height:\s*34px/s
  );
  assert.match(
    css,
    /@media\s*\(max-height:\s*730px\)[\s\S]*?\.gm-secondary-row\s*\{[^}]*padding-block:\s*6px 5px/s
  );
  assert.match(
    css,
    /@media\s*\(max-height:\s*730px\)[\s\S]*?\.gm-secondary-category\s*\{[^}]*min-height:\s*26px/s
  );
  assert.match(
    css,
    /@media\s*\(max-height:\s*730px\)[\s\S]*?\.gm-item-card-name\s*\{[^}]*line-height:\s*13px/s
  );
  assert.match(
    css,
    /@media\s*\(max-height:\s*730px\)[\s\S]*?\.gm-item-card-id\s*\{[^}]*line-height:\s*12px/s
  );
  assert.match(
    css,
    /@media\s*\(max-height:\s*730px\)[\s\S]*?\.gm-item-card-category\s*\{[^}]*line-height:\s*10px/s
  );
  assert.match(
    css,
    /@media\s*\(max-height:\s*730px\)[\s\S]*?\.gm-item-footer\s*\{[^}]*min-height:\s*32px/s
  );
  assert.match(
    css,
    /\.gm-item-header\s*\{[^}]*height:\s*clamp\(50px,\s*7\.5vh,\s*64px\)/s
  );

  const headerHeight = (windowHeight) => Math.min(64, Math.max(50, windowHeight * 0.075));
  const compactAvailableHeight = (windowHeight) => (
    (windowHeight * 0.92)
    - 2
    - headerHeight(windowHeight)
    - 18
    - 34
    - 37
    - 20
    - 32
  );
  const compactCardMinimum = 32 + 13 + 12 + 10 + 20 + 4 + 6;
  const compactGridMinimum = (compactCardMinimum * 3) + 8;
  assert.ok(320 >= compactGridMinimum);
  for (const height of [700, 701, 730]) {
    assert.ok(
      compactAvailableHeight(height) >= 320,
      `${height}px compact viewport must fit the 320px three-row grid`
    );
  }

  const defaultAvailableAt731 = (731 * 0.88)
    - 2
    - headerHeight(731)
    - 26
    - 38
    - 48
    - 20
    - 42;
  const defaultCardMinimum = 42 + 16 + 14 + 12 + 24 + 8 + 10;
  const defaultGridMinimum = (defaultCardMinimum * 3) + 18;
  assert.ok(defaultAvailableAt731 >= defaultGridMinimum);
});

test('the minimum BrowserWindow width derives a six-column GM result viewport', () => {
  const main = read('src/main/index.js');
  const css = read('src/renderer/styles.css');
  const minWindowMatch = main.match(/const MIN_WINDOW_WIDTH = (\d+);/);
  assert.ok(minWindowMatch);
  const minWindowWidth = Number(minWindowMatch[1]);
  assert.equal(minWindowWidth, 1024);
  assert.match(finalCssRule(css, '.gm-item-panel'), /width:\s*min\(94vw,\s*1380px\)/);
  assert.match(finalCssRule(css, '.gm-primary-categories'), /width:\s*clamp\(122px,\s*10\.5vw,\s*156px\)/);
  assert.match(
    css,
    /@media\s*\(max-width:\s*1200px\)[\s\S]*?\.gm-item-main\s*\{[^}]*padding-inline:\s*12px/s
  );
  const derivedResultWidth = (minWindowWidth * 0.94) - 2 - 122 - (12 * 2);
  assert.ok(derivedResultWidth > 814 && derivedResultWidth < 816);
  assert.deepEqual(resolveGmGridLayout(derivedResultWidth), { columns: 6, pageSize: 18 });
});
