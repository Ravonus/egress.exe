/* Exercise the supplied firmware itself, with an in-memory cartridge bus. No hardware IO. */
#include <assert.h>
#include "/private/tmp/egress-chromatic-research/shiny-source/shiny-relay-src-1.0.2/mcu/components/nw/cartflash/cf_esp.c"
#include "cf_usb.h"
static uint8_t image[131072], cart[131072];
static int saw_done,saw_verify,saw_header;
static int power(void *p){return 1;}
static int chunk(void *p,uint32_t off,uint16_t n,uint8_t *out){memcpy(out,image+off,n);return 1;}
static void line(void *p,const char *s){puts(s);if(strstr(s,"CBU1 done rom "))saw_done++;if(strstr(s,"CBU1 time rom verify "))saw_verify++;if(strstr(s,"CBU1 time rom check-header "))saw_header++;}
int main(int argc,char **argv){
 int corrupt=argc>1?atoi(argv[1]):0;
 for(unsigned i=0;i<sizeof image;i++)image[i]=(uint8_t)(i*17+(i>>8));
 memcpy(cart,image,sizeof cart);memset(cart,255,16384);
 cf_esp_t c={0};cf_usb_t u;c.h.power_ok=power;c.h.chunk=chunk;
 cf_usb_init(&u,&c,line,NULL);
 cf_sha256_t sha;uint8_t digest[32];char hash[65],crc[9],length[16];
 cf_sha256_init(&sha);cf_sha256_update(&sha,image,sizeof image);cf_sha256_final(&sha,digest);
 for(int i=0;i<32;i++)sprintf(hash+2*i,"%02x",digest[i]);
 snprintf(crc,sizeof crc,"%08x",cf_crc32(0,image,sizeof image));snprintf(length,sizeof length,"%u",(unsigned)sizeof image);
 char *args[]={"write","rom",length,crc,hash,"bin"};
 assert(cf_usb_cmd(&u,6,args)==0);
 assert(c.opt.strm_have_sha&&c.j.strm_src==1&&!memcmp(c.j.strm_sha,digest,32));
 c.nreg=1;c.reg[0]=(cf_region_t){16384,8};c.nsec=8;c.ovl_ok=1;c.s_wr_end=sizeof image;
 c.ph=CF_PH_STREAM;c.sp=SP_HASH0;c.pos=0;c.sbuf=malloc(CF_PROG_CHUNK);
 if(corrupt==1)cart[20000]^=1; /* bad body, source hash unchanged */
 if(corrupt==2)c.j.strm_sha[0]^=1; /* right CRC, wrong SHA target */
 for(unsigned i=1;i<1000&&!c.done_pending;i++){
  cf_usb_tick(&u,i*1000);stream_next(&c);if(c.done_pending)break;
  unsigned at=c.last.bank*BANK+c.last.addr-0x4000,n=c.last.len;
  if(c.last.cmd==CF_C_READ){assert(at+n<=sizeof cart);stream_result(&c,CF_R_OK,cart+at,n);}
  else if(c.last.cmd==CF_C_PROGRAM){memcpy(cart+at,c.sbuf,n);if(corrupt==3&&at<=256&&at+n>256)cart[256]^=1;stream_result(&c,CF_R_OK,NULL,0);}
  else if(c.last.cmd==CF_C_ERASE){memset(cart+at,255,16384);stream_result(&c,CF_R_OK,NULL,0);}
  else{fprintf(stderr,"unexpected command %u\n",c.last.cmd);return 2;}
 }
 assert(c.done_pending);int result=c.st.result;c.armed=0;c.done_pending=0;cf_usb_tick(&u,1000001);
 if(!corrupt){assert(result==CF_D_OK&&saw_done==1&&saw_verify==1&&saw_header==1);assert(!memcmp(cart,image,sizeof cart));}
 else{assert(result!=CF_D_OK&&saw_done==0);}
 printf("PASS scenario=%d result=%d done=%d verify=%d header=%d\n",corrupt,result,saw_done,saw_verify,saw_header);
 free(u.mem);return 0;
}
