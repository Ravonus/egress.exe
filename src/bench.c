/* A physical workbench, with room for the bodies to meet and change shape.
 * The paused scene borrows 68 bank-1 OBJ tiles and two hardware palettes.
 * SRAM scratch holds their exact preimage; no world/cache/actor art is lost. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "puzzle.h"
#include "look.h"
#include "ui.h"
#include "heir.h"
#include "stage.h"
#include "narrator.h"
#include "mapper.h"
#include "save_map.h"
#include "core/eg_piece.h"
#include "catalogue_art.h"
#include "collection.h"
#include "companion.h"
#include "world_play.h"
#include "world.h"

extern uint8_t bench_phase;
uint8_t bench_page; /* MIX, BAG, LOG, individual detail. */
static uint8_t sock[2],art_pose,inventory_page,layout,art_buffer;
static eg_item_id result,book_cur;
static uint8_t detail_book;
/* Paused-only text in the floor bank's reserved gap, before pocket DBC0. */
static char __at(0xDBA9) label[20];
typedef char bench_label_fits[(0xDBA9u+20u<=0xDBC0u) ? 1 : -1];
#define INK 2u
#define PAPER 3u
static const uint16_t colours[8]={RGB(4,3,8),RGB(11,8,16),RGB(22,15,25),RGB(30,27,25),
                                RGB(28,24,21),RGB(22,15,21),RGB(14,9,18),RGB(5,4,9)};
static const uint8_t motes[64]={
  0,0,0,0,0,0,0x18,0x18,0x3c,0x3c,0x18,0x18,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0x10,0x10,0,0,0x44,0x44,0,0,0x10,0x10,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
/* All text is at most 18 cells inside the screen's one-cell margins. */
static void row(uint8_t y,const char *s,uint8_t pal) {
  char clipped[19];uint8_t n=0u;
  while(s[n] && n<18u){clipped[n]=s[n];n++;}clipped[n]=0;
  ui_fill(1u,1u,y,18u,1u,pal);text_at(1u,1u,y,clipped,pal);
}
static uint8_t first(uint8_t k) {
  uint8_t j;for(j=0u;j<k;j++)if(pocket_id(k)<EG_PETS_FIRST && pocket_id(j)==pocket_id(k))return j;return k;
}
static uint8_t units(eg_item_id p) {return pocket_count(p);}
static uint8_t available(eg_item_id p) {
  uint8_t k;for(k=0u;k<pocket_n;k++)if(pocket_id(k)==p && k!=sock[0] && k!=sock[1])return k;
  return 255u;
}
static void step(uint8_t back) {
  uint8_t k=bench_cur;if(!pocket_n)return;
  do {k=back ? (k ? k-1u : pocket_n-1u) : (k+1u<pocket_n ? k+1u : 0u);}while(first(k)!=k);
  bench_cur=k;
}
static void card(uint8_t x,uint8_t y,eg_item_id p,uint8_t index) {
  uint8_t tiles[4],attrs[4],base=(uint8_t)(2u+index*4u);
  memset(attrs,(uint8_t)(8u|catalogue_art_bg_palette(p)),4u);
  EG_SRAM_OPEN();catalogue_art_raw(p,(frame_count>>5)&3u,(uint8_t *)SAVE_FUSION_SCRATCH);
  VBK_REG=1u;set_bkg_data(base,4u,(const uint8_t *)SAVE_FUSION_SCRATCH);EG_SRAM_CLOSE();
  set_win_tiles(x,y,2u,2u,attrs);VBK_REG=0u;
  tiles[0]=base;tiles[1]=base+2u;tiles[2]=base+1u;tiles[3]=base+3u;set_win_tiles(x,y,2u,2u,tiles);
}
static void hint(eg_item_id p) {
  eg_pet_profile pet;
  if(p>=EG_PETS_FIRST && pocket_n && pocket_id(bench_cur)==p && eg_pet_get(p,&pet)){
    row(16u,"HP",INK);num_at(1u,4u,16u,pet_meta[bench_cur*3u]&PM_HP,2u,INK);text_at(1u,6u,16u,"/",INK);num_at(1u,7u,16u,pet.hp,2u,INK);
    if(pet.traits&PT_SECOND_SPRING)text_at(1u,11u,16u,(pet_meta[bench_cur*3u]&PM_REVIVED) ? "REV USED" : "REV ONCE",INK);
  }else {eg_catalogue_hint(p,label);row(16u,label,INK);}
}
static void short_name(uint8_t x,uint8_t y,eg_item_id p) {eg_catalogue_abbr(p,label);text_at(1u,x,y,label,INK);}
static void hide(void) {
  uint8_t k;for(k=0u;k<40u;k++)move_sprite(k,0u,0u);
}
static void body(uint8_t which,int16_t x,int16_t y,uint8_t pal) {
  uint8_t k,base=(uint8_t)(which*8u);
  for(k=0u;k<8u;k++) {
    set_sprite_tile(base+k,(uint8_t)(art_buffer*32u+which*16u+k*2u));set_sprite_prop(base+k,(uint8_t)(8u|pal));
    move_sprite(base+k,(uint8_t)(x+(k&3u)*8u+8u),(uint8_t)(y+(k>>2)*16u+16u));
  }
}
static void particles(uint8_t t) {
  uint8_t k;
  for(k=0u;k<2u;k++) {
    set_sprite_tile(16u+k,(uint8_t)(64u+((t>>3)&1u)*2u));set_sprite_prop(16u+k,14u);
    move_sprite(16u+k,(uint8_t)(68u+k*8u+((t+k*3u)&7u)),(uint8_t)(88u-((t+k*9u)&31u)));
  }
}
static void wait(uint8_t n) {while(n--){frame_wait();DISABLE_OAM_DMA;sound_tick();input_poll();}}
static void preview(void) {
  eg_item_id a,b;
  if(bench_phase || bench_page)return;
  art_buffer^=1u;
  if(!bench_mode) {
    if(pocket_n){catalogue_bench_body(pocket_id(bench_cur),art_pose,art_buffer*32u);catalogue_art_palette(pocket_id(bench_cur),6u,1u);body(0u,64,36,6u);}
    return;
  }
  a=sock[0]==255u ? 0u : pocket_id(sock[0]);b=sock[1]==255u ? 0u : pocket_id(sock[1]);
  if(a){catalogue_bench_body(a,art_pose,art_buffer*32u);catalogue_art_palette(a,6u,1u);body(0u,20,36,6u);}
  if(b){catalogue_bench_body(b,art_pose,art_buffer*32u+16u);catalogue_art_palette(b,7u,1u);body(1u,108,36,7u);}
}
static void inventory(void) {
  uint8_t k,ordinal=0u,total=0u,at=0u,page,x,y,count,shown,redraw;eg_item_id p;
  for(k=0u;k<pocket_n;k++)if(first(k)==k){total++;if(k<bench_cur)ordinal++;}
  page=(uint8_t)((ordinal/9u)*9u);ordinal=0u;
  shown=total>page+9u ? 9u : (uint8_t)(total-page);
  redraw=(uint8_t)(page!=inventory_page);inventory_page=page;
  if(redraw)ui_fill(1u,0u,3u,20u,13u,INK);
  if(!pocket_n){row(3u,"YOUR BAG IS EMPTY",PAPER);row(10u,"FIND MATERIALS",INK);return;}
  p=pocket_id(bench_cur);eg_catalogue_name(p,label);row(3u,label,PAPER);
  for(k=0u;k<pocket_n;k++) {
    if(first(k)!=k)continue;
    if(ordinal++<page)continue;
    if(at==9u)break;
    x=(uint8_t)(2u+(shown<3u ? (3u-shown)*3u : 0u)+(at%3u)*6u);y=(uint8_t)(4u+(at/3u)*4u);
    at++;p=pocket_id(k);count=p>=EG_PETS_FIRST ? 1u : units(p);
    ui_frame(1u,x,y,4u,4u,k==bench_cur ? PAPER : INK);
    if(redraw)card(x+1u,y+1u,p,(uint8_t)(at-1u));
    num_at(1u,x+1u,y,count,2u,k==bench_cur ? PAPER : INK);
  }
}
static void book(void) {
  eg_item_id p,start=(eg_item_id)(((book_cur-1u)/9u)*9u+1u);uint8_t k,x,y;
  uint8_t redraw=(uint8_t)(inventory_page!=(uint8_t)((book_cur-1u)/9u));
  inventory_page=(uint8_t)((book_cur-1u)/9u);
  if(redraw)ui_fill(1u,0u,3u,20u,13u,INK);
  if(collection_known(book_cur))eg_catalogue_name(book_cur,label);else strcpy(label,"UNDISCOVERED");row(3u,label,PAPER);
  for(k=0u;k<9u;k++){
    p=start+k;if(p>EG_DISCOVERIES)break;x=(uint8_t)(2u+(k%3u)*6u);y=(uint8_t)(4u+(k/3u)*4u);
    ui_frame(1u,x,y,4u,4u,p==book_cur ? PAPER : INK);
    if(redraw){if(collection_known(p))card(x+1u,y+1u,p,k);else text_at(1u,x+1u,y+1u,"?",INK);}
  }
  row(16u,"",INK);num_at(1u,1u,16u,book_cur,3u,INK);text_at(1u,5u,16u,"/444",INK);
  row(17u,"A DETAILS  B MIX",PAPER);
}
static void detail(void) {
  static const char roles[6][8]={"ATTACK","HEALER","SUPPORT","TANK","DOT","TACTIC"};
  eg_item_id id=detail_book ? book_cur : pocket_id(bench_cur),a,b;eg_pet_profile pet;
  uint8_t known=(uint8_t)(!detail_book || collection_known(id));
  if(known){eg_catalogue_name(id,label);row(3u,label,PAPER);art_buffer^=1u;catalogue_bench_body(id,art_pose,art_buffer*32u);catalogue_art_palette(id,6u,1u);body(0u,64,36,6u);}
  else row(3u,"UNDISCOVERED",PAPER);
  if(id>=EG_PETS_FIRST && known && eg_pet_get(id,&pet)){
    row(9u,roles[pet.role],INK);text_at(1u,11u,9u,"HP",INK);
    num_at(1u,14u,9u,detail_book ? pet.hp : pet_meta[bench_cur*3u]&PM_HP,2u,INK);text_at(1u,16u,9u,"/",INK);num_at(1u,17u,9u,pet.hp,2u,INK);
    eg_pet_ability_name(id,0u,label);row(11u,label,INK);eg_pet_ability_name(id,1u,label);row(13u,label,INK);
    row(15u,(pet.traits&PT_SECOND_SPRING) ? (!detail_book && (pet_meta[bench_cur*3u]&PM_REVIVED)) ? "REVIVAL SPENT" : "REVIVES ONCE" : "DEATH IS PERMANENT",INK);
  }else if(known && eg_catalogue_ingredients(id,&a,&b)){
    row(9u,"RECIPE",INK);eg_catalogue_name(a,label);row(11u,label,INK);row(12u,"+",INK);eg_catalogue_name(b,label);row(13u,label,INK);hint(id);
  }else {row(10u,"FIND AND COMBINE",INK);row(12u,"BASE MATERIALS",INK);}
  row(17u,detail_book ? "B BACK TO LOG" : "A EQUIP  B BACK",PAPER);
}
static void detail_idle(void){
  eg_item_id id=detail_book ? book_cur : pocket_id(bench_cur);
  if(detail_book && !collection_known(id))return;
  art_buffer^=1u;catalogue_bench_body(id,art_pose,art_buffer*32u);body(0u,64,36,6u);
}
static void animate_card(void){
  uint8_t at,k,n=0u,x,y,total=0u;eg_item_id id=0u;
  if((frame_count&7u) || (bench_page!=1u && bench_page!=2u))return;
  at=(uint8_t)((frame_count>>3)%9u);
  if(bench_page==2u){id=(eg_item_id)inventory_page*9u+at+1u;if(id>EG_DISCOVERIES || !collection_known(id))return;}
  else for(k=0u;k<pocket_n;k++)if(first(k)==k){total++;if(n++==(uint8_t)(inventory_page+at))id=pocket_id(k);}
  if(!id)return;x=(uint8_t)(3u+(at%3u)*6u+(bench_page==1u && total<3u ? (3u-total)*3u : 0u));y=(uint8_t)(5u+(at/3u)*4u);card(x,y,id,at);
}
static void draw(void) {
  eg_item_id p;
  hide();
  if(layout!=bench_page){
    if(layout==255u)ui_fill(1u,0u,0u,20u,3u,INK);
    ui_fill(1u,0u,3u,20u,15u,INK);inventory_page=255u;layout=bench_page;
  }
  ui_frame(1u,0u,0u,20u,3u,INK);
  text_at(1u,bench_mode ? 1u : 2u,1u,bench_mode ? "WORK" : "MATERIALS",INK);
  if(bench_mode) {
    text_at(1u,6u,1u,"MIX",bench_page==0u ? PAPER : INK);
    text_at(1u,11u,1u,"BAG",bench_page==1u ? PAPER : INK);
    text_at(1u,16u,1u,"LOG",bench_page==2u ? PAPER : INK);
    text_at(1u,14u,2u,"SEL >",INK);
  } else {num_at(1u,13u,1u,pocket_n,2u,INK);text_at(1u,15u,1u,"/24",INK);}
  if(bench_mode){num_at(1u,1u,2u,collection_count(),3u,INK);text_at(1u,4u,2u,"/444",INK);}
  if(bench_page==3u){detail();return;}
  if(bench_page==2u){book();return;}
  if(!bench_page) {
    ui_frame(1u,1u,3u,7u,7u,INK);ui_frame(1u,12u,3u,7u,7u,INK);
    text_at(1u,2u,3u,"FIRST",INK);text_at(1u,13u,3u,"SECOND",INK);text_at(1u,9u,6u,"+",INK);
    ui_fill(1u,1u,9u,8u,1u,INK);ui_fill(1u,11u,9u,8u,1u,INK);
    if(sock[0]!=255u)short_name(2u,9u,pocket_id(sock[0]));
    if(sock[1]!=255u)short_name(13u,9u,pocket_id(sock[1]));
    row(12u,sock[1]!=255u ? "READY TO COMBINE" : sock[0]!=255u ? "ADD ONE MORE" : "ADD TWO MATERIALS",INK);
    row(14u,"SELECT OPENS BAG",INK);row(16u,"",INK);
    row(17u,sock[1]!=255u ? "A MIX    B UNDO" : "A ADD    B BACK",PAPER);
  } else {
    inventory();p=pocket_n ? pocket_id(bench_cur) : 0u;
    hint(p);row(17u,p>=EG_PETS_FIRST ? "A CRITTER DETAILS" : bench_mode ? "A ADD   B MIX" : "SEL EQUIP  A DROP",PAPER);
  }
  art_pose=(uint8_t)((frame_count>>4)&3u);preview();
}

static void scene_row(const char *s) {
  layout=2u;hide();ui_fill(1u,0u,3u,20u,8u,INK);ui_frame(1u,1u,3u,18u,7u,INK);
  row(3u,s,INK);row(10u,"",INK);
}
/* Keep both source identities through contact, then close their contours onto
 * the real product. The pose calculation is baked; the native loop only uploads. */
static void combine(eg_item_id r) {
  static const uint8_t apart[17]={44,44,43,41,39,36,32,29,26,23,21,19,18,17,16,16,16};
  uint8_t k;eg_item_id a=pocket_id(sock[0]),b=pocket_id(sock[1]);
  bench_on=3u;bench_phase=1u;scene_row("BRINGING TOGETHER");
  ui_fill(1u,0u,11u,20u,5u,INK);inventory_page=255u;
  eg_catalogue_name(a,label);row(11u,label,INK);eg_catalogue_name(b,label);row(12u,label,INK);
  row(13u,"ONE UNIT FROM EACH",INK);
  row(16u,"",PAPER);row(17u,"",INK);sfx(SFX_CHARGED);
  art_buffer^=1u;catalogue_bench_body(a,0u,art_buffer*32u);catalogue_bench_body(b,0u,art_buffer*32u+16u);catalogue_art_palette(a,6u,1u);catalogue_art_palette(b,7u,1u);
  for(k=0u;k<17u;k++){body(0u,64-apart[k],36,6u);body(1u,64+apart[k],36,7u);wait(2u);}
  catalogue_fusion_begin(a,b,r);
  bench_phase=2u;row(3u,"MATERIALS REACT",INK);sfx(SFX_ZAP);
  for(k=0u;k<17u;k++) {
    if(k==5u)bench_phase=3u;
    art_buffer^=1u;catalogue_fusion_step(k,art_buffer*32u);
    if(k>=9u){catalogue_art_palette(r,6u,1u);catalogue_art_palette(r,7u,1u);}
    body(0u,48+k,36,6u);body(1u,80-k,36,7u);particles(k*3u);wait(3u);
  }
  bench_phase=4u;result=r;hide();sfx(SFX_KEY);
  art_buffer^=1u;catalogue_bench_body(r,0u,art_buffer*32u);catalogue_art_palette(r,6u,1u);body(0u,64,36,6u);
  row(3u,"A NEW MATERIAL",INK);eg_catalogue_name(r,label);row(9u,label,PAPER);
  row(10u,"",INK);eg_catalogue_hint(r,label);row(13u,label,INK);
  row(11u,collection_known(r) ? "KNOWN DISCOVERY" : "NEW DISCOVERY",INK);
  row(16u,"",INK);row(17u,"A COLLECT",PAPER);
  particles(18u);wait(16u);bench_on=1u;
}
static void collect(void) {
  uint8_t a=sock[0],b=sock[1];pocket_take(a>b ? a : b);pocket_take(a>b ? b : a);
  pocket_add(result);tool_piece=result;hud_dirty=puzzle_dirty=1u;
  narrator_event(NE_MIX,eg_catalogue_family(result));bench_cur=(uint8_t)(pocket_n-1u);
  result=bench_phase=0u;sock[0]=sock[1]=255u;sfx(SFX_TOKEN);draw();
}
static void close(void) {
  bench_on=4u;hide();EG_SRAM_OPEN();VBK_REG=1u;
  set_sprite_data(0u,68u,(const uint8_t *)SAVE_BENCH_SCRATCH);
  set_bkg_data(2u,36u,(const uint8_t *)SAVE_BENCH_BG_SCRATCH);
  VBK_REG=0u;EG_SRAM_CLOSE();pal_commit(8u);
  hud_draw();hud_charge();hud_tick();frame_wait();
  DISABLE_OAM_DMA;draw_sprites();
  bench_on=bench_phase=0u;
  puzzle_dirty=1u;effect_n=128u; /* paused art borrows the reset-hint cache */
}
void bench_run(void) BANKED {
  uint8_t k,menu=0u,pose,nav,nav_t=0u;eg_item_id p;
  bench_on=4u;bench_page=bench_mode ? 0u : 1u;layout=inventory_page=255u;art_buffer=0u;bench_phase=result=0u;book_cur=1u;detail_book=0u;sock[0]=sock[1]=255u;
  if(bench_cur>=pocket_n)bench_cur=0u;
  if(pocket_n)bench_cur=first(bench_cur);
  hide();frame_wait();EG_SRAM_OPEN();VBK_REG=1u;
  get_sprite_data(0u,68u,(uint8_t *)SAVE_BENCH_SCRATCH);
  get_bkg_data(2u,36u,(uint8_t *)SAVE_BENCH_BG_SCRATCH);VBK_REG=0u;EG_SRAM_CLOSE();
  VBK_REG=1u;set_sprite_data(64u,4u,motes);VBK_REG=0u;
  ui_load();set_bkg_palette(2u,2u,colours);catalogue_art_ui_palettes();
  draw();hud_line=144u;move_win(7u,0u);sfx(SFX_STEP);bench_on=1u;
  while(1) {
    frame_wait();DISABLE_OAM_DMA;sound_tick();
    if(world_on && !bench_phase){
      uint8_t held=joy,previous=joy_prev;
      /* Planning a craft also warms the inactive map strips. The world stays
       * paused; menu navigation must not choose a travel direction. */
      joy=joy_prev=0u;world_prefetch();world_frame_end();joy=held;joy_prev=previous;
    }
    if(!bench_phase)animate_card();
    input_poll();nav=joy_pressed;
    if(joy&15u){
      if(nav&15u)nav_t=0u;else if(nav_t<24u)nav_t++;else nav_t=21u;
      if(nav_t>=20u && !(nav_t&3u))nav|=joy&15u;
    }else nav_t=0u;
    pose=(uint8_t)((frame_count>>4)&3u);
    if(pose!=art_pose) {
      art_pose=pose;
      if(bench_phase==4u){art_buffer^=1u;catalogue_bench_body(result,pose,art_buffer*32u);body(0u,64,36,6u);particles((uint8_t)frame_count);}
      else if(bench_page==3u)detail_idle();else preview();
    }
    if(bench_phase==4u) {
      if(nav&J_A)collect();
      else if(nav&(J_B|J_START|J_SELECT)){sfx(SFX_DENY);row(17u,"A TAKES THE ITEM",INK);}
      continue;
    }
    if(bench_page==3u){
      if(nav&J_B){bench_page=detail_book ? 2u : 1u;draw();}
      else if((nav&J_A) && !detail_book && pocket_n){
        if(pocket_id(bench_cur)>=EG_PETS_FIRST){if(!companion_equip(bench_cur)){sfx(SFX_DENY);continue;}}
        else tool_piece=pocket_id(bench_cur);
        hud_dirty=1u;close();bench_on=6u;return;
      }else if(nav&J_START){menu=1u;break;}
      continue;
    }
    if(bench_phase==5u) {
      if(nav&J_A){pocket_take(bench_cur);inventory_page=255u;if(bench_cur>=pocket_n)bench_cur=0u;hud_dirty=puzzle_dirty=1u;bench_phase=0u;sfx(SFX_STEP);draw();}
      else if(nav&J_B){bench_phase=0u;draw();}
      continue;
    }
    if((nav&J_SELECT) && bench_mode){bench_page=(uint8_t)((bench_page+1u)%3u);sfx(SFX_STEP);draw();continue;}
    if(bench_page==2u){
      if(nav&(J_UP|J_DOWN|J_LEFT|J_RIGHT)){
        k=(nav&(J_UP|J_DOWN)) ? 3u : 1u;
        while(k--){if(nav&(J_UP|J_LEFT))book_cur=book_cur>1u ? book_cur-1u : EG_DISCOVERIES;else book_cur=book_cur<EG_DISCOVERIES ? book_cur+1u : 1u;}
        book();sfx(SFX_STEP);
      }else if(nav&J_A){detail_book=1u;bench_page=3u;draw();}
      else if(nav&J_B){bench_page=0u;draw();}
      else if(nav&J_START){menu=1u;break;}
      continue;
    }
    if((nav&J_SELECT) && pocket_n && !bench_mode) {
      if(pocket_id(bench_cur)>=EG_PETS_FIRST){if(!companion_equip(bench_cur))continue;}
      else tool_piece=pocket_id(bench_cur);hud_dirty=1u;sfx(SFX_KEY);close();hud_msg("SELECT: ITEM READY");bench_on=6u;return;
    }
    if(nav&(J_UP|J_DOWN|J_LEFT|J_RIGHT)) {
      if(!bench_page){bench_page=1u;draw();}
      k=(nav&(J_UP|J_DOWN)) ? 3u : 1u;
      while(k--)step((uint8_t)(nav&(J_UP|J_LEFT)));
      sfx(SFX_STEP);inventory();hint(pocket_n ? pocket_id(bench_cur) : 0u);
    } else if(nav&J_A) {
      if(!pocket_n){sfx(SFX_DENY);continue;}
      if(bench_page==1u && pocket_id(bench_cur)>=EG_PETS_FIRST){detail_book=0u;bench_page=3u;draw();continue;}
      if(!bench_mode){bench_phase=5u;row(15u,"DISCARD ONE UNIT?",INK);row(16u,"A DROP    B KEEP",PAPER);row(17u,"",INK);continue;}
      if(!stage_bench_ready()){sfx(SFX_DENY);continue;}
      if(!bench_page && sock[1]!=255u) {
        p=eg_catalogue_recipe(pocket_id(sock[0]),pocket_id(sock[1]));
        if(p)combine(p);else{sfx(SFX_DENY);narrator_event(NE_REFUSED,0u);row(10u,"NO REACTION",INK);row(17u,"B CHANGE MATERIAL",INK);}
      } else if(!bench_page){bench_page=1u;draw();}
      else {
        if(sock[1]!=255u)sock[1]=255u;
        k=available(pocket_id(bench_cur));
        if(k==255u){sfx(SFX_DENY);row(10u,"NO SPARE UNIT",INK);continue;}
        sock[sock[0]==255u ? 0u : 1u]=k;bench_page=0u;sfx(SFX_PUSH);draw();
      }
    } else if(nav&J_B) {
      if(bench_mode && bench_page){bench_page=0u;sfx(SFX_STEP);draw();continue;}
      if(sock[1]!=255u)sock[1]=255u;else if(sock[0]!=255u)sock[0]=255u;else break;
      sfx(SFX_STEP);draw();
    } else if(nav&J_START){menu=1u;break;}
  }
  close();bench_on=menu ? 5u : 6u;
}
