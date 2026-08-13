# APP 显示方案维护说明

## 目标

让显示方式绑定在 Sunshine 服务端的 APP 配置中。普通 Moonlight 客户端只需启动 APP，无须实现基地版扩展参数；客户端仍负责提交本次连接的宽度、高度和帧率。

## 兼容原则

- 没有 `display-target` 的旧 APP 等同于“跟随全局”，行为与上游一致。
- APP 强制方案拥有最高优先级；客户端扩展参数只在“跟随全局”时生效。
- 不另建显示器实现。虚拟屏创建、显示拓扑、模式更新和恢复仍调用基地版现有显示系统。
- 修改集中在 APP 配置解析、启动会话注入和编辑界面，尽量避免侵入底层显示代码。

## 配置字段

| 字段 | 值 | 说明 |
| --- | --- | --- |
| `display-target` | 空、`virtual`、`physical` | 跟随全局、强制虚拟屏、强制物理屏 |
| `display-device-prep` | `ensure_active`、`ensure_primary`、`ensure_secondary`、`ensure_only_display` | 直接对应基地版现有显示准备枚举 |
| `display-resolution-mode` | 空、`client`、`fixed` | 跟随全局、跟随当前客户端、固定值 |
| `display-resolution` | 例如 `1920x1080` | 固定分辨率 |
| `display-refresh-rate-mode` | 空、`client`、`fixed` | 跟随全局、跟随当前客户端、固定值 |
| `display-refresh-rate` | 例如 `60` | 固定刷新率 |
| `display-vdd-identity` | 空、`app`、`app-client` | 跟随全局、APP 共用、APP 与客户端分别独立 |
| `display-output-name` | 显示设备编号 | 强制物理屏时可选 |
| `display-disconnect-action` | `keep`、`restore` | 断开后保留等待恢复，或立即恢复物理拓扑 |

## 调用顺序

启动和恢复连接都必须遵守：

1. 解析普通 GameStream 启动参数。
2. 根据 APP 编号取得配置。
3. 调用 `apply_app_display_profile()` 覆盖启动会话。
4. 调用现有 `prepare_display_and_probe_encoders()`。
5. 启动或恢复 APP。

不能把显示方案放进 APP 前置命令，因为显示器准备发生在前置命令之前。

动态分辨率调整也必须重新应用 APP 方案，避免活动显示器探测覆盖强制目标。

## 参考项目

- Vibepollo：参考 APP 级虚拟屏模式和布局的数据模型，以及在显示准备前解析 APP 的顺序。
- Apollo：参考按 APP、按 APP 与客户端生成稳定虚拟屏身份的思路。
- 本项目不移植它们的显示助手或驱动体系。

## 上游同步

建议保留三类提交：配置和后端、界面和翻译、测试和文档。同步上游后重点检查：

- `src/nvhttp.cpp` 中启动和恢复连接的显示准备顺序。
- `src/process.*` 的 APP 字段解析与保存。
- `src/display_device/parsed_config.cpp` 的显示意图和分辨率解析。
- `src/display_device/session.cpp` 的虚拟屏身份生成。
- `src/stream.cpp` 的最后客户端断开和动态分辨率路径。
- `AppEditor.vue` 是否仍使用上游表单组件和主题变量。

## 已知限制

- 同一个 Moonlight 设备通过“退出运行中的应用并运行此应用”切换显示方案已经验证可用。
- 当一个 Moonlight 设备仍在串流时，另一个设备直接退出当前应用或启动不同应用，客户端第一次操作仍可能报错；再次启动通常可以正常连接。
- 当前版本不尝试在 `/resume` 请求中自动终止其他设备的会话并切换应用，以免把未经验证的跨设备接管逻辑带入稳定功能。

## 验证清单

- 旧 APP 不增加字段并保持原行为。
- 强制虚拟副屏、虚拟主屏、仅虚拟屏。
- 强制物理屏和不存在的物理屏编号。
- iPad 启动后由手机重新启动或恢复，分辨率和刷新率取新客户端请求。
- 断开保留与断开恢复两种策略。
- APP 正常退出、启动失败、Sunshine 停止时恢复原显示拓扑。
- 明暗主题、窄窗口和高缩放下没有横向溢出。
