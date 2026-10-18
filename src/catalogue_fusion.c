/* Crucible's contour/neck fusion, sampled at native 16x16 then doubled to
 * 32x32. One shared renderer covers every recipe; two complete inactive
 * body buffers are published together. Scratch is paused-only SRAM. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "catalogue_art.h"
#include "mapper.h"
#include "save_map.h"
#define PIX(s) ((uint8_t *)(SAVE_FUSION_SCRATCH+(uint16_t)(s)*256u))
#define OUT ((uint8_t *)(SAVE_FUSION_SCRATCH+768u))
#define SP(s,y,e) (((uint8_t *)(SAVE_FUSION_SCRATCH+1024u))[(s)*32u+(y)*2u+(e)])
#define BOUND(s,e) (((uint8_t *)(SAVE_FUSION_SCRATCH+1120u))[(s)*2u+(e)])
#define RAW ((uint8_t *)(SAVE_FUSION_SCRATCH+SAVE_FUSION_BYTES))
/* Reserve no live WRAM. The row machine runs only with SRAM enabled. */
uint8_t *__at(0xB4A6) cf_src;
uint8_t *__at(0xB4A8) cf_dst;
uint8_t __at(0xB4AA) cf_lead;
uint8_t __at(0xB4AB) cf_n;
uint8_t __at(0xB4AC) cf_sf;
uint8_t __at(0xB4AD) cf_si;
uint8_t __at(0xB4AE) cf_fill;
uint8_t __at(0xB4AF) cf_k;
uint8_t __at(0xB4B0) cf_frac;
const uint8_t cf_twice[16]={0,3,12,15,48,51,60,63,192,195,204,207,240,243,252,255};
static const uint16_t recip[17]={0,256,128,85,64,51,43,37,32,28,26,23,21,20,18,17,16};
static const int8_t curl[16]={0,1,1,2,1,0,-1,-2,-1,0,1,1,0,-1,-1,0};
/* ~190 cycles per drawn pixel; no multiply or divide in this inner loop.
 * Only L advances: each specimen is page-aligned and each sampled row fits. */
static void row_render(void) __naked {
 __asm
 ld a, (_cf_n)
 or a, a
 ret Z
 ld b, a
 ld de, #0x0000
 ld a, (_cf_lead)
 ld (_cf_k), a
 ld hl, #_cf_src
 ld a, (hl+)
 ld h, (hl)
 ld l, a
 ld a, (_cf_frac)
 ld c, a
1$:
 ld a, (hl)
 or a, a
 jr NZ, 2$
 ld a, (_cf_fill)
2$:
 rra
 rl d
 rra
 rl e
 ld a, (_cf_sf)
 add a, c
 ld c, a
 ld a, (_cf_si)
 adc a, l
 ld l, a
 ld a, (_cf_k)
 inc a
 ld (_cf_k), a
 cp a, #0x08
 jr NZ, 3$
 call 4$
3$:
 dec b
 jr NZ, 1$
 ld a, (_cf_k)
 or a, a
 ret Z
 ld c, a
5$:
 ld a, c
 cp a, #0x08
 jr Z, 4$
 sla d
 sla e
 inc c
 jr 5$
4$:
 push hl
 ld hl, #_cf_dst
 ld a, (hl+)
 ld h, (hl)
 ld l, a
 ld a, d
 ld (hl+), a
 ld (hl), e
 ld hl, #_cf_dst
 ld a, (hl)
 add a, #0x20
 ld (hl+), a
 ld a, (hl)
 adc a, #0x00
 ld (hl), a
 xor a, a
 ld (_cf_k), a
 ld de, #0x0000
 pop hl
 ret
 __endasm;
}
/* Inflate each native bit to a two-pixel pair, preserving the 8x16 OAM order. */
static void inflate_tile(void) __naked {
 __asm
 ld a, #0x08
 ld (_cf_n), a
1$:
 ld hl, #_cf_src
 ld a, (hl+)
 ld h, (hl)
 ld l, a
 ld a, (hl+)
 ld c, a
 ld a, (hl+)
 ld b, a
 ld a, l
 ld (_cf_src), a
 ld a, h
 ld (_cf_src+1), a
 ld hl, #_cf_dst
 ld a, (hl+)
 ld e, a
 ld d, (hl)
 ld a, c
 swap a
 and a, #0x0f
 call 2$
 ld (de), a
 inc de
 inc de
 ld (de), a
 dec de
 ld a, b
 swap a
 and a, #0x0f
 call 2$
 ld (de), a
 inc de
 inc de
 ld (de), a
 ld a, e
 add a, #0x1d
 ld e, a
 ld a, d
 adc a, #0x00
 ld d, a
 ld a, c
 and a, #0x0f
 call 2$
 ld (de), a
 inc de
 inc de
 ld (de), a
 dec de
 ld a, b
 and a, #0x0f
 call 2$
 ld (de), a
 inc de
 inc de
 ld (de), a
 ld a, e
 sub a, #0x1f
 ld e, a
 ld a, d
 sbc a, #0x00
 ld d, a
 ld hl, #_cf_dst
 ld a, e
 ld (hl+), a
 ld (hl), d
 ld hl, #_cf_n
 dec (hl)
 jp NZ, 1$
 ret
2$:
 push bc
 ld c, a
 ld b, #0x00
 ld hl, #_cf_twice
 add hl, bc
 ld a, (hl)
 pop bc
 ret
 __endasm;
}
static void enlarge(void){
 static const uint8_t offsets[4]={0u,128u,64u,192u};uint8_t k;
 for(k=0u;k<4u;k++){cf_src=RAW+k*16u;cf_dst=OUT+offsets[k];inflate_tile();}
}
static void decode(uint8_t slot){
 uint8_t y,x,k,lo,hi,v,l,r,top=15u,bottom=0u,any=0u;uint16_t at;uint8_t *dst=PIX(slot);
 for(y=0u;y<16u;y++){
  l=16u;r=0u;
  for(k=0u;k<2u;k++){
   at=k*32u+(y&8u)*2u+(y&7u)*2u;lo=RAW[at];hi=RAW[at+1u];
   for(x=0u;x<8u;x++){
    v=(uint8_t)((lo>>7)|((hi>>6)&2u));*dst++=v;lo<<=1;hi<<=1;
    if(v){if(l==16u)l=k*8u+x;r=k*8u+x;}
   }
  }
  SP(slot,y,0u)=l;SP(slot,y,1u)=r;
  if(l<16u){if(!any)top=y;any=1u;bottom=y;}
 }
 BOUND(slot,0u)=any ? top : 7u;BOUND(slot,1u)=any ? bottom : 8u;
}
void catalogue_bench_body(eg_item_id id,uint8_t pose,uint8_t tile) BANKED {
 EG_SRAM_OPEN();catalogue_art_raw(id,pose,RAW);enlarge();VBK_REG=1u;set_sprite_data(tile,16u,OUT);VBK_REG=0u;EG_SRAM_CLOSE();
}
void catalogue_fusion_begin(eg_item_id a,eg_item_id b,eg_item_id result) BANKED {
 EG_SRAM_OPEN();catalogue_art_raw(a,0u,RAW);decode(0u);catalogue_art_raw(b,0u,RAW);decode(1u);catalogue_art_raw(result,0u,RAW);decode(2u);EG_SRAM_CLOSE();
}
static int16_t between(int16_t a,int16_t b,uint8_t t){return a+(((b-a)*(int16_t)t)>>4);}
void catalogue_fusion_step(uint8_t step,uint8_t tile) BANKED {
 uint8_t side,y,sy,ty,top,bottom,span,seam;int16_t sl,sr,tl,tr,dl,dr,neck,bend,cut;uint16_t syfp,tyfp,systep,tystep,spos,sstep;
 if(step>16u)step=16u;EG_SRAM_OPEN();
 for(side=0u;side<2u;side++){
  memset(RAW,0,64u);
  if(step==0u || step==16u){
   for(y=0u;y<16u;y++){
    cf_src=PIX(step ? 2u : side)+(uint16_t)y*16u+(step ? side*8u : 0u);
    cf_dst=RAW+(y&8u)*2u+(y&7u)*2u+(step ? side*32u : 0u);
    cf_lead=cf_sf=cf_frac=cf_fill=0u;cf_si=1u;cf_n=step ? 8u : 16u;row_render();
   }
   enlarge();VBK_REG=1u;set_sprite_data(tile+side*16u,16u,OUT);VBK_REG=0u;continue;
  }
  top=(uint8_t)between(BOUND(side,0u),BOUND(2u,0u),step);bottom=(uint8_t)between(BOUND(side,1u),BOUND(2u,1u),step);
  if(bottom<=top)bottom=top+1u;
  systep=(uint16_t)(BOUND(side,1u)-BOUND(side,0u))*recip[bottom-top];tystep=(uint16_t)(BOUND(2u,1u)-BOUND(2u,0u))*recip[bottom-top];syfp=(uint16_t)BOUND(side,0u)<<8;tyfp=(uint16_t)BOUND(2u,0u)<<8;
  for(y=top;y<=bottom && y<16u;y++,syfp+=systep,tyfp+=tystep){
   sy=syfp>>8;ty=tyfp>>8;if(sy>15u)sy=15u;if(ty>15u)ty=15u;
   sl=SP(side,sy,0u);sr=SP(side,sy,1u);tl=SP(2u,ty,0u);tr=SP(2u,ty,1u);
   if(sl>sr)sl=sr=8;if(tl>tr)tl=tr=8;
   dl=between(sl,tl,step);dr=between(sr,tr,step);
   neck=(int16_t)(step<=8u ? step : 16u-step)*(8-(y>8u ? y-8u : 8u-y));neck>>=4;
   bend=step>2u && step<14u ? curl[(y+step)&15u]/2 : 0;
   if(side)dl-=neck;else dr+=neck;dl+=bend;dr+=bend;
   if(dl<0)dl=0;if(dr>15)dr=15;
   if(dr<dl)continue;
   span=(uint8_t)(dr-dl);sstep=span ? (uint16_t)(sr-sl)*recip[span] : 0u;spos=(uint16_t)sl<<8;
   /* Texture progressively becomes the product while retaining both contours. */
   if(step>8u && ((y*3u)&7u)<step-8u){spos=(uint16_t)tl<<8;sstep=span ? (uint16_t)(tr-tl)*recip[span] : 0u;cf_src=PIX(2u)+(uint16_t)ty*16u;}
   else cf_src=PIX(side)+(uint16_t)sy*16u;
   if(step>8u){
    seam=(uint8_t)(8+(step==16u ? 0 : curl[(y+step*2u)&15u]/2));
    if(side){cut=between(0,seam,(step-8u)*2u);if(dl<cut){spos+=(uint16_t)(cut-dl)*sstep;dl=cut;}}
    else {cut=between(16,seam,(step-8u)*2u)-1;if(dr>cut)dr=cut;}
   }
   if(dr<dl)continue;
   cf_src+=(uint8_t)(spos>>8);cf_frac=(uint8_t)spos;
   cf_dst=RAW+(y&8u)*2u+(y&7u)*2u+((dl&8u)<<2);cf_lead=dl&7u;cf_n=(uint8_t)(dr-dl+1);cf_sf=(uint8_t)sstep;cf_si=sstep>>8;cf_fill=step>=6u ? 2u : 0u;row_render();
  }
  enlarge();VBK_REG=1u;set_sprite_data(tile+side*16u,16u,OUT);VBK_REG=0u;
 }
 EG_SRAM_CLOSE();
}
