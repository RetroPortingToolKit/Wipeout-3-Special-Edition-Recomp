[CmdletBinding()]
param(
    [string]$Executable = "build-direct-stock100/Wipeout_3_Special_Edition__Recompiled.exe",
    [string]$GameConfig = "game.toml",
    [string]$Disc = "disc/WipEout 3 - Special Edition (Europe) (En,Fr,De,Es,It).cue",
    [string]$Bios = "psxrecomp/bios/SCPH1001.BIN",
    [ValidateRange(12, 120)][int]$DurationSeconds = 30,
    [ValidateRange(0, 60)][int]$SkipSeconds = 14,
    [ValidateRange(100, 1000)][int]$SkipIntervalMs = 250,
    [ValidateRange(2, 10)][int]$CaptureCount = 6,
    [ValidateRange(250, 5000)][int]$CaptureIntervalMs = 1000,
    [ValidateRange(1, 90)][int]$CaptureStartSeconds = 16,
    [ValidateRange(0, 90)][int]$MenuProbeSeconds = 0,
    [ValidateRange(250, 10000)][int]$DiagnosticIntervalMs = 3000,
    [ValidateRange(0, 20)][int]$WarmupSamples = 2,
    [double]$ExpectedGuestHz = 100.0,
    [double]$ExpectedProgressRatio = 1.0,
    [ValidateRange(0.01, 0.5)][double]$ProgressTolerance = 0.1,
    [ValidateRange(1, 20)][int]$MinimumProgressWindows = 2,
    [ValidateRange(0.001, 0.05)][double]$RaceClockAggregateTolerance = 0.01,
    [ValidateRange(0, 2000)][int]$LiveCpuOverclock = 0,
    [ValidateRange(1, 99)][int]$ExpectedTrackCount = 14,
    [ValidateRange(-1, 11)][int]$LoadSlot = -1,
    [string]$SavestateSourceDir = "",
    [ValidateRange(0, 120)][int]$SaveAtSeconds = 0,
    [ValidateRange(0, 11)][int]$SaveSlot = 0,
    [ValidateRange(0, 120)][int]$PauseGameAtSeconds = 0,
    [ValidateSet("Auto", "PresentShot", "Window", "None")][string]$CaptureMode = "Auto",
    [int]$ClientWidth = 0,
    [int]$ClientHeight = 0,
    [string]$CaptureClientSizes = "",
    [switch]$AllowNoCdda,
    [switch]$ShippingSoak,
    [string]$OutputRoot = "artifacts/candidate-probe"
)

$ErrorActionPreference = "Stop"
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$clientWidthSpecified = $PSBoundParameters.ContainsKey("ClientWidth")
$clientHeightSpecified = $PSBoundParameters.ContainsKey("ClientHeight")
if ($clientWidthSpecified -ne $clientHeightSpecified) {
    throw "ClientWidth and ClientHeight must be specified together"
}
$resizeClient = $clientWidthSpecified -and $clientHeightSpecified
if ($resizeClient -and
    ($ClientWidth -lt 64 -or $ClientWidth -gt 16384 -or
     $ClientHeight -lt 64 -or $ClientHeight -gt 16384)) {
    throw "ClientWidth and ClientHeight must each be in the range 64..16384"
}
if ($SaveAtSeconds -ge $DurationSeconds) {
    throw "SaveAtSeconds must be less than DurationSeconds"
}
if ($PauseGameAtSeconds -gt 0 -and $PauseGameAtSeconds -ge $DurationSeconds) {
    throw "PauseGameAtSeconds must be less than DurationSeconds"
}
if ($PauseGameAtSeconds -gt $CaptureStartSeconds) {
    throw "PauseGameAtSeconds must be no later than CaptureStartSeconds"
}
$captureSizeSequence = @()
if (-not [string]::IsNullOrWhiteSpace($CaptureClientSizes)) {
    foreach ($sizeText in ($CaptureClientSizes -split ',')) {
        if ($sizeText.Trim() -notmatch '^(\d+)x(\d+)$') {
            throw "CaptureClientSizes entries must use WIDTHxHEIGHT"
        }
        $width = [int]$Matches[1]
        $height = [int]$Matches[2]
        if ($width -lt 64 -or $width -gt 16384 -or
            $height -lt 64 -or $height -gt 16384) {
            throw "CaptureClientSizes dimensions must each be in the range 64..16384"
        }
        $captureSizeSequence += [pscustomobject]@{ Width = $width; Height = $height }
    }
    if ($captureSizeSequence.Count -ne $CaptureCount) {
        throw "CaptureClientSizes must contain exactly CaptureCount entries"
    }
}

function Resolve-RepoPath([string]$Value) {
    if ([System.IO.Path]::IsPathRooted($Value)) {
        return [System.IO.Path]::GetFullPath($Value)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repo $Value))
}

function Get-FileProvenance([string]$Path, [string]$SelectionRule) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if (Test-Path -LiteralPath $absolute -PathType Leaf) {
        return [ordered]@{
            path = (Resolve-Path -LiteralPath $absolute).Path
            sha256 = (Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash.ToLowerInvariant()
            selection_rule = $SelectionRule
            missing = $false
        }
    }
    return [ordered]@{
        path = $absolute
        sha256 = $null
        selection_rule = $SelectionRule
        missing = $true
    }
}

function Get-UniqueLoopbackPort {
    # Ask Winsock for an unused dynamic port. The candidate receives this exact
    # value via --debug-port, and listener ownership is proved after launch.
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    try {
        $listener.Start()
        $port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port
        if ($port -lt 49152 -or $port -gt 65535) {
            throw "Winsock returned a debug port outside the dynamic-port bound: $port"
        }
        return $port
    } finally {
        $listener.Stop()
    }
}

function Require-DebugListenerOwner(
    [int]$Port, [int]$ExpectedPid, [int]$TimeoutMs = 10000,
    [switch]$AllowMissing) {
    $wait = [Diagnostics.Stopwatch]::StartNew()
    do {
        $listeners = @(Get-NetTCPConnection -LocalAddress "127.0.0.1" -LocalPort $Port `
            -State Listen -ErrorAction SilentlyContinue)
        if ($listeners.Count -gt 0) {
            if ($listeners.Count -ne 1 -or [int]$listeners[0].OwningProcess -ne $ExpectedPid) {
                $owners = ($listeners | ForEach-Object OwningProcess) -join ","
                throw "Debug listener 127.0.0.1:$Port is not uniquely owned by candidate PID $ExpectedPid (owners=$owners)"
            }
            return [ordered]@{
                address = "127.0.0.1"
                port = $Port
                owning_pid = [int]$listeners[0].OwningProcess
            }
        }
        Start-Sleep -Milliseconds 50
    } while ($wait.ElapsedMilliseconds -lt $TimeoutMs)
    if ($AllowMissing) { return $null }
    throw "Candidate PID $ExpectedPid did not open debug listener 127.0.0.1:$Port within $TimeoutMs ms"
}

function Invoke-DebugJsonCommand([int]$Port, [hashtable]$Command, [int]$TimeoutMs = 1500) {
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $connect = $client.ConnectAsync([Net.IPAddress]::Loopback, $Port)
        if (-not $connect.Wait($TimeoutMs)) {
            throw "Timed out connecting to debug server on 127.0.0.1:$Port"
        }
        $client.ReceiveTimeout = $TimeoutMs
        $client.SendTimeout = $TimeoutMs
        $stream = $client.GetStream()
        $utf8 = [Text.UTF8Encoding]::new($false)
        $writer = [IO.StreamWriter]::new($stream, $utf8, 4096, $true)
        $reader = [IO.StreamReader]::new($stream, $utf8, $false, 4096, $true)
        try {
            $writer.NewLine = "`n"
            $writer.WriteLine(($Command | ConvertTo-Json -Compress))
            $writer.Flush()
            $line = $reader.ReadLine()
            if ([string]::IsNullOrWhiteSpace($line)) {
                throw "Debug server returned an empty response"
            }
            $response = $line | ConvertFrom-Json
            if (-not $response.ok) {
                throw "Debug command $($Command.cmd) failed: $($response.error)"
            }
            return $response
        } finally {
            $reader.Dispose()
            $writer.Dispose()
        }
    } finally {
        $client.Dispose()
    }
}

function Capture-ComposedPresent(
    [int]$Port, [int]$ExpectedPid, [IntPtr]$Window,
    [string]$ExpectedTitlePrefix, [string]$Path,
    [Diagnostics.Stopwatch]$RunClock, [int]$TimeoutMs = 5000) {
    $before = [CandidateProbeWindow]::RequireOwnedClient(
        $Window, [uint32]$ExpectedPid, $ExpectedTitlePrefix)
    $listener = Require-DebugListenerOwner $Port $ExpectedPid
    if (Test-Path -LiteralPath $Path) {
        throw "Refusing to overwrite an existing composed capture: $Path"
    }
    $baseline = Invoke-DebugJsonCommand $Port @{ cmd = "present_shot_seq" }
    $requestedElapsedTicks = $RunClock.ElapsedTicks
    $requestedElapsedSeconds = [double]$requestedElapsedTicks / [Diagnostics.Stopwatch]::Frequency
    $requestedUtc = (Get-Date).ToUniversalTime().ToString("o")
    $staged = Invoke-DebugJsonCommand $Port @{ cmd = "present_shot"; path = $Path }
    $stagedElapsedTicks = $RunClock.ElapsedTicks
    $stagedElapsedSeconds = [double]$stagedElapsedTicks / [Diagnostics.Stopwatch]::Frequency
    $stagedUtc = (Get-Date).ToUniversalTime().ToString("o")
    if (-not $staged.staged -or [int]$staged.seq -ne [int]$baseline.seq) {
        throw "present_shot staging acknowledgment did not match baseline sequence"
    }
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    $completed = $null
    while ($deadline.ElapsedMilliseconds -lt $TimeoutMs) {
        Start-Sleep -Milliseconds 25
        $status = Invoke-DebugJsonCommand $Port @{ cmd = "present_shot_seq" }
        if ([int]$status.seq -gt [int]$baseline.seq) {
            $completed = $status
            break
        }
    }
    if ($null -eq $completed) {
        throw "present_shot did not complete within $TimeoutMs ms"
    }
    if ([int]$completed.seq -ne ([int]$baseline.seq + 1) -or [int]$completed.wrote -ne 1) {
        throw "present_shot completion was not the exact successful next sequence"
    }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "present_shot reported success but did not create $Path"
    }
    $completedElapsedTicks = $RunClock.ElapsedTicks
    $completedElapsedSeconds = [double]$completedElapsedTicks / [Diagnostics.Stopwatch]::Frequency
    $completedUtc = (Get-Date).ToUniversalTime().ToString("o")
    $after = [CandidateProbeWindow]::RequireOwnedClient(
        $Window, [uint32]$ExpectedPid, $ExpectedTitlePrefix)
    if ($after.ClientWidth -ne $before.ClientWidth -or $after.ClientHeight -ne $before.ClientHeight) {
        throw "candidate client resized during composed capture"
    }
    $inspection = [CandidateProbeWindow]::InspectComposedFrame($Path)
    return [ordered]@{
        inspection = $inspection
        listener = $listener
        baseline_seq = [int]$baseline.seq
        staged_seq = [int]$staged.seq
        completed_seq = [int]$completed.seq
        wrote = [int]$completed.wrote
        requested_elapsed_ticks = $requestedElapsedTicks
        requested_elapsed_seconds = $requestedElapsedSeconds
        requested_utc = $requestedUtc
        staged_elapsed_ticks = $stagedElapsedTicks
        staged_elapsed_seconds = $stagedElapsedSeconds
        staged_utc = $stagedUtc
        completed_elapsed_ticks = $completedElapsedTicks
        completed_elapsed_seconds = $completedElapsedSeconds
        completed_utc = $completedUtc
    }
}

$exePath = Resolve-RepoPath $Executable
$gamePath = Resolve-RepoPath $GameConfig
$discPath = Resolve-RepoPath $Disc
$biosPath = Resolve-RepoPath $Bios
$outputBase = Resolve-RepoPath $OutputRoot

foreach ($entry in @(
    @{ Label = "executable"; Path = $exePath },
    @{ Label = "game config"; Path = $gamePath },
    @{ Label = "disc"; Path = $discPath },
    @{ Label = "BIOS"; Path = $biosPath }
)) {
    if (-not (Test-Path -LiteralPath $entry.Path -PathType Leaf)) {
        throw "Missing $($entry.Label): $($entry.Path)"
    }
}

$exePath = (Resolve-Path -LiteralPath $exePath).Path
$gamePath = (Resolve-Path -LiteralPath $gamePath).Path
$discPath = (Resolve-Path -LiteralPath $discPath).Path
$biosPath = (Resolve-Path -LiteralPath $biosPath).Path

# Resolve every FILE entry from the cue before launch. This catches the common
# Track-01-only mistake immediately without spending each fast probe hashing an
# entire Redump image again.
$cueDirectory = Split-Path -Parent $discPath
$mediaFiles = [System.Collections.Generic.List[object]]::new()
foreach ($line in Get-Content -LiteralPath $discPath) {
    if ($line -match '^\s*FILE\s+"([^"]+)"\s+\S+') {
        $trackPath = [IO.Path]::GetFullPath((Join-Path $cueDirectory $Matches[1]))
        if (-not (Test-Path -LiteralPath $trackPath -PathType Leaf)) {
            throw "Cue references a missing track: $trackPath"
        }
        $track = Get-Item -LiteralPath $trackPath
        $mediaFiles.Add([ordered]@{ path = $track.FullName; bytes = $track.Length })
    }
}
if ($mediaFiles.Count -ne $ExpectedTrackCount) {
    throw "Cue has $($mediaFiles.Count) track file(s); expected $ExpectedTrackCount`: $discPath"
}

# Never attach to or terminate an ambiguous/stale candidate. The executable path,
# not its process name, defines identity because several builds share the name.
$samePath = @()
try {
    $samePath = @(Get-CimInstance Win32_Process -Filter "Name='$([IO.Path]::GetFileName($exePath).Replace("'", "''"))'" |
        Where-Object {
            $_.ExecutablePath -and
            [string]::Equals([IO.Path]::GetFullPath($_.ExecutablePath), $exePath,
                [StringComparison]::OrdinalIgnoreCase)
        })
} catch {
    throw "Could not prove single-instance state for $exePath`: $($_.Exception.Message)"
}
if ($samePath.Count -ne 0) {
    $ids = ($samePath | ForEach-Object ProcessId) -join ", "
    throw "Exact candidate is already running (PID $ids): $exePath"
}

if ($CaptureStartSeconds + (($CaptureCount - 1) * $CaptureIntervalMs / 1000.0) -gt $DurationSeconds) {
    throw "Capture schedule exceeds DurationSeconds"
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss-fff"
$runDir = Join-Path $outputBase $stamp
$stateDir = Join-Path $runDir "isolated-state"
$captureDir = Join-Path $runDir "captures"
New-Item -ItemType Directory -Path $stateDir, $captureDir -Force | Out-Null
$loadedStateSource = $null
if ($LoadSlot -ge 0) {
    if ([string]::IsNullOrWhiteSpace($SavestateSourceDir)) {
        throw "LoadSlot requires SavestateSourceDir"
    }
    $sourceStateDir = Resolve-RepoPath $SavestateSourceDir
    if (-not (Test-Path -LiteralPath $sourceStateDir -PathType Container)) {
        throw "Missing savestate source directory: $sourceStateDir"
    }
    $slotTag = "slot{0:D2}" -f $LoadSlot
    $sourceState = @(Get-ChildItem -LiteralPath $sourceStateDir -File |
        Where-Object { $_.Name -like "*_$slotTag.pst" })
    if ($sourceState.Count -ne 1) {
        throw "Expected one $slotTag savestate in $sourceStateDir; found $($sourceState.Count)"
    }
    $isolatedBiosDir = Join-Path $stateDir (Split-Path -Leaf $sourceStateDir)
    New-Item -ItemType Directory -Path $isolatedBiosDir -Force | Out-Null
    Copy-Item -LiteralPath $sourceState[0].FullName -Destination $isolatedBiosDir
    $sourceThumb = [IO.Path]::ChangeExtension($sourceState[0].FullName, ".thumb")
    if (Test-Path -LiteralPath $sourceThumb -PathType Leaf) {
        Copy-Item -LiteralPath $sourceThumb -Destination $isolatedBiosDir
    }
    $loadedStateSource = $sourceState[0].FullName
}
$stdoutPath = Join-Path $runDir "runtime.stdout.log"
$stderrPath = Join-Path $runDir "runtime.stderr.log"
$summaryPath = Join-Path $runDir "cadence.json"
$progressPath = Join-Path $runDir "progression.json"
$raceClockPath = Join-Path $runDir "race-clock.json"
$motionPath = Join-Path $runDir "motion.json"
$visualTimerPath = Join-Path $runDir "visible-race-timer.json"
$receiptPath = Join-Path $runDir "run.json"
$debugPort = Get-UniqueLoopbackPort
$preloadedStatePath = Resolve-RepoPath "mods/preloaded/state.toml"
$preloadedState = Get-FileProvenance $preloadedStatePath `
    "reviewed source state; staged into build output by packaging/build workflow"
$effectiveStatePath = Join-Path (Split-Path -Parent $exePath) "mods/state.toml"
$effectiveState = Get-FileProvenance $effectiveStatePath `
    "runtime initializes mods from <resolved executable directory>/mods"
$effectiveStateSnapshotPath = Join-Path $runDir "effective-mod-state.toml"
$effectiveStateSnapshot = if (-not $effectiveState.missing) {
    Copy-Item -LiteralPath $effectiveStatePath -Destination $effectiveStateSnapshotPath
    $snapshot = Get-FileProvenance $effectiveStateSnapshotPath `
        "immutable run-local copy of the runtime-selected mod state before candidate launch"
    if ($snapshot.sha256 -ne $effectiveState.sha256) {
        throw "Effective mod state changed while its run-local snapshot was created"
    }
    $snapshot
} else {
    Get-FileProvenance $effectiveStateSnapshotPath `
        "runtime-selected mod state was absent before candidate launch"
}

if ($CaptureMode -ne "None") {
    Add-Type -AssemblyName System.Drawing.Common
    $drawingAssembly = [System.Drawing.Bitmap].Assembly.Location
    $drawingDirectory = Split-Path -Parent $drawingAssembly
    $gdiPlusAssembly = Join-Path $drawingDirectory "System.Private.Windows.GdiPlus.dll"
    $windowsCoreAssembly = Join-Path $drawingDirectory "System.Private.Windows.Core.dll"
    $drawingPrimitivesAssembly = Join-Path $drawingDirectory "System.Drawing.Primitives.dll"
    $captureSupport = Join-Path $PSScriptRoot "CandidateProbeWindowCapture.cs"
    Add-Type -ReferencedAssemblies $drawingAssembly, $gdiPlusAssembly, $windowsCoreAssembly, $drawingPrimitivesAssembly `
        -Path $captureSupport
}

$windowTitlePrefix = "Wipeout candidate probe $stamp"
$arguments = @(
    "--game", $gamePath,
    "--disc", $discPath,
    "--bios", $biosPath,
    "--memcard-dir", $stateDir,
    "--window-title", $windowTitlePrefix,
    "--debug-port", "$debugPort",
    "--no-launcher"
)
$receipt = [ordered]@{
    schema = 4
    started_utc = (Get-Date).ToUniversalTime().ToString("o")
    executable = $exePath
    executable_sha256 = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash.ToLowerInvariant()
    source_working_directory = $repo
    game_config = $gamePath
    game_config_sha256 = (Get-FileHash -LiteralPath $gamePath -Algorithm SHA256).Hash.ToLowerInvariant()
    disc = $discPath
    disc_sha256 = (Get-FileHash -LiteralPath $discPath -Algorithm SHA256).Hash.ToLowerInvariant()
    cue_track_file_count = $mediaFiles.Count
    cue_track_files = $mediaFiles
    bios = $biosPath
    bios_sha256 = (Get-FileHash -LiteralPath $biosPath -Algorithm SHA256).Hash.ToLowerInvariant()
    isolated_state_directory = $stateDir
    loaded_state_slot = $LoadSlot
    loaded_state_source = $loadedStateSource
    arguments = $arguments
    preloaded_mod_state = $preloadedState
    effective_mod_state = $effectiveState
    effective_mod_state_snapshot = $effectiveStateSnapshot
    debug_server = [ordered]@{
        address = "127.0.0.1"
        port = $debugPort
        allocation = "unique Winsock dynamic port (49152..65535)"
        capture_timeout_ms = 5000
    }
    environment = [ordered]@{
        PSX_RUNTIME_PERF_DIAG = "1"
        PSX_RUNTIME_PERF_DIAG_MS = "$DiagnosticIntervalMs"
        PSX_WIP3_PROGRESS_DIAG = "1"
        PSX_WIP3_RACE_CLOCK_DIAG = "1"
    }
    schedule = [ordered]@{
        duration_seconds = $DurationSeconds
        x_skip_seconds = $SkipSeconds
        x_skip_interval_ms = $SkipIntervalMs
        capture_start_seconds = $CaptureStartSeconds
        capture_count = $CaptureCount
        capture_interval_ms = $CaptureIntervalMs
        menu_probe_seconds = $MenuProbeSeconds
        save_at_seconds = if ($SaveAtSeconds -gt 0) { $SaveAtSeconds } else { $null }
        save_slot = if ($SaveAtSeconds -gt 0) { $SaveSlot } else { $null }
        pause_game_at_seconds = if ($PauseGameAtSeconds -gt 0) { $PauseGameAtSeconds } else { $null }
        paused_capture_is_speed_evidence = $false
        live_cpu_overclock_percent = if ($LiveCpuOverclock -gt 0) { $LiveCpuOverclock } else { $null }
        requested_capture_mode = $CaptureMode
        requested_client_size = if ($resizeClient) {
            [ordered]@{ width = $ClientWidth; height = $ClientHeight }
        } else { $null }
        per_capture_client_sizes = if ($captureSizeSequence.Count -gt 0) {
            @($captureSizeSequence | ForEach-Object {
                [ordered]@{ width = $_.Width; height = $_.Height }
            })
        } else { $null }
    }
    race_clock_acceptance = [ordered]@{
        source = "stock in-race centisecond timer stores"
        min_aggregate_ratio = 1.0 - $RaceClockAggregateTolerance
        max_aggregate_ratio = 1.0 + $RaceClockAggregateTolerance
        note = "FPS and scheduler counters are not accepted as game-speed proof."
    }
}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptPath -Encoding utf8

$psi = [Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $exePath
$psi.WorkingDirectory = $repo
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.CreateNoWindow = $false
$psi.Environment["PSX_RUNTIME_PERF_DIAG"] = "1"
$psi.Environment["PSX_RUNTIME_PERF_DIAG_MS"] = "$DiagnosticIntervalMs"
$psi.Environment["PSX_WIP3_PROGRESS_DIAG"] = "1"
$psi.Environment["PSX_WIP3_RACE_CLOCK_DIAG"] = "1"
$psi.Environment["PSX_CANDIDATE_CPU_OVERCLOCK"] = if ($LiveCpuOverclock -gt 0) {
    "$LiveCpuOverclock"
} else { "" }
if ($LoadSlot -ge 0) { $psi.Environment["PSX_LOAD_SLOT"] = "$LoadSlot" }
if ($null -ne $psi.ArgumentList) {
    foreach ($argument in $arguments) { [void]$psi.ArgumentList.Add($argument) }
} else {
    $psi.Arguments = ($arguments | ForEach-Object {
        '"' + ($_ -replace '"', '\"') + '"'
    }) -join ' '
}

$process = [Diagnostics.Process]::new()
$process.StartInfo = $psi
$started = $process.Start()
if (-not $started) { throw "Failed to start exact candidate: $exePath" }
$receipt.pid = $process.Id
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptPath -Encoding utf8
$stdoutTask = $process.StandardOutput.ReadToEndAsync()
$stderrTask = $process.StandardError.ReadToEndAsync()
$startedAt = [Diagnostics.Stopwatch]::StartNew()

try {
    $window = [IntPtr]::Zero
    if ($CaptureMode -ne "None") {
        while ($startedAt.Elapsed.TotalSeconds -lt [Math]::Min(10, $DurationSeconds) -and -not $process.HasExited) {
            $process.Refresh()
            if ($process.MainWindowHandle -ne [IntPtr]::Zero) { $window = $process.MainWindowHandle; break }
            Start-Sleep -Milliseconds 100
        }
        if ($window -eq [IntPtr]::Zero) { throw "Candidate did not create a visible window within the bound" }
    }

    if ($CaptureMode -eq "None") {
        $initialWindowInfo = [pscustomobject]@{ Hwnd = 0; ProcessId = $process.Id; Title = $windowTitlePrefix; ClassName = ""; ClientLeft = 0; ClientTop = 0; ClientWidth = $ClientWidth; ClientHeight = $ClientHeight }
        $windowInfo = $initialWindowInfo
    } else {
        $initialWindowInfo = [CandidateProbeWindow]::RequireOwnedClient(
            $window, [uint32]$process.Id, $windowTitlePrefix)
        $windowInfo = if ($resizeClient) {
            [CandidateProbeWindow]::ResizeOwnedClient(
                $window, [uint32]$process.Id, $windowTitlePrefix,
                $ClientWidth, $ClientHeight, 3000)
        } else {
            $initialWindowInfo
        }
    }
    $debugListener = $null
    $effectiveCaptureMode = $CaptureMode
    if ($CaptureMode -eq "PresentShot") {
        $debugListener = Require-DebugListenerOwner $debugPort $process.Id
    } elseif ($CaptureMode -eq "Auto") {
        $debugListener = Require-DebugListenerOwner $debugPort $process.Id 750 -AllowMissing
        $effectiveCaptureMode = if ($null -ne $debugListener) { "PresentShot" } else { "Window" }
    }
    $captureMethod = switch ($effectiveCaptureMode) {
        "PresentShot" { "debug_server present_shot/present_shot_seq (composed post-aspect output)" }
        "Window" { "PrintWindow(PW_CLIENTONLY|PW_RENDERFULLCONTENT)" }
        "None" { "none (cadence-only probe)" }
    }
    $receipt.capture_target = [ordered]@{
        hwnd = ("0x{0:X}" -f $windowInfo.Hwnd)
        pid = $windowInfo.ProcessId
        title = $windowInfo.Title
        expected_title_prefix = $windowTitlePrefix
        class_name = $windowInfo.ClassName
        client_rect_screen = [ordered]@{
            left = $windowInfo.ClientLeft
            top = $windowInfo.ClientTop
            width = $windowInfo.ClientWidth
            height = $windowInfo.ClientHeight
        }
        client_resize = if ($resizeClient) {
            [ordered]@{
                requested_width = $ClientWidth
                requested_height = $ClientHeight
                initial_width = $initialWindowInfo.ClientWidth
                initial_height = $initialWindowInfo.ClientHeight
                settled_width = $windowInfo.ClientWidth
                settled_height = $windowInfo.ClientHeight
                exact = ($windowInfo.ClientWidth -eq $ClientWidth -and
                         $windowInfo.ClientHeight -eq $ClientHeight)
            }
        } else { $null }
        requested_mode = $CaptureMode
        effective_mode = $effectiveCaptureMode
        capture_method = $captureMethod
        desktop_pixels_used = $false
    }
    $receipt.debug_server.listener = $debugListener
    if ($LiveCpuOverclock -gt 0) {
        $receipt.debug_server.live_cpu_overclock = [ordered]@{
            requested_percent = $LiveCpuOverclock
            applied_percent = $LiveCpuOverclock
            mechanism = "PSX_CANDIDATE_CPU_OVERCLOCK before emulation starts"
        }
    }
    $receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptPath -Encoding utf8

    $nextSkipMs = 500
    $nextCaptureMs = $CaptureStartSeconds * 1000
    $menuProbeSent = $false
    $saveStateSent = $false
    $pauseGameSent = $false
    $pauseGameRequestedSeconds = $null
    $saveStateRequestedSeconds = $null
    $captures = [System.Collections.Generic.List[object]]::new()
    $captureFailure = $null
    while ($startedAt.Elapsed.TotalSeconds -lt $DurationSeconds -and -not $process.HasExited) {
        $elapsedMs = $startedAt.Elapsed.TotalMilliseconds
        if ($SkipSeconds -gt 0 -and $elapsedMs -le ($SkipSeconds * 1000) -and
            $elapsedMs -ge $nextSkipMs) {
            [CandidateProbeWindow]::PressX(
                $window, [uint32]$process.Id, $windowTitlePrefix, 35)
            $nextSkipMs += $SkipIntervalMs
        }
        if (-not $pauseGameSent -and $PauseGameAtSeconds -gt 0 -and
            $elapsedMs -ge ($PauseGameAtSeconds * 1000)) {
            $pauseGameRequestedSeconds = $startedAt.Elapsed.TotalSeconds
            [CandidateProbeWindow]::PressStart(
                $window, [uint32]$process.Id, $windowTitlePrefix, 45)
            $pauseGameSent = $true
            # Allow the pause menu/frame to settle before any capture scheduled
            # at the same monotonic second.
            Start-Sleep -Milliseconds 150
            $elapsedMs = $startedAt.Elapsed.TotalMilliseconds
        }
        if ($effectiveCaptureMode -ne "None" -and $captures.Count -lt $CaptureCount -and
            $elapsedMs -ge $nextCaptureMs) {
            $captureRequestedSize = $null
            if ($captureSizeSequence.Count -gt 0) {
                $captureRequestedSize = $captureSizeSequence[$captures.Count]
                $windowInfo = [CandidateProbeWindow]::ResizeOwnedClient(
                    $window, [uint32]$process.Id, $windowTitlePrefix,
                    $captureRequestedSize.Width, $captureRequestedSize.Height, 3000)
                # Let adaptive aspect and the title's projection consume several
                # live frames before binding the renderer-native screenshot.
                Start-Sleep -Milliseconds 250
            }
            $capturePath = Join-Path $captureDir ("frame-{0:D2}.png" -f ($captures.Count + 1))
            $captureIndex = $captures.Count + 1
            $scheduledElapsedSeconds = $nextCaptureMs / 1000.0
            if ($effectiveCaptureMode -eq "PresentShot") {
                $captureResult = Capture-ComposedPresent $debugPort $process.Id $window `
                    $windowTitlePrefix $capturePath $startedAt
                $capture = $captureResult.inspection
            } else {
                $requestedElapsedTicks = $startedAt.ElapsedTicks
                $requestedElapsedSeconds = [double]$requestedElapsedTicks / [Diagnostics.Stopwatch]::Frequency
                $requestedUtc = (Get-Date).ToUniversalTime().ToString("o")
                $capture = [CandidateProbeWindow]::CaptureClient(
                    $window, [uint32]$process.Id, $windowTitlePrefix, $capturePath)
                $completedElapsedTicks = $startedAt.ElapsedTicks
                $captureResult = [ordered]@{
                    listener = $null
                    baseline_seq = $null; staged_seq = $null
                    completed_seq = $null; wrote = $null
                    requested_elapsed_ticks = $requestedElapsedTicks
                requested_elapsed_seconds = $requestedElapsedSeconds
                requested_utc = $requestedUtc
                staged_elapsed_ticks = $null
                staged_elapsed_seconds = $null
                staged_utc = $null
                completed_elapsed_ticks = $completedElapsedTicks
                    completed_elapsed_seconds = [double]$completedElapsedTicks / [Diagnostics.Stopwatch]::Frequency
                    completed_utc = (Get-Date).ToUniversalTime().ToString("o")
                }
            }
            $captureSha256 = if (Test-Path -LiteralPath $capturePath -PathType Leaf) {
                (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash.ToLowerInvariant()
            } else { $null }
            $captures.Add([ordered]@{
                index = $captureIndex
                seconds = $captureResult.completed_elapsed_seconds
                scheduled_elapsed_seconds = $scheduledElapsedSeconds
                requested_elapsed_ticks = $captureResult.requested_elapsed_ticks
                requested_elapsed_seconds = $captureResult.requested_elapsed_seconds
                requested_utc = $captureResult.requested_utc
                staged_elapsed_ticks = $captureResult.staged_elapsed_ticks
                staged_elapsed_seconds = $captureResult.staged_elapsed_seconds
                staged_utc = $captureResult.staged_utc
                completed_elapsed_ticks = $captureResult.completed_elapsed_ticks
                completed_elapsed_seconds = $captureResult.completed_elapsed_seconds
                completed_utc = $captureResult.completed_utc
                capture_duration_seconds = $captureResult.completed_elapsed_seconds - $captureResult.requested_elapsed_seconds
                stopwatch_frequency_hz = [Diagnostics.Stopwatch]::Frequency
                path = $capturePath
                sha256 = $captureSha256
                hwnd = ("0x{0:X}" -f $windowInfo.Hwnd)
                pid = $windowInfo.ProcessId
                title = $windowInfo.Title
                client_size = "$($capture.Width)x$($capture.Height)"
                requested_client_size = if ($null -ne $captureRequestedSize) {
                    "$($captureRequestedSize.Width)x$($captureRequestedSize.Height)"
                } else { $null }
                method = $capture.Method
                print_window_returned = $capture.PrintWindowReturned
                debug_server_address = if ($captureResult.listener) { $captureResult.listener.address } else { $null }
                debug_server_port = if ($captureResult.listener) { $captureResult.listener.port } else { $null }
                debug_server_owning_pid = if ($captureResult.listener) { $captureResult.listener.owning_pid } else { $null }
                present_shot_baseline_seq = $captureResult.baseline_seq
                present_shot_staged_seq = $captureResult.staged_seq
                present_shot_completed_seq = $captureResult.completed_seq
                present_shot_wrote = $captureResult.wrote
                game_paused_for_visual_comparison = $pauseGameSent
                accepted = $capture.Accepted
                rejection_reason = $capture.RejectionReason
                mean_luma = [Math]::Round($capture.MeanLuma, 4)
                non_black_fraction = [Math]::Round($capture.NonBlackFraction, 6)
                sentinel_fraction = [Math]::Round($capture.SentinelFraction, 6)
                sampled_pixels = $capture.SampledPixels
            })
            if (-not $capture.Accepted) {
                $captureFailure = $capture.RejectionReason
            }
            $nextCaptureMs += $CaptureIntervalMs
        }
        if (-not $menuProbeSent -and $MenuProbeSeconds -gt 0 -and
            $elapsedMs -ge ($MenuProbeSeconds * 1000)) {
            [CandidateProbeWindow]::PressStartSelect(
                $window, [uint32]$process.Id, $windowTitlePrefix, 250)
            $menuProbeSent = $true
        }
        if (-not $saveStateSent -and $SaveAtSeconds -gt 0 -and
            $elapsedMs -ge ($SaveAtSeconds * 1000)) {
            $saveStateRequestedSeconds = $startedAt.Elapsed.TotalSeconds
            [CandidateProbeWindow]::SaveStateSlot(
                $window, [uint32]$process.Id, $windowTitlePrefix, $SaveSlot)
            $saveStateSent = $true
        }
        Start-Sleep -Milliseconds 25
    }
} finally {
    if (-not $process.HasExited) {
        try { $process.Kill($true) } catch [System.Management.Automation.MethodException] { $process.Kill() }
        [void]$process.WaitForExit(5000)
    }
    $stdoutTask.GetAwaiter().GetResult() | Set-Content -LiteralPath $stdoutPath -Encoding utf8
    $stderrTask.GetAwaiter().GetResult() | Set-Content -LiteralPath $stderrPath -Encoding utf8
}

$pairs = [System.Collections.Generic.List[object]]::new()
$acceptedCaptures = @($captures | Where-Object { $_.accepted })
for ($i = 1; $i -lt $acceptedCaptures.Count; $i++) {
    if ($acceptedCaptures[$i - 1].client_size -ne $acceptedCaptures[$i].client_size) {
        $pairs.Add([ordered]@{
            first = $acceptedCaptures[$i - 1].path
            second = $acceptedCaptures[$i].path
            comparable = $false
            skip_reason = "client sizes differ"
            first_client_size = $acceptedCaptures[$i - 1].client_size
            second_client_size = $acceptedCaptures[$i].client_size
        })
        continue
    }
    $raw = [CandidateProbeWindow]::Compare($acceptedCaptures[$i - 1].path, $acceptedCaptures[$i].path).Split("|")
    $pairs.Add([ordered]@{
        first = $acceptedCaptures[$i - 1].path
        second = $acceptedCaptures[$i].path
        comparable = $true
        mean_channel_delta = [double]::Parse($raw[0], [Globalization.CultureInfo]::InvariantCulture)
        changed_fraction = [double]::Parse($raw[1], [Globalization.CultureInfo]::InvariantCulture)
        first_mean_luma = [double]::Parse($raw[2], [Globalization.CultureInfo]::InvariantCulture)
        sampled_pixels = [int]$raw[3]
    })
}
$comparablePairs = @($pairs | Where-Object { $_.comparable })
$movingPairs = @($comparablePairs | Where-Object { $_.mean_channel_delta -ge 1.0 -and $_.changed_fraction -ge 0.02 })
$visiblePairs = @($comparablePairs | Where-Object { $_.first_mean_luma -ge 3.0 })
$motionVerdict = if ($effectiveCaptureMode -eq "None") { "NOT_REQUESTED" }
    elseif ($acceptedCaptures.Count -lt 2 -and $captureFailure) { "CAPTURE_REJECTED" }
    elseif ($acceptedCaptures.Count -lt 2) { "INSUFFICIENT_CAPTURES" }
    elseif ($comparablePairs.Count -eq 0) { "INCOMPARABLE_GEOMETRY" }
    elseif ($visiblePairs.Count -eq 0) { "BLACK_OR_HIDDEN" }
    elseif ($movingPairs.Count -ge [Math]::Ceiling($comparablePairs.Count / 2.0)) { "MOVING" }
    else { "STATIC" }
$motion = [ordered]@{
    verdict = $motionVerdict
    capture_failure = $captureFailure
    target = $receipt.capture_target
    note = if ($effectiveCaptureMode -eq "PresentShot") { "Frames are exact composed post-aspect output requested from the unique PID-owned debug server; HWND/PID/title provenance is retained. Motion does not prove correct gameplay speed." } elseif ($effectiveCaptureMode -eq "Window") { "Release fallback frames come from the PID-owned HWND via PrintWindow and are not final aspect-ratio evidence. Motion does not prove correct gameplay speed." } else { "Visual capture was explicitly disabled for this cadence-only probe." }
    captures = $captures
    pairs = $pairs
}
$motion | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $motionPath -Encoding utf8

$pythonArgs = @(
    (Join-Path $PSScriptRoot "summarize_runtime_cadence.py"),
    $stdoutPath,
    "--json", $summaryPath,
    "--warmup-samples", "$WarmupSamples",
    "--expected-guest-hz", "$ExpectedGuestHz"
)
if ($AllowNoCdda) { $pythonArgs += "--allow-no-cdda" }
& py -3 @pythonArgs
$cadenceExit = $LASTEXITCODE

if ($ShippingSoak) {
    $progressExit = 0
    $raceClockExit = 0
    $progressResult = [ordered]@{
        schema = 1
        verdict = "NOT_REQUESTED"
        reason = "shipping build excludes title-specific diagnostic callbacks"
    }
    $raceClockResult = [ordered]@{
        schema = 1
        verdict = "NOT_REQUESTED"
        aggregate_ratio = $null
        reason = "shipping build excludes title-specific diagnostic callbacks; visible HUD timer matrix is the speed authority"
    }
    $progressResult | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $progressPath -Encoding utf8
    $raceClockResult | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $raceClockPath -Encoding utf8
} else {
    $progressArgs = @(
        (Join-Path $PSScriptRoot "summarize_wip3_progress.py"),
        $stdoutPath,
        "--json", $progressPath,
        "--expected-ratio", "$ExpectedProgressRatio",
        "--tolerance", "$ProgressTolerance",
        "--minimum-windows", "$MinimumProgressWindows"
    )
    & py -3 @progressArgs
    $progressExit = $LASTEXITCODE

    $raceClockArgs = @(
        (Join-Path $PSScriptRoot "summarize_wip3_race_clock.py"),
        $stdoutPath,
        "--json",
        "--min-aggregate-ratio", "$(1.0 - $RaceClockAggregateTolerance)",
        "--max-aggregate-ratio", "$(1.0 + $RaceClockAggregateTolerance)"
    )
    $raceClockJson = & py -3 @raceClockArgs
    $raceClockExit = $LASTEXITCODE
    $raceClockJson | Set-Content -LiteralPath $raceClockPath -Encoding utf8
    $raceClockResult = ($raceClockJson -join "`n") | ConvertFrom-Json
}

# This manifest binds each capture to monotonic wall time. Final manual visual
# evidence requires three or more accepted PresentShot frames; a reviewer must
# transcribe the visible stock HUD timer and compare its delta with
# requested_elapsed_seconds. The automated gate above independently observes
# the same stock centisecond timer stores and enforces the 1% aggregate.
$visibleTimerFrames = [System.Collections.Generic.List[object]]::new()
$previousRequestedSeconds = $null
foreach ($captureRecord in $captures) {
    $deltaSeconds = if ($null -eq $previousRequestedSeconds) {
        $null
    } else {
        $captureRecord.requested_elapsed_seconds - $previousRequestedSeconds
    }
    $visibleTimerFrames.Add([ordered]@{
        index = $captureRecord.index
        path = $captureRecord.path
        sha256 = $captureRecord.sha256
        accepted = $captureRecord.accepted
        scheduled_elapsed_seconds = $captureRecord.scheduled_elapsed_seconds
        requested_elapsed_ticks = $captureRecord.requested_elapsed_ticks
        requested_elapsed_seconds = $captureRecord.requested_elapsed_seconds
        staged_elapsed_ticks = $captureRecord.staged_elapsed_ticks
        staged_elapsed_seconds = $captureRecord.staged_elapsed_seconds
        seconds_since_previous_frame = $deltaSeconds
        capture_duration_seconds = $captureRecord.capture_duration_seconds
        client_size = $captureRecord.client_size
    })
    $previousRequestedSeconds = $captureRecord.requested_elapsed_seconds
}
$visualReviewEligible = $effectiveCaptureMode -eq "PresentShot" -and
    $acceptedCaptures.Count -ge 3 -and -not $pauseGameSent
$visibleTimer = [ordered]@{
    schema = 1
    verdict = if ($visualReviewEligible) {
        "REQUIRES_MANUAL_HUD_REVIEW"
    } else {
        "INVALID_CAPTURE_EVIDENCE"
    }
    capture_mode = $effectiveCaptureMode
    accepted_frame_count = $acceptedCaptures.Count
    minimum_accepted_frames = 3
    automated_race_clock_verdict = $raceClockResult.verdict
    automated_aggregate_ratio = $raceClockResult.aggregate_ratio
    automated_aggregate_limits = if ($ShippingSoak) { $null } else { [ordered]@{
        minimum = 1.0 - $RaceClockAggregateTolerance
        maximum = 1.0 + $RaceClockAggregateTolerance
    } }
    wall_time_basis = [ordered]@{
        clock = "System.Diagnostics.Stopwatch"
        frequency_hz = [Diagnostics.Stopwatch]::Frequency
        zero = "candidate process launch"
        frame_time = "requested_elapsed_ticks/requested_elapsed_seconds immediately before present_shot"
        staging_ack = "staged_elapsed_ticks/staged_elapsed_seconds immediately after the renderer accepted the exact next-sequence request"
    }
    review = [ordered]@{
        required = $visualReviewEligible
        game_paused = $pauseGameSent
        speed_evidence_eligible = -not $pauseGameSent
        instruction = "Final proof requires at least three accepted PresentShot frames showing the post-start chase view and a legible stock in-race HUD timer. Compare displayed deltas with seconds_since_previous_frame (or requested_elapsed_seconds differences). PrintWindow fallback, flyby/start-pan/loading/demo frames, and the FPS overlay are not speed evidence."
        tolerance = "Displayed race-time delta must agree with monotonic wall-time delta within 1%, allowing only HUD centisecond quantization."
    }
    frames = $visibleTimerFrames
}
$visibleTimer | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $visualTimerPath -Encoding utf8

$receipt.completed_utc = (Get-Date).ToUniversalTime().ToString("o")
$receipt.effective_mod_state_after_run = Get-FileProvenance $effectiveStatePath `
    "same runtime-selected state path rehashed after candidate exit"
$receipt.effective_mod_state_unchanged = `
    $receipt.effective_mod_state.sha256 -eq $receipt.effective_mod_state_after_run.sha256
$receipt.process_exit_code = $process.ExitCode
$receipt.motion_verdict = $motionVerdict
$receipt.cadence_exit_code = $cadenceExit
$receipt.progression_exit_code = $progressExit
$receipt.race_clock_exit_code = $raceClockExit
$receipt.shipping_soak = [bool]$ShippingSoak
$receipt.visible_race_timer_review = $visibleTimer.verdict
$receipt.paused_visual_comparison = if ($PauseGameAtSeconds -gt 0) {
    [ordered]@{
        requested_elapsed_seconds = $pauseGameRequestedSeconds
        input = "single Start press"
        speed_evidence_eligible = $false
    }
} else { $null }
$savedState = $null
if ($SaveAtSeconds -gt 0) {
    $slotTag = "slot{0:D2}" -f $SaveSlot
    $savedFiles = @(Get-ChildItem -LiteralPath $stateDir -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "*_$slotTag.pst" })
    if ($savedFiles.Count -ne 1) {
        throw "Expected one newly captured $slotTag savestate; found $($savedFiles.Count)"
    }
    $savedState = Get-FileProvenance $savedFiles[0].FullName `
        "host F7 savestate menu captured at the requested monotonic time"
    $savedState.requested_elapsed_seconds = $saveStateRequestedSeconds
    $savedState.slot = $SaveSlot
}
$receipt.saved_state = $savedState
$receipt.artifacts = [ordered]@{
    stdout = $stdoutPath; stderr = $stderrPath; cadence = $summaryPath
    progression = $progressPath; race_clock = $raceClockPath; motion = $motionPath
    visible_race_timer = $visualTimerPath
}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptPath -Encoding utf8

Write-Host "motion $motionVerdict`: $($captures.Count) captures, $($movingPairs.Count)/$($comparablePairs.Count) comparable moving pairs"
Write-Host "receipt: $receiptPath"
if ($PauseGameAtSeconds -gt 0) {
    if ($cadenceExit -ne 0 -or $captureFailure) { exit 1 }
} elseif ($cadenceExit -ne 0 -or $progressExit -ne 0 -or $raceClockExit -ne 0 -or
    ($effectiveCaptureMode -ne "None" -and $motionVerdict -ne "MOVING")) { exit 1 }
exit 0
