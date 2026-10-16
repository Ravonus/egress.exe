/* B1 puzzle rooms on the cartridge: the pocket, the portable bench (SELECT), using a piece on the cell you face, the
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
void items_tick(void) BANKED;
extern volatile uint8_t video_epoch;
extern uint8_t frame_epoch;

/* Bank 1 floor state, after the mover map; title playback uses bank 2 here. */
uint8_t __at(0xDBC0) pocket[POCKET_CAP];
uint8_t pocket_n;
static uint8_t __at(0xDBD8) entry_pocket[POCKET_CAP];
static uint8_t entry_n;
uint8_t puzzle_dirty, frag_skip, floor_resets;
eg_plan_state plan;
static uint8_t pl_cell, pl_safe, beat_was, frag_pend, has_glitch;
uint8_t dead_t; /* the dead-room hint: 0 none, counting down, 1 shown (the harness reads it) */
uint8_t dead_run; /* the sliced dead-room check is under way (the harness reads it) */
#define DEAD_SLICE 4u /* queue pops a frame (~3.5 lines each): a whole check is one or two floods, about a second */
static uint8_t frag_t[4][3]; /* global x/y, ticks of 8 frames left (0: free) */
uint8_t bench_on, bench_cur, last_rx; /* 1 listening, 2 use, 3 combination, 4 paused redraw */
static uint8_t sock[2], told;
static char buf[20];
static uint8_t bench_cam_y;

void pocket_begin(void) BANKED { memset(pocket, 0, sizeof pocket); pocket_n = 0; }
void pocket_enter(void) BANKED { memcpy(entry_pocket, pocket, sizeof pocket); entry_n = pocket_n; }
void pocket_restore(void) BANKED { memcpy(pocket, entry_pocket, sizeof pocket); pocket_n = entry_n; }

void pocket_reset(void) BANKED {
  uint8_t i;
  memset(frag_t, 0, sizeof frag_t);
  pl_cell = pl_safe = room.start;
  frag_pend = 255u;
  puzzle_dirty = frag_skip = dead_t = dead_run = 0;
  beat_was = 255u;
  look_glitch_period = 0; /* the puzzle drives the beat (frame-based, EG_BEAT_LEN; the solver's model is a window) */
  bench_on = 0;
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
}

/* the player walked into a piece lying in the room */
void puzzle_pick(uint8_t cell) BANKED {
  tutorial_queue(TE_BENCH);
  uint8_t p = eg_item_at(&room, cell);
  if (!p) return;
  if (pocket_n >= POCKET_CAP) { hud_msg("SATCHEL FULL: USE IT"); return; }
  pocket[pocket_n++] = p;
  room.c[cell] = T_EMPTY;
  queue_cell(cell);
  sfx(SFX_KEY);
  puff(pl_x, (uint8_t)(pl_y - 10u), 4, ST_SPARK);
  if (acts.picked != 255u) acts.picked++;
  if (!told) {
    told = 1;
    hud_msg("SELECT: BENCH");
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
  }
}

/* use pocket[slot] on the cell the player faces; 1 if the room reacted */
static uint8_t use(uint8_t slot) {
  uint8_t occ[4], n = body(occ), here = cell_at(pl_x, pl_y), cell, rx, k, nd, pal;
  uint8_t order[EG_DIRTY_MAX];
  cell = (uint8_t)(here + (uint8_t)(fdy[pl_face] * (int8_t)EG_W) + (uint8_t)fdx[pl_face]);
  if (slot >= pocket_n) return 0;
  if (enemy_in_cell(cell)) {
    /* Consumable tools enter the existing combat matrix, including wet -> shock/freeze. */
    for (k = 0u; k != nen; k++) {
      if (en[k].st == ES_DEAD || cell_at((uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4)) != cell) continue;
      if (en[k].kind != SPAWN_MOB) { hud_msg("ARMOURED: ITEM KEPT"); return 0u; }
      if (!mob_tool(k,pocket[slot])) { hud_msg("BLOCKED: ITEM KEPT"); return 0; }
      narrator_event(NE_USE,(uint8_t)(pocket[slot]|(pocket_n==1u ? 128u : 0u)));
      pocket_take(slot);
      hud_msg("TOOL USED ON MOB");
      sfx(SFX_CHARGED);
      return 1u;
    }
    return 0u;
  }
  rx = eg_apply(&room, pocket[slot], cell, occ, n);
  if (!rx) {
    narrator_event(NE_REFUSED,0u);
    sfx(SFX_DENY);
    hud_msg("NO EFFECT: KEPT");
    return 0;
  }
  last_rx = rx;
  narrator_event(NE_USE,(uint8_t)(pocket[slot]|(pocket_n==1u ? 128u : 0u)));
  pocket_take(slot);
  nd = eg_ndirty;
  if (nd <= EG_DIRTY_MAX) memcpy(order, eg_dirty, nd);
  eg_rx_name(rx, buf);
  hud_msg(buf);
  sfx(rx == RX_BURN ? SFX_BREAK : (rx == RX_SHORT ? SFX_DOOR : (rx == RX_WEIGHT ? SFX_THUD : SFX_CHARGED)));
  /* the spread, a cell at a time, nearest first */
  pal = rx == RX_BURN ? 7u : (rx == RX_SHORT ? 4u : (rx == RX_WEIGHT ? 4u : 4u));
  if (nd > EG_DIRTY_MAX) {
    for (k = 0; k != (uint8_t)EG_N; k++) draw_cell(k);
  } else
    for (k = 0; k != nd; k++) {
      cell = order[k];
      draw_cell(cell);
      puff((uint8_t)(EG_X(cell) * 16u + 8u), (uint8_t)(EG_Y(cell) * 16u + 8u), pal,
           rx == RX_BURN || rx == RX_SHORT ? ST_SPARK : ST_PUFF);
      draw_sprites();
      wait_frames(rx == RX_WEIGHT ? 2u : 5u);
    }
  if (rx == RX_SHORT) shake = 6u;
  puzzle_dirty = 1u; /* a burnt plank leaves a pit: look for a dead room */
  grav_t = 1u; /* a burnt plank drops what stood on it; a weighted plate lifts the shutters */
  return 1;
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
static void bench_icon(uint8_t y,uint8_t p) {
  uint8_t t=(uint8_t)(LOOK_ICON_BASE+p-1u),a=(uint8_t)(meta_pal[M_ITEM0+p-1u]&7u);
  VBK_REG=1u;set_win_tiles(1u,y,1u,1u,&a);
  VBK_REG=0u;set_win_tiles(1u,y,1u,1u,&t);
}
static void bench_scene(uint8_t phase,uint8_t result) {
  uint8_t y;
  for(y=1u;y!=6u;y++) text_at(1,10u,y,"          ",BP);
  bench_card(14u,3u,M_BENCH);
  if(result) {
    bench_card(14u,1u,(uint8_t)(M_ITEM0+result-1u));
    text_at(1,11u,2u,"+",HP);text_at(1,18u,2u,"+",HP);
  } else {
    if(sock[0]!=255u) bench_card((uint8_t)(11u+phase),1u,(uint8_t)(M_ITEM0+pocket[sock[0]]-1u));
    if(sock[1]!=255u) bench_card((uint8_t)(17u-phase),1u,(uint8_t)(M_ITEM0+pocket[sock[1]]-1u));
    if(!phase) text_at(1,15u,1u,"+",HP);
  }
}
static void bench_combine(uint8_t result) {
  uint8_t phase,t;
  text_at(1,10u,0u,"COMBINING ",HP);
  for(phase=0u;phase!=3u;phase++) {
    bench_scene(phase,0u);sfx(phase==2u ? SFX_CHARGED : SFX_PUSH);
    for(t=0u;t!=8u;t++) {frame_wait();sound_tick();draw_sprites();}
  }
  bench_scene(0u,result);sfx(SFX_KEY);
  for(t=0u;t!=18u;t++) {frame_wait();sound_tick();draw_sprites();}
}

static const char *const target_names[T_TYPES] = {
  "FLOOR","FOAM","CONCRETE","CRACK WALL","CRT","TOKEN","KEYCARD","DOOR",
  "CARPET","GAP","PLATE","GATE","OPEN GATE","WATER","GLITCH","PAPER",
  "PLANK","ITEM","SOCKET","POWERED","DEAD GATE","CRATE","LOADED",
  "CARPET","WORN","TORN","CEILING","CRACKED","GLASS","ICE","THIN ICE","PARTITION"
};

static void bench_draw(void) {
  uint8_t k, j, row, count, rx, cell, r = 0, ordinal=0u, page;
  if(pocket_n) bench_cur=first_slot(bench_cur);
  for(k=0u;k<bench_cur;k++) if(first_slot(k)==k) ordinal++;
  page=(uint8_t)((ordinal/6u)*6u);
  text_at(1, 0, 0, "BAG       BENCH      ", BP);
  num_at(1, 4, 0, pocket_n, 2u, BP);
  text_at(1, 6, 0, "/24", BP);
  k=0u; ordinal=0u;
  for (row = 1u; row != 7u; row++) {
    while(k<pocket_n && (first_slot(k)!=k || ordinal<page)) {
      if(first_slot(k)==k) ordinal++;
      k++;
    }
    text_at(1, 0, row, "          ", BP);
    if (k >= pocket_n) {
      if (!pocket_n && row == 1u) text_at(1, 1, row, "EMPTY", BP);
      continue;
    }
    count = 0u;
    for (j = 0u; j != pocket_n; j++) if (pocket[j] == pocket[k]) count++;
    eg_piece_abbr(pocket[k], buf);
    bench_icon(row,pocket[k]);
    text_at(1, 2, row, buf, (uint8_t)(sock[0] == k || sock[1] == k ? HP : BP));
    text_at(1, 6, row, "x", BP);
    num_at(1, 7, row, count, 2u, BP);
    if (k == bench_cur) text_at(1, 0, row, ">", HP);
    k++;
  }
  bench_scene(0u,0u);
  if (sock[1] != 255u) {
    r = eg_recipe(pocket[sock[0]], pocket[sock[1]]);
    if (r) {
      eg_piece_name(r, buf);
      text_at(1, 11, 5, buf, HP);
    } else
      text_at(1, 11, 5, "NOTHING", BP);
  }
  text_at(1, 10, 6, "          ", BP);
  cell = (uint8_t)(cell_at(pl_x,pl_y) + (uint8_t)(fdy[pl_face] * (int8_t)EG_W) + (uint8_t)fdx[pl_face]);
  rx = EG_ABYSS(room.c[cell]) ? RX_NONE : eg_use_rx(pocket_n ? pocket[bench_cur] : 0u, cell_type(cell));
  if (enemy_in_cell(cell)) { text_at(1,10,6,"MOB: TOOL",HP); }
  else {
    if (rx) { eg_rx_name(rx, buf); buf[10] = 0; text_at(1, 10, 6, buf, HP); }
    else text_at(1,10,6,EG_ABYSS(room.c[cell]) ? "OPEN VOID" : target_names[cell_type(cell)],BP);
  }
  eg_piece_name(r ? r : (pocket_n ? pocket[bench_cur] : 0u), buf);
  line(7, buf);
  if (pocket_n) text_at(1,10,7,"START USE",HP);
  line(8, sock[1] != 255u ? "A MAKE B UNDO > DROP" : "A ADD B BACK > DROP");
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
  bench_on = 0u;
}

void bench_run(void) BANKED {
  uint8_t a, b, r, drop_t=0u;
  bench_on=4u; /* opening: gameplay is paused while the existing scene draws */
  sock[0] = sock[1] = 255u;
  if (bench_cur >= pocket_n) bench_cur = 0;
  bench_cam_y=cam_y;
  cam_y=pl_y>40u ? (uint8_t)(pl_y-40u) : 0u;
  if(cam_y>152u) cam_y=152u;
  move_bkg(BG_SCROLL_X(cam_x),BG_SCROLL_Y(cam_y));
  clear_map(1, BP);
  bench_draw();
  hud_line = BENCH_Y;
  move_win(7, BENCH_Y);
  sfx(SFX_STEP);
  bench_on = 1; /* up and listening (the harness waits for this) */
  heir_idle_stop(); heir_play(HA_BENCH,1u);
  while (1) {
    frame_wait(); /* no hud_tick here: a toast closing would move the window under the bench */
    sound_tick();
    items_tick();
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
    if (joy_pressed & J_SELECT) break;
    if (joy_pressed & (J_UP | J_DOWN)) {
      if (pocket_n) {
        bench_step((uint8_t)(joy_pressed & J_UP));
        sfx(SFX_STEP);
        bench_draw();
      }
    } else if (joy_pressed & J_A) {
      if (sock[1] != 255u) { /* make it */
        a = sock[0];
        b = sock[1];
        r = eg_recipe(pocket[a], pocket[b]);
        if (!r) {
          narrator_event(NE_REFUSED,0u);
          sfx(SFX_DENY);
          text_at(1,10,6,"ITEMS KEPT",BP);
          continue;
        }
        bench_on=3u;bench_combine(r);
        pocket_take(a > b ? a : b);
        pocket_take(a > b ? b : a);
        pocket[pocket_n++] = r;
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
    } else if ((joy_pressed & J_START) && pocket_n) {
      bench_close();
      bench_on = 2; /* the reaction playing out */
      use(bench_cur);
      frame_wait();
      bench_on = 0;
      if (bench_cur >= pocket_n) bench_cur = 0;
      return;
    }
  }
  bench_close();
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
