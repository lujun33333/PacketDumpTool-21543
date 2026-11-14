const { contextBridge, ipcRenderer } = require('electron');

function onLaunchStatus(listener) {
  if (typeof listener !== 'function') {
    return () => {};
  }
  const handler = (_event, status) => listener(status);
  ipcRenderer.on('launcher:status', handler);
  return () => ipcRenderer.removeListener('launcher:status', handler);
}

function onSessionEvent(channel, listener) {
  if (typeof listener !== 'function') {
    return () => {};
  }
  const handler = (_event, session) => listener(session);
  ipcRenderer.on(channel, handler);
  return () => ipcRenderer.removeListener(channel, handler);
}

const launcherApi = {
  getState: () => ipcRenderer.invoke('launcher:get-state'),
  getLaunchStatus: () => ipcRenderer.invoke('launcher:get-launch-status'),
  getLauncherContent: (scope, options = {}) => ipcRenderer.invoke('launcher:get-content', {
    scope,
    refresh: options?.refresh === true
  }),
  getGmCatalog: (primaryCategory = '') => ipcRenderer.invoke('launcher:get-gm-catalog', { primaryCategory }),
  queryGmItems: (query) => ipcRenderer.invoke('launcher:query-gm-items', query),
  openExternal: (url) => ipcRenderer.invoke('launcher:open-external', url),
  onLaunchStatus,
  onUserLogin: (listener) => onSessionEvent('launcher:user-login', listener),
  onUserLogout: (listener) => onSessionEvent('launcher:user-logout', listener),
  selectGameRoot: () => ipcRenderer.invoke('launcher:select-game-root'),
  prepareGame: () => ipcRenderer.invoke('launcher:prepare-game'),
  preloadGame: () => ipcRenderer.invoke('launcher:preload-game'),
  login: (payload) => ipcRenderer.invoke('launcher:login', payload),
  switchAccount: () => ipcRenderer.invoke('launcher:switch-account'),
  startGame: (payload = {}) => ipcRenderer.invoke('launcher:start-game', payload),
  getInstances: () => ipcRenderer.invoke('launcher:get-instances'),
  bindInstanceAccount: (payload) => ipcRenderer.invoke('launcher:bind-instance-account', payload),
  unbindInstanceAccount: (payload) => ipcRenderer.invoke('launcher:unbind-instance-account', payload),
  startInstance: (payload) => ipcRenderer.invoke('launcher:start-instance', payload),
  onInstancesChanged: (listener) => onSessionEvent('launcher:instances', listener),
  minimize: () => ipcRenderer.invoke('window:minimize'),
  close: () => ipcRenderer.invoke('window:close'),
  onCloseRequested: (listener) => onSessionEvent('window:close-requested', listener),
  onClosePreferenceReset: (listener) => onSessionEvent('window:close-preference-reset', listener),
  applyCloseAction: (action) => ipcRenderer.invoke('window:apply-close-action', action)
};

contextBridge.exposeInMainWorld('qqspeedLauncher', launcherApi);
