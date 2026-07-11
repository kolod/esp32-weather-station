#!/usr/bin/env bash
# Flash device cert+key to the storage partition via LittleFS image.
# Usage: ./device-provision.sh <suffix> <port>  (e.g. ./device-provision.sh a1b2 /dev/ttyUSB0)
set -euo pipefail

SUFFIX="${1:?Usage: $0 <suffix> <port>}"
PORT="${2:?Usage: $0 <suffix> <port>}"
DIR="$(cd "$(dirname "$0")" && pwd)"

KEY_FILE="$DIR/device.key"
CERT_FILE="$DIR/device.crt"
TEMP_DIR="$DIR/_provision_tmp"
IMAGE="$DIR/storage.bin"
PART_SIZE=10354688   # 0x9E0000
PART_OFFSET=0x620000

[[ -f "$KEY_FILE"  ]] || { echo "device.key missing. Run device-issue.sh first."; exit 1; }
[[ -f "$CERT_FILE" ]] || { echo "device.crt missing. Run device-issue.sh first."; exit 1; }

# Find mklittlefs
MKLFS=$(command -v mklittlefs 2>/dev/null || \
        find "$IDF_PATH" -name "mklittlefs" -type f 2>/dev/null | head -1)
[[ -n "$MKLFS" ]] || { echo "mklittlefs not found. Ensure ESP-IDF tools are installed."; exit 1; }

rm -rf "$TEMP_DIR"
mkdir -p "$TEMP_DIR/certs"
cp "$CERT_FILE" "$TEMP_DIR/certs/device.crt"
cp "$KEY_FILE"  "$TEMP_DIR/certs/device.key"

"$MKLFS" -c "$TEMP_DIR" -s $PART_SIZE -p 256 -b 4096 "$IMAGE"
echo "Image: $IMAGE"

python "$IDF_PATH/components/esptool_py/esptool/esptool.py" \
  --chip esp32 --port "$PORT" --baud 921600 \
  write_flash $PART_OFFSET "$IMAGE"

rm -rf "$TEMP_DIR"
echo "Device weather-$SUFFIX provisioned on $PORT"
