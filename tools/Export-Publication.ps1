param([Parameter(Mandatory=$true)][string]$GitPath)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskOutput=Join-Path $taskRoot '.deps/publication'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$entries=[Collections.Generic.List[object]]::new()
$utf8=[Text.UTF8Encoding]::new($false,$true)
$paths=& $GitPath -C $taskRoot ls-files
if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate reviewed Git index' }
foreach ($relative in $paths) {
    $absolute=[IO.Path]::GetFullPath((Join-Path $taskRoot $relative))
    if (-not $absolute.StartsWith($taskRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'File outside checkout' }
    $bytes=[IO.File]::ReadAllBytes($absolute)
    if ($bytes.Length -gt 2MB -or $bytes -contains 0) { throw "Non-text or oversized public file: $relative" }
    $indexed=& $GitPath -C $taskRoot rev-parse (':'+$relative)
    $working=& $GitPath -C $taskRoot hash-object --path=$relative $absolute
    if ($indexed -ne $working) { throw "Reviewed index changed: $relative" }
    # All permitted public files use eol=lf. Match Git's reviewed blob exactly.
    $content=$utf8.GetString($bytes).Replace("`r`n","`n")
    $entries.Add(@{path=$relative;mode='100644';type='blob';content=$content})
}
# Chunk only the local transport. Publication must follow the separate security
# audit and a human-readable git status / git diff --cached review.
$batch=[Collections.Generic.List[object]]::new()
$size=0
$part=0
foreach ($entry in $entries) {
    $entrySize=($entry | ConvertTo-Json -Compress -Depth 4).Length
    if ($size+$entrySize -gt 60000 -and $batch.Count -gt 0) {
        ConvertTo-Json -InputObject @($batch.ToArray()) -Depth 4 -Compress | Set-Content -LiteralPath (Join-Path $taskOutput ('files-{0:D3}.json' -f $part)) -Encoding utf8NoBOM
        $part++
        $batch.Clear()
        $size=0
    }
    $batch.Add($entry)
    $size+=$entrySize
}
if ($batch.Count) {
    ConvertTo-Json -InputObject @($batch.ToArray()) -Depth 4 -Compress | Set-Content -LiteralPath (Join-Path $taskOutput ('files-{0:D3}.json' -f $part)) -Encoding utf8NoBOM
    $part++
}
Write-Output "Exported $($entries.Count) reviewed text files in $part local chunks; no Git history exported."
