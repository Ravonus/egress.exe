/* egtool: the host side of EGRESS rooms.
 *   egtool show SEED TIER        the generated room as text (with its trick, pieces and checks)
 *   egtool solve SEED TIER       the solver's plan (one line of moves), stats on stderr
 *   egtool dump SEED TIER        the room's 224 cells as hex, then start door keys tokens spawns items trick
 *                                (compared against the cartridge by the harness)
 *   egtool accept SEED TIER      validate one curriculum candidate; print its bake record on success
 *   egtool tier TIER COUNT [FIRST STRIDE]  the first COUNT seeds the tier accepts, trying FIRST, FIRST+STRIDE, ...
 *                                (stdout is flushed per seed)
 *   egtool survey TIER COUNT     one line per seed: trick, solve stats, difficulty, checks, accepted
 *   egtool walls < "SEED TIER" lines   the wall-structure metric (host/eg_walls.c) per room, then the totals */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eg_solve.h"
#include "eg_walls.h"
#include "../src/core/eg_obst.h"

#define TIERS 6
#ifndef BUDGET
#define BUDGET 30000
#endif

static char glyph(uint8_t v) {
  static const char g[T_TYPES + 1] = ".:#=O$kD~_pGgw%Ph*SsXBbWVUEF|IJY";
  uint8_t t = v & T_MASK;
  return t < T_TYPES ? g[t] : '?';
}

static void show(const eg_room *r) {
  for (int y = 0; y < (int)EG_H; y++) {
    for (int x = 0; x < (int)EG_W; x++) {
      int i = y * EG_W + x;
      char ch = glyph(r->c[i]);
      if ((r->c[i] & T_MASK) == T_ITEM) ch = (char)('a' + eg_item_at(r, (uint8_t)i) - 1);
      if (i == r->start) ch = '@';
      for (int k = 0; k < r->nob; k++)
        if (r->ob[k][1] == i) ch = (char)('1' + k); /* B2: obstacle slots */
      putchar(ch);
    }
    putchar('\n');
  }
}

/* the tier's bounds on the 3.2 difficulty score, and what every accepted room must pass */
static const int diff_lo[TIERS] = {20, 40, 60, 70, 85, 95};
static const int diff_hi[TIERS] = {110, 150, 200, 220, 240, 260};

static int checks(int tier, const eg_room *r, const eg_stats *st, const char **why) {
  int d = eg_difficulty(r, st);
  if (r->nob && (!st->ob_start_live || !st->ob_idle_ok || st->ob_idle_hits || !st->ob_plan_ok || st->ob_hits))
    return *why = "obstacle-fairness", 0;
  if (st->steps > 320) return *why = "long", 0;
  if (eg_trivial(r)) return *why = "trivial", 0;
  if (!eg_trick_needed(r)) return *why = "trick-skippable", 0;
  if (tier > 0 && r->trick != TRICK_BURN && r->trick < TRICK_GLASS && st->combines < 1) return *why = "no-bench", 0;
  if (!eg_glitch_ok(r)) return *why = "glitch-run", 0;
  for (int i = 0; i < (int)EG_N; i++)
    if (EG_PROT(r, i) && EG_FRAGILE(r->c[i] & T_MASK)) return *why = "protected-fragile", 0;
  /* the void tricks are about order, not length: their band starts lower */
  int lo = r->trick >= TRICK_GLASS ? diff_lo[tier] * 2 / 3 : diff_lo[tier];
  if (d < lo || d > diff_hi[tier]) return *why = "out-of-band", 0;
  int a, b;
  if (r->herrings && !eg_herrings_safe(r, BUDGET, &a, &b)) return *why = "herring-unsafe", 0;
  return *why = "ok", 1;
}

static void piece_list(const eg_room *r, FILE *f) {
  char name[16];
  for (int k = 0; k < r->nitem; k++) {
    eg_piece_name(r->item_piece[k], name);
    fprintf(f, "%s%c=%s%s", k ? " " : "", 'a' + r->item_piece[k] - 1, name,
            k >= r->nitem - r->herrings ? "(herring)" : "");
  }
}

int main(int argc, char **argv) {
  static char moves[8192];
  eg_room r;
  eg_stats st;
  if (argc >= 4 && (!strcmp(argv[1], "show") || !strcmp(argv[1], "solve") || !strcmp(argv[1], "dump") ||
                    !strcmp(argv[1], "accept"))) {
    unsigned seed = (unsigned)strtoul(argv[2], NULL, 0);
    int tier = atoi(argv[3]);
    if (!eg_generate(&r, (uint16_t)seed, (uint8_t)tier)) {
      fprintf(stderr, "generation failed\n");
      return 2;
    }
    if (!strcmp(argv[1], "show")) {
      show(&r);
      printf("trick %s support %d herrings %d depth %d keys %d tokens %d spawns %d trivial %d needed %d mech 0x%03x\n"
             "pieces: ",
             eg_trick_name[r.trick], r.support, r.herrings, r.depth, r.keys, r.tokens, r.nspawn, eg_trivial(&r),
             eg_trick_needed(&r), eg_room_mech(&r));
      piece_list(&r, stdout);
      printf("\n");
      for (int k = 0; k < r.nob; k++) /* B2: the obstacle records and the route */
        printf("obstacle %d: rule %d %s%s%s dir %d variant %d arg %d%s\n", k + 1, OBA_RULE(r.ob[k][0]),
               (r.ob[k][0] & OBA_P2) ? "period 2" : "period 1", (r.ob[k][3] & OBP_PHASE) ? " (odd)" : "",
               (r.ob[k][0] & OBA_LEFT) ? " left-handed" : "", OBB_DIR(r.ob[k][2]), OBB_VAR(r.ob[k][2]),
               OBP_ARG(r.ob[k][3]), (r.ob[k][2] & OBB_ASLEEP) ? " asleep" : "");
      if (r.nrun) {
        printf("route:");
        for (int k = 0; k < r.nrun; k++) printf(" %c%d", "NESW"[r.run[k] >> 6], r.run[k] & 63);
        printf("\n");
      }
      return 0;
    }
    if (!strcmp(argv[1], "dump")) {
      for (int i = 0; i < (int)EG_N; i++) printf("%02x", r.c[i]);
      printf(" %d %d %d %d %d", r.start, r.door, r.keys, r.tokens, r.nspawn);
      for (int s = 0; s < r.nspawn; s++) printf(" %d:%d", r.spawn[s], r.spawn_kind[s]);
      printf(" items");
      for (int k = 0; k < r.nitem; k++) printf(" %d:%d", r.item_cell[k], r.item_piece[k]);
      printf(" trick %d %s\n", r.trick, eg_trick_name[r.trick]);
      return 0;
    }
    int ok = eg_solve(&r, argc > 4 ? atoi(argv[4]) : BUDGET, moves, sizeof moves, &st);
    fprintf(stderr,
            "solved %d nodes %d acts %d steps %d pushes %d breaks %d digs %d drops %d picks %d combines %d uses %d "
            "branching %.2f difficulty %d trick %s states %d dead %d fatal-depth %d\n",
            ok, st.nodes, st.actions, st.steps, st.pushes, st.breaks, st.digs, st.drops, st.picks, st.combines, st.uses,
            st.branching100 / 100.0, eg_difficulty(&r, &st), eg_trick_name[r.trick], st.states, st.dead,
            st.fatal_depth);
    if (!ok) return 1;
    if (!strcmp(argv[1], "accept")) {
      const char *why;
      if (!checks(tier, &r, &st, &why)) {
        fprintf(stderr, "rejected: %s\n", why);
        return 1;
      }
      printf("%u %d 0x%03x %d %d %d %d %d %d\n", seed, r.trick, eg_room_mech(&r), eg_difficulty(&r, &st),
             st.states ? st.dead * 100 / st.states : 0, st.fatal_depth, eg_insights(&r), st.steps,
             st.branching100);
      return 0;
    }
    printf("%s\n", moves);
    return 0;
  }
  if (argc >= 4 && (!strcmp(argv[1], "tier") || !strcmp(argv[1], "survey"))) {
    int survey = !strcmp(argv[1], "survey");
    int tier = atoi(argv[2]), per = atoi(argv[3]);
    unsigned first = argc > 4 ? (unsigned)atoi(argv[4]) : 1u, stride = argc > 5 ? (unsigned)atoi(argv[5]) : 1u;
    int got = 0, tried = 0, gen_fail = 0, unsolved = 0, rejected = 0, by_trick[TRICK_COUNT] = {0};
    long steps = 0, diff = 0;
    for (unsigned seed = first; (survey ? tried : got) < per && seed < 65536; seed += stride) {
      tried++;
      if (!eg_generate(&r, (uint16_t)seed, (uint8_t)tier)) {
        gen_fail++;
        if (survey) printf("%u genfail\n", seed);
        continue;
      }
      /* Reject structurally irrelevant rooms before exploring millions of tick states. */
      if (!survey && (eg_trivial(&r) || !eg_trick_needed(&r))) { rejected++; continue; }
      if (eg_sealed(&r) || !eg_solve(&r, BUDGET, moves, sizeof moves, &st)) {
        unsolved++;
        if (survey) printf("%u %s unsolved nodes %d\n", seed, eg_trick_name[r.trick], st.nodes);
        continue;
      }
      const char *why;
      int ok = checks(tier, &r, &st, &why);
      if (survey)
        printf("%u %s sup %d herr %d depth %d steps %d acts %d combines %d uses %d branch %.2f diff %d nodes %d dead "
               "%d/%d fatal %d %s\n",
               seed, eg_trick_name[r.trick], r.support, r.herrings, r.depth, st.steps, st.actions, st.combines,
               st.uses, st.branching100 / 100.0, eg_difficulty(&r, &st), st.nodes, st.dead, st.states,
               st.fatal_depth, why);
      if (!ok) {
        rejected++;
        continue;
      }
      steps += st.steps;
      diff += eg_difficulty(&r, &st);
      by_trick[r.trick]++;
      /* the bake record: seed trick mechanics difficulty dead-share% fatal-depth insights steps branching% */
      if (!survey)
        printf("%u %d 0x%03x %d %d %d %d %d %d\n", seed, r.trick, eg_room_mech(&r), eg_difficulty(&r, &st),
               st.states ? st.dead * 100 / st.states : 0, st.fatal_depth, eg_insights(&r), st.steps,
               st.branching100);
      fflush(stdout);
      got++;
    }
    fprintf(stderr,
            "tier %d: %d accepted of %d (gen-fail %d unsolved %d rejected %d) mean steps %ld difficulty %ld; "
            "burn %d cold %d short %d weight %d glass %d rhythm %d\n",
            tier, got, tried, gen_fail, unsolved, rejected, got ? steps / got : 0, got ? diff / got : 0, by_trick[0],
            by_trick[1], by_trick[2], by_trick[3], by_trick[4], by_trick[5]);
    return (!survey && got < per) ? 3 : 0;
  }
  if (argc >= 2 && !strcmp(argv[1], "walls")) { /* B2 part B: connected wall components, singleton share */
    unsigned seed;
    int tier, rooms = 0;
    long cells = 0, comps = 0, single = 0, sfloat = 0, small = 0, anch = 0;
    while (scanf("%u %d", &seed, &tier) == 2) {
      eg_wall_stats w;
      if (!eg_generate(&r, (uint16_t)seed, (uint8_t)tier)) continue;
      eg_walls(&r, &w);
      printf("%u %d cells %d comps %d single %d float %d small %d anchored %d\n", seed, tier, w.cells, w.comps,
             w.single, w.single_float, w.small, w.anchored);
      rooms++, cells += w.cells, comps += w.comps, single += w.single, sfloat += w.single_float, small += w.small;
      anch += w.anchored;
    }
    printf("rooms %d wall-cells/room %.1f components/room %.2f cells/component %.2f singleton-share %.1f%% "
           "floating-singleton-share %.1f%% small(<=2)-share %.1f%% anchored-share %.1f%%\n",
           rooms, rooms ? (double)cells / rooms : 0, rooms ? (double)comps / rooms : 0,
           comps ? (double)cells / comps : 0, comps ? 100.0 * single / comps : 0, comps ? 100.0 * sfloat / comps : 0,
           comps ? 100.0 * small / comps : 0, comps ? 100.0 * anch / comps : 0);
    return 0;
  }
  fprintf(stderr, "usage: egtool show|solve|dump SEED TIER | tier TIER COUNT | survey TIER COUNT\n");
  return 2;
}
