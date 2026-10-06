$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$output = Join-Path $projectRoot 'dist/validation'
New-Item -ItemType Directory -Path $output -Force | Out-Null
foreach ($entry in @(
    @{source='bin/validation/CoreValidationModule.dll';name='CoreValidationModule.dll'},
    @{source='bin/validation/EternalCoreValidation.dll';name='EternalCoreValidation.dll'},
    @{source='bin/validation/EternalTestConsumer.dll';name='EternalTestConsumer.dll'},
    @{source='tests/Fixtures/CoreValidationModule.txt';name='README.txt'}
)) {
    $source = Join-Path $projectRoot $entry.source
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Validation input missing: $($entry.source)" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $entry.name) -Force
}
$dlls = @('CoreValidationModule.dll','EternalCoreValidation.dll','EternalTestConsumer.dll')
$hashes = @($dlls | ForEach-Object { [pscustomobject]@{file=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $output $_) -Algorithm SHA256).Hash.ToLowerInvariant()} })
[pscustomobject]@{purpose='Isolated validation only; never install with the production Core';files=$hashes;authenticated_client='NOT_RUN'} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'validation-manifest.json') -Encoding utf8
Write-Output 'Packaged three independent validation DLLs, instructions and hashes; excluded from the production release'
