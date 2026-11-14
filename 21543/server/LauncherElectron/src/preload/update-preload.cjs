const { contextBridge, ipcRenderer } = require('electron');

const UPDATE_PROGRESS_CHANNEL = 'launcher:update-progress';

contextBridge.exposeInMainWorld('launcherUpdate', {
  onProgress(listener) {
    if (typeof listener !== 'function') {
      return () => {};
    }
    const handler = (_event, progress) => listener(progress);
    ipcRenderer.on(UPDATE_PROGRESS_CHANNEL, handler);
    return () => ipcRenderer.removeListener(UPDATE_PROGRESS_CHANNEL, handler);
  }
});
