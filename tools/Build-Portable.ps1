param([switch]$HostOnly)
. (Join-Path $PSScriptRoot 'NativeToolchain.ps1')
function Get-SourceHash([string]$path) {
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash([IO.File]::ReadAllBytes($path))).Replace('-','').ToLowerInvariant() }
    finally { $hash.Dispose() }
}
$output = Join-Path $projectRoot 'bin/Eternal'
$build = Join-Path $projectRoot 'artifacts/portable'
foreach ($buildInput in @('host/EternalHost/HostMod.cpp','host/EternalHost/runtime/Host.cpp','host/EternalHost/runtime/Config.cpp','modules/EternalCore/runtime/Runtime.cpp','sdk/EternalSDK/include/EternalSDK/Module/module_abi.h')) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $buildInput) -PathType Leaf)) { throw "Native build input missing: $buildInput" }
}
New-Item -ItemType Directory -Path $output,(Join-Path $output 'modules'),$build -Force | Out-Null
$sources = @((Get-ChildItem -LiteralPath (Join-Path $projectRoot 'host'),(Join-Path $projectRoot 'modules'),(Join-Path $projectRoot 'sdk/EternalSDK') -Recurse -File | Where-Object { $_.Extension -in @('.c','.cpp','.hpp','.h') }).FullName)
$sources += @($PSCommandPath,(Join-Path $PSScriptRoot 'NativeToolchain.ps1'))
$sources += @((Get-ChildItem -LiteralPath (Join-Path $projectRoot 'tests/Fixtures') -File | Where-Object { $_.Extension -in @('.cpp','.hpp','.h') }).FullName)
$fmtRoot = Split-Path -Parent ($headerLock | Where-Object name -eq 'fmt').include
$sources += @((Join-Path $fmtRoot 'src/format.cc'),(Join-Path $dependencyRoot 'symbolprovider/SymbolProvider-6c93ec45c8455992ee726d92df60316c8e731c44/src/SymbolProvider.cpp'))
$sourceHashes = @($sources | ForEach-Object { [pscustomobject]@{path=$_.Substring($projectRoot.Length+1);sha256=(Get-SourceHash $_)} })
function Compile-Cpp([string]$path,[string[]]$flags,[string]$prefix = '') {
    $object = Join-Path $build ($prefix+($path.Replace('/','_').Replace([char]92,[char]95))+'.obj')
    & $nativeCompiler @flags '/c' (Join-Path $projectRoot $path) ('/Fo'+$object) | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $path" }
    return $object
}
$commonFlags = @('/nologo','/std:c++20','/EHa','/MD','/utf-8','/O2','/D_HAS_CXX23=1','/DNOMINMAX','/DUNICODE','/DWIN32_LEAN_AND_MEAN',"/imsvc$msvcInclude","/imsvc$windowsInclude/ucrt","/imsvc$windowsInclude/shared","/imsvc$windowsInclude/um","/I$projectRoot/sdk/EternalSDK/include")
$jsonInclude = ($headerLock | Where-Object name -eq 'json').include
if (-not $jsonInclude) { throw 'The fixed nlohmann_json header dependency is missing' }
$runtimeLibraries = @('msvcrt.lib','msvcprt.lib','vcruntime.lib','ucrt.lib','kernel32.lib')
$sqlite = Join-Path $dependencyRoot 'sqlite/sqlite-amalgamation-3530400'
$sqliteObject = Join-Path $build 'sqlite3.obj'
$moduleNames = @('EternalCore','EternalCommerce','EternalLife','EternalWorld','EternalContent','EternalManagement','EternalPresentation','EternalEncounters')
$products = @()
$moduleBaseline = $null
$domainObjects = @((Join-Path $build 'modules_EternalCore_domain_Core.cpp.obj'),(Join-Path $build 'modules_EternalCore_domain_Sha256.cpp.obj'),$sqliteObject)
if ($HostOnly) {
    $receiptPath = Join-Path $build 'build-receipt.json'
    if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) { throw 'Host-only build requires an existing complete build receipt' }
    $baseline = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    $moduleSources = @($baseline.sources | Where-Object { $_.path -match '^modules[\\/]' })
    $currentModuleSources = @($sourceHashes | Where-Object { $_.path -match '^modules[\\/]' })
    if ($moduleSources.Count -ne $currentModuleSources.Count) { throw 'Internal module sources changed; run a complete build' }
    foreach ($source in $moduleSources) {
        if ((Get-SourceHash (Join-Path $projectRoot $source.path)) -ne $source.sha256) { throw "Internal module source changed; run a complete build: $($source.path)" }
    }
    foreach ($name in $moduleNames) {
        $product = @($baseline.products | Where-Object target -eq $name)
        if ($product.Count -ne 1 -or (Get-SourceHash (Join-Path $output $product[0].path)) -ne $product[0].sha256) { throw "Internal module baseline is missing or changed: $name" }
        $products += $product[0]
    }
    if ($baseline.module_baseline) { $moduleBaseline = $baseline.module_baseline }
    else { $moduleBaseline = [pscustomobject]@{built_utc=$baseline.built_utc;receipt_sha256=(Get-SourceHash $receiptPath);sources=$moduleSources;products=$products} }
} else {
& $nativeCompiler '/nologo' '/std:c11' '/TC' '/MD' '/O2' '/DSQLITE_THREADSAFE=1' '/DSQLITE_DQS=0' '/DSQLITE_ENABLE_API_ARMOR=1' "/imsvc$msvcInclude" "/imsvc$windowsInclude/ucrt" "/imsvc$windowsInclude/shared" "/imsvc$windowsInclude/um" '/c' (Join-Path $sqlite 'sqlite3.c') ('/Fo'+$sqliteObject)
if ($LASTEXITCODE -ne 0) { throw 'Private SQLite compilation failed' }
$domainFlags = $commonFlags + @("/I$sqlite","/I$projectRoot/modules/EternalCore/domain")
$domainObjects = @((Compile-Cpp 'modules/EternalCore/domain/Core.cpp' $domainFlags),(Compile-Cpp 'modules/EternalCore/domain/Sha256.cpp' $domainFlags),$sqliteObject)
foreach ($name in $moduleNames) {
    $flags = $commonFlags + @('/DETERNAL_MODULE_BUILD')
    $objects = @()
    if ($name -eq 'EternalCore') { $flags += @('/DETERNAL_CORE_BUILD',"/I$projectRoot/modules/EternalCore/api","/I$projectRoot/modules/EternalCore/runtime","/I$projectRoot/modules/EternalCore/domain","/I$sqlite","/I$jsonInclude") }
    $objects += Compile-Cpp ('modules/'+$name+'/Module.cpp') $flags
    if ($name -eq 'EternalCore') {
        $objects += Compile-Cpp 'modules/EternalCore/api/ApiService.cpp' $flags
        $objects += Compile-Cpp 'modules/EternalCore/runtime/Runtime.cpp' $flags
        if (Test-Path -LiteralPath (Join-Path $projectRoot 'modules/EternalCore/api/Phase2Service.cpp') -PathType Leaf) {
            $objects += Compile-Cpp 'modules/EternalCore/api/Phase2Service.cpp' $flags
        }
        $objects += $domainObjects
    }
    $moduleLibraries = $runtimeLibraries
    if ($name -eq 'EternalCore') { $moduleLibraries += 'bcrypt.lib' }
    $dll = Join-Path $output ('modules/'+$name+'.dll')
    & $nativeLinker @nativeLinkerArguments '/DLL' '/DEBUG' ('/OUT:'+ $dll) ('/PDB:'+ $output+'/modules/'+$name+'.pdb') @objects @moduleLibraries
    if ($LASTEXITCODE -ne 0) { throw "Internal module link failed: $name" }
    $products += [pscustomobject]@{target=$name;path=('modules/'+$name+'.dll');sha256=(Get-SourceHash $dll)}
    & $nativeReadobj '--coff-exports' '--coff-imports' $dll | Set-Content -LiteralPath (Join-Path $build ($name+'-inspection.txt')) -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "Module DLL inspection failed: $name" }
}
}
$fixtureOutput = Join-Path $projectRoot 'bin/validation'
New-Item -ItemType Directory -Path $fixtureOutput -Force | Out-Null
$fixtureObject = Compile-Cpp 'tests/Fixtures/CoreValidationModule.cpp' ($commonFlags + '/DETERNAL_MODULE_BUILD')
$fixtureDll = Join-Path $fixtureOutput 'CoreValidationModule.dll'
& $nativeLinker @nativeLinkerArguments '/DLL' '/DEBUG' ('/OUT:'+ $fixtureDll) ('/PDB:'+ $fixtureOutput+'/CoreValidationModule.pdb') $fixtureObject @runtimeLibraries
if ($LASTEXITCODE -ne 0) { throw 'Validation fixture DLL link failed' }
& $nativeReadobj '--coff-exports' '--coff-imports' $fixtureDll | Set-Content -LiteralPath (Join-Path $build 'CoreValidationModule-inspection.txt') -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw 'Validation fixture DLL inspection failed' }
$validationFixture = [pscustomobject]@{target='CoreValidationModule';project_path='bin/validation/CoreValidationModule.dll';sha256=(Get-SourceHash $fixtureDll)}
$validationProducts = @($validationFixture)
# Never reuse the production Module/Runtime object paths for the validation macro.
$validationFlags = $commonFlags + @('/DETERNAL_MODULE_BUILD','/DETERNAL_CORE_BUILD','/DETERNAL_CORE_VALIDATION_BUILD',"/I$projectRoot/modules/EternalCore/api","/I$projectRoot/modules/EternalCore/runtime","/I$projectRoot/modules/EternalCore/domain","/I$sqlite","/I$jsonInclude")
$validationObjects = @(
    (Compile-Cpp 'modules/EternalCore/Module.cpp' $validationFlags 'validation_'),
    (Compile-Cpp 'modules/EternalCore/api/ApiService.cpp' $validationFlags 'validation_'),
    (Compile-Cpp 'modules/EternalCore/runtime/Runtime.cpp' $validationFlags 'validation_')
) + $domainObjects
$validationDll = Join-Path $fixtureOutput 'EternalCoreValidation.dll'
& $nativeLinker @nativeLinkerArguments '/DLL' '/DEBUG' ('/OUT:'+$validationDll) ('/PDB:'+$fixtureOutput+'/EternalCoreValidation.pdb') @validationObjects @runtimeLibraries 'bcrypt.lib'
if ($LASTEXITCODE -ne 0) { throw 'Separate validation Core DLL link failed' }
$validationProducts += [pscustomobject]@{target='EternalCoreValidation';project_path='bin/validation/EternalCoreValidation.dll';sha256=(Get-SourceHash $validationDll)}
$consumerObjects = @((Compile-Cpp 'tests/Fixtures/EternalTestConsumer.cpp' ($commonFlags + @('/DETERNAL_MODULE_BUILD',"/I$sqlite"))),$sqliteObject)
$consumerDll = Join-Path $fixtureOutput 'EternalTestConsumer.dll'
& $nativeLinker @nativeLinkerArguments '/DLL' '/DEBUG' ('/OUT:'+$consumerDll) ('/PDB:'+$fixtureOutput+'/EternalTestConsumer.pdb') @consumerObjects @runtimeLibraries
if ($LASTEXITCODE -ne 0) { throw 'Independent SDK consumer DLL link failed' }
$validationProducts += [pscustomobject]@{target='EternalTestConsumer';project_path='bin/validation/EternalTestConsumer.dll';sha256=(Get-SourceHash $consumerDll)}
foreach ($validationProduct in $validationProducts) {
    & $nativeReadobj '--coff-exports' '--coff-imports' (Join-Path $projectRoot $validationProduct.project_path) | Set-Content -LiteralPath (Join-Path $build ($validationProduct.target+'-inspection.txt')) -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "Validation DLL inspection failed: $($validationProduct.target)" }
}
$hostSources = @('host/EternalHost/HostMod.cpp') + @((Get-ChildItem -LiteralPath (Join-Path $projectRoot 'host/EternalHost/runtime') -Filter '*.cpp' -File).FullName | ForEach-Object { $_.Substring($projectRoot.Length+1) })
$hostNative = Join-Path $projectRoot 'host/EternalHost/native'
if (Test-Path -LiteralPath $hostNative -PathType Container) {
    $hostSources += @((Get-ChildItem -LiteralPath $hostNative -Filter '*.cpp' -File).FullName | ForEach-Object { $_.Substring($projectRoot.Length+1) })
}
$hostFlags = $nativeCompilerArguments + @("/I$projectRoot/host/EternalHost","/I$projectRoot/host/EternalHost/runtime","/I$hostNative")
$hostObjects = @($hostSources | ForEach-Object { Compile-Cpp $_ $hostFlags })
$hostObjects += Compile-Cpp ((Join-Path $fmtRoot 'src/format.cc').Substring($projectRoot.Length+1)) $hostFlags
$hostObjects += Compile-Cpp '.deps/symbolprovider/SymbolProvider-6c93ec45c8455992ee726d92df60316c8e731c44/src/SymbolProvider.cpp' $hostFlags
$definition = Join-Path $build 'LeviLamina.def'
$importLibrary = Join-Path $build 'LeviLamina.lib'
$exports = & $nativeReadobj '--coff-exports' (Join-Path $projectRoot 'server/plugins/LeviLamina/LeviLamina.dll')
if ($LASTEXITCODE -ne 0) { throw 'LeviLamina export inspection failed' }
$names = @($exports | Where-Object { $_ -match '^  Name: ' } | ForEach-Object { $_.Substring(8) })
if (-not $names.Count) { throw 'LeviLamina exports unavailable' }
@('LIBRARY LeviLamina.dll','EXPORTS') + $names | Set-Content -LiteralPath $definition -Encoding ascii
& $nativeDlltool '-m' 'i386:x86-64' '-d' $definition '-l' $importLibrary
if ($LASTEXITCODE -ne 0) { throw 'LeviLamina import library generation failed' }
$prelinkDirectory = Join-Path $build 'prelink'
New-Item -ItemType Directory -Path (Join-Path $prelinkDirectory 'lib') -Force | Out-Null
& (Join-Path $dependencyRoot 'prelink/prelink.exe') 'server-windows-x64' $prelinkDirectory (Join-Path $dependencyRoot 'bedrock-runtime-data/bedrock_runtime_data') @hostObjects $importLibrary
if ($LASTEXITCODE -ne 0) { throw 'Official Host prelink failed' }
$hostDll = Join-Path $output 'EternalHost.dll'
& $nativeLinker @nativeLinkerArguments '/DLL' '/DEBUG' ('/OUT:'+ $hostDll) ('/PDB:'+ $output+'/EternalHost.pdb') ('/LIBPATH:'+ $prelinkDirectory+'/lib') '/DELAYLOAD:bedrock_runtime.dll' @hostObjects $importLibrary 'bedrock_runtime_api.lib' @runtimeLibraries
if ($LASTEXITCODE -ne 0) { throw 'Host DLL link failed' }
$products += [pscustomobject]@{target='EternalHost';path='EternalHost.dll';sha256=(Get-SourceHash $hostDll)}
& $nativeReadobj '--coff-exports' '--coff-imports' $hostDll | Set-Content -LiteralPath (Join-Path $build 'EternalHost-inspection.txt') -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw 'Host DLL inspection failed' }
foreach ($source in $sourceHashes) {
    if ((Get-SourceHash (Join-Path $projectRoot $source.path)) -ne $source.sha256) { throw "Build input changed during compilation: $($source.path)" }
}
[pscustomobject]@{format_version=3;host_only=[bool]$HostOnly;module_baseline=$moduleBaseline;architecture='Host + 8 internal ABI modules';compiler='LLVM22 clang-cl MSVC ABI /MD';levilamina='26.51.6';sources=$sourceHashes;products=$products;validation_fixture=$validationFixture;validation_products=$validationProducts;built_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $build 'build-receipt.json') -Encoding utf8
if ($HostOnly) { Write-Output 'Rebuilt EternalHost and verified the unchanged 8 internal module DLLs. No deployment or server startup was performed.' }
else { Write-Output 'Built EternalHost and all 8 internal module DLLs. No deployment or server startup was performed.' }
