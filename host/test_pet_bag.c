#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "eg_pet_bag.h"
#include "eg_journal.h"
int main(void){
 uint8_t lo[24]={0},hi[3]={0},meta[72]={0},entry[90]={0},record[112],other[112],copy[72],a[72]={0},b[72]={0};
 uint8_t n,k,slot,serial=254u;unsigned cut;eg_pet_state s;
 assert(EG_PET_ENTRY_BYTES==90u);
 for(k=0u;k<24u;k++){assert(eg_bag_put(lo,hi,k,443u));eg_pet_bag_put(meta,k,(uint8_t)(k%8u+1u),k&1u,k+10u,k+1u);}
 for(slot=0u;slot<24u;slot++){
  memcpy(copy,meta,72u);eg_pet_bag_remove(copy,24u,slot);
  for(k=0u;k<23u;k++)assert(!memcmp(copy+k*3u,meta+(k+(k>=slot))*3u,3u));
  assert(!copy[69] && !copy[70] && !copy[71]);
 }
 eg_pet_start(&s,433u);memcpy(entry,&s,16u);entry[16]=25u;entry[17]=25u;memcpy(entry+18u,meta,72u);
 eg_pet_record_pack(record,entry,0x1234u,7u,serial);
 assert(eg_pet_record_valid(record,lo,hi,24u,0x1234u,7u,serial));
 /* A stored second spring is spent even when the critter is returned to its bag. */
 assert(record[26u+3u]&PM_REVIVED);
 assert(!eg_pet_record_valid(record,lo,hi,24u,0x1234u,7u,255u));
 assert(!eg_pet_record_valid(record,lo,hi,24u,0x1234u,8u,serial));
 assert(!eg_pet_record_valid(record,lo,hi,24u,0x1235u,7u,serial));
 for(cut=0u;cut<112u;cut++){
  memcpy(other,record,112u);other[0]=0u;memcpy(other+1u,record+1u,cut ? cut-1u : 0u);
  assert(!eg_pet_record_valid(other,lo,hi,24u,0x1234u,7u,serial));
 }
 for(k=0u;k<24u;k++){a[k*3u+2u]=k+1u;b[k*3u+2u]=k+25u;}
 assert(eg_pet_instance_next(0u,49u,a,24u,b,24u,50u)==51u);
 assert(eg_pet_instance_next(255u,49u,a,24u,b,24u,50u)==51u);
 /* Duplicate individual IDs, dead bag entries and invalid revival flags fail CRC-valid records. */
 memcpy(other,record,112u);other[26u+5u]=1u;eg_save_seal(other,112u);assert(!eg_pet_record_valid(other,lo,hi,24u,0x1234u,7u,serial));
 memcpy(other,record,112u);other[26u]=0u;eg_save_seal(other,112u);assert(!eg_pet_record_valid(other,lo,hi,24u,0x1234u,7u,serial));
 n=24u;assert(eg_bag_remove(lo,hi,&n,8u));eg_pet_bag_remove(meta,24u,8u);memcpy(entry+18u,meta,72u);eg_pet_record_pack(record,entry,0x1234u,7u,0u);
 assert(eg_pet_record_valid(record,lo,hi,n,0x1234u,7u,0u));
 puts("Individual critters retain health, cooldown and spent revival through compaction and CRC records; identity allocation avoids live and checkpoint copies.");return 0;
}
