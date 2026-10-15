#!/bin/sh
# Build one raycaster config: ROM + host checker.  build.sh W H COLW [extra lcc -D flags]
# Output: build/ray_<W>x<H>_c<COLW>.gbc (+ .noi/.map) and build/host_<W>x<H>_c<COLW>
set -e
cd "$(dirname "$0")"
W=$1; H=$2; C=$3; shift 3
GBDK_HOME=${GBDK_HOME:-$HOME/.cache/gbdk-4.5.0}
LCC=$GBDK_HOME/bin/lcc
PY=../../.venv/bin/python
TAG=${W}x${H}_c${C}${SUFFIX:-}
B=build/$TAG
mkdir -p "$B"
$PY gen_tables.py "$B/tables.c" --w "$W" --h "$H" --colw "$C" >/dev/null
DEFS="-DVIEW_W=$W -DVIEW_H=$H -DCOLW=$C"
CF="-c -Isrc -Wf--disable-warning -Wf110 -Wf--disable-warning -Wf126 $DEFS $*"
$LCC $CF -o "$B/main.o" src/main.c
$LCC $CF -o "$B/core.o" src/core.c
$LCC $CF -o "$B/tables.o" "$B/tables.c"
$LCC -c -o "$B/tables_sq.o" "$B/tables_sq.s"
OBJS="$B/main.o $B/core.o $B/tables.o $B/tables_sq.o"
if echo "$*" | grep -q RUN_ASM; then
  if [ "$C" = 4 ]; then MASKS="0xF0, 0x0F"; else MASKS="0xC0, 0x30, 0x0C, 0x03"; fi
  VOFF=$((255 - (H + 255) / 2))
  { echo "VOFF = $VOFF"; echo "VEND = $((VOFF + H))"; echo "STRIP = $((H * 2))"; echo "RPT = $((8 / C))";
    echo "NRAYS = $((W / C))"; echo "DITH_H = $((H / 4))"; echo "	.area _CODE"; echo "MASKS: .db $MASKS";
    cat src/run_asm.s; } > "$B/run_asm.s"
  $LCC -c -o "$B/run_asm.o" "$B/run_asm.s"
  OBJS="$OBJS $B/run_asm.o"
fi
if [ -f src/cast_asm.s ] && echo "$*" | grep -q CAST_ASM; then
  $LCC -c -o "$B/cast_asm.o" src/cast_asm.s
  OBJS="$OBJS $B/cast_asm.o"
fi
$LCC -Wm-yC -Wm-yt0x19 -Wm-yn"RAYCAST" -Wl-j -Wl-m -o "build/ray_$TAG.gbc" $OBJS
cc -O2 -std=c99 -Wall -Wextra -Isrc $DEFS -o "build/host_$TAG" src/host_check.c src/core.c "$B/tables.c"
ls -l "build/ray_$TAG.gbc" | awk '{print $5, $9}'
