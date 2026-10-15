import sys
from pathlib import Path
sys.path.insert(0, '/Users/ravonus/dev/crucible.exe/test/harness')
import capture_flow as cf, catalogue
rom=Path('/Users/ravonus/dev/crucible.exe/build/crucible.gbc'); out=Path(sys.argv[1])
ctx=cf.Context(rom=rom,out=out,addr=cf.Addresses.resolve(rom,rom.parent/'obj'),catalogue=catalogue.load(catalogue.DEFAULT_DIR),seeds=1,discoveries=int(sys.argv[2]),browse=0)
g=cf.Game(ctx,'grab')
g.boot()
g.free_play()
owned=cf.free_first_mix(g)
cf.free_discoveries(g, owned)
g.settle(); g.step(200)
n=0
for k in range(int(sys.argv[3])):
    if g.screen!=cf.BENCH: g.settle(); g.step(30)
    g.pulse('right' if k%5 else 'down',2); g.step(90)
    g.pb.screen.image.convert('RGB').save(out/f'b{n:03d}.png'); n+=1
# book, filter
g.pulse('start',30); g.step(30); g.pb.screen.image.convert('RGB').save(out/'menu.png')
g.stop()
print('done',n)
