# WA Proxy — Version 0.1

**A local-only Windows diagnostic listener built with C++17.** Version 0.1 is the first implementation milestone, not the complete remote privacy proxy described in the long-term project plan.

## What works in v0.1

- Binds only to `127.0.0.1` (loopback). The configuration loader rejects other bind addresses.
- Defaults to TCP port `8080`; the port and operational limits are configurable.
- Handles a bounded HTTP request and provides `GET /health` and `GET /` responses.
- Explicitly returns HTTP `501 Not Implemented` for `CONNECT` requests.
- Tracks accepted/rejected/active connections and bytes received/sent.
- Writes timestamped console and file logs without recording request headers or destinations.
- Handles Ctrl+C / Ctrl+Break and drains worker threads on normal shutdown.
- Builds with Visual Studio 2022 (v143 toolset) or CMake 3.20+.

## What does not work yet

There is **no HTTP proxy forwarding**, no CONNECT tunnel, no TLS/OpenSSL transport, no remote VPS server, no DNS forwarding, no GUI, and no traffic-monitoring dashboard in v0.1. The app is a diagnostic listener only. A browser configured to use it as a proxy will not be able to browse through it yet.

## Quick start (Windows)

1. Install Visual Studio 2022 with **Desktop development with C++** and a Windows 10/11 SDK.
2. Open `WAProxy.sln`.
3. Select `x64` and `Debug` or `Release`, then build.
4. From the project root, run `bin\Release\WAProxy.exe` (or the Debug build path) after copying `config\config.example.json` to `config\config.json`.
5. Test `http://127.0.0.1:8080/health` in a browser, or run the smoke test described in `docs/INSTALL.md`.

See [BUILD.md](docs/BUILD.md), [INSTALL.md](docs/INSTALL.md), and [SECURITY.md](docs/SECURITY.md).

A ready-to-run Windows x64 Release build workflow is included at `.github/workflows/build-windows.yml`. Push the repository to GitHub and open **Actions** to download the `WAProxy-v0.1.0-Windows-x64` build artifact.

## Roadmap

- **0.1:** Local diagnostic listener — implemented in this package.
- **0.2:** Local HTTP CONNECT proxy — not included yet.
- **0.3:** Authenticated encrypted remote transport — not included yet.
- **0.4:** Linux VPS server — not included yet.
- **0.5:** Windows GUI and monitoring — not included yet.
- **1.0:** Integrated and security-tested application — future milestone.

## Scope and naming

“WA Proxy” is the project name only. This software does not impersonate WhatsApp, Facebook, or any other service. It cannot make arbitrary traffic appear to an ISP as genuine WhatsApp traffic. DNS changes, traffic labels, and protocol imitation do not guarantee destination masking.
