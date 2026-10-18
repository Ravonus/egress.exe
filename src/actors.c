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
#include "narrator.h"
#include "core/eg_move.h"
#include "companion.h"
#include "health.h"

enemy_t en[MAXE];
uint8_t nen;
orb_t orbs[MAXO];
uint8_t enemies_alive, kills_run;

/* B2: the office's matrix mobs take the room's spawns (src/mob.c); the B1 mite and eye stay for boss.c's summons */
void actors_spawn(void) BANKED { mobs_floor(); }

void enemy_kill(uint8_t k, uint8_t crushed) BANKED {
  enemy_t *e = &en[k];
  narrator_event(crushed ? NE_CRUSH : NE_KILL,0u);
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
  if(!(((uint8_t)floor_seed^kills_run^k)&15u))health_drop(cell_at((uint8_t)(e->x>>4),(uint8_t)(e->y>>4)));
  sfx(SFX_KILL);
  if (enemies_alive) enemies_alive--;
  if (e->kind == SPAWN_BOSS) {
    shake = 30u;
    freeze = 20u;
    open_door_quiet();
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

/* Every generated spawn is a matrix mob; the two current arena bosses have
 * their own shared controller. There is no second enemy renderer/AI. */
void enemies_update(void) BANKED {
  uint8_t k,mobs=0u;
  for(k=0u;k<nen;k++) {
    enemy_t *e=&en[k];if(e->st==ES_DEAD)continue;
    if(e->kind==SPAWN_MOB){mobs=1u;continue;}
    if(e->kind==SPAWN_BOSS && bs_kind){e->anim++;if(e->flash)e->flash--;boss_update_all();}
  }
  if(mobs)mobs_update();
}

void orbs_update(void) BANKED {
  /* Constant addresses avoid the indexed eight-slot walk on quiet frames. */
  if(!(orbs[0].live|orbs[1].live|orbs[2].live|orbs[3].live|
       orbs[4].live|orbs[5].live|orbs[6].live|orbs[7].live))return;
  uint8_t k, j, ox, oy, t;
  uint16_t x,y;
  orb_t *o=orbs;
  for (k = 0; k != MAXO; k++,o++) {
    if (!o->live || (o->friendly&128u)) continue;
    x = (uint16_t)(o->x + o->vx);
    y = (uint16_t)(o->y + o->vy);
    o->x=x;o->y=y;
    /* Bounds precede byte conversion and the cell lookup: the island has no
     * invisible concrete rim to stop an orb before its coordinate would wrap. */
    if((uint8_t)(x>>8)>=EG_W || (uint8_t)(y>>8)>=EG_H) {
      o->live=0u;
      continue;
    }
    ox = (uint8_t)(((uint8_t)x>>4)|((uint8_t)(x>>8)<<4));
    oy = (uint8_t)(((uint8_t)y>>4)|((uint8_t)(y>>8)<<4));
    t = cell_type(cell_at(ox, oy));
    if (!--o->life || t == T_WALL || t == T_ROCK || t == T_BRICK || t == T_DOOR) {
      o->live = 0;
      puff(ox, oy, 5, ST_PUFF);
      continue;
    }
    if (o->friendly) {
      enemy_t *e=en;
      for (j = 0; j != nen; j++,e++) {
        uint8_t r = e->kind == SPAWN_BOSS ? 16u : 9u;
        if (e->st != ES_DEAD && overlap(ox, oy, (uint8_t)(e->x >> 4), (uint8_t)(e->y >> 4), r, r)) {
          o->live = 0;
          hit_src = HS_ORB; /* B2: a returned orb, and whose it was */
          hit_orb_from = (uint8_t)(o->pad - 1u);
          hit_tags = 0;
          enemy_hit(j, (uint8_t)(((mods & MOD_GROUND) ? 4u : 2u)+(o->friendly-1u)), ox, oy);
          break;
        }
      }
    } else if(companion.hp && (t=companion_intercept(ox,oy))) {
      if(t==2u){o->friendly=1u;o->vx=-o->vx;o->vy=-o->vy;o->life=80u;}
      else o->live=0u;
    } else if (overlap(ox, oy, pl_x, pl_y, 6u, 6u) && !pl_iframe && !pl_inv) {
      o->live = 0;
      player_hurt(1u, ox, oy);
    }
    if(freeze)break; /* the remaining projectiles share this impact's hitstop */
  }
}

/* after a gravity step: a falling CRT in an enemy's cell crushes it (the boss: it takes a heavy hit) */
void actors_crush(void) BANKED {
  uint8_t k, c, x, y;
  /* Stage mobs already resolve standing inside any CRT in mobs_update. Only
   * this step's newly falling load can reach another one afterward. Bosses
   * retain their separate multi-cell geometry check. */
  if(!board_busy && !boss_floor) return;
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
