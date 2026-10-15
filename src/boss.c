/* The last floor: THE WATCHMAN, a ceiling camera that came loose and kept watching.
 *
 * Its arena is built, not generated: open carpet, two concrete pillars for cover, and a gallery of CRT sets resting on
 * drywall along the top. Break the drywall under a set while the Watchman hangs below it and the set drops on it
 * (a heavy hit). It drifts after the player and cycles three attacks, each with a tell (the lens flares and it
 * shudders): FAN (a spread of orbs, deflectable), SUMMON (two static mites), DIVE (it drops at the player, then hangs
 * stunned and open). Under half its strength the tells are shorter and the fan wider. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "boss_api.h" /* B2 bosses */

#define BOSS_HP 44u
#define HOVER_Y 92u
enum { AT_FAN = 0, AT_SUMMON, AT_FAN2, AT_DIVE };

void boss_build(void) BANKED {
  uint8_t i, x, y;
  enemy_t *e = &en[0];
  for (i = 0; i != (uint8_t)EG_N; i++) {
    x = EG_X(i);
    y = EG_Y(i);
    room.c[i] = (x == 0u || y == 0u || x == EG_W - 1u || y == EG_H - 1u) ? T_WALL : T_EMPTY;
  }
  /* the gallery: CRT sets on a drywall ledge */
  for (x = 2; x != 14u; x++) {
    room.c[EG_IDX(x, 3)] = T_BRICK;
    if (x & 1u) room.c[EG_IDX(x, 2)] = T_ROCK;
    else room.c[EG_IDX(x, 2)] = T_DIRT;
  }
  room.c[EG_IDX(1, 3)] = T_WALL;
  room.c[EG_IDX(14, 3)] = T_WALL;
  /* pillars */
  room.c[EG_IDX(3, 9)] = room.c[EG_IDX(3, 10)] = T_WALL;
  room.c[EG_IDX(12, 9)] = room.c[EG_IDX(12, 10)] = T_WALL;
  /* a few tokens in the corners, foam along the bottom */
  room.c[EG_IDX(1, 12)] = room.c[EG_IDX(14, 12)] = T_TOKEN;
  for (x = 4; x != 12u; x++) room.c[EG_IDX(x, 12)] = (x == 7u || x == 8u) ? T_EMPTY : T_DIRT;
  room.door = EG_IDX(7, 0);
  room.c[room.door] = T_DOOR;
  room.start = EG_IDX(7, 11);
  room.keys = 0;
  room.tokens = 2;
  room.nspawn = 0;
  nen = 1;
  enemies_alive = 1;
  memset(e, 0, sizeof *e);
  e->kind = SPAWN_BOSS;
  e->st = ES_FLOAT;
  e->hp = BOSS_HP;
  e->x = (uint16_t)128u << 4;
  e->y = (uint16_t)HOVER_Y << 4;
  e->cd = 120u;
  for (i = 1; i != MAXE; i++) en[i].st = ES_DEAD;
}

static void summon(enemy_t *b) {
  uint8_t k, n = 0, live = 0, side;
  for (k = 1; k != MAXE; k++)
    if (en[k].st != ES_DEAD) live++;
  for (side = 0; side != 2u && live < 3u; side++) {
    uint8_t x = (uint8_t)((b->x >> 4) + (side ? 24u : (uint8_t)-24)), y = (uint8_t)((b->y >> 4) + 8u);
    uint8_t c = cell_at(x, y);
    if (cell_type(c) != T_EMPTY) continue;
    for (k = 1; k != MAXE; k++)
      if (en[k].st == ES_DEAD) break;
    if (k == MAXE) return;
    memset(&en[k], 0, sizeof(enemy_t));
    en[k].kind = SPAWN_MITE;
    en[k].st = ES_REST;
    en[k].t = 30u;
    en[k].hp = 3u;
    en[k].cd = 40u;
    en[k].x = cell_sub(EG_X(c));
    en[k].y = cell_sub(EG_Y(c));
    puff((uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4), 2, ST_PUFF);
    if (k >= nen) nen = (uint8_t)(k + 1u);
    enemies_alive++;
    live++;
    n++;
  }
  if (n) sfx(SFX_TELL);
}

static void fan(enemy_t *b, uint8_t wide) {
  uint8_t bx = (uint8_t)(b->x >> 4), by = (uint8_t)((b->y >> 4) + 10u), i, n = wide ? 7u : 5u;
  int16_t dx = (int16_t)pl_x - bx, dy = (int16_t)pl_y - by;
  /* spread across the line to the player: offsets perpendicular to its main axis */
  for (i = 0; i != n; i++) {
    int16_t o = (int16_t)i - (int16_t)(n >> 1);
    if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy))
      fire_orb_at(bx, by, dx, dy + o * 24, 20u);
    else
      fire_orb_at(bx, by, dx + o * 24, dy, 20u);
  }
  sfx(SFX_SHOT);
  shake = 4u;
}

void boss_update(uint8_t k) BANKED {
  enemy_t *b = &en[k];
  uint8_t bx = (uint8_t)(b->x >> 4), by = (uint8_t)(b->y >> 4), phase2 = (uint8_t)(b->hp <= BOSS_HP / 2u);
  if (bs_kind) { /* B2 bosses (src/boss_core.c) */
    boss_update_all();
    return;
  }
  switch (b->st) {
    case ES_FLOAT: {
      /* drift after the player's column; hang at the hover line */
      uint8_t sp = phase2 ? 12u : 9u;
      if (bx + 4u < pl_x && bx < 220u) b->x += sp;
      else if (bx > pl_x + 4u && bx > 36u) b->x -= sp;
      if (by < HOVER_Y) b->y += 8u;
      else if (by > HOVER_Y) b->y -= 8u;
      if (!b->cd--) {
        b->st = ES_TELL;
        b->t = phase2 ? 30u : 44u;
        sfx(SFX_TELL);
      }
      break;
    }
    case ES_TELL:
      if (!--b->t) {
        switch (b->tx & 3u) {
          case AT_FAN:
          case AT_FAN2:
            fan(b, phase2);
            b->st = ES_FLOAT;
            b->cd = phase2 ? 70u : 100u;
            break;
          case AT_SUMMON:
            summon(b);
            b->st = ES_FLOAT;
            b->cd = phase2 ? 80u : 110u;
            break;
          default:
            b->st = ES_DIVE;
            b->vy = 36;
            b->ty = pl_y; /* where it aims */
            shake = 6u;
            break;
        }
        b->tx++;
      }
      break;
    case ES_DIVE:
      if (by + 18u < b->ty && by < 176u && eye_free(bx, (uint8_t)(by + 18u))) {
        b->y += (uint8_t)b->vy;
        if (bx + 2u < pl_x) b->x += 6u;
        else if (bx > pl_x + 2u) b->x -= 6u;
      } else {
        b->st = ES_STUN;
        b->t = phase2 ? 56u : 72u;
        shake = 10u;
        sfx(SFX_THUD);
      }
      break;
    case ES_STUN:
      if (!--b->t) {
        b->st = ES_FLOAT;
        b->cd = 60u;
      }
      break;
  }
  if (overlap(bx, by, pl_x, pl_y, 20u, 20u)) player_hurt(b->st == ES_DIVE ? 2u : 1u, bx, by);
}
