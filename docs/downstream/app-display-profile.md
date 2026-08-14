# APP 显示方案维护说明

## 目标

让显示方式绑定在 Sunshine 服务端的 APP 配置中。普通 Moonlight 客户端只需启动 APP，无须实现基地版扩展参数；通常由客户端提交本次连接的宽度、高度和帧率，“原样串流当前物理显示器”模式除外。

## 兼容原则

- 没有 `display-target` 的旧 APP 等同于“跟随全局”，行为与上游一致。
- APP 强制方案拥有最高优先级；客户端扩展参数只在“跟随全局”时生效。
- 不另建显示器实现。虚拟屏创建、显示拓扑、模式更新和恢复仍调用基地版现有显示系统。
- 修改集中在 APP 配置解析、启动会话注入和编辑界面，尽量避免侵入底层显示代码。
- `physical-current` 是只读显示模式：不得调用显示配置、恢复或 VDD 回退路径。

## 配置字段

| 字段 | 值 | 说明 |
| --- | --- | --- |
| `display-target` | 空、`virtual`、`physical`、`physical-current` | 跟随全局、强制虚拟屏、强制物理屏、原样串流当前物理屏 |
| `display-device-prep` | `ensure_active`、`ensure_primary`、`ensure_secondary`、`ensure_only_display` | 直接对应基地版现有显示准备枚举 |
| `display-resolution-mode` | 空、`client`、`fixed` | 跟随全局、跟随当前客户端、固定值 |
| `display-resolution` | 例如 `1920x1080` | 固定分辨率 |
| `display-refresh-rate-mode` | 空、`client`、`fixed` | 跟随全局、跟随当前客户端、固定值 |
| `display-refresh-rate` | 例如 `60` | 固定刷新率 |
| `display-vdd-identity` | 空、`app`、`app-client` | 跟随全局、APP 共用、APP 与客户端分别独立 |
| `display-output-name` | 显示设备编号 | 强制物理屏或原样串流物理屏时可选 |
| `display-disconnect-action` | `keep`、`restore` | 断开后保留等待恢复，或立即恢复物理拓扑 |

## 原样串流当前物理显示器

`display-target: physical-current` 的固定行为：

- 留空 `display-output-name` 时，优先选择当前已启用的主物理显示器；没有主物理屏时选择一个已启用物理屏。
- 填写 `display-output-name` 时，只接受该设备编号、系统显示名或友好名称对应的已启用物理屏；不存在、未启用或指向 ZakoVDD 时直接报错，不回退。
- 启动预检只枚举设备、读取当前显示模式并精确探测编码器，不调用 `configure_display()`。
- RTSP `ANNOUNCE` 时重新读取一次当前模式，用它覆盖客户端宽度、高度和帧率，并锁定精确捕获目标。
- 全局捕获后端为 `vdd` 时，本会话改用 `ddx` 捕获物理屏；不会创建、启用或复用 VDD。
- 忽略串流中的客户端动态分辨率和帧率变更；不执行断开恢复或取消恢复。
- 保存配置时清除布局、分辨率、刷新率、VDD 身份和断开恢复字段，只保留目标和可选物理屏编号。

## Foundation Desktop 入口

基地自带的 Foundation Desktop“串流配置”页面为内置 `Desktop` APP 提供完整显示方案编辑：

- 自动适配基地版 Moonlight。这是默认值，不保存 `display-target`，因此基地版客户端的 `useVdd`、`customScreenMode`、`customVddScreenMode` 和显示器名称等本次连接参数仍然有效；普通 Moonlight 没有扩展参数时继续使用全局设置。
- 原样串流当前物理屏。
- 强制物理屏或基地虚拟屏，并可分别调整显示布局、分辨率模式、固定分辨率、刷新率模式、固定刷新率、虚拟屏身份、目标物理屏和断开策略。
- 遇到当前版本不认识的新字段值时保留原始自定义方案，禁止静默降级或覆盖。

这个入口只编辑 `apps.json` 中 `Desktop` APP 的同一组 `display-*` 字段，不另建显示后端。选择“自动适配基地版 Moonlight”时删除这些字段，保持客户端参数和上游全局回退路径。所有强制方案仍由 Sunshine 服务端按 APP 编号应用，因此普通 Moonlight 也可以使用。

Foundation Desktop 保存前必须重新读取最新 APP 列表，只替换当前 `Desktop` 项，避免页面长时间打开后用旧列表覆盖应用管理中的新修改。原有菜单命令、分离启动命令和自动打开 Desktop UI 的行为必须保留。

## Moonlight 系列兼容矩阵

| 客户端 | 显示目标参数 | 布局参数 | 服务端行为 |
| --- | --- | --- | --- |
| 标准 Moonlight、VoidLink | 无基地扩展参数 | 无基地扩展参数 | APP 强制方案生效；未配置 APP 时跟随全局设置 |
| Moonlight V+ | `useVdd` 或 `display_name` | `customScreenMode` | 自动适配和 APP 强制方案均完整支持 |
| Moonlight macOS Enhanced | `useVdd` 或 `display_name` | `customScreenMode` | 自动适配和 APP 强制方案均完整支持 |
| Foundation Moonlight PC | 当前版本不发送本次选择的物理屏、VDD 目标 | 同时发送 `customScreenMode` 和 `customVddScreenMode` | 服务端根据实际目标分别使用物理屏或 VDD 布局；显示目标由 APP 强制方案或主机全局设置确定 |

Foundation Moonlight PC 的显示器选择界面目前只修改两套布局偏好，没有把本次选中的显示器编号或 `useVdd` 传入启动请求。两套偏好都设置后，服务端不能从请求中反推出本次点击的是哪一类显示器。因此本 Fork 不猜测目标、不规定含糊参数的优先级；需要由电脑版客户端发送 `useVdd=0/1` 或 `display_name` 后，才能让“自动适配”模式仅凭客户端选择完整切换目标。在客户端完成该修正前，可使用两个按 APP 编号强制指定物理屏和 VDD 的入口，标准 Moonlight 也同样可用。

## 调用顺序

启动和恢复连接都必须遵守：

1. 解析普通 GameStream 启动参数。
2. 根据 APP 编号取得配置。
3. 调用 `apply_app_display_profile()` 覆盖启动会话。
4. 调用 `prepare_display_and_probe_encoders()`；`physical-current` 在其中走只读物理屏分支，其余模式使用现有显示准备流程。
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
- `src/nvhttp_stream_start.cpp` 的只读物理屏预检是否仍绕过显示配置和全部恢复路径。
- `src/process.*` 的 APP 字段解析与保存。
- `src/display_device/parsed_config.cpp` 的显示意图和分辨率解析。
- `src/display_device/session.cpp` 的虚拟屏身份生成。
- `src/rtsp.cpp`、`src/video.*` 的握手模式覆盖、精确捕获目标和禁止显示回退。
- `src/stream.cpp` 的最后客户端断开和动态分辨率、帧率路径。
- `AppEditor.vue` 是否仍使用上游表单组件和主题变量。
- `src_assets/common/sunshine-control-panel` 子模块中的 `StreamView.vue` 和 `desktopDisplayProfile.js` 是否仍复用相同字段，并在保存前读取最新 APP 列表。

## 已验证范围与项目边界

- 同一个 Moonlight 设备通过“退出运行中的应用并运行此应用”切换显示方案已经验证可用。
- 跨设备直接接管完全不在项目范围内，不分析、不实现、不测试，也不作为待办。不得在 `/resume` 或 APP 启停路径中加入自动终止其他设备会话的逻辑。

## 验证清单

- 旧 APP 不增加字段并保持原行为。
- 强制虚拟副屏、虚拟主屏、仅虚拟屏。
- 强制物理屏和不存在的物理屏编号。
- 原样串流当前物理屏时，编码尺寸和帧率等于主机当前模式，Moonlight 设置不变，主机拓扑、模式和 VDD 数量均不变。
- 原样串流指定屏不存在、未启用或指向 VDD 时直接失败，且不会捕获其他屏幕。
- 同一 Moonlight 设备重新启动或恢复 APP 时，分辨率和刷新率取本次客户端请求。
- 断开保留与断开恢复两种策略。
- APP 正常退出、启动失败、Sunshine 停止时恢复原显示拓扑。
- 明暗主题、窄窗口和高缩放下没有横向溢出。
