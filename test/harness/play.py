"""PyBoy harness for EGRESS.EXE: boot, play seeded floors, screenshot, check.

    python test/harness/play.py build/egress.gbc docs/shots [--egtool build/host/egtool] [--audio DIR]

Runs, each from power-on on a blank save, headless and deterministic:

  boot     the KEEL card and the title come up (screenshots)
  solve    a seeded run with enemies off and the player invulnerable: the cartridge's room must match the host
           generator byte for byte, then an autopilot drives the host solver's plan through the real game (walking,
           digging, pushing CRTs, breaking drywall) until the egress door takes the player to floor 2
  combat   a seeded run with enemies on (invulnerable): scripted lashes, a charged spin and dodge rolls near the
           enemies; checks enemies take damage and die, and screenshots the fight
  death    a seeded run with enemies on and mortal: stands still until something kills the player; the run must end
           on the death screen and return to the title

Writes screenshots and report.json into the output directory; exit status 1 if any check failed.
"""

from __future__ import annotations

import argparse
import io
import json
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image
from pyboy import PyBoy

import title3d  # noqa: E402  (test/harness/title3d.py: the 3D title checks)

MODE_BOOT, MODE_TITLE, MODE_PLAY, MODE_CLEAR, MODE_DEAD = range(5)
EG_W, EG_N = 16, 224
T_MASK, T_EMPTY, T_DIRT, T_BRICK, T_ROCK = 0x0F, 0, 1, 3, 4
DIRS = {"U": (0, -1, "up"), "D": (0, 1, "down"), "L": (-1, 0, "left"), "R": (1, 0, "right")}
SEED = 0x5EED


class Game:
    def __init__(self, rom: Path, audio: bool = False, sram: bytes | None = None) -> None:
        self.ram = io.BytesIO(sram if sram is not None else bytes(8192))
        self.pb = PyBoy(str(rom), window="null", cgb=True, sound_emulated=audio, log_level="ERROR",
                        ram_file=self.ram)
        self.pb.set_emulation_speed(0)
        noi = rom.with_suffix(".noi").read_text(encoding="latin-1")
        self.sym = {m[1]: int(m[2], 16) for m in re.finditer(r"DEF (\S+) 0x([0-9A-Fa-f]+)", noi)}
        self.frames = 0
        self.held: set[str] = set()
        self.audio: list[bytes] = []
        self.audio_on = audio

    def addr(self, name: str) -> int:
        return self.sym["_" + name]

    def peek(self, name: str, off: int = 0) -> int:
        return self.pb.memory[self.addr(name) + off]

    def peek16(self, name: str) -> int:
        return self.peek(name) | (self.peek(name, 1) << 8)

    def poke(self, name: str, v: int, off: int = 0) -> None:
        self.pb.memory[self.addr(name) + off] = v & 0xFF

    def poke16(self, name: str, v: int) -> None:
        self.poke(name, v)
        self.poke(name, v >> 8, 1)

    def tick(self, n: int = 1) -> None:
        for _ in range(n):
            self.pb.tick(1, True)
            self.frames += 1
            if self.audio_on:
                self.audio.append(self.pb.sound.ndarray.tobytes())

    def hold(self, buttons: set[str]) -> None:
        for b in self.held - buttons:
            self.pb.button_release(b)
        for b in buttons - self.held:
            self.pb.button_press(b)
        self.held = set(buttons)

    def press(self, button: str, frames: int = 2) -> None:
        self.hold(self.held | {button})
        self.tick(frames)
        self.hold(self.held - {button})
        self.tick(1)

    def wait_mode(self, mode: int, limit: int = 2000) -> bool:
        for _ in range(limit):
            if self.peek("game_mode") == mode:
                return True
            self.tick()
        return False

    def shot(self, path: Path) -> None:
        img: Image.Image = self.pb.screen.image.convert("RGB")
        img.save(path)

    def room(self) -> list[int]:
        a = self.addr("room")
        return [self.pb.memory[a + i] for i in range(EG_N + 5)]

    def player(self) -> tuple[int, int]:
        return self.peek("pl_x"), self.peek("pl_y")

    def stop(self) -> bytes:
        """stops the emulator; returns the cartridge RAM as the battery would keep it"""
        out = io.BytesIO()
        self.pb.stop(save=True, ram_file=out)
        return out.getvalue()[:8192]


def start_run(g: Game, god: bool, no_enemies: bool, seed: int = SEED, floor: int = 0) -> bool:
    if not g.wait_mode(MODE_TITLE, 1500):
        return False
    g.tick(30)
    g.poke("dbg_god", int(god))
    g.poke("dbg_no_enemies", int(no_enemies))
    g.poke16("dbg_seed", seed)
    g.poke("dbg_floor", floor)
    for _ in range(40):  # the title polls once its fade-in is done: press until the run starts
        g.press("start")
        if g.wait_mode(MODE_PLAY, 20):
            break
    else:
        return False
    g.tick(40)  # the fade in
    return True


# --- the autopilot: drive the solver's grid plan through the real-time game ---
def cell_center(c: int) -> tuple[int, int]:
    return (c % EG_W) * 16 + 8, (c // EG_W) * 16 + 8


def settle_board(g: Game, limit: int = 240) -> None:
    quiet = 0
    for _ in range(limit):
        quiet = quiet + 1 if not g.peek("board_busy") else 0
        if quiet >= 18:
            return
        g.tick()


def align(g: Game, tx: int, ty: int, axis: str, limit: int = 60) -> None:
    for _ in range(limit):
        x, y = g.player()
        if axis == "y":
            if abs(y - ty) <= 1:
                break
            g.hold({"down" if y < ty else "up"})
        else:
            if abs(x - tx) <= 1:
                break
            g.hold({"right" if x < tx else "left"})
        g.tick()
    g.hold(set())


def step(g: Game, mv: str, log: list[str]) -> bool:
    x, y = g.player()
    cur = (y // 16) * EG_W + (x // 16)
    if mv in "udlr":
        d = mv.upper()
        dx, dy, btn = DIRS[d]
        target = cur + dx + dy * EG_W
        cx, cy = cell_center(cur)
        align(g, cx, cy, "y" if dx else "x")
        g.hold({btn})
        g.tick(1)
        g.hold(set())
        for _ in range(6):
            if g.room()[target] & T_MASK != T_BRICK:
                return True
            g.press("a", 2)
            g.tick(22)
        log.append(f"drywall at {target} did not break")
        return False
    dx, dy, btn = DIRS[mv]
    target = cur + dx + dy * EG_W
    tx, ty = cell_center(target)
    cx, cy = cell_center(cur)
    align(g, cx, cy, "y" if dx else "x")
    floor0 = g.peek("floor_no")
    for _ in range(240):
        x, y = g.player()
        if g.peek("floor_no") != floor0 or g.peek("game_mode") != MODE_PLAY:
            g.hold(set())
            return True
        if (dx and abs(x - tx) <= 1) or (dy and abs(y - ty) <= 1):
            g.hold(set())
            g.tick(1)
            return True
        g.hold({btn})
        g.tick()
    g.hold(set())
    log.append(f"stuck moving {mv} from cell {cur} toward {target} at {g.player()}")
    return False


def drive_plan(g: Game, moves: str, log: list[str], shots_at: dict[int, Path] | None = None) -> None:
    floor0 = g.peek("floor_no")
    for i, mv in enumerate(moves):
        settle_board(g)
        if shots_at and i in shots_at:
            g.shot(shots_at[i])
        if not step(g, mv, log):
            break
        if g.peek("game_mode") != MODE_PLAY or g.peek("floor_no") != floor0:
            break


def solve_here(g: Game, egtool: Path, log: list[str]) -> str:
    seed, tier = g.peek16("floor_seed"), g.peek("floor_tier")
    plan = subprocess.run([str(egtool), "solve", str(seed), str(tier)], capture_output=True, text=True)
    moves = plan.stdout.strip()
    drive_plan(g, moves, log)
    return moves


def run_solve(g: Game, egtool: Path, out: Path, report: dict) -> bool:
    ok = start_run(g, god=True, no_enemies=True)
    report["solve_started"] = ok
    if not ok:
        return False
    seed, tier = g.peek16("floor_seed"), g.peek("floor_tier")
    dump = subprocess.run([str(egtool), "dump", str(seed), str(tier)], capture_output=True, text=True, check=True)
    host_cells = bytes.fromhex(dump.stdout.split()[0])
    cart = bytes(g.room()[:EG_N])
    report["floor_seed"], report["floor_tier"] = seed, tier
    report["room_matches_host"] = cart == host_cells
    g.shot(out / "03_floor1_start.png")
    plan = subprocess.run([str(egtool), "solve", str(seed), str(tier)], capture_output=True, text=True)
    moves = plan.stdout.strip()
    report["plan"] = moves
    report["plan_stats"] = plan.stderr.strip()
    log: list[str] = []
    shots_at = {len(moves) // 3: out / "04_floor1_digging.png", (2 * len(moves)) // 3: out / "05_floor1_later.png"}
    drive_plan(g, moves, log, shots_at)
    for _ in range(300):
        if g.peek("floor_no") >= 2 and g.peek("game_mode") == MODE_PLAY:
            break
        g.tick()
    report["autopilot_log"] = log
    report["reached_floor2"] = g.peek("floor_no") >= 2
    report["keys_held_at_end"] = g.peek("keys_held")
    g.tick(60)
    g.shot(out / "06_floor2.png")
    return report["room_matches_host"] and report["reached_floor2"]


ENEMY_BYTES = 16


def enemies(g: Game) -> list[dict]:
    out = []
    base = g.addr("en")
    for k in range(g.peek("nen")):
        m = [g.pb.memory[base + k * ENEMY_BYTES + i] for i in range(ENEMY_BYTES)]
        out.append({"kind": m[0], "st": m[1], "hp": m[3], "x": (m[8] | m[9] << 8) >> 4, "y": (m[10] | m[11] << 8) >> 4})
    return out


ES_DEAD = 6


def path_step(cells: list[int], start: int, goal: int, through: set[int] = frozenset()) -> str | None:
    """first move of a shortest walk from start to goal over cells the player can enter (BFS); `through` adds tile
    types the walk may cut through (drywall)"""
    from collections import deque

    passable = {0, 1, 5, 6} | set(through)
    prev = {start: None}
    q = deque([start])
    while q:
        c = q.popleft()
        if c == goal:
            break
        for mv, (dx, dy, _b) in DIRS.items():
            n = c + dx + dy * EG_W
            if 0 <= n < EG_N and n not in prev and (cells[n] & T_MASK in passable or n == goal):
                prev[n] = (c, mv)
                q.append(n)
    if goal not in prev:
        return None
    c, first = goal, None
    while prev[c] is not None:
        c, first = prev[c]
    return first


def path_near(cells: list[int], start: int, tx: int, ty: int) -> str | None:
    """first move toward the reachable cell closest to the point (tx, ty): for enemies that float over walls"""
    from collections import deque

    prev = {start: None}
    q = deque([start])
    while q:
        c = q.popleft()
        for mv, (dx, dy, _b) in DIRS.items():
            n = c + dx + dy * EG_W
            if 0 <= n < EG_N and n not in prev and cells[n] & T_MASK in {0, 1, 5, 6}:
                prev[n] = (c, mv)
                q.append(n)
    best = min(prev, key=lambda c: abs(cell_center(c)[0] - tx) + abs(cell_center(c)[1] - ty))
    if best == start:
        return None
    c, first = best, None
    while prev[c] is not None:
        c, first = prev[c]
    return first


def run_combat(g: Game, out: Path, report: dict) -> bool:
    """hunt the floor's enemies with the lead: walk a shortest path to the nearest one, line up, lash or spin"""
    ok = start_run(g, god=True, no_enemies=False, seed=0xCAFE)
    report["combat_started"] = ok
    if not ok:
        return False
    ens = enemies(g)
    report["enemies_at_start"] = len(ens)
    hp_total0 = sum(e["hp"] for e in ens)
    tokens0 = g.peek("tokens")
    lash_shot = spin_shot = close_shot = False
    log: list[str] = []
    strikes = 0
    for k in range(600):
        live = [e for e in enemies(g) if e["st"] != ES_DEAD]
        if not live:
            break
        px, py = g.player()
        e = min(live, key=lambda e: abs(e["x"] - px) + abs(e["y"] - py))
        dx, dy = e["x"] - px, e["y"] - py
        if (abs(dx) <= 26 and abs(dy) <= 12) or (abs(dy) <= 26 and abs(dx) <= 12):
            face = ("right" if dx > 0 else "left") if abs(dx) > abs(dy) else ("down" if dy > 0 else "up")
            if not close_shot:
                g.shot(out / "08_combat_close.png")
                close_shot = True
            g.hold({face})
            g.tick(1)
            g.hold(set())
            strikes += 1
            if strikes % 4 == 0:
                g.hold({"a"})  # hold through the lash and the charge, then let go: the spin
                g.tick(40)
                g.hold(set())
                g.tick(3)
                if g.peek("pl_state") == 3 and not spin_shot:
                    g.shot(out / "09_combat_spin.png")
                    spin_shot = True
                g.tick(16)
            else:
                g.press("a", 2)
                g.tick(2)
                if g.peek("pl_state") == 1 and not lash_shot:
                    g.shot(out / "07_combat_lash.png")
                    lash_shot = True
                g.tick(12)
            continue
        cur = (py // 16) * EG_W + px // 16
        goal = (e["y"] // 16) * EG_W + e["x"] // 16
        mv = path_step(g.room(), cur, goal) or path_near(g.room(), cur, e["x"], e["y"])
        if mv is None and abs(dx) + abs(dy) < 40:
            g.tick(6)  # as close as the floor allows: wait for it to drift into reach
            continue
        if mv is None or (strikes == 0 and k % 7 == 6):
            g.press("b", 2)  # nothing to walk to (or now and then): roll toward it and keep moving
            g.tick(10)
            mv = mv or ("left" if dx < 0 else "right")[0].upper()
        step(g, mv, log)
    ens = enemies(g)
    report["combat_strikes"] = strikes
    report["enemies_killed"] = sum(1 for e in ens if e["st"] == ES_DEAD)
    report["enemy_hp_removed"] = hp_total0 - sum(e["hp"] for e in ens if e["st"] != ES_DEAD)
    report["tokens_gained"] = g.peek("tokens") - tokens0
    g.tick(20)
    g.shot(out / "10_combat_end.png")
    return report["enemies_killed"] >= 1


def run_death(g: Game, out: Path, report: dict) -> bool:
    ok = start_run(g, god=False, no_enemies=False, seed=0xD1E)
    if not ok:
        return False
    hp0 = g.peek("pl_hp")
    report["death_hp_start"] = hp0
    # walk up to the nearest enemy and stand there, never striking, until the run ends
    took = False
    log: list[str] = []
    for _ in range(900):
        if g.peek("pl_hp") < hp0:
            took = True
        if g.peek("game_mode") == MODE_DEAD:
            break
        live = [e for e in enemies(g) if e["st"] != ES_DEAD]
        px, py = g.player()
        if live:
            e = min(live, key=lambda e: abs(e["x"] - px) + abs(e["y"] - py))
            if abs(e["x"] - px) + abs(e["y"] - py) > 20:
                cur = (py // 16) * EG_W + px // 16
                mv = path_step(g.room(), cur, (e["y"] // 16) * EG_W + e["x"] // 16)
                if mv and g.peek("game_mode") == MODE_PLAY and g.peek("pl_hp"):
                    step(g, mv, log)
                    continue
        g.tick(10)
    g.hold(set())
    report["death_took_damage"] = took
    report["death_reached_screen"] = g.peek("game_mode") == MODE_DEAD
    if report["death_reached_screen"]:
        g.tick(90)
        g.shot(out / "12_death.png")
        g.tick(40)
        g.press("a")
        report["death_back_to_title"] = g.wait_mode(MODE_TITLE, 600)
        if report["death_back_to_title"] and out is not None:
            # the 3D title again after a run: it re-inits its memory, walks a new seed, and START plays again
            g.tick(150)  # the title zeroes its counters on entry: measure once it walks
            title3d.shot_lit(g, out / "b1_title_return.png")
            v0 = g.peek16("t3d_nviews")
            g.tick(120)
            report["title_return_views"] = (g.peek16("t3d_nviews") - v0) & 0xFFFF  # ~48 at 24 fps
            report["title_return_seed"] = f"{g.peek16('t3d_seed'):04X}"
            for _ in range(40):
                g.press("start")
                if g.wait_mode(MODE_PLAY, 20):
                    break
            report["title_return_plays"] = g.peek("game_mode") == MODE_PLAY
            took = took and 36 < report["title_return_views"] < 80 and report["title_return_plays"]
    return took


def crc16(b: bytes) -> int:
    c = 0xFFFF
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def read_save(sram: bytes) -> dict | None:
    """the newer valid record of the two slots (src/run.c): magic EG, version, generation, meta, CRC-16/CCITT"""
    best = None
    for off in (0, 32):
        r = sram[off:off + 22]
        if r[:3] != b"EG\x01" or crc16(r[:20]) != (r[20] | r[21] << 8):
            continue
        m = r[4:20]
        rec = {"gen": r[3], "runs": m[0] | m[1] << 8, "wins": m[2], "best_floor": m[3], "unlocked": m[4],
               "seen": m[5] | m[6] << 8, "trace": list(m[7:16]), "slot": off // 32}
        if best is None or ((rec["gen"] - best["gen"]) & 0xFF) < 128:
            best = rec
    return best


def run_landing(g: Game, egtool: Path, out: Path, report: dict) -> bool:
    """start on floor 3 (enemies off), solve it; the landing after it: a reply, then a purchase, then floor 4"""
    ok = start_run(g, god=True, no_enemies=True, floor=3)
    if not ok:
        return False
    log: list[str] = []
    solve_here(g, egtool, log)
    for _ in range(400):
        if g.peek("game_mode") == MODE_CLEAR:
            break
        g.tick()
    g.tick(120)  # the exit fade, the landing's fade in
    report["landing_log"] = log
    report["landing_mode"] = g.peek("game_mode")
    g.shot(out / "13_landing_talk.png")
    truth0 = [g.peek("truth", i) for i in range(9)]
    g.poke("tokens", 40)
    g.press("a")
    g.tick(30)
    g.shot(out / "14_landing_trade.png")
    g.press("a")  # buy the first offer
    g.tick(20)
    report["landing_mods"] = g.peek("mods")
    report["landing_tokens_left"] = g.peek("tokens")
    g.shot(out / "15_landing_bought.png")
    g.press("b")
    ok4 = False
    for _ in range(400):
        if g.peek("game_mode") == MODE_PLAY and g.peek("floor_no") == 4:
            ok4 = True
            break
        g.tick()
    standing = [((g.peek("standing", i) + 128) & 0xFF) - 128 for i in range(6)]
    truth1 = [g.peek("truth", i) for i in range(9)]
    report["landing_standing"] = standing
    report["landing_truth_before_after"] = [truth0, truth1]
    report["landing_reached_floor4"] = ok4
    return ok4 and report["landing_mods"] != 0 and report["landing_tokens_left"] < 40 and truth0 != truth1


def run_boss(g: Game, out: Path, report: dict) -> bool:
    """floor 10, invulnerable: hunt THE WATCHMAN with lashes and spins until it falls, walk out of the door, the
    ending's question"""
    ok = start_run(g, god=True, no_enemies=False, seed=0xB055, floor=10)
    if not ok:
        return False
    g.tick(30)
    g.shot(out / "16_boss_start.png")
    shots = {"tell": False, "orbs": False, "stun": False}
    log: list[str] = []
    strikes = 0
    for k in range(2500):
        ens = enemies(g)
        boss = ens[0] if ens else None
        if not boss or boss["st"] == ES_DEAD:
            break
        if boss["st"] == 1 and not shots["tell"] and boss["y"] > 40:
            g.shot(out / "17_boss_tell.png")
            shots["tell"] = True
        if not shots["orbs"] and sum(g.peek("orbs", i * 10) for i in range(8)) >= 3:
            g.shot(out / "18_boss_fan.png")
            shots["orbs"] = True
        if boss["st"] == 8 and not shots["stun"]:
            g.shot(out / "19_boss_stunned.png")
            shots["stun"] = True
        live = [e for e in ens if e["st"] != ES_DEAD]
        px, py = g.player()
        e = min(live, key=lambda e: abs(e["x"] - px) + abs(e["y"] - py))
        dx, dy = e["x"] - px, e["y"] - py
        near = 30 if e["kind"] == 2 else 22
        if (abs(dx) <= near and abs(dy) <= 12) or (abs(dy) <= near and abs(dx) <= 12):
            face = ("right" if dx > 0 else "left") if abs(dx) > abs(dy) else ("down" if dy > 0 else "up")
            g.hold({face})
            g.tick(1)
            g.hold(set())
            strikes += 1
            if strikes % 4 == 0:
                g.hold({"a"})
                g.tick(40)
                g.hold(set())
                g.tick(19)
            else:
                g.press("a", 2)
                g.tick(14)
            continue
        cur = (py // 16) * EG_W + px // 16
        gx, gy = e["x"], e["y"] + (18 if e["kind"] == 2 else 0)  # under the Watchman
        goal = (min(gy, 200) // 16) * EG_W + gx // 16
        mv = path_step(g.room(), cur, goal)
        if mv is None:
            g.tick(8)
            continue
        step(g, mv, log)
    ens = enemies(g)
    report["boss_strikes"] = strikes
    report["boss_dead"] = bool(ens) and ens[0]["st"] == ES_DEAD
    report["boss_hp_left"] = ens[0]["hp"] if ens else None
    report["boss_frames"] = g.frames
    report["boss_shots"] = shots
    g.tick(60)
    g.shot(out / "20_boss_down.png")
    if not report["boss_dead"]:
        return False
    door = 7
    for _ in range(80):
        if g.peek("game_mode") != MODE_PLAY:
            break
        px, py = g.player()
        cur = (py // 16) * EG_W + px // 16
        cells = g.room()
        mv = path_step(cells, cur, door, {T_BRICK})
        if mv is None:
            break
        dx, dy, _b = DIRS[mv]
        if cells[cur + dx + dy * EG_W] & T_MASK == T_BRICK:
            mv = mv.lower()  # the gallery's drywall: break it
        settle_board(g)
        step(g, mv, log)
    for _ in range(400):
        if g.peek("game_mode") == MODE_CLEAR and g.peek("pl_state") != 5:
            break
        g.tick()
    g.tick(150)
    g.shot(out / "21_ending.png")
    report["boss_log"] = log[-5:]
    g.press("a")
    report["ending_back_to_title"] = g.wait_mode(MODE_TITLE, 900)
    return report["ending_back_to_title"]


def contact(out: Path, names: list[str]) -> None:
    imgs = [Image.open(out / n) for n in names if (out / n).exists()]
    if not imgs:
        return
    cols = 4
    rows = (len(imgs) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * 164, rows * 148), (24, 24, 30))
    for i, im in enumerate(imgs):
        sheet.paste(im, ((i % cols) * 164 + 2, (i // cols) * 148 + 2))
    sheet = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
    sheet.save(out / "contact.png")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--egtool", type=Path, default=Path("build/host/egtool"))
    ap.add_argument("--t3d-host", type=Path, default=Path("build/host/t3d_host"))
    ap.add_argument("--audio", type=Path, help="record the title and floor music as WAV into this directory")
    ap.add_argument("--only", choices=["boot", "solve", "combat", "death", "landing", "boss"])
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    report: dict = {"rom": str(a.rom), "size": a.rom.stat().st_size}
    results = {}

    if a.only in (None, "boot"):
        g = Game(a.rom, audio=bool(a.audio))
        g.tick(150)  # past the CGB boot ROM's logo and the fade in
        g.shot(a.out / "01_boot_keel.png")
        report["boot_mode_at_150"] = g.peek("game_mode")
        results["boot"] = g.wait_mode(MODE_TITLE, 1500)
        results["title3d"] = title3d.title_pass(g, a.out, report, a.t3d_host)
        title3d.shot_lit(g, a.out / "02_title.png")  # the walk, well under way
        if a.audio:
            g.audio.clear()
            g.tick(60 * 20)
            write_wav(a.audio / "lobby.wav", g.audio)
            g.audio.clear()
        g.stop()

    if a.only in (None, "solve"):
        g = Game(a.rom)
        results["solve"] = run_solve(g, a.egtool, a.out, report)
        g.stop()

    if a.only in (None, "combat"):
        g = Game(a.rom, audio=bool(a.audio))
        results["combat"] = run_combat(g, a.out, report)
        if a.audio:
            write_wav(a.audio / "stairwell_with_sfx.wav", g.audio)
        g.stop()

    if a.only in (None, "death"):
        g = Game(a.rom)
        results["death"] = run_death(g, a.out, report)
        sram = g.stop()
        # the battery save: one run on record; a second boot on that save reads it back and adds the next run
        rec = read_save(sram)
        report["save_after_death"] = rec
        g = Game(a.rom, sram=sram)
        run_death(g, a.out, {})
        rec2 = read_save(g.stop())
        report["save_after_second_death"] = rec2
        results["save"] = bool(rec and rec["runs"] == 1 and rec2 and rec2["runs"] == 2 and rec2["slot"] != rec["slot"])

    if a.only in (None, "landing"):
        g = Game(a.rom)
        results["landing"] = run_landing(g, a.egtool, a.out, report)
        g.stop()

    if a.only in (None, "boss"):
        g = Game(a.rom)
        results["boss"] = run_boss(g, a.out, report)
        rec = read_save(g.stop())
        report["save_after_win"] = rec
        results["save_win"] = bool(rec and rec["wins"] == 1 and rec["unlocked"] & 0x20)

    report["results"] = results
    contact(a.out, sorted(p.name for p in a.out.glob("[0-2][0-9]_*.png")))
    (a.out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k not in ("plan",)}, indent=2))
    return 0 if all(results.values()) else 1


def write_wav(path: Path, chunks: list[bytes]) -> None:
    import wave

    path.parent.mkdir(parents=True, exist_ok=True)
    data = b"".join(chunks)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(1)
        w.setframerate(48000)
        w.writeframes(bytes((b + 128) & 0xFF for b in data))


if __name__ == "__main__":
    sys.exit(main())
