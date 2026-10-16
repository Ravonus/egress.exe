#ifdef __SDCC
#pragma bank 255
#endif
#include <string.h>
#include "eg_family.h"
static uint16_t frng;
static uint8_t rand8(void) {
  frng ^= frng << 7; frng ^= frng >> 9; frng ^= frng << 8;
  return (uint8_t)(frng ^ (frng >> 8));
}
static uint8_t pick(uint8_t n) { return (uint8_t)(((uint16_t)rand8() * n) >> 8); }
static const uint8_t looks_max[G_LOOKS] = {6u,3u,5u,4u,8u,8u,8u};
void eg_founder(eg_genome *g, uint16_t seed) EG_BANKED {
  uint8_t i;
  memset(g,0,sizeof *g);
  frng = seed ? seed : 1u;
  for (i=0; i!=G_LOOKS; i++) g->look[i] = pick(looks_max[i]);
  for (i=0; i!=2u; i++) {
    g->weapon[i] = pick(5u); g->dodge[i] = pick(3u);
    g->traits[i] = (uint8_t)(1u << pick(4u));
  }
  for (i=0; i!=G_STATS; i++) g->stat[i] = pick(4u);
  g->seed = frng;
}
uint8_t eg_gene_dodge(const eg_genome *g) EG_BANKED {
  /* Allele 0 roll dominates 1 slide, with blink recessive; native EG_DODGE order differs. */
  if (!g->dodge[0] || !g->dodge[1]) return 0u;
  return g->dodge[0] == 1u || g->dodge[1] == 1u ? 2u : 1u;
}
void eg_children(const eg_genome *parent, uint16_t seed, eg_genome *out3) EG_BANKED {
  eg_genome mate;
  uint8_t i,k,a;
  memset(out3,0,sizeof(eg_genome)*3u);
  eg_founder(&mate, (uint16_t)(seed ^ 0x792bu));
  frng = seed ? seed : 1u;
  for (k=0; k!=3u; k++) {
    eg_genome *g = &out3[k];
    for (i=0; i!=G_LOOKS; i++) {
      g->look[i] = rand8() < 184u ? parent->look[i] : mate.look[i];
      if (!rand8()) g->look[i] = pick(looks_max[i]);
    }
    g->weapon[0] = parent->weapon[pick(2u)]; g->weapon[1] = mate.weapon[pick(2u)];
    g->dodge[0] = parent->dodge[pick(2u)]; g->dodge[1] = mate.dodge[pick(2u)];
    g->traits[0] = parent->traits[pick(2u)]; g->traits[1] = mate.traits[pick(2u)];
    /* Different parental allele order gives siblings meaningful expressed weapon choices. */
    if (k & 1u) { a=g->weapon[0]; g->weapon[0]=g->weapon[1]; g->weapon[1]=a; }
    for (i=0; i!=G_STATS; i++) {
      a = rand8() < 184u ? parent->stat[i] : mate.stat[i];
      if (rand8() < 23u) { if (rand8()&1u) { if (a<3u) a++; } else if (a) a--; }
      g->stat[i] = a;
    }
    g->seed = frng;
    /* Authored sibling differentiation is visual only; abilities remain inherited alleles. */
    g->look[G_PATTERN] = (uint8_t)((parent->look[G_PATTERN] + k) & 7u);
    g->look[G_ACCENT] = (uint8_t)((parent->look[G_ACCENT] + k * 3u) & 7u);
  }
}
void eg_family_init(eg_family *f, uint16_t seed) EG_BANKED {
  memset(f,0,sizeof *f);
  f->generation = 1u; f->rng = seed ? seed : 1u;
  eg_founder(&f->parent,f->rng);
  eg_children(&f->parent,f->rng,f->child);
}
void eg_family_choose(eg_family *f, uint8_t choice) EG_BANKED {
  if (f->active || choice >= 3u) return;
  f->selected = choice; f->current = f->child[choice]; f->active = 1u;
}
void eg_family_finish(eg_family *f, uint8_t floor, uint8_t won) EG_BANKED {
  uint8_t i;
  if (!f->active) return;
  for (i=EG_FAMILY_HISTORY-1u; i; i--) f->history[i]=f->history[i-1u];
  f->history[0].gene=f->current; f->history[0].generation=f->generation;
  f->history[0].floor=floor; f->history[0].won=won;
  if (f->nrecord<EG_FAMILY_HISTORY) f->nrecord++;
  f->parent=f->current;
  if (f->generation!=65535u) f->generation++;
  f->rng=(uint16_t)(f->rng * 40503u + f->current.seed + floor + 1u);
  if (!f->rng) f->rng=1u;
  eg_children(&f->parent,f->rng,f->child);
  f->active=0u;
}
