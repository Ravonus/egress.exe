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

void bench_art_piece(uint8_t p,uint8_t pose,uint8_t tile) BANKED;
void bench_art_palette(uint8_t p,uint8_t slot) BANKED;
void bench_art_fuse(uint8_t result,uint8_t step,uint8_t tile) BANKED;
void items_bench_tick(uint8_t a,uint8_t b,uint8_t r) BANKED;
extern uint8_t bench_phase;
uint8_t bench_page; /* 0 merge scene, 1 material inventory; SELECT swaps at a physical bench. */
static uint8_t sock[2],result,art_pose,inventory_page,layout,art_buffer;
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
static const char *const hints[P_COUNT]={
  "FIND MATERIALS","BURNS PAPER / ICE","DOUSES FIRE","PUSHES LIGHT BOXES",
  "MIX WITH WATER","MIX WITH ENERGY","MIX WITH METAL","WETS YOUR TARGET",
  "SLOWS A MOB","FREEZES WATER/MOBS","FREEZES WATER/MOBS","WEIGHS DOWN PLATES",
  "POWERS WET WIRES","HOT MIST BLINDS","BURNS PAPER / ICE","DUST BLINDS A MOB","WETS YOUR TARGET"
};

/* All text is at most 18 cells inside the screen's one-cell margins. */
static void row(uint8_t y,const char *s,uint8_t pal) {
  char clipped[19];uint8_t n=0u;
  while(s[n] && n<18u){clipped[n]=s[n];n++;}clipped[n]=0;
  ui_fill(1u,1u,y,18u,1u,pal);text_at(1u,1u,y,clipped,pal);
}
static uint8_t first(uint8_t k) {
  uint8_t j;for(j=0u;j<k;j++)if(pocket[j]==pocket[k])return j;return k;
}
static uint8_t units(uint8_t p) {
  uint8_t k,n=0u;for(k=0u;k<pocket_n;k++)if(pocket[k]==p)n++;return n;
}
static uint8_t available(uint8_t p) {
  uint8_t k;for(k=0u;k<pocket_n;k++)if(pocket[k]==p && k!=sock[0] && k!=sock[1])return k;
  return 255u;
}
static void step(uint8_t back) {
  uint8_t k=bench_cur;if(!pocket_n)return;
  do {k=back ? (k ? k-1u : pocket_n-1u) : (k+1u<pocket_n ? k+1u : 0u);}while(first(k)!=k);
  bench_cur=k;
}
static void card(uint8_t x,uint8_t y,uint8_t p) {
  uint8_t a[4],m=(uint8_t)(M_ITEM0+p-1u);
  memset(a,(uint8_t)(meta_pal[m]&7u),4u);
  VBK_REG=1u;set_win_tiles(x,y,2u,2u,a);VBK_REG=0u;set_win_tiles(x,y,2u,2u,meta_tile[m]);
}
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
  uint8_t a,b;
  if(bench_phase || bench_page)return;
  art_buffer^=1u;
  if(!bench_mode) {
    if(pocket_n){bench_art_piece(pocket[bench_cur],art_pose,art_buffer*32u);bench_art_palette(pocket[bench_cur],6u);body(0u,64,36,6u);}
    return;
  }
  a=sock[0]==255u ? 0u : pocket[sock[0]];b=sock[1]==255u ? 0u : pocket[sock[1]];
  if(a){bench_art_piece(a,art_pose,art_buffer*32u);bench_art_palette(a,6u);body(0u,20,36,6u);}
  if(b){bench_art_piece(b,art_pose,art_buffer*32u+16u);bench_art_palette(b,7u);body(1u,108,36,7u);}
}
static void inventory(void) {
  uint8_t k,ordinal=0u,total=0u,at=0u,page,x,y,p,count,shown,redraw;
  for(k=0u;k<pocket_n;k++)if(first(k)==k){total++;if(k<bench_cur)ordinal++;}
  page=(uint8_t)((ordinal/9u)*9u);ordinal=0u;
  shown=total>page+9u ? 9u : (uint8_t)(total-page);
  redraw=(uint8_t)(page!=inventory_page);inventory_page=page;
  if(redraw)ui_fill(1u,0u,3u,20u,13u,INK);
  if(!pocket_n){row(3u,"YOUR BAG IS EMPTY",PAPER);row(10u,"FIND MATERIALS",INK);return;}
  p=pocket[bench_cur];eg_piece_name(p,label);row(3u,label,PAPER);
  text_at(1u,15u,3u,"x",PAPER);num_at(1u,16u,3u,units(p),2u,PAPER);
  for(k=0u;k<pocket_n;k++) {
    if(first(k)!=k)continue;
    if(ordinal++<page)continue;
    if(at==9u)break;
    x=(uint8_t)(2u+(shown<3u ? (3u-shown)*3u : 0u)+(at%3u)*6u);y=(uint8_t)(4u+(at/3u)*4u);
    at++;p=pocket[k];count=units(p);
    ui_frame(1u,x,y,4u,4u,k==bench_cur ? PAPER : INK);
    if(redraw)card(x+1u,y+1u,p);
    num_at(1u,x+1u,y,count,2u,k==bench_cur ? PAPER : INK);
  }
}
static void draw(void) {
  uint8_t p;
  hide();
  if(layout!=bench_page){
    if(layout==255u)ui_fill(1u,0u,0u,20u,3u,INK);
    ui_fill(1u,0u,3u,20u,15u,INK);inventory_page=255u;layout=bench_page;
  }
  ui_frame(1u,0u,0u,20u,3u,INK);
  text_at(1u,2u,1u,bench_mode ? "BENCH" : "MATERIALS",INK);
  if(bench_mode) {
    text_at(1u,11u,1u,"MIX",bench_page ? INK : PAPER);
    text_at(1u,15u,1u,"BAG",bench_page ? PAPER : INK);
    text_at(1u,15u,2u,"SEL",INK);
  } else {num_at(1u,13u,1u,pocket_n,2u,INK);text_at(1u,15u,1u,"/24",INK);}
  if(!bench_page) {
    ui_frame(1u,1u,3u,7u,7u,INK);ui_frame(1u,12u,3u,7u,7u,INK);
    text_at(1u,2u,3u,"FIRST",INK);text_at(1u,13u,3u,"SECOND",INK);text_at(1u,9u,6u,"+",INK);
    ui_fill(1u,1u,9u,8u,1u,INK);ui_fill(1u,11u,9u,8u,1u,INK);
    if(sock[0]!=255u){eg_piece_name(pocket[sock[0]],label);text_at(1u,1u,9u,label,INK);}
    if(sock[1]!=255u){eg_piece_name(pocket[sock[1]],label);text_at(1u,11u,9u,label,INK);}
    row(12u,sock[1]!=255u ? "READY TO COMBINE" : sock[0]!=255u ? "ADD ONE MORE" : "ADD TWO MATERIALS",INK);
    row(14u,"SELECT OPENS BAG",INK);row(16u,"",INK);
    row(17u,sock[1]!=255u ? "A MIX    B UNDO" : "A ADD    B BACK",PAPER);
  } else {
    inventory();p=pocket_n ? pocket[bench_cur] : 0u;
    row(16u,hints[p],INK);row(17u,bench_mode ? "A ADD   B MIX" : "SEL EQUIP  A DROP",PAPER);
  }
  art_pose=(uint8_t)((frame_count>>4)&3u);preview();
}

static void scene_row(const char *s) {
  layout=2u;hide();ui_fill(1u,0u,3u,20u,8u,INK);ui_frame(1u,1u,3u,18u,7u,INK);
  row(3u,s,INK);row(10u,"",INK);
}
/* Keep both source identities through contact, then close their contours onto
 * the real product. The pose calculation is baked; the native loop only uploads. */
static void combine(uint8_t r) {
  static const uint8_t apart[17]={44,44,43,41,39,36,32,29,26,23,21,19,18,17,16,16,16};
  static const uint8_t source_a[17]={0,0,2,3,4,0,3,3,4,3,2,1,6,2,4,4,3};
  uint8_t k,a=pocket[sock[0]],b=pocket[sock[1]];
  if(a!=source_a[r]){k=a;a=b;b=k;}
  bench_on=3u;bench_phase=1u;scene_row("BRINGING TOGETHER");
  ui_fill(1u,0u,11u,20u,5u,INK);inventory_page=255u;
  eg_piece_name(a,label);strcat(label," + ");eg_piece_name(b,label+strlen(label));row(11u,label,INK);
  row(13u,"ONE UNIT FROM EACH",INK);
  row(16u,"",PAPER);row(17u,"",INK);sfx(SFX_CHARGED);
  art_buffer^=1u;bench_art_piece(a,0u,art_buffer*32u);bench_art_piece(b,0u,art_buffer*32u+16u);bench_art_palette(a,6u);bench_art_palette(b,7u);
  for(k=0u;k<17u;k++){body(0u,64-apart[k],36,6u);body(1u,64+apart[k],36,7u);wait(2u);}
  bench_phase=2u;row(3u,"MATERIALS REACT",INK);sfx(SFX_ZAP);
  for(k=0u;k<17u;k++) {
    if(k==5u)bench_phase=3u;
    art_buffer^=1u;bench_art_fuse(r,k,art_buffer*32u);
    if(k>=9u){bench_art_palette(r,6u);bench_art_palette(r,7u);}
    body(0u,48+k,36,6u);body(1u,80-k,36,7u);particles(k*3u);wait(3u);
  }
  bench_phase=4u;result=r;hide();sfx(SFX_KEY);
  art_buffer^=1u;bench_art_piece(r,0u,art_buffer*32u);bench_art_palette(r,6u);body(0u,64,36,6u);
  row(3u,"A NEW MATERIAL",INK);eg_piece_name(r,label);row(9u,label,PAPER);
  row(10u,"",INK);eg_tags_text(r,label);row(11u,label,INK);row(13u,hints[r],INK);
  row(16u,"",INK);row(17u,"A TAKE + EQUIP",PAPER);
  particles(18u);wait(16u);bench_on=1u;
}
static void take(uint8_t k) {
  for(;k+1u<pocket_n;k++)pocket[k]=pocket[k+1u];pocket[--pocket_n]=0u;
}
static void collect(void) {
  uint8_t a=sock[0],b=sock[1];take(a>b ? a : b);take(a>b ? b : a);
  pocket[pocket_n++]=result;tool_piece=result;hud_dirty=puzzle_dirty=1u;
  narrator_event(NE_MIX,result);bench_cur=(uint8_t)(pocket_n-1u);
  result=bench_phase=0u;sock[0]=sock[1]=255u;sfx(SFX_TOKEN);draw();

}
static void close(void) {
  bench_on=4u;hide();EG_SRAM_OPEN();VBK_REG=1u;
  set_sprite_data(0u,68u,(const uint8_t *)SAVE_BENCH_SCRATCH);
  VBK_REG=0u;EG_SRAM_CLOSE();pal_commit(8u);
  hud_draw();hud_charge();hud_tick();frame_wait();
  bench_on=bench_phase=0u;
}
void bench_run(void) BANKED {
  uint8_t p,k,menu=0u,pose;
  bench_on=4u;bench_page=bench_mode ? 0u : 1u;layout=inventory_page=255u;art_buffer=0u;bench_phase=result=0u;sock[0]=sock[1]=255u;
  if(bench_cur>=pocket_n)bench_cur=0u;
  if(pocket_n)bench_cur=first(bench_cur);
  hide();frame_wait();EG_SRAM_OPEN();VBK_REG=1u;
  get_sprite_data(0u,68u,(uint8_t *)SAVE_BENCH_SCRATCH);VBK_REG=0u;EG_SRAM_CLOSE();
  VBK_REG=1u;set_sprite_data(64u,4u,motes);VBK_REG=0u;
  ui_load();set_bkg_palette(2u,2u,colours);
  draw();hud_line=144u;move_win(7u,0u);sfx(SFX_STEP);bench_on=1u;
  while(1) {
    frame_wait();DISABLE_OAM_DMA;sound_tick();
    items_bench_tick(pocket_n ? pocket[bench_cur] : 0u,sock[0]!=255u ? pocket[sock[0]] : 0u,sock[1]!=255u ? pocket[sock[1]] : 0u);
    input_poll();pose=(uint8_t)((frame_count>>4)&3u);
    if(pose!=art_pose) {
      art_pose=pose;
      if(bench_phase==4u){art_buffer^=1u;bench_art_piece(result,pose,art_buffer*32u);body(0u,64,36,6u);particles((uint8_t)frame_count);}
      else preview();
    }
    if(bench_phase==4u) {
      if(joy_pressed&J_A)collect();
      else if(joy_pressed&(J_B|J_START|J_SELECT)){sfx(SFX_DENY);row(17u,"A TAKES THE ITEM",INK);}
      continue;
    }
    if(bench_phase==5u) {
      if(joy_pressed&J_A){take(bench_cur);inventory_page=255u;if(bench_cur>=pocket_n)bench_cur=0u;hud_dirty=puzzle_dirty=1u;bench_phase=0u;sfx(SFX_STEP);draw();}
      else if(joy_pressed&J_B){bench_phase=0u;draw();}
      continue;
    }
    if((joy_pressed&J_SELECT) && bench_mode){bench_page^=1u;sfx(SFX_STEP);draw();continue;}
    if((joy_pressed&J_SELECT) && pocket_n && !bench_mode) {
      tool_piece=pocket[bench_cur];hud_dirty=1u;sfx(SFX_KEY);close();hud_msg("SELECT: ITEM READY");bench_on=6u;return;
    }
    if(joy_pressed&(J_UP|J_DOWN|J_LEFT|J_RIGHT)) {
      if(!bench_page){bench_page=1u;draw();}
      k=(joy_pressed&(J_UP|J_DOWN)) ? 3u : 1u;
      while(k--)step((uint8_t)(joy_pressed&(J_UP|J_LEFT)));
      sfx(SFX_STEP);inventory();row(16u,hints[pocket_n ? pocket[bench_cur] : 0u],INK);
    } else if(joy_pressed&J_A) {
      if(!pocket_n){sfx(SFX_DENY);continue;}
      if(!bench_mode){bench_phase=5u;row(15u,"DISCARD ONE UNIT?",INK);row(16u,"A DROP    B KEEP",PAPER);row(17u,"",INK);continue;}
      if(!stage_bench_ready()){sfx(SFX_DENY);continue;}
      if(!bench_page && sock[1]!=255u) {
        p=eg_recipe(pocket[sock[0]],pocket[sock[1]]);
        if(p)combine(p);else{sfx(SFX_DENY);narrator_event(NE_REFUSED,0u);row(10u,"NO REACTION",INK);row(17u,"B CHANGE MATERIAL",INK);}
      } else if(!bench_page){bench_page=1u;draw();}
      else {
        if(sock[1]!=255u)sock[1]=255u;
        k=available(pocket[bench_cur]);
        if(k==255u){sfx(SFX_DENY);row(10u,"NO SPARE UNIT",INK);continue;}
        sock[sock[0]==255u ? 0u : 1u]=k;bench_page=0u;sfx(SFX_PUSH);draw();
      }
    } else if(joy_pressed&J_B) {
      if(bench_mode && bench_page){bench_page=0u;sfx(SFX_STEP);draw();continue;}
      if(sock[1]!=255u)sock[1]=255u;else if(sock[0]!=255u)sock[0]=255u;else break;
      sfx(SFX_STEP);draw();
    } else if(joy_pressed&J_START){menu=1u;break;}
  }
  close();bench_on=menu ? 5u : 6u;
}
