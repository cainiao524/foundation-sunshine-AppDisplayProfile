## Sunshine App Display Profile {{RELEASE_TAG}}

本版基于 Foundation Sunshine 正式版 `{{UPSTREAM_RELEASE}}`，并保留按应用指定显示目标、拓扑、分辨率、刷新率、 HDR 与断开恢复策略的定制能力。

### 项目能力

- 配置应用显示方案时，由服务端覆盖客户端的显示目标和布局，并可按配置采用客户端分辨率、刷新率与 HDR 请求。
- 未配置应用显示方案的 Desktop、Steam 和其他入口，继续遵循 Moonlight 客户端及全局显示请求。
- 保留同一 Moonlight 客户端通过“退出运行中的应用并运行此应用”切换不同应用显示方案的会话上下文。
- 虚拟显示器继续复用基地版身份、创建、拓扑切换与恢复流程。
- Windows 安装包固定使用仓库锁定的控制面板，并校验上游正式版 DualSense 组件清单、下载地址、大小与摘要。

### 显示行为

- `ensure_secondary`：物理显示器保持主屏，虚拟显示器作为副屏。
- `ensure_primary`：虚拟显示器作为主屏，物理显示器保持活动副屏。
- `ensure_only_display`：只保留目标虚拟显示器。
- Moonlight 仅断开串流连接时，按应用配置决定是否保持当前显示状态。
- 从 Moonlight 关闭运行中的应用时，按应用配置恢复创建虚拟显示器前的物理显示拓扑。

### 安装

推荐下载 `{{INSTALLER_FILE}}` 并直接覆盖安装，无需预先卸载旧版本或虚拟显示器驱动。

需要便携部署时，可使用 `{{PORTABLE_FILE}}`。

首次启用 DualSense 模拟时，控制面板会从已同步的上游正式发布下载并校验可选组件。

### 验证

发布页只会在网页检查、控制面板渲染器与原生测试、DualSense 组件清单校验、虚拟显示器辅助脚本冒烟测试、 Windows 原生编译与测试、安装版和便携版打包、产物校验全部成功后创建。

### SHA256

```text
{{CHECKSUM_LINES}}
```

上游基线：`{{UPSTREAM_RELEASE}}`<br>
发布提交：`{{RELEASE_COMMIT}}`

### 版本变更

以下变更记录由 GitHub 根据自上一个正式版本以来的提交自动生成。
