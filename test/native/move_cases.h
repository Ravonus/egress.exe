#ifndef EG_MOVE_CASES_H
#define EG_MOVE_CASES_H
#include "core/eg_move.h"
#define MOVE_CASES 24u
#define MOVE_STEPS 192u
#define MOVE_BATCH 16u
static uint8_t *mc_cells,*mc_map;
static eg_grid_t mc_grid;
static eg_mover_t mc_mover;
static void move_case(uint8_t k,uint8_t *cells,uint8_t *map) {
  static const uint8_t widths[6]={16u,16u,8u,10u,32u,16u};
  static const uint8_t heights[6]={14u,20u,12u,8u,8u,16u};
  static const uint8_t strides[6]={4u,4u,3u,0u,5u,4u};
  uint8_t x,y,v;uint16_t i;
  mc_cells=cells;mc_map=map;
  mc_grid.w=widths[k%6u];mc_grid.h=heights[k%6u];mc_grid.wshift=strides[k%6u];
  mc_grid.cells=cells;mc_grid.map=k&2u ? map : 0;
  for(i=0u;i!=256u;i++)map[i]=(uint8_t)(i&3u);
  for(y=0u;y!=mc_grid.h;y++)for(x=0u;x!=mc_grid.w;x++) {
    v=(!x || !y || x==mc_grid.w-1u || y==mc_grid.h-1u ||
       (x==7u && y>=3u && y<8u) || (x>=4u && x<8u && y==6u)) ? EG_C_WALL : EG_C_FREE;
    if(x==4u && y==2u)v=EG_C_BLOCK;
    if(mc_grid.map)v|=(uint8_t)(((x*13u+y*7u+k)&63u)<<2);
    cells[(uint16_t)y*mc_grid.w+x]=v;
  }
  eg_move_init(&mc_mover,544,(int16_t)((k%6u==1u ? 17u : 2u)*256u+32u),(uint8_t)(k&1u));
}
static void move_sample(uint8_t k,uint8_t frame) {
  static const uint8_t dirs[16]={1u,9u,8u,10u,2u,6u,4u,5u,0u,3u,12u,15u,1u,8u,2u,4u};
  static const uint8_t speeds[8]={16u,24u,28u,48u,64u,96u,7u,31u};
  uint8_t dir=dirs[(uint8_t)(frame/12u+k)%16u],speed=speeds[(uint8_t)(frame/24u+k)%8u];
  int16_t x,y;uint16_t from,to;
  eg_move_step(&mc_mover,dir,speed,&mc_grid);
  if(!mc_mover.req)return;
  x=mc_mover.req_cx;y=mc_mover.req_cy;
  from=(uint16_t)y*mc_grid.w+(uint16_t)x;
  if(mc_mover.req_dir==1u)x++;else if(mc_mover.req_dir==2u)x--;
  else if(mc_mover.req_dir==4u)y--;else y++;
  if(eg_grid_cell(&mc_grid,x,y)!=EG_C_FREE)return;
  to=(uint16_t)y*mc_grid.w+(uint16_t)x;
  mc_cells[to]=mc_cells[from];mc_cells[from]=0u;
}
#endif
