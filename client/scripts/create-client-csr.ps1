$ErrorActionPreference = 'Stop'

if (-not (Get-Command openssl -ErrorAction SilentlyContinue)) {
    throw 'OpenSSL command not found. Install OpenSSL or run this from a terminal where openssl.exe is on PATH.'
}

New-Item -ItemType Directory -Force -Path 'certs' | Out-Null
if (Test-Path 'certs/client.key') { throw 'certs/client.key already exists; move it safely before creating a new identity.' }

openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out certs/client.key
if ($LASTEXITCODE -ne 0) { throw 'OpenSSL key generation failed.' }
openssl req -new -key certs/client.key -subj '/CN=WAProxy Windows client' -out certs/client.csr
if ($LASTEXITCODE -ne 0) { throw 'OpenSSL CSR generation failed.' }

# Restrict the private key to the current Windows user and Local System.
$currentUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
& icacls.exe 'certs\client.key' /inheritance:r /grant:r "${currentUser}:F" 'NT AUTHORITY\SYSTEM:F' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Could not restrict client.key permissions with icacls.' }

Write-Host 'Created certs/client.key and certs/client.csr.' -ForegroundColor Green
Write-Host 'Keep client.key on this Windows PC. Upload only client.csr to the VPS for signing.'
