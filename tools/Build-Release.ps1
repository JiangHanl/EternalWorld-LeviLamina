param([string]$Xmake = 'xmake')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $projectRoot
try {
    & $Xmake 'f' '-p' 'windows' '-a' 'x64' '-m' 'release' '-y'
    if ($LASTEXITCODE -ne 0) { throw 'Official XMake release configuration failed' }
    & $Xmake 'build' '-v'
    if ($LASTEXITCODE -ne 0) { throw 'Official XMake release build failed' }
} finally { Pop-Location }
