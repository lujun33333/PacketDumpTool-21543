function finiteNonNegative(value) {
  const number = Number(value);
  return Number.isFinite(number) && number > 0 ? number : 0;
}

function formatBytes(value) {
  const bytes = finiteNonNegative(value);
  if (bytes >= 1024 * 1024) {
    return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  }
  if (bytes >= 1024) {
    return `${(bytes / 1024).toFixed(1)} KB`;
  }
  return `${Math.round(bytes)} B`;
}

function fixedState(title, detail) {
  return {
    title,
    detail,
    percentLabel: '100%',
    percent: 100,
    determinate: true
  };
}

export function createUpdateProgressView(progress = {}) {
  const phase = String(progress.phase || 'checking');
  if (phase === 'downloading') {
    const downloadedBytes = finiteNonNegative(progress.downloadedBytes);
    const totalBytes = finiteNonNegative(progress.totalBytes);
    const bytesPerSecond = finiteNonNegative(progress.bytesPerSecond);
    if (totalBytes > 0) {
      const percent = Math.max(0, Math.min(
        100,
        Math.round((downloadedBytes / totalBytes) * 100)
      ));
      return {
        title: '正在下载登录器更新',
        detail: `${formatBytes(downloadedBytes)} / ${formatBytes(totalBytes)}${
          bytesPerSecond > 0 ? ` · ${formatBytes(bytesPerSecond)}/s` : ''
        }`,
        percentLabel: `${percent}%`,
        percent,
        determinate: true
      };
    }
    return {
      title: '正在下载登录器更新',
      detail: '已发现新版本，正在连接下载服务器。',
      percentLabel: '准备中',
      percent: 0,
      determinate: false
    };
  }

  if (phase === 'verifying') {
    return fixedState('正在校验更新文件', '下载完成，正在验证文件完整性。');
  }
  if (phase === 'installing') {
    return fixedState('正在启动更新安装程序', '校验通过，即将自动安装并重新打开登录器。');
  }
  return {
    title: '正在检查登录器更新',
    detail: '正在连接更新服务器，请稍候。',
    percentLabel: '检查中',
    percent: 0,
    determinate: false
  };
}
