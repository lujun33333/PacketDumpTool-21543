const LOCAL_ASSET_PREFIX = '../../assets/';
const LOCAL_ASSET_PATH_PATTERN = /^[A-Za-z0-9._~!$&'()+,;=@%/-]+$/;
const TRUSTED_MEDIA_HOSTS = [
  'qq.com',
  'gtimg.com',
  'gtimg.cn',
  'qpic.cn'
];

const CUSTOMER_BRAND = 'QQ飞车';

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

function isTrustedHost(hostname) {
  const normalized = String(hostname || '').toLowerCase();
  return TRUSTED_MEDIA_HOSTS.some((host) => normalized === host || normalized.endsWith(`.${host}`));
}

export function sanitizeMediaSource(value) {
  const source = typeof value === 'string' ? value.trim() : '';
  if (!source || source.length > 2048) {
    return '';
  }

  if (source.startsWith(LOCAL_ASSET_PREFIX)) {
    const relativePath = source.slice(LOCAL_ASSET_PREFIX.length);
    const segments = relativePath.split('/');
    if (
      relativePath &&
      LOCAL_ASSET_PATH_PATTERN.test(relativePath) &&
      !source.includes('\\') &&
      !/%(?:2e|2f|5c)/i.test(source) &&
      segments.every((segment) => segment && segment !== '.' && segment !== '..')
    ) {
      return source;
    }
    return '';
  }

  try {
    const url = new URL(source);
    if (
      url.protocol !== 'https:' ||
      url.username ||
      url.password ||
      (url.port && url.port !== '443') ||
      !isTrustedHost(url.hostname)
    ) {
      return '';
    }
    return url.href;
  } catch {
    return '';
  }
}

function sanitizeNavigationTarget(value) {
  const target = sanitizeMediaSource(value);
  return target.startsWith('https://') ? target : '';
}

function text(value, fallback = '', maxLength = 160, { preserveEmpty = false } = {}) {
  const hasTextValue = typeof value === 'string' || typeof value === 'number';
  const normalized = hasTextValue
    ? String(value).replace(/[\u0000-\u001f\u007f]/g, ' ').trim()
    : '';
  return (preserveEmpty && hasTextValue ? normalized : normalized || fallback).slice(0, maxLength);
}


function normalizeBanners(value) {
  if (!Array.isArray(value)) {
    return [];
  }
  return value.slice(0, 12).map((item, index) => ({
    id: text(item?.id, `banner-${index + 1}`, 64),
    title: text(item?.title, '活动资讯', 80),
    subtitle: text(item?.subtitle || item?.description, '', 120),
    imageUrl: sanitizeMediaSource(item?.imageUrl || item?.image || item?.source),
    linkId: text(item?.linkId || item?.id, '', 64),
    link: sanitizeNavigationTarget(item?.link || item?.url)
  }));
}

function normalizeSecondaryActivities(value) {
  if (!Array.isArray(value)) {
    return [];
  }
  return value.slice(0, 4).map((item, index) => ({
    id: text(item?.id, `secondary-${index + 1}`, 64),
    title: text(item?.title, '', 80),
    imageUrl: sanitizeMediaSource(item?.imageUrl || item?.image || item?.source),
    link: sanitizeNavigationTarget(item?.link || item?.url)
  })).filter((item) => item.imageUrl);
}

function normalizeBulletins(value) {
  if (!Array.isArray(value)) {
    return [];
  }
  const normalized = value.slice(0, 24).map((item, index) => ({
    id: text(item?.id, `bulletin-${index + 1}`, 64),
    category: text(item?.category || item?.type, '公告', 12),
    title: text(item?.title, '公告', 100),
    date: text(item?.date || item?.publishedAt, '', 20),
    linkId: text(item?.linkId || item?.id, '', 64),
    link: sanitizeNavigationTarget(item?.link || item?.url)
  }));
  const categoryCounts = new Map();
  return normalized.filter((item) => {
    const count = categoryCounts.get(item.category) || 0;
    categoryCounts.set(item.category, count + 1);
    return count < 3;
  });
}

function normalizeTimeline(value) {
  const source = value && typeof value === 'object' ? value : {};
  const items = Array.isArray(source.items)
    ? source.items.slice(0, 8).map((item, index) => {
        const status = ['done', 'active', 'upcoming'].includes(item?.status)
          ? item.status
          : index === 0 ? 'active' : 'upcoming';
        return {
          id: text(item?.id, `release-${index + 1}`, 64),
          title: text(item?.title, '版本节点', 40),
          date: text(item?.date, '', 20),
          status,
          description: text(item?.description || item?.subtitle, '', 120),
          imageUrl: sanitizeMediaSource(item?.imageUrl || item?.picUrl || item?.pic_url),
          link: sanitizeNavigationTarget(item?.link || item?.jumpTarget || item?.jump_target)
        };
      })
    : [];

  const suppliedProgress = Number(source.progress);
  return {
    items,
    progress: Number.isFinite(suppliedProgress) && suppliedProgress >= 0 && suppliedProgress <= 100
      ? suppliedProgress
      : computeTimelineProgress(items)
  };
}

function normalizeServers(value) {
  const knownStatuses = new Set(['smooth', 'busy', 'full', 'maintenance']);
  const servers = Array.isArray(value)
    ? value.slice(0, 32).map((item, index) => {
        const available = item?.available !== false;
        const requestedStatus = text(item?.status || item?.state, '', 20).toLowerCase();
        return {
          id: text(item?.id ?? item?.value, String(index + 1), 32),
          label: text(item?.label || item?.name, `大区 ${index + 1}`, 40),
          available,
          selected: item?.selected === true,
          status: available && knownStatuses.has(requestedStatus) && requestedStatus !== 'maintenance'
            ? requestedStatus
            : available ? 'smooth' : 'maintenance'
        };
      }).filter((item) => item.id)
    : [];

  if (!servers.length) {
    return DEFAULT_LAUNCHER_CONTENT.servers.map((server) => ({ ...server }));
  }
  if (!servers.some((server) => server.selected && server.available)) {
    const firstAvailable = servers.find((server) => server.available);
    if (firstAvailable) {
      firstAvailable.selected = true;
    }
  }
  return servers;
}

function normalizeLinks(value) {
  if (!Array.isArray(value)) {
    return [];
  }
  return value.slice(0, 8)
    .map((item, index) => ({
      id: text(item?.id, `link-${index + 1}`, 64),
      label: text(item?.label || item?.title, '快捷入口', 24),
      url: sanitizeNavigationTarget(item?.url)
    }))
    .filter((item) => item.url);
}

export function computeTimelineProgress(items) {
  if (!Array.isArray(items) || !items.length) {
    return 0;
  }
  const activeIndex = items.findIndex((item) => item?.status === 'active');
  const completedIndex = items.reduce(
    (last, item, index) => item?.status === 'done' ? index : last,
    -1
  );
  const position = activeIndex >= 0 ? activeIndex : completedIndex;
  if (items.length === 1) {
    return position >= 0 ? 100 : 0;
  }
  return Math.round((Math.max(0, position) / (items.length - 1)) * 100);
}

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

function createTextElement(documentRef, tagName, className, value) {
  const element = documentRef.createElement(tagName);
  if (className) {
    element.className = className;
  }
  element.textContent = value;
  return element;
}

function renderBanners(documentRef, container, banners) {
  if (!container) {
    return;
  }
  const fragment = documentRef.createDocumentFragment();
  if (!banners.length) {
    const empty = createTextElement(documentRef, 'div', 'home-content-empty', '活动内容暂不可用');
    fragment.appendChild(empty);
  } else {
    banners.forEach((banner, index) => {
      const card = documentRef.createElement(banner.link ? 'button' : 'article');
      if (banner.link) {
        card.type = 'button';
        card.dataset.linkUrl = banner.link;
      }
      card.className = 'home-banner-card';
      card.dataset.bannerId = banner.id;
      card.dataset.bannerTitle = banner.title;
      card.classList.toggle('active', index === 0);

      if (banner.imageUrl) {
        const image = documentRef.createElement('img');
        image.src = banner.imageUrl;
        image.alt = '';
        image.loading = index === 0 ? 'eager' : 'lazy';
        image.draggable = false;
        image.addEventListener('error', () => card.classList.add('image-failed'), { once: true });
        card.appendChild(image);
      }

      const copy = documentRef.createElement('span');
      copy.className = 'home-banner-copy';
      copy.appendChild(createTextElement(documentRef, 'strong', '', banner.title));
      if (banner.subtitle) {
        copy.appendChild(createTextElement(documentRef, 'small', '', banner.subtitle));
      }
      card.appendChild(copy);
      fragment.appendChild(card);
    });
  }
  container.replaceChildren(fragment);
}

function renderReleaseStrip(documentRef, container, content) {
  if (!container) {
    return;
  }
  const activity = content.secondaryActivities[0];
  if (!activity) {
    container.classList.add('hidden');
    container.replaceChildren();
    return;
  }
  container.classList.remove('hidden');
  const items = content.timeline.items;
  const release = items.find((item) => item.status === 'active')
    || [...items].reverse().find((item) => item.status === 'done')
    || items.at(-1)
    || { id: 'release-current', title: '当前版本', date: '' };
  const card = documentRef.createElement(activity.link ? 'button' : 'article');
  if (activity.link) {
    card.type = 'button';
    card.dataset.linkUrl = activity.link;
  }
  card.className = 'home-release-card';
  card.classList.add('is-live-secondary');
  card.dataset.releaseId = activity.id;
  const image = documentRef.createElement('img');
  image.src = activity.imageUrl;
  image.alt = '';
  image.loading = 'lazy';
  image.draggable = false;
  image.addEventListener('error', () => card.classList.add('image-failed'), { once: true });
  card.append(
    image,
    createTextElement(documentRef, 'span', 'sr-only', activity.title || release.title || '版本活动')
  );
  container.replaceChildren(card);
}

function renderBulletins(documentRef, container, bulletins) {
  if (!container) {
    return;
  }
  container.dataset.activeCategory ||= '公告';
  const fragment = documentRef.createDocumentFragment();
  if (!bulletins.length) {
    fragment.appendChild(createTextElement(documentRef, 'li', 'home-content-empty', '暂无公告'));
  } else {
    for (const bulletin of bulletins) {
      const row = documentRef.createElement('li');
      row.dataset.bulletinId = bulletin.id;
      row.dataset.linkUrl = bulletin.link;
      row.dataset.category = bulletin.category;
      row.append(
        createTextElement(documentRef, 'em', '', bulletin.category),
        createTextElement(documentRef, 'span', '', bulletin.title),
        createTextElement(documentRef, 'time', '', bulletin.date)
      );
      fragment.appendChild(row);
    }
  }
  container.replaceChildren(fragment);
}

function renderTimeline(documentRef, container, progress, timeline) {
  if (!container) {
    return;
  }
  const fragment = documentRef.createDocumentFragment();
  if (!timeline.items.length) {
    const empty = documentRef.createElement('div');
    empty.className = 'timeline-item active';
    empty.append(
      documentRef.createElement('i'),
      createTextElement(documentRef, 'span', '', '等待版本内容')
    );
    fragment.appendChild(empty);
  } else {
    for (const item of timeline.items) {
      const node = documentRef.createElement('div');
      node.className = `timeline-item ${item.status}`;
      node.dataset.timelineId = item.id;
      const copy = createTextElement(documentRef, 'span', '', item.title);
      if (item.date) {
        copy.appendChild(createTextElement(documentRef, 'small', '', item.date));
      }
      node.append(documentRef.createElement('i'), copy);

      if (item.imageUrl || item.description) {
        node.className += ' has-popover';
        node.setAttribute('tabindex', '0');
        node.setAttribute('aria-label', `${item.title}：${item.description || '查看活动详情'}`);
        const popover = documentRef.createElement('span');
        popover.className = 'timeline-popover';
        if (item.link) {
          popover.dataset.linkUrl = item.link;
          popover.setAttribute('role', 'link');
        }
        if (item.imageUrl) {
          const image = documentRef.createElement('img');
          image.className = 'timeline-popover-image';
          image.src = item.imageUrl;
          image.alt = '';
          image.loading = 'lazy';
          image.draggable = false;
          image.addEventListener('error', () => popover.classList.add('image-failed'), { once: true });
          popover.appendChild(image);
        }
        popover.appendChild(createTextElement(
          documentRef,
          'span',
          'timeline-popover-text',
          item.description || item.title
        ));
        node.appendChild(popover);
      }
      fragment.appendChild(node);
    }
  }
  container.replaceChildren(fragment);
  container.style.setProperty('--timeline-columns', String(Math.max(1, timeline.items.length)));
  if (progress) {
    progress.style.width = `${timeline.progress}%`;
  }
}

const SERVER_STATUS_LABELS = Object.freeze({
  smooth: '流畅',
  busy: '繁忙',
  full: '爆满',
  maintenance: '维护'
});

function createServerOption(documentRef, server, selectedId, groupName) {
  const option = documentRef.createElement('label');
  const selected = server.id === selectedId;
  option.className = `server-option${selected ? ' is-selected' : ''} is-${server.status}`;
  option.dataset.serverId = server.id;

  const radio = documentRef.createElement('input');
  radio.type = 'radio';
  radio.name = groupName;
  radio.value = server.id;
  radio.checked = selected;
  radio.disabled = !server.available;

  option.append(
    radio,
    createTextElement(documentRef, 'span', 'server-option-name', server.label),
    createTextElement(documentRef, 'small', 'server-option-status', SERVER_STATUS_LABELS[server.status])
  );
  return option;
}

function replaceServerOptions(documentRef, container, servers, selectedId, groupName) {
  if (!container) {
    return;
  }
  const fragment = documentRef.createDocumentFragment();
  for (const server of servers) {
    fragment.appendChild(createServerOption(documentRef, server, selectedId, groupName));
  }
  container.replaceChildren(fragment);
}

function renderServers(documentRef, elements, servers) {
  const { select, label, recentSection, recentList, list } = elements;
  if (!select) {
    return;
  }
  const previous = select.value;
  const selectedServer = servers.find((server) => server.available && server.id === previous)
    || servers.find((server) => server.available && server.selected)
    || servers.find((server) => server.available)
    || servers[0];
  const selectedId = selectedServer?.id || '';
  const fragment = documentRef.createDocumentFragment();
  for (const server of servers) {
    const option = documentRef.createElement('option');
    option.value = server.id;
    option.textContent = server.available ? server.label : `${server.label}（维护）`;
    option.disabled = !server.available;
    option.selected = server.id === selectedId;
    fragment.appendChild(option);
  }
  select.replaceChildren(fragment);
  select.value = selectedId;
  if (label) {
    label.textContent = selectedServer?.label || '暂无可用大区';
  }

  const recentServers = servers.filter((server) => server.selected);
  if (recentSection) {
    recentSection.classList.toggle('hidden', recentServers.length === 0);
  }
  replaceServerOptions(documentRef, recentList, recentServers, selectedId, 'server-recent');
  replaceServerOptions(documentRef, list, servers, selectedId, 'server-selection');
}

export function renderLauncherContent(documentRef, value) {
  const content = normalizeLauncherContent(value);
  const title = documentRef.getElementById('homeHeroTitle');
  const subtitle = documentRef.getElementById('homeHeroSubtitle');
  if (title) title.textContent = content.hero.title;
  if (subtitle) subtitle.textContent = content.hero.subtitle;

  renderBanners(documentRef, documentRef.getElementById('homeBannerTrack'), content.banners);
  renderReleaseStrip(documentRef, documentRef.getElementById('homeSecondaryBanner'), content);
  renderBulletins(documentRef, documentRef.getElementById('homeBulletinList'), content.bulletins);
  renderTimeline(
    documentRef,
    documentRef.getElementById('releaseTimeline'),
    documentRef.getElementById('timelineProgressValue'),
    content.timeline
  );
  renderServers(documentRef, {
    select: documentRef.getElementById('serverSelect'),
    label: documentRef.getElementById('serverSelectLabel'),
    recentSection: documentRef.getElementById('serverRecentSection'),
    recentList: documentRef.getElementById('serverRecentList'),
    list: documentRef.getElementById('serverList')
  }, content.servers);
  const ticker = documentRef.getElementById('homeTickerText');
  if (ticker) {
    ticker.textContent = content.bulletins[0]?.title
      || content.hero.subtitle
      || '活动与公告暂不可用';
  }
  return content;
}
