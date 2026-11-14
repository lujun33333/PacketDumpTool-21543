import assert from 'node:assert/strict';
import test from 'node:test';

import { focusLoginUsernameIfVisible } from '../../src/renderer/logout-focus.js';

function createFixture({
  view = 'login',
  loginHidden = false,
  usernameHidden = false,
  usernameDisabled = false,
  focusThrows = false
} = {}) {
  let focusCalls = 0;
  const fixture = {
    shell: { dataset: { view } },
    loginView: {
      classList: {
        contains(value) {
          return value === 'hidden' && loginHidden;
        }
      }
    },
    username: {
      hidden: usernameHidden,
      disabled: usernameDisabled,
      getAttribute() {
        return null;
      },
      focus(options) {
        focusCalls += 1;
        assert.deepEqual(options, { preventScroll: true });
        if (focusThrows) throw new Error('focus failed');
      }
    }
  };
  return { fixture, getFocusCalls: () => focusCalls };
}

test('logout focus policy focuses the visible login username without scrolling', () => {
  const { fixture, getFocusCalls } = createFixture();
  assert.equal(focusLoginUsernameIfVisible(fixture), true);
  assert.equal(getFocusCalls(), 1);
});

test('logout focus policy skips hidden, disabled, disconnected, or non-login targets', () => {
  for (const options of [
    { view: 'home' },
    { loginHidden: true },
    { usernameHidden: true },
    { usernameDisabled: true }
  ]) {
    const { fixture, getFocusCalls } = createFixture(options);
    assert.equal(focusLoginUsernameIfVisible(fixture), false);
    assert.equal(getFocusCalls(), 0);
  }

  const { fixture, getFocusCalls } = createFixture();
  fixture.username.isConnected = false;
  assert.equal(focusLoginUsernameIfVisible(fixture), false);
  assert.equal(getFocusCalls(), 0);
});

test('logout focus policy contains a platform focus failure', () => {
  const { fixture, getFocusCalls } = createFixture({ focusThrows: true });
  assert.equal(focusLoginUsernameIfVisible(fixture), false);
  assert.equal(getFocusCalls(), 1);
});
