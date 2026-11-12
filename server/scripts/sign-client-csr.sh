#!/usr/bin/env bash
set -euo pipefail

CSR="${1:-}"
OUTPUT="${2:-certs/client.crt}"
if [[ -z "$CSR" || ! -f "$CSR" ]]; then
  echo "Usage: $0 CLIENT.csr [output.crt]" >&2
  exit 2
fi
if [[ ! -f certs/ca.crt || ! -f certs/ca.key ]]; then
  echo "Missing certs/ca.crt or certs/ca.key; run create-server-pki.sh first." >&2
  exit 2
fi

mkdir -p "$(dirname "$OUTPUT")"
cat > certs/client.ext <<EXT
basicConstraints=CA:FALSE
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=clientAuth
EXT
openssl x509 -req -in "$CSR" -CA certs/ca.crt -CAkey certs/ca.key \
  -CAcreateserial -out "$OUTPUT" -days 365 -sha256 -extfile certs/client.ext
rm -f certs/client.ext
chmod 644 "$OUTPUT"
echo "Signed client certificate: $OUTPUT"
echo "Copy this certificate and certs/ca.crt to the Windows client. Never copy client.key from Windows to the VPS."
