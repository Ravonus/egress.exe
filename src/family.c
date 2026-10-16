/* SRAM extension: two CRC-checked slots at A100/A300. Legacy meta and both clocks keep their own records. */
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

eg_family __at(0xDA40) family;
typedef char family_fits[(sizeof(eg_family) <= 384u) ? 1 : -1];
uint8_t family_cur, family_stat[G_STATS];
static uint8_t serial;
#define FSLOT(n) ((uint8_t *)(0xA100u + ((uint16_t)(n) << 9)))
#define FLEN ((uint16_t)(sizeof(eg_family) + 4u))
static uint16_t crc(const uint8_t *p, uint16_t n) {
  uint16_t c=65535u; uint8_t k;
  while(n--) { c^=(uint16_t)*p++ << 8; for(k=0;k!=8u;k++) c=c&0x8000u ? (uint16_t)((c<<1)^0x1021u) : (uint16_t)(c<<1); }
  return c;
}
static uint8_t valid(uint8_t *p) {
  return (uint8_t)(p[0]=='F' && p[1]=='A' && p[2]==1u &&
    crc(p,FLEN)==((uint16_t)p[FLEN] | ((uint16_t)p[FLEN+1u]<<8)));
}
static void write_family(void) {
  uint8_t *p; uint16_t c;
  serial++; p=FSLOT(serial&1u);
  EG_SRAM_OPEN();
  p[0]=0u; p[1]='A'; p[2]=1u; p[3]=serial;
  memcpy(p+4u,&family,sizeof family);
  p[0]='F'; c=crc(p,FLEN); p[0]=0u;
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
void family_apply(void) BANKED {
  if (dbg_heir) { heir_roll(dbg_heir); memset(family_stat,0,sizeof family_stat); }
  else apply_gene(&family.current);
  heir_colours();
}
static const char *const weapon_name[5] = {"BLADE","HAMMER","WHIP","GAUNTLET","LAUNCHER"};
static const char *const dodge_name[3] = {"ROLL","BLINK","SLIDE"};
static const char *const weapon_tell[5] = {"BALANCED ARC","SLOW HEAVY BREAK","LONG NARROW REACH","SHORT FAST STRIKE","DISTANT CHARGE"};
static void draw_choice(void) {
  uint8_t k;
  const eg_genome *g=&family.child[family_cur];
  apply_gene(g);
  clear_map(0,4u);
  text_at(0,1,1,"CHOOSE YOUR HEIR",4u);
  text_at(0,1,3,"GEN",4u); num_at(0,5,3,family.generation,3u,4u);
  text_at(0,12,3,"HEIR",4u); num_at(0,17,3,family_cur+1u,1u,4u);
  text_at(0,1,9,weapon_name[heir.weapon],4u);
  text_at(0,12,9,dodge_name[heir.dodge],4u);
  text_at(0,1,10,weapon_tell[heir.weapon],4u);
  text_at(0,1,11,"PWR",4u);num_at(0,5,11,family_stat[G_POWER],1u,4u);
  text_at(0,7,11,"FOCUS",4u);num_at(0,13,11,family_stat[G_FOCUS],1u,4u);
  if(heir.traits&HT_HEAVY) text_at(0,14,11,"HEAVY",4u);
  text_at(0,1,12,"HP",4u); num_at(0,4,12,6u+family_stat[G_VIGOR],1u,4u);
  text_at(0,7,12,"PACE",4u); num_at(0,12,12,(heir.traits&HT_HEAVY ? 18u : 20u)+family_stat[G_PACE],2u,4u);
  text_at(0,1,5,"PARENT",4u);
  if (family.nrecord) num_at(0,3,6,family.history[0].floor,2u,4u);
  else text_at(0,3,6,"--",4u);
  text_at(0,1,13,"A HIT/HOLD CHARGE",4u);
  text_at(0,0,14,heir.weapon==1u || heir.weapon==3u ? "B PULSE/HOLD REPEAT" : "B FIRE/HOLD REPEAT",4u);
  text_at(0,0,15,"DIRx2 EVADE HOLD RUN",4u);
  text_at(0,1,16,"L/R CHOOSE  A PLAY",4u);
  for(k=0;k!=40u;k++) move_sprite(k,0,0);
  ha_dir=0u; heir_idle_stop(); heir_play(HA_IDLE,1u);
  pal_commit(8u);
}
void family_begin(void) BANKED {
  uint8_t n,k;
  if (dbg_heir) { family_apply(); return; }
  load_family();
  if (!family.active) {
    game_mode=MODE_HEIR;
    DISPLAY_OFF; HIDE_WIN; HIDE_SPRITES; SPRITES_8x16;
    hud_line=144u; move_bkg(0,0);
    art_load_game(); look_load(BIOME_OFFICE);
    family_cur=0u; draw_choice(); SHOW_BKG; SHOW_SPRITES; DISPLAY_ON;
    while(1) {
      frame_wait(); sound_tick(); input_poll();
      if(joy_pressed & (J_LEFT|J_RIGHT)) {
        family_cur=(joy_pressed&J_LEFT) ? (family_cur ? family_cur-1u : 2u) : (family_cur+1u)%3u;
        draw_choice(); sfx(SFX_STEP);
      }
      if(joy_pressed & (J_A|J_START)) break;
      heir_tick(); n=heir_draw(0u,76,47);
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
