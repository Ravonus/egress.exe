#ifdef __SDCC
#pragma bank 255
#endif
#include <string.h>
#include "eg_pet.h"
#include "eg_pet_data.inc"
uint8_t eg_pet_get(eg_item_id id,eg_pet_profile *out) EG_BANKED {
 if(id<EG_PETS_FIRST || id>EG_DISCOVERIES){memset(out,0,sizeof *out);return 0u;}
 memcpy(out,&profiles[id-EG_PETS_FIRST],sizeof *out);return 1u;
}
void eg_pet_start(eg_pet_state *s,eg_item_id id) EG_BANKED {
 eg_pet_profile p;memset(s,0,sizeof *s);s->shown=255u;
 if(!eg_pet_get(id,&p))return;s->id=id;s->hp=p.hp;s->ox=-16;s->oy=4;s->state=32u;
}
void eg_pet_tick(eg_pet_state *s) EG_BANKED {
 eg_pet_profile p;if(!s->id)return;
 if(s->cd)s->cd--;if(s->auto_cd)s->auto_cd--;if(s->invuln)s->invuln--;
 if(s->threat_t && !--s->threat_t)s->threat=0u;
 if(s->buff_t && !--s->buff_t){s->buff=0u;s->guard=0u;}
 if(!s->hp){s->threat=0u;s->threat_t=0u;s->guard=0u;
  if((s->buff&PB_RISING) && !s->auto_cd && eg_pet_get(s->id,&p)){
   s->hp=(uint8_t)((p.hp+1u)>>1);s->auto_cd=p.auto_cd;s->buff=0u;s->shown=255u;
   s->state=(s->state&248u)|PP_HEAL;s->timer=24u;
  }
 }
}
uint8_t eg_pet_ready(const eg_pet_state *s,uint8_t manual,uint8_t stamina) EG_BANKED {
 eg_pet_profile p;
 if(!s->hp || !eg_pet_get(s->id,&p) || (manual ? s->cd : s->auto_cd))return 0u;
 return stamina>=(manual ? p.command_cost : p.auto_cost) ? (manual ? p.command : p.automatic) : 0u;
}
uint8_t eg_pet_commit(eg_pet_state *s,uint8_t manual) EG_BANKED {
 eg_pet_profile p;if(!s->hp || !eg_pet_get(s->id,&p))return 0u;
 if(manual)s->cd=p.command_cd;else s->auto_cd=p.auto_cd;
 s->timer=24u;s->state=(s->state&248u)|PP_CAST;
 return manual ? p.command_cost : p.auto_cost;
}
uint8_t eg_pet_hurt(eg_pet_state *s,uint8_t damage) EG_BANKED {
 eg_pet_profile p;uint8_t absorb;
 if(!s->hp || s->invuln || !damage || !eg_pet_get(s->id,&p))return 0u;
 if(s->guard){absorb=damage<s->guard ? damage : s->guard;s->guard-=absorb;damage-=absorb;}
 if(damage){if(p.armour && damage>1u)damage--;s->hp=s->hp>damage ? s->hp-damage : 0u;}
 s->invuln=3u;
 s->state=(s->state&248u)|(damage ? PP_HURT : PP_GUARD);s->timer=damage ? 18u : 12u;
 if(!s->hp){
  s->threat=s->threat_t=s->buff=s->buff_t=s->guard=0u;s->auto_cd=0u;s->state=(s->state&248u)|PP_DEATH;s->timer=24u;
  if((p.traits&PT_SECOND_SPRING) && !(s->state&PET_REVIVE_SPENT)){
   s->state|=PET_REVIVE_SPENT;s->buff=PB_RISING;s->auto_cd=8u;
  }
 }
 return 1u;
}
void eg_pet_ability_name(eg_item_id id,uint8_t manual,char *out) EG_BANKED {
 const char *s;uint8_t k=0u;
 if(id<EG_PETS_FIRST || id>EG_DISCOVERIES){out[0]=0;return;}
 s=ability_names[id-EG_PETS_FIRST][manual ? 1u : 0u];
 while(s[k]){out[k]=s[k];k++;}out[k]=0;
}
