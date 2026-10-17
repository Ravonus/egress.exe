/* B1 puzzle rooms on the cartridge: the pocket, physical seeded benches (A), using a piece on the cell you face, the
 * reaction as it spreads, and how the reaction tiles are drawn until the look branch's tileset lands.
 *
 * The rules are the shared core's (src/core/eg_piece.c: eg_recipe, eg_apply), so the cartridge does exactly what the
 * host solver planned. Globals the PyBoy harness reads are plain: pocket, pocket_n, bench_on, bench_cur, last_rx. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "puzzle.h"
#include "obstacle.h"
#include "core/eg_piece.h"
#include "core/eg_plan.h"
#include "run.h"
#include "look.h"
#include "mob.h"
#include "heir.h"
#include "family.h"
#include "narrator.h"
#include "tutorial.h"
#include "world.h"
#include "world_dead.h"
#include "ui.h"
#include "stamina.h"
#include "stage.h"
#include "core/eg_move.h"
extern eg_mover_t pmv;
void items_bench_tick(uint8_t a,uint8_t b,uint8_t result) BANKED;
void items_tick(void) BANKED;
extern volatile uint8_t video_epoch;
extern uint8_t frame_epoch;

/* Bank 1 floor state, after the mover map; title playback uses bank 2 here. */
uint8_t __at(0xDBC0) pocket[POCKET_CAP];
uint8_t pocket_n;
static uint8_t __at(0xDBD8) entry_pocket[POCKET_CAP];
static uint8_t entry_n;
/* Bank 1 has 28 spare bytes between the four-byte stamina record and bag. */
static uint8_t __at(0xDBA4) entry_tool;
typedef char tool_entry_spare[(sizeof(eg_stamina)==4u) ? 1 : -1];
uint8_t tool_piece,tool_cd;
uint8_t __at(0xDBA5) bench_mode;
static uint8_t __at(0xDBA6) bench_hint;
static uint8_t __at(0xDBA7) bench_ready;
uint8_t puzzle_dirty, frag_skip, floor_resets;
eg_plan_state plan;
static uint8_t pl_cell, pl_safe, beat_was, frag_pend, has_glitch;
uint8_t dead_t; /* the dead-room hint: 0 none, counting down, 1 shown (the harness reads it) */
uint8_t dead_run; /* the sliced dead-room check is under way (the harness reads it) */
#define DEAD_SLICE 4u /* queue pops a frame (~3.5 lines each): a whole check is one or two floods, about a second */
static uint8_t frag_t[4][3]; /* global x/y, ticks of 8 frames left (0: free) */
uint8_t bench_phase; /* 0 select, 1 approach, 2 contact, 3 reaction, 4 reveal */
uint8_t bench_on, bench_cur, last_rx; /* 1 listening, 2 use, 3 combination, 4 paused redraw */
static uint8_t sock[2], told;
static char buf[20];
static uint8_t bench_cam_y;

void pocket_begin(void) BANKED { memset(pocket, 0, sizeof pocket); pocket_n = tool_piece = tool_cd = 0; }
void pocket_enter(void) BANKED { memcpy(entry_pocket, pocket, sizeof pocket); entry_n = pocket_n; entry_tool=tool_piece; }
void pocket_restore(void) BANKED { memcpy(pocket, entry_pocket, sizeof pocket); pocket_n = entry_n; tool_piece=entry_tool;tool_cd=0u;hud_dirty=1u; }

void pocket_reset(void) BANKED {
  uint8_t i;
  memset(frag_t, 0, sizeof frag_t);
  pl_cell = pl_safe = room.start;
  frag_pend = 255u;
  puzzle_dirty = frag_skip = dead_t = dead_run = 0;
  beat_was = 255u;
  look_glitch_period = 0; /* the puzzle drives the beat (frame-based, EG_BEAT_LEN; the solver's model is a window) */
  bench_on=tool_cd=bench_mode=0u;bench_hint=bench_ready=255u;
  bench_cur = 0;
  last_rx = 0;
  has_glitch = 0;
  for (i = 0; i != (uint8_t)EG_N; i++)
    if (cell_type(i) == T_GLITCH) has_glitch = 1;
}
void puzzle_shift_view(int8_t dx,int8_t dy) BANKED {
  uint8_t c=cell_at(pl_x,pl_y),i,x,y,from;
  pl_cell=eg_view_rebase(pl_cell,dx,dy);pl_safe=eg_view_rebase(pl_safe,dx,dy);frag_pend=eg_view_rebase(frag_pend,dx,dy);
  if(pl_cell>=EG_N)pl_cell=c;if(pl_safe>=EG_N)pl_safe=c;
  dead_run=0u;puzzle_dirty=1u;
  if(world_on) {has_glitch|=world_glitch;return;}
  if(has_glitch)return;
  if(dx) {
    from=dx>0 ? 12u : 0u;
    for(y=0u;y<14u;y++)for(x=from;x<from+4u;x++)if(cell_type(EG_IDX(x,y))==T_GLITCH)has_glitch=1u;
  } else {
    from=dy>0 ? 10u : 0u;
    for(y=from;y<from+4u;y++)for(x=0u;x<16u;x++) {i=EG_IDX(x,y);if(cell_type(i)==T_GLITCH)has_glitch=1u;}
  }
}

static void pocket_take(uint8_t slot) {
  uint8_t k;
  for (k = slot; k + 1u < pocket_n; k++) pocket[k] = pocket[(uint8_t)(k + 1u)];
  pocket_n--;
  pocket[pocket_n] = 0;
  hud_dirty=1u;
}

/* the player walked into a piece lying in the room */
void puzzle_pick(uint8_t cell) BANKED {
  tutorial_queue(TE_BENCH);
  uint8_t p = eg_item_at(&room, cell);
  if (!p) return;
  if (pocket_n >= POCKET_CAP) { hud_msg("SATCHEL FULL: USE IT"); return; }
  pocket[pocket_n++] = p;
  if(!tool_piece)tool_piece=p;
  hud_dirty=1u;
  room.c[cell] = T_EMPTY;
  queue_cell(cell);
  sfx(SFX_KEY);
  puff(pl_x, (uint8_t)(pl_y - 10u), 4, ST_SPARK);
  if (acts.picked != 255u) acts.picked++;
  if (!told) {
    told = 1;
    hud_msg("FIND A BENCH TO MIX");
  } else {
    eg_piece_name(p, buf);
    hud_msg(buf);
  }
}

/* --- the cards (b1-look, src/gen/look_art.h) the puzzle cells show; a fragile kind's stage is its first card + the
 * steps taken, the same order as the cell ids (src/core/eg_room.h) --- */
static const uint8_t card[T_TYPES - T_CRUMBLE] = {
    M_CRUMBLE,      /* T_CRUMBLE */
    M_PIT,          /* T_PIT */
    M_PLATE,        /* T_PLATE */
    M_GATE,         /* T_GATE */
    M_GATE_OPEN,    /* T_GATE_OPEN */
    M_WET,          /* T_WATER (the look's placeholder reaction card) */
    M_GLITCH0,      /* T_GLITCH: + the beat's phase (puzzle_meta) */
    M_WALL_NOTE,    /* T_PAPER: a wall of notes and posters */
    M_SLAB_CRACK,   /* T_PLANK: cardboard over a hole */
    M_SLAB_A,       /* T_ITEM: its piece's card, M_ITEM0 + piece - 1 (puzzle_meta) */
    M_WALL_SWITCH,  /* T_SOCKET */
    M_BATTERY,      /* T_SOCKET_ON */
    M_GATE,         /* T_DEAD: a dead shutter */
    M_CRATE,        /* T_BLOCK: a laid brick */
    M_CRATE,        /* T_BLOCK_PLATE */
    M_CARPET0,      /* T_WORN */
    M_CARPET0 + 1u, /* T_WORN_1 */
    M_CARPET0 + 2u, /* T_WORN_2 */
    M_CEIL0,        /* T_CEIL */
    M_CEIL0 + 1u,   /* T_CEIL_1 */
    M_GLASS,        /* T_GLASS */
    M_ICE,          /* T_ICE: thin ice */
    M_ICE1,         /* T_ICE_1: cracked, one step from giving way */
    M_GLASS,        /* T_PARTITION: office glass, broken only by a heavy */
};

/* the glitch beat as the look shows it: solid, flicker at its end, gone, flicker before it comes back */
static uint8_t beat_phase(void) {
  uint8_t f = (uint8_t)(frame_count & (EG_BEAT_LEN - 1u));
  return f < EG_BEAT_ON - 16u ? 0u : (f < EG_BEAT_ON ? 1u : (f < EG_BEAT_LEN - 16u ? 2u : 1u));
}

uint8_t puzzle_meta(uint8_t i) BANKED {
  uint8_t t = cell_type(i);
  if((room.portal_need&EG_PORTAL_ON) && (i==room.portal[0] || i==room.portal[1]) && (t==T_EMPTY || t==T_PLATE)) {
    static const uint8_t phase[4]={1u,2u,3u,2u};
    uint8_t p=cell_at(pl_x,pl_y);
    return (uint8_t)(M_PORTAL0+(eg_portal_target(&room,i,&p,(uint8_t)(pl_hp && !pl_air && pl_state!=PS_FALL))!=EG_LINK_NONE ? phase[(frame_count>>4)&3u] : 0u));
  }
  if (t == T_GLITCH) return (uint8_t)(M_GLITCH0 + beat_phase());
  if(t==T_PLATE && pl_hp && !pl_air && pl_state!=PS_FALL &&
     absdiff((uint8_t)(EG_X(i)*16u+8u),pl_x)<14u && absdiff((uint8_t)(EG_Y(i)*16u+8u),pl_y)<14u)
    return M_PLATE_DOWN;
  if (t == T_ITEM) { /* the piece lying there, as an office thing that carries its element */
    t = eg_item_at(&room, i);
    return (t && t <= LOOK_ITEMS) ? (uint8_t)(M_ITEM0 - 1u + t) : M_SLAB_A;
  }
  return card[t - T_CRUMBLE];
}

/* --- using a piece --- */
static uint8_t body(uint8_t *occ) {
  uint8_t n = 0, c, k, a, b, xs[2], ys[2];
  xs[0] = (uint8_t)(pl_x - 6u); /* the mover's 12x12 box (core/eg_move) */
  xs[1] = (uint8_t)(pl_x + 5u);
  ys[0] = (uint8_t)(pl_y - 6u);
  ys[1] = (uint8_t)(pl_y + 5u);
  for (a = 0; a != 2u; a++)
    for (b = 0; b != 2u; b++) {
      c = cell_at(xs[b], ys[a]);
      for (k = 0; k != n; k++)
        if (occ[k] == c) break;
      if (k == n) occ[n++] = c;
    }
  return n;
}

static void wait_frames(uint8_t n) {
  while (n--) {
    frame_wait();
    sound_tick();
    hud_tick();
    draw_sprites();
  }
}

/* A stable piece ID is the quick slot; compacting the satchel never changes it. */
uint8_t pocket_count(uint8_t piece) BANKED {
  uint8_t k,n=0u;
  for(k=0u;k<pocket_n;k++)if(pocket[k]==piece)n++;
  return n;
}
static uint8_t tool_fx(uint8_t p) {
  if(p==P_FIRE || p==P_LAVA)return FX_HOT;
  if(p==P_WATER || p==P_RAIN || p==P_CLOUD)return FX_WET;
  if(p==P_SNOW || p==P_ICE)return FX_COLD;
  if(p==P_ELECTRIC || p==P_ENERGY)return FX_SHOCK;
  if(p==P_EARTH || p==P_MUD || p==P_BRICK || p==P_METAL)return FX_STONE;
  return FX_KILL; /* wind, dust and steam disperse */
}
static void cast_fx(uint8_t p,uint8_t x,uint8_t y) {
  uint8_t kind=tool_fx(p);
  player_tool_cast();
  fx_burst(pl_x,(uint8_t)(pl_y-9u),kind,2u);
  fx_burst(x,y,kind,4u);
  sfx(kind==FX_HOT ? SFX_BREAK : kind==FX_WET ? SFX_PUSH : kind==FX_COLD ? SFX_SHATTER :
      kind==FX_SHOCK ? SFX_ZAP : kind==FX_STONE ? SFX_THUD : SFX_SPIN);
}

/* SELECT release uses one finite unit. Failed targets keep it. The ordinary
 * shared reaction table owns the puzzle changes and the combat conditions. */
void puzzle_tool_use(void) BANKED {
  static const uint8_t cost[P_COUNT]={0u,12u,8u,10u,10u,12u,14u,10u,12u,14u,16u,16u,18u,12u,20u,8u,10u};
  static const int8_t dx[8]={0,1,1,1,0,-1,-1,-1},dy[8]={-1,-1,0,1,1,1,0,-1};
  uint8_t occ[4],n,here=cell_at(pl_x,pl_y),cell,k,slot,p=tool_piece,rx,nd;
  uint8_t order[EG_DIRTY_MAX];
  int16_t x=(int16_t)EG_X(here)+dx[pmv.facing],y=(int16_t)EG_Y(here)+dy[pmv.facing];
  if(tool_cd || pl_state!=PS_FREE || pl_air || freeze)return;
  if(!p || p>=P_COUNT) {sfx(SFX_DENY);hud_msg("START: INVENTORY");return;}
  for(slot=0u;slot<pocket_n;slot++)if(pocket[slot]==p)break;
  if(slot==pocket_n) {sfx(SFX_DENY);hud_msg("EMPTY: EQUIP AN ITEM");return;}
  if(stamina.value<cost[p]) {sfx(SFX_DENY);hud_msg("REST: LOW STAMINA");return;}
  if(x<0 || x>=EG_W || y<0 || y>=EG_H) {sfx(SFX_DENY);hud_msg("NO TARGET: KEPT");return;}
  cell=EG_IDX((uint8_t)x,(uint8_t)y);
  /* Combat follows the feet and facing, rather than a tile centre. An enemy
   * touching the heir must not slip through a gap at the cell boundary. */
  x=(int16_t)pl_x+dx[pmv.facing]*12;y=(int16_t)pl_y+dy[pmv.facing]*12;
  for(k=0u;k<nen;k++) {
    if(en[k].st==ES_DEAD || !overlap_world(x,y,(uint8_t)(en[k].x>>4),(uint8_t)(en[k].y>>4),14u,14u))continue;
    if(en[k].kind!=SPAWN_MOB || !mob_tool(k,p)) {sfx(SFX_DENY);hud_msg("BLOCKED: ITEM KEPT");return;}
    cast_fx(p,(uint8_t)(en[k].x>>4),(uint8_t)(en[k].y>>4));
    narrator_event(NE_USE,(uint8_t)(p|(pocket_n==1u ? 128u : 0u)));
    pocket_take(slot);eg_stamina_spend(&stamina,cost[p]);tool_cd=24u;
    hud_msg("ELEMENT USED ON MOB");return;
  }
  n=body(occ);rx=eg_apply(&room,p,cell,occ,n);
  if(!rx) {narrator_event(NE_REFUSED,0u);sfx(SFX_DENY);hud_msg("NO EFFECT: KEPT");return;}
  last_rx=rx;nd=eg_ndirty;
  if(nd<=EG_DIRTY_MAX)memcpy(order,eg_dirty,nd);
  cast_fx(p,(uint8_t)(EG_X(cell)*16u+8u),(uint8_t)(EG_Y(cell)*16u+8u));
  narrator_event(NE_USE,(uint8_t)(p|(pocket_n==1u ? 128u : 0u)));
  pocket_take(slot);eg_stamina_spend(&stamina,cost[p]);tool_cd=24u;
  eg_rx_name(rx,buf);hud_msg(buf);
  /* A connected reaction retains its short reveal. No new particle/card pool
   * or frame-sized allocation is needed; the heir advances during the spread. */
  bench_on=2u;
  if(nd>EG_DIRTY_MAX) {
    for(k=0u;k!=EG_N;k++) {draw_cell(k);if(!(k&7u))wait_frames(1u);}
  } else for(k=0u;k<nd;k++) {
    cell=order[k];queue_cell(cell);draw_cell(cell);
    if(k<4u)fx_burst((uint8_t)(EG_X(cell)*16u+8u),(uint8_t)(EG_Y(cell)*16u+8u),tool_fx(p),2u);
    wait_frames(2u);
  }
  bench_on=0u;
  if(rx==RX_SHORT)shake=6u;
  puzzle_dirty=grav_t=1u;
}

/* Benches are solid generator cells. A nearby locked bench never steals an
 * attack: combat continues, with a short cue explaining the lock. */
static uint8_t near_bench(void) {
  uint8_t c=cell_at(pl_x,pl_y),k;int16_t n;
  static const int8_t delta[4]={-16,16,-1,1};
  for(k=0u;k<4u;k++) {
    if((k==0u && !EG_Y(c)) || (k==1u && EG_Y(c)==13u) || (k==2u && !EG_X(c)) || (k==3u && EG_X(c)==15u))continue;
    n=(int16_t)c+delta[k];if(EG_BENCH(room.c[n]))return (uint8_t)n;
  }
  return 255u;
}
uint8_t puzzle_bench_interact(void) BANKED {
  if(near_bench()==255u)return 0u;
  if(!stage_bench_ready()) {hud_msg("BENCH: CLEAR MOBS");sfx(SFX_DENY);return 0u;}
  if(board_busy) {hud_msg("BENCH: WAIT FOR LOAD");return 0u;}
  bench_mode=1u;bench_run();return 1u;
}
static void bench_near_tick(void) {
  uint8_t c,ready;
  if(frame_count&15u)return;
  c=near_bench();
  if(c==255u) {bench_hint=255u;return;}
  ready=stage_bench_ready();
  if(c==bench_hint && ready==bench_ready)return;
  bench_hint=c;bench_ready=ready;
  if(ready) {hud_msg("BENCH: A TO COMBINE");puff((uint8_t)(EG_X(c)*16u+8u),(uint8_t)(EG_Y(c)*16u),4u,ST_SPARK);}
  else hud_msg("BENCH: CLEAR MOBS");
}

/* --- the bench: an overlay on the window, over the bottom of the room --- */
#define BENCH_Y 72u
#define BP LOOK_PAL_HUD     /* the bench's ink */
#define HP LOOK_PAL_REACT_B /* what is chosen */
static void line(uint8_t y, const char *s) {
  text_at(1, 0, y, "                    ", BP);
  text_at(1, 0, y, s, BP);
}

static uint8_t first_slot(uint8_t k) {
  uint8_t j;
  for(j=0u;j<k;j++) if(pocket[j]==pocket[k]) return j;
  return k;
}

static void bench_step(uint8_t up) {
  uint8_t k=bench_cur;
  if(!pocket_n) return;
  do {
    k=up ? (k ? k-1u : pocket_n-1u) : (k+1u<pocket_n ? k+1u : 0u);
  } while(first_slot(k)!=k);
  bench_cur=k;
}

static void bench_card(uint8_t x,uint8_t y,uint8_t m) {
  uint8_t a[4],p=meta_pal[m];
  memset(a,(p&LOOK_META_BANK0) ? (p&(7u|LOOK_META_FLIP)) : (p|8u),sizeof a);
  VBK_REG=1u;set_win_tiles(x,y,2u,2u,a);
  VBK_REG=0u;set_win_tiles(x,y,2u,2u,meta_tile[m]);
}
static void bench_icon(uint8_t x,uint8_t y,uint8_t p,uint8_t chosen) {
  uint8_t t=(uint8_t)(LOOK_ICON_BASE+p-1u),a=(uint8_t)(meta_pal[M_ITEM0+p-1u]&7u);
  VBK_REG=1u;set_win_tiles(x,y,1u,1u,&a);
  VBK_REG=0u;set_win_tiles(x,y,1u,1u,&t);
  if(chosen)text_at(1u,x-1u,y,">",HP);
}
static void bench_scene(uint8_t phase,uint8_t result) {
  uint8_t y;
  for(y=1u;y!=3u;y++)text_at(1u,1u,y,"                  ",BP);
  if(result) {
    bench_card(9u,1u,(uint8_t)(M_ITEM0+result-1u));
    text_at(1u,6u,1u,"+",HP);text_at(1u,13u,2u,"+",HP);
  } else {
    if(sock[0]!=255u)bench_card((uint8_t)(2u+phase),1u,(uint8_t)(M_ITEM0+pocket[sock[0]]-1u));
    else text_at(1u,2u,1u,"[ ]",BP);
    if(sock[1]!=255u)bench_card((uint8_t)(14u-phase),1u,(uint8_t)(M_ITEM0+pocket[sock[1]]-1u));
    else text_at(1u,14u,1u,"[ ]",BP);
    if(!phase)text_at(1u,9u,1u,"+",HP);
  }
}
static void bench_wait(uint8_t n,uint8_t r) {
  while(n--) {
    frame_wait();sound_tick();
    items_bench_tick(pocket[sock[0]],pocket[sock[1]],r);
    draw_sprites();
  }
}
static void bench_combine(uint8_t result) {
  uint8_t phase;
  line(3u,"    BRING TOGETHER");line(4u,"");line(5u,"");
  bench_phase=1u;
  for(phase=0u;phase!=6u;phase++) {
    bench_scene(phase,0u);
    if(!(phase&1u))sfx(SFX_PUSH);
    bench_wait(6u,0u);
  }
  bench_phase=2u;bench_scene(6u,0u);sfx(SFX_CHARGED);
  line(3u,"      REACTING");
  bench_wait(8u,0u);
  bench_phase=3u;
  for(phase=0u;phase!=4u;phase++) {
    bench_scene(6u,0u);
    text_at(1u,(uint8_t)(6u+phase),1u,"+",HP);
    text_at(1u,(uint8_t)(13u-phase),2u,"+",HP);
    bench_wait(4u,0u);
  }
  bench_phase=4u;bench_scene(0u,result);sfx(SFX_KEY);
  eg_piece_name(result,buf);line(3u,buf);
  eg_tags_text(result,buf);line(4u,buf);line(5u,"    A NEW MATERIAL");
  bench_wait(24u,result);
}
static const char *const piece_hint[P_COUNT]={
  "PICK TWO MATERIALS","BURNS PAPER / ICE","WETS / EXTINGUISHES","PUSHES LIGHT CRATES",
  "MIX WITH WATER","MIX WITH ENERGY","MIX WITH METAL","WETS YOUR TARGET",
  "SLOWS A MOB","FREEZES WATER / MOBS","FREEZES WATER / MOBS","WEIGHS DOWN PLATES",
  "POWERS WET CIRCUITS","HOT MIST BLINDS","BURNS PAPER / ICE","DUST BLINDS A MOB","WETS YOUR TARGET"
};
static void bench_draw(void) {
  uint8_t k,j,row,count,r=0u,ordinal=0u,page,x,p;
  bench_phase=0u;
  if(pocket_n)bench_cur=first_slot(bench_cur);
  for(k=0u;k<bench_cur;k++)if(first_slot(k)==k)ordinal++;
  page=(uint8_t)((ordinal/6u)*6u);
  ui_fill(1u,0u,0u,20u,8u,BP);ui_frame(1u,0u,0u,20u,8u,BP);
  text_at(1u,1u,0u,bench_mode ? "BENCH" : "BAG",BP);text_at(1u,7u,0u,"SEL EQ",pocket_n && pocket[bench_cur]==tool_piece ? HP : BP);num_at(1u,13u,0u,pocket_n,2u,BP);text_at(1u,15u,0u,"/24",BP);
  if(sock[1]!=255u)r=eg_recipe(pocket[sock[0]],pocket[sock[1]]);
  if(r) {
    /* The complete recipe reads left to right, as in the reference panel. */
    bench_card(2u,1u,(uint8_t)(M_ITEM0+pocket[sock[0]]-1u));
    bench_card(8u,1u,(uint8_t)(M_ITEM0+pocket[sock[1]]-1u));
    bench_card(15u,1u,(uint8_t)(M_ITEM0+r-1u));
    text_at(1u,6u,1u,"+",HP);text_at(1u,12u,1u,">",HP);
  } else if(bench_mode)bench_scene(0u,0u);
  else if(pocket_n)bench_card(9u,1u,(uint8_t)(M_ITEM0+pocket[bench_cur]-1u));
  p=r ? r : pocket_n ? pocket[bench_cur] : 0u;
  eg_piece_name(p,buf);text_at(1u,1u,3u,buf,HP);
  eg_tags_text(p,buf);text_at(1u,1u,4u,buf,BP);
  text_at(1u,1u,5u,piece_hint[p],BP);
  k=0u;ordinal=0u;
  for(row=0u;row!=6u;row++) {
    while(k<pocket_n && (first_slot(k)!=k || ordinal<page)) {
      if(first_slot(k)==k)ordinal++;
      k++;
    }
    if(k>=pocket_n)continue;
    count=0u;for(j=0u;j<pocket_n;j++)if(pocket[j]==pocket[k])count++;
    x=(uint8_t)(2u+row*3u);
    bench_icon(x,6u,pocket[k],(uint8_t)(k==bench_cur));
    num_at(1u,x-1u,7u,count,2u,(uint8_t)(k==bench_cur ? HP : BP));
    k++;
  }
  line(8u,!bench_mode ? "SEL EQUIP B BACK" : sock[1]!=255u ? "A MIX B UNDO ST MENU" : "A ADD B BACK ST MENU");
}

static void bench_close(void) {
  bench_on = 4u;
  cam_y=bench_cam_y;move_bkg(BG_SCROLL_X(cam_x),BG_SCROLL_Y(cam_y));
  clear_map(1, BP);
  hud_draw();
  hud_charge();
  move_win(7, 128);
  hud_line = 128u;
  frame_wait(); /* finish the paused redraw before active play resumes */
  bench_on = bench_phase = 0u;
}

void bench_run(void) BANKED {
  uint8_t a,b,r,drop_t=0u,menu=0u;

  bench_on=4u; /* opening: gameplay is paused while the existing scene draws */
  sock[0] = sock[1] = 255u;
  if (bench_cur >= pocket_n) bench_cur = 0;
  bench_cam_y=cam_y;
  cam_y=pl_y>40u ? (uint8_t)(pl_y-40u) : 0u;
  if(cam_y>152u) cam_y=152u;
  move_bkg(BG_SCROLL_X(cam_x),BG_SCROLL_Y(cam_y));
  ui_load();clear_map(1, BP);
  bench_draw();
  hud_line = BENCH_Y;
  move_win(7, BENCH_Y);
  sfx(SFX_STEP);
  bench_on = 1; /* up and listening (the harness waits for this) */
  heir_idle_stop();heir_play(bench_mode ? HA_BENCH : HA_PICKUP,1u);
  while (1) {
    frame_wait(); /* no hud_tick here: a toast closing would move the window under the bench */
    sound_tick();
    items_bench_tick(sock[0]!=255u ? pocket[sock[0]] : pocket_n ? pocket[bench_cur] : 0u,
      sock[1]!=255u ? pocket[sock[1]] : 0u,sock[1]!=255u ? eg_recipe(pocket[sock[0]],pocket[sock[1]]) : 0u);
    if (!(frame_count&31u)) {
      uint8_t target=(uint8_t)(cell_at(pl_x,pl_y)+(uint8_t)(fdy[pl_face]*(int8_t)EG_W)+(uint8_t)fdx[pl_face]);
      puff((uint8_t)(EG_X(target)*16u+8u),(uint8_t)(EG_Y(target)*16u+8u),4u,ST_SPARK);
    }
    draw_sprites();
    input_poll();
    if(joy&J_RIGHT) {
      if(drop_t<31u) drop_t++;
      if(drop_t==1u) line(8,"HOLD RIGHT: DROP ONE");
      if(drop_t==30u && pocket_n) {
        pocket_take(bench_cur);sock[0]=sock[1]=255u;
        puzzle_dirty=1u;
        if(bench_cur>=pocket_n) bench_cur=0u;
        bench_draw();line(8,"ONE UNIT DISCARDED");sfx(SFX_STEP);
      }
      continue;
    }
    if(drop_t) { drop_t=0u;bench_draw(); }
    if ((joy_pressed & J_SELECT) && pocket_n) {
      tool_piece=pocket[bench_cur];hud_dirty=1u;
      sfx(SFX_KEY);bench_close();hud_msg("SELECT: ITEM READY");bench_on=6u;return;
    }
    if (joy_pressed & (J_UP | J_DOWN)) {
      if (pocket_n) {
        bench_step((uint8_t)(joy_pressed & J_UP));
        sfx(SFX_STEP);
        bench_draw();
      }
    } else if (joy_pressed & J_A) {
      if(!bench_mode) {sfx(SFX_DENY);line(5u,"CRAFT AT A BENCH");continue;}
      if(!stage_bench_ready()) {sfx(SFX_DENY);line(5u,"CLEAR THE MOBS FIRST");continue;}
      if (sock[1] != 255u) { /* make it */

        a = sock[0];
        b = sock[1];
        r = eg_recipe(pocket[a], pocket[b]);
        if (!r) {
          narrator_event(NE_REFUSED,0u);
          sfx(SFX_DENY);
          line(5u,"NO RECIPE: BOTH KEPT");
          continue;
        }
        bench_on=3u;bench_combine(r);
        pocket_take(a > b ? a : b);
        pocket_take(a > b ? b : a);
        pocket[pocket_n++] = r;
        tool_piece=r;hud_dirty=1u;
        puzzle_dirty=1u;
        narrator_event(NE_MIX,r);
        bench_cur = (uint8_t)(pocket_n - 1u);
        sock[0] = sock[1] = 255u;
        sfx(SFX_CHARGED);
        bench_draw();
        bench_on=1u;
      } else if (pocket_n && sock[0] != bench_cur) {
        sock[sock[0] == 255u ? 0u : 1u] = bench_cur;
        sfx(SFX_PUSH);
        bench_draw();
      } else
        sfx(SFX_DENY);
    } else if (joy_pressed & J_B) {
      if (sock[0] == 255u) break;
      sock[0] = sock[1] = 255u;
      sfx(SFX_STEP);
      bench_draw();
    } else if (joy_pressed & J_START) {
      menu=1u;break;
    }
  }
  bench_close();
  bench_on=menu ? 5u : 6u; /* menu request or closed: suppress its held SELECT release */
}

/* --- the run planner: the curriculum picks every generated floor's room (src/core/eg_plan.h) --- */
void puzzle_plan_begin(void) BANKED {
  eg_plan_begin(&plan, run_seed, (uint16_t)(MK_FROM_SEEN(meta.seen) | (dbg_heir ? 0u : family.seen)), meta.runs > 255u ? 255u : (uint8_t)meta.runs);
}

void puzzle_plan_floor(void) BANKED {
  eg_seed_rec e;
  floor_resets = 0;
  if (dbg_fseed) { /* the harness: this exact room */
    floor_seed = dbg_fseed;
    floor_tier = dbg_ftier;
    dbg_fseed = 0;
    return;
  }
  eg_plan_floor(&plan, floor_no, &e);
  floor_seed = e.seed;
  floor_tier = e.tier;
}

void puzzle_plan_done(void) BANKED {
  eg_plan_result(&plan, floor_resets, acts.frames);
  meta.seen |= MK_TO_SEEN(plan.seen);
  if(!dbg_heir) family.seen |= plan.seen;
}

/* holding SELECT put the room back: it costs a token (never a heart), and the matrix remembers */
void puzzle_reset_cost(void) BANKED {
  narrator_event(NE_RESET,0u);
  if (floor_resets != 255u) floor_resets++;
  if (tokens) tokens--;
  run_room_forgot();
}

/* B2's mobs (and anything else) break fragile floor through here: protected cells refuse, timed kinds come back */
uint8_t puzzle_frag_hit(uint8_t cell, uint8_t who) BANKED {
  uint8_t k = eg_frag_kind_of(cell_type(cell)), j, ticks = eg_frag_ticks(k);
  if (!eg_frag_hit(&room, cell, who)) return 0u;
  if (ticks)
    for (j = 0; j != 4u; j++)
      if (!frag_t[j][2]) {
        frag_t[j][0]=(uint8_t)(EG_X(cell)+(world_on ? world_view.x : 0u));
        frag_t[j][1]=(uint8_t)(EG_Y(cell)+(world_on ? world_view.y : 0u));
        frag_t[j][2] = ticks;
        break;
      }
  puzzle_break_fx(cell, k);
  puzzle_dirty = 1u;
  return 1u;
}

/* a fragile cell of kind k just broke: b1-look's per-tile break (a wobble, then the fall or the shards, the sound;
 * the cell shows M_PIT at once) */
void puzzle_break_fx(uint8_t cell, uint8_t k) BANKED {
  static const uint8_t look_kind[FK_COUNT] = {LOOK_FRAG_CARPET, LOOK_FRAG_CARPET, LOOK_FRAG_CEIL,
                                              LOOK_FRAG_GLASS,  LOOK_FRAG_GLITCH, LOOK_FRAG_GLASS};
  look_break_tile(cell, k < FK_COUNT ? look_kind[k] : LOOK_FRAG_AUTO);
}

static uint8_t body_on(uint8_t cell) {
  uint8_t occ[4], n = body(occ), k;
  for (k = 0; k != n; k++)
    if (occ[k] == cell) return 1u;
  return 0u;
}

/* every frame of play: the beat, the player's steps on fragile floor, falls, timed resets, the dead-room hint */
void puzzle_tick(void) BANKED {
  uint8_t c = cell_at(pl_x, pl_y), t, on = (uint8_t)eg_beat_on(frame_count), j;
  if(tool_cd)tool_cd--;
  if(!boss_floor && !bench_on && !enemies_alive)bench_near_tick();
  if(pl_state==PS_FALL) return;
  on = beat_phase();
  if(world_on && world_glitch)has_glitch=1u;
  if (on != beat_was && has_glitch) { /* the glitched glass changes phase (solid, flicker, gone) */
    beat_was = on;
    look_glitch_beat(on);
    j = 0;
    do {
      if (cell_type(j) == T_GLITCH) queue_cell(j);
    } while (++j != (uint8_t)EG_N);
  }
  if(c!=pl_cell && cell_type(c)==T_PLATE && !pl_air) tutorial_queue(TE_PLATE);
  if (c != pl_cell) { /* the centre moved on: the old cell counts once the whole body has left it */
    if (c == frag_pend) frag_pend = 255u; /* stepped back: no step */
    else if (EG_FRAGILE(cell_type(pl_cell)) && !frag_skip) frag_pend = pl_cell; /* (a blink, B3, skips it) */
    pl_cell = c;
  }
  if (frag_pend != 255u && !body_on(frag_pend)) {
    j = eg_frag_kind_of(cell_type(frag_pend));
    if (eg_step_off(&room, frag_pend, BB_PLAYER, 1u)) {
      obstacle_noise(frag_pend);
      puzzle_break_fx(frag_pend, j);
      puzzle_dirty = 1u;
    } else
      queue_cell(frag_pend); /* the next crack stage */
    frag_pend = 255u;
  }
  t = cell_type(c);
  if (!pl_air && EG_PASSABLE(t) && !EG_FRAGILE(t) && t != T_GLITCH) pl_safe = c;
  if ((t == T_PIT || (t == T_GLITCH && !eg_beat_on(frame_count))) && !pl_air && pl_state != PS_EXIT) {
    player_fall(pl_safe);
    frag_pend = 255u;
    return;
  }
  if (!(frame_count & 7u))
    for (j = 0; j != 4u; j++)
      if (frag_t[j][2] && !--frag_t[j][2] && (world_read(0,frag_t[j][0],frag_t[j][1])&T_MASK) == T_PIT) {
        uint8_t restored=world_write(frag_t[j][0],frag_t[j][1],T_GLITCH);
        if(restored<EG_N) {look_skin[restored]=0u;queue_cell(restored);}
      }
  if (puzzle_dirty) { /* (re)start the dead-room check; it runs a slice a frame so walking never drops a frame */
    puzzle_dirty = 0;
    if (!dead_t && !boss_floor) { /* B2 bosses: an arena is never a dead room */
      if(world_on)world_dead_begin();else eg_dead_begin(c);
      dead_run = 1u;
    }
  } else if (dead_run && grav_t != 1u && (!world_on ||
             (video_epoch==frame_epoch && (LY_REG<80u || LY_REG>=144u)))) {
    /* Named dependencies add portal and resource probes to each flood pop.
     * Keep that work bounded beside an active camera/animation frame. */
    j = world_on ? world_dead_step(c,1u) : eg_dead_step(&room, pocket, pocket_n,(room.nlink || (room.portal_need&EG_PORTAL_ON)) ? 2u : DEAD_SLICE);
    if (j != EG_DEAD_BUSY) {
      dead_run = 0u;
      if (j && !dead_t) {
        narrator_event(NE_STUCK,0u);
        dead_t = 120u;
        hud_msg("THE ROOM FORGETS");
        sfx(SFX_TELL);
        shake = 4u;
      }
    }
  }
  if (dead_t > 2u && --dead_t == 2u) {
    hud_msg("HOLD SELECT");
    dead_t = 1u; /* shown once; the reset (floor_build) clears it */
  }
}
