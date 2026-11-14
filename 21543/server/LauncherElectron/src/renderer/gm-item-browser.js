const MAX_ITEM_ID = 0xffff_ffff;

export function resolveGmGridLayout(resultWidth) {
  const width = Math.max(0, Number(resultWidth) || 0);
  if (width >= 1000) return { columns: 10, pageSize: 30 };
  if (width >= 860) return { columns: 8, pageSize: 24 };
  return { columns: 6, pageSize: 18 };
}

export function createGmItemImageUrl(value) {
  const text = typeof value === 'string' ? value.trim() : String(value ?? '');
  if (!/^\d+$/u.test(text)) return '';
  const id = Number(text);
  if (!Number.isSafeInteger(id) || id <= 0 || id > MAX_ITEM_ID) return '';
  return `https://iips.speed.qq.com/images/${id}.png`;
}

export function formatGmSubcategoryLabel(value) {
  const normalized = String(value ?? '').trim();
  return normalized === '发饰' ? '发饰/头发' : normalized;
}
