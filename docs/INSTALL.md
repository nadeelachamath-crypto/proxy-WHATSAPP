# Installation overview

## Windows client

1. Download the `WAProxy-v1.0.0-Windows-x64` GitHub Actions artifact.
2. Extract it to a folder that is writable by your Windows account.
3. Create `config/client.json` from `config/client.example.json`.
4. Place `ca.crt`, `client.crt`, and `client.key` in the configured certificate paths. Keep `client.key` private.
5. Configure `remote_host` (preferably the VPS numeric IP), `remote_server_name`, and the TLS port.
6. Run `WAProxyGUI.exe`, then click Start Proxy. For terminal use, run `WAProxy.exe --config config/client.json`.
7. Configure the browser or application to use HTTP proxy `127.0.0.1:8080`.

## Linux VPS

1. Download the `WAProxy-v1.0.0-Linux-VPS` artifact or build from source.
2. Install `WAPServer` and create the VPS certificate/CA using `server/scripts/create-server-pki.sh`.
3. Sign the Windows client CSR with `server/scripts/sign-client-csr.sh`.
4. Configure `config/server.json` and paths to the server certificate, private key and client CA.
5. Open the chosen TCP port (default 8443) in the VPS firewall and run WAPServer as an unprivileged service user.

See `CLIENT_SETUP.md` and `VPS_SETUP.md` for complete commands. Do not open the Windows localhost proxy port to the public internet.
