import {
  existsSync,
  mkdirSync,
  readFileSync,
  renameSync,
  statSync,
  unlinkSync,
  writeFileSync
} from 'node:fs';
import { basename, dirname, join, resolve } from 'node:path';

import {
  commitAuthenticatedCredentialMetadata,
  hasLegacyProtectedPassword,
  normalizeAccountStore,
  removeAccountCredentialMetadata,
  resolveCredentialRequest,
  toPublicLoginState
} from './credential-policy.js';

const SETTINGS_FILE = 'launcher-settings.json';
const ACCOUNT_FILE = 'launcher-account.json';
const GAME_SUBDIR = 'Releasephysx27_64';
const GAME_APP_EXE = 'GameApp.exe';
const CLIENT_LOADING_FILES = Object.freeze([
  join(GAME_SUBDIR, 'LoadingGameUI', 'LoaderBG.bmp'),
  join(GAME_SUBDIR, 'LoadingGameUI', 'Progress_Back.bmp'),
  join(GAME_SUBDIR, 'LoadingGameUI', 'Progress_Fore.bmp'),
  join(GAME_SUBDIR, 'LoadingGameUI', 'cancel_normal.bmp'),
  'loadertipstext.ini'
]);

function uniqueExistingDirs(dirs) {
  const seen = new Set();
  const result = [];
  for (const value of dirs.filter(Boolean)) {
    const normalized = resolve(value);
    const key = process.platform === 'win32' ? normalized.toLowerCase() : normalized;
    if (!seen.has(key)) {
      seen.add(key);
      result.push(normalized);
    }
  }
  return result;
}

function firstValidGameRoot(dirs) {
  return uniqueExistingDirs(dirs).map((dir) => resolveGameInstall(dir)).find(Boolean) || null;
}

function getSettingsFile(settingsDir) {
  return join(settingsDir, SETTINGS_FILE);
}

function getAccountFile(settingsDir) {
  return join(settingsDir, ACCOUNT_FILE);
}

export function loadLauncherSettings(settingsDir) {
  const file = getSettingsFile(settingsDir);
  try {
    const parsed = JSON.parse(readFileSync(file, 'utf8'));
    return {
      file,
      gameRoot: parsed.gameRoot ? resolve(String(parsed.gameRoot)) : '',
      gameRootSource: parsed.gameRootSource === 'user' ? 'user' : ''
    };
  } catch {
    return { file, gameRoot: '', gameRootSource: '' };
  }
}

export function saveLauncherSettings(settingsDir, nextSettings) {
  mkdirSync(settingsDir, { recursive: true });
  const current = loadLauncherSettings(settingsDir);
  const normalizedGameRoot = nextSettings.gameRoot
    ? normalizeGameRoot(nextSettings.gameRoot) || resolve(String(nextSettings.gameRoot))
    : current.gameRoot || '';
  const merged = {
    gameRoot: normalizedGameRoot,
    gameRootSource: nextSettings.gameRootSource === 'user'
      ? 'user'
      : current.gameRootSource || ''
  };
  writeFileSync(getSettingsFile(settingsDir), `${JSON.stringify(merged, null, 2)}\n`, 'utf8');
  return loadLauncherSettings(settingsDir);
}

function saveInternalAccount(settingsDir, accountStore) {
  mkdirSync(settingsDir, { recursive: true });
  const destination = getAccountFile(settingsDir);
  const temporary = `${destination}.${process.pid}.${Date.now()}.tmp`;
  const normalized = normalizeAccountStore(accountStore);
  try {
    writeFileSync(temporary, `${JSON.stringify(normalized, null, 2)}\n`, {
      encoding: 'utf8',
      flag: 'wx'
    });
    renameSync(temporary, destination);
  } finally {
    try {
      unlinkSync(temporary);
    } catch {
      // A successful rename consumed the temporary file.
    }
  }
}

function readInternalAccount(settingsDir) {
  if (!settingsDir) return normalizeAccountStore();
  const file = getAccountFile(settingsDir);
  let parsed;
  try {
    parsed = JSON.parse(readFileSync(file, 'utf8'));
  } catch (error) {
    if (error?.code === 'ENOENT') return normalizeAccountStore();
    const empty = normalizeAccountStore();
    saveInternalAccount(settingsDir, empty);
    return empty;
  }
  const normalized = normalizeAccountStore(parsed);
  if (JSON.stringify(parsed) !== JSON.stringify(normalized) || hasLegacyProtectedPassword(parsed)) {
    saveInternalAccount(settingsDir, normalized);
  }
  return normalized;
}

export function resolveGameInstall(dir) {
  if (!dir) return null;
  const candidate = resolve(String(dir));
  if (basename(candidate) === GAME_SUBDIR) {
    const gameAppPath = join(candidate, GAME_APP_EXE);
    if (existsSync(gameAppPath)) {
      return {
        gameRoot: dirname(candidate),
        gameInstallRoot: dirname(candidate),
        gameWorkDir: candidate,
        gameSubDir: GAME_SUBDIR,
        gameAppPath
      };
    }
  }

  const gameWorkDir = join(candidate, GAME_SUBDIR);
  const gameAppPath = join(gameWorkDir, GAME_APP_EXE);
  if (!existsSync(gameAppPath)) return null;
  return {
    gameRoot: candidate,
    gameInstallRoot: candidate,
    gameWorkDir,
    gameSubDir: GAME_SUBDIR,
    gameAppPath
  };
}

export function normalizeGameRoot(dir) {
  return resolveGameInstall(dir)?.gameRoot || '';
}

export function isValidGameRoot(dir) {
  return Boolean(resolveGameInstall(dir));
}

function resolveLauncherPaths(appRoot, settingsDir) {
  const settings = settingsDir ? loadLauncherSettings(settingsDir) : { gameRoot: '' };
  const executableDir = process.execPath ? dirname(process.execPath) : '';
  const installDir = appRoot.endsWith('.asar') ? resolve(appRoot, '..', '..') : appRoot;
  const selectedGameRoot = settings.gameRootSource === 'user' ? settings.gameRoot : '';
  const gameInstall = firstValidGameRoot([
    selectedGameRoot,
    process.cwd(),
    executableDir,
    executableDir ? resolve(executableDir, '..') : '',
    executableDir ? resolve(executableDir, '..', '..') : '',
    installDir,
    resolve(installDir, '..'),
    resolve(installDir, '..', '..')
  ]);

  return {
    gameRoot: gameInstall?.gameRoot || '',
    gameInstallRoot: gameInstall?.gameRoot || '',
    gameWorkDir: gameInstall?.gameWorkDir || '',
    gameSubDir: gameInstall?.gameSubDir || '',
    configuredGameRoot: selectedGameRoot,
    gameAppPath: gameInstall?.gameAppPath || '',
    gameRootReady: Boolean(gameInstall)
  };
}

function publicPaths(paths) {
  return {
    gameRoot: paths.gameRoot,
    gameInstallRoot: paths.gameInstallRoot,
    gameWorkDir: paths.gameWorkDir,
    gameSubDir: paths.gameSubDir,
    configuredGameRoot: paths.configuredGameRoot,
    gameAppPath: paths.gameAppPath,
    gameRootReady: paths.gameRootReady
  };
}

export function loadNativeState(appRoot, settingsDir) {
  const paths = resolveLauncherPaths(appRoot, settingsDir);
  const account = readInternalAccount(settingsDir);
  return {
    paths: publicPaths(paths),
    settings: settingsDir ? loadLauncherSettings(settingsDir) : { gameRoot: '' },
    login: {
      ...toPublicLoginState(account),
      server: '64'
    }
  };
}

export function resolveCredentialsForAuthentication(appRoot, input, settingsDir) {
  const savedAccount = readInternalAccount(settingsDir);
  const candidate = resolveCredentialRequest(savedAccount, input);
  return {
    paths: resolveLauncherPaths(appRoot, settingsDir),
    username: candidate.username,
    server: '64',
    passwordForLaunch: candidate.password,
    remember: candidate.remember,
    autoLogin: candidate.autoLogin
  };
}

export function commitAuthenticatedCredentials(_appRoot, credentials, settingsDir, options = {}) {
  const current = readInternalAccount(settingsDir);
  const committed = commitAuthenticatedCredentialMetadata(current, credentials, options);
  saveInternalAccount(settingsDir, committed);
  return toPublicLoginState(committed);
}

export function removeRememberedAccountMetadata(settingsDir, username) {
  const current = readInternalAccount(settingsDir);
  const updated = removeAccountCredentialMetadata(current, username);
  saveInternalAccount(settingsDir, updated);
  return toPublicLoginState(updated);
}

export function loadRememberedCredentials(appRoot, settingsDir, requestedUser = '') {
  const savedAccount = readInternalAccount(settingsDir);
  const candidate = resolveCredentialRequest(savedAccount, {
    username: requestedUser || savedAccount.lastUser,
    password: '',
    remember: true,
    autoLogin: savedAccount.autoLogin
  });
  return {
    paths: resolveLauncherPaths(appRoot, settingsDir),
    username: candidate.username,
    server: '64',
    passwordForLaunch: candidate.password,
    remember: true,
    autoLogin: candidate.autoLogin
  };
}

export function prepareGameApp(appRoot, settingsDir) {
  const state = loadNativeState(appRoot, settingsDir);
  const { gameRoot, gameAppPath, gameWorkDir, gameSubDir } = state.paths;
  if (!gameRoot || !gameWorkDir || !gameAppPath) {
    return {
      ready: false,
      state,
      message: `未找到 QQ飞车主目录。需要主目录下存在 ${GAME_SUBDIR}\\${GAME_APP_EXE}。`
    };
  }

  const clientLoadingFiles = CLIENT_LOADING_FILES.map((relativePath) => ({
    relativePath,
    absolutePath: join(gameRoot, relativePath)
  }));
  const missing = clientLoadingFiles.filter(({ absolutePath }) => !existsSync(absolutePath));
  if (missing.length > 0) {
    return {
      ready: false,
      state,
      message: `客户端官服加载界面资源不完整：${missing.map(({ relativePath }) => relativePath).join('、')}。请使用客户端修复功能恢复后重试。`
    };
  }

  return {
    ready: true,
    state,
    gameRoot,
    gameInstallRoot: gameRoot,
    gameWorkDir,
    gameSubDir,
    gameAppPath,
    clientLoadingUI: {
      source: 'game-client',
      files: clientLoadingFiles.map(({ relativePath }) => relativePath)
    },
    size: statSync(gameAppPath).size,
    message: `GameApp.exe 与官服动态加载资源已就绪：${gameSubDir}\\${GAME_APP_EXE}。`
  };
}
