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

rm -rf "$TEMP_DIR"
mkdir -p "$TEMP_DIR/certs"
cp "$CERT_FILE" "$TEMP_DIR/certs/device.crt"
cp "$KEY_FILE"  "$TEMP_DIR/certs/device.key"

python3 - "$TEMP_DIR" "$IMAGE" <<'PYEOF'
import littlefs, os, sys
src, out = sys.argv[1], sys.argv[2]
size, bsize = 0x9E0000, 4096
fs = littlefs.LittleFS(block_size=bsize, block_count=size // bsize)
fs.mkdir('/certs')
for fname in os.listdir(os.path.join(src, 'certs')):
    with open(os.path.join(src, 'certs', fname), 'rb') as f: data = f.read()
    with fs.open('/certs/' + fname, 'wb') as f: f.write(data)
with open(out, 'wb') as f: f.write(bytes(fs.context.buffer))
print(f'Image: {out}')
PYEOF

python3 -m esptool --chip esp32 --port "$PORT" --baud 921600 \
  write-flash $PART_OFFSET "$IMAGE"

rm -rf "$TEMP_DIR"
echo "Device weather-$SUFFIX provisioned on $PORT"
