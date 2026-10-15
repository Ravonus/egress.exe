/* The B2 bosses' shared runtime (docs/redesign.md 7.5, 3.6.6, 2, 12b): the slot API, the hit intake, the phase
 * gates, the palette tells, the damage profile, the beat when a boss falls (its heart shard, then the floor) and the
 * line it says when it kills the heir.
 *
 * The rules every boss keeps (3.6.6 "punishing but fair"):
 *   - every attack has a tell of at least 24 frames: a pose, a palette flash (OBJ 7 on its sprites, the screen's BG
 *     palette on the Tenant) and its own sound (bc_tell);
 *   - the arena is one screen and the camera holds still, so nothing hits off screen;
 *   - no damage rolls: an attack's damage is fixed by phase (phase 3 slams take 2 hearts);
 *   - the boss only takes damage while it is open (bs_open): a resting stamp, an open screen, a stunned fax.
 *     Hits on it otherwise clink off (a spark, the clink), so the player learns the window.
 * The no-hit bot in test/harness/boss.py proves every hit avoidable. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "boss_int.h"
#include "heir.h"
#include "look.h"
#include "core/eg_input.h"

extern eg_input_t pin; /* player.c */

uint8_t boss_result, heart_shards, dbg_boss;
uint8_t boss_cause[3];
uint8_t bs_kind, bs_phase, bs_hp, bs_st, bs_t, bs_atk, bs_seq, bs_hits, bs_open, bs_face, bs_flash;
uint8_t bs_tx, bs_ty, bs_x, bs_y;
uint8_t bs_hx[2], bs_hy[2], bs_hs[2], bs_ht[2], bs_htx[2], bs_hty[2];
uint8_t bs_harm;
int8_t bs_vx, bs_vy;
uint16_t bs_clock;
uint8_t bs_dmg[ATK_N], bs_mist[ATK_N];
uint8_t bs_cause_atk, bs_shard_x, bs_shard_y, bs_drop_row, bs_pal_dirty, bs_dim;
static uint8_t seen_hits, seen_hp, hit_flash, said, env_atk, env_t;
static uint16_t pal_keep[16]; /* BG 0, 2, 3, 6 at full light (the lights going dims them) */
static const uint8_t keep_pal[4] = {0u, 2u, 3u, 6u};

/* --- the slot API --- */
void boss_setup(uint8_t slot, uint8_t biome) BANKED {
  enemy_t *e = &en[0];
  uint8_t i;
  (void)biome; /* B2: the office only */
  bs_kind = slot;
  bs_phase = bs_st = bs_t = bs_atk = bs_seq = bs_hits = bs_open = bs_flash = bs_harm = 0;
  bs_face = BA_FACE_CALM;
  bs_pal_dirty = bs_dim = 0;
  bs_clock = 0;
  bs_drop_row = AR_Y1;
  boss_result = 0;
  said = hit_flash = env_atk = env_t = 0;
  memset(bs_dmg, 0, sizeof bs_dmg);
  memset(bs_mist, 0, sizeof bs_mist);
  memset(bs_hs, 0, sizeof bs_hs);
  memset(boss_cause, 0, sizeof boss_cause);
  for (i = 0; i != (uint8_t)EG_N; i++) room.c[i] = T_WALL;
  room.keys = 0;
  room.tokens = 0;
  room.nspawn = 0;
  room.nitem = 0;
  memset(room.prot, 0, sizeof room.prot);
  nen = 1;
  enemies_alive = 1;
  memset(e, 0, sizeof *e);
  e->kind = SPAWN_BOSS;
  e->st = ES_FLOAT;
  e->hp = 255u;
  for (i = 1; i != MAXE; i++) en[i].st = ES_DEAD;
  if (slot == BOSS_BIOME) tenant_setup();
  else fax_setup();
  seen_hits = acts.hits_taken;
}

void boss_show(void) BANKED {
  uint8_t k, j;
  boss_art_load(bs_kind);
  boss_art_pals(bs_kind, &pal_bg[4], &pal_bg[28], &pal_sp[12]);
  for (k = 0; k != 4u; k++)
    for (j = 0; j != 4u; j++) pal_keep[k * 4u + j] = pal_bg[keep_pal[k] * 4u + j];
  /* the Tenant's body arrives once the room has (BS_INTRO), so the assembly can't draw over it */
}

void orbs_clear(void) BANKED {
  uint8_t k;
  for (k = 0; k != MAXO; k++)
    if (orbs[k].live) {
      orbs[k].live = 0;
      puff((uint8_t)(orbs[k].x >> 4), (uint8_t)(orbs[k].y >> 4), 5, ST_PUFF);
    }
}

/* --- living obstacles: b2-obstacles answers these; until then nothing does --- */
uint8_t boss_obstacle_cmd(uint8_t cmd, uint8_t cell, uint8_t rule) BANKED {
  (void)cmd;
  (void)cell;
  (void)rule;
  return 0;
}
void boss_obstacles_clear(void) BANKED {}

/* --- helpers --- */
uint8_t bc_floor_at(uint8_t x, uint8_t y) BANKED {
  uint8_t c = cell_at(x, y), t = cell_type(c);
  return (uint8_t)(EG_PASSABLE(t) && look_here(c));
}

void bc_tell(uint8_t sfx_id) BANKED {
  sfx(sfx_id);
  bs_flash = 24u;
  bs_pal_dirty |= 1u;
}

void bc_face(uint8_t v) BANKED {
  if (bs_face == v) return;
  bs_face = v;
  bs_pal_dirty |= 4u;
}

/* the hit lands through player_hurt (i-frames, the parry's guard, invulnerability all apply there); returns 0 no
 * effect, 1 hurt, 2 the guard took it, 3 a perfect parry took it */
uint8_t bc_hurt_r(uint8_t n, uint8_t atk, uint8_t fromx, uint8_t fromy) BANKED {
  uint8_t g = (uint8_t)(pin.flags & EG_FL_GUARD), p = (uint8_t)(pin.flags & EG_FL_PERFECT), h0 = acts.hits_taken;
  if (pl_inv || pl_iframe || !pl_hp || pl_state == PS_EXIT) return 0;
  bs_cause_atk = atk;
  player_hurt(n, fromx, fromy);
  if (acts.hits_taken != h0) return 1u;
  return g ? (p ? 3u : 2u) : 0u;
}
void bc_hurt(uint8_t n, uint8_t atk, uint8_t fromx, uint8_t fromy) BANKED { bc_hurt_r(n, atk, fromx, fromy); }

/* a shelf's drywall gives way: the CRT above it falls down the column on the next gravity step */
void bc_break_shelf(uint8_t c) BANKED {
  if (cell_type(c) != T_BRICK) return;
  room.c[c] = T_EMPTY;
  queue_cell(c);
  puff((uint8_t)(EG_X(c) * 16u + 8u), (uint8_t)(EG_Y(c) * 16u + 8u), 4, ST_PUFF);
  sfx(SFX_BREAK);
  grav_t = 1u;
  env_atk = ATK_QUAKE;
  env_t = 120u;
}

/* --- damage to the boss --- */
void boss_hit(uint8_t dmg, uint8_t fromx, uint8_t fromy) BANKED {
  (void)fromx;
  (void)fromy;
  if (bs_st != BS_ATTACK && bs_st != BS_IDLE) return;
  if (!bs_open) { /* armour, a shut screen: it clinks off */
    sfx(SFX_B_CLINK);
    puff((uint8_t)(en[0].x >> 4), (uint8_t)((en[0].y >> 4) - 6u), 5, ST_SPARK);
    freeze = 2u;
    return;
  }
  dmg = dmg >= 8u ? 4u : (dmg >= 3u ? 2u : 1u); /* a crush 4, a heavy hit 2, anything else 1: good hits count */
  en[0].flash = 8u;
  hit_flash = 6u;
  bs_pal_dirty |= 1u;
  freeze = dmg >= 2u ? 5u : 3u;
  sfx(SFX_HIT);
  if (bs_hits != 255u) bs_hits++;
  if (bs_hp <= dmg) {
    bs_hp = 0;
    bc_gate();
  } else
    bs_hp = (uint8_t)(bs_hp - dmg);
}

/* the phase's HP is gone */
void bc_gate(void) BANKED {
  uint8_t last = (uint8_t)(bs_kind == BOSS_BIOME ? 2u : 1u);
  bs_open = 0;
  orbs_clear();
  if (bs_phase < last) {
    bs_phase++;
    bs_st = BS_GATE;
    bs_t = 0;
    bs_clock = 0;
    sfx(SFX_B_ROAR);
    shake = 20u;
    if (bs_kind == BOSS_BIOME) tenant_gate(bs_phase);
    else fax_gate(bs_phase);
    return;
  }
  /* down: the beat */
  bs_st = BS_DOWN;
  bs_t = 90u;
  shake = 30u;
  freeze = 20u;
  sfx(SFX_B_DOWN);
  boss_obstacles_clear();
  hud_msg(bs_kind == BOSS_BIOME ? "THE TENANT LETS GO." : "LAST FAX: SENT.");
  if (bs_kind == BOSS_BIOME) bc_face(BA_FACE_OPEN);
}

/* falling CRTs on boss parts (after each gravity step): a crush is a heavy hit, open or not */
void boss_crush(void) BANKED {
  uint8_t k, c;
  if (bs_st != BS_ATTACK && bs_st != BS_IDLE) return;
  for (k = 0; k != 3u; k++) {
    uint8_t x, y;
    if (k == 2u) {
      if (bs_kind != BOSS_MID) break;
      x = bs_x;
      y = bs_y;
    } else {
      if (bs_kind != BOSS_BIOME || bs_hs[k] != HS_REST) continue;
      x = bs_hx[k];
      y = bs_hy[k];
    }
    c = cell_at(x, (uint8_t)(y - 4u));
    if (cell_type(c) == T_ROCK && (room.c[c] & F_FALL)) {
      room.c[c] = T_EMPTY; /* the set shatters on it */
      queue_cell(c);
      puff(x, (uint8_t)(y - 8u), 4, ST_PUFF);
      shake = 10u;
      bs_open = 1u; /* a crush always lands */
      boss_hit(8u, x, (uint8_t)(y - 16u));
      if (bs_st == BS_ATTACK && k != 2u) bs_hs[k] = HS_BACK;
      return;
    }
  }
}

/* --- per frame --- */
static uint16_t dimc(uint16_t c, uint8_t lvl) { /* lvl 0..8 of full */
  uint8_t r = (uint8_t)(c & 31u), g = (uint8_t)((c >> 5) & 31u), b = (uint8_t)((c >> 10) & 31u);
  r = (uint8_t)((r * lvl) >> 3);
  g = (uint8_t)((g * lvl) >> 3);
  b = (uint8_t)((b * lvl) >> 3);
  return (uint16_t)(r | ((uint16_t)g << 5) | ((uint16_t)b << 10));
}

void boss_set_dim(uint8_t d) BANKED {
  uint8_t k, j, lvl = (uint8_t)(8u - d);
  bs_dim = d;
  for (k = 0; k != 4u; k++)
    for (j = 0; j != 4u; j++) pal_bg[keep_pal[k] * 4u + j] = dimc(pal_keep[k * 4u + j], k == 2u ? 8u : lvl);
  bs_pal_dirty |= 8u;
}

static const char *const kill_line[3] = {"", "SEE ATTACHED.", "I WAS HERE FIRST."};

void boss_vblank(void) BANKED {
  static uint16_t p[4];
  uint8_t d = bs_pal_dirty, k;
  if (!bs_kind) return;
  bs_pal_dirty = 0;
  if (d & 16u) {
    boss_art_body();
    d |= 4u;
  }
  if (d & 4u) boss_art_face(bs_face);
  if (d & 8u)
    for (k = 0; k != 4u; k++) set_bkg_palette(keep_pal[k], 1, &pal_bg[keep_pal[k] * 4u]);
  if (bs_kind == BOSS_BIOME && (d & 1u)) {
    /* the body flashes white when hit; the screen burns hot on a tell */
    if (hit_flash) {
      p[0] = pal_bg[4];
      p[1] = pal_bg[5];
      p[2] = p[3] = 0x7fffu;
      set_bkg_palette(1, 1, p);
    } else
      set_bkg_palette(1, 1, &pal_bg[4]);
    if (bs_flash && (bs_flash & 4u)) {
      p[0] = pal_bg[28];
      p[1] = 0x111fu; /* the tell's red and yellow (OBJ 7's) */
      p[2] = 0x239fu;
      p[3] = 0x7fffu;
      set_bkg_palette(7, 1, p);
    } else
      set_bkg_palette(7, 1, &pal_bg[28]);
  }
  if (d & 2u) set_sprite_palette(3, 1, &pal_sp[12]);
  /* the heir fell to the boss: it says its line (12b.7), and the card's record */
  if (!pl_hp && !said) {
    said = 1u;
    boss_result = 2u;
    hud_msg(kill_line[bs_kind]);
    boss_cause[0] = (uint8_t)(0x00u | (2u << 4) | (3u << 6)); /* a plain hit (the biome's verb), owner BOSS, killer boss */
    boss_cause[1] = (uint8_t)(0xf0u | bs_kind);                 /* the boss id */
    boss_cause[2] = (uint8_t)(5u | ((uint8_t)(bs_phase + 1u) << 3)); /* PHASE n */
  }
}

/* the boss floor's frame, from boss_update (enemies_update) */
void boss_update_all(void) BANKED {
  uint8_t c;
  if (bs_flash) {
    bs_flash--;
    if (!(bs_flash & 3u)) bs_pal_dirty |= 1u;
  }
  if (hit_flash && !--hit_flash) bs_pal_dirty |= 1u;
  if (env_t && !--env_t) env_atk = 0;
  /* damage that didn't come through bc_hurt: orbs, a falling CRT, a fall: attribute it to the hazard */
  if (acts.hits_taken != seen_hits) {
    uint8_t a = bs_cause_atk ? bs_cause_atk : (env_atk ? env_atk : bs_atk);
    if (bs_mist[a] != 255u) bs_mist[a]++;
    if (seen_hp > pl_hp) bs_dmg[a] = (uint8_t)(bs_dmg[a] + (seen_hp - pl_hp));
    seen_hits = acts.hits_taken;
  }
  bs_cause_atk = 0;
  seen_hp = pl_hp;
  if (bs_clock != 0xffffu) bs_clock++;
  switch (bs_st) {
    case BS_INTRO:
      if (look_busy()) return;
      if (bs_kind == BOSS_BIOME) bs_pal_dirty |= 16u | 1u | 2u;
      else bs_pal_dirty |= 2u;
      bs_st = BS_IDLE;
      bs_t = 60u;
      hud_msg(bs_kind == BOSS_BIOME ? "THE TENANT" : "LAST FAX");
      sfx(SFX_B_ROAR);
      return;
    case BS_DOWN:
      if (bs_t == 60u && bs_kind == BOSS_BIOME) { /* the body comes apart: its cells fall into the void */
        for (c = 0; c != 4u; c++) {
          look_drop_cell(EG_IDX(TEN_CX + c, TEN_CY + 2u));
          look_drop_cell(EG_IDX(TEN_CX + c, TEN_CY + 1u));
          look_drop_cell(EG_IDX(TEN_CX + c, TEN_CY));
        }
      }
      if (!--bs_t) {
        bs_st = BS_SHARD;
        bs_t = 0;
        puff(bs_shard_x, bs_shard_y, 5, ST_SPARK);
        sfx(SFX_TOKEN);
      }
      return;
    case BS_SHARD:
      bs_t++;
      if (overlap(pl_x, pl_y, bs_shard_x, bs_shard_y, 12u, 14u)) {
        heart_shards++;
        sfx(SFX_KEY);
        if (heart_shards >= 4u) {
          heart_shards = (uint8_t)(heart_shards - 4u);
          if (pl_hp_max < 20u) pl_hp_max = (uint8_t)(pl_hp_max + 2u);
          pl_hp = pl_hp_max;
          hud_msg("A NEW HEART");
        } else
          hud_msg(heart_shards == 1u ? "A HEART SHARD 1/4" : (heart_shards == 2u ? "A HEART SHARD 2/4" :
                                                                                  "A HEART SHARD 3/4"));
        hud_dirty = 1u;
        bs_st = BS_EXIT;
        bs_t = 40u;
      }
      return;
    case BS_EXIT:
      if (bs_t && !--bs_t) {
        boss_result = 1u;
        if (bs_kind == BOSS_BIOME) { /* the floor lets go under you: into the next biome */
          pl_state = PS_EXIT;
          sfx(SFX_EXIT);
          hud_msg("THE FLOOR GIVES");
        } else
          open_door(); /* LAST FAX: the door north; play.c's exit takes it */
        en[0].st = ES_DEAD;
        enemies_alive = 0;
      }
      return;
    default:
      break;
  }
  if (bs_kind == BOSS_BIOME) tenant_update();
  else fax_update();
}
