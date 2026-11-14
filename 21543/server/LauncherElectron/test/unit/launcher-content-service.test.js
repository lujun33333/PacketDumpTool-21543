import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

import {
  createFallbackLauncherContent,
  getCachedLauncherContentRequest,
  invalidateLauncherHomeContent,
  loadLauncherContent,
  normalizeLauncherContent,
  resolveAuthorizedLauncherContentScope
} from '../../src/main/launcher-content-service.js';

const LOCAL_BACKGROUND_VIDEO = '../../assets/launcher-background.mp4';

test('packaged background video drives login and home while official images remain posters', () => {
  const data = normalizeLauncherContent({
    CDNHost: 'https://cdn-launcher.qq.com/media',
    LauncherConf: { AppConf: {
      LauncherLoginBackgroundType: 'image',
      LauncherLoginBackgroundImage: 'login.jpg',
      LauncherMainBackgroundType: 'image',
      LauncherMainBackgroundImage: 'home.jpg',
      LauncherBackgroundMusic: 'theme.mp3'
    } }
  });

  assert.deepEqual(data.loginVisual, {
    type: 'video',
    source: LOCAL_BACKGROUND_VIDEO,
    poster: 'https://cdn-launcher.qq.com/media/login.jpg'
  });
  assert.deepEqual({
    type: data.hero.type,
    source: data.hero.source,
    poster: data.hero.poster
  }, {
    type: 'video',
    source: LOCAL_BACKGROUND_VIDEO,
    poster: 'https://cdn-launcher.qq.com/media/home.jpg'
  });
  assert.deepEqual(data.music, { source: '', title: '' });

  const fallback = createFallbackLauncherContent();
  assert.equal(fallback.loginVisual.type, 'video');
  assert.equal(fallback.loginVisual.source, LOCAL_BACKGROUND_VIDEO);
  assert.equal(fallback.hero.type, 'video');
  assert.equal(fallback.hero.source, LOCAL_BACKGROUND_VIDEO);
});

test('official CDN allowlist matches renderer support for gtimg.cn and qpic.cn', () => {
  const data = normalizeLauncherContent({
    LauncherConf: { AppConf: {
      LauncherMainBackgroundImage: 'https://game.gtimg.cn/images/speed/home.jpg',
      LauncherBackgroundMusic: 'https://music.qpic.cn/speed/theme.mp3'
    } },
    AdsConf: [
      { sTitle: '活动一', sIMG: 'https://music.qpic.cn/speed/banner.jpg' },
      { sTitle: '活动二', sIMG: 'https://ossweb-img.qq.com/images/banner.jpg' }
    ]
  });

  assert.equal(data.hero.source, LOCAL_BACKGROUND_VIDEO);
  assert.equal(data.hero.poster, 'https://game.gtimg.cn/images/speed/home.jpg');
  assert.equal(data.music.source, '');
  assert.equal(data.banners[0].imageUrl, 'https://music.qpic.cn/speed/banner.jpg');
  assert.equal(data.banners[1].imageUrl, 'https://ossweb-img.qq.com/images/banner.jpg');
});

test('launcher content keeps the customer-facing QQ飞车 brand even when remote content supplies another title', () => {
  assert.equal(createFallbackLauncherContent().hero.title, 'QQ飞车');
  assert.equal(normalizeLauncherContent({
    LauncherConf: { AppConf: { LauncherMainTitle: 'QQ飞车' } }
  }).hero.title, 'QQ飞车');
});

test('main content scope rejects home before authentication', () => {
  assert.equal(resolveAuthorizedLauncherContentScope('bootstrap', false), 'bootstrap');
  assert.equal(resolveAuthorizedLauncherContentScope('unexpected', false), 'bootstrap');
  assert.equal(resolveAuthorizedLauncherContentScope('home', true), 'home');
  assert.throws(
    () => resolveAuthorizedLauncherContentScope('home', false),
    /请先登录账号.*主页内容/
  );
});

test('logout invalidates the cached home request so relogin loads all feeds again', async () => {
  const cache = new Map();
  let loads = 0;
  const loader = async () => ({ generation: ++loads });

  assert.deepEqual(
    await getCachedLauncherContentRequest(cache, 'home', false, loader),
    { generation: 1 }
  );
  assert.deepEqual(
    await getCachedLauncherContentRequest(cache, 'home', false, loader),
    { generation: 1 }
  );
  assert.equal(loads, 1);

  invalidateLauncherHomeContent(cache);
  assert.deepEqual(
    await getCachedLauncherContentRequest(cache, 'home', false, loader),
    { generation: 2 }
  );
  assert.equal(loads, 2);
});

test('relogin re-requests all four official home endpoints after cache invalidation', async () => {
  const root = mkdtempSync(join(tmpdir(), 'launcher-content-test-'));
  const cache = new Map();
  const requests = [];
  try {
    const fetchImpl = async (url) => {
      const path = new URL(url).pathname;
      requests.push(path);
      const data = path.endsWith('/get_all_conf')
        ? { LauncherConf: { AppConf: {} } }
        : path.endsWith('/get_ads')
          ? { items: [] }
          : path.endsWith('/get_bulletin')
            ? { items: [] }
            : [];
      return new Response(JSON.stringify({ ret: 0, data }), { status: 200 });
    };
    const loader = (scope) => loadLauncherContent(root, {
      scope,
      fetchImpl,
      timeoutMs: 1000
    });

    await getCachedLauncherContentRequest(cache, 'home', false, loader);
    await getCachedLauncherContentRequest(cache, 'home', false, loader);
    assert.equal(requests.length, 4);

    invalidateLauncherHomeContent(cache);
    await getCachedLauncherContentRequest(cache, 'home', false, loader);
    assert.deepEqual(
      Object.fromEntries([...new Set(requests)].map((path) => [
        path,
        requests.filter((entry) => entry === path).length
      ])),
      {
        '/basesrv/get_all_conf': 2,
        '/basesrv/get_ads': 2,
        '/basesrv/get_bulletin': 2,
        '/basesrv/get_reflinks': 2
      }
    );
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('normalizes official-style config into a bounded renderer DTO', () => {
  const data = normalizeLauncherContent({
    CDNHost: 'https://cdn-launcher.qq.com/media',
    LauncherConf: { AppConf: {
      LauncherLoginBackgroundType: 'video',
      LauncherLoginBackgroundVideo: 'login.mp4',
      LauncherLoginBackgroundImage: 'login.jpg',
      LauncherMainBackgroundType: 'image',
      LauncherMainBackgroundImage: 'home.jpg'
    } },
    AdsConf: { items: [{ iId: 7, sTitle: '活动', sIMG: '//cdn-launcher.qq.com/banner.jpg', sRedirectURL: 'https://speed.qq.com/news' }] },
    BulletinConf: { items: [{ iId: 8, sTitle: '维护公告', sTime: '07/16', sType: '公告' }] },
    EventConfs: [{ iId: 9, sTitle: '新版本', sDate: '2026-07-16', status: 'active' }]
  });

  assert.deepEqual(data.loginVisual, {
    type: 'video',
    source: LOCAL_BACKGROUND_VIDEO,
    poster: 'https://cdn-launcher.qq.com/media/login.jpg'
  });
  assert.equal(data.hero.source, LOCAL_BACKGROUND_VIDEO);
  assert.equal(data.hero.poster, 'https://cdn-launcher.qq.com/media/home.jpg');
  assert.equal(data.banners[0].imageUrl, 'https://cdn-launcher.qq.com/banner.jpg');
  assert.equal(data.banners[0].link, 'https://speed.qq.com/news');
  assert.equal(data.bulletins[0].title, '维护公告');
  assert.equal(data.timeline.items[0].status, 'active');
  assert.equal(data.source, 'official-live');
});

test('aggregates the official homepage endpoints instead of inventing bulletin data', () => {
  const data = normalizeLauncherContent({
    AllConf: {
      CDNHost: 'https://cdn-launcher.qq.com/383998722/service',
      LauncherConf: {
        AppConf: { LauncherMainBackgroundImage: 'home.png' },
        SubActivityConf: [{ ActivityImg: 'release-strip.png', ActivityUrl: 'https://speed.qq.com/release-strip.shtml' }]
      },
      EventConfs: [{
        id: 'release', title: '连胜之星', content: '2026-04-23', status: 'active',
        pic_url: 'release.png', jump_target: 'https://speed.qq.com/release.shtml',
        date_description: '4月23日版本更新'
      }]
    },
    Ads: {
      items: [{ iId: 'a1', sTitle: '动态活动', sDesc: '限时', sIMG: '//cdn-launcher.qq.com/banner.png' }]
    },
    Bulletin: {
      bulletinType: 3,
      infoData: {
        info_sections: [
          { tag_alias: '公告', infos: [{
            release_time: '2026-07-14 21:04:49',
            title: '7%E6%9C%8815%E6%97%A5%E4%B8%8D%E5%81%9C%E6%9C%BA%E6%9B%B4%E6%96%B0%E5%85%AC%E5%91%8A',
            url: 'https://speed.qq.com/news.shtml'
          }] },
          { tag_alias: '赛事', infos: [{
            release_time: '2026-06-28 19:12:09',
            title: '%E5%85%A8%E5%9B%BD%E5%85%AC%E5%BC%80%E8%B5%9B',
            url: 'https://speed.qq.com/esport.shtml'
          }] }
        ]
      }
    },
    Reflinks: [{
      Key: 'official',
      Name: '官网首页',
      Hyperlink: [{ Url: 'https://speed.qq.com/main.shtml' }]
    }]
  });

  assert.equal(data.hero.source, LOCAL_BACKGROUND_VIDEO);
  assert.equal(data.hero.poster, 'https://cdn-launcher.qq.com/383998722/service/home.png');
  assert.equal(data.banners[0].title, '动态活动');
  assert.equal(data.banners[0].subtitle, '限时');
  assert.deepEqual(data.bulletins.map((item) => [item.category, item.title, item.date]), [
    ['公告', '7月15日不停机更新公告', '07/14'],
    ['赛事', '全国公开赛', '06/28']
  ]);
  assert.deepEqual(data.links, [{ id: 'official', label: '官网首页', url: 'https://speed.qq.com/main.shtml' }]);
  assert.deepEqual({
    imageUrl: data.timeline.items[0].imageUrl,
    link: data.timeline.items[0].link,
    description: data.timeline.items[0].description
  }, {
    imageUrl: 'https://cdn-launcher.qq.com/383998722/service/release.png',
    link: 'https://speed.qq.com/release.shtml',
    description: '4月23日版本更新'
  });
  assert.deepEqual(data.secondaryActivities, [{
    id: 'secondary-0',
    imageUrl: 'https://cdn-launcher.qq.com/383998722/service/release-strip.png',
    link: 'https://speed.qq.com/release-strip.shtml'
  }]);
});

test('drops untrusted or insecure remote URLs', () => {
  const data = normalizeLauncherContent({
    CDNHost: 'https://evil.example',
    LauncherConf: { AppConf: {
      LauncherLoginBackgroundType: 'video',
      LauncherLoginBackgroundVideo: 'https://evil.example/a.mp4',
      LauncherMainBackgroundImage: 'http://speed.qq.com/plain.jpg'
    } },
    AdsConf: [{ sTitle: 'x', sIMG: 'javascript:alert(1)', sRedirectURL: 'file:///c:/secret' }]
  });
  assert.equal(data.loginVisual.type, 'video');
  assert.equal(data.loginVisual.source, LOCAL_BACKGROUND_VIDEO);
  assert.match(data.loginVisual.poster, /login-bg-default/);
  assert.equal(data.banners.length, 0);
});

test('derives official timeline state from ordered start times instead of long-lived end times', () => {
  const now = Date.parse('2026-07-16T00:00:00Z');
  const data = normalizeLauncherContent({
    EventConfs: [
      { title: 'A', content: '2025-08-25', start_at: Date.parse('2025-08-25T00:00:00Z'), end_at: Date.parse('2029-01-01T00:00:00Z') },
      { title: 'B', content: '2025-10-27', start_at: Date.parse('2025-10-27T00:00:00Z'), end_at: Date.parse('2028-01-01T00:00:00Z') },
      { title: 'C', content: '2026-04-23', start_at: Date.parse('2026-04-23T00:00:00Z'), end_at: Date.parse('2027-01-01T00:00:00Z') },
      { title: 'D', content: '2026-10-01', start_at: Date.parse('2026-10-01T00:00:00Z'), end_at: Date.parse('2029-01-01T00:00:00Z') }
    ]
  }, { now });

  assert.deepEqual(data.timeline.items.map((item) => item.status), ['done', 'done', 'active', 'upcoming']);
  assert.equal(data.timeline.progress, 67);
});

test('normalizes second timestamps and preserves an explicit server timeline progress', () => {
  const now = Date.parse('2026-07-16T00:00:00Z');
  const data = normalizeLauncherContent({
    EventProgress: 42,
    EventConfs: [
      { title: 'A', start_at: Math.floor(Date.parse('2026-01-01T00:00:00Z') / 1000) },
      { title: 'B', start_at: Math.floor(Date.parse('2026-10-01T00:00:00Z') / 1000) }
    ]
  }, { now });

  assert.deepEqual(data.timeline.items.map((item) => item.status), ['active', 'upcoming']);
  assert.equal(data.timeline.progress, 42);
});

test('network success writes only normalized cache and network failure reuses it', async () => {
  const root = mkdtempSync(join(tmpdir(), 'launcher-content-test-'));
  try {
    const response = {
      CDNHost: 'https://cdn-launcher.qq.com',
      LauncherConf: { AppConf: { LauncherMainBackgroundImage: 'home.jpg' } },
      AdsConf: [{ iId: 1, sTitle: 'cached', sIMG: 'banner.jpg' }],
      secretUnexpectedField: 'must-not-be-cached'
    };
    const first = await loadLauncherContent(root, {
      fetchImpl: async (url) => {
        const path = new URL(url).pathname;
        const data = path.endsWith('/get_all_conf')
          ? response
          : path.endsWith('/get_ads')
            ? response.AdsConf
            : path.endsWith('/get_bulletin')
              ? { items: [] }
              : [];
        return new Response(JSON.stringify({ ret: 0, data }), { status: 200 });
      },
      timeoutMs: 1000
    });
    assert.equal(first.source, 'official-live');
    const cacheText = readFileSync(join(root, 'launcher-content-cache.json'), 'utf8');
    assert.doesNotMatch(cacheText, /secretUnexpectedField/);

    const second = await loadLauncherContent(root, {
      fetchImpl: async () => { throw new Error('offline'); },
      timeoutMs: 1000
    });
    assert.equal(second.source, 'official-cache');
    assert.equal(second.banners[0].title, 'cached');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('partial home revalidation keeps cached feeds while applying fresh configuration', async () => {
  const root = mkdtempSync(join(tmpdir(), 'launcher-content-test-'));
  try {
    const ok = (data) => new Response(JSON.stringify({ ret: 0, data }), { status: 200 });
    await loadLauncherContent(root, {
      fetchImpl: async (url) => {
        const path = new URL(url).pathname;
        if (path.endsWith('/get_all_conf')) {
          return ok({ LauncherConf: { AppConf: { LauncherMainBackgroundImage: 'https://cdn-launcher.qq.com/old-home.png' } } });
        }
        if (path.endsWith('/get_ads')) {
          return ok({ items: [{ iId: 1, sTitle: '缓存活动', sIMG: 'https://cdn-launcher.qq.com/cached-ad.png' }] });
        }
        if (path.endsWith('/get_bulletin')) {
          return ok({ items: [{ iId: 2, sTitle: '缓存公告', sType: '公告' }] });
        }
        return ok([{ Key: 'site', Name: '官网首页', Hyperlink: [{ Url: 'https://speed.qq.com/main.shtml' }] }]);
      },
      timeoutMs: 1000
    });

    const refreshed = await loadLauncherContent(root, {
      fetchImpl: async (url) => {
        const path = new URL(url).pathname;
        if (path.endsWith('/get_all_conf')) {
          return ok({ LauncherConf: { AppConf: { LauncherMainBackgroundImage: 'https://cdn-launcher.qq.com/new-home.png' } } });
        }
        throw new Error('feed temporarily unavailable');
      },
      timeoutMs: 1000
    });

    assert.equal(refreshed.hero.source, LOCAL_BACKGROUND_VIDEO);
    assert.equal(refreshed.hero.poster, 'https://cdn-launcher.qq.com/new-home.png');
    assert.equal(refreshed.banners[0].title, '缓存活动');
    assert.equal(refreshed.bulletins[0].title, '缓存公告');
    assert.equal(refreshed.links[0].label, '官网首页');
    assert.equal(refreshed.source, 'official-live-partial');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('loads the same public homepage endpoints used by the official renderer', async () => {
  const root = mkdtempSync(join(tmpdir(), 'launcher-content-test-'));
  const requests = [];
  try {
    const result = await loadLauncherContent(root, {
      fetchImpl: async (url, options) => {
        requests.push({ url, options });
        const path = new URL(url).pathname;
        const data = path.endsWith('/get_all_conf')
          ? { LauncherConf: { AppConf: { LauncherMainBackgroundImage: 'https://cdn-launcher.qq.com/home.png' } } }
          : path.endsWith('/get_ads')
            ? { items: [{ sTitle: '活动', sIMG: 'https://cdn-launcher.qq.com/ad.png' }] }
            : path.endsWith('/get_bulletin')
              ? { bulletinType: 3, infoData: { info_sections: [] } }
              : [{ Key: 'site', Name: '官网首页', Hyperlink: [{ Url: 'https://speed.qq.com/main.shtml' }] }];
        return new Response(JSON.stringify({ ret: 0, data }), { status: 200 });
      },
      timeoutMs: 1000
    });

    assert.deepEqual(
      requests.map((request) => [new URL(request.url).pathname, request.options.method || 'GET']).sort(),
      [
        ['/basesrv/get_ads', 'GET'],
        ['/basesrv/get_all_conf', 'POST'],
        ['/basesrv/get_bulletin', 'GET'],
        ['/basesrv/get_reflinks', 'GET']
      ]
    );
    assert.equal(result.banners[0].title, '活动');
    assert.equal(result.links[0].label, '官网首页');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('login bootstrap requests only get_all_conf before authentication', async () => {
  const root = mkdtempSync(join(tmpdir(), 'launcher-content-test-'));
  const requests = [];
  try {
    const result = await loadLauncherContent(root, {
      scope: 'bootstrap',
      fetchImpl: async (url, options) => {
        requests.push({ url, options });
        return new Response(JSON.stringify({
          ret: 0,
          data: {
            LauncherConf: {
              AppConf: {
                LauncherLoginBackgroundImage: 'https://cdn-launcher.qq.com/login.png'
              }
            }
          }
        }), { status: 200 });
      },
      timeoutMs: 1000
    });

    assert.deepEqual(requests.map(({ url, options }) => [
      new URL(url).pathname,
      options.method || 'GET'
    ]), [['/basesrv/get_all_conf', 'POST']]);
    assert.equal(result.loginVisual.source, LOCAL_BACKGROUND_VIDEO);
    assert.equal(result.loginVisual.poster, 'https://cdn-launcher.qq.com/login.png');
    assert.equal(existsSync(join(root, 'launcher-bootstrap-cache.json')), true);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('a cache write failure does not discard successfully fetched live content', async () => {
  const root = mkdtempSync(join(tmpdir(), 'launcher-content-test-'));
  const blockedSettingsPath = join(root, 'not-a-directory');
  writeFileSync(blockedSettingsPath, 'blocked', 'utf8');
  try {
    const result = await loadLauncherContent(blockedSettingsPath, {
      fetchImpl: async (url) => {
        const path = new URL(url).pathname;
        const data = path.endsWith('/get_all_conf')
          ? { LauncherConf: { AppConf: { LauncherMainBackgroundImage: 'https://cdn-launcher.qq.com/live.png' } } }
          : path.endsWith('/get_ads') ? { items: [] }
            : path.endsWith('/get_bulletin') ? { items: [] } : [];
        return new Response(JSON.stringify({ ret: 0, data }), { status: 200 });
      },
      timeoutMs: 1000
    });

    assert.equal(result.source, 'official-live');
    assert.equal(result.hero.source, LOCAL_BACKGROUND_VIDEO);
    assert.equal(result.hero.poster, 'https://cdn-launcher.qq.com/live.png');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('official content request carries the launcher identity headers and rejects nonzero ret', async () => {
  const root = mkdtempSync(join(tmpdir(), 'launcher-content-test-'));
  try {
    const requests = [];
    const result = await loadLauncherContent(root, {
      fetchImpl: async (url, options) => {
        requests.push({ url, options });
        return new Response(JSON.stringify({ ret: 400, msg: 'bad request' }), { status: 200 });
      },
      timeoutMs: 1000
    });
    const request = requests.find((entry) => entry.url.endsWith('/get_all_conf'));
    assert.ok(request);
    assert.match(request.url, /\/basesrv\/get_all_conf$/);
    assert.equal(request.options.headers['L-Game-Id'], '383998722');
    assert.equal(request.options.headers['L-Dolphin-Channel-Id'], '117091501');
    assert.equal(request.options.headers['L-Launcher-Version'], '1.0.0.14');
    assert.deepEqual(JSON.parse(request.options.body), {});
    assert.equal(requests.length, 4);
    assert.equal(result.source, 'fallback');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('first-run offline path is a structured fallback, never an entire-page screenshot', async () => {
  const root = mkdtempSync(join(tmpdir(), 'launcher-content-test-'));
  try {
    const fallback = await loadLauncherContent(root, {
      fetchImpl: async () => { throw new Error('offline'); },
      timeoutMs: 20
    });
    assert.deepEqual(fallback, createFallbackLauncherContent());
    assert.doesNotMatch(JSON.stringify(fallback), /home-bg-official-qqspeed/);
    assert.ok(fallback.bulletins.length > 0);
    assert.ok(fallback.timeline.items.length > 0);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
