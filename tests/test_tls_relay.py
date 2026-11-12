#!/usr/bin/env python3
"""Generate an ephemeral test PKI and exercise the C++ mTLS relay close-notify path."""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


def run(args: list[str]) -> None:
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_tls_relay.py PATH_TO_WAProxyRelayTest", file=sys.stderr)
        return 2
    test_exe = Path(sys.argv[1]).resolve()
    if not test_exe.is_file():
        print(f"test executable not found: {test_exe}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="waproxy-relay-pki-") as temporary:
        certs = Path(temporary)
        run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(certs / "ca.key")])
        run(["openssl", "req", "-x509", "-new", "-key", str(certs / "ca.key"), "-sha256", "-days", "2",
             "-subj", "/CN=WAProxy Relay Test CA", "-addext", "basicConstraints=critical,CA:TRUE",
             "-addext", "keyUsage=critical,keyCertSign,cRLSign", "-addext", "subjectKeyIdentifier=hash",
             "-out", str(certs / "ca.crt")])

        run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(certs / "server.key")])
        run(["openssl", "req", "-new", "-key", str(certs / "server.key"), "-subj", "/CN=localhost", "-out", str(certs / "server.csr")])
        (certs / "server.ext").write_text(
            "basicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\nsubjectAltName=DNS:localhost\n",
            encoding="ascii",
        )
        run(["openssl", "x509", "-req", "-in", str(certs / "server.csr"), "-CA", str(certs / "ca.crt"),
             "-CAkey", str(certs / "ca.key"), "-CAcreateserial", "-out", str(certs / "server.crt"),
             "-days", "2", "-sha256", "-extfile", str(certs / "server.ext")])

        run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(certs / "client.key")])
        run(["openssl", "req", "-new", "-key", str(certs / "client.key"), "-subj", "/CN=WAProxy Relay Test Client", "-out", str(certs / "client.csr")])
        (certs / "client.ext").write_text(
            "basicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=clientAuth\n",
            encoding="ascii",
        )
        run(["openssl", "x509", "-req", "-in", str(certs / "client.csr"), "-CA", str(certs / "ca.crt"),
             "-CAkey", str(certs / "ca.key"), "-CAcreateserial", "-out", str(certs / "client.crt"),
             "-days", "2", "-sha256", "-extfile", str(certs / "client.ext")])

        subprocess.run([
            str(test_exe), str(certs / "ca.crt"), str(certs / "server.crt"), str(certs / "server.key"),
            str(certs / "client.crt"), str(certs / "client.key"),
        ], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
