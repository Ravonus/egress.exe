"""The death record's words (docs/redesign.md 12b.4 and 9.6), shared by death_mock.py and tree_mock.py.

Design-stage data the cartridge would carry (~1.2 KB of strings), plus the 3-byte cause record and the epitaph id
exactly as the tree node stores them, so the card, the tree, a ghost and a talker all print the same lines.

    cause bytes (node offsets 17..19)
      17 VERB    bits 0-3 how (HOW), bits 4-5 owner (OWNER), bits 6-7 killer class (CLASS)
      18 KILLER  class mob: bits 0-3 the mob's noun (MOB_NOUN), bits 4-7 its adjective (MOB_ADJ; 0 none)
                 other classes: the noun id in that class's table (OBJECT, REACTION, BOSS)
      19 TWIST   bits 0-2 the context clause (CTX; 0 none), bits 3-7 its value (item id, seconds, boss phase)
    The relative clause of a SELF death ("A CRT SHE PUSHED HERSELF") is not stored: owner SELF + an object implies
    it, and the verb is the object's own (OBJECT[i][1]).

    epitaph id (node offset 20)
      bits 0-2 the pool: the truth axis that leaned hardest at death (AXES) x 2 + (1 if it leaned negative)
      bits 3-4 the line in that pool
      bits 5-6 the keepsake that fills the line's slot: 0 the line's own noun, 1 the best feat's, 2/3 the first or
               second expressed trait's
      bit 7    spare
"""
from __future__ import annotations

# ------------------------------------------------------------------------------------------------ cause
HOW = ["HIT", "CRUSH", "BURN", "STEAM", "FREEZE", "SHORT", "OVERGROW", "GLITCH", "SHATTER", "DROWN", "MUD", "BEAM",
       "LAVA", "VOID"]  # 14 of 16 (4 bits); HIT takes the biome's verb
VERB = {  # damage/reaction -> past participle (<= 11 chars)
    "CRUSH": "CRUSHED", "BURN": "COOKED", "STEAM": "STEAMED", "FREEZE": "FROZEN", "SHORT": "SHORTED OUT",
    "OVERGROW": "OVERGROWN", "GLITCH": "UNRENDERED", "SHATTER": "SHATTERED", "DROWN": "DROWNED", "MUD": "STUCK",
    "BEAM": "ERASED", "LAVA": "MELTED",
}
BIOME_HIT = {  # a plain hit takes the biome's verb, so the line sounds like where it happened
    "OFFICE": "FILED AWAY", "VENTS": "VENTED", "ATRIUM": "DUNKED", "SERVER": "DEFRAGGED", "MALL": "RUNG UP",
    "GARDEN": "COMPOSTED", "ROOFTOP": "BLOWN OFF", "GLITCH": "DELETED",
}
BIOME_ALIAS = {"ROOF": "ROOFTOP", "CORE": "GLITCH"}  # the tree mock's short biome names
OWNER = ["SELF", "MOB", "BOSS", "ROOM"]
CLASS = ["mob", "object", "reaction", "boss"]
MOB_NOUN = ["INTERN", "TOASTER", "LAMP", "MOTH", "STAPLER", "FAN", "PRINTER", "KETTLE", "CLOCK", "FERN", "CART",
            "MOP", "BINDER", "PHONE", "BULB", "CHAIR"]
MOB_ADJ = ["", "DAMP", "RIME", "SORRY", "NOCLIP", "WARM", "LOUD", "TIRED", "SHINY", "OVERDUE", "SPARE", "LOOSE",
           "HUMMING", "LATE", "NEW", "OLD"]
OBJECT = [("CRT", "PUSHED"), ("PUDDLE", "POURED"), ("SPROUT", "PLANTED"), ("LEAK", "DUG"), ("CRATE", "PUSHED"),
          ("FILING CABINET", "PUSHED"), ("KETTLE", "BREWED"), ("BENCH", "MADE"), ("FLARE", "LIT"), ("MUG", "THREW"),
          ("VENT", "OPENED"), ("STEAM PIPE", "OPENED")]
REACTION = ["STEAM", "ORB", "FIRE", "ICE", "SPARK", "MUD", "VINE", "STATIC"]
BOSS = ["OLD MONITOR", "THE COPIER", "NIGHT SHIFT", "THE ATRIUM", "FLOOR 9"]
ITEM = ["MUG", "KEY", "BULB", "FLASK", "FERN", "BADGE", "CHARGER", "BOX"]
PRON = {0: ("THEY", "THEIR", "THEMSELF", "THEM"), 1: ("HE", "HIS", "HIMSELF", "HIM"),
        2: ("SHE", "HER", "HERSELF", "HER")}
CTX = [None, "ONE HIT SHORT", "MID-{DODGE}", "{ITEM} IN HAND", "{SECS}S IN", "PHASE {PHASE}", "LAST HEART"]


def pack_cause(rec):
    """rec: dict(how, owner, cls, noun, adj, ctx, val) as indices -> 3 bytes"""
    b0 = (rec["how"] & 15) | ((rec["owner"] & 3) << 4) | ((rec["cls"] & 3) << 6)
    b1 = ((rec["noun"] & 15) | ((rec.get("adj", 0) & 15) << 4)) if rec["cls"] == 0 else rec["noun"] & 255
    b2 = (rec.get("ctx", 0) & 7) | ((rec.get("val", 0) & 31) << 3)
    return bytes((b0, b1, b2))


def unpack_cause(b):
    cls = b[0] >> 6
    rec = {"how": b[0] & 15, "owner": (b[0] >> 4) & 3, "cls": cls, "ctx": b[2] & 7, "val": b[2] >> 3}
    if cls == 0:
        rec["noun"], rec["adj"] = b[1] & 15, b[1] >> 4
    else:
        rec["noun"], rec["adj"] = b[1], 0
    return rec


def killer_name(rec):
    cls = CLASS[rec["cls"]]
    if cls == "mob":
        adj = MOB_ADJ[rec["adj"]]
        return (adj + " " if adj else "") + MOB_NOUN[rec["noun"]]
    if cls == "object":
        return OBJECT[rec["noun"]][0]
    if cls == "reaction":
        return REACTION[rec["noun"]]
    return BOSS[rec["noun"]]


def a_an(noun):
    return ("AN " if noun[0] in "AEIOU" and not noun.startswith("U") else "A ") + noun


def split2(sent, n=18):
    """the sentence on 1 line, or broken at the word boundary that keeps both lines <= n (most balanced)"""
    if len(sent) <= n:
        return [sent]
    best = None
    for i, ch in enumerate(sent):
        if ch == " " and len(sent[:i]) <= n and len(sent[i + 1:]) <= n:
            k = abs(len(sent[:i]) - len(sent[i + 1:]))
            if best is None or k < best[0]:
                best = (k, [sent[:i], sent[i + 1:]])
    return best[1] if best else None


def cause_lines(b, look, biome, floor, dodge="ROLL"):
    """the 3 cause bytes + what the node already holds (look gene, biome, floor, dodge) -> 1..2 lines of <= 18.
       CAUSE  := VERB BY KILLER [TWIST]
       KILLER := POSS OWN NOUN    when the owner is SELF and the killer is a reaction or projectile ("HER OWN STEAM")
               | A/AN NOUN | BOSS NAME
       TWIST  := PRON OWN_VERB REFLEX | PRON OWN_VERB     when the owner is SELF and the killer is an object
               | ", " CONTEXT
       special: the void = "LET GO OF / THE FLOOR ON Fn". Tried longest-first; the twist drops if nothing fits."""
    rec = unpack_cause(b)
    he, his, self_, _ = PRON[look]
    how = HOW[rec["how"]]
    if how == "VOID":
        return ["LET GO OF", f"THE FLOOR ON F{floor}"]
    verb = VERB.get(how) or BIOME_HIT[BIOME_ALIAS.get(biome, biome)]
    cls, own = CLASS[rec["cls"]], OWNER[rec["owner"]] == "SELF"
    noun = killer_name(rec)
    kp = f"{his} OWN {noun}" if own and cls == "reaction" else (noun if cls == "boss" else a_an(noun))
    twists = []
    if own and cls == "object":
        ov = OBJECT[rec["noun"]][1]
        twists += [f" {he} {ov} {self_}", f" {he} {ov}"]  # a relative clause: "A CRT SHE PUSHED HERSELF"
    if rec["ctx"]:
        twists.append(", " + CTX[rec["ctx"]].format(DODGE=dodge, ITEM=ITEM[rec["val"] % len(ITEM)],
                                                      SECS=rec["val"], PHASE=rec["val"]))
    twists.append("")
    for tw in twists:
        lines = split2(f"{verb} BY {kp}{tw}")
        if lines:
            return lines
        l1, l2 = f"{verb} BY {kp}", tw.lstrip(", ").strip()
        if tw and len(l1) <= 18 and len(l2) <= 18:
            return [l1, l2]
    return [verb, noun[:18]]


def rec(how, owner, cls, noun, adj=0, ctx=0, val=0):
    """a cause record by names (for mocks and examples)"""
    c = CLASS.index(cls)
    tables = {"mob": MOB_NOUN, "object": [o[0] for o in OBJECT], "reaction": REACTION, "boss": BOSS}
    return pack_cause(dict(how=HOW.index(how), owner=OWNER.index(owner), cls=c,
                           noun=tables[cls].index(noun) if isinstance(noun, str) else noun,
                           adj=MOB_ADJ.index(adj) if isinstance(adj, str) else adj,
                           ctx=CTX.index(ctx) if isinstance(ctx, str) or ctx is None else ctx, val=val))


# (record, look, biome, floor) -> the 12b table
GRAMMAR_EXAMPLES = [
    (rec("CRUSH", "SELF", "object", "CRT"), 2, "OFFICE", 3),
    (rec("FREEZE", "SELF", "reaction", "STEAM"), 2, "VENTS", 2),
    (rec("SHORT", "SELF", "object", "PUDDLE"), 1, "SERVER", 4),
    (rec("HIT", "MOB", "mob", "INTERN", "DAMP", "MID-{DODGE}"), 0, "VENTS", 2),
    (rec("BURN", "SELF", "reaction", "ORB"), 1, "MALL", 5),
    (rec("OVERGROW", "SELF", "object", "SPROUT"), 2, "GARDEN", 6),
    (rec("BURN", "MOB", "mob", "LAMP", "SORRY", "LAST HEART"), 2, "SERVER", 4),
    (rec("BEAM", "BOSS", "boss", "OLD MONITOR", 0, "PHASE {PHASE}", 3), 0, "SERVER", 4),
    (rec("DROWN", "ROOM", "object", "LEAK", 0, "{ITEM} IN HAND", 0), 1, "ATRIUM", 3),
    (rec("VOID", "ROOM", "object", 0), 2, "ROOFTOP", 2),
    (rec("HIT", "MOB", "mob", "TOASTER", "RIME", "{SECS}S IN", 4), 0, "MALL", 5),
    (rec("GLITCH", "MOB", "mob", "MOTH", "NOCLIP"), 1, "GLITCH", 8),
]

# ------------------------------------------------------------------------------------------------ epitaph
AXES = ["SELF", "CAUSE", "CULPRIT", "REVERSIBLE"]
# The axis that leaned hardest at death and its sign pick the pool (the meaning); the seed picks the line; a {K}
# slot takes a keepsake noun from the best feat or an expressed trait (the flavour). Never a reveal or a pun.
# {SHE} {HER} (possessive) {HER_O} (object) are rewritten for the heir. Every filled line is <= 18.
EPITAPH = {
    ("SELF", +1): ["THE {K:CHAIR} WAS WARM", "SAME {K:DESK}, AGAIN", "THE {K:BADGE} FIT", "{SHE} KNEW THE WAY"],
    ("SELF", -1): ["A STRANGER'S {K:SHOES}", "NOBODY KNEW {HER_O}", "THE NAME WAS NEW"],
    ("CAUSE", +1): ["SOMEONE MIXED THIS", "IT WAS PUT HERE", "THE {K:BENCH} WAS WARM"],
    ("CAUSE", -1): ["THE FLOOR GAVE UP", "ALWAYS LOOSE", "THE BUILDING SAGS"],
    ("CULPRIT", +1): ["{SHE} HAD THE {K:KEY}", "{HER} HANDS, AGAIN", "{SHE} OPENED IT"],
    ("CULPRIT", -1): ["THEY WROTE IT DOWN", "SOMEONE WATCHED", "THE DOOR WAS DRAWN"],
    ("REVERSIBLE", +1): ["NOT YET. NOT HERE", "IT CAN BE PUT BACK", "THE STAIRS WAIT"],
    ("REVERSIBLE", -1): ["SOME DOORS STAY", "THE {K:STAIRS} WENT", "NOTHING CAME BACK"],
}
FEAT_KEEP = {"BOSS": "CROWN", "ELITE": "MEDAL", "RECIPE": "FLASK", "TREASURE": "KEY", "ESCORT": "GLOVE"}
TRAIT_KEEP = {"HEAVY": "BOOT", "SMALL": "VENT", "ECHO": "HALL", "GLITCHED": "PIXEL", "INSOMNIAC": "LAMP",
              "REMEMBERS": "NOTE", "DAMP": "COAT", "LOUD": "BELL", "COLOURBLIND": "SIGN", "ALLERGIC": "MASK"}


def _fill(line, look, keep):
    he, his, _, him = PRON[look]
    out = line.replace("{SHE}", he).replace("{HER}", his).replace("{HER_O}", him)
    if "{K:" in out:
        a = out.index("{K:")
        b = out.index("}", a)
        out = out[:a] + (keep or out[a + 3:b]) + out[b + 1:]
    return out


def choose_epitaph(lean, traits, feat, seed):
    """lean: {axis: signed lean}; traits: expressed traits; feat: best-feat type or None. Returns the epitaph id.
    With a keepsake, the pool's slotted lines count twice in the draw; the feat's keepsake beats the traits'."""
    ax = max(AXES, key=lambda a: abs(lean[a]))
    neg = lean[ax] < 0
    pool = EPITAPH[(ax, -1 if neg else 1)]
    src = 1 if feat in FEAT_KEEP else (2 + ((seed >> 4) & 1 if len(traits) > 1 else 0) if traits else 0)
    cands = list(range(len(pool))) + ([i for i, ln in enumerate(pool) if "{K:" in ln] if src else [])
    line = cands[seed % len(cands)]
    if "{K:" not in pool[line]:
        src = 0
    return (AXES.index(ax) * 2 + int(neg)) | (line << 3) | (src << 5)


def epitaph_text(eid, look, traits, feat):
    ax, neg = AXES[(eid & 7) >> 1], eid & 1
    line = EPITAPH[(ax, -1 if neg else 1)][(eid >> 3) & 3]
    src = (eid >> 5) & 3
    keep = None
    if src == 1:
        keep = FEAT_KEEP.get(feat)
    elif src >= 2 and len(traits) > src - 2:
        keep = TRAIT_KEEP[traits[src - 2]]
    return _fill(line, look, keep)


def check_tables():
    """every filled epitaph fits 18; every cause record round-trips"""
    for (ax, sg), pool in EPITAPH.items():
        for ln in pool:
            for look in (0, 1, 2):
                for keep in [None] + list(FEAT_KEEP.values()) + list(TRAIT_KEEP.values()):
                    s = _fill(ln, look, keep)
                    assert len(s) <= 18, (s, len(s))
    for b, *_ in GRAMMAR_EXAMPLES:
        assert pack_cause(unpack_cause(b)) == b
    for i in range(1 << 16):
        b = bytes((i & 255, (i >> 8) & 255, (i * 37) & 255))
        assert pack_cause(unpack_cause(b)) == b
    return True
