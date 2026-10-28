#!/usr/bin/env bash
# Boot-connect A/B test: every trial switches profile (= reboot) and records
# how fast the board gets back on WiFi and MQTT.
# Order rotates per round (123, 231, 312, ...) to avoid time-of-day bias.
# Usage: ./boot_test.sh [ip] [rounds]

IP="${1:-192.168.50.161}"
ROUNDS="${2:-5}"
TIMEOUT=120 # s to wait for the board to come back
OUT="$(dirname "$0")/logs/boot_test_$(date +%Y%m%d_%H%M%S).csv"
mkdir -p "$(dirname "$OUT")"

ORDERS=("1 2 3" "2 3 1" "3 1 2")
echo "trial,profile,boot_connect_ms,failed_attempts,web_up_s,mqtt_up_s,rssi_now,rssi_avg,tx_dbm,channel" | tee "$OUT"

field() { echo "$STATUS" | grep "^$1" | head -1; }

trial=0
for ((r = 0; r < ROUNDS; r++)); do
  for p in ${ORDERS[$((r % 3))]}; do
    trial=$((trial + 1))
    curl -s -m 5 "http://$IP/profile?p=$p" > /dev/null
    start=$(date +%s)
    sleep 3 # Let the board actually go down

    # Wait for web server (only starts after WiFi got an IP)
    web_up=""
    while (( $(date +%s) - start < TIMEOUT )); do
      STATUS=$(curl -s -m 3 "http://$IP/")
      if echo "$STATUS" | grep -q "Profile      : P$p"; then
        web_up=$(( $(date +%s) - start )); break
      fi
      sleep 1
    done
    if [[ -z "$web_up" ]]; then
      echo "$trial,P$p,FAIL,,,,,,," | tee -a "$OUT"
      continue
    fi

    # Wait for MQTT (TLS handshake = uplink-heavy traffic)
    mqtt_up=""
    while (( $(date +%s) - start < TIMEOUT )); do
      STATUS=$(curl -s -m 3 "http://$IP/")
      if echo "$STATUS" | grep -q "Connected    : YES"; then
        mqtt_up=$(( $(date +%s) - start )); break
      fi
      sleep 1
    done
    sleep 5 # Collect a few RSSI samples
    STATUS=$(curl -s -m 3 "http://$IP/")

    boot_ms=$(field "Boot connect" | sed -E 's/.*: ([0-9]+) ms.*/\1/')
    fails=$(field "Boot connect" | sed -E 's/.*after ([0-9]+) failed.*/\1/')
    rssi_now=$(field "RSSI" | sed -E 's/.*now (-?[0-9]+),.*/\1/')
    rssi_avg=$(field "RSSI" | sed -E 's/.*avg (-?[0-9.]+).*/\1/')
    tx=$(field "Channel / TX" | sed -E 's/.*\/ ([0-9.]+) dBm.*/\1/')
    ch=$(field "Channel / TX" | sed -E 's/.*: ([0-9]+) \/.*/\1/')

    echo "$trial,P$p,$boot_ms,$fails,$web_up,${mqtt_up:-FAIL},$rssi_now,$rssi_avg,$tx,$ch" | tee -a "$OUT"
  done
done
echo "Saved: $OUT"
