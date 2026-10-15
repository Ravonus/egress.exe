#!/bin/sh
# Build one v2 config: build2.sh W H SCENE [extra lcc -D flags]  ->  build/r2_<W>x<H>_s<SCENE><SUFFIX>.gbc + host2_<W>x<H>
set -e
cd "$(dirname "$0")"
W=$1; H=$2; S=$3; shift 3
GBDK_HOME=${GBDK_HOME:-$HOME/.cache/gbdk-4.5.0}
LCC=$GBDK_HOME/bin/lcc
PY=../../../.venv/bin/python
TAG=${W}x${H}_s${S}${SUFFIX:-}
B=build/$TAG
mkdir -p "$B"
$PY gen2.py "$B" --w "$W" --h "$H" ${FRFLAG:-} >/dev/null
OBJ=0; [ "$S" != 0 ] && [ "$S" != 4 ] && OBJ=1
[ -n "$OBJFORCE" ] && OBJ=$OBJFORCE
DEFS="-DVIEW_W=$W -DVIEW_H=$H -DSCENE=$S -DOBJ_ON=$OBJ"
CF="-c -DCAST_ASM -DWALLS_ASM -DFLOORS_ASM -Isrc -I$B -Wf--disable-warning -Wf110 -Wf--disable-warning -Wf126 -Wf--max-allocs-per-node50000 $DEFS $*"
$LCC $CF -o "$B/main2.o" src/main2.c
$LCC $CF -o "$B/core2.o" src/core2.c
$LCC $CF -o "$B/tables2.o" "$B/tables2.c"
$LCC -c -o "$B/tables2_abs.o" "$B/tables2_abs.s"
OBJS="$B/main2.o $B/core2.o"
if [ "$OBJ" = 1 ]; then
  $LCC $CF -o "$B/obj2.o" src/obj2.c
  $LCC $CF -Wf-bo2 -o "$B/spr2.o" "$B/spr2.c"
  OBJX="$B/obj2.o $B/spr2.o"
fi
{ echo "VOFF = $((255 - (H + 255) / 2))"; echo "VEND = $((255 - (H + 255) / 2 + H))"; echo "STRIP = $((H * 2))";
  echo "NRAYS = $((W / 4))"; echo "VIEW_H = $H"; echo "TW = $((W / 8))"; cat "$B/layout.inc" src/fill2.s; } > "$B/fill2.s"
$LCC -c -o "$B/fill2.o" "$B/fill2.s"
{ echo "NRAYS = $((W / 4))"; echo "	.area _CODE"; cat src/cast2.s; } > "$B/cast2.s"
$LCC -c -o "$B/cast2.o" "$B/cast2.s"
$LCC -c -o "$B/bank2.o" "$B/bank2_walls.s"
$LCC -c -o "$B/bank3.o" "$B/bank3_floors.s"
$LCC -c -o "$B/bank4.o" "$B/bank4_walls1.s"
OBJS="$B/main2.o $B/fill2.o $B/cast2.o $B/core2.o ${OBJX:-} $B/tables2.o $B/tables2_abs.o $B/bank2.o $B/bank3.o $B/bank4.o"
$LCC -Wm-yC -Wm-yt0x19 -Wm-yo8 -Wm-yn"RAYCAST2" -Wl-j -Wl-m -o "build/r2_$TAG.gbc" $OBJS
cc -O2 -std=c99 -Wall -Wextra -Isrc -I$B $DEFS ${HOSTDEFS:-} -o "build/host2_${W}x${H}" src/host2.c src/core2.c "$B/tables2.c"
ls -l "build/r2_$TAG.gbc" | awk '{print $5, $9}'
