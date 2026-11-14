import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

import {
  computeTimelineProgress,
  DEFAULT_LAUNCHER_CONTENT,
  normalizeLauncherContent,
  renderLauncherContent,
  sanitizeMediaSource
} from '../../src/renderer/content-view.js';
import {
  DEFAULT_MEDIA_PREFERENCES,
  MediaController,
  normalizeMediaPreferences
} from '../../src/renderer/media-controller.js';

const projectRoot = new URL('../../', import.meta.url);

class FakeClassList {
  constructor(values = []) {
    this.values = new Set(values);
  }

  add(...values) {
    values.forEach((value) => this.values.add(value));
  }

  remove(...values) {
    values.forEach((value) => this.values.delete(value));
  }

  contains(value) {
    return this.values.has(value);
  }

  toggle(value, force) {
    const enabled = force === undefined ? !this.contains(value) : Boolean(force);
    if (enabled) {
      this.add(value);
    } else {
      this.remove(value);
    }
    return enabled;
  }
}

class FakeElement {
  constructor(tagName = 'div', { classes = [] } = {}) {
    this.tagName = tagName.toUpperCase();
    this.classList = new FakeClassList(classes);
    this.dataset = {};
    this.children = [];
    this.attributes = new Map();
    this.style = { setProperty: () => {} };
    this.src = '';
    this.value = '';
    this.disabled = false;
    this.listeners = new Map();
  }

  addEventListener(type, listener) {
    const listeners = this.listeners.get(type) || [];
    listeners.push(listener);
    this.listeners.set(type, listeners);
  }

  dispatch(type) {
    for (const listener of this.listeners.get(type) || []) {
      listener({ target: this });
    }
  }

  appendChild(child) {
    this.children.push(child);
    return child;
  }

  append(...children) {
    this.children.push(...children);
  }

  replaceChildren(...children) {
    this.children = children;
  }

  setAttribute(name, value) {
    this.attributes.set(name, String(value));
  }

  pause() {}

  load() {}

  removeAttribute(name) {
    if (name === 'src') {
      this.src = '';
    }
    this.attributes.delete(name);
  }
}

class FakeMediaElement extends FakeElement {
  constructor({ classes = [], playResults = [] } = {}) {
    super('video', { classes });
    this.muted = true;
    this.volume = 1;
    this.playResults = [...playResults];
    this.playCalls = 0;
    this.pauseCalls = 0;
  }

  play() {
    this.playCalls += 1;
    return this.playResults.shift() ?? Promise.resolve();
  }

  pause() {
    this.pauseCalls += 1;
  }
}

function createMediaFixture(playResults = [], storedPreferences = null, storageOverride) {
  const elements = {
    app: new FakeElement('div'),
    launcherBackgroundVideo: new FakeMediaElement({ playResults }),
    launcherMediaFallback: new FakeElement('div', { classes: ['hidden'] }),
    launcherMediaStatus: new FakeElement('p', { classes: ['hidden'] }),
    mediaControls: new FakeElement('aside', { classes: ['hidden'] }),
    videoControlItem: new FakeElement('div', { classes: ['hidden'] }),
    videoMuteBtn: new FakeElement('button'),
    videoVolume: new FakeElement('input')
  };
  elements.launcherBackgroundVideo.src = '../../assets/launcher-background.mp4';
  const documentListeners = new Map();
  const documentRef = {
    hidden: false,
    getElementById: (id) => elements[id] || null,
    addEventListener(type, listener) {
      const listeners = documentListeners.get(type) || [];
      listeners.push(listener);
      documentListeners.set(type, listeners);
    },
    dispatch(type, init = {}) {
      for (const listener of documentListeners.get(type) || []) {
        listener({ target: this, ...init });
      }
    }
  };
  const storage = storageOverride || {
    value: storedPreferences === null ? null : JSON.stringify(storedPreferences),
    getItem() {
      return this.value;
    },
    setItem(_key, value) {
      this.value = value;
    }
  };
  return { controller: new MediaController(documentRef, storage), documentRef, elements, storage };
}

test('media preferences contain only video sound and default to audible 70 percent', () => {
  assert.deepEqual(DEFAULT_MEDIA_PREFERENCES, {
    videoMuted: false,
    videoVolume: 0.7
  });
  assert.deepEqual(normalizeMediaPreferences({
    videoMuted: true,
    videoVolume: 4,
    musicMuted: false,
    musicVolume: 0.1
  }), {
    videoMuted: true,
    videoVolume: 1
  });
});

test('one shared video keeps playing across login and home views', async () => {
  const { controller, elements } = createMediaFixture([Promise.resolve()]);
  await controller.playBackgroundVideo();
  controller.setView('home');
  controller.setView('login');
  assert.equal(elements.launcherBackgroundVideo.playCalls, 1);
  assert.equal(elements.launcherBackgroundVideo.pauseCalls, 0);
  assert.equal(elements.launcherBackgroundVideo.muted, false);
  assert.equal(elements.launcherBackgroundVideo.volume, 0.7);
});

test('autoplay rejection waits for a user gesture without changing mute preference', async () => {
  const rejection = Object.assign(new Error('autoplay blocked'), { name: 'NotAllowedError' });
  const { controller, documentRef, elements, storage } = createMediaFixture([
    Promise.reject(rejection),
    Promise.resolve()
  ]);
  await controller.playBackgroundVideo();
  assert.equal(elements.launcherBackgroundVideo.playCalls, 1);
  assert.equal(elements.launcherBackgroundVideo.muted, false);
  assert.equal(JSON.parse(storage.value).videoMuted, false);
  documentRef.dispatch('pointerdown');
  await new Promise((resolve) => setTimeout(resolve, 0));
  assert.equal(elements.launcherBackgroundVideo.playCalls, 2);
  assert.equal(elements.launcherBackgroundVideo.muted, false);
});

test('visibility pauses and resumes the shared video', async () => {
  const { controller, documentRef, elements } = createMediaFixture([
    Promise.resolve(),
    Promise.resolve()
  ]);
  await controller.playBackgroundVideo();
  documentRef.hidden = true;
  documentRef.dispatch('visibilitychange');
  assert.equal(elements.launcherBackgroundVideo.pauseCalls, 1);
  documentRef.hidden = false;
  documentRef.dispatch('visibilitychange');
  await new Promise((resolve) => setTimeout(resolve, 0));
  assert.equal(elements.launcherBackgroundVideo.playCalls, 2);
});

test('video load failure exposes a neutral fallback and does not create an image fallback', () => {
  const { elements } = createMediaFixture();
  elements.launcherBackgroundVideo.dispatch('error');
  assert.equal(elements.app.classList.contains('media-failed'), true);
  assert.equal(elements.launcherMediaFallback.classList.contains('hidden'), false);
  assert.equal(elements.launcherMediaStatus.classList.contains('hidden'), false);
  assert.equal(elements.videoMuteBtn.disabled, true);
});

function createRenderFixture() {
  const bannerTrack = new FakeElement('div');
  const secondaryBanner = new FakeElement('div');
  const timeline = new FakeElement('div');
  const timelineProgress = new FakeElement('span');
  const homeTickerText = new FakeElement('strong');
  const serverSelect = new FakeElement('select');
  const serverSelectLabel = new FakeElement('span');
  const serverRecentSection = new FakeElement('section', { classes: ['hidden'] });
  const serverRecentList = new FakeElement('div');
  const serverList = new FakeElement('div');
  const elements = {
    homeBannerTrack: bannerTrack,
    homeSecondaryBanner: secondaryBanner,
    homeBulletinList: new FakeElement('ul'),
    homeTickerText,
    releaseTimeline: timeline,
    timelineProgressValue: timelineProgress,
    serverSelect,
    serverSelectLabel,
    serverRecentSection,
    serverRecentList,
    serverList
  };
  return {
    bannerTrack,
    secondaryBanner,
    timeline,
    timelineProgress,
    serverSelect,
    serverSelectLabel,
    serverRecentSection,
    serverRecentList,
    serverList,
    documentRef: {
      getElementById: (id) => elements[id] || null,
      createElement: (tagName) => new FakeElement(tagName),
      createDocumentFragment: () => new FakeElement('#fragment')
    }
  };
}

test('home markup exposes a floating activity popover and compact dock', async () => {
  const html = await readFile(new URL('src/renderer/index.html', projectRoot), 'utf8');
  for (const id of [
    'homeAnnouncementTicker',
    'homeTickerText',
    'homeActivityBtn',
    'homeActivityPopover',
    'homeActivityCloseBtn',
    'homeDockAccountName',
    'gmItemBrowserBtn'
  ]) {
    assert.match(html, new RegExp(`id="${id}"`), id);
  }
  assert.doesNotMatch(html, /id="gmItemBrowserMenuBtn"/);
});

test('the lightweight ticker mirrors the first normalized bulletin', () => {
  const { documentRef } = createRenderFixture();
  renderLauncherContent(documentRef, {
    bulletins: [
      { id: 'notice-1', category: '公告', title: '7月版本更新已开放', date: '07/23' }
    ]
  });
  assert.equal(documentRef.getElementById('homeTickerText').textContent, '7月版本更新已开放');
});

test('remote launcher DTO keeps copy as inert text while rejecting executable media URLs', () => {
  const content = normalizeLauncherContent({
    loginVisual: {
      type: 'video',
      source: 'https://cdn-launcher.qq.com/media/login.mp4',
      poster: 'https://cdn-launcher.qq.com/media/login.jpg'
    },
    hero: {
      type: 'image',
      source: 'https://cdn-launcher.qq.com/media/home.jpg',
      title: '<img src=x onerror=alert(1)>连胜之星',
      subtitle: '超越不息'
    },
    banners: [
      { id: 'one', title: '<script>alert(1)</script>五虎上将', imageUrl: 'javascript:alert(1)' },
      { id: 'two', title: '版本更新', imageUrl: 'https://game.gtimg.cn/images/speed/banner.jpg' }
    ],
    secondaryActivities: [{
      id: 'sub-activity',
      imageUrl: 'https://cdn-launcher.qq.com/sub-activity.png',
      link: 'https://speed.qq.com/sub-activity.html'
    }],
    bulletins: [{ id: 'n1', category: '公告', title: '排位赛公告', date: '07/07' }],
    timeline: {
      items: [
        { id: 'v1', title: '跃动大闯关', date: '2025-08-25', status: 'done' },
        { id: 'v2', title: '连胜之星', date: '2026-04-23', status: 'active' }
      ]
    },
    servers: [
      { id: '64', label: '电信区', available: true, selected: true },
      { id: '65', label: '网通区', available: false }
    ],
    music: { source: 'https://cdn-launcher.qq.com/media/theme.mp3', title: '大厅主题曲' }
  });

  assert.equal(content.hero.title, 'QQ飞车');
  assert.equal(content.hero.subtitle, '超越不息');
  assert.equal('loginVisual' in content, false);
  assert.equal('music' in content, false);
  assert.equal(content.banners.length, 2);
  assert.equal(content.banners[0].imageUrl, '');
  assert.equal(content.banners[1].imageUrl, 'https://game.gtimg.cn/images/speed/banner.jpg');
  assert.equal(content.secondaryActivities[0].id, 'sub-activity');
  assert.equal(content.servers[0].selected, true);
});

test('media source allow-list rejects executable and unrelated remote URLs', () => {
  assert.equal(sanitizeMediaSource('javascript:alert(1)'), '');
  assert.equal(sanitizeMediaSource('data:text/html,<script>alert(1)</script>'), '');
  assert.equal(sanitizeMediaSource('https://example.org/a.mp4'), '');
  assert.equal(
    sanitizeMediaSource('https://cdn-launcher.qq.com/a/b.mp4'),
    'https://cdn-launcher.qq.com/a/b.mp4'
  );
  assert.equal(
    sanitizeMediaSource('https://ossweb-img.qq.com/game.gtimg.com/images/speed/home.jpg'),
    'https://ossweb-img.qq.com/game.gtimg.com/images/speed/home.jpg'
  );
  assert.equal(sanitizeMediaSource('https://game.gtimg.com/a.jpg'), 'https://game.gtimg.com/a.jpg');
  assert.equal(
    sanitizeMediaSource('../../assets/login-bg-default-BpD5IuOu.png'),
    '../../assets/login-bg-default-BpD5IuOu.png'
  );
});

test('local media source only accepts traversal-free paths inside the assets directory', () => {
  assert.equal(sanitizeMediaSource('../../assets/media/login/loop.mp4'), '../../assets/media/login/loop.mp4');
  assert.equal(sanitizeMediaSource('../../assets/../src/renderer/app.js'), '');
  assert.equal(sanitizeMediaSource('../../assets/media/../../src/preload/preload.cjs'), '');
  assert.equal(sanitizeMediaSource('./../main/index.js'), '');
  assert.equal(sanitizeMediaSource('./local-video.mp4'), '');
});

test('explicit empty live subtitle stays empty instead of claiming the content service failed', () => {
  const content = normalizeLauncherContent({
    hero: {
      type: 'image',
      source: 'https://cdn-launcher.qq.com/live.jpg',
      title: 'QQ飞车',
      subtitle: ''
    }
  });

  assert.equal(content.hero.subtitle, '');
});

test('navigation normalization removes entries that do not have an allowed URL', () => {
  const content = normalizeLauncherContent({
    links: [
      { id: 'missing', label: '无效入口', url: '' },
      { id: 'rejected', label: '非信任域', url: 'https://example.org/' },
      { id: 'local', label: '本地资源', url: '../../assets/login-bg-default-BpD5IuOu.png' },
      { id: 'official', label: '官网', url: 'https://speed.qq.com/' }
    ]
  });

  assert.deepEqual(content.links, [{
    id: 'official',
    label: '官网',
    url: 'https://speed.qq.com/'
  }]);
});

test('bulletin normalization keeps only the first three live entries in each category', () => {
  const bulletins = [
    ...Array.from({ length: 4 }, (_, index) => ({
      id: `notice-${index + 1}`,
      category: '公告',
      title: `公告 ${index + 1}`
    })),
    ...Array.from({ length: 4 }, (_, index) => ({
      id: `match-${index + 1}`,
      category: '赛事',
      title: `赛事 ${index + 1}`
    }))
  ];

  const content = normalizeLauncherContent({ bulletins });
  assert.deepEqual(content.bulletins.map((item) => item.id), [
    'notice-1',
    'notice-2',
    'notice-3',
    'match-1',
    'match-2',
    'match-3'
  ]);
});

test('banner without a URL renders as non-interactive content', () => {
  const { bannerTrack, documentRef } = createRenderFixture();
  renderLauncherContent(documentRef, {
    banners: [
      { id: 'static', title: '静态活动' },
      { id: 'linked', title: '官网活动', link: 'https://speed.qq.com/' }
    ]
  });

  const [staticBanner, linkedBanner] = bannerTrack.children[0].children;
  assert.equal(staticBanner.tagName, 'ARTICLE');
  assert.equal(staticBanner.dataset.linkUrl, undefined);
  assert.equal(staticBanner.dataset.bannerTitle, '静态活动');
  assert.equal(linkedBanner.tagName, 'BUTTON');
  assert.equal(linkedBanner.dataset.linkUrl, 'https://speed.qq.com/');
});

test('secondary activity strip uses the dedicated live image instead of a fixed screenshot or event thumbnail', () => {
  const { secondaryBanner, documentRef } = createRenderFixture();
  renderLauncherContent(documentRef, {
    hero: {
      type: 'image',
      source: 'https://cdn-launcher.qq.com/live-home.png',
      title: 'QQ飞车',
      subtitle: ''
    },
    banners: [{
      id: 'release-link',
      title: '官方活动',
      imageUrl: 'https://cdn-launcher.qq.com/release-art.png',
      link: 'https://speed.qq.com/release.html'
    }],
    secondaryActivities: [{
      id: 'sub-live',
      imageUrl: 'https://cdn-launcher.qq.com/sub-live.png',
      link: 'https://speed.qq.com/sub-live.html'
    }],
    timeline: {
      items: [
        { id: 'old', title: '旧版本', date: '2026-03-12', status: 'done' },
        {
          id: 'live',
          title: '连胜之星',
          date: '2026-04-23',
          status: 'active',
          imageUrl: 'https://cdn-launcher.qq.com/event-art.png',
          link: 'https://speed.qq.com/event.html'
        }
      ]
    }
  });

  const releaseCard = secondaryBanner.children[0];
  assert.equal(releaseCard.tagName, 'BUTTON');
  assert.equal(releaseCard.dataset.releaseId, 'sub-live');
  assert.equal(releaseCard.dataset.linkUrl, 'https://speed.qq.com/sub-live.html');
  assert.equal(releaseCard.classList.contains('is-live-secondary'), true);
  assert.equal(releaseCard.children[0].src, 'https://cdn-launcher.qq.com/sub-live.png');
  assert.equal(releaseCard.children[1].textContent, '连胜之星');
});

test('missing secondary activity does not fabricate a strip from banner or timeline artwork', () => {
  const { secondaryBanner, documentRef } = createRenderFixture();
  renderLauncherContent(documentRef, {
    banners: [{
      id: 'main-only',
      title: '主广告',
      imageUrl: 'https://cdn-launcher.qq.com/main-only.png'
    }],
    timeline: {
      items: [{
        id: 'timeline-only',
        title: '版本节点',
        date: '2026-04-23',
        status: 'active',
        imageUrl: 'https://cdn-launcher.qq.com/timeline-only.png'
      }]
    }
  });

  assert.equal(secondaryBanner.classList.contains('hidden'), true);
  assert.equal(secondaryBanner.children.length, 0);
});

test('timeline progress follows the active item when the DTO does not supply a valid percentage', () => {
  assert.equal(computeTimelineProgress([
    { status: 'done' },
    { status: 'done' },
    { status: 'active' },
    { status: 'upcoming' },
    { status: 'upcoming' }
  ]), 50);
  assert.equal(computeTimelineProgress([{ status: 'active' }]), 100);
  assert.equal(computeTimelineProgress([]), 0);
});

test('timeline entries render an official-size hover and keyboard popover from live event data', () => {
  const { timeline, documentRef } = createRenderFixture();
  renderLauncherContent(documentRef, {
    timeline: {
      progress: 50,
      items: [{
        id: 'event-live',
        title: '连胜之星',
        date: '2026-04-23',
        status: 'active',
        description: '超越不息，新星加冕',
        imageUrl: 'https://cdn-launcher.qq.com/event-live.png',
        link: 'https://speed.qq.com/event-live.html'
      }]
    }
  });

  const item = timeline.children[0].children[0];
  assert.equal(item.className, 'timeline-item active has-popover');
  assert.equal(item.dataset.timelineId, 'event-live');
  assert.equal(item.attributes.get('tabindex'), '0');

  const popover = item.children[2];
  assert.equal(popover.className, 'timeline-popover');
  assert.equal(popover.dataset.linkUrl, 'https://speed.qq.com/event-live.html');
  assert.equal(popover.children[0].src, 'https://cdn-launcher.qq.com/event-live.png');
  assert.equal(popover.children[1].textContent, '超越不息，新星加冕');
});

test('server selector mirrors only real content servers into the hidden value owner and modal lists', () => {
  const {
    serverSelect,
    serverSelectLabel,
    serverRecentSection,
    serverRecentList,
    serverList,
    documentRef
  } = createRenderFixture();

  renderLauncherContent(documentRef, {
    servers: [{ id: '64', label: '电信区', available: true, selected: true }]
  });

  assert.equal(serverSelect.children[0].children.length, 1);
  assert.equal(serverSelect.children[0].children[0].value, '64');
  assert.equal(serverSelectLabel.textContent, '电信区');
  assert.equal(serverRecentSection.classList.contains('hidden'), false);
  assert.equal(serverRecentList.children[0].children.length, 1);
  assert.equal(serverList.children[0].children.length, 1);

  const option = serverList.children[0].children[0];
  assert.equal(option.className, 'server-option is-selected is-smooth');
  assert.equal(option.children[0].tagName, 'INPUT');
  assert.equal(option.children[0].value, '64');
  assert.equal(option.children[1].textContent, '电信区');
  assert.equal(option.children[2].textContent, '流畅');
});

test('default private server receives the same normalized smooth status as live server data', () => {
  const content = normalizeLauncherContent({});
  assert.equal(content.servers.length, 1);
  assert.equal(content.servers[0].id, '64');
  assert.equal(content.servers[0].status, 'smooth');
});

test('renderer markup exposes dynamic content and media controls without the old whole-page screenshot', async () => {
  const html = await readFile(new URL('src/renderer/index.html', projectRoot), 'utf8');
  const css = await readFile(new URL('src/renderer/styles.css', projectRoot), 'utf8');
  const app = await readFile(new URL('src/renderer/app.js', projectRoot), 'utf8');

  for (const id of [
    'launcherBackgroundVideo',
    'launcherMediaFallback',
    'launcherMediaStatus',
    'homeBannerTrack',
    'homeSecondaryBanner',
    'homeBulletinList',
    'bulletinTabs',
    'releaseTimeline',
    'mediaControls',
    'videoControlItem',
    'videoMuteBtn',
    'videoVolume',
    'homeMenuBtn',
    'scanLoginBtn',
    'quickLoginBtn',
    'loginAccountAvatar',
    'loginMethodFallback',
    'launcherMenu',
    'selectGameRootMenuBtn'
  ]) {
    assert.match(html, new RegExp(`id=["']${id}["']`), `missing #${id}`);
  }

  assert.match(html, /id="launcherBackgroundVideo"[^>]*autoplay[^>]*loop[^>]*playsinline[^>]*preload="auto"[^>]*src="\.\.\/\.\.\/assets\/launcher-background\.mp4"/s);
  assert.doesNotMatch(html, /id="(?:loginMediaImage|loginMediaVideo|homeHeroImage|homeHeroVideo|launcherMusic|musicControlItem|musicMuteBtn|musicVolume)"/);
  assert.doesNotMatch(html, /<video[^>]*\sposter=/);
  assert.doesNotMatch(html + css, /home-bg-official-qqspeed\.png/);
  assert.doesNotMatch(html, /daoju-modal|mallModal/);
  assert.match(html, /img-src[^";]*https:\/\/iips\.speed\.qq\.com/);
  assert.match(html + css, /window-menu/);
  assert.match(app, /\$\(['"]homeMenuBtn['"]\)\.addEventListener\(['"]click['"],\s*toggleLauncherMenu\)/);
  assert.match(app, /\$\(['"]selectGameRootMenuBtn['"]\)\.addEventListener\(['"]click['"],\s*selectGameRootFromMenu\)/);
  assert.doesNotMatch(app, /\$\(['"]homeMenuBtn['"]\)\.addEventListener\(['"]click['"],\s*selectGameRoot\)/);
  assert.equal((html.match(/role=["']menuitem["']/g) || []).length, 1);
  assert.doesNotMatch(html + app, /storageInfoMenuBtn|aboutLauncherMenuBtn|launcherDialog/);
  assert.doesNotMatch(html + app, /Hook|NativeBridge|Windows DPAPI|私服协议|utility process|renderer 不持有/i);
  assert.doesNotMatch(html + css + app, /wechatLogin|method-wechat|wechat-login-content/);
  assert.doesNotMatch(html, /data-home-action=/);
  assert.doesNotMatch(html, /id=["'](?:homePocketLink|mallOpenBtn)["']/);
  assert.doesNotMatch(html, /id=["'](?:loginLoadingBtn|selectGameRootLoginBtn|homeMinimizeBtn|homeCloseBtn)["']/);
  assert.doesNotMatch(html + css, /age-prompt/);
  assert.match(html, /script\s+type=["']module["']\s+src=["']\.\/app\.js["']/);
  assert.match(app, /dot\.dataset\.label\s*=\s*card\.dataset\.bannerTitle/);
  assert.match(css, /content:\s*attr\(data-label\)/);
  assert.equal((html.match(/class=["'][^"']*shared-top-bar[^"']*["']/g) || []).length, 1);
  assert.equal((html.match(/class=["'][^"']*bottom-control-box[^"']*["']/g) || []).length, 1);
  const loginPanel = css.match(/\.login-panel\s*\{([\s\S]*?)\}/)?.[1];
  assert.ok(loginPanel, 'missing base login panel rule');
  assert.match(loginPanel, /top:\s*0[\s\S]*right:\s*0[\s\S]*bottom:\s*0/);
  assert.match(loginPanel, /width:\s*clamp\(360px,\s*31vw,\s*470px\)/);
  assert.match(loginPanel, /padding:\s*clamp\(76px,\s*10vh,\s*104px\)/);
  assert.match(loginPanel, /background:\s*[\s\S]*linear-gradient\(116deg,[\s\S]*linear-gradient\(180deg,[\s\S]*rgba\(5,\s*12,\s*18,\s*0\.42\)/);
  assert.match(loginPanel, /backdrop-filter:\s*none/);
  assert.doesNotMatch(loginPanel, /#312b7466/);
  assert.match(css, /\.login-method-icon\s*\{[^}]*width:\s*clamp\([^}]*height:\s*clamp\(/s);
  assert.match(css, /\.login-secondary\s*\{[^}]*height:\s*clamp\(/s);
  assert.match(css, /\.media-control-item:(?:hover|focus-within)[^{]*\.media-volume-popover/s);
  assert.match(html, /背景音量/);
  assert.doesNotMatch(html, /视频音量|关闭视频|停用视频/);
  assert.match(css, /#videoMuteBtn\[data-muted="true"\]\s*\{[^}]*voice-off-default/s);
  assert.match(css, /#videoMuteBtn\[data-muted="false"\]\s*\{[^}]*voice-on-default/s);
  assert.doesNotMatch(css, /#videoMuteBtn[\s\S]{0,120}video-(?:on|off)-/);
  for (const id of [
    'serverSelectTrigger',
    'serverSelectLabel',
    'serverModal',
    'serverModalCloseBtn',
    'serverRecentSection',
    'serverRecentList',
    'serverList',
    'serverModalCancelBtn',
    'serverModalConfirmBtn'
  ]) {
    assert.match(html, new RegExp(`id=["']${id}["']`), `missing #${id}`);
  }
  assert.match(html, /class=["'][^"']*server-native-select[^"']*sr-only[^"']*["']/);
  assert.match(html, />\s*流畅\s*</);
  assert.match(html, />\s*繁忙\s*</);
  assert.match(html, />\s*爆满\s*</);
  assert.match(html, />\s*维护\s*</);
  assert.match(css, /\.home-banner-track\s*\{[^}]*display:\s*flex[^}]*transition:\s*transform/s);
  assert.match(css, /\.home-banner-card\s*\{[^}]*flex:\s*0\s+0\s+clamp\([^}]*height:\s*clamp\(/s);
  assert.match(css, /\.home-secondary-banner\s*\{[^}]*width:\s*clamp\([^}]*height:\s*clamp\([^}]*border:\s*1px\s+solid\s+#fff/s);
  assert.match(css, /\.timeline-item\s*>\s*span:not\(\.timeline-popover\)\s*\{[^}]*max-width:\s*114px/s);
  assert.doesNotMatch(css, /\.timeline-item\s+span\s*\{[^}]*max-width:\s*114px/s);
  assert.match(css, /\.timeline-popover\s*\{[^}]*width:\s*208px[^}]*height:\s*109px/s);
  assert.match(css, /\.server-select-trigger\s*\{[^}]*width:\s*clamp\([^}]*height:\s*clamp\(/s);
  assert.match(css, /\.server-modal-panel\s*\{[^}]*width:\s*min\(92vw,\s*890px\)[^}]*height:\s*min\(88vh,\s*674px\)/s);
  assert.match(css, /\.start-btn-desc\s*\{[^}]*display:\s*none/s);
  assert.doesNotMatch(html, /\b网通区\b|\b电信二区\b/);
  assert.match(app, /当前暂不支持扫码登录/);
  assert.match(app, /没有可用的已记住密码账号/);
  assert.match(html, /id=["']startBtnText["'][^>]*>登录后开始游戏</);
});

test('authenticated session renders a constrained QQ avatar with a local fallback', async () => {
  const html = await readFile(new URL('src/renderer/index.html', projectRoot), 'utf8');
  const css = await readFile(new URL('src/renderer/styles.css', projectRoot), 'utf8');
  const app = await readFile(new URL('src/renderer/app.js', projectRoot), 'utf8');
  const main = await readFile(new URL('src/main/index.js', projectRoot), 'utf8');

  assert.match(html, /id=["']accountAvatar["']/);
  assert.match(html, /id=["']accountAvatarFallback["']/);
  assert.match(html, /id=["']loginAccountAvatar["']/);
  assert.match(html, /id=["']loginMethodFallback["'][^>]*>K<\/span>/);
  assert.match(html, /referrerpolicy=["']no-referrer["']/);
  assert.match(html, /img-src[^"]*https:\/\/q3\.qlogo\.cn/);
  assert.match(css, /\.account-avatar\s*\{[^}]*object-fit:\s*cover/s);
  assert.match(main, /avatarUrl:\s*authenticated\s*\?\s*createQqAvatarUrl\(launcherAuthenticatedUser\)\s*:\s*''/);
  assert.match(app, /sanitizeAccountAvatarUrl\(value\)/);
  assert.match(app, /accountAvatar\.addEventListener\('error',\s*showAccountAvatarFallback\)/);
  assert.match(app, /setLoginAccountAvatar\(selectedUser\)/);
  assert.match(app, /username\.addEventListener\('blur',[\s\S]*?setLoginAccountAvatar\(username\.value\)/);
});

test('customer-facing launcher branding uses QQ飞车 and generic account labels', async () => {
  const paths = [
    'src/renderer/index.html',
    'src/renderer/update.html',
    'src/renderer/app.js',
    'src/renderer/content-view.js',
    'src/main/index.js',
    'src/main/native-core.js',
    'src/main/launcher-content-service.js',
    'src/main/tray-controller.js',
    'src/scripts/smoke-release.cjs'
  ];
  const source = (await Promise.all(
    paths.map((path) => readFile(new URL(path, projectRoot), 'utf8'))
  )).join('\n');
  const html = await readFile(new URL('src/renderer/index.html', projectRoot), 'utf8');
  const css = await readFile(new URL('src/renderer/styles.css', projectRoot), 'utf8');

  assert.doesNotMatch(source, /QQ号码|QQ密码|QQ 登录|请输入QQ账号/);
  assert.match(html, /<title>QQ飞车登录器<\/title>/);
  assert.match(html, /class="login-row-label">号码<\/span>/);
  assert.match(html, /class="login-row-label">密码<\/span>/);
  assert.match(html, /class="top-bar-logo"[^>]*>QQ飞车<\/span>/);
  assert.match(html, /class="login-method-icon method-qqspeed"[\s\S]*?id="loginMethodFallback"[^>]*>飞<\/span>/);
  assert.doesNotMatch(html + css, /app-logo-CZrPeQSY|login-icon-qq-active|method-qq/);
});
