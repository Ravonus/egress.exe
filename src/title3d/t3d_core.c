/* The 3D title's ROM0 side: the renderer's globals and glue (the asm in t3d_cast.s / t3d_fill.s maps the table
 * blobs, so it and everything an ISR runs stays in bank 0), the VBlank and LYC ISRs, and the stack switch.
 * See t3d.h for the memory map. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gb/hardware.h>
#include "egress.h"
#include "mapper.h"
#include "title3d/t3d.h"

BANKREF_EXTERN(t3d_bank_fl)
#define CT_COSABS 0x5A00u /* gen/t3d_layout.inc */

uint16_t t3d_cx, t3d_cy, t3d_ca;
uint8_t t3d_ray_h[T3D_NRAYS], t3d_ray_u[T3D_NRAYS], t3d_ray_t[T3D_NRAYS];
uint16_t t3d_ray_d[T3D_NRAYS];
uint8_t t3d_ray_fv[T3D_NRAYS], t3d_ray_cv[T3D_NRAYS];

volatile uint16_t t3d_vbl;
volatile uint8_t t3d_pend;
uint8_t t3d_pend_bank;
volatile uint8_t t3d_wbank;
uint16_t t3d_palx[40];
volatile uint8_t t3d_pal_dirty;
uint8_t t3d_scx[4];
uint8_t t3d_glitch;
volatile uint8_t t3d_sound;
volatile uint8_t t3d_buzz;
volatile uint8_t t3d_keys; /* START, A, SELECT pressed (edges) since the main loop last took them */
static uint8_t keys_prev;
uint16_t t3d_nviews, t3d_nflips;
uint8_t t3d_stack[T3D_STACK];
uint16_t t3d_saved_sp;
static uint8_t front, slice, pad_n;

void t3d_cast_asm(void);
void t3d_walls_asm(void);
void t3d_floors_asm(void);
extern uint8_t *t3d_wbuf;
extern uint16_t t3d_f_cx8, t3d_f_cy8;

void t3d_render(void) NONBANKED {
  uint8_t b = _current_bank;
  t3d_cast_asm();
  t3d_wbuf = (uint8_t *)T3D_FB;
  t3d_walls_asm();
  t3d_f_cx8 = (uint16_t)(t3d_cx << 3);
  t3d_f_cy8 = (uint16_t)(t3d_cy << 3);
  t3d_wbuf = (uint8_t *)T3D_FB;
  t3d_floors_asm();
  EG_ROM_BANK(b);
}

uint8_t t3d_cosabs(uint16_t a) NONBANKED {
  uint8_t b = _current_bank, v;
  EG_ROM_BANK(BANK(t3d_bank_fl));
  v = *(const uint8_t *)(CT_COSABS + (a & 1023u));
  EG_ROM_BANK(b);
  return v;
}

/* PyBoy treats GDMA as free: the ROM pads the transfer's time (16 bytes per 8 us) so the measured fps is the
 * hardware's; on hardware it costs ~0.7 ms per view */
static void dma_pad(void) NONBANKED __naked {
  __asm
    ld a, (#_pad_n)
    or a, a
    ret z
    ld b, a
  09901$:
    ld c, #3
  09902$:
    dec c
    jr nz, 09902$
    dec b
    jr nz, 09901$
    ret
  __endasm;
}

/* VBlank: palettes (row 0 back from the text, 7 to the logo; all rows when the view's changed), the logo's first
 * slice scroll, the LYC chain, the finished view's copy into the hidden VRAM bank and the map flip, then the sound */
static void t3d_vbl_isr(void) NONBANKED {
  uint8_t vbk = VBK_REG, i;
  t3d_vbl++;
  frame_count++;
  SCX_REG = t3d_glitch ? t3d_scx[0] : 0u;
  slice = 1;
  LYC_REG = t3d_glitch ? 12u : T3D_SWAP_LINE;
  BCPS_REG = 0x80u;
  if (t3d_pal_dirty) {
    const uint8_t *p = (const uint8_t *)t3d_palx;
    for (i = 0; i != 56u; i++) BCPD_REG = *p++;
    t3d_pal_dirty = 0;
  } else {
    const uint8_t *p = (const uint8_t *)t3d_palx;
    for (i = 0; i != 8u; i++) BCPD_REG = *p++;
  }
  {
    const uint8_t *p = (const uint8_t *)&t3d_palx[32];
    BCPS_REG = 0x80u | 56u;
    for (i = 0; i != 8u; i++) BCPD_REG = *p++;
  }
  if (t3d_pend) {
    VBK_REG = (uint8_t)(front ^ 1u);
    SVBK_REG = t3d_pend_bank;
    HDMA1_REG = (uint8_t)(T3D_FB >> 8);
    HDMA2_REG = (uint8_t)T3D_FB;
    HDMA3_REG = 0x80u;
    HDMA4_REG = 0x00u;
    HDMA5_REG = (uint8_t)(T3D_BUFSZ / 16u - 1u);
    SVBK_REG = t3d_wbank;
#ifndef T3D_NO_DMA_PAD
    pad_n = (uint8_t)(T3D_BUFSZ / 16u);
    dma_pad();
#endif
    front ^= 1u;
    LCDC_REG ^= LCDCF_BG9C00;
    t3d_nflips++;
    t3d_pend = 0;
  }
  VBK_REG = vbk;
  {
    uint8_t j = joypad();
    t3d_keys |= (uint8_t)(j & (uint8_t)~keys_prev & (J_START | J_A | J_SELECT));
    keys_prev = j;
  }
  if (t3d_sound) sound_tick();
  if (t3d_buzz) {
    /* the fluorescent buzz: a short, quiet, low 7-bit noise tone on channel 4 (the music takes it back at its next
     * note, like the game's effects) */
    uint8_t v = (uint8_t)((sfx_volume * 3u) >> 3);
    t3d_buzz = 0;
    if (v) {
      NR42_REG = (uint8_t)((v << 4) | 0x02u);
      NR43_REG = 0x49u;
      NR44_REG = 0x80u;
    }
  }
}

/* LYC: the logo's glitch slices (lines 12, 18, 24, then 32 back to 0), then at T3D_SWAP_LINE palette 0 becomes the
 * text's and palette 7 the view's last row. The palette write waits for HBlank (mode 3, then mode 0) and is 16 bytes
 * of unrolled ldh, well inside one HBlank plus the next line's OAM scan. */
static void t3d_lcd_isr(void) NONBANKED {
  if (LYC_REG != T3D_SWAP_LINE) {
    SCX_REG = slice < 4u ? t3d_scx[slice] : 0u;
    slice++;
    LYC_REG = slice == 2u ? 18u : slice == 3u ? 24u : slice == 4u ? 32u : T3D_SWAP_LINE;
    return;
  }
  __asm
    ld hl, #(_t3d_palx + 72)
    ld c, #0x69
  00701$:
    ldh a, (0x41)
    and a, #3
    cp a, #3
    jr nz, 00701$
  00702$:
    ldh a, (0x41)
    and a, #3
    jr nz, 00702$
    ld a, #0x80
    ldh (0x68), a

    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, #0xB8
    ldh (0x68), a
    ld hl, #(_t3d_palx + 56)
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
  __endasm;
}

void t3d_isr_on(void) NONBANKED {
  front = (LCDC_REG & LCDCF_BG9C00) ? 1u : 0u;
  keys_prev = joypad(); /* a button still held from the last screen is not a press */
  t3d_keys = 0;
  CRITICAL {
    add_VBL(t3d_vbl_isr);
    add_LCD(t3d_lcd_isr);
  }
}

void t3d_isr_off(void) NONBANKED {
  CRITICAL {
    remove_VBL(t3d_vbl_isr);
    remove_LCD(t3d_lcd_isr);
    LYC_REG = hud_line;
    SCX_REG = 0;
  }
}

/* The title's own stack (t3d.h): GBDK's sits in 0xD000-0xDFFF, which the title banks. */
void t3d_on_stack(void) NONBANKED {
  __asm
    ld (_t3d_saved_sp), sp
    ld sp, #(_t3d_stack + 192)
  __endasm;
  title3d_main();
  __asm
    ld hl, #_t3d_saved_sp
    ld a, (hl+)
    ld h, (hl)
    ld l, a
    ld sp, hl
  __endasm;
}
