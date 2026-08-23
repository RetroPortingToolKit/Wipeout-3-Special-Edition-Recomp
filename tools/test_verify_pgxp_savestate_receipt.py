import importlib.util
from copy import deepcopy
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("verify", ROOT / "tools/verify_pgxp_savestate_receipt.py")
verify = importlib.util.module_from_spec(spec); spec.loader.exec_module(verify)


def receipt():
    cases = []
    for aspect in ("4:3", "16:9", "21:9"):
        cases.append({"name": aspect, "aspect": aspect, "pass": True, "save": {"generation": 10}, "load": {"generation": 11, "last_op": "load", "last_ok": True}, "continuation": {"pass": True, "frames": 120, "expected_frames": 120, **{key: {"before": f"{key}-{aspect}", "after": f"{key}-{aspect}"} for key in ("geometry", "depth", "texture", "culling")}}})
    return {"configured_pgxp": {"geometry": True, "texture": True, "depth": True}, "effective_pgxp": {"geometry": True, "texture": True, "depth": True}, "reset": {"pass": True, "cleared_previous_state": True}, "cases": cases}


def test_valid_receipt(): assert verify.validate(receipt()) == []


def test_rejects_effective_fallback_and_missing_aspect():
    candidate = receipt(); candidate["effective_pgxp"]["depth"] = False; candidate["cases"].pop(); errors = verify.validate(candidate)
    assert any("effective_pgxp.depth" in error for error in errors); assert any("missing aspects" in error for error in errors)


def test_rejects_uncommitted_restore_and_continuation_mismatch():
    candidate = deepcopy(receipt()); candidate["cases"][0]["load"]["generation"] = 12; candidate["cases"][1]["continuation"]["texture"]["after"] = "different"; errors = verify.validate(candidate)
    assert any("generation" in error for error in errors); assert any("texture" in error for error in errors)
