param([string]$Commit)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Commit) {
    $Commit = (& git -C $projectRoot rev-parse --verify HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'A Git commit is required to name the release archive' }
}
if ($Commit -notmatch '^[0-9a-fA-F]{40}$') { throw 'Commit must be a full 40-character Git SHA' }
$manifest = Join-Path $projectRoot 'packaging/Eternal/manifest.json'
$metadata = Get-Content -Raw -LiteralPath $manifest | ConvertFrom-Json
if ($metadata.name -ne 'Eternal' -or $metadata.entry -ne 'EternalHost.dll' -or $metadata.type -ne 'native') {
    throw 'Invalid Eternal Host manifest'
}
$buildLock = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'build-lock.json') | ConvertFrom-Json
function Read-VersionMacro([string]$relative, [string]$macro) {
    $text = Get-Content -Raw -LiteralPath (Join-Path $projectRoot $relative)
    $match = [regex]::Match($text, '#define\s+'+[regex]::Escape($macro)+'\s+UINT32_C\((\d+)\)')
    if (-not $match.Success) { throw "SDK version macro missing: $macro" }
    return $match.Groups[1].Value
}
$coreHeader = 'sdk/EternalSDK/Core/core_abi.h'
$moduleHeader = 'sdk/EternalSDK/Module/module_abi.h'
$coreApi = (Read-VersionMacro $coreHeader 'EC_API_MAJOR')+'.'+(Read-VersionMacro $coreHeader 'EC_API_MINOR')
$moduleAbi = (Read-VersionMacro $moduleHeader 'EM_ABI_MAJOR')+'.'+(Read-VersionMacro $moduleHeader 'EM_ABI_MINOR')
$buildInfoPath = Join-Path $projectRoot 'artifacts/packaging/build-info.json'
New-Item -ItemType Directory -Path (Split-Path -Parent $buildInfoPath) -Force | Out-Null
[pscustomobject]@{
    source_commit=$Commit.ToLowerInvariant()
    eternal_version=$metadata.version
    sdk_core_api=$coreApi
    sdk_module_abi=$moduleAbi
    levilamina=$buildLock.levilamina
    bds=$buildLock.bds
    llvm=$buildLock.llvm.version
    xmake=$buildLock.xmake.version
    platform=$buildLock.platform
    language='C++20'
    runtime='MD'
    bds_validation='REAL BDS TEST REQUIRED'
} | ConvertTo-Json | Set-Content -LiteralPath $buildInfoPath -Encoding utf8
$files = @(
    @{source='bin/Eternal/EternalHost.dll';entry='Eternal/EternalHost.dll'},
    @{source='packaging/Eternal/manifest.json';entry='Eternal/manifest.json'},
    @{source='config/modules.example.json';entry='Eternal/config/modules.example.json'},
    @{source='LICENSE';entry='Eternal/LICENSE'},
    @{source='NOTICE';entry='Eternal/NOTICE'},
    @{source='THIRD_PARTY_NOTICES.md';entry='Eternal/THIRD_PARTY_NOTICES.md'},
    @{source='artifacts/packaging/build-info.json';entry='Eternal/resources/build-info.json'}
)
foreach ($license in Get-ChildItem -LiteralPath (Join-Path $projectRoot 'third_party/licenses') -Recurse -File) {
    $relative = $license.FullName.Substring($projectRoot.Length+1).Replace([char]92,[char]47)
    $files += @{source=$relative;entry=('Eternal/'+$relative)}
}
foreach ($name in @('EternalCore','EternalCommerce','EternalLife','EternalWorld','EternalContent','EternalManagement','EternalPresentation','EternalEncounters')) {
    $files += @{source=('bin/Eternal/modules/'+$name+'.dll');entry=('Eternal/modules/'+$name+'.dll')}
}
foreach ($file in $files) {
    $path = Join-Path $projectRoot $file.source
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Release input missing: $($file.source)" }
    if ($file.source.EndsWith('.dll')) {
        $stream = [IO.File]::OpenRead($path)
        try { if ($stream.ReadByte() -ne 77 -or $stream.ReadByte() -ne 90) { throw "Not a PE DLL: $($file.source)" } }
        finally { $stream.Dispose() }
    }
}
$dist = Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$archivePath = Join-Path $dist ('EternalWorld-'+$Commit.ToLowerInvariant()+'-windows-x64.zip')
Add-Type -AssemblyName System.IO.Compression
$archiveStream = [IO.File]::Open($archivePath,[IO.FileMode]::Create)
$archive = [IO.Compression.ZipArchive]::new($archiveStream,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in ($files | Sort-Object entry)) {
        $entry = $archive.CreateEntry($file.entry,[IO.Compression.CompressionLevel]::Optimal)
        $sourceStream = [IO.File]::OpenRead((Join-Path $projectRoot $file.source))
        $entryStream = $entry.Open()
        try { $sourceStream.CopyTo($entryStream) } finally { $entryStream.Dispose(); $sourceStream.Dispose() }
    }
} finally { $archive.Dispose(); $archiveStream.Dispose() }
$hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
($hash+'  '+[IO.Path]::GetFileName($archivePath)) | Set-Content -LiteralPath ($archivePath+'.sha256') -Encoding ascii
Write-Output ('Package: '+$archivePath)
Write-Output ('SHA256: '+$hash)
