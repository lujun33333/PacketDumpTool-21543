import assert from 'node:assert/strict';
import test from 'node:test';

import { createTrayController } from '../../src/main/tray-controller.js';

function createWindow({ minimized = false } = {}) {
  const calls = [];
  return {
    calls,
    destroyed: false,
    minimized,
    isDestroyed() {
      return this.destroyed;
    },
    isMinimized() {
      return this.minimized;
    },
    restore() {
      calls.push('restore');
      this.minimized = false;
    },
    show() {
      calls.push('show');
    },
    focus() {
      calls.push('focus');
    },
    hide() {
      calls.push('hide');
    },
    minimize() {
      calls.push('minimize');
    }
  };
}

function createHarness({ window = createWindow() } = {}) {
  const trays = [];
  const menus = [];
  let quitCalls = 0;
  let resetCalls = 0;

  const controller = createTrayController({
    iconPath: 'C:\\fixtures\\app.ico',
    createTray(iconPath) {
      const handlers = new Map();
      const tray = {
        iconPath,
        destroyed: false,
        destroyCalls: 0,
        toolTip: '',
        contextMenu: null,
        setToolTip(value) {
          this.toolTip = value;
        },
        setContextMenu(value) {
          this.contextMenu = value;
        },
        on(eventName, listener) {
          handlers.set(eventName, listener);
        },
        emit(eventName) {
          handlers.get(eventName)?.();
        },
        destroy() {
          this.destroyed = true;
          this.destroyCalls += 1;
        },
        isDestroyed() {
          return this.destroyed;
        }
      };
      trays.push(tray);
      return tray;
    },
    buildMenu(template) {
      const menu = { template };
      menus.push(menu);
      return menu;
    },
    getMainWindow: () => window,
    onQuit: () => {
      quitCalls += 1;
    },
    onResetClosePreference: () => {
      resetCalls += 1;
    }
  });

  return {
    controller,
    menus,
    trays,
    get quitCalls() {
      return quitCalls;
    },
    get resetCalls() {
      return resetCalls;
    }
  };
}

test('creates one tray instance when create is called repeatedly', () => {
  const harness = createHarness();

  const first = harness.controller.create();
  const second = harness.controller.create();

  assert.strictEqual(second, first);
  assert.equal(harness.trays.length, 1);
  assert.equal(first.iconPath, 'C:\\fixtures\\app.ico');
  assert.equal(first.toolTip, 'QQ飞车登录器');
  assert.equal(harness.menus.length, 1);
});

test('close-to-tray hides the owned window instead of minimizing it', () => {
  const harness = createHarness();
  const window = createWindow();

  assert.equal(harness.controller.hideToTray(window), true);
  assert.deepEqual(window.calls, ['hide']);
});

test('game loading hides the owned window instead of minimizing it', () => {
  const harness = createHarness();
  const window = createWindow();

  assert.equal(harness.controller.hideForGameLoading(window), true);
  assert.deepEqual(window.calls, ['hide']);
});

test('left clicking the tray restores, shows, and focuses the main window', () => {
  const window = createWindow({ minimized: true });
  const harness = createHarness({ window });
  const tray = harness.controller.create();

  tray.emit('click');

  assert.deepEqual(window.calls, ['restore', 'show', 'focus']);
});

test('the show menu item restores the main window', () => {
  const window = createWindow();
  const harness = createHarness({ window });
  harness.controller.create();
  const showItem = harness.menus[0].template.find((item) => item.label === '打开启动器');

  showItem.click();

  assert.deepEqual(window.calls, ['show', 'focus']);
});

test('the reset menu item restores prompting through the injected callback', () => {
  const harness = createHarness();
  harness.controller.create();
  const resetItem = harness.menus[0].template.find((item) => item.label === '恢复关闭提示');

  resetItem.click();

  assert.equal(harness.resetCalls, 1);
});

test('the exit menu item invokes the injected quit callback', () => {
  const harness = createHarness();
  harness.controller.create();
  const exitItem = harness.menus[0].template.find((item) => item.label === '退出启动器');

  exitItem.click();

  assert.equal(harness.quitCalls, 1);
});

test('destroy is idempotent and permits a later tray recreation', () => {
  const harness = createHarness();
  const first = harness.controller.create();

  harness.controller.destroy();
  harness.controller.destroy();
  const second = harness.controller.create();

  assert.equal(first.destroyCalls, 1);
  assert.notStrictEqual(second, first);
  assert.equal(harness.trays.length, 2);
});
