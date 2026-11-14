const DEFAULT_POLL_INTERVAL_MS = 1500;
const DEFAULT_MISSING_THRESHOLD = 2;

export function isProcessAlive(pid, signalProcess = process.kill.bind(process)) {
  if (!Number.isSafeInteger(pid) || pid <= 0) {
    return false;
  }
  try {
    signalProcess(pid, 0);
    return true;
  } catch (error) {
    // Access denied still proves the PID exists. ESRCH and other failures mean it is gone.
    return error?.code === 'EPERM' || error?.code === 'EACCES';
  }
}

export function watchProcessByPid(pid, onExit, options = {}) {
  if (!Number.isSafeInteger(pid) || pid <= 0) {
    throw new TypeError('游戏进程 PID 无效。');
  }
  if (typeof onExit !== 'function') {
    throw new TypeError('缺少游戏进程退出回调。');
  }

  const pollIntervalMs = Math.max(250, Number(options.pollIntervalMs) || DEFAULT_POLL_INTERVAL_MS);
  const missingThreshold = Math.max(1, Math.floor(Number(options.missingThreshold) || DEFAULT_MISSING_THRESHOLD));
  const checkAlive = typeof options.isAlive === 'function' ? options.isAlive : isProcessAlive;
  let stopped = false;
  let missingCount = 0;
  let timer = null;

  const stop = () => {
    if (stopped) {
      return;
    }
    stopped = true;
    if (timer) {
      clearInterval(timer);
      timer = null;
    }
  };

  const check = () => {
    if (stopped) {
      return false;
    }
    const alive = Boolean(checkAlive(pid));
    missingCount = alive ? 0 : missingCount + 1;
    if (missingCount >= missingThreshold) {
      stop();
      onExit(pid);
      return false;
    }
    return true;
  };

  timer = setInterval(check, pollIntervalMs);
  timer.unref?.();

  return {
    pid,
    check,
    stop,
    get stopped() {
      return stopped;
    }
  };
}
