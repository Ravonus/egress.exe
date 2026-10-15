from egkit import *
from egkit import face as _face
class lib:
    face=staticmethod(_face)
    @staticmethod
    def sprite(tmpl,pal):
        rows=[r for r in tmpl.strip('\n').split('\n')]
        im=Image.new('RGBA',(len(rows[0]),len(rows)),(0,0,0,0))
        for y,r in enumerate(rows):
            for x,c in enumerate(r):
                if c in '123': im.putpixel((x,y),(*pal[int(c)],255))
        return im
    PLAYER=PLAYER

T={}
T['S']=lib.PLAYER
T['SE']='''
......1111......
....11111111....
...1111111111...
..111111111111..
..111122222211..
..111222222221..
..112222212221..
..112222222221..
...1122222221...
....11222221....
....13333331....
...1333333321...
...1233333321...
...1213333312...
...1133333311...
....13311331....
....1331.1331...
.....11...11....'''
T['E']='''
......1111......
....11111111....
...1111111111...
..111111111111..
..111111222221..
..111112222221..
..111122221221..
..111122222222..
...11112222221..
....111222211...
.....133333.....
....13333331....
....12333331....
....13333321....
....13333331....
.....133331.....
.....13311......
......11........'''
T['NE']='''
......1111......
....11111111....
...1111111111...
..111111111111..
..111111111111..
..111111111221..
..111111111221..
..111111111221..
...1111111111...
....11111111....
....13333331....
...1333333331...
...1233333321...
...1213333312...
...1133333311...
....13311331....
....1331.1331...
.....11...11....'''
T['N']='''
......1111......
....11111111....
...1111111111...
..111111111111..
..111111111111..
..111111111111..
..111111111111..
..111111111111..
...1111111111...
....11111111....
....13333331....
...1333333331...
..123333333321..
..121333333121..
...1133333311...
....13311331....
....1331.1331...
.....11...11....'''
f,p=lib.face(2,13); PAL=[None,p[1],p[2],(64,120,232)]
def spr(k): return lib.sprite(T[k],PAL)
def mirror(i): return i.transpose(Image.FLIP_LEFT_RIGHT)
def walk(img,ph):
    # swap the leg rows by phase: legs are the last 3 rows
    out=img.copy(); w,h=img.size
    if ph%2==1:
        legs=img.crop((0,h-3,w,h)); out.paste((0,0,0,0),(0,h-3,w,h)); out.paste(legs,(1 if ph==1 else -1,h-3))
    if ph in (1,3): 
        body=out.crop((0,0,w,h-3)); o2=Image.new('RGBA',img.size,(0,0,0,0)); o2.paste(body,(0,-1)); o2.paste(out.crop((0,h-3,w,h)),(0,h-3)); out=o2
    return out
def blink(img):
    out=img.copy(); px=out.load()
    for x in range(16):
        if px[x,6][:3]==PAL[1]: px[x,6]=(*PAL[2],255)
    return out
def flash(img,col=(248,248,248)):
    out=img.copy(); px=out.load()
    for y in range(img.height):
        for x in range(img.width):
            if px[x,y][3]: px[x,y]=(*col,255)
    return out
def glow(img,col):
    a=img.split()[3]; out=Image.new('RGBA',(img.width+2,img.height+2),(0,0,0,0)); po=out.load(); pa=a.load()
    for y in range(out.height):
        for x in range(out.width):
            sx,sy=x-1,y-1
            ins=lambda X,Y:0<=X<img.width and 0<=Y<img.height and pa[X,Y]
            if not ins(sx,sy) and any(ins(sx+dx,sy+dy) for dx,dy in((1,0),(-1,0),(0,1),(0,-1))): po[x,y]=(*col,255)
    out.alpha_composite(img,(1,1)); return out
def roll(img,q):
    return img.rotate(-90*q,expand=False)
def squash(img):
    return img.resize((18,14),Image.NEAREST)
def dissolve(img,k):
    out=img.copy(); px=out.load()
    for y in range(img.height):
        for x in range(img.width):
            if (x*7+y*13)%5<k: px[x,y]=(0,0,0,0)
            elif (y+k)%4==0 and px[x,y][3]: px[x,y]=(64,200,248,255)
    return out


def sheet(out):
    W = 300
    im = Image.new("RGB", (W, 232), (20, 14, 46))
    d = ImageDraw.Draw(im)

    def row(y, label, frames):
        text(im, 0, y + 6, label, CYAN)
        for i, fr in enumerate(frames):
            put(im, fr, 80 + i * 22, y)
    dirs = [spr("S"), spr("SE"), spr("E"), spr("NE"), spr("N"), mirror(spr("NE")), mirror(spr("E")), mirror(spr("SE"))]
    row(2, "8 DIR", dirs)
    row(24, "WALK SE", [walk(spr("SE"), i) for i in range(4)] + [walk(spr("E"), i) for i in range(4)])
    s = spr("S")
    row(46, "IDLE", [s, walk(s, 0), blink(s), s])
    row(68, "COMBO", [s, squash(s), glow(s, WHITE), s, squash(spr("SE")), glow(spr("SE"), CYAN)])
    row(90, "CHARGE", [glow(s, PINK), glow(s, YEL), glow(s, CYAN), flash(s, YEL)])
    row(112, "ROLL", [roll(spr("E"), q) for q in range(4)])
    row(134, "BLINK", [dissolve(spr("E"), 1), dissolve(spr("E"), 3), dissolve(spr("E"), 4), dissolve(spr("E"), 2)])
    row(156, "HURT", [flash(s), s, flash(s, (255, 90, 110)), s])
    row(178, "DEATH", [dissolve(s, k) for k in range(5)])
    text(im, 0, 204, "5 DRAWN + 3 MIRRORED", WHITE)
    text(im, 0, 214, "STATE: FRAMES, TICKS", WHITE)
    im.resize((W * 3, 232 * 3), Image.NEAREST).save(out)


if __name__ == "__main__":
    import sys
    sheet(sys.argv[1])
