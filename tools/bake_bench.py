"""Bake the actual item bodies into a small, cartridge-native Crucible fusion.

Only the two 32x32 bodies are streamed (512 bytes per pose). The floor,
inventory tiles and fonts are never part of the animation payload.
"""
from pathlib import Path
import math
import numpy as np
from PIL import Image
import bake_look as art

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'src/gen'
PAIRS = [(2,1,13),(4,2,8),(4,1,14),(4,3,15),(3,2,7),(3,1,6),
         (3,13,16),(3,7,9),(2,9,10),(1,8,11),(6,5,12)]

def body(piece, dy=0):
    rows = art.ITEM_ART[piece-1][2].strip().splitlines()
    small = np.array([[int(c) if c in '0123' else -1 for c in r] for r in rows])
    result = np.zeros((32,32), np.uint8)
    for y,x in zip(*np.nonzero(small>=0)):
        yy = y*2+dy
        if 0 <= yy < 31:
            result[yy:yy+2,x*2:x*2+2] = max(1,int(small[y,x]))
    return result

def packed(m):
    data = bytearray()
    # 8x16 OAM order: each sprite's top and bottom 8x8 tile stay adjacent.
    for ty in (0,16):
        for tx in range(0,32,8):
            for y in range(ty,ty+16):
                row=m[y,tx:tx+8]
                data.extend((sum((int(v)&1)<<(7-x) for x,v in enumerate(row)),
                             sum(((int(v)>>1)&1)<<(7-x) for x,v in enumerate(row))))
    return bytes(data)

def spans(m):
    ys,xs=np.nonzero(m)
    bounds=(int(ys.min()),int(ys.max()))
    span=[]
    for row in m:
        x=np.flatnonzero(row);span.append((int(x[0]),int(x[-1])) if len(x) else (16,16))
    return bounds,span

def fused(source,target,step,side):
    if not step:return source.copy()
    if step==16:
        result=target.copy()
        result[:,16:] = 0 if side==0 else result[:,16:]
        if side:result[:,:16]=0
        return result
    (st,sb),ss=spans(source);(tt,tb),ts=spans(target)
    t=step/16;top=round(st+(tt-st)*t);bottom=round(sb+(tb-sb)*t)
    out=np.zeros((32,32),np.uint8)
    for y in range(top,bottom+1):
        f=(y-top)/max(1,bottom-top)
        sy=round(st+f*(sb-st));ty=round(tt+f*(tb-tt))
        sl,sr=ss[sy];tl,tr=ts[ty]
        neck=round(math.sin(math.pi*t)*max(0,6-abs(y-16)*.45))
        bend=round(math.sin((y+step*2)*.3)*math.sin(math.pi*t)*2)
        dl=max(0,round(sl+(tl-sl)*t)+bend-(neck if side else 0))
        dr=min(31,round(sr+(tr-sr)*t)+bend+(neck if not side else 0))
        for x in range(dl,dr+1):
            sx=round(sl+(x-dl)/max(1,dr-dl)*(sr-sl))
            color=int(source[sy,sx]) or (2 if step>=6 else 1)
            # Ordered material-to-product transition keeps texture through the seam.
            if step>8 and ((x*3+y*5)&15)<(step-8)*2:
                tx=round(tl+(x-dl)/max(1,dr-dl)*(tr-tl))
                color=int(target[ty,tx]) or color
            seam=16+round(math.sin((y+step*2)*.3)*(16-step)/8)
            if step>8 and ((not side and x>=32-round((step-8)*2)+(seam-16)) or
                           (side and x<round((step-8)*2)+(seam-16))):continue
            out[y,x]=color
    return out

def array(name, rows):
    return 'static const uint8_t '+name+'['+str(len(rows))+']['+str(len(rows[0]))+'] = {\n'+''.join(
        '{'+','.join(map(str,row))+'},\n' for row in rows)+'};\n'

def main():
    for half in range(2):
        rows=[packed(body(p,dy)) for p in range(1+half*8,9+half*8) for dy in (0,-1,0,1)]
        code='#pragma bank 255\n#include <gb/gb.h>\n#include <gb/cgb.h>\n'+array('poses',rows)
        code+=f'void bench_piece_{half}(uint8_t piece,uint8_t pose,uint8_t tile) BANKED {{\n'
        code+=f' VBK_REG=1u;set_sprite_data(tile,16u,poses[((piece-{1+half*8}u)<<2)+(pose&3u)]);VBK_REG=0u;\n}}\n'
        (OUT/f'bench_items_{half}.c').write_text(code)
    for k,(a,b,r) in enumerate(PAIRS):
        rows=[packed(fused(body(a),body(r),step,0))+packed(fused(body(b),body(r),step,1)) for step in range(17)]
        code='#pragma bank 255\n#include <gb/gb.h>\n#include <gb/cgb.h>\n'+array('poses',rows)
        code+=f'void bench_fuse_{k}(uint8_t step,uint8_t tile) BANKED {{\n VBK_REG=1u;set_sprite_data(tile,32u,poses[step]);VBK_REG=0u;\n}}\n'
        (OUT/f'bench_fuse_{k}.c').write_text(code)
    code='#pragma bank 255\n#include <gb/gb.h>\n#include <gb/cgb.h>\n'
    for k in range(2):code+=f'void bench_piece_{k}(uint8_t,uint8_t,uint8_t) BANKED;\n'
    for k in range(11):code+=f'void bench_fuse_{k}(uint8_t,uint8_t) BANKED;\n'
    code+='void bench_art_piece(uint8_t p,uint8_t pose,uint8_t tile) BANKED {if(!p || p>16u)return;if(p<=8u)bench_piece_0(p,pose,tile);else bench_piece_1(p,pose,tile);}\n'
    code+='void bench_art_fuse(uint8_t result,uint8_t step,uint8_t tile) BANKED {switch(result) {\n'
    for k,(_,_,r) in enumerate(PAIRS):code+=f'case {r}u:bench_fuse_{k}(step,tile);break;\n'
    code+='}}\n'
    code+='static const uint16_t palettes[16][4]={\n'
    for _,pal,_ in art.ITEM_ART:
        colors=art.BIOMES['OFFICE'][pal]
        # The object keeps its native element colour; outline stays visible on ink.
        colors=[(0,0,0),(61,41,74),colors[2],colors[3]]
        code+='{'+','.join(str((r>>3)|((g>>3)<<5)|((b>>3)<<10)) for r,g,b in colors)+'},\n'
    code+='};\nvoid bench_art_palette(uint8_t p,uint8_t slot) BANKED {if(p && p<=16u)set_sprite_palette(slot,1u,palettes[p-1u]);}\n'
    (OUT/'bench_art.c').write_text(code)
    print('11 recipes, 17 deforming poses each, 16 animated item bodies; 112128 payload bytes')

if __name__=='__main__':main()
