/* The floor's enemies and their orbs.
 *
 * Static mite: walks the open carpet cell to cell, chases when near. When it shares a clear row or column with the
 * player it tells (shakes and flashes for 26 frames), then lunges in a straight line and rests, open to hits.
 * WATCH eye: floats over anything but concrete, keeps its distance, tells (the iris dilates, 40 frames) and fires an
 * orb. The lead deflects orbs back at them. A falling CRT crushes anything under it. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "boss_api.h" /* B2 bosses */
#include "heir.h"
#include "mob.h"
#include "core/eg_move.h"

enemy_t en[MAXE];
uint8_t nen;
orb_t orbs[MAXO];
uint8_t enemies_alive, kills_run;

/* B2: the office's matrix mobs take the room's spawns (src/mob.c); the B1 mite and eye stay for boss.c's summons */
void actors_spawn(void) BANKED { mobs_floor(); }

void enemy_kill(uint8_t k, uint8_t crushed) BANKED {
  enemy_t *e = &en[k];
  e->st = ES_DEAD;
  if (e->kind == SPAWN_MOB) mob_died(k); /* B2 */
  puff((uint8_t)(e->x >> 4), (uint8_t)(e->y >> 4), e->kind == SPAWN_MITE ? 2u : 3u, ST_PUFF);
  puff((uint8_t)(e->x >> 4), (uint8_t)((e->y >> 4) - 10u), 5, ST_SPARK);
  if (tokens < 250u) tokens = (uint8_t)(tokens + (crushed ? 2u : 1u) + (e->kind == SPAWN_BOSS ? 4u : 0u));
  if (e->kind == SPAWN_MITE || e->kind == SPAWN_MOB) {
    if (acts.mites != 255u) acts.mites++;
  } else if (acts.eyes != 255u)
    acts.eyes++;
  if (kills_run != 255u) kills_run++;
  if ((mods & MOD_SPLICE) && kills_run % 3u == 0u && pl_hp && pl_hp < pl_hp_max) pl_hp++;
  hud_dirty = 1;
  sfx(SFX_KILL);
  if (enemies_alive) enemies_alive--;
  if (e->kind == SPAWN_BOSS) {
    shake = 30u;
    freeze = 20u;
    open_door();
    hud_msg("IT BLINKS OUT.");
    /* its static goes with it */
    for (k = 1; k != nen; k++)
      if (en[k].st != ES_DEAD) {
        en[k].st = ES_DEAD;
        puff((uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4), 2, ST_PUFF);
        if (enemies_alive) enemies_alive--;
      }
  }
}

void enemy_hit(uint8_t k, uint8_t dmg, uint8_t fromx, uint8_t fromy) BANKED {
  enemy_t *e = &en[k];
  uint8_t ex = (uint8_t)(e->x >> 4), ey = (uint8_t)(e->y >> 4);
  if (e->kind == SPAWN_MOB) { /* B2: armour, conditions, reactions, knockback, hitstop */
    mob_hit(k, dmg, fromx, fromy);
    return;
  }
  if (e->kind == SPAWN_BOSS && bs_kind) { /* B2 bosses: they keep their own count */
    boss_hit(dmg, fromx, fromy);
    return;
  }
  e->flash = 8u;
  freeze = dmg >= 3u ? 5u : 3u;
  sfx(SFX_HIT);
  if (e->hp <= dmg) {
    e->hp = 0;
    enemy_kill(k, 0);
    return;
  }
  e->hp = (uint8_t)(e->hp - dmg);
  if (e->kind == SPAWN_BOSS) return; /* the boss does not flinch; it flashes */
  e->vx = ex > fromx ? 40 : (ex < fromx ? -40 : 0);
  e->vy = ey > fromy ? 40 : (ey < fromy ? -40 : 0);
  if (absdiff(ex, fromx) > absdiff(ey, fromy)) e->vy = 0;
  else e->vx = 0;
  e->st = ES_HURT;
  e->t = 10u;
}

/* fire an orb from (ex, ey) toward (tx, ty) at `speed` subpixels a frame */
void fire_orb_at(uint8_t ex, uint8_t ey, int16_t dx, int16_t dy, uint8_t speed) BANKED {
  uint8_t k;
  orb_t *o;
  for (k = 0; k != MAXO; k++)
    if (!orbs[k].live) break;
  if (k == MAXO) return;
  o=&orbs[k];
  o->live=1u;o->friendly=0u;
  o->pad=(uint8_t)(orb_shooter+1u);
  o->life=200u;o->x=(uint16_t)ex<<4;o->y=(uint16_t)ey<<4;
  eg_aim(dx,dy,speed,&o->vx,&o->vy);
}

/* a straight, open line of carpet between two cells? */
static uint8_t clear_line(uint8_t a, uint8_t b, uint8_t step) {
  while (a != b) {
    a = (uint8_t)(a + step);
    if (room.c[a] != T_EMPTY) return 0;
  }
  return 1u;
}

static void mite_pick(enemy_t *e) {
  uint8_t ex = (uint8_t)(e->x >> 4), ey = (uint8_t)(e->y >> 4), c = cell_at(ex, ey), d, best = 255u, bd = 255u, n, k;
  uint8_t chase = (uint8_t)(absdiff(ex, pl_x) + absdiff(ey, pl_y) < 112u && (eg_rnd() & 3u));
  for (k = 0; k != 4u; k++) {
    d = (uint8_t)((k + (eg_rnd() & 3u)) & 3u);
    n = (uint8_t)(c + (int8_t)(fdy[d] * 16 + fdx[d]));
    if (room.c[n] != T_EMPTY) continue;
    if (chase) {
      uint8_t nx = (uint8_t)(EG_X(n) * 16u + 8u), ny = (uint8_t)(EG_Y(n) * 16u + 8u);
      uint8_t dist = (uint8_t)(absdiff(nx, pl_x) + absdiff(ny, pl_y));
      if (dist < bd) {
        bd = dist;
        best = d;
      }
    } else if ((d ^ 1u) != e->face || best == 255u) {
      best = d;
      if ((d ^ 1u) != e->face) break;
    }
  }
  if (best == 255u) {
    e->tx = (uint8_t)(EG_X(c) * 16u + 8u);
    e->ty = (uint8_t)(EG_Y(c) * 16u + 8u);
    return;
  }
  e->face = best;
  n = (uint8_t)(c + (int8_t)(fdy[best] * 16 + fdx[best]));
  e->tx = (uint8_t)(EG_X(n) * 16u + 8u);
  e->ty = (uint8_t)(EG_Y(n) * 16u + 8u);
}

static void approach(uint16_t *v, uint8_t target, uint8_t speed) {
  uint16_t t = (uint16_t)target << 4;
  if (*v < t) *v = (uint16_t)(*v + speed) > t ? t : (uint16_t)(*v + speed);
  else if (*v > t) *v = (uint16_t)(*v - speed) < t ? t : (uint16_t)(*v - speed);
}

static void tell(enemy_t *e, uint8_t frames) {
  e->st = ES_TELL;
  e->t = frames;
  sfx(SFX_TELL);
}

static void mite_update(enemy_t *e) {
  uint8_t ex = (uint8_t)(e->x >> 4), ey = (uint8_t)(e->y >> 4), nx, ny;
  switch (e->st) {
    case ES_WALK:
      approach(&e->x, e->tx, 12u);
      approach(&e->y, e->ty, 12u);
      if (ex == e->tx && ey == e->ty) {
        uint8_t c = cell_at(ex, ey), p = cell_at(pl_x, pl_y);
        if (!e->cd && EG_Y(c) == EG_Y(p) && absdiff(ex, pl_x) < 72u && clear_line(c, p, ex < pl_x ? 1u : 255u)) {
          e->face = ex < pl_x ? F_RIGHT : F_LEFT;
          tell(e, 26u);
        } else if (!e->cd && EG_X(c) == EG_X(p) && absdiff(ey, pl_y) < 72u &&
                   clear_line(c, p, ey < pl_y ? 16u : 240u)) {
          e->face = ey < pl_y ? F_DOWN : F_UP;
          tell(e, 26u);
        } else
          mite_pick(e);
      }
      if (e->cd) e->cd--;
      break;
    case ES_TELL:
      if (!--e->t) {
        e->st = ES_LUNGE;
        e->t = 22u;
        e->vx = (int8_t)(fdx[e->face] * 50);
        e->vy = (int8_t)(fdy[e->face] * 50);
      }
      break;
    case ES_LUNGE:
      nx = (uint8_t)((uint16_t)(e->x + e->vx) >> 4);
      ny = (uint8_t)((uint16_t)(e->y + e->vy) >> 4);
      if (!mite_free(nx, ny) || !--e->t) {
        e->st = ES_REST;
        e->t = 40u;
        e->cd = 50u;
      } else {
        e->x = (uint16_t)(e->x + e->vx);
        e->y = (uint16_t)(e->y + e->vy);
      }
      break;
    case ES_REST:
      if (!--e->t) {
        uint8_t c = cell_at(ex, ey);
        e->st = ES_WALK;
        e->tx = (uint8_t)(EG_X(c) * 16u + 8u);
        e->ty = (uint8_t)(EG_Y(c) * 16u + 8u);
      }
      break;
    case ES_HURT:
      nx = (uint8_t)((uint16_t)(e->x + e->vx) >> 4);
      ny = (uint8_t)((uint16_t)(e->y + e->vy) >> 4);
      if (mite_free(nx, ny)) {
        e->x = (uint16_t)(e->x + e->vx);
        e->y = (uint16_t)(e->y + e->vy);
      }
      e->vx = (int8_t)(e->vx / 2 + e->vx / 4);
      e->vy = (int8_t)(e->vy / 2 + e->vy / 4);
      if (!--e->t) {
        e->st = ES_REST;
        e->t = 12u;
        e->cd = 30u;
      }
      break;
  }
  if (e->st != ES_TELL && overlap(ex, ey, pl_x, pl_y, 11u, 11u)) player_hurt(e->st == ES_LUNGE ? 2u : 1u, ex, ey);
}

static void eye_update(enemy_t *e) {
  uint8_t ex = (uint8_t)(e->x >> 4), ey = (uint8_t)(e->y >> 4), d;
  int8_t mx = 0, my = 0;
  uint16_t nx, ny;
  switch (e->st) {
    case ES_FLOAT:
      d = (uint8_t)(absdiff(ex, pl_x) + absdiff(ey, pl_y));
      if (d > 70u) {
        mx = ex < pl_x ? 7 : -7;
        my = ey < pl_y ? 7 : -7;
      } else if (d < 44u) {
        mx = ex < pl_x ? -9 : 9;
        my = ey < pl_y ? -9 : 9;
      } else { /* circle: strafe across the line to the player */
        mx = (e->anim & 128u) ? 6 : -6;
        my = (e->anim & 128u) ? -6 : 6;
        if (absdiff(ex, pl_x) > absdiff(ey, pl_y)) mx = 0;
        else my = 0;
      }
      if (e->cd) e->cd--;
      else if (d < 110u)
        tell(e, 40u);
      break;
    case ES_TELL:
      if (!--e->t) {
        fire_orb_at(ex, ey, (int16_t)pl_x - ex, (int16_t)pl_y - ey, 22u);
        sfx(SFX_SHOT);
        e->st = ES_FLOAT;
        e->cd = (uint8_t)(110u + eg_rndn(70u));
      }
      break;
    case ES_HURT:
      mx = e->vx;
      my = e->vy;
      e->vx = (int8_t)(e->vx / 2 + e->vx / 4);
      e->vy = (int8_t)(e->vy / 2 + e->vy / 4);
      if (!--e->t) {
        e->st = ES_FLOAT;
        if (e->cd < 30u) e->cd = 30u;
      }
      break;
  }
  if (mx) {
    nx = (uint16_t)(e->x + mx);
    if (eye_free((uint8_t)(nx >> 4), ey)) e->x = nx;
  }
  if (my) {
    ny = (uint16_t)(e->y + my);
    if (eye_free((uint8_t)(e->x >> 4), (uint8_t)(ny >> 4))) e->y = ny;
  }
  if (overlap(ex, ey, pl_x, pl_y, 10u, 10u)) player_hurt(1u, ex, ey);
}

void enemies_update(void) BANKED {
  uint8_t k;
  uint8_t mobs = 0;
  for (k = 0; k != nen; k++) {
    enemy_t *e = &en[k];
    if (e->st == ES_DEAD) continue;
    if (e->kind == SPAWN_MOB) { /* B2: src/mob.c, all at once */
      mobs = 1u;
      continue;
    }
    e->anim++;
    if (e->flash) e->flash--;
    if (e->kind == SPAWN_MITE)
      mite_update(e);
    else if (e->kind == SPAWN_EYE)
      eye_update(e);
    else
      boss_update(k);
  }
  if (mobs) mobs_update();
}

void orbs_update(void) BANKED {
  uint8_t k, j, ox, oy, t;
  for (k = 0; k != MAXO; k++) {
    orb_t *o = &orbs[k];
    if (!o->live) continue;
    o->x = (uint16_t)(o->x + o->vx);
    o->y = (uint16_t)(o->y + o->vy);
    ox = (uint8_t)(o->x >> 4);
    oy = (uint8_t)(o->y >> 4);
    t = cell_type(cell_at(ox, oy));
    if (!--o->life || t == T_WALL || t == T_ROCK || t == T_BRICK || t == T_DOOR) {
      o->live = 0;
      puff(ox, oy, 5, ST_PUFF);
      continue;
    }
    if (o->friendly) {
      for (j = 0; j != nen; j++) {
        uint8_t r = en[j].kind == SPAWN_BOSS ? 16u : 9u;
        if (en[j].st != ES_DEAD && overlap(ox, oy, (uint8_t)(en[j].x >> 4), (uint8_t)(en[j].y >> 4), r, r)) {
          o->live = 0;
          hit_src = HS_ORB; /* B2: a returned orb, and whose it was */
          hit_orb_from = (uint8_t)(o->pad - 1u);
          hit_tags = 0;
          enemy_hit(j, (mods & MOD_GROUND) ? 4u : 2u, ox, oy);
          break;
        }
      }
    } else if (overlap(ox, oy, pl_x, pl_y, 6u, 6u) && !pl_iframe && !pl_inv) {
      o->live = 0;
      player_hurt(1u, ox, oy);
    }
  }
}

/* after a gravity step: a falling CRT in an enemy's cell crushes it (the boss: it takes a heavy hit) */
void actors_crush(void) BANKED {
  uint8_t k, c, x, y;
  for (k = 0; k != nen; k++) {
    enemy_t *e = &en[k];
    if (e->st == ES_DEAD) continue;
    x = (uint8_t)(e->x >> 4);
    y = (uint8_t)(e->y >> 4);
    if (e->kind == SPAWN_BOSS && bs_kind) { /* B2 bosses */
      boss_crush();
      continue;
    }
    if (e->kind == SPAWN_BOSS) {
      /* the boss is 2x2 cells: any falling CRT landing in its top cells */
      uint8_t i;
      for (i = 0; i != 2u; i++) {
        c = cell_at((uint8_t)(x - 8u + i * 15u), (uint8_t)(y - 8u));
        if (cell_type(c) == T_ROCK && (room.c[c] & F_FALL)) {
          room.c[c] = T_EMPTY; /* the set shatters on it */
          queue_cell(c);
          puff((uint8_t)(EG_X(c) * 16u + 8u), (uint8_t)(EG_Y(c) * 16u + 8u), 4, ST_PUFF);
          enemy_hit(k, 8u, x, (uint8_t)(y - 20u));
          shake = 10u;
          break;
        }
      }
      continue;
    }
    c = cell_at(x, y);
    if (cell_type(c) == T_ROCK && (room.c[c] & F_FALL)) {
      hit_src = HS_CRUSH; /* B2 */
      if (e->kind == SPAWN_MOB && (mob[k].status & MS_UNDER)) continue;
      enemy_kill(k, 1);
    }
  }
}
