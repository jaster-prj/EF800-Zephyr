#!/bin/sh
set -eu

chown -R virtualef800:virtualef800 /var/lib/virtual-ef800
while true; do
    set +e
    runuser -u virtualef800 -- /opt/virtual-ef800/zephyr.exe \
        --rt --flash@0=/var/lib/virtual-ef800/ef800-virtual.flash "$@"
    status=$?
    set -e
    if [ "$status" -ne 42 ]; then
        exit "$status"
    fi
    sleep 1
done
