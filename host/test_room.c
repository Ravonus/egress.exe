/* Host tests for the room rules, the generator and the solver: `test_room src/gen/eg_seeds.c`.
 *
 *  - gravity: fall, stack, roll, a CRT held up by the player, a falling CRT hitting the player
 *  - the solver: a push puzzle it must solve (and the plan replays), a sealed room it must not
 *  - generation is deterministic
 *  - every seed baked into the cartridge regenerates, is not trivial (tiers > 0) and solves, and its plan replays */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eg_solve.h"

static int failures, checks;
#define CHECK(c, ...)                         \
  do {                                        \
    checks++;                                 \
    if (!(c)) {                               \
      failures++;                             \
      fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
      fprintf(stderr, __VA_ARGS__);           \
      fputc('\n', stderr);                    \
    }                                         \
  } while (0)

/* a room from text rows: # wall, : foam, . floor, O CRT, = drywall, $ token, k keycard, D door, @ start */
static void parse(eg_room *r, const char *const *rows) {
  memset(r, 0, sizeof *r);
  for (int y = 0; y < (int)EG_H; y++)
    for (int x = 0; x < (int)EG_W; x++) {
      char ch = rows[y][x];
      uint8_t i = EG_IDX(x, y), t = T_EMPTY;
      switch (ch) {
        case '#': t = T_WALL; break;
        case ':': t = T_DIRT; break;
        case 'O': t = T_ROCK; break;
        case '=': t = T_BRICK; break;
        case '$': t = T_TOKEN; break;
        case 'k': t = T_KEY; r->keys++; break;
        case 'D': t = T_DOOR; r->door = i; break;
        case '@': r->start = i; break;
        default: break;
      }
      r->c[i] = t;
    }
}

static int settle(eg_room *r, uint8_t *occ, uint8_t n) {
  int hit = 0;
  for (int k = 0; k < 64; k++) {
    uint8_t g = eg_gravity(r, occ, n);
    if (g & EG_G_HIT) hit = 1;
    if (!(g & EG_G_CHANGED)) break;
  }
  return hit;
}

static void test_gravity(void) {
  static const char *const rows[EG_H] = {
      "################", "#O.............#", "#..............#", "#....O.........#", "#....O.........#",
      "#..............#", "#..............#", "#..O...........#", "#..O...........#", "#..:...........#",
      "#..............#", "#.........O....#", "#.........O....#", "################"};
  eg_room r;
  parse(&r, rows);
  settle(&r, NULL, 0);
  CHECK((r.c[EG_IDX(1, 12)] & T_MASK) == T_ROCK, "a lone CRT falls to the floor");
  CHECK((r.c[EG_IDX(5, 12)] & T_MASK) == T_ROCK && (r.c[EG_IDX(4, 12)] & T_MASK) == T_ROCK,
        "a stack of two: the top one rolls off left");
  CHECK((r.c[EG_IDX(3, 8)] & T_MASK) == T_ROCK && (r.c[EG_IDX(3, 7)] & T_MASK) != T_ROCK,
        "a CRT on a CRT on foam: the top one rolls off, the lower stays on the foam");
  CHECK((r.c[EG_IDX(10, 11)] & T_MASK) == T_ROCK && (r.c[EG_IDX(10, 12)] & T_MASK) == T_ROCK ||
            (r.c[EG_IDX(9, 12)] & T_MASK) == T_ROCK,
        "a resting pair settles");
  for (int i = 0; i < (int)EG_N; i++) CHECK(!(r.c[i] & (F_FALL | F_MOVED)), "settled cells carry no flags (%d)", i);

  /* held up: a resting CRT does not fall into the player's cell */
  static const char *const held[EG_H] = {
      "################", "#..............#", "#....O.........#", "#....:.........#", "#..............#",
      "#..............#", "#..............#", "#..............#", "#..............#", "#..............#",
      "#..............#", "#..............#", "#..............#", "################"};
  parse(&r, held);
  r.c[EG_IDX(5, 3)] = T_EMPTY; /* the player dug the foam and stands there */
  uint8_t p = EG_IDX(5, 3);
  CHECK(!settle(&r, &p, 1) && (r.c[EG_IDX(5, 2)] & T_MASK) == T_ROCK, "the player holds the CRT up");
  p = EG_IDX(5, 4); /* stepping down from under it: it follows and hits */
  CHECK(settle(&r, &p, 1), "stepping out from under a CRT downwards gets you hit");
  /* a CRT already falling onto the player hits */
  parse(&r, held);
  r.c[EG_IDX(5, 3)] = T_EMPTY;
  p = EG_IDX(5, 8);
  CHECK(settle(&r, &p, 1), "a falling CRT hits the player below");
  CHECK((r.c[EG_IDX(5, 7)] & T_MASK) == T_ROCK, "and stops on top of them");
}

static void test_solver(void) {
  static char moves[4096];
  eg_stats st;
  eg_room r;
  /* the keycard sits behind a CRT that must be pushed into a gap */
  static const char *const push[EG_H] = {
      "##D#############", "#....#.........#", "#....#.........#", "#....#.........#", "#....#.........#",
      "#....#.........#", "#....#.........#", "#....#.........#", "#....#.........#", "#....#.........#",
      "#k...O.@.......#", "####.###########", "################", "################"};
  parse(&r, push);
  CHECK(!eg_trivial(&r), "push room is not trivial");
  int ok = eg_solve(&r, 40000, moves, sizeof moves, &st);
  CHECK(ok, "push room solves");
  CHECK(st.pushes >= 1, "push room needs a push (got %d)", st.pushes);
  CHECK(eg_replay(&r, moves), "push plan replays: %s", moves);

  static const char *const sealed[EG_H] = {
      "#####D##########", "#####:#........#", "#####O#........#", "#####O#........#", "#####O#........#",
      "######.........#", "#..............#", "#..............#", "#..............#", "#..............#",
      "#k.....@.......#", "#..............#", "#..............#", "################"};
  parse(&r, sealed);
  CHECK(!eg_solve(&r, 40000, moves, sizeof moves, &st), "a door sealed behind concrete does not solve");
  CHECK(!eg_replay(&r, "UUUUUUUUUU"), "a bogus plan does not replay");
}

static void test_determinism(void) {
  eg_room a, b;
  for (unsigned s = 1; s < 200; s += 7)
    for (int t = 0; t < 6; t++) {
      int ga = eg_generate(&a, (uint16_t)s, (uint8_t)t);
      int gb = eg_generate(&b, (uint16_t)s, (uint8_t)t);
      CHECK(ga == gb && !memcmp(&a, &b, sizeof a), "seed %u tier %d generates the same room twice", s, t);
    }
}

static void test_baked(const char *path) {
  static char moves[8192];
  FILE *f = fopen(path, "r");
  CHECK(f != NULL, "open %s", path);
  if (!f) return;
  static char text[1 << 16];
  size_t n = fread(text, 1, sizeof text - 1, f);
  fclose(f);
  text[n] = 0;
  char *p = strstr(text, "= {");
  CHECK(p != NULL, "seed table found");
  if (!p) return;
  int tier = 0, k = 0, total = 0, depth = 0;
  long steps = 0;
  for (p += 2; *p; p++) {
    if (*p == '{') {
      depth++;
      continue;
    }
    if (*p == '}') {
      if (depth == 1) break;
      if (depth == 2) {
        CHECK(k == 128, "tier %d has %d seeds", tier, k);
        tier++;
        k = 0;
      }
      depth--;
      continue;
    }
    if (depth == 2 && isdigit((unsigned char)*p)) {
      unsigned seed = (unsigned)strtoul(p, &p, 0);
      p--;
      eg_room r;
      eg_stats st;
      int g = eg_generate(&r, (uint16_t)seed, (uint8_t)tier);
      CHECK(g, "baked seed %u tier %d generates", seed, tier);
      if (!g) continue;
      if (tier > 0) CHECK(!eg_trivial(&r), "baked seed %u tier %d is a puzzle", seed, tier);
      int ok = eg_solve(&r, 40000, moves, sizeof moves, &st);
      CHECK(ok, "baked seed %u tier %d solves", seed, tier);
      if (ok) CHECK(eg_replay(&r, moves), "baked seed %u tier %d plan replays", seed, tier);
      steps += st.steps;
      k++;
      total++;
    }
  }
  CHECK(tier == 6, "six tiers (got %d)", tier);
  printf("baked seeds: %d checked, mean plan %ld moves\n", total, total ? steps / total : 0);
}

int main(int argc, char **argv) {
  test_gravity();
  test_solver();
  test_determinism();
  if (argc > 1) test_baked(argv[1]);
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
