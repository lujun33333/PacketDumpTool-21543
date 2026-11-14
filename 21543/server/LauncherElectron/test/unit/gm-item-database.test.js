import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { DatabaseSync } from 'node:sqlite';

import { writeItemCatalogDatabase } from '../../src/shared/gm-item-database.js';

test('local catalog stores all rows and racer parent excludes the skin category', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'qqspeed-gm-items-'));
  const databasePath = path.join(directory, 'gm-items.sqlite3');
  try {
    writeItemCatalogDatabase(databasePath, {
      version: 10015,
      sourceSha256: 'a'.repeat(64),
      items: [
        {
          id: 12720,
          name: '雷诺',
          category: '赛车',
          subcategory: 'A级赛车',
          gender: '通用',
          typeCode: 1539
        },
        {
          id: 133726,
          name: '哈雷特3级皮肤',
          category: '赛车皮肤',
          subcategory: '赛车皮肤',
          gender: '通用',
          typeCode: 1566
        }
      ]
    });

    const database = new DatabaseSync(databasePath, { readOnly: true });
    try {
      assert.equal(database.prepare('SELECT COUNT(*) AS count FROM items').get().count, 2);
      assert.deepEqual(
        database
          .prepare("SELECT id, name FROM items WHERE category = '赛车' ORDER BY id")
          .all()
          .map((row) => ({ ...row })),
        [{ id: 12720, name: '雷诺' }]
      );
      assert.equal(
        database.prepare("SELECT value FROM catalog_meta WHERE key = 'source_version'").get().value,
        '10015'
      );
    } finally {
      database.close();
    }
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
