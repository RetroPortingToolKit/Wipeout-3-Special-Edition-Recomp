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
if ($cacheText -notmatch '(?m)^PSX_SETUP_WIZARD:BOOL=OFF\r?$') {
    throw 'Setup wizard is enabled or not explicitly disabled.'
}
if ($cacheText -notmatch '(?m)^PSXRECOMP_BIOS_STEMS:STRING=SCPH1001\r?$') {
    throw 'Build is not restricted to the retail SCPH1001 backend.'
}

$staged = Get-ChildItem -LiteralPath $build -Recurse -File
$badBios = $staged | Where-Object { $_.Name -match '(?i)^openbios(\.bin)?$|^OpenBIOS\.LICENSE$' }
if ($badBios) { throw "Redistributable OpenBIOS artifact staged: $($badBios[0].FullName)" }
$badCache = $staged | Where-Object { $_.FullName -match '(?i)[\\/]overlay_captures\.json$|[\\/]cache[\\/]' }
if ($badCache) { throw "Runtime overlay/compiler artifact staged: $($badCache[0].FullName)" }

Write-Output ('PASS static-release-boundary build={0} exe={1}' -f $build, $exe.Name)
