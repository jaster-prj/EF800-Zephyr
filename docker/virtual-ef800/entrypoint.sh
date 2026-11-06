#!/bin/sh
set -eu

chown -R virtualef800:virtualef800 /var/lib/virtual-ef800
log() {
    printf '%s virtual-ef800: %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" >&2
}

run=0
while true; do
    run=$((run + 1))
    log "starting native_sim (run=$run)"
    set +e
    runuser -u virtualef800 -- /opt/virtual-ef800/zephyr.exe \
        --rt --flash@0=/var/lib/virtual-ef800/ef800-virtual.flash "$@"
    status=$?
    set -e
    if [ "$status" -eq 42 ]; then
        log "native_sim requested controlled restart (run=$run exit=42); restarting in 1 second"
        sleep 1
        continue
    fi
    log "native_sim exited unexpectedly (run=$run exit=$status); container will exit"
    exit "$status"
done
