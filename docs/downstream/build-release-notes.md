# 下游构建与发布注意事项

本文记录 `foundation-sunshine-AppDisplayProfile` 在 Windows 本地构建、GitHub Actions 发布和上游检查中已经验证过的注意事项。新任务开始前，先阅读 [`project-handoff.md`](project-handoff.md) 和 [`app-display-profile.md`](app-display-profile.md)。

## Windows 本地构建

### 基地版安装器打包规则

- 固定使用 `scripts/build-app-display-package.ps1` 生成基地版 Inno Setup 安装器；CPack 只用于生成便携 ZIP，不用于生成安装器。
- 每次打包前都会清空 `inno_staging`、`inno_artifacts`、`portable_artifacts` 和顶层 `artifacts`，防止旧网页资源、二进制或校验文件混入新包。
- Windows 主包必须构建仓库锁定的 `sunshine-control-panel` 子模块，禁止从外部仓库下载“最新” GUI；否则 DualSense 组件协议和清单版本会发生漂移。
- DualSense Sidecar 继续作为上游正式版的按需下载组件。主包只携带经 GitHub Release 元数据校验的官方清单，不重新构建没有发布地址的下游 Sidecar，也不把完整运行时塞入安装版或便携版。
- Inno 安装器输出在 `build-package-current/inno_artifacts`，便携包输出在 `build-package-current/portable_artifacts`；不要从旧的 `cpack_artifacts` 目录取安装器。
- 发布前必须确认暂存目录中只有当前的 `AppEditor-*.js`、`NewDisplayOutputSelector-*.js` 资源，并检查 `artifacts/SHA256SUMS.txt`、`artifacts/checksums.json` 与实际文件一致。
- 发布前还必须确认安装版暂存目录与便携包中的 `tools/ds5-sidecar-package.json` 完全一致，下载地址指向 `.github/upstream-stable-release` 记录的正式版，且 `assets/gui/sunshine-gui.exe` 与锁定子模块的本次构建产物完全一致。

推荐命令：

```powershell
.\scripts\build-app-display-package.ps1 `
  -BuildAlias C:\CodexSunshineAppDisplay `
  -BuildDirectory build-package-current `
  -PublisherName cainiao524
```

### 工具链

- 使用 MSYS2 `UCRT64` 环境，不要混用 `MINGW64`、MSVC 和普通 PowerShell 中的同名工具。
- 常用工具路径：

```powershell
C:\msys64\ucrt64\bin\cmake.exe
C:\msys64\ucrt64\bin\ninja.exe
C:\msys64\ucrt64\bin\c++.exe
```

- 如果 PowerShell 找不到 `ninja`，使用完整路径，或从 `MSYS2 UCRT64` 终端运行。
- MSYS2 更新后不要继续复用旧构建目录；工具链、Boost 或 CMake 发生变化时，重新配置一个新的 `build-*` 目录。

### 构建脚本运行前提（踩坑记录）

- **Node.js 必须 >= 26.7 且 < 27**（`.node-version` 指定；vite 8 需要 MSVC 构建的 Node）。系统 Node 版本不符时，把便携版目录前置到 PATH：
  ```powershell
  $env:PATH = 'E:\...\node-v26.7.0-win-x64;' + $env:PATH
  ```
- **cargo 必须在 PATH**（`C:\Users\<user>\.cargo\bin`，rustup 默认），否则脚本直接 `throw 'Rust Cargo is required...'`（控制面板 src-tauri 需要）。
- **必须用 PowerShell 5.1 包装运行脚本**：PS7 的 `$PSNativeCommandUseErrorActionPreference` 会把 npm 的 stderr 警告当作 NativeCommandError 导致误报失败：
  ```powershell
  & 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' -NoProfile -ExecutionPolicy Bypass -File scripts\build-app-display-package.ps1 -BuildAlias C:\CodexSunshineAppDisplay -BuildDirectory build-package-current -PublisherName cainiao524
  ```
- **必须设 `NO_PROXY=127.0.0.1,localhost,::1`**：否则 reqwest 继承代理环境变量会破坏控制面板的 `proxy_server` 测试（`proxy_retries_with_refreshed_target_when_port_changes` 502 失败）。
- **PowerShell 直跑 ninja 会静默失败**（GCC 无诊断、退出 1），用 msys bash 看真实诊断：
  ```bash
  C:\msys64\usr\bin\bash.exe -lc "export PATH=/ucrt64/bin:/usr/bin; cd /c/CodexSunshineAppDisplay; ninja -C build-package-current sunshine -j2"
  ```
- 构建结束后的 `exit code: 1` 且日志全绿 = PS7 对 stderr 的误报，不是真失败；以四件套产物与 `Sunshine Version: <提交>` 头部为准。
- 驱动依赖（ZakoVDD/vmouse/nefcon）和 Boost FetchContent 在 `build-package-current/_deps` 有缓存；首次构建仍需网络（npm registry / crates.io 可直连，GitHub 需代理）。

### 中文路径和异常编译退出

仓库路径包含中文时，GCC/`cc1plus` 可能只返回退出码 `1`，不输出有效诊断，并留下 `0` 字节临时汇编文件。这种现象不能当作源码错误继续盲目修改。

优先使用 ASCII 构建别名，例如：

```powershell
.\scripts\build-app-display-package.ps1 `
  -BuildAlias C:\CodexSunshineAppDisplay `
  -BuildDirectory build-package-current
```

构建目录和 `logs_archive` 等临时目录不要提交。提交前只暂存明确的源码、测试、文档或工作流文件。

### 最小验证

原生改动至少执行：

```powershell
git diff --check
& 'C:\msys64\ucrt64\bin\ninja.exe' -C build-native-current sunshine
& 'C:\msys64\ucrt64\bin\ninja.exe' -C build-native-current test_sunshine
```

如果本地工具链无法产生有效编译诊断，以 GitHub Actions 的 Windows 原生构建结果为准；不要用删除客户端分辨率、禁用 VDD 或固定 `Desktop` 分辨率来规避构建或运行问题。

## GitHub Actions 发布

### `source-ref` 的限制

当前 `.github/workflows/main.yml` 的 `actions/checkout` 将 `source-ref` 直接作为分支或标签引用使用。手动发布时应传入包含目标提交的分支：

```powershell
gh workflow run main.yml `
  -R cainiao524/foundation-sunshine-AppDisplayProfile `
  --ref feature/app-display-profile `
  -f build-version=v2.0 `
  -f publish-release=true `
  -f release-tag=v2.0 `
  -f release-name="Sunshine App Display Profile v2.0" `
  -f source-ref=feature/app-display-profile
```

不要把短提交号直接作为 `source-ref`。否则 Actions 可能尝试获取 `refs/heads/8ff89545*`，在检出阶段失败。若以后要支持提交哈希，应先修改并验证工作流的检出逻辑。

### 云端基地版安装器

云端打包必须与本地脚本保持同样的输出隔离：Inno Setup 写入 `inno_artifacts`，CPack ZIP 写入 `portable_artifacts`。发布安装版只能取 `inno_artifacts/Sunshine.exe`，不得再从 `cpack_artifacts` 取安装器。

### 发布前检查

```powershell
git status --short
git diff --check
git tag --list v1.0.4
gh api repos/cainiao524/foundation-sunshine-AppDisplayProfile/git/ref/tags/v1.0.4
```

正式发布前确认标签不存在、工作区没有不明源码改动，并确认用户明确授权创建标签和发布页。完整工作流成功前，不要报告发布完成。

发布成功后确认：

- `Setup Release` 成功；
- `VDD helper smoke tests` 成功；
- `Windows` 成功；
- Windows 原生测试、安装包、便携包、`SHA256SUMS.txt` 和 `checksums.json` 均已生成；
- 发布说明已由 `scripts/generate-app-display-release-notes.ps1` 根据发布标签、上游正式版记录、精确提交和最终产物校验值生成，并由 GitHub 自动追加自上一个正式版本以来的变更记录；
- GitHub Release 不是草稿，也不是预发布。

正式发布说明必须在最终重命名和校验值生成后创建。说明文件只作为 Release 正文输入，不得加入发布资产；正式产物仍然只能包含安装版、便携版、`SHA256SUMS.txt` 和 `checksums.json`。

## Clash 代理

当 GitHub 直连失败时，先确认 Clash 监听端口，再只在当前 PowerShell 会话设置代理：

```powershell
Get-NetTCPConnection -State Listen |
  Where-Object { $_.LocalPort -eq 7890 }

$env:HTTP_PROXY = 'http://127.0.0.1:7890'
$env:HTTPS_PROXY = 'http://127.0.0.1:7890'
$env:ALL_PROXY = 'http://127.0.0.1:7890'
```

然后再执行 `gh`、`git fetch`、`git push` 或 `gh run watch`。不要把代理地址写入仓库配置或提交到文件中。GitHub Actions 运行在远端，不会使用本机 Clash，不能因为本地代理正常就推断远端构建网络一定正常。

## 上游正式版检查

发布后只检查上游最新正式发布，不使用上游开发分支、草稿或预发布：

```powershell
gh api repos/AlkaidLab/foundation-sunshine/releases/latest
git fetch upstream --tags
```

先判断上游正式版提交是否已经是当前功能分支祖先：

```powershell
git merge-base --is-ancestor <上游正式版提交> HEAD
git rev-list --left-right --count <上游正式版提交>...HEAD
```

如果结果为 `0 <本地提交数>`，说明当前分支已经包含该正式版，不需要重复合并。重点检查以下定制高风险文件的差异：

- `src/nvhttp.cpp`
- `src/process.*`
- `src/display_device/*`
- `src/stream.cpp`
- `src_assets/common/assets/web/components/AppEditor.vue`

真正同步上游时，必须由用户手动触发 `.github/workflows/sync-upstream-build.yml`。发生冲突立即停止并人工处理，禁止强制覆盖功能分支。

## 已记录的失败模式

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| Actions 检出阶段获取 `refs/heads/<短提交号>*` 失败 | `source-ref` 传入了短提交号 | 改传功能分支；若需哈希检出，先修工作流 |
| `set_sunshine_error` 未声明 | 调用点在 `nvhttp`，函数属于 `nvhttp::stream_start` | 使用 `stream_start::set_sunshine_error` |
| 本地 `c++.exe` 无诊断并生成 0 字节临时文件 | 本地 MSYS2/GCC 或路径环境异常 | 使用 UCRT64 和 ASCII 构建别名，必要时交给 Actions 验证 |
| GitHub API 返回 `503` | GitHub Actions 调度接口临时不可用 | 等待后重试一次，确认没有新运行后再重试，不要并行创建多个发布 |
