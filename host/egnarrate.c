#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/eg_narrator.h"
int main(int argc,char **argv) {
  unsigned e,v,i,j;char text[160],caption[15];uint16_t line;
  if(argc==2 && !strcmp(argv[1],"--catalogue")) {
    for(e=0;e<NE_COUNT;e++) for(v=0;v<4;v++) for(i=0;i<4;i++) for(j=0;j<8;j++) {
      line=(uint16_t)(e|(v<<5)|(i<<7)|(j<<9));eg_narr_text(line,text);eg_narr_caption(line,caption);
      printf("%u\t%s\t%s\n",line,caption,text);
    }
    return 0;
  }
  if(argc!=2) {fputs("Usage: egnarrate LINE_ID | --catalogue\n",stderr);return 1;}
  line=(uint16_t)strtoul(argv[1],0,0);eg_narr_text(line,text);puts(text);return !text[0];
}
