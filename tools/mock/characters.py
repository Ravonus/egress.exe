"""docs/shots/characters.png: before (M2) / CRUCIBLE reference (style only) / after (EGRESS, shared face generator)."""
import os
import sys

from egkit import *  # noqa: F403
import mocks

E = ROOT / "docs/shots"
CAPS = os.environ.get("CRU_CAPS", "build/cru_caps")
im = Image.new("RGB", (480, 3 * 160 + 8), (20, 14, 46))
text(im, 0, 0, "BEFORE: EGRESS M2", PINK)
for i, f in enumerate(["13_landing_talk.png", "07_combat_lash.png"]):
    im.paste(Image.open(E / f).convert("RGB"), (i * 160, 8))
im.paste(Image.open(E / "art_faces.png").convert("RGB").crop((0, 0, 160, 128)), (320, 8))
text(im, 0, 160, "CRUCIBLE: STYLE REFERENCE", PINK)
for i, f in enumerate(["cruflow/free-free-talk0-talk.png", "cruflow/story-story-fight4-ch1-fight.png"]):
    im.paste(Image.open(f"{CAPS}/{f}").convert("RGB"), (i * 160, 168))
x, y = 320, 168
for k, (fa, se) in enumerate([(4, 7), (4, 3), (5, 15), (1, 6), (3, 3), (0, 12)]):
    fc, _ = face(fa, se, (46, 30, 92))
    im.paste(fc, (x + (k % 3) * 53, y + (k // 3) * 50))
text(im, x, y + 104, "SHARED FACE CODE", WHITE)
text(im, 0, 320, "AFTER: EGRESS", PINK)
im.paste(mocks.heir(), (0, 328))
im.paste(mocks.combat(), (160, 328))
im.paste(mocks.room(), (320, 328))
im.resize((960, im.height * 2), Image.NEAREST).save(E / "characters.png")
