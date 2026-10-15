# MOBGEN: mob looks from parts

Design-stage mock: `tools/mock/mobgen.py` (run it from `tools/mock`: `python3 mobgen.py`, or `python3 mobgen.py count`).
It writes `docs/shots/mobs/family_<name>.png` (64 looks per family at 3x, on alternating light floor and dark void
cells) and `docs/shots/mock_mobs.png`. A look is how a mob appears on screen. What it does comes from the mob matrix
(`proto/matrix/mx_mob.c`). The two meet only through the tag, the defence, the mobility hint and the elite affixes.

## Port shape (C)

- **The look.** A struct of small ints: `fam body size limb topper eyes style mouth pattern acc(bitmask) accent
  hue quirk tag defence adj noun pseed`, plus `affix[2]` and `aura` for elites.
- **Drawing order.** Fields are drawn from xorshift32 in that fixed order. Each one is picked from the list of
  choices that the `allowed_*` rules leave valid for the fields already drawn. The face is drawn as one triple,
  `(eyes, style, mouth)`, from `valid_faces()`, which keeps only faces whose layout fits the body row (`face_layout`).
- **Part data.** Bodies are primitives in a 16-unit box: ellipse, clipped ellipse, rounded rect, polygon, cut rect
  and cut ellipse. All raster tests use integer, doubled coordinates. Every other part is a pixel template
  (`'.'` = clear, digits = palette codes) with an anchor.
- **Palette codes.** The canvas holds one code per pixel:

  | Codes | Palette | Colours |
  |---|---|---|
  | `1 2 3` | **A**, the body (one OBJ palette) | dark/outline, mid, light |
  | `4 5 6` | **B**, the overlay sprite | ink, biome accent, tag cue (white when untagged) |
  | `7 8 9` | **E**, the elite palette (OBJ palette 7) | ink, affix 1, affix 2 |

  Each sprite therefore uses at most 3 colours plus transparent per palette. Accessories and tag tells are in B.
  Defence tells and toppers are in A, except the cap and the cable, which are in B. Elite parts are in E.
- **Shading.** A 4x4 Bayer dither runs from mid to light toward the top-left, with a dithered dark rim at the
  bottom. The outline is 1px inside the silhouette, plus seams where a limb or wing meets the body.
- **Palette building.**
  1. Pick one of the family's body ramps (OFFICE has 8: cream, mint filing cabinet, sticky-note pink, sticky-note
     yellow, carpet teal-blue, lavender CRT, coral, toner magenta; MALL has 5; the others have 4). Mix it with the
     element ramp: 6/16 on mid, 4/16 on light, 6/16 on dark.
  2. Rotate the hue by an integer channel mix, in 5 steps from -2 to +2.
  3. Quantise to RGB555.
  4. Nudge with bounded loops: darken the outline until contrast against both floor tones is at least 4.5; lift
     the mid until contrast against the void is at least 4; rotate or lift the mid away from the floor's lilac
     (RGB distance at least 48); separate light from mid (contrast at least 1.3).
- **Sprite budget.** S is 12px (2x2 tiles), M 16px (2x2), L 24px (3x3), XL 32px (4x4). In 8x16 mode an XL mob is
  4 OBJs wide on a line. Keep XL to one per screen, and leave the overlay and elite parts on separate OBJs.

## Parts

| Axis | Choices |
|---|---|
| Families (4 bodies each) | **OFFICE**: clerk blob, filing cabinet, desk lamp (dome shade), fax thing · **VENTS**: moth, dust worm (segmented), grille crab, dust ghost (round top) · **ATRIUM**: droplet, jelly, puddle frog, cloud puff · **SERVER**: rack, eye orb, block bug, cable snake · **MALL**: soda cup, burger, shop bag, dumpling · **GARDEN**: bulb, mushroom, flower head, topiary · **ROOFTOP**: pigeon, kite, water tower, sky dish · **GLITCH**: null cube, shard, static blob (seeded edge nibbles), hex face |
| Size | S 12 (swarm), M 16, L 24, XL 32 |
| Topper | none, antennae, horns, tuft, cap, bun, halo, sprout, cable, fin (the fin sits off-centre). Small and big templates. Per-body bans; S allows only none, antennae, tuft, sprout, halo and horns. |
| Limbs | none, 2/4/6 legs, arms, tentacles, wings, wheels, hover shadow, turret mount. Each body lists what it allows; S drops 4/6 legs, arms and wheels. Mobility hints: TURRET forces the mount, FLYER keeps wings or hover, SWARM forces size S. |
| Eyes | 1 to 4 eyes (S at most 2; M at most 3, and at most 2 if the style is ring), in a second row when one row doesn't fit. Styles: dot, ring, slit, screen, sleepy. SERVER uses lit LED/screen styles only (dot, slit, screen in B colours with an ink frame). |
| Mouth | none, line, teeth zigzag, O, smile, grille. S has no teeth or grille. |
| Pattern | none, stripes, spots, circuit, cracks, dither band. S allows only none, stripes and spots. The circuit is horizontal traces with a single downward stub, all in one orientation, so no rotational motifs. |
| Accessories (B) | OFFICE: lanyard, tie, sticky note, coffee stain · VENTS: dust, cable · ATRIUM: water line, bubbles · SERVER: cables, LEDs · MALL: price tag, bag · GARDEN: vines, flower · ROOFTOP: scarf, antenna · GLITCH: row tear (a 1px gap), dead pixels. At most 1 at S, 2 at M, 3 at L/XL, and at least one item from the family cue set (OFFICE needs a lanyard, tie or sticky note). 2 accent colours per biome. |
| Palette | body ramp (8 OFFICE, 5 MALL, 4 elsewhere) x 11 element ramps (10 tags + none) x 5 hue shifts. The family cue (accessories, server LED eyes) is the link between looks, not the body colour. |
| Idle quirk (data) | twitch, hover bob, flicker, breathe, eye roll. Each one is a 2-frame idle pose. |

### Tag and defence tells

Each tell is fixed and drawn the same way on every look, in cue colour B3:

| Tag or defence | Tell |
|---|---|
| COLD | frost-blue pixels every 2px along the top rim, plus icicles |
| WET | blue drips below, plus falling drops |
| HOT | ember above the head |
| ALIVE | a green leaf at the top-left |
| MAGIC | a floating rune diamond at the top-right |
| GLITCH | 2 scanline bands shifted right 2px, with magenta at the gap |
| SHOCK | a bolt at the side |
| SHINY | a glint on the body |
| STONE | pebbles at the feet |
| AIRY | wisps at the sides |
| ARMOURED (A palette) | a light plate with dark rim and rivets on the front, below the mouth |
| SHIELDED (A palette) | a dashed light and dark ring around the whole sprite |

These rules keep the tells unambiguous:
- Atrium uses a water line instead of drips, so drips always mean WET.
- Garden vines and the flower use the accent colour, never the leaf green, so a green leaf always means ALIVE.
- The glitch row tear is a gap, not a shift, so a shifted band always means the GLITCH tag.
- The topper halo is an A-palette disc and the shield is a dashed ring, so neither reads as an elite.

### Animation

States are the same parts posed differently. A pose sets: `dx, dy`, a squash (the top half moves down), a
leg/wing phase, the eye state (open, roll or closed), a mouth swap, a palette swap (normal, tell, hurt, death) and a
Bayer dissolve.

| State | Pose |
|---|---|
| Idle | 2 frames, set by the quirk |
| Move | 2 frames: leg lift and bob |
| Tell | crouch, lean back, eyes roll; palette flashes to yellow and white; `!` above the head |
| Attack | 4px lunge, O mouth |
| Hurt | white palette flash, eyes shut |
| Death | 2 frames: palette fades to the void, 40% then 80% Bayer dissolve, puff |

## Elites

Any look can be elite: any family, size or mobility, including swarm members, turrets and mimics. An elite has 1 or
2 affixes and is drawn in palette 7 = `[ink, affix 1 colour, affix 2 colour]`. With one affix, the third colour is
a lighter shade of the first.

- **Outline.** A 1px ring outside the whole silhouette. Its colour alternates between E2 and E3 each phase, which
  is the palette-cycle pulse.
- **Second glow ring.** A dithered second ring is added when the mob is XL (it can't grow, so it glows instead) or
  when the elite has the element aura.
- **Size bump.** S→M, M→L, L→XL. XL stays XL and gets the second glow ring.
- **Crown.** One icon per affix above the head: SHIELDED is a badge, VAMPIRIC fangs, SPLITTING twin cells, HASTED
  `>>`, AURA a drop in the element colour, TELEPORT corner brackets, COLLAPSE falling blocks. The crown bobs 1px
  per phase.
- **Particles.** 4 particles (S/M) or 6 (L/XL), each 2x2 in the affix colours, orbiting on a 16-step sine table.
- **Name tag.** An `ELITE` chip, then the affix words (`SHIELDED, VAMPIRIC, SPLITTING, HASTED, <ELEMENT> AURA,
  BLINKING, CRUMBLING`), then the name. Names are an adjective plus a family noun, for example DAMP INTERN or
  SORRY ROOT.
- **Swarm elites.** These get the glow, the particles and the size bump only: no crown and no name tag.
- **Mimic elites.** These stay the egkit object (`obj('MUG')` or `obj('CRT')`). The only giveaway is a 3-pixel
  shimmer travelling around the rim.
- **Affix rules** (from `mx_af_rule`):
  - SPLITTING: not on swarms or S.
  - TELEPORT: not on armoured mobs or turrets.
  - COLLAPSE: ground movers only (no hover, wings or mount).
  - SHIELDED: not on mobs that are already ring-shielded.

## Counts (product of independent choices, pruned)

For each body, size and limb, the count is: toppers × valid faces (eyes × style × mouth that fit) × patterns ×
accessory sets × 2 accents × body ramps × 5 hue shifts. The "with tag+defence" column multiplies that by 11 tags × (2 or 3)
defences.

| Family | Looks | With tag+defence cues |
|---|---:|---:|
| OFFICE | 422,336,640 | 13,922,853,120 |
| VENTS | 22,914,000 | 753,817,680 |
| ATRIUM | 27,381,600 | 901,058,400 |
| SERVER | 34,475,760 | 1,132,457,040 |
| MALL | 49,493,700 | 1,628,540,100 |
| GARDEN | 30,332,400 | 997,624,320 |
| ROOFTOP | 29,774,880 | 978,737,760 |
| GLITCH | 61,303,680 | 2,016,622,080 |
| **Total** | **678,012,660** | **22,331,710,500** |

OFFICE is larger because it has 4 accessories (up to 13 valid sets against 3 for the other families) and 8 body ramps, and its bodies allow
the most limbs.

Not included in the table:
- Idle quirks multiply each count by 5, but they only show when animated.
- Elite affix sets: 16 single affixes (6 affixes, with AURA counted once per element: 10) and 75 pairs.

## Content review (2026-10-14)

Every sheet was checked by eye. These looks were fixed:
- A centred fin on round domes, and the first lamp shade, which was a pointed cone, both made a pointed peak above
  two eyes that could read as a hood. The lamp shade is now a dome and the fin sits off-centre.
- A plain lamp bar under a topper could make a cross. The bar is now a dome.
- The diagonal worm could read as phallic. It is now a segmented, horizontal caterpillar.
- The fries in the fry box read as fingers. It is now a dumpling.
- The kite has no cross spars, the sparkle is a small equal plus and the circuit pattern has one orientation only.
- There are no human figures or likenesses.

Per-sheet stats:

| Measure | Result |
|---|---|
| Hue bins over 3% of saturated mob pixels | 5–9 per sheet |
| Circular hue standard deviation | 41–90° |
| Min outline contrast against the floor | 4.51 |
| Min body contrast against the void | 4.49 (floor rule: 4.0) |
| Min body RGB distance from the floor | 48 |
| Body-pixel hue rule (at least 5 bins with 3% or more, no bin over 35%) | passes on every sheet: 6–10 bins, largest bin 21–33% |

None of the sheets is monotone.
