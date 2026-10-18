#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/eg_room.h"
#include "../src/core/eg_stamina.h"
static void board(eg_room *r) {
  unsigned i;memset(r,0,sizeof *r);
  for(i=0;i<EG_N;i++)if(!EG_X(i) || EG_X(i)==EG_W-1 || !EG_Y(i) || EG_Y(i)==EG_H-1)r->c[i]=T_WALL;
}
int main(void) {
  eg_room r,before;eg_stamina s;unsigned w,p,g,n,h,i;uint8_t base=EG_IDX(6,8),body=EG_IDX(6,9);
  for(w=0;w<5;w++)for(p=0;p<4;p++)for(g=1;g<32;g++) {
    unsigned strength=eg_lift_strength(w,p,g);
    assert(strength>=1 && strength<=3);
    if(g<6 || p<2 || (w!=1 && w!=3))assert(strength==1);
    for(n=0;n<5;n++)for(h=0;h<5;h++)assert(eg_lift_allowed(n,h,strength)==(n && h && n*h<=strength));
  }
  assert(eg_lift_strength(1,3,65535)==3);
  assert(!eg_lift_allowed(255,255,3) && !eg_lift_allowed(1,1,255));
  assert(eg_lift_cost(1,1,0,0)<eg_lift_cost(1,0,0,0));
  assert(eg_lift_cost(1,1,3,1)<eg_lift_cost(1,1,0,1));
  for(n=1;n<=3;n++) {
    board(&r);for(i=0;i<n;i++)r.c[base-i*EG_W]=T_ROCK;
    before=r;assert(eg_lift_load(&r,base)==n);
    assert(!eg_lift_raise(&r,base,n-1));assert(!memcmp(&r,&before,sizeof r));
    assert(eg_lift_raise(&r,base,n)==n);assert(r.c[base]==T_EMPTY);
    for(i=1;i<=n;i++)assert(r.c[base-i*EG_W]==T_ROCK);
    body=base;eg_gravity(&r,&body,1);assert(r.c[base-EG_W]==T_ROCK);
    eg_gravity(&r,0,0);assert((r.c[base]&T_MASK)==T_ROCK); /* released weight really falls */
  }
  board(&r);r.c[base]=T_ROCK|F_PLATE;r.c[base-EG_W]=T_PLATE;eg_plates_dirty=0;
  assert(eg_lift_raise(&r,base,1));assert(r.c[base]==T_PLATE && r.c[base-EG_W]==(T_ROCK|F_PLATE));assert(eg_plates_dirty);
  board(&r);r.c[base]=T_ROCK|F_FALL;before=r;
  assert(!eg_lift_raise(&r,base,3));assert(!memcmp(&r,&before,sizeof r));
  board(&r);r.c[base]=T_ROCK;r.c[base-EG_W]=T_WALL;before=r;
  assert(!eg_lift_raise(&r,base,3));assert(!memcmp(&r,&before,sizeof r));
  board(&r);r.c[EG_IDX(6,0)]=T_ROCK;before=r;
  assert(!eg_lift_raise(&r,EG_IDX(6,0),3));assert(!memcmp(&r,&before,sizeof r));
  eg_stamina_begin(&s,0);assert(eg_stamina_spend(&s,12));
  for(i=0;i<500;i++)eg_stamina_tick(&s,1);assert(s.value==88 && s.wait==24);
  while(s.value) {if(!eg_stamina_spend(&s,eg_lift_cost(1,0,0,1)))s.value=0;eg_stamina_tick(&s,1);}
  for(i=0;i<100;i++)eg_stamina_tick(&s,1);assert(!s.value);
  for(i=0;i<28;i++)eg_stamina_tick(&s,0);assert(s.value==2);
  puts("lift: weight x height, class/generation limits, atomic stacks, plates, gravity release and no held recovery pass");
  return 0;
}
