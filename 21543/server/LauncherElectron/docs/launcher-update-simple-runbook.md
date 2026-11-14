# 登录器强制更新：最简上线步骤

## 固定架构

- 主检查地址：`http://43.226.61.100:28881/update.json`
- 备用检查地址：`http://114.132.178.23:28881/update.json`
- 更新目录：`D:\LauncherUpdate`
- 游戏登录地址：`43.226.61.100:29999`
- 更新下载端口：`28881`
- 更新服务总带宽：默认不限速（`QQS_THROTTLE_MBPS=0`）
- 不限制人数，不限制单个连接速度
- 不使用数据库

登录器会同时请求主、备用检查地址，只采用签名验证通过的
`update.json`。安装包在当前来源下载失败或 SHA256 不一致时，会自动切换到
另一个来源重新下载。两个服务器必须始终保存完全相同的 `.exe` 和已签名
`update.json`；备用服务器不运行游戏服，也不需要数据库或 Aurora 上传接口。
两个来源可用时，约 50% 的客户端优先下载主服务器、约 50% 优先下载备用
服务器，以分担安装包流量；首选来源失败时仍会回退到另一个来源。

如需临时恢复总带宽上限，可把 `QQS_THROTTLE_MBPS` 改为 `1` 到 `700`
之间的整数并重新运行 `setup-update-server.cmd`。设为 `0` 会删除旧的
`QQSpeed Launcher Update *` Windows QoS 策略，不需要重启服务器。
默认的 `setup-update-server.cmd` 和 `start-update-server.cmd` 只维护
已经安装好的更新 Nginx，不查找、不停止也不重启游戏服务端，不读取或修改
游戏服务端配置。

服务器管理员平时直接使用中文入口即可：

- 主服务器首次配置或取消限速：`配置主登录器更新服务.cmd`
- 主服务器手动启动/重载：`启动或重载主更新服务.cmd`
- 备用服务器首次配置：`backup-server\配置备用登录器下载服务.cmd`

这些中文文件只是清晰的人工入口，内部仍调用英文脚本；计划任务继续使用英文
内部文件名，避免 Windows 路径编码和既有部署兼容问题。不要重命名
`SpeedServer.exe` 或 `QQSpeedServer.exe`。

登录器在安装态启动时先检查签名更新配置。检查失败、签名失败、下载失败或文件 SHA256 不一致时，只允许重试或退出；检查通过后才创建登录窗口并后台预加载 GameApp。

## 当前主服务器维护

1. 在 `43.226.61.100` 使用 Windows Nginx：`C:\Users\Administrator\Desktop\nginx-1.31.2\nginx-1.31.2`。
2. 将 `deploy\update-server` 目录复制到服务器。
3. 确认现有配置
   `C:\nginx-1.31.2\conf\launcher-update.conf` 已存在，然后双击
   `配置主登录器更新服务.cmd` 并确认 UAC。它只会验证/重载现有 Nginx、
   更新 TCP `28881` 防火墙规则、按 `QQS_THROTTLE_MBPS` 更新或删除 QoS，
   并把开机任务改为只启动更新 Nginx。它不会验证或操作
   `SpeedServer.exe`。需要手动重载时双击
   `启动或重载主更新服务.cmd`。

   如果这是全新主机且还没有 `launcher-update.conf`，不要用维护脚本猜测
   配置；先从当前在线主服务器备份并恢复该配置。只有明确需要重新部署
   Aurora 上传接口并允许脚本配置/重启游戏服务端时，才使用
   `setup-update-server-with-aurora.cmd`。

4. 在云服务器安全组放行 TCP `28881`。
5. 等登录器完成最终加密和打包后，在构建电脑执行发布脚本：

   ```powershell
   npm run update:publish -- `
     --installer 'F:\完整路径\QQSpeedLauncher-Setup-1.0.1-x64.exe' `
     --output 'F:\LauncherUpdatePublish' `
     --private-key 'C:\Users\lp\.qqspeed-launcher\update-signing-private.pem' `
     --version '1.0.1'
   ```

6. 先把生成的 `.exe` 上传到服务器 `D:\LauncherUpdate`，确认上传完成后，最后上传 `update.json`。
7. 从外网检查：

   ```powershell
   Invoke-RestMethod 'http://43.226.61.100:28881/update.json'
   ```

只有服务器已经能返回与首版相同版本的签名 `update.json` 后，才能把首版安装包发给客户。否则强制更新门禁会阻止登录器进入登录页面。

## 配置备用下载服务器（114.132.178.23）

当前探测结果是 TCP `28881` 可连接，但 HTTP 返回空响应，因此它还不能作为
更新源。先在该服务器用管理员 PowerShell 确认占用端口的程序：

```powershell
$listener = Get-NetTCPConnection -State Listen -LocalPort 28881
$listener | Select-Object LocalAddress, LocalPort, OwningProcess
Get-CimInstance Win32_Process -Filter "ProcessId=$($listener[0].OwningProcess)" |
  Select-Object Name, ExecutablePath, CommandLine
```

不要直接结束未知进程。确认它是需要替换的旧 Nginx 后，再完成以下步骤：

1. 安装或解压 Windows Nginx，例如到 `C:\nginx-1.31.2`。
2. 创建 `C:\LauncherUpdate`。
3. 将 `deploy\update-server\backup-server\nginx-update-mirror.conf` 复制到
   `C:\nginx-1.31.2\conf\launcher-update-mirror.conf`。如果实际目录不同，修改
   配置中的 `root C:/LauncherUpdate;`。
4. 把同一批发布产物中的安装包和 `update.json` 复制到
   `C:\LauncherUpdate`。
5. 在 Windows 防火墙和云安全组中放行入站 TCP `28881`。
6. 在 Nginx 目录验证并启动：

   ```powershell
   Set-Location C:\nginx-1.31.2
   .\nginx.exe -t -c conf/launcher-update-mirror.conf
   Start-Process .\nginx.exe -ArgumentList '-c','conf/launcher-update-mirror.conf' -WindowStyle Hidden
   ```

7. 分别从服务器本机和外网验证，两个请求都必须返回 HTTP 200：

   ```powershell
   Invoke-WebRequest 'http://127.0.0.1:28881/update.json' -UseBasicParsing
   Invoke-WebRequest 'http://114.132.178.23:28881/update.json' -UseBasicParsing
   ```

再用 `Get-FileHash -Algorithm SHA256` 对比两个服务器上的 `.exe`，哈希必须一致。
确认正常后，为 Nginx 增加“计算机启动时”计划任务，避免服务器重启后备用源掉线。

也可以直接把独立目录 `deploy\update-server\backup-server` 复制到备用服务器，
然后以管理员身份运行其中的 `配置备用登录器下载服务.cmd`。该目录不会调用或修改
主服务器的 SpeedServer、Aurora、MySQL 和数据库配置。

## 以后发布新版

1. 将 `package.json` 的版本从例如 `1.0.1` 改为 `1.0.2`。
2. 完成加密和 NSIS 打包。
3. 使用上面的 `update:publish` 命令，将 `--version` 改成新版本。
4. 先上传新 `.exe` 到两个服务器，确认两边哈希一致；然后先替换备用服务器
   `114.132.178.23` 的 `update.json`，最后替换主服务器
   `43.226.61.100` 的 `update.json`。
5. 客户下次启动时自动下载、校验、静默安装并重启。

## 签名密钥

- 私钥：`C:\Users\lp\.qqspeed-launcher\update-signing-private.pem`
- 公钥：`resources\update-public-key.pem`

私钥只保留在构建电脑并做离线备份，不能上传到服务器、网盘、安装包或客户电脑。公钥会随登录器打包，用于验证 `update.json` 和安装包 SHA256。
