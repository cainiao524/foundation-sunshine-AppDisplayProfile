# Foundation Sunshine 应用显示方案 Fork：项目交接与开发指南

本 Fork 基于 `AlkaidLab/foundation-sunshine`，只在基地版现有显示器、虚拟显示器和串流流程之上增加“按 Sunshine APP 选择显示方案”。新工作区开始开发、同步、构建或发布前，应阅读本文件、[`app-display-profile.md`](app-display-profile.md) 和 [`build-release-notes.md`](build-release-notes.md)。

## 项目边界

- 核心定制是按 APP 绑定显示目标、拓扑、分辨率、刷新率和断开策略。
- 复用基地版 `use_vdd`、`custom_screen_mode`、`custom_vdd_screen_mode`、显示准备和恢复能力。
- 不复制外部项目的显示管理代码，不新增显示器创建、拓扑切换或驱动管理。
- 跨设备直接接管不在项目范围内。

## 分支与远端

- 用户 Fork：`https://github.com/cainiao524/foundation-sunshine-AppDisplayProfile`
- 上游：`https://github.com/AlkaidLab/foundation-sunshine`
- 开发分支：`feature/app-display-profile`
- `master` 仅保存上游基础线，不用于开发定制功能。

上游同步只跟随 GitHub 正式发布，不根据草稿、预发布或开发分支猜测版本。同步工作流只允许用户手动触发，不设置定时自动合并。合并冲突必须人工处理，禁止强制覆盖功能分支。构建目录、驱动签名文件和无关改动不得提交。

## APP 优先级

没有 `display-target` 时，客户端的 `useVdd`、布局、显示器名称、分辨率和刷新率请求保持原样。配置了 `display-target` 时，APP 方案在显示准备前覆盖客户端显示目标和布局，并清除不适用的客户端虚拟屏布局或显示器名称。缺少 `display-device-prep` 时默认使用 `ensure_active`。

应用配置示例：

```json
{
  "name": "虚拟副屏",
  "display-target": "virtual",
  "display-device-prep": "ensure_secondary",
  "display-resolution-mode": "client",
  "display-refresh-rate-mode": "client",
  "display-dynamic-resolution-follow-display": "enabled",
  "display-disconnect-action": "keep"
}
```

## 关键实现位置

| 文件 | 职责 |
| --- | --- |
| `src/process.*` | 解析 APP 字段并覆盖启动会话 |
| `src/confighttp.cpp` | 校验和保存 APP 字段 |
| `src/nvhttp.cpp` | 在显示准备前应用 APP 方案并处理恢复 |
| `src/display_device/parsed_config.cpp` | 将 APP 字段转换为基地版显示意图 |
| `src/display_device/session.cpp` | 使用基地版 VDD 创建、复用和恢复 |
| `src/stream.cpp` | 动态参数和会话结束时的恢复行为 |
| `src_assets/common/assets/web/components/AppEditor.vue` | APP 编辑器和主设置显示组件复用 |
| `src_assets/common/sunshine-control-panel` | Foundation Desktop 的 APP 字段编辑入口 |

## VDD 标识

始终使用基地版原生身份生成规则：

```cpp
config::video.vdd_reuse ? "shared_vdd" : client_id
```

禁止按 APP、客户端或二者组合生成新的虚拟显示器身份。

## 双显卡笔记本适配（已实现的修复）

混合显卡（dGPU + iGPU）笔记本的面板存在两条 GPU 路径（dGPU 活动路径 + iGPU 路径）。会话开始后约 1~6 秒 Windows 会切换面板 GPU 路径，导致 VDD 输出从活动拓扑中掉出，采集端出现 `Failed to locate an output device`。恢复机制：

- 会话启动约 3 秒主动重断言 VDD 拓扑（`session_t::reassert_vdd_session_topology`，由 `video.cpp` 采集阶段调用，最多 2 次：3s 提前 + 8s 兜底）。
- 重断言后 Windows 常把 VDD 重置为默认模式（如 1280x720），必须按会话保存的 `current_vdd_mode` 重新应用（400ms 间隔最多 3 次）。
- 设备 ID 重映射失败时回退到历史物理身份（`get_historical_physical_device_identities`）。
- 主屏模式校验重试（6 次、200ms/1000ms 退避），最终失败不中止会话；退出恢复时用 `original_modes_snapshot` 校验并修复主屏模式（重试 3 次）。

## 客户端兼容（原版 Moonlight 与 V+）

- 原版 Moonlight（Android/iOS/Windows/Linux）：只发标准参数（`mode/sops/hdrMode/maxBrightness`），无扩展参数时严格按基地版路径处理，串流当前活动显示器。
- Moonlight V+ 独有扩展参数（服务端解析于 `src/nvhttp.cpp`，映射于 `src/display_device/parsed_config.cpp`）：

| 参数 | 含义 | 取值 |
| --- | --- | --- |
| `useVdd` | 选虚拟显示器/副屏快速启动发 1，选物理屏发 0，未选择不发送 | `1` / `0` |
| `customScreenMode` | 副屏组合模式（覆盖全局 `display-device-prep`）| `0`=no_operation、`1`=ensure_active、`2`=ensure_primary、`3`=ensure_only_display、`4`=ensure_secondary |
| `resolutionScale` | 主机分辨率缩放（只缩放 `mode=`，RTSP clientViewport 不缩放）| 默认 `100` |
| `display_name` | 指定显示器 | 设备 ID / VDD GUID |

- `no_operation` 语义（`33d9aa86` 起全面跟随基地版）：VDD 会话 `vdd_prep=no_operation` 时**保持 no_operation**——创建 VDD 但不设置拓扑，激活方式由 Windows 默认处理；`apply_vdd_display_stage` 跳过拓扑应用；模式发布失败降级为警告继续（基地版是 `modes_failed` 中止），退出时校正显示状态。
- 优先级：**APP 配置 > 客户端显式请求 > 全局配置**。V+ 发 `customScreenMode=0` 且未配置 APP 方案时，最终行为由全局配置决定；配置了 APP 方案的入口由服务端覆盖。

## 控制面板子模块（GUI）

- `src_assets/common/sunshine-control-panel` 是子模块：`cainiao524/sunshine-control-panel`（fork 自 `qiin2333/sunshine-control-panel`）。
- 当前锁定 `8d41ca6a`（与上游正式版 `v2026.823.92127.杂鱼` 锁定的 GUI 一致）。
- fork 定制集中在 5 个文件：`desktop/views/StreamView.vue`（桌面显示方案 UI）、`desktop/utils/desktopDisplayProfile.js/.test.js`、`desktop/i18n/en.js`、`zh.js`。
- 上游 `main` 有 8 个未采用提交（8/23~8/24）：DualSense 控制器页像素风重构（#95）、组件下载复用更新镜像（#98）、离线安装容错（#99）、大屏模式（#97）、Material You 动态色（#100）等。升级需子模块 merge main → 解决 2 个 i18n 冲突 → 重建控制面板 → 全量回归（重点：桌面显示方案 + DualSense UI）。

## 当前状态快照（2026-08-25）

- HEAD：`9cedf8da`（`feature/app-display-profile`，已推送，与远端一致）
- v2.0 Release 已发布（安装版 + 便携版 + `SHA256SUMS.txt` + `checksums.json`）
- 上游正式版 `v2026.823.92127.杂鱼`（`adecdec1`）已同步；prerelease `v2026.824.154321` / `v2026.825.25620` 未同步（规则：只跟随正式版）
- `master` = 上游正式版镜像（`adecdec1`），`backup/app-display-profile-with-physical-current-4119d218` 为历史备份

## 验证与发布

网页改动至少运行 `npm run lint:webui`、`npm run test:webui` 和 `npm run build`。原生改动运行对应 Windows 原生测试或打包脚本，并执行 `git diff --check`。工作流改动还需通过 `actionlint` 和一次真实的 GitHub Actions 手动检查。

自动构建只生成操作页面产物；除非用户明确授权，不自动创建正式发布页、标签或签名文件。
