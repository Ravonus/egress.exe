#ifndef EG_RUN_CASES_H
#define EG_RUN_CASES_H
#include "core/eg_plan.h"
/* Shared inputs, independent host/native construction. Includes shortcuts,
 * late curriculum, cycles and descent, plus a fresh front-door story run. */
#define RUN_CASES 24u
static void run_case(eg_run_in *in,uint8_t k) {
  memset(in,0,sizeof *in);
  in->run_seed=(uint16_t)((uint16_t)k*7919u+1u);
  in->cycle_seed=(uint16_t)((uint16_t)k*40503u+17u);
  in->generation=(uint8_t)(k*3u);in->lineage=(uint8_t)(k%4u);
  in->cycle=(uint8_t)(1u+k%5u);in->door=(uint8_t)(k%7u);
  in->mode=(uint8_t)(k && k%5u==0u);in->stratum=k;in->sky=(uint8_t)(k&1u);
  in->seen=((uint32_t)k*2654435761ul)&((1ul<<MK_ALL)-1ul);
  in->developed=in->seen&((uint32_t)k*40503ul);
  if(k%8u==0u)in->seen=in->developed=0u;
}
#endif
