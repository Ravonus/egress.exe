#ifndef T3D_H
#define T3D_H
/* The PRESS START title: a slow, seeded backrooms walk drawn by the v2 raycaster (proto/raycast/v2, docs/redesign.md
 * 15 and 15.1) at 96x72, with the EGRESS.EXE logo above and PRESS START below.
 *
 * Memory while the title runs (nothing of it is live once title3d_run() returns):
 *
 *   WRAM banks 2 and 3 (SVBK), the same layout in both:
 *     0xD000-0xD3FF  the 32x32 map (1 KB aligned: the floor asm builds cell addresses from bits)   copy in both
 *     0xD400-0xDABF  a framebuffer, 12 strips of 144 bytes (bank 2: buffer A, bank 3: buffer B)
 *     0xDB00-0xDB2F  REC, the floor pass's per-tile-column scratch (page aligned)                  per bank
 *     0xDB80-0xDBFF  dead-end bits of the maze (the walker avoids them)                            copy in both
 *     0xDC00-0xDE4F  the generator's scratch: carve stack x, y; dressing zones (init only)         bank 2
 *     0xDF00-0xDF68  HRAM 0xFF93-0xFFFB saved on entry, restored on exit                           bank 2
 *   The main loop renders a view with SVBK on that view's bank; the VBlank ISR maps the other bank only around its
 *   GDMA. So the CPU stack cannot be in 0xD000-0xDFFF (GBDK's stack is at 0xDFFF down): the title runs on its own
 *   stack, t3d_stack in WRAM0, switched in and out by t3d_on_stack(). Every game global must stay below 0xD000 (the
 *   Makefile checks the link): the ISRs and the sound driver read them while bank 2 or 3 is mapped.
 *   WRAM0: the title's own C data (~0.5 KB, t3d_core.c / title3d.c / the asm's _DATA) and t3d_stack.
 *
 *   HRAM 0xFF93-0xFFFB: the asm's scratch (GBDK owns 0xFF80-0xFF92), saved and restored around the title.
 *
 *   ROM: three autobanked 16 KB blobs (t3d_bank_w0, t3d_bank_fl, t3d_bank_w1: each fills its bank, so it starts at
 *   0x4000 where the asm expects its tables); the asm and the ISRs in ROM0 (they map the blobs); the title's C in an
 *   autobanked bank.
 *
 *   VRAM: tiles 0-107 of both banks are the double-buffered framebuffer (bank 0 for BG map 0x9800, bank 1 for 0x9C00;
 *   the BG flips between the maps). Bank 0 tiles 108+ the logo and 160+ the frame, bank 1 tiles 108+ and 160+ the font in two inks.
 *   Both BG maps hold the same logo, frame and text. BG palettes: 0-7 are the view's per-tile-row gradient (9 rows:
 *   palette 0 serves rows 0 and 8); palette 7 shows the logo above the view and palette 0 the text below it, swapped
 *   by two LYC interrupts (lines T3D_SWAP_LINE and T3D_SWAP2_LINE).
 *   No sprites.
 */
#include <gb/gb.h>
#include <stdint.h>

#define T3D_WBANK_A 2u
#define T3D_WBANK_B 3u
#define T3D_MAP ((uint8_t *)0xD000u)
#define T3D_FB 0xD400u
#define T3D_DEAD ((uint8_t *)0xDB80u)
#define T3D_STK ((uint8_t *)0xDC00u)
#define T3D_HSAVE ((uint8_t *)0xDF00u)
#define T3D_HRAM_LO 0x93u
#define T3D_HRAM_N (0xFCu - 0x93u)

#define T3D_VW 96u
#define T3D_VH 72u
#define T3D_NRAYS 24u
#define T3D_TW 12u
#define T3D_TH 9u
#define T3D_BUFSZ (T3D_TW * T3D_VH * 2u)
#define T3D_VX 4u /* view tile column */
#define T3D_VY 5u /* view tile row */
#define T3D_MAPW 32u

#define T3D_SWAP_LINE 64u /* palette 7 changes from the logo to view row 7, palette 0 from view row 0 to row 8 */
#define T3D_SWAP2_LINE 111u /* palette 0 changes from view row 8 to the text (the bar under the view, line 112) */
#define T3D_TEXT_Y (T3D_VY + T3D_TH + 1u) /* PRESS START; a blank row, then the cycling info row (clock / prompt / seed) */
/* t3d_palx: the view rows' colours, then the logo's, then the text's */
#define T3D_PX_VIEW (T3D_TH * 4u)
#define T3D_PX_LOGO T3D_PX_VIEW
#define T3D_PX_TEXT (T3D_PX_VIEW + 4u)
#define T3D_PX_N (T3D_PX_VIEW + 8u)
#define T3D_STACK 192u /* t3d_core.c hard-codes it in the stack switch */

/* VRAM tiles */
#define T3D_T_LOGO 108u  /* bank 0, after the framebuffer's 108 tiles */
#define T3D_T_FRAME 160u /* bank 0, after the logo (<= 52 logo tiles) */
#define T3D_T_FONT1 108u /* bank 1, ink colour 1 (48 glyphs) */
#define T3D_T_FONT2 160u /* bank 1, ink colour 2 */

enum { T3D_START = 1, T3D_SELECT = 2 };

/* ---- t3d_core.c (ROM0): renderer glue, the ISRs, the stack switch ---- */
extern uint16_t t3d_cx, t3d_cy, t3d_ca; /* 8.8 cells, angle 0..1023 */
extern uint8_t t3d_ray_h[T3D_NRAYS], t3d_ray_u[T3D_NRAYS], t3d_ray_t[T3D_NRAYS];
extern uint16_t t3d_ray_d[T3D_NRAYS];
extern uint8_t t3d_ray_fv[T3D_NRAYS], t3d_ray_cv[T3D_NRAYS];

extern volatile uint16_t t3d_vbl;      /* VBlanks while the title runs */
extern volatile uint8_t t3d_pend;      /* a finished view waits for its VBlank copy */
extern uint8_t t3d_pend_bank;          /* its WRAM bank */
extern volatile uint8_t t3d_wbank;     /* the WRAM bank the main loop has mapped */
extern uint16_t t3d_palx[T3D_PX_N];    /* view rows 0-8, logo, text: written to the hardware by the VBlank / LYC ISRs */
extern volatile uint8_t t3d_pal_dirty; /* the view rows changed */
extern uint8_t t3d_scx[4];             /* logo slice scroll (glitch) for lines 0-11, 12-17, 18-23, 24-31 */
extern uint8_t t3d_glitch;             /* nonzero: run the slice chain */
extern volatile uint8_t t3d_sound;     /* the VBlank ISR ticks the sound driver */
extern volatile uint8_t t3d_buzz;      /* the VBlank ISR plays the fluorescent buzz once */
extern volatile uint8_t t3d_keys;      /* new presses latched by the VBlank ISR (so none is lost while the world builds) */
extern uint16_t t3d_nviews, t3d_nflips;
extern uint8_t t3d_stack[T3D_STACK];

void t3d_render(void) NONBANKED;          /* cast + walls + floors into T3D_FB of the mapped WRAM bank */
uint8_t t3d_cosabs(uint16_t a) NONBANKED; /* |cos| table (floors blob) */
void t3d_isr_on(void) NONBANKED;
void t3d_isr_off(void) NONBANKED;
void t3d_on_stack(void) NONBANKED;        /* runs title3d_main() on t3d_stack */
void t3d_cast_init(void);

/* ---- title3d.c (banked) ---- */
extern uint16_t t3d_seed;       /* this title's walk */
extern uint16_t t3d_seed_force; /* harness hook: nonzero forces the walk's seed */
extern uint8_t t3d_result;
extern volatile uint8_t t3d_freeze, t3d_frozen; /* harness hook: hold the loop after a view (host diff) */
void title3d_main(void) BANKED;  /* on t3d_stack, WRAM banked: the whole title */
uint8_t title3d_run(void) BANKED; /* T3D_START or T3D_SELECT; returns with every palette black, LCD on */

#endif
