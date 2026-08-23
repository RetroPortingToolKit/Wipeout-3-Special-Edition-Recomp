[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BuildDir
)

$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDir).Path
$cache = Join-Path $build 'CMakeCache.txt'
$exe = Get-ChildItem -LiteralPath $build -Filter '*.exe' -File | Select-Object -First 1
if (-not (Test-Path -LiteralPath $cache)) { throw "Missing CMakeCache.txt: $build" }
if ($null -eq $exe) { throw "No executable staged in: $build" }

$cacheText = Get-Content -Raw -LiteralPath $cache
$buildGraph = Join-Path $build 'build.ninja'
if (-not (Test-Path -LiteralPath $buildGraph)) { throw "Missing generated build graph: $buildGraph" }
$buildGraphText = Get-Content -Raw -LiteralPath $buildGraph
$config = Join-Path $build 'game.toml'
if (-not (Test-Path -LiteralPath $config)) { throw "Missing staged game.toml: $build" }
$configText = Get-Content -Raw -LiteralPath $config
if ($configText -notmatch '(?m)^openbios\s*=\s*false\r?$') {
    throw 'OpenBIOS is not explicitly disabled in the staged game config.'
}
if ($configText -notmatch '(?m)^bios_hle\s*=\s*true\r?$') {
    throw 'BIOS intro skip is not explicitly enabled in the staged game config.'
}
if ($configText -notmatch '(?m)^supersampling\s*=\s*4\r?$') {
    throw 'The clean release does not carry the required 4x supersampling baseline.'
}
if ($configText -notmatch '(?m)^hd_textures\s*=\s*true\r?$') {
    throw 'HD texture replacement is not enabled in the remaster configuration.'
}
if ($configText -notmatch '(?m)^cpu_overclock\s*=\s*900\r?$') {
    throw 'The release does not carry the required 900% CPU overclock.'
}
foreach ($setting in @('geometry_correction', 'perspective_texturing', 'pgxp_depth')) {
    if ($configText -notmatch ('(?m)^' + [regex]::Escape($setting) + '\s*=\s*true\r?$')) {
        throw "Full PGXP setting is not enabled in the staged game config: $setting"
    }
}
if ($configText -notmatch '(?m)^offer_ultrawide\s*=\s*true\r?$') {
    throw 'Widescreen configuration does not offer ultrawide output.'
}
if ($configText -notmatch '(?m)^auto_skip_fmv\s*=\s*true\r?$') {
    throw 'FMV skipping is not enabled in the remaster configuration.'
}
$skipManifest = Join-Path $build 'mods/packages/psx.enhancement.skip-fmv/1.0.0/manifest.toml'
if (-not (Test-Path -LiteralPath $skipManifest)) { throw "Missing staged skip-opening manifest: $skipManifest" }
if ((Get-Content -Raw -LiteralPath $skipManifest) -notmatch '(?m)^default_enabled\s*=\s*false\r?$') {
    throw 'Skip-opening-sequence is enabled by default; it must remain user-controlled.'
}
$pgxpManifest = Join-Path $build 'mods/packages/psx.enhancement.pgxp/1.0.0/manifest.toml'
if (-not (Test-Path -LiteralPath $pgxpManifest)) { throw "Missing staged PGXP manifest: $pgxpManifest" }
if ((Get-Content -Raw -LiteralPath $pgxpManifest) -notmatch '(?m)^default_enabled\s*=\s*true\r?$') {
    throw 'PGXP precision is not enabled by default in the remaster build.'
}
$fr120 = Join-Path $build 'mods/packages/wipeout3.enhancement.framerate-120/1.0.0/manifest.toml'
if (Test-Path -LiteralPath $fr120) {
    if ((Get-Content -Raw -LiteralPath $fr120) -match '(?m)^default_enabled\s*=\s*true\r?$') {
        throw 'Experimental NTSC-120 package is enabled by default for a PAL release.'
    }
}
$pal100 = Join-Path $build 'mods/packages/wipeout3.enhancement.framerate-pal100/1.0.0/manifest.toml'
if (-not (Test-Path -LiteralPath $pal100)) { throw "Missing staged PAL x2 manifest: $pal100" }
if ((Get-Content -Raw -LiteralPath $pal100) -notmatch '(?m)^default_enabled\s*=\s*true\r?$') {
    throw 'PAL x2 / 100 Hz is not enabled by default; the release requirement is automatic 100 Hz for the authenticated PAL title.'
}
$allowedPackages = @{
    'psx.enhancement.8mb-ram' = $true
    'psx.enhancement.fast-loading' = $true
    'psx.enhancement.pgxp' = $true
    'psx.enhancement.skip-fmv' = $true
    'wipeout3.enhancement.framerate-pal100' = $true
    'wipeout3.enhancement.widescreen' = $true
}
$packageRoot = Join-Path $build 'mods/packages'
if (-not (Test-Path -LiteralPath $packageRoot)) { throw "Missing staged package root: $packageRoot" }
$stagedPackages = @(Get-ChildItem -LiteralPath $packageRoot -Directory | Select-Object -ExpandProperty Name)
foreach ($package in $stagedPackages) {
    if (-not $allowedPackages.ContainsKey($package)) {
        throw "Retired, legacy, experimental, or duplicate package staged: $package"
    }
}
foreach ($package in $allowedPackages.Keys) {
    if ($stagedPackages -notcontains $package) { throw "Required active package is missing from staging: $package" }
}
$retiredRoot = Join-Path $build 'mods/retired-packages'
if (Test-Path -LiteralPath $retiredRoot) { throw "Retired package payload staged: $retiredRoot" }
$state = Join-Path $build 'mods/state.toml'
if (-not (Test-Path -LiteralPath $state)) { throw "Missing staged mod state: $state" }
$stateText = Get-Content -Raw -LiteralPath $state
if ($stateText -notmatch '(?ms)\[\[feature\]\]\s*package_id\s*=\s*"wipeout3\.enhancement\.framerate-pal100".*?\benabled\s*=\s*true\b') {
    throw 'PAL x2 / 100 Hz is not enabled in staged mods/state.toml.'
}
if ($cacheText -notmatch '(?m)^PSX_SETUP_WIZARD:BOOL=OFF\r?$') {
    throw 'Setup wizard is enabled or not explicitly disabled.'
}
if ($cacheText -notmatch '(?m)^PSXRECOMP_BIOS_STEMS:STRING=SCPH1001\r?$') {
    throw 'Build is not restricted to the retail SCPH1001 backend.'
}
if ($buildGraphText -match '(?i)(psx_interpreter\.c|psx_interpreter\.c\.obj)') {
    throw 'Release build graph links the real interpreter; native builds must use stub_interpreter.c.'
}

$staged = Get-ChildItem -LiteralPath $build -Recurse -File
$badBios = $staged | Where-Object { $_.Name -match '(?i)^openbios(\.bin)?$|^OpenBIOS\.LICENSE$' }
if ($badBios) { throw "Redistributable OpenBIOS artifact staged: $($badBios[0].FullName)" }
$badCache = $staged | Where-Object { $_.FullName -match '(?i)[\\/]overlay_captures\.json$|[\\/]cache[\\/]' }
if ($badCache) { throw "Runtime overlay/compiler artifact staged: $($badCache[0].FullName)" }

Write-Output ('PASS static-release-boundary build={0} exe={1}' -f $build, $exe.Name)
