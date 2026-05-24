param(
    [switch]$List,
    [string]$Cpplint = "cpplint",
    [string[]]$Path = @()
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

$SourceExtensions = @(
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inl",
    ".ipp",
    ".ixx",
    ".m",
    ".mm"
)

$IgnoredDirectoryNames = @(
    ".cache",
    ".git",
    ".vs",
    ".vscode",
    ".worktrees",
    "_deps",
    "build",
    "external",
    "node_modules",
    "out",
    "reference",
    "third_party",
    "vendor"
)

$IgnoredDirectoryPrefixes = @(
    "build-",
    "cmake-build-"
)

function Test-IgnoredDirectory {
    param([System.IO.DirectoryInfo]$Directory)

    if ($IgnoredDirectoryNames -contains $Directory.Name) {
        return $true
    }

    foreach ($Prefix in $IgnoredDirectoryPrefixes) {
        if ($Directory.Name.StartsWith($Prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }

    return $false
}

function Resolve-InputPath {
    param([string]$InputPath)

    if ([System.IO.Path]::IsPathRooted($InputPath)) {
        return (Resolve-Path -LiteralPath $InputPath).Path
    }

    return (Resolve-Path -LiteralPath (Join-Path $Root $InputPath)).Path
}

function Get-SourceFiles {
    param([string[]]$StartDirectories)

    $PendingDirectories = [System.Collections.Generic.Stack[System.IO.DirectoryInfo]]::new()

    foreach ($StartDirectory in $StartDirectories) {
        $PendingDirectories.Push([System.IO.DirectoryInfo]::new($StartDirectory))
    }

    while ($PendingDirectories.Count -gt 0) {
        $CurrentDirectory = $PendingDirectories.Pop()

        Get-ChildItem -LiteralPath $CurrentDirectory.FullName -Directory -Force -ErrorAction SilentlyContinue |
                Where-Object { -not (Test-IgnoredDirectory $_) } |
                ForEach-Object { $PendingDirectories.Push($_) }

        Get-ChildItem -LiteralPath $CurrentDirectory.FullName -File -Force -ErrorAction SilentlyContinue |
                Where-Object { $SourceExtensions -contains $_.Extension.ToLowerInvariant() } |
                ForEach-Object { $_.FullName }
    }
}

function Convert-ToRelativePath {
    param([string]$InputPath)

    return [System.IO.Path]::GetRelativePath($Root, $InputPath).Replace("\", "/")
}

$StartDirectories = @()
if ($Path.Count -gt 0) {
    $StartDirectories = @($Path | ForEach-Object { Resolve-InputPath $_ })
} else {
    $StartDirectories = @($Root)
}

$Files = @(Get-SourceFiles $StartDirectories | Sort-Object -Unique)

if ($List) {
    $Files | ForEach-Object { Convert-ToRelativePath $_ }
    exit 0
}

if ($Files.Count -eq 0) {
    Write-Host "No source files found."
    exit 0
}

if (Get-Command $Cpplint -ErrorAction SilentlyContinue) {
    $CpplintCommand = @($Cpplint)
}
elseif (Get-Command uv -ErrorAction SilentlyContinue) {
    $CpplintCommand = @("uv", "run", "--with", "cpplint", "cpplint")
}
else {
    Write-Error "cpplint was not found. Install cpplint, install uv, or pass -Cpplint <path>." -ErrorAction Continue
    exit 2
}

& $CpplintCommand @Files
if ($LASTEXITCODE -ne 0) {
    Write-Error "cpplint failed for $($Files.Count) source file(s)."
    exit $LASTEXITCODE
}

Write-Host "cpplint passed for $($Files.Count) source file(s)."
