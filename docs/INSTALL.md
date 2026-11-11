# Installation and first run (Windows)

1. Download and extract the Windows x64 build artifact from GitHub Actions, or build the solution locally.
2. Keep `WAProxy.exe` and its `config` folder together. From the repository root, copy the example configuration:

   ```powershell
   Copy-Item .\config\config.example.json .\config\config.json
   ```

3. Start WA Proxy:

   ```powershell
   .\bin\Release\WAProxy.exe --config .\config\config.json
   ```

4. Confirm it is listening:

   ```powershell
   curl.exe -i http://127.0.0.1:8080/health
   ```

   Test a real HTTPS destination (requires internet access):

   ```powershell
   curl.exe -I --proxy http://127.0.0.1:8080 https://example.com/
   ```

5. In the browser's manual proxy settings, set HTTP proxy host `127.0.0.1` and port `8080`. HTTPS requests use CONNECT through this HTTP proxy. Plain HTTP forwarding is not implemented in v0.2.
6. Stop the proxy with Ctrl+C in its console window.

The application must remain running while the browser uses it. It runs as a normal user and should not require administrator privileges because it binds only to a high localhost port.

## Default port already in use

Change `port` in `config/config.json` to a free port between 1024 and 65535, then use that same port in the browser proxy settings.

## Important v0.2 limitation

This release forwards HTTPS connections directly from the Windows PC to the website. It does not route through a VPS and does not encrypt a separate client-to-VPS transport. It is not a VPN and cannot guarantee destination masking from the ISP.
