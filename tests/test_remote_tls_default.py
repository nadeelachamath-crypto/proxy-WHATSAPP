#!/usr/bin/env python3
"""Verify missing remote settings fail closed rather than starting direct mode."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_remote_tls_default.py PATH_TO_WAPROXY")
        return 2
    exe = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="waproxy-default-security-") as temp:
        config = {
            "listen_address": "127.0.0.1",
            "port": 18080,
            "log_file": str(Path(temp) / "client.log"),
            "max_connections": 4,
            "request_timeout_ms": 1000,
            "max_request_bytes": 4096,
            "connect_timeout_ms": 1000,
            "tunnel_idle_timeout_ms": 10000
        }
        path = Path(temp) / "client.json"
        path.write_text(json.dumps(config), encoding="utf-8")
        result = subprocess.run([exe, "--config", str(path)], capture_output=True, text=True, timeout=5)
        combined = (result.stdout or "") + (result.stderr or "")
        if result.returncode == 0 or "remote TLS requires" not in combined:
            raise RuntimeError(f"missing remote config did not fail closed (exit={result.returncode}): {combined}")
    print("PASS: remote TLS is the default; missing VPS identity fails before listener startup")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
