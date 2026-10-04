param([string]$ServerRoot, [string]$BuildRoot)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $ServerRoot) { $ServerRoot = Join-Path $projectRoot 'server' }
if (-not $BuildRoot) { $BuildRoot = Join-Path $projectRoot 'bin/Eternal' }
$server = [IO.Path]::GetFullPath($ServerRoot)
$build = [IO.Path]::GetFullPath($BuildRoot)
if (-not (Test-Path -LiteralPath (Join-Path $server 'server.properties'))) { throw 'BDS server.properties is required' }
foreach ($process in Get-CimInstance Win32_Process -Filter "Name='bedrock_server_mod.exe' OR Name='bedrock_server.exe'") {
    if ($process.ExecutablePath -and [IO.Path]::GetDirectoryName($process.ExecutablePath) -eq $server) {
        throw 'Server is running. Stop gracefully before replacing DLLs.'
    }
}
$pluginRoot = Join-Path $server 'plugins'
foreach ($plugin in Get-ChildItem -LiteralPath $pluginRoot -Directory) {
    if ($plugin.Name -notin @('LeviLamina','Eternal')) { throw "Remove the obsolete runtime plugin before deployment: $($plugin.Name)" }
}
$moduleNames = @('EternalCore','EternalCommerce','EternalLife','EternalWorld','EternalContent','EternalManagement','EternalPresentation','EternalEncounters')
foreach ($file in @('EternalHost.dll') + @($moduleNames | ForEach-Object { 'modules/'+$_+'.dll' })) {
    if (-not (Test-Path -LiteralPath (Join-Path $build $file) -PathType Leaf)) { throw "Built DLL missing: $file" }
}
$target = Join-Path $pluginRoot 'Eternal'
New-Item -ItemType Directory -Path $target,(Join-Path $target 'modules'),(Join-Path $target 'config') -Force | Out-Null
foreach ($directory in @('data','logs','resources')) {
    $path = Join-Path $target $directory
    if (-not (Test-Path -LiteralPath $path)) { New-Item -ItemType Directory -Path $path | Out-Null }
}
Copy-Item -LiteralPath (Join-Path $build 'EternalHost.dll') -Destination (Join-Path $target 'EternalHost.dll') -Force
foreach ($name in $moduleNames) {
    Copy-Item -LiteralPath (Join-Path $build ('modules/'+$name+'.dll')) -Destination (Join-Path $target ('modules/'+$name+'.dll')) -Force
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging/Eternal/manifest.json') -Destination (Join-Path $target 'manifest.json') -Force
$example = Join-Path $projectRoot 'config/modules.example.json'
Copy-Item -LiteralPath $example -Destination (Join-Path $target 'config/modules.example.json') -Force
$configuration = Join-Path $target 'config/modules.json'
if (-not (Test-Path -LiteralPath $configuration)) { Copy-Item -LiteralPath $example -Destination $configuration }
$coreConfig = Join-Path $target 'config/core'
New-Item -ItemType Directory -Path $coreConfig -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'config/core.example.json') -Destination (Join-Path $coreConfig 'core.example.json') -Force
Write-Output 'Deployed EternalHost and internal modules; existing configuration was preserved. No server startup was performed.'
