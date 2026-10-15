/* Host tests for src/core/eg_move.c: 8-way movement, 4-way mode, facing, wall slide, corner nudge, push requests.
 * cc -O2 -std=c99 -Wall -Wextra -Werror -Isrc -o build/host/test_move host/test_move.c src/core/eg_move.c */
#include <stdio.h>
#include <string.h>

#include "core/eg_move.h"

static int pass, fail;
#define CHECK(c, ...)                                  \
  do {                                                 \
    if (c) {                                           \
      pass++;                                          \
    } else {                                           \
      fail++;                                          \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);      \
      printf(__VA_ARGS__);                             \
      printf("\n");                                    \
    }                                                  \
  } while (0)

#define W 10
#define H 8
#define R EG_DP_RIGHT
#define L EG_DP_LEFT
#define U EG_DP_UP
#define D EG_DP_DOWN
#define PX(p) ((int16_t)((p) << EG_FIX))

static uint8_t cells[W * H];
static eg_grid_t grid = {cells, W, H};

static void room(void) { /* 10x8, wall border, open inside */
  int x, y;
  for (y = 0; y < H; y++)
    for (x = 0; x < W; x++) cells[y * W + x] = (x == 0 || y == 0 || x == W - 1 || y == H - 1) ? EG_C_WALL : EG_C_FREE;
}
static void set(int x, int y, uint8_t c) { cells[y * W + x] = c; }

static void steps(eg_mover_t *m, uint8_t dp, int n) {
  while (n--) eg_move_step(m, dp, 24u, &grid);
}

static int overlaps_solid(const eg_mover_t *m) {
  int x0 = (m->x >> EG_FIX), y0 = (m->y >> EG_FIX), x1 = x0 + EG_BOX_PX - 1, y1 = y0 + EG_BOX_PX - 1;
  int cx, cy;
  if (m->x < 0 || m->y < 0) return 1;
  for (cy = y0 >> 4; cy <= y1 >> 4; cy++)
    for (cx = x0 >> 4; cx <= x1 >> 4; cx++)
      if (eg_grid_cell(&grid, (int16_t)cx, (int16_t)cy) != EG_C_FREE) return 1;
  return 0;
}

static void t_diagonal(void) {
  eg_mover_t m;
  room();
  eg_move_init(&m, PX(32), PX(32), EG_MODE_8WAY);
  steps(&m, R, 32);
  CHECK(m.x == PX(32) + 768 && m.y == PX(32), "straight: 32 frames x 24 sub = 768 (%d)", m.x - PX(32));
  eg_move_init(&m, PX(32), PX(32), EG_MODE_8WAY);
  steps(&m, R | D, 32);
  /* floor(32 * 24 * 181 / 256) = 543 per axis, 543/768 = 0.707 */
  CHECK(m.x == PX(32) + 543 && m.y == PX(32) + 543, "diagonal: 543 sub per axis (%d, %d)", m.x - PX(32), m.y - PX(32));
  eg_move_init(&m, PX(80), PX(80), EG_MODE_8WAY);
  steps(&m, U | L, 32);
  CHECK(m.x == PX(80) - 543 && m.y == PX(80) - 543, "diagonal up-left symmetric (%d, %d)", PX(80) - m.x, PX(80) - m.y);
  eg_move_init(&m, PX(32), PX(32), EG_MODE_8WAY);
  steps(&m, R | D, 7);
  /* per-frame steps carry the remainder: frame 1 = 16 (4344>>8), after 7 frames floor(7*4344/256) = 118 */
  CHECK(m.x == PX(32) + 118, "diagonal 7 frames: 118 sub (%d)", m.x - PX(32));
  eg_move_init(&m, PX(48), PX(48), EG_MODE_8WAY);
  steps(&m, R | L, 5);
  CHECK(m.x == PX(48), "8-way: left+right cancel");
}

static void t_four_way(void) {
  eg_mover_t m;
  int16_t x, y;
  room();
  eg_move_init(&m, PX(64), PX(64), EG_MODE_4WAY);
  steps(&m, R, 3);
  x = m.x;
  y = m.y;
  CHECK(m.dir == R && m.y == PX(64), "4-way: right moves right only");
  steps(&m, R | U, 2);
  CHECK(m.dir == U && m.x == x && m.y == y - 48 && m.facing == EG_FACE_N, "4-way: up pressed last wins over held right");
  steps(&m, R, 1);
  CHECK(m.dir == R && m.facing == EG_FACE_E, "4-way: releasing up falls back to held right");
  steps(&m, R | L, 1);
  CHECK(m.dir == L, "4-way: left pressed while right held wins");
  steps(&m, R | L | D, 1);
  CHECK(m.dir == D, "4-way: down newest");
  steps(&m, R | L, 1);
  CHECK(m.dir == L, "4-way: down released -> left (next most recent), not right");
  steps(&m, 0, 1);
  steps(&m, U | R, 1);
  CHECK(m.dir == R, "4-way: same-frame press resolves horizontal (right) first");
  eg_move_init(&m, PX(64), PX(64), EG_MODE_8WAY);
  steps(&m, U | R, 1);
  CHECK(m.dir == (U | R) && m.facing == EG_FACE_NE, "8-way: up+right is NE");
}

static void t_facing(void) {
  static const uint8_t dp[8] = {U, U | R, R, D | R, D, D | L, L, U | L};
  static const uint8_t fc[8] = {EG_FACE_N, EG_FACE_NE, EG_FACE_E, EG_FACE_SE, EG_FACE_S, EG_FACE_SW, EG_FACE_W, EG_FACE_NW};
  eg_mover_t m;
  int i;
  room();
  for (i = 0; i < 8; i++) {
    eg_move_init(&m, PX(64), PX(56), EG_MODE_8WAY);
    steps(&m, dp[i], 2);
    CHECK(m.facing == fc[i], "facing %d", i);
    steps(&m, 0, 5);
    CHECK(m.facing == fc[i], "facing %d kept when stopped", i);
  }
  /* facing follows input even when blocked */
  eg_move_init(&m, PX(16), PX(56), EG_MODE_8WAY);
  steps(&m, L, 1);
  CHECK(m.facing == EG_FACE_W && m.x == PX(16) && (m.blocked & EG_BLK_X), "facing west into the wall while blocked");
}

static void t_wall_slide(void) {
  eg_mover_t m;
  int i, ok = 1;
  room();
  for (i = 1; i < H - 1; i++) set(6, i, EG_C_WALL);
  eg_move_init(&m, PX(96 - EG_BOX_PX), PX(40), EG_MODE_8WAY); /* flush against the wall column at x = 96 */
  for (i = 0; i < 10; i++) {
    eg_move_step(&m, R | D, 24u, &grid);
    if (!(m.blocked & EG_BLK_X) || (m.blocked & EG_BLK_Y) || m.x != PX(84)) ok = 0;
  }
  CHECK(ok, "slide: x pinned at the wall, x blocked, y free every frame");
  CHECK(m.y == PX(40) + 169, "slide: y advances floor(10*4344/256) = 169 sub (%d)", m.y - PX(40));
  /* approaching from a distance snaps flush */
  eg_move_init(&m, PX(80), PX(40), EG_MODE_8WAY);
  steps(&m, R, 4);
  CHECK(m.x == PX(84) && !overlaps_solid(&m), "approach: snapped flush at x = 84 px");
}

static void t_corner_nudge(void) {
  eg_mover_t m;
  room();
  set(5, 3, EG_C_WALL); /* rows 48..63, cols 80..95 */
  /* box rows 61..72: 3 px into the wall's row -> nudged down */
  eg_move_init(&m, PX(80 - EG_BOX_PX), PX(61), EG_MODE_8WAY);
  steps(&m, R, 1);
  CHECK(m.x == PX(68) && m.y == PX(61) + 24, "nudge down: frame 1 moves 24 sub across, not forward");
  steps(&m, R, 1);
  CHECK(m.x == PX(68) && m.y == PX(64), "nudge down: frame 2 clamps at the lane edge (y = 64 px)");
  steps(&m, R, 1);
  CHECK(m.x == PX(68) + 24 && m.y == PX(64), "nudge down: frame 3 passes the corner");
  /* box rows 37..48: 1 px into the wall's row -> nudged up */
  eg_move_init(&m, PX(80 - EG_BOX_PX), PX(37), EG_MODE_8WAY);
  steps(&m, R, 1);
  CHECK(m.y == PX(36) && m.x == PX(68), "nudge up: 16 sub (exactly the overlap)");
  steps(&m, R, 1);
  CHECK(m.x == PX(68) + 24, "nudge up: then passes");
  /* 5 px overlap: too much, no nudge */
  eg_move_init(&m, PX(80 - EG_BOX_PX), PX(59), EG_MODE_8WAY);
  steps(&m, R, 6);
  CHECK(m.x == PX(68) && m.y == PX(59), "no nudge at 5 px overlap");
  /* vertical approach: nudge sideways */
  eg_move_init(&m, PX(93), PX(64), EG_MODE_8WAY); /* box cols 93..104 under the wall, 3 px into col 5 */
  steps(&m, U, 2);
  CHECK(m.x == PX(96) && m.y == PX(64), "nudge right on an upward bump");
}

static void t_push(void) {
  eg_mover_t m;
  int i, at = -1, n = 0;
  room();
  set(6, 3, EG_C_BLOCK); /* x 96..111, y 48..63 */
  eg_move_init(&m, PX(96 - EG_BOX_PX), PX(44), EG_MODE_8WAY);
  for (i = 0; i < 20; i++) {
    eg_move_step(&m, R | D, 24u, &grid);
    if (m.req) {
      n++;
      if (at < 0) at = i;
    }
    if (i == 9) {
      CHECK(m.req && m.req_cx == 6 && m.req_cy == 3 && m.req_dir == EG_DP_RIGHT,
            "diagonal push: on the 10th frame, the X-blocked axis, cell (6,3), dir RIGHT");
      CHECK(m.y == PX(44) + 169 && m.x == PX(84), "diagonal push: y kept sliding while x pressed the block");
    }
  }
  CHECK(at == 9 && n == 1, "diagonal push: exactly one request, at frame index 9 (%d, %d)", at, n);

  /* Y-blocked diagonal: block below, moving down-right; the push resolves to DOWN and x slides */
  room();
  set(4, 5, EG_C_BLOCK); /* x 64..79, y 80..95 */
  eg_move_init(&m, PX(60), PX(80 - EG_BOX_PX), EG_MODE_8WAY);
  at = -1;
  for (i = 0; i < 12; i++) {
    eg_move_step(&m, R | D, 24u, &grid);
    if (m.req && at < 0) at = i;
    if (m.req) CHECK(m.req_dir == EG_DP_DOWN && m.req_cx == 4 && m.req_cy == 5, "diagonal push down: cell (4,5) DOWN");
  }
  CHECK(at == 9, "diagonal push down after the hold (%d)", at);

  /* brief bumps never push */
  room();
  set(6, 3, EG_C_BLOCK);
  eg_move_init(&m, PX(84), PX(50), EG_MODE_4WAY);
  n = 0;
  for (i = 0; i < 9; i++) {
    eg_move_step(&m, R, 24u, &grid);
    n += m.req;
  }
  eg_move_step(&m, 0, 24u, &grid);
  for (i = 0; i < 9; i++) {
    eg_move_step(&m, R, 24u, &grid);
    n += m.req;
  }
  CHECK(n == 0, "two 9-frame bumps: no push");
  eg_move_step(&m, R, 24u, &grid);
  CHECK(m.req && m.req_dir == EG_DP_RIGHT, "the 10th continuous frame pushes");
  /* walls never raise a push */
  room();
  eg_move_init(&m, PX(16), PX(50), EG_MODE_8WAY);
  n = 0;
  for (i = 0; i < 30; i++) {
    eg_move_step(&m, L, 24u, &grid);
    n += m.req;
  }
  CHECK(n == 0, "no push request against a wall");
}

static uint32_t lcg;
static uint8_t rnd(void) {
  lcg = lcg * 1103515245u + 12345u;
  return (uint8_t)(lcg >> 16);
}

static uint32_t run_maze(int *bad, int *reqs) {
  eg_mover_t m;
  uint32_t h = 2166136261u;
  uint8_t dp = 0u;
  int i;
  room();
  set(3, 2, EG_C_WALL); set(3, 3, EG_C_BLOCK); set(6, 2, EG_C_BLOCK); set(6, 5, EG_C_WALL); set(5, 5, EG_C_WALL);
  set(2, 5, EG_C_BLOCK); set(7, 3, EG_C_WALL);
  lcg = 99u;
  eg_move_init(&m, PX(20), PX(20), EG_MODE_8WAY);
  *bad = 0;
  *reqs = 0;
  for (i = 0; i < 4000; i++) {
    if ((rnd() & 7u) == 0u) dp = (uint8_t)(rnd() & 0x0fu);
    if (i == 2000) m.mode = EG_MODE_4WAY;
    eg_move_step(&m, dp, (uint8_t)(8u + (rnd() & 31u)), &grid);
    if (overlaps_solid(&m)) (*bad)++;
    if (m.facing > EG_FACE_NW) (*bad)++;
    if (m.req && (m.req_dir == 0u || eg_grid_cell(&grid, m.req_cx, m.req_cy) != EG_C_BLOCK)) (*bad)++;
    *reqs += m.req;
    h = (h ^ (uint32_t)(uint16_t)m.x) * 16777619u;
    h = (h ^ (uint32_t)(uint16_t)m.y) * 16777619u;
    h = (h ^ (uint32_t)(m.facing | (m.blocked << 3) | (m.req << 5) | (m.push_t << 8))) * 16777619u;
  }
  return h;
}

static void t_determinism(void) {
  int bad1, bad2, r1, r2;
  uint32_t h1 = run_maze(&bad1, &r1), h2 = run_maze(&bad2, &r2);
  CHECK(bad1 == 0, "random walk: never overlaps a solid cell, valid outputs (%d bad)", bad1);
  CHECK(h1 == h2 && r1 == r2, "random walk twice: identical trace");
}

int main(void) {
  t_diagonal();
  t_four_way();
  t_facing();
  t_wall_slide();
  t_corner_nudge();
  t_push();
  t_determinism();
  printf("test_move: %d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
