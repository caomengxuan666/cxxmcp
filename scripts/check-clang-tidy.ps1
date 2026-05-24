param(
    [switch]$List,
    [string]$BuildDir = "",
    [string]$ClangTidy = "clang-tidy",
    [string[]]$Path = @()
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

$SourceExtensions = @(
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
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

function Convert-ToRelativePath {
    param([string]$InputPath)

    return [System.IO.Path]::GetRelativePath($Root, $InputPath).Replace("\", "/")
}

function Test-PathUnderRoot {
    param([string]$InputPath)

    $RelativePath = [System.IO.Path]::GetRelativePath($Root, $InputPath)
    return -not ($RelativePath.StartsWith("..", [System.StringComparison]::Ordinal) -or
        [System.IO.Path]::IsPathRooted($RelativePath))
}

function Test-IgnoredProjectPath {
    param([string]$InputPath)

    if (-not (Test-PathUnderRoot $InputPath)) {
        return $true
    }

    $RelativePath = [System.IO.Path]::GetRelativePath($Root, $InputPath)
    $Segments = $RelativePath -split '[\\/]'

    for ($Index = 0; $Index -lt ($Segments.Count - 1); $Index++) {
        $Segment = $Segments[$Index]

        if ($IgnoredDirectoryNames -contains $Segment) {
            return $true
        }

        foreach ($Prefix in $IgnoredDirectoryPrefixes) {
            if ($Segment.StartsWith($Prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                return $true
            }
        }
    }

    return $false
}

function Test-SourceFile {
    param([string]$InputPath)

    $Extension = [System.IO.Path]::GetExtension($InputPath).ToLowerInvariant()
    return ($SourceExtensions -contains $Extension) -and -not (Test-IgnoredProjectPath $InputPath)
}

function Resolve-InputPath {
    param([string]$InputPath)

    if ([System.IO.Path]::IsPathRooted($InputPath)) {
        return (Resolve-Path -LiteralPath $InputPath).Path
    }

    return (Resolve-Path -LiteralPath (Join-Path $Root $InputPath)).Path
}

function Test-FileUnderDirectory {
    param(
        [string]$File,
        [string]$Directory
    )

    $RelativePath = [System.IO.Path]::GetRelativePath($Directory, $File)
    return -not ($RelativePath.StartsWith("..", [System.StringComparison]::Ordinal) -or
        [System.IO.Path]::IsPathRooted($RelativePath))
}

function Resolve-CompileDatabaseDirectories {
    if ($BuildDir) {
        $ResolvedBuildDir = Resolve-InputPath $BuildDir
        $CompileCommandsPath = Join-Path $ResolvedBuildDir "compile_commands.json"

        if (-not (Test-Path -LiteralPath $CompileCommandsPath)) {
            Write-Error "compile_commands.json was not found in $ResolvedBuildDir."
            exit 2
        }

        return @($ResolvedBuildDir)
    }

    $CandidateDirectories = @(
        (Join-Path $Root "out/build/tests"),
        (Join-Path $Root "out/build/cli"),
        (Join-Path $Root "out/build/sdk"),
        (Join-Path $Root "out/build/tests-release"),
        (Join-Path $Root "out/build/cli-release"),
        (Join-Path $Root "out/build/sdk-release"),
        (Join-Path $Root "build"),
        (Join-Path $Root "build-ninja")
    )

    $OutBuildDirectory = Join-Path $Root "out/build"
    if (Test-Path -LiteralPath $OutBuildDirectory) {
        $CandidateDirectories += @(Get-ChildItem -LiteralPath $OutBuildDirectory -Directory -ErrorAction SilentlyContinue |
                ForEach-Object { $_.FullName })
    }

    $CompileDatabaseDirectories = @()

    foreach ($CandidateDirectory in ($CandidateDirectories | Select-Object -Unique)) {
        if (Test-Path -LiteralPath (Join-Path $CandidateDirectory "compile_commands.json")) {
            $CompileDatabaseDirectories += $CandidateDirectory
        }
    }

    if ($CompileDatabaseDirectories.Count -eq 0) {
        Write-Error "No compile_commands.json found. Run 'cmake --preset tests' or pass -BuildDir <dir>."
        exit 2
    }

    return @($CompileDatabaseDirectories)
}

function Get-CompileCommandEntries {
    param([string]$CompileDatabaseDirectory)

    $CompileCommandsPath = Join-Path $CompileDatabaseDirectory "compile_commands.json"
    $CompileCommands = Get-Content -LiteralPath $CompileCommandsPath -Raw | ConvertFrom-Json
    $Entries = [System.Collections.Generic.List[object]]::new()

    foreach ($Entry in $CompileCommands) {
        $File = [string]$Entry.file
        if (-not [System.IO.Path]::IsPathRooted($File)) {
            $File = Join-Path ([string]$Entry.directory) $File
        }

        if (-not (Test-Path -LiteralPath $File)) {
            continue
        }

        $ResolvedFile = (Resolve-Path -LiteralPath $File).Path
        if (Test-SourceFile $ResolvedFile) {
            $Entries.Add([pscustomobject]@{
                    File     = $ResolvedFile
                    BuildDir = $CompileDatabaseDirectory
                })
        }
    }

    return @($Entries)
}

$CompileDatabaseDirectories = @(Resolve-CompileDatabaseDirectories)
$EntriesByFile = [System.Collections.Generic.Dictionary[string, object]]::new([System.StringComparer]::OrdinalIgnoreCase)

foreach ($CompileDatabaseDirectory in $CompileDatabaseDirectories) {
    foreach ($Entry in (Get-CompileCommandEntries $CompileDatabaseDirectory)) {
        if (-not $EntriesByFile.ContainsKey($Entry.File)) {
            $EntriesByFile.Add($Entry.File, $Entry)
        }
    }
}

$Entries = @($EntriesByFile.Values | Sort-Object File)

if ($Path.Count -gt 0) {
    $FilterDirectories = @($Path | ForEach-Object { Resolve-InputPath $_ })
    $Entries = @($Entries | Where-Object {
            $File = $_.File
            @($FilterDirectories | Where-Object { Test-FileUnderDirectory $File $_ }).Count -gt 0
        })
}

if ($List) {
    $Entries | ForEach-Object { Convert-ToRelativePath $_.File }
    exit 0
}

if ($Entries.Count -eq 0) {
    Write-Host "No clang-tidy source files found."
    exit 0
}

if (-not (Get-Command $ClangTidy -ErrorAction SilentlyContinue)) {
    Write-Error "clang-tidy was not found. Install clang-tidy or pass -ClangTidy <path>." -ErrorAction Continue
    exit 2
}

$FailedFiles = @()

foreach ($Entry in $Entries) {
    $File = $Entry.File
    $CompileDatabaseDirectory = $Entry.BuildDir
    Write-Host "clang-tidy: $(Convert-ToRelativePath $File)"
    & $ClangTidy -p $CompileDatabaseDirectory $File
    if ($LASTEXITCODE -ne 0) {
        $FailedFiles += $File
    }
}

if ($FailedFiles.Count -gt 0) {
    Write-Error "clang-tidy failed for $($FailedFiles.Count) of $($Entries.Count) source file(s)."
    exit 1
}

$CompileDatabaseList = @($CompileDatabaseDirectories | ForEach-Object { Convert-ToRelativePath $_ }) -join ", "
Write-Host "clang-tidy passed for $($Entries.Count) source file(s) using $CompileDatabaseList."
