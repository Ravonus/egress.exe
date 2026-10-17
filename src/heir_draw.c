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
extern uint8_t world_on,game_mode;
uint8_t heir_cache_get(uint8_t pose,uint8_t dd) BANKED;
void heir_cache_put(uint8_t pose,uint8_t dd,uint8_t mask) BANKED;

uint8_t heir_pixels[128];
uint8_t heir_cache_enabled;
static uint8_t want[7], nwant; /* six selected slots and a sentinel */
static uint8_t chunk_bank[HEIR_CHUNKS];
volatile uint8_t heir_pending;
static uint8_t up_tile;
static uint8_t vbl_on;
static const uint8_t *ap_p;
static uint8_t ap_n;
static uint8_t delta_mask;

/* The base and VBlank upload are always exactly 128 bytes. */
static void copy128(void *dst,const void *src) NONBANKED NAKED {
  dst;src;
  __asm
    ld h,b
    ld l,c
    ld b,#16
  1$:
    .rept 8
      ld a,(hl+)
      ld (de),a
      inc de
    .endm
    dec b
    jr nz,1$
    ret
  __endasm;
}

/* Sprite buffers start at tile 0 or 8: their 128 bytes never cross a page.
 * The VBlank upload can use byte increments, saving 512 CPU cycles. */
static void upload128(void *dst,const void *src) NONBANKED NAKED {
  dst;src;
  __asm
    ld h,b
    ld l,c
    ld b,#16
  1$:
    .rept 8
      ld a,(hl+)
      ld (de),a
      inc e
    .endm
    dec b
    jr nz,1$
    ret
  __endasm;
}

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
    add a, #<_heir_pixels
    ld e, a
    ld a, #>_heir_pixels
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

/* Walk the compressed stream once. Advancing past a selected empty delta is
 * essential: that gene may add nothing to this pose, while later genes do. */
static uint8_t compose_deltas(void) NONBANKED NAKED {
  __asm
    ld hl,#_ap_p
    ld a,(hl+)
    ld h,(hl)
    ld l,a
    ld de,#_want
    ld b,#0
    ld a,(de)
    cp a,#27
    jr z,6$
  1$:
    ld a,(hl+)
    ld c,a
    ld a,(de)
    cp a,b
    jr nz,3$
    inc de
    ld a,c
    or a,a
    jr z,4$
    ld a,(hl+)
    push hl
    ld hl,#_delta_mask
    or a,(hl)
    ld (hl),a
    pop hl
    ld a,c
    ld (#_ap_n),a
    ld a,l
    ld (#_ap_p),a
    ld a,h
    ld (#_ap_p+1),a
    push bc
    push de
    call _apply_patches
    pop de
    pop bc
  4$:
    ld a,(de)
    cp a,#27
    jr z,6$
    jr 5$
  3$:
    ld a,c
    or a,a
    jr z,5$
    inc hl
    push de
    ld e,c
    ld d,#0
    sla e
    rl d
    sla e
    rl d
    add hl,de
    pop de
  5$:
    inc b
    ld a,b
    cp a,#27
    jr nz,1$
  6$:
    ld a,(#_delta_mask)
    ret
  __endasm;
}
typedef char native_heir_slot_count[(HEIR_SLOTS==27u) ? 1 : -1];

/* the VRAM upload runs in VBlank (an add_VBL handler): composing a frame never stalls the frame on STAT waits */
static void heir_vbl(void) NONBANKED {
  uint8_t v;
  if (!heir_pending) return;
  v = VBK_REG;
  VBK_REG = 0;
  upload128((uint8_t *)(0x8000u + ((uint16_t)up_tile << 4)),heir_pixels);
  VBK_REG = v;
  heir_pending = 0;
}

static void want_slot(uint8_t first, uint8_t base, uint8_t v) {
  if (v == base) return;
  want[nwant++] = (uint8_t)(first + (v < base ? v : (uint8_t)(v - 1u)));
}

void heir_compose_init(void) NONBANKED {
  heir_cache_enabled=0u;
  nwant = 0;
  want_slot(HEIR_SLOT_LOOK, HEIR_BASE_LOOK, heir.look);
  want_slot(HEIR_SLOT_NECK, HEIR_BASE_NECK, heir.neck);
  want_slot(HEIR_SLOT_PAT, HEIR_BASE_PAT, heir.pat);
  want_slot(HEIR_SLOT_HAIR, HEIR_BASE_HAIR, heir.hair);
  want_slot(HEIR_SLOT_MARK, HEIR_BASE_MARK, heir.mark);
  want_slot(HEIR_SLOT_WEAPON, HEIR_BASE_WEAPON, heir.weapon);
  want[nwant]=HEIR_SLOTS;
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
  uint8_t save = CURRENT_BANK, mask, k;
  uint16_t i = (uint16_t)pose * 5u + dd;
  while (heir_pending) /* the last frame's upload has not had its VBlank yet (the LCD is off): do it now */
    if (!(LCDC_REG & LCDCF_ON)) {
      k = VBK_REG;
      VBK_REG = 0;
      copy128((uint8_t *)(0x8000u + ((uint16_t)up_tile << 4)),heir_pixels);
      VBK_REG = k;
      heir_pending = 0;
    }
  heir_rec_t r;
  const uint8_t *p;
  if(game_mode==2u && !world_on) {
    mask=heir_cache_get(pose,dd);
    if(mask&128u) {up_tile=tile0;heir_pending=1u;return (uint8_t)(mask&15u);}
  }
  EG_ROM_BANK(BANK(heir_tab));
  r = heir_recs[i];
  EG_ROM_BANK(chunk_bank[r.chunk]);
  p = heir_chunk_ptr[r.chunk] + (uint16_t)(r.lo | ((uint16_t)r.hi << 8));
  delta_mask = *p++; /* a selected delta may draw into an initially empty sprite */
  copy128(heir_pixels,p);
  p += 128u;
  ap_p=p;
  mask=compose_deltas();
  EG_ROM_BANK(save);
  if(game_mode==2u && !world_on)heir_cache_put(pose,dd,mask);
  up_tile = tile0;
  heir_pending = 1;
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
