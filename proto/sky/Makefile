# Host builds for the sky engine prototype (not the ROM: rom/build.sh). Tables: python gen_tables.py first.
CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra
SKY = sky.c sky_bank0.c astro.c ../skyscenes/out/meteor_tab.c out/sky_tab.c out/sky_ev.c $(wildcard out/sky_ev_b*.c)
MX = ../matrix/mx_core.c ../matrix/mx_mob.c ../matrix/mx_world.c ../matrix/mx_build.c
all: out/host_dump out/astro_sim
out/host_dump: host_dump.c $(SKY) sky.h
	$(CC) $(CFLAGS) -DSKY_HOST_TEST -I. -Iout -I../skyscenes/out -o $@ host_dump.c $(SKY)
out/astro_sim: astro_sim.c astro.h $(SKY) ../matrix/sim.c
	$(CC) $(CFLAGS) -Wno-unused-function -I. -Iout -I../skyscenes/out -I../matrix -o $@ astro_sim.c $(SKY) $(MX)
.PHONY: all
