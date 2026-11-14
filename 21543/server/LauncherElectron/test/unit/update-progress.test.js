import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';
import test from 'node:test';

const modulePath = resolve(import.meta.dirname, '..', '..', 'src', 'renderer', 'update-progress.js');

test('update progress view describes a determinate download with percent and speed', async () => {
  assert.ok(existsSync(modulePath), 'update progress view model is missing');
  const { createUpdateProgressView } = await import(pathToFileURL(modulePath).href);
  assert.deepEqual(createUpdateProgressView({
    phase: 'downloading',
    downloadedBytes: 25 * 1024 * 1024,
    totalBytes: 100 * 1024 * 1024,
    bytesPerSecond: 5 * 1024 * 1024
  }), {
    title: '正在下载登录器更新',
    detail: '25.0 MB / 100.0 MB · 5.0 MB/s',
    percentLabel: '25%',
    percent: 25,
    determinate: true
  });
});

test('update progress view exposes checking, verifying and installing states', async () => {
  assert.ok(existsSync(modulePath), 'update progress view model is missing');
  const { createUpdateProgressView } = await import(pathToFileURL(modulePath).href);

  assert.equal(createUpdateProgressView({ phase: 'checking' }).title, '正在检查登录器更新');
  assert.equal(createUpdateProgressView({ phase: 'verifying' }).title, '正在校验更新文件');
  assert.equal(createUpdateProgressView({ phase: 'installing' }).title, '正在启动更新安装程序');
});
