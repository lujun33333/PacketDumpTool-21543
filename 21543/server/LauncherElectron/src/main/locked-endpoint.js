export const LOCKED_LOGIN_ENDPOINT = Object.freeze({
  host: '127.0.0.1',
  port: 28880,
  // Must match ASpeedConfig.yml GameConfig.passEncryp on the private server.
  // "md5" means the historical Launcher/Server wire-compatible digest, not RFC MD5.
  // This is launcher service configuration, not a client-side INI dependency.
  passwordMode: 'md5'
});
