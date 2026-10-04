$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyRoot = Join-Path $projectRoot '.deps'
$nativeCompiler = Join-Path $dependencyRoot 'LLVM22/bin/clang-cl.exe'
$nativeLinker = Join-Path $dependencyRoot 'LLVM22/bin/lld-link.exe'
$nativeReadobj = Join-Path $dependencyRoot 'LLVM22/bin/llvm-readobj.exe'
$nativeDlltool = Join-Path $dependencyRoot 'LLVM22/bin/llvm-dlltool.exe'
$msvcInclude = Join-Path $dependencyRoot 'msvc-payloads/Microsoft.VC.14.44.17.14.CRT.Headers.base/Contents/VC/Tools/MSVC/14.44.35207/include'
$msvcLibraries = Join-Path $dependencyRoot 'msvc-payloads/Microsoft.VC.14.44.17.14.CRT.x64.Desktop.base/Contents/VC/Tools/MSVC/14.44.35207/lib/x64'
$msvcImportLibraries = Join-Path $dependencyRoot 'msvc-payloads/Microsoft.VC.14.44.17.14.CRT.x64.Store.base/Contents/VC/Tools/MSVC/14.44.35207/lib/x64'
$windowsInclude = Join-Path $dependencyRoot 'winsdk-nuget/microsoft.windows.sdk.cpp/c/Include/10.0.26100.0'
$windowsLibraryRoot = Join-Path $dependencyRoot 'winsdk-nuget/microsoft.windows.sdk.cpp.x64/c'
$leviSource = Join-Path $dependencyRoot 'sdk-source/LeviLamina-26.51.6'
$leviManifest = Get-Content -LiteralPath (Join-Path $projectRoot 'server/plugins/LeviLamina/manifest.json') -Raw | ConvertFrom-Json
if ($leviManifest.version -ne '26.51.6') { throw 'This toolchain requires LeviLamina 26.51.6' }
$headerLock = Get-Content -LiteralPath (Join-Path $dependencyRoot 'll-headers/headers.lock.json') -Raw | ConvertFrom-Json
$nativeCompilerArguments = @('/nologo', '/std:c++20', '/EHa', '/MD', '/utf-8', '/O2', '/D_HAS_CXX23=1', '/DLL_PLAT_S', '/DNOMINMAX', '/DUNICODE', '/DWIN32_LEAN_AND_MEAN', '/DENTT_PACKED_PAGE=128', '/DENTT_SPARSE_PAGE=2048', '/DFMT_USE_FULL_CACHE_DRAGONBOX=1', '/clang:-Wno-ignored-attributes', '/clang:-Wno-character-conversion', '/clang:-Wno-microsoft-cast')
$nativeCompilerArguments += @("/imsvc$msvcInclude", "/imsvc$windowsInclude/ucrt", "/imsvc$windowsInclude/shared", "/imsvc$windowsInclude/um", "/I$leviSource/src", "/I$leviSource/src-server", "/I$projectRoot/sdk/EternalSDK/include")
$nativeCompilerArguments += @($headerLock | ForEach-Object { '/I' + $_.include })
$nativeLinkerArguments = @('/NOLOGO', '/MACHINE:X64', "/LIBPATH:$msvcLibraries", "/LIBPATH:$msvcImportLibraries", "/LIBPATH:$windowsLibraryRoot/ucrt/x64", "/LIBPATH:$windowsLibraryRoot/um/x64")
foreach ($path in @($nativeCompiler, $nativeLinker, $nativeReadobj, $nativeDlltool, $msvcInclude, $msvcLibraries, "$windowsInclude/ucrt", "$windowsLibraryRoot/ucrt/x64")) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Native toolchain prerequisite missing: $path" }
}
