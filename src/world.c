#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "look.h"
#include "world.h"
#include "gen/world_art.h"
#include "core/eg_stage.h"
#include "ui.h"
eg_land world_plan;
eg_view world_view;
uint8_t world_on,world_latched,world_glitch;
static uint8_t synchronized[7];
static uint8_t binding_valid,binding_x,binding_y;
static uint8_t read_link(void *ctx,uint8_t x,uint8_t y) NONBANKED;
void eg_land_cached_at(const eg_land *p,uint8_t x,uint8_t y,eg_land_cell *out) BANKED;
uint8_t eg_land_void4(const eg_land *p,uint8_t x,uint8_t y) BANKED;
uint8_t world_binding_ready(int8_t dx,int8_t dy) NONBANKED {
  return binding_valid && binding_x==(uint8_t)(world_view.x+dx) && binding_y==(uint8_t)(world_view.y+dy);
}
void world_prepare_binding(int8_t dx,int8_t dy) BANKED {
  eg_view target=world_view;uint8_t x,y,k;
  eg_land_frame *binding=(eg_land_frame *)eg_stage_work.world_dead;
  if(!world_on)return;
  if(world_binding_ready(dx,dy))return;
  target.x=(uint8_t)(target.x+dx);target.y=(uint8_t)(target.y+dy);
  if(!eg_view_valid(&target) || (dx && dy) || (!dx && !dy) ||
     (dx && dx!=4 && dx!=-4) || (dy && dy!=4 && dy!=-4))return;
  if(dx) {
    x=dx>0 ? world_view.x+16u : world_view.x-4u;
    for(y=0u;y<14u;y++)if(!world_cached(x,world_view.y+y))return;
  } else {
    y=dy>0 ? world_view.y+14u : world_view.y-4u;
    for(x=0u;x<16u;x+=4u)for(k=0u;k<4u;k++)if(!world_cached(world_view.x+x,y+k))return;
  }
  /* This occasional preflight borrows constructor/runtime scratch. Preserve
   * the live scanner while constructing the 27-byte frame, then keep that
   * frame in bank 6 until the seam. No large extra hot-WRAM allocation. */
  for(k=0u;k<4u;k++) {
    CRITICAL {view_put9((uint16_t)(0x4100u+k*9u),eg_stage_work.world_dead+k*9u);}
  }
  eg_land_bind_frame(&world_plan,&target,binding,world_latched,read_link,0);
  for(k=0u;k<3u;k++) {
    CRITICAL {view_put9((uint16_t)(0x4140u+k*9u),eg_stage_work.world_dead+k*9u);}
  }
  for(k=0u;k<4u;k++) {
    CRITICAL {view_get9((uint16_t)(0x4100u+k*9u),eg_stage_work.world_dead+k*9u);}
  }
  binding_x=target.x;binding_y=target.y;binding_valid=1u;
}
void world_touch(uint8_t cell) NONBANKED {
  uint8_t chunk;
  if(!world_on || cell>=EG_N)return;
  binding_valid=0u;
  if((room.c[cell]&T_MASK)==T_GLITCH)world_glitch=1u;
  chunk=cell>>2;synchronized[chunk>>3]&=(uint8_t)~look_bits[chunk&7u];
}
uint8_t world_chunk_preserved(uint8_t x,uint8_t y) NONBANKED {
  uint8_t chunk=(uint8_t)(y*4u+(x>>2));
  return synchronized[chunk>>3]&look_bits[chunk&7u];
}
void world_preserve(uint8_t x,uint8_t y) NONBANKED {
  uint8_t i=EG_IDX(x,y),chunk=i>>2,k,p=room.prot[i>>3],c=cracked[i>>3];
  if(x&4u) {p>>=4;c>>=4;}
  eg_window_packet[8]=(uint8_t)((p&15u)|(c<<4));
  for(k=0u;k<4u;k++) {eg_window_packet[k]=room.c[i+k];eg_window_packet[k+4u]=look_skin[i+k];}
  x+=world_view.x;y+=world_view.y;
  view_store(0,x,y,eg_window_packet);world_mark_cached(x,y);
  synchronized[chunk>>3]|=look_bits[chunk&7u];
}
uint8_t world_preserved(int8_t dx,int8_t dy) NONBANKED {
  uint8_t k,mask;
  if(dx) {
    mask=dx>0 ? 17u : 136u;
    for(k=0u;k<7u;k++)if((synchronized[k]&mask)!=mask)return 0u;
  } else {
    k=dy>0 ? 0u : 5u;
    if(synchronized[k]!=255u || synchronized[k+1u]!=255u)return 0u;
  }
  return 1u;
}
/* Two bitmap rows fit an eight-byte payload. The ninth byte is padding,
 * keeping every metadata transfer within the proven bounded nine-byte leaf. */
static uint16_t mask_address(uint8_t y) NONBANKED {return (uint16_t)(0x4000u+(uint8_t)((y>>1)*9u));}
uint8_t world_cached(uint8_t x,uint8_t y) NONBANKED {
  uint8_t byte=(uint8_t)(((y&1u)<<2)+(x>>5));
  CRITICAL {view_get9(mask_address(y),world_mask_packet);}
  return (uint8_t)(world_mask_packet[byte]&look_bits[(x>>2)&7u]);
}
void world_mark_cached(uint8_t x,uint8_t y) NONBANKED {
  uint16_t address=mask_address(y);uint8_t byte=(uint8_t)(((y&1u)<<2)+(x>>5)),k,t;
  CRITICAL {view_get9(address,world_mask_packet);}
  world_mask_packet[byte]|=look_bits[(x>>2)&7u];
  for(k=0u;k<4u;k++) {
    t=eg_window_packet[k];
    if((t&T_MASK)==T_GLITCH)world_glitch=1u;
    if(EG_CRATE(t) || (t&T_MASK)==T_ROCK || (t&F_OB))world_mask_packet[8]|=look_bits[byte];
  }
  CRITICAL {view_put9(address,world_mask_packet);}
}
static void note_weight(uint8_t x,uint8_t y) NONBANKED {
  uint16_t address=mask_address(y);uint8_t bit=look_bits[((y&1u)<<2)+(x>>5)];
  CRITICAL {view_get9(address,world_mask_packet);}
  if(world_mask_packet[8]&bit)return;
  world_mask_packet[8]|=bit;
  CRITICAL {view_put9(address,world_mask_packet);}
}
uint8_t world_skin(uint8_t design,uint8_t variant) NONBANKED {
  switch(design) {
    case EL_FERN:return (uint8_t)(1u+((variant&1u) ? M_WORLD_FERN1 : M_WORLD_FERN0));
    case EL_FLOWER:return (uint8_t)(1u+((variant&1u) ? M_WORLD_FLOWER1 : M_WORLD_FLOWER0));
    case EL_REED:return (uint8_t)(1u+((variant&1u) ? M_WORLD_REED1 : M_WORLD_REED0));
    case EL_PROP:return (uint8_t)(1u+((variant&1u) ? M_WORLD_PROP1 : M_WORLD_PROP0));
    case EL_RIVER:return (uint8_t)(1u+((variant&1u) ? M_WORLD_RIVER1 : M_WORLD_RIVER0));
    case EL_FALL:return (uint8_t)(1u+((variant&1u) ? M_WORLD_FALL1 : M_WORLD_FALL0));
  }
  return 0u;
}
static uint8_t skin_ok(uint8_t skin,uint8_t type) NONBANKED {
  if(!skin)return 1u;skin--;
  if(skin==M_WORLD_FERN0 || skin==M_WORLD_FERN1 || skin==M_WORLD_FLOWER0 || skin==M_WORLD_FLOWER1 ||
     skin==M_WORLD_REED0 || skin==M_WORLD_REED1)return type==T_DIRT;
  if(skin==M_WORLD_PROP0 || skin==M_WORLD_PROP1)return type==T_EMPTY;
  if(skin==M_WORLD_RIVER0 || skin==M_WORLD_RIVER1 || skin==M_WORLD_FALL0 || skin==M_WORLD_FALL1)return type==T_WATER;
  return 1u;
}
void world_clean_skin(uint8_t cell) NONBANKED {
  if(cell<EG_N && (!skin_ok(look_skin[cell],cell_type(cell)) || (look_skin[cell]==M_PIT+1u && cell_type(cell)!=T_PIT)))look_skin[cell]=0u;
}
static void generate(uint8_t x,uint8_t y) NONBANKED {
  eg_land_cell cell;uint8_t k;
  eg_window_packet[8]=0u;
  for(k=0u;k<4u;k++) {
    eg_land_cached_at(&world_plan,x+k,y,&cell);eg_window_packet[k]=cell.material;
    eg_window_packet[k+4u]=world_skin(cell.design,cell.variant);
    if(cell.flags&EL_PROTECTED)eg_window_packet[8]|=look_bits[k];
  }
  view_store(0,x,y,eg_window_packet);world_mark_cached(x,y);
}
void world_cache_load(void *ctx,uint8_t x,uint8_t y,uint8_t *packet) NONBANKED {
  (void)ctx;
  if(!world_cached(x,y))generate(x,y);else view_load(0,x,y,eg_window_packet);
  if(packet!=eg_window_packet)memcpy(packet,eg_window_packet,9u);
}
static uint8_t local(uint8_t x,uint8_t y) NONBANKED {
  if(x<world_view.x || y<world_view.y)return EG_LINK_NONE;
  x=(uint8_t)(x-world_view.x);y=(uint8_t)(y-world_view.y);
  return x<EG_W && y<EG_H ? EG_IDX(x,y) : EG_LINK_NONE;
}
static uint8_t read_link(void *ctx,uint8_t x,uint8_t y) NONBANKED {
  uint8_t c;(void)ctx;
  c=local(x,y);if(c<EG_N)return room.c[c];
  /* Canonical codec plates/sockets start unweighted and gates start closed.
   * Every mutation initializes its backing packet. Binding must not generate
   * distant terrain just to rediscover the unmodified source's false state. */
  if(!world_cached(x,y))return T_EMPTY;
  view_load(0,(uint8_t)(x&252u),y,eg_window_packet);return eg_window_packet[x&3u];
}
uint8_t world_read(void *ctx,uint8_t x,uint8_t y) NONBANKED {
  uint8_t c;(void)ctx;
  if(!world_on)return x<EG_W && y<EG_H ? room.c[EG_IDX(x,y)] : T_PIT|F_ABYSS;
  if(x>=world_view.width || y>=world_view.height)return T_PIT|F_ABYSS;
  c=local(x,y);if(c<EG_N)return room.c[c];
  world_cache_load(0,(uint8_t)(x&252u),y,eg_window_packet);return eg_window_packet[x&3u];
}
uint8_t world_write(uint8_t x,uint8_t y,uint8_t material) NONBANKED {
  uint8_t c,k;
  if(!world_on) {if(x>=EG_W || y>=EG_H)return EG_LINK_NONE;c=EG_IDX(x,y);room.c[c]=material;return c;}
  if(x>=world_view.width || y>=world_view.height)return EG_LINK_NONE;
  binding_valid=0u;
  c=local(x,y);
  if(c<EG_N) {
    room.c[c]=material;world_clean_skin(c);world_touch(c);
    if(EG_CRATE(material) || (material&T_MASK)==T_ROCK || (material&F_OB))note_weight(x,y);
    return c;
  }
  k=x&3u;world_cache_load(0,(uint8_t)(x&252u),y,eg_window_packet);eg_window_packet[k]=material;
  if(!skin_ok(eg_window_packet[k+4u],material) || (eg_window_packet[k+4u]==M_PIT+1u && (material&T_MASK)!=T_PIT))eg_window_packet[k+4u]=0u;
  view_store(0,(uint8_t)(x&252u),y,eg_window_packet);
  if(EG_CRATE(material) || (material&T_MASK)==T_ROCK || (material&F_OB))note_weight(x,y);
  return EG_LINK_NONE;
}
void world_bind(void) BANKED {eg_land_bind(&world_plan,&world_view,&room,world_latched,read_link,0);eg_plates_dirty=1u;}
static uint8_t origin(uint8_t cell,uint8_t span,uint8_t size) {
  uint8_t pos=cell>span/2u ? (uint8_t)((cell-span/2u)&252u) : 0u;
  return pos>size-span ? size-span : pos;
}
static void loading(void) {
  DISPLAY_OFF;HIDE_WIN;HIDE_SPRITES;hud_line=144u;move_bkg(0u,0u);
  art_load_game();ui_load();ui_colours();
  ui_page("EGRESS","GENERATING STAGE");ui_frame(0u,1u,7u,18u,7u,UI_DARK);
  text_at(0u,3u,8u,"PLANNING ROUTES",UI_DARK);
  text_at(0u,2u,10u,"................",UI_DARK);
  pal_commit(8u);SHOW_BKG;DISPLAY_ON;frame_wait();
}
static void precache(void) {
  uint8_t x,y,k,n;char progress[17];
  text_at(0u,3u,8u,"STORING FLOOR  ",UI_DARK);
  num_at(0u,5u,5u,world_view.width,3u,UI_DARK);text_at(0u,9u,5u,"X",UI_DARK);num_at(0u,11u,5u,world_view.height,2u,UI_DARK);
  for(y=0u;y<world_view.height;y++) {
    for(x=0u;x<world_view.width;x+=4u) {
      if(eg_land_void4(&world_plan,x,y)) {
        memset(eg_window_packet,T_PIT|F_ABYSS,4u);memset(eg_window_packet+4u,0,5u);
        view_store(0,x,y,eg_window_packet);world_mark_cached(x,y);
      }else generate(x,y);
    }
    n=(uint8_t)(((uint16_t)(y+1u)*16u)/world_view.height);
    for(k=0u;k<16u;k++)progress[k]=k<n ? '-' : '.';progress[16]=0;
    text_at(0u,2u,10u,progress,UI_DARK);
    num_at(0u,6u,12u,y+1u,2u,UI_DARK);text_at(0u,9u,12u,"/",UI_DARK);num_at(0u,11u,12u,world_view.height,2u,UI_DARK);
    frame_wait();sound_tick();
  }
}
uint8_t world_begin(uint16_t seed,uint8_t rule,uint8_t size,uint8_t biome) BANKED {
  uint8_t k,x,y,previous_mode=game_mode;
  world_on=world_latched=world_glitch=binding_valid=0u;
  game_mode=MODE_CLEAR;loading();
  if(!eg_land_plan(&world_plan,seed,rule,size,biome)){game_mode=previous_mode;return 0u;}
  world_view.width=world_plan.width;world_view.height=world_plan.height;
  k=eg_land_lane(&world_plan,2u);x=EL_X(&world_plan,2u,k);y=EL_Y(&world_plan,2u,k);
  world_view.x=origin(x,EG_W,world_view.width);world_view.y=origin(y,EG_H,world_view.height);
  memset(world_mask_packet,0,9u);
  for(k=0u;k<27u;k++) {CRITICAL {view_put9((uint16_t)(0x4000u+(uint16_t)k*9u),world_mask_packet);}}
  memset(&room,0,sizeof room);world_on=1u;
  /* All supported stages already fit the persistent WRAM packet store.
   * Fill it once while loading; camera travel reuses these same packets,
   * including subsequent mutations, without live geometry generation. */
  precache();
  memset(synchronized,0,sizeof synchronized);
  eg_view_load(&world_view,&room,look_skin,cracked,world_cache_load,0);
  room.keys=1u;world_bind();game_mode=previous_mode;return 1u;
}
void world_show(void) BANKED {
  if(!world_on)return;
  eg_view_load(&world_view,&room,look_skin,cracked,world_cache_load,0);
  world_art_load(eg_land_water_style(world_plan.biome),meta_pal,&meta_tile[0][0],pal_bg);
}
void world_sources(const uint8_t *occupied,uint8_t n) BANKED {
  uint8_t k;
  if(!world_on)return;
  for(k=0u;k<room.nlink;k++)if((room.link[k].rule&EG_LINK_LATCH) && eg_link_power(&room,k,occupied,n)) {
    if(!(world_latched&(1u<<k)))binding_valid=0u;
    world_latched|=(uint8_t)(1u<<k);
    if(room.link[k].gate==EG_LINK_EXTERNAL)room.link[k].rule|=EG_LINK_EXT_LATCH;
  }
}
uint8_t world_shift_data(int8_t dx,int8_t dy) BANKED {
  uint8_t x,y,k,c,t;
  if(!world_on || !eg_view_valid(&world_view) || (dx && dy) || (!dx && !dy))return 0u;
  if((dx && dx!=4 && dx!=-4) || (dy && dy!=4 && dy!=-4))return 0u;
  if((dx<0 && !world_view.x) || (dy<0 && !world_view.y) ||
     (dx>0 && world_view.x==world_view.width-EG_W) || (dy>0 && world_view.y==world_view.height-EG_H))return 0u;
  if(dx) {
    x=dx>0 ? world_view.x+16u : world_view.x-4u;
    for(y=0u;y<14u;y++)if(!world_cached(x,world_view.y+y))return 0u;
    x=dx>0 ? 0u : 12u;
    for(y=0u;y<14u;y++)for(k=0u;k<4u;k++) {
      c=EG_IDX(x+k,y);t=room.c[c];
      if(EG_CRATE(t) || (t&T_MASK)==T_ROCK || (t&F_OB))note_weight(world_view.x+x+k,world_view.y+y);
    }
  } else {
    y=dy>0 ? world_view.y+14u : world_view.y-4u;
    for(x=0u;x<16u;x+=4u) {
      uint8_t row;for(row=0u;row<4u;row++)if(!world_cached(world_view.x+x,y+row))return 0u;
    }
    y=dy>0 ? 0u : 10u;
    for(k=0u;k<4u;k++)for(x=0u;x<16u;x++) {
      c=EG_IDX(x,y+k);t=room.c[c];
      if(EG_CRATE(t) || (t&T_MASK)==T_ROCK || (t&F_OB))note_weight(world_view.x+x,world_view.y+y+k);
    }
  }
  if(!eg_view_shift_cached(&world_view,&room,look_skin,cracked,dx,dy))return 0u;
  binding_valid=0u;memset(synchronized,0,sizeof synchronized);world_bind();return 1u;
}
uint8_t world_shift_prepared(int8_t dx,int8_t dy) BANKED {
  uint8_t k;
  if(!world_on || !world_preserved(dx,dy) || !world_binding_ready(dx,dy))return 0u;
  if(!eg_view_shift_prepared(&world_view,&room,look_skin,cracked,dx,dy))return 0u;
  binding_valid=0u;
  /* Preserved overlap stays identical to its backing packet. Retain those
   * certificates so a perpendicular approach saves only newly entered cells. */
  if(dx) {
    for(k=0u;k<7u;k++)synchronized[k]=dx>0 ? (uint8_t)((synchronized[k]>>1)&119u) : (uint8_t)((synchronized[k]<<1)&238u);
  } else if(dy>0) {memmove(synchronized,synchronized+2u,5u);synchronized[5]=synchronized[6]=0u;}
  else {memmove(synchronized+2u,synchronized,5u);synchronized[0]=synchronized[1]=0u;}
  /* The view change restarts the scanner. Its old continuation may now be
   * replaced by the prepared frame before puzzle_shift_view marks it dirty. */
  for(k=0u;k<3u;k++) {
    CRITICAL {view_get9((uint16_t)(0x4140u+k*9u),eg_stage_work.world_dead+k*9u);}
  }
  eg_land_apply_frame(&room,(eg_land_frame *)eg_stage_work.world_dead,world_latched);
  for(k=0u;k<room.nlink;k++)if((room.link[k].rule&EG_LINK_LATCH) && (world_latched&(1u<<k)) && room.link[k].gate<EG_N)world_touch(room.link[k].gate);
  eg_plates_dirty=1u;return 1u;
}
