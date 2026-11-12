# WA Proxy v0.2.0

WA Proxy is a self-hosted, loopback-only Windows HTTP CONNECT proxy prototype written in C++17.

## What v0.2 does

- Listens on `127.0.0.1:8080` by default. It does not bind to LAN or public interfaces.
- Accepts HTTP `CONNECT host:443` requests and opens a direct TCP connection to the destination.
- Relays the HTTPS byte stream in both directions without decrypting or modifying TLS.
- Performs DNS resolution on the Windows client using the operating system resolver.
- Enforces a 443-only CONNECT port policy by default.
- Exposes `GET /health`, connection counters, and timestamped logs.
- Has connection limits, connect/request/idle timeouts, and Ctrl+C shutdown handling.

## What v0.2 does NOT do

- It does not create an encrypted connection to a VPS. The client-to-destination TCP route is direct.
- It does not hide destination IP addresses, DNS lookups, or the fact that a proxy is being used from the ISP/network operator.
- It does not impersonate WhatsApp or make non-WhatsApp traffic appear to be WhatsApp traffic.
- It does not provide DNS-over-HTTPS, a VPN, a GUI, UDP/QUIC proxying, or plain HTTP forwarding.

The HTTPS connection between the browser and the website remains protected by the browser's normal TLS. The local proxy itself does not terminate that TLS session.

## Build and run (Windows)

See [docs/BUILD.md](docs/BUILD.md) and [docs/INSTALL.md](docs/INSTALL.md). A GitHub Actions workflow is included at `.github/workflows/build-windows.yml` to build with MSBuild on a Windows runner and upload a tested artifact.

After building, from the repository root:

```powershell
Copy-Item .\config\config.example.json .\config\config.json
.\bin\Release\WAProxy.exe --config .\config\config.json
```

Check the health endpoint from another terminal:

```powershell
curl.exe -i http://127.0.0.1:8080/health
```

Use a browser's manual HTTP proxy setting `127.0.0.1`, port `8080`. The proxy supports HTTPS via CONNECT only. Stop it with Ctrl+C.

## Test the tunnel

The included smoke test starts a local TCP echo service on a temporary port, temporarily enables non-443 ports in a temporary test configuration, checks CONNECT 200, verifies bytes in both directions, tests `/health`, and tests shutdown:

```powershell
python .\scripts\smoke_test.py .\bin\Release\WAProxy.exe
```

For normal use leave `allow_non_443_ports` set to `false`.

## Development roadmap

- v0.1: diagnostic listener (completed).
- v0.2: local direct HTTPS CONNECT forwarding (this release).
- v0.3: authenticated TLS transport from client to remote endpoint.
- v0.4: Linux VPS server with client authentication and DNS forwarding.
- v0.5: Windows GUI, status, and monitoring.
- v1.0: integrated release and security testing.

Do not treat a future stage as implemented until its tests and release artifacts exist.
