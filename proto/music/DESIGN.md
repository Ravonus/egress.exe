# EGRESS music and SFX: one semi-generative system (prototype)

Status: a design with a working proof. `egm.c` is a real cartridge driver, and `compose.py` writes its data. A test
ROM plays both on an emulated CGB, and the previews in `docs/audio/` are recordings of that ROM. Nothing here is wired
into `src/` yet.

## What we keep from CRUCIBLE, and what changes

CRUCIBLE's driver (`crucible_sound.c` with format v4 in `keel_music.h`) is a register-level groove tracker. Each
channel has 16-row patterns, and songs are sections with seeded alternates. Effects take over pulse 1 and noise, and
the music leaves a held channel alone. Effects are always centred, and every note's start level is scaled by the
MUSIC volume. Song changes fade out, then the next song starts on a beat. EGRESS keeps those rules. It changes three
things:
- notes are chord-relative, so the harmony is data rather than baked pitches;
- the arrangement is made of layers, and game state switches them on and off;
- effects are composed from the music's current key and chord.

## Moods

| mood | BPM | mode | heard at intensity 0 / 1 / 2 / 3 | character |
|---|---|---|---|---|
| calm landing | 75 | ionian | pad+bass / +lead / +brush kit, the pad becomes an 8th arpeggio | swells, long lead notes, a 100 % sine bass |
| uneasy explore | 90 | dorian | pad+bass / all, with a 3+3+2 arpeggio / busier kit, walking bass | questions in the lead, offbeat ticks |
| tension | 82 | aeolian | pad+heartbeat bass / +lead and thumps / ticking arpeggio | sus pads; the lead hardly speaks |
| combat | 112 | aeolian | all / all / 16th arpeggio, busier kit | plucked arpeggio, octave-pumping bass |
| boss | 128 | phrygian | all / all / 16th bass and arpeggio, rolls | the flat second; driving, heroic lead |
| collapse / chase | 149 | aeolian | never any bass; at 1+, a rising 16th arpeggio and 16th ticks | thumps carry the low end |
| dream | 69 | lydian | pad / +lead / +bass / +breath noise | glass pads with vibrato, a long echo |
| glitch rift | 100 | phrygian | pad+bass / all / offbeat plucks | fragments, soft rolls, noise that jumps sides |
| ending | 75 | ionian | pad+bass / +lead / +brushes | phrases end on IV or a sus V (a question) |

## Biome palettes (key, timbre, motif)

| biome | key | wave (bass) | lead / harmony | kit (4 sounds) | colour |
|---|---|---|---|---|---|
| office | D | round sine | warm 50 % swell | paper: tick, brush, thump, page | echo 3 rows |
| vents | E | hollow (+3rd harmonic) | 50 % slow-decay lead, soft arpeggio | air: tick, breath, thump, rattle | ticks on the right |
| flooded atrium | B | pure sine | glass pad with vibrato | drip | echo 5 rows |
| server farm | C | organ (+2nd, +3rd) | 25 % short-pluck arpeggio | hum | echo 2 rows |
| mall food court | G | round sine | brighter 25 % lead | brush | swung 16ths, no echo |
| dream garden | Eb | glass | slow-attack lead, deep vibrato | breath | echo 6 rows |
| rooftop sky | A | soft triangle | warm lead | wind | ticks on the right |
| glitch core | F# | stepped sine | 25 % arpeggio, hollow lead | static | the noise side flips by bar |

Each biome has a four-note motif, written in key degrees (for example the office's 5-3-2-1). It's played as the lead's
answer at the end of a phrase, so a floor is recognisable in any mood.

## The semi-generative scheme

- **Authored per mood** (`compose.py`): 3-4 chord progressions (four bars, one chord a bar), a form of phrase letters
  (A A B A), and grooves per layer in a low tier and a high tier. Each groove is a 4-bar set of 16-row patterns.
  Patterns name chord tones (root, 3rd, 5th, 7th/9th and octaves), scale degrees or motif notes, never absolute
  pitches.
- **The seed decides** the first time each letter plays in a cycle. It picks the progression, a groove per layer and
  a lead transform: answer with the biome motif in bar 4, thin out the odd notes, hush bar 3, or lift an octave. The
  second time a letter plays (A'), the answer and thinning flip. After the cycle, every choice is rolled again. The
  stream belongs to the music (a stride through a shuffled table) and never touches gameplay RNG, so replays stay
  deterministic.
- **Resolved at play time**: key (biome) + mode (mood) + the bar's chord (+ sus/add9). Every combination stays
  diatonic. A lead or motif note of 6 rows or more snaps to a chord tone, so long notes never rub against the
  harmony.
- **Layers by game state**: `egm_intensity(0..3)` picks the mask of heard layers and the groove tier. A layer that
  enters ramps in over a bar, and one that leaves ramps out over a bar. Collapse simply has no bass layer.

## Transitions (bar-quantised, crossfaded on 4 channels)

`egm_go(mood, biome, mode)` lands on the next bar line:
- **XFADE**: the old song leans back to 10/16 before the line. On the line, the new song takes the channels it uses
  (from 8/16, up to full in half a bar). The channels it doesn't use keep playing the *old* song's notes and fade out
  over a bar. A second cursor plays these tails at the old tempo, and the new downbeat is aligned to the old bar line.
- **DROP**: the layers the new song lacks ring to the bar line, then are cut on the new downbeat. That's the
  combat→collapse bass dropout.
- **BEAT**: the change lands on the next beat with a hard cut, for stingers.

## SFX in the music

- **Tonal effects** are written in the same chord-tone / degree language and resolved against the current key and
  chord when they fire. A pickup arpeggiates the bar's chord, a chime rises 5th→octave, and a key or door climbs the
  chord. They're always in key and consonant.
- **Channels**: tonal effects borrow pulse 1 (the harmony), and impacts borrow noise. The lead and the bass are never
  taken.
- **Priority** (0 step, 1 pickup/chime/deny, 2 key/hit/door, 3 hurt): a lower effect can't take a channel from a
  higher one.
- **Ducking**: while an effect plays, the music's other channels drop 4/16 (about -2.5 dB) and recover a step every
  6 frames. When the effect ends, a held music note comes back at its level.
- **Volumes**: MUSIC 0-8 scales every music level (with fades: `egm_music_volume(v, frames_per_step)`), and SFX 0-8
  scales the effects. Master NR50 ramps are for pause and game over. The bass never vanishes before the pulses at
  low volume.

## Budgets (measured)

- **Data**: 3,768 bytes for 9 moods, 8 biomes, 10 instruments, 7 waves, 8 kits and 8 effects. That breaks down as:
  - 126 unique patterns: 1,466 bytes
  - sets: 370 bytes
  - songs: 526 bytes
  - lookup tables: 842 bytes
- **Code**: about 10 KB (SDCC).
- **WRAM**: 368 bytes, including two 93-byte cursors.
- **CPU** (CGB double speed, measured in PyBoy with an idle-loop method; DIV reads mid-frame proved unreliable):
  - mean 5.3-7.5 % of a frame
  - p95 7.7-14.7 %
  - worst 21-31 % on song-start and song-change frames
- **Decode-ahead keeps row frames cheap**: rows are decoded one channel per frame during the frames before they play,
  so a row's own frame mostly writes registers.

That CPU cost is about 3× CRUCIBLE's documented 1.9 % mean. Bringing it to ≤3 % mean and ≤8 % worst would take
three changes: state in globals rather than struct pointers (SDCC's main cost), assembly for `resolve`/`pulse_on`,
and spreading song changes over two frames.

## The proof (`docs/audio/`)

- **How it was made**: `build.sh` composes the data and builds `build/egm_test.gbc`. `render.py` then plays scripted
  scenarios in PyBoy 2.7.0 and records the APU each frame.
- **What's exact**: pitches, timing, envelopes, panning and relative levels are the cartridge's register writes on an
  emulated APU (2 pulses, wave, 15/7-bit LFSR noise, NR51).
- **Corrected in post**: PyBoy lacks a few things, so `render.py` adds them:
  - NR50 is read back every frame and applied.
  - A 20 Hz first-order high-pass stands in for the output capacitor.
  - Recording at 192 kHz with band-limited resampling replaces PyBoy's point sampling.
  - The true 59.73 Hz frame rate is restored.
- **Not modelled**: the real DAC's analog offsets (the click and thump character at note on and off may differ a
  little), headphone-amp colour, and zombie-mode quirks (unused).
- **Listen-check proxies** are in `stats.json`. No clipping. Peaks are -6.8 to -12.3 dBFS, and RMS is -15.8 to
  -24.6 dBFS. All EGRESS takes are at or below CRUCIBLE's current recordings for brightness. EGRESS has spectral
  centroids of 358-877 Hz and 1.0-2.6 % of energy above 5 kHz. CRUCIBLE's after-bench, after-boss and final-* takes
  have centroids of 622-758 Hz and 2.1-2.7 % above 5 kHz. Pitch-class energy outside the scale is 1.6-10 %, mostly
  pulse harmonics.

## Decisions (owner, 2026-10-14)

Recorded as decided; not yet applied to the driver, data or previews (the previews in `docs/audio/` predate them
and are awaiting the owner's listening feedback).

1. Combat and boss lean toward CRUCIBLE's trailer groove: pulse-sweep kicks on pulse 1, and the harmony layer thins
   during fights.
2. The key is per biome (not per save).
3. In calm rooms, tonal SFX may borrow the lead (pulse 2) so the pad never breaks.
4. The driver runs in the main loop with frame catch-up, like CRUCIBLE's `sys_time` catch-up; VBlank stays free for
   tile and VRAM writes.
5. Hand-write hot paths in assembly if needed to reach a mean of 3 % or less of a double-speed frame. Song-change
   spikes must not drop frames.
6. The click and thump check on real hardware (flash cart) is deferred to later.

## Open questions

1. Story hooks: faction leitmotifs as biome motifs; lucidity bending dream's mode.
2. Real-hardware click and thump check (deferred, see decision 6).
