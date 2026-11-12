# Build verification report

## Verified in the development container

Environment: Linux x86-64, GCC 14.2, CMake 3.31.6, OpenSSL 3.5.7.

- CMake Release and Debug configuration and compilation of the Linux `WAProxy` client and `WAPServer` relay.
- Clang 17 Release compilation in addition to GCC 14.
- AddressSanitizer + UndefinedBehaviorSanitizer Debug build and execution of the integration tests.
- Compilation of the dedicated TLS relay regression test target.
- Local listener health endpoint returns HTTP 200.
- Local direct-mode CONNECT tunnel establishes and transfers bytes in both directions (test-only config explicitly sets `remote_tls_enabled: false`).
- VPS mutual TLS: the server certificate is verified by the client, the server requires a CA-signed client certificate, and a client without a certificate is rejected.
- WAPX control frame is carried inside TLS; destination port other than 443 is rejected.
- Client-to-VPS authenticated control flow is exercised; the VPS rejects a loopback destination and the Windows-client proxy logic returns 502 without a direct fallback.
- Shared TLS relay transfers payloads in both directions and completes TCP half-close/TLS close-notify.
- Configuration now defaults to remote TLS enabled; direct mode requires an explicit `remote_tls_enabled: false` setting.

## Not verified here

- Native MSVC compilation of `WAProxy.exe` and `WAProxyGUI.exe`.
- Native Win32 GUI runtime behavior.
- A live public VPS deployment using the user's DNS name, certificates, firewall and network.
- A live end-to-end public HTTPS destination through an actual VPS, because outbound DNS/network access is not available in this build environment.
- Long-duration/high-concurrency, soak, and independent security audit.

The included GitHub Actions workflow builds the Windows x64 client/GUI and Linux VPS server and runs the Windows local-proxy smoke test plus Linux mTLS and TLS-relay integration tests. A green Actions run is required before treating the Windows executables as verified.

## Acceptance tests after deployment

1. Start `WAPServer` with a CA-signed server certificate; verify that it rejects a client without a trusted certificate.
2. Start the Windows client with `remote_tls_enabled: true`, a valid VPS certificate name, CA certificate, client certificate, and private key.
3. Confirm `/health` reports `remote_tls: true`.
4. Confirm an HTTPS request through `127.0.0.1:8080` returns the destination's HTTPS response.
5. Stop the VPS relay and confirm the Windows client returns 502 instead of direct-connecting.
6. Try an untrusted/expired client certificate and confirm TLS authentication fails.
7. Confirm the VPS rejects non-443 CONNECT requests and private/reserved target addresses.
8. Verify destination DNS is resolved by the VPS resolver; review the resolver's privacy and transport properties.
9. Stop the client and server and confirm clean process shutdown.
