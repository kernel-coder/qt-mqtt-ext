#!/bin/sh
set -eu

install -d -o mosquitto -g mosquitto -m 0700 /run/mosquitto
install -o mosquitto -g mosquitto -m 0600 /mosquitto/secrets/passwd /run/mosquitto/passwd
install -o mosquitto -g mosquitto -m 0600 /mosquitto/config/acl /run/mosquitto/acl
install -o mosquitto -g mosquitto -m 0600 /mosquitto/certs/server.key /run/mosquitto/server.key

exec /usr/sbin/mosquitto -c /mosquitto/config/mosquitto.conf
