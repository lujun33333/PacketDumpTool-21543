import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

const appSource = await readFile(
  new URL('../../src/renderer/app.js', import.meta.url),
  'utf8'
);
const stylesSource = await readFile(
  new URL('../../src/renderer/styles.css', import.meta.url),
  'utf8'
);

test('renderer routes and game button are owned by the official launcher state model', () => {
  assert.match(appSource, /from '\.\/official-launcher-state\.js'/);
  assert.match(appSource, /selectLauncherView\(launcherState\)/);
  assert.match(appSource, /selectGameButton\(launcherState\)/);
  assert.match(appSource, /reduceNativeLaunchStatus\(/);
  assert.match(appSource, /api\.onUserLogin\(/);
  assert.match(appSource, /api\.onUserLogout\(/);
});

test('login completion waits for authoritative session state instead of calling showHome', () => {
  const loginHandlerStart = appSource.indexOf("loginForm.addEventListener('submit'");
  const loginHandlerEnd = appSource.indexOf('\nasync function launchGame', loginHandlerStart);
  const loginHandler = appSource.slice(loginHandlerStart, loginHandlerEnd);

  assert.ok(loginHandlerStart >= 0 && loginHandlerEnd > loginHandlerStart);
  assert.doesNotMatch(loginHandler, /showHome\s*\(/);
  assert.doesNotMatch(loginHandler, /showLogin\s*\(/);
  assert.match(loginHandler, /beginAuthentication\(\)/);
  assert.match(appSource, /function beginAuthentication\([\s\S]*?AUTH_REQUESTED/);
  assert.match(loginHandler, /await refreshState\(\{ synchronizeSession: false \}\)/);
});

test('content requests are scoped and home revalidation is throttled to five minutes', () => {
  assert.match(appSource, /getLauncherContent\('bootstrap',\s*\{/);
  assert.match(appSource, /getLauncherContent\('home',\s*\{/);
  assert.doesNotMatch(appSource, /getLauncherContent\(\s*\)/);
  assert.match(appSource, /HOME_REFRESH_THROTTLE_MS\s*=\s*5\s*\*\s*60\s*\*\s*1000/);
  assert.match(appSource, /window\.addEventListener\('focus',\s*refreshHomeIfDue\)/);
  assert.match(appSource, /window\.addEventListener\('online',\s*refreshHomeIfDue\)/);
  assert.match(appSource, /document\.addEventListener\('visibilitychange',\s*refreshHomeWhenVisible\)/);
});

test('authoritative session changes advance the home request epoch before routing and stale replies are ignored', () => {
  const synchronizeStart = appSource.indexOf('function synchronizeAuthoritativeSession');
  const synchronizeEnd = appSource.indexOf('\nfunction beginAuthentication', synchronizeStart);
  const synchronize = appSource.slice(synchronizeStart, synchronizeEnd);
  const requestStart = appSource.indexOf('async function requestLauncherContent');
  const requestEnd = appSource.indexOf('\nfunction refreshHomeIfDue', requestStart);
  const request = appSource.slice(requestStart, requestEnd);

  assert.ok(synchronizeStart >= 0 && synchronizeEnd > synchronizeStart);
  assert.ok(synchronize.indexOf('advanceAuthSession()') >= 0);
  assert.ok(synchronize.indexOf('advanceAuthSession()') < synchronize.indexOf('transitionLauncher'));
  assert.match(request, /capture\(normalizedScope\)/);
  assert.match(request, /isCurrent\(normalizedScope,\s*requestEpoch\)/);
  assert.match(request, /deleteIfCurrentRequest\(normalizedScope,\s*request\)/);
});

test('official game button renders Chinese-only state labels', () => {
  assert.match(appSource, /startBtnDesc\.textContent\s*=\s*''/);
  for (const retiredDescription of [
    'LAUNCH GAME',
    'GAME READY',
    'GAME PRELOADED',
    'PRELOADING GAME',
    'ENTERING GAME',
    'SELECT PATH'
  ]) {
    assert.doesNotMatch(appSource, new RegExp(retiredDescription));
  }
});

test('banner rotation uses a responsive horizontal five-second carousel contract', () => {
  assert.match(
    appSource,
    /homeBannerTrack\.style\.transform\s*=\s*`translate3d\(-\$\{activeIndex\s*\*\s*bannerWidth\}px,\s*0,\s*0\)`/
  );
  assert.match(appSource, /cards\[0\]\.getBoundingClientRect\(\)\.width/);
  assert.match(appSource, /},\s*5000\);/);
});

test('timeline popovers open their official link by click or keyboard activation', () => {
  assert.match(appSource, /const releaseTimeline = \$\('releaseTimeline'\)/);
  assert.match(appSource, /releaseTimeline\.addEventListener\('click'/);
  assert.match(appSource, /releaseTimeline\.addEventListener\('keydown'/);
  assert.match(appSource, /event\.key === 'Enter'/);
});

test('server selector modal commits only an explicit draft selection', () => {
  assert.match(appSource, /const serverSelectTrigger = \$\('serverSelectTrigger'\)/);
  assert.match(appSource, /function openServerModal\(/);
  assert.match(appSource, /function closeServerModal\(/);
  assert.match(appSource, /serverModalConfirmBtn\.addEventListener\('click'/);
  assert.match(appSource, /serverSelect\.dispatchEvent\(new Event\('change'/);
  assert.match(appSource, /event\.target === serverModal/);
});

test('routine preload clears the login toast while home keeps errors and warnings visible', () => {
  assert.match(appSource, /isRoutineLoginPreloadStatus/);
  assert.match(appSource, /toCustomerStatusMessage\(text,\s*type,\s*''\)/);
  assert.match(appSource, /message:\s*toCustomerStatusMessage\(status\.message,\s*type,\s*''\)/);
  assert.match(
    appSource,
    /isRoutineLoginPreloadStatus\(status,\s*launcherState\.auth\.phase\)[\s\S]*?setStatus\('',\s*'info'\)/
  );
  assert.match(
    stylesSource,
    /\.shell\[data-view="home"\]\s+\.status:not\(\[data-type="error"\]\):not\(\[data-type="warn"\]\)\s*\{\s*display:\s*none;/
  );
  assert.doesNotMatch(stylesSource, /\.shell\[data-view="home"\]\s+\.status\s*\{\s*display:\s*none;/);
});
