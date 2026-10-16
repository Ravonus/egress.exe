#include <stdio.h>
#include <string.h>
#include "eg_solve.h"
#include "../src/core/eg_physics.h"
int main(void) {
  eg_room r,t;eg_stats st;char moves[8192];unsigned k,checks=0u;
  for(k=1u;k<=64u;k++) {
    eg_physics_gen(&r,PH_PORTAL,(uint16_t)(k*7919u),(uint8_t)(k&15u));
    /* Physical route: no crafted weight can conceal a broken crate route. */
    if(!eg_solve_ban(&r,60000,1ull<<RX_WEIGHT,moves,sizeof moves,&st) || !eg_replay(&r,moves) ||
       st.pushes<2 || !(st.reactions&(1ull<<RX_BURN))) return fprintf(stderr,"crate route %u\n",k),1;
    checks++;
    /* Independently remove the second island's crate: one flame and the mud
     * must become the weight, leaving exactly one flame for the final lock. */
    t=r;
    for(unsigned c=0u;c<EG_N;c++)if(EG_CRATE(t.c[c]) &&
       ((EG_X(c)<8u)==(EG_X(t.portal[1])<8u)))t.c[c]=T_EMPTY;
    if(!eg_solve(&t,250000,moves,sizeof moves,&st) || !eg_replay(&t,moves) || !st.combines ||
       !(st.reactions&(1ull<<RX_WEIGHT)) || !(st.reactions&(1ull<<RX_BURN))) return fprintf(stderr,"tool route %u nodes %d states %d combines %d reactions %llx plan %s\n",k,st.nodes,st.states,st.combines,(unsigned long long)st.reactions,moves),2;
    checks++;
    /* Two tempting but wrong mixes really spend the finite HOT supply. */
    t=r;t.item_piece[0]=P_BRICK;t.item_piece[1]=P_ENERGY;
    t.c[t.item_cell[2]]=t.c[t.item_cell[4]]=T_EMPTY;
    if(!(eg_dead(&t,t.start,0,0u)&EG_DEAD_DOOR))return fprintf(stderr,"missing resource deadlock %u\n",k),5;
    if(eg_solve(&t,60000,moves,sizeof moves,&st))return fprintf(stderr,"wrong-spend bypass %u\n",k),3;
    checks++;
    t=r;t.portal_need=0u;
    if(eg_solve(&t,60000,moves,sizeof moves,&st))return fprintf(stderr,"portal bypass %u\n",k),4;
    checks++;
  }
  printf("connected motifs: %u complete solver checks; crate/tool alternatives, finite wrong-spend branches and required portals\n",checks);
  return 0;
}
