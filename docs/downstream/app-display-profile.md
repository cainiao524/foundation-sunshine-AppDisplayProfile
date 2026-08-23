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
| `display-resolution-mode` | 空、`no_operation`、`client`、`fixed` | 跟随全局、保持当前分辨率、客户端请求或固定分辨率 |
| `display-resolution` | 例如 `1920x1080` | 固定分辨率值 |
| `display-refresh-rate-mode` | 空、`no_operation`、`client`、`fixed` | 跟随全局、保持当前刷新率、客户端请求或固定刷新率 |
| `display-refresh-rate` | 例如 `60` | 固定刷新率值 |
| `display-dynamic-resolution-follow-display` | 空、`enabled`、`disabled` | 跟随全局、启用或禁用串流中跟随主机分辨率变化 |
| `display-hdr-policy` | 空、`ignore_client`、`client`、`forced` | 跟随全局、忽略客户端 HDR 请求、响应客户端请求或强制状态 |
| `display-hdr-state` | `enabled`、`disabled` | `forced` 策略下强制启用或禁用 HDR；其他策略下不保存 |
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

需要保持主机当前分辨率和刷新率、不接受客户端显示模式请求时，使用：

```json
{
  "display-resolution-mode": "no_operation",
  "display-refresh-rate-mode": "no_operation"
}
```

动态分辨率字段只选择基地版现有行为，不重新实现编码器重建或客户端通知。字段缺失时继续使用全局 `dynamic_resolution_follow_display`。

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

## 多适配器和多显示器不变量

- 创建 VDD 前保存完整物理拓扑、镜像分组、主屏、显示模式和 HDR 状态；VDD 只能作为独立拓扑组追加、前置或独占，不能拆散或重排物理显示器组。
- 笔记本内屏因核显、独显路径切换而更换设备 ID 时，只能通过唯一的物理显示器身份映射；映射缺失或有歧义时安全失败，不能猜测另一台显示器。
- APP 已指定显示目标时，捕获枚举尚未发布该输出必须限时等待，不能回退到默认物理屏，否则会形成跨适配器捕获与编码错配。
- 控制连接断开按 APP 策略保持当前显示状态，并保留首次创建 VDD 前的基线供恢复连接使用；明确关闭运行中的 APP 才恢复该基线并清理会话状态。

## 验证范围

- 无 APP 方案时保留客户端显示请求。
- 虚拟主屏、副屏和仅虚拟屏方案覆盖客户端布局。
- 普通物理显示器和不存在的显示器编号处理正确。
- `no_operation`、分辨率、刷新率和断开恢复字段正确保存和执行。
- APP 编辑器复用主设置显示组件，窄窗口和主题下无布局问题。
- 控制面板只写入现有 `display-*` 字段，不写入已删除的身份字段。
