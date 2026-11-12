# Linux VPS setup

This component is a TCP relay for a VPS you own or are authorized to administer. Use a supported Ubuntu LTS release, keep the OS/OpenSSL patched, and expose only the TLS listener and SSH in the VPS firewall.

## 1. Install dependencies and build

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libssl-dev openssl ufw
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target WAPServer --parallel
```

## 2. Create the private CA and VPS certificate

Choose a DNS name that resolves to this VPS public IP and use the same name for the server certificate:

```bash
./server/scripts/create-server-pki.sh proxy.example.net
```

The script creates `certs/ca.crt`, `certs/ca.key`, `certs/server.crt`, and `certs/server.key`. The VPS needs `server.crt`, `server.key`, and `ca.crt` (the CA certificate used to validate Windows client certificates). The CA private key is only used to sign authorized client CSRs. Keep it protected and move it offline after issuance.

## 3. Sign the Windows client's CSR

On Windows, generate a private key and CSR with the packaged `create-client-csr.ps1` script. Transfer **only** `client.csr` to the VPS using authenticated encrypted file transfer. On the VPS, while the CA private key is available:

```bash
./server/scripts/sign-client-csr.sh /secure/upload/client.csr certs/client.crt
```

Transfer `certs/client.crt` and `certs/ca.crt` back to Windows. Never copy `client.key` to the VPS, and never distribute `ca.key`.

## 4. Install the relay as an unprivileged service

Create a dedicated account and directories:

```bash
sudo useradd --system --no-create-home --home-dir /opt/waproxy --shell /usr/sbin/nologin waproxy
sudo install -d -o root -g root -m 755 /opt/waproxy /opt/waproxy/bin /opt/waproxy/config
sudo install -d -o root -g waproxy -m 750 /opt/waproxy/certs
sudo install -o root -g root -m 755 build/WAPServer /opt/waproxy/bin/WAPServer
sudo install -o root -g waproxy -m 640 certs/server.crt /opt/waproxy/certs/server.crt
sudo install -o root -g waproxy -m 640 certs/server.key /opt/waproxy/certs/server.key
sudo install -o root -g waproxy -m 640 certs/ca.crt /opt/waproxy/certs/ca.crt
sudo install -o root -g waproxy -m 640 config/server.example.json /opt/waproxy/config/server.json
```

Edit `/opt/waproxy/config/server.json` so the three certificate paths point to `/opt/waproxy/certs/`. The example listens on `0.0.0.0:8443`; change it only if you deliberately choose another interface/port. Check that `waproxy` can read the certs but cannot modify the keys.

## 5. Configure the firewall

Make sure the SSH rule matches your actual SSH port before enabling UFW:

```bash
sudo ufw allow 22/tcp
sudo ufw allow 8443/tcp
sudo ufw enable
sudo ufw status verbose
```

Do not expose the Windows local proxy port (8080) on the VPS or on the Windows LAN. The Windows client binds only to `127.0.0.1`.

## 6. Test the server in the foreground

Run it as the unprivileged account:

```bash
sudo -u waproxy /opt/waproxy/bin/WAPServer --config /opt/waproxy/config/server.json
```

Stop with Ctrl+C. The server requires a valid client certificate, resolves destination hostnames using the VPS system resolver, permits TCP port 443 only, and rejects private/local/link-local/special-use target IPs. It does not act as a generic port forwarder.

## 7. Enable systemd

Copy the included service unit and start it:

```bash
sudo install -o root -g root -m 644 server/waproxy.service /etc/systemd/system/waproxy.service
sudo systemctl daemon-reload
sudo systemctl enable --now waproxy
sudo systemctl status waproxy --no-pager
sudo journalctl -u waproxy -f
```

The unit runs as `waproxy`, uses a read-only system view, and logs to journald. It does not need root privileges or Linux capabilities.

## 8. Confirm the route

Configure the Windows client for mutual-TLS mode and request an HTTPS site through `127.0.0.1:8080`. The client should report `remote_tls: true` in `/health`. The VPS log records the client's peer address and byte counts, but deliberately omits the target hostname.

To verify fail-closed behavior, stop the VPS service and repeat the request. The local proxy must return HTTP 502; it must not fall back to direct destination connections.

## DNS and privacy note

Destination names are resolved by the VPS operating system resolver. This prevents the Windows PC from making those destination DNS lookups when the app uses this proxy, but the VPS resolver/provider may see them and the lookup may not be encrypted upstream. A future configurable DNS-over-TLS/HTTPS resolver is not included in this release.
