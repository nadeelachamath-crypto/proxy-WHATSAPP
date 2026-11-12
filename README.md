# WA Proxy v1.0.0

WA Proxy is a self-hosted HTTPS CONNECT proxy with an optional authenticated TLS transport to a Linux VPS. The Windows client accepts browser proxy connections on `127.0.0.1:8080`. In remote mode, it opens a mutual-TLS connection to the configured VPS, sends the destination authority inside the encrypted channel, and the VPS resolves the destination and forwards TCP to destination port 443.

**WA Proxy is not WhatsApp, does not impersonate WhatsApp, and cannot make other traffic become genuine WhatsApp traffic.** It is a normal proxy with a TLS-protected client-to-VPS hop.

## Components

- `WAProxy.exe`: Windows console client.
- `WAProxyGUI.exe`: Windows desktop control panel with Start/Stop, status, endpoint and connection counters.
- `WAPServer`: Linux VPS relay server.
- Mutual TLS: the Windows client verifies the VPS certificate; the VPS requires and verifies a client certificate.
- Remote DNS: destination hostnames are sent inside TLS and resolved by the VPS system resolver.
- Fail closed: with `remote_tls_enabled: true`, a failed VPS/TLS connection returns 502. The client never silently falls back to a direct destination connection.
- Destination restrictions: VPS relays only TCP port 443 and rejects private, loopback, link-local, multicast and reserved destination addresses.

## Quick start

1. Build the Linux VPS relay and Windows client. See `docs/BUILD.md`.
2. Set up certificates and the VPS. See `docs/VPS_SETUP.md`.
3. Create the Windows client certificate locally and have the VPS sign its CSR. See `docs/CLIENT_SETUP.md`.
4. Copy `config/client.example.json` to `config/client.json`, set the VPS address and certificate paths, and keep `remote_tls_enabled` set to `true`. Remote TLS is now the default; direct mode requires an explicit opt-out.
5. Start `WAProxyGUI.exe` and click **Start Proxy**, or run `WAProxy.exe --config config/client.json`.
6. Configure an application/browser to use HTTP proxy `127.0.0.1`, port `8080`.
7. Test with `curl.exe -v -x http://127.0.0.1:8080 https://example.com/`.

For Windows client packaging, GitHub Actions publishes `WAProxy-v1.0.0-Windows-x64` with both the console client and GUI. For Linux, it publishes `WAProxy-v1.0.0-Linux-VPS` from an Ubuntu 22.04 build runner for compatibility with Ubuntu 22.04/24.04 VPS hosts. The VPS artifact includes `WAPServer`, its config template, PKI scripts and systemd unit; see `VPS_PACKAGE_INSTALL.md`.

## Build and verification status

The C++ client and Linux server build with GCC 14 and OpenSSL 3 in this development environment. Actual MSVC compilation and the Win32 GUI runtime must be verified by the included Windows GitHub Actions job or on a Windows 10/11 machine. Do not treat a Linux-built binary as a Windows executable.

See `docs/BUILD_VERIFICATION.md` for the current checks and remaining Windows-only verification. See `docs/GITHUB_BUILD.md` for exact steps to generate both release artifacts on GitHub Actions.

## Privacy boundaries

With remote TLS enabled and valid certificates, the user's ISP generally sees the connection to the VPS IP, TLS metadata, packet sizes, timing and total traffic. It does not see the CONNECT destination hostname or destination DNS lookup inside this application tunnel. The VPS and its resolver can see destination DNS queries, and destination websites see the VPS public IP. A DNS lookup for a VPS hostname occurs locally; using the VPS numeric IP in `remote_host` avoids that lookup. Browser prefetch, DoH, other apps, and traffic not configured to use this proxy may follow separate network paths.

The proxy does not create a full-device VPN, route all Windows traffic, guarantee anonymity, or change application traffic classification. HTTPS content remains end-to-end encrypted by the browser with the destination website; WA Proxy does not decrypt it.

## Project structure

```text
WAProxy/
  client/       Windows local CONNECT proxy, TLS client and GUI
  common/       socket utilities and single-threaded TLS relay loop
  server/       Linux mutual-TLS VPS relay
  config/       example client/server configuration
  docs/         build, install, security and setup instructions
  tests/        local, mTLS, fail-closed and TLS relay integration tests
  resources/    application icon and Win32 resources
  .github/      Windows/Linux GitHub Actions builds
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  WAProxy.sln
```

## License

MIT. See `LICENSE`.
