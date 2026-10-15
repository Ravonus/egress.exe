"""The cartridge's idle scheduler (src/core/eg_idle.c via build/host/test_idle) against proto/anim/heirs.py
idle_plan(): the same segments on the same ticks for seeds x trait sets x contexts.

    .venv/bin/python tools/test_idle.py build/host/test_idle
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "proto/anim"))
import heirs as HA  # noqa: E402

SEG = ["idle"] + HA.FIDGETS + ["fidget_twitch", "fidget_bounce", "long_sit", "long_sit_twitch", "long_doze", "long_lie",
                               "ctx_ready", "ctx_cool", "ctx_lowhp"]
TRAITS = [("INSOMNIAC", 1), ("HEAVY", 2), ("GLITCHED", 4), ("SMALL", 8)]


def main():
    exe = sys.argv[1]
    cases = fails = 0
    for seed in range(0, 4000, 97):
        for tmask in (0, 1, 2, 4, 8, 3, 9, 15):
            for ctx in (0, 1, 2, 3):
                traits = [n for n, b in TRAITS if tmask & b]
                context = "fight" if ctx & 1 else None
                until = 2400
                want = HA.idle_plan(seed, traits, until=until, context=context, hp_low=bool(ctx & 2))
                got = subprocess.run([exe, str(seed), str(tmask), str(ctx), str(until)], capture_output=True, text=True,
                                     check=True).stdout.split("\n")
                got = [tuple(int(x) for x in ln.split()) for ln in got if ln.strip()]
                got = [(a, b, SEG[s]) for a, b, s in got]
                cases += 1
                if got != want:
                    fails += 1
                    if fails <= 3:
                        print("MISMATCH seed", seed, traits, ctx)
                        print(" want", want[:8])
                        print(" got ", got[:8])
    print(f"test_idle: {cases - fails} of {cases} plans match heirs.py idle_plan")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
