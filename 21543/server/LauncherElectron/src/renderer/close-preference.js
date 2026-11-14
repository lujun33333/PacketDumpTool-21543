export const CLOSE_ACTION_TRAY = 'tray';
export const CLOSE_ACTION_QUIT = 'quit';

const CLOSE_PREFERENCE_STORAGE_KEY = 'qqspeed.launcher.closePreference.v1';
const CLOSE_PREFERENCE_VERSION = 1;

function defaultClosePreference() {
  return {
    action: CLOSE_ACTION_TRAY,
    skipPrompt: false
  };
}

function isSupportedCloseAction(action) {
  return action === CLOSE_ACTION_TRAY || action === CLOSE_ACTION_QUIT;
}

export function readClosePreference(storage) {
  if (!storage || typeof storage.getItem !== 'function') {
    return defaultClosePreference();
  }

  try {
    const saved = JSON.parse(storage.getItem(CLOSE_PREFERENCE_STORAGE_KEY));
    if (
      saved?.version !== CLOSE_PREFERENCE_VERSION
      || saved?.skipPrompt !== true
      || !isSupportedCloseAction(saved?.action)
    ) {
      return defaultClosePreference();
    }
    return {
      action: saved.action,
      skipPrompt: true
    };
  } catch {
    return defaultClosePreference();
  }
}

export function saveClosePreference(storage, action) {
  if (!isSupportedCloseAction(action)) {
    throw new Error('Unsupported close action.');
  }

  const preference = {
    action,
    skipPrompt: true
  };
  try {
    storage?.setItem?.(CLOSE_PREFERENCE_STORAGE_KEY, JSON.stringify({
      version: CLOSE_PREFERENCE_VERSION,
      ...preference
    }));
  } catch {
    // The close action still works when browser storage is unavailable.
  }
  return preference;
}

export function resetClosePreference(storage) {
  try {
    storage?.removeItem?.(CLOSE_PREFERENCE_STORAGE_KEY);
  } catch {
    // An unavailable storage backend already behaves like a reset preference.
  }
}
