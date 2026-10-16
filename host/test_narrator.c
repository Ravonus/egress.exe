#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/eg_narrator.h"
int main(void) {
  eg_narrator s,again;char text[160],caption[15];uint16_t line,a,b;unsigned e,v,i,j,k,lines=0,emitted=0;
  assert(sizeof s==48);
  for(e=0;e<NE_COUNT;e++) for(v=0;v<4;v++) for(i=0;i<4;i++) for(j=0;j<8;j++) {
    line=(uint16_t)(e|(v<<5)|(i<<7)|(j<<9));eg_narr_text(line,text);eg_narr_caption(line,caption);
    assert(strlen(text)>20 && strlen(text)<160 && strlen(caption)>0 && strlen(caption)<=14);lines++;
  }
  eg_narr_begin(&s,123);assert(eg_narr_take(&s,0,0)==NARR_NONE);
  eg_narr_event(&s,NE_CLOSE,0);s.now=30;
  assert(eg_narr_take(&s,1,0)==NARR_NONE);line=eg_narr_take(&s,0,0);assert((line&31)==NE_CLOSE);
  eg_narr_event(&s,NE_MIX,0);s.now=75;
  assert(eg_narr_take(&s,0,0)==NARR_NONE); /* queued comment expires; it does not interrupt a later unrelated moment */
  eg_narr_event(&s,NE_MIX,0);assert(eg_narr_take(&s,0,0)==NARR_NONE);
  again=s;assert(eg_narr_take(&again,0,1)!=NARR_NONE); /* voice may speak before the much rarer text */
  s.now=120;eg_narr_event(&s,NE_MIX,0);assert((eg_narr_take(&s,0,0)&31)==NE_MIX);
  eg_narr_event(&s,NE_CLOSE,0);eg_narr_event(&s,NE_PUSH,0);eg_narr_event(&s,NE_PUSH,0);eg_narr_event(&s,NE_PUSH,0);
  assert(s.pending==NE_CLOSE); /* a minor furniture action cannot overwrite a close call */
  eg_narr_begin(&s,999);eg_narr_begin(&again,999);
  for(k=0;k<10000;k++) {
    e=k%NE_COUNT;s.now=again.now=(uint16_t)(30u+k*91u);
    eg_narr_event(&s,(uint8_t)e,0);eg_narr_event(&again,(uint8_t)e,0);
    a=eg_narr_take(&s,0,0);b=eg_narr_take(&again,0,0);assert(a==b && !memcmp(&s,&again,sizeof s));
    if(a!=NARR_NONE) {assert((a&31)==e);if(e==NE_USE) assert(((a>>5)&3)!=1);emitted++;}
  }
  assert(emitted>9900);assert(s.count[NE_USE]==255);
  printf("narrator: %u grammatical lines, captions <=14, 10000 deterministic event histories, cooldown/priority/expiry/last-item guards pass\n",lines);
  return 0;
}
