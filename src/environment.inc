/* Included in elements.c: one bounded environmental transition per tick,
 * with anticipation, material change, settling particles and a spent state. */
static uint8_t environment_state(uint8_t raw,uint8_t skin){
 if(raw==T_EMPTY){
  if(skin==M_WORLD_FERN0+1u)return EN_SEED;
  if(skin==M_WORLD_FLOWER0+1u)return EN_SPROUT;
  if(skin==M_WORLD_FLOWER1+1u)return EN_ASH;
  return skin ? EN_NO_CHANGE : EN_SOIL;
 }
 if(raw==T_PAPER && skin==M_WORLD_FERN1+1u)return EN_TREE;
 if(raw==T_DIRT && skin>=M_WORLD_FERN0+1u && skin<=M_WORLD_REED1+1u)return EN_FOLIAGE;
 if(raw==T_WATER)return EN_WATER;
 if(raw==T_ICE || raw==T_ICE_1)return EN_ICE;
 return EN_NO_CHANGE;
}
static uint8_t environment_occupied(uint8_t gx,uint8_t gy){
 uint8_t k;int16_t x=(int16_t)gx-(world_on ? world_view.x : 0u),y=(int16_t)gy-(world_on ? world_view.y : 0u);
 if((uint16_t)x>=EG_W || (uint16_t)y>=EG_H)return 0u;
 x=x*16+8;y=y*16+8;
 if(pl_hp && overlap((uint8_t)x,(uint8_t)y,pl_x,pl_y,14u,14u))return 1u;
 if(companion.hp && overlap_world(x,y,(int16_t)pl_x+companion.ox,(int16_t)pl_y+companion.oy,12u,12u))return 1u;
 for(k=0u;k<nen;k++)if(en[k].st!=ES_DEAD && overlap((uint8_t)x,(uint8_t)y,(uint8_t)(en[k].x>>4),(uint8_t)(en[k].y>>4),14u,14u))return 1u;
 return 0u;
}
static uint8_t environment_fx(uint8_t action){return action==EA_FIRE ? FX_HOT : action==EA_COLD ? FX_COLD : action==EA_WATER ? FX_WET : FX_ALIVE;}
static void environment_particles(uint8_t gx,uint8_t gy,uint8_t action,uint8_t smoke){
 int16_t x=(int16_t)gx-(world_on ? world_view.x : 0u),y=(int16_t)gy-(world_on ? world_view.y : 0u);
 if((uint16_t)x>=EG_W || (uint16_t)y>=EG_H)return;
 x=x*16+8;y=y*16+8;
 if(smoke)puff((uint8_t)x,(uint8_t)(y-6u),5u,ST_PUFF);
 else fx_burst((uint8_t)x,(uint8_t)(y-4u),environment_fx(action),3u);
}
int8_t elements_environment(uint8_t family,uint8_t effect,uint8_t cell,uint8_t radius) BANKED {
 uint8_t action,state,next,k,gx,gy;orb_t *o;
 if(cell>=EG_N)return -1;
 if(family==20u)action=EA_PLANT;
 else if(EG_PIECE_HOT(effect))action=EA_FIRE;
 else if(effect==P_WATER || effect==P_RAIN || effect==P_CLOUD)action=EA_WATER;
 else if(effect==P_ICE || effect==P_SNOW)action=EA_COLD;
 else return -1;
 state=environment_state(room.c[cell],look_skin[cell]);
 if(action==EA_PLANT && room.c[cell]==T_DIRT && !look_skin[cell])state=EN_SOIL;
 /* Authored puzzle barriers retain their finite connected reactions. Natural
  * lakes use the bounded patch field instead of freezing an entire biome. */
 if((state==EN_WATER || state==EN_ICE) && (!world_on || EG_PROT(&room,cell)))return -1;
 next=eg_environment_next(state,action);if(next==EN_NO_CHANGE)return -1;
 if(EG_PROT(&room,cell) || (room.portal_need&EG_PORTAL_ON && (cell==room.portal[0] || cell==room.portal[1])))return 0;
 gx=(uint8_t)(EG_X(cell)+(world_on ? world_view.x : 0u));gy=(uint8_t)(EG_Y(cell)+(world_on ? world_view.y : 0u));
 if(next==EN_TREE && environment_occupied(gx,gy))return 0;
 for(k=0u;k<MAXO;k++)if(orbs[k].live==4u && orbs[k].x==gx && orbs[k].y==gy)return 0;
 for(k=0u;k<MAXO;k++)if(!orbs[k].live)break;if(k==MAXO)return 0;
 if(action==EA_PLANT || action==EA_WATER)radius=0u;else if(radius>2u)radius=2u;
 if(action==EA_COLD && !radius)radius=1u;
 o=&orbs[k];o->live=4u;o->friendly=(uint8_t)(128u|action);o->life=64u;o->pad=radius;
 o->x=gx;o->y=gy;o->vx=o->vy=-(int8_t)radius;elemental_active|=look_bits[k];
 environment_particles(gx,gy,action,0u);sfx(action==EA_FIRE ? SFX_BREAK : action==EA_COLD ? SFX_SHATTER : SFX_PUSH);
 hud_msg(action==EA_PLANT ? "PLANTING A SEED" : action==EA_WATER ? "WATER WAKES ROOTS" : action==EA_FIRE ? "LEAVES IGNITE" : "FROST SPREADS");
 return 1;
}
static void environment_step(orb_t *o){
 uint8_t gx,gy,raw,skin,state,next,cell,r=o->pad&3u,action=o->friendly&31u;int16_t x,y;
 if(video_epoch!=frame_epoch || (LY_REG>=56u && LY_REG<144u))return;
 if(o->pad&4u){
  if(action==EA_FIRE && !(o->life&7u))environment_particles((uint8_t)o->x,(uint8_t)o->y,action,1u);
  if(!--o->life)o->live=0u;return;
 }
 if(!--o->life){o->live=0u;return;}
 if(o->life>56u || (o->life&1u))return;
 x=(int16_t)o->x+o->vx;y=(int16_t)o->y+o->vy;
 if((uint16_t)x<(world_on ? world_view.width : EG_W) && (uint16_t)y<(world_on ? world_view.height : EG_H) &&
    (uint8_t)((o->vx<0 ? -o->vx : o->vx)+(o->vy<0 ? -o->vy : o->vy))<=r){
  gx=(uint8_t)x;gy=(uint8_t)y;raw=world_read(0,gx,gy);skin=world_read_skin(gx,gy);state=environment_state(raw,skin);
  if(action==EA_PLANT && raw==T_DIRT && !skin)state=EN_SOIL;
  next=eg_environment_next(state,action);
  /* A two-cell frost ray cannot jump across an intervening wall or shore. */
  if(next==EN_ICE && r==2u && (o->vx==2 || o->vx==-2 || o->vy==2 || o->vy==-2)){
   raw=world_read(0,(uint8_t)((int16_t)o->x+o->vx/2),(uint8_t)((int16_t)o->y+o->vy/2));
   if(raw!=T_WATER && raw!=T_ICE && raw!=T_ICE_1)next=EN_NO_CHANGE;
  }
  if(next==EN_TREE && environment_occupied(gx,gy)){if(o->life<16u)o->life=16u;return;}
  if(next!=EN_NO_CHANGE){
   raw=next==EN_TREE ? T_PAPER : next==EN_ICE ? T_ICE : next==EN_WATER ? T_WATER : T_EMPTY;
   skin=next==EN_SEED ? M_WORLD_FERN0+1u : next==EN_SPROUT ? M_WORLD_FLOWER0+1u : next==EN_TREE ? M_WORLD_FERN1+1u : next==EN_ASH ? M_WORLD_FLOWER1+1u : 0u;
   cell=world_write_skin(gx,gy,raw,skin);if(cell<EG_N)queue_cell(cell);
   environment_particles(gx,gy,action,0u);if(action==EA_FIRE)environment_particles(gx,gy,action,1u);
   puzzle_dirty=1u;effect_n=128u;last_rx=action==EA_FIRE ? (next==EN_WATER ? RX_THAW : RX_BURN) : action==EA_COLD ? RX_FREEZE : RX_BLOOM;
  }
 }
 if(++o->vx>(int8_t)r){o->vx=-(int8_t)r;o->vy++;}
 if(o->vy>(int8_t)r){o->pad|=4u;o->life=16u;}
}
