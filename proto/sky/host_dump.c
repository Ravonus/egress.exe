/* Host driver for the sky engine: the same C as the cartridge, printing what verify.py compares.
 *   host_dump pos  D0 D1 STEP_MIN   date mod and the 10 live positions (binary angles), D0..D1 days since 2000
 *   host_dump read D0 D1 STEP_MIN   date mod flags phase8 eclipse retro station moon_sign voc_end
 *   host_dump events                every baked event, decoded by walking sky_find: stream index minute state flag
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sky.h"

extern uint8_t sky_force_live;
int main(int argc, char **argv) {
  uint16_t p[SKY_NPOS];
  if (getenv("SKY_LIVE")) sky_force_live = 1;
  if (argc >= 5 && (!strcmp(argv[1], "pos") || !strcmp(argv[1], "read"))) {
    long d0 = atol(argv[2]), d1 = atol(argv[3]), st = atol(argv[4]), t;
    for (t = d0 * 1440 + (argc > 5 ? atol(argv[5]) : 0); t < d1 * 1440; t += st) {
      uint16_t date = (uint16_t)(t / 1440), mod = (uint16_t)(t % 1440);
      if (argv[1][0] == 'p') {
        int i;
        sky_positions(date, mod, p);
        printf("%u %u", date, mod);
        for (i = 0; i < SKY_NPOS; i++) printf(" %u", p[i]);
        printf("\n");
      } else {
        sky_now n;
        sky_read(date, mod, &n);
        printf("%u %u %u %u %u %u %u %u %ld\n", date, mod, n.flags, n.phase8, n.eclipse, n.retro, n.station, n.moon_sign,
               (long)(int32_t)n.voc_end);
      }
    }
    return 0;
  }
  if (argc >= 2 && !strcmp(argv[1], "events")) {
    uint8_t s;
    for (s = 0; s < SKY_NSTREAM; s++) {
      sky_ev a, b;
      uint32_t m = 0;
      sky_find(s, 0, &a, &b);
      if (a.at != SKY_NO_EVENT) printf("%u %u %lu %u %u\n", s, a.i, (unsigned long)a.at, a.state, a.flag);
      while (b.at != SKY_NO_EVENT) {
        printf("%u %u %lu %u %u", s, b.i, (unsigned long)b.at, b.state, b.flag);
        if (s == SKY_S_PHASE && !(b.state & 1u)) {
          int8_t off = 0;
          uint8_t f = sky_mark(b.i, &off);
          printf(" %u %d", f, off);
        }
        printf("\n");
        m = b.at;
        sky_find(s, m, &a, &b);
      }
    }
    return 0;
  }
  fprintf(stderr, "usage: host_dump pos|read D0 D1 STEP | events\n");
  return 2;
}
