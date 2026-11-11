#!/usr/bin/env python3
"""Cross-platform smoke test for WA Proxy v0.2 CONNECT data forwarding."""
from __future__ import annotations

import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def read_headers(sock: socket.socket, timeout: float = 5.0) -> bytes:
    sock.settimeout(timeout)
    data = bytearray()
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data.extend(chunk)
        if len(data) > 16384:
            raise RuntimeError("HTTP response headers exceeded test limit")
    return bytes(data)


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: python scripts/smoke_test.py PATH_TO_WAPROXY_EXECUTABLE", file=sys.stderr)
        return 2

    executable = Path(sys.argv[1]).resolve()
    if not executable.is_file():
        print(f"Executable not found: {executable}", file=sys.stderr)
        return 2

    proxy_port = reserve_port()
    upstream_port = reserve_port()
    echo_ready = threading.Event()
    echo_errors: list[str] = []

    def echo_server() -> None:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
                server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                server.bind(("127.0.0.1", upstream_port))
                server.listen(1)
                server.settimeout(8.0)
                echo_ready.set()
                conn, _ = server.accept()
                with conn:
                    conn.settimeout(5.0)
                    received = bytearray()
                    while True:
                        block = conn.recv(4096)
                        if not block:
                            break
                        received.extend(block)
                    conn.sendall(received)
        except Exception as exc:  # surfaced in main thread below
            echo_errors.append(str(exc))
            echo_ready.set()

    echo_thread = threading.Thread(target=echo_server, daemon=True)
    echo_thread.start()
    if not echo_ready.wait(3.0):
        raise RuntimeError("Local test upstream failed to start")

    with tempfile.TemporaryDirectory(prefix="waproxy-v02-") as temporary:
        temp = Path(temporary)
        config_path = temp / "config.json"
        log_path = temp / "wa-proxy.log"
        config_path.write_text(
            json.dumps({
                "listen_address": "127.0.0.1",
                "port": proxy_port,
                "log_file": str(log_path),
                "max_connections": 8,
                "request_timeout_ms": 1500,
                "max_request_bytes": 16384,
                "connect_timeout_ms": 2000,
                "tunnel_idle_timeout_ms": 15000,
                "allow_non_443_ports": True,
            }, indent=2), encoding="utf-8")

        flags = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0) if os.name == "nt" else 0
        process = subprocess.Popen(
            [str(executable), "--config", str(config_path)],
            cwd=str(executable.parent), stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, creationflags=flags,
        )
        try:
            deadline = time.time() + 10.0
            while time.time() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f"WA Proxy exited early: {process.returncode}")
                try:
                    with socket.create_connection(("127.0.0.1", proxy_port), timeout=0.25):
                        break
                except OSError:
                    time.sleep(0.1)
            else:
                raise RuntimeError("WA Proxy listener did not start")

            with socket.create_connection(("127.0.0.1", proxy_port), timeout=3.0) as client:
                client.sendall(
                    f"CONNECT 127.0.0.1:{upstream_port} HTTP/1.1\r\n"
                    f"Host: 127.0.0.1:{upstream_port}\r\n\r\n".encode("ascii")
                )
                response = read_headers(client)
                if not response.startswith(b"HTTP/1.1 200 Connection Established"):
                    raise AssertionError(f"CONNECT did not establish tunnel: {response!r}")
                print("PASS: CONNECT returned HTTP 200 Connection Established.")

                test_payload = b"WAProxy-v0.2-bidirectional-tunnel-test"
                client.sendall(test_payload)
                client.shutdown(socket.SHUT_WR)
                echoed = bytearray()
                while len(echoed) < len(test_payload):
                    chunk = client.recv(len(test_payload) - len(echoed))
                    if not chunk:
                        break
                    echoed.extend(chunk)
                if bytes(echoed) != test_payload:
                    raise AssertionError(f"Tunnel payload mismatch: {bytes(echoed)!r}")
                print("PASS: Bytes traversed the proxy in both directions.")

            echo_thread.join(timeout=3.0)
            if echo_errors:
                raise RuntimeError("Local echo server failed: " + "; ".join(echo_errors))

            with socket.create_connection(("127.0.0.1", proxy_port), timeout=3.0) as health_client:
                health_client.sendall(
                    b"GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
                )
                health_response = read_headers(health_client)
                if b"HTTP/1.1 200 OK" not in health_response:
                    raise AssertionError(f"Health check failed: {health_response!r}")
                health_body = health_response.split(b"\r\n\r\n", 1)[1]
                while True:
                    chunk = health_client.recv(4096)
                    if not chunk:
                        break
                    health_body += chunk
                # The helper may receive headers and body in the same initial read.
                health_json = json.loads(health_body.decode("utf-8"))
                if health_json.get("connect_tunneling") is not True:
                    raise AssertionError("Health endpoint did not report CONNECT enabled")
                print("PASS: /health reports CONNECT tunneling enabled.")

        finally:
            if process.poll() is None:
                try:
                    if os.name == "nt" and hasattr(signal, "CTRL_BREAK_EVENT"):
                        process.send_signal(signal.CTRL_BREAK_EVENT)
                    else:
                        process.send_signal(signal.SIGINT)
                    process.wait(timeout=5.0)
                except (OSError, subprocess.TimeoutExpired):
                    process.terminate()
                    try:
                        process.wait(timeout=3.0)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=3.0)

            output = process.stdout.read() if process.stdout else ""
            if process.stdout:
                process.stdout.close()
            if process.returncode not in (0, -signal.SIGINT):
                raise RuntimeError(f"WA Proxy exited unexpectedly ({process.returncode}).\n{output}")
            if not log_path.is_file():
                raise AssertionError("Expected log file was not created")
            print("PASS: Graceful shutdown and log file.")

        # Verify the default destination policy separately: only port 443 is accepted.
        policy_port = reserve_port()
        policy_log = temp / "policy.log"
        policy_config = temp / "policy-config.json"
        policy_config.write_text(json.dumps({
            "listen_address": "127.0.0.1",
            "port": policy_port,
            "log_file": str(policy_log),
            "max_connections": 4,
            "request_timeout_ms": 1000,
            "max_request_bytes": 16384,
            "connect_timeout_ms": 1000,
            "tunnel_idle_timeout_ms": 10000,
            "allow_non_443_ports": False,
        }), encoding="utf-8")
        policy_process = subprocess.Popen(
            [str(executable), "--config", str(policy_config)],
            cwd=str(executable.parent), stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, creationflags=flags,
        )
        try:
            deadline = time.time() + 8.0
            while time.time() < deadline:
                if policy_process.poll() is not None:
                    raise RuntimeError("Policy-test WA Proxy process exited early")
                try:
                    with socket.create_connection(("127.0.0.1", policy_port), timeout=0.2):
                        break
                except OSError:
                    time.sleep(0.1)
            else:
                raise RuntimeError("Policy-test listener did not start")
            with socket.create_connection(("127.0.0.1", policy_port), timeout=2.0) as policy_client:
                policy_client.sendall(
                    f"CONNECT 127.0.0.1:{upstream_port} HTTP/1.1\r\n"
                    f"Host: 127.0.0.1:{upstream_port}\r\n\r\n".encode("ascii")
                )
                policy_response = read_headers(policy_client)
                if not policy_response.startswith(b"HTTP/1.1 403 Forbidden"):
                    raise AssertionError(f"Default port policy did not reject non-443: {policy_response!r}")
                print("PASS: Default policy rejects non-443 CONNECT destinations.")
        finally:
            if policy_process.poll() is None:
                try:
                    if os.name == "nt" and hasattr(signal, "CTRL_BREAK_EVENT"):
                        policy_process.send_signal(signal.CTRL_BREAK_EVENT)
                    else:
                        policy_process.send_signal(signal.SIGINT)
                    policy_process.wait(timeout=5.0)
                except (OSError, subprocess.TimeoutExpired):
                    policy_process.terminate()
                    policy_process.wait(timeout=3.0)
            if policy_process.stdout:
                policy_output = policy_process.stdout.read()
                policy_process.stdout.close()
            if policy_process.returncode not in (0, -signal.SIGINT):
                raise RuntimeError(f"Policy-test process exited with code {policy_process.returncode}: {policy_output}")

    print("All WA Proxy v0.2 smoke tests passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
