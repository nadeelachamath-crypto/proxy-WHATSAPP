# Changelog

## 1.0.0 - Integrated release candidate

- Added Windows local HTTPS CONNECT client and Win32 Start/Stop/monitoring GUI.
- Added mutually authenticated TLS transport between the Windows client and Linux VPS relay.
- Added remote hostname resolution on the VPS and TCP destination-port 443 policy.
- Added fail-closed remote mode, local loopback-only binding, connection limits and byte counters.
- Added Linux systemd service sample, certificate/CSR scripts, configuration examples and setup guides.
- Added regression tests for local CONNECT, mTLS authentication, fail-closed behavior and TLS half-close/close-notify.
- Clarified traffic visibility and the distinction between a privacy proxy and WhatsApp traffic classification.
