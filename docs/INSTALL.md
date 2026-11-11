# Installation and test guide — WA Proxy v0.1

## 1. Install and configure

1. Build the project using [BUILD.md](BUILD.md), or use the executable produced by your own build.
2. Copy `config\config.example.json` to `config\config.json`.
3. Keep `listen_address` set to `127.0.0.1`. Version 0.1 rejects any other address so the diagnostic listener is not exposed to your LAN.
4. Adjust `port` if 8080 is already in use. Use a port from 1024 through 65535.
5. Run the executable from the project root:

```powershell
.\bin\Release\WAProxy.exe --config .\config\config.json
```

If the configuration file is omitted or not found, built-in defaults are used (`127.0.0.1:8080`). The default log file is `logs\wa-proxy.log`.

## 2. Test the health endpoint

In another PowerShell window:

```powershell
curl.exe -i http://127.0.0.1:8080/health
```

Expected response: HTTP `200 OK`, JSON containing `"version": "0.1.0"`, `"status": "ok"`, and `"connect_tunneling": false`.

## 3. Test the HTTP CONNECT response

```powershell
curl.exe -i -x http://127.0.0.1:8080 https://example.com/
```

Expected response: HTTP `501 Not Implemented`. This is intentional in v0.1. It confirms that the listener recognises CONNECT but does not pretend to establish a tunnel. HTTPS browsing through this program is not supported yet.

You can also send a raw CONNECT request from PowerShell:

```powershell
$tcp = [System.Net.Sockets.TcpClient]::new('127.0.0.1', 8080)
$stream = $tcp.GetStream()
$bytes = [System.Text.Encoding]::ASCII.GetBytes("CONNECT example.com:443 HTTP/1.1`r`nHost: example.com:443`r`n`r`n")
$stream.Write($bytes, 0, $bytes.Length)
$reader = [System.IO.StreamReader]::new($stream)
$reader.ReadToEnd()
$tcp.Close()
```

## 4. Check connection statistics and logs

Call `/health` again. The counters should have changed. Open `logs\wa-proxy.log` to see listener startup, request outcomes, and final counters. v0.1 intentionally does not log HTTP headers, URL paths beyond the fixed endpoint label, or payload data.

## 5. Verify loopback-only binding

Run:

```powershell
Get-NetTCPConnection -State Listen -LocalPort 8080 |
  Select-Object LocalAddress, LocalPort, State
```

`LocalAddress` should be `127.0.0.1`. A listener bound to `0.0.0.0`, a LAN address, or a public address is not expected and should be treated as a configuration/build problem.

## 6. Shutdown

Return to the console running WA Proxy and press **Ctrl+C**. It stops accepting new requests, closes the listening socket, waits for the bounded request workers to finish, prints final counters, and exits. Avoid force-closing the process during routine testing.

## 7. Automated smoke test

A Python 3 smoke test is included:

```powershell
python .\scripts\smoke_test.py .\bin\Release\WAProxy.exe
```

It starts the app with a temporary loopback configuration, checks `/health`, checks that CONNECT returns `501`, and requests graceful shutdown. The script uses only Python's standard library.

## Encrypted remote transport and DNS tests

These tests are **not applicable to v0.1**: there is no remote transport, TLS session, VPS component, DNS forwarding, or upstream connection. They must be added and verified in later milestones before the application is described as a privacy proxy.
