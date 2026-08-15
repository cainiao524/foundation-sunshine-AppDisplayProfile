# 本 Fork 的项目级开发指令

本仓库不是原版 Foundation Sunshine 的纯镜像。开始任何开发、同步、构建或发布工作前，必须完整阅读：

1. [`docs/downstream/project-handoff.md`](docs/downstream/project-handoff.md)
2. [`docs/downstream/app-display-profile.md`](docs/downstream/app-display-profile.md)

## 不可偏离的方向

- 核心定制是“按应用显示方案”：显示目标、拓扑、分辨率、刷新率和断开恢复策略由 Sunshine 服务端的 APP 配置决定。
- 上游同步只跟随 `AlkaidLab/foundation-sunshine` 的 GitHub 正式发布；草稿版、预发布版和 `master` 开发提交都不是新的同步目标。
- 跨设备直接接管完全不在项目范围内。不要分析、实现、恢复或测试自动终止另一台 Moonlight 设备会话的逻辑，也不要把它列为待办。
- 需要保证同一 Moonlight 设备通过“退出运行中的应用并运行此应用”在不同 APP 显示方案间切换。
- 当前不开展自有代码签名或驱动重新签名。不得替换、修改或伪造上游随包驱动的签名文件。

## 分支与安全边界

- GitHub 默认分支和实际开发分支：`feature/app-display-profile`。
- `origin` 应指向 `cainiao524/foundation-sunshine`；`upstream` 应指向 `AlkaidLab/foundation-sunshine`。
- `master` 是上游镜像分支，不用于开发定制功能。由于历史上同步过开发版源码，迁移期间禁止为了对齐旧正式版而回退或强推它。
- 上游合并发生冲突时必须停止并人工处理；禁止强制覆盖功能分支。
- 不得覆盖已有标签或正式发布。创建标签、正式发布页、签名、推送或改变发布策略前，确认用户已经明确授权。
- 工作区若有来源不明的改动，先确认范围，禁止把无关文件一起提交。

## 修改与验证要求

- 尽量保持定制集中在 APP 解析、启动会话覆盖和 APP 编辑界面，复用基地版已有显示器实现。
- 上游同步后重点检查 `src/nvhttp.cpp`、`src/process.*`、`src/display_device/*`、`src/stream.cpp` 和 `AppEditor.vue`。
- 网页改动至少运行 `npm run lint:webui`、`npm run test:webui` 和 `npm run build`。
- 原生或打包改动使用 `scripts/build-app-display-package.ps1` 或等价云端流水线验证，并运行原生测试。
- 工作流改动必须通过 `actionlint` 和 `git diff --check`，再进行一次真实的 GitHub Actions 手动检查。
- 自动构建只负责生成“操作”页面产物；除非用户另行授权，不自动创建正式发布页。

## 上游正式版同步与云端构建操作

处理上游同步时必须由用户手动触发 `.github/workflows/sync-upstream-build.yml`，不要手工把上游 `master` 合并到功能分支。禁止恢复定时计划或在无人确认时自动合并新的上游正式版。

同步流程必须保持以下顺序：

1. 调用 `AlkaidLab/foundation-sunshine` 的 `releases/latest`，只接受 GitHub 最新正式发布。
2. 只获取该正式版标签和提交，不把上游开发分支作为同步目标。
3. 仅在能够快进时更新 Fork 的 `master` 镜像；禁止回退或强推。
4. 把正式版提交正常合并到 `feature/app-display-profile`。发生冲突时立即失败并要求人工处理。
5. 推送合并结果和 `.github/upstream-stable-release` 状态记录。
6. 把合并后的精确提交号传给可复用的 `.github/workflows/main.yml`，等待网页测试、VDD 冒烟测试、Windows 原生编译、原生测试和打包全部完成。
7. 只上传安装版、便携版、`SHA256SUMS.txt` 和 `checksums.json` 到 GitHub Actions 产物。未经用户明确授权，不创建标签或 Release。

修改工作流后必须先在本地运行：

```powershell
actionlint .github/workflows/main.yml .github/workflows/sync-upstream-build.yml
git diff --check
```

随后必须进行一次真实云端检查：

```powershell
gh workflow run sync-upstream-build.yml `
  -R cainiao524/foundation-sunshine `
  --ref feature/app-display-profile `
  -f force-build=true

gh run list `
  -R cainiao524/foundation-sunshine `
  --workflow sync-upstream-build.yml `
  --event workflow_dispatch `
  --limit 1

gh run watch <运行编号> `
  -R cainiao524/foundation-sunshine `
  --exit-status
```

只有整次同步工作流成功且 Windows 任务完成产物上传，才可以报告云端构建验证通过。只看到“同步分支”任务成功不算验证完成。不要通过重复触发工作流来绕过失败；应读取失败步骤日志并修复实际问题。
