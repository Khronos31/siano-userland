import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "docs/release-validation/ts-quality-check.py"


class ReleaseTsQualityTests(unittest.TestCase):
    def check(self, profile, scans, environment="representative-linux"):
        with tempfile.TemporaryDirectory() as temporary:
            paths = []
            for index, scan in enumerate(scans):
                scan_path = Path(temporary) / f"scan-{index}.json"
                scan_path.write_text(json.dumps(scan), encoding="utf-8")
                paths.append(str(scan_path))
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
                self.assertEqual(self.check(profile, [self.scan(100)])[0], 0)
                self.assertEqual(self.check(profile, [self.scan(100, tei=1)])[0], 1)
                self.assertEqual(self.check(profile, [self.scan(100, cc=1)])[0], 1)

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
        code, result = self.check("asicen-w3u3-rx0-3", [self.scan(100), self.scan(100, cc=1)])
        self.assertEqual(code, 1)
        self.assertEqual([row["automated_result"] for row in result["results"]], ["PASS", "FAIL"])

    def test_zero_packet_and_missing_scan_never_become_zero_quality_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            zero_path = Path(temporary) / "zero.json"
            zero_path.write_text(json.dumps(self.scan(0)), encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(SCRIPT), "--profile", "siano-s1ud", "--environment", "macos", str(zero_path), str(Path(temporary) / "missing.json")],
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
