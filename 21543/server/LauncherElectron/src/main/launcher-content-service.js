import { mkdirSync, readFileSync, renameSync, unlinkSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const CONTENT_BASE = 'https://online-launcher.launcher.qq.com/basesrv/';
const CONTENT_ENDPOINTS = Object.freeze({
  AllConf: 'get_all_conf',
  Ads: 'get_ads',
  Bulletin: 'get_bulletin',
  Reflinks: 'get_reflinks'
});
const CACHE_FILES = Object.freeze({
  bootstrap: 'launcher-bootstrap-cache.json',
  home: 'launcher-content-cache.json'
});
const MAX_RESPONSE_BYTES = 2 * 1024 * 1024;
const MAX_ITEMS = 20;
const FALLBACK_IMAGE = '../../assets/login-bg-default-BpD5IuOu.png';
const LOCAL_BACKGROUND_VIDEO = '../../assets/launcher-background.mp4';
const CUSTOMER_BRAND = 'QQ飞车';
const OFFICIAL_HEADERS = Object.freeze({
  'content-type': 'application/json',
  accept: 'application/json',
  'L-Game-Id': '383998722',
  'L-Dolphin-Channel-Id': '117091501',
  'L-Launcher-Version': '1.0.0.14',
  'L-Client-Type': '1',
  'L-Os': 'windows',
  'L-Device-Id': ''
});

function text(value, maxLength = 160) {
  return String(value ?? '').replace(/[\u0000-\u001f\u007f]/g, ' ').trim().slice(0, maxLength);
}

function allowedOfficialHost(hostname) {
  const host = String(hostname || '').toLowerCase();
  return host === 'qq.com' || host.endsWith('.qq.com') ||
    host === 'gtimg.com' || host.endsWith('.gtimg.com') ||
    host === 'gtimg.cn' || host.endsWith('.gtimg.cn') ||
    host === 'qpic.cn' || host.endsWith('.qpic.cn');
}

export function resolveAuthorizedLauncherContentScope(value, authenticated) {
  const scope = value === 'home' ? 'home' : 'bootstrap';
  if (scope === 'home' && authenticated !== true) {
    throw new Error('请先登录账号，再加载主页内容。');
  }
  return scope;
}

export function getCachedLauncherContentRequest(cache, scope, refresh, loader) {
  if (!(cache instanceof Map) || typeof loader !== 'function') {
    throw new TypeError('启动器内容缓存参数无效。');
  }
  const normalizedScope = scope === 'home' ? 'home' : 'bootstrap';
  if (refresh === true) {
    cache.delete(normalizedScope);
  }
  if (!cache.has(normalizedScope)) {
    cache.set(normalizedScope, Promise.resolve().then(() => loader(normalizedScope)));
  }
  return cache.get(normalizedScope);
}

export function invalidateLauncherHomeContent(cache) {
  return cache instanceof Map ? cache.delete('home') : false;
}

function safeRemoteUrl(value, base = '') {
  let source = text(value, 2048);
  if (!source) {
    return '';
  }
  if (source.startsWith('//')) {
    source = `https:${source}`;
  }
  try {
    const resolved = new URL(source, base ? `${String(base).replace(/\/+$/, '')}/` : undefined);
    return resolved.protocol === 'https:' && allowedOfficialHost(resolved.hostname) ? resolved.href : '';
  } catch {
    return '';
  }
}

function list(value) {
  if (Array.isArray(value)) {
    return value;
  }
  for (const key of ['items', 'Items', 'list', 'List']) {
    if (Array.isArray(value?.[key])) {
      return value[key];
    }
  }
  return [];
}

function firstValue(object, keys) {
  for (const key of keys) {
    if (object?.[key] !== undefined && object?.[key] !== null && object?.[key] !== '') {
      return object[key];
    }
  }
  return '';
}

function decodePublicText(value, maxLength = 160) {
  const encoded = text(value, 2048);
  if (!encoded) return '';
  try {
    return text(decodeURIComponent(encoded), maxLength);
  } catch {
    return text(encoded, maxLength);
  }
}

function bulletinDate(value) {
  const source = text(value, 64);
  const match = source.match(/(?:^|\D)(\d{1,2})-(\d{1,2})(?:\D|$)/);
  return match ? `${match[1].padStart(2, '0')}/${match[2].padStart(2, '0')}` : source.slice(0, 10);
}

function normalizeBulletins(value) {
  if (Number(value?.bulletinType) === 3 && Array.isArray(value?.infoData?.info_sections)) {
    return value.infoData.info_sections.flatMap((section, sectionIndex) =>
      list(section?.infos).slice(0, 3).map((item, index) => ({
        id: text(firstValue(item, ['id', 'release_time']), 64) || `bulletin-${sectionIndex}-${index}`,
        category: text(section?.tag_alias, 24) || '公告',
        title: decodePublicText(item?.title, 160),
        date: bulletinDate(item?.release_time),
        link: safeRemoteUrl(item?.url)
      }))
    ).filter((item) => item.title).slice(0, MAX_ITEMS);
  }

  if (Array.isArray(value?.bulletinData?.tags)) {
    return value.bulletinData.tags.flatMap((tag, tagIndex) =>
      list(tag?.Content).slice(0, 3).map((item, index) => ({
        id: text(firstValue(item, ['iId', 'id']), 64) || `bulletin-${tagIndex}-${index}`,
        category: text(firstValue(tag, ['sTag', 'text', 'iTag']), 24) || '公告',
        title: text(firstValue(item, ['sTitle', 'title', 'Name']), 160),
        date: bulletinDate(firstValue(item, ['sIdxTime', 'sTime', 'date', 'Time'])),
        link: safeRemoteUrl(firstValue(item, ['sRedirectURL', 'link', 'Url']))
      }))
    ).filter((item) => item.title).slice(0, MAX_ITEMS);
  }

  return list(value).slice(0, MAX_ITEMS).map((item, index) => ({
    id: text(firstValue(item, ['iId', 'id']), 64) || `bulletin-${index}`,
    category: text(firstValue(item, ['sType', 'category', 'Type']), 24) || '公告',
    title: text(firstValue(item, ['sTitle', 'title', 'Name']), 160),
    date: text(firstValue(item, ['sTime', 'date', 'Time']), 32),
    link: safeRemoteUrl(firstValue(item, ['sRedirectURL', 'link', 'Url']))
  })).filter((item) => item.title);
}

function defaultOfficialLinks() {
  return [
    { id: 'support', label: '在线客服', url: 'https://kf.qq.com/' },
    { id: 'website', label: '官网首页', url: 'https://speed.qq.com/main.shtml' },
    { id: 'tutorial', label: '教学视频', url: 'https://speed.qq.com/video3/search.shtml?G_biz=8&iType=1095' },
    { id: 'pocket', label: '掌上飞车', url: 'https://speed.qq.com/cp/a20160601zsfc/index.htm#1' },
    { id: 'mall', label: '道聚城', url: 'https://daoju.qq.com/speed/' }
  ];
}

function normalizeReflinks(value) {
  const links = list(value).slice(0, 8).map((item, index) => ({
    id: text(firstValue(item, ['Key', 'key', 'id']), 64) || `link-${index}`,
    label: text(firstValue(item, ['Name', 'name', 'label', 'title']), 24),
    url: safeRemoteUrl(firstValue(list(item?.Hyperlink)[0], ['Url', 'url']))
  })).filter((item) => item.label && item.url);
  return links.length ? links : defaultOfficialLinks();
}

function background(_typeValue, imageValue, _videoValue, cdnHost, fallbackSource = FALLBACK_IMAGE) {
  const image = safeRemoteUrl(imageValue, cdnHost);
  return {
    type: 'video',
    source: LOCAL_BACKGROUND_VIDEO,
    poster: image || fallbackSource
  };
}

function normalizeExplicitStatus(value) {
  const status = text(value, 16).toLowerCase();
  if (['done', 'active', 'upcoming'].includes(status)) {
    return status;
  }
  return '';
}

function timestampMs(value) {
  if (value === undefined || value === null || value === '') return 0;
  const numeric = Number(value);
  if (Number.isFinite(numeric) && numeric > 0) {
    return numeric < 100_000_000_000 ? numeric * 1000 : numeric;
  }
  const parsed = Date.parse(String(value));
  return Number.isFinite(parsed) && parsed > 0 ? parsed : 0;
}

function normalizeTimeline(items, rawProgress, now) {
  let activeIndex = -1;
  for (let index = 0; index < items.length; index += 1) {
    if (!items[index].explicitStatus && items[index].startAt > 0 && items[index].startAt <= now) {
      activeIndex = index;
    }
  }

  const normalizedItems = items.map((item, index) => {
    let status = item.explicitStatus;
    if (!status) {
      if (activeIndex < 0) status = 'upcoming';
      else if (index < activeIndex) status = 'done';
      else if (index === activeIndex) status = 'active';
      else status = 'upcoming';
    }
    return {
      id: item.id,
      title: item.title,
      date: item.date,
      status,
      imageUrl: item.imageUrl || '',
      link: item.link || '',
      description: item.description || ''
    };
  });

  const hasExplicitProgress = rawProgress !== undefined && rawProgress !== null && rawProgress !== '';
  let progress = hasExplicitProgress ? Number(rawProgress) : Number.NaN;
  if (!Number.isFinite(progress)) {
    const markerIndex = normalizedItems.reduce((last, item, index) =>
      item.status === 'done' || item.status === 'active' ? index : last, -1);
    progress = normalizedItems.length > 1 && markerIndex >= 0
      ? Math.round((markerIndex / (normalizedItems.length - 1)) * 100)
      : 0;
  }
  return {
    progress: Math.max(0, Math.min(100, progress)),
    items: normalizedItems
  };
}

export function createFallbackLauncherContent() {
  return {
    schemaVersion: 1,
    source: 'fallback',
    loginVisual: {
      type: 'video',
      source: LOCAL_BACKGROUND_VIDEO,
      poster: FALLBACK_IMAGE
    },
    hero: {
      type: 'video', source: LOCAL_BACKGROUND_VIDEO, poster: FALLBACK_IMAGE,
      title: CUSTOMER_BRAND, subtitle: '准备就绪，选择区服后开始游戏'
    },
    banners: [],
    secondaryActivities: [],
    bulletins: [{ id: 'local-ready', category: '提示', title: '当前为离线内容，联网后将自动获取最新活动与公告', date: '' }],
    timeline: {
      progress: 0,
      items: [{ id: 'local-version', title: '当前版本', date: '', status: 'active' }]
    },
    servers: [{ id: '64', label: '电信区', available: true, selected: true }],
    music: { source: '', title: '' },
    links: defaultOfficialLinks()
  };
}

export function normalizeLauncherContent(rawInput = {}, options = {}) {
  const envelope = rawInput?.Data || rawInput?.data || rawInput;
  const raw = envelope?.AllConf || envelope?.allConf || envelope;
  const adsSource = envelope?.Ads || envelope?.ads || raw?.AdsConf || raw?.adsConf;
  const bulletinSource = envelope?.Bulletin || envelope?.bulletin || raw?.BulletinConf || raw?.bulletinConf;
  const reflinkSource = envelope?.Reflinks || envelope?.reflinks || raw?.Reflinks || raw?.reflinks;
  const now = Number.isFinite(Number(options.now)) ? Number(options.now) : Date.now();
  const cdnHost = safeRemoteUrl(raw?.CDNHost || raw?.cdnHost || '');
  const appConf = raw?.LauncherConf?.AppConf || raw?.launcherConf?.appConf || raw?.AppConf || {};
  const loginVisual = background(
    appConf.LauncherLoginBackgroundType,
    appConf.LauncherLoginBackgroundImage,
    appConf.LauncherLoginBackgroundVideo || appConf.LauncherLoginBackground,
    cdnHost
  );
  const heroBackground = background(
    appConf.LauncherMainBackgroundType,
    appConf.LauncherMainBackgroundImage,
    appConf.LauncherMainBackgroundVideo || appConf.LauncherMainBackgroundContent,
    cdnHost
  );

  const banners = list(adsSource?.ManualContent || adsSource?.manualContent || adsSource).slice(0, MAX_ITEMS).map((item, index) => ({
    id: text(firstValue(item, ['iId', 'id']), 64) || `banner-${index}`,
    title: text(firstValue(item, ['sTitle', 'title', 'Name']), 120),
    subtitle: text(firstValue(item, ['sSubTitle', 'sDesc', 'subtitle', 'Description']), 180),
    imageUrl: safeRemoteUrl(firstValue(item, ['sIMG', 'imageUrl', 'Image']), cdnHost),
    link: safeRemoteUrl(firstValue(item, ['sRedirectURL', 'link', 'Url']))
  })).filter((item) => item.imageUrl);
  const secondaryActivities = list(raw?.LauncherConf?.SubActivityConf || raw?.launcherConf?.subActivityConf)
    .slice(0, 4)
    .map((item, index) => ({
      id: text(firstValue(item, ['Key', 'key', 'id']), 64) || `secondary-${index}`,
      imageUrl: safeRemoteUrl(firstValue(item, ['ActivityImg', 'activityImg', 'imageUrl']), cdnHost),
      link: safeRemoteUrl(firstValue(item, ['ActivityUrl', 'activityUrl', 'link']))
    }))
    .filter((item) => item.imageUrl);

  const bulletins = normalizeBulletins(bulletinSource);

  const timelineItems = list(raw?.EventConfs || raw?.eventConfs).slice(0, 12).map((item, index) => {
    const date = text(firstValue(item, ['sDate', 'date', 'Time', 'content']), 32);
    return {
      id: text(firstValue(item, ['iId', 'id']), 64) || `event-${index}`,
      title: text(firstValue(item, ['sTitle', 'title', 'Name']), 80) || `活动 ${index + 1}`,
      date,
      explicitStatus: normalizeExplicitStatus(firstValue(item, ['status', 'Status'])),
      startAt: timestampMs(firstValue(item, ['start_at', 'startAt', 'StartAt'])) || timestampMs(date),
      imageUrl: safeRemoteUrl(firstValue(item, ['pic_url', 'picUrl', 'PicUrl', 'imageUrl']), cdnHost),
      link: safeRemoteUrl(firstValue(item, ['jump_target', 'jumpTarget', 'JumpTarget', 'link', 'Url'])),
      description: text(firstValue(item, ['date_description', 'dateDescription', 'description']), 120)
    };
  });
  const timeline = normalizeTimeline(timelineItems, raw?.EventProgress, now);

  return {
    schemaVersion: 1,
    source: 'official-live',
    loginVisual,
    hero: {
      ...heroBackground,
      title: CUSTOMER_BRAND,
      subtitle: text(appConf.LauncherMainSubTitle || raw?.SubTitle, 160)
    },
    banners,
    secondaryActivities,
    bulletins,
    timeline,
    servers: [{ id: '64', label: '电信区', available: true, selected: true }],
    // The packaged MP4 carries its own soundtrack; do not layer the
    // official independent music track over it.
    music: { source: '', title: '' },
    links: normalizeReflinks(reflinkSource)
  };
}

function sanitizeCachedContent(parsed) {
  if (parsed?.schemaVersion !== 1 || !parsed?.loginVisual || !parsed?.hero) {
    throw new Error('缓存格式无效。');
  }
  const fallback = createFallbackLauncherContent();
  const sanitizeMedia = (media, defaultMedia) => {
    const remotePoster = safeRemoteUrl(media?.poster) ||
      (media?.type === 'image' ? safeRemoteUrl(media?.source) : '');
    return {
      type: 'video',
      source: LOCAL_BACKGROUND_VIDEO,
      poster: remotePoster || defaultMedia.poster || FALLBACK_IMAGE
    };
  };
  const data = normalizeLauncherContent({
    LauncherConf: {
      SubActivityConf: list(parsed.secondaryActivities).map((item) => ({
        Key: item.id,
        ActivityImg: item.imageUrl,
        ActivityUrl: item.link
      }))
    },
    AdsConf: list(parsed.banners).map((item) => ({
      iId: item.id, sTitle: item.title, sSubTitle: item.subtitle, sIMG: item.imageUrl, sRedirectURL: item.link
    })),
    BulletinConf: list(parsed.bulletins).map((item) => ({
      iId: item.id, sType: item.category, sTitle: item.title, sTime: item.date, sRedirectURL: item.link
    })),
    Reflinks: list(parsed.links).map((item) => ({
      Key: item.id,
      Name: item.label,
      Hyperlink: [{ Url: item.url }]
    })),
    EventConfs: list(parsed.timeline?.items).map((item) => ({
      iId: item.id,
      sTitle: item.title,
      sDate: item.date,
      status: item.status,
      pic_url: item.imageUrl,
      jump_target: item.link,
      date_description: item.description
    })),
    EventProgress: parsed.timeline?.progress
  });
  return {
    ...data,
    source: 'official-cache',
    loginVisual: sanitizeMedia(parsed.loginVisual, fallback.loginVisual),
    hero: {
      ...sanitizeMedia(parsed.hero, fallback.hero),
      title: CUSTOMER_BRAND,
      subtitle: text(parsed.hero?.subtitle, 160)
    },
    music: { source: '', title: '' }
  };
}

function writeCache(settingsDir, data, cacheFile = CACHE_FILES.home) {
  mkdirSync(settingsDir, { recursive: true });
  const destination = join(settingsDir, cacheFile);
  const temporary = `${destination}.${process.pid}.${Date.now()}.tmp`;
  try {
    writeFileSync(temporary, `${JSON.stringify(data, null, 2)}\n`, { encoding: 'utf8', flag: 'wx' });
    renameSync(temporary, destination);
  } finally {
    try { unlinkSync(temporary); } catch { /* renamed or never created */ }
  }
}

function readCache(settingsDir, cacheFile = CACHE_FILES.home) {
  return sanitizeCachedContent(JSON.parse(readFileSync(join(settingsDir, cacheFile), 'utf8')));
}

function tryReadCache(settingsDir, cacheFile) {
  try {
    return readCache(settingsDir, cacheFile);
  } catch {
    return null;
  }
}

function mergePartialHomeContent(normalized, aggregate, settingsDir) {
  if (Object.keys(aggregate).length === Object.keys(CONTENT_ENDPOINTS).length) {
    return normalized;
  }

  const previousHome = tryReadCache(settingsDir, CACHE_FILES.home);
  const previousConfig = tryReadCache(settingsDir, CACHE_FILES.bootstrap) || previousHome;
  const merged = { ...normalized, source: 'official-live-partial' };

  if (!aggregate.AllConf && previousConfig) {
    merged.loginVisual = previousConfig.loginVisual;
    merged.hero = previousConfig.hero;
    merged.secondaryActivities = previousConfig.secondaryActivities;
    merged.timeline = previousConfig.timeline;
    merged.music = previousConfig.music;
  }
  if (!aggregate.Ads && previousHome) {
    merged.banners = previousHome.banners;
  }
  if (!aggregate.Bulletin && previousHome) {
    merged.bulletins = previousHome.bulletins;
  }
  if (!aggregate.Reflinks && previousHome) {
    merged.links = previousHome.links;
  }
  return merged;
}

async function fetchOfficialEndpoint(fetchImpl, name, signal) {
  const path = CONTENT_ENDPOINTS[name];
  const isAllConf = name === 'AllConf';
  const response = await fetchImpl(new URL(path, CONTENT_BASE).href, {
    method: isAllConf ? 'POST' : 'GET',
    headers: { ...OFFICIAL_HEADERS },
    ...(isAllConf ? {
      body: JSON.stringify({})
    } : {}),
    signal
  });
  if (!response?.ok) {
    throw new Error(`${path} HTTP ${response?.status || 0}`);
  }
  const declaredLength = Number(response.headers?.get?.('content-length')) || 0;
  if (declaredLength > MAX_RESPONSE_BYTES) {
    throw new Error(`${path} response too large`);
  }
  const body = await response.text();
  if (Buffer.byteLength(body, 'utf8') > MAX_RESPONSE_BYTES) {
    throw new Error(`${path} response too large`);
  }
  const parsed = JSON.parse(body);
  if (parsed?.ret !== undefined && Number(parsed.ret) !== 0) {
    throw new Error(`${path} ret ${Number(parsed.ret)}`);
  }
  return parsed?.data ?? parsed?.Data ?? parsed;
}

export async function loadLauncherContent(settingsDir, options = {}) {
  const fetchImpl = options.fetchImpl || globalThis.fetch;
  const scope = options.scope === 'bootstrap' ? 'bootstrap' : 'home';
  const cacheFile = CACHE_FILES[scope];
  const timeoutMs = Math.max(100, Math.min(10000, Number(options.timeoutMs) || 3500));
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    if (typeof fetchImpl !== 'function') {
      throw new Error('fetch unavailable');
    }
    // The official renderer mounts only the global configuration/background
    // hook before login. Ads, bulletins and reflinks are mounted with the home
    // branch after onUserLogin populates loginInfo.
    const names = scope === 'bootstrap' ? ['AllConf'] : Object.keys(CONTENT_ENDPOINTS);
    const settled = await Promise.allSettled(
      names.map((name) => fetchOfficialEndpoint(fetchImpl, name, controller.signal))
    );
    const aggregate = {};
    settled.forEach((result, index) => {
      if (result.status === 'fulfilled') {
        aggregate[names[index]] = result.value;
      }
    });
    if (!Object.keys(aggregate).length) {
      throw new Error('all official content endpoints failed');
    }
    const normalized = normalizeLauncherContent(aggregate);
    const resolved = scope === 'home'
      ? mergePartialHomeContent(normalized, aggregate, settingsDir)
      : normalized;
    try {
      writeCache(settingsDir, resolved, cacheFile);
    } catch {
      // A read-only/full profile must not replace valid live content with fallback data.
    }
    return resolved;
  } catch {
    try {
      return readCache(settingsDir, cacheFile);
    } catch {
      return createFallbackLauncherContent();
    }
  } finally {
    clearTimeout(timer);
  }
}
