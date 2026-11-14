import assert from 'node:assert/strict';
import test from 'node:test';

import {
  CLOSE_ACTION_QUIT,
  CLOSE_ACTION_TRAY,
  readClosePreference,
  resetClosePreference,
  saveClosePreference
} from '../../src/renderer/close-preference.js';

function createStorage(initialValue = null) {
  let value = initialValue;
  return {
    get value() {
      return value;
    },
    getItem() {
      return value;
    },
    setItem(_key, nextValue) {
      value = String(nextValue);
    },
    removeItem() {
      value = null;
    }
  };
}

test('missing or unavailable storage asks again and defaults to the tray action', () => {
  assert.deepEqual(readClosePreference(createStorage()), {
    action: CLOSE_ACTION_TRAY,
    skipPrompt: false
  });
  assert.deepEqual(readClosePreference(null), {
    action: CLOSE_ACTION_TRAY,
    skipPrompt: false
  });
});

test('corrupt, stale, or unknown saved values fail closed to asking again', () => {
  for (const value of [
    '{',
    JSON.stringify({ version: 2, skipPrompt: true, action: CLOSE_ACTION_QUIT }),
    JSON.stringify({ version: 1, skipPrompt: true, action: 'destroy' }),
    JSON.stringify({ version: 1, skipPrompt: false, action: CLOSE_ACTION_QUIT })
  ]) {
    assert.deepEqual(readClosePreference(createStorage(value)), {
      action: CLOSE_ACTION_TRAY,
      skipPrompt: false
    });
  }
});

test('confirming remember persists only a supported close action', () => {
  const storage = createStorage();

  assert.deepEqual(saveClosePreference(storage, CLOSE_ACTION_QUIT), {
    action: CLOSE_ACTION_QUIT,
    skipPrompt: true
  });
  assert.deepEqual(readClosePreference(storage), {
    action: CLOSE_ACTION_QUIT,
    skipPrompt: true
  });
  assert.throws(() => saveClosePreference(storage, 'destroy'), /Unsupported close action/);
});

test('reset removes the remembered choice and restores the official default', () => {
  const storage = createStorage();
  saveClosePreference(storage, CLOSE_ACTION_QUIT);

  resetClosePreference(storage);

  assert.equal(storage.value, null);
  assert.deepEqual(readClosePreference(storage), {
    action: CLOSE_ACTION_TRAY,
    skipPrompt: false
  });
});

