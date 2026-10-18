/* Two independent circuits form a dependency chain across separated islands.
 * First park a load to power transport. Across it, a second gate accepts a
 * pushed crate or one crafted brick. A paper lock still needs a HOT unit:
 * waste both found flames on plausible recipes and that final route is lost. */
#ifdef __SDCC
#pragma bank 255
#endif
#include <string.h>
#include "eg_physics.h"
#include "eg_piece.h"
static uint8_t mirror(uint8_t c) { return EG_IDX(15u-EG_X(c),EG_Y(c)); }
void eg_portal_gen(eg_room *r,uint16_t seed,uint8_t level) EG_BANKED {
  uint8_t x,y,c,k,ax=(uint8_t)(3u+(seed%3u)),ay=(uint8_t)(8u+((seed>>3)&1u));
  (void)level;
  memset(r,0,sizeof *r);
  for(y=0u;y!=EG_H;y++) for(x=0u;x!=EG_W;x++) {
    c=EG_IDX(x,y);
    r->c[c]=(x==0u || x==15u || y==13u || (x>=8u && x<=10u)) ? (T_PIT|F_ABYSS) : y==0u ? T_WALL : T_EMPTY;
  }
  r->start=EG_IDX(2u,12u);r->door=EG_IDX(12u,0u);r->c[r->door]=T_DOOR;
  for(x=11u;x!=15u;x++) {r->c[EG_IDX(x,2u)]=T_WALL;r->c[EG_IDX(x,3u)]=T_WALL;}
  r->c[EG_IDX(12u,2u)]=T_PAPER;r->c[EG_IDX(12u,3u)]=T_GATE;
  r->keys=1u;r->c[EG_IDX(12u,1u)]=T_KEY;
  r->nlink=2u;
  r->link[0].a=EG_IDX(ax,ay);r->link[0].b=r->link[0].gate=EG_LINK_NONE;
  r->link[1].a=EG_IDX(12u,5u);r->link[1].b=EG_LINK_NONE;r->link[1].gate=EG_IDX(12u,3u);
  r->c[r->link[0].a]=r->c[r->link[1].a]=T_PLATE;
  r->c[EG_IDX(ax+1u,11u)]=T_BLOCK;
  r->c[EG_IDX(13u,7u+((seed>>4)&1u))]=T_BLOCK;
  r->portal[0]=EG_IDX(ax,5u+((seed>>2)&1u));r->portal[1]=EG_IDX(12u,10u);
  r->portal_need=EG_PORTAL_ON|1u;
  r->nitem=6u;
  r->item_piece[0]=r->item_piece[1]=P_FIRE;r->item_piece[2]=P_EARTH;
  r->item_piece[3]=P_WATER;r->item_piece[4]=P_AIR;r->item_piece[5]=P_WATER;
  for(k=0u;k!=r->nitem;k++) {r->item_cell[k]=EG_IDX(3u+k,12u);r->c[r->item_cell[k]]=T_ITEM;}
  r->trick=TRICK_WEIGHT;r->support=(1u<<TRICK_BURN);r->depth=1u;
  if(seed&0x20u) {
    for(y=0u;y!=EG_H;y++) for(x=0u;x!=8u;x++) {
      c=EG_IDX(x,y);k=r->c[c];r->c[c]=r->c[mirror(c)];r->c[mirror(c)]=k;
    }
    r->start=mirror(r->start);r->door=mirror(r->door);
    for(k=0u;k!=r->nitem;k++)r->item_cell[k]=mirror(r->item_cell[k]);
    for(k=0u;k!=r->nlink;k++) {
      r->link[k].a=mirror(r->link[k].a);
      if(r->link[k].gate<EG_N)r->link[k].gate=mirror(r->link[k].gate);
    }
    r->portal[0]=mirror(r->portal[0]);r->portal[1]=mirror(r->portal[1]);
  }
  /* All apparatus is deliberate; later detail placement may decorate the
   * remaining floor but must not break the dependency graph. */
  for(c=0u;c!=EG_N;c++) if(r->c[c]!=T_EMPTY)r->prot[c>>3]|=(uint8_t)(1u<<(c&7u));
  eg_benches(r,seed);
  eg_plates_dirty=1u;
}
