param([string]$Xmake = 'xmake')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$lock = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'build-lock.json') | ConvertFrom-Json
$recipeRoot = Join-Path $projectRoot $lock.recipe_repository.path
$env:XMAKE_MAIN_REPO = $recipeRoot
$test = Join-Path $PSScriptRoot 'recipe-selection-tests.lua'
$xmakeCommand = (Get-Command $Xmake -ErrorAction Stop).Source
Push-Location -LiteralPath $projectRoot
try {
    & $xmakeCommand 'lua' '-P' $projectRoot $test $projectRoot
    if ($LASTEXITCODE -ne 0) { throw 'Root official recipe selection failed' }
} finally { Pop-Location }
foreach ($order in @(@('levimc-repo','xmake-repo'),@('xmake-repo','levimc-repo'))) {
    $fixture = Join-Path $projectRoot ('artifacts/recipe-tests/'+$order[0])
    New-Item -ItemType Directory -Path $fixture -Force | Out-Null
    $repoLuaPath = $recipeRoot.Replace('\','/')
    [IO.File]::WriteAllText((Join-Path $fixture 'xmake.lua'),('add_repositories("levimc-repo '+$repoLuaPath+'")'),[Text.UTF8Encoding]::new($false))
    Push-Location -LiteralPath $fixture
    try {
        & $xmakeCommand 'repo' '-P' $fixture '--clear'
        if ($LASTEXITCODE -ne 0) { throw 'Cannot reset recipe test fixture cache' }
        foreach ($name in $order) {
            & $xmakeCommand 'repo' '-P' $fixture '--add' $name $recipeRoot
            if ($LASTEXITCODE -ne 0) { throw 'Cannot prepare nested recipe test fixture' }
        }
        & $xmakeCommand 'lua' '-P' $fixture $test $projectRoot
        if ($LASTEXITCODE -ne 0) { throw 'Nested official recipe selection failed' }
    } finally { Pop-Location }
}
Write-Output 'Official recipe selection passed for the root project and both child cache insertion orders.'
