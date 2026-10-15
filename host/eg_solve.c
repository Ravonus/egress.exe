/* Host room solver (see eg_solve.h). */
#include "eg_solve.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint8_t c[EG_N];
  uint8_t p;    /* canonical player cell (the lowest cell of its open region, or the cell it holds a CRT from) */
  uint8_t land; /* where the act left the player (for the plan) */
  uint8_t keys; /* keycards still on the board */
  int parent;
  uint8_t from, dir; /* the act: from this cell, toward step4[dir] */
  int g;
} node;

static const int step4[4] = {-16, 16, -1, 1};
static const char step_ch[4] = {'U', 'D', 'L', 'R'};
static const char hit_ch[4] = {'u', 'd', 'l', 'r'};

static uint64_t hash_state(const uint8_t *c, uint8_t p, uint8_t keys) {
  uint64_t h = 1469598103934665603ull;
  for (int i = 0; i < (int)EG_N; i++) h = (h ^ c[i]) * 1099511628211ull;
  h = (h ^ p) * 1099511628211ull;
  h = (h ^ keys) * 1099511628211ull;
  return h ? h : 1;
}

/* settle with the player at p; returns 0 if a falling CRT hits the player */
static int settle_at(eg_room *r, uint8_t p) {
  for (int n = 0; n < 256; n++) {
    uint8_t g = eg_gravity(r, &p, 1);
    if (g & EG_G_HIT) return 0;
    if (!(g & EG_G_CHANGED)) return 1;
  }
  return 1;
}

/* does the player at p hold something up (would the board move if p were empty)? */
static int holding(const uint8_t *c) {
  eg_room t;
  memcpy(t.c, c, EG_N);
  return (eg_gravity(&t, NULL, 0) & EG_G_CHANGED) != 0;
}

/* the open region around p: cells reached through empty floor */
static int region(const uint8_t *c, uint8_t p, uint8_t *out, uint8_t *prev) {
  uint8_t seen[EG_N];
  memset(seen, 0, sizeof seen);
  int n = 0, h = 0;
  out[n++] = p;
  seen[p] = 1;
  if (prev) prev[p] = p;
  while (h < n) {
    uint8_t q = out[h++];
    for (int d = 0; d < 4; d++) {
      int s = q + step4[d];
      if (s < 0 || s >= (int)EG_N || seen[s] || !EG_FLOOR(c[s])) continue;
      seen[s] = 1;
      if (prev) prev[s] = q;
      out[n++] = (uint8_t)s;
    }
  }
  return n;
}

/* the player can't walk freely: holding a CRT up, or standing on rotten carpet */
static int pinned(const uint8_t *c, uint8_t p) { return c[p] == T_CRUMBLE || holding(c); }

static uint8_t canon(const uint8_t *c, uint8_t p) {
  uint8_t reg[EG_N];
  if (pinned(c, p)) return p;
  int n = region(c, p, reg, NULL);
  uint8_t m = p;
  for (int i = 0; i < n; i++)
    if (reg[i] < m) m = reg[i];
  return m;
}

/* lower bound-ish distance to the next goal (a keycard, then the door); cells cost by how hard they are to cross */
static int heuristic(const uint8_t *c, uint8_t p, uint8_t keys, uint8_t door) {
  int dist[EG_N];
  for (int i = 0; i < (int)EG_N; i++) dist[i] = 1 << 20;
  /* small Dijkstra with a bucket queue */
  static uint8_t bucket[64][EG_N];
  int bn[64];
  memset(bn, 0, sizeof bn);
  dist[p] = 0;
  bucket[0][bn[0]++] = p;
  for (int b = 0; b < 64; b++) {
    for (int k = 0; k < bn[b]; k++) {
      uint8_t q = bucket[b][k];
      if (dist[q] != b) continue;
      uint8_t t = c[q] & T_MASK;
      if (keys ? t == T_KEY : q == door) return b + keys * 24;
      for (int d = 0; d < 4; d++) {
        int s = q + step4[d];
        if (s < 0 || s >= (int)EG_N) continue;
        uint8_t u = c[s] & T_MASK;
        int w = u == T_WALL ? 0
                : u == T_ROCK ? 4
                : u == T_BRICK ? 2
                : u == T_PIT ? 5
                : u == T_GATE ? 6
                : u == T_DOOR ? (keys ? 0 : 1)
                              : 1;
        if (!w) continue;
        int nd = b + w;
        if (nd < 64 && nd < dist[s]) {
          dist[s] = nd;
          bucket[nd][bn[nd]++] = (uint8_t)s;
        }
      }
    }
  }
  return 400;
}

/* --- open list (binary heap on f) and closed set (open addressing on 64-bit hashes) --- */
typedef struct {
  int f, id;
} hent;
static hent *heap;
static int hn;
static void hpush(int f, int id) {
  int i = hn++;
  while (i) {
    int pa = (i - 1) / 2;
    if (heap[pa].f <= f) break;
    heap[i] = heap[pa];
    i = pa;
  }
  heap[i].f = f;
  heap[i].id = id;
}
static int hpop(void) {
  int top = heap[0].id;
  hent last = heap[--hn];
  int i = 0;
  for (;;) {
    int l = 2 * i + 1, r = l + 1, m = i;
    int mf = last.f;
    if (l < hn && heap[l].f < mf) m = l, mf = heap[l].f;
    if (r < hn && heap[r].f < mf) m = r;
    if (m == i) break;
    heap[i] = heap[m];
    i = m;
  }
  if (hn) heap[i] = last;
  return top;
}

static int apply(const node *s, uint8_t from, int d, node *out, int *goal) {
  int ni = from + step4[d];
  *goal = 0;
  if (ni < 0 || ni >= (int)EG_N) return 0;
  uint8_t n = (uint8_t)ni;
  eg_room r;
  memcpy(r.c, s->c, EG_N);
  uint8_t t = r.c[n] & T_MASK, p = n, keys = s->keys;
  switch (t) {
    case T_EMPTY:
    case T_PLATE:
    case T_GATE_OPEN:
    case T_CRUMBLE:
      break; /* open floor: an act only when the player is pinned (or onto rotten carpet) */
    case T_DIRT:
    case T_TOKEN:
      r.c[n] = T_EMPTY;
      break;
    case T_KEY:
      r.c[n] = T_EMPTY;
      keys--;
      break;
    case T_ROCK: {
      if (d < 2) return 0;
      if (!eg_push(&r, n, (uint8_t)(n + step4[d]))) return 0;
      break;
    }
    case T_BRICK:
      r.c[n] = T_EMPTY;
      p = from;
      break;
    case T_DOOR:
      if (keys) return 0;
      *goal = 1;
      return 1;
    default:
      return 0;
  }
  if (p != from) eg_leave(&r, from);
  if (!settle_at(&r, p)) return 0;
  memcpy(out->c, r.c, EG_N);
  out->land = p;
  out->keys = keys;
  out->p = canon(out->c, p);
  return 1;
}

typedef struct {
  uint64_t *keys;
  size_t mask;
} hset;
static int hset_add(hset *h, uint64_t k) {
  size_t i = (size_t)k & h->mask;
  while (h->keys[i]) {
    if (h->keys[i] == k) return 0;
    i = (i + 1) & h->mask;
  }
  h->keys[i] = k;
  return 1;
}

static void stats_for(const node *nodes, int id, eg_stats *st) {
  for (; nodes[id].parent >= 0; id = nodes[id].parent) {
    const node *a = &nodes[id], *pa = &nodes[a->parent];
    uint8_t n = (uint8_t)(a->from + step4[a->dir]);
    uint8_t t = pa->c[n] & T_MASK;
    st->actions++;
    if (t == T_ROCK) st->pushes++;
    if (t == T_BRICK) st->breaks++;
    if (t == T_DIRT) st->digs++;
    /* did the act set CRTs moving? compare rock counts in place */
    for (int i = 0; i < (int)EG_N; i++)
      if ((pa->c[i] & T_MASK) == T_ROCK && (a->c[i] & T_MASK) != T_ROCK && i != n) {
        st->drops++;
        break;
      }
  }
}

static size_t walk(const uint8_t *c, uint8_t at, uint8_t to, char *moves, size_t m, size_t cap) {
  uint8_t reg[EG_N], prev[EG_N], path[EG_N];
  if (at == to) return m;
  region(c, at, reg, prev);
  int pl = 0;
  for (uint8_t q = to; q != at && pl < (int)EG_N; q = prev[q]) path[pl++] = q;
  uint8_t cur = at;
  while (pl) {
    uint8_t nx = path[--pl];
    int dd = nx - cur;
    int di = dd == -16 ? 0 : dd == 16 ? 1 : dd == -1 ? 2 : 3;
    if (m + 1 < cap) moves[m++] = step_ch[di];
    cur = nx;
  }
  return m;
}

static size_t act(const uint8_t *c, uint8_t from, int dir, char *moves, size_t m, size_t cap) {
  uint8_t tgt = (uint8_t)(from + step4[dir]);
  if (m + 1 < cap) moves[m++] = (c[tgt] & T_MASK) == T_BRICK ? hit_ch[dir] : step_ch[dir];
  return m;
}

static size_t emit_plan(const eg_room *r0, const node *nodes, int last, uint8_t goal_from, int goal_dir, char *moves,
                        size_t cap) {
  static int chain[1 << 16];
  int n = 0;
  for (int id = last; id >= 0 && n < (1 << 16); id = nodes[id].parent) chain[n++] = id;
  size_t m = 0;
  uint8_t at = r0->start;
  for (int k = n - 2; k >= 0; k--) {
    const node *before = &nodes[chain[k + 1]], *a = &nodes[chain[k]];
    m = walk(before->c, at, a->from, moves, m, cap);
    m = act(before->c, a->from, a->dir, moves, m, cap);
    at = a->land;
  }
  m = walk(nodes[chain[0]].c, at, goal_from, moves, m, cap);
  m = act(nodes[chain[0]].c, goal_from, goal_dir, moves, m, cap);
  moves[m] = 0;
  return m;
}

int eg_solve(const eg_room *r, int budget, char *moves, size_t cap, eg_stats *st) {
  node *nodes = malloc(sizeof(node) * (size_t)(budget + 1));
  heap = malloc(sizeof(hent) * (size_t)(budget * 64 + 64));
  hset hs;
  hs.mask = 1;
  while (hs.mask < (size_t)budget * 8) hs.mask <<= 1;
  hs.keys = calloc(hs.mask, sizeof(uint64_t));
  hs.mask -= 1;
  hn = 0;
  memset(st, 0, sizeof *st);
  int count = 0, solved = 0;
  node *root = &nodes[count];
  memcpy(root->c, r->c, EG_N);
  root->keys = r->keys;
  root->land = r->start;
  root->p = canon(root->c, r->start);
  root->parent = -1;
  root->g = 0;
  hset_add(&hs, hash_state(root->c, root->p, root->keys));
  hpush(0, count++);
  while (hn && !solved) {
    int id = hpop();
    st->nodes++;
    node s = nodes[id];
    uint8_t reg[EG_N];
    int rn, single = pinned(s.c, s.land);
    if (single) {
      reg[0] = s.land;
      rn = 1;
    } else
      rn = region(s.c, s.land, reg, NULL);
    for (int i = 0; i < rn && !solved; i++) {
      for (int d = 0; d < 4; d++) {
        int ni = reg[i] + step4[d];
        if (ni < 0 || ni >= (int)EG_N) continue;
        if (!single && EG_FLOOR(s.c[ni])) continue; /* free walking */
        node nx;
        int goal;
        if (!apply(&s, reg[i], d, &nx, &goal)) continue;
        if (goal) {
          stats_for(nodes, id, st);
          st->actions++;
          st->steps = (int)emit_plan(r, nodes, id, reg[i], d, moves, cap);
          solved = 1;
          break;
        }
        if (count >= budget) continue;
        uint64_t h = hash_state(nx.c, nx.p, nx.keys);
        if (!hset_add(&hs, h)) continue;
        nx.parent = id;
        nx.from = reg[i];
        nx.dir = (uint8_t)d;
        nx.g = s.g + 1;
        nodes[count] = nx;
        hpush(nx.g + 3 * heuristic(nx.c, nx.land, nx.keys, r->door), count);
        count++;
      }
    }
  }
  free(nodes);
  free(heap);
  free(hs.keys);
  if (!solved) return 0;
  return eg_replay(r, moves);
}

int eg_trivial(const eg_room *r) {
  uint8_t seen[EG_N], q[EG_N];
  memset(seen, 0, sizeof seen);
  int n = 0, h = 0;
  q[n++] = r->start;
  seen[r->start] = 1;
  while (h < n) {
    uint8_t c = q[h++];
    for (int d = 0; d < 4; d++) {
      int s = c + step4[d];
      if (s < 0 || s >= (int)EG_N || seen[s]) continue;
      uint8_t t = r->c[s] & T_MASK;
      if (!EG_PASSABLE(t) && t != T_DOOR) continue;
      seen[s] = 1;
      if (t != T_DOOR) q[n++] = (uint8_t)s;
    }
  }
  for (int i = 0; i < (int)EG_N; i++)
    if ((r->c[i] & T_MASK) == T_KEY && !seen[i]) return 0;
  return seen[r->door];
}

int eg_sealed(const eg_room *r) {
  uint8_t seen[EG_N], q[EG_N];
  memset(seen, 0, sizeof seen);
  int n = 0, h = 0;
  q[n++] = r->start;
  seen[r->start] = 1;
  while (h < n) {
    uint8_t c = q[h++];
    for (int d = 0; d < 4; d++) {
      int s = c + step4[d];
      if (s < 0 || s >= (int)EG_N || seen[s]) continue;
      uint8_t t = r->c[s] & T_MASK;
      if (t == T_WALL) continue;
      seen[s] = 1;
      if (t != T_DOOR) q[n++] = (uint8_t)s;
    }
  }
  for (int i = 0; i < (int)EG_N; i++)
    if ((r->c[i] & T_MASK) == T_KEY && !seen[i]) return 1;
  return !seen[r->door];
}

int eg_replay(const eg_room *r0, const char *moves) {
  eg_room r = *r0;
  uint8_t p = r.start, keys = r.keys;
  for (const char *m = moves; *m; m++) {
    int d = -1, hit = 0;
    for (int k = 0; k < 4; k++) {
      if (*m == step_ch[k]) d = k;
      if (*m == hit_ch[k]) d = k, hit = 1;
    }
    if (d < 0) return 0;
    uint8_t n = (uint8_t)(p + step4[d]);
    uint8_t t = r.c[n] & T_MASK;
    if (hit) {
      if (t != T_BRICK) return 0;
      r.c[n] = T_EMPTY;
    } else if (t == T_DOOR) {
      return keys == 0 && m[1] == 0;
    } else if (t == T_ROCK) {
      if (d < 2 || (r.c[n] & F_FALL) || !eg_push(&r, n, (uint8_t)(n + step4[d]))) return 0;
      eg_leave(&r, p);
      p = n;
    } else if (EG_PASSABLE(t)) {
      if (t == T_KEY) keys--;
      if (t == T_DIRT || t == T_TOKEN || t == T_KEY) r.c[n] = T_EMPTY;
      eg_leave(&r, p);
      p = n;
    } else
      return 0;
    if (!settle_at(&r, p)) return 0;
  }
  return 0;
}
