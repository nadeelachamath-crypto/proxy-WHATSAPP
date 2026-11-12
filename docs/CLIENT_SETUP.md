# Windows client setup

## 1. Create a private client identity on Windows

Install the OpenSSL command-line tools on the Windows PC and ensure `openssl.exe` is on `PATH`. The proxy itself links OpenSSL statically; the command-line tool is needed only to create the client private key and CSR. From the project/package root, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\create-client-csr.ps1
```

This creates:

- `certs/client.key` — private key. Keep it on the Windows PC; never upload it to the VPS.
- `certs/client.csr` — certificate signing request. This is safe to transfer to the VPS for signing.

Upload only `client.csr` to the VPS using an authenticated encrypted file-transfer method.

## 2. Have the VPS sign the CSR

On the VPS, from the project root:

```bash
./server/scripts/sign-client-csr.sh /path/to/client.csr certs/client.crt
```

Transfer `certs/client.crt` and `certs/ca.crt` back to the Windows PC. Do not transfer `certs/ca.key` or `certs/server.key`.

## 3. Configure the client

Create `config/client.json` from `config/client.example.json` and edit the values below. Remote TLS is the default; if a required VPS/certificate setting is missing, startup fails rather than silently switching to direct mode. Use direct mode only for local diagnostics by explicitly setting `remote_tls_enabled` to `false`.

- `remote_host`: preferably the numeric public IP of the VPS. This avoids a local DNS lookup for the VPS hostname.
- `remote_server_name`: DNS name in the VPS server certificate's subjectAltName.
- `remote_port`: same as the VPS listener (default 8443).
- `ca_certificate`: `certs/ca.crt`.
- `client_certificate`: `certs/client.crt`.
- `client_private_key`: `certs/client.key`.
- `remote_tls_enabled`: keep `true`.

Use forward slashes in JSON paths, e.g. `certs/client.key`, or correctly escape Windows backslashes. Do not set `allow_non_443_ports` to true in remote mode.

## 4. Start and test

Console:

```powershell
.\WAProxy.exe --config .\config\client.json
```

GUI:

```powershell
.\WAProxyGUI.exe
```

In the GUI, click **Start Proxy**. The application binds only to `127.0.0.1:8080`.

Test the health endpoint in another terminal:

```powershell
curl.exe -i http://127.0.0.1:8080/health
```

Test a site:

```powershell
curl.exe -v -x http://127.0.0.1:8080 https://example.com/
```

A successful request should show the local CONNECT response `200 Connection Established` followed by the destination site's HTTPS response.

## 5. Stop

Use the GUI's **Stop Proxy** button, or press Ctrl+C in the console window. If the VPS/TLS connection fails while remote mode is enabled, WA Proxy returns 502 and does not direct-connect to the destination.
