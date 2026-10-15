/* Host side of the idle scheduler test: prints the segment timeline of src/core/eg_idle.c for one case, as
 * "start end segment" lines; tools/test_idle.py compares it with proto/anim/heirs.py idle_plan() tick for tick.
 * usage: test_idle SEED TRAITS CTX UNTIL */
#include <stdio.h>
#include <stdlib.h>

#include "core/eg_idle.h"

int main(int argc, char **argv) {
  eg_idle_t s;
  unsigned t, until, start = 0;
  uint8_t cur;
  if (argc != 5) return 2;
  eg_idle_init(&s, (uint16_t)strtoul(argv[1], 0, 0), (uint8_t)strtoul(argv[2], 0, 0), (uint8_t)strtoul(argv[3], 0, 0));
  until = (unsigned)strtoul(argv[4], 0, 0);
  cur = s.seg;
  for (t = 1; t <= until; t++) {
    uint8_t ch = t < until ? eg_idle_step(&s) : 1u;
    if (ch) {
      printf("%u %u %u\n", start, t, cur);
      start = t;
      cur = s.seg;
    }
  }
  return 0;
}
