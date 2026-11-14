export function createGameLoadingVisibilityController({ onHide, onRestore, isQuitting }) {
  if (typeof onHide !== 'function' || typeof onRestore !== 'function' || typeof isQuitting !== 'function') {
    throw new TypeError('Game loading visibility callbacks are required.');
  }

  const attempts = new Map();
  let sequence = 0;

  return {
    begin(window) {
      const attempt = Object.freeze({ id: ++sequence });
      attempts.set(attempt, 'pending');
      try {
        onHide(window);
      } catch (error) {
        attempts.delete(attempt);
        throw error;
      }
      return attempt;
    },

    activate(attempt) {
      if (!attempts.has(attempt)) return false;
      attempts.set(attempt, 'active');
      return true;
    },

    finish(attempt) {
      if (!attempts.delete(attempt)) return false;
      if (attempts.size === 0 && !isQuitting()) {
        onRestore();
      }
      return true;
    },

    get size() {
      return attempts.size;
    }
  };
}
