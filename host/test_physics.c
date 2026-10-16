#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eg_solve.h"
#include "../src/core/eg_physics.h"
int main(int argc, char **argv) {
  eg_room r, alt;
  eg_stats st;
  char moves[4096];
  unsigned motif, seed, k, n = argc > 1 ? (unsigned)atoi(argv[1]) : 64u, checked = 0u;
  for (motif = PH_PUSH; motif != PH_COUNT; motif++)
    for (seed = 1; seed <= n; seed++) {
      eg_physics_gen(&r, (uint8_t)motif, (uint16_t)(seed * 7919u), (uint8_t)(seed & 8u));
      if (!eg_solve(&r, 30000, moves, sizeof moves, &st) || !eg_replay(&r, moves) || eg_trivial(&r)) {
        fprintf(stderr, "physical motif %u seed %u failed\n", motif, seed); return 1;
      }
      if ((motif == PH_PUSH && !st.pushes) || (motif == PH_DROP && !st.drops)) {
        fprintf(stderr, "motif %u seed %u bypassed its verb (push %d dig %d drop %d): %s\n", motif,seed,st.pushes,st.digs,st.drops,moves); return 1;
      }
      if (motif == PH_BRIDGE) {
        /* Prove both answers, not two cosmetic walks through the same answer. */
        if (!eg_solve_ban(&r, 30000, 1ull << RX_MUD, moves, sizeof moves, &st) ||
            !eg_replay(&r,moves) || !st.pushes || st.uses) return 2;
        alt = r;
        for (k = 0; k < EG_N; k++) if (alt.c[k] == T_ROCK) alt.c[k] = T_WALL;
        if (!eg_solve(&alt, 30000, moves, sizeof moves, &st) || !eg_replay(&alt,moves) ||
            !(st.reactions & (1ull << RX_MUD)) || !st.combines) return 3;
        /* Spending those ingredients removes the tool route; the physical fallback remains. */
        alt = r;
        for (k = 0; k < alt.nitem; k++) alt.c[alt.item_cell[k]] = T_EMPTY;
        alt.nitem = 0;
        if (!eg_solve(&alt,30000,moves,sizeof moves,&st) || !eg_replay(&alt,moves) || !st.pushes) return 4;
      }
      checked++;
    }
  printf("physical rooms: %u solver/replay checks; each bridge has independent CRT and crafted-mud routes\n",checked);
  return 0;
}
