#include <assert.h>
#include <stdio.h>
#include "eg_environment.h"
int main(void){
 static const uint8_t expected[EA_ACTIONS][EN_STATES]={
  {EN_SEED,255,255,255,EN_SEED,255,255,255},
  {255,EN_SPROUT,EN_TREE,255,255,255,255,255},
  {255,EN_ASH,EN_ASH,EN_ASH,255,255,EN_WATER,EN_ASH},
  {255,255,255,255,255,EN_ICE,255,255}
 };
 unsigned s,a,n=0;
 for(s=0;s<256;s++)for(a=0;a<256;a++){
  uint8_t got=eg_environment_next((uint8_t)s,(uint8_t)a),want=s<EN_STATES && a<EA_ACTIONS ? expected[a][s] : EN_NO_CHANGE;
  assert(got==want);n++;
 }
 s=EN_SOIL;s=eg_environment_next((uint8_t)s,EA_PLANT);s=eg_environment_next((uint8_t)s,EA_WATER);
 s=eg_environment_next((uint8_t)s,EA_WATER);assert(s==EN_TREE);
 s=eg_environment_next((uint8_t)s,EA_FIRE);assert(s==EN_ASH);
 assert(eg_environment_next((uint8_t)s,EA_PLANT)==EN_SEED);
 puts("Environment: 65,536 state/action inputs; finite plant/water/grow/burn/replant and freeze/thaw cycles.");return n==65536u ? 0 : 1;
}
