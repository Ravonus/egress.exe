#ifndef LOOK_H
#define LOOK_H
/* The look: EGRESS's own floor cards in the shared world's family style, the biome palettes, and the world falling
 * into place and falling apart (docs/redesign.md sections 1 and 2).
 *
 * VRAM bank 1 (BG tiles; the HUD's font in bank 1 too, sprites stay in bank 0):
 *   0              blank (clear_map)
 *   2 .. 137       the biome's core cards (all but M_SLAB_A and M_WALL_TOP), 4 tiles each in 8x16 order (TL, BL, TR,
 *                  BR), never deduplicated; meta_tile[m] holds each card's 2x2 map tiles, meta_pal[m] its palette
 *   LOOK_T_REACT .. LOOK_T_END-1 (138 .. 181)   RESERVED for reaction tiles (puzzle workstream): LOOK_REACT_METAS
 *                  cards, M_REACT0 .. M_REACT0+LOOK_REACT_METAS-1. Four placeholders are baked there now (M_WET,
 *                  M_ICE, M_BURN_A, M_BURN_B); add more in tools/bake_look.py REACTS
 *   182 .. 189     M_GLITCH1, M_PIT (fragile cards that never fly)
 *   190 .. 252     the font and the HUD (tools/bake_art.py, BT_*); 253 .. 255 spare
 *   (the landing loads the talkers' faces at 2 .. 97; the next floor's look_load puts the cards back)
 *
 * VRAM bank 0, BG-only (0x9000 .. 0x97FF, BG indices 0 .. 127 with the bank bit clear; no sprite sees it, nothing
 * else on a floor uses it, the screens' pictures load there and are gone before look_load):
 *   LOOK_LOW0_BASE .. 109  thin ice's crack, the 16 item cards, shared carpet/edge quadrants and four transport-pad
 *                  states. 110..111 spare; 112..127 inventory icons. The baker checks the icon boundary.
 *
 * VRAM bank 0 (sprites 0 .. 209 from tools/bake_art.py; the BG reads bank 0 too, attribute bit 3 clear):
 *   (indices 128 .. 255 are the sprites' tiles 128 .. 255: the BG addresses tiles signed on a floor, LCDC bit 4 clear)
 *   LOOK_B0_BASE .. (210 .. 241)  the fragile cards that fly (meta_pal | LOOK_META_BANK0)
 *   LOOK_SHARD .. +3 (242 .. 245) two glass shard sprites; 246, 247 spare
 *   LOOK_SPR_BASE .. +7 (248 .. 255) M_SLAB_A and M_WALL_TOP themselves: the BG draws them from here, and every card
 *                  in flight during assembly and collapse is one of these two (the real card commits to the BG when it
 *                  lands). Sprites read bank 0 only here: PyBoy 2.7 draws bank 1 sprites (S_BANK) blank.
 *   bake_look.py fails the bake if the sprite set grows past 210.
 *
 * BG palettes on a floor (LOOK_PAL_*): 0 floor slab, 1 foam (+ sprout), 2 walls / drywall / shutters, 3 void and
 * undersides, 4 REACTION A (wet, ice, the water bottle), 5 HUD and the egress door, 6 objects (CRTs, tokens,
 * keycards, bench and props), 7 REACTION B (burning, the burner). Index 1 of palettes 0, 1, 4, 6 and 7 is the biome's
 * slab base, so anything in them sits on the floor without a seam; keep that when adding reaction colours.
 *
 * OBJ palettes: the look borrows OBJ 6 and 7 (normally the hit and tell flashes) only while cards are in flight or
 * pieces are falling, and puts pal_sp's back when the air is clear. OAM: cards use slots 24..39 from the top down
 * (two per card, at most 8 cards), below every actor in priority; draw_sprites keeps to the low slots.
 *
 * Tech (why sprites, not raster tricks): each card is 2 sprites in flight and commits to the BG when it lands (4 map
 * + 4 attribute bytes, through set_bkg_tiles, which waits for VRAM access). At most LOOK_COMMITS_PER_FRAME BG cell
 * writes a frame, at most 8 cards in flight, and at most 3 cards whose paths overlap vertically, so a line never
 * carries more than 6 card sprites beside the player's 4. Per-row SCY splits would move whole rows, not cards, and
 * the LCD interrupt already belongs to the HUD split. Cells off the screen commit at once, in wave order.
 */
#include "play.h"

extern uint8_t look_present[EG_N / 8u]; /* bit set: the cell is in the world (else it shows the void) */
extern uint8_t roam_hidden[EG_N / 8u]; /* camera-owned construction only; never a real pit or broken floor */
extern const uint8_t look_bits[8]; /* ROM0: a variable SM83 shift otherwise loops up to seven times */
#define look_here(i) (look_present[(uint8_t)(i) >> 3] & look_bits[(uint8_t)(i) & 7u])
extern uint8_t look_busy_n; /* cells queued or in flight */
#define look_busy() (look_busy_n != 0u)

/* grid.c (bank 0) */
uint8_t look_void_cell(uint8_t i) BANKED; /* the cell's final look is the void (the room's outer ring of wall) */
uint8_t cell_meta(uint8_t i) BANKED;      /* the card the cell shows now */
void draw_meta(uint8_t i, uint8_t m) BANKED;

/* look.c */
void look_load(uint8_t biome) BANKED; /* after art_load_game, display off: cards to VRAM, the biome into pal_bg */
void look_freeze(void) BANKED;        /* stop the wave where it is and hide the cards (before a fade: the fade
                                         rewrites OBJ 6/7) */
void look_end(void) BANKED;           /* leaving the floor: the screens' palettes back, OBJ 6/7 back, cards hidden */
/* The room assembles around `origin`: every cell on screen (but the origin) leaves the world and comes back in a
 * wave by distance, dropping, rising, sliding or glitching in. Call after look_load and camera(1), before drawing. */
void look_assemble(uint8_t origin) BANKED;
/* Camera presentation only: retain the material board and all object mutations. */
void look_roam_begin(uint8_t enabled) BANKED;
void look_roam_tick(void) BANKED;
void look_anchor(uint8_t cell) BANKED; /* portal landing: reveal camera-owned ground immediately */
void look_shift_view(int8_t dx,int8_t dy) BANKED;
void look_stream_void(uint8_t cell) BANKED; /* prepare an off-screen hardware slot */
/* The room falls apart: the edges first, toward `origin`; cells within `keep` (Chebyshev, 0 = none, 1 = the origin,
 * 2 = it and its 8 neighbours) stay. Each cell cracks, splits in two and falls, darkening, into the void. */
void look_collapse(uint8_t origin, uint8_t keep) BANKED;
void look_drop_cell(uint8_t i) BANKED;  /* one cell into the void (crumble, chase stages, a boss tearing tiles out) */
void look_raise_cell(uint8_t i) BANKED; /* one cell arrives (a bridge built by a recipe) */
void look_tick(void) BANKED;            /* once a frame, right after frame_wait: palettes, cards, commits */
/* Roaming floors reserve VBlank for palettes, then spend remaining time on
 * cosmetic terrain after simulation and complete actor composition. */
void look_frame_begin(void) BANKED;
void look_frame_end(void) BANKED;

/* --- fragile floors (the puzzle workstream owns the rules; the look owns the cards, the break and its sound) ---
 *
 * The cards (gen/look_art.h), each kind's stages consecutive, so a cell shows M_<KIND>0 + steps taken:
 *   M_CARPET0 .. +2   worn carpet (LOOK_CARPET_STEPS 3): sunk and threadbare, a hairline, a split; the 3rd step breaks
 *   M_CEIL0 .. +1     a ceiling tile laid as floor (LOOK_CEIL_STEPS 2): whole, then cracked with a corner sagging
 *   M_GLASS           glass over the void (1 step): the void and its far grid show through, dithered, under glints
 *   M_GLITCH0 .. +2   glitched glass on a beat: solid, flicker, gone (look_glitch_phase)
 *   M_PIT             the hole a break leaves: the floor's torn underside along the top, ragged sides
 * Palettes: carpet FLOOR, ceiling WALL, glass / glitch / pit VOID. Tiles: VRAM bank 0 (LOOK_B0_BASE ..; meta_pal
 * carries LOOK_META_BANK0) for every card that flies (all but M_GLITCH1 and M_PIT, which sit at the end of the bank 1
 * range), so the wobble and the fall show the card itself; two glass shard sprites at LOOK_SHARD.
 *
 * Items: a piece lying in a room shows M_ITEM0 + piece - 1 (src/puzzle.c puzzle_meta): an office thing carrying its
 * element, lifted off the slab on a dithered shadow with a glint. FIRE tealight, LAVA lava lamp, BRICK brick (REACT_B);
 * WATER cooler jug, RAIN watering can, SNOW snow globe, ICE ice cube, STEAM kettle (REACT_A); AIR desk fan, EARTH
 * potted plant, CLOUD cloud (FOAM); METAL wrench, DUST dust bunny (FLOOR); ENERGY energy drink, MUD coffee mug,
 * ELECTRIC sparking plug (OBJ). Each sits on its palette's index 1, the slab base. Thin ice: M_ICE, then M_ICE1.
 *
 * Showing a stage: either map the puzzle's cell types to these ids in grid.c meta_for, or set a skin: look_skin[i]
 * (0, or card + 1) overrides the cell's type while the cell is in the world. look_load clears every skin. */
extern uint8_t __at(0xDCF0) look_skin[EG_N]; /* floor-only; aliases picker scratch */
#define LOOK_SKIN_NONE 0xffu
void look_skin_set(uint8_t i, uint8_t m) BANKED; /* m = M_*, or LOOK_SKIN_NONE; redrawn by look_tick (budgeted) */
void look_redraw(uint8_t i) BANKED;              /* queue a cell's redraw (after the puzzle changes its type) */
enum { LOOK_FRAG_AUTO = 0, LOOK_FRAG_CARPET, LOOK_FRAG_CEIL, LOOK_FRAG_GLASS, LOOK_FRAG_GLITCH };
/* The break: the cell's card wobbles (8 frames), then carpet and ceiling fall with the collapse's split-and-darken,
 * glass and glitched glass burst into shards with a sparkle; the cell shows M_PIT from the first frame (its skin is
 * set to it), and the kind's sound plays. LOOK_FRAG_AUTO reads the kind from the cell's current card. Safe to call
 * from the game loop: one 2x2 map write, the rest runs in look_tick. Uses one card slot (two OAM entries); if all 8
 * are busy the cell turns to the pit at once. The cell stays in the world (look_here): a pit is part of the room. */
void look_break_tile(uint8_t i, uint8_t kind) BANKED;
#define look_break_xy(x, y, kind) look_break_tile(EG_IDX((x), (y)), (kind))
/* Glitched glass: the look runs the beat (look_glitch_period frames solid, 16 flickering, the same gone, 16
 * flickering, on and on) and re-skins every cell whose skin is a glitch card. look_glitch_phase is what shows: 0 solid,
 * 1 flicker, 2 gone. Set look_glitch_period to 0 to drive it yourself with look_glitch_beat(phase). */
extern uint8_t look_glitch_phase, look_glitch_period;
void look_glitch_beat(uint8_t phase) BANKED;
/* harness hook: write a cell, then a kind, and look_tick breaks it (0x80: applies a skin poked into look_skin) */
extern uint8_t look_dbg_cell, look_dbg_kind;

#define LOOK_COMMITS_PER_FRAME 6u
#define LOOK_MAX_CARDS 8u
#define LOOK_OAM_TOP 39u

#endif
