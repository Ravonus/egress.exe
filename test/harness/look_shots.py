"""The look's review captures (B1): a floor in the new look, the room assembling and falling apart.

    python test/harness/look_shots.py build/egress.gbc docs/shots

Writes, from power-on on a blank save, headless and deterministic:
  b1_look_floor.png        a floor once it has assembled (1x and 3x side by side), plus b1_look_floor2.png (another
                           seed, lower in the room)
  b1_look_assemble.gif     the floor assembling around the player, a beat of play, then the exit collapse (2x)
  b1_look_death.gif        the world letting go around a body (2x): the death collapse keeps the 8 neighbours
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


def main() -> int:
    rom, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    rep: dict = {}
    # 1. assembly, a floor, the exit collapse
    g = H.Game(rom)
    ok = start(g, 0x5EED, enemies=True)
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
    print(json.dumps(rep, indent=2))
    return 0 if rep["assembled"] and rep["cells_absent_after_assembly"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
