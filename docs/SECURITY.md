# Security model and limitations

## Transport and authentication

- TLS 1.2 minimum; TLS 1.3 is negotiated where supported by OpenSSL and the peer.
- The Windows client validates the VPS certificate chain against the configured CA and validates the configured `remote_server_name` against the certificate's identity.
- The VPS requires a client certificate signed by the configured client CA (mutual TLS).
- Destination hostnames and the `WAPX/1` CONNECT command are sent only after TLS authentication completes.
- Remote mode is the default and fails closed: VPS/TLS connection or authentication failure returns HTTP 502. There is no automatic direct-mode fallback.
- Direct mode is available only when a config file explicitly sets `remote_tls_enabled` to `false`; it does not hide destinations from the local ISP.

## Destination restrictions

- The Windows listener is restricted to `127.0.0.1`; the config validator rejects other listen addresses.
- The remote relay permits TCP destination port 443 only.
- The VPS resolves destination hostnames and checks every resolved address against private, loopback, link-local, carrier-grade NAT, multicast, documentation, transition and reserved ranges. It connects to the checked address list rather than performing a second DNS lookup for the same request.
- The VPS logs omit destination hostnames and HTTP paths. Operational logs include client peer IPs, outcomes and byte totals.
- Configure the VPS firewall to expose only SSH and the chosen TLS port.

## What network operators can observe

With remote TLS enabled and a correctly configured client, the user's access ISP generally sees a TLS connection to the VPS IP (and may see the VPS hostname in TLS metadata depending on the TLS stack/network), plus timing, duration, packet sizes and total traffic volume. The final destination hostname and its DNS request are carried out of the Windows PC: the hostname is inside the authenticated TLS tunnel, and the VPS performs DNS resolution.

The VPS provider and the DNS resolver configured on that VPS can see destination lookup requests; the VPS can see destination IP addresses. This version uses the VPS operating system resolver, which may send conventional DNS queries unless the VPS administrator has configured an encrypted resolver. A DNS lookup for a VPS hostname occurs locally; using the VPS numeric IP in `remote_host` avoids that lookup.

Browsers/apps that ignore the proxy, browser prefetch, separately configured DoH, other Windows applications, Windows services, IPv6 paths outside the proxy, UDP/QUIC traffic and any traffic not explicitly configured to use the proxy may use other network paths. This is an application-level HTTPS CONNECT proxy, not a full-device VPN or kill switch.

## Important boundaries

WA Proxy is a normal encrypted privacy proxy. It does not impersonate WhatsApp, does not turn other traffic into genuine WhatsApp traffic, and cannot guarantee any particular traffic classification by an ISP. It does not guarantee anonymity or defeat traffic analysis. HTTPS content remains end-to-end encrypted between the browser/application and destination website; WA Proxy does not decrypt website content.

## Key handling and operations

- Keep `client.key`, `server.key` and especially `ca.key` private. The Windows client private key stays on Windows; only its CSR is sent to the VPS for signing.
- Keep the CA private key offline after issuing client certificates. To revoke/replace client trust, rotate the CA and update both sides; this release does not implement CRL/OCSP revocation.
- Renew server and client certificates before expiration.
- Run the Linux service using the provided unprivileged `waproxy` service account/systemd unit.
- Keep OpenSSL and the operating system patched; monitor service logs and connection limits.
- Do not enable non-443 destination ports in remote mode. Do not bind the Windows local proxy to a LAN/public interface.
- The local loopback listener does not authenticate individual local processes; malware or another local account with access to the PC may be able to use it. Protect the Windows account and key files.

## Known limitations

- HTTPS CONNECT over TCP only. General HTTP forward-proxy, SOCKS, UDP, QUIC, kernel-level/full-device VPN, and system-wide routing are not included.
- One TLS connection is opened per CONNECT tunnel; multiplexing is not implemented.
- Remote DNS uses the VPS system resolver. A configurable DNS-over-TLS/HTTPS upstream resolver is not included.
- Certificate issuance/renewal automation and certificate revocation service are not included.
- Windows/MSVC compilation and native GUI behavior require verification via the included Windows GitHub Actions job or on Windows 10/11. Linux compilation does not prove Windows correctness.
