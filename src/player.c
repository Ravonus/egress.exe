/* Player movement and attacks. A tap/hold = primary/charge, B tap/hold = shot/secondary charge.
 * Directional double taps evade relative to the pre-tap aim; holding a direction runs.
 * A>B counters, B>A rushes. The shared resolver in core/eg_input owns those decisions. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "mob.h" /* B2: hit_src */
#include "obstacle.h"
#include "boss_api.h"
#include "heir.h"
#include "core/eg_input.h"
#include "core/eg_move.h"
#include "puzzle.h"
#include "family.h"
#include "core/eg_view.h"
#include "world.h"
#include "world_play.h"
void play_camera_warp(void) BANKED;
#include "resume.h"
#include "run.h"
#include "narrator.h"
#include "stamina.h"
#include "tutorial.h"
#include "look.h"

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
/* room occupies D800..D93F at most (asserted by the build); this floor-only map
 * follows it. Both remain intact in bank 1 during title playback. */
static uint8_t __at(0xD940) pmap[256];
static uint8_t map_door = 0xffu;
static eg_grid_t pg; /* WRAM: eg_move (banked) reads it; set in player_floor_start */
static uint8_t dodge_dp, ov_state, ov_t, fight_t, moved_last, last_face;
static uint8_t portal_lock;
int8_t __at(0xDB9A) pl_z;
uint8_t __at(0xDB9B) pl_air;
static uint8_t __at(0xDB9C) air_void;
static uint8_t __at(0xDB9D) fall_safe;
static const uint8_t jump_z[20]={0,3,5,7,9,10,11,12,12,12,11,10,9,7,5,3,1,0,0,0};
static void overlay(uint8_t st,uint8_t t);

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
    v = t != T_ROCK && (c & F_OB) ? EG_C_WALL
        : t == T_ROCK ? ((c & F_FALL) ? EG_C_WALL : EG_C_BLOCK)
        : EG_CRATE(t) ? EG_C_BLOCK
        : t == T_DOOR ? (door_open ? EG_C_FREE : EG_C_WALL)
        : (EG_PASSABLE(t) || t==T_PIT) ? EG_C_FREE : EG_C_WALL;
    pmap[c] = v;
  } while (++c);
}
static void door_map(void) { /* the door opened or closed: only door bytes change */
  uint8_t c = T_DOOR;
  map_door = door_open;
  do {
    pmap[c] = door_open ? EG_C_FREE : EG_C_WALL;
    c += 0x20u; /* five type bits: 0x10 would also overwrite worn carpet (type 23) */
  } while (c != T_DOOR);
}

void player_hurt(uint8_t n, uint8_t fromx, uint8_t fromy) BANKED {
  if (pl_inv || pl_state == PS_EXIT || pl_state==PS_FALL || !pl_hp || pl_iframe) return;
  if (pin.flags & EG_FL_GUARD) { /* the parry's guard takes it */
    sfx(SFX_DENY);
    freeze = 2u;
    return;
  }
  if (!dbg_god) pl_hp = pl_hp > n ? (uint8_t)(pl_hp - n) : 0u;
  if (!pl_hp && boss_floor) boss_death_record();
  if (acts.hits_taken != 255u) acts.hits_taken++;
  pl_inv = 60u;
  pl_state = PS_HURT;
  pl_t = 8u;
  pl_charge = 0;
  pl_arc = 0;
  fight_t = 180u;
  pl_air=0u;pl_z=0;
  kb_dx = fromx < pl_x ? 1 : (fromx > pl_x ? -1 : 0);
  kb_dy = fromy < pl_y ? 1 : (fromy > pl_y ? -1 : 0);
  freeze = 4u;
  shake = 8u;
  sfx(SFX_HURT);
  hud_dirty = 1;
  heir_idle_stop();
  heir_play(pl_hp ? HA_HURT : HA_DEATH, 1);
  narrator_event(NE_HURT,0u);
  if(!pl_hp) run_record_end(0u);
  else if(!dbg_god) {if(pl_hp<=2u) narrator_event(NE_CLOSE,0u);resume_damage();}
}

/* A void fall ignores dodge/guard immunity. Charge once, animate down into the void,
 * then restore the last firm tile; power-off retains the already-paid damage. */
void player_fall(uint8_t safe) BANKED {
  if(pl_state==PS_FALL || pl_state==PS_EXIT || !pl_hp) return;
  pl_inv=pl_iframe=0u;pin.flags=0u;pl_air=0u;pl_z=0;fall_safe=safe;
  player_hurt(1u,pl_x,pl_y);
  pl_state=PS_FALL;pl_t=0u;freeze=0u;pl_arc=pl_charge=0u;air_void=0u;
  narrator_event(NE_FALL,0u);
}

static void fall_tick(void) {
  if(pl_t<30u) {
    pl_t++;
    pl_z=(int8_t)-(int8_t)(pl_t+(pl_t>>1));
    return;
  }
  if(!pl_hp) return;
  psx=cell_sub(EG_X(fall_safe));psy=cell_sub(EG_Y(fall_safe));
  pl_x=(uint8_t)(psx>>4);pl_y=(uint8_t)(psy>>4);pl_z=0;
  pl_state=PS_FREE;pl_inv=60u;
  look_anchor(fall_safe);
  eg_input_init(&pin,heir.dodge);pin.weapon=heir.weapon;
  overlay(HA_SPAWN,20u);
  heir_play(HA_SPAWN,1u);
}

void open_door(void) BANKED {
  door_open = 1;
  if(room.door<EG_N)queue_cell(room.door);
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
  uint8_t c = (uint8_t)(pmv.req_cy * EG_W + pmv.req_cx), beyond, crate = EG_CRATE(room.c[c]);
  if (!crate && pmv.req_dir != EG_DP_LEFT && pmv.req_dir != EG_DP_RIGHT) return;
  if (!crate && (cell_type(c) != T_ROCK || (room.c[c] & F_FALL))) return;
  beyond = (uint8_t)(c + (pmv.req_dir == EG_DP_RIGHT ? 1 : pmv.req_dir == EG_DP_LEFT ? -1 :
                         pmv.req_dir == EG_DP_DOWN ? 16 : -16));
  if (enemy_in_cell(beyond) || (crate ? !eg_push_block(&room, c, beyond) : !eg_push(&room, c, beyond))) {
    sfx(SFX_DENY);
    return;
  }
  puzzle_dirty = 1u; /* B1: a push can seal a room */
  if(room.portal_need&EG_PORTAL_ON) {
    uint8_t to,occ[4];
    occ[0]=cell_at((uint8_t)(pl_x-6u),(uint8_t)(pl_y-6u));
    occ[1]=cell_at((uint8_t)(pl_x+5u),(uint8_t)(pl_y-6u));
    occ[2]=cell_at((uint8_t)(pl_x-6u),(uint8_t)(pl_y+5u));
    occ[3]=cell_at((uint8_t)(pl_x+5u),(uint8_t)(pl_y+5u));
    eg_ndirty=0u;to=eg_portal_object(&room,beyond,occ,4u);
    if(to!=EG_LINK_NONE) {
      queue_cell(to);sfx(SFX_CHARGED);
      puff((uint8_t)(EG_X(to)*16u+8u),(uint8_t)(EG_Y(to)*16u+8u),4u,ST_SPARK);
    }
  }
  queue_cell(c);
  queue_cell(beyond);
  if (crate && cell_type(beyond) == T_PIT) obstacle_noise(beyond);
  sfx(SFX_PUSH);
  overlay(HA_PUSH,12u);
  puff((uint8_t)(EG_X(c)*16u+5u),(uint8_t)(EG_Y(c)*16u+14u),2u,ST_PUFF);
  puff((uint8_t)(EG_X(c)*16u+11u),(uint8_t)(EG_Y(c)*16u+14u),2u,ST_PUFF);
  if (acts.pushes != 255u) acts.pushes++;
  narrator_event(NE_PUSH,crate);
  grav_t = 1u; /* let it drop promptly if nothing holds it */
}

static uint8_t built_cell(uint8_t x,uint8_t y) {
  uint8_t i;
  if(x>=EG_W || y>=EG_H)return 1u; /* ordinary grid bounds own exterior edges */
  i=EG_IDX(x,y);
  return (uint8_t)(look_here(i) || !(roam_hidden[i>>3]&look_bits[i&7u]));
}
static uint8_t built_box(int16_t x,int16_t y) {
  uint8_t x0=(uint8_t)((uint16_t)x>>8),y0=(uint8_t)((uint16_t)y>>8);
  uint8_t x1=(uint8_t)((uint16_t)(x+176)>>8),y1=(uint8_t)((uint16_t)(y+176)>>8);
  return (uint8_t)(built_cell(x0,y0) && (x1==x0 || built_cell(x1,y0)) &&
    (y1==y0 || (built_cell(x0,y1) && (x1==x0 || built_cell(x1,y1)))));
}
/* one mover step from the player's centre (psx, psy are the centre, the mover's box is 12x12 from its corner) */
static void mstep(uint8_t dpad, uint8_t speed, uint8_t synth) {
  uint8_t prev=0u,o0=0u,o1=0u,o2=0u,o3=0u;
  int16_t old_x=(int16_t)psx-96,old_y=(int16_t)psy-96;
  if(synth) {prev=pmv.prev;o0=pmv.order[0];o1=pmv.order[1];o2=pmv.order[2];o3=pmv.order[3];}
  if (map_door != door_open) door_map();
  pmv.x = old_x;
  pmv.y = old_y;
  pmv.mode = (opt_move4 || EG_OPT_MOVE4) && !synth ? EG_MODE_4WAY : EG_MODE_8WAY;
  eg_move_step(&pmv, dpad, speed, &pg);
  if(world_on) {
    /* An unloaded continuation is not an island edge. Keep the complete
     * body inside the active window until its prepared strip is published;
     * true exterior edges retain their ordinary fall/jump behavior. */
    if(world_view.x && pmv.x<288)pmv.x=288;
    if(world_view.x<world_view.width-EG_W && pmv.x>3616)pmv.x=3616;
    if(world_view.y && pmv.y<544)pmv.y=544;
    if(world_view.y<world_view.height-EG_H && pmv.y>3104)pmv.y=3104;
  }
  /* Presentation cannot become a gameplay hole. Test the entire destination
   * footprint after grid collision, preserving sliding on the ready axis.
   * Real pits, fragile failures and island edges never set roam_hidden. */
  if(!built_box(pmv.x,pmv.y)) {
    if(built_box(pmv.x,old_y))pmv.y=old_y;
    else if(built_box(old_x,pmv.y))pmv.x=old_x;
    else {pmv.x=old_x;pmv.y=old_y;}
  }
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
  if(push_t==1u) tutorial_queue(TE_PUSH);
  if(!pl_air) collect();
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
  narrator_event(NE_BREAK,0u);
  puff((uint8_t)(EG_X(i) * 16u + 8u), (uint8_t)(EG_Y(i) * 16u + 8u), 4, ST_PUFF);
  grav_t = 1u;
}

static void deflect(orb_t *o, uint8_t perfect) {
  if(perfect) narrator_event(NE_PARRY,0u);
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
                                           HS_HIT2, HS_HIT3, HS_CHARGED, HS_CHARGED, HS_PARRY, HS_ORB, HS_HIT1, HS_CHARGED};
  hit_src = spin && pin.move == EG_MV_CHARGED ? HS_SPIN : src[pin.move];
  hit_tags = 0;
}

typedef struct {int16_t x,y;uint8_t rx,ry;} strike_box;
static void strike_bounds(uint8_t spin,strike_box *b) {
  uint8_t width=14u,reach=14u;
  if(heir.weapon==1u) { reach=12u; }
  else if(heir.weapon==2u) { reach=24u; width=6u; }
  else if(heir.weapon==3u) { reach=10u; width=9u; }
  else if(heir.weapon==4u) { reach=20u; width=6u; }
  if(mods & MOD_LONG) reach+=4u;
  int8_t ix = f8x[pmv.facing], iy = f8y[pmv.facing];
  if (spin) {
    b->x = pl_x;
    b->y = pl_y;
    b->rx = b->ry = (mods & MOD_LONG) ? 28u : 24u;
  } else if (ix && iy) {
    reach = (uint8_t)(reach - (reach >> 2));
    b->x = (int16_t)pl_x + ix * (int8_t)reach;
    b->y = (int16_t)pl_y + iy * (int8_t)reach;
    b->rx = b->ry = (uint8_t)(width-1u);
  } else {
    b->x = (int16_t)pl_x + ix * (int8_t)reach;
    b->y = (int16_t)pl_y + iy * (int8_t)reach;
    b->rx = ix ? (uint8_t)(reach - 1u) : width;
    b->ry = ix ? width : (uint8_t)(reach - 1u);
  }
}
static void strike(uint8_t spin,uint8_t dmg) {
  strike_box b;int16_t cx,cy;uint8_t rx,ry,k,wide,cx8,cy8;
  strike_bounds(spin,&b);cx=b.x;cy=b.y;rx=b.rx;ry=b.ry;
  if(heir.weapon==1u)dmg++;
  if(pin.move==EG_MV_CHARGED && family_stat[G_POWER]>=3u)dmg++;
  hit_from_move(spin);
  /* Resolve the exceptional outside-map centre once per attack. Ordinary
   * combat keeps the byte overlap path instead of rechecking it per target. */
  wide=(uint8_t)((uint16_t)cx>255u || (uint16_t)cy>255u);
  cx8=(uint8_t)cx;cy8=(uint8_t)cy;
  obstacle_hit(cx, cy, rx, ry);
  for (k = 0; k != nen; k++) {
    uint8_t r2 = en[k].kind == SPAWN_BOSS ? 14u : 0u;
    if (en[k].st == ES_DEAD || (hit_mask & (uint8_t)(1u << k))) continue;
    if (wide ? overlap_world(cx, cy, (uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4), (uint8_t)(rx + r2), (uint8_t)(ry + r2)) :
               overlap(cx8, cy8, (uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4), (uint8_t)(rx + r2), (uint8_t)(ry + r2))) {
      hit_mask |= (uint8_t)(1u << k);
      fight_t = 180u;
      enemy_hit(k, dmg, pl_x, pl_y);
    }
  }
  for (k = 0; k != MAXO; k++) {
    if (!orbs[k].live || orbs[k].friendly) continue;
    if (wide ? overlap_world(cx, cy, (uint8_t)(orbs[k].x >> 4), (uint8_t)(orbs[k].y >> 4), (uint8_t)(rx + 2u), (uint8_t)(ry + 2u)) :
               overlap(cx8, cy8, (uint8_t)(orbs[k].x >> 4), (uint8_t)(orbs[k].y >> 4), (uint8_t)(rx + 2u), (uint8_t)(ry + 2u)))
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

/* Attack the complete tile rectangle touched by the visible arc. A single
 * point at centre+12 misses the lower side and diagonal corners of a wall. */
static void strike_bricks(uint8_t spin,uint8_t force) {
  strike_box b;int16_t l,r,t,d;uint8_t x,y;
  strike_bounds(spin,&b);l=b.x-b.rx;r=b.x+b.rx-1;t=b.y-b.ry;d=b.y+b.ry-1;
  if(r<0 || d<0 || l>=256 || t>=224)return;
  if(l<0)l=0;if(t<0)t=0;if(r>255)r=255;if(d>223)d=223;
  for(y=(uint8_t)(t>>4);y<=(uint8_t)(d>>4);y++)
    for(x=(uint8_t)(l>>4);x<=(uint8_t)(r>>4);x++)break_brick(EG_IDX(x,y),force);
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

static uint8_t walk_speed(void) { return (uint8_t)(((heir.traits & HT_HEAVY) ? 18u : 20u) + family_stat[G_PACE]); }

static void on_start(uint8_t mv) {
  if(mv==EG_MV_HIT1) tutorial_queue(TE_A);
  else if(mv==EG_MV_BCHARGE) tutorial_queue(TE_B);
  else if(mv==EG_MV_DODGE) tutorial_queue(TE_EVADE);
  if((mv>=EG_MV_HIT1 && mv<=EG_MV_HIT3) || mv==EG_MV_SHOT || mv==EG_MV_CHARGED) narrator_event(NE_ATTACK,mv);
  uint8_t d = dpad_now();
  pl_arc = 0;
  hit_mask = 0;
  heir_idle_stop();
  ov_t = 0;
  switch (mv) {
    case EG_MV_DODGE:
      dodge_dp = f8dp[pin.dash_dir];
      pmv.facing = pin.evade==EG_EVADE_FORWARD ? pin.dash_dir : pin.tap_face;
      roll_dx = f8x[pin.dash_dir];
      roll_dy = f8y[pin.dash_dir];
      pl_charge = 0;
      hud_charge();
      sfx(SFX_ROLL);
      puff(pl_x, (uint8_t)(pl_y + 4u), 2, ST_PUFF);
      heir_play(pin.evade ? HA_DODGE_SLIDE : (pin.style == EG_DODGE_BLINK ? HA_DODGE_BLINK : (pin.style == EG_DODGE_SLIDE ? HA_DODGE_SLIDE
                                                                                            : HA_DODGE_ROLL)), 1);
      break;
    case EG_MV_SHOT:
    case EG_MV_BPOWER:
      if(d) pmv.facing=face_of_dp(d);
      heir_play(HA_ATTACK,1u);
      break;
    case EG_MV_BCHARGE: heir_play(HA_CHARGE,1u);break;
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
      if(family_stat[G_FOCUS]>=3u && pl_level<3u) pl_level++;
      if (pl_level >= 2u && heir.weapon != 4u) {
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
  if(pin.evade) return ph==EG_PH_ACTIVE ? (pin.evade==EG_EVADE_SIDE ? 36u : 30u) : 8u;
  if (pin.style == EG_DODGE_BLINK) return ph == EG_PH_ACTIVE ? 104u : (ph == EG_PH_STARTUP ? 8u : 0u);
  if (pin.style == EG_DODGE_SLIDE) return ph == EG_PH_ACTIVE ? 36u : 12u;
  return ph == EG_PH_ACTIVE ? 44u : (ph == EG_PH_STARTUP ? 24u : 16u);
}

static void secondary(uint8_t level) {
  uint8_t k;
  if(heir.weapon==1u || heir.weapon==3u) {
    hit_mask=0u;strike(1u,level ? (uint8_t)(1u+level) : 1u);pl_arc=2u;
    puff(pl_x,pl_y,4u,ST_SPARK);sfx(SFX_THUD);
  } else {
    for(k=0u;k!=MAXO;k++) if(!orbs[k].live) break;
    if(k==MAXO) return;
    fire_orb_at(pl_x,pl_y,f8x[pmv.facing]*32,f8y[pmv.facing]*32,heir.weapon==4u ? 44u : 36u);
    orbs[k].friendly=(uint8_t)(1u+level);orbs[k].pad=0u;orbs[k].life=level ? (uint8_t)(48u+level*16u) : (heir.weapon==4u ? 52u : 36u);
    if(level) {sfx(SFX_CHARGED);puff(pl_x,pl_y,5u,ST_SPARK);}
    puff(pl_x,pl_y,4u,ST_SPARK);sfx(SFX_SHOT);
  }
  heir_play(HA_ATTACK,1u);
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
  portal_lock=EG_LINK_NONE;
  pl_z=0;pl_air=air_void=0u;
  grid_map(); /* here, not on the first step: 256 entries cost most of a frame */
  pg.cells = room.c;
  pg.w = EG_W;
  pg.h = EG_H;
  pg.wshift = 4u;
  pg.map = pmap;
  eg_input_init(&pin, heir.dodge);
  pin.weapon = heir.weapon;
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
void player_shift_view(int8_t dx,int8_t dy) BANKED {
  uint8_t c;
  int16_t sx=(int16_t)dx*256,sy=(int16_t)dy*256;
  psx=(uint16_t)(psx-sx);psy=(uint16_t)(psy-sy);pmv.x-=sx;pmv.y-=sy;
  pl_x=(uint8_t)(pl_x-dx*16);pl_y=(uint8_t)(pl_y-dy*16);
  portal_lock=eg_view_rebase(portal_lock,dx,dy);fall_safe=eg_view_rebase(fall_safe,dx,dy);
  if(fall_safe>=EG_N)fall_safe=cell_at(pl_x,pl_y);
  c=eg_view_rebase(EG_IDX(pmv.push_cx,pmv.push_cy),dx,dy);
  if(c<EG_N) {pmv.push_cx=EG_X(c);pmv.push_cy=EG_Y(c);}else pmv.push_t=0u;
  c=eg_view_rebase(EG_IDX(pmv.req_cx,pmv.req_cy),dx,dy);
  if(c<EG_N) {pmv.req_cx=EG_X(c);pmv.req_cy=EG_Y(c);}else pmv.req=0u;
  /* Contact may have reached its single request while the destination was
   * outside the old window. Recheck it once against the newly loaded space. */
  if(pmv.push_t>=EG_PUSH_HOLD)pmv.push_t=EG_PUSH_HOLD-1u;
  push_t=pmv.push_t;
}

/* Reserve the transport frame before speculative world binding. The mover can
 * enter the centre this tick, so testing only the current centre is too late.
 * Arrival locks and unpowered pads must keep their normal prefetch work. */
uint8_t player_portal_pending(void) BANKED {
  uint8_t k,c,to;
  if(!(room.portal_need&EG_PORTAL_ON) || room.portal[0]>=EG_N || room.portal[1]>=EG_N ||
     pl_air || pl_state==PS_FALL || world_moved)return 0u;
  for(k=0u;k<2u;k++) {
    c=room.portal[k];
    if(c==portal_lock || absdiff(pl_x,(uint8_t)(EG_X(c)*16u+8u))>10u ||
       absdiff(pl_y,(uint8_t)(EG_Y(c)*16u+8u))>10u)continue;
    if(!(joy&(J_UP|J_DOWN|J_LEFT|J_RIGHT)) && pl_state!=PS_ROLL &&
       ((pl_x&15u)<5u || (pl_x&15u)>11u || (pl_y&15u)<5u || (pl_y&15u)>11u))continue;
    to=eg_portal_target(&room,c,&c,1u);
    if(to!=EG_LINK_NONE && !enemy_in_cell(to))return 1u;
  }
  return 0u;
}
static void portal_tick(void) {
  uint8_t c=cell_at(pl_x,pl_y),to,x,y;
  /* First publish the complete incoming view; transport can activate on the
   * following tick. Walking, attacks and physics keep their ordinary update. */
  if(world_on && world_moved)return;
  if(c!=portal_lock) portal_lock=EG_LINK_NONE;
  if(c==portal_lock || pl_air || pl_state==PS_FALL) return;
  if(c!=room.portal[0] && c!=room.portal[1]) return;
  /* The complete feet box crosses before the transfer, avoiding activation
   * from a corner brushed while walking alongside the device. */
  if((pl_x&15u)<5u || (pl_x&15u)>11u || (pl_y&15u)<5u || (pl_y&15u)>11u) return;
  to=eg_portal_target(&room,c,&c,1u);
  if(to==EG_LINK_NONE) {tutorial_queue(TE_PORTAL);return;}
  if(enemy_in_cell(to)) return;
  x=(uint8_t)(EG_X(to)*16u+8u);y=(uint8_t)(EG_Y(to)*16u+8u);
  if(!box_free(x,y)) return;
  eg_leave(&room,c);queue_cell(c);
  psx=cell_sub(EG_X(to));psy=cell_sub(EG_Y(to));pl_x=x;pl_y=y;
  pmv.x=(int16_t)psx-96;pmv.y=(int16_t)psy-96;
  portal_lock=to;fall_safe=to;puzzle_dirty=1u;grav_t=1u;
  if(world_on)play_camera_warp();
  look_anchor(to);sfx(SFX_CHARGED);overlay(HA_SPAWN,12u);
  puff(x,y,4u,ST_SPARK);collect();
}

void player_update(void) BANKED {
  uint8_t st, mv, prev = pin.move, ph, dmg, was_air=pl_air;
  eg_frames_t fd;
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
    case PS_FALL: fall_tick();return;
    case PS_HURT: { /* knocked back; the resolver starts clean after */
      uint8_t kd = (uint8_t)((kb_dx > 0 ? J_RIGHT : (kb_dx < 0 ? J_LEFT : 0u)) |
                             (kb_dy > 0 ? J_DOWN : (kb_dy < 0 ? J_UP : 0u)));
      uint8_t f = pmv.facing;
      if (kd) mstep(kd, 32u, 1);
      pmv.facing = f;
      if (!--pl_t) {
        pl_state = PS_FREE;
        eg_input_init(&pin, heir.dodge);
        pin.weapon = heir.weapon;
      }
      ha_dir = (uint8_t)((4u - pmv.facing) & 7u);
      return;
    }
    default: break;
  }
  pin.facing=pmv.facing;
  st = eg_input_step(&pin, (uint8_t)(joy & (J_A | J_B | 0x0fu)));
  mv = pin.move;
  pl_air=(uint8_t)(mv==EG_MV_DODGE && pin.evade==EG_EVADE_FORWARD);
  pl_z=pl_air ? (int8_t)jump_z[pin.frame<20u ? pin.frame : 19u] : 0;
  if (prev == EG_MV_DODGE && mv != EG_MV_DODGE && mv != EG_MV_SPRINT && (mods & MOD_FUSE)) {
    hit_mask = 0; /* the dodge ends in a static shock around the player */
    strike(1, 1u);
    puff(pl_x, pl_y, 4, ST_SPARK);
  }
  if(st!=EG_NO_MOVE && !stamina_move(st,pin.evade,pin.level)) {
    eg_input_init(&pin,heir.dodge);pin.weapon=heir.weapon;pin.prev=joy;st=EG_NO_MOVE;mv=EG_MV_IDLE;pl_air=0u;pl_z=0;
    overlay(HA_RECOVER,10u);
  }
  if(st!=EG_NO_MOVE) on_start(st);
  stamina_tick((uint8_t)(mv!=EG_MV_IDLE && mv!=EG_MV_SKID));
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
      pmv.facing=pin.evade==EG_EVADE_FORWARD ? pin.dash_dir : pin.tap_face;
      cardinal_face();
      break;
    case EG_MV_BCHARGE:
      walk((uint8_t)(walk_speed()>>1),0u);pl_charge=pin.b_hold;heir_play(HA_CHARGE,0u);
      if((pl_charge&3u)==0u) hud_charge();
      break;
    case EG_MV_SHOT:
    case EG_MV_BPOWER:
      pl_state=PS_LASH;
      walk((uint8_t)(walk_speed()+(pin.gait ? 8u : 0u)),0u);
      if(pin.frame==(mv==EG_MV_SHOT ? 4u : 5u)) secondary(mv==EG_MV_BPOWER ? pin.level : 0u);
      else if(ha_done) heir_play(HA_CTX_READY,0u);
      break;
    case EG_MV_SPRINT: {
      uint8_t d = dpad_now(), sp = (heir.traits & HT_HEAVY) ? 25u : 28u;
      if (d) dodge_dp = d;
      if(!(frame_count&15u) && !stamina_move(EG_MV_SPRINT,0u,0u)) {pin.move=EG_MV_IDLE;pin.dp_hold=pin.gait=0u;sp=walk_speed();}
      pl_state = PS_ROLL;
      /* Running into a movable object becomes a sustained brace. Sprinting
       * must preserve contact time; synthetic evade steps deliberately don't. */
      mstep(dodge_dp,sp,0);
      if(push_t) {pin.move=EG_MV_IDLE;pin.dp_hold=pin.gait=0u;heir_play(HA_PUSH,0u);}
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
      eg_input_attack_frames(&pin, &fd);
      if (pin.frame == 2u) mstep(f8dp[pmv.facing], 24u, 1); /* step into it */
      if (ph == EG_PH_ACTIVE) {
        dmg = mv == EG_MV_HIT3 ? ((mods & MOD_SURGE) ? 3u : 2u) : 1u;
        strike(0, dmg);
        if (pin.frame == fd.startup) strike_bricks(0u,(uint8_t)(mv == EG_MV_HIT3 || heir.weapon == 1u));
      }
      if (pin.frame >= 2u && pin.frame < (uint8_t)(fd.startup + fd.active + 3u)) pl_arc = 1;
      break;
    case EG_MV_CHARGED:
      eg_input_attack_frames(&pin, &fd);
      if (heir.weapon == 4u) {
        pl_state=PS_LASH;
        if(pin.frame==fd.startup) {
          uint8_t k;
          for(k=0u;k!=MAXO;k++) if(!orbs[k].live) break;
          if(k!=MAXO) {
            fire_orb_at(pl_x,pl_y,f8x[pmv.facing]*32,f8y[pmv.facing]*32,40u);
            orbs[k].friendly=1u; orbs[k].pad=0u; orbs[k].life=(uint8_t)(24u+pl_level*12u);
            puff(pl_x,pl_y,4u,ST_SPARK); sfx(SFX_SHOT);
          }
        }
        if(ph==EG_PH_ACTIVE) pl_arc=1u;
        break;
      }
      if (pl_level >= 2u) {
        pl_state = PS_SPIN;
        if (ph == EG_PH_ACTIVE || (ph == EG_PH_RECOVERY && pin.frame < 14u)) {
          strike(1, (uint8_t)(((mods & MOD_SURGE) ? 4u : 3u) + (pl_level == 3u ? 1u : 0u)));
          if (pin.frame == 3u) {
            strike_bricks(1u,1u);
            shake = 4u;
          }
        }
        if (pin.frame <= 16u) pl_arc = 2;
      } else {
        pl_state = PS_LASH;
        if (ph == EG_PH_ACTIVE) {
          strike(0, 2u);
          if (pin.frame == 3u) strike_bricks(0u,1u);
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
  if (mv != EG_MV_CHARGE && mv!=EG_MV_BCHARGE && pl_charge) {
    pl_charge = 0;
    hud_charge();
  }
  ha_dir = (uint8_t)((4u - pmv.facing) & 7u);
  if(room.portal_need&EG_PORTAL_ON) portal_tick();
  st=cell_type(cell_at(pl_x,pl_y));
  if(pl_air && (st==T_PIT || (st==T_GLITCH && !eg_beat_on(frame_count)))) air_void=1u;
  if(was_air && !pl_air) {
    if(air_void && EG_PASSABLE(st) && (st!=T_GLITCH || eg_beat_on(frame_count))) narrator_event(NE_JUMP,0u);
    air_void=0u;
  }
}
