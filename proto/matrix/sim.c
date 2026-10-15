/* Host driver: prints every table and runs the headless simulations as Markdown.
 * Usage: ./mxsim [runs] [build_runs]   (defaults 10000 and 32). Integer only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mx.h"
#include "mx_build.h"

#define PCT10(a, b) ((b) ? (int)(((long long)(a) * 1000 + (b) / 2) / (b)) : 0)
static void pp(int v10) { printf("%d.%d%%", v10 / 10, v10 % 10); }

/* ---------- tiny hash set (static, no malloc) ---------- */
#define HS_BITS 22
#define HS_SIZE (1u << HS_BITS)
static uint64_t hs_tab[HS_SIZE];
static uint32_t hs_n;
static void hs_clear(void) { memset(hs_tab, 0, sizeof hs_tab); hs_n = 0; }
static int hs_add(uint64_t key)
{
  uint64_t k = key + 1, h = (key * 0x9E3779B97F4A7C15ull) >> (64 - HS_BITS);
  while (hs_tab[h]) { if (hs_tab[h] == k) return 0; h = (h + 1) & (HS_SIZE - 1); }
  hs_tab[h] = k; hs_n++;
  return 1;
}
static uint64_t str_hash(const char *s) { uint64_t h = 1469598103934665603ull; while (*s) h = (h ^ (uint8_t)*s++) * 1099511628211ull; return h; }

static uint64_t elite_key(const mx_mob *m)
{
  uint8_t a = m->af[0], b = m->af[1], t;
  if (a != AF_NONE && b != AF_NONE && a > b) { t = a; a = b; b = t; }
  return (uint64_t)mx_mob_id(m) | ((uint64_t)a << 20) | ((uint64_t)b << 28);
}
static uint64_t boss_key(const mx_boss *b)
{
  return (uint64_t)b->core | ((uint64_t)b->atk[0] << 2) | ((uint64_t)b->atk[1] << 6) | ((uint64_t)b->atk[2] << 10) |
         ((uint64_t)b->weak[0] << 14) | ((uint64_t)b->weak[1] << 18) | ((uint64_t)b->weak[2] << 22) |
         ((uint64_t)b->hazard << 26) | ((uint64_t)b->el[0] << 30) | ((uint64_t)b->el[1] << 34);
}

static void tagmask_str(uint16_t m, char *o)
{
  uint8_t i; o[0] = 0;
  for (i = 0; i < MX_TAGS; i++) if (m & TB(i)) { if (o[0]) strcat(o, "+"); strcat(o, mx_tag_name[i]); }
  if (m & M_CRUSH) { if (o[0]) strcat(o, "+"); strcat(o, "CRUSH"); }
  if (m & M_PROJ) { if (o[0]) strcat(o, "+"); strcat(o, "PROJ"); }
  if (m & M_LIGHT) { if (o[0]) strcat(o, "+"); strcat(o, "LIGHT"); }
  if (!o[0]) strcpy(o, "-");
}

/* ---------- 1. schema ---------- */
static void print_schema(void)
{
  uint8_t i;
  printf("## 1. Piece schema\n\n");
  printf("Every piece (weapon base, mod, heir trait, dodge, mob part, affix, boss part, element) is one 5-byte rule.\n\n");
  printf("| byte | bits 7..4 | bits 3..0 |\n|---|---|---|\n");
  printf("| b0 | trigger (%d used) | effect (%d used) |\n", TR_COUNT, EF_COUNT);
  printf("| b1 | tag A (15 = none) | tag B (15 = none) |\n");
  printf("| b2 | magnitude 0..255 (percent-ish units) | |\n");
  printf("| b3 | argA: count / period s / radius | argB: duration (1/4 s) |\n");
  printf("| b4 | kind:3 (bits 7..5) group:3 (4..2) | cap class:2 (1..0; 3 = COST) |\n\n");
  printf("Triggers: ");
  for (i = 0; i < TR_COUNT; i++) printf("%d=%s%s", i, mx_trig_name[i], i + 1 < TR_COUNT ? ", " : "\n\n");
  printf("Effects: ");
  for (i = 0; i < EF_COUNT; i++) printf("%d=%s%s", i, mx_eff_name[i], i + 1 < EF_COUNT ? ", " : "\n\n");
  printf("Kinds: ");
  for (i = 0; i < PK_COUNT; i++) printf("%d=%s%s", i, mx_kind_name[i], i + 1 < PK_COUNT ? ", " : "\n\n");
  printf("Tags: ");
  for (i = 0; i < MX_TAGS; i++) printf("%d=%s%s", i, mx_tag_name[i], i + 1 < MX_TAGS ? ", " : "\n\n");
  printf("Stacking: pieces in the same group are sorted by magnitude and weighted 16,12,8,6,4,3,2,2 /16 "
         "(diminishing), then a soft knee at the group cap (excess counts 1/4) and a hard stop at cap x1.5. "
         "Costs (cap class 3) subtract after stacking and never diminish.\n\n");
  printf("Example encodings (weapon mods):\n\n| piece | bytes | reads as |\n|---|---|---|\n");
  for (i = 0; i < 6; i++) {
    const mx_piece *p = &mx_mods[i].p;
    printf("| %s | %02X %02X %02X %02X %02X | %s -> %s %s mag %d, group %s |\n", mx_mods[i].name,
           p->b[0], p->b[1], p->b[2], p->b[3], p->b[4], mx_trig_name[P_TRIG(p)], P_TAGA(p) < MX_TAGS ? mx_tag_name[P_TAGA(p)] : "",
           mx_eff_name[P_EFF(p)], P_MAG(p), mx_group_name[P_GROUP(p)]);
  }
  printf("\n");
}

static void print_caps(const char *title)
{
  uint8_t i;
  printf("%s: ", title);
  for (i = 0; i < SG_COUNT; i++) printf("%s %d%s", mx_group_name[i], mx_cap[i], i + 1 < SG_COUNT ? ", " : "\n\n");
}

/* ---------- 2. reactions ---------- */
static const char *short_rx(uint8_t rx) { return mx_rx[rx].name; }
static void print_reactions(void)
{
  uint8_t a, b, ok = 1, n = 0;
  printf("## 2. Reaction matrix (one table: combat and tiles)\n\n|  |");
  for (b = 0; b < MX_TAGS; b++) printf(" %s |", mx_tag_name[b]);
  printf("\n|---|");
  for (b = 0; b < MX_TAGS; b++) printf("---|");
  printf("\n");
  for (a = 0; a < MX_TAGS; a++) {
    printf("| **%s** |", mx_tag_name[a]);
    for (b = 0; b < MX_TAGS; b++) {
      printf(" %s |", short_rx(mx_rx_table[a][b]));
      if (mx_rx_table[a][b] != mx_rx_table[b][a]) ok = 0;
    }
    printf("\n");
  }
  printf("\nSymmetric: %s. Plus the context rule: SHINY + any projectile = REFLECT (a shot already reflected splits). "
         "GLITCH + x replays a seeded reaction from x's row (never GLITCH again, so no recursion) and adds a 1-frame stutter.\n\n",
         ok ? "yes" : "NO");
  printf("| reaction | combat | dmg x | ctrl %% | dur s | tile it converts | tile result |\n|---|---|---|---|---|---|---|\n");
  {
    static const char *const kn[RK_COUNT] = { "-", "burst", "damage over time", "blind", "stun", "root", "slow", "split shot",
      "reflect", "summon ally", "armour break", "push", "reveal", "dispel", "nothing (null)", "random", "light", "heal",
      "chain", "confuse", "pull", "wall" };
    for (a = 1; a < RX_COUNT; a++) {
      const mx_reaction *r = &mx_rx[a];
      printf("| %s | %s | %d.%02d | %d | %d.%02d | %s | %s |\n", r->name, kn[r->kind], r->mult / 16, (r->mult % 16) * 100 / 16,
             r->ctrl, r->dur / 4, (r->dur % 4) * 25, mx_tile_name[r->tile_in], r->tile_note);
      n++;
    }
  }
  printf("\n%d reactions over 55 unordered tag pairs.\n\n", n);
  /* the same table on tiles */
  printf("Tile reactions from the same table (tile's intrinsic tag x incoming tag):\n\n| tile | + tag | reaction | becomes |\n|---|---|---|---|\n");
  {
    static const uint8_t demo[][2] = {
      { TL_WATER, MX_COLD }, { TL_WATER, MX_HOT }, { TL_WATER, MX_SHOCK }, { TL_ICE, MX_HOT }, { TL_VINE, MX_HOT },
      { TL_PIT, MX_ALIVE }, { TL_WALL, MX_COLD }, { TL_CRACK, MX_HOT }, { TL_MIRROR, MX_HOT }, { TL_MIRROR, MX_SHOCK },
      { TL_WALL, MX_SHINY }, { TL_NODE, MX_HOT }, { TL_VINE, MX_SHINY }, { TL_RIFT, MX_WET }, { TL_VENT, MX_HOT },
      { TL_CRATE, MX_AIRY }, { TL_FLOOR, MX_STONE }, { TL_WATER, MX_ALIVE } };
    uint8_t i, rx, out;
    for (i = 0; i < sizeof demo / sizeof demo[0]; i++) {
      out = mx_tile_react(demo[i][0], demo[i][1], 7, &rx);
      if (demo[i][0] == TL_PIT && rx == RX_NONE) { /* open pit: ALIVE+WET of a neighbour tile is what bridges it */
        rx = mx_rx_table[MX_ALIVE][MX_WET]; out = mx_rx[rx].tile_out;
      }
      printf("| %s | %s | %s | %s |\n", mx_tile_name[demo[i][0]], mx_tag_name[demo[i][1]], mx_rx[rx].name,
             out == TL_KEEP ? "(same tile, effect only)" : mx_tile_name[out]);
    }
  }
  printf("\n");
}

/* ---------- 3. mob variety by enumeration ---------- */
static uint32_t n_valid_mobs, n_valid_elites, n_valid_elite_single, n_valid_bosses, n_names;
static void enumerate_mobs(void)
{
  mx_mob m;
  uint32_t raw = 0, ok = 0, per_body[BD_COUNT] = { 0 }, per_def[DF_COUNT] = { 0 }, el1 = 0, el2 = 0;
  uint8_t a, b, bi;
  char name[16];
  hs_clear();
  m.af[0] = m.af[1] = AF_NONE;
  for (m.body = 0; m.body < BD_COUNT; m.body++)
    for (m.atk = 0; m.atk < AT_COUNT; m.atk++)
      for (m.def = 0; m.def < DF_COUNT; m.def++)
        for (m.el = 0; m.el < EL_COUNT; m.el++)
          for (m.beh = 0; m.beh < BH_COUNT; m.beh++) {
            raw++;
            if (!mx_mob_valid(&m)) continue;
            ok++; per_body[m.body]++; per_def[m.def]++;
            for (bi = 0; bi < BI_COUNT; bi++) { mx_mob_name(&m, bi, name); hs_add(str_hash(name)); }
            for (a = 0; a < AF_COUNT; a++) {
              m.af[0] = a; m.af[1] = AF_NONE;
              if (mx_elite_valid(&m)) el1++;
              for (b = (uint8_t)(a + 1); b < AF_COUNT; b++) { m.af[1] = b; if (mx_elite_valid(&m)) el2++; }
            }
            m.af[0] = m.af[1] = AF_NONE;
          }
  n_names = hs_n;
  n_valid_mobs = ok; n_valid_elite_single = el1; n_valid_elites = el1 + el2;
  printf("## 3. Mob composer\n\n");
  printf("Axes: %d bodies x %d attacks x %d defences/kill rules x %d elements (10 tags + untagged) x %d behaviours = %u raw combos.\n\n",
         BD_COUNT, AT_COUNT, DF_COUNT, EL_COUNT, BH_COUNT, raw);
  printf("**Valid after compatibility rules: %u (%d.%d%% of raw).** Elite variants (1 affix): %u; (2 affixes): %u; "
         "total elite builds: %u.\n\n", ok, PCT10(ok, raw) / 10, PCT10(ok, raw) % 10, el1, el2, el1 + el2);
  printf("Distinct generated names over all valid mobs x 8 biomes: %u (names are short CRUCIBLE-voice pairs, so they repeat "
         "across builds by design; the build, not the name, is the identity).\n\n", n_names);
  printf("| body | valid builds | | defence | valid builds | rule (means) |\n|---|---|---|---|---|---|\n");
  for (a = 0; a < BD_COUNT || a < DF_COUNT; a++) {
    if (a < BD_COUNT) printf("| %s | %u | |", mx_body_name[a], per_body[a]); else printf("| | | |");
    if (a < DF_COUNT) printf(" %s | %u | %s |\n", mx_def_name[a], per_def[a], mx_def_rule[a]); else printf(" | | |\n");
  }
  printf("\nAttack tells (frames @60 Hz; minimum readable = 12; hasted x0.75 must stay >= 12): ");
  for (a = 0; a < AT_COUNT; a++) printf("%s %d%s%s", mx_atk_name[a], mx_atk_tell[a], mx_atk_lit[a] ? "" : " (unlit)", a + 1 < AT_COUNT ? ", " : ".\n\n");
  printf("Behaviours: ");
  for (a = 0; a < BH_COUNT; a++) printf("%s%s", mx_beh_name[a], a + 1 < BH_COUNT ? ", " : ".\n\n");
  printf("Elite affixes (max 2, max 1 aura):\n\n| affix | rule |\n|---|---|\n");
  for (a = 0; a < AF_AURA0; a++) printf("| %s | %s |\n", mx_af_name[a], mx_af_rule[a]);
  printf("| element aura (x10) | not its own element; never one that solves or contradicts its kill rule (e.g. no WET aura on wet-then-shock, no HOT aura on reformers, no SHINY aura on dark-invisible); never GROUND-cancelling |\n");
  printf("| pairs | no vampiric+shielded, hasted+teleport-on-hit, splitting+vampiric |\n\n");
  /* a few sample names */
  {
    mx_rng r; mx_mob s; uint8_t k = 0; mx_seed(&r, 77);
    printf("Sample generated mobs:\n\n| name | biome | body | attack | defence | element | behaviour |\n|---|---|---|---|---|---|---|\n");
    while (k < 12) {
      s.body = mx_below(&r, BD_COUNT); s.atk = mx_below(&r, AT_COUNT); s.def = mx_below(&r, DF_COUNT);
      s.el = mx_below(&r, EL_COUNT); s.beh = mx_below(&r, BH_COUNT); s.af[0] = s.af[1] = AF_NONE;
      if (!mx_mob_valid(&s)) continue;
      bi = (uint8_t)(k % BI_COUNT);
      mx_mob_name(&s, bi, name);
      printf("| %s | %s | %s | %s | %s | %s | %s |\n", name, mx_biome_name[bi], mx_body_name[s.body], mx_atk_name[s.atk],
             mx_def_name[s.def], s.el < MX_TAGS ? mx_tag_name[s.el] : "-", mx_beh_name[s.beh]);
      k++;
    }
    printf("\n");
  }
}

static void enumerate_bosses(void)
{
  mx_boss b;
  uint32_t ok = 0, raw = 0, per_core[BC_COUNT] = { 0 };
  uint8_t i;
  for (b.core = 0; b.core < BC_COUNT; b.core++)
    for (b.atk[0] = 0; b.atk[0] < AT_COUNT; b.atk[0]++)
      for (b.atk[1] = 0; b.atk[1] < AT_COUNT; b.atk[1]++) {
        if (b.atk[1] == b.atk[0]) continue;
        for (b.atk[2] = 0; b.atk[2] < AT_COUNT; b.atk[2]++) {
          if (b.atk[2] == b.atk[0] || b.atk[2] == b.atk[1]) continue;
          for (b.weak[0] = 0; b.weak[0] < DF_COUNT; b.weak[0]++)
            for (b.weak[1] = 0; b.weak[1] < DF_COUNT; b.weak[1]++) {
              if (b.weak[1] == b.weak[0]) continue;
              for (b.weak[2] = 0; b.weak[2] < DF_COUNT; b.weak[2]++) {
                if (b.weak[2] == b.weak[0] || b.weak[2] == b.weak[1]) continue;
                for (b.hazard = 0; b.hazard < HZ_COUNT; b.hazard++)
                  for (b.el[0] = 0; b.el[0] < MX_TAGS; b.el[0]++)
                    for (b.el[1] = 0; b.el[1] < MX_TAGS; b.el[1]++) {
                      raw++;
                      if (mx_boss_valid(&b)) { ok++; per_core[b.core]++; }
                    }
              }
            }
        }
      }
  n_valid_bosses = ok;
  printf("### Boss templates\n\nParts: core (%d) x 3 ordered phase attacks (distinct) x 3 ordered phase weak points (distinct; "
         "plain/splits not allowed) x arena hazard (%d) x element pair (ordered, primary first). Raw (after distinctness): %u. "
         "**Valid: %u.** Per core: ", BC_COUNT, HZ_COUNT, raw, ok);
  for (i = 0; i < BC_COUNT; i++) printf("%s %u%s", mx_core_name[i], per_core[i], i + 1 < BC_COUNT ? ", " : ".\n\n");
  printf("Rules: each phase's tell x(8-phase)/8 >= 12 f; each phase must be a valid mob for the core's body class; HIVE needs a summon "
         "phase; ENGINE never lunges; no GROUND-cancelling element pair; dark weak point only with the darkness hazard; no crush "
         "weak point in flooding arenas; no COLD element over rising lava. Mid-bosses use the first 2 phases.\n\n");
}

/* ---------- 4/5. run simulations ---------- */
static mx_run g_run;
static int8_t rnd8(mx_rng *r, int8_t lo, int8_t hi) { return (int8_t)(lo + (int8_t)mx_below(r, (uint8_t)(hi - lo + 1))); }

static void run_world_sims(uint32_t runs)
{
  uint32_t i, s, k;
  mx_rng r;
  int8_t stand[FC_COUNT], lean[LN_COUNT];
  /* variety */
  uint64_t picks = 0, repeats = 0, distinct_sum = 0, e_picks = 0, e_rep = 0, e_distinct_sum = 0, b_picks = 0, b_rep = 0;
  uint32_t dmin = 999, dmax = 0, emin = 999, emax = 0;
  static uint8_t base_seen[1u << 17];
  uint32_t base_cov = 0;
  /* fairness */
  uint64_t fstages = 0, fneed = 0, fail_before = 0, fail_before_kit = 0, fail_after = 0, adds_tiles = 0, adds_pick = 0;
  uint64_t miss_bit[13] = { 0 };
  /* decks */
  uint64_t type_count[ST_COUNT] = { 0 }, total_stages = 0, b2b = 0, near3 = 0, trick_rep = 0, trick_forced = 0, trick_stages = 0;
  uint64_t dsum[RUN_FLOORS] = { 0 }, dn[RUN_FLOORS] = { 0 }, gsum[10] = { 0 }, gn[10] = { 0 }, xsum[RUN_FLOORS] = { 0 }, xn[RUN_FLOORS] = { 0 };
  uint64_t conv = 0, bfall = 0, main_viol = 0;
  /* treasure */
  uint64_t t_n = 0, t_tier[4] = { 0 }, t_kind[TK_COUNT] = { 0 }, t_gate[TG_COUNT] = { 0 }, t_hidden = 0, t_hinted = 0, t_unreach_b = 0, t_unreach = 0;
  uint64_t t_spot[SP_COUNT] = { 0 }, t_cost_tier[4] = { 0 }, t_flag[4] = { 0 };
  int64_t sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
  uint32_t t_run_min = 999, t_run_max = 0;
  uint32_t dmn[RUN_FLOORS], dmx[RUN_FLOORS];
  uint64_t biome_at[RUN_BIOMES][BI_COUNT];
  uint64_t boss_fail = 0, mod_count[MD_COUNT] = { 0 };
  /* coupling */
  uint64_t ch_hate_tag = 0, ch_hate_all = 0, ch_like_tag = 0, ch_like_all = 0;
  uint64_t dream_lean_runs = 0, dream_lean_hit = 0, other_runs = 0, other_hit = 0;
  uint64_t watch_like_heist = 0, watch_like_st = 0, watch_hate_heist = 0, watch_hate_st = 0;
  uint32_t det_a = 0, det_b = 0;
  uint64_t hk_elite;
  memset(biome_at, 0, sizeof biome_at);
  for (k = 0; k < RUN_FLOORS; k++) { dmn[k] = 65535; dmx[k] = 0; }
  memset(base_seen, 0, sizeof base_seen);
  hs_clear();
  for (i = 0; i < runs; i++) {
    uint32_t seed = 0x9E37u * (i + 1) + 12345u;
    uint64_t run_ids[192]; uint32_t nid = 0;
    uint64_t run_el[64]; uint32_t nel = 0;
    uint64_t run_boss[8]; uint32_t nb = 0;
    uint32_t distinct = 0, edistinct = 0;
    uint16_t kit;
    uint8_t lean_pick;
    mx_seed(&r, seed ^ 0xA5A5u);
    for (k = 0; k < FC_COUNT; k++) stand[k] = rnd8(&r, -60, 60);
    for (k = 0; k < LN_COUNT; k++) lean[k] = rnd8(&r, 0, 10);
    lean_pick = mx_below(&r, LN_COUNT); lean[lean_pick] = rnd8(&r, 30, 70);
    /* a random kit for the "player kit covers it" fairness view */
    { mx_build bb; mx_bstats bs;
      bb.base = mx_below(&r, WB_COUNT); bb.mod[0] = mx_below(&r, N_MODS); bb.mod[1] = mx_below(&r, N_MODS);
      bb.trait[0] = mx_below(&r, N_TRAITS); bb.trait[1] = mx_below(&r, N_TRAITS); bb.dodge = mx_below(&r, DG_COUNT);
      mx_build_stats(&bb, &bs);
      kit = bs.tags; if (bs.push) kit |= M_CRUSH; if (bs.tags & (TB(MX_HOT) | TB(MX_SHINY))) kit |= M_LIGHT; }
    mx_run_generate(&g_run, seed, (uint8_t)(i % 10), stand, lean);
    if (i == 0) { /* determinism: generate twice and hash */
      uint32_t h = 0; for (s = 0; s < g_run.nstages; s++) h = h * 31u + g_run.st[s].type * 7u + g_run.st[s].diff + g_run.st[s].need;
      det_a = h;
      mx_run_generate(&g_run, seed, (uint8_t)(i % 10), stand, lean);
      h = 0; for (s = 0; s < g_run.nstages; s++) h = h * 31u + g_run.st[s].type * 7u + g_run.st[s].diff + g_run.st[s].need;
      det_b = h;
    }
    trick_forced += g_run.trick_forced; conv += g_run.trick_converted; bfall += g_run.boss_fallbacks;
    { uint32_t tr_run = 0;
      for (s = 0; s < g_run.nstages; s++) {
        const mx_stage *st = &g_run.st[s];
        for (k = 0; k < st->ntreasure; k++) {
          const mx_treasure *t = &st->tr[k];
          uint8_t f;
          t_n++; tr_run++; t_tier[t->tier]++; t_kind[t->kind]++; t_gate[t->gate]++; t_spot[t->spot]++;
          t_cost_tier[t->tier] += t->cost;
          if (t->spot >= SP_FIRST_HIDDEN) { t_hidden++; t_hinted += t->hinted; }
          if (!t->ok_before) t_unreach_b++;
          if (!t->ok) t_unreach++;
          for (f = 0; f < 4; f++) if (t->flags & (1u << f)) t_flag[f]++;
          sx += t->cost; sy += t->tier; sxx += (int64_t)t->cost * t->cost; syy += (int64_t)t->tier * t->tier; sxy += (int64_t)t->cost * t->tier;
        }
      }
      if (tr_run < t_run_min) t_run_min = tr_run;
      if (tr_run > t_run_max) t_run_max = tr_run;
    }
    for (k = 0; k < RUN_BIOMES; k++) biome_at[k][g_run.order[k]]++;
    {
      uint8_t has_dream = 0;
      for (k = 0; k < RUN_BIOMES; k++) if (g_run.order[k] == BI_DREAM) has_dream = 1;
      if (lean_pick == LN_DREAM) { dream_lean_runs++; dream_lean_hit += has_dream; } else { other_runs++; other_hit += has_dream; }
    }
    for (s = 0; s < g_run.nstages; s++) {
      const mx_stage *st = &g_run.st[s];
      total_stages++; type_count[st->type]++; mod_count[st->modifier]++;
      if (s && g_run.st[s - 1].type == st->type) b2b++;
      if (s >= 2 && g_run.st[s - 2].type == st->type) near3++;
      if (st->trick < 200) {
        uint32_t back, seen_t = 0, c = 0;
        trick_stages++;
        for (back = s; back > 0 && c < 6; back--) {
          const mx_stage *p = &g_run.st[back - 1];
          if (p->trick < 200) { c++; if (p->trick == st->trick) seen_t = 1; }
        }
        trick_rep += seen_t;
      }
      if (st->type == ST_HEIST || st->type < ST_COUNT) {
        if (stand[FC_WATCH] > 30) { watch_like_st++; watch_like_heist += st->type == ST_HEIST; }
        if (stand[FC_WATCH] < -30) { watch_hate_st++; watch_hate_heist += st->type == ST_HEIST; }
      }
      if (st->type != ST_REST) {
        uint16_t miss;
        fstages++;
        dsum[st->floor] += st->diff; dn[st->floor]++;
        if (!st->phases) { xsum[st->floor] += st->diff; xn[st->floor]++; }
        if (st->need & ~st->have_main) main_viol++;
        if (st->diff < dmn[st->floor]) dmn[st->floor] = st->diff;
        if (st->diff > dmx[st->floor]) dmx[st->floor] = st->diff;
        gsum[g_run.gen] += st->diff; gn[g_run.gen]++;
        if (st->need) fneed++;
        miss = (uint16_t)(st->need & ~st->have_before);
        if (miss) { uint8_t bt; fail_before++; for (bt = 0; bt < 13; bt++) if (miss & (1u << bt)) miss_bit[bt]++; }
        if (st->need & ~(st->have_before | kit)) fail_before_kit++;
        if (st->need & ~st->have) fail_after++;
        if (!st->boss_ok) boss_fail++;
        adds_tiles += st->added_tiles;
        { uint8_t bt; for (bt = 0; bt < 13; bt++) if (st->pickup & (1u << bt)) adds_pick++; }
      }
      for (k = 0; k < st->nmobs; k++) {
        const mx_mob *m = &st->mob[k];
        uint64_t id = mx_mob_id(m);
        uint32_t q, seen_b = 0;
        picks++;
        for (q = 0; q < nid; q++) if (run_ids[q] == id) { seen_b = 1; break; }
        if (seen_b) repeats++; else { if (nid < 192) run_ids[nid++] = id; distinct++; }
        if (!base_seen[id]) { base_seen[id] = 1; base_cov++; }
        if (m->el < MX_TAGS) {
          uint8_t charge_tag = (uint8_t)(m->el == MX_SHOCK || m->el == MX_HOT);
          if (stand[FC_CHARGE] < -30) { ch_hate_all++; ch_hate_tag += charge_tag; }
          if (stand[FC_CHARGE] > 30) { ch_like_all++; ch_like_tag += charge_tag; }
        } else {
          if (stand[FC_CHARGE] < -30) ch_hate_all++;
          if (stand[FC_CHARGE] > 30) ch_like_all++;
        }
        if (m->af[0] != AF_NONE) {
          uint64_t ek = elite_key(m);
          e_picks++;
          seen_b = 0;
          for (q = 0; q < nel; q++) if (run_el[q] == ek) { seen_b = 1; break; }
          if (seen_b) e_rep++; else { if (nel < 64) run_el[nel++] = ek; edistinct++; }
        }
      }
      if (st->phases) {
        uint64_t bk = boss_key(&st->boss);
        uint32_t q, seen_b = 0;
        b_picks++;
        for (q = 0; q < nb; q++) if (run_boss[q] == bk) seen_b = 1;
        if (seen_b) b_rep++; else if (nb < 8) run_boss[nb++] = bk;
        hs_add(bk | (1ull << 62));
      }
    }
    distinct_sum += distinct; e_distinct_sum += edistinct;
    if (distinct < dmin) dmin = distinct;
    if (distinct > dmax) dmax = distinct;
    if (edistinct < emin) emin = edistinct;
    if (edistinct > emax) emax = edistinct;
    for (s = 0; s < g_run.nstages; s++) for (k = 0; k < g_run.st[s].nmobs; k++)
      if (g_run.st[s].mob[k].af[0] != AF_NONE) { hk_elite = elite_key(&g_run.st[s].mob[k]); hs_add(hk_elite); }
  }
  {
    uint32_t boss_and_elite_distinct = hs_n; /* combined set; split below by re-counting bosses */
    (void)boss_and_elite_distinct;
  }
  printf("## 4. Headless simulation: %u seeded runs (heir generation = run %% 10, random starting faction standing, "
         "one random story leaning)\n\n", runs);
  printf("Determinism: same seed generated twice -> hash %08X / %08X (%s).\n\n", det_a, det_b, det_a == det_b ? "identical" : "DIFFERENT");
  printf("### 4a. Variety\n\n| | per run (mean) | min | max | repeat rate within a run | distinct across all runs | possible |\n|---|---|---|---|---|---|---|\n");
  printf("| mob builds (base) | %llu.%llu distinct of %llu.%llu picks | %u | %u | ", (unsigned long long)(distinct_sum * 10 / runs / 10),
         (unsigned long long)(distinct_sum * 10 / runs % 10), (unsigned long long)(picks * 10 / runs / 10), (unsigned long long)(picks * 10 / runs % 10), dmin, dmax);
  pp(PCT10(repeats, picks)); printf(" | %u (", base_cov); pp(PCT10(base_cov, n_valid_mobs)); printf(" coverage) | %u |\n", n_valid_mobs);
  printf("| elites | %llu.%llu distinct of %llu.%llu elite picks | %u | %u | ", (unsigned long long)(e_distinct_sum * 10 / runs / 10),
         (unsigned long long)(e_distinct_sum * 10 / runs % 10), (unsigned long long)(e_picks * 10 / runs / 10), (unsigned long long)(e_picks * 10 / runs % 10), emin, emax);
  pp(PCT10(e_rep, e_picks)); printf(" | (see below) | %u |\n", n_valid_elites);
  printf("| bosses + mid-bosses | %llu.%llu per run | | | ", (unsigned long long)(b_picks * 10 / runs / 10), (unsigned long long)(b_picks * 10 / runs % 10));
  pp(PCT10(b_rep, b_picks)); printf(" | | %u |\n\n", n_valid_bosses);
  {
    /* recount distinct elites and bosses separately */
    uint32_t ne, nbo;
    hs_clear();
    for (i = 0; i < runs; i++) {
      uint32_t seed = 0x9E37u * (i + 1) + 12345u;
      mx_seed(&r, seed ^ 0xA5A5u);
      for (k = 0; k < FC_COUNT; k++) stand[k] = rnd8(&r, -60, 60);
      for (k = 0; k < LN_COUNT; k++) lean[k] = rnd8(&r, 0, 10);
      k = mx_below(&r, LN_COUNT); lean[k] = rnd8(&r, 30, 70);
      mx_run_generate(&g_run, seed, (uint8_t)(i % 10), stand, lean);
      for (s = 0; s < g_run.nstages; s++) for (k = 0; k < g_run.st[s].nmobs; k++)
        if (g_run.st[s].mob[k].af[0] != AF_NONE) hs_add(elite_key(&g_run.st[s].mob[k]));
    }
    ne = hs_n;
    hs_clear();
    for (i = 0; i < runs; i++) {
      uint32_t seed = 0x9E37u * (i + 1) + 12345u;
      mx_seed(&r, seed ^ 0xA5A5u);
      for (k = 0; k < FC_COUNT; k++) stand[k] = rnd8(&r, -60, 60);
      for (k = 0; k < LN_COUNT; k++) lean[k] = rnd8(&r, 0, 10);
      k = mx_below(&r, LN_COUNT); lean[k] = rnd8(&r, 30, 70);
      mx_run_generate(&g_run, seed, (uint8_t)(i % 10), stand, lean);
      for (s = 0; s < g_run.nstages; s++) if (g_run.st[s].phases) hs_add(boss_key(&g_run.st[s].boss));
    }
    nbo = hs_n;
    printf("Distinct elites seen across all runs: %u; distinct boss/mid-boss templates seen: %u. ", ne, nbo);
    printf("Mob generator rejection rate (invalid combos resampled): "); pp(PCT10(mx_stat_mob_rejects, mx_stat_mob_tries));
    printf(" of %u draws.\n\n", mx_stat_mob_tries);
  }
  printf("### 4b. Fairness (kill/win means present in the stage)\n\n");
  printf("| | stages | %% of non-rest stages |\n|---|---|---|\n");
  printf("| stages needing a specific means | %llu | ", (unsigned long long)fneed); pp(PCT10(fneed, fstages)); printf(" |\n");
  printf("| FAIL before guarantee (room only) | %llu | ", (unsigned long long)fail_before); pp(PCT10(fail_before, fstages)); printf(" |\n");
  printf("| FAIL before guarantee (room + a random player kit) | %llu | ", (unsigned long long)fail_before_kit); pp(PCT10(fail_before_kit, fstages)); printf(" |\n");
  printf("| **FAIL after guarantee** | **%llu** | ", (unsigned long long)fail_after); pp(PCT10(fail_after, fstages)); printf(" |\n");
  printf("| boss rooms without a valid template | %llu | |\n", (unsigned long long)boss_fail);
  printf("| main path needing a means only treasure placement added | %llu | |\n\n", (unsigned long long)main_viol);
  printf("Guarantee step placed %llu source tiles and %llu pickups (MAGIC has no tile, so it is always a pickup). "
         "Missing means before the guarantee, by kind: ", (unsigned long long)adds_tiles, (unsigned long long)adds_pick);
  {
    static const char *const bn[13] = { "HOT", "COLD", "WET", "SHOCK", "SHINY", "ALIVE", "MAGIC", "GLITCH", "STONE", "AIRY", "CRUSH", "PROJ", "LIGHT" };
    uint8_t bt; for (bt = 0; bt < 13; bt++) if (miss_bit[bt]) printf("%s %llu, ", bn[bt], (unsigned long long)miss_bit[bt]);
  }
  printf("(stages can miss several).\n\n");
  printf("### 4c. Stage decks\n\n%u stages per run on average (10 floors x 4 + 9 landings + optional rifts).\n\n",
         (unsigned)(total_stages / runs));
  printf("| stage type | per run | share | win condition |\n|---|---|---|---|\n");
  for (k = 0; k < ST_COUNT; k++) {
    printf("| %s | %llu.%02llu | ", mx_stage_name[k], (unsigned long long)(type_count[k] * 100 / runs / 100), (unsigned long long)(type_count[k] * 100 / runs % 100));
    pp(PCT10(type_count[k], total_stages)); printf(" | %s |\n", mx_stage_win[k]);
  }
  printf("\nBack-to-back repeats of a stage type: **%llu**. Same type two stages apart: %llu (", (unsigned long long)b2b, (unsigned long long)near3);
  pp(PCT10(near3, total_stages));
  printf("). Trick (the room's key reaction / recipe / dark-room method) repeated within the last 6 trick rooms: %llu of %llu (",
         (unsigned long long)trick_rep, (unsigned long long)trick_stages);
  pp(PCT10(trick_rep, trick_stages));
  printf("). When every method for a DARK/MIX/RIFT room was recent, the room became a puzzle room instead: %llu times; "
         "still forced to repeat: %llu.\n\n", (unsigned long long)conv, (unsigned long long)trick_forced);
  printf("Boss generator: %llu templates came from the deterministic fallback walk (rejection sampling gave up after 2000 draws); 0 invalid.\n\n",
         (unsigned long long)bfall);
  printf("Modifiers: ");
  for (k = 0; k < MD_COUNT; k++) { printf("%s ", mx_md_name[k]); pp(PCT10(mod_count[k], total_stages)); printf("%s", k + 1 < MD_COUNT ? ", " : ".\n\n"); }
  printf("Difficulty score = solver-steps estimate x3 + enemy budget spent + conditions x4 (rests excluded).\n\n");
  printf("| floor (depth) | mean | min | max | mean without boss/mid-boss rooms |\n|---|---|---|---|---|\n");
  for (k = 0; k < RUN_FLOORS; k++)
    printf("| %u | %llu | %u | %u | %llu |\n", (unsigned)k + 1, (unsigned long long)(dn[k] ? dsum[k] / dn[k] : 0), dmn[k], dmx[k],
           (unsigned long long)(xn[k] ? xsum[k] / xn[k] : 0));
  printf("\n(Even floors end in the biome's mid-boss, which is the saw-tooth in the first column; the room curve without it rises every floor.)\n");
  printf("\n| heir generation | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |\n|---|---|---|---|---|---|---|---|---|---|---|\n| mean difficulty |");
  for (k = 0; k < 10; k++) printf(" %llu |", (unsigned long long)(gn[k] ? gsum[k] / gn[k] : 0));
  printf("\n\nCompletability proxy: every stage's required means are present after the guarantee (above). Puzzle rooms are "
         "only checked for the means of their trick reaction here; the real BFS solver in host/eg_solve.c still has to verify "
         "each generated grid at generation time.\n\n");
  printf("Biome run order (OFFICE first, GLITCH CORE last, middle three drawn by story leaning and grudges):\n\n| slot |");
  for (k = 0; k < BI_COUNT; k++) printf(" %s |", mx_biome_name[k]);
  printf("\n|---|"); for (k = 0; k < BI_COUNT; k++) printf("---|"); printf("\n");
  for (s = 0; s < RUN_BIOMES; s++) {
    printf("| %u |", (unsigned)s + 1);
    for (k = 0; k < BI_COUNT; k++) { pp(PCT10(biome_at[s][k], runs)); printf(" |"); }
    printf("\n");
  }
  printf("\n### 4d. Treasure placement\n\nPlaced after the main path from its own RNG stream (seeded from the stage, so the main path's "
         "generation is byte-identical with or without treasure). Budget per stage = 2 + floor/2 + type bonus (rest = 1); a patch costs tier+1.\n\n");
  printf("Patches: %llu.%02llu per stage, %llu.%llu per run (min %u, max %u). Hidden: ", (unsigned long long)(t_n * 100 / total_stages / 100),
         (unsigned long long)(t_n * 100 / total_stages % 100), (unsigned long long)(t_n * 10 / runs / 10), (unsigned long long)(t_n * 10 / runs % 10), t_run_min, t_run_max);
  pp(PCT10(t_hidden, t_n)); printf(" (of those hinted "); pp(PCT10(t_hinted, t_hidden)); printf("). Trait-gated: ");
  pp(PCT10(t_n - t_gate[0], t_n)); printf(" (");
  for (k = 1; k < TG_COUNT; k++) { printf("%s ", mx_tg_name[k]); pp(PCT10(t_gate[k], t_n)); printf("%s", k + 1 < TG_COUNT ? ", " : ").\n\n"); }
  printf("| tier | share | mean reach cost |\n|---|---|---|\n");
  for (k = 0; k < 4; k++) { printf("| T%u | ", (unsigned)k); pp(PCT10(t_tier[k], t_n)); printf(" | %llu |\n", (unsigned long long)(t_tier[k] ? t_cost_tier[k] / t_tier[k] : 0)); }
  {
    int64_t n = (int64_t)t_n, cov = n * sxy - sx * sy, vx = n * sxx - sx * sx, vy = n * syy - sy * sy;
    int64_t den = 0, lo = 0, hi = 3037000499LL, prod;
    /* integer sqrt of vx*vy via sqrt(vx)*sqrt(vy) */
    int64_t a = vx, b2 = vy, ra, rb;
    lo = 0; hi = 3037000499LL; while (lo < hi) { int64_t m = (lo + hi + 1) / 2; if (m * m <= a) lo = m; else hi = m - 1; } ra = lo;
    lo = 0; hi = 3037000499LL; while (lo < hi) { int64_t m = (lo + hi + 1) / 2; if (m * m <= b2) lo = m; else hi = m - 1; } rb = lo;
    den = ra * rb; prod = den ? cov * 1000 / den : 0;
    printf("\nReach cost vs reward tier: Pearson r = 0.%03lld (tier is a step function of cost, so r < 1 only from the step edges).\n\n", (long long)prod);
  }
  printf("| kind | share | | spot | share |\n|---|---|---|---|---|\n");
  for (k = 0; k < SP_COUNT; k++) {
    if (k < TK_COUNT) { printf("| %s | ", mx_tk_name[k]); pp(PCT10(t_kind[k], t_n)); printf(" |"); } else printf("| | |");
    printf(" | %s | ", mx_sp_name[k]); pp(PCT10(t_spot[k], t_n)); printf(" |\n");
  }
  printf("\nNeeds: recipe "); pp(PCT10(t_flag[0], t_n)); printf(", reaction "); pp(PCT10(t_flag[1], t_n)); printf(", collapse ");
  pp(PCT10(t_flag[2], t_n)); printf(", timed "); pp(PCT10(t_flag[3], t_n));
  printf(". **Unreachable before the placement guarantee: %llu (", (unsigned long long)t_unreach_b); pp(PCT10(t_unreach_b, t_n));
  printf("); after: %llu.** Gated patches count as reachable with their trait; the means check is the same proxy as 4b.\n", (unsigned long long)t_unreach);
  printf("\n### Coupling (CHAT/truth -> WORLD/FUNCTION)\n\n");
  printf("- CHARGE hated (< -30): "); pp(PCT10(ch_hate_tag, ch_hate_all)); printf(" of enemies are SHOCK/HOT; CHARGE liked (> 30): ");
  pp(PCT10(ch_like_tag, ch_like_all)); printf(".\n");
  printf("- WATCH liked: heists are "); pp(PCT10(watch_like_heist, watch_like_st)); printf(" of stages; WATCH hated: ");
  pp(PCT10(watch_hate_heist, watch_hate_st)); printf(".\n");
  printf("- DREAM leaning runs visit DREAM GARDEN "); pp(PCT10(dream_lean_hit, dream_lean_runs)); printf(" of the time; other runs ");
  pp(PCT10(other_hit, other_runs)); printf(".\n\n");
}

/* ---------- 5d. builds ---------- */
#define BRUNS_MAX 64
static mx_run b_runs[BRUNS_MAX];
static uint32_t nbr = 32;

static void make_build_runs(void)
{
  uint32_t i, k;
  mx_rng r;
  int8_t stand[FC_COUNT], lean[LN_COUNT];
  for (i = 0; i < nbr; i++) {
    mx_seed(&r, 4242u + i * 7u);
    for (k = 0; k < FC_COUNT; k++) stand[k] = rnd8(&r, -60, 60);
    for (k = 0; k < LN_COUNT; k++) lean[k] = rnd8(&r, 0, 30);
    mx_run_generate(&b_runs[i], 0xC0FFEEu + i * 977u, (uint8_t)(i % 10), stand, lean);
  }
}

static uint32_t build_index_count(void) { return (uint32_t)WB_COUNT * 153u * 91u * DG_COUNT; }
static void build_from_index(uint32_t idx, mx_build *b)
{
  uint32_t mp = idx % 153u, tp, i, a, c;
  idx /= 153u; tp = idx % 91u; idx /= 91u;
  b->dodge = (uint8_t)(idx % DG_COUNT); b->base = (uint8_t)(idx / DG_COUNT);
  c = 0;
  for (a = 0; a < N_MODS; a++) for (i = a + 1; i < N_MODS; i++) { if (c == mp) { b->mod[0] = (uint8_t)a; b->mod[1] = (uint8_t)i; } c++; }
  c = 0;
  for (a = 0; a < N_TRAITS; a++) for (i = a + 1; i < N_TRAITS; i++) { if (c == tp) { b->trait[0] = (uint8_t)a; b->trait[1] = (uint8_t)i; } c++; }
}

typedef struct { uint64_t win[ARCH_COUNT], n[ARCH_COUNT], wb_win[WB_COUNT], wb_n[WB_COUNT], dg_win[DG_COUNT], dg_n[DG_COUNT];
                 uint64_t mod_win[N_MODS], mod_n[N_MODS], tr_win[N_TRAITS], tr_n[N_TRAITS], grp[ARCH_COUNT][SG_COUNT];
                 uint64_t mono_win, mono_n, hyb_win, hyb_n;
                 uint64_t win_all, n_all; uint32_t hist[11]; uint64_t death_floor[RUN_FLOORS + 1]; } bstat;
static bstat B;
static uint8_t g_rate[WB_COUNT * 153 * 91 * DG_COUNT];
static uint64_t g_pw[N_MODS][N_MODS], g_pn[N_MODS][N_MODS];
static uint16_t g_K;

static void eval_builds(uint32_t stride, uint16_t K, uint8_t full)
{
  uint32_t idx, total = build_index_count(), j, w;
  mx_build b; mx_bstats s; uint8_t d, i;
  memset(&B, 0, sizeof B);
  if (full) { memset(g_pw, 0, sizeof g_pw); memset(g_pn, 0, sizeof g_pn); }
  for (idx = 0; idx < total; idx += stride) {
    build_from_index(idx, &b);
    mx_build_stats(&b, &s);
    w = 0;
    for (j = 0; j < nbr; j++) {
      uint8_t ok = mx_sim_run(&b, &s, &b_runs[j], idx * 31u + j, K, &d);
      w += ok;
      if (full) B.death_floor[d]++;
    }
    if (full) { g_rate[idx] = (uint8_t)(w * 100 / nbr); g_pw[b.mod[0]][b.mod[1]] += w; g_pn[b.mod[0]][b.mod[1]] += nbr; }
    B.win[s.arch] += w; B.n[s.arch] += nbr;
    B.wb_win[b.base] += w; B.wb_n[b.base] += nbr;
    B.dg_win[b.dodge] += w; B.dg_n[b.dodge] += nbr;
    for (i = 0; i < 2; i++) { B.mod_win[b.mod[i]] += w; B.mod_n[b.mod[i]] += nbr; B.tr_win[b.trait[i]] += w; B.tr_n[b.trait[i]] += nbr; }
    for (i = 0; i < SG_COUNT - 1; i++) B.grp[s.arch][i] += s.grp[i];
    B.win_all += w; B.n_all += nbr;
    if (s.mono) { B.mono_win += w; B.mono_n += nbr; } else if (s.arch != ARCH_PLAIN) { B.hyb_win += w; B.hyb_n += nbr; }
    B.hist[w * 10 / nbr]++;
  }
}

static uint16_t calibrate(uint32_t stride, int target10)
{
  uint16_t lo = 1, hi = 4000, mid;
  while (lo < hi) {
    mid = (uint16_t)((lo + hi + 1) / 2);
    eval_builds(stride, mid, 0);
    if (PCT10(B.win_all, B.n_all) >= target10) lo = mid; else hi = (uint16_t)(mid - 1);
  }
  return lo;
}

static void print_arch_table(const char *title)
{
  uint8_t a;
  printf("%s\n\n| archetype (tag the build carries most) | share of builds | clear rate |\n|---|---|---|\n", title);
  for (a = 0; a < ARCH_COUNT; a++) {
    if (!B.n[a]) continue;
    printf("| %s | ", mx_arch_name[a]); pp(PCT10(B.n[a], B.n_all)); printf(" | "); pp(PCT10(B.win[a], B.n[a])); printf(" |\n");
  }
  printf("| **field** | 100%% | "); pp(PCT10(B.win_all, B.n_all)); printf(" |\n");
  { uint32_t nb = 0, k2; for (k2 = 0; k2 <= 10; k2++) nb += B.hist[k2];
    printf("| (builds clearing >= 90%%) | "); pp(PCT10(B.hist[9] + B.hist[10], nb)); printf(" | |\n");
    printf("| (builds clearing < 10%%) | "); pp(PCT10(B.hist[0], nb)); printf(" | |\n"); }
  printf("| (mono-element: top tag >= 55%%) | "); pp(PCT10(B.mono_n, B.n_all)); printf(" | "); pp(PCT10(B.mono_win, B.mono_n)); printf(" |\n");
  printf("| (hybrid: two or more tags) | "); pp(PCT10(B.hyb_n, B.n_all)); printf(" | "); pp(PCT10(B.hyb_win, B.hyb_n)); printf(" |\n\n");
}

static void run_build_sims(void)
{
  int target = 400, hi_band = 600, lo_band = 200; /* x10 percent */
  uint16_t K;
  uint8_t iter, g;
  int best, worst;
  uint32_t stride = 61;
  printf("## 5. Builds and the abstract combat model\n\n");
  printf("Build = weapon base (%d) + 2 of %d weapon mods + 2 of %d heir traits (each an upside piece + a COST piece) + dodge (%d) "
         "= %u builds; all are enumerated. Each build runs %u fixed generated runs (same runs for every build). The model: "
         "time-to-kill = enemy HP pool (x kill-rule penalty when the build lacks the means and must use the room) / build DPS "
         "(with stacked bonuses and reaction bonuses); damage taken = enemy DPS x time x (1 - control) x (1 - avoid +/- skill noise) "
         "x (1 - defence) x K. Rests heal; player power grows per floor. It is an abstract budget model, not the game.\n\n",
         WB_COUNT, N_MODS, N_TRAITS, DG_COUNT, build_index_count(), nbr);
  printf("Design target: field clear rate %d%%; every archetype within [%d%%, %d%%] (none more than +50%% relative / "
         "+20 points over the target, none under half the target = useless).\n\n", target / 10, lo_band / 10, hi_band / 10);
  /* A: no diminishing returns, no caps */
  mx_dr_on = 0;
  K = calibrate(stride, target);
  eval_builds(stride, K, 0);
  printf("### Before: linear stacking, no caps (K=%u)\n\n", K);
  print_arch_table("");
  /* B: DR + caps, tuned */
  mx_dr_on = 1;
  print_caps("Starting soft caps");
  static uint8_t exhausted[3][32];
  memset(exhausted, 0, sizeof exhausted);
  for (iter = 0; iter < 24; iter++) {
    /* candidates: archetypes (>= 0.5% of builds), every mod and every trait. Knob: the cap of the group the
       candidate leans on (lifesteal -> heal cap). Lower it when over the band, raise it when under. */
    int v, bv = 0, wv = 1000; uint8_t bk = 0, bi = 0, wk = 0, wi = 0, kind, idx, mg = 0; uint64_t mx = 0;
    K = calibrate(stride, target);
    eval_builds(stride, K, 0);
    for (g = 0; g < ARCH_COUNT; g++) { if (B.n[g] < B.n_all / 200 || exhausted[0][g]) continue; v = PCT10(B.win[g], B.n[g]);
      if (v > bv) { bv = v; bk = 0; bi = g; } if (v < wv) { wv = v; wk = 0; wi = g; } }
    for (g = 0; g < N_MODS; g++) { if (exhausted[1][g]) continue; v = PCT10(B.mod_win[g], B.mod_n[g]); if (v > bv) { bv = v; bk = 1; bi = g; } if (v < wv) { wv = v; wk = 1; wi = g; } }
    for (g = 0; g < N_TRAITS; g++) { if (exhausted[2][g]) continue; v = PCT10(B.tr_win[g], B.tr_n[g]); if (v > bv) { bv = v; bk = 2; bi = g; } if (v < wv) { wv = v; wk = 2; wi = g; } }
    best = bv; worst = wv;
    if (best <= hi_band && worst >= lo_band) break;
    if (best > hi_band) { kind = bk; idx = bi; } else { kind = wk; idx = wi; }
    if (kind == 0) { for (g = 0; g < SG_COUNT - 1; g++) if (B.grp[idx][g] > mx) { mx = B.grp[idx][g]; mg = g; } }
    else if (kind == 1) mg = P_GROUP(&mx_mods[idx].p);
    else mg = P_GROUP(&mx_traits[idx].up);
    printf("- tuning step %u: %s %s at ", iter + 1, kind == 0 ? "archetype" : kind == 1 ? "mod" : "trait",
           kind == 0 ? mx_arch_name[idx] : kind == 1 ? mx_mods[idx].name : mx_traits[idx].name);
    pp(best > hi_band ? best : worst);
    if (kind == 1 && mg == SG_DEFENSE && P_TAGA(&mx_mods[idx].p) == MX_ALIVE) {
      if (best > hi_band) { if (mx_heal_cap > 1) mx_heal_cap--; else exhausted[kind][idx] = 1; } else mx_heal_cap++;
      printf(" -> lifesteal heal cap %s to %d HP/room\n", best > hi_band ? "lowered" : "raised", mx_heal_cap);
      continue;
    }
    if (best > hi_band) { if (mx_cap[mg] > 24) mx_cap[mg] = (uint8_t)(mx_cap[mg] - 8); else exhausted[kind][idx] = 1; }
    else { if (mx_cap[mg] < 200) mx_cap[mg] = (uint8_t)(mx_cap[mg] + 8); else exhausted[kind][idx] = 1; }
    printf(" -> %s cap %s to %d\n", mx_group_name[mg], best > hi_band ? "lowered" : "raised", mx_cap[mg]);
  }
  if (iter == 24) printf("- stopped after 24 steps: remaining outliers are data (magnitudes), not caps; see the piece table.\n");
  else printf("- all archetypes, mods and traits inside [%d%%, %d%%] after %u step(s).\n", lo_band / 10, hi_band / 10, iter);
  printf("\n");
  print_caps("Tuned soft caps (hard stop = cap x1.5)");
  printf("Lifesteal heal cap: %d HP per room.\n\n", mx_heal_cap);
  /* full enumeration */
  K = calibrate(13, target);
  g_K = K;
  eval_builds(1, K, 1);
  printf("### After: diminishing returns + tuned caps, all %u builds (K=%u)\n\n", build_index_count(), K);
  print_arch_table("");
  printf("| weapon base | clear | | dodge | clear |\n|---|---|---|---|---|\n");
  for (g = 0; g < WB_COUNT; g++) {
    printf("| %s | ", mx_wbases[g].name); pp(PCT10(B.wb_win[g], B.wb_n[g]));
    if (g < DG_COUNT) { printf(" | | %s | ", mx_dodges[g].name); pp(PCT10(B.dg_win[g], B.dg_n[g])); printf(" |\n"); }
    else printf(" | | | |\n");
  }
  printf("\n| weapon mod | clear (builds with it) | | heir trait | clear |\n|---|---|---|---|---|\n");
  for (g = 0; g < N_MODS || g < N_TRAITS; g++) {
    if (g < N_MODS) { printf("| %s | ", mx_mods[g].name); pp(PCT10(B.mod_win[g], B.mod_n[g])); printf(" |"); } else printf("| | |");
    if (g < N_TRAITS) { printf(" | %s | ", mx_traits[g].name); pp(PCT10(B.tr_win[g], B.tr_n[g])); printf(" |\n"); } else printf(" | | |\n");
  }
  printf("\nSpread of per-build clear rate (%u runs each):\n\n| clear rate | 0-9%% | 10-19 | 20-29 | 30-39 | 40-49 | 50-59 | 60-69 | 70-79 | 80-89 | 90-100 |\n|---|---|---|---|---|---|---|---|---|---|---|\n| builds |", nbr);
  for (g = 0; g < 10; g++) printf(" %u |", B.hist[g] + (g == 9 ? B.hist[10] : 0));
  printf("\n\nWhere runs end (floor of death; 11 = cleared): ");
  for (g = 0; g <= RUN_FLOORS; g++) { printf("%u:", g + 1); pp(PCT10(B.death_floor[g], B.n_all)); printf("%s", g < RUN_FLOORS ? " " : "\n\n"); }
}

/* degenerate scan: best/worst mod pairs and full builds */
static void degenerate_scan(void)
{
  uint8_t *rate = g_rate;
  uint32_t idx, total = build_index_count(), top = 0, dead = 0;
  uint32_t best_i[5] = { 0 }, worst_i[5] = { 0 };
  int best_v[5] = { -1, -1, -1, -1, -1 }, worst_v[5] = { 1001, 1001, 1001, 1001, 1001 };
  uint64_t (*pw)[N_MODS] = g_pw, (*pn)[N_MODS] = g_pn;
  mx_build b; uint8_t k, q;
  for (idx = 0; idx < total; idx++) { if (rate[idx] >= 90) top++; if (rate[idx] <= 3) dead++; }
  printf("### Degenerate / broken scan\n\nBuilds clearing >= 90%%: %u (", top); pp(PCT10(top, total));
  printf("); builds clearing <= 3%%: %u (", dead); pp(PCT10(dead, total)); printf(").\n\n");
  for (k = 0; k < N_MODS; k++) for (q = (uint8_t)(k + 1); q < N_MODS; q++) {
    int v = PCT10(pw[k][q], pn[k][q]); uint8_t z;
    for (z = 0; z < 5; z++) if (v > best_v[z]) { uint8_t y; for (y = 4; y > z; y--) { best_v[y] = best_v[y - 1]; best_i[y] = best_i[y - 1]; } best_v[z] = v; best_i[z] = (uint32_t)(k * 32 + q); break; }
    for (z = 0; z < 5; z++) if (v < worst_v[z]) { uint8_t y; for (y = 4; y > z; y--) { worst_v[y] = worst_v[y - 1]; worst_i[y] = worst_i[y - 1]; } worst_v[z] = v; worst_i[z] = (uint32_t)(k * 32 + q); break; }
  }
  printf("| strongest mod pairs | clear | | weakest mod pairs | clear |\n|---|---|---|---|---|\n");
  for (k = 0; k < 5; k++) {
    printf("| %s + %s | ", mx_mods[best_i[k] / 32].name, mx_mods[best_i[k] % 32].name); pp(best_v[k]);
    printf(" | | %s + %s | ", mx_mods[worst_i[k] / 32].name, mx_mods[worst_i[k] % 32].name); pp(worst_v[k]); printf(" |\n");
  }
  /* the single best and worst full builds */
  {
    uint32_t bi = 0, wi = 0;
    for (idx = 0; idx < total; idx++) { if (rate[idx] > rate[bi]) bi = idx; if (rate[idx] < rate[wi]) wi = idx; }
    build_from_index(bi, &b);
    printf("\nTop build: %s + %s + %s, %s + %s, %s: %d%%. ", mx_wbases[b.base].name, mx_mods[b.mod[0]].name, mx_mods[b.mod[1]].name,
           mx_traits[b.trait[0]].name, mx_traits[b.trait[1]].name, mx_dodges[b.dodge].name, rate[bi]);
    build_from_index(wi, &b);
    printf("Bottom build: %s + %s + %s, %s + %s, %s: %d%%.\n\n", mx_wbases[b.base].name, mx_mods[b.mod[0]].name, mx_mods[b.mod[1]].name,
           mx_traits[b.trait[0]].name, mx_traits[b.trait[1]].name, mx_dodges[b.dodge].name, rate[wi]);
  }
}


/* ---------- 5f. bred heirs (proto/genetics) through the same model ---------- */
#define MAX_BRED 1200
typedef struct { uint8_t strat; mx_build b; uint8_t aff[7]; uint8_t st[4]; uint8_t gtr; uint8_t allergen; uint16_t gbits; } bred_heir; /* gtr: 1 REMEMBERS 2 COLOURBLIND 4 ALLERGIC; gbits: genetic traits */
static uint8_t g_bred_costs; /* 0 none, 1 the three per-room trait costs, 2 every genetic trait as the doc defines it */
static uint8_t g_aff_mag = 10, g_stat_budget = 0, g_heavy_soft = 0, g_heavy_stagger = 0;
static const char *const gtrait_name[10] = { "HEAVY", "SMALL", "ECHO", "GLITCHED", "INSOMNIAC", "REMEMBERS", "DAMP", "LOUD", "COLOURBLIND", "ALLERGIC" };
static bred_heir g_bred[MAX_BRED];
static uint32_t n_bred;
static char strat_name[8][16];
static uint8_t n_strat;
static const uint8_t gen_tag[7] = { MX_HOT, MX_COLD, MX_WET, MX_SHINY, MX_ALIVE, MX_MAGIC, MX_GLITCH };

static uint8_t map_trait(const char *t)
{ /* genetic trait -> nearest combat trait in this model; story/look-only traits carry no combat piece */
  if (!strcmp(t, "HEAVY")) return 6;       /* GIANT */
  if (!strcmp(t, "SMALL")) return 7;       /* TINY */
  if (!strcmp(t, "GLITCHED")) return 0;    /* GLITCHED */
  if (!strcmp(t, "DAMP")) return 1;        /* WET HANDS */
  if (!strcmp(t, "INSOMNIAC")) return 10;  /* SLEEPWALKER */
  if (!strcmp(t, "LOUD")) return 2;        /* HOTHEAD */
  if (!strcmp(t, "ECHO")) return 3;        /* BRITTLE (echo dodge: longer i-frames, fragile) */
  return 255;                              /* REMEMBERS, COLOURBLIND, ALLERGIC: none */
}

static void load_bred(const char *path)
{
  FILE *f = fopen(path, "r");
  char st[16], w[16], d[16], t0[16], t1[16];
  int a[7], q[4], i;
  uint32_t line_in_strat = 0;
  n_bred = 0; n_strat = 0;
  if (!f) return;
  while (n_bred < MAX_BRED && fscanf(f, "%15s %15s %15s %15s %15s %d %d %d %d %d %d %d %d %d %d %d", st, w, d, t0, t1,
                                     &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &q[0], &q[1], &q[2], &q[3]) == 16) {
    bred_heir *h = &g_bred[n_bred];
    mx_rng r;
    uint8_t k;
    for (k = 0; k < n_strat; k++) if (!strcmp(strat_name[k], st)) break;
    if (k == n_strat && n_strat < 8) { strcpy(strat_name[n_strat++], st); line_in_strat = 0; }
    h->strat = k;
    h->b.base = !strcmp(w, "HAMMER") ? WB_HAMMER : !strcmp(w, "GAUNTLET") ? WB_GAUNTLET : !strcmp(w, "WHIP") ? WB_SPEAR :
                !strcmp(w, "LAUNCHER") ? WB_DISC : WB_BLADE;
    h->b.dodge = !strcmp(d, "BLINK") ? DG_BLINK : !strcmp(d, "SLIDE") ? DG_SLIDE : DG_ROLL;
    h->b.trait[0] = map_trait(t0); h->b.trait[1] = map_trait(t1);
    h->gtr = 0; h->gbits = 0;
    { uint8_t q2; for (q2 = 0; q2 < 10; q2++) if (!strcmp(t0, gtrait_name[q2]) || !strcmp(t1, gtrait_name[q2])) h->gbits |= (uint16_t)(1u << q2); }
    if (!strcmp(t0, "REMEMBERS") || !strcmp(t1, "REMEMBERS")) h->gtr |= 1;
    if (!strcmp(t0, "COLOURBLIND") || !strcmp(t1, "COLOURBLIND")) h->gtr |= 2;
    if (!strcmp(t0, "ALLERGIC") || !strcmp(t1, "ALLERGIC")) h->gtr |= 4;
    { static const uint8_t pick[7] = { MX_HOT, MX_COLD, MX_WET, MX_SHOCK, MX_ALIVE, MX_STONE, MX_AIRY };
      h->allergen = pick[(line_in_strat * 5u + 3u) % 7u]; } /* the element is seeded per line */
    if (h->b.trait[0] == h->b.trait[1]) h->b.trait[1] = 255;
    /* typical found mods: the same seeded pair for line i in every strategy, so only the genes differ */
    mx_seed(&r, 9000u + line_in_strat);
    h->b.mod[0] = mx_below(&r, N_MODS);
    do h->b.mod[1] = mx_below(&r, N_MODS); while (h->b.mod[1] == h->b.mod[0]);
    for (i = 0; i < 7; i++) h->aff[i] = (uint8_t)a[i];
    for (i = 0; i < 4; i++) h->st[i] = (uint8_t)q[i];
    line_in_strat++; n_bred++;
  }
  fclose(f);
}

static uint8_t bred_mode; /* 0 full, 1 no stats, 2 no affinities, 3 stats at half weight, 4 stat budget 40 */
static uint8_t bred_pieces(const bred_heir *h, mx_piece *p)
{
  uint8_t n = 0, i, sh = (uint8_t)(bred_mode == 3);
  mx_piece t;
  bred_heir hh = *h;
  if (bred_mode == 4 || g_stat_budget) { /* one combined genome budget across SPD/POW/GRD/FOC */
    uint16_t bud = g_stat_budget ? g_stat_budget : 40, tot = (uint16_t)(h->st[0] + h->st[1] + h->st[2] + h->st[3]);
    if (tot > bud) for (i = 0; i < 4; i++) hh.st[i] = (uint8_t)(h->st[i] * bud / tot);
    h = &hh;
  }
  /* stats arrive as percent bonuses (already diminished in genes.py); they enter the same group stacks */
  if (bred_mode != 1) {
  t = (mx_piece)MXP(TR_PASSIVE, EF_STAT, MX_NOTAG, MX_NOTAG, h->st[1] >> sh, 0, 0, PK_TRAIT, SG_DAMAGE, 0); p[n++] = t;       /* POW */
  t = (mx_piece)MXP(TR_PASSIVE, EF_STAT, MX_NOTAG, MX_NOTAG, (h->st[0] / 2) >> sh, 0, 0, PK_TRAIT, SG_SPEED, 0); p[n++] = t;   /* SPD */
  t = (mx_piece)MXP(TR_PASSIVE, EF_STAT, MX_NOTAG, MX_NOTAG, (h->st[2] / 2) >> sh, 0, 0, PK_TRAIT, SG_DEFENSE, 0); p[n++] = t; /* GRD */
  t = (mx_piece)MXP(TR_CHARGE, EF_AREA, MX_NOTAG, MX_NOTAG, h->st[3] >> sh, 0, 0, PK_TRAIT, SG_AREA, 0); p[n++] = t;         /* FOC */
  }
  if (g_bred_costs == 2) { /* doc-faithful trait upsides that act in combat */
    if (h->gbits & 1) { t = (mx_piece)MXP(TR_PUSH, EF_TERRAIN, MX_STONE, MX_NOTAG, 30, 2, 0, PK_TRAIT, SG_DAMAGE, 0); p[n++] = t; }  /* HEAVY: 2-block push, crush */
    if (h->gbits & 64) { t = (mx_piece)MXP(TR_PASSIVE, EF_STATUS, MX_WET, MX_NOTAG, 20, 0, 0, PK_TRAIT, SG_STATUS, 0); p[n++] = t; } /* DAMP: always WET */
  }
  if (bred_mode != 2) for (i = 0; i < 7; i++) if (h->aff[i]) {                                                                              /* affinities */
    t = (mx_piece)MXP(TR_HIT, EF_STATUS, gen_tag[i], MX_NOTAG, g_aff_mag * h->aff[i], 0, 4, PK_ELEMENT, SG_STATUS, 0); p[n++] = t;
  }
  return n;
}

/* REMEMBERS: keeps one recipe, forgets another -> +1 recipe step in mix rooms (the upside is run meta, not combat).
 * COLOURBLIND: reds and greens merge -> HOT and ALIVE tells read worse (+25% damage from those foes); its upside
 * (hidden SHINY paths) is treasure, not combat. ALLERGIC: one element hurts (+50%), immune-ish to its opposite (-50%). */
static uint8_t opposite(uint8_t t)
{
  switch (t) { case MX_HOT: return MX_COLD; case MX_COLD: return MX_HOT; case MX_WET: return MX_SHOCK; case MX_SHOCK: return MX_WET;
  case MX_ALIVE: return MX_STONE; case MX_STONE: return MX_ALIVE; case MX_AIRY: return MX_STONE; default: return MX_NOTAG; }
}
static void apply_gene_costs(const bred_heir *h, mx_bstats *s)
{
  if (h->gtr & 1) s->mix_pen = 1;
  if (h->gtr & 2) { s->el_mult[MX_HOT] = (uint8_t)(s->el_mult[MX_HOT] * 20 / 16); s->el_mult[MX_ALIVE] = (uint8_t)(s->el_mult[MX_ALIVE] * 20 / 16); }
  if (h->gtr & 4) {
    uint8_t o = opposite(h->allergen);
    s->el_mult[h->allergen] = (uint8_t)(s->el_mult[h->allergen] * 24 / 16);
    if (o < MX_TAGS) s->el_mult[o] = (uint8_t)(s->el_mult[o] * 8 / 16);
  }
  if (g_bred_costs == 2) { /* the rest of the doc's trait table (section 9) */
    if (h->gbits & 1) {
      if (g_heavy_soft) { /* adopted: -10% speed, and the B->A dodge-cancel lunge staggers (control) */
        s->avoid = (int16_t)(s->avoid - 5);
        s->ctrl = (int16_t)(s->ctrl + g_heavy_stagger);
      } else s->avoid = (int16_t)(s->avoid - 10);                                   /* doc v1: -20% speed */
    }
    if (h->gbits & 2) s->hp = (int16_t)(s->hp - 20);                               /* SMALL: -1 heart (of 5) */
    if (h->gbits & 4) s->foe_pct = (uint8_t)(s->foe_pct + 10);                     /* ECHO: the ghost's killer spawns */
    if (h->gbits & 16) s->avoid = (int16_t)(s->avoid - 4);                         /* INSOMNIAC: -1 i-frame */
    if (h->gbits & 64) { s->el_mult[MX_HOT] = (uint8_t)(s->el_mult[MX_HOT] / 2); s->el_mult[MX_SHOCK] = (uint8_t)(s->el_mult[MX_SHOCK] * 2); } /* DAMP */
    if (h->gbits & 128) { s->st_mult[ST_HOLDOUT] = 12; s->st_mult[ST_ESCORT] = 12; s->st_mult[ST_HEIST] = 20; s->st_mult[ST_DARK] = 20; } /* LOUD */
  }
}

static void eval_bred(uint16_t K, uint32_t *win, uint32_t *n, uint32_t *over60, uint8_t *rates)
{
  uint32_t i, j, w; uint8_t d, np; mx_piece pc[16]; mx_bstats s;
  for (i = 0; i < 8; i++) { win[i] = n[i] = over60[i] = 0; }
  for (i = 0; i < n_bred; i++) {
    const bred_heir *h = &g_bred[i];
    mx_build bb = h->b;
    if (g_bred_costs == 2) { bb.trait[0] = (uint8_t)((h->gbits & 8) ? 0 : 255); bb.trait[1] = 255; } /* only GLITCHED keeps its piece */
    np = bred_pieces(h, pc);
    mx_build_stats_ex(&bb, pc, np, &s);
    if (g_bred_costs) apply_gene_costs(h, &s);
    w = 0;
    for (j = 0; j < nbr; j++) w += mx_sim_run(&bb, &s, &b_runs[j], i * 131u + j, K, &d);
    win[h->strat] += w; n[h->strat] += nbr;
    if (w * 100 > nbr * 60) over60[h->strat]++;
    if (rates) rates[i] = (uint8_t)(w * 100 / nbr);
  }
}

static void strat_table_row(uint32_t *win, uint32_t *n, int *out, uint8_t ns)
{
  uint8_t k; for (k = 0; k < ns; k++) out[k] = PCT10(win[k], n[k]);
}
static void run_bred(uint16_t K, const char *path_after, const char *path_before)
{
  uint32_t win[8], n[8], o60[8], i, k, base = 0;
  static uint8_t rates[MAX_BRED];
  int before[8], budget_only[8], after[8], b0, b1, b2;
  char names[8][16]; uint8_t ns;
  printf("## 7. Bred lines (proto/genetics, generation-30 heirs)\n\n");
  /* BEFORE: the old genome (93 stat points) and the old model (three traits free) */
  load_bred(path_before);
  if (!n_bred) { printf("(%s not found: run bred_to_txt.py)\n\n", path_before); return; }
  g_bred_costs = 0; bred_mode = 0;
  eval_bred(K, win, n, o60, 0); strat_table_row(win, n, before, n_strat);
  ns = n_strat; for (k = 0; k < ns; k++) strcpy(names[k], strat_name[k]);
  /* AFTER: the new genome (one 40-point stat budget), first without and then with the three trait costs */
  load_bred(path_after);
  if (!n_bred || n_strat != ns) { printf("(%s missing or has different strategies)\n\n", path_after); return; }
  eval_bred(K, win, n, o60, 0); strat_table_row(win, n, budget_only, n_strat);
  g_bred_costs = 1;
  eval_bred(K, win, n, o60, rates); strat_table_row(win, n, after, n_strat);
  for (k = 0; k < n_strat; k++) if (!strcmp(strat_name[k], "random")) base = k;
  b0 = before[base]; b1 = budget_only[base]; b2 = after[base];
  printf("%u heirs per file, 200 per strategy, %u runs each, enemies kept on the base-build balance (K=%u, tuned caps). "
         "Before = `bred_lines_before.json` (stats up to ~93 points) with REMEMBERS/COLOURBLIND/ALLERGIC free. After = "
         "`bred_lines.json` (one shared 40-point stat budget, applied in genes.py) plus real costs for those three: "
         "REMEMBERS forgets a different recipe (+1 recipe step in every mix room); COLOURBLIND merges reds and greens "
         "(HOT and ALIVE foes' tells read worse: they deal +25%%); ALLERGIC makes one seeded element hurt (+50%%) and its "
         "opposite hurt less (-50%%). Their upsides (a kept recipe, hidden SHINY paths) are run meta and treasure, so they "
         "add nothing in combat. Mapping as before: WHIP->SPEAR, LAUNCHER->DISC, HEAVY->GIANT, SMALL->TINY, "
         "GLITCHED->GLITCHED, DAMP->WET HANDS, INSOMNIAC->SLEEPWALKER, LOUD->HOTHEAD, ECHO->BRITTLE; stats POW->DAMAGE, "
         "SPD/2->SPEED, GRD/2->DEFENSE, FOC->charge AREA; each affinity level is a 10-point on-hit status. Every line gets "
         "the same seeded pair of found mods by line index.\n\n", n_bred, nbr, K);
  printf("| strategy | clear before | vs random before | budget only | clear after | vs random after | heirs > 60%% after | p10 / p50 / p90 after | flag |\n"
         "|---|---|---|---|---|---|---|---|---|\n");
  for (k = 0; k < n_strat; k++) {
    uint8_t v[MAX_BRED]; uint32_t m = 0, a2, c2; uint8_t t;
    for (i = 0; i < n_bred; i++) if (g_bred[i].strat == k) v[m++] = rates[i];
    for (a2 = 1; a2 < m; a2++) { t = v[a2]; c2 = a2; while (c2 && v[c2 - 1] > t) { v[c2] = v[c2 - 1]; c2--; } v[c2] = t; }
    printf("| %s | ", strat_name[k]); pp(before[k]);
    printf(" | %s%d.%d | ", before[k] < b0 ? "-" : "+", abs(before[k] - b0) / 10, abs(before[k] - b0) % 10);
    pp(budget_only[k]); printf(" (%s%d.%d) | ", budget_only[k] < b1 ? "-" : "+", abs(budget_only[k] - b1) / 10, abs(budget_only[k] - b1) % 10);
    pp(after[k]); printf(" | **%s%d.%d** | %u (", after[k] < b2 ? "-" : "+", abs(after[k] - b2) / 10, abs(after[k] - b2) % 10, o60[k]);
    pp(PCT10(o60[k], n[k] / nbr)); printf(") | %u / %u / %u | %s |\n", v[m / 10], v[m / 2], v[m * 9 / 10],
      after[k] > 600 ? "**over 60%**" : (after[k] - b2 > 50 ? "**beats random by > 5 pts**" : "ok"));
  }
  {
    uint32_t tw = 0, tn = 0;
    for (k = 0; k < n_strat; k++) { tw += win[k]; tn += n[k]; }
    printf("\nAll bred heirs after: "); pp(PCT10(tw, tn)); printf(" (base-build field 40.9%%).\n\n");
  }
  /* which cost does what: random strategy, after-genome, one cost at a time */
  {
    static const char *const cn[4] = { "none", "REMEMBERS only", "COLOURBLIND only", "ALLERGIC only" };
    uint8_t c, saved[MAX_BRED];
    uint32_t carriers[3] = { 0 };
    for (i = 0; i < n_bred; i++) { saved[i] = g_bred[i].gtr; for (c = 0; c < 3; c++) if (g_bred[i].gtr & (1u << c)) carriers[c]++; }
    printf("Carriers in the new file: REMEMBERS %u, COLOURBLIND %u, ALLERGIC %u (of %u). Each cost alone, all strategies:\n\n| cost applied | clear |\n|---|---|\n",
           carriers[0], carriers[1], carriers[2], n_bred);
    for (c = 0; c < 4; c++) {
      uint32_t tw = 0, tn = 0;
      for (i = 0; i < n_bred; i++) g_bred[i].gtr = (uint8_t)(c ? (saved[i] & (1u << (c - 1))) : 0);
      eval_bred(K, win, n, o60, 0);
      for (k = 0; k < n_strat; k++) { tw += win[k]; tn += n[k]; }
      printf("| %s | ", cn[c]); pp(PCT10(tw, tn)); printf(" |\n");
    }
    for (i = 0; i < n_bred; i++) g_bred[i].gtr = saved[i];
    printf("\n");
  }
  /* what-ifs: the asked-for change does not reach the 60% / few-points target, so test the next levers */
  {
    static const struct { const char *name; uint8_t mode, aff, budget; } wf[4] = {
      { "every genetic trait as section 9 of the redesign doc defines it (HEAVY push+crush/-speed, SMALL -1 heart, ECHO killer spawns, INSOMNIAC -1 i-frame, DAMP WET/fire-proof/shock x2, LOUD holdout/escort easier, heist/dark harder)", 2, 10, 0 },
      { "doc traits + affinity 5 per level (was 10)", 2, 5, 0 },
      { "doc traits + stat budget 30 (was 40)", 2, 10, 30 },
      { "doc traits + affinity 5 + stat budget 30", 2, 5, 30 } };
    uint8_t q;
    printf("What-ifs past the requested change (same K):\n\n| change | all heirs |");
    for (k = 0; k < n_strat; k++) printf(" %s |", strat_name[k]);
    printf(" worst gap vs random |\n|---|---|"); for (k = 0; k < n_strat; k++) printf("---|"); printf("---|\n");
    for (q = 0; q < 4; q++) {
      uint32_t tw = 0, tn = 0; int v[8], gap = 0;
      g_bred_costs = wf[q].mode; g_aff_mag = wf[q].aff; g_stat_budget = wf[q].budget;
      eval_bred(K, win, n, o60, 0);
      for (k = 0; k < n_strat; k++) { tw += win[k]; tn += n[k]; v[k] = PCT10(win[k], n[k]); }
      for (k = 0; k < n_strat; k++) if (v[k] - v[base] > gap) gap = v[k] - v[base];
      printf("| %s | ", wf[q].name); pp(PCT10(tw, tn)); printf(" |");
      for (k = 0; k < n_strat; k++) { printf(" "); pp(v[k]); printf(" |"); }
      printf(" +%d.%d |\n", gap / 10, gap % 10);
    }
    printf("\n");
    g_aff_mag = 10; g_stat_budget = 0;
  }
  /* FINAL (adopted): doc-defined traits as the reference, HEAVY softened to -10% speed + a staggering B->A lunge.
     Sweep the stagger (percent of enemy output removed) and take the smallest that puts heavy within +/-4 of random. */
  {
    static const uint8_t sweep[5] = { 0, 3, 5, 8, 12 };
    uint8_t q, chosen = 255;
    int v[8], tw, tn, best_gap = 1000;
    g_bred_costs = 2; g_heavy_soft = 1;
    printf("### Final (adopted): doc-defined traits, HEAVY at -10%% speed with a staggering dodge-cancel lunge\n\n");
    printf("Stagger sweep (random heirs carry HEAVY too, so both columns move; the B->A lunge interrupts; modelled as %% of enemy output removed, before the 60%% control cap):\n\n| stagger | random | heavy | heavy vs random |\n|---|---|---|---|\n");
    for (q = 0; q < 5; q++) {
      g_heavy_stagger = sweep[q];
      eval_bred(K, win, n, o60, 0);
      v[0] = PCT10(win[base], n[base]);
      for (k = 0; k < n_strat; k++) if (!strcmp(strat_name[k], "heavy")) v[1] = PCT10(win[k], n[k]);
      printf("| %u | ", sweep[q]); pp(v[0]); printf(" | "); pp(v[1]);
      printf(" | %s%d.%d |\n", v[1] < v[0] ? "-" : "+", abs(v[1] - v[0]) / 10, abs(v[1] - v[0]) % 10);
      if (sweep[q] && abs(v[1] - v[0]) <= 40 && (chosen == 255 || abs(v[1] - v[0]) < best_gap)) { chosen = sweep[q]; best_gap = abs(v[1] - v[0]); } /* the owner wants a real upside: smallest gap among non-zero staggers */
    }
    if (chosen == 255) chosen = sweep[4];
    g_heavy_stagger = chosen;
    eval_bred(K, win, n, o60, rates);
    tw = 0; tn = 0;
    for (k = 0; k < n_strat; k++) { v[k] = PCT10(win[k], n[k]); tw += (int)win[k]; tn += (int)n[k]; }
    printf("\nChosen stagger: %u (the non-zero value with the smallest heavy-vs-random gap). Final per-strategy table (K=%u, base-build balance):\n\n| strategy | clear | vs random | heirs > 60%% | p10 / p50 / p90 |\n|---|---|---|---|---|\n", chosen, K);
    for (k = 0; k < n_strat; k++) {
      uint8_t vv[MAX_BRED]; uint32_t m = 0, a2, c2; uint8_t t;
      for (i = 0; i < n_bred; i++) if (g_bred[i].strat == k) vv[m++] = rates[i];
      for (a2 = 1; a2 < m; a2++) { t = vv[a2]; c2 = a2; while (c2 && vv[c2 - 1] > t) { vv[c2] = vv[c2 - 1]; c2--; } vv[c2] = t; }
      printf("| %s | ", strat_name[k]); pp(v[k]);
      printf(" | %s%d.%d | %u (", v[k] < v[base] ? "-" : "+", abs(v[k] - v[base]) / 10, abs(v[k] - v[base]) % 10, o60[k]);
      pp(PCT10(o60[k], n[k] / nbr)); printf(") | %u / %u / %u |\n", vv[m / 10], vv[m / 2], vv[m * 9 / 10]);
    }
    printf("| **all heirs** | "); pp(PCT10(tw, tn)); printf(" | | | |\n\n");
    g_heavy_soft = 0; g_heavy_stagger = 0;
  }
  g_bred_costs = 0;
}

/* ---------- 5e. ten example builds ---------- */
typedef struct { const char *title; uint8_t base, m0, m1, t0, t1, dodge; const char *does; } example;
static const example ex[10] = {
  { "FROST AFTERIMAGE", WB_BLADE, 1, 12, 0, 8, DG_BLINK,
    "Each BLINK leaves a GLITCHED afterimage at both ends; FROSTBITTEN hits chill, FROST CORE's charge freezes the ring. A HOT foe or lamp touching a frozen afterimage = SHATTER (x2.5 burst). GLITCH + COLD replays a seeded COLD reaction (often FREEZE or SNOW)." },
  { "STEAM BLINDER", WB_SPEAR, 0, 12, 1, 3, DG_ROLL,
    "EMBER EDGE (HOT) + WET HANDS (WET) means every hit makes STEAM: a blind cloud that removes ~45% of enemy output. Pays for it: shock hurts +25% and BRITTLE takes +20%." },
  { "SPLIT MIRROR", WB_GAUNTLET, 4, 15, 5, 6, DG_ROLL,
    "Perfect parry reflects orbs (REFLECT); MIRROR GUARD makes the reflected orb SHINY, so it hits SHINY again = MIRROR (splits in 2), PRISM CUT splits on MAGIC (3). Only-hurt-by-reflected-shots foes melt. GIANT for the HP." },
  { "SHORT CIRCUIT", WB_DISC, 16, 3, 9, 7, DG_SLIDE,
    "BRINE TIP soaks, SPARK PLUG every 3rd hit shocks: WET + SHOCK = SHORT, a stun chain through every wet thing. Kills shield-until-wet-then-shocked foes on its own. STATIC HAIR pulses SHOCK when hurt; TINY makes it hard to hit." },
  { "GREENHOUSE", WB_HAMMER, 5, 2, 4, 6, DG_SLIDE,
    "Kills sprout SEED POUCH vines, TIDE HEM puddles on each slide: ALIVE + WET = OVERGROW roots everything; the same reaction bridges pits in puzzle rooms. Hammer pushes crush-only foes." },
  { "CINDER HEX", WB_WAND, 6, 0, 10, 2, DG_ROLL,
    "HEX INK (MAGIC) + EMBER EDGE (HOT) = CINDER: a delayed mark that bursts after 1 s. SLEEPWALKER drifts but its periodic MAGIC buff stacks into SURGE." },
  { "STORM KITE", WB_DISC, 9, 13, 13, 7, DG_BLINK,
    "FAN BLADE throws AIRY shots on every blink (x1.5 at both ends), LUCKY CABLE shocks when hurt: SHOCK + AIRY = THUNDER stun ring. FEATHERWEIGHT and TINY: a kiter with no HP." },
  { "MUD BRICK", WB_HAMMER, 8, 16, 12, 6, DG_ROLL,
    "GRAVEL CORE turns every push into STONE terrain, BRINE TIP makes WET: STONE + WET = MUD (slows, forces burrowers up). STONE SKIN + GIANT: the slow wall build. Armour break from STONE tag." },
  { "LANTERN DIVER", WB_SPEAR, 10, 0, 5, 3, DG_BLINK,
    "LENS beam (SHINY) + EMBER EDGE (HOT) = LANTERN: lights dark rooms and reveals invisible foes; beams relay through mirrors. SHINY TEETH widens parry but glows in the dark." },
  { "BERSERK KILN", WB_BLADE, 11, 12, 11, 2, DG_ROLL,
    "KILN + BERSERK both fire on low HP: the only stack that goes past the DAMAGE cap's soft knee. 30% less healing is the bill. The sim's check on low-HP stacking." },
};

static void run_examples(uint16_t K)
{
  uint8_t i, d;
  uint32_t j, w, rep;
  mx_build b; mx_bstats s;
  printf("## 6. Ten example builds\n\n| build | pieces | archetype | off/def/ctrl/avoid | clear | what it does |\n|---|---|---|---|---|---|\n");
  for (i = 0; i < 10; i++) {
    b.base = ex[i].base; b.mod[0] = ex[i].m0; b.mod[1] = ex[i].m1; b.trait[0] = ex[i].t0; b.trait[1] = ex[i].t1; b.dodge = ex[i].dodge;
    mx_build_stats(&b, &s);
    w = 0;
    for (rep = 0; rep < 8; rep++) for (j = 0; j < nbr; j++) w += mx_sim_run(&b, &s, &b_runs[j], rep * 1000u + j, K, &d);
    printf("| **%s** | %s + %s + %s; %s + %s; %s | %s | %d/%d/%d/%d | ", ex[i].title, mx_wbases[b.base].name, mx_mods[b.mod[0]].name,
           mx_mods[b.mod[1]].name, mx_traits[b.trait[0]].name, mx_traits[b.trait[1]].name, mx_dodges[b.dodge].name,
           mx_arch_name[s.arch], s.off, s.def, s.ctrl, s.avoid);
    pp(PCT10(w, 8 * nbr)); printf(" | %s |\n", ex[i].does);
  }
  printf("\n");
}

static void print_world_tables(void)
{
  uint8_t b, k;
  printf("### 4e. World tables\n\n| biome | faction turf | top body | top element | top stage | modifier |\n|---|---|---|---|---|---|\n");
  for (b = 0; b < BI_COUNT; b++) {
    uint8_t tb = 0, te = 0, ts = 0;
    static const char *const mdn[BI_COUNT] = { "dark", "windy", "flooded", "heat", "heat", "overgrown", "windy", "glitchy" };
    for (k = 0; k < BD_COUNT; k++) if (mx_biome_body_w[b][k] > mx_biome_body_w[b][tb]) tb = k;
    for (k = 0; k < EL_COUNT; k++) if (mx_biome_el_w[b][k] > mx_biome_el_w[b][te]) te = k;
    for (k = 0; k < ST_COUNT; k++) if (mx_biome_stage_w[b][k] > mx_biome_stage_w[b][ts]) ts = k;
    printf("| %s | %s | %s | %s | %s | %s |\n", mx_biome_name[b], mx_fc_name[mx_biome_fc[b]], mx_body_name[tb],
           te < MX_TAGS ? mx_tag_name[te] : "untagged", mx_stage_name[ts], mdn[b]);
  }
  printf("\nFactions own tags (hated -> those tags field more enemies; liked -> those tags appear as helpful room sources): ");
  for (k = 0; k < FC_COUNT; k++) printf("%s %s/%s%s", mx_fc_name[k], mx_tag_name[mx_fc_tags[k][0]], mx_tag_name[mx_fc_tags[k][1]], k + 1 < FC_COUNT ? ", " : ".\n\n");
  printf("MIX recipes (CRUCIBLE's own), unlocked by depth (steps <= 1 + floor/3): ");
  for (k = 0; k < MX_RECIPES; k++) { char t[64]; tagmask_str(mx_recipes[k].need, t); printf("%s (%s, %d)%s", mx_recipes[k].name, t, mx_recipes[k].steps, k + 1 < MX_RECIPES ? ", " : ".\n\n"); }
}

static void print_budget(void)
{
  unsigned rom = (unsigned)(sizeof mx_rx_table + sizeof mx_tile_tag + sizeof mx_biome_body_w + sizeof mx_biome_atk_w +
                            sizeof mx_biome_def_w + sizeof mx_biome_el_w + sizeof mx_biome_beh_w + sizeof mx_biome_stage_w +
                            sizeof mx_biome_tile_w + sizeof mx_fc_tags + sizeof mx_atk_tell + sizeof mx_atk_lit);
  printf("## 8. ROM/CPU/OAM notes (estimates)\n\n");
  printf("- Numeric tables in this prototype: %u bytes (reaction matrix %u B full, 55 B as a triangle). Reaction records "
         "%d x ~7 B + names. Pieces: %u B each; a full player build is 6 pieces = 30 B of WRAM; mobs are 7 B.\n", rom,
         (unsigned)sizeof mx_rx_table, RX_COUNT, (unsigned)sizeof(mx_piece));
  printf("- Event resolution is fixed-order, once per frame: hurt -> hit -> parry -> kill -> charge/dodge -> timers -> "
         "tile-enter -> reactions -> terrain writes. Each equipped piece's trigger is a 4-bit field, so a 16-bit "
         "trigger mask per build filters which pieces wake (no scan). Max 8 queued events/frame; overflow drops the oldest "
         "cosmetic one, never a damage event.\n");
  printf("- Reactions: one table read per (tag, tag) contact; GLITCH adds one more read. No recursion: a reaction's "
         "result cannot itself react in the same frame (it is queued for the next).\n");
  printf("- Stacking runs only when a piece is equipped or lost (cached per group), never per hit.\n");
  printf("- OAM: 40 sprites, 10 per line. Budget: player 2 (8x16), weapon swing 1, mobs up to 6 x 2 = 12, boss 8-12 "
         "(mobs drop to 3 in boss rooms), material overlay ~6, **shared particle pool cap 10** (steam, sparks, shards, "
         "reflected orbs). Per-line pressure is the real limit: particles are culled first on busy lines; split shots "
         "reuse the pool and a 3-way PRISM splits only when 2 slots are free (else it reflects as 1).\n");
  printf("- CPU (CGB double speed ~70k cycles/frame): budget ~2k cycles for the piece/reaction layer: <= 8 events x "
         "(mask test + up to 6 piece reads ~40 cycles) + <= 4 reaction lookups ~60 cycles each. The generator (mobs, "
         "deck, guarantee) runs on floor load, not per frame; boss rejection sampling is the slowest piece (mean draws "
         "reported above) and can be baked into a seed table if it stalls.\n\n");
}

int main(int argc, char **argv)
{
  uint32_t runs = 10000;
  uint16_t K;
  if (argc > 1) runs = (uint32_t)strtoul(argv[1], 0, 10);
  if (argc > 2) nbr = (uint32_t)strtoul(argv[2], 0, 10);
  if (nbr > BRUNS_MAX) nbr = BRUNS_MAX;
  setvbuf(stdout, 0, _IOLBF, 0);
  printf("# EGRESS super matrix: generated report\n\n");
  print_schema();
  print_reactions();
  enumerate_mobs();
  enumerate_bosses();
  run_world_sims(runs);
  print_world_tables();
  make_build_runs();
  run_build_sims();
  K = g_K;
  degenerate_scan();
  run_examples(K);
  run_bred(K, argc > 3 ? argv[3] : "bred_lines.txt", argc > 4 ? argv[4] : "bred_lines_before.txt");
  print_budget();
  return 0;
}
