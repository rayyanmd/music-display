#!/bin/sh
# One-time: create a self-signed code-signing identity "Music Display Dev" in the login keychain.
# A stable signature keeps macOS Bluetooth/Automation permissions across rebuilds.
set -e
NAME="Music Display Dev"
if security find-identity -p codesigning 2>/dev/null | grep -q "\"$NAME\""; then
  echo "Identity '$NAME' already exists."; exit 0
fi
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cat > "$T/cert.cnf" <<CNF
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = $NAME
[ext]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = critical,codeSigning
CNF
/usr/bin/openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -config "$T/cert.cnf" \
  -keyout "$T/key.pem" -out "$T/cert.pem" 2>/dev/null
PASS=musicdisplay
/usr/bin/openssl pkcs12 -export -inkey "$T/key.pem" -in "$T/cert.pem" -name "$NAME" \
  -out "$T/id.p12" -passout pass:$PASS 2>/dev/null
security import "$T/id.p12" -k "$HOME/Library/Keychains/login.keychain-db" -P $PASS -T /usr/bin/codesign
security find-identity -p codesigning | grep "$NAME"
