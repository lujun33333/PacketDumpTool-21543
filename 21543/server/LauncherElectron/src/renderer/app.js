import {
  normalizeLauncherContent,
  renderLauncherContent
} from './content-view.js';
import { MediaController } from './media-controller.js';
import {
  toCustomerErrorMessage,
  toCustomerStatusMessage
} from '../shared/customer-error-message.js';
import {
  createQqAvatarUrl,
  sanitizeAccountAvatarUrl
} from '../shared/account-avatar.js';
import {
  AuthSessionRequestGate,
  createOfficialLauncherState,
  isRoutineLoginPreloadStatus,
  officialLauncherReducer,
  reduceNativeLaunchStatus,
  selectContentEffects,
  selectGameButton,
  selectLauncherView
} from './official-launcher-state.js';
import {
  CLOSE_ACTION_QUIT,
  CLOSE_ACTION_TRAY,
  readClosePreference,
  resetClosePreference,
  saveClosePreference
} from './close-preference.js';
import {
  createGmItemImageUrl,
  formatGmSubcategoryLabel,
  resolveGmGridLayout
} from './gm-item-browser.js';
import { focusLoginUsernameIfVisible } from './logout-focus.js';

const api = window.qqspeedLauncher;

const $ = (id) => document.getElementById(id);

const shell = $('app');
const loginView = $('loginView');
const homeView = $('homeView');
const loginForm = $('loginForm');
const username = $('username');
const accountDropdownBtn = $('accountDropdownBtn');
const accountDropdown = $('accountDropdown');
const password = $('password');
const remember = $('remember');
const autoLogin = $('autoLogin');
const loginBtn = $('loginBtn');
const quickLoginBtn = $('quickLoginBtn');
const loginAccountAvatar = $('loginAccountAvatar');
const loginMethodFallback = $('loginMethodFallback');
const accountName = $('accountName');
const homeDockAccountName = $('homeDockAccountName');
const accountAvatar = $('accountAvatar');
const accountAvatarFallback = $('accountAvatarFallback');
const gameRootText = $('gameRootText');
const homeStatusText = $('homeStatusText');
const serverSelect = $('serverSelect');
const serverSelectTrigger = $('serverSelectTrigger');
const serverSelectLabel = $('serverSelectLabel');
const serverModal = $('serverModal');
const serverModalCloseBtn = $('serverModalCloseBtn');
const serverModalCancelBtn = $('serverModalCancelBtn');
const serverModalConfirmBtn = $('serverModalConfirmBtn');
const startBtn = $('startBtn');
const startBtnText = $('startBtnText');
const startBtnDesc = $('startBtnDesc');
const statusBox = $('status');
const agreementCheck = $('agreementCheck');
const homeLinks = $('homeLinks');
const dynamicHomeLinks = $('dynamicHomeLinks');
const homeBannerTrack = $('homeBannerTrack');
const homeBannerDots = $('homeBannerDots');
const homeSecondaryBanner = $('homeSecondaryBanner');
const releaseTimeline = $('releaseTimeline');
const bulletinTabs = $('bulletinTabs');
const homeBulletinList = $('homeBulletinList');
const launcherMenu = $('launcherMenu');
const gmItemBrowserBtn = $('gmItemBrowserBtn');
const homeAnnouncementTicker = $('homeAnnouncementTicker');
const homeActivityBtn = $('homeActivityBtn');
const homeActivityPopover = $('homeActivityPopover');
const homeActivityCloseBtn = $('homeActivityCloseBtn');
const gmItemBrowserModal = $('gmItemBrowserModal');
const gmModalCloseBtn = $('gmModalCloseBtn');
const gmPrimaryCategories = $('gmPrimaryCategories');
const gmFilterForm = $('gmFilterForm');
const gmKeyword = $('gmKeyword');
const gmSecondaryCategories = $('gmSecondaryCategories');
const gmSecondaryCategory = $('gmSecondaryCategory');
const gmGender = $('gmGender');
const gmSortDirection = $('gmSortDirection');
const gmQueryBtn = $('gmQueryBtn');
const gmResetBtn = $('gmResetBtn');
const gmLiveStatus = $('gmLiveStatus');
const gmResultsViewport = $('gmResultsViewport');
const gmItemGrid = $('gmItemGrid');
const gmResultSummary = $('gmResultSummary');
const gmPrevPageBtn = $('gmPrevPageBtn');
const gmNextPageBtn = $('gmNextPageBtn');
const gmPageIndicator = $('gmPageIndicator');
const gmPageSizeLabel = $('gmPageSizeLabel');
const multiOpenBtn = $('multiOpenBtn');
const multiOpenCount = $('multiOpenCount');
const multiInstanceModal = $('multiInstanceModal');
const multiInstanceCloseBtn = $('multiInstanceCloseBtn');
const multiMainAccount = $('multiMainAccount');
const multiInstanceList = $('multiInstanceList');
const bindAccountBtn = $('bindAccountBtn');
const bindAccountForm = $('bindAccountForm');
const bindAccountUser = $('bindAccountUser');
const bindAccountPassword = $('bindAccountPassword');
const bindAccountCancelBtn = $('bindAccountCancelBtn');
const bindAccountSubmitBtn = $('bindAccountSubmitBtn');
const closeConfirmModal = $('closeConfirmModal');
const closeConfirmDismiss = $('closeConfirmDismiss');
const closeActionTray = $('closeActionTray');
const closeActionQuit = $('closeActionQuit');
const closeRemember = $('closeRemember');
const closeConfirmBtn = $('closeConfirmBtn');

function getMediaStorage() {
  try {
    return window.localStorage;
  } catch {
    return null;
  }
}

function getClosePreferenceStorage() {
  try {
    return window.localStorage;
  } catch {
    return null;
  }
}

const mediaController = new MediaController(document, getMediaStorage());

let currentState = null;
let currentContent = null;
let contentFingerprint = '';
let bannerTimer = null;
let bannerResizeFrame = null;
let launcherState = createOfficialLauncherState();
let lastNativeStatus = null;
let lastHomeRefreshAt = 0;
let currentInstanceSnapshot = {
  starting: false,
  runningCount: 0,
  instances: [],
  accounts: [],
  servers: [{ id: '64', label: '电信区', available: true }],
  maxBoundAccounts: 10
};
let multiActionBusy = false;
const gmBrowserState = {
  open: false,
  busy: false,
  primaryCategory: '',
  secondaryCategory: '',
  gender: '',
  keyword: '',
  sortDirection: 'asc',
  page: 1,
  pageSize: 30,
  total: 0,
  pageCount: 0,
  requestSequence: 0
};
let gmCatalogState = { categories: [], subcategories: [], genders: [] };
const contentRequests = new AuthSessionRequestGate();
const HOME_REFRESH_THROTTLE_MS = 5 * 60 * 1000;

function openCloseDialog() {
  if (!closeConfirmModal.classList.contains('hidden')) {
    closeActionTray.focus();
    return;
  }
  closeActionTray.checked = true;
  closeActionQuit.checked = false;
  closeRemember.checked = false;
  closeConfirmBtn.disabled = false;
  closeConfirmModal.classList.remove('hidden');
  closeActionTray.focus();
}

function dismissCloseDialog() {
  if (closeConfirmModal.classList.contains('hidden')) {
    return;
  }
  closeConfirmModal.classList.add('hidden');
  closeConfirmBtn.disabled = false;
  $('closeBtn').focus();
}

async function applyLauncherCloseAction(action) {
  try {
    await api.applyCloseAction(action);
  } catch (error) {
    openCloseDialog();
    setStatus(error?.message || '暂时无法关闭启动器，请重试。', 'error');
  }
}

async function confirmLauncherClose() {
  const action = closeActionQuit.checked ? CLOSE_ACTION_QUIT : CLOSE_ACTION_TRAY;
  closeConfirmBtn.disabled = true;

  if (closeRemember.checked) {
    saveClosePreference(getClosePreferenceStorage(), action);
  } else {
    resetClosePreference(getClosePreferenceStorage());
  }

  closeConfirmModal.classList.add('hidden');
  await applyLauncherCloseAction(action);
}

function requestLauncherClose() {
  const preference = readClosePreference(getClosePreferenceStorage());
  if (preference.skipPrompt) {
    void applyLauncherCloseAction(preference.action);
    return;
  }
  openCloseDialog();
}

function restoreClosePrompt() {
  resetClosePreference(getClosePreferenceStorage());
  dismissCloseDialog();
  setStatus('关闭提示已恢复。', 'ok');
}

function selectBanner(index) {
  const cards = [...homeBannerTrack.querySelectorAll('.home-banner-card')];
  const dots = [...homeBannerDots.querySelectorAll('button[data-banner-index]')];
  if (!cards.length) {
    return;
  }
  const activeIndex = ((Number(index) || 0) % cards.length + cards.length) % cards.length;
  cards.forEach((card, cardIndex) => card.classList.toggle('active', cardIndex === activeIndex));
  dots.forEach((dot, dotIndex) => {
    const active = dotIndex === activeIndex;
    dot.classList.toggle('active', active);
    dot.setAttribute('aria-pressed', String(active));
  });
  homeBannerTrack.dataset.activeIndex = String(activeIndex);
  const renderedCardWidth = cards[0].getBoundingClientRect().width;
  const bannerWidth = renderedCardWidth > 0
    ? renderedCardWidth
    : homeBannerTrack.parentElement?.getBoundingClientRect().width || 500;
  homeBannerTrack.style.transform = `translate3d(-${activeIndex * bannerWidth}px, 0, 0)`;
}

function scheduleBannerGeometryRefresh() {
  if (bannerResizeFrame !== null) {
    window.cancelAnimationFrame(bannerResizeFrame);
  }
  bannerResizeFrame = window.requestAnimationFrame(() => {
    bannerResizeFrame = null;
    selectBanner(Number(homeBannerTrack.dataset.activeIndex) || 0);
  });
}

function openHomeActivityPopover() {
  if (shell.dataset.view !== 'home') return;
  homeActivityPopover.classList.remove('hidden');
  homeActivityBtn.setAttribute('aria-expanded', 'true');
  homeAnnouncementTicker.setAttribute('aria-expanded', 'true');
  scheduleBannerGeometryRefresh();
  homeActivityCloseBtn.focus();
}

function closeHomeActivityPopover({ restoreFocus = false } = {}) {
  if (homeActivityPopover.classList.contains('hidden')) return;
  homeActivityPopover.classList.add('hidden');
  homeActivityBtn.setAttribute('aria-expanded', 'false');
  homeAnnouncementTicker.setAttribute('aria-expanded', 'false');
  if (restoreFocus && shell.dataset.view === 'home') homeActivityBtn.focus();
}

function toggleHomeActivityPopover() {
  if (homeActivityPopover.classList.contains('hidden')) {
    openHomeActivityPopover();
  } else {
    closeHomeActivityPopover({ restoreFocus: true });
  }
}

function scheduleBannerRotation() {
  if (bannerTimer) {
    window.clearInterval(bannerTimer);
    bannerTimer = null;
  }
  const cards = [...homeBannerTrack.querySelectorAll('.home-banner-card')];
  homeBannerDots.replaceChildren();
  if (!cards.length) {
    return;
  }
  const fragment = document.createDocumentFragment();
  cards.forEach((card, index) => {
    const dot = document.createElement('button');
    dot.type = 'button';
    dot.dataset.bannerIndex = String(index);
    dot.dataset.label = card.dataset.bannerTitle || `活动 ${index + 1}`;
    dot.setAttribute('aria-label', `切换到活动 ${index + 1}`);
    dot.setAttribute('aria-pressed', String(index === 0));
    dot.classList.toggle('active', index === 0);
    fragment.appendChild(dot);
  });
  homeBannerDots.appendChild(fragment);
  selectBanner(0);
  if (cards.length < 2) {
    return;
  }
  bannerTimer = window.setInterval(() => {
    const activeIndex = Number(homeBannerTrack.dataset.activeIndex) || 0;
    selectBanner(activeIndex + 1);
  }, 5000);
}

function renderHomeLinks(links) {
  const fragment = document.createDocumentFragment();
  for (const link of Array.isArray(links) ? links : []) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'home-link';
    button.dataset.linkId = link.id;
    button.dataset.linkUrl = link.url;
    button.textContent = link.label;
    fragment.appendChild(button);
  }
  dynamicHomeLinks.replaceChildren(fragment);
}

function applyLauncherContent(value) {
  const normalized = normalizeLauncherContent(value);
  const fingerprint = JSON.stringify(normalized);
  if (fingerprint === contentFingerprint) {
    return currentContent;
  }
  contentFingerprint = fingerprint;
  currentContent = renderLauncherContent(document, normalized);
  renderHomeLinks(currentContent.links);
  scheduleBannerRotation();
  return currentContent;
}

function hasAuthenticatedLauncherSession() {
  return currentState?.session?.authenticated === true
    || launcherState.auth.phase === 'authenticated';
}

function renderLauncherState() {
  const view = selectLauncherView(launcherState);
  shell.dataset.view = view;
  mediaController.setView(view);
  loginView.classList.toggle('hidden', view !== 'login');
  homeView.classList.toggle('hidden', view !== 'home');
  if (view === 'home') {
    scheduleBannerGeometryRefresh();
  }
  gmItemBrowserBtn.classList.toggle(
    'hidden',
    view !== 'home' || !hasAuthenticatedLauncherSession()
  );
  if (view !== 'home') closeHomeActivityPopover();
  if (view !== 'home' && gmBrowserState.open) {
    closeGmItemBrowser({ reset: true, restoreFocus: false });
  }

  const gameButton = selectGameButton(launcherState);
  const progress = Math.max(0, Math.min(100, Number(lastNativeStatus?.progress) || 0));
  setStartButtonState({
    title: gameButton.label,
    state: gameButton.action,
    disabled: gameButton.disabled,
    progress
  });
  loginBtn.disabled = ['booting', 'authenticating', 'switching'].includes(
    launcherState.auth.phase
  );
}

function transitionLauncher(event, { scheduleEffects = true } = {}) {
  launcherState = officialLauncherReducer(launcherState, event);
  renderLauncherState();
  if (scheduleEffects) {
    scheduleContentEffects();
  }
  return launcherState;
}

function scheduleContentEffects() {
  for (const effect of selectContentEffects(launcherState)) {
    if (effect === 'load-bootstrap') {
      void requestLauncherContent('bootstrap');
    } else if (effect === 'load-home') {
      void requestLauncherContent('home');
    }
  }
}

async function requestLauncherContent(scope, { refresh = false } = {}) {
  const normalizedScope = scope === 'home' ? 'home' : 'bootstrap';
  if (contentRequests.has(normalizedScope)) {
    return contentRequests.get(normalizedScope);
  }
  if (typeof api?.getLauncherContent !== 'function') {
    return currentContent;
  }
  if (normalizedScope === 'home') {
    lastHomeRefreshAt = Date.now();
  }
  const requestEpoch = contentRequests.capture(normalizedScope);

  const request = Promise.resolve()
    .then(() => normalizedScope === 'home'
      ? api.getLauncherContent('home', { refresh })
      : api.getLauncherContent('bootstrap', { refresh }))
    .then((response) => {
      if (!contentRequests.isCurrent(normalizedScope, requestEpoch)) {
        return currentContent;
      }
      if (response?.ok === false) {
        throw new Error(response.error || '内容服务暂不可用');
      }
      const payload = response?.data ?? response?.content ?? response;
      if (launcherState.content.scope !== normalizedScope) {
        return currentContent;
      }
      const rendered = applyLauncherContent(payload);
      transitionLauncher({
        type: normalizedScope === 'home'
          ? 'HOME_CONTENT_READY'
          : 'BOOTSTRAP_CONTENT_READY',
        data: payload
      }, { scheduleEffects: false });
      return rendered;
    })
    .catch((error) => {
      if (!contentRequests.isCurrent(normalizedScope, requestEpoch)) {
        return currentContent;
      }
      if (launcherState.content.scope === normalizedScope) {
        transitionLauncher({
          type: normalizedScope === 'home'
            ? 'HOME_CONTENT_FAILED'
            : 'BOOTSTRAP_CONTENT_FAILED',
          error: error?.message || String(error)
        }, { scheduleEffects: false });
      }
      return currentContent;
    })
    .finally(() => {
      contentRequests.deleteIfCurrentRequest(normalizedScope, request);
    });
  contentRequests.set(normalizedScope, request);
  return request;
}

function refreshHomeIfDue() {
  if (
    launcherState.auth.phase !== 'authenticated' ||
    launcherState.content.scope !== 'home' ||
    contentRequests.has('home') ||
    Date.now() - lastHomeRefreshAt < HOME_REFRESH_THROTTLE_MS
  ) {
    return;
  }

  if (launcherState.content.status === 'ready') {
    transitionLauncher({ type: 'HOME_CONTENT_STALE' }, { scheduleEffects: false });
  } else if (launcherState.content.status === 'error') {
    transitionLauncher({ type: 'HOME_CONTENT_RETRY' }, { scheduleEffects: false });
  } else {
    return;
  }
  void requestLauncherContent('home', { refresh: true });
}

applyLauncherContent({});
void mediaController.playBackgroundVideo();

function setStatus(text, type = 'info') {
  const message = toCustomerStatusMessage(text, type, '');
  statusBox.textContent = message;
  statusBox.dataset.type = type;
  if (homeStatusText) {
    homeStatusText.textContent = message || '账号已就绪，可以开始游戏。';
  }
}

function showLoginAccountAvatarFallback() {
  loginAccountAvatar.classList.add('hidden');
  loginMethodFallback.classList.remove('hidden');
}

function setLoginAccountAvatar(user) {
  const avatarUrl = sanitizeAccountAvatarUrl(createQqAvatarUrl(user));
  loginAccountAvatar.dataset.expectedSrc = avatarUrl;
  loginAccountAvatar.alt = avatarUrl ? `${String(user).trim()} 的账号头像` : '账号头像';
  if (!avatarUrl) {
    loginAccountAvatar.removeAttribute('src');
    showLoginAccountAvatarFallback();
    return;
  }
  showLoginAccountAvatarFallback();
  loginAccountAvatar.src = avatarUrl;
}

loginAccountAvatar.addEventListener('load', () => {
  if (
    !loginAccountAvatar.dataset.expectedSrc ||
    loginAccountAvatar.src !== loginAccountAvatar.dataset.expectedSrc
  ) {
    return;
  }
  loginMethodFallback.classList.add('hidden');
  loginAccountAvatar.classList.remove('hidden');
});

loginAccountAvatar.addEventListener('error', showLoginAccountAvatarFallback);

function showAccountAvatarFallback() {
  accountAvatar.classList.add('hidden');
  accountAvatarFallback.classList.remove('hidden');
}

function setAccountAvatar(value, user = '') {
  const avatarUrl = sanitizeAccountAvatarUrl(value);
  accountAvatar.dataset.expectedSrc = avatarUrl;
  accountAvatar.alt = user ? `${user} 的账号头像` : '账号头像';
  if (!avatarUrl) {
    accountAvatar.removeAttribute('src');
    showAccountAvatarFallback();
    return;
  }
  showAccountAvatarFallback();
  accountAvatar.src = avatarUrl;
}

accountAvatar.addEventListener('load', () => {
  if (!accountAvatar.dataset.expectedSrc || accountAvatar.src !== accountAvatar.dataset.expectedSrc) {
    return;
  }
  accountAvatarFallback.classList.add('hidden');
  accountAvatar.classList.remove('hidden');
});

accountAvatar.addEventListener('error', showAccountAvatarFallback);

function setRenderedAccountName(value) {
  const label = String(value || '').trim() || '未登录';
  accountName.textContent = label;
  homeDockAccountName.textContent = label;
}

function applyAccountIdentity(session, fallbackUser = '') {
  const authenticated = session?.authenticated === true;
  const user = authenticated ? String(session?.user || fallbackUser || '').trim() : '';
  setRenderedAccountName(user || (authenticated ? '已登录' : '未登录'));
  setAccountAvatar(authenticated ? session?.avatarUrl : '', user);
}

async function openOfficialLink(label, url) {
  if (!url || typeof api?.openExternal !== 'function') {
    setStatus(`${label}入口当前没有可用地址。`, 'info');
    return;
  }
  try {
    const result = await api.openExternal(url);
    if (result?.ok === false) {
      throw new Error(result.error || '主进程拒绝打开此地址');
    }
    setStatus(`已在系统浏览器打开${label}。`, 'ok');
  } catch (error) {
    setStatus(error.message || String(error), 'error');
  }
}

function setStartButtonState({
  title = '加载中',
  state = 'loading',
  disabled = true,
  progress = 0
} = {}) {
  startBtnText.textContent = title;
  startBtnDesc.textContent = '';
  startBtn.dataset.state = state;
  startBtn.dataset.action = state;
  startBtn.disabled = disabled;
  startBtn.style.setProperty('--start-progress', `${Math.max(0, Math.min(100, Number(progress) || 0))}%`);
}

function synchronizeAuthoritativeSession(session) {
  contentRequests.advanceAuthSession();
  const authenticated = session?.authenticated === true;
  if (authenticated) {
    if (launcherState.auth.phase === 'authenticating') {
      transitionLauncher({ type: 'AUTH_SUCCEEDED' });
    } else if (['booting', 'login-ready', 'error'].includes(launcherState.auth.phase)) {
      transitionLauncher({ type: 'SESSION_RESTORED' });
    } else {
      renderLauncherState();
    }
    applyAccountIdentity(session, currentState?.login?.user);
    return;
  }

  if (launcherState.auth.phase === 'booting') {
    transitionLauncher({ type: 'BOOT_READY' });
  } else if (launcherState.auth.phase === 'authenticated') {
    transitionLauncher({ type: 'SWITCH_ACCOUNT_REQUESTED' });
    transitionLauncher({ type: 'SWITCH_ACCOUNT_COMPLETED' });
  } else if (launcherState.auth.phase === 'switching') {
    transitionLauncher({ type: 'SWITCH_ACCOUNT_COMPLETED' });
  } else {
    renderLauncherState();
  }
  applyAccountIdentity(session);
}

function beginAuthentication() {
  if (launcherState.auth.phase === 'error') {
    transitionLauncher({ type: 'AUTH_RETRY' });
  }
  if (launcherState.auth.phase !== 'login-ready') {
    throw new Error('登录器尚未进入可登录状态。');
  }
  transitionLauncher({ type: 'AUTH_REQUESTED' });
}

function failAuthentication(error) {
  if (launcherState.auth.phase === 'authenticating') {
    transitionLauncher({
      type: 'AUTH_FAILED',
      error: toCustomerErrorMessage(error, '登录失败，请重试。')
    });
  }
}

function applyLaunchStatus(status) {
  if (!status || typeof status !== 'object') {
    return;
  }

  const phase = String(status.phase || '').toLowerCase();
  const type = status.error || ['failed', 'error'].includes(phase)
    ? 'error'
    : ['preparing', 'preloading', 'authenticating', 'starting'].includes(phase)
      ? 'loading'
      : ['authenticated', 'ready', 'preloaded'].includes(phase)
        ? 'ok'
        : 'info';
  const customerStatus = {
    ...status,
    message: toCustomerStatusMessage(status.message, type, ''),
    ...(status.error ? { error: toCustomerErrorMessage(status.error) } : {})
  };
  lastNativeStatus = customerStatus;
  const message = customerStatus.message || '';
  const gameProcessId = Math.max(0, Number(customerStatus.gameProcessId ?? customerStatus.pid) || 0);
  const pidText = gameProcessId ? ` PID=${gameProcessId}` : '';
  launcherState = reduceNativeLaunchStatus(launcherState, customerStatus, {
    hasGameRoot: Boolean(currentState?.paths?.gameRoot)
  });
  renderLauncherState();

  if (isRoutineLoginPreloadStatus(status, launcherState.auth.phase)) {
    setStatus('', 'info');
    return;
  }

  if (message) {
    setStatus(`${message}${pidText}`, type);
  }
}

function knownAccounts() {
  return Array.isArray(currentState?.login?.accounts) ? currentState.login.accounts : [];
}

function findKnownAccount(user) {
  const normalized = String(user || '').trim();
  return knownAccounts().find((account) => account.user === normalized) || null;
}

function setPasswordHint(hasPassword) {
  password.placeholder = hasPassword ? '已记住密码，可直接登录' : '请输入密码';
}

function findRememberedAccount() {
  const selected = findKnownAccount(username.value);
  return selected?.hasPassword ? selected : knownAccounts().find((account) => account.hasPassword) || null;
}

function updateQuickLoginAvailability() {
  quickLoginBtn.disabled = !findRememberedAccount();
}

function renderAccountDropdown(accounts = knownAccounts()) {
  accountDropdown.replaceChildren();
  accountDropdownBtn.classList.toggle('hidden', accounts.length === 0);

  if (!accounts.length) {
    const empty = document.createElement('div');
    empty.className = 'account-dropdown-empty';
    empty.textContent = '暂无登录过的账号';
    accountDropdown.appendChild(empty);
    return;
  }

  for (const account of accounts) {
    const item = document.createElement('button');
    item.type = 'button';
    item.className = 'account-dropdown-item';
    item.dataset.user = account.user;

    const user = document.createElement('span');
    user.textContent = account.user;
    item.appendChild(user);

    const state = document.createElement('em');
    state.textContent = account.hasPassword ? '已记住' : '需输入密码';
    item.appendChild(state);

    accountDropdown.appendChild(item);
  }
}

function selectKnownAccount(account) {
  if (!account) {
    return;
  }
  username.value = account.user;
  password.value = '';
  remember.checked = Boolean(account.hasPassword);
  autoLogin.checked = Boolean(
    account.hasPassword &&
    account.user === currentState?.login?.user &&
    currentState?.login?.autoLogin
  );
  setPasswordHint(Boolean(account.hasPassword));
  setLoginAccountAvatar(account.user);
  setRenderedAccountName(account.user);
  accountDropdown.classList.add('hidden');
  updateQuickLoginAvailability();
}

function applyState(state, { preserveLoginForm = false } = {}) {
  currentState = state;
  const login = state.login || {};
  const paths = state.paths || {};
  const accounts = Array.isArray(login.accounts) ? login.accounts : [];
  const selectedUser = login.user || username.value || accounts[0]?.user || '';
  const selectedAccount = accounts.find((account) => account.user === selectedUser);

  shell.dataset.gameReady = paths.gameRoot ? '1' : '0';
  if (!preserveLoginForm) {
    username.value = selectedUser;
    remember.checked = Boolean(selectedAccount?.hasPassword ?? login.hasPassword);
    autoLogin.checked = Boolean(login.autoLogin && (selectedAccount?.hasPassword ?? login.hasPassword));
    setPasswordHint(Boolean(selectedAccount?.hasPassword ?? login.hasPassword));
    setLoginAccountAvatar(selectedUser);
  }
  renderAccountDropdown(accounts);
  updateQuickLoginAvailability();
  if (!preserveLoginForm) {
    const authenticated = state.session?.authenticated === true
      || (state.session === undefined && launcherState.auth.phase === 'authenticated');
    applyAccountIdentity({
      authenticated,
      user: state.session?.user || login.user || username.value,
      avatarUrl: state.session?.avatarUrl || ''
    }, login.user || username.value);
  }

  if (paths.gameRoot) {
    const displayPath = paths.gameSubDir ? `${paths.gameRoot}  →  ${paths.gameSubDir}` : paths.gameRoot;
    gameRootText.textContent = displayPath;
    gameRootText.title = paths.gameAppPath || paths.gameRoot;
  } else {
    gameRootText.textContent = '未定位 QQ飞车主目录';
    gameRootText.title = '';
  }
  if (state.instances) {
    renderMultiInstanceSnapshot(state.instances);
  }
}

function appendTextElement(documentRef, parent, tagName, className, text) {
  const element = documentRef.createElement(tagName);
  if (className) element.className = className;
  element.textContent = text;
  parent.appendChild(element);
  return element;
}

function createMultiAccountCard(account, snapshot) {
  const card = document.createElement('article');
  card.className = 'multi-account-card';
  card.dataset.user = account.user;
  card.classList.toggle('is-main', account.main === true);

  const identity = document.createElement('div');
  identity.className = 'multi-account-identity';
  const avatar = appendTextElement(document, identity, 'span', 'multi-account-avatar', String(account.user || '?').slice(-1));
  avatar.setAttribute('aria-hidden', 'true');
  const copy = document.createElement('div');
  copy.className = 'multi-account-copy';
  appendTextElement(document, copy, 'strong', '', `${account.user}（QQ）`);
  appendTextElement(document, copy, 'em', '', account.main ? '当前登录账号' : '账号密码绑定');
  const instances = Array.isArray(account.instances) ? account.instances : [];
  const running = instances.filter((instance) => instance.phase === 'running');
  const starting = instances.find((instance) => instance.phase === 'starting');
  if (starting) {
    appendTextElement(document, copy, 'small', 'multi-account-state is-starting', `${starting.zoneLabel}启动中`);
  } else if (running.length) {
    appendTextElement(document, copy, 'small', 'multi-account-state is-running', `运行中：${running.map((instance) => instance.zoneLabel).join('、')}`);
  } else {
    appendTextElement(document, copy, 'small', 'multi-account-state', '未启动任何区服');
  }
  identity.appendChild(copy);
  card.appendChild(identity);

  const controls = document.createElement('div');
  controls.className = 'multi-account-controls';
  const select = document.createElement('select');
  select.className = 'multi-zone-select';
  select.setAttribute('aria-label', `${account.user} 选择区服`);
  for (const server of Array.isArray(snapshot.servers) ? snapshot.servers : []) {
    if (server.available === false) continue;
    const option = document.createElement('option');
    option.value = server.id;
    option.textContent = server.label;
    select.appendChild(option);
  }
  if (!select.options.length) {
    const option = document.createElement('option');
    option.value = '64';
    option.textContent = '电信区';
    select.appendChild(option);
  }
  controls.appendChild(select);

  const selectedZoneInUse = instances.some(
    (instance) => instance.zone === select.value && ['starting', 'running'].includes(instance.phase)
  );
  const start = appendTextElement(document, controls, 'button', 'multi-instance-start', starting ? '启动中' : running.length ? '启动' : '启动');
  start.type = 'button';
  start.dataset.multiStart = '1';
  start.disabled = multiActionBusy || snapshot.starting === true || selectedZoneInUse || (!account.hasPassword && !account.main);
  if (!account.main) {
    const unbind = appendTextElement(document, controls, 'button', 'multi-account-unbind', '解绑');
    unbind.type = 'button';
    unbind.dataset.multiUnbind = '1';
    unbind.disabled = multiActionBusy || instances.some((instance) => ['starting', 'running'].includes(instance.phase));
  }
  card.appendChild(controls);

  select.addEventListener('change', () => {
    const inUse = instances.some(
      (instance) => instance.zone === select.value && ['starting', 'running'].includes(instance.phase)
    );
    start.disabled = multiActionBusy || snapshot.starting === true || inUse || (!account.hasPassword && !account.main);
  });
  return card;
}

function renderMultiInstanceSnapshot(value) {
  const snapshot = value && typeof value === 'object' ? value : {};
  currentInstanceSnapshot = {
    starting: snapshot.starting === true,
    runningCount: Math.max(0, Number(snapshot.runningCount) || 0),
    instances: Array.isArray(snapshot.instances) ? snapshot.instances : [],
    accounts: Array.isArray(snapshot.accounts) ? snapshot.accounts : [],
    servers: Array.isArray(snapshot.servers) && snapshot.servers.length
      ? snapshot.servers
      : [{ id: '64', label: '电信区', available: true }],
    maxBoundAccounts: Math.max(1, Number(snapshot.maxBoundAccounts) || 10)
  };

  multiOpenCount.textContent = String(currentInstanceSnapshot.runningCount);
  multiOpenCount.classList.toggle('hidden', currentInstanceSnapshot.runningCount === 0);

  const main = currentInstanceSnapshot.accounts.find((account) => account.main === true);
  if (main) {
    multiMainAccount.replaceChildren(createMultiAccountCard(main, currentInstanceSnapshot));
  } else {
    const empty = appendTextElement(document, document.createDocumentFragment(), 'p', 'multi-instance-empty', '请先登录主账号。');
    multiMainAccount.replaceChildren(empty);
  }

  const fragment = document.createDocumentFragment();
  const boundAccounts = currentInstanceSnapshot.accounts.filter((account) => account.main !== true);
  for (const account of boundAccounts) {
    fragment.appendChild(createMultiAccountCard(account, currentInstanceSnapshot));
  }
  if (!boundAccounts.length) {
    appendTextElement(document, fragment, 'p', 'multi-instance-empty', '暂未绑定多开账号');
  }
  multiInstanceList.replaceChildren(fragment);
  bindAccountBtn.disabled = multiActionBusy ||
    boundAccounts.length >= currentInstanceSnapshot.maxBoundAccounts ||
    currentInstanceSnapshot.starting;
  bindAccountSubmitBtn.disabled = multiActionBusy;
}

async function refreshMultiInstances() {
  if (typeof api?.getInstances !== 'function') return currentInstanceSnapshot;
  const snapshot = await api.getInstances();
  renderMultiInstanceSnapshot(snapshot);
  return currentInstanceSnapshot;
}

async function openMultiInstanceModal() {
  multiInstanceModal.classList.remove('hidden');
  multiOpenBtn.setAttribute('aria-expanded', 'true');
  try {
    await refreshMultiInstances();
  } catch (error) {
    setStatus(error.message || String(error), 'error');
  }
  multiInstanceCloseBtn.focus();
}

function closeMultiInstanceModal() {
  multiInstanceModal.classList.add('hidden');
  multiOpenBtn.setAttribute('aria-expanded', 'false');
  bindAccountForm.classList.add('hidden');
  bindAccountPassword.value = '';
  multiOpenBtn.focus();
}

async function refreshState({ synchronizeSession = true } = {}) {
  const state = await api.getState();
  applyState(state);
  if (synchronizeSession) {
    synchronizeAuthoritativeSession(state.session);
  }
  if (state.launch?.phase) {
    applyLaunchStatus(state.launch);
  } else {
    launcherState = reduceNativeLaunchStatus(launcherState, { phase: 'idle' }, {
      hasGameRoot: Boolean(state.paths?.gameRoot)
    });
    renderLauncherState();
  }
  return state;
}

async function prepareGame({ preserveLoginForm = false } = {}) {
  const result = await api.prepareGame();
  applyState(result.state, { preserveLoginForm });
  if (!result.ready) {
    applyLaunchStatus({
      phase: result.state?.paths?.gameRoot ? 'failed' : 'idle',
      progress: 0,
      message: result.message
    });
    setStatus(result.message, 'warn');
    return false;
  }
  setStatus(`${result.message} 主目录：${result.gameRoot}`, 'ok');
  return true;
}

async function preloadGame() {
  setStatus('正在后台预加载 GameApp；此时不会进入游戏。', 'loading');
  const result = await api.preloadGame();
  applyLaunchStatus(result);
  return ['preloaded', 'authenticated'].includes(String(result?.phase || '').toLowerCase());
}

async function selectGameRoot() {
  try {
    const state = await api.selectGameRoot();
    applyState(state);
    launcherState = reduceNativeLaunchStatus(launcherState, { phase: 'idle' }, {
      hasGameRoot: Boolean(state.paths?.gameRoot)
    });
    renderLauncherState();
    if (state.paths?.gameRoot) {
      setStatus(`QQ飞车主目录已选择：${state.paths.gameRoot}`, 'ok');
      if (await prepareGame()) {
        await preloadGame();
      }
    } else {
      setStatus('请选择 QQ飞车主目录，目录下必须有 Releasephysx27_64\\GameApp.exe。', 'warn');
    }
  } catch (error) {
    setStatus(error.message || String(error), 'error');
  }
}

function closeLauncherMenu() {
  launcherMenu.classList.add('hidden');
  $('homeMenuBtn').setAttribute('aria-expanded', 'false');
}

function toggleLauncherMenu(event) {
  event?.preventDefault();
  event?.stopPropagation();
  const opening = launcherMenu.classList.contains('hidden');
  launcherMenu.classList.toggle('hidden', !opening);
  $('homeMenuBtn').setAttribute('aria-expanded', String(opening));
}

async function selectGameRootFromMenu() {
  closeLauncherMenu();
  await selectGameRoot();
}

function setGmLiveStatus(message = '', type = 'info') {
  gmLiveStatus.textContent = message;
  gmLiveStatus.dataset.type = type;
}

function setGmBrowserBusy(busy) {
  gmBrowserState.busy = Boolean(busy);
  gmQueryBtn.disabled = gmBrowserState.busy;
  gmResetBtn.disabled = gmBrowserState.busy;
  gmKeyword.disabled = gmBrowserState.busy;
  gmSecondaryCategory.disabled = gmBrowserState.busy || !gmBrowserState.primaryCategory;
  gmGender.disabled = gmBrowserState.busy;
  gmSortDirection.disabled = gmBrowserState.busy;
  for (const button of gmPrimaryCategories.querySelectorAll('button')) {
    button.disabled = gmBrowserState.busy;
  }
  for (const button of gmSecondaryCategories.querySelectorAll('button')) {
    button.disabled = gmBrowserState.busy || !gmBrowserState.primaryCategory;
  }
}

function resetGmItemBrowserState() {
  gmBrowserState.primaryCategory = '';
  gmBrowserState.secondaryCategory = '';
  gmBrowserState.gender = '';
  gmBrowserState.keyword = '';
  gmBrowserState.sortDirection = 'asc';
  gmBrowserState.page = 1;
  gmBrowserState.total = 0;
  gmBrowserState.pageCount = 0;
  gmKeyword.value = '';
  gmGender.value = '';
  gmSortDirection.value = 'asc';
  gmCatalogState = { categories: [], subcategories: [], genders: [] };
}

function closeGmItemBrowser({ reset = false, restoreFocus = true } = {}) {
  gmBrowserState.requestSequence += 1;
  gmBrowserState.open = false;
  setGmBrowserBusy(false);
  gmItemBrowserModal.classList.add('hidden');
  setGmLiveStatus('');
  if (reset) {
    resetGmItemBrowserState();
    gmPrimaryCategories.replaceChildren();
    gmItemGrid.replaceChildren();
    renderGmPagination();
    renderGmSecondaryCategories([]);
  }
  if (restoreFocus && !gmItemBrowserBtn.classList.contains('hidden')) {
    gmItemBrowserBtn.focus();
  }
}

function createGmCategoryButton(value, label, count) {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'gm-primary-category';
  button.dataset.gmPrimary = value;
  button.classList.toggle('is-active', gmBrowserState.primaryCategory === value);
  button.setAttribute('aria-pressed', String(gmBrowserState.primaryCategory === value));

  const text = document.createElement('span');
  text.textContent = label;
  button.appendChild(text);
  const countNode = document.createElement('small');
  countNode.textContent = String(Math.max(0, Number(count) || 0));
  button.appendChild(countNode);
  return button;
}

function renderGmPrimaryCategories(categories = gmCatalogState.categories) {
  const rows = Array.isArray(categories) ? categories : [];
  const total = rows.reduce((sum, entry) => sum + Math.max(0, Number(entry?.count) || 0), 0);
  const fragment = document.createDocumentFragment();
  fragment.appendChild(createGmCategoryButton('', '全部', total));
  for (const entry of rows) {
    const value = String(entry?.value || '').trim();
    if (!value) continue;
    fragment.appendChild(createGmCategoryButton(value, String(entry?.label || value), entry?.count));
  }
  gmPrimaryCategories.replaceChildren(fragment);
  setGmBrowserBusy(gmBrowserState.busy);
}

function createGmSecondaryButton(value, label, count) {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'gm-secondary-category';
  button.dataset.gmSecondary = value;
  button.classList.toggle('is-active', gmBrowserState.secondaryCategory === value);
  button.setAttribute('aria-pressed', String(gmBrowserState.secondaryCategory === value));
  button.textContent = label;
  if (Number.isFinite(Number(count))) {
    const countNode = document.createElement('small');
    countNode.textContent = String(Math.max(0, Number(count) || 0));
    button.appendChild(countNode);
  }
  return button;
}

function renderGmSecondaryCategories(subcategories = gmCatalogState.subcategories) {
  const optionFragment = document.createDocumentFragment();
  const buttonFragment = document.createDocumentFragment();
  const allLabel = gmBrowserState.primaryCategory
    ? `全部${gmBrowserState.primaryCategory}`
    : '全部二级分类';
  const allOption = document.createElement('option');
  allOption.value = '';
  allOption.textContent = allLabel;
  optionFragment.appendChild(allOption);
  buttonFragment.appendChild(createGmSecondaryButton('', allLabel));

  for (const entry of Array.isArray(subcategories) ? subcategories : []) {
    const value = String(entry?.value || '').trim();
    if (!value) continue;
    const label = formatGmSubcategoryLabel(entry?.label || value);
    const option = document.createElement('option');
    option.value = value;
    option.textContent = label;
    optionFragment.appendChild(option);
    buttonFragment.appendChild(createGmSecondaryButton(value, label, entry?.count));
  }

  gmSecondaryCategory.replaceChildren(optionFragment);
  const available = [...gmSecondaryCategory.options].some(
    (option) => option.value === gmBrowserState.secondaryCategory
  );
  if (!available) gmBrowserState.secondaryCategory = '';
  gmSecondaryCategory.value = gmBrowserState.secondaryCategory;
  gmSecondaryCategory.disabled = gmBrowserState.busy || !gmBrowserState.primaryCategory;
  gmSecondaryCategories.replaceChildren(buttonFragment);
  gmSecondaryCategories.classList.toggle('is-disabled', !gmBrowserState.primaryCategory);
  for (const button of gmSecondaryCategories.querySelectorAll('button')) {
    const selected = button.dataset.gmSecondary === gmBrowserState.secondaryCategory;
    button.classList.toggle('is-active', selected);
    button.setAttribute('aria-pressed', String(selected));
  }
  setGmBrowserBusy(gmBrowserState.busy);
}

function renderGmPagination() {
  gmResultSummary.textContent = `共 ${Math.max(0, Number(gmBrowserState.total) || 0)} 条`;
  gmPageSizeLabel.textContent = `${gmBrowserState.pageSize} 条/页`;
  gmPageIndicator.textContent = gmBrowserState.pageCount > 0
    ? `${gmBrowserState.page} / ${gmBrowserState.pageCount}`
    : '0 / 0';
  gmPrevPageBtn.disabled = gmBrowserState.busy || gmBrowserState.page <= 1;
  gmNextPageBtn.disabled = gmBrowserState.busy
    || gmBrowserState.pageCount === 0
    || gmBrowserState.page >= gmBrowserState.pageCount;
}

function createGmItemCard(item) {
  const card = document.createElement('article');
  card.className = 'gm-item-card';
  card.dataset.itemId = String(item.id);

  const imageShell = document.createElement('div');
  imageShell.className = 'gm-item-card-image-shell';
  const imageUrl = createGmItemImageUrl(item.id);
  if (imageUrl) {
    const image = document.createElement('img');
    image.className = 'gm-item-card-image';
    image.alt = `${item.name || item.id} 道具图片`;
    image.loading = 'lazy';
    image.decoding = 'async';
    image.referrerPolicy = 'no-referrer';
    image.draggable = false;
    image.addEventListener('error', () => {
      image.remove();
      imageShell.classList.add('is-missing');
    }, { once: true });
    image.src = imageUrl;
    imageShell.appendChild(image);
  } else {
    imageShell.classList.add('is-missing');
  }
  card.appendChild(imageShell);

  const name = document.createElement('strong');
  name.className = 'gm-item-card-name';
  name.textContent = item.name || '未命名道具';
  name.title = item.name || '未命名道具';
  card.appendChild(name);

  const id = document.createElement('span');
  id.className = 'gm-item-card-id';
  id.textContent = String(item.id);
  card.appendChild(id);

  const category = document.createElement('span');
  category.className = 'gm-item-card-category';
  category.textContent = formatGmSubcategoryLabel(
    item.subcategory || item.secondaryCategory || '未分类'
  );
  category.title = category.textContent;
  card.appendChild(category);

  const copy = document.createElement('button');
  copy.type = 'button';
  copy.className = 'gm-item-copy';
  copy.dataset.gmCopyId = String(item.id);
  copy.textContent = '复制ID';
  card.appendChild(copy);
  return card;
}

function renderGmItems(items) {
  const rows = Array.isArray(items) ? items : [];
  if (!rows.length) {
    const empty = document.createElement('div');
    empty.className = 'gm-item-empty';
    empty.textContent = '没有找到符合条件的道具';
    gmItemGrid.replaceChildren(empty);
    return;
  }
  const fragment = document.createDocumentFragment();
  for (const item of rows) fragment.appendChild(createGmItemCard(item));
  gmItemGrid.replaceChildren(fragment);
}

async function loadGmCatalog(primaryCategory = gmBrowserState.primaryCategory) {
  if (typeof api?.getGmCatalog !== 'function') {
    throw new Error('当前版本未提供 GM 道具查询接口。');
  }
  const requestSequence = ++gmBrowserState.requestSequence;
  const catalog = await api.getGmCatalog(primaryCategory);
  if (!gmBrowserState.open || requestSequence !== gmBrowserState.requestSequence) return false;
  gmCatalogState = {
    categories: Array.isArray(catalog?.categories) ? catalog.categories : [],
    subcategories: Array.isArray(catalog?.subcategories) ? catalog.subcategories : [],
    genders: Array.isArray(catalog?.genders) ? catalog.genders : []
  };
  renderGmPrimaryCategories();
  renderGmSecondaryCategories();
  return true;
}

async function queryGmItems() {
  if (!gmBrowserState.open) return;
  if (typeof api?.queryGmItems !== 'function') {
    setGmLiveStatus('当前版本未提供 GM 道具查询接口。', 'error');
    return;
  }

  gmBrowserState.keyword = gmKeyword.value.trim();
  gmBrowserState.secondaryCategory = gmSecondaryCategory.value;
  gmBrowserState.gender = gmGender.value;
  gmBrowserState.sortDirection = gmSortDirection.value === 'desc' ? 'desc' : 'asc';
  const requestSequence = ++gmBrowserState.requestSequence;
  setGmBrowserBusy(true);
  renderGmPagination();
  setGmLiveStatus('正在查询道具…', 'loading');

  try {
    const result = await api.queryGmItems({
      primaryCategory: gmBrowserState.primaryCategory,
      secondaryCategory: gmBrowserState.secondaryCategory,
      gender: gmBrowserState.gender,
      keyword: gmBrowserState.keyword,
      sortDirection: gmBrowserState.sortDirection,
      page: gmBrowserState.page,
      pageSize: gmBrowserState.pageSize
    });
    if (!gmBrowserState.open || requestSequence !== gmBrowserState.requestSequence) return;
    gmBrowserState.total = Math.max(0, Number(result?.total) || 0);
    gmBrowserState.page = Math.max(1, Number(result?.page) || 1);
    gmBrowserState.pageCount = Math.max(0, Number(result?.pageCount) || 0);
    renderGmItems(result?.items);
    setGmLiveStatus('');
  } catch (error) {
    if (!gmBrowserState.open || requestSequence !== gmBrowserState.requestSequence) return;
    gmBrowserState.total = 0;
    gmBrowserState.pageCount = 0;
    renderGmItems([]);
    setGmLiveStatus(toCustomerErrorMessage(error, '道具查询失败，请重试。'), 'error');
  } finally {
    if (requestSequence === gmBrowserState.requestSequence) {
      setGmBrowserBusy(false);
      renderGmPagination();
    }
  }
}

function updateGmGridLayout({ requery = true } = {}) {
  const width = gmResultsViewport.getBoundingClientRect().width;
  const layout = resolveGmGridLayout(width);
  gmItemGrid.style.setProperty('--gm-columns', String(layout.columns));
  if (gmBrowserState.pageSize === layout.pageSize) {
    renderGmPagination();
    return;
  }
  gmBrowserState.pageSize = layout.pageSize;
  gmBrowserState.page = 1;
  renderGmPagination();
  if (requery && gmBrowserState.open) void queryGmItems();
}

async function openGmItemBrowser() {
  if (!hasAuthenticatedLauncherSession()) {
    setStatus('请先登录后使用 GM 道具查询。', 'warn');
    return;
  }
  closeLauncherMenu();
  gmBrowserState.open = true;
  gmItemBrowserModal.classList.remove('hidden');
  updateGmGridLayout({ requery: false });
  setGmBrowserBusy(true);
  setGmLiveStatus('正在读取道具分类…', 'loading');
  try {
    if (await loadGmCatalog(gmBrowserState.primaryCategory)) {
      await queryGmItems();
      gmKeyword.focus();
    }
  } catch (error) {
    setGmLiveStatus(toCustomerErrorMessage(error, '道具数据库不可用。'), 'error');
  } finally {
    setGmBrowserBusy(false);
    renderGmPagination();
  }
}

async function selectGmPrimaryCategory(value) {
  const normalized = String(value || '').trim();
  if (normalized === gmBrowserState.primaryCategory || gmBrowserState.busy) return;
  gmBrowserState.primaryCategory = normalized;
  gmBrowserState.secondaryCategory = '';
  gmBrowserState.page = 1;
  setGmBrowserBusy(true);
  setGmLiveStatus('正在读取二级分类…', 'loading');
  try {
    if (await loadGmCatalog(normalized)) await queryGmItems();
  } catch (error) {
    setGmLiveStatus(toCustomerErrorMessage(error, '分类加载失败，请重试。'), 'error');
  } finally {
    setGmBrowserBusy(false);
    renderGmPagination();
  }
}

async function resetGmItemBrowser() {
  if (gmBrowserState.busy) return;
  resetGmItemBrowserState();
  updateGmGridLayout({ requery: false });
  setGmBrowserBusy(true);
  try {
    if (await loadGmCatalog('')) await queryGmItems();
  } catch (error) {
    setGmLiveStatus(toCustomerErrorMessage(error, '道具查询重置失败。'), 'error');
  } finally {
    setGmBrowserBusy(false);
    renderGmPagination();
  }
}

async function copyGmItemId(value, button) {
  const text = String(value || '').trim();
  if (!/^\d+$/u.test(text)) return;
  try {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(text);
    } else {
      const input = document.createElement('textarea');
      input.value = text;
      input.className = 'sr-only';
      document.body.appendChild(input);
      input.select();
      const copied = document.execCommand('copy');
      input.remove();
      if (!copied) throw new Error('copy command failed');
    }
    const previous = button.textContent;
    button.textContent = '已复制';
    window.setTimeout(() => {
      if (button.isConnected) button.textContent = previous;
    }, 900);
  } catch {
    setGmLiveStatus('复制失败，请手动记录道具 ID。', 'error');
  }
}

function trapGmModalFocus(event) {
  if (event.key !== 'Tab' || !gmBrowserState.open) return false;
  const focusable = [...gmItemBrowserModal.querySelectorAll(
    'button:not(:disabled), input:not(:disabled), select:not(:disabled), [tabindex]:not(:disabled)'
  )].filter((element) => (
    !element.classList.contains('hidden')
    && !element.classList.contains('sr-only')
    && element.getAttribute('aria-hidden') !== 'true'
    && element.getAttribute('tabindex') !== '-1'
  ));
  if (!focusable.length) return false;
  const first = focusable[0];
  const last = focusable[focusable.length - 1];
  if (event.shiftKey && document.activeElement === first) {
    event.preventDefault();
    last.focus();
    return true;
  }
  if (!event.shiftKey && document.activeElement === last) {
    event.preventDefault();
    first.focus();
    return true;
  }
  return false;
}

let serverDraftValue = '';

function serverOptionInputs() {
  return [...serverModal.querySelectorAll('.server-option input[type="radio"]')];
}

function renderServerDraft(value) {
  const normalized = String(value || '');
  for (const input of serverOptionInputs()) {
    const selected = input.value === normalized;
    input.checked = selected;
    input.closest('.server-option')?.classList.toggle('is-selected', selected);
  }
}

function openServerModal() {
  const inputs = serverOptionInputs().filter((input) => !input.disabled);
  serverDraftValue = serverSelect.value || inputs[0]?.value || '';
  renderServerDraft(serverDraftValue);
  serverModal.classList.remove('hidden');
  serverSelectTrigger.setAttribute('aria-expanded', 'true');
  (inputs.find((input) => input.value === serverDraftValue) || serverModalCloseBtn).focus();
}

function closeServerModal() {
  serverDraftValue = serverSelect.value || '';
  renderServerDraft(serverDraftValue);
  serverModal.classList.add('hidden');
  serverSelectTrigger.setAttribute('aria-expanded', 'false');
  serverSelectTrigger.focus();
}

function commitServerDraft() {
  const selectedInput = serverOptionInputs().find(
    (input) => !input.disabled && input.value === serverDraftValue
  );
  if (!selectedInput) {
    setStatus('当前没有可用大区。', 'warn');
    return;
  }

  serverSelect.value = serverDraftValue;
  serverSelect.dispatchEvent(new Event('change', { bubbles: true }));
  const label = selectedInput.closest('.server-option')?.querySelector('.server-option-name')?.textContent;
  if (label) {
    serverSelectLabel.textContent = label;
  }
  renderServerDraft(serverDraftValue);
  closeServerModal();
}

async function bootstrap() {
  renderLauncherState();
  try {
    await requestLauncherContent('bootstrap');
    const state = await refreshState();
    if (!state.paths?.gameRoot) {
      setStatus('', 'info');
      return;
    }
    setStatus('正在加载 GameApp 路径...', 'loading');
    const ready = await prepareGame();
    if (!ready) {
      return;
    }
    const preloaded = await preloadGame();
    if (
      preloaded &&
      launcherState.auth.phase === 'login-ready' &&
      state.login?.autoLogin &&
      state.login?.hasPassword &&
      state.login?.user
    ) {
      setStatus('已读取保存的登录信息，正在自动登录...', 'loading');
      try {
        beginAuthentication();
        const loginResult = await api.login({
          username: state.login.user,
          password: '',
          remember: true,
          autoLogin: true,
          server: serverSelect.value || '64'
        });
        applyLaunchStatus(loginResult);
        await refreshState({ synchronizeSession: false });
        setStatus(
          loginResult.warning || '账号已自动登录；点击“开始游戏”后才会进入客户端。',
          loginResult.warning ? 'warn' : 'ok'
        );
      } catch (error) {
        failAuthentication(error);
        setStatus(error.message || String(error), 'error');
      }
    }
  } catch (error) {
    failAuthentication(error);
    setStatus(error.message || String(error), 'error');
  }
}

loginForm.addEventListener('submit', async (event) => {
  event.preventDefault();

  try {
    if (!agreementCheck.checked) {
      setStatus('请先阅读并同意游戏许可及隐私保护相关协议。', 'warn');
      return;
    }

    // Snapshot the editable form before prepareGame() refreshes main-process state.
    // Otherwise the stored account/remember flag can overwrite the user's draft.
    const loginDraft = {
      username: username.value,
      password: password.value,
      remember: remember.checked,
      autoLogin: autoLogin.checked,
      server: serverSelect.value || '64'
    };
    setLoginAccountAvatar(loginDraft.username);

    if (!loginDraft.password && !loginDraft.remember) {
      setStatus('未勾选记住密码时，请输入密码。', 'warn');
      return;
    }

    beginAuthentication();
    const ready = await prepareGame({ preserveLoginForm: true });
    if (!ready) {
      failAuthentication(new Error('请先选择 QQ飞车主目录，再登录。'));
      setStatus('请先选择 QQ飞车主目录，再登录。', 'warn');
      await selectGameRoot();
      return;
    }

    if (!await preloadGame()) {
      throw new Error('GameApp 预加载未完成，请重试。');
    }
    const loginResult = await api.login(loginDraft);
    applyLaunchStatus(loginResult);
    await refreshState({ synchronizeSession: false });
    password.value = '';
    setStatus(
      loginResult.warning || '账号已登录；GameApp 仍在后台等待，点击“开始游戏”后才会进入客户端。',
      loginResult.warning ? 'warn' : 'ok'
    );
  } catch (error) {
    failAuthentication(error);
    setStatus(error.message || String(error), 'error');
  } finally {
    renderLauncherState();
  }
});

async function launchGame() {
  const gameButton = selectGameButton(launcherState);
  if (!['start', 'retry'].includes(gameButton.action) || gameButton.disabled) {
    return;
  }
  transitionLauncher({
    type: gameButton.action === 'retry' ? 'GAME_RETRY' : 'START_REQUESTED'
  });
  setStatus('正在检查游戏目录...', 'loading');

  try {
    const ready = await prepareGame();
    if (!ready) {
      await selectGameRoot();
      return;
    }

    setStatus('已点击开始游戏，正在放行后台预加载的 GameApp...', 'loading');
    const result = await api.startGame({ zone: serverSelect.value || '64' });
    applyLaunchStatus(result);
  } catch (error) {
    applyLaunchStatus({
      phase: 'failed',
      progress: 0,
      message: error.message || String(error)
    });
  }
}

const gmGridResizeObserver = typeof ResizeObserver === 'function'
  ? new ResizeObserver(() => updateGmGridLayout())
  : null;
gmGridResizeObserver?.observe(gmResultsViewport);

startBtn.addEventListener('click', launchGame);
gmItemBrowserBtn.addEventListener('click', () => void openGmItemBrowser());
homeActivityBtn.addEventListener('click', toggleHomeActivityPopover);
homeAnnouncementTicker.addEventListener('click', toggleHomeActivityPopover);
homeActivityCloseBtn.addEventListener(
  'click',
  () => closeHomeActivityPopover({ restoreFocus: true })
);
gmModalCloseBtn.addEventListener('click', () => closeGmItemBrowser());
gmItemBrowserModal.addEventListener('click', (event) => {
  if (event.target === gmItemBrowserModal) closeGmItemBrowser();
});
gmPrimaryCategories.addEventListener('click', (event) => {
  const button = event.target.closest('button[data-gm-primary]');
  if (!button) return;
  void selectGmPrimaryCategory(button.dataset.gmPrimary);
});
gmSecondaryCategories.addEventListener('click', (event) => {
  const button = event.target.closest('button[data-gm-secondary]');
  if (!button || button.disabled) return;
  gmBrowserState.secondaryCategory = button.dataset.gmSecondary;
  gmSecondaryCategory.value = gmBrowserState.secondaryCategory;
  gmBrowserState.page = 1;
  renderGmSecondaryCategories();
  void queryGmItems();
});
gmFilterForm.addEventListener('submit', (event) => {
  event.preventDefault();
  if (gmBrowserState.busy) return;
  gmBrowserState.page = 1;
  void queryGmItems();
});
gmSecondaryCategory.addEventListener('change', () => {
  gmBrowserState.secondaryCategory = gmSecondaryCategory.value;
  gmBrowserState.page = 1;
  renderGmSecondaryCategories();
  void queryGmItems();
});
gmGender.addEventListener('change', () => {
  gmBrowserState.gender = gmGender.value;
  gmBrowserState.page = 1;
  void queryGmItems();
});
gmSortDirection.addEventListener('change', () => {
  gmBrowserState.sortDirection = gmSortDirection.value === 'desc' ? 'desc' : 'asc';
  gmBrowserState.page = 1;
  void queryGmItems();
});
gmResetBtn.addEventListener('click', () => void resetGmItemBrowser());
gmPrevPageBtn.addEventListener('click', () => {
  if (gmBrowserState.busy || gmBrowserState.page <= 1) return;
  gmBrowserState.page -= 1;
  void queryGmItems();
});
gmNextPageBtn.addEventListener('click', () => {
  if (gmBrowserState.busy || gmBrowserState.page >= gmBrowserState.pageCount) return;
  gmBrowserState.page += 1;
  void queryGmItems();
});
gmItemGrid.addEventListener('click', (event) => {
  const button = event.target.closest('button[data-gm-copy-id]');
  if (button) void copyGmItemId(button.dataset.gmCopyId, button);
});
multiOpenBtn.addEventListener('click', openMultiInstanceModal);
multiInstanceCloseBtn.addEventListener('click', closeMultiInstanceModal);
multiInstanceModal.addEventListener('click', (event) => {
  if (event.target === multiInstanceModal && !multiActionBusy) closeMultiInstanceModal();
});
bindAccountBtn.addEventListener('click', () => {
  bindAccountForm.classList.remove('hidden');
  bindAccountUser.focus();
});
bindAccountCancelBtn.addEventListener('click', () => {
  bindAccountForm.classList.add('hidden');
  bindAccountPassword.value = '';
  bindAccountBtn.focus();
});
bindAccountForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  if (multiActionBusy) return;
  const usernameValue = bindAccountUser.value.trim();
  const passwordValue = bindAccountPassword.value;
  if (!usernameValue || !passwordValue) {
    setStatus('绑定账号和密码不能为空。', 'warn');
    return;
  }
  multiActionBusy = true;
  renderMultiInstanceSnapshot(currentInstanceSnapshot);
  setStatus(`正在验证并绑定账号 ${usernameValue}…`, 'loading');
  try {
    const snapshot = await api.bindInstanceAccount({
      username: usernameValue,
      password: passwordValue
    });
    bindAccountUser.value = '';
    bindAccountPassword.value = '';
    bindAccountForm.classList.add('hidden');
    renderMultiInstanceSnapshot(snapshot);
    setStatus(`账号 ${usernameValue} 已绑定并记住密码。`, 'ok');
  } catch (error) {
    setStatus(error.message || String(error), 'error');
  } finally {
    bindAccountPassword.value = '';
    multiActionBusy = false;
    await refreshMultiInstances().catch(() => renderMultiInstanceSnapshot(currentInstanceSnapshot));
  }
});
multiInstanceModal.addEventListener('click', async (event) => {
  const start = event.target.closest('button[data-multi-start]');
  const unbind = event.target.closest('button[data-multi-unbind]');
  if ((!start && !unbind) || multiActionBusy) return;
  const card = event.target.closest('.multi-account-card');
  const user = card?.dataset.user || '';
  if (!user) return;

  multiActionBusy = true;
  renderMultiInstanceSnapshot(currentInstanceSnapshot);
  try {
    if (start) {
      const zone = card.querySelector('.multi-zone-select')?.value || '64';
      setStatus(`正在启动账号 ${user} 的独立游戏实例…`, 'loading');
      const snapshot = await api.startInstance({ user, zone });
      renderMultiInstanceSnapshot(snapshot);
    } else {
      setStatus(`正在解绑账号 ${user}…`, 'loading');
      const snapshot = await api.unbindInstanceAccount({ user });
      renderMultiInstanceSnapshot(snapshot);
      setStatus(`账号 ${user} 已解绑。`, 'ok');
    }
  } catch (error) {
    setStatus(error.message || String(error), 'error');
  } finally {
    multiActionBusy = false;
    await refreshMultiInstances().catch(() => renderMultiInstanceSnapshot(currentInstanceSnapshot));
  }
});

$('selectGameRootBtn').addEventListener('click', selectGameRoot);
$('homeMenuBtn').addEventListener('click', toggleLauncherMenu);
$('selectGameRootMenuBtn').addEventListener('click', selectGameRootFromMenu);
serverSelectTrigger.addEventListener('click', openServerModal);
serverModalCloseBtn.addEventListener('click', closeServerModal);
serverModalCancelBtn.addEventListener('click', closeServerModal);
serverModalConfirmBtn.addEventListener('click', commitServerDraft);
serverModal.addEventListener('change', (event) => {
  const input = event.target.closest('.server-option input[type="radio"]');
  if (!input || input.disabled) {
    return;
  }
  serverDraftValue = input.value;
  renderServerDraft(serverDraftValue);
});
serverModal.addEventListener('click', (event) => {
  if (event.target === serverModal) closeServerModal();
});
$('scanLoginBtn').addEventListener('click', () => {
  setStatus('当前暂不支持扫码登录，请使用账号密码或已保存账号快捷登录。', 'warn');
});
$('quickLoginBtn').addEventListener('click', () => {
  const account = findRememberedAccount();
  if (!account) {
    setStatus('没有可用的已记住密码账号，请先完成一次账号密码登录并勾选记住密码。', 'warn');
    return;
  }
  selectKnownAccount(account);
  remember.checked = true;
  password.value = '';
  loginForm.requestSubmit();
});
$('forgotPasswordBtn').addEventListener('click', () => {
  setStatus('当前登录器不提供找回密码，请联系服务器管理员。', 'warn');
});
$('accountBtn').addEventListener('click', async () => {
  setStatus('正在退出当前账号会话…', 'loading');
  try {
    const state = await api.switchAccount();
    applyState(state);
    if (state.launch?.phase) {
      applyLaunchStatus(state.launch);
    }
    password.value = '';
    setPasswordHint(Boolean(findKnownAccount(username.value)?.hasPassword || state.login?.hasPassword));
    if (state.paths?.gameRoot) {
      await prepareGame();
      await preloadGame();
    } else {
      setStatus('已退出账号；请选择 QQ飞车主目录后重新登录。', 'info');
    }
  } catch (error) {
    setStatus(error.message || String(error), 'error');
  }
});
$('minimizeBtn').addEventListener('click', () => api.minimize());
$('closeBtn').addEventListener('click', requestLauncherClose);
closeConfirmDismiss.addEventListener('click', dismissCloseDialog);
closeConfirmBtn.addEventListener('click', confirmLauncherClose);

autoLogin.addEventListener('change', () => {
  if (autoLogin.checked) {
    remember.checked = true;
  }
  // Auto-login only authenticates the launcher; entering GameApp still requires Start Game.
});

homeLinks.addEventListener('click', async (event) => {
  const button = event.target.closest('.home-link');
  if (!button) {
    return;
  }
  const label = button.textContent.trim() || button.dataset.homeAction || '快捷';
  await openOfficialLink(label, button.dataset.linkUrl);
});

homeBannerTrack.addEventListener('click', (event) => {
  const banner = event.target.closest('.home-banner-card[data-link-url]');
  if (banner?.dataset.linkUrl) {
    openOfficialLink(banner.textContent.trim() || '活动页面', banner.dataset.linkUrl);
  }
});

homeSecondaryBanner.addEventListener('click', (event) => {
  const release = event.target.closest('.home-release-card[data-link-url]');
  if (release?.dataset.linkUrl) {
    openOfficialLink(release.textContent.trim() || '版本更新', release.dataset.linkUrl);
  }
});

function openTimelineLink(target) {
  const link = target?.closest?.('[data-link-url]')
    || target?.closest?.('.timeline-item')?.querySelector('[data-link-url]');
  if (link?.dataset.linkUrl) {
    void openOfficialLink(link.textContent.trim() || '活动详情', link.dataset.linkUrl);
  }
}

releaseTimeline.addEventListener('click', (event) => {
  openTimelineLink(event.target);
});

releaseTimeline.addEventListener('keydown', (event) => {
  if (event.key === 'Enter' || event.key === ' ') {
    const link = event.target.closest?.('[data-link-url]')
      || event.target.closest?.('.timeline-item')?.querySelector('[data-link-url]');
    if (link) {
      event.preventDefault();
      openTimelineLink(link);
    }
  }
});

homeBulletinList.addEventListener('click', (event) => {
  const bulletin = event.target.closest('li[data-link-url]');
  if (bulletin?.dataset.linkUrl) {
    openOfficialLink(bulletin.textContent.trim() || '公告页面', bulletin.dataset.linkUrl);
  }
});

bulletinTabs.addEventListener('click', (event) => {
  const tab = event.target.closest('button[data-bulletin-category]');
  if (!tab) {
    return;
  }
  const category = tab.dataset.bulletinCategory;
  [...bulletinTabs.querySelectorAll('button[data-bulletin-category]')].forEach((button) => {
    const active = button === tab;
    button.classList.toggle('active', active);
    button.setAttribute('aria-pressed', String(active));
  });
  homeBulletinList.dataset.activeCategory = category;
});

homeBannerDots.addEventListener('click', (event) => {
  const dot = event.target.closest('button[data-banner-index]');
  if (dot) {
    selectBanner(Number(dot.dataset.bannerIndex));
  }
});

accountDropdownBtn.addEventListener('click', (event) => {
  event.preventDefault();
  event.stopPropagation();
  renderAccountDropdown();
  accountDropdown.classList.toggle('hidden');
});

accountDropdown.addEventListener('click', (event) => {
  const item = event.target.closest('.account-dropdown-item');
  if (!item) {
    return;
  }
  selectKnownAccount(findKnownAccount(item.dataset.user));
});

username.addEventListener('input', () => {
  const account = findKnownAccount(username.value);
  if (account) {
    remember.checked = Boolean(account.hasPassword);
    setPasswordHint(Boolean(account.hasPassword));
    setLoginAccountAvatar(account.user);
  } else {
    autoLogin.checked = false;
    setPasswordHint(false);
    setLoginAccountAvatar('');
  }
  updateQuickLoginAvailability();
});

username.addEventListener('blur', () => {
  setLoginAccountAvatar(username.value);
});

document.addEventListener('click', (event) => {
  if (!accountDropdown.contains(event.target) && !accountDropdownBtn.contains(event.target)) {
    accountDropdown.classList.add('hidden');
  }
  if (
    !homeActivityPopover.classList.contains('hidden')
    && !homeActivityPopover.contains(event.target)
    && !homeActivityBtn.contains(event.target)
    && !homeAnnouncementTicker.contains(event.target)
  ) {
    closeHomeActivityPopover();
  }
  if (!launcherMenu.contains(event.target) && !$('homeMenuBtn').contains(event.target)) {
    closeLauncherMenu();
  }
});

document.addEventListener('keydown', (event) => {
  if (trapGmModalFocus(event)) return;
  if (event.key !== 'Escape') return;
  if (!closeConfirmModal.classList.contains('hidden')) {
    dismissCloseDialog();
    return;
  }
  if (!gmItemBrowserModal.classList.contains('hidden')) {
    closeGmItemBrowser();
    return;
  }
  if (!multiInstanceModal.classList.contains('hidden') && !multiActionBusy) closeMultiInstanceModal();
  if (!serverModal.classList.contains('hidden')) closeServerModal();
  if (!homeActivityPopover.classList.contains('hidden')) {
    closeHomeActivityPopover({ restoreFocus: true });
    return;
  }
  closeLauncherMenu();
});

const lifecycleUnsubscribers = [];

function keepUnsubscriber(unsubscribe) {
  if (typeof unsubscribe === 'function') {
    lifecycleUnsubscribers.push(unsubscribe);
  }
}

if (typeof api?.onCloseRequested === 'function') {
  try {
    keepUnsubscriber(api.onCloseRequested(requestLauncherClose));
  } catch {
    // A missing close event subscription does not block the custom title-bar control.
  }
}

if (typeof api?.onClosePreferenceReset === 'function') {
  try {
    keepUnsubscriber(api.onClosePreferenceReset(restoreClosePrompt));
  } catch {
    // The tray remains usable even if this renderer is already being torn down.
  }
}

if (typeof api?.onLaunchStatus === 'function') {
  try {
    keepUnsubscriber(api.onLaunchStatus(applyLaunchStatus));
  } catch (error) {
    setStatus(`启动状态订阅不可用：${error.message || String(error)}`, 'warn');
  }
}

if (typeof api?.onInstancesChanged === 'function') {
  try {
    keepUnsubscriber(api.onInstancesChanged(renderMultiInstanceSnapshot));
  } catch (error) {
    setStatus(`多开状态订阅不可用：${error.message || String(error)}`, 'warn');
  }
}

if (typeof api?.onUserLogin === 'function') {
  try {
    keepUnsubscriber(api.onUserLogin((session) => {
      if (session?.authenticated === true) {
        synchronizeAuthoritativeSession(session);
      }
    }));
  } catch (error) {
    setStatus(`登录状态订阅不可用：${error.message || String(error)}`, 'warn');
  }
}

if (typeof api?.onUserLogout === 'function') {
  try {
    keepUnsubscriber(api.onUserLogout((session) => {
      if (session?.authenticated === false) {
        closeGmItemBrowser({ reset: true, restoreFocus: false });
        lastHomeRefreshAt = 0;
        synchronizeAuthoritativeSession(session);
        password.value = '';
        focusLoginUsernameIfVisible({ shell, loginView, username });
      }
    }));
  } catch (error) {
    setStatus(`退出状态订阅不可用：${error.message || String(error)}`, 'warn');
  }
}

function refreshHomeWhenVisible() {
  if (document.visibilityState === 'visible') {
    refreshHomeIfDue();
  }
}

window.addEventListener('focus', refreshHomeIfDue);
window.addEventListener('online', refreshHomeIfDue);
window.addEventListener('resize', scheduleBannerGeometryRefresh);
document.addEventListener('visibilitychange', refreshHomeWhenVisible);

window.addEventListener('beforeunload', () => {
  gmGridResizeObserver?.disconnect();
  if (bannerTimer) {
    window.clearInterval(bannerTimer);
  }
  if (bannerResizeFrame !== null) {
    window.cancelAnimationFrame(bannerResizeFrame);
  }
  for (const unsubscribe of lifecycleUnsubscribers.splice(0)) {
    unsubscribe();
  }
}, { once: true });

bootstrap();
