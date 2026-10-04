param([string]$Python = 'python')
. (Join-Path $PSScriptRoot 'NativeToolchain.ps1')
$build = Join-Path $projectRoot 'artifacts/portable'
$output = Join-Path $projectRoot 'bin/tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
Push-Location -LiteralPath $projectRoot
try {
    & $Python 'sdk/EternalSDK/tests/validate_sdk.py' '--clang' '.deps/LLVM22/bin/clang.exe'
    if ($LASTEXITCODE -ne 0) { throw 'SDK contract/layout checks failed' }
    foreach ($language in @('c','c++')) {
        $standard = if ($language -eq 'c') { 'c11' } else { 'c++20' }
        & '.deps/LLVM22/bin/clang.exe' '--target=x86_64-pc-windows-msvc' '-ffreestanding' '-fsyntax-only' '-Wall' '-Wextra' '-Werror' '-x' $language ('-std='+$standard) 'sdk/EternalSDK/tests/module_abi_layout.c'
        if ($LASTEXITCODE -ne 0) { throw "Module SDK layout checks failed: $language" }
    }
    $sqlite = Join-Path $dependencyRoot 'sqlite/sqlite-amalgamation-3530400'
    $flags = $nativeCompilerArguments + @('/UNDEBUG',"/I$sqlite","/I$projectRoot/modules/EternalCore/domain","/I$projectRoot/modules/EternalCore/api","/I$projectRoot/host/EternalHost/runtime")
    $runtimeLibraries = @('msvcrt.lib','msvcprt.lib','vcruntime.lib','ucrt.lib','kernel32.lib')
    $cases = @(
        @{name='CoreDomainTests';sources=@('tests/EternalCore/core_tests.cpp');extra=@('modules_EternalCore_domain_Core.cpp.obj','modules_EternalCore_domain_Sha256.cpp.obj','sqlite3.obj')},
        @{name='CoreApiTests';sources=@('tests/EternalCore/api_service_test.cpp','modules/EternalCore/api/ApiService.cpp');extra=@()},
        @{name='HostRuntimeTests';sources=@('tests/EternalHost/runtime_tests.cpp','host/EternalHost/runtime/Host.cpp','host/EternalHost/runtime/Config.cpp');extra=@()},
        @{name='EternalExampleContracts';sources=@('templates/EternalModule/Module.cpp','templates/EternalModule/tests/ModuleContract.cpp');extra=@()},
        @{name='ModuleArtifactTests';sources=@('tests/Host/real_module_tests.cpp','host/EternalHost/runtime/Host.cpp','host/EternalHost/runtime/Config.cpp');extra=@()},
        @{name='ConfigTests';sources=@('tests/Host/config_tests.cpp','host/EternalHost/runtime/Config.cpp');extra=@()},
        @{name='SDKCppTests';sources=@('sdk/EternalSDK/tests/cpp_headers.cpp');extra=@()}
    )
    foreach ($case in $cases) {
        $objects = @()
        foreach ($source in $case.sources) {
            $object = Join-Path $build ('test_'+$source.Replace('/','_')+'.obj')
            & $nativeCompiler @flags '/DETERNAL_MODULE_BUILD' '/c' (Join-Path $projectRoot $source) ('/Fo'+$object)
            if ($LASTEXITCODE -ne 0) { throw "Test compilation failed: $source" }
            $objects += $object
        }
        $objects += @($case.extra | ForEach-Object { Join-Path $build $_ })
        $executable = Join-Path $output ($case.name+'.exe')
        & $nativeLinker @nativeLinkerArguments ('/OUT:'+ $executable) @objects @runtimeLibraries
        if ($LASTEXITCODE -ne 0) { throw "Test link failed: $($case.name)" }
        if ($case.name -eq 'ModuleArtifactTests') { & $executable (Join-Path $projectRoot 'bin/Eternal') }
        else { & $executable }
        if ($LASTEXITCODE -ne 0) { throw "Test failed: $($case.name)" }
    }
} finally { Pop-Location }
