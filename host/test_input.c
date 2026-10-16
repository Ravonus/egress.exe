/* User control contract, 2026-10-15: A/B attacks; directional double taps evade; hold direction runs. */
#include <stdio.h>
#include "core/eg_input.h"
static int pass,fail;
#define CHECK(c,msg) do { if(c) pass++; else {fail++;printf("FAIL: %s\n",msg);} } while(0)
static void ticks(eg_input_t *s,uint8_t keys,unsigned n) {while(n--) eg_input_step(s,keys);}
static void timing_edges(void) {
  static const uint8_t lengths[]={10,11,12,29,30,49,50,200};
  eg_input_t s;
  unsigned i,f;
  for(i=0;i<sizeof lengths;i++) {
    uint8_t n=lengths[i];
    eg_input_init(&s,0);ticks(&s,EG_IN_A,n);eg_input_step(&s,0);
    CHECK((s.move==EG_MV_CHARGED)==(n>=11),"A tap/hold boundary");
    if(n>=11) CHECK(s.level==(n>=50 ? 3 : n>=30 ? 2 : 1),"charge level boundaries");
  }
  eg_input_init(&s,0);
  for(f=0;f<23;f++) {
    eg_input_step(&s,f ? 0 : EG_IN_A|EG_IN_B);
    CHECK(eg_input_parry(&s)==(f>=2 && f<=4 ? 2 : f>=5 && f<=9 ? 1 : 0),"perfect/guard/recovery windows");
  }
  for(i=2;i<=5;i++) {
    uint8_t linked=0;
    eg_input_init(&s,0);
    for(f=0;f<9;f++) if(eg_input_step(&s,f==0 || f==i ? EG_IN_A : 0)==EG_MV_HIT2) linked=1;
    CHECK(linked==(i>=3),"combo buffer expires at six frames");
  }
}
static uint32_t mash(unsigned style,unsigned weapon) {
  eg_input_t s;
  uint32_t rng=0x1234567u,hash=2166136261u;
  unsigned i;
  eg_input_init(&s,(uint8_t)style);s.weapon=(uint8_t)weapon;
  for(i=0;i<10000;i++) {
    uint8_t held,started;
    rng=rng*1103515245u+12345u;
    held=i%1000>=936 ? 0 : (uint8_t)((rng>>16)&63u);
    started=eg_input_step(&s,held);
    CHECK(s.move<EG_MV_COUNT,"mixed input keeps a valid move");
    CHECK(started==EG_NO_MOVE || started==s.move,"one reported move per frame");
    CHECK(!(s.flags&EG_FL_IFRAME) || s.move==EG_MV_DODGE,"only evade grants invulnerability");
    CHECK(!(s.flags&(EG_FL_GUARD|EG_FL_PERFECT)) || s.move==EG_MV_PARRY,"only parry grants guard");
    CHECK(!((s.flags&EG_FL_HITBOX) && (s.flags&(EG_FL_IFRAME|EG_FL_GUARD))),"attacks and defence do not overlap");
    CHECK(s.buf<=EG_INT_BA && s.buf_age<EG_BUF_FRAMES,"buffer remains bounded");
    CHECK(s.move!=EG_MV_BURST || (held&EG_IN_B),"repeat fire stops on release");
    if(i%1000==999) CHECK(s.move==EG_MV_IDLE,"released inputs settle to idle");
    hash=(hash^s.move)*16777619u;hash=(hash^s.frame)*16777619u;hash=(hash^s.flags)*16777619u;
  }
  return hash;
}
int main(void) {
  eg_input_t s;unsigned w,d,style,i;uint8_t dirs[8]={4,5,1,9,8,10,2,6};
  eg_input_init(&s,0);CHECK(eg_input_step(&s,EG_IN_A)==EG_MV_HIT1,"A taps attack");
  eg_input_init(&s,0);CHECK(eg_input_step(&s,EG_IN_B)==EG_MV_SHOT,"B taps attack");
  for(i=0;i<100;i++){eg_input_step(&s,EG_IN_B);CHECK(s.move!=EG_MV_DODGE && s.move!=EG_MV_SPRINT,"B never means dodge/run");}
  CHECK(s.move==EG_MV_BURST,"held B repeats");eg_input_step(&s,0);CHECK(s.move==EG_MV_IDLE,"release B stops repeat");
  eg_input_init(&s,0);ticks(&s,EG_IN_A,65);CHECK(s.move==EG_MV_CHARGE,"held A charges");
  eg_input_step(&s,0);CHECK(s.move==EG_MV_CHARGED && s.level==3,"release A fires full charge");
  eg_input_init(&s,0);eg_input_step(&s,EG_IN_A);CHECK(eg_input_step(&s,EG_IN_B)==EG_MV_PARRY,"A then B counter combo");
  eg_input_init(&s,0);eg_input_step(&s,EG_IN_B);CHECK(eg_input_step(&s,EG_IN_A)==EG_MV_LUNGE,"B then A rushing combo");
  eg_input_init(&s,0);CHECK(eg_input_step(&s,EG_IN_A|EG_IN_B)==EG_MV_PARRY,"same-frame chord deterministic");
  eg_input_init(&s,0);eg_input_step(&s,EG_IN_A);ticks(&s,0,9);CHECK(eg_input_step(&s,EG_IN_B)==EG_MV_SHOT,"late B is its own attack");
  for(style=0;style<3;style++) for(d=0;d<8;d++) {
    unsigned length=0,expect;
    eg_input_init(&s,(uint8_t)style);s.facing=0;
    eg_input_step(&s,dirs[d]);eg_input_step(&s,0);
    CHECK(eg_input_step(&s,dirs[d])==EG_MV_DODGE,"all directions double tap evade");
    CHECK(s.dash_dir==d,"evade follows tapped direction");
    CHECK(s.evade==(d==0||d==1||d==7 ? EG_EVADE_FORWARD : d==2||d==6 ? EG_EVADE_SIDE : EG_EVADE_BACK),"relative side/back/forward");
    expect=s.evade ? 12 : style==0 ? 18 : style==1 ? 14 : 20;
    do {length++;eg_input_step(&s,0);} while(s.move!=EG_MV_IDLE && length<100);
    CHECK(length==expect,"evade timing and release");
  }
  eg_input_init(&s,0);eg_input_step(&s,4);eg_input_step(&s,0);eg_input_step(&s,4);ticks(&s,4,15);
  CHECK(s.move==EG_MV_SPRINT,"double tap then hold runs after evade");
  eg_input_init(&s,0);
  for(i=0;i<40;i++){eg_input_step(&s,4);CHECK(s.move!=EG_MV_DODGE,"single held direction does not dodge");}
  CHECK(s.move==EG_MV_SPRINT,"single direction hold runs");eg_input_step(&s,0);CHECK(s.move==EG_MV_SKID,"release direction brakes");
  CHECK(eg_input_step(&s,EG_IN_A)==EG_MV_HIT1,"attack cancels brake");
  eg_input_init(&s,0);eg_input_step(&s,4);ticks(&s,0,EG_TAP_WINDOW+1);
  CHECK(eg_input_step(&s,4)!=EG_MV_DODGE,"separate late taps stay walking");
  eg_input_init(&s,0);ticks(&s,3,40);CHECK(s.move==EG_MV_IDLE && !s.gait,"opposites cancel");
  for(w=0;w<5;w++) {
    eg_input_init(&s,0);s.weapon=(uint8_t)w;ticks(&s,4,25);
    CHECK(eg_input_step(&s,4|EG_IN_B)==EG_MV_SHOT && s.gait,"run and shoot together");
    ticks(&s,4|EG_IN_B,40);CHECK(s.move==EG_MV_BURST && s.gait,"run and repeat fire together");
    {unsigned since=0,shots=0;for(i=0;i<2000;i++) {
      eg_input_step(&s,EG_IN_B);since++;
      if(!s.frame) {if(shots) CHECK(since==eg_input_shot_period((uint8_t)w),"held fire cadence survives long holds");shots++;since=0;}
    }CHECK(shots>100,"holding B keeps firing beyond frame 255");}
    CHECK(eg_input_shot_period((uint8_t)w)>=7,"bounded shot cadence");
  }
  timing_edges();
  for(style=0;style<3;style++) for(w=0;w<5;w++) {
    uint32_t a=mash(style,w),b=mash(style,w);
    CHECK(a==b,"mixed inputs replay deterministically for every weapon and dodge");
  }
  printf("input: %d checks, %d failures\n",pass+fail,fail);return fail ? 1 : 0;
}
