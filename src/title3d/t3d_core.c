/* The baked title's ROM0 side: the record player (it maps the loops' ROM chunks, so it stays in bank 0), the VBlank
 * and LYC ISRs, and the stack switch. See t3d.h for the memory map and tools/title3d/bake.py for the records. */
#include <gb/gb.h>
#include <string.h>
#include <gb/cgb.h>
#include <gb/hardware.h>
#include "egress.h"
#include "mapper.h"
#include "title3d/t3d.h"

volatile uint16_t t3d_vbl;
volatile uint8_t t3d_map_ready;
uint8_t t3d_map_pace = 2u;
uint16_t t3d_palx[36];
volatile uint8_t t3d_pal_dirty;
uint8_t t3d_scx[4];
uint8_t t3d_glitch;
volatile uint8_t t3d_sound;
volatile uint8_t t3d_buzz;
volatile uint8_t t3d_keys;
volatile uint16_t t3d_nflips;
volatile uint16_t t3d_cyc_tiles, t3d_cyc_pal;
uint8_t t3d_flip_late, t3d_flip_line_max; /* flips outside VBlank (must stay 0); the latest VBlank line one landed on */
uint8_t t3d_stack[T3D_STACK];
uint16_t t3d_saved_sp;
static uint8_t keys_prev, slice;
static uint16_t last_flip;

/* ---- the records (tools/title3d/bake.py): one per frame, in ROM chunks of up to 16 KiB; per loop a dictionary of
 * its unique tiles (1024 to a chunk) the records name by index ---- */
extern const uint8_t tb_chunks[]; /* per chunk: bank, address (u16) */
extern const uint8_t tb_loops[];  /* per loop: first chunk, chunks, frames (u16), frame 1's record: chunk, offset (u16),
                                     first dictionary chunk */

static const uint8_t *rec; /* the next record */
static uint8_t rec_chunk, dict0;

static void rec_at(uint8_t c, uint16_t off) NONBANKED {
  const uint8_t *e = &tb_chunks[(uint16_t)c * 3u];
  rec_chunk = c;
  rec = (const uint8_t *)((e[1] | ((uint16_t)e[2] << 8)) + off);
}

uint16_t tb_frames(uint8_t loop) NONBANKED {
  const uint8_t *l = &tb_loops[(uint16_t)loop * 8u];
  return (uint16_t)(l[2] | ((uint16_t)l[3] << 8));
}

void tb_open(uint8_t loop) NONBANKED {
  const uint8_t *l = &tb_loops[(uint16_t)loop * 8u];
  dict0 = l[7];
  rec_at(l[0], 0);
}

void tb_frame1(uint8_t loop) NONBANKED {
  const uint8_t *l = &tb_loops[(uint16_t)loop * 8u];
  rec_at(l[4], (uint16_t)(l[5] | ((uint16_t)l[6] << 8)));
}

/* one HBlank DMA (Pan Docs, HDMA5 bit 7 = 1): n blocks of 16 bytes from WRAM into VRAM bank 0, one block in each
 * HBlank (mode 0, where VRAM is free to write) of the visible lines; waits for the last one to finish first. HDMA5
 * reads with bit 7 set once a transfer is done.
 * The wait also writes SB (the serial data register; no transfer is ever started, so on hardware it does nothing).
 * That is for PyBoy (the harness): with an HBlank DMA running in double speed it can hand the CPU a run of ~2^32
 * cycles (its cycles_to_mode0() wraps after an overshoot into mode 2), so a plain poll loop spins for minutes; a
 * write below 0xFF4C makes it give control back every pass. Without it PyBoy hangs. */
static void hdma_wait(void) NONBANKED __naked {
  __asm
  00601$:
    ldh a, (0x55)
    rlca
    ret c
    ldh (0x01), a
    jr 00601$
  __endasm;
}

static void hdma(const uint8_t *from, uint16_t to, uint8_t n) NONBANKED {
  hdma_wait();
  VBK_REG = 0;
  HDMA1_REG = (uint8_t)((uint16_t)from >> 8);
  HDMA2_REG = (uint8_t)(uint16_t)from;
  HDMA3_REG = (uint8_t)(to >> 8);
  HDMA4_REG = (uint8_t)to;
  HDMA5_REG = (uint8_t)(0x80u | (uint8_t)(n - 1u));
}

/* the next record, the whole frame. A record: u16 next (0: the next chunk), u8 jobs, u8 map runs, u8 dictionary
 * runs; jobs x {u16 VRAM address, u8 tiles}; map runs x {u16 shadow offset, u8 n, n ids}; dictionary runs x
 * {u16 first tile, u8 n} (the frame's tiles in job order). The map runs go into the shadow, the jobs and the
 * dictionary runs into WRAM, then each run's tiles are copied out of the dictionary (its own bank) into staging;
 * then HBlank DMA puts the tiles into their VRAM slots and the shadow's ids into the hidden BG map (its attributes
 * never change: t3d.h), job after job, during the visible lines; then the frame is marked ready and the VBlank flips
 * it in. Called once the last frame has flipped in (t3d_map_ready == 0): the bake keeps this frame's uploads off the
 * tiles of the frame on screen. Runs with WRAM bank 2 mapped. */
void tb_play(void) NONBANKED {
  uint8_t b = _current_bank, nj, nm, nr, n, k;
  const uint8_t *p = rec, *q, *e;
  uint8_t *st;
  uint16_t next, off, i;
  EG_ROM_BANK(tb_chunks[(uint16_t)rec_chunk * 3u]);
  next = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
  nj = p[2];
  nm = p[3];
  nr = p[4];
  q = p + 5u;
  memcpy(T3D_JOBS, q, (uint16_t)nj * 3u);
  q += (uint16_t)nj * 3u;
  for (k = nm; k; k--) {
    off = (uint16_t)(q[0] | ((uint16_t)q[1] << 8));
    n = q[2];
    memcpy(T3D_SHADOW + off, q + 3, n);
    q += 3u + (uint16_t)n;
  }
  memcpy(T3D_RANGES, q, (uint16_t)nr * 3u);
  if (next) {
    rec = p + next;
  } else {
    rec_at((uint8_t)(rec_chunk + 1u), 0);
  }
  /* the tiles, out of the dictionary */
  st = T3D_STAGE;
  for (k = nr, q = T3D_RANGES; k; k--, q += 3u) {
    i = (uint16_t)(q[0] | ((uint16_t)q[1] << 8));
    e = &tb_chunks[(uint16_t)(uint8_t)(dict0 + (uint8_t)(i >> 10)) * 3u];
    EG_ROM_BANK(e[0]);
    off = (uint16_t)q[2] << 4;
    memcpy(st, (const uint8_t *)((e[1] | ((uint16_t)e[2] << 8)) + ((i & 1023u) << 4)), off);
    st += off;
  }
  EG_ROM_BANK(b);
  /* into VRAM: the tiles, then the ids */
  st = T3D_STAGE;
  for (k = 0, q = T3D_JOBS; k != nj; k++, q += 3u) {
    n = q[2];
    hdma(st, (uint16_t)(q[0] | ((uint16_t)q[1] << 8)), n);
    st += (uint16_t)n * 16u;
    t3d_cyc_tiles += n;
  }
  hdma(T3D_SHADOW, ((LCDC_REG & LCDCF_BG9C00) ? 0x9800u : 0x9C00u) + T3D_ROW_VIEW * 32u, 26u);
  hdma_wait();
  t3d_map_ready = 1;
}

/* ---- VBlank ---- */
/* 64 BG palette bytes and 8 OBJ bytes, unrolled ldh (c) (VBlank: palette RAM is free) */
static void pal_upload(void) NONBANKED __naked {
  __asm
    ld hl, #_t3d_palx
    ld a, #0x80
    ldh (0x68), a
    ld c, #0x69
    ld b, #16
  00801$:
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    ld a, (hl+)
    ldh (c), a
    dec b
    jr nz, 00801$
    ld a, #0x80
    ldh (0x6A), a
    ld c, #0x6B
    ld b, #8
  00802$:
    ld a, (hl+)
    ldh (c), a
    dec b
    jr nz, 00802$
    ret
  __endasm;
}

/* VBlank: the logo's first slice scroll and the LYC chain; the flip, once the next frame is in VRAM and the pace
 * allows (a register write: LCDC bit 3); the palettes, if they changed and the VBlank has the ~2 lines left (LY now:
 * the VBlank may have started late, main having had interrupts off); then the keys and the sound. No VRAM work: the
 * frames go in by HBlank DMA (tb_play). */
static void t3d_vbl_isr(void) NONBANKED {
  uint8_t ly;
  t3d_vbl++;
  frame_count++;
  SCX_REG = t3d_glitch ? t3d_scx[0] : 0u;
  slice = 1;
  LYC_REG = t3d_glitch ? 11u : 144u;
  if (t3d_map_ready && (uint16_t)(t3d_vbl - last_flip) >= t3d_map_pace) {
    ly = LY_REG;
    if ((STAT_REG & 3u) == 1u) {
      LCDC_REG ^= LCDCF_BG9C00;
      t3d_map_ready = 0;
      t3d_nflips++;
      last_flip = t3d_vbl;
      if (ly >= 144u && (uint8_t)(ly - 144u) > t3d_flip_line_max) t3d_flip_line_max = (uint8_t)(ly - 144u);
    } else {
      t3d_flip_late++; /* not flipped: the VBlank was missed, next one */
    }
  }
  ly = LY_REG;
  if (t3d_pal_dirty && (STAT_REG & 3u) == 1u && ly >= 144u && ly < T3D_VBL_LAST - 2u) {
    pal_upload();
    t3d_pal_dirty = 0;
    t3d_cyc_pal += 72u;
  }
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

/* wait for this line's HBlank (mode 3 then mode 0), so an SCX write applies from the next line, cleanly (the SB
 * writes: hdma_wait; this can run while an HBlank DMA is in progress) */
static void hbl_wait(void) NONBANKED __naked {
  __asm
  00701$:
    ldh (0x01), a
    ldh a, (0x41)
    and a, #3
    cp a, #3
    jr nz, 00701$
  00702$:
    ldh (0x01), a
    ldh a, (0x41)
    and a, #3
    jr nz, 00702$
    ret
  __endasm;
}

/* LYC: the logo's glitch slices: lines 12-17, 18-23, 24-29 take their own SCX, from 30 (the neon rule, then the
 * view) it is 0 again. Only while the logo glitches; otherwise the one LYC at line 144 returns at once. */
static void t3d_lcd_isr(void) NONBANKED {
  if (!t3d_glitch || slice > 4u) return;
  hbl_wait();
  SCX_REG = slice < 4u ? t3d_scx[slice] : 0u;
  slice++;
  LYC_REG = slice == 2u ? 17u : slice == 3u ? 23u : slice == 4u ? 29u : 144u;
}

void t3d_isr_on(void) NONBANKED {
  keys_prev = joypad(); /* a button still held from the last screen is not a press */
  t3d_keys = 0;
  last_flip = t3d_vbl;
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
