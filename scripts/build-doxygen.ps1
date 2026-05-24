param(
    [switch]$Clean,
    [string]$Doxygen = "doxygen",
    [string]$Config = "docs/Doxyfile"
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ConfigPath = if ([System.IO.Path]::IsPathRooted($Config)) {
    (Resolve-Path -LiteralPath $Config).Path
} else {
    (Resolve-Path -LiteralPath (Join-Path $Root $Config)).Path
}

$OutputDirectory = Join-Path $Root "docs/doxygen"

if (-not (Get-Command $Doxygen -ErrorAction SilentlyContinue)) {
    Write-Error "doxygen was not found. Install Doxygen or pass -Doxygen <path>." -ErrorAction Continue
    exit 2
}

if ($Clean -and (Test-Path -LiteralPath $OutputDirectory)) {
    Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
}

Push-Location $Root
try {
    & $Doxygen $ConfigPath
    if ($LASTEXITCODE -ne 0) {
        Write-Error "doxygen failed with exit code $LASTEXITCODE."
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}

$IndexPath = Join-Path $OutputDirectory "html/index.html"
if (Test-Path -LiteralPath $IndexPath) {
    Write-Host "Doxygen HTML generated at $IndexPath"
} else {
    Write-Error "doxygen finished, but $IndexPath was not found."
    exit 1
}
