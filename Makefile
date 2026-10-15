# EGRESS.EXE build. `make` builds build/egress.gbc (finished, trimmed, gated); `make test` runs the host tests;
# `make shots` runs the PyBoy harness. Needs GBDK-2020 4.5.0 (GBDK_HOME) and the .venv (pyboy, pillow, numpy).
#
# Two ROMs from the same source; MAPPER picks which one `make` (`make rom`) builds:
#   MAPPER=mbc5 (default)  build/egress.gbc      MBC5 + RAM + battery (0x1B), the set-once VBlank clock
#   MAPPER=mbc3            build/egress-rtc.gbc  MBC3 + timer + RAM + battery (0x10), the RTC clock with the fallback
# `make rtc` builds the RTC ROM, `make all` both. Only the sources that include src/mapper.h are compiled per mapper.
GBDK_HOME ?= $(HOME)/.cache/gbdk-4.5.0
LCC := $(GBDK_HOME)/bin/lcc
PY := .venv/bin/python
HOSTCC ?= cc
HOSTCFLAGS := -O2 -std=c99 -Wall -Wextra -Werror
MAPPER ?= mbc5

SRC := src/main.c src/screens.c src/play.c src/grid.c src/hud.c src/player.c src/actors.c src/sprites.c src/boss.c \
	src/sound.c src/boot/keel_boot.c src/core/eg_room.c src/core/eg_gen.c src/core/eg_piece.c src/core/eg_plan.c \
	src/core/eg_clock.c src/gen/art.c src/gen/art_pics.c \
	src/gen/look_art.c src/look.c src/gen/eg_seeds.c src/puzzle.c \
	src/core/eg_input.c src/core/eg_move.c src/core/eg_idle.c src/heir.c src/gen/heir_tab.c src/gen/heir_banks.c \
	$(sort $(wildcard src/gen/heir_data_*.c))
MAPPER_SRC := src/run.c src/clock.c src/heir_draw.c
# the title (src/title3d, docs/redesign.md 15): its C, its art (committed), the baked walk (generated into build/)
SRC += src/title3d/title3d.c src/title3d/t3d_core.c
T3D_GEN := src/title3d/gen
TB_GEN := build/t3d_bake/gen
TB_HOST := build/t3d_bake/v160x104
-include $(TB_GEN)/chunks.mk
TB_OBJ := $(patsubst %,build/obj/tb/%.o,$(TB_CHUNKS) tb_index)
OBJ := $(patsubst src/%.c,build/obj/%.o,$(SRC)) $(TB_OBJ)
OBJ_mbc5 := $(OBJ) $(patsubst src/%.c,build/obj-mbc5/%.o,$(MAPPER_SRC))
OBJ_mbc3 := $(OBJ) $(patsubst src/%.c,build/obj-mbc3/%.o,$(MAPPER_SRC))
ROM_mbc5 := build/egress.gbc
ROM_mbc3 := build/egress-rtc.gbc
CART_mbc5 := 0x1B
CART_mbc3 := 0x10
DEFS_mbc5 :=
DEFS_mbc3 := -DEG_MBC3
ifeq ($(ROM_$(MAPPER)),)
$(error MAPPER must be mbc5 or mbc3)
endif
CFLAGS := -c -Isrc -Wf--Werror -Wf--disable-warning -Wf110 -Wf--disable-warning -Wf126
# RAM + battery, CGB only, 2 MiB declared (trimmed afterwards), 8 KiB SRAM declared on both mappers. Autobank stops at
# bank 114: bank 115 would end past the 1,900,000-byte gate. MBC3 addresses 128 banks of 16 KiB, so the same gate holds.
LDFLAGS := -Wm-yC -Wm-ya1 -Wm-yo128 -autobank -Wb-ext=.rel -Wb-max=114 -Wl-j -Wl-m
HDRS := $(wildcard src/*.h src/core/*.h src/gen/*.h src/boot/*.h src/title3d/*.h) $(T3D_GEN)/t3d_data.h $(TB_GEN)/tb.h

.SECONDEXPANSION:
.SECONDARY:

rom: $(ROM_$(MAPPER))

all: build/egress.gbc build/egress-rtc.gbc

rtc: build/egress-rtc.gbc

build/obj/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -o $@ $<

build/obj-mbc5/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) $(DEFS_mbc5) -o $@ $<

build/obj-mbc3/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) $(DEFS_mbc3) -o $@ $<

build/obj/sound.o: src/gen/music_data.h

# the heir's baked frame records (tools/bake_heir.py): plain data, no headers but GBDK's
build/obj/gen/heir_data_%.o: src/gen/heir_data_%.c
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -o $@ $<

build/obj/title3d/%.o: CFLAGS += -I$(TB_GEN)

# the title's logo, font and palettes (committed; rebuilt when the tool or its art changes)
$(T3D_GEN)/t3d_data.h: tools/title3d/gen_t3d.py tools/title3d/t3d_art.py
	$(PY) tools/title3d/gen_t3d.py $(T3D_GEN)

# the baked walk: the v2 renderer's C on the host at 160x104, the loops, their per-frame records and unique-tile dictionaries in ROM (and the
# frames the harness checks the cartridge against); see tools/title3d/bake.py and build/t3d_bake/gen/ledger.txt
TB_TOOLS := tools/title3d/bake.py tools/title3d/bake_world.py tools/title3d/gen_t3d.py \
	tools/title3d/t3d_art.py
$(TB_HOST)/bake_host: tools/title3d/bake_host.c proto/raycast/v2/src/core2.c proto/raycast/v2/src/ray2.h \
		tools/title3d/gen_t3d.py tools/title3d/t3d_art.py
	@mkdir -p $(TB_HOST)/gen
	$(PY) tools/title3d/gen_t3d.py $(TB_HOST)/gen --host $(TB_HOST) --view 160 104
	$(HOSTCC) -O2 -std=c99 -Wall -DVIEW_W=160 -DVIEW_H=104 -Iproto/raycast/v2/src -I$(TB_HOST) -o $@ \
		tools/title3d/bake_host.c proto/raycast/v2/src/core2.c $(TB_HOST)/tables2.c
$(TB_GEN)/chunks.mk: $(TB_TOOLS) $(TB_HOST)/bake_host
	$(PY) tools/title3d/bake.py $(TB_GEN) $(TB_HOST)
$(TB_GEN)/tb.h $(TB_GEN)/tb_index.s $(TB_GEN)/tb_l%.s: $(TB_GEN)/chunks.mk
	@:
build/obj/tb/%.o: $(TB_GEN)/%.s
	@mkdir -p $(dir $@)
	$(LCC) -c -o $@ $<
	cp $< $(@:.o=.asm) # beside the object, like SDCC's output, for tools/mapper_audit.py

build/linked-%.gbc: $$(OBJ_$$*)
	$(LCC) $(LDFLAGS) -Wm-yt$(CART_$*) -o $@ $(OBJ_$*)
	@end=$$(grep -oE '[0-9A-F]{8}  s__HEAP ' build/linked-$*.map | head -1 | cut -c5-8); \
	[ $$((0x$$end)) -le $$((0xD000)) ] || { echo "WRAM: game data ends at 0x$$end, past 0xD000 (the 3D title banks \
	0xD000-0xDFFF, src/title3d/t3d.h)" >&2; rm -f $@; exit 1; }

build/egress.gbc: build/linked-mbc5.gbc tools/finish_rom.py
	$(PY) tools/finish_rom.py build/linked-mbc5.gbc build/linked-mbc5.map $@ --mapper mbc5
	cp build/linked-mbc5.noi build/egress.noi
	cp build/linked-mbc5.map build/egress.map
	shasum -a 256 $@

build/egress-rtc.gbc: build/linked-mbc3.gbc tools/finish_rom.py
	$(PY) tools/finish_rom.py build/linked-mbc3.gbc build/linked-mbc3.map $@ --mapper mbc3
	cp build/linked-mbc3.noi build/egress-rtc.noi
	cp build/linked-mbc3.map build/egress-rtc.map
	shasum -a 256 $@

# generated sources (committed; rebuilt when their tools change). The heir bake takes minutes (`make heir`); it reads
# the D3 style and the state table, so a change to either needs a rebake.
heir: tools/bake_heir.py
	$(PY) tools/bake_heir.py src/gen

# generated sources (committed; rebuilt when their tools change)
src/gen/art.c src/gen/art_pics.c src/gen/art.h: tools/bake_art.py
	$(PY) tools/bake_art.py src/gen --sheet docs/shots/art_sheet.png

# the floor's look: the office cards, the biome palettes (tools/bake_look.py borrows bake_art.py's helpers)
src/gen/look_art.c src/gen/look_art.h: tools/bake_look.py tools/bake_art.py
	$(PY) tools/bake_look.py src/gen --sheet docs/shots/b1_look_tiles.png --fragile docs/shots/b1_fragile_tiles.png

src/gen/music_data.h: tools/compose.py
	$(PY) tools/compose.py $@

ROOM_SRC := src/core/eg_room.c src/core/eg_gen.c src/core/eg_piece.c
ROOM_HDR := src/core/eg_room.h src/core/eg_piece.h host/eg_solve.h
build/host/egtool: host/egtool.c host/eg_solve.c $(ROOM_SRC) $(ROOM_HDR)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/egtool.c host/eg_solve.c $(ROOM_SRC)

# The seed table is baked on request only (minutes of solving): `make seeds`. `make test` re-solves every baked seed
# with the current generator, so a generator change without a rebake fails the tests.
seeds: build/host/egtool tools/bake_seeds.sh
	sh tools/bake_seeds.sh build/host/egtool src/gen/eg_seeds.c 128

# the run planner over the baked table, headless (B1 curriculum): `egplan check 1000` is part of `make test`
build/host/egplan: host/egplan.c src/core/eg_plan.c src/core/eg_plan.h src/gen/eg_seeds.c src/gen/eg_seeds.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/egplan.c src/core/eg_plan.c src/gen/eg_seeds.c

build/host/test_room: host/test_room.c host/eg_solve.c $(ROOM_SRC) $(ROOM_HDR)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_room.c host/eg_solve.c $(ROOM_SRC)

# the ported face generator on the host, for the art tools and mocks (tools/avatar_png.py, tools/mock/*)
AVATAR_SRC := src/avatar/crucible_avatar.c src/avatar/crucible_avatar_base.c
build/host/avatar_dump: host/avatar_dump.c $(AVATAR_SRC) $(wildcard src/avatar/*.h)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Wno-unused-function -DAVATAR_HOST -Isrc/avatar -o $@ host/avatar_dump.c $(AVATAR_SRC)

# the B1 moveset and the heir's idle scheduler (src/core: eg_input, eg_move, eg_idle)
build/host/test_input: host/test_input.c src/core/eg_input.c src/core/eg_input.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/test_input.c src/core/eg_input.c

build/host/test_move: host/test_move.c src/core/eg_move.c src/core/eg_move.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/test_move.c src/core/eg_move.c

build/host/test_idle: host/test_idle.c src/core/eg_idle.c src/core/eg_idle.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/test_idle.c src/core/eg_idle.c

test-heir: build/host/test_input build/host/test_move build/host/test_idle
	build/host/test_input
	build/host/test_move
	$(PY) tools/test_idle.py build/host/test_idle

build/host/test_clock: host/test_clock.c src/core/eg_clock.c src/core/eg_clock.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_clock.c src/core/eg_clock.c

# the clock: host rules, then both ROMs in PyBoy (RTC across a power-off, the carry fold, a dead chip, the set-once
# clock, moving a save between the builds) and the mapper-write audit of both links
test-rtc: build/host/test_clock build/egress.gbc build/egress-rtc.gbc
	build/host/test_clock
	$(PY) tools/mapper_audit.py --self-test
	$(PY) tools/mapper_audit.py build/linked-mbc5.map build/linked-mbc3.map
	$(PY) test/rtc_test.py build/egress.gbc build/egress-rtc.gbc

test: build/host/test_room build/host/egplan build/egress.gbc build/egress-rtc.gbc test-rtc test-heir
	build/host/test_room src/gen/eg_seeds.c
	build/host/egplan check 1000
	$(PY) test/harness/title3d.py build/egress.gbc --loops # the title: every loop at 30 fps, VRAM = the bake
	$(PY) tools/test_finish_rom.py build/egress.gbc
	$(PY) tools/test_finish_rom.py build/egress-rtc.gbc

shots: build/egress.gbc build/host/egtool $(TB_GEN)/chunks.mk
	$(PY) test/harness/play.py build/egress.gbc docs/shots
	$(PY) tools/title3d/vs_refs.py docs/shots

clean:
	rm -rf build

.PHONY: rom all rtc test test-rtc test-heir shots clean seeds heir
