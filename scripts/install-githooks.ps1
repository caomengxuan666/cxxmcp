param()

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$HooksPath = Join-Path $Root "scripts/githooks"

git config --local core.hooksPath $HooksPath
Write-Host "core.hooksPath set to scripts/githooks"
