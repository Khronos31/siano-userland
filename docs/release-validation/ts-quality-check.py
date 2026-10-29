#!/usr/bin/env python3
import argparse
from decimal import Decimal, localcontext
from fractions import Fraction
import json
from pathlib import Path


PROFILES = {
    "asicen-w3u3-rx0-3": {"product": "ASICEN", "pattern": "PX-W3U3 receivers 0-3 simultaneous", "receivers": ("0", "1", "2", "3"), "tei_limit_percent": Fraction(0, 1), "tei_op": "zero", "cc_limit": 0, "cc_op": "equal"},
    "px4-rx0-5": {"product": "PX4", "pattern": "PX-Q3U4 receivers 0-5 simultaneous", "receivers": ("0", "1", "2", "3", "4", "5"), "tei_limit_percent": Fraction(0, 1), "tei_op": "zero", "cc_limit": 0, "cc_op": "equal"},
    "px4-rx6": {"product": "PX4", "pattern": "PX-Q3U4 receiver 6 single", "receivers": ("6",), "tei_limit_percent": Fraction(0, 1), "tei_op": "zero", "cc_limit": 0, "cc_op": "equal"},
    "px4-rx7": {"product": "PX4", "pattern": "PX-Q3U4 receiver 7 single", "receivers": ("7",), "tei_limit_percent": Fraction(1, 2), "tei_op": "less", "cc_limit": 40, "cc_op": "less"},
    "pix-dt181": {"product": "PIX", "pattern": "PIX-DT181-PU0", "receivers": ("0",), "tei_limit_percent": Fraction(0, 1), "tei_op": "zero", "cc_limit": 12, "cc_op": "less"},
    "pix-dt295": {"product": "PIX", "pattern": "PIX-DT295", "receivers": ("0",), "tei_limit_percent": Fraction(0, 1), "tei_op": "zero", "cc_limit": 12, "cc_op": "less"},
    "siano-s1ud": {"product": "Siano", "pattern": "PX-S1UD receiver 0", "receivers": ("0",), "tei_limit_percent": Fraction(1, 1000), "tei_op": "less", "cc_limit": 10, "cc_op": "less"},
}

ENVIRONMENTS = ("representative-linux", "windows", "macos")
CC_DEFINITION = "tei_null_excluded: adopted Siano ts-check.py continuity_errors; TEI packets and null PID are excluded, per-PID payload CC advances modulo 16, adaptation-only retains CC, discontinuity starts a new comparison epoch, and one byte-equivalent adjacent duplicate payload pair (PCR timestamp excepted) is tolerated."


def valid_count(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def load_scan(path):
    try:
        data = Path(path).read_bytes()
        if data.startswith(b"\xff\xfe"):
            text = data.decode("utf-16")
        elif data.startswith(b"\xfe\xff"):
            text = data.decode("utf-16")
        elif data.startswith(b"\xef\xbb\xbf"):
            text = data[3:].decode("utf-8")
        else:
            text = data.decode("utf-8")
        scan = json.loads(text)
    except FileNotFoundError:
        return None, "missing scan JSON"
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
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


def evaluate_scan(path, receiver, scan, profile):
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
        "receiver": receiver,
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


def run(profile_name, environment, inputs):
    profile = PROFILES[profile_name]
    results = []
    input_errors = []
    parsed_inputs = []
    for value in inputs:
        if "=" in value:
            receiver, path = value.split("=", 1)
        elif len(profile["receivers"]) == 1:
            receiver, path = profile["receivers"][0], value
        else:
            receiver, path = "", value
            input_errors.append(f"receiver label required for {value}")
        parsed_inputs.append((receiver, Path(path)))

    seen_receivers = set()
    seen_paths = set()
    seen_files = set()
    for receiver, input_path in parsed_inputs:
        if receiver in seen_receivers:
            input_errors.append(f"duplicate receiver: {receiver}")
        seen_receivers.add(receiver)
        if receiver not in profile["receivers"]:
            input_errors.append(f"unexpected receiver: {receiver or '(missing)'}")
        resolved_path = input_path.resolve()
        if resolved_path in seen_paths:
            input_errors.append(f"duplicate input path: {resolved_path}")
        seen_paths.add(resolved_path)

    missing_receivers = sorted(set(profile["receivers"]) - seen_receivers)
    if missing_receivers:
        input_errors.append(f"missing receivers: {','.join(missing_receivers)}")

    for receiver, input_path in parsed_inputs:
        scan, error = load_scan(input_path)
        if error:
            results.append({"input": str(input_path), "receiver": receiver, "automated_result": "FAIL", "error": error})
        else:
            scan_file = scan.get("file")
            if isinstance(scan_file, str) and scan_file in seen_files:
                input_errors.append(f"duplicate scan file: {scan_file}")
                results.append({"input": str(input_path), "receiver": receiver, "scan_file": scan_file, "automated_result": "FAIL", "error": "duplicate scan file"})
            else:
                if isinstance(scan_file, str):
                    seen_files.add(scan_file)
                results.append(evaluate_scan(input_path, receiver, scan, profile))
    passed = bool(results) and not input_errors and all(result["automated_result"] == "PASS" for result in results)
    limit = profile["tei_limit_percent"]
    return {
        "schema": "release-ts-quality-v1",
        "product": profile["product"],
        "profile": profile_name,
        "pattern": profile["pattern"],
        "environment": environment,
        "scope": "tei_cc_numeric_only",
        "capture_seconds_required": 300,
        "capture_conditions_verified_by_tool": False,
        "t15_overall": "not_determined_by_tool",
        "cc_definition": CC_DEFINITION,
        "tei_rate_limit_percent": f"{limit.numerator}/{limit.denominator}",
        "cc_limit": profile["cc_limit"],
        "cc_comparison": "==" if profile["cc_op"] == "equal" else "<",
        "results": results,
        "input_errors": input_errors,
        "automated_result": "PASS" if passed else "FAIL",
        "human_decision": None,
    }


def main():
    parser = argparse.ArgumentParser(description="Evaluate release 5-minute TS quality from fixed ts-check JSON results")
    parser.add_argument("--profile", choices=sorted(PROFILES), required=True)
    parser.add_argument("--environment", choices=ENVIRONMENTS, required=True)
    parser.add_argument("receiver_scan_json", nargs="+", metavar="RECEIVER=PATH")
    args = parser.parse_args()
    result = run(args.profile, args.environment, args.receiver_scan_json)
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0 if result["automated_result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
