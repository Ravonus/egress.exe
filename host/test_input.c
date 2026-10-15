/* Host tests for src/core/eg_input.c: exact frame-by-frame timings for the two-button move resolver.
 * cc -O2 -std=c99 -Wall -Wextra -Werror -Isrc -o build/host/test_input host/test_input.c src/core/eg_input.c */
#include <stdio.h>
#include <string.h>

#include "core/eg_input.h"

static int pass, fail;
#define CHECK(c, ...)                                  \
  do {                                                 \
    if (c) {                                           \
      pass++;                                          \
    } else {                                           \
      fail++;                                          \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);      \
      printf(__VA_ARGS__);                             \
      printf("\n");                                    \
    }                                                  \
  } while (0)

#define A EG_IN_A
#define B EG_IN_B
#define MAXF 400

static uint8_t in[MAXF], st[MAXF], mv_at[MAXF], fr_at[MAXF], fl_at[MAXF], lv_at[MAXF], pq_at[MAXF];

static void clear(void) { memset(in, 0, sizeof in); }
static void hold(uint8_t bits, int from, int to) {
  int i;
  for (i = from; i <= to; i++) in[i] |= bits;
}
static void run(uint8_t style, int n) {
  eg_input_t s;
  int i;
  eg_input_init(&s, style);
  for (i = 0; i < n; i++) {
    st[i] = eg_input_step(&s, in[i]);
    mv_at[i] = s.move;
    fr_at[i] = s.frame;
    fl_at[i] = s.flags;
    lv_at[i] = s.level;
    pq_at[i] = eg_input_parry(&s);
  }
}
static int first(uint8_t mv, int from, int n) {
  int i;
  for (i = from; i < n; i++)
    if (st[i] == mv) return i;
  return -1;
}
static int count(uint8_t mv, int n) {
  int i, c = 0;
  for (i = 0; i < n; i++)
    if (st[i] == mv) c++;
  return c;
}
static int first_idle(int from, int n) {
  int i;
  for (i = from; i < n; i++)
    if (mv_at[i] == EG_MV_IDLE) return i;
  return -1;
}

static void t_b_tap(void) {
  int i, ok = 1;
  clear();
  hold(B, 0, 2);
  run(EG_DODGE_ROLL, 60);
  CHECK(st[0] == EG_MV_DODGE, "B tap: dodge starts on the press frame (got %u)", st[0]);
  CHECK(count(EG_MV_SPRINT, 60) == 0, "B tap: no sprint");
  CHECK(first_idle(1, 60) == 18, "B tap: roll lasts 18 frames (idle at %d)", first_idle(1, 60));
  for (i = 0; i < 18; i++)
    if (((fl_at[i] & EG_FL_IFRAME) != 0) != (i >= 2 && i <= 11)) ok = 0;
  CHECK(ok, "B tap: roll i-frames exactly on frames 2..11");
  /* styles */
  run(EG_DODGE_BLINK, 60);
  CHECK(first_idle(1, 60) == 14, "blink lasts 14 frames (%d)", first_idle(1, 60));
  run(EG_DODGE_SLIDE, 60);
  CHECK(first_idle(1, 60) == 20, "slide lasts 20 frames (%d)", first_idle(1, 60));
}

static void t_b_hold(void) {
  clear();
  hold(B, 0, 39);
  run(EG_DODGE_ROLL, 80);
  CHECK(st[0] == EG_MV_DODGE, "B hold: dodge still starts on press");
  CHECK(first(EG_MV_SPRINT, 0, 80) == 12, "B hold: roll -> sprint at frame 12 (%d)", first(EG_MV_SPRINT, 0, 80));
  CHECK(mv_at[39] == EG_MV_SPRINT && (fl_at[39] & EG_FL_MOMENTUM), "B hold: sprinting while held");
  CHECK(first(EG_MV_SKID, 0, 80) == 40, "B release: skid on the release frame (%d)", first(EG_MV_SKID, 0, 80));
  CHECK(first_idle(41, 80) == 44, "skid lasts 4 frames (%d)", first_idle(41, 80));
  run(EG_DODGE_BLINK, 80);
  CHECK(first(EG_MV_SPRINT, 0, 80) == 10, "blink -> sprint at 10 (hold threshold, %d)", first(EG_MV_SPRINT, 0, 80));
  run(EG_DODGE_SLIDE, 80);
  CHECK(first(EG_MV_SPRINT, 0, 80) == 13, "slide -> sprint at 13 (%d)", first(EG_MV_SPRINT, 0, 80));
  /* threshold edges on blink, where the sprint fires exactly at the threshold */
  clear();
  hold(B, 0, 9); /* 10 frames: a tap */
  run(EG_DODGE_BLINK, 40);
  CHECK(count(EG_MV_SPRINT, 40) == 0, "B held 10 frames: tap, no sprint");
  clear();
  hold(B, 0, 10); /* 11 frames: hold */
  run(EG_DODGE_BLINK, 40);
  CHECK(first(EG_MV_SPRINT, 0, 40) == 10 && first(EG_MV_SKID, 0, 40) == 11, "B held 11 frames: sprint at 10, skid at 11");
  /* roll: hold must still be down when the recovery starts */
  clear();
  hold(B, 0, 11);
  run(EG_DODGE_ROLL, 40);
  CHECK(count(EG_MV_SPRINT, 40) == 0, "roll, B released at 12: no sprint");
}

static void t_b_then_a(void) {
  int a, n = 60;
  /* roll window 4..14 */
  for (a = 1; a <= 20; a++) {
    clear();
    hold(B, 0, 1);
    hold(A, a, a + 1);
    run(EG_DODGE_ROLL, n);
    if (a <= 3) {
      CHECK(first(EG_MV_LUNGE, 0, n) == 4, "roll A at %d (before window): buffered, lunge at 4 (%d)", a, first(EG_MV_LUNGE, 0, n));
    } else if (a <= 14) {
      CHECK(first(EG_MV_LUNGE, 0, n) == a, "roll A at %d (in window): lunge at %d (%d)", a, a, first(EG_MV_LUNGE, 0, n));
      CHECK(fl_at[a] & EG_FL_MOMENTUM, "lunge keeps dodge momentum");
    } else if (a <= 17) {
      CHECK(count(EG_MV_LUNGE, n) == 0 && first(EG_MV_HIT1, 0, n) == 18,
            "roll A at %d (after window, before end): normal hit at dodge end 18 (%d)", a, first(EG_MV_HIT1, 0, n));
    } else {
      CHECK(count(EG_MV_LUNGE, n) == 0 && first(EG_MV_HIT1, 0, n) == a, "roll A at %d (after dodge): normal hit at %d", a, a);
    }
  }
  /* blink window 6..11, slide 3..16: first/last frame and one outside each edge */
  clear(); hold(B, 0, 1); hold(A, 6, 6); run(EG_DODGE_BLINK, n);
  CHECK(first(EG_MV_LUNGE, 0, n) == 6, "blink A at 6: lunge at 6");
  clear(); hold(B, 0, 1); hold(A, 5, 5); run(EG_DODGE_BLINK, n);
  CHECK(first(EG_MV_LUNGE, 0, n) == 6, "blink A at 5: buffered to 6");
  clear(); hold(B, 0, 1); hold(A, 11, 11); run(EG_DODGE_BLINK, n);
  CHECK(first(EG_MV_LUNGE, 0, n) == 11, "blink A at 11: lunge at 11");
  clear(); hold(B, 0, 1); hold(A, 12, 12); run(EG_DODGE_BLINK, n);
  CHECK(count(EG_MV_LUNGE, n) == 0 && first(EG_MV_HIT1, 0, n) == 14, "blink A at 12: hit at 14");
  clear(); hold(B, 0, 1); hold(A, 3, 3); run(EG_DODGE_SLIDE, n);
  CHECK(first(EG_MV_LUNGE, 0, n) == 3, "slide A at 3: lunge at 3");
  clear(); hold(B, 0, 1); hold(A, 16, 16); run(EG_DODGE_SLIDE, n);
  CHECK(first(EG_MV_LUNGE, 0, n) == 16, "slide A at 16: lunge at 16");
  clear(); hold(B, 0, 1); hold(A, 17, 17); run(EG_DODGE_SLIDE, n);
  CHECK(count(EG_MV_LUNGE, n) == 0 && first(EG_MV_HIT1, 0, n) == 20, "slide A at 17: hit at 20");
  /* A in sprint: lunge */
  clear(); hold(B, 0, 30); hold(A, 20, 20); run(EG_DODGE_ROLL, n);
  CHECK(first(EG_MV_LUNGE, 0, n) == 20, "A during sprint: lunge at 20");
  /* lunge -> combo continues at HIT2 */
  clear(); hold(B, 0, 1); hold(A, 5, 5); hold(A, 13, 13); run(EG_DODGE_ROLL, n);
  CHECK(first(EG_MV_LUNGE, 0, n) == 5 && first(EG_MV_HIT2, 0, n) == 15, "lunge links to HIT2 at lunge frame 10 (%d)", first(EG_MV_HIT2, 0, n));
  /* no second lunge from a dodge out of lunge recovery */
  clear(); hold(B, 0, 1); hold(A, 5, 5); hold(B, 12, 12); hold(A, 18, 18); run(EG_DODGE_ROLL, n);
  CHECK(first(EG_MV_DODGE, 1, n) == 12, "dodge out of lunge recovery at 12 (%d)", first(EG_MV_DODGE, 1, n));
  CHECK(count(EG_MV_LUNGE, n) == 1 && first(EG_MV_HIT1, 0, n) == -1, "chained dodge: A in its window does not lunge again");
}

static void t_same_frame(void) {
  clear();
  hold(A | B, 0, 1);
  run(EG_DODGE_ROLL, 40);
  CHECK(st[0] == EG_MV_PARRY, "A+B same frame: parry (got %u)", st[0]);
  CHECK(count(EG_MV_DODGE, 40) == 0 && count(EG_MV_HIT1, 40) == 0, "A+B same frame: nothing else");
}

static void t_tap_hold(void) {
  int n = 80;
  clear(); hold(A, 0, 9); run(EG_DODGE_ROLL, n);
  CHECK(st[0] == EG_MV_HIT1 && count(EG_MV_CHARGE, n) == 0 && first_idle(1, n) == 16, "A held 10 (T-1): tap, hit1 16 frames, no charge");
  clear(); hold(A, 0, 10); run(EG_DODGE_ROLL, n);
  CHECK(st[0] == EG_MV_HIT1, "A held 11 (T): hit1 still starts on press");
  CHECK(first(EG_MV_CHARGE, 0, n) == 10 && first(EG_MV_CHARGED, 0, n) == 11 && lv_at[11] == 1,
        "A held 11 (T): charge at 10, charged L1 at release 11");
  clear(); hold(A, 0, 11); run(EG_DODGE_ROLL, n);
  CHECK(first(EG_MV_CHARGE, 0, n) == 10 && first(EG_MV_CHARGED, 0, n) == 12 && lv_at[12] == 1,
        "A held 12 (T+1): charge at 10, charged at 12");
  CHECK(first_idle(13, n) == 12 + 21, "charged attack lasts 21 frames (%d)", first_idle(13, n));
}

static void t_charge_levels(void) {
  static const uint8_t len[6] = {11u, 29u, 30u, 49u, 50u, 200u};
  static const uint8_t lev[6] = {1u, 1u, 2u, 2u, 3u, 3u};
  int i, n = 260;
  for (i = 0; i < 6; i++) {
    clear();
    hold(A, 0, len[i] - 1);
    run(EG_DODGE_ROLL, n);
    CHECK(first(EG_MV_CHARGED, 0, n) == len[i] && lv_at[len[i]] == lev[i], "A held %u: charged level %u (got %u)", len[i], lev[i],
          lv_at[len[i]]);
  }
}

static void t_a_then_b(void) {
  int n = 60;
  clear(); hold(A, 0, 1); hold(B, 4, 4); run(EG_DODGE_ROLL, n);
  CHECK(st[0] == EG_MV_HIT1 && first(EG_MV_PARRY, 0, n) == 4, "A at 0, B at 4: parry at 4");
  clear(); hold(A, 0, 1); hold(B, 1, 1); run(EG_DODGE_ROLL, n);
  CHECK(first(EG_MV_PARRY, 0, n) == 1, "A at 0, B at 1: parry at 1");
  clear(); hold(A, 0, 1); hold(B, 5, 5); run(EG_DODGE_ROLL, n);
  CHECK(count(EG_MV_PARRY, n) == 0 && first(EG_MV_DODGE, 0, n) == 6,
        "A at 0, B at 5 (outside, A up): dodge, buffered past hit1 active to 6 (%d)", first(EG_MV_DODGE, 0, n));
  clear(); hold(A, 0, 1); hold(B, 8, 8); run(EG_DODGE_ROLL, n);
  CHECK(first(EG_MV_DODGE, 0, n) == 8, "B in hit1 recovery: dodge cancels instantly at 8");
  clear(); hold(A, 0, 19); hold(B, 15, 15); run(EG_DODGE_ROLL, n);
  CHECK(first(EG_MV_PARRY, 0, n) == 15 && count(EG_MV_CHARGED, n) == 0, "A held (charging), B at 15: parry at 15");
  clear(); hold(B, 0, 1); hold(A, 2, 2); run(EG_DODGE_ROLL, n);
  CHECK(st[0] == EG_MV_DODGE && count(EG_MV_PARRY, n) == 0, "B first then A: dodge, not parry");
}

static void t_parry(void) {
  int i, ok = 1;
  clear();
  hold(A | B, 0, 0);
  run(EG_DODGE_ROLL, 40);
  for (i = 0; i < 22; i++) {
    uint8_t want = (i >= 2 && i <= 4) ? 2u : ((i >= 5 && i <= 9) ? 1u : 0u);
    if (pq_at[i] != want || mv_at[i] != EG_MV_PARRY) ok = 0;
  }
  CHECK(ok, "parry: perfect on frames 2..4, guard 5..9, recovery 10..21");
  CHECK(first_idle(1, 40) == 22, "parry lasts 22 frames (%d)", first_idle(1, 40));
}

static void t_combo(void) {
  int n = 120;
  clear(); hold(A, 0, 1); hold(A, 5, 5); hold(A, 14, 14); run(EG_DODGE_ROLL, n);
  CHECK(st[0] == EG_MV_HIT1, "combo: hit1 at 0");
  CHECK(first(EG_MV_HIT2, 0, n) == 8, "combo: buffered press at 5 -> hit2 at link frame 8 (%d)", first(EG_MV_HIT2, 0, n));
  CHECK(first(EG_MV_HIT3, 0, n) == 17, "combo: press at 14 (hit2 f6) -> hit3 at hit2 f9 = 17 (%d)", first(EG_MV_HIT3, 0, n));
  CHECK(first_idle(18, n) == 17 + 27, "combo: hit3 lasts 27 frames (%d)", first_idle(18, n));
  clear(); hold(A, 0, 1); hold(A, 10, 10); run(EG_DODGE_ROLL, n);
  CHECK(first(EG_MV_HIT2, 0, n) == 10, "combo: press in open window at 10 -> hit2 instantly");
  clear(); hold(A, 0, 1); hold(A, 15, 15); run(EG_DODGE_ROLL, n);
  CHECK(first(EG_MV_HIT2, 0, n) == 15, "combo: last window frame 15 -> hit2");
  clear(); hold(A, 0, 1); hold(A, 16, 16); run(EG_DODGE_ROLL, n);
  CHECK(count(EG_MV_HIT2, n) == 0 && first(EG_MV_HIT1, 1, n) == 16, "combo: press at 16 (dropped) -> new hit1");
  clear(); hold(A, 0, 0); hold(A, 2, 2); run(EG_DODGE_ROLL, n);
  CHECK(count(EG_MV_HIT2, n) == 0, "combo: press at 2 expires before link 8 (buffer 6)");
  clear(); hold(A, 0, 0); hold(A, 3, 3); run(EG_DODGE_ROLL, n);
  CHECK(first(EG_MV_HIT2, 0, n) == 8, "combo: press at 3 survives to link 8");
  clear(); hold(A, 0, 0); run(EG_DODGE_ROLL, n);
  CHECK(first_idle(1, n) == 16, "combo: no press -> combo drops, idle at 16");
  clear(); hold(A, 0, 0); hold(A, 5, 5); hold(A, 14, 14); hold(A, 40, 40); run(EG_DODGE_ROLL, n);
  CHECK(first(EG_MV_HIT1, 1, n) == 44, "after hit3: press at 40 buffered -> hit1 at 44 (%d)", first(EG_MV_HIT1, 1, n));
  clear(); hold(A, 0, 0); hold(B, 5, 5); run(EG_DODGE_ROLL, n);
  CHECK(first(EG_MV_DODGE, 0, n) == 6, "B during hit1 active (5): buffered dodge at recovery 6");
}

/* ---- mashing ---- */
static uint32_t lcg;
static uint8_t rnd(void) {
  lcg = lcg * 1103515245u + 12345u;
  return (uint8_t)(lcg >> 16);
}
#define MASH 10000
static uint8_t trace_a[MASH * 5], trace_b[MASH * 5];

static int mash(uint8_t style, uint32_t seed, uint8_t *trace) {
  eg_input_t s;
  uint8_t held = 0u, r;
  int i, k = 0, bad = 0, lunges_since_reset = 0, idle_ok = 1;
  lcg = seed;
  eg_input_init(&s, style);
  for (i = 0; i < MASH; i++) {
    int quiet = (i % 1000) >= 936; /* 64 released frames every 1000: must settle to idle */
    if (quiet) held = 0u;
    else {
      r = rnd();
      if (r < 70u) held ^= EG_IN_A;
      else if (r < 140u) held ^= EG_IN_B;
      else if (r < 150u) held = (uint8_t)(EG_IN_A | EG_IN_B);
      else if (r < 160u) held = 0u;
      held |= (uint8_t)(rnd() & 0x0fu); /* noise on d-pad bits: ignored */
    }
    r = eg_input_step(&s, held);
    if (s.move >= EG_MV_COUNT) bad++;
    if (r != EG_NO_MOVE && r != s.move) bad++;
    if (r != EG_NO_MOVE && r >= EG_MV_COUNT) bad++;
    if (s.move == EG_MV_IDLE && (s.phase != EG_PH_NONE || s.frame != 0u)) bad++;
    if ((s.move == EG_MV_SPRINT || s.move == EG_MV_CHARGE) && s.phase != EG_PH_LOOP) bad++;
    if (s.move != EG_MV_IDLE && s.move != EG_MV_SPRINT && s.move != EG_MV_CHARGE && s.frame >= eg_input_total(&s, s.move)) bad++;
    if ((s.flags & EG_FL_IFRAME) && s.move != EG_MV_DODGE) bad++;
    if ((s.flags & (EG_FL_GUARD | EG_FL_PERFECT)) && s.move != EG_MV_PARRY) bad++;
    if ((s.flags & EG_FL_HITBOX) && (s.flags & (EG_FL_IFRAME | EG_FL_GUARD))) bad++; /* never two moves' effects */
    if (s.buf > EG_INT_AB || s.buf_age >= EG_BUF_FRAMES || s.a_age > EG_AGE_CAP || s.chain > 1u) bad++;
    if (s.move == EG_MV_CHARGE && !(held & EG_IN_A)) bad++;
    if (s.move == EG_MV_SPRINT && !(held & EG_IN_B)) bad++;
    if (s.move == EG_MV_CHARGED && (s.level < 1u || s.level > 3u)) bad++;
    /* infinite-cancel guard: a lunge never starts from a dodge/sprint that itself came out of a lunge, so two lunges
     * always have a reset (idle, hit, parry, charge, skid, or a fresh dodge) between them */
    if (s.move == EG_MV_IDLE) lunges_since_reset = 0;
    if (r == EG_MV_LUNGE) {
      if (++lunges_since_reset > 1) bad++;
    } else if (r != EG_NO_MOVE && r != EG_MV_SPRINT && !(r == EG_MV_DODGE && s.chain)) {
      lunges_since_reset = 0;
    }
    if ((i % 1000) == 999 && s.move != EG_MV_IDLE) idle_ok = 0;
    trace[k++] = s.move;
    trace[k++] = s.phase;
    trace[k++] = s.frame;
    trace[k++] = s.flags;
    trace[k++] = r;
  }
  CHECK(idle_ok, "mash style %u: always settles to idle after 64 released frames", style);
  return bad;
}

static void t_mash(void) {
  uint8_t style;
  for (style = 0; style < EG_DODGE_COUNT; style++) {
    int bad = mash(style, 0x1234567u + style, trace_a);
    int bad2 = mash(style, 0x1234567u + style, trace_b);
    int i, starts = 0;
    CHECK(bad == 0 && bad2 == 0, "mash style %u: %d invariant violations", style, bad);
    CHECK(memcmp(trace_a, trace_b, sizeof trace_a) == 0, "mash style %u: same stream twice gives the same trace", style);
    for (i = 0; i < MASH; i++)
      if (trace_a[i * 5 + 4] != EG_NO_MOVE) starts++;
    CHECK(starts > 500, "mash style %u: mashing actually starts moves (%d)", style, starts);
  }
}

int main(void) {
  t_b_tap();
  t_b_hold();
  t_b_then_a();
  t_same_frame();
  t_tap_hold();
  t_charge_levels();
  t_a_then_b();
  t_parry();
  t_combo();
  t_mash();
  printf("test_input: %d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
