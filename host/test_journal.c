#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../src/core/eg_journal.h"
int main(void) {
  uint8_t a[384],b[384],torn[384];unsigned n,cut,i;
  assert(eg_save_crc((const uint8_t *)"123456789",9u)==0x29b1u);
  for(n=64;n<=384;n+=64) {
    for(i=0;i<n;i++) a[i]=(uint8_t)(i*17u+9u);
    a[0]=1;a[1]=255;eg_save_seal(a,(uint16_t)n);assert(eg_save_valid(a,(uint16_t)n,1));
    memcpy(b,a,n);b[1]=0;b[20]^=15;eg_save_seal(b,(uint16_t)n);
    assert(eg_save_valid(b,(uint16_t)n,1) && eg_save_newer(b[1],a[1]));
    for(cut=0;cut<n;cut++) {
      memcpy(torn,a,n);torn[0]=0;
      if(cut) memcpy(torn+1,b+1,cut);
      assert(!eg_save_valid(torn,(uint16_t)n,1));
      assert(eg_save_valid(a,(uint16_t)n,1));
    }
    for(i=0;i<n;i++) {memcpy(torn,b,n);torn[i]^=1;assert(!eg_save_valid(torn,(uint16_t)n,1));}
  }
  puts("journal: CCITT vector, six record sizes, every torn-write prefix, every single-byte corruption and serial wrap pass");
  return 0;
}
