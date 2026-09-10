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

$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$profileVerifier = Join-Path $PSScriptRoot 'verify_release_freshness.py'
if (-not (Test-Path -LiteralPath $profileVerifier)) {
    throw "Missing release profile verifier: $profileVerifier"
}
& python $profileVerifier $exe.FullName --source-root $sourceRoot
if ($LASTEXITCODE -ne 0) {
    throw 'Staged release profile does not match the current source profile.'
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
$sourceCmake = Get-Content -Raw -LiteralPath (Join-Path $sourceRoot 'CMakeLists.txt')
$ramContract = [regex]::Match(
    $sourceCmake,
    'PSX_MAIN_RAM_BYTES=(?<bytes>[^\s\)]+)'
)
if ($ramContract.Success) {
    $expectedDefine = 'PSX_MAIN_RAM_BYTES=' + $ramContract.Groups['bytes'].Value
    if ($buildGraphText -notmatch [regex]::Escape($expectedDefine)) {
        throw "Release build graph does not compile the source RAM contract: $expectedDefine"
    }
}

$staged = Get-ChildItem -LiteralPath $build -Recurse -File
$badBios = $staged | Where-Object { $_.Name -match '(?i)^openbios(\.bin)?$|^OpenBIOS\.LICENSE$' }
if ($badBios) { throw "Redistributable OpenBIOS artifact staged: $($badBios[0].FullName)" }
$badCache = $staged | Where-Object { $_.FullName -match '(?i)[\\/]overlay_captures\.json$|[\\/]cache[\\/]' }
if ($badCache) { throw "Runtime overlay/compiler artifact staged: $($badCache[0].FullName)" }

Write-Output ('PASS static-release-boundary build={0} exe={1}' -f $build, $exe.Name)
