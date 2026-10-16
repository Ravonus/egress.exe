/* CRC family journal in the reserved tree regions. Migrate checkpoint FA1 journals before continue can write. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "family.h"
#include "heir.h"
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
static uint8_t valid(uint8_t *p, uint8_t version) {
  return (uint8_t)(p[0]=='F' && p[1]=='A' && p[2]==version &&
    eg_save_crc(p,FLEN)==((uint16_t)p[FLEN] | ((uint16_t)p[FLEN+1u]<<8)));
}
static void write_family(void) {
  uint8_t *p; uint16_t c;
  serial++; p=FSLOT(serial&1u);
  EG_SRAM_OPEN();
  p[0]=0u; p[1]='A'; p[2]=2u; p[3]=serial;
  memcpy(p+4u,&family,sizeof family);
  p[0]='F'; c=eg_save_crc(p,FLEN); p[0]=0u;
  p[FLEN]=(uint8_t)c; p[FLEN+1u]=(uint8_t)(c>>8);
  p[0]='F';
  EG_SRAM_CLOSE();
}
static void load_family(void) {
  uint8_t *a=FSLOT(0u), *b=FSLOT(1u), oka,okb,migrating=0u;
  EG_SRAM_OPEN(); oka=valid(a,2u); okb=valid(b,2u);
  if(!oka && !okb) {
    a=(uint8_t *)SAVE_OLD_FAMILY_A;b=(uint8_t *)SAVE_OLD_FAMILY_B;
    oka=valid(a,1u);okb=valid(b,1u);migrating=(uint8_t)(oka || okb);
  }
  if (oka && (!okb || (int8_t)(a[3]-b[3])>=0)) { serial=a[3]; memcpy(&family,a+4u,sizeof family); }
  else if (okb) { serial=b[3]; memcpy(&family,b+4u,sizeof family); }
  EG_SRAM_CLOSE();
  if (!oka && !okb) {
    serial=0u;eg_family_init(&family,run_seed);
    family.generation=meta.runs==65535u ? 65535u : (uint16_t)(meta.runs+1u);
  }
  if(migrating) write_family();
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
static void pick_portrait(uint8_t card) {
  uint8_t n,k,x,y,v,c,raw[32],tile[16],map,attr,px,py,cloth;
  OAM_item_t *o;
  apply_gene(&family.child[card]);heir_idle_stop();heir_play(HA_IDLE,1u);heir_reset();ha_dir=0u;
  n=heir_draw(0u,8,16);frame_wait();memset(pick_pixels,0,16u*24u);
  pal_bg[card*8u]=pal_bg[card*8u+4u]=RGB(10,8,15);
  for(k=1u;k!=4u;k++) {pal_bg[card*8u+k]=pal_sp[k];pal_bg[card*8u+4u+k]=pal_sp[4u+k];}
  while(n) {
    o=&shadow_OAM[--n];get_sprite_data(o->tile,2u,raw);
    px=(uint8_t)(o->x-8u);py=(uint8_t)(o->y-16u);
    for(y=0u;y!=16u;y++) for(x=0u;x!=8u;x++) {
      v=(uint8_t)(((raw[y*2u]>>(7u-x))&1u)|(((raw[y*2u+1u]>>(7u-x))&1u)<<1));
      if(v && px+x<16u && py+y<24u) pick_pixels[(uint16_t)(py+y)*16u+px+x]=(uint8_t)(v+(v>1u && (o->prop&7u)==1u ? 2u : 0u));
    }
  }
  VBK_REG=0u;
  for(k=0u;k!=6u;k++) {
    cloth=0u;px=(uint8_t)((k&1u)*8u);py=(uint8_t)((k>>1)*8u);
    for(y=0u;y!=8u;y++) for(x=0u;x!=8u;x++) if(pick_pixels[(uint16_t)(py+y)*16u+px+x]>=4u) cloth++;
    cloth=(uint8_t)(cloth>=16u);
    memset(tile,0,sizeof tile);
    for(y=0u;y!=8u;y++) for(x=0u;x!=8u;x++) {
      v=pick_pixels[(uint16_t)(py+y)*16u+px+x];if(v>=4u) v-=2u;
      if(v&1u) tile[y*2u]|=(uint8_t)(128u>>x);
      if(v&2u) tile[y*2u+1u]|=(uint8_t)(128u>>x);
    }
    map=(uint8_t)(80u+card*6u+k);set_bkg_data(map,1u,tile);
    attr=(uint8_t)(card*2u+cloth);VBK_REG=1u;set_bkg_tiles((uint8_t)(card*7u+2u+(k&1u)),(uint8_t)(5u+(k>>1)),1u,1u,&attr);
    VBK_REG=0u;set_bkg_tiles((uint8_t)(card*7u+2u+(k&1u)),(uint8_t)(5u+(k>>1)),1u,1u,&map);
  }
  for(c=0u;c!=40u;c++) move_sprite(c,0u,0u);
}
static void pick_box(uint8_t x,uint8_t selected) {
  uint8_t y;
  text_at(0,x,3u,"+----+",selected ? 6u : 7u);
  for(y=4u;y!=10u;y++) {text_at(0,x,y,"+",selected ? 6u : 7u);text_at(0,(uint8_t)(x+5u),y,"+",selected ? 6u : 7u);}
  text_at(0,x,10u,"+----+",selected ? 6u : 7u);
}
static void draw_pick_cards(void) {
  uint8_t k;
  clear_map(0,6u);text_at(0,2u,1u,"THE NEXT HEIR",6u);
  text_at(0,2u,2u,"GENERATION",6u);num_at(0,13u,2u,family.generation,3u,6u);
  for(k=0u;k!=3u;k++) {
    pick_box((uint8_t)(k*7u),(uint8_t)(k==family_cur));pick_portrait(k);
    num_at(0,(uint8_t)(k*7u+2u),4u,k+1u,2u,6u);
    text_at(0,(uint8_t)(k*7u+2u),11u,k==family_cur ? ">" : " ",6u);
  }
  apply_gene(&family.child[family_cur]);
  text_at(0,1u,13u,weapon_name[heir.weapon],6u);text_at(0,12u,13u,dodge_name[heir.dodge],6u);
  text_at(0,1u,14u,"HP",6u);num_at(0,4u,14u,6u+family_stat[G_VIGOR],1u,6u);
  text_at(0,7u,14u,"PWR",6u);num_at(0,11u,14u,family_stat[G_POWER],1u,6u);
  text_at(0,1u,16u,"L/R CHOOSE A DETAILS",6u);
  pal_bg[24]=pal_bg[28]=RGB(7,5,11);pal_bg[25]=pal_bg[29]=RGB(10,8,15);
  pal_bg[26]=RGB(25,20,31);pal_bg[27]=RGB(30,28,31);pal_bg[30]=RGB(15,12,20);pal_bg[31]=RGB(22,19,27);
  HIDE_SPRITES;
}
static void draw_pick_detail(void) {
  uint8_t k;
  apply_gene(&family.child[family_cur]);look_load(BIOME_OFFICE);clear_map(0,4u);
  text_at(0,2u,1u,"YOUR NEXT HEIR",4u);num_at(0,16u,1u,family_cur+1u,1u,4u);
  text_at(0,2u,7u,weapon_name[heir.weapon],4u);text_at(0,12u,7u,dodge_name[heir.dodge],4u);
  text_at(0,1u,9u,weapon_tell[heir.weapon],4u);
  text_at(0,2u,11u,"HP",4u);num_at(0,7u,11u,6u+family_stat[G_VIGOR],1u,4u);
  text_at(0,2u,12u,"POWER",4u);num_at(0,7u,12u,family_stat[G_POWER],1u,4u);
  text_at(0,10u,11u,"FOCUS",4u);num_at(0,17u,11u,family_stat[G_FOCUS],1u,4u);
  text_at(0,10u,12u,"PACE",4u);num_at(0,17u,12u,family_stat[G_PACE],1u,4u);
  text_at(0,1u,14u,"A HIT  B SHOT",4u);text_at(0,1u,15u,"HOLD EITHER: CHARGE",4u);
  text_at(0,1u,17u,"B BACK     A BEGIN",4u);
  for(k=0u;k!=40u;k++) move_sprite(k,0u,0u);
  ha_dir=0u;heir_idle_stop();heir_play(HA_IDLE,1u);heir_reset();SHOW_SPRITES;heir_draw(0u,80,48);frame_wait();
}
static void draw_choice(void) {
  family_ready=0u;pal_commit(0u);DISPLAY_ON;
  if(family_page) draw_pick_detail();else draw_pick_cards();
  pal_commit(8u);SHOW_BKG;DISPLAY_ON;frame_wait();
  /* Establish the pad after the page finishes drawing: a released clock/menu
   * confirmation must not remain latched through portrait preparation. */
  input_poll();joy_pressed=0u;family_ready=1u;
}

void family_begin(void) BANKED {
  uint8_t n,k;
  if (dbg_heir) { family_apply(); return; }
  load_family();
  if (!family.active) {
    game_mode=MODE_HEIR;
    pal_commit(0u);DISPLAY_ON;HIDE_WIN;HIDE_SPRITES;SPRITES_8x16;
    hud_line=144u; move_bkg(0,0);
    art_load_game(); look_load(BIOME_OFFICE);
    family_cur=family_page=0u;draw_choice();
    while(1) {
      frame_wait(); sound_tick(); input_poll();
      if(!family_page && (joy_pressed & (J_LEFT|J_RIGHT))) {
        family_cur=(joy_pressed&J_LEFT) ? (family_cur ? family_cur-1u : 2u) : (family_cur+1u)%3u;
        draw_choice(); sfx(SFX_STEP);
      }
      if(family_page && (joy_pressed&J_B)) {family_page=0u;draw_choice();continue;}
      if(joy_pressed & (J_A|J_START)) {
        if(family_page) break;
        family_page=1u;draw_choice();continue;
      }
      if(!family_page) continue;
      heir_tick(); n=heir_draw(0u,80,48);
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
