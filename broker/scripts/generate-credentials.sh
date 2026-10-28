#!/bin/sh
set -eu

cd /secrets
if [ ! -f credentials.txt ]; then
    umask 077
    for user in tester device-sim app-sim; do
        password=$(head -c 48 /dev/urandom | base64 | tr -dc 'A-Za-z0-9' | head -c 32)
        echo "$user:$password" >> credentials.txt
    done
fi

mkdir -p mosquitto
rm -f mosquitto/passwd.tmp
create="-c"
while IFS=: read -r user password; do
    [ -n "$user" ] || continue
    mosquitto_passwd $create -b mosquitto/passwd.tmp "$user" "$password"
    create=""
done < credentials.txt
mv mosquitto/passwd.tmp mosquitto/passwd
chmod 0644 mosquitto/passwd
