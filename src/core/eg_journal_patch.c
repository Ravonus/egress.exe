#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_journal.h"
#include "eg_journal_patch.inc"
/* Updating a few live fields takes at most eight table probes per field.
 * Keep the already sealed CRC exact instead of rescanning all 254 bytes. */
void eg_save_patch256(uint8_t *p,uint8_t at,uint8_t value) EG_BANKED {
 uint8_t delta,k=0u;uint16_t c;const uint16_t *bits;
 if(at>=254u)return;delta=p[at]^value;if(!delta)return;
 bits=patch_bits256[at];c=(uint16_t)(p[254]|((uint16_t)p[255]<<8));
 do{if(delta&1u)c^=bits[k];k++;delta>>=1;}while(delta);
 p[at]=value;p[254]=(uint8_t)c;p[255]=(uint8_t)(c>>8);
}
void eg_save_patch112(uint8_t *p,uint8_t at,uint8_t value) EG_BANKED {
 uint8_t delta,k=0u;uint16_t c;const uint16_t *bits;
 if(at>=110u)return;delta=p[at]^value;if(!delta)return;
 bits=patch_bits112[at];c=(uint16_t)(p[110]|((uint16_t)p[111]<<8));
 do{if(delta&1u)c^=bits[k];k++;delta>>=1;}while(delta);
 p[at]=value;p[110]=(uint8_t)c;p[111]=(uint8_t)(c>>8);
}
