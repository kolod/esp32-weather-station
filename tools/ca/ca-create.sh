#!/usr/bin/env bash
# Create a private CA for the weather station fleet.
# Usage: ./ca-create.sh [OrgName]
set -euo pipefail

ORG="${1:-WeatherStation CA}"
DIR="$(cd "$(dirname "$0")" && pwd)"
KEY="$DIR/ca.key"
CERT="$DIR/ca.crt"
CNF="$DIR/_ca.cnf"

if [[ -f "$KEY" ]]; then
  echo "ca.key already exists — skipping. Delete manually if you want a new CA."
  exit 0
fi

cat > "$CNF" <<EOF
[req]
distinguished_name = dn
x509_extensions    = v3_ca
prompt             = no

[dn]
CN = $ORG
O  = $ORG

[v3_ca]
subjectKeyIdentifier   = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints       = critical, CA:TRUE
keyUsage               = critical, keyCertSign, cRLSign
EOF

openssl ecparam -name prime256v1 -genkey -noout -out "$KEY"
openssl req -new -x509 -key "$KEY" -out "$CERT" -days 7300 -sha256 -config "$CNF"
rm -f "$CNF"

echo ""
echo "CA created:"
echo "  Private key : $KEY   (KEEP PRIVATE)"
echo "  Certificate : $CERT  (install on clients)"
