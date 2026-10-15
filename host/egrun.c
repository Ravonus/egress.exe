/* egrun: the full run (src/core/eg_plan.c eg_run_build, docs/redesign.md 3.6) on the host.
 *   full_check   lays out RUNS runs over lines of heirs and checks every curriculum rule at the ~52-floor length
 *   full_runsim  writes every slot of RUNS front-door runs, with a modelled player's nudges (tools/plot_curve.py --full)
 *   survive      the survival model: lines of heirs played by a skilled player, with shortcuts and lineage strength
 *                (tools/plot_survival.py); the difficulty knobs are the constants below
 * The layout needs no baked rooms: each slot names its stage type, curriculum stage, mechanics and level, and the B4
 * banks are sized to answer those slots (3.6.9). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/eg_plan.h"

#define RUNS_PER_LINE 25
static uint32_t rs = 0x9e3779b9u;
static uint32_t rnd(void) {
  rs ^= rs << 13;
  rs ^= rs >> 17;
  rs ^= rs << 5;
  return rs;
}
static double frand(void) { return (rnd() & 0xffffffu) / (double)0x1000000; }
static double gauss(void) {
  double u = frand() + 1e-9, v = frand();
  return sqrt(-2.0 * log(u)) * cos(6.283185307 * v);
}
static int popc(uint32_t m) {
  int n = 0;
  for (; m; m >>= 1) n += (int)(m & 1u);
  return n;
}
static uint32_t set_of(const eg_slot *s) {
  uint32_t v = 0;
  for (int i = 0; i < 3; i++)
    if (s->m[i] != 255u) v |= 1u << s->m[i];
  return v;
}

static const char *mk_name[MK_ALL] = {"crt",    "drywall", "burn",   "cold",    "short",  "weight",   "glass", "rhythm",
                                      "steps",  "kettle",  "beam",   "dark",    "patrol", "heavy",    "sleeper",
                                      "pusher", "eater",   "sentry", "mirror",  "follower", "lighteater", "drifter"};
static const char *bi_name[BI_COUNT] = {"office", "vents", "atrium", "server", "mall", "garden", "rooftop", "core"};
static const char *sg_name[SG_COUNT] = {"puzzle", "holdout", "chase", "hunt", "escort", "mixp",
                                        "dark",   "heist",   "3d",    "rift", "sky"};

/* ---------------------------------------------------------------- the rules ---------------------------------------- */
typedef struct {
  long layouts, slots, stage_slots, order_bad, band_bad, intro_n, intro_bad, develop_bad, repeat_bad, type_repeat,
      rare_repeat, struct_bad, nudge_bad, floors_min, floors_max, floors_sum, front_layouts, descent_bad;
  long intros_by_pos[BI_COUNT], stage_by_pos[BI_COUNT], mixed_by_pos[BI_COUNT], rare[SG_COUNT], types[SG_COUNT];
  long intro_delay[4]; /* progressive lines: introduced at band, band+1, band+2, later */
  long fresh_layouts, fresh_pos_hit[BI_COUNT], fresh_missing;
} checks_t;

/* check one layout against its inputs; returns the number of rule breaks */
static long check_layout(const eg_run *r, const eg_run_in *in, checks_t *c, int progressive, int upto) {
  long bad = 0;
  uint32_t intro = in->seen | MK_BASIC, devel = (in->seen & in->developed) | MK_BASIC, prev_set = 0;
  int prev_stage = -1, prev_type = -1, prev_rare = 0, prev_lvl = -1, pend = -1;
  c->layouts++;
  if (in->mode == RM_STORY) {
    /* the world: office or vents first, the core last, every biome once */
    uint8_t seenb = 0;
    for (int p = 0; p < BI_COUNT; p++) seenb |= (uint8_t)(1u << r->order[p]);
    if (seenb != 0xff || (r->order[0] != BI_OFFICE && r->order[0] != BI_VENTS) || r->order[7] != BI_CORE)
      c->struct_bad++, bad++;
    /* blocks: S stages, the mid-boss after (S+1)/2, the boss, a landing between biomes; then FINAL, EGRESS */
    int i = 0;
    for (int p = in->door; p < BI_COUNT; p++) {
      int S = r->stages[p], h = (S + 1) / 2, st = 0;
      if (S < 4 || S > 6) c->struct_bad++, bad++;
      for (int j = 0; j < S + 2; j++, i++) {
        int want = j == h ? SK_MIDBOSS : j == S + 1 ? SK_BOSS : SK_STAGE;
        if (r->s[i].kind != want || r->s[i].bpos != p || r->s[i].biome != r->order[p]) c->struct_bad++, bad++;
        if (want == SK_STAGE) st++;
      }
      if (st != S) c->struct_bad++, bad++;
      if (p < BI_COUNT - 1) {
        if (r->s[i].kind != SK_LANDING) c->struct_bad++, bad++;
        i++;
      }
    }
    if (r->s[i].kind != SK_FINAL || r->s[i + 1].kind != SK_EGRESS || i + 2 != r->n) c->struct_bad++, bad++;
    if (in->door == 0) {
      c->front_layouts++;
      c->floors_sum += r->floors;
      if (!c->floors_min || r->floors < c->floors_min) c->floors_min = r->floors;
      if (r->floors > c->floors_max) c->floors_max = r->floors;
      if (r->floors < 50 || r->floors > 57) c->struct_bad++, bad++;
    }
  }
  for (int i = 0; i < r->n; i++) {
    const eg_slot *s = &r->s[i];
    c->slots++;
    /* the nudge can never take a slot under its floor, nor more than EG_PLAN_SOFT under its plan */
    {
      uint8_t lo = eg_nudge_level(s->target, -100, s->floor_min);
      if (lo < s->floor_min || (int)lo < (int)s->target - EG_PLAN_SOFT) c->nudge_bad++, bad++;
      if (s->kind != SK_STAGE && s->kind != SK_LANDING && s->kind != SK_EGRESS && s->floor_min != s->target)
        c->nudge_bad++, bad++; /* fights and the final room are never softened */
    }
    if (s->kind == SK_MIDBOSS || s->kind == SK_BOSS) {
      prev_type = -1; /* a fight sits between two stages */
      continue;
    }
    if (s->kind != SK_STAGE) continue;
    uint32_t set = set_of(s);
    int nm = popc(set & ~MK_BASIC), played = i < upto; /* the statistics count the slots the heir reached */
    if (played) {
      c->stage_slots++;
      c->stage_by_pos[s->bpos]++;
      c->types[s->stype]++;
      if (SG_RARE(s->stype)) c->rare[s->stype]++;
      if (nm >= 2) c->mixed_by_pos[s->bpos]++;
    }
    /* bands: nothing before its biome position */
    for (int k = 0; k < 3; k++)
      if (s->m[k] != 255u && r->band[s->m[k]] > s->bpos) c->band_bad++, bad++;
    /* develop follows its introduction, in the very next stage slot */
    if (pend >= 0 && !(s->cstage == ST_DEVELOP && s->m[0] == pend)) c->develop_bad++, bad++;
    pend = -1;
    if (s->cstage == ST_INTRO) {
      c->intro_n++;
      if (played) c->intros_by_pos[s->bpos]++;
      int m = s->m[0];
      if (m == 255 || s->m[1] != 255u || s->m[2] != 255u || (intro & (1u << m)) || s->stype != SG_PUZZLE ||
          s->target > EG_INTRO_LEVEL(s->bpos) || s->twist || in->mode == RM_DESCENT)
        c->intro_bad++, bad++;
      if (m != 255) {
        intro |= 1u << m;
        pend = m;
        if (progressive && played) {
          int d = s->bpos - r->band[m];
          c->intro_delay[d < 0 ? 3 : d > 2 ? 3 : d]++;
        }
      }
    } else if (s->cstage == ST_DEVELOP) {
      if (s->m[0] == 255u || !(intro & (1u << s->m[0])) || s->m[1] != 255u) c->order_bad++, bad++;
      if (s->m[0] != 255u) devel |= 1u << s->m[0];
    } else {
      /* a mix: only mechanics the player has been introduced to AND has developed */
      if (set & ~devel) c->order_bad++, bad++;
      if (in->mode == RM_DESCENT && nm < 2 && popc(devel & ~MK_BASIC) >= 2) c->descent_bad++, bad++;
    }
    /* no repeats: the same mechanics at the same level, the same non-puzzle type, or two rare stages, running */
    if (prev_stage >= 0 && set == prev_set && (int)s->target == prev_lvl) c->repeat_bad++, bad++;
    if (prev_type >= 0 && s->stype != SG_PUZZLE && (int)s->stype == prev_type) c->type_repeat++, bad++;
    if (prev_rare && SG_RARE(s->stype)) c->rare_repeat++, bad++;
    prev_stage = i;
    prev_set = set;
    prev_lvl = s->target;
    prev_type = s->stype;
    prev_rare = SG_RARE(s->stype);
  }
  return bad;
}

typedef struct {
  uint32_t seen, devel;
  uint8_t doors; /* bit p: the door to biome position p is open (bit 0, the front door, always) */
  uint16_t cycle_seed;
  uint8_t cycle;
} line_t;

int full_check(int runs, int verbose) {
  checks_t c;
  memset(&c, 0, sizeof c);
  static eg_run r;
  long bad = 0;
  int done = 0;
  rs = 0x2468aceu;
  for (int li = 0; done < runs; li++) {
    line_t L = {0, 0, 1, (uint16_t)rnd(), (uint8_t)(1 + (li % 5 == 4) + (li % 10 == 9))};
    for (int run = 0; run < RUNS_PER_LINE && done < runs; run++, done++) {
      eg_run_in in;
      memset(&in, 0, sizeof in);
      in.run_seed = (uint16_t)rnd();
      in.cycle_seed = L.cycle_seed;
      in.cycle = L.cycle;
      in.generation = (uint8_t)run;
      in.sky = (uint8_t)(rnd() % 5 == 0);
      int stress = run && rnd() % 4 == 0;
      if (stress) { /* any save the cart might hold: arbitrary history, any door */
        in.seen = rnd() & ((1u << MK_ALL) - 1u) & ~MK_BASIC;
        in.developed = rnd() & in.seen;
        in.door = (uint8_t)(rnd() % (EG_LAST_DOOR + 1));
      } else {
        in.seen = L.seen;
        in.developed = L.devel;
        int d = 0;
        for (int p = 0; p < BI_COUNT; p++)
          if (L.doors & (1u << p)) d = p;
        in.door = (uint8_t)(rnd() % 3 ? d : 0);
      }
      eg_run_build(&r, &in);
      /* the heir dies somewhere: every stage ~6%, a mid-boss 12%, a boss 20% */
      int upto = r.n;
      for (int i = 0; i < r.n; i++) {
        const eg_slot *s = &r.s[i];
        double die = s->kind == SK_BOSS ? 0.15 : s->kind == SK_MIDBOSS ? 0.08 : s->kind == SK_STAGE ? 0.035 : 0.0;
        if (frand() < die) {
          upto = i;
          break;
        }
      }
      bad += check_layout(&r, &in, &c, !stress, stress ? 0 : upto);
      if (!run && !stress) { /* a fresh line's whole layout: where the introductions land if it went all the way */
        c.fresh_layouts++;
        uint32_t met = 0;
        for (int i = 0; i < r.n; i++)
          if (r.s[i].kind == SK_STAGE && r.s[i].cstage == ST_INTRO) {
            c.fresh_pos_hit[r.s[i].bpos]++;
            met |= 1u << r.s[i].m[0];
          }
        c.fresh_missing += MK_ALL - 2 - popc(met);
      }
      /* what the heir cleared goes into the save */
      if (!stress)
        for (int i = 0; i < upto; i++) {
          const eg_slot *s = &r.s[i];
          if (s->kind == SK_STAGE && s->cstage == ST_INTRO) L.seen |= 1u << s->m[0];
          if (s->kind == SK_STAGE && s->cstage == ST_DEVELOP) L.devel |= 1u << s->m[0];
          if (s->kind == SK_BOSS && s->bpos <= EG_LAST_DOOR) L.doors |= (uint8_t)(1u << s->bpos);
        }
    }
    /* descent strata after an egress: mixes only, themes stacking */
    for (int k = 0; k < 4; k++) {
      eg_run_in in;
      memset(&in, 0, sizeof in);
      in.run_seed = (uint16_t)rnd();
      in.cycle_seed = L.cycle_seed;
      in.cycle = L.cycle;
      in.mode = RM_DESCENT;
      in.stratum = (uint8_t)(rnd() % 30);
      in.seen = in.developed = ((1u << MK_ALL) - 1u) & ~MK_BASIC;
      eg_run_build(&r, &in);
      bad += check_layout(&r, &in, &c, 0, r.n);
    }
  }
  /* the nudge itself: any run of results keeps the bias inside [-SOFT, +HARD], and softening needs a streak */
  for (int t = 0; t < 20000; t++) {
    int8_t b = 0;
    uint8_t st = 0, fails = 0;
    for (int k = 0; k < 40; k++) {
      int f = rnd() % 3 == 0, cl = !f && rnd() % 2, fa = cl && rnd() % 2;
      int8_t before = b;
      eg_nudge_result(&b, &st, (uint8_t)f, (uint8_t)cl, (uint8_t)fa);
      fails = f ? (uint8_t)(fails + 1) : 0;
      if (b < -EG_PLAN_SOFT || b > EG_PLAN_HARD || (b < before && fails < EG_PLAN_STREAK)) c.nudge_bad++, bad++;
    }
  }
  /* introductions are spread: on a fresh line every biome but the last introduces something */
  for (int p = 0; p < BI_COUNT - 1; p++)
    if (c.fresh_pos_hit[p] < c.fresh_layouts) c.struct_bad++, bad++;
  long late_stage = c.stage_by_pos[5] + c.stage_by_pos[6] + c.stage_by_pos[7];
  long late_mix = c.mixed_by_pos[5] + c.mixed_by_pos[6] + c.mixed_by_pos[7];
  if (verbose) {
    printf("== full run (3.6): %ld layouts (%d story runs over lines of 25, plus descent strata), %ld slots ==\n",
           c.layouts, done, c.slots);
    printf("floors per front-door run: %ld..%ld, mean %.1f\n", c.floors_min, c.floors_max,
           c.front_layouts ? (double)c.floors_sum / c.front_layouts : 0.0);
    printf("structure breaks %ld; band breaks %ld; introduce -> develop -> combine ordering breaks %ld\n",
           c.struct_bad, c.band_bad, c.order_bad);
    printf("introductions %ld, not alone/gentle %ld; develop not right after its introduction %ld\n", c.intro_n,
           c.intro_bad, c.develop_bad);
    printf("back to back: same mechanics at the same level %ld, same stage type %ld, two rare stages %ld\n",
           c.repeat_bad, c.type_repeat, c.rare_repeat);
    printf("nudge under its floor or past its cap %ld; descent slots that don't mix %ld\n", c.nudge_bad, c.descent_bad);
    printf("introductions by biome position (slots reached): ");
    for (int p = 0; p < BI_COUNT; p++) printf("%6ld", c.intros_by_pos[p]);
    printf("\nfresh line, whole layout: every position introduces (of %ld):", c.fresh_layouts);
    for (int p = 0; p < BI_COUNT; p++) printf(" %ld", c.fresh_pos_hit[p]);
    printf("\n  mechanics a fresh line would not meet even going all the way: %.2f of %d\n",
           c.fresh_layouts ? (double)c.fresh_missing / c.fresh_layouts : 0.0, MK_ALL - 2);
    long dn = c.intro_delay[0] + c.intro_delay[1] + c.intro_delay[2] + c.intro_delay[3];
    printf("progressive lines: introduced in its own biome %.1f%%, one later %.1f%%, two later %.1f%%, later %.1f%%\n",
           dn ? 100.0 * c.intro_delay[0] / dn : 0, dn ? 100.0 * c.intro_delay[1] / dn : 0,
           dn ? 100.0 * c.intro_delay[2] / dn : 0, dn ? 100.0 * c.intro_delay[3] / dn : 0);
    printf("mixed share by biome position: ");
    for (int p = 0; p < BI_COUNT; p++)
      printf("%5.0f%%", c.stage_by_pos[p] ? 100.0 * c.mixed_by_pos[p] / c.stage_by_pos[p] : 0.0);
    printf("  (late biomes %.0f%%)\n", late_stage ? 100.0 * late_mix / late_stage : 0.0);
    printf("stage types:");
    for (int t = 0; t < SG_COUNT; t++) printf(" %s %.1f%%", sg_name[t], 100.0 * c.types[t] / (c.stage_slots ? c.stage_slots : 1));
    printf("\n");
  }
  if (late_stage && late_mix * 100 < late_stage * 60) bad++; /* the late biomes mostly combine */
  if (verbose) printf("full run: %s\n", bad ? "FAIL" : "ok");
  return bad ? 1 : 0;
}

/* ------------------------------------------------------------- the survival model ----------------------------------- */
/* A skilled player, one heir per generation. The heir dies on a floor with
 *   P(death) = H0 + HMAX * logistic((challenge - ability) / W)
 * challenge = the slot's level after the capped nudge + the stage type's and fight's pressure;
 * ability   = the player's skill (grows with floors played) + biome familiarity + the line's strength (inherited
 *             mods, recipes, the bounded genes; 9.5) + the run's kit (hearts and pieces found so far) + the heir's build
 *             (9.3's spread; a bred strategy adds at most 9.4's measured gap) - an unseen mechanic's surprise.
 * Bosses: every attempt teaches the fight (tells never change), so a boss gets easier with the line's attempts.
 * Shortcuts: the player takes the deepest open door; the door's trade-offs (no kit from the skipped biomes, half the
 * treasure in the first biome) are in the kit term. A door opens once its fee is paid at a landing. */
static double SK_BASE = 6.2;   /* a skilled player's level on the first heir */
static double SK_GROW = 2.45;   /* ... and what experience adds, */
static double SK_TAU = 200.0;  /* ... over this many floors */
static double FAM_MAX = 1.0;   /* knowing a biome: its cards, mobs and hazards */
static double FAM_TAU = 5.0;   /* visits */
static double LIN_MAX = 3.2;   /* the line's strength at its strongest */
static double LIN_TAU = 6.0;  /* generations */
static double KIT_MAX = 1.0;   /* what a run picks up as it goes */
static double KIT_TAU = 14.0;  /* floors */
static double BUILD_SD = 0.35; /* one heir's build against another's */
static double BRED_EDGE = 0.21; /* the best bred strategy: 9.4's +3.7 points on the ~58% stage bar = W x 0.153 logits */
static double BOSS_LEARN = 0.30; /* per earlier attempt at that fight */
static double BOSS_LEARN_MAX = 1.5;
static double NEW_MECH = 0.7;  /* the surprise of an unseen mechanic outside its introduction */
static double H0 = 0.006;      /* a slip on any floor */
static double HMAX = 0.85;
static double W = 1.345;
static const double sg_press[SG_COUNT] = {0.0, 0.8, 0.8, 0.5, 0.9, 0.9, 0.5, 0.3, 1.0, 1.6, 0.4};
static double MIDBOSS_PRESS = 1.22;
static double BOSS_PRESS = 2.99;
static double FINAL_PRESS = 2.6;
#define LVW_DEFAULT 0.607
static double LVW = LVW_DEFAULT; /* a level's weight in the challenge */
/* tuning only: EGSURV="NAME=value,..." overrides the knobs above (the committed values are the defaults) */
static void knobs_from_env(void) {
  struct { const char *n; double *v; } k[] = {
      {"SK_BASE", &SK_BASE},
      {"SK_GROW", &SK_GROW},
      {"SK_TAU", &SK_TAU},
      {"FAM_MAX", &FAM_MAX},
      {"FAM_TAU", &FAM_TAU},
      {"LIN_MAX", &LIN_MAX},
      {"LIN_TAU", &LIN_TAU},
      {"KIT_MAX", &KIT_MAX},
      {"KIT_TAU", &KIT_TAU},
      {"BUILD_SD", &BUILD_SD},
      {"BRED_EDGE", &BRED_EDGE},
      {"BOSS_LEARN", &BOSS_LEARN},
      {"BOSS_LEARN_MAX", &BOSS_LEARN_MAX},
      {"NEW_MECH", &NEW_MECH},
      {"H0", &H0},
      {"HMAX", &HMAX},
      {"W", &W},
      {"MIDBOSS_PRESS", &MIDBOSS_PRESS},
      {"BOSS_PRESS", &BOSS_PRESS},
      {"FINAL_PRESS", &FINAL_PRESS},
      {"LVW", &LVW}};
  const char *e = getenv("EGSURV");
  char name[32];
  double v;
  int used;
  while (e && sscanf(e, "%31[^=]=%lf%n", name, &v, &used) == 2) {
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
      if (!strcmp(k[i].n, name)) *k[i].v = v;
    e += used;
    if (*e == ',') e++;
  }
}
#define DOOR_FEE(p) (40 * (p)) /* tokens, paid once at a landing */
#define TOKENS_PER_FLOOR 7

static double logistic(double x) { return 1.0 / (1.0 + exp(-x)); }

#define GMAX 64
#define SEED_TIERS 6
#define SEED_CELLS (1 << 18) /* open addressing; kept under 3/4 full */
typedef struct {
  uint32_t key;
  double v, v2; /* plays per line, summed over lines, and squared */
} cell_t;
static cell_t cells[SEED_CELLS];
static int ncells;
static int cell_find(uint32_t key) {
  uint32_t h = (key * 2654435761u) & (SEED_CELLS - 1);
  while (cells[h].key && cells[h].key != key) h = (h + 1) & (SEED_CELLS - 1);
  if (!cells[h].key) {
    if (ncells > SEED_CELLS * 3 / 4) return -1;
    cells[h].key = key, ncells++;
  }
  return (int)h;
}

int survive(int lines, int gens, const char *json) {
  static eg_run r;
  if (gens > GMAX) gens = GMAX;
  knobs_from_env();
  /* per generation: runs, the deepest biome position reached (8: the egress), the door taken */
  static long reach[GMAX][BI_COUNT + 1], egress[GMAX], front_clear[GMAX], door_hist[GMAX][BI_COUNT], first_egress[GMAX + 1];
  static long strat_egress[2][GMAX], strat_runs[2][GMAX], soft_used, soft_slots, floors_played;
  static double minutes_full;
  static long minutes_full_n;
  static uint16_t line_cell_plays[SEED_CELLS];
  /* the stages heirs actually played, by generation group (1-3, 4-8, 9-15, 16-30, 31+) and biome position */
  static long pl_stage[5][BI_COUNT], pl_mix[5][BI_COUNT], pl_intro[5][BI_COUNT], pl_runs[5];
  memset(reach, 0, sizeof reach);
  rs = 0x13579bdu;
  memset(cells, 0, sizeof cells);
  ncells = 0;
  for (int li = 0; li < lines; li++) {
    int strat = li & 1; /* 0: random picks; 1: the best bred strategy (9.4: +3.7 points on the clear bar) */
    line_t L = {0, 0, 1, (uint16_t)rnd(), 1};
    int fee_left[BI_COUNT] = {0}, found = 1, boss_tries[BI_COUNT] = {0}, visits[BI_COUNT] = {0}, egressed = 0;
    long floors_total = 0;
    memset(line_cell_plays, 0, sizeof line_cell_plays);
    for (int g = 0; g < gens; g++) {
      eg_run_in in;
      memset(&in, 0, sizeof in);
      in.run_seed = (uint16_t)rnd();
      in.cycle_seed = L.cycle_seed;
      in.cycle = 1;
      in.generation = (uint8_t)g;
      in.seen = L.seen;
      in.developed = L.devel;
      in.sky = (uint8_t)(rnd() % 5 == 0);
      int d = 0;
      for (int p = 0; p < BI_COUNT; p++)
        if (L.doors & (1u << p)) d = p;
      in.door = (uint8_t)d;
      door_hist[g][d]++;
      eg_run_build(&r, &in);
      double skill = SK_BASE + SK_GROW * (1.0 - exp(-floors_total / SK_TAU));
      double lin = LIN_MAX * (1.0 - exp(-g / LIN_TAU));
      double build = BUILD_SD * gauss() + (strat ? BRED_EDGE : 0.0);
      int8_t bias[MK_ALL] = {0};
      uint8_t streak[MK_ALL] = {0};
      int floors_run = 0, deepest = d, tokens = 0, died = 0;
      double minutes = 0;
      for (int i = 0; i < r.n && !died; i++) {
        const eg_slot *s = &r.s[i];
        if (s->kind == SK_LANDING) {
          /* the landing: pay toward a found door (LOCK's toll keeper) */
          for (int p = 1; p < BI_COUNT; p++)
            if ((found & (1 << p)) && !(L.doors & (1u << p)) && tokens > 0) {
              int pay = tokens < fee_left[p] ? tokens : fee_left[p];
              tokens -= pay;
              fee_left[p] -= pay;
              if (!fee_left[p]) L.doors |= (uint8_t)(1u << p);
            }
          minutes += 2;
          continue;
        }
        if (s->bpos > deepest) deepest = s->bpos;
        double kit = KIT_MAX * (1.0 - exp(-floors_run / KIT_TAU));
        double fam = FAM_MAX * (1.0 - exp(-visits[s->biome] / FAM_TAU));
        double ab = skill + lin + build + kit + fam;
        double ch;
        int m0 = s->m[0];
        if (s->kind == SK_STAGE) {
          uint8_t lvl = eg_nudge_level(s->target, m0 != 255 ? bias[m0] : 0, s->floor_min);
          soft_slots++;
          if (lvl < s->target) soft_used++;
          ch = LVW * lvl + sg_press[s->stype];
          {
            int gg = g < 3 ? 0 : g < 8 ? 1 : g < 15 ? 2 : g < 30 ? 3 : 4;
            pl_stage[gg][s->bpos]++;
            pl_mix[gg][s->bpos] += popc(set_of(s) & ~MK_BASIC) >= 2;
            pl_intro[gg][s->bpos] += s->cstage == ST_INTRO;
          }
          for (int k = 0; k < 3; k++)
            if (s->cstage != ST_INTRO && s->m[k] != 255u && !((L.seen | in.seen) & (1u << s->m[k]))) ch += NEW_MECH;
          ch += popc(s->twist) * 0.4;
          minutes += s->stype == SG_PUZZLE ? 3.0 : 2.5 + (SG_RARE(s->stype) ? 1.0 : 0.0);
          /* the cell this slot draws its room from: curriculum class, mechanics, tier (the 3.6.9 bank estimate) */
          if (g < 30) {
            uint32_t cls = s->cstage == ST_INTRO ? 1u : s->cstage == ST_DEVELOP ? 2u : 3u;
            /* key: lead mechanic | mix size | class | stage type | tier. A baked room serves a slot when it holds the
             * slot's lead mechanic and nothing outside the slot's open set (3.6.9), so the bank is keyed coarse */
            uint32_t nmix = (uint32_t)popc(set_of(s) & ~MK_BASIC);
            uint32_t key = (uint32_t)(m0 + 1) << 12 | (nmix >= 3 ? 1u : 0u) << 10 | cls << 8 |
                           (uint32_t)s->stype << 4 | (uint32_t)(lvl * SEED_TIERS / 16);
            int h = cell_find(key);
            if (h >= 0) line_cell_plays[h]++;
          }
        } else if (s->kind == SK_MIDBOSS || s->kind == SK_BOSS) {
          double learn = BOSS_LEARN * boss_tries[s->bpos] * (s->kind == SK_BOSS ? 1.0 : 0.6);
          if (learn > BOSS_LEARN_MAX) learn = BOSS_LEARN_MAX;
          ch = LVW * s->target + (s->kind == SK_BOSS ? BOSS_PRESS : MIDBOSS_PRESS) - learn;
          if (s->kind == SK_BOSS) boss_tries[s->bpos]++;
          minutes += s->kind == SK_BOSS ? 5 : 3;
        } else if (s->kind == SK_FINAL) {
          ch = LVW * s->target + FINAL_PRESS;
          minutes += 4;
        } else {
          ch = -99; /* the egress: the question */
          minutes += 2;
        }
        double pd = s->kind == SK_EGRESS ? 0.0 : H0 + HMAX * logistic((ch - ab) / W);
        if (frand() < pd) {
          died = 1;
          break;
        }
        floors_run++;
        floors_total++;
        floors_played++;
        tokens += TOKENS_PER_FLOOR;
        if (s->kind == SK_STAGE) {
          /* how the room went, for the nudge */
          double gap = ch - ab;
          int failed = frand() < logistic((gap + 0.8) / W) * 0.9, clean = !failed && gap < -1.0;
          if (m0 != 255) eg_nudge_result(&bias[m0], &streak[m0], (uint8_t)failed, (uint8_t)clean, (uint8_t)(clean && gap < -2.5));
          if (s->cstage == ST_INTRO) L.seen |= 1u << m0;
          if (s->cstage == ST_DEVELOP) L.devel |= 1u << m0;
        }
        if (s->kind == SK_BOSS && s->bpos > 0 && s->bpos < EG_LAST_DOOR + 1 && !(found & (1 << s->bpos))) {
          found |= 1 << s->bpos;
          fee_left[s->bpos] = DOOR_FEE(s->bpos);
        }
      }
      for (int p = d; p < BI_COUNT; p++) visits[r.order[p]] += p <= deepest;
      int got_out = !died;
      reach[g][got_out ? BI_COUNT : deepest]++;
      if (got_out) {
        egress[g]++;
        if (d == 0) front_clear[g]++, minutes_full += minutes, minutes_full_n++;
        if (!egressed) first_egress[g]++, egressed = 1;
      }
      strat_runs[strat][g]++;
      pl_runs[g < 3 ? 0 : g < 8 ? 1 : g < 15 ? 2 : g < 30 ? 3 : 4]++;
      strat_egress[strat][g] += got_out;
    }
    if (!egressed) first_egress[gens]++;
    for (int h = 0; h < SEED_CELLS; h++)
      if (line_cell_plays[h]) cells[h].v += line_cell_plays[h], cells[h].v2 += (double)line_cell_plays[h] * line_cell_plays[h];
  }
  FILE *j = fopen(json, "w");
  if (!j) return 2;
  fprintf(j, "{\n  \"lines\": %d, \"generations\": %d,\n", lines, gens);
  fprintf(j, "  \"knobs\": {\"skill_base\": %.2f, \"skill_grow\": %.2f, \"skill_tau_floors\": %.0f, \"familiarity\": %.2f, "
             "\"lineage_max\": %.2f, \"lineage_tau_gens\": %.0f, \"kit_max\": %.2f, \"kit_tau_floors\": %.0f, "
             "\"build_sd\": %.2f, \"boss_learn\": %.2f, \"h0\": %.3f, \"hmax\": %.2f, \"w\": %.2f, "
             "\"midboss_press\": %.1f, \"boss_press\": %.1f, \"final_press\": %.1f},\n",
          SK_BASE, SK_GROW, SK_TAU, FAM_MAX, LIN_MAX, LIN_TAU, KIT_MAX, KIT_TAU, BUILD_SD, BOSS_LEARN, H0, HMAX, W,
          MIDBOSS_PRESS, BOSS_PRESS, FINAL_PRESS);
  fprintf(j, "  \"reach\": [");
  for (int g = 0; g < gens; g++) {
    fprintf(j, "%s[", g ? ", " : "");
    for (int b = 0; b <= BI_COUNT; b++) fprintf(j, "%s%ld", b ? ", " : "", reach[g][b]);
    fprintf(j, "]");
  }
  fprintf(j, "],\n  \"door\": [");
  for (int g = 0; g < gens; g++) {
    fprintf(j, "%s[", g ? ", " : "");
    for (int b = 0; b < BI_COUNT; b++) fprintf(j, "%s%ld", b ? ", " : "", door_hist[g][b]);
    fprintf(j, "]");
  }
  fprintf(j, "],\n  \"egress\": [");
  for (int g = 0; g < gens; g++) fprintf(j, "%s%ld", g ? ", " : "", egress[g]);
  fprintf(j, "],\n  \"front_door_clear\": [");
  for (int g = 0; g < gens; g++) fprintf(j, "%s%ld", g ? ", " : "", front_clear[g]);
  fprintf(j, "],\n  \"first_egress\": [");
  for (int g = 0; g <= gens; g++) fprintf(j, "%s%ld", g ? ", " : "", first_egress[g]);
  fprintf(j, "],\n  \"strategy_egress\": {\"random\": [");
  for (int g = 0; g < gens; g++) fprintf(j, "%s%.4f", g ? ", " : "", strat_runs[0][g] ? (double)strat_egress[0][g] / strat_runs[0][g] : 0.0);
  fprintf(j, "], \"bred\": [");
  for (int g = 0; g < gens; g++) fprintf(j, "%s%.4f", g ? ", " : "", strat_runs[1][g] ? (double)strat_egress[1][g] / strat_runs[1][g] : 0.0);
  fprintf(j, "]},\n  \"softened_share\": %.4f,\n  \"floors_played\": %ld,\n", soft_slots ? (double)soft_used / soft_slots : 0.0,
          floors_played);
  {
    const char *nm[3] = {"played_stages", "played_mixed", "played_intros"};
    long(*arr[3])[BI_COUNT] = {pl_stage, pl_mix, pl_intro};
    fprintf(j, "  \"played_runs_by_group\": [%ld, %ld, %ld, %ld, %ld],\n", pl_runs[0], pl_runs[1], pl_runs[2], pl_runs[3],
            pl_runs[4]);
    for (int a = 0; a < 3; a++) {
      fprintf(j, "  \"%s\": [", nm[a]);
      for (int gg = 0; gg < 5; gg++) {
        fprintf(j, "%s[", gg ? ", " : "");
        for (int b = 0; b < BI_COUNT; b++) fprintf(j, "%s%ld", b ? ", " : "", arr[a][gg][b]);
        fprintf(j, "]");
      }
      fprintf(j, "],\n");
    }
  }
  fprintf(j, "  \"minutes_front_door_clear\": %.1f,\n", minutes_full_n ? minutes_full / minutes_full_n : 0.0);
  /* the seed-bank demand: per cell (mechanics, class, stage type, tier) the plays per line over 30 generations */
  fprintf(j, "  \"cells\": [");
  int first = 1;
  for (int h = 0; h < SEED_CELLS; h++)
    if (cells[h].key) {
      fprintf(j, "%s[%u, %.4f, %.4f]", first ? "" : ", ", cells[h].key, cells[h].v / lines, cells[h].v2 / lines);
      first = 0;
    }
  fprintf(j, "]\n}\n");
  fclose(j);
  printf("survive: %d lines x %d generations -> %s (%d bank cells)\n", lines, gens, json, ncells);
  for (int g = 0; g < gens; g++)
    if (g == 0 || g == 4 || g == 9 || g == 19 || g == 29 || g == gens - 1) {
      long n = 0;
      for (int b = 0; b <= BI_COUNT; b++) n += reach[g][b];
      double at[BI_COUNT + 2] = {0};
      for (int b = BI_COUNT; b >= 0; b--) at[b] = at[b + 1] + reach[g][b];
      printf("  gen %2d: reach biome 2 %5.1f%%  3 %5.1f%%  4 %5.1f%%  6 %5.1f%%  8 %5.1f%%  egress %5.2f%% (front door %5.2f%%)\n",
             g + 1, 100 * at[1] / n, 100 * at[2] / n, 100 * at[3] / n, 100 * at[5] / n, 100 * at[7] / n,
             100.0 * egress[g] / n, 100.0 * front_clear[g] / n);
    }
  return 0;
}

/* -------------------------------------------------------------- the waves, for the plots ---------------------------- */
int full_runsim(int runs, const char *csvp, const char *json) {
  static eg_run r;
  FILE *csv = fopen(csvp, "w");
  if (!csv) return 2;
  fprintf(csv, "run,gen,reach,slot,floor,kind,stype,cstage,bpos,biome,nmech,target,floor_min,level,new,twist\n");
  rs = 0xabcdefu;
  long soft = 0, stages = 0;
  for (int k = 0; k < runs; k++) {
    eg_run_in in;
    memset(&in, 0, sizeof in);
    in.run_seed = (uint16_t)rnd();
    in.cycle_seed = (uint16_t)rnd();
    in.cycle = 1;
    in.generation = (uint8_t)(k % 30);
    /* a line's history at that generation: everything banded before the depth such a line has reached (the median
     * deepest biome in the survival model: 2 by generation 1, 4 by 5, 6 by 10, 7 by 15, 8 by 20) */
    int g = in.generation, reached = g < 1 ? 1 : g < 5 ? 2 : g < 10 ? 4 : g < 15 ? 5 : g < 20 ? 6 : 7;
    eg_run_world(&r, in.cycle_seed, 1);
    for (int m = MK_BURN; m < MK_ALL; m++)
      if (r.band[m] < reached) in.seen |= 1u << m;
    in.developed = in.seen;
    in.sky = (uint8_t)(rnd() % 5 == 0);
    eg_run_build(&r, &in);
    int8_t bias[MK_ALL] = {0};
    uint8_t streak[MK_ALL] = {0};
    double skill = 5.0 + in.generation * 0.25;
    for (int i = 0; i < r.n; i++) {
      const eg_slot *s = &r.s[i];
      int lvl = s->target, nm = popc(set_of(s) & ~MK_BASIC);
      if (s->kind == SK_STAGE) {
        int m0 = s->m[0];
        lvl = eg_nudge_level(s->target, m0 != 255 ? bias[m0] : 0, s->floor_min);
        stages++;
        soft += lvl < s->target;
        double gap = lvl + sg_press[s->stype] - skill - s->bpos * 0.35;
        int failed = frand() < logistic((gap + 0.8) / W) * 0.9, clean = !failed && gap < -1.0;
        if (m0 != 255) eg_nudge_result(&bias[m0], &streak[m0], (uint8_t)failed, (uint8_t)clean, (uint8_t)(clean && gap < -2.5));
      }
      fprintf(csv, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", k, in.generation, reached, i, s->floor, s->kind, s->stype,
              s->cstage, s->bpos, s->biome, nm, s->target, s->floor_min, lvl,
              s->kind == SK_STAGE && s->cstage == ST_INTRO ? s->m[0] : -1, s->twist);
    }
  }
  fclose(csv);
  FILE *j = fopen(json, "w");
  if (j) {
    fprintf(j, "{\"runs\": %d, \"stage_slots\": %ld, \"softened\": %ld}\n", runs, stages, soft);
    fclose(j);
  }
  printf("runsim: %d runs, %ld stage slots, %.1f%% softened by the capped nudge\n", runs, stages,
         stages ? 100.0 * soft / stages : 0.0);
  (void)mk_name;
  (void)bi_name;
  return 0;
}

/* one run's layout as text (the doc's sample) */
int full_layout(unsigned run_seed, unsigned cycle_seed, unsigned gen, uint32_t seen, unsigned door) {
  static eg_run r;
  eg_run_in in;
  memset(&in, 0, sizeof in);
  in.run_seed = (uint16_t)run_seed;
  in.cycle_seed = (uint16_t)cycle_seed;
  in.cycle = 1;
  in.generation = (uint8_t)gen;
  in.seen = in.developed = seen;
  in.door = (uint8_t)door;
  eg_run_build(&r, &in);
  static const char *kn[] = {"stage", "MID-BOSS", "BOSS", "landing", "FINAL", "EGRESS"};
  static const char *cn[] = {"introduce", "develop", "combine", "test"};
  for (int i = 0; i < r.n; i++) {
    const eg_slot *s = &r.s[i];
    printf("%2d  b%d %-7s %-8s", s->floor, s->bpos + 1, bi_name[s->biome], kn[s->kind]);
    if (s->kind == SK_STAGE) {
      printf(" %-9s %-7s", cn[s->cstage], sg_name[s->stype]);
      for (int k = 0; k < 3; k++)
        if (s->m[k] != 255u) printf(" %s", mk_name[s->m[k]]);
      if (s->twist) printf("  +theme 0x%02x", s->twist);
    }
    if (s->kind != SK_LANDING) printf("  L%d (floor %d)", s->target, s->floor_min);
    printf("\n");
  }
  printf("%d floors\n", r.floors);
  return 0;
}
