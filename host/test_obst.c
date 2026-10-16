/* B2 living obstacles: the core rules (src/core/eg_obst.c) and the tick solver (host/eg_osolve.c), checked against
 * the design-stage model of the office mock room (tools/mock/obstacle_room.py: its numbers are in redesign.md 7.7),
 * plus one small room per behaviour. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eg_osolve.h"

static int fails;
#define CHECK(c, msg)                                                                                               \
  do {                                                                                                              \
    if (!(c)) {                                                                                                     \
      printf("FAIL %s (%s:%d)\n", msg, __FILE__, __LINE__);                                                         \
      fails++;                                                                                                      \
    }                                                                                                               \
  } while (0)

/* rows of '#' wall, '.' carpet, 'G' partition, 'g' floor glass, 'o' pit, 'E' door, 'B' crate, '_' plate, 'k' key,
 * 'S' start; the rest of the 16x14 room is concrete */
static void room_from(eg_room *r, const char *const *rows, int h) {
  memset(r, 0, sizeof *r);
  for (int i = 0; i < (int)EG_N; i++) r->c[i] = T_WALL;
  for (int y = 0; y < h; y++)
    for (int x = 0; rows[y][x]; x++) {
      uint8_t i = EG_IDX(x, y), t = T_WALL;
      switch (rows[y][x]) {
        case '.': t = T_EMPTY; break;
        case 'G': t = T_PARTITION; break;
        case 'g': t = T_GLASS; break;
        case 'o': t = T_PIT; break;
        case 'E': t = T_DOOR, r->door = i; break;
        case 'B': t = T_BLOCK; break;
        case '_': t = T_PLATE; break;
        case '=': t = T_GATE; break;
        case 'k': t = T_KEY, r->keys++; break;
        case 'S': t = T_EMPTY, r->start = i; break;
        case 'w': t = T_WORN; break;
      }
      r->c[i] = t;
    }
}

static void add_ob(eg_room *r, uint8_t rule, uint8_t flags, int x, int y, uint8_t dir, uint8_t var, uint8_t arg) {
  uint8_t *o = r->ob[r->nob++];
  o[0] = (uint8_t)(rule | flags);
  o[1] = EG_IDX(x, y);
  o[2] = (uint8_t)(dir | (var << 2));
  o[3] = arg;
}

static const char *const MOCK[] = {
    "#######E##", "#....##g##", "#....G...#", "#....#####", "#....#####", "#....#####", "##########",
};

static void mock_room(eg_room *r) {
  room_from(r, MOCK, 7);
  r->start = EG_IDX(3, 4);
  r->c[EG_IDX(2, 3)] = T_BLOCK; /* the crate */
  r->c[EG_IDX(3, 3)] = T_BLOCK; /* the CRT (the mock pushes it like a crate) */
  add_ob(r, OB_HEAVY, OBA_P2, 1, 5, 0, 0, 0);
}

/* the mock's N E S W actions as the solver's moves */
static void mock_plan(const char *m, char *out) {
  for (; *m; m++) *out++ = *m == 'N' ? 'U' : *m == 'E' ? 'R' : *m == 'S' ? 'D' : *m == 'W' ? 'L' : '.';
  *out = 0;
}

static char plan[4096];

static void test_mock(void) {
  eg_room r;
  eg_ostats st;
  mock_room(&r);
  int ok = eg_osolve(&r, 4000000, EGO_ORDERED, plan, sizeof plan, &st);
  printf("mock room: states %d live %d dead %d (%.1f%%) fatal tick %d rescue %d passive %d plan %s (%d ticks, %d hits)\n",
         st.states, st.live, st.dead, 100.0 * st.dead / st.states, st.fatal_depth, st.rescue, st.passive, plan,
         st.ticks, st.hits);
  CHECK(ok, "the mock room is solved");
  CHECK(st.states == 482274, "the mock's 482,274 states (redesign.md 7.7)");
  CHECK(st.dead * 1000 / st.states == 317, "31.7% dead");
  CHECK(st.fatal_depth == 7, "the first fatal move at tick 7");
  CHECK(st.rescue == 140, "140 rescue-window states");
  CHECK(st.start_live && st.idle_ok, "fairness 1 and 2");
  CHECK(st.ticks == 24 && st.hits == 0, "24 ticks, no hit");
  char m[128];
  int h = -1;
  mock_plan(".WNNENW.........EESEEENN", m);
  CHECK(eg_oreplay(&r, m, EGO_ORDERED, &h) && h == 0, "the drawn solution replays");
  mock_plan(".WNNENW.........EESSSWNWNEEEW................", m);
  CHECK(!eg_oreplay(&r, m, EGO_ORDERED, &h), "the drawn mistake does not win");
}

/* one tick by hand */
static eg_otick tick_at(eg_room *r, eg_ob *ob, uint8_t p, uint8_t pp, uint8_t face, uint8_t n) {
  eg_otick t;
  memset(&t, 0, sizeof t);
  t.p = p, t.pp = pp, t.face = face, t.tick = n;
  eg_ob_tick(r, ob, &t);
  return t;
}

static void test_rules(void) {
  eg_room r;
  eg_ob ob[EG_MAX_OB];
  eg_otick t;
  /* the patroller: a 2x2 loop; it reverses at a crate and holds; it shoves the player along its path */
  static const char *const LOOP[] = {"#######", "#.....#", "#.....#", "#.....#", "#######"};
  room_from(&r, LOOP, 5);
  r.nrun = 4;
  r.run[0] = (1u << 6) | 2u, r.run[1] = (2u << 6) | 2u, r.run[2] = (3u << 6) | 2u, r.run[3] = (0u << 6) | 2u;
  add_ob(&r, OB_PATROL, 0, 1, 1, 0, 0, 0);
  eg_ob_init(&r, ob);
  CHECK(r.c[EG_IDX(1, 1)] == (T_EMPTY | F_OB), "F_OB marks its cell");
  t = tick_at(&r, ob, EG_IDX(5, 3), EG_IDX(5, 3), 0, 0);
  CHECK(ob[0].cell == EG_IDX(2, 1) && ob[0].pos == 1, "patroller step 1");
  r.c[EG_IDX(3, 1)] = T_BLOCK;
  t = tick_at(&r, ob, EG_IDX(5, 3), EG_IDX(5, 3), 0, 1);
  CHECK(ob[0].cell == EG_IDX(2, 1) && (ob[0].dir & OBD_REV), "blocked by a crate: reverses and holds");
  t = tick_at(&r, ob, EG_IDX(1, 1), EG_IDX(1, 1), 0, 2);
  CHECK(t.hits == 1 && t.p == EG_IDX(2, 1), "pinned against the wall: trampled, a hit, the two swap");
  /* the shove: a heavy walking east into the player shoves them one cell */
  room_from(&r, LOOP, 5);
  add_ob(&r, OB_HEAVY, 0, 1, 2, 1, 0, 0);
  eg_ob_init(&r, ob);
  t = tick_at(&r, ob, EG_IDX(2, 2), EG_IDX(2, 2), 0, 0);
  CHECK(t.p == EG_IDX(3, 2) && t.hits == 0 && ob[0].cell == EG_IDX(2, 2) && (t.ev & EV_SHOVE), "shoved, no hit");
  /* the heavy breaks a partition and cracks floor glass behind it */
  static const char *const PART[] = {"#######", "#.gG..#", "#######"};
  room_from(&r, PART, 3);
  add_ob(&r, OB_HEAVY, 0, 1, 1, 1, 0, 0);
  eg_ob_init(&r, ob);
  tick_at(&r, ob, EG_IDX(5, 1), EG_IDX(5, 1), 0, 0);
  t = tick_at(&r, ob, EG_IDX(5, 1), EG_IDX(5, 1), 0, 1);
  CHECK((t.ev & EV_BROKE) && ob[0].cell == EG_IDX(3, 1) && r.c[EG_IDX(2, 1)] == T_PIT, "partition broken, glass behind");
  /* the pusher pushes a crate onto the plate: the shutter opens */
  static const char *const PUSH[] = {"########", "#..B_..#", "###=####", "########"};
  room_from(&r, PUSH, 4);
  add_ob(&r, OB_PUSHER, 0, 1, 1, 1, 0, 0);
  eg_ob_init(&r, ob);
  tick_at(&r, ob, EG_IDX(6, 1), EG_IDX(6, 1), 0, 0);
  tick_at(&r, ob, EG_IDX(6, 1), EG_IDX(6, 1), 0, 1);
  uint8_t pc = EG_IDX(6, 1);
  eg_gravity(&r, &pc, 1);
  CHECK(r.c[EG_IDX(4, 1)] == T_BLOCK_PLATE && r.c[EG_IDX(3, 2)] == T_GATE_OPEN, "pushed onto the plate, shutter up");
  /* the mirror copies the step flipped on x */
  room_from(&r, LOOP, 5);
  add_ob(&r, OB_MIRROR, 0, 3, 2, 0, 1, 0);
  eg_ob_init(&r, ob);
  tick_at(&r, ob, EG_IDX(2, 3), EG_IDX(1, 3), 0, 0);
  CHECK(ob[0].cell == EG_IDX(2, 2), "mirror: the player stepped east, it steps west");
  /* the sentry: still while looked at; else one cell closer on the bigger gap; a catch sends it back */
  static const char *const HALL[] = {"########", "#......#", "#......#", "########"};
  room_from(&r, HALL, 4);
  add_ob(&r, OB_SENTRY, OBA_LOCK, 6, 1, 0, 0, 0);
  eg_ob_init(&r, ob);
  tick_at(&r, ob, EG_IDX(1, 1), EG_IDX(1, 1), 2, 0); /* facing east: seen */
  CHECK(ob[0].cell == EG_IDX(6, 1), "seen: still");
  tick_at(&r, ob, EG_IDX(1, 1), EG_IDX(1, 1), 6, 1); /* facing west: unseen */
  CHECK(ob[0].cell == EG_IDX(5, 1), "unseen: one cell closer");
  r.c[EG_IDX(3, 1)] = T_BLOCK;
  tick_at(&r, ob, EG_IDX(1, 1), EG_IDX(1, 1), 2, 2);
  CHECK(ob[0].cell == EG_IDX(4, 1), "a crate on the sight line hides it");
  r.c[EG_IDX(3, 1)] = T_EMPTY;
  CHECK(eg_ob_seen(&r, EG_IDX(1, 2), 1, EG_IDX(4, 1)) == 1 && eg_ob_seen(&r, EG_IDX(1, 2), 4, EG_IDX(4, 1)) == 0,
        "the 90-degree cone");
  ob[0].cell = EG_IDX(2, 1), r.c[EG_IDX(4, 1)] &= (uint8_t)~F_OB, r.c[EG_IDX(2, 1)] |= F_OB;
  t = tick_at(&r, ob, EG_IDX(1, 1), EG_IDX(1, 1), 6, 3);
  CHECK(t.hits == 1 && (t.ev & EV_CATCH) && ob[0].cell == EG_IDX(6, 1) && t.p == EG_IDX(1, 1), "caught: back to the plinth");
  /* the sleeper: noise within its radius wakes it; two ticks of yawn; then it walks (variant 1: a heavy) */
  room_from(&r, HALL, 4);
  add_ob(&r, OB_SLEEPER, 0, 6, 2, 3, 1, 3);
  r.ob[0][2] |= OBB_ASLEEP;
  eg_ob_init(&r, ob);
  tick_at(&r, ob, EG_IDX(1, 1), EG_IDX(1, 1), 0, 0);
  CHECK(ob[0].cell == EG_IDX(6, 2) && !(ob[0].st & OBF_AWAKE), "asleep");
  memset(&t, 0, sizeof t);
  t.p = t.pp = EG_IDX(1, 1), t.nnoise = 1, t.noise[0] = EG_IDX(3, 1);
  eg_ob_tick(&r, ob, &t);
  CHECK((t.ev & EV_WOKE) && ob[0].cell == EG_IDX(6, 2), "woken (yawn 1)");
  tick_at(&r, ob, EG_IDX(1, 1), EG_IDX(1, 1), 0, 2);
  CHECK(ob[0].cell == EG_IDX(6, 2), "yawn 2");
  tick_at(&r, ob, EG_IDX(1, 1), EG_IDX(1, 1), 0, 3);
  CHECK(ob[0].cell == EG_IDX(5, 2), "then it walks");
  /* a noise lure turns the heavy */
  room_from(&r, LOOP, 5);
  add_ob(&r, OB_HEAVY, 0, 3, 3, 0, 0, 0);
  eg_ob_init(&r, ob);
  memset(&t, 0, sizeof t);
  t.p = t.pp = EG_IDX(1, 1), t.nnoise = 1, t.noise[0] = EG_IDX(5, 3);
  eg_ob_tick(&r, ob, &t);
  CHECK(ob[0].cell == EG_IDX(4, 3), "lured east by the noise");
  /* the follower wakes beside the player and walks into the cell they left */
  room_from(&r, HALL, 4);
  add_ob(&r, OB_FOLLOWER, 0, 3, 2, 0, 0, 0);
  eg_ob_init(&r, ob);
  tick_at(&r, ob, EG_IDX(3, 1), EG_IDX(2, 1), 0, 0);
  tick_at(&r, ob, EG_IDX(4, 1), EG_IDX(3, 1), 0, 1);
  CHECK(ob[0].cell == EG_IDX(3, 1), "the follower: one step behind");
  /* stops are brief: immune for 4 ticks after one ends */
  CHECK(eg_ob_stop(&ob[0], OBS_STUN, 2) && !eg_ob_stop(&ob[0], OBS_STUN, 2), "stunned, not twice");
  tick_at(&r, ob, EG_IDX(4, 1), EG_IDX(4, 1), 0, 2);
  tick_at(&r, ob, EG_IDX(4, 1), EG_IDX(4, 1), 0, 3);
  CHECK(!ob[0].stop && ob[0].imm == EG_OB_IMMUNE && !eg_ob_stop(&ob[0], OBS_FREEZE, 6), "then immune");
  /* the shutter rule: every plate weighted */
  static const char *const TWO[] = {"#######", "#_.._.#", "##=####", "#######"};
  room_from(&r, TWO, 4);
  r.c[EG_IDX(1, 1)] = T_BLOCK_PLATE;
  pc = EG_IDX(3, 1);
  eg_plates_dirty = 1;
  eg_gravity(&r, &pc, 1);
  CHECK(r.c[EG_IDX(2, 2)] == T_GATE, "one plate of two: down");
  r.c[EG_IDX(4, 1)] |= F_OB;
  eg_gravity(&r, &pc, 1);
  CHECK(r.c[EG_IDX(2, 2)] == T_GATE_OPEN, "both (an obstacle on one): up");
  /* Crates smash glass or fall into holes; unlike a CRT they never fill a pit. */
  static const char *const CR[] = {"#######", "#Bg.Bo#", "#######"};
  room_from(&r, CR, 3);
  CHECK(eg_push_block(&r, EG_IDX(1, 1), EG_IDX(2, 1)) && r.c[EG_IDX(2, 1)] == T_PIT && r.c[EG_IDX(1, 1)] == T_EMPTY,
        "a crate smashes through floor glass");
  CHECK(eg_push_block(&r, EG_IDX(4, 1), EG_IDX(5, 1)) && r.c[EG_IDX(4, 1)]==T_EMPTY &&
        r.c[EG_IDX(5, 1)]==T_PIT, "a crate falls into a pit without filling it");
}

int main(void) {
  test_rules();
  test_mock();
  printf(fails ? "test_obst: %d FAILED\n" : "test_obst: all passed\n", fails);
  return fails != 0;
}
