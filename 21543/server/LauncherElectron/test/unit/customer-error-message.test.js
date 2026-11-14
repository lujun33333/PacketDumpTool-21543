import assert from 'node:assert/strict';
import test from 'node:test';

import {
  CUSTOMER_CREDENTIAL_ERROR,
  CUSTOMER_LAUNCHER_ERROR,
  CUSTOMER_MEMORY_ERROR,
  toCustomerErrorMessage,
  toCustomerStatusMessage
} from '../../src/shared/customer-error-message.js';

test('native memory implementation details are replaced with one customer-safe message', () => {
  for (const input of [
    '原生 Hook 会话失败。',
    'NativeBridge rejected the DLL payload.',
    'ManualMapHookImage failed to map the image.',
    '启动桥接拒绝了内存 Hook 载荷。',
    "Error invoking remote method 'launcher:preload-game': Error: Hook injection failed"
  ]) {
    assert.equal(toCustomerErrorMessage(input), CUSTOMER_MEMORY_ERROR);
  }
});

test('Electron IPC wrapper is removed without hiding a useful business error', () => {
  assert.equal(
    toCustomerErrorMessage(
      "Error invoking remote method 'launcher:preload-game': Error: 无法连接登录服务器。"
    ),
    '无法连接登录服务器。'
  );
  assert.equal(
    toCustomerErrorMessage("Error invoking remote method 'launcher:get-state': Error"),
    CUSTOMER_LAUNCHER_ERROR
  );
  for (const input of [
    'Native runtime bundle magic is invalid.',
    '启动组件未返回有效的游戏进程 PID。',
    '本机安装密钥保险库无效。'
  ]) {
    assert.equal(toCustomerErrorMessage(input), CUSTOMER_LAUNCHER_ERROR);
  }
});

test('credential implementation details use a normal password prompt', () => {
  assert.equal(
    toCustomerErrorMessage('Windows DPAPI 解密失败，密码保险库无法读取。'),
    CUSTOMER_CREDENTIAL_ERROR
  );
});

test('stable customer messages and selected game paths remain unchanged', () => {
  assert.equal(toCustomerErrorMessage('账号或密码错误。'), '账号或密码错误。');
  assert.equal(
    toCustomerErrorMessage('该账号已被封禁，如有疑问请联系管理员。'),
    '该账号已被封禁，如有疑问请联系管理员。'
  );
  assert.equal(
    toCustomerErrorMessage('QQ飞车主目录已选择：F:\\QQ飞车'),
    'QQ飞车主目录已选择：F:\\QQ飞车'
  );
  assert.equal(toCustomerErrorMessage('', '操作失败，请重试。'), '操作失败，请重试。');
});

test('normal progress messages never become false launcher errors', () => {
  const progress = '启动组件正在初始化，请稍候。';
  assert.equal(toCustomerErrorMessage(progress), CUSTOMER_LAUNCHER_ERROR);
  assert.equal(toCustomerStatusMessage(progress, 'loading'), progress);
  assert.equal(toCustomerStatusMessage(progress, 'info'), progress);
  assert.equal(toCustomerStatusMessage(progress, 'ok'), progress);
  assert.equal(toCustomerStatusMessage(progress, 'error'), CUSTOMER_LAUNCHER_ERROR);
});
