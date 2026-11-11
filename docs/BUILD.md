# Build instructions

## Visual Studio 2022/2026 Developer PowerShell

Install the **Desktop development with C++** workload and the MSVC x64/x86 toolset plus a Windows SDK. The project currently uses the v143 platform toolset; install that component if your Visual Studio installation does not include it.

From the repository root:

```powershell
msbuild .\WAProxy.sln /m /p:Configuration=Release /p:Platform=x64 /verbosity:minimal
```

Expected executable: `bin\Release\WAProxy.exe`.

Debug build:

```powershell
msbuild .\WAProxy.sln /m /p:Configuration=Debug /p:Platform=x64 /verbosity:minimal
```

## CMake

The Windows CMake generator can also build the project when CMake and the Visual Studio C++ tools are installed:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

For newer Visual Studio versions, use the generator listed by `cmake --help`; generator names depend on the installed CMake release.

## GitHub Actions

The repository includes `.github/workflows/build-windows.yml`. Push the source to `main` or manually run **Build WA Proxy Windows x64** from the Actions tab. The workflow compiles the Visual Studio solution, runs the local CONNECT smoke test, and uploads `WAProxy-v0.2.0-Windows-x64`.

## Portable Linux developer test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
python3 scripts/smoke_test.py ./build/WAProxy
```
