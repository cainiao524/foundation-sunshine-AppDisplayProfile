param(
  [string]$BuildAlias = 'C:\CodexSunshineAppDisplay',
  [string]$BuildDirectory = 'build-package',
  [string]$PublisherName = 'cainiao524'
)

$ErrorActionPreference = 'Stop'
$sourceDirectory = Split-Path -Parent $PSScriptRoot
$sourcePath = (Resolve-Path -LiteralPath $sourceDirectory).Path
$msysBash = 'C:\msys64\usr\bin\bash.exe'
$innoCompiler = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'

if (-not (Test-Path -LiteralPath $msysBash)) {
  throw 'MSYS2 is not installed at C:\msys64.'
}
if (-not (Test-Path -LiteralPath $innoCompiler)) {
  throw 'Inno Setup 6 is not installed for the current user.'
}

if (-not (Test-Path -LiteralPath $BuildAlias)) {
  New-Item -ItemType Junction -Path $BuildAlias -Target $sourcePath | Out-Null
}
$aliasTarget = (Get-Item -LiteralPath $BuildAlias).Target
if ($aliasTarget -notcontains $sourcePath) {
  throw "Build alias already targets a different directory: $BuildAlias"
}

$msysSource = $BuildAlias.Replace('\', '/').Replace('C:', '/c')
$buildPath = Join-Path $BuildAlias $BuildDirectory
$stagingPath = Join-Path $buildPath 'inno_staging'
$installerOutputPath = Join-Path $buildPath 'inno_artifacts'
$portableOutputPath = Join-Path $buildPath 'portable_artifacts'
$artifacts = Join-Path $sourcePath 'artifacts'
$controlPanelPath = Join-Path $sourcePath 'src_assets\common\sunshine-control-panel'

foreach ($pathToClean in $stagingPath, $installerOutputPath, $portableOutputPath, $artifacts) {
  if (Test-Path -LiteralPath $pathToClean) {
    Remove-Item -LiteralPath $pathToClean -Recurse -Force
  }
}

Push-Location $sourcePath
try {
  npm.cmd ci
  if ($LASTEXITCODE -ne 0) { throw 'Web UI dependency installation failed.' }
  npm.cmd run lint:webui
  if ($LASTEXITCODE -ne 0) { throw 'Web UI lint failed.' }
  npm.cmd run test:webui
  if ($LASTEXITCODE -ne 0) { throw 'Web UI tests failed.' }

  New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
  $previousSourceAssets = $env:SUNSHINE_SOURCE_ASSETS_DIR
  $previousAssets = $env:SUNSHINE_ASSETS_DIR
  try {
    $env:SUNSHINE_SOURCE_ASSETS_DIR = Join-Path $sourcePath 'src_assets'
    $env:SUNSHINE_ASSETS_DIR = $buildPath
    npm.cmd run build
    if ($LASTEXITCODE -ne 0) { throw 'Web UI build failed.' }
  }
  finally {
    $env:SUNSHINE_SOURCE_ASSETS_DIR = $previousSourceAssets
    $env:SUNSHINE_ASSETS_DIR = $previousAssets
  }
}
finally {
  Pop-Location
}

$cargoCommand = Get-Command cargo -CommandType Application -ErrorAction SilentlyContinue
if ($null -eq $cargoCommand) {
  throw 'Rust Cargo is required to build the pinned Sunshine Control Panel.'
}

Push-Location $controlPanelPath
try {
  npm.cmd ci
  if ($LASTEXITCODE -ne 0) { throw 'Control Panel dependency installation failed.' }
  npm.cmd run build:renderer
  if ($LASTEXITCODE -ne 0) { throw 'Control Panel renderer build failed.' }
  npm.cmd run test:renderer
  if ($LASTEXITCODE -ne 0) { throw 'Control Panel renderer tests failed.' }
  & $cargoCommand.Source test --release --manifest-path .\src-tauri\Cargo.toml
  if ($LASTEXITCODE -ne 0) { throw 'Control Panel native tests failed.' }
  & $cargoCommand.Source build --release --manifest-path .\src-tauri\Cargo.toml
  if ($LASTEXITCODE -ne 0) { throw 'Control Panel native build failed.' }
}
finally {
  Pop-Location
}

$localGuiCandidates = @(
  (Join-Path $controlPanelPath 'src-tauri\target\release\sunshine-gui.exe'),
  (Join-Path $controlPanelPath 'src-tauri\target\x86_64-pc-windows-msvc\release\sunshine-gui.exe'),
  (Join-Path $controlPanelPath 'src-tauri\target\x86_64-pc-windows-gnu\release\sunshine-gui.exe')
)
$localGui = $localGuiCandidates |
  Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
  Get-Item |
  Sort-Object LastWriteTimeUtc -Descending |
  Select-Object -First 1
if ($null -eq $localGui) {
  throw 'Pinned Control Panel build did not produce sunshine-gui.exe.'
}

New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
& (Join-Path $PSScriptRoot 'test-prepare-ds5-sidecar-manifest.ps1')
if ($LASTEXITCODE -ne 0) { throw 'DualSense manifest tests failed.' }
$preparedManifest = Join-Path $buildPath 'ds5-sidecar-package.json'
& (Join-Path $PSScriptRoot 'prepare-ds5-sidecar-manifest.ps1') -OutputPath $preparedManifest
if ($LASTEXITCODE -ne 0) { throw 'DualSense manifest preparation failed.' }

& $msysBash -lc "set -euo pipefail; export PATH=/ucrt64/bin:/usr/bin; cd '$msysSource'; cmake -B '$BuildDirectory' -G Ninja -S . -DBUILD_DOCS=OFF -DBUILD_TRAY_TESTS=ON -DBUILD_WEB_UI=OFF -DSUNSHINE_ASSETS_DIR=assets -DFETCH_GUI=OFF -DSUNSHINE_PREFER_LOCAL_GUI=ON -DFETCH_DRIVER_DEPS=ON -DDRIVER_DEPS_REQUIRED=ON -DSUNSHINE_PUBLISHER_NAME='$PublisherName' -DSUNSHINE_PUBLISHER_WEBSITE='https://github.com/cainiao524/foundation-sunshine-AppDisplayProfile' -DSUNSHINE_PUBLISHER_ISSUE_URL='https://github.com/cainiao524/foundation-sunshine-AppDisplayProfile/issues'; ninja -C '$BuildDirectory' -j2; ctest --test-dir '$BuildDirectory' --output-on-failure; cmake --install '$BuildDirectory' --prefix '$BuildDirectory/inno_staging'"
if ($LASTEXITCODE -ne 0) { throw 'Native build or staging failed.' }

$stagedManifest = Join-Path $stagingPath 'tools\ds5-sidecar-package.json'
$stagedGui = Join-Path $stagingPath 'assets\gui\sunshine-gui.exe'
foreach ($requiredFile in $stagedManifest, $stagedGui) {
  if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
    throw "Required staged file is missing: $requiredFile"
  }
}
if ((Get-FileHash -LiteralPath $preparedManifest -Algorithm SHA256).Hash -cne
    (Get-FileHash -LiteralPath $stagedManifest -Algorithm SHA256).Hash) {
  throw 'Staged DualSense manifest differs from the verified upstream manifest.'
}
if ((Get-FileHash -LiteralPath $localGui.FullName -Algorithm SHA256).Hash -cne
    (Get-FileHash -LiteralPath $stagedGui -Algorithm SHA256).Hash) {
  throw 'Staged Sunshine Control Panel differs from the pinned local build.'
}

foreach ($outputPath in $installerOutputPath, $portableOutputPath) {
  if (Test-Path -LiteralPath $outputPath) {
    Remove-Item -LiteralPath $outputPath -Recurse -Force
  }
  New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
}

& $innoCompiler "/O$installerOutputPath" (Join-Path $buildPath 'sunshine_installer.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer packaging failed.' }

& 'C:\msys64\ucrt64\bin\cpack.exe' -G ZIP -B $portableOutputPath --config (Join-Path $buildPath 'CPackConfig.cmake')
if ($LASTEXITCODE -ne 0) { throw 'Portable package creation failed.' }

$portableVerifyPath = Join-Path $buildPath 'portable_verify'
if (Test-Path -LiteralPath $portableVerifyPath) {
  Remove-Item -LiteralPath $portableVerifyPath -Recurse -Force
}
Expand-Archive -LiteralPath (Join-Path $portableOutputPath 'Sunshine.zip') -DestinationPath $portableVerifyPath
$portableManifest = Join-Path $portableVerifyPath 'Sunshine\tools\ds5-sidecar-package.json'
$portableGui = Join-Path $portableVerifyPath 'Sunshine\assets\gui\sunshine-gui.exe'
foreach ($requiredFile in $portableManifest, $portableGui) {
  if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
    throw "Required portable file is missing: $requiredFile"
  }
}
if ((Get-FileHash -LiteralPath $preparedManifest -Algorithm SHA256).Hash -cne
    (Get-FileHash -LiteralPath $portableManifest -Algorithm SHA256).Hash) {
  throw 'Portable DualSense manifest differs from the verified upstream manifest.'
}
if ((Get-FileHash -LiteralPath $localGui.FullName -Algorithm SHA256).Hash -cne
    (Get-FileHash -LiteralPath $portableGui -Algorithm SHA256).Hash) {
  throw 'Portable Sunshine Control Panel differs from the pinned local build.'
}

New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$installerArtifact = Join-Path $artifacts 'Sunshine-AppDisplayProfile-WindowsInstaller.exe'
$portableArtifact = Join-Path $artifacts 'Sunshine-AppDisplayProfile-WindowsPortable.zip'
Copy-Item -LiteralPath (Join-Path $installerOutputPath 'Sunshine.exe') -Destination $installerArtifact
Copy-Item -LiteralPath (Join-Path $portableOutputPath 'Sunshine.zip') -Destination $portableArtifact

& (Join-Path $PSScriptRoot 'generate-checksums.ps1') -Path $artifacts -Output 'SHA256SUMS.txt'
if ($LASTEXITCODE -ne 0) { throw 'Checksum generation failed.' }

$expectedArtifacts = @(
  'Sunshine-AppDisplayProfile-WindowsInstaller.exe',
  'Sunshine-AppDisplayProfile-WindowsPortable.zip',
  'SHA256SUMS.txt',
  'checksums.json'
)
$actualArtifacts = @(Get-ChildItem -LiteralPath $artifacts -File | Select-Object -ExpandProperty Name)
$unexpectedArtifacts = @($actualArtifacts | Where-Object { $_ -notin $expectedArtifacts })
$missingArtifacts = @($expectedArtifacts | Where-Object { $_ -notin $actualArtifacts })
if ($unexpectedArtifacts.Count -gt 0 -or $missingArtifacts.Count -gt 0) {
  throw "Unexpected package artifact set. Missing: $($missingArtifacts -join ', '); unexpected: $($unexpectedArtifacts -join ', ')"
}

Get-ChildItem -LiteralPath $artifacts
