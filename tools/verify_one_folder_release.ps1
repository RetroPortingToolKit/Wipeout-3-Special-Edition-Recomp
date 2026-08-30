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

$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$profileVerifier = Join-Path $PSScriptRoot 'verify_release_freshness.py'
$exePath = Join-Path $root $ExeName
if (-not (Test-Path -LiteralPath $profileVerifier)) {
    $failures.Add("missing release profile verifier: $profileVerifier")
} elseif (Test-Path -LiteralPath $exePath) {
    & python $profileVerifier $exePath --source-root $sourceRoot
    if ($LASTEXITCODE -ne 0) {
        $failures.Add('shipped source-profile verification failed')
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
Write-Output 'PASS: executable, assets, bezels, packages, and source-owned defaults are present'
Write-Output 'PASS: developer BIOS/disc/settings sidecars and retired mod payloads are absent'
