param([string]$Root = (Split-Path -Parent $PSScriptRoot), [switch]$SourceOnly)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath($Root)
$modules = @('EternalCore','EternalCommerce','EternalLife','EternalWorld','EternalContent','EternalManagement','EternalPresentation','EternalEncounters')
$failures = [Collections.Generic.List[string]]::new()
if (-not $SourceOnly) {
    $pluginRoot = Join-Path $workspace 'server/plugins'
    foreach ($directory in Get-ChildItem -LiteralPath $pluginRoot -Directory) {
        if ($directory.Name -notin @('LeviLamina','Eternal')) { $failures.Add("Unexpected runtime plugin: $($directory.Name)") }
        foreach ($file in Get-ChildItem -LiteralPath $directory.FullName -Recurse -File) {
            if ($file.Extension -in @('.js','.lua')) { $failures.Add("Legacy runtime source: $($file.Name)") }
        }
    }
}
$components = @((Get-ChildItem -LiteralPath (Join-Path $workspace 'modules') -Directory).FullName)
$components += Join-Path $workspace 'host/EternalHost'
foreach ($component in $components) {
    $name = Split-Path -Leaf $component
    foreach ($file in Get-ChildItem -LiteralPath $component -Recurse -File) {
        if ($file.Extension -notin @('.cpp','.hpp','.h','.c')) { continue }
        $text = Get-Content -LiteralPath $file.FullName -Raw
        foreach ($match in [regex]::Matches($text, '#\s*include\s*[<"]([^>"\r\n]+)[>"]')) {
            $include = $match.Groups[1].Value.Replace([char]92,[char]47)
            foreach ($other in $modules | Where-Object { $_ -ne $name }) {
                if ($include -match "(^|/)$other(/|\.)") { $failures.Add("Cross-module private include: $($file.Name) -> $include") }
            }
        }
    }
}
if ($failures.Count) {
    $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    exit 1
}
Write-Output 'PASS: Host/internal module source boundary and requested runtime allowlist'
Write-Output 'LIMIT: static checks do not prove absence of dynamically constructed private database paths'
