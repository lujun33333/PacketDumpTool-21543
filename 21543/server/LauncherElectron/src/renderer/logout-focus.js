export function focusLoginUsernameIfVisible({ shell, loginView, username } = {}) {
  const canFocus = (
    shell?.dataset?.view === 'login'
    && !loginView?.classList?.contains('hidden')
    && username?.hidden !== true
    && username?.disabled !== true
    && username?.isConnected !== false
    && username?.getAttribute?.('aria-hidden') !== 'true'
    && typeof username?.focus === 'function'
  );
  if (!canFocus) {
    return false;
  }

  try {
    username.focus({ preventScroll: true });
    return true;
  } catch {
    return false;
  }
}
