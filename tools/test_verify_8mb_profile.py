import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("verify", ROOT / "tools/verify_8mb_profile.py")
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


def game_config(overclock=900):
    return {"video": {"cpu_overclock": overclock}}


def state(enabled=True):
    return {"feature": [{"package_id": "psx.enhancement.8mb-ram", "enabled": enabled}]}


def test_valid_explicit_profile():
    assert verify.validate(game_config(), state()) == []


def test_missing_profile_fails_closed():
    errors = verify.validate(game_config(), {})
    assert any("silent 2 MB" in error for error in errors)


def test_disabled_ram_and_wrong_pairing_fail_closed():
    errors = verify.validate(game_config(100), state(False))
    assert len(errors) == 2
