# Build the Windows and Linux releases on GitHub Actions

The repository includes `.github/workflows/build-release.yml`. It builds two release artifacts:

- `WAProxy-v1.0.0-Windows-x64`: `WAProxy.exe`, `WAProxyGUI.exe`, client configuration example, CSR script and client/security documentation.
- `WAProxy-v1.0.0-Linux-VPS`: Linux `WAPServer`, server configuration example, certificate scripts, systemd unit and VPS/security documentation. The Linux binary is built on Ubuntu 22.04 to remain compatible with Ubuntu 22.04 and 24.04 hosts.

## Publish a build

1. Create or open your private GitHub repository.
2. Extract the complete source ZIP and upload/replace the repository contents, including the hidden `.github` directory. Keep the directory structure unchanged.
3. Commit the changes to the `main` branch.
4. Open the repository's **Actions** tab and select **WA Proxy v1.0 Windows + Linux release**.
5. Open the completed workflow run. Download the Windows and Linux artifacts from its **Artifacts** section.
6. Follow `CLIENT_SETUP.md` and `VPS_PACKAGE_INSTALL.md` from the corresponding downloaded artifact.

The Windows build runs on a GitHub-hosted Windows runner using MSVC and vcpkg OpenSSL. This avoids the need to install CMake/MSBuild on the developer PC. A green Windows job is required before treating the Windows `.exe` files as built and verified.

Do not commit private keys, issued certificates, `config/client.json`, `config/server.json`, or live VPS credentials to GitHub. Commit only the example configuration and source files.
