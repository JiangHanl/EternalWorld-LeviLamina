param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$compilerDir = Join-Path $projectRoot '.deps/llvm-mingw/llvm-mingw-20260922-ucrt-x86_64/bin'
$sqliteDir = Join-Path $projectRoot '.deps/sqlite/sqlite-amalgamation-3530400'
$domainDir = Join-Path $projectRoot 'modules/EternalCore/domain'
$outputDir = Join-Path $projectRoot 'artifacts/tests'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$sqliteObject = Join-Path $outputDir 'sqlite3.o'
& (Join-Path $compilerDir 'clang.exe') '-std=c11' '-O2' '-DSQLITE_THREADSAFE=1' '-DSQLITE_DQS=0' '-DSQLITE_ENABLE_API_ARMOR=1' '-c' (Join-Path $sqliteDir 'sqlite3.c') '-o' $sqliteObject
if ($LASTEXITCODE -ne 0) { throw 'SQLite compilation failed' }
$executable = Join-Path $outputDir 'core_tests.exe'
& (Join-Path $compilerDir 'clang++.exe') '-std=c++20' '-O2' '-Wall' '-Wextra' "-I$sqliteDir" "-I$domainDir" (Join-Path $domainDir 'Core.cpp') (Join-Path $domainDir 'Sha256.cpp') (Join-Path $projectRoot 'tests/EternalCore/core_tests.cpp') $sqliteObject '-o' $executable
if ($LASTEXITCODE -ne 0) { throw 'Core domain test compilation failed' }
$originalPath = $env:PATH
$originalDirectory = Get-Location
try {
    $env:PATH = $compilerDir + ';' + $originalPath
    Set-Location -LiteralPath $outputDir
    & $executable
    if ($LASTEXITCODE -ne 0) { throw 'Core domain tests failed' }
} finally {
    Set-Location -LiteralPath $originalDirectory.Path
    $env:PATH = $originalPath
}
