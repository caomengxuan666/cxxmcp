param(
    [switch]$Check,
    [switch]$List,
    [string]$ClangFormat = "clang-format"
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$CopyrightLine = "// Copyright (c) 2025 [caomengxuan666]"
$CopyrightPattern = "^// Copyright \(c\) 2025 \[[^\]]+\]$"

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

function Get-SourceFiles {
    param([string]$StartDirectory)

    $PendingDirectories = [System.Collections.Generic.Stack[System.IO.DirectoryInfo]]::new()
    $PendingDirectories.Push([System.IO.DirectoryInfo]::new($StartDirectory))

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
    param([string]$Path)

    $FullPath = [System.IO.Path]::GetFullPath($Path)
    $RootPrefix = $Root.TrimEnd("\", "/") + [System.IO.Path]::DirectorySeparatorChar

    if ($FullPath.StartsWith($RootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $FullPath.Substring($RootPrefix.Length).Replace("\", "/")
    }

    return $FullPath.Replace("\", "/")
}

function Test-HasCopyrightHeader {
    param([string]$Path)

    $FirstLine = Get-Content -LiteralPath $Path -TotalCount 1 -ErrorAction SilentlyContinue
    return $FirstLine -match $CopyrightPattern
}

function Add-CopyrightHeader {
    param([string]$Path)

    if (Test-HasCopyrightHeader $Path) {
        return
    }

    $Content = [System.IO.File]::ReadAllText($Path)
    $Content = "$CopyrightLine`n`n$Content"
    [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}

$Files = @(Get-SourceFiles $Root | Sort-Object)

if ($List) {
    $Files | ForEach-Object { Convert-ToRelativePath $_ }
    exit 0
}

if ($Files.Count -eq 0) {
    Write-Host "No source files found."
    exit 0
}

if ($Check) {
    $FailedFiles = @()

    foreach ($File in $Files) {
        if (-not (Test-HasCopyrightHeader $File)) {
            $FailedFiles += $File
            Write-Host "Missing copyright header: $(Convert-ToRelativePath $File)"
            continue
        }

        $Output = & $ClangFormat --dry-run --Werror --style=file $File 2>&1
        if ($LASTEXITCODE -ne 0) {
            $FailedFiles += $File
            $Output | ForEach-Object { Write-Host $_ }
        }
    }

    if ($FailedFiles.Count -gt 0) {
        Write-Error "$($FailedFiles.Count) source file(s) need clang-format."
        exit 1
    }

    Write-Host "All $($Files.Count) source file(s) are formatted."
    exit 0
}

foreach ($File in $Files) {
    Add-CopyrightHeader $File
    & $ClangFormat -i --style=file $File
    if ($LASTEXITCODE -ne 0) {
        Write-Error "clang-format failed for $(Convert-ToRelativePath $File)."
        exit 1
    }
}

Write-Host "Formatted $($Files.Count) source file(s)."
