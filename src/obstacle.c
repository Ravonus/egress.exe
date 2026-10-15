/* The cartridge adapter for the same living-obstacle rules the tick solver runs.
 * Floor-only state; room.c carries the actual occupied cells, never the tween. */
#pragma bank 255
#include <gb/cgb.h>
#include <string.h>
#include "obstacle.h"
#include "puzzle.h"
#include "mob.h"
#include "oam.h"
#include "look.h"
#include "boss_api.h"
#include "core/eg_input.h"
#include "core/eg_move.h"
#include "gen/obstacle_art.h"

extern eg_input_t pin;
extern eg_mover_t pmv;
eg_ob obstacle[EG_MAX_OB];
uint8_t obstacle_phase, obstacle_clock;
static eg_otick tick;
static uint8_t previous, noise_n, noise_cell[2], hit_mask_ob;

void obstacle_reset(void) BANKED {
  memset(obstacle, 0, sizeof obstacle);
  obstacle_phase = obstacle_clock = noise_n = hit_mask_ob = 0;
  previous = room.start;
  if (room.nob) eg_ob_init(&room, obstacle);
}

void obstacle_show(void) BANKED {
  uint8_t k;
  /* The Tenant can summon one MOVER at its phase gate. Its tiles are ready
   * before the LCD comes on; summoning never uploads sprite data mid-frame. */
  if (boss_floor == BOSS_BIOME) obstacle_art_load(OB_HEAVY, 0u);
  for (k = 0; k != room.nob; k++)
    obstacle_art_load(OBA_RULE(room.ob[k][0]), k);
}

uint8_t boss_obstacle_cmd(uint8_t cmd, uint8_t cell, uint8_t rule) BANKED {
  uint8_t k;
  if (cmd == BOC_SUMMON) {
    if (room.nob || rule != OB_HEAVY || room.c[cell] != T_EMPTY || cell == cell_at(pl_x, pl_y)) return 0;
    room.nob = 1u;
    room.ob[0][0] = OB_HEAVY | OBA_P2;
    room.ob[0][1] = cell;
    room.ob[0][2] = 1u; /* east, then clockwise when blocked */
    room.ob[0][3] = 0;
    obstacle_reset();
    return 1u;
  }
  for (k = 0; k != room.nob; k++) {
    if (cmd == BOC_REVERSE) eg_ob_reverse(&room, &obstacle[k], k);
    else if (cmd == BOC_WAKE) obstacle[k].st |= OBF_AWAKE;
    else return 0;
  }
  return room.nob != 0u;
}

void boss_obstacles_clear(void) BANKED {
  uint8_t k, c;
  for (k = 0; k != room.nob; k++) {
    c = obstacle[k].cell;
    room.c[c] &= (uint8_t)~F_OB;
    puff(EG_X(c) * 16u + 8u, EG_Y(c) * 16u + 8u, 5u, ST_PUFF);
    queue_cell(c);
  }
  room.nob = 0;
  eg_plates_dirty = 1u;
}

void obstacle_noise(uint8_t cell) BANKED {
  if (noise_n < 2u) noise_cell[noise_n++] = cell;
}

void obstacle_tick(void) BANKED {
  uint8_t k, c, p;
  if (!room.nob) return;
  if (!(pin.flags & EG_FL_HITBOX)) hit_mask_ob = 0;
  if (++obstacle_phase != EG_OB_TICK) return;
  obstacle_phase = 0;
  p = cell_at(pl_x, pl_y);
  memset(&tick, 0, sizeof tick);
  tick.p = p;
  tick.pp = previous;
  tick.face = pmv.facing;
  tick.tick = obstacle_clock++;
  tick.nnoise = noise_n;
  tick.noise[0] = noise_cell[0];
  tick.noise[1] = noise_cell[1];
  noise_n = 0;
  eg_ob_tick(&room, obstacle, &tick);
  previous = tick.p;
  if (tick.ev & EV_SHOVE) {
    psx = cell_sub(EG_X(tick.p));
    psy = cell_sub(EG_Y(tick.p));
    pl_x = (uint8_t)(psx >> 4);
    pl_y = (uint8_t)(psy >> 4);
  }
  if (tick.hits) {
    if (pin.flags & EG_FL_GUARD) {
      for (k = 0; k != room.nob; k++)
        if (obstacle[k].cell == p || obstacle[k].from == p)
          eg_ob_stop(&obstacle[k], OBS_STUN, (pin.flags & EG_FL_PERFECT) ? 6u : 4u);
    }
    player_hurt(tick.hits, pl_x, pl_y);
  }
  for (k = 0; k != tick.nchg; k++) {
    c = tick.chg[k];
    if (cell_type(c) == T_PIT) look_break_tile(c, LOOK_FRAG_AUTO);
    else queue_cell(c);
  }
  if (tick.ev & EV_TILES) puzzle_dirty = 1u;
  if (tick.ev & (EV_MOVED | EV_BROKE | EV_WOKE)) sfx(SFX_PUSH);
  /* Plate/shutter resolution follows the obstacle tick in the same frame. */
  eg_shutters(&room, &tick.p, 1u);
  for (k = 0; k != eg_ndirty && k != EG_DIRTY_MAX; k++) queue_cell(eg_dirty[k]);
}

void obstacle_hit(uint8_t x, uint8_t y, uint8_t rx, uint8_t ry) BANKED {
  uint8_t k, c, tags = hit_tags;
  if (wmods & WM_RIME) tags |= TG(TAG_COLD);
  if (wmods & WM_EMBER) tags |= TG(TAG_HOT);
  if (wmods & WM_DAMP) tags |= TG(TAG_WET);
  for (k = 0; k != room.nob; k++) {
    if (hit_mask_ob & (1u << k)) continue;
    c = obstacle[k].cell;
    if (!overlap(x, y, EG_X(c) * 16u + 8u, EG_Y(c) * 16u + 8u, rx + 6u, ry + 6u)) continue;
    hit_mask_ob |= (uint8_t)(1u << k);
    if (tags & TG(TAG_COLD)) eg_ob_stop(&obstacle[k], OBS_FREEZE, 6u);
    else if (tags & TG(TAG_ALIVE)) eg_ob_stop(&obstacle[k], OBS_ROOT, 6u);
    else if (tags & TG(TAG_GLITCH)) eg_ob_reverse(&room, &obstacle[k], k);
    sfx(SFX_DENY);
    fx_burst(EG_X(c) * 16u + 8u, EG_Y(c) * 16u, FX_HIT, 1u);
    if (!(obstacle[k].st & OBF_TAGGED)) {
      obstacle[k].st |= OBF_TAGGED;
      obstacle_art_name(OBA_RULE(room.ob[k][0]));
    }
  }
}

void obstacle_draw(void) BANKED {
  static const uint8_t tween[EG_OB_TICK] = {0,0,1,2,2,3,4,4,5,6,6,7,8,8,9,10,10,11,12,12,13,14,14,15};
  uint8_t j, k, c, f, tile, t;
  int16_t x, y, from_x, from_y;
  for (j = 0; j != room.nob; j++) {
    k = room.nob == 1u ? 0u : (uint8_t)((j + oam_spin) % room.nob);
    c = obstacle[k].cell;
    x = (int16_t)EG_X(c) * 16 + 8;
    y = (int16_t)EG_Y(c) * 16 + 8;
    if (obstacle[k].st & OBF_MOVED) {
      from_x = (int16_t)EG_X(obstacle[k].from) * 16 + 8;
      from_y = (int16_t)EG_Y(obstacle[k].from) * 16 + 8;
      x = from_x + (x > from_x ? tween[obstacle_phase] : x < from_x ? -(int8_t)tween[obstacle_phase] : 0);
      y = from_y + (y > from_y ? tween[obstacle_phase] : y < from_y ? -(int8_t)tween[obstacle_phase] : 0);
    }
    x -= (int16_t)cam_x + 12;
    y -= (int16_t)cam_y + 24;
    f = (frame_count & 7u) >= 6u && !obstacle[k].stop;
    tile = (uint8_t)(MOB_TILE0 + k * MOB_SLOT_TILES + f * 12u);
    t = k == 2u ? 5u : (uint8_t)(2u + k);
    if (!oam_fits(y, 3u) || !oam_fits(y + 16, 3u)) continue;
    oam_group(x, y, 3u, tile, 2u, t);
    oam_group(x, y + 16, 3u, tile + 6u, 2u, t);
  }
}
