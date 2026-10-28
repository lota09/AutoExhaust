#!/usr/bin/env bash
# Safe OTA flash: compile -> upload -> watch the new image get verified
# (or rolled back by the bootloader) through /api/status.
# Usage: ./ota.sh [ip] [--selftest-fail]
#   --selftest-fail  build that never passes verification (rollback test)

cd "$(dirname "$0")"
IP=192.168.50.161
SELFTEST=0
for a in "$@"; do
  case $a in
  --selftest-fail) SELFTEST=1 ;;
  *) IP=$a ;;
  esac
done

FQBN=esp32:esp32:makergo_c3_supermini:PartitionScheme=min_spiffs
OUT=../../.build/AutoExhaust
VERIFY_WAIT=240 # s, > FW_VERIFY_TIMEOUT_MS + reboot

VERSION="$(git describe --always --dirty 2>/dev/null || echo nogit)-$(date +%m%d%H%M)"
[ $SELFTEST = 1 ] && VERSION="$VERSION-selftest"

status() { curl -s -m 4 "http://$IP/api/status"; }
field() { grep -o "\"$1\":\"[^\"]*\"" | head -1 | cut -d'"' -f4; }

s=$(status)
if [ -z "$s" ]; then
  echo "Device not reachable at $IP"
  exit 1
fi
before=$(echo "$s" | field firmware)
echo "Running : ${before:-unversioned} ($(echo "$s" | field firmware_state))"
echo "New     : $VERSION"

# version.h only exists during this build, so IDE builds stay "dev"
trap 'rm -f version.h' EXIT
{
  echo "#define FW_VERSION \"$VERSION\""
  [ $SELFTEST = 1 ] && echo "#define FW_SELFTEST_FAIL"
} >version.h

echo "== Compile"
if ! arduino-cli compile --fqbn $FQBN --output-dir $OUT . 2>&1 |
  grep -iE "warning|error|바이트|bytes"; then :; fi
[ ${PIPESTATUS[0]} -eq 0 ] || { echo "Compile failed"; exit 1; }

echo "== Upload"
PW=$(sed -n 's/.*SECRET_OTA_PASS *"\(.*\)".*/\1/p' secrets.h)
arduino-cli upload --fqbn $FQBN --protocol network -p "$IP" \
  --upload-field password="$PW" --input-dir $OUT . 2>&1 | tr '\r' '\n' |
  grep -v -e "^$" -e "Uploading:" | tail -2
[ ${PIPESTATUS[0]} -eq 0 ] || { echo "Upload failed (old firmware still running)"; exit 1; }

echo "== Verify (up to ${VERIFY_WAIT}s)"
start=$(date +%s)
seen=0
while (($(date +%s) - start < VERIFY_WAIT)); do
  s=$(status)
  fw=$(echo "$s" | field firmware)
  st=$(echo "$s" | field firmware_state)
  t=$(($(date +%s) - start))
  if [ "$fw" = "$VERSION" ]; then
    [ $seen = 0 ] && echo "[${t}s] New firmware up, $st"
    seen=1
    if [ "$st" = "valid" ]; then
      echo "[${t}s] OK: $VERSION verified"
      exit 0
    fi
  elif [ $seen = 1 ] && [ -n "$fw" ]; then
    echo "[${t}s] ROLLED BACK to $fw ($st)"
    exit 2
  fi
  sleep 5
done
echo "Timeout: running '${fw:-unreachable}' (${st:-?})"
exit 3
