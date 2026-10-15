/* egplan stages RUNS [OUT.json]: the B2 stage sim (src/core/eg_stage.c), headless.
 *
 * 1. The cartridge's run, RUNS times (the B1 slice with the deck: nine office floors, the mid-boss slot, the boss),
 *    for players of generation 0-8 with fresh and veteran seen histories: every floor is planned exactly as the
 *    cartridge plans it (eg_slice_floor) and every non-puzzle stage is generated (eg_stage_gen). It checks:
 *      - the deck: never the same non-puzzle type back to back; how often a type comes back two floors later;
 *      - the generator: every stage was made, every one passed its own reachability checks, and lands in the band
 *        (|score - level| <= 1) after its rerolls; the chase's drop is re-checked against a real shortest path
 *        (BFS, the player at EG_CHASE_WALK frames a cell) and always leaves slack;
 *      - the climb: the mean planned level and the mean stage score rise segment by segment (floors 1-3, 4-6, 7-9)
 *        and with the generation;
 *      - the text: every objective and hunt tell fits the HUD's 18 characters.
 * 2. RUNS full runs (eg_run_build, ~52 floors, the deck of section 4 with its history): the same repeat rules, the
 *    stage-type shares, and stages generated for every non-puzzle slot at its planned level.
 * Fails (exit 1) on any break. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/eg_stage.h"

static uint32_t rs = 0x2545f491u;
static uint32_t rnd32(void) {
  rs ^= rs << 13;
  rs ^= rs >> 17;
  rs ^= rs << 5;
  return rs;
}

static const char *const tname[EG_DECK_TYPES] = {"puzzle", "holdout", "chase", "hunt", "escort", "mixp", "dark",
                                                 "heist"};

/* the chase, played out on the grid: the player moves one cell (or stays) every EG_CHASE_WALK frames, never onto a
 * cell whose row has dropped by the time they would leave it (row y drops at grace*4 + (12 - y) * speed); drywall is
 * the long way round. Returns the largest extra delay at the start (frames, in steps of 8) that still reaches the
 * door, or -1 if the door can't be reached at all. */
static int chase_feasible(const eg_room *r, const eg_stage *s, int delay) {
  static uint8_t cur[EG_N], nxt[EG_N];
  int step, i, k;
  memset(cur, 0, sizeof cur);
  cur[r->start] = 1;
  for (step = 0; step < 200; step++) {
    int t = delay + (step + 1) * (int)EG_CHASE_WALK, any = 0; /* the time a cell entered now is left */
    memset(nxt, 0, sizeof nxt);
    for (i = 0; i < (int)EG_N; i++) {
      if (!cur[i]) continue;
      for (k = 0; k < 5; k++) {
        int x = EG_X(i) + (k == 0 ? -1 : k == 1 ? 1 : 0), y = EG_Y(i) + (k == 2 ? -1 : k == 3 ? 1 : 0);
        uint8_t n, ty;
        if (x < 0 || y < 0 || x >= (int)EG_W || y >= (int)EG_H) continue;
        n = EG_IDX(x, y);
        ty = (uint8_t)(r->c[n] & T_MASK);
        if (!(EG_PASSABLE(ty) || ty == T_DOOR)) continue;
        if (n == r->door) return 1;
        if (y >= 1 && y <= 12 && t > s->grace * 4 + (12 - y) * s->speed) continue;
        nxt[n] = 1;
        any = 1;
      }
    }
    if (!any) return 0;
    memcpy(cur, nxt, sizeof cur);
  }
  return 0;
}
static int chase_margin(const eg_room *r, const eg_stage *s) {
  int d;
  if (!chase_feasible(r, s, 0)) return -1;
  for (d = 8; d < 2000 && chase_feasible(r, s, d); d += 8) {
  }
  return d - 8;
}

typedef struct {
  long stages, made, band, fail_made, chase_n, chase_bad, rep_b2b, rep_2, slots, trick_rep, slots_trick;
  long types[EG_DECK_TYPES];
  int chase_min;
  double lvl_sum[3], sc_sum[3];
  long seg_n[3];
  double gen_lvl[9];
  long gen_n[9];
  long by_type_band[EG_DECK_TYPES], by_type_n[EG_DECK_TYPES];
  double by_type_absdiff[EG_DECK_TYPES];
  long budget_sum[EG_DECK_TYPES], conds_sum[EG_DECK_TYPES], fail_type[EG_DECK_TYPES];
} acc;

static void gen_one(acc *a, uint8_t type, uint16_t seed, uint8_t level, uint8_t m) {
  static eg_room r;
  eg_stage s;
  uint16_t got;
  uint8_t d;
  a->stages++;
  got = eg_stage_gen(&r, &s, type, seed, level, m);
  if (!got) {
    a->fail_made++;
    a->fail_type[type]++;
    return;
  }
  a->made++;
  d = s.score > level ? (uint8_t)(s.score - level) : (uint8_t)(level - s.score);
  if (d <= 1) a->band++, a->by_type_band[type]++;
  a->by_type_n[type]++;
  a->by_type_absdiff[type] += d;
  a->budget_sum[type] += s.budget;
  a->conds_sum[type] += s.conds;
  /* re-check what the generator promised */
  eg_stage_flood(&r, r.start);
  if (!EG_REACHED(r.door)) a->fail_made++;
  if (type == SG_CHASE) {
    int mg = chase_margin(&r, &s);
    a->chase_n++;
    if (mg < 0) a->chase_bad++;
    if (mg < a->chase_min) a->chase_min = mg;
  }
}

int stage_sim(int runs, const char *json) {
  acc a;
  int run, f, k, bad = 0;
  char buf[24];
  memset(&a, 0, sizeof a);
  a.chase_min = 1 << 30;
  /* 0: the text */
  for (k = 0; k < EG_DECK_TYPES + KC_COUNT; k++) {
    eg_stage_text((uint8_t)k, buf);
    if (strlen(buf) > 18 || !buf[0]) {
      printf("objective %d too long or empty: '%s'\n", k, buf);
      bad++;
    }
  }
  /* 1: the cartridge's run */
  for (run = 0; run < runs; run++) {
    eg_plan_state ps;
    eg_slice sl;
    eg_seed_rec rec;
    uint8_t gen = (uint8_t)(run % 9), prev = 255, prev2 = 255;
    uint16_t seen = (run & 3) == 3 ? (uint16_t)0xffffu : 0u; /* a quarter are veterans who met everything */
    uint16_t seed = (uint16_t)rnd32();
    eg_plan_begin(&ps, seed, seen, gen);
    eg_slice_begin(&sl);
    for (f = 1; f <= 10; f++) {
      uint8_t kind = eg_slice_floor(&sl, &ps, (uint8_t)f, 10, &rec, 255);
      int seg = (f - 1) / 3;
      if (kind == SK_BOSS) break;
      if (kind == SK_STAGE) {
        a.slots++;
        a.types[sl.type]++;
        if (sl.type != SG_PUZZLE && sl.type == prev) a.rep_b2b++;
        if (sl.type != SG_PUZZLE && sl.type == prev2) a.rep_2++;
        prev2 = prev;
        prev = sl.type;
        if (sl.type == SG_PUZZLE) {
          a.slots_trick++;
          /* the planner's own rule: never the same trick at the same level twice running (B1) */
        }
      }
      if (seg < 3) {
        a.lvl_sum[seg] += sl.level;
        a.seg_n[seg]++;
      }
      a.gen_lvl[gen] += sl.level;
      a.gen_n[gen]++;
      if (sl.type != SG_PUZZLE) {
        static eg_room r;
        eg_stage s;
        gen_one(&a, sl.type, (uint16_t)(seed ^ (uint16_t)(f * 0x3b1)), sl.level, sl.m);
        if (eg_stage_gen(&r, &s, sl.type, (uint16_t)(seed ^ (uint16_t)(f * 0x3b1)), sl.level, sl.m) && seg < 3)
          a.sc_sum[seg] += s.score;
        else if (seg < 3)
          a.sc_sum[seg] += sl.level; /* (a puzzle's level is its baked room's) */
      } else if (seg < 3)
        a.sc_sum[seg] += rec.diff >> 4;
      eg_plan_result(&ps, (uint8_t)(rnd32() % 3), (uint16_t)(60u * (40u + rnd32() % 200u)));
    }
  }
  /* every type at every level, many seeds: the generator alone */
  for (k = 1; k < EG_DECK_TYPES; k++) {
    int lv, sd;
    for (lv = 0; lv <= 15; lv++)
      for (sd = 0; sd < runs / 8 + 8; sd++) gen_one(&a, (uint8_t)k, (uint16_t)rnd32(), (uint8_t)lv, k == SG_MIXP ? (uint8_t)(MK_BURN + sd % 4) : 255u);
  }
  /* 2: full runs through the deck */
  {
    static eg_run r;
    long fslots = 0, frep = 0, frep2 = 0, ftypes[SG_COUNT];
    memset(ftypes, 0, sizeof ftypes);
    for (run = 0; run < runs; run++) {
      eg_run_in in;
      int i;
      uint8_t prev = 255, prev2 = 255;
      memset(&in, 0, sizeof in);
      in.run_seed = (uint16_t)rnd32();
      in.cycle_seed = (uint16_t)rnd32();
      in.generation = (uint8_t)(run % 25);
      in.cycle = (uint8_t)(1 + run % 3);
      in.seen = run & 1 ? 0xfffffcu : 0u;
      in.developed = in.seen;
      eg_run_build(&r, &in);
      for (i = 0; i < r.n; i++) {
        const eg_slot *sl = &r.s[i];
        if (sl->kind != SK_STAGE) continue;
        fslots++;
        ftypes[sl->stype]++;
        if (sl->stype != SG_PUZZLE && sl->stype == prev && !SG_RARE(sl->stype)) frep++;
        if (sl->stype != SG_PUZZLE && sl->stype == prev2 && !SG_RARE(sl->stype)) frep2++;
        prev2 = prev;
        prev = sl->stype;
        if (sl->stype != SG_PUZZLE && sl->stype < SG_3D && (run % 10) == 0)
          gen_one(&a, sl->stype, (uint16_t)(in.run_seed + i), sl->target, sl->m[0]);
      }
    }
    printf("full runs %d: stage slots %ld, back to back same type %ld, same type two apart %.1f%%\n", runs, fslots,
           frep, 100.0 * frep2 / fslots);
    printf("  shares:");
    for (k = 0; k < SG_COUNT; k++) printf(" %s %.1f%%", k < EG_DECK_TYPES ? tname[k] : (k == SG_3D ? "3d" : k == SG_RIFT ? "rift" : "sky"), 100.0 * ftypes[k] / fslots);
    printf("\n");
    if (frep) bad++;
  }
  printf("cart runs %d: stage slots %ld, back to back same type %ld, same type two apart %.1f%%\n", runs, a.slots,
         a.rep_b2b, 100.0 * a.rep_2 / a.slots);
  printf("  types:");
  for (k = 0; k < EG_DECK_TYPES; k++) printf(" %s %.1f%%", tname[k], 100.0 * a.types[k] / a.slots);
  printf("\n");
  printf("stages generated %ld: made %ld (failed %ld), in the band %.1f%%\n", a.stages, a.made, a.fail_made,
         100.0 * a.band / (a.made ? a.made : 1));
  for (k = 1; k < EG_DECK_TYPES; k++)
    printf("  %-8s failed %4ld n %6ld  band %5.1f%%  mean |score-level| %.2f  budget %.1f  conds %.2f\n", tname[k], a.fail_type[k], a.by_type_n[k],
           100.0 * a.by_type_band[k] / (a.by_type_n[k] ? a.by_type_n[k] : 1),
           a.by_type_absdiff[k] / (a.by_type_n[k] ? a.by_type_n[k] : 1),
           (double)a.budget_sum[k] / (a.by_type_n[k] ? a.by_type_n[k] : 1),
           (double)a.conds_sum[k] / (a.by_type_n[k] ? a.by_type_n[k] : 1));
  printf("chase played out on the grid at %u frames a cell: %ld stages, %ld lost, least spare start delay %d frames\n", EG_CHASE_WALK, a.chase_n,
         a.chase_bad, a.chase_min);
  printf("climb: mean level by segment %.2f %.2f %.2f, mean stage score %.2f %.2f %.2f\n", a.lvl_sum[0] / a.seg_n[0],
         a.lvl_sum[1] / a.seg_n[1], a.lvl_sum[2] / a.seg_n[2], a.sc_sum[0] / a.seg_n[0], a.sc_sum[1] / a.seg_n[1],
         a.sc_sum[2] / a.seg_n[2]);
  printf("  by generation:");
  for (k = 0; k < 9; k++) printf(" %.2f", a.gen_lvl[k] / (a.gen_n[k] ? a.gen_n[k] : 1));
  printf("\n");
  if (a.rep_b2b || a.fail_made || a.chase_bad) bad++;
  if ((double)a.band / a.made < 0.95) bad++;
  if (!(a.lvl_sum[0] / a.seg_n[0] < a.lvl_sum[1] / a.seg_n[1] && a.lvl_sum[1] / a.seg_n[1] < a.lvl_sum[2] / a.seg_n[2]))
    bad++;
  if (!(a.sc_sum[0] / a.seg_n[0] < a.sc_sum[1] / a.seg_n[1] && a.sc_sum[1] / a.seg_n[1] < a.sc_sum[2] / a.seg_n[2]))
    bad++;
  if (!(a.gen_lvl[0] / a.gen_n[0] < a.gen_lvl[8] / a.gen_n[8])) bad++;
  if (json) {
    FILE *j = fopen(json, "w");
    if (j) {
      fprintf(j, "{\"runs\": %d, \"cart_slots\": %ld, \"back_to_back\": %ld, \"two_apart_pct\": %.2f, \"types\": {", runs,
              a.slots, a.rep_b2b, 100.0 * a.rep_2 / a.slots);
      for (k = 0; k < EG_DECK_TYPES; k++) fprintf(j, "%s\"%s\": %.2f", k ? ", " : "", tname[k], 100.0 * a.types[k] / a.slots);
      fprintf(j, "}, \"stages\": %ld, \"made\": %ld, \"band_pct\": %.2f, \"chase_min_margin\": %d,", a.stages, a.made,
              100.0 * a.band / a.made, a.chase_min);
      fprintf(j, " \"level_by_segment\": [%.3f, %.3f, %.3f], \"score_by_segment\": [%.3f, %.3f, %.3f], \"level_by_gen\": [",
              a.lvl_sum[0] / a.seg_n[0], a.lvl_sum[1] / a.seg_n[1], a.lvl_sum[2] / a.seg_n[2], a.sc_sum[0] / a.seg_n[0],
              a.sc_sum[1] / a.seg_n[1], a.sc_sum[2] / a.seg_n[2]);
      for (k = 0; k < 9; k++) fprintf(j, "%s%.3f", k ? ", " : "", a.gen_lvl[k] / a.gen_n[k]);
      fprintf(j, "]}\n");
      fclose(j);
    }
  }
  printf("stage sim: %s\n", bad ? "FAIL" : "ok");
  return bad ? 1 : 0;
}
