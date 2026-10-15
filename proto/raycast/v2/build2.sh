#!/bin/sh
# Build one v2 config: build2.sh W H SCENE [extra lcc -D flags]  ->  build/r2_<W>x<H>_s<SCENE><SUFFIX>.gbc + host2_<W>x<H>_s<SCENE><SUFFIX> (tables differ per stage kind)
set -e
cd "$(dirname "$0")"
W=$1; H=$2; S=$3; shift 3
GBDK_HOME=${GBDK_HOME:-$HOME/.cache/gbdk-4.5.0}
LCC=$GBDK_HOME/bin/lcc
PY=../../../.venv/bin/python
TAG=${W}x${H}_s${S}${SUFFIX:-}
B=build/$TAG
mkdir -p "$B"
OBJ=0; [ "$S" != 0 ] && [ "$S" != 4 ] && OBJ=1
[ -n "$OBJFORCE" ] && OBJ=$OBJFORCE
# gameplay stages (billboards on): the floor rows below mid-depth sample once per 8 rows instead of 4 (-32 lines)
[ "$OBJ" = 1 ] && [ -z "${FRFLAG+x}" ] && FRFLAG="--fr 8 --split 16"
# fullscreen (W 160): H framebuffer rows shown twice each (LY/SCY row doubling) = a 160 x 2H view, HUD below.
# WRAM: framebuffers in banks 2/3 at 0xD000 with their billboard staging (0xDA00) and OAM buffer (0xDE00); map
# 0xCC00, REC 0xCB00, stack below 0xCB00 (bank 0, so switching SVBK never moves it)
FS=0; [ "$W" -ge 160 ] && FS=1
if [ "$FS" = 1 ]; then MAPADDR=0xCC00; STAGEADDR=0xDA00; STAGEMAX=28; VSH=$((H * 2)); VY0=0; OBJT0=160; VS=2
else MAPADDR=0xD800; STAGEADDR=0xC800; STAGEMAX=24; VSH=$H; VY0=40; OBJT0=96; VS=1; fi
$PY gen2.py "$B" --w "$W" --h "$H" --vs $VS ${FRFLAG:-} >/dev/null
[ "$OBJ" = 1 ] && $PY gen_bb.py "$B" --fs $FS >/dev/null
DEFS="-DVIEW_W=$W -DVIEW_H=$H -DSCENE=$S -DOBJ_ON=$OBJ -DFS=$FS"
CF="-c -DCAST_ASM -DWALLS_ASM -DFLOORS_ASM -Isrc -I$B -Wf--disable-warning -Wf110 -Wf--disable-warning -Wf126 -Wf--max-allocs-per-node50000 $DEFS $*"
$LCC $CF -o "$B/main2.o" src/main2.c
$LCC $CF -o "$B/core2.o" src/core2.c
$LCC $CF -o "$B/tables2.o" "$B/tables2.c"
$LCC -c -o "$B/tables2_abs.o" "$B/tables2_abs.s"
OBJS="$B/main2.o $B/core2.o"
OBJA=""; OBJB=""
if [ "$OBJ" = 1 ]; then
  $LCC $CF -o "$B/obj2.o" src/obj2.c
  $LCC $CF -o "$B/bb_data.o" "$B/bb_data.c"
  OBJB="$B/bb_data.o"
  for f in "$B"/bank*_bb.s; do n=$(basename "$f" .s); $LCC -c -o "$B/$n.o" "$f"; OBJB="$OBJB $B/$n.o"; done
  OBJA="$B/obj2.o"
fi
{ echo "OBJ_ON = $OBJ"; echo "MAPADDR = $MAPADDR"; echo "STAGEADDR = $STAGEADDR"; echo "STAGEMAX = $STAGEMAX";
  echo "VSH = $VSH"; echo "VY0 = $VY0"; echo "OBJT0 = $OBJT0"; echo "FS = $FS"; echo "VOFF = $((255 - (H + 255) / 2))"; echo "VEND = $((255 - (H + 255) / 2 + H))"; echo "STRIP = $((H * 2))";
  echo "NRAYS = $((W / 4))"; echo "VIEW_H = $H"; echo "TW = $((W / 8))"; cat "$B/layout.inc" src/fill2.s; } > "$B/fill2.s"
$LCC -c -o "$B/fill2.o" "$B/fill2.s"
{ echo "NRAYS = $((W / 4))"; echo "	.area _CODE"; cat src/cast2.s; } > "$B/cast2.s"
$LCC -c -o "$B/cast2.o" "$B/cast2.s"
LCDO=""
DPAD=1; case " $* " in *NO_DMA_PAD*) DPAD=0;; esac
if [ "$FS" = 1 ]; then { echo "VIEW_H = $H"; echo "TW = $((W / 8))"; echo "TH = $((H / 8))"; echo "OBJ_ON = $OBJ"; echo "DMA_PAD = $DPAD"; cat src/lcd2.s; } > "$B/lcd2.s"; $LCC -c -o "$B/lcd2.o" "$B/lcd2.s"; LCDO="$B/lcd2.o"; fi
$LCC -c -o "$B/bank2.o" "$B/bank2_walls.s"
$LCC -c -o "$B/bank3.o" "$B/bank3_floors.s"
$LCC -c -o "$B/bank4.o" "$B/bank4_walls1.s"
OBJS="$LCDO $B/main2.o $OBJA $B/fill2.o $B/cast2.o $B/core2.o $OBJB $B/tables2.o $B/tables2_abs.o $B/bank2.o $B/bank3.o $B/bank4.o"
LF=""; [ "$FS" = 1 ] && LF="-Wl-g.STACK=0xCB00"
$LCC -Wm-yC -Wm-yt0x19 -Wm-yo${ROMBANKS:-16} -Wm-yn"RAYCAST2" -Wl-j -Wl-m $LF -o "build/r2_$TAG.gbc" $OBJS
cc -O2 -std=c99 -Wall -Wextra -Isrc -I$B $DEFS ${HOSTDEFS:-} -o "build/host2_$TAG" src/host2.c src/core2.c "$B/tables2.c"
ls -l "build/r2_$TAG.gbc" | awk '{print $5, $9}'
# WRAM guard: C data must end below the fixed areas (billboard staging at 0xC800, or REC at 0xCB00 without OBJ)
END=$(grep -oE '[0-9A-F]{8}  s__HEAP\b' "build/r2_$TAG.map" | cut -c1-8)
LIM=CB00; [ "$OBJ" = 1 ] && LIM=C800
[ "$FS" = 1 ] && LIM=C980  # leaves 384 bytes of stack under REC
[ $((0x$END)) -lt $((0x$LIM)) ] || { echo "WRAM overflow: data ends at 0x$END, limit 0x$LIM" >&2; exit 1; }
