/* Streaming portable reference for the cartridge's assembly gravity regression. */
#include <stdio.h>
#include <string.h>
#include "../src/core/eg_room.h"

int main(void) {
  eg_room r;
  uint8_t head[5], result;
  while (fread(head, 1, sizeof head, stdin) == sizeof head) {
    memset(&r, 0, sizeof r);
    if (head[0] > 4 || fread(r.c, 1, EG_N, stdin) != EG_N) return 2;
    eg_plates_dirty = 1;
    result = eg_gravity(&r, head + 1, head[0]);
    if (fwrite(r.c, 1, EG_N, stdout) != EG_N || fwrite(&result, 1, 1, stdout) != 1) return 3;
    fflush(stdout);
  }
  return ferror(stdin) ? 4 : 0;
}
