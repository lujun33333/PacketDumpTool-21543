import {
  initializeStartupLog,
  logStartupEvent,
  serializeStartupError
} from './startup-log.js';

initializeStartupLog();

let fatalExitScheduled = false;

function scheduleFatalExit(event, error) {
  logStartupEvent('bootstrap', event, serializeStartupError(error));
  if (fatalExitScheduled) {
    return;
  }
  fatalExitScheduled = true;
  process.exitCode = 1;
  setImmediate(() => process.exit(1));
}

process.once('uncaughtException', (error) => {
  scheduleFatalExit('UNCAUGHT_EXCEPTION', error);
});

process.once('unhandledRejection', (reason) => {
  scheduleFatalExit('UNHANDLED_REJECTION', reason);
});

process.on('exit', (code) => {
  logStartupEvent('bootstrap', 'APP_PROCESS_EXIT', { exitCode: Number(code) || 0 });
});

logStartupEvent('bootstrap', 'MAIN_IMPORT_BEGIN');
try {
  await import('./index.js');
  logStartupEvent('bootstrap', 'MAIN_IMPORT_OK');
} catch (error) {
  scheduleFatalExit('MAIN_IMPORT_FAILED', error);
}
