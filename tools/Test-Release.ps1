param([string]$Xmake = 'xmake', [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'Test-Manifest.ps1')
function Invoke-ReleaseTest([string]$Name, [string[]]$Arguments = @()) {
    & $Xmake 'build' $Name
    if ($LASTEXITCODE -ne 0) { throw "Test build failed: $Name" }
    $testOutput = @(& (Join-Path $projectRoot ('bin/tests/'+$Name+'.exe')) @Arguments 2>&1)
    $testExit = $LASTEXITCODE
    $testOutput | Out-Host
    Add-TestResult $Name $testOutput $(if ($testExit -eq 0) {'PASS'} else {'FAIL'})
    if ($testExit -ne 0) { throw "Test failed: $Name" }
}
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
    Add-TestResult 'SDKLayoutChecks'
    foreach ($name in @('CoreDomainTests','Phase2DomainTests','CommerceDomainTests','Phase2RuntimeTests','SyntheticMultiUserTests','ProductionRuntimeIsolationTests','CoreApiTests','HostRuntimeTests','Phase2NativeBridgeTests','EternalExampleContracts','ConfigTests','SDKCppTests','Phase2ClientContracts')) {
        Invoke-ReleaseTest $name
    }
    Invoke-ReleaseTest 'ModuleArtifactTests' @('bin/Eternal')
    foreach ($fixture in @('CoreValidationModule','EternalCoreValidation','EternalTestConsumer')) {
        & $Xmake 'build' $fixture
        if ($LASTEXITCODE -ne 0) { throw "Independent validation DLL build failed: $fixture" }
    }
    Invoke-ReleaseTest 'ValidationModuleTests' @('bin/Eternal','bin/validation/CoreValidationModule.dll')
    Invoke-ReleaseTest 'ConsumerModuleTests' @('bin/Eternal','bin/validation/EternalCoreValidation.dll','bin/validation/EternalTestConsumer.dll')
    Invoke-ReleaseTest 'ProductionIsolationTests' @('bin/Eternal/modules/EternalCore.dll','bin/validation/EternalCoreValidation.dll')
    & (Join-Path $PSScriptRoot 'Test-ProductionArtifacts.ps1') -Report 'artifacts/ci/production-isolation.json'
    Add-TestResult 'ProductionArtifactScan'
    & (Join-Path $PSScriptRoot 'Test-PublicTree.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Public source audit failed' }
    Add-TestResult 'PublicTreeAudit'
    & (Join-Path $PSScriptRoot 'Test-NativeBoundary.ps1') -SourceOnly
    if ($LASTEXITCODE -ne 0) { throw 'Cross-module source boundary failed' }
    Add-TestResult 'SourceBoundaryChecks'
} finally { Write-TestManifest 'artifacts/ci/test-manifest.json'; Pop-Location }
