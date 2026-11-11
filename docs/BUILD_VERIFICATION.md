# Build and test verification — WA Proxy v0.1.0

Verification performed in the available Linux container on 2026-11-11.

## Completed checks

| Check | Result | Notes |
|---|---|---|
| CMake Release build | PASS | GCC 14.2, C++17, POSIX socket compatibility path |
| CMake Debug build | PASS | GCC 14.2, C++17 |
| CMake Release build | PASS | Clang 17, C++17, no compiler warnings after cleanup |
| Smoke test | PASS | Valid `GET /health` JSON; `CONNECT` returns HTTP 501; graceful SIGINT shutdown; log created |
| Sanitizer smoke test | PASS | GCC AddressSanitizer + UndefinedBehaviorSanitizer; leak detection enabled |
| Loopback enforcement | PASS | A config using `0.0.0.0` exits with an error before binding |
| Visual Studio project XML | PASS | `WAProxy.vcxproj` and `.filters` parse as XML |
| Solution/source references | PASS | `.sln` references the included `.vcxproj`; all listed C++ source files exist |

## Windows-specific verification not performed

This environment does not have Windows, Visual Studio 2022, MSBuild, or the Windows SDK. Therefore an actual MSVC/Windows build and Windows runtime test could not be executed here. The solution is configured for Visual Studio 2022 / MSVC v143 / x64 with Debug and Release configurations, C++17, and `Ws2_32.lib`, but this is not a substitute for a real Windows build.

To complete that verification, open `WAProxy.sln` on Windows in Visual Studio 2022, select `x64` and build both `Debug` and `Release`. Then follow `INSTALL.md` and run `scripts\\smoke_test.py` against the resulting Windows executable.

## Scope reminder

Passing v0.1 tests validates the diagnostic listener only. It does not validate proxy forwarding, TLS, remote server authentication, DNS forwarding, kill-switch behavior, or ISP destination privacy; those features are not implemented in v0.1.
