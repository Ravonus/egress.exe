/* Host room solver (see eg_solve.h). */
#include "eg_solve.h"
#include "eg_osolve.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

const char *const eg_trick_name[TRICK_COUNT] = {"burn", "cold", "short", "weight", "glass", "rhythm",
                                                "rush", "steer", "mirror", "stare"};

enum { ACT_ENTER = 0, ACT_COMBINE = 1, ACT_USE = 2 };

typedef struct {
  uint8_t c[EG_N];
  uint8_t pk[EG_MAX_ITEM]; /* the pocket, sorted (0 = empty slot, at the end) */
  uint8_t np;
  uint8_t p;    /* canonical player cell (the lowest cell of its open region, or the cell it holds a CRT from) */
  uint8_t land; /* where the act left the player (for the plan) */
  uint8_t keys; /* keycards still on the board */
  int parent;
  uint8_t from, dir, kind, a, b, rx; /* the act: from this cell toward step4[dir]; bench pieces; the reaction */
  uint8_t nc, comb[3][2];            /* a use: the bench recipes made just before it */
  int g;
  int meaningful; /* meaningful acts available here (pushes, breaks, bench, uses), counted on expansion */
  /* B2 hook: living obstacles (position, direction, phase, awake bits), part of the state and its hash. Empty in B1;
   * world_tick advances them after every act. */
  uint8_t nent, ent[EG_MAX_ENT][4];
} node;

/* B2 hook: one world step after a player act, in the tick order player move -> tile cracks/pushes (eg_step_off,
 * eg_push, eg_gravity: done by the act) -> mobs (here) -> reactions. Returns 0 if the step kills the player. B1 has
 * no mobs: nothing moves. */
static int world_tick(eg_room *r, node *s, uint8_t p) {
  (void)r;
  (void)s;
  (void)p;
  return 1;
}

static const int step4[4] = {-16, 16, -1, 1};
static const char step_ch[4] = {'U', 'D', 'L', 'R'};
static const char hit_ch[4] = {'u', 'd', 'l', 'r'};

static uint64_t hash_state(const node *s) {
  uint64_t h = 1469598103934665603ull;
  for (int i = 0; i < (int)EG_N; i++) h = (h ^ s->c[i]) * 1099511628211ull;
  for (int i = 0; i < (int)EG_MAX_ITEM; i++) h = (h ^ s->pk[i]) * 1099511628211ull;
  h = (h ^ s->p) * 1099511628211ull;
  h = (h ^ s->keys) * 1099511628211ull;
  for (int k = 0; k < s->nent; k++)
    for (int b = 0; b < 4; b++) h = (h ^ s->ent[k][b]) * 1099511628211ull;
  return h ? h : 1;
}

static void pk_add(uint8_t *pk, uint8_t *np, uint8_t piece) {
  int i = *np;
  while (i > 0 && pk[i - 1] > piece) {
    pk[i] = pk[i - 1];
    i--;
  }
  pk[i] = piece;
  (*np)++;
}
static void pk_del(uint8_t *pk, uint8_t *np, int k) {
  for (int i = k; i + 1 < *np; i++) pk[i] = pk[i + 1];
  (*np)--;
  pk[*np] = 0;
}
static int pk_find(const uint8_t *pk, int np, uint8_t piece, int skip) {
  for (int i = 0; i < np; i++)
    if (pk[i] == piece && i != skip) return i;
  return -1;
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

/* Foam or a token with no CRT within reach of it (the 3x3 box from two rows above down to its own row) is dug
 * without consequence: the search walks through it as if it were floor and leaves it on the board (the plan's
 * replay digs it for real, so a plan this gets wrong is rejected, never accepted). It keeps the state space to the
 * digs that matter. */
static int inert(const uint8_t *c, int s) {
  uint8_t t = c[s] & T_MASK;
  if (t != T_DIRT && t != T_TOKEN) return 0;
  int x = s & 15, y = s >> 4;
  for (int dy = -2; dy <= 0; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      int xx = x + dx, yy = y + dy;
      if (xx < 0 || xx > 15 || yy < 0) continue;
      if ((c[yy * 16 + xx] & T_MASK) == T_ROCK) return 0;
    }
  return 1;
}
#define WALKABLE(c, s) (EG_FLOOR((c)[s]) || inert(c, s))

/* the open region around p: cells reached through open floor (and inert foam) */
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
      if (s < 0 || s >= (int)EG_N || seen[s] || !WALKABLE(c, s)) continue;
      seen[s] = 1;
      if (prev) prev[s] = q;
      out[n++] = (uint8_t)s;
    }
  }
  return n;
}

/* the player can't walk freely: holding a CRT up, or standing on fragile floor (every step off it counts) */
static int pinned(const uint8_t *c, uint8_t p) { return EG_FRAGILE(c[p] & T_MASK) || holding(c); }

static uint8_t canon(const uint8_t *c, uint8_t p) {
  uint8_t reg[EG_N];
  if (pinned(c, p)) return p;
  int n = region(c, p, reg, NULL);
  uint8_t m = p;
  for (int i = 0; i < n; i++)
    if (reg[i] < m) m = reg[i];
  return m;
}

/* distance-ish to what is left: the pieces still lying in the room first (collect them all: the bench and the locks
 * need them), then a keycard, then the door; cells cost by how hard they are to cross */
#define HB 160
static int heuristic(const uint8_t *c, uint8_t p, uint8_t keys, uint8_t door) {
  int dist[EG_N];
  for (int i = 0; i < (int)EG_N; i++) dist[i] = 1 << 20;
  static uint8_t bucket[HB][EG_N];
  int bn[HB];
  memset(bn, 0, sizeof bn);
  dist[p] = 0;
  bucket[0][bn[0]++] = p;
  int items = 0, near_item = -1, near_goal = -1, dead = 0, gate = 0, near_trig = -1;
  for (int i = 0; i < (int)EG_N; i++) {
    uint8_t t = c[i] & T_MASK;
    if (t == T_ITEM) items++;
    if (t == T_DEAD) dead = 1;
    if (t == T_GATE) gate = 1;
  }
  /* a shut gate's trigger: the water a socket hangs on, or the plate */
  for (int b = 0; b < HB; b++) {
    for (int k = 0; k < bn[b]; k++) {
      uint8_t q = bucket[b][k];
      if (dist[q] != b) continue;
      uint8_t t = c[q] & T_MASK;
      if (t == T_ITEM && near_item < 0) near_item = b;
      if (near_goal < 0 && (keys ? t == T_KEY : q == door)) near_goal = b;
      if (near_trig < 0 && ((dead && t == T_WATER) || (gate && t == T_PLATE))) near_trig = b;
      if (near_goal >= 0 && (near_item >= 0 || !items) && (near_trig >= 0 || !(dead | gate))) goto done;
      for (int d = 0; d < 4; d++) {
        int s = q + step4[d];
        if (s < 0 || s >= (int)EG_N) continue;
        uint8_t u = c[s] & T_MASK;
        int w;
        switch (u) {
          case T_WALL:
          case T_SOCKET:
          case T_SOCKET_ON:
          case T_BLOCK:
          case T_BLOCK_PLATE:
            w = 0;
            break;
          case T_ROCK: w = 4; break;
          case T_BRICK: w = 2; break;
          case T_PIT: w = 5; break;
          case T_GATE:
          case T_DEAD:
          case T_WATER:
          case T_PAPER: w = 6; break;
          case T_DOOR: w = keys ? 0 : 1; break;
          default: w = 1; break;
        }
        if (!w) continue;
        int nd = b + w;
        if (nd < HB && nd < dist[s]) {
          dist[s] = nd;
          bucket[nd][bn[nd]++] = (uint8_t)s;
        }
      }
    }
  }
done:
  if (near_goal < 0) near_goal = 300;
  return near_goal + keys * 24 + (items ? (near_item < 0 ? 100 : near_item) + 16 * items : 0) +
         ((dead | gate) ? (near_trig < 0 ? 100 : near_trig) + 12 : 0);
}

/* --- open list (binary heap on f) and closed set (open addressing on 64-bit hashes) --- */
typedef struct {
  int f, id;
} hent;
static hent *heap;
static int hn, hcap;
static void hpush(int f, int id) {
  if (hn == hcap) {
    hcap = hcap ? hcap * 2 : 1024;
    heap = realloc(heap, sizeof(hent) * (size_t)hcap);
  }
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

#define KIDS 4096
static const eg_room *room0;


/* enter the neighbour of `from` in direction d (dig, collect, pick up, push, break, walk when pinned, the door) */
static int act_enter(const node *s, uint8_t from, int d, node *out, int *goal) {
  int ni = from + step4[d];
  *goal = 0;
  if (ni < 0 || ni >= (int)EG_N) return 0;
  uint8_t n = (uint8_t)ni;
  eg_room r;
  memcpy(r.c, s->c, EG_N);
  *out = *s;
  uint8_t t = r.c[n] & T_MASK, p = n, keys = s->keys;
  switch (t) {
    case T_EMPTY:
    case T_PLATE:
    case T_GATE_OPEN:
    case T_CRUMBLE:
    case T_PLANK:
    case T_GLITCH:
    case T_WORN:
    case T_WORN_1:
    case T_WORN_2:
    case T_CEIL:
    case T_CEIL_1:
    case T_GLASS:
    case T_ICE:
    case T_ICE_1:
      break; /* open floor: an act when the player is pinned, or onto fragile floor (stepping off it wears it) */
    case T_DIRT:
    case T_TOKEN:
      r.c[n] = T_EMPTY;
      break;
    case T_KEY:
      r.c[n] = T_EMPTY;
      keys--;
      break;
    case T_ITEM:
      if (out->np >= EG_MAX_ITEM) return 0;
      pk_add(out->pk, &out->np, eg_item_at(room0, n));
      r.c[n] = T_EMPTY;
      break;
    case T_ROCK:
      if (d < 2) return 0;
      if (!eg_push(&r, n, (uint8_t)(n + step4[d]))) return 0;
      break;
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
  if (!world_tick(&r, out, p)) return 0;
  memcpy(out->c, r.c, EG_N);
  out->land = p;
  out->keys = keys;
  out->p = canon(out->c, p);
  out->kind = ACT_ENTER;
  out->nc = 0;
  return 1;
}

/* a pocket after up to 3 bench recipes: what the player could make before a use. Bench acts commute with walking
 * and only matter for what they let you use, so the search makes them only right before a use (a macro act). */
typedef struct {
  uint8_t pk[EG_MAX_ITEM], np, nc, comb[3][2];
} pocket_q;
static int closure(const uint8_t *pk, uint8_t np, pocket_q *out, int cap) {
  int n = 1;
  memset(&out[0], 0, sizeof out[0]);
  memcpy(out[0].pk, pk, EG_MAX_ITEM);
  out[0].np = np;
  for (int h = 0; h < n; h++) {
    if (out[h].nc == 3) continue;
    for (int i = 0; i < out[h].np; i++)
      for (int j = i + 1; j < out[h].np; j++) {
        uint8_t a = out[h].pk[i], b = out[h].pk[j], res = eg_recipe(a, b);
        if (!res || n >= cap) continue;
        pocket_q q = out[h];
        pk_del(q.pk, &q.np, j);
        pk_del(q.pk, &q.np, i);
        pk_add(q.pk, &q.np, res);
        q.comb[q.nc][0] = a;
        q.comb[q.nc][1] = b;
        q.nc++;
        int dup = 0;
        for (int k = 0; k < n && !dup; k++) dup = out[k].np == q.np && !memcmp(out[k].pk, q.pk, EG_MAX_ITEM);
        if (!dup) out[n++] = q;
      }
  }
  return n;
}

/* could `piece` change a cell of this type at all (the matrix, before trying it)? */
static int can_react(uint8_t piece, uint8_t type) {
  uint8_t eff = PC_EFF(piece), tags = PC_TAGS(piece), tt = eg_tile_tag(type);
  if (eff == EFF_SHOCK) return type == T_WATER || type == T_SOCKET;
  if (eff == EFF_LAY) return type == T_PLATE;
  if (tt == EG_TAGS) return 0;
  for (int k = 0; k < EG_TAGS; k++)
    if (tags & TG(k)) {
      uint8_t rx = eg_rx_table[k][tt];
      if (rx == RX_BURN || rx == RX_FREEZE || rx == RX_THAW) return 1;
    }
  return 0;
}

/* use pocket slot k on the neighbour of `from` in direction d */
static int act_use(const node *s, uint8_t from, int d, int k, uint64_t ban, node *out) {
  int ni = from + step4[d];
  if (ni < 0 || ni >= (int)EG_N) return 0;
  uint8_t piece = s->pk[k];
  /* the solver lays bricks on plates only (anywhere else they only wall things off) */
  if (PC_EFF(piece) == EFF_LAY && s->c[ni] != T_PLATE) return 0;
  eg_room r;
  memcpy(r.c, s->c, EG_N);
  uint8_t rx = eg_apply(&r, piece, (uint8_t)ni, &from, 1);
  if (!rx || (ban >> rx) & 1u) return 0;
  if (!settle_at(&r, from)) return 0;

  *out = *s;
  if (!world_tick(&r, out, from)) return 0;
  memcpy(out->c, r.c, EG_N);
  pk_del(out->pk, &out->np, k);
  out->land = from;
  out->p = canon(out->c, from);
  out->kind = ACT_USE;
  out->a = piece;
  out->rx = rx;
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
  int path = 0;
  long branch = 0;
  for (; nodes[id].parent >= 0; id = nodes[id].parent) {
    const node *a = &nodes[id], *pa = &nodes[a->parent];
    path++;
    branch += pa->meaningful;
    st->actions++;
    if (a->kind == ACT_COMBINE) {
      st->combines++;
      continue;
    }
    if (a->kind == ACT_USE) {
      st->uses++;
      st->combines += a->nc;
      st->reactions |= 1ull << a->rx;
    } else {
      uint8_t n = (uint8_t)(a->from + step4[a->dir]);
      uint8_t t = pa->c[n] & T_MASK;
      if (t == T_ROCK) st->pushes++;
      if (t == T_BRICK) st->breaks++;
      if (t == T_DIRT) st->digs++;
      if (t == T_ITEM) st->picks++;
    }
    /* did the act set CRTs moving? */
    for (int i = 0; i < (int)EG_N; i++)
      if ((pa->c[i] & T_MASK) == T_ROCK && (a->c[i] & T_MASK) != T_ROCK &&
          i != (uint8_t)(a->from + step4[a->dir])) {
        st->drops++;
        break;
      }
  }
  st->branching100 = path ? (int)(branch * 100 / path) : 0;
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

static size_t put(char *moves, size_t m, size_t cap, char ch) {
  if (m + 1 < cap) moves[m++] = ch;
  return m;
}

static size_t act(const node *before, const node *a, char *moves, size_t m, size_t cap) {
  if (a->kind == ACT_COMBINE) {
    m = put(moves, m, cap, 'C');
    m = put(moves, m, cap, (char)('a' + a->a - 1));
    return put(moves, m, cap, (char)('a' + a->b - 1));
  }
  if (a->kind == ACT_USE) {
    for (int k = 0; k < a->nc; k++) {
      m = put(moves, m, cap, 'C');
      m = put(moves, m, cap, (char)('a' + a->comb[k][0] - 1));
      m = put(moves, m, cap, (char)('a' + a->comb[k][1] - 1));
    }
    m = put(moves, m, cap, 'P');
    m = put(moves, m, cap, (char)('a' + a->a - 1));
    return put(moves, m, cap, step_ch[a->dir]);
  }
  uint8_t tgt = (uint8_t)(a->from + step4[a->dir]);
  return put(moves, m, cap, (before->c[tgt] & T_MASK) == T_BRICK ? hit_ch[a->dir] : step_ch[a->dir]);
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
    m = act(before, a, moves, m, cap);
    at = a->land;
  }
  m = walk(nodes[chain[0]].c, at, goal_from, moves, m, cap);
  m = put(moves, m, cap, step_ch[goal_dir]);
  moves[m] = 0;
  return m;
}

/* B2: a room with living obstacles is solved by the tick solver (every state, fairness, the passive-safe plan) */
static int solve_obstacles(const eg_room *r, char *moves, size_t cap, eg_stats *st) {
  eg_ostats o;
  memset(st, 0, sizeof *st);
  st->ob_rooms = 1;
  int ok = eg_osolve(r, EG_OB_CAP, EGO_NOISE, moves, cap, &o);
  st->states = o.states;
  st->dead = o.dead;
  st->fatal_depth = o.fatal_depth;
  st->dead_at_start = o.start_live ? 0 : EG_DEAD_DOOR;
  st->ob_capped = o.capped;
  st->ob_start_live = o.start_live;
  st->ob_idle_ok = o.idle_ok;
  st->ob_idle_hits = o.idle_hits;
  st->ob_plan_ok = o.plan_ok;
  st->ob_ticks = o.ticks;
  st->ob_hits = o.hits;
  st->ob_rescue = o.rescue;
  st->steps = o.ticks;
  st->actions = o.moves;
  st->nodes = o.states;
  return ok;
}

int eg_solve_ban(const eg_room *r, int budget, uint64_t ban, char *moves, size_t cap, eg_stats *st) {
  if (r->nob) return solve_obstacles(r, moves, cap, st);
  node *nodes = malloc(sizeof(node) * (size_t)(budget + 1));
  hset hs;
  hs.mask = 1;
  while (hs.mask < (size_t)budget * 8) hs.mask <<= 1;
  hs.keys = calloc(hs.mask, sizeof(uint64_t));
  hs.mask -= 1;
  hn = 0;
  room0 = r;
  memset(st, 0, sizeof *st);
  st->fatal_depth = -1;
  int count = 0, solved = 0;
  node *root = &nodes[count];
  memset(root, 0, sizeof *root);
  memcpy(root->c, r->c, EG_N);
  root->keys = r->keys;
  root->land = r->start;
  root->p = canon(root->c, r->start);
  root->parent = -1;
  /* "unwinnable before any player action" (a generator or mob fault) is told apart from a player's mistake */
  st->dead_at_start = eg_dead(r, r->start, NULL, 0);
  if (st->dead_at_start) {
    free(nodes);
    free(hs.keys);
    return 0;
  }
  hset_add(&hs, hash_state(root));
  hpush(0, count++);
  static uint32_t tried[48][EG_N];
  while (hn && !solved) {
    int id = hpop();
    st->nodes++;
    node s = nodes[id];
    if (getenv("EG_DEBUG") && st->nodes % 500 == 1) /* progress: EG_DEBUG=1 egtool solve ... */
      fprintf(stderr, "node %d g%d land%d pocket %d keys %d h%d\n", st->nodes, s.g, s.land, s.np, s.keys,
              heuristic(s.c, s.land, s.keys, r->door));
    uint8_t reg[EG_N];
    int rn, single = pinned(s.c, s.land), meaningful = 0;
    if (single) {
      reg[0] = s.land;
      rn = 1;
    } else
      rn = region(s.c, s.land, reg, NULL);
    static node kids[KIDS];
    int nk = 0;
    pocket_q pq[48];
    int npq = s.np ? closure(s.pk, s.np, pq, 48) : 0;
    memset(tried, 0, sizeof(tried[0]) * (size_t)(npq ? npq : 1));
    for (int i = 0; i < rn && !solved; i++) {
      for (int d = 0; d < 4; d++) {
        int ni = reg[i] + step4[d];
        if (ni < 0 || ni >= (int)EG_N) continue;
        uint8_t tv = s.c[ni] & T_MASK;
        /* uses: a piece (made on the bench just before, if need be) on a cell the matrix acts on */
        if (npq && (tv == T_WATER || tv == T_ICE || tv == T_ICE_1 || tv == T_PAPER || tv == T_PLANK || tv == T_SOCKET ||
                    tv == T_PLATE)) {
          for (int qi = 0; qi < npq; qi++) {
            node tmp = s;
            memcpy(tmp.pk, pq[qi].pk, EG_MAX_ITEM);
            tmp.np = pq[qi].np;
            for (int k = 0; k < tmp.np && nk < KIDS; k++) {
              uint8_t pc = tmp.pk[k];
              if (k > 0 && pc == tmp.pk[k - 1]) continue;
              if (!can_react(pc, tv) || tried[qi][ni] & (1u << (pc & 31))) continue;
              if (!act_use(&tmp, reg[i], d, k, ban, &kids[nk])) continue;
              meaningful++;
              if (eg_ndirty <= EG_DIRTY_MAX)
                for (int q = 0; q < eg_ndirty; q++) tried[qi][eg_dirty[q]] |= 1u << (pc & 31);
              kids[nk].from = reg[i];
              kids[nk].dir = (uint8_t)d;
              kids[nk].nc = pq[qi].nc;
              memcpy(kids[nk].comb, pq[qi].comb, sizeof pq[qi].comb);
              nk++;
            }
          }
        }
        if (!single && WALKABLE(s.c, ni)) continue; /* free walking */
        int goal;
        if (nk >= KIDS) continue;
        if (!act_enter(&s, reg[i], d, &kids[nk], &goal)) continue;
        if (goal) {
          nodes[id].meaningful = meaningful;
          stats_for(nodes, id, st);
          st->actions++;
          st->steps = (int)emit_plan(r, nodes, id, reg[i], d, moves, cap);
          solved = 1;
          break;
        }
        if (tv == T_ROCK || tv == T_BRICK) meaningful++;
        kids[nk].from = reg[i];
        kids[nk].dir = (uint8_t)d;
        nk++;
      }
    }
    if (solved) break;
    nodes[id].meaningful = meaningful;
    for (int k = 0; k < nk; k++) {
      node *nx = &kids[k];
      if (count >= budget) break;
      if (!hset_add(&hs, hash_state(nx))) continue;
      /* a dead end (eg_dead: sound, never a false alarm) is counted and never expanded */
      st->states++;
      {
        eg_room t = *r;
        memcpy(t.c, nx->c, EG_N);
        if (eg_dead(&t, nx->land, nx->pk, nx->np)) {
          st->dead++;
          if (st->fatal_depth < 0 || s.g + 1 < st->fatal_depth) st->fatal_depth = s.g + 1;
          continue;
        }
      }
      nx->parent = id;
      nx->g = s.g + 1;
      nx->meaningful = 0;
      nodes[count] = *nx;
      hpush(nx->g + 3 * heuristic(nx->c, nx->land, nx->keys, r->door), count);
      count++;
    }
  }
  free(nodes);
  free(hs.keys);
  if (!solved) return 0;
  return eg_replay(r, moves);
}

int eg_solve(const eg_room *r, int budget, char *moves, size_t cap, eg_stats *st) {
  return eg_solve_ban(r, budget, 0, moves, cap, st);
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
      if ((!EG_PASSABLE(t) && t != T_DOOR) || t == T_GLASS || t == T_GLITCH) continue; /* bridges: the puzzle */
      seen[s] = 1;
      if (t != T_DOOR) q[n++] = (uint8_t)s;
    }
  }
  for (int i = 0; i < (int)EG_N; i++)
    if ((r->c[i] & T_MASK) == T_KEY && !seen[i]) return 0;
  return seen[r->door];
}

int eg_glitch_ok(const eg_room *r) {
  uint8_t seen[EG_N], q[EG_N];
  memset(seen, 0, sizeof seen);
  for (int i = 0; i < (int)EG_N; i++) {
    if ((r->c[i] & T_MASK) != T_GLITCH || seen[i]) continue;
    int n = 0, h = 0;
    q[n++] = (uint8_t)i;
    seen[i] = 1;
    while (h < n) {
      uint8_t c = q[h++];
      for (int d = 0; d < 4; d++) {
        int s = c + step4[d];
        if (s < 0 || s >= (int)EG_N || seen[s] || (r->c[s] & T_MASK) != T_GLITCH) continue;
        seen[s] = 1;
        q[n++] = (uint8_t)s;
      }
    }
    if (n > (int)EG_GLITCH_RUN) return 0;
  }
  return 1;
}

static int sealed2(const eg_room *r, uint8_t type, uint8_t type2);
int eg_sealed_with(const eg_room *r, uint8_t type) { return sealed2(r, type, type); }
static int sealed2(const eg_room *r, uint8_t type, uint8_t type2) {
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
      if (t == T_WALL || t == T_SOCKET || t == type || t == type2) continue;
      seen[s] = 1;
      if (t != T_DOOR) q[n++] = (uint8_t)s;
    }
  }
  for (int i = 0; i < (int)EG_N; i++)
    if ((r->c[i] & T_MASK) == T_KEY && !seen[i]) return 1;
  return !seen[r->door];
}

int eg_sealed(const eg_room *r) { return eg_sealed_with(r, T_WALL); }

int eg_trick_needed(const eg_room *r) {
  static const uint8_t obstacle[TRICK_COUNT] = {T_PAPER, T_WATER, T_DEAD, T_GATE, T_GLASS, T_GLITCH};
  if (r->trick >= TRICK_COUNT) return 0;
  if (r->trick >= TRICK_GLASS) {
    /* the void is crossed only on the bridge, or over a pit a CRT filled: sound while the room has no CRT */
    for (int i = 0; i < (int)EG_N; i++)
      if ((r->c[i] & T_MASK) == T_ROCK) return 0;
    eg_room t = *r;
    for (int i = 0; i < (int)EG_N; i++) /* the other locks open */
      if (t.c[i] == T_PAPER || t.c[i] == T_WATER) t.c[i] = T_EMPTY;
    return sealed2(&t, T_PIT, obstacle[r->trick]);
  }
  if (r->trick == TRICK_WEIGHT) /* sound only while every plate sits where no CRT can go (the north wall) */
    for (int i = (int)EG_W; i < (int)EG_N; i++)
      if ((r->c[i] & T_MASK) == T_PLATE) return 0;
  if (r->trick == TRICK_COLD) {
    /* a flood is removed only by freezing; but a frozen supporting flood is another band: check this one alone */
    eg_room t = *r;
    for (int i = 0; i < (int)EG_N; i++)
      if (t.c[i] == T_PAPER) t.c[i] = T_EMPTY; /* the other locks open */
    return eg_sealed_with(&t, T_WATER);
  }
  if (r->trick == TRICK_BURN) {
    eg_room t = *r;
    for (int i = 0; i < (int)EG_N; i++)
      if (t.c[i] == T_WATER) t.c[i] = T_EMPTY;
    return eg_sealed_with(&t, T_PAPER);
  }
  return eg_sealed_with(r, obstacle[r->trick]);
}

int eg_herrings_safe(const eg_room *r, int budget, int *bad_a, int *bad_b) {
  static char moves[8192];
  eg_stats st;
  for (int h = r->nitem - r->herrings; h < r->nitem; h++)
    for (int j = 0; j < r->nitem; j++) {
      if (j == h || j >= r->nitem - r->herrings) continue; /* two herrings lose nothing needed */
      if (!eg_recipe(r->item_piece[h], r->item_piece[j])) continue;
      /* the misuse: both pieces gone, the result kept (it lies where the needed piece lay) */
      eg_room t = *r;
      t.c[t.item_cell[h]] = T_EMPTY;
      t.item_piece[j] = eg_recipe(r->item_piece[h], r->item_piece[j]);
      if (!eg_solve(&t, budget, moves, sizeof moves, &st)) {
        if (bad_a) *bad_a = r->item_piece[h];
        if (bad_b) *bad_b = r->item_piece[j];
        return 0;
      }
    }
  return 1;
}

static int dir_of(char ch) {
  for (int k = 0; k < 4; k++)
    if (ch == step_ch[k]) return k;
  return -1;
}

int eg_replay(const eg_room *r0, const char *moves) {
  eg_room r = *r0;
  uint8_t p = r.start, keys = r.keys, pk[EG_MAX_ITEM], np = 0;
  memset(pk, 0, sizeof pk);
  for (const char *m = moves; *m; m++) {
    if (*m == 'C') {
      if (!m[1] || !m[2]) return 0;
      uint8_t a = (uint8_t)(m[1] - 'a' + 1), b = (uint8_t)(m[2] - 'a' + 1), res = eg_recipe(a, b);
      int ia = pk_find(pk, np, a, -1);
      int ib = ia < 0 ? -1 : pk_find(pk, np, b, ia);
      if (!res || ia < 0 || ib < 0) return 0;
      pk_del(pk, &np, ia > ib ? ia : ib);
      pk_del(pk, &np, ia > ib ? ib : ia);
      pk_add(pk, &np, res);
      m += 2;
      continue;
    }
    if (*m == 'P') {
      if (!m[1] || !m[2]) return 0;
      uint8_t a = (uint8_t)(m[1] - 'a' + 1);
      int d = dir_of(m[2]), ia = pk_find(pk, np, a, -1);
      if (d < 0 || ia < 0) return 0;
      if (!eg_apply(&r, a, (uint8_t)(p + step4[d]), &p, 1)) return 0;
      pk_del(pk, &np, ia);
      m += 2;
      if (!settle_at(&r, p)) return 0;
      continue;
    }
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
      if (t == T_ITEM) {
        if (np >= EG_MAX_ITEM) return 0;
        pk_add(pk, &np, eg_item_at(&r, n));
      }
      if (t == T_DIRT || t == T_TOKEN || t == T_KEY || t == T_ITEM) r.c[n] = T_EMPTY;
      eg_leave(&r, p);
      p = n;
    } else
      return 0;
    if (!settle_at(&r, p)) return 0;
  }
  return 0;
}

int eg_obstacle_load(const eg_room *r, const eg_stats *st) {
  int load = 0;
  for (int k = 0; k < r->nob; k++) load += eg_ob_cost[OBA_RULE(r->ob[k][0])];
  int w = st->ob_rescue / 100;
  return load + (w > 10 ? 10 : w);
}

int eg_difficulty(const eg_room *r, const eg_stats *st) {
  int sup = 0;
  if (r->nob) {
    int cost = 0, w = st->ob_rescue / 50;
    for (int k = 0; k < r->nob; k++) cost += eg_ob_cost[OBA_RULE(r->ob[k][0])];
    return st->ob_ticks + 6 * cost + (w > 30 ? 30 : w) + 2 * (r->nob - 1);
  }
  for (int k = 0; k < TRICK_COUNT; k++)
    if (r->support & (1u << k)) sup++;
  return st->steps + 3 * st->branching100 / 100 + 4 * r->depth + 2 * sup;
}

uint16_t eg_room_mech(const eg_room *r) {
  uint16_t m = (uint16_t)(1u << (MK_BURN + r->trick));
  for (int k = 0; k < TRICK_COUNT; k++)
    if (r->support & (1u << k)) m |= (uint16_t)(1u << (MK_BURN + k));
  for (int i = 0; i < (int)EG_N; i++) {
    uint8_t t = r->c[i] & T_MASK;
    if (t == T_ROCK) m |= 1u << MK_CRT;
    if (t == T_BRICK) m |= 1u << MK_DRYWALL;
    if (t >= T_WORN && t <= T_CEIL_1) m |= 1u << MK_STEPS;
  }
  return m;
}

int eg_insights(const eg_room *r) {
  int n = 1;
  for (int k = 0; k < TRICK_COUNT; k++)
    if (k != r->trick && (r->support & (1u << k))) n++;
  return n;
}
