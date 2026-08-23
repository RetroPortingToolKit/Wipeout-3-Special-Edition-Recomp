[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ReleaseDir,

    [string]$ExeName = 'Wipeout_3_Special_Edition__Recompiled.exe'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ReleaseDir).Path
$failures = [System.Collections.Generic.List[string]]::new()

function Require-Path([string]$RelativePath, [string]$Description) {
    $full = Join-Path $root $RelativePath
    if (-not (Test-Path -LiteralPath $full)) {
        $failures.Add("missing $Description`: $RelativePath")
    }
}

function Require-EnabledPackage([string]$PackageId) {
    $statePath = Join-Path $root 'mods/state.toml'
    $state = Get-Content -LiteralPath $statePath -Raw
    $escaped = [regex]::Escape($PackageId)
    $pattern = '(?ms)\[\[feature\]\]\s*package_id\s*=\s*"' + $escaped + '".*?\benabled\s*=\s*true\b'
    if ($state -notmatch $pattern) {
        $failures.Add("package is not enabled in mods/state.toml: $PackageId")
    }
}

Require-Path $ExeName 'portable executable'
Require-Path 'psx_game_version.txt' 'version stamp'
Require-Path 'game.toml' 'game configuration'
Require-Path 'assets/fonts' 'launcher fonts'
Require-Path 'assets/img' 'launcher images'
Require-Path 'bezels' 'bezel artwork'
Require-Path 'mods/packages' 'active mod packages'
Require-Path 'mods/state.toml' 'mod state'

$bezelDir = Join-Path $root 'bezels'
if (Test-Path -LiteralPath $bezelDir) {
    if (@(Get-ChildItem -LiteralPath $bezelDir -Filter '*.png' -File).Count -eq 0) {
        $failures.Add('bezels directory contains no PNG artwork')
    }
}

$retired = Join-Path $root 'mods/retired-packages'
if (Test-Path -LiteralPath $retired) {
    $failures.Add('release contains development-only mods/retired-packages')
}

$statePath = Join-Path $root 'mods/state.toml'
if (Test-Path -LiteralPath $statePath) {
    @(
        'psx.enhancement.8mb-ram'
        'psx.enhancement.fast-loading'
        'psx.enhancement.skip-fmv'
        'wipeout3.enhancement.framerate-pal100'
        'wipeout3.enhancement.widescreen'
    ) | ForEach-Object { Require-EnabledPackage $_ }

    $pgxpPattern = '(?ms)\[\[feature\]\]\s*package_id\s*=\s*"psx\.enhancement\.pgxp".*?\benabled\s*=\s*false\b'
    if ((Get-Content -LiteralPath $statePath -Raw) -notmatch $pgxpPattern) {
        $failures.Add('PGXP must be disabled in mods/state.toml for WipEout 3 SE')
    }

    $expectedPackages = @{
        'psx.enhancement.8mb-ram' = $true
        'psx.enhancement.fast-loading' = $true
        'psx.enhancement.pgxp' = $true
        'psx.enhancement.skip-fmv' = $true
        'wipeout3.enhancement.framerate-pal100' = $true
        'wipeout3.enhancement.widescreen' = $true
    }
    $packageDir = Join-Path $root 'mods/packages'
    if (Test-Path -LiteralPath $packageDir) {
        $stagedPackages = @(Get-ChildItem -LiteralPath $packageDir -Directory)
        foreach ($packageId in $expectedPackages.Keys) {
            if (($stagedPackages | Where-Object Name -eq $packageId).Count -eq 0) {
                $failures.Add("release is missing required active package: $packageId")
            }
        }
        $stagedPackages | ForEach-Object {
            if (-not $expectedPackages.ContainsKey($_.Name)) {
                $failures.Add("release contains an unconfigured mod package: $($_.Name)")
            }
        }
        $nestedLegacy = Get-ChildItem -LiteralPath $packageDir -Recurse -Directory |
            Where-Object { $_.Name -match '(?i)(ntsc|120|legacy|retired|duplicate)' }
        foreach ($directory in $nestedLegacy) {
            $failures.Add("release contains a retired/legacy package path: $($directory.FullName)")
        }
    }
}

# The high-detail profile must be explicit: a missing state file otherwise
# silently falls back to retail 2 MB RAM even when the executable is present.
$profileVerifier = Join-Path $PSScriptRoot 'verify_8mb_profile.py'
if (-not (Test-Path -LiteralPath $profileVerifier)) {
    $failures.Add("missing 8 MB profile verifier: $profileVerifier")
} else {
    & python $profileVerifier $root
    if ($LASTEXITCODE -ne 0) {
        $failures.Add('shipped 8 MB + 900% profile verification failed')
    }
}

# A portable release must not carry the developer's BIOS/disc paths or saved
# settings. The launcher creates fresh sidecars after the user selects them.
@('settings.toml', 'bios.cfg', 'disc.cfg', 'input.ini', 'keybinds.ini') | ForEach-Object {
    if (Test-Path -LiteralPath (Join-Path $root $_)) {
        $failures.Add("release contains machine/user state: $_")
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output "PASS: portable one-folder release layout verified: $root"
Write-Output 'PASS: executable, assets/fonts, assets/img, bezels, active mods, and enabled defaults are present'
Write-Output 'PASS: developer BIOS/disc/settings sidecars and retired mod payloads are absent'
