const DEFAULT_TOOL_TIP = 'QQ飞车登录器';

function isUsableWindow(window) {
  return Boolean(window) && !(typeof window.isDestroyed === 'function' && window.isDestroyed());
}

export function createTrayController({
  iconPath,
  createTray,
  buildMenu,
  getMainWindow,
  onQuit,
  onResetClosePreference,
  toolTip = DEFAULT_TOOL_TIP
}) {
  let tray = null;

  function resolveWindow(window) {
    return window ?? getMainWindow?.();
  }

  function hideWindow(window) {
    const target = resolveWindow(window);
    if (!isUsableWindow(target)) {
      return false;
    }
    target.hide();
    return true;
  }

  function restore() {
    const window = resolveWindow();
    if (!isUsableWindow(window)) {
      return false;
    }
    if (window.isMinimized?.()) {
      window.restore();
    }
    window.show();
    window.focus();
    return true;
  }

  function create() {
    if (tray && !(typeof tray.isDestroyed === 'function' && tray.isDestroyed())) {
      return tray;
    }

    tray = createTray(iconPath);
    tray.setToolTip(toolTip);
    tray.setContextMenu(buildMenu([
      { label: '打开启动器', click: restore },
      { label: '恢复关闭提示', click: () => onResetClosePreference?.() },
      { type: 'separator' },
      { label: '退出启动器', click: onQuit }
    ]));
    tray.on('click', restore);
    return tray;
  }

  function destroy() {
    if (!tray) {
      return;
    }
    const currentTray = tray;
    tray = null;
    if (!(typeof currentTray.isDestroyed === 'function' && currentTray.isDestroyed())) {
      currentTray.destroy();
    }
  }

  return Object.freeze({
    create,
    destroy,
    hideForGameLoading: hideWindow,
    hideToTray: hideWindow,
    restore
  });
}
