#!/bin/sh
set -eu

cd /certs
if [ -f ca.crt ] && [ -f server.crt ] && [ -f server.key ] && [ -f other-ca.crt ]; then
    exit 0
fi

apk add --no-cache openssl >/dev/null

openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days 825 \
    -subj "/CN=MqttExt Local CA" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -keyout ca.key -out ca.crt

openssl req -newkey rsa:3072 -sha256 -nodes -subj "/CN=localhost" -keyout server.key -out server.csr
cat > server.ext <<EOF
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:localhost,DNS:mosquitto,IP:127.0.0.1
EOF
openssl x509 -req -sha256 -days 825 -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
    -extfile server.ext -out server.crt

openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days 825 \
    -subj "/CN=MqttExt Untrusted CA" -keyout other-ca.key -out other-ca.crt

rm -f server.csr server.ext ca.srl other-ca.key
chmod 0644 ca.crt server.crt other-ca.crt server.key
chmod 0600 ca.key
