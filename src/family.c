/* Current-epoch CRC family journal. Earlier saves require a fresh family. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "family.h"
#include "heir.h"
#include "ui.h"
#include "look.h"
#include "mapper.h"
#include "run.h"
#include "save_map.h"
#include "core/eg_journal.h"

eg_family __at(0xDA40) family;
typedef char family_fits[(sizeof(eg_family) <= 384u) ? 1 : -1];
uint8_t family_cur, family_stat[G_STATS];
static uint8_t serial;
#define FSLOT(n) ((uint8_t *)((n) ? SAVE_FAMILY_B : SAVE_FAMILY_A))
#define FLEN ((uint16_t)(sizeof(eg_family) + 4u))
typedef char family_record_fits[(FLEN + 2u <= SAVE_FAMILY_BYTES) ? 1 : -1];
static uint8_t valid(uint8_t *p) {
  return (uint8_t)(p[0]=='F' && p[1]=='A' && p[2]==SAVE_FAMILY_VERSION &&
    eg_save_crc(p,FLEN)==((uint16_t)p[FLEN] | ((uint16_t)p[FLEN+1u]<<8)));
}
static void write_family(void) {
  uint8_t *p; uint16_t c;
  serial++; p=FSLOT(serial&1u);
  EG_SRAM_OPEN();
  p[0]=0u; p[1]='A'; p[2]=SAVE_FAMILY_VERSION; p[3]=serial;
  memcpy(p+4u,&family,sizeof family);
  p[0]='F'; c=eg_save_crc(p,FLEN); p[0]=0u;
  p[FLEN]=(uint8_t)c; p[FLEN+1u]=(uint8_t)(c>>8);
  p[0]='F';
  EG_SRAM_CLOSE();
}
static void load_family(void) {
  uint8_t *a=FSLOT(0u), *b=FSLOT(1u), oka,okb;
  EG_SRAM_OPEN(); oka=valid(a); okb=valid(b);
  if (oka && (!okb || (int8_t)(a[3]-b[3])>=0)) { serial=a[3]; memcpy(&family,a+4u,sizeof family); }
  else if (okb) { serial=b[3]; memcpy(&family,b+4u,sizeof family); }
  EG_SRAM_CLOSE();
  if (!oka && !okb) {
    serial=0u;eg_family_init(&family,run_seed);
    family.generation=meta.runs==65535u ? 65535u : (uint16_t)(meta.runs+1u);
  }
}
static void apply_gene(const eg_genome *g) {
  heir.skin=g->look[G_SKIN]; heir.look=g->look[G_LOOK]; heir.hair=g->look[G_HAIR];
  heir.neck=g->look[G_NECK]; heir.pat=g->look[G_PATTERN]; heir.mark=g->look[G_MARK];
  heir.accent=g->look[G_ACCENT]; heir.weapon=g->weapon[0]; heir.dodge=eg_gene_dodge(g);
  heir.traits=(uint8_t)((g->traits[0] | g->traits[1]) & 15u); heir.seed=g->seed;
  memcpy(family_stat,g->stat,sizeof family_stat);
  heir_compose_init(); heir_reset(); heir_colours();
}
void family_prepare(void) BANKED { if(!dbg_heir) load_family(); }
void family_apply(void) BANKED {
  if (dbg_heir) { heir_roll(dbg_heir); memset(family_stat,0,sizeof family_stat); }
  else apply_gene(&family.current);
  heir_colours();
}
static const char *const weapon_name[5] = {"BLADE","HAMMER","WHIP","GAUNTLET","LAUNCHER"};
static const char *const dodge_name[3] = {"ROLL","BLINK","SLIDE"};
static const char *const weapon_tell[5] = {"BALANCED ARC","SLOW HEAVY BREAK","LONG NARROW REACH","SHORT FAST STRIKE","DISTANT CHARGE"};
/* Native portraits use BG tiles, so three complete heirs never exceed the 10-OBJ line limit. */
/* Picker-only scratch overlaps floor-only look_skin (DCF0..DDCF). */
static uint8_t __at(0xDD00) pick_pixels[16u*24u];
uint8_t family_page,family_ready;
/* The floor-only skin prefix is idle here; nine palette masks fit before
 * the DD00 portrait scratch without consuming the game's remaining BSS. */
static uint8_t __at(0xDCF0) portrait_cloth[9];
/* Preserve body/cloth colours when a BG tile crosses the layer boundary.
 * Each enlarged tile covers only 4x4 source pixels, reducing palette mixing. */
static uint8_t pick_nearest(uint16_t colour,const uint16_t *pal) {
  uint8_t k,best=1u;uint16_t score,min=65535u;int16_t r,g,b;
  for(k=1u;k<4u;k++) {
    r=(int16_t)(colour&31u)-(int16_t)(pal[k]&31u);
    g=(int16_t)((colour>>5)&31u)-(int16_t)((pal[k]>>5)&31u);
    b=(int16_t)((colour>>10)&31u)-(int16_t)((pal[k]>>10)&31u);
    score=(uint16_t)(r*r+g*g+b*b);
    if(score<min) {min=score;best=k;}
  }
  return best;
}
static void pick_portrait(uint8_t card) {
  uint8_t n,k,x,y,v,c,raw[32],tile[16],map,px,py,cloth,remap[2][6];
  OAM_item_t *o;
  apply_gene(&family.child[card]);heir_idle_stop();heir_play(HA_IDLE,1u);heir_reset();ha_dir=0u;
  n=heir_draw(0u,8,16);frame_wait();memset(pick_pixels,0,16u*24u);
  pal_bg[card*8u]=pal_bg[card*8u+4u]=RGB(8,6,13);
  for(k=1u;k!=4u;k++) {pal_bg[card*8u+k]=pal_sp[k];pal_bg[card*8u+4u+k]=pal_sp[4u+k];}
  for(k=0u;k<2u;k++)for(v=0u;v<6u;v++) {
    remap[k][v]=v ? pick_nearest(pal_sp[v>=4u ? v+2u : v],&pal_bg[card*8u+k*4u]) : 0u;
  }
  while(n) {
    o=&shadow_OAM[--n];memcpy(raw,heir_pixels+((uint8_t)(o->tile&7u)<<4),32u);
    px=(uint8_t)(o->x-8u);py=(uint8_t)(o->y-16u);
    for(y=0u;y!=16u;y++)for(x=0u;x!=8u;x++) {
      v=(uint8_t)(((raw[y*2u]>>(7u-x))&1u)|(((raw[y*2u+1u]>>(7u-x))&1u)<<1));
      if(v && px+x<16u && py+y<24u)pick_pixels[(uint16_t)(py+y)*16u+px+x]=(uint8_t)(v+(v>1u && (o->prop&7u)==1u ? 2u : 0u));
    }
  }
  /* Cache pixels in BG VRAM and palette choices in nine WRAM bytes. Never
   * read active VRAM: real hardware and binjgb enforce its access windows. */
  memset(portrait_cloth+card*3u,0,3u);
  for(k=0u;k!=24u;k++) {
    cloth=0u;px=(uint8_t)((k&3u)*4u);py=(uint8_t)((k>>2)*4u);
    for(y=0u;y<4u;y++)for(x=0u;x<4u;x++)if(pick_pixels[(uint16_t)(py+y)*16u+px+x]>=4u)cloth++;
    cloth=(uint8_t)(cloth>=4u);memset(tile,0,sizeof tile);
    for(y=0u;y!=8u;y++)for(x=0u;x!=8u;x++) {
      v=remap[cloth][pick_pixels[(uint16_t)(py+(y>>1))*16u+px+(x>>1)]];
      if(v&1u)tile[y*2u]|=(uint8_t)(128u>>x);
      if(v&2u)tile[y*2u+1u]|=(uint8_t)(128u>>x);
    }
    map=(uint8_t)(32u+card*24u+k);VBK_REG=0u;set_bkg_data(map,1u,tile);
    if(cloth)portrait_cloth[card*3u+(k>>3)]|=look_bits[k&7u];
  }
  for(c=0u;c!=40u;c++)move_sprite(c,0u,0u);
}
static void pick_show(uint8_t card,uint8_t x,uint8_t y) {
  uint8_t k,r,attr[4],tile[4];
  for(r=0u;r<6u;r++) {
    for(k=0u;k<4u;k++) {
      uint8_t n=(uint8_t)(r*4u+k);
      attr[k]=(uint8_t)(card*2u+((portrait_cloth[card*3u+(n>>3)]&look_bits[n&7u]) ? 1u : 0u));
      tile[k]=(uint8_t)(32u+card*24u+n);
    }
    VBK_REG=1u;set_bkg_tiles(x,y+r,4u,1u,attr);
    VBK_REG=0u;set_bkg_tiles(x,y+r,4u,1u,tile);
  }
}
static void pick_box(uint8_t k) {
  uint8_t pal=k==family_cur ? UI_LIGHT : UI_DARK,x=(uint8_t)(k*7u);
  ui_frame(0u,x,4u,6u,9u,pal);ui_fill(0u,x+1u,11u,4u,1u,pal);
  text_at(0u,x+1u,11u,k==family_cur ? ">" : " ",pal);num_at(0u,x+2u,11u,k+1u,2u,pal);
}
static void pick_summary(void) {
  apply_gene(&family.child[family_cur]);
  ui_fill(0u,0u,13u,20u,4u,UI_LIGHT);ui_frame(0u,0u,13u,20u,4u,UI_LIGHT);
  text_at(0u,1u,14u,weapon_name[heir.weapon],UI_LIGHT);text_at(0u,12u,14u,dodge_name[heir.dodge],UI_LIGHT);
  text_at(0u,1u,15u,"HP",UI_LIGHT);num_at(0u,4u,15u,6u+family_stat[G_VIGOR],1u,UI_LIGHT);
  text_at(0u,7u,15u,"PWR",UI_LIGHT);num_at(0u,11u,15u,family_stat[G_POWER],1u,UI_LIGHT);
  text_at(0u,13u,15u,"PACE",UI_LIGHT);num_at(0u,18u,15u,family_stat[G_PACE],1u,UI_LIGHT);
}
static void draw_pick_cards(void) {
  uint8_t k;
  HIDE_SPRITES;ui_page("CHOOSE YOUR HEIR","L/R CHOOSE A DETAILS");
  ui_fill(0u,0u,3u,20u,1u,UI_DARK);text_at(0u,2u,3u,"GENERATION",UI_DARK);num_at(0u,14u,3u,family.generation,3u,UI_DARK);
  for(k=0u;k!=3u;k++) {pick_box(k);pick_show(k,(uint8_t)(k*7u+1u),5u);}
  pick_summary();
}
static void draw_pick_detail(void) {
  uint8_t k;
  apply_gene(&family.child[family_cur]);set_sprite_palette(0u,2u,pal_sp);
  ui_page("YOUR NEXT HEIR","B BACK     A BEGIN");
  ui_frame(0u,0u,3u,6u,10u,UI_DARK);pick_show(family_cur,1u,4u);num_at(0u,2u,11u,family_cur+1u,2u,UI_DARK);
  text_at(0u,8u,4u,weapon_name[heir.weapon],UI_DARK);text_at(0u,8u,6u,dodge_name[heir.dodge],UI_DARK);
  text_at(0u,8u,8u,"HP",UI_DARK);num_at(0u,18u,8u,6u+family_stat[G_VIGOR],1u,UI_DARK);
  text_at(0u,8u,9u,"POWER",UI_DARK);num_at(0u,18u,9u,family_stat[G_POWER],1u,UI_DARK);
  text_at(0u,8u,10u,"FOCUS",UI_DARK);num_at(0u,18u,10u,family_stat[G_FOCUS],1u,UI_DARK);
  text_at(0u,8u,11u,"PACE",UI_DARK);num_at(0u,18u,11u,family_stat[G_PACE],1u,UI_DARK);
  ui_fill(0u,0u,13u,20u,3u,UI_LIGHT);
  text_at(0u,1u,13u,weapon_tell[heir.weapon],UI_LIGHT);
  text_at(0u,1u,14u,"A STRIKE  B SHOT",UI_LIGHT);text_at(0u,1u,15u,"HOLD EITHER: CHARGE",UI_LIGHT);
  for(k=0u;k!=40u;k++)move_sprite(k,0u,0u);
  ha_dir=0u;heir_idle_stop();heir_play(HA_IDLE,1u);heir_reset();SHOW_SPRITES;heir_draw(0u,144,54);frame_wait();
}
static void draw_choice(void) {
  family_ready=0u;
  if(family_page)draw_pick_detail();else draw_pick_cards();
  SHOW_BKG;frame_wait();input_poll();joy_pressed=0u;family_ready=1u;
}

void family_begin(void) BANKED {
  uint8_t n,k;
  if (dbg_heir) { family_apply(); return; }
  load_family();
  if (!family.active) {
    game_mode=MODE_HEIR;
    pal_commit(0u);DISPLAY_ON;HIDE_WIN;HIDE_SPRITES;SPRITES_8x16;
    hud_line=144u; move_bkg(0,0);
    art_load_game();look_load(BIOME_OFFICE);ui_load();ui_colours();
    family_cur=family_page=0u;
    for(k=0u;k<3u;k++)pick_portrait(k);
    draw_choice();pal_commit(8u);
    while(1) {
      frame_wait(); sound_tick(); input_poll();
      if(!family_page && (joy_pressed & (J_LEFT|J_RIGHT))) {
        uint8_t old=family_cur;
        family_cur=(joy_pressed&J_LEFT) ? (family_cur ? family_cur-1u : 2u) : (family_cur+1u)%3u;
        pick_box(old);pick_box(family_cur);pick_summary();sfx(SFX_STEP);
      }
      if(family_page && (joy_pressed&J_B)) {family_page=0u;draw_choice();continue;}
      if(joy_pressed & (J_A|J_START)) {
        if(family_page) break;
        family_page=1u;draw_choice();continue;
      }
      if(!family_page) continue;
      heir_tick(); n=heir_draw(0u,144,54);
      for(k=n;k!=40u;k++) move_sprite(k,0,0);
    }
    eg_family_choose(&family,family_cur); write_family(); fade_to(8u,0u); HIDE_SPRITES;
  }
  family_apply();
  pl_hp_max=(uint8_t)(6u+family_stat[G_VIGOR]); pl_hp=pl_hp_max;
}
void family_end(uint8_t won) BANKED {
  if(dbg_heir) return;
  eg_family_finish(&family,floor_no,won); write_family();
}
