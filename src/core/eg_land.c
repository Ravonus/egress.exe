#ifdef __SDCC
#pragma bank 255
#endif
#include <string.h>
#include "eg_land.h"
#include "eg_piece.h"
#include "../gen/stage_codec.inc"
#ifdef __SDCC
/* One KiB of immutable products replaces the generator's repeated constant
 * multiplies. The portable oracle below retains the original arithmetic. */
#define LP(n,m) (uint16_t)((uint32_t)(n)*(m))
#define LP16(n,m) LP(n,m),LP((n)+1,m),LP((n)+2,m),LP((n)+3,m), \
  LP((n)+4,m),LP((n)+5,m),LP((n)+6,m),LP((n)+7,m), \
  LP((n)+8,m),LP((n)+9,m),LP((n)+10,m),LP((n)+11,m), \
  LP((n)+12,m),LP((n)+13,m),LP((n)+14,m),LP((n)+15,m)
#define LP64(n,m) LP16(n,m),LP16((n)+16,m),LP16((n)+32,m),LP16((n)+48,m)
static const uint16_t product251[256]={LP64(0,251u),LP64(64,251u),LP64(128,251u),LP64(192,251u)};
static const uint16_t product1387[256]={LP64(0,1387u),LP64(64,1387u),LP64(128,1387u),LP64(192,1387u)};
#undef LP64
#undef LP16
#undef LP
#endif
uint8_t eg_land_water_style(uint8_t biome) EG_BANKED {return biome<EL_BIOMES ? land_biomes[biome][3] : 0u;}
/* Separate, stateless stream: decorations and window order never consume the
 * combat RNG or alter puzzle placement. All arithmetic is identical on SM83. */
static uint16_t hash(uint16_t seed,uint8_t a,uint8_t b) {
#ifdef __SDCC
  uint16_t v=(uint16_t)(seed^product251[a]^product1387[b]);
  uint8_t lo=(uint8_t)v,hi=(uint8_t)(v>>8);
  /* The same three xorshifts expressed as byte operations. Avoid general
   * word shifts in each terrain query; the host keeps the independent form. */
  hi^=(uint8_t)((lo>>1)|(hi<<7));lo^=(uint8_t)(lo<<7);
  lo^=(uint8_t)(hi>>1);hi^=lo;
  return (uint16_t)(lo|((uint16_t)hi<<8));
#else
  uint16_t v=(uint16_t)(seed^((uint16_t)a*251u)^((uint16_t)b*1387u));
  v^=(uint16_t)(v<<7);v^=v>>9;v^=(uint16_t)(v<<8);return v;
#endif
}
static uint8_t pick(uint8_t bits,uint16_t roll) {
  uint8_t n=0u,i;
  for(i=0u;i<8u;i++)if(bits&(1u<<i))n++;
  n=(uint8_t)(roll%n);
  for(i=0u;i<8u;i++)if(bits&(1u<<i)) {if(!n)return i;n--;}
  return 0u;
}
static uint8_t distance(uint8_t a,uint8_t b) {return a>b ? a-b : b-a;}
#ifdef __SDCC
static uint8_t centre_phase;
#endif
static uint8_t centre(const eg_land *p,uint8_t segment) {
  uint8_t cross=EL_CROSS(p),amplitude=(uint8_t)((cross-10u)>>1),period,phase;
  if(amplitude>12u)amplitude=12u;
  period=(uint8_t)(amplitude*4u);
#ifdef __SDCC
  /* The seed's remainder is immutable. At most sixteen map segments remain,
   * so this bounded byte reduction replaces two repeated 16-bit divisions. */
  phase=(uint8_t)(segment*4u+centre_phase);
  while(phase>=period)phase=(uint8_t)(phase-period);
#else
  phase=(uint8_t)(((uint16_t)segment*4u+(uint8_t)hash(p->seed,0u,71u))%period);
#endif
  if(phase>amplitude*2u)phase=(uint8_t)(period-phase);
  return (uint8_t)(cross/2u-amplitude+phase);
}
uint8_t eg_land_lane(const eg_land *p,uint8_t u) EG_BANKED {
  uint8_t k=(uint8_t)(u>>3),f=(uint8_t)(u&7u),a,b;
#ifdef __SDCC
  /* Four-cell prefetch and structural queries repeatedly visit one segment.
   * Cache only the two immutable interpolation endpoints, keyed by every
   * input to centre(). The independent host oracle always recomputes them. */
  static uint16_t cached_seed;
  static uint8_t cached_cross,cached_segment,cached_a,cached_b;
  if(cached_cross!=EL_CROSS(p) || cached_seed!=p->seed) {
    a=(uint8_t)((EL_CROSS(p)-10u)>>1);if(a>12u)a=12u;
    centre_phase=(uint8_t)hash(p->seed,0u,71u)%(uint8_t)(a*4u);
  }
  if(cached_cross!=EL_CROSS(p) || cached_seed!=p->seed || cached_segment!=k) {
    cached_seed=p->seed;cached_cross=EL_CROSS(p);cached_segment=k;
    cached_a=centre(p,k);cached_b=centre(p,(uint8_t)(k+1u));
  }
  a=cached_a;b=cached_b;
#else
  a=centre(p,k);b=centre(p,(uint8_t)(k+1u));
#endif
#ifdef __SDCC
  /* Adjacent triangle endpoints differ by at most four. Interpolate with
   * seven bounded byte additions instead of two general 16-bit multiplies.
   * A descending span rounds upward before subtraction, preserving floor. */
  k=0u;
  if(b>=a) {while(f--)k=(uint8_t)(k+b-a);return (uint8_t)(a+(k>>3));}
  while(f--)k=(uint8_t)(k+a-b);
  return (uint8_t)(a-((k+7u)>>3));
#else
  return (uint8_t)(((uint16_t)a*(8u-f)+(uint16_t)b*f)>>3);
#endif
}
static void item(eg_land *p,uint8_t u,uint8_t v,uint8_t piece) {
  eg_land_item *it=&p->item[p->nitem++];it->u=u;it->v=v;it->piece=piece;
}
uint8_t eg_land_plan(eg_land *p,uint16_t seed,uint8_t rule,uint8_t size,uint8_t biome) EG_BANKED {
  const uint8_t *r,*s;uint8_t k,spacing,capacity,count,kind,mask,v,u,channels=0u,required,needed=0u;
  eg_land_node *n;
  if(!p || (rule&127u)>=EL_RULES || biome>=EL_BIOMES)return 0u;
  r=land_rules[rule&127u];
  if(size==255u)size=pick(r[3],hash(seed,0u,11u));
  if(size>=EL_SIZES || !(r[3]&(1u<<size)))return 0u;
  s=land_sizes[size];memset(p,0,sizeof *p);
  required=r[5];for(k=0u;k<EL_GOALS;k++)if(required&(1u<<k))needed++;
  p->seed=seed;p->size=size;p->biome=biome;p->rule=rule;
  p->width=(uint8_t)(s[0]+4u*(hash(seed,0u,12u)%((s[1]-s[0])/4u+1u)));
  p->height=(uint8_t)(s[2]+4u*(hash(seed,0u,13u)%((s[3]-s[2])/4u+1u)));
  p->shape=pick(r[2],hash(seed,0u,14u));
  /* Some maps progress north/south, some widen east/west. Do not pretend a
   * large footprint must have more locks: scale and thinking are separate. */
  p->vertical=(uint8_t)(p->height>=26u && (hash(seed,0u,15u)&3u)==0u);
  capacity=(uint8_t)((EL_LENGTH(p)-6u)/10u);if(capacity>EL_NODES)capacity=EL_NODES;
  count=(uint8_t)(s[4]+hash(seed,0u,16u)%(s[5]-s[4]+1u));
  if(count>capacity)count=capacity;
  if(!(r[0]&((1u<<EL_HEAT)|(1u<<EL_COLD))) && count>2u)count=2u;
  if(needed && count<needed) {
    p->vertical=0u;capacity=(uint8_t)((p->width-6u)/10u);
    if(capacity<needed) { /* selected profile has a valid wider realization */
      p->width=s[1];capacity=(uint8_t)((p->width-6u)/10u);
    }
    if(capacity<needed)return 0u;
    count=needed;
  }
  p->nnode=count;spacing=(uint8_t)((EL_LENGTH(p)-6u)/count);
  for(k=0u;k<count;k++) {
    n=&p->node[k];u=(uint8_t)(spacing*(k+1u));v=eg_land_lane(p,u);
    mask=r[0];
    /* Each circuit occupies one of the native room's two named channels.
     * The mandatory powered transport consumes its preceding load channel. */
    if(channels>=2u)mask&=(uint8_t)~((1u<<EL_LOAD)|(1u<<EL_POWER));
    if(required) {for(kind=0u;kind<EL_GOALS;kind++)if(required&(1u<<kind))break;required&=(uint8_t)~(1u<<kind);}
    else {mask&=(uint8_t)~(1u<<EL_PORTAL);kind=pick(mask,hash(seed,k,22u));}
    n->kind=kind;n->at=u;n->lane=v;
    if(kind==EL_LOAD || kind==EL_POWER)channels++;
    n->condition=kind==EL_LOAD ? pick(r[1],hash(seed,k,23u)) : EL_HOLD;
    n->needs=k ? (uint8_t)(1u<<(k-1u)) : 0u;
    n->alternatives=r[4];
    /* Every resource is supplied before its own cut, with persistent identity.
     * A physical load route preserves the two optional crafting ingredients. */
    if(kind==EL_LOAD) {item(p,u-5u,v-2u,P_FIRE);item(p,u-5u,v-1u,P_MUD);}
    else if(kind==EL_HEAT)item(p,u-5u,v-2u,P_FIRE);
    else if(kind==EL_COLD) {item(p,u-5u,v-2u,P_AIR);item(p,u-5u,v-1u,P_RAIN);item(p,u-5u,v,P_MUD);}
    else if(kind==EL_POWER) {item(p,u-5u,v-2u,P_ENERGY);item(p,u-5u,v-1u,P_METAL);}
  }
  return 1u;
}
uint8_t eg_land_item_at(const eg_land *p,uint8_t x,uint8_t y) EG_BANKED {
  uint8_t k,u=p->vertical ? y : x,v=p->vertical ? x : y;
  for(k=0u;k<p->nitem;k++)if(p->item[k].u==u && p->item[k].v==v)return p->item[k].piece;
  return 0u;
}
static uint8_t floor_at(const eg_land *p,uint8_t u,uint8_t v) {
  uint8_t lane,d,radius,k;
  if(!u || !v || u>=EL_LENGTH(p)-1u || v>=EL_CROSS(p)-1u)return 0u;
  /* The approach, working space and retreat around apparatus are reserved
   * before any geography, hole or foliage is considered. */
  for(k=0u;k<p->nnode;k++)if(u>=p->node[k].at-6u && u<=p->node[k].at+3u && distance(v,p->node[k].lane)<=3u)return 2u;
  lane=eg_land_lane(p,u);d=distance(lane,v);
  if(d<=2u)return 1u;
  radius=(uint8_t)(3u+(hash(p->seed,(uint8_t)(u>>3),40u)&3u));
  if(p->shape==EL_BASINS)radius=(uint8_t)(radius+EL_CROSS(p)/5u);
  else if(p->shape==EL_BEND)radius=3u;
  else if(p->shape==EL_COURTS) {
    radius=(uint8_t)(radius+3u);
    if(d>3u && ((u+3u)&15u)<3u && (hash(p->seed,u>>4,41u)&1u))return 0u;
  }
  return d<=radius;
}
void eg_land_at(const eg_land *p,uint8_t x,uint8_t y,eg_land_cell *o) EG_BANKED {
  uint8_t u,v,k,lane,d,h,protected=0u,inside;const eg_land_node *n;const uint8_t *b;
  o->material=T_PIT|F_ABYSS;o->design=EL_FLOOR;o->variant=o->flags=0u;
  if(x>=p->width || y>=p->height)return;
  u=p->vertical ? y : x;v=p->vertical ? x : y;
  inside=floor_at(p,u,v);protected=(uint8_t)(inside==2u);
  if(!inside)for(k=0u;k<p->nnode;k++) {
    n=&p->node[k];
    if(n->kind!=EL_PORTAL && distance(u,n->at)<=1u &&
       (floor_at(p,n->at-1u,v) || floor_at(p,n->at,v) || floor_at(p,n->at+1u,v))) {inside=1u;break;}
  }
  if(!inside)return;
  o->material=T_EMPTY;o->flags=protected ? EL_PROTECTED : 0u;
  b=land_biomes[p->biome];
  h=(uint8_t)hash(p->seed,x,y);o->variant=(uint8_t)(h&3u);
  if(!protected) {
    lane=eg_land_lane(p,u);d=distance(v,lane);
    if(d>2u) {
      /* Streams are reactive terrain beside the traversable spine. */
      if((d==4u || d==5u) && (uint8_t)hash(p->seed,0u,42u)<b[1]) {
        o->material=T_WATER;o->design=EL_RIVER;
        if(!floor_at(p,u,(uint8_t)(v+1u)))o->design=EL_FALL;
        o->variant=b[3];
      } else if(h<b[0]) {
        o->material=T_DIRT;o->design=(uint8_t)(EL_FERN+(h%3u));
      } else if((uint8_t)(h^0x99u)<b[2])o->design=EL_PROP;
    }
  }
  for(k=0u;k<p->nnode;k++) {
    n=&p->node[k];
    /* A one-column wall can be bypassed by rolling around its island edge.
     * Three-column structures share the union of their supporting footprint.
     * Their deliberate doorways still accept the authored alternative verbs. */
    if(distance(u,n->at)<=1u) {
      if(n->kind==EL_PORTAL) {o->material=T_PIT|F_ABYSS;o->design=EL_FLOOR;o->flags=EL_PROTECTED;return;}
      o->material=T_WALL;o->design=EL_FLOOR;o->flags=EL_PROTECTED;
      if(v==n->lane || (n->kind==EL_COLD && (n->alternatives&EL_JUMP) && v==n->lane+2u)) {
        o->material=T_EMPTY;
        if(u==n->at) {
          o->material=n->kind==EL_LOAD || n->kind==EL_POWER ? T_GATE : n->kind==EL_HEAT ? T_PAPER :
            v!=n->lane ? T_PIT : T_WATER;
          if(o->material==T_WATER) {o->design=EL_RIVER;o->variant=b[3];}
        }
      }
      if(n->kind!=EL_PORTAL && u==n->at-1u && v==(uint8_t)(n->lane+((p->seed>>k)&1u ? 1 : -1)))o->material=T_WALL|F_BENCH;
      return;
    }
    if(n->kind==EL_LOAD) {
      if(u==n->at-3u && v==n->lane-1u)o->material=T_PLATE;
      if(u==n->at-3u && v==n->lane+1u)o->material=T_BLOCK;
      if(n->condition>=EL_EITHER) {
        if(u==n->at-4u && v==n->lane+2u)o->material=T_PLATE;
        if(u==n->at-3u && v==n->lane+2u)o->material=T_BLOCK;
      }
    } else if(n->kind==EL_POWER && u==n->at-3u && v==n->lane-1u)o->material=T_SOCKET;
    else if(n->kind==EL_PORTAL && distance(u,n->at)==2u && v==n->lane) {o->design=EL_PAD;o->flags=EL_PROTECTED;}
  }
  if((p->rule&EL_HAZARDS) && o->material==T_EMPTY) {
    /* Small interior voids and brittle shoulders, beside a permanent walking
     * route. Apparatus, ingredients and all three doorway cells stay intact. */
    for(k=0u;k<p->nnode;k++) {
      n=&p->node[k];
      if(u==n->at+3u && v==n->lane-2u)o->material=T_PIT|F_ABYSS;
      else if(u==n->at+3u && v==n->lane-1u)o->material=T_GLASS;
      else if(u==n->at-4u && v==n->lane+3u)o->material=T_CEIL;
    }
    if(!protected && distance(v,eg_land_lane(p,u))==1u && (u&15u)==6u && u>5u && u<EL_LENGTH(p)-5u)
      o->material=T_PIT|F_ABYSS;
  }
  if(eg_land_item_at(p,x,y))o->material=T_ITEM;
  if(u==EL_LENGTH(p)-3u && v==eg_land_lane(p,u)) {o->material=T_DOOR;o->flags=EL_PROTECTED;}
  if(u==EL_LENGTH(p)-4u && v==eg_land_lane(p,u)) {o->material=T_KEY;o->flags=EL_PROTECTED;}
}
