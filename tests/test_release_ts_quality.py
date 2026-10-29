import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "docs/release-validation/ts-quality-check.py"
RECEIVERS = {
    "asicen-w3u3-rx0-3": ("0", "1", "2", "3"),
    "px4-rx0-5": ("0", "1", "2", "3", "4", "5"),
    "px4-rx6": ("6",),
    "px4-rx7": ("7",),
    "pix-dt181": ("0",),
    "pix-dt295": ("0",),
    "siano-s1ud": ("0",),
}


class ReleaseTsQualityTests(unittest.TestCase):
    def check(self, profile, scans, environment="representative-linux"):
        with tempfile.TemporaryDirectory() as temporary:
            paths = []
            for index, scan in enumerate(scans):
                scan = dict(scan)
                scan["file"] = f"fixture-{index}.ts"
                scan_path = Path(temporary) / f"scan-{index}.json"
                scan_path.write_text(json.dumps(scan), encoding="utf-8")
                receiver = RECEIVERS[profile][index] if index < len(RECEIVERS[profile]) else str(index)
                paths.append(f"{receiver}={scan_path}")
            result = subprocess.run(
                [sys.executable, str(SCRIPT), "--profile", profile, "--environment", environment, *paths],
                capture_output=True,
                text=True,
                check=False,
            )
            return result.returncode, json.loads(result.stdout)

    def scan(self, packets, tei=0, cc=0):
        return {"file": "fixture.ts", "packets": packets, "tei": tei, "continuity_errors": cc}

    def test_zero_threshold_profiles_require_both_counters_to_be_zero(self):
        for profile in ("asicen-w3u3-rx0-3", "px4-rx0-5", "px4-rx6"):
            with self.subTest(profile=profile):
                count = len(RECEIVERS[profile])
                self.assertEqual(self.check(profile, [self.scan(100) for _ in range(count)])[0], 0)
                self.assertEqual(self.check(profile, [self.scan(100, tei=1) if index == 0 else self.scan(100) for index in range(count)])[0], 1)
                self.assertEqual(self.check(profile, [self.scan(100, cc=1) if index == 0 else self.scan(100) for index in range(count)])[0], 1)

    def test_pix_strict_cc_boundary(self):
        self.assertEqual(self.check("pix-dt181", [self.scan(100)])[0], 0)
        self.assertEqual(self.check("pix-dt181", [self.scan(100, tei=1)])[0], 1)
        self.assertEqual(self.check("pix-dt181", [self.scan(100, cc=11)])[0], 0)
        self.assertEqual(self.check("pix-dt295", [self.scan(100, cc=12)])[0], 1)

    def test_siano_strict_cc_and_tei_rate_boundaries(self):
        self.assertEqual(self.check("siano-s1ud", [self.scan(100000, cc=9)])[0], 0)
        self.assertEqual(self.check("siano-s1ud", [self.scan(100000, cc=10)])[0], 1)
        self.assertEqual(self.check("siano-s1ud", [self.scan(100000, tei=1)])[0], 1)
        self.assertEqual(self.check("siano-s1ud", [self.scan(100001, tei=1)])[0], 0)

    def test_px4_rx7_strict_cc_and_tei_rate_boundaries(self):
        self.assertEqual(self.check("px4-rx7", [self.scan(201, tei=1, cc=39)])[0], 0)
        self.assertEqual(self.check("px4-rx7", [self.scan(200, tei=1, cc=39)])[0], 1)
        self.assertEqual(self.check("px4-rx7", [self.scan(1000, cc=40)])[0], 1)

    def test_multiple_ts_fail_if_any_receiver_fails(self):
        code, result = self.check("asicen-w3u3-rx0-3", [self.scan(100), self.scan(100), self.scan(100), self.scan(100, cc=1)])
        self.assertEqual(code, 1)
        self.assertEqual([row["automated_result"] for row in result["results"]], ["PASS", "PASS", "PASS", "FAIL"])

    def test_required_receiver_sets_and_duplicates_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            scan_path = Path(temporary) / "scan.json"
            scan_path.write_text(json.dumps(self.scan(100)), encoding="utf-8")
            cases = (
                ("asicen-w3u3-rx0-3", [f"0={scan_path}"]),
                ("px4-rx0-5", [f"{receiver}={scan_path}" for receiver in ("0", "1", "2", "3", "4", "5", "6")]),
                ("px4-rx7", [f"7={scan_path}", f"7={scan_path}"]),
                ("px4-rx7", [f"7={scan_path}", f"7={Path(temporary) / '.' / 'scan.json'}"]),
            )
            for profile, paths in cases:
                with self.subTest(profile=profile, paths=paths):
                    result = subprocess.run(
                        [sys.executable, str(SCRIPT), "--profile", profile, "--environment", "macos", *paths],
                        capture_output=True,
                        text=True,
                        check=False,
                    )
                    self.assertEqual(result.returncode, 1)
                    self.assertEqual(json.loads(result.stdout)["automated_result"], "FAIL")

    def test_duplicate_scan_file_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            paths = []
            for index in range(2):
                scan_path = Path(temporary) / f"scan-{index}.json"
                scan_path.write_text(json.dumps(self.scan(100)), encoding="utf-8")
                paths.append(f"6={scan_path}")
            result = subprocess.run(
                [sys.executable, str(SCRIPT), "--profile", "px4-rx6", "--environment", "macos", *paths],
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 1)
        self.assertTrue(any("duplicate scan file" in error for error in json.loads(result.stdout)["input_errors"]))

    def test_bom_encodings_match_and_invalid_bytes_fail_without_traceback(self):
        scan = self.scan(100)
        encoded = json.dumps(scan).encode("utf-8")
        with tempfile.TemporaryDirectory() as temporary:
            reports = []
            for name, data in (
                ("utf16le", b"\xff\xfe" + json.dumps(scan).encode("utf-16-le")),
                ("utf16be", b"\xfe\xff" + json.dumps(scan).encode("utf-16-be")),
                ("utf8bom", b"\xef\xbb\xbf" + encoded),
                ("utf8", encoded),
            ):
                path = Path(temporary) / f"{name}.json"
                path.write_bytes(data)
                result = subprocess.run(
                    [sys.executable, str(SCRIPT), "--profile", "siano-s1ud", "--environment", "windows", f"0={path}"],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(result.returncode, 0)
                reports.append(json.loads(result.stdout)["results"][0]["automated_result"])
            bad_path = Path(temporary) / "invalid.json"
            bad_path.write_bytes(b"\xff\xfe\x00")
            results = []
            for path, data in ((bad_path, b"\xff\xfe\x00"), (Path(temporary) / "utf16-no-bom.json", json.dumps(scan).encode("utf-16-le"))):
                path.write_bytes(data)
                results.append(subprocess.run(
                    [sys.executable, str(SCRIPT), "--profile", "siano-s1ud", "--environment", "windows", f"0={path}"],
                    capture_output=True,
                    text=True,
                    check=False,
                ))
        self.assertEqual(reports, ["PASS"] * 4)
        for result in results:
            self.assertEqual(result.returncode, 1)
            self.assertNotIn("Traceback", result.stderr)
            self.assertIn("unreadable scan JSON", json.loads(result.stdout)["results"][0]["error"])

    def test_output_limits_tool_scope(self):
        code, result = self.check("siano-s1ud", [self.scan(100)])
        self.assertEqual(code, 0)
        self.assertEqual(result["scope"], "tei_cc_numeric_only")
        self.assertFalse(result["capture_conditions_verified_by_tool"])
        self.assertEqual(result["t15_overall"], "not_determined_by_tool")
        self.assertNotIn("required_capture_seconds", result)

    def test_zero_packet_and_missing_scan_never_become_zero_quality_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            zero_path = Path(temporary) / "zero.json"
            zero_path.write_text(json.dumps(self.scan(0)), encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(SCRIPT), "--profile", "siano-s1ud", "--environment", "macos", f"0={zero_path}", f"0={Path(temporary) / 'missing.json'}"],
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 1)
        report = json.loads(result.stdout)
        self.assertEqual([row["error"] for row in report["results"]], ["packet count is zero", "missing scan JSON"])
        self.assertEqual(report["human_decision"], None)


if __name__ == "__main__":
    unittest.main()
