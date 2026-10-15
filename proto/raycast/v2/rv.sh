#!/bin/sh
# quick host re-render of the snapshot views after art/table edits
cd "$(dirname "$0")"
../../../.venv/bin/python gen2.py build/96x64_s0 >/dev/null && cc -O2 -std=c99 -Wall -Wextra -Isrc -Ibuild/96x64_s0 -DVIEW_W=96 -DVIEW_H=64 -o build/host2_96x64 src/host2.c src/core2.c build/96x64_s0/tables2.c && ../../../.venv/bin/python view.py build/host2_96x64 out/snaps out/views.png
