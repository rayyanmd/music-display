#!/usr/bin/env bash
# Detect the LCDwiki 2.8" ESP32-S3 display (ES3C28P/ES3N28P) over USB.
# Prints the serial port(s) and, if esptool is available, chip/flash/PSRAM info.
# Usage: bash find_board.sh [--no-probe]
set -u
PROBE=1; [ "${1:-}" = "--no-probe" ] && PROBE=0

echo "== Espressif USB devices (VID 0x303A) =="
case "$(uname)" in
  Darwin)
    ioreg -p IOUSB -l -w0 2>/dev/null | awk '
      /"USB Product Name"/ {p=$0; sub(/.*= /,"",p)}
      /"USB Vendor Name"/  {v=$0; sub(/.*= /,"",v)}
      /"idVendor" = 12346/ {print "  found: " v " " p}' | sort -u
    PORTS=$(ls /dev/cu.usbmodem* 2>/dev/null) ;;
  Linux)
    lsusb 2>/dev/null | grep -i '303a' | sed 's/^/  found: /'
    PORTS=$(ls /dev/ttyACM* 2>/dev/null) ;;
  *) PORTS="" ;;
esac

if [ -z "${PORTS:-}" ]; then
  echo "  no USB-Serial/JTAG port found."
  echo "  - try another cable (many USB-C cables are power-only)"
  echo "  - hold BOOT, tap RESET, release BOOT, then re-run"
  exit 1
fi
echo "== Serial port(s) =="; for p in $PORTS; do echo "  $p"; done

[ $PROBE -eq 0 ] && exit 0
ESPTOOL=""
for c in esptool esptool.py; do command -v $c >/dev/null 2>&1 && { ESPTOOL=$c; break; }; done
if [ -z "$ESPTOOL" ] && python3 -c "import esptool" 2>/dev/null; then ESPTOOL="python3 -m esptool"; fi
if [ -z "$ESPTOOL" ]; then
  echo "(esptool not installed: 'pipx install esptool' to also read chip/flash/PSRAM info)"; exit 0
fi
PORT=$(echo "$PORTS" | head -1)
echo "== $ESPTOOL flash_id on $PORT (read-only; board resets afterwards) =="
$ESPTOOL --port "$PORT" flash_id 2>&1 | grep -E "Chip type|Chip is|Features|USB mode|MAC|flash size|PSRAM" | sed 's/^/  /'
echo "Expected: ESP32-S3, Embedded PSRAM 8MB, 16MB flash."
