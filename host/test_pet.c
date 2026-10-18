#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "eg_pet.h"
int main(void) {
 eg_pet_state s;eg_pet_profile p;uint16_t id;uint8_t k,roles=0u,hp;char name[19];
 assert(sizeof s==16u && sizeof p==11u);
 for(id=433u;id<=444u;id++) {
  assert(eg_pet_get(id,&p));roles|=(uint8_t)(1u<<p.role);
  eg_pet_start(&s,id);assert(s.id==id && s.hp==p.hp && !s.cd && s.shown==255u);
  assert(!eg_pet_ready(&s,1u,p.command_cost-1u));
  assert(eg_pet_ready(&s,1u,p.command_cost)==p.command);
  assert(eg_pet_commit(&s,1u)==p.command_cost);
  assert(!eg_pet_ready(&s,1u,100u));
  for(k=1u;k<p.command_cd;k++) {eg_pet_tick(&s);assert(!eg_pet_ready(&s,1u,100u));}
  eg_pet_tick(&s);assert(eg_pet_ready(&s,1u,100u)==p.command);
  s.threat=255u;s.threat_t=2u;s.buff=PB_HASTE|PB_WARD;s.buff_t=2u;s.guard=4u;
  eg_pet_tick(&s);assert(s.threat==255u && s.buff && s.guard);
  eg_pet_tick(&s);assert(!s.threat && !s.buff && !s.guard);
  s.guard=3u;hp=s.hp;assert(eg_pet_hurt(&s,2u));assert(s.guard==1u && s.hp==hp && (s.state&7u)==PP_GUARD);
  assert(!eg_pet_hurt(&s,255u));assert(s.hp==hp); /* bounded hurt immunity */
  for(k=0u;k<3u;k++)eg_pet_tick(&s);
  s.threat=255u;s.threat_t=50u;s.buff=PB_MIGHT;s.buff_t=40u;
  assert(eg_pet_hurt(&s,255u));assert(!s.hp && !s.threat && s.buff==((p.traits&PT_SECOND_SPRING) ? PB_RISING : 0u));
  assert(!eg_pet_ready(&s,0u,100u) && !eg_pet_ready(&s,1u,100u));
  if(p.traits&PT_SECOND_SPRING){
   for(k=1u;k<8u;k++){eg_pet_tick(&s);assert(!s.hp);}
   eg_pet_tick(&s);assert(s.hp==((p.hp+1u)>>1));assert(s.auto_cd==p.auto_cd && (s.state&PET_REVIVE_SPENT));
   /* A restored copy has the same spent charge. A second lethal hit stays lethal. */
   {eg_pet_state restored;memcpy(&restored,&s,sizeof s);assert(eg_pet_hurt(&restored,255u));assert(!restored.hp && !restored.buff);s=restored;}
  }
  for(k=0u;k<250u;k++){eg_pet_tick(&s);assert(!s.hp);}
  eg_pet_ability_name(id,0u,name);assert(strlen(name)>0u && strlen(name)<=18u);
  eg_pet_ability_name(id,1u,name);assert(strlen(name)>0u && strlen(name)<=18u);
 }
 assert(roles==63u);assert(!eg_pet_get(0u,&p) && !eg_pet_get(432u,&p) && !eg_pet_get(445u,&p));
 eg_pet_start(&s,0u);assert(!s.id && !s.hp);
 puts("12 combat profiles, 24 abilities, cooldowns, stamina gates, threats/buffs, shields, permanent death and one explicit non-renewable revival pass.");return 0;
}
