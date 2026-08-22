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

## 验证与发布

网页改动至少运行 `npm run lint:webui`、`npm run test:webui` 和 `npm run build`。原生改动运行对应 Windows 原生测试或打包脚本，并执行 `git diff --check`。工作流改动还需通过 `actionlint` 和一次真实的 GitHub Actions 手动检查。

自动构建只生成操作页面产物；除非用户明确授权，不自动创建正式发布页、标签或签名文件。
