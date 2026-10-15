/* The wall-structure metric (B2 part B, readable walls): interior wall components of a room.
 * Interior = not the outer ring. Wall = a solid structure cell: concrete, drywall, paper, socket, dead shutter,
 * shutter, glass partition. Components are 4-connected; one is anchored when it touches the outer ring. */
#include "eg_walls.h"

static int wallish(uint8_t v) {
  uint8_t t = v & T_MASK;
  if (t == T_ROCK) return 0;
  return t == T_WALL || t == T_BRICK || t == T_PAPER || t == T_SOCKET || t == T_SOCKET_ON || t == T_DEAD ||
         t == T_GATE || t == 31;
}

void eg_walls(const eg_room *r, eg_wall_stats *s) {
  uint8_t seen[EG_N] = {0}, q[EG_N];
  s->cells = s->comps = s->single = s->single_float = s->small = s->anchored = 0;
  for (int i = 0; i < (int)EG_N; i++) {
    int x = EG_X(i), y = EG_Y(i);
    if (x == 0 || y == 0 || x == EG_W - 1 || y == EG_H - 1 || seen[i] || !wallish(r->c[i])) continue;
    int n = 0, h = 0, anch = 0;
    q[n++] = (uint8_t)i;
    seen[i] = 1;
    while (h < n) {
      int c = q[h++], cx = EG_X(c), cy = EG_Y(c);
      static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
      for (int d = 0; d < 4; d++) {
        int nx = cx + dx[d], ny = cy + dy[d];
        if (nx == 0 || ny == 0 || nx == EG_W - 1 || ny == EG_H - 1) {
          anch = 1;
          continue;
        }
        int j = EG_IDX(nx, ny);
        if (seen[j] || !wallish(r->c[j])) continue;
        seen[j] = 1;
        q[n++] = (uint8_t)j;
      }
    }
    s->cells += n;
    s->comps++;
    if (n == 1) {
      s->single++;
      if (!anch) s->single_float++;
    }
    if (n <= 2) s->small++;
    if (anch) s->anchored++;
  }
}
