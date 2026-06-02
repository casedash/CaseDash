param(
    [Parameter(Mandatory = $true)]
    [string]$Root,

    [ValidateSet("all", "changed", "staged")]
    [string]$Scope = "all",

    [Parameter(Mandatory = $true)]
    [string]$Output,

    [string]$Roots = "",

    [string]$Extensions = "",

    [string]$Prefix = ""
)

$ErrorActionPreference = "Stop"

function Invoke-GitLines {
    param([string[]]$Arguments)

    $lines = & git -C $Root -c core.quotepath=off -c core.safecrlf=false @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed: $($lines -join [Environment]::NewLine)"
    }
    return @($lines | Where-Object { $_ -is [string] -and $_.Trim().Length -gt 0 })
}

function Get-PathspecArgs {
    if ($script:RootList.Count -eq 0) {
        return @()
    }
    return @("--") + $script:RootList
}

function Test-HasHead {
    & git -C $Root rev-parse --verify HEAD *> $null
    return $LASTEXITCODE -eq 0
}

function Split-ListArgument {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return @()
    }
    return @($Value -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_.Length -gt 0 })
}

function Normalize-Prefix {
    param([string]$Value)

    $normalized = $Value.Replace("\", "/").Trim("/")
    return $normalized
}

$RootList = Split-ListArgument $Roots
$ExtensionList = Split-ListArgument $Extensions
$OutputPrefix = Normalize-Prefix $Prefix
$pathspec = Get-PathspecArgs
$paths = @()

if ($Scope -eq "all") {
    $paths += Invoke-GitLines (@("ls-files", "--cached", "--others", "--exclude-standard") + $pathspec)
} elseif ($Scope -eq "changed") {
    if (Test-HasHead) {
        $paths += Invoke-GitLines (@("diff", "--name-only", "--diff-filter=ACMR", "HEAD") + $pathspec)
    } else {
        $paths += Invoke-GitLines (@("diff", "--name-only", "--diff-filter=ACMR", "--cached") + $pathspec)
    }
    $paths += Invoke-GitLines (@("ls-files", "--others", "--exclude-standard") + $pathspec)
} elseif ($Scope -eq "staged") {
    $paths += Invoke-GitLines (@("diff", "--cached", "--name-only", "--diff-filter=ACMR") + $pathspec)
}

$extensionSet = @{}
foreach ($extension in $ExtensionList) {
    if ($extension.Length -gt 0) {
        $extensionSet[$extension.ToLowerInvariant()] = $true
    }
}

$selected = New-Object "System.Collections.Generic.List[string]"
$seen = New-Object "System.Collections.Generic.HashSet[string]" ([System.StringComparer]::OrdinalIgnoreCase)
foreach ($path in ($paths | Sort-Object)) {
    $normalized = $path.Replace("\", "/")
    if ($extensionSet.Count -gt 0) {
        $extension = [System.IO.Path]::GetExtension($normalized).ToLowerInvariant()
        if (-not $extensionSet.ContainsKey($extension)) {
            continue
        }
    }
    if (-not [System.IO.File]::Exists((Join-Path $Root $normalized))) {
        continue
    }
    $outputPath = $normalized
    if ($OutputPrefix.Length -gt 0) {
        $outputPath = "$OutputPrefix/$normalized"
    }
    if ($seen.Add($outputPath)) {
        $selected.Add($outputPath)
    }
}

$outputDirectory = Split-Path -Parent $Output
if ($outputDirectory) {
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}

$encoding = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines($Output, $selected, $encoding)
