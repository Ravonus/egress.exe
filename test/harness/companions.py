"""Native companion acceptance. Saved individuals and projectile/target records
are explicit fixtures; commands, damage, death, healing and reset run through
cartridge code and controller input. No direct calls to game functions."""
import argparse,hashlib,json,sys
from pathlib import Path
from PIL import Image
from play import Game,start_run,pocket,bench_cursor
from heir import OamMeter
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from rtc_test import crc16
HP=[8,6,6,14,7,7,16,9,10,6,8,7]
CONT=(0x40,0x140);PETS=(0x420,0x490)
def seal(b):b[-2:]=crc16(b[:-2]).to_bytes(2,'little');return b

def fixture(saved,pet,bag=(),spent=False,hp=None,auto_cd=240):
    data=bytearray(saved)
    for at,pa in zip(CONT,PETS):
        c=bytearray(data[at:at+256])
        if c[0]!=3 or not c[2]:continue
        c[6]=1;c[35:41]=bytes(6);c[35]=pet&255;c[40]=pet>>8;c[70]=len(bag);c[71:95]=bytes(24)
        entry=bytearray(90);entry[0:2]=pet.to_bytes(2,'little');entry[2]=240;entry[3]=4;entry[6]=32|(128 if spent else 0);entry[7]=255;entry[8]=HP[pet-433] if hp is None else hp;entry[14]=auto_cd;entry[16]=1;entry[17]=len(bag)+1
        for k,(id,health,revived,cd) in enumerate(bag):
            c[71+k]=id&255;c[37+(k>>3)]|=(id>>8)<<(k&7)
            entry[18+k*3:21+k*3]=bytes((health|(32 if revived else 0),cd,k+2))
        p=bytearray(112);p[0]=1;p[1]=c[1];p[2:4]=c[4:6];p[4]=c[23];p[8:98]=entry
        data[pa:pa+112]=seal(p);data[at:at+256]=seal(c)
    return bytes(data)

def load(rom,saved):
    g=Game(rom,sram=saved);assert start_run(g,True,True,physical_fixture=1)
    g.poke('stage_type',0);g.poke('tutorial_on',0)
    g.pb.memory[1,0xde78:0xde98]=[0xa7]*32
    return g

def petpos(g):return max(0,min(255,g.peek('pl_x')+(g.peek('companion',2)^128)-128)),max(0,min(223,g.peek('pl_y')+(g.peek('companion',3)^128)-128))
def wait(g,pred,limit=240):
    for _ in range(limit):
        if pred():return True
        g.tick()
    return pred()
def fire_at(g,x,y):
    for k,v in enumerate((1,0,120,0,(x<<4)&255,x>>4,(y<<4)&255,y>>4,0,0)):g.poke('orbs',v,k)
def fire_at_pet(g):fire_at(g,*petpos(g))
def target(g):
    # Place both bodies in a clear central encounter, facing its target. The
    # preceding walking fixture can finish at the bottom of the island.
    g.poke16('psx',120<<4);g.poke16('psy',104<<4);g.poke('pl_x',120);g.poke('pl_y',104);g.poke('pmv',4,5);g.tick(4)
    g.poke('companion',240,2);g.poke('companion',4,3)
    x,y=120,128
    for dy in range(-2,3):
        for dx in range(-2,3):
            c=((y//16+dy)*16+x//16+dx)
            if 0<=c<224:g.poke('room',0,c)
    g.poke('nen',1);g.poke('enemies_alive',1)
    for k,v in enumerate((3,3,240,80,0,0,240,0,(x<<4)&255,x>>4,(y<<4)&255,y>>4,0,0,0,0)):g.poke('en',v,k)
    # Plain, stationary target. Conditional armour/reflect are covered separately.
    for k in range(19):g.poke('mob',0,k)
    g.poke('mob',255,5);g.poke('mob',255,6);g.poke('mob',8,3);g.poke('mob',80,14)
    g.poke('pmv',4,5)

def run(rom,out):
    out.mkdir(parents=True,exist_ok=True);r={};details={};frames=[]
    g=Game(rom);assert start_run(g,True,True,physical_fixture=1);base=g.stop()
    for id in range(433,445):
        g=load(rom,fixture(base,id));meter=OamMeter(g)
        r[f'pet_{id}_restores_native_individual']=g.peek16('companion')==id and g.peek('companion',8)==HP[id-433] and g.peek('pet_instance')==1
        patterns=set();dirs=set();states=set()
        for buttons,n in [({'right'},24),({'down','right'},24),({'down'},24),({'left'},24),(set(),180)]:
            g.hold(buttons)
            for k in range(n):
                g.tick();state=g.peek('companion',6);dirs.add((state>>3)&7);states.add(state&7)
                if g.peek('companion',7)!=255:
                    at=0x8000+(124+(4 if state&64 else 0))*16;patterns.add(bytes(g.pb.memory[0,at:at+64]))
                if k%12==0:frames.append(g.pb.screen.image.convert('RGB').resize((320,288),Image.Resampling.NEAREST))
        g.hold(set());r[f'pet_{id}_walks_and_changes_view']=len(patterns)>=5 and len(dirs)>=3 and 1 in states
        g.shot(out/f'pet-{id}-native.png');target(g);g.poke('companion',4,8);g.poke('companion',0,4);g.poke('companion',240,14);g.poke('stamina',100)
        before=g.peek('stamina');g.press('select',4);g.tick(16)
        r[f'pet_{id}_command_has_cost_and_cooldown']=g.peek('companion',4)>0 and g.peek('stamina')<before
        if id in (436,439):
            r[f'tank_{id}_creates_real_enemy_threat']=bool(g.peek('companion',12)&1) and g.peek('companion',13)>0
            # Put the enemy between the heir and tank. Its walk must head
            # toward the tank on the left, away from the heir on the right.
            px,py=petpos(g);ex=(g.peek('pl_x')+px)//2;ey=py
            g.poke('companion',24,5) # hold the visible command pose while measuring its stationary threat
            g.poke('companion',255,15) # isolate steering from Moss Ball's contact-triggered counter-knockback
            for off,v in [(1,0),(2,0),(5,3),(6,255),(8,(ex<<4)&255),(9,ex>>4),(10,(ey<<4)&255),(11,ey>>4),(14,ex),(15,ey)]:g.poke('en',v,off)
            g.poke('mob',1,4);g.poke('mob',16,16);g.tick(12)
            r[f'tank_{id}_redirects_enemy_movement']=g.peek16_at('en',8)<(ex<<4) and g.peek('en',5)==2
        details[str(id)]={'patterns':len(patterns),'facings':sorted(dirs),'states':sorted(states),'cooldown':g.peek('companion',4),'maxOam':meter.max_line}
        r[f'pet_{id}_oam_and_stack']=meter.over==0 and bytes(g.pb.memory[1,0xde78:0xde98])==bytes([0xa7]*32)
        g.stop()
    g=load(rom,fixture(base,433));target(g);g.poke('en',1,3);g.press('select',4);g.tick(12)
    r['companion_can_kill_a_real_matrix_target']=g.peek('en',1)==6 and g.peek('kill_by',14)>0;g.stop()
    for defence,name in [(1,'frontal_armour'),(3,'crush_only'),(6,'own_reflected_orb')]:
        g=load(rom,fixture(base,433));target(g);g.poke('en',1,3);g.poke('en',0,5);g.poke('mob',defence,2)
        g.press('select',4);g.tick(12);r['pet_cannot_bypass_'+name]=g.peek('en',3)==1 and g.peek('en',1)!=6;g.stop()
    g=load(rom,fixture(base,436));g.poke('dbg_god',0);g.poke('pl_inv',0);g.poke('companion',3,9);g.poke('companion',2,10);g.poke('companion',60,11)
    hp=g.peek('pl_hp');fire_at(g,*g.player());g.tick(10)
    r['tank_ward_protects_heir_from_real_projectile']=g.peek('pl_hp')==hp and g.peek('companion',9)==2;g.stop()
    # Ordinary death is durable even when the map rolls back to floor entry.
    g=load(rom,fixture(base,433,hp=1));fire_at_pet(g)
    r['ordinary_lethal_hit_plays_death']=wait(g,lambda:g.peek('companion',8)==0) and (g.peek('companion',6)&7)==7
    r['ordinary_death_removes_individual']=wait(g,lambda:g.peek16('companion')==0)
    g.hold({'select'});g.tick(62);g.hold(set());g.tick(160)
    r['ordinary_death_survives_room_reset']=g.peek16('companion')==0
    g.tick(8);dead=g.stop();g=load(rom,dead);r['ordinary_death_survives_poweroff']=g.peek16('companion')==0;g.stop()
    # Only Vine Frog has the explicit once-per-individual exception.
    g=load(rom,fixture(base,443,hp=1));fire_at_pet(g)
    r['second_spring_spends_charge_at_lethal_hit']=wait(g,lambda:g.peek('companion',8)==0) and bool(g.peek('companion',6)&128) and bool(g.peek('companion',10)&128)
    g.tick(3);pending=g.stop();g=load(rom,pending)
    r['pending_revival_survives_poweroff']=g.peek16('companion')==443 and bool(g.peek('companion',6)&128)
    r['second_spring_returns_once']=wait(g,lambda:g.peek('companion',8)>0,120) and g.peek('companion',8)==4
    g.tick(8);revived=g.stop();g=load(rom,revived)
    r['spent_charge_survives_poweroff']=g.peek16('companion')==443 and bool(g.peek('companion',6)&128)
    g.poke('companion',1,8);g.poke('companion',0,15);fire_at_pet(g)
    r['second_lethal_hit_is_permanent']=wait(g,lambda:g.peek16('companion')==0,180);g.tick(8);save=g.stop();g=load(rom,save);r['no_second_revival_after_reload']=g.peek16('companion')==0;g.stop()
    # Start -> BAG -> individual details -> equip. Two frogs of the same
    # species have different health, identities and spent charges.
    g=load(rom,fixture(base,443,bag=[(433,5,False,100),(443,2,True,75)],spent=True,hp=3))
    def equip(slot):
        g.press('start',4);g.tick(12);g.press('a',4);g.tick(16);assert g.peek('bench_on')==1
        bench_cursor(g,slot);g.press('a',4);g.tick(12);assert g.peek('bench_page')==3
        g.shot(out/f'pet-detail-{g.peek16("companion")}.png')
        g.press('a',4);g.tick(12);assert not g.peek('bench_on'),{'page':g.peek('bench_page'),'pet':g.peek16('companion'),'hp':g.peek('companion',8),'bag':pocket(g),'metadata':list(g.peek('pet_meta',k) for k in range(9))}
    equip(0)
    r['swap_keeps_old_individual_in_bag']=g.peek16('companion')==433 and g.peek('companion',8)==5 and pocket(g)==[443,443] and g.peek('pet_meta',3)&32
    r['swap_preserves_cooldown']=60<g.peek('companion',4)<=100
    equip(0)
    r['duplicate_species_have_individual_health_and_charge']=g.peek16('companion')==443 and g.peek('companion',8)==2 and bool(g.peek('companion',6)&128) and g.peek('pet_instance')==3
    r['bag_ui_preserves_native_stack']=bytes(g.pb.memory[1,0xde78:0xde98])==bytes([0xa7]*32)
    g.hold({'select'});g.tick(62);g.hold(set());g.tick(160)
    r['reset_restores_matching_individual_without_heal_or_charge_refresh']=g.peek16('companion')==443 and g.peek('pet_instance')==1 and g.peek('companion',8)==3 and bool(g.peek('companion',6)&128)
    g.stop()
    # Pickups use the real player collection path; full health leaves medicine.
    g=load(rom,fixture(base,436,hp=2));g.poke('companion',255,14)
    def collect():g.hold({'right'});g.tick(2);g.hold(set());g.tick(8)
    c=(g.peek('pl_y')//16)*16+g.peek('pl_x')//16;g.poke('room',37,c);g.poke('pl_hp',g.peek('pl_hp_max'));g.poke('pl_quarter',1);collect()
    r['quarter_missing_heals_heir_first']=g.peek('pl_quarter')==0 and g.peek('companion',8)==2 and g.room()[c]==0
    g.poke('room',37,c);collect();r['full_heir_routes_pickup_to_living_pet']=g.peek('companion',8)==4 and g.room()[c]==0
    g.poke('companion',14,8);g.poke('room',37,c);g.tick(10);r['both_full_leave_pickup']=g.room()[c]==37
    g.poke('companion',0,8);g.poke('companion',24,5);g.poke('companion',7,6);g.tick(3);r['pickup_never_revives_dead_pet']=g.room()[c]==37 and g.peek('companion',8)==0;g.stop()
    if frames:frames[0].save(out/'native-pet-follow.gif',save_all=True,append_images=frames[1:],duration=120,loop=0)
    report={'sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'fixtures':__doc__,'results':r,'details':details};(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return all(r.values())
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();raise SystemExit(0 if run(a.rom,a.out) else 1)
