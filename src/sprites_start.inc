/* Identical to spin % count. The common three-live-enemy pool needs only
 * nibble/two-bit folds (16 and 4 are both 1 modulo 3), not SM83 division.
 * Other supported pools use six bounded binary subtraction steps, avoiding
 * division and its helper-call stack work. count is in 1..8 (MAXE). */
static uint8_t sprite_start(uint8_t spin,uint8_t count) {
  uint8_t step;
  if(count==3u) {
    spin=(uint8_t)((spin&15u)+(spin>>4));
    spin=(uint8_t)((spin&3u)+(spin>>2));
    spin=(uint8_t)((spin&3u)+(spin>>2));
    return spin>=3u ? spin-3u : spin;
  }
  if(count==1u || count==2u || count==4u || count==8u)return spin&(count-1u);
  step=(uint8_t)(count<<5);
  do {
    if(spin>=step)spin-=step;
    step>>=1;
  } while(step>=count);
  return spin;
}
