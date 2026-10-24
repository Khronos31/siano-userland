#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import subprocess
import sys
import threading
import time


def wait_ready(process):
    lines = []
    reader = threading.Thread(target=lambda: lines.append(process.stderr.readline()))
    reader.start()
    reader.join(timeout=10)
    if reader.is_alive():
        process.kill()
        process.wait(timeout=5)
        reader.join(timeout=5)
        raise TimeoutError("fixture did not report ready within 10 seconds")
    line = lines[0].decode("utf-8", errors="replace").rstrip("\r\n")
    if line != "sianod fixture ready":
        raise AssertionError(f"fixture did not become ready: {line!r}")


def test_eof_stops_and_closes(fixture):
    process = subprocess.Popen(
        [fixture, "--exit-on-stdin-eof"], stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        wait_ready(process)
        process.stdin.close()
        process.stdin = None
        stdout, stderr = process.communicate(timeout=10)
        output = stderr.decode("utf-8", errors="replace")
        assert process.returncode == 0, output
        assert "sianod fixture cleanup complete\n" in output, output
        assert stdout == b""
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)


def test_default_is_off(fixture):
    process = subprocess.Popen(
        [fixture], stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        wait_ready(process)
        process.stdin.close()
        process.stdin = None
        time.sleep(0.3)
        assert process.poll() is None, "stdin EOF stopped the default-off fixture"
    finally:
        if process.poll() is None:
            process.kill()
        process.communicate(timeout=5)


def test_product_cli(sianod):
    help_result = subprocess.run([sianod, "--help"], capture_output=True, text=True)
    help_output = help_result.stdout + help_result.stderr
    assert help_result.returncode == 0, help_output
    assert "--exit-on-stdin-eof" in help_output

    flag_result = subprocess.run(
        [sianod, "--exit-on-stdin-eof"], capture_output=True, text=True)
    assert flag_result.returncode == 2
    assert "--device or --fd is required" in flag_result.stderr, flag_result.stderr
    assert "unknown argument" not in flag_result.stderr, flag_result.stderr


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_sianod_windows_eof.py FIXTURE SIANOD")
    fixture, sianod = sys.argv[1:]
    test_eof_stops_and_closes(fixture)
    test_default_is_off(fixture)
    test_product_cli(sianod)
    print("Siano Windows stdin EOF/cleanup/default-off/CLI proof: PASS")


if __name__ == "__main__":
    main()
