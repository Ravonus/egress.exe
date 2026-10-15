/* EGRESS 8-direction movement (see eg_move.h, proto/input/FRAMES.md). */
#include "eg_move.h"

#define BOX_SUB ((int16_t)(EG_BOX_PX << EG_FIX))

static const uint8_t face_of[9] = {EG_FACE_NW, EG_FACE_N, EG_FACE_NE, EG_FACE_W, 0xffu,
                                   EG_FACE_E,  EG_FACE_SW, EG_FACE_S, EG_FACE_SE};

uint8_t eg_grid_cell(const eg_grid_t *g, int16_t cx, int16_t cy) {
  if (cx < 0 || cy < 0 || cx >= (int16_t)g->w || cy >= (int16_t)g->h) return EG_C_WALL;
  return g->cells[(uint16_t)cy * g->w + (uint16_t)cx];
}

/* axis 0: along = x, across = y; axis 1: along = y, across = x. */
static uint8_t cell_ax(const eg_grid_t *g, uint8_t axis, int16_t along, int16_t across) {
  return axis ? eg_grid_cell(g, across, along) : eg_grid_cell(g, along, across);
}

/* Move *p by d (|d| < 16 px) with the box's across coordinate q; on a hit, snap flush and return 1 with the
 * blocking column/row index in *hit. */
static uint8_t move_axis(const eg_grid_t *g, uint8_t axis, int16_t *p, int16_t q, int16_t d, int16_t *hit) {
  int16_t np = (int16_t)(*p + d), col;
  int16_t qpx = (int16_t)(q >> EG_FIX);
  int16_t r0 = (int16_t)(qpx >> 4), r1 = (int16_t)((qpx + EG_BOX_PX - 1) >> 4);
  if (d > 0) {
    col = (int16_t)(((np >> EG_FIX) + EG_BOX_PX - 1) >> 4);
    if (cell_ax(g, axis, col, r0) || cell_ax(g, axis, col, r1)) {
      *p = (int16_t)((col * EG_CELL_PX - EG_BOX_PX) << EG_FIX);
      *hit = col;
      return 1u;
    }
  } else if (d < 0) {
    col = np < 0 ? (int16_t)-1 : (int16_t)((np >> EG_FIX) >> 4);
    if (cell_ax(g, axis, col, r0) || cell_ax(g, axis, col, r1)) {
      *p = (int16_t)(((col + 1) * EG_CELL_PX) << EG_FIX);
      *hit = col;
      return 1u;
    }
  }
  *p = np;
  return 0u;
}

void eg_move_init(eg_mover_t *m, int16_t x, int16_t y, uint8_t mode) {
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
static void nudge(eg_mover_t *m, const eg_grid_t *g, uint8_t axis, int16_t col, uint8_t speed) {
  int16_t *q = axis ? &m->x : &m->y;
  int16_t p = axis ? m->y : m->x;
  int16_t qpx = (int16_t)(*q >> EG_FIX), hit;
  int16_t r0 = (int16_t)(qpx >> 4), r1 = (int16_t)((qpx + EG_BOX_PX - 1) >> 4);
  int16_t lane = (int16_t)((r1 * EG_CELL_PX) << EG_FIX), over, step;
  uint8_t s0, s1;
  if (r0 == r1) return;
  s0 = cell_ax(g, axis, col, r0) != EG_C_FREE;
  s1 = cell_ax(g, axis, col, r1) != EG_C_FREE;
  if (s0 == s1) return;
  over = s0 ? (int16_t)(lane - *q) : (int16_t)(*q + BOX_SUB - lane);
  if (over > (int16_t)(EG_NUDGE_PX << EG_FIX)) return;
  step = over < (int16_t)speed ? over : (int16_t)speed;
  (void)move_axis(g, (uint8_t)(axis ^ 1u), q, p, s0 ? step : (int16_t)-step, &hit);
}

void eg_move_step(eg_mover_t *m, uint8_t dpad, uint8_t speed, const eg_grid_t *g) {
  uint8_t d = resolve_dir(m, (uint8_t)(dpad & EG_DP_MASK)), f, cand = 0u, ccx = 0u, ccy = 0u, cdir = 0u;
  int8_t sx = (d & EG_DP_RIGHT) ? 1 : ((d & EG_DP_LEFT) ? -1 : 0);
  int8_t sy = (d & EG_DP_DOWN) ? 1 : ((d & EG_DP_UP) ? -1 : 0);
  int16_t step = (int16_t)speed, hx = 0, hy = 0, c;
  uint16_t t;

  m->dir = d;
  m->blocked = 0u;
  m->req = 0u;
  f = face_of[(sy + 1) * 3 + (sx + 1)];
  if (f != 0xffu) m->facing = f;

  if (sx && sy) {
    t = (uint16_t)((uint16_t)m->acc + (uint16_t)speed * EG_DIAG_NUM);
    step = (int16_t)(t >> 8);
    m->acc = (uint8_t)(t & 0xffu);
  } else {
    m->acc = 0u;
  }

  if (sx && move_axis(g, 0u, &m->x, m->y, sx > 0 ? step : (int16_t)-step, &hx)) m->blocked |= EG_BLK_X;
  if (sy && move_axis(g, 1u, &m->y, m->x, sy > 0 ? step : (int16_t)-step, &hy)) m->blocked |= EG_BLK_Y;

  if (sx && !sy && (m->blocked & EG_BLK_X)) nudge(m, g, 0u, hx, speed);
  if (sy && !sx && (m->blocked & EG_BLK_Y)) nudge(m, g, 1u, hy, speed);

  /* Push candidate: the blocked axis only, the block in the lane holding the box centre. X wins an inside corner. */
  if (m->blocked & EG_BLK_X) {
    c = (int16_t)(((m->y >> EG_FIX) + EG_BOX_PX / 2) >> 4);
    if (eg_grid_cell(g, hx, c) == EG_C_BLOCK) {
      cand = 1u;
      ccx = (uint8_t)hx;
      ccy = (uint8_t)c;
      cdir = sx > 0 ? EG_DP_RIGHT : EG_DP_LEFT;
    }
  }
  if (!cand && (m->blocked & EG_BLK_Y)) {
    c = (int16_t)(((m->x >> EG_FIX) + EG_BOX_PX / 2) >> 4);
    if (eg_grid_cell(g, c, hy) == EG_C_BLOCK) {
      cand = 1u;
      ccx = (uint8_t)c;
      ccy = (uint8_t)hy;
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
