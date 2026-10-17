# EGRESS.EXE build. `make` builds build/egress.gbc (finished, trimmed, gated); `make test` runs host and cartridge checks;
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
	src/core/eg_obst.c src/core/eg_ogen.c src/core/eg_clock.c src/gen/art.c src/gen/art_pics.c \
	src/gen/look_art.c src/look.c src/gen/eg_seeds.c src/puzzle.c \
	src/mob.c src/mob_draw.c src/fx.c src/gen/mob_info.c src/gen/mob_art_0.c src/gen/mob_art_1.c \
	src/obstacle.c src/gen/obstacle_art.c \
	src/stage.c src/stage_spawn.c src/stage_rest.c src/core/eg_stage.c \
	src/core/eg_input.c src/core/eg_move.c src/core/eg_idle.c src/heir.c src/gen/heir_tab.c src/gen/heir_banks.c \
	$(sort $(wildcard src/gen/heir_data_*.c))
# B2 bosses (src/boss_api.h)
SRC += src/boss_core.c src/boss_tenant.c src/boss_fax.c src/boss_art.c src/boss_oam.c
SRC += src/core/eg_physics.c
SRC += src/core/eg_link.c src/core/eg_link_gen.c
SRC += src/core/eg_window.c src/window_fast.c src/window_cache.c
SRC += src/core/eg_view.c src/view_cache.c
SRC += src/gen/world_art.c
SRC += src/world.c src/core/eg_land.c src/core/eg_land_bind.c
SRC += src/world_dead.c src/core/eg_land_dead.c
SRC += src/world_play.c
SRC += src/ui.c
SRC += src/pose_cache.c
SRC += src/core/eg_family.c
SRC += src/items.c
SRC += src/core/eg_combat.c src/core/eg_stamina.c src/stamina.c
SRC += src/core/eg_journal.c
SRC += src/narrator.c src/core/eg_narrator.c src/voice.c
SRC += $(sort $(wildcard src/gen/voice_data_*.c))
MAPPER_SRC := src/run.c src/clock.c src/heir_draw.c src/family.c src/resume.c src/tutorial.c
# the title (src/title3d, docs/redesign.md 15): its C, its art (committed), the baked walk (generated into build/)
SRC += src/title3d/title3d.c src/title3d/t3d_core.c
T3D_GEN := src/title3d/gen
TB_GEN := build/t3d_bake/gen
TB_HOST := build/t3d_bake/v160x104
-include $(TB_GEN)/chunks.mk
TB_OBJ := $(patsubst %,build/obj/tb/%.o,$(TB_CHUNKS) tb_index)
OBJ := $(patsubst src/%.c,build/obj/%.o,$(SRC)) $(TB_OBJ)
# Keep the page-aligned pixel module first, then the fixed upload state before
# banked sources. Clean links must not depend on a previous autobank rewrite.
# The linker also gates
# the shared pose's GDMA alignment and the fixed-WRAM stream packets below.
OBJ_mbc5 := build/obj/main.o build/obj-mbc5/heir_draw.o $(filter-out build/obj/main.o,$(OBJ)) $(patsubst src/%.c,build/obj-mbc5/%.o,$(filter-out src/heir_draw.c,$(MAPPER_SRC)))
OBJ_mbc3 := build/obj/main.o build/obj-mbc3/heir_draw.o $(filter-out build/obj/main.o,$(OBJ)) $(patsubst src/%.c,build/obj-mbc3/%.o,$(filter-out src/heir_draw.c,$(MAPPER_SRC)))
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
# Native simulation/rendering spends the frame budget here. Prefer SDCC's
# faster instruction choices; generated art/data keep their normal build.
HOT_SRC := play player actors grid mob mob_draw sprites look stage fx heir hud puzzle world_play core/eg_input core/eg_move core/eg_combat core/eg_stamina
$(addprefix build/obj/,$(addsuffix .o,$(HOT_SRC))): CFLAGS += -Wf--opt-code-speed
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
build/obj/items.o: src/gen/item_idle.inc
build/obj/sprites.o: src/gen/diag_arc.inc src/gen/hammer.inc

src/gen/hammer.inc: tools/bake_hammer.py
	$(PY) tools/bake_hammer.py
build/obj/grid.o: src/overlap.inc

build/overlap-check/overlap.gbc: test/native/overlap.c src/overlap.inc
	@mkdir -p $(dir $@)
	$(LCC) -Isrc -Wm-yC -o $@ $<

test-native-overlap: build/overlap-check/overlap.gbc
	$(PY) tools/test_native_overlap.py $<

src/gen/item_idle.inc: tools/bake_item_idle.py tools/bake_look.py
	$(PY) tools/bake_item_idle.py

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
$(TB_HOST)/bake_host: tools/title3d/bake_host.c tools/title3d/core/core2.c tools/title3d/core/ray2.h \
		tools/title3d/gen_t3d.py tools/title3d/t3d_art.py
	@mkdir -p $(TB_HOST)/gen
	$(PY) tools/title3d/gen_t3d.py $(TB_HOST)/gen --host $(TB_HOST) --view 160 104
	$(HOSTCC) -O2 -std=c99 -Wall -DVIEW_W=160 -DVIEW_H=104 -Itools/title3d/core -I$(TB_HOST) -o $@ \
		tools/title3d/bake_host.c tools/title3d/core/core2.c $(TB_HOST)/tables2.c
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
	[ $$((0x$$end)) -le $$((0xD800)) ] || { echo "WRAM: game data ends at 0x$$end, past 0xD800 (the title mirrors \
	0xD000-0xD7FF and keeps its buffers above it, src/title3d/t3d.h)" >&2; rm -f $@; exit 1; }
	@$(PY) -c 'import re; from pathlib import Path; s=Path("build/linked-$*.noi").read_text(); sizes={"eg_window_packet":9,"world_mask_packet":9,"world_pre_x":9,"world_pre_y":9,"eg_stage_work":36,"heir_cache_enabled":1,"heir_pixels":128}; addresses={n:int(re.search(r"DEF _"+n+r" 0x([0-9A-Fa-f]+)",s)[1],16) for n in sizes}; assert all(0xC000<=a and a+sizes[n]<=0xD000 for n,a in addresses.items()),{n:hex(a) for n,a in addresses.items()}; assert not(addresses["heir_pixels"]&255),hex(addresses["heir_pixels"])'

# GBDK's autobanker rewrites shared .rel inputs. Two mapper links must never run
# concurrently, even under make -j; compilation can still run in parallel.
build/linked-mbc3.gbc: | build/linked-mbc5.gbc

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
	$(PY) tools/bake_look.py src/gen --sheet docs/shots/b1_look_tiles.png --fragile docs/shots/b1_fragile_tiles.png --items docs/shots/b1_items_tiles.png

# B2 stages: the temp and the shopkeeper (BG cards, src/stage.c)
src/gen/stage_art.h: tools/bake_stage.py
	$(PY) tools/bake_stage.py src/gen --sheet docs/shots/b2_stage_cards.png

src/gen/obstacle_art.c src/gen/obstacle_art.h: tools/bake_obstacles.py tools/mock/obstacle_mock.py tools/mock/mobgen.py
	$(PY) tools/bake_obstacles.py
# B2 bosses: THE TENANT and LAST FAX (tools/bake_boss.py borrows mobgen's shading)
src/gen/boss_art.h src/gen/boss_art_data.h: tools/bake_boss.py tools/mock/mobgen.py
	$(PY) tools/bake_boss.py src/gen --sheet docs/shots/b2_boss_art.png

src/gen/music_data.h: tools/compose.py
	$(PY) tools/compose.py $@

ROOM_SRC := src/core/eg_room.c src/core/eg_link.c src/core/eg_link_gen.c src/core/eg_gen.c src/core/eg_piece.c src/core/eg_physics.c src/core/eg_obst.c src/core/eg_ogen.c host/eg_osolve.c
ROOM_HDR := src/core/eg_room.h src/core/eg_piece.h host/eg_solve.h src/core/eg_obst.h host/eg_osolve.h
build/host/egtool: host/egtool.c host/eg_solve.c host/eg_walls.c $(ROOM_SRC) $(ROOM_HDR)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/egtool.c host/eg_solve.c host/eg_walls.c $(ROOM_SRC)

# Baking needs a deterministic search bound per candidate. The accepted rooms
# retain the same difficulty/fairness bands and are rechecked with the full solver.
build/host/egtool-bake: host/egtool.c host/eg_solve.c host/eg_walls.c $(ROOM_SRC) $(ROOM_HDR)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -DBUDGET=5000 -DEG_OB_CAP=512000 -o $@ host/egtool.c host/eg_solve.c host/eg_walls.c $(ROOM_SRC)

# The seed table is baked on request only (minutes of solving): `make seeds`. `make test` re-solves every baked seed
# with the current generator, so a generator change without a rebake fails the tests.
seeds: build/host/egtool-bake build/host/egtool tools/bake_seeds.sh tools/seed_curriculum.txt
	sh tools/bake_seeds.sh build/host/egtool-bake src/gen/eg_seeds.c 128 1 build/host/egtool

# the run planner over the baked table, headless (B1 curriculum) and the full ~52-floor layout (3.6): `egplan check 1000`
# is part of `make test`
build/host/egplan: host/egplan.c host/egrun.c host/egstage.c src/core/eg_plan.c src/core/eg_plan.h src/core/eg_stage.c \
		src/core/eg_stage.h src/core/eg_room.c src/core/eg_link.c src/gen/eg_seeds.c src/gen/eg_seeds.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/egplan.c host/egrun.c host/egstage.c src/core/eg_plan.c src/core/eg_stage.c \
		src/core/eg_room.c src/core/eg_link.c src/gen/eg_seeds.c -lm

# B2 living obstacles: the core rules and the tick solver against the mock room (tools/mock/obstacle_room.py)
build/host/test_obst: host/test_obst.c $(ROOM_SRC) $(ROOM_HDR)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_obst.c $(ROOM_SRC)

build/host/test_room: host/test_room.c host/eg_solve.c $(ROOM_SRC) $(ROOM_HDR)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_room.c host/eg_solve.c $(ROOM_SRC)

build/host/test_physics: host/test_physics.c host/eg_solve.c $(ROOM_SRC) $(ROOM_HDR) src/core/eg_physics.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_physics.c host/eg_solve.c $(ROOM_SRC)

test-physics: build/host/test_physics
	build/host/test_physics

build/host/test_family: host/test_family.c src/core/eg_family.c src/core/eg_family.h src/core/eg_input.c src/core/eg_input.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_family.c src/core/eg_family.c src/core/eg_input.c

test-family: build/host/test_family
	build/host/test_family

build/host/test_interactions: host/test_interactions.c src/core/eg_combat.c $(ROOM_SRC) $(ROOM_HDR)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_interactions.c src/core/eg_combat.c $(ROOM_SRC)

test-interactions: build/host/test_interactions
	build/host/test_interactions

build/host/test_curriculum: host/test_curriculum.c src/core/eg_family.c src/core/eg_plan.c src/core/eg_stage.c src/core/eg_room.c src/core/eg_link.c src/gen/eg_seeds.c $(HDRS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_curriculum.c src/core/eg_family.c src/core/eg_plan.c src/core/eg_stage.c src/core/eg_room.c src/core/eg_link.c src/gen/eg_seeds.c

build/host/test_run_stream: host/test_run_stream.c src/core/eg_plan.c src/core/eg_stage.c src/core/eg_room.c src/core/eg_link.c src/gen/eg_seeds.c $(HDRS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc/core -o $@ host/test_run_stream.c src/core/eg_plan.c src/core/eg_stage.c src/core/eg_room.c src/core/eg_link.c src/gen/eg_seeds.c

test-run-stream: build/host/test_run_stream
	$<

build/host/run_stream_dump: host/run_stream_dump.c test/native/run_cases.h src/core/eg_plan.c src/core/eg_stage.c src/core/eg_room.c src/core/eg_link.c src/gen/eg_seeds.c $(HDRS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/run_stream_dump.c src/core/eg_plan.c src/core/eg_stage.c src/core/eg_room.c src/core/eg_link.c src/gen/eg_seeds.c

build/run-check/test.o: test/native/run_stream.c test/native/run_cases.h $(HDRS)
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -o $@ $<

build/run-check/plan.o: src/core/eg_plan.c $(HDRS)
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -DEG_FULL_RUN -o $@ $<

build/run-check/run.gbc: build/run-check/test.o build/run-check/plan.o build/cache-check/cache.o build/obj/core/eg_stage.o build/obj/core/eg_room.o build/obj/core/eg_link.o build/obj/core/eg_piece.o build/obj/gen/eg_seeds.o
	$(LCC) $(LDFLAGS) -Wm-yt0x1B -o $@ $^

test-native-run-stream: build/host/run_stream_dump build/run-check/run.gbc
	$(PY) tools/test_run_stream.py $^

build/host/move_stream_dump: host/move_stream_dump.c test/native/move_cases.h src/core/eg_move.c src/core/eg_move.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/move_stream_dump.c src/core/eg_move.c

build/move-check/test.o: test/native/move_stream.c test/native/move_cases.h src/core/eg_move.h
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -o $@ $<

build/move-check/move.gbc: build/move-check/test.o build/obj/core/eg_move.o
	$(LCC) $(LDFLAGS) -Wm-yt0x1B -o $@ $^

test-native-move: build/host/move_stream_dump build/move-check/move.gbc
	$(PY) tools/test_move_stream.py $^

test-camera: all
	$(PY) test/harness/camera_tiles.py build/egress.gbc build/camera-check/mbc5
	$(PY) test/harness/camera_tiles.py build/egress-rtc.gbc build/camera-check/mbc3
	$(PY) test/harness/camera_combat.py build/egress.gbc build/camera-check/combat-mbc5
	$(PY) test/harness/camera_combat.py build/egress-rtc.gbc build/camera-check/combat-mbc3

build/host/test_gen_fingerprint: host/test_gen_fingerprint.c $(ROOM_SRC) $(ROOM_HDR) src/gen/eg_seeds.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc/core -o $@ host/test_gen_fingerprint.c $(ROOM_SRC) src/gen/eg_seeds.c

test-gen-fingerprint: build/host/test_gen_fingerprint
	$<

build/host/test_links: host/test_links.c src/core/eg_room.c src/core/eg_link.c src/core/eg_piece.c $(ROOM_HDR)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_links.c src/core/eg_room.c src/core/eg_link.c src/core/eg_piece.c

test-links: build/host/test_links
	$<

build/host/test_link_gen: host/test_link_gen.c host/eg_solve.c $(ROOM_SRC) $(ROOM_HDR) src/core/eg_physics.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_link_gen.c host/eg_solve.c $(ROOM_SRC)

test-linked-puzzles: build/host/test_link_gen test-links
	$<

build/host/link_stream_dump: host/link_stream_dump.c test/native/link_cases.h src/core/eg_room.c src/core/eg_link.c $(ROOM_HDR)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/link_stream_dump.c src/core/eg_room.c src/core/eg_link.c

build/link-check/test.o: test/native/link_stream.c test/native/link_cases.h $(HDRS)
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -o $@ $<

build/link-check/links.gbc: build/link-check/test.o build/obj/core/eg_room.o build/obj/core/eg_link.o
	$(LCC) $(LDFLAGS) -Wm-yt0x1B -o $@ $^

test-native-links: build/host/link_stream_dump build/link-check/links.gbc
	$(PY) tools/test_link_stream.py $^

test-native-linked-puzzles: all build/host/egtool
	$(PY) test/harness/linked_puzzles.py build/egress.gbc build/link-check/game-mbc5
	$(PY) test/harness/linked_puzzles.py build/egress-rtc.gbc build/link-check/game-mbc3

build/host/test_window: host/test_window.c src/core/eg_window.c src/core/eg_window.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/test_window.c src/core/eg_window.c

test-window: build/host/test_window
	$<

build/host/window_stream_dump: host/window_stream_dump.c test/native/window_cases.h src/core/eg_window.c src/core/eg_window.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/window_stream_dump.c src/core/eg_window.c

build/window-check/test.o: test/native/window_stream.c test/native/window_cases.h $(HDRS)
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -o $@ $<

build/window-check/window.gbc: build/window-check/test.o build/obj/core/eg_window.o build/obj/window_fast.o build/obj/window_cache.o build/obj/core/eg_view.o build/obj/view_cache.o
	$(LCC) $(LDFLAGS) -Wm-yt0x1B -o $@ $^

test-native-window: build/host/window_stream_dump build/window-check/window.gbc
	$(PY) tools/test_window_stream.py $^

src/gen/stage_codec.inc: assets/stages/codec.json tools/bake_stage_codec.py
	$(PY) tools/bake_stage_codec.py

src/gen/world_art.c src/gen/world_art.h: tools/bake_world_art.py tools/bake_look.py tools/bake_art.py
	$(PY) tools/bake_world_art.py

build/obj/core/eg_land.o: src/gen/stage_codec.inc

LAND_SRC := src/core/eg_land.c src/core/eg_land_bind.c src/core/eg_land_dead.c src/core/eg_room.c src/core/eg_link.c src/core/eg_piece.c
build/host/land_one: host/land_one.c src/core/eg_land.c src/core/eg_land.h src/gen/stage_codec.inc
	@mkdir -p $(@D)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/land_one.c src/core/eg_land.c

build/host/test_land: host/test_land.c $(LAND_SRC) src/core/eg_land.h src/gen/stage_codec.inc
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/test_land.c $(LAND_SRC)

test-land: build/host/test_land
	$(PY) tools/bake_stage_codec.py --check
	$(PY) tools/test_stage_codec.py
	$<

build/host/land_dump: host/land_dump.c test/native/land_cases.h src/core/eg_land.c src/core/eg_land.h src/gen/stage_codec.inc
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/land_dump.c src/core/eg_land.c

build/land-check/test.o: test/native/land_codec.c test/native/land_cases.h src/core/eg_land.h
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -o $@ $<

build/land-check/land.gbc: build/land-check/test.o build/obj/core/eg_land.o
	$(LCC) $(LDFLAGS) -Wm-yt0x1B -o $@ $^

test-native-land: build/host/land_dump build/land-check/land.gbc
	$(PY) tools/test_land_native.py $^

build/host/view_dump: host/view_dump.c test/native/view_cases.h src/core/eg_view.c src/core/eg_view.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/view_dump.c src/core/eg_view.c

build/view-check/test.o: test/native/view_stream.c test/native/view_cases.h src/core/eg_view.h src/view_cache.h
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -o $@ $<

build/view-check/view.gbc: build/view-check/test.o build/obj/core/eg_view.o build/obj/view_cache.o build/obj/window_cache.o build/obj/window_fast.o
	$(LCC) $(LDFLAGS) -Wm-yt0x1B -o $@ $^

test-native-view: build/host/view_dump build/view-check/view.gbc
	$(PY) tools/test_view_native.py $^

build/host/test_view: host/test_view.c src/core/eg_view.c src/core/eg_view.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/test_view.c src/core/eg_view.c

test-view: build/host/test_view
	$<

build/world-art-check/test.o: test/native/world_art.c src/gen/look_art.h src/gen/world_art.h
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -o $@ $<

build/world-art-check/art.gbc: build/world-art-check/test.o build/obj/gen/world_art.o build/obj/gen/look_art.o
	$(LCC) $(LDFLAGS) -Wm-yt0x1B -o $@ $^

test-native-world-art: build/world-art-check/art.gbc
	$(PY) tools/test_world_art.py $<

build/host/land_bind_dump: host/land_bind_dump.c test/native/land_bind_cases.h test/native/land_cases.h src/core/eg_land.c src/core/eg_land_bind.c src/core/eg_land_bind.h src/gen/stage_codec.inc
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/land_bind_dump.c src/core/eg_land.c src/core/eg_land_bind.c

build/land-bind-check/test.o: test/native/land_bind.c test/native/land_bind_cases.h test/native/land_cases.h src/core/eg_land_bind.h
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -o $@ $<

build/land-bind-check/bind.gbc: build/land-bind-check/test.o build/obj/core/eg_land.o build/obj/core/eg_land_bind.o
	$(LCC) $(LDFLAGS) -Wm-yt0x1B -o $@ $^

test-native-land-bind: build/host/land_bind_dump build/land-bind-check/bind.gbc
	$(PY) tools/test_land_bind_native.py $^

build/host/world_dump: host/world_dump.c test/native/world_cases.h test/native/land_cases.h src/core/eg_land.c src/core/eg_land_bind.c src/core/eg_view.c src/core/eg_land_bind.h src/gen/stage_codec.inc
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/world_dump.c src/core/eg_land.c src/core/eg_land_bind.c src/core/eg_view.c

build/world-check/test.o: test/native/world_backend.c test/native/world_cases.h test/native/land_cases.h $(HDRS)
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -o $@ $<

build/world-check/world.gbc: build/world-check/test.o build/obj/world.o build/obj/core/eg_land.o build/obj/core/eg_land_bind.o build/obj/core/eg_view.o build/obj/window_cache.o build/obj/window_fast.o build/obj/view_cache.o build/obj/gen/world_art.o build/obj/core/eg_link.o build/obj/world_dead.o build/obj/core/eg_land_dead.o
	$(LCC) $(LDFLAGS) -Wm-yt0x1B -o $@ $^
	@$(PY) -c 'import re; from pathlib import Path; s=Path("build/world-check/world.map").read_text(); end=int(re.search(r"([0-9A-F]{8})  s__HEAP ",s)[1],16); assert end<=0xC500,hex(end)'

test-native-world: build/host/world_dump build/world-check/world.gbc
	$(PY) tools/test_world_backend.py $^

WORLD_PLAY_SEEDS := 0x5fed 0x5100 0x5109 0x5103 0x5104 0x511c 0x5107 0x5102 0x5128 0x5101 0x5105 0x5106
test-world-play: all build/host/land_one
	$(PY) test/harness/world_play.py build/egress.gbc build/world-play/mbc5 --seeds $(WORLD_PLAY_SEEDS)
	$(PY) test/harness/world_play.py build/egress-rtc.gbc build/world-play/mbc3 --seeds $(WORLD_PLAY_SEEDS)

test-world-environments: all build/host/land_one
	@set -e; for rom in egress egress-rtc; do \
	  $(PY) test/harness/world_play.py build/$$rom.gbc build/world-play/$$rom-xs --rule 0 --size 0 --biome 5; \
	  for biome in 0 1 2 3 4 5 6 7; do \
	    $(PY) test/harness/world_play.py build/$$rom.gbc build/world-play/$$rom-biome-$$biome --rule 0 --size 1 --biome $$biome; \
	  done; \
	done

test-world-failure: all build/host/land_one
	$(PY) test/harness/world_failure.py build/egress.gbc build/world-play/failure-mbc5
	$(PY) test/harness/world_failure.py build/egress-rtc.gbc build/world-play/failure-mbc3

test-curriculum: build/host/test_curriculum
	build/host/test_curriculum

build/host/test_aim: host/test_aim.c src/core/eg_move.c src/core/eg_move.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_aim.c src/core/eg_move.c

test-aim: build/host/test_aim
	build/host/test_aim

build/host/test_journal: host/test_journal.c src/core/eg_journal.c src/core/eg_journal.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_journal.c src/core/eg_journal.c

test-journal: build/host/test_journal
	build/host/test_journal

test-continue: all build/host/egtool test-journal
	$(PY) test/harness/resume_game.py build/egress.gbc build/sprints/continue-mbc5
	$(PY) test/harness/resume_game.py build/egress-rtc.gbc build/sprints/continue-mbc3

build/host/test_narrator: host/test_narrator.c src/core/eg_narrator.c src/core/eg_narrator.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_narrator.c src/core/eg_narrator.c

build/host/egnarrate: host/egnarrate.c src/core/eg_narrator.c src/core/eg_narrator.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/egnarrate.c src/core/eg_narrator.c

test-narrator: build/host/test_narrator build/host/egnarrate
	build/host/test_narrator

build/host/test_plate: host/test_plate.c $(ROOM_SRC) $(ROOM_HDR)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_plate.c $(ROOM_SRC)

build/host/test_deadlock: host/test_deadlock.c $(ROOM_SRC) $(ROOM_HDR)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_deadlock.c $(ROOM_SRC)

build/host/test_stamina: host/test_stamina.c src/core/eg_stamina.c src/core/eg_stamina.h src/core/eg_input.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -Isrc -o $@ host/test_stamina.c src/core/eg_stamina.c

test-runtime-core: build/host/test_plate build/host/test_deadlock build/host/test_stamina test-narrator test-journal
	build/host/test_plate
	build/host/test_deadlock
	build/host/test_stamina
	$(PY) tools/narrator/bake_assets.py --check

build/cache-check/test.o: test/native/wram_cache.c src/wram_cache.h
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -o $@ $<

build/cache-check/cache.o: src/wram_cache.c src/wram_cache.h
	@mkdir -p $(dir $@)
	$(LCC) $(CFLAGS) -o $@ $<

build/cache-check/cache.gbc: build/cache-check/test.o build/cache-check/cache.o
	$(LCC) -Wm-yC -o $@ $^

test-wram-cache: build/cache-check/cache.gbc
	$(PY) tools/test_wram_cache.py $<

build/host/test_edges: host/test_edges.c src/core/eg_stage.c src/core/eg_plan.c src/gen/eg_seeds.c $(ROOM_SRC) $(ROOM_HDR)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/test_edges.c src/core/eg_stage.c src/core/eg_plan.c src/gen/eg_seeds.c $(ROOM_SRC)

test-island-edges: build/host/test_edges
	build/host/test_edges

test-runtime: all test-runtime-core test-native-overlap
	$(PY) test/harness/terrain_effect.py build/egress.gbc build/sprints/terrain-effect-mbc5
	$(PY) test/harness/terrain_effect.py build/egress-rtc.gbc build/sprints/terrain-effect-mbc3
	$(PY) test/harness/tutorial_course.py build/egress.gbc build/sprints/tutorial-course-mbc5
	$(PY) test/harness/tutorial_course.py build/egress-rtc.gbc build/sprints/tutorial-course-mbc3
	$(PY) test/harness/hammer.py build/egress.gbc build/sprints/hammer-mbc5
	$(PY) test/harness/hammer.py build/egress-rtc.gbc build/sprints/hammer-mbc3
	$(PY) test/harness/mob_render_fixtures.py build/egress.gbc build/sprints/mob-render-mbc5
	$(PY) test/harness/mob_render_fixtures.py build/egress-rtc.gbc build/sprints/mob-render-mbc3
	$(PY) test/harness/waves.py build/egress.gbc build/sprints/waves-mbc5
	$(PY) test/harness/waves.py build/egress-rtc.gbc build/sprints/waves-mbc3
	$(PY) test/harness/picker_collision.py build/egress.gbc build/sprints/picker-collision-mbc5
	$(PY) test/harness/picker_collision.py build/egress-rtc.gbc build/sprints/picker-collision-mbc3
	$(PY) test/harness/terrain_boundary.py build/egress.gbc build/sprints/terrain-boundary-mbc5
	$(PY) test/harness/terrain_boundary.py build/egress-rtc.gbc build/sprints/terrain-boundary-mbc3
	$(PY) test/harness/menu.py build/egress.gbc build/sprints/menu-mbc5
	$(PY) test/harness/menu.py build/egress-rtc.gbc build/sprints/menu-mbc3
	$(PY) test/harness/stamina.py build/egress.gbc build/sprints/stamina-mbc5
	$(PY) test/harness/stamina.py build/egress-rtc.gbc build/sprints/stamina-mbc3
	$(PY) test/harness/interaction_runtime.py build/egress.gbc build/sprints/interactions-mbc5
	$(PY) test/harness/interaction_runtime.py build/egress-rtc.gbc build/sprints/interactions-mbc3
	$(PY) test/harness/diagonal.py build/egress.gbc build/sprints/diagonal-mbc5
	$(PY) test/harness/void_game.py build/egress.gbc build/sprints/void-mbc5
	$(PY) test/harness/void_game.py build/egress-rtc.gbc build/sprints/void-mbc3
	$(PY) test/harness/island_edges.py build/egress.gbc build/sprints/island-edges-mbc5
	$(PY) test/harness/island_edges.py build/egress-rtc.gbc build/sprints/island-edges-mbc3
	$(PY) test/harness/edge_aim.py build/egress.gbc build/sprints/edge-aim-mbc5
	$(PY) test/harness/projectile_bounds.py build/egress.gbc build/sprints/projectile-bounds-mbc5
	$(PY) test/harness/projectile_bounds.py build/egress-rtc.gbc build/sprints/projectile-bounds-mbc3
	$(PY) test/harness/voice_game.py build/egress.gbc build/sprints/voice-mbc5
	$(PY) test/harness/voice_game.py build/egress-rtc.gbc build/sprints/voice-mbc3
	$(PY) test/harness/seated.py build/egress.gbc build/sprints/seated-native
	$(PY) test/harness/bench_scene.py build/egress.gbc build/sprints/bench-native
	$(PY) test/harness/bench_scene.py build/egress-rtc.gbc build/sprints/bench-native-rtc

test-gameplay: build/egress.gbc build/egress-rtc.gbc build/host/egtool
	$(PY) test/harness/gameplay_rebuild.py build/egress.gbc build/gameplay-rebuild/native
	$(PY) test/harness/gameplay_rebuild.py build/egress-rtc.gbc build/gameplay-rebuild/native-rtc
	$(PY) test/harness/controls.py build/egress.gbc build/gameplay-rebuild/controls
	$(PY) test/harness/controls.py build/egress-rtc.gbc build/gameplay-rebuild/controls-rtc
	$(PY) test/harness/tools_and_weapons.py build/egress.gbc build/gameplay-rebuild/tools

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

test-character-art: build/egress.gbc build/egress-rtc.gbc
	$(PY) test/harness/heir.py build/egress.gbc build/egress-rtc.gbc
	$(PY) test/harness/heir_pixels.py build/egress.gbc build/sprints/heir-pixels-mbc5
	$(PY) test/harness/heir_pixels.py build/egress-rtc.gbc build/sprints/heir-pixels-mbc3

build/host/gravity_ref: host/gravity_ref.c src/core/eg_room.c src/core/eg_link.c src/core/eg_room.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ host/gravity_ref.c src/core/eg_room.c src/core/eg_link.c

test-cartridge-gravity: build/host/gravity_ref build/egress.gbc build/egress-rtc.gbc
	$(PY) test/harness/gravity.py build/host/gravity_ref build/egress.gbc build/egress-rtc.gbc

test-b2: build/egress.gbc
	$(PY) test/harness/stage2.py build/egress.gbc build/b2-checks/stages
	$(PY) test/harness/boss.py build/egress.gbc build/b2-checks/bosses

test-fs-proto:
	SUFFIX=_checked sh proto/raycast/v2/build2.sh 160 64 1
	$(PY) proto/raycast/v2/measure2.py proto/raycast/v2/build/r2_160x64_s1_checked.gbc --frames 1800 --checks 64 \
		--host proto/raycast/v2/build/host2_160x64_s1_checked --min-moving-fps 20 --json build/fs-proto-check.json

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

test-procedural: all build/host/land_one
	$(PY) test/harness/procedural_runs.py build/egress.gbc build/fresh-runs/mbc5
	$(PY) test/harness/procedural_runs.py build/egress-rtc.gbc build/fresh-runs/mbc3

test: build/host/test_room build/host/test_obst build/host/egplan build/egress.gbc build/egress-rtc.gbc test-rtc test-heir test-character-art test-cartridge-gravity test-b2 test-physics test-island-edges test-family test-interactions test-curriculum test-gameplay test-aim test-continue test-runtime test-run-stream test-native-run-stream test-native-move test-gen-fingerprint test-camera test-linked-puzzles test-native-links test-native-linked-puzzles test-window test-native-window test-land test-native-land test-view test-native-view test-native-land-bind test-native-world test-native-world-art test-world-play test-world-environments test-world-failure test-procedural
	build/host/test_obst
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

.PHONY: rom all rtc test test-rtc test-heir test-character-art test-cartridge-gravity test-b2 test-fs-proto test-physics test-island-edges test-family test-interactions test-curriculum test-gameplay test-aim test-continue test-narrator test-journal test-runtime-core test-runtime test-wram-cache shots clean seeds heir
.PHONY: test-run-stream test-native-run-stream test-native-move test-gen-fingerprint test-camera test-links test-linked-puzzles test-native-links test-native-linked-puzzles
.PHONY: test-window test-native-window test-land test-native-land
.PHONY: test-view test-native-view test-native-land-bind test-native-world test-native-world-art test-world-play test-world-environments test-world-failure
.PHONY: test-procedural

.PHONY: test-native-overlap
