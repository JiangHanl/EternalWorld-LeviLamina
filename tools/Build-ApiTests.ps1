param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $projectRoot '.deps/llvm-mingw/llvm-mingw-20260922-ucrt-x86_64/bin/clang++.exe'
$nativeDir = Join-Path $projectRoot 'modules/EternalCore/api'
$sdkInclude = Join-Path $projectRoot 'sdk/EternalSDK/include'
$outputDir = Join-Path $projectRoot 'artifacts/tests'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$executable = Join-Path $outputDir 'api_service_test.exe'
& $compiler '-std=c++20' '-O2' '-Wall' '-Wextra' '-DETERNAL_CORE_BUILD' "-I$nativeDir" "-I$sdkInclude" (Join-Path $nativeDir 'ApiService.cpp') (Join-Path $projectRoot 'tests/EternalCore/api_service_test.cpp') '-o' $executable
if ($LASTEXITCODE -ne 0) { throw 'API service test compilation failed' }
$originalPath = $env:PATH
try {
    $env:PATH = (Split-Path -Parent $compiler) + ';' + $originalPath
    & $executable
    if ($LASTEXITCODE -ne 0) { throw 'API service tests failed' }
} finally { $env:PATH = $originalPath }
