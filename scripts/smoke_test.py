#!/usr/bin/env python3
"""End-to-end smoke test for WA Proxy v0.1. Uses Python standard library only."""

from __future__ import annotations

import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def wait_for_listener(host: str, port: int, process: subprocess.Popen[str]) -> None:
    deadline = time.time() + 10.0
    last_error: OSError | None = None
    while time.time() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"WA Proxy exited early with code {process.returncode}")
        try:
            with socket.create_connection((host, port), timeout=0.25):
                return
        except OSError as error:
            last_error = error
            time.sleep(0.1)
    raise RuntimeError(f"Listener did not start: {last_error}")


def http_request(port: int, request: bytes) -> bytes:
    with socket.create_connection(("127.0.0.1", port), timeout=3.0) as connection:
        connection.settimeout(4.0)
        connection.sendall(request)
        chunks: list[bytes] = []
        while True:
            try:
                block = connection.recv(4096)
            except socket.timeout:
                break
            if not block:
                break
            chunks.append(block)
    return b"".join(chunks)


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: python scripts/smoke_test.py PATH_TO_WAPROXY_EXECUTABLE", file=sys.stderr)
        return 2

    executable = Path(sys.argv[1]).resolve()
    if not executable.is_file():
        print(f"Executable not found: {executable}", file=sys.stderr)
        return 2

    # Ask the OS for an unused high port to avoid collisions with another test run.
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as port_probe:
        port_probe.bind(("127.0.0.1", 0))
        port = int(port_probe.getsockname()[1])

    with tempfile.TemporaryDirectory(prefix="waproxy-v01-") as temporary:
        temp_dir = Path(temporary)
        config_file = temp_dir / "config.json"
        log_file = temp_dir / "wa-proxy.log"
        config_file.write_text(
            json.dumps(
                {
                    "listen_address": "127.0.0.1",
                    "port": port,
                    "log_file": str(log_file),
                    "max_connections": 8,
                    "request_timeout_ms": 1000,
                    "max_request_bytes": 8192,
                },
                indent=2,
            ),
            encoding="utf-8",
        )

        creation_flags = 0
        if os.name == "nt":
            creation_flags = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
        process = subprocess.Popen(
            [str(executable), "--config", str(config_file)],
            cwd=str(executable.parent),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            creationflags=creation_flags,
        )

        try:
            wait_for_listener("127.0.0.1", port, process)

            health = http_request(
                port,
                b"GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",
            )
            if b"HTTP/1.1 200 OK" not in health:
                raise AssertionError(f"Health endpoint did not return 200: {health!r}")
            header, _, body = health.partition(b"\r\n\r\n")
            del header
            health_json = json.loads(body.decode("utf-8"))
            assert health_json["version"] == "0.1.0"
            assert health_json["status"] == "ok"
            assert health_json["connect_tunneling"] is False
            print("PASS: GET /health returned valid v0.1 JSON.")

            connect = http_request(
                port,
                b"CONNECT example.invalid:443 HTTP/1.1\r\nHost: example.invalid:443\r\n\r\n",
            )
            if b"HTTP/1.1 501 Not Implemented" not in connect:
                raise AssertionError(f"CONNECT did not return 501: {connect!r}")
            print("PASS: CONNECT returned the expected 501 Not Implemented.")

        finally:
            if process.poll() is None:
                try:
                    if os.name == "nt" and hasattr(signal, "CTRL_BREAK_EVENT"):
                        process.send_signal(signal.CTRL_BREAK_EVENT)
                    else:
                        process.send_signal(signal.SIGINT)
                    process.wait(timeout=6.0)
                except (OSError, subprocess.TimeoutExpired):
                    process.terminate()
                    try:
                        process.wait(timeout=3.0)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=3.0)

            output = ""
            if process.stdout is not None:
                output = process.stdout.read()
                process.stdout.close()
            if process.returncode not in (0, -signal.SIGINT):
                raise RuntimeError(
                    f"WA Proxy exited unexpectedly with code {process.returncode}.\n{output}"
                )
            print("PASS: Process shut down.")
            if log_file.exists():
                print(f"PASS: Log file created ({log_file.stat().st_size} bytes).")
            else:
                raise AssertionError("Log file was not created.")

    print("All WA Proxy v0.1 smoke tests passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # keep a clear failure summary for Windows users
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
