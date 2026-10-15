/* The stages' adapter onto today's actors (see stage_spawn.h): mites and eyes, set up as actors_spawn sets them. */
#pragma bank 255
#include <gb/gb.h>
#include "play.h"
#include "stage_spawn.h"

uint8_t ss_spawn(uint8_t fam, uint8_t cell, uint8_t elite, uint16_t seed) BANKED {
  /* mob.h: return mob_spawn(fam_of[fam], cell, elite, seed); */
  uint8_t k;
  enemy_t *e;
  if (cell_type(cell) != T_EMPTY) return 255u;
  for (k = 0; k != nen; k++)
    if (en[k].st == ES_DEAD) break;
  if (k == MAXE) return 255u;
  if (k == nen) nen++;
  e = &en[k];
  e->kind = (fam == SS_ORB || fam == SS_SHOOTER) ? SPAWN_EYE : SPAWN_MITE;
  e->x = cell_sub(EG_X(cell));
  e->y = cell_sub(EG_Y(cell));
  e->tx = (uint8_t)(e->x >> 4);
  e->ty = (uint8_t)(e->y >> 4);
  e->anim = (uint8_t)seed;
  e->flash = 0;
  e->vx = e->vy = 0;
  e->face = 0;
  if (e->kind == SPAWN_MITE) {
    e->st = ES_WALK;
    e->hp = (uint8_t)(3u + (floor_tier >> 1) + (elite ? 3u : 0u));
    e->cd = 60u;
  } else {
    e->st = ES_FLOAT;
    e->hp = (uint8_t)(2u + (floor_tier >> 2) + (elite ? 2u : 0u));
    e->cd = (uint8_t)(70u + ((uint8_t)seed & 31u));
  }
  enemies_alive++;
  return k;
}

uint8_t ss_spawn_ok(uint8_t cell) BANKED {
  /* mob.h: return mob_spawn_ok(cell); */
  uint8_t k, row = 0, c, p = cell_at(pl_x, pl_y);
  if (cell_type(cell) != T_EMPTY) return 0u;
  if (absdiff(EG_X(cell), EG_X(p)) < 3u && absdiff(EG_Y(cell), EG_Y(p)) < 3u) return 0u;
  for (k = 0; k != nen; k++) {
    if (en[k].st == ES_DEAD) continue;
    c = cell_at((uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4));
    if (absdiff(EG_X(c), EG_X(cell)) < 2u && absdiff(EG_Y(c), EG_Y(cell)) < 2u) return 0u;
    if (EG_Y(c) == EG_Y(cell)) row++;
  }
  return (uint8_t)(row < 2u);
}

uint8_t ss_alive(void) BANKED {
  /* mob.h: return mobs_alive(); */
  uint8_t k, n = 0;
  for (k = 0; k != nen; k++)
    if (en[k].st != ES_DEAD) n++;
  return n;
}

void ss_preload(uint8_t fams) BANKED { (void)fams; /* mob.h: mobs_preload(mask of FAM_*) */ }
