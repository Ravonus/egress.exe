/* B2 stage types on the cartridge: the slot and the deck (core/eg_stage.c deals them), the stage room
 * (eg_stage_gen), and each type's play, written against the thin mob adapter (src/stage_spawn.h).
 *
 *   holdout   survive anywhere and defeat finite enemy waves. Each clear starts
 *             a short breather; later waves replenish larger, tougher groups.
 *   chase     the door is open; after the grace the floor drops a row at a time from the bottom (the look's drop,
 *             four cells a frame). Caught: two half hearts and up onto the floor above; no floor above: dead.
 *   hunt      the marked target (sparks over it) dies only by its condition: a falling CRT, its own orb returned,
 *             or a hit from behind just after a roll. Its hit points hold at 250 until the condition is in play,
 *             then drop to 1, so a wrong kind of hit never kills it. Two conditions: it re-forms once.
 *   escort    the temp (a BG card, no OAM) walks its path a cell at a time while you are near; a mob or an orb on
 *             its cell hurts it. At the door it leaves and the door opens; gone: the room is lost (hold SELECT).
 *   mixp      the bench works only in the ring around it; a wave every few seconds until the recipe is in the
 *             pocket. Made: the door opens.
 *   dark      unlit cells show the void; light reaches 1 cell around you (2 with a GLOWS piece, the tealight, in the
 *             pocket) and 1 around each lamp; the door is lit. Keycards open the door as usual.
 *   heist     the shopkeeper behind the counter; SELECT at the desk buys the goods. Walk in and take them: the door
 *             seals and waves come (a holdout without the ring); the last one down, the door opens again.
 * Per-frame work is sliced: at most one spawn probe batch, four dropped cells, one neighbourhood relight a frame. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "puzzle.h"
#include "run.h"
#include "look.h"
#include "stage.h"
#include "stage_spawn.h"
#include "mob.h"
#include "core/eg_piece.h"
#include "gen/stage_art.h"
#include "core/eg_physics.h"
#include "narrator.h"
#include "world.h"

extern uint8_t dead_t; /* src/puzzle.c: 120 shows the lost room's hint, then HOLD SELECT */
extern uint8_t dbg_world_rule,dbg_world_size,dbg_world_biome;

uint8_t stage_type, stage_kind, dbg_stage;
eg_stage stg;
eg_slice slice;
char stage_goal[19], stage_hud[7];
uint8_t stage_won, stage_lost, stage_wave, stage_pct, stage_row, stage_cell, stage_thp;

static uint16_t st_seed; /* the stage room's seed (a reset rebuilds the same room) */
static uint16_t st_t;    /* frames of play on this floor */
static uint16_t st_next; /* chase: the next row's drop; timed waves: the next */
static uint16_t st_acc;
static uint8_t st_cond, st_roll, st_near, st_inv, st_x, st_pc, st_paid, st_theft, st_told, st_fam, st_pend;
static uint8_t wv_pend, wv_shoot;
static char sbuf[20];

#define CARD_M(c) ((uint8_t)(M_REACT0 + 8u + (c))) /* metas 44..46: the reaction range's spare tail */

static uint8_t cheb(uint8_t a, uint8_t b) {
  uint8_t dx = absdiff(EG_X(a), EG_X(b)), dy = absdiff(EG_Y(a), EG_Y(b));
  return dx > dy ? dx : dy;
}
static uint8_t pcell(void) { return cell_at(pl_x, pl_y); }
static uint8_t px_of(uint8_t c) { return (uint8_t)(EG_X(c) * 16u + 8u); }
static uint8_t py_of(uint8_t c) { return (uint8_t)(EG_Y(c) * 16u + 8u); }

/* 6 characters into the HUD, padded */
static void hud_set(const char *s) {
  uint8_t n = 0;
  while (n != 6u && s[n]) {
    stage_hud[n] = s[n];
    n++;
  }
  while (n != 6u) stage_hud[n++] = ' ';
  stage_hud[6] = 0;
  hud_dirty = 1u;
}
static void hud_numset(const char *pre, uint8_t v, const char *post) {
  char b[8];
  uint8_t n = 0;
  while (*pre) b[n++] = *pre++;
  if (v >= 10u) b[n++] = (char)('0' + v / 10u);
  b[n++] = (char)('0' + v % 10u);
  while (*post && n != 6u) b[n++] = *post++;
  b[n] = 0;
  hud_set(b);
}

/* ---------------- the run's slots ---------------- */
void stage_run_begin(void) BANKED {
  eg_slice_begin(&slice);
  stage_type = SG_PUZZLE;
  stage_kind = SK_STAGE;
}

void stage_plan_floor(void) BANKED {
  eg_seed_rec e;
  uint8_t force = dbg_stage ? (uint8_t)(dbg_stage - 1u) : 255u;
  floor_resets = 0;
  st_seed = (uint16_t)(run_seed ^ ((uint16_t)floor_no * 0x9e37u) ^ 0x51a7u);
  if (!st_seed) st_seed = 1u;
  /* Explicit authored mechanic fixtures for isolated interaction regressions.
   * Ordinary runs never set this debug flag and use the spatial codec. */
  if(dbg_stage>=129u && dbg_stage<=132u) {
    slice.kind=stage_kind=SK_STAGE;slice.type=stage_type=SG_PUZZLE;
    slice.planned=0u;slice.physical=dbg_stage;slice.m=255u;
    slice.level=eg_plan_target(&plan,floor_no);dbg_stage=0u;
    return;
  }
  if (dbg_fseed) { /* the harness: this exact room (a puzzle, unless a type is forced too) */
    floor_seed = dbg_fseed;
    floor_tier = dbg_ftier;
    dbg_fseed = 0;
    slice.planned = 0;
    slice.physical = PH_NONE;
    slice.kind = SK_STAGE;
    slice.level = (uint8_t)(dbg_ftier * 3u > 15u ? 15u : dbg_ftier * 3u);
    slice.m = 255u;
    stage_kind = SK_STAGE;
    stage_type = force != 255u ? force : SG_PUZZLE;
    return;
  }
  stage_kind = eg_slice_floor(&slice, &plan, floor_no, RUN_FLOORS, &e, force);
  stage_type = slice.type;
  if(force==255u)slice.physical|=EG_SLICE_HAZARDS;
  if (slice.planned) {
    floor_seed = e.seed;
    floor_tier = e.tier;
  }
}

void stage_resume_floor(void) BANKED {
  st_seed=(uint16_t)(run_seed^((uint16_t)floor_no*0x9e37u)^0x51a7u);
  if(!st_seed) st_seed=1u;
  stage_kind=slice.kind;stage_type=slice.type;
}

static void world_encounter(void) {
  uint8_t k,c,n,tries,x,y,sx=EG_X(room.start),sy=EG_Y(room.start),d;
  eg_stage_flood(&room,room.start);
  n=(uint8_t)(floor_no>=3u ? 2u : 1u+(st_seed&1u));
  for(k=0u;k<n;k++)for(tries=0u;tries<128u;tries++) {
    c=(uint8_t)(st_seed+k*67u+tries*37u);if(c>=EG_N)continue;
    x=EG_X(c);y=EG_Y(c);d=absdiff(x,sx)+absdiff(y,sy);
    if(room.c[c]!=T_EMPTY || !EG_REACHED(c) || d<4u || d>7u || absdiff(x,sx)>5u || absdiff(y,sy)>5u)continue;
    if(k && c==room.spawn[0])continue;
    room.spawn[room.nspawn]=c;room.spawn_kind[room.nspawn]=(uint8_t)(k && floor_no>=3u ? SPAWN_EYE : SPAWN_MITE);room.nspawn++;
    break;
  }
}
uint8_t stage_build(void) BANKED {
  uint8_t rule;
  if(slice.physical&128u) {eg_physics_gen(&room,slice.physical&127u,st_seed,slice.level);return 1u;}
  if(dbg_world_rule && dbg_world_rule<=EL_RULES && dbg_world_size<=EL_SIZES && dbg_world_biome<EL_BIOMES) {
    stage_type=SG_PUZZLE;
    return world_begin(st_seed,dbg_world_rule-1u,dbg_world_size ? dbg_world_size-1u : 255u,dbg_world_biome);
  }
  if (slice.physical&31u) {
    /* Falling-load chutes remain in the ordinary deal alongside the spatial
     * cold/power routes, rather than disappearing into test-only fixtures. */
    if(floor_no==3u && !(st_seed&3u)) {eg_physics_gen(&room,PH_DROP,st_seed,slice.level);return 1u;}
    /* The floor keeps its mechanical role; its dimensions, topology, goal
     * conditions and apparatus are constructed anew from this run's seed. */
    rule=floor_no==1u ? 0u : floor_no==3u ? (uint8_t)(2u+(st_seed&1u)) : (uint8_t)(4u+(st_seed&1u));
    if(!world_begin(st_seed,(uint8_t)(rule|((slice.physical&32u) ? EL_HAZARDS : 0u)),255u,BIOME_OFFICE))return 0u;
    world_encounter();return 1u;
  }
  if (stage_type == SG_PUZZLE || stage_type >= EG_DECK_TYPES) return 0u;
  if (!eg_stage_gen(&room,&stg,stage_type,st_seed,slice.level,slice.m)) {
    stage_type = SG_PUZZLE; /* never seen in the host sim; the puzzle room stands in */
    return 0u;
  }
  return 1u;
}

/* ---------------- waves: glitched in from the void, a probe batch a frame ---------------- */
static uint8_t entry_offscreen(uint8_t cell) {
  int16_t x=(int16_t)((uint16_t)EG_X(cell)*16u+8u)-(int16_t)cam_x;
  int16_t y=(int16_t)((uint16_t)EG_Y(cell)*16u+8u)-(int16_t)cam_y;
  /* Include the complete body, entry particles and the next camera easing
   * step. Recheck the current camera for every probe, including pooled slots. */
  return (uint8_t)(x<=-24 || x>=184 || y<=-24 || y>=152);
}
static void wave_begin(uint8_t n, uint8_t shooter) {
  if(n && !dbg_no_enemies) narrator_event(NE_WAVE,0u);
  wv_pend = dbg_no_enemies ? 0u : n;
  wv_shoot = shooter;
  stage_wave++;
  sfx(SFX_TELL);
}
static void wave_step(void) {
  uint8_t k, c, fam,slot,rank;
  if (!wv_pend || enemies_alive >= 3u) return;
  if(pl_state==PS_FALL || pl_state==PS_EXIT)return;
  /* A reinforcement is optional work, unlike the current attack and live AI.
   * Do not stack its setup on a hit/charge frame that has spent the budget. */
  if(freeze || (frame_count&3u) || (LY_REG>=24u && LY_REG<144u))return;
  for (k = 0; k != 2u; k++) {
    c = EG_IDX(1u + eg_rndn(14u), 1u + eg_rndn(12u));
    /* Cosmetic visibility is not material: actors can approach over genuine
     * offscreen floor before its camera-owned cards are drawn. */
    if (!entry_offscreen(c) || (stage_type == SG_DARK && look_skin[c]) || !ss_spawn_ok(c)) continue;
    fam=wv_shoot ? SS_SHOOTER : SS_CHASER;
    if(stage_type==SG_HOLDOUT) {
      /* Three preloaded silhouettes; the third role opens after the first
       * wave. Conditional CRT/reflect-only foes stay in their authored hunts. */
      if(stg.level>=3u && stage_wave>1u && (wv_pend&1u))
        fam=stg.level>=8u && (st_seed&1u) ? FAM_MEMO : FAM_STAPLES;
      if((stage_wave>1u || stg.level>=5u) && !(wv_pend%3u))fam=SS_SHOOTER;
    }
    slot=ss_spawn(fam,c,0u,frame_count);
    if(slot==255u)return;
    if(stage_type==SG_HOLDOUT) {
      rank=(uint8_t)(stg.level/5u+((stage_wave-1u)>>1));
      en[slot].hp+=rank;mob[slot].hp_max=en[slot].hp;
      mob[slot].speed+=(uint8_t)(stg.level/3u+stage_wave-1u);
    }
    puff(px_of(c), py_of(c), 4u, ST_PUFF);
    puff(px_of(c), (uint8_t)(py_of(c) - 10u), 4u, ST_SPARK);
    wv_shoot = 0;
    wv_pend--;
    return;
  }
}

/* a spark on the ring's edge, going round */
static void ring_spark(uint8_t fast) {
  uint8_t r = stg.ring_r, k, side, off;
  int8_t dx, dy;
  if (frame_count & (fast ? 3u : 15u)) return;
  if(r==1u) {k=(uint8_t)((frame_count>>2)&7u);side=k>>1;off=k&1u;}
  else if(r==2u) {k=(uint8_t)((frame_count>>2)&15u);side=k>>2;off=k&3u;}
  else {
    if(!r)return;
    k = (uint8_t)((frame_count >> 2) % (uint8_t)(8u * r));
    side = (uint8_t)(k / (uint8_t)(2u * r));
    off = (uint8_t)(k % (uint8_t)(2u * r));
  }
  dx = side == 0u ? (int8_t)(off - r) : (side == 1u ? (int8_t)r : (side == 2u ? (int8_t)(r - off) : -(int8_t)r));
  dy = side == 0u ? -(int8_t)r : (side == 1u ? (int8_t)(off - r) : (side == 2u ? (int8_t)r : (int8_t)(r - off)));
  puff((uint8_t)(px_of(stg.ring) + dx * 16), (uint8_t)(py_of(stg.ring) + dy * 16), 4u, ST_SPARK);
}
static uint8_t in_ring(void) { return (uint8_t)(cheb(pcell(), stg.ring) <= stg.ring_r); }
static void win(const char *msg) {
  stage_won = 1u;
  wv_pend = 0;
  if (!door_open) open_door();
  hud_msg(msg);
}

/* ---------------- hunt ---------------- */
static const uint8_t kc_fam[KC_COUNT] = {SS_CRUSH, SS_ORB, SS_BACK};
static uint8_t hunt_spawn(uint8_t arrival) {
  uint8_t c = stg.tcell, k, p = pcell();
  if(arrival && (freeze || (LY_REG>=24u && LY_REG<144u)))return 0u;
  for (k = 0; k != (arrival ? 2u : 24u); k++) {
    if((!arrival || entry_offscreen(c)) && ss_spawn_ok(c))break;
    c = EG_IDX(1u + eg_rndn(14u), 1u + eg_rndn(12u));
    if (cheb(c, p) < 5u) c = 0u;
  }
  if (k == (arrival ? 2u : 24u)) return 0u;
  stage_cell = ss_spawn(st_fam, c, (uint8_t)(stage_kind == SK_MIDBOSS), frame_count);
  if (stage_cell == 255u) return 0u;
  if (stg.cond[st_cond] == KC_BACK) mob[stage_cell].flags |= MOBF_HUNT_BACK;
  puff(px_of(c), py_of(c), 4u, ST_PUFF);
  return 1u;
}
static void hunt_tell(void) {
  eg_stage_text((uint8_t)(EG_DECK_TYPES + stg.cond[st_cond]), sbuf);
  hud_msg(sbuf);
  hud_numset("MARK", (uint8_t)(st_cond + 1u), stg.conds == 2u ? "/2" : "/1");
}
static void hunt_tick(void) {
  enemy_t *e;
  uint8_t k, ex, ey, ok, near = 0;
  if (st_pend) { /* re-forming */
    if (--st_pend == 1u && !hunt_spawn(1u)) st_pend = 2u;
    return;
  }
  e = &en[stage_cell];
  ex = (uint8_t)(e->x >> 4);
  ey = (uint8_t)(e->y >> 4);
  if (e->st == ES_DEAD) {
    /* how it went: crushed keeps its hit points; an orb or the lash took the last one */
    ok = 1u; /* the mob runtime admitted the exact killing source */
    if (!ok) { /* the wrong kind of hit: it re-forms */
      st_pend = 40u;
      hud_msg("IT RE-FORMS");
      return;
    }
    if (++st_cond >= stg.conds) {
      win("THE MARK IS DOWN");
      return;
    }
    st_fam = kc_fam[stg.cond[st_cond]];
    st_pend = 60u;
    hud_msg("IT COMES BACK");
    st_told = 0;
    return;
  }
  /* the marker */
  if (!(frame_count & 15u)) puff(ex, (uint8_t)(ey - 12u), stage_kind == SK_MIDBOSS ? 5u : 4u, ST_SPARK);
  if (!st_told && st_t >= 150u) {
    st_told = 1u;
    hunt_tell();
  }
  if (pl_state == PS_ROLL) st_roll = 45u;
  else if (st_roll) st_roll--;
  /* its own orb, returned: a friendly orb close */
  for (k = 0; k != MAXO; k++)
    if (orbs[k].live && orbs[k].friendly && absdiff((uint8_t)(orbs[k].x >> 4), ex) < 24u &&
        absdiff((uint8_t)(orbs[k].y >> 4), ey) < 24u)
      near = 1u;
  st_near = near;

}

/* ---------------- chase ---------------- */
static void chase_catch(void) {
  uint8_t y, k, x = EG_X(pcell()), c;
  int8_t d;
  for (y = (uint8_t)(stage_row - 1u); y; y--)
    for (k = 0; k != 15u; k++) {
      d = (int8_t)((k & 1u) ? (k + 1u) >> 1 : -(int8_t)(k >> 1));
      if ((int8_t)x + d < 1 || (int8_t)x + d > 14) continue;
      c = EG_IDX((uint8_t)((int8_t)x + d), y);
      if (cell_type(c) != T_EMPTY || enemy_in_cell(c)) continue;
      player_hurt(2u, pl_x, (uint8_t)(pl_y + 16u));
      psx = cell_sub(EG_X(c));
      psy = cell_sub(y);
      pl_x = (uint8_t)(psx >> 4);
      pl_y = (uint8_t)(psy >> 4);
      hud_msg("THE FLOOR TOOK YOU");
      return;
    }
  if (!dbg_god) pl_hp = 0; /* no floor left above */
}
static void chase_tick(void) {
  uint8_t n, i, k;
  if (stage_row == 255u) {
    if (st_t < st_next) return;
    stage_row = (uint8_t)(EG_H - 2u);
    st_x = 1u;
    sfx(SFX_TEAR);
    shake = 6u;
  }
  if (!stage_row) return;
  if (st_x) { /* the row going now: four cells a frame */
    for (n = 0; n != 4u && st_x != EG_W - 1u; n++, st_x++) {
      i = EG_IDX(st_x, stage_row);
      if (look_here(i)) look_drop_cell(i);
      room.c[i] = T_WALL; /* the void (a wall to every body; drawn as the void once out of the world) */
    }
    if (st_x == EG_W - 1u) {
      st_x = 0;
      st_next = (uint16_t)(st_next + stg.speed);
      for (k = 0; k != nen; k++) /* whatever stood there goes with it */
        if (en[k].st != ES_DEAD && (uint8_t)(en[k].y >> 8) >= stage_row) {
          en[k].st = ES_DEAD;
          puff((uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4), 3u, ST_PUFF);
          if (enemies_alive) enemies_alive--;
        }
    }
  } else if (st_t >= st_next && stage_row > 1u) {
    stage_row--;
    st_x = 1u;
  }
  if (pl_state != PS_EXIT && EG_Y(cell_at(pl_x, (uint8_t)(pl_y + 4u))) >= stage_row &&
      (st_x == 0u || st_x > EG_X(pcell())))
    chase_catch();
}

/* ---------------- escort ---------------- */
static void temp_show(uint8_t c, uint8_t card) { look_skin_set(c, card == 255u ? LOOK_SKIN_NONE : CARD_M(card)); }
static void temp_hurt(void) {
  if (st_inv || stage_lost || stage_won) return;
  st_inv = 60u;
  sfx(SFX_HURT);
  puff(px_of(stage_cell), (uint8_t)(py_of(stage_cell) - 8u), 3u, ST_SPARK);
  if (stage_thp) stage_thp--;
  hud_numset("TEMP", stage_thp, "");
  if (!stage_thp) {
    stage_lost = 1u;
    temp_show(stage_cell, 255u);
    puff(px_of(stage_cell), py_of(stage_cell), 3u, ST_PUFF);
    hud_msg("THE TEMP IS GONE");
    dead_t = 120u; /* the lost room: HOLD SELECT */
  }
}
static void escort_tick(void) {
  uint8_t k, c = stage_cell, to, nx;
  if (stage_lost || stage_won) return;
  if (st_inv) st_inv--;
  for (k = 0; k != nen; k++)
    if (en[k].st != ES_DEAD && cell_at((uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4)) == c) temp_hurt();
  for (k = 0; k != MAXO; k++)
    if (orbs[k].live && !orbs[k].friendly && overlap((uint8_t)(orbs[k].x >> 4), (uint8_t)(orbs[k].y >> 4), px_of(c),
                                                     py_of(c), 7u, 7u)) {
      orbs[k].live = 0;
      temp_hurt();
    }
  if (stage_wave < stg.waves && st_t >= st_next) {
    wave_begin(stg.per_wave, (uint8_t)(stage_wave >= stg.shooters));
    st_next = (uint16_t)(st_next + 600u);
  }
  /* a step every `speed` frames while you are near and the way is open */
  if (++st_acc < stg.speed || cheb(c, pcell()) > 4u) return;
  st_acc = 0;
  to = stg.path[st_x];
  if (EG_X(c) != EG_X(to)) nx = EG_X(c) < EG_X(to) ? (uint8_t)(c + 1u) : (uint8_t)(c - 1u);
  else nx = EG_Y(c) < EG_Y(to) ? (uint8_t)(c + EG_W) : (uint8_t)(c - EG_W);
  if (!EG_PASSABLE(cell_type(nx)) || enemy_in_cell(nx) || nx == pcell()) return;
  temp_show(c, 255u);
  stage_cell = nx;
  temp_show(nx, (uint8_t)(STAGE_CARD_BACK_A + (nx & 1u)));
  if (nx == to && ++st_x == stg.npath) {
    temp_show(nx, 255u);
    puff(px_of(nx), py_of(nx), 4u, ST_SPARK);
    win("THE TEMP IS OUT");
  }
}

/* ---------------- mix under pressure ---------------- */
static void mixp_tick(void) {
  uint8_t k;
  for (k = 0; k != pocket_n; k++)
    if (pocket[k] == stg.piece) {
      win("MADE. GET OUT");
      return;
    }
  ring_spark(in_ring());
  if (st_t >= st_next) {
    wave_begin(stg.per_wave, (uint8_t)(stg.shooters != 255u && (stage_wave & 1u)));
    st_next = (uint16_t)(st_next + (uint16_t)stg.speed * 4u);
  }
}

/* ---------------- dark ---------------- */
static uint8_t dk_r;
static uint8_t lit(uint8_t i) {
  uint8_t k;
  if (i == room.door || i == (uint8_t)(room.door + EG_W)) return 1u;
  if (cheb(i, st_pc) <= dk_r) return 1u;
  for (k = 0; k != stg.npath; k++)
    if (cheb(i, stg.path[k]) <= 1u) return 1u;
  return 0u;
}
static uint8_t is_lamp(uint8_t i) {
  uint8_t k;
  for (k = 0; k != stg.npath; k++)
    if (stg.path[k] == i) return 1u;
  return 0u;
}
static uint8_t dk_want(uint8_t i) {
  if (!lit(i)) return M_VOID_A;
  return is_lamp(i) ? M_BURNER : LOOK_SKIN_NONE;
}
static void dk_apply(uint8_t i) {
  uint8_t want, cur;
  if (look_void_cell(i)) return;
  want = dk_want(i);
  cur = look_skin[i] ? (uint8_t)(look_skin[i] - 1u) : LOOK_SKIN_NONE;
  if (want != cur) look_skin_set(i, want);
}
static void dark_radius(void) {
  uint8_t k;
  dk_r = 1u;
  for (k = 0; k != pocket_n; k++)
    if (pocket[k] == P_FIRE) dk_r = 2u; /* GLOWS: the tealight lights a wider ring */
}
static void dark_tick(void) {
  uint8_t c = pcell(), x, y, r, x0, y0, x1, y1, n;
  dark_radius();
  if (c == st_pc && dk_r == st_x) { /* still: a slow sweep keeps every cell honest */
    for (n = 0; n != 8u; n++) {
      dk_apply(st_paid);
      if (++st_paid == (uint8_t)EG_N) st_paid = 0;
    }
    return;
  }
  /* moved: relight the old and new neighbourhoods (radius + 1 around the new cell covers the old one's edge) */
  st_pc = c;
  st_x = dk_r;
  r = (uint8_t)(dk_r + 1u);
  x0 = EG_X(c) > r ? (uint8_t)(EG_X(c) - r) : 0u;
  y0 = EG_Y(c) > r ? (uint8_t)(EG_Y(c) - r) : 0u;
  x1 = EG_X(c) + r > EG_W - 1u ? EG_W - 1u : (uint8_t)(EG_X(c) + r);
  y1 = EG_Y(c) + r > EG_H - 1u ? EG_H - 1u : (uint8_t)(EG_Y(c) + r);
  for (y = y0; y <= y1; y++)
    for (x = x0; x <= x1; x++) dk_apply(EG_IDX(x, y));
}

/* ---------------- heist ---------------- */
static void heist_tick(void) {
  uint8_t desk = (uint8_t)(stg.spot + EG_W), k;
  if (!st_paid && !st_theft && cell_type(stg.spot) != T_ITEM) { /* taken */
    st_theft = 1u;
    door_open = 0;
    queue_cell(room.door);
    shake = 8u;
    hud_msg("THIEF. IT SEALS");
    temp_show(stg.tcell, 255u);
    st_next = (uint16_t)(st_t + 60u);
  }
  if (st_theft && !stage_won) {
    if (stage_wave < stg.waves && st_t >= st_next && !wv_pend) {
      wave_begin(stg.per_wave, (uint8_t)(stage_wave >= stg.shooters));
      st_next = (uint16_t)(st_t + 360u);
      hud_numset("WAVE", stage_wave, "");
    }
    if (stage_wave >= stg.waves && !wv_pend && !ss_alive()) win("THE SHOP LETS GO");
    return;
  }
  if (!st_paid && pcell() == desk && !st_told) {
    st_told = 1u;
    strcpy(sbuf, "SELECT BUYS: ");
    k = 13u;
    if (stg.price >= 10u) sbuf[k++] = (char)('0' + stg.price / 10u);
    sbuf[k++] = (char)('0' + stg.price % 10u);
    sbuf[k] = 0;
    hud_msg(sbuf);
  } else if (pcell() != desk)
    st_told = 0;
}

/* ---------------- the hooks ---------------- */
void stage_start(void) BANKED {
  uint8_t k;
  stage_won = stage_lost = stage_wave = stage_pct = 0;
  stage_row = 255u;
  stage_cell = 255u;
  stage_thp = 0;
  st_t = st_acc = 0;
  st_next = 0;
  st_cond = st_roll = st_near = st_inv = st_x = st_paid = st_theft = st_told = st_pend = 0;
  st_pc = 255u;
  wv_pend = wv_shoot = 0;
  stage_hud[0] = 0;
  stage_goal[0] = 0;
  if (boss_floor) stage_type = SG_PUZZLE;
  if (stage_type == SG_PUZZLE) return;
  eg_stage_text(stage_type, stage_goal);
  switch (stage_type) {
    case SG_HOLDOUT:
      hud_set("READY ");
      break;
    case SG_CHASE:
      door_open = 1u;
      st_next = (uint16_t)stg.grace * 4u;
      hud_set("RUN");
      break;
    case SG_HUNT:
      st_fam = kc_fam[stg.cond[0]];
      if (!hunt_spawn(0u)) st_pend = 2u;
      hud_numset("MARK", 1u, stg.conds == 2u ? "/2" : "/1");
      break;
    case SG_ESCORT:
      stage_cell = stg.tcell;
      stage_thp = stg.grace;
      st_x = 1u; /* the next waypoint */
      st_next = 120u;
      hud_numset("TEMP", stage_thp, "");
      break;
    case SG_MIXP:
      eg_piece_name(stg.piece, sbuf);
      strcpy(stage_goal, "MAKE ");
      for (k = 0; sbuf[k] && k != 8u; k++) stage_goal[5u + k] = sbuf[k];
      strcpy(&stage_goal[5u + k], " FAST");
      eg_piece_abbr(stg.piece, sbuf);
      hud_set(sbuf);
      st_next = 180u;
      break;
    case SG_DARK:
      break; /* the keycards on the HUD */
    case SG_HEIST:
      door_open = 1u;
      hud_numset("COST", stg.price, "");
      break;
  }
}

/* display off, after look_load: the people cards and their palette, the stage's skins */
void stage_show(void) BANKED {
  uint8_t k, i;
  uint16_t s;
  if (stage_type == SG_PUZZLE) return;
  if (stage_type == SG_HUNT)
    ss_preload((uint8_t)((1u << kc_fam[stg.cond[0]]) |
        (stg.conds == 2u ? 1u << kc_fam[stg.cond[1]] : 0u)));
  else if(stage_type==SG_HOLDOUT && stg.level>=3u)
    ss_preload((1u<<SS_CHASER)|(1u<<SS_SHOOTER)|
      (1u<<(stg.level>=8u && (st_seed&1u) ? FAM_MEMO : FAM_STAPLES)));
  else ss_preload((1u << SS_CHASER) | (1u << SS_SHOOTER));
  if (stage_type == SG_ESCORT || stage_type == SG_HEIST) {
    VBK_REG = 0;
    set_bkg_data(STAGE_T0, (uint8_t)(STAGE_CARDS * 4u), stage_card_tiles); /* 0x9000 block: signed BG addressing */
    for (k = 0; k != STAGE_CARDS; k++) {
      for (i = 0; i != 4u; i++) meta_tile[CARD_M(k)][i] = (uint8_t)(STAGE_T0 + k * 4u + i);
      meta_pal[CARD_M(k)] = (uint8_t)(LOOK_PAL_REACT_B | LOOK_META_BANK0);
    }
    /* REACTION B becomes the person: ink, the slab (kept), skin, clothes, seeded by the run and the floor */
    s = (uint16_t)(run_seed ^ ((uint16_t)floor_no * 0x2b9u) ^ ((uint16_t)stage_type << 6));
    pal_bg[LOOK_PAL_REACT_B * 4u] = RGB(3, 2, 5);
    pal_bg[LOOK_PAL_REACT_B * 4u + 2u] = people[(uint8_t)(s & 7u)];
    pal_bg[LOOK_PAL_REACT_B * 4u + 3u] = people[ART_SKINS + ART_HAIRS + (uint8_t)((s >> 3) & 7u)];
    if (stage_type == SG_ESCORT) look_skin[stg.tcell] = (uint8_t)(CARD_M(STAGE_CARD_BACK_A) + 1u);
    else if (!st_theft) look_skin[stg.tcell] = (uint8_t)(CARD_M(STAGE_CARD_FRONT) + 1u);
  } else if (stage_type == SG_MIXP)
    look_skin[stg.spot] = (uint8_t)(M_BENCH + 1u);
  else if (stage_type == SG_DARK) {
    st_pc = room.start;
    dark_radius();
    st_x = dk_r;
    i = 0;
    do {
      if (!look_void_cell(i)) {
        k = dk_want(i);
        look_skin[i] = k == LOOK_SKIN_NONE ? 0u : (uint8_t)(k + 1u);
      }
    } while (++i != (uint8_t)EG_N);
  }
}

uint8_t stage_announce(void) BANKED {
  if(world_on) {hud_msg(enemies_alive ? "FIGHT. SOLVE. EXIT." : "LOAD. CROSS. EXIT.");return 1u;}
  if (slice.physical&31u) { eg_physics_goal(slice.physical&31u, sbuf); hud_msg(sbuf); return 1u; }
  if (!stage_goal[0]) return 0u;
  hud_msg(stage_goal);
  return 1u;
}

void stage_tick(void) BANKED {
  uint8_t left;
  if (stage_type == SG_PUZZLE || pl_hp == 0u) return;
  if (st_t != 0xffffu) st_t++;
  wave_step();
  switch (stage_type) {
    case SG_HOLDOUT:
      if (stage_won) break;
      left=(uint8_t)(wv_pend+enemies_alive);
      if(left!=st_pc) {
        st_pc=left;sbuf[0]='W';sbuf[1]=(char)('0'+stage_wave);sbuf[2]='/';
        sbuf[3]=(char)('0'+stg.waves);sbuf[4]=' ';sbuf[5]=(char)('0'+left);sbuf[6]=0;hud_set(sbuf);
      }
      if(left) {st_acc=0u;break;}
      stage_pct=(uint8_t)((uint16_t)stage_wave*100u/stg.waves);
      if(stage_wave>=stg.waves) {win("WAVES CLEARED");break;}
      if(++st_acc>=90u) {
        st_acc=0u;
        wave_begin(eg_wave_count(&stg,stage_wave),(uint8_t)(stage_wave>=stg.shooters));
        hud_msg(stage_wave==stg.waves ? "FINAL WAVE: FIGHT" : "WAVE INCOMING");
      }
      break;
    case SG_CHASE: chase_tick(); break;
    case SG_HUNT:
      if (!stage_won) hunt_tick();
      break;
    case SG_ESCORT: escort_tick(); break;
    case SG_MIXP:
      if (!stage_won) mixp_tick();
      break;
    case SG_DARK: dark_tick(); break;
    case SG_HEIST: heist_tick(); break;
  }
}

uint8_t stage_select(void) BANKED {
  if (stage_type == SG_MIXP && !stage_won && !in_ring()) {
    sfx(SFX_DENY);
    hud_msg("THE BENCH: IN RING");
    return 1u;
  }
  if (stage_type == SG_HEIST && !st_paid && !st_theft && pcell() == (uint8_t)(stg.spot + EG_W) &&
      cell_type(stg.spot) == T_ITEM) {
    if (tokens < stg.price) {
      sfx(SFX_DENY);
      hud_msg("NOT ENOUGH TOKENS");
      return 1u;
    }
    tokens = (uint8_t)(tokens - stg.price);
    st_paid = 1u;
    hud_dirty = 1u;
    puzzle_pick(stg.spot);
    win("PAID. GOOD SHIFT");
    return 1u;
  }
  return 0u;
}

void stage_floor_done(void) BANKED {
  if (slice.planned && stage_type == SG_PUZZLE) puzzle_plan_done(); /* the planner learns from its puzzle rooms */
  else if (slice.planned)
    meta.seen |= MK_TO_SEEN(plan.seen);
}
