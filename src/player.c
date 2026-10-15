/* The player: the heir on the floor. Movement is src/core/eg_move (8-way on 12.4 fixed point, or 4-way with the most
 * recent press winning; axis-separated collision, wall slide, corner nudge, a CRT pushed sideways after 10 frames of
 * deliberate contact), the buttons are src/core/eg_input (docs/redesign.md section 6: A tap the 3-hit combo, A hold a
 * charge released into the charged attack, B tap the heir's own dodge, B hold the sprint, B then A the lunge, A then
 * B the parry). Digging and collecting by entering cells. The heir's sprite and its animation states are heir.c. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "mob.h" /* B2: hit_src */
#include "heir.h"
#include "core/eg_input.h"
#include "core/eg_move.h"
#include "puzzle.h"

uint8_t pl_x, pl_y, pl_hp, pl_hp_max, pl_state, pl_face;
uint8_t pl_t, pl_inv, pl_combo, pl_queue, pl_charge, roll_cd, push_t, hit_mask, walk_t, combo_gap;
int8_t roll_dx, roll_dy, kb_dx, kb_dy;
uint16_t psx, psy;
floor_acts_t acts;
/* B1: the resolver, the mover and what the sprite code reads */
eg_input_t pin;
eg_mover_t pmv;
uint8_t pl_iframe; /* dodge i-frames this frame */
uint8_t pl_arc;    /* the lead's arc to draw: 0 none, 1 a strike toward pl_face, 2 the spin */
uint8_t pl_level;  /* the last charged attack's level, 1..3 */
static uint8_t pmap[256], map_door = 0xffu;
static const eg_grid_t pg = {room.c, EG_W, EG_H, 4u, pmap};
static uint8_t dodge_dp, ov_state, ov_t, fight_t, moved_last, last_face;

/* 8-way facing (EG_FACE_*: N NE E SE S SW W NW) as vectors and d-pad bits */
static const int8_t f8x[8] = {0, 1, 1, 1, 0, -1, -1, -1}, f8y[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
static const uint8_t f8dp[8] = {J_UP, J_UP | J_RIGHT, J_RIGHT, J_DOWN | J_RIGHT, J_DOWN, J_DOWN | J_LEFT, J_LEFT,
                                J_UP | J_LEFT};

static uint8_t face_of_dp(uint8_t d) {
  uint8_t f;
  for (f = 0; f != 8u; f++)
    if (f8dp[f] == d) return f;
  return pmv.facing;
}

/* the cardinal facing the grid acts use (bricks, the arc): a diagonal keeps the old one if it is one of its axes */
static void cardinal_face(void) {
  int8_t ix = f8x[pmv.facing], iy = f8y[pmv.facing];
  if (ix && iy) {
    if ((pl_face == F_LEFT && ix < 0) || (pl_face == F_RIGHT && ix > 0) || (pl_face == F_UP && iy < 0) ||
        (pl_face == F_DOWN && iy > 0))
      return;
  }
  if (ix) pl_face = ix < 0 ? F_LEFT : F_RIGHT;
  else pl_face = iy < 0 ? F_UP : F_DOWN;
}

/* the mover reads room.c itself through a byte -> cell map (no copy per step), built at the floor's start; the door
 * bytes follow the door. T_* -> mover cell: 0 free, 1 wall, 2 a CRT (a wall while it falls), 3 the door (a wall until open) */
/* B1: every cell type the shared rules call passable is free (fragile floor, glitched glass, ice, plates, pieces) */
static void grid_map(void) {
  uint8_t c = 0, v, t;
  map_door = door_open;
  do {
    t = (uint8_t)(c & T_MASK);
    v = t == T_ROCK ? ((c & F_FALL) ? EG_C_WALL : EG_C_BLOCK)
        : t == T_DOOR ? (door_open ? EG_C_FREE : EG_C_WALL)
        : EG_PASSABLE(t) ? EG_C_FREE : EG_C_WALL;
    pmap[c] = v;
  } while (++c);
}
static void door_map(void) { /* the door opened or closed: only door bytes change */
  uint8_t c = T_DOOR;
  map_door = door_open;
  do {
    pmap[c] = door_open ? EG_C_FREE : EG_C_WALL;
    c += 0x10u;
  } while (c != T_DOOR);
}

void player_hurt(uint8_t n, uint8_t fromx, uint8_t fromy) BANKED {
  if (pl_inv || pl_state == PS_EXIT || !pl_hp || pl_iframe) return;
  if (pin.flags & EG_FL_GUARD) { /* the parry's guard takes it */
    sfx(SFX_DENY);
    freeze = 2u;
    return;
  }
  if (!dbg_god) pl_hp = pl_hp > n ? (uint8_t)(pl_hp - n) : 0u;
  if (acts.hits_taken != 255u) acts.hits_taken++;
  pl_inv = 60u;
  pl_state = PS_HURT;
  pl_t = 8u;
  pl_charge = 0;
  pl_arc = 0;
  fight_t = 180u;
  kb_dx = fromx < pl_x ? 1 : (fromx > pl_x ? -1 : 0);
  kb_dy = fromy < pl_y ? 1 : (fromy > pl_y ? -1 : 0);
  freeze = 4u;
  shake = 8u;
  sfx(SFX_HURT);
  hud_dirty = 1;
  heir_idle_stop();
  heir_play(pl_hp ? HA_HURT : HA_DEATH, 1);
}

void open_door(void) BANKED {
  door_open = 1;
  queue_cell(room.door);
  sfx(SFX_DOOR);
  hud_msg("THE DOOR HUMS");
}

static void overlay(uint8_t st, uint8_t t) {
  ov_state = st;
  ov_t = t;
  heir_idle_stop();
}

static void collect(void) {
  uint8_t i = cell_at(pl_x, pl_y), t = cell_type(i);
  if (t == T_DIRT) {
    room.c[i] = T_EMPTY;
    queue_cell(i);
    sfx(SFX_DIG);
    if (acts.dug != 255u) acts.dug++;
    puff((uint8_t)(EG_X(i) * 16u + 8u), (uint8_t)(EG_Y(i) * 16u + 8u), 2, ST_PUFF);
    overlay(HA_DIG, 12u);
  } else if (t == T_TOKEN) {
    room.c[i] = T_EMPTY;
    queue_cell(i);
    if (tokens != 255u) tokens++;
    if (acts.picked != 255u) acts.picked++;
    sfx(SFX_TOKEN);
    puff(pl_x, (uint8_t)(pl_y - 10u), 5, ST_SPARK);
    hud_dirty = 1;
  } else if (t == T_KEY) {
    room.c[i] = T_EMPTY;
    queue_cell(i);
    keys_held++;
    sfx(SFX_KEY);
    puff(pl_x, (uint8_t)(pl_y - 10u), 4, ST_SPARK);
    hud_dirty = 1;
    overlay(HA_PICKUP, 16u);
    if (keys_held == room.keys) open_door();
    else hud_msg("KEYCARD");
  } else if (t == T_ITEM)
    puzzle_pick(i); /* B1: a piece into the pocket */
}

/* a push request from the mover: CRTs go sideways only (the solver's rules), into an empty cell */
static void do_push(void) {
  uint8_t c = (uint8_t)(pmv.req_cy * EG_W + pmv.req_cx), beyond;
  if (pmv.req_dir != EG_DP_LEFT && pmv.req_dir != EG_DP_RIGHT) return;
  if (cell_type(c) != T_ROCK || (room.c[c] & F_FALL)) return;
  beyond = (uint8_t)(pmv.req_dir == EG_DP_RIGHT ? c + 1u : c - 1u);
  if (enemy_in_cell(beyond) || !eg_push(&room, c, beyond)) { /* B1: the shared rule (plates, pits, glass too) */
    sfx(SFX_DENY);
    return;
  }
  puzzle_dirty = 1u; /* B1: a push can seal a room */
  queue_cell(c);
  queue_cell(beyond);
  sfx(SFX_PUSH);
  if (acts.pushes != 255u) acts.pushes++;
  grav_t = 1u; /* let it drop promptly if nothing holds it */
}

/* one mover step from the player's centre (psx, psy are the centre, the mover's box is 12x12 from its corner) */
static void mstep(uint8_t dpad, uint8_t speed, uint8_t synth) {
  uint8_t prev = pmv.prev, o0 = pmv.order[0], o1 = pmv.order[1], o2 = pmv.order[2], o3 = pmv.order[3];
  if (map_door != door_open) door_map();
  pmv.x = (int16_t)psx - 96;
  pmv.y = (int16_t)psy - 96;
  pmv.mode = (opt_move4 || EG_OPT_MOVE4) && !synth ? EG_MODE_4WAY : EG_MODE_8WAY;
  eg_move_step(&pmv, dpad, speed, &pg);
  if (synth) { /* momentum, not a press: the d-pad's press order and the push contact are not touched */
    pmv.prev = prev;
    pmv.order[0] = o0;
    pmv.order[1] = o1;
    pmv.order[2] = o2;
    pmv.order[3] = o3;
    pmv.push_t = 0;
  } else if (pmv.req)
    do_push();
  psx = (uint16_t)(pmv.x + 96);
  psy = (uint16_t)(pmv.y + 96);
  pl_x = (uint8_t)(psx >> 4);
  pl_y = (uint8_t)(psy >> 4);
  push_t = pmv.push_t;
  collect();
}

static void break_brick(uint8_t i, uint8_t force) {
  if (cell_type(i) != T_BRICK) return;
  if (!force && !is_cracked(i)) {
    cracked[i >> 3] |= (uint8_t)(1u << (i & 7u));
    queue_cell(i);
    sfx(SFX_HIT);
    return;
  }
  cracked[i >> 3] &= (uint8_t)~(1u << (i & 7u));
  room.c[i] = T_EMPTY;
  queue_cell(i);
  sfx(SFX_BREAK);
  if (acts.breaks != 255u) acts.breaks++;
  puff((uint8_t)(EG_X(i) * 16u + 8u), (uint8_t)(EG_Y(i) * 16u + 8u), 4, ST_PUFF);
  grav_t = 1u;
}

static void deflect(orb_t *o, uint8_t perfect) {
  o->friendly = 1;
  o->vx = (int8_t)(-o->vx - (o->vx >> 1));
  o->vy = (int8_t)(-o->vy - (o->vy >> 1));
  if (perfect) {
    o->vx = (int8_t)(o->vx + (o->vx >> 1));
    o->vy = (int8_t)(o->vy + (o->vy >> 1));
  }
  o->life = 90u;
  if (acts.deflects != 255u) acts.deflects++;
  sfx(SFX_HIT);
  freeze = 2u;
}

/* the lead's reach: a box toward the 8-way facing; the spin's: around the player */
/* B2: what the next enemy_hit came from (src/mob.h hit_src) */
static void hit_from_move(uint8_t spin) {
  static const uint8_t src[EG_MV_COUNT] = {HS_FUSE, HS_FUSE, HS_FUSE, HS_FUSE, HS_LUNGE, HS_HIT1,
                                           HS_HIT2, HS_HIT3, HS_CHARGED, HS_CHARGED, HS_PARRY};
  hit_src = spin && pin.move == EG_MV_CHARGED ? HS_SPIN : src[pin.move];
  hit_tags = 0;
}

static void strike(uint8_t spin, uint8_t dmg) {
  uint8_t cx, cy, rx, ry, k, reach = (mods & MOD_LONG) ? 18u : 14u;
  int8_t ix = f8x[pmv.facing], iy = f8y[pmv.facing];
  hit_from_move(spin); /* B2 */
  if (spin) {
    cx = pl_x;
    cy = pl_y;
    rx = ry = (mods & MOD_LONG) ? 28u : 24u;
  } else if (ix && iy) {
    reach = (uint8_t)(reach - (reach >> 2));
    cx = (uint8_t)(pl_x + ix * (int8_t)reach);
    cy = (uint8_t)(pl_y + iy * (int8_t)reach);
    rx = ry = 13u;
  } else {
    cx = (uint8_t)(pl_x + ix * (int8_t)reach);
    cy = (uint8_t)(pl_y + iy * (int8_t)reach);
    rx = ix ? (uint8_t)(reach - 1u) : 14u;
    ry = ix ? 14u : (uint8_t)(reach - 1u);
  }
  for (k = 0; k != nen; k++) {
    uint8_t r2 = en[k].kind == SPAWN_BOSS ? 14u : 0u;
    if (en[k].st == ES_DEAD || (hit_mask & (uint8_t)(1u << k))) continue;
    if (overlap(cx, cy, (uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4), (uint8_t)(rx + r2), (uint8_t)(ry + r2))) {
      hit_mask |= (uint8_t)(1u << k);
      fight_t = 180u;
      enemy_hit(k, dmg, pl_x, pl_y);
    }
  }
  for (k = 0; k != MAXO; k++) {
    if (!orbs[k].live || orbs[k].friendly) continue;
    if (overlap(cx, cy, (uint8_t)(orbs[k].x >> 4), (uint8_t)(orbs[k].y >> 4), (uint8_t)(rx + 2u), (uint8_t)(ry + 2u)))
      deflect(&orbs[k], 0); /* send it back faster; now it hurts them */
  }
}

/* the parry's guard: orbs that reach the heir go back; on the perfect frames they go back harder and whatever stands
 * right in front staggers */
static void guard(void) {
  uint8_t k, perfect = (uint8_t)((pin.flags & EG_FL_PERFECT) != 0u);
  for (k = 0; k != MAXO; k++) {
    if (!orbs[k].live || orbs[k].friendly) continue;
    if (overlap(pl_x, pl_y, (uint8_t)(orbs[k].x >> 4), (uint8_t)(orbs[k].y >> 4), 16u, 16u)) {
      deflect(&orbs[k], perfect);
      if (perfect) puff(pl_x, (uint8_t)(pl_y - 8u), 4, ST_SPARK);
    }
  }
  if (perfect && pin.frame == 2u) {
    hit_mask = 0;
    hit_src = HS_PARRY; /* B2 */
    hit_tags = 0;
    for (k = 0; k != nen; k++)
      if (en[k].st != ES_DEAD && en[k].kind != SPAWN_BOSS &&
          overlap(pl_x, pl_y, (uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4), 18u, 18u))
        enemy_hit(k, 1u, pl_x, pl_y);
  }
}

static void spin_bricks(void) {
  uint8_t c = cell_at(pl_x, pl_y);
  break_brick((uint8_t)(c - EG_W), 1);
  break_brick((uint8_t)(c + EG_W), 1);
  break_brick((uint8_t)(c - 1u), 1);
  break_brick((uint8_t)(c + 1u), 1);
}

static uint8_t front_cell(void) {
  return cell_at((uint8_t)(pl_x + fdx[pl_face] * 12), (uint8_t)(pl_y + fdy[pl_face] * 12));
}

/* d-pad bits as the mover would resolve them (opposites cancel; 4-way: the newest held press) */
static uint8_t dpad_now(void) {
  uint8_t d = (uint8_t)(joy & 0x0fu), i;
  if ((d & (J_LEFT | J_RIGHT)) == (J_LEFT | J_RIGHT)) d &= (uint8_t)~(J_LEFT | J_RIGHT);
  if ((d & (J_UP | J_DOWN)) == (J_UP | J_DOWN)) d &= (uint8_t)~(J_UP | J_DOWN);
  if ((opt_move4 || EG_OPT_MOVE4) && d) {
    for (i = 0; i != 4u; i++)
      if (d & pmv.order[i]) return pmv.order[i];
  }
  return d;
}

static uint8_t walk_speed(void) { return (heir.traits & HT_HEAVY) ? 18u : 20u; }

static void on_start(uint8_t mv) {
  uint8_t d = dpad_now();
  pl_arc = 0;
  hit_mask = 0;
  heir_idle_stop();
  ov_t = 0;
  switch (mv) {
    case EG_MV_DODGE:
      dodge_dp = d ? d : f8dp[pmv.facing];
      pmv.facing = face_of_dp(dodge_dp);
      roll_dx = f8x[pmv.facing];
      roll_dy = f8y[pmv.facing];
      pl_charge = 0;
      hud_charge();
      sfx(SFX_ROLL);
      puff(pl_x, (uint8_t)(pl_y + 4u), 2, ST_PUFF);
      heir_play(pin.style == EG_DODGE_BLINK ? HA_DODGE_BLINK : (pin.style == EG_DODGE_SLIDE ? HA_DODGE_SLIDE
                                                                                            : HA_DODGE_ROLL), 1);
      break;
    case EG_MV_SPRINT: heir_play(HA_SPRINT, 1); break;
    case EG_MV_SKID: heir_play(HA_SKID, 1); break;
    case EG_MV_LUNGE:
      sfx(SFX_LASH);
      heir_play(HA_LUNGE, 1);
      break;
    case EG_MV_HIT1:
    case EG_MV_HIT2:
    case EG_MV_HIT3:
      if (d) pmv.facing = face_of_dp(d);
      pl_combo = (uint8_t)(mv - EG_MV_HIT1);
      sfx(SFX_LASH);
      heir_play((uint8_t)(HA_HIT1 + pl_combo), 1);
      break;
    case EG_MV_CHARGE:
      pl_charge = pin.a_hold;
      heir_play(HA_CHARGE, 1);
      break;
    case EG_MV_CHARGED:
      pl_level = pin.level;
      if ((mods & MOD_STATIC) && pl_level < 3u) pl_level++; /* charges faster: a level up */
      pl_charge = 0;
      hud_charge();
      if (pl_level >= 2u) {
        sfx(SFX_SPIN);
        heir_play(HA_SPIN, 1);
      } else {
        sfx(SFX_LASH);
        heir_play(HA_CHARGED, 1);
      }
      break;
    case EG_MV_PARRY:
      sfx(SFX_LASH);
      heir_play(HA_PARRY, 1);
      break;
    default:
      break;
  }
  cardinal_face();
}

static uint8_t dodge_speed(void) {
  uint8_t ph = pin.phase;
  if (pin.style == EG_DODGE_BLINK) return ph == EG_PH_ACTIVE ? 104u : (ph == EG_PH_STARTUP ? 8u : 0u);
  if (pin.style == EG_DODGE_SLIDE) return ph == EG_PH_ACTIVE ? 36u : 12u;
  return ph == EG_PH_ACTIVE ? 44u : (ph == EG_PH_STARTUP ? 24u : 16u);
}

/* walking, pushing, the idle animations */
static void walk(uint8_t speed, uint8_t anim) {
  uint8_t d = (uint8_t)(joy & 0x0fu), f0 = pmv.facing;
  if (d) {
    mstep(d, speed, 0);
    cardinal_face();
    walk_t++;
    if ((walk_t & 15u) == 0u) sfx(SFX_STEP);
    heir_idle_stop();
    if (!moved_last && ((uint8_t)(pmv.facing - last_face + 1u) & 7u) >= 3u) overlay(HA_TURN, 13u);
    if (ov_t && ov_state == HA_SPAWN) ov_t = 0;
    if (!anim) return;
    if (ov_t) heir_play(ov_state, 0);
    else heir_play(pmv.push_t ? HA_PUSH : HA_WALK, 0);
  } else {
    walk_t = 0;
    pmv.push_t = 0;
    push_t = 0;
    pmv.prev = 0; /* a release: the next press is fresh */
    if (!anim) return;
    if (ov_t) heir_play(ov_state, 0);
    else heir_idle((uint8_t)((fight_t ? EG_IC_FIGHT : 0u) | (pl_hp <= 2u ? EG_IC_LOWHP : 0u)));
  }
  moved_last = (uint8_t)(d != 0u);
  if (!d) last_face = f0;
  else last_face = pmv.facing;
}

void player_floor_start(void) BANKED {
  grid_map(); /* here, not on the first step: 256 entries cost most of a frame */
  eg_input_init(&pin, heir.dodge);
  eg_move_init(&pmv, (int16_t)psx - 96, (int16_t)psy - 96, EG_MODE_8WAY);
  pl_face = F_DOWN;
  pl_iframe = pl_arc = 0;
  fight_t = 0;
  moved_last = 0;
  last_face = pmv.facing;
  ha_dir = 0;
  heir_idle_stop();
  heir_play(HA_SPAWN, 1);
  overlay(HA_SPAWN, 34u);
  heir_reset();
}

void player_update(void) BANKED {
  uint8_t st, mv, prev = pin.move, ph, dmg;
  if (pl_inv) pl_inv--;
  if (fight_t) fight_t--;
  if (ov_t) ov_t--;
  if (dbg_heir_state) { /* the harness's state sheet: a state on loop, standing still */
    ha_dir = (uint8_t)(dbg_heir_dir & 7u);
    heir_play((uint8_t)(dbg_heir_state - 1u), 0);
    if (ha_done) heir_play((uint8_t)(dbg_heir_state - 1u), 1);
    pl_state = PS_FREE;
    return;
  }
  switch (pl_state) {
    case PS_EXIT: return;
    case PS_HURT: { /* knocked back; the resolver starts clean after */
      uint8_t kd = (uint8_t)((kb_dx > 0 ? J_RIGHT : (kb_dx < 0 ? J_LEFT : 0u)) |
                             (kb_dy > 0 ? J_DOWN : (kb_dy < 0 ? J_UP : 0u)));
      uint8_t f = pmv.facing;
      if (kd) mstep(kd, 32u, 1);
      pmv.facing = f;
      if (!--pl_t) {
        pl_state = PS_FREE;
        eg_input_init(&pin, heir.dodge);
      }
      ha_dir = (uint8_t)((4u - pmv.facing) & 7u);
      return;
    }
    default: break;
  }
  st = eg_input_step(&pin, (uint8_t)(joy & (J_A | J_B)));
  mv = pin.move;
  if (prev == EG_MV_DODGE && mv != EG_MV_DODGE && mv != EG_MV_SPRINT && (mods & MOD_FUSE)) {
    hit_mask = 0; /* the dodge ends in a static shock around the player */
    strike(1, 1u);
    puff(pl_x, pl_y, 4, ST_SPARK);
  }
  if (st != EG_NO_MOVE) on_start(st);
  pl_iframe = (uint8_t)(pin.flags & EG_FL_IFRAME);
  ph = pin.phase;
  pl_t = pin.frame;
  pl_arc = 0;
  pl_state = PS_FREE;
  switch (mv) {
    case EG_MV_IDLE:
      walk(walk_speed(), 1);
      break;
    case EG_MV_CHARGE:
      walk((uint8_t)(walk_speed() >> 1), 0);
      heir_play(HA_CHARGE, 0);
      st = pin.a_hold;
      if (mods & MOD_STATIC) st = (uint8_t)(st > 166u ? 250u : st + (st >> 1));
      if (pl_charge < 30u && st >= 30u) {
        sfx(SFX_CHARGED);
        puff(pl_x, (uint8_t)(pl_y - 12u), 4, ST_SPARK);
      }
      pl_charge = st;
      if ((pl_charge & 3u) == 0u) hud_charge();
      break;
    case EG_MV_DODGE:
      pl_state = PS_ROLL;
      mstep(dodge_dp, dodge_speed(), 1);
      break;
    case EG_MV_SPRINT: {
      uint8_t d = dpad_now(), sp = (heir.traits & HT_HEAVY) ? 25u : 28u;
      if (d) dodge_dp = d;
      pl_state = PS_ROLL;
      mstep(dodge_dp, sp, 1);
      pmv.facing = face_of_dp(dodge_dp);
      cardinal_face();
      walk_t++;
      if ((walk_t & 7u) == 0u) sfx(SFX_STEP);
      break;
    }
    case EG_MV_SKID:
      mstep(dodge_dp, (uint8_t)(16u - (pin.frame << 2)), 1);
      break;
    case EG_MV_LUNGE:
      pl_state = PS_LASH;
      if (pin.frame < 6u) mstep(f8dp[pmv.facing], 40u, 1);
      if (ph == EG_PH_ACTIVE) {
        strike(0, 2u);
        pl_arc = 1;
      }
      break;
    case EG_MV_HIT1:
    case EG_MV_HIT2:
    case EG_MV_HIT3:
      pl_state = PS_LASH;
      if (pin.frame == 2u) mstep(f8dp[pmv.facing], 24u, 1); /* step into it */
      if (ph == EG_PH_ACTIVE) {
        dmg = mv == EG_MV_HIT3 ? ((mods & MOD_SURGE) ? 3u : 2u) : 1u;
        strike(0, dmg);
        if (pin.frame == eg_input_fd[mv].startup) break_brick(front_cell(), (uint8_t)(mv == EG_MV_HIT3));
      }
      if (pin.frame >= 2u && pin.frame < (uint8_t)(eg_input_fd[mv].startup + eg_input_fd[mv].active + 3u)) pl_arc = 1;
      break;
    case EG_MV_CHARGED:
      if (pl_level >= 2u) {
        pl_state = PS_SPIN;
        if (ph == EG_PH_ACTIVE || (ph == EG_PH_RECOVERY && pin.frame < 14u)) {
          strike(1, (uint8_t)(((mods & MOD_SURGE) ? 4u : 3u) + (pl_level == 3u ? 1u : 0u)));
          if (pin.frame == 3u) {
            spin_bricks();
            shake = 4u;
          }
        }
        if (pin.frame <= 16u) pl_arc = 2;
      } else {
        pl_state = PS_LASH;
        if (ph == EG_PH_ACTIVE) {
          strike(0, 2u);
          if (pin.frame == 3u) break_brick(front_cell(), 1);
        }
        if (pin.frame >= 2u && pin.frame < 10u) pl_arc = 1;
      }
      break;
    case EG_MV_PARRY:
      pl_state = PS_LASH;
      if (pin.flags & EG_FL_GUARD) guard();
      break;
    default:
      break;
  }
  if (mv != EG_MV_CHARGE && pl_charge) {
    pl_charge = 0;
    hud_charge();
  }
  ha_dir = (uint8_t)((4u - pmv.facing) & 7u);
}
