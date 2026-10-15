"""EGRESS living obstacles: the office mock room (heavy wanderer + glass), simulated exactly and checked.

Run from tools/mock:   python3 obstacle_room.py     (solve, run the fairness checks, replay the two drawn paths)

The design-stage model of redesign.md 7.7, small enough to read. One tick = one player step. The order is the doc's:
  1. the player acts (move, push or wait); the tile the player left takes its crack;
  2. each obstacle acts if its phase is due (slot order): target cell, resolve, move, crack the tile it left;
  3. dead-state check (the runtime flood fill of 3.4).

Tiles:  '#' wall | '.' floor | 'G' glass partition (solid; only a heavy mob breaks it, by walking into it)
        'g' floor glass (1 step: it breaks into a pit when anything leaves it; a block pushed onto it falls through)
        'o' pit | 'E' exit (the player wins by entering it; solid to mobs and blocks)
Blocks: 'B' a crate (the steering tool), 'C' a CRT (the red herring). Both push one cell, Sokoban rules.

THE MOVER (heavy wanderer) walks straight and acts every 2nd tick. If the cell ahead is a wall, pit, block, exit or
another mob, it turns right instead of moving. A player in the cell ahead is shoved one cell along its path; if the
shove can't land, it tramples over the player (a hit) and they swap cells. So a body never turns it (only solid things
do) and it never traps anyone. It never pushes blocks and never steps into a pit, so floor glass breaks behind it,
never under it.
"""
from __future__ import annotations

import heapq
from collections import deque

MAP = [
    "#######E##",
    "#....##g##",
    "#....G...#",
    "#....#####",
    "#....#####",
    "#....#####",
    "##########",
]
W, H = len(MAP[0]), len(MAP)
DIRS = [(0, -1), (1, 0), (0, 1), (-1, 0)]  # N E S W; turn right = +1
PERIOD = 2
GLASS_G = (5, 2)
LANDING = (7, 1)
EXIT = (7, 0)
START = dict(player=(3, 4), blocks=((2, 3), (3, 3)), mob=(1, 5, 0, 0))  # blocks: crate, CRT. mob: x, y, dir, phase
ACTIONS = {"N": 0, "E": 1, "S": 2, "W": 3, ".": None}

# the paths drawn in docs/shots/mock_obstacle_room.png (both asserted in main)
SOLUTION = ".WNNENW.........EESEEENN"
# the mistake: the same steer, then the CRT is shoved east through the broken partition "to trap it in the corridor".
# The wanderer comes back west, the CRT turns it north onto the landing glass, and the glass breaks behind it.
MISTAKE = SOLUTION[:16] + "EESSSWNWNEEEW" + "." * 16  # ends with it one cell east of the pit


def base(x, y):
    return MAP[y][x] if 0 <= x < W and 0 <= y < H else "#"


def tile(s, x, y):
    """the live tile: the static map plus the two dynamic bits"""
    if (x, y) == GLASS_G:
        return "." if s["g_broken"] else "G"
    if (x, y) == LANDING:
        return "o" if s["landing_broken"] else "g"
    return base(x, y)


def initial():
    return dict(player=START["player"], blocks=START["blocks"], mob=START["mob"], tick=0, g_broken=False,
                landing_broken=False, won=False, hurt=0)


def leave(s, x, y):
    """step-off crack: floor glass breaks behind whoever leaves it"""
    if (x, y) == LANDING:
        s["landing_broken"] = True


def step(s0, act):
    """one tick; None if the player's action is illegal (bumping a wall is not a move)"""
    s = dict(s0)
    mx, my = s["mob"][:2]
    # ---- 1. the player
    d = ACTIONS[act]
    if d is not None:
        px, py = s["player"]
        dx, dy = DIRS[d]
        nx, ny = px + dx, py + dy
        t = tile(s, nx, ny)
        if (nx, ny) == (mx, my) or t in "#Go":
            return None
        if t == "E":
            s.update(player=(nx, ny), won=True)
            leave(s, px, py)
            return s
        if (nx, ny) in s["blocks"]:
            bx, by = nx + dx, ny + dy
            bt = tile(s, bx, by)
            if bt in "#GoE" or (bx, by) == (mx, my) or (bx, by) in s["blocks"]:
                return None
            i = s["blocks"].index((nx, ny))
            if bt == "g":  # a pushed block shatters floor glass and falls through: block lost, pit left
                s["blocks"] = s["blocks"][:i] + ((-1, -1),) + s["blocks"][i + 1:]
                s["landing_broken"] = True
            else:
                s["blocks"] = s["blocks"][:i] + ((bx, by),) + s["blocks"][i + 1:]
        s["player"] = (nx, ny)
        leave(s, px, py)
    # ---- 2. the obstacle, if its phase is due
    x, y, dr, ph = s["mob"]
    if s["tick"] % PERIOD == ph:
        dx, dy = DIRS[dr]
        nx, ny = x + dx, y + dy
        t = tile(s, nx, ny)
        blocked = t in "#oE" or (nx, ny) in s["blocks"]
        if not blocked and (nx, ny) == s["player"]:
            sx, sy = nx + dx, ny + dy
            st = tile(s, sx, sy)
            if st in "#GoE" or (sx, sy) in s["blocks"]:
                # pinned: it tramples over the player (a hit) and they swap. Its own step-off crack waits for the
                # player to leave that cell.
                s["hurt"] += 1
                s["player"] = (x, y)
                leave(s, nx, ny)
                s["mob"] = (nx, ny, dr, ph)
                s["tick"] += 1
                return s
            s["player"] = (sx, sy)
            leave(s, nx, ny)
        if blocked:
            s["mob"] = (x, y, (dr + 1) % 4, ph)
        else:
            if t == "G":
                s["g_broken"] = True  # it walks through the partition: shards, then floor
            s["mob"] = (nx, ny, dr, ph)
            leave(s, x, y)
    s["tick"] += 1
    return s


def neighbours(s):
    for a in ACTIONS:
        n = step(s, a)
        if n is not None:  # a hit is legal (it costs a heart, not the room); the planner avoids it by cost
            yield a, n


def enc(s):
    """pack a state into one int (the solver's whole state; hits and absolute time don't matter)"""
    c = 0
    for b in s["blocks"]:
        c = c * 71 + (70 if b == (-1, -1) else b[1] * W + b[0])
    x, y, d, _ = s["mob"]
    px, py = s["player"]
    c = ((c * 70 + y * W + x) * 4 + d) * 70 + py * W + px
    return ((c * PERIOD + s["tick"] % PERIOD) * 2 + s["g_broken"]) * 4 + s["landing_broken"] * 2 + s["won"]


def dec(c):
    won, c = c & 1, c >> 1
    lb, c = c & 1, c >> 1
    gb, c = c % 2, c // 2
    tick, c = c % PERIOD, c // PERIOD
    pp, c = c % 70, c // 70
    d, c = c % 4, c // 4
    mp, c = c % 70, c // 70
    blocks = []
    for _ in START["blocks"]:
        b, c = c % 71, c // 71
        blocks.append((-1, -1) if b == 70 else (b % W, b // W))
    return dict(player=(pp % W, pp // W), blocks=tuple(blocks[::-1]), mob=(mp % W, mp // W, d, START["mob"][3]),
                tick=tick, g_broken=bool(gb), landing_broken=bool(lb), won=bool(won), hurt=0)


def key(s):
    return enc(s)


def explore():
    """every state reachable from the start (as packed ints), and the subset that can still win"""
    from array import array
    s0 = initial()
    idx = {enc(s0): 0}
    codes = [enc(s0)]
    src, dst = array("i"), array("i")
    i = 0
    while i < len(codes):
        s = dec(codes[i])
        if not s["won"]:
            for a, n in neighbours(s):
                k = enc(n)
                j = idx.get(k)
                if j is None:
                    j = idx[k] = len(codes)
                    codes.append(k)
                src.append(i)
                dst.append(j)
        i += 1
    n = len(codes)
    # reverse CSR, then a backward sweep from the wins
    cnt = array("i", [0]) * (n + 1)
    for j in dst:
        cnt[j + 1] += 1
    for k in range(n):
        cnt[k + 1] += cnt[k]
    pos = array("i", cnt)
    rev = array("i", [0]) * len(dst)
    for e in range(len(dst)):
        j = dst[e]
        rev[pos[j]] = src[e]
        pos[j] += 1
    alive = bytearray(n)
    q = deque(k for k in range(n) if codes[k] & 1)
    for k in q:
        alive[k] = 1
    while q:
        j = q.popleft()
        for e in range(cnt[j], cnt[j + 1]):
            p = rev[e]
            if not alive[p]:
                alive[p] = 1
                q.append(p)
    seen = idx
    live = {codes[k] for k in range(n) if alive[k]}
    return seen, live


def plan(s0=None):
    """the cleanest winning plan: fewest hits, then fewest ticks, then fewest moves (waits read better than dancing)"""
    s0 = s0 or initial()
    best = {key(s0): 0}
    pq = [(0, 0, "", s0)]
    n_ = 0
    while pq:
        c, _, acts, s = heapq.heappop(pq)
        if s["won"]:
            return acts
        if c > best.get(key(s), 1 << 60):
            continue
        for a, n in neighbours(s):
            nc = c + (n["hurt"] - s["hurt"]) * 10000 + 100 + (a != ".")
            k = key(n)
            if nc < best.get(k, 1 << 60):
                best[k] = nc
                n_ += 1
                heapq.heappush(pq, (nc, n_, acts + a, n))
    return None


def run(actions, s=None):
    s = s or initial()
    out = [s]
    for a in actions:
        n = step(s, a)
        if n is None:
            raise ValueError(f"illegal action {a!r} at tick {s['tick']}")
        s = n
        out.append(s)
    return out


def exit_reachable(s):
    """the runtime dead-state flood fill (3.4): can the player still walk to the exit? It closes only cells that can
    never reopen (walls, pits). Blocks, mobs and an unbroken partition count as open, so the hint can miss a Sokoban
    dead end (the solver scores those offline) but never fires on a room that can still be won."""
    if s["won"]:
        return True
    q = deque([s["player"]])
    seen = {s["player"]}
    while q:
        x, y = q.popleft()
        for dx, dy in DIRS:
            nx, ny = x + dx, y + dy
            t = tile(s, nx, ny)
            if t == "E":
                return True
            if (nx, ny) in seen or t in "#o":
                continue
            seen.add((nx, ny))
            q.append((nx, ny))
    return False


def show(s):
    rows = []
    for y in range(H):
        r = ""
        for x in range(W):
            c = tile(s, x, y)
            if (x, y) == s["player"]:
                c = "P"
            elif (x, y) in s["blocks"]:
                c = "BC"[s["blocks"].index((x, y))]
            elif s["mob"][:2] == (x, y):
                c = "^>v<"[s["mob"][2]]
            r += c
        rows.append(r)
    return "\n".join(rows)


def passive_safe(s, live, cycles=2):
    """waiting from here for `cycles` full mob cycles never leaves the winnable set"""
    for _ in range(cycles * 4 * PERIOD * (W + H)):
        s = step(s, ".")
        if key(s) not in live:
            return False
    return True


def report():
    seen, live = explore()
    dead = [k for k in seen if k not in live]
    s0 = initial()
    # fairness 1: the start is winnable
    ok_start = key(s0) in live
    # fairness 2, the idle rollout ("not before the player acts"): the player only waits; two full mob cycles from
    # the start stay winnable
    idle_ok = passive_safe(s0, live)
    # fairness 3 ("only player mistakes"): the drawn solution is passive-safe at every step, so the player never has
    # to race a mob to keep the room alive; time pressure on the room exists only after a mistake. Those states are
    # counted (rescue windows: winnable, but one more WAIT dooms the room).
    sol_safe = all(passive_safe(st, live) for st in run(SOLUTION) if not st["won"])
    rescue = 0
    for k in live:
        st = dec(k)
        if not st["won"] and key(step(st, ".")) not in live:
            rescue += 1
    best = plan()
    return dict(states=len(seen), live=len(live), dead=len(dead), dead_ratio=round(len(dead) / len(seen), 3),
                earliest_fatal_tick=earliest_dead(live),
                start_winnable=ok_start, idle_rollout_ok=idle_ok, solution_passive_safe=sol_safe,
                rescue_window_states=rescue,
                best_plan=best, best_steps=len(best)), seen, live


def earliest_dead(live):
    """the first tick at which some plan can be in a dead state (BFS depth; 3.4's "unforgiving" score)"""
    s0 = initial()
    frontier, seen, t = {key(s0): s0}, {key(s0)}, 0
    while frontier:
        if any(k not in live for k in frontier):
            return t
        nxt = {}
        for s in frontier.values():
            for a, n in neighbours(s):
                k = key(n)
                if k not in seen:
                    seen.add(k)
                    nxt[k] = n
        frontier, t = nxt, t + 1
    return None


def check_paths(live):
    sol = run(SOLUTION)
    assert sol[-1]["won"] and sol[-1]["hurt"] == 0, "the drawn solution must win without a hit"
    assert all(key(s) in live for s in sol), "every state on the drawn solution must be winnable"
    bad = run(MISTAKE)
    first_dead = next(i for i, s in enumerate(bad) if key(s) not in live)
    end = bad[-1]
    assert end["landing_broken"] and not end["won"], "the mistake must end with the landing broken"
    assert not exit_reachable(end), "the runtime flood fill must flag it: THE ROOM FORGETS"
    assert all(exit_reachable(s) for s in bad if key(s) in live), "the hint never fires on a winnable state"
    broke = next(i for i, s in enumerate(bad) if s["landing_broken"])
    assert bad[broke - 1]["mob"][:2] == LANDING, "the wanderer, not the player, broke the landing"
    mistake_tick = len(SOLUTION[:16]) + 12  # the push that puts the CRT in the corridor
    return dict(solution_ticks=len(SOLUTION), mistake_push_tick=mistake_tick, mistake_dead_from_tick=first_dead,
                landing_broken_at_tick=broke,
                flood_fill_flags_at_tick=next(i for i, s in enumerate(bad) if not exit_reachable(s)))


def main():
    rep, seen, live = report()
    for k, v in rep.items():
        print(f"{k:26s} {v}")
    assert rep["start_winnable"] and rep["idle_rollout_ok"] and rep["solution_passive_safe"]
    for k, v in check_paths(live).items():
        print(f"{k:26s} {v}")
    return rep


if __name__ == "__main__":
    main()
