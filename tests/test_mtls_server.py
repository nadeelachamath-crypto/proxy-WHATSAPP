#!/usr/bin/env python3
"""Linux integration test: valid mTLS identity reaches the WAPX protocol."""
import json
import os
import socket
import ssl
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def run(cmd, cwd=None):
    subprocess.run(cmd, cwd=cwd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    if len(sys.argv) != 2:
        print("usage: test_mtls_server.py PATH_TO_WAPSERVER")
        return 2
    server_exe = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="waproxy-mtls-") as temp:
        root = Path(temp)
        certs = root / "certs"
        certs.mkdir()
        run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(certs / "ca.key")])
        run(["openssl", "req", "-x509", "-new", "-key", str(certs / "ca.key"), "-sha256", "-days", "2",
             "-subj", "/CN=WAProxy Test CA",
             "-addext", "basicConstraints=critical,CA:TRUE",
             "-addext", "keyUsage=critical,keyCertSign,cRLSign",
             "-addext", "subjectKeyIdentifier=hash", "-out", str(certs / "ca.crt")])
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

        port = free_port()
        config = {
            "bind_address": "127.0.0.1", "port": port,
            "server_certificate": str(certs / "server.crt"),
            "server_private_key": str(certs / "server.key"),
            "client_ca_certificate": str(certs / "ca.crt"),
            "max_connections": 8, "connect_timeout_ms": 2000,
            "tls_handshake_timeout_ms": 3000, "tunnel_idle_timeout_ms": 10000
        }
        config_path = root / "server.json"
        config_path.write_text(json.dumps(config))
        proc = subprocess.Popen([server_exe, "--config", str(config_path)], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        try:
            context = ssl.create_default_context(ssl.Purpose.SERVER_AUTH, cafile=str(certs / "ca.crt"))
            context.load_cert_chain(certfile=str(certs / "client.crt"), keyfile=str(certs / "client.key"))
            deadline = time.time() + 8
            last_error = None
            tls = None
            while time.time() < deadline:
                try:
                    raw = socket.create_connection(("127.0.0.1", port), timeout=1)
                    tls = context.wrap_socket(raw, server_hostname="localhost")
                    break
                except OSError as exc:
                    last_error = exc
                    time.sleep(0.1)
            if tls is None:
                raise RuntimeError(f"server did not accept TLS connections: {last_error}")
            with tls:
                tls.sendall(b"WAPX/1 CONNECT example.com 80\r\n")
                line = tls.recv(512).decode("ascii", "replace")
                if not line.startswith("WAPX/1 403 PORT_NOT_ALLOWED"):
                    raise RuntimeError(f"unexpected server response: {line!r}")

            no_identity_context = ssl.create_default_context(ssl.Purpose.SERVER_AUTH, cafile=str(certs / "ca.crt"))
            unauthenticated_rejected = False
            try:
                raw = socket.create_connection(("127.0.0.1", port), timeout=2)
                with no_identity_context.wrap_socket(raw, server_hostname="localhost") as unauthenticated:
                    # TLS 1.3 may finish the client-side handshake before the
                    # server's mandatory-certificate alert is observed. Force
                    # an application read/write to surface that alert.
                    unauthenticated.sendall(b"WAPX/1 CONNECT example.com 443\r\n")
                    unauthenticated.recv(1)
            except (ssl.SSLError, OSError):
                unauthenticated_rejected = True
            if not unauthenticated_rejected:
                raise RuntimeError("server accepted a TLS connection without a client certificate")
            print("PASS: server TLS certificate verified by client")
            print("PASS: server required and accepted the signed client certificate")
            print("PASS: server rejected a client that did not present a certificate")
            print("PASS: encrypted WAPX control frame was received")
            print("PASS: server rejected non-443 tunnel port")
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
