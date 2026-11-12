# Changelog

## v0.2.0 — 2026-11-11

- Implemented local HTTP CONNECT tunneling for TCP port 443.
- Added outbound destination resolution and bounded TCP connect timeout.
- Added bidirectional tunnel relay, half-close handling, and idle timeout.
- Kept listener restricted to `127.0.0.1`.
- Added default CONNECT port 443 restriction; non-443 mode exists only for controlled local tests.
- Expanded health output, logs, and smoke test.
- Added Windows GitHub Actions build/test/artifact workflow.
- Remote TLS and Linux VPS remain future milestones (not implemented in this release).

## v0.1.1

- Fixed MSVC C4996 warning in IPv4 peer-address fallback.

## v0.1.0

- Initial loopback diagnostic listener.
