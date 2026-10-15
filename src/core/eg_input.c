/* EGRESS two-button move resolver (see eg_input.h, proto/input/FRAMES.md). */
#include "eg_input.h"

/*                                   startup active recovery open close */
const eg_frames_t eg_input_fd[EG_MV_COUNT] = {
    {0u, 0u, 0u, EG_NO_LINK, EG_NO_LINK},   /* IDLE */
    {0u, 0u, 0u, EG_NO_LINK, EG_NO_LINK},   /* DODGE: per style, eg_dodge_fd */
    {0u, 0u, 0u, EG_NO_LINK, EG_NO_LINK},   /* SPRINT: loop while B held */
    {0u, 0u, 4u, EG_NO_LINK, EG_NO_LINK},   /* SKID */
    {2u, 4u, 12u, 10u, 17u},                /* LUNGE: links to HIT2 from frame 10 */
    {4u, 2u, 10u, 8u, 15u},                 /* HIT1: links to HIT2 from frame 8 */
    {4u, 2u, 12u, 9u, 17u},                 /* HIT2: links to HIT3 from frame 9 */
    {6u, 3u, 18u, EG_NO_LINK, EG_NO_LINK},  /* HIT3: finisher */
    {0u, 0u, 0u, EG_NO_LINK, EG_NO_LINK},   /* CHARGE: loop while A held */
    {3u, 4u, 14u, EG_NO_LINK, EG_NO_LINK},  /* CHARGED */
    {2u, 8u, 12u, 2u, 4u},                  /* PARRY: perfect on frames 2..4 */
};

const eg_frames_t eg_dodge_fd[EG_DODGE_COUNT] = {
    {2u, 10u, 6u, 4u, 14u},  /* ROLL: 18 frames, cancel 4..14 */
    {3u, 4u, 7u, 6u, 11u},   /* BLINK: 14 frames, cancel 6..11 */
    {1u, 12u, 7u, 3u, 16u},  /* SLIDE: 20 frames, cancel 3..16 */
};

static uint8_t sat_inc(uint8_t v) { return v == 255u ? v : (uint8_t)(v + 1u); }

const eg_frames_t *eg_input_frames(const eg_input_t *s, uint8_t move) {
  if (move == EG_MV_DODGE) return &eg_dodge_fd[s->style];
  return &eg_input_fd[move];
}

uint8_t eg_input_total(const eg_input_t *s, uint8_t move) {
  const eg_frames_t *f = eg_input_frames(s, move);
  return (uint8_t)(f->startup + f->active + f->recovery);
}

static uint8_t is_loop(uint8_t mv) { return (uint8_t)(mv == EG_MV_SPRINT || mv == EG_MV_CHARGE); }

static uint8_t phase_of(const eg_input_t *s) {
  const eg_frames_t *f;
  if (s->move == EG_MV_IDLE) return EG_PH_NONE;
  if (is_loop(s->move)) return EG_PH_LOOP;
  f = eg_input_frames(s, s->move);
  if (s->frame < f->startup) return EG_PH_STARTUP;
  if (s->frame < (uint8_t)(f->startup + f->active)) return EG_PH_ACTIVE;
  return EG_PH_RECOVERY;
}

static void refresh(eg_input_t *s) {
  uint8_t ph = phase_of(s), fl = 0u, mv = s->move;
  const eg_frames_t *f = eg_input_frames(s, mv);
  if (mv == EG_MV_DODGE || mv == EG_MV_SPRINT || mv == EG_MV_LUNGE) fl |= EG_FL_MOMENTUM;
  if (ph == EG_PH_ACTIVE) {
    if (mv == EG_MV_DODGE) fl |= EG_FL_IFRAME;
    else if (mv == EG_MV_PARRY) {
      fl |= EG_FL_GUARD;
      if (s->frame >= f->open && s->frame <= f->close) fl |= EG_FL_PERFECT;
    } else if (mv >= EG_MV_LUNGE && mv != EG_MV_CHARGE) fl |= EG_FL_HITBOX;
  }
  s->phase = ph;
  s->flags = fl;
}

static void start(eg_input_t *s, uint8_t mv) {
  if (mv == EG_MV_DODGE) s->chain = (uint8_t)(s->move == EG_MV_LUNGE);
  else if (mv != EG_MV_LUNGE && mv != EG_MV_SPRINT) s->chain = 0u;
  s->move = mv;
  s->frame = 0u;
  refresh(s);
}

void eg_input_init(eg_input_t *s, uint8_t style) {
  s->style = style < EG_DODGE_COUNT ? style : EG_DODGE_ROLL;
  s->prev = 0u;
  s->move = EG_MV_IDLE;
  s->frame = 0u;
  s->level = 0u;
  s->chain = 0u;
  s->a_hold = 0u;
  s->b_hold = 0u;
  s->a_age = EG_AGE_CAP;
  s->buf = EG_INT_NONE;
  s->buf_age = 0u;
  refresh(s);
}

/* The move a buffered intent starts in the current state, or EG_NO_MOVE to keep waiting. */
static uint8_t resolve(const eg_input_t *s, uint8_t intent) {
  uint8_t mv = s->move, ph = s->phase, fr = s->frame;
  const eg_frames_t *f = eg_input_frames(s, mv);
  if (intent == EG_INT_A) {
    switch (mv) {
      case EG_MV_IDLE:
      case EG_MV_SKID: return EG_MV_HIT1;
      case EG_MV_HIT1:
      case EG_MV_LUNGE: return fr >= f->open ? EG_MV_HIT2 : EG_NO_MOVE;
      case EG_MV_HIT2: return fr >= f->open ? EG_MV_HIT3 : EG_NO_MOVE;
      case EG_MV_DODGE: return (!s->chain && fr >= f->open && fr <= f->close) ? EG_MV_LUNGE : EG_NO_MOVE;
      case EG_MV_SPRINT: return s->chain ? EG_NO_MOVE : EG_MV_LUNGE;
      default: return EG_NO_MOVE;
    }
  }
  if (intent == EG_INT_B) {
    switch (mv) {
      case EG_MV_IDLE:
      case EG_MV_SKID:
      case EG_MV_CHARGE: return EG_MV_DODGE;
      case EG_MV_HIT1:
      case EG_MV_HIT2:
      case EG_MV_HIT3: return ph != EG_PH_ACTIVE ? EG_MV_DODGE : EG_NO_MOVE;
      case EG_MV_LUNGE:
      case EG_MV_CHARGED: return ph == EG_PH_RECOVERY ? EG_MV_DODGE : EG_NO_MOVE;
      default: return EG_NO_MOVE;
    }
  }
  if (intent == EG_INT_AB) {
    switch (mv) {
      case EG_MV_IDLE:
      case EG_MV_SKID:
      case EG_MV_HIT1:
      case EG_MV_HIT2:
      case EG_MV_HIT3:
      case EG_MV_CHARGE: return EG_MV_PARRY;
      case EG_MV_LUNGE:
      case EG_MV_CHARGED: return ph == EG_PH_RECOVERY ? EG_MV_PARRY : EG_NO_MOVE;
      default: return EG_NO_MOVE;
    }
  }
  return EG_NO_MOVE;
}

uint8_t eg_input_step(eg_input_t *s, uint8_t held) {
  uint8_t pa = (uint8_t)(held & (uint8_t)~s->prev & EG_IN_A);
  uint8_t pb = (uint8_t)(held & (uint8_t)~s->prev & EG_IN_B);
  uint8_t a_rel = s->a_hold; /* frames A was held before this frame */
  uint8_t intent = EG_INT_NONE, started = EG_NO_MOVE, mv;

  s->prev = held;
  s->a_hold = (held & EG_IN_A) ? (pa ? 1u : sat_inc(s->a_hold)) : 0u;
  s->b_hold = (held & EG_IN_B) ? (pb ? 1u : sat_inc(s->b_hold)) : 0u;
  if (pa) s->a_age = 0u;
  else if (s->a_age < EG_AGE_CAP) s->a_age++;

  /* Intent at press time. Same-frame A+B, or B while A is down / within EG_AB_WIN of A's press: secondary. */
  if (pa && pb) intent = EG_INT_AB;
  else if (pb) intent = ((held & EG_IN_A) || s->a_age <= EG_AB_WIN) ? EG_INT_AB : EG_INT_B;
  else if (pa) intent = EG_INT_A;
  if (intent != EG_INT_NONE) {
    s->buf = intent;
    s->buf_age = 0u;
  } else if (s->buf != EG_INT_NONE) {
    s->buf_age++;
    if (s->buf_age >= EG_BUF_FRAMES) {
      s->buf = EG_INT_NONE;
      s->buf_age = 0u;
    }
  }

  /* Advance the current move; automatic ends and loop releases. */
  if (s->move != EG_MV_IDLE) {
    s->frame = sat_inc(s->frame);
    if (s->move == EG_MV_SPRINT) {
      if (!(held & EG_IN_B)) started = EG_MV_SKID;
    } else if (s->move == EG_MV_CHARGE) {
      if (!(held & EG_IN_A)) {
        s->level = a_rel >= EG_CHARGE_L3 ? 3u : (a_rel >= EG_CHARGE_L2 ? 2u : 1u);
        started = EG_MV_CHARGED;
      }
    } else if (s->frame >= eg_input_total(s, s->move)) {
      s->move = EG_MV_IDLE;
      s->frame = 0u;
      s->chain = 0u;
    }
  }
  if (started != EG_NO_MOVE) start(s, started);
  else refresh(s);

  /* The buffered press, if the current state accepts it. */
  if (started == EG_NO_MOVE && s->buf != EG_INT_NONE) {
    mv = resolve(s, s->buf);
    if (mv != EG_NO_MOVE) {
      s->buf = EG_INT_NONE;
      s->buf_age = 0u;
      started = mv;
      start(s, mv);
    }
  }

  /* Hold branches: B held past the threshold turns the dodge into a sprint once its active frames end; A held past
   * the threshold turns idle / a combo hit / the lunge into a charge outside active frames. */
  if (started == EG_NO_MOVE) {
    if (s->move == EG_MV_DODGE && (held & EG_IN_B) && s->b_hold >= EG_HOLD_T && s->phase == EG_PH_RECOVERY) {
      started = EG_MV_SPRINT;
    } else if ((held & EG_IN_A) && s->a_hold >= EG_HOLD_T && s->phase != EG_PH_ACTIVE &&
               (s->move == EG_MV_IDLE || s->move == EG_MV_LUNGE ||
                (s->move >= EG_MV_HIT1 && s->move <= EG_MV_HIT3))) {
      started = EG_MV_CHARGE;
    }
    if (started != EG_NO_MOVE) start(s, started);
  }
  return started;
}

uint8_t eg_input_parry(const eg_input_t *s) {
  if (s->flags & EG_FL_PERFECT) return 2u;
  if (s->flags & EG_FL_GUARD) return 1u;
  return 0u;
}
