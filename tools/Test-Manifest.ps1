# Shared reporting only: actual test executables decide success.
$testDefinition = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'test-suites.json') -Raw | ConvertFrom-Json
$testResults = [Collections.Generic.List[object]]::new()
function Add-TestResult([string]$Name, [object[]]$Output = @(), [string]$Result = 'PASS') {
    $property = $testDefinition.suites.PSObject.Properties[$Name]
    if (-not $property -or $property.Value -notin $testDefinition.classifications) { throw "Unclassified test suite: $Name" }
    $counts = [regex]::Matches(($Output -join "`n"), '(\d+)\s+(?:[A-Za-z0-9 ]+\s+)?groups\b')
    $count = if ($counts.Count) { [int]$counts[$counts.Count-1].Groups[1].Value } else { $null }
    $testResults.Add([pscustomobject]@{suite=$Name;classification=$property.Value;result=$Result;groups=$count})
}
function Write-TestManifest([string]$Relative) {
    $path = Join-Path (Split-Path -Parent $PSScriptRoot) $Relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
    $missing = @($testDefinition.suites.PSObject.Properties | Where-Object { $_.Name -notin $testResults.suite } | ForEach-Object { [pscustomobject]@{suite=$_.Name;classification=$_.Value;result='NOT_RUN';groups=$null} })
    $external = @(
        [pscustomobject]@{suite='BDSLifecycle';classification='REAL_BDS';result='NOT_RUN';details='This build/test command does not launch BDS; consult separately captured real-server evidence.'},
        [pscustomobject]@{suite='GitHubActions';classification='CLOUD_CI';result=$(if ($env:GITHUB_ACTIONS -eq 'true') {'IN_PROGRESS'} else {'NOT_RUN'});details='Only the completed workflow run proves the cloud pipeline result.'},
        [pscustomobject]@{suite='AuthenticatedMinecraftClient';classification='REAL_CLIENT';result='DEFERRED_REAL_CLIENT';details='Requires a real authenticated Minecraft client; synthetic identities and BDS startup do not satisfy this category.'}
    )
    [pscustomobject]@{classifications=$testDefinition.classifications;suites=@($testResults.ToArray())+$missing+$external;generated_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $path -Encoding utf8
    Write-Output ('Test classification manifest: '+$Relative)
}
