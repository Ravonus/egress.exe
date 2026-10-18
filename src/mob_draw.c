/* Native mob rendering: separate from the AI/rule bank so neither must discard
 * states or art to fit a 16 KiB ROM bank. The look cache is shared WRAM. */
#pragma bank 255
#include <gb/gb.h>
#include <stddef.h>
#include "play.h"
#include "companion.h"
#include "look.h"
#include "mob.h"
#include "oam.h"
#include "gen/mob_art.h"
extern uint8_t mob_slot_bmask[MOB_SLOTS],mob_has_elite;

/* ---- drawing ---- */
/* Body and overlay use identical transformed positions. Visible coordinates
 * biased by 16 fit in bytes, including partially clipped edge sprites. */
static uint8_t draw_x[MAXE],draw_y[MAXE];

/* Overlay columns share the body coordinates and the normal allocator. Keep
 * pointers and members in registers instead of spilling them for each column.
 * This leaf never switches WRAM or bypasses OAM budgets. */
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
    ld e,a
    sla e
    sla e
    ld a,c
    or a,e
    ld c,a
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
    and a,#12
    cp a,#12
    jr nz,10$
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
    jr 6$
  10$:
    bit 3,e
    jr z,11$
    inc d
    inc d
  11$:
    bit 7,e
    jr z,12$
    bit 2,e
    jr nz,13$
    jr 14$
  12$:
    bit 3,e
    jr z,14$
  13$:
    ldhl sp,#0
    ld a,(hl)
    add a,#8
    ld (hl+),a
    ld a,(hl)
    adc a,#0
    ld (hl),a
  14$:
    ld a,#5
    bit 7,e
    jr z,15$
    or a,#32
  15$:
    push af
    inc sp
    xor a,a
    push af
    inc sp
    ld a,d
    push af
    inc sp
    ld a,#1
    push af
    inc sp
    jr 16$
  6$:
    ld a,d
  5$:
    push af
    inc sp
    ld a,#2
    push af
    inc sp
  16$:
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
  __endasm;
}
typedef char native_overlay_layout[(sizeof(mob_t)==19u && offsetof(mob_t,drawn)==15u &&
  offsetof(mob_t,status)==10u && offsetof(mob_t,slot)==9u && sizeof(enemy_t)==16u &&
  offsetof(enemy_t,face)==5u && MOB_TILE0==16u && MOB_SLOT_TILES==24u && MS_UNDER==64u &&
  SPAWN_MOB==3u && ES_DEAD==6u && F_LEFT==2u && S_FLIPX==32u && MAXE<=16u) ? 1 : -1];

/* Twelve stack bytes hold the draw packet. No WRAM bank changes or private
 * allocation path: every body still passes through the shared OAM allocator. */
static void body_fast(uint8_t k) NAKED {
  k;
  __asm
    add sp,#-12
    ldhl sp,#0
    ld (hl),a
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
    ld de,#_mob
    add hl,de
    ld a,(hl)
    ld b,#0
    cp a,#1
    jr nz,1$
    set 6,b
  1$:
    ld de,#9
    add hl,de
    ld a,(hl+)
    ld c,a
    ld d,(hl)
    inc hl
    inc hl
    inc hl
    ld e,(hl)
    inc hl
    inc hl
    ld (hl),#0
    bit 6,d
    jr z,2$
    set 7,b
  2$:
    ldhl sp,#1
    ld (hl),c
    inc hl
    ld (hl),b
    inc hl
    ld a,c
    and a,#1
    add a,#2
    ld (hl+),a
    ld (hl),d
    inc hl
    ld (hl),e
    ldhl sp,#0
    ld a,(hl)
    swap a
    ld e,a
    ld d,#0
    ld hl,#_en+1
    add hl,de
    ld a,(hl+)
    ld b,a
    ld c,(hl)
    inc hl
    inc hl
    ld d,(hl)
    inc hl
    ld e,(hl)
    inc hl
    inc hl
    ld a,(hl)
    ldhl sp,#10
    ld (hl),a
    inc hl
    ld (hl),e
    ldhl sp,#7
    ld (hl),d
    inc hl
    ld (hl),b
    inc hl
    ld (hl),c
    ld a,b
    cp a,#1
    jr z,5$
    ldhl sp,#10
    or a,a
    jr z,3$
    cp a,#5
    jr z,3$
    ld a,(hl)
    swap a
    and a,#1
    jr 4$
  3$:
    ld a,(hl)
    rrca
    rrca
    rrca
    and a,#1
  4$:
    ld b,a
    jr 6$
  5$:
    ld b,#2
    ldhl sp,#9
    bit 2,(hl)
    jr z,6$
    ldhl sp,#3
    ld (hl),#7
  6$:
    ldhl sp,#2
    ld a,(hl)
    or a,b
    ld (hl),a
    ldhl sp,#5
    bit 0,(hl)
    jr z,7$
    ldhl sp,#10
    ld a,(hl)
    and a,#56
    cp a,#56
    jr nz,7$
    ldhl sp,#3
    ld (hl),#7
  7$:
    ldhl sp,#4
    bit 3,(hl)
    jr z,8$
    ldhl sp,#3
    ld (hl),#5
  8$:
    ldhl sp,#7
    bit 1,(hl)
    jr z,9$
    ldhl sp,#3
    ld (hl),#6
  9$:
    ldhl sp,#0
    ld a,(hl)
    swap a
    ld e,a
    ld d,#0
    ld hl,#_en+8
    add hl,de
    ld a,(hl+)
    swap a
    and a,#15
    ld e,a
    ld a,(hl+)
    swap a
    and a,#240
    or a,e
    ld b,h
    ld c,l
    ld e,a
    ld a,(#_cam_x)
    add a,#16
    ld d,a
    ld a,e
    sub a,d
    ld e,a
    ld a,#0
    sbc a,#0
    ld d,a
    ldhl sp,#4
    ld (hl),e
    inc hl
    ld (hl),d
    ld h,b
    ld l,c
    ld a,(hl+)
    swap a
    and a,#15
    ld e,a
    ld a,(hl)
    swap a
    and a,#240
    or a,e
    ld e,a
    ld a,(#_cam_y)
    add a,#16
    ld d,a
    ld a,e
    sub a,d
    ld e,a
    ld a,#0
    sbc a,#0
    ld d,a
    ldhl sp,#6
    ld (hl),e
    inc hl
    ld (hl),d
    ldhl sp,#2
    bit 7,(hl)
    jr z,30$
    ldhl sp,#10
    bit 3,(hl)
    jp z,90$
    ldhl sp,#4
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    inc de
    inc de
    inc de
    inc de
    inc hl
    ld a,(hl+)
    ld c,a
    ld b,(hl)
    inc bc
    inc bc
    inc bc
    inc bc
    inc bc
    inc bc
    ld a,#5
    push af
    inc sp
    xor a,a
    push af
    inc sp
    ld a,#108
    push af
    inc sp
    ld a,#1
    push af
    inc sp
    call _oam_group
    jp 90$
  30$:
    ldhl sp,#8
    ld a,(hl)
    cp a,#1
    jr nz,32$
    ldhl sp,#9
    ld c,(hl)
    ldhl sp,#4
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    bit 1,c
    jr z,31$
    inc de
    jr 34$
  31$:
    dec de
    jr 34$
  32$:
    cp a,#2
    jr nz,35$
    ldhl sp,#11
    ld a,(hl)
    ld c,a
    ldhl sp,#4
    cp a,#2
    jr nc,33$
    inc hl
    inc hl
  33$:
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    bit 0,c
    jr z,36$
    inc de
    inc de
    jr 34$
  36$:
    dec de
    dec de
  34$:
    ld (hl),d
    dec hl
    ld (hl),e
  35$:
    ldhl sp,#2
    bit 6,(hl)
    jr z,37$
    ldhl sp,#10
    ld a,(hl)
    and a,#24
    cp a,#8
    jr nz,37$
    ldhl sp,#6
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    dec de
    ld (hl),d
    dec hl
    ld (hl),e
  37$:
    ldhl sp,#4
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    ld hl,#15
    add hl,de
    ld a,h
    or a,a
    jp nz,90$
    ld a,l
    cp a,#175
    jp nc,90$
    ld b,a
    inc b
    ldhl sp,#6
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    ld hl,#15
    add hl,de
    ld a,h
    or a,a
    jp nz,90$
    ld a,l
    cp a,#159
    jp nc,90$
    ld c,a
    inc c
    ldhl sp,#0
    ld e,(hl)
    ld d,#0
    ld hl,#_draw_x
    add hl,de
    ld (hl),b
    ld hl,#_draw_y
    add hl,de
    ld (hl),c
    ldhl sp,#1
    ld a,(hl)
    ld b,a
    add a,a
    add a,b
    add a,a
    add a,a
    add a,a
    add a,#16
    ld b,a
    inc hl
    ld a,(hl)
    and a,#3
    add a,a
    add a,a
    add a,b
    ldhl sp,#8
    ld (hl),a
    ldhl sp,#11
    ld a,(hl)
    cp a,#2
    jr z,40$
    ldhl sp,#3
    ld a,(hl)
    push af
    inc sp
    ld a,#2
    push af
    inc sp
    ldhl sp,#10
    ld a,(hl)
    jr 41$
  40$:
    ldhl sp,#3
    ld a,(hl)
    or a,#32
    push af
    inc sp
    ld a,#254
    push af
    inc sp
    ldhl sp,#10
    ld a,(hl)
    add a,#2
  41$:
    push af
    inc sp
    ld a,#2
    push af
    inc sp
    ldhl sp,#8
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    inc hl
    ld a,(hl+)
    ld c,a
    ld b,(hl)
    call _oam_group
    or a,a
    jr z,90$
    ldhl sp,#2
    ld a,(hl)
    and a,#3
    inc a
    ld b,a
    ldhl sp,#0
    ld e,(hl)
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
    ld (hl),b
  90$:
    add sp,#12
    ret
  __endasm;
}
void mob_draw(uint8_t k) BANKED {body_fast(k);}
typedef char native_body_layout[(offsetof(mob_t,ax)==0u && offsetof(mob_axes_t,body)==0u &&
  offsetof(mob_t,flags)==13u && offsetof(enemy_t,st)==1u && offsetof(enemy_t,t)==2u &&
  offsetof(enemy_t,flash)==4u && offsetof(enemy_t,anim)==7u && offsetof(enemy_t,x)==8u &&
  offsetof(enemy_t,y)==10u && MB_FLYER==1u && MS_FROZEN==8u && FXT_BUMP==10u) ? 1 : -1];

/* Keep the passes separate so the simple marker walk does not spill the
 * body's pointers and coordinates throughout the loop. */
static void mob_marks(void) {
  uint8_t k;
  mob_t *m=mob;
  enemy_t *e=en;
  if(!mob_has_elite && !companion.threat)return;
  for(k=nen;k;k--,m++,e++) {
    if((m->status&MS_UNDER) || e->kind!=SPAWN_MOB || e->st==ES_DEAD)continue;
    if(companion.threat&look_bits[(uint8_t)(nen-k)])
      oam_group((int16_t)(e->x>>4)-4-cam_x,(int16_t)(e->y>>4)-28-cam_y,1u,(uint8_t)(MOB_FX_TILE+FXT_RING*2u),0u,4u);
    if(!(m->flags&MOBF_ELITE))continue;
    oam_group((int16_t)(e->x>>4)-12-cam_x,
              (int16_t)(e->y>>4)-25-cam_y+((e->anim>>4)&1u),
              1u,(uint8_t)(MOB_FX_TILE+FXT_CROWN*2u),0u,7u);
  }
}

void mobs_draw(uint8_t pass) BANKED {
  uint8_t k;
  if(pass==OP_MARK) {mob_marks();return;}
  for(k=0u;k!=nen;k++)overlay_pair(k);
}

uint8_t mob_look_draw(uint8_t slot, uint8_t f, int16_t sx, int16_t sy, uint8_t prop_or) BANKED {
  return oam_group(sx, sy, 2u, (uint8_t)(MOB_TILE0 + slot * MOB_SLOT_TILES + (f << 2)), 2u,
                   (uint8_t)((2u + (slot & 1u)) | prop_or));
}
