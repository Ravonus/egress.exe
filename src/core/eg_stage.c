/* B2 stage types: the rules, the generator and its score, the deck (see eg_stage.h, docs/redesign.md 4).
 * Shared verbatim by the cartridge (src/stage.c) and the host stage sim (host/egstage.c). */
#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_stage.h"
#include "eg_piece.h"
#include "eg_physics.h"

/* ---------------- the rules ---------------- */
/* the HUD objectives: <= 18 characters (section 12), and the hunt target's tells */
static const char goal_txt[EG_DECK_TYPES + KC_COUNT][19] = {
    "FIND THE DOOR",     /* puzzle */
    "HOLD THE RING",     /* holdout */
    "OUTRUN THE FALL",   /* chase */
    "HUNT THE MARKED",   /* hunt */
    "WALK THE TEMP OUT", /* escort */
    "MAKE IT, FAST",     /* mix under pressure (src/stage.c names the piece: MAKE SNOW, FAST) */
    "FIND THE KEYCARD",  /* dark */
    "BUY IT OR TAKE IT", /* heist */
    "ONLY A CRT KILLS",  /* KC_CRUSH */
    "RETURN ITS ORB",    /* KC_ORB */
    "ROLL, HIT ITS BACK" /* KC_BACK */
};
void eg_stage_text(uint8_t which, char *out) EG_BANKED {
  const char *s = goal_txt[which < EG_DECK_TYPES + KC_COUNT ? which : 0u];
  uint8_t n = 0u;
  while (s[n] && n != 18u) {
    out[n] = s[n];
    n++;
  }
  out[n] = 0;
}

static const uint8_t win_of[EG_DECK_TYPES] = {WIN_EXIT, WIN_CHARGE, WIN_OUTRUN, WIN_KILL, WIN_ESCORT, WIN_MAKE,
                                              WIN_KEYS, WIN_LEAVE};
uint8_t eg_stage_win(uint8_t type) EG_BANKED { return type < EG_DECK_TYPES ? win_of[type] : WIN_EXIT; }

/* ---------------- helpers ---------------- */
uint8_t eg_stage_reach[EG_N / 8u];
static uint8_t *C;
static eg_room *R;

static uint8_t absd(uint8_t a, uint8_t b) { return a > b ? (uint8_t)(a - b) : (uint8_t)(b - a); }
static uint8_t man(uint8_t a, uint8_t b) { return (uint8_t)(absd(EG_X(a), EG_X(b)) + absd(EG_Y(a), EG_Y(b))); }
static uint8_t cheb(uint8_t a, uint8_t b) {
  uint8_t dx = absd(EG_X(a), EG_X(b)), dy = absd(EG_Y(a), EG_Y(b));
  return dx > dy ? dx : dy;
}
static void prot(uint8_t i) { R->prot[i >> 3] |= (uint8_t)(1u << (i & 7u)); }
static uint8_t is_prot(uint8_t i) { return (uint8_t)(R->prot[i >> 3] & (uint8_t)(1u << (i & 7u))); }
static uint8_t inner(uint8_t x, uint8_t y) { return (uint8_t)(x >= 1u && x <= EG_W - 2u && y >= 1u && y <= EG_H - 2u); }
static void prot_around(uint8_t c, uint8_t r) { /* every interior cell within Chebyshev r: protected, carpet */
  uint8_t i = 0;
  do {
    if (inner(EG_X(i), EG_Y(i)) && cheb(i, c) <= r) {
      C[i] = T_EMPTY;
      prot(i);
    }
  } while (++i != (uint8_t)EG_N);
}
static uint8_t walk_ok(uint8_t t) { return (uint8_t)(EG_PASSABLE(t) || t == T_DOOR || t == T_BRICK); }

/* the flood: relaxation sweeps over a bit table (no queue), forward then backward until nothing changes */
uint8_t eg_stage_flood(const eg_room *r, uint8_t from) EG_BANKED {
  uint8_t i, k, ch, n = 1u, pass;
  for (k = 0u; k != (uint8_t)(EG_N / 8u); k++) eg_stage_reach[k] = 0u;
  eg_stage_reach[from >> 3] = (uint8_t)(1u << (from & 7u));
  for (pass = 0u; pass != 60u; pass++) {
    ch = 0u;
    for (k = 0u; k != (uint8_t)EG_N; k++) {
      i = (pass & 1u) ? (uint8_t)(EG_N - 1u - k) : k;
      if (EG_REACHED(i) || !walk_ok((uint8_t)(r->c[i] & T_MASK))) continue;
      if ((EG_X(i) && EG_REACHED(i - 1u)) || (EG_X(i) != EG_W - 1u && EG_REACHED(i + 1u)) ||
          (EG_Y(i) && EG_REACHED(i - EG_W)) || (EG_Y(i) != EG_H - 1u && EG_REACHED(i + EG_W))) {
        eg_stage_reach[i >> 3] |= (uint8_t)(1u << (i & 7u));
        n++;
        ch = 1u;
      }
    }
    if (!ch) break;
  }
  return n;
}

/* a level's worth of points spent across components: u units of `per` points each, at most maxu, sometimes one less
 * (so the same level builds different stages) */
static uint8_t take(uint8_t *rem, uint8_t maxu, uint8_t per) {
  uint8_t u = (uint8_t)(*rem / per);
  if (u > maxu) u = maxu;
  if (u && !(eg_rnd() & 3u)) u--;
  *rem = (uint8_t)(*rem - u * per);
  return u;
}

static void base(eg_room *r, eg_stage *s, uint16_t seed, uint8_t type, uint8_t level) {
  uint8_t i, x, y;
  R = r;
  C = r->c;
  eg_rs = (uint16_t)(seed ^ 0x3c5au ^ ((uint16_t)type << 12) ^ ((uint16_t)level << 7));
  if (!eg_rs) eg_rs = 0x2961u;
  eg_rnd();
  i = 0;
  do {
    x = EG_X(i);
    y = EG_Y(i);
    C[i] = (x == 0u || y == 0u || x == EG_W - 1u || y == EG_H - 1u) ? T_WALL : T_EMPTY;
  } while (++i != (uint8_t)EG_N);
  for (i = 0u; i != (uint8_t)(EG_N / 8u); i++) r->prot[i] = 0u;
  r->keys = r->tokens = r->nspawn = r->nitem = 0u;
  r->nob = r->nrun = 0u;
  for (i = 0u; i != EG_MAX_OB; i++)
    for (x = 0u; x != 4u; x++) r->ob[i][x] = 0u;
  for (i = 0u; i != EG_MAX_RUN; i++) r->run[i] = 0u;
  for (i = 0u; i != EG_MAX_SPAWN; i++) r->spawn[i] = r->spawn_kind[i] = 0u;
  for (i = 0u; i != EG_MAX_ITEM; i++) r->item_cell[i] = r->item_piece[i] = 0u;
  r->trick = r->support = r->herrings = r->depth = 0u;
  r->door = EG_IDX(2u + eg_rndn(12u), 0u);
  C[r->door] = T_DOOR;
  prot((uint8_t)(r->door + EG_W));
  prot((uint8_t)(r->door + 2u * EG_W));
  r->start = EG_IDX(1u + eg_rndn(14u), 11u + eg_rndn(2u));
  s->type = type;
  s->level = level;
  s->score = s->budget = s->conds = s->waves = s->per_wave = 0u;
  s->shooters = 255u;
  s->ring = s->ring_r = 0u;
  s->charge = 0u;
  s->speed = s->grace = s->piece = s->price = s->spot = s->npath = s->lamps = 0u;
  s->cond[0] = s->cond[1] = 255u;
  s->tcell = 0u;
}

/* concrete or drywall pillars (1x1, 2x1, 1x2) on unprotected carpet, off the start */
static uint8_t pillars(uint8_t n, uint8_t y0, uint8_t y1) {
  uint8_t k, tries, x, y, w, h, a, b, t, ok, got = 0u;
  for (k = 0u; k != n; k++)
    for (tries = 0u; tries != 24u; tries++) {
      x = (uint8_t)(2u + eg_rndn(12u));
      y = (uint8_t)(y0 + eg_rndn((uint8_t)(y1 - y0 + 1u)));
      t = eg_rndn(3u);
      w = (uint8_t)(t == 1u ? 2u : 1u);
      h = (uint8_t)(t == 2u ? 2u : 1u);
      ok = 1u;
      for (a = 0u; a != w; a++)
        for (b = 0u; b != h; b++) {
          uint8_t i = EG_IDX(x + a, y + b);
          if (!inner((uint8_t)(x + a), (uint8_t)(y + b)) || C[i] != T_EMPTY || is_prot(i) || cheb(i, R->start) < 2u)
            ok = 0u;
        }
      if (!ok) continue;
      t = (eg_rnd() & 3u) ? T_WALL : T_BRICK;
      for (a = 0u; a != w; a++)
        for (b = 0u; b != h; b++) C[EG_IDX(x + a, y + b)] = t;
      got++;
      break;
    }
  return got;
}

/* foam patches (2x2, the carpet's insulation): dug on entry, so they cost nothing but read as terrain */
static void foam(uint8_t n) {
  uint8_t k, x, y, a, b, i;
  for (k = 0u; k != n; k++) {
    x = (uint8_t)(1u + eg_rndn(13u));
    y = (uint8_t)(4u + eg_rndn(8u));
    for (a = 0u; a != 2u; a++)
      for (b = 0u; b != 2u; b++) {
        i = EG_IDX(x + a, y + b);
        if (inner((uint8_t)(x + a), (uint8_t)(y + b)) && C[i] == T_EMPTY && !is_prot(i) && i != R->start) C[i] = T_DIRT;
      }
  }
}

/* a free carpet cell in rows y0..y1, at least `far` (Manhattan) from the start and 2 from `avoid` cells */
static uint8_t free_cell(uint8_t y0, uint8_t y1, uint8_t far) {
  uint8_t tries, i;
  for (tries = 0u; tries != 64u; tries++) {
    i = EG_IDX(1u + eg_rndn(14u), y0 + eg_rndn((uint8_t)(y1 - y0 + 1u)));
    if (C[i] != T_EMPTY || is_prot(i) || man(i, R->start) < far) continue;
    return i;
  }
  return 0u;
}

static void spawn_add(uint8_t cell, uint8_t kind) {
  if (R->nspawn == EG_MAX_SPAWN) return;
  R->spawn[R->nspawn] = cell;
  R->spawn_kind[R->nspawn] = kind;
  R->nspawn++;
}

static uint8_t item_add(uint8_t cell, uint8_t piece) {
  if (!cell || R->nitem == EG_MAX_ITEM) return 0u;
  C[cell] = T_ITEM;
  prot(cell);
  R->item_cell[R->nitem] = cell;
  R->item_piece[R->nitem] = piece;
  R->nitem++;
  return 1u;
}

static uint8_t clamp15(int16_t v) { return v < 0 ? 0u : v > 15 ? 15u : (uint8_t)v; }

/* the waves a stage brings: W extra waves over `wmin`, P extra mobs a wave, S waves with a shooter (the last S) */
static void waves_of(eg_stage *s, uint8_t *rem, uint8_t wmin, uint8_t wmax) {
  uint8_t w, p, sh;
  if (eg_rnd() & 1u) {
    w = take(rem, (uint8_t)(wmax - wmin), 2u);
    p = take(rem, 2u, 2u);
  } else {
    p = take(rem, 2u, 2u);
    w = take(rem, (uint8_t)(wmax - wmin), 2u);
  }
  s->waves = (uint8_t)(wmin + w);
  s->per_wave = (uint8_t)(1u + p);
  sh = take(rem, s->waves, 1u);
  s->shooters = sh ? (uint8_t)(s->waves - sh) : 255u;
  s->budget = (uint8_t)(s->waves * s->per_wave * EG_COST_CHASER + sh * (EG_COST_SHOOTER - EG_COST_CHASER));
}
static uint8_t waves_pts(const eg_stage *s, uint8_t wmin) {
  return (uint8_t)(2u * (s->waves - wmin) + 2u * (s->per_wave - 1u) + (s->shooters == 255u ? 0u : s->waves - s->shooters));
}

/* ---------------- the stage types ---------------- */
static uint8_t build_holdout(eg_stage *s, uint8_t aim) {
  uint8_t rem = aim, c, n, dx = EG_X(R->door);
  if (dx < 2u) dx = 2u;
  if (dx > 13u) dx = 13u;
  s->ring = EG_IDX(dx, 3u);
  s->ring_r = 1u;
  prot_around(s->ring, 2u);
  prot_around(R->start, 1u);
  waves_of(s, &rem, 2u, 4u);
  c = take(&rem, 2u, 1u);
  s->charge = (uint16_t)(s->waves * (360u + 120u * c));
  n = pillars((uint8_t)(3u + eg_rndn(4u)), 6u, 11u);
  foam((uint8_t)(1u + eg_rndn(2u)));
  s->conds = 1u; /* the ring */
  s->score = clamp15((int16_t)(waves_pts(s, 2u) + c) + (n <= 3u ? 1 : (n >= 6u ? -1 : 0)));
  eg_stage_flood(R, R->start);
  return (uint8_t)(EG_REACHED(s->ring) != 0u);
}

/* chase: bars of concrete across the room with one gap each, alternating sides, so the way up snakes; the floor drops
 * a row at a time from the bottom. The slack is the tightest moment on the way: the front reaching the row under a
 * gap against the player walking there at EG_CHASE_WALK frames a cell. */
/* slack 48 frames scores 15, 408 or more scores 0 */
#define EG_CHASE_SCORE(sl) ((int16_t)((sl) > 408 ? 0 : (408 - (sl)) / 24))
static int16_t chase_slack(const eg_stage *s, const uint8_t *gx, const uint8_t *gy, uint8_t nb) {
  uint8_t k, at = R->start, nx;
  int16_t t = 0, slack = 32767, front, v;
  for (k = 0u; k <= nb; k++) {
    nx = k == nb ? (uint8_t)(R->door + EG_W) : EG_IDX(gx[k], gy[k]);
    t = (int16_t)(t + (int16_t)man(at, nx) * (int16_t)EG_CHASE_WALK);
    /* the row under the checkpoint (y + 1) drops at grace + (11 - y) rows x speed */
    front = (int16_t)((int16_t)s->grace * 4 + (int16_t)(EG_H - 3u - EG_Y(nx)) * (int16_t)s->speed);
    v = (int16_t)(front - (t - (int16_t)EG_CHASE_WALK));
    if (v < slack) slack = v;
    at = nx;
  }
  return slack;
}

static uint8_t build_chase(eg_stage *s, uint8_t aim) {
  uint8_t nb = (uint8_t)(2u + eg_rndn(2u)), k, x, y, side = eg_rnd() & 1u, gx[3], gy[3], g, mobs, best_sp = 248u;
  int16_t slack, bd = 32767, sc;
  uint8_t rem = aim;
  for (k = 0u; k != nb; k++) {
    y = nb == 2u ? (uint8_t)(4u + k * 4u) : (uint8_t)(3u + k * 3u);
    y = (uint8_t)(EG_H - 2u - (y - 1u)); /* from the bottom up: the first gap is the lowest */
    gy[k] = y;
    gx[k] = side ? (uint8_t)(12u + eg_rndn(3u)) : (uint8_t)(1u + eg_rndn(3u));
    for (x = 1u; x != EG_W - 1u; x++) C[EG_IDX(x, y)] = T_WALL;
    C[EG_IDX(gx[k], y)] = T_EMPTY;
    prot(EG_IDX(gx[k], y));
    prot(EG_IDX(gx[k], y - 1u));
    prot(EG_IDX(gx[k], y + 1u));
    if (eg_rnd() & 1u) C[EG_IDX(6u + eg_rndn(4u), y)] = T_BRICK; /* a shortcut: two strikes of the lash */
    side ^= 1u;
  }
  /* the start under the first gap's far side, the door over the last gap's far side */
  C[R->door] = T_WALL;
  R->door = EG_IDX(side ? (uint8_t)(10u + eg_rndn(4u)) : (uint8_t)(2u + eg_rndn(4u)), 0u);
  C[R->door] = T_DOOR;
  prot((uint8_t)(R->door + EG_W));
  R->start = EG_IDX(gx[0] < 8u ? (uint8_t)(10u + eg_rndn(4u)) : (uint8_t)(1u + eg_rndn(4u)), EG_H - 2u);
  C[R->start] = T_EMPTY;
  prot(R->start);
  foam((uint8_t)(1u + eg_rndn(3u)));
  /* chasers in the lanes ahead: a point each */
  mobs = take(&rem, 2u, 2u);
  for (k = 0u; k != mobs; k++) {
    g = free_cell(2u, (uint8_t)(gy[0] - 1u), 6u);
    if (g) spawn_add(g, SPAWN_MITE);
  }
  /* the drop's speed: the one whose slack scores nearest what is left (never under 48 frames of slack) */
  s->grace = (uint8_t)(30u + 8u * eg_rndn(3u));
  for (g = 248u; g >= 40u; g = (uint8_t)(g - 8u)) {
    s->speed = g;
    slack = chase_slack(s, gx, gy, nb);
    if (slack < 48) break;
    sc = EG_CHASE_SCORE(slack);
    if (sc < 0) sc = 0;
    sc = sc > (int16_t)rem ? (int16_t)(sc - (int16_t)rem) : (int16_t)((int16_t)rem - sc);
    if (sc < bd) {
      bd = sc;
      best_sp = g;
    }
  }
  s->speed = best_sp;
  slack = chase_slack(s, gx, gy, nb);
  s->budget = (uint8_t)(R->nspawn * EG_COST_CHASER);
  s->conds = 1u;
  s->score = clamp15(EG_CHASE_SCORE(slack) + R->nspawn);
  eg_stage_flood(R, R->start);
  return (uint8_t)(slack >= 48 && EG_REACHED(R->door));
}

/* hunt: the target and its conditions; the means are in the room (CRT traps for a crush; the lash and the roll are
 * the kit) */
static uint8_t trap(void) { /* a CRT resting on foam over three cells of open lane; dig the foam, step aside */
  uint8_t tries, x, y, i;
  for (tries = 0u; tries != 40u; tries++) {
    x = (uint8_t)(2u + eg_rndn(12u));
    y = (uint8_t)(3u + eg_rndn(4u));
    for (i = 0u; i != 5u; i++)
      if (is_prot(EG_IDX(x, y - 1u + i)) || C[EG_IDX(x, y - 1u + i)] != T_EMPTY) break;
    if (i != 5u) continue;
    C[EG_IDX(x, y - 1u)] = T_ROCK;
    C[EG_IDX(x, y)] = T_DIRT;
    for (i = 0u; i != 5u; i++) prot(EG_IDX(x, y - 1u + i));
    prot(EG_IDX(x - 1u, y));
    prot(EG_IDX(x + 1u, y));
    C[EG_IDX(x - 1u, y)] = C[EG_IDX(x + 1u, y)] = T_EMPTY;
    return EG_IDX(x, y);
  }
  return 0u;
}

static uint8_t build_hunt(eg_stage *s, uint8_t aim, uint8_t elite) {
  static const uint8_t kpts[KC_COUNT] = {2u, 1u, 1u};
  uint8_t rem = aim, k, e, t[3], nt = 0u, ok = 1u;
  prot_around(R->start, 1u);
  s->cond[0] = eg_rndn(KC_COUNT);
  if (rem >= 7u + kpts[s->cond[0]] || elite) {
    s->cond[1] = (uint8_t)((s->cond[0] + 1u + eg_rndn(2u)) % KC_COUNT);
    rem = (uint8_t)(rem > 4u ? rem - 4u : 0u);
  }
  s->conds = (uint8_t)(s->cond[1] == 255u ? 1u : 2u);
  rem = (uint8_t)(rem > kpts[s->cond[0]] ? rem - kpts[s->cond[0]] : 0u);
  if (s->cond[1] != 255u) rem = (uint8_t)(rem > kpts[s->cond[1]] ? rem - kpts[s->cond[1]] : 0u);
  if (s->cond[0] == KC_CRUSH || s->cond[1] == KC_CRUSH)
    for (k = 0u; k != 3u; k++)
      if ((t[nt] = trap()) != 0u) nt++;
  pillars((uint8_t)(2u + eg_rndn(3u)), 5u, 10u);
  foam(1u);
  e = take(&rem, 3u, 2u);
  /* the target: up the room, far from the start; a shooter if its condition is the orb */
  s->tcell = free_cell(2u, 6u, 7u);
  if (!s->tcell) return 0u;
  prot(s->tcell);
  for (k = 0u; k != e; k++) {
    uint8_t c = free_cell(2u, 9u, 6u);
    if (c) spawn_add(c, (k == 2u || (rem && k == 1u)) ? SPAWN_EYE : SPAWN_MITE);
  }
  s->budget = (uint8_t)(4u + 2u * s->conds);
  for (k = 0u; k != R->nspawn; k++)
    s->budget = (uint8_t)(s->budget + (R->spawn_kind[k] == SPAWN_EYE ? EG_COST_SHOOTER : EG_COST_CHASER));
  s->score = clamp15((int16_t)(kpts[s->cond[0]] + (s->conds == 2u ? 4u + kpts[s->cond[1]] : 0u)) +
                     (int16_t)(s->budget - 4u - 2u * s->conds) - (int16_t)(nt >= 3u));
  eg_stage_flood(R, R->start);
  if (!EG_REACHED(s->tcell) || !EG_REACHED(R->door)) ok = 0u;
  if ((s->cond[0] == KC_CRUSH || s->cond[1] == KC_CRUSH) && !nt) ok = 0u;
  for (k = 0u; k != nt; k++) /* the trap's foam is reached from a side */
    if (!EG_REACHED(t[k] - 1u) && !EG_REACHED(t[k] + 1u)) ok = 0u;
  return ok;
}

/* escort: the temp walks a generated path of straight runs (one axis at a time) to the cell under the door */
static uint8_t build_escort(eg_stage *s, uint8_t aim) {
  uint8_t rem = aim, x, y, k, ty, a, b, i, sp, hp, len = 0u;
  s->tcell = (uint8_t)(EG_X(R->start) < 8u ? R->start + 1u : R->start - 1u);
  C[s->tcell] = T_EMPTY;
  prot_around(R->start, 1u);
  x = EG_X(s->tcell);
  y = EG_Y(s->tcell);
  s->npath = 0u;
  s->path[s->npath++] = s->tcell;
  ty = y;
  while (s->npath + 4u <= EG_STAGE_PATH && ty > 3u) {
    /* across the room, then up 2-4 rows */
    a = x < 8u ? (uint8_t)(9u + eg_rndn(5u)) : (uint8_t)(1u + eg_rndn(5u));
    s->path[s->npath++] = EG_IDX(a, ty);
    x = a;
    ty = (uint8_t)(ty - 2u - eg_rndn(3u));
    if (ty < 2u) ty = 2u;
    s->path[s->npath++] = EG_IDX(x, ty);
  }
  /* over to the door's column, and up under it */
  if (x != EG_X(R->door)) s->path[s->npath++] = EG_IDX(EG_X(R->door), ty);
  if (ty != 1u) s->path[s->npath++] = (uint8_t)(R->door + EG_W);
  /* carve the runs: carpet, protected */
  for (k = 0u; k + 1u < s->npath; k++) {
    a = s->path[k];
    b = s->path[k + 1u];
    i = a;
    for (;;) {
      C[i] = T_EMPTY;
      prot(i);
      if (i == b) break;
      if (EG_X(i) != EG_X(b)) i = EG_X(i) < EG_X(b) ? (uint8_t)(i + 1u) : (uint8_t)(i - 1u);
      else i = EG_Y(i) < EG_Y(b) ? (uint8_t)(i + EG_W) : (uint8_t)(i - EG_W);
      len++;
    }
  }
  waves_of(s, &rem, 1u, 4u);
  sp = take(&rem, 2u, 1u);
  s->speed = (uint8_t)(20u + 8u * sp); /* a slower temp is longer in the open */
  hp = take(&rem, 2u, 1u);
  s->grace = (uint8_t)(6u - 2u * hp); /* the temp's hit points (half hearts) */
  pillars((uint8_t)(3u + eg_rndn(4u)), 3u, 11u);
  foam(1u);
  s->conds = 1u;
  s->score = clamp15((int16_t)(waves_pts(s, 1u) + sp + hp) + (len >= 34u ? 1 : 0) - (len < 20u ? 1 : 0));
  eg_stage_flood(R, R->start);
  for (k = 0u; k != s->npath; k++)
    if (!EG_REACHED(s->path[k])) return 0u;
  return (uint8_t)(EG_REACHED(R->door) != 0u);
}

/* mix under pressure: the recipe to make, its found pieces in the room, the bench in a ring at the middle */
static uint8_t expand(uint8_t p, uint8_t *out) {
  /* the found pieces each recipe comes from, flattened (no recursion on the cartridge) */
  static const uint8_t fl[8][5] = {
      {P_RAIN, P_AIR, P_WATER, 0u, 0u},       {P_MUD, P_EARTH, P_WATER, 0u, 0u},
      {P_ENERGY, P_AIR, P_FIRE, 0u, 0u},      {P_STEAM, P_WATER, P_FIRE, 0u, 0u},
      {P_SNOW, P_AIR, P_AIR, P_WATER, 0u},    {P_BRICK, P_FIRE, P_EARTH, P_WATER, 0u},
      {P_ELECTRIC, P_METAL, P_AIR, P_FIRE, 0u}, {P_ICE, P_WATER, P_AIR, P_AIR, P_WATER}};
  uint8_t k, n = 0u;
  for (k = 0u; k != 8u; k++)
    if (fl[k][0] == p) break;
  if (k == 8u) k = 7u;
  while (n != 4u && fl[k][n + 1u]) {
    out[n] = fl[k][n + 1u];
    n++;
  }
  return n;
}

static uint8_t build_mixp(eg_stage *s, uint8_t aim, uint8_t m) {
  static const uint8_t d1[4] = {P_RAIN, P_MUD, P_ENERGY, P_STEAM}, d2[3] = {P_SNOW, P_BRICK, P_ELECTRIC};
  uint8_t rem = aim, d, k, n, buf[EG_MAX_ITEM], h, iv, p, sh, r1, c, ok = 1u;
  /* the slot's trick picks the recipe when it has one (its object), else the depth the level affords */
  if (rem >= 2u && (m == MK_COLD || m == MK_SHORT || m == MK_WEIGHT)) {
    /* the slot's trick: its object (COLD's snow or ice, SHORT's electric, WEIGHT's brick) */
    d = (uint8_t)(m == MK_COLD && rem >= 5u ? 3u : 2u);
    s->piece = m == MK_COLD ? (d == 3u ? P_ICE : P_SNOW) : (m == MK_SHORT ? P_ELECTRIC : P_BRICK);
    rem = (uint8_t)(rem - 2u * (d - 1u));
  } else {
    d = (uint8_t)(1u + take(&rem, 2u, 2u));
    s->piece = d == 1u ? d1[eg_rndn(4u)] : (d == 2u ? d2[eg_rndn(3u)] : P_ICE);
  }
  s->spot = EG_IDX(5u + eg_rndn(6u), 5u + eg_rndn(3u));
  r1 = (uint8_t)(rem >= 1u && (eg_rnd() & 1u));
  rem = (uint8_t)(rem - r1);
  s->ring = s->spot;
  s->ring_r = (uint8_t)(2u - r1);
  prot_around(s->spot, s->ring_r);
  prot_around(R->start, 1u);
  C[s->spot] = T_BLOCK; /* the bench: solid, the ring around it is where it works */
  pillars((uint8_t)(3u + eg_rndn(3u)), 2u, 11u);
  foam((uint8_t)(1u + eg_rndn(2u)));
  /* the pieces, then herrings: anything found, plausible, wrong */
  n = expand(s->piece, buf);
  h = take(&rem, (uint8_t)(EG_MAX_ITEM - n > 2u ? 2u : EG_MAX_ITEM - n), 1u);
  for (k = 0u; k != h; k++) buf[n + k] = (uint8_t)(P_FIRE + eg_rndn(5u));
  for (k = 0u; k != n + h; k++) {
    for (c = 0u, p = 0u; p != 8u && !c; p++) {
      c = free_cell(2u, 12u, 3u);
      if (c && cheb(c, s->spot) < 3u) c = 0u;
    }
    if (!item_add(c, buf[k])) ok = 0u;
  }
  R->herrings = h;
  /* the mobs: a wave every `speed` x 4 frames while the recipe isn't made */
  iv = take(&rem, 3u, 1u);
  s->speed = (uint8_t)(150u - 30u * iv);
  p = take(&rem, 2u, 2u);
  s->per_wave = (uint8_t)(1u + p);
  sh = take(&rem, 1u, 1u);
  s->shooters = sh ? 1u : 255u;
  s->waves = 9u; /* until it is made */
  s->budget = (uint8_t)(4u * (s->per_wave * EG_COST_CHASER) + sh * 4u); /* about four waves */
  s->conds = d;
  s->score = clamp15((int16_t)(2u * (d - 1u) + h + iv + 2u * p + sh + r1));
  eg_stage_flood(R, R->start);
  for (k = 0u; k != R->nitem; k++)
    if (!EG_REACHED(R->item_cell[k])) ok = 0u;
  if (!EG_REACHED((uint8_t)(s->spot + EG_W)) && !EG_REACHED((uint8_t)(s->spot - 1u))) ok = 0u;
  return (uint8_t)(ok && EG_REACHED(R->door));
}

/* dark: lamps light a cell around them for good; the heir's own light reaches 1 cell, 2 with a GLOWS piece in the
 * pocket (the tealight); the keycards are somewhere in the dark */
static uint8_t build_dark(eg_stage *s, uint8_t aim) {
  uint8_t rem = aim, k, nl, lp, kk, mobs, eyes, c, ok = 1u;
  prot_around(R->start, 1u);
  pillars((uint8_t)(5u + eg_rndn(4u)), 2u, 11u);
  kk = take(&rem, 1u, 2u);
  R->keys = (uint8_t)(1u + kk);
  for (k = 0u; k != R->keys; k++) {
    c = free_cell(1u, 7u, 7u);
    if (!c) return 0u;
    C[c] = T_KEY;
    prot(c);
  }
  nl = take(&rem, 3u, 1u); /* fewer lamps, darker */
  s->lamps = (uint8_t)(4u - nl);
  for (k = 0u; k != s->lamps; k++) {
    c = free_cell(2u, 11u, 3u);
    if (!c) continue;
    C[c] = T_BLOCK;
    prot(c);
    s->path[s->npath++] = c;
  }
  lp = take(&rem, 2u, 1u); /* the tealight: near the start, far, or none */
  s->piece = lp == 2u ? 0u : P_FIRE;
  if (s->piece) {
    for (k = 0u, c = 0u; k != 16u && !c; k++) {
      c = free_cell(2u, 12u, lp ? 8u : 2u);
      if (c && !lp && man(c, R->start) > 4u) c = 0u;
    }
    if (!item_add(c, P_FIRE)) ok = 0u;
  }
  mobs = take(&rem, 3u, 2u);
  eyes = (uint8_t)(rem && mobs ? 1u : 0u);
  for (k = 0u; k != mobs; k++) {
    c = free_cell(2u, 10u, 6u);
    if (c) spawn_add(c, k < eyes ? SPAWN_EYE : SPAWN_MITE);
  }
  s->budget = 0u;
  for (k = 0u; k != R->nspawn; k++)
    s->budget = (uint8_t)(s->budget + (R->spawn_kind[k] == SPAWN_EYE ? EG_COST_SHOOTER : EG_COST_CHASER));
  s->conds = R->keys;
  s->score = clamp15((int16_t)(nl + lp + 2u * kk + s->budget));
  eg_stage_flood(R, R->start);
  for (k = 0u; k != (uint8_t)EG_N; k++)
    if (C[k] == T_KEY && !EG_REACHED(k)) ok = 0u;
  for (k = 0u; k != R->nitem; k++)
    if (!EG_REACHED(R->item_cell[k])) ok = 0u;
  return (uint8_t)(ok && EG_REACHED(R->door));
}

/* heist: the shopkeeper behind a counter, the goods on it; taking them unpaid shuts the door and brings waves */
static uint8_t build_heist(eg_stage *s, uint8_t aim) {
  static const uint8_t goods[6] = {P_ELECTRIC, P_ENERGY, P_SNOW, P_ICE, P_CLOUD, P_METAL};
  uint8_t rem = aim, kx = (uint8_t)(3u + eg_rndn(10u)), k, pr;
  /* the door on the other half from the shop */
  C[R->door] = T_WALL;
  R->door = EG_IDX(kx < 8u ? (uint8_t)(10u + eg_rndn(4u)) : (uint8_t)(2u + eg_rndn(4u)), 0u);
  C[R->door] = T_DOOR;
  prot((uint8_t)(R->door + EG_W));
  prot((uint8_t)(R->door + 2u * EG_W));
  s->tcell = EG_IDX(kx, 2u);
  s->spot = EG_IDX(kx, 3u);
  for (k = 0u; k != 5u; k++) {
    C[EG_IDX(kx - 2u + k, 3u)] = T_BLOCK; /* the counter */
    prot(EG_IDX(kx - 2u + k, 3u));
    prot(EG_IDX(kx - 2u + k, 4u));
  }
  C[EG_IDX(kx - 1u, 2u)] = C[EG_IDX(kx + 1u, 2u)] = T_BLOCK;
  C[s->tcell] = T_BLOCK; /* the shopkeeper stands there: solid */
  prot(s->tcell);
  s->piece = goods[eg_rndn(6u)];
  item_add(s->spot, s->piece);
  prot_around(R->start, 1u);
  pillars((uint8_t)(3u + eg_rndn(3u)), 6u, 11u);
  foam(1u);
  pr = take(&rem, 3u, 1u);
  s->price = (uint8_t)(4u + 2u * pr);
  waves_of(s, &rem, 2u, 4u); /* what a theft brings */
  s->conds = 1u;
  s->score = clamp15((int16_t)(waves_pts(s, 2u) + pr));
  eg_stage_flood(R, R->start);
  return (uint8_t)(EG_REACHED(R->door) && EG_REACHED((uint8_t)(s->spot + EG_W)));
}

static uint8_t build(eg_room *r, eg_stage *s, uint8_t type, uint16_t seed, uint8_t level, uint8_t m) {
  int8_t j;
  uint8_t aim, ok;
  base(r, s, seed, type, level);
  j = (int8_t)((int8_t)eg_rndn(3u) - 1);
  aim = clamp15((int16_t)level + j);
  switch (type) {
    case SG_HOLDOUT: ok = build_holdout(s, aim); break;
    case SG_CHASE: ok = build_chase(s, aim); break;
    case SG_HUNT: ok = build_hunt(s, aim, m == 254u); break;
    case SG_ESCORT: ok = build_escort(s, aim); break;
    case SG_MIXP: ok = build_mixp(s, aim, m); break;
    case SG_DARK: ok = build_dark(s, aim); break;
    case SG_HEIST: ok = build_heist(s, aim); break;
    default: ok = 0u;
  }
  if (C[r->start] != T_EMPTY) ok = 0u;
  return ok;
}

uint16_t eg_stage_gen(eg_room *r, eg_stage *s, uint8_t type, uint16_t seed, uint8_t level, uint8_t m) EG_BANKED {
  uint8_t t, d, bd = 255u;
  uint16_t sd, best = 0u;
  for (t = 0u; t != EG_STAGE_TRIES * 2u; t++) {
    sd = (uint16_t)(seed + (uint16_t)t * 0x9e37u);
    if (!sd) sd = 1u;
    if (!build(r, s, type, sd, level, m)) continue;
    d = absd(s->score, level);
    if (d < bd) {
      bd = d;
      best = sd;
    }
    if (d <= 1u || (best && t + 1u >= EG_STAGE_TRIES)) break; /* in the band, or the tries are spent */
  }
  if (best && best != sd) build(r, s, type, best, level, m);
  return best;
}

/* ---------------- the deck ---------------- */
void eg_deck_reset(eg_deck *d) EG_BANKED {
  uint8_t k;
  for (k = 0u; k != EG_DECK_HIST; k++) d->hist[k] = 255u;
  for (k = 0u; k != EG_DECK_TRICKS; k++) d->tricks[k] = 255u;
}

uint8_t eg_deck_deal(const eg_deck *d, uint8_t bpos, uint8_t cstage, uint8_t m0, uint8_t dark_open, uint8_t roll)
    EG_BANKED {
  uint8_t w[EG_DECK_TYPES], t, k, x;
  uint16_t tot = 0u;
  for (t = 0u; t != EG_DECK_TYPES; t++) w[t] = 0u;
  if (cstage == ST_INTRO) return SG_PUZZLE;
  /* weights x4, so the history can halve them twice and stay whole */
  if (cstage == ST_DEVELOP) {
    if (m0 != 255u && MK_OBSTACLE(m0)) {
      w[SG_PUZZLE] = 20u, w[SG_HOLDOUT] = 8u;
      if (bpos >= 1u) w[SG_CHASE] = 8u;
      if (bpos >= 2u) w[SG_ESCORT] = 4u;
    } else if (m0 == MK_DARK) {
      w[SG_PUZZLE] = 12u, w[SG_DARK] = 12u;
    } else {
      w[SG_PUZZLE] = 24u;
      if (bpos >= 2u) w[SG_MIXP] = 8u;
    }
  } else {
    w[SG_PUZZLE] = 20u, w[SG_HOLDOUT] = 8u;
    if (bpos >= 1u) w[SG_CHASE] = 8u, w[SG_HUNT] = 8u;
    if (bpos >= 2u) w[SG_ESCORT] = 4u, w[SG_MIXP] = 8u;
    if (dark_open) w[SG_DARK] = 8u;
    if (bpos >= 3u && cstage != ST_TEST) w[SG_HEIST] = 4u;
  }
  /* the history: never the last type again (but a puzzle), half weight per sighting in the last 6 */
  if (d->hist[0] != 255u && d->hist[0] != SG_PUZZLE && d->hist[0] < EG_DECK_TYPES) w[d->hist[0]] = 0u;
  for (k = 1u; k != EG_DECK_HIST; k++) {
    t = d->hist[k];
    if (t < EG_DECK_TYPES && t != SG_PUZZLE) w[t] = (uint8_t)(w[t] >> 1);
  }
  for (t = 0u; t != EG_DECK_TYPES; t++) tot = (uint16_t)(tot + w[t]);
  if (!tot) return SG_PUZZLE;
  x = (uint8_t)(((uint16_t)roll * tot) >> 8);
  for (t = 0u; t != EG_DECK_TYPES; t++) {
    if (x < w[t]) return t;
    x = (uint8_t)(x - w[t]);
  }
  return SG_PUZZLE;
}

void eg_deck_note(eg_deck *d, uint8_t type, uint8_t trick) EG_BANKED {
  uint8_t k;
  for (k = EG_DECK_HIST - 1u; k; k--) d->hist[k] = d->hist[k - 1u];
  d->hist[0] = type;
  if (trick == 255u) return;
  for (k = EG_DECK_TRICKS - 1u; k; k--) d->tricks[k] = d->tricks[k - 1u];
  d->tricks[0] = trick;
}

uint8_t eg_deck_trick_seen(const eg_deck *d, uint8_t trick) EG_BANKED {
  uint8_t k;
  for (k = 0u; k != EG_DECK_TRICKS; k++)
    if (d->tricks[k] == trick) return (uint8_t)(k + 1u);
  return 0u;
}

uint8_t eg_stage_roll(uint16_t run_seed, uint8_t floor, uint8_t salt) EG_BANKED {
  uint16_t x = (uint16_t)(run_seed ^ (uint16_t)((uint16_t)floor * 0x9e37u) ^ ((uint16_t)salt << 8));
  x ^= x << 7;
  x ^= x >> 9;
  x ^= x << 8;
  return (uint8_t)(x ^ (x >> 8));
}

/* ---------------- the cartridge's run ---------------- */
void eg_slice_begin(eg_slice *sl) EG_BANKED {
  eg_deck_reset(&sl->deck);
  sl->kind = SK_STAGE;
  sl->type = SG_PUZZLE;
  sl->cstage = ST_INTRO;
  sl->level = 0u;
  sl->m = 255u;
  sl->planned = 0u;
  sl->physical = PH_NONE;
}

uint8_t eg_slice_floor(eg_slice *sl, eg_plan_state *ps, uint8_t floor, uint8_t last, eg_seed_rec *rec, uint8_t force)
    EG_BANKED {
  sl->planned = 0u;
  sl->physical = PH_NONE;
  sl->m = 255u;
  if (floor >= last) {
    sl->kind = SK_BOSS;
    sl->type = SG_HUNT;
    sl->level = 15u;
    return SK_BOSS;
  }
  if (floor == EG_SLICE_MIDBOSS && force == 255u) {
    sl->kind = SK_MIDBOSS;
    sl->type = SG_HUNT;
    sl->cstage = ST_TEST;
    sl->level = EG_MIDBOSS_LEVEL(eg_plan_target(ps, floor));
    sl->m = 254u;
    return SK_MIDBOSS;
  }
  sl->kind = SK_STAGE;
  /* The native slice alternates physical reasoning and fights. A combat floor must not silently
   * mark a planner lesson as seen when its puzzle was never played. Forced harness rooms stay exact. */
  if (force == 255u) {
    sl->level = eg_plan_target(ps, floor);
    sl->cstage = ST_DEVELOP;
    if (floor == 1u || floor == 3u || floor == 7u) {
      sl->physical = floor == 7u ? PH_BRIDGE :
        (uint8_t)(floor == 1u ? PH_PUSH : PH_DROP);
      sl->type = SG_PUZZLE;
      eg_deck_note(&sl->deck, sl->type, 255u);
      return SK_STAGE;
    }
    if (floor == 2u || floor == 6u || floor == 8u) {
      uint8_t roll = eg_stage_roll(ps->run_seed, floor, 2u);
      sl->type = floor == 2u ? SG_HOLDOUT : floor == 6u ?
        (roll & 1u ? SG_CHASE : SG_ESCORT) : (roll & 1u ? SG_HUNT : SG_HOLDOUT);
      if (sl->type == sl->deck.hist[0]) sl->type = floor == 6u ?
        (sl->type == SG_CHASE ? SG_ESCORT : SG_CHASE) : (sl->type == SG_HUNT ? SG_HOLDOUT : SG_HUNT);
      eg_deck_note(&sl->deck, sl->type, 255u);
      return SK_STAGE;
    }
  }
  sl->cstage = force == 255u && floor == 9u ? eg_plan_intro(ps,floor,rec) : eg_plan_floor(ps, floor, rec);
  sl->planned = 1u;
  sl->level = ps->target;
  /* The deck uses the full-run taxonomy; the slice planner has compact local bits. */
  if (TRICK_OBSTACLE(rec->trick)) {
    static const uint8_t obs_mech[4] = {MK_PATROL, MK_HEAVY, MK_MIRROR, MK_SENTRY};
    sl->m = obs_mech[rec->trick - TRICK_RUSH];
  } else sl->m = MK_TRICK(rec->trick);
  sl->type = force != 255u ? force
                           : eg_deck_deal(&sl->deck, EG_SLICE_POS(floor), sl->cstage, sl->m,
                                          (uint8_t)(EG_SLICE_POS(floor) >= 3u), eg_stage_roll(ps->run_seed, floor, 1u));
  /* a puzzle keeps its trick in the history; a mix under pressure makes the trick's object, so it counts too */
  eg_deck_note(&sl->deck, sl->type,
               (sl->type == SG_PUZZLE || sl->type == SG_MIXP) ? rec->trick : 255u);
  return SK_STAGE;
}
