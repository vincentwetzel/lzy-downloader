<#
.SYNOPSIS
Prepares a reversible older-tool fixture for testing LzyDownloader's Update All flow.

.DESCRIPTION
Snapshots the current versions and settings, then downgrades the tools detected in
the standard Windows setup: pip-installed yt-dlp/gallery-dl, WinGet FFmpeg (which
also supplies ffprobe), and app-managed Deno. It does not run unless -Apply is
provided, and it asks for a final typed confirmation before changing anything.

Run while LzyDownloader is closed:
  .\tools\PrepareBinaryUpgradeTest.ps1
  .\tools\PrepareBinaryUpgradeTest.ps1 -Apply

After testing, use LzyDownloader's Set Up Required Tools > Update All. The script
stores a snapshot path in its final output. To restore the settings and the exact
Deno executable without relying on the application update path:
  .\tools\PrepareBinaryUpgradeTest.ps1 -RestoreSnapshot <snapshot-folder>

aria2c is deliberately opt-in because the application currently does not perform
a startup update check for it, so downgrading it would not exercise the new setup
dialog. Use -IncludeAria2c only if you want to test its package-manager state.
#>

[CmdletBinding()]
param(
    [switch]$Apply,
    [switch]$IncludeAria2c,
    [string]$DenoVersion = '2.8.0',
    [string]$FfmpegVersion = '8.0.1',
    [string]$Aria2cVersion = '1.36.0',
    [string]$RestoreSnapshot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory)] [string]$FilePath,
        [Parameter(Mandatory)] [string[]]$Arguments
    )

    Write-Host ("> {0} {1}" -f $FilePath, ($Arguments -join ' ')) -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

function Get-CommandPath {
    param([Parameter(Mandatory)] [string]$Name)

    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($command) {
        return $command.Path
    }
    return $null
}

function Get-CommandOutput {
    param([Parameter(Mandatory)] [string]$Path)

    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) {
        return $null
    }
    try {
        return (& $Path --version 2>&1 | Out-String).Trim()
    } catch {
        return "<unable to read version: $($_.Exception.Message)>"
    }
}

function Get-PythonForPipCommand {
    param([Parameter(Mandatory)] [string]$PackageCommand)

    $packagePath = Get-CommandPath $PackageCommand
    if (-not $packagePath) {
        return $null
    }
    $scriptsDirectory = Split-Path -Parent $packagePath
    $pythonPath = Join-Path (Split-Path -Parent $scriptsDirectory) 'python.exe'
    if (Test-Path -LiteralPath $pythonPath) {
        return $pythonPath
    }
    return $null
}

function Get-OlderPipVersion {
    param(
        [Parameter(Mandatory)] [string]$PythonPath,
        [Parameter(Mandatory)] [string]$PackageName,
        [Parameter(Mandatory)] [string]$CurrentVersionText
    )

    $currentVersion = ConvertTo-VersionOrNull $CurrentVersionText
    if (-not $currentVersion) {
        throw "Could not determine the installed version for $PackageName from '$CurrentVersionText'."
    }
    $raw = & $PythonPath -m pip index versions $PackageName --pre 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "pip could not list available versions for $PackageName."
    }
    $match = [regex]::Match($raw, 'Available versions:\s*(.+)')
    if (-not $match.Success) {
        throw "Could not parse pip's available-version list for $PackageName."
    }
    $versions = foreach ($versionText in $match.Groups[1].Value.Split(',')) {
        $versionText = $versionText.Trim()
        $version = ConvertTo-VersionOrNull $versionText
        if ($version -and $version -lt $currentVersion) {
            [pscustomobject]@{ Text = $versionText; Version = $version }
        }
    }
    $target = $versions | Sort-Object Version -Descending | Select-Object -First 1
    if (-not $target) {
        throw "No older pip release is available for $PackageName."
    }
    return $target.Text
}

function ConvertTo-VersionOrNull {
    param([string]$Value)

    $match = [regex]::Match($Value, '\d+(?:\.\d+){1,3}')
    if (-not $match.Success) {
        return $null
    }
    try {
        return [version]$match.Value
    } catch {
        return $null
    }
}

function Set-IniValue {
    param(
        [Parameter(Mandatory)] [string]$Path,
        [Parameter(Mandatory)] [string]$Section,
        [Parameter(Mandatory)] [string]$Key,
        [Parameter(Mandatory)] [string]$Value
    )

    $lines = [System.Collections.Generic.List[string]]::new()
    if (Test-Path -LiteralPath $Path) {
        foreach ($line in Get-Content -LiteralPath $Path) {
            $lines.Add([string]$line)
        }
    }
    $sectionLine = "[$Section]"
    $sectionStart = -1
    for ($index = 0; $index -lt $lines.Count; ++$index) {
        if ($lines[$index].Trim().Equals($sectionLine, [System.StringComparison]::OrdinalIgnoreCase)) {
            $sectionStart = $index
            break
        }
    }
    if ($sectionStart -lt 0) {
        if ($lines.Count -gt 0 -and $lines[$lines.Count - 1] -ne '') {
            $lines.Add('')
        }
        $lines.Add($sectionLine)
        $sectionStart = $lines.Count - 1
    }

    $sectionEnd = $lines.Count
    for ($index = $sectionStart + 1; $index -lt $lines.Count; ++$index) {
        if ($lines[$index] -match '^\s*\[') {
            $sectionEnd = $index
            break
        }
    }
    $keyExpression = '^\s*' + [regex]::Escape($Key) + '\s*='
    for ($index = $sectionStart + 1; $index -lt $sectionEnd; ++$index) {
        if ($lines[$index] -match $keyExpression) {
            $lines[$index] = "$Key=$Value"
            Set-Content -LiteralPath $Path -Value $lines -Encoding utf8
            return
        }
    }
    $lines.Insert($sectionEnd, "$Key=$Value")
    Set-Content -LiteralPath $Path -Value $lines -Encoding utf8
}

function Restore-Snapshot {
    param([Parameter(Mandatory)] [string]$SnapshotPath)

    $manifestPath = Join-Path $SnapshotPath 'snapshot.json'
    if (-not (Test-Path -LiteralPath $manifestPath)) {
        throw "Snapshot manifest was not found: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.settingsBackup -and (Test-Path -LiteralPath $manifest.settingsBackup)) {
        Copy-Item -LiteralPath $manifest.settingsBackup -Destination $manifest.settingsPath -Force
        Write-Host "Restored settings: $($manifest.settingsPath)" -ForegroundColor Green
    }
    if ($manifest.denoBackup -and (Test-Path -LiteralPath $manifest.denoBackup) -and
        $manifest.denoPath) {
        Copy-Item -LiteralPath $manifest.denoBackup -Destination $manifest.denoPath -Force
        Write-Host "Restored Deno: $($manifest.denoPath)" -ForegroundColor Green
    }
}

if ($RestoreSnapshot) {
    Restore-Snapshot -SnapshotPath $RestoreSnapshot
    exit 0
}

if ($env:OS -ne 'Windows_NT') {
    throw 'This test fixture targets the Windows package locations shown in the supplied screenshot.'
}

$lzyData = Join-Path $env:LOCALAPPDATA 'LzyDownloader'
$settingsPath = Join-Path $lzyData 'settings.ini'
$denoPath = Join-Path $lzyData 'bin\deno.exe'
$snapshotRoot = Join-Path $lzyData 'upgrade-flow-fixtures'
$snapshotPath = Join-Path $snapshotRoot (Get-Date -Format 'yyyyMMdd-HHmmss')

$ytDlpPath = Get-CommandPath 'yt-dlp'
$galleryDlPath = Get-CommandPath 'gallery-dl'
$ffmpegPath = Get-CommandPath 'ffmpeg'
$aria2cPath = Get-CommandPath 'aria2c'
$pythonPath = if ($ytDlpPath) { Get-PythonForPipCommand 'yt-dlp' } else { $null }

$plan = [ordered]@{
    'yt-dlp' = if ($ytDlpPath -and $pythonPath) { "pip via $pythonPath" } else { 'not found; skipped' }
    'gallery-dl' = if ($galleryDlPath -and $pythonPath) { "pip via $pythonPath" } else { 'not found; skipped' }
    'FFmpeg + FFprobe' = if ($ffmpegPath) { 'WinGet package Gyan.FFmpeg' } else { 'not found; skipped' }
    'Deno' = if (Test-Path -LiteralPath $denoPath) { "app-managed copy at $denoPath" } else { 'not found; skipped' }
    'aria2c' = if ($IncludeAria2c -and $aria2cPath) { 'WinGet package aria2.aria2 (not covered by startup update checks)' } else { 'skipped by default' }
}

Write-Host "Planned downgrade fixture:" -ForegroundColor Cyan
$plan.GetEnumerator() | ForEach-Object { Write-Host ("  {0}: {1}" -f $_.Key, $_.Value) }
Write-Host ""
if (-not $Apply) {
    Write-Host 'Dry run only. Re-run with -Apply after closing LzyDownloader.' -ForegroundColor Yellow
    exit 0
}

$confirmation = Read-Host 'Type DOWNGRADE to uninstall/reinstall package-managed tools and replace app-managed Deno'
if ($confirmation -cne 'DOWNGRADE') {
    throw 'Downgrade cancelled.'
}

New-Item -ItemType Directory -Path $snapshotPath -Force | Out-Null
$settingsBackup = if (Test-Path -LiteralPath $settingsPath) {
    Join-Path $snapshotPath 'settings.ini.backup'
} else {
    $null
}
if ($settingsBackup) {
    Copy-Item -LiteralPath $settingsPath -Destination $settingsBackup -Force
}
$denoBackup = if (Test-Path -LiteralPath $denoPath) {
    Join-Path $snapshotPath 'deno.exe.backup'
} else {
    $null
}
if ($denoBackup) {
    Copy-Item -LiteralPath $denoPath -Destination $denoBackup -Force
}

$manifest = [ordered]@{
    createdUtc = [DateTime]::UtcNow.ToString('o')
    settingsPath = $settingsPath
    settingsBackup = $settingsBackup
    denoPath = $denoPath
    denoBackup = $denoBackup
    before = [ordered]@{
        ytDlp = Get-CommandOutput $ytDlpPath
        galleryDl = Get-CommandOutput $galleryDlPath
        ffmpeg = Get-CommandOutput $ffmpegPath
        aria2c = Get-CommandOutput $aria2cPath
        deno = Get-CommandOutput $denoPath
    }
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $snapshotPath 'snapshot.json') -Encoding utf8

if ($settingsBackup) {
    Set-IniValue -Path $settingsPath -Section 'Binaries' -Key 'automatic_update_frequency' -Value 'weekly'
    Set-IniValue -Path $settingsPath -Section 'Binaries' -Key 'deno_last_automatic_update' -Value ([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())
}

if ($pythonPath) {
    if ($ytDlpPath) {
        $target = Get-OlderPipVersion -PythonPath $pythonPath -PackageName 'yt-dlp' -CurrentVersionText (Get-CommandOutput $ytDlpPath)
        Invoke-CheckedCommand -FilePath $pythonPath -Arguments @('-m', 'pip', 'install', '--force-reinstall', "yt-dlp==$target")
    }
    if ($galleryDlPath) {
        $target = Get-OlderPipVersion -PythonPath $pythonPath -PackageName 'gallery-dl' -CurrentVersionText (Get-CommandOutput $galleryDlPath)
        Invoke-CheckedCommand -FilePath $pythonPath -Arguments @('-m', 'pip', 'install', '--no-deps', '--force-reinstall', "gallery-dl==$target")
    }
}

if ($ffmpegPath) {
    $current = ConvertTo-VersionOrNull (Get-CommandOutput $ffmpegPath)
    $target = ConvertTo-VersionOrNull $FfmpegVersion
    if (-not $target -or (-not $current) -or $target -ge $current) {
        throw "FFmpeg fixture version $FfmpegVersion is not older than the detected version $(Get-CommandOutput $ffmpegPath)."
    }
    Invoke-CheckedCommand -FilePath 'winget' -Arguments @('uninstall', '--id', 'Gyan.FFmpeg', '--exact', '--silent', '--disable-interactivity')
    Invoke-CheckedCommand -FilePath 'winget' -Arguments @('install', '--id', 'Gyan.FFmpeg', '--version', $FfmpegVersion, '--exact', '--source', 'winget', '--force', '--accept-package-agreements', '--accept-source-agreements', '--disable-interactivity')
}

if (Test-Path -LiteralPath $denoPath) {
    $current = ConvertTo-VersionOrNull (Get-CommandOutput $denoPath)
    $target = ConvertTo-VersionOrNull $DenoVersion
    if (-not $target -or (-not $current) -or $target -ge $current) {
        throw "Deno fixture version $DenoVersion is not older than the detected version $(Get-CommandOutput $denoPath)."
    }
    # Use Deno's own versioned updater so the fixture preserves the exact
    # app-managed path that LzyDownloader will later update.
    Invoke-CheckedCommand -FilePath $denoPath -Arguments @('upgrade', '--version', $DenoVersion)
}

if ($IncludeAria2c -and $aria2cPath) {
    $current = ConvertTo-VersionOrNull (Get-CommandOutput $aria2cPath)
    $target = ConvertTo-VersionOrNull $Aria2cVersion
    if (-not $target -or (-not $current) -or $target -ge $current) {
        throw "aria2c fixture version $Aria2cVersion is not older than the detected version $(Get-CommandOutput $aria2cPath)."
    }
    Invoke-CheckedCommand -FilePath 'winget' -Arguments @('uninstall', '--id', 'aria2.aria2', '--exact', '--silent', '--disable-interactivity')
    Invoke-CheckedCommand -FilePath 'winget' -Arguments @('install', '--id', 'aria2.aria2', '--version', $Aria2cVersion, '--exact', '--source', 'winget', '--force', '--accept-package-agreements', '--accept-source-agreements', '--disable-interactivity')
}

Write-Host "" 
Write-Host "Downgrade fixture created. Snapshot: $snapshotPath" -ForegroundColor Green
Write-Host 'Start LzyDownloader and test Set Up Required Tools > Update All.' -ForegroundColor Green
Write-Host 'Use -RestoreSnapshot with the path above if you need to put Deno and settings back immediately.' -ForegroundColor Yellow
