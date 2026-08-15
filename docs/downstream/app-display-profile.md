# APP 显示方案维护说明

## 目标

显示方案绑定在 Sunshine 服务端 APP 配置中。普通 Moonlight 只需启动 APP，显示目标、拓扑、分辨率、刷新率和断开行为由服务端应用方案决定。

## 兼容原则

- 未配置 `display-target` 的 APP 完全跟随全局设置和客户端请求。
- 配置了 `display-target` 的 APP 在显示准备前覆盖客户端显示目标和布局请求。
- 不新增显示器创建、拓扑切换或驱动管理实现，继续复用基地版显示流程。
- APP 显示器组合直接复用主设置的 `DisplayPreparationPicker.vue` 五种布局。

## 配置字段

| 字段 | 可用值 | 含义 |
| --- | --- | --- |
| `display-target` | 空、`virtual`、`physical` | 跟随全局、基地虚拟显示器、指定物理显示器 |
| `display-device-prep` | `no_operation`、`ensure_active`、`ensure_primary`、`ensure_secondary`、`ensure_only_display` | 复用基地版显示准备动作 |
| `display-resolution-mode` | 空、`client`、`fixed` | 跟随全局、客户端请求或固定分辨率 |
| `display-resolution` | 例如 `1920x1080` | 固定分辨率值 |
| `display-refresh-rate-mode` | 空、`client`、`fixed` | 跟随全局、客户端请求或固定刷新率 |
| `display-refresh-rate` | 例如 `60` | 固定刷新率值 |
| `display-output-name` | 显示器编号或名称 | `physical` 方案的目标显示器 |
| `display-disconnect-action` | `keep`、`restore` | 断开后保持或恢复显示状态 |

普通物理显示器保持客户端分辨率和刷新率、并且不改变拓扑时，使用：

```json
{
  "display-target": "physical",
  "display-device-prep": "no_operation",
  "display-resolution-mode": "client",
  "display-refresh-rate-mode": "client"
}
```

## Foundation Desktop

Foundation Desktop 只编辑内置 `Desktop` APP 的上述字段，不复制 Sunshine 显示后端。自动适配基地版 Moonlight 时删除这些字段，使客户端和全局设置继续生效；强制方案则通过 APP 编号由 Sunshine 服务端执行，普通 Moonlight 同样可用。

## 调用顺序

1. 解析标准 GameStream 启动参数。
2. 根据 APP 编号读取服务端配置。
3. 调用 `apply_app_display_profile()` 覆盖启动会话参数。
4. 进入基地版显示准备和编码器探测流程。
5. 启动或恢复 APP，并按断开策略处理显示状态。

## VDD 标识

虚拟显示器身份始终使用基地版原生规则：

```cpp
config::video.vdd_reuse ? "shared_vdd" : client_id
```

不得按 APP 或客户端组合生成新的虚拟屏身份。

## 验证范围

- 无 APP 方案时保留客户端显示请求。
- 虚拟主屏、副屏和仅虚拟屏方案覆盖客户端布局。
- 普通物理显示器和不存在的显示器编号处理正确。
- `no_operation`、分辨率、刷新率和断开恢复字段正确保存和执行。
- APP 编辑器复用主设置显示组件，窄窗口和主题下无布局问题。
- 控制面板只写入现有 `display-*` 字段，不写入已删除的身份字段。
