#ifdef __SDCC
#pragma bank 255
#endif
#include <string.h>
#include "eg_pet_bag.h"
#include "eg_journal.h"
void eg_pet_bag_put(uint8_t *meta,uint8_t slot,uint8_t hp,uint8_t spent,uint8_t cooldown,uint8_t instance) EG_BANKED {
 uint8_t *p;if(slot>=EG_BAG_CAP)return;p=meta+slot*3u;
 p[0]=(uint8_t)((hp&PM_HP)|(spent ? PM_REVIVED : 0u));p[1]=cooldown;p[2]=instance;
}
void eg_pet_bag_remove(uint8_t *meta,uint8_t count_before,uint8_t slot) EG_BANKED {
 uint8_t n,*to,*from;
 if(!count_before || count_before>EG_BAG_CAP || slot>=count_before)return;
 n=(count_before-slot-1u)*3u;to=meta+slot*3u;from=to+3u;
 while(n--)*to++=*from++;
 memset(meta+(count_before-1u)*3u,0,3u);
}
uint8_t eg_pet_instance_next(uint8_t cursor,uint8_t active,const uint8_t *current,uint8_t ncurrent,const uint8_t *entry,uint8_t nentry,uint8_t entry_active) EG_BANKED {
 uint8_t tries,k,taken;
 if(ncurrent>24u || nentry>24u)return 0u;
 for(tries=0u;tries<52u;tries++){
  if(!++cursor)cursor=1u;taken=(uint8_t)(cursor==active || cursor==entry_active);
  for(k=0u;k<ncurrent && !taken;k++)if(current[k*3u+2u]==cursor)taken=1u;
  for(k=0u;k<nentry && !taken;k++)if(entry[k*3u+2u]==cursor)taken=1u;
  if(!taken)return cursor;
 }
 return 0u;
}
void eg_pet_record_pack(uint8_t *out,const uint8_t *entry,uint16_t seed,uint8_t floor,uint8_t serial) EG_BANKED {
 memset(out,0,EG_PET_RECORD_BYTES);out[0]=EG_PET_RECORD_VERSION;out[1]=serial;
 out[2]=(uint8_t)seed;out[3]=(uint8_t)(seed>>8);out[4]=floor;
 memcpy(out+8u,entry,EG_PET_ENTRY_BYTES);eg_save_seal(out,EG_PET_RECORD_BYTES);
}
uint8_t eg_pet_record_valid(const uint8_t *r,const uint8_t *lo,const uint8_t *hi,uint8_t n,uint16_t seed,uint8_t floor,uint8_t serial) EG_BANKED {
 eg_pet_state s;eg_pet_profile p;eg_item_id id;uint8_t k,j,instance,flags;const uint8_t *meta=r+26u;
 if(!eg_save_valid(r,EG_PET_RECORD_BYTES,EG_PET_RECORD_VERSION) || r[1]!=serial || r[2]!=(uint8_t)seed || r[3]!=(uint8_t)(seed>>8) || r[4]!=floor || !eg_bag_valid(lo,hi,n))return 0u;
 memcpy(&s,r+8u,sizeof s);
 if(s.id){
  if(!r[24] || !eg_pet_get(s.id,&p) || s.hp>p.hp)return 0u;
  if((s.state&PET_REVIVE_SPENT) && !(p.traits&PT_SECOND_SPRING))return 0u;
  if(!s.hp && (!(s.buff&PB_RISING) || !(s.state&PET_REVIVE_SPENT) || !s.auto_cd || s.auto_cd>8u))return 0u;
 } else if(r[24] || s.hp)return 0u;
 for(k=0u;k<24u;k++){
  id=k<n ? eg_bag_get(lo,hi,k) : 0u;flags=meta[k*3u];instance=meta[k*3u+2u];
  if(id<EG_PETS_FIRST){if(flags || meta[k*3u+1u] || instance)return 0u;continue;}
  if(!eg_pet_get(id,&p) || !instance || !(flags&PM_HP) || (flags&PM_HP)>p.hp || (flags&192u))return 0u;
  if((flags&PM_REVIVED) && !(p.traits&PT_SECOND_SPRING))return 0u;
  if(instance==r[24])return 0u;
  for(j=0u;j<k;j++)if(meta[j*3u+2u]==instance)return 0u;
 }
 return 1u;
}
