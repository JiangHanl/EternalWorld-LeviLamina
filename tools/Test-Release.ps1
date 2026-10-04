param([string]$Xmake = 'xmake', [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $projectRoot
try {
    & $Python 'sdk/EternalSDK/tests/validate_sdk.py' '--clang' '.deps/LLVM22/bin/clang.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Public SDK contract/layout checks failed' }
    foreach ($probe in @('module_abi_layout.c','phase2_abi_layout.c','native_ingress_layout.c')) {
        foreach ($language in @('c','c++')) {
            $standard = if ($language -eq 'c') { 'c11' } else { 'c++20' }
            & '.deps/LLVM22/bin/clang.exe' '--target=x86_64-pc-windows-msvc' '-ffreestanding' '-fsyntax-only' '-Wall' '-Wextra' '-Werror' '-x' $language ('-std='+$standard) ('sdk/EternalSDK/tests/'+$probe)
            if ($LASTEXITCODE -ne 0) { throw "SDK layout checks failed: $probe ($language)" }
        }
    }
    foreach ($name in @('CoreDomainTests','Phase2DomainTests','Phase2RuntimeTests','CoreApiTests','HostRuntimeTests','Phase2NativeBridgeTests','EternalExampleContracts','ConfigTests','SDKCppTests','Phase2ClientContracts')) {
        & $Xmake 'build' $name
        if ($LASTEXITCODE -ne 0) { throw "Test build failed: $name" }
        & (Join-Path $projectRoot ('bin/tests/'+$name+'.exe'))
        if ($LASTEXITCODE -ne 0) { throw "Test failed: $name" }
    }
    & $Xmake 'build' 'ModuleArtifactTests'
    if ($LASTEXITCODE -ne 0) { throw 'Module artifact test build failed' }
    & 'bin/tests/ModuleArtifactTests.exe' 'bin/Eternal'
    if ($LASTEXITCODE -ne 0) { throw 'Module artifact test failed' }
    & (Join-Path $PSScriptRoot 'Test-PublicTree.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Public source audit failed' }
    & (Join-Path $PSScriptRoot 'Test-NativeBoundary.ps1') -SourceOnly
    if ($LASTEXITCODE -ne 0) { throw 'Cross-module source boundary failed' }
} finally { Pop-Location }
