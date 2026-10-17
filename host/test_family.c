#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/eg_family.h"
#include "../src/core/eg_input.h"
int main(void) {
  eg_family f;
  eg_genome parent, kids[3], again[3];
  unsigned seed,k,j,traits=0,variants=0;
  int durations[5];
  for(seed=1;seed<=4096;seed++) {
    eg_founder(&parent,(uint16_t)seed);
    eg_children(&parent,(uint16_t)(seed*7919u),kids);
    eg_children(&parent,(uint16_t)(seed*7919u),again);
    assert(!memcmp(kids,again,sizeof kids));
    for(k=0;k<3;k++) {
      assert(kids[k].weapon[0]<5 && kids[k].weapon[1]<5);
      assert(kids[k].dodge[0]<3 && kids[k].dodge[1]<3 && eg_gene_dodge(&kids[k])<3);
      for(j=0;j<G_STATS;j++) assert(kids[k].stat[j]<4);
      assert(kids[k].look[G_PATTERN]!=(kids[(k+1)%3].look[G_PATTERN]));
      assert(kids[k].look[G_LOOK]!=kids[(k+1)%3].look[G_LOOK]);
      assert(kids[k].look[G_HAIR]!=kids[(k+1)%3].look[G_HAIR]);
      traits+=kids[k].traits[0]!=0;
    }
    variants+=kids[0].weapon[0]!=kids[1].weapon[0] || kids[1].weapon[0]!=kids[2].weapon[0];
    assert(kids[0].weapon[0]==parent.weapon[0] || kids[0].weapon[0]==parent.weapon[1]);
    assert(kids[0].dodge[0]==parent.dodge[0] || kids[0].dodge[0]==parent.dodge[1]);
  }
  assert(variants>2048 && traits==4096*3);
  eg_family_init(&f,123u);
  f.seen=0x1fffu;
  for(k=1;k<=20;k++) {
    eg_family_choose(&f,(uint8_t)(k%3));
    parent=f.current;
    eg_family_choose(&f,(uint8_t)((k+1)%3)); assert(!memcmp(&parent,&f.current,sizeof parent));
    eg_family_finish(&f,(uint8_t)(k%10+1),0);
    assert(f.generation==k+1 && !f.active && f.history[0].generation==k);
    assert(!memcmp(&f.parent,&parent,sizeof parent));
    assert(!memcmp(&f.history[0].gene,&parent,sizeof parent));
    assert(f.seen==0x1fffu);
  }
  assert(f.nrecord==EG_FAMILY_HISTORY && f.history[7].generation==13);
  for(k=0;k<5;k++) {
    eg_input_t in; eg_frames_t fd;
    eg_input_init(&in,0); in.weapon=(uint8_t)k;
    eg_input_step(&in,EG_IN_A); eg_input_step(&in,0);
    eg_input_attack_frames(&in,&fd);
    durations[k]=eg_input_total(&in,EG_MV_HIT1);
    assert(fd.startup+fd.active+fd.recovery==durations[k]);
    assert(in.phase==EG_PH_STARTUP || k==3);
    for(j=1;j<80;j++) eg_input_step(&in,0);
    assert(in.move==EG_MV_IDLE);
  }
  for(k=0;k<5;k++) for(j=k+1;j<5;j++) assert(durations[k]!=durations[j]);
  printf("family: 12288 inherited siblings, bounded stats, actual-parent history; weapon hit1 frames %d/%d/%d/%d/%d\n",
    durations[0],durations[1],durations[2],durations[3],durations[4]);
  return 0;
}
