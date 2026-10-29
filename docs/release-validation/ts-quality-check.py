#!/usr/bin/env python3
import argparse
from decimal import Decimal, localcontext
from fractions import Fraction
import json
from pathlib import Path


PROFILES = {
    "asicen-w3u3-rx0-3": {"product": "ASICEN", "pattern": "PX-W3U3 receivers 0-3 simultaneous", "tei_limit_percent": Fraction(0, 1), "tei_op": "zero", "cc_limit": 0, "cc_op": "equal"},
    "px4-rx0-5": {"product": "PX4", "pattern": "PX-Q3U4 receivers 0-5 simultaneous", "tei_limit_percent": Fraction(0, 1), "tei_op": "zero", "cc_limit": 0, "cc_op": "equal"},
    "px4-rx6": {"product": "PX4", "pattern": "PX-Q3U4 receiver 6 single", "tei_limit_percent": Fraction(0, 1), "tei_op": "zero", "cc_limit": 0, "cc_op": "equal"},
    "px4-rx7": {"product": "PX4", "pattern": "PX-Q3U4 receiver 7 single", "tei_limit_percent": Fraction(1, 2), "tei_op": "less", "cc_limit": 40, "cc_op": "less"},
    "pix-dt181": {"product": "PIX", "pattern": "PIX-DT181-PU0", "tei_limit_percent": Fraction(0, 1), "tei_op": "zero", "cc_limit": 12, "cc_op": "less"},
    "pix-dt295": {"product": "PIX", "pattern": "PIX-DT295", "tei_limit_percent": Fraction(0, 1), "tei_op": "zero", "cc_limit": 12, "cc_op": "less"},
    "siano-s1ud": {"product": "Siano", "pattern": "PX-S1UD receiver 0", "tei_limit_percent": Fraction(1, 1000), "tei_op": "less", "cc_limit": 10, "cc_op": "less"},
}

ENVIRONMENTS = ("representative-linux", "windows", "macos")
CC_DEFINITION = "tei_null_excluded: adopted Siano ts-check.py continuity_errors; TEI packets and null PID are excluded, per-PID payload CC advances modulo 16, adaptation-only retains CC, discontinuity starts a new comparison epoch, and one byte-equivalent adjacent duplicate payload pair (PCR timestamp excepted) is tolerated."


def valid_count(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def load_scan(path):
    try:
        with Path(path).open(encoding="utf-8") as stream:
            scan = json.load(stream)
    except FileNotFoundError:
        return None, "missing scan JSON"
    except (OSError, json.JSONDecodeError) as error:
        return None, f"unreadable scan JSON: {error}"
    if not isinstance(scan, dict):
        return None, "scan JSON must be an object"
    for key in ("packets", "tei", "continuity_errors"):
        if not valid_count(scan.get(key)):
            return None, f"{key} must be a nonnegative integer"
    if scan["packets"] == 0:
        return None, "packet count is zero"
    if scan["tei"] > scan["packets"]:
        return None, "TEI count exceeds packet count"
    return scan, None


def evaluate_scan(path, scan, profile):
    packets = scan["packets"]
    tei = scan["tei"]
    cc = scan["continuity_errors"]
    if profile["tei_op"] == "zero":
        tei_pass = tei == 0
        tei_rule = "TEI == 0"
    else:
        limit = profile["tei_limit_percent"]
        tei_pass = tei * 100 * limit.denominator < packets * limit.numerator
        tei_rule = f"TEI * 100 * {limit.denominator} < packets * {limit.numerator}"
    if profile["cc_op"] == "equal":
        cc_pass = cc == profile["cc_limit"]
        cc_rule = f"CC == {profile['cc_limit']}"
    else:
        cc_pass = cc < profile["cc_limit"]
        cc_rule = f"CC < {profile['cc_limit']}"
    with localcontext() as context:
        context.prec = 24
        tei_rate = str(Decimal(tei * 100) / Decimal(packets))
    return {
        "input": str(path),
        "scan_file": scan.get("file"),
        "packets": packets,
        "tei": tei,
        "tei_rate_percent_display_only": tei_rate,
        "tei_rule": tei_rule,
        "tei_result": "PASS" if tei_pass else "FAIL",
        "continuity_errors_tei_null_excluded": cc,
        "cc_rule": cc_rule,
        "cc_result": "PASS" if cc_pass else "FAIL",
        "automated_result": "PASS" if tei_pass and cc_pass else "FAIL",
    }


def run(profile_name, environment, input_paths):
    profile = PROFILES[profile_name]
    results = []
    for input_path in input_paths:
        scan, error = load_scan(input_path)
        if error:
            results.append({"input": str(input_path), "automated_result": "FAIL", "error": error})
        else:
            results.append(evaluate_scan(input_path, scan, profile))
    passed = bool(results) and all(result["automated_result"] == "PASS" for result in results)
    limit = profile["tei_limit_percent"]
    return {
        "schema": "release-ts-quality-v1",
        "product": profile["product"],
        "profile": profile_name,
        "pattern": profile["pattern"],
        "environment": environment,
        "required_capture_seconds": 300,
        "cc_definition": CC_DEFINITION,
        "tei_rate_limit_percent": f"{limit.numerator}/{limit.denominator}",
        "cc_limit": profile["cc_limit"],
        "cc_comparison": "==" if profile["cc_op"] == "equal" else "<",
        "results": results,
        "automated_result": "PASS" if passed else "FAIL",
        "human_decision": None,
    }


def main():
    parser = argparse.ArgumentParser(description="Evaluate release 5-minute TS quality from fixed ts-check JSON results")
    parser.add_argument("--profile", choices=sorted(PROFILES), required=True)
    parser.add_argument("--environment", choices=ENVIRONMENTS, required=True)
    parser.add_argument("scan_json", nargs="+")
    args = parser.parse_args()
    result = run(args.profile, args.environment, args.scan_json)
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0 if result["automated_result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
