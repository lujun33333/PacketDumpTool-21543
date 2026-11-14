const AUTHENTICATED_PRELOAD_FALLBACK_CODES = new Set([
  'NETWORK_ERROR',
  'TIMEOUT',
  'RUNTIME_MATERIAL_UNSUPPORTED',
  'RUNTIME_MATERIAL_UNAVAILABLE'
]);

export function shouldUseAuthenticatedPreloadFallback(error) {
  return AUTHENTICATED_PRELOAD_FALLBACK_CODES.has(String(error?.code || ''));
}
