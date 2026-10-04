param([string]$Xmake = 'xmake')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$lock = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'build-lock.json') | ConvertFrom-Json
$env:XMAKE_MAIN_REPO = Join-Path $projectRoot $lock.recipe_repository.path
if (-not (Test-Path -LiteralPath (Join-Path $env:XMAKE_MAIN_REPO 'packages/l/levilamina/xmake.lua'))) { throw 'Run Prepare-CI.ps1 before the official build' }
Push-Location -LiteralPath $projectRoot
try {
    $diagnostics = Join-Path $projectRoot 'artifacts/ci'
    New-Item -ItemType Directory -Path $diagnostics -Force | Out-Null
    & $Xmake 'f' '-p' 'windows' '-a' 'x64' '-m' 'release' '-y' '-vD' 2>&1 | Tee-Object -FilePath (Join-Path $diagnostics 'configure.log')
    if ($LASTEXITCODE -ne 0) { throw 'Official XMake release configuration failed' }
    & $Xmake 'build' '-vD' 2>&1 | Tee-Object -FilePath (Join-Path $diagnostics 'build.log')
    if ($LASTEXITCODE -ne 0) { throw 'Official XMake release build failed' }
} finally { Pop-Location }
