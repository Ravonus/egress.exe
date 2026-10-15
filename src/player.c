/* The player and the lead. Free movement against the grid (corner assist; lean on a CRT to push it), digging and
 * collecting by entering cells, and the weapon: tap A to lash (three-hit combo), keep holding to charge and release
 * for a spin that breaks drywall, B to dodge-roll (invulnerable for most of it). Mods change the lead. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "puzzle.h"

uint8_t pl_x, pl_y, pl_hp, pl_hp_max, pl_state, pl_face;
uint8_t pl_t, pl_inv, pl_combo, pl_queue, pl_charge, roll_cd, push_t, hit_mask, walk_t, combo_gap;
int8_t roll_dx, roll_dy, kb_dx, kb_dy;
uint16_t psx, psy;
floor_acts_t acts;

void player_hurt(uint8_t n, uint8_t fromx, uint8_t fromy) BANKED {
  if (pl_inv || pl_state == PS_EXIT || !pl_hp) return;
  if (pl_state == PS_ROLL && pl_t > 4u) return;
  if (!dbg_god) pl_hp = pl_hp > n ? (uint8_t)(pl_hp - n) : 0u;
  if (acts.hits_taken != 255u) acts.hits_taken++;
  pl_inv = 60u;
  pl_state = PS_HURT;
  pl_t = 8u;
  pl_charge = 0;
  kb_dx = fromx < pl_x ? 2 : (fromx > pl_x ? -2 : 0);
  kb_dy = fromy < pl_y ? 2 : (fromy > pl_y ? -2 : 0);
  freeze = 4u;
  shake = 8u;
  sfx(SFX_HURT);
  hud_dirty = 1;
}

void open_door(void) BANKED {
  door_open = 1;
  queue_cell(room.door);
  sfx(SFX_DOOR);
  hud_msg("THE DOOR HUMS");
}

static void collect(void) {
  uint8_t i = cell_at(pl_x, pl_y), t = cell_type(i);
  if (t == T_DIRT) {
    room.c[i] = T_EMPTY;
    queue_cell(i);
    sfx(SFX_DIG);
    if (acts.dug != 255u) acts.dug++;
    puff((uint8_t)(EG_X(i) * 16u + 8u), (uint8_t)(EG_Y(i) * 16u + 8u), 2, ST_PUFF);
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
    if (keys_held == room.keys) open_door();
    else hud_msg("KEYCARD");
  } else if (t == T_ITEM)
    puzzle_pick(i); /* B1: a piece into the pocket */
}

static void try_push(int8_t dir) {
  uint8_t ahead = cell_at((uint8_t)(dir > 0 ? pl_x + 6u : pl_x - 7u), pl_y), beyond, cy;
  if (cell_type(ahead) != T_ROCK || (room.c[ahead] & F_FALL)) {
    push_t = 0;
    return;
  }
  cy = (uint8_t)(EG_Y(ahead) * 16u + 8u);
  if (absdiff(pl_y, cy) > 5u) return;
  if (pl_y < cy) psy += 8u; /* lean into it: settle onto its row */
  else if (pl_y > cy) psy -= 8u;
  beyond = (uint8_t)(dir > 0 ? ahead + 1u : ahead - 1u);
  if (++push_t < 10u) return;
  push_t = 0;
  if (enemy_in_cell(beyond) || !eg_push(&room, ahead, beyond)) { /* B1: the shared rule (plates and pits too) */
    sfx(SFX_DENY);
    return;
  }
  queue_cell(ahead);
  queue_cell(beyond);
  sfx(SFX_PUSH);
  if (acts.pushes != 255u) acts.pushes++;
  grav_t = 1u; /* let it drop promptly if nothing holds it */
}

/* move by (dx, dy) subpixels with collision, corner assist and pushing */
static void player_move(int16_t dx, int16_t dy, uint8_t can_push) {
  uint16_t n;
  uint8_t x, y, k;
  if (dx) {
    n = (uint16_t)(psx + dx);
    x = (uint8_t)(n >> 4);
    y = (uint8_t)(psy >> 4);
    if (box_free(x, y)) {
      psx = n;
      push_t = 0;
    } else {
      if (can_push && !dy) try_push(dx > 0 ? 1 : -1);
      for (k = 1; k != 7u; k++) { /* corner assist */
        if (box_free(x, (uint8_t)(y - k))) {
          psy -= 16u;
          break;
        }
        if (box_free(x, (uint8_t)(y + k))) {
          psy += 16u;
          break;
        }
      }
    }
  }
  if (dy) {
    n = (uint16_t)(psy + dy);
    x = (uint8_t)(psx >> 4);
    y = (uint8_t)(n >> 4);
    if (box_free(x, y))
      psy = n;
    else
      for (k = 1; k != 7u; k++) {
        if (box_free((uint8_t)(x - k), y)) {
          psx -= 16u;
          break;
        }
        if (box_free((uint8_t)(x + k), y)) {
          psx += 16u;
          break;
        }
      }
  }
  pl_x = (uint8_t)(psx >> 4);
  pl_y = (uint8_t)(psy >> 4);
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

/* the lash's reach: a box in front; the spin's: around the player */
static void strike(uint8_t spin, uint8_t dmg) {
  uint8_t cx, cy, rx, ry, k, reach = (mods & MOD_LONG) ? 18u : 14u;
  if (spin) {
    cx = pl_x;
    cy = pl_y;
    rx = ry = (mods & MOD_LONG) ? 28u : 24u;
  } else {
    cx = (uint8_t)(pl_x + fdx[pl_face] * (int8_t)reach);
    cy = (uint8_t)(pl_y + fdy[pl_face] * (int8_t)reach);
    rx = fdx[pl_face] ? (uint8_t)(reach - 1u) : 14u;
    ry = fdx[pl_face] ? 14u : (uint8_t)(reach - 1u);
  }
  for (k = 0; k != nen; k++) {
    uint8_t r2 = en[k].kind == SPAWN_BOSS ? 14u : 0u;
    if (en[k].st == ES_DEAD || (hit_mask & (uint8_t)(1u << k))) continue;
    if (overlap(cx, cy, (uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4), (uint8_t)(rx + r2), (uint8_t)(ry + r2))) {
      hit_mask |= (uint8_t)(1u << k);
      enemy_hit(k, dmg, pl_x, pl_y);
    }
  }
  for (k = 0; k != MAXO; k++) {
    if (!orbs[k].live || orbs[k].friendly) continue;
    if (overlap(cx, cy, (uint8_t)(orbs[k].x >> 4), (uint8_t)(orbs[k].y >> 4), (uint8_t)(rx + 2u), (uint8_t)(ry + 2u))) {
      /* deflect: send it back faster; now it hurts them */
      orbs[k].friendly = 1;
      orbs[k].vx = (int8_t)(-orbs[k].vx - (orbs[k].vx >> 1));
      orbs[k].vy = (int8_t)(-orbs[k].vy - (orbs[k].vy >> 1));
      orbs[k].life = 90u;
      if (acts.deflects != 255u) acts.deflects++;
      sfx(SFX_HIT);
      freeze = 2u;
    }
  }
}

static void start_lash(void) {
  pl_state = PS_LASH;
  pl_t = 0;
  hit_mask = 0;
  pl_queue = 0;
  sfx(SFX_LASH);
}

static uint8_t input_dir(int8_t *ix, int8_t *iy) {
  *ix = (joy & J_LEFT) ? -1 : ((joy & J_RIGHT) ? 1 : 0);
  *iy = (joy & J_UP) ? -1 : ((joy & J_DOWN) ? 1 : 0);
  return (uint8_t)(*ix || *iy);
}

static void face_from(int8_t ix, int8_t iy) {
  if (ix && iy) { /* on a diagonal, keep the facing if it is one of its two axes */
    if ((pl_face == F_LEFT && ix < 0) || (pl_face == F_RIGHT && ix > 0) || (pl_face == F_UP && iy < 0) ||
        (pl_face == F_DOWN && iy > 0))
      return;
  }
  if (ix) pl_face = ix < 0 ? F_LEFT : F_RIGHT;
  else if (iy) pl_face = iy < 0 ? F_UP : F_DOWN;
}

static void start_roll(int8_t ix, int8_t iy) {
  if (!ix && !iy) {
    ix = fdx[pl_face];
    iy = fdy[pl_face];
  }
  roll_dx = ix;
  roll_dy = iy;
  pl_state = PS_ROLL;
  pl_t = 16u;
  pl_charge = 0;
  sfx(SFX_ROLL);
  puff(pl_x, (uint8_t)(pl_y + 4u), 2, ST_PUFF);
}

static void spin_bricks(void) {
  uint8_t c = cell_at(pl_x, pl_y);
  break_brick((uint8_t)(c - EG_W), 1);
  break_brick((uint8_t)(c + EG_W), 1);
  break_brick((uint8_t)(c - 1u), 1);
  break_brick((uint8_t)(c + 1u), 1);
}

void player_update(void) BANKED {
  int8_t ix, iy;
  uint8_t moving = input_dir(&ix, &iy), full = CHARGE_FULL;
  int16_t sp;
  if (pl_inv) pl_inv--;
  if (roll_cd) roll_cd--;
  if (combo_gap && !--combo_gap) pl_combo = 0;
  /* charging: keep holding A after a lash */
  if (joy & J_A) {
    if (pl_charge && pl_charge < 250u) {
      pl_charge++;
      if (pl_charge == full) {
        sfx(SFX_CHARGED);
        puff(pl_x, (uint8_t)(pl_y - 12u), 4, ST_SPARK);
      }
      if ((pl_charge & 3u) == 0u) hud_charge();
    }
  } else if (pl_charge) {
    if (pl_charge >= full && (pl_state == PS_FREE || (pl_state == PS_LASH && pl_t >= 8u))) {
      pl_state = PS_SPIN;
      pl_t = 0;
      hit_mask = 0;
      sfx(SFX_SPIN);
    }
    pl_charge = 0;
    hud_charge();
  }
  switch (pl_state) {
    case PS_HURT:
      player_move((int16_t)kb_dx * 16, (int16_t)kb_dy * 16, 0);
      if (!--pl_t) pl_state = PS_FREE;
      return;
    case PS_ROLL:
      sp = pl_t > 8u ? 46 : 26;
      player_move(roll_dx * sp, roll_dy * sp, 0);
      if (!--pl_t) {
        pl_state = PS_FREE;
        roll_cd = 8u;
        if (mods & MOD_FUSE) { /* the roll ends in a static shock around the player */
          hit_mask = 0;
          strike(1, 1u);
          puff(pl_x, pl_y, 4, ST_SPARK);
        }
      }
      return;
    case PS_LASH:
      pl_t++;
      if (pl_t == 2u) player_move(fdx[pl_face] * 24, fdy[pl_face] * 24, 0); /* step into it */
      if (pl_t >= 3u && pl_t <= 7u)
        strike(0, pl_combo == 2u ? ((mods & MOD_SURGE) ? 3u : 2u) : 1u);
      if (pl_t == 4u) {
        uint8_t c = cell_at((uint8_t)(pl_x + fdx[pl_face] * 12), (uint8_t)(pl_y + fdy[pl_face] * 12));
        break_brick(c, pl_combo == 2u);
      }
      if (pl_t >= 6u && (joy_pressed & J_A)) pl_queue = 1;
      if (pl_t >= 8u && (joy_pressed & J_B) && !roll_cd) {
        start_roll(ix, iy);
        return;
      }
      if (pl_t >= 13u) {
        if (pl_queue && pl_combo < 2u) {
          pl_combo++;
          if (moving) face_from(ix, iy);
          start_lash();
        } else {
          pl_state = PS_FREE;
          combo_gap = 16u;
          pl_combo = pl_queue ? 0u : (uint8_t)(pl_combo + 1u);
          if (pl_combo > 2u) pl_combo = 0;
        }
      }
      return;
    case PS_SPIN:
      pl_t++;
      if (pl_t >= 2u && pl_t <= 14u) strike(1, (mods & MOD_SURGE) ? 4u : 3u);
      if (pl_t == 3u) {
        spin_bricks();
        shake = 4u;
      }
      if (pl_t >= 20u) pl_state = PS_FREE;
      return;
    case PS_EXIT:
      return;
    default:
      break;
  }
  if (moving) {
    face_from(ix, iy);
    sp = (ix && iy) ? 14 : 20;
    if (pl_charge) sp >>= 1;
    player_move(ix * sp, iy * sp, 1);
    walk_t++;
    if ((walk_t & 15u) == 0u) sfx(SFX_STEP);
  } else {
    walk_t = 0;
    push_t = 0;
  }
  if ((joy_pressed & J_B) && !roll_cd) {
    start_roll(ix, iy);
    return;
  }
  if (joy_pressed & J_A) {
    if (!combo_gap) pl_combo = 0;
    start_lash();
    pl_charge = 1;
  }
}
