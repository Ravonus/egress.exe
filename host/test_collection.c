#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "eg_collection.h"
#include "eg_journal.h"
int main(void) {
 uint8_t a[64],b[64],p[64],cut;eg_item_id id;
 memset(a,0,64);memset(b,255,64);assert(eg_collection_choose(a,b)==255);
 eg_collection_empty(a);assert(eg_collection_valid(a));a[1]=255;eg_save_seal(a,64);assert(eg_collection_choose(a,b)==0);
 memcpy(p,a,64);p[1]=0;assert(eg_collection_mark(p,444));eg_save_seal(p,64);assert(eg_collection_valid(p));
 // Power loss after every byte of an inactive-slot write preserves the old
 // record; its version byte is committed only after the payload and CRC.
 for(cut=0;cut<64;cut++) {
  memset(b,255,64);b[0]=0;if(cut)memcpy(b+1,p+1,cut);
  assert(eg_collection_choose(a,b)==0);assert(eg_collection_valid(a));
 }
 memcpy(b,p,64);assert(eg_collection_choose(a,b)==1);b[18]^=1;assert(eg_collection_choose(a,b)==0);
 eg_collection_empty(p);for(id=1;id<=444;id++)assert(eg_collection_mark(p,id));assert(p[2]==188 && p[3]==1);eg_save_seal(p,64);assert(eg_collection_valid(p));
 assert(!eg_collection_mark(p,444));p[59]|=128;eg_save_seal(p,64);assert(!eg_collection_valid(p));
 eg_collection_empty(p);p[2]=1;eg_save_seal(p,64);assert(!eg_collection_valid(p));
 puts("Collection survives every interrupted byte write, CRC corruption and serial wrap; exact 444-bit ownership count.");return 0;
}
