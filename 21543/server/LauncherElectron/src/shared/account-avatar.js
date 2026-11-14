const QQ_UIN_PATTERN = /^[1-9]\d{4,11}$/;
const QQ_AVATAR_HOST = 'q3.qlogo.cn';
const QQ_AVATAR_SIZE = '100';

export function normalizeQqUin(value) {
  const uin = String(value ?? '').trim();
  return QQ_UIN_PATTERN.test(uin) ? uin : '';
}

export function createQqAvatarUrl(value) {
  const uin = normalizeQqUin(value);
  if (!uin) return '';
  const url = new URL('https://q3.qlogo.cn/g');
  url.searchParams.set('b', 'qq');
  url.searchParams.set('nk', uin);
  url.searchParams.set('s', QQ_AVATAR_SIZE);
  return url.href;
}

export function sanitizeAccountAvatarUrl(value) {
  try {
    const url = new URL(String(value ?? '').trim());
    if (
      url.protocol !== 'https:' ||
      url.username ||
      url.password ||
      url.port ||
      url.hostname.toLowerCase() !== QQ_AVATAR_HOST ||
      url.pathname !== '/g' ||
      url.searchParams.get('b') !== 'qq' ||
      !normalizeQqUin(url.searchParams.get('nk')) ||
      url.searchParams.get('s') !== QQ_AVATAR_SIZE
    ) {
      return '';
    }
    const allowedKeys = new Set(['b', 'nk', 's']);
    const queryKeys = [...url.searchParams.keys()];
    if (
      queryKeys.length !== allowedKeys.size ||
      queryKeys.some((key) => !allowedKeys.has(key)) ||
      [...allowedKeys].some((key) => url.searchParams.getAll(key).length !== 1)
    ) {
      return '';
    }
    return url.href;
  } catch {
    return '';
  }
}
