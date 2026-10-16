#include <stdio.h>
#include <string.h>
#include "../test/native/move_cases.h"
int main(void) {
  uint8_t cells[1024],map[256],k,f;
  typedef char host_move_record[(sizeof(eg_mover_t)==22u) ? 1 : -1];
  (void)sizeof(host_move_record);
  for(k=0u;k!=MOVE_CASES;k++) {
    move_case(k,cells,map);
    for(f=0u;f!=MOVE_STEPS;f++) {
      move_sample(k,f);
      if(fwrite(&mc_mover,1,sizeof mc_mover,stdout)!=sizeof mc_mover)return 1;
    }
  }
  return 0;
}
