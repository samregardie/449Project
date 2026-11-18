#!/bin/sh
# Set up both CAN interfaces, build, and start the cockpit. Run with sudo.
set -e
cd "$(dirname "$0")"

for bus in canA canB; do
  ip link set $bus down
  ip link set $bus type can bitrate 500000 restart-ms 100 # restart-ms: bus-off recovery
  ip link set $bus txqueuelen 4 # short queue: few stale frames when a bus comes back
  ip link set $bus up
done

# Cloud mirror: REDIS_PASSWORD (and optionally REDIS_HOST) from a root-only file,
# so the password is never on the command line. No file = publisher off.
if [ -f /etc/vehicle-pub.env ]; then
  set -a
  . /etc/vehicle-pub.env
  set +a
fi

systemctl stop pigpiod 2>/dev/null || true # pigpio can't start while the daemon runs
sh build.sh
./proxy_receiver
