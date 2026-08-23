[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ReleaseTag,

  [Parameter(Mandatory = $true)]
  [string]$ReleaseCommit,

  [string]$ChecksumsPath = 'artifacts/checksums.json',

  [string]$UpstreamReleasePath = '.github/upstream-stable-release',

  [string]$OutputPath = 'release-notes.md'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not (Test-Path -LiteralPath $ChecksumsPath -PathType Leaf)) {
  throw "Checksum metadata not found: $ChecksumsPath"
}

if (-not (Test-Path -LiteralPath $UpstreamReleasePath -PathType Leaf)) {
  throw "Upstream release state not found: $UpstreamReleasePath"
}

$upstreamRelease = (Get-Content -LiteralPath $UpstreamReleasePath -Raw).Trim()
if ([string]::IsNullOrWhiteSpace($upstreamRelease)) {
  throw "Upstream release state is empty: $UpstreamReleasePath"
}

$checksumEntries = @(Get-Content -LiteralPath $ChecksumsPath -Raw | ConvertFrom-Json)
$installer = @($checksumEntries | Where-Object { $_.File -like '*.WindowsInstaller.exe' })
$portable = @($checksumEntries | Where-Object { $_.File -like '*.Portable-x64.zip' })

if ($installer.Count -ne 1 -or $portable.Count -ne 1) {
  throw 'Release notes require exactly one Windows installer and one portable package.'
}

$packageEntries = @($installer[0], $portable[0])
$checksumLines = @(
  $packageEntries | ForEach-Object {
    '{0}  {1}' -f ([string]$_.Hash).ToLowerInvariant(), $_.File
  }
)

$lines = @(
  "## Sunshine App Display Profile $ReleaseTag"
  ''
  ('本版基于 Foundation Sunshine 正式版 `{0}`，并保留按应用指定显示目标、拓扑、分辨率、刷新率、 HDR 与断开恢复策略的定制能力。' -f $upstreamRelease)
  ''
  '### 项目能力'
  ''
  '- 配置应用显示方案时，由服务端覆盖客户端的显示目标和布局，并可按配置采用客户端分辨率、刷新率与 HDR 请求。'
  '- 未配置应用显示方案的 Desktop、Steam 和其他入口，继续遵循 Moonlight 客户端及全局显示请求。'
  '- 保留同一 Moonlight 客户端通过“退出运行中的应用并运行此应用”切换不同应用显示方案的会话上下文。'
  '- 虚拟显示器继续复用基地版身份、创建、拓扑切换与恢复流程。'
  ''
  '### 显示行为'
  ''
  '- `ensure_secondary`：物理显示器保持主屏，虚拟显示器作为副屏。'
  '- `ensure_primary`：虚拟显示器作为主屏，物理显示器保持活动副屏。'
  '- `ensure_only_display`：只保留目标虚拟显示器。'
  '- Moonlight 仅断开串流连接时，按应用配置决定是否保持当前显示状态。'
  '- 从 Moonlight 关闭运行中的应用时，按应用配置恢复创建虚拟显示器前的物理显示拓扑。'
  ''
  '### 安装'
  ''
  ('推荐下载 `{0}` 并直接覆盖安装，无需预先卸载旧版本或虚拟显示器驱动。' -f $installer[0].File)
  ''
  ('需要便携部署时，可使用 `{0}`。' -f $portable[0].File)
  ''
  '### 验证'
  ''
  '发布页只会在网页检查、虚拟显示器辅助脚本冒烟测试、 Windows 原生编译与测试、安装版和便携版打包、产物校验全部成功后创建。'
  ''
  '### SHA256'
  ''
  '```text'
  $checksumLines
  '```'
  ''
  ('上游基线：`{0}`  ' -f $upstreamRelease)
  ('发布提交：`{0}`' -f $ReleaseCommit)
  ''
  '### 版本变更'
  ''
  '以下变更记录由 GitHub 根据自上一个正式版本以来的提交自动生成。'
)

$outputDirectory = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
  [System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
}

$content = ($lines -join "`n") + "`n"
[System.IO.File]::WriteAllText(
  [System.IO.Path]::GetFullPath($OutputPath),
  $content,
  [System.Text.UTF8Encoding]::new($false)
)

Write-Host "Release notes saved to: $OutputPath"
