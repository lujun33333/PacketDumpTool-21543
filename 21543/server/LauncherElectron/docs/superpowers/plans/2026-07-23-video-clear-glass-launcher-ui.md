# 1.0.4 动态视频清透玻璃启动器 UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Electron 启动器升级到 1.0.4，以一个带原声的共享本地视频作为登录页与主页唯一背景，实现 I 方案登录页、B 方案主页和 A 增强 GM 窗口，并生成安装包、便携包及签名 `update.json`。

**Architecture:** 背景媒体从动态内容 DTO 中解耦，由 HTML 中唯一的 `#launcherBackgroundVideo` 和精简后的 `MediaController` 管理，页面切换不再替换或重载媒体。登录页、主页和 GM 沿用现有 DOM 行为与 IPC 边界，只重排结构、增加活动浮层与二级分类文字导航；GM 数据库、查询服务、关闭确认和托盘链路不改。

**Tech Stack:** Electron 35、原生 JavaScript ES modules、HTML/CSS、Node.js 20 `node:test`、electron-builder、PowerShell、现有 SQLite GM 数据库与 IPC。

## Global Constraints

- 目标版本必须是 `1.0.4`。
- 登录页和主页只能使用 `assets/launcher-background.mp4`，不得显示旧背景 `<img>` 或视频 `poster`。
- 视频原声默认开启，默认音量为 `70%`；只保留视频声音控制，不加载独立音乐。
- Chromium 拒绝首次有声自动播放时，只等待首次鼠标或键盘交互重试，不得持久化为静音。
- 登录页、主页和 GM 大面积玻璃容器必须使用 `backdrop-filter: none`，保持视频背景清晰。
- 主色限定为中性透明层、冰青色和白色；登录、开始游戏和 GM 查询主按钮不得继续使用金色资源皮肤。
- 未登录时隐藏 GM 入口；登录后显示；登出时关闭 GM 并清理状态。
- GM 一级分类只显示文字；二级分类跟随一级分类；卡片显示图片、名称、ID、二级分类和复制 ID。
- GM 始终保持三行：标准 `10×3=30`、中等 `8×3=24`、小窗口 `6×3=18`。
- 不修改 GM SQLite 结构、GM IPC、游戏启动参数、服务端源码、关闭确认语义或托盘语义。
- 不增加第三方 UI、音乐或媒体依赖。
- 不覆盖或删除现有 `dist` 中的 1.0.3 产物。
- 不提交 `.superpowers/` 可视化临时目录，也不暂存 `F:\PacketDumpTool\21487\server\Server` 下的任何文件。

---

## File Map

### Renderer media boundary

- Modify: `src/renderer/index.html`
  - 建立唯一共享视频节点；删除旧图片、两套页面视频和独立音频节点。
- Modify: `src/renderer/media-controller.js`
  - 只管理一个本地视频及其音量、静音、可见性和自动播放重试。
- Modify: `src/renderer/content-view.js`
  - 内容 DTO 只向主页输出文字、活动、公告、时间线、区服与链接；不再输出可渲染背景或音乐。
- Modify: `src/renderer/app.js`
  - 停止把动态内容交给媒体控制器；适配新主页、活动浮层和 GM 入口。
- Test: `test/unit/renderer-content.test.js`
  - 覆盖单视频、无海报、无音乐、默认原声、偏好保存、手势重试与错误降级。
- Modify: `src/scripts/smoke-renderer-flow.cjs`
  - 静态烟雾测试改查共享视频和无音乐契约。

### Login and home presentation

- Modify: `src/renderer/index.html`
  - 实现 I 登录面板和 B 主页结构。
- Modify: `src/renderer/styles.css`
  - 实现清透晶体材质、悬浮顶栏、公告条、活动浮层、底部控制台及断点。
- Modify: `src/renderer/content-view.js`
  - 把首条公告同步到轻量公告条。
- Modify: `src/renderer/app.js`
  - 控制活动浮层、账号摘要和页面切换清理。
- Test: `test/unit/responsive-launcher-layout.test.js`
  - 覆盖无模糊玻璃、登录面板比例、主页单行/双行重排和三个视口档位。
- Test: `test/unit/renderer-content.test.js`
  - 覆盖新 DOM 和公告条渲染。

### GM presentation

- Modify: `src/renderer/index.html`
  - GM 入口移到主页控制台；增加纯文字二级分类导航。
- Modify: `src/renderer/app.js`
  - 同步隐藏的二级分类值、文字按钮、卡片元数据和焦点恢复。
- Modify: `src/renderer/styles.css`
  - 实现 A 增强晶体玻璃、纯文字左栏、二级分类条与 10/8/6 列三行网格。
- Test: `test/unit/gm-item-browser-integration.test.js`
  - 覆盖入口权限、两级分类、卡片字段和强玻璃材质。
- Test: `test/unit/gm-item-browser.test.js`
  - 保留现有 30/24/18 页容量断言。

### Packaging and release

- Modify: `package.json`
  - 版本升级到 1.0.4，保留 MP4 解包和 GM SQLite 额外资源规则。
- Modify: `package-lock.json`
  - 同步根包版本。
- Modify: `test/unit/embedded-native-packaging.test.js`
  - 断言 MP4、GM SQLite 被打包且 renderer 不包含独立音乐节点。
- Modify: `src/scripts/smoke-release.cjs`
  - 在真实打包应用中检查共享视频、70% 音量和默认非静音。
- Generated, do not commit unless the repository already tracks release artifacts:
  - `dist/Kiss-Setup-1.0.4-x64.exe`
  - `dist/Kiss-Portable-1.0.4-x64.exe`
  - `dist/update-1.0.4/Kiss-Setup-1.0.4-x64.exe`
  - `dist/update-1.0.4/update.json`

### Explicitly unchanged

- `src/main/gm-item-query-service.js`
- `src/shared/gm-item-catalog.js`
- `src/shared/gm-item-database.js`
- `resources/gm-items.sqlite3`
- `src/main/tray-controller.js`
- `src/renderer/close-preference.js`
- `F:\PacketDumpTool\21487\server\Server\**`

---

### Task 1: Replace the dual-media pipeline with one shared local video

**Files:**
- Modify: `test/unit/renderer-content.test.js`
- Modify: `src/renderer/index.html`
- Modify: `src/renderer/media-controller.js`
- Modify: `src/renderer/content-view.js`
- Modify: `src/renderer/app.js`
- Modify: `src/scripts/smoke-renderer-flow.cjs`

**Interfaces:**
- Consumes: static local asset path `../../assets/launcher-background.mp4`; existing localStorage key `qqspeed.launcher.media.v2`.
- Produces: `MediaController(documentRef, storage)`, `normalizeMediaPreferences(value) -> { videoMuted: boolean, videoVolume: number }`, `MediaController.playBackgroundVideo() -> Promise<boolean>`, and one DOM media target `#launcherBackgroundVideo`.
- Preserves: `sanitizeMediaSource()` for banners, activity art, timeline art and trusted links.

- [ ] **Step 1: Replace the media fixture and write failing single-video tests**

In `test/unit/renderer-content.test.js`, keep the existing content/server tests, replace `createMediaFixture` and the media-specific tests with this contract:

```js
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
```

Also extend the markup test with:

```js
assert.match(html, /id="launcherBackgroundVideo"[^>]*autoplay[^>]*loop[^>]*playsinline[^>]*preload="auto"[^>]*src="\.\.\/\.\.\/assets\/launcher-background\.mp4"/s);
assert.doesNotMatch(html, /id="(?:loginMediaImage|loginMediaVideo|homeHeroImage|homeHeroVideo|launcherMusic|musicControlItem|musicMuteBtn|musicVolume)"/);
assert.doesNotMatch(html, /<video[^>]*\sposter=/);
```

In the existing remote DTO test, remove the obsolete assertions for
`content.loginVisual` and `content.music`, and assert that the renderer
boundary drops both fields:

```js
assert.equal(content.hero.title, 'Kiss');
assert.equal(content.hero.subtitle, '超越不息');
assert.equal('loginVisual' in content, false);
assert.equal('music' in content, false);
assert.equal(content.banners.length, 2);
assert.equal(content.banners[0].imageUrl, '');
assert.equal(content.banners[1].imageUrl, 'https://game.gtimg.cn/images/speed/banner.jpg');
assert.equal(content.secondaryActivities[0].id, 'sub-activity');
assert.equal(content.servers[0].selected, true);
```

Delete the obsolete tests named:

```text
media chrome only exposes controls for sources available in the active view
1.0.3 does not inherit the legacy v1 default-mute record
packaged video is the renderer fallback for both responsive views
a persisted unmuted music preference resumes when dynamic content supplies a track
autoplay policy rejection retries the active video muted without visual fallback
play rejection does not hide video; the media error event does
```

Their retained requirements are covered by the new single-video tests above.

- [ ] **Step 2: Run the media tests and confirm the old pipeline fails**

Run:

```powershell
node --test test/unit/renderer-content.test.js
```

Expected: FAIL because `DEFAULT_MEDIA_PREFERENCES` still contains music fields, `#launcherBackgroundVideo` does not exist, and `MediaController.playBackgroundVideo` is not defined.

- [ ] **Step 3: Replace the old media DOM with a shared video**

At the start of `#app` in `src/renderer/index.html`, insert:

```html
<div class="launcher-background" aria-hidden="true">
  <video
    id="launcherBackgroundVideo"
    class="launcher-background-video"
    autoplay
    loop
    playsinline
    preload="auto"
    src="../../assets/launcher-background.mp4"
  ></video>
  <div id="launcherMediaFallback" class="launcher-media-fallback hidden"></div>
</div>
<p id="launcherMediaStatus" class="launcher-media-status hidden" role="status">
  背景视频暂不可用，登录和启动功能仍可继续使用。
</p>
```

Delete `.login-media`, `.home-hero-media`, their two `<img>` elements, their two `<video>` elements, `#musicControlItem`, and `#launcherMusic`. Keep only:

```html
<aside id="mediaControls" class="media-controls" aria-label="背景声音设置">
  <div id="videoControlItem" class="media-control-item">
    <button
      id="videoMuteBtn"
      class="media-toggle"
      type="button"
      aria-pressed="false"
      aria-label="关闭背景声音"
    ></button>
    <label class="media-volume-popover" title="背景音量">
      <span>背景音量</span>
      <input id="videoVolume" type="range" min="0" max="100" step="1" value="70" />
    </label>
  </div>
</aside>
```

- [ ] **Step 4: Replace `media-controller.js` with the one-video controller**

Use this complete module:

```js
const STORAGE_KEY = 'qqspeed.launcher.media.v2';

export const DEFAULT_MEDIA_PREFERENCES = Object.freeze({
  videoMuted: false,
  videoVolume: 0.7
});

function clampVolume(value, fallback) {
  const number = Number(value);
  return Number.isFinite(number) ? Math.max(0, Math.min(1, number)) : fallback;
}

export function normalizeMediaPreferences(value) {
  const source = value && typeof value === 'object' ? value : {};
  return {
    videoMuted: typeof source.videoMuted === 'boolean'
      ? source.videoMuted
      : DEFAULT_MEDIA_PREFERENCES.videoMuted,
    videoVolume: clampVolume(source.videoVolume, DEFAULT_MEDIA_PREFERENCES.videoVolume)
  };
}

function loadPreferences(storage) {
  try {
    return normalizeMediaPreferences(JSON.parse(storage?.getItem(STORAGE_KEY) || 'null'));
  } catch {
    return { ...DEFAULT_MEDIA_PREFERENCES };
  }
}

function setHidden(element, hidden) {
  element?.classList.toggle('hidden', hidden);
}

export class MediaController {
  constructor(documentRef, storage = globalThis.localStorage) {
    this.document = documentRef;
    this.storage = storage;
    this.preferences = loadPreferences(storage);
    this.video = documentRef.getElementById('launcherBackgroundVideo');
    this.shell = documentRef.getElementById('app');
    this.fallback = documentRef.getElementById('launcherMediaFallback');
    this.status = documentRef.getElementById('launcherMediaStatus');
    this.controls = documentRef.getElementById('mediaControls');
    this.videoControlItem = documentRef.getElementById('videoControlItem');
    this.videoMuteButton = documentRef.getElementById('videoMuteBtn');
    this.videoVolume = documentRef.getElementById('videoVolume');
    this.pendingGestureRetry = false;
    this.view = 'login';
    this.bind();
    this.applyPreferences();
  }

  bind() {
    this.video?.addEventListener('loadeddata', () => {
      this.shell?.classList.remove('media-failed');
      setHidden(this.fallback, true);
      setHidden(this.status, true);
      this.updateControls();
    }, { passive: true });
    this.video?.addEventListener('error', () => {
      this.pendingGestureRetry = false;
      this.shell?.classList.add('media-failed');
      setHidden(this.fallback, false);
      setHidden(this.status, false);
      this.updateControls();
    }, { passive: true });
    this.videoMuteButton?.addEventListener('click', () => {
      this.preferences.videoMuted = !this.preferences.videoMuted;
      this.applyPreferences();
      if (!this.preferences.videoMuted) void this.playBackgroundVideo();
    });
    this.videoVolume?.addEventListener('input', () => {
      this.preferences.videoVolume = clampVolume(
        Number(this.videoVolume.value) / 100,
        this.preferences.videoVolume
      );
      this.applyPreferences();
    });
    this.document.addEventListener?.('visibilitychange', () => this.handleVisibilityChange());
    this.document.addEventListener?.('pointerdown', () => this.retryPendingPlayback(), true);
    this.document.addEventListener?.('keydown', () => this.retryPendingPlayback(), true);
  }

  persist() {
    try {
      this.storage?.setItem(STORAGE_KEY, JSON.stringify(this.preferences));
    } catch {
      // Media preferences must never block login or launch.
    }
  }

  applyPreferences() {
    if (this.video) {
      this.video.muted = this.preferences.videoMuted;
      this.video.volume = this.preferences.videoVolume;
    }
    if (this.videoVolume) {
      this.videoVolume.value = String(Math.round(this.preferences.videoVolume * 100));
    }
    this.persist();
    this.updateControls();
  }

  updateControls() {
    const unavailable = !this.video?.src || this.shell?.classList.contains('media-failed');
    setHidden(this.controls, !this.video?.src);
    setHidden(this.videoControlItem, !this.video?.src);
    if (this.videoMuteButton) {
      this.videoMuteButton.disabled = unavailable;
      this.videoMuteButton.dataset.muted = String(this.preferences.videoMuted);
      this.videoMuteButton.setAttribute('aria-pressed', String(this.preferences.videoMuted));
      this.videoMuteButton.setAttribute(
        'aria-label',
        this.preferences.videoMuted ? '打开背景声音' : '关闭背景声音'
      );
      this.videoMuteButton.title = unavailable
        ? '背景视频暂不可用'
        : this.preferences.videoMuted ? '打开背景声音' : '关闭背景声音';
    }
    if (this.videoVolume) this.videoVolume.disabled = unavailable;
  }

  async playBackgroundVideo() {
    if (
      this.document.hidden ||
      !this.video?.src ||
      this.shell?.classList.contains('media-failed')
    ) {
      return false;
    }
    try {
      await this.video.play();
      this.pendingGestureRetry = false;
      return true;
    } catch (error) {
      if (error?.name === 'NotAllowedError') {
        this.pendingGestureRetry = true;
      }
      return false;
    }
  }

  retryPendingPlayback() {
    if (!this.pendingGestureRetry || this.document.hidden) return;
    this.pendingGestureRetry = false;
    void this.playBackgroundVideo();
  }

  pauseAllMedia() {
    this.video?.pause();
  }

  handleVisibilityChange() {
    if (this.document.hidden) {
      this.pauseAllMedia();
      return;
    }
    void this.playBackgroundVideo();
  }

  setView(view) {
    this.view = view === 'home' ? 'home' : 'login';
    this.updateControls();
  }
}
```

- [ ] **Step 5: Remove background and music data from the renderer content boundary**

In `src/renderer/content-view.js`, replace the default object with:

```js
const CUSTOMER_BRAND = 'Kiss';

export const DEFAULT_LAUNCHER_CONTENT = Object.freeze({
  hero: Object.freeze({
    title: CUSTOMER_BRAND,
    subtitle: '内容服务不可用，已使用本地内容'
  }),
  banners: Object.freeze([]),
  secondaryActivities: Object.freeze([]),
  bulletins: Object.freeze([]),
  timeline: Object.freeze({ items: Object.freeze([]), progress: 0 }),
  servers: Object.freeze([
    Object.freeze({ id: '64', label: '电信区', available: true, selected: true, status: 'smooth' })
  ]),
  links: Object.freeze([])
});
```

Delete `DEFAULT_LOGIN_IMAGE`, `LOCAL_BACKGROUND_VIDEO`, `normalizeVisual`, and `normalizePackagedBackground`. Replace the beginning of `normalizeLauncherContent` with:

```js
export function normalizeLauncherContent(value) {
  const source = value && typeof value === 'object' ? value : {};
  const heroSource = source.hero && typeof source.hero === 'object'
    ? source.hero
    : source.homeVisual && typeof source.homeVisual === 'object'
      ? source.homeVisual
      : {};
  return {
    hero: {
      title: CUSTOMER_BRAND,
      subtitle: text(
        heroSource.subtitle,
        DEFAULT_LAUNCHER_CONTENT.hero.subtitle,
        120,
        { preserveEmpty: true }
      )
    },
    banners: normalizeBanners(source.banners),
    secondaryActivities: normalizeSecondaryActivities(source.secondaryActivities),
    bulletins: normalizeBulletins(source.bulletins || source.notices),
    timeline: normalizeTimeline(source.timeline),
    servers: normalizeServers(source.servers),
    links: normalizeLinks(source.links)
  };
}
```

Do not remove `sanitizeMediaSource`; it remains the allow-list for activity art and navigation.

- [ ] **Step 6: Stop dynamic content from reconfiguring background media**

In `src/renderer/app.js`, delete:

```js
mediaController.applyContent(currentContent);
```

Keep `mediaController.setView(view)` because it updates control state without pausing or reloading the shared video. After `applyLauncherContent({});`, start the shared media once:

```js
void mediaController.playBackgroundVideo();
```

- [ ] **Step 7: Update the static renderer smoke contract**

In `src/scripts/smoke-renderer-flow.cjs`, replace the old video/music assertion with:

```js
assert(
  renderer.includes('MediaController')
    && rendererHtml.includes('launcherBackgroundVideo')
    && rendererHtml.includes('../../assets/launcher-background.mp4'),
  'The shared packaged background video is missing.'
);
assert(
  !/(?:loginMediaImage|homeHeroImage|launcherMusic|musicControlItem)/.test(rendererHtml),
  'Legacy image or independent music media remains in the renderer.'
);
assert(
  !/<video[^>]*\sposter=/s.test(rendererHtml),
  'The shared background video must not use a poster image.'
);
```

- [ ] **Step 8: Run the media and renderer smoke tests**

Run:

```powershell
node --test test/unit/renderer-content.test.js
npm run check:renderer-flow
```

Expected: both commands PASS; no assertion refers to the removed media IDs.

- [ ] **Step 9: Commit the media boundary**

```powershell
git diff -- src/renderer/index.html src/renderer/media-controller.js src/renderer/content-view.js src/renderer/app.js src/scripts/smoke-renderer-flow.cjs test/unit/renderer-content.test.js
git add -- src/renderer/index.html src/renderer/media-controller.js src/renderer/content-view.js src/renderer/app.js src/scripts/smoke-renderer-flow.cjs test/unit/renderer-content.test.js
git commit -m "feat: use one audible launcher background video"
```

---

### Task 2: Implement the I-scheme clear-glass login page

**Files:**
- Modify: `test/unit/responsive-launcher-layout.test.js`
- Modify: `src/renderer/index.html`
- Modify: `src/renderer/styles.css`

**Interfaces:**
- Consumes: `#launcherBackgroundVideo`, existing login form IDs and shared top-bar controls.
- Produces: `.login-panel.crystal-surface`, responsive width `clamp(360px, 31vw, 470px)`, and no login footer.
- Preserves: username/password, remembered accounts, auto login, QR login, quick login, agreement and window buttons.

- [ ] **Step 1: Write failing material and layout assertions**

Add this helper and test to `test/unit/responsive-launcher-layout.test.js`:

```js
function finalCssRule(css, selector) {
  const marker = `${selector} {`;
  const start = css.lastIndexOf(marker);
  assert.notEqual(start, -1, `missing ${selector}`);
  const bodyStart = start + marker.length;
  return css.slice(bodyStart, css.indexOf('}', bodyStart));
}

test('login uses a responsive clear crystal panel without a persistent footer', () => {
  const html = read('src/renderer/index.html');
  const css = read('src/renderer/styles.css');
  assert.match(html, /class="login-panel crystal-surface"/);
  assert.doesNotMatch(html, /class="login-media"/);
  assert.match(finalCssRule(css, '.login-panel'), /width:\s*clamp\(360px,\s*31vw,\s*470px\)/);
  assert.match(finalCssRule(css, '.login-panel'), /backdrop-filter:\s*none/);
  assert.match(
    finalCssRule(css, '.shell[data-view="login"] .bottom-control-box'),
    /display:\s*none/
  );
  assert.match(css, /@media\s*\(max-width:\s*1200px\)[\s\S]*?\.login-panel\s*\{[^}]*width:\s*clamp\(360px,\s*40vw,\s*460px\)/s);
  assert.match(css, /@media\s*\(max-width:\s*1080px\)[\s\S]*?\.login-panel\s*\{[^}]*width:\s*clamp\(350px,\s*42vw,\s*440px\)/s);
});
```

Replace the old agreement assertion with:

```js
assert.match(css, /\.agreement\s*\{[^}]*margin-top:\s*clamp\(/s);
```

- [ ] **Step 2: Run the responsive test and confirm it fails**

Run:

```powershell
node --test test/unit/responsive-launcher-layout.test.js
```

Expected: FAIL because the panel still uses `32vw`, uses `blur(20px)`, and the login footer is still present.

- [ ] **Step 3: Mark the login panel as the primary crystal surface**

Change:

```html
<aside class="login-panel">
```

to:

```html
<aside class="login-panel crystal-surface">
```

Keep all existing controls and IDs inside the panel unchanged.

- [ ] **Step 4: Replace the root media and login surface CSS**

Replace the old root colors, `.login-media` rules and `.login-panel` material with:

```css
:root {
  color-scheme: dark;
  font-family: "Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI", sans-serif;
  --ice: #8ffcf4;
  --ice-strong: #32e8dc;
  --text: #f8ffff;
  --muted: rgba(235, 252, 255, 0.7);
  --crystal-edge: rgba(226, 255, 255, 0.48);
  --crystal-edge-soft: rgba(205, 247, 255, 0.16);
  --crystal-dark: rgba(5, 12, 18, 0.34);
  --top-bar-height: clamp(56px, 7.5vh, 68px);
  --bottom-control-height: clamp(108px, 15vh, 136px);
}

html,
body {
  width: 100%;
  height: 100%;
  margin: 0;
  overflow: hidden;
  background: #071016;
  user-select: none;
}

.shell {
  position: relative;
  width: 100vw;
  height: 100vh;
  overflow: hidden;
  color: var(--text);
  background: #071016;
}

.launcher-background {
  position: absolute;
  z-index: 0;
  inset: 0;
  overflow: hidden;
  background:
    radial-gradient(circle at 70% 20%, rgba(63, 116, 124, 0.18), transparent 46%),
    #071016;
  pointer-events: none;
}

.launcher-background-video {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  object-fit: cover;
  object-position: center;
}

.media-failed .launcher-background-video {
  opacity: 0;
}

.launcher-media-fallback {
  position: absolute;
  inset: 0;
  background:
    radial-gradient(circle at 52% 32%, rgba(82, 148, 152, 0.2), transparent 35%),
    linear-gradient(145deg, #071016, #111d24);
}

.launcher-media-status {
  position: absolute;
  z-index: 140;
  left: 50%;
  bottom: 24px;
  margin: 0;
  padding: 9px 14px;
  border: 1px solid rgba(207, 252, 255, 0.18);
  border-radius: 999px;
  color: rgba(240, 255, 255, 0.76);
  background: rgba(3, 10, 14, 0.72);
  font-size: 12px;
  transform: translateX(-50%);
}

.login-view,
.home-view {
  position: absolute;
  z-index: 1;
  inset: 0;
  overflow: hidden;
  background: transparent;
}

.login-stage {
  position: absolute;
  inset: 0;
}

.login-panel {
  position: absolute;
  top: 0;
  right: 0;
  bottom: 0;
  width: clamp(360px, 31vw, 470px);
  display: flex;
  flex-direction: column;
  justify-content: center;
  padding: clamp(76px, 10vh, 104px) clamp(30px, 3vw, 48px) clamp(26px, 4vh, 42px);
  overflow: hidden;
  border: 1px solid var(--crystal-edge-soft);
  border-right: 0;
  background:
    linear-gradient(116deg, rgba(255, 255, 255, 0.1), transparent 23%),
    linear-gradient(180deg, rgba(7, 16, 22, 0.22), rgba(5, 12, 18, 0.42));
  box-shadow:
    inset 1px 0 rgba(236, 255, 255, 0.34),
    inset 18px 0 48px rgba(207, 255, 255, 0.035),
    -24px 0 70px rgba(0, 0, 0, 0.16);
  backdrop-filter: none;
}

.login-panel::before,
.login-panel::after {
  content: "";
  position: absolute;
  pointer-events: none;
}

.login-panel::before {
  inset: 0;
  background:
    linear-gradient(112deg, transparent 7%, rgba(255, 255, 255, 0.16) 8%, transparent 12%),
    repeating-linear-gradient(120deg, rgba(255, 255, 255, 0.018) 0 1px, transparent 1px 8px);
  opacity: 0.64;
}

.login-panel::after {
  top: 0;
  left: 0;
  width: 2px;
  height: 100%;
  background: linear-gradient(180deg, transparent, rgba(157, 255, 249, 0.9), transparent);
  box-shadow: 0 0 24px rgba(50, 232, 220, 0.34);
}

.login-panel > * {
  position: relative;
  z-index: 1;
}

.shell[data-view="login"] .bottom-control-box {
  display: none;
}
```

- [ ] **Step 5: Replace gold login controls with ice-crystal controls**

Use CSS gradients rather than old button background images:

```css
.login-input-shell {
  border: 1px solid rgba(221, 255, 255, 0.26);
  border-radius: 8px;
  background:
    linear-gradient(180deg, rgba(255, 255, 255, 0.1), transparent 34%),
    rgba(3, 11, 15, 0.36);
  box-shadow:
    inset 0 1px rgba(255, 255, 255, 0.16),
    inset 0 -12px 24px rgba(0, 0, 0, 0.08);
}

.login-input-shell:focus-within {
  border-color: rgba(143, 252, 244, 0.76);
  box-shadow:
    inset 0 1px rgba(255, 255, 255, 0.2),
    0 0 0 3px rgba(50, 232, 220, 0.1),
    0 0 24px rgba(50, 232, 220, 0.12);
}

.login-primary {
  border: 1px solid rgba(210, 255, 252, 0.72);
  border-radius: 8px;
  color: #071a1a;
  background:
    linear-gradient(180deg, rgba(255, 255, 255, 0.72), transparent 38%),
    linear-gradient(100deg, #b8fff9, #43eadf 52%, #a7fff8);
  box-shadow:
    inset 0 1px #fff,
    0 10px 28px rgba(31, 226, 212, 0.2);
}

.login-primary:hover:not(:disabled) {
  filter: brightness(1.08);
  box-shadow:
    inset 0 1px #fff,
    0 0 0 3px rgba(143, 252, 244, 0.12),
    0 14px 34px rgba(31, 226, 212, 0.3);
}

.login-secondary {
  border: 1px solid rgba(219, 255, 255, 0.22);
  border-radius: 8px;
  color: rgba(245, 255, 255, 0.88);
  background:
    linear-gradient(180deg, rgba(255, 255, 255, 0.08), transparent 38%),
    rgba(4, 13, 18, 0.3);
}

.agreement {
  position: static;
  margin-top: clamp(18px, 3vh, 30px);
}
```

- [ ] **Step 6: Add width and height breakpoints**

At the end of `styles.css`, make these the final login rules:

```css
@media (max-width: 1200px) {
  .login-panel {
    width: clamp(360px, 40vw, 460px);
    padding-inline: clamp(26px, 3vw, 38px);
  }
}

@media (max-width: 1080px) {
  .login-panel {
    width: clamp(350px, 42vw, 440px);
  }
}

@media (max-height: 760px) {
  .login-panel {
    justify-content: flex-start;
    padding-top: 62px;
    padding-bottom: 18px;
  }

  .login-method-icon {
    width: 60px;
    height: 60px;
  }

  .login-row,
  .login-input-shell {
    height: 40px;
  }

  .login-primary {
    height: 46px;
  }

  .login-secondary {
    height: 40px;
  }

  .agreement {
    margin-top: 12px;
    font-size: 12px;
    line-height: 1.35;
  }
}
```

- [ ] **Step 7: Run the login/responsive test**

Run:

```powershell
node --test test/unit/responsive-launcher-layout.test.js test/unit/official-launcher-app-integration.test.js
```

Expected: PASS; form IDs and application integration remain intact.

- [ ] **Step 8: Commit the login view**

```powershell
git diff -- src/renderer/index.html src/renderer/styles.css test/unit/responsive-launcher-layout.test.js
git add -- src/renderer/index.html src/renderer/styles.css test/unit/responsive-launcher-layout.test.js
git commit -m "feat: add responsive clear-glass login view"
```

---

### Task 3: Implement the B-scheme immersive home console and activity popover

**Files:**
- Modify: `test/unit/renderer-content.test.js`
- Modify: `test/unit/responsive-launcher-layout.test.js`
- Modify: `src/renderer/index.html`
- Modify: `src/renderer/content-view.js`
- Modify: `src/renderer/app.js`
- Modify: `src/renderer/styles.css`

**Interfaces:**
- Consumes: existing `renderLauncherContent`, banner, bulletin, timeline, server, multi-open and start-game handlers.
- Produces: `#homeAnnouncementTicker`, `#homeTickerText`, `#homeActivityBtn`, `#homeActivityPopover`, `#homeActivityCloseBtn`, `#homeDockAccountName`, and `#gmItemBrowserBtn`.
- Preserves: remote activity image allow-list and all existing content click handlers.

- [ ] **Step 1: Write failing home DOM, ticker and popover assertions**

Add to `test/unit/renderer-content.test.js`:

```js
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
```

Extend `createRenderFixture()` with:

```js
const homeTickerText = new FakeElement('strong');
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
```

Then add:

```js
test('the lightweight ticker mirrors the first normalized bulletin', () => {
  const { documentRef } = createRenderFixture();
  renderLauncherContent(documentRef, {
    bulletins: [
      { id: 'notice-1', category: '公告', title: '7月版本更新已开放', date: '07/23' }
    ]
  });
  assert.equal(documentRef.getElementById('homeTickerText').textContent, '7月版本更新已开放');
});
```

Add to `test/unit/responsive-launcher-layout.test.js`:

```js
test('home uses floating clear surfaces and a responsive two-row dock', () => {
  const css = read('src/renderer/styles.css');
  assert.match(finalCssRule(css, '.shell[data-view="home"] .top-bar'), /backdrop-filter:\s*none/);
  assert.match(finalCssRule(css, '.shell[data-view="home"] .bottom-control-box'), /backdrop-filter:\s*none/);
  assert.match(css, /\.home-activity-popover\s*\{[^}]*width:\s*min\(92vw,\s*760px\)/s);
  assert.match(css, /@media\s*\(max-width:\s*1200px\)[\s\S]*?\.bottom-control-box\s*\{[^}]*grid-template-columns:\s*1fr/s);
  assert.match(css, /\.home-announcement-ticker\s*\{[^}]*bottom:\s*calc\(var\(--bottom-control-height\)/s);
});
```

- [ ] **Step 2: Run the renderer tests and confirm the new home contract fails**

Run:

```powershell
node --test test/unit/renderer-content.test.js test/unit/responsive-launcher-layout.test.js
```

Expected: FAIL because the new dock, ticker and activity popover IDs do not exist.

- [ ] **Step 3: Restructure the home view**

Replace the current always-visible `.home-content-deck` and bottom timeline with:

```html
<section id="homeView" class="home-view hidden">
  <main class="home-stage">
    <section class="home-live-copy">
      <h1 id="homeHeroTitle">Kiss</h1>
      <p id="homeHeroSubtitle">正在同步活动与版本内容</p>
    </section>

    <button
      id="homeAnnouncementTicker"
      class="home-announcement-ticker"
      type="button"
      aria-controls="homeActivityPopover"
      aria-expanded="false"
    >
      <span>公告</span>
      <strong id="homeTickerText">活动与公告加载中…</strong>
      <small>查看详情</small>
    </button>

    <section
      id="homeActivityPopover"
      class="home-activity-popover crystal-surface hidden"
      aria-label="活动与公告"
    >
      <header>
        <div>
          <strong>活动中心</strong>
          <span>活动、公告与版本进度</span>
        </div>
        <button id="homeActivityCloseBtn" type="button" aria-label="关闭活动中心">×</button>
      </header>
      <div class="home-content-deck">
        <div class="home-banner-shell">
          <div id="homeBannerTrack" class="home-banner-track" aria-live="polite">
            <div class="home-content-empty">活动内容加载中...</div>
          </div>
          <div id="homeBannerDots" class="home-banner-dots" aria-label="切换活动"></div>
        </div>
        <div id="homeSecondaryBanner" class="home-secondary-banner" aria-label="版本更新">
          <div class="home-content-empty">版本内容加载中...</div>
        </div>
        <div class="home-bulletins">
          <div id="bulletinTabs" class="bulletin-tabs" aria-label="资讯分类">
            <button class="active" type="button" data-bulletin-category="公告" aria-pressed="true">公告</button>
            <button type="button" data-bulletin-category="赛事" aria-pressed="false">赛事</button>
          </div>
          <ul id="homeBulletinList">
            <li class="home-content-empty">公告加载中...</li>
          </ul>
        </div>
        <div class="home-activity-timeline">
          <div id="releaseTimeline" class="release-timeline" aria-label="版本进度">
            <div class="timeline-item active"><i></i><span>版本内容加载中...</span></div>
          </div>
          <div class="timeline-progress"><span id="timelineProgressValue"></span></div>
        </div>
      </div>
    </section>
  </main>
</section>
```

Replace the footer contents with:

```html
<footer class="bottom-control-box">
  <section class="home-dock-summary home-only" aria-label="客户端摘要">
    <div>
      <span>当前账号</span>
      <strong id="homeDockAccountName">未登录</strong>
    </div>
    <div>
      <span>客户端状态</span>
      <strong id="homeStatusText" role="status" aria-live="polite">账号已就绪，可以开始游戏。</strong>
    </div>
    <div class="home-dock-path">
      <span>游戏目录</span>
      <strong id="gameRootText">未定位 Kiss主目录</strong>
    </div>
  </section>

  <nav class="home-dock-actions home-only" aria-label="游戏操作">
    <button id="gmItemBrowserBtn" class="home-dock-button hidden" type="button">GM 道具查询</button>
    <button
      id="homeActivityBtn"
      class="home-dock-button"
      type="button"
      aria-controls="homeActivityPopover"
      aria-expanded="false"
    >活动</button>
    <button id="multiOpenBtn" class="home-dock-button multi-open-btn" type="button"
      aria-haspopup="dialog" aria-controls="multiInstanceModal" aria-expanded="false">
      <span class="multi-open-label">多开</span>
      <b id="multiOpenCount" class="multi-open-count hidden">0</b>
    </button>
    <div class="server-select-wrapper">
      <button id="serverSelectTrigger" class="server-select-trigger" type="button"
        aria-haspopup="dialog" aria-controls="serverModal" aria-expanded="false">
        <span id="serverSelectLabel" class="server-select-label">大区加载中</span>
      </button>
      <select id="serverSelect" class="server-native-select sr-only"
        tabindex="-1" aria-hidden="true"></select>
    </div>
    <button id="selectGameRootBtn" class="home-dock-button" type="button">选择目录</button>
    <button class="start-btn" id="startBtn" type="button"
      data-state="disabled" style="--start-progress: 0%;" disabled>
      <span class="start-btn-spinner" aria-hidden="true"></span>
      <span id="startBtnText" class="start-btn-title">登录后开始游戏</span>
      <small id="startBtnDesc" class="start-btn-desc"></small>
    </button>
  </nav>
</footer>
```

Delete `#gmItemBrowserMenuBtn` from `#launcherMenu`; keep the game-root and restore-close-prompt menu items.

- [ ] **Step 4: Render the first bulletin into the ticker**

At the end of `renderLauncherContent` in `src/renderer/content-view.js`, before `return content`, add:

```js
const ticker = documentRef.getElementById('homeTickerText');
if (ticker) {
  ticker.textContent = content.bulletins[0]?.title
    || content.hero.subtitle
    || '活动与公告暂不可用';
}
```

- [ ] **Step 5: Add account-summary and activity-popover behavior**

In `src/renderer/app.js`, replace the GM menu reference and add:

```js
const homeDockAccountName = $('homeDockAccountName');
const gmItemBrowserBtn = $('gmItemBrowserBtn');
const homeAnnouncementTicker = $('homeAnnouncementTicker');
const homeActivityBtn = $('homeActivityBtn');
const homeActivityPopover = $('homeActivityPopover');
const homeActivityCloseBtn = $('homeActivityCloseBtn');
```

Add:

```js
function setRenderedAccountName(value) {
  const label = String(value || '').trim() || '未登录';
  accountName.textContent = label;
  homeDockAccountName.textContent = label;
}

function openHomeActivityPopover() {
  if (shell.dataset.view !== 'home') return;
  homeActivityPopover.classList.remove('hidden');
  homeActivityBtn.setAttribute('aria-expanded', 'true');
  homeAnnouncementTicker.setAttribute('aria-expanded', 'true');
  scheduleBannerGeometryRefresh();
  homeActivityCloseBtn.focus();
}

function closeHomeActivityPopover({ restoreFocus = false } = {}) {
  if (homeActivityPopover.classList.contains('hidden')) return;
  homeActivityPopover.classList.add('hidden');
  homeActivityBtn.setAttribute('aria-expanded', 'false');
  homeAnnouncementTicker.setAttribute('aria-expanded', 'false');
  if (restoreFocus && shell.dataset.view === 'home') homeActivityBtn.focus();
}

function toggleHomeActivityPopover() {
  if (homeActivityPopover.classList.contains('hidden')) {
    openHomeActivityPopover();
  } else {
    closeHomeActivityPopover({ restoreFocus: true });
  }
}
```

Replace direct writes to `accountName.textContent` in `applyAccountIdentity` and account selection with `setRenderedAccountName(...)`.

In `renderLauncherState`, use:

```js
gmItemBrowserBtn.classList.toggle(
  'hidden',
  view !== 'home' || !hasAuthenticatedLauncherSession()
);
if (view !== 'home') closeHomeActivityPopover();
```

Add listeners:

```js
homeActivityBtn.addEventListener('click', toggleHomeActivityPopover);
homeAnnouncementTicker.addEventListener('click', toggleHomeActivityPopover);
homeActivityCloseBtn.addEventListener(
  'click',
  () => closeHomeActivityPopover({ restoreFocus: true })
);
```

Extend the existing document click listener:

```js
if (
  !homeActivityPopover.classList.contains('hidden')
  && !homeActivityPopover.contains(event.target)
  && !homeActivityBtn.contains(event.target)
  && !homeAnnouncementTicker.contains(event.target)
) {
  closeHomeActivityPopover();
}
```

In the existing Escape handler, after modal checks and before closing the launcher menu:

```js
if (!homeActivityPopover.classList.contains('hidden')) {
  closeHomeActivityPopover({ restoreFocus: true });
  return;
}
```

- [ ] **Step 6: Implement the floating home surfaces**

Make the following the final home rules in `styles.css`:

```css
.shell[data-view="home"] .top-bar {
  top: 12px;
  left: clamp(14px, 1.6vw, 24px);
  right: clamp(14px, 1.6vw, 24px);
  height: var(--top-bar-height);
  padding-inline: clamp(18px, 2vw, 30px);
  border: 1px solid var(--crystal-edge-soft);
  border-radius: 14px;
  background:
    linear-gradient(180deg, rgba(255, 255, 255, 0.09), transparent 45%),
    rgba(4, 12, 17, 0.34);
  box-shadow:
    inset 0 1px rgba(255, 255, 255, 0.18),
    0 18px 44px rgba(0, 0, 0, 0.16);
  backdrop-filter: none;
}

.home-stage {
  position: absolute;
  z-index: 5;
  inset: calc(var(--top-bar-height) + 24px) 0 0;
}

.home-announcement-ticker {
  position: absolute;
  left: 50%;
  bottom: calc(var(--bottom-control-height) + 16px);
  width: min(78vw, 980px);
  height: 42px;
  display: grid;
  grid-template-columns: auto minmax(0, 1fr) auto;
  align-items: center;
  gap: 12px;
  padding: 0 18px;
  overflow: hidden;
  border: 1px solid rgba(216, 255, 255, 0.18);
  border-radius: 999px;
  color: var(--text);
  background:
    linear-gradient(180deg, rgba(255, 255, 255, 0.08), transparent 48%),
    rgba(4, 12, 17, 0.34);
  box-shadow: inset 0 1px rgba(255, 255, 255, 0.12), 0 12px 32px rgba(0, 0, 0, 0.14);
  backdrop-filter: none;
  transform: translateX(-50%);
}

.home-announcement-ticker > span {
  color: var(--ice);
}

.home-announcement-ticker strong {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-weight: 500;
}

.home-announcement-ticker small {
  color: var(--muted);
}

.home-activity-popover {
  position: absolute;
  z-index: 130;
  left: 50%;
  bottom: calc(var(--bottom-control-height) + 68px);
  width: min(92vw, 760px);
  max-height: min(70vh, 620px);
  overflow: hidden;
  border: 1px solid var(--crystal-edge);
  border-radius: 16px;
  background:
    linear-gradient(120deg, rgba(255, 255, 255, 0.1), transparent 22%),
    linear-gradient(180deg, rgba(6, 18, 24, 0.82), rgba(3, 10, 14, 0.74));
  box-shadow:
    inset 0 1px rgba(255, 255, 255, 0.22),
    inset 0 0 50px rgba(143, 252, 244, 0.03),
    0 28px 80px rgba(0, 0, 0, 0.38);
  backdrop-filter: none;
  transform: translateX(-50%);
}

.home-activity-popover > header {
  height: 58px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 18px 0 22px;
  border-bottom: 1px solid rgba(223, 255, 255, 0.15);
}

.home-activity-popover > header div {
  display: flex;
  align-items: baseline;
  gap: 12px;
}

.home-activity-popover > header span {
  color: var(--muted);
  font-size: 12px;
}

.home-activity-popover > header button {
  width: 34px;
  height: 34px;
  border: 0;
  color: #fff;
  background: transparent;
  font-size: 28px;
}

.home-content-deck {
  position: static;
  width: auto;
  display: grid;
  grid-template-columns: minmax(0, 1.6fr) minmax(220px, 0.8fr);
  gap: 12px;
  padding: 14px;
}

.home-banner-shell {
  width: 100%;
  height: 260px;
}

.home-banner-card {
  flex-basis: calc(min(92vw, 760px) - 276px);
  width: calc(min(92vw, 760px) - 276px);
  height: 260px;
}

.home-secondary-banner,
.home-bulletins {
  width: 100%;
}

.home-activity-timeline {
  grid-column: 1 / -1;
  padding: 12px 4px 2px;
}

.home-link:hover,
.home-link:focus-visible {
  color: #fff;
  background: rgba(50, 232, 220, 0.1);
}

.avatar-dot {
  background:
    radial-gradient(circle at 50% 35%, #fff 0 17%, transparent 18%),
    radial-gradient(circle at 50% 80%, #fff 0 30%, transparent 31%),
    linear-gradient(135deg, rgba(220, 255, 255, 0.94), rgba(50, 232, 220, 0.72));
}

.home-banner-card {
  background:
    radial-gradient(circle at 68% 18%, rgba(50, 232, 220, 0.18), transparent 42%),
    linear-gradient(135deg, rgba(10, 30, 35, 0.94), rgba(3, 12, 16, 0.94));
}

.home-secondary-banner,
.home-bulletins {
  border-color: rgba(215, 255, 255, 0.16);
  background: rgba(3, 13, 17, 0.46);
}

.home-banner-dots {
  background: rgba(2, 10, 14, 0.58);
  backdrop-filter: none;
}

.timeline-item.active > span:not(.timeline-popover),
.timeline-item.active small {
  color: var(--ice);
}

.timeline-popover {
  border-color: rgba(143, 252, 244, 0.66);
  background: rgba(3, 12, 16, 0.96);
}

.bottom-control-box {
  position: absolute;
  z-index: 100;
  left: clamp(14px, 1.6vw, 24px);
  right: clamp(14px, 1.6vw, 24px);
  bottom: 12px;
  min-height: var(--bottom-control-height);
  display: grid;
  grid-template-columns: minmax(280px, 1fr) auto;
  align-items: center;
  gap: clamp(14px, 2vw, 28px);
  padding: 14px clamp(16px, 2vw, 26px);
  border: 1px solid var(--crystal-edge-soft);
  border-radius: 16px;
  background:
    linear-gradient(180deg, rgba(255, 255, 255, 0.09), transparent 42%),
    rgba(4, 12, 17, 0.38);
  box-shadow:
    inset 0 1px rgba(255, 255, 255, 0.18),
    0 18px 54px rgba(0, 0, 0, 0.2);
  backdrop-filter: none;
}

.home-dock-summary {
  min-width: 0;
  display: grid;
  grid-template-columns: auto minmax(140px, 1fr);
  gap: 8px 18px;
}

.home-dock-summary > div {
  min-width: 0;
}

.home-dock-summary span {
  display: block;
  color: var(--muted);
  font-size: 11px;
}

.home-dock-summary strong {
  display: block;
  overflow: hidden;
  color: var(--text);
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 13px;
}

.home-dock-path {
  grid-column: 1 / -1;
}

.home-dock-actions {
  display: flex;
  align-items: stretch;
  gap: 8px;
}

.home-dock-button,
.server-select-trigger {
  min-width: 80px;
  height: 46px;
  padding: 0 14px;
  border: 1px solid rgba(220, 255, 255, 0.22);
  border-radius: 9px;
  color: rgba(247, 255, 255, 0.9);
  background:
    linear-gradient(180deg, rgba(255, 255, 255, 0.08), transparent 42%),
    rgba(4, 13, 18, 0.36);
}

.home-dock-button:hover,
.server-select-trigger:hover {
  border-color: rgba(143, 252, 244, 0.68);
  color: #fff;
  box-shadow: 0 0 20px rgba(50, 232, 220, 0.1);
}

.start-btn {
  min-width: clamp(170px, 15vw, 230px);
  height: 58px;
  border: 1px solid rgba(214, 255, 252, 0.72);
  border-radius: 10px;
  color: #071a1a;
  background:
    linear-gradient(180deg, rgba(255, 255, 255, 0.72), transparent 38%),
    linear-gradient(100deg, #b8fff9, #43eadf 52%, #a7fff8);
  box-shadow: inset 0 1px #fff, 0 10px 30px rgba(31, 226, 212, 0.2);
}

.start-btn-bg {
  display: none;
}
```

- [ ] **Step 7: Add the medium and compact home layouts**

```css
@media (max-width: 1200px) {
  .shell[data-view="home"] .dynamic-home-links .home-link:nth-child(n + 4) {
    display: none;
  }

  .bottom-control-box {
    grid-template-columns: 1fr;
    gap: 10px;
    padding-block: 10px;
  }

  .home-dock-summary {
    grid-template-columns: repeat(3, minmax(0, 1fr));
  }

  .home-dock-path {
    grid-column: auto;
  }

  .home-dock-actions {
    justify-content: flex-end;
  }
}

@media (max-width: 1080px) {
  .home-dock-actions {
    overflow-x: auto;
    justify-content: flex-start;
    scrollbar-width: none;
  }

  .home-dock-button,
  .server-select-trigger {
    flex: 0 0 auto;
  }

  .home-announcement-ticker {
    width: calc(100vw - 44px);
  }
}

@media (max-height: 700px) {
  :root {
    --bottom-control-height: 116px;
  }

  .home-announcement-ticker {
    height: 36px;
  }

  .home-activity-popover {
    max-height: calc(100vh - 170px);
  }

  .home-content-deck {
    overflow-y: auto;
  }
}
```

- [ ] **Step 8: Run home renderer and integration tests**

Run:

```powershell
node --test test/unit/renderer-content.test.js test/unit/responsive-launcher-layout.test.js test/unit/official-launcher-app-integration.test.js test/unit/official-multi-instance-integration.test.js
npm run check:renderer-flow
```

Expected: all tests PASS; the activity data still renders and multi-open/server/start IDs remain present.

- [ ] **Step 9: Commit the home console**

```powershell
git diff -- src/renderer/index.html src/renderer/content-view.js src/renderer/app.js src/renderer/styles.css test/unit/renderer-content.test.js test/unit/responsive-launcher-layout.test.js
git add -- src/renderer/index.html src/renderer/content-view.js src/renderer/app.js src/renderer/styles.css test/unit/renderer-content.test.js test/unit/responsive-launcher-layout.test.js
git commit -m "feat: add immersive launcher home console"
```

---

### Task 4: Upgrade GM to the A-enhanced text-category crystal browser

**Files:**
- Modify: `test/unit/gm-item-browser-integration.test.js`
- Modify: `src/renderer/index.html`
- Modify: `src/renderer/app.js`
- Modify: `src/renderer/styles.css`

**Interfaces:**
- Consumes: `api.getGmCatalog(primaryCategory)`, `api.queryGmItems(query)`, `resolveGmGridLayout(width)`, `formatGmSubcategoryLabel(value)`, and `createGmItemImageUrl(id)`.
- Produces: visible `#gmSecondaryCategories` text navigation while retaining hidden `#gmSecondaryCategory` as the query value owner.
- Preserves: `{ primaryCategory, secondaryCategory, gender, keyword, sortDirection, page, pageSize }`, pagination, image fallback and clipboard copy.

- [ ] **Step 1: Write failing GM layout and behavior assertions**

Replace the GM markup assertions in `test/unit/gm-item-browser-integration.test.js` with:

```js
test('renderer exposes an authenticated dock GM browser with two text category levels', () => {
  const html = read('src/renderer/index.html');
  assert.match(html, /img-src[^;]*https:\/\/iips\.speed\.qq\.com/);
  assert.match(html, /id="gmItemBrowserBtn"[^>]*class="home-dock-button hidden"/);
  assert.doesNotMatch(html, /id="gmItemBrowserMenuBtn"/);
  assert.match(html, /id="gmItemBrowserModal"[^>]*role="dialog"[^>]*aria-modal="true"/);
  for (const id of [
    'gmPrimaryCategories',
    'gmSecondaryCategories',
    'gmSecondaryCategory',
    'gmKeyword',
    'gmGender',
    'gmSortDirection',
    'gmQueryBtn',
    'gmResetBtn',
    'gmItemGrid',
    'gmResultSummary',
    'gmPrevPageBtn',
    'gmNextPageBtn',
    'gmModalCloseBtn'
  ]) {
    assert.match(html, new RegExp(`id="${id}"`), id);
  }
  assert.doesNotMatch(html, /gm-category-icon/);
});
```

Extend the behavior test:

```js
assert.match(app, /gmSecondaryCategories\.replaceChildren/);
assert.match(app, /dataGmSecondary|dataset\.gmSecondary/);
assert.match(app, /className = 'gm-item-card-category'/);
assert.match(app, /gmItemBrowserBtn\.classList\.toggle/);
assert.match(app, /gmSecondaryCategories\.addEventListener\('click'/);
```

Replace the CSS test with:

```js
test('GM uses a no-blur crystal panel and a three-row variable-column card grid', () => {
  const css = read('src/renderer/styles.css');
  assert.match(finalCssRule(css, '.gm-item-modal'), /backdrop-filter:\s*none/);
  assert.match(finalCssRule(css, '.gm-item-panel'), /backdrop-filter:\s*none/);
  assert.match(css, /\.gm-primary-categories\s*\{[^}]*width:/s);
  assert.match(css, /\.gm-primary-category\s*\{[^}]*text-align:\s*left/s);
  assert.match(css, /\.gm-secondary-categories\s*\{[^}]*display:\s*flex/s);
  assert.doesNotMatch(css, /\.gm-category-icon/);
  assert.match(css, /\.gm-item-grid\s*\{[^}]*grid-template-columns:\s*repeat\(var\(--gm-columns\),\s*minmax\(0,\s*1fr\)\)/s);
  assert.match(css, /\.gm-item-grid\s*\{[^}]*grid-template-rows:\s*repeat\(3,\s*minmax\(0,\s*1fr\)\)/s);
  assert.match(css, /\.gm-item-card-category\s*\{/);
});
```

Copy the `finalCssRule` helper used in the responsive test into this test file so it remains independently runnable.

- [ ] **Step 2: Run GM tests and confirm the new contract fails**

Run:

```powershell
node --test test/unit/gm-item-browser.test.js test/unit/gm-item-browser-integration.test.js
```

Expected: `gm-item-browser.test.js` PASS and integration test FAIL because the dock entry, secondary text navigation and card category are missing.

- [ ] **Step 3: Add visible secondary-category text navigation**

In `src/renderer/index.html`, remove the visible secondary `<select>` from the filter row. Place this immediately after `#gmFilterForm`:

```html
<div class="gm-secondary-row">
  <span>二级分类</span>
  <nav
    id="gmSecondaryCategories"
    class="gm-secondary-categories"
    aria-label="二级分类"
  ></nav>
  <select id="gmSecondaryCategory" class="sr-only" disabled tabindex="-1" aria-hidden="true">
    <option value="">全部二级分类</option>
  </select>
</div>
```

The filter form keeps search, gender, sort, query and reset in that order.

- [ ] **Step 4: Render and synchronize the secondary text buttons**

Add the new reference:

```js
const gmSecondaryCategories = $('gmSecondaryCategories');
```

Replace `renderGmSecondaryCategories` with:

```js
function createGmSecondaryButton(value, label, count) {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'gm-secondary-category';
  button.dataset.gmSecondary = value;
  button.classList.toggle('is-active', gmBrowserState.secondaryCategory === value);
  button.setAttribute('aria-pressed', String(gmBrowserState.secondaryCategory === value));
  button.textContent = label;
  if (Number.isFinite(Number(count))) {
    const countNode = document.createElement('small');
    countNode.textContent = String(Math.max(0, Number(count) || 0));
    button.appendChild(countNode);
  }
  return button;
}

function renderGmSecondaryCategories(subcategories = gmCatalogState.subcategories) {
  const optionFragment = document.createDocumentFragment();
  const buttonFragment = document.createDocumentFragment();
  const allLabel = gmBrowserState.primaryCategory
    ? `全部${gmBrowserState.primaryCategory}`
    : '全部二级分类';
  const allOption = document.createElement('option');
  allOption.value = '';
  allOption.textContent = allLabel;
  optionFragment.appendChild(allOption);
  buttonFragment.appendChild(createGmSecondaryButton('', allLabel));

  for (const entry of Array.isArray(subcategories) ? subcategories : []) {
    const value = String(entry?.value || '').trim();
    if (!value) continue;
    const label = formatGmSubcategoryLabel(entry?.label || value);
    const option = document.createElement('option');
    option.value = value;
    option.textContent = label;
    optionFragment.appendChild(option);
    buttonFragment.appendChild(createGmSecondaryButton(value, label, entry?.count));
  }

  gmSecondaryCategory.replaceChildren(optionFragment);
  const available = [...gmSecondaryCategory.options].some(
    (option) => option.value === gmBrowserState.secondaryCategory
  );
  if (!available) gmBrowserState.secondaryCategory = '';
  gmSecondaryCategory.value = gmBrowserState.secondaryCategory;
  gmSecondaryCategory.disabled = gmBrowserState.busy || !gmBrowserState.primaryCategory;
  gmSecondaryCategories.replaceChildren(buttonFragment);
  gmSecondaryCategories.classList.toggle('is-disabled', !gmBrowserState.primaryCategory);
}
```

Extend `setGmBrowserBusy`:

```js
for (const button of gmSecondaryCategories.querySelectorAll('button')) {
  button.disabled = gmBrowserState.busy || !gmBrowserState.primaryCategory;
}
```

Add:

```js
gmSecondaryCategories.addEventListener('click', (event) => {
  const button = event.target.closest('button[data-gm-secondary]');
  if (!button || button.disabled) return;
  gmBrowserState.secondaryCategory = button.dataset.gmSecondary;
  gmSecondaryCategory.value = gmBrowserState.secondaryCategory;
  gmBrowserState.page = 1;
  renderGmSecondaryCategories();
  void queryGmItems();
});
```

Keep the hidden select change listener only if a test or assistive path still invokes it; the visible buttons are the normal user path.

In `renderGmPrimaryCategories`, use the approved left-rail label:

```js
fragment.appendChild(createGmCategoryButton('', '全部', total));
```

- [ ] **Step 5: Show secondary category on each GM card**

In `createGmItemCard`, after the ID node and before the copy button, insert:

```js
const category = document.createElement('span');
category.className = 'gm-item-card-category';
category.textContent = formatGmSubcategoryLabel(
  item.subcategory || item.secondaryCategory || '未分类'
);
category.title = category.textContent;
card.appendChild(category);
```

Replace every `gmItemBrowserMenuBtn` reference with `gmItemBrowserBtn`, including:

```js
if (restoreFocus && !gmItemBrowserBtn.classList.contains('hidden')) {
  gmItemBrowserBtn.focus();
}
```

and:

```js
gmItemBrowserBtn.addEventListener('click', () => void openGmItemBrowser());
```

- [ ] **Step 6: Apply the A-enhanced GM crystal material**

Make these the final GM container/category rules:

```css
.gm-item-modal {
  position: absolute;
  z-index: 260;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(1, 7, 10, 0.48);
  backdrop-filter: none;
  -webkit-app-region: no-drag;
}

.gm-item-panel {
  position: relative;
  width: min(94vw, 1380px);
  height: min(88vh, 780px);
  min-width: 0;
  min-height: 0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  border: 1px solid rgba(225, 255, 255, 0.46);
  border-radius: 16px;
  color: #f7ffff;
  background:
    linear-gradient(118deg, rgba(255, 255, 255, 0.12), transparent 19%),
    repeating-linear-gradient(122deg, rgba(255, 255, 255, 0.014) 0 1px, transparent 1px 9px),
    linear-gradient(180deg, rgba(6, 18, 24, 0.88), rgba(3, 10, 14, 0.82));
  box-shadow:
    inset 0 1px rgba(255, 255, 255, 0.26),
    inset 0 0 70px rgba(116, 255, 246, 0.035),
    0 34px 100px rgba(0, 0, 0, 0.52),
    0 0 0 1px rgba(111, 237, 231, 0.07);
  backdrop-filter: none;
}

.gm-item-panel::before {
  content: "";
  position: absolute;
  z-index: 2;
  top: -44%;
  left: -14%;
  width: 52%;
  height: 118%;
  background: linear-gradient(112deg, transparent 36%, rgba(255, 255, 255, 0.12), transparent 62%);
  pointer-events: none;
  transform: rotate(7deg);
}

.gm-item-header,
.gm-item-body {
  position: relative;
  z-index: 3;
}

.gm-item-body {
  min-height: 0;
  flex: 1 1 auto;
  display: grid;
  grid-template-columns: clamp(122px, 10.5vw, 156px) minmax(0, 1fr);
}

.gm-primary-categories {
  width: clamp(122px, 10.5vw, 156px);
  min-height: 0;
  overflow-y: auto;
  border-right: 1px solid rgba(215, 255, 255, 0.14);
  background:
    linear-gradient(90deg, rgba(255, 255, 255, 0.045), transparent),
    rgba(2, 10, 14, 0.2);
  scrollbar-width: thin;
}

.gm-primary-category {
  width: 100%;
  min-height: 46px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 6px;
  padding: 9px 13px;
  border: 0;
  border-bottom: 1px solid rgba(215, 255, 255, 0.07);
  color: rgba(237, 255, 255, 0.74);
  background: transparent;
  text-align: left;
}

.gm-primary-category:hover,
.gm-primary-category.is-active {
  color: #fff;
  background:
    linear-gradient(90deg, rgba(50, 232, 220, 0.2), rgba(50, 232, 220, 0.035));
}

.gm-primary-category.is-active {
  box-shadow:
    inset 3px 0 var(--ice-strong),
    inset 0 1px rgba(255, 255, 255, 0.08),
    0 0 24px rgba(50, 232, 220, 0.08);
}

.gm-secondary-row {
  min-width: 0;
  display: grid;
  grid-template-columns: auto minmax(0, 1fr);
  align-items: center;
  gap: 10px;
  padding: 10px 0 8px;
  color: var(--muted);
  font-size: 12px;
}

.gm-secondary-categories {
  min-width: 0;
  display: flex;
  gap: 7px;
  overflow-x: auto;
  scrollbar-width: thin;
}

.gm-secondary-category {
  flex: 0 0 auto;
  min-height: 30px;
  padding: 0 11px;
  border: 1px solid rgba(218, 255, 255, 0.16);
  border-radius: 999px;
  color: rgba(240, 255, 255, 0.74);
  background: rgba(5, 16, 21, 0.34);
}

.gm-secondary-category.is-active {
  border-color: rgba(143, 252, 244, 0.72);
  color: #fff;
  background: rgba(50, 232, 220, 0.16);
  box-shadow: inset 0 1px rgba(255, 255, 255, 0.14), 0 0 18px rgba(50, 232, 220, 0.12);
}
```

- [ ] **Step 7: Apply the compact crystal card grid**

```css
.gm-item-main {
  min-width: 0;
  min-height: 0;
  display: grid;
  grid-template-rows: auto auto 20px minmax(0, 1fr) auto;
  padding: 14px 16px 12px;
}

.gm-filter-form {
  display: grid;
  grid-template-columns: minmax(210px, 1.8fr) repeat(2, minmax(118px, 1fr)) 86px 68px;
  gap: 9px;
}

.gm-filter-form input,
.gm-filter-form select,
.gm-filter-form button {
  height: 38px;
  border: 1px solid rgba(218, 255, 255, 0.18);
  border-radius: 7px;
  color: rgba(247, 255, 255, 0.94);
  background:
    linear-gradient(180deg, rgba(255, 255, 255, 0.07), transparent 40%),
    rgba(3, 12, 16, 0.4);
}

.gm-filter-form .gm-query-button {
  color: #071a1a;
  background:
    linear-gradient(180deg, rgba(255, 255, 255, 0.68), transparent 38%),
    linear-gradient(100deg, #b8fff9, #43eadf);
  font-weight: 700;
}

.gm-item-grid {
  --gm-columns: 10;
  width: 100%;
  height: 100%;
  display: grid;
  grid-template-columns: repeat(var(--gm-columns), minmax(0, 1fr));
  grid-template-rows: repeat(3, minmax(0, 1fr));
  gap: clamp(5px, 0.62vw, 9px);
}

.gm-item-card {
  min-width: 0;
  min-height: 0;
  display: grid;
  grid-template-rows: minmax(42px, 1fr) auto auto auto 24px;
  gap: 2px;
  padding: 5px;
  overflow: hidden;
  border: 1px solid rgba(200, 249, 248, 0.18);
  border-radius: 8px;
  color: rgba(248, 255, 255, 0.96);
  background:
    linear-gradient(180deg, rgba(255, 255, 255, 0.07), transparent 31%),
    rgba(4, 15, 20, 0.46);
  box-shadow:
    inset 0 1px rgba(255, 255, 255, 0.1),
    inset 0 -16px 26px rgba(0, 0, 0, 0.08);
}

.gm-item-card:hover,
.gm-item-card:focus-within {
  border-color: rgba(143, 252, 244, 0.72);
  box-shadow:
    inset 0 1px rgba(255, 255, 255, 0.15),
    0 0 20px rgba(50, 232, 220, 0.1);
}

.gm-item-card-image-shell {
  background:
    radial-gradient(circle at 50% 38%, rgba(255, 255, 255, 0.92), rgba(210, 230, 232, 0.72));
}

.gm-item-card-category {
  min-width: 0;
  overflow: hidden;
  color: rgba(143, 252, 244, 0.72);
  text-align: center;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 9px;
  line-height: 12px;
}

.gm-item-copy {
  border-color: rgba(203, 250, 248, 0.2);
  border-radius: 5px;
  color: rgba(246, 255, 255, 0.86);
  background: rgba(12, 38, 42, 0.54);
}

.gm-item-copy:hover {
  border-color: rgba(143, 252, 244, 0.7);
  color: var(--ice);
}
```

- [ ] **Step 8: Run all GM and IPC tests**

Run:

```powershell
node --test test/unit/gm-item-browser.test.js test/unit/gm-item-browser-integration.test.js test/unit/gm-item-catalog.test.js test/unit/gm-item-database.test.js test/unit/gm-item-query-service.test.js test/unit/gm-item-ipc-integration.test.js
```

Expected: all tests PASS; the 10/8/6 helper still returns 30/24/18.

- [ ] **Step 9: Commit the GM presentation**

```powershell
git diff -- src/renderer/index.html src/renderer/app.js src/renderer/styles.css test/unit/gm-item-browser-integration.test.js
git add -- src/renderer/index.html src/renderer/app.js src/renderer/styles.css test/unit/gm-item-browser-integration.test.js
git commit -m "feat: strengthen the crystal GM item browser"
```

---

### Task 5: Lock version 1.0.4 and update packaged smoke coverage

**Files:**
- Modify: `test/unit/embedded-native-packaging.test.js`
- Modify: `src/scripts/smoke-release.cjs`
- Modify: `package.json`
- Modify: `package-lock.json`

**Interfaces:**
- Consumes: electron-builder `files`, `asarUnpack`, `extraResources`; shared video ID.
- Produces: package version `1.0.4` and release smoke state `{ readyState, duration, paused, muted, volume, source, soundLabel }`.
- Preserves: `assets/launcher-background.mp4` unpacking and `resources/gm-items.sqlite3 -> gm-items.sqlite3`.

- [ ] **Step 1: Add failing release-boundary assertions**

In `test/unit/embedded-native-packaging.test.js`, add:

```js
test('1.0.4 packages one video soundtrack and the GM database without independent music UI', () => {
  const html = read('src/renderer/index.html');
  assert.equal(packageJson.version, '1.0.4');
  assert.ok((packageJson.build?.files || []).includes('assets/**/*'));
  assert.ok((packageJson.build?.asarUnpack || []).includes('assets/launcher-background.mp4'));
  assert.ok((packageJson.build?.extraResources || []).some(
    (entry) => entry.from === 'resources/gm-items.sqlite3' && entry.to === 'gm-items.sqlite3'
  ));
  assert.match(html, /id="launcherBackgroundVideo"/);
  assert.doesNotMatch(html, /id="launcherMusic"|musicMuteBtn|musicVolume/);
});
```

- [ ] **Step 2: Run the packaging test and confirm only the version assertion fails**

Run:

```powershell
node --test test/unit/embedded-native-packaging.test.js
```

Expected: FAIL with `1.0.3 !== 1.0.4`; MP4, GM database and no-music assertions pass.

- [ ] **Step 3: Bump package and lockfile versions without creating a tag**

Run:

```powershell
npm version 1.0.4 --no-git-tag-version
```

Expected output:

```text
v1.0.4
```

Verify only the root versions:

```powershell
node -e "const p=require('./package.json');const l=require('./package-lock.json');if(p.version!=='1.0.4'||l.version!=='1.0.4'||l.packages[''].version!=='1.0.4')process.exit(1);console.log('VERSION_1_0_4_OK')"
```

Expected:

```text
VERSION_1_0_4_OK
```

- [ ] **Step 4: Point the packaged smoke test at the shared video**

In `src/scripts/smoke-release.cjs`, replace:

```js
const loginVideo = document.getElementById('loginMediaVideo');
```

with:

```js
const backgroundVideo = document.getElementById('launcherBackgroundVideo');
```

Build the returned state from `backgroundVideo`:

```js
backgroundVideo: {
  readyState: Number(backgroundVideo?.readyState || 0),
  duration: Number(backgroundVideo?.duration || 0),
  paused: Boolean(backgroundVideo?.paused),
  muted: Boolean(backgroundVideo?.muted),
  volume: Number(backgroundVideo?.volume || 0),
  source: String(backgroundVideo?.currentSrc || backgroundVideo?.src || ''),
  soundLabel: backgroundSoundButton?.getAttribute('aria-label') || ''
}
```

Retain the existing readiness checks and add:

```js
assert.equal(state.backgroundVideo.muted, false);
assert.equal(state.backgroundVideo.volume, 0.7);
assert.match(state.backgroundVideo.soundLabel, /^关闭背景声音$/);
```

- [ ] **Step 5: Run packaging and static checks**

Run:

```powershell
node --test test/unit/embedded-native-packaging.test.js
npm run check
npm run check:renderer-flow
```

Expected: all commands PASS.

- [ ] **Step 6: Commit the version and smoke boundary**

```powershell
git diff -- package.json package-lock.json src/scripts/smoke-release.cjs test/unit/embedded-native-packaging.test.js
git add -- package.json package-lock.json src/scripts/smoke-release.cjs test/unit/embedded-native-packaging.test.js
git commit -m "chore: prepare launcher 1.0.4 release"
```

---

### Task 6: Run the complete regression suite and inspect the real Electron UI

**Files:**
- Verify only: all source and test files modified by Tasks 1–5.
- Do not modify release behavior to bypass a failing test.

**Interfaces:**
- Consumes: completed renderer, existing close/tray/GM/multi-open/launch implementations.
- Produces: a verified source state ready for packaging.

- [ ] **Step 1: Run syntax and complete unit tests**

Run:

```powershell
npm run check
npm run test:unit
```

Expected: both commands exit `0`; all unit tests pass with no skipped renderer/GM regression.

- [ ] **Step 2: Run focused renderer, lifecycle and trust smoke tests**

Run:

```powershell
npm run check:renderer-flow
npm run check:lifecycle
npm run check:ipc-trust
npm run check:native-boundary
```

Expected: all four commands exit `0`.

- [ ] **Step 3: Launch the development Electron app**

Run:

```powershell
npm start
```

Verify at the initial 1280×720 window:

1. Cold start shows video or the neutral dark fallback only; the old QQ Speed image never appears.
2. Video starts with original sound at 70%; the speaker button mutes and restores it.
3. Login panel is transparent crystal without background blur or fixed blue/purple fill.
4. Resizing down to the configured 1024×640 minimum keeps the login fields, agreement and window buttons reachable.
5. The title-bar close button and `Alt+F4` both enter the same existing close-confirm flow.
6. Choosing tray hide leaves the process running; the tray menu still contains `打开启动器` and `退出启动器`.
7. A programmatic close after successful game launch remains on its existing no-prompt path.

Exit using the existing tray menu `退出启动器` after inspection.

- [ ] **Step 4: Verify the authenticated home and GM flow**

Using a valid local launcher account:

1. Complete login and confirm the same video continues without restarting or flashing an image.
2. Confirm the floating top bar, announcement ticker and bottom dock use the same neutral crystal material.
3. Confirm `GM 道具查询` appears only after login.
4. Open Activity; click outside and press `Escape` in separate attempts; each closes the popover.
5. Open GM, select `赛车 -> S` and `服饰 -> 套装`; confirm the second-level list changes.
6. Query ascending and descending ID order.
7. Confirm each card shows image, name, ID, secondary category and copy button.
8. Resize GM and confirm the page-size label changes through `30`, `24`, and `18`, always keeping three rows.
9. Log out and confirm the GM entry disappears and an open GM modal closes.

- [ ] **Step 5: Review the diff for scope violations**

Run:

```powershell
git status --short
git diff --check
git diff --stat
```

Expected:

- `git diff --check` emits no whitespace errors.
- No staged or newly modified file is under `F:\PacketDumpTool\21487\server\Server`.
- `.superpowers/` remains untracked and unstaged.
- `resources/gm-items.sqlite3` is unchanged by the UI work.

- [ ] **Step 6: Commit any test-only corrections discovered during verification**

Only if verification required a source/test correction:

```powershell
git add -- src/renderer/index.html src/renderer/styles.css src/renderer/app.js src/renderer/content-view.js src/renderer/media-controller.js src/scripts/smoke-renderer-flow.cjs src/scripts/smoke-release.cjs test/unit/renderer-content.test.js test/unit/responsive-launcher-layout.test.js test/unit/gm-item-browser-integration.test.js test/unit/embedded-native-packaging.test.js
git commit -m "fix: close launcher 1.0.4 UI regressions"
```

If no correction was needed, do not create an empty commit.

---

### Task 7: Build 1.0.4 artifacts and publish the signed update manifest

**Files:**
- Generate: `dist/Kiss-Setup-1.0.4-x64.exe`
- Generate: `dist/Kiss-Portable-1.0.4-x64.exe`
- Generate: `dist/update-1.0.4/Kiss-Setup-1.0.4-x64.exe`
- Generate: `dist/update-1.0.4/update.json`
- Verify: `dist/win-unpacked/Kiss.exe`

**Interfaces:**
- Consumes: version 1.0.4 package metadata, packaged MP4, GM SQLite resource, existing update signing key path documented by the project.
- Produces: installer, portable executable, copied update installer and signed manifest.
- Security boundary: the private signing key is read by `publish-update.cjs`; its contents must never be printed, copied into the repository or added to a command log.

- [ ] **Step 1: Build installer and portable packages**

Run:

```powershell
npm run dist:all
```

Expected:

- Native/runtime verification passes.
- electron-builder produces `dist/Kiss-Setup-1.0.4-x64.exe`.
- portable builder produces `dist/Kiss-Portable-1.0.4-x64.exe`.
- `postdist:all` runs the real packaged release smoke successfully.

- [ ] **Step 2: Verify the release files exist and are non-empty**

Run:

```powershell
$files = @(
  'dist\Kiss-Setup-1.0.4-x64.exe',
  'dist\Kiss-Portable-1.0.4-x64.exe',
  'dist\win-unpacked\Kiss.exe'
)
foreach ($file in $files) {
  $item = Get-Item -LiteralPath $file -ErrorAction Stop
  if ($item.Length -le 0) { throw "Empty release file: $file" }
  "{0}`t{1}" -f $item.Name, $item.Length
}
```

Expected: three filenames and positive byte sizes.

- [ ] **Step 3: Run explicit installed and portable smoke checks**

Run:

```powershell
npm run check:packaged
npm run check:portable-x64
npm run check:portable-runtime-x64
```

Expected: all three commands exit `0`; packaged renderer finds the shared video, volume `0.7`, `muted=false`, and the GM database resource.

- [ ] **Step 4: Generate the signed 1.0.4 update directory**

Run:

```powershell
npm run update:publish -- `
  --installer 'dist\Kiss-Setup-1.0.4-x64.exe' `
  --output 'dist\update-1.0.4' `
  --private-key 'C:\Users\lp\.qqspeed-launcher\update-signing-private.pem' `
  --version '1.0.4'
```

Expected:

```text
[update] published Kiss-Setup-1.0.4-x64.exe version 1.0.4 SHA256 ...
```

If the documented private key file is absent, stop this step and report only that the manifest cannot be signed; do not generate a new key because it would not match existing clients.

- [ ] **Step 5: Verify manifest fields and installer hash**

Run:

```powershell
@'
const fs = require('node:fs');
const crypto = require('node:crypto');
const path = require('node:path');
const root = path.resolve('dist/update-1.0.4');
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'update.json'), 'utf8'));
if (manifest.version !== '1.0.4') throw new Error('wrong manifest version');
if (manifest.file !== 'Kiss-Setup-1.0.4-x64.exe') throw new Error('wrong installer name');
if (manifest.required !== true) throw new Error('update must remain required');
if (!/^[a-f0-9]{64}$/.test(manifest.sha256)) throw new Error('invalid SHA256');
if (!manifest.signature) throw new Error('missing signature');
const actual = crypto.createHash('sha256')
  .update(fs.readFileSync(path.join(root, manifest.file)))
  .digest('hex');
if (actual !== manifest.sha256) throw new Error('installer hash mismatch');
console.log('UPDATE_1_0_4_OK');
'@ | node
```

Expected:

```text
UPDATE_1_0_4_OK
```

- [ ] **Step 6: Record final artifact hashes without modifying 1.0.3**

Run:

```powershell
Get-FileHash -Algorithm SHA256 `
  'dist\Kiss-Setup-1.0.4-x64.exe', `
  'dist\Kiss-Portable-1.0.4-x64.exe', `
  'dist\update-1.0.4\update.json' |
  Format-Table Path, Hash -AutoSize
```

Expected: three SHA256 values; existing `dist\Kiss-Setup-1.0.3-x64.exe`, `dist\Kiss-Portable-1.0.3-x64.exe`, and `dist\update-1.0.3` remain present.

---

## Final Acceptance Checklist

- [ ] `node --test test/unit/renderer-content.test.js` passes.
- [ ] `node --test test/unit/responsive-launcher-layout.test.js` passes.
- [ ] All six GM unit/integration files pass.
- [ ] Complete `npm run test:unit` passes.
- [ ] `npm run check` and renderer/lifecycle/IPC smoke checks pass.
- [ ] Cold start never renders `login-bg-default-BpD5IuOu.png`.
- [ ] DOM contains one local video, no `poster`, no launcher audio node and no music controls.
- [ ] Original video sound defaults to unmuted at volume 0.7.
- [ ] Login/home transitions leave the shared video uninterrupted.
- [ ] Login panel, home top bar, home dock, activity popover and GM panel use `backdrop-filter: none`.
- [ ] Home Activity popover closes by outside click and `Escape`.
- [ ] GM appears only after authentication and closes/hides on logout.
- [ ] GM category labels are text-only; second-level options follow the first-level category.
- [ ] GM grids are 10×3, 8×3 and 6×3 with page sizes 30, 24 and 18.
- [ ] Close confirmation, tray, game-root selection, server selection, multi-open and start-game regressions pass.
- [ ] Title-bar close and `Alt+F4` share the close prompt, while successful-launch auto-close remains prompt-free.
- [ ] Installer, portable executable and signed update manifest are all version 1.0.4.
- [ ] 1.0.3 release artifacts are preserved.
