#!/usr/bin/env bash
set -euo pipefail

SERVER_NAME="${1:-}"
if [[ -z "$SERVER_NAME" ]]; then
  echo "Usage: $0 proxy.example.net" >&2
  exit 2
fi

mkdir -p certs
chmod 700 certs
umask 077

openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out certs/ca.key
openssl req -x509 -new -sha256 -days 3650 -key certs/ca.key \
  -subj "/CN=WA Proxy Private CA" \
  -addext "basicConstraints=critical,CA:TRUE" \
  -addext "keyUsage=critical,keyCertSign,cRLSign" \
  -addext "subjectKeyIdentifier=hash" \
  -out certs/ca.crt

openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out certs/server.key
openssl req -new -key certs/server.key -subj "/CN=${SERVER_NAME}" -out certs/server.csr

if [[ "$SERVER_NAME" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ || "$SERVER_NAME" == *:* ]]; then
  SAN="IP:${SERVER_NAME}"
else
  SAN="DNS:${SERVER_NAME}"
fi
cat > certs/server.ext <<EXT
basicConstraints=CA:FALSE
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=${SAN}
EXT

openssl x509 -req -in certs/server.csr -CA certs/ca.crt -CAkey certs/ca.key \
  -CAcreateserial -out certs/server.crt -days 825 -sha256 -extfile certs/server.ext
rm -f certs/server.csr certs/server.ext
chmod 600 certs/ca.key certs/server.key
chmod 644 certs/ca.crt certs/server.crt

echo "Created private CA and VPS server certificate in ./certs"
echo "Keep certs/ca.key offline after issuing the client certificate."
