/* Host tests for the room rules, the generator and the solver: `test_room src/gen/eg_seeds.c`.
 *
 *  - gravity: fall, stack, roll, a CRT held up by the player, a falling CRT hitting the player
 *  - the solver: a push puzzle it must solve (and the plan replays), a sealed room it must not
 *  - generation is deterministic
 *  - the matrix on tiles: HOT burns paper and the planks touching it, COLD freezes a flood, HOT thaws it, a shock
 *    through water powers a socket, a brick laid on a plate opens the shutters; recipes either way round
 *  - the solver with the pocket and the bench: a room needing a recipe, a room that can't be done without it
 *  - every seed baked into the cartridge regenerates, is not trivial, needs its trick, solves, and its plan replays */
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

/* a room from text rows: # wall, : foam, . floor, O CRT, = drywall, $ token, k keycard, D door, @ start, ~ rotten
 * carpet, _ pit, p plate, G shutter, w water, i ice, P paper, h plank, S socket, X dead shutter, W worn carpet,
 * E ceiling tile, | glass, % glitched glass; a..e pieces
 * (FIRE WATER AIR EARTH METAL), f ENERGY, g RAIN, m MUD */
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
        case '~': t = T_CRUMBLE; break;
        case '_': t = T_PIT; break;
        case 'p': t = T_PLATE; break;
        case 'G': t = T_GATE; break;
        case 'w': t = T_WATER; break;
        case 'i': t = T_ICE; break;
        case 'P': t = T_PAPER; break;
        case 'h': t = T_PLANK; break;
        case 'S': t = T_SOCKET; break;
        case 'X': t = T_DEAD; break;
        case 'W': t = T_WORN; break;
        case 'E': t = T_CEIL; break;
        case '|': t = T_GLASS; break;
        case '%': t = T_GLITCH; break;
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'm':
          t = T_ITEM;
          r->item_cell[r->nitem] = i;
          r->item_piece[r->nitem++] = (uint8_t)(ch == 'f' ? P_ENERGY : ch == 'g' ? P_RAIN : ch == 'm' ? P_MUD
                                                                                         : P_FIRE + (ch - 'a'));
          break;
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

static void test_features(void) {
  static char moves[4096];
  eg_stats st;
  eg_room r;
  /* a pit and a plate: the CRT pushed into the pit fills it; a CRT on the plate opens the shutter */
  static const char *const pit[EG_H] = {
      "################", "#..............#", "#..............#", "#..............#", "#..............#",
      "#..............#", "#..............#", "#..............#", "#..............#", "#..............#",
      "#.....O_..O.p..#", "################", "################", "################"};
  parse(&r, pit);
  CHECK(eg_push(&r, EG_IDX(6, 10), EG_IDX(7, 10)) && r.c[EG_IDX(7, 10)] == T_EMPTY && r.c[EG_IDX(6, 10)] == T_EMPTY,
        "a CRT pushed into a pit fills it");
  CHECK(eg_push(&r, EG_IDX(10, 10), EG_IDX(11, 10)) && eg_push(&r, EG_IDX(11, 10), EG_IDX(12, 10)) &&
            r.c[EG_IDX(12, 10)] == (T_ROCK | F_PLATE),
        "a CRT pushed onto a plate rests on it");
  CHECK(eg_push(&r, EG_IDX(12, 10), EG_IDX(13, 10)) && r.c[EG_IDX(12, 10)] == T_PLATE, "the plate comes back");
  /* rotten carpet gives way behind you */
  r.c[EG_IDX(3, 10)] = T_CRUMBLE;
  CHECK(eg_leave(&r, EG_IDX(3, 10)) && r.c[EG_IDX(3, 10)] == T_PIT, "rotten carpet becomes a pit");
  CHECK(!eg_leave(&r, EG_IDX(4, 10)), "plain carpet stays");

  /* the shutter puzzle: the door is behind a shutter; push the CRT onto the plate, then walk out */
  static const char *const gate[EG_H] = {
      "#######D########", "######.G.######", "######...######", "#..............#", "#..............#",
      "#..............#", "#..............#", "#..............#", "#..............#", "#..............#",
      "#..@O.p........#", "################", "################", "################"};
  parse(&r, gate);
  r.c[EG_IDX(6, 1)] = T_WALL;
  r.c[EG_IDX(8, 1)] = T_WALL;
  CHECK(!eg_trivial(&r), "the shutter room is not trivial");
  int ok = eg_solve(&r, 40000, moves, sizeof moves, &st);
  CHECK(ok && st.pushes >= 2, "the shutter room solves with pushes (%d): %s", st.pushes, moves);
  CHECK(ok && eg_replay(&r, moves), "the shutter plan replays");

  /* rotten carpet as a one-way bridge over a pit row: crossing back is impossible, so the keycard first */
  static const char *const bridge[EG_H] = {
      "#####D##########", "#....#.........#", "#....#.........#", "#....#.........#", "#....#.........#",
      "#....#.........#", "#....#.........#", "#....#.........#", "#....#.........#", "#k...~...@.....#",
      "#____#.........#", "################", "################", "################"};
  parse(&r, bridge);
  ok = eg_solve(&r, 40000, moves, sizeof moves, &st);
  CHECK(!ok, "a one-way bridge with no way back up does not solve");
  r.c[EG_IDX(4, 1)] = T_EMPTY;
  r.c[EG_IDX(5, 1)] = T_EMPTY; /* now the far side reaches the door */
  r.c[EG_IDX(5, 0)] = T_WALL;
  r.c[EG_IDX(3, 0)] = T_DOOR;
  r.door = EG_IDX(3, 0);
  ok = eg_solve(&r, 40000, moves, sizeof moves, &st);
  CHECK(ok && eg_replay(&r, moves), "over the bridge to the keycard, then out the far side: %s", moves);
}


static void test_matrix(void) {
  eg_room r;
  uint8_t p;
  /* a paper barricade touching a plank bridge: one flame takes both, the bridge leaves a pit */
  static const char *const burn[EG_H] = {
      "################", "#..............#", "#..............#", "#####PPP########", "#......h.......#",
      "#......h.......#", "#..............#", "#..............#", "#..............#", "#..............#",
      "#..............#", "#..............#", "#..............#", "################"};
  parse(&r, burn);
  p = EG_IDX(6, 4);
  CHECK(eg_apply(&r, P_WATER, EG_IDX(6, 3), &p, 1) == RX_NONE, "WATER does nothing to paper");
  CHECK(eg_apply(&r, P_FIRE, EG_IDX(6, 3), &p, 1) == RX_BURN, "FIRE burns paper");
  CHECK(r.c[EG_IDX(5, 3)] == T_EMPTY && r.c[EG_IDX(7, 3)] == T_EMPTY && r.c[EG_IDX(7, 4)] == T_PIT &&
            r.c[EG_IDX(7, 5)] == T_PIT,
        "the burn spreads through the connected paper and the planks (planks leave pits)");
  CHECK(eg_ndirty == 5 && eg_dirty[0] == EG_IDX(6, 3), "the spread lists its cells nearest first (%d)", eg_ndirty);
  parse(&r, burn);
  p = EG_IDX(7, 5);
  CHECK(eg_apply(&r, P_LAVA, EG_IDX(7, 3), &p, 1) == RX_BURN && r.c[EG_IDX(7, 5)] == T_PLANK,
        "LAVA is HOT too; the plank under the player doesn't burn");
  CHECK(eg_apply(&r, P_FIRE, EG_IDX(7, 5), &p, 1) == RX_NONE, "nothing is used on the player's own cell");

  /* a flood: COLD freezes the whole body, HOT thaws it, WET on ice does nothing */
  static const char *const flood[EG_H] = {
      "################", "#..............#", "#..wwww........#", "#..ww..........#", "#..............#",
      "#.....w........#", "#..............#", "#..............#", "#..............#", "#..............#",
      "#..............#", "#..............#", "#..............#", "################"};
  parse(&r, flood);
  p = EG_IDX(2, 2);
  CHECK(eg_apply(&r, P_RAIN, EG_IDX(3, 2), &p, 1) == RX_NONE, "RAIN on water: nothing");
  CHECK(eg_apply(&r, P_SNOW, EG_IDX(3, 2), &p, 1) == RX_FREEZE, "SNOW freezes the flood");
  CHECK(eg_count(&r, T_ICE) == 6 && r.c[EG_IDX(6, 5)] == T_WATER, "every connected water cell, no other (%d)",
        eg_count(&r, T_ICE));
  CHECK(eg_apply(&r, P_MUD, EG_IDX(3, 2), &p, 1) == RX_NONE, "WET on ice does nothing");
  CHECK(eg_apply(&r, P_FIRE, EG_IDX(3, 2), &p, 1) == RX_THAW && eg_count(&r, T_WATER) == 7, "FIRE thaws it back");

  /* the short circuit: a shock into water reaches the socket it touches; every dead shutter goes */
  static const char *const shortc[EG_H] = {
      "#######D########", "######.X.######", "#..............#", "#..............#", "Swww...........#",
      "#..............#", "#..............#", "#..............#", "#..............#", "#..............#",
      "#..............#", "#..............#", "#..............#", "################"};
  parse(&r, shortc);
  p = EG_IDX(4, 4);
  CHECK(eg_apply(&r, P_ENERGY, EG_IDX(3, 4), &p, 1) == RX_NONE, "ENERGY alone doesn't shock");
  CHECK(eg_apply(&r, P_ELECTRIC, EG_IDX(3, 4), &p, 1) == RX_SHORT, "ELECTRIC into the water: a short circuit");
  CHECK(r.c[EG_IDX(0, 4)] == T_SOCKET_ON && r.c[EG_IDX(7, 1)] == T_EMPTY, "the socket powers, the dead shutter goes");
  parse(&r, shortc);
  r.c[EG_IDX(1, 4)] = T_ICE;
  CHECK(eg_apply(&r, P_ELECTRIC, EG_IDX(3, 4), &p, 1) == RX_NONE, "ice doesn't conduct: no short");

  /* a laid brick on a plate holds the shutters open */
  static const char *const weight[EG_H] = {
      "#####p#D########", "#......G#......#", "#..............#", "#..............#", "#..............#",
      "#..............#", "#..............#", "#..............#", "#..............#", "#..............#",
      "#..............#", "#..............#", "#..............#", "################"};
  parse(&r, weight);
  p = EG_IDX(5, 1);
  CHECK(eg_apply(&r, P_EARTH, EG_IDX(5, 0), &p, 1) == RX_NONE, "EARTH is not a weight");
  CHECK(eg_apply(&r, P_BRICK, EG_IDX(5, 0), &p, 1) == RX_WEIGHT && r.c[EG_IDX(5, 0)] == T_BLOCK_PLATE, "BRICK lays");
  eg_gravity(&r, &p, 1);
  CHECK(r.c[EG_IDX(7, 1)] == T_GATE_OPEN, "and the shutter opens");
  CHECK(eg_apply(&r, P_BRICK, EG_IDX(4, 2), &p, 1) == RX_WEIGHT && r.c[EG_IDX(4, 2)] == T_BLOCK, "on carpet: a block");

  /* recipes, either order; the five-byte pieces' tags are the traits */
  CHECK(eg_recipe(P_FIRE, P_MUD) == P_BRICK && eg_recipe(P_MUD, P_FIRE) == P_BRICK, "FIRE + MUD = BRICK");
  CHECK(eg_recipe(P_AIR, P_RAIN) == P_SNOW && eg_recipe(P_WATER, P_SNOW) == P_ICE, "SNOW, ICE");
  CHECK(eg_recipe(P_METAL, P_ENERGY) == P_ELECTRIC && eg_recipe(P_FIRE, P_FIRE) == 0, "ELECTRIC; FIRE + FIRE: none");
  CHECK(PC_TAGS(P_ICE) == (TG(TAG_COLD) | TG(TAG_SHINY)) && PC_EFF(P_ELECTRIC) == EFF_SHOCK, "traits are tags");
  for (int a = 1; a < P_COUNT; a++)
    for (int b = 0; b < EG_TAGS; b++)
      for (int c = 0; c < EG_TAGS; c++) CHECK(eg_rx_table[b][c] == eg_rx_table[c][b], "the table is symmetric");
}

static void test_bench_solver(void) {
  static char moves[4096];
  eg_stats st;
  eg_room r;
  /* the flood cuts the door off; RAIN + AIR make SNOW; the plan combines on the bench and freezes it */
  static const char *const cold[EG_H] = {
      "######D#########", "#..............#", "#.....k........#", "#..............#", "#####wwww#######",
      "#..............#", "#..g...........#", "#..........c...#", "#..............#", "#.....@........#",
      "#..............#", "#..............#", "#..............#", "################"};
  parse(&r, cold);
  r.trick = TRICK_COLD;
  CHECK(!eg_trivial(&r) && eg_trick_needed(&r), "the cold room needs its trick");
  int ok = eg_solve(&r, 40000, moves, sizeof moves, &st);
  CHECK(ok && st.combines == 1 && st.uses == 1 && (st.reactions >> RX_FREEZE & 1), "it solves on the bench: %s",
        moves);
  CHECK(ok && strchr(moves, 'C') && strchr(moves, 'P') && eg_replay(&r, moves), "the plan replays");
  CHECK(!eg_solve_ban(&r, 40000, 1ull << RX_FREEZE, moves, sizeof moves, &st), "without FREEZE it can't be done");
  CHECK(!eg_replay(&r, "UUUUUUUCgcPiU"), "a bench act with pieces not in the pocket doesn't replay");
  /* only one piece: no recipe */
  r.c[EG_IDX(11, 7)] = T_EMPTY;
  CHECK(!eg_solve(&r, 40000, moves, sizeof moves, &st), "half a recipe doesn't solve");
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


/* fragile floor: steps wear a tile through its crack stages to a pit; glass takes one crossing; a pushed or falling
 * CRT smashes glass; protected cells refuse a mob; the dead-state detector; the solver plans the order */
static void test_fragile(void) {
  eg_room r;
  static char moves[8192];
  eg_stats st;
  static const char *const rows[EG_H] = {
      "#######D########",
      "#..............#",
      "#..............#",
      "#______|_______#",
      "#..............#",
      "#..........k...#",
      "#..............#",
      "#..............#",
      "#..............#",
      "#..............#",
      "#..............#",
      "#.......@......#",
      "#..............#",
      "################",
  };
  parse(&r, rows);
  uint8_t g = EG_IDX(7, 3);
  CHECK(eg_frag_kind_of(T_WORN_2) == FK_WORN && eg_frag_kind_of(T_ICE_1) == FK_ICE, "kinds by id");
  CHECK(eg_frag[FK_WORN].base == T_WORN && eg_frag[FK_CEIL].base == T_CEIL && eg_frag[FK_ICE].base == T_ICE,
        "stage 0 ids are the kinds' bases");
  /* one-way glass: the solver takes the keycard before it crosses */
  int ok = eg_solve(&r, 20000, moves, sizeof moves, &st);
  CHECK(ok && eg_replay(&r, moves), "one-way glass room solves (%s)", moves);
  CHECK(!eg_trivial(&r), "the glass bridge is the puzzle");
  r.trick = TRICK_GLASS;
  CHECK(eg_trick_needed(&r), "glass needed");
  /* A short hole now has a native roll-jump answer; do not falsely ask for a reset. */
  eg_room t = r;
  CHECK(eg_step_off(&t, g, BB_PLAYER, 1) && t.c[g] == T_PIT, "glass breaks on the first step off");
  CHECK(eg_dead(&t, EG_IDX(7, 2), NULL, 0) == 0, "short gap permits a roll back to the keycard");
  CHECK(eg_dead(&t, EG_IDX(7, 5), NULL, 0) == 0, "short gap permits a roll toward the door");
  CHECK(eg_dead(&r, r.start, NULL, 0) == 0, "the start is not dead");
  /* worn carpet: three crossings, stage = base + steps */
  t = r;
  t.c[g] = T_WORN;
  eg_step_off(&t, g, BB_PLAYER, 1);
  CHECK(t.c[g] == T_WORN_1, "worn: first step");
  eg_step_off(&t, g, BB_PLAYER, 2); /* HEAVY counts 2 */
  CHECK(t.c[g] == T_PIT, "worn: a heavy step finishes it");
  t.c[g] = T_GLASS;
  CHECK(!eg_step_off(&t, g, BB_MOB_WALKER, 1) && t.c[g] == T_GLASS, "steps count only for what can break the kind");
  t.c[g] = T_CEIL;
  CHECK(eg_frag_hit(&t, g, BB_REACTION) && t.c[g] == T_PIT, "a reaction breaks a ceiling tile");
  /* protected cells refuse mobs; glass refuses walkers */
  t = r;
  t.prot[g >> 3] |= (uint8_t)(1u << (g & 7u));
  CHECK(!eg_frag_hit(&t, g, BB_MOB_HEAVY) && t.c[g] == T_GLASS, "protected: a mob can't break it");
  t = r;
  CHECK(!eg_frag_hit(&t, g, BB_MOB_WALKER) && eg_frag_hit(&t, g, BB_MOB_HEAVY), "glass: walkers no, heavies yes");
  /* a pushed CRT smashes glass and falls through */
  t = r;
  t.c[EG_IDX(6, 3)] = T_ROCK;
  CHECK(eg_push(&t, EG_IDX(6, 3), g) && t.c[g] == T_PIT && t.c[EG_IDX(6, 3)] == T_EMPTY, "push through glass");
  /* a falling CRT too */
  t = r;
  t.c[EG_IDX(7, 1)] = T_ROCK;
  t.c[EG_IDX(7, 2)] = T_EMPTY;
  for (int k = 0; k < 8; k++) eg_gravity(&t, NULL, 0);
  CHECK(t.c[g] == T_PIT && (t.c[EG_IDX(7, 2)] & T_MASK) != T_ROCK, "a falling CRT goes through glass");
  /* a reachable CRT can fill a pit: not dead */
  t = r;
  eg_step_off(&t, g, BB_PLAYER, 1);
  t.c[EG_IDX(3, 2)] = T_ROCK;
  CHECK(eg_dead(&t, EG_IDX(7, 2), NULL, 0) == 0, "a CRT in reach might fill the pit: not called dead");
  t.c[EG_IDX(3, 2)] = T_EMPTY;
  t.c[EG_IDX(3, 8)] = T_ROCK;
  CHECK(eg_dead(&t, EG_IDX(7, 5), NULL, 0) == 0, "a CRT in reach might fill the pit: not called dead");
  /* glitched runs */
  t = r;
  t.c[g] = T_GLITCH;
  CHECK(eg_glitch_ok(&t), "one glitched cell fits a beat");
  t.c[EG_IDX(7, 2)] = t.c[EG_IDX(7, 4)] = t.c[EG_IDX(7, 5)] = T_GLITCH;
  CHECK(!eg_glitch_ok(&t), "four in a run don't");
  /* thin ice: freeze, then two crossings */
  t = r;
  t.c[g] = T_WATER;
  uint8_t occ = r.start;
  CHECK(eg_apply(&t, P_SNOW, g, &occ, 1) == RX_FREEZE && t.c[g] == T_ICE, "COLD makes thin ice");
  eg_step_off(&t, g, BB_PLAYER, 1);
  CHECK(t.c[g] == T_ICE_1, "thin ice cracks");
  CHECK(eg_apply(&t, P_FIRE, g, &occ, 1) == RX_THAW && t.c[g] == T_WATER, "cracked ice thaws too");
  /* step routing: a 2-step ceiling tile in a wall must be crossed at most twice; the solver keeps one for the way
   * back */
  static const char *const rows2[EG_H] = {
      "#######D########",
      "#..............#",
      "#..............#",
      "##############E#",
      "#..............#",
      "#..............#",
      "#####E##########",
      "#..............#",
      "#..k...........#",
      "#..............#",
      "#..............#",
      "#.......@......#",
      "#..............#",
      "################",
  };
  parse(&r, rows2);
  ok = eg_solve(&r, 20000, moves, sizeof moves, &st);
  CHECK(ok && eg_replay(&r, moves), "ceiling-tile room solves (%s)", moves);
  /* wearing the upper tile through before the keycard is a fatal mistake the detector sees */
  eg_room t2 = r;
  t2.c[EG_IDX(14, 3)] = T_PIT;
  CHECK(eg_dead(&t2, EG_IDX(8, 8), NULL, 0) == 0, "one worn-through tile leaves a roll-jump route");
}

static void test_baked(const char *path) {
  static char moves[8192];
  FILE *f = fopen(path, "r");
  CHECK(f != NULL, "open %s", path);
  if (!f) return;
  int tier = 0, total = 0, tricks[6][TRICK_COUNT] = {{0}}, per_tier[6] = {0};
  long diff[6] = {0}, foam_sum = 0, objects_sum = 0, dead_sum = 0, frag_rooms = 0, glass_rooms = 0,
       glitch_rooms = 0;
  int foam_max = 0, fatal_n = 0;
  long fatal_sum = 0;
  long steps = 0;
  char line[256];
  while (fgets(line, sizeof line, f)) {
    unsigned seed, mech;
    int rtier, rtrick, rdiff, rdead, rfatal, rins, rsteps, rbranch;
    if (sscanf(line, " {%u, %d, %d, %i, %d, %d, %d, %d, %d, %d}", &seed, &rtier, &rtrick, &mech, &rdiff, &rdead,
               &rfatal, &rins, &rsteps, &rbranch) != 10)
      continue;
    tier = rtier;
    if (tier < 0 || tier > 5) continue;
    per_tier[tier]++;
    {
      eg_room r;
      eg_stats st;
      int g = eg_generate(&r, (uint16_t)seed, (uint8_t)tier);
      CHECK(g, "baked seed %u tier %d generates", seed, tier);
      if (!g) continue;
      CHECK(r.trick == rtrick && eg_room_mech(&r) == mech, "baked seed %u tier %d: its record matches (trick %d/%d "
            "mech 0x%x/0x%x)", seed, tier, r.trick, rtrick, eg_room_mech(&r), mech);
      CHECK(eg_glitch_ok(&r), "baked seed %u tier %d: glitched runs fit a beat", seed, tier);
      {
        int bad = 0, fr = 0;
        for (int i = 0; i < (int)EG_N; i++) {
          uint8_t t = r.c[i] & T_MASK;
          if (EG_PROT(&r, i) && EG_FRAGILE(t)) bad++;
          if (EG_FRAGILE(t) || t == T_GLITCH) fr = 1;
          if (t == T_GLASS) glass_rooms += !(fr & 2), fr |= 2;
          if (t == T_GLITCH) glitch_rooms += !(fr & 4), fr |= 4;
        }
        CHECK(!bad, "baked seed %u tier %d: %d protected cells are fragile", seed, tier, bad);
        frag_rooms += fr != 0;
        CHECK(eg_dead(&r, r.start, NULL, 0) == 0, "baked seed %u tier %d: not dead at the start", seed, tier);
      }
      dead_sum += rdead;
      if (rfatal != 255) fatal_sum += rfatal, fatal_n++;
      CHECK(!eg_trivial(&r), "baked seed %u tier %d is a puzzle", seed, tier);
      CHECK(eg_trick_needed(&r), "baked seed %u tier %d needs its trick (%s)", seed, tier, eg_trick_name[r.trick]);
      CHECK(r.nitem <= EG_MAX_ITEM, "baked seed %u tier %d fits the pocket", seed, tier);
      int ok = eg_solve(&r, 40000, moves, sizeof moves, &st);
      CHECK(ok, "baked seed %u tier %d solves", seed, tier);
      if (ok) CHECK(eg_replay(&r, moves), "baked seed %u tier %d plan replays", seed, tier);
      steps += st.steps;
      tricks[tier][r.trick]++;
      /* floor first (the approved look): foam in patches under 25% of the interior, carpet the commonest cell */
      int count[T_TYPES] = {0}, objects = 0;
      for (int y = 1; y < (int)EG_H - 1; y++)
        for (int x = 1; x < (int)EG_W - 1; x++) count[r.c[EG_IDX(x, y)] & T_MASK]++;
      int foam = count[T_DIRT] * 1000 / ((EG_W - 2) * (EG_H - 2)), commonest = 0;
      for (int t = 0; t < (int)T_TYPES; t++)
        if (t != T_WALL && count[t] > count[commonest]) commonest = t;
      CHECK(foam < 250, "baked seed %u tier %d: foam %d.%d%% of the floor", seed, tier, foam / 10, foam % 10);
      CHECK(commonest == T_EMPTY, "baked seed %u tier %d: carpet is the commonest cell (%d)", seed, tier, commonest);
      for (int t = 0; t < (int)T_TYPES; t++)
        if (t != T_EMPTY && t != T_DIRT && t != T_WALL) objects += count[t];
      foam_sum += foam;
      if (foam > foam_max) foam_max = foam;
      objects_sum += objects;
      diff[tier] += eg_difficulty(&r, &st);
      total++;
    }
  }
  fclose(f);
  for (int t = 0; t < 6; t++) CHECK(per_tier[t] == 128, "tier %d has %d seeds", t, per_tier[t]);
  printf("baked seeds: %d checked, mean plan %ld moves\n", total, total ? steps / total : 0);
  if (total)
    printf("  foam: mean %.1f%% max %.1f%% of the interior; objects (everything but carpet, foam, concrete): mean "
           "%.1f per room\n", foam_sum / 10.0 / total, foam_max / 10.0, (double)objects_sum / total);
  if (total)
    printf("  fragile floor in %ld rooms (glass %ld, glitched glass %ld); dead-end share mean %.1f%%; a fatal mistake "
           "possible after %.1f acts on average (%d rooms)\n", frag_rooms, glass_rooms, glitch_rooms,
           (double)dead_sum / total, fatal_n ? (double)fatal_sum / fatal_n : 0.0, fatal_n);
  for (int t = 0; t < 6; t++)
    printf("  tier %d: burn %d cold %d short %d weight %d glass %d rhythm %d, mean difficulty %ld\n", t,
           tricks[t][0], tricks[t][1], tricks[t][2], tricks[t][3], tricks[t][4], tricks[t][5], diff[t] / 128);
}

int main(int argc, char **argv) {
  test_gravity();
  test_solver();
  test_features();
  test_matrix();
  test_bench_solver();
  test_determinism();
  test_fragile();
  if (argc > 1) test_baked(argv[1]);
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
