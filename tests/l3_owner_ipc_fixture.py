#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import os
import pathlib
import queue
import signal
import subprocess
import sys
import tempfile
import threading
import time


def write_firmware(path):
    image = bytearray(12 + 85828)
    image[0:4] = (173).to_bytes(4, "little")
    image[4:8] = (85828).to_bytes(4, "little")
    image[8:12] = (0x40260).to_bytes(4, "little")
    for offset in range(12, len(image)):
        image[offset] = offset & 0xff
    path.write_bytes(image)


def wait_ready(process):
    diagnostics = []
    while True:
        lines = queue.Queue()

        def read_line():
            lines.put(process.stderr.readline())

        reader = threading.Thread(target=read_line, daemon=True)
        reader.start()
        try:
            line = lines.get(timeout=10).decode(
                "utf-8", errors="replace").rstrip()
        except queue.Empty as error:
            raise TimeoutError(
                "sianod_l3_offline did not become ready\n" +
                "\n".join(diagnostics)) from error
        if line.startswith("sianod ready: endpoint="):
            return diagnostics
        diagnostics.append(line)
        if not line.startswith("siano-init-trace "):
            raise RuntimeError(
                f"unexpected daemon startup: {line!r}\n" +
                "\n".join(diagnostics))


def main():
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: l3_owner_ipc_fixture.py SIANOD_L3_OFFLINE SIANO_TS FIXTURE")
    daemon, client, fixture = map(pathlib.Path, sys.argv[1:])
    fixture = fixture.resolve()
    with tempfile.TemporaryDirectory(prefix="siano-l3-owner-") as temp:
        root = pathlib.Path(temp)
        firmware = root / "firmware.bin"
        output = root / "capture.ts"
        runtime = root / "runtime"
        write_firmware(firmware)
        env = os.environ.copy()
        env["SIANO_L3_OFFLINE_FIXTURE"] = str(fixture)
        args = [str(daemon), "--device", "usb:5-1.2", "--firmware",
                str(firmware), "--runtime-dir", str(runtime), "--instance",
                "offline"]
        if os.name == "nt":
            args.append("--exit-on-stdin-eof")
        server = subprocess.Popen(args, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, env=env)
        startup_diagnostics = []
        failure = None
        try:
            startup_diagnostics = wait_ready(server)
            time.sleep(0.25)
            capture = subprocess.run(
                [str(client), "--runtime-dir", str(runtime), "--instance",
                 "offline", "-c", "T27", "--system", "ISDB-T",
                 "--receiver", "0", "-t", "3", "-o", str(output)],
                capture_output=True, timeout=20)
            if capture.returncode != 0:
                raise RuntimeError(
                    "siano-ts failed: " + capture.stderr.decode(
                        "utf-8", errors="replace"))
            if not output.exists() or output.stat().st_size == 0:
                raise RuntimeError("siano-ts produced no transport stream")
        except Exception as error:
            failure = error
        finally:
            if server.poll() is None:
                if os.name == "nt":
                    server.stdin.close()
                    server.stdin = None
                else:
                    server.send_signal(signal.SIGTERM)
            try:
                _, stderr = server.communicate(timeout=15)
            except subprocess.TimeoutExpired:
                server.kill()
                _, stderr = server.communicate(timeout=5)
                failure = failure or RuntimeError(
                    "sianod_l3_offline did not shut down")
        diagnostics = "\n".join(
            startup_diagnostics +
            stderr.decode("utf-8", errors="replace").splitlines())
        if failure is not None:
            raise RuntimeError(
                f"{failure}\nsianod_l3_offline exited {server.returncode}:\n"
                f"{diagnostics}") from failure
        if server.returncode != 0:
            raise RuntimeError(f"sianod_l3_offline exited {server.returncode}:\n{diagnostics}")
        stats = next((line for line in diagnostics.splitlines()
                      if line.startswith("FAKE_STATS ")), "")
        required = ("owner_in_sub=", "owner_in_comp=", "owner_out_sub=",
                    "owner_out_comp=", "direct_sub=")
        if not stats or any(field not in stats for field in required):
            raise RuntimeError(f"missing owner-route diagnostics:\n{diagnostics}")
        values = dict(field.split("=", 1) for field in stats.split()[1:])
        if (int(values["owner_in_sub"]) == 0 or
                int(values["owner_in_comp"]) == 0 or
                int(values["owner_out_sub"]) == 0 or
                int(values["owner_out_comp"]) == 0 or
                int(values["direct_sub"]) != 0):
            raise RuntimeError(f"USB owner route proof failed: {stats}")
        print(f"Siano standard main/IPC/client owner fixture: PASS ({stats})")


if __name__ == "__main__":
    main()
