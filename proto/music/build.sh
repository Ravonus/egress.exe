#!/bin/sh
# Build the EGM test ROM: compose the data, compile with GBDK-2020 4.5.0. Output: build/egm_test.gbc (+ .noi/.map).
set -e
cd "$(dirname "$0")"
GBDK_HOME="${GBDK_HOME:-$HOME/.cache/gbdk-4.5.0}"
PY="${PY:-../../.venv/bin/python}"
mkdir -p build
"$PY" compose.py build/egm_data.h
"$GBDK_HOME/bin/lcc" -Ibuild -I. -Wm-yC -Wl-j -Wl-m -o build/egm_test.gbc test_rom.c egm.c
"$GBDK_HOME/bin/romusage" build/egm_test.map -g 2>/dev/null | head -20 || true
