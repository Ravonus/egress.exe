/* egplan: the run planner (src/core/eg_plan.c) over the baked table, headless.
 *   egplan check RUNS              simulate RUNS runs of the B1 slice (the cartridge's nine office floors) and RUNS
 *                                  laid-out full runs (~52 floors, host/egrun.c); fail (exit 1) if any curriculum rule
 *                                  breaks in either
 *   egplan sim RUNS OUT.csv OUT.json   the slice sim, writing every room picked and the summary (tools/plot_curve.py)
 *   egplan runsim RUNS OUT.csv OUT.json    the full run: every slot of RUNS runs (tools/plot_curve.py --full)
 *   egplan survive LINES GENS OUT.json     the survival model: lines of heirs with shortcuts (tools/plot_survival.py)
 *   egplan layout RUNSEED CYCLESEED GEN SEEN DOOR   one full run's layout as text
 *   egplan stages RUNS [OUT.json]   B2: the deck and the stage generator over RUNS cartridge runs and full runs
 *                                  (host/egstage.c; part of `check`)
 *
 * The runs are played by simulated players, 8 runs each, with a skill that grows run by run: a room far above it
 * costs resets and sometimes the run; a room well below it is cleared fast. Seen mechanics carry over between a
 * player's runs the way the save keeps them (a mechanic counts as seen once its room is cleared). "Mixed" is a room
 * with two or more trick mechanics (BURN .. RHYTHM); step-count routing (STEPS) is a twist on top. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/eg_plan.h"

#define RUNS_PER_PLAYER 8
#define FLOORS 9

static uint32_t rs = 12345u;
static uint32_t rnd(void) {
  rs ^= rs << 13;
  rs ^= rs >> 17;
  rs ^= rs << 5;
  return rs;
}

static int popc(unsigned m) {
  int n = 0;
  for (; m; m >>= 1) n += (int)(m & 1u);
  return n;
}

int full_check(int runs, int verbose);                     /* host/egrun.c */
int full_runsim(int runs, const char *csv, const char *json);
int survive(int lines, int gens, const char *json);
int full_layout(unsigned run_seed, unsigned cycle_seed, unsigned gen, uint32_t seen, unsigned door);
int stage_sim(int runs, const char *json);                   /* host/egstage.c (B2 stages) */

static int slice_main(int argc, char **argv);

int main(int argc, char **argv) {
  if (argc >= 3 && !strcmp(argv[1], "check")) {
    int a = slice_main(argc, argv);
    int b = full_check(atoi(argv[2]), 1);
    int c = stage_sim(atoi(argv[2]), NULL); /* B2: the deck and the stage generator */
    return a || b || c;
  }
  if (argc >= 3 && !strcmp(argv[1], "sim")) return slice_main(argc, argv);
  if (argc >= 5 && !strcmp(argv[1], "runsim")) return full_runsim(atoi(argv[2]), argv[3], argv[4]);
  if (argc >= 7 && !strcmp(argv[1], "layout"))
    return full_layout((unsigned)strtoul(argv[2], 0, 0), (unsigned)strtoul(argv[3], 0, 0), (unsigned)atoi(argv[4]),
                       (uint32_t)strtoul(argv[5], 0, 0), (unsigned)atoi(argv[6]));
  if (argc >= 3 && !strcmp(argv[1], "stages")) return stage_sim(atoi(argv[2]), argc > 3 ? argv[3] : NULL);
  if (argc >= 5 && !strcmp(argv[1], "survive")) return survive(atoi(argv[2]), atoi(argv[3]), argv[4]);
  fprintf(stderr, "usage: egplan check RUNS | sim RUNS OUT.csv OUT.json | runsim RUNS OUT.csv OUT.json | "
                  "survive LINES GENS OUT.json\n");
  return 2;
}

static int slice_main(int argc, char **argv) {
  printf("== B1 slice (the cartridge: nine office floors over the baked table) ==\n");
  int sim = !strcmp(argv[1], "sim"), runs = atoi(argv[2]);
  FILE *csv = sim && argc > 3 ? fopen(argv[3], "w") : NULL;
  if (csv)
    fprintf(csv, "player,run,floor,stage,seed,tier,trick,mech,nmech,level,target,dead,fatal,insights,steps,branch,"
                 "enemy,resets,died,new\n");
  long rooms = 0, order_bad = 0, intro_n = 0, intro_alone = 0, repeat_bad = 0, stage_n[4] = {0};
  long mixed[FLOORS] = {0}, played[FLOORS] = {0}, level_sum[FLOORS] = {0}, target_sum[FLOORS] = {0};
  long mixed_gen[3][FLOORS] = {{0}}, played_gen[3][FLOORS] = {{0}}, deaths = 0, intros_by_mech[MK_SLICE_COUNT] = {0};
  long first_intro_run_sum = 0, first_intro_n = 0;
  int players = (runs + RUNS_PER_PLAYER - 1) / RUNS_PER_PLAYER, done = 0;
  for (int p = 0; p < players && done < runs; p++) {
    uint16_t saved = 0; /* MK bits the save holds */
    double skill = 3.0 + (rnd() % 100) / 100.0;
    for (int run = 0; run < RUNS_PER_PLAYER && done < runs; run++, done++) {
      eg_plan_state ps;
      eg_plan_begin(&ps, (uint16_t)(rnd() & 0xffffu), saved, (uint8_t)run);
      uint8_t prev_trick = 255, prev_level = 255;
      for (int f = 1; f <= FLOORS; f++) {
        uint16_t before = ps.seen;
        eg_seed_rec e;
        uint8_t stage = eg_plan_floor(&ps, (uint8_t)f, &e);
        int lvl = e.diff >> 4, own = popc(e.mech & ~MK_BASIC & ~(1u << MK_STEPS)); /* trick mechanics */
        rooms++;
        stage_n[stage]++;
        /* the rules */
        uint16_t allowed = (uint16_t)(before | (stage == ST_INTRO ? (1u << ps.mech_new) : 0u));
        if (e.mech & ~allowed) order_bad++;
        if (stage == ST_INTRO) {
          intro_n++;
          intros_by_mech[ps.mech_new]++;
          if (ps.mech_new == MK_STEPS ? own == 1 : (e.mech & ~MK_BASIC) == (1u << ps.mech_new)) intro_alone++;
          first_intro_run_sum += run;
          first_intro_n++;
        }
        if (e.trick == prev_trick && lvl == prev_level) repeat_bad++;
        prev_trick = e.trick;
        prev_level = (uint8_t)lvl;
        played[f - 1]++;
        level_sum[f - 1] += lvl;
        target_sum[f - 1] += ps.target;
        if (own >= 2) mixed[f - 1]++;
        int gb = run < 2 ? 0 : run < 5 ? 1 : 2;
        played_gen[gb][f - 1]++;
        if (own >= 2) mixed_gen[gb][f - 1]++;
        /* the player */
        double gap = lvl - skill;
        int resets = 0, died = 0;
        uint16_t frames = (uint16_t)(60u * (40u + 10u * (unsigned)lvl));
        if (gap > 2) {
          resets = 2 + (int)(rnd() % 2);
          died = rnd() % 100 < 15;
        } else if (gap > 0) {
          resets = (int)(rnd() % 2);
          died = rnd() % 100 < 3;
        } else if (gap < -1)
          frames = (uint16_t)(60u * (20u + 6u * (unsigned)lvl));
        if (csv)
          fprintf(csv, "%d,%d,%d,%d,%u,%u,%u,%u,%d,%d,%u,%u,%u,%u,%u,%u,%u,%d,%d,%d\n", p, run, f, stage, e.seed,
                  e.tier, e.trick, e.mech, own, lvl, ps.target, e.dead, e.fatal, e.insights, e.steps, e.branch,
                  EG_ENEMY_PRESSURE(&e), resets, died, stage == ST_INTRO ? ps.mech_new : -1);
        if (died) {
          deaths++;
          break;
        }
        eg_plan_result(&ps, (uint8_t)resets, frames);
        saved = (uint16_t)(ps.seen & ~MK_BASIC);
      }
      skill += 0.5 + (rnd() % 50) / 100.0;
    }
  }
  if (csv) fclose(csv);
  printf("%d runs, %ld rooms (%ld runs ended in a death)\n", done, rooms, deaths);
  printf("stages: introduce %ld develop %ld combine %ld test %ld\n", stage_n[0], stage_n[1], stage_n[2], stage_n[3]);
  printf("introduce -> combine ordering: %ld violations (%.1f%% clean)\n", order_bad,
         rooms ? 100.0 * (rooms - order_bad) / rooms : 0.0);
  printf("introductions alone: %ld of %ld\n", intro_alone, intro_n);
  printf("same trick at the same level back to back: %ld\n", repeat_bad);
  printf("floor:        ");
  for (int f = 0; f < FLOORS; f++) printf("%6d", f + 1);
  printf("\nmixed share:  ");
  for (int f = 0; f < FLOORS; f++) printf("%5.0f%%", played[f] ? 100.0 * mixed[f] / played[f] : 0.0);
  printf("\nmean target:  ");
  for (int f = 0; f < FLOORS; f++) printf("%6.2f", played[f] ? (double)target_sum[f] / played[f] : 0.0);
  printf("\nmean level:   ");
  for (int f = 0; f < FLOORS; f++) printf("%6.2f", played[f] ? (double)level_sum[f] / played[f] : 0.0);
  printf("\n");
  if (sim && argc > 4) {
    FILE *j = fopen(argv[4], "w");
    if (j) {
      fprintf(j, "{\n  \"runs\": %d,\n  \"rooms\": %ld,\n  \"deaths\": %ld,\n", done, rooms, deaths);
      fprintf(j, "  \"stages\": {\"introduce\": %ld, \"develop\": %ld, \"combine\": %ld, \"test\": %ld},\n", stage_n[0],
              stage_n[1], stage_n[2], stage_n[3]);
      fprintf(j, "  \"ordering_violations\": %ld,\n  \"ordering_clean_pct\": %.2f,\n", order_bad,
              rooms ? 100.0 * (rooms - order_bad) / rooms : 0.0);
      fprintf(j, "  \"introductions\": %ld,\n  \"introductions_alone\": %ld,\n", intro_n, intro_alone);
      fprintf(j, "  \"back_to_back_repeats\": %ld,\n", repeat_bad);
      fprintf(j, "  \"mean_run_of_an_introduction\": %.2f,\n", first_intro_n ? (double)first_intro_run_sum / first_intro_n : 0.0);
      const char *names[MK_SLICE_COUNT] = {"crt", "drywall", "burn", "cold", "short", "weight", "glass", "rhythm", "steps", "rush", "steer", "mirror", "stare"};
      fprintf(j, "  \"introductions_by_mechanic\": {");
      for (int m = MK_BURN; m < MK_SLICE_COUNT; m++) fprintf(j, "%s\"%s\": %ld", m > MK_BURN ? ", " : "", names[m], intros_by_mech[m]);
      fprintf(j, "},\n");
      const char *keys[3] = {"mixed_share_by_floor", "mean_target_by_floor", "mean_level_by_floor"};
      for (int k = 0; k < 3; k++) {
        fprintf(j, "  \"%s\": [", keys[k]);
        for (int f = 0; f < FLOORS; f++) {
          double v = !played[f] ? 0.0
                     : k == 0   ? 100.0 * mixed[f] / played[f]
                     : k == 1   ? (double)target_sum[f] / played[f]
                                : (double)level_sum[f] / played[f];
          fprintf(j, "%s%.2f", f ? ", " : "", v);
        }
        fprintf(j, "],\n");
      }
      const char *gk[3] = {"runs_1_2", "runs_3_5", "runs_6_8"};
      fprintf(j, "  \"mixed_share_by_floor_and_generation\": {");
      for (int g = 0; g < 3; g++) {
        fprintf(j, "%s\"%s\": [", g ? ", " : "", gk[g]);
        for (int f = 0; f < FLOORS; f++)
          fprintf(j, "%s%.2f", f ? ", " : "", played_gen[g][f] ? 100.0 * mixed_gen[g][f] / played_gen[g][f] : 0.0);
        fprintf(j, "]");
      }
      fprintf(j, "}\n}\n");
      fclose(j);
    }
  }
  return order_bad || repeat_bad || intro_alone != intro_n ? 1 : 0;
}
