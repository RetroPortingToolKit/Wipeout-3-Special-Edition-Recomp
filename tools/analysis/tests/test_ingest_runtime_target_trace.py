import copy
import hashlib
import json
import struct
import unittest

from tools.analysis.ingest_runtime_target_trace import TraceError, build_closure


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fixture() -> tuple[dict, dict[str, bytes]]:
    main = bytearray(16)
    callback = bytearray(16)
    struct.pack_into("<I", main, 0, (4 << 21) | (31 << 11) | 9)  # jalr ra,a0
    struct.pack_into("<I", callback, 4, (5 << 21) | 8)  # jr a1
    images = {"callback-epoch": bytes(callback), "main": bytes(main)}
    rows = []
    # Deliberately overlapping guest ranges prove that epoch identity, not the
    # virtual address alone, binds each side of a cross-image callback.
    for name, base in (("callback-epoch", 0x1000), ("main", 0x1000)):
        rows.append({"id": name, "image_kind": "raw-payload",
                     "load_start": base, "load_end_exclusive": base + 16,
                     "sha256": digest(images[name]),
                     "variant_sha256": digest((name + "-variant").encode())})
    receipt = {
        "capture": {"complete": True, "dropped_events": 0, "event_count": 3,
                    "first_sequence": 40, "last_sequence": 42},
        "images": rows,
        "schema": "clean-aot-runtime-target-trace-v1",
        "sites": [
            {"complete": True, "observations": [
                {"count": 1, "destination_image": "main",
                 "destination_pc": 0x1008}],
             "source_image": "callback-epoch", "source_pc": 0x1004,
             "transfer": "indirect-jump"},
            {"complete": True, "observations": [
                {"count": 2, "destination_image": "callback-epoch",
                 "destination_pc": 0x1000}],
             "source_image": "main", "source_pc": 0x1000,
             "transfer": "indirect-call"},
        ],
    }
    return receipt, images


def encoded(receipt: dict) -> bytes:
    return json.dumps(receipt, sort_keys=True).encode()


class RuntimeTargetTraceTests(unittest.TestCase):
    def test_extended_runtime_telemetry_is_consumed_and_tamper_rejected(self) -> None:
        receipt, images = fixture()
        identity = receipt["images"][1]["variant_sha256"]
        receipt["telemetry"] = {
            "complete": True, "dropped_events": 0, "event_count": 3,
            "first_sequence": 80, "last_sequence": 82,
            "events": [
                {"accepted": True, "active_epoch_identity_sha256": identity,
                 "destination_image": "main", "destination_pc": 0x1008,
                 "kind": "target-attempt", "sequence": 80,
                 "source_image": "callback-epoch", "source_pc": 0x1004,
                 "status": "executed", "transfer": "indirect-jump"},
                {"accepted": True, "active_epoch_identity_sha256": identity,
                 "destination_image": "callback-epoch", "destination_pc": 0x1000,
                 "kind": "target-attempt", "sequence": 81,
                 "source_image": "main", "source_pc": 0x1000,
                 "status": "executed", "transfer": "indirect-call"},
                {"accepted": True, "active_epoch_identity_sha256": identity,
                 "destination_image": "callback-epoch", "destination_pc": 0x1000,
                 "kind": "target-attempt", "sequence": 82,
                 "source_image": "main", "source_pc": 0x1000,
                 "status": "executed", "transfer": "indirect-call"},
            ],
        }
        build_closure(encoded(receipt), images)
        receipt["telemetry"]["events"][2]["sequence"] = 83
        with self.assertRaisesRegex(TraceError, "sequence"):
            build_closure(encoded(receipt), images)

    def test_cross_image_closure_is_identity_bound_and_deterministic(self) -> None:
        receipt, images = fixture()
        first = build_closure(encoded(receipt), images)
        second = build_closure(json.dumps(receipt, indent=4).encode(), images)
        self.assertEqual(first, second)
        closure, manifests = first
        self.assertEqual([region["image"] for region in closure["regions"]],
                         ["callback-epoch", "main"])
        self.assertIn("entry=0x00001000\n", manifests["callback-epoch"])
        self.assertIn("entry=0x00001008\n", manifests["main"])
        self.assertNotIn("entry=0x00001000\n", manifests["main"])

    def test_identity_opcode_and_accounting_fail_closed(self) -> None:
        receipt, images = fixture()
        attacks = []
        wrong_bytes = dict(images)
        wrong_bytes["main"] = bytes(16)
        attacks.append((receipt, wrong_bytes, "identity"))
        wrong_opcode = copy.deepcopy(receipt)
        wrong_opcode["sites"][1]["transfer"] = "indirect-jump"
        attacks.append((wrong_opcode, images, "opcode"))
        wrong_count = copy.deepcopy(receipt)
        wrong_count["capture"]["event_count"] = 4
        wrong_count["capture"]["last_sequence"] = 43
        attacks.append((wrong_count, images, "counts"))
        gap = copy.deepcopy(receipt)
        gap["capture"]["last_sequence"] = 44
        attacks.append((gap, images, "gap"))
        for candidate, supplied, label in attacks:
            with self.subTest(label=label), self.assertRaises(TraceError):
                build_closure(encoded(candidate), supplied)

    def test_incomplete_ambiguous_and_unknown_evidence_fail_closed(self) -> None:
        receipt, images = fixture()
        attacks = []
        incomplete = copy.deepcopy(receipt)
        incomplete["capture"]["complete"] = False
        attacks.append(incomplete)
        dropped = copy.deepcopy(receipt)
        dropped["capture"]["dropped_events"] = 1
        attacks.append(dropped)
        duplicate = copy.deepcopy(receipt)
        duplicate["sites"][0]["observations"].append(
            copy.deepcopy(duplicate["sites"][0]["observations"][0]))
        attacks.append(duplicate)
        unknown = copy.deepcopy(receipt)
        unknown["sites"][0]["observations"][0]["destination_image"] = "missing"
        attacks.append(unknown)
        unsorted = copy.deepcopy(receipt)
        unsorted["sites"].reverse()
        attacks.append(unsorted)
        for candidate in attacks:
            with self.assertRaises(TraceError):
                build_closure(encoded(candidate), images)

    def test_duplicate_json_keys_are_rejected(self) -> None:
        receipt, images = fixture()
        payload = encoded(receipt).replace(
            b'"schema": "clean-aot-runtime-target-trace-v1"',
            b'"schema": "clean-aot-runtime-target-trace-v1", "schema": "evil"')
        with self.assertRaisesRegex(TraceError, "duplicate JSON key"):
            build_closure(payload, images)

    def test_returns_are_never_promoted_to_finite_callbacks(self) -> None:
        receipt, images = fixture()
        changed = dict(images)
        callback = bytearray(changed["callback-epoch"])
        struct.pack_into("<I", callback, 4, (31 << 21) | 8)  # jr ra
        changed["callback-epoch"] = bytes(callback)
        receipt["images"][0]["sha256"] = digest(changed["callback-epoch"])
        with self.assertRaisesRegex(TraceError, "return sites"):
            build_closure(encoded(receipt), changed)


if __name__ == "__main__":
    unittest.main()
