#!/usr/bin/env bash
# Issue a device certificate signed by the fleet CA.
# Usage: ./device-issue.sh <suffix>  (e.g. ./device-issue.sh a1b2)
set -euo pipefail

SUFFIX="${1:?Usage: $0 <4-hex-suffix>}"
SUFFIX="${SUFFIX,,}"
HOSTNAME="weather-$SUFFIX"
DIR="$(cd "$(dirname "$0")" && pwd)"

CA_KEY="$DIR/ca.key"
CA_CERT="$DIR/ca.crt"
DEV_KEY="$DIR/device.key"
DEV_CSR="$DIR/_device.csr"
DEV_CERT="$DIR/device.crt"
EXT_FILE="$DIR/_device_ext.cnf"

[[ -f "$CA_KEY"  ]] || { echo "ca.key not found. Run ca-create.sh first."; exit 1; }
[[ -f "$CA_CERT" ]] || { echo "ca.crt not found."; exit 1; }

cat > "$EXT_FILE" <<EOF
subjectKeyIdentifier   = hash
authorityKeyIdentifier = keyid,issuer
subjectAltName         = DNS:${HOSTNAME}.local
extendedKeyUsage       = serverAuth
basicConstraints       = critical, CA:FALSE
EOF

openssl ecparam -name prime256v1 -genkey -noout -out "$DEV_KEY"
openssl req -new -key "$DEV_KEY" -out "$DEV_CSR" \
  -subj "/CN=$HOSTNAME"
openssl x509 -req -in "$DEV_CSR" -CA "$CA_CERT" -CAkey "$CA_KEY" \
  -CAcreateserial -out "$DEV_CERT" -days 825 -sha256 -extfile "$EXT_FILE"

rm -f "$DEV_CSR" "$EXT_FILE" "$DIR/ca.srl"

echo ""
echo "Device certificate issued for ${HOSTNAME}.local"
echo "  Key  : $DEV_KEY"
echo "  Cert : $DEV_CERT"
echo "Run: ./device-provision.sh $SUFFIX /dev/ttyUSB0"
