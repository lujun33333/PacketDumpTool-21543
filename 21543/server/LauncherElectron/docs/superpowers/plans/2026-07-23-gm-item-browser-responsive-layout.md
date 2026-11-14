# GM Item Browser and Responsive Launcher Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an authenticated, read-only GM item image browser with linked primary/secondary categories and make the Electron launcher responsive from `1024×640` upward.

**Architecture:** Keep SQLite ownership in the Electron main process behind two authenticated IPC handlers. Expose only catalog/query functions through preload, render the GM modal in the existing renderer, and isolate testable URL/grid helpers in a shared module. Package the SQLite file as an unpacked read-only resource and replace fixed renderer geometry with CSS reflow rather than whole-page zoom.

**Tech Stack:** Electron 35, Node.js ESM/CommonJS preload, `node:sqlite`, HTML/CSS/vanilla JavaScript, Node test runner, electron-builder.

## Global Constraints

- GM access is available only after successful login and closes immediately on logout.
- Primary category is the broad class; secondary options are generated only from the selected primary category.
- Default item layout is 10 columns × 3 rows; narrower result widths use 8 × 3 or 6 × 3.
- The primary category rail contains text only, with no category icons.
- Sorting supports `ID 从小到大` and `ID 从大到小`; default is ascending.
- Item images use only `https://iips.speed.qq.com/images/{positive-integer-ID}.png` with a local failure state.
- BrowserWindow defaults to `1280×720`, has minimum `1024×640`, and supports resize/maximize.
- SQLite remains read-only at runtime; no account, character, or item-grant write operation is added.
- Existing unrelated working-tree changes must not be reverted or committed accidentally.

---

### Task 1: Read-only GM query service

**Files:**
- Create: `src/main/gm-item-query-service.js`
- Create: `test/unit/gm-item-query-service.test.js`
- Reuse: `src/shared/gm-item-database.js`

**Interfaces:**
- Produces: `createGmItemQueryService({ databasePath })`.
- Produces service methods: `getCatalog(primaryCategory = '')`, `queryItems(input)`, and `close()`.
- `getCatalog` returns `{ categories, subcategories, genders }`; each category entry is `{ value, label, count }`.
- `queryItems` accepts `{ primaryCategory, secondaryCategory, gender, keyword, sortDirection, page, pageSize }` and returns `{ items, total, page, pageSize, pageCount }`.

- [ ] **Step 1: Write the failing query-service tests**

Create a temporary SQLite fixture with A/S racers, a racer skin, suit/top/hair clothing, and a pet. Assert:

```js
const service = createGmItemQueryService({ databasePath });
assert.deepEqual(
  service.getCatalog('赛车').subcategories.map((entry) => entry.value),
  ['A级赛车', 'S级赛车']
);
assert.deepEqual(
  service.queryItems({
    primaryCategory: '赛车',
    secondaryCategory: 'S级赛车',
    sortDirection: 'desc',
    page: 1,
    pageSize: 30
  }).items.map((item) => item.id),
  [200]
);
assert.deepEqual(
  service.getCatalog('服饰').subcategories.map((entry) => entry.value),
  ['上装', '发饰', '套装']
);
```

Also assert that racer parent results exclude `赛车皮肤`, `asc/desc` change ID ordering, pure numeric keywords match exact ID, names use substring matching, and invalid category/subcategory/page size/sort values throw stable validation errors.

- [ ] **Step 2: Run the test and verify RED**

Run:

```powershell
node --test test/unit/gm-item-query-service.test.js
```

Expected: fail with `ERR_MODULE_NOT_FOUND` for `src/main/gm-item-query-service.js`.

- [ ] **Step 3: Implement the minimal query service**

Implement a read-only `DatabaseSync` connection and parameterized SQL. Use exact allowlists:

```js
const ALLOWED_PAGE_SIZES = new Set([18, 24, 30]);
const SORT_SQL = Object.freeze({ asc: 'ASC', desc: 'DESC' });
```

Build `WHERE` clauses only from fixed SQL fragments and positional parameters. Validate a secondary category with:

```sql
SELECT 1
FROM items
WHERE category = ? AND subcategory = ?
LIMIT 1
```

Query rows with:

```sql
SELECT id, name, category, subcategory, gender
FROM items
${whereSql}
ORDER BY id ${sortSql}
LIMIT ? OFFSET ?
```

The only interpolated SQL token is the `ASC`/`DESC` value selected from `SORT_SQL`.

- [ ] **Step 4: Run the query-service tests and verify GREEN**

Run the command from Step 2. Expected: all query-service tests pass; the only acceptable runtime notice is Node's known experimental `node:sqlite` warning.

### Task 2: Authenticated IPC, preload contract, and packaged database

**Files:**
- Modify: `src/main/index.js`
- Modify: `src/preload/preload.cjs`
- Modify: `package.json`
- Modify: `test/unit/runtime-packaging-boundary.test.js`
- Modify: `test/unit/embedded-native-packaging.test.js`
- Create: `test/unit/gm-item-ipc-integration.test.js`

**Interfaces:**
- Consumes: `createGmItemQueryService` from Task 1.
- Produces preload functions: `getGmCatalog(primaryCategory)` and `queryGmItems(query)`.
- Produces IPC channels: `launcher:get-gm-catalog` and `launcher:query-gm-items`.

- [ ] **Step 1: Write failing integration and packaging tests**

Assert main contains both handlers, each calls existing `assertTrustedSender` and an explicit authenticated-session guard before opening/querying SQLite. Assert preload exposes only named invocations:

```js
getGmCatalog: (primaryCategory = '') =>
  ipcRenderer.invoke('launcher:get-gm-catalog', { primaryCategory }),
queryGmItems: (query) => ipcRenderer.invoke('launcher:query-gm-items', query)
```

Update packaging expectations so `extraResources` contains only the GM database mapping while encrypted native runtime artifacts remain excluded:

```json
{
  "from": "resources/gm-items.sqlite3",
  "to": "gm-items.sqlite3"
}
```

- [ ] **Step 2: Run the tests and verify RED**

Run:

```powershell
node --test test/unit/gm-item-ipc-integration.test.js test/unit/runtime-packaging-boundary.test.js test/unit/embedded-native-packaging.test.js
```

Expected: fail because the GM IPC channels, preload functions, and packaged resource mapping are absent.

- [ ] **Step 3: Implement main-process ownership and authorization**

Add a lazy service getter that chooses:

```js
const databasePath = app.isPackaged
  ? join(process.resourcesPath, 'gm-items.sqlite3')
  : join(appRoot, 'resources', 'gm-items.sqlite3');
```

Before each GM operation enforce:

```js
if (!launcherAuthenticatedUser) {
  throw new Error('请先登录后使用 GM 道具查询。');
}
```

Call the sender trust check first, then the authentication check, then the service. Close the service from the existing `before-quit` handler. Do not expose the database path to preload or renderer.

- [ ] **Step 4: Add the minimal preload bridge and package mapping**

Add the two named methods and the single `extraResources` entry. Extend `npm run check` with `node --check src/main/gm-item-query-service.js` and later shared/renderer GM modules.

- [ ] **Step 5: Run the Task 2 tests and verify GREEN**

Run the command from Step 2. Expected: all selected tests pass and native runtime bundle exclusion assertions remain green.

### Task 3: Pure renderer helpers and GM modal behavior

**Files:**
- Create: `src/renderer/gm-item-browser.js`
- Create: `test/unit/gm-item-browser.test.js`
- Modify: `src/renderer/index.html`
- Modify: `src/renderer/app.js`
- Modify: `src/renderer/styles.css`
- Create: `test/unit/gm-item-browser-integration.test.js`

**Interfaces:**
- Produces: `resolveGmGridLayout(resultWidth)` returning `{ columns, pageSize }`.
- Produces: `createGmItemImageUrl(id)` returning the fixed HTTPS URL or `''`.
- Produces: `formatGmSubcategoryLabel(value)` mapping `发饰` to `发饰/头发`.
- Consumes preload methods from Task 2.

- [ ] **Step 1: Write failing helper tests**

Assert exact boundaries and URL safety:

```js
assert.deepEqual(resolveGmGridLayout(1000), { columns: 10, pageSize: 30 });
assert.deepEqual(resolveGmGridLayout(999), { columns: 8, pageSize: 24 });
assert.deepEqual(resolveGmGridLayout(719), { columns: 6, pageSize: 18 });
assert.equal(createGmItemImageUrl(10024), 'https://iips.speed.qq.com/images/10024.png');
assert.equal(createGmItemImageUrl('../x'), '');
assert.equal(formatGmSubcategoryLabel('发饰'), '发饰/头发');
```

- [ ] **Step 2: Run helper tests and verify RED**

Run `node --test test/unit/gm-item-browser.test.js`. Expected: module-not-found failure.

- [ ] **Step 3: Implement pure helpers and verify GREEN**

Implement only the tested conversions, run the Step 2 command, and expect all helper tests to pass.

- [ ] **Step 4: Write failing renderer integration tests**

Assert HTML contains:

- authenticated menu item `gmItemBrowserMenuBtn`;
- dialog `gmItemBrowserModal`;
- text-only primary category list;
- keyword, secondary category, gender and ID order controls;
- results grid and pagination;
- no GM category icon elements.

Assert app behavior source closes the modal from `onUserLogout`, rebuilds secondary options after primary selection, resets page on filters/sort/layout changes, and uses `api.getGmCatalog`/`api.queryGmItems`.

- [ ] **Step 5: Run renderer integration tests and verify RED**

Run:

```powershell
node --test test/unit/gm-item-browser-integration.test.js
```

Expected: fail because the menu item and modal do not exist.

- [ ] **Step 6: Add the GM HTML and CSP image source**

Add the menu item and dialog markup. Extend only `img-src` with `https://iips.speed.qq.com`; do not change `script-src`, `connect-src`, `frame-src`, or Electron web preferences.

- [ ] **Step 7: Implement renderer state and event flow**

Maintain one local state object:

```js
const gmBrowserState = {
  open: false,
  primaryCategory: '',
  secondaryCategory: '',
  gender: '',
  keyword: '',
  sortDirection: 'asc',
  page: 1,
  pageSize: 30,
  total: 0,
  pageCount: 0,
  requestSequence: 0
};
```

Open only when `currentState?.session?.authenticated === true` or the authoritative launcher state is authenticated. Query catalog, render text categories, render linked secondary options, and discard stale asynchronous responses using `requestSequence`. Render item fields only through `textContent`. Build image `src` only with `createGmItemImageUrl`; on the first `error`, clear the URL and add a local CSS placeholder class.

Use `ResizeObserver` on the grid shell to recalculate 10/8/6 columns. If `pageSize` changes, reset `page = 1` and requery. Close and reset on logout.

- [ ] **Step 8: Implement the approved 3-row visual layout**

Use CSS Grid with `repeat(var(--gm-columns), minmax(0, 1fr))`, text-only category buttons, three equal card rows, fixed modal header/filter/footer, and an internally bounded results region. Cards contain image, one-line name, ID, and `复制ID`.

- [ ] **Step 9: Run Task 3 tests and verify GREEN**

Run:

```powershell
node --test test/unit/gm-item-browser.test.js test/unit/gm-item-browser-integration.test.js
```

Expected: all GM renderer tests pass.

### Task 4: Responsive BrowserWindow and launcher layout

**Files:**
- Modify: `src/main/index.js`
- Modify: `src/renderer/styles.css`
- Modify: `test/unit/runtime-packaging-boundary.test.js`
- Modify: `test/unit/renderer-content.test.js`
- Create: `test/unit/responsive-launcher-layout.test.js`

**Interfaces:**
- BrowserWindow geometry becomes default `1280×720`, minimum `1024×640`, resizable/maximizable.
- Existing renderer DOM IDs and login/home state transitions remain unchanged.

- [ ] **Step 1: Write failing window and CSS contract tests**

Assert main includes:

```js
const BASE_WINDOW_WIDTH = 1280;
const BASE_WINDOW_HEIGHT = 720;
const MIN_WINDOW_WIDTH = 1024;
const MIN_WINDOW_HEIGHT = 640;
```

Assert BrowserWindow uses these values with `resizable: true` and `maximizable: true`. Assert CSS removes fixed `min-width: 1601px`, `min-height: 910px`, `.shell { width: 1601px; height: 910px; }`, adds a fluid shell, a `clamp(330px, 32vw, 430px)` login panel, bounded modals, and compact height media queries.

- [ ] **Step 2: Run responsive tests and verify RED**

Run:

```powershell
node --test test/unit/responsive-launcher-layout.test.js test/unit/runtime-packaging-boundary.test.js test/unit/renderer-content.test.js
```

Expected: fail on fixed 1601×910 contracts.

- [ ] **Step 3: Implement responsive BrowserWindow settings**

Change only geometry/resizing flags. Preserve `frame: false`, `useContentSize: true`, tray lifecycle, close confirmation, `contextIsolation: true`, `sandbox: true`, `nodeIntegration: false`, and `webSecurity: true`.

- [ ] **Step 4: Convert fixed renderer geometry to reflow**

Make `.shell` fill the viewport. Use `clamp()` for login width, gaps, paddings, activity cards and footer controls. Add width/height media queries for compact top navigation, login spacing, home content deck, timeline and existing modals. Do not use `zoom`, `setZoomFactor`, or a global transform scale.

- [ ] **Step 5: Update brittle official-geometry assertions**

Replace exact fixed-size assertions only where the approved responsive contract changed. Retain branding, security, asset, close-dialog, and native-boundary assertions.

- [ ] **Step 6: Run Task 4 tests and verify GREEN**

Run the Step 2 command. Expected: all selected tests pass at the new responsive contract.

### Task 5: Full verification and packaged-resource proof

**Files:**
- Modify: `src/scripts/smoke-renderer-flow.cjs`
- Modify: `src/scripts/smoke-packaged.cjs`
- Verify: `resources/gm-items.sqlite3`

**Interfaces:**
- Consumes all previous tasks.
- Produces test, syntax, renderer-flow, database-integrity, and packaged-resource evidence.

- [ ] **Step 1: Run syntax and unit verification**

Run:

```powershell
npm run check
npm test
```

Expected: exit code 0; no failed tests. Record the known `node:sqlite` experimental warning separately rather than treating it as a functional failure.

- [ ] **Step 2: Verify the production database independently**

Use Python `sqlite3` in read-only mode to assert `PRAGMA integrity_check = ok`, total rows `135674`, distinct IDs `135674`, racer count `2081`, S racer count `177`, suit count `17874`, top count `28956`, and hair/accessory count `19962`.

- [ ] **Step 3: Run renderer smoke**

Run `npm run check:renderer-flow`. Expected: exit code 0. Exercise or statically assert `1024×640`, `1280×720`, and `1600×900` responsive contracts, login-to-home visibility, GM open/query/close, and logout cleanup.

- [ ] **Step 4: Build an unpacked x64 application**

Run:

```powershell
npm run pack
```

Expected: exit code 0 and an unpacked Windows application under `dist`. If the repository's mandatory native-runtime prepack gate cannot obtain its separate release material, report the exact gate as unverified and do not claim packaging success.

- [ ] **Step 5: Verify the packaged SQLite resource**

Confirm `gm-items.sqlite3` exists directly under the packaged `resources` directory, is not inside `app.asar`, passes `PRAGMA integrity_check`, and can answer a read-only S-racer descending query. Confirm native runtime artifacts remain governed by their existing PE resource boundary.

- [ ] **Step 6: Audit only the task diff**

Review all changed/new task files, ensure no arbitrary URL/SQL/IPC exposure, no regression to login/game/tray/close flows, and no unrelated user changes were reverted. Report changed files, verification results, packaging status, failures, skipped items, and remaining unverified behavior.
