param()
. (Join-Path $PSScriptRoot 'NativeToolchain.ps1')
function Get-SourceHash([string]$path) {
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash([IO.File]::ReadAllBytes($path))).Replace('-','').ToLowerInvariant() }
    finally { $hash.Dispose() }
}
$output = Join-Path $projectRoot 'bin/Eternal'
$build = Join-Path $projectRoot 'artifacts/portable'
foreach ($buildInput in @('host/EternalHost/HostMod.cpp','host/EternalHost/runtime/Host.cpp','host/EternalHost/runtime/Config.cpp','sdk/EternalSDK/include/EternalSDK/Module/module_abi.h')) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $buildInput) -PathType Leaf)) { throw "Native build input missing: $buildInput" }
}
New-Item -ItemType Directory -Path $output,(Join-Path $output 'modules'),$build -Force | Out-Null
$sources = @((Get-ChildItem -LiteralPath (Join-Path $projectRoot 'host'),(Join-Path $projectRoot 'modules'),(Join-Path $projectRoot 'sdk/EternalSDK') -Recurse -File | Where-Object { $_.Extension -in @('.c','.cpp','.hpp','.h') }).FullName)
$sources += @($PSCommandPath,(Join-Path $PSScriptRoot 'NativeToolchain.ps1'))
$fmtRoot = Split-Path -Parent ($headerLock | Where-Object name -eq 'fmt').include
$sources += @((Join-Path $fmtRoot 'src/format.cc'),(Join-Path $dependencyRoot 'symbolprovider/SymbolProvider-6c93ec45c8455992ee726d92df60316c8e731c44/src/SymbolProvider.cpp'))
$sourceHashes = @($sources | ForEach-Object { [pscustomobject]@{path=$_.Substring($projectRoot.Length+1);sha256=(Get-SourceHash $_)} })
function Compile-Cpp([string]$path,[string[]]$flags) {
    $object = Join-Path $build (($path.Replace('/','_').Replace([char]92,[char]95))+'.obj')
    & $nativeCompiler @flags '/c' (Join-Path $projectRoot $path) ('/Fo'+$object) | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $path" }
    return $object
}
$commonFlags = @('/nologo','/std:c++20','/EHa','/MD','/utf-8','/O2','/D_HAS_CXX23=1','/DNOMINMAX','/DUNICODE','/DWIN32_LEAN_AND_MEAN',"/imsvc$msvcInclude","/imsvc$windowsInclude/ucrt","/imsvc$windowsInclude/shared","/imsvc$windowsInclude/um","/I$projectRoot/sdk/EternalSDK/include")
$runtimeLibraries = @('msvcrt.lib','msvcprt.lib','vcruntime.lib','ucrt.lib','kernel32.lib')
$sqlite = Join-Path $dependencyRoot 'sqlite/sqlite-amalgamation-3530400'
$sqliteObject = Join-Path $build 'sqlite3.obj'
& $nativeCompiler '/nologo' '/std:c11' '/TC' '/MD' '/O2' '/DSQLITE_THREADSAFE=1' '/DSQLITE_DQS=0' '/DSQLITE_ENABLE_API_ARMOR=1' "/imsvc$msvcInclude" "/imsvc$windowsInclude/ucrt" "/imsvc$windowsInclude/shared" "/imsvc$windowsInclude/um" '/c' (Join-Path $sqlite 'sqlite3.c') ('/Fo'+$sqliteObject)
if ($LASTEXITCODE -ne 0) { throw 'Private SQLite compilation failed' }
$domainFlags = $commonFlags + @("/I$sqlite","/I$projectRoot/modules/EternalCore/domain")
$domainObjects = @((Compile-Cpp 'modules/EternalCore/domain/Core.cpp' $domainFlags),(Compile-Cpp 'modules/EternalCore/domain/Sha256.cpp' $domainFlags),$sqliteObject)
$moduleNames = @('EternalCore','EternalCommerce','EternalLife','EternalWorld','EternalContent','EternalManagement','EternalPresentation','EternalEncounters')
$products = @()
foreach ($name in $moduleNames) {
    $flags = $commonFlags + @('/DETERNAL_MODULE_BUILD')
    $objects = @()
    if ($name -eq 'EternalCore') { $flags += @('/DETERNAL_CORE_BUILD',"/I$projectRoot/modules/EternalCore/api") }
    $objects += Compile-Cpp ('modules/'+$name+'/Module.cpp') $flags
    if ($name -eq 'EternalCore') {
        $objects += Compile-Cpp 'modules/EternalCore/api/ApiService.cpp' $flags
        $objects += $domainObjects
    }
    $dll = Join-Path $output ('modules/'+$name+'.dll')
    & $nativeLinker @nativeLinkerArguments '/DLL' '/DEBUG' ('/OUT:'+ $dll) ('/PDB:'+ $output+'/modules/'+$name+'.pdb') @objects @runtimeLibraries
    if ($LASTEXITCODE -ne 0) { throw "Internal module link failed: $name" }
    $products += [pscustomobject]@{target=$name;path=('modules/'+$name+'.dll');sha256=(Get-SourceHash $dll)}
    & $nativeReadobj '--coff-exports' '--coff-imports' $dll | Set-Content -LiteralPath (Join-Path $build ($name+'-inspection.txt')) -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "Module DLL inspection failed: $name" }
}
$hostSources = @('host/EternalHost/HostMod.cpp') + @((Get-ChildItem -LiteralPath (Join-Path $projectRoot 'host/EternalHost/runtime') -Filter '*.cpp' -File).FullName | ForEach-Object { $_.Substring($projectRoot.Length+1) })
$hostFlags = $nativeCompilerArguments + @("/I$projectRoot/host/EternalHost","/I$projectRoot/host/EternalHost/runtime")
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
[pscustomobject]@{format_version=1;architecture='Host + 8 internal ABI modules';compiler='LLVM22 clang-cl MSVC ABI /MD';levilamina='26.51.6';sources=$sourceHashes;products=$products;built_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $build 'build-receipt.json') -Encoding utf8
Write-Output 'Built EternalHost and all 8 internal module DLLs. No deployment or server startup was performed.'
