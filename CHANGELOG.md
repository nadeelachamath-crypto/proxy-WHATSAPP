# Changelog

## 0.1.0 — Initial diagnostic milestone

- Added loopback-only Winsock/POSIX TCP listener.
- Added configurable port, request bounds, connection limit, and log path.
- Added `GET /health` and `GET /` diagnostic endpoints.
- Added explicit HTTP 501 response for CONNECT requests.
- Added connection and byte counters, timestamped logs, and graceful Ctrl+C shutdown.
- Added Visual Studio 2022 x64 Debug/Release project and CMake build.
- Added installation, build, security, and verification documentation plus a smoke-test script.
