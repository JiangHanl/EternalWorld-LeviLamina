param([string]$Directory = 'bin/Eternal', [string]$Report = 'artifacts/production-isolation.json')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$root = Join-Path $projectRoot $Directory
$llvm = Join-Path $projectRoot '.deps/LLVM22/bin'
$modules = @('EternalCore','EternalCommerce','EternalLife','EternalWorld','EternalContent','EternalManagement','EternalPresentation','EternalEncounters')
$expected = @('EternalHost.dll') + @($modules | ForEach-Object { 'modules/'+$_.ToString()+'.dll' })
$actual = @((Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.dll').FullName | ForEach-Object { $_.Substring($root.Length+1).Replace([char]92,[char]47) })
if (@(Compare-Object ($expected | Sort-Object) ($actual | Sort-Object)).Count) { throw 'Production output must contain exactly the Host and eight official module DLLs' }
$forbidden = 'testEnableFeatures|testIssueCapability|EternalCore_ValidationBuildMarker|CoreValidationModule_|EternalTestConsumer_|ETERNAL_CORE_(?:RUNTIME_TESTING|VALIDATION_BUILD)'
$results = @()
foreach ($relative in $expected) {
    $dll = Join-Path $root $relative
    $inspection = & (Join-Path $llvm 'llvm-readobj.exe') '--coff-exports' '--coff-imports' $dll 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Production PE inspection failed: $relative" }
    $inspectionText = $inspection -join "`n"
    if ($inspectionText -match $forbidden) { throw "Test entry leaked into production DLL: $relative" }
    if ($relative -eq 'EternalHost.dll') {
        if ($inspectionText -notmatch 'Name: ll_mod_load' -or $inspectionText -notmatch 'Name: ll_memory_operator_overrided') { throw 'Host NativeMod/allocator exports missing' }
    } elseif ($inspectionText -match 'Name: (?:ll_mod_load|LeviLamina\.dll|bedrock_runtime\.dll)') {
        throw "Internal production module unexpectedly depends on LL/BDS entry points: $relative"
    }
    if ($inspectionText -match '(?i)Name: (?:EternalCoreValidation|CoreValidationModule|EternalTestConsumer|LiteLoader|LegacyScriptEngine|LegacyMoney|LegacyRemoteCall).*\.dll') { throw "Validation/legacy DLL dependency leaked: $relative" }
    $pdb = [IO.Path]::ChangeExtension($dll, '.pdb')
    if (-not (Test-Path -LiteralPath $pdb -PathType Leaf)) { throw "Production debug symbols required for test-hook scan: $relative" }
    $symbols = & (Join-Path $llvm 'llvm-pdbutil.exe') 'dump' '--publics' '--globals' $pdb 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Production symbol inspection failed: $relative" }
    if (($symbols -join "`n") -match $forbidden) { throw "Private Runtime test hook leaked into production symbols: $relative" }
    $results += [pscustomobject]@{path=$relative;sha256=(Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant();pe_and_symbols='PASS'}
}
$metadata = Get-Content -LiteralPath (Join-Path $projectRoot 'packaging/Eternal/manifest.json') -Raw | ConvertFrom-Json
if ($metadata.name -ne 'Eternal' -or $metadata.entry -ne 'EternalHost.dll' -or $metadata.type -ne 'native' -or $metadata.platform -ne 'server') { throw 'Production Host manifest boundary failed' }
$core = Get-Content -LiteralPath (Join-Path $projectRoot 'config/core.example.json') -Raw | ConvertFrom-Json
if ($core.ownerXuid -ne '' -or $core.developmentValidation -ne $false -or $core.validatedAssets -ne $false) { throw 'Public production example enables private identity or validation assets' }
$config = Get-Content -LiteralPath (Join-Path $projectRoot 'config/modules.example.json') -Raw | ConvertFrom-Json
if ($config.modules.Count -ne 8) { throw 'Production module example must contain exactly eight modules' }
foreach ($entry in $config.modules) {
    if ($entry.path -notin $expected -or [bool]$entry.enabled -ne ($entry.id -eq 'core') -or [bool]$entry.required -ne ($entry.id -eq 'core')) { throw 'Production module example includes a fixture or changes planned-module enablement' }
}
if (@(Compare-Object @($config.modules.path | Sort-Object) @($expected | Where-Object { $_ -ne 'EternalHost.dll' } | Sort-Object)).Count) { throw 'Production module example has duplicate or missing modules' }
foreach ($fixture in Get-ChildItem -LiteralPath (Join-Path $projectRoot 'tests/Fixtures') -Filter '*.cpp' -File) {
    $text = Get-Content -LiteralPath $fixture.FullName -Raw
    if ($text -match '#\s*include\s*[<"][^>"\r\n]*(?:\b(?:ll|mc)/|Core\.hpp|Runtime\.hpp|ApiService\.hpp|modules/EternalCore/)') { throw 'Validation fixtures must use public SDK and their own SQLite implementation only' }
}
$reportPath = Join-Path $projectRoot $Report
New-Item -ItemType Directory -Path (Split-Path -Parent $reportPath) -Force | Out-Null
[pscustomobject]@{classification='REAL_DLL';result='PASS';dll_count=$results.Count;dlls=$results;config_examples='PASS';limit='Configuration execution is separately verified by ProductionIsolationTests; no real client is involved.'} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding utf8
Write-Output 'PASS ProductionArtifactScan: 9 actual DLL exports/imports/PDB symbols and production examples; validation fixtures excluded'
