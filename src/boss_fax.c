/* LAST FAX, the office's mid-boss: a fax machine on rollers, 32x32 sprites (docs/redesign.md 7.5, 3.6.6).
 *
 * Two phases (13: a mid-boss takes 10-12 good hits):
 *   1  dodge:  RAM (it squats and rattles, chevrons mark its lane, then it charges down it until something stops it:
 *              step out of the lane or roll through; a perfect parry stops it dead). It stands stunned where it hits,
 *              open. Rammed into a shelf, it knocks the CRT down onto itself (a crush). JAM: come within reach and it
 *              buzzes, then bursts paper round itself: get clear or roll.
 *   -> THE GLASS: the middle of the floor turns to glass. It cracks under whoever crosses it, the fax included, and a
 *              ram stops at a hole.
 *   2  parry:  FAN (five pages: parry them back into it), RAM twice, JAM. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "boss_int.h"
#include "look.h"

static const uint8_t script0[] = {ATK_RAM, ATK_RAM, ATK_RAM};
static const uint8_t script1[] = {ATK_FAN, ATK_RAM, ATK_RAM, ATK_FAN, ATK_RAM};
static const uint8_t ram_tell[2] = {32u, 26u}, stun_t[2] = {56u, 44u}, ram_sp[2] = {4u, 5u};
#define JAM_TELL 26u
#define FAN_TELL 30u
static uint8_t rams;
uint8_t fx_jam_cd; /* not static: the harness reads it */

void fax_setup(void) BANKED {
  uint8_t x, y;
  for (y = 4; y <= AR_Y1; y++)
    for (x = AR_X0; x <= AR_X1; x++) room.c[EG_IDX(x, y)] = T_EMPTY;
  /* two shelves in the north wall: CRT sets on drywall */
  room.c[EG_IDX(5, 3)] = room.c[EG_IDX(10, 3)] = T_ROCK;
  room.c[EG_IDX(5, 4)] = room.c[EG_IDX(10, 4)] = T_BRICK;
  room.c[EG_IDX(5, 2)] = room.c[EG_IDX(10, 2)] = T_WALL;
  room.door = EG_IDX(7, 3);
  room.c[room.door] = T_DOOR;
  room.start = EG_IDX(7, 9);
  bs_hp = 6u;
  bs_x = 136u;
  bs_y = 92u;
  bs_shard_x = 128u;
  bs_shard_y = 120u;
  rams = fx_jam_cd = 0;
}

/* can the fax's box (24x20 round its centre) stand at (x, y)? */
static uint8_t fax_free(uint8_t x, uint8_t y) {
  return (uint8_t)(bc_floor_at((uint8_t)(x - 11u), (uint8_t)(y - 9u)) && bc_floor_at((uint8_t)(x + 11u), (uint8_t)(y - 9u)) &&
                   bc_floor_at((uint8_t)(x - 11u), (uint8_t)(y + 9u)) && bc_floor_at((uint8_t)(x + 11u), (uint8_t)(y + 9u)));
}

void fax_gate(uint8_t to) BANKED {
  uint8_t x, y, c;
  (void)to;
  hud_msg("THE FLOOR TURNS TO GLASS");
  for (y = 7; y != 9u; y++)
    for (x = 5; x != 11u; x++) {
      c = EG_IDX(x, y);
      if (cell_type(c) != T_EMPTY) continue;
      room.c[c] = T_GLASS;
      look_redraw(c);
    }
  sfx(SFX_SHATTER);
}

/* glass under the fax cracks when it rolls off it */
static uint8_t under[4];
static void glass_step(void) {
  uint8_t k, c, cs[4];
  cs[0] = cell_at((uint8_t)(bs_x - 10u), (uint8_t)(bs_y - 8u));
  cs[1] = cell_at((uint8_t)(bs_x + 10u), (uint8_t)(bs_y - 8u));
  cs[2] = cell_at((uint8_t)(bs_x - 10u), (uint8_t)(bs_y + 8u));
  cs[3] = cell_at((uint8_t)(bs_x + 10u), (uint8_t)(bs_y + 8u));
  for (k = 0; k != 4u; k++) {
    c = under[k];
    if (c && cell_type(c) == T_GLASS && c != cs[0] && c != cs[1] && c != cs[2] && c != cs[3] &&
        cell_at(pl_x, pl_y) != c) {
      room.c[c] = T_PIT;
      look_break_tile(c, LOOK_FRAG_GLASS);
    }
  }
  for (k = 0; k != 4u; k++) under[k] = cs[k];
}

static void next_attack(void) {
  const uint8_t *s = bs_phase ? script1 : script0;
  uint8_t n = bs_phase ? sizeof script1 : sizeof script0;
  int16_t dx = (int16_t)pl_x - bs_x, dy = (int16_t)pl_y - bs_y;
  bs_atk = s[bs_seq];
  if (++bs_seq >= n) bs_seq = 0;
  /* a heir in its reach gets the jam first */
  if (!fx_jam_cd && absdiff(pl_x, bs_x) < 34u && absdiff(pl_y, bs_y) < 32u) bs_atk = ATK_JAM;
  bs_st = BS_ATTACK;
  switch (bs_atk) {
    case ATK_RAM:
      bs_t = ram_tell[bs_phase];
      if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) {
        bs_vx = (int8_t)(dx < 0 ? -(int8_t)ram_sp[bs_phase] : (int8_t)ram_sp[bs_phase]);
        bs_vy = 0;
      } else {
        bs_vx = 0;
        bs_vy = (int8_t)(dy < 0 ? -(int8_t)ram_sp[bs_phase] : (int8_t)ram_sp[bs_phase]);
      }
      bc_tell(SFX_B_RAM);
      break;
    case ATK_JAM:
      bs_t = JAM_TELL;
      fx_jam_cd = 120u;
      bc_tell(SFX_B_JAM);
      break;
    default: /* ATK_FAN */
      bs_t = FAN_TELL;
      bc_tell(SFX_TELL);
      break;
  }
  bs_hs[0] = HS_RISE; /* the tell */
}

static void fan(void) {
  uint8_t i;
  int16_t dx = (int16_t)pl_x - bs_x, dy = (int16_t)pl_y - bs_y;
  for (i = 0; i != 5u; i++) {
    int16_t o = (int16_t)i - 2;
    if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy))
      fire_orb_at(bs_x, bs_y, dx, dy + o * 22, 18u);
    else
      fire_orb_at(bs_x, bs_y, dx + o * 22, dy, 18u);
  }
  sfx(SFX_SHOT);
}

void fax_update(void) BANKED {
  uint8_t r;
  if (fx_jam_cd) fx_jam_cd--;
  if (bs_phase) glass_step();
  en[0].x = (uint16_t)bs_x << 4;
  en[0].y = (uint16_t)bs_y << 4;
  if (bs_st == BS_GATE) {
    if (++bs_t >= 70u) {
      bs_st = BS_IDLE;
      bs_t = 30u;
      bs_hp = 6u;
      bs_hs[0] = HS_HOME;
    }
    return;
  }
  bs_open = (uint8_t)(bs_hs[0] == HS_REST || (bs_atk == ATK_FAN && bs_hs[0] == HS_BACK));
  switch (bs_st) {
    case BS_IDLE: { /* it rolls toward a good distance from the heir */
      uint8_t nx = bs_x, ny = bs_y;
      if (absdiff(pl_x, bs_x) > 48u) nx = (uint8_t)(bs_x + (pl_x > bs_x ? 1u : 255u));
      if (absdiff(pl_y, bs_y) > 40u) ny = (uint8_t)(bs_y + (pl_y > bs_y ? 1u : 255u));
      if ((bs_t & 1u) && fax_free(nx, ny)) {
        bs_x = nx;
        bs_y = ny;
      }
      if (!--bs_t) next_attack();
      return;
    }
    case BS_ATTACK:
      break;
    default:
      return;
  }
  switch (bs_hs[0]) {
    case HS_RISE:
      if (!--bs_t) {
        if (bs_atk == ATK_RAM) bs_hs[0] = HS_SWEEP;
        else if (bs_atk == ATK_JAM) {
          bs_hs[0] = HS_DROP;
          bs_t = 6u;
          sfx(SFX_B_SLAM);
          shake = 6u;
          puff((uint8_t)(bs_x - 16u), bs_y, 5, ST_PUFF);
          puff((uint8_t)(bs_x + 16u), bs_y, 5, ST_PUFF);
          puff(bs_x, (uint8_t)(bs_y - 18u), 5, ST_PUFF);
        } else {
          fan();
          bs_hs[0] = HS_BACK;
          bs_t = 60u;
        }
      }
      break;
    case HS_SWEEP: { /* the ram */
      uint8_t nx = (uint8_t)(bs_x + bs_vx), ny = (uint8_t)(bs_y + bs_vy);
      if (overlap(pl_x, pl_y, bs_x, bs_y, 17u, 15u)) {
        r = bc_hurt_r(2u, ATK_RAM, bs_x, bs_y);
        if (r == 3u) { /* parried dead */
          bs_hs[0] = HS_REST;
          bs_t = stun_t[bs_phase];
          sfx(SFX_CHARGED);
          shake = 6u;
          break;
        }
      }
      if (!fax_free(nx, ny)) {
        /* a shelf's drywall in its way: the set comes down on it */
        uint8_t c = cell_at((uint8_t)(nx + (bs_vx > 0 ? 11 : (bs_vx < 0 ? -11 : 0))), (uint8_t)(ny - 10u));
        if (cell_type(c) == T_BRICK) bc_break_shelf(c);
        bs_hs[0] = HS_REST;
        bs_t = stun_t[bs_phase];
        shake = 8u;
        sfx(SFX_B_SLAM);
        break;
      }
      bs_x = nx;
      bs_y = ny;
      break;
    }
    case HS_DROP: /* the jam's burst */
      if (overlap(pl_x, pl_y, bs_x, bs_y, 30u, 28u)) bc_hurt(2u, ATK_JAM, bs_x, bs_y);
      if (!--bs_t) {
        bs_hs[0] = HS_REST;
        bs_t = 30u;
      }
      break;
    case HS_REST:
      if (!--bs_t) {
        bs_hs[0] = HS_HOME;
        if (bs_atk == ATK_RAM && bs_phase && !(rams++ & 1u)) { /* phase 2 rams twice: a second tell, a new lane */
          bs_seq = bs_seq ? (uint8_t)(bs_seq - 1u) : 0u;
        }
        bs_st = BS_IDLE;
        bs_t = 24u;
      }
      break;
    case HS_BACK: /* after the fan: open while its pages are out */
      if (!--bs_t) {
        bs_hs[0] = HS_HOME;
        bs_st = BS_IDLE;
        bs_t = 30u;
      }
      break;
    default:
      bs_st = BS_IDLE;
      bs_t = 30u;
      break;
  }
}
