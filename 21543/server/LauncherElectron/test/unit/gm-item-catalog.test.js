import assert from 'node:assert/strict';
import test from 'node:test';

import {
  classifyOfficialItem,
  decodeOfficialGender,
  parseOfficialItemList
} from '../../src/shared/gm-item-catalog.js';

const ITEM_ID_XOR = 0x4d10031e;
const STRING_XOR_KEY = Buffer.from('!G#&^* 9', 'ascii');

function encodeOfficialString(value) {
  const fixtureEncoding = new Map([
    ['雷诺', 'c0d7c5b5'],
    ['哈雷特3级皮肤', 'b9fec0d7ccd833bcb6c6a4b7f4']
  ]);
  const source = fixtureEncoding.has(value)
    ? Buffer.from(fixtureEncoding.get(value), 'hex')
    : Buffer.from(value, 'ascii');
  const encoded = Buffer.alloc(source.length);
  for (let index = 0; index < source.length; index += 1) {
    encoded[index] = source[index] ^ STRING_XOR_KEY[index % STRING_XOR_KEY.length];
  }
  const length = Buffer.alloc(4);
  length.writeInt32LE(encoded.length);
  return Buffer.concat([length, encoded]);
}

function createRecord({ id, name, itemType, garageType = 0, sexType = 0 }) {
  const header = Buffer.alloc(55);
  header.writeUInt32LE((id ^ ITEM_ID_XOR) >>> 0, 0);
  header.writeUInt32LE(itemType, 8);
  header.writeUInt32LE(garageType, 20);
  header.writeUInt8(sexType, 24);
  return Buffer.concat([
    header,
    encodeOfficialString(''),
    encodeOfficialString(''),
    encodeOfficialString(''),
    encodeOfficialString(name)
  ]);
}

function createItemListFixture(rows) {
  const version = Buffer.alloc(4);
  version.writeUInt32LE(10015);
  const records = [];
  const offsets = [];
  let offset = version.length;
  for (const row of rows) {
    const record = createRecord(row);
    records.push(record);
    offsets.push(offset);
    offset += record.length;
  }

  const index = Buffer.alloc(4 + rows.length * 8);
  index.writeUInt32LE(rows.length, 0);
  rows.forEach((row, rowIndex) => {
    index.writeUInt32LE(row.id, 4 + rowIndex * 8);
    index.writeUInt32LE(offsets[rowIndex], 8 + rowIndex * 8);
  });

  const footer = Buffer.alloc(8);
  footer.writeUInt32LE(offset, 0);
  return Buffer.concat([version, ...records, index, footer]);
}

test('official item types are normalized into query categories', () => {
  assert.deepEqual(classifyOfficialItem(1539), {
    category: '赛车',
    subcategory: 'A级赛车'
  });
  assert.deepEqual(classifyOfficialItem(1543), {
    category: '赛车',
    subcategory: 'S级赛车'
  });
  assert.deepEqual(classifyOfficialItem(1566), {
    category: '赛车皮肤',
    subcategory: '赛车皮肤'
  });
  assert.deepEqual(classifyOfficialItem(262), {
    category: '服饰',
    subcategory: '上装'
  });
  assert.deepEqual(classifyOfficialItem(2328), {
    category: '宠物',
    subcategory: '宠物'
  });
  assert.deepEqual(classifyOfficialItem(2323), {
    category: '道具',
    subcategory: '宝箱'
  });
  assert.deepEqual(classifyOfficialItem(514), {
    category: '赛车装扮',
    subcategory: '喷漆'
  });
  assert.deepEqual(classifyOfficialItem(2817), {
    category: '宝石',
    subcategory: '圆形竞速宝珠'
  });
  assert.deepEqual(classifyOfficialItem(2334), {
    category: '水族',
    subcategory: '鱼儿'
  });
  assert.deepEqual(classifyOfficialItem(319), {
    category: '社交展示',
    subcategory: '头像'
  });
  assert.deepEqual(classifyOfficialItem(319, 4294442239), {
    category: '社交展示',
    subcategory: '头像'
  });
});

test('selecting the racer parent can include every official racer grade without skins', () => {
  for (const itemType of [
    1537, 1538, 1539, 1540, 1541, 1542, 1543,
    1546, 1547, 1548,
    1550, 1551, 1552, 1553,
    1556, 1557, 1558,
    1560, 1561, 1562, 1565, 1567
  ]) {
    assert.equal(classifyOfficialItem(itemType).category, '赛车', String(itemType));
  }
  assert.equal(classifyOfficialItem(1566).category, '赛车皮肤');
});

test('official gender values are preserved for clothing filters', () => {
  assert.equal(decodeOfficialGender(0), '通用');
  assert.equal(decodeOfficialGender(1), '男');
  assert.equal(decodeOfficialGender(2), '女');
  assert.equal(decodeOfficialGender(255), '未知');
});

test('official itemlist parser imports every indexed record', () => {
  const fixture = createItemListFixture([
    { id: 12720, name: '雷诺', itemType: 1539 },
    { id: 133726, name: '哈雷特3级皮肤', itemType: 1566 }
  ]);

  const catalog = parseOfficialItemList(fixture);

  assert.equal(catalog.version, 10015);
  assert.equal(catalog.items.length, 2);
  assert.deepEqual(catalog.items[0], {
    id: 12720,
    name: '雷诺',
    category: '赛车',
    subcategory: 'A级赛车',
    gender: '通用',
    typeCode: 1539
  });
  assert.equal(catalog.items[1].category, '赛车皮肤');
});
