# 官服客户端加载流程修复设计

## 目标

- 打开登录器时继续后台预加载 `GameApp.exe`，但不进入游戏。
- 只有点击“开始游戏”才放行客户端。
- 原生启动命令被接受后立即最小化 Electron 登录器。
- 不在 Electron 内伪造加载图或百分比；由现有最小 `QQSpeedNativeBridge.dll` 读取客户端本地资源并显示官服样式的原生动态加载窗。
- 启动失败时恢复登录器并展示原生错误。

## 客户端资源

启动前只读校验下列文件，不复制、不打包、不覆盖：

- `Loader/LoaderBG.bmp`
- `Loader/cancel_normal.bmp`
- `Loader/Progress_Back.bmp`
- `Loader/Progress_Fore.bmp`
- `loadertipstext.ini`

官服通过 `-StartFromLauncher --preload-game` 与 GLauncher 命名管道触发 `GameApp.exe` 内部的 `ShowLoadingUI`。当前私服链仍使用旧 `KartMap_Shared/RunSpeedGameEvent` 协议，直接追加官服参数会切断现有登录链，因此由 NativeBridge 在不改变协议的前提下读取同一资源组合背景、提示和动态进度条。

## 状态机

1. Electron 启动：创建原生会话并预加载 GameApp。
2. 登录成功：原生会话进入 `authenticated`，继续等待开始事件。
3. 点击开始：主进程发送 `start-game`，等待原生 DLL 的同步命令确认。
4. 命令成功：立即最小化 Electron；原生状态继续异步发布 `starting -> ready/failed`。
5. NativeBridge 显示客户端资源组成的原生加载窗；GameApp 初始化完成后关闭加载窗并切换至游戏主窗口。
6. 失败：如果登录器因本次启动而最小化，立即恢复、置前并显示错误。
7. 游戏退出：恢复登录器，允许重新建立预加载会话。

## 就绪判断

`GameApp.exe` 的约 300×200 加载窗只是加载阶段，不能作为“游戏已就绪”。原生桥接仅接受：

- GameServer 登录成功回调设置的 ready 标志；或
- 可见且至少 640×480 的 GameApp 主窗口兜底。

这样不会在动态进度条刚出现时提前释放共享内存和原生会话。

## 边界

- 不恢复 `LauncherKernel.exe`、`QQSpeedLauncher_x64.exe` 或 `LauncherCore.dll`。
- 不修改、复制或打包客户端 Loader 资源。
- Hook DLL 仍由固定构建产物和哈希清单管理，打包流程只校验，不从旧目录覆盖。
