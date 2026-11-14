export const CUSTOMER_MEMORY_ERROR = '游戏内存出现问题，请重新启动登录器后重试。';
export const CUSTOMER_LAUNCHER_ERROR = '登录器运行出现问题，请重新启动后重试。';
export const CUSTOMER_CREDENTIAL_ERROR = '已保存的密码暂时无法使用，请重新输入密码。';

const REMOTE_METHOD_PREFIX = /^(?:error:\s*)?error invoking remote method\s+['"][^'"]+['"]:\s*/i;
const MEMORY_IMPLEMENTATION_PATTERN = /hook|nativebridge|manual[- ]?map|\bdll\b|\bpe\s+(?:image|file)\b|in-memory\s+(?:hook|payload)|memory\s+(?:mapping|payload|hook)|inject(?:ion|ed)?|原生\s*hook|内存\s*(?:hook|映射|载荷)|(?:启动)?桥接|组件包身份校验|注入/i;
const CREDENTIAL_IMPLEMENTATION_PATTERN = /\bdpapi\b|credential(?:\s+vault)?|密码保险库|凭据(?:仓库|进程)|加密副本/i;
const LAUNCHER_IMPLEMENTATION_PATTERN = /\belectron\b|\bipc\b|\bnative\s+runtime\b|remote method|utility process|\bpid\b|\bx64\b|主进程|渲染进程|工作进程|原生命令|原生启动|启动组件|内嵌组件包|本机组件包|安装密钥|进程身份/i;

function readMessage(value) {
  if (typeof value === 'string') return value;
  if (value && typeof value.message === 'string') return value.message;
  if (value === null || value === undefined) return '';
  return String(value);
}

export function toCustomerErrorMessage(value, fallback = CUSTOMER_LAUNCHER_ERROR) {
  let message = readMessage(value).trim();
  const hadRemoteMethodPrefix = REMOTE_METHOD_PREFIX.test(message);
  message = message
    .replace(REMOTE_METHOD_PREFIX, '')
    .replace(/^error\s*:?\s*/i, '')
    .trim();

  if (!message) {
    return hadRemoteMethodPrefix ? CUSTOMER_LAUNCHER_ERROR : fallback;
  }
  if (MEMORY_IMPLEMENTATION_PATTERN.test(message)) {
    return CUSTOMER_MEMORY_ERROR;
  }
  if (CREDENTIAL_IMPLEMENTATION_PATTERN.test(message)) {
    return CUSTOMER_CREDENTIAL_ERROR;
  }
  if (LAUNCHER_IMPLEMENTATION_PATTERN.test(message)) {
    return CUSTOMER_LAUNCHER_ERROR;
  }
  return message;
}

export function toCustomerStatusMessage(value, type = 'info', fallback = '') {
  const normalizedType = String(type || 'info').trim().toLowerCase();
  if (normalizedType === 'error' || normalizedType === 'warn') {
    return toCustomerErrorMessage(value, fallback);
  }
  return readMessage(value).trim() || fallback;
}
