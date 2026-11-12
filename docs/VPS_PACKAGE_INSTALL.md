# Install the prebuilt Linux VPS package

This guide is for the `WAProxy-v1.0.0-Linux-VPS` GitHub Actions artifact. It contains the prebuilt `WAPServer` binary, server example configuration, PKI scripts and systemd unit. For a source build, see `VPS_SETUP.md`.

## 1. Install the package and runtime

On a supported Ubuntu LTS VPS:

```bash
sudo apt-get update
sudo apt-get install -y openssl ufw libssl3
```

Extract the artifact, then install it under `/opt/waproxy`:

```bash
sudo install -d -o root -g root -m 755 /opt/waproxy /opt/waproxy/bin /opt/waproxy/config
sudo install -d -o root -g root -m 755 /opt/waproxy/server/scripts
sudo install -m 755 ./WAPServer /opt/waproxy/bin/WAPServer
sudo install -m 644 ./config/server.example.json /opt/waproxy/config/server.json
sudo install -m 644 ./server/scripts/*.sh /opt/waproxy/server/scripts/
sudo install -m 644 ./server/waproxy.service /etc/systemd/system/waproxy.service
```

Create the service account and certificate directory:

```bash
sudo useradd --system --no-create-home --home-dir /opt/waproxy --shell /usr/sbin/nologin waproxy
sudo install -d -o root -g waproxy -m 750 /opt/waproxy/certs
```

## 2. Create the VPS certificate and private CA

Use a DNS name that points to this VPS and will appear in the server certificate:

```bash
cd /opt/waproxy
sudo bash server/scripts/create-server-pki.sh proxy.example.net
```

Do not put `ca.key` on the Windows PC. Keep it restricted to root while signing the client CSR. The Windows configuration should use the VPS numeric public IP for `remote_host` if you want to avoid a local DNS lookup for the VPS; keep `remote_server_name` set to `proxy.example.net` for certificate validation.

## 3. Sign the Windows client certificate

Generate `client.key` and `client.csr` on Windows using `scripts/create-client-csr.ps1`. Transfer only `client.csr` to the VPS through authenticated encrypted file transfer. On the VPS:

```bash
cd /opt/waproxy
sudo bash server/scripts/sign-client-csr.sh /secure/upload/client.csr certs/client.crt
```

Copy `certs/client.crt` and `certs/ca.crt` to the Windows client. Never copy `client.key` or `ca.key` off their respective machines. After signing, move `certs/ca.key` to protected offline storage and remove it from the VPS.

## 4. Configure permissions and server settings

Edit `/opt/waproxy/config/server.json`. The certificate paths should be:

```json
{
  "bind_address": "0.0.0.0",
  "port": 8443,
  "server_certificate": "certs/server.crt",
  "server_private_key": "certs/server.key",
  "client_ca_certificate": "certs/ca.crt",
  "max_connections": 128,
  "connect_timeout_ms": 8000,
  "tls_handshake_timeout_ms": 10000,
  "tunnel_idle_timeout_ms": 300000
}
```

Ensure the service can read only the runtime certificate files:

```bash
sudo chown root:waproxy /opt/waproxy/certs
sudo chmod 750 /opt/waproxy/certs
sudo chown root:waproxy /opt/waproxy/certs/server.key /opt/waproxy/certs/ca.crt
sudo chmod 640 /opt/waproxy/certs/server.key /opt/waproxy/certs/ca.crt
sudo chown root:root /opt/waproxy/certs/ca.key 2>/dev/null || true
sudo chmod 600 /opt/waproxy/certs/ca.key 2>/dev/null || true
sudo chmod 644 /opt/waproxy/certs/server.crt
```

If you have moved `ca.key` offline, the last two commands harmlessly do nothing. The runtime service does not need `ca.key`.

## 5. Firewall and service

Adjust the SSH port rule to your VPS configuration before enabling UFW:

```bash
sudo ufw allow 22/tcp
sudo ufw allow 8443/tcp
sudo ufw enable
sudo ufw status verbose
```

Start the relay:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now waproxy
sudo systemctl status waproxy --no-pager
sudo journalctl -u waproxy -f
```

The server runs as the unprivileged `waproxy` user. It requires a client certificate, resolves requested destination hostnames on the VPS, permits TCP 443 only, and rejects private/local/link-local/special-use IP destinations. Do not expose the Windows client port 8080 on the VPS.

## 6. Configure the Windows client

On Windows, place `ca.crt`, `client.crt` and the locally generated `client.key` in the paths specified by `config/client.json`. Configure:

- `remote_host`: the VPS public IP (preferred to avoid a local DNS lookup for the VPS name).
- `remote_server_name`: `proxy.example.net`, matching the server certificate.
- `remote_port`: `8443`.
- `remote_tls_enabled`: `true`.

Start WA Proxy and configure the browser to use `127.0.0.1:8080`. Test:

```powershell
curl.exe -v -x http://127.0.0.1:8080 https://example.com/
```

If the VPS is stopped or its certificate is not trusted, the client must return 502 rather than send the destination request directly.
