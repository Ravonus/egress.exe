"""The look's review captures (B1): a floor in the new look, the room assembling and falling apart.

    python test/harness/look_shots.py build/egress.gbc docs/shots

Writes, from power-on on a blank save, headless and deterministic:
  b1_look_floor.png        a floor once it has assembled (1x and 3x side by side), with the foam thinned to a few
                           deliberate patches as the puzzle workstream's rooms will have it (the harness rewrites the
                           cells on screen while they are still in the air; nothing is played on that floor)
  b1_look_vs_mock.png      that floor at 4x beside docs/shots/mock_room.png
  b1_look_floor2.png       another seed as the current generator makes it (foam-heavy)
  b1_look_assemble.gif     the floor assembling around the player, a beat of play, then the exit collapse (2x)
  b1_look_death.gif        the world letting go around a body (2x): the death collapse keeps the 8 neighbours
  b1_fragile_room.png      the fragile floors in a room (1x and 3x): worn carpet's three stages, a ceiling tile's two,
                           glass, glitched glass and a pit, set through the look's skins beside ordinary floor
  b1_items_room.png        a room's pieces as their item cards (1x and 3x): the floor play.py starts on, then a piece of each
                           kind laid on the slabs around the player, and a cell of thin ice and cracked ice
  b1_floor_vs_mock.png     docs/shots/mock_room.png (left) beside that floor as the game draws it (right), the same scale
  b1_fragile_break.gif     the glitched glass's beat, worn carpet stepped through its stages, then each kind breaking
                           (3x)
and checks: the room assembles (look_busy_n reaches 0, the game reaches MODE_PLAY), every cell on screen is back in
the world, and the collapse empties the world but for the kept cells. Exit status 1 if a check fails.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import play as H  # noqa: E402

PS_EXIT = 5


def present(g: H.Game) -> list[int]:
    a = g.addr("look_present")
    bits = [g.pb.memory[a + k] for k in range(28)]
    return [(bits[i >> 3] >> (i & 7)) & 1 for i in range(224)]


def frame(g: H.Game) -> Image.Image:
    return g.pb.screen.image.convert("RGB")


def start(g: H.Game, seed: int, enemies: bool) -> bool:
    if not g.wait_mode(H.MODE_TITLE, 1500):
        return False
    g.tick(30)
    g.poke("dbg_god", 1)
    g.poke("dbg_no_enemies", 0 if enemies else 1)
    g.poke16("dbg_seed", seed)
    for _ in range(40):
        g.press("start")
        if g.peek("look_busy_n") or g.wait_mode(H.MODE_PLAY, 20):
            return True
    return False


def gif(frames: list[Image.Image], path: Path, scale: int = 2, ms: int = 33) -> None:
    big = [f.resize((160 * scale, 144 * scale), Image.NEAREST) for f in frames]
    big[0].save(path, save_all=True, append_images=big[1:], duration=ms, loop=0, optimize=False)


def look_ids() -> dict[str, int]:
    h = Path(__file__).resolve().parents[2] / "src" / "gen" / "look_art.h"
    ids = {}
    for ln in h.read_text().splitlines():
        p = ln.split()
        if len(p) >= 3 and p[0] == "#define" and p[1].startswith("M_"):
            ids[p[1][2:]] = int(p[2].rstrip("u"))
    return ids


def fragile(rom: Path, out: Path) -> dict:
    M = look_ids()
    rep: dict = {}
    g = H.Game(rom)
    start(g, 0x5EED, enemies=False)
    a = g.addr("room")
    for c in range(224):  # floor first, as in run 1
        x, y = c % 16, c // 16
        if g.pb.memory[a + c] & 0x1F == 1 and ((x // 3) + (y // 2)) % 4:
            g.pb.memory[a + c] = 0
    while g.peek("game_mode") != H.MODE_PLAY and g.frames < 4000:
        g.tick()
    g.tick(20)
    room, skin = g.addr("room"), g.addr("look_skin")
    px, py = g.peek("pl_x") >> 4, g.peek("pl_y") >> 4
    cx, cy = g.peek("cam_x"), g.peek("cam_y")
    # a row of the room on screen, two rows from the player at least: the stages in order, left to right, laid
    # over whatever was there (the cells become carpet underneath)
    show = ["CARPET0", "CARPET1", "CARPET2", "CEIL0", "CEIL1", "GLASS", "GLITCH0", "PIT"]
    x0 = max(1, (cx + 15) // 16)
    best = None
    for y in range(1, 13):
        sy = y * 16 - cy
        if not (0 <= sy <= 96) or abs(y - py) < 2:
            continue
        xs = [x for x in range(x0, x0 + len(show) + 1)]
        if xs[-1] > 14:
            continue
        walls = sum(1 for x in xs if g.pb.memory[room + y * 16 + x] & 0x1F in (2, 7))
        if best is None or walls < best[0]:
            best = (walls, y, xs)
    if best is None:
        return {"fragile_ok": False}
    _, ry, xs = best
    cells = [ry * 16 + x for x in xs]
    for c in cells:
        g.pb.memory[room + c] = 0
    at = dict(zip(show, cells))
    walk = cells[len(show)]  # a fresh worn carpet tile, stepped through its stages

    def skin_set(c: int, m: int) -> None:
        g.pb.memory[skin + c] = m + 1
        g.poke("look_dbg_cell", c)
        g.poke("look_dbg_kind", 0x80)
        g.tick()

    for nm, c in at.items():
        skin_set(c, M[nm])
    skin_set(walk, M["CARPET0"])
    g.tick(8)
    shot = frame(g)
    pair = Image.new("RGB", (160 + 8 + 480, 432), (24, 22, 32))
    pair.paste(shot, (0, 0))
    pair.paste(shot.resize((480, 432), Image.NEAREST), (168, 0))
    pair.save(out / "b1_fragile_room.png")
    frames: list[Image.Image] = []

    def run(n: int) -> None:
        for _ in range(n):
            g.tick()
            frames.append(frame(g))

    phases = set()
    for _ in range(140):  # the glitch beat
        g.tick()
        frames.append(frame(g))
        phases.add(g.peek("look_glitch_phase"))
    rep["glitch_phases_seen"] = sorted(phases)
    for st in (1, 2):  # worn carpet, stepped on
        skin_set(walk, M["CARPET0"] + st)
        frames.append(frame(g))
        run(24)
    breaks = [(walk, 1), (at["CEIL1"], 2), (at["GLASS"], 3), (at["GLITCH0"], 4), (at["CARPET0"], 0)]
    for c, kind in breaks:
        if kind == 4:  # wait for the glass to be there
            for _ in range(120):
                if g.peek("look_glitch_phase") == 0:
                    break
                run(1)
        g.poke("look_dbg_cell", c)
        g.poke("look_dbg_kind", kind if kind else 1)
        run(34)
    run(10)
    rep["fragile_pits"] = sum(1 for c, _ in breaks if g.pb.memory[skin + c] == M["PIT"] + 1)
    rep["fragile_idle"] = g.peek("look_busy_n") == 0
    rep["fragile_ok"] = rep["fragile_pits"] == len(breaks) and rep["fragile_idle"] and phases == {0, 1, 2}
    gif(frames, out / "b1_fragile_break.gif", scale=3)
    g.stop()
    return rep


ROOM_NITEM = 241  # eg_room: c[224], start, door, keys, tokens, nspawn, spawn[6], spawn_kind[6], nitem, item_cell[6],
ROOM_ITEM_CELL = 242  # item_piece[6] (src/core/eg_room.h)
ROOM_ITEM_PIECE = 248


def labelled(img: Image.Image, label: str, scale: int) -> Image.Image:
    from PIL import ImageDraw
    big = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    out = Image.new("RGB", (big.width, big.height + 20), (24, 22, 32))
    out.paste(big, (0, 20))
    ImageDraw.Draw(out).text((4, 4), label, fill=(236, 220, 255))
    return out


def items(rom: Path, out: Path) -> dict:
    M = look_ids()
    rep: dict = {}
    g = H.Game(rom)
    ok = H.start_run(g, god=True, no_enemies=True)
    g.tick(45)
    room = g.addr("room")
    mem = g.pb.memory
    n0 = mem[room + ROOM_NITEM]
    natural = [mem[room + ROOM_ITEM_CELL + k] for k in range(n0) if mem[room + mem[room + ROOM_ITEM_CELL + k]] & 0x1F == 17]
    rep["items_in_room"] = n0
    floor = frame(g)
    mock = Image.open(out / "mock_room.png").convert("RGB")
    k = max(1, mock.width // 160)  # the same scale: the mock is drawn at k x the screen
    a = labelled(mock, "mock_room.png (target)", 1)
    b = labelled(floor, "the B1 floor (b1-items, PyBoy), %dx" % k, k)
    vs = Image.new("RGB", (a.width + 16 + b.width, max(a.height, b.height)), (24, 22, 32))
    vs.paste(a, (0, 0))
    vs.paste(b, (a.width + 16, 0))
    vs.save(out / "b1_floor_vs_mock.png")
    # every kind: six at a time on carpet around the player (the room's item list holds six), then thin ice
    px, py = g.peek("pl_x") >> 4, g.peek("pl_y") >> 4
    cx, cy = g.peek("cam_x"), g.peek("cam_y")
    spots = []
    for c in range(224):
        x, y = c % 16, c // 16
        sx, sy = x * 16 - cx, y * 16 - cy
        if mem[room + c] & 0x1F == 0 and (x, y) != (px, py) and 0 <= sx <= 144 and 0 <= sy <= 96:
            spots.append((abs(x - px) + abs(y - py), c))
    spots = [c for _, c in sorted(spots)][:8]
    skin = g.addr("look_skin")

    def redraw(c: int) -> None:
        mem[skin + c] = 0
        g.poke("look_dbg_cell", c)
        g.poke("look_dbg_kind", 0x80)  # applies "no skin" and redraws the cell
        g.tick()

    shots = []
    seen = set()
    for first in range(1, 17, 6):
        pieces = list(range(first, min(first + 6, 17)))
        for c in natural:
            mem[room + c] = 0
            redraw(c)
        mem[room + ROOM_NITEM] = len(pieces)
        for k, p in enumerate(pieces):
            c = spots[k]
            mem[room + ROOM_ITEM_CELL + k] = c
            mem[room + ROOM_ITEM_PIECE + k] = p
            mem[room + c] = 17
            redraw(c)
        if first == 13:  # the last four: and the two stages of thin ice
            for k, t in ((6, 29), (7, 30)):
                mem[room + spots[k]] = t
                redraw(spots[k])
        g.tick(4)
        natural = spots[: len(pieces)]
        shots.append(frame(g))
        vram_ok = all(g.peek("meta_pal", M["ITEM0"] + p - 1) & 0x80 for p in pieces)
        seen.add(vram_ok)
    strip = Image.new("RGB", (160 * 3 + 16, 144), (24, 22, 32))
    for k, f in enumerate(shots):
        strip.paste(f, (k * 168, 0))
    big = strip.resize((strip.width * 3, strip.height * 3), Image.NEAREST)
    o = Image.new("RGB", (big.width, 144 + 8 + big.height), (24, 22, 32))
    o.paste(strip, (0, 0))
    o.paste(big, (0, 152))
    o.save(out / "b1_items_room.png")
    rep["items_ok"] = ok and seen == {True}
    g.stop()
    return rep


def main() -> int:
    rom, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    rep: dict = {}
    # 1. assembly, a floor, the exit collapse
    g = H.Game(rom)
    ok = start(g, 0x5EED, enemies=False)
    a = g.addr("room")
    thinned = 0
    for c in range(224):
        x, y = c % 16, c // 16
        if g.pb.memory[a + c] & 0x1F == 1 and ((x // 3) + (y // 2)) % 4:
            g.pb.memory[a + c] = 0
            thinned += 1
    rep["foam_thinned"] = thinned
    frames: list[Image.Image] = []
    n = 0
    while g.peek("game_mode") != H.MODE_PLAY and n < 600:
        g.tick()
        frames.append(frame(g))
        n += 1
    rep["assemble_frames"] = n
    rep["assembled"] = ok and g.peek("game_mode") == H.MODE_PLAY and g.peek("look_busy_n") == 0
    pres = present(g)
    rep["cells_absent_after_assembly"] = 224 - sum(pres)
    for _ in range(45):
        g.tick()
        frames.append(frame(g))
    shot = frame(g)
    pair = Image.new("RGB", (160 + 8 + 480, 432), (24, 22, 32))
    pair.paste(shot, (0, 0))
    pair.paste(shot.resize((480, 432), Image.NEAREST), (168, 0))
    pair.save(out / "b1_look_floor.png")
    mock = Image.open(out / "mock_room.png").convert("RGB")
    vs = Image.new("RGB", (640 * 2 + 16, 576), (24, 22, 32))
    vs.paste(shot.resize((640, 576), Image.NEAREST), (0, 0))
    vs.paste(mock.resize((640, 576), Image.NEAREST), (656, 0))
    vs.save(out / "b1_look_vs_mock.png")
    g.poke("pl_state", PS_EXIT)  # out through the door (as if): the floor falls apart behind
    for _ in range(130):
        g.tick()
        frames.append(frame(g))
        if g.peek("game_mode") != H.MODE_PLAY:
            break
    rep["exit_collapse_absent"] = 224 - sum(present(g))
    gif(frames, out / "b1_look_assemble.gif")
    g.stop()
    # 2. another seed, the camera lower: and the death collapse
    g = H.Game(rom)
    ok2 = start(g, 0xD1E, enemies=False)
    while g.peek("game_mode") != H.MODE_PLAY and g.frames < 4000:
        g.tick()
    g.tick(30)
    frame(g).save(out / "b1_look_floor2.png")
    dframes = []
    g.poke("pl_hp", 0)
    for _ in range(170):
        g.tick()
        dframes.append(frame(g))
        if g.peek("game_mode") == H.MODE_DEAD:
            break
    gif(dframes, out / "b1_look_death.gif")
    rep["death_reached"] = ok2
    g.stop()
    # 3. the fragile floors: skins in a room, the glitch beat, the stages, the breaks
    rep.update(fragile(rom, out))
    # 4. the pieces lying in a room, as cards; the floor beside the mock
    rep.update(items(rom, out))
    print(json.dumps(rep, indent=2))
    ok_all = rep["assembled"] and rep["cells_absent_after_assembly"] == 0 and rep["fragile_ok"] and rep["items_ok"]
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
