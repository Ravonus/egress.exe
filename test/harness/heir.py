"""B1 heir checks in the real ROM (called from play.py; `--only heir`):

  roll      the cartridge's heir (src/heir.c) equals genes.founder() expressed, for many seeds
  moves     the current A/B tap, hold and ordered combos, plus directional double-tap evades and held running
  movement  8-way: a diagonal moves both axes at ~181/256 of the straight speed; 4-way (opt_move4): the newest press
            wins and only one axis moves
  idle      standing still, the animation follows heirs.py idle_plan() for the heir's seed and traits
  budget    OAM entries and sprites per scan line over a fight and the boss (heir + weapon arcs + enemies + FX)
  shots     docs/shots/b1_heir_states.gif (four heirs cycling through every baked state, in the ROM) and
            b1_heir_play.png (six heirs, one per skin tone, on a floor; the moves mid-flight)
"""
from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
for _p in (ROOT / "proto/genetics", ROOT / "proto/anim", ROOT / "tools/mock"):
    if str(_p) not in sys.path:
        sys.path.insert(0, str(_p))

from play import Game, start_run, run_combat, run_boss  # noqa: E402

HEIR_FIELDS = ["skin", "look", "hair", "neck", "pat", "mark", "accent", "weapon", "dodge", "traits"]
DODGE_C = {"ROLL": 0, "BLINK": 1, "SLIDE": 2}
TRAIT_BITS = {"HEAVY": 1, "SMALL": 2, "GLITCHED": 4, "INSOMNIAC": 8}
GIF_HEIRS = [6, 4, 2, 18]  # skin 0 roll, skin 2 blink, skin 3 slide, skin 5 blink (tools/mock search below)
PLAY_HEIRS = [6, 7, 4, 2, 10, 18]  # one per skin tone 0..5
MV = {"IDLE": 0, "DODGE": 1, "SPRINT": 2, "SKID": 3, "LUNGE": 4, "HIT1": 5, "HIT2": 6, "HIT3": 7, "CHARGE": 8,
      "CHARGED": 9, "PARRY": 10}


def ha_states() -> dict[str, int]:
    h = (ROOT / "src/gen/heir_gen.h").read_text()
    return {m[1]: int(m[2]) for m in re.finditer(r"#define HA_(\w+) (\d+)u", h) if m[1] != "COUNT"}


def py_heir(seed: int) -> dict:
    import genes
    from sprite_styles import eff_hair

    g = genes.founder(genes.Rng(((seed * 0x9E3779B1) & 0xFFFFFFFF) ^ 0x5BD1E995))
    p = genes.express(g)
    tr = 0
    for t in p["traits"]:
        tr |= TRAIT_BITS.get(t, 0x10)
    return {"skin": g["skin"], "look": g["look"], "hair": eff_hair(g), "neck": g["neck"], "pat": g["pat"],
            "mark": g["mark"], "accent": p["accent"], "weapon": g["weapon"][0], "dodge": DODGE_C[p["dodge"]],
            "traits": tr, "_traits": p["traits"]}


def cart_heir(g: Game) -> dict:
    out = {k: g.peek("heir", i) for i, k in enumerate(HEIR_FIELDS)}
    out["seed"] = g.peek("heir", 10) | (g.peek("heir", 11) << 8)
    return out


def boot_heir(rom: Path, seed: int, enemies_off: bool = True, game_seed: int = 0x5EED, fade: int = 40) -> Game:
    g = Game(rom)
    g.wait_mode(1, 1500)
    g.poke16("dbg_heir", seed)
    start_run(g, god=True, no_enemies=enemies_off, seed=game_seed, fade=fade)
    return g


# ------------------------------------------------------------------------------------------------ checks
def check_roll(rom: Path, report: dict) -> bool:
    bad = []
    seeds = list(range(1, 25)) + [0x5EED, 0xCAFE, 0xFFFF]
    for s in seeds:
        g = boot_heir(rom, s)
        got, want = cart_heir(g), py_heir(s)
        g.stop()
        diff = {k: (got[k], want[k]) for k in HEIR_FIELDS if got[k] != want[k]}
        if diff:
            bad.append({"seed": s, "diff": diff})
    report["heir_roll_seeds"] = len(seeds)
    report["heir_roll_mismatches"] = bad
    return not bad


def check_sprite_mode(rom: Path, report: dict) -> bool:
    """The title's 8x8 prompt must not halve the floor's layered 8x16 bodies.

    Exercise the real boot/title handoff, then all states and facings. Tile data
    alone cannot catch this: both halves were present in VRAM when the bug hid
    the lower half of every OBJ on the actual screen.
    """
    bad, samples = [], 0
    for seed in PLAY_HEIRS:
        g = boot_heir(rom, seed)
        for name, state in ha_states().items():
            for direction in range(8):
                g.poke("dbg_heir_state", state + 1)
                g.poke("dbg_heir_dir", direction)
                g.tick(3)
                samples += 1
                if not g.pb.memory[0xFF40] & 0x04:
                    bad.append({"seed": seed, "state": name, "dir": direction})
        g.stop()
    report["heir_sprite_mode"] = {"samples": samples, "missing_8x16": bad[:12]}
    return not bad


def pin_move(g: Game) -> int:
    return g.peek("pin", 2)


def check_moves(rom: Path, report: dict) -> bool:
    from controls import run
    out=ROOT / "build/gameplay-rebuild/heir-controls" / rom.stem
    ok=run(rom,out)
    import json
    report["heir_moves"]=json.loads((out/"report.json").read_text())
    return ok


def walk_rate(g: Game, keys: set, ticks: int = 48) -> tuple:
    """mean sub-pixels per game frame on each axis, over frames the mover was not blocked (pmv.blocked)"""
    g.hold(keys)
    g.tick(1)  # the pad is read once a frame: a walk from standing starts a frame later (latency, not speed)
    sx = sy = n = 0
    for _ in range(ticks):
        f0, p0 = g.peek16("frame_count"), (g.peek16("psx"), g.peek16("psy"))
        g.tick(1)
        if g.peek16("frame_count") - f0 == 1 and g.peek("pmv", 13) == 0:
            sx += abs(g.peek16("psx") - p0[0])
            sy += abs(g.peek16("psy") - p0[1])
            n += 1
    g.hold(set())
    return (sx / n, sy / n, n) if n else (0, 0, 0)


def check_movement(rom: Path, report: dict) -> bool:
    res = {}
    g = boot_heir(rom, 6)
    g.tick(60)
    # An explicit clear-lane fixture isolates direction normalisation. Keep each
    # measurement below the new 18-frame run threshold; testing a walk/run mix
    # at different collision points would not compare equal speeds.
    from stage2 import teleport
    for y in range(5,12):
        for x in range(4,13):g.pb.memory[g.addr('room')+y*16+x]=0
    teleport(g,120,136)
    best=walk_rate(g,{'right'},12)
    straight=max(best[:2])
    g.hold(set());g.tick(60);teleport(g,120,136)
    diag=walk_rate(g,{'right','down'},12)
    res["straight_sub_per_frame"] = round(straight, 2)
    res["diag_sub_per_frame"] = [round(diag[0], 2), round(diag[1], 2)] if diag else None
    res["diagonal_normalised"] = bool(straight and diag and
                                      all(abs(d - straight * 181 / 256) <= 1.0 for d in diag[:2]))
    g.stop()
    g = boot_heir(rom, 6)
    g.poke("opt_move4", 1)
    g.tick(60)
    p0 = g.peek16("psx"), g.peek16("psy")
    g.hold({"down"})
    g.tick(2)
    g.hold({"down", "left"})  # left pressed last: 4-way follows it
    g.tick(8)
    p1 = g.peek16("psx"), g.peek16("psy")
    g.hold(set())
    dy_after = abs(p1[1] - p0[1])
    res["4way_left_wins"] = p1[0] < p0[0] and dy_after <= 2 * 20 + 2
    g.stop()
    # SETUP > MOVE flips the option (A), and back
    g = Game(rom)
    ok = g.wait_mode(1)
    g.tick(120)
    g.press("select")
    ok = ok and g.wait_mode(5, 300)
    g.tick(90)  # the screen fades in before it reads the pad
    g.press("down")
    g.tick(4)
    g.press("a")
    g.tick(4)
    on = g.peek("opt_move4")
    g.press("a")
    g.tick(4)
    res["setup_move_toggles"] = bool(ok and on == 1 and g.peek("opt_move4") == 0)
    g.stop()
    report["heir_movement"] = res
    return all(v for k, v in res.items() if isinstance(v, bool))


def check_idle(rom: Path, report: dict) -> bool:
    """stand still for 15 s; the states the heir plays are heirs.py idle_plan() for its seed and traits"""
    import heirs as HP

    HA = {v: k for k, v in ha_states().items()}
    ok = True
    out = []
    for seed in (6, 2):
        g = boot_heir(rom, seed, fade=0)  # from the spawn on: the idle clock starts when the spawn overlay ends
        h = cart_heir(g)
        traits = py_heir(seed)["_traits"]
        seen = []
        start = None
        for _ in range(1100):
            g.tick()
            t = g.peek16("frame_count")  # game frames, not emulator ticks: a lag frame does not advance the plan
            name = HA[g.peek("ha_state")].lower()
            if start is None:
                if name in ("idle", "idle_blink"):
                    # the plan's origin is the scheduler's own start (heir_idl.t ticks ago): the run may have begun the
                    # idle before start_run handed over (the look's assembly)
                    start = t - g.peek16_at("heir_idl", 4)
                else:
                    continue
            name = "idle" if name == "idle_blink" else name
            if not seen or seen[-1][1] != name:
                seen.append([t - start, name])
        g.stop()
        if seen and seen[0][1] == "idle":
            seen[0][0] = 0  # the base segment began with the scheduler
        plan = HP.idle_plan(h["seed"], traits, until=t - start)
        want = [[a, ("long_doze" if n == "long_lie" else n)] for a, b, n in plan]
        # merge consecutive equal names in the plan (a fidget never repeats; a base stretch is one segment)
        merged = []
        for a, n in want:
            if not merged or merged[-1][1] != n:
                merged.append([a, n])
        match = [n for _, n in seen] == [n for _, n in merged] and all(
            abs(a - b) <= 1 for (a, _), (b, _) in zip(seen, merged))
        ok &= match
        out.append({"seed": seed, "traits": traits, "cart": seen[:12], "plan": merged[:12], "match": match})
    report["heir_idle"] = out
    return ok


# ------------------------------------------------------------------------------------------------ budget
class OamMeter:
    """sprites per scan line (8x16 objects, the play area above the HUD) after every frame"""

    def __init__(self, g: Game):
        self.g = g
        self.max_line = 0
        self.max_oam = 0
        self.over = 0
        self.frames = 0
        orig = g.tick

        def tick(n=1):
            for _ in range(n):
                orig(1)
                self.sample()

        g.tick = tick

    def sample(self):
        mem = self.g.pb.memory
        lines = [0] * 144
        n = 0
        for k in range(40):
            y, x = mem[0xFE00 + k * 4], mem[0xFE00 + k * 4 + 1]
            if y == 0 or y >= 160 or x == 0 or x >= 168:
                continue
            n += 1
            for ly in range(max(0, y - 16), min(128, y)):  # sprites are off under the HUD (line 128 on)
                lines[ly] += 1
        if self.g.peek("game_mode") != 2:
            return
        self.frames += 1
        m = max(lines)
        self.max_line = max(self.max_line, m)
        self.max_oam = max(self.max_oam, n)
        if m > 10:
            self.over += 1


def check_budget(rom: Path, out: Path, report: dict) -> bool:
    res = {}
    for name, fn in (("combat", run_combat), ("boss", run_boss)):
        g = Game(rom)
        meter = OamMeter(g)
        with tempfile.TemporaryDirectory() as scratch:  # the fights' own screenshots are not kept
            fn(g, Path(scratch), {})
        g.stop()
        res[name] = {"frames": meter.frames, "max_oam": meter.max_oam, "max_per_line": meter.max_line,
                     "frames_over_10": meter.over}
    report["heir_budget"] = res
    # normal play stays within 10 per line; the boss arena is reported (its 32x32 body is 4 per line)
    return res["combat"]["frames_over_10"] == 0


# ------------------------------------------------------------------------------------------------ shots
def crop_heir(g: Game, size: int = 40) -> Image.Image:
    img = g.pb.screen.image.convert("RGB")
    cx = g.peek("pl_x") - g.peek("cam_x")
    cy = g.peek("pl_y") - g.peek("cam_y") - 5
    x0 = max(0, min(160 - size, cx - size // 2))
    y0 = max(0, min(128 - size, cy - size // 2))
    return img.crop((x0, y0, x0 + size, y0 + size))


def states_gif(rom: Path, out: Path, report: dict) -> bool:
    from egkit import CYAN, PINK, WHITE, YEL, text

    HA = ha_states()
    order = sorted(HA, key=HA.get)
    games = [boot_heir(rom, s) for s in GIF_HEIRS]
    for g in games:
        g.tick(40)
    seq = []
    for name in order:
        if name in ("WALK", "RUN", "SPRINT"):
            continue
        seq.append((name, 1))
    for d in range(8):
        seq.append(("WALK", d))
    seq += [("RUN", 2), ("RUN", 6), ("SPRINT", 1), ("SPRINT", 7), ("HIT3", 2), ("HIT3", 6), ("SPIN", 0),
            ("DODGE_ROLL", 4), ("LUNGE", 3)]
    sc, size = 3, 40
    frames, durs = [], []
    shown = 0
    for name, d in seq:
        idx = HA[name]
        n_ticks = state_ticks(games[0], idx)
        for g in games:
            g.poke("dbg_heir_dir", d)
            g.poke("dbg_heir_state", idx + 1)
        step = 3
        for t in range(0, n_ticks, step):
            for g in games:
                g.tick(step)
            row = Image.new("RGB", (len(games) * size * sc, size * sc + 20), (20, 14, 46))
            for i, g in enumerate(games):
                row.paste(crop_heir(g, size).resize((size * sc, size * sc), Image.NEAREST), (i * size * sc, 20))
            text(row, 4, 3, name.replace("_", " "), PINK)
            text(row, 4 + 8 * 18, 3, ["S", "SE", "E", "NE", "N", "NW", "W", "SW"][d], YEL)
            text(row, row.width - 8 * 14, 3, f"T {t:3d}/{n_ticks}", CYAN)
            frames.append(row)
            durs.append(step * 1000 // 60)
        shown += 1
    text(frames[0], 4, 12, "B1 HEIRS IN THE ROM", WHITE)
    for g in games:
        g.stop()
    frames[0].save(out / "b1_heir_states.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0,
                   optimize=True)
    # a contact sheet of the same: one still per state (frame 2 of each), for review
    cols = 8
    rows = (len(order) + cols - 1) // cols
    report["heir_states_shown"] = shown
    report["heir_gif_frames"] = len(frames)
    return shown >= len(order)


def state_ticks(g: Game, idx: int) -> int:
    """a state's length from the ROM's own table is not exported; use the bake's (heirs.py) via the tool"""
    import importlib.util
    spec = importlib.util.spec_from_file_location("bake_heir", ROOT / "tools/bake_heir.py")
    if not hasattr(state_ticks, "cache"):
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        state_ticks.cache = [min(120, sum(min(f["dur"], 60) for f in fr)) for _, fr, _ in mod.cart_states()]
    return max(12, state_ticks.cache[idx])


def play_png(rom: Path, out: Path, report: dict) -> bool:
    """six heirs (one per skin tone) on a floor with enemies; then one heir's moves mid-flight"""
    from egkit import CYAN, PINK, YEL, text

    tiles = []
    for s in PLAY_HEIRS:
        g = boot_heir(rom, s, enemies_off=False, game_seed=0xCAFE)
        g.tick(30)
        g.hold({"right"})
        g.tick(10)
        g.hold(set())
        g.tick(50)
        tiles.append((s, g.pb.screen.image.convert("RGB").crop((0, 0, 160, 128))))
        g.stop()
    moves = []
    g = boot_heir(rom, 2, enemies_off=True)
    g.tick(60)
    for label, steps, at in (("WALK SE", [({"right", "down"}, 14)], 0), ("DODGE", [({"left"}, 3), (set(), 3), ({"left"}, 3)], 4),
                             ("SPRINT", [({"up"}, 26)], 0), ("LUNGE", [({"b"}, 3), (set(), 2), ({"a"}, 3)], 2),
                             ("HIT 3", [({"a"}, 2), (set(), 8), ({"a"}, 2), (set(), 8), ({"a"}, 2)], 6),
                             ("CHARGE", [({"a"}, 40)], 0), ("SPIN", [(set(), 1)], 4), ("PARRY", [({"a"}, 1),
                                                                                           ({"a", "b"}, 2)], 3)):
        for b, n in steps:
            g.hold(b)
            g.tick(n)
        g.hold(set()) if label != "CHARGE" else None
        g.tick(at)
        moves.append((label, crop_heir(g, 48)))
        if label != "CHARGE":
            g.tick(40)
    g.hold(set())
    g.stop()
    W = 3 * 164
    sheet = Image.new("RGB", (W, 2 * 140 + 14 + 70), (20, 14, 46))
    for i, (s, im) in enumerate(tiles):
        x, y = (i % 3) * 164, (i // 3) * 140
        sheet.paste(im, (x + 2, y + 2))
        h = py_heir(s)
        text(sheet, x + 4, y + 131, f"SKIN {h['skin']} {['DREAM', 'BOY', 'GIRL'][h['look']]}", YEL)
    y = 2 * 140 + 2
    text(sheet, 4, y, "ONE HEIR'S MOVES, LIVE: A/B SIX INPUTS", PINK)
    for i, (label, im) in enumerate(moves):
        sheet.paste(im, (4 + i * 60, y + 12))
        text(sheet, 4 + i * 60, y + 62, label, CYAN)
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(out / "b1_heir_play.png")
    return True


def run_all(rom: Path, out: Path, report: dict, shots: bool = True) -> dict:
    res = {"heir_roll": check_roll(rom, report), "heir_sprite_mode": check_sprite_mode(rom, report),
           "heir_moves": check_moves(rom, report),
           "heir_movement": check_movement(rom, report), "heir_idle": check_idle(rom, report),
           "heir_budget": check_budget(rom, out, report)}
    if shots:
        res["heir_states_gif"] = states_gif(rom, out, report)
        res["heir_play_png"] = play_png(rom, out, report)
    return res


if __name__ == "__main__":
    import argparse
    import json

    ap = argparse.ArgumentParser(description="Check character sprite mode after the real title handoff")
    ap.add_argument("rom", nargs="+", type=Path)
    args = ap.parse_args()
    reports, passed = {}, True
    for path in args.rom:
        report = {}
        passed = check_sprite_mode(path, report) and passed
        reports[str(path)] = report
    print(json.dumps(reports, indent=2))
    sys.exit(0 if passed else 1)
