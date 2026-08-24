param(
    [Parameter(Mandatory = $true)]
    [string] $Executable,

    [string] $WorkingDirectory,
    [string] $StdoutLog,
    [string] $StderrLog,
    [string[]] $Argument = @(),
    [hashtable] $Environment = @{}
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $WorkingDirectory) {
    $WorkingDirectory = Split-Path -Parent $resolvedExecutable
}
$resolvedWorkingDirectory = (Resolve-Path -LiteralPath $WorkingDirectory).Path

function Get-CandidateProcesses {
    Get-CimInstance Win32_Process -Filter "Name='Wipeout_3_Special_Edition__Recompiled_pgxp.exe'" |
        Where-Object { $_.ExecutablePath -eq $resolvedExecutable }
}

# A candidate launch is a single-instance operation.  Scope cleanup to the
# exact executable path; never kill another build or an unrelated process.
foreach ($process in @(Get-CandidateProcesses)) {
    Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 250
if (@(Get-CandidateProcesses).Count -ne 0) {
    throw "A prior candidate process is still running: $resolvedExecutable"
}

$start = @{
    FilePath         = $resolvedExecutable
    # Start-Process joins array entries into one command line. Quote path-like
    # entries here so a CUE/BIOS path containing spaces remains one argv item.
    ArgumentList     = @($Argument | ForEach-Object {
        if ($_ -match '\s' -and -not ($_.StartsWith('"') -and $_.EndsWith('"'))) {
            '"' + $_.Replace('"', '\\"') + '"'
        } else {
            $_
        }
    })
    WorkingDirectory = $resolvedWorkingDirectory
    PassThru          = $true
}
if ($StdoutLog) {
    $start.RedirectStandardOutput = (Resolve-Path -LiteralPath $StdoutLog -ErrorAction SilentlyContinue)?.Path
    if (-not $start.RedirectStandardOutput) {
        $start.RedirectStandardOutput = [IO.Path]::GetFullPath($StdoutLog)
    }
}
if ($StderrLog) {
    $start.RedirectStandardError = (Resolve-Path -LiteralPath $StderrLog -ErrorAction SilentlyContinue)?.Path
    if (-not $start.RedirectStandardError) {
        $start.RedirectStandardError = [IO.Path]::GetFullPath($StderrLog)
    }
}
if ($Environment.Count -gt 0) {
    # Make launch-time diagnostics/state selection explicit instead of relying
    # on the caller's shell environment surviving the extra PowerShell hop.
    $start.Environment = $Environment
}

$process = Start-Process @start
Write-Output ("candidate_pid={0}" -f $process.Id)
Write-Output ("candidate_executable={0}" -f $resolvedExecutable)
