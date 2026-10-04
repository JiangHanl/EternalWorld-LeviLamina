param([switch]$SkipLLVM)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyRoot = Join-Path $projectRoot '.deps'
$lock = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'build-lock.json') | ConvertFrom-Json
New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
function Get-VerifiedArchive($record, [string]$directory) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $path = Join-Path $directory $record.file
    if (-not (Test-Path -LiteralPath $path)) { Invoke-WebRequest -Uri $record.url -OutFile $path }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.sha256) {
        throw "Dependency archive hash mismatch: $path"
    }
    return $path
}
$xmakeArchive = Get-VerifiedArchive $lock.xmake $dependencyRoot
$xmakeDirectory = Join-Path $dependencyRoot 'xmake'
if (-not (Test-Path -LiteralPath (Join-Path $xmakeDirectory 'xmake/xmake.exe'))) {
    Expand-Archive -LiteralPath $xmakeArchive -DestinationPath $xmakeDirectory -Force
}
$xmakeBin = Join-Path $xmakeDirectory 'xmake'
$env:PATH = $xmakeBin+';'+$env:PATH
if ($env:GITHUB_PATH) { $xmakeBin | Out-File -LiteralPath $env:GITHUB_PATH -Encoding utf8 -Append }
& (Join-Path $xmakeBin 'xmake.exe') '--version'
if ($LASTEXITCODE -ne 0) { throw 'XMake executable validation failed' }
$sqliteArchive = Get-VerifiedArchive $lock.sqlite $dependencyRoot
$sqliteDirectory = Join-Path $dependencyRoot 'sqlite'
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot ($lock.sqlite.source+'/sqlite3.c')))) {
    Expand-Archive -LiteralPath $sqliteArchive -DestinationPath $sqliteDirectory -Force
}
foreach ($repository in $lock.repositories) {
    $directory = Join-Path $dependencyRoot ('ci-repositories/'+$repository.name)
    $record = [pscustomobject]@{file=($repository.name+'.zip');url=$repository.url;sha256=$repository.sha256}
    $archive = Get-VerifiedArchive $record (Join-Path $dependencyRoot 'ci-repositories')
    if (-not (Test-Path -LiteralPath (Join-Path $directory ('xmake-repo-'+$repository.commit+'/packages')))) {
        Expand-Archive -LiteralPath $archive -DestinationPath $directory -Force
    }
}
if (-not $SkipLLVM) {
    $llvmArchive = Get-VerifiedArchive $lock.llvm $dependencyRoot
    $llvmDirectory = Join-Path $dependencyRoot 'LLVM22'
    if (-not (Test-Path -LiteralPath (Join-Path $llvmDirectory 'bin/clang-cl.exe'))) {
        $sevenZip = Join-Path $env:ProgramFiles '7-Zip/7z.exe'
        if (-not (Test-Path -LiteralPath $sevenZip)) { throw '7-Zip is required to extract the official LLVM .tar.xz archive' }
        $expanded = Join-Path $dependencyRoot 'llvm-extract'
        New-Item -ItemType Directory -Path $expanded,$llvmDirectory -Force | Out-Null
        & $sevenZip 'x' '-y' $llvmArchive ('-o'+$expanded) | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'LLVM XZ extraction failed' }
        $tar = Get-ChildItem -LiteralPath $expanded -Filter '*.tar' -File | Select-Object -First 1
        if (-not $tar) { throw 'LLVM XZ archive did not produce a TAR' }
        & tar.exe '-xf' $tar.FullName '-C' $llvmDirectory '--strip-components=1'
        if ($LASTEXITCODE -ne 0) { throw 'LLVM TAR extraction failed' }
    }
    $llvmBin = Join-Path $llvmDirectory 'bin'
    $env:PATH = $llvmBin+';'+$env:PATH
    if ($env:GITHUB_PATH) { $llvmBin | Out-File -LiteralPath $env:GITHUB_PATH -Encoding utf8 -Append }
    $env:LLVMInstallDir = $llvmDirectory
    if ($env:GITHUB_ENV) { ('LLVMInstallDir='+$llvmDirectory) | Out-File -LiteralPath $env:GITHUB_ENV -Encoding utf8 -Append }
    & (Join-Path $llvmBin 'clang-cl.exe') '--version'
    if ($LASTEXITCODE -ne 0) { throw 'LLVM compiler version check failed' }
}
Write-Output 'Prepared fixed official package repositories and SQLite; no server or DLL was launched.'
