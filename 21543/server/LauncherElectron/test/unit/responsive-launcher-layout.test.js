import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import test from 'node:test';

const root = resolve(import.meta.dirname, '..', '..');
const read = (relativePath) => readFileSync(resolve(root, relativePath), 'utf8');

function finalCssRule(css, selector) {
  const marker = `\n${selector} {`;
  const start = css.lastIndexOf(marker);
  assert.notEqual(start, -1, `missing ${selector}`);
  const bodyStart = start + marker.length;
  return css.slice(bodyStart, css.indexOf('}', bodyStart));
}

test('renderer fills the window and reflows without whole-page zoom', () => {
  const css = read('src/renderer/styles.css');
  const app = read('src/renderer/app.js');
  const main = read('src/main/index.js');

  assert.doesNotMatch(css, /html,\s*\nbody\s*\{[^}]*min-width:\s*1601px/s);
  assert.doesNotMatch(css, /html,\s*\nbody\s*\{[^}]*min-height:\s*910px/s);
  assert.match(css, /\.shell\s*\{[^}]*width:\s*100vw[^}]*height:\s*100vh/s);
  assert.match(css, /\.agreement\s*\{[^}]*margin-top:\s*clamp\(/s);
  assert.match(css, /@media\s*\(max-height:\s*760px\)[\s\S]*?\.login-row\s*\{[^}]*height:\s*40px/s);
  assert.match(css, /@media\s*\(max-width:\s*1200px\)/);
  assert.match(css, /@media\s*\(max-height:\s*700px\)/);
  assert.doesNotMatch(css + app + main, /setZoomFactor|document\.body\.style\.zoom|zoom:\s*[\d.]+/);
  assert.match(app, /getBoundingClientRect\(\)\.width/);
  assert.match(app, /window\.addEventListener\(['"]resize['"],\s*scheduleBannerGeometryRefresh\)/);
  assert.doesNotMatch(app, /activeIndex\s*\*\s*500/);
});

test('login uses a responsive clear crystal panel without a persistent footer', () => {
  const html = read('src/renderer/index.html');
  const css = read('src/renderer/styles.css');
  assert.match(html, /class="login-panel crystal-surface"/);
  assert.doesNotMatch(html, /class="login-media"/);
  assert.match(finalCssRule(css, '.login-panel'), /width:\s*clamp\(360px,\s*31vw,\s*470px\)/);
  assert.match(finalCssRule(css, '.login-panel'), /backdrop-filter:\s*none/);
  assert.match(
    finalCssRule(css, '.shell[data-view="login"] .bottom-control-box'),
    /display:\s*none/
  );
  assert.match(css, /@media\s*\(max-width:\s*1200px\)[\s\S]*?\.login-panel\s*\{[^}]*width:\s*clamp\(360px,\s*40vw,\s*460px\)/s);
  assert.match(css, /@media\s*\(max-width:\s*1080px\)[\s\S]*?\.login-panel\s*\{[^}]*width:\s*clamp\(350px,\s*42vw,\s*440px\)/s);
});

test('existing dialogs are bounded by the responsive viewport', () => {
  const css = read('src/renderer/styles.css');
  assert.match(css, /\.gm-item-panel\s*\{[^}]*width:\s*min\(94vw,\s*1380px\)[^}]*height:\s*min\(88vh,\s*780px\)/s);
  assert.match(css, /\.server-modal-panel\s*\{[^}]*width:\s*min\(92vw,\s*890px\)[^}]*height:\s*min\(88vh,\s*674px\)/s);
  assert.match(css, /\.multi-instance-panel\s*\{[^}]*width:\s*min\(92vw,\s*720px\)[^}]*height:\s*min\(88vh,\s*750px\)/s);
  assert.match(css, /\.close-confirm-panel\s*\{[^}]*width:\s*min\(92vw,\s*460px\)/s);
});


test('shared launcher background covers the shell while media failure preserves foreground controls', () => {
  const css = read('src/renderer/styles.css');
  const homeView = finalCssRule(css, '.home-view');
  const homeVideo = finalCssRule(css, '.shell[data-view="home"] .launcher-background-video');

  assert.match(css, /\.launcher-background\s*\{[^}]*position:\s*absolute[^}]*z-index:\s*0[^}]*inset:\s*0[^}]*pointer-events:\s*none/s);
  assert.match(css, /\.launcher-background-video\s*\{[^}]*position:\s*absolute[^}]*inset:\s*0[^}]*width:\s*100%[^}]*height:\s*100%[^}]*object-fit:\s*cover/s);
  assert.match(css, /\.login-view,\s*\.home-view\s*\{[^}]*z-index:\s*1/s);
  assert.match(homeView, /background:\s*transparent/);
  assert.doesNotMatch(homeView, /#161044|rgb\(\s*22,\s*16,\s*68\s*\)/);
  assert.match(homeVideo, /filter:\s*none/);
  assert.doesNotMatch(homeVideo, /\b(?:brightness|saturate|contrast)\(/);
  assert.match(css, /\.media-failed\s+\.launcher-background-video\s*\{[^}]*opacity:\s*0/s);
  assert.doesNotMatch(css, /\.media-failed\s*\{[^}]*opacity:\s*0/s);
  assert.match(css, /\.launcher-media-fallback\s*\{[^}]*position:\s*absolute[^}]*inset:\s*0/s);
  assert.match(css, /\.launcher-media-status\s*\{[^}]*position:\s*absolute[^}]*z-index:\s*140/s);
});

test('large launcher glass surfaces disable backdrop blur', () => {
  const css = read('src/renderer/styles.css');

  for (const pattern of [
    /\.login-panel\s*\{[^}]*backdrop-filter:\s*none/s,
    /\.shell\[data-view="home"\] \.top-bar\s*\{[^}]*backdrop-filter:\s*none/s,
    /\.home-bulletins\s*\{[^}]*backdrop-filter:\s*none/s,
    /\.shell\[data-view="home"\] \.bottom-control-box\s*\{[^}]*backdrop-filter:\s*none/s,
    /\.gm-item-modal\s*\{[^}]*backdrop-filter:\s*none/s,
    /\.gm-item-panel\s*\{[^}]*backdrop-filter:\s*none/s
  ]) {
    assert.match(css, pattern);
  }
  assert.doesNotMatch(css, /backdrop-filter:\s*(?:[^;{}]*\bblur\b)/);
});

test('home activity popover stays bounded and scrollable inside the available stage', () => {
  const css = read('src/renderer/styles.css');
  const popover = finalCssRule(css, '.home-activity-popover');
  const header = finalCssRule(css, '.home-activity-popover > header');
  const deck = finalCssRule(css, '.home-content-deck');
  const lastTimelinePopover = finalCssRule(
    css,
    '.timeline-item:last-child .timeline-popover'
  );
  const lastTimelineArrow = finalCssRule(
    css,
    '.timeline-item:last-child .timeline-popover::after'
  );

  assert.match(popover, /display:\s*flex/);
  assert.match(popover, /flex-direction:\s*column/);
  assert.match(
    popover,
    /max-height:\s*min\(70vh,\s*620px,\s*calc\(100%\s*-\s*var\(--bottom-control-height\)\s*-\s*80px\)\)/
  );
  assert.match(header, /flex:\s*0\s+0\s+58px/);
  assert.match(deck, /min-height:\s*0/);
  assert.match(deck, /overflow-x:\s*hidden/);
  assert.match(deck, /overflow-y:\s*auto/);
  assert.match(deck, /align-content:\s*start/);
  assert.match(lastTimelinePopover, /right:\s*0/);
  assert.match(lastTimelinePopover, /left:\s*auto/);
  assert.match(lastTimelineArrow, /right:\s*16px/);
  assert.match(lastTimelineArrow, /left:\s*auto/);
  assert.match(
    css,
    /@media\s*\(max-height:\s*700px\)[\s\S]*?\.shell\[data-view="home"\]\s*\{[^}]*--bottom-control-height:\s*116px[^}]*\}[\s\S]*?\.home-activity-popover\s*\{[^}]*max-height:\s*calc\(100%\s*-\s*var\(--bottom-control-height\)\s*-\s*80px\)/s
  );
  assert.doesNotMatch(
    css,
    /@media\s*\(max-height:\s*700px\)[\s\S]*?:root\s*\{[^}]*--bottom-control-height/s
  );
  assert.match(css, /\.home-announcement-ticker\s*\{[^}]*bottom:\s*calc\(var\(--bottom-control-height\)/s);
});

test('home dock material is scoped while the approved login view keeps its footer hidden', () => {
  const css = read('src/renderer/styles.css');
  const homeFooter = finalCssRule(css, '.shell[data-view="home"] .bottom-control-box');
  const baseFooter = finalCssRule(css, '.bottom-control-box');
  const loginFooter = finalCssRule(css, '.shell[data-view="login"] .bottom-control-box');
  const finalHomeCascade = css.slice(css.lastIndexOf('/* Final home cascade:'));

  assert.match(finalCssRule(css, '.shell[data-view="home"] .top-bar'), /backdrop-filter:\s*none/);
  assert.match(homeFooter, /display:\s*grid/);
  assert.match(homeFooter, /grid-template-columns:\s*minmax\(280px,\s*1fr\)\s+auto/);
  assert.match(homeFooter, /backdrop-filter:\s*none/);
  assert.doesNotMatch(baseFooter, /display:\s*grid/);
  assert.match(loginFooter, /display:\s*none/);
  assert.doesNotMatch(
    finalHomeCascade,
    /(?:^|\n)\s*\.(?:bottom-control-box|home-dock-summary|home-dock-path|home-dock-actions|home-dock-button|server-select-trigger|start-btn)(?:[\s,{.:#>])/m
  );
  assert.match(
    css,
    /@media\s*\(max-width:\s*1200px\)[\s\S]*?\.shell\[data-view="home"\] \.bottom-control-box\s*\{[^}]*grid-template-columns:\s*1fr/s
  );
});

test('home glass surfaces brighten their local highlights without filtering the video', () => {
  const css = read('src/renderer/styles.css');
  const topBar = finalCssRule(css, '.shell[data-view="home"] .top-bar');
  const ticker = finalCssRule(css, '.home-announcement-ticker');
  const popover = finalCssRule(css, '.home-activity-popover');
  const dock = finalCssRule(css, '.shell[data-view="home"] .bottom-control-box');
  const controls = finalCssRule(
    css,
    '.shell[data-view="home"] .home-dock-button,\n.shell[data-view="home"] .server-select-trigger'
  );

  for (const surface of [topBar, ticker, dock]) {
    assert.match(surface, /border:\s*1px solid rgba\(220,\s*255,\s*255,\s*0\.3\)/);
    assert.match(surface, /rgba\(255,\s*255,\s*255,\s*0\.15\)/);
    assert.match(surface, /inset 0 1px rgba\(255,\s*255,\s*255,\s*0\.28\)/);
    assert.match(surface, /backdrop-filter:\s*none/);
  }

  assert.match(topBar, /rgba\(4,\s*12,\s*17,\s*0\.3\)/);
  assert.match(ticker, /rgba\(4,\s*12,\s*17,\s*0\.3\)/);
  assert.match(dock, /rgba\(4,\s*12,\s*17,\s*0\.32\)/);

  assert.match(controls, /border:\s*1px solid rgba\(220,\s*255,\s*255,\s*0\.34\)/);
  assert.match(controls, /rgba\(255,\s*255,\s*255,\s*0\.16\)/);
  assert.match(controls, /rgba\(4,\s*13,\s*18,\s*0\.32\)/);
  assert.match(controls, /inset 0 1px rgba\(255,\s*255,\s*255,\s*0\.26\)/);

  assert.match(popover, /border:\s*1px solid rgba\(226,\s*255,\s*255,\s*0\.58\)/);
  assert.match(popover, /rgba\(255,\s*255,\s*255,\s*0\.16\)/);
  assert.match(
    popover,
    /linear-gradient\(180deg,\s*rgba\(6,\s*18,\s*24,\s*0\.72\),\s*rgba\(3,\s*10,\s*14,\s*0\.64\)\)/
  );
  assert.match(popover, /inset 0 1px rgba\(255,\s*255,\s*255,\s*0\.3\)/);
  assert.match(popover, /backdrop-filter:\s*none/);
});

test('home dock actions keep every direct control on one shared height and baseline', () => {
  const css = read('src/renderer/styles.css');
  const actions = finalCssRule(css, '.shell[data-view="home"] .home-dock-actions');
  const standardControls = finalCssRule(
    css,
    '.shell[data-view="home"] .home-dock-button,\n.shell[data-view="home"] .server-select-trigger'
  );
  const serverWrapper = finalCssRule(
    css,
    '.shell[data-view="home"] .home-dock-actions .server-select-wrapper'
  );
  const startButton = finalCssRule(css, '.shell[data-view="home"] .start-btn');

  assert.match(actions, /align-items:\s*stretch/);
  assert.match(standardControls, /height:\s*58px/);
  assert.match(serverWrapper, /margin-top:\s*0/);
  assert.match(startButton, /height:\s*58px/);
});

test('start and GM query buttons use the crystal action palette without legacy assets', () => {
  const html = read('src/renderer/index.html');
  const css = read('src/renderer/styles.css');
  const startButton = finalCssRule(css, '.start-btn');
  const startHover = finalCssRule(css, '.start-btn:not(:disabled):hover');
  const startDisabled = finalCssRule(css, '.start-btn:disabled');
  const startLoading = finalCssRule(css, '.start-btn[data-state="loading"]');
  const startRunning = finalCssRule(css, '.start-btn[data-state="running"]');
  const gmQueryButton = finalCssRule(css, '.gm-filter-form .gm-query-button');
  const gmQueryHover = finalCssRule(css, '.gm-filter-form .gm-query-button:hover:not(:disabled)');

  assert.doesNotMatch(html, /start-btn-bg/);
  assert.doesNotMatch(css, /\.start-btn-bg/);
  assert.match(startButton, /color:\s*#071a1a/);
  assert.match(startButton, /linear-gradient\(100deg,\s*#b8fff9,\s*#43eadf\s*52%,\s*#a7fff8\)/);
  assert.doesNotMatch(startButton, /var\(--button-text\)/);
  assert.doesNotMatch(css, /var\(--button-text\)/);
  assert.match(startHover, /filter:\s*brightness\(1\.08\)/);
  assert.match(startHover, /0 14px 34px rgba\(31,\s*226,\s*212,\s*0\.3\)/);
  assert.match(startDisabled, /color:\s*rgba\(235,\s*255,\s*253,\s*0\.62\)/);
  assert.match(startDisabled, /linear-gradient\(100deg,\s*#4e6768,\s*#3e5d60\s*52%,\s*#577678\)/);
  assert.match(startLoading, /animation:\s*start-button-pulse\s+1\.1s\s+ease-in-out\s+infinite\s+alternate/);
  assert.match(startRunning, /linear-gradient\(100deg,\s*#b8fff9,\s*#43eadf\s*52%,\s*#a7fff8\)/);
  assert.match(finalCssRule(css, '.start-btn::after'), /width:\s*var\(--start-progress,\s*0%\)/);
  assert.match(finalCssRule(css, '.start-btn[data-state="loading"] .start-btn-spinner'), /animation:\s*spin/);
  assert.match(css, /\.start-btn-title,\s*\.start-btn-desc\s*\{[^}]*z-index:\s*2/s);
  assert.match(gmQueryButton, /linear-gradient\(180deg,[\s\S]*linear-gradient\(100deg,\s*#b8fff9,\s*#43eadf\)/);
  assert.doesNotMatch(gmQueryButton, /login-button-second/);
  assert.match(gmQueryHover, /filter:\s*brightness\(1\.08\)/);
  assert.doesNotMatch(gmQueryHover, /login-button-second/);
});

test('compact overlays use the neutral ice crystal material without retired music controls', () => {
  const html = read('src/renderer/index.html');
  const css = read('src/renderer/styles.css');
  const volume = finalCssRule(css, '.media-volume-popover');
  const volumeArrow = finalCssRule(css, '.media-volume-popover::before');
  const menu = finalCssRule(css, '.launcher-menu');
  const menuHover = finalCssRule(
    css,
    '.launcher-menu button:hover,\n.launcher-menu button:focus-visible'
  );
  const gmReset = finalCssRule(css, '.gm-filter-form .gm-reset-button');
  const homeVideo = finalCssRule(css, '.shell[data-view="home"] .launcher-background-video');

  assert.doesNotMatch(html + css, /musicMuteBtn/);
  assert.doesNotMatch(css, /#211b5d|#4b438a|rgba\(78,\s*78,\s*142/);

  for (const surface of [volume, menu, gmReset]) {
    assert.match(surface, /rgba\(220,\s*255,\s*255/);
    assert.match(surface, /rgba\(4,\s*13,\s*18/);
    assert.match(surface, /backdrop-filter:\s*none/);
  }
  assert.match(volumeArrow, /rgba\(9,\s*24,\s*29/);
  assert.match(volumeArrow, /rgba\(220,\s*255,\s*255/);
  assert.match(menuHover, /rgba\(50,\s*232,\s*220/);
  assert.match(homeVideo, /filter:\s*none/);
});
