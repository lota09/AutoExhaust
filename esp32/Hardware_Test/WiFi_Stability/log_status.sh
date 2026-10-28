#!/usr/bin/env bash
# Polls the test board's status page and appends snapshots to logs/.
# Usage: ./log_status.sh [ip] [interval_sec]
#   e.g. ./log_status.sh 192.168.50.161 300
# Unreachable polls are logged too, so gaps in the board's own counters
# (e.g. after a reboot) can be cross-checked from the PC side.

IP="${1:-wifitest-c3.local}"
INTERVAL="${2:-300}"
DIR="$(dirname "$0")/logs"
mkdir -p "$DIR"
LOG="$DIR/status_$(date +%Y%m%d_%H%M%S).log"

echo "Logging http://$IP/ every ${INTERVAL}s -> $LOG"
while true; do
  {
    echo "##### $(date '+%Y-%m-%d %H:%M:%S')"
    curl -s -m 10 "http://$IP/" || echo "!!! UNREACHABLE"
    echo
  } >> "$LOG"
  sleep "$INTERVAL"
done
