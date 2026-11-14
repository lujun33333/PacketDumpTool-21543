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
