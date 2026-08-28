import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
HARNESS = (ROOT / "tools" / "run_candidate_probe.ps1").read_text(encoding="utf-8")
SUPPORT = (ROOT / "tools" / "CandidateProbeWindowCapture.cs").read_text(encoding="utf-8")


class CandidateCaptureContractTests(unittest.TestCase):
    def test_never_reads_desktop_pixels(self):
        self.assertNotIn("CopyFromScreen", HARNESS)
        self.assertNotIn("CopyFromScreen", SUPPORT)
        self.assertIn("desktop_pixels_used = $false", HARNESS)

    def test_binds_each_capture_to_exact_process_window(self):
        self.assertIn("GetWindowThreadProcessId", SUPPORT)
        self.assertIn("info.ProcessId != expectedPid", SUPPORT)
        self.assertIn("StartsWith(expectedTitlePrefix", SUPPORT)
        self.assertIn("RequireOwnedClient(h, expectedPid, expectedTitlePrefix)", SUPPORT)

    def test_uses_exact_composed_post_aspect_output(self):
        self.assertIn('@{ cmd = "present_shot"; path = $Path }', HARNESS)
        self.assertIn('@{ cmd = "present_shot_seq" }', HARNESS)
        self.assertIn("composed post-aspect output", HARNESS)
        self.assertIn("InspectComposedFrame", SUPPORT)

    def test_capture_modes_are_explicit_and_release_safe(self):
        self.assertIn('ValidateSet("Auto", "PresentShot", "Window", "None")', HARNESS)
        self.assertIn('$effectiveCaptureMode = if ($null -ne $debugListener)', HARNESS)
        self.assertIn("CaptureClient", HARNESS)
        self.assertIn("PrintWindow", SUPPORT)
        self.assertIn('"None" { "none (cadence-only probe)" }', HARNESS)

    def test_fails_closed_on_unrendered_or_empty_frame(self):
        self.assertIn("MeanLuma < 1.0", SUPPORT)
        self.assertIn("PrintWindow returned false", SUPPORT)
        self.assertIn("present_shot reported success but did not create", HARNESS)
        self.assertIn("present_shot did not complete within", HARNESS)
        self.assertIn("completed.wrote", HARNESS)
        self.assertIn('"CAPTURE_REJECTED"', HARNESS)

    def test_receipt_records_capture_identity_and_geometry(self):
        for token in (
            "capture_target", "hwnd", "pid", "title", "class_name",
            "client_rect_screen", "debug_server_owning_pid", "sentinel_fraction",
        ):
            self.assertIn(token, HARNESS)

    def test_optional_client_resize_is_paired_exact_and_pre_capture(self):
        self.assertIn('ContainsKey("ClientWidth")', HARNESS)
        self.assertIn('ContainsKey("ClientHeight")', HARNESS)
        self.assertIn("must be specified together", HARNESS)
        self.assertIn("ResizeOwnedClient", HARNESS)
        self.assertIn("SetWindowPos", SUPPORT)
        self.assertIn("SetThreadDpiAwarenessContext", SUPPORT)
        self.assertIn("DpiAwarenessPerMonitorV2", SUPPORT)
        self.assertIn("stableExactSamples >= 3", SUPPORT)
        self.assertIn("requested_client_size", HARNESS)
        self.assertIn("client_resize", HARNESS)
        self.assertLess(HARNESS.index("ResizeOwnedClient"),
                        HARNESS.index("Capture-ComposedPresent $debugPort"))

    def test_debug_port_is_unique_bounded_and_pid_owned(self):
        for token in (
            "Get-UniqueLoopbackPort", "49152", "65535", '"--debug-port"',
            "Get-NetTCPConnection", "OwningProcess", "Require-DebugListenerOwner",
        ):
            self.assertIn(token, HARNESS)

    def test_records_source_and_runtime_effective_mod_state_hashes(self):
        for token in (
            "preloaded_mod_state", "effective_mod_state", "Get-FileProvenance",
            'Join-Path (Split-Path -Parent $exePath) "mods/state.toml"',
            "runtime initializes mods from <resolved executable directory>/mods",
            'Join-Path $runDir "effective-mod-state.toml"',
            "effective_mod_state_snapshot",
            "immutable run-local copy of the runtime-selected mod state",
        ):
            self.assertIn(token, HARNESS)

    def test_capture_completion_is_exact_and_bounded(self):
        self.assertIn("baseline.seq + 1", HARNESS)
        self.assertIn("TimeoutMs = 5000", HARNESS)
        for token in (
            "present_shot_baseline_seq", "present_shot_staged_seq",
            "present_shot_completed_seq", "present_shot_wrote",
        ):
            self.assertIn(token, HARNESS)

    def test_each_frame_is_bound_to_exact_monotonic_wall_time(self):
        for token in (
            "requested_elapsed_ticks", "requested_elapsed_seconds",
            "staged_elapsed_ticks", "staged_elapsed_seconds",
            "completed_elapsed_ticks", "capture_duration_seconds",
            "stopwatch_frequency_hz", "scheduled_elapsed_seconds",
            "seconds_since_previous_frame", "sha256",
        ):
            self.assertIn(token, HARNESS)
        self.assertIn("immediately before present_shot", HARNESS)
        self.assertIn("immediately after the renderer accepted", HARNESS)
        self.assertIn(
            "$requestedElapsedSeconds = [double]$requestedElapsedTicks / [Diagnostics.Stopwatch]::Frequency",
            HARNESS,
        )
        self.assertIn(
            "$completedElapsedSeconds = [double]$completedElapsedTicks / [Diagnostics.Stopwatch]::Frequency",
            HARNESS,
        )

    def test_visible_hud_review_explicitly_rejects_fps_as_speed_proof(self):
        self.assertIn('visible-race-timer.json', HARNESS)
        self.assertIn('"REQUIRES_MANUAL_HUD_REVIEW"', HARNESS)
        self.assertIn('"INVALID_CAPTURE_EVIDENCE"', HARNESS)
        self.assertIn('$effectiveCaptureMode -eq "PresentShot"', HARNESS)
        self.assertIn("minimum_accepted_frames = 3", HARNESS)
        self.assertIn("PrintWindow fallback", HARNESS)
        self.assertIn("stock in-race HUD timer", HARNESS)
        self.assertIn("the FPS overlay are not speed evidence", HARNESS)

    def test_probe_passes_one_percent_aggregate_limit_to_timer_gate(self):
        self.assertIn('RaceClockAggregateTolerance = 0.01', HARNESS)
        self.assertIn('"--min-aggregate-ratio"', HARNESS)
        self.assertIn('"--max-aggregate-ratio"', HARNESS)

    def test_mixed_geometry_motion_comparison_is_skipped_safely(self):
        self.assertIn('skip_reason = "client sizes differ"', HARNESS)
        self.assertIn('$comparablePairs = @(', HARNESS)
        self.assertIn('"INCOMPARABLE_GEOMETRY"', HARNESS)

    def test_optional_single_start_pause_is_visual_only(self):
        self.assertIn('PauseGameAtSeconds = 0', HARNESS)
        self.assertIn('PauseGameAtSeconds must be no later than CaptureStartSeconds', HARNESS)
        self.assertIn('[CandidateProbeWindow]::PressStart(', HARNESS)
        self.assertNotIn('[CandidateProbeWindow]::PressStartSelect(\n                $window, [uint32]$process.Id, $windowTitlePrefix, 45)', HARNESS)
        self.assertIn('public static void PressStart(', SUPPORT)
        self.assertIn('PressVirtualKey(h, 0x0D, holdMs)', SUPPORT)
        self.assertIn('paused_capture_is_speed_evidence = $false', HARNESS)
        self.assertIn('game_paused_for_visual_comparison', HARNESS)
        self.assertIn('speed_evidence_eligible = $false', HARNESS)


if __name__ == "__main__":
    unittest.main()
