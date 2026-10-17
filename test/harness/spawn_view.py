"""Observe completed native reinforcement creation, including reused actor slots."""
import re
from pathlib import Path

class SpawnView:
    def __init__(self,g):
        self.g=g;self.rows=[];self.errors=[]
        obj=Path(__file__).resolve().parents[2]/'build/obj/mob'
        symbols=obj.with_suffix('.sym').read_text()
        offset=int(re.search(r'^\s*\d+\s+_mob_spawn\s+([0-9A-F]+)',symbols,re.M)[1],16)
        base=g.sym['_mob_spawn']-offset
        section=obj.with_suffix('.lst').read_text().split('; Function mob_spawn\n',1)[1].split('; Function ',1)[0]
        returns=re.findall(r'^\s+([0-9A-F]{8})\s+.*\tret\s*$',section,re.M)
        assert returns,'native spawn return not found'
        for value in returns:
            at=base+int(value,16);g.pb.hook_register(at>>16,at&65535,self.created,None)
    def created(self,_):
        g=self.g;k=g.pb.register_file.A
        if k==255:return
        assert k<g.peek('nen') and g.peek('en',k*16)==3 and g.peek('en',k*16+1)!=6,k
        x=g.peek16_at('en',k*16+8)//16;y=g.peek16_at('en',k*16+10)//16
        cx,cy=g.peek('cam_x'),g.peek('cam_y');sx,sy=x-cx,y-cy
        row={'frame':g.frames,'slot':k,'position':[x,y],'camera':[cx,cy],'screen':[sx,sy],
             'family':g.peek('mob',k*19+7),'whole_entry_outside_view':sx<=-24 or sx>=184 or sy<=-24 or sy>=152}
        self.rows.append(row)
        if not row['whole_entry_outside_view']:self.errors.append(row)
    def report(self):
        return {'spawns':len(self.rows),'camera_positions':sorted({tuple(r['camera']) for r in self.rows}),
                'mismatches':self.errors,'all_entries_start_offscreen':bool(self.rows) and not self.errors,
                'entries':self.rows}
