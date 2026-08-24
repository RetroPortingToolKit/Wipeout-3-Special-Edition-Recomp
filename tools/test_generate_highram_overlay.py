import argparse
import base64
import hashlib
import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "generate_highram", ROOT / "tools/generate_highram_overlay.py")
generate_highram = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generate_highram)


def capture(path, *, target=True):
    body = b"\0\0\0\0"
    row = {
        "schema": generate_highram.CAPTURE_SCHEMA,
        "load_addr": f"0x{generate_highram.REGION:08X}",
        "size": len(body),
        "bytes_b64": base64.b64encode(body).decode(),
        "executed_pcs": [f"0x{generate_highram.TARGET:08X}"] if target else [],
        "dispatch_entry_pcs": [f"0x{generate_highram.TARGET:08X}"] if target else [],
        "function_entry_pcs": [],
        "seeds": [],
    }
    path.write_text(json.dumps([row]), encoding="utf-8")


def test_explicit_capture_validates_pin_structure_and_target(monkeypatch, tmp_path):
    path = tmp_path / "capture.json"
    capture(path)
    monkeypatch.setattr(generate_highram, "PINNED_SHA256",
                        hashlib.sha256(path.read_bytes()).hexdigest())
    monkeypatch.setattr(generate_highram, "PINNED_ROWS", 1)
    monkeypatch.setattr(generate_highram, "PINNED_TARGET_EXECUTED", 1)
    monkeypatch.setattr(generate_highram, "PINNED_TARGET_DISPATCH", 1)
    stats = generate_highram.validate_capture(path)
    assert stats == {"sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                     "rows": 1, "executed": 1, "dispatch": 1}
    capture(path, target=False)
    monkeypatch.setattr(generate_highram, "PINNED_SHA256",
                        hashlib.sha256(path.read_bytes()).hexdigest())
    try:
        generate_highram.validate_capture(path)
    except generate_highram.GenerationError as exc:
        assert "lacks target proof" in str(exc)
    else:
        raise AssertionError("capture without target proof was accepted")


def test_command_pins_bounded_static_cps_prefix(tmp_path):
    cmd = generate_highram.command(
        tmp_path / "capture.json", tmp_path / "recompiler.exe", tmp_path / "out")
    assert cmd[-6:] == ["--only-region", "0x80780000", "--static", "--cps",
                        "--static-symbol-prefix", "highram"]


def test_atomic_publish_and_check_mode(monkeypatch, tmp_path):
    cap = tmp_path / "capture.json"
    capture(cap)
    monkeypatch.setattr(generate_highram, "PINNED_SHA256",
                        hashlib.sha256(cap.read_bytes()).hexdigest())
    monkeypatch.setattr(generate_highram, "PINNED_ROWS", 1)
    monkeypatch.setattr(generate_highram, "PINNED_TARGET_EXECUTED", 1)
    monkeypatch.setattr(generate_highram, "PINNED_TARGET_DISPATCH", 1)
    recomp = tmp_path / "psxrecomp-game.exe"
    recomp.write_bytes(b"exe")
    output = tmp_path / "generated" / "overlays_highram.c"
    generated = (
        "void ov_highram_80780000_abcd_func_807814D0(void);\n"
        "void psx_overlay_dispatch_highram(CPUState *cpu) {\n"
        "case 0x807814D0: break;\n}\n"
    ).encode()

    def run(cmd, **kwargs):
        assert kwargs["stderr"] is generate_highram.subprocess.STDOUT
        assert kwargs["stdout"].name.endswith("compile_overlays.log")
        out = Path(cmd[cmd.index("--out-dir") + 1])
        out.mkdir(parents=True, exist_ok=True)
        (out / "overlays_static.c").write_bytes(generated)
        return argparse.Namespace(returncode=0)

    monkeypatch.setattr(generate_highram.subprocess, "run", run)
    base = argparse.Namespace(capture=cap, capture_explicit=True, output=output,
                              recompiler=recomp, check=False, dry_run=False)
    assert generate_highram.generate(base) == 0
    assert output.read_bytes() == generated
    base.check = True
    assert generate_highram.generate(base) == 0
    output.write_bytes(b"stale")
    try:
        generate_highram.generate(base)
    except generate_highram.GenerationError as exc:
        assert "stale or missing" in str(exc)
    else:
        raise AssertionError("check accepted stale output")
