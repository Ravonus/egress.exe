/* The ten-floor office slice: physical and elemental puzzles, fights, three landings,
 * LAST FAX on five and THE TENANT on ten. See core/eg_stage.c for the playable pacing.
 *
 * A floor is a 16x14 grid of 16 px cells drawn whole into the background map; the camera only scrolls. CRTs fall on
 * the shared eg_gravity step every 8 frames: a falling set crushes enemies and hurts the player. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "puzzle.h"
#include "run.h"
#include "heir.h"
#include "gen/eg_seeds.h"
#include "look.h"
#include "stage.h"       /* B2 */
#include "stage_spawn.h" /* B2 */
#include "boss_api.h" /* B2 bosses */
#include "obstacle.h"
#include "family.h"
#include "resume.h"
#include "narrator.h"
#include "stamina.h"
#include "tutorial.h"
void sprites_budget(void) NONBANKED;

uint8_t tokens, keys_held, door_open, board_busy, cam_x, cam_y;
uint8_t freeze, shake, hud_dirty, grav_t, boss_floor;
static uint8_t was_falling, sel_t, floor_tokens0;
static uint8_t restored_floor;
static uint8_t weight_cells[4],weight_n;
uint8_t run_loading;
uint8_t floor_has_crt;

static void controls_pause(void) {
  uint8_t old_y=cam_y;
  cam_y=pl_y>40u ? (uint8_t)(pl_y-40u) : 0u;
  if(cam_y>144u) cam_y=144u;
  move_bkg(BG_SCROLL_X(cam_x),cam_y);
  clear_map(1,LOOK_PAL_HUD);
  text_at(1,0,0,"A HIT / HOLD CHARGE",LOOK_PAL_HUD);
  text_at(1,0,1,"B SHOT / HOLD CHARGE",LOOK_PAL_HUD);
  text_at(1,0,2,"A>B COUNTER B>A RUSH",LOOK_PAL_HUD);
  text_at(1,0,3,"DIR x2 DODGE/STEP",LOOK_PAL_HUD);
  text_at(1,0,4,"HOLD DIR TO RUN",LOOK_PAL_HUD);
  text_at(1,0,5,"SELECT BENCH/TOOLS",LOOK_PAL_HUD);
  text_at(1,0,7,"START/B RESUME",LOOK_PAL_HUD);
  hud_line=80u;move_win(7,80);
  while(1) {
    frame_wait();look_tick();flush_cells(8u);sound_tick();draw_sprites();input_poll();
    if(joy_pressed&(J_START|J_B)) break;
  }
  cam_y=old_y;move_bkg(BG_SCROLL_X(cam_x),cam_y);
  clear_map(1,LOOK_PAL_HUD);hud_draw();hud_charge();move_win(7,128);hud_line=128u;
}


static void people_colours(void) { /* B1: the heir's palettes (heir.c), rolled from the run's seed */
  family_apply();
}

static void floor_build(void) {
  uint8_t i;
  /* B2 bosses: the slot (boss_api.h); b2-stages chooses which floors hold one */
  boss_floor = dbg_boss ? dbg_boss : (floor_no >= RUN_FLOORS ? BOSS_BIOME : (stage_kind == SK_MIDBOSS ? BOSS_MID : 0u));
  memset(&acts, 0, sizeof acts);
  memset(cracked, 0, sizeof cracked);
  memset(orbs, 0, sizeof orbs);
  memset(puffs, 0, sizeof puffs);
  if (boss_floor) {
    floor_tier = 5u;
    floor_seed = 0;
    boss_setup(boss_floor, BIOME_OFFICE);
  } else if (!stage_build()) {
    /* B1: the run planner chose floor_seed and floor_tier (puzzle_plan_floor); a reset regenerates the same room */
    if (!eg_generate(&room, floor_seed, floor_tier))
      for (i = 0;; i++) {
        floor_seed = eg_seed_pick(floor_tier, i);
        if (eg_generate(&room, floor_seed, floor_tier)) break;
      }
  }
  /* A room without CRTs cannot acquire one through ordinary reactions. A
   * queued new CRT promotes this flag; boss arenas keep their full physics. */
  floor_has_crt=0u;
  for(i=0u;i!=EG_N;i++)if(cell_type(i)==T_ROCK) {floor_has_crt=1u;break;}
  /* game randomness from here on: seeded by the run and the floor */
  eg_rs = (uint16_t)(run_seed ^ floor_seed ^ ((uint16_t)floor_no << 8));
  if (!eg_rs) eg_rs = 1u;
  keys_held = 0;
  door_open = 0;
  board_busy = 0;
  was_falling = 0;
  weight_n=0u;
  eg_plates_dirty=1u;
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
  player_floor_start(); /* B1: the resolver, the mover, the spawn */
  freeze = shake = 0;
  grav_t = GRAVITY_FRAMES;
  if (!boss_floor) { bs_kind = 0; actors_spawn(); }
  cells_reset();
  pocket_reset(); /* B1 */
  obstacle_reset();
  stage_start();  /* B2 */
}

static uint8_t camera_follow(uint8_t current,uint8_t target) {
  uint8_t distance;
  if(target>current) {distance=(uint8_t)(target-current);return (uint8_t)(current+(distance>=4u ? distance>>2 : 1u));}
  if(target<current) {distance=(uint8_t)(current-target);return (uint8_t)(current-(distance>=4u ? distance>>2 : 1u));}
  return current;
}
static void camera(uint8_t snap) {
  uint8_t tx,ty;
  if (boss_floor) { /* B2 bosses: the arena is one screen, held still */
    cam_x = BOSS_CAM_X;
    cam_y = BOSS_CAM_Y;
    return;
  }
  tx=pl_x<=80u ? 0u : pl_x>=176u ? 96u : (uint8_t)(pl_x-80u);
  ty=pl_y<=64u ? 0u : pl_y>=160u ? 96u : (uint8_t)(pl_y-64u);
  if (snap) {
    cam_x = (uint8_t)tx;
    cam_y = (uint8_t)ty;
    return;
  }
  cam_x=camera_follow(cam_x,tx);
  cam_y=camera_follow(cam_y,ty);
}

static void scroll(void) {
  uint8_t sx = cam_x, sy = cam_y;
  if (shake) {
    shake--;
    sx = (uint8_t)(sx + ((frame_count & 2u) ? 1u : 255u));
    sy = (uint8_t)(sy + ((frame_count & 4u) ? 1u : 255u));
  }
  move_bkg(BG_SCROLL_X(sx), sy);
}

static void floor_show(void) {
  uint8_t i;
  DISPLAY_OFF;
  HIDE_SPRITES;
  SPRITES_8x16; /* floors own 8x16 OBJs; the title uses 8x8 for its prompt */
  art_load_game();
  /* look: the office's cards and palettes; the room on screen will assemble around the player */
  look_load(BIOME_OFFICE);
  stage_show(); /* B2: the stage's cards and skins */
  camera(1);
  look_roam_begin((uint8_t)(!boss_floor && stage_type!=SG_DARK && stage_type!=SG_CHASE));
  look_assemble(room.start);
  for (i = 0; i != (uint8_t)EG_N; i++) draw_cell(i);
  if (boss_floor) boss_show(); /* B2 bosses */
  hud_init();
  scroll();
  sprites_reset();
  obstacle_show();
  draw_sprites();
  SHOW_BKG;
  SHOW_WIN;
  SHOW_SPRITES;
  pal_commit(0);
  DISPLAY_ON;
}

static void gravity_tick(void) {
  uint8_t occ[4], n = 0, c, k, g, a, b, weights=0u;
  uint8_t xs[2], ys[2];
  xs[0] = (uint8_t)(pl_x - 6u); /* the mover's 12x12 box (eg_move) */
  xs[1] = (uint8_t)(pl_x + 5u);
  ys[0] = (uint8_t)(pl_y - 6u);
  ys[1] = (uint8_t)(pl_y + 5u);
  for (a = 0; a != 2u; a++)
    for (b = 0; b != 2u; b++) {
      c = cell_at(xs[b], ys[a]);
      for (k = 0; k != n; k++)
        if (occ[k] == c) break;
      if (k == n) occ[n++] = c;
    }
  /* Compare only grounded plate contacts, rather than invalidating the full
   * shutter scan on every subpixel movement. Keep the complete body footprint
   * for falling CRTs and the occupied-gate safety check. */
  if(pl_hp && !pl_air && pl_state!=PS_FALL)
    for(k=0u;k!=n;k++) if(room.c[occ[k]]==T_PLATE) {
      if(weights>=weight_n || weight_cells[weights]!=occ[k]) {
        eg_plates_dirty=1u;queue_cell(occ[k]);
        if(weights<weight_n) queue_cell(weight_cells[weights]);
      }
      weight_cells[weights++]=occ[k];
    }
  if(weights!=weight_n) {eg_plates_dirty=1u;for(k=weights;k<weight_n;k++) queue_cell(weight_cells[k]);}
  weight_n=weights;
  g = floor_has_crt || boss_floor ? eg_gravity(&room, occ, pl_hp && !pl_air && pl_state!=PS_FALL ? n : 0u)
                                : eg_shutters(&room, occ, pl_hp && !pl_air && pl_state!=PS_FALL ? n : 0u);
  if (eg_ndirty > EG_DIRTY_MAX) {
    for (c = 0; c != (uint8_t)EG_N; c++)
      if (cell_type(c) == T_ROCK || cell_type(c) == T_EMPTY) queue_cell(c);
  } else
    for (k = 0; k != eg_ndirty; k++) {
      c=eg_dirty[k];queue_cell(c);
      if(cell_type(c)==T_GATE || cell_type(c)==T_GATE_OPEN) {
        sfx(SFX_DOOR);puff((uint8_t)(EG_X(c)*16u+8u),(uint8_t)(EG_Y(c)*16u+8u),4u,ST_SPARK);
      }
    }
  if (g & EG_G_HIT) player_hurt(2u, pl_x, (uint8_t)(pl_y - 16u));
  if (g & EG_G_BROKE) puzzle_dirty = 1u; /* B1 */
  board_busy = (uint8_t)((g & EG_G_FALLING) ? 1u : 0u);
  if (was_falling && !board_busy) {
    narrator_event(NE_DROP,0u);
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
  uint8_t end_t = 0,roaming,late;
  floor_tokens0 = tokens;
  pocket_enter();
  if(restored_floor) {stage_resume_floor();restored_floor=0u;narrator_event(NE_RETURN,0u);}
  else {
    if (floor_no < RUN_FLOORS) stage_plan_floor(); /* B1, B2: the slot, the stage type, the room */
    if(family.nrecord && !family.history[0].won && family.history[0].floor==floor_no) narrator_event(NE_ANCESTOR,0u);
  }
  resume_save(RES_FLOOR);
  floor_build();
  floor_show();
  music_play(boss_floor ? SONG_BOSS : SONG_FLOOR);
  music_fade(0);
  fade_to(0, 8);
  assemble_wait();
  game_mode = MODE_PLAY;
  roaming=(uint8_t)(!boss_floor && stage_type!=SG_DARK && stage_type!=SG_CHASE);
  if (boss_floor) hud_msg("IT SEES YOU.");
  else if (stage_announce()) {} /* B2: the stage's objective */
  else if (floor_no == 1u) hud_msg("FIND THE DOOR");
  else hud_msg("ANOTHER FLOOR");
  while (1) {
    frame_wait();
    input_poll(); /* sample before variable-cost VRAM/audio work can cross a display boundary */
    DISABLE_OAM_DMA;
    late=(uint8_t)(roaming && (enemies_alive || stage_type!=SG_PUZZLE));
    if (boss_floor) boss_vblank(); /* B2 bosses: in VBlank, before the look's writes */
    if (pal_dirty && !look_busy()) {
      set_sprite_palette(2u, 6u, &pal_sp[8]);
      pal_dirty = 0;
    }
    if(late) {
      /* Combat renders actors before spending spare time on cosmetic terrain. */
      look_frame_begin();flush_cells(2u);
    } else if(roaming) {flush_cells(2u);look_tick();}
    else {look_tick();flush_cells(8u);}
    scroll();
    sound_tick();
    hud_tick();
    if (pl_state == PS_EXIT) {
      if(late) look_frame_end();
      sprites_budget();
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
      if(pl_state==PS_FALL) player_update();
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
      if(late) look_frame_end();
      continue;
    }
    if (joy_pressed & J_START) {
      controls_pause();
      continue;
    }
    /* hold SELECT to put the floor back the way it was (B1: it costs a token, never a heart) */
    if ((joy & J_SELECT) && !boss_floor) {
      if (sel_t != 61u && ++sel_t == 60u) { /* 61: still held from closing the bench */
        sel_t = 0;
        tokens = floor_tokens0;
        puzzle_reset_cost(); /* B1: a token and a lean of the matrix, never a heart */
        look_freeze();
        fade_to(8, 0);
        pocket_restore();
        floor_build();
        floor_show();
        fade_to(0, 8);
        assemble_wait();
        hud_msg("IT RESETS.");
        continue;
      }
    } else {
      if (sel_t && sel_t < 20u && !boss_floor && pl_state == PS_FREE) { /* B1: a tap of SELECT opens the bench */
        if (!stage_select()) bench_run(); /* B2: the heist's desk, the ring's bench */
        sel_t = 61u;
        continue;
      }
      sel_t = 0;
    }
    if (acts.frames != 0xffffu) acts.frames++;
    if (freeze) {
      freeze--;
      if(late) look_frame_end();
      sprites_budget();
      continue;
    }
    player_update();
    stage_tick(); /* B2 */
    puzzle_tick(); /* B1: fragile floor, the beat, the dead-room hint */
    obstacle_tick();
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
    if(!boss_floor) look_roam_tick();
    tutorial_tick();
    if(!(frame_count&63u)) narrator_tick();
    draw_sprites();
    if(late) look_frame_end();
  }
}

void play_run(void) BANKED {
  run_loading=1u;
  art_copy_tables(&meta_tile[0][0], meta_pal, pal_bg, pal_sp, people);
  run_begin();
  narrator_begin();
  family_prepare();
  if(resume_finish_pending()) {run_begin();narrator_begin();}
  if(resume_new) {
    if(resume_restore()) run_record_end(0u);
    resume_new=0u;run_begin();narrator_begin();family_prepare();
  }
  run_loading=0u;
  family_begin();
  stamina_begin();
  pocket_begin();
  puzzle_plan_begin(); /* B1 */
  stage_run_begin();   /* B2 */
  restored_floor=resume_restore();
  if(!restored_floor) {
    floor_no = (dbg_floor && dbg_floor <= RUN_FLOORS) ? dbg_floor : 1u;
    tokens = 0;resume_rest=0u;
    narrator_event(NE_HEIR,0u);
  } else if(resume_state==RES_LANDING) {
    restored_floor=0u;art_load_game();stage_landing();
    art_copy_tables(&meta_tile[0][0],meta_pal,pal_bg,pal_sp,people);people_colours();floor_no++;
  }
  while (1) {
    if (!floor_play()) {
      HIDE_WIN;
      hud_line = 144u;
      run_record_end(0u);
      dead_screen();
      return;
    }
    if (floor_no < RUN_FLOORS) stage_floor_done(); /* B1, B2 */
    run_floor_done();
    narrator_event(NE_CLEAR,0u);
    if (floor_no >= RUN_FLOORS) {
      HIDE_WIN;
      hud_line = 144u;
      run_record_end(1u);
      ending_screen();
      return;
    }
    if (pl_hp < pl_hp_max) pl_hp++;
    if (floor_no % 3u == 0u) {
      HIDE_WIN;
      hud_line = 144u;
      resume_rest=0u;resume_save(RES_LANDING);
      stage_landing(); /* B2: the landing, then the rest */
      art_copy_tables(&meta_tile[0][0], meta_pal, pal_bg, pal_sp, people);
      people_colours();
    }
    floor_no++;
  }
}
