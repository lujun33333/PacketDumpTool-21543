import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';

let ipcTrust = null;
try {
  ipcTrust = await import('../main/ipc-trust.js');
} catch {
  // The first TDD run intentionally reaches this branch before the helper exists.
}

assert.equal(
  typeof ipcTrust?.isTrustedIpcSender,
  'function',
  'IPC trust helper must exist before privileged handlers can use normalized frame checks.'
);

const rendererEntry = 'C:\\Users\\Test User\\AppData\\Local\\Temp\\QQSpeed\\resources\\app.asar\\src\\renderer\\index.html';
const rendererUrl = pathToFileURL(rendererEntry).href.replace('/C:/', '/c:/') + '#restored';
const webContents = {
  isDestroyed: () => false,
  mainFrame: {
    processId: 700,
    routingId: 11,
    frameTreeNodeId: 91,
    url: rendererUrl
  }
};
const mainWindow = {
  isDestroyed: () => false,
  webContents
};
const event = {
  sender: webContents,
  // Deliberately use a different JS wrapper for the same underlying Electron frame.
  senderFrame: {
    processId: 700,
    routingId: 11,
    frameTreeNodeId: 91,
    url: rendererUrl
  }
};

assert.equal(
  ipcTrust.isOwnedMainFrameSender(event, mainWindow),
  true,
  'Distinct WebFrameMain wrappers with the same process/routing IDs must be accepted.'
);
assert.equal(
  ipcTrust.isTrustedIpcSender(event, mainWindow, rendererEntry),
  true,
  'The trusted renderer must survive Windows drive-letter case, URL encoding and hash differences.'
);

const remoteEvent = {
  ...event,
  senderFrame: { ...event.senderFrame, url: 'https://example.invalid/launcher' }
};
assert.equal(
  ipcTrust.isTrustedIpcSender(remoteEvent, mainWindow, rendererEntry),
  false,
  'A remote document in the owned window must not invoke privileged launcher IPC.'
);
assert.equal(
  ipcTrust.isWindowControlSender(remoteEvent, mainWindow),
  true,
  'The owned main frame must retain the emergency close/minimize path even when document validation fails.'
);

const childFrameEvent = {
  ...event,
  senderFrame: { ...event.senderFrame, routingId: 12, frameTreeNodeId: 92 }
};
assert.equal(ipcTrust.isOwnedMainFrameSender(childFrameEvent, mainWindow), false, 'Child frames must be rejected.');

const foreignContentsEvent = {
  ...event,
  sender: { ...webContents }
};
assert.equal(ipcTrust.isWindowControlSender(foreignContentsEvent, mainWindow), false, 'Other windows must be rejected.');

const queryEvent = {
  ...event,
  senderFrame: { ...event.senderFrame, url: `${rendererUrl.split('#')[0]}?unexpected=1` }
};
assert.equal(
  ipcTrust.isTrustedIpcSender(queryEvent, mainWindow, rendererEntry),
  false,
  'Unexpected query parameters must not widen the privileged renderer document.'
);

console.log('IPC_TRUST_SMOKE_OK');
