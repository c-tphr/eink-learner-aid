#!/usr/bin/env bash
# Size summary of a PlatformIO firmware build (jobs firmware-picco,
# firmware-other-device and firmware-2slot of .github/workflows/project-ci.yml):
#
#   .github/scripts/firmware-size.sh <env> [build log] [--gate]
#
# Prints PlatformIO's RAM/Flash lines from the build log, the sizes of
# firmware.bin, bootloader.bin and partitions.bin, the ELF section sizes, and
# the headroom of firmware.bin against the two-slot app budget
# (APP_SLOT_BUDGET_BYTES, default 7,431,782 bytes: docs/PLAN.md section 11,
# spike S3). With --gate, an image over the budget fails (exit 1); without it
# the comparison is informational.

set -euo pipefail

ENV_NAME="$1"
LOG="${2:-}"
GATE=0
[[ "${3:-}" == "--gate" ]] && GATE=1
BUDGET="${APP_SLOT_BUDGET_BYTES:-7431782}"
DIR=".pio/build/$ENV_NAME"

echo "== $ENV_NAME"
if [[ -n "$LOG" && -f "$LOG" ]]; then
  grep -E '^(RAM|Flash):' "$LOG" || echo "(no RAM/Flash lines in $LOG)"
fi
for f in firmware.bin bootloader.bin partitions.bin firmware.map; do
  if [[ -f "$DIR/$f" ]]; then
    printf '%-15s %12s bytes\n' "$f" "$(stat -c %s "$DIR/$f")"
  else
    printf '%-15s %12s\n' "$f" "(not built)"
  fi
done
SIZE_TOOL="$(ls "${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-size 2>/dev/null | head -1 || true)"
if [[ -n "$SIZE_TOOL" && -f "$DIR/firmware.elf" ]]; then
  "$SIZE_TOOL" -A "$DIR/firmware.elf" | awk 'NR<=2 || $2 > 0'
fi

APP="$(stat -c %s "$DIR/firmware.bin")"
HEADROOM=$((BUDGET - APP))
printf 'size gate: firmware.bin %d bytes, budget %d bytes, headroom %d bytes (%.1f%% used)\n' \
  "$APP" "$BUDGET" "$HEADROOM" "$(awk -v a="$APP" -v b="$BUDGET" 'BEGIN { print 100 * a / b }')"
if [[ $GATE -eq 1 && $APP -gt $BUDGET ]]; then
  echo "::error::$ENV_NAME firmware.bin is $APP bytes, over the two-slot budget of $BUDGET bytes by $((APP - BUDGET))"
  exit 1
fi
