# Official Close Confirmation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为登录页和主页增加共享的官服式关闭确认，并统一标题栏、Alt+F4、托盘恢复和真实退出。

**Architecture:** renderer 负责弹窗和偏好；preload 暴露最小 IPC 契约；main 拦截原生关闭并执行动作；tray controller 提供恢复偏好入口。游戏启动隐藏链路保持独立。

**Tech Stack:** Electron 35、Node.js 20、ES modules、HTML/CSS、node:test。

## Global Constraints

- 默认动作固定为 `tray`。
- 只有弹窗“确定”事件允许持久化 `skipPrompt`。
- 不新增依赖，不修改原生登录、游戏启动或服务端协议。
- main/preload/renderer IPC 必须使用具名白名单并验证 sender。

---

### Task 1: 关闭偏好纯逻辑

**Files:**
- Create: `src/renderer/close-preference.js`
- Create: `test/unit/close-preference.test.js`

**Interfaces:**
- Produces: `readClosePreference(storage)`, `saveClosePreference(storage, action)`, `resetClosePreference(storage)`。

- [ ] 先写测试，覆盖空值、损坏值、未知动作、确认保存和恢复提示。
- [ ] 运行 `node --test test/unit/close-preference.test.js`，确认因模块不存在而失败。
- [ ] 实现最小纯函数并重新运行，预期全部通过。

### Task 2: 共享官服弹窗

**Files:**
- Modify: `src/renderer/index.html`
- Modify: `src/renderer/styles.css`
- Modify: `src/renderer/app.js`
- Create: `test/unit/official-close-dialog-integration.test.js`

**Interfaces:**
- Consumes: Task 1 的关闭偏好函数。
- Produces: `requestLauncherClose()`, `openCloseDialog()`, `confirmLauncherClose()`。

- [ ] 先写静态集成测试，要求共享 modal 位于两页面之外、默认 `tray`、官服文案与素材、标题栏和原生关闭事件共用 `requestLauncherClose`。
- [ ] 运行该测试并确认缺少 modal/handler 的预期失败。
- [ ] 增加 HTML、CSS、焦点/Escape 行为和确认逻辑。
- [ ] 重新运行测试，预期全部通过。

### Task 3: main/preload/tray 关闭契约

**Files:**
- Modify: `src/preload/preload.cjs`
- Modify: `src/main/index.js`
- Modify: `src/main/tray-controller.js`
- Modify: `test/unit/main-tray-integration.test.js`
- Modify: `test/unit/tray-controller.test.js`

**Interfaces:**
- Produces IPC: `window:close-requested`, `window:close-preference-reset`, `window:apply-close-action`。

- [ ] 先更新测试：原生 close 只发送请求；`tray` 隐藏；`quit` 走 `requestApplicationQuit`；托盘菜单文案和恢复回调准确。
- [ ] 运行相关测试并确认旧直接隐藏实现导致失败。
- [ ] 实现主进程、preload 和托盘最小改动。
- [ ] 重新运行相关测试，预期全部通过。

### Task 4: 全量验证

**Files:**
- Verify only.

- [ ] 运行 `npm run test:unit`，预期 0 failures。
- [ ] 运行 `npm run check`，预期 exit code 0。
- [ ] 运行 `npm run check:renderer-flow`，预期 exit code 0。
- [ ] 运行开发态 Electron，分别验证登录页、主页、Alt+F4、记住选择和托盘恢复；若真实登录条件不可用，明确记录主页人工验证限制。
- [ ] 检查 `git diff --check` 与限定目录 diff，确认未触碰已有 Server 改动。

