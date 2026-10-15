#ifndef LOOK_H
#define LOOK_H
/* The look: EGRESS's own floor cards in the shared world's family style, the biome palettes, and the world falling
 * into place and falling apart (docs/redesign.md sections 1 and 2).
 *
 * VRAM bank 1 (BG tiles; the HUD's font in bank 1 too, sprites stay in bank 0):
 *   0              blank (clear_map)
 *   2 .. 145       the biome's core cards, metatile m = tiles LOOK_T_BASE + 4m .. +3 in 8x16 order (TL, BL, TR, BR),
 *                  never deduplicated
 *   LOOK_T_REACT .. LOOK_T_END-1   RESERVED for reaction tiles (puzzle workstream): LOOK_REACT_METAS metatiles,
 *                  M_REACT0 .. M_REACT0+LOOK_REACT_METAS-1, same 8x16 order. Four placeholders are baked there now
 *                  (M_WET, M_ICE, M_BURN_A, M_BURN_B); add more in tools/bake_look.py REACTS
 *   190 .. 252     the font and the HUD (tools/bake_art.py, BT_*); 253 .. 255 spare
 *   (the landing loads the talkers' faces at 2 .. 97; the next floor's look_load puts the cards back)
 *
 * VRAM bank 0 (sprites): LOOK_SPR_BASE .. +7 (248 .. 255) RESERVED: the two flying cards, a floor slab and a wall
 * top, copied from the bank 1 cards by look_load. (Reading the bank 1 cards straight from OAM with S_BANK works on
 * hardware, but PyBoy 2.7 draws bank 1 sprites blank, so the harness could not see them.) Every card in flight and
 * every falling piece is one of these two; the real card commits to the BG when it lands.
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
#define look_here(i) (look_present[(uint8_t)(i) >> 3] & (uint8_t)(1u << ((uint8_t)(i) & 7u)))
extern uint8_t look_busy_n; /* cells queued or in flight */
#define look_busy() (look_busy_n != 0u)

/* grid.c (bank 0) */
uint8_t look_void_cell(uint8_t i) NONBANKED; /* the cell's final look is the void (the room's outer ring of wall) */
uint8_t cell_meta(uint8_t i) NONBANKED;      /* the card the cell shows now */
void draw_meta(uint8_t i, uint8_t m) NONBANKED;

/* look.c */
void look_load(uint8_t biome) BANKED; /* after art_load_game, display off: cards to VRAM, the biome into pal_bg */
void look_freeze(void) BANKED;        /* stop the wave where it is and hide the cards (before a fade: the fade
                                         rewrites OBJ 6/7) */
void look_end(void) BANKED;           /* leaving the floor: the screens' palettes back, OBJ 6/7 back, cards hidden */
/* The room assembles around `origin`: every cell on screen (but the origin) leaves the world and comes back in a
 * wave by distance, dropping, rising, sliding or glitching in. Call after look_load and camera(1), before drawing. */
void look_assemble(uint8_t origin) BANKED;
/* The room falls apart: the edges first, toward `origin`; cells within `keep` (Chebyshev, 0 = none, 1 = the origin,
 * 2 = it and its 8 neighbours) stay. Each cell cracks, splits in two and falls, darkening, into the void. */
void look_collapse(uint8_t origin, uint8_t keep) BANKED;
void look_drop_cell(uint8_t i) BANKED;  /* one cell into the void (crumble, chase stages, a boss tearing tiles out) */
void look_raise_cell(uint8_t i) BANKED; /* one cell arrives (a bridge built by a recipe) */
void look_tick(void) BANKED;            /* once a frame, right after frame_wait: palettes, cards, commits */

#define LOOK_COMMITS_PER_FRAME 6u
#define LOOK_MAX_CARDS 8u
#define LOOK_OAM_TOP 39u

#endif
