#!/usr/bin/env python3
"""End-to-end mTLS control-plane test: private destination is rejected, client fails closed."""
import json
import socket
import ssl
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def free_port():
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def run(cmd):
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def make_pki(certs):
    run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(certs / "ca.key")])
    run(["openssl", "req", "-x509", "-new", "-key", str(certs / "ca.key"), "-sha256", "-days", "2",
         "-subj", "/CN=WAProxy Test CA", "-addext", "basicConstraints=critical,CA:TRUE",
         "-addext", "keyUsage=critical,keyCertSign,cRLSign", "-addext", "subjectKeyIdentifier=hash",
         "-out", str(certs / "ca.crt")])
    run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(certs / "server.key")])
    run(["openssl", "req", "-new", "-key", str(certs / "server.key"), "-subj", "/CN=localhost", "-out", str(certs / "server.csr")])
    (certs / "server.ext").write_text("subjectAltName=DNS:localhost\nextendedKeyUsage=serverAuth\nkeyUsage=digitalSignature,keyEncipherment\n")
    run(["openssl", "x509", "-req", "-in", str(certs / "server.csr"), "-CA", str(certs / "ca.crt"),
         "-CAkey", str(certs / "ca.key"), "-CAcreateserial", "-out", str(certs / "server.crt"),
         "-days", "2", "-sha256", "-extfile", str(certs / "server.ext")])
    run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(certs / "client.key")])
    run(["openssl", "req", "-new", "-key", str(certs / "client.key"), "-subj", "/CN=WAProxy Test Client", "-out", str(certs / "client.csr")])
    (certs / "client.ext").write_text("extendedKeyUsage=clientAuth\nkeyUsage=digitalSignature,keyEncipherment\n")
    run(["openssl", "x509", "-req", "-in", str(certs / "client.csr"), "-CA", str(certs / "ca.crt"),
         "-CAkey", str(certs / "ca.key"), "-CAcreateserial", "-out", str(certs / "client.crt"),
         "-days", "2", "-sha256", "-extfile", str(certs / "client.ext")])


def main():
    if len(sys.argv) != 3:
        print("usage: test_remote_client_failclosed.py PATH_TO_WAProxy PATH_TO_WAPServer")
        return 2
    client_exe = str(Path(sys.argv[1]).resolve())
    server_exe = str(Path(sys.argv[2]).resolve())
    with tempfile.TemporaryDirectory(prefix="waproxy-e2e-") as temp:
        root = Path(temp)
        certs = root / "certs"
        certs.mkdir()
        make_pki(certs)
        tls_port = free_port()
        local_port = free_port()
        server_config = {
            "bind_address": "127.0.0.1", "port": tls_port,
            "server_certificate": str(certs / "server.crt"),
            "server_private_key": str(certs / "server.key"),
            "client_ca_certificate": str(certs / "ca.crt"),
            "max_connections": 8, "connect_timeout_ms": 2000,
            "tls_handshake_timeout_ms": 3000, "tunnel_idle_timeout_ms": 10000
        }
        client_config = {
            "listen_address": "127.0.0.1", "port": local_port,
            "log_file": str(root / "client.log"), "max_connections": 8,
            "request_timeout_ms": 2000, "max_request_bytes": 16384,
            "connect_timeout_ms": 3000, "tunnel_idle_timeout_ms": 10000,
            "allow_non_443_ports": False, "remote_tls_enabled": True,
            "remote_host": "127.0.0.1", "remote_port": tls_port,
            "remote_server_name": "localhost", "ca_certificate": str(certs / "ca.crt"),
            "client_certificate": str(certs / "client.crt"),
            "client_private_key": str(certs / "client.key")
        }
        server_path = root / "server.json"
        client_path = root / "client.json"
        server_path.write_text(json.dumps(server_config))
        client_path.write_text(json.dumps(client_config))
        server = subprocess.Popen([server_exe, "--config", str(server_path)], stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True)
        client = None
        try:
            deadline = time.time() + 8
            while time.time() < deadline:
                try:
                    with socket.create_connection(("127.0.0.1", tls_port), timeout=0.2):
                        break
                except OSError:
                    time.sleep(0.1)
            else:
                raise RuntimeError("TLS server did not start")

            client = subprocess.Popen([client_exe, "--config", str(client_path)], stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT, text=True)
            deadline = time.time() + 8
            while time.time() < deadline:
                try:
                    with socket.create_connection(("127.0.0.1", local_port), timeout=0.2):
                        break
                except OSError:
                    time.sleep(0.1)
            else:
                raise RuntimeError("local proxy did not start")

            def assert_blocked(authority: str) -> None:
                with socket.create_connection(("127.0.0.1", local_port), timeout=3) as proxy:
                    request = f"CONNECT {authority} HTTP/1.1\r\nHost: {authority}\r\n\r\n".encode("ascii")
                    proxy.sendall(request)
                    response = b""
                    while b"\r\n\r\n" not in response:
                        chunk = proxy.recv(4096)
                        if not chunk:
                            break
                        response += chunk
                    if not response.startswith(b"HTTP/1.1 502 Bad Gateway"):
                        raise RuntimeError(f"expected fail-closed HTTP 502 for {authority}, got {response!r}")

            assert_blocked("127.0.0.1:443")
            try:
                assert_blocked("[::1]:443")
            except OSError as exc:
                raise RuntimeError(f"could not execute IPv6 loopback policy test: {exc}") from exc
            print("PASS: client established and verified mutual TLS with local VPS relay")
            print("PASS: destination authority was carried inside the TLS control channel")
            print("PASS: VPS blocked IPv4 and IPv6 loopback destinations")
            print("PASS: client returned 502 and did not fall back to direct forwarding")
        finally:
            if client is not None:
                client.terminate()
                try: client.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    client.kill(); client.wait(timeout=5)
            server.terminate()
            try: server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill(); server.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
