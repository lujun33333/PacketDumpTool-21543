import { createUpdateProgressView } from './update-progress.js';

const title = document.querySelector('[data-update-title]');
const detail = document.querySelector('[data-update-detail]');
const progressTrack = document.querySelector('[data-update-progress]');
const progressFill = document.querySelector('[data-update-progress-fill]');
const percent = document.querySelector('[data-update-percent]');

function renderProgress(progress) {
  const view = createUpdateProgressView(progress);
  title.textContent = view.title;
  detail.textContent = view.detail;
  percent.textContent = view.percentLabel;
  progressTrack.classList.toggle('is-indeterminate', !view.determinate);
  progressFill.style.width = view.determinate ? `${view.percent}%` : '';
  progressTrack.setAttribute('aria-valuetext', view.percentLabel);
  if (view.determinate) {
    progressTrack.setAttribute('aria-valuenow', String(view.percent));
  } else {
    progressTrack.removeAttribute('aria-valuenow');
  }
}

const unsubscribe = window.launcherUpdate?.onProgress?.(renderProgress);
window.addEventListener('beforeunload', () => unsubscribe?.(), { once: true });
