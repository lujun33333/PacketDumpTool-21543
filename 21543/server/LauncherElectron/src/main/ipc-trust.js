import { normalize, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

function isDestroyed(value) {
  try {
    return typeof value?.isDestroyed === 'function' && value.isDestroyed();
  } catch {
    return true;
  }
}

function normalizeComparablePath(value) {
  let result = normalize(resolve(String(value || '')));
  if (process.platform === 'win32') {
    result = result.toLowerCase();
  }
  return result;
}

function sameFrameIdentity(left, right) {
  if (!left || !right || isDestroyed(left) || isDestroyed(right) || left.detached === true || right.detached === true) {
    return false;
  }

  const sameProcessAndRoute = Number.isInteger(left.processId) &&
    Number.isInteger(left.routingId) &&
    left.processId === right.processId &&
    left.routingId === right.routingId;
  if (!sameProcessAndRoute) {
    return false;
  }

  if (Number.isInteger(left.frameTreeNodeId) && Number.isInteger(right.frameTreeNodeId)) {
    return left.frameTreeNodeId === right.frameTreeNodeId;
  }
  return true;
}

function rendererDocumentMatches(frameUrl, rendererEntry) {
  try {
    const parsed = new URL(String(frameUrl || ''));
    if (parsed.protocol !== 'file:' || parsed.host || parsed.search) {
      return false;
    }
    return normalizeComparablePath(fileURLToPath(parsed)) === normalizeComparablePath(rendererEntry);
  } catch {
    return false;
  }
}

export function inspectIpcSender(event, mainWindow, rendererEntry) {
  const windowReady = Boolean(mainWindow) && !isDestroyed(mainWindow);
  const webContents = windowReady ? mainWindow.webContents : null;
  const contentsReady = Boolean(webContents) && !isDestroyed(webContents);
  const sameWebContents = contentsReady && event?.sender === webContents;
  const senderFrame = event?.senderFrame || null;
  const mainFrame = contentsReady ? webContents.mainFrame : null;
  const senderFrameReady = Boolean(senderFrame) && !isDestroyed(senderFrame) && senderFrame.detached !== true;
  const mainFrameReady = Boolean(mainFrame) && !isDestroyed(mainFrame) && mainFrame.detached !== true;
  const sameMainFrame = senderFrameReady && mainFrameReady && sameFrameIdentity(senderFrame, mainFrame);
  const trustedDocument = sameMainFrame && rendererDocumentMatches(senderFrame.url, rendererEntry);

  let senderProtocol = '';
  try {
    senderProtocol = new URL(String(senderFrame?.url || '')).protocol;
  } catch {
    // Keep diagnostics path-free when the sender URL cannot be parsed.
  }

  return {
    windowReady,
    contentsReady,
    sameWebContents,
    senderFrameReady,
    mainFrameReady,
    sameMainFrame,
    trustedDocument,
    senderProtocol,
    ownedMainFrame: sameWebContents && sameMainFrame,
    windowControl: sameWebContents,
    trusted: sameWebContents && sameMainFrame && trustedDocument
  };
}

export function isOwnedMainFrameSender(event, mainWindow) {
  return inspectIpcSender(event, mainWindow, '').ownedMainFrame;
}

export function isWindowControlSender(event, mainWindow) {
  return inspectIpcSender(event, mainWindow, '').windowControl;
}

export function isTrustedIpcSender(event, mainWindow, rendererEntry) {
  return inspectIpcSender(event, mainWindow, rendererEntry).trusted;
}
