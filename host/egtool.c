/* egtool: the host side of EGRESS rooms.
 *   egtool show SEED TIER        the generated room as text
 *   egtool solve SEED TIER       the solver's plan (one line of moves), stats on stderr
 *   egtool dump SEED TIER        the room's 224 cells as hex (compared against the cartridge by the harness)
 *   egtool tier TIER COUNT [FIRST STRIDE]  the first COUNT seeds the tier accepts (generate, reject trivial, solve),
 *                                trying FIRST, FIRST+STRIDE, ... (stdout is flushed per seed) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eg_solve.h"

#define TIERS 6
#define BUDGET 20000

static const char glyph[8] = {'.', ':', '#', '=', 'O', '$', 'k', 'D'};

static void show(const eg_room *r) {
  for (int y = 0; y < (int)EG_H; y++) {
    for (int x = 0; x < (int)EG_W; x++) {
      int i = y * EG_W + x;
      char ch = glyph[r->c[i] & 7];
      if (i == r->start) ch = '@';
      for (int s = 0; s < r->nspawn; s++)
        if (r->spawn[s] == i) ch = r->spawn_kind[s] ? 'E' : 'M';
      putchar(ch);
    }
    putchar('\n');
  }
}

/* what a tier asks of its accepted rooms */
static int accept(int tier, const eg_room *r, const eg_stats *st) {
  int acts = st->pushes + st->breaks + st->drops;
  if (st->steps > 260) return 0;
  if (tier == 0) return acts >= 1;
  if (eg_trivial(r)) return 0;
  return acts >= 1 + tier && st->steps >= 30 + 6 * tier;
}

int main(int argc, char **argv) {
  static char moves[8192];
  eg_room r;
  eg_stats st;
  if (argc >= 4 && (!strcmp(argv[1], "show") || !strcmp(argv[1], "solve") || !strcmp(argv[1], "dump"))) {
    unsigned seed = (unsigned)strtoul(argv[2], NULL, 0);
    int tier = atoi(argv[3]);
    if (!eg_generate(&r, (uint16_t)seed, (uint8_t)tier)) {
      fprintf(stderr, "generation failed\n");
      return 2;
    }
    if (!strcmp(argv[1], "show")) {
      show(&r);
      printf("keys %d tokens %d spawns %d trivial %d\n", r.keys, r.tokens, r.nspawn, eg_trivial(&r));
      return 0;
    }
    if (!strcmp(argv[1], "dump")) {
      for (int i = 0; i < (int)EG_N; i++) printf("%02x", r.c[i]);
      printf(" %d %d %d %d %d", r.start, r.door, r.keys, r.tokens, r.nspawn);
      for (int s = 0; s < r.nspawn; s++) printf(" %d:%d", r.spawn[s], r.spawn_kind[s]);
      printf("\n");
      return 0;
    }
    int ok = eg_solve(&r, argc > 4 ? atoi(argv[4]) : BUDGET, moves, sizeof moves, &st);
    fprintf(stderr, "solved %d nodes %d acts %d steps %d pushes %d breaks %d digs %d drops %d\n", ok, st.nodes,
            st.actions, st.steps, st.pushes, st.breaks, st.digs, st.drops);
    if (!ok) return 1;
    printf("%s\n", moves);
    return 0;
  }
  if (argc >= 4 && !strcmp(argv[1], "tier")) {
    /* accepted seeds for one tier, one per line (tools/bake_seeds.sh runs the tiers in parallel) */
    int tier = atoi(argv[2]), per = atoi(argv[3]);
    unsigned first = argc > 4 ? (unsigned)atoi(argv[4]) : 1u, stride = argc > 5 ? (unsigned)atoi(argv[5]) : 1u;
    int got = 0, tried = 0, gen_fail = 0, trivial = 0, unsolved = 0, rejected = 0;
    long steps = 0;
    for (unsigned seed = first; got < per && seed < 65536; seed += stride) {
      tried++;
      if (!eg_generate(&r, (uint16_t)seed, (uint8_t)tier)) {
        gen_fail++;
        continue;
      }
      if (tier > 0 && eg_trivial(&r)) {
        trivial++;
        continue;
      }
      if (eg_sealed(&r) || !eg_solve(&r, BUDGET, moves, sizeof moves, &st)) {
        unsolved++;
        continue;
      }
      if (!accept(tier, &r, &st)) {
        rejected++;
        continue;
      }
      steps += st.steps;
      printf("%u\n", seed);
      fflush(stdout);
      got++;
    }
    fprintf(stderr, "tier %d: %d accepted of %d (gen-fail %d trivial %d unsolved %d out-of-range %d) mean steps %ld\n",
            tier, got, tried, gen_fail, trivial, unsolved, rejected, got ? steps / got : 0);
    return got < per ? 3 : 0;
  }
  fprintf(stderr, "usage: egtool show|solve|dump SEED TIER | tier TIER COUNT\n");
  return 2;
}
