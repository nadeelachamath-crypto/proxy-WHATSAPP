# Build instructions

## Dependencies

- C++17 compiler.
- CMake 3.20+.
- OpenSSL 3.x development libraries.
- Threads support.
- Windows: Visual Studio 2026 or Visual Studio 2022 with the C++ workload, Windows SDK, CMake/Ninja tools, and vcpkg.
- Linux VPS: GCC/Clang, CMake, OpenSSL development headers.

## Windows 10/11 with Visual Studio 2026

Use Developer PowerShell for Visual Studio and the Ninja generator for the most compatible setup. If your installed CMake does not list the `Visual Studio 18 2026` generator, do not force it; use Ninja, or install a CMake release that provides that generator.

1. In Visual Studio Installer, install **Desktop development with C++**, MSVC x64/x86 tools, Windows SDK, CMake tools, and Ninja.
2. Install vcpkg and set `VCPKG_ROOT` to its folder.
3. Open Developer PowerShell for Visual Studio 2026 in the project root.
4. Run:

```powershell
$env:VCPKG_ROOT = 'C:\src\vcpkg' # change this to your vcpkg folder
& "$env:VCPKG_ROOT\bootstrap-vcpkg.bat" -disableMetrics
& "$env:VCPKG_ROOT\vcpkg.exe" install openssl:x64-windows-static
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --parallel
```

The Windows build generates `WAProxy.exe` and `WAProxyGUI.exe`. CMake can also be opened as a folder in Visual Studio 2026. If CMake cannot find Ninja, add the Visual Studio CMake/Ninja components or use the GitHub Actions Windows artifact. Direct Visual Studio solution builds use the v145 toolset and the included vcpkg manifest.

To build via CMake's native VS 2026 generator, install CMake 4.2 or newer and run:

```powershell
cmake -S . -B build-vs -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build-vs --config Release --parallel
```

## Linux (VPS)

On Ubuntu 22.04/24.04:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libssl-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target WAPServer --parallel
```

The resulting executable is `build/WAPServer`.

## GitHub Actions builds

Push the complete repository to GitHub. The workflow `.github/workflows/build-release.yml` builds the Windows x64 client/GUI and Linux VPS server, then uploads two artifacts:

- `WAProxy-v1.0.0-Windows-x64`
- `WAProxy-v1.0.0-Linux-VPS`

The Windows workflow installs OpenSSL through vcpkg. It is intended to provide a reproducible Windows build without requiring MSBuild or CMake to be installed on your own PC. The Linux workflow uses Ubuntu's OpenSSL development package.

## Visual Studio solution

`WAProxy.sln` contains the client console and GUI projects. The projects use the `VCPKG_ROOT` environment variable and the `x64-windows-static` triplet for OpenSSL. CMake is the canonical build definition and is what GitHub Actions uses.
