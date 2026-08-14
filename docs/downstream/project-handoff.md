# Foundation Sunshine 应用显示方案 Fork：项目交接与开发指南

本文是 `cainiao524/foundation-sunshine` 的 Fork 专用交接文档。全新工作区只需克隆本仓库、阅读本文和 [`app-display-profile.md`](app-display-profile.md)，即可理解当前目标、边界、实现、同步、构建和发布方式。

上游通用功能仍以 Foundation Sunshine 自带文档和源码为准；本文只描述本 Fork 的差异和必须保持的开发决策。文中的版本快照会过期，当前事实必须用 Git、GitHub Releases 和 GitHub Actions 重新确认。

## 一、项目一句话说明

这是 [`AlkaidLab/foundation-sunshine`](https://github.com/AlkaidLab/foundation-sunshine) 的个人 Fork，在保留基地版显示器、虚拟显示器和串流架构的前提下，增加“按 Sunshine APP 选择显示方案”，并自动把该功能应用到上游后续正式版，生成可安装版和便携版 Windows 程序。

主要使用场景是：

- 在 Moonlight 中把不同 Sunshine APP 分别配置为普通电脑屏幕、虚拟副屏、虚拟主屏或仅使用目标显示器。
- 同一台 Moonlight 设备切换 APP 时，Sunshine 按目标 APP 的配置重新准备显示器。
- 不要求 Moonlight 客户端实现本 Fork 的扩展参数；普通客户端启动 APP 即可。
- 上游发布新的正式版后，自动保留本 Fork 功能并构建 Windows 产物。

## 二、必须保持的产品方向

### 2.1 当前优先级

1. 保持按应用显示方案稳定可用。
2. 只同步上游正式版，并让定制功能容易继续应用。
3. 保持 Windows 安装版和便携版可以自动构建、校验和下载。
4. 在不破坏稳定架构的前提下吸收上游新功能与修复。
5. 自有应用仓库、品牌包装和自有代码签名可以延后。

### 2.2 明确排除：跨设备直接接管

跨设备直接接管不属于本项目目标，也不是待修问题。

这里的“跨设备直接接管”是指：A 设备仍在串流某个 APP 时，B 设备尝试自动终止 A 的会话并立即切换到另一个 APP 或显示方案。

后续开发必须遵守：

- 不在 `/resume`、APP 启动或退出路径加入自动终止其他客户端会话的实验逻辑。
- 不恢复此前剥离的跨设备自动切换实验。
- 不为该场景设计重试、锁、等待窗口或会话所有权转移。
- 不把跨设备首次连接报错写入待办或验收清单。
- 除非用户以后明确改变方向，否则无需分析或测试该场景。

需要维护的切换场景仅是：同一台 Moonlight 设备使用“退出运行中的应用并运行此应用”，在不同 APP 显示方案之间切换。

## 三、仓库、远端与分支模型

| 项目 | 当前约定 | 用途 |
| --- | --- | --- |
| 用户 Fork | `https://github.com/cainiao524/foundation-sunshine` | 定制代码、自动化和发布 |
| 上游仓库 | `https://github.com/AlkaidLab/foundation-sunshine` | 正式版来源 |
| 默认/开发分支 | `feature/app-display-profile` | 本 Fork 的实际主线，包含上游代码和定制功能 |
| 镜像分支 | `master` | 保存上游基线，不在此分支开发定制功能 |
| 正式版状态 | `.github/upstream-stable-release` | 记录自动化已经处理的上游正式版标签 |

### 3.1 迁移期注意事项

本 Fork 最初曾同步上游 `master` 最新源码，因此 `origin/master` 和功能分支可能暂时领先于上游“最新正式版”。正式版自动化采用只前进策略：

- 如果正式版仍落后于现有基线，只记录该正式版，不回退、不强推、不构建一个伪装成正式版底座的包。
- 当后续正式版与现有基线相同或包含现有基线时，才快进 `master`、合入功能分支并构建。
- 如果正式版与镜像分支发生真正分叉，工作流失败并要求人工处理。

不得通过重置、强推或重写现有功能分支来“清理”这段历史。已有发布和标签必须继续可追溯。

### 3.2 全新工作区初始化

```powershell
git clone https://github.com/cainiao524/foundation-sunshine.git
Set-Location foundation-sunshine
git remote add upstream https://github.com/AlkaidLab/foundation-sunshine.git
git fetch --all --tags
git switch feature/app-display-profile
git status --short --branch
```

随后确认：

```powershell
git remote -v
gh auth status
gh repo view cainiao524/foundation-sunshine --json defaultBranchRef,url
gh release list -R cainiao524/foundation-sunshine --limit 10
gh run list -R cainiao524/foundation-sunshine --limit 10
```

不要依赖本地缓存的 `origin/HEAD`、旧标签列表或本文的历史版本号判断当前状态；每次接手都重新获取远端事实。

## 四、按应用显示方案的行为

### 4.1 优先级与兼容性

- APP 没有 `display-target` 时完全跟随全局配置，保持上游行为。
- APP 设置了显示目标时，APP 方案优先于全局显示目标和客户端扩展选择。
- 分辨率和刷新率可以继续取当前 Moonlight 客户端请求，也可以由 APP 固定。
- 本 Fork 不另建显示器后端，仍使用基地版的显示意图、拓扑准备、ZakoVDD、恢复和探测流程。
- 配置保存在 Sunshine 服务端的 APP 数据中，不要求修改 Moonlight。
- `physical-current` 由主机当前物理屏模式决定串流尺寸和帧率，不修改显示设置，也不允许回退到 VDD。

### 4.2 APP 配置字段

| 字段 | 可用值 | 含义 |
| --- | --- | --- |
| `display-target` | 空、`virtual`、`physical`、`physical-current` | 跟随全局、强制虚拟屏、强制物理屏、原样串流当前物理屏 |
| `display-device-prep` | `ensure_active`、`ensure_primary`、`ensure_secondary`、`ensure_only_display` | 保证目标启用、设为主屏、设为副屏、仅保留目标显示器 |
| `display-resolution-mode` | 空、`client`、`fixed` | 跟随全局、跟随客户端、固定分辨率 |
| `display-resolution` | 例如 `1920x1080` | 固定分辨率，仅在 `fixed` 时保存 |
| `display-refresh-rate-mode` | 空、`client`、`fixed` | 跟随全局、跟随客户端、固定刷新率 |
| `display-refresh-rate` | 例如 `60`、`120` | 固定刷新率，仅在 `fixed` 时保存 |
| `display-vdd-identity` | 空、`app`、`app-client` | 跟随全局、每个 APP 共用、每个 APP 与客户端组合独立 |
| `display-output-name` | 显示设备编号 | 强制物理屏或原样串流物理屏时可选 |
| `display-disconnect-action` | `keep`、`restore` | 断开后保留当前方案，或恢复原物理显示布局 |

界面默认值以 `AppEditor.vue` 为准。当前新建强制虚拟屏方案默认使用 `app-client` 身份和 `keep` 断开策略。

### 4.3 示例

以下只是字段示例，日常使用优先通过 WebUI 的 APP 编辑器保存：

```json
{
  "name": "虚拟副屏",
  "display-target": "virtual",
  "display-device-prep": "ensure_secondary",
  "display-resolution-mode": "client",
  "display-refresh-rate-mode": "client",
  "display-vdd-identity": "app-client",
  "display-disconnect-action": "keep"
}
```

原样串流当前主物理屏，不要求 Moonlight 修改分辨率或帧率：

```json
{
  "name": "原样串流当前显示器",
  "display-target": "physical-current"
}
```

这个模式只保存可选的 `display-output-name`。它在启动预检和 RTSP 握手分别读取一次当前物理显示模式，用主机当前宽度、高度和刷新率覆盖客户端请求；不调用显示配置、不恢复显示状态、不创建或回退到 VDD。指定屏不可用时直接失败。串流期间来自客户端的动态分辨率和帧率请求也会被忽略。

```json
{
  "name": "仅虚拟屏",
  "display-target": "virtual",
  "display-device-prep": "ensure_only_display",
  "display-resolution-mode": "fixed",
  "display-resolution": "1920x1080",
  "display-refresh-rate-mode": "fixed",
  "display-refresh-rate": "60",
  "display-vdd-identity": "app-client",
  "display-disconnect-action": "restore"
}
```

## 五、实现结构与调用顺序

### 5.1 关键文件

| 文件 | 职责 |
| --- | --- |
| `src_assets/common/assets/web/components/AppEditor.vue` | APP 显示方案表单、默认值、条件显示和提交清理 |
| `src_assets/common/assets/web/public/assets/locale/*.json` | APP 显示方案界面翻译 |
| `src/confighttp.cpp` | 保存 APP 时验证枚举、分辨率、刷新率和条件字段 |
| `src/process.h`、`src/process.cpp` | 解析 APP 字段，并通过 `apply_app_display_profile()` 覆盖启动会话 |
| `src/rtsp.h` | 在启动会话中携带显示目标、分辨率、刷新率、VDD 身份和恢复策略 |
| `src/nvhttp.cpp` | APP 启动和恢复时，在显示器准备前应用 APP 方案 |
| `src/nvhttp_stream_start.cpp` | 原样串流模式只读解析物理屏和当前模式，并绕过显示配置与恢复 |
| `src/display_device/parsed_config.cpp` | 把 APP 覆盖转换成基地版显示意图和显示请求 |
| `src/display_device/session.cpp` | 根据全局、APP 或 APP+客户端生成 VDD 身份并复用现有创建流程 |
| `src/rtsp.cpp`、`src/video.*` | 在握手时覆盖串流规格，锁定捕获目标并禁止静默换屏 |
| `src/stream.cpp` | 动态参数时重新应用或保护 APP 方案，并在最后会话断开时执行恢复策略 |
| `docs/downstream/app-display-profile.md` | 字段和底层维护要点 |

### 5.2 启动和恢复顺序

必须保持以下顺序：

1. 解析标准 GameStream/Moonlight 启动参数。
2. 根据 APP 编号找到服务端 APP 配置。
3. 调用 `proc::proc.apply_app_display_profile()` 覆盖启动会话。
4. 调用显示器准备和编码器探测入口；`physical-current` 只读取物理屏并精确探测，其余模式调用基地版现有显示配置流程。
5. 启动或恢复 APP。

不要把显示方案放进 APP 前置命令。显示器准备早于前置命令，放在那里会来不及影响捕获目标。

动态分辨率请求也必须重新应用当前 APP 方案，否则活动显示器探测可能覆盖强制目标。

### 5.3 VDD 身份

- 全局模式：保持基地版 `vdd_reuse` 和客户端身份行为。
- `app`：同一 APP 共用稳定身份。
- `app-client`：APP 编号与客户端证书标识组合成稳定身份，适合不同客户端分别维护虚拟屏。
- 优先使用客户端证书 UUID；名称只作为兼容回退。

不要另写 VDD 驱动或显示助手。现有驱动、创建、恢复、IOCTL 和拓扑逻辑属于基地版，应优先复用。

## 六、正式版同步自动化

入口：`.github/workflows/sync-upstream-build.yml`。

### 6.1 触发方式

- GitHub 云端每六小时检查一次：`23 */6 * * *`。
- 香港时间通常约为 `02:23、08:23、14:23、20:23`，GitHub 调度可能延迟几分钟。
- 可以手动运行，并选择 `force-build`。
- 用户电脑不需要开机。

### 6.2 正式版判定

工作流调用上游 GitHub Releases 的 `releases/latest` 接口：

- 草稿发布被排除。
- 标记为预发布的版本被排除。
- 不根据最新标签名称猜测正式版。
- 不同步上游 `master` 的普通开发提交。
- 只获取当前正式版标签，避免拉取全部上游分支。

### 6.3 状态机

1. 读取 `.github/upstream-stable-release` 中已经处理的正式版。
2. 取得上游最新正式版标签和对应提交。
3. 比较 `origin/master`、正式版提交和功能分支。
4. 正式版仍落后于迁移前基线时，只更新版本记录，不回退，也不启动构建。
5. 正式版追上后，快进镜像 `master`，合入 `feature/app-display-profile`。
6. 合并成功后由机器人提交版本记录并推送功能分支。
7. 仅在真正可用的新正式版合入时调用 Windows 构建工作流。
8. 任何分叉或代码冲突都会停止任务，不使用强推。

手动 `force-build` 只构建当前功能分支。如果正式版尚未追上迁移前基线，产物使用 `app-display-current-<短提交>`，避免冒充正式版底座。

## 七、Windows 构建与产物

### 7.1 云端构建

入口：`.github/workflows/main.yml`。

正式版同步工作流通过 `workflow_dispatch` 调用它，并传入 `build-version`。主要阶段包括：

- VDD 辅助脚本冒烟测试。
- Node.js 22 环境、`npm ci`、网页检查、网页测试和网页构建。
- MSYS2/UCRT64、CMake、Ninja 和 ccache 原生构建。
- Windows 原生测试。
- 固定 Inno Setup 6.7.3 下载地址和 SHA-256 校验。
- 安装版和便携版打包。
- 最终文件重命名后再生成 `SHA256SUMS.txt` 和 `checksums.json`。
- 上传 GitHub Actions 构建产物。

自动同步触发的手动构建设置 `publish_release=false`，因此不会自动创建 GitHub 正式发布页。

预期附件：

- `Sunshine.<版本>.WindowsInstaller.exe`
- `Sunshine.<版本>.WindowsPortable.zip`
- `SHA256SUMS.txt`
- `checksums.json`

### 7.2 本地完整构建

Windows 本地可使用：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-app-display-package.ps1
```

脚本当前假设：

- MSYS2 安装在 `C:\msys64`，并具有 UCRT64 构建依赖。
- 当前用户安装了 Inno Setup 6。
- Node.js 和 npm 可用。
- 网络可以获取要求的驱动依赖。
- 默认使用 `C:\CodexSunshineAppDisplay` 目录联接，避免含中文或过长路径影响部分 Windows 工具。

输出写入被 Git 忽略的 `artifacts/`；原生构建目录 `build-native/`、`build-package/` 和产物目录都不得提交。

## 八、验证要求

### 8.1 快速代码验证

```powershell
npm ci
npm run lint:webui
npm run test:webui
npm run build
git diff --check
```

工作流修改还必须运行 `actionlint`。如果本机没有该程序，可从 [`rhysd/actionlint`](https://github.com/rhysd/actionlint) 官方发布下载，不要把工具二进制提交到仓库。

### 8.2 原生和打包验证

- 原生代码必须至少完成一次 Windows 编译。
- 运行 `ctest --test-dir <构建目录> --output-on-failure`。
- 安装包和便携包都必须生成。
- 对两项产物重新计算 SHA-256，并与校验文件比较。
- 便携包至少验证 ZIP 可完整读取，并确认 `sunshine.exe`、VDD 和虚拟鼠标驱动文件存在。
- 不要为了验证而静默安装或替换用户当前正在使用的 Sunshine。

### 8.3 功能验收

- 未配置显示方案的旧 APP 保持原行为。
- 同一 Moonlight 设备可以在副屏 APP 和独占屏 APP 之间退出并切换。
- 强制虚拟副屏、虚拟主屏和仅虚拟屏。
- 强制物理屏以及不存在的物理屏编号错误处理。
- 原样串流当前物理屏的宽度、高度和刷新率覆盖客户端请求，且显示拓扑、模式与 VDD 数量不变。
- 原样串流指定屏不存在、未启用或指向 VDD 时直接失败，不捕获其他屏幕。
- 跟随客户端与固定分辨率、刷新率。
- `app` 和 `app-client` 两种 VDD 身份。
- 断开保留与断开恢复。
- APP 正常退出、启动失败和 Sunshine 停止时的显示恢复。
- 明暗主题、窄窗口和高缩放下 APP 编辑器无横向溢出。

跨设备直接接管不属于验收内容。

## 九、签名、安装和驱动策略

- 当前 Fork 的 Sunshine 安装程序没有自有代码签名，Windows 可能显示未知发布者或安全提示。
- 便携版涉及服务、驱动或虚拟显示器操作时，应以管理员身份运行。
- 暂不购买、申请或自动配置自有签名证书。
- 不对基地版随包的 `.cat`、`.cer`、`.inf`、`.dll` 等驱动材料进行重新签名或内容修改。
- 不在仓库、日志或文档中保存任何私钥、令牌或证书密码。
- 如果以后决定签名，必须作为独立项目处理，明确区分程序代码签名、安装程序签名和内核驱动签名。

## 十、发布策略

自动构建和正式发布是两个不同边界：

- 自动化默认只生成 GitHub Actions 产物。
- 只有用户明确要求创建正式发布页时，才创建新标签并上传附件。
- 发布前确认目标标签和发布页不存在。
- 永不覆盖已有标签或发布。
- 正式发布应上传安装版、便携版、`SHA256SUMS.txt` 和 `checksums.json`。
- 发布说明必须写明上游底座、本 Fork 功能、已完成验证、签名状态和项目范围。
- GitHub 可能清理附件文件名中的非 ASCII 字符；上传后必须重新核对实际名称，并让校验清单与远端附件名称一致。

推荐标签格式：

```text
app-display-<上游正式版标签>
```

历史发布可能来自迁移到“只跟随正式版”策略之前，不能据此推断后续同步规则。

## 十一、上游合并后的重点审查

每次正式版合入后，先看上游对下列路径的修改，再解决冲突：

1. `src/nvhttp.cpp`：APP 启动、恢复与显示准备顺序。
2. `src/process.*`：APP 结构、JSON 字段解析和 `apply_app_display_profile()`。
3. `src/rtsp.h`：启动会话字段布局和初始化。
4. `src/display_device/parsed_config.cpp`：显示意图、客户端显示器和 VDD 回退。
5. `src/display_device/session.cpp`：VDD 创建、复用、身份和恢复。
6. `src/stream.cpp`：动态分辨率、最后会话断开和显示恢复。
7. `src_assets/common/assets/web/components/AppEditor.vue`：表单数据、校验、条件字段清理和布局。
8. `src_assets/common/assets/web/public/assets/locale/*.json`：新键和上游翻译结构。
9. `.github/workflows/main.yml`：上游构建变化与本 Fork 打包步骤。

冲突处理原则：

- 先理解上游新结构，再移植最小的 APP 覆盖逻辑。
- 不用本 Fork 的旧文件整体覆盖上游新文件。
- 不把与按应用显示方案无关的实验一起带入冲突解决提交。
- 后端、界面、自动化尽量保持独立提交，便于下次同步审查。
- 冲突处理完成后必须重新执行网页、原生和打包验证。

## 十二、当前历史快照

以下只用于理解仓库历史，不是永远有效的“当前版本”：

- 2026-08-13 完成按应用显示方案后端和 APP 编辑界面。
- 同日完成 Windows 安装版、便携版、校验文件和 GitHub Actions 云端构建验证。
- 已有历史发布：`app-display-v2026.813.110311.杂鱼`。它产生于正式版同步策略迁移之前，底座来自当时的上游预发布/开发源码。
- 2026-08-13 后自动化改为只读取上游 GitHub 的正式发布。
- 迁移时记录的正式版基线保存在 `.github/upstream-stable-release`，必须以该文件和 GitHub 当前状态为准。

## 十三、全新工作区接手清单

接手者开始工作前应逐项完成：

- [ ] 阅读根目录 `AGENTS.md`、本文和 `app-display-profile.md`。
- [ ] 确认当前分支是 `feature/app-display-profile`，工作区无来源不明的改动。
- [ ] 确认 `origin` 和 `upstream` 地址。
- [ ] 获取远端分支、标签、最新正式版、近期 Actions 和发布页。
- [ ] 读取 `.github/upstream-stable-release`，理解迁移状态。
- [ ] 修改前定位上游与本 Fork 在相关文件中的差异。
- [ ] 保持跨设备直接接管在范围之外。
- [ ] 运行与改动相匹配的检查、测试、编译和打包。
- [ ] 只提交本次任务相关文件，检查 `git diff --check`。
- [ ] 推送、标签、正式发布和签名严格遵守用户授权边界。

完成这些步骤后，无须依赖旧工作区、旧聊天记录或本地构建目录，即可继续维护本项目。
