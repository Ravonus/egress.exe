#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_catalogue.h"
#include "eg_catalogue_data.inc"
static uint8_t valid(eg_item_id id) {return id && id<=EG_DISCOVERIES;}
static uint8_t family(eg_item_id id) {
  if(!valid(id))return 0u;
  if(id<=24u)return (uint8_t)id;
  if(id<EG_BOMBS_FIRST)return material_family[id-25u];
  if(id<EG_TOOLS_FIRST)return (uint8_t)(id-EG_BOMBS_FIRST+1u);
  if(id<EG_PETS_FIRST)return (uint8_t)(id-EG_TOOLS_FIRST+1u);
  return pet_families[id-EG_PETS_FIRST];
}
static uint8_t form(eg_item_id id) {return id>=25u && id<EG_BOMBS_FIRST ? material_form[id-25u] : 0u;}
static eg_item_id variant(uint8_t f,uint8_t n) {return !f || f>24u || n>15u ? 0u : n ? (eg_item_id)(25u+(uint16_t)(f-1u)*15u+n-1u) : f;}
uint8_t eg_catalogue_family(eg_item_id id) EG_BANKED {return family(id);}
uint8_t eg_catalogue_form(eg_item_id id) EG_BANKED {return form(id);}
/* Conservative reachability for the reset hint. Inferred effects are final
 * capabilities, never extra ingredients fed back into the finite recipe bag. */
uint8_t eg_catalogue_capabilities(const uint8_t *counts) EG_BANKED {
 uint8_t known[24],k,a,b,r,cap=0u;
 for(k=0u;k<24u;k++)known[k]=counts[k];
 for(k=0u;k<19u;k++){
  a=cap_recipes[k][0]-1u;b=cap_recipes[k][1]-1u;r=cap_recipes[k][2]-1u;
  if(known[a] && known[b] && (a!=b || known[a]>=2u) && !known[r])known[r]=1u;
 }
 if(known[0] || known[13] || known[12])cap|=CC_HOT;
 if(known[8] || known[9] || known[23])cap|=CC_COLD;
 if(known[11] || known[18])cap|=CC_POWER;
 if(known[7] || known[19])cap|=CC_MUD;
 if(known[20])cap|=CC_WEIGHT; /* ordinary brick inputs are counted separately */
 return cap;
}
eg_item_id eg_catalogue_variant(uint8_t f,uint8_t n) EG_BANKED {return variant(f,n);}
void eg_catalogue_profile(eg_item_id id,eg_item_profile *out) EG_BANKED {
  static const uint8_t costs[17]={0,12,8,10,10,12,14,10,12,14,16,16,18,12,20,8,10};
  uint8_t f=family(id),n=form(id);
  out->family=f;out->effect=f ? family_effects[f-1u] : 0u;
  out->kind=id>=EG_PETS_FIRST ? CK_PET : id>=EG_TOOLS_FIRST ? CK_TOOL : id>=EG_BOMBS_FIRST ? CK_BOMB : CK_MATERIAL;
  out->shape=form_profile[n][0];out->power=f ? form_profile[n][1] : 0u;out->radius=form_profile[n][2];
  out->cost=(uint8_t)(costs[out->effect]+out->power*2u+out->radius*2u);
  if(out->kind==CK_BOMB){out->shape=CS_BURST;out->power=3u;out->radius=2u;out->cost=24u;}
  else if(out->kind==CK_TOOL){out->shape=(f==17u || f==24u || f==5u) ? CS_WARD : (f&1u) ? CS_LINE : CS_FIELD;out->power=3u;out->radius=1u;out->cost=20u;}
  else if(out->kind==CK_PET){out->shape=CS_AURA;out->power=1u;out->radius=1u;out->cost=8u;}
}
static uint8_t base_recipe(uint8_t a,uint8_t b) {
  uint8_t k;
  for(k=0u;k<19u;k++)if((base_recipes[k][0]==a && base_recipes[k][1]==b) || (base_recipes[k][1]==a && base_recipes[k][0]==b))return base_recipes[k][2];
  return 0u;
}
eg_item_id eg_catalogue_recipe(eg_item_id a,eg_item_id b) EG_BANKED {
  uint8_t k,f,n,r;eg_item_id swap;
  if(!valid(a) || !valid(b))return 0u;
  if(a>b){swap=a;a=b;b=swap;}
  if(b<=24u) {
    r=base_recipe((uint8_t)a,(uint8_t)b);if(r)return r;
    return a==b ? variant((uint8_t)a,1u) : 0u;
  }
  if(a>=25u && b<EG_BOMBS_FIRST) {
    n=form(a);k=form(b);if(k<n)n=k;
    r=base_recipe(family(a),family(b));return r ? variant(r,n) : 0u;
  }
  if(a>24u || b>=EG_BOMBS_FIRST)return 0u;
  f=family(b);n=form(b);
  if(n<15u && a==catalysts[f-1u][n+1u])return b+1u;
  if(n==4u && a==22u)return (eg_item_id)(EG_BOMBS_FIRST+f-1u);
  if(n==6u && a==24u)return (eg_item_id)(EG_TOOLS_FIRST+f-1u);
  if(n==6u && a==18u)for(k=0u;k<12u;k++)if(pet_families[k]==f)return (eg_item_id)(EG_PETS_FIRST+k);
  r=base_recipe((uint8_t)a,f);return r ? variant(r,n) : 0u;
}
uint8_t eg_catalogue_ingredients(eg_item_id id,eg_item_id *a,eg_item_id *b) EG_BANKED {
  uint8_t k,f=family(id),n=form(id);
  *a=*b=0u;if(!f)return 0u;
  if(id<=24u) {
    for(k=0u;k<19u;k++)if(base_recipes[k][2]==id){*a=base_recipes[k][0];*b=base_recipes[k][1];return 1u;}
    return 0u;
  }
  if(id<EG_BOMBS_FIRST){*a=n==1u ? f : id-1u;*b=n==1u ? f : catalysts[f-1u][n];}
  else if(id<EG_TOOLS_FIRST){*a=variant(f,4u);*b=22u;}
  else if(id<EG_PETS_FIRST){*a=variant(f,6u);*b=24u;}
  else {*a=variant(f,6u);*b=18u;}
  return 1u;
}
static char *copy(char *out,const char *s) {while(*s)*out++=*s++;*out=0;return out;}
void eg_catalogue_name(eg_item_id id,char *out) EG_BANKED {
  uint8_t f=family(id),n=form(id);
  if(!f){copy(out,"UNKNOWN");return;}
  if(id>=EG_PETS_FIRST){copy(out,pet_names[id-EG_PETS_FIRST]);return;}
  if(id>=EG_TOOLS_FIRST){copy(out,tool_names[f-1u]);return;}
  out=copy(out,family_names[f-1u]);
  if(id>=EG_BOMBS_FIRST){copy(out," BOMB");return;}
  if(n){*out++=' ';copy(out,form_names[n]);}
}
uint8_t eg_discovery_has(const uint8_t *bits,eg_item_id id) EG_BANKED {
  if(!valid(id))return 0u;id--;return (uint8_t)((bits[id>>3]>>(id&7u))&1u);
}
uint8_t eg_discovery_mark(uint8_t *bits,eg_item_id id) EG_BANKED {
  uint8_t mask;if(!valid(id))return 0u;id--;mask=(uint8_t)(1u<<(id&7u));
  if(bits[id>>3]&mask)return 0u;bits[id>>3]|=mask;return 1u;
}
uint16_t eg_discovery_count(const uint8_t *bits) EG_BANKED {
  uint8_t k,v;uint16_t n=0u;
  for(k=0u;k<56u;k++){v=k==55u ? bits[k]&15u : bits[k];while(v){v&=(uint8_t)(v-1u);n++;}}
  return n;
}
void eg_catalogue_abbr(eg_item_id id,char *out) EG_BANKED {
  uint8_t f=family(id),k,n=form(id);const char *s;
  if(!f){copy(out,"----");return;}s=family_names[f-1u];
  for(k=0u;k<4u;k++)out[k]=s[k] ? s[k] : ' ';
  if(id>=EG_PETS_FIRST)out[3]='P';else if(id>=EG_TOOLS_FIRST)out[3]='T';else if(id>=EG_BOMBS_FIRST)out[3]='B';else if(n)out[3]=(char)(n<10u ? '0'+n : 'A'+n-10u);
  out[4]=0;
}
void eg_catalogue_hint(eg_item_id id,char *out) EG_BANKED {
  static const char hints[17][19]={"FIND MATERIALS","BURNS PAPER / ICE","DOUSES FIRE","PUSHES LIGHT BOXES","SLOWS A MOB","MIX / SHAPED WARD","MIX WITH METAL","WETS YOUR TARGET","FILLS SHALLOW GAPS","FREEZES WATER/MOBS","FREEZES WATER/MOBS","WEIGHS DOWN PLATES","POWERS WET WIRES","HOT MIST BLINDS","BURNS PAPER / ICE","DUST BLINDS A MOB","WETS YOUR TARGET"};
  uint8_t f=family(id);
  if(id>=EG_PETS_FIRST){copy(out,"SUMMON / COMMAND");return;}
  if(id>=EG_TOOLS_FIRST){copy(out,(f==17u || f==24u || f==5u) ? "BRIEF ELEMENT WARD" : f==18u ? "RESTORES HEALTH" : "FOCUSED ELEMENT");return;}
  if(id>=EG_BOMBS_FIRST){copy(out,"FUSE / AREA EFFECT");return;}
  if(f==18u){copy(out,"RESTORES HEALTH");return;}
  if(f==17u || form_profile[form(id)][0]==CS_WARD){copy(out,"BRIEF ELEMENT WARD");return;}
  copy(out,hints[f ? family_effects[f-1u] : 0u]);
}
