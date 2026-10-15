/* Host tick solver for rooms with living obstacles (see eg_osolve.h). */
#include "eg_osolve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int step4[4] = {-16, 16, -1, 1}; /* U D L R */
static const char move_ch[4] = {'U', 'D', 'L', 'R'};
static const uint8_t move_face[4] = {0, 4, 6, 2}; /* EG_FACE_N S W E */
static const char turn_ch[4] = {'n', 'e', 's', 'w'};

/* one simulated moment between ticks */
typedef struct {
  eg_room r;
  eg_ob ob[EG_MAX_OB];
  uint8_t p, face, tick, won;
  uint8_t cr[4]; /* crates (ordered mode: by identity; 255 lost) */
} sim;

typedef struct {
  eg_room base; /* crates lifted off, no F_OB */
  int nd;
  uint8_t dcell[EG_N];
  int ncr, flags, sentry, p2, kl;
} ctx;

static void lift(ctx *x, const eg_room *r0) {
  memset(x, 0, sizeof *x);
  x->base = *r0;
  for (int i = 0; i < (int)EG_N; i++) {
    uint8_t v = r0->c[i] & (uint8_t)~F_OB, t = v & T_MASK;
    if ((r0->c[i] & T_MASK) == T_ROCK) v = r0->c[i];
    x->base.c[i] = v;
    if (EG_CRATE(v)) {
      x->base.c[i] = v == T_BLOCK_PLATE ? T_PLATE : T_EMPTY;
      if (x->ncr < 4) x->ncr++;
    }
    if (EG_FRAGILE(t) || t == T_PARTITION || t == T_KEY || t == T_GATE || t == T_GATE_OPEN || t == T_TOKEN ||
        t == T_ITEM || t == T_DIRT)
      x->dcell[x->nd++] = (uint8_t)i;
  }
  for (int k = 0; k < r0->nob; k++) {
    uint8_t ru = OBA_RULE(r0->ob[k][0]);
    if (ru == OB_SENTRY) x->sentry = 1;
    if (r0->ob[k][0] & OBA_P2) x->p2 = 1;
  }
  x->kl = 4 + x->nd + x->ncr + 4 * r0->nob;
}

static void encode(const ctx *x, const sim *s, uint8_t *k) {
  int n = 0;
  k[n++] = s->p;
  k[n++] = x->sentry ? s->face : 0;
  k[n++] = x->p2 ? (s->tick & 1) : 0;
  k[n++] = s->won;
  for (int d = 0; d < x->nd; d++) k[n++] = s->r.c[x->dcell[d]] & (uint8_t)~F_OB;
  if (x->flags & EGO_ORDERED)
    for (int c = 0; c < x->ncr; c++) k[n++] = s->cr[c];
  else {
    int m = 0;
    for (int i = 0; i < (int)EG_N && m < x->ncr; i++)
      if (EG_CRATE(s->r.c[i])) k[n++] = (uint8_t)i, m++;
    for (; m < x->ncr; m++) k[n++] = 255;
  }
  for (int o = 0; o < s->r.nob; o++) {
    k[n++] = s->ob[o].cell;
    k[n++] = s->ob[o].dir;
    k[n++] = s->ob[o].pos;
    k[n++] = s->ob[o].st & (OBF_AWAKE | OBF_YAWN);
  }
}

static void decode(const ctx *x, const uint8_t *k, sim *s) {
  int n = 0;
  s->r = x->base;
  s->p = k[n++];
  s->face = k[n++];
  s->tick = k[n++];
  s->won = k[n++];
  for (int d = 0; d < x->nd; d++) s->r.c[x->dcell[d]] = k[n++];
  for (int c = 0; c < x->ncr; c++) {
    uint8_t q = k[n++];
    s->cr[c] = q;
    if (q != 255) s->r.c[q] = x->base.c[q] == T_PLATE ? T_BLOCK_PLATE : T_BLOCK;
  }
  for (int o = 0; o < s->r.nob; o++) {
    eg_ob *b = &s->ob[o];
    b->cell = b->from = k[n++];
    b->dir = k[n++];
    b->pos = k[n++];
    b->st = k[n++];
    b->stop = b->imm = b->belly = 0;
    s->r.c[b->cell] |= F_OB;
  }
}

static int keys_left(const eg_room *r) {
  for (int i = 0; i < (int)EG_N; i++)
    if ((r->c[i] & T_MASK) == T_KEY) return 1;
  return 0;
}

/* one tick: action a (0-3 move, 4 wait, 5-8 turn). Returns 0 if illegal; *hits gets the tick's hits. */
static int step(sim *s, int a, int flags, int *hits) {
  uint8_t pp = s->p, noise[2], nn = 0;
  *hits = 0;
  if (a < 4) {
    uint8_t to = (uint8_t)(s->p + step4[a]), v = s->r.c[to], t = v & T_MASK;
    if (v & F_OB) return 0;
    if (t == T_DOOR) {
      if (keys_left(&s->r)) return 0;
      eg_leave(&s->r, s->p);
      s->p = to;
      s->won = 1;
      return 1;
    }
    if (EG_CRATE(v)) {
      uint8_t b = (uint8_t)(to + step4[a]), bt = s->r.c[b];
      if (!eg_push_block(&s->r, to, b)) return 0;
      if ((bt == T_GLASS || bt == T_CEIL || bt == T_CEIL_1) && nn < 2) noise[nn++] = b;
      for (int c = 0; c < 4; c++)
        if (s->cr[c] == to) s->cr[c] = s->r.c[b] == T_PIT ? 255 : b;
    } else if (EG_PASSABLE(t)) {
      if (t == T_ITEM) return 0; /* obstacle rooms hold no pieces (the bench is the B1 solver's) */
      if (t == T_KEY || t == T_TOKEN || t == T_DIRT) s->r.c[to] = T_EMPTY;
    } else
      return 0;
    if (eg_leave(&s->r, s->p) && nn < 2) noise[nn++] = s->p;
    s->p = to;
    s->face = move_face[a];
  } else if (a > 4)
    s->face = (uint8_t)((a - 5) * 2); /* n e s w */
  eg_gravity(&s->r, &s->p, 1);
  eg_otick t;
  memset(&t, 0, sizeof t);
  t.p = s->p;
  t.pp = pp;
  t.face = s->face;
  t.tick = s->tick;
  if (flags & EGO_NOISE) {
    t.nnoise = nn;
    t.noise[0] = noise[0];
    t.noise[1] = noise[1];
  }
  eg_ob_tick(&s->r, s->ob, &t);
  s->p = t.p;
  *hits = t.hits;
  eg_gravity(&s->r, &s->p, 1);
  s->tick++;
  return 1;
}

static void sim_start(const eg_room *r0, sim *s) {
  memset(s, 0, sizeof *s);
  s->r = *r0;
  s->p = r0->start;
  s->face = 4; /* the heir starts facing down */
  int m = 0;
  memset(s->cr, 255, sizeof s->cr);
  for (int i = 0; i < (int)EG_N && m < 4; i++)
    if (EG_CRATE(r0->c[i])) s->cr[m++] = (uint8_t)i;
  eg_ob_init(&s->r, s->ob);
  eg_plates_dirty = 1;
  eg_gravity(&s->r, &s->p, 1);
}

static int act_of(char ch) {
  for (int k = 0; k < 4; k++) {
    if (ch == move_ch[k]) return k;
    if (ch == turn_ch[k]) return 5 + k;
  }
  return ch == '.' ? 4 : -1;
}

int eg_oreplay(const eg_room *r, const char *plan, int flags, int *hits) {
  sim s;
  int h = 0, th;
  sim_start(r, &s);
  for (const char *m = plan; *m; m++) {
    int a = act_of(*m);
    if (a < 0 || s.won || !step(&s, a, flags, &th)) return 0;
    h += th;
  }
  if (hits) *hits = h;
  return s.won;
}

/* ---- the exploration: a key arena, an open-addressing index, the edges (in state order) ---- */
typedef struct {
  uint8_t *keys;
  int kl, n, cap;
  int32_t *tab;
  uint32_t mask;
} store;

static uint32_t hash(const uint8_t *k, int n) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) h = (h ^ k[i]) * 16777619u;
  return h ^ (h >> 15);
}

static int find_or_add(store *S, const uint8_t *k, int *added) {
  uint32_t h = hash(k, S->kl) & S->mask;
  *added = 0;
  for (;;) {
    int32_t i = S->tab[h];
    if (i < 0) break;
    if (!memcmp(S->keys + (size_t)i * S->kl, k, S->kl)) return i;
    h = (h + 1) & S->mask;
  }
  if (S->n >= S->cap) return -1;
  memcpy(S->keys + (size_t)S->n * S->kl, k, S->kl);
  S->tab[h] = S->n;
  *added = 1;
  return S->n++;
}

int eg_osolve(const eg_room *r0, int cap, int flags, char *plan, size_t plancap, eg_ostats *st) {
  static ctx x;
  sim s, t;
  uint8_t key[512];
  memset(st, 0, sizeof *st);
  st->fatal_depth = -1;
  if (plancap) plan[0] = 0;
  lift(&x, r0);
  x.flags = flags;
  int nact = x.sentry ? 9 : 5;
  store S;
  S.kl = x.kl;
  S.cap = cap;
  S.n = 0;
  uint32_t tsz = 1;
  while (tsz < (uint32_t)cap * 2u) tsz <<= 1;
  S.mask = tsz - 1;
  S.tab = malloc(sizeof(int32_t) * tsz);
  S.keys = malloc((size_t)cap * S.kl);
  size_t ecap = (size_t)cap * 3 + 64, ne = 0;
  int32_t *edst = malloc(sizeof(int32_t) * ecap);
  uint8_t *eact = malloc(ecap);
  int32_t *est = malloc(sizeof(int32_t) * ((size_t)cap + 1));
  uint16_t *depth = malloc(sizeof(uint16_t) * (size_t)cap);
  uint8_t *won = malloc((size_t)cap);
  int32_t *wsucc = malloc(sizeof(int32_t) * (size_t)cap);
  uint8_t *whit = malloc((size_t)cap);
  memset(S.tab, 0xff, sizeof(int32_t) * tsz);
  sim_start(r0, &s);
  encode(&x, &s, key);
  int added, ok = 0;
  find_or_add(&S, key, &added);
  depth[0] = 0;
  won[0] = 0;
  for (int i = 0; i < S.n; i++) {
    est[i] = (int32_t)ne;
    wsucc[i] = -1;
    whit[i] = 0;
    if (won[i]) continue;
    decode(&x, S.keys + (size_t)i * S.kl, &s);
    for (int a = 0; a < nact; a++) {
      int h;
      t = s;
      if (!step(&t, a, flags, &h)) continue;
      encode(&x, &t, key);
      int j = find_or_add(&S, key, &added);
      if (j < 0) {
        st->capped = 1;
        goto out;
      }
      if (added) {
        depth[j] = (uint16_t)(depth[i] + 1);
        won[j] = t.won;
      }
      if (ne == ecap) {
        ecap *= 2;
        edst = realloc(edst, sizeof(int32_t) * ecap);
        eact = realloc(eact, ecap);
      }
      edst[ne] = j;
      eact[ne] = (uint8_t)(a | (h ? 0x80 : 0));
      ne++;
      if (a == 4) wsucc[i] = j, whit[i] = (uint8_t)h;
    }
  }
  est[S.n] = (int32_t)ne;
  int n = S.n;
  st->states = n;
  /* the winnable set: back from the wins over the reversed edges */
  {
    int32_t *rc = calloc((size_t)n + 1, sizeof(int32_t)), *rs = malloc(sizeof(int32_t) * (ne ? ne : 1));
    int32_t *q = malloc(sizeof(int32_t) * (size_t)n);
    uint8_t *live = calloc((size_t)n, 1), *ps = calloc((size_t)n, 1), *col = calloc((size_t)n, 1);
    for (size_t e = 0; e < ne; e++) rc[edst[e] + 1]++;
    for (int i = 0; i < n; i++) rc[i + 1] += rc[i];
    int32_t *pos = malloc(sizeof(int32_t) * ((size_t)n + 1));
    memcpy(pos, rc, sizeof(int32_t) * ((size_t)n + 1));
    for (int i = 0; i < n; i++)
      for (int32_t e = est[i]; e < est[i + 1]; e++) rs[pos[edst[e]]++] = i;
    int qh = 0, qn = 0;
    for (int i = 0; i < n; i++)
      if (won[i]) live[i] = 1, q[qn++] = i;
    while (qh < qn) {
      int j = q[qh++];
      for (int32_t e = rc[j]; e < rc[j + 1]; e++)
        if (!live[rs[e]]) live[rs[e]] = 1, q[qn++] = rs[e];
    }
    for (int i = 0; i < n; i++) {
      if (live[i]) st->live++;
      else if (st->fatal_depth < 0 || depth[i] < st->fatal_depth)
        st->fatal_depth = depth[i];
    }
    st->dead = n - st->live;
    st->start_live = live[0];
    /* passive safety over the wait graph (a function: each state has one wait successor) */
    for (int i = 0; i < n; i++) {
      if (col[i] || won[i]) continue;
      int sp = 0, j = i;
      while (j >= 0 && !col[j] && !won[j]) {
        col[j] = 1;
        q[sp++] = j;
        j = wsucc[j];
      }
      int v;
      if (j < 0 || won[j]) v = 0;
      else if (col[j] == 2)
        v = ps[j];
      else { /* a cycle closed at j: safe iff every state on it is winnable */
        v = 1;
        int c = j;
        do {
          if (!live[c]) v = 0;
          c = wsucc[c];
        } while (c != j);
        c = j;
        do {
          ps[c] = (uint8_t)v;
          col[c] = 2;
          c = wsucc[c];
        } while (c != j);
      }
      while (sp--) {
        int c = q[sp];
        if (col[c] == 2) {
          v = ps[c];
          continue;
        }
        v = v && live[c];
        ps[c] = (uint8_t)v;
        col[c] = 2;
      }
    }
    for (int i = 0; i < n; i++) {
      if (ps[i]) st->passive++;
      if (live[i] && !won[i] && wsucc[i] >= 0 && !live[wsucc[i]]) st->rescue++;
    }
    st->idle_ok = ps[0];
    {
      int j = 0;
      for (int k = 0; k < EGO_IDLE_TICKS && j >= 0 && !won[j]; k++) {
        st->idle_hits += whit[j];
        j = wsucc[j];
      }
    }
    /* the plan: Dijkstra over passive-safe states (a win ends it) */
    if (ps[0]) {
      uint32_t *dist = malloc(sizeof(uint32_t) * (size_t)n);
      int32_t *par = malloc(sizeof(int32_t) * (size_t)n);
      uint8_t *pa = malloc((size_t)n);
      int64_t *heap = malloc(sizeof(int64_t) * (ne + 2));
      int hn = 0, goal = -1;
      memset(dist, 0xff, sizeof(uint32_t) * (size_t)n);
      dist[0] = 0;
      par[0] = -1;
      heap[hn++] = 0;
#define HPUSH(v)                                                                                                    \
  do {                                                                                                              \
    int64_t hv = (v);                                                                                               \
    int c = hn++;                                                                                                   \
    while (c && heap[(c - 1) / 2] > hv) heap[c] = heap[(c - 1) / 2], c = (c - 1) / 2;                               \
    heap[c] = hv;                                                                                                   \
  } while (0)
      while (hn) {
        int64_t top = heap[0], last = heap[--hn];
        int c = 0;
        for (;;) {
          int l = 2 * c + 1;
          if (l >= hn) break;
          if (l + 1 < hn && heap[l + 1] < heap[l]) l++;
          if (heap[l] >= last) break;
          heap[c] = heap[l];
          c = l;
        }
        if (hn) heap[c] = last;
        uint32_t d = (uint32_t)(top >> 32);
        int i = (int)(top & 0xffffffff);
        if (d != dist[i]) continue;
        if (won[i]) {
          goal = i;
          break;
        }
        for (int32_t e = est[i]; e < est[i + 1]; e++) {
          int j = edst[e], a = eact[e] & 0x7f;
          if (!won[j] && !ps[j]) continue;
          uint32_t nd = d + ((eact[e] & 0x80) ? 10000u : 0u) + 100u + (a != 4);
          if (nd < dist[j]) {
            dist[j] = nd;
            par[j] = i;
            pa[j] = (uint8_t)a;
            HPUSH(((int64_t)nd << 32) | (uint32_t)j);
          }
        }
      }
      if (goal >= 0) {
        int len = 0;
        for (int j = goal; par[j] >= 0; j = par[j]) len++;
        if ((size_t)len < plancap) {
          plan[len] = 0;
          int k = len;
          for (int j = goal; par[j] >= 0; j = par[j]) {
            int a = pa[j];
            plan[--k] = a < 4 ? move_ch[a] : (a == 4 ? '.' : turn_ch[a - 5]);
            if (a != 4) st->moves++;
          }
          st->ticks = len;
          st->hits = (int)(dist[goal] / 10000u);
          st->plan_ok = 1;
          ok = 1;
        }
      }
      free(dist);
      free(par);
      free(pa);
      free(heap);
    }
    free(rc);
    free(rs);
    free(q);
    free(live);
    free(ps);
    free(col);
    free(pos);
  }
out:
  free(S.tab);
  free(S.keys);
  free(edst);
  free(eact);
  free(est);
  free(depth);
  free(won);
  free(wsucc);
  free(whit);
  if (ok) {
    int h = 0;
    if (!eg_oreplay(r0, plan, flags, &h)) {
      fprintf(stderr, "eg_osolve: the plan does not replay\n");
      return 0;
    }
  }
  return ok;
}
