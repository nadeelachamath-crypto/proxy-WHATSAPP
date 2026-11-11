# Security notes — v0.2.0

## Implemented protections

- Binds only to IPv4 loopback `127.0.0.1`; configuration rejects other listen addresses.
- Uses exclusive-address binding on Windows where supported.
- Default CONNECT destination port is restricted to TCP 443. Non-443 testing is opt-in and off by default.
- Limits concurrent connections and HTTP request-header size.
- Uses request, outbound-connect, and tunnel-idle timeouts.
- Does not log requested destination hostnames; logs only connection outcome and local peer.
- Relays TLS bytes without terminating website TLS.

## Security limitations

- There is no remote VPS or client-to-server TLS transport in v0.2. Direct mode connects from the PC to the requested website.
- DNS resolution is performed by the operating system on the Windows PC. DNS queries may be visible to the configured DNS resolver and network operator.
- The browser's TLS protects application data between browser and website, but does not hide destination IPs or DNS lookups.
- Local processes running as the same user may be able to connect to the proxy. Do not expose the listener on a LAN or public interface.
- No UDP, QUIC, WebRTC proxying, or non-CONNECT HTTP forwarding is included.
- The proxy is a prototype and has not undergone an independent security audit.

## Safe configuration

Keep these settings unless you intentionally need a local test:

```json
{
  "listen_address": "127.0.0.1",
  "port": 8080,
  "allow_non_443_ports": false
}
```

Do not change `listen_address` to `0.0.0.0` or a LAN address. Do not publish the proxy port on a router. Keep Windows Firewall enabled.

## Traffic-label clarification

DNS changes, HTTP headers, port numbers, or protocol imitation cannot make unrelated traffic become genuine WhatsApp traffic. WA Proxy does not attempt protocol impersonation or bypass carrier policy.

## Planned remote mode

The next milestone must add authenticated TLS with strict certificate validation and client authentication. The remote Linux service must not be deployed as an unauthenticated open proxy. DNS behavior needs to be explicit and tested in the remote mode.
