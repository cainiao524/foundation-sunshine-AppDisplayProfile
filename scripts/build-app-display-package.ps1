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

& $msysBash -lc "set -euo pipefail; export PATH=/ucrt64/bin:/usr/bin; cd '$msysSource'; cmake -B '$BuildDirectory' -G Ninja -S . -DBUILD_DOCS=OFF -DBUILD_TRAY_TESTS=ON -DBUILD_WEB_UI=OFF -DSUNSHINE_ASSETS_DIR=assets -DFETCH_DRIVER_DEPS=ON -DDRIVER_DEPS_REQUIRED=ON -DSUNSHINE_PUBLISHER_NAME='$PublisherName' -DSUNSHINE_PUBLISHER_WEBSITE='https://github.com/cainiao524/foundation-sunshine-AppDisplayProfile' -DSUNSHINE_PUBLISHER_ISSUE_URL='https://github.com/cainiao524/foundation-sunshine-AppDisplayProfile/issues'; ninja -C '$BuildDirectory' -j2; ctest --test-dir '$BuildDirectory' --output-on-failure; cmake --install '$BuildDirectory' --prefix '$BuildDirectory/inno_staging'"
if ($LASTEXITCODE -ne 0) { throw 'Native build or staging failed.' }

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
