/* Bank 0: composing the heir's sprite from the baked D3 frame records (tools/bake_heir.py). A record is a mask of
 * the base's non-empty sprites, the base frame's 4 sprites (BODY L, BODY R, WEAR L, WEAR R; 128 bytes) and 27
 * delta slots, one per non-base value of the shape genes (look, neck, pattern, hair, mark, weapon), each a count
 * and, if non-zero, the sprites it draws into and that many (row, mask, plane 0, plane 1) patches. The heir applies
 * at most six of them, in slot order. The data spans several banks: this code switches. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "mapper.h"
#include "heir.h"

static uint8_t buf[128];
static uint8_t want[6], nwant;
static uint8_t chunk_bank[HEIR_CHUNKS];
static volatile uint8_t up_pending, up_tile;
static uint8_t vbl_on;
static const uint8_t *ap_p;
static uint8_t ap_n;

/* ap_n patches from ap_p into buf: per patch (row, mask, plane 0, plane 1) -> buf[row * 2 + 0/1] = (b & ~mask) | p.
 * Hand-written: SDCC's loop costs ~6x this. Leaves ap_p past the patches. */
static void apply_patches(void) NAKED {
  __asm
    ld hl, #_ap_n
    ld c, (hl)
    ld hl, #_ap_p
    ld a, (hl+)
    ld h, (hl)
    ld l, a
  1$:
    ld a, (hl+)
    add a, a
    add a, #<_buf
    ld e, a
    ld a, #>_buf
    adc a, #0
    ld d, a
    ld a, (hl+)
    cpl
    ld b, a
    ld a, (de)
    and a, b
    or a, (hl)
    inc hl
    ld (de), a
    inc de
    ld a, (de)
    and a, b
    or a, (hl)
    inc hl
    ld (de), a
    dec c
    jr nz, 1$
    ld a, l
    ld (#_ap_p), a
    ld a, h
    ld (#_ap_p + 1), a
    ret
  __endasm;
}

/* the VRAM upload runs in VBlank (an add_VBL handler): composing a frame never stalls the frame on STAT waits */
static void heir_vbl(void) NONBANKED {
  uint8_t v;
  if (!up_pending) return;
  v = VBK_REG;
  VBK_REG = 0;
  memcpy((uint8_t *)(0x8000u + ((uint16_t)up_tile << 4)), buf, 128u);
  VBK_REG = v;
  up_pending = 0;
}

static void want_slot(uint8_t first, uint8_t base, uint8_t v) {
  if (v == base) return;
  want[nwant++] = (uint8_t)(first + (v < base ? v : (uint8_t)(v - 1u)));
}

void heir_compose_init(void) NONBANKED {
  nwant = 0;
  want_slot(HEIR_SLOT_LOOK, HEIR_BASE_LOOK, heir.look);
  want_slot(HEIR_SLOT_NECK, HEIR_BASE_NECK, heir.neck);
  want_slot(HEIR_SLOT_PAT, HEIR_BASE_PAT, heir.pat);
  want_slot(HEIR_SLOT_HAIR, HEIR_BASE_HAIR, heir.hair);
  want_slot(HEIR_SLOT_MARK, HEIR_BASE_MARK, heir.mark);
  want_slot(HEIR_SLOT_WEAPON, HEIR_BASE_WEAPON, heir.weapon);
  heir_chunk_banks(chunk_bank);
  if (!vbl_on) {
    vbl_on = 1;
    CRITICAL { add_VBL(heir_vbl); }
  }
}

void heir_get_frame(uint8_t state, uint8_t frame, heir_frame_t *out) NONBANKED {
  uint8_t save = CURRENT_BANK;
  uint16_t f;
  EG_ROM_BANK(BANK(heir_tab));
  f = (uint16_t)(heir_states[state].first_lo | ((uint16_t)heir_states[state].first_hi << 8)) + frame;
  *out = heir_frames[f];
  EG_ROM_BANK(save);
}

uint8_t heir_state_len(uint8_t state, uint8_t *loop) NONBANKED {
  uint8_t save = CURRENT_BANK, n;
  EG_ROM_BANK(BANK(heir_tab));
  n = heir_states[state].n;
  *loop = heir_states[state].loop;
  EG_ROM_BANK(save);
  return n;
}

uint8_t heir_compose(uint8_t pose, uint8_t dd, uint8_t tile0) NONBANKED {
  uint8_t save = CURRENT_BANK, s, n, w = 0, mask, k;
  uint16_t i = (uint16_t)pose * 5u + dd;
  while (up_pending) /* the last frame's upload has not had its VBlank yet (the LCD is off): do it now */
    if (!(LCDC_REG & LCDCF_ON)) {
      k = VBK_REG;
      VBK_REG = 0;
      memcpy((uint8_t *)(0x8000u + ((uint16_t)up_tile << 4)), buf, 128u);
      VBK_REG = k;
      up_pending = 0;
    }
  heir_rec_t r;
  const uint8_t *p;
  EG_ROM_BANK(BANK(heir_tab));
  r = heir_recs[i];
  EG_ROM_BANK(chunk_bank[r.chunk]);
  p = heir_chunk_ptr[r.chunk] + (uint16_t)(r.lo | ((uint16_t)r.hi << 8));
  mask = *p++; /* the base's non-empty sprites; a delta that draws into another one adds it below */
  memcpy(buf, p, 128u);
  p += 128u;
  for (s = 0; s != HEIR_SLOTS; s++) {
    n = *p++;
    if (!n) continue;
    k = *p++; /* the sprites this slot draws into */
    if (w != nwant && want[w] == s) {
      w++;
      mask |= k;
      ap_p = p;
      ap_n = n;
      apply_patches();
      p = ap_p;
    } else
      p += (uint16_t)n << 2;
  }
  EG_ROM_BANK(save);
  up_tile = tile0;
  up_pending = 1;
  return mask;
}

void heir_get_pal(uint8_t skin, uint8_t accent, uint16_t *out8) NONBANKED {
  uint8_t save = CURRENT_BANK, k;
  EG_ROM_BANK(BANK(heir_tab));
  for (k = 0; k != 4u; k++) out8[k] = heir_body_pal[skin][k];
  out8[5] = heir_cloth_pal[accent][0];
  out8[6] = heir_cloth_pal[accent][1];
  EG_ROM_BANK(save);
}
