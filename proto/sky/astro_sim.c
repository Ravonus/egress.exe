/* Balance check for the birth charts and the sky (redesign 9.7), through the matrix prototype's own combat model.
 * It includes proto/matrix/sim.c unchanged (its main renamed), so the bred heirs, the runs and the costs are exactly
 * the adopted "final" setting of redesign 9.3 (doc traits, HEAVY -10% speed, stagger 5, K = 42).
 *
 *   make -C proto/sky astro_sim && ./proto/sky/out/astro_sim [runs_per_heir=32]   (run from proto/matrix)
 *
 * Births: every heir gets a seeded birth moment in 2026..2126 (the parent died then) and a chart from sky.c.
 * Alignments: 40% of heirs none, 35% one tier somewhere, 25% "optimisers" at the cap (Sun 2 + Mars 1) with a
 * strengthened Sun-Mars aspect when they have one; half of all heirs carry a parent's echo on Sun or Mars.
 * Sky: each run starts at a seeded moment in 2026..2030 (sky_read + transits to the heir's chart). */
#define main matrix_main
#include "../matrix/sim.c"
#undef main
#include "sky.h"
#include "astro.h"

static const uint8_t ELEM_TAG[4] = {MX_HOT, MX_STONE, MX_AIRY, MX_WET};
static astro_birth g_birth[MAX_BRED];
static astro_chart g_chart[MAX_BRED];
static uint8_t g_policy[MAX_BRED];
static uint8_t g_kind[MAX_BRED]; /* the heir's event-born kind (AB_*) at its seeded birth minute */
static uint8_t g_sb_mode, g_sb_force; /* SKYBORN: 0 off, 1 each heir's own kind, 2 every heir forced to g_sb_force */
#define NDAYS 1826 /* 2026-01-01 .. 2030-12-31 */
static sky_now g_day[NDAYS * 4]; /* four moments a day: 00:00 06:00 12:00 18:00 UTC */

static uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

static void make_births(void) {
  uint32_t i;
  for (i = 0; i < n_bred; i++) {
    uint32_t h = hash32(i * 2654435761u + 17u);
    astro_birth *b = &g_birth[i];
    uint8_t k;
    b->date = (uint16_t)(9497u + h % 36890u);
    b->mod = (uint16_t)((h >> 16) % 1440u);
    b->quality = AQ_PLAYER; b->approx = 0; b->order = (uint8_t)(i % 3u);
    for (k = 0; k < AP_COUNT; k++) b->tier[k] = 0;
    b->echo = 7; b->asp_pair = 15; b->asp_tier = 0;
    astro_chart_make(b->date, b->mod, &g_chart[i]);
    g_kind[i] = astro_born_kind((uint32_t)b->date * 1440u + b->mod - 9497u * 1440u);
    h = hash32(h);
    g_policy[i] = (uint8_t)(h % 100u < 40u ? 0 : h % 100u < 75u ? 1 : 2);
    if (g_policy[i] == 1) b->tier[(h >> 8) % AP_COUNT] = 1;
    if (g_policy[i] == 2) {
      b->tier[AP_SUN] = 2; b->tier[AP_MARS] = 1;
      for (k = 0; k < g_chart[i].nasp; k++) {
        const astro_asp *a = &g_chart[i].asp[k];
        if ((a->a == SKY_SUN && a->b == SKY_MARS) || (a->a == SKY_MARS && a->b == SKY_SUN)) {
          b->tier[AP_MARS] = 0; b->asp_pair = 0; b->asp_tier = 1; /* tighten Sun-Mars instead of Mars */
        }
      }
    }
    if ((h >> 20) & 1u) b->echo = ((h >> 21) & 1u) ? AP_SUN : AP_MARS;
  }
}

static void make_days(void) {
  uint32_t d;
  for (d = 0; d < NDAYS * 4u; d++) sky_read((uint16_t)(9497u + d / 4u), (uint16_t)((d % 4u) * 360u), &g_day[d]);
}

/* modes */
enum { M_OFF, M_NATAL, M_SKY, M_FORCE };
static uint8_t g_mode;
static uint16_t g_force; /* AS_* forced on every run in M_FORCE (natal on, no transits) */
static int32_t g_day_pin = -1; /* M_SKY: every run on this moment index, else seeded */
static uint8_t g_skip_fx;
static uint32_t g_run_ofs, g_run_mul = 1; /* run j uses b_runs[(j * mul + ofs) % nbr] */ /* 1: natal effects off inside M_FORCE (to isolate the state) */

static void astro_apply(uint32_t i, uint32_t run, mx_piece *pc, uint8_t *np, mx_bstats *s, uint8_t stage) {
  astro_fx f;
  mx_piece t;
  astro_fx_clear(&f);
  if (g_mode == M_OFF) return;
  if (!(g_mode == M_FORCE && g_skip_fx)) astro_natal_fx(&g_chart[i], &g_birth[i], &f);
  else { f.aff_mag = 0; f.chg_mag = 0; }
  if (g_sb_mode) astro_skyborn_fx(g_sb_mode == 1 ? g_kind[i] : g_sb_force, &f);
  if (g_mode == M_SKY) {
    uint32_t di = g_day_pin >= 0 ? (uint32_t)g_day_pin : hash32(i * 7919u + run * 104729u) % (NDAYS * 4u);
    const sky_now *n = &g_day[di];
    astro_hit hits[16];
    uint8_t nh = astro_transits(&g_chart[i], n->pos, hits, 16);
    astro_session_fx(astro_states(n, &g_chart[i]) | (astro_meteor_on((di / 4u) * 1440u + (di % 4u) * 360u) ? AS_METEOR : 0u), hits, nh, &f);
  } else if (g_mode == M_FORCE) {
    astro_session_fx(g_force, 0, 0, &f);
  }
  if (stage == 0) { /* pieces: the Sun's affinity and Mars's charge tag; Mars retrograde softens fire */
    uint8_t am = f.aff_mag, cm = f.chg_mag;
    if (f.hot_pct < 100) {
      uint8_t k;
      if (f.aff_elem == AE_FIRE) am = (uint8_t)(am * f.hot_pct / 100u);
      if (f.chg_elem == AE_FIRE) cm = (uint8_t)(cm * f.hot_pct / 100u);
      for (k = 0; k < *np; k++) /* the genome's HOT affinity too */
        if (P_TAGA(&pc[k]) == MX_HOT && P_KIND(&pc[k]) == PK_ELEMENT) {
          mx_piece q = pc[k]; uint8_t mg = (uint8_t)(P_MAG(&q) * f.hot_pct / 100u);
          pc[k] = (mx_piece)MXP(TR_HIT, EF_STATUS, MX_HOT, MX_NOTAG, mg, 0, 4, PK_ELEMENT, SG_STATUS, 0);
        }
    }
    if (am) { t = (mx_piece)MXP(TR_HIT, EF_STATUS, ELEM_TAG[f.aff_elem], MX_NOTAG, am, 0, 4, PK_ELEMENT, SG_STATUS, 0); pc[(*np)++] = t; }
    if (cm) { t = (mx_piece)MXP(TR_CHARGE, EF_STATUS, ELEM_TAG[f.chg_elem], MX_NOTAG, cm, 0, 0, PK_ELEMENT, SG_STATUS, 0); pc[(*np)++] = t; }
    if (f.parry_mag) { t = (mx_piece)MXP(TR_PARRY, EF_STAT, MX_NOTAG, MX_NOTAG, f.parry_mag, 0, 0, PK_TRAIT, SG_DEFENSE, 0); pc[(*np)++] = t; }
    if (f.g_status > 0) { t = (mx_piece)MXP(TR_PASSIVE, EF_STAT, MX_NOTAG, MX_NOTAG, f.g_status, 0, 0, PK_TRAIT, SG_STATUS, 0); pc[(*np)++] = t; }
    return;
  }
  /* stats: the nudges land after the stacks, as points (redesign 9.7 caps them at 4 a group) */
  s->off = (int16_t)(s->off + f.g_dmg + f.off);
  s->def = (int16_t)(s->def + f.g_def);
  s->avoid = (int16_t)(s->avoid + f.g_avoid + f.g_spd / 2);
  if (f.g_status < 0) s->off = (int16_t)(s->off + f.g_status);
  s->hp = (int16_t)(s->hp + f.g_hp);
  if (f.mix_pen > s->mix_pen) s->mix_pen = f.mix_pen;
  s->foe_pct = (uint8_t)(s->foe_pct + f.foe_pct);
  s->st_mult[ST_DARK] = (uint8_t)(s->st_mult[ST_DARK] * f.dark16 / 16u * f.dark_b16 / 16u);
  s->st_mult[ST_MIX] = (uint8_t)(s->st_mult[ST_MIX] * f.mix_b16 / 16u);
  if (f.opp16 != 16u) { uint8_t t2 = ELEM_TAG[f.opp_elem]; s->el_mult[t2] = (uint8_t)(s->el_mult[t2] * f.opp16 / 16u); }
}

/* one heir subset x runs; returns wins, fills per-heir rates */
static uint32_t eval_astro(uint32_t nruns, uint8_t *rates, uint32_t *win_s, uint32_t *n_s, uint32_t stride) {
  uint32_t i, j, w, tot = 0, k;
  uint8_t d, np;
  mx_piece pc[24];
  mx_bstats s;
  for (k = 0; k < 8; k++) { win_s[k] = 0; n_s[k] = 0; }
  for (i = 0; i < n_bred; i += stride) {
    const bred_heir *h = &g_bred[i];
    mx_build bb = h->b;
    bb.trait[0] = (uint8_t)((h->gbits & 8) ? 0 : 255); bb.trait[1] = 255;
    w = 0;
    for (j = 0; j < nruns; j++) {
      np = bred_pieces(h, pc);
      astro_apply(i, j, pc, &np, &s, 0);
      mx_build_stats_ex(&bb, pc, np, &s);
      apply_gene_costs(h, &s);
      astro_apply(i, j, pc, &np, &s, 1);
      w += mx_sim_run(&bb, &s, &b_runs[(j * g_run_mul + g_run_ofs) % nbr], i * 131u + j, 42, &d);
    }
    if (rates) rates[i] = (uint8_t)(w * 100u / nruns);
    win_s[h->strat] += w; n_s[h->strat] += nruns; tot += w;
  }
  return tot;
}

static int pct10(uint32_t w, uint32_t n);
static void pd(int v);
static uint32_t eval_astro(uint32_t nruns, uint8_t *rates, uint32_t *win_s, uint32_t *n_s, uint32_t stride);
static const char *const AB_NAME[AB_COUNT] = {"none", "Halley-born", "eclipse-born", "ingress-born", "retro-born",
                                              "moon-born", "meteor-born", "season-born"};

/* 5. SKYBORN (redesign 9.7.11, 19.4): the event-born third trait, against charts on with no sky (r1, tot1) */
static void skyborn_section(uint32_t nr, uint32_t tot1, const uint8_t *r1) {
  static uint8_t r2[MAX_BRED];
  uint32_t ws[8], ns[8], t, i, k, n_all = n_bred * nr, cnt[AB_COUNT] = {0}, s1[AB_COUNT] = {0}, s2[AB_COUNT] = {0};
  int dv, worst = 0;
  g_mode = M_NATAL; g_day_pin = -1; g_run_mul = 1; g_run_ofs = 0;
  printf("\n## SKYBORN: event-born heirs (charts on, no sky; vs the same heirs without the mark: %d.%d%%)\n\n", pct10(tot1, n_all) / 10, pct10(tot1, n_all) % 10);
  printf("Each kind forced on every heir (a player who times every birth to that event), and the bound of +-3:\n\n| kind | trait | clear | delta | bound |\n|---|---|---|---|---|\n");
  {
    static const char *const TR[AB_COUNT] = {"", "a wish star from 1 elite in 20 (offering +1)", "1 more hidden SHINY cell in dark rooms (dark x15/16)",
                                             "1 guided talk a run (no numbers)", "the last room's recipe hint once a floor (mix x13/16)",
                                             "+1 status", "a wish star from 1 elite in 20 (offering +1)", "+1 guard"};
    for (k = 1; k < AB_COUNT; k++) {
      g_sb_mode = 2; g_sb_force = (uint8_t)k;
      t = eval_astro(nr, 0, ws, ns, 1);
      dv = pct10(t, n_all) - pct10(tot1, n_all);
      if (abs(dv) > abs(worst)) worst = dv;
      printf("| %s | %s | %d.%d%% | ", AB_NAME[k], TR[k], pct10(t, n_all) / 10, pct10(t, n_all) % 10); pd(dv);
      printf(" | %s |\n", abs(dv) <= 30 ? "ok (<= 3)" : "**over 3**");
    }
  }
  printf("\nLargest forced move: "); pd(worst); printf(" points.\n\n");
  /* the real share: each heir's own seeded birth minute (2026..2126) */
  g_sb_mode = 1;
  t = eval_astro(nr, r2, ws, ns, 1);
  for (i = 0; i < n_bred; i++) { cnt[g_kind[i]]++; s1[g_kind[i]] += r1[i]; s2[g_kind[i]] += r2[i]; }
  printf("Natural births (each heir's own seeded minute, 2026..2126; the windows of 19.1, rarest first):\n\n| kind | heirs | without | with | delta |\n|---|---|---|---|---|\n");
  for (k = 0; k < AB_COUNT; k++)
    printf("| %s | %u | %.1f%% | %.1f%% | %+.1f |\n", AB_NAME[k], cnt[k], cnt[k] ? s1[k] / (double)cnt[k] : 0, cnt[k] ? s2[k] / (double)cnt[k] : 0,
           cnt[k] ? (s2[k] - (double)s1[k]) / cnt[k] : 0);
  {
    uint32_t e1 = 0, e2 = 0, ne = 0;
    for (k = 1; k < AB_COUNT; k++) { e1 += s1[k]; e2 += s2[k]; ne += cnt[k]; }
    printf("\nSky-born share: %u of %u heirs (%.1f%%); their clear rate %.1f%% -> %.1f%% (%+.1f). All heirs: %d.%d%% -> %d.%d%% (",
           ne, n_bred, ne * 100.0 / n_bred, ne ? e1 / (double)ne : 0, ne ? e2 / (double)ne : 0, ne ? (e2 - (double)e1) / ne : 0,
           pct10(tot1, n_all) / 10, pct10(tot1, n_all) % 10, pct10(t, n_all) / 10, pct10(t, n_all) % 10);
    pd(pct10(t, n_all) - pct10(tot1, n_all)); printf(").\n");
  }
  g_sb_mode = 0;
}

/* 6. siblings (redesign 9.7.2): the Moon and the Rising across the three kids, the new rule against the old one */
static uint16_t adiff_(uint16_t a, uint16_t b) { uint16_t d = (uint16_t)(a - b); return d > 0x8000u ? (uint16_t)(0u - d) : d; }
static int cmpd_(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static void sibling_section(void) {
  enum { NS = 20000 };
  static double moon[NS], rgap[NS], span[NS], prb[NS];
  uint32_t r, k, j, o[3];
  uint8_t sl[3];
  printf("\n## Siblings: the Moon and the Rising across the three kids (%u sets, death minutes seeded over 2026..2126)\n\n", NS);
  printf("| rule | span, median / p95 / max (min) | Risings all 3 signs | Rising gap, min / median (deg) | Moon first->last, median / p95 (deg) | Moon signs 1 / 2 / 3 | probes, mean / max |\n|---|---|---|---|---|---|---|\n");
  for (r = 0; r < 2; r++) {
    uint32_t rise3 = 0, ms[4] = {0}, pmax = 0;
    double psum = 0;
    for (k = 0; k < NS; k++) {
      uint32_t h = hash32(k * 40503u + 99u), d = 9497u * 1440u + h % 53121000u;
      uint16_t mo[3], as[3];
      uint8_t np = 0, msg, i2;
      if (r == 0) { /* the old rule: 7..97 seeded minutes after each other */
        o[0] = d; o[1] = d + 7u + hash32(h) % 91u; o[2] = o[1] + 7u + hash32(h + 1u) % 91u;
      } else np = astro_siblings(d, h, o, sl);
      for (j = 0; j < 3; j++) {
        uint16_t p[SKY_NPOS];
        sky_positions((uint16_t)(o[j] / 1440u), (uint16_t)(o[j] % 1440u), p);
        mo[j] = p[SKY_MOON]; as[j] = p[SKY_ASC];
      }
      {
        uint8_t a0 = (uint8_t)((as[0] * 12u) >> 16), a1 = (uint8_t)((as[1] * 12u) >> 16), a2 = (uint8_t)((as[2] * 12u) >> 16);
        uint8_t m0 = (uint8_t)((mo[0] * 12u) >> 16), m1 = (uint8_t)((mo[1] * 12u) >> 16), m2 = (uint8_t)((mo[2] * 12u) >> 16);
        rise3 += (uint32_t)(a0 != a1 && a1 != a2 && a0 != a2);
        msg = (uint8_t)(1u + (m1 != m0) + (m2 != m1 && m2 != m0));
        ms[msg]++;
      }
      i2 = 0; (void)i2;
      moon[k] = adiff_(mo[2], mo[0]) * 360.0 / 65536.0;
      rgap[k] = (adiff_(as[1], as[0]) < adiff_(as[2], as[1]) ? adiff_(as[1], as[0]) : adiff_(as[2], as[1])) * 360.0 / 65536.0;
      span[k] = (double)(o[2] - o[0]);
      prb[k] = np; psum += np; if (np > pmax) pmax = np;
    }
    qsort(moon, NS, sizeof(double), cmpd_); qsort(rgap, NS, sizeof(double), cmpd_); qsort(span, NS, sizeof(double), cmpd_);
    printf("| %s | %.0f / %.0f / %.0f | %.1f%% | %.1f / %.1f | %.2f / %.2f | %.1f%% / %.1f%% / %.1f%% | %.1f / %u |\n",
           r ? "new: Rising steps + Moon catch" : "old: 7..97 min apart", span[NS / 2], span[NS * 95 / 100], span[NS - 1], rise3 * 100.0 / NS,
           rgap[0], rgap[NS / 2], moon[NS / 2], moon[NS * 95 / 100], ms[1] * 100.0 / NS, ms[2] * 100.0 / NS, ms[3] * 100.0 / NS,
           r ? psum / NS : 0.0, pmax);
  }
  (void)prb;
}

static int pct10(uint32_t w, uint32_t n) { return n ? (int)((w * 1000u + n / 2u) / n) : 0; }
static void pd(int v) { printf("%s%d.%d", v < 0 ? "-" : "+", abs(v) / 10, abs(v) % 10); }

int main(int argc, char **argv) {
  uint32_t ws[8], ns[8], wb[8], nb_[8], i, k, tot0, tot1, n_all;
  static uint8_t r0[MAX_BRED], r1[MAX_BRED];
  int base[8];
  uint32_t nr = argc > 1 ? (uint32_t)atoi(argv[1]) : 32u;
  nbr = 32;
  setvbuf(stdout, 0, _IOLBF, 0);
  if (argc > 1 && !strcmp(argv[1], "sib")) { sibling_section(); return 0; } /* the sibling table alone */
  make_build_runs();
  load_bred("bred_lines.txt");
  if (!n_bred) { fprintf(stderr, "run from proto/matrix (bred_lines.txt)\n"); return 1; }
  g_bred_costs = 2; g_heavy_soft = 1; g_heavy_stagger = 5;
  make_births();
  make_days();
  n_all = n_bred * nr;
  printf("# Birth charts and the sky through the matrix model (redesign 9.7)\n\n%u bred heirs (generation 30, 200 per strategy), "
         "%u runs each, K=42, the adopted final trait model.\n\n", n_bred, nr);
  /* 1. natal on/off per strategy */
  g_mode = M_OFF; tot0 = eval_astro(nr, r0, wb, nb_, 1);
  g_mode = M_NATAL; tot1 = eval_astro(nr, r1, ws, ns, 1);
  printf("## Natal charts on (no sky)\n\n| strategy | clear, no charts | clear, charts | delta |\n|---|---|---|---|\n");
  for (k = 0; k < n_strat; k++) {
    base[k] = pct10(wb[k], nb_[k]);
    printf("| %s | %d.%d%% | %d.%d%% | ", strat_name[k], base[k] / 10, base[k] % 10, pct10(ws[k], ns[k]) / 10, pct10(ws[k], ns[k]) % 10);
    pd(pct10(ws[k], ns[k]) - base[k]); printf(" |\n");
  }
  printf("| **all** | %d.%d%% | %d.%d%% | ", pct10(tot0, n_all) / 10, pct10(tot0, n_all) % 10, pct10(tot1, n_all) / 10, pct10(tot1, n_all) % 10);
  pd(pct10(tot1, n_all) - pct10(tot0, n_all)); printf(" |\n\n");
  /* 2. dominance: the same heirs, grouped by chart */
  {
    static const char *const EN[4] = {"fire (HOT)", "earth (STONE)", "air (AIRY)", "water (WET)"};
    static const char *const PN[3] = {"no alignment", "one tier", "optimiser at the cap"};
    uint32_t a0[16], a1[16], c[16];
    int worst = 0, best = 0; char wn[64] = "", bn[64] = "";
    printf("## Does any chart dominate? (same heirs, charts off vs on)\n\n| group | heirs | off | on | delta |\n|---|---|---|---|---|\n");
    for (k = 0; k < 4u * 3u; k++) {
      uint8_t kind = (uint8_t)(k / 4u), v = (uint8_t)(k % 4u);
      uint32_t s0 = 0, s1 = 0, n = 0;
      if (kind == 2 && v > 2) continue;
      for (i = 0; i < n_bred; i++) {
        uint8_t key = kind == 0 ? astro_elem(g_chart[i].sign[SKY_SUN]) : kind == 1 ? astro_elem(g_chart[i].sign[SKY_MARS]) : g_policy[i];
        if (key != v) continue;
        s0 += r0[i]; s1 += r1[i]; n++;
      }
      printf("| %s %s | %u | %.1f%% | %.1f%% | %+.1f |\n", kind == 0 ? "Sun" : kind == 1 ? "Mars" : "", kind == 2 ? PN[v] : EN[v], n,
             n ? s0 / (double)n : 0, n ? s1 / (double)n : 0, n ? (s1 - (double)s0) / n : 0);
    }
    /* every Sun x Mars element pair x policy cell */
    for (k = 0; k < 16; k++) { a0[k] = a1[k] = c[k] = 0; }
    for (i = 0; i < n_bred; i++) {
      uint8_t key = (uint8_t)(astro_elem(g_chart[i].sign[SKY_SUN]) * 4u + astro_elem(g_chart[i].sign[SKY_MARS]));
      a0[key] += r0[i]; a1[key] += r1[i]; c[key]++;
    }
    for (k = 0; k < 16; k++) {
      int dlt = c[k] ? (int)((a1[k] * 10.0 - a0[k] * 10.0) / c[k]) : 0;
      if (dlt > best) { best = dlt; snprintf(bn, sizeof bn, "Sun %s, Mars %s (%u heirs)", EN[k / 4], EN[k % 4], c[k]); }
      if (dlt < worst) { worst = dlt; snprintf(wn, sizeof wn, "Sun %s, Mars %s (%u heirs)", EN[k / 4], EN[k % 4], c[k]); }
    }
    printf("\nAcross the 16 Sun x Mars element pairs: best "); pd(best); printf(" (%s), worst ", bn); pd(worst); printf(" (%s).\n", wn);
    {
      uint32_t t = 0, sh0 = 0, sh1 = 0, nn = 0, m;
      int bh = 0;
      for (i = 0; i < n_bred; i++) { int d2 = (int)r1[i] - (int)r0[i]; if (d2 > bh) bh = d2; t += (uint32_t)(d2 > 5); }
      for (i = 0; i < n_bred; i++) if (g_policy[i] == 2) { sh0 += r0[i]; sh1 += r1[i]; nn++; }
      m = nn;
      printf("Single heirs: the biggest gain is +%d points over %u runs (noise floor at 32 runs is about +-9); %u of %u gain more than 5.\n\n", bh, nr, t, n_bred);
      (void)sh0; (void)sh1; (void)m;
    }
  }
  /* 3. the sky: each state forced on every run (natal on, no transits), vs natal-only */
  {
    static const struct { const char *name; uint16_t s; } ST[] = {
      {"Mercury retrograde", AS_MERC_RX}, {"Venus retrograde", AS_VENUS_RX}, {"Mars retrograde", AS_MARS_RX},
      {"Jupiter station", AS_JUP_STATION}, {"Saturn station", AS_SAT_STATION}, {"solar eclipse", AS_ECL_SUN | AS_NEW},
      {"lunar eclipse", AS_ECL_MOON | AS_FULL}, {"full moon", AS_FULL}, {"new moon", AS_NEW}, {"super full moon", AS_FULL | AS_SUPER},
      {"void-of-course moon", AS_VOC}, {"solstice or equinox", AS_SEASON}, {"solar return (birthday)", AS_SOLAR_RETURN},
      {"lunar return", AS_LUNAR_RETURN}, {"meteor shower", AS_METEOR}, {"worst stack: Mercury Rx + Saturn station + super full moon", AS_MERC_RX | AS_SAT_STATION | AS_FULL | AS_SUPER},
      {"best stack: Jupiter station + birthday + new moon", AS_JUP_STATION | AS_SOLAR_RETURN | AS_NEW}};
    uint32_t tn = tot1;
    printf("## Sky states, each forced on every run (charts on, no transits; vs charts on, no sky: %d.%d%%)\n\n| state | clear | delta | bound |\n|---|---|---|---|\n",
           pct10(tn, n_all) / 10, pct10(tn, n_all) % 10);
    for (k = 0; k < sizeof ST / sizeof ST[0]; k++) {
      uint32_t t;
      int dv;
      g_mode = M_FORCE; g_force = ST[k].s;
      t = eval_astro(nr, 0, ws, ns, 1);
      dv = pct10(t, n_all) - pct10(tn, n_all);
      printf("| %s | %d.%d%% | ", ST[k].name, pct10(t, n_all) / 10, pct10(t, n_all) % 10); pd(dv);
      printf(" | %s |\n", abs(dv) <= 50 ? "ok (<= 5)" : "**over 5**");
    }
    printf("\n");
  }
  /* 4. real moments 2026-2030: every 6 hours, a 1-in-5 heir subsample, the full sky + transits */
  {
    static int dl[NDAYS * 4];
    uint32_t dsel = 0, w, n = 0, s0 = 0, nn = 0, within2 = 0, d;
    int lo = 1000, hi = -1000, sorted[NDAYS * 4];
    uint16_t st_count[15] = {0};
    uint32_t b4[4];
    for (i = 0; i < n_bred; i += 5) nn++;
    g_mode = M_NATAL; g_run_mul = 4;
    for (k = 0; k < 4; k++) { g_run_ofs = k; b4[k] = eval_astro(8, 0, ws, ns, 5); } /* the same 8 runs as each moment */
    (void)s0;
    for (d = 0; d < NDAYS * 4u; d++) {
      uint16_t s = (uint16_t)(astro_states(&g_day[d], 0) | (astro_meteor_on((d / 4u) * 1440u + (d % 4u) * 360u) ? AS_METEOR : 0u));
      for (k = 0; k < 15; k++) if (s & (1u << k)) st_count[k]++;
    }
    g_mode = M_SKY;
    for (d = 0; d < NDAYS * 4u; d += 2) {
      g_day_pin = (int32_t)d; g_run_ofs = (d / 2u) % 4u;
      w = eval_astro(8, 0, ws, ns, 5);
      n = ns[0] + ns[1] + ns[2] + ns[3] + ns[4] + ns[5] + ns[6] + ns[7];
      dl[dsel] = pct10(w, n) - pct10(b4[g_run_ofs], n);
      if (dl[dsel] < lo) lo = dl[dsel];
      if (dl[dsel] > hi) hi = dl[dsel];
      within2 += (uint32_t)(abs(dl[dsel]) <= 20);
      dsel++;
    }
    g_run_mul = 1; g_run_ofs = 0;
    for (d = 0; d < dsel; d++) sorted[d] = dl[d];
    for (d = 1; d < dsel; d++) { int t = sorted[d]; uint32_t e = d; while (e && sorted[e - 1] > t) { sorted[e] = sorted[e - 1]; e--; } sorted[e] = t; }
    printf("## Real moments, 2026-01-01 .. 2030-12-31 every 12 h (%u moments; 200 heirs x 8 runs each, the same 8 runs as its baseline; full sky + transits)\n\n", dsel);
    printf("Clear-rate change against charts-on-no-sky (%u runs a moment, so about +-2.5 points of it is sampling noise):\n\n", nn * 8u);
    printf("| p5 | p25 | median | p75 | p95 | min | max | moments within +-2 |\n|---|---|---|---|---|---|---|---|\n| ");
    pd(sorted[dsel * 5 / 100]); printf(" | "); pd(sorted[dsel / 4]); printf(" | "); pd(sorted[dsel / 2]); printf(" | ");
    pd(sorted[dsel * 3 / 4]); printf(" | "); pd(sorted[dsel * 95 / 100]); printf(" | "); pd(lo); printf(" | "); pd(hi);
    printf(" | %.1f%% |\n\n", within2 * 100.0 / dsel);
    printf("How often each state held (share of all %u six-hour moments): ", NDAYS * 4u);
    {
      static const char *const SN[15] = {"Mercury Rx", "Venus Rx", "Mars Rx", "Jupiter station", "Saturn station", "solar eclipse",
                                         "lunar eclipse", "full moon", "new moon", "super", "void of course", "season day", "", "", "meteor shower"};
      for (k = 0; k < 15; k++) if (SN[k][0]) printf("%s %.1f%%%s", SN[k], st_count[k] * 100.0 / (NDAYS * 4u), k < 14 ? ", " : ".\n\n");
    }
    g_day_pin = -1;
    w = eval_astro(nr, 0, ws, ns, 1);
    printf("All heirs, runs on seeded moments across 2026-2030 (%u runs each): %d.%d%% (charts, no sky %d.%d%%; no charts %d.%d%%).\n\n", nr,
           pct10(w, n_all) / 10, pct10(w, n_all) % 10, pct10(tot1, n_all) / 10, pct10(tot1, n_all) % 10, pct10(tot0, n_all) / 10, pct10(tot0, n_all) % 10);
    printf("| strategy | no charts | charts + sky | delta |\n|---|---|---|---|\n");
    for (k = 0; k < n_strat; k++) {
      printf("| %s | %d.%d%% | %d.%d%% | ", strat_name[k], base[k] / 10, base[k] % 10, pct10(ws[k], ns[k]) / 10, pct10(ws[k], ns[k]) % 10);
      pd(pct10(ws[k], ns[k]) - base[k]); printf(" |\n");
    }
  }
  skyborn_section(nr, tot1, r1);
  sibling_section();
  return 0;
}
