/* Host build of the ported CRUCIBLE face generator (src/avatar, -DAVATAR_HOST): prints faces for the art tools.
 *   avatar_dump seed FACTION [SEED...]     one line per face: 576 tile bytes (hex), then 4 RGB555 colours
 *   avatar_dump genome HEX12               the face of a 6-byte genome
 * With "talk", a second line per face has the mouth open (avatar_say('A')) and a third the eyes shut (a blink). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define BANKED
#include "crucible_avatar_int.h"

static void out(void) {
  uint16_t p[4];
  avatar_palette(p);
  for (int i = 0; i < (int)AVATAR_BYTES; i++) printf("%02x", work_[i]);
  printf(" %04x %04x %04x %04x\n", p[0], p[1], p[2], p[3]);
}

int main(int argc, char **argv) {
  if (argc < 3) return 2;
  int talk = argc > 1 && !strcmp(argv[argc - 1], "talk");
  if (talk) argc--;
  if (!strcmp(argv[1], "genome")) {
    for (int a = 2; a < argc; a++) {
      uint8_t g[6];
      for (int i = 0; i < 6; i++) {
        unsigned v;
        sscanf(argv[a] + i * 2, "%2x", &v);
        g[i] = (uint8_t)v;
      }
      avatar_make_genome(g);
      out();
    }
    return 0;
  }
  int fac = atoi(argv[2]);
  for (int a = 3; a < argc; a++) {
    avatar_make((uint16_t)strtoul(argv[a], NULL, 0), (uint8_t)fac);
    out();
    if (talk) {
      avatar_say('A');
      avatar_tick(1);
      out();
      blink_ = 5;
      dirty_ = 1;
      avatar_tick(0);
      out();
    }
  }
  return 0;
}
