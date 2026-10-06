param([string]$Python = 'python')
. (Join-Path $PSScriptRoot 'NativeToolchain.ps1')
. (Join-Path $PSScriptRoot 'Test-Manifest.ps1')
$build = Join-Path $projectRoot 'artifacts/portable'
$output = Join-Path $projectRoot 'bin/tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
Push-Location -LiteralPath $projectRoot
try {
    & $Python 'sdk/EternalSDK/tests/validate_sdk.py' '--clang' '.deps/LLVM22/bin/clang.exe'
    if ($LASTEXITCODE -ne 0) { throw 'SDK contract/layout checks failed' }
    foreach ($probe in @('module_abi_layout.c','phase2_abi_layout.c','native_ingress_layout.c')) {
        foreach ($language in @('c','c++')) {
            $standard = if ($language -eq 'c') { 'c11' } else { 'c++20' }
            & '.deps/LLVM22/bin/clang.exe' '--target=x86_64-pc-windows-msvc' '-ffreestanding' '-fsyntax-only' '-Wall' '-Wextra' '-Werror' '-x' $language ('-std='+$standard) ('sdk/EternalSDK/tests/'+$probe)
            if ($LASTEXITCODE -ne 0) { throw "SDK layout checks failed: $probe ($language)" }
        }
    }
    Add-TestResult 'SDKLayoutChecks'
    $sqlite = Join-Path $dependencyRoot 'sqlite/sqlite-amalgamation-3530400'
    $flags = $nativeCompilerArguments + @('/UNDEBUG',"/I$sqlite","/I$projectRoot/modules/EternalCore/domain","/I$projectRoot/modules/EternalCore/api","/I$projectRoot/modules/EternalCore/runtime","/I$projectRoot/host/EternalHost/runtime","/I$projectRoot/host/EternalHost/native")
    $runtimeLibraries = @('msvcrt.lib','msvcprt.lib','vcruntime.lib','ucrt.lib','kernel32.lib')
    $cases = @(
        @{name='CoreDomainTests';sources=@('tests/EternalCore/core_tests.cpp');extra=@('modules_EternalCore_domain_Core.cpp.obj','modules_EternalCore_domain_Sha256.cpp.obj','sqlite3.obj')},
        @{name='Phase2DomainTests';sources=@('tests/EternalCore/phase2_domain_tests.cpp');extra=@('modules_EternalCore_domain_Core.cpp.obj','modules_EternalCore_domain_Sha256.cpp.obj','sqlite3.obj')},
        @{name='CommerceDomainTests';sources=@('tests/EternalCommerce/commerce_tests.cpp');extra=@('modules_EternalCommerce_domain_Commerce.cpp.obj','modules_EternalCommerce_domain_Sha256.cpp.obj','sqlite3.obj')},
        @{name='Phase2RuntimeTests';sources=@('tests/EternalCore/phase2_runtime_tests.cpp','modules/EternalCore/runtime/Runtime.cpp','modules/EternalCore/api/ApiService.cpp');extra=@('modules_EternalCore_domain_Core.cpp.obj','modules_EternalCore_domain_Sha256.cpp.obj','sqlite3.obj');libraries=@('bcrypt.lib')},
        @{name='SyntheticMultiUserTests';sources=@('tests/EternalCore/synthetic_multi_user_tests.cpp','modules/EternalCore/runtime/Runtime.cpp','modules/EternalCore/api/ApiService.cpp');extra=@('modules_EternalCore_domain_Core.cpp.obj','modules_EternalCore_domain_Sha256.cpp.obj','sqlite3.obj');libraries=@('bcrypt.lib')},
        @{name='ProductionRuntimeIsolationTests';sources=@('tests/EternalCore/production_runtime_tests.cpp','modules/EternalCore/runtime/Runtime.cpp','modules/EternalCore/api/ApiService.cpp');extra=@('modules_EternalCore_domain_Core.cpp.obj','modules_EternalCore_domain_Sha256.cpp.obj','sqlite3.obj');libraries=@('bcrypt.lib')},
        @{name='CoreApiTests';sources=@('tests/EternalCore/api_service_test.cpp','modules/EternalCore/api/ApiService.cpp');extra=@()},
        @{name='HostRuntimeTests';sources=@('tests/EternalHost/runtime_tests.cpp','host/EternalHost/runtime/Host.cpp','host/EternalHost/runtime/Config.cpp');extra=@()},
        @{name='Phase2NativeBridgeTests';sources=@('tests/Host/phase2_native_bridge_tests.cpp','host/EternalHost/runtime/Host.cpp','host/EternalHost/runtime/Config.cpp');extra=@()},
        @{name='EternalExampleContracts';sources=@('templates/EternalModule/Module.cpp','templates/EternalModule/tests/ModuleContract.cpp');extra=@()},
        @{name='ModuleArtifactTests';sources=@('tests/Host/real_module_tests.cpp','host/EternalHost/runtime/Host.cpp','host/EternalHost/runtime/Config.cpp');extra=@()},
        @{name='ConfigTests';sources=@('tests/Host/config_tests.cpp','host/EternalHost/runtime/Config.cpp');extra=@()},
        @{name='SDKCppTests';sources=@('sdk/EternalSDK/tests/cpp_headers.cpp');extra=@()},
        @{name='Phase2ClientContracts';sources=@('sdk/EternalSDK/tests/phase2_client_contract.cpp');extra=@()},
        @{name='ValidationModuleTests';sources=@('tests/Host/validation_module_tests.cpp','host/EternalHost/runtime/Host.cpp','host/EternalHost/runtime/Config.cpp');extra=@()},
        @{name='ConsumerModuleTests';sources=@('tests/Host/consumer_module_tests.cpp','host/EternalHost/runtime/Host.cpp','host/EternalHost/runtime/Config.cpp');extra=@('sqlite3.obj')},
        @{name='ProductionIsolationTests';sources=@('tests/Host/production_isolation_tests.cpp');extra=@()}
        @{name='CommerceModuleTests';sources=@('tests/Host/commerce_module_tests.cpp','host/EternalHost/runtime/Host.cpp','host/EternalHost/runtime/Config.cpp');extra=@()}
    )
    foreach ($case in $cases) {
        if ($case.name -eq 'Phase2RuntimeTests' -and (Test-Path -LiteralPath 'modules/EternalCore/api/Phase2Service.cpp' -PathType Leaf)) {
            $case.sources += 'modules/EternalCore/api/Phase2Service.cpp'
        }
        $objects = @()
        $caseFlags = $flags
        if ($case.name -eq 'CommerceDomainTests') { $caseFlags = $nativeCompilerArguments + @('/UNDEBUG',"/I$sqlite","/I$projectRoot/modules/EternalCommerce/domain") }
        if ($case.name -in @('Phase2RuntimeTests','SyntheticMultiUserTests')) { $caseFlags += @('/DETERNAL_CORE_RUNTIME_TESTING','/DETERNAL_CORE_VALIDATION_BUILD') }
        foreach ($source in $case.sources) {
            $object = Join-Path $build ('test_'+$case.name+'_'+$source.Replace('/','_')+'.obj')
            & $nativeCompiler @caseFlags '/DETERNAL_MODULE_BUILD' '/c' (Join-Path $projectRoot $source) ('/Fo'+$object)
            if ($LASTEXITCODE -ne 0) { throw "Test compilation failed: $source" }
            $objects += $object
        }
        $objects += @($case.extra | ForEach-Object { Join-Path $build $_ })
        $executable = Join-Path $output ($case.name+'.exe')
        $testLibraries = $runtimeLibraries
        if ($case.libraries) { $testLibraries += $case.libraries }
        & $nativeLinker @nativeLinkerArguments ('/OUT:'+ $executable) @objects @testLibraries
        if ($LASTEXITCODE -ne 0) { throw "Test link failed: $($case.name)" }
        $argumentMap = @{
            'ModuleArtifactTests'      = @('bin/Eternal')
            'ValidationModuleTests'    = @('bin/Eternal','bin/validation/CoreValidationModule.dll')
            'ConsumerModuleTests'      = @('bin/Eternal','bin/validation/EternalCoreValidation.dll','bin/validation/EternalTestConsumer.dll')
            'ProductionIsolationTests' = @('bin/Eternal/modules/EternalCore.dll','bin/validation/EternalCoreValidation.dll')
            'CommerceModuleTests'      = @('bin/Eternal','bin/validation/EternalCoreValidation.dll','bin/validation/EternalCommerceValidation.dll')
        }
        $arguments = @()
        if ($argumentMap.ContainsKey($case.name)) { $arguments = $argumentMap[$case.name] }
        $testOutput = @(& $executable @arguments 2>&1)
        $testExit = $LASTEXITCODE
        $testOutput | Out-Host
        Add-TestResult $case.name $testOutput $(if ($testExit -eq 0) {'PASS'} else {'FAIL'})
        if ($testExit -ne 0) { throw "Test failed: $($case.name)" }
    }
    & (Join-Path $PSScriptRoot 'Test-ProductionArtifacts.ps1') -Report 'artifacts/portable/production-isolation.json'
    Add-TestResult 'ProductionArtifactScan'
    & (Join-Path $PSScriptRoot 'Test-NativeBoundary.ps1') -SourceOnly
    if ($LASTEXITCODE -ne 0) { throw 'Cross-module source boundary failed' }
    Add-TestResult 'SourceBoundaryChecks'
    & (Join-Path $PSScriptRoot 'Test-PublicTree.ps1') -WorkingTree
    if ($LASTEXITCODE -ne 0) { throw 'Public working-tree audit failed' }
    Add-TestResult 'PublicTreeAudit'
} finally { Write-TestManifest 'artifacts/portable/test-manifest.json'; Pop-Location }
