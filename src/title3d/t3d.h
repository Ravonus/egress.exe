#ifndef T3D_H
#define T3D_H
/* The PRESS START title (docs/redesign.md 15 and 15.1): a fullscreen backrooms walk at 30 fps, pre-baked. The walk is
 * rendered offline by the v2 renderer (tools/title3d/bake.py, 160x104 through proto/raycast/v2/src/core2.c) and stored
 * in ROM as per-frame records: the tiles that change (as indices into the loop's dictionary of unique tiles), the map
 * cells that change. The cartridge does no rendering and no decoding: main copies a record's map runs into a shadow
 * map and its tiles out of the dictionary into WRAM, HBlank DMA puts both into VRAM, the VBlank flips the BG map. The EGRESS.EXE logo sits on a dark band
 * above the view (static BG rows), PRESS START floats over the view (sprites), the info row (clock / prompt / seed) is
 * the window along the bottom. Nothing over the view is ever redrawn by the walk.
 *
 * Documented CGB features only (Pan Docs): double speed, HBlank DMA (HDMA) for every VRAM write of the walk (it
 * writes only in HBlank, mode 0, where VRAM is free), palette writes and the map flip (LCDC bit 3) in VBlank, SCX
 * writes for the logo's glitch slices after a STAT wait for HBlank, the window, sprites. No mid-frame palette swaps,
 * no GDMA, nothing timing-tight: the same on a GBC, a ModRetro Chromatic, SameBoy and PyBoy.
 *
 * Per shown frame (30 fps, two VBlanks): main copies the record's map runs into the shadow and its tiles into
 * staging (~25 scanlines), then feeds HDMA job by job: ~13 tiles and the 26 blocks of map ids, a block per visible
 * line (~45 lines); the VBlank flips once that is all in and the pace allows, and uploads the palettes. Verified in
 * SameBoy (cycle-accurate: tools/title3d/sameboy_check.py) and PyBoy (test/harness/title3d.py). Before: an LZ4-style
 * stream decoded in C took ~270 scanlines a frame (13-25 fps), then GDMA in VBlank ran out of VBlank on big frames.
 *
 * Memory while the title runs (nothing of it is live once title3d_run() returns):
 *
 *   WRAM bank 2 (SVBK = 2 for the whole title; game data stays below 0xD000, the Makefile checks the link):
 *     0xD000-0xD7FF  staging: a frame's tiles (up to 128), the HDMA source
 *     0xD800-0xD88F  the frame's upload jobs (VRAM address, tiles; up to 48)
 *     0xD900-0xDA7F  the frame's dictionary runs (first tile, tiles; up to 128)
 *     0xDC00-0xDD9F  the shadow BG map: 13 view rows x 32 tile ids (the HDMA source of the hidden map)
 *     the rest free
 *   GBDK's stack is at 0xDFFF down, inside the bank: the title runs on its own stack, t3d_stack in WRAM0.
 *   WRAM0: the title's C data (~0.2 KiB) and t3d_stack (192 B).
 *
 *   ROM: the loops' records and dictionaries (TB_NLOOPS loops, ~470 KB in autobanked chunks of up to 16 KiB,
 *   build/t3d_bake/gen/ledger.txt);
 *   the chunk and loop tables, the record player and the ISRs in ROM0 (they switch banks); the title's C banked.
 *
 *   VRAM (BG tile data at 0x8800, signed ids): bank 0 ids 0-255 are the stream's tile slots (all of them: so the
 *   view's BG attributes are fixed, each row's palette band in bank 0, and only map ids ever move); bank 1 ids
 *   152-255 the static tiles (blank, logo, font), bank 1 ids 0-151 unused. OBJ tiles 0-5 at 0x8000 (bank 0, outside the BG's
 *   range): PRESS START's letters. BG maps 0x9800 and 0x9C00 both hold the logo band (rows 14-17) and a view (rows
 *   18-30); SCY = 112 shows rows 14-31. The hidden map gets the next frame's view by HBlank DMA, then LCDC flips the maps.
 *   The window (map 0x9C00, row 0, never inside the BG's rows) is the info row from line 136.
 *   Palettes: BG 0-5 the view's bands, 6 the text, 7 the logo; OBJ 0 PRESS START.
 */
#include <gb/gb.h>
#include <stdint.h>

#define T3D_WBANK 2u
#define T3D_STAGE ((uint8_t *)0xD000u)
#define T3D_JOBS ((uint8_t *)0xD800u)
#define T3D_RANGES ((uint8_t *)0xD900u)
#define T3D_SHADOW ((uint8_t *)0xDC00u)

#define T3D_SCY 112u      /* BG rows 14-31 on screen */
#define T3D_ROW_LOGO 14u  /* the dark band: row 14 blank, the logo's three rows 15-17 */
#define T3D_ROW_VIEW 18u  /* the view: rows 18-30, lines 32-135 */
#define T3D_WY 136u       /* the window: the info row */
#define T3D_SPR_Y 116u    /* PRESS START over the view (screen line) */

#define T3D_STATIC 152u /* bank 1 BG ids from here: static (TB_STATIC) */
#define T3D_T_BLANK 152u
#define T3D_T_LOGO 153u
#define T3D_T_FONT 205u

#define T3D_PAL_TEXT 6u
#define T3D_PAL_LOGO 7u

#define T3D_STACK 192u     /* t3d_core.c hard-codes it in the stack switch */
#define T3D_VBL_LAST 152u  /* the palettes go up only if the VBlank has from here 2 lines left */
#define T3D_JOB_MAX 24u    /* tiles per upload job (the bake splits runs) */
#define T3D_STAGE_TILES 128u
#define T3D_MAX_JOBS 48u
#define T3D_MAX_RANGES 128u /* dictionary runs a record stages */
#define T3D_DICT_TILES 1024u

enum { T3D_START = 1, T3D_SELECT = 2 };

/* ---- t3d_core.c (ROM0): the record player, the VBlank / LYC ISRs, the stack switch ---- */
extern volatile uint16_t t3d_vbl;     /* VBlanks while the title runs */
extern volatile uint8_t t3d_map_ready; /* the next frame is in VRAM (its tiles, the hidden map): flip it */
extern uint8_t t3d_map_pace;          /* VBlanks between flips (2: 30 fps) */
extern uint16_t t3d_palx[36];         /* BG palettes 0-7 and OBJ palette 0: written in VBlank when dirty */
extern volatile uint8_t t3d_pal_dirty;
extern uint8_t t3d_scx[4];            /* logo slice scroll (glitch) for lines 0-11, 12-17, 18-23, 24-29 */
extern uint8_t t3d_glitch;            /* nonzero: run the slice chain */
extern volatile uint8_t t3d_sound;    /* the VBlank ISR ticks the sound driver */
extern volatile uint8_t t3d_buzz;     /* the VBlank ISR plays the fluorescent buzz once */
extern volatile uint8_t t3d_keys;     /* new presses latched by the VBlank ISR */
extern volatile uint16_t t3d_nflips;  /* frames shown */
extern volatile uint16_t t3d_cyc_tiles, t3d_cyc_pal; /* harness: tiles uploaded, palette bytes, this title */
extern uint8_t t3d_stack[T3D_STACK];
extern uint8_t t3d_flip_late, t3d_flip_line_max; /* flips that missed a VBlank (must stay 0); latest flip line - 144 */

void tb_open(uint8_t loop) NONBANKED;   /* the loop from its keyframe record */
void tb_frame1(uint8_t loop) NONBANKED; /* the loop's frame 1 record (after the wrap record W) */
void tb_play(void) NONBANKED;           /* the next record into VRAM by HBlank DMA, then marked ready to flip */
uint16_t tb_frames(uint8_t loop) NONBANKED;
void t3d_isr_on(void) NONBANKED;
void t3d_isr_off(void) NONBANKED;
void t3d_on_stack(void) NONBANKED; /* runs title3d_main() on t3d_stack */

/* ---- title3d.c (banked) ---- */
extern uint16_t t3d_seed;       /* this title's seed: picks the loop and the palette */
extern uint16_t t3d_seed_force; /* harness hook: nonzero forces the seed */
extern uint8_t t3d_loop, t3d_var;
extern uint16_t t3d_frame;      /* the frame on screen (harness) */
extern uint8_t t3d_result;
extern volatile uint8_t t3d_freeze, t3d_frozen; /* harness hook: hold the walk after a flip (VRAM check) */
void title3d_main(void) BANKED;  /* on t3d_stack, WRAM bank 2: the whole title */
uint8_t title3d_run(void) BANKED; /* T3D_START or T3D_SELECT; returns with every palette black, LCD on */

#endif
