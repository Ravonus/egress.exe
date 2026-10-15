"""Run the sky cost probe in PyBoy (CGB, headless), report cycles/ms per call and check the ROM's positions against
the host build of the same C.

    .venv/bin/python proto/sky/rom/measure.py      # after proto/sky/rom/build.sh; writes proto/sky/out/cost.json
"""

from __future__ import annotations

import json
import re
import subprocess
from pathlib import Path

from pyboy import PyBoy

HERE = Path(__file__).resolve().parent.parent
ROM = HERE / "build" / "sky_probe.gbc"
CPU = 8388608  # CGB double speed
TICK = 16  # CPU clocks per timer tick at TAC 262144 Hz in double speed
FRAME = 140448  # CPU clocks per frame in double speed
NAMES = ["sky_positions (all 10)", "sky_read in range", "sky_read outside (fallback)", "sky_find phase (late block)",
         "sky_find Mercury ingress (late block)", "natal chart + 15 aspects", "transit scan (now + 49 aspects)", "sky_mark"]


def main() -> int:
    noi = ROM.with_suffix(".noi").read_text(encoding="latin-1")
    sym = {m[1]: int(m[2], 16) for m in re.finditer(r"DEF (\S+) 0x([0-9A-Fa-f]+)", noi)}
    pb = PyBoy(str(ROM), window="null", cgb=True, sound_emulated=False, log_level="ERROR")
    pb.set_emulation_speed(0)
    mem = pb.memory
    for f in range(3000):
        pb.tick(1, False)
        if mem[sym["_done"]]:
            break
    else:
        raise SystemExit("probe did not finish")
    u16 = lambda a: mem[a] | (mem[a + 1] << 8)  # noqa: E731
    ticks = [u16(sym["_res_ticks"] + 4 * i) | (u16(sym["_res_ticks"] + 4 * i + 2) << 16) for i in range(len(NAMES))]
    pos = [u16(sym["_res_pos"] + 2 * i) for i in range(10)]
    host = subprocess.run([str(HERE / "out" / "host_dump"), "pos", "9784", "9785", "1440", "48"], capture_output=True, text=True,
                          check=True).stdout.split()
    hp = [int(x) for x in host[2:12]]
    pb.stop(save=False)
    rows = []
    print("| call | CPU clocks | ms (double speed) | frames |")
    print("|---|---|---|---|")
    for n, t in zip(NAMES, ticks):
        c = t * TICK
        rows.append(dict(call=n, clocks=c, ms=round(c / CPU * 1000, 2), frames=round(c / FRAME, 2)))
        print(f"| {n} | {c:,} | {c / CPU * 1000:.2f} | {c / FRAME:.2f} |")
    same = pos == hp
    print(f"ROM positions == host positions: {same}  ({pos})")
    use = subprocess.run([str(Path.home() / ".cache/gbdk-4.5.0/bin/romusage"), str(ROM.with_suffix(".map")), "-g"],
                         capture_output=True, text=True).stdout
    (HERE / "out" / "cost.json").write_text(json.dumps(dict(calls=rows, rom_matches_host=same, romusage=use), indent=1))
    print(use)
    return 0 if same else 1


if __name__ == "__main__":
    raise SystemExit(main())
