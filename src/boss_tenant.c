/* THE TENANT, the office's biome boss (docs/redesign.md 7.5): it lives in the arena's north wall, a filing machine
 * with a CRT for a face, and it fights with two rubber stamps. Its body is background tiles; the stamps are sprites.
 *
 * Three phases, each demanding a different move (7.5), each gated by the room (bc_gate -> tenant_gate):
 *   1  dodge:  SLAM (a stamp's shadow follows you, holds still, the stamp drops: walk out or roll) and SWEEP (a stamp
 *              glows at a wall on your row, then crosses it: change rows, roll through, or perfect-parry it to a
 *              stop). A stamp that lands rests on the floor, open: hit it.
 *   -> THE LIGHTS GO: the room dims to half, the screen glows.
 *   2  parry:  VOLLEY (three orbs from the screen: parry them back into it), BEAM (a marked column burns: leave it,
 *              then the screen hangs open), QUAKE (a shelf cracks and its CRT drops down that column), and the
 *              stamps as before.
 *   -> THE FLOOR GOES: the two bottom rows crack, then fall into the void.
 *   3  sprint: DOUBLE SLAM (two shadows; each slam takes 2 hearts; the stamps land armoured and the screen opens for
 *              a short window: sprint and lunge to it), SWEEP x2 (two rows, from both walls), BEAM, QUAKE. After 45 s
 *              the enrage drops another row every 6 s (each with its crack tell).
 * A falling CRT that lands on a resting stamp is a crush: it always lands (4). */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "boss_int.h"
#include "look.h"

static const uint8_t home_x[2] = {72u, 184u}, home_y = 84u;
/* the phase scripts: attack ids, the stamp (0 left, 1 right, 2 both) packed in the top bits */
#define A(atk, h) ((uint8_t)((atk) | ((h) << 6)))
static const uint8_t script0[] = {A(ATK_SLAM, 0), A(ATK_SLAM, 1), A(ATK_SWEEP, 0), A(ATK_SLAM, 1), A(ATK_SWEEP, 1)};
static const uint8_t script1[] = {A(ATK_VOLLEY, 0), A(ATK_SLAM, 0), A(ATK_BEAM, 0), A(ATK_QUAKE, 0),
                                  A(ATK_VOLLEY, 0), A(ATK_SWEEP, 1), A(ATK_BEAM, 0), A(ATK_SLAM, 1)};
static const uint8_t script2[] = {A(ATK_SLAM, 2), A(ATK_SWEEP, 2), A(ATK_BEAM, 0), A(ATK_SLAM, 2), A(ATK_QUAKE, 0),
                                  A(ATK_SWEEP, 2)};
static const uint8_t *const scripts[3] = {script0, script1, script2};
static const uint8_t script_n[3] = {sizeof script0, sizeof script1, sizeof script2};
static const uint8_t gap[3] = {40u, 34u, 28u};
/* tells (>= 24 frames, 3.6.6) and how long a shadow follows before it holds still */
static const uint8_t slam_tell[3] = {36u, 32u, 30u}, slam_track[3] = {16u, 12u, 10u};
static const uint8_t sweep_tell[3] = {28u, 26u, 26u};
#define BEAM_TELL 40u
#define BEAM_TRACK 16u
#define BEAM_ON 20u
#define VOLLEY_TELL 30u
#define QUAKE_TELL 40u
#define CRACK_T 48u
static uint8_t shot_n, quake_c, slam_lag;
uint8_t tn_beam_on, tn_floor_t; /* not static: the harness reads them */

void tenant_setup(void) BANKED {
  uint8_t x, y;
  for (y = 5; y <= AR_Y1; y++)
    for (x = AR_X0; x <= AR_X1; x++) room.c[EG_IDX(x, y)] = T_EMPTY;
  for (y = TEN_CY; y != TEN_CY + 3u; y++)
    for (x = TEN_CX; x != TEN_CX + 4u; x++) room.c[EG_IDX(x, y)] = T_WALL; /* the body: its cells stay wall */
  for (x = 4; x != 12u; x++) { /* the shelves: CRT sets on drywall either side of it */
    if (x == 6u) x = 10u;
    room.c[EG_IDX(x, 3)] = T_ROCK;
    room.c[EG_IDX(x, 4)] = T_BRICK;
  }
  room.door = EG_IDX(7, 1); /* off screen: the Tenant's floor is left by falling, not by a door */
  room.c[room.door] = T_DOOR;
  room.start = EG_IDX(7, 9);
  bs_hp = 8u;
  bs_x = TEN_FACE_X;
  bs_y = TEN_FACE_Y;
  bs_hx[0] = home_x[0];
  bs_hx[1] = home_x[1];
  bs_hy[0] = bs_hy[1] = home_y;
  bs_shard_x = 128u;
  bs_shard_y = 112u;
  en[0].x = (uint16_t)TEN_FACE_X << 4;
  en[0].y = (uint16_t)90u << 4;
  shot_n = tn_beam_on = tn_floor_t = slam_lag = 0;
  quake_c = 255u;
}

/* --- the floor: rows crack (tell), then fall --- */
static void crack_row(uint8_t y) {
  uint8_t x;
  for (x = AR_X0; x <= AR_X1; x++)
    if (cell_type(EG_IDX(x, y)) == T_EMPTY) look_skin_set(EG_IDX(x, y), M_SLAB_CRACK);
  sfx(SFX_B_RUMBLE);
  shake = 6u;
}
static void drop_row(uint8_t y) {
  uint8_t x, c;
  for (x = AR_X0; x <= AR_X1; x++) {
    c = EG_IDX(x, y);
    if (cell_type(c) == T_EMPTY || cell_type(c) == T_ROCK) {
      /* The collapse queue owns the redraw; avoid scheduling the same cell
       * through both the skin-redraw queue and the falling-card queue. */
      look_skin[c] = 0;
      room.c[c] = T_PIT;
      look_drop_cell(c);
    }
  }
  sfx(SFX_THUD);
  shake = 10u;
}

void tenant_gate(uint8_t to) BANKED {
  bs_hs[0] = bs_hs[1] = HS_BACK;
  bs_harm = 0;
  tn_beam_on = 0;
  bc_face(BA_FACE_TELL);
  if (to == 1u) {
    hud_msg("THE LIGHTS GO");
    /* 7.7: the Tenant brings in a MOVER when b2-obstacles is there to answer */
    boss_obstacle_cmd(BOC_SUMMON, EG_IDX(4, 8), 1u);
  } else {
    hud_msg("THE FLOOR GOES");
    crack_row(AR_Y1 - 1u);
    crack_row(AR_Y1);
  }
}

/* --- the stamps --- */
static void toward(uint8_t *v, uint8_t to, uint8_t sp) {
  if (*v + sp < to) *v = (uint8_t)(*v + sp);
  else if (*v > to + sp) *v = (uint8_t)(*v - sp);
  else *v = to;
}

static void slam_start(uint8_t h, uint8_t lag) {
  bs_hs[h] = HS_RISE;
  bs_ht[h] = (uint8_t)(slam_tell[bs_phase] + lag);
  bs_htx[h] = pl_x;
  bs_hty[h] = pl_y;
  if (!lag) bc_tell(SFX_B_RISE);
}

static uint8_t clamp_row(uint8_t y) {
  uint8_t lo = 88u, hi = bs_phase == 2u ? (uint8_t)(bs_drop_row * 16u - 24u) : 168u;
  if (hi < 104u) hi = 104u;
  y = (uint8_t)((y & 0xf0u) | 8u); /* a row's centre */
  return y < lo ? lo : (y > hi ? hi : y);
}

static void sweep_start(uint8_t h, uint8_t row, uint8_t from_right, uint8_t lag) {
  bs_hs[h] = HS_PREP;
  bs_hty[h] = row;
  bs_htx[h] = from_right ? 200u : 56u;
  bs_ht[h] = (uint8_t)(sweep_tell[bs_phase] + lag);
}

static void stamp(uint8_t h) {
  uint8_t *x = &bs_hx[h], *y = &bs_hy[h], r;
  switch (bs_hs[h]) {
    case HS_RISE: /* the shadow follows, then holds; the stamp hangs over it, winding up */
      if (bs_ht[h] > (uint8_t)(slam_tell[bs_phase] - slam_track[bs_phase])) {
        bs_htx[h] = pl_x;
        bs_hty[h] = pl_y < 92u ? 92u : pl_y;
      }
      if (bs_ht[h] == slam_tell[bs_phase] && h && bs_atk == ATK_SLAM && bs_phase == 2u) bc_tell(SFX_B_RISE);
      toward(x, bs_htx[h], 5u);
      toward(y, (uint8_t)(bs_hty[h] - 22u), 5u);
      if (!--bs_ht[h]) {
        bs_hs[h] = HS_DROP;
        bs_ht[h] = 4u;
      }
      break;
    case HS_DROP:
      *x = bs_htx[h];
      *y = (uint8_t)(bs_hty[h] - (uint8_t)(bs_ht[h] * 5u));
      if (!--bs_ht[h]) {
        *y = bs_hty[h];
        shake = 6u;
        sfx(SFX_B_SLAM);
        puff((uint8_t)(*x - 8u), (uint8_t)(*y + 4u), 2, ST_PUFF);
        puff((uint8_t)(*x + 8u), (uint8_t)(*y + 4u), 2, ST_PUFF);
        r = 0;
        if (overlap(pl_x, pl_y, *x, *y, 15u, 13u)) r = bc_hurt_r(bs_phase == 2u ? 4u : 2u, ATK_SLAM, *x, *y);
        bs_hs[h] = HS_REST;
        bs_ht[h] = bs_harm ? 34u : 46u;
        if (r == 3u) { /* a perfect parry on the drop: it staggers, open longer */
          bs_ht[h] = 72u;
          sfx(SFX_CHARGED);
        }
      }
      break;
    case HS_REST:
      if (!--bs_ht[h]) bs_hs[h] = HS_BACK;
      break;
    case HS_PREP: /* to the wall on the row, then glow there: the tell */
      toward(x, bs_htx[h], 6u);
      toward(y, bs_hty[h], 6u);
      if (*x == bs_htx[h] && *y == bs_hty[h]) {
        if (bs_ht[h] == sweep_tell[bs_phase]) bc_tell(SFX_B_WHOOSH);
        if (!--bs_ht[h]) bs_hs[h] = HS_SWEEP;
      }
      break;
    case HS_SWEEP: {
      uint8_t sp = bs_phase == 2u ? 4u : 3u;
      if (bs_htx[h] > 128u) {
        *x = (uint8_t)(*x - sp);
        if (*x <= 44u) bs_hs[h] = HS_BACK;
      } else {
        *x = (uint8_t)(*x + sp);
        if (*x >= 212u) bs_hs[h] = HS_BACK;
      }
      if (overlap(pl_x, pl_y, *x, *y, 13u, 11u) && bc_hurt_r(2u, ATK_SWEEP, *x, *y) == 3u) {
        bs_hs[h] = HS_REST; /* parried to a stop */
        bs_ht[h] = 56u;
        sfx(SFX_CHARGED);
        shake = 4u;
      }
      break;
    }
    case HS_BACK:
      toward(x, home_x[h], 4u);
      toward(y, home_y, 4u);
      if (*x == home_x[h] && *y == home_y) bs_hs[h] = HS_HOME;
      break;
    default:
      break;
  }
}

/* --- the screen --- */
static void beam_hit(void) {
  if (pl_y >= 92u && absdiff(pl_x, bs_tx) < 11u) bc_hurt_r(2u, ATK_BEAM, bs_tx, pl_y);
}

static void next_attack(void) {
  uint8_t a = scripts[bs_phase][bs_seq], h = (uint8_t)(a >> 6), k, best, d;
  if (++bs_seq >= script_n[bs_phase]) bs_seq = 0;
  bs_atk = (uint8_t)(a & 63u);
  bs_st = BS_ATTACK;
  bs_t = 0;
  slam_lag = 0;
  switch (bs_atk) {
    case ATK_SLAM:
      if (h == 2u) {
        slam_start(0, 0);
        slam_start(1, 22u);
        bs_harm = 1u;
      } else
        slam_start(h, 0);
      break;
    case ATK_SWEEP: {
      uint8_t row = clamp_row(pl_y), right = (uint8_t)(pl_x < 128u);
      if (h == 2u) {
        uint8_t row2 = (uint8_t)(row >= 120u ? row - 16u : row + 16u);
        sweep_start(0, row, right, 0);
        sweep_start(1, row2, (uint8_t)!right, 18u);
      } else
        sweep_start(h, row, right, 0);
      break;
    }
    case ATK_VOLLEY:
      bs_t = VOLLEY_TELL;
      shot_n = 0;
      bc_face(BA_FACE_TELL);
      bc_tell(SFX_TELL);
      break;
    case ATK_BEAM:
      bs_t = BEAM_TELL;
      bs_tx = pl_x;
      tn_beam_on = 0;
      bc_face(BA_FACE_TELL);
      bc_tell(SFX_B_BEAM);
      break;
    case ATK_QUAKE: /* the shelf nearest the heir that still holds a set */
      best = 255u;
      quake_c = 255u;
      for (k = 4; k != 12u; k++) {
        if (k == 6u) k = 10u;
        if (cell_type(EG_IDX(k, 4)) != T_BRICK || cell_type(EG_IDX(k, 3)) != T_ROCK) continue;
        d = absdiff((uint8_t)(k * 16u + 8u), pl_x);
        if (d < best) {
          best = d;
          quake_c = EG_IDX(k, 4);
        }
      }
      if (quake_c == 255u) { /* the shelves are bare: a slam instead */
        bs_atk = ATK_SLAM;
        slam_start(0, 0);
        break;
      }
      bs_tx = (uint8_t)(EG_X(quake_c) * 16u + 8u);
      cracked[quake_c >> 3] |= (uint8_t)(1u << (quake_c & 7u));
      queue_cell(quake_c);
      bs_t = QUAKE_TELL;
      bc_face(BA_FACE_TELL);
      bc_tell(SFX_B_RUMBLE);
      break;
    default:
      break;
  }
}

void tenant_update(void) BANKED {
  uint8_t h, busy;
  if (bs_st == BS_GATE) {
    bs_t++;
    stamp(0);
    stamp(1);
    if (bs_phase == 1u) { /* the lights go down in four steps */
      if (bs_t == 20u || bs_t == 32u || bs_t == 44u || bs_t == 56u) boss_set_dim((uint8_t)((bs_t - 8u) / 12u));
    } else if (bs_t == CRACK_T + 8u) {
      drop_row(AR_Y1 - 1u);
      drop_row(AR_Y1);
      bs_drop_row = AR_Y1 - 1u; /* rows from here down are gone */
    }
    if (bs_t >= 100u && bs_hs[0] == HS_HOME && bs_hs[1] == HS_HOME) {
      bs_st = BS_IDLE;
      bs_t = 30u;
      bs_hp = 8u;
      bc_face(BA_FACE_CALM);
    }
    return;
  }
  /* the enrage (phase 3): after 45 s a row cracks every 6 s, then falls */
  if (bs_phase == 2u && bs_clock > 2700u && bs_drop_row > 7u) {
    if (!tn_floor_t) {
      tn_floor_t = 1u;
      crack_row((uint8_t)(bs_drop_row - 1u));
    } else if (++tn_floor_t == CRACK_T) {
      bs_drop_row--;
      drop_row(bs_drop_row);
    } else if (tn_floor_t >= 200u)
      tn_floor_t = 0;
  }
  stamp(0);
  stamp(1);
  /* the weak point: a resting stamp (not armoured), or the open screen */
  bs_open = 0;
  for (h = 0; h != 2u; h++)
    if (bs_hs[h] == HS_REST && !bs_harm) {
      bs_open = 1u;
      en[0].x = (uint16_t)bs_hx[h] << 4;
      en[0].y = (uint16_t)bs_hy[h] << 4;
    }
  if (!bs_open) {
    en[0].x = (uint16_t)TEN_FACE_X << 4;
    en[0].y = (uint16_t)90u << 4;
    bs_open = (uint8_t)(bs_face == BA_FACE_OPEN || (bs_atk == ATK_VOLLEY && bs_st == BS_ATTACK && !bs_t));
  }
  busy = (uint8_t)(bs_hs[0] != HS_HOME || bs_hs[1] != HS_HOME);
  switch (bs_st) {
    case BS_IDLE:
      if (bs_face == BA_FACE_OPEN && bs_t < 2u) bc_face(BA_FACE_CALM);
      if (!--bs_t) next_attack();
      return;
    case BS_ATTACK:
      break;
    default:
      return;
  }
  switch (bs_atk) {
    case ATK_SLAM:
      if (bs_harm && bs_hs[1] == HS_REST && bs_face != BA_FACE_OPEN && !slam_lag) {
        bc_face(BA_FACE_OPEN); /* both stamps down in armour: the screen opens, briefly */
        slam_lag = 54u;
      }
      if (slam_lag && !--slam_lag) bc_face(BA_FACE_CALM);
      if (!busy && !slam_lag) {
        bs_harm = 0;
        bc_face(BA_FACE_CALM);
        bs_st = BS_IDLE;
        bs_t = gap[bs_phase];
      }
      break;
    case ATK_SWEEP:
      if (!busy) {
        bs_st = BS_IDLE;
        bs_t = gap[bs_phase];
      }
      break;
    case ATK_VOLLEY:
      if (bs_t) {
        if (!--bs_t) bc_face(BA_FACE_OPEN);
        break;
      }
      if (shot_n < 3u) {
        if (!(slam_lag++ % 12u)) {
          fire_orb_at(TEN_FACE_X, 96u, (int16_t)pl_x - TEN_FACE_X, (int16_t)pl_y - 96, 18u);
          sfx(SFX_SHOT);
          shot_n++;
        }
      } else if (++slam_lag >= 36u + 60u) {
        slam_lag = 0;
        bc_face(BA_FACE_CALM);
        bs_st = BS_IDLE;
        bs_t = gap[bs_phase];
      }
      break;
    case ATK_BEAM:
      if (bs_t) {
        if (bs_t > BEAM_TELL - BEAM_TRACK) bs_tx = pl_x;
        if (!--bs_t) {
          tn_beam_on = BEAM_ON;
          sfx(SFX_ZAP);
          shake = 4u;
        }
        break;
      }
      if (tn_beam_on) {
        beam_hit();
        if (!--tn_beam_on) {
          bc_face(BA_FACE_OPEN);
          slam_lag = 56u;
        }
        break;
      }
      if (!--slam_lag) {
        bc_face(BA_FACE_CALM);
        bs_st = BS_IDLE;
        bs_t = gap[bs_phase];
      }
      break;
    case ATK_QUAKE:
      if (bs_t) {
        if (!(bs_t & 7u)) shake = 3u;
        if (!--bs_t) {
          bc_break_shelf(quake_c);
          bc_face(BA_FACE_CALM);
          bs_st = BS_IDLE;
          bs_t = (uint8_t)(gap[bs_phase] + 24u);
        }
      }
      break;
    default:
      bs_st = BS_IDLE;
      bs_t = gap[bs_phase];
      break;
  }
}

/* the beam's live state, for the drawing */
uint8_t tenant_beam(void) BANKED { return (uint8_t)(bs_atk == ATK_BEAM && bs_st == BS_ATTACK ? (tn_beam_on ? 2u : (bs_t ? 1u : 0u)) : 0u); }
