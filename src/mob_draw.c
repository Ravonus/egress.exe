/* Native mob rendering: separate from the AI/rule bank so neither must discard
 * states or art to fit a 16 KiB ROM bank. The look cache is shared WRAM. */
#pragma bank 255
#include <gb/gb.h>
#include <stddef.h>
#include "play.h"
#include "mob.h"
#include "oam.h"
#include "gen/mob_art.h"
extern uint8_t mob_slot_bmask[MOB_SLOTS],mob_has_elite;

/* ---- drawing ---- */
static const int8_t bob2[4] = {0, -1, 0, 0};
/* Body and overlay use identical transformed positions. Visible coordinates
 * biased by 16 fit in bytes, including partially clipped edge sprites. */
static uint8_t draw_x[MAXE],draw_y[MAXE];

/* Complete overlays share one allocator call. Handle this common path without
 * SDCC spilling every pointer/member to its stack; partial columns retain the
 * portable path below. This leaf never switches WRAM or bypasses OAM budgets. */
static uint8_t overlay_pair(uint8_t k) NAKED {
  k;
  __asm
    push af
    ld e,a
    ld d,#0
    ld l,e
    ld h,d
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,de
    add hl,hl
    add hl,de
    ld de,#_mob+15
    add hl,de
    ld a,(hl)
    or a,a
    jp z,8$
    cp a,#4
    jp nc,8$
    dec a
    cp a,#2
    jr nz,1$
    dec a
  1$:
    ld c,a
    ld de,#-5
    add hl,de
    bit 6,(hl)
    jp nz,8$
    dec hl
    ld b,(hl)
    ld e,b
    ld d,#0
    ld hl,#_mob_slot_bmask
    add hl,de
    ld a,(hl)
    bit 0,c
    jr z,2$
    srl a
    srl a
  2$:
    and a,#3
    jp z,8$
    cp a,#3
    jp nz,9$
    ldhl sp,#1
    ld a,(hl)
    swap a
    ld e,a
    ld d,#0
    ld hl,#_en
    add hl,de
    ld a,(hl+)
    cp a,#3
    jp nz,8$
    ld a,(hl)
    cp a,#6
    jp z,8$
    ld de,#4
    add hl,de
    ld a,(hl)
    cp a,#2
    jr nz,3$
    set 7,c
  3$:
    ld a,b
    add a,a
    add a,b
    add a,a
    add a,a
    add a,a
    add a,#32
    ld b,a
    ld a,c
    and a,#1
    add a,a
    add a,a
    add a,b
    ld b,a
    push bc
    ldhl sp,#3
    ld e,(hl)
    ld d,#0
    ld hl,#_draw_x
    add hl,de
    ld a,(hl)
    sub a,#16
    ld e,a
    ld a,#0
    sbc a,#0
    ld d,a
    push de
    ldhl sp,#5
    ld e,(hl)
    ld d,#0
    ld hl,#_draw_y
    add hl,de
    ld a,(hl)
    sub a,#16
    ld c,a
    ld a,#0
    sbc a,#0
    ld b,a
    ldhl sp,#2
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    bit 7,e
    jr z,4$
    ld a,#37
    push af
    inc sp
    ld a,#254
    push af
    inc sp
    ld a,d
    add a,#2
    jr 5$
  4$:
    ld a,#5
    push af
    inc sp
    ld a,#2
    push af
    inc sp
    ld a,d
  5$:
    push af
    inc sp
    ld a,#2
    push af
    inc sp
    ldhl sp,#4
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    call _oam_group
    add sp,#6
    ld a,#1
    ret
  8$:
    add sp,#2
    ld a,#1
    ret
  9$:
    add sp,#2
    xor a,a
    ret
  __endasm;
}
typedef char native_overlay_layout[(sizeof(mob_t)==19u && offsetof(mob_t,drawn)==15u &&
  offsetof(mob_t,status)==10u && offsetof(mob_t,slot)==9u && sizeof(enemy_t)==16u &&
  offsetof(enemy_t,face)==5u && MOB_TILE0==16u && MOB_SLOT_TILES==24u && MS_UNDER==64u) ? 1 : -1];

void mob_draw(uint8_t k) BANKED {
  enemy_t *e = &en[k];
  mob_t *m = &mob[k];
  uint8_t f, pal, tile, ex = (uint8_t)(e->x >> 4), ey = (uint8_t)(e->y >> 4), flip;
  int16_t sx = (int16_t)ex - 16 - cam_x, sy = (int16_t)ey - 16 - cam_y;
  m->drawn = 0;
  if (m->status & MS_UNDER) { /* a bump in the carpet, moving */
    if (e->anim & 8u) oam_group(sx + 4, sy + 6, 1u, (uint8_t)(MOB_FX_TILE + FXT_BUMP * 2u), 0, 5u);
    return;
  }
  f = (uint8_t)((e->anim >> 4) & 1u);
  if (e->st == ES_WALK || e->st == ES_FLOAT) f = (uint8_t)((e->anim >> 3) & 1u);
  pal = (uint8_t)(2u + (m->slot & 1u));
  if (e->st == ES_TELL) {
    f = 2u;
    sx += (e->t & 2u) ? 1 : -1;
    if (e->t & 4u) pal = 7u;
  } else if (e->st == ES_LUNGE) {
    sx += fdx[e->face] * 2;
    sy += fdy[e->face] * 2;
  }
  if (m->ax.body == MB_FLYER) sy += bob2[(e->anim >> 3) & 3u];
  if ((m->flags & MOBF_ELITE) && (e->anim & 0x38u) == 0x38u) pal = 7u; /* the elite's pulse */
  if (m->status & MS_FROZEN) pal = 5u;
  if (e->flash) pal = (e->flash & 2u) ? 6u : pal;
  if(sx<=-16 || sx>=160 || sy<=-16 || sy>=144)return;
  draw_x[k]=(uint8_t)(sx+16);draw_y[k]=(uint8_t)(sy+16);
  tile = (uint8_t)(MOB_TILE0 + m->slot * MOB_SLOT_TILES + (f << 2));
  flip = (uint8_t)(e->face == F_LEFT);
  if (flip) m->drawn = oam_group(sx, sy, 2u, (uint8_t)(tile + 2u), 254u, (uint8_t)(pal | S_FLIPX));
  else m->drawn = oam_group(sx, sy, 2u, tile, 2u, pal);
  if (m->drawn) m->drawn = (uint8_t)(f + 1u);
}

/* Keep the passes separate so the simple marker walk does not spill the
 * body's pointers and coordinates throughout the loop. */
static void mob_marks(void) {
  uint8_t k;
  mob_t *m=mob;
  enemy_t *e=en;
  if(!mob_has_elite)return;
  for(k=nen;k;k--,m++,e++) {
    if(!(m->flags&MOBF_ELITE) || (m->status&MS_UNDER) ||
       e->kind!=SPAWN_MOB || e->st==ES_DEAD)continue;
    oam_group((int16_t)(e->x>>4)-12-cam_x,
              (int16_t)(e->y>>4)-25-cam_y+((e->anim>>4)&1u),
              1u,(uint8_t)(MOB_FX_TILE+FXT_CROWN*2u),0u,7u);
  }
}

/* One body's overlay does not keep the outer loop's two pointers alive across
 * every allocator call. Positions are the exact transformed body coordinates. */
static void mob_overlay(uint8_t k) {
  if(overlay_pair(k))return;
  mob_t *m=&mob[k];
  uint8_t f,b,tile,flip;
  int16_t sx,sy;
  if(!m->drawn || m->drawn>3u || (m->status&MS_UNDER) ||
     en[k].kind!=SPAWN_MOB || en[k].st==ES_DEAD)return;
  f=(uint8_t)(m->drawn-1u);
  if(f==2u)f=1u;
  b=mob_slot_bmask[m->slot];
  if(f)b>>=2;
  b&=3u;
  if(!b)return;
  tile=(uint8_t)(MOB_TILE0+m->slot*MOB_SLOT_TILES+16u+(f<<2));
  flip=(uint8_t)(en[k].face==F_LEFT);
  sx=(int16_t)draw_x[k]-16;sy=(int16_t)draw_y[k]-16;
  if(b==3u) {
    if(flip)oam_group(sx,sy,2u,(uint8_t)(tile+2u),254u,5u|S_FLIPX);
    else oam_group(sx,sy,2u,tile,2u,5u);
    return;
  }
  if(flip) {
    if(b&2u)oam_group(sx,sy,1u,(uint8_t)(tile+2u),0u,5u|S_FLIPX);
    if(b&1u)oam_group(sx+8,sy,1u,tile,0u,5u|S_FLIPX);
  } else {
    if(b&1u)oam_group(sx,sy,1u,tile,0u,5u);
    if(b&2u)oam_group(sx+8,sy,1u,(uint8_t)(tile+2u),0u,5u);
  }
}

void mobs_draw(uint8_t pass) BANKED {
  uint8_t k;
  if(pass==OP_MARK) {mob_marks();return;}
  for(k=0u;k!=nen;k++)mob_overlay(k);
}

uint8_t mob_look_draw(uint8_t slot, uint8_t f, int16_t sx, int16_t sy, uint8_t prop_or) BANKED {
  return oam_group(sx, sy, 2u, (uint8_t)(MOB_TILE0 + slot * MOB_SLOT_TILES + (f << 2)), 2u,
                   (uint8_t)((2u + (slot & 1u)) | prop_or));
}
