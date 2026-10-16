/* EGRESS 8-direction movement (see eg_move.h, proto/input/FRAMES.md). */
#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_move.h"

#define BOX_SUB ((int16_t)(EG_BOX_PX << EG_FIX))

void eg_aim(int16_t dx, int16_t dy, uint8_t speed, int8_t *vx, int8_t *vy) EG_BANKED {
  uint16_t ax=(uint16_t)(dx<0 ? -dx : dx), ay=(uint16_t)(dy<0 ? -dy : dy);
  uint8_t minor;
  if(!ax && !ay) {*vx=*vy=0;return;}
  /* The dominant component is exactly +/-speed: only the minor one needs a division. */
  if(ax>=ay) {
    *vx=dx<0 ? -(int8_t)speed : (int8_t)speed;
    minor=(uint8_t)((ay*speed)/ax);
    *vy=dy<0 ? -(int8_t)minor : (int8_t)minor;
  } else {
    *vy=dy<0 ? -(int8_t)speed : (int8_t)speed;
    minor=(uint8_t)((ax*speed)/ay);
    *vx=dx<0 ? -(int8_t)minor : (int8_t)minor;
  }
}

/* facing by the resolved d-pad bits (R 1, L 2, U 4, D 8; never both of a pair) */
static const uint8_t face_of[16] = {0xffu,      EG_FACE_E,  EG_FACE_W,  0xffu, EG_FACE_N, EG_FACE_NE,
                                    EG_FACE_NW, 0xffu,      EG_FACE_S,  EG_FACE_SE, EG_FACE_SW, 0xffu,
                                    0xffu,      0xffu,      0xffu,      0xffu};

uint8_t eg_grid_cell(const eg_grid_t *g, int16_t cx, int16_t cy) EG_BANKED {
  uint8_t c;
  const uint8_t *map = g->map;
  /* unsigned compares: a negative coordinate is out too */
  if ((uint16_t)cx >= g->w || (uint16_t)cy >= g->h) return EG_C_WALL;
  if (g->wshift == 4u) c = g->cells[(uint8_t)(((uint8_t)cy << 4) | (uint8_t)cx) + ((uint16_t)((uint8_t)cy >> 4) << 8)];
  else if (g->wshift) c = g->cells[((uint16_t)cy << g->wshift) + (uint16_t)cx];
  else c = g->cells[(uint16_t)cy * g->w + (uint16_t)cx];
  return map ? map[c] : c;
}

/* The step's own reads: the grid's fields copied to statics once per step, cells as 8-bit indices (a coordinate
 * of -1 arrives as 0xff, past any width, so it reads as wall). With 16-pixel cells and 4 fraction bits, a cell index
 * is the high byte of a sub-pixel coordinate: SM83 code without 16-bit shifts. */
#if EG_CELL_PX != 16 || EG_FIX != 4
#error "eg_move: cell index = high byte assumes 16-pixel cells and 12.4 fixed point"
#endif
static const uint8_t *g_cells, *g_map;
static uint8_t g_w, g_h, g_shift;
#define HI(v) ((uint8_t)((uint16_t)(v) >> 8))
#define FAR_EDGE ((int16_t)((EG_BOX_PX - 1) << EG_FIX)) /* the box's last pixel row/column, in sub-pixels */

static uint8_t cell8(uint8_t cx, uint8_t cy) {
  uint8_t c;
  if (cx >= g_w || cy >= g_h) return EG_C_WALL;
  if (g_shift == 4u) c = g_cells[((uint16_t)cy << 4) | cx];
  else if (g_shift) c = g_cells[((uint16_t)cy << g_shift) + cx];
  else c = g_cells[(uint16_t)cy * g_w + cx];
  return g_map ? g_map[c] : c;
}

/* axis 0: along = x, across = y; axis 1: along = y, across = x. */
static uint8_t cell_ax(uint8_t axis, uint8_t along, uint8_t across) {
  return axis ? cell8(across, along) : cell8(along, across);
}

/* Move *p by d (|d| < 16 px) with the box's across coordinate q (>= 0); on a hit, snap flush and return 1 with the
 * blocking column/row index in *hit (0xff: the column left of the grid). */
static uint8_t move_axis(uint8_t axis, int16_t *p, int16_t q, int16_t d, uint8_t *hit) {
  int16_t np = (int16_t)(*p + d);
  uint8_t r0 = HI(q), r1 = HI(q + FAR_EDGE), col;
  if (d > 0) {
    col = HI(np + FAR_EDGE);
    if (cell_ax(axis, col, r0) || (r1 != r0 && cell_ax(axis, col, r1))) {
      *p = (int16_t)(((int16_t)col << 8) - BOX_SUB);
      *hit = col;
      return 1u;
    }
  } else if (d < 0) {
    col = np < 0 ? 0xffu : HI(np);
    if (cell_ax(axis, col, r0) || (r1 != r0 && cell_ax(axis, col, r1))) {
      *p = np < 0 ? 0 : (int16_t)(((uint16_t)col + 1u) << 8);
      *hit = col;
      return 1u;
    }
  }
  *p = np;
  return 0u;
}

void eg_move_init(eg_mover_t *m, int16_t x, int16_t y, uint8_t mode) EG_BANKED {
  m->x = x;
  m->y = y;
  m->mode = mode;
  m->facing = EG_FACE_S;
  m->prev = 0u;
  m->order[0] = EG_DP_RIGHT;
  m->order[1] = EG_DP_LEFT;
  m->order[2] = EG_DP_UP;
  m->order[3] = EG_DP_DOWN;
  m->dir = 0u;
  m->acc = 0u;
  m->blocked = 0u;
  m->push_t = 0u;
  m->push_cx = m->push_cy = m->push_dir = 0u;
  m->req = 0u;
  m->req_cx = m->req_cy = m->req_dir = 0u;
}

static void to_front(eg_mover_t *m, uint8_t bit) {
  uint8_t i = 0u;
  while (i < 3u && m->order[i] != bit) i++;
  while (i) {
    m->order[i] = m->order[i - 1u];
    i--;
  }
  m->order[0] = bit;
}

/* Track press order and resolve the direction bits for this frame. */
static uint8_t resolve_dir(eg_mover_t *m, uint8_t dpad) {
  static const uint8_t press_seq[4] = {EG_DP_DOWN, EG_DP_UP, EG_DP_LEFT, EG_DP_RIGHT};
  uint8_t pressed = (uint8_t)(dpad & (uint8_t)~m->prev), i, d;
  /* same-frame presses are applied DOWN, UP, LEFT, RIGHT, so the last (horizontal, right first) ranks newest */
  for (i = 0u; i < 4u; i++)
    if (pressed & press_seq[i]) to_front(m, press_seq[i]);
  m->prev = dpad;
  if (m->mode == EG_MODE_4WAY) {
    for (i = 0u; i < 4u; i++)
      if (dpad & m->order[i]) return m->order[i];
    return 0u;
  }
  d = dpad;
  if ((d & (EG_DP_LEFT | EG_DP_RIGHT)) == (EG_DP_LEFT | EG_DP_RIGHT)) d &= (uint8_t)~(EG_DP_LEFT | EG_DP_RIGHT);
  if ((d & (EG_DP_UP | EG_DP_DOWN)) == (EG_DP_UP | EG_DP_DOWN)) d &= (uint8_t)~(EG_DP_UP | EG_DP_DOWN);
  return d;
}

/* Corner nudge after a cardinal block on `axis` against column `col`: if the box straddles two lanes and only one
 * of them is solid with <= EG_NUDGE_PX of overlap, slide across toward the open lane (never past the edge). */
static void nudge(eg_mover_t *m, uint8_t axis, uint8_t col, uint8_t speed) {
  int16_t *q = axis ? &m->x : &m->y;
  int16_t p = axis ? m->y : m->x;
  uint8_t r0 = HI(*q), r1 = HI(*q + FAR_EDGE), s0, s1, hit;
  int16_t lane = (int16_t)((int16_t)r1 << 8), over, step;
  if (r0 == r1) return;
  s0 = cell_ax(axis, col, r0) != EG_C_FREE;
  s1 = cell_ax(axis, col, r1) != EG_C_FREE;
  if (s0 == s1) return;
  over = s0 ? (int16_t)(lane - *q) : (int16_t)(*q + BOX_SUB - lane);
  if (over > (int16_t)(EG_NUDGE_PX << EG_FIX)) return;
  step = over < (int16_t)speed ? over : (int16_t)speed;
  (void)move_axis((uint8_t)(axis ^ 1u), q, p, s0 ? step : (int16_t)-step, &hit);
}

void eg_move_step(eg_mover_t *m, uint8_t dpad, uint8_t speed, const eg_grid_t *g) EG_BANKED {
  uint8_t d = resolve_dir(m, (uint8_t)(dpad & EG_DP_MASK)), f, cand = 0u, ccx = 0u, ccy = 0u, cdir = 0u;
  uint8_t hx = 0u, hy = 0u, c;
  int8_t sx = (d & EG_DP_RIGHT) ? 1 : ((d & EG_DP_LEFT) ? -1 : 0);
  int8_t sy = (d & EG_DP_DOWN) ? 1 : ((d & EG_DP_UP) ? -1 : 0);
  int16_t step = (int16_t)speed;
  uint16_t t;

  g_cells = g->cells;
  g_map = g->map;
  g_w = g->w;
  g_h = g->h;
  g_shift = g->wshift;

  m->dir = d;
  m->blocked = 0u;
  m->req = 0u;
  f = face_of[d];
  if (f != 0xffu) m->facing = f;

  if (sx && sy) {
    t = (uint16_t)((uint16_t)m->acc + (uint16_t)speed * EG_DIAG_NUM);
    step = (int16_t)(t >> 8);
    m->acc = (uint8_t)(t & 0xffu);
  } else {
    m->acc = 0u;
  }

  if (sx && move_axis(0u, &m->x, m->y, sx > 0 ? step : (int16_t)-step, &hx)) m->blocked |= EG_BLK_X;
  if (sy && move_axis(1u, &m->y, m->x, sy > 0 ? step : (int16_t)-step, &hy)) m->blocked |= EG_BLK_Y;

  if (sx && !sy && (m->blocked & EG_BLK_X)) nudge(m, 0u, hx, speed);
  if (sy && !sx && (m->blocked & EG_BLK_Y)) nudge(m, 1u, hy, speed);

  /* Push candidate: the blocked axis only, the block in the lane holding the box centre. X wins an inside corner. */
  if (m->blocked & EG_BLK_X) {
    c = HI(m->y + (EG_BOX_PX / 2 << EG_FIX));
    if (cell8(hx, c) == EG_C_BLOCK) {
      cand = 1u;
      ccx = hx;
      ccy = c;
      cdir = sx > 0 ? EG_DP_RIGHT : EG_DP_LEFT;
    }
  }
  if (!cand && (m->blocked & EG_BLK_Y)) {
    c = HI(m->x + (EG_BOX_PX / 2 << EG_FIX));
    if (cell8(c, hy) == EG_C_BLOCK) {
      cand = 1u;
      ccx = c;
      ccy = hy;
      cdir = sy > 0 ? EG_DP_DOWN : EG_DP_UP;
    }
  }
  if (!cand) {
    m->push_t = 0u;
  } else if (m->push_t && ccx == m->push_cx && ccy == m->push_cy && cdir == m->push_dir) {
    if (m->push_t != 255u) m->push_t++;
  } else {
    m->push_t = 1u;
    m->push_cx = ccx;
    m->push_cy = ccy;
    m->push_dir = cdir;
  }
  if (m->push_t == EG_PUSH_HOLD) {
    m->req = 1u;
    m->req_cx = ccx;
    m->req_cy = ccy;
    m->req_dir = cdir;
  }
}
