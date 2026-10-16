/* EGRESS two-button move resolver (see eg_input.h, proto/input/FRAMES.md). */
#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_input.h"

/*                                   startup active recovery open close */
const eg_frames_t eg_input_fd[EG_MV_COUNT] = {
    {0u, 0u, 0u, EG_NO_LINK, EG_NO_LINK},   /* IDLE */
    {0u, 0u, 0u, EG_NO_LINK, EG_NO_LINK},   /* DODGE: per style, eg_dodge_fd */
    {0u, 0u, 0u, EG_NO_LINK, EG_NO_LINK},   /* SPRINT: loop while direction held */
    {0u, 0u, 4u, EG_NO_LINK, EG_NO_LINK},   /* SKID */
    {2u, 4u, 12u, 10u, 17u},                /* LUNGE: links to HIT2 from frame 10 */
    {4u, 2u, 10u, 8u, 15u},                 /* HIT1: links to HIT2 from frame 8 */
    {4u, 2u, 12u, 9u, 17u},                 /* HIT2: links to HIT3 from frame 9 */
    {6u, 3u, 18u, EG_NO_LINK, EG_NO_LINK},  /* HIT3: finisher */
    {0u, 0u, 0u, EG_NO_LINK, EG_NO_LINK},   /* CHARGE: loop while A held */
    {3u, 4u, 14u, EG_NO_LINK, EG_NO_LINK},  /* CHARGED */
    {2u, 8u, 12u, 2u, 4u},                  /* PARRY: perfect on frames 2..4 */
    {4u, 1u, 21u, EG_NO_LINK, EG_NO_LINK},  /* B tap: deliberate recovery */
    {0u, 0u, 0u, EG_NO_LINK, EG_NO_LINK},   /* B hold, charge */
    {5u, 2u, 25u, EG_NO_LINK, EG_NO_LINK},  /* B charge release */
};

const eg_frames_t eg_dodge_fd[EG_DODGE_COUNT] = {
    {2u, 10u, 6u, 4u, 14u},  /* ROLL: 18 frames, cancel 4..14 */
    {3u, 4u, 7u, 6u, 11u},   /* BLINK: 14 frames, cancel 6..11 */
    {1u, 12u, 7u, 3u, 16u},  /* SLIDE: 20 frames, cancel 3..16 */
};

void eg_input_fd_get(uint8_t move, eg_frames_t *out) EG_BANKED { *out = eg_input_fd[move]; }

static uint8_t sat_inc(uint8_t v) { return v == 255u ? v : (uint8_t)(v + 1u); }

/* near versions for the step itself (on the cartridge the public ones are banked calls) */
static const eg_frames_t *frames_of(const eg_input_t *s, uint8_t move) {
  static eg_frames_t tuned;
  int8_t ds,dr;
  if (move == EG_MV_DODGE) {
    static const eg_frames_t side={1u,6u,5u,4u,10u}, back={1u,5u,6u,4u,10u};
    if(s->evade==EG_EVADE_SIDE) return &side;
    if(s->evade==EG_EVADE_BACK) return &back;
    return &eg_dodge_fd[s->style];
  }
  if (s->weapon && s->weapon < 5u && move >= EG_MV_LUNGE && move <= EG_MV_CHARGED && move != EG_MV_CHARGE) {
    tuned=eg_input_fd[move];
    ds=s->weapon==1u ? 3 : s->weapon==2u ? 1 : s->weapon==3u ? -2 : 2;
    dr=s->weapon==1u ? 6 : s->weapon==2u ? 2 : s->weapon==3u ? -4 : 4;
    tuned.startup=(uint8_t)((int8_t)tuned.startup+ds);
    tuned.recovery=(uint8_t)((int8_t)tuned.recovery+dr);
    if(tuned.open!=EG_NO_LINK) tuned.open=(uint8_t)((int8_t)tuned.open+ds);
    if(tuned.close!=EG_NO_LINK) tuned.close=(uint8_t)((int8_t)tuned.close+ds+dr);
    return &tuned;
  }
  return &eg_input_fd[move];
}
void eg_input_attack_frames(const eg_input_t *s, eg_frames_t *out) EG_BANKED { *out=*frames_of(s,s->move); }
static uint8_t total_of(const eg_input_t *s, uint8_t move) {
  const eg_frames_t *f = frames_of(s, move);
  return (uint8_t)(f->startup + f->active + f->recovery);
}
const eg_frames_t *eg_input_frames(const eg_input_t *s, uint8_t move) EG_BANKED { return frames_of(s, move); }
uint8_t eg_input_total(const eg_input_t *s, uint8_t move) EG_BANKED { return total_of(s, move); }

static uint8_t is_loop(uint8_t mv) { return (uint8_t)(mv == EG_MV_SPRINT || mv == EG_MV_CHARGE || mv == EG_MV_BCHARGE); }
static uint8_t shot_period(uint8_t weapon) {
  (void)weapon;return 26u;
}
uint8_t eg_input_shot_period(uint8_t weapon) EG_BANKED { return shot_period(weapon); }

static void refresh(eg_input_t *s) {
  uint8_t ph, fl = 0u, mv = s->move;
  const eg_frames_t *f;
  if(mv==EG_MV_IDLE) {s->phase=EG_PH_NONE;s->flags=0u;return;}
  if(is_loop(mv)) {s->phase=EG_PH_LOOP;s->flags=mv==EG_MV_SPRINT ? EG_FL_MOMENTUM : 0u;return;}
  f=frames_of(s,mv);
  ph=s->frame<f->startup ? EG_PH_STARTUP : s->frame<(uint8_t)(f->startup+f->active) ? EG_PH_ACTIVE : EG_PH_RECOVERY;
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

void eg_input_init(eg_input_t *s, uint8_t style) EG_BANKED {
  s->style = style < EG_DODGE_COUNT ? style : EG_DODGE_ROLL;
  s->weapon = 0u;
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
  s->facing=0u;s->dp_prev=EG_NO_DIR;s->tap_dir=EG_NO_DIR;
  s->dp_hold=s->tap_age=s->tap_face=s->dash_dir=s->evade=s->dash_cd=s->gait=0u;
  s->b_age=EG_AGE_CAP;
  refresh(s);
}

/* The move a buffered intent starts in the current state, or EG_NO_MOVE to keep waiting. */
static uint8_t resolve(const eg_input_t *s, uint8_t intent) {
  uint8_t mv = s->move, ph = s->phase, fr = s->frame;
  uint8_t secondary=s->b_hold ? EG_MV_BCHARGE : EG_MV_SHOT;
  const eg_frames_t *f = frames_of(s, mv);
  if (intent == EG_INT_A) {
    switch (mv) {
      case EG_MV_IDLE:
      case EG_MV_SPRINT:
      case EG_MV_SKID: return EG_MV_HIT1;
      case EG_MV_HIT1:
      case EG_MV_LUNGE: return fr >= f->open ? EG_MV_HIT2 : EG_NO_MOVE;
      case EG_MV_HIT2: return fr >= f->open ? EG_MV_HIT3 : EG_NO_MOVE;
      case EG_MV_DODGE: return (!s->chain && fr >= f->open && fr <= f->close) ? EG_MV_LUNGE : EG_NO_MOVE;
      case EG_MV_BCHARGE: return EG_MV_LUNGE;
      default: return EG_NO_MOVE;
    }
  }
  if (intent == EG_INT_B) {
    switch (mv) {
      case EG_MV_IDLE:
      case EG_MV_SKID:
      case EG_MV_SPRINT:
      case EG_MV_CHARGE: return secondary;
      case EG_MV_HIT1:
      case EG_MV_HIT2:
      case EG_MV_HIT3: return ph != EG_PH_ACTIVE ? secondary : EG_NO_MOVE;
      case EG_MV_LUNGE:
      case EG_MV_CHARGED:
      case EG_MV_SHOT: return fr>=total_of(s,mv)-EG_BUF_FRAMES ? secondary : EG_NO_MOVE;
      default: return EG_NO_MOVE;
    }
  }
  if (intent == EG_INT_AB) {
    switch (mv) {
      case EG_MV_IDLE:
      case EG_MV_SPRINT:
      case EG_MV_SKID:
      case EG_MV_HIT1:
      case EG_MV_HIT2:
      case EG_MV_HIT3:
      case EG_MV_CHARGE:
      case EG_MV_SHOT:
      case EG_MV_BCHARGE: return EG_MV_PARRY;
      case EG_MV_LUNGE:
      case EG_MV_CHARGED: return ph == EG_PH_RECOVERY ? EG_MV_PARRY : EG_NO_MOVE;
      default: return EG_NO_MOVE;
    }
  }
  if(intent==EG_INT_BA && mv!=EG_MV_PARRY && mv!=EG_MV_CHARGED && mv!=EG_MV_HIT3) return EG_MV_LUNGE;
  return EG_NO_MOVE;
}

static uint8_t direction(uint8_t held) {
  static const uint8_t dir[16]={255u,2u,6u,255u,0u,1u,7u,0u,4u,3u,5u,4u,255u,2u,6u,255u};
  return dir[held&EG_DP_MASK];
}

static uint8_t movement(eg_input_t *s, uint8_t held) {
  uint8_t dir, dash=0u, rel;
  if(s->tap_age<EG_TAP_WINDOW+1u) s->tap_age++;
  if(s->dash_cd) s->dash_cd--;
  if(!(held&EG_DP_MASK)) { s->dp_prev=EG_NO_DIR;s->dp_hold=s->gait=0u;return 0u; }
  dir=direction(held);
  if(dir==EG_NO_DIR) s->dp_hold=0u;
  else if(dir==s->dp_prev) s->dp_hold=sat_inc(s->dp_hold);
  else {
    s->dp_hold=1u;
    if(s->dp_prev==EG_NO_DIR && s->tap_dir==dir && s->tap_age<=EG_TAP_WINDOW && !s->dash_cd) {
      rel=(uint8_t)((dir-s->tap_face)&7u);
      s->evade=rel==0u || rel==1u || rel==7u ? EG_EVADE_FORWARD : rel==2u || rel==6u ? EG_EVADE_SIDE : EG_EVADE_BACK;
      s->dash_dir=dir;s->dash_cd=20u;s->tap_dir=EG_NO_DIR;dash=1u;
    } else { s->tap_dir=dir;s->tap_age=0u;s->tap_face=s->facing; }
  }
  s->dp_prev=dir;
  s->gait=(uint8_t)(dir!=EG_NO_DIR && (s->dp_hold>=EG_RUN_HOLD || s->move==EG_MV_SPRINT || (s->move==EG_MV_DODGE && s->phase==EG_PH_RECOVERY)));
  return dash;
}

uint8_t eg_input_step(eg_input_t *s, uint8_t held) EG_BANKED {
  uint8_t pa = (uint8_t)(held & (uint8_t)~s->prev & EG_IN_A);
  uint8_t pb = (uint8_t)(held & (uint8_t)~s->prev & EG_IN_B);
  uint8_t b_rel=s->b_hold;
  uint8_t a_rel = s->a_hold; /* frames A was held before this frame */
  uint8_t intent = EG_INT_NONE, started = EG_NO_MOVE, mv, dash=movement(s,held);

  s->prev = held;
  s->a_hold = (held & EG_IN_A) ? (pa ? 1u : sat_inc(s->a_hold)) : 0u;
  s->b_hold = (held & EG_IN_B) ? (pb ? 1u : sat_inc(s->b_hold)) : 0u;
  if (pa) s->a_age = 0u;
  else if (s->a_age < EG_AGE_CAP) s->a_age++;
  if(pb) s->b_age=0u;
  else if(s->b_age<EG_AGE_CAP) s->b_age++;

  /* Ordered action presses: A>B counters, B>A rushes; a simultaneous chord counters. */
  if (pa && pb) intent = EG_INT_AB;
  else if (pb) intent = ((held & EG_IN_A) || s->a_age <= EG_AB_WIN) ? EG_INT_AB : EG_INT_B;
  else if (pa) intent = ((held&EG_IN_B) || s->b_age<=EG_AB_WIN) ? EG_INT_BA : EG_INT_A;
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
      if (!(held & EG_DP_MASK)) started = EG_MV_SKID;
    } else if(s->move==EG_MV_BCHARGE) {
      if(!(held&EG_IN_B)) {
        s->level=b_rel>=EG_CHARGE_L3 ? 3u : b_rel>=EG_CHARGE_L2 ? 2u : 1u;
        started=b_rel<EG_HOLD_T ? EG_MV_SHOT : EG_MV_BPOWER;
      }
    } else if (s->move == EG_MV_CHARGE) {
      if (!(held & EG_IN_A)) {
        s->level = a_rel >= EG_CHARGE_L3 ? 3u : (a_rel >= EG_CHARGE_L2 ? 2u : 1u);
        started = EG_MV_CHARGED;
      }
    } else if (s->frame >= total_of(s, s->move)) {
      s->move = EG_MV_IDLE;
      s->frame = 0u;
      s->chain = 0u;
    }
  }
  /* Ordered combos take precedence over a secondary release in the same frame. */
  if((started==EG_MV_SHOT || started==EG_MV_BPOWER) && (s->buf==EG_INT_BA || s->buf==EG_INT_AB)) {
    mv=resolve(s,s->buf);
    if(mv!=EG_NO_MOVE) {started=mv;s->buf=EG_INT_NONE;s->buf_age=0u;}
  }
  if (started != EG_NO_MOVE) start(s, started);
  else refresh(s);
  if(dash) {
    s->buf=EG_INT_NONE;
    start(s,EG_MV_DODGE);
    return EG_MV_DODGE;
  }

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

  /* Direction holds run; B starts charging on press and resolves its tap/charge on release. */
  if (started == EG_NO_MOVE) {
    if ((s->move == EG_MV_DODGE && s->dp_prev!=EG_NO_DIR && s->phase == EG_PH_RECOVERY) ||
        (s->move==EG_MV_IDLE && s->gait)) {
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

uint8_t eg_input_parry(const eg_input_t *s) EG_BANKED {
  if (s->flags & EG_FL_PERFECT) return 2u;
  if (s->flags & EG_FL_GUARD) return 1u;
  return 0u;
}
