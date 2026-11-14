const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..', '..');
const renderer = fs.readFileSync(path.join(root, 'src', 'renderer', 'app.js'), 'utf8');
const preload = fs.readFileSync(path.join(root, 'src', 'preload', 'preload.cjs'), 'utf8');
const main = fs.readFileSync(path.join(root, 'src', 'main', 'index.js'), 'utf8');
const gameLoadingVisibility = fs.readFileSync(path.join(root, 'src', 'main', 'game-loading-visibility.js'), 'utf8');
const nativeCore = fs.readFileSync(path.join(root, 'src', 'main', 'native-core.js'), 'utf8');
const worker = fs.readFileSync(path.join(root, 'src', 'main', 'native-worker.js'), 'utf8');
const packageJson = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8'));
const rendererHtml = fs.readFileSync(path.join(root, 'src', 'renderer', 'index.html'), 'utf8');
const rendererCss = fs.readFileSync(path.join(root, 'src', 'renderer', 'styles.css'), 'utf8');
const gmBrowserHelper = fs.readFileSync(path.join(root, 'src', 'renderer', 'gm-item-browser.js'), 'utf8');

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

assert(preload.includes("preloadGame: () => ipcRenderer.invoke('launcher:preload-game')"), 'Preload API is missing.');
assert(/startGame:\s*\([^)]*\)\s*=>\s*ipcRenderer\.invoke\('launcher:start-game'/.test(preload),
  'Explicit start-game API is missing.');
assert(main.includes("ipcMain.handle('launcher:preload-game'"), 'Main process does not expose the preload phase.');
assert(main.includes("ipcMain.handle('launcher:start-game'"), 'Main process does not expose the click-to-start phase.');
assert(renderer.includes('async function preloadGame()'), 'Renderer has no GameApp preload flow.');
assert(
  /function startLauncherAfterUpdateGate\(\)[\s\S]*?createMainWindow\(\);[\s\S]*?startStartupNativePreload\(\);/.test(main)
    && /app\.whenReady\(\)\.then[\s\S]*?startLauncherAfterUpdateGate\(\)/.test(main),
  'The main process must start GameApp preload as soon as the launcher window is created.'
);
assert(
  /async function bootstrap\(\)[\s\S]*?requestLauncherContent\('bootstrap'\)[\s\S]*?await prepareGame\(\);[\s\S]*?await preloadGame\(\);/.test(renderer),
  'Opening the launcher must load only bootstrap content before preloading GameApp.'
);
assert(
  /api\.onUserLogin\([\s\S]*?synchronizeAuthoritativeSession\(session\)/.test(renderer),
  'Only the authoritative main-process login event may enter the launcher home page.'
);
assert(
  !/loginForm\.addEventListener[\s\S]*?showHome\(/.test(renderer),
  'The login IPC result must not route directly to the home page.'
);
assert(
  /api\.onUserLogout\([\s\S]*?synchronizeAuthoritativeSession\(session\)/.test(renderer),
  'Only the authoritative main-process logout event may leave the home page.'
);
assert(
  /async function launchGame\(\)[\s\S]*?await api\.startGame\(/.test(renderer),
  'Only the explicit start-game flow may release the preloaded GameApp.'
);
assert(
  /async function ensureAuthenticatedNativeSession[\s\S]*?loadRememberedCredentials/.test(main),
  'A second Start click must restore a fresh native session from the encrypted remembered credential.'
);
assert(
  /async function startNativeGame\([^)]*\)[\s\S]*?await ensureAuthenticatedNativeSession\(\)/.test(main),
  'Start game must ensure the native session is authenticated after the previous GameApp exits.'
);
assert(
  renderer.includes("startBtn.addEventListener('click', launchGame);"),
  'Start button must own the GameApp release flow.'
);
assert(
  /const loginDraft = \{[\s\S]*?autoLogin: autoLogin\.checked/.test(renderer),
  'Auto-login selection must be persisted with the credential settings.'
);
assert(
  /async function bootstrap\(\)[\s\S]*?login\?\.autoLogin[\s\S]*?await api\.login\(/.test(renderer),
  'Launcher restart must authenticate automatically when auto-login and a remembered password are available.'
);
assert(
  /async function startNativeGame\([^)]*\)[\s\S]*?waitForCommandResult\(record, 'start-game'/.test(main),
  'Start Game must return after the native command is accepted instead of waiting for GameApp readiness.'
);
assert(
  /ipcMain\.handle\('launcher:start-game'[\s\S]*?hideForGameLoading\([\s\S]*?await startNativeGame\(/.test(main),
  'The main process must hide the launcher in the tray before releasing GameApp.'
);
assert(
  !/const result = await api\.startGame\(\);[\s\S]*?await api\.minimize\(\);/.test(renderer),
  'The renderer must not wait for the final ready state before minimizing.'
);
assert(
  /FAILED_SESSION_PHASES\.has\(record\.status\.phase\)[\s\S]*?restoreAfterGameLoadingFailure\(record\.gameLoadingAttempt\)/.test(main)
    && /ipcMain\.handle\('launcher:start-game'[\s\S]*?const loadingAttempt = hideForGameLoading\([\s\S]*?catch \(error\)[\s\S]*?restoreAfterGameLoadingFailure\(loadingAttempt\)/.test(main)
    && /attempts\.delete\(attempt\)[\s\S]*?attempts\.size === 0 && !isQuitting\(\)[\s\S]*?onRestore\(\)/.test(gameLoadingVisibility),
  'A failed startup must release only its own hide request and restore after the final attempt ends.'
);
assert(
  /ipcMain\.handle\('window:close'[\s\S]*?assertWindowControlSender\(event/.test(main),
  'The close button must use the owned-window control check instead of privileged document URL matching.'
);
assert(
  /mainWindow\.on\('close'[\s\S]*?!applicationQuitRequested[\s\S]*?requestRendererCloseDecision\(mainWindow\)/.test(main)
    && /ipcMain\.handle\('window:apply-close-action'[\s\S]*?action === 'tray'[\s\S]*?trayController\?\.hideToTray\(window\)/.test(main)
    && /hasPendingNativeSession\(\)[\s\S]*?cancelAllPendingNativeSessions\(\)/.test(main),
  'Prompted close-to-tray and explicit-exit native cleanup are not both wired.'
);
assert(
  /CLIENT_LOADING_FILES[\s\S]*?LoaderBG\.bmp[\s\S]*?Progress_Back\.bmp[\s\S]*?Progress_Fore\.bmp[\s\S]*?loadertipstext\.ini/.test(nativeCore),
  'Game preparation must require the official client loading UI resources.'
);
assert(rendererHtml.includes('start-btn-spinner'), 'Start button is missing its dynamic loading indicator.');
assert(
  /\.start-btn\[data-state="loading"\][\s\S]*?animation:/.test(rendererCss),
  'Start button loading state is not animated.'
);
for (const bindingName of [
  "'beginSession'",
  "'applyAuth'",
  "'startGame'",
  "'getState'"
]) {
  assert(worker.includes(bindingName), `Native worker is missing the minimal Hook binding: ${bindingName}`);
}
assert(worker.includes('loginPrivateServer') && worker.includes('LauncherSessionStateMachine'),
  'JS utility process does not own private login and the launcher state machine.');
assert(!worker.includes('QQSpeedNativeBridge_SubmitCredentials'),
  'Credentials still cross the C++ boundary.');
assert(
  preload.includes('getLauncherContent')
    && renderer.includes("getLauncherContent('bootstrap'")
    && renderer.includes("getLauncherContent('home'"),
  'Dynamic launcher content API is missing.');
assert(
  renderer.includes('MediaController')
    && rendererHtml.includes('launcherBackgroundVideo')
    && rendererHtml.includes('../../assets/launcher-background.mp4'),
  'The shared packaged background video is missing.'
);
assert(
  !/(?:loginMediaImage|homeHeroImage|launcherMusic|musicControlItem)/.test(rendererHtml),
  'Legacy image or independent music media remains in the renderer.'
);
assert(
  !/<video[^>]*\sposter=/s.test(rendererHtml),
  'The shared background video must not use a poster image.'
);
assert(!rendererCss.includes('home-bg-official-qqspeed.png'),
  'Renderer still uses the captured whole-page screenshot.');
assert(packageJson.build?.compression === 'store', 'Portable build must use store compression for fast single-EXE startup.');
assert(
  packageJson.build?.win?.requestedExecutionLevel === 'asInvoker',
  'The launcher UI and Chromium child processes must run at the caller integrity level without a UAC prompt.'
);
assert(
  /const BASE_WINDOW_WIDTH = 1280;/.test(main)
    && /const BASE_WINDOW_HEIGHT = 720;/.test(main)
    && /const MIN_WINDOW_WIDTH = 1024;/.test(main)
    && /const MIN_WINDOW_HEIGHT = 640;/.test(main)
    && /resizable:\s*true/.test(main)
    && /maximizable:\s*true/.test(main),
  'Responsive BrowserWindow geometry is incomplete.'
);
assert(
  /\.shell\s*\{[\s\S]*?width:\s*100vw;[\s\S]*?height:\s*100vh;/.test(rendererCss)
    && /\.login-panel\s*\{[\s\S]*?width:\s*clamp\(360px,\s*31vw,\s*470px\)/.test(rendererCss)
    && /@media\s*\(max-width:\s*1200px\)/.test(rendererCss)
    && /@media\s*\(max-height:\s*700px\)/.test(rendererCss)
    && !/setZoomFactor|document\.body\.style\.zoom/.test(`${main}\n${renderer}`),
  'The renderer must reflow at 1024x640, 1280x720, and larger viewports without whole-page zoom.'
);
for (const id of [
  'gmItemBrowserBtn',
  'gmItemBrowserModal',
  'gmPrimaryCategories',
  'gmSecondaryCategory',
  'gmSortDirection',
  'gmItemGrid'
]) {
  assert(rendererHtml.includes(`id="${id}"`), `GM renderer is missing #${id}.`);
}
assert(
  !rendererHtml.includes('id="gmItemBrowserMenuBtn"'),
  'The retired GM launcher-menu entry must not be restored.'
);
assert(
  preload.includes("getGmCatalog: (primaryCategory = '')")
    && preload.includes("ipcRenderer.invoke('launcher:get-gm-catalog'")
    && preload.includes("ipcRenderer.invoke('launcher:query-gm-items'"),
  'Authenticated GM preload APIs are incomplete.'
);
assert(
  /ipcMain\.handle\('launcher:get-gm-catalog'[\s\S]*?assertTrustedSender[\s\S]*?assertGmItemAccess/.test(main)
    && /ipcMain\.handle\('launcher:query-gm-items'[\s\S]*?assertTrustedSender[\s\S]*?assertGmItemAccess/.test(main),
  'GM IPC must enforce sender trust and login state before database access.'
);
assert(
  /async function openGmItemBrowser\([\s\S]*?loadGmCatalog/.test(renderer)
    && /async function loadGmCatalog\([\s\S]*?api\.getGmCatalog/.test(renderer)
    && /async function queryGmItems\([\s\S]*?api\.queryGmItems/.test(renderer)
    && /api\.onUserLogout\([\s\S]*?closeGmItemBrowser\(\{ reset:\s*true/.test(renderer),
  'GM open/query/logout cleanup flow is incomplete.'
);
assert(
  /width >= 1000/.test(gmBrowserHelper)
    && /width >= 860/.test(gmBrowserHelper)
    && /https:\/\/iips\.speed\.qq\.com\/images\//.test(gmBrowserHelper),
  'GM 10/8/6-column layout or fixed item-image origin is missing.'
);
assert(
  Array.isArray(packageJson.build?.extraResources)
    && packageJson.build.extraResources.some((entry) => (
      entry?.from === 'resources/gm-items.sqlite3' && entry?.to === 'gm-items.sqlite3'
    )),
  'The read-only GM database is not configured as a packaged resource.'
);

console.log('smoke-renderer-flow: PASS');
