# Build guide — WA Proxy v0.1

## Visual Studio 2022 (recommended for Windows)

1. Install Visual Studio 2022 and select **Desktop development with C++**. Include the MSVC v143 x64 build tools and a Windows 10/11 SDK.
2. Open `WAProxy.sln`.
3. Choose `x64` as the solution platform.
4. Choose `Debug` or `Release`.
5. Select **Build → Build Solution**.
6. The executable is written to `bin\Debug\WAProxy.exe` or `bin\Release\WAProxy.exe`.

No OpenSSL dependency is needed in v0.1 because this stage does not perform encryption or TLS. Winsock is provided by Windows and linked with `Ws2_32.lib`.

## CMake

From a Visual Studio Developer PowerShell or a terminal where CMake can find the Visual Studio generator:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Run from the project root:

```powershell
.\build\Release\WAProxy.exe --config .\config\config.example.json
```

CMake builds the same source on Linux for developer smoke testing, but the primary target is Windows 10/11.

## Build configuration

The Visual Studio project includes `Debug|x64` and `Release|x64`, uses the v143 toolset, C++17, warning level 4, and links `Ws2_32.lib`. No administrator privileges are required for the default unprivileged port 8080.
