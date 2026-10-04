[CmdletBinding(DefaultParameterSetName = 'Staged')]
param(
    [Parameter(ParameterSetName = 'Staged')][switch]$Staged,
    [Parameter(Mandatory = $true, ParameterSetName = 'Tree')][string]$Treeish,
    [Parameter(Mandatory = $true, ParameterSetName = 'Work')][switch]$WorkingTree,
    [Parameter(Mandatory = $true, ParameterSetName = 'SelfTest')][switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

# Report paths/rule names only. Do not echo matched credentials or account IDs.
$contentRules = [ordered]@{
    'private-windows-path' = '(?i)[A-Z]:[\\/](?:Users[\\/]|MC_BDS)'
    'private-owner-metadata' = '(?i)["''](?:sourceOwnerName|privateWindowsrefs)["'']\s*:'
    'credential-assignment' = '(?i)(?:api[_-]?key|access[_-]?token|client[_-]?secret|password|private[_-]?key)\s*[:=]\s*["''][A-Za-z0-9_+/=-]{12,}["'']'
    'private-key-material' = '-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'
    'provider-token-candidate' = '(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{30,}|sk-[A-Za-z0-9_-]{20,}|AKIA[A-Z0-9]{16})'
}
$numericIdentity = '(?<![A-Za-z0-9])[1-9][0-9]{14,19}(?![A-Za-z0-9])'
# Deliberate integer boundary fixtures, not player identities.
$numericBoundaries = @('18446744073709551615', '18446744073709551616', '99999999999999999999')

function Get-ContentIssues([string]$Text) {
    $issues = @()
    foreach ($rule in $contentRules.GetEnumerator()) {
        if ([regex]::IsMatch($Text, $rule.Value)) { $issues += $rule.Key }
    }
    foreach ($match in [regex]::Matches($Text, $numericIdentity)) {
        if ($match.Value -notin $numericBoundaries) { $issues += 'private-numeric-identity-candidate'; break }
    }
    return $issues
}
function Get-PathIssues([string]$File) {
    $issues = @()
    if ($File -match '(?i)(^|/)(?:\.deps|\.work|private|local|secrets|worlds|data|storage|logs|backups|crashdumps|assets|node_modules)(/|$)') { $issues += 'private-runtime-or-dependency-tree' }
    if ($File -match '(?i)^server/' -and $File -ne 'server/README.md') { $issues += 'runtime-server-file' }
    if ($File -match '(?i)\.(?:dll|exe|lib|obj|o|pdb|db|db3|sqlite|sqlite3|ldb|sst|log|dmp|dump|zip|7z|nupkg|vsix)(?:-(?:wal|shm|journal))?$') { $issues += 'runtime-binary-data-or-archive' }
    if ($File -match '(?i)(^|/)(?:\.env(?:\..+)?|allowlist\.json|permissions\.json|ops\.json|credentials[^/]*|secrets[^/]*)$' -and $File -notmatch '(?i)(?:\.example|\.sample)$') { $issues += 'private-config-or-credentials' }
    if ($File -match '(?i)\.(?:pem|key|pfx|p12)$') { $issues += 'credential-file' }
    return $issues
}
function Invoke-Git([string[]]$Arguments) {
    $result = & git -C $projectRoot @Arguments
    if ($LASTEXITCODE -ne 0) { throw ('Git failed: ' + $Arguments[0]) }
    return $result
}

if ($SelfTest) {
    function Assert-Rule([bool]$Condition, [string]$Name) {
        if (-not $Condition) { throw ('Public scanner self-test failed: ' + $Name) }
    }
    Assert-Rule ((Get-ContentIssues ('const owner = "' + '12345678' + '90123456' + '";')).Count -gt 0) 'identity rejection'
    Assert-Rule ((Get-ContentIssues 'const owner = "1000";').Count -eq 0) 'synthetic identity'
    Assert-Rule ((Get-ContentIssues ('C:' + '\Users\' + 'ExampleUser\notes.txt')).Count -gt 0) 'private path rejection'
    Assert-Rule ((Get-ContentIssues ('password = "' + ('X' * 16) + '"')).Count -gt 0) 'credential rejection'
    Assert-Rule ((Get-ContentIssues ('-----BEGIN ' + 'PRIVATE KEY-----')).Count -gt 0) 'key rejection'
    Assert-Rule ((Get-ContentIssues ('max uint64=' + $numericBoundaries[0])).Count -eq 0) 'integer boundary'
    Assert-Rule ((Get-PathIssues 'server/worlds/test/db/000001.ldb').Count -gt 0) 'world rejection'
    Assert-Rule ((Get-PathIssues 'modules/EternalCore/domain/Core.cpp').Count -eq 0) 'source allowance'
    Assert-Rule ((Get-ContentIssues ('gh' + 'p_' + ('X' * 24))).Count -gt 0) 'provider token rejection'
    Write-Output 'PASS public scanner: 9 rule groups; no Git mutation'
    exit 0
}

$findings = @()
if ($PSCmdlet.ParameterSetName -eq 'Tree') {
    Invoke-Git @('rev-parse', '--verify', ($Treeish + '^{commit}')) | Out-Null
    $files = ((Invoke-Git @('-c', 'core.quotepath=false', 'ls-tree', '-r', '-z', '--name-only', $Treeish)) -join "`n").Split([char]0) | Where-Object { $_ }
    # Retain the local baseline, but never publish any of its ancestors.
    $baseline = & git -C $projectRoot rev-parse --verify 'phase1-native-core-baseline^{commit}' 2>$null
    if ($LASTEXITCODE -eq 0) {
        $privateAncestors = @(Invoke-Git @('rev-list', [string]$baseline))
        $publicAncestors = @(Invoke-Git @('rev-list', $Treeish))
        if (@($publicAncestors | Where-Object { $_ -in $privateAncestors }).Count -gt 0) {
            $findings += [PSCustomObject]@{file='[commit ancestry]';rule='private-baseline-ancestor'}
        }
    }
} elseif ($WorkingTree) {
    $files = ((Invoke-Git @('-c', 'core.quotepath=false', 'ls-files', '-z', '--cached', '--others', '--exclude-standard')) -join "`n").Split([char]0) | Where-Object { $_ }
} else {
    $files = ((Invoke-Git @('-c', 'core.quotepath=false', 'ls-files', '-z')) -join "`n").Split([char]0) | Where-Object { $_ }
}

$count = 0
foreach ($file in ($files | Sort-Object -Unique)) {
    if ($WorkingTree -and -not [IO.File]::Exists((Join-Path $projectRoot $file))) { continue }
    ++$count
    $pathIssues = @(Get-PathIssues $file)
    foreach ($rule in $pathIssues) { $findings += [PSCustomObject]@{file=$file;rule=$rule} }
    if ($pathIssues.Count -gt 0) { continue }
    if ($WorkingTree) {
        $bytes = [IO.File]::ReadAllBytes((Join-Path $projectRoot $file))
        if ($bytes.Length -gt 5MB) { $findings += [PSCustomObject]@{file=$file;rule='large-file-needs-review'}; continue }
        $text = [Text.Encoding]::UTF8.GetString($bytes)
    } else {
        $spec = if ($PSCmdlet.ParameterSetName -eq 'Tree') { $Treeish + ':' + $file } else { ':' + $file }
        $size = [long](Invoke-Git @('cat-file', '-s', $spec))
        if ($size -gt 5MB) { $findings += [PSCustomObject]@{file=$file;rule='large-file-needs-review'}; continue }
        $text = (Invoke-Git @('cat-file', '-p', $spec)) -join "`n"
    }
    if ($text.Contains([char]0)) { $findings += [PSCustomObject]@{file=$file;rule='unexpected-binary-blob'} }
    foreach ($rule in (Get-ContentIssues $text)) { $findings += [PSCustomObject]@{file=$file;rule=$rule} }
}
if ($findings.Count -gt 0) {
    $findings | Sort-Object file,rule -Unique | Format-Table -AutoSize
    throw ('Public tree audit failed; checked ' + $count + ' files. Matched values were not printed.')
}
Write-Output ('PASS public tree: ' + $PSCmdlet.ParameterSetName + ', ' + $count + ' files; review author metadata and licenses separately.')
