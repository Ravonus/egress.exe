"""Controller-only lift/brace/release in the cartridge-generated CRT course.
No gameplay map, position, stamina or actor writes; capture every native frame.
"""
from pathlib import Path
import sys,json,hashlib,argparse
sys.path.insert(0,'test/harness')
from play import Game,start_run
from gameplay_rebuild import walk
from heir import OamMeter

p=argparse.ArgumentParser();p.add_argument('rom',type=Path);p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
g=Game(a.rom);assert start_run(g,True,True,physical_fixture=2)
rock=next(i for i,v in enumerate(g.room()[:224]) if v&31==4);source=bytes(g.room()[:224]);frames=[];epochs=[];late=[];log=[];meter=OamMeter(g);phase='approach'
at=g.sym['_frame_wait']
def completed(_):
 if g.peek('game_mode')!=2:return
 ep=g.peek('frame_epoch');epochs.append(ep if not epochs else epochs[-1]+((ep-epochs[-1])&255))
 if ep!=g.peek('video_epoch'):late.append({'frame':g.frames,'frame_count':g.peek16('frame_count'),'height':g.peek('pl_lift',1),'load':g.peek('pl_lift_force'),'move':g.peek('pin',2),'player':g.player(),'grav_t':g.peek('grav_t'),'LY':g.pb.memory[0xff44],'phase':phase})
g.pb.hook_register(at>>16,at&65535,completed,None)
tick=g.tick
def record(n=1):
 for _ in range(n):tick();frames.append(g.pb.screen.image.convert('RGB'))
g.tick=record
assert walk(g,rock+16,log);g.hold(set());g.tick(8)
before=g.peek('stamina');phase='raise';g.hold({'up'})
for _ in range(30):
 g.tick()
 if g.peek('pl_lift',1)==1:break
g.hold(set());g.tick(8);lifted=g.peek('pl_lift',1)==1 and g.peek('room',rock-16)&31==4
paid=g.peek('stamina');g.shot(a.out/'raised.png');phase='hold';g.tick(64);held=g.peek('stamina');brace=g.peek('pl_lift_force')==1
phase='release';g.hold({'left'});g.tick(35);phase='recover';g.hold(set());g.tick(180)
windows=[epochs[i]-epochs[i-120] for i in range(120,len(epochs))]
results={'native_drop_course_has_supported_crt':source[rock+16]==1,'one_tile_raise_in_native_drop_course':lifted,'raise_spends_stamina':paid<before,'no_recovery_after_releasing_up':brace and held<paid,'moving_clear_allows_recovery':g.peek('stamina')>held and not g.peek('pl_lift_force'),'released_monitor_resumes_gravity':g.peek('room',rock-16)&31!=4,'exact_frame_deadlines':bool(windows) and min(windows)==max(windows)==120 and not late,'sprite_line_budget':not meter.over}
report={'sha256':hashlib.sha256(a.rom.read_bytes()).hexdigest(),'source':'Native PH_DROP course from the cartridge generator. Only debug course/no-enemy/god-mode flags before entry, then ordinary controller input. No map/position/resource writes, painting or interpolation.','results':results,'initial_crt_cell':rock,'native_entry_sha256':hashlib.sha256(source).hexdigest(),'stamina':{'before':before,'after_raise':paid,'after_hold':held,'after_clear':g.peek('stamina')},'frames':len(frames),'oam_max':meter.max_line,'late':late,'log':log}
(a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n');g.stop()
if frames:frames[0].save(a.out/'monitor-lift-native.gif',save_all=True,append_images=frames[1:],duration=[20 if k%3<2 else 10 for k in range(len(frames))],loop=0)
print(json.dumps(report,indent=2));raise SystemExit(0 if all(results.values()) else 1)
