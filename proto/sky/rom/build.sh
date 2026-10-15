#!/bin/sh
# Build the sky cost probe ROM: build/sky_probe.gbc (+ .map/.noi). Tables come from ../gen_tables.py (out/).
# Layout as in the game: the engine (sky.c, its tables and the stream descriptors) in one switchable bank (1 here),
# the copy helper in bank 0, the event streams in banks 8+.
set -e
cd "$(dirname "$0")/.."
GBDK_HOME=${GBDK_HOME:-$HOME/.cache/gbdk-4.5.0}
LCC=$GBDK_HOME/bin/lcc
B=build
mkdir -p $B
CF="-c -I. -Iout -Wf--disable-warning -Wf110 -Wf--disable-warning -Wf126"
$LCC $CF -o $B/main.o rom/main.c
$LCC $CF -Wf-bo1 -o $B/sky.o sky.c
$LCC $CF -Wf-bo1 -o $B/sky_tab.o out/sky_tab.c
$LCC $CF -Wf-bo1 -o $B/sky_ev.o out/sky_ev.c
$LCC $CF -o $B/sky_bank0.o sky_bank0.c
$LCC -c -o $B/sky_mul.o sky_mul.s
OBJS="$B/main.o $B/sky.o $B/sky_bank0.o $B/sky_mul.o $B/sky_tab.o $B/sky_ev.o"
for f in out/sky_ev_b*.c; do n=$(basename $f .c); $LCC $CF -o $B/$n.o $f; OBJS="$OBJS $B/$n.o"; done
$LCC -Wm-yC -Wm-yt0x1B -Wm-yo16 -Wm-ya4 -Wm-yn"SKYPROBE" -Wl-j -Wl-m -o $B/sky_probe.gbc $OBJS
ls -l $B/sky_probe.gbc | awk '{print $5, $9}'
