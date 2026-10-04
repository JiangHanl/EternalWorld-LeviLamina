$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$output = Join-Path $projectRoot 'dist/validation'
New-Item -ItemType Directory -Path $output -Force | Out-Null
foreach ($entry in @(
    @{source='bin/validation/CoreValidationModule.dll';name='CoreValidationModule.dll'},
    @{source='tests/Fixtures/CoreValidationModule.txt';name='README.txt'}
)) {
    $source = Join-Path $projectRoot $entry.source
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Validation input missing: $($entry.source)" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $entry.name) -Force
}
Write-Output ('Validation fixture SHA256: '+(Get-FileHash -LiteralPath (Join-Path $output 'CoreValidationModule.dll') -Algorithm SHA256).Hash.ToLowerInvariant())
