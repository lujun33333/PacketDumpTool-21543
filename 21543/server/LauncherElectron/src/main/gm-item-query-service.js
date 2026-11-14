import { DatabaseSync } from 'node:sqlite';

const ALLOWED_PAGE_SIZES = new Set([18, 24, 30]);
const ALLOWED_GENDERS = new Set(['通用', '男', '女', '未知']);
const SORT_SQL = Object.freeze({ asc: 'ASC', desc: 'DESC' });
const MAX_KEYWORD_LENGTH = 80;
const MAX_ITEM_ID = 0xffff_ffff;
const CATEGORY_ORDER = Object.freeze([
  '赛车',
  '赛车皮肤',
  '服饰',
  '宠物',
  '道具',
  '赛车装扮',
  '社交展示',
  '宝石',
  '水族',
  '系统/其他'
]);

function normalizeText(value) {
  return String(value ?? '').trim();
}

function readPositiveInteger(value, fallback, label) {
  const normalized = value === undefined || value === null || value === ''
    ? fallback
    : Number(value);
  if (!Number.isSafeInteger(normalized) || normalized <= 0) {
    throw new TypeError(`${label}无效。`);
  }
  return normalized;
}

function escapeLike(value) {
  return value.replace(/[\\%_]/gu, (character) => `\\${character}`);
}

function toPlainItem(row) {
  return {
    id: Number(row.id),
    name: String(row.name),
    category: String(row.category),
    subcategory: String(row.subcategory),
    gender: String(row.gender)
  };
}

export function createGmItemQueryService({ databasePath } = {}) {
  const resolvedPath = normalizeText(databasePath);
  if (!resolvedPath) {
    throw new TypeError('GM 道具数据库路径不能为空。');
  }

  const database = new DatabaseSync(resolvedPath, { readOnly: true });
  let closed = false;

  const categoryRows = database.prepare(`
    SELECT category, COUNT(*) AS count
    FROM items
    GROUP BY category
  `).all();
  const categoryRank = new Map(CATEGORY_ORDER.map((value, index) => [value, index]));
  const categories = categoryRows
    .map((row) => ({
      value: String(row.category),
      label: String(row.category),
      count: Number(row.count)
    }))
    .sort((left, right) => {
      const leftRank = categoryRank.get(left.value) ?? CATEGORY_ORDER.length;
      const rightRank = categoryRank.get(right.value) ?? CATEGORY_ORDER.length;
      return leftRank - rightRank || left.label.localeCompare(right.label, 'zh-CN');
    });
  const allowedCategories = new Set(categories.map((entry) => entry.value));

  function assertOpen() {
    if (closed) {
      throw new Error('GM 道具数据库已经关闭。');
    }
  }

  function validatePrimaryCategory(value) {
    const primaryCategory = normalizeText(value);
    if (primaryCategory && !allowedCategories.has(primaryCategory)) {
      throw new TypeError('一级分类无效。');
    }
    return primaryCategory;
  }

  function validateSecondaryCategory(primaryCategory, value) {
    const secondaryCategory = normalizeText(value);
    if (!secondaryCategory) return '';
    if (!primaryCategory) {
      throw new TypeError('请先选择一级分类。');
    }
    const exists = database.prepare(`
      SELECT 1 AS present
      FROM items
      WHERE category = ? AND subcategory = ?
      LIMIT 1
    `).get(primaryCategory, secondaryCategory);
    if (!exists) {
      throw new TypeError('二级分类不属于当前一级分类。');
    }
    return secondaryCategory;
  }

  function getCatalog(primaryCategoryValue = '') {
    assertOpen();
    const primaryCategory = validatePrimaryCategory(primaryCategoryValue);
    const subcategories = primaryCategory
      ? database.prepare(`
          SELECT subcategory, COUNT(*) AS count, MIN(type_code) AS first_type
          FROM items
          WHERE category = ?
          GROUP BY subcategory
          ORDER BY first_type ASC, subcategory COLLATE NOCASE ASC
        `).all(primaryCategory).map((row) => ({
          value: String(row.subcategory),
          label: String(row.subcategory),
          count: Number(row.count)
        }))
      : [];
    return {
      categories: categories.map((entry) => ({ ...entry })),
      subcategories,
      genders: [...ALLOWED_GENDERS]
    };
  }

  function queryItems(input = {}) {
    assertOpen();
    const primaryCategory = validatePrimaryCategory(input.primaryCategory);
    const secondaryCategory = validateSecondaryCategory(primaryCategory, input.secondaryCategory);
    const gender = normalizeText(input.gender);
    if (gender && !ALLOWED_GENDERS.has(gender)) {
      throw new TypeError('性别筛选无效。');
    }
    const keyword = normalizeText(input.keyword);
    if (keyword.length > MAX_KEYWORD_LENGTH) {
      throw new TypeError('搜索内容过长。');
    }
    const sortDirection = normalizeText(input.sortDirection || 'asc').toLowerCase();
    const sortSql = SORT_SQL[sortDirection];
    if (!sortSql) {
      throw new TypeError('排序方式无效。');
    }
    const page = readPositiveInteger(input.page, 1, '页码');
    const pageSize = readPositiveInteger(input.pageSize, 30, '分页大小');
    if (!ALLOWED_PAGE_SIZES.has(pageSize)) {
      throw new TypeError('分页大小无效。');
    }

    const predicates = [];
    const parameters = [];
    if (primaryCategory) {
      predicates.push('category = ?');
      parameters.push(primaryCategory);
    }
    if (secondaryCategory) {
      predicates.push('subcategory = ?');
      parameters.push(secondaryCategory);
    }
    if (gender) {
      predicates.push('gender = ?');
      parameters.push(gender);
    }
    if (keyword) {
      if (/^\d+$/u.test(keyword)) {
        const numericId = BigInt(keyword);
        predicates.push('id = ?');
        parameters.push(numericId <= BigInt(MAX_ITEM_ID) ? Number(numericId) : -1);
      } else {
        predicates.push("name LIKE ? ESCAPE '\\'");
        parameters.push(`%${escapeLike(keyword)}%`);
      }
    }

    const whereSql = predicates.length ? `WHERE ${predicates.join(' AND ')}` : '';
    const totalRow = database.prepare(`
      SELECT COUNT(*) AS count
      FROM items
      ${whereSql}
    `).get(...parameters);
    const total = Number(totalRow.count);
    const pageCount = total > 0 ? Math.ceil(total / pageSize) : 0;
    const offset = (page - 1) * pageSize;
    const items = database.prepare(`
      SELECT id, name, category, subcategory, gender
      FROM items
      ${whereSql}
      ORDER BY id ${sortSql}
      LIMIT ? OFFSET ?
    `).all(...parameters, pageSize, offset).map(toPlainItem);

    return { items, total, page, pageSize, pageCount };
  }

  function close() {
    if (closed) return;
    closed = true;
    database.close();
  }

  return { getCatalog, queryItems, close };
}
