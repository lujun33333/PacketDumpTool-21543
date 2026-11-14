import assert from 'node:assert/strict';
import test from 'node:test';

import {
  createGmItemImageUrl,
  formatGmSubcategoryLabel,
  resolveGmGridLayout
} from '../../src/renderer/gm-item-browser.js';

test('GM grid uses ten, eight, or six columns with three visible rows', () => {
  assert.deepEqual(resolveGmGridLayout(1400), { columns: 10, pageSize: 30 });
  assert.deepEqual(resolveGmGridLayout(1000), { columns: 10, pageSize: 30 });
  assert.deepEqual(resolveGmGridLayout(999), { columns: 8, pageSize: 24 });
  assert.deepEqual(resolveGmGridLayout(860), { columns: 8, pageSize: 24 });
  assert.deepEqual(resolveGmGridLayout(859), { columns: 6, pageSize: 18 });
  assert.deepEqual(resolveGmGridLayout(0), { columns: 6, pageSize: 18 });
});

test('item image URL accepts only positive 32-bit integer IDs', () => {
  assert.equal(createGmItemImageUrl(10024), 'https://iips.speed.qq.com/images/10024.png');
  assert.equal(createGmItemImageUrl('12720'), 'https://iips.speed.qq.com/images/12720.png');
  assert.equal(createGmItemImageUrl('../10024'), '');
  assert.equal(createGmItemImageUrl(0), '');
  assert.equal(createGmItemImageUrl(-1), '');
  assert.equal(createGmItemImageUrl(0x1_0000_0000), '');
});

test('database subcategory values get user-facing labels without changing other values', () => {
  assert.equal(formatGmSubcategoryLabel('发饰'), '发饰/头发');
  assert.equal(formatGmSubcategoryLabel('S级赛车'), 'S级赛车');
  assert.equal(formatGmSubcategoryLabel(''), '');
});
