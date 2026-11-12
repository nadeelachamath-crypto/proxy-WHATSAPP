#!/usr/bin/env python3
"""Local HTTP CONNECT smoke test for the WA Proxy executable.
Uses an explicit temporary direct-mode config and a loopback echo target.
"""
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


def reserve_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def main():
    if len(sys.argv) != 2:
        print("usage: smoke_proxy.py PATH_TO_WAProxy")
        return 2
    exe = str(Path(sys.argv[1]).resolve())
    proxy_port = reserve_port()
    target = socket.socket()
    target.bind(("127.0.0.1", 0))
    target.listen(1)
    target_port = target.getsockname()[1]

    def echo_worker():
        conn, _ = target.accept()
        with conn:
            while True:
                data = conn.recv(4096)
                if not data:
                    break
                conn.sendall(data)

    echo_thread = threading.Thread(target=echo_worker, daemon=True)
    echo_thread.start()

    with tempfile.TemporaryDirectory(prefix="waproxy-smoke-") as temp:
        root = Path(temp)
        config = {
            "listen_address": "127.0.0.1",
            "port": proxy_port,
            "log_file": str(root / "wa-proxy.log"),
            "max_connections": 8,
            "request_timeout_ms": 2000,
            "max_request_bytes": 16384,
            "connect_timeout_ms": 3000,
            "tunnel_idle_timeout_ms": 10000,
            "allow_non_443_ports": True,
            "remote_tls_enabled": False
        }
        config_path = root / "client.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        creationflags = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0) if os.name == "nt" else 0
        process = subprocess.Popen([exe, "--config", str(config_path)], stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, creationflags=creationflags)
        try:
            deadline = time.time() + 8
            while time.time() < deadline:
                try:
                    with socket.create_connection(("127.0.0.1", proxy_port), timeout=0.25) as s:
                        s.sendall(b"GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n")
                        response = s.recv(8192)
                        if b"200 OK" in response and b'"connect_tunneling": true' in response:
                            break
                except OSError:
                    time.sleep(0.1)
            else:
                raise RuntimeError("proxy health endpoint did not become ready")

            with socket.create_connection(("127.0.0.1", proxy_port), timeout=3) as proxy:
                request = (f"CONNECT 127.0.0.1:{target_port} HTTP/1.1\r\n"
                           f"Host: 127.0.0.1:{target_port}\r\n\r\n").encode()
                proxy.sendall(request)
                response = b""
                while b"\r\n\r\n" not in response:
                    chunk = proxy.recv(4096)
                    if not chunk:
                        raise RuntimeError("proxy closed before CONNECT response")
                    response += chunk
                if b"200 Connection Established" not in response:
                    raise RuntimeError(f"unexpected CONNECT response: {response!r}")
                payload = b"WAProxy-v1.0-connect-smoke"
                proxy.sendall(payload)
                received = b""
                while len(received) < len(payload):
                    chunk = proxy.recv(4096)
                    if not chunk:
                        break
                    received += chunk
                if received != payload:
                    raise RuntimeError(f"echo mismatch: {received!r}")
            print("PASS: health endpoint returned HTTP 200")
            print("PASS: CONNECT returned 200 Connection Established")
            print("PASS: bidirectional tunnel echo matched")
        finally:
            try:
                if process.poll() is None:
                    if os.name == "nt" and hasattr(signal, "CTRL_BREAK_EVENT"):
                        process.send_signal(signal.CTRL_BREAK_EVENT)
                    else:
                        process.send_signal(signal.SIGINT)
                    process.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
            if process.stdout:
                output = process.stdout.read()
                process.stdout.close()
            else:
                output = ""
            if process.returncode not in (0, -signal.SIGINT):
                raise RuntimeError(f"proxy process exited {process.returncode}: {output}")
            target.close()
            echo_thread.join(timeout=2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
