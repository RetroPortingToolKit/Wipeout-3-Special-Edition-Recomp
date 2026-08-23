import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from verify_pal_pacing_diagnostic import validate


def receipt(**overrides):
    value = {
        "elapsed_s": 10.0,
        "target_hz": 100,
        "pal_crtc_multiplier": 2,
        "native_pal_x2": True,
        "wall_pacer": True,
        "swap_interval": 0,
        "vblank": 1000,
        "gameplay_updates": 1000,
        "backend_swaps": 1000,
        "audio_frames": 441000,
        "audio_underruns": 0,
        "max_work_ms": 8.0,
        "max_debt_periods": 2,
    }
    value.update(overrides)
    return value


def test_pass_separates_100_hz_gameplay_and_44_1khz_audio():
    errors, diagnosis = validate(receipt())
    assert errors == []
    assert diagnosis == []


def test_classifies_52_to_79_hz_as_over_budget_work():
    errors, diagnosis = validate(
        receipt(gameplay_updates=769, backend_swaps=769, audio_frames=338_000,
                max_work_ms=13.0, audio_underruns=4)
    )
    assert errors == []
    assert any("52-79 Hz" in item for item in diagnosis)
    assert any("audio underruns" in item for item in diagnosis)


def test_rejects_double_blocking_and_unbounded_debt():
    errors, _ = validate(receipt(swap_interval=1, max_debt_periods=13))
    assert any("double-blocking" in item for item in errors)
    assert any("bounded 12-period" in item for item in errors)


def test_fmv_requires_50_hz_backend_swaps_but_100_hz_guest():
    errors, diagnosis = validate(receipt(fmv=True, backend_swaps=500))
    assert errors == []
    assert diagnosis == []
