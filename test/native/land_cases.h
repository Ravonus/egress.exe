#ifndef LAND_CASES_H
#define LAND_CASES_H
#include "core/eg_land.h"
#define LC_CASES 24u
static const uint8_t lc_sizes[LC_CASES]={0,1,2,3,4,5,1,2,3,4,5,1,0,0,0,1,1,2,1,3,3,2,3,4};
static uint8_t lc_plan(eg_land *p,uint8_t k) {
  return eg_land_plan(p,(uint16_t)(k*997u+17u),(uint8_t)(k%6u),lc_sizes[k],(uint8_t)((k*3u)&7u));
}
#endif
