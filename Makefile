# EGRESS.EXE build. `make` builds build/egress.gbc (finished, trimmed, gated); `make test` runs the host tests;
# `make shots` runs the PyBoy harness. Needs GBDK-2020 4.5.0 (GBDK_HOME) and the .venv (pyboy, pillow, numpy).
GBDK_HOME ?= $(HOME)/.cache/gbdk-4.5.0
LCC := $(GBDK_HOME)/bin/lcc
PY := .venv/bin/python
HOSTCC ?= cc
HOSTCFLAGS := -O2 -std=c99 -Wall -Wextra -Werror

SRC := src/main.c src/screens.c src/play.c src/grid.c src/hud.c src/player.c src/actors.c src/sprites.c src/boss.c \
	src/run.c src/sound.c src/boot/keel_boot.c src/core/eg_room.c src/gen/art.c src/gen/art_pics.c src/gen/eg_seeds.c
OBJ := $(patsubst src/%.c,build/obj/%.o,$(SRC))
CFLAGS := -c -Isrc -Wf--Werror -Wf--disable-warning -Wf110 -Wf--disable-warning -Wf126
# MBC5 + RAM + battery, CGB only, 2 MiB declared (trimmed afterwards), 8 KiB SRAM. Autobank stops at bank 114: bank
# 115 would end past the 1,900,000-byte gate.
LDFLAGS := -Wm-yC -Wm-yt0x1B -Wm-ya1 -Wm-yo128 -autobank -Wb-ext=.rel -Wb-max=114 -Wl-j -Wl-m

all: build/egress.gbc

build/obj/%.o: src/%.c $(wildcard src/*.h src/core/*.h src/gen/*.h src/boot/*.h)
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -o $@ $<

build/obj/sound.o: src/gen/music_data.h

build/linked.gbc: $(OBJ)
	$(LCC) $(LDFLAGS) -o $@ $(OBJ)

build/egress.gbc: build/linked.gbc tools/finish_rom.py
	$(PY) tools/finish_rom.py build/linked.gbc build/linked.map $@
	cp build/linked.noi build/egress.noi
	cp build/linked.map build/egress.map
	shasum -a 256 $@

# generated sources (committed; rebuilt when their tools change)
src/gen/art.c src/gen/art_pics.c src/gen/art.h: tools/bake_art.py
	$(PY) tools/bake_art.py src/gen --sheet docs/shots/art_sheet.png

src/gen/music_data.h: tools/compose.py
	$(PY) tools/compose.py $@

build/host/egtool: host/egtool.c host/eg_solve.c host/eg_solve.h src/core/eg_room.c src/core/eg_room.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/egtool.c host/eg_solve.c src/core/eg_room.c

# The seed table is baked on request only (minutes of solving): `make seeds`. `make test` re-solves every baked seed
# with the current generator, so a generator change without a rebake fails the tests.
seeds: build/host/egtool tools/bake_seeds.sh
	sh tools/bake_seeds.sh build/host/egtool src/gen/eg_seeds.c 128

build/host/test_room: host/test_room.c host/eg_solve.c host/eg_solve.h src/core/eg_room.c src/core/eg_room.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_room.c host/eg_solve.c src/core/eg_room.c

# the ported face generator on the host, for the art tools and mocks (tools/avatar_png.py, tools/mock/*)
AVATAR_SRC := src/avatar/crucible_avatar.c src/avatar/crucible_avatar_base.c
build/host/avatar_dump: host/avatar_dump.c $(AVATAR_SRC) $(wildcard src/avatar/*.h)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Wno-unused-function -DAVATAR_HOST -Isrc/avatar -o $@ host/avatar_dump.c $(AVATAR_SRC)

test: build/host/test_room build/egress.gbc
	build/host/test_room src/gen/eg_seeds.c
	$(PY) tools/test_finish_rom.py

shots: build/egress.gbc build/host/egtool
	$(PY) test/harness/play.py build/egress.gbc docs/shots

clean:
	rm -rf build

.PHONY: all test shots clean seeds
