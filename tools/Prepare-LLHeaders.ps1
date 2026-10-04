$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $projectRoot '.deps/ll-headers'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$lockPath = Join-Path $projectRoot 'docs/toolchain-lock.json'
$expectedHashes = @{}
if (Test-Path -LiteralPath $lockPath) {
    foreach ($header in (Get-Content -Raw -LiteralPath $lockPath | ConvertFrom-Json).headers) {
        $expectedHashes[$header.name] = $header.sha256
    }
}
# Exact source dependency versions from LeviLamina v26.51.6 xmake.lua.
$packages = @(
    @{name='concurrentqueue';repo='cameron314/concurrentqueue';ref='refs/tags/v1.0.4';include=''},
    @{name='parallel-hashmap';repo='greg7mdp/parallel-hashmap';ref='refs/tags/v2.0.0';include=''},
    @{name='fmt';repo='fmtlib/fmt';ref='refs/tags/11.2.0';include='include'},
    @{name='expected-lite';repo='martinmoene/expected-lite';ref='f339d2f73730f8fee4412f5e4938717866ecef48';include='include'},
    @{name='magic_enum';repo='Neargye/magic_enum';ref='refs/tags/v0.9.7';include='include'},
    @{name='json';repo='nlohmann/json';ref='refs/tags/v3.12.0';include='include'},
    @{name='gsl';repo='microsoft/GSL';ref='refs/tags/v4.2.0';include='include'},
    @{name='glm';repo='g-truc/glm';ref='refs/tags/1.0.1';include=''},
    @{name='entt';repo='skypjack/entt';ref='refs/tags/v4.0.0';include='src'},
    @{name='pfr';repo='boostorg/pfr';ref='294a4976bd04829dd204aaf9e9fd30338a5d3199';include='include'},
    @{name='leveldb';repo='google/leveldb';ref='refs/tags/1.23';include='include'},
    @{name='rapidjson';repo='Tencent/rapidjson';ref='24b5e7a8b27f42fa16b96fc70aade9106cf7102f';include='include'},
    @{name='type_safe';repo='foonathan/type_safe';ref='refs/tags/v0.2.4';include='include'},
    @{name='debug_assert';repo='foonathan/debug_assert';ref='refs/tags/v1.3.4';include=''},
    @{name='stb';repo='nothings/stb';ref='f0569113c93ad095470c54bf34a17b36646bbbb5';include=''}
)
$records = $packages | ForEach-Object -Parallel {
    $ErrorActionPreference = 'Stop'
    $package = $_
    $outputRoot = $using:outputRoot
    $expectedHashes = $using:expectedHashes
    $archive = Join-Path $outputRoot ($package.name+'.zip')
    $unpack = Join-Path $outputRoot $package.name
    $url = 'https://codeload.github.com/'+$package.repo+'/zip/'+$package.ref
    if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest $url -OutFile $archive }
    $archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expectedHashes.ContainsKey($package.name) -and $archiveHash -ne $expectedHashes[$package.name]) {
        throw "Header archive hash mismatch: $($package.name)"
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $unpack -Force
    $source = (Get-ChildItem -LiteralPath $unpack -Directory | Select-Object -First 1).FullName
    [pscustomobject]@{name=$package.name;url=$url;ref=$package.ref;sha256=$archiveHash;include=(Join-Path $source $package.include)}
} -ThrottleLimit 5
$records | Sort-Object name | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputRoot 'headers.lock.json') -Encoding utf8
$records | Sort-Object name | Format-Table name,include -AutoSize
