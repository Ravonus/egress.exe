/* A run: ten floors down to the egress. Nine generated puzzle floors (seeds the host solver accepted, harder as you
 * go), landings between them where someone talks and trades, and THE WATCHMAN on the tenth.
 *
 * A floor is a 16x14 grid of 16 px cells drawn whole into the background map; the camera only scrolls. CRTs fall on
 * the shared eg_gravity step every 8 frames: a falling set crushes enemies and hurts the player. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "run.h"
#include "gen/eg_seeds.h"
#include "look.h"

uint8_t tokens, keys_held, door_open, board_busy, cam_x, cam_y;
uint8_t freeze, shake, hud_dirty, grav_t, boss_floor;
static uint8_t was_falling, paused, sel_t, floor_tokens0;

static const uint8_t floor_tiers[RUN_FLOORS - 1u] = {0, 0, 1, 1, 2, 2, 3, 4, 5};

static void people_colours(void) {
  uint8_t s = (uint8_t)(run_seed & 7u), h = (uint8_t)((run_seed >> 3) & 7u), c = (uint8_t)((run_seed >> 6) & 7u),
          t = (uint8_t)((run_seed >> 9) % ART_TROUSERS);
  pal_sp[0 * 4 + 2] = people[s];
  pal_sp[0 * 4 + 3] = people[ART_SKINS + h];
  pal_sp[1 * 4 + 2] = people[ART_SKINS + ART_HAIRS + c];
  pal_sp[1 * 4 + 3] = people[ART_SKINS + ART_HAIRS + ART_HOODIES + t];
}

static void floor_build(void) {
  uint8_t i, k;
  boss_floor = (uint8_t)(floor_no >= RUN_FLOORS);
  memset(&acts, 0, sizeof acts);
  memset(cracked, 0, sizeof cracked);
  memset(orbs, 0, sizeof orbs);
  memset(puffs, 0, sizeof puffs);
  if (boss_floor) {
    floor_tier = 5u;
    floor_seed = 0;
    boss_build();
  } else {
    floor_tier = floor_tiers[floor_no - 1u];
    k = (uint8_t)(run_seed * 5u + (run_seed >> 8) + floor_no * 37u);
    for (i = 0;; i++) {
      floor_seed = eg_seed_pick(floor_tier, (uint8_t)(k + i));
      if (eg_generate(&room, floor_seed, floor_tier)) break;
    }
  }
  /* game randomness from here on: seeded by the run and the floor */
  eg_rs = (uint16_t)(run_seed ^ floor_seed ^ ((uint16_t)floor_no << 8));
  if (!eg_rs) eg_rs = 1u;
  keys_held = 0;
  door_open = 0;
  board_busy = 0;
  was_falling = 0;
  psx = cell_sub(EG_X(room.start));
  psy = cell_sub(EG_Y(room.start));
  pl_x = (uint8_t)(psx >> 4);
  pl_y = (uint8_t)(psy >> 4);
  pl_state = PS_FREE;
  pl_face = F_UP;
  pl_inv = 30u;
  pl_charge = 0;
  pl_combo = 0;
  roll_cd = 0;
  freeze = shake = 0;
  grav_t = GRAVITY_FRAMES;
  if (!boss_floor) actors_spawn();
  cells_reset();
}

static void camera(uint8_t snap) {
  int16_t tx = (int16_t)pl_x - 80, ty = (int16_t)pl_y - 64, dx, dy;
  if (tx < 0) tx = 0;
  if (tx > 96) tx = 96;
  if (ty < 0) ty = 0;
  if (ty > 96) ty = 96;
  if (snap) {
    cam_x = (uint8_t)tx;
    cam_y = (uint8_t)ty;
    return;
  }
  dx = tx - cam_x;
  dy = ty - cam_y;
  cam_x = (uint8_t)(cam_x + (dx / 4 ? dx / 4 : (dx > 0 ? 1 : (dx < 0 ? -1 : 0))));
  cam_y = (uint8_t)(cam_y + (dy / 4 ? dy / 4 : (dy > 0 ? 1 : (dy < 0 ? -1 : 0))));
}

static void scroll(void) {
  uint8_t sx = cam_x, sy = cam_y;
  if (shake) {
    shake--;
    sx = (uint8_t)(sx + ((frame_count & 2u) ? 1u : 255u));
    sy = (uint8_t)(sy + ((frame_count & 4u) ? 1u : 255u));
  }
  move_bkg(sx, sy);
}

static void floor_show(void) {
  uint8_t i;
  DISPLAY_OFF;
  HIDE_SPRITES;
  art_load_game();
  /* look: the office's cards and palettes; the room on screen will assemble around the player */
  look_load(BIOME_OFFICE);
  camera(1);
  look_assemble(room.start);
  for (i = 0; i != (uint8_t)EG_N; i++) draw_cell(i);
  hud_init();
  scroll();
  sprites_reset();
  draw_sprites();
  SHOW_BKG;
  SHOW_WIN;
  SHOW_SPRITES;
  pal_commit(0);
  DISPLAY_ON;
}

static void gravity_tick(void) {
  uint8_t occ[4], n = 0, c, k, g, a, b;
  uint8_t xs[2], ys[2];
  xs[0] = (uint8_t)(pl_x - 5u);
  xs[1] = (uint8_t)(pl_x + 4u);
  ys[0] = (uint8_t)(pl_y - 5u);
  ys[1] = (uint8_t)(pl_y + 4u);
  for (a = 0; a != 2u; a++)
    for (b = 0; b != 2u; b++) {
      c = cell_at(xs[b], ys[a]);
      for (k = 0; k != n; k++)
        if (occ[k] == c) break;
      if (k == n) occ[n++] = c;
    }
  g = eg_gravity(&room, occ, pl_hp ? n : 0u);
  if (eg_ndirty > EG_DIRTY_MAX) {
    for (c = 0; c != (uint8_t)EG_N; c++)
      if (cell_type(c) == T_ROCK || cell_type(c) == T_EMPTY) queue_cell(c);
  } else
    for (k = 0; k != eg_ndirty; k++) queue_cell(eg_dirty[k]);
  if (g & EG_G_HIT) player_hurt(2u, pl_x, (uint8_t)(pl_y - 16u));
  board_busy = (uint8_t)((g & EG_G_FALLING) ? 1u : 0u);
  if (was_falling && !board_busy) {
    sfx(SFX_THUD);
    shake = 3u;
  }
  was_falling = board_busy;
  actors_crush();
}

/* look: the room falls into place around the player; nothing moves until it has (the floor's enemies glitch in
 * once it is whole) */
static void assemble_wait(void) {
  uint8_t keep = nen, inv = pl_inv, k;
  nen = 0;
  pl_inv = 0; /* no blinking while the world arrives */
  while (look_busy()) {
    frame_wait();
    look_tick();
    sound_tick();
    input_poll();
    draw_sprites();
  }
  nen = keep;
  pl_inv = inv;
  for (k = 0; k != nen; k++) puff((uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4), 4u, ST_PUFF);
}

/* 1: through the door, 0: dead */
static uint8_t floor_play(void) {
  uint8_t end_t = 0;
  floor_tokens0 = tokens;
  floor_build();
  floor_show();
  music_play(boss_floor ? SONG_BOSS : SONG_FLOOR);
  music_fade(0);
  fade_to(0, 8);
  assemble_wait();
  game_mode = MODE_PLAY;
  if (boss_floor) hud_msg("IT SEES YOU.");
  else if (floor_no == 1u) hud_msg("FIND THE DOOR");
  else hud_msg("ANOTHER FLOOR");
  while (1) {
    frame_wait();
    look_tick();
    flush_cells(8);
    scroll();
    sound_tick();
    input_poll();
    hud_tick();
    if (pl_state == PS_EXIT) {
      /* look: the floor falls apart behind you, from the edges in to the door */
      if (++end_t == 2u) look_collapse(cell_at(pl_x, pl_y), 1u);
      if (end_t >= 20u && (!look_busy() || end_t >= 110u)) {
        look_freeze();
        music_fade(1);
        fade_to(8, 0);
        look_end();
        game_mode = MODE_CLEAR;
        return 1;
      }
      continue;
    }
    if (pl_hp == 0u) {
      /* look: the world lets go around the body, leaving it on one tile and its neighbours (B3 owns the rest) */
      if (++end_t == 20u) look_collapse(cell_at(pl_x, pl_y), 2u);
      if (end_t >= 60u && (!look_busy() || end_t >= 150u)) {
        look_freeze();
        music_fade(1);
        fade_to(8, 0);
        look_end();
        return 0;
      }
      draw_sprites();
      continue;
    }
    if (joy_pressed & J_START) {
      paused ^= 1u;
      if (paused) hud_msg("PAUSED");
      else hud_msg("");
    }
    if (paused) continue;
    /* hold SELECT to put the floor back the way it was (it costs half a heart) */
    if ((joy & J_SELECT) && !boss_floor) {
      if (++sel_t == 60u) {
        sel_t = 0;
        tokens = floor_tokens0;
        if (pl_hp > 1u) pl_hp--;
        look_freeze();
        fade_to(8, 0);
        floor_build();
        floor_show();
        fade_to(0, 8);
        assemble_wait();
        hud_msg("IT RESETS.");
        continue;
      }
    } else
      sel_t = 0;
    if (acts.frames != 0xffffu) acts.frames++;
    if (freeze) {
      freeze--;
      continue;
    }
    player_update();
    enemies_update();
    orbs_update();
    if (!--grav_t) {
      grav_t = GRAVITY_FRAMES;
      gravity_tick();
    }
    if (door_open && cell_at(pl_x, pl_y) == room.door) {
      pl_state = PS_EXIT;
      sfx(SFX_EXIT);
      hud_msg("EGRESS");
    }
    camera(0);
    draw_sprites();
  }
}

void play_run(void) BANKED {
  art_copy_tables(&meta_tile[0][0], meta_pal, pal_bg, pal_sp, people);
  people_colours();
  run_begin();
  floor_no = (dbg_floor && dbg_floor <= RUN_FLOORS) ? dbg_floor : 1u;
  tokens = 0;
  paused = 0;
  while (1) {
    if (!floor_play()) {
      HIDE_WIN;
      hud_line = 144u;
      run_end(0);
      dead_screen();
      return;
    }
    run_floor_done();
    if (floor_no >= RUN_FLOORS) {
      HIDE_WIN;
      hud_line = 144u;
      run_end(1);
      ending_screen();
      return;
    }
    if (pl_hp < pl_hp_max) pl_hp++;
    if (floor_no % 3u == 0u) {
      HIDE_WIN;
      hud_line = 144u;
      landing_screen();
      art_copy_tables(&meta_tile[0][0], meta_pal, pal_bg, pal_sp, people);
      people_colours();
    }
    floor_no++;
  }
}
