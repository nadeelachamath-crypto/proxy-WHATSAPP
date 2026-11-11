# v0.2.0 build verification status

## Verified in this development environment

- GCC 14 Release CMake build: PASS.
- GCC 14 Debug CMake build: PASS.
- Clang 17 Release CMake build: PASS (after clearing two signedness warnings).
- GCC AddressSanitizer + UndefinedBehaviorSanitizer build: PASS.
- CONNECT test returns HTTP 200: PASS.
- Bidirectional byte relay through a local test upstream: PASS.
- Half-close behavior in the test relay: PASS.
- `/health` reports `connect_tunneling: true`: PASS.
- Default non-443 destination port restriction: PASS (HTTP 403).
- Graceful shutdown and log creation: PASS.

## Windows/MSVC status

This environment does not have Visual Studio/MSVC or the Windows SDK, so a native Windows build was not run here. The package includes `.github/workflows/build-windows.yml`, which compiles the solution with MSBuild on a GitHub-hosted Windows runner and runs the same tunnel smoke test. The Windows build should only be marked verified after that workflow is green.

## Smoke test coverage

The Python standard-library test starts a temporary local TCP echo service and WA Proxy on loopback, sends an HTTP CONNECT request, verifies the 200 tunnel response, verifies bytes pass in both directions, checks `/health`, tests the default port policy, and verifies process shutdown and log creation. It does not require external internet access.
