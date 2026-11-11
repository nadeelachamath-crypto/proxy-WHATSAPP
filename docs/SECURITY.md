# Security notes — WA Proxy v0.1

## Security posture and limits

WA Proxy v0.1 is a diagnostic TCP listener, not a functioning privacy proxy. It does not route or encrypt user traffic and must not be presented as hiding destinations from an ISP. It does not alter DNS, spoof traffic labels, impersonate WhatsApp, or imitate another service's protocol.

## Implemented safeguards

- Listens only on IPv4 loopback `127.0.0.1`; configuration validation rejects any other address.
- Uses an unprivileged default port (`8080`).
- Limits request size, request duration, and concurrent worker count.
- Closes each client socket after one response.
- Handles normal Ctrl+C / Ctrl+Break shutdown and joins worker threads.
- Does not write request headers or body content to logs.
- Sets `Connection: close`, `Cache-Control: no-store`, and `X-Content-Type-Options: nosniff` on HTTP responses.

## Important limitations

- HTTP on loopback is not TLS. Local processes on the same machine may connect to the diagnostic endpoint.
- The application has no client authentication because v0.1 has no remote listener.
- There is no CONNECT tunnel, upstream proxy, encrypted transport, DNS relay, or kill switch.
- The built-in counters are diagnostic metrics, not proof that traffic is protected.
- The lightweight JSON configuration reader supports the simple key/value format in the supplied example. Use the example structure; this is not a general-purpose JSON parser.

## Safe-use guidance

Keep the listener bound to `127.0.0.1`. Do not change the code to bind to a LAN or public interface as a shortcut. Do not configure sensitive browser traffic to rely on v0.1 for privacy. Stop the program when finished testing.

## Requirements for future stages

Before remote transport is considered usable, later versions must implement authenticated TLS with certificate and hostname validation, a secure client-authentication design, bounded resource use, robust DNS behavior, explicit failure handling, and tests for certificate errors and DNS leakage. Logs must avoid secrets and sensitive browsing metadata. A kill switch and any claims about ISP visibility require independent testing and precise documentation.
