import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { createGmItemQueryService } from '../../src/main/gm-item-query-service.js';
import { writeItemCatalogDatabase } from '../../src/shared/gm-item-database.js';

const FIXTURE_ITEMS = [
  { id: 100, name: '流火 A', category: '赛车', subcategory: 'A级赛车', gender: '通用', typeCode: 1539 },
  { id: 200, name: '雷霆 S', category: '赛车', subcategory: 'S级赛车', gender: '通用', typeCode: 1543 },
  { id: 300, name: '风暴 S', category: '赛车', subcategory: 'S级赛车', gender: '通用', typeCode: 1543 },
  { id: 400, name: '雷霆皮肤', category: '赛车皮肤', subcategory: '赛车皮肤', gender: '通用', typeCode: 1566 },
  { id: 500, name: '星光套装', category: '服饰', subcategory: '套装', gender: '女', typeCode: 1793 },
  { id: 600, name: '极速上装', category: '服饰', subcategory: '上装', gender: '男', typeCode: 262 },
  { id: 700, name: '银月发型', category: '服饰', subcategory: '发饰', gender: '女', typeCode: 266 },
  { id: 800, name: '小飞象', category: '宠物', subcategory: '宠物', gender: '通用', typeCode: 2328 }
];

async function withService(run) {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'qqspeed-gm-query-'));
  const databasePath = path.join(directory, 'gm-items.sqlite3');
  writeItemCatalogDatabase(databasePath, {
    version: 10015,
    sourceSha256: 'b'.repeat(64),
    items: FIXTURE_ITEMS
  });
  const service = createGmItemQueryService({ databasePath });
  try {
    await run(service);
  } finally {
    service.close();
    await rm(directory, { recursive: true, force: true });
  }
}

test('catalog links secondary options to the selected primary category', async () => {
  await withService((service) => {
    const racers = service.getCatalog('赛车');
    assert.deepEqual(
      racers.subcategories,
      [
        { value: 'A级赛车', label: 'A级赛车', count: 1 },
        { value: 'S级赛车', label: 'S级赛车', count: 2 }
      ]
    );

    const clothing = service.getCatalog('服饰');
    assert.deepEqual(
      clothing.subcategories.map((entry) => entry.value),
      ['上装', '发饰', '套装']
    );
    assert.ok(clothing.categories.some((entry) => entry.value === '赛车' && entry.count === 3));
    assert.ok(clothing.categories.some((entry) => entry.value === '服饰' && entry.count === 3));
    assert.deepEqual(clothing.genders, ['通用', '男', '女', '未知']);
  });
});

test('racer parent and secondary filters are exact and skins stay separate', async () => {
  await withService((service) => {
    const allRacers = service.queryItems({
      primaryCategory: '赛车',
      sortDirection: 'asc',
      page: 1,
      pageSize: 30
    });
    assert.equal(allRacers.total, 3);
    assert.deepEqual(allRacers.items.map((item) => item.id), [100, 200, 300]);

    const sRacers = service.queryItems({
      primaryCategory: '赛车',
      secondaryCategory: 'S级赛车',
      sortDirection: 'desc',
      page: 1,
      pageSize: 30
    });
    assert.equal(sRacers.total, 2);
    assert.deepEqual(sRacers.items.map((item) => item.id), [300, 200]);
  });
});

test('clothing secondary category, gender, ID and name filters compose safely', async () => {
  await withService((service) => {
    assert.deepEqual(
      service.queryItems({
        primaryCategory: '服饰',
        secondaryCategory: '套装',
        gender: '女',
        sortDirection: 'asc',
        page: 1,
        pageSize: 18
      }).items.map((item) => item.id),
      [500]
    );

    assert.deepEqual(
      service.queryItems({ keyword: '200', sortDirection: 'asc', page: 1, pageSize: 24 }).items.map((item) => item.id),
      [200]
    );
    assert.deepEqual(
      service.queryItems({ keyword: '雷霆', sortDirection: 'asc', page: 1, pageSize: 24 }).items.map((item) => item.id),
      [200, 400]
    );
  });
});

test('query validation rejects cross-category and unbounded input', async () => {
  await withService((service) => {
    assert.throws(
      () => service.queryItems({
        primaryCategory: '赛车',
        secondaryCategory: '套装',
        sortDirection: 'asc',
        page: 1,
        pageSize: 30
      }),
      /二级分类不属于当前一级分类/
    );
    assert.throws(
      () => service.queryItems({ sortDirection: 'DROP TABLE items', page: 1, pageSize: 30 }),
      /排序方式无效/
    );
    assert.throws(
      () => service.queryItems({ sortDirection: 'asc', page: 1, pageSize: 20 }),
      /分页大小无效/
    );
    assert.throws(
      () => service.queryItems({ primaryCategory: '不存在', sortDirection: 'asc', page: 1, pageSize: 30 }),
      /一级分类无效/
    );
    assert.throws(
      () => service.queryItems({ keyword: '道'.repeat(81), sortDirection: 'asc', page: 1, pageSize: 30 }),
      /搜索内容过长/
    );
  });
});
