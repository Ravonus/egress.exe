#!/bin/sh
# Trick test ROM: tt/build_tt.sh [SCY_DELAY PAL_DELAY]  ->  build/tt/tt_d<SCY>_p<PAL>.gbc (default 0 0 = the shipped timing)
set -e
cd "$(dirname "$0")/.."
GBDK_HOME=${GBDK_HOME:-$HOME/.cache/gbdk-4.5.0}
LCC=$GBDK_HOME/bin/lcc
D=${1:-0}; P=${2:-0}
B=build/tt; mkdir -p $B
N=tt_d${D}_p${P}
rm -f $B/$N.* $B/lcd2_$N.o
{ echo "VIEW_H = 64"; echo "TW = 20"; echo "TH = 8"; echo "OBJ_ON = 0"; echo "DMA_PAD = 0"; echo "TT = 1"; echo "TT_DELAY = $D"; echo "TT_PDELAY = $P"; cat src/lcd2.s; } > $B/lcd2_$N.s
$LCC -c -o $B/lcd2_$N.o $B/lcd2_$N.s
$LCC -c -o $B/tt_main.o tt/tt_main.c
$LCC -Wl-j -Wl-m -Wm-yC -Wm-yt0x1B -Wm-ya1 -Wm-yn"TRICKTEST" -Wl-g.STACK=0xCB00 -o $B/$N.gbc $B/lcd2_$N.o $B/tt_main.o
ls -l $B/$N.gbc | awk '{print $5, $9}'
