#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "play.h"
#include "puzzle.h"
#include "look.h"
#include "world.h"
#include "world_play.h"
#include "stage.h"
extern volatile uint8_t video_epoch;
extern uint8_t frame_epoch;
uint8_t player_portal_pending(void) BANKED;
typedef struct {int8_t dir;uint8_t n,part,bg,save;} preparation;
static preparation px,py;
static uint8_t turn,told;
static uint8_t spare(uint8_t last_line) {
  uint8_t ly=LY_REG;
  return video_epoch==frame_epoch && (ly<last_line || ly>=144u);
}
uint16_t world_seams;
uint8_t world_moved;
static uint8_t priority_axis(void) {
  uint8_t mx=pl_x<128u ? pl_x : (uint8_t)(255u-pl_x);
  uint8_t my=pl_y<112u ? pl_y : (uint8_t)(223u-pl_y);
  uint8_t c=cell_at(pl_x,pl_y),portal=(uint8_t)((c==room.portal[0] && room.portal[1]>=EG_N) || (c==room.portal[1] && room.portal[0]>=EG_N));
  uint8_t horizontal=joy&(J_LEFT|J_RIGHT),vertical=joy&(J_UP|J_DOWN);
  uint8_t n=portal ? world_plan.vertical : vertical && !horizontal ? 1u : horizontal && !vertical ? 0u : my<mx;
  preparation *p=n ? &py : &px;
  /* Finish the requested strip before binding a speculative perpendicular
   * one. Cell updates can invalidate that binding every tick;
   * rebuilding it must not consume the approaching edge's generation time. */
  if(!p->dir || (!(horizontal|vertical) && p->n!=(n ? 16u : 14u)))n^=1u;
  return n;
}
static void bind_requested(void) {
  uint8_t n=priority_axis();preparation *p=n ? &py : &px;
  if(p->dir && p->n==(n ? 16u : 14u) && !world_binding_ready(n ? 0 : p->dir*4,n ? p->dir*4 : 0))
    world_prepare_binding(n ? 0 : p->dir*4,n ? p->dir*4 : 0);
}
uint8_t world_work_pending(void) BANKED {
  preparation *p;uint8_t n;
  /* Presentation yields only to work that can run this frame. A fight or
   * fall suspends prefetch; its pending strips must not starve rim arrivals. */
  if(!world_on || !pl_hp || pl_state==PS_FALL || pl_state==PS_EXIT || enemies_alive || player_portal_pending())return 0u;
  if((px.dir && px.n<14u) || (py.dir && py.n<16u))return 1u;
  n=priority_axis();p=n ? &py : &px;
  return p->dir && !world_binding_ready(n ? 0 : p->dir*4,n ? p->dir*4 : 0);
}
void world_play_reset(void) BANKED {
  memset(&px,0,sizeof px);memset(&py,0,sizeof py);turn=told=0u;world_seams=0u;
  if(world_on) {
    room_origin_x=world_view.x;room_origin_y=world_view.y;room_width=world_view.width;room_height=world_view.height;
    grid_shift_view(0,0);
  }
}
static int8_t wanted(uint8_t vertical) {
  uint8_t position=vertical ? pl_y : pl_x,origin=vertical ? world_view.y : world_view.x;
  uint8_t max=vertical ? world_view.height-EG_H : world_view.width-EG_W;
  /* A vertical strip has sixteen packets. Start it a tile earlier so a
   * sprint out of a horizontal turn can finish before the swept feet arrive. */
  if(joy&(vertical ? J_DOWN : J_RIGHT))return origin<max && position>=(vertical ? 32u : 64u) ? 1 : 0;
  if(joy&(vertical ? J_UP : J_LEFT))return origin && position<=192u ? -1 : 0;
  if(origin && position<=96u)return -1;
  if(origin<max && position>=(vertical ? 144u : 160u))return 1;
  if(joy&(J_UP|J_DOWN|J_LEFT|J_RIGHT))return 0;
  /* Warm the return side while approaching or standing on a portal. Its
   * arrival must not leave the body waiting at an unprepared view edge. */
  if(origin && position<=112u)return -1;
  if(origin<max && position>=(vertical ? 144u : 176u))return 1;
  return 0;
}
static void choose(preparation *p,int8_t dir) {
  if(!dir || p->dir==dir)return;
  p->dir=dir;p->n=p->part=p->bg=p->save=0u;
}
static void chunk(preparation *p,uint8_t vertical,uint8_t *x,uint8_t *y) {
  if(vertical) {
    *x=(uint8_t)(world_view.x+(p->n&3u)*4u);
    *y=(uint8_t)(world_view.y+(p->dir>0 ? 14 : -4)+(p->n>>2));
  } else {
    *x=(uint8_t)(world_view.x+(p->dir>0 ? 16 : -4));*y=(uint8_t)(world_view.y+p->n);
  }
}
static uint8_t generate_one(preparation *p,uint8_t vertical,uint8_t *packet) {
  uint8_t x,y,k;eg_land_cell c;
  if(!p->dir || p->n>=(vertical ? 16u : 14u))return 0u;
  chunk(p,vertical,&x,&y);
  if(!p->part && world_cached(x,y)) {p->n++;return 1u;}
  if(!spare(50u))return 0u;
  k=p->part;if(!k)packet[8]=0u;
  eg_land_at(&world_plan,(uint8_t)(x+k),y,&c);packet[k]=c.material;packet[k+4u]=world_skin(c.design,c.variant);
  if(c.flags&EL_PROTECTED)packet[8]|=look_bits[k];
  if(++p->part==4u) {
    /* A timed reaction or other global mutation may have initialized this
     * chunk while its cosmetic prefetch was in progress. Never overwrite it. */
    if(!world_cached(x,y)) {
      memcpy(eg_window_packet,packet,9u);view_store(0,x,y,eg_window_packet);world_mark_cached(x,y);
    }
    p->part=0u;p->n++;
  }
  return 2u;
}
static uint8_t clear_one(preparation *p,uint8_t vertical) {
  uint8_t x,y,cam=vertical ? cam_y : cam_x;
  if(!p->dir || (!(p->bg&128u) && p->bg>=(vertical ? 64u : 56u)))return 0u;
  /* South retires padding rows 14/15 and real rows 0/1. The latter are
   * completely outside the 128px view once SCY reaches 32, unlike the four
   * real columns retired by a horizontal transfer. */
  if(p->dir>0 ? cam<(vertical ? 32u : 64u) : cam>(vertical ? 64u : 32u))return 0u;
  if(p->bg&128u) {
    /* A horizontal transfer retains the perpendicular cleared strip. Only
     * the two padding rows in its new four-column corner need clearing. */
    x=(uint8_t)((p->bg&16u ? 0u : 12u)+(p->bg&3u));y=(uint8_t)(14u+((p->bg&7u)>>2));
    look_stream_void(EG_IDX(x,y));
    if(!(++p->bg&7u))p->bg=64u;
    return 1u;
  }
  if(vertical) {
    x=p->bg&15u;
    /* South uses old rows 14,15,0,1. North uses 12,13,14,15.
     * The padding rows are real hardware slots, not simulation cells. */
    y=(uint8_t)((p->dir>0 ? 14u : 12u)+(p->bg>>4));
  } else {x=(uint8_t)((p->dir>0 ? 0u : 12u)+(p->bg&3u));y=p->bg>>2;}
  look_stream_void(EG_IDX(x,y));p->bg++;return 1u;
}
static void effects(int8_t dx,int8_t dy) {
  uint8_t k;int16_t x,y;
  if(orbs[0].live|orbs[1].live|orbs[2].live|orbs[3].live|orbs[4].live|orbs[5].live|orbs[6].live|orbs[7].live)
  for(k=0u;k<MAXO;k++)if(orbs[k].live) {
    x=(int16_t)orbs[k].x-(int16_t)dx*256;y=(int16_t)orbs[k].y-(int16_t)dy*256;
    if((uint16_t)x>=4096u || (uint16_t)y>=3584u)orbs[k].live=0u;
    else {orbs[k].x=(uint16_t)x;orbs[k].y=(uint16_t)y;}
  }
  if(puffs[0].t|puffs[1].t|puffs[2].t|puffs[3].t|puffs[4].t|puffs[5].t)
  for(k=0u;k<MAXP;k++)if(puffs[k].t) {
    x=(int16_t)puffs[k].x-(int16_t)dx*16;y=(int16_t)puffs[k].y-(int16_t)dy*16;
    if((uint16_t)x>=256u || (uint16_t)y>=224u)puffs[k].t=0u;
    else {puffs[k].x=(uint8_t)x;puffs[k].y=(uint8_t)y;}
  }
}
static uint8_t ready(preparation *p,uint8_t vertical) {
  uint8_t position=vertical ? pl_y : pl_x,cam=vertical ? cam_y : cam_x;
  uint8_t toward=joy&(vertical ? (p->dir>0 ? J_DOWN : J_UP) : (p->dir>0 ? J_RIGHT : J_LEFT));
  uint8_t c=cell_at(pl_x,pl_y),portal=(uint8_t)((c==room.portal[0] && room.portal[1]>=EG_N) || (c==room.portal[1] && room.portal[0]>=EG_N));
  return p->dir && p->n==(vertical ? 16u : 14u) && p->bg==(vertical ? 64u : 56u) &&
    world_binding_ready(vertical ? 0 : p->dir*4,vertical ? p->dir*4 : 0) &&
    world_preserved(vertical ? 0 : p->dir,vertical ? p->dir : 0) &&
    (toward || portal || (p->dir>0 ? position>=(vertical ? 208u : 240u) : position<=16u)) &&
    (p->dir>0 ? (position>=(vertical ? 144u : 160u) && cam>=64u) : (position<=96u && cam<=32u));
}
uint8_t world_frame_begin(void) BANKED {
  int8_t dx=0,dy=0;
  uint8_t mx,my;
  preparation retained;
  world_moved=0u;
  if(!world_on || !pl_hp || pl_state==PS_FALL || pl_state==PS_EXIT || freeze || grav_t==1u)return 0u;
  /* Clock polling and pending VBlank uploads sometimes consume the start of
   * this frame. Publish a prepared seam only while its complete budget fits. */
  if(LY_REG<144u || LY_REG>149u)return 0u;
  /* A generated encounter owns this window until its native actor pool is
   * cleared. Keep its art/physics budget ahead of speculative map work. */
  if(enemies_alive) {
    if(stage_goal[0]!='F' && (((joy&J_RIGHT) && pl_x>208u && world_view.x<world_view.width-EG_W) ||
       ((joy&J_LEFT) && pl_x<48u && world_view.x) || ((joy&J_DOWN) && pl_y>176u && world_view.y<world_view.height-EG_H) ||
       ((joy&J_UP) && pl_y<64u && world_view.y))) {hud_msg("CLEAR THIS FIGHT");stage_goal[0]='F';}
    return 0u;
  }
  mx=pl_x<128u ? pl_x : (uint8_t)(255u-pl_x);
  my=pl_y<112u ? pl_y : (uint8_t)(223u-pl_y);
  /* A warm orthogonal strip does not imply a requested camera move. Among
   * actual movement/portal transfers, publish the tighter edge first. */
  if(my<mx) {
    if(ready(&py,1u))dy=(int8_t)(py.dir*4);
    else if(ready(&px,0u))dx=(int8_t)(px.dir*4);
    else {bind_requested();return 0u;}
  } else {
    if(ready(&px,0u))dx=(int8_t)(px.dir*4);
    else if(ready(&py,1u))dy=(int8_t)(py.dir*4);
    else {bind_requested();return 0u;}
  }
  if(!world_shift_prepared(dx,dy))return 0u;
  retained=dx ? py : px;
  room_origin_x=world_view.x;room_origin_y=world_view.y;
  cam_x=(uint8_t)(cam_x-dx*16);cam_y=(uint8_t)(cam_y-dy*16);
  player_shift_view(dx,dy);puzzle_shift_view(dx,dy);effects(dx,dy);grid_shift_view(dx,dy);look_shift_view(dx,dy);
  memset(&px,0,sizeof px);memset(&py,0,sizeof py);
  if(retained.dir) {
    preparation *p=dx ? &py : &px;
    p->dir=retained.dir;
    if(dx) {if(retained.bg==64u)p->bg=dx>0 ? 128u : 144u;}
    else if(retained.bg==56u)p->bg=56u;
  }
  world_seams++;world_moved=1u;return 1u;
}
static void goal_hint(void) {
  uint8_t k,u=world_plan.vertical ? (uint8_t)(world_view.y+(pl_y>>4)) : (uint8_t)(world_view.x+(pl_x>>4));
  for(k=0u;k<world_plan.nnode;k++)if(!(told&look_bits[k]) && world_plan.node[k].at>=u && world_plan.node[k].at-u<=8u) {
    told|=look_bits[k];
    switch(world_plan.node[k].kind) {
      case EL_LOAD:hud_msg("WEIGHT HOLDS GATE");break;
      case EL_HEAT:hud_msg("HEAT BURNS PAPER");break;
      case EL_COLD:hud_msg("FREEZE, FILL, ROLL");break;
      case EL_POWER:hud_msg("SHOCK THE SOCKET");break;
      case EL_PORTAL:hud_msg("POWER THE PORTAL");break;
    }
    break;
  }
}
static uint8_t pickup_cell(uint8_t x,uint8_t y) {
  uint8_t t=room.c[EG_IDX(x,y)]&T_MASK;
  return (uint8_t)(t==T_KEY || t==T_ITEM || t==T_TOKEN || t==T_DIRT);
}
static uint8_t pickup_ahead(void) {
  /* A seven-pixel sweep covers the fastest evade and a landing. Pickups can
   * compose a complete HUD message later in player_update; reserve that turn
   * before admitting an expensive inactive-map query. */
  uint8_t x0=pl_x<7u ? 0u : (pl_x-7u)>>4;
  uint8_t x1=pl_x>248u ? 15u : (pl_x+7u)>>4;
  uint8_t y0=pl_y<7u ? 0u : (pl_y-7u)>>4;
  uint8_t y1=pl_y>216u ? 13u : (pl_y+7u)>>4;
  return (uint8_t)(pickup_cell(x0,y0) || (x1!=x0 && pickup_cell(x1,y0)) ||
    (y1!=y0 && (pickup_cell(x0,y1) || (x1!=x0 && pickup_cell(x1,y1)))));
}
void world_prefetch(void) BANKED {
  uint8_t n,did,axes=0u,mx,my;int8_t x,y;
  if(!world_on || !pl_hp || pl_state==PS_FALL || pl_state==PS_EXIT || enemies_alive)return;
  if(player_portal_pending())return;
  /* Interaction/attack frames own their budget; do not stack speculative
   * seed queries on a fresh action. */
  if(((joy^joy_prev)&(J_A|J_B|J_START|J_SELECT)) || pl_state==PS_LASH || pl_state==PS_SPIN || push_t || pickup_ahead())return;
  x=wanted(0u);y=wanted(1u);choose(&px,x);choose(&py,y);
  if(joy&(J_LEFT|J_RIGHT))axes|=1u;
  if(joy&(J_UP|J_DOWN))axes|=2u;
  if(!axes)axes=3u;
  mx=pl_x<128u ? pl_x : (uint8_t)(255u-pl_x);
  my=pl_y<112u ? pl_y : (uint8_t)(223u-pl_y);
  /* Near a corner, finish the closer continuation before the next turn.
   * The held axis resumes as soon as that strip is cached. Splitting every
   * frame between both strips leaves the imminent edge half prepared. */
  if(axes==1u && py.dir && my<48u && my<mx)axes=2u;
  else if(axes==2u && px.dir && mx<48u && mx<my)axes=1u;
  /* Retire one already-offscreen quad early near a requested boundary.
   * This work must progress even while late actors/cards use their margin. */
  if(spare(24u)) {
    n=priority_axis();
    if((n ? my : mx)<48u)clear_one(n ? &py : &px,n);
  }
  /* Resolve a ready transfer before spending its spare time on a different
   * strip. A tight vertical edge must not starve after a horizontal turn. */
  if(spare(15u))bind_requested();
  /* Finish both started strips across a direction change. New orthogonal
   * preparations still require movement on that axis (wanted() above). */
  if(axes==3u)axes=my<32u && my<mx ? 2u : mx<32u && mx<=my ? 1u : 3u;
  /* Seed queries touch only the inactive backing store. Reserve their time
   * before simulation; a late collision/render pass must not starve a seam. */
  /* A seed query near apparatus can consume roughly forty scanlines.
   * Admit it early enough to retain the mover, audio and complete actor list. */
  for(n=0u;n<6u && spare(55u);n++) {
    /* Once the requested strip is generated, warm the other axis while its
     * outgoing BG/save work finishes. A turn must not start from cold data. */
    if(axes==1u && px.dir && px.n==14u) {generate_one(&py,1u,world_pre_y);continue;}
    if(axes==2u && py.dir && py.n==16u) {generate_one(&px,0u,world_pre_x);continue;}
    turn^=1u;
    did=axes==1u || (axes==3u && turn) ? generate_one(&px,0u,world_pre_x) : generate_one(&py,1u,world_pre_y);
    if(!did) {
      if(axes==1u || (axes==3u && turn))generate_one(&py,1u,world_pre_y);
      else generate_one(&px,0u,world_pre_x);
    }
  }
}
void world_frame_end(void) BANKED {
  uint8_t n,did,axis;
  if(!world_on || !pl_hp || pl_state==PS_FALL || pl_state==PS_EXIT || enemies_alive)return;
  if(!spare(116u))return;
  axis=priority_axis();
  for(n=0u;n<4u && spare(116u);n++) {
    did=axis ? clear_one(&py,1u) : clear_one(&px,0u);
    if(!did) {
      /* Do not spend the save allowance clearing a speculative strip while
       * the requested transfer still needs its outgoing packets. */
      if(!world_preserved(axis ? 0 : px.dir,axis ? py.dir : 0))break;
      if(axis)clear_one(&px,0u);else clear_one(&py,1u);
    }
  }
  /* Save a single outgoing packet during the approach. Every later material,
   * skin or crack change invalidates its certificate through world_touch. */
  for(n=0u;n<4u && spare(116u);n++) {
    uint8_t axis=priority_axis();
    if(n==3u)axis^=1u;
    preparation *p=axis ? &py : &px;
    uint8_t a,b,limit=axis ? 16u : 14u;
    if(!p->dir)continue;
    if(p->save==limit) {
      if(world_preserved(axis ? 0 : p->dir,axis ? p->dir : 0))continue;
      p->save=0u;
    }
    while(p->save<limit) {
      if(axis) {a=(p->save&3u)*4u;b=(uint8_t)((p->dir>0 ? 0u : 10u)+(p->save>>2));}
      else {a=p->dir>0 ? 0u : 12u;b=p->save;}
      p->save++;
      if(!world_chunk_preserved(a,b)) {world_preserve(a,b);break;}
    }
  }
  /* A newly approached apparatus may compose a full HUD row. Keep that
   * pending until it fits after the outgoing-strip work above. */
  if(spare(100u))goal_hint();
}
